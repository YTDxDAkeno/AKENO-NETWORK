import { Check, Clock, Search, UserPlus } from 'lucide-react';
import { useState } from 'react';
import { Link } from 'react-router';
import type { UserSearchResult } from '@akeno/shared';
import { Button } from '../../components/Button';
import { Spinner } from '../../components/Spinner';
import { EmptyState, ErrorState } from '../../components/States';
import { UserRow } from '../../components/UserRow';
import { useDebounce } from '../../hooks/useDebounce';
import { useFriendRequests, useUserSearch } from '../../lib/queries';
import { useFriendActions } from './useFriendActions';

function ResultAction({ user }: { user: UserSearchResult }) {
  const actions = useFriendActions();
  const requests = useFriendRequests();
  if (user.relationship === 'friend') {
    return (
      <span className="inline-flex items-center gap-1 text-xs font-medium text-success">
        <Check aria-hidden className="size-3.5" /> Friends
      </span>
    );
  }
  if (user.relationship === 'request_sent') {
    return (
      <span className="inline-flex items-center gap-1 text-xs font-medium text-muted">
        <Clock aria-hidden className="size-3.5" /> Request sent
      </span>
    );
  }
  if (user.relationship === 'request_received') {
    const request = requests.data?.incoming.find((entry) => entry.user.id === user.id);
    return (
      <Button size="sm" variant="primary" disabled={!request} loading={actions.accept.isPending} onClick={() => request && actions.accept.mutate(request.id)}>
        Accept request
      </Button>
    );
  }
  return (
    <Button
      size="sm"
      variant="subtle"
      loading={actions.sendRequest.isPending && actions.sendRequest.variables === user.id}
      onClick={() => actions.sendRequest.mutate(user.id)}
      icon={<UserPlus aria-hidden className="size-4" />}
      aria-label={`Add ${user.displayName} as a friend`}
    >
      Add friend
    </Button>
  );
}

export function UserSearch({ autoFocus = false }: { autoFocus?: boolean }) {
  const [term, setTerm] = useState('');
  const debounced = useDebounce(term, 300);
  const results = useUserSearch(debounced);
  const searching = debounced.trim().length > 0;

  return (
    <div>
      <label className="relative block">
        <span className="sr-only">Search players by Online ID or display name</span>
        <Search aria-hidden className="absolute left-3.5 top-1/2 size-4 -translate-y-1/2 text-subtle" />
        <input
          type="search"
          value={term}
          onChange={(event) => setTerm(event.target.value)}
          placeholder="Search players by Online ID or name"
          autoFocus={autoFocus}
          maxLength={32}
          className="h-11 w-full rounded-xl border border-line-strong bg-surface-2 pl-10 pr-10 text-sm text-ink placeholder:text-subtle focus:border-accent focus:outline-none focus:ring-2 focus:ring-accent/30"
          data-testid="user-search"
        />
        {results.isFetching ? <Spinner className="absolute right-3.5 top-1/2 -translate-y-1/2 text-muted" label="Searching" /> : null}
      </label>
      {searching ? (
        <div className="mt-3">
          {results.isError ? (
            <ErrorState error={results.error} onRetry={() => void results.refetch()} />
          ) : results.data && results.data.length === 0 ? (
            <EmptyState title="No players found" description="Check the spelling of the Online ID and try again." />
          ) : (
            <ul className="flex flex-col divide-y divide-line" aria-label="Search results">
              {results.data?.map((user) => (
                <li key={user.id} className="py-2.5">
                  <UserRow
                    user={user}
                    secondary={<Link to={`/u/${user.onlineId}`} className="hover:underline">View profile</Link>}
                    actions={<ResultAction user={user} />}
                  />
                </li>
              ))}
            </ul>
          )}
        </div>
      ) : null}
    </div>
  );
}
