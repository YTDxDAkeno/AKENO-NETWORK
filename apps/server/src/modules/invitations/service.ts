import { and, desc, eq, gt, lt, or } from 'drizzle-orm';
import {
  INVITATION_LIFETIME_HOURS,
  type AcceptInvitationResponse,
  type InvitationDto,
  type InvitationKind,
} from '@akeno/shared';
import type { AppContext } from '../../app/context.js';
import type { DbExecutor } from '../../database/client.js';
import { gameSessions, groups, invitations, parties } from '../../database/schema/index.js';
import { isUniqueViolation } from '../../utils/db-errors.js';
import { Effects } from '../../utils/effects.js';
import { AppError, Errors } from '../../utils/errors.js';
import { hoursFromNow } from '../../utils/time.js';

type InvitationRow = typeof invitations.$inferSelect;

function targetIdOf(row: InvitationRow): string {
  return (row.groupId ?? row.partyId ?? row.gameSessionId)!;
}

export class InvitationsService {
  constructor(private readonly ctx: AppContext) {}

  private async runWithEffects<T>(work: (tx: DbExecutor, effects: Effects) => Promise<T>): Promise<T> {
    const effects = new Effects();
    const result = await this.ctx.db.transaction((tx) => work(tx, effects));
    await effects.run((error) => this.ctx.log.error({ err: error }, 'Invitation side effect failed'));
    return result;
  }

  private async targetName(kind: InvitationKind, targetId: string, executor: DbExecutor): Promise<string | null> {
    if (kind === 'group') {
      const [row] = await executor.select({ name: groups.name }).from(groups).where(eq(groups.id, targetId));
      return row?.name ?? null;
    }
    if (kind === 'party') {
      const [row] = await executor.select({ name: parties.name }).from(parties).where(eq(parties.id, targetId));
      return row?.name ?? null;
    }
    const [row] = await executor.select({ name: gameSessions.gameName }).from(gameSessions).where(eq(gameSessions.id, targetId));
    return row?.name ?? null;
  }

  private async toDtos(rows: InvitationRow[]): Promise<InvitationDto[]> {
    const summaries = await this.ctx.users.getSummaries(rows.flatMap((row) => [row.inviterId, row.inviteeId]));
    const result: InvitationDto[] = [];
    for (const row of rows) {
      const inviter = summaries.get(row.inviterId);
      const invitee = summaries.get(row.inviteeId);
      const targetId = targetIdOf(row);
      const name = await this.targetName(row.kind, targetId, this.ctx.db);
      if (!inviter || !invitee || name === null) continue;
      result.push({
        id: row.id,
        kind: row.kind,
        inviter,
        invitee,
        target: { id: targetId, name },
        createdAt: row.createdAt.toISOString(),
        expiresAt: row.expiresAt.toISOString(),
      });
    }
    return result;
  }

  /** Validates permissions for the specific invitation kind and creates the invitation. */
  async create(kind: InvitationKind, targetId: string, inviterId: string, inviteeId: string): Promise<InvitationDto> {
    if (inviterId === inviteeId) throw Errors.badRequest('You cannot invite yourself.');
    const row = await this.runWithEffects(async (tx, effects) => {
      await this.ctx.users.requireActiveUser(inviteeId, tx);
      if (await this.ctx.relations.isBlockedEitherWay(inviterId, inviteeId, tx)) {
        throw new AppError(403, 'BLOCKED', 'You cannot invite this user.');
      }
      await this.assertCanInvite(tx, kind, targetId, inviterId, inviteeId);

      let inserted: InvitationRow | undefined;
      try {
        [inserted] = await tx
          .insert(invitations)
          .values({
            kind,
            groupId: kind === 'group' ? targetId : null,
            partyId: kind === 'party' ? targetId : null,
            gameSessionId: kind === 'game_session' ? targetId : null,
            inviterId,
            inviteeId,
            expiresAt: hoursFromNow(INVITATION_LIFETIME_HOURS[kind]),
          })
          .returning();
      } catch (error) {
        if (isUniqueViolation(error)) {
          throw Errors.conflict('This user already has a pending invitation.', 'REQUEST_PENDING');
        }
        throw error;
      }
      if (!inserted) throw new Error('Invitation insert returned no row');
      const actor = await this.ctx.users.getSummary(inviterId, tx);
      const targetName = (await this.targetName(kind, targetId, tx)) ?? '';
      await this.ctx.notifications.create(
        inviteeId,
        { type: 'invitation.received', actor, invitationId: inserted.id, kind, targetId, targetName },
        tx,
        effects,
      );
      effects.add(() => this.ctx.hub.sendToUser(inviteeId, { type: 'invitations.changed', data: {} }));
      return inserted;
    });
    const [dto] = await this.toDtos([row]);
    if (!dto) throw Errors.notFound('The invitation');
    return dto;
  }

  private async assertCanInvite(
    tx: DbExecutor,
    kind: InvitationKind,
    targetId: string,
    inviterId: string,
    inviteeId: string,
  ): Promise<void> {
    if (kind === 'group') {
      const role = await this.ctx.groups.memberRole(targetId, inviterId, tx);
      if (!role) throw Errors.notFound('The group');
      if (role === 'member') throw Errors.forbidden('Only the owner or moderators can invite people to this group.');
      if (await this.ctx.groups.memberRole(targetId, inviteeId, tx)) {
        throw Errors.conflict('This user is already a member of the group.', 'ALREADY_MEMBER');
      }
      return;
    }

    // Parties and game sessions: participants may invite their friends.
    if (!(await this.ctx.relations.areFriends(inviterId, inviteeId, tx))) {
      throw Errors.forbidden('You can only invite friends.');
    }
    if (kind === 'party') {
      const members = await this.ctx.parties.memberIds(targetId, tx);
      if (!members.includes(inviterId)) throw Errors.notFound('The party');
      if (members.includes(inviteeId)) throw Errors.conflict('This user is already in the party.', 'ALREADY_MEMBER');
      const [party] = await tx.select().from(parties).where(eq(parties.id, targetId));
      if (party && party.memberCount >= party.maxMembers) throw new AppError(409, 'CAPACITY_REACHED', 'This party is full.');
      return;
    }

    const [session] = await tx.select().from(gameSessions).where(eq(gameSessions.id, targetId));
    if (!session) throw Errors.notFound('The game session');
    const members = await this.ctx.gameSessions.memberIds(targetId, tx);
    if (!members.includes(inviterId)) throw Errors.notFound('The game session');
    if (session.visibility === 'private' && session.hostId !== inviterId) {
      throw Errors.forbidden('Only the host can invite people to a private session.');
    }
    if (session.status === 'finished') throw new AppError(409, 'SESSION_CLOSED', 'This session has already finished.');
    if (members.includes(inviteeId)) throw Errors.conflict('This user is already in the session.', 'ALREADY_MEMBER');
    if (session.participantCount >= session.maxParticipants) {
      throw new AppError(409, 'CAPACITY_REACHED', 'This session is full.');
    }
  }

  async listIncoming(userId: string): Promise<InvitationDto[]> {
    const rows = await this.ctx.db
      .select()
      .from(invitations)
      .where(and(eq(invitations.inviteeId, userId), eq(invitations.status, 'pending'), gt(invitations.expiresAt, new Date())))
      .orderBy(desc(invitations.createdAt))
      .limit(100);
    return this.toDtos(rows);
  }

  async accept(userId: string, invitationId: string): Promise<AcceptInvitationResponse> {
    return this.runWithEffects(async (tx, effects) => {
      const invitation = await this.lockPendingForInvitee(tx, invitationId, userId);
      if (await this.ctx.relations.isBlockedEitherWay(userId, invitation.inviterId, tx)) {
        throw new AppError(403, 'BLOCKED', 'You cannot accept this invitation.');
      }
      const targetId = targetIdOf(invitation);
      if (invitation.kind === 'group') {
        await this.ctx.groups.addMember(tx, targetId, userId, effects);
      } else if (invitation.kind === 'party') {
        await this.ctx.parties.joinInTx(tx, targetId, userId, effects);
      } else {
        await this.ctx.gameSessions.joinInTx(tx, targetId, userId, effects);
      }
      await tx
        .update(invitations)
        .set({ status: 'accepted', respondedAt: new Date() })
        .where(eq(invitations.id, invitationId));
      effects.add(() => this.ctx.hub.sendToUsers([userId, invitation.inviterId], { type: 'invitations.changed', data: {} }));
      return { kind: invitation.kind, targetId };
    });
  }

  async decline(userId: string, invitationId: string): Promise<void> {
    await this.runWithEffects(async (tx, effects) => {
      const invitation = await this.lockPendingForInvitee(tx, invitationId, userId);
      await tx
        .update(invitations)
        .set({ status: 'declined', respondedAt: new Date() })
        .where(eq(invitations.id, invitationId));
      effects.add(() => this.ctx.hub.sendToUsers([userId, invitation.inviterId], { type: 'invitations.changed', data: {} }));
    });
  }

  /** The inviter (or, for groups, an owner/moderator) withdraws a pending invitation. */
  async cancel(userId: string, invitationId: string): Promise<void> {
    await this.runWithEffects(async (tx, effects) => {
      const [invitation] = await tx.select().from(invitations).where(eq(invitations.id, invitationId)).for('update').limit(1);
      if (!invitation || invitation.status !== 'pending') throw Errors.notFound('The pending invitation');
      let allowed = invitation.inviterId === userId;
      if (!allowed && invitation.kind === 'group' && invitation.groupId) {
        const role = await this.ctx.groups.memberRole(invitation.groupId, userId, tx);
        allowed = role === 'owner' || role === 'moderator';
      }
      if (!allowed) throw Errors.notFound('The pending invitation');
      await tx
        .update(invitations)
        .set({ status: 'cancelled', respondedAt: new Date() })
        .where(eq(invitations.id, invitationId));
      effects.add(() =>
        this.ctx.hub.sendToUsers([invitation.inviteeId, userId], { type: 'invitations.changed', data: {} }),
      );
    });
  }

  private async lockPendingForInvitee(tx: DbExecutor, invitationId: string, userId: string): Promise<InvitationRow> {
    const [invitation] = await tx.select().from(invitations).where(eq(invitations.id, invitationId)).for('update').limit(1);
    if (!invitation || invitation.inviteeId !== userId) throw Errors.notFound('The invitation');
    if (invitation.status !== 'pending') throw Errors.conflict('This invitation is no longer pending.');
    if (invitation.expiresAt <= new Date()) {
      await tx.update(invitations).set({ status: 'expired' }).where(eq(invitations.id, invitationId));
      throw Errors.conflict('This invitation has expired.');
    }
    return invitation;
  }

  /** Marks pending invitations past their expiry as expired. */
  async expireStale(): Promise<void> {
    await this.ctx.db
      .update(invitations)
      .set({ status: 'expired' })
      .where(and(eq(invitations.status, 'pending'), lt(invitations.expiresAt, new Date())));
  }

  /** Cancels pending invitations sent to or by a user (account deletion). */
  async cancelAllForUser(tx: DbExecutor, userId: string): Promise<void> {
    await tx
      .update(invitations)
      .set({ status: 'cancelled', respondedAt: new Date() })
      .where(and(eq(invitations.status, 'pending'), or(eq(invitations.inviterId, userId), eq(invitations.inviteeId, userId))));
  }
}
