import { describe, expect, it } from 'vitest';
import {
  checkPasswordQuality,
  createGameSessionSchema,
  invitationCodeSchema,
  onlineIdSchema,
  sendMessageSchema,
  singleLineText,
  updateProfileSchema,
  uuidSchema,
} from './index.js';

describe('shared validation schemas', () => {
  it('accepts valid Online IDs and rejects invalid ones', () => {
    for (const valid of ['Alice', 'bob_99', 'x-Ray', 'abc']) expect(onlineIdSchema.safeParse(valid).success).toBe(true);
    for (const invalid of ['ab', '9lives', 'with space', 'a'.repeat(17), 'emoji🙂', '_under']) {
      expect(onlineIdSchema.safeParse(invalid).success, invalid).toBe(false);
    }
  });

  it('normalizes invitation codes', () => {
    expect(invitationCodeSchema.parse(' abcd-efgh-2345-6789 ')).toBe('ABCDEFGH23456789');
  });

  it('normalizes UUIDs to lowercase', () => {
    expect(uuidSchema.parse('6F9619FF-8B86-4D11-B42D-00C04FC964FF')).toBe('6f9619ff-8b86-4d11-b42d-00c04fc964ff');
  });

  it('cleans single-line text', () => {
    expect(singleLineText(20).parse('  hello\n\tworld  ')).toBe('hello world');
    expect(singleLineText(20).parse('a\u0000b')).toBe('ab');
  });

  it('keeps line breaks in messages but rejects blank ones', () => {
    const id = '6f9619ff-8b86-4d11-b42d-00c04fc964ff';
    expect(sendMessageSchema.parse({ body: ' line 1\r\nline 2 ', clientMessageId: id }).body).toBe('line 1\nline 2');
    expect(sendMessageSchema.safeParse({ body: '   ', clientMessageId: id }).success).toBe(false);
  });

  it('validates profile updates', () => {
    expect(updateProfileSchema.safeParse({ avatar: 'nova' }).success).toBe(true);
    expect(updateProfileSchema.safeParse({ avatar: 'unknown' }).success).toBe(false);
    expect(updateProfileSchema.safeParse({ favoriteGames: Array(7).fill('Game') }).success).toBe(false);
    expect(updateProfileSchema.safeParse({ displayName: '   ' }).success).toBe(false);
  });

  it('applies game session defaults', () => {
    const parsed = createGameSessionSchema.parse({ gameName: 'Test Game' });
    expect(parsed).toMatchObject({ visibility: 'friends', joinMode: 'open', maxParticipants: 4, gameIdentifier: null });
    expect(createGameSessionSchema.safeParse({ gameName: 'x', maxParticipants: 65 }).success).toBe(false);
    expect(createGameSessionSchema.parse({ gameName: 'x', region: '  ' }).region).toBeNull();
  });

  it('flags weak passwords', () => {
    expect(checkPasswordQuality('password1234')).not.toBeNull();
    expect(checkPasswordQuality('aaaaaaaaaaaaaa')).not.toBeNull();
    expect(checkPasswordQuality('i-am-alice-forever', 'alice')).not.toBeNull();
    expect(checkPasswordQuality('correct-horse-battery-staple', 'alice')).toBeNull();
  });
});
