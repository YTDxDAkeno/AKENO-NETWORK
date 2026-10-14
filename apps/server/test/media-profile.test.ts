import { mkdirSync, utimesSync, writeFileSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join } from 'node:path';
import { randomBytes } from 'node:crypto';
import { eq } from 'drizzle-orm';
import sharp from 'sharp';
import { afterAll, beforeAll, describe, expect, it } from 'vitest';
import { MEDIA_LIMITS, mediaUrl, type CurrentUser, type FriendDto, type PublicProfile } from '@akeno/shared';
import { mediaObjects } from '../src/database/schema/index.js';
import { TEST_ORIGIN, createTestServer, makeFriends, registerUser, type TestClient, type TestServer } from './helpers.js';
import { animatedGif, hugePngHeader, testImage } from './images.js';

let server: TestServer;
let alice: TestClient;
let bob: TestClient;

beforeAll(async () => {
  server = await createTestServer();
  alice = await registerUser(server, 'alice_media');
  bob = await registerUser(server, 'bob_media');
  await makeFriends(alice, bob);
});

afterAll(async () => {
  await server.close();
});

async function fetchMedia(client: TestClient, mediaId: string, variant: 'lg' | 'md' | 'sm' | 'fallback' = 'md') {
  return client.fetchRaw(mediaUrl(mediaId, variant));
}

describe('profile picture uploads', () => {
  it('stores a valid picture as normalized square variants and serves them to signed-in users', async () => {
    const response = await alice.upload<CurrentUser>('/profile/avatar', await testImage(800, 800, 'png'));
    expect(response.status).toBe(200);
    const mediaId = response.body.profile.avatarImage!;
    expect(mediaId).toMatch(/^[0-9a-f]{32}$/);

    const md = await fetchMedia(bob, mediaId, 'md');
    expect(md.statusCode).toBe(200);
    expect(md.headers['content-type']).toBe('image/webp');
    expect(md.headers['cache-control']).toBe('private, max-age=31536000, immutable');
    expect(md.headers['x-content-type-options']).toBe('nosniff');
    const mdInfo = await sharp(md.rawPayload).metadata();
    expect([mdInfo.format, mdInfo.width, mdInfo.height]).toEqual(['webp', 256, 256]);

    const lg = await sharp((await fetchMedia(bob, mediaId, 'lg')).rawPayload).metadata();
    expect([lg.width, lg.height]).toEqual([512, 512]);
    const fallback = await fetchMedia(bob, mediaId, 'fallback');
    expect(fallback.headers['content-type']).toBe('image/jpeg');
    expect((await sharp(fallback.rawPayload).metadata()).format).toBe('jpeg');

    // Conditional requests are answered without a body.
    const cached = await bob.fetchRaw(mediaUrl(mediaId, 'md'), { 'if-none-match': md.headers.etag as string });
    expect(cached.statusCode).toBe(304);
  });

  it('shows the uploaded picture to friends wherever user summaries appear', async () => {
    const me = (await alice.get<CurrentUser>('/users/me')).body;
    const friends = (await bob.get<{ friends: FriendDto[] }>('/friends')).body.friends;
    expect(friends.find((friend) => friend.id === alice.id)?.avatarImage).toBe(me.profile.avatarImage);
    const profile = (await bob.get<PublicProfile>('/users/alice_media')).body;
    expect(profile.user.avatarImage).toBe(me.profile.avatarImage);
    // The preset stays available as the fallback.
    expect(profile.user.avatar).toBe(me.profile.avatar);
  });

  it('center-crops non-square pictures and never enlarges small ones', async () => {
    const response = await alice.upload<CurrentUser>('/profile/avatar', await testImage(600, 300, 'jpeg'));
    expect(response.status).toBe(200);
    const id = response.body.profile.avatarImage!;
    const lg = await sharp((await fetchMedia(alice, id, 'lg')).rawPayload).metadata();
    expect([lg.width, lg.height]).toEqual([300, 300]);
  });

  it('applies EXIF orientation and strips every metadata block (GPS, camera, comments)', async () => {
    const jpeg = await testImage(400, 400, 'jpeg', {
      exif: { IFD0: { Make: 'SecretCam', Model: 'X1', Copyright: 'private' }, IFD3: { GPSLatitudeRef: 'N', GPSLatitude: '48/1 8/1 0/1' } },
    });
    expect((await sharp(jpeg).metadata()).exif).toBeDefined();
    const response = await alice.upload<CurrentUser>('/profile/avatar', jpeg, 'image/jpeg');
    expect(response.status).toBe(200);
    for (const variant of ['lg', 'md', 'sm', 'fallback'] as const) {
      const payload = (await fetchMedia(alice, response.body.profile.avatarImage!, variant)).rawPayload;
      const meta = await sharp(payload).metadata();
      expect(meta.exif, variant).toBeUndefined();
      expect(meta.xmp, variant).toBeUndefined();
      expect(meta.icc, variant).toBeUndefined();
      expect(payload.includes(Buffer.from('SecretCam')), variant).toBe(false);
    }

    // A tall photo stored sideways (orientation 6) becomes a wide banner after rotation; without
    // applying the orientation it would be too small after the 3:1 crop and be refused.
    const sideways = await testImage(400, 1800, 'jpeg', { orientation: 6 });
    const banner = await alice.upload<CurrentUser>('/profile/banner', sideways, 'image/jpeg');
    expect(banner.status).toBe(200);
    const lg = await sharp((await fetchMedia(alice, banner.body.profile.appearance.bannerImage!, 'lg')).rawPayload).metadata();
    expect([lg.width, lg.height]).toEqual([1200, 400]);
  });

  it('turns animated GIFs into still images', async () => {
    const response = await alice.upload<CurrentUser>('/profile/avatar', await animatedGif(300, 300), 'image/gif');
    expect(response.status).toBe(200);
    const meta = await sharp((await fetchMedia(alice, response.body.profile.avatarImage!, 'md')).rawPayload, { animated: true }).metadata();
    expect(meta.pages ?? 1).toBe(1);
  });

  it('rejects files that are not supported images, whatever their name or declared type', async () => {
    const cases: Array<[string, Buffer, string]> = [
      ['text', Buffer.from('hello world, this is not an image at all'), 'image/png'],
      ['svg', Buffer.from('<svg xmlns="http://www.w3.org/2000/svg"><script>alert(1)</script></svg>'), 'image/png'],
      ['html', Buffer.from('<!doctype html><html><body><script>alert(1)</script></body></html>'), 'image/jpeg'],
      ['pdf', Buffer.from('%PDF-1.7\n1 0 obj <<>> endobj\ntrailer <<>>\n%%EOF'), 'application/octet-stream'],
      ['tiff', Buffer.concat([Buffer.from([0x49, 0x49, 0x2a, 0x00]), randomBytes(64)]), 'image/webp'],
      ['elf', Buffer.concat([Buffer.from([0x7f, 0x45, 0x4c, 0x46]), randomBytes(64)]), 'image/png'],
    ];
    for (const [name, data, type] of cases) {
      const response = await alice.upload('/profile/avatar', data, type);
      expect(response.status, name).toBe(422);
      expect(response.body.error.code, name).toBe('INVALID_IMAGE');
    }
  });

  it('rejects malformed, truncated and mislabelled images', async () => {
    const png = await testImage(400, 400, 'png');
    const truncated = png.subarray(0, Math.floor(png.length / 3));
    expect((await alice.upload('/profile/avatar', truncated)).status).toBe(422);
    const jpeg = await testImage(400, 400, 'jpeg');
    const corrupted = Buffer.from(jpeg);
    corrupted.fill(0x41, 200, 1200);
    expect((await alice.upload('/profile/avatar', corrupted, 'image/jpeg')).status).toBe(422);
    // PNG signature followed by garbage.
    const fake = Buffer.concat([png.subarray(0, 8), randomBytes(500)]);
    expect((await alice.upload('/profile/avatar', fake)).status).toBe(422);
  });

  it('refuses decompression-bomb headers and images that are too small', async () => {
    const bomb = await alice.upload('/profile/avatar', hugePngHeader(30_000, 30_000));
    expect(bomb.status).toBe(422);
    expect(bomb.body.error.message).toMatch(/pixels|read/i);
    const tiny = await alice.upload('/profile/avatar', await testImage(64, 64));
    expect(tiny.status).toBe(422);
    expect(tiny.body.error.message).toMatch(/too small/);
    const thinBanner = await alice.upload('/profile/banner', await testImage(500, 500));
    expect(thinBanner.status).toBe(422);
  });

  it('rejects oversized uploads and unsupported content types', async () => {
    const big = Buffer.alloc(MEDIA_LIMITS.uploadMaxBytes + 1024, 0x41);
    const response = await alice.upload('/profile/avatar', big);
    expect(response.status).toBe(413);
    const wrongType = await alice.upload('/profile/avatar', await testImage(300, 300), 'text/plain');
    expect(wrongType.status).toBe(415);
    // JSON endpoints do not accept image bodies at all.
    const json = await alice.upload('/profile', await testImage(300, 300), 'image/png', { method: 'PUT' });
    expect([404, 415]).toContain(json.status);
  });

  it('requires a signed-in browser session with a CSRF token', async () => {
    const image = await testImage(300, 300);
    const anonymous = await server.app.inject({
      method: 'PUT',
      url: '/api/v1/profile/avatar',
      headers: { 'content-type': 'image/png', origin: TEST_ORIGIN },
      payload: image,
    });
    expect(anonymous.statusCode).toBe(401);
    // Without an allowed Origin the request is refused even earlier.
    const foreign = await server.app.inject({ method: 'PUT', url: '/api/v1/profile/avatar', headers: { 'content-type': 'image/png' }, payload: image });
    expect(foreign.statusCode).toBe(403);
    const noCsrf = await alice.upload('/profile/avatar', image, 'image/png', { csrf: false });
    expect(noCsrf.status).toBe(403);
    expect(noCsrf.body.error.code).toBe('CSRF_FAILED');
  });

  it('replaces and removes pictures, deleting the files of the old ones', async () => {
    const first = (await alice.upload<CurrentUser>('/profile/avatar', await testImage(300, 300))).body.profile.avatarImage!;
    const second = (await alice.upload<CurrentUser>('/profile/avatar', await testImage(320, 320))).body.profile.avatarImage!;
    expect(second).not.toBe(first);
    expect((await fetchMedia(alice, first)).statusCode).toBe(404);
    expect((await fetchMedia(alice, second)).statusCode).toBe(200);
    const [old] = await server.database.db.select().from(mediaObjects).where(eq(mediaObjects.id, first));
    expect(old?.deletedAt).not.toBeNull();

    const removed = await alice.delete<CurrentUser>('/profile/avatar');
    expect(removed.status).toBe(200);
    expect(removed.body.profile.avatarImage).toBeNull();
    expect((await fetchMedia(alice, second)).statusCode).toBe(404);
  });
});

describe('media access', () => {
  it('requires authentication and only serves valid references', async () => {
    const id = (await bob.upload<CurrentUser>('/profile/avatar', await testImage(300, 300))).body.profile.avatarImage!;
    const anonymous = await server.app.inject({ method: 'GET', url: mediaUrl(id) });
    expect(anonymous.statusCode).toBe(401);
    for (const path of [
      `/media/${id}/../../../etc/passwd`,
      `/media/${id}/md.png`,
      `/media/${id}/%2e%2e%2fmd.webp`,
      `/media/${id.toUpperCase()}/md.webp`,
      `/media/../objects/${id}/md.webp`,
      `/media/${'0'.repeat(32)}/md.webp`,
    ]) {
      const response = await alice.fetchRaw(path);
      expect(response.statusCode, path).toBe(404);
      expect(response.headers['content-type'], path).toMatch(/json/);
    }
  });

  it('keeps media across a server restart', async () => {
    const id = (await bob.upload<CurrentUser>('/profile/avatar', await testImage(300, 300))).body.profile.avatarImage!;
    const restarted = await server.restart();
    server = restarted;
    const response = await server.app.inject({ method: 'GET', url: mediaUrl(id), headers: { cookie: bob.cookie! } });
    expect(response.statusCode).toBe(200);
    // Clients created before the restart talk to the old app instance; rebind them.
    Object.assign(alice, { app: server.app });
    Object.assign(bob, { app: server.app });
  });
});

describe('quotas and storage limits', () => {
  it('limits profile uploads per user per day', async () => {
    const carol = await registerUser(server, 'carol_media');
    await server.database.db.insert(mediaObjects).values(
      Array.from({ length: MEDIA_LIMITS.dailyUploadsPerUser }, () => ({
        id: randomBytes(16).toString('hex'),
        kind: 'avatar' as const,
        ownerId: carol.id,
        width: 1,
        height: 1,
        variants: [],
        totalBytes: 0,
        sourceSha256: '0'.repeat(64),
        deletedAt: new Date(),
      })),
    );
    const response = await carol.upload('/profile/avatar', await testImage(300, 300));
    expect(response.status).toBe(429);
    expect(response.body.error.code).toBe('MEDIA_QUOTA_EXCEEDED');
  });

  it('refuses uploads when the storage limit is reached, without affecting other features', async () => {
    const limited = await createTestServer({ MEDIA_MAX_TOTAL_MB: '16' });
    try {
      const dave = await registerUser(limited, 'dave_media');
      await limited.database.db.insert(mediaObjects).values({
        id: randomBytes(16).toString('hex'),
        kind: 'banner',
        ownerId: null,
        width: 1,
        height: 1,
        variants: [],
        totalBytes: 16 * 1024 * 1024,
        sourceSha256: '0'.repeat(64),
      });
      const response = await dave.upload('/profile/avatar', await testImage(300, 300));
      expect(response.status).toBe(507);
      expect(response.body.error.code).toBe('MEDIA_STORAGE_FULL');
      expect((await dave.patch('/profile', { statusMessage: 'still works' })).status).toBe(200);
    } finally {
      await limited.close();
    }
  });

  it('disables only uploads when the media directory is not writable', async () => {
    const blocker = join(tmpdir(), `akeno-media-blocker-${randomBytes(4).toString('hex')}`);
    writeFileSync(blocker, 'not a directory');
    const broken = await createTestServer({ MEDIA_DIR: join(blocker, 'media') });
    try {
      const erin = await registerUser(broken, 'erin_media');
      const frank = await registerUser(broken, 'frank_media');
      await makeFriends(erin, frank);
      const upload = await erin.upload('/profile/avatar', await testImage(300, 300));
      expect(upload.status).toBe(503);
      expect(upload.body.error.code).toBe('MEDIA_UNAVAILABLE');
      const conversation = await erin.post('/conversations', { userId: frank.id });
      expect(conversation.status).toBeLessThan(300);
      const sent = await erin.post(`/conversations/${conversation.body.id}/messages`, { body: 'messaging still works', clientMessageId: crypto.randomUUID() });
      expect(sent.status).toBe(201);
    } finally {
      await broken.close();
    }
  });
});

describe('maintenance', () => {
  it('removes orphaned files and purges old soft-deleted rows, keeping live media', async () => {
    const live = (await bob.upload<CurrentUser>('/profile/avatar', await testImage(300, 300))).body.profile.avatarImage!;
    const orphan = randomBytes(16).toString('hex');
    const orphanDir = join(server.ctx.config.media.dir, 'objects', orphan.slice(0, 2), orphan);
    mkdirSync(orphanDir, { recursive: true });
    writeFileSync(join(orphanDir, 'md.webp'), 'x');
    const old = new Date(Date.now() - 3 * 60 * 60 * 1000);
    utimesSync(orphanDir, old, old);
    const purgeable = randomBytes(16).toString('hex');
    await server.database.db.insert(mediaObjects).values({
      id: purgeable,
      kind: 'avatar',
      ownerId: bob.id,
      width: 1,
      height: 1,
      variants: [],
      totalBytes: 10,
      sourceSha256: '0'.repeat(64),
      createdAt: new Date(Date.now() - 4 * 24 * 3600 * 1000),
      deletedAt: new Date(Date.now() - 3 * 24 * 3600 * 1000),
    });

    const result = await server.ctx.media.maintenance();
    expect(result.orphans).toBeGreaterThanOrEqual(1);
    expect(result.purged).toBeGreaterThanOrEqual(1);
    expect(await server.ctx.media.exists(orphan, 'md.webp')).toBe(false);
    expect(await server.ctx.media.exists(live, 'md.webp')).toBe(true);
    expect(await server.database.db.select().from(mediaObjects).where(eq(mediaObjects.id, purgeable))).toHaveLength(0);
  });

  it('deletes uploaded pictures and banners together with the account', async () => {
    const gina = await registerUser(server, 'gina_media');
    const avatar = (await gina.upload<CurrentUser>('/profile/avatar', await testImage(300, 300))).body.profile.avatarImage!;
    const banner = (await gina.upload<CurrentUser>('/profile/banner', await testImage(900, 300))).body.profile.appearance.bannerImage!;
    const deleted = await gina.delete('/users/me', { password: 'correct-horse-battery-staple', confirmation: 'DELETE' });
    expect(deleted.status).toBe(204);
    expect(await server.ctx.media.exists(avatar, 'md.webp')).toBe(false);
    expect(await server.ctx.media.exists(banner, 'md.webp')).toBe(false);
  });
});

describe('profile appearance', () => {
  it('saves theme, custom accent and banner overlay, and validates colours', async () => {
    const saved = await alice.patch<CurrentUser>('/profile/appearance', { theme: 'custom', accentColor: '#FF8800', bannerOverlay: false });
    expect(saved.status).toBe(200);
    expect(saved.body.profile.appearance).toMatchObject({ theme: 'custom', accentColor: '#ff8800', bannerOverlay: false });
    expect((await alice.patch('/profile/appearance', { accentColor: 'red' })).status).toBe(400);
    expect((await alice.patch('/profile/appearance', { accentColor: '#ff88' })).status).toBe(400);
    expect((await alice.patch('/profile/appearance', { theme: 'neon' })).status).toBe(400);
    const preset = await alice.patch<CurrentUser>('/profile/appearance', { theme: 'sakura' });
    // A preset theme ignores the stored custom accent.
    expect(preset.body.profile.appearance).toMatchObject({ theme: 'sakura', accentColor: null });
    const profile = (await bob.get<PublicProfile>('/users/alice_media')).body;
    expect(profile.appearance.theme).toBe('sakura');
  });

  it('hides the banner from viewers who may not see profile details', async () => {
    const banner = (await alice.upload<CurrentUser>('/profile/banner', await testImage(1500, 500))).body.profile.appearance.bannerImage;
    expect(banner).toBeTruthy();
    const stranger = await registerUser(server, 'stranger_media');
    expect((await stranger.get<PublicProfile>('/users/alice_media')).body.appearance.bannerImage).toBe(banner);
    await alice.patch('/profile/privacy', { profileVisibility: 'friends' });
    const restricted = (await stranger.get<PublicProfile>('/users/alice_media')).body;
    expect(restricted.restricted).toBe(true);
    expect(restricted.appearance.bannerImage).toBeNull();
    expect((await bob.get<PublicProfile>('/users/alice_media')).body.appearance.bannerImage).toBe(banner);
    await alice.patch('/profile/privacy', { profileVisibility: 'everyone' });
  });

  it('keeps preset avatars working for existing users', async () => {
    const harry = await registerUser(server, 'harry_media');
    const updated = await harry.patch<CurrentUser>('/profile', { avatar: 'ember' });
    expect(updated.body.profile).toMatchObject({ avatar: 'ember', avatarImage: null });
    expect(updated.body.profile.appearance).toEqual({ theme: 'default', accentColor: null, bannerImage: null, bannerOverlay: true });
  });
});
