import { Ban, CalendarDays, Gamepad2, Lock, MessageSquare, MoreHorizontal, Pencil, Sparkles, UserCheck, UserMinus, UserPlus, Users } from 'lucide-react';
import { Link, useParams } from 'react-router';
import { Avatar } from '../../components/Avatar';
import { Badge } from '../../components/Badge';
import { Button, ButtonLink } from '../../components/Button';
import { Card } from '../../components/Card';
import { GameActivityCard, GameHistory } from '../../components/GameActivity';
import { Menu } from '../../components/Menu';
import { PageLoader } from '../../components/Spinner';
import { EmptyState, ErrorState, Notice } from '../../components/States';
import { useDocumentTitle } from '../../hooks/useDocumentTitle';
import { ApiError } from '../../lib/api';
import { avatarPreset } from '../../lib/avatars';
import { longDate, presenceDescription } from '../../lib/format';
import { useGameActivity, useProfile } from '../../lib/queries';
import { useFriendActions } from '../friends/useFriendActions';
import { useStartConversation } from '../messages/useStartConversation';

export function ProfilePage() {
  const { handle = '' } = useParams();
  const profile = useProfile(handle);
  const activity = useGameActivity(profile.data?.user.id);
  const actions = useFriendActions();
  const start = useStartConversation();
  useDocumentTitle(profile.data ? profile.data.user.displayName : 'Profile');

  if (profile.isPending) return <PageLoader label="Loading profile…" />;
  if (profile.isError) {
    if (profile.error instanceof ApiError && profile.error.status === 404) {
      return (
        <Card>
          <EmptyState icon={<Users className="size-6" />} title="Player not found" description="This profile does not exist or is not available to you." action={<ButtonLink to="/friends">Back to friends</ButtonLink>} />
        </Card>
      );
    }
    return <ErrorState error={profile.error} onRetry={() => void profile.refetch()} />;
  }

  const data = profile.data;
  const { user, relationship } = data;
  const preset = avatarPreset(user.avatar);
  const isSelf = relationship.state === 'self';

  const primaryAction = (() => {
    switch (relationship.state) {
      case 'self':
        return (
          <ButtonLink to="/settings" variant="secondary" icon={<Pencil aria-hidden className="size-4" />}>
            Edit profile
          </ButtonLink>
        );
      case 'friend':
        return (
          <Badge tone="success" icon={<UserCheck aria-hidden className="size-3.5" />} className="px-3 py-1.5 text-sm">
            Friends
          </Badge>
        );
      case 'request_sent':
        return (
          <Button variant="secondary" onClick={() => relationship.friendRequestId && actions.cancel.mutate(relationship.friendRequestId)}>
            Cancel friend request
          </Button>
        );
      case 'request_received':
        return (
          <Button
            variant="primary"
            loading={actions.accept.isPending}
            onClick={() => relationship.friendRequestId && actions.accept.mutate(relationship.friendRequestId)}
            icon={<UserCheck aria-hidden className="size-4" />}
          >
            Accept friend request
          </Button>
        );
      case 'blocked':
        return (
          <Button variant="secondary" onClick={() => actions.unblock.mutate(user.id)}>
            Unblock
          </Button>
        );
      default:
        return relationship.canSendFriendRequest ? (
          <Button
            variant="primary"
            loading={actions.sendRequest.isPending}
            onClick={() => actions.sendRequest.mutate(user.id)}
            icon={<UserPlus aria-hidden className="size-4" />}
          >
            Add friend
          </Button>
        ) : (
          <Badge>Not accepting friend requests</Badge>
        );
    }
  })();

  return (
    <div className="mx-auto max-w-4xl">
      <Card className="overflow-hidden">
        <div
          className="relative h-36 sm:h-44"
          style={{ backgroundImage: `linear-gradient(120deg, ${preset.from}cc, ${preset.to}cc), radial-gradient(circle at 80% 20%, #ffffff33, transparent 50%)` }}
          aria-hidden
        >
          <div className="absolute inset-0 bg-[linear-gradient(transparent_60%,var(--akeno-surface))]" />
        </div>
        <div className="relative px-5 pb-6 sm:px-8">
          <div className="-mt-16 flex flex-col gap-4 sm:-mt-14 sm:flex-row sm:items-end sm:justify-between">
            <div className="flex items-end gap-4">
              <Avatar avatar={user.avatar} name={user.displayName} size="xl" status={data.presence.status} className="rounded-full ring-4 ring-surface" />
              <div className="pb-1">
                <h1 className="text-2xl font-bold sm:text-3xl" data-testid="profile-display-name">
                  {user.displayName}
                </h1>
                <p className="text-sm text-muted">@{user.onlineId}</p>
              </div>
            </div>
            <div className="flex flex-wrap items-center gap-2">
              {primaryAction}
              {!isSelf && relationship.canMessage ? (
                <Button variant="secondary" onClick={() => start.mutate(user.id)} loading={start.isPending} icon={<MessageSquare aria-hidden className="size-4" />}>
                  Message
                </Button>
              ) : null}
              {!isSelf && relationship.state !== 'blocked' ? (
                <Menu
                  label={`More actions for ${user.displayName}`}
                  triggerClassName="h-10 border border-line-strong bg-surface-2 px-2.5 text-muted hover:bg-surface-3"
                  trigger={<MoreHorizontal aria-hidden className="size-4" />}
                  items={[
                    ...(relationship.state === 'friend'
                      ? [{ label: 'Remove friend', icon: <UserMinus />, onSelect: () => void actions.confirmRemove(user.id, user.displayName) }]
                      : []),
                    { label: 'Block', icon: <Ban />, tone: 'danger' as const, onSelect: () => void actions.confirmBlock(user.id, user.displayName) },
                  ]}
                />
              ) : null}
            </div>
          </div>

          <div className="mt-5 flex flex-wrap items-center gap-x-5 gap-y-2 text-sm text-muted">
            <span className="inline-flex items-center gap-1.5" data-testid="profile-presence">
              {presenceDescription(data.presence)}
            </span>
            <span className="inline-flex items-center gap-1.5">
              <CalendarDays aria-hidden className="size-4" /> Joined {longDate(data.joinedAt)}
            </span>
            {!isSelf && data.mutualFriendCount > 0 ? (
              <span className="inline-flex items-center gap-1.5">
                <Users aria-hidden className="size-4" /> {data.mutualFriendCount} mutual friend{data.mutualFriendCount === 1 ? '' : 's'}
              </span>
            ) : null}
          </div>

          {data.presence.activity ? (
            <p className="mt-3 inline-flex items-center gap-2 rounded-xl bg-accent-soft px-3 py-2 text-sm text-ink">
              <Gamepad2 aria-hidden className="size-4 text-accent" />
              {data.presence.activity}
              <span className="text-xs text-muted">(self-reported)</span>
            </p>
          ) : null}
        </div>
      </Card>

      <Card className="mt-6 p-5" aria-labelledby="game-activity-title">
        <h2 id="game-activity-title" className="mb-3 text-sm font-semibold uppercase tracking-wider text-subtle">
          Game activity
        </h2>
        {/* presence.game follows realtime updates; the server applies the same audience rules to both. */}
        {data.presence.game ? (
          <div className="mb-4">
            <GameActivityCard game={data.presence.game} />
          </div>
        ) : null}
        {activity.isPending ? (
          <p className="text-sm text-muted">Loading activity…</p>
        ) : activity.isError ? (
          <p className="text-sm text-muted">Game activity could not be loaded.</p>
        ) : (
          <GameHistory activity={activity.data} isSelf={isSelf} />
        )}
      </Card>

      {data.restricted ? (
        <div className="mt-6">
          <Notice icon={<Lock className="size-4" />}>
            {relationship.state === 'blocked'
              ? 'You blocked this player. Unblock them to see their profile again.'
              : 'This player only shares profile details with friends.'}
          </Notice>
        </div>
      ) : (
        <div className="mt-6 grid gap-6 md:grid-cols-[1.4fr_1fr]">
          <Card className="p-5">
            <h2 className="mb-3 text-sm font-semibold uppercase tracking-wider text-subtle">About</h2>
            {data.statusMessage ? (
              <p className="mb-3 inline-flex items-center gap-2 text-sm font-medium text-ink" data-testid="profile-status-message">
                <Sparkles aria-hidden className="size-4 text-violet" /> {data.statusMessage}
              </p>
            ) : null}
            <p className="whitespace-pre-wrap text-sm leading-relaxed text-muted" data-testid="profile-bio">
              {data.bio || (isSelf ? 'You have not written a bio yet.' : 'No bio yet.')}
            </p>
          </Card>
          <Card className="p-5">
            <h2 className="mb-3 text-sm font-semibold uppercase tracking-wider text-subtle">Favorite games</h2>
            {data.favoriteGames.length === 0 ? (
              <p className="text-sm text-muted">{isSelf ? <Link to="/settings" className="text-accent hover:underline">Add your favorite games</Link> : 'None listed.'}</p>
            ) : (
              <ul className="flex flex-wrap gap-2">
                {data.favoriteGames.map((game) => (
                  <li key={game} className="inline-flex items-center gap-1.5 rounded-lg border border-line bg-surface-2 px-2.5 py-1 text-sm">
                    <Gamepad2 aria-hidden className="size-3.5 text-accent" />
                    {game}
                  </li>
                ))}
              </ul>
            )}
          </Card>
        </div>
      )}
    </div>
  );
}
