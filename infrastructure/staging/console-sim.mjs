// A simulated PS5 for the voice staging drill (voice-staging.mjs): pairs like a console, asks the
// AKENO server for a console voice pass with its device token and talks to the voice gateway with
// protocol 1 (apps/voice-gateway/src/protocol.ts) over TLS: a 300 Hz tone as its microphone, and
// every frame it receives is Opus-decoded and measured. Nothing here is PS5 evidence; it proves
// the server, Caddy, the gateway and LiveKit carry real audio both ways.
import { randomUUID } from 'node:crypto';
import https from 'node:https';
import { createRequire } from 'node:module';
import { dirname, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';

const root = resolve(dirname(fileURLToPath(import.meta.url)), '../..');
const gatewayRequire = createRequire(resolve(root, 'apps/voice-gateway/package.json'));
const { WebSocket } = gatewayRequire('ws');
const OpusScript = gatewayRequire('opusscript');

export const RATE = 48_000;
export const FRAME = 960; // 20 ms mono
const AUDIO_TAG = 0x01;

/** Single-tone frequency of a block by zero crossings (0 when quiet), as in the gateway tests. */
export function toneHz(pcm, rate = RATE) {
  let energy = 0;
  for (const v of pcm) energy += v * v;
  if (pcm.length < 64 || Math.sqrt(energy / pcm.length) < 300) return 0;
  let crossings = 0;
  let first = -1;
  let last = -1;
  for (let i = 1; i < pcm.length; i++) {
    if (pcm[i - 1] < 0 !== pcm[i] < 0) {
      if (first < 0) first = i;
      last = i;
      crossings++;
    }
  }
  return crossings < 3 ? 0 : Math.round(rate / ((2 * (last - first)) / (crossings - 1)));
}

/**
 * HTTPS JSON requests to the staging server, trusting only the staging CA (Caddy's local CA).
 * A web session keeps its cookie and CSRF token; a device sends its bearer token.
 */
export class Api {
  constructor({ origin, ca }) {
    this.origin = origin;
    this.ca = ca;
    this.cookie = null;
    this.csrf = null;
    this.bearer = null;
    this.user = null;
  }

  request(method, path, body, extraHeaders = {}) {
    // "/friends" means the API (/api/v1/friends); "/health/ready" and full URLs are taken as they are.
    const url = new URL(/^(https:|\/api\/|\/health)/.test(path) ? path : `/api/v1${path}`, this.origin);
    const payload = body === undefined ? null : JSON.stringify(body);
    const headers = { Accept: 'application/json', ...extraHeaders };
    if (payload !== null) headers['Content-Type'] = 'application/json';
    if (this.bearer) headers.Authorization = `Bearer ${this.bearer}`;
    else {
      headers.Origin = this.origin;
      if (this.cookie) headers.Cookie = this.cookie;
      if (this.csrf && method !== 'GET') headers['X-CSRF-Token'] = this.csrf;
    }
    return new Promise((done, fail) => {
      const req = https.request(url, { method, headers, ca: this.ca, timeout: 20_000 }, (res) => {
        let text = '';
        res.setEncoding('utf8');
        res.on('data', (chunk) => (text += chunk));
        res.on('end', () => {
          const setCookie = res.headers['set-cookie'];
          if (setCookie?.length && !this.bearer) this.cookie = setCookie.map((c) => c.split(';')[0]).join('; ');
          let json;
          try {
            json = text ? JSON.parse(text) : null;
          } catch {
            json = null;
          }
          done({ status: res.statusCode ?? 0, body: json, text });
        });
      });
      req.on('timeout', () => req.destroy(new Error(`${method} ${path} timed out`)));
      req.on('error', fail);
      if (payload !== null) req.write(payload);
      req.end();
    });
  }

  async signIn(onlineId, password) {
    const r = await this.request('POST', '/auth/login', { onlineId, password });
    if (r.status !== 200) throw new Error(`sign-in of ${onlineId}: HTTP ${r.status} ${r.text}`);
    this.csrf = r.body.csrfToken;
    this.user = r.body.user;
    return this;
  }
}

/**
 * Pairs a simulated console for `owner` (a signed-in web session) through the real pairing flow
 * (RFC 8628 style: request, approval on the website, polling) and returns its device API client.
 */
export async function pairConsole({ origin, ca, owner, scopes, name = 'PS5 (staging simulation)' }) {
  const device = new Api({ origin, ca });
  const start = await device.request('POST', '/devices/pairings', { deviceName: name, platform: 'PlayStation 5', clientVersion: 'voice-staging', scopes });
  if (start.status !== 201) throw new Error(`pairing request: HTTP ${start.status} ${start.text}`);
  const approve = await owner.request('POST', '/devices/pairings/approve', { userCode: start.body.userCode, scopes });
  if (approve.status !== 204) throw new Error(`pairing approval: HTTP ${approve.status} ${approve.text}`);
  const deadline = Date.now() + 60_000;
  let interval = (start.body.interval ?? 5) * 1000;
  while (Date.now() < deadline) {
    await new Promise((r) => setTimeout(r, interval));
    const token = await device.request('POST', '/devices/pairings/token', { deviceCode: start.body.deviceCode });
    if (token.status === 200) {
      device.bearer = token.body.accessToken;
      const me = await device.request('GET', '/devices/current');
      if (me.status !== 200) throw new Error(`GET /devices/current after pairing: HTTP ${me.status}`);
      device.deviceId = me.body.device?.id ?? me.body.id;
      return device;
    }
    if (token.body?.error?.code === 'SLOW_DOWN') interval += 5000;
    else if (token.body?.error?.code !== 'AUTHORIZATION_PENDING') throw new Error(`pairing token: HTTP ${token.status} ${token.text}`);
  }
  throw new Error('pairing did not complete in 60 s');
}

/**
 * One console voice connection to the gateway. `heard` collects one entry per decoded 20 ms frame
 * ({ at, hz }); `texts` every control message; `closed` resolves with { code, reason }.
 */
export class ConsoleVoice {
  constructor({ url, token, ca, toneHz: hz = 300, headers = {} }) {
    this.url = url;
    this.token = token;
    this.ca = ca;
    this.hz = hz;
    this.headers = headers;
    this.heard = [];
    this.texts = [];
    this.sent = 0;
    this.muted = false;
    this.ready = null;
    this.timer = null;
    this.ws = null;
    this.closed = null;
  }

  /** Opens the WebSocket and sends hello; resolves with the `ready` message or rejects with the close code. */
  connect({ muted = false, deafened = false } = {}) {
    this.muted = muted;
    return new Promise((done, fail) => {
      const ws = new WebSocket(this.url, { ca: this.ca, headers: this.headers, handshakeTimeout: 15_000, perMessageDeflate: false });
      this.ws = ws;
      const decoder = new OpusScript(RATE, 1, OpusScript.Application.VOIP);
      let settled = false;
      this.closed = new Promise((closedDone) =>
        ws.on('close', (code, reason) => {
          clearInterval(this.timer);
          decoder.delete();
          const result = { code, reason: reason.toString() };
          if (!settled) {
            settled = true;
            fail(Object.assign(new Error(`gateway closed before ready: ${code} ${result.reason}`), result));
          }
          closedDone(result);
        }),
      );
      ws.on('unexpected-response', (_req, res) => {
        settled = true;
        fail(Object.assign(new Error(`gateway answered HTTP ${res.statusCode}`), { status: res.statusCode }));
        ws.terminate();
      });
      ws.on('error', (error) => {
        if (!settled) {
          settled = true;
          fail(error);
        }
      });
      ws.on('open', () => ws.send(JSON.stringify({ type: 'hello', protocol: 1, token: this.token, client: 'voice-staging-sim', muted, deafened })));
      ws.on('message', (data, binary) => {
        if (binary) {
          if (data.length < 4 || data[0] !== AUDIO_TAG) return;
          const pcm = decoder.decode(data.subarray(3));
          const samples = new Int16Array(pcm.buffer.slice(pcm.byteOffset, pcm.byteOffset + pcm.byteLength));
          this.heard.push({ at: Date.now(), hz: toneHz(samples) });
          return;
        }
        const message = JSON.parse(data.toString('utf8'));
        this.texts.push({ at: Date.now(), ...message });
        if (message.type === 'ready' && !settled) {
          settled = true;
          this.ready = message;
          this.startMicrophone();
          done(message);
        }
      });
    });
  }

  /** 50 Opus frames per second of a continuous tone, paced by the wall clock. */
  startMicrophone() {
    const encoder = new OpusScript(RATE, 1, OpusScript.Application.VOIP);
    encoder.setBitrate(32_000);
    let phase = 0;
    let seq = 0;
    const started = Date.now();
    let frames = 0;
    this.timer = setInterval(() => {
      if (this.ws?.readyState !== WebSocket.OPEN) return;
      const due = Math.floor((Date.now() - started) / 20);
      while (frames < due) {
        frames++;
        const pcm = new Int16Array(FRAME);
        for (let i = 0; i < FRAME; i++) {
          pcm[i] = Math.round(9000 * Math.sin(phase));
          phase += (2 * Math.PI * this.hz) / RATE;
        }
        if (this.muted) continue;
        const opus = encoder.encode(Buffer.from(pcm.buffer), FRAME);
        const frame = Buffer.allocUnsafe(opus.length + 3);
        frame[0] = AUDIO_TAG;
        frame.writeUInt16BE(seq, 1);
        Buffer.from(opus).copy(frame, 3);
        seq = (seq + 1) & 0xffff;
        this.ws.send(frame, { binary: true });
        this.sent++;
      }
    }, 10);
    this.ws.once('close', () => encoder.delete());
  }

  send(message) {
    if (this.ws?.readyState === WebSocket.OPEN) this.ws.send(JSON.stringify(message));
  }

  setMuted(muted) {
    this.muted = muted;
    this.send({ type: 'mute', muted });
  }

  setDeafened(deafened) {
    this.send({ type: 'deafen', deafened });
  }

  /** Frames heard since `since` whose tone is within `tolerance` Hz of `hz`. */
  framesAt(hz, since, tolerance = 15) {
    return this.heard.filter((h) => h.at >= since && Math.abs(h.hz - hz) <= tolerance).length;
  }

  /** The latest participant list the gateway sent. */
  participants() {
    for (let i = this.texts.length - 1; i >= 0; i--) if (this.texts[i].type === 'participants') return this.texts[i].list;
    return [];
  }

  async leave() {
    this.send({ type: 'leave' });
    return this.closed;
  }

  /** A network drop: the socket goes away without a close handshake. */
  drop() {
    this.ws?.terminate();
  }
}

export const newClientMessageId = () => randomUUID();
