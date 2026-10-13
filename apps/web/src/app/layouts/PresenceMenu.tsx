import { useMutation } from '@tanstack/react-query';
import { ChevronDown } from 'lucide-react';
import { PRESENCE_PREFERENCES, type PresencePreference, type SelfPresenceStatus } from '@akeno/shared';
import { Avatar, PresenceDot } from '../../components/Avatar';
import { Menu } from '../../components/Menu';
import { useToast } from '../../components/Toast';
import { useAuth, useCurrentUser } from '../../features/auth/AuthProvider';
import { api, errorMessage } from '../../lib/api';
import { presenceLabel } from '../../lib/format';

export function PresenceMenu({ compact = false }: { compact?: boolean }) {
  const user = useCurrentUser();
  const { patchUser } = useAuth();
  const toast = useToast();
  const mutation = useMutation({
    mutationFn: (status: PresencePreference) =>
      api.put<{ preference: PresencePreference; status: SelfPresenceStatus }>('/presence', { status }),
    onSuccess: (result) => patchUser({ presencePreference: result.preference, presence: result.status }),
    onError: (error) => toast.error('Could not change your status', errorMessage(error)),
  });
  const shown: SelfPresenceStatus = user.presence === 'offline' ? user.presencePreference : user.presence;

  return (
    <Menu
      label={`Your status: ${presenceLabel(shown)}. Change status`}
      triggerClassName="gap-2 px-1.5 py-1 hover:bg-surface-2"
      trigger={
        <>
          <Avatar avatar={user.profile.avatar} name={user.profile.displayName} size="sm" status={shown} />
          {compact ? null : (
            <span className="hidden text-left md:block">
              <span className="block max-w-32 truncate text-sm font-medium text-ink">{user.profile.displayName}</span>
              <span className="block text-xs text-muted">{presenceLabel(shown)}</span>
            </span>
          )}
          <ChevronDown aria-hidden className="size-4 text-subtle" />
        </>
      }
      items={PRESENCE_PREFERENCES.map((preference) => ({
        label: presenceLabel(preference),
        icon: <PresenceDot status={preference} className="size-2.5" />,
        onSelect: () => mutation.mutate(preference),
      }))}
    />
  );
}
