import { useMutation } from '@tanstack/react-query';
import { Check, Search, Send } from 'lucide-react';
import { useState } from 'react';
import type { UserSummary } from '@akeno/shared';
import { Button } from '../../components/Button';
import { Modal } from '../../components/Modal';
import { EmptyState } from '../../components/States';
import { useToast } from '../../components/Toast';
import { UserRow } from '../../components/UserRow';
import { useDebounce } from '../../hooks/useDebounce';
import { api, errorMessage } from '../../lib/api';
import { useFriends, useUserSearch } from '../../lib/queries';

/**
 * Invite people to a group, party or game session. Lists friends; when `searchAll` is set
 * (groups), any member of the network can be found by Online ID.
 */
export function InviteDialog({
  open,
  onClose,
  title,
  invitePath,
  excludeIds,
  searchAll = false,
}: {
  open: boolean;
  onClose: () => void;
  title: string;
  invitePath: string;
  excludeIds: string[];
  searchAll?: boolean;
}) {
  const toast = useToast();
  const friends = useFriends();
  const [term, setTerm] = useState('');
  const debounced = useDebounce(term, 300);
  const search = useUserSearch(searchAll ? debounced : '');
  const [invited, setInvited] = useState<Set<string>>(new Set());

  const invite = useMutation({
    mutationFn: (userId: string) => api.post(invitePath, { userId }),
    onSuccess: (_result, userId) => {
      setInvited((current) => new Set(current).add(userId));
      toast.success('Invitation sent.');
    },
    onError: (error) => toast.error('Could not send the invitation', errorMessage(error)),
  });

  const filter = term.trim().toLowerCase();
  const fromFriends: UserSummary[] = (friends.data ?? []).filter(
    (friend) => !excludeIds.includes(friend.id) && `${friend.displayName} ${friend.onlineId}`.toLowerCase().includes(filter),
  );
  const fromSearch: UserSummary[] =
    searchAll && filter
      ? (search.data ?? []).filter((user) => !excludeIds.includes(user.id) && !fromFriends.some((friend) => friend.id === user.id))
      : [];
  const people = [...fromFriends, ...fromSearch];

  return (
    <Modal open={open} onClose={onClose} title={title} description={searchAll ? 'Invite friends or find players by Online ID.' : 'Invite your friends.'}>
      <label className="relative mb-3 block">
        <span className="sr-only">{searchAll ? 'Search players' : 'Filter friends'}</span>
        <Search aria-hidden className="absolute left-3 top-1/2 size-4 -translate-y-1/2 text-subtle" />
        <input
          value={term}
          onChange={(event) => setTerm(event.target.value)}
          placeholder={searchAll ? 'Search players' : 'Filter friends'}
          className="h-10 w-full rounded-xl border border-line-strong bg-surface-2 pl-9 pr-3 text-sm focus:border-accent focus:outline-none"
        />
      </label>
      {people.length === 0 ? (
        <EmptyState title="Nobody to invite" description={searchAll ? 'Try another Online ID.' : 'All of your friends are already here, or you have not added friends yet.'} />
      ) : (
        <ul className="flex flex-col divide-y divide-line">
          {people.map((user) => {
            const done = invited.has(user.id);
            return (
              <li key={user.id} className="py-2.5">
                <UserRow
                  user={user}
                  size="sm"
                  linkToProfile={false}
                  actions={
                    done ? (
                      <span className="inline-flex items-center gap-1 text-xs font-medium text-success">
                        <Check aria-hidden className="size-3.5" /> Invited
                      </span>
                    ) : (
                      <Button
                        size="sm"
                        variant="subtle"
                        loading={invite.isPending && invite.variables === user.id}
                        onClick={() => invite.mutate(user.id)}
                        icon={<Send aria-hidden className="size-3.5" />}
                        aria-label={`Invite ${user.displayName}`}
                      >
                        Invite
                      </Button>
                    )
                  }
                />
              </li>
            );
          })}
        </ul>
      )}
    </Modal>
  );
}
