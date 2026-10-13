import type { QueryClient } from '@tanstack/react-query';
import type { MessageDto, MessagePage } from '@akeno/shared';
import { api } from './api';
import { type MessageCache, mergeIntoCache, newestMessageId } from './messages';
import { queryKeys } from './query-keys';

/**
 * Applies a `message.created` event to the message cache.
 *
 * While a fetch for the conversation is in flight, its response may have been produced before
 * the message was stored and would overwrite a merged message (React Query does not cancel an
 * initial fetch that has no data yet). The event is therefore applied once that fetch settles;
 * merging is idempotent by message ID.
 */
export function applyRealtimeMessage(queryClient: QueryClient, conversationId: string, message: MessageDto): void {
  const key = queryKeys.messages(conversationId);
  const cache = queryClient.getQueryCache();
  const query = cache.find<MessageCache>({ queryKey: key, exact: true });
  if (!query) return; // Not displayed or cached: it will be fetched when opened.
  if (query.state.fetchStatus === 'fetching') {
    const unsubscribe = cache.subscribe((event) => {
      if (event.query !== query) return;
      if (event.type === 'removed') {
        unsubscribe();
      } else if (query.state.fetchStatus === 'idle') {
        unsubscribe();
        applyRealtimeMessage(queryClient, conversationId, message);
      }
    });
    return;
  }
  if (!query.state.data || query.state.data.pages.length === 0) {
    void queryClient.invalidateQueries({ queryKey: key, exact: true });
    return;
  }
  queryClient.setQueryData<MessageCache>(key, (data) => mergeIntoCache(data, [message]));
}

/**
 * Fetches messages that may have been missed while no realtime connection existed, for every
 * cached conversation. Runs after every `hello`, including the first one after a page load,
 * because messages can be stored between the initial REST fetch and the socket connecting.
 */
export async function catchUpMessages(queryClient: QueryClient): Promise<void> {
  const messageQueries = queryClient.getQueryCache().findAll({ queryKey: queryKeys.allMessages });
  for (const query of messageQueries) {
    const conversationId = query.queryKey[1] as string;
    const data = query.state.data as MessageCache | undefined;
    let after = newestMessageId(data);
    if (!data || after === 0) {
      // Nothing to use as a cursor (not loaded yet, or an empty conversation): refetch.
      void queryClient.invalidateQueries({ queryKey: queryKeys.messages(conversationId), exact: true });
      continue;
    }
    try {
      for (let page = 0; page < 10; page += 1) {
        const result = await api.get<MessagePage>(`/conversations/${conversationId}/messages?after=${after}&limit=100`);
        queryClient.setQueryData<MessageCache>(queryKeys.messages(conversationId), (current) =>
          mergeIntoCache(current, result.messages),
        );
        if (!result.hasMore || result.messages.length === 0) break;
        after = result.messages[result.messages.length - 1]!.id;
      }
    } catch {
      void queryClient.invalidateQueries({ queryKey: queryKeys.messages(conversationId), exact: true });
    }
  }
}
