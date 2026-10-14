import { and, eq, inArray, ne } from 'drizzle-orm';
import type { PresencePreference, SelfPresenceStatus } from '@akeno/shared';
import type { AppContext } from '../app/context.js';
import { profiles, users } from '../database/schema/index.js';
import type { ClientConnection } from './hub.js';
import { computePresenceView, effectiveSelfStatus, type PresenceSubject } from './presence-rules.js';

interface SubjectRow {
  id: string;
  status: string;
  lastSeenAt: Date | null;
  activity: string | null;
  presencePreference: PresencePreference;
  presenceVisibility: PresenceSubject['presenceVisibility'];
  lastSeenVisibility: PresenceSubject['lastSeenVisibility'];
  gameActivityVisibility: NonNullable<PresenceSubject['gameActivityVisibility']>;
}

/**
 * Presence is derived from live, authenticated WebSocket connections held in memory, combined
 * with the user's persisted preference (online / away / dnd / invisible) and privacy settings.
 *
 * - A user is online while at least one connection is alive; closing one of two devices keeps
 *   them online.
 * - Crashed clients are detected by heartbeat expiry in the WebSocket gateway.
 * - After a server restart all connections are gone, so everyone is offline until clients
 *   reconnect; nothing ephemeral needs to be restored from the database.
 */
export class PresenceService {
  /** Last presence payload broadcast to friends, used to suppress duplicate events. */
  private readonly lastBroadcast = new Map<string, string>();

  constructor(private readonly ctx: AppContext) {}

  isConnected(userId: string): boolean {
    return this.ctx.hub.isConnected(userId);
  }

  allIdle(userId: string): boolean {
    const connections = this.ctx.hub.connectionsOf(userId);
    if (connections.size === 0) return false;
    for (const connection of connections) if (!connection.idle) return false;
    return true;
  }

  /** Builds the presence subject for a user row loaded elsewhere (lists, profiles). */
  subjectFor(row: Omit<SubjectRow, 'id' | 'status'> & { id: string; status?: string }): PresenceSubject {
    return {
      connected: this.isConnected(row.id),
      allIdle: this.allIdle(row.id),
      preference: row.presencePreference,
      presenceVisibility: row.presenceVisibility,
      lastSeenVisibility: row.lastSeenVisibility,
      lastSeenAt: row.lastSeenAt,
      activity: row.activity,
      game: this.ctx.activity.currentOf(row.id),
      gameActivityVisibility: row.gameActivityVisibility,
      deleted: row.status === 'deleted',
    };
  }

  async selfStatus(userId: string): Promise<SelfPresenceStatus> {
    const row = await this.loadSubject(userId);
    if (!row) return 'offline';
    return effectiveSelfStatus(this.subjectFor(row));
  }

  async handleConnect(connection: ClientConnection): Promise<void> {
    await this.publish(connection.userId);
  }

  async handleDisconnect(connection: ClientConnection, lastForUser: boolean): Promise<void> {
    if (lastForUser) {
      // Record "last seen" only if the user was visibly online; invisible sessions must not leak.
      const row = await this.loadSubject(connection.userId);
      if (row && row.presencePreference !== 'invisible') {
        await this.ctx.db.update(users).set({ lastSeenAt: new Date() }).where(eq(users.id, connection.userId));
      }
    }
    await this.publish(connection.userId);
  }

  async handleIdle(connection: ClientConnection, idle: boolean): Promise<void> {
    if (connection.idle === idle) return;
    connection.idle = idle;
    await this.publish(connection.userId);
  }

  async setPreference(userId: string, preference: PresencePreference): Promise<SelfPresenceStatus> {
    const current = await this.loadSubject(userId);
    if (!current) return 'offline';
    const now = new Date();
    await this.ctx.db.transaction(async (tx) => {
      await tx.update(profiles).set({ presencePreference: preference, updatedAt: now }).where(eq(profiles.userId, userId));
      // Going invisible while connected: the user was visibly online until this moment.
      if (preference === 'invisible' && current.presencePreference !== 'invisible' && this.isConnected(userId)) {
        await tx.update(users).set({ lastSeenAt: now }).where(eq(users.id, userId));
      }
    });
    await this.publish(userId);
    return this.selfStatus(userId);
  }

  /**
   * Recomputes a user's presence and pushes it to their own devices and to connected friends.
   * Friends receive the friend-audience view, so privacy settings are applied before sending.
   */
  async publish(userId: string): Promise<void> {
    const row = await this.loadSubject(userId);
    if (!row) return;
    const subject = this.subjectFor(row);
    this.ctx.hub.sendToUser(userId, { type: 'self.presence', data: { status: effectiveSelfStatus(subject) } });

    const friendView = computePresenceView(subject, { isSelf: false, isFriend: true, blocked: false });
    const key = JSON.stringify(friendView);
    if (this.lastBroadcast.get(userId) === key) return;
    this.lastBroadcast.set(userId, key);
    if (!subject.connected && friendView.status === 'offline') {
      // Keep the map small: offline users are re-added when they publish again.
      this.lastBroadcast.delete(userId);
    }

    const friendIds = await this.ctx.relations.friendIds(userId);
    const connectedFriends = friendIds.filter((id) => this.ctx.hub.isConnected(id));
    this.ctx.hub.sendToUsers(connectedFriends, { type: 'presence.updated', data: { userId, presence: friendView } });
  }

  /** Forces the next publish to be sent even if it looks unchanged (e.g. after new friendships). */
  invalidate(userId: string): void {
    this.lastBroadcast.delete(userId);
  }

  /** Periodically persists last-seen for visibly online users so crashes do not lose it. */
  async flushLastSeen(): Promise<void> {
    const connected = this.ctx.hub.connectedUserIds();
    if (connected.length === 0) return;
    const visible = await this.ctx.db
      .select({ id: profiles.userId })
      .from(profiles)
      .where(and(inArray(profiles.userId, connected), ne(profiles.presencePreference, 'invisible')));
    if (visible.length === 0) return;
    const ids = visible.map((entry) => entry.id);
    await this.ctx.db.update(users).set({ lastSeenAt: new Date() }).where(inArray(users.id, ids));
  }

  private async loadSubject(userId: string): Promise<SubjectRow | null> {
    const [row] = await this.ctx.db
      .select({
        id: users.id,
        status: users.status,
        lastSeenAt: users.lastSeenAt,
        activity: profiles.activity,
        presencePreference: profiles.presencePreference,
        presenceVisibility: profiles.presenceVisibility,
        lastSeenVisibility: profiles.lastSeenVisibility,
        gameActivityVisibility: profiles.gameActivityVisibility,
      })
      .from(users)
      .innerJoin(profiles, eq(profiles.userId, users.id))
      .where(eq(users.id, userId))
      .limit(1);
    return row ?? null;
  }
}
