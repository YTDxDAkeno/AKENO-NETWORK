/**
 * AKENO voice gateway protocol, version 1 (docs/PS5_VOICE.md#gateway-protocol). The console side
 * is clients/ps5-native/src/core/voice_proto.c; both are kept in step by the end-to-end test.
 */
export const PROTOCOL_VERSION = 1;
export const SAMPLE_RATE = 48_000;
export const FRAME_SAMPLES = 960; // 20 ms
export const AUDIO_TAG = 0x01;
export const MAX_OPUS_PACKET = 400;
export const MAX_MESSAGE_BYTES = 4096;

export const CLOSE = {
  normal: 1000,
  auth: 4001,
  removed: 4003,
  replaced: 4004,
  ended: 4005,
  protocol: 4006,
  busy: 4007,
  timeout: 4008,
  unavailable: 4009,
} as const;

export type ClientMessage =
  | { type: 'hello'; protocol: number; token: string; client: string; muted: boolean; deafened: boolean }
  | { type: 'mute'; muted: boolean }
  | { type: 'deafen'; deafened: boolean }
  | { type: 'ping'; id: number }
  | { type: 'leave' };

/** Parses and validates a text message from a console; null when it is not a valid message. */
export function parseClientMessage(text: string): ClientMessage | null {
  if (text.length > MAX_MESSAGE_BYTES) return null;
  let value: unknown;
  try {
    value = JSON.parse(text);
  } catch {
    return null;
  }
  if (!value || typeof value !== 'object' || Array.isArray(value)) return null;
  const m = value as Record<string, unknown>;
  switch (m.type) {
    case 'hello':
      if (typeof m.token !== 'string' || m.token.length < 10 || m.token.length > 3000) return null;
      if (typeof m.protocol !== 'number') return null;
      return {
        type: 'hello',
        protocol: m.protocol,
        token: m.token,
        client: typeof m.client === 'string' ? m.client.slice(0, 64) : '',
        muted: m.muted === true,
        deafened: m.deafened === true,
      };
    case 'mute':
      return typeof m.muted === 'boolean' ? { type: 'mute', muted: m.muted } : null;
    case 'deafen':
      return typeof m.deafened === 'boolean' ? { type: 'deafen', deafened: m.deafened } : null;
    case 'ping':
      return typeof m.id === 'number' && Number.isSafeInteger(m.id) ? { type: 'ping', id: m.id } : null;
    case 'leave':
      return { type: 'leave' };
    default:
      return null;
  }
}

export function packAudio(seq: number, opus: Uint8Array): Buffer {
  const frame = Buffer.allocUnsafe(opus.length + 3);
  frame[0] = AUDIO_TAG;
  frame.writeUInt16BE(seq & 0xffff, 1);
  Buffer.from(opus.buffer, opus.byteOffset, opus.byteLength).copy(frame, 3);
  return frame;
}

export function unpackAudio(frame: Buffer): { seq: number; opus: Buffer } | null {
  if (frame.length < 4 || frame.length > MAX_OPUS_PACKET + 3 || frame[0] !== AUDIO_TAG) return null;
  return { seq: frame.readUInt16BE(1), opus: frame.subarray(3) };
}

export interface ParticipantView {
  id: string;
  name: string;
  muted: boolean;
  speaking: boolean;
  level: number;
  self: boolean;
}
