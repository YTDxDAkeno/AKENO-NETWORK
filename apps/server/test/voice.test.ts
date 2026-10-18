import { createHash } from 'node:crypto';
import { createServer, type IncomingMessage, type Server, type ServerResponse } from 'node:http';
import type { AddressInfo } from 'node:net';
import { eq } from 'drizzle-orm';
import { AccessToken, TokenVerifier } from 'livekit-server-sdk';
import { afterAll, beforeAll, describe, expect, it } from 'vitest';
import type { PartyDto, VoiceTokenResponse } from '@akeno/shared';
import { loadConfig } from '../src/config/env.js';
import { deviceCredentials, devicePairings, users } from '../src/database/schema/index.js';
import { VoiceGrantSealer } from '../src/modules/voice/grant.js';
import { VoiceService } from '../src/modules/voice/service.js';
import { createTestServer, login, makeFriends, registerUser, sleep, type TestClient, type TestServer } from './helpers.js';

const API_KEY = 'akeno-test-key';
const API_SECRET = 'akeno-test-secret-that-is-long-enough-0123456789';
const GATEWAY_URL = 'wss://akeno.test/voice-gateway';

/**
 * A stand-in for LiveKit's room service API (Twirp/JSON), recording what AKENO asks for. The real
 * SFU is exercised by the browser voice test (apps/web/tests/e2e/voice.spec.ts).
 */
type FakeParticipant = { sid: string; attributes: Record<string, string> };
type FakeRoom = { maxParticipants: number; participants: Map<string, FakeParticipant> };

let nextSid = 0;

/** A connection as LiveKit reports it: the attributes come from the token it joined with (none without one). */
function participantFrom(token?: string): FakeParticipant {
  const claims = token ? (JSON.parse(Buffer.from(token.split('.')[1]!, 'base64url').toString('utf8')) as { attributes?: Record<string, string> }) : {};
  return { sid: `PA_${++nextSid}`, attributes: claims.attributes ?? {} };
}

class FakeLiveKit {
  readonly rooms = new Map<string, FakeRoom>();
  readonly calls: Array<{ method: string; body: any; authorization: string | undefined }> = [];
  server!: Server;
  url!: string;

  async start(): Promise<void> {
    this.server = createServer((request, response) => void this.handle(request, response));
    await new Promise<void>((resolve) => this.server.listen(0, '127.0.0.1', resolve));
    this.url = `ws://127.0.0.1:${(this.server.address() as AddressInfo).port}`;
  }

  stop(): Promise<void> {
    return new Promise((resolve) => this.server.close(() => resolve()));
  }

  private async handle(request: IncomingMessage, response: ServerResponse): Promise<void> {
    let raw = '';
    for await (const chunk of request) raw += chunk;
    const body = raw ? JSON.parse(raw) : {};
    const method = (request.url ?? '').split('/').pop() ?? '';
    this.calls.push({ method, body, authorization: request.headers.authorization });
    const json = (status: number, data: unknown) => {
      response.writeHead(status, { 'content-type': 'application/json' });
      response.end(JSON.stringify(data));
    };
    switch (method) {
      case 'CreateRoom': {
        const room = this.rooms.get(body.name) ?? { maxParticipants: body.maxParticipants ?? 0, participants: new Map<string, FakeParticipant>() };
        this.rooms.set(body.name, room);
        return json(200, { name: body.name, sid: `RM_${body.name}`, maxParticipants: room.maxParticipants });
      }
      case 'ListRooms':
        return json(200, { rooms: [...this.rooms.keys()].map((name) => ({ name, sid: `RM_${name}` })) });
      case 'DeleteRoom':
        if (!this.rooms.delete(body.room)) return json(404, { code: 'not_found', msg: 'room not found' });
        return json(200, {});
      case 'ListParticipants':
        return json(200, { participants: [...(this.rooms.get(body.room)?.participants ?? [])].map(([identity, p]) => ({ identity, ...p })) });
      case 'GetParticipant': {
        const participant = this.rooms.get(body.room)?.participants.get(body.identity);
        if (!participant) return json(404, { code: 'not_found', msg: 'participant not found' });
        return json(200, { identity: body.identity, ...participant });
      }
      case 'RemoveParticipant': {
        const room = this.rooms.get(body.room);
        if (!room?.participants.delete(body.identity)) return json(404, { code: 'not_found', msg: 'participant not found' });
        return json(200, {});
      }
      default:
        return json(404, { code: 'bad_route', msg: 'unknown method' });
    }
  }

  removed(identity: string): boolean {
    return this.calls.some((call) => call.method === 'RemoveParticipant' && call.body.identity === identity);
  }
}

let server: TestServer;
let livekit: FakeLiveKit;
let leader: TestClient;
let friend: TestClient;
let outsider: TestClient;
/** The LiveKit URL the server was configured with (the outage test later moves the fake server). */
let configuredUrl: string;

/** Sends a webhook signed like LiveKit does (JWT with the body's SHA-256), optionally forged. */
async function webhook(event: Record<string, unknown>, options: { secret?: string; tamper?: boolean } = {}) {
  const body = JSON.stringify({ id: `EV_${Math.random()}`, createdAt: String(Math.floor(Date.now() / 1000)), ...event });
  const token = new AccessToken(API_KEY, options.secret ?? API_SECRET);
  token.sha256 = createHash('sha256').update(body).digest('base64');
  const jwt = await token.toJwt();
  return server.app.inject({
    method: 'POST',
    url: '/api/v1/voice/webhook',
    headers: { 'content-type': 'application/webhook+json', authorization: jwt },
    payload: options.tamper ? body.replace('participant_', 'participant-') : body,
  });
}

/**
 * Simulates a participant connecting to the SFU with a token (or none, like a forged join), which
 * then reports the join. Like LiveKit, a new connection with the same identity replaces the old one.
 */
async function connect(partyId: string, identity: string, token?: string) {
  const room = `akeno-party-${partyId}`;
  const entry = livekit.rooms.get(room) ?? { maxParticipants: 0, participants: new Map<string, FakeParticipant>() };
  const participant = participantFrom(token);
  entry.participants.set(identity, participant);
  livekit.rooms.set(room, entry);
  return webhook({ event: 'participant_joined', room: { name: room, sid: `RM_${room}` }, participant: { identity, ...participant } });
}

/** A browser session asks for a voice token and connects with it, as the website does. */
async function join(partyId: string, client: TestClient) {
  const response = await client.post<VoiceTokenResponse>(`/parties/${partyId}/voice/token`);
  expect(response.status).toBe(200);
  return connect(partyId, client.id, response.body.token);
}

/** A paired console asks for a voice pass and connects with it through the gateway. */
async function joinDevice(partyId: string, owner: TestClient, device: { token: string }) {
  const response = await deviceToken(device, partyId);
  expect(response.statusCode).toBe(200);
  return connect(partyId, owner.id, (JSON.parse(response.body) as VoiceTokenResponse).token);
}

async function startParty(): Promise<PartyDto> {
  const created = await leader.post<PartyDto>('/parties', { name: 'Gaming Night', privacy: 'friends', maxMembers: 4 });
  expect(created.status).toBe(201);
  expect((await friend.post(`/parties/${created.body.id}/join`)).status).toBe(200);
  return created.body;
}

beforeAll(async () => {
  livekit = new FakeLiveKit();
  await livekit.start();
  configuredUrl = livekit.url;
  server = await createTestServer({ LIVEKIT_URL: livekit.url, LIVEKIT_API_KEY: API_KEY, LIVEKIT_API_SECRET: API_SECRET, VOICE_GATEWAY_URL: GATEWAY_URL });
  leader = await registerUser(server, 'voice_leader');
  friend = await registerUser(server, 'voice_friend');
  outsider = await registerUser(server, 'voice_outsider');
  await makeFriends(leader, friend);
});

afterAll(async () => {
  await server.close();
  await livekit.stop();
});

describe('voice tokens', () => {
  it('issues a short-lived, room-bound, microphone-only token to party members', async () => {
    const party = await startParty();
    expect(party.voice).toEqual({ available: true, reason: null });
    const response = await friend.post<VoiceTokenResponse>(`/parties/${party.id}/voice/token`);
    expect(response.status).toBe(200);
    expect(response.body).toMatchObject({ url: livekit.url, room: `akeno-party-${party.id}`, identity: friend.id });
    const claims = await new TokenVerifier(API_KEY, API_SECRET).verify(response.body.token);
    expect(claims.sub).toBe(friend.id);
    expect(claims.video).toMatchObject({ room: `akeno-party-${party.id}`, roomJoin: true, canSubscribe: true, canPublishData: false });
    expect(claims.video?.canPublishSources).toEqual(['microphone']);
    expect(claims.video?.roomAdmin).toBeFalsy();
    expect(claims.video?.roomCreate).toBeFalsy();
    const lifetime = (claims.exp as number) - (claims.nbf as number);
    expect(lifetime).toBeLessThanOrEqual(120);
    // The room is created with the party's capacity.
    const create = livekit.calls.find((call) => call.method === 'CreateRoom');
    expect(create?.body).toMatchObject({ name: `akeno-party-${party.id}`, maxParticipants: 4 });
    // Server-side calls are signed with an admin grant (never exposed to clients).
    expect(create?.authorization).toMatch(/^Bearer /);
    await leader.post(`/parties/${party.id}/close`);
  });

  it('refuses non-members, unauthenticated requests, missing CSRF tokens and paired devices', async () => {
    const party = await startParty();
    const denied = await outsider.post(`/parties/${party.id}/voice/token`);
    expect(denied.status).toBe(403);
    expect(denied.body.error.code).toBe('NOT_MEMBER');
    const anonymous = await server.app.inject({ method: 'POST', url: `/api/v1/parties/${party.id}/voice/token`, headers: { origin: 'http://localhost:5173' } });
    expect(anonymous.statusCode).toBe(401);
    expect((await friend.post(`/parties/${party.id}/voice/token`, {}, { csrf: false })).status).toBe(403);

    // A paired device (console) of a member has no voice permission.
    const start = await server.app.inject({ method: 'POST', url: '/api/v1/devices/pairings', payload: { deviceName: 'PS5', platform: 'PlayStation 5 (homebrew)', clientVersion: '0.2.0' } });
    const started = JSON.parse(start.body) as { userCode: string; deviceCode: string };
    expect((await friend.post('/devices/pairings/approve', { userCode: started.userCode })).status).toBe(204);
    await server.database.db.update(devicePairings).set({ lastPolledAt: new Date(Date.now() - 60_000) });
    const redeemed = await server.app.inject({ method: 'POST', url: '/api/v1/devices/pairings/token', payload: { deviceCode: started.deviceCode } });
    const { accessToken } = JSON.parse(redeemed.body) as { accessToken: string };
    const device = await server.app.inject({ method: 'POST', url: `/api/v1/parties/${party.id}/voice/token`, headers: { authorization: `Bearer ${accessToken}` } });
    expect([401, 403]).toContain(device.statusCode);
    await leader.post(`/parties/${party.id}/close`);
  });
});

/** Pairs a console for a user (the device flow the daemon uses) and returns its bearer token and ID. */
async function pairDevice(owner: TestClient): Promise<{ token: string; id: string }> {
  const start = await server.app.inject({ method: 'POST', url: '/api/v1/devices/pairings', payload: { deviceName: 'PS5', platform: 'PlayStation 5 (homebrew)', clientVersion: '0.5.0-alpha' } });
  const started = JSON.parse(start.body) as { userCode: string; deviceCode: string };
  expect((await owner.post('/devices/pairings/approve', { userCode: started.userCode })).status).toBe(204);
  await server.database.db.update(devicePairings).set({ lastPolledAt: new Date(Date.now() - 60_000) });
  const redeemed = await server.app.inject({ method: 'POST', url: '/api/v1/devices/pairings/token', payload: { deviceCode: started.deviceCode } });
  const { accessToken } = JSON.parse(redeemed.body) as { accessToken: string };
  const current = await server.app.inject({ method: 'GET', url: '/api/v1/devices/current', headers: { authorization: `Bearer ${accessToken}` } });
  return { token: accessToken, id: (JSON.parse(current.body) as { device: { id: string } }).device.id };
}

function deviceToken(device: { token: string }, partyId: string) {
  return server.app.inject({ method: 'POST', url: `/api/v1/parties/${partyId}/voice/device-token`, headers: { authorization: `Bearer ${device.token}` } });
}

describe('console voice (paired devices through the voice gateway)', () => {
  it('issues a gateway voice pass to the paired console of a member, with the same limits as browsers', async () => {
    const party = await startParty();
    const ps5 = await pairDevice(friend);
    const response = await deviceToken(ps5, party.id);
    expect(response.statusCode).toBe(200);
    const body = JSON.parse(response.body) as VoiceTokenResponse;
    expect(body).toMatchObject({ url: GATEWAY_URL, room: `akeno-party-${party.id}`, identity: friend.id });
    const claims = await new TokenVerifier(API_KEY, API_SECRET).verify(body.token);
    expect(claims.sub).toBe(friend.id);
    expect(claims.video).toMatchObject({ room: `akeno-party-${party.id}`, roomJoin: true, canSubscribe: true, canPublishData: false });
    expect(claims.video?.canPublishSources).toEqual(['microphone']);
    expect(claims.video?.roomAdmin).toBeFalsy();
    expect(claims.attributes?.['akeno.client']).toBe('console-gateway');
    expect(Object.keys(claims.attributes ?? {}).sort()).toEqual(['akeno.client', 'akeno.grant']);
    expect((claims.exp as number) - (claims.nbf as number)).toBeLessThanOrEqual(120);
    // The device ID is not in the token (other participants can read its claims).
    expect(JSON.stringify(claims)).not.toContain(ps5.id);

    // Browsers use their own route; consoles cannot use the browser route.
    expect((await friend.post(`/parties/${party.id}/voice/device-token`)).status).toBe(400);
    const browserRoute = await server.app.inject({ method: 'POST', url: `/api/v1/parties/${party.id}/voice/token`, headers: { authorization: `Bearer ${ps5.token}` } });
    expect(browserRoute.statusCode).toBe(403);
    // Not a member: refused like a browser.
    const stranger = await pairDevice(outsider);
    const denied = await deviceToken(stranger, party.id);
    expect(denied.statusCode).toBe(403);
    expect(JSON.parse(denied.body).error.code).toBe('NOT_MEMBER');
    // Without "parties:write" the console has no voice.
    const devices = (await friend.get<{ devices: Array<{ id: string; scopes: string[] }> }>('/devices')).body.devices;
    const scopes = devices.find((d) => d.id === ps5.id)!.scopes.filter((scope) => scope !== 'parties:write');
    expect((await friend.patch(`/devices/${ps5.id}`, { scopes })).status).toBe(200);
    const noScope = await deviceToken(ps5, party.id);
    expect(noScope.statusCode).toBe(403);
    expect(JSON.parse(noScope.body).error.code).toBe('DEVICE_SCOPE_DENIED');
    await leader.post(`/parties/${party.id}/close`);
  });

  it('ends console voice when the console is unpaired or loses "parties:write"; refuses joins from such a pass', async () => {
    const party = await startParty();
    const ps5 = await pairDevice(friend);
    const pass = JSON.parse((await deviceToken(ps5, party.id)).body) as VoiceTokenResponse;
    await connect(party.id, friend.id, pass.token);
    livekit.calls.length = 0;
    expect((await friend.delete(`/devices/${ps5.id}`)).status).toBe(204);
    await waitFor(() => livekit.removed(friend.id));
    // A join reported with the pass of the unpaired console is refused.
    livekit.calls.length = 0;
    await connect(party.id, friend.id, pass.token);
    await waitFor(() => livekit.removed(friend.id));

    const second = await pairDevice(friend);
    livekit.calls.length = 0;
    await joinDevice(party.id, friend, second);
    await sleep(100);
    expect(livekit.removed(friend.id)).toBe(false); // a valid console stays
    const devices = (await friend.get<{ devices: Array<{ id: string; scopes: string[] }> }>('/devices')).body.devices;
    const scopes = devices.find((d) => d.id === second.id)!.scopes.filter((scope) => scope !== 'parties:write');
    expect((await friend.patch(`/devices/${second.id}`, { scopes })).status).toBe(200);
    await waitFor(() => livekit.removed(friend.id));
    // Leaving the party removes the console's voice like a browser's.
    const third = await pairDevice(friend);
    await joinDevice(party.id, friend, third);
    livekit.calls.length = 0;
    expect([200, 204]).toContain((await friend.post(`/parties/${party.id}/leave`)).status);
    await waitFor(() => livekit.removed(friend.id));
    await leader.post(`/parties/${party.id}/close`);
  });

  it('answers 503 when console voice is not set up, and validates the gateway URL', async () => {
    const plain = await createTestServer({ LIVEKIT_URL: livekit.url, LIVEKIT_API_KEY: API_KEY, LIVEKIT_API_SECRET: API_SECRET });
    const previous = server;
    try {
      server = plain;
      const a = await registerUser(plain, 'nogateway_a');
      const party = (await a.post<PartyDto>('/parties', {})).body;
      const ps5 = await pairDevice(a);
      const response = await deviceToken(ps5, party.id);
      expect(response.statusCode).toBe(503);
      expect(JSON.parse(response.body).error.code).toBe('VOICE_GATEWAY_UNAVAILABLE');
    } finally {
      server = previous;
      await plain.close();
    }
    const base = { NODE_ENV: 'production', DATABASE_URL: 'postgres://a:b@127.0.0.1:5432/c', PUBLIC_ORIGIN: 'https://akeno.example.com' };
    const voice = { LIVEKIT_URL: 'wss://akeno.example.com/livekit', LIVEKIT_API_KEY: 'akeno1', LIVEKIT_API_SECRET: 'a'.repeat(40) };
    expect(loadConfig({ ...base, ...voice, VOICE_GATEWAY_URL: 'wss://akeno.example.com/voice-gateway' }).voice.gatewayUrl).toBe('wss://akeno.example.com/voice-gateway');
    expect(() => loadConfig({ ...base, ...voice, VOICE_GATEWAY_URL: 'https://akeno.example.com/voice-gateway' })).toThrow(/wss:\/\//);
    expect(() => loadConfig({ ...base, ...voice, VOICE_GATEWAY_URL: 'wss://akeno.example.com/x?y=1' })).toThrow(/wss:\/\//);
    expect(() => loadConfig({ ...base, VOICE_GATEWAY_URL: 'wss://akeno.example.com/voice-gateway' })).toThrow(/needs voice/);
  });
});

describe('voice webhooks', () => {
  it('accepts only correctly signed webhooks', async () => {
    const party = await startParty();
    const forged = await connectWith({ secret: 'some-other-secret-that-is-long-enough-000000' }, party.id, friend.id);
    expect(forged.statusCode).toBe(401);
    const tampered = await connectWith({ tamper: true }, party.id, friend.id);
    expect(tampered.statusCode).toBe(401);
    const unsigned = await server.app.inject({ method: 'POST', url: '/api/v1/voice/webhook', headers: { 'content-type': 'application/webhook+json' }, payload: '{}' });
    expect(unsigned.statusCode).toBe(401);
    const view = (await leader.get<PartyDto>(`/parties/${party.id}`)).body;
    expect(view.members.find((member) => member.user.id === friend.id)?.voice.connected).toBe(false);
    await leader.post(`/parties/${party.id}/close`);
  });

  it('shows who is connected to party members only', async () => {
    const party = await startParty();
    expect((await join(party.id, friend)).statusCode).toBe(204);
    const asLeader = (await leader.get<PartyDto>(`/parties/${party.id}`)).body;
    expect(asLeader.members.find((member) => member.user.id === friend.id)?.voice).toEqual({ connected: true, muted: null });
    expect(asLeader.members.find((member) => member.user.id === leader.id)?.voice.connected).toBe(false);
    // A friend of the leader who may see the (friends) party but is not a member sees no voice state.
    await makeFriends(leader, outsider);
    const asOutsider = (await outsider.get<PartyDto>(`/parties/${party.id}`)).body;
    expect(asOutsider.voice.available).toBe(false);
    expect(asOutsider.members.every((member) => !member.voice.connected)).toBe(true);
    await webhook({ event: 'participant_left', room: { name: `akeno-party-${party.id}` }, participant: { identity: friend.id } });
    expect((await leader.get<PartyDto>(`/parties/${party.id}`)).body.members.find((m) => m.user.id === friend.id)?.voice.connected).toBe(false);
    await leader.post(`/parties/${party.id}/close`);
  });

  it('removes anyone the SFU reports who is not a member (for example a reused token)', async () => {
    const party = await startParty();
    await connect(party.id, outsider.id);
    expect(livekit.removed(outsider.id)).toBe(true);
    expect((await leader.get<PartyDto>(`/parties/${party.id}`)).body.members.some((m) => m.user.id === outsider.id)).toBe(false);
    await leader.post(`/parties/${party.id}/close`);
  });
});

describe('revocation', () => {
  it('disconnects a kicked member and one who leaves; closing deletes the room', async () => {
    const party = await startParty();
    await join(party.id, friend);
    expect((await leader.delete(`/parties/${party.id}/members/${friend.id}`)).status).toBe(204);
    await waitFor(() => livekit.removed(friend.id));
    // A kicked member cannot get a new token.
    expect((await friend.post(`/parties/${party.id}/voice/token`)).status).toBe(403);

    expect((await friend.post(`/parties/${party.id}/join`)).status).toBe(200);
    livekit.calls.length = 0;
    await join(party.id, friend);
    expect((await friend.post(`/parties/${party.id}/leave`)).status).toBe(204);
    await waitFor(() => livekit.removed(friend.id));

    livekit.calls.length = 0;
    expect((await leader.post(`/parties/${party.id}/close`)).status).toBe(204);
    await waitFor(() => livekit.calls.some((call) => call.method === 'DeleteRoom' && call.body.room === `akeno-party-${party.id}`));
  });

  it('ends voice when the browser session that asked for the token is signed out', async () => {
    const party = await startParty();
    // A second browser session of the same user joins voice; the first one stays signed in.
    const phone = await login(server, 'voice_friend');
    await join(party.id, phone);

    livekit.calls.length = 0;
    expect((await friend.post('/auth/logout')).status).toBe(204);
    await sleep(100);
    expect(livekit.removed(friend.id)).toBe(false); // another session signing out changes nothing

    friend = await login(server, 'voice_friend');
    expect((await friend.post('/auth/sessions/revoke-others')).status).toBe(200);
    await waitFor(() => livekit.removed(friend.id));

    // A token issued to a session that is gone is refused when the join is reported.
    livekit.calls.length = 0;
    const tablet = await login(server, 'voice_friend');
    const tabletToken = (await tablet.post<VoiceTokenResponse>(`/parties/${party.id}/voice/token`)).body.token;
    const sessions = (await tablet.get<{ sessions: Array<{ id: string; current: boolean }> }>('/auth/sessions')).body.sessions;
    expect((await friend.delete(`/auth/sessions/${sessions.find((session) => session.current)!.id}`)).status).toBe(204);
    livekit.calls.length = 0;
    await connect(party.id, friend.id, tabletToken);
    await waitFor(() => livekit.removed(friend.id));
    // A fresh token from a valid session is accepted again.
    livekit.calls.length = 0;
    await join(party.id, friend);
    await sleep(100);
    expect(livekit.removed(friend.id)).toBe(false);
    await leader.post(`/parties/${party.id}/close`);
  });

  it('keeps users with a block out of the same voice room', async () => {
    const party = await startParty();
    await join(party.id, leader);
    await join(party.id, friend);
    livekit.calls.length = 0;
    expect((await leader.post('/blocks', { userId: friend.id })).status).toBeLessThan(300);
    await waitFor(() => livekit.removed(friend.id));
    // Blocking ended the friendship; the friend is still a party member here only if the party
    // allows it, but voice is refused while the blocker is connected.
    if ((await friend.get<{ party: PartyDto | null }>('/parties/current')).body.party?.id === party.id) {
      const refused = await friend.post(`/parties/${party.id}/voice/token`);
      expect(refused.status).toBe(403);
      expect(refused.body.error.code).toBe('BLOCKED');
    }
    await leader.delete(`/blocks/${friend.id}`);
    await makeFriends(leader, friend);
    await leader.post(`/parties/${party.id}/close`);
  });

  it('disconnects a suspended account', async () => {
    const party = await startParty();
    await join(party.id, friend);
    const admin = await registerUser(server, 'voice_admin');
    await server.database.db.update(users).set({ role: 'admin' }).where(eq(users.id, admin.id));
    const adminSession = await login(server, 'voice_admin');
    livekit.calls.length = 0;
    expect((await adminSession.patch(`/admin/users/${friend.id}`, { status: 'suspended' })).status).toBe(200);
    await waitFor(() => livekit.removed(friend.id));
    await adminSession.patch(`/admin/users/${friend.id}`, { status: 'active' });
    friend = await login(server, 'voice_friend');
    await leader.post(`/parties/${party.id}/close`);
  });

  it('reconciliation removes intruders and deletes rooms of parties that no longer exist', async () => {
    const party = await startParty();
    const room = `akeno-party-${party.id}`;
    const token = (await friend.post<VoiceTokenResponse>(`/parties/${party.id}/voice/token`)).body.token; // as the client does
    livekit.rooms.set(room, { maxParticipants: 4, participants: new Map([[friend.id, participantFrom(token)], [outsider.id, participantFrom()]]) });
    livekit.rooms.set('akeno-party-00000000-0000-4000-8000-000000000000', { maxParticipants: 4, participants: new Map() });
    livekit.rooms.set('someone-elses-room', { maxParticipants: 2, participants: new Map([['x', participantFrom()]]) });
    livekit.calls.length = 0;
    await server.ctx.voice.reconcile();
    expect(livekit.removed(outsider.id)).toBe(true);
    expect(livekit.removed(friend.id)).toBe(false);
    expect(livekit.rooms.has('akeno-party-00000000-0000-4000-8000-000000000000')).toBe(false);
    expect(livekit.rooms.has('someone-elses-room')).toBe(true); // not ours: untouched
    expect(server.ctx.voice.participantsOf(party.id)).toEqual([friend.id]);
    await leader.post(`/parties/${party.id}/close`);
  });
});

/** Removes "parties:write" from a paired device, as its owner does in the device settings. */
async function dropPartiesScope(owner: TestClient, deviceId: string) {
  const devices = (await owner.get<{ devices: Array<{ id: string; scopes: string[] }> }>('/devices')).body.devices;
  const scopes = devices.find((d) => d.id === deviceId)!.scopes.filter((scope) => scope !== 'parties:write');
  expect((await owner.patch(`/devices/${deviceId}`, { scopes })).status).toBe(200);
}

/** Simulates a restart of the backend: a new voice service with nothing in memory (LiveKit and the database keep their state). */
function restartVoice(): VoiceService {
  const previous = server.ctx.voice;
  server.ctx.voice = new VoiceService(server.ctx);
  return previous;
}

describe('revocation follows the connection that is in the room', () => {
  it('revoking the connected console ends voice even after another credential asked for a newer token', async () => {
    const party = await startParty();
    const a = await pairDevice(friend);
    const b = await pairDevice(friend);
    await joinDevice(party.id, friend, a);
    // B asks for a voice pass but never connects; a browser session does the same.
    expect((await deviceToken(b, party.id)).statusCode).toBe(200);
    expect((await friend.post(`/parties/${party.id}/voice/token`)).status).toBe(200);

    // Revoking B (unused) leaves A connected.
    livekit.calls.length = 0;
    expect((await friend.delete(`/devices/${b.id}`)).status).toBe(204);
    await sleep(150);
    expect(livekit.removed(friend.id)).toBe(false);
    // Revoking A, the console actually in the room, removes it.
    await dropPartiesScope(friend, a.id);
    await waitFor(() => livekit.removed(friend.id));
    expect(server.ctx.voice.participantsOf(party.id)).toEqual([]);

    // The same with unpairing, and with a browser session as the unused newer token.
    const c = await pairDevice(friend);
    await joinDevice(party.id, friend, c);
    expect((await friend.post(`/parties/${party.id}/voice/token`)).status).toBe(200);
    livekit.calls.length = 0;
    expect((await friend.delete(`/devices/${c.id}`)).status).toBe(204);
    await waitFor(() => livekit.removed(friend.id));
    await leader.post(`/parties/${party.id}/close`);
  });

  it('a browser session that is signed out ends its own connection, not one a newer token request belongs to', async () => {
    const party = await startParty();
    const laptop = await login(server, 'voice_friend');
    await join(party.id, laptop);
    const ps5 = await pairDevice(friend);
    expect((await deviceToken(ps5, party.id)).statusCode).toBe(200); // newer, never used
    livekit.calls.length = 0;
    expect((await laptop.post('/auth/logout')).status).toBe(204);
    await waitFor(() => livekit.removed(friend.id));
    await leader.post(`/parties/${party.id}/close`);
  });

  it('after a backend restart, revoking the connected console or dropping its scope still removes it', async () => {
    const party = await startParty();
    const ps5 = await pairDevice(friend);
    await joinDevice(party.id, friend, ps5);
    const previous = restartVoice();
    try {
      // No webhook and no reconciliation yet: the new process has never seen this connection.
      expect(server.ctx.voice.participantsOf(party.id)).toEqual([]);
      livekit.calls.length = 0;
      await dropPartiesScope(friend, ps5.id);
      await waitFor(() => livekit.removed(friend.id));

      // Unpairing, after another restart, while the console is connected.
      const second = await pairDevice(friend);
      await joinDevice(party.id, friend, second);
      restartVoice();
      livekit.calls.length = 0;
      expect((await friend.delete(`/devices/${second.id}`)).status).toBe(204);
      await waitFor(() => livekit.removed(friend.id));
    } finally {
      server.ctx.voice = previous;
    }
    await leader.post(`/parties/${party.id}/close`);
  });

  it('after a restart, reconciliation re-checks the grant of every connection and fails closed', async () => {
    const party = await startParty();
    const room = `akeno-party-${party.id}`;
    const ps5 = await pairDevice(friend);
    await joinDevice(party.id, friend, ps5);
    await join(party.id, leader);
    const previous = restartVoice();
    try {
      // While the backend was down, the console lost "parties:write" (no revocation event reached voice).
      await server.database.db.update(deviceCredentials).set({ scopes: ['profile:read'] }).where(eq(deviceCredentials.id, ps5.id));
      // A connection without a grant (for example a token from before this release) is not trusted either.
      livekit.rooms.get(room)!.participants.set(outsider.id, participantFrom());
      livekit.calls.length = 0;
      await server.ctx.voice.reconcile();
      expect(livekit.removed(friend.id)).toBe(true);
      expect(livekit.removed(outsider.id)).toBe(true);
      expect(livekit.removed(leader.id)).toBe(false);
      expect(server.ctx.voice.participantsOf(party.id)).toEqual([leader.id]);
    } finally {
      server.ctx.voice = previous;
    }
    await leader.post(`/parties/${party.id}/close`);
  });

  it('refuses a join without a grant, or with a grant sealed for someone else', async () => {
    const party = await startParty();
    livekit.calls.length = 0;
    await connect(party.id, friend.id); // a member, but no grant
    expect(livekit.removed(friend.id)).toBe(true);
    // The leader's token, replayed under the friend's identity, does not open for the friend.
    const leaderToken = (await leader.post<VoiceTokenResponse>(`/parties/${party.id}/voice/token`)).body.token;
    livekit.calls.length = 0;
    await connect(party.id, friend.id, leaderToken);
    expect(livekit.removed(friend.id)).toBe(true);
    await leader.post(`/parties/${party.id}/close`);
  });

  it('keeps session and device IDs out of what other participants can see', async () => {
    const party = await startParty();
    const sessions = (await friend.get<{ sessions: Array<{ id: string; current: boolean }> }>('/auth/sessions')).body.sessions;
    const sessionId = sessions.find((session) => session.current)!.id;
    const browser = (await friend.post<VoiceTokenResponse>(`/parties/${party.id}/voice/token`)).body.token;
    const ps5 = await pairDevice(friend);
    const consolePass = (JSON.parse((await deviceToken(ps5, party.id)).body) as VoiceTokenResponse).token;
    for (const [token, secretId] of [[browser, sessionId], [consolePass, ps5.id]] as const) {
      const claims = await new TokenVerifier(API_KEY, API_SECRET).verify(token);
      // Name, metadata and attributes are what LiveKit shows to everyone in the room.
      const visible = JSON.stringify({ name: claims.name, metadata: claims.metadata, attributes: claims.attributes });
      expect(visible).not.toContain(secretId);
      expect(JSON.stringify(claims)).not.toContain(secretId);
      expect(claims.attributes?.['akeno.grant']).toMatch(/^v1\./);
    }
    // A fresh value per token: two passes for the same session do not look alike.
    const again = (await friend.post<VoiceTokenResponse>(`/parties/${party.id}/voice/token`)).body.token;
    const grantOf = async (token: string) => (await new TokenVerifier(API_KEY, API_SECRET).verify(token)).attributes?.['akeno.grant'];
    expect(await grantOf(again)).not.toBe(await grantOf(browser));
    await leader.post(`/parties/${party.id}/close`);
  });
});

describe('voice grants', () => {
  it('open only for the identity and room they were sealed for, and not when altered', () => {
    const sealer = new VoiceGrantSealer(API_SECRET);
    const sealed = sealer.seal({ kind: 'device', id: 'device-1' }, 'user-1', 'room-1');
    expect(sealer.open(sealed, 'user-1', 'room-1')).toEqual({ kind: 'device', id: 'device-1' });
    expect(sealer.open(sealed, 'user-2', 'room-1')).toBeNull();
    expect(sealer.open(sealed, 'user-1', 'room-2')).toBeNull();
    expect(new VoiceGrantSealer('some-other-secret-that-is-long-enough-000000').open(sealed, 'user-1', 'room-1')).toBeNull();
    const flipped = sealed.slice(0, -2) + (sealed.endsWith('A') ? 'B' : 'A') + sealed.slice(-1);
    expect(sealer.open(flipped, 'user-1', 'room-1')).toBeNull();
    expect(sealer.open(undefined, 'user-1', 'room-1')).toBeNull();
    expect(sealer.open('v1.', 'user-1', 'room-1')).toBeNull();
    expect(sealer.open('garbage', 'user-1', 'room-1')).toBeNull();
    expect(sealer.open(sealer.seal({ kind: 'session', id: 's' }, 'u', 'r'), 'u', 'r')).toEqual({ kind: 'session', id: 's' });
  });
});

describe('parties and blocks', () => {
  it('refuses joining a party where any member has a block with the joiner', async () => {
    const party = await startParty();
    const third = await registerUser(server, 'voice_third');
    await makeFriends(leader, third);
    expect((await friend.post('/blocks', { userId: third.id })).status).toBeLessThan(300);
    const refused = await third.post(`/parties/${party.id}/join`);
    expect(refused.status).toBe(403);
    expect(refused.body.error.code).toBe('BLOCKED');
    await friend.delete(`/blocks/${third.id}`);
    expect((await third.post(`/parties/${party.id}/join`)).status).toBe(200);
    await leader.post(`/parties/${party.id}/close`);
  });
});

describe('failure isolation', () => {
  it('a voice server outage only affects voice: token requests fail fast, parties keep working', async () => {
    const party = await startParty();
    await join(party.id, friend);
    await livekit.stop();
    try {
      const started = Date.now();
      const token = await friend.post(`/parties/${party.id}/voice/token`);
      expect(token.status).toBe(503);
      expect(token.body.error.code).toBe('VOICE_UNAVAILABLE');
      expect(Date.now() - started).toBeLessThan(7000);
      const leaveStarted = Date.now();
      expect((await friend.post(`/parties/${party.id}/leave`)).status).toBe(204);
      expect(Date.now() - leaveStarted).toBeLessThan(1000);
      const sent = await leader.post(`/parties/${party.id}/messages`, { body: 'chat still works', clientMessageId: crypto.randomUUID() });
      expect(sent.status).toBe(201);
    } finally {
      await livekit.start();
    }
  }, 20_000);

  it('reports voice as unavailable when it is not configured', async () => {
    const plain = await createTestServer();
    try {
      const a = await registerUser(plain, 'novoice_a');
      expect((await a.get('/voice/status')).body).toMatchObject({ enabled: false });
      const party = (await a.post<PartyDto>('/parties', {})).body;
      expect(party.voice.available).toBe(false);
      expect(party.voice.reason).toMatch(/not set up/);
      const token = await a.post(`/parties/${party.id}/voice/token`);
      expect(token.status).toBe(503);
      expect(token.body.error.code).toBe('VOICE_UNAVAILABLE');
    } finally {
      await plain.close();
    }
  });
});

describe('configuration', () => {
  const base = { NODE_ENV: 'production', DATABASE_URL: 'postgres://a:b@127.0.0.1:5432/c', PUBLIC_ORIGIN: 'https://akeno.example.com' };
  const voice = { LIVEKIT_URL: 'wss://akeno.example.com/livekit', LIVEKIT_API_KEY: 'akeno1', LIVEKIT_API_SECRET: 'a'.repeat(40) };

  it('enables voice only with URL, key and secret together, and validates them', () => {
    expect(loadConfig(base).voice.enabled).toBe(false);
    const config = loadConfig({ ...base, ...voice });
    expect(config.voice).toMatchObject({ enabled: true, apiUrl: 'https://akeno.example.com/livekit', tokenTtlSeconds: 120 });
    expect(loadConfig({ ...base, ...voice, LIVEKIT_API_URL: 'http://livekit:7880' }).voice.apiUrl).toBe('http://livekit:7880');
    expect(() => loadConfig({ ...base, LIVEKIT_URL: voice.LIVEKIT_URL })).toThrow(/together/);
    expect(() => loadConfig({ ...base, ...voice, LIVEKIT_API_SECRET: 'short' })).toThrow(/at least 32/);
    expect(() => loadConfig({ ...base, ...voice, LIVEKIT_API_KEY: 'key: injected' })).toThrow(/letters, digits/);
    expect(() => loadConfig({ ...base, ...voice, LIVEKIT_URL: 'https://akeno.example.com/livekit' })).toThrow(/wss:\/\//);
    expect(() => loadConfig({ ...base, ...voice, LIVEKIT_URL: 'ws://akeno.example.com/livekit' })).toThrow(/Use wss/);
    expect(loadConfig({ ...base, ...voice, LIVEKIT_URL: 'ws://localhost:7880' }).voice.enabled).toBe(true);
    expect(() => loadConfig({ ...base, ...voice, VOICE_TOKEN_TTL_SECONDS: '3600' })).toThrow();
  });

  it('allows browsers to reach the LiveKit server in the Content-Security-Policy', async () => {
    const response = await server.app.inject({ method: 'GET', url: '/health/live' });
    const csp = String(response.headers['content-security-policy']);
    const host = new URL(configuredUrl).host;
    expect(csp).toContain(`ws://${host}`);
    expect(csp).toContain(`http://${host}`);
    expect(csp).toMatch(/script-src 'self'(;|$)/);
  });
});

async function connectWith(options: { secret?: string; tamper?: boolean }, partyId: string, identity: string) {
  const room = `akeno-party-${partyId}`;
  return webhook({ event: 'participant_joined', room: { name: room }, participant: { identity } }, options);
}

async function waitFor(check: () => boolean, timeoutMs = 3000): Promise<void> {
  const deadline = Date.now() + timeoutMs;
  while (!check()) {
    if (Date.now() > deadline) throw new Error('Timed out waiting for the voice server call');
    await sleep(20);
  }
}
