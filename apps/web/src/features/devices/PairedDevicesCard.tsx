import { useMutation, useQueryClient } from '@tanstack/react-query';
import { Gamepad2, Link2 } from 'lucide-react';
import { DEVICE_SCOPE_LABELS, type ConsoleAccountState, type DeviceDto, type DeviceScope } from '@akeno/shared';
import { Badge } from '../../components/Badge';
import { Button, ButtonLink } from '../../components/Button';
import { Card, CardHeader } from '../../components/Card';
import { useConfirm } from '../../components/ConfirmDialog';
import { RelativeTime } from '../../components/RelativeTime';
import { Skeleton } from '../../components/Spinner';
import { ErrorState } from '../../components/States';
import { useToast } from '../../components/Toast';
import { api, errorMessage } from '../../lib/api';
import { useDevices } from '../../lib/queries';
import { queryKeys } from '../../lib/query-keys';

/** What the console's optional read-only account check found (never a PSN verification). */
const CONSOLE_ACCOUNT_LABELS: Record<ConsoleAccountState, string> = {
  not_checked: 'account check not run',
  no_account_id: 'offline user, no account ID',
  account_id_present: 'has an account ID (PSN or local, not distinguished)',
  check_failed: 'account check failed',
};

/** Lists devices paired with a code (console and companion clients) and lets the user unpair them. */
export function PairedDevicesCard() {
  const devices = useDevices();
  const toast = useToast();
  const confirm = useConfirm();
  const queryClient = useQueryClient();

  const unpair = useMutation({
    mutationFn: (id: string) => api.delete(`/devices/${id}`),
    onSuccess: () => {
      void queryClient.invalidateQueries({ queryKey: queryKeys.devices });
      toast.success('Device unpaired.', 'It was signed out immediately.');
    },
    onError: (error) => toast.error('Could not unpair the device', errorMessage(error)),
  });

  // Reauthorization: grant a permission that did not exist (or was not granted) when the device was paired.
  const grant = useMutation({
    mutationFn: ({ device, scope }: { device: DeviceDto; scope: DeviceScope }) =>
      api.patch<{ device: DeviceDto }>(`/devices/${device.id}`, { scopes: [...device.scopes, scope] }),
    onSuccess: () => {
      void queryClient.invalidateQueries({ queryKey: queryKeys.devices });
      toast.success('Permission granted.', 'The console picks it up within a minute.');
    },
    onError: (error) => toast.error('Could not change the permissions', errorMessage(error)),
  });

  const confirmGrantActivity = async (device: DeviceDto) => {
    const ok = await confirm({
      title: `Allow ${device.name} to share game activity?`,
      message: `${DEVICE_SCOPE_LABELS['activity:write']}. Who sees it is decided by your privacy settings; you can turn tracking off there at any time.`,
      confirmLabel: 'Allow',
    });
    if (ok) grant.mutate({ device, scope: 'activity:write' });
  };

  // Account Bridge: removes the local console-user link; the device stays paired.
  const unlinkUser = useMutation({
    mutationFn: (id: string) => api.delete<{ device: DeviceDto }>(`/devices/${id}/console-user`),
    onSuccess: () => {
      void queryClient.invalidateQueries({ queryKey: queryKeys.devices });
      toast.success('PS5 user unlinked.', 'The console stays paired.');
    },
    onError: (error) => toast.error('Could not unlink the PS5 user', errorMessage(error)),
  });

  const confirmUnpair = async (id: string, name: string) => {
    const ok = await confirm({
      title: `Unpair ${name}?`,
      message: 'The device is signed out immediately. You can pair it again later with a new code.',
      confirmLabel: 'Unpair',
      tone: 'danger',
    });
    if (ok) unpair.mutate(id);
  };

  return (
    <Card>
      <CardHeader
        title="Paired devices"
        icon={<Gamepad2 className="size-5" />}
        description="Consoles and companion apps signed in with a pairing code. They only have the permissions you granted."
        action={
          <ButtonLink to="/pair" size="sm" variant="ghost" icon={<Link2 aria-hidden className="size-4" />}>
            Pair a device
          </ButtonLink>
        }
      />
      {devices.isPending ? (
        <Skeleton className="m-5 h-16" />
      ) : devices.isError ? (
        <ErrorState error={devices.error} onRetry={() => void devices.refetch()} />
      ) : devices.data.length === 0 ? (
        <p className="px-5 py-4 text-sm text-muted">No devices are paired with your account.</p>
      ) : (
        <ul className="divide-y divide-line">
          {devices.data.map((device) => (
            <li key={device.id} className="flex items-center gap-3 px-5 py-3">
              <Gamepad2 aria-hidden className="size-5 text-subtle" />
              <div className="min-w-0 flex-1">
                <p className="flex flex-wrap items-center gap-2 text-sm font-medium">
                  {device.name}
                  {device.platform ? <Badge>{device.platform}</Badge> : null}
                </p>
                <p className="text-xs text-muted">
                  Last active <RelativeTime iso={device.lastUsedAt} />
                  {device.ipAddress ? ` · ${device.ipAddress}` : ''} · {device.scopes.length} permission
                  {device.scopes.length === 1 ? '' : 's'}
                </p>
                {device.consoleUser ? (
                  <p className="text-xs text-muted" data-testid="console-user">
                    Linked PS5 user: <span className="font-medium text-ink">{device.consoleUser.userName}</span> (local,
                    not verified by Sony) · {CONSOLE_ACCOUNT_LABELS[device.consoleUser.accountState]}
                    {' · '}
                    <button
                      type="button"
                      className="font-medium text-accent hover:underline"
                      disabled={unlinkUser.isPending}
                      onClick={() => unlinkUser.mutate(device.id)}
                    >
                      Unlink
                    </button>
                  </p>
                ) : null}
              </div>
              {!device.scopes.includes('activity:write') ? (
                <Button size="sm" variant="secondary" loading={grant.isPending} onClick={() => void confirmGrantActivity(device)}>
                  Allow game activity
                </Button>
              ) : null}
              <Button size="sm" variant="ghost" onClick={() => void confirmUnpair(device.id, device.name)}>
                Unpair
              </Button>
            </li>
          ))}
        </ul>
      )}
    </Card>
  );
}
