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
const databaseUrl = new URL(adminUrl);
databaseUrl.pathname = `/${databaseName}`;
const serverEnv = {
  MEDIA_DIR: join(work, 'media'), ...process.env, NODE_ENV: 'production', HOST: '127.0.0.1', PORT: String(backendPort),
  DATABASE_URL: databaseUrl.toString(), PUBLIC_ORIGIN: origin, REGISTRATION_MODE: 'invite', MIGRATE_ON_START: 'true',
  LOG_LEVEL: 'warn', LOG_PRETTY: 'false',
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
async function startTlsProxy(port) {
  openssl(['req', '-x509', '-newkey', 'rsa:2048', '-nodes', '-keyout', 'ca.key', '-out', 'ca.pem', '-days', '2', '-subj',
    '/CN=AKENO Native E2E CA', '-addext', 'basicConstraints=critical,CA:TRUE', '-addext', 'keyUsage=critical,keyCertSign,cRLSign']);
  openssl(['req', '-newkey', 'rsa:2048', '-nodes', '-keyout', 'server.key', '-out', 'server.csr', '-subj', '/CN=127.0.0.1']);
  writeFileSync(join(work, 'ext.cnf'), 'subjectAltName=IP:127.0.0.1\nbasicConstraints=CA:FALSE\nkeyUsage=digitalSignature,keyEncipherment\nextendedKeyUsage=serverAuth\n');
  openssl(['x509', '-req', '-in', 'server.csr', '-CA', 'ca.pem', '-CAkey', 'ca.key', '-CAcreateserial', '-out', 'server.pem', '-days', '2', '-extfile', 'ext.cnf']);
  const server = tls.createServer({ key: readFileSync(join(work, 'server.key')), cert: readFileSync(join(work, 'server.pem')) }, (socket) => {
    const upstream = net.connect(backendPort, '127.0.0.1');
    let first = true;
    socket.on('data', (chunk) => {
      if (!first) return;
      first = false;
      const head = chunk.toString('latin1');
      tlsRequests.push({ line: head.split('\r\n')[0], upgrade: /\r\nupgrade: websocket/i.test(head), at: Date.now() });
    });
    socket.pipe(upstream).pipe(socket);
    const destroy = () => {
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
const backend = spawn('node', ['apps/server/dist/main.js'], { cwd: root, env: serverEnv, stdio: ['ignore', 'ignore', 'pipe'] });
children.add(backend);
await waitFor(async () => {
  try {
    return (await fetch(`http://127.0.0.1:${backendPort}/health/ready`)).ok;
  } catch {
    return false;
  }
}, 'the server');
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
  assert.deepEqual(instance.body, { app: 'akeno-network-console', version: '0.5.0-alpha', nativeIpc: 2 });
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
  assert.equal(hello.body.ipc, 2);
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
  assert.match(daemonLog, /native\.ready Native interface 2 ready/);
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
  assert.match(report, /State:\s+Connected to the AKENO daemon: protocol 2, daemon 0\.5\.0-alpha \([0-9a-f]+-linux-native-ipc\)/);
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
