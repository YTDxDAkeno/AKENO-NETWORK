import { and, asc, desc, eq, gt, ilike, inArray, ne, notInArray, or, sql, type SQL } from 'drizzle-orm';
import type {
  GamePlatform,
  GameSessionDetailDto,
  GameSessionDto,
  GameSessionViewerState,
  JoinGameSessionResponse,
  createGameSessionSchema,
  gameSessionListQuerySchema,
  updateGameSessionSchema,
} from '@akeno/shared';
import type { z } from 'zod';
import type { AppContext } from '../../app/context.js';
import type { DbExecutor } from '../../database/client.js';
import {
  gameSessionJoinRequests,
  gameSessionMembers,
  gameSessions,
  invitations,
  profiles,
  users,
} from '../../database/schema/index.js';
import { Effects } from '../../utils/effects.js';
import { AppError, Errors } from '../../utils/errors.js';
import { placeholderSummary, userCardColumns } from '../users/service.js';

type SessionRow = typeof gameSessions.$inferSelect;

const MAX_OPEN_HOSTED_SESSIONS = 5;

function escapeLike(value: string): string {
  return value.replace(/[\\%_]/g, (match) => `\\${match}`);
}

/**
 * Application-level game session directory. Joining a session here records intent and
 * participation inside AKENO NETWORK only; it never connects to or controls any game.
 */
export class GameSessionsService {
  constructor(private readonly ctx: AppContext) {}

  private async runWithEffects<T>(work: (tx: DbExecutor, effects: Effects) => Promise<T>): Promise<T> {
    const effects = new Effects();
    const result = await this.ctx.db.transaction((tx) => work(tx, effects));
    await effects.run((error) => this.ctx.log.error({ err: error }, 'Game session side effect failed'));
    return result;
  }

  private async lockSession(sessionId: string, executor: DbExecutor): Promise<SessionRow> {
    const [row] = await executor.select().from(gameSessions).where(eq(gameSessions.id, sessionId)).for('update').limit(1);
    if (!row) throw Errors.notFound('The game session');
    return row;
  }

  async memberIds(sessionId: string, executor: DbExecutor = this.ctx.db): Promise<string[]> {
    const rows = await executor
      .select({ userId: gameSessionMembers.userId })
      .from(gameSessionMembers)
      .where(eq(gameSessionMembers.sessionId, sessionId))
      .orderBy(asc(gameSessionMembers.joinedAt));
    return rows.map((row) => row.userId);
  }

  private async viewerStates(userId: string, sessionIds: string[]): Promise<{
    states: Map<string, GameSessionViewerState>;
    invitationIds: Map<string, string>;
  }> {
    const states = new Map<string, GameSessionViewerState>();
    const invitationIds = new Map<string, string>();
    if (sessionIds.length === 0) return { states, invitationIds };
    const [memberships, requests, invites] = await Promise.all([
      this.ctx.db
        .select({ sessionId: gameSessionMembers.sessionId, role: gameSessionMembers.role })
        .from(gameSessionMembers)
        .where(and(eq(gameSessionMembers.userId, userId), inArray(gameSessionMembers.sessionId, sessionIds))),
      this.ctx.db
        .select({ sessionId: gameSessionJoinRequests.sessionId })
        .from(gameSessionJoinRequests)
        .where(
          and(
            eq(gameSessionJoinRequests.userId, userId),
            eq(gameSessionJoinRequests.status, 'pending'),
            inArray(gameSessionJoinRequests.sessionId, sessionIds),
          ),
        ),
      this.ctx.db
        .select({ sessionId: invitations.gameSessionId, id: invitations.id })
        .from(invitations)
        .where(
          and(
            eq(invitations.inviteeId, userId),
            eq(invitations.status, 'pending'),
            gt(invitations.expiresAt, new Date()),
            inArray(invitations.gameSessionId, sessionIds),
          ),
        ),
    ]);
    for (const row of invites) {
      if (!row.sessionId) continue;
      states.set(row.sessionId, 'invited');
      invitationIds.set(row.sessionId, row.id);
    }
    for (const row of requests) states.set(row.sessionId, 'requested');
    for (const row of memberships) states.set(row.sessionId, row.role === 'host' ? 'host' : 'member');
    return { states, invitationIds };
  }

  private async toDtos(userId: string, rows: SessionRow[]): Promise<GameSessionDto[]> {
    if (rows.length === 0) return [];
    const [hosts, { states }] = await Promise.all([
      this.ctx.users.getSummaries(rows.map((row) => row.hostId)),
      this.viewerStates(
        userId,
        rows.map((row) => row.id),
      ),
    ]);
    return rows.map((row) => {
      const viewerState = states.get(row.id) ?? 'none';
      const isParticipant = viewerState === 'host' || viewerState === 'member';
      return {
        id: row.id,
        gameName: row.gameName,
        gameIdentifier: row.gameIdentifier,
        art: this.ctx.games.artForSession(row.gameIdentifier, row.gameName),
        description: row.description,
        host: hosts.get(row.hostId) ?? placeholderSummary(row.hostId),
        visibility: row.visibility,
        joinMode: row.joinMode,
        maxParticipants: row.maxParticipants,
        participantCount: row.participantCount,
        status: row.status,
        region: row.region,
        platform: (row.platform as GamePlatform | null) ?? null,
        createdAt: row.createdAt.toISOString(),
        updatedAt: row.updatedAt.toISOString(),
        startedAt: row.startedAt?.toISOString() ?? null,
        endedAt: row.endedAt?.toISOString() ?? null,
        viewerState,
        connectionInfo: isParticipant ? row.connectionInfo : null,
      };
    });
  }

  /** Whether the viewer may see a session at all. Non-visible sessions behave as if they do not exist. */
  private async canView(userId: string, session: SessionRow, executor: DbExecutor = this.ctx.db): Promise<boolean> {
    const members = await this.memberIds(session.id, executor);
    if (members.includes(userId)) return true;
    if (await this.ctx.relations.isBlockedEitherWay(userId, session.hostId, executor)) return false;
    const [invited] = await executor
      .select({ id: invitations.id })
      .from(invitations)
      .where(
        and(
          eq(invitations.gameSessionId, session.id),
          eq(invitations.inviteeId, userId),
          eq(invitations.status, 'pending'),
          gt(invitations.expiresAt, new Date()),
        ),
      )
      .limit(1);
    if (invited) return true;
    if (session.visibility === 'public') return true;
    if (session.visibility === 'friends') return this.ctx.relations.areFriends(userId, session.hostId, executor);
    return false;
  }

  async list(userId: string, query: z.output<typeof gameSessionListQuerySchema>): Promise<GameSessionDto[]> {
    const [friendIds, excluded, myRows, invitedRows] = await Promise.all([
      this.ctx.relations.friendIds(userId),
      this.ctx.relations.blockedByOrBlocking(userId),
      this.ctx.db
        .select({ sessionId: gameSessionMembers.sessionId })
        .from(gameSessionMembers)
        .where(eq(gameSessionMembers.userId, userId)),
      this.ctx.db
        .select({ sessionId: invitations.gameSessionId })
        .from(invitations)
        .where(
          and(
            eq(invitations.inviteeId, userId),
            eq(invitations.kind, 'game_session'),
            eq(invitations.status, 'pending'),
            gt(invitations.expiresAt, new Date()),
          ),
        ),
    ]);
    const visibleIds = [
      ...myRows.map((row) => row.sessionId),
      ...invitedRows.flatMap((row) => (row.sessionId ? [row.sessionId] : [])),
    ];
    const visibility: SQL[] = [eq(gameSessions.visibility, 'public')];
    if (friendIds.length > 0) {
      visibility.push(and(eq(gameSessions.visibility, 'friends'), inArray(gameSessions.hostId, friendIds))!);
    }
    if (visibleIds.length > 0) visibility.push(inArray(gameSessions.id, visibleIds));

    const filters: Array<SQL | undefined> = [
      or(...visibility),
      query.status ? eq(gameSessions.status, query.status) : ne(gameSessions.status, 'finished'),
      query.platform ? eq(gameSessions.platform, query.platform) : undefined,
      query.region ? ilike(gameSessions.region, `%${escapeLike(query.region)}%`) : undefined,
      query.q
        ? or(
            ilike(gameSessions.gameName, `%${escapeLike(query.q)}%`),
            ilike(gameSessions.description, `%${escapeLike(query.q)}%`),
          )
        : undefined,
      query.joinable ? sql`${gameSessions.participantCount} < ${gameSessions.maxParticipants}` : undefined,
      excluded.size > 0 ? notInArray(gameSessions.hostId, [...excluded]) : undefined,
    ];
    const rows = await this.ctx.db
      .select()
      .from(gameSessions)
      .where(and(...filters))
      .orderBy(
        sql`CASE ${gameSessions.status} WHEN 'waiting' THEN 0 WHEN 'active' THEN 1 ELSE 2 END`,
        desc(gameSessions.createdAt),
      )
      .limit(query.limit);
    return this.toDtos(userId, rows);
  }

  async listMine(userId: string): Promise<GameSessionDto[]> {
    const rows = await this.ctx.db
      .select({ session: gameSessions })
      .from(gameSessionMembers)
      .innerJoin(gameSessions, eq(gameSessions.id, gameSessionMembers.sessionId))
      .where(eq(gameSessionMembers.userId, userId))
      .orderBy(
        sql`CASE ${gameSessions.status} WHEN 'waiting' THEN 0 WHEN 'active' THEN 1 ELSE 2 END`,
        desc(gameSessions.createdAt),
      )
      .limit(50);
    return this.toDtos(
      userId,
      rows.map((row) => row.session),
    );
  }

  async get(userId: string, sessionId: string): Promise<GameSessionDetailDto> {
    const [session] = await this.ctx.db.select().from(gameSessions).where(eq(gameSessions.id, sessionId));
    if (!session || !(await this.canView(userId, session))) throw Errors.notFound('The game session');
    const [dto] = await this.toDtos(userId, [session]);
    if (!dto) throw Errors.notFound('The game session');

    const memberRows = await this.ctx.db
      .select({ card: userCardColumns, role: gameSessionMembers.role, joinedAt: gameSessionMembers.joinedAt })
      .from(gameSessionMembers)
      .innerJoin(users, eq(users.id, gameSessionMembers.userId))
      .innerJoin(profiles, eq(profiles.userId, users.id))
      .where(eq(gameSessionMembers.sessionId, sessionId))
      .orderBy(sql`CASE ${gameSessionMembers.role} WHEN 'host' THEN 0 ELSE 1 END`, asc(gameSessionMembers.joinedAt));
    const ids = memberRows.map((row) => row.card.id);
    const [friends, blocked] = await Promise.all([
      this.ctx.relations.friendSet(userId, ids),
      this.ctx.relations.blockedSet(userId, ids),
    ]);

    let joinRequests: GameSessionDetailDto['joinRequests'] = [];
    if (dto.viewerState === 'host') {
      const requests = await this.ctx.db
        .select()
        .from(gameSessionJoinRequests)
        .where(and(eq(gameSessionJoinRequests.sessionId, sessionId), eq(gameSessionJoinRequests.status, 'pending')))
        .orderBy(asc(gameSessionJoinRequests.createdAt));
      const summaries = await this.ctx.users.getSummaries(requests.map((request) => request.userId));
      joinRequests = requests.flatMap((request) => {
        const user = summaries.get(request.userId);
        return user ? [{ id: request.id, user, message: request.message, createdAt: request.createdAt.toISOString() }] : [];
      });
    }
    const { invitationIds } = await this.viewerStates(userId, [sessionId]);

    return {
      ...dto,
      members: memberRows.map((row) => ({
        user: this.ctx.users.withPresence(row.card, userId, friends.has(row.card.id), blocked.has(row.card.id)),
        role: row.role,
        joinedAt: row.joinedAt.toISOString(),
      })),
      joinRequests,
      pendingInvitationId: invitationIds.get(sessionId) ?? null,
    };
  }

  async create(userId: string, input: z.output<typeof createGameSessionSchema>): Promise<GameSessionDetailDto> {
    const sessionId = await this.runWithEffects(async (tx, effects) => {
      const [hosted] = await tx
        .select({ count: sql<number>`count(*)::int` })
        .from(gameSessions)
        .where(and(eq(gameSessions.hostId, userId), ne(gameSessions.status, 'finished')));
      if ((hosted?.count ?? 0) >= MAX_OPEN_HOSTED_SESSIONS) {
        throw Errors.conflict(`You can host at most ${MAX_OPEN_HOSTED_SESSIONS} open sessions. Close one first.`);
      }
      const [session] = await tx
        .insert(gameSessions)
        .values({
          gameName: input.gameName,
          gameIdentifier: input.gameIdentifier ?? null,
          description: input.description,
          hostId: userId,
          visibility: input.visibility,
          joinMode: input.joinMode,
          maxParticipants: input.maxParticipants,
          participantCount: 1,
          region: input.region ?? null,
          platform: input.platform ?? null,
          connectionInfo: input.connectionInfo ?? null,
        })
        .returning();
      if (!session) throw new Error('Game session insert returned no row');
      await tx.insert(gameSessionMembers).values({ sessionId: session.id, userId, role: 'host' });
      effects.add(() => this.notifyChanged(session, [userId]));
      return session.id;
    });
    return this.get(userId, sessionId);
  }

  async update(userId: string, sessionId: string, input: z.output<typeof updateGameSessionSchema>): Promise<GameSessionDetailDto> {
    await this.runWithEffects(async (tx, effects) => {
      const session = await this.lockSession(sessionId, tx);
      if (session.hostId !== userId) throw Errors.forbidden('Only the host can change this session.');
      if (session.status === 'finished') throw new AppError(409, 'SESSION_CLOSED', 'This session has already finished.');
      if (input.maxParticipants !== undefined && input.maxParticipants < session.participantCount) {
        throw Errors.conflict('The maximum cannot be lower than the current number of participants.');
      }
      const patch: Partial<typeof gameSessions.$inferInsert> = { updatedAt: new Date() };
      if (input.description !== undefined) patch.description = input.description;
      if (input.visibility !== undefined) patch.visibility = input.visibility;
      if (input.joinMode !== undefined) patch.joinMode = input.joinMode;
      if (input.maxParticipants !== undefined) patch.maxParticipants = input.maxParticipants;
      if (input.region !== undefined) patch.region = input.region;
      if (input.platform !== undefined) patch.platform = input.platform;
      if (input.connectionInfo !== undefined) patch.connectionInfo = input.connectionInfo;
      if (input.status !== undefined) {
        patch.status = input.status;
        if (input.status === 'active' && !session.startedAt) patch.startedAt = new Date();
      }
      const [updated] = await tx.update(gameSessions).set(patch).where(eq(gameSessions.id, sessionId)).returning();
      const members = await this.memberIds(sessionId, tx);
      if (updated) effects.add(() => this.notifyChanged(updated, members));
    });
    return this.get(userId, sessionId);
  }

  /**
   * Join flow:
   * - A pending invitation always allows joining (accepting the invitation).
   * - "open" sessions add eligible users immediately, subject to capacity.
   * - "approval" sessions create a join request that the host accepts or rejects.
   */
  async join(userId: string, sessionId: string, message: string): Promise<JoinGameSessionResponse> {
    const [invitation] = await this.ctx.db
      .select({ id: invitations.id })
      .from(invitations)
      .where(
        and(
          eq(invitations.gameSessionId, sessionId),
          eq(invitations.inviteeId, userId),
          eq(invitations.status, 'pending'),
          gt(invitations.expiresAt, new Date()),
        ),
      )
      .limit(1);
    if (invitation) {
      await this.ctx.invitations.accept(userId, invitation.id);
      return { outcome: 'joined', session: await this.get(userId, sessionId) };
    }

    const outcome = await this.runWithEffects(async (tx, effects): Promise<JoinGameSessionResponse['outcome']> => {
      const session = await this.lockSession(sessionId, tx);
      if (!(await this.canView(userId, session, tx))) throw Errors.notFound('The game session');
      if ((await this.memberIds(sessionId, tx)).includes(userId)) return 'already_member';
      if (session.status === 'finished') throw new AppError(409, 'SESSION_CLOSED', 'This session has already finished.');
      if (session.visibility === 'private') throw Errors.notFound('The game session');

      if (session.joinMode === 'approval') {
        if (session.participantCount >= session.maxParticipants) {
          throw new AppError(409, 'CAPACITY_REACHED', 'This session is full.');
        }
        const [request] = await tx
          .insert(gameSessionJoinRequests)
          .values({ sessionId, userId, message })
          .onConflictDoNothing()
          .returning();
        if (request) {
          const actor = await this.ctx.users.getSummary(userId, tx);
          await this.ctx.notifications.create(
            session.hostId,
            { type: 'game_session.join_requested', actor, sessionId, gameName: session.gameName },
            tx,
            effects,
          );
          effects.add(() => this.ctx.hub.sendToUsers([session.hostId, userId], { type: 'game-session.changed', data: { sessionId } }));
        }
        return 'requested';
      }

      await this.joinInTx(tx, sessionId, userId, effects);
      return 'joined';
    });
    return { outcome, session: await this.get(userId, sessionId) };
  }

  /** Adds a participant; capacity is enforced by a locked conditional UPDATE and a CHECK constraint. */
  async joinInTx(tx: DbExecutor, sessionId: string, userId: string, effects: Effects): Promise<void> {
    const session = await this.lockSession(sessionId, tx);
    if (session.status === 'finished') throw new AppError(409, 'SESSION_CLOSED', 'This session has already finished.');
    if (await this.ctx.relations.isBlockedEitherWay(userId, session.hostId, tx)) {
      throw Errors.notFound('The game session');
    }
    const [inserted] = await tx
      .insert(gameSessionMembers)
      .values({ sessionId, userId, role: 'player' })
      .onConflictDoNothing()
      .returning({ userId: gameSessionMembers.userId });
    if (!inserted) return;
    const [updated] = await tx
      .update(gameSessions)
      .set({ participantCount: sql`${gameSessions.participantCount} + 1`, updatedAt: new Date() })
      .where(and(eq(gameSessions.id, sessionId), sql`${gameSessions.participantCount} < ${gameSessions.maxParticipants}`))
      .returning();
    if (!updated) throw new AppError(409, 'CAPACITY_REACHED', 'This session is full.');
    await tx
      .update(gameSessionJoinRequests)
      .set({ status: 'accepted', respondedAt: new Date() })
      .where(
        and(
          eq(gameSessionJoinRequests.sessionId, sessionId),
          eq(gameSessionJoinRequests.userId, userId),
          eq(gameSessionJoinRequests.status, 'pending'),
        ),
      );
    await tx
      .update(invitations)
      .set({ status: 'accepted', respondedAt: new Date() })
      .where(and(eq(invitations.gameSessionId, sessionId), eq(invitations.inviteeId, userId), eq(invitations.status, 'pending')));
    const members = await this.memberIds(sessionId, tx);
    effects.add(() => this.notifyChanged(updated, members));
  }

  async respondToRequest(hostId: string, sessionId: string, requestId: string, accept: boolean): Promise<GameSessionDetailDto> {
    await this.runWithEffects(async (tx, effects) => {
      const session = await this.lockSession(sessionId, tx);
      if (session.hostId !== hostId) throw Errors.forbidden('Only the host can respond to join requests.');
      const [request] = await tx
        .select()
        .from(gameSessionJoinRequests)
        .where(and(eq(gameSessionJoinRequests.id, requestId), eq(gameSessionJoinRequests.sessionId, sessionId)))
        .for('update')
        .limit(1);
      if (!request) throw Errors.notFound('The join request');
      if (request.status !== 'pending') throw Errors.conflict('This join request is no longer pending.');
      const host = await this.ctx.users.getSummary(hostId, tx);
      if (accept) {
        await this.joinInTx(tx, sessionId, request.userId, effects);
      }
      await tx
        .update(gameSessionJoinRequests)
        .set({ status: accept ? 'accepted' : 'rejected', respondedAt: new Date(), respondedById: hostId })
        .where(eq(gameSessionJoinRequests.id, requestId));
      await this.ctx.notifications.create(
        request.userId,
        {
          type: accept ? 'game_session.request_accepted' : 'game_session.request_rejected',
          actor: host,
          sessionId,
          gameName: session.gameName,
        },
        tx,
        effects,
      );
      effects.add(() => this.ctx.hub.sendToUsers([hostId, request.userId], { type: 'game-session.changed', data: { sessionId } }));
    });
    return this.get(hostId, sessionId);
  }

  async cancelRequest(userId: string, sessionId: string): Promise<void> {
    const updated = await this.ctx.db
      .update(gameSessionJoinRequests)
      .set({ status: 'cancelled', respondedAt: new Date() })
      .where(
        and(
          eq(gameSessionJoinRequests.sessionId, sessionId),
          eq(gameSessionJoinRequests.userId, userId),
          eq(gameSessionJoinRequests.status, 'pending'),
        ),
      )
      .returning();
    if (updated.length === 0) throw Errors.notFound('The pending join request');
    const [session] = await this.ctx.db.select({ hostId: gameSessions.hostId }).from(gameSessions).where(eq(gameSessions.id, sessionId));
    if (session) this.ctx.hub.sendToUsers([session.hostId, userId], { type: 'game-session.changed', data: { sessionId } });
  }

  async leave(userId: string, sessionId: string): Promise<void> {
    await this.runWithEffects((tx, effects) => this.removeMemberInTx(tx, sessionId, userId, effects));
  }

  async kick(hostId: string, sessionId: string, targetId: string): Promise<void> {
    if (hostId === targetId) throw Errors.badRequest('Use "Leave session" to leave yourself.');
    await this.runWithEffects(async (tx, effects) => {
      const session = await this.lockSession(sessionId, tx);
      if (session.hostId !== hostId) throw Errors.forbidden('Only the host can remove participants.');
      await this.removeMemberInTx(tx, sessionId, targetId, effects);
    });
  }

  /**
   * Removes a participant. If the host leaves, the longest-standing participant becomes host;
   * when nobody remains the session is marked finished.
   */
  async removeMemberInTx(tx: DbExecutor, sessionId: string, userId: string, effects: Effects): Promise<void> {
    const session = await this.lockSession(sessionId, tx);
    const deleted = await tx
      .delete(gameSessionMembers)
      .where(and(eq(gameSessionMembers.sessionId, sessionId), eq(gameSessionMembers.userId, userId)))
      .returning();
    if (deleted.length === 0) throw new AppError(404, 'NOT_MEMBER', 'That user is not part of this session.');
    const remaining = await this.memberIds(sessionId, tx);
    const patch: Partial<typeof gameSessions.$inferInsert> = {
      participantCount: remaining.length,
      updatedAt: new Date(),
    };
    if (remaining.length === 0) {
      patch.status = 'finished';
      patch.endedAt = new Date();
    } else if (session.hostId === userId) {
      patch.hostId = remaining[0]!;
      await tx
        .update(gameSessionMembers)
        .set({ role: 'host' })
        .where(and(eq(gameSessionMembers.sessionId, sessionId), eq(gameSessionMembers.userId, remaining[0]!)));
    }
    const [updated] = await tx.update(gameSessions).set(patch).where(eq(gameSessions.id, sessionId)).returning();
    if (remaining.length === 0) await this.cancelPendingInTx(tx, sessionId);
    if (updated) effects.add(() => this.notifyChanged(updated, [...remaining, userId]));
  }

  async close(hostId: string, sessionId: string): Promise<GameSessionDetailDto> {
    await this.runWithEffects(async (tx, effects) => {
      const session = await this.lockSession(sessionId, tx);
      if (session.hostId !== hostId) throw Errors.forbidden('Only the host can close this session.');
      if (session.status === 'finished') return;
      const [updated] = await tx
        .update(gameSessions)
        .set({ status: 'finished', endedAt: new Date(), updatedAt: new Date() })
        .where(eq(gameSessions.id, sessionId))
        .returning();
      const affected = await this.cancelPendingInTx(tx, sessionId);
      const members = await this.memberIds(sessionId, tx);
      if (updated) effects.add(() => this.notifyChanged(updated, [...members, ...affected]));
    });
    return this.get(hostId, sessionId);
  }

  /** Cancels pending join requests and invitations; returns affected user IDs. */
  private async cancelPendingInTx(tx: DbExecutor, sessionId: string): Promise<string[]> {
    const requests = await tx
      .update(gameSessionJoinRequests)
      .set({ status: 'cancelled', respondedAt: new Date() })
      .where(and(eq(gameSessionJoinRequests.sessionId, sessionId), eq(gameSessionJoinRequests.status, 'pending')))
      .returning({ userId: gameSessionJoinRequests.userId });
    const invites = await tx
      .update(invitations)
      .set({ status: 'cancelled', respondedAt: new Date() })
      .where(and(eq(invitations.gameSessionId, sessionId), eq(invitations.status, 'pending')))
      .returning({ userId: invitations.inviteeId });
    return [...requests.map((row) => row.userId), ...invites.map((row) => row.userId)];
  }

  /** Account deletion: leave every session the user participates in. */
  async removeUserEverywhere(tx: DbExecutor, userId: string, effects: Effects): Promise<void> {
    const rows = await tx
      .select({ sessionId: gameSessionMembers.sessionId })
      .from(gameSessionMembers)
      .where(eq(gameSessionMembers.userId, userId));
    for (const row of rows) await this.removeMemberInTx(tx, row.sessionId, userId, effects);
    await tx.delete(gameSessionJoinRequests).where(eq(gameSessionJoinRequests.userId, userId));
  }

  /** Pushes a change event to participants and, for listed sessions, to the host's online friends. */
  private notifyChanged(session: SessionRow, participantIds: string[]): void {
    const event = { type: 'game-session.changed' as const, data: { sessionId: session.id } };
    this.ctx.hub.sendToUsers(participantIds, event);
    if (session.visibility !== 'private') {
      void this.ctx.relations
        .friendIds(session.hostId)
        .then((friendIds) => this.ctx.hub.sendToUsers(friendIds.filter((id) => this.ctx.hub.isConnected(id)), event))
        .catch((error: unknown) => this.ctx.log.error({ err: error }, 'Failed to notify friends of session change'));
    }
  }
}
