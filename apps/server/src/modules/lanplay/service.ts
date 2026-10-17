import { createHash, randomBytes, randomUUID } from 'node:crypto';
import { eq } from 'drizzle-orm';
import {
  findLanPlayGame,
  type LanPlayConnectionDto,
  type LanPlayConnectionMode,
  type LanPlayParticipantDto,
  type LanPlayPlatform,
  type LanPlayRole,
  type LanPlaySessionDto,
  type LanPlaySessionState,
  type LanPlayStatusDto,
  type LanPlayTransportStatus,
  type PartyLanPlayDto,
} from '@akeno/shared';
import type { AppContext } from '../../app/context.js';
import { parties } from '../../database/schema/index.js';
import { AppError } from '../../utils/errors.js';
import { platformPairs } from './capabilities.js';
import { type LanTransportAdapter, type TransportSession, UnavailableTransport } from './transport.js';

const NOT_ENABLED = 'AKENO LAN Play is not enabled on this server.';

export interface LanPlayTimings {
  /** A session is torn down this long after creation unless it is still in use. */
  sessionTtlMs: number;
  /** A connection credential is valid for this long; a participant refreshes by reconnecting. */
  credentialTtlMs: number;
}

export const DEFAULT_LAN_PLAY_TIMINGS: LanPlayTimings = {
  sessionTtlMs: 4 * 60 * 60 * 1000,
  credentialTtlMs: 2 * 60 * 1000,
};

interface Credential {
  hash: string;
  expiresAt: number;
}

interface Participant {
  userId: string;
  role: LanPlayRole;
  platform: LanPlayPlatform;
  joinedAt: number;
  /** Bumped on every revocation so an old credential can never be reused. */
  epoch: number;
  credential: Credential | null;
  connectionMode: LanPlayConnectionMode;
  lastConnectedAt: number | null;
}

interface Session {
  id: string;
  partyId: string;
  hostId: string;
  gameId: string | null;
  state: LanPlaySessionState;
  createdAt: number;
  expiresAt: number;
  participants: Map<string, Participant>;
  /** Browser session that created or last reconnected each participant (for sign-out revocation). */
  authSessions: Map<string, string>;
}

function iso(ms: number | null): string | null {
  return ms === null ? null : new Date(ms).toISOString();
}

/**
 * AKENO LAN Play control plane (experimental). It manages LAN sessions inside existing AKENO
 * parties: creation and termination, host and player roles, membership and authorization,
 * short-lived connection credentials, revocation and reconnection. It carries **no** game
 * traffic: a pluggable {@link LanTransportAdapter} would do that, and the only adapter wired here
 * is the unavailable one, so every session honestly reports "Transport not available".
 *
 * State is in memory, like voice and SharePlay: a restart ends every session and credential.
 */
export class LanPlayService {
  private readonly sessions = new Map<string, Session>();
  private readonly transport: LanTransportAdapter;
  readonly timings: LanPlayTimings;

  constructor(
    private readonly ctx: AppContext,
    options: { transport?: LanTransportAdapter; timings?: Partial<LanPlayTimings> } = {},
  ) {
    this.transport = options.transport ?? new UnavailableTransport();
    this.timings = { ...DEFAULT_LAN_PLAY_TIMINGS, ...options.timings };
  }

  get enabled(): boolean {
    return this.ctx.config.lanplay.enabled;
  }

  private get maxParticipants(): number {
    return this.ctx.config.lanplay.maxParticipants;
  }

  transportStatus(): LanPlayTransportStatus {
    const { available, reason } = this.transport.availability();
    return { id: this.transport.id, label: this.transport.label, available, reason };
  }

  status(): LanPlayStatusDto {
    const reason = this.enabled ? null : NOT_ENABLED;
    const transport = this.transportStatus();
    return {
      enabled: this.enabled,
      reason,
      transport,
      maxParticipants: this.maxParticipants,
      sessionTtlSeconds: Math.floor(this.timings.sessionTtlMs / 1000),
      credentialTtlSeconds: Math.floor(this.timings.credentialTtlMs / 1000),
      platforms: platformPairs(reason ?? transport.reason),
    };
  }

  sessionOf(partyId: string): Session | undefined {
    return this.sessions.get(partyId);
  }

  private requireEnabled(): void {
    if (!this.enabled) throw new AppError(503, 'LAN_PLAY_UNAVAILABLE', NOT_ENABLED);
  }

  private async requireMember(userId: string, partyId: string): Promise<void> {
    if ((await this.ctx.parties.currentPartyId(userId)) !== partyId) {
      throw new AppError(403, 'NOT_MEMBER', 'Only members of this party can use its LAN Play.');
    }
  }

  private requireSession(partyId: string): Session {
    const session = this.sessions.get(partyId);
    if (!session) throw new AppError(404, 'NO_LAN_SESSION', 'There is no LAN Play session in this party right now.');
    return session;
  }

  private async leaderOf(partyId: string): Promise<string | null> {
    const [party] = await this.ctx.db.select({ leaderId: parties.leaderId }).from(parties).where(eq(parties.id, partyId));
    return party?.leaderId ?? null;
  }

  // ---- Views ----------------------------------------------------------------------------------

  private participantDto(p: Participant): LanPlayParticipantDto {
    const connected = p.credential !== null && p.credential.expiresAt > Date.now();
    return {
      userId: p.userId,
      role: p.role,
      platform: p.platform,
      joinedAt: iso(p.joinedAt)!,
      connected,
      connectionMode: connected ? p.connectionMode : 'unavailable',
      lastConnectedAt: iso(p.lastConnectedAt),
    };
  }

  sessionDto(session: Session): LanPlaySessionDto {
    return {
      id: session.id,
      partyId: session.partyId,
      hostId: session.hostId,
      state: session.state,
      gameId: session.gameId,
      createdAt: iso(session.createdAt)!,
      expiresAt: iso(session.expiresAt)!,
      maxParticipants: this.maxParticipants,
      participants: [...session.participants.values()].map((p) => this.participantDto(p)),
      transport: this.transportStatus(),
    };
  }

  async partyView(userId: string, partyId: string): Promise<PartyLanPlayDto> {
    await this.requireMember(userId, partyId);
    const reason = this.enabled ? null : NOT_ENABLED;
    const session = reason ? undefined : this.sessions.get(partyId);
    const me = session?.participants.get(userId);
    const leaderId = session ? await this.leaderOf(partyId) : null;
    const isHost = session?.hostId === userId;
    return {
      available: reason === null,
      reason,
      session: session ? this.sessionDto(session) : null,
      you: {
        role: me?.role ?? null,
        canCreate: reason === null && !session,
        canJoin: Boolean(reason === null && session && session.state === 'open' && !me && session.participants.size < this.maxParticipants),
        canTerminate: Boolean(session && (isHost || leaderId === userId)),
        inSession: Boolean(me),
        connected: Boolean(me && me.credential && me.credential.expiresAt > Date.now()),
      },
    };
  }

  // ---- Lifecycle ------------------------------------------------------------------------------

  async create(userId: string, partyId: string, platform: LanPlayPlatform, gameId: string | null, authSessionId?: string): Promise<LanPlaySessionDto> {
    this.requireEnabled();
    await this.requireMember(userId, partyId);
    if (gameId && !findLanPlayGame(gameId)) throw new AppError(404, 'LAN_GAME_UNKNOWN', 'That game is not in the LAN Play compatibility list.');
    if (this.sessions.has(partyId)) {
      throw new AppError(409, 'LAN_SESSION_ACTIVE', 'This party already has a LAN Play session. Join it or end it first.');
    }
    const now = Date.now();
    const session: Session = {
      id: randomUUID(),
      partyId,
      hostId: userId,
      gameId,
      state: 'open',
      createdAt: now,
      expiresAt: now + this.timings.sessionTtlMs,
      participants: new Map(),
      authSessions: new Map(),
    };
    session.participants.set(userId, this.newParticipant(userId, 'host', platform, now));
    if (authSessionId) session.authSessions.set(userId, authSessionId);
    this.sessions.set(partyId, session);
    this.ctx.log.info({ partyId, sessionId: session.id }, 'LAN Play session created');
    await this.changed(partyId);
    return this.sessionDto(session);
  }

  async join(userId: string, partyId: string, platform: LanPlayPlatform, authSessionId?: string): Promise<LanPlaySessionDto> {
    this.requireEnabled();
    await this.requireMember(userId, partyId);
    const session = this.requireSession(partyId);
    if (session.state !== 'open') throw new AppError(409, 'NO_LAN_SESSION', 'This LAN Play session has ended.');
    if (!session.participants.has(userId) && session.participants.size >= this.maxParticipants) {
      throw new AppError(409, 'LAN_PARTICIPANTS_FULL', `This LAN Play session is full (up to ${this.maxParticipants}).`);
    }
    const existing = session.participants.get(userId);
    if (existing) {
      existing.platform = platform;
    } else {
      session.participants.set(userId, this.newParticipant(userId, 'player', platform, Date.now()));
    }
    if (authSessionId) session.authSessions.set(userId, authSessionId);
    await this.changed(partyId);
    return this.sessionDto(session);
  }

  async selectGame(userId: string, partyId: string, gameId: string | null): Promise<LanPlaySessionDto> {
    this.requireEnabled();
    await this.requireMember(userId, partyId);
    const session = this.requireSession(partyId);
    if (session.hostId !== userId) throw new AppError(403, 'NOT_LAN_HOST', 'Only the host can choose the game.');
    if (gameId && !findLanPlayGame(gameId)) throw new AppError(404, 'LAN_GAME_UNKNOWN', 'That game is not in the LAN Play compatibility list.');
    session.gameId = gameId;
    await this.changed(partyId);
    return this.sessionDto(session);
  }

  async leave(userId: string, partyId: string): Promise<void> {
    this.requireEnabled();
    const session = this.sessions.get(partyId);
    if (!session) return;
    if (session.hostId === userId) {
      await this.end(partyId, 'The host ended the LAN Play session.');
      return;
    }
    if (session.participants.has(userId)) await this.removeParticipant(session, userId, 'left the session');
  }

  async terminate(userId: string, partyId: string): Promise<void> {
    this.requireEnabled();
    const session = this.requireSession(partyId);
    if (session.hostId !== userId && (await this.leaderOf(partyId)) !== userId) {
      throw new AppError(403, 'NOT_LAN_HOST', 'Only the host or the party leader can end the LAN Play session.');
    }
    await this.end(partyId, 'The LAN Play session was ended.');
  }

  /**
   * A participant asks the transport to connect. The credential lifecycle is fully exercised, but
   * the only adapter wired in this build authorizes nothing, so this returns 503 with the honest
   * reason and no credential is issued.
   */
  async connect(userId: string, partyId: string, authSessionId?: string): Promise<LanPlayConnectionDto> {
    this.requireEnabled();
    await this.requireMember(userId, partyId);
    const session = this.requireSession(partyId);
    if (session.state !== 'open') throw new AppError(409, 'NO_LAN_SESSION', 'This LAN Play session has ended.');
    const participant = session.participants.get(userId);
    if (!participant) throw new AppError(409, 'NOT_IN_LAN_SESSION', 'Join the LAN Play session before connecting.');

    const transportSession: TransportSession = {
      id: session.id,
      partyId: session.partyId,
      participants: [...session.participants.values()].map((p) => ({ userId: p.userId, platform: p.platform, role: p.role })),
    };
    const grant = await this.transport.authorize(transportSession, { userId, platform: participant.platform, role: participant.role });
    if (!grant) {
      const { reason } = this.transport.availability();
      throw new AppError(503, 'LAN_TRANSPORT_UNAVAILABLE', reason ?? 'No LAN Play transport is available.');
    }

    const token = `lanc_${randomBytes(32).toString('base64url')}`;
    const expiresAt = Date.now() + this.timings.credentialTtlMs;
    participant.credential = { hash: hashToken(token), expiresAt };
    participant.connectionMode = grant.connectionMode;
    participant.lastConnectedAt = Date.now();
    if (authSessionId) session.authSessions.set(userId, authSessionId);
    await this.changed(partyId);
    return {
      sessionId: session.id,
      credential: token,
      expiresAt: iso(expiresAt)!,
      connectionMode: grant.connectionMode,
      transport: { id: this.transport.id, label: this.transport.label, connectable: grant.connectable, details: grant.details },
    };
  }

  /**
   * Validates a connection credential for a data plane (or a test). Returns the bound session and
   * user, or null when the credential is unknown, expired or revoked. Credentials are stored only
   * as SHA-256 hashes.
   */
  verifyCredential(token: string): { sessionId: string; userId: string } | null {
    const hash = hashToken(token);
    const now = Date.now();
    for (const session of this.sessions.values()) {
      for (const p of session.participants.values()) {
        if (p.credential && p.credential.hash === hash && p.credential.expiresAt > now) {
          return { sessionId: session.id, userId: p.userId };
        }
      }
    }
    return null;
  }

  // ---- Membership and safety hooks ------------------------------------------------------------

  onMemberLeft(partyId: string, userId: string): void {
    const session = this.sessions.get(partyId);
    if (!session) return;
    if (session.hostId === userId) void this.end(partyId, 'The host left the party.');
    else if (session.participants.has(userId)) void this.removeParticipant(session, userId, 'left the party');
  }

  onPartyClosed(partyId: string): void {
    void this.end(partyId, 'The party ended.');
  }

  onBlock(blockerId: string, blockedId: string): void {
    for (const session of [...this.sessions.values()]) {
      const other = session.hostId === blockerId ? blockedId : session.hostId === blockedId ? blockerId : null;
      if (other && session.participants.has(other)) void this.removeParticipant(session, other, 'a block');
      else if (session.hostId === blockerId || session.hostId === blockedId) {
        // The host blocked or was blocked by a participant who is not in this session: nothing.
      }
    }
  }

  removeUserEverywhere(userId: string): void {
    for (const session of [...this.sessions.values()]) {
      if (session.hostId === userId) void this.end(session.partyId, 'The host is no longer available.');
      else if (session.participants.has(userId)) void this.removeParticipant(session, userId, 'no longer available');
    }
  }

  onSessionsRevoked(userId: string, revoked: (sessionId: string) => boolean): void {
    for (const session of [...this.sessions.values()]) {
      const authSessionId = session.authSessions.get(userId);
      if (authSessionId && revoked(authSessionId)) {
        if (session.hostId === userId) void this.end(session.partyId, 'The host signed out.');
        else if (session.participants.has(userId)) void this.removeParticipant(session, userId, 'signed out');
      }
    }
  }

  // ---- Internals ------------------------------------------------------------------------------

  private newParticipant(userId: string, role: LanPlayRole, platform: LanPlayPlatform, now: number): Participant {
    return { userId, role, platform, joinedAt: now, epoch: 1, credential: null, connectionMode: 'unavailable', lastConnectedAt: null };
  }

  private async removeParticipant(session: Session, userId: string, reason: string): Promise<void> {
    const participant = session.participants.get(userId);
    if (!participant) return;
    participant.epoch += 1;
    participant.credential = null;
    session.participants.delete(userId);
    session.authSessions.delete(userId);
    await this.transport.revoke(session.id, userId).catch((error: unknown) =>
      this.ctx.log.warn({ err: error, partyId: session.partyId }, 'LAN Play transport revoke failed'),
    );
    this.ctx.log.info({ partyId: session.partyId, reason }, 'LAN Play participant removed');
    await this.changed(session.partyId);
  }

  private async end(partyId: string, reason: string): Promise<void> {
    const session = this.sessions.get(partyId);
    if (!session) return;
    session.state = 'closed';
    this.sessions.delete(partyId);
    await this.transport.release(session.id).catch((error: unknown) =>
      this.ctx.log.warn({ err: error, partyId }, 'LAN Play transport release failed'),
    );
    this.ctx.log.info({ partyId, sessionId: session.id, reason }, 'LAN Play session ended');
    await this.changed(partyId, true);
  }

  /** Ends sessions past their TTL. Called on a timer. */
  sweep(): void {
    const now = Date.now();
    for (const session of [...this.sessions.values()]) {
      if (session.expiresAt <= now) void this.end(session.partyId, 'The LAN Play session expired.');
    }
  }

  shutdown(): void {
    this.sessions.clear();
  }

  private async changed(partyId: string, ended = false): Promise<void> {
    try {
      const memberIds = await this.ctx.parties.memberIds(partyId);
      if (memberIds.length > 0) this.ctx.hub.sendToUsers(memberIds, { type: 'lanplay.changed', data: { partyId, ended } });
    } catch (error) {
      this.ctx.log.warn({ err: error, partyId }, 'Could not announce a LAN Play change');
    }
  }
}

function hashToken(token: string): string {
  return createHash('sha256').update(token).digest('hex');
}
