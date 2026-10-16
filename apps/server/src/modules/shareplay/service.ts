import { randomInt, randomUUID } from 'node:crypto';
import { eq } from 'drizzle-orm';
import { AccessToken, RoomServiceClient, TrackSource, type WebhookEvent } from 'livekit-server-sdk';
import {
  INPUT_HEARTBEAT_MS,
  INPUT_MAX_RATE_HZ,
  INPUT_PROTOCOL_VERSION,
  INPUT_SLOT_PLAYER_2,
  INPUT_WATCHDOG_MS,
  InputSequencer,
  SHAREPLAY_DEFAULT_QUALITY,
  qualitiesUpTo,
  type PartySharePlayDto,
  type SharePlayControllerGrantDto,
  type SharePlayQuality,
  type SharePlayRole,
  type SharePlaySessionDto,
  type SharePlaySource,
  type SharePlayState,
  type SharePlayStatusDto,
  type SharePlayTokenResponse,
} from '@akeno/shared';
import type { AppContext } from '../../app/context.js';
import { parties } from '../../database/schema/index.js';
import { AppError, Errors } from '../../utils/errors.js';
import { InputRelay } from './input-relay.js';
import { sourceCapabilities, VIRTUAL_CONTROLLER_UNSUPPORTED } from './capabilities.js';

const ROOM_PREFIX = 'akeno-shareplay-';
const UUID = /^[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}$/;

const NOT_ENABLED = 'SharePlay is not enabled on this server.';
const NEEDS_MEDIA_SERVER = 'SharePlay needs the voice server (LiveKit), which is not set up on this server.';

/** Timings (milliseconds). Exposed so tests can shorten them; production uses the defaults. */
export interface SharePlayTimings {
  /** The host must connect and start publishing within this time after "Start SharePlay". */
  startTimeoutMs: number;
  /** A host whose connection dropped may reconnect within this time before the session ends. */
  hostGraceMs: number;
  /** An unanswered "Request Player 2" expires after this time. */
  requestTtlMs: number;
  /** A guest whose controller link dropped may reconnect within this time before the grant ends. */
  inputReconnectGraceMs: number;
}

export const DEFAULT_SHAREPLAY_TIMINGS: SharePlayTimings = {
  startTimeoutMs: 90_000,
  hostGraceMs: 30_000,
  requestTtlMs: 60_000,
  inputReconnectGraceMs: 10_000,
};

export interface ControllerGrant {
  grantId: string;
  epoch: number;
  guestId: string;
  slot: number;
  grantedAt: number;
  expiresAt: number;
  accepted: number;
  rejected: number;
  lastInputAt: number | null;
  inputConnected: boolean;
  /** Last accepted sequence number of this grant (kept across reconnects of the guest's link). */
  sequencer: InputSequencer;
  expiryTimer: NodeJS.Timeout | null;
  reconnectTimer: NodeJS.Timeout | null;
}

export interface SharePlaySession {
  id: string;
  partyId: string;
  hostId: string;
  source: SharePlaySource;
  quality: SharePlayQuality;
  state: SharePlayState;
  startedAt: number;
  liveAt: number | null;
  hostConnected: boolean;
  viewers: Map<string, { joinedAt: number }>;
  requests: Map<string, { requestedAt: number; expiresAt: number }>;
  grant: ControllerGrant | null;
  /** Start timeout or host reconnect grace. */
  timer: NodeJS.Timeout | null;
}

/** LiveKit answers 404 / "not_found" for rooms or participants that do not exist (already gone). */
function isNotFound(error: unknown): boolean {
  const twirp = error as { status?: number; code?: string; message?: string };
  return twirp?.status === 404 || twirp?.code === 'not_found' || /not.?found|does not exist/i.test(String(twirp?.message ?? ''));
}

function iso(ms: number | null): string | null {
  return ms === null ? null : new Date(ms).toISOString();
}

/**
 * SharePlay sessions (Alpha 0.5, experimental). One broadcaster per party streams video and game
 * audio to the other members through LiveKit, in a room separate from the party's voice room so
 * a video problem never touches voice. The AKENO server only authorizes; media never passes
 * through this process and nothing is recorded (no egress).
 *
 * Least privilege, enforced in the LiveKit token (and re-checked on every join webhook):
 * - the host may publish only screen-share video and screen-share audio, and subscribes to nothing;
 * - viewers may only subscribe;
 * - nobody may publish data, change metadata or publish a microphone or camera in this room.
 *
 * Player 2 controller access is never implied by membership or by watching: the guest asks, the
 * host explicitly approves, and the grant (with a fresh epoch) ends on revocation, expiry, the
 * guest or host leaving the stream or the party, blocks, suspension, session sign-out and the
 * session ending. Controller packets are relayed by InputRelay only while that grant is valid.
 *
 * State is kept in memory (single backend instance, like voice presence). A server restart ends
 * every session and every grant; clients see the session disappear.
 */
export class SharePlayService {
  private readonly sessions = new Map<string, SharePlaySession>();
  /** Browser session that requested each user's latest SharePlay token. */
  private readonly tokenSessions = new Map<string, string>();
  /** Console apps that asked for Player 2 input (device ID → expiry); older apps never ask. */
  private readonly receivers = new Map<string, number>();
  private readonly client: RoomServiceClient | null;
  /** Grant epochs start at a random point so a restart does not reuse recent numbers. */
  private nextEpoch = randomInt(1, 0x7fffffff);
  readonly relay: InputRelay;
  timings: SharePlayTimings = { ...DEFAULT_SHAREPLAY_TIMINGS };

  constructor(private readonly ctx: AppContext) {
    const { voice, shareplay } = ctx.config;
    this.client =
      shareplay.enabled && voice.enabled ? new RoomServiceClient(voice.apiUrl, voice.apiKey, voice.apiSecret, { requestTimeout: 5, failover: false }) : null;
    this.relay = new InputRelay(ctx, this);
  }

  /** SharePlay is switched on and has a media server. */
  get enabled(): boolean {
    return this.client !== null;
  }

  get remoteInputEnabled(): boolean {
    return this.enabled && this.ctx.config.shareplay.remoteInputEnabled;
  }

  roomName(partyId: string): string {
    return `${ROOM_PREFIX}${partyId}`;
  }

  partyIdOf(roomName: string | undefined): string | null {
    if (!roomName?.startsWith(ROOM_PREFIX)) return null;
    const id = roomName.slice(ROOM_PREFIX.length);
    return UUID.test(id) ? id : null;
  }

  sessionOf(partyId: string): SharePlaySession | undefined {
    return this.sessions.get(partyId);
  }

  private unavailableReason(): string | null {
    if (!this.ctx.config.shareplay.enabled) return NOT_ENABLED;
    if (!this.ctx.config.voice.enabled) return NEEDS_MEDIA_SERVER;
    return null;
  }

  status(): SharePlayStatusDto {
    const reason = this.unavailableReason();
    const { shareplay } = this.ctx.config;
    const remoteReason = reason ?? (shareplay.remoteInputEnabled ? null : 'Remote controller input is switched off on this server.');
    return {
      enabled: reason === null,
      reason,
      maxViewers: shareplay.maxViewers,
      qualities: qualitiesUpTo(shareplay.maxQuality),
      defaultQuality: this.defaultQuality(),
      sources: sourceCapabilities(reason),
      remoteInput: {
        supported: remoteReason === null,
        reason: remoteReason,
        watchdogMs: INPUT_WATCHDOG_MS,
        heartbeatMs: INPUT_HEARTBEAT_MS,
        maxRateHz: INPUT_MAX_RATE_HZ,
        protocolVersion: INPUT_PROTOCOL_VERSION,
      },
      virtualController: { supported: false, reason: VIRTUAL_CONTROLLER_UNSUPPORTED },
    };
  }

  private defaultQuality(): SharePlayQuality {
    const allowed = qualitiesUpTo(this.ctx.config.shareplay.maxQuality);
    return allowed.includes(SHAREPLAY_DEFAULT_QUALITY) ? SHAREPLAY_DEFAULT_QUALITY : allowed[allowed.length - 1]!;
  }

  private requireEnabled(): RoomServiceClient {
    const reason = this.unavailableReason();
    if (reason || !this.client) throw new AppError(503, 'SHAREPLAY_UNAVAILABLE', reason ?? NOT_ENABLED);
    return this.client;
  }

  private async requireMember(userId: string, partyId: string): Promise<void> {
    if ((await this.ctx.parties.currentPartyId(userId)) !== partyId) {
      throw new AppError(403, 'NOT_MEMBER', 'Only members of this party can use its SharePlay.');
    }
  }

  private requireSession(partyId: string): SharePlaySession {
    const session = this.sessions.get(partyId);
    if (!session) throw new AppError(404, 'NO_SHAREPLAY', 'Nobody is streaming in this party right now.');
    return session;
  }

  // ---- Views ----------------------------------------------------------------------------------

  private grantDto(grant: ControllerGrant): SharePlayControllerGrantDto {
    return {
      grantId: grant.grantId,
      epoch: grant.epoch,
      guestId: grant.guestId,
      slot: grant.slot,
      grantedAt: iso(grant.grantedAt)!,
      expiresAt: iso(grant.expiresAt)!,
      inputConnected: grant.inputConnected,
      lastInputAt: iso(grant.lastInputAt),
      packetsAccepted: grant.accepted,
      packetsRejected: grant.rejected,
    };
  }

  sessionDto(session: SharePlaySession): SharePlaySessionDto {
    const now = Date.now();
    return {
      id: session.id,
      partyId: session.partyId,
      hostId: session.hostId,
      source: session.source,
      quality: session.quality,
      state: session.state,
      startedAt: iso(session.startedAt)!,
      liveAt: iso(session.liveAt),
      hostConnected: session.hostConnected,
      viewerIds: [...session.viewers.keys()],
      maxViewers: this.ctx.config.shareplay.maxViewers,
      controller: {
        enabled: this.remoteInputEnabled,
        requests: [...session.requests.entries()]
          .filter(([, request]) => request.expiresAt > now)
          .map(([userId, request]) => ({ userId, requestedAt: iso(request.requestedAt)!, expiresAt: iso(request.expiresAt)! })),
        grant: session.grant ? this.grantDto(session.grant) : null,
      },
    };
  }

  /** SharePlay of a party as one member sees it. */
  async partyView(userId: string, partyId: string): Promise<PartySharePlayDto> {
    await this.requireMember(userId, partyId);
    const reason = this.unavailableReason();
    const session = reason ? undefined : this.sessions.get(partyId);
    const leaderId = session ? await this.leaderOf(partyId) : null;
    const isHost = session?.hostId === userId;
    const role: SharePlayRole | null = session ? (isHost ? 'host' : 'viewer') : null;
    const request = session?.requests.get(userId);
    const pending = Boolean(request && request.expiresAt > Date.now());
    return {
      available: reason === null,
      reason,
      session: session ? this.sessionDto(session) : null,
      you: {
        role,
        canStart: reason === null && !session,
        canStop: Boolean(session && (isHost || leaderId === userId)),
        canWatch: Boolean(session && !isHost),
        canRequestController: Boolean(session && !isHost && this.remoteInputEnabled && session.state === 'live' && !session.grant && !pending),
        controllerRequestPending: pending,
        hasControllerGrant: session?.grant?.guestId === userId,
      },
    };
  }

  private async leaderOf(partyId: string): Promise<string | null> {
    const [party] = await this.ctx.db.select({ leaderId: parties.leaderId }).from(parties).where(eq(parties.id, partyId));
    return party?.leaderId ?? null;
  }

  // ---- Starting and stopping ------------------------------------------------------------------

  /**
   * Starts a session with the caller as host. Nothing is captured here: the host's browser asks
   * the person to pick what to share (or which capture device) after this succeeds.
   */
  async start(userId: string, partyId: string, source: SharePlaySource, quality: SharePlayQuality | undefined, sessionId?: string): Promise<SharePlaySessionDto> {
    this.requireEnabled();
    await this.requireMember(userId, partyId);
    const capability = sourceCapabilities(null)[source];
    if (!capability.supported) throw new AppError(409, 'SOURCE_UNSUPPORTED', capability.reason ?? 'This capture source is not supported.');
    const allowed = qualitiesUpTo(this.ctx.config.shareplay.maxQuality);
    const chosen = quality ?? this.defaultQuality();
    if (!allowed.includes(chosen)) throw new AppError(400, 'QUALITY_NOT_ALLOWED', `This server allows up to ${this.ctx.config.shareplay.maxQuality}.`);
    if (this.sessions.has(partyId)) {
      throw new AppError(409, 'SHAREPLAY_ACTIVE', 'Someone in this party is already streaming. Only one SharePlay stream per party is possible.');
    }
    const session: SharePlaySession = {
      id: randomUUID(),
      partyId,
      hostId: userId,
      source,
      quality: chosen,
      state: 'starting',
      startedAt: Date.now(),
      liveAt: null,
      hostConnected: false,
      viewers: new Map(),
      requests: new Map(),
      grant: null,
      timer: null,
    };
    this.sessions.set(partyId, session);
    if (sessionId) this.tokenSessions.set(userId, sessionId);
    this.armTimer(session, this.timings.startTimeoutMs, 'The stream did not start in time.');
    this.ctx.log.info({ partyId, sessionId: session.id, source, quality: chosen }, 'SharePlay started');
    await this.changed(partyId);
    return this.sessionDto(session);
  }

  /** Ends a session when the timer fires (start timeout, host did not come back). */
  private armTimer(session: SharePlaySession, ms: number, reason: string): void {
    if (session.timer) clearTimeout(session.timer);
    session.timer = setTimeout(() => {
      session.timer = null;
      if (this.sessions.get(session.partyId) === session) void this.end(session.partyId, reason);
    }, ms);
    session.timer.unref();
  }

  private disarmTimer(session: SharePlaySession): void {
    if (session.timer) clearTimeout(session.timer);
    session.timer = null;
  }

  /** The host, or the party leader, stops the stream for everyone. */
  async stop(userId: string, partyId: string): Promise<void> {
    await this.requireMember(userId, partyId);
    const session = this.requireSession(partyId);
    if (session.hostId !== userId && (await this.leaderOf(partyId)) !== userId) {
      throw Errors.forbidden('Only the person streaming or the party leader can stop SharePlay.');
    }
    await this.end(partyId, session.hostId === userId ? 'The host stopped SharePlay.' : 'The party leader stopped SharePlay.');
  }

  async setQuality(userId: string, partyId: string, quality: SharePlayQuality): Promise<SharePlaySessionDto> {
    const session = this.requireSession(partyId);
    if (session.hostId !== userId) throw Errors.forbidden('Only the person streaming can change the stream quality.');
    if (!qualitiesUpTo(this.ctx.config.shareplay.maxQuality).includes(quality)) {
      throw new AppError(400, 'QUALITY_NOT_ALLOWED', `This server allows up to ${this.ctx.config.shareplay.maxQuality}.`);
    }
    session.quality = quality;
    await this.changed(partyId);
    return this.sessionDto(session);
  }

  /** Ends a session: grant revoked (input released), viewers disconnected, room deleted. */
  async end(partyId: string, reason: string): Promise<void> {
    const session = this.sessions.get(partyId);
    if (!session) return;
    this.sessions.delete(partyId);
    this.disarmTimer(session);
    this.clearGrant(session, reason);
    this.relay.closeMonitors(partyId, reason);
    this.ctx.log.info({ partyId, sessionId: session.id, reason }, 'SharePlay ended');
    if (this.client) {
      try {
        await this.client.deleteRoom(this.roomName(partyId));
      } catch (error) {
        if (!isNotFound(error)) this.ctx.log.warn({ err: error, partyId }, 'Could not delete a SharePlay room; reconciliation will retry');
      }
    }
    await this.changed(partyId, true);
  }

  // ---- Tokens ---------------------------------------------------------------------------------

  /** A join token for the SharePlay room. The role is decided here, never by the client. */
  async issueToken(userId: string, partyId: string, sessionId?: string): Promise<SharePlayTokenResponse> {
    const client = this.requireEnabled();
    await this.requireMember(userId, partyId);
    const session = this.requireSession(partyId);
    const role: SharePlayRole = session.hostId === userId ? 'host' : 'viewer';
    if (role === 'viewer') {
      if ((await this.ctx.relations.blockedSet(userId, [session.hostId])).size > 0) {
        throw new AppError(403, 'BLOCKED', 'You cannot watch this stream.');
      }
      if (!session.viewers.has(userId) && session.viewers.size >= this.ctx.config.shareplay.maxViewers) {
        throw new AppError(409, 'VIEWERS_FULL', `This stream already has the maximum of ${this.ctx.config.shareplay.maxViewers} viewers.`);
      }
    }
    const room = this.roomName(partyId);
    try {
      await client.createRoom({ name: room, emptyTimeout: 30, departureTimeout: 20, maxParticipants: this.ctx.config.shareplay.maxViewers + 1 });
    } catch (error) {
      this.ctx.log.error({ err: error }, 'Media server unreachable while creating a SharePlay room');
      throw new AppError(503, 'SHAREPLAY_UNAVAILABLE', 'The streaming server is not reachable right now. Voice and party chat are not affected.');
    }
    const user = await this.ctx.users.getSummary(userId);
    const ttl = this.ctx.config.voice.tokenTtlSeconds;
    const token = new AccessToken(this.ctx.config.voice.apiKey, this.ctx.config.voice.apiSecret, {
      identity: userId,
      name: user.displayName,
      ttl,
      metadata: JSON.stringify({ role, sessionId: session.id }),
    });
    token.addGrant({
      room,
      roomJoin: true,
      canPublish: role === 'host',
      canPublishSources: role === 'host' ? [TrackSource.SCREEN_SHARE, TrackSource.SCREEN_SHARE_AUDIO] : [],
      canSubscribe: role === 'viewer',
      canPublishData: false,
      canUpdateOwnMetadata: false,
    });
    if (sessionId) this.tokenSessions.set(userId, sessionId);
    return {
      url: this.ctx.config.voice.publicUrl,
      token: await token.toJwt(),
      room,
      identity: userId,
      role,
      sessionId: session.id,
      expiresAt: new Date(Date.now() + ttl * 1000).toISOString(),
    };
  }

  // ---- Console diagnostic receivers -----------------------------------------------------------

  static readonly RECEIVER_TTL_MS = 90_000;

  /**
   * A paired console app (the device of a member) asks to receive Player 2 input for display.
   * Only registered devices get "shareplay.input" events, so console apps that do not know the
   * event never receive it. Renewed by the app every 30 s while its SharePlay view is open.
   */
  async registerReceiver(userId: string, deviceId: string, partyId: string): Promise<{ expiresAt: string }> {
    this.requireEnabled();
    await this.requireMember(userId, partyId);
    const expiresAt = Date.now() + SharePlayService.RECEIVER_TTL_MS;
    this.receivers.set(deviceId, expiresAt);
    for (const [id, until] of this.receivers) if (until <= Date.now()) this.receivers.delete(id);
    return { expiresAt: new Date(expiresAt).toISOString() };
  }

  isReceiver(deviceId: string): boolean {
    const until = this.receivers.get(deviceId);
    return until !== undefined && until > Date.now();
  }

  // ---- Player 2 -------------------------------------------------------------------------------

  private requireRemoteInput(): void {
    this.requireEnabled();
    if (!this.ctx.config.shareplay.remoteInputEnabled) {
      throw new AppError(503, 'REMOTE_INPUT_DISABLED', 'Remote controller input is switched off on this server.');
    }
  }

  async requestController(userId: string, partyId: string): Promise<void> {
    this.requireRemoteInput();
    await this.requireMember(userId, partyId);
    const session = this.requireSession(partyId);
    if (session.hostId === userId) throw Errors.badRequest('You are the one streaming; your own controller is Player 1.');
    if (session.state !== 'live' || !session.hostConnected) throw new AppError(409, 'NOT_LIVE', 'The stream is not live yet.');
    if (session.grant?.guestId === userId) return;
    if (session.grant) throw new AppError(409, 'PLAYER2_TAKEN', 'Someone else already has Player 2.');
    if ((await this.ctx.relations.blockedSet(userId, [session.hostId])).size > 0) throw new AppError(403, 'BLOCKED', 'You cannot request Player 2 here.');
    const now = Date.now();
    session.requests.set(userId, { requestedAt: now, expiresAt: now + this.timings.requestTtlMs });
    const timer = setTimeout(() => {
      const request = session.requests.get(userId);
      if (request && request.expiresAt <= Date.now()) {
        session.requests.delete(userId);
        void this.changed(partyId);
      }
    }, this.timings.requestTtlMs + 50);
    timer.unref();
    await this.changed(partyId);
  }

  async cancelRequest(userId: string, partyId: string): Promise<void> {
    const session = this.requireSession(partyId);
    if (session.requests.delete(userId)) await this.changed(partyId);
  }

  /**
   * The host approves a pending request. Approval needs a live request: there is nothing to
   * approve without one, and an approval can never be replayed (each grant has a new id and epoch).
   */
  async approveController(hostId: string, partyId: string, guestId: string): Promise<SharePlayControllerGrantDto> {
    this.requireRemoteInput();
    await this.requireMember(hostId, partyId);
    const session = this.requireSession(partyId);
    if (session.hostId !== hostId) throw Errors.forbidden('Only the person streaming can hand out Player 2.');
    const request = session.requests.get(guestId);
    if (!request || request.expiresAt <= Date.now()) {
      session.requests.delete(guestId);
      throw new AppError(409, 'NO_REQUEST', 'That person has not asked for Player 2 (or the request expired).');
    }
    if (session.grant) throw new AppError(409, 'PLAYER2_TAKEN', 'Player 2 is already taken. Revoke it first.');
    if ((await this.ctx.parties.currentPartyId(guestId)) !== partyId) {
      session.requests.delete(guestId);
      throw new AppError(409, 'NOT_MEMBER', 'That person is no longer in the party.');
    }
    if ((await this.ctx.relations.blockedSet(hostId, [guestId])).size > 0) throw new AppError(403, 'BLOCKED', 'You cannot give Player 2 to this person.');
    session.requests.delete(guestId);
    const now = Date.now();
    const epoch = this.nextEpoch;
    this.nextEpoch = this.nextEpoch >= 0xfffffff0 ? 1 : this.nextEpoch + 1;
    const grant: ControllerGrant = {
      grantId: randomUUID(),
      epoch,
      guestId,
      slot: INPUT_SLOT_PLAYER_2,
      grantedAt: now,
      expiresAt: now + this.ctx.config.shareplay.grantMaxMs,
      accepted: 0,
      rejected: 0,
      lastInputAt: null,
      inputConnected: false,
      sequencer: new InputSequencer(),
      expiryTimer: null,
      reconnectTimer: null,
    };
    grant.expiryTimer = setTimeout(() => this.revokeIfCurrent(partyId, grant, 'Player 2 access expired. Ask again to continue.'), grant.expiresAt - now);
    grant.expiryTimer.unref();
    session.grant = grant;
    this.ctx.log.info({ partyId, sessionId: session.id, grantId: grant.grantId }, 'SharePlay Player 2 granted');
    await this.changed(partyId);
    return this.grantDto(grant);
  }

  async denyController(hostId: string, partyId: string, guestId: string): Promise<void> {
    const session = this.requireSession(partyId);
    if (session.hostId !== hostId) throw Errors.forbidden('Only the person streaming can answer Player 2 requests.');
    if (session.requests.delete(guestId)) await this.changed(partyId);
  }

  /** The host revokes Player 2, or the guest gives it back. Takes effect at once. */
  async revokeController(userId: string, partyId: string): Promise<void> {
    const session = this.requireSession(partyId);
    const grant = session.grant;
    if (!grant) return;
    if (session.hostId !== userId && grant.guestId !== userId) throw Errors.forbidden('Only the host or Player 2 can end Player 2 access.');
    await this.revokeGrant(partyId, session.hostId === userId ? 'The host revoked Player 2.' : 'Player 2 gave the controller back.');
  }

  private revokeIfCurrent(partyId: string, grant: ControllerGrant, reason: string): void {
    if (this.sessions.get(partyId)?.grant === grant) void this.revokeGrant(partyId, reason);
  }

  async revokeGrant(partyId: string, reason: string): Promise<void> {
    const session = this.sessions.get(partyId);
    if (!session?.grant) return;
    this.clearGrant(session, reason);
    await this.changed(partyId);
  }

  /** Drops the grant: receivers are told to release everything, the guest's link is closed. */
  private clearGrant(session: SharePlaySession, reason: string): void {
    const grant = session.grant;
    if (!grant) return;
    session.grant = null;
    if (grant.expiryTimer) clearTimeout(grant.expiryTimer);
    if (grant.reconnectTimer) clearTimeout(grant.reconnectTimer);
    this.relay.grantEnded(session, grant, reason);
    this.ctx.log.info({ partyId: session.partyId, grantId: grant.grantId, reason }, 'SharePlay Player 2 ended');
  }

  /** Called by the relay when the guest's controller link opens or closes. */
  inputLinkChanged(partyId: string, grant: ControllerGrant, connected: boolean): void {
    const session = this.sessions.get(partyId);
    if (session?.grant !== grant) return;
    grant.inputConnected = connected;
    if (grant.reconnectTimer) clearTimeout(grant.reconnectTimer);
    grant.reconnectTimer = null;
    if (!connected) {
      grant.reconnectTimer = setTimeout(
        () => this.revokeIfCurrent(partyId, grant, 'Player 2’s connection was lost.'),
        this.timings.inputReconnectGraceMs,
      );
      grant.reconnectTimer.unref();
    }
    void this.changed(partyId);
  }

  // ---- Membership and safety hooks (called by parties, friends, administration, auth) --------

  /** A member left or was removed from the party. */
  onMemberLeft(partyId: string, userId: string): void {
    const session = this.sessions.get(partyId);
    if (!session) return;
    if (session.hostId === userId) {
      void this.end(partyId, 'The host left the party.');
      return;
    }
    void this.removeFromSession(session, userId, 'Player 2 left the party.');
  }

  onPartyClosed(partyId: string): void {
    void this.end(partyId, 'The party ended.');
  }

  /** A block between two users: the one who is not the host leaves the stream (and Player 2). */
  onBlock(blockerId: string, blockedId: string): void {
    for (const session of [...this.sessions.values()]) {
      const other = session.hostId === blockerId ? blockedId : session.hostId === blockedId ? blockerId : null;
      if (other) void this.removeFromSession(session, other, 'Player 2 access ended because of a block.');
    }
  }

  /** Suspension or account deletion. */
  removeUserEverywhere(userId: string): void {
    for (const session of [...this.sessions.values()]) {
      if (session.hostId === userId) void this.end(session.partyId, 'The host is no longer available.');
      else void this.removeFromSession(session, userId, 'Player 2 is no longer available.');
    }
  }

  /** Browser sessions were signed out or revoked. */
  onSessionsRevoked(userId: string, revoked: (sessionId: string) => boolean): void {
    const tokenSession = this.tokenSessions.get(userId);
    const linkSession = this.relay.senderSessionOf(userId);
    if ((tokenSession && revoked(tokenSession)) || (linkSession && revoked(linkSession))) this.removeUserEverywhere(userId);
  }

  private async removeFromSession(session: SharePlaySession, userId: string, grantReason: string): Promise<void> {
    let changed = session.requests.delete(userId);
    if (session.grant?.guestId === userId) {
      this.clearGrant(session, grantReason);
      changed = true;
    }
    if (session.viewers.delete(userId)) changed = true;
    if (this.client) {
      try {
        await this.client.removeParticipant(this.roomName(session.partyId), userId);
      } catch (error) {
        if (!isNotFound(error)) this.ctx.log.warn({ err: error, partyId: session.partyId }, 'Could not remove a SharePlay participant; reconciliation will retry');
      }
    }
    if (changed) await this.changed(session.partyId);
  }

  // ---- Media server events --------------------------------------------------------------------

  /** May this identity be in the party's SharePlay room right now? */
  private async allowed(session: SharePlaySession, userId: string): Promise<boolean> {
    if (!UUID.test(userId)) return false;
    if ((await this.ctx.parties.currentPartyId(userId)) !== session.partyId) return false;
    const sessionId = this.tokenSessions.get(userId);
    if (sessionId && !(await this.ctx.auth.isSessionValid(sessionId))) return false;
    if ((await this.ctx.users.loadCards([userId])).get(userId)?.status !== 'active') return false;
    if (userId === session.hostId) return true;
    if ((await this.ctx.relations.blockedSet(userId, [session.hostId])).size > 0) return false;
    return session.viewers.has(userId) || session.viewers.size < this.ctx.config.shareplay.maxViewers;
  }

  /**
   * Webhook events for SharePlay rooms (signature already verified by the voice module, which
   * owns LiveKit's single webhook URL).
   */
  async handleWebhookEvent(event: WebhookEvent): Promise<void> {
    const partyId = this.partyIdOf(event.room?.name);
    if (!partyId || !this.client) return;
    const session = this.sessions.get(partyId);
    const identity = event.participant?.identity ?? '';
    if (!session) {
      // A room without a session (ended, or the server restarted): nobody belongs in it.
      if (event.event === 'participant_joined') await this.closeOrphanRoom(partyId);
      return;
    }
    switch (event.event) {
      case 'participant_joined': {
        if (!(await this.allowed(session, identity))) {
          this.ctx.log.warn({ partyId }, 'Removed a SharePlay participant who is not allowed in the room');
          await this.client.removeParticipant(this.roomName(partyId), identity).catch(() => {});
          return;
        }
        if (identity === session.hostId) {
          session.hostConnected = true;
          if (session.state === 'host-reconnecting') {
            session.state = session.liveAt ? 'live' : 'starting';
            this.armTimer(session, this.timings.startTimeoutMs, 'The stream did not resume in time.');
          }
        } else {
          session.viewers.set(identity, { joinedAt: Date.now() });
        }
        await this.changed(partyId);
        return;
      }
      case 'participant_left':
      case 'participant_connection_aborted': {
        if (identity === session.hostId) {
          session.hostConnected = false;
          session.state = 'host-reconnecting';
          // Player 2 depends on the host's stream: it ends at once, the stream gets a short grace.
          this.clearGrant(session, 'The host disconnected.');
          this.armTimer(session, this.timings.hostGraceMs, 'The host did not come back.');
        } else if (session.viewers.delete(identity) && session.grant?.guestId === identity) {
          this.clearGrant(session, 'Player 2 stopped watching the stream.');
        }
        await this.changed(partyId);
        return;
      }
      case 'track_published': {
        if (identity !== session.hostId) return;
        if (event.track?.source !== undefined && Number(event.track.source) === TrackSource.SCREEN_SHARE) {
          session.state = 'live';
          session.liveAt ??= Date.now();
          this.disarmTimer(session);
          await this.changed(partyId);
        }
        return;
      }
      case 'track_unpublished': {
        if (identity !== session.hostId) return;
        if (event.track?.source !== undefined && Number(event.track.source) === TrackSource.SCREEN_SHARE && session.state === 'live') {
          // The capture stopped without "Stop SharePlay" (for example the browser's own stop button).
          session.state = 'starting';
          this.clearGrant(session, 'The stream stopped.');
          this.armTimer(session, this.timings.startTimeoutMs, 'The stream did not resume in time.');
          await this.changed(partyId);
        }
        return;
      }
      case 'room_finished': {
        await this.end(partyId, 'The stream ended.');
        return;
      }
      default:
        return;
    }
  }

  private async closeOrphanRoom(partyId: string): Promise<void> {
    if (!this.client) return;
    await this.client.deleteRoom(this.roomName(partyId)).catch((error: unknown) => {
      if (!isNotFound(error)) this.ctx.log.warn({ err: error, partyId }, 'Could not delete an orphaned SharePlay room');
    });
  }

  /**
   * Rebuilds viewers and the host's connection from LiveKit, removes anyone not allowed and
   * deletes SharePlay rooms without a session. Covers missed webhooks.
   */
  async reconcile(): Promise<void> {
    if (!this.client) return;
    const rooms = await this.client.listRooms();
    for (const room of rooms) {
      const partyId = this.partyIdOf(room.name);
      if (!partyId) continue;
      const session = this.sessions.get(partyId);
      if (!session) {
        await this.closeOrphanRoom(partyId);
        continue;
      }
      const participants = await this.client.listParticipants(room.name);
      const viewers = new Map<string, { joinedAt: number }>();
      let hostConnected = false;
      for (const participant of participants) {
        if (!(await this.allowed(session, participant.identity))) {
          await this.removeFromSession(session, participant.identity, 'Player 2 access ended.');
          continue;
        }
        if (participant.identity === session.hostId) hostConnected = true;
        else viewers.set(participant.identity, session.viewers.get(participant.identity) ?? { joinedAt: Date.now() });
      }
      const changed = hostConnected !== session.hostConnected || viewers.size !== session.viewers.size || [...viewers.keys()].some((id) => !session.viewers.has(id));
      session.viewers = viewers;
      if (session.grant && !viewers.has(session.grant.guestId) && session.grant.guestId !== session.hostId) {
        // A guest who is not watching keeps the grant only while the controller link is open.
        if (!session.grant.inputConnected) this.clearGrant(session, 'Player 2 is no longer connected.');
      }
      if (session.hostConnected && !hostConnected) {
        session.hostConnected = false;
        session.state = 'host-reconnecting';
        this.clearGrant(session, 'The host disconnected.');
        this.armTimer(session, this.timings.hostGraceMs, 'The host did not come back.');
      }
      if (changed) await this.changed(partyId);
    }
  }

  /** Stops every timer (server shutdown). Sessions are not persisted. */
  shutdown(): void {
    for (const session of this.sessions.values()) {
      this.disarmTimer(session);
      if (session.grant?.expiryTimer) clearTimeout(session.grant.expiryTimer);
      if (session.grant?.reconnectTimer) clearTimeout(session.grant.reconnectTimer);
    }
    this.relay.closeAll();
  }

  /** Tells the party's members to refetch (never throws: a notification must not undo an action). */
  private async changed(partyId: string, ended = false): Promise<void> {
    try {
      const memberIds = await this.ctx.parties.memberIds(partyId);
      if (memberIds.length > 0) this.ctx.hub.sendToUsers(memberIds, { type: 'shareplay.changed', data: { partyId, ended } });
    } catch (error) {
      this.ctx.log.warn({ err: error, partyId }, 'Could not announce a SharePlay change');
    }
  }
}
