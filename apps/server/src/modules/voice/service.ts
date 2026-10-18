import { eq } from 'drizzle-orm';
import { AccessToken, RoomServiceClient, TrackSource, WebhookReceiver } from 'livekit-server-sdk';
import type { PartyMemberVoiceState, PartyVoiceStatus, VoiceStatusDto, VoiceTokenResponse } from '@akeno/shared';
import type { AppContext } from '../../app/context.js';
import { parties } from '../../database/schema/index.js';
import { AppError, Errors } from '../../utils/errors.js';
import { GRANT_ATTRIBUTE, VoiceGrantSealer, type VoiceCredential } from './grant.js';

const ROOM_PREFIX = 'akeno-party-';
const UUID = /^[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}$/;

interface VoiceParticipant {
  joinedAt: number;
  /** LiveKit's ID of this connection (a reconnect with a new token gets a new one). */
  sid: string | null;
  /** The session or device that authorized the token this connection joined with. */
  credential: VoiceCredential;
}

/** The parts of LiveKit's participant info this service reads. */
interface ReportedParticipant {
  identity?: string;
  sid?: string;
  attributes?: Record<string, string>;
}

/** Participant attribute that marks a console connected through the voice gateway. */
export const CONSOLE_CLIENT_ATTRIBUTE = 'akeno.client';
export const CONSOLE_CLIENT_VALUE = 'console-gateway';

/** LiveKit answers 404 / "not_found" for rooms or participants that do not exist (already gone). */
function isNotFound(error: unknown): boolean {
  const twirp = error as { status?: number; code?: string; message?: string };
  return twirp?.status === 404 || twirp?.code === 'not_found' || /not.?found|does not exist/i.test(String(twirp?.message ?? ''));
}

const NOT_CONFIGURED = 'Voice chat is not set up on this server. Use party chat to talk with your party.';
const GATEWAY_NOT_CONFIGURED = 'Console voice is not set up on this server. Voice works on the website and the apps.';

/**
 * Voice parties. Audio flows through a self-hosted LiveKit SFU; this service only decides who may
 * be in which room:
 *
 * - A room exists per AKENO party (`akeno-party-<party id>`); LiveKit runs with auto_create off.
 * - Join tokens are short-lived (VOICE_TOKEN_TTL_SECONDS), bound to one room and one identity
 *   (the AKENO user ID), and may only publish microphone audio.
 * - Membership is checked when a token is issued AND when LiveKit reports the join (signed
 *   webhook), and again every reconciliation pass; anyone who is not (or no longer) a member, or
 *   who has a block with someone already in the room, is removed from the room.
 * - Leaving or being removed from the party, the party closing, blocking and suspension remove
 *   the participant at once; so does signing out (or revoking) the browser session, or unpairing
 *   (or removing "parties:write" from) the device, that asked for the token.
 * - The authorizing session or device is sealed into the token (participant attribute
 *   GRANT_ATTRIBUTE, encrypted), so every check applies to the credential of the connection that
 *   is actually in the room, not to the user's latest token request, and survives a restart of
 *   this process. A participant without a valid grant, or whose session or device is no longer
 *   valid, is refused at join and removed by reconciliation (fail closed).
 *
 * Nothing is recorded: no egress is configured and no audio passes through this process.
 * The participant list is cached in memory (single backend instance, like presence) and rebuilt
 * from LiveKit by reconcile().
 */
export class VoiceService {
  private readonly rooms = new Map<string, Map<string, VoiceParticipant>>();
  private readonly client: RoomServiceClient | null;
  private readonly receiver: WebhookReceiver | null;
  private readonly grants: VoiceGrantSealer | null;

  constructor(private readonly ctx: AppContext) {
    const voice = ctx.config.voice;
    // Requests to the voice server are bounded so a slow or unreachable LiveKit never stalls a request.
    this.client = voice.enabled ? new RoomServiceClient(voice.apiUrl, voice.apiKey, voice.apiSecret, { requestTimeout: 5, failover: false }) : null;
    this.receiver = voice.enabled ? new WebhookReceiver(voice.apiKey, voice.apiSecret) : null;
    this.grants = voice.enabled ? new VoiceGrantSealer(voice.apiSecret) : null;
  }

  get enabled(): boolean {
    return this.client !== null;
  }

  roomName(partyId: string): string {
    return `${ROOM_PREFIX}${partyId}`;
  }

  partyIdOf(roomName: string | undefined): string | null {
    if (!roomName?.startsWith(ROOM_PREFIX)) return null;
    const id = roomName.slice(ROOM_PREFIX.length);
    return UUID.test(id) ? id : null;
  }

  status(): VoiceStatusDto {
    return this.enabled ? { enabled: true, reason: null } : { enabled: false, reason: NOT_CONFIGURED };
  }

  /** Voice availability as shown to a viewer of a party. */
  statusFor(isMember: boolean): PartyVoiceStatus {
    if (!this.enabled) return { available: false, reason: NOT_CONFIGURED };
    if (!isMember) return { available: false, reason: 'Join the party to use its voice chat.' };
    return { available: true, reason: null };
  }

  /** Whether a member is connected to the party's voice room (as LiveKit last reported). */
  memberState(partyId: string, userId: string): PartyMemberVoiceState {
    const connected = this.rooms.get(partyId)?.has(userId) ?? false;
    // Mute state is only known inside the room (clients see it live); the server does not guess.
    return { connected, muted: null };
  }

  participantsOf(partyId: string): string[] {
    return [...(this.rooms.get(partyId)?.keys() ?? [])];
  }

  private requireClient(): RoomServiceClient {
    if (!this.client) throw new AppError(503, 'VOICE_UNAVAILABLE', NOT_CONFIGURED);
    return this.client;
  }

  /** Issues a join token for a browser session after checking membership and blocks. */
  async issueToken(userId: string, partyId: string, sessionId: string): Promise<VoiceTokenResponse> {
    return this.issue(userId, partyId, { kind: 'session', id: sessionId }, this.ctx.config.voice.publicUrl, null);
  }

  /**
   * Issues a join token for a paired console, which connects through the AKENO voice gateway
   * (apps/voice-gateway) instead of WebRTC. Same rules as for browsers; the token is marked for
   * the gateway, and the voice connection ends when the device is unpaired or loses the
   * "parties:write" permission.
   */
  async issueDeviceToken(userId: string, partyId: string, deviceId: string): Promise<VoiceTokenResponse> {
    this.requireClient();
    const gatewayUrl = this.ctx.config.voice.gatewayUrl;
    if (!gatewayUrl) throw new AppError(503, 'VOICE_GATEWAY_UNAVAILABLE', GATEWAY_NOT_CONFIGURED);
    return this.issue(userId, partyId, { kind: 'device', id: deviceId }, gatewayUrl, { [CONSOLE_CLIENT_ATTRIBUTE]: CONSOLE_CLIENT_VALUE });
  }

  private async issue(
    userId: string,
    partyId: string,
    credential: VoiceCredential,
    url: string,
    attributes: Record<string, string> | null,
  ): Promise<VoiceTokenResponse> {
    const client = this.requireClient();
    const grants = this.grants;
    if (!grants) throw new AppError(503, 'VOICE_UNAVAILABLE', NOT_CONFIGURED);
    if ((await this.ctx.parties.currentPartyId(userId)) !== partyId) {
      throw new AppError(403, 'NOT_MEMBER', 'Only members of this party can join its voice chat.');
    }
    const [party] = await this.ctx.db.select({ maxMembers: parties.maxMembers }).from(parties).where(eq(parties.id, partyId));
    if (!party) throw Errors.notFound('The party');
    const others = this.participantsOf(partyId).filter((id) => id !== userId);
    if ((await this.ctx.relations.blockedSet(userId, others)).size > 0) {
      throw new AppError(403, 'BLOCKED', 'You cannot join this voice chat while someone you blocked, or who blocked you, is in it.');
    }

    const room = this.roomName(partyId);
    try {
      // Idempotent: returns the existing room. Capacity follows the party's member limit.
      await client.createRoom({ name: room, emptyTimeout: 60, departureTimeout: 20, maxParticipants: party.maxMembers });
    } catch (error) {
      this.ctx.log.error({ err: error }, 'Voice server unreachable while creating a room');
      throw new AppError(503, 'VOICE_UNAVAILABLE', 'The voice server is not reachable right now. Party chat still works.');
    }

    const grant = grants.seal(credential, userId, room);
    const user = await this.ctx.users.getSummary(userId);
    const ttl = this.ctx.config.voice.tokenTtlSeconds;
    const token = new AccessToken(this.ctx.config.voice.apiKey, this.ctx.config.voice.apiSecret, {
      identity: userId,
      name: user.displayName,
      ttl,
      metadata: JSON.stringify({ onlineId: user.onlineId, avatar: user.avatar, avatarImage: user.avatarImage }),
      attributes: { ...attributes, [GRANT_ATTRIBUTE]: grant },
    });
    token.addGrant({
      room,
      roomJoin: true,
      canPublish: true,
      canPublishSources: [TrackSource.MICROPHONE],
      canSubscribe: true,
      canPublishData: false,
      canUpdateOwnMetadata: false,
    });
    return {
      url,
      token: await token.toJwt(),
      room,
      identity: userId,
      expiresAt: new Date(Date.now() + ttl * 1000).toISOString(),
    };
  }

  /**
   * Fire-and-forget variants for request paths (leaving a party, blocking, suspension): the
   * cache is updated at once and the voice server is told in the background, so a voice outage
   * never delays or fails the core action. Reconciliation retries anything that failed.
   */
  revoke(partyId: string, userId: string): void {
    void this.removeParticipant(partyId, userId).catch((error: unknown) => this.ctx.log.warn({ err: error }, 'Voice revoke failed'));
  }

  closeRoomLater(partyId: string): void {
    void this.closeRoom(partyId).catch((error: unknown) => this.ctx.log.warn({ err: error }, 'Voice room close failed'));
  }

  /** Removes a user from a party's voice room (best effort; unknown participants are fine). */
  async removeParticipant(partyId: string, userId: string): Promise<void> {
    const room = this.rooms.get(partyId);
    const known = room?.delete(userId) ?? false;
    if (!this.client) return;
    try {
      await this.client.removeParticipant(this.roomName(partyId), userId);
    } catch (error) {
      // "not found" means the user was not connected (or already left).
      if (!isNotFound(error)) {
        this.ctx.log.warn({ err: error, partyId }, 'Could not remove a voice participant; reconciliation will retry');
      }
    }
    if (known) await this.changed(partyId);
  }

  /** Ends a party's voice room for everyone (party closed or empty). */
  async closeRoom(partyId: string): Promise<void> {
    this.rooms.delete(partyId);
    if (!this.client) return;
    try {
      await this.client.deleteRoom(this.roomName(partyId));
    } catch (error) {
      if (!isNotFound(error)) {
        this.ctx.log.warn({ err: error, partyId }, 'Could not delete a voice room; reconciliation will retry');
      }
    }
  }

  /** A block between two users who share a voice room: the blocked user is disconnected. */
  onBlock(blockerId: string, blockedId: string): void {
    for (const [partyId, participants] of this.rooms) {
      if (participants.has(blockerId) && participants.has(blockedId)) this.revoke(partyId, blockedId);
    }
  }

  /** Suspension: out of every voice room. */
  removeUserEverywhere(userId: string): void {
    for (const [partyId, participants] of this.rooms) {
      if (participants.has(userId)) this.revoke(partyId, userId);
    }
  }

  /**
   * Browser sessions were signed out or revoked: a voice connection authorized by one of them
   * ends now (another tab or device of the same user is not affected).
   */
  onSessionsRevoked(userId: string, revoked: (sessionId: string) => boolean): void {
    this.revokeCredentials(userId, (credential) => credential.kind === 'session' && revoked(credential.id));
  }

  /**
   * Paired devices were unpaired, or lost the "parties:write" permission: a voice connection
   * authorized by one of them ends now.
   */
  onDevicesRevoked(userId: string, revoked: (deviceId: string) => boolean): void {
    this.revokeCredentials(userId, (credential) => credential.kind === 'device' && revoked(credential.id));
  }

  /**
   * Removes the user's connections whose grant matches. The cache answers at once; LiveKit is
   * then asked for the connection actually in the room, which covers joins this process has not
   * seen yet (a restart, a missed webhook). Failures are left to reconciliation, which re-checks
   * every grant against the database.
   */
  private revokeCredentials(userId: string, matches: (credential: VoiceCredential) => boolean): void {
    for (const [partyId, participants] of this.rooms) {
      const participant = participants.get(userId);
      if (participant && matches(participant.credential)) this.revoke(partyId, userId);
    }
    void this.revokeConnected(userId, matches).catch((error: unknown) => this.ctx.log.warn({ err: error }, 'Voice revoke failed'));
  }

  private async revokeConnected(userId: string, matches: (credential: VoiceCredential) => boolean): Promise<void> {
    const client = this.client;
    if (!client) return;
    const partyId = await this.ctx.parties.currentPartyId(userId);
    if (!partyId) return;
    let participant: ReportedParticipant;
    try {
      participant = await client.getParticipant(this.roomName(partyId), userId);
    } catch (error) {
      if (isNotFound(error)) return;
      throw error;
    }
    const credential = this.credentialOf(partyId, participant);
    if (!credential || matches(credential)) await this.removeParticipant(partyId, userId);
  }

  /** The credential sealed into the token a reported participant joined with (null: none or invalid). */
  private credentialOf(partyId: string, participant: ReportedParticipant): VoiceCredential | null {
    const identity = participant.identity ?? '';
    return this.grants?.open(participant.attributes?.[GRANT_ATTRIBUTE], identity, this.roomName(partyId)) ?? null;
  }

  /**
   * May this connection be in this party's voice room right now (given who else is in it)?
   * Returns the credential it joined with, or null to remove it.
   */
  private async allowed(partyId: string, participant: ReportedParticipant, others: string[]): Promise<VoiceCredential | null> {
    const userId = participant.identity ?? '';
    if (!UUID.test(userId)) return null;
    if ((await this.ctx.parties.currentPartyId(userId)) !== partyId) return null;
    const credential = this.credentialOf(partyId, participant);
    if (!credential) return null;
    if (credential.kind === 'session' && !(await this.ctx.auth.isSessionValid(credential.id))) return null;
    if (credential.kind === 'device' && !(await this.ctx.devices.hasScope(credential.id, userId, 'parties:write'))) return null;
    const status = (await this.ctx.users.loadCards([userId])).get(userId)?.status;
    if (status !== 'active') return null;
    const rest = others.filter((id) => id !== userId);
    return (await this.ctx.relations.blockedSet(userId, rest)).size === 0 ? credential : null;
  }

  /**
   * Handles a signed LiveKit webhook. The signature (a JWT signed with the API secret that
   * includes the body's SHA-256) is verified before anything is read.
   */
  async handleWebhook(body: string, authorization: string | undefined): Promise<void> {
    if (!this.receiver) throw new AppError(404, 'NOT_FOUND', 'Voice is not enabled.');
    let event: Awaited<ReturnType<WebhookReceiver['receive']>>;
    try {
      event = await this.receiver.receive(body, authorization);
    } catch {
      throw new AppError(401, 'UNAUTHENTICATED', 'Invalid webhook signature.');
    }
    const partyId = this.partyIdOf(event.room?.name);
    if (!partyId) return;
    const reported: ReportedParticipant = event.participant ?? {};
    const identity = reported.identity ?? '';
    switch (event.event) {
      case 'participant_joined': {
        const room = this.rooms.get(partyId) ?? new Map<string, VoiceParticipant>();
        const credential = await this.allowed(partyId, reported, [...room.keys()]);
        if (!credential) {
          this.ctx.log.warn({ partyId }, 'Removed a voice participant who is not allowed in the room');
          await this.removeParticipant(partyId, identity);
          return;
        }
        room.set(identity, { joinedAt: Date.now(), sid: reported.sid || null, credential });
        this.rooms.set(partyId, room);
        await this.changed(partyId);
        return;
      }
      case 'participant_left':
      case 'participant_connection_aborted': {
        const room = this.rooms.get(partyId);
        const known = room?.get(identity);
        // A late "left" for a connection that was replaced (same identity, new token) keeps the new one.
        if (!known || (known.sid && reported.sid && known.sid !== reported.sid)) return;
        room!.delete(identity);
        await this.changed(partyId);
        return;
      }
      case 'room_finished': {
        if (this.rooms.delete(partyId)) await this.changed(partyId);
        return;
      }
      default:
        return;
    }
  }

  /**
   * Rebuilds the participant cache from LiveKit, removes anyone who is not allowed and deletes
   * rooms of parties that no longer exist. Covers missed webhooks and server restarts.
   */
  async reconcile(): Promise<void> {
    if (!this.client) return;
    const rooms = await this.client.listRooms();
    const seen = new Set<string>();
    for (const room of rooms) {
      const partyId = this.partyIdOf(room.name);
      if (!partyId) continue;
      seen.add(partyId);
      const [party] = await this.ctx.db.select({ id: parties.id }).from(parties).where(eq(parties.id, partyId));
      if (!party) {
        await this.closeRoom(partyId);
        continue;
      }
      const participants = await this.client.listParticipants(room.name);
      const next = new Map<string, VoiceParticipant>();
      const previous = this.rooms.get(partyId);
      for (const participant of participants) {
        const credential = await this.allowed(partyId, participant, [...next.keys()]);
        if (credential) {
          const known = previous?.get(participant.identity);
          next.set(participant.identity, { joinedAt: known?.joinedAt ?? Date.now(), sid: participant.sid || null, credential });
        } else {
          await this.removeParticipant(partyId, participant.identity);
        }
      }
      const changed = !previous || previous.size !== next.size || [...next.keys()].some((id) => !previous.has(id));
      this.rooms.set(partyId, next);
      if (changed) await this.changed(partyId);
    }
    for (const partyId of [...this.rooms.keys()]) {
      if (!seen.has(partyId)) {
        this.rooms.delete(partyId);
        await this.changed(partyId);
      }
    }
  }

  private async changed(partyId: string): Promise<void> {
    const memberIds = await this.ctx.parties.memberIds(partyId);
    if (memberIds.length > 0) this.ctx.parties.notifyChanged(memberIds, partyId);
  }
}
