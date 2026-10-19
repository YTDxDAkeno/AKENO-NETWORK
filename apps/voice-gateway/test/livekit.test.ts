/**
 * The gateway against a real LiveKit server (the pinned binary, LIVEKIT_SERVER_BIN): a "console"
 * WebSocket client and a LiveKit participant ("bob", as a browser would be) exchange tones in both
 * directions; mute, deafen, replacement, removal and refused passes. Skipped without the binary.
 */
import { spawn, type ChildProcess } from 'node:child_process';
import { randomUUID } from 'node:crypto';
import { mkdtempSync, writeFileSync } from 'node:fs';
import { createServer } from 'node:net';
import { tmpdir } from 'node:os';
import { join } from 'node:path';
import { AudioFrame, AudioSource, AudioStream, LocalAudioTrack, Room, RoomEvent, TrackKind, TrackPublishOptions, TrackSource } from '@livekit/rtc-node';
import { AccessToken, RoomServiceClient, TrackSource as GrantSource } from 'livekit-server-sdk';
import OpusScript from 'opusscript';
import { pino } from 'pino';
import { afterAll, beforeAll, describe, expect, it } from 'vitest';
import { WebSocket } from 'ws';
import { createGateway, type Gateway } from '../src/server.js';

const BIN = process.env.LIVEKIT_SERVER_BIN;
const KEY = 'gwtest';
const SECRET = 'gateway-test-secret-0123456789abcdef';

async function freePort(): Promise<number> {
  return new Promise((resolve) => {
    const s = createServer().listen(0, '127.0.0.1', () => {
      const port = (s.address() as { port: number }).port;
      s.close(() => resolve(port));
    });
  });
}

/** Single-tone frequency of a block by zero crossings (0 when quiet). */
export function toneHz(pcm: Int16Array, rate = 48000): number {
  let energy = 0;
  for (const v of pcm) energy += v * v;
  if (pcm.length < 64 || Math.sqrt(energy / pcm.length) < 300) return 0;
  let crossings = 0;
  let first = -1;
  let last = -1;
  for (let i = 1; i < pcm.length; i++) {
    if (((pcm[i - 1] ?? 0) < 0) !== ((pcm[i] ?? 0) < 0)) {
      if (first < 0) first = i;
      last = i;
      crossings++;
    }
  }
  if (crossings < 3) return 0;
  return Math.round(rate / ((2 * (last - first)) / (crossings - 1)));
}

function tone(hz: number, frames: number, phase: { v: number }): Int16Array {
  const out = new Int16Array(960 * frames);
  for (let i = 0; i < out.length; i++) {
    out[i] = Math.round(9000 * Math.sin(phase.v));
    phase.v += (2 * Math.PI * hz) / 48000;
  }
  return out;
}

async function token(identity: string, room: string, consoleClient: boolean, name = identity): Promise<string> {
  const t = new AccessToken(KEY, SECRET, { identity, name, ttl: 120, ...(consoleClient ? { attributes: { 'akeno.client': 'console-gateway' } } : {}) });
  t.addGrant({ room, roomJoin: true, canPublish: true, canSubscribe: true, canPublishData: false, canPublishSources: [GrantSource.MICROPHONE] });
  return t.toJwt();
}

interface ConsoleClient {
  ws: WebSocket;
  texts: Array<Record<string, unknown>>;
  pcm: Int16Array[];
  closed: Promise<number>;
}

function connectConsole(url: string, headers: Record<string, string> = {}): Promise<ConsoleClient> {
  return new Promise((resolve, reject) => {
    const ws = new WebSocket(url, { headers });
    const decoder = new OpusScript(48000, 1, OpusScript.Application.VOIP);
    const client: ConsoleClient = { ws, texts: [], pcm: [], closed: new Promise((done) => ws.on('close', (code) => done(code))) };
    ws.on('message', (data, binary) => {
      const buffer = data as Buffer;
      if (!binary) client.texts.push(JSON.parse(buffer.toString('utf8')));
      else {
        const pcm = decoder.decode(buffer.subarray(3));
        client.pcm.push(new Int16Array(pcm.buffer.slice(pcm.byteOffset, pcm.byteOffset + pcm.byteLength)));
      }
    });
    ws.on('open', () => resolve(client));
    ws.on('unexpected-response', (_req, res) => reject(new Error(`HTTP ${res.statusCode}`)));
    ws.on('error', reject);
  });
}

async function until(check: () => boolean, ms = 10_000): Promise<void> {
  const end = Date.now() + ms;
  while (!check()) {
    if (Date.now() > end) throw new Error('timed out');
    await new Promise((r) => setTimeout(r, 25));
  }
}

describe.skipIf(!BIN)('gateway with a real LiveKit server', () => {
  let livekit: ChildProcess;
  let gateway: Gateway;
  let lkUrl = '';
  let gwUrl = '';
  const room = `akeno-party-${randomUUID()}`;

  beforeAll(async () => {
    const [port, rtcTcp, rtcUdp] = [await freePort(), await freePort(), await freePort()];
    const dir = mkdtempSync(join(tmpdir(), 'lk-'));
    const config = join(dir, 'livekit.yaml');
    writeFileSync(
      config,
      `port: ${port}\nrtc:\n  tcp_port: ${rtcTcp}\n  udp_port: ${rtcUdp}\n  use_external_ip: false\n  node_ip: 127.0.0.1\nkeys:\n  ${KEY}: ${SECRET}\nroom:\n  auto_create: true\nlogging:\n  level: warn\n`,
    );
    livekit = spawn(BIN!, ['--config', config], { stdio: 'ignore' });
    lkUrl = `ws://127.0.0.1:${port}`;
    const http = `http://127.0.0.1:${port}`;
    for (let i = 0; i < 100; i++) {
      try {
        if ((await fetch(http)).ok) break;
      } catch {
        /* starting */
      }
      await new Promise((r) => setTimeout(r, 100));
    }
    gateway = createGateway(
      { host: '127.0.0.1', port: 0, path: '/voice-gateway', livekitUrl: lkUrl, apiKey: KEY, apiSecret: SECRET, maxSessions: 4, logLevel: 'warn' },
      pino({ level: 'warn' }),
    );
    await new Promise<void>((resolve) => gateway.server.listen(0, '127.0.0.1', () => resolve()));
    gwUrl = `ws://127.0.0.1:${(gateway.server.address() as { port: number }).port}/voice-gateway`;
  });

  afterAll(async () => {
    await gateway?.close();
    livekit?.kill('SIGTERM');
  });

  it('carries audio both ways between a console and a LiveKit participant; mute, deafen and removal', async () => {
    // bob joins like a browser and speaks 600 Hz; he also records what reaches him
    const bob = new Room();
    await bob.connect(lkUrl, await token('bob', room, false, 'Bob'), { autoSubscribe: true, dynacast: false });
    const bobHeard: Int16Array[] = [];
    let consoleTrackPublished = 0;
    let consoleTrackUnpublished = 0;
    bob.on(RoomEvent.TrackPublished, (_p, participant) => participant.identity === 'ps5user' && consoleTrackPublished++);
    bob.on(RoomEvent.TrackUnpublished, (_p, participant) => participant.identity === 'ps5user' && consoleTrackUnpublished++);
    bob.on(RoomEvent.TrackSubscribed, (track) => {
      if (track.kind !== TrackKind.KIND_AUDIO) return;
      void (async () => {
        for await (const frame of new AudioStream(track, { sampleRate: 48000, numChannels: 1, frameSizeMs: 20 })) bobHeard.push(new Int16Array(frame.data));
      })();
    });
    const bobSource = new AudioSource(48000, 1);
    const bobTrack = LocalAudioTrack.createAudioTrack('microphone', bobSource);
    const options = new TrackPublishOptions();
    options.source = TrackSource.SOURCE_MICROPHONE;
    await bob.localParticipant!.publishTrack(bobTrack, options);
    let speaking = true;
    const bobPhase = { v: 0 };
    const bobLoop = (async () => {
      while (speaking) await bobSource.captureFrame(new AudioFrame(tone(600, 1, bobPhase), 48000, 1, 960));
    })();

    // the console
    const ps5 = await connectConsole(gwUrl);
    ps5.ws.send(JSON.stringify({ type: 'hello', protocol: 1, token: await token('ps5user', room, true, 'PS5 User'), client: 'test', muted: false, deafened: false }));
    await until(() => ps5.texts.some((m) => m.type === 'ready'));
    expect(ps5.texts.find((m) => m.type === 'ready')).toMatchObject({ protocol: 1, room, identity: 'ps5user', sampleRate: 48000, channels: 1, frameMs: 20 });

    // bob -> console: 600 Hz arrives as Opus
    await until(() => ps5.pcm.length > 40);
    const heard = ps5.pcm.slice(-10);
    expect(heard.map((pcm) => toneHz(pcm)).filter((hz) => hz > 590 && hz < 610).length).toBeGreaterThanOrEqual(8);

    // console -> bob: 300 Hz, 50 frames per second
    const encoder = new OpusScript(48000, 1, OpusScript.Application.VOIP);
    const phase = { v: 0 };
    let seq = 0;
    let sending = true;
    const sendLoop = (async () => {
      while (sending) {
        const pcm = tone(300, 1, phase);
        const packet = encoder.encode(Buffer.from(pcm.buffer), 960);
        const frame = Buffer.concat([Buffer.from([1, (seq >> 8) & 255, seq & 255]), packet]);
        seq = (seq + 1) & 0xffff;
        if (ps5.ws.readyState === ps5.ws.OPEN) ps5.ws.send(frame);
        await new Promise((r) => setTimeout(r, 20));
      }
    })();
    await until(() => bobHeard.filter((pcm) => Math.abs(toneHz(pcm) - 300) < 10).length > 20);
    // the participant list shows both, bob speaking
    await until(() =>
      ps5.texts.some((m) => m.type === 'participants' && (m.list as Array<{ id: string; speaking: boolean }>).some((p) => p.id === 'bob' && p.speaking)),
    );
    const lastList = [...ps5.texts].reverse().find((m) => m.type === 'participants')!.list as Array<{ id: string; self: boolean; name: string }>;
    expect(lastList.find((p) => p.self)?.id).toBe('ps5user');
    expect(lastList.find((p) => p.id === 'bob')?.name).toBe('Bob');

    // mute: the console's microphone track goes away; unmute brings it back
    ps5.ws.send('{"type":"mute","muted":true}');
    await until(() => consoleTrackUnpublished > 0);
    await until(() =>
      ps5.texts.some((m) => m.type === 'participants' && (m.list as Array<{ self: boolean; muted: boolean }>).some((p) => p.self && p.muted)),
    );
    const before = consoleTrackPublished;
    ps5.ws.send('{"type":"mute","muted":false}');
    await until(() => consoleTrackPublished > before);
    bobHeard.length = 0;
    await until(() => bobHeard.filter((pcm) => Math.abs(toneHz(pcm) - 300) < 10).length > 10);

    // deafen: no audio frames reach the console
    ps5.ws.send('{"type":"deafen","deafened":true}');
    await new Promise((r) => setTimeout(r, 300));
    const count = ps5.pcm.length;
    await new Promise((r) => setTimeout(r, 500));
    expect(ps5.pcm.length).toBe(count);
    ps5.ws.send('{"type":"deafen","deafened":false}');
    await until(() => ps5.pcm.length > count + 10);

    // ping
    ps5.ws.send('{"type":"ping","id":77}');
    await until(() => ps5.texts.some((m) => m.type === 'pong' && m.id === 77));

    // removal by the AKENO server (room service API) closes the console with 4003
    const service = new RoomServiceClient(lkUrl.replace('ws://', 'http://'), KEY, SECRET);
    await service.removeParticipant(room, 'ps5user');
    expect(await ps5.closed).toBe(4003);
    sending = false;
    speaking = false;
    await sendLoop;
    await bobLoop.catch(() => undefined);
    await bob.disconnect();
    await until(() => gateway.registry.sessions.size === 0);
  });

  it('refuses passes that are not console voice passes, browsers, and malformed input; one session per account', async () => {
    const a = await connectConsole(gwUrl);
    a.ws.send(JSON.stringify({ type: 'hello', protocol: 1, token: await token('u2', room, false), muted: true }));
    expect(await a.closed).toBe(4001); // a browser pass (no console attribute)
    const b = await connectConsole(gwUrl);
    b.ws.send(JSON.stringify({ type: 'hello', protocol: 1, token: 'not.a.token.at.all', muted: true }));
    expect(await b.closed).toBe(4001);
    const other = new AccessToken(KEY, 'another-secret-0123456789abcdef0123456', { identity: 'u2', attributes: { 'akeno.client': 'console-gateway' } });
    other.addGrant({ room, roomJoin: true });
    const c = await connectConsole(gwUrl);
    c.ws.send(JSON.stringify({ type: 'hello', protocol: 1, token: await other.toJwt(), muted: true }));
    expect(await c.closed).toBe(4001); // signed by someone else
    const d = await connectConsole(gwUrl);
    d.ws.send('{"type":"mute"}');
    expect(await d.closed).toBe(4006);
    await expect(connectConsole(gwUrl, { origin: 'https://evil.example' })).rejects.toThrow(/403/);
    await expect(connectConsole(gwUrl.replace('/voice-gateway', '/other'))).rejects.toThrow(/404/);

    const first = await connectConsole(gwUrl);
    first.ws.send(JSON.stringify({ type: 'hello', protocol: 1, token: await token('u3', room, true), muted: true }));
    await until(() => first.texts.some((m) => m.type === 'ready'));
    const second = await connectConsole(gwUrl);
    second.ws.send(JSON.stringify({ type: 'hello', protocol: 1, token: await token('u3', room, true), muted: true }));
    expect(await first.closed).toBe(4004);
    await until(() => second.texts.some((m) => m.type === 'ready'));
    second.ws.send('{"type":"leave"}');
    expect(await second.closed).toBe(1000);
  });
});
