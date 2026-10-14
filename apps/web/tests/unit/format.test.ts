import { describe, expect, it } from 'vitest';
import { describeNotification } from '../../src/lib/notifications';
import { formatDuration, gameGeneration, presenceDescription, relativeTime, secondsSince } from '../../src/lib/format';
import { uuidv4 } from '../../src/lib/uuid';

describe('formatting helpers', () => {
  const now = Date.parse('2026-10-13T12:00:00Z');
  it('formats relative times in English', () => {
    expect(relativeTime('2026-10-13T11:59:50Z', now)).toBe('just now');
    expect(relativeTime('2026-10-13T11:55:00Z', now)).toBe('5 minutes ago');
    expect(relativeTime('2026-10-13T11:00:00Z', now)).toBe('1 hour ago');
    expect(relativeTime('2026-10-12T10:00:00Z', now)).toBe('yesterday');
    expect(relativeTime('2026-10-09T12:00:00Z', now)).toBe('4 days ago');
  });

  it('describes presence', () => {
    expect(presenceDescription({ status: 'dnd', lastSeenAt: null })).toBe('Do Not Disturb');
    expect(presenceDescription({ status: 'offline', lastSeenAt: null })).toBe('Offline');
    expect(presenceDescription({ status: 'offline', lastSeenAt: new Date().toISOString() })).toBe('Last seen just now');
  });

  it('formats tracked playtime', () => {
    expect(formatDuration(0)).toBe('under a minute');
    expect(formatDuration(59)).toBe('under a minute');
    expect(formatDuration(60 * 45 + 30)).toBe('45 min');
    expect(formatDuration(3600 * 2 + 60 * 5)).toBe('2 h 5 min');
    expect(formatDuration(3600 * 3)).toBe('3 h');
    expect(formatDuration(-5)).toBe('under a minute');
    expect(secondsSince('2026-10-13T11:58:00Z', now)).toBe(120);
    expect(secondsSince('2026-10-13T12:05:00Z', now)).toBe(0); // clock skew never goes negative
    expect(gameGeneration('PPSA01234')).toBe('PS5 game');
    expect(gameGeneration('CUSA00001')).toBe('PS4 game');
    expect(gameGeneration('NPXS20001')).toBeNull();
  });

  it('describes notifications with links', () => {
    const actor = { id: 'u', onlineId: 'bob', displayName: 'Bob', avatar: 'nova', deleted: false };
    expect(describeNotification({ type: 'friend_request.received', actor, requestId: 'r' })).toEqual({
      text: 'Bob sent you a friend request.',
      href: '/friends?tab=requests',
    });
    expect(
      describeNotification({ type: 'invitation.received', actor, invitationId: 'i', kind: 'game_session', targetId: 's1', targetName: 'Racing' }).text,
    ).toBe('Bob invited you to the game session "Racing".');
  });
});

describe('uuidv4', () => {
  it('produces RFC 4122 v4 identifiers even without crypto.randomUUID', () => {
    const original = crypto.randomUUID;
    Object.defineProperty(crypto, 'randomUUID', { value: undefined, configurable: true });
    try {
      const id = uuidv4();
      expect(id).toMatch(/^[0-9a-f]{8}-[0-9a-f]{4}-4[0-9a-f]{3}-[89ab][0-9a-f]{3}-[0-9a-f]{12}$/);
      expect(uuidv4()).not.toBe(id);
    } finally {
      Object.defineProperty(crypto, 'randomUUID', { value: original, configurable: true });
    }
  });
});
