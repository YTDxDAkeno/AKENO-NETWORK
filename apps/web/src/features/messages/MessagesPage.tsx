import { useQueryClient } from '@tanstack/react-query';
import { ArrowLeft, MessageSquarePlus, MessagesSquare, Search } from 'lucide-react';
import { useState } from 'react';
import { Link, NavLink, useParams } from 'react-router';
import type { DirectConversationDto } from '@akeno/shared';
import { Avatar } from '../../components/Avatar';
import { CountBadge } from '../../components/Badge';
import { Button } from '../../components/Button';
import { cn } from '../../components/cn';
import { Modal } from '../../components/Modal';
import { RelativeTime } from '../../components/RelativeTime';
import { Skeleton } from '../../components/Spinner';
import { EmptyState, ErrorState } from '../../components/States';
import { UserRow } from '../../components/UserRow';
import { useDocumentTitle } from '../../hooks/useDocumentTitle';
import { presenceDescription } from '../../lib/format';
import { useConversations, useFriends } from '../../lib/queries';
import { queryKeys } from '../../lib/query-keys';
import { useCurrentUser } from '../auth/AuthProvider';
import { ChatPanel } from './ChatPanel';
import { useStartConversation } from './useStartConversation';

function NewConversationDialog({ open, onClose }: { open: boolean; onClose: () => void }) {
  const friends = useFriends();
  const start = useStartConversation();
  const [filter, setFilter] = useState('');
  const list = (friends.data ?? []).filter((friend) =>
    `${friend.displayName} ${friend.onlineId}`.toLowerCase().includes(filter.trim().toLowerCase()),
  );
  return (
    <Modal open={open} onClose={onClose} title="New message" description="Choose a friend to message.">
      <label className="relative mb-3 block">
        <span className="sr-only">Filter friends</span>
        <Search aria-hidden className="absolute left-3 top-1/2 size-4 -translate-y-1/2 text-subtle" />
        <input
          value={filter}
          onChange={(event) => setFilter(event.target.value)}
          placeholder="Filter friends"
          className="h-10 w-full rounded-xl border border-line-strong bg-surface-2 pl-9 pr-3 text-sm focus:border-accent focus:outline-none"
        />
      </label>
      {friends.data && list.length === 0 ? (
        <EmptyState title="No friends found" description="Add friends first, then start a conversation." />
      ) : null}
      <ul className="flex flex-col gap-1">
        {list.map((friend) => (
          <li key={friend.id}>
            <button
              type="button"
              onClick={() => {
                start.mutate(friend.id);
                onClose();
              }}
              className="w-full rounded-xl p-2 text-left hover:bg-surface-2"
            >
              <UserRow user={friend} presence={friend.presence} linkToProfile={false} size="sm" />
            </button>
          </li>
        ))}
      </ul>
    </Modal>
  );
}

function ConversationItem({ conversation }: { conversation: DirectConversationDto }) {
  const me = useCurrentUser();
  const preview = conversation.lastMessage
    ? `${conversation.lastMessage.sender.id === me.id ? 'You: ' : ''}${conversation.lastMessage.body}`
    : 'No messages yet';
  return (
    <NavLink
      to={`/messages/${conversation.id}`}
      className={({ isActive }) =>
        cn('flex items-center gap-3 rounded-xl px-3 py-2.5 transition-colors', isActive ? 'bg-accent-soft' : 'hover:bg-surface-2')
      }
    >
      <Avatar avatar={conversation.otherUser.avatar} name={conversation.otherUser.displayName} status={conversation.otherUser.presence.status} />
      <span className="min-w-0 flex-1">
        <span className="flex items-baseline justify-between gap-2">
          <span className={cn('truncate text-sm', conversation.unreadCount > 0 ? 'font-semibold text-ink' : 'font-medium text-ink')}>
            {conversation.otherUser.displayName}
          </span>
          {conversation.lastMessageAt ? <RelativeTime iso={conversation.lastMessageAt} className="shrink-0 text-[11px] text-subtle" /> : null}
        </span>
        <span className="flex items-center justify-between gap-2">
          <span className={cn('truncate text-xs', conversation.unreadCount > 0 ? 'text-ink' : 'text-muted')}>{preview}</span>
          <CountBadge count={conversation.unreadCount} label={`${conversation.unreadCount} unread`} />
        </span>
      </span>
    </NavLink>
  );
}

export function MessagesPage() {
  useDocumentTitle('Messages');
  const { conversationId } = useParams();
  const conversations = useConversations();
  const queryClient = useQueryClient();
  const [composeOpen, setComposeOpen] = useState(false);
  const active = conversations.data?.find((conversation) => conversation.id === conversationId) ?? null;

  return (
    <div className="card flex h-[calc(100dvh-9.5rem)] overflow-hidden lg:h-[calc(100dvh-7rem)]">
      <aside
        className={cn('flex w-full flex-col border-line md:w-80 md:shrink-0 md:border-r', conversationId ? 'hidden md:flex' : 'flex')}
        aria-label="Conversations"
      >
        <div className="flex items-center justify-between border-b border-line px-4 py-3.5">
          <h1 className="text-lg font-semibold">Messages</h1>
          <Button size="sm" variant="subtle" onClick={() => setComposeOpen(true)} icon={<MessageSquarePlus aria-hidden className="size-4" />}>
            New
          </Button>
        </div>
        <div className="scrollbar-thin flex-1 overflow-y-auto p-2">
          {conversations.isPending ? (
            <div className="space-y-2 p-2">
              {[0, 1, 2].map((index) => (
                <Skeleton key={index} className="h-14" />
              ))}
            </div>
          ) : conversations.isError ? (
            <ErrorState error={conversations.error} onRetry={() => void conversations.refetch()} />
          ) : conversations.data.length === 0 ? (
            <EmptyState
              icon={<MessagesSquare className="size-6" />}
              title="No conversations yet"
              description="Start a conversation with one of your friends."
              action={
                <Button size="sm" variant="primary" onClick={() => setComposeOpen(true)}>
                  New message
                </Button>
              }
            />
          ) : (
            <nav className="flex flex-col gap-0.5">
              {conversations.data.map((conversation) => (
                <ConversationItem key={conversation.id} conversation={conversation} />
              ))}
            </nav>
          )}
        </div>
      </aside>

      <section className={cn('min-w-0 flex-1 flex-col', conversationId ? 'flex' : 'hidden md:flex')} aria-label="Conversation">
        {conversationId && active ? (
          <>
            <header className="flex items-center gap-3 border-b border-line px-3 py-3 sm:px-5">
              <Link to="/messages" className="rounded-lg p-1.5 text-muted hover:bg-surface-2 md:hidden" aria-label="Back to conversations">
                <ArrowLeft aria-hidden className="size-5" />
              </Link>
              <Avatar avatar={active.otherUser.avatar} name={active.otherUser.displayName} status={active.otherUser.presence.status} />
              <div className="min-w-0">
                <Link to={`/u/${active.otherUser.onlineId}`} className="block truncate font-semibold hover:underline">
                  {active.otherUser.displayName}
                </Link>
                <p className="truncate text-xs text-muted" data-testid="chat-presence">
                  {presenceDescription(active.otherUser.presence)}
                  {active.otherUser.presence.activity ? ` · ${active.otherUser.presence.activity} (self-reported)` : ''}
                </p>
              </div>
            </header>
            <ChatPanel
              key={active.id}
              conversationId={active.id}
              canSend={active.canSend}
              cannotSendReason="You can't send messages in this conversation because of privacy settings or a block."
              participants={[active.otherUser]}
              onRead={() => void queryClient.invalidateQueries({ queryKey: queryKeys.conversations })}
            />
          </>
        ) : conversationId && conversations.isSuccess ? (
          <EmptyState title="Conversation not found" description="It may have been removed, or you don't have access to it." />
        ) : (
          <EmptyState
            className="m-auto"
            icon={<MessagesSquare className="size-7" />}
            title="Select a conversation"
            description="Choose a conversation on the left or start a new one."
          />
        )}
      </section>
      <NewConversationDialog open={composeOpen} onClose={() => setComposeOpen(false)} />
    </div>
  );
}
