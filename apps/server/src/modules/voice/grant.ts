import { createCipheriv, createDecipheriv, hkdfSync, randomBytes } from 'node:crypto';

/** What authorized a voice token: a browser session or a paired device. */
export interface VoiceCredential {
  kind: 'session' | 'device';
  id: string;
}

/** Participant attribute that carries the sealed credential of the token the participant joined with. */
export const GRANT_ATTRIBUTE = 'akeno.grant';

const VERSION = 'v1.';
const IV_BYTES = 12;
const TAG_BYTES = 16;

/**
 * Seals the credential behind a voice token into the token itself, so the server can tell which
 * session or device authorized the participant that is actually connected (not merely the latest
 * token request), also after a restart.
 *
 * Participant attributes are visible to everyone in the room, so the credential is encrypted
 * (AES-256-GCM, fresh IV per token): other participants see an opaque value and never a session
 * or device ID. The user ID and room are authenticated data, so a grant only opens for the
 * identity and room it was issued for. The key is derived from the LiveKit API secret, which
 * already signs every token.
 */
export class VoiceGrantSealer {
  private readonly key: Buffer;

  constructor(apiSecret: string) {
    this.key = Buffer.from(hkdfSync('sha256', apiSecret, Buffer.alloc(0), 'akeno-voice-grant-v1', 32));
  }

  seal(credential: VoiceCredential, userId: string, room: string): string {
    const iv = randomBytes(IV_BYTES);
    const cipher = createCipheriv('aes-256-gcm', this.key, iv);
    cipher.setAAD(aad(userId, room));
    const body = Buffer.concat([cipher.update(JSON.stringify({ k: credential.kind === 'device' ? 'd' : 's', i: credential.id }), 'utf8'), cipher.final()]);
    return VERSION + Buffer.concat([iv, body, cipher.getAuthTag()]).toString('base64url');
  }

  /** The credential sealed for this identity and room, or null when the value is missing, foreign or tampered with. */
  open(value: string | undefined, userId: string, room: string): VoiceCredential | null {
    if (!value?.startsWith(VERSION)) return null;
    const raw = Buffer.from(value.slice(VERSION.length), 'base64url');
    if (raw.length <= IV_BYTES + TAG_BYTES) return null;
    try {
      const decipher = createDecipheriv('aes-256-gcm', this.key, raw.subarray(0, IV_BYTES));
      decipher.setAAD(aad(userId, room));
      decipher.setAuthTag(raw.subarray(raw.length - TAG_BYTES));
      const plain = Buffer.concat([decipher.update(raw.subarray(IV_BYTES, raw.length - TAG_BYTES)), decipher.final()]).toString('utf8');
      const parsed = JSON.parse(plain) as { k?: unknown; i?: unknown };
      if ((parsed.k !== 's' && parsed.k !== 'd') || typeof parsed.i !== 'string' || parsed.i.length === 0) return null;
      return { kind: parsed.k === 'd' ? 'device' : 'session', id: parsed.i };
    } catch {
      return null;
    }
  }
}

function aad(userId: string, room: string): Buffer {
  return Buffer.from(`${userId}\n${room}`, 'utf8');
}
