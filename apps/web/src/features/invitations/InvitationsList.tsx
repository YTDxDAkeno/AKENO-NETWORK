import { useMutation, useQueryClient } from '@tanstack/react-query';
import { Gamepad2, Headphones, Inbox, PartyPopper, UsersRound } from 'lucide-react';
import { useNavigate } from 'react-router';
import type { AcceptInvitationResponse, InvitationDto } from '@akeno/shared';
import { Button } from '../../components/Button';
import { RelativeTime } from '../../components/RelativeTime';
import { EmptyState } from '../../components/States';
import { useToast } from '../../components/Toast';
import { api, errorMessage } from '../../lib/api';
import { useVoiceStatus } from '../../lib/queries';
import { queryKeys } from '../../lib/query-keys';
import { useVoiceController } from '../voice/VoiceProvider';

const ICONS = { group: UsersRound, party: PartyPopper, game_session: Gamepad2 };
const LABELS = { group: 'Group', party: 'Party', game_session: 'Game session' };

export function useInvitationActions() {
  const queryClient = useQueryClient();
  const toast = useToast();
  const navigate = useNavigate();
  const refresh = () => {
    for (const key of [queryKeys.invitations, queryKeys.groups, queryKeys.currentParty, queryKeys.joinableParties, queryKeys.allGameSessions, queryKeys.allGroups, queryKeys.allGameSessionDetails]) {
      void queryClient.invalidateQueries({ queryKey: key });
    }
  };
  const voice = useVoiceController();
  const accept = useMutation({
    mutationFn: ({ id }: { id: string; joinVoice?: boolean }) => api.post<AcceptInvitationResponse>(`/invitations/${id}/accept`),
    onSuccess: (result, { joinVoice }) => {
      refresh();
      toast.success('Invitation accepted.');
      // Still inside the click's user activation, so the microphone prompt and audio can start.
      if (joinVoice && result.kind === 'party') void voice?.join(result.targetId);
      void navigate(result.kind === 'group' ? `/groups/${result.targetId}` : result.kind === 'party' ? '/parties' : `/sessions/${result.targetId}`);
    },
    onError: (error) => {
      refresh();
      toast.error('Could not accept the invitation', errorMessage(error));
    },
  });
  const decline = useMutation({
    mutationFn: (id: string) => api.post(`/invitations/${id}/decline`),
    onSuccess: refresh,
    onError: (error) => toast.error('Could not decline the invitation', errorMessage(error)),
  });
  return { accept, decline };
}

export function InvitationsList({ invitations }: { invitations: InvitationDto[] }) {
  const { accept, decline } = useInvitationActions();
  const voiceStatus = useVoiceStatus();
  if (invitations.length === 0) {
    return <EmptyState icon={<Inbox className="size-6" />} title="No pending invitations" description="Group, party and game session invitations appear here." />;
  }
  return (
    <ul className="divide-y divide-line">
      {invitations.map((invitation) => {
        const Icon = ICONS[invitation.kind];
        return (
          <li key={invitation.id} className="flex flex-col gap-3 px-5 py-3.5 sm:flex-row sm:items-center" data-testid="invitation">
            <div className="flex min-w-0 flex-1 items-center gap-3">
              <span className="rounded-xl bg-accent-soft p-2 text-accent">
                <Icon aria-hidden className="size-4" />
              </span>
              <div className="min-w-0">
                <p className="truncate text-sm font-medium text-ink">{invitation.target.name}</p>
                <p className="truncate text-xs text-muted">
                  {LABELS[invitation.kind]} · from {invitation.inviter.displayName} · <RelativeTime iso={invitation.createdAt} />
                </p>
              </div>
            </div>
            <div className="flex gap-2">
              <Button size="sm" variant="primary" loading={accept.isPending && accept.variables?.id === invitation.id && !accept.variables.joinVoice} onClick={() => accept.mutate({ id: invitation.id })}>
                Accept
              </Button>
              {invitation.kind === 'party' && voiceStatus.data?.enabled ? (
                <Button
                  size="sm"
                  variant="secondary"
                  icon={<Headphones aria-hidden className="size-4" />}
                  loading={accept.isPending && accept.variables?.id === invitation.id && accept.variables.joinVoice}
                  onClick={() => accept.mutate({ id: invitation.id, joinVoice: true })}
                >
                  Join voice
                </Button>
              ) : null}
              <Button size="sm" variant="ghost" onClick={() => decline.mutate(invitation.id)}>
                Decline
              </Button>
            </div>
          </li>
        );
      })}
    </ul>
  );
}
