#!/usr/bin/env node
// End-to-end tests for the AKENO device client (akeno-cli) against a real, disposable AKENO
// NETWORK server. Nothing is mocked:
//   * a fresh PostgreSQL database (name must start with "akeno_client_it") and the production
//     server build (apps/server/dist),
//   * a local TLS terminator with a throwaway CA, so certificate and host name verification
//     are exercised exactly as against a real HTTPS deployment,
//   * disposable accounts created through the public API.
// Hostile-server cases (garbage, oversized and stalled responses) use small local fake servers.
//
// Usage: node clients/ps5/tests/integration/run.mjs <path-to-akeno-cli> [--app <path-to-akeno-app>]
// With --app, the console app (host build) is also tested against the same server.
// Requires: `pnpm build`, PostgreSQL (TEST_DATABASE_ADMIN_URL), openssl on PATH.
import assert from 'node:assert/strict';
import { spawn, spawnSync } from 'node:child_process';
import { mkdtempSync, readFileSync, rmSync, statSync, writeFileSync, existsSync, copyFileSync } from 'node:fs';
import { createRequire } from 'node:module';
import net from 'node:net';
import { tmpdir } from 'node:os';
import { dirname, join, resolve } from 'node:path';
import tls from 'node:tls';
import { fileURLToPath } from 'node:url';

const root = resolve(dirname(fileURLToPath(import.meta.url)), '../../../..');
const pg = createRequire(resolve(root, 'apps/server/package.json'))('pg');
const cli = resolve(process.argv[2] ?? 'clients/ps5/build/akeno-cli');
const appIndex = process.argv.indexOf('--app');
const appBinary = appIndex > 0 ? resolve(process.argv[appIndex + 1]) : null;
const adminUrl = process.env.TEST_DATABASE_ADMIN_URL ?? 'postgres://akeno:akeno_dev_password@127.0.0.1:5432/postgres';
const databaseName = 'akeno_client_it';
const ADMIN_PASSWORD = 'client-it-admin-password-1';
const PASSWORD = 'correct-horse-battery-staple';

const work = mkdtempSync(join(tmpdir(), 'akeno-client-it-'));
const children = new Set();
const servers = [];
let failures = 0;
const allStderr = [];

function log(message) {
  process.stdout.write(`${message}\n`);
}

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

const sleep = (ms) => new Promise((done) => setTimeout(done, ms));

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

// ---- Server lifecycle -------------------------------------------------------------------------

const backendPort = await freePort();
const origin = `http://localhost:${backendPort}`;
const databaseUrl = new URL(adminUrl);
databaseUrl.pathname = `/${databaseName}`;
const serverEnv = {
  ...process.env,
  NODE_ENV: 'production',
  HOST: '127.0.0.1',
  PORT: String(backendPort),
  DATABASE_URL: databaseUrl.toString(),
  PUBLIC_ORIGIN: origin,
  REGISTRATION_MODE: 'invite',
  MIGRATE_ON_START: 'true',
  LOG_LEVEL: 'warn',
  LOG_PRETTY: 'false',
};

let backend = null;

async function waitReady() {
  for (let i = 0; i < 100; i += 1) {
    try {
      const response = await fetch(`http://127.0.0.1:${backendPort}/health/ready`);
      if (response.ok) return;
    } catch {
      // Not listening yet.
    }
    await sleep(100);
  }
  throw new Error('Server did not become ready');
}

async function startBackend() {
  backend = spawn('node', ['apps/server/dist/main.js'], { cwd: root, env: serverEnv, stdio: ['ignore', 'ignore', 'pipe'] });
  backend.stderr.on('data', (chunk) => process.env.IT_VERBOSE && process.stderr.write(chunk));
  children.add(backend);
  await waitReady();
}

async function stopBackend() {
  if (!backend) return;
  const exited = new Promise((done) => backend.once('exit', done));
  backend.kill('SIGTERM');
  await exited;
  children.delete(backend);
  backend = null;
}

// ---- HTTP helpers for the "website" side ------------------------------------------------------

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
    const response = await fetch(`http://127.0.0.1:${backendPort}/api/v1${path}`, {
      method,
      headers,
      body: body === undefined ? undefined : JSON.stringify(body),
    });
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
}

async function registerUser(admin, onlineId) {
  const invitation = await admin.request('POST', '/admin/invitations', { maxUses: 1, expiresInHours: 1, note: 'client it' });
  assert.equal(invitation.status, 201);
  const session = new WebSession();
  const response = await session.request('POST', '/auth/register', {
    onlineId,
    password: PASSWORD,
    invitationCode: invitation.body.code,
  });
  assert.equal(response.status, 201, JSON.stringify(response.body));
  session.csrf = response.body.csrfToken;
  session.user = response.body.user;
  return session;
}

// ---- TLS terminator with a throwaway CA -------------------------------------------------------

function openssl(args, input) {
  const result = spawnSync('openssl', args, { cwd: work, input, encoding: 'utf8' });
  if (result.status !== 0) throw new Error(`openssl ${args.join(' ')} failed: ${result.stderr}`);
}

function createCertificates() {
  openssl(['req', '-x509', '-newkey', 'rsa:2048', '-nodes', '-keyout', 'ca.key', '-out', 'ca.pem', '-days', '2', '-subj',
    '/CN=AKENO Integration Test CA', '-addext', 'basicConstraints=critical,CA:TRUE', '-addext', 'keyUsage=critical,keyCertSign,cRLSign']);
  openssl(['req', '-newkey', 'rsa:2048', '-nodes', '-keyout', 'server.key', '-out', 'server.csr', '-subj', '/CN=127.0.0.1']);
  writeFileSync(join(work, 'ext.cnf'),
    'subjectAltName=IP:127.0.0.1\nbasicConstraints=CA:FALSE\nkeyUsage=digitalSignature,keyEncipherment\nextendedKeyUsage=serverAuth\n');
  openssl(['x509', '-req', '-in', 'server.csr', '-CA', 'ca.pem', '-CAkey', 'ca.key', '-CAcreateserial', '-out', 'server.pem',
    '-days', '2', '-extfile', 'ext.cnf']);
  // A second certificate that names the server by DNS name only.
  openssl(['req', '-newkey', 'rsa:2048', '-nodes', '-keyout', 'dns.key', '-out', 'dns.csr', '-subj', '/CN=localhost']);
  writeFileSync(join(work, 'dns.cnf'),
    'subjectAltName=DNS:localhost\nbasicConstraints=CA:FALSE\nkeyUsage=digitalSignature,keyEncipherment\nextendedKeyUsage=serverAuth\n');
  openssl(['x509', '-req', '-in', 'dns.csr', '-CA', 'ca.pem', '-CAkey', 'ca.key', '-CAcreateserial', '-out', 'dns.pem',
    '-days', '2', '-extfile', 'dns.cnf']);
}

async function startTlsProxy(port, name = 'server') {
  const server = tls.createServer(
    { key: readFileSync(join(work, `${name}.key`)), cert: readFileSync(join(work, `${name}.pem`)) },
    (socket) => {
      const upstream = net.connect(backendPort, '127.0.0.1');
      socket.pipe(upstream).pipe(socket);
      const destroy = () => {
        socket.destroy();
        upstream.destroy();
      };
      socket.on('error', destroy);
      upstream.on('error', destroy);
      socket.on('close', destroy);
      upstream.on('close', destroy);
    },
  );
  await new Promise((done) => server.listen(port, '127.0.0.1', done));
  servers.push(server);
  return server;
}

// ---- Running the client ----------------------------------------------------------------------

function runCli(args, { env = {}, timeoutMs = 60_000 } = {}) {
  return new Promise((done) => {
    const child = spawn(cli, args, { env: { ...process.env, ...env }, stdio: ['ignore', 'pipe', 'pipe'] });
    children.add(child);
    let stdout = '';
    let stderr = '';
    child.stdout.on('data', (chunk) => (stdout += chunk));
    child.stderr.on('data', (chunk) => (stderr += chunk));
    const timer = setTimeout(() => child.kill('SIGKILL'), timeoutMs);
    child.on('exit', (code, signal) => {
      clearTimeout(timer);
      children.delete(child);
      allStderr.push(stderr);
      done({ code, signal, stdout, stderr });
    });
  });
}

/** Starts a long-running client command and exposes its output as it arrives. */
function startCli(args, env = {}) {
  const child = spawn(cli, args, { env: { ...process.env, ...env }, stdio: ['ignore', 'pipe', 'pipe'] });
  children.add(child);
  const state = { child, stdout: '', stderr: '', exit: null };
  child.stdout.on('data', (chunk) => (state.stdout += chunk));
  child.stderr.on('data', (chunk) => (state.stderr += chunk));
  state.exited = new Promise((done) =>
    child.on('exit', (code, signal) => {
      children.delete(child);
      allStderr.push(state.stderr);
      state.exit = { code, signal };
      done(state.exit);
    }),
  );
  return state;
}

async function waitFor(predicate, what, timeoutMs = 20_000) {
  const deadline = Date.now() + timeoutMs;
  while (Date.now() < deadline) {
    if (predicate()) return;
    await sleep(50);
  }
  throw new Error(`Timed out waiting for ${what}`);
}

const count = (text, needle) => text.split(needle).length - 1;

// ---- Setup -----------------------------------------------------------------------------------

for (const required of ['apps/server/dist/main.js', 'apps/server/dist/cli.js']) {
  if (!existsSync(resolve(root, required))) throw new Error(`Missing ${required}. Run "pnpm build" first.`);
}
if (!existsSync(cli)) throw new Error(`Missing client binary ${cli}`);

const admin = new pg.Client({ connectionString: adminUrl });
await admin.connect();
await admin.query(`DROP DATABASE IF EXISTS ${databaseName} WITH (FORCE)`);
await admin.query(`CREATE DATABASE ${databaseName}`);
await admin.end();

const created = spawnSync('node', ['apps/server/dist/cli.js', 'migrate'], { cwd: root, env: serverEnv, encoding: 'utf8' });
assert.equal(created.status, 0, created.stderr);
const adminCreated = spawnSync('node', ['apps/server/dist/cli.js', 'create-admin', '--online-id', 'it_admin', '--password-stdin'], {
  cwd: root,
  env: serverEnv,
  input: ADMIN_PASSWORD,
  encoding: 'utf8',
});
assert.equal(adminCreated.status, 0, adminCreated.stderr);

await startBackend();
createCertificates();
const tlsPort = await freePort();
await startTlsProxy(tlsPort);
const server = `https://127.0.0.1:${tlsPort}`;
const caFile = join(work, 'ca.pem');
const credentials = join(work, 'device.json');
const base = ['--server', server, '--ca-file', caFile, '--credentials', credentials];

const adminSession = await new WebSession().signIn('it_admin', ADMIN_PASSWORD);
const alice = await registerUser(adminSession, 'it_alice');
const bob = await registerUser(adminSession, 'it_bob');
const request = await alice.request('POST', '/friends/requests', { userId: bob.user.id });
assert.equal(request.status, 201);
assert.equal((await bob.request('POST', `/friends/requests/${request.body.request.id}/accept`)).status, 200);

log(`AKENO device client integration tests (server ${server}, client ${cli})`);

/** Runs `pair`, approves the code as `approver` with optional scopes, and returns the result. */
async function pair(credentialPath, approver, { scopes, deny = false, name = 'IT console' } = {}) {
  const run = startCli(['--server', server, '--ca-file', caFile, '--credentials', credentialPath, '--name', name, 'pair']);
  await waitFor(() => /enter the code: ([A-Z0-9]{4}-[A-Z0-9]{4})/.test(run.stdout), 'the pairing code');
  const userCode = /enter the code: ([A-Z0-9]{4}-[A-Z0-9]{4})/.exec(run.stdout)[1];
  assert.match(run.stdout, new RegExp(`/pair\\?code=${userCode}`));
  const lookup = await approver.request('POST', '/devices/pairings/lookup', { userCode });
  assert.equal(lookup.status, 200);
  assert.equal(lookup.body.pairing.deviceName, name);
  assert.equal(lookup.body.pairing.platform, 'PlayStation 5 (homebrew)');
  const decision = deny
    ? await approver.request('POST', '/devices/pairings/deny', { userCode })
    : await approver.request('POST', '/devices/pairings/approve', { userCode, ...(scopes ? { scopes } : {}) });
  assert.equal(decision.status, 204);
  const exit = await run.exited;
  return { ...exit, stdout: run.stdout, stderr: run.stderr };
}

// ---- Tests -----------------------------------------------------------------------------------

await step('refuses servers whose certificate is not trusted', async () => {
  const result = await runCli(['--server', server, '--credentials', credentials, 'pair'], { timeoutMs: 20_000 });
  assert.equal(result.code, 6, result.stderr);
  assert.match(result.stderr, /certificate|TLS/i);
});

await step('refuses certificates issued for a different host name', async () => {
  const result = await runCli(['--server', `https://localhost:${tlsPort}`, '--ca-file', caFile, '--credentials', credentials, 'pair'], {
    timeoutMs: 20_000,
  });
  assert.equal(result.code, 6, result.stderr);
});

await step('accepts a certificate issued for the DNS name of the server', async () => {
  const dnsPort = await freePort();
  await startTlsProxy(dnsPort, 'dns');
  const dnsArgs = ['--server', `https://localhost:${dnsPort}`, '--ca-file', caFile, '--credentials', join(work, 'dns.json')];
  const run = startCli([...dnsArgs, '--name', 'DNS check', 'pair']);
  await waitFor(() => /enter the code: [A-Z0-9]{4}-[A-Z0-9]{4}/.test(run.stdout), 'a pairing code over a DNS-named certificate');
  run.child.kill('SIGTERM');
  assert.equal((await run.exited).code, 0, run.stderr);
  const wrongName = await runCli(['--server', `https://127.0.0.1:${dnsPort}`, '--ca-file', caFile, '--credentials', join(work, 'dns.json'), 'pair'], {
    timeoutMs: 20_000,
  });
  assert.equal(wrongName.code, 6, wrongName.stderr);
});

await step('commands fail cleanly before pairing', async () => {
  const result = await runCli([...base, 'friends']);
  assert.equal(result.code, 3, result.stderr);
  assert.match(result.stderr, /not paired/);
});

await step('pairing can be denied on the website', async () => {
  const result = await pair(join(work, 'denied.json'), alice, { deny: true });
  assert.equal(result.code, 8, result.stderr);
  assert.equal(existsSync(join(work, 'denied.json')), false);
});

await step('pairs through the website and stores a private credential', async () => {
  const result = await pair(credentials, alice);
  assert.equal(result.code, 0, result.stderr);
  assert.match(result.stdout, /Paired as it_alice\./);
  const mode = statSync(credentials).mode & 0o777;
  assert.equal(mode, 0o600);
  const stored = JSON.parse(readFileSync(credentials, 'utf8'));
  assert.match(stored.accessToken, /^akd_[A-Za-z0-9_-]{43}$/);
  assert.equal(stored.server, server);
  assert.equal(stored.onlineId, 'it_alice');
  const devices = await alice.request('GET', '/devices');
  assert.equal(devices.body.devices.length, 1);
  assert.equal(devices.body.devices[0].name, 'IT console');
});

await step('reads the profile, friends and device status', async () => {
  const me = await runCli([...base, '--json', 'me']);
  assert.equal(me.code, 0, me.stderr);
  assert.equal(JSON.parse(me.stdout).onlineId, 'it_alice');
  const friends = await runCli([...base, 'friends']);
  assert.equal(friends.code, 0, friends.stderr);
  assert.match(friends.stdout, /it_bob/);
  const status = await runCli([...base, '--json', 'status']);
  const body = JSON.parse(status.stdout);
  assert.equal(body.apiVersion, 1);
  assert.ok(body.device.scopes.includes('realtime'));
});

await step('sends a message that the friend receives, and reads history', async () => {
  const sent = await runCli([...base, 'send', 'it_bob', 'Hello from the console client "quoted" \\ ünïcode']);
  assert.equal(sent.code, 0, sent.stderr);
  const conversations = await bob.request('GET', '/conversations');
  const conversation = conversations.body.conversations.find((entry) => entry.otherUser.onlineId === 'it_alice');
  assert.ok(conversation, 'conversation exists for bob');
  const history = await bob.request('GET', `/conversations/${conversation.id}/messages`);
  assert.equal(history.body.messages.at(-1).body, 'Hello from the console client "quoted" \\ ünïcode');
  const listed = await runCli([...base, '--json', 'conversations']);
  const own = JSON.parse(listed.stdout).conversations[0];
  const messages = await runCli([...base, 'messages', own.id]);
  assert.equal(messages.code, 0, messages.stderr);
  assert.match(messages.stdout, /it_alice: Hello from the console client/);
});

await step('updates presence as seen by friends', async () => {
  const result = await runCli([...base, 'presence', 'away']);
  assert.equal(result.code, 0, result.stderr);
  assert.equal((await alice.request('GET', '/users/me')).body.presencePreference, 'away');
  assert.equal((await runCli([...base, 'presence', 'online'])).code, 0);
  assert.equal((await alice.request('GET', '/users/me')).body.presencePreference, 'online');
});

await step('lists and accepts party invitations, lists parties and game sessions', async () => {
  const party = await bob.request('POST', '/parties', { name: 'Raid night', privacy: 'invite_only', maxMembers: 4 });
  assert.equal(party.status, 201, JSON.stringify(party.body));
  assert.equal((await bob.request('POST', `/parties/${party.body.id}/invitations`, { userId: alice.user.id })).status, 201);
  const invitations = await runCli([...base, '--json', 'invitations']);
  const invitation = JSON.parse(invitations.stdout).invitations.find((entry) => entry.kind === 'party');
  assert.ok(invitation, invitations.stdout);
  const accepted = await runCli([...base, 'accept', invitation.id]);
  assert.equal(accepted.code, 0, accepted.stderr);
  const current = await runCli([...base, 'party']);
  assert.match(current.stdout, /Party Raid night \(2 members, text chat only\)/);

  const session = await bob.request('POST', '/game-sessions', {
    gameName: 'Integration Racer',
    visibility: 'friends',
    joinMode: 'open',
    maxParticipants: 4,
  });
  assert.equal(session.status, 201, JSON.stringify(session.body));
  const sessions = await runCli([...base, 'sessions']);
  assert.equal(sessions.code, 0, sessions.stderr);
  assert.match(sessions.stdout, /Integration Racer/);
});

let listener = null;
await step('receives realtime events and survives a server restart', async () => {
  listener = startCli([...base, 'listen'], { AKENO_BACKOFF_BASE_MS: '300' });
  await waitFor(() => count(listener.stderr, 'realtime.connected') === 1, 'the first realtime connection');
  const conversations = await bob.request('GET', '/conversations');
  const conversation = conversations.body.conversations.find((entry) => entry.otherUser.onlineId === 'it_alice');
  const first = await bob.request('POST', `/conversations/${conversation.id}/messages`, {
    body: 'Realtime before restart',
    clientMessageId: '11111111-1111-4111-8111-111111111111',
  });
  assert.equal(first.status, 201);
  await waitFor(() => listener.stdout.includes('Realtime before restart'), 'the first realtime message');
  const presence = await bob.request('GET', '/friends');
  assert.equal(presence.body.friends[0].presence.status, 'online', 'the console connection makes alice appear online');

  await stopBackend();
  await waitFor(() => /close code 1012|disconnected/.test(listener.stderr), 'the disconnect to be noticed');
  await sleep(1500); // Reconnect attempts fail while the server is down.
  await startBackend();
  await waitFor(() => count(listener.stderr, 'realtime.connected') >= 2, 'the reconnection', 30_000);
  const second = await bob.request('POST', `/conversations/${conversation.id}/messages`, {
    body: 'Realtime after restart',
    clientMessageId: '22222222-2222-4222-8222-222222222222',
  });
  assert.equal(second.status, 201);
  await waitFor(() => listener.stdout.includes('Realtime after restart'), 'the message after the restart');
  for (const line of listener.stdout.trim().split('\n')) JSON.parse(line); // Every event line is valid JSON.
  assert.equal(listener.exit, null, 'the listener is still running');
});

await step('stops immediately and forgets the credential when unpaired on the website', async () => {
  copyFileSync(credentials, join(work, 'stale.json'));
  const devices = await alice.request('GET', '/devices');
  assert.equal((await alice.request('DELETE', `/devices/${devices.body.devices[0].id}`)).status, 204);
  const exit = await Promise.race([listener.exited, sleep(10_000).then(() => 'timeout')]);
  assert.notEqual(exit, 'timeout', 'listener should exit');
  assert.equal(exit.code, 4, listener.stderr);
  assert.equal(existsSync(credentials), false, 'credential deleted');
  const stale = await runCli(['--server', server, '--ca-file', caFile, '--credentials', join(work, 'stale.json'), 'me']);
  assert.equal(stale.code, 4, stale.stderr);
  assert.equal(existsSync(join(work, 'stale.json')), false, 'stale credential deleted after 401');
});

await step('respects the permissions granted at pairing time', async () => {
  const limited = join(work, 'limited.json');
  const result = await pair(limited, alice, { scopes: ['profile:read'] });
  assert.equal(result.code, 0, result.stderr);
  const args = ['--server', server, '--ca-file', caFile, '--credentials', limited];
  assert.equal((await runCli([...args, 'me'])).code, 0);
  const friends = await runCli([...args, 'friends']);
  assert.equal(friends.code, 5, friends.stderr);
  assert.match(friends.stderr, /friends:read/);
  const listen = await runCli([...args, 'listen'], { timeoutMs: 15_000 });
  assert.equal(listen.code, 5, listen.stderr);
  const logout = await runCli([...args, 'logout']);
  assert.equal(logout.code, 0, logout.stderr);
  assert.equal(existsSync(limited), false);
  assert.equal((await alice.request('GET', '/devices')).body.devices.length, 0);
});

await step('refuses credential files readable by other users', async () => {
  const loose = join(work, 'loose.json');
  const result = await pair(loose, alice);
  assert.equal(result.code, 0);
  spawnSync('chmod', ['644', loose]);
  const me = await runCli(['--server', server, '--ca-file', caFile, '--credentials', loose, 'me']);
  assert.equal(me.code, 3, me.stderr);
  assert.match(me.stderr, /credentials\.permissions/);
});

await step('never writes tokens or device codes to its logs', async () => {
  const verbose = await runCli([...base, '-v', 'caps']);
  assert.equal(verbose.code, 0);
  const everything = allStderr.join('\n');
  assert.doesNotMatch(everything, /akd_[A-Za-z0-9_-]{10,}/);
  assert.doesNotMatch(everything, /akp_[A-Za-z0-9_-]{10,}/);
});

// Hostile and broken servers: every case must end with a clean error, never a crash or hang.
async function fakeServer(handler) {
  const port = await freePort();
  const fake = net.createServer(handler);
  await new Promise((done) => fake.listen(port, '127.0.0.1', done));
  servers.push(fake);
  return `http://127.0.0.1:${port}`;
}

const fakeCredentials = join(work, 'fake.json');
const hostile = [
  ['malformed HTTP', (socket) => socket.end('NOT HTTP AT ALL\r\n\r\n'), 7],
  ['invalid JSON body', (socket) => socket.end('HTTP/1.1 201 Created\r\nContent-Length: 7\r\n\r\n{"a":1,'), 7],
  ['oversized response', (socket) => socket.end('HTTP/1.1 200 OK\r\nContent-Length: 104857600\r\n\r\n{'), 7],
  ['truncated body', (socket) => socket.end('HTTP/1.1 201 Created\r\nContent-Length: 50\r\n\r\n{"deviceCode":'), 7],
  ['unexpected pairing payload', (socket) => {
    const body = JSON.stringify({ deviceCode: 'nope', userCode: 'X', expiresIn: -1, interval: 0 });
    socket.end(`HTTP/1.1 201 Created\r\nContent-Type: application/json\r\nContent-Length: ${body.length}\r\n\r\n${body}`);
  }, 7],
  ['server error', (socket) => socket.end('HTTP/1.1 503 Service Unavailable\r\nContent-Length: 0\r\n\r\n'), 7],
  ['stalled response', () => {}, 6],
];
for (const [name, handler, expected] of hostile) {
  await step(`handles a ${name} from the server`, async () => {
    const url = await fakeServer((socket) => {
      socket.once('data', () => handler(socket));
      socket.on('error', () => {});
    });
    const result = await runCli(['--server', url, '--credentials', fakeCredentials, 'pair'], {
      env: { AKENO_IO_TIMEOUT_MS: '1500', AKENO_CONNECT_TIMEOUT_MS: '1500' },
      timeoutMs: 15_000,
    });
    assert.equal(result.signal, null, `terminated by ${result.signal}: ${result.stderr}`);
    assert.equal(result.code, expected, result.stderr);
  });
}

await step('reports a refused connection as a network error', async () => {
  const port = await freePort();
  const result = await runCli(['--server', `http://127.0.0.1:${port}`, '--credentials', fakeCredentials, 'pair']);
  assert.equal(result.code, 6, result.stderr);
});

await step('gives up after a bounded number of reconnect attempts', async () => {
  const bounded = join(work, 'bounded.json');
  assert.equal((await pair(bounded, alice)).code, 0);
  const deadPort = await freePort();
  const stored = JSON.parse(readFileSync(bounded, 'utf8'));
  stored.server = `http://127.0.0.1:${deadPort}`;
  writeFileSync(bounded, JSON.stringify(stored), { mode: 0o600 });
  const result = await runCli(['--server', `http://127.0.0.1:${deadPort}`, '--credentials', bounded, 'listen'], {
    env: { AKENO_MAX_RECONNECTS: '3', AKENO_BACKOFF_BASE_MS: '50' },
    timeoutMs: 20_000,
  });
  assert.equal(result.code, 9, result.stderr);
  assert.equal(count(result.stderr, 'realtime.connecting'), 3);
});


// ---- Console app (host build of the PS5 app) ------------------------------------------------

/** Starts the console app; resolves once it has printed its page URL. */
async function startApp(dataDir, args = [], env = {}) {
  const child = spawn(appBinary, ['--data-dir', dataDir, ...args], { env: { ...process.env, ...env }, stdio: ['ignore', 'pipe', 'pipe'] });
  children.add(child);
  const state = { child, stdout: '', stderr: '', events: [], exit: null, port: 0, token: '' };
  child.stdout.on('data', (chunk) => {
    state.stdout += chunk;
    const lines = state.stdout.split('\n');
    state.events = lines.filter((line) => line.startsWith('{')).map((line) => JSON.parse(line));
    const open = state.events.find((e) => e.event === 'open-browser');
    if (open && !state.port) {
      const match = /^http:\/\/127\.0\.0\.1:(\d+)\/#t=([0-9a-f]{64})$/.exec(open.url);
      assert.ok(match, open.url);
      state.port = Number(match[1]);
      state.token = match[2];
    }
  });
  child.stderr.on('data', (chunk) => (state.stderr += chunk));
  state.exited = new Promise((done) =>
    child.on('exit', (code, signal) => {
      children.delete(child);
      allStderr.push(state.stderr);
      state.exit = { code, signal };
      done(state.exit);
    }),
  );
  await waitFor(() => state.port || state.exit, 'the console app to start');
  return state;
}

function notifications(app) {
  return app.events.filter((e) => e.event === 'notification').map((e) => e.text);
}

async function appApi(app, method, path, body, headers = {}) {
  const response = await fetch(`http://127.0.0.1:${app.port}${path}`, {
    method,
    headers: { 'X-Akeno-Token': app.token, ...(body === undefined ? {} : { 'Content-Type': 'application/json' }), ...headers },
    body: body === undefined ? undefined : JSON.stringify(body),
  });
  const text = await response.text();
  let json = null;
  try {
    json = JSON.parse(text);
  } catch {
    json = null;
  }
  return { status: response.status, headers: response.headers, text, body: json };
}

/** Sends a raw request (to control the Host header, which fetch() always sets itself). */
function rawRequest(port, request) {
  return new Promise((done, fail) => {
    const socket = net.connect(port, '127.0.0.1', () => socket.end(request));
    let data = '';
    socket.on('data', (chunk) => (data += chunk));
    socket.on('end', () => done(data));
    socket.on('error', fail);
  });
}

async function appState(app) {
  return (await appApi(app, 'GET', '/api/state')).body;
}

if (appBinary) {
  if (!existsSync(appBinary)) throw new Error(`Missing app binary ${appBinary}`);
  log(`AKENO console app (host build) tests (${appBinary})`);
  const appData = join(work, 'app-data');
  let app = null;

  await step('console app: serves its page and refuses requests without the session secret', async () => {
    app = await startApp(appData, ['--port', '0', '--server', server, '--ca-file', caFile], { AKENO_BACKOFF_BASE_MS: '300' });
    assert.equal(app.exit, null, app.stderr);
    const page = await appApi(app, 'GET', '/');
    assert.equal(page.status, 200);
    assert.match(page.text, /<title>AKENO NETWORK<\/title>/);
    assert.match(page.headers.get('content-security-policy'), /default-src 'none'; script-src 'self'/);
    assert.equal(page.headers.get('x-frame-options'), 'DENY');
    const noSecret = await appApi(app, 'GET', '/api/state', undefined, { 'X-Akeno-Token': 'wrong' });
    assert.equal(noSecret.status, 403);
    assert.equal(noSecret.body.error.code, 'SESSION_REQUIRED');
    const rebinding = await rawRequest(app.port, `GET /api/state HTTP/1.1\r\nHost: attacker.example:${app.port}\r\nX-Akeno-Token: ${app.token}\r\n\r\n`);
    assert.match(rebinding, /^HTTP\/1\.1 403 /);
    assert.match(rebinding, /BAD_HOST/);
    const crossSite = await appApi(app, 'POST', '/api/exit', undefined, { Origin: 'https://attacker.example' });
    assert.equal(crossSite.status, 403);
    assert.equal(crossSite.body.error.code, 'BAD_ORIGIN');
    const malformed = await rawRequest(app.port, 'NOT HTTP\r\n\r\n');
    assert.match(malformed, /^HTTP\/1\.1 400 /);
    const state = await appState(app);
    assert.equal(state.configured, true);
    assert.equal(state.paired, false);
    assert.equal(state.server, server);
    assert.ok(notifications(app).includes('AKENO NETWORK Alpha 0.1 started.'));
  });

  await step('console app: refuses to proxy before pairing and outside the allow-list', async () => {
    const before = await appApi(app, 'GET', '/api/v1/friends');
    assert.equal(before.status, 401);
    for (const [method, path] of [['GET', '/api/v1/devices'], ['POST', '/api/v1/devices/pairings/approve'], ['GET', '/api/v1/auth/session'],
      ['GET', '/api/v1/admin/stats'], ['DELETE', '/api/v1/devices/current']]) {
      const refused = await appApi(app, method, path);
      assert.equal(refused.status, 403, `${method} ${path}`);
      assert.equal(refused.body.error.code, 'NOT_ALLOWED');
    }
  });

  await step('console app: pairs through its page and the website', async () => {
    const started = await appApi(app, 'POST', '/api/pair/start');
    assert.equal(started.status, 200, started.text);
    const state = await appState(app);
    assert.equal(state.pairing.state, 'waiting');
    assert.match(state.pairing.userCode, /^[A-Z0-9]{4}-[A-Z0-9]{4}$/);
    assert.ok(state.pairing.expiresIn > 500);
    assert.ok(notifications(app).some((text) => text.includes(state.pairing.userCode)), 'the code is also shown as a notification');
    const lookup = await alice.request('POST', '/devices/pairings/lookup', { userCode: state.pairing.userCode });
    assert.equal(lookup.body.pairing.platform, 'PlayStation 5 (homebrew)');
    assert.equal((await alice.request('POST', '/devices/pairings/approve', { userCode: state.pairing.userCode })).status, 204);
    await waitFor(() => notifications(app).some((text) => text.includes('paired as it_alice')), 'the paired notification', 20_000);
    let paired = null;
    for (let i = 0; i < 50 && !(paired && paired.paired); i += 1) {
      paired = await appState(app);
      if (!paired.paired) await sleep(100);
    }
    assert.equal(paired.paired, true);
    assert.equal(paired.onlineId, 'it_alice');
    assert.equal(statSync(join(appData, 'device.json')).mode & 0o777, 0o600);
    assert.equal(statSync(join(appData, 'settings.json')).mode & 0o777, 0o600);
  });

  await step('console app: lists friends and sends messages through the proxy', async () => {
    const friends = await appApi(app, 'GET', '/api/v1/friends');
    assert.equal(friends.status, 200);
    assert.equal(friends.body.ok, true);
    assert.ok(friends.body.data.friends.some((f) => f.onlineId === 'it_bob'));
    const conversation = await appApi(app, 'POST', '/api/v1/conversations', { userId: bob.user.id });
    assert.equal(conversation.body.ok, true, conversation.text);
    const sent = await appApi(app, 'POST', `/api/v1/conversations/${conversation.body.data.id}/messages`, {
      body: 'Sent from the console page ✓',
      clientMessageId: '33333333-3333-4333-8333-333333333333',
    });
    assert.equal(sent.body.ok, true, sent.text);
    assert.equal(sent.body.status, 201);
    const history = await bob.request('GET', `/conversations/${conversation.body.data.id}/messages`);
    assert.equal(history.body.messages.at(-1).body, 'Sent from the console page ✓');
    const invalid = await appApi(app, 'POST', `/api/v1/conversations/${conversation.body.data.id}/messages`, { body: '' });
    assert.equal(invalid.body.ok, false);
    assert.equal(invalid.body.status, 400);
    const notJson = await rawRequest(app.port, `POST /api/v1/conversations HTTP/1.1\r\nHost: 127.0.0.1:${app.port}\r\nX-Akeno-Token: ${app.token}\r\n` +
      'Content-Type: application/json\r\nContent-Length: 5\r\n\r\n{bad}');
    assert.match(notJson, /INVALID_BODY/);
  });

  await step('console app: turns realtime events into page events and system notifications', async () => {
    await waitFor(() => false || app.stderr.includes('realtime.state connected'), 'the realtime connection', 20_000);
    const before = (await appState(app)).eventsNext;
    const conversations = await bob.request('GET', '/conversations');
    const conversation = conversations.body.conversations.find((entry) => entry.otherUser.onlineId === 'it_alice');
    await bob.request('POST', `/conversations/${conversation.id}/messages`, {
      body: 'Hello console, from bob',
      clientMessageId: '44444444-4444-4444-8444-444444444444',
    });
    await waitFor(() => notifications(app).some((text) => text === 'AKENO: it_bob: Hello console, from bob'), 'the message notification');
    const events = await appApi(app, 'GET', `/api/events?after=${before}`);
    const created = events.body.events.find((e) => e.type === 'message.created');
    assert.ok(created, events.text);
    assert.equal(created.data.message.body, 'Hello console, from bob');

    const party = await bob.request('POST', '/parties', { name: 'Console party', privacy: 'invite_only', maxMembers: 4 });
    assert.equal(party.status, 201, JSON.stringify(party.body));
    assert.equal((await bob.request('POST', `/parties/${party.body.id}/invitations`, { userId: alice.user.id })).status, 201);
    await waitFor(() => notifications(app).some((text) => text.includes('invited you to the party "Console party"')), 'the invitation notification');
    const invitations = await appApi(app, 'GET', '/api/v1/invitations');
    const invitation = invitations.body.data.invitations.find((i) => i.target.name === 'Console party');
    assert.ok(invitation, invitations.text);
    assert.equal((await appApi(app, 'POST', `/api/v1/invitations/${invitation.id}/accept`)).body.ok, true);
    const current = await appApi(app, 'GET', '/api/v1/parties/current');
    assert.equal(current.body.data.party.name, 'Console party');
    assert.equal(current.body.data.party.members.length, 2);
    const presence = await appApi(app, 'PUT', '/api/v1/presence', { status: 'away' });
    assert.equal(presence.body.ok, true);
    assert.equal((await alice.request('GET', '/users/me')).body.presencePreference, 'away');
    await appApi(app, 'PUT', '/api/v1/presence', { status: 'online' });
  });


  await step('console app: the page works in a browser with keyboard-only navigation', async () => {
    let chromium = null;
    try {
      ({ chromium } = createRequire(resolve(root, 'apps/web/package.json'))('@playwright/test'));
    } catch {
      chromium = null;
    }
    if (!chromium) {
      if (process.env.AKENO_REQUIRE_BROWSER) throw new Error('Playwright is not installed (pnpm install)');
      log('       (skipped: Playwright not installed)');
      return;
    }
    const shots = process.env.AKENO_UI_SCREENSHOTS;
    const browser = await chromium.launch();
    try {
      const page = await browser.newPage({ viewport: { width: 1920, height: 1080 } });
      const errors = [];
      page.on('pageerror', (error) => errors.push(String(error)));
      // Expected 4xx answers (e.g. a rejected server address) are logged by the browser; they are not script errors.
      page.on('console', (message) => message.type() === 'error' && !/^Failed to load resource: the server responded with a status of 4\d\d/.test(message.text()) && errors.push(message.text()));
      await page.goto(`http://127.0.0.1:${app.port}/#t=${app.token}`);
      await page.getByRole('heading', { name: /Friends · \d+ of \d+ online/ }).waitFor();
      assert.equal(new URL(page.url()).hash, '', 'the secret is removed from the address bar');
      await page.getByText('@it_bob').first().waitFor();
      if (shots) await page.screenshot({ path: join(shots, '1-friends.png') });

      // Keyboard only: focus starts in the page; arrows move between controls, Enter activates.
      await page.keyboard.press('ArrowDown');
      const focused = await page.evaluate(() => document.activeElement && document.activeElement.id);
      assert.match(focused, /^friend-/, `focus moved to a friend (got ${focused})`);
      await page.keyboard.press('Enter');
      await page.locator('#composer-input').waitFor();
      await page.getByText('Sent from the console page ✓').waitFor();
      await page.locator('#composer-input').fill('Typed on the console page');
      await page.keyboard.press('Enter');
      await page.locator('.bubble.mine', { hasText: 'Typed on the console page' }).waitFor();
      if (shots) await page.screenshot({ path: join(shots, '2-chat.png') });
      const conversations = await bob.request('GET', '/conversations');
      const conversation = conversations.body.conversations.find((entry) => entry.otherUser.onlineId === 'it_alice');
      const history = await bob.request('GET', `/conversations/${conversation.id}/messages`);
      assert.equal(history.body.messages.at(-1).body, 'Typed on the console page');

      // A reply arrives live through the realtime feed.
      await bob.request('POST', `/conversations/${conversation.id}/messages`, {
        body: 'Live reply from bob',
        clientMessageId: '66666666-6666-4666-8666-666666666666',
      });
      await page.locator('.bubble', { hasText: 'Live reply from bob' }).waitFor({ timeout: 15_000 });

      for (const [tab, check, file] of [
        ['Invitations', 'Recent activity', '3-invitations.png'],
        ['Party', 'Party chat', '4-party.png'],
        ['Game sessions', 'My game sessions', '5-sessions.png'],
        ['Diagnostics', 'Secure connection', '6-diagnostics.png'],
        ['Settings', 'Notifications', '7-settings.png'],
      ]) {
        await page.locator('.tab', { hasText: tab }).click();
        await page.getByRole('heading', { name: check, exact: true }).waitFor();
        if (tab === 'Diagnostics') await page.getByText(/^mbedTLS 3\.6\.7$/).waitFor();
        if (shots) await page.screenshot({ path: join(shots, file), fullPage: true });
      }
      assert.deepEqual(errors, [], 'no script errors');
    } finally {
      await browser.close();
    }
  });

  await step('console app: notification settings are respected', async () => {
    assert.equal((await appApi(app, 'POST', '/api/settings', { notifyMessages: false })).body.ok, true);
    const conversations = await bob.request('GET', '/conversations');
    const conversation = conversations.body.conversations.find((entry) => entry.otherUser.onlineId === 'it_alice');
    const before = (await appState(app)).eventsNext;
    await bob.request('POST', `/conversations/${conversation.id}/messages`, { body: 'Muted message', clientMessageId: '55555555-5555-4555-8555-555555555555' });
    let seen = false;
    for (let i = 0; i < 40 && !seen; i += 1) {
      const events = await appApi(app, 'GET', `/api/events?after=${before}`);
      seen = events.body.events.some((e) => e.type === 'message.created' && e.data.message.body === 'Muted message');
      if (!seen) await sleep(100);
    }
    assert.ok(seen, 'the event still reaches the page');
    await sleep(300);
    assert.ok(!notifications(app).some((text) => text.includes('Muted message')), 'no system notification');
    assert.equal((await appApi(app, 'POST', '/api/settings', { notifyMessages: true })).body.ok, true);
    const changeServer = await appApi(app, 'POST', '/api/settings', { server: 'https://other.example' });
    assert.equal(changeServer.status, 409);
  });

  await step('console app: diagnostics report the verified TLS connection', async () => {
    const diagnostics = await appApi(app, 'GET', '/api/diagnostics');
    assert.equal(diagnostics.body.ok, true);
    assert.match(diagnostics.body.tls.backend, /^mbedTLS 3\.6\.7$/);
    assert.equal(diagnostics.body.tls.customCa, true);
    assert.equal(diagnostics.body.tls.entropy, 'getrandom()');
    assert.equal(diagnostics.body.credential.mode, '600');
    assert.ok(diagnostics.body.capabilities.some((c) => c.feature === 'native-friends-list' && c.state === 'unsupported'));
    const test = await appApi(app, 'POST', '/api/diagnostics/connection');
    assert.equal(test.body.ok, true, test.text);
    assert.match(test.body.tls, /TLS/);
  });

  await step('console app: a second launch asks the running app to reopen its page', async () => {
    const opens = app.events.filter((e) => e.event === 'open-browser').length;
    const second = await startApp(appData, ['--port', String(app.port), '--server', server, '--ca-file', caFile]);
    const exit = await Promise.race([second.exited, sleep(10_000).then(() => 'timeout')]);
    assert.notEqual(exit, 'timeout');
    assert.equal(exit.code, 0, second.stderr);
    await waitFor(() => app.events.filter((e) => e.event === 'open-browser').length === opens + 1, 'the page to be reopened');
    assert.equal(app.exit, null, 'the first instance keeps running');
  });

  await step('console app: forgets its credential when unpaired on the website', async () => {
    const devices = await alice.request('GET', '/devices');
    const device = devices.body.devices.find((d) => d.name === 'PlayStation 5');
    assert.ok(device, JSON.stringify(devices.body));
    assert.equal((await alice.request('DELETE', `/devices/${device.id}`)).status, 204);
    await waitFor(() => notifications(app).some((text) => text.includes('signed out')), 'the signed-out notification');
    let state = null;
    for (let i = 0; i < 50 && !(state && !state.paired); i += 1) {
      state = await appState(app);
      if (state.paired) await sleep(100);
    }
    assert.equal(state.paired, false);
    assert.equal(existsSync(join(appData, 'device.json')), false);
    assert.equal((await appApi(app, 'GET', '/api/v1/friends')).status, 401);
  });

  await step('console app: exits cleanly when asked from its page', async () => {
    assert.equal((await appApi(app, 'POST', '/api/exit')).body.ok, true);
    const exit = await Promise.race([app.exited, sleep(10_000).then(() => 'timeout')]);
    assert.notEqual(exit, 'timeout');
    assert.equal(exit.code, 0, app.stderr);
    assert.ok(notifications(app).includes('AKENO NETWORK stopped.'));
    assert.equal(existsSync(join(appData, 'instance.key')), false);
  });

  await step('console app: refuses a server whose certificate is not trusted', async () => {
    const untrusted = await startApp(join(work, 'app-untrusted'), ['--port', '0', '--server', server]);
    const started = await appApi(untrusted, 'POST', '/api/pair/start');
    assert.equal(started.status, 502);
    assert.match(started.body.error.message, /certificate/);
    const test = await appApi(untrusted, 'POST', '/api/diagnostics/connection');
    assert.equal(test.body.ok, false);
    assert.match(test.body.detail, /not trusted/);
    untrusted.child.kill('SIGTERM');
    assert.equal((await untrusted.exited).code, 0);
  });


  await step('console app: first-run setup and pairing screens work in a browser', async () => {
    let chromium = null;
    try {
      ({ chromium } = createRequire(resolve(root, 'apps/web/package.json'))('@playwright/test'));
    } catch {
      chromium = null;
    }
    if (!chromium) {
      if (process.env.AKENO_REQUIRE_BROWSER) throw new Error('Playwright is not installed (pnpm install)');
      log('       (skipped: Playwright not installed)');
      return;
    }
    const shots = process.env.AKENO_UI_SCREENSHOTS;
    const fresh = await startApp(join(work, 'app-fresh'), ['--port', '0', '--ca-file', caFile]);
    const browser = await chromium.launch();
    try {
      const page = await browser.newPage({ viewport: { width: 1920, height: 1080 } });
      const errors = [];
      page.on('pageerror', (error) => errors.push(String(error)));
      // Expected 4xx answers (e.g. a rejected server address) are logged by the browser; they are not script errors.
      page.on('console', (message) => message.type() === 'error' && !/^Failed to load resource: the server responded with a status of 4\d\d/.test(message.text()) && errors.push(message.text()));
      await page.goto(`http://127.0.0.1:${fresh.port}/#t=${fresh.token}`);
      await page.getByRole('heading', { name: 'Welcome to AKENO NETWORK' }).waitFor();
      if (shots) await page.screenshot({ path: join(shots, '0-setup.png') });
      await page.locator('#setup-server').fill('http://plain.example');
      await page.locator('#setup-save').click();
      await page.getByText(/Enter the server address as https:\/\/host/).waitFor();
      await page.locator('#setup-server').fill(server);
      await page.locator('#setup-name').fill('Living room PS5');
      await page.locator('#setup-save').click();
      await page.getByRole('heading', { name: 'Pair this console with your account' }).waitFor();
      await page.locator('#pair-start').click();
      const code = await page.locator('#pair-code').textContent({ timeout: 15_000 });
      assert.match(code, /^[A-Z0-9]{4}-[A-Z0-9]{4}$/);
      const lookup = await alice.request('POST', '/devices/pairings/lookup', { userCode: code });
      assert.equal(lookup.body.pairing.deviceName, 'Living room PS5');
      if (shots) await page.screenshot({ path: join(shots, '0-pairing-code.png') });
      await page.locator('#pair-cancel').click();
      await page.getByText('Pairing was cancelled.').waitFor({ timeout: 10_000 });
      assert.deepEqual(errors, [], 'no script errors');
      await page.locator('#exit-btn').click({ noWaitAfter: true }).catch(() => {});
    } finally {
      await browser.close();
      fresh.child.kill('SIGTERM');
      await fresh.exited;
    }
  });

  await step('console app: never writes tokens or device codes to its logs', async () => {
    const logs = readFileSync(join(appData, 'akeno.log'), 'utf8') + allStderr.join('\n');
    assert.doesNotMatch(logs, /akd_[A-Za-z0-9_-]{10,}/);
    assert.doesNotMatch(logs, /akp_[A-Za-z0-9_-]{10,}/);
    assert.match(logs, /pairing.started/);
  });
}

// ---- Teardown --------------------------------------------------------------------------------

for (const child of children) child.kill('SIGKILL');
for (const s of servers) s.close();
await stopBackend().catch(() => {});
const cleanup = new pg.Client({ connectionString: adminUrl });
await cleanup.connect();
await cleanup.query(`DROP DATABASE IF EXISTS ${databaseName} WITH (FORCE)`);
await cleanup.end();
rmSync(work, { recursive: true, force: true });

log(failures === 0 ? 'All client integration tests passed.' : `${failures} client integration test(s) failed.`);
process.exit(failures === 0 ? 0 : 1);
