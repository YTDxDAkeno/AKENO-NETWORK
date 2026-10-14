import { useMutation, useQueryClient } from '@tanstack/react-query';
import { ArrowLeft, Copy, DoorOpen, Flag, Gamepad2, Info, Link2, MapPin, Monitor, MoreHorizontal, Pencil, Play, UserMinus, UserPlus, XCircle } from 'lucide-react';
import { useState } from 'react';
import { Link, useParams } from 'react-router';
import { GAME_SESSION_STATUS_LABELS, type GameSessionDetailDto, type JoinGameSessionResponse } from '@akeno/shared';
import { Badge } from '../../components/Badge';
import { GameArt } from '../../components/GameArt';
import { Button, ButtonLink } from '../../components/Button';
import { Card, CardHeader } from '../../components/Card';
import { useConfirm } from '../../components/ConfirmDialog';
import { Menu } from '../../components/Menu';
import { RelativeTime } from '../../components/RelativeTime';
import { PageLoader } from '../../components/Spinner';
import { EmptyState, ErrorState, Notice } from '../../components/States';
import { useToast } from '../../components/Toast';
import { UserRow } from '../../components/UserRow';
import { useDocumentTitle } from '../../hooks/useDocumentTitle';
import { ApiError, api, errorMessage } from '../../lib/api';
import { useGameSession } from '../../lib/queries';
import { queryKeys } from '../../lib/query-keys';
import { useCurrentUser } from '../auth/AuthProvider';
import { useInvitationActions } from '../invitations/InvitationsList';
import { InviteDialog } from '../invitations/InviteDialog';
import { SessionFormDialog } from './SessionFormDialog';

export function SessionDetailPage() {
  const { sessionId = '' } = useParams();
  const me = useCurrentUser();
  const session = useGameSession(sessionId);
  const queryClient = useQueryClient();
  const toast = useToast();
  const confirm = useConfirm();
  const invitationActions = useInvitationActions();
  const [inviteOpen, setInviteOpen] = useState(false);
  const [editOpen, setEditOpen] = useState(false);
  useDocumentTitle(session.data?.gameName ?? 'Game session');

  const apply = (result: GameSessionDetailDto | undefined) => {
    if (result) queryClient.setQueryData(queryKeys.gameSession(sessionId), result);
    void queryClient.invalidateQueries({ queryKey: queryKeys.gameSession(sessionId) });
    void queryClient.invalidateQueries({ queryKey: queryKeys.allGameSessions });
  };
  const action = useMutation({
    mutationFn: (run: () => Promise<GameSessionDetailDto | undefined>) => run(),
    onSuccess: apply,
    onError: (error) => toast.error('Action failed', errorMessage(error)),
  });
  const join = useMutation({
    mutationFn: () => api.post<JoinGameSessionResponse>(`/game-sessions/${sessionId}/join`, {}),
    onSuccess: (result) => {
      apply(result.session);
      toast.success(result.outcome === 'requested' ? 'Join request sent to the host.' : 'You joined the session.');
    },
    onError: (error) => {
      apply(undefined);
      toast.error('Could not join the session', errorMessage(error));
    },
  });

  if (session.isPending) return <PageLoader label="Loading session…" />;
  if (session.isError) {
    if (session.error instanceof ApiError && session.error.status === 404) {
      return (
        <Card>
          <EmptyState icon={<Gamepad2 className="size-6" />} title="Session not found" description="This session does not exist or is not visible to you." action={<ButtonLink to="/sessions">Back to sessions</ButtonLink>} />
        </Card>
      );
    }
    return <ErrorState error={session.error} onRetry={() => void session.refetch()} />;
  }

  const data = session.data;
  const isHost = data.viewerState === 'host';
  const isParticipant = isHost || data.viewerState === 'member';
  const finished = data.status === 'finished';
  const full = data.participantCount >= data.maxParticipants;

  const joinButton = () => {
    if (finished || isParticipant) return null;
    if (data.viewerState === 'invited' && data.pendingInvitationId) {
      return (
        <>
          <Button variant="primary" loading={invitationActions.accept.isPending} onClick={() => invitationActions.accept.mutate({ id: data.pendingInvitationId! })}>
            Accept invitation
          </Button>
          <Button variant="ghost" onClick={() => invitationActions.decline.mutate(data.pendingInvitationId!)}>
            Decline
          </Button>
        </>
      );
    }
    if (data.viewerState === 'requested') {
      return (
        <Button variant="secondary" onClick={() => action.mutate(async () => (await api.delete(`/game-sessions/${data.id}/request`), undefined))}>
          Cancel join request
        </Button>
      );
    }
    return (
      <Button variant="primary" disabled={full} loading={join.isPending} onClick={() => join.mutate()} icon={<UserPlus aria-hidden className="size-4" />}>
        {full ? 'Session is full' : data.joinMode === 'approval' ? 'Request to join' : 'Join session'}
      </Button>
    );
  };

  return (
    <div className="mx-auto max-w-5xl">
      <div className="mb-5 flex items-start gap-3">
        <Link to="/sessions" className="mt-1 rounded-lg p-1.5 text-muted hover:bg-surface-2" aria-label="Back to game sessions">
          <ArrowLeft aria-hidden className="size-5" />
        </Link>
        <GameArt art={data.art} name={data.gameName} className="size-14 sm:size-16" sizes="64px" eager decorative />
        <div className="min-w-0 flex-1">
          <div className="flex flex-wrap items-center gap-2">
            <h1 className="text-2xl font-semibold sm:text-3xl" data-testid="session-title">
              {data.gameName}
            </h1>
            <Badge tone={data.status === 'waiting' ? 'success' : data.status === 'active' ? 'accent' : 'neutral'}>{GAME_SESSION_STATUS_LABELS[data.status]}</Badge>
          </div>
          <p className="mt-1 text-sm text-muted">
            Hosted by {data.host.displayName} · created <RelativeTime iso={data.createdAt} />
          </p>
        </div>
      </div>

      <div className="grid gap-6 lg:grid-cols-[1fr_340px]">
        <div className="flex flex-col gap-6">
          <Card className="p-5">
            <div className="flex flex-wrap gap-2">
              {data.platform ? <Badge icon={<Monitor aria-hidden className="size-3" />}>{data.platform}</Badge> : null}
              {data.region ? <Badge icon={<MapPin aria-hidden className="size-3" />}>{data.region}</Badge> : null}
              <Badge>{data.visibility === 'public' ? 'Public' : data.visibility === 'friends' ? "Host's friends" : 'Private'}</Badge>
              <Badge tone={data.joinMode === 'approval' ? 'warning' : 'neutral'}>{data.joinMode === 'approval' ? 'Host approval required' : 'Instant join'}</Badge>
              {data.gameIdentifier ? <Badge>ID: {data.gameIdentifier}</Badge> : null}
            </div>
            <p className="mt-4 whitespace-pre-wrap text-sm text-muted">{data.description || 'No description.'}</p>
            <div className="mt-5 flex flex-wrap gap-2">
              {joinButton()}
              {isParticipant && !finished ? (
                <Button variant="secondary" onClick={() => setInviteOpen(true)} disabled={full} icon={<UserPlus aria-hidden className="size-4" />}>
                  Invite friends
                </Button>
              ) : null}
              {isHost && !finished ? (
                <>
                  <Button variant="secondary" onClick={() => setEditOpen(true)} icon={<Pencil aria-hidden className="size-4" />}>
                    Edit
                  </Button>
                  {data.status === 'waiting' ? (
                    <Button variant="secondary" onClick={() => action.mutate(() => api.patch(`/game-sessions/${data.id}`, { status: 'active' }))} icon={<Play aria-hidden className="size-4" />}>
                      Mark in progress
                    </Button>
                  ) : (
                    <Button variant="secondary" onClick={() => action.mutate(() => api.patch(`/game-sessions/${data.id}`, { status: 'waiting' }))} icon={<Flag aria-hidden className="size-4" />}>
                      Back to waiting
                    </Button>
                  )}
                  <Button
                    variant="ghost"
                    className="text-danger hover:text-danger"
                    icon={<XCircle aria-hidden className="size-4" />}
                    onClick={async () => {
                      const ok = await confirm({ title: 'Close this session?', message: 'The session is marked as finished and nobody else can join.', confirmLabel: 'Close session' });
                      if (ok) action.mutate(() => api.post(`/game-sessions/${data.id}/close`));
                    }}
                  >
                    Close session
                  </Button>
                </>
              ) : null}
              {isParticipant && !finished ? (
                <Button
                  variant="ghost"
                  icon={<DoorOpen aria-hidden className="size-4" />}
                  onClick={async () => {
                    const ok = await confirm({
                      title: 'Leave this session?',
                      message: isHost ? 'Hosting passes to the longest-standing participant. If nobody is left, the session ends.' : 'You can rejoin later if there is space.',
                      confirmLabel: 'Leave session',
                    });
                    if (ok) action.mutate(async () => (await api.post(`/game-sessions/${data.id}/leave`), undefined));
                  }}
                >
                  Leave
                </Button>
              ) : null}
            </div>
          </Card>

          {isParticipant ? (
            <Card>
              <CardHeader title="Connection details" icon={<Link2 className="size-5" />} description="Shared by the host with participants only." />
              <div className="flex items-center justify-between gap-3 p-5">
                <p className="break-all font-mono text-sm text-ink" data-testid="connection-info">
                  {data.connectionInfo ?? 'The host has not added connection details yet.'}
                </p>
                {data.connectionInfo ? (
                  <Button
                    size="sm"
                    variant="ghost"
                    icon={<Copy aria-hidden className="size-4" />}
                    onClick={() => {
                      void navigator.clipboard
                        ?.writeText(data.connectionInfo ?? '')
                        .then(() => toast.success('Copied to clipboard.'))
                        .catch(() => toast.error('Could not copy. Select the text and copy it manually.'));
                    }}
                  >
                    Copy
                  </Button>
                ) : null}
              </div>
            </Card>
          ) : null}

          {isHost && data.joinRequests.length > 0 ? (
            <Card>
              <CardHeader title={`Join requests (${data.joinRequests.length})`} />
              <ul className="divide-y divide-line">
                {data.joinRequests.map((request) => (
                  <li key={request.id} className="px-5 py-3">
                    <UserRow
                      user={request.user}
                      secondary={request.message || <RelativeTime iso={request.createdAt} />}
                      actions={
                        <>
                          <Button size="sm" variant="primary" disabled={full} onClick={() => action.mutate(() => api.post(`/game-sessions/${data.id}/requests/${request.id}/accept`))}>
                            Accept
                          </Button>
                          <Button size="sm" variant="ghost" onClick={() => action.mutate(() => api.post(`/game-sessions/${data.id}/requests/${request.id}/reject`))}>
                            Reject
                          </Button>
                        </>
                      }
                    />
                  </li>
                ))}
              </ul>
            </Card>
          ) : null}

          <Notice icon={<Info className="size-4" />}>
            Joining here records that you are part of this lobby on AKENO NETWORK. It does not launch or join a game on any
            console or PC. Use the connection details to meet up in-game.
          </Notice>
        </div>

        <Card className="h-fit">
          <h2 className="border-b border-line px-5 py-3.5 text-sm font-semibold">
            Players ({data.participantCount}/{data.maxParticipants})
          </h2>
          <ul className="divide-y divide-line" data-testid="session-members">
            {data.members.map((member) => (
              <li key={member.user.id} className="px-4 py-2.5">
                <UserRow
                  user={member.user}
                  presence={member.user.presence}
                  size="sm"
                  secondary={member.role === 'host' ? 'Host' : undefined}
                  actions={
                    isHost && member.user.id !== me.id && !finished ? (
                      <Menu
                        label={`Manage ${member.user.displayName}`}
                        triggerClassName="p-1.5 text-muted hover:bg-surface-2"
                        trigger={<MoreHorizontal aria-hidden className="size-4" />}
                        items={[
                          {
                            label: 'Remove from session',
                            icon: <UserMinus />,
                            tone: 'danger',
                            onSelect: () => action.mutate(async () => (await api.delete(`/game-sessions/${data.id}/members/${member.user.id}`), undefined)),
                          },
                        ]}
                      />
                    ) : null
                  }
                />
              </li>
            ))}
          </ul>
        </Card>
      </div>
      {inviteOpen ? (
        <InviteDialog
          open
          onClose={() => setInviteOpen(false)}
          title={`Invite friends to ${data.gameName}`}
          invitePath={`/game-sessions/${data.id}/invitations`}
          excludeIds={data.members.map((member) => member.user.id)}
        />
      ) : null}
      {editOpen ? <SessionFormDialog open onClose={() => setEditOpen(false)} session={data} /> : null}
    </div>
  );
}
