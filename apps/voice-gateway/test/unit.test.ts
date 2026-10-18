import { describe, expect, it } from 'vitest';
import { loadConfig } from '../src/config.js';
import { levelOf, Mixer } from '../src/mixer.js';
import { FRAME_SAMPLES, packAudio, parseClientMessage, unpackAudio } from '../src/protocol.js';

const secret = 's'.repeat(40);

describe('protocol', () => {
  it('accepts exactly the console messages of protocol 1', () => {
    expect(parseClientMessage(JSON.stringify({ type: 'hello', protocol: 1, token: 'a.b.c.d.e.f', client: 'akeno-native', muted: true }))).toEqual({
      type: 'hello',
      protocol: 1,
      token: 'a.b.c.d.e.f',
      client: 'akeno-native',
      muted: true,
      deafened: false,
    });
    expect(parseClientMessage('{"type":"mute","muted":false}')).toEqual({ type: 'mute', muted: false });
    expect(parseClientMessage('{"type":"deafen","deafened":true}')).toEqual({ type: 'deafen', deafened: true });
    expect(parseClientMessage('{"type":"ping","id":5}')).toEqual({ type: 'ping', id: 5 });
    expect(parseClientMessage('{"type":"leave"}')).toEqual({ type: 'leave' });
    for (const bad of ['', '[]', 'null', '{"type":"mute"}', '{"type":"hello","token":"short","protocol":1}', '{"type":"ping","id":1.5}', '{"type":"x"}', '{'])
      expect(parseClientMessage(bad)).toBeNull();
    expect(parseClientMessage(JSON.stringify({ type: 'hello', protocol: 1, token: 'x'.repeat(5000) }))).toBeNull();
  });

  it('frames Opus packets with a sequence number', () => {
    const frame = packAudio(0x1beef, new Uint8Array([9, 8, 7]));
    expect([...frame]).toEqual([1, 0xbe, 0xef, 9, 8, 7]);
    expect(unpackAudio(frame)).toEqual({ seq: 0xbeef, opus: Buffer.from([9, 8, 7]) });
    expect(unpackAudio(Buffer.from([2, 0, 0, 1]))).toBeNull();
    expect(unpackAudio(Buffer.from([1, 0, 0]))).toBeNull();
    expect(unpackAudio(Buffer.alloc(404, 1))).toBeNull();
  });
});

describe('mixer', () => {
  it('sums sources with saturation, bounds the queues and reports levels', () => {
    const mixer = new Mixer();
    mixer.add('a');
    mixer.add('b');
    expect(mixer.tick()).toBeNull();
    mixer.push('a', new Int16Array(FRAME_SAMPLES).fill(20000));
    mixer.push('b', new Int16Array(FRAME_SAMPLES).fill(20000));
    mixer.push('b', new Int16Array(10)); // wrong size: ignored
    const out = mixer.tick();
    expect(out?.[0]).toBe(32767);
    expect(mixer.level('a')).toBeGreaterThan(90);
    for (let i = 0; i < 20; i++) mixer.push('a', new Int16Array(FRAME_SAMPLES).fill(100));
    let frames = 0;
    while (mixer.tick()) frames++;
    expect(frames).toBe(Mixer.MAX_QUEUE);
    expect(mixer.level('a')).toBe(0);
    mixer.remove('a');
    expect(mixer.sources).toBe(1);
    expect(levelOf(new Int16Array(10))).toBe(0);
  });
});

describe('configuration', () => {
  it('needs LiveKit and validates limits', () => {
    const base = { LIVEKIT_URL: 'ws://livekit:7880', LIVEKIT_API_KEY: 'k', LIVEKIT_API_SECRET: secret };
    expect(loadConfig(base)).toMatchObject({ host: '127.0.0.1', port: 7890, path: '/voice-gateway', maxSessions: 50 });
    expect(() => loadConfig({ ...base, LIVEKIT_URL: '' })).toThrow(/LIVEKIT_URL/);
    expect(() => loadConfig({ ...base, LIVEKIT_URL: 'http://x' })).toThrow(/ws/);
    expect(() => loadConfig({ ...base, LIVEKIT_API_SECRET: 'short' })).toThrow(/32/);
    expect(() => loadConfig({ ...base, GATEWAY_PATH: '/a?b' })).toThrow(/path/);
    expect(() => loadConfig({ ...base, GATEWAY_MAX_SESSIONS: '0' })).toThrow();
  });
});
