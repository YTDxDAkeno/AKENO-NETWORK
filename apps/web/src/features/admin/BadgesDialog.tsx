import { useMutation, useQuery, useQueryClient } from '@tanstack/react-query';
import { Trash2 } from 'lucide-react';
import { useState } from 'react';
import { BADGES, GRANTABLE_BADGE_IDS, badgeDefinition, type AdminUserDto, type BadgeId, type UserBadgeAdminDto } from '@akeno/shared';
import { BadgeIcon } from '../../components/BadgeIcon';
import { Button } from '../../components/Button';
import { SelectField, TextField } from '../../components/Field';
import { Modal } from '../../components/Modal';
import { RelativeTime } from '../../components/RelativeTime';
import { Skeleton } from '../../components/Spinner';
import { Notice } from '../../components/States';
import { useToast } from '../../components/Toast';
import { api, errorMessage } from '../../lib/api';

/** Grant and revoke a member's badges. Every change is written to the audit log. */
export function BadgesDialog({ user, isSelf, onClose }: { user: AdminUserDto; isSelf: boolean; onClose: () => void }) {
  const toast = useToast();
  const queryClient = useQueryClient();
  const key = ['admin', 'badges', user.id];
  const badges = useQuery({ queryKey: key, queryFn: () => api.get<{ badges: UserBadgeAdminDto[] }>(`/admin/users/${user.id}/badges`), select: (data) => data.badges });
  const available = GRANTABLE_BADGE_IDS.filter((id) => !badges.data?.some((badge) => badge.badgeId === id));
  const [badgeId, setBadgeId] = useState<BadgeId | ''>('');
  const [note, setNote] = useState('');
  const refresh = () => {
    void queryClient.invalidateQueries({ queryKey: key });
    void queryClient.invalidateQueries({ queryKey: ['profile'] });
  };
  const grant = useMutation({
    mutationFn: () => api.post(`/admin/users/${user.id}/badges`, { badgeId, note }),
    onSuccess: () => {
      refresh();
      setBadgeId('');
      setNote('');
      toast.success('Badge granted.');
    },
    onError: (error) => toast.error('Could not grant the badge', errorMessage(error)),
  });
  const revoke = useMutation({
    mutationFn: (id: BadgeId) => api.delete(`/admin/users/${user.id}/badges/${id}`),
    onSuccess: () => {
      refresh();
      toast.success('Badge removed.');
    },
    onError: (error) => toast.error('Could not remove the badge', errorMessage(error)),
  });
  const selected = badgeId ? badgeDefinition(badgeId) : undefined;
  const selfAuthority = isSelf && selected?.authority;

  return (
    <Modal open onClose={onClose} title={`Badges of ${user.displayName}`} description="Badges are AKENO community recognitions, not PlayStation trophies.">
      <div className="flex flex-col gap-4">
        {user.role === 'admin' ? (
          <p className="flex items-center gap-2 text-sm text-muted">
            <BadgeIcon id="administrator" size="sm" /> Administrator (from the account role)
          </p>
        ) : null}
        {badges.isPending ? (
          <Skeleton className="h-16" />
        ) : badges.data && badges.data.length > 0 ? (
          <ul className="divide-y divide-line rounded-xl border border-line">
            {badges.data.map((badge) => (
              <li key={badge.badgeId} className="flex items-center gap-3 px-3 py-2.5">
                <BadgeIcon id={badge.badgeId} size="md" />
                <span className="min-w-0 flex-1">
                  <span className="block text-sm font-medium">{BADGES.find((entry) => entry.id === badge.badgeId)?.name}</span>
                  <span className="block truncate text-xs text-muted">
                    {badge.grantedBy ? `by ${badge.grantedBy.displayName}` : 'by the server operator'} · <RelativeTime iso={badge.grantedAt} />
                    {badge.note ? ` · ${badge.note}` : ''}
                  </span>
                </span>
                <Button size="sm" variant="ghost" aria-label="Remove badge" icon={<Trash2 aria-hidden className="size-4" />} loading={revoke.isPending && revoke.variables === badge.badgeId} onClick={() => revoke.mutate(badge.badgeId)} />
              </li>
            ))}
          </ul>
        ) : (
          <p className="text-sm text-muted">No granted badges.</p>
        )}
        {available.length > 0 ? (
          <div className="grid gap-3 rounded-xl border border-line p-3 sm:grid-cols-[1fr_1fr_auto] sm:items-end">
            <SelectField
              label="Grant badge"
              value={badgeId}
              onChange={(event) => setBadgeId(event.target.value as BadgeId)}
              options={[{ value: '', label: 'Choose a badge' }, ...available.map((id) => ({ value: id, label: badgeDefinition(id)?.name ?? id }))]}
            />
            <TextField label="Note (optional)" value={note} maxLength={100} onChange={(event) => setNote(event.target.value)} />
            <Button variant="primary" disabled={!badgeId || selfAuthority} loading={grant.isPending} onClick={() => grant.mutate()}>
              Grant
            </Button>
          </div>
        ) : null}
        {selfAuthority ? <Notice tone="warning">Authority badges must be granted to you by another administrator or the server operator (CLI).</Notice> : null}
      </div>
    </Modal>
  );
}
