import { and, asc, desc, eq, gt, inArray, ne, or, sql } from 'drizzle-orm';
import type { PartyDto, PartyMemberDto, ProfilePartyDto, createPartySchema, updatePartySchema } from '@akeno/shared';
import type { z } from 'zod';
import type { AppContext } from '../../app/context.js';
import type { DbExecutor } from '../../database/client.js';
import { conversations, invitations, parties, partyMembers, profiles, users } from '../../database/schema/index.js';
import { Effects } from '../../utils/effects.js';
import { AppError, Errors } from '../../utils/errors.js';
import { userCardColumns } from '../users/service.js';

type PartyRow = typeof parties.$inferSelect;

export class PartiesService {
  constructor(private readonly ctx: AppContext) {}

  private async runWithEffects<T>(work: (tx: DbExecutor, effects: Effects) => Promise<T>): Promise<T> {
    const effects = new Effects();
    const result = await this.ctx.db.transaction((tx) => work(tx, effects));
    await effects.run((error) => this.ctx.log.error({ err: error }, 'Party side effect failed'));
    return result;
  }

  async memberIds(partyId: string, executor: DbExecutor = this.ctx.db): Promise<string[]> {
    const rows = await executor
      .select({ userId: partyMembers.userId })
      .from(partyMembers)
      .where(eq(partyMembers.partyId, partyId))
      .orderBy(asc(partyMembers.joinedAt));
    return rows.map((row) => row.userId);
  }

  async currentPartyId(userId: string, executor: DbExecutor = this.ctx.db): Promise<string | null> {
    const [row] = await executor
      .select({ partyId: partyMembers.partyId })
      .from(partyMembers)
      .where(eq(partyMembers.userId, userId))
      .limit(1);
    return row?.partyId ?? null;
  }

  private async lockParty(partyId: string, executor: DbExecutor): Promise<PartyRow> {
    const [party] = await executor.select().from(parties).where(eq(parties.id, partyId)).for('update').limit(1);
    if (!party) throw Errors.notFound('The party');
    return party;
  }

  private async toDtos(viewerId: string, rows: PartyRow[]): Promise<PartyDto[]> {
    if (rows.length === 0) return [];
    const memberRows = await this.ctx.db
      .select({ partyId: partyMembers.partyId, joinedAt: partyMembers.joinedAt, card: userCardColumns })
      .from(partyMembers)
      .innerJoin(users, eq(users.id, partyMembers.userId))
      .innerJoin(profiles, eq(profiles.userId, users.id))
      .where(
        inArray(
          partyMembers.partyId,
          rows.map((row) => row.id),
        ),
      )
      .orderBy(asc(partyMembers.joinedAt));
    const userIds = memberRows.map((row) => row.card.id);
    const [friends, blocked] = await Promise.all([
      this.ctx.relations.friendSet(viewerId, userIds),
      this.ctx.relations.blockedSet(viewerId, userIds),
    ]);
    return rows.map((party) => {
      const members = memberRows
        .filter((row) => row.partyId === party.id)
        .map((row) => ({
          user: this.ctx.users.withPresence(row.card, viewerId, friends.has(row.card.id), blocked.has(row.card.id)),
          joinedAt: row.joinedAt.toISOString(),
          isLeader: row.card.id === party.leaderId,
          voice: { connected: false, muted: null } as PartyMemberDto['voice'],
        }));
      const isMember = members.some((member) => member.user.id === viewerId);
      // Who is in voice is only shown to members of the party.
      if (isMember) for (const member of members) member.voice = this.ctx.voice.memberState(party.id, member.user.id);
      return {
        id: party.id,
        name: party.name,
        privacy: party.privacy,
        leaderId: party.leaderId,
        maxMembers: party.maxMembers,
        memberCount: party.memberCount,
        createdAt: party.createdAt.toISOString(),
        isMember,
        conversationId: isMember ? party.conversationId : null,
        members,
        voice: this.ctx.voice.statusFor(isMember),
      };
    });
  }

  async current(userId: string): Promise<PartyDto | null> {
    const partyId = await this.currentPartyId(userId);
    if (!partyId) return null;
    const [row] = await this.ctx.db.select().from(parties).where(eq(parties.id, partyId));
    if (!row) return null;
    const [dto] = await this.toDtos(userId, [row]);
    return dto ?? null;
  }

  /** The party a profile owner is in, when the viewer may see that party. */
  async profileParty(viewerId: string, targetId: string): Promise<ProfilePartyDto | null> {
    const partyId = await this.currentPartyId(targetId);
    if (!partyId) return null;
    const [party] = await this.ctx.db.select().from(parties).where(eq(parties.id, partyId));
    if (!party || !(await this.canView(viewerId, party))) return null;
    return {
      id: party.id,
      name: party.name,
      memberCount: party.memberCount,
      maxMembers: party.maxMembers,
      isMember: (await this.currentPartyId(viewerId)) === party.id,
    };
  }

  /** Parties the user could join: friends' parties open to friends, plus parties they are invited to. */
  async listJoinable(userId: string): Promise<PartyDto[]> {
    const friendIds = await this.ctx.relations.friendIds(userId);
    const invitedRows = await this.ctx.db
      .select({ partyId: invitations.partyId })
      .from(invitations)
      .where(
        and(
          eq(invitations.inviteeId, userId),
          eq(invitations.kind, 'party'),
          eq(invitations.status, 'pending'),
          gt(invitations.expiresAt, new Date()),
        ),
      );
    const invitedIds = invitedRows.flatMap((row) => (row.partyId ? [row.partyId] : []));
    const conditions = [];
    if (friendIds.length > 0) conditions.push(and(eq(parties.privacy, 'friends'), inArray(parties.leaderId, friendIds)));
    if (invitedIds.length > 0) conditions.push(inArray(parties.id, invitedIds));
    if (conditions.length === 0) return [];
    const currentId = await this.currentPartyId(userId);
    const rows = await this.ctx.db
      .select()
      .from(parties)
      .where(and(or(...conditions), currentId ? ne(parties.id, currentId) : undefined))
      .orderBy(desc(parties.createdAt))
      .limit(50);
    return this.toDtos(userId, rows);
  }

  private async canView(userId: string, party: PartyRow): Promise<boolean> {
    if ((await this.memberIds(party.id)).includes(userId)) return true;
    if (await this.hasPendingInvitation(party.id, userId)) return true;
    if (party.privacy === 'friends' && (await this.ctx.relations.areFriends(userId, party.leaderId))) return true;
    return false;
  }

  private async hasPendingInvitation(partyId: string, userId: string, executor: DbExecutor = this.ctx.db): Promise<string | null> {
    const [row] = await executor
      .select({ id: invitations.id })
      .from(invitations)
      .where(
        and(
          eq(invitations.partyId, partyId),
          eq(invitations.inviteeId, userId),
          eq(invitations.status, 'pending'),
          gt(invitations.expiresAt, new Date()),
        ),
      )
      .limit(1);
    return row?.id ?? null;
  }

  async get(userId: string, partyId: string): Promise<PartyDto> {
    const [party] = await this.ctx.db.select().from(parties).where(eq(parties.id, partyId));
    if (!party || !(await this.canView(userId, party))) throw Errors.notFound('The party');
    const [dto] = await this.toDtos(userId, [party]);
    if (!dto) throw Errors.notFound('The party');
    return dto;
  }

  /** Creating a party leaves the user's current party, mirroring how console parties behave. */
  async create(userId: string, input: z.output<typeof createPartySchema>): Promise<PartyDto> {
    const partyId = await this.runWithEffects(async (tx, effects) => {
      await this.leaveCurrentInTx(tx, userId, effects);
      const me = await this.ctx.users.getSummary(userId, tx);
      const conversationId = await this.ctx.messages.createConversation(tx, 'party');
      const [party] = await tx
        .insert(parties)
        .values({
          name: input.name && input.name.length > 0 ? input.name : `${me.displayName}'s Party`.slice(0, 48),
          leaderId: userId,
          privacy: input.privacy,
          maxMembers: input.maxMembers,
          memberCount: 1,
          conversationId,
        })
        .returning();
      if (!party) throw new Error('Party insert returned no row');
      await tx.insert(partyMembers).values({ partyId: party.id, userId });
      effects.add(() => this.notifyChanged([userId], party.id));
      return party.id;
    });
    return this.get(userId, partyId);
  }

  async update(userId: string, partyId: string, input: z.output<typeof updatePartySchema>): Promise<PartyDto> {
    await this.runWithEffects(async (tx, effects) => {
      const party = await this.lockParty(partyId, tx);
      if (party.leaderId !== userId) throw Errors.forbidden('Only the party leader can change party settings.');
      await tx
        .update(parties)
        .set({ ...input, updatedAt: new Date() })
        .where(eq(parties.id, partyId));
      const memberIds = await this.memberIds(partyId, tx);
      effects.add(() => this.notifyChanged(memberIds, partyId));
    });
    return this.get(userId, partyId);
  }

  async join(userId: string, partyId: string): Promise<PartyDto> {
    const invitationId = await this.hasPendingInvitation(partyId, userId);
    if (invitationId) {
      await this.ctx.invitations.accept(userId, invitationId);
      return this.get(userId, partyId);
    }
    await this.runWithEffects(async (tx, effects) => {
      const [party] = await tx.select().from(parties).where(eq(parties.id, partyId));
      if (!party) throw Errors.notFound('The party');
      const eligible = party.privacy === 'friends' && (await this.ctx.relations.areFriends(userId, party.leaderId, tx));
      if (!eligible) throw Errors.notFound('The party');
      await this.joinInTx(tx, partyId, userId, effects);
    });
    return this.get(userId, partyId);
  }

  /**
   * Adds a user to a party inside a transaction. Leaves the user's previous party first.
   * Capacity is enforced atomically with a conditional UPDATE plus a CHECK constraint.
   */
  async joinInTx(tx: DbExecutor, partyId: string, userId: string, effects: Effects): Promise<void> {
    const currentId = await this.currentPartyId(userId, tx);
    if (currentId === partyId) return;
    if (currentId) await this.leaveCurrentInTx(tx, userId, effects);

    await this.lockParty(partyId, tx);
    // Nobody joins a party (its chat and voice) together with someone they blocked or who blocked them.
    const members = await this.memberIds(partyId, tx);
    if ((await this.ctx.relations.blockedSet(userId, members, tx)).size > 0) {
      throw new AppError(403, 'BLOCKED', 'You cannot join this party.');
    }
    const [updated] = await tx
      .update(parties)
      .set({ memberCount: sql`${parties.memberCount} + 1`, updatedAt: new Date() })
      .where(and(eq(parties.id, partyId), sql`${parties.memberCount} < ${parties.maxMembers}`))
      .returning({ id: parties.id });
    if (!updated) throw new AppError(409, 'CAPACITY_REACHED', 'This party is full.');
    await tx.insert(partyMembers).values({ partyId, userId });
    await tx
      .update(invitations)
      .set({ status: 'accepted', respondedAt: new Date() })
      .where(and(eq(invitations.partyId, partyId), eq(invitations.inviteeId, userId), eq(invitations.status, 'pending')));
    const memberIds = await this.memberIds(partyId, tx);
    effects.add(() => {
      this.notifyChanged(memberIds, partyId);
      this.ctx.hub.sendToUser(userId, { type: 'invitations.changed', data: {} });
    });
  }

  async leave(userId: string, partyId: string): Promise<void> {
    await this.runWithEffects(async (tx, effects) => {
      const currentId = await this.currentPartyId(userId, tx);
      if (currentId !== partyId) throw new AppError(404, 'NOT_MEMBER', 'You are not in this party.');
      await this.leaveCurrentInTx(tx, userId, effects);
    });
  }

  /**
   * Removes the user from their current party (if any). The longest-standing remaining member
   * becomes leader if the leader leaves; an empty party is deleted with its chat history.
   */
  async leaveCurrentInTx(tx: DbExecutor, userId: string, effects: Effects): Promise<void> {
    const partyId = await this.currentPartyId(userId, tx);
    if (!partyId) return;
    const party = await this.lockParty(partyId, tx);
    await tx.delete(partyMembers).where(and(eq(partyMembers.partyId, partyId), eq(partyMembers.userId, userId)));
    const remaining = await this.memberIds(partyId, tx);
    if (remaining.length === 0) {
      await tx.delete(conversations).where(eq(conversations.id, party.conversationId));
      effects.add(() => this.notifyChanged([userId], partyId, true));
      effects.add(() => this.ctx.voice.closeRoomLater(partyId));
      effects.add(() => this.ctx.shareplay.onPartyClosed(partyId));
      effects.add(() => this.ctx.lanplay.onPartyClosed(partyId));
      return;
    }
    await tx
      .update(parties)
      .set({
        memberCount: remaining.length,
        leaderId: party.leaderId === userId ? remaining[0]! : party.leaderId,
        updatedAt: new Date(),
      })
      .where(eq(parties.id, partyId));
    effects.add(() => {
      this.notifyChanged([userId], partyId, true);
      this.notifyChanged(remaining, partyId);
    });
    // Leaving or being removed ends the voice connection at once.
    effects.add(() => this.ctx.voice.revoke(partyId, userId));
    effects.add(() => this.ctx.shareplay.onMemberLeft(partyId, userId));
    effects.add(() => this.ctx.lanplay.onMemberLeft(partyId, userId));
  }

  async kick(leaderId: string, partyId: string, targetId: string): Promise<void> {
    if (leaderId === targetId) throw Errors.badRequest('Use "Leave party" to leave the party yourself.');
    await this.runWithEffects(async (tx, effects) => {
      const party = await this.lockParty(partyId, tx);
      if (party.leaderId !== leaderId) throw Errors.forbidden('Only the party leader can remove members.');
      if ((await this.currentPartyId(targetId, tx)) !== partyId) {
        throw new AppError(404, 'NOT_MEMBER', 'That user is not in this party.');
      }
      await this.leaveCurrentInTx(tx, targetId, effects);
    });
  }

  async transferLeader(leaderId: string, partyId: string, newLeaderId: string): Promise<PartyDto> {
    await this.runWithEffects(async (tx, effects) => {
      const party = await this.lockParty(partyId, tx);
      if (party.leaderId !== leaderId) throw Errors.forbidden('Only the party leader can hand over leadership.');
      const memberIds = await this.memberIds(partyId, tx);
      if (!memberIds.includes(newLeaderId)) throw new AppError(404, 'NOT_MEMBER', 'That user is not in this party.');
      await tx.update(parties).set({ leaderId: newLeaderId, updatedAt: new Date() }).where(eq(parties.id, partyId));
      effects.add(() => this.notifyChanged(memberIds, partyId));
    });
    return this.get(leaderId, partyId);
  }

  /** Closing disbands the party for everyone and deletes its chat. */
  async close(leaderId: string, partyId: string): Promise<void> {
    await this.runWithEffects(async (tx, effects) => {
      const party = await this.lockParty(partyId, tx);
      if (party.leaderId !== leaderId) throw Errors.forbidden('Only the party leader can close the party.');
      const memberIds = await this.memberIds(partyId, tx);
      const invited = await tx
        .select({ inviteeId: invitations.inviteeId })
        .from(invitations)
        .where(and(eq(invitations.partyId, partyId), eq(invitations.status, 'pending')));
      await tx.delete(conversations).where(eq(conversations.id, party.conversationId));
      effects.add(() => this.ctx.voice.closeRoomLater(partyId));
      effects.add(() => this.ctx.shareplay.onPartyClosed(partyId));
      effects.add(() => this.ctx.lanplay.onPartyClosed(partyId));
      effects.add(() => {
        this.notifyChanged(memberIds, partyId, true);
        this.ctx.hub.sendToUsers(
          invited.map((row) => row.inviteeId),
          { type: 'invitations.changed', data: {} },
        );
      });
    });
  }

  notifyChanged(userIds: string[], partyId: string, removed = false): void {
    this.ctx.hub.sendToUsers(userIds, { type: 'party.changed', data: { partyId, removed } });
  }
}
