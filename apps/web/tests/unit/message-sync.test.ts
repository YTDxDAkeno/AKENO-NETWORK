import { QueryClient } from '@tanstack/react-query';
import { afterEach, describe, expect, it, vi } from 'vitest';
import type { MessageDto, MessagePage } from '@akeno/shared';
import { flattenPages, type MessageCache } from '../../src/lib/messages';
import { queryKeys } from '../../src/lib/query-keys';

const getMock = vi.fn();
vi.mock('../../src/lib/api', () => ({ api: { get: (path: string) => getMock(path) } }));

const { applyRealtimeMessage, catchUpMessages } = await import('../../src/lib/message-sync');

const sender = { id: 'u1', onlineId: 'bob', displayName: 'Bob', avatar: 'nova', deleted: false };
const message = (id: number): MessageDto => ({
  id,
  conversationId: 'c1',
  sender,
  body: `Message ${id}`,
  clientMessageId: null,
  createdAt: new Date(id * 1000).toISOString(),
});
const page = (...ids: number[]): MessagePage => ({ messages: ids.map(message), hasMore: false });

/** Loads the conversation like useMessages does, with a controllable server state. */
function setup(serverIds: () => number[]) {
  const client = new QueryClient({ defaultOptions: { queries: { retry: false } } });
  const fetches: Array<() => void> = [];
  const observe = () =>
    client.fetchInfiniteQuery({
      queryKey: queryKeys.messages('c1'),
      initialPageParam: undefined as number | undefined,
      // Resolve manually so the test controls when the "REST response" arrives.
      queryFn: () => {
        const snapshot = page(...serverIds());
        return new Promise<MessagePage>((resolve) => fetches.push(() => resolve(snapshot)));
      },
      getNextPageParam: () => undefined,
    });
  return { client, fetches, observe };
}

const ids = (client: QueryClient) => flattenPages(client.getQueryData<MessageCache>(queryKeys.messages('c1'))).map((m) => m.id);

afterEach(() => getMock.mockReset());

describe('applyRealtimeMessage', () => {
  it('merges into a loaded conversation', async () => {
    const server = [1, 2];
    const { client, fetches, observe } = setup(() => server);
    const loaded = observe();
    fetches.shift()!();
    await loaded;
    applyRealtimeMessage(client, 'c1', message(3));
    expect(ids(client)).toEqual([1, 2, 3]);
  });

  it('does not lose a message that arrives while the initial fetch is in flight', async () => {
    const server = [1];
    const { client, fetches, observe } = setup(() => server);
    const loading = observe(); // Snapshot taken before message 2 exists on the server.
    server.push(2);
    applyRealtimeMessage(client, 'c1', message(2)); // Event arrives before the stale response.
    expect(fetches).toHaveLength(1);
    fetches[0]!(); // The stale response lands without message 2...
    await loading;
    await vi.waitFor(() => expect(ids(client)).toEqual([1, 2])); // ...and the event is applied after it.
  });

  it('does not duplicate a message the in-flight response already contains', async () => {
    const server = [1, 2];
    const { client, fetches, observe } = setup(() => server);
    const loading = observe();
    applyRealtimeMessage(client, 'c1', message(2));
    fetches[0]!();
    await loading;
    await vi.waitFor(() => expect(ids(client)).toEqual([1, 2]));
  });

  it('ignores conversations that are not cached', () => {
    const client = new QueryClient();
    applyRealtimeMessage(client, 'unknown', message(1));
    expect(client.getQueryCache().getAll()).toHaveLength(0);
  });
});

describe('catchUpMessages', () => {
  it('fetches messages after the newest cached one', async () => {
    const { client, fetches, observe } = setup(() => [1, 2]);
    const loaded = observe();
    fetches.shift()!();
    await loaded;
    getMock.mockResolvedValueOnce(page(3, 4));
    await catchUpMessages(client);
    expect(getMock).toHaveBeenCalledWith('/conversations/c1/messages?after=2&limit=100');
    expect(ids(client)).toEqual([1, 2, 3, 4]);
  });

  it('refetches empty conversations, which have no cursor', async () => {
    const server: number[] = [];
    const { client, fetches, observe } = setup(() => server);
    const loaded = observe();
    fetches.shift()!();
    await loaded;
    server.push(1); // Sent before the socket connected.
    const invalidate = vi.spyOn(client, 'invalidateQueries');
    await catchUpMessages(client);
    expect(getMock).not.toHaveBeenCalled();
    expect(invalidate).toHaveBeenCalledWith({ queryKey: queryKeys.messages('c1'), exact: true });
  });
});
