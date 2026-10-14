import { and, asc, desc, eq, gt, inArray, notInArray, sql } from 'drizzle-orm';
import type { GroupDetailDto, GroupDto, GroupRole } from '@akeno/shared';
import type { createGroupSchema, updateGroupSchema } from '@akeno/shared';
import type { z } from 'zod';
import type { AppContext } from '../../app/context.js';
import type { DbExecutor } from '../../database/client.js';
import { conversations, groupMembers, groups, invitations, profiles, users } from '../../database/schema/index.js';
import { Effects } from '../../utils/effects.js';
import { AppError, Errors } from '../../utils/errors.js';
import { placeholderSummary, userCardColumns } from '../users/service.js';

type GroupRow = typeof groups.$inferSelect;

export const MAX_GROUP_MEMBERS = 100;
const MAX_GROUPS_OWNED = 25;

export class GroupsService {
  constructor(private readonly ctx: AppContext) {}

  private async runWithEffects<T>(work: (tx: DbExecutor, effects: Effects) => Promise<T>): Promise<T> {
    const effects = new Effects();
    const result = await this.ctx.db.transaction((tx) => work(tx, effects));
    await effects.run((error) => this.ctx.log.error({ err: error }, 'Group side effect failed'));
    return result;
  }

  async memberRole(groupId: string, userId: string, executor: DbExecutor = this.ctx.db): Promise<GroupRole | null> {
    const [row] = await executor
      .select({ role: groupMembers.role })
      .from(groupMembers)
      .where(and(eq(groupMembers.groupId, groupId), eq(groupMembers.userId, userId)))
      .limit(1);
    return row?.role ?? null;
  }

  async memberIds(groupId: string, executor: DbExecutor = this.ctx.db): Promise<string[]> {
    const rows = await executor
      .select({ userId: groupMembers.userId })
      .from(groupMembers)
      .where(eq(groupMembers.groupId, groupId));
    return rows.map((row) => row.userId);
  }

  private async requireGroup(groupId: string, executor: DbExecutor, lock = false): Promise<GroupRow> {
    const query = executor.select().from(groups).where(eq(groups.id, groupId)).limit(1);
    const [group] = lock ? await query.for('update') : await query;
    if (!group) throw Errors.notFound('The group');
    return group;
  }

  private async toDtos(userId: string, rows: GroupRow[]): Promise<GroupDto[]> {
    if (rows.length === 0) return [];
    const ids = rows.map((row) => row.id);
    const [counts, owners, mine] = await Promise.all([
      this.ctx.db
        .select({ groupId: groupMembers.groupId, count: sql<number>`count(*)::int` })
        .from(groupMembers)
        .where(inArray(groupMembers.groupId, ids))
        .groupBy(groupMembers.groupId),
      this.ctx.db
        .select({ groupId: groupMembers.groupId, userId: groupMembers.userId })
        .from(groupMembers)
        .where(and(inArray(groupMembers.groupId, ids), eq(groupMembers.role, 'owner'))),
      this.ctx.db
        .select({ groupId: groupMembers.groupId, role: groupMembers.role })
        .from(groupMembers)
        .where(and(inArray(groupMembers.groupId, ids), eq(groupMembers.userId, userId))),
    ]);
    const countMap = new Map(counts.map((row) => [row.groupId, row.count]));
    const ownerMap = new Map(owners.map((row) => [row.groupId, row.userId]));
    const roleMap = new Map(mine.map((row) => [row.groupId, row.role]));
    const ownerSummaries = await this.ctx.users.getSummaries([...ownerMap.values()]);
    const memberConversations = rows.filter((row) => roleMap.has(row.id)).map((row) => row.conversationId);
    const unread = await this.ctx.messages.unreadCounts(userId, memberConversations);

    return rows.map((row) => {
      const myRole = roleMap.get(row.id) ?? null;
      const ownerId = ownerMap.get(row.id);
      return {
        id: row.id,
        name: row.name,
        description: row.description,
        joinPolicy: row.joinPolicy,
        owner: (ownerId && ownerSummaries.get(ownerId)) || placeholderSummary(ownerId ?? ''),
        memberCount: countMap.get(row.id) ?? 0,
        myRole,
        conversationId: myRole ? row.conversationId : null,
        unreadCount: myRole ? (unread.get(row.conversationId) ?? 0) : 0,
        createdAt: row.createdAt.toISOString(),
        updatedAt: row.updatedAt.toISOString(),
      };
    });
  }

  async listMine(userId: string): Promise<GroupDto[]> {
    const rows = await this.ctx.db
      .select({ group: groups })
      .from(groupMembers)
      .innerJoin(groups, eq(groups.id, groupMembers.groupId))
      .where(eq(groupMembers.userId, userId))
      .orderBy(asc(groups.name));
    return this.toDtos(
      userId,
      rows.map((row) => row.group),
    );
  }

  /** Open groups the user is not a member of. */
  async discover(userId: string): Promise<GroupDto[]> {
    const memberOf = this.ctx.db
      .select({ groupId: groupMembers.groupId })
      .from(groupMembers)
      .where(eq(groupMembers.userId, userId));
    const rows = await this.ctx.db
      .select()
      .from(groups)
      .where(and(eq(groups.joinPolicy, 'open'), notInArray(groups.id, memberOf)))
      .orderBy(desc(groups.updatedAt))
      .limit(50);
    // Hide groups owned by users with a block relationship to the viewer.
    const dtos = await this.toDtos(userId, rows);
    const blocked = await this.ctx.relations.blockedSet(
      userId,
      dtos.map((dto) => dto.owner.id),
    );
    return dtos.filter((dto) => !blocked.has(dto.owner.id));
  }

  async create(userId: string, input: z.output<typeof createGroupSchema>): Promise<GroupDetailDto> {
    const groupId = await this.runWithEffects(async (tx, effects) => {
      const [owned] = await tx
        .select({ count: sql<number>`count(*)::int` })
        .from(groupMembers)
        .where(and(eq(groupMembers.userId, userId), eq(groupMembers.role, 'owner')));
      if ((owned?.count ?? 0) >= MAX_GROUPS_OWNED) {
        throw Errors.conflict(`You can own at most ${MAX_GROUPS_OWNED} groups.`);
      }
      const conversationId = await this.ctx.messages.createConversation(tx, 'group');
      const [group] = await tx
        .insert(groups)
        .values({
          name: input.name,
          description: input.description,
          joinPolicy: input.joinPolicy,
          conversationId,
          createdById: userId,
        })
        .returning();
      if (!group) throw new Error('Group insert returned no row');
      await tx.insert(groupMembers).values({ groupId: group.id, userId, role: 'owner' });
      effects.add(() => this.notifyChanged([userId], group.id));
      return group.id;
    });
    return this.get(userId, groupId);
  }

  async get(userId: string, groupId: string): Promise<GroupDetailDto> {
    const group = await this.requireGroup(groupId, this.ctx.db);
    const myRole = await this.memberRole(groupId, userId);
    const [pendingForMe] = await this.ctx.db
      .select({ id: invitations.id })
      .from(invitations)
      .where(
        and(
          eq(invitations.groupId, groupId),
          eq(invitations.inviteeId, userId),
          eq(invitations.status, 'pending'),
          gt(invitations.expiresAt, new Date()),
        ),
      )
      .limit(1);
    // Invite-only groups are invisible to non-members without an invitation.
    if (!myRole && group.joinPolicy !== 'open' && !pendingForMe) throw Errors.notFound('The group');

    const [dto] = await this.toDtos(userId, [group]);
    if (!dto) throw Errors.notFound('The group');
    if (!myRole && (await this.ctx.relations.blockedSet(userId, [dto.owner.id])).size > 0) {
      throw Errors.notFound('The group');
    }

    const memberRows = await this.ctx.db
      .select({ card: userCardColumns, role: groupMembers.role, joinedAt: groupMembers.joinedAt })
      .from(groupMembers)
      .innerJoin(users, eq(users.id, groupMembers.userId))
      .innerJoin(profiles, eq(profiles.userId, users.id))
      .where(eq(groupMembers.groupId, groupId))
      .orderBy(sql`CASE ${groupMembers.role} WHEN 'owner' THEN 0 WHEN 'moderator' THEN 1 ELSE 2 END`, asc(groupMembers.joinedAt));
    const memberIds = memberRows.map((row) => row.card.id);
    const [friends, blocked] = await Promise.all([
      this.ctx.relations.friendSet(userId, memberIds),
      this.ctx.relations.blockedSet(userId, memberIds),
    ]);
    const members = memberRows.map((row) => ({
      user: this.ctx.users.withPresence(row.card, userId, friends.has(row.card.id), blocked.has(row.card.id)),
      role: row.role,
      joinedAt: row.joinedAt.toISOString(),
    }));

    let pendingInvitations: GroupDetailDto['pendingInvitations'] = [];
    if (myRole === 'owner' || myRole === 'moderator') {
      const rows = await this.ctx.db
        .select()
        .from(invitations)
        .where(and(eq(invitations.groupId, groupId), eq(invitations.status, 'pending'), gt(invitations.expiresAt, new Date())))
        .orderBy(desc(invitations.createdAt));
      const summaries = await this.ctx.users.getSummaries(rows.flatMap((row) => [row.inviteeId, row.inviterId]));
      pendingInvitations = rows.flatMap((row) => {
        const invitee = summaries.get(row.inviteeId);
        const inviter = summaries.get(row.inviterId);
        return invitee && inviter ? [{ id: row.id, invitee, inviter, createdAt: row.createdAt.toISOString() }] : [];
      });
    }

    return {
      ...dto,
      // Non-members of open groups see the member list but not the chat.
      members: myRole || group.joinPolicy === 'open' ? members : [],
      pendingInvitations,
      hasPendingInvitation: Boolean(pendingForMe),
    };
  }

  async update(userId: string, groupId: string, input: z.output<typeof updateGroupSchema>): Promise<GroupDetailDto> {
    await this.runWithEffects(async (tx, effects) => {
      await this.requireGroup(groupId, tx, true);
      const role = await this.memberRole(groupId, userId, tx);
      if (role !== 'owner' && role !== 'moderator') throw Errors.forbidden('Only the owner or moderators can edit this group.');
      if (input.joinPolicy !== undefined && role !== 'owner') {
        throw Errors.forbidden('Only the owner can change who may join this group.');
      }
      await tx
        .update(groups)
        .set({ ...input, updatedAt: new Date() })
        .where(eq(groups.id, groupId));
      const memberIds = await this.memberIds(groupId, tx);
      effects.add(() => this.notifyChanged(memberIds, groupId));
    });
    return this.get(userId, groupId);
  }

  /** Deleting a group removes its members, invitations and messages permanently. */
  async delete(userId: string, groupId: string): Promise<void> {
    await this.runWithEffects(async (tx, effects) => {
      const group = await this.requireGroup(groupId, tx, true);
      const role = await this.memberRole(groupId, userId, tx);
      if (role !== 'owner') throw Errors.forbidden('Only the owner can delete this group.');
      const memberIds = await this.memberIds(groupId, tx);
      await tx.delete(conversations).where(eq(conversations.id, group.conversationId));
      await this.ctx.audit.record({ actorId: userId, action: 'group.deleted', targetType: 'group', targetId: groupId }, tx);
      effects.add(() => this.notifyChanged(memberIds, groupId, true));
    });
  }

  /** Joins an open group directly, or accepts a pending invitation for invite-only groups. */
  async join(userId: string, groupId: string): Promise<GroupDetailDto> {
    const [pending] = await this.ctx.db
      .select({ id: invitations.id })
      .from(invitations)
      .where(
        and(
          eq(invitations.groupId, groupId),
          eq(invitations.inviteeId, userId),
          eq(invitations.status, 'pending'),
          gt(invitations.expiresAt, new Date()),
        ),
      )
      .limit(1);
    if (pending) {
      await this.ctx.invitations.accept(userId, pending.id);
      return this.get(userId, groupId);
    }
    await this.runWithEffects(async (tx, effects) => {
      const group = await this.requireGroup(groupId, tx, true);
      if (group.joinPolicy !== 'open') throw Errors.notFound('The group');
      await this.addMember(tx, groupId, userId, effects);
    });
    return this.get(userId, groupId);
  }

  /** Adds a member inside an existing transaction (used by joins and accepted invitations). */
  async addMember(tx: DbExecutor, groupId: string, userId: string, effects: Effects): Promise<void> {
    if (await this.memberRole(groupId, userId, tx)) {
      throw Errors.conflict('You are already a member of this group.', 'ALREADY_MEMBER');
    }
    const ownerId = (await tx
      .select({ userId: groupMembers.userId })
      .from(groupMembers)
      .where(and(eq(groupMembers.groupId, groupId), eq(groupMembers.role, 'owner'))))[0]?.userId;
    if (ownerId && (await this.ctx.relations.isBlockedEitherWay(ownerId, userId, tx))) {
      throw new AppError(403, 'BLOCKED', 'You cannot join this group.');
    }
    const [count] = await tx
      .select({ count: sql<number>`count(*)::int` })
      .from(groupMembers)
      .where(eq(groupMembers.groupId, groupId));
    if ((count?.count ?? 0) >= MAX_GROUP_MEMBERS) {
      throw new AppError(409, 'CAPACITY_REACHED', `This group has reached the maximum of ${MAX_GROUP_MEMBERS} members.`);
    }
    await tx.insert(groupMembers).values({ groupId, userId, role: 'member' });
    await tx.update(groups).set({ updatedAt: new Date() }).where(eq(groups.id, groupId));
    const memberIds = await this.memberIds(groupId, tx);
    effects.add(() => this.notifyChanged(memberIds, groupId));
  }

  /**
   * Leaving: members and moderators leave immediately. The owner must transfer ownership first,
   * unless they are the last member, in which case the group is deleted.
   */
  async leave(userId: string, groupId: string): Promise<{ deleted: boolean }> {
    return this.runWithEffects(async (tx, effects) => {
      await this.requireGroup(groupId, tx, true);
      const role = await this.memberRole(groupId, userId, tx);
      if (!role) throw new AppError(404, 'NOT_MEMBER', 'You are not a member of this group.');
      const memberIds = await this.memberIds(groupId, tx);
      if (role === 'owner') {
        if (memberIds.length > 1) {
          throw Errors.conflict('Transfer ownership to another member before leaving, or delete the group.');
        }
        await this.deleteGroupInTx(tx, groupId, memberIds, effects);
        return { deleted: true };
      }
      await tx.delete(groupMembers).where(and(eq(groupMembers.groupId, groupId), eq(groupMembers.userId, userId)));
      effects.add(() => {
        this.notifyChanged([userId], groupId, true);
        this.notifyChanged(
          memberIds.filter((id) => id !== userId),
          groupId,
        );
      });
      return { deleted: false };
    });
  }

  private async deleteGroupInTx(tx: DbExecutor, groupId: string, memberIds: string[], effects: Effects): Promise<void> {
    const group = await this.requireGroup(groupId, tx);
    await tx.delete(conversations).where(eq(conversations.id, group.conversationId));
    effects.add(() => this.notifyChanged(memberIds, groupId, true));
  }

  async removeMember(actorId: string, groupId: string, targetId: string): Promise<void> {
    if (actorId === targetId) throw Errors.badRequest('Use "Leave group" to leave a group yourself.');
    await this.runWithEffects(async (tx, effects) => {
      const group = await this.requireGroup(groupId, tx, true);
      const actorRole = await this.memberRole(groupId, actorId, tx);
      const targetRole = await this.memberRole(groupId, targetId, tx);
      if (!actorRole) throw Errors.notFound('The group');
      if (!targetRole) throw new AppError(404, 'NOT_MEMBER', 'That user is not a member of this group.');
      const allowed = actorRole === 'owner' || (actorRole === 'moderator' && targetRole === 'member');
      if (!allowed) throw Errors.forbidden('You do not have permission to remove this member.');
      await tx.delete(groupMembers).where(and(eq(groupMembers.groupId, groupId), eq(groupMembers.userId, targetId)));
      const actor = await this.ctx.users.getSummary(actorId, tx);
      await this.ctx.notifications.create(
        targetId,
        { type: 'group.removed', actor, groupId, groupName: group.name },
        tx,
        effects,
      );
      const memberIds = await this.memberIds(groupId, tx);
      effects.add(() => {
        this.notifyChanged([targetId], groupId, true);
        this.notifyChanged(memberIds, groupId);
      });
    });
  }

  async setRole(actorId: string, groupId: string, targetId: string, role: 'moderator' | 'member'): Promise<void> {
    await this.runWithEffects(async (tx, effects) => {
      const group = await this.requireGroup(groupId, tx, true);
      if ((await this.memberRole(groupId, actorId, tx)) !== 'owner') {
        throw Errors.forbidden('Only the owner can change member roles.');
      }
      const targetRole = await this.memberRole(groupId, targetId, tx);
      if (!targetRole) throw new AppError(404, 'NOT_MEMBER', 'That user is not a member of this group.');
      if (targetRole === 'owner') throw Errors.badRequest('Use ownership transfer to change the owner.');
      await tx
        .update(groupMembers)
        .set({ role })
        .where(and(eq(groupMembers.groupId, groupId), eq(groupMembers.userId, targetId)));
      const actor = await this.ctx.users.getSummary(actorId, tx);
      await this.ctx.notifications.create(
        targetId,
        { type: 'group.role_changed', actor, groupId, groupName: group.name, role },
        tx,
        effects,
      );
      const memberIds = await this.memberIds(groupId, tx);
      effects.add(() => this.notifyChanged(memberIds, groupId));
    });
  }

  /** The previous owner becomes a moderator. */
  async transferOwnership(actorId: string, groupId: string, newOwnerId: string): Promise<void> {
    await this.runWithEffects(async (tx, effects) => {
      await this.requireGroup(groupId, tx, true);
      if ((await this.memberRole(groupId, actorId, tx)) !== 'owner') {
        throw Errors.forbidden('Only the owner can transfer ownership.');
      }
      if (actorId === newOwnerId) throw Errors.badRequest('You already own this group.');
      if (!(await this.memberRole(groupId, newOwnerId, tx))) {
        throw new AppError(404, 'NOT_MEMBER', 'The new owner must be a member of this group.');
      }
      await this.transferInTx(tx, groupId, actorId, newOwnerId);
      await this.ctx.audit.record(
        { actorId, action: 'group.ownership_transferred', targetType: 'group', targetId: groupId, metadata: { newOwnerId } },
        tx,
      );
      const memberIds = await this.memberIds(groupId, tx);
      effects.add(() => this.notifyChanged(memberIds, groupId));
    });
  }

  private async transferInTx(tx: DbExecutor, groupId: string, fromId: string, toId: string): Promise<void> {
    // Demote first so the single-owner unique index is never violated.
    await tx
      .update(groupMembers)
      .set({ role: 'moderator' })
      .where(and(eq(groupMembers.groupId, groupId), eq(groupMembers.userId, fromId)));
    await tx
      .update(groupMembers)
      .set({ role: 'owner' })
      .where(and(eq(groupMembers.groupId, groupId), eq(groupMembers.userId, toId)));
  }

  /**
   * Removes a user from all groups during account deletion. Owned groups pass to the longest
   * standing moderator (or member); groups without other members are deleted.
   */
  async removeUserEverywhere(tx: DbExecutor, userId: string, effects: Effects): Promise<void> {
    const memberships = await tx
      .select({ groupId: groupMembers.groupId, role: groupMembers.role })
      .from(groupMembers)
      .where(eq(groupMembers.userId, userId));
    for (const membership of memberships) {
      await this.requireGroup(membership.groupId, tx, true);
      const memberIds = await this.memberIds(membership.groupId, tx);
      if (membership.role === 'owner') {
        const [successor] = await tx
          .select({ userId: groupMembers.userId })
          .from(groupMembers)
          .where(and(eq(groupMembers.groupId, membership.groupId), sql`${groupMembers.userId} <> ${userId}`))
          .orderBy(sql`CASE ${groupMembers.role} WHEN 'moderator' THEN 0 ELSE 1 END`, asc(groupMembers.joinedAt))
          .limit(1);
        if (!successor) {
          await this.deleteGroupInTx(tx, membership.groupId, memberIds, effects);
          continue;
        }
        await this.transferInTx(tx, membership.groupId, userId, successor.userId);
      }
      await tx
        .delete(groupMembers)
        .where(and(eq(groupMembers.groupId, membership.groupId), eq(groupMembers.userId, userId)));
      effects.add(() =>
        this.notifyChanged(
          memberIds.filter((id) => id !== userId),
          membership.groupId,
        ),
      );
    }
  }

  notifyChanged(userIds: string[], groupId: string, removed = false): void {
    this.ctx.hub.sendToUsers(userIds, { type: 'group.changed', data: { groupId, removed } });
  }
}

