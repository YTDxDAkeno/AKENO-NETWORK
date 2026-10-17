import { and, desc, eq, or, sql } from 'drizzle-orm';
import type {
  BlockedUserDto,
  FriendDto,
  FriendRequestDto,
  FriendRequestsResponse,
  SendFriendRequestResponse,
} from '@akeno/shared';
import type { AppContext } from '../../app/context.js';
import type { DbExecutor } from '../../database/client.js';
import { friendRequests, friendships, invitations, profiles, userBlocks, users } from '../../database/schema/index.js';
import { isUniqueViolation } from '../../utils/db-errors.js';
import { Effects } from '../../utils/effects.js';
import { AppError, Errors } from '../../utils/errors.js';
import { orderedPair } from '../../utils/pairs.js';
import { toUserSummary, userCardColumns } from '../users/service.js';

/** Anti-spam limits for friend requests. */
const MAX_PENDING_OUTGOING = 50;
const MAX_REQUESTS_PER_HOUR = 30;

export class FriendsService {
  constructor(private readonly ctx: AppContext) {}

  async listFriends(userId: string): Promise<FriendDto[]> {
    const rows = await this.ctx.db
      .select({ card: userCardColumns, since: friendships.createdAt })
      .from(friendships)
      .innerJoin(
        users,
        sql`${users.id} = CASE WHEN ${friendships.userLowId} = ${userId} THEN ${friendships.userHighId} ELSE ${friendships.userLowId} END`,
      )
      .innerJoin(profiles, eq(profiles.userId, users.id))
      .where(or(eq(friendships.userLowId, userId), eq(friendships.userHighId, userId)));
    return rows
      .filter((row) => row.card.status === 'active')
      .map((row) => ({
        ...this.ctx.users.withPresence(row.card, userId, true, false),
        friendsSince: row.since.toISOString(),
        statusMessage: row.card.statusMessage,
      }))
      .sort((a, b) => a.displayName.localeCompare(b.displayName, 'en', { sensitivity: 'base' }));
  }

  async listRequests(userId: string): Promise<FriendRequestsResponse> {
    const rows = await this.ctx.db
      .select()
      .from(friendRequests)
      .where(
        and(
          eq(friendRequests.status, 'pending'),
          or(eq(friendRequests.senderId, userId), eq(friendRequests.recipientId, userId)),
        ),
      )
      .orderBy(desc(friendRequests.createdAt));
    const otherIds = rows.map((row) => (row.senderId === userId ? row.recipientId : row.senderId));
    const summaries = await this.ctx.users.getSummaries(otherIds);
    const incoming: FriendRequestDto[] = [];
    const outgoing: FriendRequestDto[] = [];
    for (const row of rows) {
      const direction = row.senderId === userId ? 'outgoing' : 'incoming';
      const user = summaries.get(direction === 'outgoing' ? row.recipientId : row.senderId);
      if (!user || user.deleted) continue;
      (direction === 'incoming' ? incoming : outgoing).push({
        id: row.id,
        direction,
        user,
        createdAt: row.createdAt.toISOString(),
      });
    }
    return { incoming, outgoing };
  }

  async sendRequest(senderId: string, recipientId: string): Promise<SendFriendRequestResponse> {
    if (senderId === recipientId) throw Errors.badRequest('You cannot send a friend request to yourself.');
    try {
      return await this.trySendRequest(senderId, recipientId);
    } catch (error) {
      // Two users requesting each other at the same moment: the pending-pair unique index makes
      // one insert fail. Retrying finds the other request and accepts it instead.
      if (isUniqueViolation(error, 'friend_requests_pending_pair_key')) {
        return this.trySendRequest(senderId, recipientId);
      }
      throw error;
    }
  }

  private async trySendRequest(senderId: string, recipientId: string): Promise<SendFriendRequestResponse> {
    const effects = new Effects();
    const result = await this.ctx.db.transaction(async (tx): Promise<SendFriendRequestResponse> => {
      const recipient = await this.ctx.users.requireActiveUser(recipientId, tx);
      if (await this.ctx.relations.isBlockedEitherWay(senderId, recipientId, tx)) {
        throw new AppError(403, 'BLOCKED', 'You cannot send a friend request to this user.');
      }
      if (await this.ctx.relations.areFriends(senderId, recipientId, tx)) {
        throw Errors.conflict('You are already friends with this user.', 'ALREADY_FRIENDS');
      }

      const existing = await this.ctx.relations.pendingRequestBetween(senderId, recipientId, tx);
      if (existing && existing.senderId === senderId) {
        throw Errors.conflict('You already sent a friend request to this user.', 'REQUEST_PENDING');
      }
      if (existing && existing.senderId === recipientId) {
        // The other user already asked: sending a request back accepts theirs.
        await this.acceptLocked(tx, existing.id, senderId, effects);
        return { status: 'accepted', request: null };
      }

      const [profile] = await tx
        .select({ friendRequestPrivacy: profiles.friendRequestPrivacy })
        .from(profiles)
        .where(eq(profiles.userId, recipientId));
      if (profile?.friendRequestPrivacy === 'nobody') {
        throw new AppError(403, 'PRIVACY_RESTRICTED', 'This user is not accepting friend requests.');
      }

      const [counts] = await tx
        .select({
          pending: sql<number>`count(*) FILTER (WHERE ${friendRequests.status} = 'pending')::int`,
          lastHour: sql<number>`count(*) FILTER (WHERE ${friendRequests.createdAt} > now() - interval '1 hour')::int`,
        })
        .from(friendRequests)
        .where(eq(friendRequests.senderId, senderId));
      if ((counts?.pending ?? 0) >= MAX_PENDING_OUTGOING) {
        throw Errors.rateLimited('You have too many pending friend requests. Wait for some to be answered first.');
      }
      if ((counts?.lastHour ?? 0) >= MAX_REQUESTS_PER_HOUR) {
        throw Errors.rateLimited('You are sending friend requests too quickly. Please try again later.');
      }

      const [request] = await tx.insert(friendRequests).values({ senderId, recipientId }).returning();
      if (!request) throw new Error('Friend request insert returned no row');
      const sender = await this.ctx.users.getSummary(senderId, tx);
      await this.ctx.notifications.create(
        recipientId,
        { type: 'friend_request.received', actor: sender, requestId: request.id },
        tx,
        effects,
      );
      effects.add(() =>
        this.ctx.hub.sendToUsers([senderId, recipientId], {
          type: 'friends.changed',
          data: { reason: 'request_received', userId: senderId },
        }),
      );
      return {
        status: 'pending',
        request: {
          id: request.id,
          direction: 'outgoing',
          user: toUserSummary(recipient),
          createdAt: request.createdAt.toISOString(),
        },
      };
    });
    await effects.run((error) => this.ctx.log.error({ err: error }, 'Friend request side effect failed'));
    return result;
  }

  async acceptRequest(userId: string, requestId: string): Promise<void> {
    const effects = new Effects();
    await this.ctx.db.transaction((tx) => this.acceptLocked(tx, requestId, userId, effects));
    await effects.run((error) => this.ctx.log.error({ err: error }, 'Friend accept side effect failed'));
  }

  /** Accepts a pending request addressed to `recipientId`. Must run inside a transaction. */
  private async acceptLocked(tx: DbExecutor, requestId: string, recipientId: string, effects: Effects): Promise<void> {
    const [request] = await tx
      .select()
      .from(friendRequests)
      .where(eq(friendRequests.id, requestId))
      .for('update')
      .limit(1);
    // Only the recipient may accept; anyone else sees "not found" to avoid leaking request IDs.
    if (!request || request.recipientId !== recipientId) throw Errors.notFound('The friend request');
    if (request.status !== 'pending') throw Errors.conflict('This friend request is no longer pending.');
    if (await this.ctx.relations.isBlockedEitherWay(request.senderId, recipientId, tx)) {
      throw new AppError(403, 'BLOCKED', 'You cannot accept this friend request.');
    }
    await this.ctx.users.requireActiveUser(request.senderId, tx);

    await tx
      .update(friendRequests)
      .set({ status: 'accepted', respondedAt: new Date() })
      .where(eq(friendRequests.id, requestId));
    const [low, high] = orderedPair(request.senderId, recipientId);
    await tx.insert(friendships).values({ userLowId: low, userHighId: high }).onConflictDoNothing();

    const accepter = await this.ctx.users.getSummary(recipientId, tx);
    await this.ctx.notifications.create(request.senderId, { type: 'friend_request.accepted', actor: accepter }, tx, effects);
    effects.add(async () => {
      this.ctx.hub.sendToUser(request.senderId, {
        type: 'friends.changed',
        data: { reason: 'request_accepted', userId: recipientId },
      });
      this.ctx.hub.sendToUser(recipientId, {
        type: 'friends.changed',
        data: { reason: 'request_accepted', userId: request.senderId },
      });
      // New friends must receive each other's current presence.
      this.ctx.presence.invalidate(request.senderId);
      this.ctx.presence.invalidate(recipientId);
      await this.ctx.presence.publish(request.senderId);
      await this.ctx.presence.publish(recipientId);
    });
  }

  async declineRequest(userId: string, requestId: string): Promise<void> {
    const updated = await this.ctx.db
      .update(friendRequests)
      .set({ status: 'declined', respondedAt: new Date() })
      .where(
        and(eq(friendRequests.id, requestId), eq(friendRequests.recipientId, userId), eq(friendRequests.status, 'pending')),
      )
      .returning();
    const request = updated[0];
    if (!request) throw Errors.notFound('The pending friend request');
    this.ctx.hub.sendToUsers([request.senderId, userId], {
      type: 'friends.changed',
      data: { reason: 'request_declined', userId },
    });
  }

  async cancelRequest(userId: string, requestId: string): Promise<void> {
    const updated = await this.ctx.db
      .update(friendRequests)
      .set({ status: 'cancelled', respondedAt: new Date() })
      .where(
        and(eq(friendRequests.id, requestId), eq(friendRequests.senderId, userId), eq(friendRequests.status, 'pending')),
      )
      .returning();
    const request = updated[0];
    if (!request) throw Errors.notFound('The pending friend request');
    this.ctx.hub.sendToUsers([request.recipientId, userId], {
      type: 'friends.changed',
      data: { reason: 'request_cancelled', userId },
    });
  }

  async removeFriend(userId: string, friendId: string): Promise<void> {
    const [low, high] = orderedPair(userId, friendId);
    const deleted = await this.ctx.db
      .delete(friendships)
      .where(and(eq(friendships.userLowId, low), eq(friendships.userHighId, high)))
      .returning();
    if (deleted.length === 0) throw Errors.notFound('The friendship');
    await this.afterRelationshipEnded(userId, friendId, 'friend_removed');
  }

  async listBlocked(userId: string): Promise<BlockedUserDto[]> {
    const rows = await this.ctx.db
      .select({ blockedId: userBlocks.blockedId, createdAt: userBlocks.createdAt })
      .from(userBlocks)
      .where(eq(userBlocks.blockerId, userId))
      .orderBy(desc(userBlocks.createdAt));
    const summaries = await this.ctx.users.getSummaries(rows.map((row) => row.blockedId));
    return rows.flatMap((row) => {
      const user = summaries.get(row.blockedId);
      return user ? [{ user, blockedAt: row.createdAt.toISOString() }] : [];
    });
  }

  /**
   * Blocking removes any friendship and pending friend requests in both directions, cancels
   * pending invitations between the two users, and prevents future interaction.
   */
  async block(userId: string, targetId: string): Promise<void> {
    if (userId === targetId) throw Errors.badRequest('You cannot block yourself.');
    await this.ctx.db.transaction(async (tx) => {
      const [target] = await tx.select({ id: users.id }).from(users).where(eq(users.id, targetId));
      if (!target) throw Errors.notFound('The user');
      await tx.insert(userBlocks).values({ blockerId: userId, blockedId: targetId }).onConflictDoNothing();
      const [low, high] = orderedPair(userId, targetId);
      await tx.delete(friendships).where(and(eq(friendships.userLowId, low), eq(friendships.userHighId, high)));
      await tx
        .update(friendRequests)
        .set({ status: 'cancelled', respondedAt: new Date() })
        .where(
          and(
            eq(friendRequests.status, 'pending'),
            or(
              and(eq(friendRequests.senderId, userId), eq(friendRequests.recipientId, targetId)),
              and(eq(friendRequests.senderId, targetId), eq(friendRequests.recipientId, userId)),
            ),
          ),
        );
      await tx
        .update(invitations)
        .set({ status: 'cancelled', respondedAt: new Date() })
        .where(
          and(
            eq(invitations.status, 'pending'),
            or(
              and(eq(invitations.inviterId, userId), eq(invitations.inviteeId, targetId)),
              and(eq(invitations.inviterId, targetId), eq(invitations.inviteeId, userId)),
            ),
          ),
        );
    });
    await this.afterRelationshipEnded(userId, targetId, 'blocked');
    this.ctx.hub.sendToUsers([userId, targetId], { type: 'invitations.changed', data: {} });
    // Users with a block never share a voice room: the blocked user is disconnected.
    this.ctx.voice.onBlock(userId, targetId);
    this.ctx.shareplay.onBlock(userId, targetId);
    this.ctx.lanplay.onBlock(userId, targetId);
  }

  async unblock(userId: string, targetId: string): Promise<void> {
    const deleted = await this.ctx.db
      .delete(userBlocks)
      .where(and(eq(userBlocks.blockerId, userId), eq(userBlocks.blockedId, targetId)))
      .returning();
    if (deleted.length === 0) throw Errors.notFound('The block');
    this.ctx.hub.sendToUser(userId, { type: 'friends.changed', data: { reason: 'unblocked', userId: targetId } });
  }

  private async afterRelationshipEnded(
    userId: string,
    otherId: string,
    reason: 'friend_removed' | 'blocked',
  ): Promise<void> {
    this.ctx.hub.sendToUser(userId, { type: 'friends.changed', data: { reason, userId: otherId } });
    // The other side is told the friendship ended, never that they were blocked.
    this.ctx.hub.sendToUser(otherId, { type: 'friends.changed', data: { reason: 'friend_removed', userId } });
    // Former friends must no longer see presence: send them an explicit offline view.
    const offline = { status: 'offline' as const, lastSeenAt: null, activity: null, game: null };
    this.ctx.hub.sendToUser(otherId, { type: 'presence.updated', data: { userId, presence: offline } });
    this.ctx.hub.sendToUser(userId, { type: 'presence.updated', data: { userId: otherId, presence: offline } });
  }
}
