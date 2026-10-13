import { useMutation, useQueryClient } from '@tanstack/react-query';
import { Activity, Copy, KeyRound, Plus, ScrollText, Search, ShieldCheck, Users } from 'lucide-react';
import { useState, type FormEvent } from 'react';
import { LIMITS, type AdminUserDto, type CreatedRegistrationInvitation, type RegistrationInvitationStatus } from '@akeno/shared';
import { Badge } from '../../components/Badge';
import { Button } from '../../components/Button';
import { Card, CardHeader } from '../../components/Card';
import { useConfirm } from '../../components/ConfirmDialog';
import { SelectField, TextField } from '../../components/Field';
import { Modal } from '../../components/Modal';
import { PageHeader } from '../../components/PageHeader';
import { RelativeTime } from '../../components/RelativeTime';
import { Skeleton } from '../../components/Spinner';
import { EmptyState, ErrorState, Notice } from '../../components/States';
import { Tabs } from '../../components/Tabs';
import { useToast } from '../../components/Toast';
import { UserRow } from '../../components/UserRow';
import { useDebounce } from '../../hooks/useDebounce';
import { api, errorMessage } from '../../lib/api';
import { useAdminInvitations, useAdminStats, useAdminUsers, useAuditEvents } from '../../lib/queries';
import { queryKeys } from '../../lib/query-keys';
import { useCurrentUser } from '../auth/AuthProvider';

type Tab = 'invitations' | 'users' | 'audit' | 'system';

const STATUS_TONE: Record<RegistrationInvitationStatus, 'success' | 'neutral' | 'warning' | 'danger'> = {
  active: 'success',
  exhausted: 'neutral',
  expired: 'warning',
  revoked: 'danger',
};

function InvitationsPanel() {
  const invitations = useAdminInvitations();
  const queryClient = useQueryClient();
  const toast = useToast();
  const confirm = useConfirm();
  const [form, setForm] = useState({ maxUses: '1', expiresInHours: '72', note: '' });
  const [created, setCreated] = useState<CreatedRegistrationInvitation | null>(null);

  const create = useMutation({
    mutationFn: () =>
      api.post<CreatedRegistrationInvitation>('/admin/invitations', {
        maxUses: Number(form.maxUses),
        expiresInHours: form.expiresInHours === 'never' ? null : Number(form.expiresInHours),
        note: form.note,
      }),
    onSuccess: (result) => {
      setCreated(result);
      setForm((current) => ({ ...current, note: '' }));
      void queryClient.invalidateQueries({ queryKey: queryKeys.admin.invitations });
    },
    onError: (error) => toast.error('Could not create the invitation', errorMessage(error)),
  });
  const revoke = useMutation({
    mutationFn: (id: string) => api.delete(`/admin/invitations/${id}`),
    onSuccess: () => void queryClient.invalidateQueries({ queryKey: queryKeys.admin.invitations }),
    onError: (error) => toast.error('Could not revoke the invitation', errorMessage(error)),
  });

  const onSubmit = (event: FormEvent) => {
    event.preventDefault();
    create.mutate();
  };

  return (
    <div className="flex flex-col gap-6">
      <Card>
        <CardHeader title="Create invitation code" icon={<KeyRound className="size-5" />} description="Share codes privately. Only a hash is stored, so the code is shown once." />
        <form onSubmit={onSubmit} className="grid gap-4 p-5 sm:grid-cols-[1fr_1fr_2fr_auto] sm:items-end">
          <SelectField
            label="Uses"
            value={form.maxUses}
            onChange={(event) => setForm((current) => ({ ...current, maxUses: event.target.value }))}
            options={[1, 2, 3, 5, 10, 25].map((value) => ({ value: String(value), label: value === 1 ? '1 account' : `${value} accounts` }))}
          />
          <SelectField
            label="Expires"
            value={form.expiresInHours}
            onChange={(event) => setForm((current) => ({ ...current, expiresInHours: event.target.value }))}
            options={[
              { value: '24', label: 'In 24 hours' },
              { value: '72', label: 'In 3 days' },
              { value: '168', label: 'In 7 days' },
              { value: '720', label: 'In 30 days' },
              { value: 'never', label: 'Never' },
            ]}
          />
          <TextField
            label="Note"
            placeholder="Who is it for? (optional)"
            value={form.note}
            maxLength={LIMITS.invitationNoteMax}
            onChange={(event) => setForm((current) => ({ ...current, note: event.target.value }))}
          />
          <Button type="submit" variant="primary" loading={create.isPending} icon={<Plus aria-hidden className="size-4" />}>
            Create
          </Button>
        </form>
      </Card>

      <Card>
        <CardHeader title="Invitations" />
        {invitations.isPending ? (
          <Skeleton className="m-5 h-24" />
        ) : invitations.isError ? (
          <ErrorState error={invitations.error} onRetry={() => void invitations.refetch()} />
        ) : invitations.data.length === 0 ? (
          <EmptyState title="No invitations yet" description="Create a code to invite your first members." />
        ) : (
          <div className="overflow-x-auto">
            <table className="w-full min-w-[640px] text-left text-sm">
              <thead className="border-b border-line text-xs uppercase tracking-wider text-subtle">
                <tr>
                  <th className="px-5 py-2.5 font-medium">Code</th>
                  <th className="px-5 py-2.5 font-medium">Status</th>
                  <th className="px-5 py-2.5 font-medium">Used</th>
                  <th className="px-5 py-2.5 font-medium">Expires</th>
                  <th className="px-5 py-2.5 font-medium">Note</th>
                  <th className="px-5 py-2.5 font-medium">
                    <span className="sr-only">Actions</span>
                  </th>
                </tr>
              </thead>
              <tbody className="divide-y divide-line">
                {invitations.data.map((invitation) => (
                  <tr key={invitation.id}>
                    <td className="px-5 py-3 font-mono text-xs">{invitation.codeHint}-••••</td>
                    <td className="px-5 py-3">
                      <Badge tone={STATUS_TONE[invitation.status]}>{invitation.status}</Badge>
                    </td>
                    <td className="px-5 py-3">
                      {invitation.useCount}/{invitation.maxUses}
                    </td>
                    <td className="px-5 py-3 text-muted">{invitation.expiresAt ? <RelativeTime iso={invitation.expiresAt} /> : 'Never'}</td>
                    <td className="max-w-48 truncate px-5 py-3 text-muted">{invitation.note || '—'}</td>
                    <td className="px-5 py-3 text-right">
                      {invitation.status === 'active' ? (
                        <Button
                          size="sm"
                          variant="ghost"
                          onClick={async () => {
                            const ok = await confirm({ title: 'Revoke this invitation?', message: 'Nobody will be able to register with it anymore.', confirmLabel: 'Revoke' });
                            if (ok) revoke.mutate(invitation.id);
                          }}
                        >
                          Revoke
                        </Button>
                      ) : null}
                    </td>
                  </tr>
                ))}
              </tbody>
            </table>
          </div>
        )}
      </Card>

      <Modal
        open={created !== null}
        onClose={() => setCreated(null)}
        title="Invitation created"
        description="Copy this code now. For security, it cannot be shown again."
        footer={
          <Button variant="primary" onClick={() => setCreated(null)}>
            Done
          </Button>
        }
      >
        <div className="flex items-center gap-2 rounded-xl border border-line-strong bg-surface-2 p-3">
          <code className="flex-1 select-all text-center font-mono text-lg tracking-widest" data-testid="created-invitation-code">
            {created?.code}
          </code>
          <Button
            size="sm"
            variant="ghost"
            icon={<Copy aria-hidden className="size-4" />}
            onClick={() => {
              void navigator.clipboard
                ?.writeText(created?.code ?? '')
                .then(() => toast.success('Code copied.'))
                .catch(() => toast.error('Could not copy. Select the code and copy it manually.'));
            }}
          >
            Copy
          </Button>
        </div>
      </Modal>
    </div>
  );
}

function UsersPanel() {
  const me = useCurrentUser();
  const [q, setQ] = useState('');
  const debounced = useDebounce(q, 300);
  const users = useAdminUsers(debounced.trim());
  const queryClient = useQueryClient();
  const toast = useToast();
  const confirm = useConfirm();
  const update = useMutation({
    mutationFn: ({ id, patch }: { id: string; patch: Partial<Pick<AdminUserDto, 'status' | 'role'>> }) => api.patch(`/admin/users/${id}`, patch),
    onSuccess: () => {
      void queryClient.invalidateQueries({ queryKey: ['admin', 'users'] });
      toast.success('User updated.');
    },
    onError: (error) => toast.error('Could not update the user', errorMessage(error)),
  });

  return (
    <Card>
      <CardHeader title="Members" icon={<Users className="size-5" />} />
      <div className="border-b border-line p-4">
        <label className="relative block max-w-md">
          <span className="sr-only">Search members</span>
          <Search aria-hidden className="absolute left-3.5 top-1/2 size-4 -translate-y-1/2 text-subtle" />
          <input
            value={q}
            onChange={(event) => setQ(event.target.value)}
            placeholder="Search by Online ID or name"
            className="h-10 w-full rounded-xl border border-line-strong bg-surface-2 pl-10 pr-3 text-sm focus:border-accent focus:outline-none"
          />
        </label>
      </div>
      {users.isPending ? (
        <Skeleton className="m-5 h-24" />
      ) : users.isError ? (
        <ErrorState error={users.error} onRetry={() => void users.refetch()} />
      ) : (
        <ul className="divide-y divide-line">
          {users.data.map((user) => (
            <li key={user.id} className="flex flex-col gap-3 px-5 py-3 md:flex-row md:items-center">
              <UserRow
                className="flex-1"
                user={user}
                secondary={
                  <>
                    Joined <RelativeTime iso={user.createdAt} /> · {user.activeSessionCount} session{user.activeSessionCount === 1 ? '' : 's'}
                    {user.connected ? ' · connected now' : ''}
                  </>
                }
              />
              <div className="flex flex-wrap items-center gap-2">
                {user.role === 'admin' ? <Badge tone="violet">Admin</Badge> : null}
                {user.status === 'suspended' ? <Badge tone="danger">Suspended</Badge> : null}
                {user.id !== me.id ? (
                  <>
                    <Button
                      size="sm"
                      variant="ghost"
                      onClick={async () => {
                        const makeAdmin = user.role !== 'admin';
                        const ok = await confirm({
                          title: makeAdmin ? `Make ${user.displayName} an administrator?` : `Remove administrator rights from ${user.displayName}?`,
                          message: makeAdmin ? 'Administrators can create invitations, suspend members and read the audit log.' : 'They keep their account but lose access to administration.',
                          confirmLabel: makeAdmin ? 'Make administrator' : 'Remove rights',
                          tone: makeAdmin ? 'primary' : 'danger',
                        });
                        if (ok) update.mutate({ id: user.id, patch: { role: makeAdmin ? 'admin' : 'user' } });
                      }}
                    >
                      {user.role === 'admin' ? 'Remove admin' : 'Make admin'}
                    </Button>
                    {user.status === 'suspended' ? (
                      <Button size="sm" variant="secondary" onClick={() => update.mutate({ id: user.id, patch: { status: 'active' } })}>
                        Reactivate
                      </Button>
                    ) : (
                      <Button
                        size="sm"
                        variant="ghost"
                        className="text-danger hover:text-danger"
                        onClick={async () => {
                          const ok = await confirm({
                            title: `Suspend ${user.displayName}?`,
                            message: 'They are signed out everywhere and cannot sign in until reactivated.',
                            confirmLabel: 'Suspend',
                          });
                          if (ok) update.mutate({ id: user.id, patch: { status: 'suspended' } });
                        }}
                      >
                        Suspend
                      </Button>
                    )}
                  </>
                ) : (
                  <span className="text-xs text-subtle">This is you</span>
                )}
              </div>
            </li>
          ))}
        </ul>
      )}
    </Card>
  );
}

function AuditPanel() {
  const audit = useAuditEvents();
  const events = audit.data?.pages.flatMap((page) => page.events) ?? [];
  return (
    <Card>
      <CardHeader title="Audit log" icon={<ScrollText className="size-5" />} description="Security-relevant events. Message contents and secrets are never recorded." />
      {audit.isPending ? (
        <Skeleton className="m-5 h-24" />
      ) : audit.isError ? (
        <ErrorState error={audit.error} onRetry={() => void audit.refetch()} />
      ) : (
        <>
          <ul className="divide-y divide-line">
            {events.map((event) => (
              <li key={event.id} className="flex flex-col gap-1 px-5 py-2.5 sm:flex-row sm:items-center sm:gap-4">
                <code className="w-60 shrink-0 text-xs text-accent">{event.action}</code>
                <span className="flex-1 truncate text-sm text-muted">
                  {event.actor ? event.actor.displayName : 'System / anonymous'}
                  {event.targetType ? ` → ${event.targetType}` : ''}
                  {event.ipAddress ? ` · ${event.ipAddress}` : ''}
                </span>
                <RelativeTime iso={event.createdAt} className="text-xs text-subtle" />
              </li>
            ))}
          </ul>
          {audit.hasNextPage ? (
            <div className="border-t border-line p-3 text-center">
              <Button size="sm" variant="ghost" loading={audit.isFetchingNextPage} onClick={() => void audit.fetchNextPage()}>
                Load more
              </Button>
            </div>
          ) : null}
        </>
      )}
    </Card>
  );
}

function formatBytes(bytes: number): string {
  return `${(bytes / 1024 / 1024).toFixed(1)} MB`;
}

function SystemPanel() {
  const stats = useAdminStats();
  if (stats.isPending) return <Skeleton className="h-40" />;
  if (stats.isError) return <ErrorState error={stats.error} onRetry={() => void stats.refetch()} />;
  const data = stats.data;
  const tiles = [
    { label: 'Members', value: `${data.users.active} active`, detail: `${data.users.suspended} suspended · ${data.users.total} total` },
    { label: 'Connected now', value: `${data.realtime.connectedUsers} users`, detail: `${data.realtime.connections} live connections` },
    { label: 'Server memory', value: formatBytes(data.process.rssBytes), detail: `Heap ${formatBytes(data.process.heapUsedBytes)} · Node ${data.process.nodeVersion}` },
    { label: 'Database pool', value: `${data.database.totalConnections} connections`, detail: `${data.database.idleConnections} idle · ${data.database.waitingRequests} waiting` },
    { label: 'Uptime', value: `${Math.floor(data.process.uptimeSeconds / 3600)} h ${Math.floor((data.process.uptimeSeconds % 3600) / 60)} min`, detail: 'Since last restart' },
  ];
  return (
    <div className="grid gap-4 sm:grid-cols-2 xl:grid-cols-3">
      {tiles.map((tile) => (
        <Card key={tile.label} className="p-5">
          <p className="text-xs font-medium uppercase tracking-wider text-subtle">{tile.label}</p>
          <p className="mt-2 text-2xl font-semibold">{tile.value}</p>
          <p className="mt-1 text-sm text-muted">{tile.detail}</p>
        </Card>
      ))}
    </div>
  );
}

export function AdminPage() {
  const [tab, setTab] = useState<Tab>('invitations');
  return (
    <>
      <PageHeader title="Administration" description="Manage access to this private network." icon={<ShieldCheck />} />
      <div className="mb-4">
        <Notice icon={<Activity className="size-4" />}>All administrator actions are recorded in the audit log.</Notice>
      </div>
      <Tabs<Tab>
        className="mb-6 w-fit"
        label="Administration sections"
        value={tab}
        onChange={setTab}
        items={[
          { value: 'invitations', label: 'Invitations' },
          { value: 'users', label: 'Members' },
          { value: 'audit', label: 'Audit log' },
          { value: 'system', label: 'System' },
        ]}
      />
      <div role="tabpanel" aria-label={tab}>
        {tab === 'invitations' ? <InvitationsPanel /> : null}
        {tab === 'users' ? <UsersPanel /> : null}
        {tab === 'audit' ? <AuditPanel /> : null}
        {tab === 'system' ? <SystemPanel /> : null}
      </div>
    </>
  );
}
