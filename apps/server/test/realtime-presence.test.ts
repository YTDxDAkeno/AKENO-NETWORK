import { eq } from 'drizzle-orm';
import { afterAll, beforeAll, describe, expect, it } from 'vitest';
import { REALTIME_CLOSE_CODES } from '@akeno/shared';
import { users } from '../src/database/schema/index.js';
import { computePresenceView, effectiveSelfStatus, type PresenceSubject } from '../src/realtime/presence-rules.js';
import {
  connectRealtime,
  createTestServer,
  listen,
  login,
  makeFriends,
  registerUser,
  sleep,
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

describe('presence rules', () => {
  const base: PresenceSubject = {
    connected: true,
    allIdle: false,
    preference: 'online',
    presenceVisibility: 'friends',
    lastSeenVisibility: 'friends',
    lastSeenAt: new Date('2026-01-01T00:00:00Z'),
    activity: 'Playing a puzzle game',
  };
  const friend = { isSelf: false, isFriend: true, blocked: false };
  const stranger = { isSelf: false, isFriend: false, blocked: false };

  it('derives the effective status from connection, preference and idleness', () => {
    expect(effectiveSelfStatus({ ...base, connected: false })).toBe('offline');
    expect(effectiveSelfStatus(base)).toBe('online');
    expect(effectiveSelfStatus({ ...base, allIdle: true })).toBe('away');
    expect(effectiveSelfStatus({ ...base, preference: 'dnd', allIdle: true })).toBe('dnd');
    expect(effectiveSelfStatus({ ...base, preference: 'invisible' })).toBe('invisible');
  });

  it('applies audience rules', () => {
    expect(computePresenceView(base, friend).status).toBe('online');
    expect(computePresenceView(base, stranger)).toEqual({ status: 'offline', lastSeenAt: null, activity: null });
    expect(computePresenceView({ ...base, presenceVisibility: 'everyone' }, stranger).status).toBe('online');
    expect(computePresenceView({ ...base, presenceVisibility: 'nobody' }, friend).status).toBe('offline');
    expect(computePresenceView(base, { ...friend, blocked: true }).status).toBe('offline');
  });

  it('never leaks invisible users and only reveals last seen when allowed', () => {
    const invisible = computePresenceView({ ...base, preference: 'invisible' }, friend);
    expect(invisible.status).toBe('offline');
    expect(invisible.activity).toBeNull();
    const offline = computePresenceView({ ...base, connected: false }, friend);
    expect(offline.lastSeenAt).toBe('2026-01-01T00:00:00.000Z');
    const hiddenLastSeen = computePresenceView({ ...base, connected: false, lastSeenVisibility: 'nobody' }, friend);
    expect(hiddenLastSeen.lastSeenAt).toBeNull();
    expect(computePresenceView(base, friend).lastSeenAt).toBeNull(); // online users show no last seen
  });
});

describe('websocket gateway', () => {
  it('rejects connections without a session or from foreign origins', async () => {
    const user = await registerUser(server, 'ws_reject');
    await expect(connectRealtime(url, user, { cookie: null })).rejects.toMatchObject({ status: 401 });
    await expect(connectRealtime(url, user, { origin: 'https://evil.example' })).rejects.toMatchObject({ status: 403 });
    await expect(connectRealtime(url, user, { origin: null })).rejects.toMatchObject({ status: 403 });
  });

  it('greets authenticated clients and answers pings', async () => {
    const user = await registerUser(server, 'ws_hello');
    const realtime = await connectRealtime(url, user);
    const hello = await realtime.waitFor('hello');
    expect(hello.data.userId).toBe(user.id);
    expect(hello.v).toBe(1);
    realtime.send({ v: 1, type: 'ping' });
    await realtime.waitFor('pong');
    await realtime.close();
  });

  it('closes the connection on protocol violations', async () => {
    const user = await registerUser(server, 'ws_protocol');
    const realtime = await connectRealtime(url, user);
    realtime.send({ v: 1, type: 'impersonate', data: { userId: 'someone-else' } });
    expect(await realtime.waitForClose()).toBe(REALTIME_CLOSE_CODES.protocolError);
  });

  it('closes sockets when their session is revoked', async () => {
    const user = await registerUser(server, 'ws_revoke');
    const realtime = await connectRealtime(url, user);
    await user.post('/auth/logout');
    const revoked = await realtime.waitFor('session.revoked');
    expect(revoked.data.reason).toMatch(/signed out/i);
    expect(await realtime.waitForClose()).toBe(REALTIME_CLOSE_CODES.sessionRevoked);
  });
});

describe('realtime presence', () => {
  it('broadcasts online/offline to friends and keeps users online while any device is connected', async () => {
    const alice = await registerUser(server, 'pr_alice');
    const bob = await registerUser(server, 'pr_bob');
    await makeFriends(alice, bob);
    const bobSocket = await connectRealtime(url, bob);

    const aliceDesktop = await connectRealtime(url, alice);
    await bobSocket.waitFor('presence.updated', (event) => event.data.userId === alice.id && event.data.presence.status === 'online');
    const friends = await bob.get('/friends');
    expect(friends.body.friends[0].presence.status).toBe('online');

    const alicePhoneClient = await login(server, 'pr_alice');
    const alicePhone = await connectRealtime(url, alicePhoneClient);
    bobSocket.clear();
    await aliceDesktop.close();
    await sleep(300);
    expect(
      bobSocket.events.some(
        (event) => event.type === 'presence.updated' && event.data.userId === alice.id && event.data.presence.status === 'offline',
      ),
    ).toBe(false);
    expect((await bob.get('/friends')).body.friends[0].presence.status).toBe('online');

    await alicePhone.close();
    const offline = await bobSocket.waitFor(
      'presence.updated',
      (event) => event.data.userId === alice.id && event.data.presence.status === 'offline',
    );
    expect(offline.data.presence.lastSeenAt).not.toBeNull();
    await bobSocket.close();
  });

  it('reports idle clients as away only when all devices are idle', async () => {
    const alice = await registerUser(server, 'idle_alice');
    const bob = await registerUser(server, 'idle_bob');
    await makeFriends(alice, bob);
    const bobSocket = await connectRealtime(url, bob);
    const tabA = await connectRealtime(url, alice);
    const tabB = await connectRealtime(url, await login(server, 'idle_alice'));
    tabA.send({ v: 1, type: 'presence.idle', data: { idle: true } });
    await sleep(200);
    expect((await bob.get('/friends')).body.friends[0].presence.status).toBe('online');
    tabB.send({ v: 1, type: 'presence.idle', data: { idle: true } });
    await bobSocket.waitFor('presence.updated', (event) => event.data.userId === alice.id && event.data.presence.status === 'away');
    await Promise.all([tabA.close(), tabB.close(), bobSocket.close()]);
  });

  it('hides invisible users from friends and does not update last seen while invisible', async () => {
    const alice = await registerUser(server, 'inv_alice');
    const bob = await registerUser(server, 'inv_bob');
    await makeFriends(alice, bob);
    const bobSocket = await connectRealtime(url, bob);
    const aliceSocket = await connectRealtime(url, alice);
    await bobSocket.waitFor('presence.updated', (event) => event.data.userId === alice.id && event.data.presence.status === 'online');

    const set = await alice.put('/presence', { status: 'invisible' });
    expect(set.body.status).toBe('invisible');
    await bobSocket.waitFor('presence.updated', (event) => event.data.userId === alice.id && event.data.presence.status === 'offline');
    await aliceSocket.waitFor('self.presence', (event) => event.data.status === 'invisible');
    const lastSeenWhenHidden = (await server.ctx.db.select().from(users).where(eq(users.id, alice.id)))[0]?.lastSeenAt;

    const viaProfile = await bob.get('/users/inv_alice');
    expect(viaProfile.body.presence.status).toBe('offline');
    const viaFriends = await bob.get('/friends');
    expect(viaFriends.body.friends[0].presence.status).toBe('offline');

    await sleep(50);
    await aliceSocket.close();
    await sleep(200);
    const lastSeenAfter = (await server.ctx.db.select().from(users).where(eq(users.id, alice.id)))[0]?.lastSeenAt;
    expect(lastSeenAfter?.getTime()).toBe(lastSeenWhenHidden?.getTime());
    await bobSocket.close();
  });

  it('does not show presence to non-friends under the default privacy settings', async () => {
    const alice = await registerUser(server, 'np_alice');
    const stranger = await registerUser(server, 'np_stranger');
    const aliceSocket = await connectRealtime(url, alice);
    const profile = await stranger.get('/users/np_alice');
    expect(profile.body.presence).toEqual({ status: 'offline', lastSeenAt: null, activity: null });
    await alice.patch('/profile/privacy', { presenceVisibility: 'everyone' });
    expect((await stranger.get('/users/np_alice')).body.presence.status).toBe('online');
    await aliceSocket.close();
  });

  it('sends realtime friend events and presence after a request is accepted', async () => {
    const alice = await registerUser(server, 'fe_alice');
    const bob = await registerUser(server, 'fe_bob');
    const aliceSocket = await connectRealtime(url, alice);
    const bobSocket = await connectRealtime(url, bob);
    const sent = await alice.post('/friends/requests', { userId: bob.id });
    await bobSocket.waitFor('friends.changed', (event) => event.data.reason === 'request_received');
    await bobSocket.waitFor('notification.created', (event) => event.data.notification.data.type === 'friend_request.received');
    await bob.post(`/friends/requests/${sent.body.request.id}/accept`);
    await aliceSocket.waitFor('friends.changed', (event) => event.data.reason === 'request_accepted');
    await aliceSocket.waitFor('presence.updated', (event) => event.data.userId === bob.id && event.data.presence.status === 'online');
    await Promise.all([aliceSocket.close(), bobSocket.close()]);
  });
});
