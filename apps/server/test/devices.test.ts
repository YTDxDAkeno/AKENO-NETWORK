import { eq } from 'drizzle-orm';
import { afterAll, beforeAll, describe, expect, it } from 'vitest';
import WebSocket from 'ws';
import {
  DEVICE_SCOPES,
  REALTIME_CLOSE_CODES,
  type ApiErrorBody,
  type DevicePairingStartResponse,
  type DeviceScope,
  type DeviceTokenResponse,
} from '@akeno/shared';
import { devicePairings, deviceCredentials, users } from '../src/database/schema/index.js';
import { deviceRouteKeys } from '../src/modules/devices/scopes.js';
import {
  DEFAULT_PASSWORD,
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
  server = await createTestServer({ PUBLIC_ORIGIN: 'http://localhost:5173' });
  url = await listen(server);
});

afterAll(async () => {
  await server.close();
});

/** A non-browser client: no cookies, no Origin header, optional bearer token. */
class DeviceClient {
  constructor(public token: string | null = null) {}

  async request<T = any>(method: 'GET' | 'POST' | 'PUT' | 'PATCH' | 'DELETE', path: string, body?: unknown, headers: Record<string, string> = {}) {
    const raw = await server.app.inject({
      method,
      url: path.startsWith('/api') ? path : `/api/v1${path}`,
      headers: { ...(this.token ? { authorization: `Bearer ${this.token}` } : {}), ...headers },
      ...(body !== undefined ? { payload: body as Record<string, unknown> } : {}),
    });
    return { status: raw.statusCode, body: (raw.body ? JSON.parse(raw.body) : null) as T, raw } satisfies ApiResponse<T>;
  }
}

async function startPairing(scopes?: DeviceScope[]): Promise<DevicePairingStartResponse> {
  const response = await new DeviceClient().request<DevicePairingStartResponse>('POST', '/devices/pairings', {
    deviceName: 'Living room PS5',
    platform: 'PlayStation 5 (homebrew)',
    clientVersion: '0.1.0',
    ...(scopes ? { scopes } : {}),
  });
  expect(response.status).toBe(201);
  return response.body;
}

/** Lets the next poll through the 5-second polling interval without waiting. */
async function allowNextPoll(deviceCodeId?: string): Promise<void> {
  const update = server.database.db.update(devicePairings).set({ lastPolledAt: new Date(Date.now() - 60_000) });
  await (deviceCodeId ? update.where(eq(devicePairings.id, deviceCodeId)) : update);
}

async function poll(deviceCode: string) {
  await allowNextPoll();
  return new DeviceClient().request<DeviceTokenResponse & ApiErrorBody>('POST', '/devices/pairings/token', { deviceCode });
}

/** Runs the complete pairing flow and returns a device client holding the issued token. */
async function pairDevice(owner: TestClient, scopes?: DeviceScope[]): Promise<{ device: DeviceClient; token: DeviceTokenResponse }> {
  const started = await startPairing();
  const approved = await owner.post('/devices/pairings/approve', { userCode: started.userCode, ...(scopes ? { scopes } : {}) });
  expect(approved.status).toBe(204);
  const redeemed = await poll(started.deviceCode);
  expect(redeemed.status).toBe(200);
  return { device: new DeviceClient(redeemed.body.accessToken), token: redeemed.body };
}

async function connectDevice(token: string | null): Promise<RealtimeClient> {
  const socket = new WebSocket(url, { headers: token ? { Authorization: `Bearer ${token}` } : {} });
  const realtime = new RealtimeClient(socket);
  await new Promise<void>((resolve, reject) => {
    socket.once('open', () => resolve());
    socket.once('unexpected-response', (_request, response) => {
      reject(Object.assign(new Error(`WebSocket rejected with HTTP ${response.statusCode}`), { status: response.statusCode }));
    });
    socket.once('error', reject);
  });
  await realtime.waitFor('hello');
  return realtime;
}

describe('device pairing', () => {
  it('pairs a device through the website without sharing the password', async () => {
    const owner = await registerUser(server, 'pair_owner');
    const started = await startPairing();
    expect(started.userCode).toMatch(/^[A-HJKMNP-Z2-9]{4}-[A-HJKMNP-Z2-9]{4}$/);
    expect(started.deviceCode).toMatch(/^akp_[A-Za-z0-9_-]{43}$/);
    expect(started.verificationUri).toBe('http://localhost:5173/pair');
    expect(started.verificationUriComplete).toBe(`http://localhost:5173/pair?code=${started.userCode}`);
    expect(started.expiresIn).toBe(600);
    expect(started.interval).toBe(5);

    // Nothing is stored in plain text.
    const [stored] = await server.database.db.select().from(devicePairings).limit(1);
    expect(JSON.stringify(stored)).not.toContain(started.deviceCode);
    expect(JSON.stringify(stored)).not.toContain(started.userCode.replace('-', ''));

    const pending = await poll(started.deviceCode);
    expect(pending.status).toBe(400);
    expect(pending.body.error.code).toBe('AUTHORIZATION_PENDING');

    // Codes are case- and separator-insensitive when typed by the user.
    const lookup = await owner.post('/devices/pairings/lookup', { userCode: started.userCode.toLowerCase().replace('-', ' ') });
    expect(lookup.status).toBe(200);
    expect(lookup.body.pairing).toMatchObject({
      deviceName: 'Living room PS5',
      platform: 'PlayStation 5 (homebrew)',
      clientVersion: '0.1.0',
      requestedScopes: [...DEVICE_SCOPES],
    });

    const granted: DeviceScope[] = ['profile:read', 'friends:read', 'realtime'];
    expect((await owner.post('/devices/pairings/approve', { userCode: started.userCode, scopes: granted })).status).toBe(204);

    const redeemed = await poll(started.deviceCode);
    expect(redeemed.status).toBe(200);
    expect(redeemed.body.tokenType).toBe('Bearer');
    expect(redeemed.body.accessToken).toMatch(/^akd_[A-Za-z0-9_-]{43}$/);
    expect(redeemed.body.device.scopes).toEqual(granted);
    expect(redeemed.body.user.onlineId).toBe('pair_owner');

    const [credential] = await server.database.db.select().from(deviceCredentials).where(eq(deviceCredentials.userId, owner.id));
    expect(credential?.tokenHash).toHaveLength(64);
    expect(JSON.stringify(credential)).not.toContain(redeemed.body.accessToken);

    // The device code can be redeemed only once.
    const again = await poll(started.deviceCode);
    expect(again.status).toBe(400);
    expect(again.body.error.code).toBe('PAIRING_INVALID');

    // The user code is consumed as well.
    expect((await owner.post('/devices/pairings/lookup', { userCode: started.userCode })).status).toBe(404);

    const listed = await owner.get('/devices');
    expect(listed.body.devices).toHaveLength(1);
    expect(listed.body.devices[0]).toMatchObject({ name: 'Living room PS5', scopes: granted, current: false });
  });

  it('asks devices that poll too quickly to slow down', async () => {
    const started = await startPairing();
    const client = new DeviceClient();
    const first = await client.request('POST', '/devices/pairings/token', { deviceCode: started.deviceCode });
    expect(first.body.error.code).toBe('AUTHORIZATION_PENDING');
    const second = await client.request('POST', '/devices/pairings/token', { deviceCode: started.deviceCode });
    expect(second.status).toBe(400);
    expect(second.body.error.code).toBe('SLOW_DOWN');
  });

  it('reports denied, expired, unknown and malformed pairing requests', async () => {
    const owner = await registerUser(server, 'pair_denier');

    const denied = await startPairing();
    expect((await owner.post('/devices/pairings/deny', { userCode: denied.userCode })).status).toBe(204);
    expect((await poll(denied.deviceCode)).body.error.code).toBe('ACCESS_DENIED');
    expect((await owner.post('/devices/pairings/approve', { userCode: denied.userCode })).status).toBe(404);

    const expired = await startPairing();
    await server.database.db
      .update(devicePairings)
      .set({ expiresAt: new Date(Date.now() - 1000) })
      .where(eq(devicePairings.status, 'pending'));
    expect((await poll(expired.deviceCode)).body.error.code).toBe('EXPIRED_TOKEN');
    const lateApproval = await owner.post('/devices/pairings/approve', { userCode: expired.userCode });
    expect(lateApproval.status).toBe(404);
    expect(lateApproval.body.error.code).toBe('PAIRING_INVALID');

    const unknown = await poll(`akp_${'A'.repeat(43)}`);
    expect(unknown.body.error.code).toBe('PAIRING_INVALID');

    const malformed = await new DeviceClient().request('POST', '/devices/pairings/token', { deviceCode: 'nope' });
    expect(malformed.status).toBe(400);
    expect(malformed.body.error.code).toBe('VALIDATION_FAILED');

    const badCode = await owner.post('/devices/pairings/lookup', { userCode: 'O0O0-I1I1' });
    expect(badCode.body.error.code).toBe('VALIDATION_FAILED');
  });

  it('only lets users grant scopes the device requested', async () => {
    const owner = await registerUser(server, 'pair_scoper');
    const started = await startPairing(['profile:read', 'friends:read']);
    const response = await owner.post('/devices/pairings/approve', {
      userCode: started.userCode,
      scopes: ['profile:read', 'messages:write'],
    });
    expect(response.status).toBe(400);
    expect(response.body.error.fields.scopes).toBeDefined();
  });

  it('requires a signed-in browser session to look up or approve codes', async () => {
    const started = await startPairing();
    const anonymous = await new DeviceClient().request('POST', '/devices/pairings/approve', { userCode: started.userCode }, {
      origin: 'http://localhost:5173',
    });
    expect(anonymous.status).toBe(401);

    const owner = await registerUser(server, 'pair_csrf');
    const withoutCsrf = await owner.request('POST', '/devices/pairings/approve', { userCode: started.userCode }, { csrf: false });
    expect(withoutCsrf.status).toBe(403);
    expect(withoutCsrf.body.error.code).toBe('CSRF_FAILED');

    // A paired device cannot approve further devices.
    const { device } = await pairDevice(owner);
    const fromDevice = await device.request('POST', '/devices/pairings/approve', { userCode: started.userCode });
    expect(fromDevice.status).toBe(403);
    expect(fromDevice.body.error.code).toBe('DEVICE_SCOPE_DENIED');
  });

  it('refuses to issue a token for a suspended account', async () => {
    const owner = await registerUser(server, 'pair_suspend');
    const started = await startPairing();
    await owner.post('/devices/pairings/approve', { userCode: started.userCode });
    await server.database.db.update(users).set({ status: 'suspended' }).where(eq(users.id, owner.id));
    expect((await poll(started.deviceCode)).body.error.code).toBe('PAIRING_INVALID');
  });
});

describe('device tokens', () => {
  it('call permitted API routes without cookies, Origin or CSRF tokens', async () => {
    const owner = await registerUser(server, 'dev_user');
    const friend = await registerUser(server, 'dev_friend');
    await makeFriends(owner, friend);
    const { device } = await pairDevice(owner);

    const self = await device.request('GET', '/devices/current');
    expect(self.status).toBe(200);
    expect(self.body).toMatchObject({ apiVersion: 1, realtimeProtocolVersion: 1, user: { onlineId: 'dev_user' } });
    expect(self.body.device.current).toBe(true);

    expect((await device.request('GET', '/users/me')).body.onlineId).toBe('dev_user');
    const friends = await device.request('GET', '/friends');
    expect(friends.body.friends.map((entry: { onlineId: string }) => entry.onlineId)).toEqual(['dev_friend']);

    const conversation = await device.request('POST', '/conversations', { userId: friend.id });
    expect(conversation.status).toBe(200);
    const sent = await device.request('POST', `/conversations/${conversation.body.id}/messages`, {
      body: 'Hello from the console client',
      clientMessageId: '6f1c1f5e-4f61-4e0c-9a2a-5a4f9a0e8a11',
    });
    expect(sent.status).toBe(201);
    const history = await friend.get(`/conversations/${conversation.body.id}/messages`);
    expect(history.body.messages.at(-1).body).toBe('Hello from the console client');

    expect((await device.request('PUT', '/presence', { status: 'away' })).status).toBe(200);
    expect((await device.request('GET', '/invitations')).status).toBe(200);
    expect((await device.request('GET', '/parties/current')).status).toBe(200);
    expect((await device.request('GET', '/game-sessions')).status).toBe(200);
  });

  it('never reach account, security, privacy, device management or admin routes', async () => {
    const owner = await registerUser(server, 'dev_admin');
    await server.database.db.update(users).set({ role: 'admin' }).where(eq(users.id, owner.id));
    const { device } = await pairDevice(owner);

    const forbidden: Array<[Parameters<DeviceClient['request']>[0], string, unknown?]> = [
      ['GET', '/auth/session'],
      ['GET', '/auth/sessions'],
      ['POST', '/auth/logout'],
      ['POST', '/auth/password', { currentPassword: DEFAULT_PASSWORD, newPassword: 'another-long-password-1' }],
      ['DELETE', '/users/me', { password: DEFAULT_PASSWORD, confirmation: 'DELETE' }],
      ['PATCH', '/profile', { bio: 'changed by device' }],
      ['PATCH', '/profile/privacy', { presenceVisibility: 'everyone' }],
      ['GET', '/blocks'],
      ['GET', '/devices'],
      ['GET', '/admin/stats'],
      ['GET', '/admin/users'],
      ['POST', '/devices/pairings', { deviceName: 'Chained device' }],
    ];
    for (const [method, path, body] of forbidden) {
      const response = await device.request(method, path, body);
      expect({ path, status: response.status, code: response.body?.error?.code }).toEqual({
        path,
        status: 403,
        code: 'DEVICE_SCOPE_DENIED',
      });
    }
  });

  it('are limited to the scopes the user granted', async () => {
    const owner = await registerUser(server, 'dev_limited');
    const { device } = await pairDevice(owner, ['profile:read']);
    expect((await device.request('GET', '/users/me')).status).toBe(200);
    const friends = await device.request('GET', '/friends');
    expect(friends.status).toBe(403);
    expect(friends.body.error.code).toBe('DEVICE_SCOPE_DENIED');
    expect(friends.body.error.message).toContain('friends:read');
    await expect(connectDevice(device.token)).rejects.toMatchObject({ status: 403 });
  });

  it('ignore cookies whenever a bearer token is present', async () => {
    const owner = await registerUser(server, 'dev_cookie');
    const response = await owner.request('GET', '/users/me', undefined, { headers: { authorization: 'Bearer akd_invalid' } });
    expect(response.status).toBe(401);
    // Basic authentication from a protecting reverse proxy does not disturb cookie sessions.
    const basic = await owner.request('GET', '/users/me', undefined, { headers: { authorization: 'Basic dXNlcjpwYXNz' } });
    expect(basic.status).toBe(200);
  });

  it('stop working after revocation, sign-out, password change or expiry', async () => {
    const owner = await registerUser(server, 'dev_revoke');

    const first = await pairDevice(owner);
    const live = await connectDevice(first.device.token);
    expect((await owner.delete(`/devices/${first.token.device.id}`)).status).toBe(204);
    expect(await live.waitForClose()).toBe(REALTIME_CLOSE_CODES.sessionRevoked);
    expect((await first.device.request('GET', '/users/me')).status).toBe(401);

    const second = await pairDevice(owner);
    expect((await second.device.request('DELETE', '/devices/current')).status).toBe(204);
    expect((await second.device.request('GET', '/devices/current')).status).toBe(401);

    const third = await pairDevice(owner);
    const changed = await owner.post('/auth/password', { currentPassword: DEFAULT_PASSWORD, newPassword: 'a-brand-new-passphrase-42' });
    expect(changed.status).toBe(204);
    expect((await third.device.request('GET', '/users/me')).status).toBe(401);

    const fourth = await pairDevice(owner);
    await server.database.db
      .update(deviceCredentials)
      .set({ expiresAt: new Date(Date.now() - 1000) })
      .where(eq(deviceCredentials.id, fourth.token.device.id));
    expect((await fourth.device.request('GET', '/users/me')).status).toBe(401);

    expect((await owner.get('/devices')).body.devices).toHaveLength(0);
  });

  it('cannot be revoked by another user', async () => {
    const owner = await registerUser(server, 'dev_owner2');
    const intruder = await registerUser(server, 'dev_intruder');
    const { token, device } = await pairDevice(owner);
    expect((await intruder.delete(`/devices/${token.device.id}`)).status).toBe(404);
    expect((await device.request('GET', '/users/me')).status).toBe(200);
  });

  it('rejects malformed bearer tokens', async () => {
    for (const header of ['Bearer', 'Bearer ', 'Bearer akd_short', `Bearer akd_${'x'.repeat(43)} extra`]) {
      const raw = await server.app.inject({ method: 'GET', url: '/api/v1/users/me', headers: { authorization: header } });
      expect(raw.statusCode).toBe(401);
    }
  });
});

describe('device realtime', () => {
  it('connects without an Origin header and receives events like a browser', async () => {
    const owner = await registerUser(server, 'rt_device');
    const friend = await registerUser(server, 'rt_friend');
    await makeFriends(owner, friend);
    const friendSocket = await connectRealtime(url, friend);
    const { device } = await pairDevice(owner);

    const deviceSocket = await connectDevice(device.token);
    // The device counts as a live connection, so friends see the user online.
    await friendSocket.waitFor('presence.updated', (event) => event.data.userId === owner.id && event.data.presence.status === 'online');

    const conversation = await friend.post('/conversations', { userId: owner.id });
    await friend.post(`/conversations/${conversation.body.id}/messages`, {
      body: 'Ready to play?',
      clientMessageId: '0b9d2c1e-7a35-4a8e-9a77-1c1f0e1a2b3c',
    });
    const event = await deviceSocket.waitFor('message.created');
    expect(event.data.message.body).toBe('Ready to play?');

    deviceSocket.send({ v: 1, type: 'ping' });
    await deviceSocket.waitFor('pong');

    await deviceSocket.close();
    await friendSocket.waitFor('presence.updated', (event) => event.data.userId === owner.id && event.data.presence.status === 'offline');
    await friendSocket.close();
  });

  it('rejects missing or invalid device tokens', async () => {
    await expect(connectDevice(null)).rejects.toMatchObject({ status: 403 });
    await expect(connectDevice(`akd_${'A'.repeat(43)}`)).rejects.toMatchObject({ status: 401 });
  });
});

describe('device route table', () => {
  it('only references routes that exist', () => {
    for (const key of deviceRouteKeys()) {
      const [method, routeUrl] = key.split(' ') as [string, string];
      expect({ key, exists: server.app.hasRoute({ method: method as 'GET', url: routeUrl }) }).toEqual({ key, exists: true });
    }
  });

  it('never exposes sensitive route families', () => {
    const sensitive = /\/(auth|admin|blocks|profile\/privacy)\b|DELETE \/api\/v1\/users\/me|^(GET|DELETE) \/api\/v1\/devices(\/:id)?$|\/devices\/pairings/;
    expect(deviceRouteKeys().filter((key) => sensitive.test(key))).toEqual([]);
  });
});
