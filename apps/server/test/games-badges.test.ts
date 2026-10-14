import { eq } from 'drizzle-orm';
import { afterAll, beforeAll, describe, expect, it } from 'vitest';
import type {
  AuditEventDto,
  CurrentUser,
  DetectedTitleDto,
  DevicePairingStartResponse,
  DeviceTokenResponse,
  FriendDto,
  GameActivityResponse,
  GameDto,
  GameSessionDto,
  PublicProfile,
  UserBadgeAdminDto,
} from '@akeno/shared';
import { devicePairings, gameTitles, users } from '../src/database/schema/index.js';
import WebSocket from 'ws';
import { RealtimeClient, createTestServer, listen, login, makeFriends, registerUser, type TestClient, type TestServer } from './helpers.js';
import { testImage } from './images.js';

let server: TestServer;
let url: string;
const sockets: RealtimeClient[] = [];
let admin: TestClient;
let alice: TestClient;
let bob: TestClient;

async function pairDevice(owner: TestClient): Promise<string> {
  const start = await server.app.inject({
    method: 'POST',
    url: '/api/v1/devices/pairings',
    payload: { deviceName: 'PS5', platform: 'PlayStation 5 (homebrew)', clientVersion: '0.3.0' },
  });
  const started = JSON.parse(start.body) as DevicePairingStartResponse;
  expect((await owner.post('/devices/pairings/approve', { userCode: started.userCode })).status).toBe(204);
  await server.database.db.update(devicePairings).set({ lastPolledAt: new Date(Date.now() - 60_000) });
  const redeemed = await server.app.inject({ method: 'POST', url: '/api/v1/devices/pairings/token', payload: { deviceCode: started.deviceCode } });
  return (JSON.parse(redeemed.body) as DeviceTokenResponse).accessToken;
}

/** A paired console is online while its realtime connection is open (presence shows the game). */
async function connectDevice(token: string): Promise<void> {
  const socket = new WebSocket(url, { headers: { Authorization: `Bearer ${token}` } });
  const realtime = new RealtimeClient(socket);
  await new Promise<void>((resolve, reject) => {
    socket.once('open', () => resolve());
    socket.once('error', reject);
  });
  await realtime.waitFor('hello');
  sockets.push(realtime);
}

async function heartbeat(token: string, titleId: string, titleName?: string) {
  const response = await server.app.inject({
    method: 'POST',
    url: '/api/v1/activity/heartbeat',
    headers: { authorization: `Bearer ${token}` },
    payload: { titleId, platform: 'PS5', ...(titleName ? { titleName } : {}) },
  });
  expect(response.statusCode).toBe(200);
}

beforeAll(async () => {
  server = await createTestServer();
  url = await listen(server);
  const adminUser = await registerUser(server, 'catalog_admin');
  await server.database.db.update(users).set({ role: 'admin' }).where(eq(users.id, adminUser.id));
  admin = await login(server, 'catalog_admin');
  alice = await registerUser(server, 'alice_games');
  bob = await registerUser(server, 'bob_games');
  await makeFriends(alice, bob);
});

afterAll(async () => {
  for (const socket of sockets) await socket.close();
  await server.close();
});

describe('game catalogue and artwork', () => {
  let game: GameDto;

  it('lets administrators create a game with verified title IDs and upload artwork', async () => {
    const created = await admin.post<GameDto>('/admin/games', {
      name: 'Elden Ring',
      aliases: ['ELDEN RING™'],
      titleIds: ['PPSA04609', 'CUSA28863'],
      artworkSource: 'publisher_press_kit',
      artworkAttribution: '© Bandai Namco / FromSoftware (press kit)',
    });
    expect(created.status).toBe(201);
    expect(created.body.titleIds.map((t) => [t.titleId, t.platform])).toEqual([
      ['CUSA28863', 'PS4'],
      ['PPSA04609', 'PS5'],
    ]);
    game = created.body;

    const icon = await admin.upload<GameDto>(`/admin/games/${game.id}/artwork/icon`, await testImage(512, 512));
    expect(icon.status).toBe(200);
    // A square image uploaded as a cover is center-cropped to 2:3, never stretched.
    const cover = await admin.upload<GameDto>(`/admin/games/${game.id}/artwork/cover`, await testImage(600, 600, 'jpeg'), 'image/jpeg');
    expect(cover.status).toBe(200);
    game = cover.body;
    expect(game.artwork.icon).toMatch(/^[0-9a-f]{32}$/);
    expect(game.artwork.cover).toMatch(/^[0-9a-f]{32}$/);
    expect(game.artworkSource).toBe('publisher_press_kit');
    const coverLg = await admin.fetchRaw(`/media/${game.artwork.cover}/lg.webp`);
    const { default: sharp } = await import('sharp');
    const meta = await sharp(coverLg.rawPayload).metadata();
    expect([meta.width, meta.height]).toEqual([400, 600]);
  });

  it('shows the artwork automatically for a detected, mapped title (presence and history)', async () => {
    const token = await pairDevice(alice);
    await connectDevice(token);
    await heartbeat(token, 'PPSA04609', 'ELDEN RING');
    const friends = (await bob.get<{ friends: FriendDto[] }>('/friends')).body.friends;
    const playing = friends.find((friend) => friend.id === alice.id)!.presence.game!;
    expect(playing.name).toBe('Elden Ring');
    expect(playing.art).toEqual({ gameId: game.id, icon: game.artwork.icon, cover: game.artwork.cover, banner: null });
    const activity = (await bob.get<GameActivityResponse>(`/users/${alice.id}/activity`)).body;
    expect(activity.current?.art?.gameId).toBe(game.id);
    expect(activity.recentGames[0]?.art?.cover).toBe(game.artwork.cover);
    expect(activity.recentSessions[0]?.art?.icon).toBe(game.artwork.icon);
  });

  it('never attaches artwork to an unmapped title, even when the reported name is identical', async () => {
    const token = await pairDevice(bob);
    await connectDevice(token);
    await heartbeat(token, 'CUSA99999', 'Elden Ring');
    const friends = (await alice.get<{ friends: FriendDto[] }>('/friends')).body.friends;
    const playing = friends.find((friend) => friend.id === bob.id)!.presence.game!;
    expect(playing.titleId).toBe('CUSA99999');
    expect(playing.name).toBe('Elden Ring');
    expect(playing.art).toBeNull();
  });

  it('reports detected titles with their mapping status for the missing-artwork review', async () => {
    const report = (await admin.get<{ titles: DetectedTitleDto[] }>('/admin/games/detected-titles')).body.titles;
    const mapped = report.find((entry) => entry.titleId === 'PPSA04609');
    const unmapped = report.find((entry) => entry.titleId === 'CUSA99999');
    expect(mapped?.game).toMatchObject({ id: game.id, hasIcon: true, hasCover: true });
    expect(unmapped).toMatchObject({ game: null, reportedName: 'Elden Ring', platform: 'PS4', players: 1 });
  });

  it('maps a title to a game after review, updating everyone who is playing it', async () => {
    const mapped = await admin.put<GameDto>(`/admin/games/${game.id}/titles/CUSA99999`, {});
    expect(mapped.status).toBe(200);
    const friends = (await alice.get<{ friends: FriendDto[] }>('/friends')).body.friends;
    expect(friends.find((friend) => friend.id === bob.id)!.presence.game!.art?.gameId).toBe(game.id);
    expect((await admin.delete(`/admin/games/${game.id}/titles/CUSA99999`)).status).toBe(204);
    const after = (await alice.get<{ friends: FriendDto[] }>('/friends')).body.friends;
    expect(after.find((friend) => friend.id === bob.id)!.presence.game!.art).toBeNull();
  });

  it('matches favorite games and game sessions only by exact name or mapped title ID', async () => {
    await alice.patch('/profile', { favoriteGames: ['elden ring', 'Elden Ring Nightreign', 'Elden Ring™ '] });
    const profile = (await bob.get<PublicProfile>('/users/alice_games')).body;
    const details = Object.fromEntries(profile.favoriteGameDetails.map((entry) => [entry.name, entry.art?.gameId ?? null]));
    expect(details['elden ring']).toBe(game.id);
    expect(details['Elden Ring Nightreign']).toBeNull();

    const byId = await alice.post<GameSessionDto>('/game-sessions', {
      gameName: 'Co-op night',
      gameIdentifier: 'PPSA04609',
      visibility: 'friends',
      joinMode: 'open',
      maxParticipants: 4,
    });
    expect(byId.status).toBe(201);
    expect(byId.body.art?.gameId).toBe(game.id);
    const byName = await bob.post<GameSessionDto>('/game-sessions', { gameName: 'Eldenring', visibility: 'friends', joinMode: 'open', maxParticipants: 4 });
    expect(byName.body.art).toBeNull();
  });

  it('keeps the Alpha 0.2 curated-name API working (and in step with game_titles)', async () => {
    const named = await admin.put('/admin/game-titles/PPSA01111', { name: 'Atelier Yumia' });
    expect(named.status).toBe(200);
    const list = (await admin.get<{ titles: Array<{ titleId: string; name: string }> }>('/admin/game-titles')).body.titles;
    expect(list).toContainEqual(expect.objectContaining({ titleId: 'PPSA01111', name: 'Atelier Yumia' }));
    const [legacy] = await server.database.db.select().from(gameTitles).where(eq(gameTitles.titleId, 'PPSA01111'));
    expect(legacy?.name).toBe('Atelier Yumia');
    expect((await admin.delete('/admin/game-titles/PPSA01111')).status).toBe(204);
    const games = (await admin.get<{ games: GameDto[] }>('/admin/games?q=Atelier')).body.games;
    expect(games).toHaveLength(0);
  });

  it('removes artwork files when artwork or a game is deleted, and audits catalogue changes', async () => {
    const icon = game.artwork.icon!;
    const removed = await admin.delete<GameDto>(`/admin/games/${game.id}/artwork/icon`);
    expect(removed.body.artwork.icon).toBeNull();
    expect(await server.ctx.media.exists(icon, 'md.webp')).toBe(false);
    const cover = game.artwork.cover!;
    expect((await admin.delete(`/admin/games/${game.id}`)).status).toBe(204);
    expect(await server.ctx.media.exists(cover, 'md.webp')).toBe(false);
    const audit = (await admin.get<{ events: AuditEventDto[] }>('/admin/audit-events?limit=100')).body.events.map((event) => event.action);
    for (const action of ['admin.game_created', 'admin.game_artwork_set', 'admin.game_title_mapped', 'admin.game_artwork_removed', 'admin.game_deleted']) {
      expect(audit).toContain(action);
    }
  });

  it('is only available to administrators', async () => {
    expect((await alice.get('/admin/games')).status).toBe(403);
    expect((await alice.post('/admin/games', { name: 'Hack' })).status).toBe(403);
    const upload = await alice.upload(`/admin/games/${crypto.randomUUID()}/artwork/icon`, await testImage(300, 300));
    expect(upload.status).toBe(403);
  });
});

describe('badges', () => {
  it('derives the Administrator badge from the role and lets nobody grant it', async () => {
    const me = (await admin.get<CurrentUser>('/users/me')).body;
    expect(me.badges.map((badge) => badge.id)).toContain('administrator');
    const response = await admin.post(`/admin/users/${alice.id}/badges`, { badgeId: 'administrator' });
    expect(response.status).toBe(400);
  });

  it('lets administrators grant and revoke badges, with audit records', async () => {
    const granted = await admin.post<{ badges: UserBadgeAdminDto[] }>(`/admin/users/${alice.id}/badges`, { badgeId: 'ps5_alpha_tester', note: 'Hardware test 12.20' });
    expect(granted.status).toBe(201);
    expect(granted.body.badges[0]).toMatchObject({ badgeId: 'ps5_alpha_tester', note: 'Hardware test 12.20', grantedBy: { onlineId: 'catalog_admin' } });
    expect((await admin.post(`/admin/users/${alice.id}/badges`, { badgeId: 'ps5_alpha_tester' })).status).toBe(409);
    const profile = (await bob.get<PublicProfile>('/users/alice_games')).body;
    expect(profile.badges.map((badge) => badge.id)).toEqual(['ps5_alpha_tester']);
    const audit = (await admin.get<{ events: AuditEventDto[] }>('/admin/audit-events?limit=50')).body.events;
    expect(audit.find((event) => event.action === 'admin.badge_granted')?.metadata).toMatchObject({ badgeId: 'ps5_alpha_tester' });
  });

  it('refuses authority badges an administrator tries to give themselves', async () => {
    const self = await admin.post(`/admin/users/${admin.id}/badges`, { badgeId: 'founder' });
    expect(self.status).toBe(403);
    // Non-authority badges are fine, and the operator CLI path (actor null) may grant anything.
    expect((await admin.post(`/admin/users/${admin.id}/badges`, { badgeId: 'beta_tester' })).status).toBe(201);
    await server.ctx.badges.grant(null, admin.id, 'founder', 'Server operator', null);
    const me = (await admin.get<CurrentUser>('/users/me')).body;
    expect(me.badges.map((badge) => badge.id)).toEqual(['founder', 'administrator', 'beta_tester']);
  });

  it('never lets users grant badges to themselves', async () => {
    expect((await alice.post(`/admin/users/${alice.id}/badges`, { badgeId: 'founder' })).status).toBe(403);
    // The appearance endpoint only selects among earned badges.
    const unearned = await alice.patch('/profile/appearance', { featuredBadges: ['founder'] });
    expect(unearned.status).toBe(400);
    const me = (await alice.get<CurrentUser>('/users/me')).body;
    expect(me.badges.map((badge) => badge.id)).toEqual(['ps5_alpha_tester']);
  });

  it('features earned badges and drops revoked ones from the selection', async () => {
    await admin.post(`/admin/users/${alice.id}/badges`, { badgeId: 'early_supporter' });
    const featured = await alice.patch<CurrentUser>('/profile/appearance', { featuredBadges: ['early_supporter', 'ps5_alpha_tester'] });
    expect(featured.status).toBe(200);
    expect(featured.body.profile.featuredBadges).toEqual(['early_supporter', 'ps5_alpha_tester']);
    expect((await alice.patch('/profile/appearance', { featuredBadges: ['early_supporter', 'early_supporter'] })).status).toBe(400);
    const profile = (await bob.get<PublicProfile>('/users/alice_games')).body;
    expect(profile.featuredBadges).toEqual(['early_supporter', 'ps5_alpha_tester']);
    expect((await admin.delete(`/admin/users/${alice.id}/badges/early_supporter`)).status).toBe(204);
    const after = (await bob.get<PublicProfile>('/users/alice_games')).body;
    expect(after.featuredBadges).toEqual(['ps5_alpha_tester']);
    expect(after.badges.map((badge) => badge.id)).toEqual(['ps5_alpha_tester']);
  });
});
