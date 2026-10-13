import type { MessageDto, MessagePage } from '@akeno/shared';
import type { InfiniteData } from '@tanstack/react-query';

/** A message the user sent that has not been acknowledged by the server yet. */
export interface PendingMessage {
  clientMessageId: string;
  body: string;
  createdAt: string;
  status: 'sending' | 'failed';
  error?: string;
}

/** Pages are ordered newest-first (page 0 = latest); messages inside a page are chronological. */
export type MessageCache = InfiniteData<MessagePage, unknown>;

/** Flattens cached pages into one chronological, de-duplicated list. */
export function flattenPages(data: MessageCache | undefined): MessageDto[] {
  if (!data) return [];
  const seen = new Set<number>();
  const result: MessageDto[] = [];
  for (const page of [...data.pages].reverse()) {
    for (const message of page.messages) {
      if (seen.has(message.id)) continue;
      seen.add(message.id);
      result.push(message);
    }
  }
  return result.sort((a, b) => a.id - b.id);
}

/** Inserts messages into the latest page unless they are already cached. Returns a new cache. */
export function mergeIntoCache(data: MessageCache | undefined, incoming: MessageDto[]): MessageCache | undefined {
  if (!data || data.pages.length === 0) return data;
  const known = new Set(data.pages.flatMap((page) => page.messages.map((message) => message.id)));
  const fresh = incoming.filter((message) => !known.has(message.id));
  if (fresh.length === 0) return data;
  const [latest, ...rest] = data.pages;
  const merged = [...latest!.messages, ...fresh].sort((a, b) => a.id - b.id);
  return { ...data, pages: [{ ...latest!, messages: merged }, ...rest] };
}

/** Pending messages that the server has not confirmed yet (matched by client message ID). */
export function unconfirmedPending(pending: PendingMessage[], confirmed: MessageDto[]): PendingMessage[] {
  const confirmedIds = new Set(confirmed.map((message) => message.clientMessageId).filter(Boolean));
  return pending.filter((entry) => !confirmedIds.has(entry.clientMessageId));
}

export function newestMessageId(data: MessageCache | undefined): number {
  const latest = data?.pages[0]?.messages;
  return latest && latest.length > 0 ? latest[latest.length - 1]!.id : 0;
}
