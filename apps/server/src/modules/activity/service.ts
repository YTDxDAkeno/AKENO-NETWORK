import { and, asc, desc, eq, inArray, isNull, lt, sql } from 'drizzle-orm';
import {
  GAME_ACTIVITY,
  type CurrentGameActivity,
  type GameActivityPlatform,
  type GameActivityResponse,
  type GameHeartbeatInput,
  type GameHeartbeatResponse,
  type GameHistoryEntry,
  type GameSessionEndReason,
  type GameSessionEntry,
  type GameStopInput,
  type GameTitleDto,
} from '@akeno/shared';
import type { AppContext } from '../../app/context.js';
import type { DbExecutor } from '../../database/client.js';
import { gameActivitySessions, gameTitles, profiles, users } from '../../database/schema/index.js';
import { audienceAllows, computePresenceView } from '../../realtime/presence-rules.js';
import { isUniqueViolation } from '../../utils/db-errors.js';
import { AppError, Errors } from '../../utils/errors.js';
import type { AuthContextData } from '../auth/service.js';

const STALE_MS = GAME_ACTIVITY.staleAfterSeconds * 1000;

/** The session shown as a user's current game (the most recently started active one). */
interface ActiveGame {
  sessionId: string;
  titleId: string;
  reportedName: string | null;
  platform: GameActivityPlatform;
  startedAt: Date;
  lastHeartbeatAt: Date;
}

type SessionRow = typeof gameActivitySessions.$inferSelect;

/**
 * Automatic game activity: sessions reported by paired devices, current game per user, tracked
 * history and playtime. Only time confirmed by heartbeats is counted; nothing is estimated.
 *
 * The current game of each user is cached in memory (like presence, this backend is a single
 * instance); PostgreSQL stays authoritative and the cache is rebuilt at startup.
 */
export class ActivityService {
  private readonly current = new Map<string, ActiveGame>();
  private readonly titles = new Map<string, string>();

  constructor(private readonly ctx: AppContext) {}

  /** Loads curated names and the active, non-stale sessions after startup. */
  async load(now = new Date()): Promise<void> {
    const titleRows = await this.ctx.db.select({ titleId: gameTitles.titleId, name: gameTitles.name }).from(gameTitles);
    this.titles.clear();
    for (const row of titleRows) this.titles.set(row.titleId, row.name);
    const cutoff = new Date(now.getTime() - STALE_MS);
    const rows = await this.ctx.db
      .select()
      .from(gameActivitySessions)
      .where(isNull(gameActivitySessions.endedAt))
      .orderBy(asc(gameActivitySessions.startedAt));
    this.current.clear();
    for (const row of rows) if (row.lastHeartbeatAt >= cutoff) this.current.set(row.userId, toActive(row));
  }

  // ---- What others see -----------------------------------------------------------------------

  /** The user's current game, or null (also when its heartbeats stopped and it is about to be swept). */
  currentOf(userId: string, now = Date.now()): CurrentGameActivity | null {
    const game = this.current.get(userId);
    if (!game || now - game.lastHeartbeatAt.getTime() > STALE_MS) return null;
    return {
      titleId: game.titleId,
      name: this.nameFor(game.titleId, game.reportedName),
      platform: game.platform,
      startedAt: game.startedAt.toISOString(),
    };
  }

  private nameFor(titleId: string, reported: string | null): string | null {
    return this.titles.get(titleId) ?? reported ?? null;
  }

  // ---- Reporting (paired devices only) ---------------------------------------------------------

  async heartbeat(auth: AuthContextData, input: GameHeartbeatInput): Promise<GameHeartbeatResponse> {
    if (auth.credential !== 'device') {
      throw new AppError(403, 'DEVICE_ONLY', 'Game activity can only be reported by a paired device.');
    }
    try {
      return await this.tryHeartbeat(auth, input);
    } catch (error) {
      // Serialized per user by the profile row lock; a duplicate can only appear if that lock was
      // bypassed by a concurrent writer, in which case the retry finds and continues the session.
      if (isUniqueViolation(error, 'game_activity_sessions_active_key')) return this.tryHeartbeat(auth, input);
      throw error;
    }
  }

  private async tryHeartbeat(auth: AuthContextData, input: GameHeartbeatInput): Promise<GameHeartbeatResponse> {
    const now = new Date();
    const deviceId = auth.sessionId;
    const outcome = await this.ctx.db.transaction(async (tx) => {
      // Lock the user's profile row: heartbeats of one user (from any device) run one at a time.
      const [profile] = await tx
        .select({ tracking: profiles.gameActivityTracking })
        .from(profiles)
        .where(eq(profiles.userId, auth.userId))
        .for('update');
      const active = await tx
        .select()
        .from(gameActivitySessions)
        .where(and(eq(gameActivitySessions.userId, auth.userId), isNull(gameActivitySessions.endedAt)));

      if (!profile?.tracking) {
        const ended = await this.endSessions(tx, active, now, 'tracking_disabled');
        return { disabled: true as const, changed: ended > 0 };
      }

      let changed = false;
      let continuing: SessionRow | null = null;
      for (const session of active) {
        if (isStale(session, now)) {
          changed = (await this.endSessions(tx, [session], now, 'stale')) > 0 || changed;
        } else if (session.titleId === input.titleId) {
          continuing = session;
        } else if (session.lastDeviceId === deviceId) {
          // This device now reports another game: the previous one ended.
          changed = (await this.endSessions(tx, [session], now, 'switched')) > 0 || changed;
        }
      }

      if (continuing) {
        const [updated] = await tx
          .update(gameActivitySessions)
          .set({
            lastHeartbeatAt: now,
            lastDeviceId: deviceId,
            heartbeatCount: sql`${gameActivitySessions.heartbeatCount} + 1`,
            ...(input.titleName ? { titleName: input.titleName } : {}),
          })
          .where(eq(gameActivitySessions.id, continuing.id))
          .returning();
        return { disabled: false as const, changed, session: updated!, created: false };
      }

      const [created] = await tx
        .insert(gameActivitySessions)
        .values({
          userId: auth.userId,
          titleId: input.titleId,
          titleName: input.titleName ?? null,
          platform: input.platform,
          startedAt: now,
          lastHeartbeatAt: now,
          startedByDeviceId: deviceId,
          lastDeviceId: deviceId,
        })
        .returning();
      return { disabled: false as const, changed: true, session: created!, created: true };
    });

    if (outcome.disabled) {
      if (outcome.changed) await this.afterChange(auth.userId);
      throw new AppError(
        409,
        'ACTIVITY_TRACKING_DISABLED',
        'Game activity tracking is turned off for this account. Turn it on in the privacy settings on the website.',
      );
    }
    if (outcome.changed) {
      await this.afterChange(auth.userId);
    } else {
      const cached = this.current.get(auth.userId);
      if (cached?.sessionId === outcome.session.id) cached.lastHeartbeatAt = outcome.session.lastHeartbeatAt;
      else await this.afterChange(auth.userId);
    }
    return {
      session: {
        id: outcome.session.id,
        titleId: outcome.session.titleId,
        startedAt: outcome.session.startedAt.toISOString(),
        created: outcome.created,
      },
      heartbeatIntervalSeconds: GAME_ACTIVITY.heartbeatIntervalSeconds,
      staleAfterSeconds: GAME_ACTIVITY.staleAfterSeconds,
    };
  }

  /** The device reports that the game it reported is no longer running. */
  async stop(auth: AuthContextData, _input: GameStopInput): Promise<{ ended: number }> {
    if (auth.credential !== 'device') {
      throw new AppError(403, 'DEVICE_ONLY', 'Game activity can only be reported by a paired device.');
    }
    const now = new Date();
    const ended = await this.ctx.db.transaction(async (tx) => {
      const sessions = await tx
        .select()
        .from(gameActivitySessions)
        .where(
          and(
            eq(gameActivitySessions.userId, auth.userId),
            isNull(gameActivitySessions.endedAt),
            eq(gameActivitySessions.lastDeviceId, auth.sessionId),
          ),
        )
        .for('update');
      return this.endSessions(tx, sessions, now, 'stopped');
    });
    if (ended > 0) await this.afterChange(auth.userId);
    return { ended };
  }

  /**
   * Ends sessions. Sessions whose heartbeats already stopped end at their last heartbeat, so
   * time that was never confirmed is never counted.
   */
  private async endSessions(tx: DbExecutor, sessions: SessionRow[], now: Date, reason: GameSessionEndReason): Promise<number> {
    let count = 0;
    for (const session of sessions) {
      const stale = isStale(session, now);
      const endedAt = stale ? session.lastHeartbeatAt : now;
      const rows = await tx
        .update(gameActivitySessions)
        .set({ endedAt, endReason: stale ? 'stale' : reason, lastHeartbeatAt: stale ? session.lastHeartbeatAt : now })
        .where(and(eq(gameActivitySessions.id, session.id), isNull(gameActivitySessions.endedAt)))
        .returning({ id: gameActivitySessions.id });
      count += rows.length;
    }
    return count;
  }

  /** Ends sessions without a heartbeat for longer than the stale limit (background maintenance). */
  async endStale(now = new Date()): Promise<number> {
    const cutoff = new Date(now.getTime() - STALE_MS);
    const ended = await this.ctx.db
      .update(gameActivitySessions)
      .set({ endedAt: sql`${gameActivitySessions.lastHeartbeatAt}`, endReason: 'stale' })
      .where(and(isNull(gameActivitySessions.endedAt), lt(gameActivitySessions.lastHeartbeatAt, cutoff)))
      .returning({ userId: gameActivitySessions.userId });
    for (const userId of new Set(ended.map((row) => row.userId))) await this.afterChange(userId);
    return ended.length;
  }

  /** Called before a device is unpaired: its running sessions end at their last heartbeat. */
  async endForDevice(userId: string, deviceId: string): Promise<void> {
    const ended = await this.ctx.db
      .update(gameActivitySessions)
      .set({ endedAt: sql`${gameActivitySessions.lastHeartbeatAt}`, endReason: 'device_revoked' })
      .where(
        and(
          eq(gameActivitySessions.userId, userId),
          eq(gameActivitySessions.lastDeviceId, deviceId),
          isNull(gameActivitySessions.endedAt),
        ),
      )
      .returning({ id: gameActivitySessions.id });
    if (ended.length > 0) await this.afterChange(userId);
  }

  /** Called when the user turns automatic tracking off. */
  async trackingDisabled(userId: string): Promise<void> {
    const now = new Date();
    const ended = await this.ctx.db.transaction(async (tx) => {
      const sessions = await tx
        .select()
        .from(gameActivitySessions)
        .where(and(eq(gameActivitySessions.userId, userId), isNull(gameActivitySessions.endedAt)))
        .for('update');
      return this.endSessions(tx, sessions, now, 'tracking_disabled');
    });
    if (ended > 0) await this.afterChange(userId);
  }

  /** Deletes the user's whole tracked history (including a running session). */
  async clearHistory(userId: string): Promise<{ deleted: number }> {
    const deleted = await this.ctx.db
      .delete(gameActivitySessions)
      .where(eq(gameActivitySessions.userId, userId))
      .returning({ id: gameActivitySessions.id });
    await this.afterChange(userId);
    return { deleted: deleted.length };
  }

  /** Refreshes the cached current game and tells the user's devices and friends. */
  private async afterChange(userId: string): Promise<void> {
    const cutoff = new Date(Date.now() - STALE_MS);
    const [latest] = await this.ctx.db
      .select()
      .from(gameActivitySessions)
      .where(
        and(
          eq(gameActivitySessions.userId, userId),
          isNull(gameActivitySessions.endedAt),
          sql`${gameActivitySessions.lastHeartbeatAt} >= ${cutoff}`,
        ),
      )
      .orderBy(desc(gameActivitySessions.startedAt))
      .limit(1);
    if (latest) this.current.set(userId, toActive(latest));
    else this.current.delete(userId);
    this.ctx.hub.sendToUser(userId, { type: 'activity.self', data: { game: this.currentOf(userId) } });
    await this.ctx.presence.publish(userId);
  }

  // ---- History -------------------------------------------------------------------------------

  async activityFor(viewerId: string, handle: string): Promise<GameActivityResponse> {
    const targetId = await this.ctx.users.resolveHandle(handle);
    if (!targetId) throw Errors.notFound('The user');
    const [row] = await this.ctx.db
      .select({
        status: users.status,
        lastSeenAt: users.lastSeenAt,
        activity: profiles.activity,
        presencePreference: profiles.presencePreference,
        presenceVisibility: profiles.presenceVisibility,
        lastSeenVisibility: profiles.lastSeenVisibility,
        profileVisibility: profiles.profileVisibility,
        gameActivityTracking: profiles.gameActivityTracking,
        gameActivityVisibility: profiles.gameActivityVisibility,
        gameHistoryVisibility: profiles.gameHistoryVisibility,
      })
      .from(users)
      .innerJoin(profiles, eq(profiles.userId, users.id))
      .where(eq(users.id, targetId))
      .limit(1);
    if (!row || row.status !== 'active') throw Errors.notFound('The user');

    const isSelf = viewerId === targetId;
    const [iBlocked, theyBlocked] = isSelf
      ? [false, false]
      : await Promise.all([this.ctx.relations.hasBlocked(viewerId, targetId), this.ctx.relations.hasBlocked(targetId, viewerId)]);
    if (theyBlocked) throw Errors.notFound('The user');
    const isFriend = !isSelf && (await this.ctx.relations.areFriends(viewerId, targetId));
    const viewer = { isSelf, isFriend, blocked: iBlocked };

    const presence = computePresenceView(this.ctx.presence.subjectFor({ id: targetId, ...row }), viewer);
    const current = presence.game;
    const restricted = !isSelf && row.profileVisibility === 'friends' && !isFriend;
    const historyVisible = isSelf || (!iBlocked && !restricted && audienceAllows(row.gameHistoryVisibility, viewer));
    const empty: GameActivityResponse = {
      current,
      historyVisible,
      recentGames: [],
      recentSessions: [],
      trackedSince: null,
      ...(isSelf ? { trackingEnabled: row.gameActivityTracking } : {}),
    };
    if (!historyVisible) return empty;

    // A running session is only part of the history when the viewer may see it running;
    // otherwise (invisible, offline to this viewer, hidden current game) only ended sessions count.
    const includeRunning = isSelf || current !== null;
    return {
      ...empty,
      ...(await this.history(targetId, includeRunning)),
    };
  }

  private async history(
    userId: string,
    includeRunning: boolean,
  ): Promise<Pick<GameActivityResponse, 'recentGames' | 'recentSessions' | 'trackedSince'>> {
    const s = gameActivitySessions;
    const end = sql`coalesce(${s.endedAt}, ${s.lastHeartbeatAt})`;
    const duration = sql<number>`greatest(0, extract(epoch from (${end} - ${s.startedAt})))::int`;
    const visible = includeRunning ? eq(s.userId, userId) : and(eq(s.userId, userId), sql`${s.endedAt} IS NOT NULL`);

    const games = await this.ctx.db
      .select({
        titleId: s.titleId,
        platform: sql<string>`(array_agg(${s.platform} ORDER BY ${s.startedAt} DESC))[1]`,
        reportedName: sql<string | null>`(array_agg(${s.titleName} ORDER BY ${s.startedAt} DESC) FILTER (WHERE ${s.titleName} IS NOT NULL))[1]`,
        lastPlayedAt: sql<Date>`max(${end})`,
        lastSessionSeconds: sql<number>`(array_agg(${duration} ORDER BY ${s.startedAt} DESC))[1]`,
        totalSeconds: sql<number>`sum(${duration})::bigint`,
        sessionCount: sql<number>`count(*)::int`,
        playingNow: sql<boolean>`bool_or(${s.endedAt} IS NULL AND ${s.lastHeartbeatAt} >= now() - make_interval(secs => ${GAME_ACTIVITY.staleAfterSeconds}))`,
      })
      .from(s)
      .where(visible)
      .groupBy(s.titleId)
      .orderBy(sql`max(${end}) DESC`)
      .limit(GAME_ACTIVITY.recentGamesMax);

    const sessions = await this.ctx.db
      .select({
        id: s.id,
        titleId: s.titleId,
        titleName: s.titleName,
        platform: s.platform,
        startedAt: s.startedAt,
        endedAt: s.endedAt,
        endReason: s.endReason,
        durationSeconds: duration,
      })
      .from(s)
      .where(visible)
      .orderBy(desc(s.startedAt))
      .limit(GAME_ACTIVITY.recentSessionsMax);

    const [first] = await this.ctx.db.select({ since: sql<Date | null>`min(${s.startedAt})` }).from(s).where(visible);

    const recentGames: GameHistoryEntry[] = games.map((game) => ({
      titleId: game.titleId,
      name: this.nameFor(game.titleId, game.reportedName),
      platform: game.platform as GameActivityPlatform,
      lastPlayedAt: new Date(game.lastPlayedAt).toISOString(),
      lastSessionSeconds: Number(game.lastSessionSeconds),
      totalSeconds: Number(game.totalSeconds),
      sessionCount: Number(game.sessionCount),
      playingNow: Boolean(game.playingNow),
    }));
    const recentSessions: GameSessionEntry[] = sessions.map((session) => ({
      id: session.id,
      titleId: session.titleId,
      name: this.nameFor(session.titleId, session.titleName),
      platform: session.platform as GameActivityPlatform,
      startedAt: session.startedAt.toISOString(),
      endedAt: session.endedAt ? session.endedAt.toISOString() : null,
      durationSeconds: Number(session.durationSeconds),
      endReason: session.endReason as GameSessionEntry['endReason'],
    }));
    return {
      recentGames,
      recentSessions,
      trackedSince: first?.since ? new Date(first.since).toISOString() : null,
    };
  }

  // ---- Curated names (administrators) ---------------------------------------------------------

  async listTitles(): Promise<GameTitleDto[]> {
    const rows = await this.ctx.db.select().from(gameTitles).orderBy(asc(gameTitles.titleId));
    return rows.map((row) => ({ titleId: row.titleId, name: row.name, updatedAt: row.updatedAt.toISOString() }));
  }

  async setTitle(actorId: string | null, titleId: string, name: string): Promise<GameTitleDto> {
    const now = new Date();
    const [row] = await this.ctx.db
      .insert(gameTitles)
      .values({ titleId, name, updatedById: actorId, updatedAt: now })
      .onConflictDoUpdate({ target: gameTitles.titleId, set: { name, updatedById: actorId, updatedAt: now } })
      .returning();
    this.titles.set(titleId, name);
    await this.republishPlaying(titleId);
    return { titleId: row!.titleId, name: row!.name, updatedAt: row!.updatedAt.toISOString() };
  }

  async removeTitle(titleId: string): Promise<void> {
    const deleted = await this.ctx.db.delete(gameTitles).where(eq(gameTitles.titleId, titleId)).returning({ id: gameTitles.titleId });
    if (deleted.length === 0) throw Errors.notFound('The game title');
    this.titles.delete(titleId);
    await this.republishPlaying(titleId);
  }

  /** Users currently playing a renamed title get a fresh presence event with the new name. */
  private async republishPlaying(titleId: string): Promise<void> {
    const userIds = [...this.current].filter(([, game]) => game.titleId === titleId).map(([userId]) => userId);
    if (userIds.length === 0) return;
    const rows = await this.ctx.db.select({ id: users.id }).from(users).where(inArray(users.id, userIds));
    for (const row of rows) {
      this.ctx.presence.invalidate(row.id);
      await this.afterChange(row.id);
    }
  }
}

function isStale(session: Pick<SessionRow, 'lastHeartbeatAt'>, now: Date): boolean {
  return now.getTime() - session.lastHeartbeatAt.getTime() > STALE_MS;
}

function toActive(row: SessionRow): ActiveGame {
  return {
    sessionId: row.id,
    titleId: row.titleId,
    reportedName: row.titleName,
    platform: row.platform as GameActivityPlatform,
    startedAt: row.startedAt,
    lastHeartbeatAt: row.lastHeartbeatAt,
  };
}
