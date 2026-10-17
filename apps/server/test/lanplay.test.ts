import { afterAll, afterEach, beforeAll, describe, expect, it } from 'vitest';
import type { LanPlayConnectionDto, LanPlayStatusDto, PartyDto, PartyLanPlayDto } from '@akeno/shared';
import type { AppContext } from '../src/app/context.js';
import { DEFAULT_LAN_PLAY_TIMINGS, LanPlayService } from '../src/modules/lanplay/service.js';
import type { LanTransportAdapter, TransportGrant } from '../src/modules/lanplay/transport.js';
import { UnavailableTransport } from '../src/modules/lanplay/transport.js';
import { createTestServer, makeFriends, registerUser, type TestClient, type TestServer } from './helpers.js';

let server: TestServer;
let host: TestClient;
let guest: TestClient;
let third: TestClient;
let outsider: TestClient;
/** Every client that might join a party, so each test starts from a clean slate. */
const clients: TestClient[] = [];

async function leaveCurrentParty(client: TestClient): Promise<void> {
  const current = await client.get<{ party: PartyDto | null }>('/parties/current');
  if (current.body?.party) await client.post(`/parties/${current.body.party.id}/leave`);
}

async function startParty(maxMembers = 6): Promise<PartyDto> {
  const created = await host.post<PartyDto>('/parties', { name: 'LAN Night', privacy: 'friends', maxMembers });
  expect(created.status).toBe(201);
  expect((await guest.post(`/parties/${created.body.id}/join`)).status).toBe(200);
  expect((await third.post(`/parties/${created.body.id}/join`)).status).toBe(200);
  return created.body;
}

async function view(client: TestClient, party: PartyDto): Promise<PartyLanPlayDto> {
  const response = await client.get<PartyLanPlayDto>(`/parties/${party.id}/lanplay`);
  expect(response.status).toBe(200);
  return response.body;
}

beforeAll(async () => {
  server = await createTestServer({ LAN_PLAY_ENABLED: 'true', LAN_PLAY_MAX_PARTICIPANTS: '3' });
  host = await registerUser(server, 'lp_host', { displayName: 'YTDxDAkeno' });
  guest = await registerUser(server, 'lp_guest', { displayName: 'AkenoV2' });
  third = await registerUser(server, 'lp_third');
  outsider = await registerUser(server, 'lp_outsider');
  clients.push(host, guest, third, outsider);
  await makeFriends(host, guest);
  await makeFriends(host, third);
});

afterEach(async () => {
  // Everyone leaves whatever party they are in so the next test can start a fresh one.
  for (const client of clients) await leaveCurrentParty(client);
});

afterAll(async () => {
  await server.close();
});

describe('configuration and availability', () => {
  it('is off unless explicitly enabled', async () => {
    const plain = await createTestServer();
    try {
      const user = await registerUser(plain, 'lp_off');
      const status = await user.get<LanPlayStatusDto>('/lanplay/status');
      expect(status.status).toBe(200);
      expect(status.body.enabled).toBe(false);
      expect(status.body.reason).toMatch(/not enabled/i);
      const party = (await user.post<PartyDto>('/parties', { privacy: 'friends', maxMembers: 4 })).body;
      const create = await user.post(`/parties/${party.id}/lanplay`, { platform: 'pc' });
      expect(create.status).toBe(503);
      expect(create.body.error.code).toBe('LAN_PLAY_UNAVAILABLE');
    } finally {
      await plain.close();
    }
  });

  it('reports a transport that is not available and no pair that looks ready', async () => {
    const status = (await host.get<LanPlayStatusDto>('/lanplay/status')).body;
    expect(status.enabled).toBe(true);
    expect(status.transport.id).toBe('none');
    expect(status.transport.available).toBe(false);
    expect(status.transport.reason).toMatch(/Transport not available/i);
    expect(status.platforms.length).toBeGreaterThan(0);
    expect(status.platforms.every((pair) => pair.capability.supported === false)).toBe(true);
  });

  it('serves the compatibility database honestly (nothing hardware-tested)', async () => {
    const { body } = await host.get<{ games: Array<{ evidence: string; crossPlatform: string; sources: unknown[] }> }>('/lanplay/games');
    expect(body.games.length).toBeGreaterThan(0);
    for (const game of body.games) {
      expect(game.evidence).not.toBe('hardware-tested');
      expect(game.crossPlatform).not.toBe('hardware-tested');
      expect(game.sources.length).toBeGreaterThan(0);
    }
  });
});

describe('session lifecycle and roles', () => {
  it('lets the host create one session and others join; rejects a non-member', async () => {
    const party = await startParty();
    const created = await host.post<{ hostId: string }>(`/parties/${party.id}/lanplay`, { platform: 'pc' });
    expect(created.status).toBe(201);
    expect(created.body.hostId).toBe(host.id);
    expect((await guest.post(`/parties/${party.id}/lanplay`, { platform: 'pc' })).status).toBe(409); // one per party

    expect((await guest.post(`/parties/${party.id}/lanplay/join`, { platform: 'pc' })).status).toBe(200);
    expect((await outsider.post(`/parties/${party.id}/lanplay/join`, { platform: 'pc' })).status).toBe(403);

    const seen = await view(guest, party);
    expect(seen.session?.participants.map((p) => p.userId).sort()).toEqual([host.id, guest.id].sort());
    expect(seen.session?.participants.every((p) => p.connected === false)).toBe(true);
    expect(seen.you.role).toBe('player');
  });

  it('enforces participant capacity', async () => {
    const party = await startParty();
    await host.post(`/parties/${party.id}/lanplay`, { platform: 'pc' });
    expect((await guest.post(`/parties/${party.id}/lanplay/join`, { platform: 'pc' })).status).toBe(200);
    expect((await third.post(`/parties/${party.id}/lanplay/join`, { platform: 'pc' })).status).toBe(200);
    const fourth = await registerUser(server, 'lp_fourth');
    clients.push(fourth);
    await makeFriends(host, fourth);
    await fourth.post(`/parties/${party.id}/join`);
    const full = await fourth.post(`/parties/${party.id}/lanplay/join`, { platform: 'pc' });
    expect(full.status).toBe(409);
    expect(full.body.error.code).toBe('LAN_PARTICIPANTS_FULL');
  });

  it('only the host can select the game, and only known games', async () => {
    const party = await startParty();
    await host.post(`/parties/${party.id}/lanplay`, { platform: 'ps5', gameId: 'f1-23-ps5' });
    expect((await guest.post(`/parties/${party.id}/lanplay/join`, { platform: 'pc' })).status).toBe(200);
    expect((await guest.post(`/parties/${party.id}/lanplay/game`, { gameId: 'f1-23-ps5' })).status).toBe(403);
    expect((await host.post(`/parties/${party.id}/lanplay/game`, { gameId: 'does-not-exist' })).status).toBe(404);
    expect((await host.post(`/parties/${party.id}/lanplay/game`, { gameId: null })).status).toBe(200);
  });

  it('refuses to connect: no transport is available', async () => {
    const party = await startParty();
    await host.post(`/parties/${party.id}/lanplay`, { platform: 'pc' });
    const connect = await host.post(`/parties/${party.id}/lanplay/connect`);
    expect(connect.status).toBe(503);
    expect(connect.body.error.code).toBe('LAN_TRANSPORT_UNAVAILABLE');
    expect(connect.body.error.message).toMatch(/Transport not available/i);
  });

  it('the party leader can end a session a member hosts', async () => {
    const party = await startParty();
    expect((await guest.post(`/parties/${party.id}/lanplay`, { platform: 'pc' })).status).toBe(201); // guest hosts the LAN session
    expect((await third.post(`/parties/${party.id}/lanplay/join`, { platform: 'pc' })).status).toBe(200);
    expect((await third.delete(`/parties/${party.id}/lanplay`)).status).toBe(403); // neither host nor leader
    expect((await host.delete(`/parties/${party.id}/lanplay`)).status).toBe(204); // party leader
    expect((await third.get<PartyLanPlayDto>(`/parties/${party.id}/lanplay`)).body.session).toBeNull();
  });
});

describe('revocation', () => {
  it('ends the session when the host leaves the party', async () => {
    const party = await startParty();
    await host.post(`/parties/${party.id}/lanplay`, { platform: 'pc' });
    await guest.post(`/parties/${party.id}/lanplay/join`, { platform: 'pc' });
    await host.post(`/parties/${party.id}/leave`);
    expect((await guest.get<PartyLanPlayDto>(`/parties/${party.id}/lanplay`)).body.session).toBeNull();
  });

  it('removes a participant when they leave the party', async () => {
    const party = await startParty();
    await host.post(`/parties/${party.id}/lanplay`, { platform: 'pc' });
    await guest.post(`/parties/${party.id}/lanplay/join`, { platform: 'pc' });
    await guest.post(`/parties/${party.id}/leave`);
    const seen = await view(host, party);
    expect(seen.session?.participants.map((p) => p.userId)).toEqual([host.id]);
  });

  it('removes a participant when a block is created', async () => {
    const party = await startParty();
    await host.post(`/parties/${party.id}/lanplay`, { platform: 'pc' });
    await guest.post(`/parties/${party.id}/lanplay/join`, { platform: 'pc' });
    expect((await host.post('/blocks', { userId: guest.id })).status).toBeLessThan(300);
    const seen = await view(host, party);
    expect(seen.session?.participants.some((p) => p.userId === guest.id)).toBe(false);
    await host.delete(`/blocks/${guest.id}`);
    await makeFriends(host, guest);
  });
});

/**
 * Credential lifecycle (issue / verify / expire / revoke) against the service with a stub
 * transport that is "available" but carries nothing. This proves the control plane issues and
 * revokes credentials correctly; it is not a working data plane.
 */
describe('credential lifecycle (stub transport)', () => {
  class StubTransport implements LanTransportAdapter {
    readonly id = 'stub';
    readonly label = 'Stub (test only, carries nothing)';
    availability() {
      return { available: true, reason: null };
    }
    async authorize(): Promise<TransportGrant> {
      return { connectionMode: 'bridge', connectable: false, details: { note: 'stub' } };
    }
    async revoke(): Promise<void> {}
    async release(): Promise<void> {}
  }

  function fakeContext(partyId: string, memberIds: string[]): AppContext {
    return {
      config: { lanplay: { enabled: true, maxParticipants: 8, sessionTtlMs: DEFAULT_LAN_PLAY_TIMINGS.sessionTtlMs, credentialTtlMs: 50 } },
      parties: {
        currentPartyId: async (userId: string) => (memberIds.includes(userId) ? partyId : null),
        memberIds: async () => memberIds,
      },
      hub: { sendToUsers: () => {} },
      log: { info() {}, warn() {}, error() {} },
    } as unknown as AppContext;
  }

  it('issues a credential a data plane can verify, then expires it', async () => {
    const partyId = '11111111-1111-4111-8111-111111111111';
    const hostId = '22222222-2222-4222-8222-222222222222';
    const svc = new LanPlayService(fakeContext(partyId, [hostId]), { transport: new StubTransport(), timings: { credentialTtlMs: 40 } });
    await svc.create(hostId, partyId, 'pc', null);
    const connection: LanPlayConnectionDto = await svc.connect(hostId, partyId);
    expect(connection.transport.id).toBe('stub');
    expect(connection.transport.connectable).toBe(false);
    expect(svc.verifyCredential(connection.credential)).toEqual({ sessionId: connection.sessionId, userId: hostId });
    expect(svc.verifyCredential('lanc_wrong')).toBeNull();
    await new Promise((resolve) => setTimeout(resolve, 60));
    expect(svc.verifyCredential(connection.credential)).toBeNull();
    svc.shutdown();
  });

  it('revokes the credential when the participant leaves', async () => {
    const partyId = '33333333-3333-4333-8333-333333333333';
    const hostId = '44444444-4444-4444-8444-444444444444';
    const playerId = '55555555-5555-4555-8555-555555555555';
    const svc = new LanPlayService(fakeContext(partyId, [hostId, playerId]), { transport: new StubTransport() });
    await svc.create(hostId, partyId, 'pc', null);
    await svc.join(playerId, partyId, 'pc');
    const connection = await svc.connect(playerId, partyId);
    expect(svc.verifyCredential(connection.credential)).not.toBeNull();
    await svc.leave(playerId, partyId);
    expect(svc.verifyCredential(connection.credential)).toBeNull();
    svc.shutdown();
  });

  it('the unavailable transport authorizes nothing', async () => {
    const transport = new UnavailableTransport();
    expect(transport.availability().available).toBe(false);
    expect(await transport.authorize()).toBeNull();
  });
});
