import { afterAll, beforeAll, describe, expect, it } from 'vitest';
import { TestClient, createTestServer, registerUser, type TestServer } from './helpers.js';

let server: TestServer;

beforeAll(async () => {
  server = await createTestServer();
});

afterAll(async () => {
  await server.close();
});

describe('health and HTTP hardening', () => {
  it('reports liveness and readiness', async () => {
    const live = await server.app.inject({ method: 'GET', url: '/health/live' });
    expect(live.statusCode).toBe(200);
    const ready = await server.app.inject({ method: 'GET', url: '/health/ready' });
    expect(ready.json()).toEqual({ status: 'ok', database: 'connected', migrations: 'applied' });
  });

  it('sends security headers and disables API caching', async () => {
    const client = await registerUser(server, 'headers');
    const response = await client.get('/users/me');
    const headers = response.raw.headers;
    expect(headers['content-security-policy']).toContain("default-src 'self'");
    expect(headers['content-security-policy']).toContain("frame-ancestors 'none'");
    expect(headers['x-content-type-options']).toBe('nosniff');
    expect(headers['cache-control']).toBe('no-store');
    expect(headers['set-cookie']).toBeUndefined();
  });

  it('sets HttpOnly SameSite cookies and never exposes the token in the body', async () => {
    const client = new TestClient(server.app);
    const code = (await server.ctx.admin.createInvitation(null, { maxUses: 1, expiresInHours: 1, note: '' }, null)).code;
    const response = await client.post('/auth/register', {
      onlineId: 'cookieuser',
      password: 'correct-horse-battery-staple',
      invitationCode: code,
    });
    const setCookie = String(response.raw.headers['set-cookie']);
    expect(setCookie).toMatch(/HttpOnly/i);
    expect(setCookie).toMatch(/SameSite=Lax/i);
    expect(setCookie).toMatch(/Path=\//);
    const token = client.cookie!.split('=')[1]!;
    expect(response.raw.body).not.toContain(token);
  });

  it('uses __Host- prefixed Secure cookies for HTTPS deployments', async () => {
    const secure = await createTestServer({ PUBLIC_ORIGIN: 'https://akeno.example.com' });
    try {
      const client = new TestClient(secure.app);
      client.origin = 'https://akeno.example.com';
      const code = (await secure.ctx.admin.createInvitation(null, { maxUses: 1, expiresInHours: 1, note: '' }, null)).code;
      const response = await client.post('/auth/register', {
        onlineId: 'secureuser',
        password: 'correct-horse-battery-staple',
        invitationCode: code,
      });
      const setCookie = String(response.raw.headers['set-cookie']);
      expect(setCookie).toMatch(/^__Host-akeno_session=/);
      expect(setCookie).toMatch(/Secure/);
      expect(response.raw.headers['strict-transport-security']).toBeDefined();
    } finally {
      await secure.close();
    }
  });

  it('returns JSON errors for unknown endpoints, oversized and malformed bodies', async () => {
    const client = await registerUser(server, 'errors');
    const missing = await client.get('/does-not-exist');
    expect(missing.status).toBe(404);
    expect(missing.body.error.code).toBe('NOT_FOUND');
    const huge = await client.patch('/profile', { bio: 'x'.repeat(70 * 1024) });
    expect(huge.status).toBe(413);
    expect(huge.body.error.code).toBe('PAYLOAD_TOO_LARGE');
    const malformed = await server.app.inject({
      method: 'PATCH',
      url: '/api/v1/profile',
      headers: { 'content-type': 'application/json', origin: 'http://localhost:5173', cookie: client.cookie!, 'x-csrf-token': client.csrf! },
      payload: '{"bio": ',
    });
    expect(malformed.statusCode).toBe(400);
    expect(malformed.json().error.code).toBe('BAD_REQUEST');
  });

  it('rate limits requests when enabled', async () => {
    const limited = await createTestServer({ RATE_LIMIT_ENABLED: 'true' });
    try {
      const client = new TestClient(limited.app);
      const statuses: number[] = [];
      for (let i = 0; i < 22; i += 1) {
        statuses.push((await client.post('/auth/login', { onlineId: `nobody${i}`, password: 'whatever-password' })).status);
      }
      expect(statuses).toContain(429);
    } finally {
      await limited.close();
    }
  });
});
