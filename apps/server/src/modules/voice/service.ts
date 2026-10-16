import { eq } from 'drizzle-orm';
import { AccessToken, RoomServiceClient, TrackSource, WebhookReceiver } from 'livekit-server-sdk';
import type { PartyMemberVoiceState, PartyVoiceStatus, VoiceStatusDto, VoiceTokenResponse } from '@akeno/shared';
import type { AppContext } from '../../app/context.js';
import { parties } from '../../database/schema/index.js';
import { AppError, Errors } from '../../utils/errors.js';

const ROOM_PREFIX = 'akeno-party-';
const UUID = /^[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}$/;

interface VoiceParticipant {
  joinedAt: number;
}

/** LiveKit answers 404 / "not_found" for rooms or participants that do not exist (already gone). */
function isNotFound(error: unknown): boolean {
  const twirp = error as { status?: number; code?: string; message?: string };
  return twirp?.status === 404 || twirp?.code === 'not_found' || /not.?found|does not exist/i.test(String(twirp?.message ?? ''));
}

const NOT_CONFIGURED = 'Voice chat is not set up on this server. Use party chat to talk with your party.';

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
 *   the participant at once; so does signing out (or revoking) the browser session that asked
 *   for the token. A token from a session that is no longer valid is refused at join.
 *
 * Nothing is recorded: no egress is configured and no audio passes through this process.
 * The participant list is cached in memory (single backend instance, like presence) and rebuilt
 * from LiveKit by reconcile().
 */
export class VoiceService {
  private readonly rooms = new Map<string, Map<string, VoiceParticipant>>();
  /** Browser session that requested each user's latest voice token (identity = user ID). */
  private readonly tokenSessions = new Map<string, string>();
  private readonly client: RoomServiceClient | null;
  private readonly receiver: WebhookReceiver | null;

  constructor(private readonly ctx: AppContext) {
    const voice = ctx.config.voice;
    // Requests to the voice server are bounded so a slow or unreachable LiveKit never stalls a request.
    this.client = voice.enabled ? new RoomServiceClient(voice.apiUrl, voice.apiKey, voice.apiSecret, { requestTimeout: 5, failover: false }) : null;
    this.receiver = voice.enabled ? new WebhookReceiver(voice.apiKey, voice.apiSecret) : null;
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

  /** Issues a join token after checking membership and blocks. */
  async issueToken(userId: string, partyId: string, sessionId?: string): Promise<VoiceTokenResponse> {
    const client = this.requireClient();
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

    const user = await this.ctx.users.getSummary(userId);
    const ttl = this.ctx.config.voice.tokenTtlSeconds;
    const token = new AccessToken(this.ctx.config.voice.apiKey, this.ctx.config.voice.apiSecret, {
      identity: userId,
      name: user.displayName,
      ttl,
      metadata: JSON.stringify({ onlineId: user.onlineId, avatar: user.avatar, avatarImage: user.avatarImage }),
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
    if (sessionId) this.tokenSessions.set(userId, sessionId);
    return {
      url: this.ctx.config.voice.publicUrl,
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
   * Browser sessions were signed out or revoked: if the user's voice connection was authorized
   * by one of them, it ends now (another tab or device of the same user is not affected).
   */
  onSessionsRevoked(userId: string, revoked: (sessionId: string) => boolean): void {
    const sessionId = this.tokenSessions.get(userId);
    if (!sessionId || !revoked(sessionId)) return;
    // The entry stays (until the next token) so a join with that token is refused as well.
    this.removeUserEverywhere(userId);
  }

  /** May this user be in this party's voice room right now (given who else is in it)? */
  private async allowed(partyId: string, userId: string, others: string[]): Promise<boolean> {
    if (!UUID.test(userId)) return false;
    if ((await this.ctx.parties.currentPartyId(userId)) !== partyId) return false;
    // Known after a token was issued by this process (the map is not kept across restarts).
    const sessionId = this.tokenSessions.get(userId);
    if (sessionId && !(await this.ctx.auth.isSessionValid(sessionId))) return false;
    const status = (await this.ctx.users.loadCards([userId])).get(userId)?.status;
    if (status !== 'active') return false;
    const rest = others.filter((id) => id !== userId);
    return (await this.ctx.relations.blockedSet(userId, rest)).size === 0;
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
    // LiveKit has one webhook URL: events of SharePlay rooms are handed over (already verified).
    if (!partyId) return this.ctx.shareplay.handleWebhookEvent(event);
    const identity = event.participant?.identity ?? '';
    switch (event.event) {
      case 'participant_joined': {
        const room = this.rooms.get(partyId) ?? new Map<string, VoiceParticipant>();
        if (!(await this.allowed(partyId, identity, [...room.keys()]))) {
          this.ctx.log.warn({ partyId }, 'Removed a voice participant who is not allowed in the room');
          await this.removeParticipant(partyId, identity);
          return;
        }
        room.set(identity, { joinedAt: Date.now() });
        this.rooms.set(partyId, room);
        await this.changed(partyId);
        return;
      }
      case 'participant_left':
      case 'participant_connection_aborted': {
        if (this.rooms.get(partyId)?.delete(identity)) await this.changed(partyId);
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
        if (await this.allowed(partyId, participant.identity, [...next.keys()])) {
          next.set(participant.identity, previous?.get(participant.identity) ?? { joinedAt: Date.now() });
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
