#!/usr/bin/env node
// Console voice staging drill: the production upgrade of docs/SERVER_VOICE_UPGRADE.md, rehearsed on
// disposable infrastructure with the production Compose stack (PostgreSQL, the app image, Caddy with
// TLS, LiveKit, the voice gateway) and a fresh database that never contains production data.
//
//   1. Baseline: a checkout of the deployed commit (on a local branch, like the VPS) with its own
//      compose.yaml and Caddyfile, voice (LiveKit) on for browsers, seeded with accounts, friends,
//      messages, a party and a paired console. The console's voice pass request answers
//      404 NOT_FOUND, as production did; vps-voice-diagnostics.sh reports it without secrets.
//   2. Upgrade, step by step as on the VPS: backup.sh, the rollback point, `git merge --ff-only`
//      in the same directory, then infrastructure/scripts/voice-upgrade.sh preflight and apply.
//      Existing data, sign-ins and the console pairing must survive unchanged; PostgreSQL and
//      LiveKit must not be restarted.
//   3. Console voice through wss://<domain>/voice-gateway: a simulated console (console-sim.mjs,
//      real Opus) and real Chromium browsers on the website exchange audio, checked by frequency
//      on both sides; refusals, mute, deafen, a duplicate session, a network drop with a fresh
//      pass, and revocation by party removal, permission removal and unpairing.
//      Optional: the PS5 voice-alpha's own code (host builds of the native-IPC daemon,
//      akeno-party-voice and the plugin's check tool from PR #18) against the same stack.
//   4. voice-upgrade.sh rollback 1, 2 and 3, then restore.sh with the step-2 backup.
//
// Requirements: Docker with Compose v2, the images (see --help), free ports 80, 443, 7881, 7882,
// `pnpm install` (ws, opusscript, Playwright from the workspace), a Playwright Chromium, and the
// staging domain resolving to 127.0.0.1 (e.g. "127.0.0.1 akeno-staging.local" in /etc/hosts).
// It refuses to run against anything else. Never point it at the production server.
import assert from 'node:assert/strict';
import { execFileSync, spawn, spawnSync } from 'node:child_process';
import { randomBytes } from 'node:crypto';
import { lookup } from 'node:dns/promises';
import { copyFileSync, existsSync, mkdirSync, mkdtempSync, readFileSync, readdirSync, rmSync, statSync, writeFileSync } from 'node:fs';
import { createRequire } from 'node:module';
import net from 'node:net';
import { networkInterfaces, tmpdir } from 'node:os';
import { dirname, join, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';
import { Api, ConsoleVoice, newClientMessageId, pairConsole } from './console-sim.mjs';

/* global window, AudioContext, MediaStream -- used inside functions that run in the browser page */

const repo = resolve(dirname(fileURLToPath(import.meta.url)), '../..');
const args = process.argv.slice(2);
const option = (name, fallback = null) => {
  const i = args.indexOf(name);
  return i >= 0 && i + 1 < args.length ? args[i + 1] : fallback;
};
if (args.includes('--help')) {
  console.log(`usage: node infrastructure/staging/voice-staging.mjs --baseline <commit> [options]

  --baseline <commit>          the deployed commit (its compose.yaml and Caddyfile are used in step 1)
  --candidate <commit>         the reviewed server update (default: HEAD)
  --baseline-image <tag>       app image built from the baseline (default akeno-network:baseline)
  --candidate-image <tag>      app image built from the candidate (default akeno-network:candidate)
  --gateway-image <tag>        voice gateway image from the candidate (default akeno-voice-gateway:candidate)
  --domain <name>              staging domain, must resolve to 127.0.0.1 (default akeno-staging.local)
  --node-ip <ipv4>             LiveKit node_ip (default: this machine's first non-loopback IPv4)
  --console-daemon <file>      optional: host build of akeno-app-native-ipc (PR #18)
  --console-voice <file>       optional: host build of akeno-party-voice (PR #18)
  --console-companion <file>   optional: host build of akeno-ingame-check (PR #18)
  --report <file>              write the results as JSON
  --keep                       leave the staging stack running at the end`);
  process.exit(0);
}

const baselineRev = option('--baseline');
if (!baselineRev) {
  console.error('--baseline <commit> is required (see --help)');
  process.exit(2);
}
const candidateRev = option('--candidate', 'HEAD');
const images = {
  baseline: option('--baseline-image', 'akeno-network:baseline'),
  candidate: option('--candidate-image', 'akeno-network:candidate'),
  gateway: option('--gateway-image', 'akeno-voice-gateway:candidate'),
};
const DOMAIN = option('--domain', 'akeno-staging.local');
const ORIGIN = `https://${DOMAIN}`;
const GATEWAY_URL = `wss://${DOMAIN}/voice-gateway`;
const PROJECT = 'akeno-voice-staging';
const consoleBinaries = { daemon: option('--console-daemon'), voice: option('--console-voice'), companion: option('--console-companion') };
const reportFile = option('--report');
const PASSWORD = 'staging-correct-horse-battery-staple';
const ADMIN_PASSWORD = `staging-admin-${randomBytes(9).toString('hex')}`;

const work = mkdtempSync(join(tmpdir(), 'akeno-voice-staging-'));
const deployDir = join(work, 'deploy');
const deployBranch = `voice-staging-deploy-${randomBytes(4).toString('hex')}`;
const results = [];
let failures = 0;
const log = (message) => process.stdout.write(`${message}\n`);
const sleep = (ms) => new Promise((done) => setTimeout(done, ms));

/** Runs one check; a failed critical step (the stack itself) ends the drill instead of cascading. */
async function step(name, fn, { critical = false } = {}) {
  const started = Date.now();
  try {
    const detail = await fn();
    results.push({ name, ok: true, ms: Date.now() - started, ...(detail ? { detail } : {}) });
    log(`  ok   ${name} (${Date.now() - started} ms)${detail ? `\n       ${typeof detail === 'string' ? detail : JSON.stringify(detail)}` : ''}`);
  } catch (error) {
    failures += 1;
    results.push({ name, ok: false, ms: Date.now() - started, error: String(error?.stack ?? error) });
    log(`  FAIL ${name}\n${String(error?.stack ?? error).replace(/^/gm, '       ')}`);
    if (critical) await finish();
  }
}

async function waitFor(predicate, what, timeoutMs = 20_000, everyMs = 250) {
  const deadline = Date.now() + timeoutMs;
  let last;
  while (Date.now() < deadline) {
    try {
      last = await predicate();
    } catch (error) {
      last = undefined;
      if (Date.now() + everyMs >= deadline) throw error;
    }
    if (last) return last;
    await sleep(everyMs);
  }
  throw new Error(`Timed out after ${timeoutMs} ms waiting for ${what}`);
}

// ---- Docker Compose in the deploy directory (the "VPS checkout") ---------------------------------

function compose(composeArgs, { input, allowFail = false, timeout = 300_000 } = {}) {
  const result = spawnSync('docker', ['compose', '-p', PROJECT, '--profile', 'https', '--profile', 'voice', ...composeArgs], {
    cwd: deployDir,
    input,
    encoding: 'utf8',
    timeout,
    env: { ...process.env, COMPOSE_PROJECT_NAME: PROJECT },
  });
  if (result.status !== 0 && !allowFail) {
    throw new Error(`docker compose ${composeArgs.join(' ')} failed (${result.status}):\n${result.stdout}\n${result.stderr}`);
  }
  return result;
}

function containerId(service) {
  return compose(['ps', '-q', service], { allowFail: true }).stdout.trim();
}

function git(gitArgs, cwd = repo) {
  return execFileSync('git', gitArgs, { cwd, encoding: 'utf8' }).trim();
}

function writeEnv(extra = {}) {
  const lines = Object.entries({ ...env, ...extra }).map(([k, v]) => `${k}=${v}`);
  writeFileSync(join(deployDir, '.env'), `${lines.join('\n')}\n`, { mode: 0o600 });
}

function primaryIpv4() {
  for (const list of Object.values(networkInterfaces())) {
    for (const address of list ?? []) if (address.family === 'IPv4' && !address.internal && !address.address.startsWith('172.17.')) return address.address;
  }
  throw new Error('no non-loopback IPv4 address found; pass --node-ip');
}

function portFree(port, host = '0.0.0.0') {
  return new Promise((done) => {
    const server = net.createServer();
    server.once('error', () => done(false));
    server.listen(port, host, () => server.close(() => done(true)));
  });
}

// ---- Safety checks: only ever a disposable stack on this machine ----------------------------------

const resolved = await lookup(DOMAIN).catch(() => null);
if (resolved?.address !== '127.0.0.1') {
  console.error(`${DOMAIN} must resolve to 127.0.0.1 on this machine (add "127.0.0.1 ${DOMAIN}" to /etc/hosts). It resolves to ${resolved?.address ?? 'nothing'}.`);
  process.exit(2);
}
for (const port of [80, 443, 7881]) {
  if (!(await portFree(port))) {
    console.error(`Port ${port} is in use. Run the staging drill on a machine without another AKENO stack (never on the production VPS).`);
    process.exit(2);
  }
}
if (execFileSync('docker', ['ps', '-aq', '--filter', `label=com.docker.compose.project=${PROJECT}`], { encoding: 'utf8' }).trim()) {
  console.error(`A Compose project named ${PROJECT} is already running; remove it first: docker compose -p ${PROJECT} down -v`);
  process.exit(2);
}
for (const image of Object.values(images)) {
  if (spawnSync('docker', ['image', 'inspect', image], { stdio: 'ignore' }).status !== 0) {
    console.error(`Docker image ${image} not found. Build it first (docs/SERVER_VOICE_UPGRADE.md, "Staging").`);
    process.exit(2);
  }
}
const baselineCommit = git(['rev-parse', '--verify', `${baselineRev}^{commit}`]);
const candidateCommit = git(['rev-parse', '--verify', `${candidateRev}^{commit}`]);
const nodeIp = option('--node-ip') ?? primaryIpv4();

const env = {
  POSTGRES_PASSWORD: randomBytes(18).toString('hex'),
  AKENO_DOMAIN: DOMAIN,
  AKENO_PUBLIC_ORIGIN: ORIGIN,
  REGISTRATION_MODE: 'invite',
  LOG_LEVEL: 'info',
  LIVEKIT_URL: `wss://${DOMAIN}/livekit`,
  LIVEKIT_API_KEY: 'akenostaging',
  LIVEKIT_API_SECRET: randomBytes(24).toString('hex'),
  VOICE_TOKEN_TTL_SECONDS: '120',
  LIVEKIT_NODE_IP: nodeIp,
  LIVEKIT_USE_EXTERNAL_IP: 'false',
};

log(`AKENO console voice staging drill
  baseline  ${baselineCommit} (${images.baseline})
  candidate ${candidateCommit} (${images.candidate}, ${images.gateway})
  domain    ${ORIGIN}  LiveKit node_ip ${nodeIp}
  work dir  ${work}`);

let ca = null;
const admin = new Api({ origin: ORIGIN, ca: null });
const users = {};
let party = null;
let consoleDevice = null;
let limitedDevice = null;
let backup = null;
let snapshot = null;
const containers = {};
let browser = null;
const browsers = {};
const phaseStart = {};
const evidence = { baseline: baselineCommit, candidate: candidateCommit, domain: DOMAIN };

const api = (user) => users[user].api;
const withCa = () => {
  admin.ca = ca;
  for (const user of Object.values(users)) user.api.ca = ca;
  if (consoleDevice) consoleDevice.ca = ca;
  if (limitedDevice) limitedDevice.ca = ca;
};

async function readyOverTls() {
  const r = await admin.request('GET', '/health/ready').catch(() => null);
  return r?.status === 200;
}

async function waitForStack(what) {
  await waitFor(async () => {
    if (!ca) {
      const pem = compose(['exec', '-T', 'caddy', 'cat', '/data/caddy/pki/authorities/local/root.crt'], { allowFail: true }).stdout;
      if (!pem.includes('BEGIN CERTIFICATE')) return false;
      ca = pem;
      writeFileSync(join(work, 'staging-ca.pem'), pem);
      withCa();
    }
    return readyOverTls();
  }, `${what} to answer /health/ready over HTTPS`, 120_000, 1000);
}

/** All the data a user can see, in a stable form, to prove an upgrade or rollback changed nothing. */
async function userView(name) {
  const a = api(name);
  const me = (await a.request('GET', '/users/me')).body;
  const friends = (await a.request('GET', '/friends')).body.friends.map((f) => f.id).sort();
  const conversations = (await a.request('GET', '/conversations')).body.conversations;
  const messages = {};
  for (const c of conversations) {
    const page = (await a.request('GET', `/conversations/${c.id}/messages`)).body;
    messages[c.id] = (page.messages ?? page.items ?? []).map((m) => `${m.id}|${m.sender?.id ?? m.senderId}|${m.body}`).sort();
  }
  const current = (await a.request('GET', '/parties/current')).body.party;
  const partyMessages = current ? ((await a.request('GET', `/parties/${current.id}/messages`)).body.messages ?? []).map((m) => `${m.id}|${m.body}`).sort() : [];
  return {
    id: me.id,
    onlineId: me.onlineId,
    displayName: me.profile?.displayName,
    friends,
    conversations: conversations.map((c) => c.id).sort(),
    messages,
    party: current ? { id: current.id, members: current.members.map((m) => m.user.id).sort(), leader: current.leaderId ?? current.leader?.id ?? null } : null,
    partyMessages,
  };
}

async function snapshotAll() {
  const view = {};
  for (const name of Object.keys(users)) view[name] = await userView(name);
  view.devices = (await api('alice').request('GET', '/devices')).body.devices.map((d) => `${d.id}|${[...d.scopes].sort().join(',')}`).sort();
  return view;
}

const deviceToken = (device, partyId = party.id) => device.request('POST', `/parties/${partyId}/voice/device-token`);

function decodeJwt(token) {
  const [, payload] = token.split('.');
  return JSON.parse(Buffer.from(payload, 'base64url').toString('utf8'));
}

async function gatewaySessions() {
  const out = compose(['exec', '-T', 'voice-gateway', 'node', '-e', "fetch('http://127.0.0.1:7890/healthz').then(r=>r.text()).then(t=>process.stdout.write(t))"], { allowFail: true }).stdout;
  return JSON.parse(out || '{"sessions":-1}').sessions;
}

/**
 * Environment for the shell scripts: curl must reach the staging stack directly (never through a
 * proxy from this machine's environment) and trust Caddy's local CA, as it trusts Let's Encrypt on the VPS.
 */
function scriptEnv(extra = {}) {
  const direct = Object.fromEntries(Object.entries(process.env).filter(([k]) => !/^(https?|all)_proxy$/i.test(k)));
  return { ...direct, COMPOSE_PROJECT_NAME: PROJECT, CURL_CA_BUNDLE: join(work, 'staging-ca.pem'), ...extra };
}

/** The read-only VPS diagnostics, run in the deploy checkout as the owner would (downloaded script). */
function diagnostics() {
  const run = spawnSync('bash', [resolve(repo, 'infrastructure/scripts/vps-voice-diagnostics.sh')], {
    cwd: deployDir,
    encoding: 'utf8',
    env: scriptEnv(),
    timeout: 120_000,
  });
  assert.equal(run.status, 0, run.stderr);
  for (const secret of [env.POSTGRES_PASSWORD, env.LIVEKIT_API_SECRET]) assert.ok(!run.stdout.includes(secret), 'the diagnostics never print a secret');
  return run.stdout;
}

/** infrastructure/scripts/voice-upgrade.sh of the deploy checkout (confirmations answered). */
function upgradeScript(scriptArgs) {
  return spawnSync('./infrastructure/scripts/voice-upgrade.sh', scriptArgs, {
    cwd: deployDir,
    encoding: 'utf8',
    env: scriptEnv({ ASSUME_YES: '1' }),
    timeout: 600_000,
  });
}

/** Which service answers a plain GET of /voice-gateway behind Caddy: the gateway (404 text) or the app (the web page). */
async function voiceGatewayRoute() {
  const r = await admin.request('GET', `${ORIGIN}/voice-gateway`, undefined, { Accept: '*/*' });
  if (r.status === 404 && r.text.trim() === 'Not found') return 'gateway';
  if (r.status === 200 && /<html/i.test(r.text)) return 'app';
  return `other (${r.status})`;
}

async function newConsoleVoice(device = consoleDevice) {
  const pass = await deviceToken(device);
  assert.equal(pass.status, 200, `voice pass: ${pass.status} ${pass.text}`);
  const voice = new ConsoleVoice({ url: pass.body.url, token: pass.body.token, ca, toneHz: 300 });
  const ready = await voice.connect();
  return { voice, pass: pass.body, ready };
}

// ---- Browsers (the website's own voice client, real WebRTC to LiveKit) ----------------------------

function speechLikeWav(hz) {
  const rate = 48_000;
  const samples = rate * 2;
  const data = Buffer.alloc(samples * 2);
  for (let n = 0; n < samples; n += 1) {
    const inBurst = (n % (rate / 2)) / rate;
    const fade = Math.min(1, inBurst / 0.01, Math.max(0, 0.3 - inBurst) / 0.01);
    const value = inBurst < 0.3 ? 0.5 * fade * Math.sin((2 * Math.PI * hz * n) / rate) : 0;
    data.writeInt16LE(Math.round(value * 32767), n * 2);
  }
  const header = Buffer.alloc(44);
  header.write('RIFF', 0);
  header.writeUInt32LE(36 + data.length, 4);
  header.write('WAVEfmt ', 8);
  header.writeUInt32LE(16, 16);
  header.writeUInt16LE(1, 20);
  header.writeUInt16LE(1, 22);
  header.writeUInt32LE(rate, 24);
  header.writeUInt32LE(rate * 2, 28);
  header.writeUInt16LE(2, 32);
  header.writeUInt16LE(16, 34);
  header.write('data', 36);
  header.writeUInt32LE(data.length, 40);
  const file = join(work, `microphone-${hz}.wav`);
  writeFileSync(file, Buffer.concat([header, data]));
  return file;
}

function instrument() {
  window.__stagingPeerConnections = [];
  const Native = window.RTCPeerConnection;
  window.RTCPeerConnection = class extends Native {
    constructor(...rest) {
      super(...rest);
      window.__stagingPeerConnections.push(this);
    }
  };
}

async function openBrowser(name) {
  const context = await browser.newContext({ ignoreHTTPSErrors: true, viewport: { width: 1366, height: 860 }, permissions: ['microphone'] });
  await context.grantPermissions(['microphone'], { origin: ORIGIN });
  await context.addInitScript(instrument);
  const page = await context.newPage();
  const errors = [];
  page.on('pageerror', (error) => errors.push(error.message));
  await page.goto(`${ORIGIN}/login`);
  await page.getByLabel('Online ID').fill(users[name].onlineId);
  await page.getByLabel('Password').fill(PASSWORD);
  await page.getByRole('button', { name: 'Sign in' }).click();
  await page.waitForURL((url) => !url.pathname.startsWith('/login'), { timeout: 30_000 });
  browsers[name] = { page, context, errors };
  return browsers[name];
}

async function joinVoiceInBrowser(name) {
  const { page } = browsers[name];
  await page.goto(`${ORIGIN}/parties`);
  await page.getByTestId('join-voice').click({ timeout: 30_000 });
  await waitFor(async () => (await page.getByTestId('party-voice').getAttribute('data-voice-status')) === 'connected', `${name}'s browser to connect to voice`, 45_000, 500);
}

/** Dominant frequency and level of every remote audio track the browser receives (Web Audio analyser). */
async function remoteTones(name) {
  return browsers[name].page.evaluate(async () => {
    const tracks = [];
    for (const pc of window.__stagingPeerConnections) {
      if (pc.connectionState === 'closed') continue;
      for (const receiver of pc.getReceivers()) if (receiver.track?.kind === 'audio' && receiver.track.readyState === 'live') tracks.push(receiver.track);
    }
    const ctx = window.__stagingAudio ?? (window.__stagingAudio = new AudioContext());
    if (ctx.state !== 'running') await ctx.resume().catch(() => undefined);
    const out = [];
    for (const track of tracks) {
      const source = ctx.createMediaStreamSource(new MediaStream([track]));
      const analyser = ctx.createAnalyser();
      analyser.fftSize = 8192;
      analyser.smoothingTimeConstant = 0;
      source.connect(analyser);
      await new Promise((r) => setTimeout(r, 600));
      const freq = new Float32Array(analyser.frequencyBinCount);
      analyser.getFloatFrequencyData(freq);
      const time = new Float32Array(analyser.fftSize);
      analyser.getFloatTimeDomainData(time);
      let peak = 1;
      for (let i = 2; i < freq.length; i++) if (freq[i] > freq[peak]) peak = i;
      let sum = 0;
      for (const v of time) sum += v * v;
      out.push({ hz: Math.round((peak * ctx.sampleRate) / analyser.fftSize), rms: Math.round(Math.sqrt(sum / time.length) * 1000) / 1000 });
      source.disconnect();
    }
    return out;
  });
}

const hearsTone = async (name, hz) => (await remoteTones(name)).some((t) => Math.abs(t.hz - hz) <= 20 && t.rms > 0.01);

async function inboundEnergy(name) {
  return browsers[name].page.evaluate(async () => {
    let energy = 0;
    for (const pc of window.__stagingPeerConnections) {
      if (pc.connectionState === 'closed') continue;
      const stats = await pc.getStats().catch(() => null);
      stats?.forEach((r) => {
        if (r.type === 'inbound-rtp' && r.kind === 'audio') energy += r.totalAudioEnergy ?? 0;
      });
    }
    return energy;
  });
}

const memberLine = (viewer, userId) => browsers[viewer].page.getByTestId('party-members').locator(`[data-user-id="${userId}"]`).getByTestId('member-voice-state');
async function memberVoiceText(viewer, userId) {
  const line = memberLine(viewer, userId);
  return (await line.count()) > 0 ? ((await line.first().textContent()) ?? '') : null;
}

// ==== 1. Baseline =================================================================================

log('\n1. Baseline (the deployed commit)');
phaseStart.baseline = new Date().toISOString();

await step('baseline stack: checkout of the deployed commit, https + voice profiles, all services healthy', async () => {
  git(['worktree', 'add', '-b', deployBranch, deployDir, baselineCommit]);
  writeEnv();
  execFileSync('docker', ['tag', images.baseline, 'akeno-network:latest']);
  compose(['up', '-d', '--no-build', '--wait', '--wait-timeout', '240'], { timeout: 300_000 });
  await waitForStack('the baseline');
  for (const service of ['db', 'app', 'caddy', 'livekit']) containers[service] = containerId(service);
  assert.equal(compose(['ps', '-q', 'voice-gateway'], { allowFail: true }).stdout.trim(), '', 'the baseline has no voice gateway');
  return { services: Object.keys(containers) };
}, { critical: true });

await step('baseline data: administrator, three accounts, friends, messages, a party with chat, a paired console', async () => {
  const created = compose(['exec', '-T', 'app', 'node', 'dist/cli.js', 'create-admin', '--online-id', 'st_admin', '--password-stdin'], { input: ADMIN_PASSWORD });
  assert.match(created.stdout, /created/i);
  await admin.signIn('st_admin', ADMIN_PASSWORD);
  for (const [name, display] of [['alice', 'Alice (PS5)'], ['bob', 'Bob (PC)'], ['carol', 'Carol (PC)']]) {
    const invitation = await admin.request('POST', '/admin/invitations', { maxUses: 1, expiresInHours: 1, note: 'voice staging' });
    assert.equal(invitation.status, 201, invitation.text);
    const a = new Api({ origin: ORIGIN, ca });
    const onlineId = `st_${name}`;
    const registered = await a.request('POST', '/auth/register', { onlineId, password: PASSWORD, invitationCode: invitation.body.code });
    assert.equal(registered.status, 201, registered.text);
    a.csrf = registered.body.csrfToken;
    a.user = registered.body.user;
    assert.equal((await a.request('PATCH', '/profile', { displayName: display })).status, 200);
    users[name] = { api: a, onlineId, id: a.user.id };
  }
  for (const [from, to] of [['alice', 'bob'], ['bob', 'carol'], ['alice', 'carol']]) {
    const request = await api(from).request('POST', '/friends/requests', { userId: users[to].id });
    assert.equal(request.status, 201, request.text);
    assert.equal((await api(to).request('POST', `/friends/requests/${request.body.request.id}/accept`)).status, 200);
  }
  const conversation = await api('alice').request('POST', '/conversations', { userId: users.bob.id });
  assert.ok([200, 201].includes(conversation.status), conversation.text);
  for (const [who, body] of [['alice', 'Hi Bob, testing the server upgrade.'], ['bob', 'Hi Alice!'], ['alice', 'Voice from the PS5 next.']]) {
    const sent = await api(who).request('POST', `/conversations/${conversation.body.id}/messages`, { body, clientMessageId: newClientMessageId() });
    assert.equal(sent.status, 201, sent.text);
  }
  // Bob leads the party (he can remove the console later); Alice and Carol join.
  const created2 = await api('bob').request('POST', '/parties', { name: 'Staging night', privacy: 'friends' });
  assert.equal(created2.status, 201, created2.text);
  party = created2.body;
  assert.equal(party.voice?.available, true, 'browser voice is on in the baseline');
  for (const who of ['alice', 'carol']) assert.equal((await api(who).request('POST', `/parties/${party.id}/join`)).status, 200);
  assert.equal((await api('bob').request('POST', `/parties/${party.id}/messages`, { body: 'Party chat before the upgrade', clientMessageId: newClientMessageId() })).status, 201);
  const allScopes = ['profile:read', 'presence:write', 'friends:read', 'friends:write', 'messages:read', 'messages:write', 'parties:read', 'parties:write',
    'sessions:read', 'sessions:write', 'invitations:read', 'invitations:write', 'realtime', 'activity:write'];
  consoleDevice = await pairConsole({ origin: ORIGIN, ca, owner: api('alice'), scopes: allScopes, name: 'PS5 (staging simulation)' });
  limitedDevice = await pairConsole({ origin: ORIGIN, ca, owner: api('alice'), scopes: allScopes.filter((s) => s !== 'parties:write'), name: 'PS5 without parties:write' });
  evidence.allScopes = allScopes;
  return { users: Object.fromEntries(Object.entries(users).map(([k, v]) => [k, v.id])), party: party.id };
}, { critical: true });

await step('baseline reproduces the production failure: POST /parties/:id/voice/device-token -> 404 NOT_FOUND (route not registered)', async () => {
  const r = await deviceToken(consoleDevice);
  assert.equal(r.status, 404, r.text);
  assert.equal(r.body?.error?.code, 'NOT_FOUND');
  assert.equal(r.body?.error?.message, 'The requested endpoint was not found.');
  // The same device is otherwise authorized: a known route with the same scope works.
  assert.equal((await consoleDevice.request('GET', '/parties/current')).status, 200);
  const browserPass = await api('bob').request('POST', `/parties/${party.id}/voice/token`);
  assert.equal(browserPass.status, 200, `browser voice works in the baseline: ${browserPass.text}`);
  evidence.baseline404 = r.body.error;
  return r.body.error;
});

await step('read-only diagnostics (vps-voice-diagnostics.sh) on the baseline: identifies the old server, prints no secrets', async () => {
  const out = diagnostics();
  assert.match(out, new RegExp(baselineCommit));
  assert.match(out, /console voice pass route: NOT in the source/);
  assert.match(out, /"code":"NOT_FOUND"/);
  assert.match(out, /LiveKit container: running/);
  assert.match(out, /LIVEKIT_API_SECRET=<set, hidden>/);
  assert.match(out, /POSTGRES_PASSWORD=<set, hidden>/);
  return { lines: out.split('\n').length };
});

await step('baseline snapshot of everything the users can see', async () => {
  snapshot = await snapshotAll();
  assert.equal(snapshot.alice.messages[snapshot.alice.conversations[0]].length, 3);
  assert.equal(snapshot.bob.party.members.length, 3);
  return { conversations: snapshot.alice.conversations.length, devices: snapshot.devices.length };
});

// ==== 2. Upgrade ==================================================================================

log('\n2. Upgrade (the documented production procedure)');
phaseStart.upgrade = new Date().toISOString();

await step('backup.sh writes a database dump and a media archive; the dump is readable by pg_restore', async () => {
  const backupDir = join(work, 'backups');
  const run = spawnSync('./infrastructure/scripts/backup.sh', [], {
    cwd: deployDir,
    encoding: 'utf8',
    env: { ...process.env, COMPOSE_PROJECT_NAME: PROJECT, COMPOSE_PROFILES: 'https,voice', BACKUP_DIR: backupDir },
  });
  assert.equal(run.status, 0, `${run.stdout}\n${run.stderr}`);
  const files = readdirSync(backupDir);
  const dump = files.find((f) => f.endsWith('.dump'));
  const media = files.find((f) => f.endsWith('.tar.gz'));
  assert.ok(dump && media, `backup files: ${files.join(', ')}`);
  assert.ok(statSync(join(backupDir, dump)).size > 10_000);
  const list = spawnSync('docker', ['compose', '-p', PROJECT, 'exec', '-T', 'db', 'pg_restore', '--list'], { cwd: deployDir, input: readFileSync(join(backupDir, dump)), encoding: 'utf8' });
  assert.equal(list.status, 0, list.stderr);
  assert.match(list.stdout, /TABLE DATA public messages/);
  assert.match(list.stdout, /TABLE DATA public device_credentials/);
  backup = { dump: join(backupDir, dump), media: join(backupDir, media) };
  return { dump, bytes: statSync(backup.dump).size, media };
}, { critical: true });

await step('rollback point, exactly as in the guide: image tagged akeno-network:pre-voice, .env.pre-voice, .pre-voice-commit', async () => {
  execFileSync('docker', ['tag', 'akeno-network:latest', 'akeno-network:pre-voice']);
  copyFileSync(join(deployDir, '.env'), join(deployDir, '.env.pre-voice'));
  writeFileSync(join(deployDir, '.pre-voice-commit'), `${git(['rev-parse', 'HEAD'], deployDir)}\n`);
  assert.doesNotMatch(readFileSync(join(deployDir, 'infrastructure/caddy/Caddyfile'), 'utf8'), /voice-gateway/);
});

await step('code update in the same checkout: git merge --ff-only of the reviewed commit (no VPS-only commits here)', async () => {
  git(['merge', '--ff-only', '--quiet', candidateCommit], deployDir);
  assert.equal(git(['rev-parse', 'HEAD'], deployDir), candidateCommit);
  assert.match(readFileSync(join(deployDir, 'infrastructure/caddy/Caddyfile'), 'utf8'), /handle \/voice-gateway/);
  // The website keeps running the old containers until `apply`.
  for (const service of ['db', 'app', 'caddy', 'livekit']) assert.equal(containerId(service), containers[service]);
});

await step('evidence: `caddy reload` keeps serving the old Caddyfile (the bind mount still holds the replaced file)', async () => {
  const beforeReload = await voiceGatewayRoute();
  compose(['exec', '-T', 'caddy', 'caddy', 'reload', '--config', '/etc/caddy/Caddyfile', '--adapter', 'caddyfile'], { allowFail: true });
  await sleep(1500);
  const afterReload = await voiceGatewayRoute();
  evidence.caddy = { beforeReload, afterReload };
  assert.equal(afterReload, 'app', 'a reload did not pick up the new route');
  return evidence.caddy;
});

await step('voice-upgrade.sh preflight passes (read-only)', async () => {
  const run = upgradeScript(['preflight']);
  assert.equal(run.status, 0, `${run.stdout}\n${run.stderr}`);
  assert.match(run.stdout, /voice pass route on the running server: HTTP 404/);
  return run.stdout.trim().split('\n').slice(-4);
});

await step('new images in place (on the VPS: voice-upgrade.sh build; here: the images built for the drill)', async () => {
  execFileSync('docker', ['tag', images.candidate, 'akeno-network:latest']);
  execFileSync('docker', ['tag', images.gateway, 'akeno-voice-gateway:latest']);
});

await step('voice-upgrade.sh apply: gateway, VOICE_GATEWAY_URL, app and Caddy recreated, verify passes; db and LiveKit untouched', async () => {
  const run = upgradeScript(['apply']);
  assert.equal(run.status, 0, `${run.stdout}\n${run.stderr}`);
  assert.match(run.stdout, /Verify passed/);
  assert.match(run.stdout, /voice pass route with an invalid device token: 401/);
  assert.match(run.stdout, /voice-gateway upgrade: 101/);
  assert.match(readFileSync(join(deployDir, '.env'), 'utf8'), new RegExp(`^VOICE_GATEWAY_URL=${GATEWAY_URL}$`, 'm'));
  await waitFor(readyOverTls, 'the upgraded app', 60_000, 1000);
  const image = execFileSync('docker', ['inspect', '--format', '{{.Image}}', containerId('app')], { encoding: 'utf8' }).trim();
  const candidateId = execFileSync('docker', ['image', 'inspect', '--format', '{{.Id}}', images.candidate], { encoding: 'utf8' }).trim();
  assert.equal(image, candidateId, 'the app runs the candidate image');
  for (const service of ['db', 'livekit']) assert.equal(containerId(service), containers[service], `${service} was not restarted`);
  evidence.caddy.afterApply = await voiceGatewayRoute();
  assert.equal(evidence.caddy.afterApply, 'gateway');
  assert.equal(await gatewaySessions(), 0);
  containers.app = containerId('app');
  return run.stdout.trim().split('\n').filter((l) => /^ {2}/.test(l)).slice(-8);
}, { critical: true });

await step('diagnostics after the upgrade: the route exists (401 for an invalid device token), the gateway answers, still no secrets', async () => {
  const out = diagnostics();
  assert.match(out, /console voice pass route: in the source/);
  assert.match(out, /"code":"UNAUTHENTICATED"/);
  assert.match(out, /voice gateway \/healthz: \{"ok":true/);
  assert.match(out, /^ {2}101$/m);
});

await step('after the upgrade: every account, friend list, conversation, message, party and device is unchanged', async () => {
  for (const name of Object.keys(users)) await users[name].api.signIn(users[name].onlineId, PASSWORD);
  const after = await snapshotAll();
  assert.deepEqual(after, snapshot);
  const current = await consoleDevice.request('GET', '/devices/current');
  assert.equal(current.status, 200, 'the console stays paired with its existing token');
});

// ==== 3. Console voice ============================================================================

log('\n3. Console voice through wss://<domain>/voice-gateway');
phaseStart.voice = new Date().toISOString();
let sim = null;

await step('voice pass for the paired console: 200, short-lived, room-bound, no account credentials inside', async () => {
  const r = await deviceToken(consoleDevice);
  assert.equal(r.status, 200, r.text);
  assert.equal(r.body.url, GATEWAY_URL);
  assert.equal(r.body.room, `akeno-party-${party.id}`);
  assert.equal(r.body.identity, users.alice.id);
  const ttl = (Date.parse(r.body.expiresAt) - Date.now()) / 1000;
  assert.ok(ttl > 100 && ttl <= 121, `expires in ${ttl} s`);
  const claims = decodeJwt(r.body.token);
  assert.equal(claims.sub, users.alice.id);
  assert.equal(claims.video.room, `akeno-party-${party.id}`);
  assert.equal(claims.video.roomJoin, true);
  assert.deepEqual(claims.video.canPublishSources, ['microphone']);
  assert.equal(claims.attributes['akeno.client'], 'console-gateway');
  assert.match(claims.attributes['akeno.grant'], /^v1\./);
  assert.ok(claims.exp - claims.nbf <= 121, 'lifetime at most VOICE_TOKEN_TTL_SECONDS');
  assert.ok(!r.text.includes(consoleDevice.bearer), 'the device token is never part of the pass');
  assert.ok(!JSON.stringify(claims).includes(consoleDevice.deviceId), 'the device ID is sealed, not readable');
  return { room: r.body.room, ttlSeconds: Math.round(ttl), attributes: Object.keys(claims.attributes) };
});

await step('refusals: no token 401, unknown token 401, website session 400, device without parties:write 403, other party 403', async () => {
  const anonymous = new Api({ origin: ORIGIN, ca });
  anonymous.bearer = 'akd_not-a-real-token-0000000000000000';
  const plain = new Api({ origin: ORIGIN, ca });
  const noAuth = await plain.request('POST', `/parties/${party.id}/voice/device-token`, undefined, {});
  assert.equal(noAuth.status, 401, noAuth.text);
  assert.equal((await deviceToken(anonymous)).status, 401);
  const web = await api('alice').request('POST', `/parties/${party.id}/voice/device-token`);
  assert.equal(web.status, 400, web.text);
  const limited = await deviceToken(limitedDevice);
  assert.equal(limited.status, 403, limited.text);
  assert.equal(limited.body.error.code, 'DEVICE_SCOPE_DENIED');
  const other = await deviceToken(consoleDevice, '00000000-0000-4000-8000-000000000000');
  assert.equal(other.status, 403, other.text);
  assert.equal(other.body.error.code, 'NOT_MEMBER');
  return { noAuth: noAuth.status, web: web.status, limited: limited.body.error.code, otherParty: other.body.error.code };
});

await step('the gateway refuses a browser pass, a forged pass and any request with an Origin header', async () => {
  const browserPass = (await api('bob').request('POST', `/parties/${party.id}/voice/token`)).body.token;
  const asBrowser = new ConsoleVoice({ url: GATEWAY_URL, token: browserPass, ca });
  await assert.rejects(asBrowser.connect(), (e) => e.code === 4001);
  const forged = new ConsoleVoice({ url: GATEWAY_URL, token: `${browserPass.split('.').slice(0, 2).join('.')}.forged-signature`, ca });
  await assert.rejects(forged.connect(), (e) => e.code === 4001);
  const fromPage = new ConsoleVoice({ url: GATEWAY_URL, token: browserPass, ca, headers: { Origin: ORIGIN } });
  await assert.rejects(fromPage.connect(), (e) => e.status === 403);
});

await step('browsers: Bob joins party voice on the website (Chromium, real WebRTC through Caddy and LiveKit)', async () => {
  const { chromium } = createRequire(resolve(repo, 'apps/web/package.json'))('@playwright/test');
  // The browser talks to the staging stack directly, never through a proxy from the environment.
  const directEnv = Object.fromEntries(Object.entries(process.env).filter(([k]) => !/^(https?|all)_proxy$/i.test(k)));
  browser = await chromium.launch({
    env: directEnv,
    args: ['--use-fake-ui-for-media-stream', '--use-fake-device-for-media-stream', `--use-file-for-fake-audio-capture=${speechLikeWav(600)}`,
      '--autoplay-policy=no-user-gesture-required', `--host-resolver-rules=MAP ${DOMAIN} 127.0.0.1`, '--no-proxy-server'],
  });
  await openBrowser('bob');
  await joinVoiceInBrowser('bob');
});

await step('the console joins through the gateway (TLS WebSocket via Caddy) into the website room; the server and Bob see it', async () => {
  sim = await newConsoleVoice();
  assert.equal(sim.ready.room, `akeno-party-${party.id}`);
  assert.equal(sim.ready.identity, users.alice.id);
  assert.equal(await gatewaySessions(), 1);
  await waitFor(async () => (await api('bob').request('GET', `/parties/${party.id}`)).body.members.find((m) => m.user.id === users.alice.id)?.voice?.connected, 'the server to report Alice in voice', 15_000, 500);
  await waitFor(async () => (await memberVoiceText('bob', users.alice.id)) !== null, "Alice's voice state on Bob's party page", 20_000, 500);
  await waitFor(() => sim.voice.participants().some((p) => p.id === users.bob.id && p.name === 'Bob (PC)'), 'Bob in the console participant list', 15_000);
  return { participants: sim.voice.participants().map((p) => p.name) };
});

await step('audio browser -> console: Bob\'s microphone (600 Hz) decoded on the console', async () => {
  const mark = Date.now();
  await waitFor(() => sim.voice.framesAt(600, mark) >= 25, 'at least 25 decoded 600 Hz frames on the console', 20_000);
  return { frames600: sim.voice.framesAt(600, mark), framesTotal: sim.voice.heard.filter((h) => h.at >= mark).length };
});

await step('audio console -> browser: the console microphone (300 Hz) analysed in Bob\'s browser', async () => {
  const tones = await waitFor(async () => {
    const t = await remoteTones('bob');
    return t.some((x) => Math.abs(x.hz - 300) <= 20 && x.rms > 0.01) ? t : null;
  }, 'a 300 Hz remote track in Bob\'s browser', 20_000, 500);
  return { tones, sent: sim.voice.sent };
});

await step('mute: the console stops publishing, Bob sees "muted" and hears nothing; unmute restores it', async () => {
  sim.voice.setMuted(true);
  await waitFor(async () => /muted/.test((await memberVoiceText('bob', users.alice.id)) ?? ''), 'Bob to see Alice muted', 15_000, 500);
  await waitFor(async () => !(await hearsTone('bob', 300)), 'no 300 Hz in Bob\'s browser', 10_000, 500);
  sim.voice.setMuted(false);
  await waitFor(async () => !/muted/.test((await memberVoiceText('bob', users.alice.id)) ?? 'muted'), 'Bob to see Alice unmuted', 15_000, 500);
  await waitFor(() => hearsTone('bob', 300), '300 Hz again in Bob\'s browser', 15_000, 500);
});

await step('deafen: the gateway sends the console nothing; undeafen brings Bob back', async () => {
  sim.voice.setDeafened(true);
  await sleep(1000);
  const mark = Date.now();
  await sleep(2500);
  assert.equal(sim.voice.heard.filter((h) => h.at >= mark).length, 0, 'no audio frames while deafened');
  sim.voice.setDeafened(false);
  const again = Date.now();
  await waitFor(() => sim.voice.framesAt(600, again) >= 15, '600 Hz on the console after undeafen', 15_000);
});

await step('browser voice unchanged: Carol joins from a second browser; Bob and Carol hear each other, the console hears them', async () => {
  await openBrowser('carol');
  const before = await inboundEnergy('bob');
  await joinVoiceInBrowser('carol');
  await waitFor(async () => (await inboundEnergy('carol')) > 0, 'audio energy in Carol\'s browser', 30_000, 500);
  await waitFor(async () => (await inboundEnergy('bob')) > before, 'more audio energy in Bob\'s browser', 30_000, 500);
  await waitFor(() => hearsTone('carol', 300), 'the console (300 Hz) in Carol\'s browser', 20_000, 500);
  const mark = Date.now();
  await waitFor(() => sim.voice.framesAt(600, mark) >= 15, 'the console still hears the browsers', 15_000);
  await waitFor(() => sim.voice.participants().length === 3, 'three people in the console participant list', 15_000);
});

await step('duplicate session: a second console connection with a fresh pass replaces the first (4004)', async () => {
  const second = await newConsoleVoice();
  const closed = await sim.voice.closed;
  assert.equal(closed.code, 4004, JSON.stringify(closed));
  sim = second;
  assert.equal(await gatewaySessions(), 1);
  await waitFor(() => hearsTone('bob', 300), 'Bob hears the replacing connection', 20_000, 500);
});

await step('network drop: the socket disappears; a fresh pass reconnects and audio flows both ways again', async () => {
  sim.voice.drop();
  await waitFor(async () => (await gatewaySessions()) === 0, 'the gateway to end the dropped session', 15_000, 500);
  sim = await newConsoleVoice();
  const mark = Date.now();
  await waitFor(() => sim.voice.framesAt(600, mark) >= 15, 'the console hears Bob after reconnecting', 20_000);
  await waitFor(() => hearsTone('bob', 300), 'Bob hears the console after reconnecting', 20_000, 500);
});

await step('revocation 1: Bob removes Alice from the party; her console voice ends at once (4003); no new pass', async () => {
  const removed = await api('bob').request('DELETE', `/parties/${party.id}/members/${users.alice.id}`);
  assert.equal(removed.status, 204, removed.text);
  const closed = await Promise.race([sim.voice.closed, sleep(10_000).then(() => ({ code: 'still open after 10 s' }))]);
  assert.equal(closed.code, 4003, JSON.stringify(closed));
  const refused = await deviceToken(consoleDevice);
  assert.equal(refused.status, 403);
  assert.equal(refused.body.error.code, 'NOT_MEMBER');
  await waitFor(async () => (await memberVoiceText('bob', users.alice.id)) === null, 'Alice gone from Bob\'s party page', 15_000, 500);
  assert.equal((await api('alice').request('POST', `/parties/${party.id}/join`)).status, 200);
  sim = await newConsoleVoice();
  return { closed: closed.code, afterRemoval: refused.body.error.code };
});

await step('revocation 2: "parties:write" removed on the website; voice ends (4003); restored, a new pass works', async () => {
  const devices = (await api('alice').request('GET', '/devices')).body.devices;
  const entry = devices.find((d) => d.id === consoleDevice.deviceId);
  const without = entry.scopes.filter((s) => s !== 'parties:write');
  assert.equal((await api('alice').request('PATCH', `/devices/${consoleDevice.deviceId}`, { scopes: without })).status, 200);
  const closed = await Promise.race([sim.voice.closed, sleep(10_000).then(() => ({ code: 'still open after 10 s' }))]);
  assert.equal(closed.code, 4003, JSON.stringify(closed));
  const refused = await deviceToken(consoleDevice);
  assert.equal(refused.status, 403);
  assert.equal(refused.body.error.code, 'DEVICE_SCOPE_DENIED');
  assert.equal((await api('alice').request('PATCH', `/devices/${consoleDevice.deviceId}`, { scopes: entry.scopes })).status, 200);
  sim = await newConsoleVoice();
  return { closed: closed.code, withoutScope: refused.body.error.code };
});

// Optional: the PS5 voice-alpha's own code against the same stack (host builds from PR #18).
if (consoleBinaries.daemon && consoleBinaries.voice && consoleBinaries.companion) {
  await step('PS5 voice-alpha code (PR #18 host builds): daemon pairs over TLS, akeno-party-voice joins and is heard both ways', async () => {
    sim.voice.drop(); // Alice's simulated console leaves; her "real" console code takes over
    await waitFor(async () => (await gatewaySessions()) === 0, 'the simulated session to end', 15_000, 500);
    const out = await runConsoleStack();
    return out;
  });
} else {
  log('  skip PS5 voice-alpha code against staging (pass --console-daemon, --console-voice and --console-companion)');
}

await step('revocation 3: unpairing the console ends its voice (4003); its token is refused (401)', async () => {
  if (!sim || sim.voice.ws.readyState !== 1) sim = await newConsoleVoice();
  assert.equal((await api('alice').request('DELETE', `/devices/${consoleDevice.deviceId}`)).status, 204);
  const closed = await Promise.race([sim.voice.closed, sleep(10_000).then(() => ({ code: 'still open after 10 s' }))]);
  assert.equal(closed.code, 4003, JSON.stringify(closed));
  assert.equal((await deviceToken(consoleDevice)).status, 401);
  await waitFor(async () => (await gatewaySessions()) === 0, 'no console session left on the gateway', 15_000, 500);
});

await step('logs: no server errors; the gateway logs sessions without tokens; resource use', async () => {
  const appLog = compose(['logs', '--no-color', '--since', phaseStart.upgrade, 'app']).stdout;
  const errors = appLog.split('\n').filter((line) => /"level":(50|60)/.test(line));
  assert.deepEqual(errors, [], 'no error-level server log lines');
  const gatewayLog = compose(['logs', '--no-color', 'voice-gateway']).stdout;
  assert.match(gatewayLog, /console voice session live/);
  assert.match(gatewayLog, /console voice session ended/);
  assert.doesNotMatch(gatewayLog, /eyJ[A-Za-z0-9_-]{10,}\./, 'no voice pass in the gateway log');
  const stats = execFileSync('docker', ['stats', '--no-stream', '--format', '{{.Name}} {{.CPUPerc}} {{.MemUsage}}', ...['app', 'livekit', 'voice-gateway', 'db', 'caddy'].map(containerId)], { encoding: 'utf8' });
  evidence.resources = stats.trim().split('\n');
  for (const page of Object.values(browsers)) assert.deepEqual(page.errors, [], 'no page errors on the website');
  return evidence.resources;
});

for (const name of Object.keys(browsers)) {
  await browsers[name].context.close().catch(() => undefined);
  delete browsers[name];
}

// ==== 4. Rollback =================================================================================

log('\n4. Rollback drill');

await step('voice-upgrade.sh rollback 1: VOICE_GATEWAY_URL emptied, app recreated -> consoles get 503, browsers keep voice', async () => {
  const run = upgradeScript(['rollback', '1']);
  assert.equal(run.status, 0, `${run.stdout}\n${run.stderr}`);
  await waitFor(readyOverTls, 'the app', 60_000, 1000);
  assert.match(readFileSync(join(deployDir, '.env'), 'utf8'), /^VOICE_GATEWAY_URL=$/m);
  const fresh = await pairConsole({ origin: ORIGIN, ca, owner: api('alice'), scopes: evidence.allScopes, name: 'PS5 after rollback level 1' });
  const pass = await deviceToken(fresh);
  assert.equal(pass.status, 503, pass.text);
  assert.equal(pass.body.error.code, 'VOICE_GATEWAY_UNAVAILABLE');
  assert.equal((await api('bob').request('POST', `/parties/${party.id}/voice/token`)).status, 200);
  for (const service of ['db', 'livekit']) assert.equal(containerId(service), containers[service]);
  consoleDevice = fresh;
  return pass.body.error.code;
});

await step('voice-upgrade.sh rollback 2: voice gateway stopped; nothing else changes', async () => {
  const run = upgradeScript(['rollback', '2']);
  assert.equal(run.status, 0, `${run.stdout}\n${run.stderr}`);
  const gateway = compose(['ps', '-aq', 'voice-gateway']).stdout.trim();
  assert.ok(gateway, 'the stopped gateway container still exists');
  assert.equal(execFileSync('docker', ['inspect', '--format', '{{.State.Running}}', gateway], { encoding: 'utf8' }).trim(), 'false');
  for (const service of ['db', 'livekit']) assert.equal(containerId(service), containers[service]);
  assert.ok(await readyOverTls());
  assert.equal((await api('bob').request('POST', `/parties/${party.id}/voice/token`)).status, 200);
});

await step('voice-upgrade.sh rollback 3: previous commit, image, .env, Caddyfile; data written after the upgrade is kept', async () => {
  const marker = await api('alice').request('POST', `/conversations/${snapshot.alice.conversations[0]}/messages`, { body: 'Written on the upgraded server', clientMessageId: newClientMessageId() });
  assert.equal(marker.status, 201);
  const run = upgradeScript(['rollback', '3']);
  assert.equal(run.status, 0, `${run.stdout}\n${run.stderr}`);
  assert.equal(git(['rev-parse', 'HEAD'], deployDir), baselineCommit);
  assert.equal(git(['rev-parse', deployBranch], deployDir), candidateCommit, 'no branch was moved back or rewritten');
  assert.equal(readFileSync(join(deployDir, '.env'), 'utf8'), readFileSync(join(deployDir, '.env.pre-voice'), 'utf8'));
  await waitFor(readyOverTls, 'the rolled-back stack', 90_000, 1000);
  const image = execFileSync('docker', ['inspect', '--format', '{{.Image}}', containerId('app')], { encoding: 'utf8' }).trim();
  assert.equal(image, execFileSync('docker', ['image', 'inspect', '--format', '{{.Id}}', images.baseline], { encoding: 'utf8' }).trim(), 'the app runs the previous image');
  assert.equal(compose(['ps', '-aq', 'voice-gateway'], { allowFail: true }).stdout.trim(), '', 'the gateway container is removed');
  for (const name of Object.keys(users)) await users[name].api.signIn(users[name].onlineId, PASSWORD);
  const messages = (await api('bob').request('GET', `/conversations/${snapshot.alice.conversations[0]}/messages`)).body.messages.map((m) => m.body);
  assert.ok(messages.includes('Written on the upgraded server'));
  const r = await deviceToken(consoleDevice);
  assert.equal(r.status, 404, 'the device route is gone again');
  assert.equal((await api('bob').request('POST', `/parties/${party.id}/voice/token`)).status, 200, 'browser voice works on the rolled-back server');
  assert.equal(await voiceGatewayRoute(), 'app', 'the old Caddyfile has no gateway route');
  assert.equal(containerId('db'), containers.db, 'the database container ran throughout');
  assert.equal(containerId('livekit'), containers.livekit, 'LiveKit ran throughout');
  return { device: r.status, head: baselineCommit.slice(0, 12) };
});

await step('restore drill: restore.sh brings back exactly the pre-upgrade data from the step-2 backup', async () => {
  const run = spawnSync('./infrastructure/scripts/restore.sh', [backup.dump, backup.media, '--yes'], {
    cwd: deployDir,
    encoding: 'utf8',
    env: { ...process.env, COMPOSE_PROJECT_NAME: PROJECT, COMPOSE_PROFILES: 'https,voice' },
  });
  assert.equal(run.status, 0, `${run.stdout}\n${run.stderr}`);
  await waitFor(readyOverTls, 'the app after the restore', 120_000, 1000);
  for (const name of Object.keys(users)) await users[name].api.signIn(users[name].onlineId, PASSWORD);
  const restored = await snapshotAll();
  assert.deepEqual(restored, snapshot);
});

// ---- The PS5 voice-alpha's own code ---------------------------------------------------------------

async function runConsoleStack() {
  const children = [];
  const keep = (text, chunk) => (text + chunk).slice(-100_000);
  const akenoDir = join(work, 'console-akeno');
  mkdirSync(akenoDir, { mode: 0o700 });
  const caFile = join(work, 'staging-ca.pem');
  // As on the console: the voice service trusts the CA file in the daemon's data directory.
  copyFileSync(caFile, join(akenoDir, 'ca.pem'));
  const local = (port, method, path, headers = {}, body) =>
    new Promise((done, fail) => {
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
        const text = data.slice(data.indexOf('\r\n\r\n') + 4);
        let json;
        try {
          json = JSON.parse(text);
        } catch {
          json = null;
        }
        done({ status: Number(/^HTTP\/1\.1 (\d+)/.exec(data)?.[1] ?? 0), body: json, text });
      });
      socket.on('error', fail);
    });
  const daemon = { port: 0, token: '', out: '' };
  let lastStatus = null;
  const daemonChild = spawn(resolve(consoleBinaries.daemon), ['--data-dir', akenoDir, '--port', '0', '--server', ORIGIN, '--ca-file', caFile], { stdio: ['ignore', 'pipe', 'pipe'] });
  children.push(daemonChild);
  daemonChild.stderr.on('data', (c) => (daemon.out = keep(daemon.out, c)));
  daemonChild.stdout.on('data', (c) => {
    daemon.out = keep(daemon.out, c);
    const m = /"url":"http:\/\/127\.0\.0\.1:(\d+)\/#t=([0-9a-f]{64})"/.exec(daemon.out);
    if (m && !daemon.port) [daemon.port, daemon.token] = [Number(m[1]), m[2]];
  });
  try {
    await waitFor(() => daemon.port, 'the daemon to start', 20_000);
    await local(daemon.port, 'POST', '/api/pair/start', { 'X-Akeno-Token': daemon.token });
    const pairing = await waitFor(async () => (await local(daemon.port, 'GET', '/api/state', { 'X-Akeno-Token': daemon.token })).body?.pairing?.userCode, 'a pairing code', 20_000);
    assert.equal((await api('alice').request('POST', '/devices/pairings/approve', { userCode: pairing })).status, 204);
    await waitFor(async () => {
      const s = (await local(daemon.port, 'GET', '/api/state', { 'X-Akeno-Token': daemon.token })).body;
      return s?.paired && s.realtime?.state === 'connected';
    }, 'the daemon to pair over TLS and connect its realtime channel', 40_000, 500);
    const voice = { port: 0, out: '' };
    const voiceChild = spawn(resolve(consoleBinaries.voice), ['--akeno-dir', akenoDir, '--data-dir', join(work, 'console-voice'), '--daemon-port', String(daemon.port), '--port', '0'], {
      env: { ...process.env, PV_SIM_MIC: 'tone:300' },
      stdio: ['ignore', 'pipe', 'pipe'],
    });
    children.push(voiceChild);
    voiceChild.stderr.on('data', (c) => (voice.out = keep(voice.out, c)));
    voiceChild.stdout.on('data', (c) => {
      voice.out = keep(voice.out, c);
      const m = /AKENO_PARTY_VOICE_READY port=(\d+)/.exec(voice.out);
      if (m && !voice.port) voice.port = Number(m[1]);
    });
    await waitFor(() => voice.port, 'akeno-party-voice to start', 20_000);
    const key = () => readFileSync(join(akenoDir, 'native.key'), 'utf8');
    const status = async () => (lastStatus = (await local(voice.port, 'GET', '/api/voice/status', { 'X-Akeno-Native-Key': key() })).body);
    const press = (event) =>
      new Promise((done) => {
        const child = spawn(resolve(consoleBinaries.companion), ['--port', String(daemon.port), '--akeno-dir', akenoDir, '--voice-port', String(voice.port), '--event', event], { stdio: ['ignore', 'pipe', 'pipe'] });
        let out = '';
        child.stdout.on('data', (d) => (out += d));
        child.stderr.on('data', (d) => (out += d));
        child.on('exit', (code) => done({ code, out }));
      });
    const joined = await press(`vjoin.${party.id}`);
    assert.equal(joined.code, 0, joined.out);
    await waitFor(async () => (await status())?.network?.state === 'connected', 'akeno-party-voice to connect through Caddy to the gateway', 30_000, 500);
    const s1 = await waitFor(async () => {
      const v = await status();
      return Math.abs(v.stats.rxToneHz - 600) <= 15 && v.stats.decodedWithSound > 50 ? v : null;
    }, 'the voice service to decode Bob (600 Hz)', 30_000, 500);
    const mic = await press('vmicon');
    assert.equal(mic.code, 0, mic.out);
    await waitFor(() => hearsTone('bob', 300), 'Bob to hear akeno-party-voice (300 Hz)', 30_000, 500);
    const s2 = await status();
    assert.equal(s2.evidence.networkConnected, true);
    assert.equal(s2.evidence.micTransmitting, true);
    await press('vleave');
    return { daemonIpc: s1.network.daemonIpc, rxToneHz: s1.stats.rxToneHz, txToneHz: s2.stats.txToneHz, decodedWithSound: s1.stats.decodedWithSound, summary: s2.summary };
  } catch (error) {
    throw new Error(`${error.message}\n--- voice status ---\n${JSON.stringify(lastStatus)?.slice(0, 3000)}\n--- daemon ---\n${daemon.out.slice(-2000)}`, { cause: error });
  } finally {
    for (const child of children) child.kill('SIGTERM');
  }
}

// ---- Report and cleanup ---------------------------------------------------------------------------

async function finish() {
  for (const b of Object.values(browsers)) await b.context.close().catch(() => undefined);
  await browser?.close().catch(() => undefined);
  if (reportFile) writeFileSync(reportFile, `${JSON.stringify({ evidence, results, failures }, null, 2)}\n`);
  if (args.includes('--keep')) {
    log(`\nThe staging stack keeps running (docker compose -p ${PROJECT} in ${deployDir}); CA: ${join(work, 'staging-ca.pem')}`);
  } else {
    if (existsSync(deployDir)) compose(['down', '-v', '--remove-orphans'], { allowFail: true });
    spawnSync('git', ['worktree', 'remove', '--force', deployDir], { cwd: repo });
    spawnSync('git', ['branch', '-D', deployBranch], { cwd: repo, stdio: 'ignore' });
    spawnSync('docker', ['image', 'rm', 'akeno-network:pre-voice'], { stdio: 'ignore' });
    if (existsSync(work)) rmSync(work, { recursive: true, force: true });
  }
  log(failures === 0 ? '\nAll voice staging checks passed.' : `\n${failures} voice staging check(s) FAILED.`);
  process.exit(failures === 0 ? 0 : 1);
}

await finish();
