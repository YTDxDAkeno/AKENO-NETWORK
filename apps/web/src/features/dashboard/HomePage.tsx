import { Bell, Gamepad2, Inbox, MessagesSquare, PartyPopper, Plus, Sparkles, UserPlus, Users } from 'lucide-react';
import { Link } from 'react-router';
import { Avatar } from '../../components/Avatar';
import { CountBadge } from '../../components/Badge';
import { ButtonLink } from '../../components/Button';
import { Card, CardHeader } from '../../components/Card';
import { RelativeTime } from '../../components/RelativeTime';
import { Skeleton } from '../../components/Spinner';
import { EmptyState, ErrorState } from '../../components/States';
import { UserRow } from '../../components/UserRow';
import { useDocumentTitle } from '../../hooks/useDocumentTitle';
import { presenceLabel } from '../../lib/format';
import { describeNotification } from '../../lib/notifications';
import { useConversations, useCurrentParty, useFriends, useGameSessions, useInvitations, useNotifications } from '../../lib/queries';
import { useCurrentUser } from '../auth/AuthProvider';
import { InvitationsList } from '../invitations/InvitationsList';
import { useStartConversation } from '../messages/useStartConversation';
import { SessionCard } from '../sessions/SessionCard';

function greeting(): string {
  const hour = new Date().getHours();
  if (hour < 5) return 'Late night session';
  if (hour < 12) return 'Good morning';
  if (hour < 18) return 'Good afternoon';
  return 'Good evening';
}

function OnlineFriends() {
  const friends = useFriends();
  const start = useStartConversation();
  const online = (friends.data ?? []).filter((friend) => friend.presence.status !== 'offline');
  return (
    <Card>
      <CardHeader
        title="Online friends"
        icon={<Users className="size-5" />}
        description={friends.data ? `${online.length} of ${friends.data.length} online` : undefined}
        action={<ButtonLink to="/friends" size="sm" variant="ghost">All friends</ButtonLink>}
      />
      {friends.isPending ? (
        <div className="space-y-3 p-5">
          <Skeleton className="h-10" />
          <Skeleton className="h-10" />
        </div>
      ) : friends.isError ? (
        <ErrorState error={friends.error} onRetry={() => void friends.refetch()} />
      ) : friends.data.length === 0 ? (
        <EmptyState icon={<UserPlus className="size-6" />} title="Add your first friend" description="Search for players by their Online ID." action={<ButtonLink to="/friends" variant="primary" size="sm">Find friends</ButtonLink>} />
      ) : online.length === 0 ? (
        <EmptyState title="Nobody is online right now" description="You will see friends here as soon as they connect." />
      ) : (
        <ul className="divide-y divide-line px-5" data-testid="online-friends">
          {online.slice(0, 8).map((friend) => (
            <li key={friend.id} className="py-2.5">
              <UserRow
                user={friend}
                presence={friend.presence}
                size="sm"
                actions={
                  <button
                    type="button"
                    onClick={() => start.mutate(friend.id)}
                    className="rounded-lg p-2 text-muted hover:bg-surface-2 hover:text-ink"
                    aria-label={`Message ${friend.displayName}`}
                  >
                    <MessagesSquare aria-hidden className="size-4" />
                  </button>
                }
              />
            </li>
          ))}
        </ul>
      )}
    </Card>
  );
}

function RecentConversations() {
  const conversations = useConversations();
  const me = useCurrentUser();
  return (
    <Card>
      <CardHeader title="Recent conversations" icon={<MessagesSquare className="size-5" />} action={<ButtonLink to="/messages" size="sm" variant="ghost">Messages</ButtonLink>} />
      {conversations.isPending ? (
        <Skeleton className="m-5 h-24" />
      ) : conversations.isError ? (
        <ErrorState error={conversations.error} onRetry={() => void conversations.refetch()} />
      ) : conversations.data.filter((conversation) => conversation.lastMessage).length === 0 ? (
        <EmptyState title="No conversations yet" description="Message a friend to get started." />
      ) : (
        <ul className="divide-y divide-line">
          {conversations.data
            .filter((conversation) => conversation.lastMessage)
            .slice(0, 5)
            .map((conversation) => (
              <li key={conversation.id}>
                <Link to={`/messages/${conversation.id}`} className="flex items-center gap-3 px-5 py-3 hover:bg-surface-2">
                  <Avatar avatar={conversation.otherUser.avatar} name={conversation.otherUser.displayName} size="sm" status={conversation.otherUser.presence.status} />
                  <span className="min-w-0 flex-1">
                    <span className="block truncate text-sm font-medium">{conversation.otherUser.displayName}</span>
                    <span className="block truncate text-xs text-muted">
                      {conversation.lastMessage!.sender.id === me.id ? 'You: ' : ''}
                      {conversation.lastMessage!.body}
                    </span>
                  </span>
                  <CountBadge count={conversation.unreadCount} label={`${conversation.unreadCount} unread`} />
                </Link>
              </li>
            ))}
        </ul>
      )}
    </Card>
  );
}

function PartySummary() {
  const party = useCurrentParty();
  return (
    <Card>
      <CardHeader title="Your party" icon={<PartyPopper className="size-5" />} />
      {party.isPending ? (
        <Skeleton className="m-5 h-16" />
      ) : party.data ? (
        <Link to="/parties" className="flex items-center gap-4 px-5 py-4 hover:bg-surface-2">
          <div className="flex -space-x-2">
            {party.data.members.slice(0, 4).map((member) => (
              <Avatar key={member.user.id} avatar={member.user.avatar} name={member.user.displayName} size="sm" className="rounded-full ring-2 ring-surface" />
            ))}
          </div>
          <div className="min-w-0">
            <p className="truncate font-medium">{party.data.name}</p>
            <p className="text-xs text-muted">
              {party.data.memberCount}/{party.data.maxMembers} members · text chat
            </p>
          </div>
        </Link>
      ) : (
        <EmptyState title="You are not in a party" description="Start one and invite friends to hang out." action={<ButtonLink to="/parties" size="sm" variant="primary" icon={<Plus aria-hidden className="size-4" />}>Start a party</ButtonLink>} />
      )}
    </Card>
  );
}

function RecentNotifications() {
  const notifications = useNotifications();
  const items = notifications.data?.notifications.slice(0, 5) ?? [];
  return (
    <Card>
      <CardHeader title="Recent notifications" icon={<Bell className="size-5" />} />
      {notifications.isPending ? (
        <Skeleton className="m-5 h-16" />
      ) : items.length === 0 ? (
        <EmptyState title="Nothing new" description="Friend requests, invitations and session updates show up here." />
      ) : (
        <ul className="divide-y divide-line">
          {items.map((notification) => {
            const { text, href } = describeNotification(notification.data);
            return (
              <li key={notification.id}>
                <Link to={href} className="flex items-start gap-3 px-5 py-3 hover:bg-surface-2">
                  <Avatar avatar={notification.data.actor.avatar} name={notification.data.actor.displayName} size="xs" />
                  <span className="min-w-0 flex-1 text-sm">
                    {text}
                    <RelativeTime iso={notification.createdAt} className="block text-xs text-subtle" />
                  </span>
                </Link>
              </li>
            );
          })}
        </ul>
      )}
    </Card>
  );
}

export function HomePage() {
  useDocumentTitle('Home');
  const user = useCurrentUser();
  const invitations = useInvitations();
  const sessions = useGameSessions({ joinable: true });
  const shownPresence = user.presence === 'offline' ? user.presencePreference : user.presence;

  return (
    <div className="flex flex-col gap-6">
      <section className="card relative overflow-hidden p-6 sm:p-8" aria-labelledby="welcome-heading">
        <div className="brand-gradient pointer-events-none absolute -right-24 -top-24 size-72 rounded-full opacity-25 blur-3xl" aria-hidden />
        <div className="relative flex flex-col gap-6 sm:flex-row sm:items-center sm:justify-between">
          <div className="flex items-center gap-4">
            <Avatar avatar={user.profile.avatar} name={user.profile.displayName} size="lg" status={shownPresence} />
            <div>
              <p className="text-sm text-muted">{greeting()},</p>
              <h1 id="welcome-heading" className="text-2xl font-bold sm:text-3xl" data-testid="welcome-name">
                {user.profile.displayName}
              </h1>
              <p className="mt-1 flex items-center gap-1.5 text-sm text-muted">
                {presenceLabel(shownPresence)}
                {user.profile.statusMessage ? (
                  <>
                    <span aria-hidden>·</span>
                    <Sparkles aria-hidden className="size-3.5 text-violet" /> {user.profile.statusMessage}
                  </>
                ) : null}
              </p>
            </div>
          </div>
          <div className="flex flex-wrap gap-2">
            <ButtonLink to="/parties" variant="primary" icon={<PartyPopper aria-hidden className="size-4" />}>
              Start a party
            </ButtonLink>
            <ButtonLink to="/sessions" icon={<Gamepad2 aria-hidden className="size-4" />}>
              Find a session
            </ButtonLink>
            <ButtonLink to="/friends" variant="ghost" icon={<UserPlus aria-hidden className="size-4" />}>
              Add friends
            </ButtonLink>
          </div>
        </div>
      </section>

      {invitations.data && invitations.data.length > 0 ? (
        <Card>
          <CardHeader title="Pending invitations" icon={<Inbox className="size-5" />} description="Accept to join instantly." />
          <InvitationsList invitations={invitations.data} />
        </Card>
      ) : null}

      <div className="grid gap-6 lg:grid-cols-3">
        <div className="lg:col-span-2">
          <OnlineFriends />
        </div>
        <PartySummary />
      </div>

      <div className="grid gap-6 lg:grid-cols-2">
        <RecentConversations />
        <RecentNotifications />
      </div>

      <section aria-labelledby="sessions-heading">
        <div className="mb-3 flex items-center justify-between">
          <h2 id="sessions-heading" className="text-lg font-semibold">
            Sessions you can join
          </h2>
          <ButtonLink to="/sessions" size="sm" variant="ghost">
            Browse all
          </ButtonLink>
        </div>
        {sessions.isPending ? (
          <div className="grid gap-4 sm:grid-cols-2 xl:grid-cols-3">
            <Skeleton className="h-48" />
            <Skeleton className="h-48" />
          </div>
        ) : sessions.data && sessions.data.length > 0 ? (
          <div className="grid gap-4 sm:grid-cols-2 xl:grid-cols-3">
            {sessions.data.slice(0, 6).map((session) => (
              <SessionCard key={session.id} session={session} />
            ))}
          </div>
        ) : (
          <Card>
            <EmptyState icon={<Gamepad2 className="size-6" />} title="No open sessions right now" description="Host one and your friends will see it here." action={<ButtonLink to="/sessions" variant="primary" size="sm">Host a session</ButtonLink>} />
          </Card>
        )}
      </section>
    </div>
  );
}
