import { and, asc, desc, eq, gt, inArray, lt, ne, sql } from 'drizzle-orm';
import type {
  ConversationKind,
  DirectConversationDto,
  MessageDto,
  MessagePage,
  MessagePageQuery,
  SendMessageResponse,
  UserSummary,
} from '@akeno/shared';
import type { AppContext } from '../../app/context.js';
import type { DbExecutor } from '../../database/client.js';
import {
  conversationReadStates,
  conversations,
  groupMembers,
  groups,
  messages,
  parties,
  partyMembers,
  profiles,
} from '../../database/schema/index.js';
import { AppError, Errors } from '../../utils/errors.js';
import { orderedPair } from '../../utils/pairs.js';
import { placeholderSummary } from '../users/service.js';

type MessageRow = typeof messages.$inferSelect;

export interface ConversationAccess {
  id: string;
  kind: ConversationKind;
  /** Everyone who may read the conversation (recipients of realtime events). */
  memberIds: string[];
  canSend: boolean;
  cannotSendReason: AppError | null;
  otherUserId: string | null;
  groupId: string | null;
  partyId: string | null;
}

interface SendCheck {
  canSend: boolean;
  reason: AppError | null;
}

export class MessagesService {
  /** Short-lived cache for typing-indicator authorization to avoid a query per keystroke burst. */
  private readonly typingAccessCache = new Map<string, { memberIds: string[]; expiresAt: number }>();

  constructor(private readonly ctx: AppContext) {}

  async createConversation(executor: DbExecutor, kind: 'group' | 'party'): Promise<string> {
    const [row] = await executor.insert(conversations).values({ kind }).returning({ id: conversations.id });
    if (!row) throw new Error('Conversation insert returned no row');
    return row.id;
  }

  /**
   * Resolves whether `userId` may read a conversation. Non-members get NOT_FOUND so that the
   * existence of private conversations is not revealed.
   */
  async resolveAccess(userId: string, conversationId: string, executor: DbExecutor = this.ctx.db): Promise<ConversationAccess> {
    const [conversation] = await executor.select().from(conversations).where(eq(conversations.id, conversationId)).limit(1);
    if (!conversation) throw Errors.notFound('The conversation');

    if (conversation.kind === 'direct') {
      const low = conversation.directUserLowId!;
      const high = conversation.directUserHighId!;
      if (userId !== low && userId !== high) throw Errors.notFound('The conversation');
      const otherUserId = userId === low ? high : low;
      const check = await this.checkDirectSend(userId, otherUserId, executor);
      return {
        id: conversation.id,
        kind: 'direct',
        memberIds: [low, high],
        canSend: check.canSend,
        cannotSendReason: check.reason,
        otherUserId,
        groupId: null,
        partyId: null,
      };
    }

    if (conversation.kind === 'group') {
      const [group] = await executor.select({ id: groups.id }).from(groups).where(eq(groups.conversationId, conversationId));
      if (!group) throw Errors.notFound('The conversation');
      const members = await executor
        .select({ userId: groupMembers.userId })
        .from(groupMembers)
        .where(eq(groupMembers.groupId, group.id));
      const memberIds = members.map((member) => member.userId);
      if (!memberIds.includes(userId)) throw Errors.notFound('The conversation');
      return {
        id: conversation.id,
        kind: 'group',
        memberIds,
        canSend: true,
        cannotSendReason: null,
        otherUserId: null,
        groupId: group.id,
        partyId: null,
      };
    }

    const [party] = await executor.select({ id: parties.id }).from(parties).where(eq(parties.conversationId, conversationId));
    if (!party) throw Errors.notFound('The conversation');
    const members = await executor
      .select({ userId: partyMembers.userId })
      .from(partyMembers)
      .where(eq(partyMembers.partyId, party.id));
    const memberIds = members.map((member) => member.userId);
    if (!memberIds.includes(userId)) throw Errors.notFound('The conversation');
    return {
      id: conversation.id,
      kind: 'party',
      memberIds,
      canSend: true,
      cannotSendReason: null,
      otherUserId: null,
      groupId: null,
      partyId: party.id,
    };
  }

  /** Direct messages require an active recipient, no block in either direction, and privacy consent. */
  private async checkDirectSend(userId: string, otherUserId: string, executor: DbExecutor): Promise<SendCheck> {
    const cards = await this.ctx.users.loadCards([otherUserId], executor);
    const other = cards.get(otherUserId);
    if (!other || other.status !== 'active') {
      return { canSend: false, reason: new AppError(403, 'FORBIDDEN', 'This account is no longer available.') };
    }
    if (await this.ctx.relations.isBlockedEitherWay(userId, otherUserId, executor)) {
      return { canSend: false, reason: new AppError(403, 'BLOCKED', 'You cannot send messages to this user.') };
    }
    const [privacy] = await executor
      .select({ messagePrivacy: profiles.messagePrivacy })
      .from(profiles)
      .where(eq(profiles.userId, otherUserId));
    if (privacy?.messagePrivacy === 'friends' && !(await this.ctx.relations.areFriends(userId, otherUserId, executor))) {
      return {
        canSend: false,
        reason: new AppError(403, 'PRIVACY_RESTRICTED', 'This user only accepts messages from friends.'),
      };
    }
    return { canSend: true, reason: null };
  }

  async findOrCreateDirect(userId: string, otherUserId: string): Promise<DirectConversationDto> {
    if (userId === otherUserId) throw Errors.badRequest('You cannot start a conversation with yourself.');
    const [low, high] = orderedPair(userId, otherUserId);
    const existing = await this.findDirect(low, high);
    if (existing) return this.getDirect(userId, existing);

    const check = await this.checkDirectSend(userId, otherUserId, this.ctx.db);
    if (!check.canSend) throw check.reason!;
    // ON CONFLICT DO NOTHING makes concurrent creation safe; the partial unique index keeps one row per pair.
    await this.ctx.db
      .insert(conversations)
      .values({ kind: 'direct', directUserLowId: low, directUserHighId: high })
      .onConflictDoNothing();
    const id = await this.findDirect(low, high);
    if (!id) throw new Error('Direct conversation could not be created');
    return this.getDirect(userId, id);
  }

  private async findDirect(low: string, high: string): Promise<string | null> {
    const [row] = await this.ctx.db
      .select({ id: conversations.id })
      .from(conversations)
      .where(
        and(
          eq(conversations.kind, 'direct'),
          eq(conversations.directUserLowId, low),
          eq(conversations.directUserHighId, high),
        ),
      )
      .limit(1);
    return row?.id ?? null;
  }

  async listDirect(userId: string): Promise<DirectConversationDto[]> {
    const result = await this.ctx.db.execute<{
      id: string;
      created_at: Date;
      last_message_at: Date | null;
      other_id: string;
      last_read: string;
      unread: number;
      lm_id: string | null;
      lm_sender: string | null;
      lm_body: string | null;
      lm_cmid: string | null;
      lm_created: Date | null;
    }>(sql`
      SELECT c.id, c.created_at, c.last_message_at,
        CASE WHEN c.direct_user_low_id = ${userId} THEN c.direct_user_high_id ELSE c.direct_user_low_id END AS other_id,
        COALESCE(rs.last_read_message_id, 0) AS last_read,
        (SELECT count(*)::int FROM messages m
          WHERE m.conversation_id = c.id
            AND m.id > COALESCE(rs.last_read_message_id, 0)
            AND m.sender_id <> ${userId}) AS unread,
        lm.id AS lm_id, lm.sender_id AS lm_sender, lm.body AS lm_body,
        lm.client_message_id AS lm_cmid, lm.created_at AS lm_created
      FROM conversations c
      LEFT JOIN conversation_read_states rs ON rs.conversation_id = c.id AND rs.user_id = ${userId}
      LEFT JOIN LATERAL (
        SELECT id, sender_id, body, client_message_id, created_at FROM messages
        WHERE conversation_id = c.id ORDER BY id DESC LIMIT 1
      ) lm ON true
      WHERE c.kind = 'direct' AND (c.direct_user_low_id = ${userId} OR c.direct_user_high_id = ${userId})
      ORDER BY COALESCE(c.last_message_at, c.created_at) DESC
      LIMIT 200
    `);
    const rows = result.rows;
    const otherIds = rows.map((row) => row.other_id);
    const [withPresence, senderSummaries, blocked, friends] = await Promise.all([
      this.ctx.users.getWithPresence(userId, otherIds),
      this.ctx.users.getSummaries(rows.flatMap((row) => (row.lm_sender ? [row.lm_sender] : []))),
      this.ctx.relations.blockedSet(userId, otherIds),
      this.ctx.relations.friendSet(userId, otherIds),
    ]);
    const privacyRows =
      otherIds.length > 0
        ? await this.ctx.db
            .select({ userId: profiles.userId, messagePrivacy: profiles.messagePrivacy })
            .from(profiles)
            .where(inArray(profiles.userId, otherIds))
        : [];
    const privacy = new Map(privacyRows.map((row) => [row.userId, row.messagePrivacy]));

    return rows.flatMap((row) => {
      const otherUser = withPresence.get(row.other_id);
      if (!otherUser) return [];
      const canSend =
        !otherUser.deleted &&
        !blocked.has(row.other_id) &&
        (privacy.get(row.other_id) === 'everyone' || friends.has(row.other_id));
      const lastMessage: MessageDto | null =
        row.lm_id && row.lm_sender && row.lm_body !== null && row.lm_created
          ? {
              id: Number(row.lm_id),
              conversationId: row.id,
              sender: senderSummaries.get(row.lm_sender) ?? unknownSender(row.lm_sender),
              body: row.lm_body,
              clientMessageId: row.lm_cmid,
              createdAt: new Date(row.lm_created).toISOString(),
            }
          : null;
      return [
        {
          id: row.id,
          kind: 'direct' as const,
          otherUser,
          lastMessage,
          lastMessageAt: row.last_message_at ? new Date(row.last_message_at).toISOString() : null,
          unreadCount: Number(row.unread),
          lastReadMessageId: Number(row.last_read),
          canSend,
        },
      ];
    });
  }

  async getDirect(userId: string, conversationId: string): Promise<DirectConversationDto> {
    const access = await this.resolveAccess(userId, conversationId);
    if (access.kind !== 'direct') throw Errors.notFound('The conversation');
    const conversation = (await this.listDirect(userId)).find((entry) => entry.id === conversationId);
    if (!conversation) throw Errors.notFound('The conversation');
    return conversation;
  }

  async listMessages(userId: string, conversationId: string, query: MessagePageQuery): Promise<MessagePage> {
    await this.resolveAccess(userId, conversationId);
    const ascending = query.after !== undefined;
    const rows = await this.ctx.db
      .select()
      .from(messages)
      .where(
        and(
          eq(messages.conversationId, conversationId),
          query.before !== undefined ? lt(messages.id, query.before) : undefined,
          query.after !== undefined ? gt(messages.id, query.after) : undefined,
        ),
      )
      .orderBy(ascending ? asc(messages.id) : desc(messages.id))
      .limit(query.limit + 1);
    const hasMore = rows.length > query.limit;
    const page = rows.slice(0, query.limit);
    if (!ascending) page.reverse();
    return { messages: await this.toDtos(page), hasMore };
  }

  async send(
    userId: string,
    conversationId: string,
    input: { body: string; clientMessageId: string },
  ): Promise<SendMessageResponse> {
    const access = await this.resolveAccess(userId, conversationId);
    if (!access.canSend) throw access.cannotSendReason ?? Errors.forbidden();

    const outcome = await this.ctx.db.transaction(async (tx) => {
      const [inserted] = await tx
        .insert(messages)
        .values({ conversationId, senderId: userId, body: input.body, clientMessageId: input.clientMessageId })
        .onConflictDoNothing({ target: [messages.senderId, messages.clientMessageId] })
        .returning();
      if (!inserted) {
        // Idempotent retry: return the message already stored for this client message ID.
        const [existing] = await tx
          .select()
          .from(messages)
          .where(and(eq(messages.senderId, userId), eq(messages.clientMessageId, input.clientMessageId)))
          .limit(1);
        if (!existing || existing.conversationId !== conversationId) {
          throw Errors.conflict('This client message ID was already used for a different conversation.');
        }
        return { row: existing, created: false };
      }
      await tx.update(conversations).set({ lastMessageAt: inserted.createdAt }).where(eq(conversations.id, conversationId));
      await this.upsertReadState(tx, conversationId, userId, inserted.id);
      return { row: inserted, created: true };
    });

    const [message] = await this.toDtos([outcome.row]);
    if (!message) throw new Error('Message could not be serialized');
    if (outcome.created) {
      // Delivery acknowledgment happens only after the message is committed to PostgreSQL.
      this.ctx.hub.sendToUsers(access.memberIds, {
        type: 'message.created',
        data: { conversationId, conversationKind: access.kind, message },
      });
    }
    return { message, created: outcome.created };
  }

  async markRead(userId: string, conversationId: string, lastReadMessageId: number): Promise<{ lastReadMessageId: number }> {
    await this.resolveAccess(userId, conversationId);
    const [latest] = await this.ctx.db
      .select({ id: sql<string | null>`max(${messages.id})` })
      .from(messages)
      .where(eq(messages.conversationId, conversationId));
    const clamped = Math.min(lastReadMessageId, Number(latest?.id ?? 0));
    const value = await this.upsertReadState(this.ctx.db, conversationId, userId, clamped);
    // Sync unread badges across the user's other devices.
    this.ctx.hub.sendToUser(userId, { type: 'conversation.read', data: { conversationId, lastReadMessageId: value } });
    return { lastReadMessageId: value };
  }

  private async upsertReadState(executor: DbExecutor, conversationId: string, userId: string, messageId: number): Promise<number> {
    const [row] = await executor
      .insert(conversationReadStates)
      .values({ conversationId, userId, lastReadMessageId: messageId })
      .onConflictDoUpdate({
        target: [conversationReadStates.conversationId, conversationReadStates.userId],
        set: {
          lastReadMessageId: sql`GREATEST(${conversationReadStates.lastReadMessageId}, excluded.last_read_message_id)`,
          updatedAt: new Date(),
        },
      })
      .returning({ lastReadMessageId: conversationReadStates.lastReadMessageId });
    return row?.lastReadMessageId ?? messageId;
  }

  /** Unread message counts for the given conversations (messages from others after the read cursor). */
  async unreadCounts(userId: string, conversationIds: string[]): Promise<Map<string, number>> {
    if (conversationIds.length === 0) return new Map();
    const rows = await this.ctx.db
      .select({ conversationId: messages.conversationId, count: sql<number>`count(*)::int` })
      .from(messages)
      .leftJoin(
        conversationReadStates,
        and(eq(conversationReadStates.conversationId, messages.conversationId), eq(conversationReadStates.userId, userId)),
      )
      .where(
        and(
          inArray(messages.conversationId, conversationIds),
          ne(messages.senderId, userId),
          sql`${messages.id} > COALESCE(${conversationReadStates.lastReadMessageId}, 0)`,
        ),
      )
      .groupBy(messages.conversationId);
    return new Map(rows.map((row) => [row.conversationId, row.count]));
  }

  /** Relays a typing indicator to the other members of a conversation the user can access. */
  async relayTyping(userId: string, conversationId: string): Promise<void> {
    const key = `${userId}:${conversationId}`;
    const now = Date.now();
    let cached = this.typingAccessCache.get(key);
    if (!cached || cached.expiresAt < now) {
      const access = await this.resolveAccess(userId, conversationId);
      if (!access.canSend) return;
      cached = { memberIds: access.memberIds, expiresAt: now + 30_000 };
      this.typingAccessCache.set(key, cached);
      if (this.typingAccessCache.size > 5000) this.typingAccessCache.clear();
    }
    this.ctx.hub.sendToUsers(
      cached.memberIds.filter((id) => id !== userId),
      { type: 'typing', data: { conversationId, userId } },
    );
  }

  async toDtos(rows: MessageRow[]): Promise<MessageDto[]> {
    const senders = await this.ctx.users.getSummaries(rows.map((row) => row.senderId));
    return rows.map((row) => ({
      id: row.id,
      conversationId: row.conversationId,
      sender: senders.get(row.senderId) ?? unknownSender(row.senderId),
      body: row.body,
      clientMessageId: row.clientMessageId,
      createdAt: row.createdAt.toISOString(),
    }));
  }
}

function unknownSender(id: string): UserSummary {
  return placeholderSummary(id);
}
