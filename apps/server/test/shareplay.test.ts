import { TokenVerifier } from 'livekit-server-sdk';
import { afterAll, beforeAll, describe, expect, it } from 'vitest';
import WebSocket from 'ws';
import {
  BUTTON,
  INPUT_PACKET_BYTES,
  SHAREPLAY_INPUT_CLOSE,
  encodeInputPacket,
  type InputPacket,
  type PartyDto,
  type PartySharePlayDto,
  type SharePlayControllerGrantDto,
  type SharePlayInputRelay,
  type SharePlaySessionDto,
  type SharePlayStatusDto,
  type SharePlayTokenResponse,
} from '@akeno/shared';
import { loadConfig } from '../src/config/env.js';
import { devicePairings } from '../src/database/schema/index.js';
import { FakeLiveKit } from './fake-livekit.js';
import {
  RealtimeClient,
  TEST_ORIGIN,
  connectRealtime,
  createTestServer,
  listen,
  login,
  makeFriends,
  registerUser,
  sleep,
  type TestClient,
  type TestServer,
} from './helpers.js';

const API_KEY = 'akeno-test-key';
const API_SECRET = 'akeno-test-secret-that-is-long-enough-0123456789';

let server: TestServer;
let livekit: FakeLiveKit;
let host: TestClient;
let guest: TestClient;
let third: TestClient;
let outsider: TestClient;
let wsBase: string;

const roomOf = (party: { id: string }) => `akeno-shareplay-${party.id}`;

async function waitFor(check: () => boolean | Promise<boolean>, timeoutMs = 3000): Promise<void> {
  const deadline = Date.now() + timeoutMs;
  while (!(await check())) {
    if (Date.now() > deadline) throw new Error('Timed out waiting for condition');
    await sleep(15);
  }
}

async function startParty(): Promise<PartyDto> {
  const created = await host.post<PartyDto>('/parties', { name: 'Co-op Night', privacy: 'friends', maxMembers: 6 });
  expect(created.status).toBe(201);
  expect((await guest.post(`/parties/${created.body.id}/join`)).status).toBe(200);
  expect((await third.post(`/parties/${created.body.id}/join`)).status).toBe(200);
  return created.body;
}

async function view(client: TestClient, party: PartyDto): Promise<PartySharePlayDto> {
  const response = await client.get<PartySharePlayDto>(`/parties/${party.id}/shareplay`);
  expect(response.status).toBe(200);
  return response.body;
}

/** Host starts SharePlay, connects to the room and publishes its screen share. */
async function goLive(party: PartyDto): Promise<SharePlaySessionDto> {
  const started = await host.post<SharePlaySessionDto>(`/parties/${party.id}/shareplay/start`, { source: 'screen' });
  expect(started.status).toBe(201);
  expect((await host.post(`/parties/${party.id}/shareplay/token`)).status).toBe(200);
  await livekit.join(server.app, roomOf(party), host.id);
  await livekit.track(server.app, roomOf(party), host.id, true);
  const live = await view(host, party);
  expect(live.session?.state).toBe('live');
  return live.session!;
}

async function watch(client: TestClient, party: PartyDto): Promise<void> {
  expect((await client.post(`/parties/${party.id}/shareplay/token`)).status).toBe(200);
  expect((await livekit.join(server.app, roomOf(party), client.id)).statusCode).toBe(204);
}

async function grantPlayer2(party: PartyDto, who: TestClient = guest): Promise<SharePlayControllerGrantDto> {
  await watch(who, party);
  expect((await who.post(`/parties/${party.id}/shareplay/controller/request`)).status).toBe(204);
  const approved = await host.post<SharePlayControllerGrantDto>(`/parties/${party.id}/shareplay/controller/approve`, { userId: who.id });
  expect(approved.status).toBe(200);
  return approved.body;
}

/** A browser-like WebSocket to the controller link. */
class InputSocket {
  readonly texts: any[] = [];
  closeCode: number | null = null;
  closeReason = '';
  constructor(readonly socket: WebSocket) {
    socket.on('message', (data, isBinary) => {
      if (!isBinary) this.texts.push(JSON.parse(data.toString()));
    });
    socket.on('close', (code, reason) => {
      this.closeCode = code;
      this.closeReason = reason.toString();
    });
  }
  relays(): SharePlayInputRelay[] {
    return this.texts.filter((text) => typeof text.seq === 'number' && text.grantId);
  }
  send(packet: Uint8Array): void {
    this.socket.send(packet);
  }
  async closed(): Promise<number> {
    await waitFor(() => this.closeCode !== null);
    return this.closeCode!;
  }
  close(): void {
    this.socket.close();
  }
}

async function openInput(client: TestClient, query: Record<string, string | number>, origin: string | null = TEST_ORIGIN): Promise<InputSocket> {
  const qs = new URLSearchParams(Object.entries(query).map(([key, value]): [string, string] => [key, String(value)]));
  const headers: Record<string, string> = {};
  if (origin) headers.Origin = origin;
  if (client.cookie) headers.Cookie = client.cookie;
  const socket = new WebSocket(`${wsBase}/api/v1/shareplay/input?${qs}`, { headers });
  const input = new InputSocket(socket);
  await new Promise<void>((resolve, reject) => {
    socket.once('open', () => resolve());
    socket.once('unexpected-response', (_request, response) => reject(Object.assign(new Error(`HTTP ${response.statusCode}`), { status: response.statusCode })));
    socket.once('error', reject);
  });
  return input;
}

async function expectRejected(promise: Promise<unknown>, status: number): Promise<void> {
  await expect(promise).rejects.toMatchObject({ status });
}

function packet(epoch: number, seq: number, patch: Partial<InputPacket> = {}): Uint8Array {
  return encodeInputPacket({ kind: 'state', slot: 2, epoch, seq, senderTime: seq * 16, buttons: 0, axes: [0, 0, 0, 0], triggers: [0, 0], ...patch });
}

async function pairDevice(client: TestClient): Promise<string> {
  const start = await server.app.inject({ method: 'POST', url: '/api/v1/devices/pairings', payload: { deviceName: 'PS5', platform: 'PlayStation 5 (homebrew)', clientVersion: '0.2.0' } });
  const started = JSON.parse(start.body) as { userCode: string; deviceCode: string };
  expect((await client.post('/devices/pairings/approve', { userCode: started.userCode })).status).toBe(204);
  await server.database.db.update(devicePairings).set({ lastPolledAt: new Date(Date.now() - 60_000) });
  const redeemed = await server.app.inject({ method: 'POST', url: '/api/v1/devices/pairings/token', payload: { deviceCode: started.deviceCode } });
  return (JSON.parse(redeemed.body) as { accessToken: string }).accessToken;
}

async function connectDevice(token: string): Promise<RealtimeClient> {
  const socket = new WebSocket(`${wsBase}/ws`, { headers: { Authorization: `Bearer ${token}` } });
  const realtime = new RealtimeClient(socket);
  await new Promise<void>((resolve, reject) => {
    socket.once('open', () => resolve());
    socket.once('error', reject);
  });
  await realtime.waitFor('hello');
  return realtime;
}

beforeAll(async () => {
  livekit = new FakeLiveKit(API_KEY, API_SECRET);
  await livekit.start();
  server = await createTestServer({
    LIVEKIT_URL: livekit.url,
    LIVEKIT_API_KEY: API_KEY,
    LIVEKIT_API_SECRET: API_SECRET,
    SHAREPLAY_ENABLED: 'true',
    SHAREPLAY_REMOTE_INPUT_ENABLED: 'true',
    SHAREPLAY_MAX_VIEWERS: '2',
  });
  server.ctx.shareplay.timings = { startTimeoutMs: 60_000, hostGraceMs: 60_000, requestTtlMs: 60_000, inputReconnectGraceMs: 60_000 };
  host = await registerUser(server, 'sp_host', { displayName: 'YTDxDAkeno' });
  guest = await registerUser(server, 'sp_guest', { displayName: 'AkenoV2' });
  third = await registerUser(server, 'sp_third');
  outsider = await registerUser(server, 'sp_outsider');
  await makeFriends(host, guest);
  await makeFriends(host, third);
  wsBase = (await listen(server)).replace(/\/ws$/, '');
});

afterAll(async () => {
  await server.close();
  await livekit.stop();
});

describe('configuration and availability', () => {
  it('is off unless explicitly enabled, and needs the media server', async () => {
    const plain = await createTestServer();
    try {
      const user = await registerUser(plain, 'sp_plain');
      const status = (await user.get<SharePlayStatusDto>('/shareplay/status')).body;
      expect(status.enabled).toBe(false);
      expect(status.remoteInput.supported).toBe(false);
      expect(status.virtualController.supported).toBe(false);
      const party = (await user.post<PartyDto>('/parties', { privacy: 'friends', maxMembers: 4 })).body;
      const partyView = (await user.get<PartySharePlayDto>(`/parties/${party.id}/shareplay`)).body;
      expect(partyView).toMatchObject({ available: false, session: null, you: { canStart: false } });
      expect((await user.post(`/parties/${party.id}/shareplay/start`, { source: 'screen' })).status).toBe(503);
    } finally {
      await plain.close();
    }
    const base = { DATABASE_URL: 'postgres://x', PUBLIC_ORIGIN: 'http://localhost:5173' };
    expect(() => loadConfig({ ...base, SHAREPLAY_ENABLED: 'true' })).toThrow(/SharePlay needs the voice server/);
    expect(() => loadConfig({ ...base, LIVEKIT_URL: 'ws://x', LIVEKIT_API_KEY: 'k', LIVEKIT_API_SECRET: API_SECRET, SHAREPLAY_REMOTE_INPUT_ENABLED: 'true' })).toThrow(
      /needs SHAREPLAY_ENABLED/,
    );
    expect(() => loadConfig({ ...base, SHAREPLAY_MAX_QUALITY: '4k120' })).toThrow();
    const config = loadConfig({ ...base });
    expect(config.shareplay).toMatchObject({ enabled: false, remoteInputEnabled: false, maxViewers: 4, maxQuality: '720p60' });
  });

  it('reports PS5-native capture and the virtual controller as unsupported', async () => {
    const status = (await host.get<SharePlayStatusDto>('/shareplay/status')).body;
    expect(status.enabled).toBe(true);
    expect(status.sources.screen.supported).toBe(true);
    expect(status.sources['capture-device'].supported).toBe(true);
    expect(status.sources['ps5-native']).toMatchObject({ supported: false });
    expect(status.sources['ps5-native'].reason).toMatch(/not possible yet/);
    expect(status.virtualController).toMatchObject({ supported: false });
    expect(status.remoteInput).toMatchObject({ supported: true, watchdogMs: 500, heartbeatMs: 100, protocolVersion: 1 });
    expect(status.qualities).toEqual(['480p30', '720p30', '720p60']);
    expect(status.defaultQuality).toBe('720p30');
  });
});

describe('starting and stopping', () => {
  it('lets a member start one stream per party; refuses outsiders, PS5-native capture and qualities above the limit', async () => {
    const party = await startParty();
    expect((await outsider.post(`/parties/${party.id}/shareplay/start`, { source: 'screen' })).status).toBe(403);
    const native = await host.post(`/parties/${party.id}/shareplay/start`, { source: 'ps5-native' });
    expect(native.status).toBe(409);
    expect(native.body.error.code).toBe('SOURCE_UNSUPPORTED');
    expect((await host.post(`/parties/${party.id}/shareplay/start`, { source: 'screen', quality: '1080p60' })).body.error.code).toBe('QUALITY_NOT_ALLOWED');
    expect((await host.post(`/parties/${party.id}/shareplay/start`, { source: 'webcam' })).status).toBe(400);
    expect((await host.post(`/parties/${party.id}/shareplay/start`, { source: 'screen' }, { csrf: false })).status).toBe(403);

    const realtime = await connectRealtime(`${wsBase}/ws`, guest);
    const started = await host.post<SharePlaySessionDto>(`/parties/${party.id}/shareplay/start`, { source: 'capture-device', quality: '720p60' });
    expect(started.status).toBe(201);
    expect(started.body).toMatchObject({ hostId: host.id, source: 'capture-device', quality: '720p60', state: 'starting', hostConnected: false, viewerIds: [] });
    await realtime.waitFor('shareplay.changed', (event) => event.data.partyId === party.id);
    await realtime.close();
    const again = await guest.post(`/parties/${party.id}/shareplay/start`, { source: 'screen' });
    expect(again.body.error.code).toBe('SHAREPLAY_ACTIVE');

    const asGuest = await view(guest, party);
    expect(asGuest.you).toMatchObject({ role: 'viewer', canStart: false, canStop: false, canWatch: true, canRequestController: false });
    expect((await outsider.get(`/parties/${party.id}/shareplay`)).status).toBe(403);

    // Only the host (or the party leader) stops it.
    expect((await guest.post(`/parties/${party.id}/shareplay/stop`)).status).toBe(403);
    expect((await host.post(`/parties/${party.id}/shareplay/quality`, { quality: '480p30' })).body.quality).toBe('480p30');
    expect((await guest.post(`/parties/${party.id}/shareplay/quality`, { quality: '480p30' })).status).toBe(403);
    expect((await host.post(`/parties/${party.id}/shareplay/stop`)).status).toBe(204);
    expect((await view(guest, party)).session).toBeNull();
    await host.post(`/parties/${party.id}/close`);
  });

  it('lets the party leader stop a stream someone else started', async () => {
    const party = await startParty();
    expect((await guest.post(`/parties/${party.id}/shareplay/start`, { source: 'screen' })).status).toBe(201);
    expect((await view(host, party)).you.canStop).toBe(true);
    expect((await third.post(`/parties/${party.id}/shareplay/stop`)).status).toBe(403);
    expect((await host.post(`/parties/${party.id}/shareplay/stop`)).status).toBe(204);
    await host.post(`/parties/${party.id}/close`);
  });

  it('ends a stream that never starts publishing', async () => {
    const party = await startParty();
    server.ctx.shareplay.timings.startTimeoutMs = 150;
    try {
      expect((await host.post(`/parties/${party.id}/shareplay/start`, { source: 'screen' })).status).toBe(201);
      await waitFor(async () => (await view(host, party)).session === null);
    } finally {
      server.ctx.shareplay.timings.startTimeoutMs = 60_000;
    }
    await host.post(`/parties/${party.id}/close`);
  });
});

describe('media server tokens', () => {
  it('gives the host a screen-share-only publisher token and viewers a subscribe-only token; nobody may publish data', async () => {
    const party = await startParty();
    expect((await guest.post(`/parties/${party.id}/shareplay/token`)).status).toBe(404); // nothing to watch yet
    await host.post(`/parties/${party.id}/shareplay/start`, { source: 'screen' });
    const verifier = new TokenVerifier(API_KEY, API_SECRET);

    const hostToken = (await host.post<SharePlayTokenResponse>(`/parties/${party.id}/shareplay/token`)).body;
    expect(hostToken).toMatchObject({ role: 'host', room: roomOf(party), identity: host.id, url: livekit.url });
    const hostClaims = await verifier.verify(hostToken.token);
    expect(hostClaims.video).toMatchObject({ room: roomOf(party), roomJoin: true, canPublish: true, canSubscribe: false, canPublishData: false });
    expect(hostClaims.video?.canPublishSources).toEqual(['screen_share', 'screen_share_audio']);
    expect(hostClaims.video?.roomAdmin).toBeFalsy();

    const viewerToken = (await guest.post<SharePlayTokenResponse>(`/parties/${party.id}/shareplay/token`)).body;
    expect(viewerToken.role).toBe('viewer');
    const viewerClaims = await verifier.verify(viewerToken.token);
    expect(viewerClaims.video).toMatchObject({ room: roomOf(party), canPublish: false, canSubscribe: true, canPublishData: false });
    expect(viewerClaims.video?.canPublishSources ?? []).toEqual([]);
    expect((viewerClaims.exp as number) - (viewerClaims.nbf as number)).toBeLessThanOrEqual(120);

    // The SharePlay room is not the voice room, and voice tokens are unchanged (microphone only).
    const voiceToken = (await guest.post<{ token: string; room: string }>(`/parties/${party.id}/voice/token`)).body;
    expect(voiceToken.room).toBe(`akeno-party-${party.id}`);
    const voiceClaims = await verifier.verify(voiceToken.token);
    expect(voiceClaims.video?.canPublishSources).toEqual(['microphone']);
    expect(voiceClaims.video?.canPublishData).toBe(false);

    const create = livekit.calls.find((call) => call.method === 'CreateRoom' && call.body.name === roomOf(party));
    expect(create?.body.maxParticipants).toBe(3); // 2 viewers + host

    expect((await outsider.post(`/parties/${party.id}/shareplay/token`)).status).toBe(403);
    expect((await guest.post(`/parties/${party.id}/shareplay/token`, {}, { csrf: false })).status).toBe(403);
    await host.post(`/parties/${party.id}/close`);
  });

  it('limits viewers and lets paired devices read the status but never join or control', async () => {
    const party = await startParty();
    await goLive(party);
    await watch(guest, party);
    await watch(third, party);
    const extra = await registerUser(server, 'sp_extra');
    await makeFriends(host, extra);
    await extra.post(`/parties/${party.id}/join`);
    const full = await extra.post(`/parties/${party.id}/shareplay/token`);
    expect(full.status).toBe(409);
    expect(full.body.error.code).toBe('VIEWERS_FULL');
    // A join the SFU reports beyond the limit (for example with a token issued before) is removed.
    livekit.calls.length = 0;
    await livekit.join(server.app, roomOf(party), extra.id);
    expect(livekit.removed(extra.id, roomOf(party))).toBe(true);

    const token = await pairDevice(host);
    const statusResponse = await server.app.inject({ method: 'GET', url: `/api/v1/parties/${party.id}/shareplay`, headers: { authorization: `Bearer ${token}` } });
    expect(statusResponse.statusCode).toBe(200);
    expect((JSON.parse(statusResponse.body) as PartySharePlayDto).session?.state).toBe('live');
    for (const path of ['shareplay/token', 'shareplay/stop', 'shareplay/controller/approve', 'shareplay/start']) {
      const denied = await server.app.inject({ method: 'POST', url: `/api/v1/parties/${party.id}/${path}`, headers: { authorization: `Bearer ${token}` }, payload: {} });
      expect(denied.statusCode, path).toBe(403);
    }
    await host.post(`/parties/${party.id}/close`);
  });
});

describe('media server events', () => {
  it('tracks the host and viewers, removes outsiders and ignores forged webhooks', async () => {
    const party = await startParty();
    const session = await goLive(party);
    expect(session.hostConnected).toBe(true);
    expect(session.liveAt).not.toBeNull();
    await watch(guest, party);
    expect((await view(host, party)).session?.viewerIds).toEqual([guest.id]);

    livekit.calls.length = 0;
    await livekit.join(server.app, roomOf(party), outsider.id);
    expect(livekit.removed(outsider.id, roomOf(party))).toBe(true);

    const forged = await livekit.webhook(server.app, { event: 'participant_left', room: { name: roomOf(party) }, participant: { identity: guest.id } }, { secret: 'x'.repeat(40) });
    expect(forged.statusCode).toBe(401);
    expect((await view(host, party)).session?.viewerIds).toEqual([guest.id]);

    // SharePlay events never touch voice state.
    const partyDto = (await host.get<PartyDto>(`/parties/${party.id}`)).body;
    expect(partyDto.members.every((member) => !member.voice.connected)).toBe(true);

    await livekit.leave(server.app, roomOf(party), guest.id);
    expect((await view(host, party)).session?.viewerIds).toEqual([]);

    // The browser's own "Stop sharing" unpublishes the track: the stream goes back to starting.
    await livekit.track(server.app, roomOf(party), host.id, false);
    expect((await view(host, party)).session?.state).toBe('starting');
    await livekit.track(server.app, roomOf(party), host.id, true);
    expect((await view(host, party)).session?.state).toBe('live');
    await host.post(`/parties/${party.id}/close`);
    await waitFor(() => livekit.deleted(roomOf(party)));
  });

  it('ends the stream when the host does not come back, and reconciles missed events', async () => {
    const party = await startParty();
    await goLive(party);
    server.ctx.shareplay.timings.hostGraceMs = 150;
    try {
      await livekit.leave(server.app, roomOf(party), host.id);
      expect((await view(guest, party)).session?.state).toBe('host-reconnecting');
      await waitFor(async () => (await view(guest, party)).session === null);
    } finally {
      server.ctx.shareplay.timings.hostGraceMs = 60_000;
    }

    // Reconciliation: a stale room without a session is deleted; an intruder is removed.
    livekit.rooms.set('akeno-shareplay-00000000-0000-4000-8000-000000000000', { maxParticipants: 3, participants: new Set(['x']) });
    await goLive(party);
    livekit.rooms.get(roomOf(party))!.participants.add(outsider.id);
    livekit.calls.length = 0;
    await server.ctx.shareplay.reconcile();
    expect(livekit.deleted('akeno-shareplay-00000000-0000-4000-8000-000000000000')).toBe(true);
    expect(livekit.removed(outsider.id, roomOf(party))).toBe(true);
    await host.post(`/parties/${party.id}/close`);
  });
});

describe('Player 2 permission', () => {
  it('is never granted without a request and an explicit approval by the host', async () => {
    const party = await startParty();
    const notLive = await host.post(`/parties/${party.id}/shareplay/start`, { source: 'screen' });
    expect(notLive.status).toBe(201);
    expect((await guest.post(`/parties/${party.id}/shareplay/controller/request`)).body.error.code).toBe('NOT_LIVE');
    await livekit.join(server.app, roomOf(party), host.id);
    await livekit.track(server.app, roomOf(party), host.id);
    await watch(guest, party);

    // Watching or being a member gives no controller access.
    expect((await view(guest, party)).you).toMatchObject({ canRequestController: true, hasControllerGrant: false });
    expect((await host.post(`/parties/${party.id}/shareplay/controller/approve`, { userId: guest.id })).body.error.code).toBe('NO_REQUEST');
    expect((await host.post(`/parties/${party.id}/shareplay/controller/request`)).status).toBe(400); // the host is Player 1
    expect((await outsider.post(`/parties/${party.id}/shareplay/controller/request`)).status).toBe(403);

    const realtime = await connectRealtime(`${wsBase}/ws`, host);
    expect((await guest.post(`/parties/${party.id}/shareplay/controller/request`)).status).toBe(204);
    await realtime.waitFor('shareplay.changed');
    await realtime.close();
    const hostView = await view(host, party);
    expect(hostView.session?.controller.requests.map((request) => request.userId)).toEqual([guest.id]);
    expect((await view(guest, party)).you.controllerRequestPending).toBe(true);

    // Only the host decides (not the guest, not another member, not the leader of another party).
    expect((await guest.post(`/parties/${party.id}/shareplay/controller/approve`, { userId: guest.id })).status).toBe(403);
    expect((await third.post(`/parties/${party.id}/shareplay/controller/approve`, { userId: guest.id })).status).toBe(403);

    const grant = (await host.post<SharePlayControllerGrantDto>(`/parties/${party.id}/shareplay/controller/approve`, { userId: guest.id })).body;
    expect(grant).toMatchObject({ guestId: guest.id, slot: 2, inputConnected: false, packetsAccepted: 0 });
    expect((await view(guest, party)).you).toMatchObject({ hasControllerGrant: true, canRequestController: false });
    // An approval cannot be replayed, and Player 2 cannot be taken twice.
    expect((await host.post(`/parties/${party.id}/shareplay/controller/approve`, { userId: guest.id })).body.error.code).toBe('NO_REQUEST');
    await watch(third, party);
    expect((await third.post(`/parties/${party.id}/shareplay/controller/request`)).body.error.code).toBe('PLAYER2_TAKEN');

    // Revoking: other members cannot; the host can; a new grant gets a new epoch.
    expect((await third.post(`/parties/${party.id}/shareplay/controller/revoke`)).status).toBe(403);
    expect((await host.post(`/parties/${party.id}/shareplay/controller/revoke`)).status).toBe(204);
    expect((await view(guest, party)).you.hasControllerGrant).toBe(false);
    await guest.post(`/parties/${party.id}/shareplay/controller/request`);
    const second = (await host.post<SharePlayControllerGrantDto>(`/parties/${party.id}/shareplay/controller/approve`, { userId: guest.id })).body;
    expect(second.epoch).not.toBe(grant.epoch);
    expect(second.grantId).not.toBe(grant.grantId);
    // The guest can give it back.
    expect((await guest.post(`/parties/${party.id}/shareplay/controller/revoke`)).status).toBe(204);

    // Deny and cancel.
    await third.post(`/parties/${party.id}/shareplay/controller/request`);
    expect((await host.post(`/parties/${party.id}/shareplay/controller/deny`, { userId: third.id })).status).toBe(204);
    expect((await view(third, party)).you.controllerRequestPending).toBe(false);
    await third.post(`/parties/${party.id}/shareplay/controller/request`);
    expect((await third.post(`/parties/${party.id}/shareplay/controller/cancel`)).status).toBe(204);
    expect((await view(host, party)).session?.controller.requests).toEqual([]);
    await host.post(`/parties/${party.id}/close`);
  });

  it('expires unanswered requests and grants', async () => {
    const party = await startParty();
    await goLive(party);
    await watch(guest, party);
    server.ctx.shareplay.timings.requestTtlMs = 100;
    try {
      await guest.post(`/parties/${party.id}/shareplay/controller/request`);
      await sleep(200);
      expect((await host.post(`/parties/${party.id}/shareplay/controller/approve`, { userId: guest.id })).body.error.code).toBe('NO_REQUEST');
    } finally {
      server.ctx.shareplay.timings.requestTtlMs = 60_000;
    }
    const grantMax = server.ctx.config.shareplay.grantMaxMs;
    server.ctx.config.shareplay.grantMaxMs = 150;
    try {
      await guest.post(`/parties/${party.id}/shareplay/controller/request`);
      await host.post(`/parties/${party.id}/shareplay/controller/approve`, { userId: guest.id });
      await waitFor(async () => (await view(guest, party)).session?.controller.grant === null);
    } finally {
      server.ctx.config.shareplay.grantMaxMs = grantMax;
    }
    await host.post(`/parties/${party.id}/close`);
  });

  it('is refused when remote input is switched off', async () => {
    const off = await createTestServer({ LIVEKIT_URL: livekit.url, LIVEKIT_API_KEY: API_KEY, LIVEKIT_API_SECRET: API_SECRET, SHAREPLAY_ENABLED: 'true' });
    try {
      const user = await registerUser(off, 'sp_off');
      const party = (await user.post<PartyDto>('/parties', { privacy: 'friends', maxMembers: 4 })).body;
      expect((await user.get<SharePlayStatusDto>('/shareplay/status')).body.remoteInput.supported).toBe(false);
      await user.post(`/parties/${party.id}/shareplay/start`, { source: 'screen' });
      expect((await user.post(`/parties/${party.id}/shareplay/controller/request`)).body.error.code).toBe('REMOTE_INPUT_DISABLED');
    } finally {
      await off.close();
    }
  });
});

describe('controller link', () => {
  it('refuses connections without a current grant, from other origins and from non-hosts as monitors', async () => {
    const party = await startParty();
    await goLive(party);
    await expectRejected(openInput(guest, { party: party.id, role: 'sender', epoch: 1 }), 403);
    await expectRejected(openInput(third, { party: party.id, role: 'monitor' }), 403);
    await expectRejected(openInput(outsider, { party: party.id, role: 'monitor' }), 403);
    const grant = await grantPlayer2(party);
    await expectRejected(openInput(guest, { party: party.id, role: 'sender', epoch: grant.epoch + 1 }), 409);
    await expectRejected(openInput(third, { party: party.id, role: 'sender', epoch: grant.epoch }), 403);
    await expectRejected(openInput(guest, { party: party.id, role: 'sender', epoch: grant.epoch }, 'https://evil.example'), 403);
    await expectRejected(openInput(guest, { party: party.id, role: 'sender', epoch: grant.epoch }, null), 403);
    const anonymous = new TestClientless();
    await expectRejected(openInput(anonymous as unknown as TestClient, { party: party.id, role: 'sender', epoch: grant.epoch }), 401);
    await host.post(`/parties/${party.id}/close`);
  });

  it('relays valid packets to the host monitor and the host’s console app, and drops everything else', async () => {
    const party = await startParty();
    const session = await goLive(party);
    const grant = await grantPlayer2(party);
    const deviceToken = await pairDevice(host);
    const console = await connectDevice(deviceToken);
    // A second console of the host that never opts in (for example an older console app).
    const oldConsole = await connectDevice(await pairDevice(host));
    const guestDeviceToken = await pairDevice(guest);
    const guestConsole = await connectDevice(guestDeviceToken);
    const register = (token: string) =>
      server.app.inject({ method: 'POST', url: `/api/v1/parties/${party.id}/shareplay/receiver`, headers: { authorization: `Bearer ${token}` }, payload: {} });
    expect((await register(deviceToken)).statusCode).toBe(200);
    expect((await register(guestDeviceToken)).statusCode).toBe(200);
    // Browser sessions cannot register as console receivers.
    expect((await host.post(`/parties/${party.id}/shareplay/receiver`)).status).toBe(400);
    const hostBrowser = await connectRealtime(`${wsBase}/ws`, host);
    const monitor = await openInput(host, { party: party.id, role: 'monitor' });
    const sender = await openInput(guest, { party: party.id, role: 'sender', epoch: grant.epoch });
    await waitFor(() => sender.texts.length > 0);
    expect(sender.texts[0]).toMatchObject({ type: 'ready', epoch: grant.epoch, lastSeq: 0, watchdogMs: 500 });
    await waitFor(async () => (await view(host, party)).session?.controller.grant?.inputConnected === true);

    const cross = 1 << BUTTON.south;
    sender.send(packet(grant.epoch, 1, { buttons: cross, axes: [32767, -32767, 0, 1200], triggers: [0, 255] }));
    await waitFor(() => monitor.relays().length === 1);
    expect(monitor.relays()[0]).toMatchObject({
      sessionId: session.id,
      grantId: grant.grantId,
      epoch: grant.epoch,
      guestId: guest.id,
      slot: 2,
      seq: 1,
      kind: 'state',
      buttons: cross,
      axes: [32767, -32767, 0, 1200],
      triggers: [0, 255],
    });
    const relayed = await console.waitFor('shareplay.input', (event) => event.data.seq === 1);
    expect(relayed.data).toMatchObject({ buttons: cross, guestId: guest.id, grantId: grant.grantId });

    // Refused: stale and duplicated sequence numbers, another grant's epoch, malformed packets,
    // the PS/Home and Create buttons, a wrong slot, unknown bits, wrong sizes.
    sender.send(packet(grant.epoch, 1, { buttons: 0 }));
    sender.send(packet(grant.epoch + 7, 9));
    const bad = packet(grant.epoch, 10);
    bad[0] = 0x00;
    sender.send(bad);
    const blocked = packet(grant.epoch, 11);
    new DataView(blocked.buffer).setUint32(16, 1 << BUTTON.home, true);
    sender.send(blocked);
    const create = packet(grant.epoch, 12);
    new DataView(create.buffer).setUint32(16, 1 << BUTTON.create, true);
    sender.send(create);
    const slot = packet(grant.epoch, 13);
    slot[3] = 1;
    sender.send(slot);
    const unknownBits = packet(grant.epoch, 14);
    new DataView(unknownBits.buffer).setUint32(16, 1 << 25, true);
    sender.send(unknownBits);
    sender.send(new Uint8Array(INPUT_PACKET_BYTES - 1));
    // …and accepted again afterwards, in order.
    sender.send(packet(grant.epoch, 20, { buttons: 0 }));
    await waitFor(() => monitor.relays().length === 2);
    expect(monitor.relays().map((relay) => relay.seq)).toEqual([1, 20]);
    const stats = (await view(host, party)).session!.controller.grant!;
    expect(stats.packetsAccepted).toBe(2);
    expect(stats.packetsRejected).toBe(8);
    expect(stats.lastInputAt).not.toBeNull();

    // Only the host's console receives Player 2 input: not the guest's console, not browsers' realtime.
    await sleep(100);
    expect(guestConsole.events.some((event) => event.type === 'shareplay.input')).toBe(false);
    expect(oldConsole.events.some((event) => event.type === 'shareplay.input')).toBe(false);
    expect(hostBrowser.events.some((event) => event.type === 'shareplay.input')).toBe(false);

    // Revocation: everything released at once, the guest's link closes and cannot reopen.
    expect((await host.post(`/parties/${party.id}/shareplay/controller/revoke`)).status).toBe(204);
    expect(await sender.closed()).toBe(SHAREPLAY_INPUT_CLOSE.grantEnded);
    await waitFor(() => monitor.relays().some((relay) => relay.kind === 'release'));
    const release = monitor.relays().find((relay) => relay.kind === 'release')!;
    expect(release).toMatchObject({ buttons: 0, axes: [0, 0, 0, 0], triggers: [0, 0], reason: 'The host revoked Player 2.' });
    await console.waitFor('shareplay.input', (event) => event.data.kind === 'release');
    await expectRejected(openInput(guest, { party: party.id, role: 'sender', epoch: grant.epoch }), 403);

    for (const socket of [console, oldConsole, guestConsole, hostBrowser]) await socket.close();
    monitor.close();
    await host.post(`/parties/${party.id}/close`);
  });

  it('closes links that send text or flood, and rate-limits input', async () => {
    const party = await startParty();
    await goLive(party);
    const grant = await grantPlayer2(party);
    const monitor = await openInput(host, { party: party.id, role: 'monitor' });
    let sender = await openInput(guest, { party: party.id, role: 'sender', epoch: grant.epoch });
    // 400 packets at once: only the burst allowance (plus the refill) gets through.
    for (let seq = 1; seq <= 400; seq += 1) sender.send(packet(grant.epoch, seq, { buttons: seq % 2 }));
    await sleep(300);
    const forwarded = monitor.relays().length;
    expect(forwarded).toBeGreaterThan(10);
    expect(forwarded).toBeLessThan(120);

    sender.socket.send('{"buttons":1}');
    expect(await sender.closed()).toBe(SHAREPLAY_INPUT_CLOSE.protocol);
    // The guest reconnects within the grace period and continues after the last accepted packet.
    sender = await openInput(guest, { party: party.id, role: 'sender', epoch: grant.epoch });
    await waitFor(() => sender.texts.length > 0);
    expect(sender.texts[0].lastSeq).toBeGreaterThan(10);
    // Malformed packets (paced under the rate limit) close the link after a tolerance.
    for (let batch = 0; batch < 6 && sender.closeCode === null; batch += 1) {
      for (let index = 0; index < 15; index += 1) sender.send(new Uint8Array(5));
      await sleep(200);
    }
    expect(await sender.closed()).toBe(SHAREPLAY_INPUT_CLOSE.protocol);
    monitor.close();
    await host.post(`/parties/${party.id}/close`);
  });

  it('releases input when the guest’s link drops and ends the grant if it does not come back', async () => {
    const party = await startParty();
    await goLive(party);
    const grant = await grantPlayer2(party);
    const monitor = await openInput(host, { party: party.id, role: 'monitor' });
    let sender = await openInput(guest, { party: party.id, role: 'sender', epoch: grant.epoch });
    sender.send(packet(grant.epoch, 1, { buttons: 1 << BUTTON.r1 }));
    await waitFor(() => monitor.relays().length === 1);

    // A second tab replaces the first link.
    const replacement = await openInput(guest, { party: party.id, role: 'sender', epoch: grant.epoch });
    expect(await sender.closed()).toBe(SHAREPLAY_INPUT_CLOSE.replaced);
    sender = replacement;

    server.ctx.shareplay.timings.inputReconnectGraceMs = 150;
    try {
      sender.close();
      await waitFor(() => monitor.relays().some((relay) => relay.kind === 'release' && /dropped/.test(relay.reason ?? '')));
      await waitFor(async () => (await view(host, party)).session?.controller.grant === null);
    } finally {
      server.ctx.shareplay.timings.inputReconnectGraceMs = 60_000;
    }
    monitor.close();
    await host.post(`/parties/${party.id}/close`);
  });
});

/** A client without a session (for the anonymous upgrade test). */
class TestClientless {
  cookie: string | null = null;
}

describe('automatic revocation', () => {
  async function liveWithGuestLink(): Promise<{ party: PartyDto; grant: SharePlayControllerGrantDto; sender: InputSocket; monitor: InputSocket }> {
    const party = await startParty();
    await goLive(party);
    const grant = await grantPlayer2(party);
    const monitor = await openInput(host, { party: party.id, role: 'monitor' });
    const sender = await openInput(guest, { party: party.id, role: 'sender', epoch: grant.epoch });
    sender.send(packet(grant.epoch, 1, { buttons: 1 << BUTTON.south }));
    await waitFor(() => monitor.relays().length === 1);
    return { party, grant, sender, monitor };
  }

  async function expectEnded(sender: InputSocket, monitor: InputSocket | null): Promise<void> {
    expect(await sender.closed()).toBe(SHAREPLAY_INPUT_CLOSE.grantEnded);
    if (monitor) await waitFor(() => monitor.relays().some((relay) => relay.kind === 'release'));
  }

  it('when the guest leaves the party or is removed', async () => {
    let { party, sender, monitor } = await liveWithGuestLink();
    await guest.post(`/parties/${party.id}/leave`);
    await expectEnded(sender, monitor);
    expect((await view(host, party)).session?.controller.grant).toBeNull();
    monitor.close();
    await host.post(`/parties/${party.id}/close`);

    ({ party, sender, monitor } = await liveWithGuestLink());
    livekit.calls.length = 0;
    expect((await host.delete(`/parties/${party.id}/members/${guest.id}`)).status).toBe(204);
    await expectEnded(sender, monitor);
    await waitFor(() => livekit.removed(guest.id, roomOf(party)));
    expect((await guest.post(`/parties/${party.id}/shareplay/token`)).status).toBe(403);
    monitor.close();
    await host.post(`/parties/${party.id}/close`);
  });

  it('when the host leaves, the party closes, or the host stops the stream', async () => {
    let { party, sender, monitor } = await liveWithGuestLink();
    await host.post(`/parties/${party.id}/leave`);
    await expectEnded(sender, monitor);
    expect(await monitor.closed()).toBe(SHAREPLAY_INPUT_CLOSE.grantEnded);
    expect((await view(guest, party)).session).toBeNull();
    await guest.post(`/parties/${party.id}/leave`);
    await third.post(`/parties/${party.id}/leave`);

    ({ party, sender, monitor } = await liveWithGuestLink());
    await host.post(`/parties/${party.id}/close`);
    await expectEnded(sender, monitor);
    await waitFor(() => livekit.deleted(roomOf(party)));

    ({ party, sender, monitor } = await liveWithGuestLink());
    await host.post(`/parties/${party.id}/shareplay/stop`);
    await expectEnded(sender, monitor);
    await host.post(`/parties/${party.id}/close`);
  });

  it('when the host disconnects from the stream or the guest stops watching', async () => {
    let { party, sender, monitor } = await liveWithGuestLink();
    await livekit.leave(server.app, roomOf(party), host.id);
    await expectEnded(sender, monitor);
    expect((await view(guest, party)).session?.state).toBe('host-reconnecting');
    // The host comes back: the stream continues, Player 2 has to be approved again.
    await livekit.join(server.app, roomOf(party), host.id);
    await livekit.track(server.app, roomOf(party), host.id);
    expect((await view(guest, party)).session).toMatchObject({ state: 'live', controller: { grant: null } });
    monitor.close();
    await host.post(`/parties/${party.id}/close`);

    ({ party, sender, monitor } = await liveWithGuestLink());
    await livekit.leave(server.app, roomOf(party), guest.id);
    await expectEnded(sender, monitor);
    monitor.close();
    await host.post(`/parties/${party.id}/close`);
  });

  it('when one of them blocks the other', async () => {
    const { party, sender, monitor } = await liveWithGuestLink();
    livekit.calls.length = 0;
    expect((await guest.post('/blocks', { userId: host.id })).status).toBeLessThan(300);
    await expectEnded(sender, monitor);
    await waitFor(() => livekit.removed(guest.id, roomOf(party)));
    monitor.close();
    await host.post(`/parties/${party.id}/close`);
    await guest.delete(`/blocks/${host.id}`);
    await makeFriends(host, guest);
  });

  it('when the guest’s browser session is signed out or the account is suspended', async () => {
    let { party, sender, monitor } = await liveWithGuestLink();
    expect((await guest.post('/auth/logout')).status).toBe(204);
    await expectEnded(sender, monitor);
    guest = await login(server, 'sp_guest');
    monitor.close();
    await host.post(`/parties/${party.id}/close`);

    ({ party, sender, monitor } = await liveWithGuestLink());
    server.ctx.shareplay.removeUserEverywhere(guest.id); // the suspension hook (administration)
    await expectEnded(sender, monitor);
    monitor.close();
    await host.post(`/parties/${party.id}/close`);
  });
});
