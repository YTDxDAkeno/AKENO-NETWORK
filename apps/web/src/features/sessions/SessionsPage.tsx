import { Gamepad2, Info, Plus, Search } from 'lucide-react';
import { useState } from 'react';
import { GAME_PLATFORMS } from '@akeno/shared';
import { Button } from '../../components/Button';
import { Card } from '../../components/Card';
import { SelectField, Switch } from '../../components/Field';
import { PageHeader } from '../../components/PageHeader';
import { Skeleton } from '../../components/Spinner';
import { EmptyState, ErrorState, Notice } from '../../components/States';
import { Tabs } from '../../components/Tabs';
import { useDebounce } from '../../hooks/useDebounce';
import { useGameSessions, useMyGameSessions } from '../../lib/queries';
import { SessionCard } from './SessionCard';
import { SessionFormDialog } from './SessionFormDialog';

type Tab = 'browse' | 'mine';

export function SessionsPage() {
  const [tab, setTab] = useState<Tab>('browse');
  const [createOpen, setCreateOpen] = useState(false);
  const [q, setQ] = useState('');
  const [status, setStatus] = useState('');
  const [platform, setPlatform] = useState('');
  const [joinable, setJoinable] = useState(false);
  const debouncedQ = useDebounce(q, 300);
  const sessions = useGameSessions({ q: debouncedQ.trim() || undefined, status: status || undefined, platform: platform || undefined, joinable });
  const mine = useMyGameSessions();
  const list = tab === 'browse' ? sessions : mine;

  return (
    <>
      <PageHeader
        title="Game Sessions"
        description="Find a lobby or host your own and invite friends."
        icon={<Gamepad2 />}
        actions={
          <Button variant="primary" onClick={() => setCreateOpen(true)} icon={<Plus aria-hidden className="size-4" />}>
            Host session
          </Button>
        }
      />
      <div className="mb-5">
        <Notice icon={<Info className="size-4" />}>
          Game sessions are coordination lobbies inside AKENO NETWORK. Joining one does not connect you to a game or a console.
        </Notice>
      </div>
      <Tabs<Tab>
        className="mb-4 w-fit"
        label="Session views"
        value={tab}
        onChange={setTab}
        items={[
          { value: 'browse', label: 'Browse' },
          { value: 'mine', label: 'My sessions' },
        ]}
      />
      {tab === 'browse' ? (
        <Card className="mb-5 grid gap-4 p-4 sm:grid-cols-2 lg:grid-cols-[2fr_1fr_1fr_auto] lg:items-end">
          <label className="relative block">
            <span className="mb-1.5 block text-sm font-medium">Search</span>
            <Search aria-hidden className="absolute bottom-3.5 left-3.5 size-4 text-subtle" />
            <input
              type="search"
              value={q}
              onChange={(event) => setQ(event.target.value)}
              placeholder="Game or description"
              className="h-11 w-full rounded-xl border border-line-strong bg-surface-2 pl-10 pr-3 text-sm focus:border-accent focus:outline-none focus:ring-2 focus:ring-accent/30"
            />
          </label>
          <SelectField
            label="Status"
            value={status}
            onChange={(event) => setStatus(event.target.value)}
            options={[
              { value: '', label: 'Open sessions' },
              { value: 'waiting', label: 'Waiting for players' },
              { value: 'active', label: 'In progress' },
            ]}
          />
          <SelectField
            label="Platform"
            value={platform}
            onChange={(event) => setPlatform(event.target.value)}
            options={[{ value: '', label: 'Any platform' }, ...GAME_PLATFORMS.map((value) => ({ value, label: value }))]}
          />
          <div className="pb-2.5">
            <Switch label="Has free slots" checked={joinable} onChange={setJoinable} />
          </div>
        </Card>
      ) : null}
      {list.isPending ? (
        <div className="grid gap-4 sm:grid-cols-2 xl:grid-cols-3">
          {[0, 1, 2].map((index) => (
            <Skeleton key={index} className="h-52" />
          ))}
        </div>
      ) : list.isError ? (
        <ErrorState error={list.error} onRetry={() => void list.refetch()} />
      ) : list.data.length === 0 ? (
        <Card>
          <EmptyState
            icon={<Gamepad2 className="size-6" />}
            title={tab === 'browse' ? 'No sessions match' : 'You have not joined any sessions'}
            description={tab === 'browse' ? 'Try different filters, or host a session yourself.' : 'Host a session or join one from the directory.'}
            action={
              <Button variant="primary" onClick={() => setCreateOpen(true)}>
                Host session
              </Button>
            }
          />
        </Card>
      ) : (
        <div className="grid gap-4 sm:grid-cols-2 xl:grid-cols-3">
          {list.data.map((session) => (
            <SessionCard key={session.id} session={session} />
          ))}
        </div>
      )}
      {createOpen ? <SessionFormDialog open onClose={() => setCreateOpen(false)} /> : null}
    </>
  );
}
