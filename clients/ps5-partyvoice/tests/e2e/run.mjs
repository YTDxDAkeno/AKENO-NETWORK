#!/usr/bin/env node
// End-to-end test of AKENO Party Voice (docs/PS5_PARTY_VOICE.md). Nothing on the network side is mocked:
//   * a disposable PostgreSQL database and the production server build (apps/server/dist) behind a TLS terminator,
//   * a real LiveKit server (pinned) and the AKENO voice gateway (apps/voice-gateway/dist) behind its own TLS terminator,
//   * the native-IPC daemon (host build of clients/ps5), paired through its page and approved on the "website",
//   * the voice service (host build of clients/ps5-partyvoice: a simulated console microphone and speaker),
//   * the AKENO plugin's own code driving it (akeno-ingame-check --event: what a press in the PS5 Settings sends),
//   * a second party member in the role of a PC/phone browser: a LiveKit participant with its website token.
// Both directions are checked on decoded audio by frequency: the console's microphone is a 300 Hz tone, the friend
// speaks 600 Hz. A PC run proves the pipeline and the protocol; it is never evidence of PS5 speaker or microphone
// behaviour.
//
//   node clients/ps5-partyvoice/tests/e2e/run.mjs --voice <akeno-party-voice> --daemon <akeno-app-native-ipc>
//     --companion <akeno-ingame-check> --livekit <livekit-server>
//
// Requires `pnpm build`, PostgreSQL (TEST_DATABASE_ADMIN_URL) and openssl. PV_E2E_OUT keeps the reports.
import assert from 'node:assert/strict';
import { spawn, spawnSync } from 'node:child_process';
import { copyFileSync, existsSync, mkdirSync, mkdtempSync, readFileSync, rmSync, statSync, writeFileSync } from 'node:fs';
import { createRequire } from 'node:module';
import net from 'node:net';
import { tmpdir } from 'node:os';
import { dirname, join, resolve } from 'node:path';
import tls from 'node:tls';
import { fileURLToPath } from 'node:url';

const root = resolve(dirname(fileURLToPath(import.meta.url)), '../../../..');
const serverRequire = createRequire(resolve(root, 'apps/server/package.json'));
const pg = serverRequire('pg');
const rtc = createRequire(resolve(root, 'apps/voice-gateway/package.json'))('@livekit/rtc-node');
const arg = (name) => {
  const i = process.argv.indexOf(name);
  return i > 0 ? resolve(process.argv[i + 1]) : null;
};
const voiceBinary = arg('--voice');
const daemonBinary = arg('--daemon');
const companionBinary = arg('--companion');
const livekitBinary = arg('--livekit');
if (!voiceBinary || !daemonBinary || !companionBinary || !livekitBinary) {
  console.error('usage: run.mjs --voice <akeno-party-voice> --daemon <akeno-app-native-ipc> --companion <akeno-ingame-check> --livekit <livekit-server>');
  process.exit(2);
}
const adminUrl = process.env.TEST_DATABASE_ADMIN_URL ?? 'postgres://akeno:akeno_dev_password@127.0.0.1:5432/postgres';
const databaseName = 'akeno_partyvoice_e2e';
const ADMIN_PASSWORD = 'partyvoice-e2e-admin-password-1';
const PASSWORD = 'correct-horse-battery-staple';
const LK_KEY = 'e2ekey';
const LK_SECRET = 'partyvoice-e2e-livekit-secret-0123456789abcdef';
const work = mkdtempSync(join(tmpdir(), 'akeno-partyvoice-e2e-'));
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
  let last;
  while (Date.now() < deadline) {
    last = await predicate();
    if (last) return last;
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

// ---- Server, LiveKit, gateway, TLS ------------------------------------------------------------

const backendPort = await freePort();
const origin = `http://localhost:${backendPort}`;
const livekitPort = await freePort();
const gatewayPort = await freePort();
const gatewayTlsPort = await freePort();
const databaseUrl = new URL(adminUrl);
databaseUrl.pathname = `/${databaseName}`;
const serverEnv = {
  MEDIA_DIR: join(work, 'media'), ...process.env, NODE_ENV: 'production', HOST: '127.0.0.1', PORT: String(backendPort),
  DATABASE_URL: databaseUrl.toString(), PUBLIC_ORIGIN: origin, REGISTRATION_MODE: 'invite', MIGRATE_ON_START: 'true',
  LOG_LEVEL: 'warn', LOG_PRETTY: 'false', LIVEKIT_URL: `ws://127.0.0.1:${livekitPort}`, LIVEKIT_API_KEY: LK_KEY,
  // every actor of this test (console, website, the test's own polling) shares 127.0.0.1 and its rate limit
  RATE_LIMIT_MAX_PER_MINUTE: '20000',
  LIVEKIT_API_SECRET: LK_SECRET, VOICE_GATEWAY_URL: `wss://127.0.0.1:${gatewayTlsPort}/voice-gateway`,
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
}

async function registerUser(admin, onlineId, displayName) {
  const invitation = await admin.request('POST', '/admin/invitations', { maxUses: 1, expiresInHours: 1, note: 'party voice e2e' });
  assert.equal(invitation.status, 201);
  const session = new WebSession();
  const response = await session.request('POST', '/auth/register', { onlineId, password: PASSWORD, invitationCode: invitation.body.code });
  assert.equal(response.status, 201, JSON.stringify(response.body));
  session.csrf = response.body.csrfToken;
  session.user = response.body.user;
  if (displayName) assert.equal((await session.request('PATCH', '/profile', { displayName })).status, 200);
  return session;
}

function openssl(args) {
  const result = spawnSync('openssl', args, { cwd: work, encoding: 'utf8' });
  if (result.status !== 0) throw new Error(`openssl ${args.join(' ')} failed: ${result.stderr}`);
}

function createCertificates() {
  openssl(['req', '-x509', '-newkey', 'rsa:2048', '-nodes', '-keyout', 'ca.key', '-out', 'ca.pem', '-days', '2', '-subj',
    '/CN=AKENO Party Voice E2E CA', '-addext', 'basicConstraints=critical,CA:TRUE', '-addext', 'keyUsage=critical,keyCertSign,cRLSign']);
  openssl(['req', '-newkey', 'rsa:2048', '-nodes', '-keyout', 'server.key', '-out', 'server.csr', '-subj', '/CN=127.0.0.1']);
  writeFileSync(join(work, 'ext.cnf'), 'subjectAltName=IP:127.0.0.1\nbasicConstraints=CA:FALSE\nkeyUsage=digitalSignature,keyEncipherment\nextendedKeyUsage=serverAuth\n');
  openssl(['x509', '-req', '-in', 'server.csr', '-CA', 'ca.pem', '-CAkey', 'ca.key', '-CAcreateserial', '-out', 'server.pem', '-days', '2', '-extfile', 'ext.cnf']);
}

const gatewaySockets = new Set();
let gatewayConnections = 0;
async function startTlsProxy(port, upstreamPort, isGateway) {
  if (!existsSync(join(work, 'server.pem'))) createCertificates();
  const server = tls.createServer({ key: readFileSync(join(work, 'server.key')), cert: readFileSync(join(work, 'server.pem')) }, (socket) => {
    const upstream = net.connect(upstreamPort, '127.0.0.1');
    if (isGateway) {
      gatewaySockets.add(socket);
      gatewayConnections++;
    }
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

for (const required of ['apps/server/dist/main.js', 'apps/server/dist/cli.js', 'apps/voice-gateway/dist/main.js', voiceBinary, daemonBinary, companionBinary, livekitBinary])
  if (!existsSync(resolve(root, required))) throw new Error(`Missing ${required}`);

const pgAdmin = new pg.Client({ connectionString: adminUrl });
await pgAdmin.connect();
await pgAdmin.query(`DROP DATABASE IF EXISTS ${databaseName} WITH (FORCE)`);
await pgAdmin.query(`CREATE DATABASE ${databaseName}`);
await pgAdmin.end();
assert.equal(spawnSync('node', ['apps/server/dist/cli.js', 'migrate'], { cwd: root, env: serverEnv, encoding: 'utf8' }).status, 0);
assert.equal(spawnSync('node', ['apps/server/dist/cli.js', 'create-admin', '--online-id', 'it_admin', '--password-stdin'],
  { cwd: root, env: serverEnv, input: ADMIN_PASSWORD, encoding: 'utf8' }).status, 0);

writeFileSync(join(work, 'livekit.yaml'), [
  `port: ${livekitPort}`, 'bind_addresses: ["127.0.0.1"]', 'rtc:', `  tcp_port: ${await freePort()}`, `  udp_port: ${await freePort()}`,
  '  use_external_ip: false', '  node_ip: 127.0.0.1', 'keys:', `  ${LK_KEY}: ${LK_SECRET}`, 'room:', '  auto_create: false',
  'webhook:', `  api_key: ${LK_KEY}`, '  urls:', `    - http://127.0.0.1:${backendPort}/api/v1/voice/webhook`, 'logging:', '  level: warn', '',
].join('\n'));
const livekit = spawn(livekitBinary, ['--config', join(work, 'livekit.yaml')], { stdio: ['ignore', 'ignore', 'ignore'] });
children.add(livekit);
await waitFor(async () => (await fetch(`http://127.0.0.1:${livekitPort}`).catch(() => null))?.ok, 'LiveKit');
// every child's output is read (or ignored): a full pipe would block it
const keep = (text, chunk) => (text + chunk).slice(-200_000);
const gateway = spawn('node', ['apps/voice-gateway/dist/main.js'], {
  cwd: root,
  env: { ...process.env, GATEWAY_HOST: '127.0.0.1', GATEWAY_PORT: String(gatewayPort), LIVEKIT_URL: `ws://127.0.0.1:${livekitPort}`,
    LIVEKIT_API_KEY: LK_KEY, LIVEKIT_API_SECRET: LK_SECRET, LOG_LEVEL: 'info' },
  stdio: ['ignore', 'pipe', 'pipe'],
});
children.add(gateway);
let gatewayLog = '';
gateway.stdout.on('data', (d) => (gatewayLog = keep(gatewayLog, d)));
gateway.stderr.on('data', (d) => (gatewayLog = keep(gatewayLog, d)));
const gatewaySessions = async () => JSON.parse(await (await fetch(`http://127.0.0.1:${gatewayPort}/healthz`)).text()).sessions;
await waitFor(async () => (await fetch(`http://127.0.0.1:${gatewayPort}/healthz`).catch(() => null))?.ok, 'the voice gateway');
await startTlsProxy(gatewayTlsPort, gatewayPort, true);

const backend = spawn('node', ['apps/server/dist/main.js'], { cwd: root, env: serverEnv, stdio: ['ignore', 'ignore', 'ignore'] });
children.add(backend);
await waitFor(async () => (await fetch(`http://127.0.0.1:${backendPort}/health/ready`).catch(() => null))?.ok, 'the server');
const tlsPort = await freePort();
const tlsServer = await startTlsProxy(tlsPort, backendPort, false);
const serverUrl = `https://127.0.0.1:${tlsPort}`;

const admin = await new WebSession().signIn('it_admin', ADMIN_PASSWORD);
const alice = await registerUser(admin, 'it_alice', 'Alice');
const bob = await registerUser(admin, 'it_bob', 'Bob');
const request = await alice.request('POST', '/friends/requests', { userId: bob.user.id });
assert.equal(request.status, 201);
assert.equal((await bob.request('POST', `/friends/requests/${request.body.request.id}/accept`)).status, 200);

// ---- The console: daemon, voice service, plugin ---------------------------------------------------

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

const akenoDir = join(work, 'akeno');
mkdirSync(akenoDir, { mode: 0o700 });
copyFileSync(join(work, 'ca.pem'), join(akenoDir, 'ca.pem'));
const daemon = { port: 0, token: '', out: '', err: '' };
const daemonChild = spawn(daemonBinary, ['--data-dir', akenoDir, '--port', '0', '--server', serverUrl, '--ca-file', join(work, 'ca.pem')], {
  env: { ...process.env, AKENO_BACKOFF_BASE_MS: '300' }, stdio: ['ignore', 'pipe', 'pipe'],
});
children.add(daemonChild);
daemonChild.stderr.on('data', (chunk) => (daemon.err = keep(daemon.err, chunk)));
daemonChild.stdout.on('data', (chunk) => {
  daemon.out = keep(daemon.out, chunk);
  const match = /"url":"http:\/\/127\.0\.0\.1:(\d+)\/#t=([0-9a-f]{64})"/.exec(daemon.out);
  if (match && !daemon.port) {
    daemon.port = Number(match[1]);
    daemon.token = match[2];
  }
});
await waitFor(() => daemon.port, 'the daemon to start');
const key = () => readFileSync(join(akenoDir, 'native.key'), 'utf8');
await local(daemon.port, 'POST', '/api/pair/start', { 'X-Akeno-Token': daemon.token });
const pairing = (await local(daemon.port, 'GET', '/api/state', { 'X-Akeno-Token': daemon.token })).body.pairing;
assert.equal((await alice.request('POST', '/devices/pairings/approve', { userCode: pairing.userCode })).status, 204);
await waitFor(async () => {
  const s = (await local(daemon.port, 'GET', '/api/state', { 'X-Akeno-Token': daemon.token })).body;
  return s.paired && s.realtime.state === 'connected';
}, 'pairing and the realtime connection', 30_000);
const device = (await alice.request('GET', '/devices')).body.devices.find((d) => d.platform.includes('PlayStation'));
assert.ok(device, 'the console is paired');

const voiceDir = join(work, 'party-voice');
const bigappFile = join(work, 'bigapp');
writeFileSync(bigappFile, '0');
const voice = { child: null, port: 0, out: '' };
async function startVoice(env = {}) {
  voice.port = 0;
  voice.out = '';
  voice.child = spawn(voiceBinary, ['--akeno-dir', akenoDir, '--data-dir', voiceDir, '--daemon-port', String(daemon.port), '--port', '0'], {
    env: { ...process.env, PV_SIM_MIC: 'tone:300', PV_SIM_BIGAPP_FILE: bigappFile, ...env }, stdio: ['ignore', 'pipe', 'pipe'],
  });
  children.add(voice.child);
  voice.exited = new Promise((done) => voice.child.once('exit', (code, signal) => {
    children.delete(voice.child);
    done({ code, signal });
  }));
  voice.child.stdout.on('data', (chunk) => {
    voice.out = keep(voice.out, chunk);
    const m = /AKENO_PARTY_VOICE_READY port=(\d+)/.exec(voice.out);
    if (m && !voice.port) voice.port = Number(m[1]);
  });
  voice.child.stderr.on('data', (chunk) => (voice.out = keep(voice.out, chunk)));
  await waitFor(() => voice.port, 'the voice service to start');
}
const status = async () => (await local(voice.port, 'GET', '/api/voice/status', { 'X-Akeno-Native-Key': key() })).body;

/** Runs the plugin's check tool without blocking this process (the TLS terminators live here). */
function companion(args) {
  return new Promise((done) => {
    const child = spawn(companionBinary, ['--port', String(daemon.port), '--akeno-dir', akenoDir, '--voice-port', String(voice.port), ...args],
      { stdio: ['ignore', 'pipe', 'pipe'] });
    let out = '';
    child.stdout.on('data', (d) => (out += d));
    child.stderr.on('data', (d) => (out += d));
    const timer = setTimeout(() => child.kill('SIGKILL'), 60_000);
    child.on('exit', (code) => {
      clearTimeout(timer);
      done({ code, out });
    });
  });
}
/** A press on an AKENO page, delivered as onionHEN delivers it, through the plugin's own code. */
const press = (...events) => companion(events.flatMap((e) => ['--event', e]));
const pages = () => companion(['--pages']);
/** A press that must work; the tool's output explains a failure. */
async function pressOk(...events) {
  const r = await press(...events);
  assert.equal(r.code, 0, r.out);
  return r;
}

// ---- The friend on a PC: LiveKit directly with the website's token -------------------------------

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

const friend = { room: null, heard: [], published: 0, unpublished: 0, speaking: false, phase: 0, source: null, loop: null };
async function friendJoins(partyId) {
  const pass = await bob.request('POST', `/parties/${partyId}/voice/token`);
  assert.equal(pass.status, 200, JSON.stringify(pass.body));
  friend.room = new rtc.Room();
  await friend.room.connect(pass.body.url, pass.body.token, { autoSubscribe: true, dynacast: false });
  friend.room.on(rtc.RoomEvent.TrackPublished, (_p, participant) => participant.identity === alice.user.id && friend.published++);
  friend.room.on(rtc.RoomEvent.TrackUnpublished, (_p, participant) => participant.identity === alice.user.id && friend.unpublished++);
  friend.room.on(rtc.RoomEvent.TrackSubscribed, (track, _pub, participant) => {
    if (track.kind !== rtc.TrackKind.KIND_AUDIO || participant.identity !== alice.user.id) return;
    void (async () => {
      for await (const frame of new rtc.AudioStream(track, { sampleRate: 48000, numChannels: 1, frameSizeMs: 20 })) friend.heard.push({ at: Date.now(), hz: toneHz(frame.data) });
    })();
  });
  friend.source = new rtc.AudioSource(48000, 1);
  const options = new rtc.TrackPublishOptions();
  options.source = rtc.TrackSource.SOURCE_MICROPHONE;
  await friend.room.localParticipant.publishTrack(rtc.LocalAudioTrack.createAudioTrack('microphone', friend.source), options);
  friend.speaking = true;
  friend.loop = (async () => {
    while (friend.room) {
      const pcm = new Int16Array(960);
      for (let i = 0; i < 960; i++) {
        pcm[i] = friend.speaking ? Math.round(9000 * Math.sin(friend.phase)) : 0;
        friend.phase += (2 * Math.PI * 600) / 48000;
      }
      await friend.source.captureFrame(new rtc.AudioFrame(pcm, 48000, 1, 960));
    }
  })();
}
const hearsConsole = (since, min = 15) => friend.heard.filter((h) => h.at >= since && Math.abs(h.hz - 300) <= 10).length >= min;
const inVoiceOnServer = async (partyId) =>
  (await bob.request('GET', `/parties/${partyId}`)).body?.members?.find((m) => (m.user?.id ?? m.id) === alice.user.id)?.voice?.connected === true;

// ---- Tests -------------------------------------------------------------------------------------

log(`AKENO Party Voice end-to-end tests (server ${serverUrl}, gateway wss://127.0.0.1:${gatewayTlsPort}/voice-gateway)`);
let party = null;

await step('setup: a party (Alice on the console, Bob on a PC); the voice service starts idle and opens nothing', async () => {
  party = (await alice.request('POST', '/parties', { name: 'Atelier night', privacy: 'friends' })).body;
  assert.ok(party?.id, 'party created');
  assert.equal(party.voice.available, true, 'the server offers party voice');
  assert.equal((await bob.request('POST', `/parties/${party.id}/join`)).status, 200);
  await startVoice();
  const s = await status();
  assert.equal(s.summary, 'Not in voice');
  assert.equal(s.mic.state, 'off');
  assert.equal(s.output.state, 'off');
  assert.equal(await gatewaySessions(), 0);
  // the hello needs no key; everything else does, and pages (Origin) are refused
  assert.equal((await local(voice.port, 'GET', '/api/voice/hello')).body.app, 'akeno-party-voice');
  assert.equal((await local(voice.port, 'GET', '/api/voice/status')).status, 403);
  assert.equal((await local(voice.port, 'GET', '/api/voice/status', { 'X-Akeno-Native-Key': key(), Origin: 'http://evil.example' })).status, 403);
  assert.equal((await local(voice.port, 'POST', '/api/voice/join', { 'X-Akeno-Native-Key': key() }, { partyId: party.id, extra: 1 })).status, 400);
  const page = await pages();
  assert.equal(page.code, 0, page.out);
  assert.match(page.out, /Voice service: ready 0\.2\.0-alpha/);
  assert.match(page.out, /Page voice\n/);
});

await step('test B: the test sound outside voice goes through the voice pipeline and is checked in the submitted buffer', async () => {
  const r = await press('vtest');
  assert.equal(r.code, 0, r.out);
  const s = await waitFor(async () => {
    const v = await status();
    return v.test.state !== 'playing' && v.test.state !== 'none' ? v : null;
  }, 'the test sound to finish', 10_000);
  assert.equal(s.test.state, 'submitted');
  assert.equal(s.test.inVoice, false);
  assert.equal(s.test.route, 1);
  assert.ok(s.test.verifiedBlocks >= 5 && s.test.hzMin >= 836 && s.test.hzMax <= 924, JSON.stringify(s.test));
  assert.equal(s.output.probe, 'consumed', 'the simulated port is consumed by its clock');
  assert.equal(s.evidence.audioSubmitted, true);
  assert.equal(s.evidence.audioAudible, 'unconfirmed', 'audible is never a measurement');
  assert.equal(s.process.kind, 'background');
  assert.equal(s.output.state, 'off', 'the test port is its own and closed again');
  assert.match(readFileSync(join(voiceDir, 'voice.log'), 'utf8'), /voice\.test_sound: route 1 \(on its own\): \d+ Opus packets encoded/);
  const report = await waitFor(() => {
    const text = readFileSync(join(voiceDir, 'report.txt'), 'utf8');
    return /Result: +SUBMITTED/.test(text) ? text : null;
  }, 'the report of the test sound', 5000);
  assert.match(report, /AUDIO SUBMITTED: +yes - accepted output blocks with sound: 0 of party speech, [1-9]\d* of the test sound/);
  assert.match(report, /Audible: +NOT CONFIRMED - only if you heard 1 beep\n/);
});

await step('1. join voice from the AKENO page: connected, the server sees the console, the microphone stays off', async () => {
  const r = await press(`vjoin.${party.id}`);
  assert.equal(r.code, 0, r.out);
  assert.match(r.out, /Action vjoin\.[0-9a-f-]+: ok: Joining the voice chat of Atelier night\. Your microphone is off\./);
  await waitFor(async () => (await status()).network.state === 'connected', 'the voice service to connect', 20_000);
  const s = await status();
  assert.equal(s.network.ready, true);
  assert.equal(s.network.daemonIpc, 3);
  assert.equal(s.mic.enabled, false);
  assert.equal(s.mic.state, 'off', 'no microphone before the user turns it on');
  assert.equal(s.evidence.networkConnected, true);
  assert.equal(await gatewaySessions(), 1);
  await waitFor(() => inVoiceOnServer(party.id), 'the server to show Alice in voice', 10_000);
  assert.doesNotMatch(readFileSync(join(voiceDir, 'voice.log'), 'utf8'), /microphone open/);
});

await step('2.+3. Bob joins from a browser; his voice reaches the console decoded (600 Hz) and goes to the speaker', async () => {
  await friendJoins(party.id);
  await waitFor(async () => {
    const s = await status();
    return s.participants.some((p) => p.name === 'Bob') && s.participants.some((p) => p.self);
  }, 'Bob in the participant list', 15_000);
  const s = await waitFor(async () => {
    const v = await status();
    const tone = v.stats.rxToneHz >= 590 && v.stats.rxToneHz <= 610;
    const played = v.stats.decodedWithSound > 50 && v.stats.outBlocksWithSound > 20 && v.evidence.speakerOutput;
    return tone && played ? v : null;
  }, 'the console to decode Bob (600 Hz) and play it (more than 20 output blocks with sound)', 20_000);
  assert.equal(s.output.state, 'active');
  assert.equal(s.output.paced, 'yes', 'the simulated port consumed the blocks at playback speed');
  assert.match(s.summary, /^Connected · Listening · Microphone off$/);
});

await step('7. speaking indicators: Bob speaking, then not; the plugin shows it', async () => {
  await waitFor(async () => (await status()).participants.find((p) => p.name === 'Bob')?.speaking, 'Bob to show as speaking', 10_000);
  const page = await pages();
  assert.match(page.out, /Voice person: Bob \(speaking\)/);
  friend.speaking = false;
  await waitFor(async () => (await status()).participants.find((p) => p.name === 'Bob')?.speaking === false, 'Bob to stop speaking', 10_000);
  friend.speaking = true;
});

await step('4. the user turns the microphone on: Bob hears the console (300 Hz), the gateway decoded it', async () => {
  const r = await press('vmicon');
  assert.equal(r.code, 0, r.out);
  const mark = Date.now();
  await waitFor(() => hearsConsole(mark), 'Bob to hear the console (300 Hz)', 20_000);
  const s = await waitFor(async () => {
    const v = await status();
    const enough = v.stats.micFramesWithSound > 20 && v.stats.sentWithSound > 20;
    return v.evidence.micTransmitting && v.evidence.gatewayHeardLevel > 0 && enough ? v : null;
  }, 'the evidence of transmission (more than 20 frames with sound captured and sent)', 10_000);
  assert.equal(s.mic.state, 'active');
  assert.equal(s.mic.rate, 16000);
  assert.ok(s.stats.txToneHz >= 290 && s.stats.txToneHz <= 310, `tx tone ${s.stats.txToneHz}`);
  await waitFor(async () => (await status()).participants.find((p) => p.self)?.speaking, 'the console to show as speaking', 10_000);
});

await step('5. mute and unmute: nobody hears a muted console; unmuted, Bob hears it again', async () => {
  const before = friend.unpublished;
  await pressOk('vmute');
  await waitFor(() => friend.unpublished > before, 'the console microphone track to end', 8000);
  await sleep(600);
  const mark = Date.now();
  await sleep(1200);
  assert.ok(!hearsConsole(mark, 3), 'nothing from a muted console');
  const s = await status();
  assert.equal(s.mic.muted, true);
  assert.equal(s.mic.state, 'active', 'muted keeps the port open (nothing is sent)');
  await pressOk('vunmute');
  const again = Date.now();
  await waitFor(() => hearsConsole(again), 'Bob to hear the console again', 15_000);
});

await step('6. deafen and undeafen: nothing is played and the console is muted; undeafened, Bob is heard again', async () => {
  await pressOk('vdeafen');
  await waitFor(async () => {
    const s = await status();
    return s.output.deafened && s.mic.muted && s.output.level === 0;
  }, 'silence while deafened', 10_000);
  await pressOk('vundeafen');
  await waitFor(async () => {
    const s = await status();
    return !s.output.deafened && s.output.level > 0 && Math.abs(s.stats.rxToneHz - 600) <= 10;
  }, 'Bob audible again', 10_000);
  await pressOk('vunmute');
  const mark = Date.now();
  await waitFor(() => hearsConsole(mark), 'Bob to hear the console after undeafen and unmute', 15_000);
});

await step('volume and gain within limits; the speaker route; the test sound goes through the voice pipeline, mixed in', async () => {
  for (let i = 0; i < 7; i++) await pressOk('vvolup');
  let s = await status();
  assert.equal(s.output.volume, 150, 'the output volume stops at 150 %');
  await pressOk('vvoldn');
  await pressOk('vgaindn');
  s = await status();
  assert.equal(s.output.volume, 140);
  assert.equal(s.mic.gain, 90);
  await pressOk('vroute.2');
  s = await status();
  assert.equal(s.output.route, 2);
  await pressOk('vtest');
  const t = await waitFor(async () => {
    const v = await status();
    return v.stats.testSounds === 2 && v.test.state === 'submitted' ? v : null;
  }, 'the test sound, mixed into the voice output', 10_000);
  assert.equal(t.test.inVoice, true);
  assert.equal(t.test.route, 2);
  assert.ok(t.test.toneBlocks >= 5, JSON.stringify(t.test));
  await pressOk('vroute.1');
  assert.equal((await local(voice.port, 'POST', '/api/voice/volume', { 'X-Akeno-Native-Key': key() }, { output: 151 })).status, 400);
  await pressOk('vvoldn', 'vvoldn', 'vvoldn', 'vvoldn');
  assert.equal((await status()).output.volume, 100);
});

await step('13. the AKENO pages opened and closed 10 times: voice runs on, no reconnect, no second session', async () => {
  // the route 1 port of the step before is reopened and probed (12 silent blocks, about 0.25 s) first
  await waitFor(async () => (await status()).output.state === 'active', 'the reopened output port', 5000);
  const before = await status();
  for (let i = 0; i < 10; i++) {
    if (i) await sleep(1500); // a person opens a page every few seconds at most (the server rate-limits a device)
    const page = await pages();
    assert.equal(page.code, 0, page.out);
    assert.match(page.out, /Voice: Connected · Listening · Microphone active/);
  }
  const after = await status();
  assert.equal(after.network.reconnects, before.network.reconnects);
  assert.ok(after.stats.sent > before.stats.sent + 50 && after.stats.received > before.stats.received + 50, 'audio kept flowing');
  assert.equal(await gatewaySessions(), 1);
});

await step('14. a game starts and no AKENO page is open for 12 s: voice keeps going both ways', async () => {
  writeFileSync(bigappFile, '0x1234');
  const before = await status();
  const mark = Date.now();
  await sleep(12_000);
  const after = await status();
  assert.equal(after.runtime.gameRunning, true);
  assert.ok(after.runtime.gameSecondsInVoice >= 8, `seconds in voice with the game: ${after.runtime.gameSecondsInVoice}`);
  assert.ok(after.stats.decodedWithSound > before.stats.decodedWithSound + 300, 'the console kept decoding Bob');
  assert.ok(hearsConsole(mark, 300), 'Bob kept hearing the console');
  assert.equal(after.network.state, 'connected');
  writeFileSync(bigappFile, '0');
});

await step('9. the network drops: the voice service reconnects with a new pass, both directions return', async () => {
  const before = await status();
  for (const socket of gatewaySockets) socket.destroy();
  await waitFor(async () => (await status()).network.reconnects > before.network.reconnects, 'the drop to be noticed', 20_000);
  await waitFor(async () => (await status()).network.state === 'connected', 'the reconnection', 30_000);
  const mark = Date.now();
  await waitFor(() => hearsConsole(mark), 'Bob to hear the console after the reconnect', 15_000);
  await waitFor(async () => Math.abs((await status()).stats.rxToneHz - 600) <= 10, 'Bob audible after the reconnect', 10_000);
  assert.ok(gatewayConnections >= 2);
});

await step('8. join again, then leave: everything closes (microphone, ports, gateway session, server state)', async () => {
  await pressOk(`vjoin.${party.id}`, 'vmicon');
  await waitFor(async () => (await status()).network.state === 'connected', 'connected again', 20_000);
  const mark = Date.now();
  await waitFor(() => hearsConsole(mark), 'Bob to hear the console', 15_000);
  const r = await press('vleave');
  assert.equal(r.code, 0, r.out);
  const s = await waitFor(async () => {
    const v = await status();
    return v.summary === 'Not in voice' && v.mic.state === 'off' && v.output.state === 'off' ? v : null;
  }, 'everything closed', 10_000);
  assert.equal(s.inVoice, false);
  await waitFor(async () => (await gatewaySessions()) === 0, 'the gateway session to end', 10_000);
  await waitFor(async () => !(await inVoiceOnServer(party.id)), 'the server to show Alice out of voice', 10_000);
  const logText = readFileSync(join(voiceDir, 'voice.log'), 'utf8');
  assert.match(logText, /microphone closed/);
  assert.match(logText, /output port closed/);
});

await step('10b. leaving the party from the AKENO page ends its voice chat (the server removes the console)', async () => {
  await pressOk(`vjoin.${party.id}`);
  await waitFor(async () => (await status()).network.state === 'connected', 'connected', 20_000);
  const r = await press(`pleave.${party.id}`);
  assert.equal(r.code, 0, r.out);
  const s = await waitFor(async () => {
    const v = await status();
    return v.network.state === 'disconnected' ? v : null;
  }, 'voice to end with the party', 20_000);
  assert.ok([4003, 4005].includes(s.network.closeCode), `close ${s.network.closeCode}`);
  // back into the party for the next steps
  assert.equal((await alice.request('POST', `/parties/${party.id}/join`)).status, 200);
});

await step('12. the voice service crashes and restarts: the plugin says so, nothing else breaks, joining works again', async () => {
  await pressOk(`vjoin.${party.id}`, 'vmicon');
  await waitFor(async () => (await status()).network.state === 'connected', 'connected', 20_000);
  voice.child.kill('SIGKILL');
  await voice.exited;
  let page = await pages();
  assert.equal(page.code, 0, page.out);
  assert.match(page.out, /Voice service: not running/);
  assert.match(page.out, /Party {11}Atelier night/, 'the other pages still work');
  const r = await press('vmicon');
  assert.match(r.out, /not running/);
  await waitFor(async () => (await gatewaySessions()) === 0, 'the gateway to drop the dead session', 40_000);
  await startVoice();
  assert.ok(existsSync(join(voiceDir, 'report-previous.txt')), 'the previous report is kept');
  assert.equal((await status()).mic.enabled, false, 'the microphone permission does not survive a restart');
  await pressOk(`vjoin.${party.id}`, 'vmicon');
  await waitFor(async () => (await status()).network.state === 'connected', 'connected after the restart', 20_000);
  const mark = Date.now();
  await waitFor(() => hearsConsole(mark), 'Bob to hear the console after the restart', 15_000);
  page = await pages();
  assert.match(page.out, /Voice service: ready/);
});

await step('11. the console refuses the microphone and the speaker: voice stays connected and says exactly what failed', async () => {
  const exit = await local(voice.port, 'POST', '/api/voice/exit', { 'X-Akeno-Native-Key': key() }, {});
  assert.equal(exit.status, 200);
  assert.equal((await voice.exited).code, 0);
  await startVoice({ PV_SIM_MIC: 'refused', PV_SIM_OUT: 'refused' });
  await pressOk(`vjoin.${party.id}`, 'vmicon');
  const s = await waitFor(async () => {
    const v = await status();
    return v.network.state === 'connected' && v.mic.state === 'failed' && v.output.state === 'failed' && v.stats.decoded > 50 ? v : null;
  }, 'connected with both audio sides failed', 20_000);
  assert.equal(s.mic.rc >>> 0, 0x80260105);
  assert.equal(s.mic.refusal, 'argument');
  assert.equal(s.output.rc >>> 0, 0x8026000a);
  // the refused microphone is not opened again in the same situation, and the page says why
  const calls = s.mic.openCalls;
  const again = await press('vmicon');
  assert.notEqual(again.code, 0, again.out);
  assert.match(again.out, /Action vmicon: failed: .*0x80260105.*tried again when a game starts or ends/);
  assert.equal((await status()).mic.openCalls, calls, 'no second sceAudioInOpen');
  // no output port: the test sound is refused, never "mixed in"
  const test = await press('vtest');
  assert.notEqual(test.code, 0, test.out);
  assert.match(test.out, /Action vtest: failed: .*no output to play the test sound on/);
  assert.match(s.summary, /Connected · Audio output unavailable · Microphone unavailable/);
  assert.equal(s.evidence.networkConnected, true);
  assert.equal(s.evidence.speakerOutput, false, 'never claimed without an output port');
  assert.equal(s.evidence.micTransmitting, false, 'never claimed without a microphone');
  const page = await pages();
  assert.match(page.out, /Voice mic: failed, off/);
  await pressOk('vleave');
});

// Last: removing a permission signs the device out (the server revokes its session; the daemon, unchanged, then
// deletes its credential and stops). Everything after this would need a new pairing.
await step('10a. revocation: the console loses "parties:write" on the website; voice ends at once (4003) and is not retried', async () => {
  await pressOk(`vjoin.${party.id}`);
  await waitFor(async () => (await status()).network.state === 'connected', 'connected before the revocation', 20_000);
  const scopes = device.scopes.filter((scope) => scope !== 'parties:write');
  assert.equal((await alice.request('PATCH', `/devices/${device.id}`, { scopes })).status, 200);
  const s = await waitFor(async () => {
    const v = await status();
    return v.network.state === 'disconnected' ? v : null;
  }, 'voice to end', 20_000);
  assert.equal(s.network.closeCode, 4003);
  assert.match(s.network.error, /removed from the voice chat/);
  assert.equal(s.inVoice, false);
  assert.equal(s.mic.state, 'off', 'the microphone closed with the session');
  await waitFor(async () => (await gatewaySessions()) === 0, 'the gateway session to end', 10_000);
  const page = await pages();
  assert.match(page.out, /Voice: Disconnected - You were removed from the voice chat\./);
});

await step('the report separates the eight outcomes and carries no name, ID, token or key', async () => {
  const exit = await local(voice.port, 'POST', '/api/voice/exit', { 'X-Akeno-Native-Key': key() }, {});
  assert.equal(exit.status, 200);
  await voice.exited;
  const report = readFileSync(join(voiceDir, 'report.txt'), 'utf8');
  const previous = readFileSync(join(voiceDir, 'report-previous.txt'), 'utf8');
  const logText = readFileSync(join(voiceDir, 'voice.log'), 'utf8');
  assert.match(report, /NETWORK CONNECTED: +yes/);
  assert.match(report, /REMOTE AUDIO RECEIVED: +yes/);
  assert.match(report, /AUDIO SUBMITTED: +no - no output block with sound was accepted \(the output port did not open\)/);
  assert.match(report, /MICROPHONE OPEN: +no - 0x80260105/);
  assert.match(report, /MICROPHONE TRANSMITTING: +no/);
  assert.match(previous, /REMOTE AUDIO RECEIVED: +yes/);
  assert.match(previous, /AUDIO SUBMITTED: +yes - accepted output blocks with sound: [1-9]\d* of party speech/);
  assert.match(previous, /MICROPHONE OPEN: +yes/);
  assert.match(previous, /MICROPHONE CAPTURING: +yes/);
  assert.match(previous, /MICROPHONE TRANSMITTING: +yes/);
  for (const text of [report, previous]) {
    // only people confirm sound; the report never does
    assert.match(text, /AUDIO AUDIBLE: +NOT CONFIRMED/);
    assert.match(text, /REMOTE USER HEARD MICROPHONE: +NOT CONFIRMED/);
    assert.doesNotMatch(text, /AUDIO AUDIBLE: +yes|HEARD MICROPHONE: +yes/);
    assert.match(text, /Process: +background process/);
  }
  assert.match(report, /\[Your answers - fill in and send back\]/);
  for (const text of [report, previous, logText]) {
    assert.doesNotMatch(text, /eyJ[A-Za-z0-9_-]{10,}/, 'no voice pass');
    assert.ok(!text.includes(key()), 'no native key');
    assert.ok(!text.includes(party.id) && !text.includes(alice.user.id) && !text.includes(bob.user.id), 'no IDs');
    assert.ok(!/Bob|Alice|it_bob|it_alice|Atelier/.test(text), 'no names');
  }
  for (const name of ['report.txt', 'voice.log']) assert.ok(statSync(join(voiceDir, name)).size < 600_000);
  // the daemon passed exactly the voice route, logging the route only
  const daemonLog = readFileSync(join(akenoDir, 'akeno.log'), 'utf8');
  assert.match(daemonLog, /native\.write POST \/parties\/:id\/voice\/device-token/);
  for (const line of daemonLog.split('\n').filter((l) => /native\.write/.test(l))) assert.ok(!line.includes(party.id), line);
  if (process.env.PV_E2E_OUT) {
    mkdirSync(process.env.PV_E2E_OUT, { recursive: true });
    copyFileSync(join(voiceDir, 'report.txt'), join(process.env.PV_E2E_OUT, 'report-device-failure.txt'));
    copyFileSync(join(voiceDir, 'report-previous.txt'), join(process.env.PV_E2E_OUT, 'report-two-way.txt'));
    copyFileSync(join(voiceDir, 'voice.log'), join(process.env.PV_E2E_OUT, 'voice.log'));
  }
});

// ---- Teardown --------------------------------------------------------------------------------

if (friend.room) {
  const room = friend.room;
  friend.room = null;
  await friend.loop?.catch(() => undefined);
  await room.disconnect().catch(() => undefined);
}
if (failures)
  log(`--- gateway log (tail) ---\n${gatewayLog.slice(-3000)}\n--- voice service (tail) ---\n${voice.out.slice(-3000)}\n--- daemon (tail) ---\n${daemon.err.slice(-2000)}`);
for (const child of children) child.kill('SIGTERM');
tlsServer.close();
await sleep(300);
const cleanup = new pg.Client({ connectionString: adminUrl });
await cleanup.connect();
await cleanup.query(`DROP DATABASE IF EXISTS ${databaseName} WITH (FORCE)`);
await cleanup.end();
if (failures === 0) rmSync(work, { recursive: true, force: true });
else log(`Artifacts kept in ${work}`);
log(failures === 0 ? 'All party voice end-to-end tests passed.' : `${failures} party voice end-to-end test(s) FAILED.`);
process.exit(failures === 0 ? 0 : 1);
