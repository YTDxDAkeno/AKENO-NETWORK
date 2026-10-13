import { Ban, MessageSquare, MoreHorizontal, UserMinus, UserPlus, Users } from 'lucide-react';
import { useSearchParams } from 'react-router';
import type { FriendDto } from '@akeno/shared';
import { CountBadge } from '../../components/Badge';
import { Button } from '../../components/Button';
import { Card, CardHeader } from '../../components/Card';
import { Menu } from '../../components/Menu';
import { PageHeader } from '../../components/PageHeader';
import { RelativeTime } from '../../components/RelativeTime';
import { Skeleton } from '../../components/Spinner';
import { EmptyState, ErrorState } from '../../components/States';
import { Tabs } from '../../components/Tabs';
import { UserRow } from '../../components/UserRow';
import { useBlockedUsers, useFriendRequests, useFriends } from '../../lib/queries';
import { useStartConversation } from '../messages/useStartConversation';
import { UserSearch } from './UserSearch';
import { useFriendActions } from './useFriendActions';

type Tab = 'online' | 'all' | 'requests' | 'blocked';

function FriendItem({ friend }: { friend: FriendDto }) {
  const actions = useFriendActions();
  const start = useStartConversation();
  return (
    <li className="py-3" data-testid="friend-item">
      <UserRow
        user={friend}
        presence={friend.presence}
        secondary={friend.statusMessage || undefined}
        actions={
          <>
            <Button
              size="sm"
              variant="ghost"
              onClick={() => start.mutate(friend.id)}
              icon={<MessageSquare aria-hidden className="size-4" />}
              aria-label={`Message ${friend.displayName}`}
            >
              <span className="hidden sm:inline">Message</span>
            </Button>
            <Menu
              label={`More actions for ${friend.displayName}`}
              triggerClassName="p-2 text-muted hover:bg-surface-2"
              trigger={<MoreHorizontal aria-hidden className="size-4" />}
              items={[
                { label: 'Remove friend', icon: <UserMinus />, onSelect: () => void actions.confirmRemove(friend.id, friend.displayName) },
                { label: 'Block', icon: <Ban />, tone: 'danger', onSelect: () => void actions.confirmBlock(friend.id, friend.displayName) },
              ]}
            />
          </>
        }
      />
    </li>
  );
}

function FriendList({ friends, emptyTitle, emptyDescription }: { friends: FriendDto[]; emptyTitle: string; emptyDescription: string }) {
  if (friends.length === 0) return <EmptyState icon={<Users className="size-6" />} title={emptyTitle} description={emptyDescription} />;
  return (
    <ul className="divide-y divide-line px-5">
      {friends.map((friend) => (
        <FriendItem key={friend.id} friend={friend} />
      ))}
    </ul>
  );
}

function RequestsPanel() {
  const requests = useFriendRequests();
  const actions = useFriendActions();
  if (requests.isPending) return <Skeleton className="m-5 h-24" />;
  if (requests.isError) return <ErrorState error={requests.error} onRetry={() => void requests.refetch()} />;
  const { incoming, outgoing } = requests.data;
  return (
    <div className="divide-y divide-line">
      <section className="px-5 py-4" aria-labelledby="incoming-heading">
        <h3 id="incoming-heading" className="mb-2 text-sm font-semibold text-muted">
          Incoming ({incoming.length})
        </h3>
        {incoming.length === 0 ? <p className="py-2 text-sm text-subtle">No incoming requests.</p> : null}
        <ul className="divide-y divide-line">
          {incoming.map((request) => (
            <li key={request.id} className="py-3" data-testid="incoming-request">
              <UserRow
                user={request.user}
                secondary={<RelativeTime iso={request.createdAt} />}
                actions={
                  <>
                    <Button size="sm" variant="primary" loading={actions.accept.isPending && actions.accept.variables === request.id} onClick={() => actions.accept.mutate(request.id)}>
                      Accept
                    </Button>
                    <Button size="sm" variant="ghost" onClick={() => actions.decline.mutate(request.id)}>
                      Decline
                    </Button>
                  </>
                }
              />
            </li>
          ))}
        </ul>
      </section>
      <section className="px-5 py-4" aria-labelledby="outgoing-heading">
        <h3 id="outgoing-heading" className="mb-2 text-sm font-semibold text-muted">
          Sent ({outgoing.length})
        </h3>
        {outgoing.length === 0 ? <p className="py-2 text-sm text-subtle">No pending sent requests.</p> : null}
        <ul className="divide-y divide-line">
          {outgoing.map((request) => (
            <li key={request.id} className="py-3">
              <UserRow
                user={request.user}
                secondary={<>Sent <RelativeTime iso={request.createdAt} /></>}
                actions={
                  <Button size="sm" variant="ghost" onClick={() => actions.cancel.mutate(request.id)}>
                    Cancel request
                  </Button>
                }
              />
            </li>
          ))}
        </ul>
      </section>
    </div>
  );
}

function BlockedPanel() {
  const blocked = useBlockedUsers();
  const actions = useFriendActions();
  if (blocked.isPending) return <Skeleton className="m-5 h-24" />;
  if (blocked.isError) return <ErrorState error={blocked.error} onRetry={() => void blocked.refetch()} />;
  if (blocked.data.length === 0) {
    return <EmptyState icon={<Ban className="size-6" />} title="No blocked users" description="People you block appear here." />;
  }
  return (
    <ul className="divide-y divide-line px-5">
      {blocked.data.map((entry) => (
        <li key={entry.user.id} className="py-3">
          <UserRow
            user={entry.user}
            secondary={<>Blocked <RelativeTime iso={entry.blockedAt} /></>}
            actions={
              <Button size="sm" variant="ghost" onClick={() => actions.unblock.mutate(entry.user.id)}>
                Unblock
              </Button>
            }
          />
        </li>
      ))}
    </ul>
  );
}

export function FriendsPage() {
  const [params, setParams] = useSearchParams();
  const tab = (['online', 'all', 'requests', 'blocked'] as const).find((value) => value === params.get('tab')) ?? 'online';
  const friends = useFriends();
  const requests = useFriendRequests();
  const online = (friends.data ?? []).filter((friend) => friend.presence.status !== 'offline');
  const incoming = requests.data?.incoming.length ?? 0;

  return (
    <>
      <PageHeader title="Friends" description="Find players, manage requests and see who is online." icon={<Users />} />
      <div className="grid gap-6 xl:grid-cols-[1fr_380px]">
        <Card>
          <div className="border-b border-line p-3">
            <Tabs<Tab>
              label="Friends views"
              value={tab}
              onChange={(value) => setParams(value === 'online' ? {} : { tab: value }, { replace: true })}
              items={[
                { value: 'online', label: `Online (${online.length})` },
                { value: 'all', label: `All friends (${friends.data?.length ?? 0})` },
                { value: 'requests', label: 'Requests', badge: <CountBadge count={incoming} label={`${incoming} incoming`} /> },
                { value: 'blocked', label: 'Blocked' },
              ]}
            />
          </div>
          <div role="tabpanel" aria-label={tab}>
            {tab === 'requests' ? (
              <RequestsPanel />
            ) : tab === 'blocked' ? (
              <BlockedPanel />
            ) : friends.isPending ? (
              <div className="space-y-3 p-5">
                <Skeleton className="h-12" />
                <Skeleton className="h-12" />
              </div>
            ) : friends.isError ? (
              <ErrorState error={friends.error} onRetry={() => void friends.refetch()} />
            ) : tab === 'online' ? (
              <FriendList friends={online} emptyTitle="Nobody is online right now" emptyDescription="Friends who are online, away or busy appear here." />
            ) : (
              <FriendList friends={friends.data} emptyTitle="No friends yet" emptyDescription="Search for players by Online ID to send your first friend request." />
            )}
          </div>
        </Card>
        <Card className="h-fit">
          <CardHeader title="Add friends" description="Search the network by Online ID or display name." icon={<UserPlus className="size-5" />} />
          <div className="p-5">
            <UserSearch />
          </div>
        </Card>
      </div>
    </>
  );
}
