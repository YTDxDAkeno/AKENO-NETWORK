import { Compass, Gamepad2, Search, UsersRound } from 'lucide-react';
import { ButtonLink } from '../../components/Button';
import { Card, CardHeader } from '../../components/Card';
import { PageHeader } from '../../components/PageHeader';
import { Skeleton } from '../../components/Spinner';
import { EmptyState } from '../../components/States';
import { useDiscoverGroups, useGameSessions } from '../../lib/queries';
import { UserSearch } from '../friends/UserSearch';
import { GroupCard } from '../groups/GroupsPage';
import { SessionCard } from '../sessions/SessionCard';

export function DiscoverPage() {
  const groups = useDiscoverGroups();
  const sessions = useGameSessions({ joinable: true });
  return (
    <>
      <PageHeader title="Discover" description="Find players, open groups and sessions across the network." icon={<Compass />} />
      <div className="flex flex-col gap-6">
        <Card>
          <CardHeader title="Find players" icon={<Search className="size-5" />} />
          <div className="p-5">
            <UserSearch />
          </div>
        </Card>
        <section aria-labelledby="discover-groups">
          <div className="mb-3 flex items-center justify-between">
            <h2 id="discover-groups" className="flex items-center gap-2 text-lg font-semibold">
              <UsersRound aria-hidden className="size-5 text-accent" /> Open groups
            </h2>
            <ButtonLink to="/groups" size="sm" variant="ghost">
              Groups
            </ButtonLink>
          </div>
          {groups.isPending ? (
            <Skeleton className="h-32" />
          ) : groups.data && groups.data.length > 0 ? (
            <div className="grid gap-4 sm:grid-cols-2 xl:grid-cols-3">
              {groups.data.map((group) => (
                <GroupCard key={group.id} group={group} />
              ))}
            </div>
          ) : (
            <Card>
              <EmptyState title="No open groups yet" description="Open groups appear here for everyone to join." />
            </Card>
          )}
        </section>
        <section aria-labelledby="discover-sessions">
          <h2 id="discover-sessions" className="mb-3 flex items-center gap-2 text-lg font-semibold">
            <Gamepad2 aria-hidden className="size-5 text-accent" /> Joinable sessions
          </h2>
          {sessions.isPending ? (
            <Skeleton className="h-40" />
          ) : sessions.data && sessions.data.length > 0 ? (
            <div className="grid gap-4 sm:grid-cols-2 xl:grid-cols-3">
              {sessions.data.map((session) => (
                <SessionCard key={session.id} session={session} />
              ))}
            </div>
          ) : (
            <Card>
              <EmptyState title="No joinable sessions" description="Public and friends' sessions with free slots appear here." />
            </Card>
          )}
        </section>
      </div>
    </>
  );
}
