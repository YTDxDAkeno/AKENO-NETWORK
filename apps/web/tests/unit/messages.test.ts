import { describe, expect, it } from 'vitest';
import type { MessageDto } from '@akeno/shared';
import { flattenPages, mergeIntoCache, newestMessageId, unconfirmedPending, type MessageCache } from '../../src/lib/messages';

const sender = { id: 'u1', onlineId: 'alice', displayName: 'Alice', avatar: 'nova', avatarImage: null, deleted: false };
const message = (id: number, clientMessageId: string | null = null): MessageDto => ({
  id,
  conversationId: 'c1',
  sender,
  body: `Message ${id}`,
  clientMessageId,
  createdAt: new Date(id * 1000).toISOString(),
});

const cache: MessageCache = {
  pages: [
    { messages: [message(4), message(5)], hasMore: true },
    { messages: [message(1), message(2), message(3)], hasMore: false },
  ],
  pageParams: [undefined, 4],
};

describe('message cache helpers', () => {
  it('flattens newest-first pages into chronological order', () => {
    expect(flattenPages(cache).map((m) => m.id)).toEqual([1, 2, 3, 4, 5]);
    expect(newestMessageId(cache)).toBe(5);
  });

  it('merges realtime messages without duplicates', () => {
    const merged = mergeIntoCache(cache, [message(5), message(6)]);
    expect(flattenPages(merged).map((m) => m.id)).toEqual([1, 2, 3, 4, 5, 6]);
    expect(mergeIntoCache(merged, [message(6)])).toBe(merged);
    expect(mergeIntoCache(undefined, [message(7)])).toBeUndefined();
  });

  it('drops pending messages once the server confirms them', () => {
    const pending = [
      { clientMessageId: 'a', body: 'one', createdAt: '', status: 'sending' as const },
      { clientMessageId: 'b', body: 'two', createdAt: '', status: 'failed' as const },
    ];
    expect(unconfirmedPending(pending, [message(9, 'a')]).map((p) => p.clientMessageId)).toEqual(['b']);
  });
});
