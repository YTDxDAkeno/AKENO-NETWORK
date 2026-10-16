#!/usr/bin/env node
// Measures SharePlay resource use against a real LiveKit server: one broadcaster and N viewers
// (Chromium), reporting the broadcaster's upload, each viewer's download, received resolution and
// frame rate, and the LiveKit process's CPU and memory.
//
// TEST-GENERATED VIDEO: the broadcaster's screen capture is replaced by a canvas of random
// coloured blocks redrawn every frame (high entropy), so the encoder runs at its bitrate cap.
// It is a worst case for bandwidth, NOT a PS5 game and not a latency measurement.
//
// Usage (after `pnpm build`, with PostgreSQL and LiveKit running as in docs/TESTING.md):
//   E2E_LIVEKIT_URL=ws://127.0.0.1:7880 E2E_LIVEKIT_API_KEY=… E2E_LIVEKIT_API_SECRET=… \
//   LIVEKIT_PID=$(pgrep -f livekit-server) node infrastructure/scripts/shareplay-measure.mjs \
//     [--viewers 3] [--quality 720p30] [--seconds 20]
// The LiveKit webhook must point at http://127.0.0.1:3200/api/v1/voice/webhook (E2E_PORT).
import { spawn } from 'node:child_process';
import { readFileSync } from 'node:fs';
import { createRequire } from 'node:module';
import { dirname, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';

const root = resolve(dirname(fileURLToPath(import.meta.url)), '../..');
const { chromium } = createRequire(resolve(root, 'apps/web/package.json'))('@playwright/test');
const arg = (name, fallback) => {
  const index = process.argv.indexOf(`--${name}`);
  return index > 0 ? process.argv[index + 1] : fallback;
};
const VIEWERS = Number(arg('viewers', '3'));
const QUALITY = arg('quality', '720p30');
const SECONDS = Number(arg('seconds', '20'));
const PORT = process.env.E2E_PORT ?? '3200';
const BASE = `http://localhost:${PORT}`;
const ADMIN_PASSWORD = process.env.E2E_ADMIN_PASSWORD ?? 'e2e-admin-password-123';
const PASSWORD = 'correct-horse-battery-staple';
const sleep = (ms) => new Promise((done) => setTimeout(done, ms));

const server = spawn('node', ['infrastructure/scripts/e2e-server.mjs'], { cwd: root, env: { ...process.env, E2E_PORT: PORT, E2E_DATABASE_NAME: 'akeno_e2e_measure' }, stdio: 'ignore' });
process.on('exit', () => server.kill('SIGTERM'));
for (let i = 0; ; i += 1) {
  try {
    if ((await fetch(`${BASE}/health/ready`)).ok) break;
  } catch {
    // starting
  }
  if (i > 200) throw new Error('server did not start');
  await sleep(250);
}

async function api(session, method, path, body) {
  const headers = { Origin: BASE, 'Content-Type': 'application/json' };
  if (session.cookie) headers.Cookie = session.cookie;
  if (session.csrf) headers['X-CSRF-Token'] = session.csrf;
  const response = await fetch(`${BASE}/api/v1${path}`, { method, headers, body: body === undefined ? undefined : JSON.stringify(body) });
  const cookie = response.headers.get('set-cookie');
  if (cookie) session.cookie = cookie.split(';')[0];
  const text = await response.text();
  return { status: response.status, json: text ? JSON.parse(text) : null };
}

const admin = {};
const login = await api(admin, 'POST', '/auth/login', { onlineId: 'e2e_admin', password: ADMIN_PASSWORD });
admin.csrf = login.json.csrfToken;
const users = [];
for (let i = 0; i <= VIEWERS; i += 1) {
  const code = (await api(admin, 'POST', '/admin/invitations', { maxUses: 1, expiresInHours: 1, note: 'measure' })).json.code;
  const session = {};
  const registered = await api(session, 'POST', '/auth/register', { onlineId: `measure${i}`, password: PASSWORD, invitationCode: code });
  if (registered.status !== 201) throw new Error(JSON.stringify(registered.json));
  session.csrf = registered.json.csrfToken;
  session.id = registered.json.user.id;
  users.push(session);
}
const [host, ...viewers] = users;
for (const viewer of viewers) {
  const sent = await api(host, 'POST', '/friends/requests', { userId: viewer.id });
  await api(viewer, 'POST', `/friends/requests/${sent.json.request.id}/accept`);
}
const party = (await api(host, 'POST', '/parties', { name: 'Measure', privacy: 'friends', maxMembers: 16 })).json;
for (const viewer of viewers) await api(viewer, 'POST', `/parties/${party.id}/join`);

function instrument() {
  window.__pcs = [];
  const Native = window.RTCPeerConnection;
  window.RTCPeerConnection = class extends Native {
    constructor(...args) {
      super(...args);
      window.__pcs.push(this);
    }
  };
  MediaDevices.prototype.getDisplayMedia = async function () {
    const canvas = document.createElement('canvas');
    canvas.width = 1280;
    canvas.height = 720;
    const g = canvas.getContext('2d');
    setInterval(() => {
      for (let i = 0; i < 120; i += 1) {
        g.fillStyle = `hsl(${Math.random() * 360},80%,${30 + Math.random() * 40}%)`;
        g.fillRect(Math.random() * 1280, Math.random() * 720, 20 + Math.random() * 80, 20 + Math.random() * 80);
      }
    }, 33);
    const stream = canvas.captureStream(60);
    const ctx = new AudioContext();
    const osc = ctx.createOscillator();
    const dest = ctx.createMediaStreamDestination();
    osc.connect(dest);
    osc.start();
    stream.addTrack(dest.stream.getAudioTracks()[0]);
    return stream;
  };
}

const browser = await chromium.launch({ args: ['--use-fake-ui-for-media-stream', '--autoplay-policy=no-user-gesture-required'] });
async function openAs(session) {
  // Media permissions make Chromium offer real (not mDNS) local addresses, which a loopback LiveKit needs.
  const context = await browser.newContext({ viewport: { width: 1366, height: 900 }, permissions: ['microphone', 'camera'] });
  await context.addCookies([{ name: session.cookie.split('=')[0], value: session.cookie.split('=').slice(1).join('='), url: BASE }]);
  await context.addInitScript(instrument);
  const page = await context.newPage();
  page.on('console', (message) => process.env.MEASURE_VERBOSE && console.error(`[${session.id.slice(0, 4)}] ${message.text().slice(0, 200)}`));
  await page.goto(`${BASE}/parties`);
  return page;
}
const hostPage = await openAs(host);
await hostPage.getByTestId('shareplay-quality').selectOption(QUALITY);
await hostPage.getByTestId('shareplay-start-button').click();
await hostPage.getByRole('button', { name: 'Choose what to share' }).click();
await hostPage.getByTestId('shareplay-capture-indicator').waitFor({ timeout: 30_000 });
await hostPage.locator('[data-shareplay="live"]').waitFor({ timeout: 30_000 });
const viewerPages = [];
for (const viewer of viewers) {
  const page = await openAs(viewer);
  await page.getByTestId('shareplay-watch').click();
  viewerPages.push(page);
}
for (const page of viewerPages) await page.locator('[data-stream-status="live"]').waitFor({ timeout: 30_000 });
await sleep(5000); // let congestion control settle

const bytes = (page, direction) =>
  page.evaluate(async (direction) => {
    let total = 0;
    let width = 0;
    let fps = 0;
    let limitation = null;
    for (const pc of window.__pcs) {
      if (pc.connectionState === 'closed') continue;
      (await pc.getStats()).forEach((r) => {
        if (r.type === (direction === 'out' ? 'outbound-rtp' : 'inbound-rtp')) total += direction === 'out' ? r.bytesSent ?? 0 : r.bytesReceived ?? 0;
        if (direction === 'out' && r.type === 'outbound-rtp' && r.kind === 'video' && (r.frameWidth ?? 0) >= width) {
          width = r.frameWidth ?? width;
          fps = r.framesPerSecond ?? fps;
          limitation = r.qualityLimitationReason ?? null;
        }
        if (direction === 'in' && r.type === 'inbound-rtp' && r.kind === 'video') {
          width = r.frameWidth ?? width;
          fps = r.framesPerSecond ?? fps;
        }
      });
    }
    return { total, width, fps, limitation };
  }, direction);

const pid = process.env.LIVEKIT_PID ? Number(process.env.LIVEKIT_PID) : null;
const cpuTicks = () => {
  if (!pid) return null;
  const fields = readFileSync(`/proc/${pid}/stat`, 'utf8').split(') ')[1].split(' ');
  return Number(fields[11]) + Number(fields[12]);
};
const rssMb = () => (pid ? Number(/VmRSS:\s+(\d+)/.exec(readFileSync(`/proc/${pid}/status`, 'utf8'))[1]) / 1024 : null);

const hostBefore = await bytes(hostPage, 'out');
const viewersBefore = await Promise.all(viewerPages.map((page) => bytes(page, 'in')));
const ticksBefore = cpuTicks();
const started = Date.now();
await sleep(SECONDS * 1000);
const elapsed = (Date.now() - started) / 1000;
const hostAfter = await bytes(hostPage, 'out');
const viewersAfter = await Promise.all(viewerPages.map((page) => bytes(page, 'in')));
const ticksAfter = cpuTicks();
const kbps = (delta) => Math.round((delta * 8) / elapsed / 1000);

const result = {
  date: new Date().toISOString(),
  note: 'TEST-GENERATED high-entropy video (worst case for bandwidth), not PS5 gameplay; single machine, loopback network.',
  quality: QUALITY,
  viewers: VIEWERS,
  seconds: Math.round(elapsed),
  hostUploadKbps: kbps(hostAfter.total - hostBefore.total),
  hostEncoder: { width: hostAfter.width, fps: hostAfter.fps, qualityLimitationReason: hostAfter.limitation },
  viewerDownloadKbps: viewersAfter.map((after, i) => kbps(after.total - viewersBefore[i].total)),
  viewerReceived: viewersAfter.map((after) => ({ width: after.width, fps: after.fps })),
  livekitCpuPercentOfOneCore: ticksBefore === null ? null : Math.round(((ticksAfter - ticksBefore) / 100 / elapsed) * 1000) / 10,
  livekitRssMb: rssMb() === null ? null : Math.round(rssMb()),
};
result.serverInKbps = result.hostUploadKbps;
result.serverOutKbps = result.viewerDownloadKbps.reduce((sum, value) => sum + value, 0);
console.log(JSON.stringify(result, null, 2));
await hostPage.getByTestId('shareplay-stop').click().catch(() => {});
await browser.close();
server.kill('SIGTERM');
process.exit(0);
