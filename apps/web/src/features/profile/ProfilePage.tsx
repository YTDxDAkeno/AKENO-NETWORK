import { useMutation, useQueryClient } from '@tanstack/react-query';
import {
  Ban,
  CalendarDays,
  Clock,
  Gamepad2,
  Lock,
  MessageSquare,
  MoreHorizontal,
  PartyPopper,
  Pencil,
  Sparkles,
  UserCheck,
  UserMinus,
  UserPlus,
  Users,
} from 'lucide-react';
import { Link, useNavigate, useParams } from 'react-router';
import type { GameActivityResponse, PublicProfile } from '@akeno/shared';
import { Avatar } from '../../components/Avatar';
import { Badge } from '../../components/Badge';
import { Button, ButtonLink } from '../../components/Button';
import { Card } from '../../components/Card';
import { GameActivityCard, GameHistory } from '../../components/GameActivity';
import { GameArt } from '../../components/GameArt';
import { Menu } from '../../components/Menu';
import { PageLoader } from '../../components/Spinner';
import { EmptyState, ErrorState, Notice } from '../../components/States';
import { useToast } from '../../components/Toast';
import { useDocumentTitle } from '../../hooks/useDocumentTitle';
import { ApiError, api, errorMessage } from '../../lib/api';
import { formatPlaytime, longDate, presenceDescription } from '../../lib/format';
import { useCurrentParty, useGameActivity, useProfile } from '../../lib/queries';
import { queryKeys } from '../../lib/query-keys';
import { themeStyle, useColorMode } from '../../lib/theme';
import { useFriendActions } from '../friends/useFriendActions';
import { useStartConversation } from '../messages/useStartConversation';
import { ProfileHeader } from './ProfileHeader';

function SectionTitle({ id, children, action }: { id?: string; children: React.ReactNode; action?: React.ReactNode }) {
  return (
    <div className="mb-3 flex items-center justify-between gap-3">
      <h2 id={id} className="text-sm font-semibold uppercase tracking-wider text-subtle">
        {children}
      </h2>
      {action}
    </div>
  );
}

function Stat({ label, value, hint }: { label: string; value: string; hint?: string }) {
  return (
    <div className="flex items-baseline justify-between gap-3 py-2" title={hint}>
      <dt className="text-sm text-muted">{label}</dt>
      <dd className="text-base font-semibold text-ink">{value}</dd>
    </div>
  );
}

function ActivityStats({ activity }: { activity: GameActivityResponse }) {
  if (!activity.historyVisible || activity.totals.sessions === 0) return null;
  return (
    <dl className="divide-y divide-line" data-testid="activity-stats">
      <Stat
        label="Tracked playtime"
        value={formatPlaytime(activity.totals.seconds)}
        hint={activity.totals.seconds === null ? 'This player does not share their playtime' : 'Time confirmed by the paired console since tracking began'}
      />
      <Stat label="Games played" value={String(activity.totals.games)} />
      <Stat label="Sessions" value={String(activity.totals.sessions)} />
      {activity.trackedSince ? <Stat label="Tracked since" value={longDate(activity.trackedSince)} /> : null}
    </dl>
  );
}

function PartyAvailability({ profile }: { profile: PublicProfile }) {
  const toast = useToast();
  const navigate = useNavigate();
  const queryClient = useQueryClient();
  const join = useMutation({
    mutationFn: () => api.post(`/parties/${profile.party!.id}/join`),
    onSuccess: () => {
      void queryClient.invalidateQueries({ queryKey: queryKeys.allParties });
      toast.success('You joined the party.');
      void navigate('/parties');
    },
    onError: (error) => toast.error('Could not join the party', errorMessage(error)),
  });
  if (!profile.party) return null;
  const { party } = profile;
  const full = party.memberCount >= party.maxMembers;
  return (
    <Card className="p-4">
      <SectionTitle>Party</SectionTitle>
      <div className="flex items-center gap-3">
        <span className="grid size-10 shrink-0 place-items-center rounded-xl bg-accent-soft text-accent">
          <PartyPopper aria-hidden className="size-5" />
        </span>
        <div className="min-w-0 flex-1">
          <p className="truncate font-medium text-ink">{party.name}</p>
          <p className="text-xs text-muted">
            {party.memberCount}/{party.maxMembers} members
          </p>
        </div>
        {party.isMember ? (
          <ButtonLink to="/parties" size="sm" variant="subtle">
            Open
          </ButtonLink>
        ) : profile.relationship.state !== 'self' ? (
          <Button size="sm" variant="primary" disabled={full} loading={join.isPending} onClick={() => join.mutate()}>
            {full ? 'Full' : 'Join'}
          </Button>
        ) : null}
      </div>
    </Card>
  );
}

export function ProfilePage() {
  const { handle = '' } = useParams();
  const profile = useProfile(handle);
  const activity = useGameActivity(profile.data?.user.id);
  const actions = useFriendActions();
  const start = useStartConversation();
  const currentParty = useCurrentParty();
  const toast = useToast();
  const mode = useColorMode();
  useDocumentTitle(profile.data ? profile.data.user.displayName : 'Profile');

  const invite = useMutation({
    mutationFn: ({ partyId, userId }: { partyId: string; userId: string }) => api.post(`/parties/${partyId}/invitations`, { userId }),
    onSuccess: () => toast.success('Party invitation sent.'),
    onError: (error) => toast.error('Could not send the invitation', errorMessage(error)),
  });

  if (profile.isPending) return <PageLoader label="Loading profile…" />;
  if (profile.isError) {
    if (profile.error instanceof ApiError && profile.error.status === 404) {
      return (
        <Card>
          <EmptyState
            icon={<Users className="size-6" />}
            title="Player not found"
            description="This profile does not exist or is not available to you."
            action={<ButtonLink to="/friends">Back to friends</ButtonLink>}
          />
        </Card>
      );
    }
    return <ErrorState error={profile.error} onRetry={() => void profile.refetch()} />;
  }

  const data = profile.data;
  const { user, relationship } = data;
  const isSelf = relationship.state === 'self';
  const myParty = currentParty.data;
  const canInvite = !isSelf && relationship.state === 'friend' && myParty && !myParty.members.some((member) => member.user.id === user.id);

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
          <Button variant="primary" loading={actions.sendRequest.isPending} onClick={() => actions.sendRequest.mutate(user.id)} icon={<UserPlus aria-hidden className="size-4" />}>
            Add friend
          </Button>
        ) : (
          <Badge>Not accepting friend requests</Badge>
        );
    }
  })();

  const headerActions = (
    <>
      {primaryAction}
      {!isSelf && relationship.canMessage ? (
        <Button variant="secondary" onClick={() => start.mutate(user.id)} loading={start.isPending} icon={<MessageSquare aria-hidden className="size-4" />}>
          Message
        </Button>
      ) : null}
      {canInvite ? (
        <Button
          variant="secondary"
          onClick={() => invite.mutate({ partyId: myParty.id, userId: user.id })}
          loading={invite.isPending}
          icon={<PartyPopper aria-hidden className="size-4" />}
        >
          Invite to party
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
    </>
  );

  const otherBadges = data.badges.map((badge) => badge.id).filter((id) => !data.featuredBadges.includes(id));
  const game = data.presence.game;

  return (
    <div className="mx-auto max-w-5xl" style={themeStyle(data.appearance, mode)} data-testid="profile-page" data-theme-id={data.appearance.theme}>
      <section className="card-tinted overflow-hidden" aria-label="Profile">
        <ProfileHeader
          identity={user}
          appearance={data.appearance}
          status={data.presence.status}
          featuredBadges={data.featuredBadges}
          otherBadges={otherBadges}
          actions={headerActions}
          meta={
            <div className="flex flex-col gap-3">
              {data.statusMessage ? (
                <p className="inline-flex items-center gap-2 text-sm font-medium text-ink" data-testid="profile-status-message">
                  <Sparkles aria-hidden className="size-4 text-accent" /> {data.statusMessage}
                </p>
              ) : null}
              <div className="flex flex-wrap items-center gap-x-5 gap-y-2 text-sm text-muted">
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
                <p className="inline-flex w-fit items-center gap-2 rounded-xl bg-accent-soft px-3 py-2 text-sm text-ink">
                  <Gamepad2 aria-hidden className="size-4 text-accent" />
                  {data.presence.activity}
                  <span className="text-xs text-muted">(self-reported)</span>
                </p>
              ) : null}
            </div>
          }
        />
      </section>

      {data.restricted ? (
        <div className="mt-6">
          <Notice icon={<Lock className="size-4" />}>
            {relationship.state === 'blocked'
              ? 'You blocked this player. Unblock them to see their profile again.'
              : 'This player only shares profile details with friends.'}
          </Notice>
        </div>
      ) : null}

      <div className="mt-6 grid gap-6 lg:grid-cols-[minmax(0,1fr)_320px]">
        <div className="flex min-w-0 flex-col gap-6">
          {/* presence.game follows realtime updates; the server applies the same audience rules to both. */}
          {game ? (
            <section aria-labelledby="now-playing-title">
              <SectionTitle id="now-playing-title">Now playing</SectionTitle>
              <GameActivityCard game={game} />
            </section>
          ) : null}

          <Card className="p-5" aria-labelledby="recent-games-title">
            <SectionTitle id="recent-games-title" action={<Clock aria-hidden className="size-4 text-subtle" />}>
              Recently played
            </SectionTitle>
            {activity.isPending ? (
              <p className="text-sm text-muted">Loading activity…</p>
            ) : activity.isError ? (
              <p className="text-sm text-muted">Game activity could not be loaded.</p>
            ) : (
              <GameHistory activity={activity.data} isSelf={isSelf} />
            )}
          </Card>

          {!data.restricted ? (
            <Card className="p-5" aria-labelledby="favorites-title">
              <SectionTitle id="favorites-title">Favorite games</SectionTitle>
              {data.favoriteGameDetails.length === 0 ? (
                <p className="text-sm text-muted">
                  {isSelf ? (
                    <Link to="/settings" className="text-accent hover:underline">
                      Add your favorite games
                    </Link>
                  ) : (
                    'None listed.'
                  )}
                </p>
              ) : (
                <ul className="grid grid-cols-3 gap-3 sm:grid-cols-6" data-testid="favorite-games">
                  {data.favoriteGameDetails.map((favorite) => (
                    <li key={favorite.name} className="min-w-0">
                      <GameArt art={favorite.art} name={favorite.name} variant="cover" sizes="120px" className="w-full" />
                      <p className="mt-1.5 truncate text-xs font-medium text-ink" title={favorite.name}>
                        {favorite.name}
                      </p>
                    </li>
                  ))}
                </ul>
              )}
            </Card>
          ) : null}
        </div>

        <aside className="flex flex-col gap-6" aria-label="Profile details">
          {activity.data ? (
            activity.data.historyVisible && activity.data.totals.sessions > 0 ? (
              <Card className="p-4">
                <SectionTitle>Gaming activity</SectionTitle>
                <ActivityStats activity={activity.data} />
              </Card>
            ) : null
          ) : null}

          <PartyAvailability profile={data} />

          {!data.restricted ? (
            <Card className="p-4">
              <SectionTitle>About</SectionTitle>
              <p className="whitespace-pre-wrap text-sm leading-relaxed text-muted" data-testid="profile-bio">
                {data.bio || (isSelf ? 'You have not written a bio yet.' : 'No bio yet.')}
              </p>
            </Card>
          ) : null}

          {data.mutualFriends.length > 0 ? (
            <Card className="p-4">
              <SectionTitle>Mutual friends</SectionTitle>
              <ul className="flex flex-col gap-2.5">
                {data.mutualFriends.map((friend) => (
                  <li key={friend.id}>
                    <Link to={`/u/${friend.onlineId}`} className="flex items-center gap-2.5 rounded-lg hover:bg-surface-2">
                      <Avatar avatar={friend.avatar} image={friend.avatarImage} name={friend.displayName} size="sm" />
                      <span className="min-w-0">
                        <span className="block truncate text-sm font-medium text-ink">{friend.displayName}</span>
                        <span className="block truncate text-xs text-subtle">@{friend.onlineId}</span>
                      </span>
                    </Link>
                  </li>
                ))}
              </ul>
            </Card>
          ) : null}
        </aside>
      </div>
    </div>
  );
}
