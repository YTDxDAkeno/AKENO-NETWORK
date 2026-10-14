import { eq } from 'drizzle-orm';
import { afterAll, beforeAll, describe, expect, it } from 'vitest';
import WebSocket from 'ws';
import type {
  ApiErrorBody,
  DevicePairingStartResponse,
  DeviceScope,
  DeviceTokenResponse,
  FriendDto,
  GameActivityResponse,
  GameHeartbeatResponse,
} from '@akeno/shared';
import { devicePairings, gameActivitySessions } from '../src/database/schema/index.js';
import {
  RealtimeClient,
  connectRealtime,
  createTestServer,
  listen,
  makeFriends,
  registerUser,
  type ApiResponse,
  type TestClient,
  type TestServer,
} from './helpers.js';

let server: TestServer;
let url: string;

beforeAll(async () => {
  server = await createTestServer();
  url = await listen(server);
});

afterAll(async () => {
  await server.close();
});

/** A heartbeat is answered with the session on success and with the API's error body otherwise. */
type HeartbeatResult = GameHeartbeatResponse | ApiErrorBody;

function isApiError(body: unknown): body is ApiErrorBody {
  return typeof body === 'object' && body !== null && 'error' in body;
}

/** The body of an accepted heartbeat; fails the test with the server's answer otherwise. */
function accepted(response: ApiResponse<HeartbeatResult>): GameHeartbeatResponse {
  if (isApiError(response.body)) throw new Error(`Heartbeat refused: HTTP ${response.status} ${response.body.error.code}`);
  return response.body;
}

/** The error of a refused heartbeat; fails the test if it was accepted. */
function refusal(response: ApiResponse<HeartbeatResult>): ApiErrorBody['error'] {
  if (!isApiError(response.body)) throw new Error(`Heartbeat unexpectedly accepted: HTTP ${response.status}`);
  return response.body.error;
}

/** A paired device (bearer token, no cookies, no Origin). */
class Device {
  constructor(
    readonly token: string,
    readonly id: string,
  ) {}

  async request<T = any>(method: 'GET' | 'POST' | 'PUT' | 'PATCH' | 'DELETE', path: string, body?: unknown): Promise<ApiResponse<T>> {
    const raw = await server.app.inject({
      method,
      url: `/api/v1${path}`,
      headers: { authorization: `Bearer ${this.token}` },
      ...(body !== undefined ? { payload: body as Record<string, unknown> } : {}),
    });
    return { status: raw.statusCode, body: (raw.body ? JSON.parse(raw.body) : null) as T, raw };
  }

  heartbeat(titleId: string, titleName?: string) {
    return this.request<HeartbeatResult>('POST', '/activity/heartbeat', {
      titleId,
      platform: 'PS5',
      ...(titleName ? { titleName } : {}),
    });
  }

  stop() {
    return this.request<{ ended: number }>('POST', '/activity/stop', {});
  }

  async connect(): Promise<RealtimeClient> {
    const socket = new WebSocket(url, { headers: { Authorization: `Bearer ${this.token}` } });
    const realtime = new RealtimeClient(socket);
    await new Promise<void>((resolve, reject) => {
      socket.once('open', () => resolve());
      socket.once('error', reject);
    });
    await realtime.waitFor('hello');
    return realtime;
  }
}

async function pair(owner: TestClient, scopes?: DeviceScope[]): Promise<Device> {
  const start = await server.app.inject({
    method: 'POST',
    url: '/api/v1/devices/pairings',
    payload: { deviceName: 'PS5', platform: 'PlayStation 5 (homebrew)', clientVersion: '0.2.0' },
  });
  const started = JSON.parse(start.body) as DevicePairingStartResponse;
  const approved = await owner.post('/devices/pairings/approve', { userCode: started.userCode, ...(scopes ? { scopes } : {}) });
  expect(approved.status).toBe(204);
  await server.database.db.update(devicePairings).set({ lastPolledAt: new Date(Date.now() - 60_000) });
  const redeemed = await server.app.inject({ method: 'POST', url: '/api/v1/devices/pairings/token', payload: { deviceCode: started.deviceCode } });
  const token = JSON.parse(redeemed.body) as DeviceTokenResponse;
  expect(redeemed.statusCode).toBe(200);
  return new Device(token.accessToken, token.device.id);
}

async function sessionsOf(userId: string) {
  return server.database.db.select().from(gameActivitySessions).where(eq(gameActivitySessions.userId, userId));
}

/** Moves a session's heartbeat (and start) into the past, as if time had passed. */
async function age(sessionId: string, seconds: number): Promise<void> {
  const [row] = await server.database.db.select().from(gameActivitySessions).where(eq(gameActivitySessions.id, sessionId));
  await server.database.db
    .update(gameActivitySessions)
    .set({
      startedAt: new Date(row!.startedAt.getTime() - seconds * 1000),
      lastHeartbeatAt: new Date(row!.lastHeartbeatAt.getTime() - seconds * 1000),
    })
    .where(eq(gameActivitySessions.id, sessionId));
}

async function friendGame(viewer: TestClient, friendId: string) {
  const friends = await viewer.get<{ friends: FriendDto[] }>('/friends');
  return friends.body.friends.find((f) => f.id === friendId)?.presence.game ?? null;
}

describe('automatic game activity', () => {
  it('starts, continues and switches sessions reported by a paired device; friends see the game', async () => {
    const player = await registerUser(server, 'ga_player');
    const friend = await registerUser(server, 'ga_friend');
    await makeFriends(player, friend);
    const device = await pair(player);
    const online = await device.connect();
    const watcher = await connectRealtime(url, friend);

    const first = await device.heartbeat('PPSA01234', 'Racing Game');
    expect(first.status).toBe(200);
    expect(accepted(first).session.created).toBe(true);
    expect(accepted(first).heartbeatIntervalSeconds).toBe(60);
    const update = await watcher.waitFor('presence.updated', (e) => e.data.userId === player.id && e.data.presence.game !== null);
    expect(update.data.presence.game).toMatchObject({ titleId: 'PPSA01234', name: 'Racing Game', platform: 'PS5' });
    expect(await friendGame(friend, player.id)).toMatchObject({ titleId: 'PPSA01234', name: 'Racing Game' });
    await online.waitFor('activity.self', (e) => e.data.game?.titleId === 'PPSA01234');

    const again = await device.heartbeat('PPSA01234');
    expect(accepted(again).session).toMatchObject({ id: accepted(first).session.id, created: false });
    expect((await friendGame(friend, player.id))?.name).toBe('Racing Game'); // name kept without re-sending it

    const switched = await device.heartbeat('CUSA05678', 'Older Game');
    expect(accepted(switched).session.created).toBe(true);
    const rows = await sessionsOf(player.id);
    expect(rows.find((r) => r.titleId === 'PPSA01234')).toMatchObject({ endReason: 'switched' });
    expect(rows.find((r) => r.titleId === 'CUSA05678')).toMatchObject({ endedAt: null });
    expect((await friendGame(friend, player.id))?.titleId).toBe('CUSA05678');

    const stopped = await device.stop();
    expect(stopped.body.ended).toBe(1);
    await watcher.waitFor('presence.updated', (e) => e.data.userId === player.id && e.data.presence.game === null);
    expect(await friendGame(friend, player.id)).toBeNull();

    const history = await friend.get<GameActivityResponse>(`/users/${player.id}/activity`);
    expect(history.body.historyVisible).toBe(true);
    expect(history.body.current).toBeNull();
    expect(history.body.recentGames.map((g) => g.titleId)).toEqual(['CUSA05678', 'PPSA01234']);
    expect(history.body.recentSessions.map((s) => s.endReason)).toEqual(['stopped', 'switched']);
    expect(history.body.trackedSince).not.toBeNull();
    expect(history.body.trackingEnabled).toBeUndefined(); // only the owner sees this
    await online.close();
    await watcher.close();
  });

  it('only counts confirmed time: stale sessions end at their last heartbeat', async () => {
    const player = await registerUser(server, 'ga_stale');
    const device = await pair(player);
    const online = await device.connect();
    const started = await device.heartbeat('PPSA00001', 'Long Game');
    await age(accepted(started).session.id, 3600); // started an hour ago, last heartbeat an hour ago
    // Not yet swept, but already hidden: no heartbeat for longer than the stale limit.
    expect(server.ctx.activity.currentOf(player.id)).not.toBeNull(); // cache still holds the old heartbeat time
    expect(server.ctx.activity.currentOf(player.id, Date.now() + 200_000)).toBeNull();

    expect(await server.ctx.activity.endStale()).toBe(1);
    const [row] = await sessionsOf(player.id);
    expect(row!.endReason).toBe('stale');
    expect(row!.endedAt!.getTime()).toBe(row!.lastHeartbeatAt.getTime());
    const own = await player.get<GameActivityResponse>(`/users/${player.id}/activity`);
    expect(own.body.trackingEnabled).toBe(true);
    // Started and last confirmed in the same instant: zero seconds, not an hour.
    expect(own.body.recentGames[0]).toMatchObject({ titleId: 'PPSA00001', totalSeconds: 0, sessionCount: 1, playingNow: false });

    // After rest mode the game is reported again: that is a new session, not a continued one.
    const resumed = await device.heartbeat('PPSA00001');
    expect(accepted(resumed).session.created).toBe(true);
    await online.close();
  });

  it('a heartbeat after a long gap does not extend the old session', async () => {
    const player = await registerUser(server, 'ga_gap');
    const device = await pair(player);
    const first = await device.heartbeat('PPSA00002');
    await age(accepted(first).session.id, 600); // last heartbeat 10 minutes ago, not swept yet
    const next = await device.heartbeat('PPSA00002');
    expect(accepted(next).session.created).toBe(true);
    const rows = await sessionsOf(player.id);
    const old = rows.find((r) => r.id === accepted(first).session.id)!;
    expect(old.endReason).toBe('stale');
    expect(old.endedAt!.getTime()).toBe(old.lastHeartbeatAt.getTime());
  });

  it('two devices reporting the same game share one session; different games stay separate', async () => {
    const player = await registerUser(server, 'ga_multi');
    const a = await pair(player);
    const b = await pair(player);
    const fromA = await a.heartbeat('PPSA00003');
    const fromB = await b.heartbeat('PPSA00003');
    expect(accepted(fromB).session).toMatchObject({ id: accepted(fromA).session.id, created: false });
    expect((await sessionsOf(player.id)).filter((r) => r.endedAt === null)).toHaveLength(1);

    // A stop from the device that did not send the last heartbeat leaves the shared session running.
    expect((await a.stop()).body.ended).toBe(0);
    await b.heartbeat('PPSA00004');
    const active = (await sessionsOf(player.id)).filter((r) => r.endedAt === null);
    expect(active.map((r) => r.titleId).sort()).toEqual(['PPSA00004']);
    expect((await sessionsOf(player.id)).find((r) => r.titleId === 'PPSA00003')?.endReason).toBe('switched');
  });

  it('accepts reports only from paired devices with the activity permission and valid input', async () => {
    const player = await registerUser(server, 'ga_auth');
    const web = await player.post('/activity/heartbeat', { titleId: 'PPSA01234', platform: 'PS5' });
    expect(web.status).toBe(403);
    expect(web.body.error.code).toBe('DEVICE_ONLY');

    const limited = await pair(player, ['profile:read', 'realtime']);
    const denied = await limited.heartbeat('PPSA01234');
    expect(denied.status).toBe(403);
    expect(refusal(denied).code).toBe('DEVICE_SCOPE_DENIED');

    const device = await pair(player);
    for (const titleId of ['ppsa01234', 'PPSA1234', 'PPSA012345', '../../etc', 'NPXS40001x']) {
      expect((await device.heartbeat(titleId)).status).toBe(400);
    }
    expect((await device.request('POST', '/activity/heartbeat', { titleId: 'PPSA01234', platform: 'Xbox' })).status).toBe(400);
    const control = await device.heartbeat('PPSA01234', 'Name\nwith\u0007control');
    expect(control.status).toBe(200);
    expect((await sessionsOf(player.id))[0]!.titleName).toBe('Name withcontrol'); // newline -> space, BEL removed
    // Devices cannot manage history.
    expect((await device.request('DELETE', '/activity/history')).status).toBe(403);
  });

  it('respects game visibility, history visibility, invisible mode and blocks', async () => {
    const player = await registerUser(server, 'ga_private');
    const friend = await registerUser(server, 'ga_priv_friend');
    const stranger = await registerUser(server, 'ga_stranger');
    await makeFriends(player, friend);
    const device = await pair(player);
    const online = await device.connect();
    await device.heartbeat('PPSA00010', 'Secret Game');

    // Defaults: friends see the game and the history, strangers see neither.
    expect((await friend.get<GameActivityResponse>(`/users/${player.id}/activity`)).body.current?.titleId).toBe('PPSA00010');
    const asStranger = await stranger.get<GameActivityResponse>(`/users/${player.id}/activity`);
    expect(asStranger.body).toMatchObject({ current: null, historyVisible: false, recentGames: [] });
    expect((await stranger.get(`/users/${player.id}`)).body.presence.game).toBeNull();

    // Everyone may see the game, but nobody the history.
    await player.patch('/profile/privacy', { presenceVisibility: 'everyone', gameActivityVisibility: 'everyone', gameHistoryVisibility: 'nobody' });
    const open = await stranger.get<GameActivityResponse>(`/users/${player.id}/activity`);
    expect(open.body.current?.titleId).toBe('PPSA00010');
    expect(open.body.historyVisible).toBe(false);
    expect((await friend.get<GameActivityResponse>(`/users/${player.id}/activity`)).body.historyVisible).toBe(false);
    expect((await player.get<GameActivityResponse>(`/users/${player.id}/activity`)).body.historyVisible).toBe(true);

    // Game hidden from everyone, history for friends.
    await player.patch('/profile/privacy', { gameActivityVisibility: 'nobody', gameHistoryVisibility: 'friends' });
    expect(await friendGame(friend, player.id)).toBeNull();
    const friendView = await friend.get<GameActivityResponse>(`/users/${player.id}/activity`);
    expect(friendView.body.current).toBeNull();
    expect(friendView.body.recentGames).toEqual([]); // the running session would reveal the game
    expect((await player.get(`/users/${player.id}`)).body.presence.game?.titleId).toBe('PPSA00010'); // owner still sees it

    // Invisible: no game, and the running session is not part of the visible history.
    await player.patch('/profile/privacy', { gameActivityVisibility: 'friends' });
    await player.put('/presence', { status: 'invisible' });
    expect(await friendGame(friend, player.id)).toBeNull();
    expect((await friend.get<GameActivityResponse>(`/users/${player.id}/activity`)).body.recentGames).toEqual([]);
    await player.put('/presence', { status: 'online' });
    expect((await friendGame(friend, player.id))?.titleId).toBe('PPSA00010');

    // Blocking hides everything in both directions.
    await player.post('/blocks', { userId: stranger.id });
    expect((await stranger.get(`/users/${player.id}/activity`)).status).toBe(404);
    await online.close();
  });

  it('turning tracking off ends the running session and refuses new reports', async () => {
    const player = await registerUser(server, 'ga_optout');
    const device = await pair(player);
    await device.heartbeat('PPSA00020');
    const off = await player.patch('/profile/privacy', { gameActivityTracking: false });
    expect(off.body.privacy.gameActivityTracking).toBe(false);
    expect((await sessionsOf(player.id))[0]).toMatchObject({ endReason: 'tracking_disabled' });
    const refused = await device.heartbeat('PPSA00020');
    expect(refused.status).toBe(409);
    expect(refusal(refused).code).toBe('ACTIVITY_TRACKING_DISABLED');
    expect((await sessionsOf(player.id)).filter((r) => r.endedAt === null)).toHaveLength(0);
    await player.patch('/profile/privacy', { gameActivityTracking: true });
    expect((await device.heartbeat('PPSA00020')).status).toBe(200);
  });

  it('unpairing the device ends its session; the owner can clear the history', async () => {
    const player = await registerUser(server, 'ga_unpair');
    const device = await pair(player);
    await device.heartbeat('PPSA00030');
    expect((await player.delete(`/devices/${device.id}`)).status).toBe(204);
    const [row] = await sessionsOf(player.id);
    expect(row).toMatchObject({ endReason: 'device_revoked', lastDeviceId: null });
    expect(server.ctx.activity.currentOf(player.id)).toBeNull();

    const cleared = await player.delete<{ deleted: number }>('/activity/history');
    expect(cleared.body.deleted).toBe(1);
    expect((await player.get<GameActivityResponse>(`/users/${player.id}/activity`)).body).toMatchObject({ recentGames: [], trackedSince: null });
  });

  it('administrators name titles for everyone; names override device reports', async () => {
    const admin = await registerUser(server, 'ga_admin');
    await server.database.pool.query(`UPDATE users SET role = 'admin' WHERE id = $1`, [admin.id]);
    const player = await registerUser(server, 'ga_named');
    const device = await pair(player);
    await device.heartbeat('PPSA00040', 'Reported Name');
    expect((await player.put('/admin/game-titles/PPSA00040', { name: 'Not allowed' })).status).toBe(403);
    const set = await admin.put('/admin/game-titles/PPSA00040', { name: 'Curated Name' });
    expect(set.status).toBe(200);
    expect(server.ctx.activity.currentOf(player.id)?.name).toBe('Curated Name');
    expect((await admin.get('/admin/game-titles')).body.titles).toEqual([expect.objectContaining({ titleId: 'PPSA00040', name: 'Curated Name' })]);
    expect((await admin.put('/admin/game-titles/bad-id', { name: 'x' })).status).toBe(400);
    expect((await admin.delete('/admin/game-titles/PPSA00040')).status).toBe(204);
    expect(server.ctx.activity.currentOf(player.id)?.name).toBe('Reported Name');
  });

  it('restores running sessions after a server restart', async () => {
    const player = await registerUser(server, 'ga_restart');
    const device = await pair(player);
    await device.heartbeat('PPSA00050', 'Persistent Game');
    server = await server.restart();
    url = await listen(server);
    expect(server.ctx.activity.currentOf(player.id)).toMatchObject({ titleId: 'PPSA00050', name: 'Persistent Game' });
  });
});
