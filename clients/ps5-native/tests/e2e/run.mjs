#!/usr/bin/env node
// End-to-end test of AKENO NETWORK Native Phase 2.1 (docs/PS5_NATIVE_PHASE2.md). Nothing is mocked:
//   * a disposable PostgreSQL database and the production server build (apps/server/dist),
//   * a TLS terminator with a throwaway CA in front of it (certificate checks as in production),
//   * the native-IPC daemon (host build of clients/ps5 with AKENO_NATIVE_IPC), paired through its
//     own page API and approved on the "website",
//   * AKENO Native (host build, headless SDL), driven through its virtual controller.
// Friends, profiles and activity come from the real API through the daemon; presence, friend
// requests and profile changes arrive live over the daemon's single realtime session. The local
// interface's security rules are checked from outside, and the release daemon is shown to ignore
// the native key. A PC run proves the logic only; it is not evidence about PS5 hardware.
//
//   node clients/ps5-native/tests/e2e/run.mjs --native <akeno-native> --daemon <akeno-app-native-ipc> --release-daemon <akeno-app>
//     [--companion <akeno-ingame-check>] [--livekit <livekit-server>]
//
// With --livekit, a disposable LiveKit server and the AKENO voice gateway (apps/voice-gateway/dist,
// behind its own TLS terminator) take part, and AKENO Native talks with a website member in both
// directions (party voice, docs/PS5_VOICE.md).
//
// Requires `pnpm build`, PostgreSQL (TEST_DATABASE_ADMIN_URL) and openssl. AKN_E2E_OUT keeps the
// screenshots and reports.
import assert from 'node:assert/strict';
import { spawn, spawnSync } from 'node:child_process';
import { copyFileSync, existsSync, mkdirSync, mkdtempSync, readFileSync, readdirSync, rmSync, statSync, writeFileSync } from 'node:fs';
import { randomUUID } from 'node:crypto';
import { createRequire } from 'node:module';
import net from 'node:net';
import { tmpdir } from 'node:os';
import { dirname, join, resolve } from 'node:path';
import { inflateSync } from 'node:zlib';
import tls from 'node:tls';
import { fileURLToPath } from 'node:url';

const root = resolve(dirname(fileURLToPath(import.meta.url)), '../../../..');
const serverRequire = createRequire(resolve(root, 'apps/server/package.json'));
const pg = serverRequire('pg');
const WebSocket = serverRequire('ws');
const arg = (name) => {
  const i = process.argv.indexOf(name);
  return i > 0 ? resolve(process.argv[i + 1]) : null;
};
const nativeBinary = arg('--native');
const daemonBinary = arg('--daemon');
const releaseBinary = arg('--release-daemon');
const companionBinary = arg('--companion'); // optional: clients/ps5-ingame akeno-ingame-check
const livekitBinary = arg('--livekit'); // optional: livekit-server (pinned) for the party voice step
if (!nativeBinary || !daemonBinary || !releaseBinary) {
  console.error('usage: run.mjs --native <akeno-native> --daemon <akeno-app-native-ipc> --release-daemon <akeno-app>');
  process.exit(2);
}
const adminUrl = process.env.TEST_DATABASE_ADMIN_URL ?? 'postgres://akeno:akeno_dev_password@127.0.0.1:5432/postgres';
const databaseName = 'akeno_native_e2e';
const ADMIN_PASSWORD = 'native-e2e-admin-password-1';
const PASSWORD = 'correct-horse-battery-staple';
const work = mkdtempSync(join(tmpdir(), 'akeno-native-e2e-'));
const children = new Set();
let failures = 0;

const log = (message) => process.stdout.write(`${message}\n`);
const sleep = (ms) => new Promise((done) => setTimeout(done, ms));

async function step(name, fn) {
  const started = Date.now();
  try {
    await fn();
    log(`  ok   ${name} (${Date.now() - started} ms)`);
  } catch (error) {
    failures += 1;
    log(`  FAIL ${name}\n${String(error?.stack ?? error).replace(/^/gm, '       ')}`);
  }
}

async function waitFor(predicate, what, timeoutMs = 20_000) {
  const deadline = Date.now() + timeoutMs;
  while (Date.now() < deadline) {
    if (await predicate()) return;
    await sleep(100);
  }
  throw new Error(`Timed out waiting for ${what}`);
}

function freePort() {
  return new Promise((done, fail) => {
    const server = net.createServer();
    server.listen(0, '127.0.0.1', () => {
      const { port } = server.address();
      server.close(() => done(port));
    });
    server.on('error', fail);
  });
}

// ---- Server, TLS and accounts ------------------------------------------------------------------

const backendPort = await freePort();
const origin = `http://localhost:${backendPort}`;
// Party voice (optional): LiveKit, the gateway and the gateway's TLS terminator.
const LK_KEY = 'e2ekey';
const LK_SECRET = 'native-e2e-livekit-secret-0123456789abcdef';
const livekitPort = livekitBinary ? await freePort() : 0;
const gatewayPort = livekitBinary ? await freePort() : 0;
const gatewayTlsPort = livekitBinary ? await freePort() : 0;
const databaseUrl = new URL(adminUrl);
databaseUrl.pathname = `/${databaseName}`;
const serverEnv = {
  MEDIA_DIR: join(work, 'media'), ...process.env, NODE_ENV: 'production', HOST: '127.0.0.1', PORT: String(backendPort),
  DATABASE_URL: databaseUrl.toString(), PUBLIC_ORIGIN: origin, REGISTRATION_MODE: 'invite', MIGRATE_ON_START: 'true',
  LOG_LEVEL: 'warn', LOG_PRETTY: 'false',
  ...(livekitBinary
    ? { LIVEKIT_URL: `ws://127.0.0.1:${livekitPort}`, LIVEKIT_API_KEY: LK_KEY, LIVEKIT_API_SECRET: LK_SECRET,
        VOICE_GATEWAY_URL: `wss://127.0.0.1:${gatewayTlsPort}/voice-gateway` }
    : {}),
};

class WebSession {
  constructor() {
    this.cookie = null;
    this.csrf = null;
    this.user = null;
  }
  async request(method, path, body) {
    const headers = { Origin: origin };
    if (body !== undefined) headers['Content-Type'] = 'application/json';
    if (this.cookie) headers.Cookie = this.cookie;
    if (this.csrf && method !== 'GET') headers['X-CSRF-Token'] = this.csrf;
    const response = await fetch(`http://127.0.0.1:${backendPort}/api/v1${path}`, { method, headers, body: body === undefined ? undefined : JSON.stringify(body) });
    const setCookie = response.headers.get('set-cookie');
    if (setCookie) this.cookie = setCookie.split(';')[0];
    const text = await response.text();
    return { status: response.status, body: text ? JSON.parse(text) : null };
  }
  async signIn(onlineId, password) {
    const response = await this.request('POST', '/auth/login', { onlineId, password });
    assert.equal(response.status, 200, JSON.stringify(response.body));
    this.csrf = response.body.csrfToken;
    this.user = response.body.user;
    return this;
  }
  /** Opens the website's realtime connection: the user appears online to friends. */
  online() {
    return new Promise((done, fail) => {
      const socket = new WebSocket(`ws://127.0.0.1:${backendPort}/ws`, { headers: { Cookie: this.cookie, Origin: origin } });
      socket.on('message', (raw) => {
        if (JSON.parse(raw.toString()).type === 'hello') done(socket);
      });
      socket.on('error', fail);
    });
  }
}

async function registerUser(admin, onlineId) {
  const invitation = await admin.request('POST', '/admin/invitations', { maxUses: 1, expiresInHours: 1, note: 'native e2e' });
  assert.equal(invitation.status, 201);
  const session = new WebSession();
  const response = await session.request('POST', '/auth/register', { onlineId, password: PASSWORD, invitationCode: invitation.body.code });
  assert.equal(response.status, 201, JSON.stringify(response.body));
  session.csrf = response.body.csrfToken;
  session.user = response.body.user;
  return session;
}

function openssl(args) {
  const result = spawnSync('openssl', args, { cwd: work, encoding: 'utf8' });
  if (result.status !== 0) throw new Error(`openssl ${args.join(' ')} failed: ${result.stderr}`);
}

// Every connection through the TLS terminator is recorded by its first request line, so the test
// can show that the console holds exactly one realtime session (the daemon's).
const tlsRequests = [];
const gatewaySockets = new Set();
async function startTlsProxy(port, upstreamPort = backendPort) {
  if (!existsSync(join(work, 'server.pem'))) createCertificates();
  const record = upstreamPort === backendPort;
  const server = tls.createServer({ key: readFileSync(join(work, 'server.key')), cert: readFileSync(join(work, 'server.pem')) }, (socket) => {
    const upstream = net.connect(upstreamPort, '127.0.0.1');
    if (!record) gatewaySockets.add(socket);
    let first = true;
    socket.on('data', (chunk) => {
      if (!first || !record) return;
      first = false;
      const head = chunk.toString('latin1');
      tlsRequests.push({ line: head.split('\r\n')[0], upgrade: /\r\nupgrade: websocket/i.test(head), at: Date.now() });
    });
    socket.pipe(upstream).pipe(socket);
    const destroy = () => {
      gatewaySockets.delete(socket);
      socket.destroy();
      upstream.destroy();
    };
    socket.on('error', destroy);
    upstream.on('error', destroy);
    socket.on('close', destroy);
    upstream.on('close', destroy);
  });
  await new Promise((done) => server.listen(port, '127.0.0.1', done));
  return server;
}

function createCertificates() {
  openssl(['req', '-x509', '-newkey', 'rsa:2048', '-nodes', '-keyout', 'ca.key', '-out', 'ca.pem', '-days', '2', '-subj',
    '/CN=AKENO Native E2E CA', '-addext', 'basicConstraints=critical,CA:TRUE', '-addext', 'keyUsage=critical,keyCertSign,cRLSign']);
  openssl(['req', '-newkey', 'rsa:2048', '-nodes', '-keyout', 'server.key', '-out', 'server.csr', '-subj', '/CN=127.0.0.1']);
  writeFileSync(join(work, 'ext.cnf'), 'subjectAltName=IP:127.0.0.1\nbasicConstraints=CA:FALSE\nkeyUsage=digitalSignature,keyEncipherment\nextendedKeyUsage=serverAuth\n');
  openssl(['x509', '-req', '-in', 'server.csr', '-CA', 'ca.pem', '-CAkey', 'ca.key', '-CAcreateserial', '-out', 'server.pem', '-days', '2', '-extfile', 'ext.cnf']);
}

// ---- The daemon -------------------------------------------------------------------------------

async function startDaemon(binary, dataDir, serverUrl) {
  const child = spawn(binary, ['--data-dir', dataDir, '--port', '0', '--server', serverUrl, '--ca-file', join(work, 'ca.pem')], {
    env: { ...process.env, AKENO_BACKOFF_BASE_MS: '300' },
    stdio: ['ignore', 'pipe', 'pipe'],
  });
  children.add(child);
  const state = { child, stdout: '', stderr: '', port: 0, token: '', exit: null };
  child.stdout.on('data', (chunk) => {
    state.stdout += chunk;
    const match = /"url":"http:\/\/127\.0\.0\.1:(\d+)\/#t=([0-9a-f]{64})"/.exec(state.stdout);
    if (match && !state.port) {
      state.port = Number(match[1]);
      state.token = match[2];
    }
  });
  child.stderr.on('data', (chunk) => (state.stderr += chunk));
  state.exited = new Promise((done) => child.on('exit', (code, signal) => {
    children.delete(child);
    state.exit = { code, signal };
    done(state.exit);
  }));
  await waitFor(() => state.port || state.exit, 'the daemon to start');
  assert.ok(state.port, state.stderr);
  return state;
}

/** A request to the daemon's local interface; raw so that every header is under control. */
function local(port, method, path, headers = {}, body) {
  return new Promise((done, fail) => {
    const socket = net.connect(port, '127.0.0.1', () => {
      const payload = body === undefined ? '' : JSON.stringify(body);
      const lines = [`${method} ${path} HTTP/1.1`, `Host: 127.0.0.1:${port}`, 'Connection: close'];
      for (const [k, v] of Object.entries(headers)) lines.push(`${k}: ${v}`);
      if (body !== undefined) lines.push('Content-Type: application/json', `Content-Length: ${Buffer.byteLength(payload)}`);
      socket.end(`${lines.join('\r\n')}\r\n\r\n${payload}`);
    });
    let data = '';
    socket.on('data', (chunk) => (data += chunk));
    socket.on('end', () => {
      const status = Number(/^HTTP\/1\.1 (\d+)/.exec(data)?.[1] ?? 0);
      const text = data.slice(data.indexOf('\r\n\r\n') + 4);
      let json;
      try {
        json = JSON.parse(text);
      } catch {
        json = null;
      }
      done({ status, body: json, text });
    });
    socket.on('error', fail);
  });
}

// ---- AKENO Native -------------------------------------------------------------------------------

function runNative(env, script, actions, timeoutMs = 180_000) {
  return new Promise((done) => {
    const child = spawn(nativeBinary, [], {
      env: { ...process.env, SDL_VIDEODRIVER: 'dummy', SDL_AUDIODRIVER: 'dummy', AKENO_NATIVE_WINDOW: '1920x1080', ...env, AKENO_NATIVE_SCRIPT: script.join(';') },
      stdio: ['ignore', 'pipe', 'pipe'],
    });
    children.add(child);
    let output = '';
    let pending = '';
    child.stdout.on('data', async (chunk) => {
      output += chunk;
      pending += chunk;
      let nl;
      while ((nl = pending.indexOf('\n')) >= 0) {
        const line = pending.slice(0, nl);
        pending = pending.slice(nl + 1);
        const m = /^AKN-SYNC ([A-Za-z0-9_-]+)$/.exec(line);
        if (!m) continue;
        try {
          if (actions[m[1]]) await actions[m[1]]();
        } catch (error) {
          output += `\nsync action ${m[1]} failed: ${error}\n`;
        }
        writeFileSync(join(env.AKENO_NATIVE_SYNC_DIR, m[1]), 'ok');
      }
    });
    child.stderr.on('data', (chunk) => (output += chunk));
    const timer = setTimeout(() => child.kill('SIGKILL'), timeoutMs);
    child.on('exit', (code, signal) => {
      clearTimeout(timer);
      children.delete(child);
      done({ code, signal, output });
    });
  });
}

// ---- Setup -----------------------------------------------------------------------------------

for (const required of ['apps/server/dist/main.js', 'apps/server/dist/cli.js', nativeBinary, daemonBinary, releaseBinary])
  if (!existsSync(resolve(root, required))) throw new Error(`Missing ${required}`);

const pgAdmin = new pg.Client({ connectionString: adminUrl });
await pgAdmin.connect();
await pgAdmin.query(`DROP DATABASE IF EXISTS ${databaseName} WITH (FORCE)`);
await pgAdmin.query(`CREATE DATABASE ${databaseName}`);
await pgAdmin.end();
assert.equal(spawnSync('node', ['apps/server/dist/cli.js', 'migrate'], { cwd: root, env: serverEnv, encoding: 'utf8' }).status, 0);
assert.equal(spawnSync('node', ['apps/server/dist/cli.js', 'create-admin', '--online-id', 'it_admin', '--password-stdin'],
  { cwd: root, env: serverEnv, input: ADMIN_PASSWORD, encoding: 'utf8' }).status, 0);
// LiveKit first: the server creates rooms through its API and receives its signed webhooks.
if (livekitBinary) {
  if (!existsSync(resolve(root, 'apps/voice-gateway/dist/main.js'))) throw new Error('Missing apps/voice-gateway/dist/main.js (pnpm build)');
  const config = join(work, 'livekit.yaml');
  writeFileSync(config, [
    `port: ${livekitPort}`, 'bind_addresses: ["127.0.0.1"]', 'rtc:', `  tcp_port: ${await freePort()}`, `  udp_port: ${await freePort()}`,
    '  use_external_ip: false', '  node_ip: 127.0.0.1', 'keys:', `  ${LK_KEY}: ${LK_SECRET}`, 'room:', '  auto_create: false',
    'webhook:', `  api_key: ${LK_KEY}`, '  urls:', `    - http://127.0.0.1:${backendPort}/api/v1/voice/webhook`, 'logging:', '  level: warn', '',
  ].join('\n'));
  const livekit = spawn(livekitBinary, ['--config', config], { stdio: ['ignore', 'ignore', 'pipe'] });
  children.add(livekit);
  await waitFor(async () => {
    try {
      return (await fetch(`http://127.0.0.1:${livekitPort}`)).ok;
    } catch {
      return false;
    }
  }, 'LiveKit');
  const gateway = spawn('node', ['apps/voice-gateway/dist/main.js'], {
    cwd: root,
    env: { ...process.env, GATEWAY_HOST: '127.0.0.1', GATEWAY_PORT: String(gatewayPort), LIVEKIT_URL: `ws://127.0.0.1:${livekitPort}`,
      LIVEKIT_API_KEY: LK_KEY, LIVEKIT_API_SECRET: LK_SECRET, LOG_LEVEL: 'info' },
    stdio: ['ignore', 'pipe', 'pipe'],
  });
  children.add(gateway);
  gateway.log = '';
  gateway.stdout.on('data', (d) => (gateway.log += d));
  gateway.stderr.on('data', (d) => (gateway.log += d));
  globalThis.voiceGateway = gateway;
  await waitFor(async () => {
    try {
      return (await fetch(`http://127.0.0.1:${gatewayPort}/healthz`)).ok;
    } catch {
      return false;
    }
  }, 'the voice gateway');
  await startTlsProxy(gatewayTlsPort, gatewayPort);
}
/** Starts the AKENO server; the voice revocation test restarts it while LiveKit keeps its rooms. */
async function startBackend() {
  const child = spawn('node', ['apps/server/dist/main.js'], { cwd: root, env: serverEnv, stdio: ['ignore', 'ignore', 'pipe'] });
  children.add(child);
  child.exited = new Promise((done) => child.once('exit', done));
  await waitFor(async () => {
    try {
      return (await fetch(`http://127.0.0.1:${backendPort}/health/ready`)).ok;
    } catch {
      return false;
    }
  }, 'the server');
  return child;
}
let backend = await startBackend();
const tlsPort = await freePort();
const tlsServer = await startTlsProxy(tlsPort);
const serverUrl = `https://127.0.0.1:${tlsPort}`;

const admin = await new WebSession().signIn('it_admin', ADMIN_PASSWORD);
const alice = await registerUser(admin, 'it_alice');
const bob = await registerUser(admin, 'it_bob');
const carol = await registerUser(admin, 'it_carol');
const request = await alice.request('POST', '/friends/requests', { userId: bob.user.id });
assert.equal(request.status, 201);
assert.equal((await bob.request('POST', `/friends/requests/${request.body.request.id}/accept`)).status, 200);
assert.equal((await bob.request('PATCH', '/profile', { bio: 'Speedrunner from the e2e test', statusMessage: 'Ranked tonight' })).status, 200);

const akenoDir = join(work, 'akeno');
const nativeDir = join(work, 'native');
const syncDir = join(work, 'sync');
mkdirSync(syncDir);
const shots = join(work, 'shots');
mkdirSync(shots);

log(`AKENO Native end-to-end tests (server ${serverUrl})`);
let daemon = null;
let key = '';

await step('native-IPC daemon: starts, writes a private key and announces the interface', async () => {
  mkdirSync(akenoDir, { mode: 0o700 });
  copyFileSync(join(work, 'ca.pem'), join(akenoDir, 'ca.pem')); // the native app trusts what the daemon trusts
  daemon = await startDaemon(daemonBinary, akenoDir, serverUrl);
  const keyPath = join(akenoDir, 'native.key');
  assert.equal(statSync(keyPath).mode & 0o777, 0o600);
  key = readFileSync(keyPath, 'utf8');
  assert.match(key, /^[0-9a-f]{64}$/);
  assert.notEqual(key, daemon.token, 'a separate secret from the page');
  const instance = await local(daemon.port, 'GET', '/api/instance');
  assert.deepEqual(instance.body, { app: 'akeno-network-console', version: '0.5.0-alpha', nativeIpc: 3 });
});

await step('native-IPC daemon: pairs through its page and the website; one realtime session', async () => {
  const started = await local(daemon.port, 'POST', '/api/pair/start', { 'X-Akeno-Token': daemon.token });
  assert.equal(started.status, 200, started.text);
  const state = (await local(daemon.port, 'GET', '/api/state', { 'X-Akeno-Token': daemon.token })).body;
  assert.equal((await alice.request('POST', '/devices/pairings/approve', { userCode: state.pairing.userCode })).status, 204);
  await waitFor(async () => {
    const s = (await local(daemon.port, 'GET', '/api/state', { 'X-Akeno-Token': daemon.token })).body;
    return s.paired && s.realtime.state === 'connected';
  }, 'pairing and the realtime connection', 30_000);
});

await step('native interface: the key opens only the native allow-list', async () => {
  const K = { 'X-Akeno-Native-Key': key };
  const hello = await local(daemon.port, 'GET', '/api/native/hello', K);
  assert.equal(hello.status, 200, hello.text);
  assert.equal(hello.body.ipc, 3);
  assert.equal(hello.body.writes, true);
  assert.equal(hello.body.paired, true);
  assert.equal(hello.body.onlineId, 'it_alice');
  assert.equal((await local(daemon.port, 'GET', '/api/native/hello')).body.error.code, 'SESSION_REQUIRED');
  const wrong = await local(daemon.port, 'GET', '/api/state', { 'X-Akeno-Native-Key': 'f'.repeat(64) });
  assert.equal(wrong.status, 403);
  assert.equal(wrong.body.error.code, 'NATIVE_KEY_INVALID');
  assert.equal((await local(daemon.port, 'GET', '/api/state', { 'X-Akeno-Native-Key': daemon.token })).body.error.code, 'NATIVE_KEY_INVALID',
    'the page secret is not a native key');
  assert.equal((await local(daemon.port, 'GET', '/api/state', { 'X-Akeno-Token': key })).body.error.code, 'SESSION_REQUIRED',
    'the native key is not a page secret');
  // A page (any Origin, even the daemon's own) can never use the native key.
  assert.equal((await local(daemon.port, 'GET', '/api/state', { ...K, Origin: `http://127.0.0.1:${daemon.port}` })).body.error.code, 'NATIVE_KEY_INVALID');
  assert.equal((await local(daemon.port, 'GET', '/api/state', { ...K, Origin: 'http://evil.example' })).body.error.code, 'BAD_ORIGIN');
  assert.equal((await local(daemon.port, 'GET', '/api/state', { 'X-Akeno-Native-Key': key, Host: 'evil.example' })).status, 400,
    'a second Host header is malformed');
  for (const [method, path] of [['POST', '/api/pair/start'], ['POST', '/api/pair/cancel'], ['POST', '/api/logout'], ['POST', '/api/exit'],
    ['POST', '/api/instance/show'], ['GET', '/api/diagnostics/audio'], ['POST', '/api/diagnostics/actionable-test'],
    ['POST', '/api/quick-access/test'], ['GET', '/api/quick-access'], ['POST', '/api/diagnostics/connection']]) {
    const refused = await local(daemon.port, method, path, K);
    assert.equal(refused.status, 403, `${method} ${path}`);
    // /api/instance/show needs the instance key and is refused before the native allow-list.
    assert.equal(refused.body.error.code, path === '/api/instance/show' ? 'FORBIDDEN' : 'NOT_ALLOWED_FOR_NATIVE', `${method} ${path}`);
  }
  for (const body of [{ server: 'https://evil.example' }, { deviceName: 'renamed' }, { quickAccessShortcut: true }, { notifySocial: 'no' }, {}]) {
    const refused = await local(daemon.port, 'POST', '/api/settings', K, body);
    assert.equal(refused.body.error.code, 'NOT_ALLOWED_FOR_NATIVE', JSON.stringify(body));
  }
  const allowed = await local(daemon.port, 'POST', '/api/settings', K, { notifySocial: false });
  assert.equal(allowed.status, 200, allowed.text);
  assert.equal((await local(daemon.port, 'GET', '/api/state', K)).body.notifications.social, false);
  await local(daemon.port, 'POST', '/api/settings', K, { notifySocial: true });
  // The daemon's own proxy allow-list still applies.
  assert.equal((await local(daemon.port, 'GET', '/api/v1/devices', K)).body.error.code, 'NOT_ALLOWED');
  const friends = await local(daemon.port, 'GET', '/api/v1/friends', K);
  assert.equal(friends.body.ok, true);
  assert.deepEqual(friends.body.data.friends.map((f) => f.onlineId), ['it_bob']);
  // IPC 2: only the listed writes, each an existing device route; IDs are one plain path segment.
  const uuid = '00000000-0000-4000-8000-000000000000';
  for (const [method, path] of [['PATCH', '/api/v1/profile'], ['POST', '/api/v1/profile'], ['DELETE', `/api/v1/friends/${uuid}`],
    ['POST', `/api/v1/parties/${uuid}/close`], ['POST', `/api/v1/parties/${uuid}/leader`], ['DELETE', `/api/v1/parties/${uuid}/members/${uuid}`],
    ['POST', `/api/v1/groups/${uuid}/messages`], ['POST', `/api/v1/parties/${uuid}/messages`], ['POST', '/api/v1/friends/requests'],
    ['POST', `/api/v1/invitations/${uuid}/cancel`], ['POST', '/api/v1/game-sessions'], ['DELETE', '/api/v1/devices/current'],
    ['POST', `/api/v1/conversations/${uuid}/messages?x=1`], ['POST', '/api/v1/conversations/../messages'],
    ['POST', `/api/v1/conversations/${uuid}/x/messages`], ['POST', '/api/v1/conversations//messages'], ['PUT', '/api/v1/presence/x'],
    ['POST', `/api/v1/conversations/${'a'.repeat(65)}/read`], ['POST', '/api/v1/conversations/a%2Fb/read'], ['PATCH', '/api/v1/presence'],
    ['POST', '/api/v1/conversationsX'], ['POST', '/api/v1/blocks']]) {
    const refused = await local(daemon.port, method, path, K, {});
    assert.equal(refused.status, 403, `${method} ${path}`);
    assert.equal(refused.body.error.code, 'NOT_ALLOWED_FOR_NATIVE', `${method} ${path}`);
  }
  // An allowed write reaches the server, which applies its own rules (here: an invalid body).
  const presence = await local(daemon.port, 'PUT', '/api/v1/presence', K, { status: 'away' });
  assert.equal(presence.body.ok, true, presence.text);
  assert.equal((await local(daemon.port, 'PUT', '/api/v1/presence', K, { status: 'online' })).body.ok, true);
  const invalid = await local(daemon.port, 'POST', `/api/v1/conversations/${uuid}/messages`, K, { body: '' });
  assert.equal(invalid.body.ok, false);
  assert.equal(invalid.body.status, 400);
  // The conversation open in AKENO Native.
  assert.equal((await local(daemon.port, 'POST', '/api/native/focus', K, { conversationId: null })).status, 200);
  assert.equal((await local(daemon.port, 'POST', '/api/native/focus', K, { conversationId: uuid })).status, 200);
  for (const body of [{ conversationId: '../x' }, { conversationId: 5 }, {}, { conversationId: 'a'.repeat(40) }])
    assert.equal((await local(daemon.port, 'POST', '/api/native/focus', K, body)).status, 400, JSON.stringify(body));
  assert.equal((await local(daemon.port, 'POST', '/api/native/focus', { 'X-Akeno-Token': daemon.token }, { conversationId: null })).status, 404,
    'the page has no focus route');
  // The page keeps working as before.
  assert.equal((await local(daemon.port, 'GET', '/api/state', { 'X-Akeno-Token': daemon.token })).status, 200);
  const daemonLog = readFileSync(join(akenoDir, 'akeno.log'), 'utf8');
  assert.match(daemonLog, /native\.ready Native interface 3 ready/);
  assert.match(daemonLog, /native\.write PUT \/presence/);
  // native.write names the route only (the client library's existing http.response line logs failed paths, as for the page).
  for (const line of daemonLog.split('\n').filter((l) => l.includes('native.write'))) assert.ok(!line.includes(uuid), line);
  assert.match(daemonLog, /native\.denied/);
  assert.ok(!daemonLog.includes(key), 'the key is never logged');
});

let nativeRun = null;
await step('AKENO Native: friends, live presence, a friend request and a profile change through the daemon', async () => {
  let bobSocket = null;
  const startedAt = Date.now();
  nativeRun = await runNative({ AKENO_NATIVE_AKENO_DIR: akenoDir, AKENO_NATIVE_DIR: nativeDir, AKENO_NATIVE_DAEMON_PORT: String(daemon.port),
    AKENO_NATIVE_SYNC_DIR: syncDir }, [
    'until ipc 15000', 'until friends 15000', 'wait 300', 'expect text Live from the AKENO daemon', 'expect text 0 ONLINE',
    'pad cross', 'expect screen friends', 'until friends 15000', 'wait 300', 'expect text @it_bob', 'expect text Offline',
    'expect text Ranked tonight', `shot ${shots}/e2e-friends-offline.png`,
    // bob opens the website: the server tells the daemon, the daemon's event ring tells AKENO Native
    'sync bob-online', 'until text 20000 1 online', 'expect text Online',
    // carol asks alice to be friends
    'expect text 0 friend requests waiting', 'sync carol-request', 'until text 20000 1 friend request waiting',
    // bob changes his status message
    'sync bob-status', 'until text 20000 Back at nine', `shot ${shots}/e2e-friends-live.png`,
    // bob's profile
    'pad cross', 'expect screen profile', 'until profile 15000', 'wait 300', 'expect text @it_bob', 'expect text Speedrunner from the e2e test',
    // the relationship pill of the redesigned profile (0.2 showed "· friend" after the online ID)
    'expect text Friends', 'expect notext Friend request sent', 'expect notext Wants to be your friend',
    // bob plays nothing and has no history: no NOW PLAYING card, the empty Recently Played state
    'expect notext NOW PLAYING', 'expect text RECENTLY PLAYED', 'expect text No games played recently.', `shot ${shots}/e2e-profile.png`, 'pad circle', 'pad circle', 'expect screen home',
    'expect text 1 ONLINE',
    // your own profile
    'pad right', 'pad right', 'pad right', 'pad right', 'expect focus profile', 'pad cross', 'until profile 15000', 'wait 300', 'expect text @it_alice',
    'expect text You', 'expect notext Friend request sent', 'pad circle',
    // activity
    'pad right', 'pad cross', 'expect screen activity', 'until activity 15000', 'wait 300', 'expect text YOUR RECENT GAMES',
    'expect text No games tracked yet.', 'expect text None of your friends is playing right now.', `shot ${shots}/e2e-activity.png`,
    'pad circle', 'pad circle', 'expect dialog exit', 'pad left', 'pad cross',
  ], {
    'bob-online': async () => {
      bobSocket = await bob.online();
    },
    'carol-request': async () => assert.equal((await carol.request('POST', '/friends/requests', { userId: alice.user.id })).status, 201),
    'bob-status': async () => assert.equal((await bob.request('PATCH', '/profile', { statusMessage: 'Back at nine' })).status, 200),
  });
  bobSocket?.close();
  assert.equal(nativeRun.code, 0, `native exit ${nativeRun.code} ${nativeRun.signal}\n${nativeRun.output.slice(-4000)}`);
  // One realtime session for this console: the daemon's, opened at pairing. AKENO Native opened none
  // and called the server only for its read-only connection check.
  const upgrades = tlsRequests.filter((r) => r.upgrade);
  assert.equal(upgrades.length, 1, `realtime sessions: ${upgrades.map((r) => r.line).join(' | ')}`);
  assert.ok(upgrades[0].at < startedAt, 'the session predates AKENO Native');
  const report = readFileSync(join(nativeDir, 'native-report.txt'), 'utf8');
  assert.match(report, /State:\s+Connected to the AKENO daemon: protocol 3, daemon 0\.5\.0-alpha \([0-9a-f]+-linux-native-ipc\)/);
  assert.match(report, /Friends:\s+1 \(1 online, 0 playing\), 1 incoming and 0 outgoing requests/);
  // Browsing friends, profiles and activity writes nothing.
  assert.match(report, /Requests:\s+\d+ \(0 writes, 0 failed\), 0 errors, \d+ event polls, [1-9]\d* events, 1 connects/);
  assert.match(report, /Signed in as:\s+it_alice \(@it_alice\)/);
  const nativeLog = readFileSync(join(nativeDir, 'native.log'), 'utf8');
  for (const text of [report, nativeLog]) {
    assert.ok(!text.includes(key), 'no native key in the native report or log');
    for (const other of ['it_bob', 'it_carol', 'Speedrunner', 'Back at nine']) assert.ok(!text.includes(other), `no data of others (${other})`);
  }
  assert.ok(readdirSync(akenoDir).every((f) => ['akeno.log', 'ca.pem', 'device.json', 'instance.key', 'native.key', 'settings.json'].includes(f)),
    `daemon folder: ${readdirSync(akenoDir)}`);
});

await step('AKENO Native: messages, the controller keyboard, party, requests and settings through the write list', async () => {
  // bob wrote to alice on the website before she opened AKENO Native
  const conv = await bob.request('POST', '/conversations', { userId: alice.user.id });
  assert.equal(conv.status, 200, JSON.stringify(conv.body));
  const convId = conv.body.id;
  const say = async (who, body) => {
    const r = await who.request('POST', `/conversations/${convId}/messages`, { body, clientMessageId: randomUUID() });
    assert.equal(r.status, 201, JSON.stringify(r.body));
  };
  await say(bob, 'Hi from the website');
  const daemonOutAt = () => daemon.stdout.length;
  const checks = {};
  let leftAt = 0;
  let partyId = null;
  const run = await runNative({ AKENO_NATIVE_AKENO_DIR: akenoDir, AKENO_NATIVE_DIR: nativeDir, AKENO_NATIVE_DAEMON_PORT: String(daemon.port),
    AKENO_NATIVE_SYNC_DIR: syncDir }, [
    'until ipc 15000', 'until friends 15000', 'wait 500',
    // Messages: the conversation with its unread message, then the thread
    'pad right', 'expect focus messages', 'pad cross', 'expect screen messages', 'until text 15000 Hi from the website',
    'expect text 1 unread message', `shot ${shots}/e2e-messages.png`,
    'pad cross', 'expect screen chat', 'until text 15000 Hi from the website', 'expect text Press Cross to write a message',
    // alice answers with the controller keyboard: H (Shift is on for the first letter), i, space, 8, 8
    'pad cross', 'expect dialog keyboard', 'pad down', 'pad right', 'pad right', 'pad right', 'pad right', 'pad right', 'pad cross',
    'pad up', 'pad right', 'pad right', 'pad cross', 'pad triangle', 'pad up', 'pad cross', 'pad cross', 'expect text Hi 88',
    `shot ${shots}/e2e-keyboard.png`, 'pad options', 'expect dialog none', 'sync check-sent',
    // bob answers while the conversation is open: it appears live, and the daemon shows no system notification for it
    'sync bob-reply', 'until text 20000 Reply while open', `shot ${shots}/e2e-chat.png`, 'wait 1500', 'sync check-quiet',
    // leaving the conversation: the next message is a system notification again
    'pad circle', 'expect screen messages', 'pad circle', 'expect screen home', 'wait 800', 'sync bob-after',
    // Party: an invitation arrives live; accept it, open the party chat, leave
    'pad right', 'expect focus parties', 'pad cross', 'expect screen party', 'until text 15000 Create a party', 'sync bob-party',
    'until text 20000 invited you', 'pad down', 'pad cross', 'until text 20000 YOUR PARTY', 'until text 20000 Party chat', 'sync check-joined',
    `shot ${shots}/e2e-party.png`,
    'pad up', 'expect text Leave the party', 'pad cross', 'expect screen chat', 'expect text Party chat', 'pad circle', 'expect screen party',
    'pad down', 'pad down', 'pad cross', 'expect dialog confirm', 'pad left', 'pad cross', 'until text 20000 Create a party', 'sync check-left',
    // Inbox: carol's friend request, accepted here
    'pad circle', 'expect screen home', 'pad right', 'expect focus inbox', 'pad cross', 'expect screen inbox',
    'until text 15000 wants to be your friend', `shot ${shots}/e2e-inbox.png`, 'pad cross', 'until text 20000 are now friends', 'sync check-carol',
    // Settings: a daemon setting and your status
    'pad circle', 'pad right', 'pad right', 'pad right', 'expect focus settings', 'pad cross', 'expect screen settings',
    'until text 10000 Message notifications', 'wait 500', 'pad cross', 'until text 10000 Message notifications off.', 'sync check-setting-off',
    'pad cross', 'until text 10000 Message notifications on.', 'pad down', 'pad down', 'pad down', 'pad cross', 'until text 10000 Your status is now Away',
    'sync check-away', `shot ${shots}/e2e-settings.png`, 'pad square', 'until text 10000 Your status is now Online',
    'pad circle', 'pad circle', 'expect dialog exit', 'pad left', 'pad cross',
  ], {
    'check-sent': async () => {
      await waitFor(async () => {
        const page = await bob.request('GET', `/conversations/${convId}/messages`);
        return page.body.messages.some((m) => m.body === 'Hi 88');
      }, 'the message on the server', 15_000);
      const page = await bob.request('GET', `/conversations/${convId}/messages`);
      checks.sent = page.body.messages.filter((m) => m.body === 'Hi 88');
      // read on the server: alice's unread count is 0
      await waitFor(async () => (await alice.request('GET', '/conversations')).body.conversations.find((c) => c.id === convId).unreadCount === 0,
        'the conversation marked read', 10_000);
      checks.read = true;
    },
    'bob-reply': async () => {
      checks.quietFrom = daemonOutAt();
      await say(bob, 'Reply while open');
    },
    'check-quiet': async () => {
      checks.quietOut = daemon.stdout.slice(checks.quietFrom);
    },
    'bob-after': async () => {
      leftAt = daemonOutAt();
      await say(bob, 'Reply after leaving');
      await waitFor(() => daemon.stdout.slice(leftAt).includes('Reply after leaving'), 'a system notification after leaving', 10_000);
      checks.notifiedAfter = true;
    },
    'bob-party': async () => {
      const party = await bob.request('POST', '/parties', { name: 'E2E party', privacy: 'invite_only' });
      assert.equal(party.status, 201, JSON.stringify(party.body));
      partyId = party.body.id;
      const inv = await bob.request('POST', `/parties/${partyId}/invitations`, { userId: alice.user.id });
      assert.equal(inv.status, 201, JSON.stringify(inv.body));
    },
    'check-joined': async () => {
      const p = await bob.request('GET', `/parties/${partyId}`);
      checks.joined = p.body.memberCount;
    },
    'check-left': async () => {
      await waitFor(async () => (await bob.request('GET', `/parties/${partyId}`)).body.memberCount === 1, 'alice left the party', 10_000);
      checks.left = true;
    },
    'check-carol': async () => {
      await waitFor(async () => (await carol.request('GET', '/friends')).body.friends.some((f) => f.id === alice.user.id), 'carol and alice friends', 10_000);
      checks.carol = true;
    },
    'check-setting-off': async () => {
      checks.notifyOff = (await local(daemon.port, 'GET', '/api/state', { 'X-Akeno-Token': daemon.token })).body.notifications.messages;
    },
    'check-away': async () => {
      await waitFor(async () => (await bob.request('GET', '/friends')).body.friends.find((f) => f.id === alice.user.id)?.presence.status === 'away',
        'alice away for bob', 10_000);
      checks.away = true;
    },
  });
  assert.equal(run.code, 0, `native exit ${run.code} ${run.signal}\n${run.output.slice(-4000)}`);
  assert.equal(checks.sent?.length, 1, 'the message reached the server exactly once');
  assert.equal(checks.sent[0].sender.id, alice.user.id);
  assert.ok(checks.read, 'the open conversation was marked read on the server');
  assert.ok(checks.quietOut !== undefined && !checks.quietOut.includes('Reply while open'),
    'no system notification for a message in the conversation open in AKENO Native');
  assert.ok(checks.notifiedAfter, 'a system notification once the conversation is closed');
  assert.equal(checks.joined, 2, 'accepting the invitation joined the party');
  assert.ok(checks.left, 'leaving the party reached the server');
  assert.ok(checks.carol, 'accepting the friend request reached the server');
  assert.equal(checks.notifyOff, false, 'the daemon setting changed through the native interface');
  assert.ok(checks.away, 'the status change reached friends');
  // still one realtime session for the console: AKENO Native opened none
  assert.equal(tlsRequests.filter((r) => r.upgrade).length, 1);
  const report = readFileSync(join(nativeDir, 'native-report.txt'), 'utf8');
  assert.match(report, /Requests:\s+\d+ \([1-9]\d* writes, 0 failed\)/);
  const nativeLog = readFileSync(join(nativeDir, 'native.log'), 'utf8');
  for (const text of [report, nativeLog])
    for (const secret of [key, 'Hi 88', 'Reply while open', 'Hi from the website', 'E2E party', convId]) assert.ok(!text.includes(secret), `not logged: ${secret}`);
  const daemonLog = readFileSync(join(akenoDir, 'akeno.log'), 'utf8');
  for (const route of ['POST /conversations/:id/messages', 'POST /conversations/:id/read', 'POST /invitations/:id/accept', 'POST /parties/:id/leave',
    'POST /friends/requests/:id/accept', 'PUT /presence'])
    assert.ok(daemonLog.includes(`native.write ${route}`), route);
});

/** Counts the pixels of a screenshot (8-bit RGBA PNG as SDL writes it) close to a colour. */
function countColour(path, rgb, tolerance) {
  const buf = readFileSync(path);
  let pos = 8;
  let width = 0;
  let height = 0;
  const idat = [];
  while (pos < buf.length) {
    const len = buf.readUInt32BE(pos);
    const type = buf.toString('ascii', pos + 4, pos + 8);
    if (type === 'IHDR') {
      width = buf.readUInt32BE(pos + 8);
      height = buf.readUInt32BE(pos + 12);
    } else if (type === 'IDAT') idat.push(buf.subarray(pos + 8, pos + 8 + len));
    pos += 12 + len;
  }
  const raw = inflateSync(Buffer.concat(idat));
  const stride = width * 4 + 1;
  let n = 0;
  for (let y = 0; y < height; y++) {
    if (raw[y * stride] !== 0) throw new Error('unexpected PNG filter');
    for (let x = 0; x < width; x++) {
      const o = y * stride + 1 + x * 4;
      if (Math.abs(raw[o] - rgb[0]) <= tolerance && Math.abs(raw[o + 1] - rgb[1]) <= tolerance && Math.abs(raw[o + 2] - rgb[2]) <= tolerance) n++;
    }
  }
  return n;
}

await step('AKENO Native: an uninstalled game keeps its administrator-verified cover (no local icon)', async () => {
  // An administrator verifies the title and uploads its cover (solid magenta, so the screenshot can find it).
  const TITLE = 'PPSA09999';
  const game = await admin.request('POST', '/admin/games', { name: 'Magenta Run', titleIds: [TITLE] });
  assert.equal(game.status, 201, JSON.stringify(game.body));
  const sharp = serverRequire('sharp');
  const cover = await sharp({ create: { width: 600, height: 900, channels: 3, background: { r: 255, g: 0, b: 255 } } }).jpeg({ quality: 95 }).toBuffer();
  const upload = await fetch(`http://127.0.0.1:${backendPort}/api/v1/admin/games/${game.body.id}/artwork/cover`, {
    method: 'PUT', body: cover, headers: { 'Content-Type': 'image/jpeg', Origin: origin, Cookie: admin.cookie, 'X-CSRF-Token': admin.csrf },
  });
  assert.equal(upload.status, 200, await upload.text());
  // This console played the game (its detection reports the title with the console's own credential), then it was closed.
  const device = JSON.parse(readFileSync(join(akenoDir, 'device.json'), 'utf8'));
  const asConsole = (path, body) => fetch(`http://127.0.0.1:${backendPort}/api/v1${path}`, {
    method: 'POST', body: JSON.stringify(body), headers: { 'Content-Type': 'application/json', Authorization: `Bearer ${device.accessToken}` },
  });
  assert.equal((await asConsole('/activity/heartbeat', { titleId: TITLE, titleName: 'Console name of the game', platform: 'PS5' })).status, 200);
  assert.equal((await asConsole('/activity/stop', { reason: 'closed' })).status, 200);
  const history = await alice.request('GET', '/users/it_alice/activity');
  const entry = history.body.recentGames.find((g) => g.titleId === TITLE);
  assert.ok(entry?.art?.cover, 'the server attaches the verified cover to the history entry');
  // The daemon has no metadata root here: the game is not installed, there is no local icon0.png.
  const run = await runNative({ AKENO_NATIVE_AKENO_DIR: akenoDir, AKENO_NATIVE_DIR: nativeDir, AKENO_NATIVE_DAEMON_PORT: String(daemon.port),
    AKENO_NATIVE_SYNC_DIR: syncDir }, [
    'until ipc 15000', 'until friends 15000', 'wait 300', 'pad right', 'pad right', 'pad right', 'pad right', 'expect focus profile', 'pad cross',
    'until profile 15000', 'wait 300', 'expect text RECENTLY PLAYED', 'expect text Magenta Run', 'wait 500', 'until pictures 15000', 'wait 300',
    `shot ${shots}/e2e-artwork-uninstalled.png`, 'pad circle', 'pad circle', 'expect dialog exit', 'pad left', 'pad cross',
  ], {});
  assert.equal(run.code, 0, `native exit ${run.code} ${run.signal}\n${run.output.slice(-3000)}`);
  // A 160 x 240 cover card is drawn in the server's magenta (a placeholder would be dark blue with initials).
  const magenta = countColour(join(shots, 'e2e-artwork-uninstalled.png'), [255, 0, 255], 24);
  assert.ok(magenta > 20000, `the verified cover is on screen (${magenta} magenta pixels)`);
  const report = readFileSync(join(nativeDir, 'native-report.txt'), 'utf8');
  assert.match(report, /Pictures:\s+\d+ asked for, [1-9]\d* shown/);
});

if (companionBinary) await step('In-Game Companion (experimental): the pages from the real server; status, invitation, reply, read, message, party and friend-request presses', async () => {
  // asynchronous: this process also runs the TLS terminator the daemon talks to
  const check = (...extra) => new Promise((done) => {
    const child = spawn(companionBinary, ['--port', String(daemon.port), '--akeno-dir', akenoDir, ...extra], { stdio: ['ignore', 'pipe', 'pipe'] });
    let stdout = '';
    let stderr = '';
    child.stdout.on('data', (d) => (stdout += d));
    child.stderr.on('data', (d) => (stderr += d));
    child.on('exit', (status) => done({ status, stdout, stderr }));
  });
  // bob writes to alice and invites her to a new party
  const conv = await bob.request('POST', '/conversations', { userId: alice.user.id });
  assert.equal((await bob.request('POST', `/conversations/${conv.body.id}/messages`, { body: 'gg, party?', clientMessageId: randomUUID() })).status, 201);
  const party = await bob.request('POST', '/parties', { name: 'Companion party', privacy: 'invite_only' });
  assert.equal(party.status, 201, JSON.stringify(party.body));
  assert.equal((await bob.request('POST', `/parties/${party.body.id}/invitations`, { userId: alice.user.id })).status, 201);
  let r = await check();
  assert.equal(r.status, 0, r.stderr + r.stdout);
  assert.match(r.stdout, /AKENO QUICK MENU/);
  assert.match(r.stdout, /Friends\s+\d+ online/);
  // bob's unread "Reply after leaving" from the messages step, and the new one
  assert.match(r.stdout, /Messages\s+2 unread in 1 conversation/);
  assert.match(r.stdout, /Party\s+Not in a party/);
  assert.match(r.stdout, /Invitations\s+1 party invitation/);
  assert.match(r.stdout, /Status\s+Online as @it_alice/);
  assert.match(r.stdout, /it_bob \(2\): gg, party\?/);
  assert.match(r.stdout, /Invitation: it_bob invited you to "Companion party"/);
  assert.match(r.stdout, /OnionHEN page: valid, \d+ nodes/);
  // the two quick actions, through the daemon's write list
  r = await check('--presence', 'away', '--accept-invite');
  assert.equal(r.status, 0, r.stderr + r.stdout);
  assert.match(r.stdout, /Status\s+Away as @it_alice/);
  assert.match(r.stdout, /Party\s+Companion party, 2 members/);
  assert.equal((await bob.request('GET', `/parties/${party.body.id}`)).body.memberCount, 2);
  await waitFor(async () => (await bob.request('GET', '/friends')).body.friends.find((f) => f.id === alice.user.id)?.presence.status === 'away',
    'alice away for bob', 10_000);
  assert.equal((await check('--presence', 'online')).status, 0);

  // The pages (1.10) and their presses, delivered as onionHEN delivers them from the PS5: the node
  // ID that names the item, and for a text field the text typed with the PS5 keyboard. A run
  // presses several controls in a row and reloads only what each press changed, like the plugin
  // (the server limits each account's requests per minute; the console shares that budget).
  // 1. a reply on bob's conversation page, then Mark as read
  r = await check('--pages', '--event', `reply.${conv.body.id}`, 'on my way', '--event', `read.${conv.body.id}`);
  assert.equal(r.status, 0, r.stderr + r.stdout);
  assert.match(r.stdout, /Action reply\.[0-9a-f-]+: ok: Sent to .+\./);
  assert.match(r.stdout, /Action read\.[0-9a-f-]+: ok: Marked .+ as read\./);
  assert.match(r.stdout, new RegExp(`Conversation [^\\n]+ \\[${conv.body.id}\\]:\\n(  [^\\n]*\\n)*  [^\\n:]+: gg, party\\?\\n  You: on my way\\n`));
  assert.doesNotMatch(r.stdout, /Unread:\n/);
  assert.match(r.stdout, new RegExp(`\\nPage c\\.${conv.body.id}\\n`));
  assert.match(r.stdout, new RegExp(`\\nPage f\\.${bob.user.id}\\n`));
  assert.match(r.stdout, /\nMember [^\n]+ \(leader\)\n/);
  const thread = await bob.request('GET', `/conversations/${conv.body.id}/messages`);
  assert.ok(thread.body.messages.some((m) => m.body === 'on my way' && m.sender.id === alice.user.id), 'bob has the reply');
  assert.equal((await alice.request('GET', '/conversations')).body.conversations.find((c) => c.id === conv.body.id).unreadCount, 0);
  // 2. an empty reply sends nothing; a message from carol's friend page; leave bob's party; create one
  r = await check('--event', `reply.${conv.body.id}`, '   ', '--event', `dm.${carol.user.id}`, 'hi carol', '--event', `pleave.${party.body.id}`,
    '--event', 'pcreate', 'Companion night');
  assert.match(r.stdout, /Action reply\.[0-9a-f-]+: failed: Nothing to send/);
  assert.match(r.stdout, /Action dm\.[0-9a-f-]+: ok: Sent to .+\./);
  assert.match(r.stdout, /Action pleave\.[0-9a-f-]+: ok: You left the party\./);
  assert.match(r.stdout, /Action pcreate: ok: Party created\./);
  assert.match(r.stdout, /Party\s+Companion night, 1 member/);
  await waitFor(async () => (await carol.request('GET', '/conversations')).body.conversations.some((c) => c.otherUser.id === alice.user.id && c.lastMessage?.body === 'hi carol'),
    'carol has the message', 10_000);
  assert.equal((await bob.request('GET', `/parties/${party.body.id}`)).body.memberCount, 1);
  // 3. invite bob from the party (leaving it would close it and withdraw the invitation)
  const mine = (await alice.request('GET', '/parties/current')).body.party;
  r = await check('--event', `pinv.${bob.user.id}`);
  assert.equal(r.status, 0, r.stderr + r.stdout);
  assert.match(r.stdout, /Action pinv\.[0-9a-f-]+: ok: Invited .+ to your party\./);
  assert.ok((await bob.request('GET', '/invitations')).body.invitations.some((i) => i.target?.id === mine.id), 'bob is invited');
  // 4. leave it; friend requests: accept dave's, decline erin's; a control that is not the companion's
  const dave = await registerUser(admin, 'it_dave');
  const daveAsks = await dave.request('POST', '/friends/requests', { userId: alice.user.id });
  assert.equal(daveAsks.status, 201, JSON.stringify(daveAsks.body));
  const erin = await registerUser(admin, 'it_erin');
  const erinAsks = await erin.request('POST', '/friends/requests', { userId: alice.user.id });
  assert.equal(erinAsks.status, 201, JSON.stringify(erinAsks.body));
  r = await check('--event', `pleave.${mine.id}`, '--event', `racc.${daveAsks.body.request.id}`, '--event', `rdec.${erinAsks.body.request.id}`,
    '--event', 'id_onion_ui_0000000000000000');
  assert.match(r.stdout, /Action pleave\.[0-9a-f-]+: ok: You left the party\./);
  assert.match(r.stdout, /Party\s+Not in a party/);
  assert.match(r.stdout, /Action racc\.[0-9a-f-]+: ok: Friend request accepted\./);
  assert.match(r.stdout, /Action rdec\.[0-9a-f-]+: ok: Friend request declined\./);
  assert.match(r.stdout, /Action id_onion_ui_0+: unknown/);
  assert.ok((await dave.request('GET', '/friends')).body.friends.some((f) => f.id === alice.user.id), 'dave and alice are friends');
  assert.ok(!(await erin.request('GET', '/friends')).body.friends.some((f) => f.id === alice.user.id), 'erin was declined');
  // still the daemon's single realtime session; the companion opened none
  assert.equal(tlsRequests.filter((x) => x.upgrade).length, 1);
});

/** Single-tone frequency of a block by zero crossings (0 when quiet). */
function toneHz(pcm, rate = 48000) {
  let energy = 0;
  for (const v of pcm) energy += v * v;
  if (pcm.length < 64 || Math.sqrt(energy / pcm.length) < 300) return 0;
  let crossings = 0;
  let first = -1;
  let last = -1;
  for (let i = 1; i < pcm.length; i++) {
    if ((pcm[i - 1] < 0) !== (pcm[i] < 0)) {
      if (first < 0) first = i;
      last = i;
      crossings++;
    }
  }
  return crossings < 3 ? 0 : Math.round(rate / ((2 * (last - first)) / (crossings - 1)));
}

if (livekitBinary) await step('Party voice: AKENO Native and a website member talk both ways through the voice gateway; mute, deafen, volume, reconnect, revocation', async () => {
  const rtc = createRequire(resolve(root, 'apps/voice-gateway/package.json'))('@livekit/rtc-node');
  // A fresh party: alice (this console's account) creates it on the website, bob joins.
  for (const who of [alice, bob]) {
    const current = (await who.request('GET', '/parties/current')).body?.party;
    if (current) await who.request('POST', `/parties/${current.id}/leave`);
  }
  const party = await alice.request('POST', '/parties', { name: 'Voice night', privacy: 'friends' });
  assert.equal(party.status, 201, JSON.stringify(party.body));
  assert.equal((await bob.request('POST', `/parties/${party.body.id}/join`)).status, 200);
  assert.equal(party.body.voice.available, true, 'the server offers party voice');

  // bob joins voice like a browser does (LiveKit directly, his website session's token) and says "600 Hz".
  const pass = await bob.request('POST', `/parties/${party.body.id}/voice/token`);
  assert.equal(pass.status, 200, JSON.stringify(pass.body));
  const room = new rtc.Room();
  await room.connect(pass.body.url, pass.body.token, { autoSubscribe: true, dynacast: false });
  const heard = []; // what bob hears from alice: { at, hz }
  let alicePublished = 0;
  let aliceUnpublished = 0;
  room.on(rtc.RoomEvent.TrackPublished, (_p, participant) => participant.identity === alice.user.id && alicePublished++);
  room.on(rtc.RoomEvent.TrackUnpublished, (_p, participant) => participant.identity === alice.user.id && aliceUnpublished++);
  room.on(rtc.RoomEvent.TrackSubscribed, (track, _pub, participant) => {
    if (track.kind !== rtc.TrackKind.KIND_AUDIO || participant.identity !== alice.user.id) return;
    void (async () => {
      for await (const frame of new rtc.AudioStream(track, { sampleRate: 48000, numChannels: 1, frameSizeMs: 20 })) heard.push({ at: Date.now(), hz: toneHz(frame.data) });
    })();
  });
  const source = new rtc.AudioSource(48000, 1);
  const options = new rtc.TrackPublishOptions();
  options.source = rtc.TrackSource.SOURCE_MICROPHONE;
  await room.localParticipant.publishTrack(rtc.LocalAudioTrack.createAudioTrack('microphone', source), options);
  let speaking = true;
  let phase = 0;
  const bobVoice = (async () => {
    while (speaking) {
      const pcm = new Int16Array(960);
      for (let i = 0; i < 960; i++) {
        pcm[i] = Math.round(9000 * Math.sin(phase));
        phase += (2 * Math.PI * 600) / 48000;
      }
      await source.captureFrame(new rtc.AudioFrame(pcm, 48000, 1, 960));
    }
  })();
  const hearsAlice = (since, min = 15) => heard.filter((h) => h.at >= since && Math.abs(h.hz - 300) <= 10).length >= min;
  const voiceDir = join(work, 'native-voice');
  const stats = () => {
    const text = existsSync(join(voiceDir, 'native.log')) ? readFileSync(join(voiceDir, 'native.log'), 'utf8') : '';
    return [...text.matchAll(/voice\.stats.*$/gm)].map((m) => m[0]);
  };
  const device = (await alice.request('GET', '/devices')).body.devices.find((d) => d.platform.includes('PlayStation'));
  assert.ok(device, 'the console is paired');
  let mark = 0;
  const run = await runNative({ AKENO_NATIVE_AKENO_DIR: akenoDir, AKENO_NATIVE_DIR: voiceDir, AKENO_NATIVE_DAEMON_PORT: String(daemon.port),
    AKENO_NATIVE_SYNC_DIR: syncDir, AKENO_NATIVE_SIM_MIC: 'tone:300' }, [
    'until ipc 15000', 'until friends 15000', 'wait 300', 'pad right', 'pad right', 'pad cross', 'expect screen party',
    'until text 15000 Join voice', 'pad down', 'pad down', 'pad down', 'pad cross', 'expect dialog confirm',
    'expect text Join voice with your microphone?', 'sync voice-consent', 'pad left', 'pad cross',
    'until text 25000 In voice', 'sync voice-live', `shot ${shots}/e2e-voice.png`,
    // mute and unmute
    'pad down', 'expect text Mute microphone', 'pad cross', 'wait 800', 'sync voice-muted', 'pad cross', 'wait 800', 'sync voice-unmuted',
    // deafen (also mutes), undeafen, unmute
    'pad down', 'pad cross', 'wait 2500', 'sync voice-deafened', 'pad cross', 'pad up', 'expect text Unmute microphone', 'pad cross', 'wait 500',
    // the voice volume of the others: 100 % -> 50 %
    'pad down', 'pad down', 'pad left', 'pad left', 'pad left', 'pad left', 'pad left', 'wait 300', 'expect text Voice volume: 50%',
    // the connection drops: AKENO Native reconnects with a new voice pass
    'sync voice-drop', 'until text 30000 In voice', 'sync voice-back',
    // the console loses "parties:write" on the website: its voice ends at once
    'sync voice-revoke', 'until text 20000 removed from the voice chat', `shot ${shots}/e2e-voice-removed.png`, 'sync voice-restore',
    'pad circle', 'pad circle', 'expect dialog exit', 'pad left', 'pad cross',
  ], {
    'voice-consent': async () => {
      // nothing opened before the user agreed
      assert.ok(!existsSync(join(voiceDir, 'native.log')) || !/voice\.mic/.test(readFileSync(join(voiceDir, 'native.log'), 'utf8')), 'no microphone before consent');
      assert.equal(JSON.parse((await fetch(`http://127.0.0.1:${gatewayPort}/healthz`).then((r) => r.text()))).sessions, 0);
    },
    'voice-live': async () => {
      mark = Date.now();
      await waitFor(() => hearsAlice(mark), 'bob hears the console (300 Hz)', 15_000);
      await waitFor(() => stats().some((l) => /rx_tone=(59\d|60\d|61\d)\b/.test(l)), 'the console plays bob (600 Hz)', 15_000);
      const members = (await bob.request('GET', `/parties/${party.body.id}`)).body.members;
      assert.equal(members.find((m) => m.id === alice.user.id)?.voice?.connected, true, 'the server sees alice in voice');
    },
    'voice-muted': async () => {
      await waitFor(() => aliceUnpublished > 0, 'the console microphone track ends', 5000);
      await sleep(500);
      mark = Date.now();
      await sleep(1000);
      assert.ok(!hearsAlice(mark, 3), 'nobody hears a muted console');
    },
    'voice-unmuted': async () => {
      mark = Date.now();
      await waitFor(() => alicePublished > 1 && hearsAlice(mark), 'bob hears the console again', 10_000);
    },
    'voice-deafened': async () => {
      const last = stats().at(-1) ?? '';
      assert.match(last, /deafened=1/);
      assert.match(last, /rx_level=0\b/, `nothing is played while deafened: ${last}`);
      assert.match(last, /muted=1/);
    },
    'voice-drop': async () => {
      for (const socket of gatewaySockets) socket.destroy();
      await sleep(300);
    },
    'voice-back': async () => {
      mark = Date.now();
      await waitFor(() => hearsAlice(mark), 'bob hears the console after the reconnect', 15_000);
    },
    'voice-revoke': async () => {
      const scopes = device.scopes.filter((scope) => scope !== 'parties:write');
      assert.equal((await alice.request('PATCH', `/devices/${device.id}`, { scopes })).status, 200);
    },
    'voice-restore': async () => {
      assert.equal((await alice.request('PATCH', `/devices/${device.id}`, { scopes: device.scopes })).status, 200);
    },
  });
  speaking = false;
  await bobVoice.catch(() => undefined);
  await room.disconnect();
  assert.equal(run.code, 0, `native exit ${run.code} ${run.signal}\n${run.output.slice(-4000)}\n${globalThis.voiceGateway?.log.slice(-3000)}`);
  const report = readFileSync(join(voiceDir, 'native-report.txt'), 'utf8');
  const logText = readFileSync(join(voiceDir, 'native.log'), 'utf8');
  assert.match(report, /\[Voice\]/);
  assert.match(report, /Codec:\s+libopus 1\.5\.2/);
  assert.match(report, /State:\s+Voice stopped - You were removed from the voice chat\./);
  const packets = /Packets:\s+(\d+) sent, (\d+) received/.exec(report);
  assert.ok(packets && Number(packets[1]) > 200 && Number(packets[2]) > 200, `audio went both ways: ${packets?.[0]}`);
  assert.match(report, /Controls:\s+not muted, hearing, output 50%, microphone 100%/);
  assert.match(logText, /voice\.mic: Microphone open \(simulated microphone \(PC test\), 16000 Hz/);
  assert.match(logText, /voice\.mic: Microphone closed/);
  assert.match(logText, /voice\.closed: .*code 4003/);
  // no voice pass, token or key in what AKENO Native writes
  assert.doesNotMatch(report + logText, /eyJ[A-Za-z0-9_-]{10,}/);
  assert.ok(!logText.includes(key) && !report.includes(key));
  // the daemon passed exactly the voice route through its write list, logging the route only
  const daemonLog = readFileSync(join(akenoDir, 'akeno.log'), 'utf8');
  assert.match(daemonLog, /native\.write POST \/parties\/:id\/voice\/device-token/);
  for (const line of daemonLog.split('\n').filter((l) => /native\.write/.test(l))) assert.ok(!line.includes(party.body.id), line);
  // still one realtime session to the AKENO server (voice uses the gateway, not the server)
  assert.equal(tlsRequests.filter((x) => x.upgrade).length, 1);
});

/** Pairs an API-only console for a user (the device flow the daemon uses) and returns its bearer token and ID. */
async function pairApiDevice(owner, name) {
  const api = (method, path, body, token) => fetch(`http://127.0.0.1:${backendPort}/api/v1${path}`, {
    method, headers: { ...(body ? { 'Content-Type': 'application/json' } : {}), ...(token ? { Authorization: `Bearer ${token}` } : {}) },
    body: body ? JSON.stringify(body) : undefined,
  }).then(async (r) => ({ status: r.status, body: await r.json().catch(() => null) }));
  const started = await api('POST', '/devices/pairings', { deviceName: name, platform: 'PlayStation 5 (homebrew)', clientVersion: '1.1.0-beta.1' });
  assert.equal(started.status, 201, JSON.stringify(started.body));
  assert.equal((await owner.request('POST', '/devices/pairings/approve', { userCode: started.body.userCode })).status, 204);
  let redeemed = null;
  await waitFor(async () => {
    redeemed = await api('POST', '/devices/pairings/token', { deviceCode: started.body.deviceCode });
    if (redeemed.status === 400 && redeemed.body?.error?.code === 'SLOW_DOWN') await sleep(1000);
    return redeemed.status === 200;
  }, `pairing ${name}`, 30_000);
  const token = redeemed.body.accessToken;
  const current = await api('GET', '/devices/current', undefined, token);
  return { token, id: current.body.device.id, pass: (partyId) => api('POST', `/parties/${partyId}/voice/device-token`, undefined, token) };
}

// The two cases of the security review of 196ca13, against the real LiveKit server and its webhooks. A console's voice
// pass is a LiveKit token (the gateway only carries audio), so the connection here joins LiveKit with it directly.
if (livekitBinary) await step('Voice revocation follows the connection in the room: an unused newer pass, a backend restart (real LiveKit)', async () => {
  const rtc = createRequire(resolve(root, 'apps/voice-gateway/package.json'))('@livekit/rtc-node');
  const livekitUrl = `ws://127.0.0.1:${livekitPort}`;
  const current = (await carol.request('GET', '/parties/current')).body?.party;
  if (current) await carol.request('POST', `/parties/${current.id}/leave`);
  const party = await carol.request('POST', '/parties', { name: 'Revocation', privacy: 'friends' });
  assert.equal(party.status, 201, JSON.stringify(party.body));
  const inVoice = async () => (await carol.request('GET', `/parties/${party.body.id}`)).body.members.find((m) => m.user.id === carol.user.id)?.voice?.connected === true;
  /** Connects with a console's pass like the gateway does, and reports when LiveKit removes the connection. */
  const connectConsole = async (device) => {
    const pass = await device.pass(party.body.id);
    assert.equal(pass.status, 200, JSON.stringify(pass.body));
    // Nothing private in what every participant can read.
    const claims = JSON.parse(Buffer.from(pass.body.token.split('.')[1], 'base64url').toString('utf8'));
    assert.ok(!JSON.stringify(claims).includes(device.id), 'no device ID in the pass');
    const room = new rtc.Room();
    const connection = { room, removed: false };
    room.on(rtc.RoomEvent.Disconnected, () => (connection.removed = true));
    await room.connect(livekitUrl, pass.body.token, { autoSubscribe: false, dynacast: false });
    await waitFor(inVoice, 'the server sees the console in voice', 15_000);
    return connection;
  };

  // 1. Console A is in voice; console B and a browser session then ask for passes they never use.
  const a = await pairApiDevice(carol, 'PS5 A');
  const b = await pairApiDevice(carol, 'PS5 B');
  const first = await connectConsole(a);
  assert.equal((await b.pass(party.body.id)).status, 200);
  assert.equal((await carol.request('POST', `/parties/${party.body.id}/voice/token`)).status, 200);
  // Unpairing B (unused) leaves A connected ...
  const devices = (await carol.request('GET', '/devices')).body.devices;
  assert.equal((await carol.request('DELETE', `/devices/${b.id}`)).status, 204);
  await sleep(1500);
  assert.equal(first.removed, false, 'revoking an unused pass does not touch the connection in the room');
  // ... and A losing "parties:write" removes A.
  const scopes = devices.find((d) => d.id === a.id).scopes.filter((scope) => scope !== 'parties:write');
  assert.equal((await carol.request('PATCH', `/devices/${a.id}`, { scopes })).status, 200);
  await waitFor(() => first.removed, 'LiveKit removes console A', 10_000);
  await waitFor(async () => !(await inVoice()), 'the server no longer shows carol in voice', 10_000);
  await first.room.disconnect();

  // 2. Console C is in voice; the AKENO server restarts (LiveKit keeps the room) and C is unpaired
  //    before any webhook or reconciliation of the new process has seen the connection.
  const c = await pairApiDevice(carol, 'PS5 C');
  const second = await connectConsole(c);
  backend.kill('SIGTERM');
  await backend.exited;
  children.delete(backend);
  backend = await startBackend();
  assert.equal(second.removed, false, 'a restart alone does not end the voice connection');
  assert.equal((await carol.request('DELETE', `/devices/${c.id}`)).status, 204);
  await waitFor(() => second.removed, 'LiveKit removes console C after the restart', 10_000);
  await second.room.disconnect();
  await carol.request('POST', `/parties/${party.body.id}/close`);
});

// The hardware report of 196ca13 lost the foreground microphone test: an accidental background start rewrote the report.
await step('AKENO Native: the last microphone test survives a later background start (counts and levels only)', async () => {
  const micDir = join(work, 'native-mic');
  const run = await runNative({ AKENO_NATIVE_AKENO_DIR: akenoDir, AKENO_NATIVE_DIR: micDir, AKENO_NATIVE_DAEMON_PORT: String(daemon.port),
    AKENO_NATIVE_SYNC_DIR: syncDir, AKENO_NATIVE_SIM_MIC: 'tone:440' }, [
    'until ipc 15000', 'until friends 15000', 'wait 300', 'pad right', 'pad right', 'pad right', 'pad right', 'pad right', 'pad right',
    'expect focus settings', 'pad cross', 'expect screen settings', 'wait 300',
    'pad down', 'pad down', 'pad down', 'pad down', 'pad down', 'pad down', 'pad down', 'pad down', 'expect text Microphone test',
    'pad cross', 'expect dialog confirm', 'expect text Test your microphone?', 'pad left', 'pad cross', 'until text 10000 Cross: stop', 'wait 1500', 'pad cross', 'until text 10000 nothing is sent or stored', 'wait 500',
    'pad circle', 'pad circle', 'expect dialog exit', 'pad left', 'pad cross',
  ], {});
  assert.equal(run.code, 0, run.output.slice(-3000));
  const kept = readFileSync(join(micDir, 'mic-test-last.txt'), 'utf8');
  assert.match(kept, /^AKENO Native microphone test v1\n/);
  assert.match(kept, /\nopen: opened\n/);
  assert.match(kept, /\nrate: 16000\n/);
  assert.ok(Number(/\nframes: (\d+)\n/.exec(kept)?.[1]) > 30, `frames captured: ${kept}`);
  assert.match(kept, /\nclosed: 1\n/);
  assert.equal(readFileSync(join(micDir, 'mic-test-last-ok.txt'), 'utf8'), kept, 'a successful test is also kept as the last successful one');
  for (const name of ['mic-test-last.txt', 'mic-test-last-ok.txt']) assert.equal(statSync(join(micDir, name)).mode & 0o077, 0, `${name} is private`);
  assert.ok(Buffer.byteLength(kept) < 1024, 'counts and levels only, no audio');

  // An accidental background start: nothing is opened, and the report still carries the test.
  const background = await runNative({ AKENO_NATIVE_AKENO_DIR: akenoDir, AKENO_NATIVE_DIR: micDir, AKENO_NATIVE_SIM_CONTEXT: 'background' }, [], {});
  assert.equal(background.code, 0, background.output.slice(-3000));
  const report = readFileSync(join(micDir, 'native-report.txt'), 'utf8');
  assert.match(report, /Context:\s+.*background/i);
  assert.match(report, /\[Last microphone test\]\nResult:\s+the microphone captured sound\n/);
  assert.match(report, /Microphone:\s+opened \(simulated microphone \(PC test\), 16000 Hz, 256 frames per read\)/);
  assert.match(report, /Levels:\s+peak \d+%, loudest \d+%, average \d+%/);
  assert.match(report, /Read errors:\s+none/);
  assert.match(report, /Closed:\s+yes/);
  assert.match(report, /Local only:\s+yes/);
  assert.equal(readFileSync(join(micDir, 'mic-test-last.txt'), 'utf8'), kept, 'the background start did not touch the kept test');
});

await step('native-IPC daemon: stopping removes the key; the release daemon ignores it', async () => {
  const stop = await local(daemon.port, 'POST', '/api/exit', { 'X-Akeno-Token': daemon.token });
  assert.equal(stop.status, 200);
  await daemon.exited;
  assert.ok(!existsSync(join(akenoDir, 'native.key')), 'native.key removed on a clean stop');
  writeFileSync(join(akenoDir, 'native.key'), key, { mode: 0o600 }); // as if left behind by a crash
  const release = await startDaemon(releaseBinary, akenoDir, serverUrl);
  try {
    assert.deepEqual((await local(release.port, 'GET', '/api/instance')).body, { app: 'akeno-network-console', version: '0.5.0-alpha' });
    const ignored = await local(release.port, 'GET', '/api/state', { 'X-Akeno-Native-Key': key });
    assert.equal(ignored.body.error.code, 'SESSION_REQUIRED', 'the release build has no native interface');
    const run = await runNative({ AKENO_NATIVE_AKENO_DIR: akenoDir, AKENO_NATIVE_DIR: join(work, 'native-release'),
      AKENO_NATIVE_DAEMON_PORT: String(release.port), AKENO_NATIVE_SYNC_DIR: syncDir },
    ['until ipc 15000', 'pad cross', 'wait 300', 'expect text This AKENO daemon has no native interface', `shot ${shots}/e2e-release-daemon.png`,
      'pad circle', 'pad circle', 'pad left', 'pad cross'], {});
    assert.equal(run.code, 0, run.output.slice(-3000));
  } finally {
    await local(release.port, 'POST', '/api/exit', { 'X-Akeno-Token': release.token });
    await release.exited;
  }
});

// ---- Teardown --------------------------------------------------------------------------------

if (process.env.AKN_E2E_OUT) {
  mkdirSync(process.env.AKN_E2E_OUT, { recursive: true });
  for (const f of readdirSync(shots)) copyFileSync(join(shots, f), join(process.env.AKN_E2E_OUT, f));
  if (existsSync(join(nativeDir, 'native-report.txt'))) copyFileSync(join(nativeDir, 'native-report.txt'), join(process.env.AKN_E2E_OUT, 'native-report-e2e.txt'));
}
for (const child of children) child.kill('SIGTERM');
tlsServer.close();
const cleanup = new pg.Client({ connectionString: adminUrl });
await cleanup.connect();
await sleep(300);
await cleanup.query(`DROP DATABASE IF EXISTS ${databaseName} WITH (FORCE)`);
await cleanup.end();
if (failures === 0) rmSync(work, { recursive: true, force: true });
else log(`Artifacts kept in ${work}`);
log(failures === 0 ? 'All native end-to-end tests passed.' : `${failures} native end-to-end test(s) FAILED.`);
process.exit(failures === 0 ? 0 : 1);
