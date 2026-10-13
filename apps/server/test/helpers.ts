import { randomUUID } from 'node:crypto';
import type { AddressInfo } from 'node:net';
import type { FastifyInstance, LightMyRequestResponse } from 'fastify';
import pg from 'pg';
import WebSocket from 'ws';
import type { AuthSessionResponse, CurrentUser, ServerEventEnvelope } from '@akeno/shared';
import { type BuiltApp, buildApp } from '../src/app/build-app.js';
import type { AppContext } from '../src/app/context.js';
import { loadConfig } from '../src/config/env.js';
import { TEMPLATE_DATABASE, TEST_ADMIN_URL, TEST_ORIGIN, databaseUrl } from './test-env.js';

export { TEST_ORIGIN };

async function withAdmin<T>(work: (client: pg.Client) => Promise<T>): Promise<T> {
  const client = new pg.Client({ connectionString: TEST_ADMIN_URL });
  await client.connect();
  try {
    return await work(client);
  } finally {
    await client.end();
  }
}

/** Creates an isolated database cloned from the migrated template. */
export async function createTestDatabase(): Promise<string> {
  const name = `akeno_test_${randomUUID().replace(/-/g, '').slice(0, 16)}`;
  for (let attempt = 0; ; attempt += 1) {
    try {
      await withAdmin((client) => client.query(`CREATE DATABASE ${name} TEMPLATE ${TEMPLATE_DATABASE}`));
      return name;
    } catch (error) {
      // Concurrent clones of the same template can briefly conflict; retry.
      if (attempt >= 20) throw error;
      await new Promise((resolve) => setTimeout(resolve, 100 + Math.random() * 200));
    }
  }
}

export async function dropTestDatabase(name: string): Promise<void> {
  await withAdmin((client) => client.query(`DROP DATABASE IF EXISTS ${name} WITH (FORCE)`));
}

export interface TestServer extends BuiltApp {
  dbName: string;
  /** Builds a second app instance on the same database (simulates a restart). */
  restart: () => Promise<TestServer>;
  close: (options?: { keepDatabase?: boolean }) => Promise<void>;
}

export async function createTestServer(
  env: Record<string, string> = {},
  existingDatabase?: string,
): Promise<TestServer> {
  const dbName = existingDatabase ?? (await createTestDatabase());
  const config = loadConfig({
    NODE_ENV: 'test',
    DATABASE_URL: databaseUrl(dbName),
    PUBLIC_ORIGIN: TEST_ORIGIN,
    RATE_LIMIT_ENABLED: 'false',
    ARGON2_MEMORY_KIB: '1024',
    ARGON2_TIME_COST: '1',
    LOG_LEVEL: 'silent',
    DB_POOL_MAX: '5',
    ...env,
  });
  const built = await buildApp({ config, maintenance: false });
  await built.app.ready();
  const server: TestServer = {
    ...built,
    dbName,
    restart: async () => {
      await built.app.close();
      return createTestServer(env, dbName);
    },
    close: async (options) => {
      await built.app.close();
      if (!options?.keepDatabase) await dropTestDatabase(dbName);
    },
  };
  return server;
}

export interface ApiResponse<T = unknown> {
  status: number;
  body: T;
  raw: LightMyRequestResponse;
}

/** A browser-like API client bound to one user session (cookie + CSRF token). */
export class TestClient {
  cookie: string | null = null;
  csrf: string | null = null;
  user: CurrentUser | null = null;
  origin: string | null = TEST_ORIGIN;

  constructor(private readonly app: FastifyInstance) {}

  get id(): string {
    if (!this.user) throw new Error('Client is not signed in');
    return this.user.id;
  }

  async request<T = any>(
    method: 'GET' | 'POST' | 'PATCH' | 'PUT' | 'DELETE',
    path: string,
    body?: unknown,
    options: { csrf?: boolean; headers?: Record<string, string> } = {},
  ): Promise<ApiResponse<T>> {
    const headers: Record<string, string> = { ...options.headers };
    if (this.origin) headers.origin = this.origin;
    if (this.cookie) headers.cookie = this.cookie;
    if (this.csrf && options.csrf !== false) headers['x-csrf-token'] = this.csrf;
    const raw = await this.app.inject({
      method,
      url: path.startsWith('/') && !path.startsWith('/api') && !path.startsWith('/health') ? `/api/v1${path}` : path,
      headers,
      ...(body !== undefined ? { payload: body as Record<string, unknown> } : {}),
    });
    for (const cookie of raw.cookies as Array<{ name: string; value: string; maxAge?: number; expires?: Date }>) {
      const cleared = cookie.value === '' || (cookie.expires && cookie.expires.getTime() < Date.now());
      this.cookie = cleared ? null : `${cookie.name}=${cookie.value}`;
    }
    const text = raw.body;
    return { status: raw.statusCode, body: (text ? JSON.parse(text) : null) as T, raw };
  }

  get<T = any>(path: string) {
    return this.request<T>('GET', path);
  }
  post<T = any>(path: string, body?: unknown, options?: { csrf?: boolean }) {
    return this.request<T>('POST', path, body ?? {}, options);
  }
  patch<T = any>(path: string, body: unknown) {
    return this.request<T>('PATCH', path, body);
  }
  put<T = any>(path: string, body: unknown) {
    return this.request<T>('PUT', path, body);
  }
  delete<T = any>(path: string, body?: unknown) {
    return this.request<T>('DELETE', path, body);
  }

  adopt(response: ApiResponse<AuthSessionResponse>): this {
    this.csrf = response.body.csrfToken;
    this.user = response.body.user;
    return this;
  }
}

export const DEFAULT_PASSWORD = 'correct-horse-battery-staple';

export async function createInvitationCode(ctx: AppContext, maxUses = 1): Promise<string> {
  const created = await ctx.admin.createInvitation(null, { maxUses, expiresInHours: 24, note: 'test' }, null);
  return created.code;
}

/** Registers a user through the public API with a fresh invitation code. */
export async function registerUser(
  server: BuiltApp,
  onlineId: string,
  options: { password?: string; displayName?: string } = {},
): Promise<TestClient> {
  const client = new TestClient(server.app);
  const invitationCode = await createInvitationCode(server.ctx);
  const response = await client.post<AuthSessionResponse>('/auth/register', {
    onlineId,
    password: options.password ?? DEFAULT_PASSWORD,
    invitationCode,
    ...(options.displayName ? { displayName: options.displayName } : {}),
  });
  if (response.status !== 201) {
    throw new Error(`Registration of ${onlineId} failed: ${response.status} ${JSON.stringify(response.body)}`);
  }
  return client.adopt(response);
}

export async function login(server: BuiltApp, onlineId: string, password = DEFAULT_PASSWORD): Promise<TestClient> {
  const client = new TestClient(server.app);
  const response = await client.post<AuthSessionResponse>('/auth/login', { onlineId, password });
  if (response.status !== 200) throw new Error(`Login failed: ${response.status} ${JSON.stringify(response.body)}`);
  return client.adopt(response);
}

export async function makeFriends(a: TestClient, b: TestClient): Promise<void> {
  const sent = await a.post('/friends/requests', { userId: b.id });
  if (sent.status !== 201) throw new Error(`Friend request failed: ${JSON.stringify(sent.body)}`);
  const accepted = await b.post(`/friends/requests/${sent.body.request.id}/accept`);
  if (accepted.status !== 200) throw new Error(`Accept failed: ${JSON.stringify(accepted.body)}`);
}

/** Starts listening on an ephemeral port so real WebSocket clients can connect. */
export async function listen(server: BuiltApp): Promise<string> {
  if (!server.app.server.listening) await server.app.listen({ host: '127.0.0.1', port: 0 });
  const address = server.app.server.address() as AddressInfo;
  return `ws://127.0.0.1:${address.port}/ws`;
}

export class RealtimeClient {
  readonly events: ServerEventEnvelope[] = [];
  closeCode: number | null = null;
  private waiters: Array<() => void> = [];

  constructor(readonly socket: WebSocket) {
    socket.on('message', (data) => {
      this.events.push(JSON.parse(data.toString()) as ServerEventEnvelope);
      for (const waiter of this.waiters.splice(0)) waiter();
    });
    socket.on('close', (code) => {
      this.closeCode = code;
      for (const waiter of this.waiters.splice(0)) waiter();
    });
  }

  send(message: unknown): void {
    this.socket.send(JSON.stringify(message));
  }

  /** Resolves with the first event (already received or future) that matches. */
  async waitFor<E extends ServerEventEnvelope['type']>(
    type: E,
    predicate: (event: Extract<ServerEventEnvelope, { type: E }>) => boolean = () => true,
    timeoutMs = 5000,
  ): Promise<Extract<ServerEventEnvelope, { type: E }>> {
    const deadline = Date.now() + timeoutMs;
    for (;;) {
      const match = this.events.find(
        (event): event is Extract<ServerEventEnvelope, { type: E }> =>
          event.type === type && predicate(event as Extract<ServerEventEnvelope, { type: E }>),
      );
      if (match) return match;
      const remaining = deadline - Date.now();
      if (remaining <= 0) {
        throw new Error(`Timed out waiting for ${type}. Received: ${this.events.map((e) => e.type).join(', ')}`);
      }
      await new Promise<void>((resolve) => {
        const timer = setTimeout(resolve, remaining);
        this.waiters.push(() => {
          clearTimeout(timer);
          resolve();
        });
      });
    }
  }

  async waitForClose(timeoutMs = 5000): Promise<number> {
    const deadline = Date.now() + timeoutMs;
    while (this.closeCode === null) {
      if (Date.now() > deadline) throw new Error('Timed out waiting for the socket to close');
      await new Promise((resolve) => setTimeout(resolve, 20));
    }
    return this.closeCode;
  }

  clear(): void {
    this.events.length = 0;
  }

  close(): Promise<void> {
    return new Promise((resolve) => {
      if (this.socket.readyState === WebSocket.CLOSED) return resolve();
      this.socket.once('close', () => resolve());
      this.socket.close();
    });
  }
}

/** Opens an authenticated WebSocket like a browser would (cookie + Origin). */
export async function connectRealtime(
  url: string,
  client: TestClient,
  options: { origin?: string | null; cookie?: string | null } = {},
): Promise<RealtimeClient> {
  const headers: Record<string, string> = {};
  const origin = options.origin === undefined ? TEST_ORIGIN : options.origin;
  const cookie = options.cookie === undefined ? client.cookie : options.cookie;
  if (origin) headers.Origin = origin;
  if (cookie) headers.Cookie = cookie;
  const socket = new WebSocket(url, { headers });
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

export function sleep(ms: number): Promise<void> {
  return new Promise((resolve) => setTimeout(resolve, ms));
}
