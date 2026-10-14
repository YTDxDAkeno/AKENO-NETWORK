import { useMutation, useQueryClient } from '@tanstack/react-query';
import { Globe, Lock, Plus, UsersRound } from 'lucide-react';
import { useState } from 'react';
import { Link } from 'react-router';
import type { GroupDto } from '@akeno/shared';
import { Badge, CountBadge } from '../../components/Badge';
import { Button } from '../../components/Button';
import { Card, CardHeader } from '../../components/Card';
import { PageHeader } from '../../components/PageHeader';
import { Skeleton } from '../../components/Spinner';
import { EmptyState, ErrorState } from '../../components/States';
import { useToast } from '../../components/Toast';
import { api, errorMessage } from '../../lib/api';
import { pluralize } from '../../lib/format';
import { useDiscoverGroups, useGroups } from '../../lib/queries';
import { queryKeys } from '../../lib/query-keys';
import { GroupFormDialog } from './GroupFormDialog';

const ROLE_LABEL = { owner: 'Owner', moderator: 'Moderator', member: 'Member' } as const;

export function GroupCard({ group }: { group: GroupDto }) {
  return (
    <Link to={`/groups/${group.id}`} className="card group flex flex-col gap-3 p-5 transition-shadow hover:shadow-glow" data-testid="group-card">
      <div className="flex items-start justify-between gap-3">
        <div className="flex min-w-0 items-center gap-3">
          <span className="inline-flex size-11 shrink-0 items-center justify-center rounded-xl bg-accent-soft font-display text-lg font-bold text-accent">
            {group.name.slice(0, 1).toUpperCase()}
          </span>
          <div className="min-w-0">
            <h3 className="truncate font-semibold text-ink group-hover:text-accent">{group.name}</h3>
            <p className="text-xs text-muted">{pluralize(group.memberCount, 'member')}</p>
          </div>
        </div>
        <CountBadge count={group.unreadCount} label={`${group.unreadCount} unread messages`} />
      </div>
      {group.description ? <p className="line-clamp-2 text-sm text-muted">{group.description}</p> : null}
      <div className="mt-auto flex flex-wrap gap-2">
        {group.myRole ? <Badge tone={group.myRole === 'owner' ? 'violet' : 'accent'}>{ROLE_LABEL[group.myRole]}</Badge> : null}
        <Badge icon={group.joinPolicy === 'open' ? <Globe aria-hidden className="size-3" /> : <Lock aria-hidden className="size-3" />}>
          {group.joinPolicy === 'open' ? 'Open' : 'Invitation only'}
        </Badge>
      </div>
    </Link>
  );
}

function DiscoverGroups() {
  const groups = useDiscoverGroups();
  const queryClient = useQueryClient();
  const toast = useToast();
  const join = useMutation({
    mutationFn: (id: string) => api.post(`/groups/${id}/join`),
    onSuccess: () => {
      void queryClient.invalidateQueries({ queryKey: queryKeys.groups });
      void queryClient.invalidateQueries({ queryKey: queryKeys.groupsDiscover });
      toast.success('You joined the group.');
    },
    onError: (error) => toast.error('Could not join the group', errorMessage(error)),
  });
  return (
    <Card>
      <CardHeader title="Open groups" description="Public groups anyone on the network can join." icon={<Globe className="size-5" />} />
      {groups.isPending ? (
        <Skeleton className="m-5 h-16" />
      ) : groups.isError ? (
        <ErrorState error={groups.error} onRetry={() => void groups.refetch()} />
      ) : groups.data.length === 0 ? (
        <EmptyState title="No open groups to join" description="Open groups created by others show up here." />
      ) : (
        <ul className="divide-y divide-line">
          {groups.data.map((group) => (
            <li key={group.id} className="flex items-center gap-3 px-5 py-3">
              <div className="min-w-0 flex-1">
                <Link to={`/groups/${group.id}`} className="block truncate font-medium hover:underline">
                  {group.name}
                </Link>
                <p className="truncate text-xs text-muted">
                  {pluralize(group.memberCount, 'member')} · created by {group.owner.displayName}
                </p>
              </div>
              <Button size="sm" variant="subtle" loading={join.isPending && join.variables === group.id} onClick={() => join.mutate(group.id)}>
                Join
              </Button>
            </li>
          ))}
        </ul>
      )}
    </Card>
  );
}

export function GroupsPage() {
  const groups = useGroups();
  const [createOpen, setCreateOpen] = useState(false);
  return (
    <>
      <PageHeader
        title="Groups"
        description="Persistent communities with members, roles and a shared chat."
        icon={<UsersRound />}
        actions={
          <Button variant="primary" onClick={() => setCreateOpen(true)} icon={<Plus aria-hidden className="size-4" />}>
            Create group
          </Button>
        }
      />
      <section aria-labelledby="my-groups" className="mb-8">
        <h2 id="my-groups" className="mb-3 text-sm font-semibold uppercase tracking-wider text-subtle">
          Your groups
        </h2>
        {groups.isPending ? (
          <div className="grid gap-4 sm:grid-cols-2 xl:grid-cols-3">
            <Skeleton className="h-36" />
            <Skeleton className="h-36" />
          </div>
        ) : groups.isError ? (
          <ErrorState error={groups.error} onRetry={() => void groups.refetch()} />
        ) : groups.data.length === 0 ? (
          <Card>
            <EmptyState
              icon={<UsersRound className="size-6" />}
              title="You are not in any groups yet"
              description="Create a group for your crew or accept an invitation."
              action={
                <Button variant="primary" onClick={() => setCreateOpen(true)}>
                  Create your first group
                </Button>
              }
            />
          </Card>
        ) : (
          <div className="grid gap-4 sm:grid-cols-2 xl:grid-cols-3">
            {groups.data.map((group) => (
              <GroupCard key={group.id} group={group} />
            ))}
          </div>
        )}
      </section>
      <DiscoverGroups />
      {createOpen ? <GroupFormDialog open onClose={() => setCreateOpen(false)} /> : null}
    </>
  );
}
