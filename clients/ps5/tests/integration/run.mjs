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
// Usage: node clients/ps5/tests/integration/run.mjs <path-to-akeno-cli>
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
