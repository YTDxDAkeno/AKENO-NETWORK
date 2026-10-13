import { useMutation, useQueryClient } from '@tanstack/react-query';
import { Gamepad2, Link2 } from 'lucide-react';
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
              </div>
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
