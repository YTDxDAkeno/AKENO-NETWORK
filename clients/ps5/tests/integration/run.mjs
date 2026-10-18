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
//        [--audiolab <path-to-akeno-app-audiolab>]
// With --app, the console app (host build) is also tested against the same server; with
// --audiolab, the experimental audio lab build (PS5 voice research, simulated audio).
// Requires: `pnpm build`, PostgreSQL (TEST_DATABASE_ADMIN_URL), openssl on PATH.
import assert from 'node:assert/strict';
import { spawn, spawnSync } from 'node:child_process';
import { mkdtempSync, readFileSync, readdirSync, rmSync, statSync, writeFileSync, existsSync, copyFileSync } from 'node:fs';
import { createRequire } from 'node:module';
import { createHash } from 'node:crypto';
import http from 'node:http';
import net from 'node:net';
import { tmpdir } from 'node:os';
import { dirname, join, resolve } from 'node:path';
import tls from 'node:tls';
import { deflateSync } from 'node:zlib';
import { fileURLToPath } from 'node:url';

const root = resolve(dirname(fileURLToPath(import.meta.url)), '../../../..');
const serverRequire = createRequire(resolve(root, 'apps/server/package.json'));
const pg = serverRequire('pg');
const cli = resolve(process.argv[2] ?? 'clients/ps5/build/akeno-cli');
const appIndex = process.argv.indexOf('--app');
const appBinary = appIndex > 0 ? resolve(process.argv[appIndex + 1]) : null;
const audioLabIndex = process.argv.indexOf('--audiolab');
const audioLabBinary = audioLabIndex > 0 ? resolve(process.argv[audioLabIndex + 1]) : null;
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

// A minimal stand-in for LiveKit's room API so the server can run with SharePlay enabled. Joins
// and published tracks are reported with signed webhooks, as LiveKit would (SharePlay step).
const LIVEKIT_KEY = 'akeno-it';
const LIVEKIT_SECRET = 'akeno-it-secret-that-is-long-enough-0123456789';
const fakeLiveKit = http.createServer((request, response) => {
  request.resume();
  request.on('end', () => {
    const method = (request.url ?? '').split('/').pop();
    response.writeHead(200, { 'content-type': 'application/json' });
    response.end(JSON.stringify(method === 'ListRooms' ? { rooms: [] } : method === 'ListParticipants' ? { participants: [] } : {}));
  });
});
await new Promise((done) => fakeLiveKit.listen(0, '127.0.0.1', done));
const fakeLiveKitUrl = `ws://127.0.0.1:${fakeLiveKit.address().port}`;
servers.push(fakeLiveKit);
const databaseUrl = new URL(adminUrl);
databaseUrl.pathname = `/${databaseName}`;
const serverEnv = {
  MEDIA_DIR: join(work, 'media'),
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
  LIVEKIT_URL: fakeLiveKitUrl,
  LIVEKIT_API_KEY: LIVEKIT_KEY,
  LIVEKIT_API_SECRET: LIVEKIT_SECRET,
  SHAREPLAY_ENABLED: 'true',
  SHAREPLAY_REMOTE_INPUT_ENABLED: 'true',
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
  async upload(path, data, type = 'image/png') {
    const response = await fetch(`http://127.0.0.1:${backendPort}/api/v1${path}`, {
      method: 'PUT',
      headers: { Origin: origin, Cookie: this.cookie, 'X-CSRF-Token': this.csrf, 'Content-Type': type },
      body: data,
    });
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
    if (await predicate()) return;
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
async function startApp(dataDir, args = [], env = {}, binary = appBinary) {
  const child = spawn(binary, ['--data-dir', dataDir, ...args], { env: { ...process.env, ...env }, stdio: ['ignore', 'pipe', 'pipe'] });
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

/** A real RGB PNG generated without dependencies (diagonal two-colour split). */
function pngImage(width, height) {
  const table = Array.from({ length: 256 }, (_, n) => {
    let c = n;
    for (let k = 0; k < 8; k++) c = c & 1 ? 0xedb88320 ^ (c >>> 1) : c >>> 1;
    return c >>> 0;
  });
  const crc = (buffer) => {
    let c = 0xffffffff;
    for (const byte of buffer) c = table[(c ^ byte) & 0xff] ^ (c >>> 8);
    return (c ^ 0xffffffff) >>> 0;
  };
  const chunk = (type, data) => {
    const length = Buffer.alloc(4);
    length.writeUInt32BE(data.length);
    const body = Buffer.concat([Buffer.from(type, 'latin1'), data]);
    const sum = Buffer.alloc(4);
    sum.writeUInt32BE(crc(body));
    return Buffer.concat([length, body, sum]);
  };
  const rows = [];
  for (let y = 0; y < height; y++) {
    const row = Buffer.alloc(1 + width * 3);
    for (let x = 0; x < width; x++) row.set(x + y < width ? [40, 90, 200] : [230, 120, 60], 1 + x * 3);
    rows.push(row);
  }
  const ihdr = Buffer.alloc(13);
  ihdr.writeUInt32BE(width, 0);
  ihdr.writeUInt32BE(height, 4);
  ihdr[8] = 8;
  ihdr[9] = 2;
  return Buffer.concat([Buffer.from([0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a]), chunk('IHDR', ihdr),
    chunk('IDAT', deflateSync(Buffer.concat(rows))), chunk('IEND', Buffer.alloc(0))]);
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
  let json;
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
    app = await startApp(appData, ['--port', '0', '--server', server, '--ca-file', caFile], {
      AKENO_BACKOFF_BASE_MS: '300',
      // Account Bridge simulation: a selected local user, readable registry, no account ID, Remote Play on.
      AKENO_SIM_CONSOLE_USER: '268435457:Justin PS5',
      AKENO_SIM_REGMGR: 'present',
      AKENO_SIM_REMOTEPLAY: 'present',
      AKENO_SIM_USER_SLOT: '1',
      AKENO_SIM_ACCOUNT_ID: '0',
      AKENO_SIM_RP_GLOBAL: '1',
    });
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
    assert.ok(notifications(app).includes('AKENO NETWORK Alpha 0.5 started.'));
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
    await waitFor(() => app.stderr.includes('realtime.state connected'), 'the realtime connection', 20_000);
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

  await step('console app: receives SharePlay Player 2 input as a diagnostic receiver (no injection)', async () => {
    const { AccessToken } = serverRequire('livekit-server-sdk');
    const WebSocket = serverRequire('ws');
    const current = await appApi(app, 'GET', '/api/v1/parties/current');
    const partyId = current.body.data.party.id;
    const room = `akeno-shareplay-${partyId}`;
    const webhook = async (event) => {
      const body = JSON.stringify({ id: `EV_${Math.random()}`, createdAt: String(Math.floor(Date.now() / 1000)), ...event });
      const token = new AccessToken(LIVEKIT_KEY, LIVEKIT_SECRET);
      token.sha256 = createHash('sha256').update(body).digest('base64');
      const response = await fetch(`http://127.0.0.1:${backendPort}/api/v1/voice/webhook`, {
        method: 'POST',
        headers: { 'content-type': 'application/webhook+json', authorization: await token.toJwt() },
        body,
      });
      assert.equal(response.status, 204);
    };
    // Before anything arrives: the receiver is empty and both adapters say "unsupported".
    let local = await appApi(app, 'GET', '/api/shareplay');
    assert.equal(local.body.ok, true, local.text);
    assert.equal(local.body.data.receiver.grantId, null);
    assert.equal(local.body.data.capabilities.virtualController.supported, false);
    assert.equal(local.body.data.capabilities.nativeCapture.supported, false);
    const page = await appApi(app, 'GET', '/shareplay.js');
    assert.equal(page.status, 200);
    assert.match(page.text, /AkenoSharePlay/);

    // alice (this console's account) hosts from the website; bob watches and asks for Player 2.
    assert.equal((await alice.request('POST', `/parties/${partyId}/shareplay/start`, { source: 'capture-device' })).status, 201);
    await webhook({ event: 'participant_joined', room: { name: room }, participant: { identity: alice.user.id } });
    await webhook({ event: 'track_published', room: { name: room }, participant: { identity: alice.user.id }, track: { sid: 'TR_1', type: 'VIDEO', source: 'SCREEN_SHARE' } });
    assert.equal((await bob.request('POST', `/parties/${partyId}/shareplay/token`)).status, 200);
    await webhook({ event: 'participant_joined', room: { name: room }, participant: { identity: bob.user.id } });
    const viaConsole = await appApi(app, 'GET', `/api/v1/parties/${partyId}/shareplay`);
    assert.equal(viaConsole.body.data.session.state, 'live', viaConsole.text);
    assert.equal((await bob.request('POST', `/parties/${partyId}/shareplay/controller/request`)).status, 204);
    const grant = (await alice.request('POST', `/parties/${partyId}/shareplay/controller/approve`, { userId: bob.user.id })).body;
    assert.ok(grant.epoch > 0, JSON.stringify(grant));

    // The console app opts in (its SharePlay tab does this while open); older apps never do.
    const registered = await appApi(app, 'POST', `/api/v1/parties/${partyId}/shareplay/receiver`, {});
    assert.equal(registered.body.ok, true, registered.text);
    const socket = new WebSocket(`ws://127.0.0.1:${backendPort}/api/v1/shareplay/input?party=${partyId}&role=sender&epoch=${grant.epoch}`, {
      headers: { Origin: origin, Cookie: bob.cookie },
    });
    const ready = new Promise((done, fail) => {
      socket.once('message', (data) => done(JSON.parse(data.toString())));
      socket.once('error', fail);
    });
    assert.equal((await ready).type, 'ready');
    const packet = (seq, buttons, lx) => {
      const bytes = Buffer.alloc(32);
      bytes.writeUInt8(0xa7, 0);
      bytes.writeUInt8(1, 1);
      bytes.writeUInt8(1, 2);
      bytes.writeUInt8(2, 3);
      bytes.writeUInt32LE(grant.epoch, 4);
      bytes.writeUInt32LE(seq, 8);
      bytes.writeUInt32LE(seq * 16, 12);
      bytes.writeUInt32LE(buttons, 16);
      bytes.writeInt16LE(lx, 20);
      bytes.writeUInt8(200, 29);
      return bytes;
    };
    socket.send(packet(1, 0b1001, 32767)); // Cross + Triangle, left stick right
    await waitFor(async () => (await appApi(app, 'GET', '/api/shareplay')).body.data.receiver.buttons === 0b1001, 'the relayed state on the console');
    local = await appApi(app, 'GET', '/api/shareplay');
    assert.equal(local.body.data.receiver.grantId, grant.grantId);
    assert.deepEqual(local.body.data.receiver.axes, [32767, 0, 0, 0]);
    assert.deepEqual(local.body.data.receiver.triggers, [0, 200]);
    assert.equal(local.body.data.receiver.neutralByWatchdog, false);
    // Not queued as page events (up to 120 per second).
    const events = await appApi(app, 'GET', '/api/events?after=0');
    assert.ok(!events.body.events.some((event) => event.type === 'shareplay.input'));

    // Without fresh input the receiver releases everything after its watchdog (500 ms).
    await sleep(700);
    local = await appApi(app, 'GET', '/api/shareplay');
    assert.equal(local.body.data.receiver.neutralByWatchdog, true);
    assert.equal(local.body.data.receiver.buttons, 0);
    // Revocation is relayed as a release with its reason.
    socket.send(packet(2, 0b10, 0));
    await waitFor(async () => (await appApi(app, 'GET', '/api/shareplay')).body.data.receiver.buttons === 0b10, 'the second state');
    assert.equal((await alice.request('POST', `/parties/${partyId}/shareplay/controller/revoke`)).status, 204);
    await waitFor(async () => (await appApi(app, 'GET', '/api/shareplay')).body.data.receiver.lastReason === 'The host revoked Player 2.', 'the release');
    local = await appApi(app, 'GET', '/api/shareplay');
    assert.equal(local.body.data.receiver.buttons, 0);
    assert.equal(local.body.data.receiver.releases, 1);
    socket.close();
    assert.equal((await alice.request('POST', `/parties/${partyId}/shareplay/stop`)).status, 204);
  });


  await step('console app: links the local console user and runs the opt-in read-only account check', async () => {
    // The selected local user is reported, nothing is linked or checked yet.
    let bridge = (await appState(app)).accountBridge;
    assert.equal(bridge.haveUser, true, JSON.stringify(bridge));
    assert.equal(bridge.userName, 'Justin PS5');
    assert.equal(bridge.linkState, '');
    assert.equal(bridge.checked, false);

    // Linking reuses the paired device credential; the website sees a local, unverified user.
    const linked = await appApi(app, 'POST', '/api/account-bridge/link', {});
    assert.equal(linked.body.ok, true, linked.text);
    assert.equal(linked.body.data.linkState, 'linked');
    const devices = await alice.request('GET', '/devices');
    const consoleUser = devices.body.devices[0].consoleUser;
    assert.ok(consoleUser, JSON.stringify(devices.body.devices[0]));
    assert.equal(consoleUser.userName, 'Justin PS5');
    assert.equal(consoleUser.accountState, 'not_checked');

    // The opt-in read-only check: functions found, no account ID, so pairing is blocked. Nothing written.
    const checked = await appApi(app, 'POST', '/api/account-bridge/check', {});
    assert.equal(checked.body.data.checked, true, checked.text);
    assert.equal(checked.body.data.accountState, 'no_account_id');
    assert.equal(checked.body.data.assessment.code, 'blocked-no-account-id');
    assert.ok(checked.body.data.presence.remoteplayLoaded);
    // The account ID value itself is never exposed, only its presence.
    assert.equal(/\baccountId"\s*:/.test(checked.text), false, checked.text);

    // Linking again with the check result records the state on the server.
    const relinked = await appApi(app, 'POST', '/api/account-bridge/link', { check: true });
    assert.equal(relinked.body.data.accountState, 'no_account_id');
    const after = await alice.request('GET', '/devices');
    assert.equal(after.body.devices[0].consoleUser.accountState, 'no_account_id');

    // Unlinking from the console removes it; the device stays paired.
    const unlinked = await appApi(app, 'POST', '/api/account-bridge/unlink', {});
    assert.equal(unlinked.body.ok, true);
    assert.equal(unlinked.body.data.linkState, '');
    assert.equal((await alice.request('GET', '/devices')).body.devices[0].consoleUser, null);

    // The read-only steps are logged step by step so a crash would name the exact operation.
    const diagnostics = await appApi(app, 'GET', '/api/diagnostics');
    assert.ok(diagnostics.body.log.some((line) => line.includes('bridge.read.begin')), diagnostics.text);
  });

  await step('console app: the page works in a browser with keyboard-only navigation', async () => {
    let chromium;
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
      const focused = await page.evaluate('document.activeElement && document.activeElement.id');
      assert.match(focused, /^friend-/, `focus moved to a friend (got ${focused})`);
      // Selecting a friend opens their profile; its first action (Message) takes the focus.
      await page.keyboard.press('Enter');
      await page.locator('#profile-name', { hasText: 'it_bob' }).waitFor();
      await page.locator('#profile-message:focus').waitFor();
      if (shots) await page.screenshot({ path: join(shots, '1b-friend-profile.png') });
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
        ['Profile', 'Recently played', '3-profile.png'],
        ['Games', 'On this console', '3-games.png'],
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
    let chromium;
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

      // "Change server" stays open through the periodic state refreshes and keeps what was typed.
      await page.locator('#change-server').click();
      await page.getByRole('heading', { name: 'Change server' }).waitFor();
      await page.locator('#setup-server').fill('https://typed-but-not-saved.example');
      await page.waitForTimeout(7000); // more than two refresh cycles (every 3 s)
      await page.getByRole('heading', { name: 'Change server' }).waitFor({ timeout: 100 });
      assert.equal(await page.locator('#setup-server').inputValue(), 'https://typed-but-not-saved.example');
      if (shots) await page.screenshot({ path: join(shots, '0-change-server.png') });
      await page.locator('#setup-cancel').click();
      await page.getByRole('heading', { name: 'Pair this console with your account' }).waitFor();
      assert.equal((await appState(fresh)).server, server, 'cancel changes nothing');
      // Circle / Escape also closes it without saving.
      await page.locator('#change-server').click();
      await page.locator('#setup-server').fill('https://also-not-saved.example');
      await page.locator('#setup-name').focus();
      await page.keyboard.press('Escape');
      await page.getByRole('heading', { name: 'Pair this console with your account' }).waitFor();
      assert.equal((await appState(fresh)).server, server);
      // Diagnostics from the pairing screen also stays until Back.
      await page.locator('#pair-diag').click();
      await page.getByRole('heading', { name: 'Secure connection' }).waitFor();
      await page.waitForTimeout(3500);
      await page.getByRole('heading', { name: 'Secure connection' }).waitFor({ timeout: 100 });
      await page.locator('#diag-back').click();
      await page.getByRole('heading', { name: 'Pair this console with your account' }).waitFor();
      assert.deepEqual(errors, [], 'no script errors');
      await page.locator('#exit-btn').click({ noWaitAfter: true }).catch(() => {});
    } finally {
      await browser.close();
      fresh.child.kill('SIGTERM');
      await fresh.exited;
    }
  });


  await step('console app: pairs through notifications when the browser cannot be opened', async () => {
    const dataDir = join(work, 'app-nobrowser');
    const first = await startApp(dataDir, ['--port', '0', '--server', server, '--ca-file', caFile]);
    first.child.kill('SIGTERM'); // Only to write settings.json, as a user would via FTP.
    await first.exited;
    const headless = await startApp(dataDir, ['--port', '0', '--ca-file', caFile, '--open-browser'], { AKENO_TEST_BROWSER_FAILS: '1' });
    await waitFor(() => notifications(headless).some((text) => /Pairing code [A-Z0-9]{4}-[A-Z0-9]{4}/.test(text)), 'the pairing code notification');
    assert.ok(notifications(headless).some((text) => text.includes('The browser could not be opened')));
    const code = /Pairing code ([A-Z0-9]{4}-[A-Z0-9]{4})/.exec(notifications(headless).find((text) => text.includes('Pairing code')))[1];
    assert.equal((await alice.request('POST', '/devices/pairings/approve', { userCode: code })).status, 204);
    await waitFor(() => notifications(headless).some((text) => text.includes('paired as it_alice')), 'the paired notification', 20_000);
    const diagnostics = await appApi(headless, 'GET', '/api/diagnostics');
    assert.equal(diagnostics.body.ui.browserLastResult, -1);
    headless.child.kill('SIGTERM');
    assert.equal((await headless.exited).code, 0);
    const devices = await alice.request('GET', '/devices');
    for (const device of devices.body.devices) await alice.request('DELETE', `/devices/${device.id}`);
  });

  // ---- Alpha 0.2: game activity and quick access (simulated process list, real server) ----------

  /** Waits for an async condition. */
  async function waitUntil(predicate, what, timeoutMs = 20_000) {
    const deadline = Date.now() + timeoutMs;
    let last;
    while (Date.now() < deadline) {
      last = await predicate();
      if (last) return last;
      await sleep(100);
    }
    throw new Error(`Timed out waiting for ${what}`);
  }

  async function dbQuery(text, params = []) {
    const client = new pg.Client({ connectionString: databaseUrl.toString() });
    await client.connect();
    try {
      return (await client.query(text, params)).rows;
    } finally {
      await client.end();
    }
  }

  /** Pairs a running console app with `approver`, granting `scopes` (default: all requested). */
  async function pairApp(inst, approver, scopes) {
    const started = await appApi(inst, 'POST', '/api/pair/start');
    assert.equal(started.status, 200, started.text);
    const code = (await appState(inst)).pairing.userCode;
    const approved = await approver.request('POST', '/devices/pairings/approve', { userCode: code, ...(scopes ? { scopes } : {}) });
    assert.equal(approved.status, 204, JSON.stringify(approved.body));
    await waitUntil(async () => (await appState(inst)).paired, 'the console app to be paired', 25_000);
  }

  const carol = await registerUser(adminSession, 'it_carol');
  {
    const friendRequest = await carol.request('POST', '/friends/requests', { userId: bob.user.id });
    assert.equal((await bob.request('POST', `/friends/requests/${friendRequest.body.request.id}/accept`)).status, 200);
  }
  const playerData = join(work, 'app-player');
  const procs = join(work, 'processes.txt');
  const metaRoot = join(work, 'console-root');
  spawnSync('mkdir', ['-p', join(metaRoot, 'system_data/priv/appmeta/PPSA01234')]);
  writeFileSync(join(metaRoot, 'system_data/priv/appmeta/PPSA01234/param.json'),
    JSON.stringify({ titleId: 'PPSA01234', localizedParameters: { defaultLanguage: 'en-US', 'en-US': { titleName: 'Racing Game' } } }));
  writeFileSync(procs, '');
  const playerEnv = {
    AKENO_SIM_PROCESSES: procs,
    AKENO_SIM_METADATA_ROOT: metaRoot,
    AKENO_ACTIVITY_SCAN_MS: '150',
    AKENO_ACTIVITY_HEARTBEAT_MS: '4000',
    AKENO_TICKET_MS: '4000',
    AKENO_BACKOFF_BASE_MS: '300',
  };
  let player = null;
  const carolActivity = async () => (await bob.request('GET', '/users/it_carol/activity')).body;
  const sessionRows = () => dbQuery(
    `SELECT s.title_id, s.ended_at, s.end_reason, s.last_heartbeat_at FROM game_activity_sessions s JOIN users u ON u.id = s.user_id
     WHERE u.online_id = 'it_carol' ORDER BY s.started_at`);
  const localActivity = async () => (await appState(player)).activity;

  await step('console app: game activity needs the permission; the owner can grant it later (no re-pairing)', async () => {
    player = await startApp(playerData, ['--port', '0', '--server', server, '--ca-file', caFile], playerEnv);
    // Paired like an Alpha 0.1 console: everything it asked for except activity:write.
    await pairApp(player, carol, ['profile:read', 'friends:read', 'friends:write', 'messages:read', 'messages:write', 'realtime',
      'invitations:read', 'invitations:write', 'parties:read', 'parties:write', 'sessions:read', 'sessions:write', 'presence:write']);
    writeFileSync(procs, '100 NPXS40028\n200 PPSA01234\n201 PPSA01234\n');
    await waitUntil(async () => (await localActivity()).state === 'permission-missing', 'the missing permission to be reported');
    assert.match((await localActivity()).detail, /Allow game activity/);
    assert.equal((await carolActivity()).current, null);

    const devices = await carol.request('GET', '/devices');
    const device = devices.body.devices[0];
    const granted = await carol.request('PATCH', `/devices/${device.id}`, { scopes: [...device.scopes, 'activity:write'] });
    assert.equal(granted.status, 200, JSON.stringify(granted.body));
    await waitUntil(async () => (await carolActivity()).current, 'the game to be shared after the permission was granted', 30_000);
    const activity = await carolActivity();
    assert.deepEqual({ titleId: activity.current.titleId, name: activity.current.name, platform: activity.current.platform },
      { titleId: 'PPSA01234', name: 'Racing Game', platform: 'PS5' });
    const local = await localActivity();
    assert.equal(local.state, 'playing');
    assert.equal(local.name, 'Racing Game');
    assert.ok(local.startedAt, 'server start time known');
  });

  await step('console app: shows profile pictures and game icons as data: images (server media and local console icons)', async () => {
    // An uploaded profile picture reaches the page through the console app's bounded media fetch.
    const png = pngImage(300, 300);
    const uploaded = await carol.upload('/profile/avatar', png);
    assert.equal(uploaded.status, 200, JSON.stringify(uploaded.body));
    const mediaId = uploaded.body.profile.avatarImage;
    const media = await appApi(player, 'GET', `/api/media?id=${mediaId}&v=fallback`);
    assert.equal(media.status, 200, JSON.stringify(media.body));
    assert.equal(media.body.type, 'image/jpeg');
    assert.equal(Buffer.from(media.body.data, 'base64').subarray(0, 3).toString('hex'), 'ffd8ff');
    assert.equal((await appApi(player, 'GET', '/api/media?id=../../etc/passwd&v=fallback')).status, 400);
    assert.equal((await appApi(player, 'GET', `/api/media?id=${'0'.repeat(32)}&v=fallback`)).status, 404);
    // Without the page secret nothing is served.
    const anonymous = await fetch(`http://127.0.0.1:${player.port}/api/media?id=${mediaId}&v=fallback`);
    assert.equal(anonymous.status, 403);

    // Icons of games installed on this console come from its own metadata, read-only.
    spawnSync('mkdir', ['-p', join(metaRoot, 'user/appmeta/PPSA01234'), join(metaRoot, 'user/appmeta/CUSA00009')]);
    writeFileSync(join(metaRoot, 'user/appmeta/PPSA01234/icon0.png'), png);
    writeFileSync(join(metaRoot, 'user/appmeta/CUSA00009/icon0.png'), '<html>not a png</html>');
    const icon = await appApi(player, 'GET', '/api/local-art?title=PPSA01234');
    assert.equal(icon.status, 200);
    assert.equal(icon.body.type, 'image/png');
    assert.equal(icon.body.source, '/user/appmeta/PPSA01234/icon0.png');
    assert.equal(Buffer.from(icon.body.data, 'base64').equals(png), true);
    assert.equal((await appApi(player, 'GET', '/api/local-art?title=CUSA00009')).status, 404);
    assert.equal((await appApi(player, 'GET', '/api/local-art?title=..%2F..%2Fetc')).status, 404);
    assert.equal((await appApi(player, 'GET', '/api/local-art?title=NPXS40028')).status, 404);
  });

  await step('console app: detects a game switch, ignores ambiguity and reports the end of the game', async () => {
    writeFileSync(procs, '100 NPXS40028\n300 CUSA00001\n');
    await waitUntil(async () => (await carolActivity()).current?.titleId === 'CUSA00001', 'the switch to CUSA00001');
    let rows = await sessionRows();
    assert.equal(rows.find((r) => r.title_id === 'PPSA01234').end_reason, 'switched');

    // Two games at once: AKENO does not guess; the reported game ends.
    writeFileSync(procs, '100 NPXS40028\n300 CUSA00001\n400 PPSA01234\n');
    await waitUntil(async () => (await carolActivity()).current === null, 'ambiguity to end the activity');
    const diagnostics = (await appApi(player, 'GET', '/api/diagnostics')).body.activity;
    assert.equal(diagnostics.lastScan.result, 'ambiguous');
    assert.deepEqual(diagnostics.lastScan.candidates.sort(), ['CUSA00001', 'PPSA01234']);
    assert.match(diagnostics.lastScan.layout, /offset 20/);
    assert.match(diagnostics.focus, /not determined/);
    rows = await sessionRows();
    assert.equal(rows.find((r) => r.title_id === 'CUSA00001').end_reason, 'stopped');

    // A suspended (stopped) game is not activity; a running one is.
    writeFileSync(procs, '100 NPXS40028\n400 PPSA01234 stopped\n');
    await sleep(800);
    assert.equal((await carolActivity()).current, null);
    writeFileSync(procs, '100 NPXS40028\n400 PPSA01234\n');
    await waitUntil(async () => (await carolActivity()).current?.titleId === 'PPSA01234', 'the game to be detected again');
    writeFileSync(procs, '100 NPXS40028\n');
    await waitUntil(async () => (await carolActivity()).current === null, 'the closed game to end');
    assert.equal((await localActivity()).state, 'idle');
    rows = await sessionRows();
    assert.ok(rows.every((r) => r.ended_at !== null), JSON.stringify(rows));
  });

  await step('console app: an inconsistent application-info layout or a failed process list reports nothing', async () => {
    writeFileSync(procs, '100 NPXS40028 running ps\n200 PPSA01234 running alt\n');
    await waitUntil(async () => (await appApi(player, 'GET', '/api/diagnostics')).body.activity.lastScan.result === 'unverified-layout',
      'the layout conflict');
    const diagnostics = (await appApi(player, 'GET', '/api/diagnostics')).body.activity;
    assert.equal(diagnostics.lastScan.layout, 'conflicting');
    assert.deepEqual(diagnostics.lastScan.layoutVotes, { offset20: 1, offset16: 1 });
    assert.equal(diagnostics.lastScan.probes.length, 2);
    await sleep(600);
    assert.equal((await carolActivity()).current, null);

    // Every process with the other layout: detected at offset 16.
    writeFileSync(procs, '100 NPXS40028 running alt\n200 PPSA01234 running alt\n');
    await waitUntil(async () => (await carolActivity()).current?.titleId === 'PPSA01234', 'detection with the other layout');
    assert.match((await appApi(player, 'GET', '/api/diagnostics')).body.activity.lastScan.layout, /offset 16/);

    writeFileSync(procs, 'fail\n');
    await waitUntil(async () => (await localActivity()).state === 'unsupported', 'the listing failure');
    await waitUntil(async () => (await carolActivity()).current === null, 'the failure to end the activity');
  });

  await step('console app: heartbeats stop when the app is killed; a restart reports again', async () => {
    writeFileSync(procs, '100 NPXS40028\n200 PPSA01234\n');
    await waitUntil(async () => (await localActivity()).state === 'playing' && (await localActivity()).startedAt, 'the game to be reported');
    await waitUntil(async () => (await appApi(player, 'GET', '/api/diagnostics')).body.activity.heartbeatsOk >= 2, 'a periodic heartbeat', 15_000);
    player.child.kill('SIGKILL'); // A crash or a console going to rest mode without notice.
    await player.exited;
    const before = (await sessionRows()).find((r) => r.ended_at === null);
    assert.ok(before, 'the session stays open until the server sees no heartbeats');
    await sleep(5000);
    const after = (await sessionRows()).find((r) => r.ended_at === null);
    assert.equal(after.last_heartbeat_at.getTime(), before.last_heartbeat_at.getTime(), 'no heartbeats after the app died');

    player = await startApp(playerData, ['--port', '0', '--ca-file', caFile], playerEnv);
    await waitUntil(async () => (await localActivity()).state === 'playing' && (await localActivity()).startedAt, 'the game to be reported again');
    const resumed = (await sessionRows()).find((r) => r.ended_at === null);
    assert.ok(resumed.last_heartbeat_at.getTime() > before.last_heartbeat_at.getTime());
  });

  await step('console app: activity respects the console switch, website tracking and excluded games', async () => {
    assert.equal((await appApi(player, 'POST', '/api/settings', { activityEnabled: false })).body.ok, true);
    await waitUntil(async () => (await carolActivity()).current === null, 'the session to end when turned off on the console');
    await waitUntil(async () => (await localActivity()).state === 'off', 'the local state to be off');
    assert.equal(JSON.parse(readFileSync(join(playerData, 'settings.json'), 'utf8')).activityEnabled, false);
    assert.equal((await appApi(player, 'POST', '/api/settings', { activityEnabled: true })).body.ok, true);
    await waitUntil(async () => (await carolActivity()).current?.titleId === 'PPSA01234', 'sharing to resume');

    assert.equal((await carol.request('PATCH', '/profile/privacy', { gameExcludedTitles: ['PPSA01234'] })).status, 200);
    await waitUntil(async () => (await carolActivity()).current === null, 'the excluded game to end');
    await waitUntil(async () => (await localActivity()).state === 'excluded', 'the console to show the exclusion', 15_000);
    assert.equal((await carol.request('PATCH', '/profile/privacy', { gameExcludedTitles: [] })).status, 200);

    assert.equal((await carol.request('PATCH', '/profile/privacy', { gameActivityTracking: false })).status, 200);
    await waitUntil(async () => (await localActivity()).state === 'disabled-on-server', 'tracking off on the website', 30_000);
    assert.equal((await carol.request('PATCH', '/profile/privacy', { gameActivityTracking: true })).status, 200);
    await waitUntil(async () => (await carolActivity()).current?.titleId === 'PPSA01234', 'tracking to resume', 30_000);
  });

  let conversationId = null;
  await step('console app: recent messages, and a second launch opens the newest conversation', async () => {
    await waitFor(() => player.stderr.includes('realtime.state connected'), 'the realtime connection', 20_000);
    const conversation = await bob.request('POST', '/conversations', { userId: carol.user.id });
    conversationId = conversation.body.id;
    await bob.request('POST', `/conversations/${conversationId}/messages`, {
      body: 'Are you playing?',
      clientMessageId: '77777777-7777-4777-8777-777777777777',
    });
    await waitFor(() => notifications(player).includes('AKENO: it_bob: Are you playing?'), 'the message notification');
    const inbox = await appApi(player, 'GET', '/api/inbox');
    assert.equal(inbox.body.messages[0].open, `conversation:${conversationId}`);
    assert.equal(inbox.body.messages[0].from, 'it_bob');
    assert.equal(inbox.body.messages[0].preview, 'Are you playing?');
    const state = await appState(player);
    assert.equal(state.inbox.unseen, 1);

    const opens = () => player.events.filter((e) => e.event === 'open-browser').map((e) => e.url);
    const before = opens().length;
    const second = await startApp(playerData, ['--port', String(player.port), '--ca-file', caFile]);
    assert.equal((await second.exited).code, 0, second.stderr);
    await waitFor(() => opens().length === before + 1, 'the page to be reopened');
    assert.ok(opens().at(-1).endsWith(`&open=conversation:${conversationId}`), opens().at(-1));

    // Once the page has shown it, a second launch opens the default view again.
    assert.equal((await appApi(player, 'POST', '/api/inbox/seen', { seq: inbox.body.messages[0].seq })).body.ok, true);
    assert.equal((await appState(player)).inbox.unseen, 0);
    const third = await startApp(playerData, ['--port', String(player.port), '--ca-file', caFile]);
    assert.equal((await third.exited).code, 0);
    await waitFor(() => opens().length === before + 2, 'the page to be reopened again');
    assert.match(opens().at(-1), /^http:\/\/127\.0\.0\.1:\d+\/#t=[0-9a-f]{64}$/);
  });

  await step('console app (browser): quick-open link, drafts, newest-message banner, games and friends playing', async () => {
    let chromium;
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
    // bob plays a game on his own paired device: carol's console shows it in the friend list.
    const bobCredential = join(work, 'bob-device.json');
    assert.equal((await pair(bobCredential, bob, { name: 'Bob PS5' })).code, 0);
    const bobToken = JSON.parse(readFileSync(bobCredential, 'utf8')).accessToken;
    // Games are only shown while the player appears online: keep bob's device connected.
    const bobOnline = startCli(['--server', server, '--ca-file', caFile, '--credentials', bobCredential, 'listen']);
    await waitFor(() => bobOnline.stderr.includes('realtime.connected'), 'bob to be online', 20_000);
    const bobBeat = await fetch(`http://127.0.0.1:${backendPort}/api/v1/activity/heartbeat`, {
      method: 'POST',
      headers: { Authorization: `Bearer ${bobToken}`, 'Content-Type': 'application/json' },
      body: JSON.stringify({ titleId: 'CUSA00042', titleName: 'Bob Adventure', platform: 'PS5' }),
    });
    assert.equal(bobBeat.status, 200, await bobBeat.text());

    const shots = process.env.AKENO_UI_SCREENSHOTS;
    const browser = await chromium.launch();
    try {
      const page = await browser.newPage({ viewport: { width: 1920, height: 1080 } });
      const errors = [];
      page.on('pageerror', (error) => errors.push(String(error)));
      page.on('console', (message) => message.type() === 'error' && !/^Failed to load resource: the server responded with a status of 4\d\d/.test(message.text()) && errors.push(message.text()));

      // The link a second launch or a notification button opens: straight into the conversation.
      await page.goto(`http://127.0.0.1:${player.port}/#t=${player.token}&open=conversation:${conversationId}`);
      await page.locator('.bubble', { hasText: 'Are you playing?' }).waitFor({ timeout: 15_000 });
      assert.equal(new URL(page.url()).hash, '', 'the secret and the target are removed from the address bar');
      if (shots) await page.screenshot({ path: join(shots, '8-quick-open.png') });

      // A half-written reply survives an incoming message (the view re-renders).
      await page.locator('#composer-input').fill('half-written reply');
      await bob.request('POST', `/conversations/${conversationId}/messages`, { body: 'Another one', clientMessageId: 'bbbbbbbb-bbbb-4bbb-8bbb-bbbbbbbbbbbb' });
      await page.locator('.bubble', { hasText: 'Another one' }).waitFor({ timeout: 15_000 });
      assert.equal(await page.locator('#composer-input').inputValue(), 'half-written reply');
      await page.keyboard.press('Enter');
      await page.locator('.bubble.mine', { hasText: 'half-written reply' }).waitFor();

      // Elsewhere in the app, a new message shows a banner that opens the conversation.
      await page.locator('.tab', { hasText: 'Friends' }).click();
      await page.locator('.playing', { hasText: 'Playing Bob Adventure · PlayStation 5' }).waitFor({ timeout: 15_000 });
      if (shots) await page.screenshot({ path: join(shots, '9-friend-playing.png') });
      await bob.request('POST', `/conversations/${conversationId}/messages`, { body: 'Banner test', clientMessageId: 'cccccccc-cccc-4ccc-8ccc-cccccccccccc' });
      await page.locator('#banner', { hasText: 'New message from it_bob' }).waitFor({ timeout: 15_000 });
      if (shots) await page.screenshot({ path: join(shots, '10-banner.png') });
      await page.locator('#banner-open').click();
      await page.locator('.bubble', { hasText: 'Banner test' }).waitFor({ timeout: 15_000 });
      await page.locator('#banner').waitFor({ state: 'hidden' });
      assert.equal((await appState(player)).inbox.unseen, 0);

      // Games: what this console detects and the tracked history.
      await page.locator('.tab', { hasText: 'Games' }).click();
      await page.locator('#now-playing-title', { hasText: 'Racing Game' }).waitFor({ timeout: 15_000 });
      await page.locator('#recent-games .name', { hasText: 'Racing Game' }).first().waitFor();
      await page.getByText('AKENO sees that this game is running on the console').waitFor();
      if (shots) await page.screenshot({ path: join(shots, '11-games.png'), fullPage: true });
      await page.locator('.tab', { hasText: 'Diagnostics' }).click();
      await page.getByRole('heading', { name: 'Game detection', exact: true }).waitFor();
      await page.getByRole('heading', { name: 'Notifications with a button (experimental)' }).waitFor();
      if (shots) await page.screenshot({ path: join(shots, '12-diagnostics-activity.png'), fullPage: true });
      assert.deepEqual(errors, [], 'no script errors');
    } finally {
      await browser.close();
      bobOnline.child.kill('SIGTERM');
      await bobOnline.exited;
      await fetch(`http://127.0.0.1:${backendPort}/api/v1/activity/stop`, {
        method: 'POST',
        headers: { Authorization: `Bearer ${bobToken}`, 'Content-Type': 'application/json' },
        body: '{}',
      });
    }
  });

  // ---- PS5 profiles: what the server returns for this account, shown in the console page --------

  const dave = await registerUser(adminSession, 'it_dave');
  const erin = await registerUser(adminSession, 'it_erin');
  const proxied = async (path) => (await appApi(player, 'GET', `/api/v1${path}`)).body;

  await step('profiles: the console reads profiles through its proxy with the server\'s privacy rules', async () => {
    // bob: a full profile (banner, picture, theme, badges, status, bio, favorites), friends with carol.
    assert.equal((await bob.request('PATCH', '/profile', { bio: 'Bob from the integration test.\nLikes racing.', statusMessage: 'Hunting trophies',
      favoriteGames: ['Racing Game', 'Bob Adventure'] })).status, 200);
    assert.equal((await bob.upload('/profile/banner', pngImage(1200, 400))).status, 200);
    assert.equal((await bob.upload('/profile/avatar', pngImage(256, 256))).status, 200);
    for (const badgeId of ['beta_tester', 'ps5_alpha_tester']) {
      assert.equal((await adminSession.request('POST', `/admin/users/${bob.user.id}/badges`, { badgeId, note: 'client it' })).status, 201);
    }
    assert.equal((await bob.request('PATCH', '/profile/appearance', { theme: 'emerald', featuredBadges: ['ps5_alpha_tester'] })).status, 200);
    // dave: friends-only profile, friends with bob only. erin: blocks carol.
    assert.equal((await dave.request('PATCH', '/profile', { bio: 'Only for friends' })).status, 200);
    assert.equal((await dave.request('PATCH', '/profile/privacy', { profileVisibility: 'friends' })).status, 200);
    const daveRequest = await dave.request('POST', '/friends/requests', { userId: bob.user.id });
    assert.equal((await bob.request('POST', `/friends/requests/${daveRequest.body.request.id}/accept`)).status, 200);
    assert.equal((await erin.request('POST', '/blocks', { userId: carol.user.id })).status, 204);

    const own = await proxied(`/users/${carol.user.id}`);
    assert.equal(own.ok, true, JSON.stringify(own));
    assert.equal(own.data.relationship.state, 'self');
    const friend = await proxied(`/users/${bob.user.id}`);
    assert.equal(friend.data.relationship.state, 'friend');
    assert.equal(friend.data.relationship.canMessage, true);
    assert.equal(friend.data.statusMessage, 'Hunting trophies');
    assert.equal(friend.data.appearance.theme, 'emerald');
    assert.match(friend.data.appearance.bannerImage, /^[0-9a-f]{32}$/);
    assert.match(friend.data.user.avatarImage, /^[0-9a-f]{32}$/);
    assert.deepEqual(friend.data.featuredBadges, ['ps5_alpha_tester']);
    assert.deepEqual(friend.data.badges.map((b) => b.id), ['ps5_alpha_tester', 'beta_tester']);
    assert.deepEqual(friend.data.favoriteGames, ['Racing Game', 'Bob Adventure']);
    // The banner reaches the page as an image (large WebP or the JPEG fallback), never as a URL with the token.
    const large = await appApi(player, 'GET', `/api/media?id=${friend.data.appearance.bannerImage}&v=lg`);
    assert.equal(large.status, 200, large.text);
    assert.equal(large.body.type, 'image/webp');
    const jpeg = await appApi(player, 'GET', `/api/media?id=${friend.data.appearance.bannerImage}&v=fallback`);
    assert.equal(jpeg.body.type, 'image/jpeg');

    const restricted = await proxied(`/users/${dave.user.id}`);
    assert.equal(restricted.data.restricted, true);
    assert.equal(restricted.data.bio, null);
    assert.equal(restricted.data.appearance.bannerImage, null);
    assert.deepEqual(restricted.data.favoriteGames, []);
    const blockedMe = await proxied(`/users/${erin.user.id}`);
    assert.equal(blockedMe.ok, false);
    assert.equal(blockedMe.status, 404, 'a user who blocked this account looks like a user that does not exist');
    assert.equal((await proxied(`/users/${erin.user.id}/activity`)).status, 404);

    assert.equal((await bob.request('PATCH', '/profile/privacy', { gameHistoryVisibility: 'nobody' })).status, 200);
    const hidden = await proxied(`/users/${bob.user.id}/activity`);
    assert.equal(hidden.data.historyVisible, false);
    assert.deepEqual(hidden.data.recentGames, []);
    assert.equal((await bob.request('PATCH', '/profile/privacy', { gameHistoryVisibility: 'everyone' })).status, 200);
    // Not on the device allow-list of the server: profile editing stays on the website.
    assert.equal((await appApi(player, 'PATCH', '/api/v1/profile', { bio: 'from the console' })).body.ok, false);
  });

  await step('profiles (browser): friend and own profiles, controller navigation, privacy and error states at 1920×1080', async () => {
    let chromium;
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
    const bobCredential = join(work, 'bob-device.json');
    const bobToken = JSON.parse(readFileSync(bobCredential, 'utf8')).accessToken;
    const bobOnline = startCli(['--server', server, '--ca-file', caFile, '--credentials', bobCredential, 'listen']);
    await waitFor(() => bobOnline.stderr.includes('realtime.connected'), 'bob to be online', 20_000);
    const beat = () => fetch(`http://127.0.0.1:${backendPort}/api/v1/activity/heartbeat`, {
      method: 'POST', headers: { Authorization: `Bearer ${bobToken}`, 'Content-Type': 'application/json' },
      body: JSON.stringify({ titleId: 'CUSA00042', titleName: 'Bob Adventure', platform: 'PS5' }),
    });
    assert.equal((await beat()).status, 200);
    // carol leads a party, so "Invite to party" is offered on a friend's profile.
    const party = await carol.request('POST', '/parties', { privacy: 'friends', maxMembers: 8 });
    assert.equal(party.status, 201, JSON.stringify(party.body));

    const shots = process.env.AKENO_UI_SCREENSHOTS;
    const browser = await chromium.launch();
    try {
      const page = await browser.newPage({ viewport: { width: 1920, height: 1080 } });
      const errors = [];
      page.on('pageerror', (error) => errors.push(String(error)));
      page.on('console', (message) => message.type() === 'error' && !/^Failed to load resource: the server responded with a status of [45]\d\d/.test(message.text()) && errors.push(message.text()));
      const open = (target) => page.evaluate(`window.location.hash = ${JSON.stringify(`#t=${player.token}&open=${target}`)}`);
      const active = () => page.evaluate('document.activeElement && document.activeElement.id');
      await page.goto(`http://127.0.0.1:${player.port}/#t=${player.token}`);
      await page.getByRole('heading', { name: /Friends · \d+ of \d+ online/ }).waitFor();

      // Friends: the row says "View profile", Message sits next to it; the controller selects the row.
      const row = page.locator(`#friend-${bob.user.id}`);
      await row.getByText('View profile').waitFor();
      await page.locator(`#friend-msg-${bob.user.id}`).waitFor();
      await row.focus();
      await page.keyboard.press('Enter');
      await page.locator('#profile-name', { hasText: 'it_bob' }).waitFor({ timeout: 15_000 });
      await page.locator('#profile-message:focus').waitFor();
      await page.locator('#profile-banner img.banner-img').waitFor({ timeout: 15_000 });
      await page.locator('.profile-head .avatar.big img').waitFor();
      await page.locator('#profile-relationship', { hasText: 'Friends' }).waitFor();
      await page.locator('#profile-badges .badge-chip', { hasText: 'PS5 Alpha Tester' }).waitFor();
      assert.equal(await page.locator('#profile-badges .medal').count(), 2, 'featured chip medal + other badge medal');
      await page.locator('#profile-status', { hasText: 'Hunting trophies' }).waitFor();
      await page.locator('#profile-now', { hasText: 'Bob Adventure' }).waitFor();
      await page.locator('#profile-presence', { hasText: 'Playing Bob Adventure' }).waitFor();
      assert.equal(await page.locator('#profile-favorites .game-tile').count(), 2);
      await page.locator('#profile-bio', { hasText: 'Likes racing.' }).waitFor();
      await page.locator('#profile-recent .game-tile', { hasText: 'Bob Adventure' }).waitFor();
      const accent = await page.locator('#profile-page').evaluate((el) => el.style.getPropertyValue('--p-accent'));
      assert.match(accent, /^#[0-9a-f]{6}$/, 'the profile theme accent is applied');
      const box = await page.locator('#profile-banner').boundingBox();
      assert.ok(box.width > 1300 && box.height > 300, `full-width banner (${box.width}×${box.height})`);
      if (shots) await page.screenshot({ path: join(shots, '13-profile-friend.png'), fullPage: true });

      // D-pad: down from the actions reaches the content cards; left/right moves between game cards.
      for (let i = 0; i < 8 && !/^pg-/.test(await active() || ''); i++) await page.keyboard.press('ArrowDown');
      assert.match(await active(), /^pg-(recent|fav)-/, 'focus reached the game cards');
      const before = await active();
      await page.keyboard.press('ArrowRight');
      assert.notEqual(await active(), before, 'right moves to the next game card');

      // Invite to party (carol's party), then Circle goes back to the friend row it came from.
      await page.locator('#profile-invite').click();
      await page.getByText('Party invitation sent.').waitFor();
      const invitations = (await bob.request('GET', '/invitations')).body.invitations;
      assert.ok(invitations.some((inv) => inv.kind === 'party' && inv.inviter.onlineId === 'it_carol'), JSON.stringify(invitations));
      await page.keyboard.press('Escape');
      await page.getByRole('heading', { name: /Friends · \d+ of \d+ online/ }).waitFor();
      assert.equal(await active(), `friend-${bob.user.id}`, 'focus returns to the friend');

      // My profile (Profile tab): my own data and my console's games.
      await page.locator('.tab', { hasText: 'Profile' }).click();
      await page.locator('#profile-name', { hasText: 'it_carol' }).waitFor({ timeout: 15_000 });
      await page.locator('#profile-relationship', { hasText: 'You' }).waitFor();
      await page.locator('#profile-games').waitFor();
      await page.locator('#profile-recent .game-tile', { hasText: 'Racing Game' }).waitFor();
      await page.locator('.profile-head .avatar.big img').waitFor();
      await page.getByText('Edit your picture, banner, theme, badges and bio on the AKENO NETWORK website.').waitFor();
      if (shots) await page.screenshot({ path: join(shots, '14-profile-own.png'), fullPage: true });

      // History hidden from this account: the profile says so instead of showing anything.
      assert.equal((await bob.request('PATCH', '/profile/privacy', { gameHistoryVisibility: 'nobody' })).status, 200);
      await open(`profile:${bob.user.id}`);
      await page.locator('#profile-recent-section', { hasText: 'does not share their game history' }).waitFor({ timeout: 15_000 });
      assert.equal(await page.locator('#profile-recent .game-tile').count(), 0);
      assert.equal((await bob.request('PATCH', '/profile/privacy', { gameHistoryVisibility: 'everyone' })).status, 200);

      // Friends-only profile of someone who is not a friend: no details, a notice instead.
      await open(`profile:${dave.user.id}`);
      await page.locator('#profile-restricted', { hasText: 'only shares profile details with friends' }).waitFor({ timeout: 15_000 });
      assert.equal(await page.locator('#profile-bio').count(), 0);
      assert.equal(await page.locator('#profile-banner img').count(), 0, 'no banner for restricted profiles');
      assert.equal((await page.locator('.profile-head .avatar.big').textContent()).trim(), 'ID', 'no picture: initials');
      await page.locator('#profile-add').waitFor();
      if (shots) await page.screenshot({ path: join(shots, '15-profile-restricted.png'), fullPage: true });

      // Someone who blocked this account: not distinguishable from a missing profile.
      await open(`profile:${erin.user.id}`);
      await page.locator('#profile-error', { hasText: 'Profile not available' }).waitFor({ timeout: 15_000 });
      await page.locator('#profile-back:focus').waitFor();
      assert.ok(!(await page.content()).includes('it_erin'), 'nothing about the blocking user is shown');
      if (shots) await page.screenshot({ path: join(shots, '16-profile-unavailable.png') });

      // Server unreachable: an error with Retry, then the profile once it is back.
      await stopBackend();
      await open(`profile:${bob.user.id}`);
      await page.locator('#profile-error', { hasText: 'Profile could not be loaded' }).waitFor({ timeout: 30_000 });
      await page.locator('#profile-retry').click(); // asks again (still down): the same error, nothing stale shown
      await page.locator('#profile-error', { hasText: 'Profile could not be loaded' }).waitFor({ timeout: 30_000 });
      await startBackend();
      // Retry, or a live update for this profile reloads it by itself once the server is back.
      await waitUntil(async () => {
        if (await page.locator('#profile-name', { hasText: 'it_bob' }).count()) return true;
        const retry = page.locator('#profile-retry');
        if (await retry.count()) await retry.click({ timeout: 2000 }).catch(() => {});
        return false;
      }, 'the profile after the outage', 30_000);
      await appApi(player, 'POST', '/api/realtime/reconnect');
      await waitUntil(async () => (await appState(player)).realtime.state === 'connected', 'live updates after the outage', 30_000);

      // A missing image (deleted on the server) keeps the initials; the profile still loads (fresh tab: empty image cache).
      const avatarId = (await proxied(`/users/${bob.user.id}`)).data.user.avatarImage;
      const objects = readdirSync(join(work, 'media')).map((dir) => join(work, 'media', dir, avatarId.slice(0, 2), avatarId)).filter((dir) => existsSync(dir));
      assert.equal(objects.length, 1, 'found the stored picture');
      rmSync(objects[0], { recursive: true, force: true });
      const fresh = await browser.newPage({ viewport: { width: 1920, height: 1080 } });
      const freshErrors = [];
      fresh.on('pageerror', (error) => freshErrors.push(String(error)));
      await fresh.goto(`http://127.0.0.1:${player.port}/#t=${player.token}&open=profile:${bob.user.id}`);
      await fresh.locator('#profile-name', { hasText: 'it_bob' }).waitFor({ timeout: 15_000 });
      await fresh.locator('#profile-banner img.banner-img').waitFor({ timeout: 15_000 });
      await fresh.waitForTimeout(800);
      assert.equal(await fresh.locator('.profile-head .avatar.big img').count(), 0, 'no broken picture');
      assert.match(await fresh.locator('.profile-head .avatar.big').textContent(), /^IB/, 'the initials stay');
      assert.deepEqual(freshErrors, []);
      await fresh.close();

      // Message from the profile opens the conversation.
      await page.locator('#profile-message').click();
      await page.locator('#composer-input').waitFor({ timeout: 15_000 });
      await page.locator('#conv-profile').waitFor();
      assert.deepEqual(errors, [], 'no script errors');
    } finally {
      await browser.close();
      bobOnline.child.kill('SIGTERM');
      await bobOnline.exited;
      await fetch(`http://127.0.0.1:${backendPort}/api/v1/activity/stop`, {
        method: 'POST', headers: { Authorization: `Bearer ${bobToken}`, 'Content-Type': 'application/json' }, body: '{}',
      });
    }
  });

  // ---- Quick access: the experimental controller shortcut (simulated controller) ---------------

  const padFile = join(work, 'pad.txt');
  const qaData = join(work, 'app-quick');
  let qa = null;
  const qaState = async () => (await appState(qa)).quickAccess;
  const qaOpens = () => qa.events.filter((e) => e.event === 'open-browser');
  const press = (bits) => writeFileSync(padFile, `0x${bits.toString(16)}\n`);
  const L1 = 0x0400, R1 = 0x0800, OPTIONS = 0x0008, L3 = 0x0002, R3 = 0x0004;

  await step('quick access: the controller shortcut is off by default and validated', async () => {
    writeFileSync(padFile, '0x0\n');
    qa = await startApp(qaData, ['--port', '0', '--server', server, '--ca-file', caFile], { AKENO_SIM_PAD: padFile, AKENO_BACKOFF_BASE_MS: '300' });
    await pairApp(qa, carol);
    const q = await qaState();
    assert.equal(q.enabled, false);
    assert.equal(q.verifiedOnHardware, false);
    assert.equal(q.state, 'off');
    assert.equal(q.library, false, 'nothing is loaded while off');
    assert.equal(q.reads, 0);
    assert.equal(q.combo, 'l1r1options');
    assert.equal(q.holdMs, 1000);
    assert.deepEqual(q.combos.map((c) => c.id), ['l1r1options', 'l3r3', 'touchoptions']);
    for (const bad of [{ quickAccessCombo: 'ps' }, { quickAccessCombo: 'l1' }, { quickAccessHoldMs: 300 }, { quickAccessHoldMs: 'long' }]) {
      const res = await appApi(qa, 'POST', '/api/settings', bad);
      assert.equal(res.status, 400, JSON.stringify(bad));
    }
    assert.equal((await qaState()).enabled, false);
    assert.equal(qaOpens().length, 1, 'only the start-up page so far');
  });

  await step('quick access: a deliberate hold opens the running app\'s page once; normal play does not', async () => {
    const opens = qaOpens().length;
    assert.equal((await appApi(qa, 'POST', '/api/settings', { quickAccessShortcut: true })).body.ok, true);
    await waitUntil(async () => (await qaState()).state === 'reading', 'the reader to start');
    const settings = JSON.parse(readFileSync(join(qaData, 'settings.json'), 'utf8'));
    assert.deepEqual([settings.quickAccessShortcut, settings.quickAccessCombo, settings.quickAccessHoldMs], [true, 'l1r1options', 1000]);
    // Gameplay-like input: two of the three buttons for a long time, short taps of all three.
    press(L1 | R1);
    await sleep(1600);
    for (let i = 0; i < 4; i++) {
      press(L1 | R1 | OPTIONS);
      await sleep(400);
      press(0);
      await sleep(200);
    }
    assert.equal(qaOpens().length, opens, 'no page opened during normal play');
    assert.equal((await qaState()).detections, 0);
    // A deliberate hold: exactly one open, however long the buttons stay down.
    press(L1 | R1 | OPTIONS);
    await waitUntil(() => qaOpens().length === opens + 1, 'the shortcut to open the page', 5000);
    await sleep(2500);
    assert.equal(qaOpens().length, opens + 1, 'held buttons do not reopen the page');
    assert.match(qaOpens().at(-1).url, new RegExp(`^http://127\\.0\\.0\\.1:${qa.port}/#t=${qa.token}`), 'the running app reopens its own page');
    let q = await qaState();
    assert.equal(q.detections, 1);
    assert.equal(q.activations, 1);
    assert.equal(q.lastBrowserResult, 0);
    assert.ok(q.samplesWithButtons > 0 && q.reads > 50);
    assert.equal(q.library, true);
    assert.equal(q.userSource, 'foreground user');
    // Released and pressed again within the cooldown: swallowed. After it: opens again.
    press(0);
    await sleep(200);
    press(L1 | R1 | OPTIONS);
    await sleep(1500);
    assert.equal(qaOpens().length, opens + 1, 'cooldown');
    press(0);
    await sleep(3200);
    press(L1 | R1 | OPTIONS);
    await waitUntil(() => qaOpens().length === opens + 2, 'the second activation', 5000);
    press(0);
    q = await qaState();
    assert.equal(q.activations, 2);
    // The combination and hold time can be changed; the old combination stops working.
    assert.equal((await appApi(qa, 'POST', '/api/settings', { quickAccessCombo: 'l3r3', quickAccessHoldMs: 1500 })).body.ok, true);
    await sleep(5200);
    press(L1 | R1 | OPTIONS);
    await sleep(2000);
    assert.equal(qaOpens().length, opens + 2, 'the previous combination no longer opens');
    press(0);
    await sleep(200);
    press(L3 | R3);
    await sleep(1200);
    assert.equal(qaOpens().length, opens + 2, 'not before 1.5 s');
    await waitUntil(() => qaOpens().length === opens + 3, 'the new combination', 3000);
    press(0);
  });

  await step('quick access: reuses the running instance; with an unseen message it opens that conversation', async () => {
    const conversation = await bob.request('POST', '/conversations', { userId: carol.user.id });
    await waitFor(() => qa.stderr.includes('realtime.state connected'), 'the realtime connection', 20_000);
    await bob.request('POST', `/conversations/${conversation.body.id}/messages`, { body: 'Shortcut test', clientMessageId: 'dddddddd-dddd-4ddd-8ddd-dddddddddddd' });
    await waitFor(() => notifications(qa).some((text) => text.includes('Shortcut test')), 'the message notification');
    const opens = qaOpens().length;
    await sleep(5200);
    press(L3 | R3);
    await waitUntil(() => qaOpens().length === opens + 1, 'the shortcut', 5000);
    press(0);
    assert.ok(qaOpens().at(-1).url.endsWith(`&open=conversation:${conversation.body.id}`), qaOpens().at(-1).url);
    // A second launch still reopens the same instance; the reader keeps running in that one only.
    const readsBefore = (await qaState()).reads;
    const second = await startApp(qaData, ['--port', String(qa.port), '--ca-file', caFile], { AKENO_SIM_PAD: padFile });
    assert.equal((await second.exited).code, 0, second.stderr);
    assert.ok(!second.events.some((e) => e.event === 'open-browser'), 'the second process opens nothing itself');
    await waitUntil(() => qaOpens().length === opens + 2, 'the running app to reopen its page');
    assert.ok((await qaState()).reads > readsBefore, 'the reader keeps running');
    assert.equal((await appState(qa)).paired, true, 'pairing kept');
  });

  await step('quick access: the test mode reports detections without opening anything; turning it off stops reading', async () => {
    const opens = qaOpens().length;
    const test = await appApi(qa, 'POST', '/api/quick-access/test', { seconds: 10 });
    assert.equal(test.body.quickAccess.test.active, true);
    press(L3 | R3);
    await waitUntil(async () => (await qaState()).test.detections === 1, 'the test detection', 5000);
    press(0);
    assert.equal(qaOpens().length, opens, 'a test never opens the page');
    const events = (await appApi(qa, 'GET', '/api/events?after=0')).body.events.filter((e) => e.type === 'quickaccess.detected');
    assert.equal(events.at(-1).data.test, true);

    assert.equal((await appApi(qa, 'POST', '/api/settings', { quickAccessShortcut: false })).body.ok, true);
    await waitUntil(async () => (await qaState()).state === 'off' || (await qaState()).test.active, 'the reader to stop (or keep running for the test)');
    await waitUntil(async () => !(await qaState()).test.active, 'the test to end', 25_000);
    await waitUntil(async () => (await qaState()).state === 'off', 'the reader to stop');
    const reads = (await qaState()).reads;
    await sleep(600);
    assert.equal((await qaState()).reads, reads, 'no more reads while off');
    press(L3 | R3);
    await sleep(2000);
    press(0);
    assert.equal(qaOpens().length, opens, 'off means off');
  });

  await step('quick access: without a readable controller the state says so and AKENO keeps working', async () => {
    assert.equal((await appApi(qa, 'POST', '/api/settings', { quickAccessShortcut: true })).body.ok, true);
    qa.child.kill('SIGTERM');
    assert.equal((await qa.exited).code, 0);
    // Opening fails (e.g. the system refuses): reported once, not retried in a loop.
    qa = await startApp(qaData, ['--port', '0', '--ca-file', caFile], { AKENO_SIM_PAD: padFile, AKENO_SIM_PAD_OPEN: 'fail' });
    await waitUntil(async () => (await qaState()).state === 'failed', 'the failure to be reported');
    assert.match((await qaState()).detail, /scePadOpen returned 0x80920001/);
    assert.equal((await qaState()).openResult, '0x80920001');
    await sleep(1000);
    assert.equal((qa.stderr.match(/quickaccess.unavailable/g) || []).length, 1, 'no retry loop');
    assert.equal((await appState(qa)).paired, true);
    assert.equal((await appApi(qa, 'GET', `/api/v1/users/${bob.user.id}`)).body.ok, true, 'the rest of AKENO works');
    qa.child.kill('SIGTERM');
    await qa.exited;
    // No controller library at all (a console where libScePad cannot be loaded).
    qa = await startApp(qaData, ['--port', '0', '--ca-file', caFile], {});
    await waitUntil(async () => (await qaState()).state === 'unavailable', 'the unavailable state');
    assert.equal((await qaState()).library, false);
    assert.equal((await appState(qa)).paired, true);
  });

  await step('quick access (browser): Settings shows every way in with its status, the shortcut controls and the test', async () => {
    let chromium;
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
    qa.child.kill('SIGTERM');
    await qa.exited;
    writeFileSync(padFile, '0x0\n');
    qa = await startApp(qaData, ['--port', '0', '--ca-file', caFile], { AKENO_SIM_PAD: padFile });
    const browser = await chromium.launch();
    try {
      const page = await browser.newPage({ viewport: { width: 1920, height: 1080 } });
      const errors = [];
      page.on('pageerror', (error) => errors.push(String(error)));
      await page.goto(`http://127.0.0.1:${qa.port}/#t=${qa.token}`);
      await page.locator('.tab', { hasText: 'Settings' }).click();
      const section = page.locator('#quick-access');
      await section.getByRole('heading', { name: 'Quick Access' }).waitFor();
      assert.equal(await section.locator('.method').count(), 4);
      await section.locator('.method', { hasText: 'Notification button' }).getByText('Works').waitFor();
      await section.locator('.method', { hasText: 'Controller shortcut' }).getByText('Untested').waitFor();
      assert.equal(await page.locator('#qa-enabled').isChecked(), true, 'the saved setting');
      await page.locator('#qa-test').click();
      await page.locator('#qa-test-result', { hasText: 'Test running' }).waitFor({ timeout: 10_000 });
      await sleep(300);
      press(L3 | R3);
      await page.getByText('Controller shortcut detected. (Test: nothing was opened.)').waitFor({ timeout: 10_000 });
      press(0);
      if (process.env.AKENO_UI_SCREENSHOTS) await page.screenshot({ path: join(process.env.AKENO_UI_SCREENSHOTS, '17-quick-access.png'), fullPage: true });
      await page.locator('#qa-enabled').click();
      await waitUntil(async () => (await qaState()).enabled === false, 'the switch to turn the shortcut off');
      await page.locator('.tab', { hasText: 'Diagnostics' }).click();
      await page.getByRole('heading', { name: 'Quick access: controller shortcut (experimental)' }).waitFor();
      await page.locator('#diag-quick-access').getByText('no — needs a test on your PS5 while a game runs').waitFor();
      if (process.env.AKENO_UI_SCREENSHOTS) await page.screenshot({ path: join(process.env.AKENO_UI_SCREENSHOTS, '18-diagnostics-quick-access.png'), fullPage: true });
      assert.deepEqual(errors, []);
    } finally {
      await browser.close();
      qa.child.kill('SIGTERM');
      await qa.exited;
      allStderr.push(qa.stderr);
    }
  });

  await step('console app: button notifications carry one-time links, never the page secret', async () => {
    assert.equal((await appApi(player, 'POST', '/api/settings', { actionableNotifications: true })).body.ok, true);
    await bob.request('POST', `/conversations/${conversationId}/messages`, { body: 'Open me', clientMessageId: '88888888-8888-4888-8888-888888888888' });
    const toast = await waitUntil(async () => player.events.find((e) => e.event === 'notification' && e.sub === 'Open me'), 'the button notification');
    assert.equal(toast.text, 'AKENO: it_bob');
    assert.equal(toast.action.label, 'Open in AKENO');
    const link = /^http:\/\/127\.0\.0\.1:(\d+)\/t\/([0-9a-f]{32})$/.exec(toast.action.url);
    assert.ok(link, toast.action.url);
    assert.ok(!JSON.stringify(toast).includes(player.token), 'the page secret is not in the notification');

    const first = await fetch(toast.action.url, { redirect: 'manual' });
    assert.equal(first.status, 302);
    assert.equal(first.headers.get('location'), `/#t=${player.token}&open=conversation:${conversationId}`);
    assert.equal(first.headers.get('cache-control'), 'no-store');
    const again = await fetch(toast.action.url, { redirect: 'manual' });
    assert.equal(again.status, 403, 'single use');
    for (const bad of ['/t/xyz', `/t/${'0'.repeat(32)}`, `/t/${'a'.repeat(64)}`, '/t/']) {
      assert.equal((await fetch(`http://127.0.0.1:${player.port}${bad}`, { redirect: 'manual' })).status, 403, bad);
    }
    const rebinding = await rawRequest(player.port, `GET /t/${link[2]} HTTP/1.1\r\nHost: attacker.example\r\n\r\n`);
    assert.match(rebinding, /^HTTP\/1\.1 403 /);

    // Links expire (4 s in this test, 10 min by default).
    await bob.request('POST', `/conversations/${conversationId}/messages`, { body: 'Expired link', clientMessageId: '99999999-9999-4999-8999-999999999999' });
    const late = await waitUntil(async () => player.events.find((e) => e.event === 'notification' && e.sub === 'Expired link'), 'the second button notification');
    await sleep(4500);
    assert.equal((await fetch(late.action.url, { redirect: 'manual' })).status, 403);

    const test = await appApi(player, 'POST', '/api/diagnostics/actionable-test');
    assert.equal(test.body.ok, true, test.text);
    const diagnostics = (await appApi(player, 'GET', '/api/diagnostics')).body.notifications.actionable;
    assert.equal(diagnostics.enabled, true);
    assert.ok(diagnostics.sent >= 3);
    assert.equal(diagnostics.unavailable, 0);
  });

  await step('console app: without a working button notification the proven plain one is shown', async () => {
    player.child.kill('SIGTERM');
    assert.equal((await player.exited).code, 0);
    player = await startApp(playerData, ['--port', '0', '--ca-file', caFile], { ...playerEnv, AKENO_TEST_ACTIONABLE: 'unavailable' });
    await waitFor(() => player.stderr.includes('realtime.state connected'), 'the realtime connection', 20_000);
    await bob.request('POST', `/conversations/${conversationId}/messages`, { body: 'Fallback please', clientMessageId: 'aaaaaaaa-aaaa-4aaa-8aaa-aaaaaaaaaaaa' });
    await waitFor(() => notifications(player).includes('AKENO: it_bob: Fallback please'), 'the plain fallback notification');
    assert.ok(!player.events.some((e) => e.action), 'no button notification');
    const diagnostics = (await appApi(player, 'GET', '/api/diagnostics')).body.notifications.actionable;
    assert.equal(diagnostics.unavailable, 1);
    assert.match(diagnostics.status, /not available/);
  });

  await step('console app: exiting reports the end of the running game session', async () => {
    writeFileSync(procs, '100 NPXS40028\n200 PPSA01234\n');
    await waitUntil(async () => (await carolActivity()).current?.titleId === 'PPSA01234', 'the game to be reported');
    assert.equal((await appApi(player, 'POST', '/api/exit')).body.ok, true);
    assert.equal((await player.exited).code, 0, player.stderr);
    assert.equal((await carolActivity()).current, null);
    const rows = await sessionRows();
    assert.ok(rows.every((r) => r.ended_at !== null), JSON.stringify(rows));
    assert.equal(rows.at(-1).end_reason, 'stopped');
    allStderr.push(player.stderr);
  });

  await step('console app: never writes tokens or device codes to its logs', async () => {
    const logs = readFileSync(join(appData, 'akeno.log'), 'utf8') + allStderr.join('\n');
    assert.doesNotMatch(logs, /akd_[A-Za-z0-9_-]{10,}/);
    assert.doesNotMatch(logs, /akp_[A-Za-z0-9_-]{10,}/);
    assert.match(logs, /pairing.started/);
  });
}

// ---- Audio lab: experimental PS5 voice research (simulated audio on Linux) --------------------

if (audioLabBinary) {
  if (!existsSync(audioLabBinary)) throw new Error(`Missing audio lab binary ${audioLabBinary}`);
  log(`AKENO console app audio lab (host build) tests (${audioLabBinary})`);
  // Stands in for /system/common/lib on the console.
  const libDir = join(work, 'system-lib');
  spawnSync('mkdir', ['-p', libDir]);
  for (const name of ['libkernel.sprx', 'libSceAudioOut.sprx', 'libSceAudioIn.sprx', 'libSceVoiceQoS.sprx', 'libSceNet.sprx']) writeFileSync(join(libDir, name), '');
  const audioEnv = { AKENO_SIM_AUDIO: 'out', AKENO_SIM_AUDIO_LIBDIR: libDir };
  let lab = null;
  const waitUntil = async (predicate, what, timeoutMs = 20_000) => {
    const deadline = Date.now() + timeoutMs;
    while (!(await predicate())) {
      if (Date.now() > deadline) throw new Error(`Timed out waiting for ${what}`);
      await sleep(100);
    }
  };

  const audioState = async () => (await appApi(lab, 'GET', '/api/diagnostics/audio')).body.audio;
  const audioRuns = () => lab.events.filter((e) => e.event === 'audio-run');
  const finished = async (id) => {
    await waitUntil(async () => {
      const a = await audioState();
      return !a.running && a.runs.some((r) => r.id === id);
    }, `test ${id} to finish`);
    return (await audioState()).runs.find((r) => r.id === id);
  };
  /** Restarts the audio lab with extra environment (failure injection). */
  const restartLab = async (env) => {
    if (lab) {
      lab.child.kill('SIGTERM');
      await lab.exited;
    }
    lab = await startApp(join(work, 'app-audiolab'), ['--port', '0'], { ...audioEnv, ...env }, audioLabBinary);
    assert.equal(lab.exit, null, lab.stderr);
  };

  await step('audio lab: the release build has no audio routes, no audio section and no microphone code', async () => {
    // Source check: no AKENO code declares or calls a microphone (libSceAudioIn) function; the audio
    // lab only looks the names up, and the platform layers contain none of them.
    for (const file of ['platform_ps5.c', 'platform_host.c', 'platform.h', 'audio_lab_pcm.c']) {
      assert.doesNotMatch(readFileSync(resolve(root, 'clients/ps5/app', file), 'utf8'), /sceAudioIn/, file);
    }
    const lab_c = readFileSync(resolve(root, 'clients/ps5/app/audio_lab.c'), 'utf8');
    assert.doesNotMatch(lab_c, /typedef[^;]*sceAudioIn|\(\s*\*\s*\w*audio_in/i, 'no AudioIn function pointer type');
    if (!appBinary) {
      log('       (skipped: --app not given)');
      return;
    }
    const release = await startApp(join(work, 'app-release-audio'), ['--port', '0'], audioEnv);
    try {
      for (const [method, path] of [['GET', '/api/diagnostics/audio'], ['POST', '/api/diagnostics/audio/tone'], ['POST', '/api/diagnostics/audio/probe'],
        ['POST', '/api/diagnostics/audio/feedback'], ['POST', '/api/diagnostics/audio/browser'], ['POST', '/api/diagnostics/audio/cancel'],
        ['POST', '/api/diagnostics/audio/media'], ['GET', '/api/diagnostics/audio/report']]) {
        assert.equal((await appApi(release, method, path, method === 'POST' ? { confirm: 'play' } : undefined)).status, 404, path);
      }
      assert.equal((await appApi(release, 'GET', '/api/diagnostics')).body.experimentalAudio, undefined);
      const releaseApp = (await appState(release)).app;
      assert.equal(releaseApp.audioLab, undefined);
      assert.equal(releaseApp.audioLabVersion, undefined);
      assert.equal(releaseApp.release, 'Alpha 0.5');
      const page = await fetch(`http://127.0.0.1:${release.port}/`);
      assert.doesNotMatch(page.headers.get('content-security-policy'), /media-src/, 'the release page may load no media');
      assert.match(page.headers.get('content-security-policy'), /default-src 'none'/);
      assert.ok(!release.events.some((e) => e.event === 'audio-run'));
    } finally {
      release.child.kill('SIGTERM');
      await release.exited;
    }
  });

  await step('audio lab v3: nothing runs until the user asks; the probe looks names up read-only', async () => {
    await restartLab({});
    assert.equal((await appApi(lab, 'GET', '/api/diagnostics')).body.experimentalAudio, true);
    const labApp = (await appState(lab)).app;
    assert.equal(labApp.audioLab, undefined, 'the shared app.js would label it v2');
    assert.equal(labApp.audioLabVersion, 3);
    assert.equal(labApp.release, 'Alpha 0.5 \u00b7 Audio lab v3');
    const csp = (await fetch(`http://127.0.0.1:${lab.port}/`)).headers.get('content-security-policy');
    assert.match(csp, /media-src blob: data:; /, 'only the lab page may play its generated WAV');
    assert.match(csp, /default-src 'none'; script-src 'self'/);
    const idle = await audioState();
    assert.equal(idle.version, 3);
    assert.equal(idle.state, 'not-run');
    assert.equal(idle.probe, null);
    assert.deepEqual(idle.runs, []);
    assert.deepEqual(idle.presets.map((p) => p.id), ['stereo16-1024', 'mono16-256', 'stereofloat-1024', 'stereo16-2048']);
    assert.deepEqual(idle.levels.map((l) => l.id), ['quiet', 'moderate', 'clear']);
    assert.match(idle.portState, /^Read-only: sceAudioOutGetPortState on the test's own port/);
    assert.match(idle.portState, /UNVERIFIED on the PS5/);
    assert.equal(idle.media, null);
    assert.match(idle.routing, /No verified interface/);
    assert.equal(audioRuns().length, 0, 'no tone without a request');
    assert.equal((await appApi(lab, 'POST', '/api/diagnostics/audio/probe', undefined, { 'X-Akeno-Token': 'wrong' })).status, 403);

    const probe = (await appApi(lab, 'POST', '/api/diagnostics/audio/probe')).body.audio.probe;
    assert.equal(probe.libraryDir, libDir);
    assert.deepEqual(probe.audioFiles, ['libSceAudioOut.sprx', 'libSceAudioIn.sprx', 'libSceVoiceQoS.sprx']);
    const [used, portState, names, input] = probe.libraries;
    assert.equal(used.name, 'libSceAudioOut.sprx');
    assert.deepEqual(used.symbols.map((sym) => sym.name), ['sceAudioOutInit', 'sceAudioOutOpen', 'sceAudioOutOutput', 'sceAudioOutClose']);
    assert.ok(used.loaded && used.symbols.every((sym) => sym.exported));
    assert.deepEqual(portState.symbols.map((sym) => sym.name), ['sceAudioOutGetPortState']);
    assert.match(portState.use, /read-only, on the output test's own port/);
    assert.equal(names.use, 'looked up only, never called');
    assert.ok(names.symbols.some((sym) => sym.name === 'sceAudioOutGetFocusEnablePid'));
    assert.ok(!names.symbols.some((sym) => sym.name === 'sceAudioOutGetPortState'));
    assert.equal(input.name, 'libSceAudioIn.sprx');
    assert.match(input.use, /never called/);
    assert.equal(input.loaded, false);
    assert.match(probe.microphoneEnumeration, /not attempted/);
    const report = readFileSync(join(work, 'app-audiolab', 'audiolab-report.txt'), 'utf8');
    assert.match(report, /LIBRARIES \(read-only probe\)/);
    assert.match(report, /STATE: No output test has run yet\./);
    assert.equal(statSync(join(work, 'app-audiolab', 'audiolab-report.txt')).mode & 0o777, 0o600);
  });

  await step('audio lab v3: the tone needs a confirmation, uses the SDL driver configuration and reports every stage', async () => {
    const unconfirmed = await appApi(lab, 'POST', '/api/diagnostics/audio/tone', {});
    assert.equal(unconfirmed.status, 400);
    assert.equal(unconfirmed.body.error.code, 'CONFIRMATION_REQUIRED');
    assert.equal((await appApi(lab, 'POST', '/api/diagnostics/audio/tone', { confirm: 'play', preset: 'surround' })).status, 400);
    assert.equal((await appApi(lab, 'POST', '/api/diagnostics/audio/tone', { confirm: 'play', delaySeconds: 3 })).status, 400);
    assert.equal((await appApi(lab, 'POST', '/api/diagnostics/audio/tone', { confirm: 'play', level: 'loud' })).status, 400);

    const started = await appApi(lab, 'POST', '/api/diagnostics/audio/tone', { confirm: 'play' });
    assert.equal(started.status, 200, started.text);
    assert.equal(started.body.audio.state, 'playing');
    const busy = await appApi(lab, 'POST', '/api/diagnostics/audio/tone', { confirm: 'play' });
    assert.equal(busy.status, 409);
    assert.equal(busy.body.error.code, 'BUSY');
    const run = await finished(1);
    assert.equal(run.outcome, 'accepted');
    assert.deepEqual({ userId: run.userId, portType: run.portType, rate: run.rate, channels: run.channels, format: run.format, formatParam: run.formatParam, grain: run.grain },
      { userId: '0xff', portType: 0, rate: 48000, channels: 2, format: 'S16 stereo', formatParam: 1, grain: 1024 });
    assert.equal(run.initResult, '0x00000000');
    assert.equal(run.initCalled, true);
    assert.equal(run.openResult, '0x00000011');
    assert.equal(run.handle, 17);
    assert.equal(run.framesSubmitted, 96256, '2 s rounded up to whole 1024-frame calls');
    assert.equal(run.calls, 94);
    assert.equal(run.callsOk, 94);
    assert.equal(run.pcmNonzero, true);
    assert.ok(run.nonzeroFrames > 70000 && run.nonzeroFrames <= 72000, String(run.nonzeroFrames));
    assert.ok(run.peak > 0.098 && run.peak <= 0.1001, String(run.peak));
    assert.equal(run.paced, true, 'the simulated output blocks like the real one');
    assert.equal(run.bufferAligned, true);
    assert.equal(run.closeResult, '0x00000000');
    assert.equal(run.heard, null);
    // v3: AKENO's own port measured before, during and after the tone (simulated PS4 layout).
    assert.equal(run.portState.exported, true);
    assert.equal(run.portState.stopped, false);
    assert.deepEqual(run.portState.points.map((p) => p.point), ['before-playback', 'during-playback', 'before-close']);
    for (const point of run.portState.points) {
      assert.equal(point.attempted, true, point.point);
      assert.deepEqual(point.calls.map((c) => [c.fill, c.result, c.leadIntact, c.tailIntact]),
        [['0xa5', '0x00000000', true, true], ['0x5a', '0x00000000', true, true]]);
      assert.equal(point.writtenBytes, 32);
      assert.equal(point.highestWrittenOffset, 31);
      assert.equal(point.windowBytes, 256);
      assert.equal(point.shownBytes, 32);
      assert.equal(point.guardsIntact, true);
      assert.equal(point.callsAgree, true);
      assert.match(point.calls[0].bytes, /^01 00 02 00 7f 00 00 00( 00){24}$/);
      assert.equal(point.values, point.calls[0].bytes);
      assert.deepEqual(point.decoded, { verified: false, layout: 'PS4 SDK layout (shadPS4, fpPS4, SharpProspero); unverified on the PS5',
        output: '0x0001', outputNames: 'primary', channel: 2, volume: 127, rerouteCounter: 0, flag: '0x0' });
    }
    assert.equal(run.portState.points[0].atMs, 0);
    assert.ok(run.portState.points[1].atMs > 700 && run.portState.points[1].atMs < 1500, String(run.portState.points[1].atMs));
    assert.ok(run.portState.points[2].atMs >= run.portState.points[1].atMs);
    const state = await audioState();
    assert.equal(state.state, 'accepted', 'never "working" without the user saying so');
    assert.deepEqual(audioRuns().map((e) => [e.format, e.grain, e.frames, e.cancelled]), [[1, 1024, 96256, false]]);

    // One test at a time, a short pause between tests, and at most 30 per launch.
    const tooSoon = await appApi(lab, 'POST', '/api/diagnostics/audio/tone', { confirm: 'play' });
    assert.equal(tooSoon.status, 409);
    assert.equal(tooSoon.body.error.code, 'TOO_SOON');

    // The user's report decides "audible".
    assert.equal((await appApi(lab, 'POST', '/api/diagnostics/audio/feedback', { run: 9, heard: true, route: 'tv' })).status, 404);
    assert.equal((await appApi(lab, 'POST', '/api/diagnostics/audio/feedback', { run: 1, heard: 'yes' })).status, 400);
    assert.equal((await appApi(lab, 'POST', '/api/diagnostics/audio/feedback', { run: 1, heard: true, route: 'kitchen' })).status, 400);
    const silent = await appApi(lab, 'POST', '/api/diagnostics/audio/feedback', { run: 1, heard: false, route: '' });
    assert.equal(silent.body.audio.state, 'accepted-not-heard');

    await sleep(3100);
    assert.equal((await appApi(lab, 'POST', '/api/diagnostics/audio/tone', { confirm: 'play', preset: 'mono16-256', level: 'quiet' })).status, 200);
    const second = await finished(2);
    assert.equal(second.format, 'S16 mono');
    assert.equal(second.grain, 256);
    assert.equal(second.calls, 375);
    assert.equal(second.initCalled, false, 'sceAudioOutInit is called once per launch');
    assert.ok(second.peak > 0.048 && second.peak <= 0.0501, String(second.peak));
    assert.equal(second.portState.points[1].decoded.channel, 1, 'the simulated state follows the port format');
    const heard = await appApi(lab, 'POST', '/api/diagnostics/audio/feedback', { run: 2, heard: true, route: 'tv' });
    assert.equal(heard.body.audio.state, 'audible-confirmed');
    assert.equal(heard.body.audio.stateRun, 2);

    const text = await appApi(lab, 'GET', '/api/diagnostics/audio/report');
    assert.equal(text.status, 200);
    assert.match(text.headers.get('content-type'), /^text\/plain/);
    assert.match(text.text, /STATE: Audible output confirmed by the user\./);
    assert.match(text.text, /Run #2 .*\n {2}Request: user 0xFF \(system\), port type 0 \(main\), 48000 Hz, S16 mono/);
    assert.match(text.text, /User report: HEARD on TV \/ HDMI/);
    assert.match(text.text, /Run #1[\s\S]*consumed at playback speed[\s\S]*User report: not heard/);
    assert.match(text.text, /AKENO NETWORK audio lab v3 report/);
    assert.match(text.text, /Port state \(sceAudioOutGetPortState on this test's own port, read-only\)\n/);
    assert.match(text.text, /Measured: return codes, raw bytes, written bytes, canaries\. Decoded fields: PS4 layout, UNVERIFIED on the PS5\./);
    assert.match(text.text, /before playback \(0 ms after the first block\): call 1 \(fill a5\) 0x00000000; call 2 \(fill 5a\) 0x00000000; 32 of 256 bytes written \(highest offset 31\); canaries intact; calls agree/);
    assert.match(text.text, /during the tone \(\d+ ms after the first block\)/);
    assert.match(text.text, /call 1 raw: 01 00 02 00 7f 00 00 00/);
    assert.match(text.text, /decoded \(UNVERIFIED PS4 layout\): output 0x0001 \(primary\), channel 2, volume 127, rerouteCounter 0, flag 0x0/);
    assert.match(text.text, /BROWSER <audio> ELEMENT[\s\S]*Not run yet\./);
    assert.ok(!text.text.includes(lab.token), 'no page secret in the report');
    assert.doesNotMatch(text.text, /akd_|akp_|Bearer/);
    assert.equal(readFileSync(join(work, 'app-audiolab', 'audiolab-report.txt'), 'utf8').includes('HEARD on TV / HDMI'), true);
  });

  await step('audio lab v3: a delayed test waits, can be cancelled and then plays nothing', async () => {
    await sleep(3100);
    const before = audioRuns().length;
    const waiting = await appApi(lab, 'POST', '/api/diagnostics/audio/tone', { confirm: 'play', delaySeconds: 10 });
    assert.equal(waiting.status, 200, waiting.text);
    assert.equal(waiting.body.audio.state, 'waiting');
    assert.ok(waiting.body.audio.secondsUntilStart > 8 && waiting.body.audio.secondsUntilStart <= 10);
    await sleep(500);
    assert.equal((await appApi(lab, 'POST', '/api/diagnostics/audio/cancel')).status, 200);
    const run = await finished(3);
    assert.equal(run.outcome, 'cancelled');
    assert.equal(run.openCalled, false);
    assert.ok(run.portState.points.every((p) => !p.attempted), 'no port, no port state');
    assert.equal(audioRuns().length, before, 'nothing was played');
  });

  await step('audio lab v3: each failing stage is reported as such and the app keeps running', async () => {
    const expect = [
      [{ AKENO_SIM_AUDIO_FAIL: 'init' }, 'init-failed', (r) => assert.equal(r.initResult, '0x80260001')],
      [{ AKENO_SIM_AUDIO_FAIL: 'open' }, 'open-failed', (r) => assert.equal(r.openResult, '0x80260005')],
      [{ AKENO_SIM_AUDIO_FAIL: 'output' }, 'submit-failed', (r) => {
        assert.equal(r.firstErrorResult, '0x80260007');
        assert.equal(r.firstErrorCall, 3);
        assert.equal(r.closeResult, '0x00000000', 'the port is closed after a failed output');
      }],
      [{ AKENO_SIM_AUDIO_FAIL: 'close' }, 'accepted', (r) => assert.match(r.error, /sceAudioOutClose returned 0x80260006/)],
      [{ AKENO_SIM_AUDIO: '' }, 'not-supported', (r) => assert.match(r.error, /libSceAudioOut\.sprx/)],
      [{ AKENO_SIM_AUDIO_PACE: 'none' }, 'accepted', (r) => assert.equal(r.paced, false, 'samples returned faster than real time')],
      [{ AKENO_SIM_AUDIO_FAIL: 'open' }, 'open-failed', (r) => assert.ok(r.portState.points.every((p) => !p.attempted), 'no port, no port state')],
      // Port state problems never change the outcome of the output test.
      [{ AKENO_SIM_AUDIO_PORTSTATE: 'fail' }, 'accepted', (r) => {
        for (const p of r.portState.points) {
          assert.deepEqual(p.calls.map((c) => c.result), ['0x80260003'], 'the second call is skipped after an error');
          assert.equal(p.writtenBytes, 0);
          assert.equal(p.decoded.output, null);
        }
      }],
      [{ AKENO_SIM_AUDIO_PORTSTATE: 'missing' }, 'accepted', (r) => {
        assert.equal(r.portState.exported, false);
        assert.ok(r.portState.points.every((p) => !p.attempted));
      }],
      [{ AKENO_SIM_AUDIO_PORTSTATE: 'unconnected' }, 'accepted', (r) => {
        assert.equal(r.portState.points[1].decoded.output, '0x0000');
        assert.equal(r.portState.points[1].decoded.outputNames, 'none');
        assert.equal(r.portState.points[1].decoded.channel, 0);
      }],
      [{ AKENO_SIM_AUDIO_PORTSTATE: 'overrun' }, 'accepted', (r) => {
        const [first, during, close] = r.portState.points;
        assert.equal(first.guardsIntact, false);
        assert.deepEqual(first.calls.map((c) => [c.leadIntact, c.tailIntact]), [[true, false]], 'no second call after a changed canary');
        assert.equal(first.shownBytes, 256);
        assert.equal(r.portState.stopped, true);
        assert.equal(during.attempted, false);
        assert.equal(close.attempted, false);
      }],
    ];
    for (const [env, outcome, check] of expect) {
      await restartLab(env);
      assert.equal((await appApi(lab, 'POST', '/api/diagnostics/audio/tone', { confirm: 'play' })).status, 200);
      const run = await finished(1);
      assert.equal(run.outcome, outcome, JSON.stringify(env));
      check(run);
      assert.equal((await audioState()).state, outcome);
      assert.equal((await appApi(lab, 'GET', '/api/state')).status, 200, 'the rest of the app is unaffected');
    }
  });

  await step('audio lab v3: the diagnostics page runs the checks, shows the port state, asks for the user\'s report and compares both browser paths', async () => {
    let chromium;
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
    lab.child.kill('SIGTERM');
    await lab.exited;
    // With a server address set the page shows the pairing screen and its Diagnostics button.
    lab = await startApp(join(work, 'app-audiolab-ui'), ['--port', '0', '--server', 'https://audio-lab.invalid'], audioEnv, audioLabBinary);
    const browser = await chromium.launch({ args: ['--autoplay-policy=no-user-gesture-required'] });
    try {
      const page = await browser.newPage({ viewport: { width: 1920, height: 1080 } });
      const errors = [];
      page.on('pageerror', (error) => errors.push(String(error)));
      await page.goto(`http://127.0.0.1:${lab.port}/#t=${lab.token}`);
      await page.locator('#release-badge', { hasText: 'Alpha 0.5 · Audio lab v3' }).waitFor();
      assert.doesNotMatch(await page.locator('#release-badge').textContent(), /v2/);
      await page.locator('#pair-diag').click();
      const section = page.locator('#audio-lab');
      await section.getByRole('heading', { name: 'Audio lab v3 (experimental build)' }).waitFor();
      await section.locator('#audio-state', { hasText: 'No output test yet' }).waitFor();
      await section.locator('#audio-probe').click();
      await section.getByText(/libSceAudioOut\.sprx \(used by the output test\)/).waitFor();
      await section.locator('#audio-tone').click();
      await section.getByText('Play a 1.5 s, 440 Hz tone on the console\'s main audio output now?').waitFor();
      assert.equal(await section.locator('#audio-preset').inputValue(), 'stereo16-1024');
      assert.equal(await section.locator('#audio-level').inputValue(), 'moderate');
      assert.equal(audioRuns().length, 0, 'nothing plays before the confirmation');
      await section.locator('#audio-tone-play').click();
      await section.locator('#audio-feedback').waitFor({ timeout: 15_000 });
      await section.locator('#audio-state', { hasText: 'Samples accepted — audible output NOT confirmed' }).waitFor();
      await section.getByText(/consumed at playback speed/).waitFor();
      await section.getByText(/before-playback: 0 \(success\) \/ 0 \(success\), 32 bytes written · decoded \(unverified\): output 0x0001 \(primary\), channel 2, volume 127/).waitFor();
      if (process.env.AKENO_UI_SCREENSHOTS) await page.screenshot({ path: join(process.env.AKENO_UI_SCREENSHOTS, 'audio-lab-v3-accepted.png'), fullPage: true });
      await section.locator('#audio-heard-no').click();
      await section.locator('#audio-state', { hasText: 'Samples accepted, but nothing was heard' }).waitFor();

      await section.locator('#audio-browser').click();
      await section.locator('#audio-browser-play').click();
      await section.locator('#audio-browser-feedback').waitFor({ timeout: 10_000 });
      await section.locator('#audio-bheard-tv').click();
      await section.getByText(/Last browser tone/).waitFor();
      await section.getByText('canPlayType WAV', { exact: true }).waitFor();

      // HTML <audio> element: the generated WAV from a blob: URL, then the user's report.
      await section.locator('#audio-media').click();
      await section.getByText(/through the browser's HTML <audio> element/).waitFor();
      await section.locator('#audio-media-play').click();
      await section.locator('#audio-media-feedback').waitFor({ timeout: 10_000 });
      const pending = await section.locator('#audio-media-feedback').textContent();
      assert.match(pending, /blob URL\): play\(\) resolved, played to the end\. Did you hear it\?/);
      await section.locator('#audio-mheard-no').click();
      await section.getByText(/Last <audio> tone/).waitFor();
      const media = (await audioState()).media;
      assert.equal(media.source, 'blob');
      assert.equal(media.playResult, 'resolved');
      assert.equal(media.loadedMetadata, true);
      assert.equal(media.ended, true);
      assert.equal(media.errorCode, 0);
      assert.ok(media.duration > 1.9 && media.duration < 2.1, String(media.duration));
      assert.equal(media.heard, false);
      assert.match(media.features, /Web Audio \(AudioContext\): available; .*canPlayType WAV: (maybe|probably)/);
      assert.equal(audioRuns().length, 1, 'the <audio> test does not touch the native port');
      await section.locator('#audio-report-btn').click();
      await section.locator('#audio-report', { hasText: 'BROWSER AUDIO' }).waitFor();
      const report = await section.locator('#audio-report').textContent();
      assert.match(report, /User report: not heard/);
      assert.match(report, /API: available/);
      assert.match(report, /BROWSER <audio> ELEMENT \(generated WAV, for comparison; v3\)\n {2}Source: blob URL; play\(\): resolved; events: loadedmetadata yes, canplay yes, playing yes, ended yes; MediaError 0; duration 2\.00 s/);
      assert.match(report, /Browser features \(read-only check\): Web Audio/);
      if (process.env.AKENO_UI_SCREENSHOTS) await page.screenshot({ path: join(process.env.AKENO_UI_SCREENSHOTS, 'audio-lab-v3-report.png'), fullPage: true });
      assert.deepEqual(errors, []);
      assert.equal(audioRuns().length, 1);
    } finally {
      await browser.close();
      lab.child.kill('SIGTERM');
      await lab.exited;
    }
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
