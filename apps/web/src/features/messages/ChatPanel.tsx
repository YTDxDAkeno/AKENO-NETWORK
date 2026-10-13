import { useMutation, useQueryClient } from '@tanstack/react-query';
import { AlertCircle, ChevronUp, MessagesSquare, RotateCw, Send } from 'lucide-react';
import { useCallback, useEffect, useLayoutEffect, useMemo, useRef, useState, type FormEvent, type KeyboardEvent } from 'react';
import { LIMITS, REALTIME_PROTOCOL_VERSION, type MessageDto, type SendMessageResponse, type UserSummary } from '@akeno/shared';
import { useRealtime } from '../../app/providers/RealtimeProvider';
import { Avatar } from '../../components/Avatar';
import { Button } from '../../components/Button';
import { cn } from '../../components/cn';
import { Spinner } from '../../components/Spinner';
import { EmptyState, ErrorState } from '../../components/States';
import { api, errorMessage } from '../../lib/api';
import { clockTime, dayLabel } from '../../lib/format';
import { type MessageCache, type PendingMessage, flattenPages, mergeIntoCache, unconfirmedPending } from '../../lib/messages';
import { useMessages } from '../../lib/queries';
import { queryKeys } from '../../lib/query-keys';
import { useTypingUsers } from '../../lib/typing-store';
import { uuidv4 } from '../../lib/uuid';
import { useCurrentUser } from '../auth/AuthProvider';

interface ChatPanelProps {
  conversationId: string;
  canSend?: boolean;
  cannotSendReason?: string;
  emptyTitle?: string;
  emptyDescription?: string;
  /** Known participants, used to name people who are typing. */
  participants?: UserSummary[];
  /** Called after the user's read position advances (e.g. to refresh unread badges). */
  onRead?: () => void;
  className?: string;
}

type Row =
  | { kind: 'day'; key: string; label: string }
  | { kind: 'message'; key: string; message: MessageDto; showHeader: boolean }
  | { kind: 'pending'; key: string; pending: PendingMessage };

const GROUP_WINDOW_MS = 5 * 60_000;

export function ChatPanel({
  conversationId,
  canSend = true,
  cannotSendReason,
  emptyTitle = 'No messages yet',
  emptyDescription = 'Say hello to get the conversation started.',
  participants = [],
  onRead,
  className,
}: ChatPanelProps) {
  const me = useCurrentUser();
  const queryClient = useQueryClient();
  const { send: sendRealtime } = useRealtime();
  const messagesQuery = useMessages(conversationId);
  const [pending, setPending] = useState<PendingMessage[]>([]);
  const [draft, setDraft] = useState('');
  const scrollRef = useRef<HTMLDivElement>(null);
  const stickToBottom = useRef(true);
  const preserveFrom = useRef<number | null>(null);
  const lastTypingSent = useRef(0);
  const lastReadSent = useRef(0);

  const messages = useMemo(() => flattenPages(messagesQuery.data), [messagesQuery.data]);
  const visiblePending = useMemo(() => unconfirmedPending(pending, messages), [pending, messages]);
  const typingIds = useTypingUsers(conversationId).filter((id) => id !== me.id);

  const nameFor = useCallback(
    (id: string) =>
      participants.find((user) => user.id === id)?.displayName ??
      messages.find((message) => message.sender.id === id)?.sender.displayName ??
      'Someone',
    [participants, messages],
  );

  const deliver = useMutation({
    mutationFn: (entry: PendingMessage) =>
      api.post<SendMessageResponse>(`/conversations/${conversationId}/messages`, {
        body: entry.body,
        clientMessageId: entry.clientMessageId,
      }),
    onSuccess: (result, entry) => {
      queryClient.setQueryData<MessageCache>(queryKeys.messages(conversationId), (data) => mergeIntoCache(data, [result.message]));
      setPending((current) => current.filter((item) => item.clientMessageId !== entry.clientMessageId));
    },
    onError: (error, entry) => {
      setPending((current) =>
        current.map((item) =>
          item.clientMessageId === entry.clientMessageId ? { ...item, status: 'failed', error: errorMessage(error) } : item,
        ),
      );
    },
  });

  const submit = (event?: FormEvent) => {
    event?.preventDefault();
    const body = draft.trim();
    if (!body || !canSend) return;
    const entry: PendingMessage = { clientMessageId: uuidv4(), body, createdAt: new Date().toISOString(), status: 'sending' };
    setPending((current) => [...current, entry]);
    setDraft('');
    stickToBottom.current = true;
    deliver.mutate(entry);
  };

  const retry = (entry: PendingMessage) => {
    const next = { ...entry, status: 'sending' as const };
    setPending((current) => current.map((item) => (item.clientMessageId === entry.clientMessageId ? next : item)));
    // Same client message ID: the server deduplicates if the first attempt actually arrived.
    deliver.mutate(next);
  };

  const onKeyDown = (event: KeyboardEvent<HTMLTextAreaElement>) => {
    if (event.key === 'Enter' && !event.shiftKey && !event.nativeEvent.isComposing) {
      event.preventDefault();
      submit();
    }
  };

  const onDraftChange = (value: string) => {
    setDraft(value);
    const now = Date.now();
    if (value.trim() && now - lastTypingSent.current > 3000) {
      lastTypingSent.current = now;
      sendRealtime({ v: REALTIME_PROTOCOL_VERSION, type: 'typing', data: { conversationId } });
    }
  };

  // Mark the newest message as read while the conversation is visible.
  const newestId = messages.length > 0 ? messages[messages.length - 1]!.id : 0;
  useEffect(() => {
    if (!newestId || newestId <= lastReadSent.current) return;
    const markRead = () => {
      if (document.visibilityState !== 'visible' || newestId <= lastReadSent.current) return;
      lastReadSent.current = newestId;
      api
        .post(`/conversations/${conversationId}/read`, { lastReadMessageId: newestId })
        .then(() => onRead?.())
        .catch(() => {
          lastReadSent.current = 0;
        });
    };
    const timer = window.setTimeout(markRead, 400);
    document.addEventListener('visibilitychange', markRead);
    return () => {
      window.clearTimeout(timer);
      document.removeEventListener('visibilitychange', markRead);
    };
  }, [newestId, conversationId, onRead]);

  // Keep the view pinned to the bottom for new messages, and stable when older ones load.
  useLayoutEffect(() => {
    const element = scrollRef.current;
    if (!element) return;
    if (preserveFrom.current !== null) {
      element.scrollTop = element.scrollHeight - preserveFrom.current;
      preserveFrom.current = null;
    } else if (stickToBottom.current) {
      element.scrollTop = element.scrollHeight;
    }
  }, [messages, visiblePending]);

  const onScroll = () => {
    const element = scrollRef.current;
    if (!element) return;
    stickToBottom.current = element.scrollHeight - element.scrollTop - element.clientHeight < 80;
  };

  const loadOlder = () => {
    const element = scrollRef.current;
    if (element) preserveFrom.current = element.scrollHeight - element.scrollTop;
    void messagesQuery.fetchNextPage();
  };

  const rows = useMemo<Row[]>(() => {
    const result: Row[] = [];
    let lastDay = '';
    let previous: MessageDto | null = null;
    for (const message of messages) {
      const day = dayLabel(message.createdAt);
      if (day !== lastDay) {
        result.push({ kind: 'day', key: `day-${message.id}`, label: day });
        lastDay = day;
        previous = null;
      }
      const grouped =
        previous !== null &&
        previous.sender.id === message.sender.id &&
        new Date(message.createdAt).getTime() - new Date(previous.createdAt).getTime() < GROUP_WINDOW_MS;
      result.push({ kind: 'message', key: `m-${message.id}`, message, showHeader: !grouped });
      previous = message;
    }
    for (const entry of visiblePending) result.push({ kind: 'pending', key: `p-${entry.clientMessageId}`, pending: entry });
    return result;
  }, [messages, visiblePending]);

  const remaining = LIMITS.messageBodyMax - draft.length;

  return (
    <div className={cn('flex min-h-0 flex-1 flex-col', className)}>
      <div
        ref={scrollRef}
        onScroll={onScroll}
        className="scrollbar-thin min-h-0 flex-1 overflow-y-auto px-3 py-4 sm:px-5"
        role="log"
        aria-live="polite"
        aria-relevant="additions"
        aria-label="Messages"
        data-testid="message-log"
      >
        {messagesQuery.isPending ? (
          <div className="flex justify-center py-10 text-muted">
            <Spinner label="Loading messages" />
          </div>
        ) : messagesQuery.isError ? (
          <ErrorState error={messagesQuery.error} onRetry={() => void messagesQuery.refetch()} />
        ) : (
          <>
            {messagesQuery.hasNextPage ? (
              <div className="mb-4 flex justify-center">
                <Button size="sm" variant="ghost" onClick={loadOlder} loading={messagesQuery.isFetchingNextPage} icon={<ChevronUp aria-hidden className="size-4" />}>
                  Load older messages
                </Button>
              </div>
            ) : null}
            {rows.length === 0 ? (
              <EmptyState icon={<MessagesSquare className="size-6" />} title={emptyTitle} description={emptyDescription} />
            ) : null}
            <ol className="flex flex-col">
              {rows.map((row) => {
                if (row.kind === 'day') {
                  return (
                    <li key={row.key} className="my-3 flex items-center gap-3 text-xs font-medium text-subtle">
                      <span className="h-px flex-1 bg-line" />
                      {row.label}
                      <span className="h-px flex-1 bg-line" />
                    </li>
                  );
                }
                if (row.kind === 'pending') return <PendingBubble key={row.key} entry={row.pending} onRetry={retry} />;
                return <MessageBubble key={row.key} message={row.message} mine={row.message.sender.id === me.id} showHeader={row.showHeader} />;
              })}
            </ol>
          </>
        )}
      </div>

      <div className="h-5 px-5 text-xs text-muted" aria-live="polite">
        {typingIds.length > 0
          ? `${typingIds.map(nameFor).slice(0, 2).join(' and ')} ${typingIds.length === 1 ? 'is' : 'are'} typing…`
          : null}
      </div>

      <form onSubmit={submit} className="border-t border-line p-3 sm:p-4">
        {canSend ? (
          <div className="flex items-end gap-2">
            <label htmlFor={`composer-${conversationId}`} className="sr-only">
              Message
            </label>
            <textarea
              id={`composer-${conversationId}`}
              value={draft}
              onChange={(event) => onDraftChange(event.target.value)}
              onKeyDown={onKeyDown}
              rows={1}
              maxLength={LIMITS.messageBodyMax}
              placeholder="Write a message…"
              className="max-h-40 min-h-11 flex-1 resize-none rounded-xl border border-line-strong bg-surface-2 px-3.5 py-2.5 text-sm text-ink placeholder:text-subtle focus:border-accent focus:outline-none focus:ring-2 focus:ring-accent/30 [field-sizing:content]"
              data-testid="message-input"
            />
            <Button type="submit" variant="primary" size="icon" className="size-11" disabled={!draft.trim()} aria-label="Send message">
              <Send aria-hidden className="size-4" />
            </Button>
          </div>
        ) : (
          <p className="rounded-xl bg-surface-2 px-4 py-3 text-sm text-muted">{cannotSendReason ?? 'You cannot send messages here.'}</p>
        )}
        {canSend && remaining < 200 ? (
          <p className={cn('mt-1.5 text-right text-xs', remaining < 0 ? 'text-danger' : 'text-subtle')}>
            {remaining} characters left
          </p>
        ) : null}
      </form>
    </div>
  );
}

function MessageBubble({ message, mine, showHeader }: { message: MessageDto; mine: boolean; showHeader: boolean }) {
  return (
    <li className={cn('flex gap-2.5', showHeader ? 'mt-3' : 'mt-0.5', mine && 'flex-row-reverse')} data-testid="message">
      <div className="w-9 shrink-0">
        {showHeader && !mine ? <Avatar avatar={message.sender.avatar} name={message.sender.displayName} size="sm" /> : null}
      </div>
      <div className={cn('flex max-w-[78%] flex-col', mine ? 'items-end' : 'items-start')}>
        {showHeader ? (
          <div className={cn('mb-1 flex items-baseline gap-2 text-xs', mine && 'flex-row-reverse')}>
            <span className="font-medium text-ink">{mine ? 'You' : message.sender.displayName}</span>
            <time dateTime={message.createdAt} className="text-subtle">
              {clockTime(message.createdAt)}
            </time>
          </div>
        ) : null}
        <p
          className={cn(
            'whitespace-pre-wrap break-words rounded-2xl px-3.5 py-2 text-sm leading-relaxed',
            mine ? 'brand-gradient rounded-tr-md text-on-accent' : 'rounded-tl-md bg-surface-3 text-ink',
          )}
          title={new Date(message.createdAt).toLocaleString('en-US')}
        >
          {message.body}
        </p>
      </div>
    </li>
  );
}

function PendingBubble({ entry, onRetry }: { entry: PendingMessage; onRetry: (entry: PendingMessage) => void }) {
  return (
    <li className="mt-1 flex flex-row-reverse gap-2.5" data-testid="pending-message">
      <div className="w-9 shrink-0" />
      <div className="flex max-w-[78%] flex-col items-end">
        <p
          className={cn(
            'whitespace-pre-wrap break-words rounded-2xl rounded-tr-md px-3.5 py-2 text-sm leading-relaxed',
            entry.status === 'failed' ? 'border border-danger/50 bg-danger-soft text-ink' : 'brand-gradient text-on-accent opacity-70',
          )}
        >
          {entry.body}
        </p>
        {entry.status === 'sending' ? (
          <span className="mt-1 text-xs text-subtle">Sending…</span>
        ) : (
          <span className="mt-1 flex items-center gap-2 text-xs text-danger">
            <AlertCircle aria-hidden className="size-3.5" />
            Not delivered{entry.error ? `: ${entry.error}` : ''}
            <button type="button" onClick={() => onRetry(entry)} className="inline-flex items-center gap-1 font-medium text-accent hover:underline">
              <RotateCw aria-hidden className="size-3" />
              Retry
            </button>
          </span>
        )}
      </div>
    </li>
  );
}
