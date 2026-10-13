import { eq } from 'drizzle-orm';
import { afterAll, beforeAll, describe, expect, it } from 'vitest';
import { gameSessionMembers, gameSessions } from '../src/database/schema/index.js';
import { createTestServer, makeFriends, registerUser, type TestClient, type TestServer } from './helpers.js';

let server: TestServer;

beforeAll(async () => {
  server = await createTestServer();
});

afterAll(async () => {
  await server.close();
});

function createSession(host: TestClient, overrides: Record<string, unknown> = {}) {
  return host.post('/game-sessions', {
    gameName: 'Starfall Arena',
    description: 'Casual matches',
    visibility: 'public',
    joinMode: 'open',
    maxParticipants: 4,
    region: 'Europe West',
    platform: 'PC',
    connectionInfo: 'Lobby code: 4242',
    ...overrides,
  });
}

describe('game sessions', () => {
  it('creates a session with the host as first participant', async () => {
    const host = await registerUser(server, 'gs_host');
    const created = await createSession(host);
    expect(created.status).toBe(201);
    expect(created.body).toMatchObject({
      gameName: 'Starfall Arena',
      participantCount: 1,
      status: 'waiting',
      viewerState: 'host',
      connectionInfo: 'Lobby code: 4242',
    });
    expect(created.body.members[0].user.id).toBe(host.id);
    const invalid = await host.post('/game-sessions', { gameName: '', maxParticipants: 1 });
    expect(invalid.status).toBe(400);
  });

  it('joins and leaves open sessions and hides connection info from non-participants', async () => {
    const host = await registerUser(server, 'gsj_host');
    const player = await registerUser(server, 'gsj_player');
    const session = (await createSession(host)).body;

    const preview = await player.get(`/game-sessions/${session.id}`);
    expect(preview.body.viewerState).toBe('none');
    expect(preview.body.connectionInfo).toBeNull();

    const join = await player.post(`/game-sessions/${session.id}/join`);
    expect(join.status).toBe(200);
    expect(join.body.outcome).toBe('joined');
    expect(join.body.session.participantCount).toBe(2);
    expect(join.body.session.connectionInfo).toBe('Lobby code: 4242');
    expect(join.body.session.members.map((m: { user: { id: string } }) => m.user.id)).toEqual([host.id, player.id]);
    expect((await player.post(`/game-sessions/${session.id}/join`)).body.outcome).toBe('already_member');

    expect((await player.post(`/game-sessions/${session.id}/leave`)).status).toBe(204);
    expect((await host.get(`/game-sessions/${session.id}`)).body.participantCount).toBe(1);
  });

  it('never exceeds capacity under concurrent joins', async () => {
    const host = await registerUser(server, 'gsc_host');
    const session = (await createSession(host, { maxParticipants: 4 })).body;
    const players = await Promise.all(Array.from({ length: 10 }, (_, i) => registerUser(server, `gsc_player${i}`)));
    const results = await Promise.all(players.map((player) => player.post(`/game-sessions/${session.id}/join`)));
    const joined = results.filter((result) => result.status === 200 && result.body.outcome === 'joined');
    const full = results.filter((result) => result.status === 409 && result.body.error.code === 'CAPACITY_REACHED');
    expect(joined).toHaveLength(3);
    expect(full).toHaveLength(7);
    const [row] = await server.ctx.db.select().from(gameSessions).where(eq(gameSessions.id, session.id));
    expect(row?.participantCount).toBe(4);
    const members = await server.ctx.db.select().from(gameSessionMembers).where(eq(gameSessionMembers.sessionId, session.id));
    expect(members).toHaveLength(4);
  });

  it('supports approval-based joining', async () => {
    const host = await registerUser(server, 'gsa_host');
    const requester = await registerUser(server, 'gsa_req');
    const rejected = await registerUser(server, 'gsa_rej');
    const session = (await createSession(host, { joinMode: 'approval' })).body;

    const request = await requester.post(`/game-sessions/${session.id}/join`, { message: 'Can I join?' });
    expect(request.body.outcome).toBe('requested');
    expect(request.body.session.viewerState).toBe('requested');
    await rejected.post(`/game-sessions/${session.id}/join`);

    const hostView = await host.get(`/game-sessions/${session.id}`);
    expect(hostView.body.joinRequests.map((r: { message: string }) => r.message)).toContain('Can I join?');
    expect((await requester.get(`/game-sessions/${session.id}`)).body.joinRequests).toEqual([]);
    const notification = (await host.get('/notifications')).body.notifications[0];
    expect(notification.data.type).toBe('game_session.join_requested');

    const toAccept = hostView.body.joinRequests.find((r: { user: { id: string } }) => r.user.id === requester.id);
    const toReject = hostView.body.joinRequests.find((r: { user: { id: string } }) => r.user.id === rejected.id);
    expect((await requester.post(`/game-sessions/${session.id}/requests/${toAccept.id}/accept`)).status).toBe(403);
    const accepted = await host.post(`/game-sessions/${session.id}/requests/${toAccept.id}/accept`);
    expect(accepted.body.participantCount).toBe(2);
    await host.post(`/game-sessions/${session.id}/requests/${toReject.id}/reject`);
    expect((await requester.get(`/game-sessions/${session.id}`)).body.viewerState).toBe('member');
    expect((await rejected.get(`/game-sessions/${session.id}`)).body.viewerState).toBe('none');
    expect((await rejected.get('/notifications')).body.notifications[0].data.type).toBe('game_session.request_rejected');
  });

  it('applies visibility rules to discovery and access', async () => {
    const host = await registerUser(server, 'gsv_host');
    const friend = await registerUser(server, 'gsv_friend');
    const stranger = await registerUser(server, 'gsv_stranger');
    await makeFriends(host, friend);
    const publicSession = (await createSession(host, { gameName: 'Public Quest' })).body;
    const friendsSession = (await createSession(host, { gameName: 'Friends Quest', visibility: 'friends' })).body;
    const privateSession = (await createSession(host, { gameName: 'Private Quest', visibility: 'private' })).body;

    const strangerList = (await stranger.get('/game-sessions')).body.sessions.map((s: { id: string }) => s.id);
    expect(strangerList).toContain(publicSession.id);
    expect(strangerList).not.toContain(friendsSession.id);
    expect(strangerList).not.toContain(privateSession.id);
    const friendList = (await friend.get('/game-sessions')).body.sessions.map((s: { id: string }) => s.id);
    expect(friendList).toEqual(expect.arrayContaining([publicSession.id, friendsSession.id]));
    expect(friendList).not.toContain(privateSession.id);

    expect((await stranger.get(`/game-sessions/${friendsSession.id}`)).status).toBe(404);
    expect((await stranger.post(`/game-sessions/${friendsSession.id}/join`)).status).toBe(404);
    expect((await friend.post(`/game-sessions/${privateSession.id}/join`)).status).toBe(404);

    // Private sessions are joinable through an invitation from the host.
    expect((await host.post(`/game-sessions/${privateSession.id}/invitations`, { userId: friend.id })).status).toBe(201);
    expect((await friend.get(`/game-sessions/${privateSession.id}`)).body.viewerState).toBe('invited');
    const joined = await friend.post(`/game-sessions/${privateSession.id}/join`);
    expect(joined.body.outcome).toBe('joined');
    expect((await host.post(`/game-sessions/${privateSession.id}/invitations`, { userId: stranger.id })).status).toBe(403);
  });

  it('filters and searches the directory', async () => {
    const host = await registerUser(server, 'gsf_host');
    const viewer = await registerUser(server, 'gsf_viewer');
    await createSession(host, { gameName: 'Galaxy Racers', platform: 'PlayStation 5', region: 'Asia', maxParticipants: 2 });
    await createSession(host, { gameName: 'Dungeon Delve', platform: 'PC', region: 'Europe Central' });
    const byName = (await viewer.get('/game-sessions?q=galaxy')).body.sessions;
    expect(byName.map((s: { gameName: string }) => s.gameName)).toEqual(['Galaxy Racers']);
    const byPlatform = (await viewer.get(`/game-sessions?platform=${encodeURIComponent('PlayStation 5')}`)).body.sessions;
    expect(byPlatform.every((s: { platform: string }) => s.platform === 'PlayStation 5')).toBe(true);
    const byRegion = (await viewer.get('/game-sessions?region=europe')).body.sessions;
    expect(byRegion.map((s: { gameName: string }) => s.gameName)).toContain('Dungeon Delve');
    expect((await viewer.get('/game-sessions?platform=Atari')).status).toBe(400);
  });

  it('lets the host update status, transfers hosting when the host leaves, and closes sessions', async () => {
    const host = await registerUser(server, 'gsl_host');
    const player = await registerUser(server, 'gsl_player');
    const session = (await createSession(host)).body;
    await player.post(`/game-sessions/${session.id}/join`);

    expect((await player.patch(`/game-sessions/${session.id}`, { status: 'active' })).status).toBe(403);
    const active = await host.patch(`/game-sessions/${session.id}`, { status: 'active' });
    expect(active.body.status).toBe('active');
    expect(active.body.startedAt).not.toBeNull();
    expect((await host.patch(`/game-sessions/${session.id}`, { maxParticipants: 1 })).status).toBe(400);

    expect((await host.post(`/game-sessions/${session.id}/leave`)).status).toBe(204);
    const handedOver = await player.get(`/game-sessions/${session.id}`);
    expect(handedOver.body.host.id).toBe(player.id);
    expect(handedOver.body.viewerState).toBe('host');

    const closed = await player.post(`/game-sessions/${session.id}/close`);
    expect(closed.body.status).toBe('finished');
    const latecomer = await registerUser(server, 'gsl_late');
    expect((await latecomer.post(`/game-sessions/${session.id}/join`)).body.error.code).toBe('SESSION_CLOSED');
    expect((await latecomer.get('/game-sessions')).body.sessions.map((s: { id: string }) => s.id)).not.toContain(session.id);
  });

  it('only allows the host to remove participants', async () => {
    const host = await registerUser(server, 'gsk_host');
    const a = await registerUser(server, 'gsk_a');
    const b = await registerUser(server, 'gsk_b');
    const session = (await createSession(host)).body;
    await a.post(`/game-sessions/${session.id}/join`);
    await b.post(`/game-sessions/${session.id}/join`);
    expect((await a.delete(`/game-sessions/${session.id}/members/${b.id}`)).status).toBe(403);
    expect((await host.delete(`/game-sessions/${session.id}/members/${b.id}`)).status).toBe(204);
    expect((await b.get(`/game-sessions/${session.id}`)).body.viewerState).toBe('none');
  });
});
