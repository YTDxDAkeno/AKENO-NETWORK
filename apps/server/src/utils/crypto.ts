import { createHash, createHmac, randomBytes, randomInt, timingSafeEqual } from 'node:crypto';

/** Cryptographically random URL-safe token. 32 bytes = 256 bits of entropy. */
export function randomToken(bytes = 32): string {
  return randomBytes(bytes).toString('base64url');
}

export function sha256Hex(value: string): string {
  return createHash('sha256').update(value, 'utf8').digest('hex');
}

export function hmacSha256(key: string, message: string): string {
  return createHmac('sha256', key).update(message, 'utf8').digest('base64url');
}

/** Constant-time string comparison that does not leak length differences through early exit. */
export function safeEqual(a: string, b: string): boolean {
  const left = Buffer.from(a, 'utf8');
  const right = Buffer.from(b, 'utf8');
  if (left.length !== right.length) {
    // Compare against itself to keep timing similar, then fail.
    timingSafeEqual(left, left);
    return false;
  }
  return timingSafeEqual(left, right);
}

/** Alphabet without visually ambiguous characters (no 0/O, 1/I/L). */
const INVITATION_ALPHABET = 'ABCDEFGHJKMNPQRSTUVWXYZ23456789';

/**
 * Generates a registration invitation code formatted as XXXX-XXXX-XXXX-XXXX.
 * 16 symbols from a 31-character alphabet provide roughly 79 bits of entropy.
 */
export function generateInvitationCode(): { display: string; normalized: string } {
  let normalized = '';
  for (let i = 0; i < 16; i += 1) {
    normalized += INVITATION_ALPHABET[randomInt(INVITATION_ALPHABET.length)];
  }
  const display = normalized.match(/.{4}/g)!.join('-');
  return { display, normalized };
}
