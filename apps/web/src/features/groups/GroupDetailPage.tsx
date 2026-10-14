import { useMutation, useQueryClient } from '@tanstack/react-query';
import { ArrowLeft, Crown, DoorOpen, Globe, Lock, MoreHorizontal, Pencil, Shield, Trash2, UserMinus, UserPlus, UsersRound } from 'lucide-react';
import { useState } from 'react';
import { Link, useNavigate, useParams } from 'react-router';
import type { GroupMemberDto } from '@akeno/shared';
import { Badge } from '../../components/Badge';
import { Button, ButtonLink } from '../../components/Button';
import { Card } from '../../components/Card';
import { useConfirm } from '../../components/ConfirmDialog';
import { Menu, type MenuItem } from '../../components/Menu';
import { PageLoader } from '../../components/Spinner';
import { EmptyState, ErrorState, Notice } from '../../components/States';
import { useToast } from '../../components/Toast';
import { UserRow } from '../../components/UserRow';
import { useDocumentTitle } from '../../hooks/useDocumentTitle';
import { ApiError, api, errorMessage } from '../../lib/api';
import { pluralize } from '../../lib/format';
import { useGroup, useInvitations } from '../../lib/queries';
import { queryKeys } from '../../lib/query-keys';
import { useCurrentUser } from '../auth/AuthProvider';
import { useInvitationActions } from '../invitations/InvitationsList';
import { InviteDialog } from '../invitations/InviteDialog';
import { ChatPanel } from '../messages/ChatPanel';
import { GroupFormDialog } from './GroupFormDialog';

const ROLE_LABEL = { owner: 'Owner', moderator: 'Moderator', member: 'Member' } as const;

export function GroupDetailPage() {
  const { groupId = '' } = useParams();
  const me = useCurrentUser();
  const group = useGroup(groupId);
  const invitations = useInvitations();
  const invitationActions = useInvitationActions();
  const queryClient = useQueryClient();
  const navigate = useNavigate();
  const toast = useToast();
  const confirm = useConfirm();
  const [inviteOpen, setInviteOpen] = useState(false);
  const [editOpen, setEditOpen] = useState(false);
  useDocumentTitle(group.data?.name ?? 'Group');

  const refresh = () => {
    void queryClient.invalidateQueries({ queryKey: queryKeys.group(groupId) });
    void queryClient.invalidateQueries({ queryKey: queryKeys.groups });
  };
  const action = useMutation({
    mutationFn: (run: () => Promise<unknown>) => run(),
    onSuccess: refresh,
    onError: (error) => toast.error('Action failed', errorMessage(error)),
  });

  if (group.isPending) return <PageLoader label="Loading group…" />;
  if (group.isError) {
    if (group.error instanceof ApiError && group.error.status === 404) {
      return (
        <Card>
          <EmptyState icon={<UsersRound className="size-6" />} title="Group not found" description="This group does not exist or you do not have access to it." action={<ButtonLink to="/groups">Back to groups</ButtonLink>} />
        </Card>
      );
    }
    return <ErrorState error={group.error} onRetry={() => void group.refetch()} />;
  }

  const data = group.data;
  const role = data.myRole;
  const canManage = role === 'owner' || role === 'moderator';
  const pendingInvitation = invitations.data?.find((invitation) => invitation.kind === 'group' && invitation.target.id === data.id);

  const leave = async () => {
    const alone = data.memberCount === 1;
    const ok = await confirm({
      title: alone ? 'Leave and delete this group?' : 'Leave this group?',
      message: alone
        ? 'You are the last member. Leaving deletes the group and its chat history permanently.'
        : 'You will lose access to the group chat until someone invites you again.',
      confirmLabel: 'Leave group',
    });
    if (!ok) return;
    try {
      await api.post(`/groups/${data.id}/leave`);
      void queryClient.invalidateQueries({ queryKey: queryKeys.groups });
      toast.success('You left the group.');
      void navigate('/groups');
    } catch (error) {
      toast.error('Could not leave the group', errorMessage(error));
    }
  };

  const remove = async () => {
    const ok = await confirm({
      title: `Delete "${data.name}"?`,
      message: 'The group, its members list and all of its messages will be deleted permanently.',
      confirmLabel: 'Delete group',
    });
    if (!ok) return;
    try {
      await api.delete(`/groups/${data.id}`);
      void queryClient.invalidateQueries({ queryKey: queryKeys.groups });
      toast.success('Group deleted.');
      void navigate('/groups');
    } catch (error) {
      toast.error('Could not delete the group', errorMessage(error));
    }
  };

  const memberMenu = (member: GroupMemberDto): MenuItem[] => {
    if (member.user.id === me.id || member.role === 'owner') return [];
    const items: MenuItem[] = [];
    if (role === 'owner') {
      items.push(
        member.role === 'moderator'
          ? { label: 'Make member', icon: <Shield />, onSelect: () => action.mutate(() => api.patch(`/groups/${data.id}/members/${member.user.id}`, { role: 'member' })) }
          : { label: 'Make moderator', icon: <Shield />, onSelect: () => action.mutate(() => api.patch(`/groups/${data.id}/members/${member.user.id}`, { role: 'moderator' })) },
        {
          label: 'Transfer ownership',
          icon: <Crown />,
          onSelect: async () => {
            const ok = await confirm({
              title: `Make ${member.user.displayName} the owner?`,
              message: 'You will become a moderator. Only the new owner can undo this.',
              confirmLabel: 'Transfer ownership',
              tone: 'primary',
            });
            if (ok) action.mutate(() => api.post(`/groups/${data.id}/transfer-ownership`, { userId: member.user.id }));
          },
        },
      );
    }
    if (role === 'owner' || (role === 'moderator' && member.role === 'member')) {
      items.push({
        label: 'Remove from group',
        icon: <UserMinus />,
        tone: 'danger',
        onSelect: async () => {
          const ok = await confirm({ title: `Remove ${member.user.displayName}?`, message: 'They lose access to the group chat.', confirmLabel: 'Remove' });
          if (ok) action.mutate(() => api.delete(`/groups/${data.id}/members/${member.user.id}`));
        },
      });
    }
    return items;
  };

  const header = (
    <div className="mb-5 flex flex-col gap-4 sm:flex-row sm:items-start sm:justify-between">
      <div className="flex items-start gap-3">
        <Link to="/groups" className="mt-1 rounded-lg p-1.5 text-muted hover:bg-surface-2" aria-label="Back to groups">
          <ArrowLeft aria-hidden className="size-5" />
        </Link>
        <span className="inline-flex size-12 shrink-0 items-center justify-center rounded-xl bg-accent-soft font-display text-xl font-bold text-accent">
          {data.name.slice(0, 1).toUpperCase()}
        </span>
        <div className="min-w-0">
          <h1 className="text-2xl font-semibold" data-testid="group-name">
            {data.name}
          </h1>
          <div className="mt-1 flex flex-wrap items-center gap-2 text-sm text-muted">
            <span>{pluralize(data.memberCount, 'member')}</span>
            <Badge icon={data.joinPolicy === 'open' ? <Globe aria-hidden className="size-3" /> : <Lock aria-hidden className="size-3" />}>
              {data.joinPolicy === 'open' ? 'Open' : 'Invitation only'}
            </Badge>
            {role ? <Badge tone="accent">{ROLE_LABEL[role]}</Badge> : null}
          </div>
          {data.description ? <p className="mt-2 max-w-2xl whitespace-pre-wrap text-sm text-muted">{data.description}</p> : null}
        </div>
      </div>
      {role ? (
        <div className="flex flex-wrap gap-2">
          {canManage ? (
            <Button variant="primary" onClick={() => setInviteOpen(true)} icon={<UserPlus aria-hidden className="size-4" />}>
              Invite
            </Button>
          ) : null}
          {canManage ? (
            <Button onClick={() => setEditOpen(true)} icon={<Pencil aria-hidden className="size-4" />}>
              Edit
            </Button>
          ) : null}
          <Button variant="ghost" onClick={() => void leave()} icon={<DoorOpen aria-hidden className="size-4" />}>
            Leave
          </Button>
          {role === 'owner' ? (
            <Button variant="ghost" className="text-danger hover:text-danger" onClick={() => void remove()} icon={<Trash2 aria-hidden className="size-4" />}>
              Delete
            </Button>
          ) : null}
        </div>
      ) : null}
    </div>
  );

  const members = (
    <Card className="flex flex-col overflow-hidden">
      <h2 className="border-b border-line px-5 py-3.5 text-sm font-semibold">Members ({data.memberCount})</h2>
      <ul className="scrollbar-thin flex-1 divide-y divide-line overflow-y-auto" data-testid="group-members">
        {data.members.map((member) => {
          const items = role ? memberMenu(member) : [];
          return (
            <li key={member.user.id} className="px-4 py-2.5">
              <UserRow
                user={member.user}
                presence={member.user.presence}
                size="sm"
                secondary={
                  <span className="inline-flex items-center gap-1">
                    {member.role === 'owner' ? <Crown aria-hidden className="size-3 text-warning" /> : null}
                    {ROLE_LABEL[member.role]}
                  </span>
                }
                actions={
                  items.length > 0 ? (
                    <Menu
                      label={`Manage ${member.user.displayName}`}
                      triggerClassName="p-1.5 text-muted hover:bg-surface-2"
                      trigger={<MoreHorizontal aria-hidden className="size-4" />}
                      items={items}
                    />
                  ) : null
                }
              />
            </li>
          );
        })}
      </ul>
      {canManage && data.pendingInvitations.length > 0 ? (
        <div className="border-t border-line px-5 py-3">
          <h3 className="mb-2 text-xs font-semibold uppercase tracking-wider text-subtle">Pending invitations</h3>
          <ul className="space-y-1.5">
            {data.pendingInvitations.map((invitation) => (
              <li key={invitation.id} className="flex items-center justify-between gap-2 text-sm">
                <span className="truncate">{invitation.invitee.displayName}</span>
                <button
                  type="button"
                  className="text-xs font-medium text-muted hover:text-danger"
                  onClick={() => action.mutate(() => api.post(`/invitations/${invitation.id}/cancel`))}
                >
                  Cancel
                </button>
              </li>
            ))}
          </ul>
        </div>
      ) : null}
    </Card>
  );

  if (!role) {
    return (
      <>
        {header}
        <div className="grid gap-6 lg:grid-cols-[1fr_320px]">
          <Card className="p-6">
            {pendingInvitation ? (
              <div className="flex flex-col items-start gap-4">
                <Notice>You were invited to this group by {pendingInvitation.inviter.displayName}.</Notice>
                <div className="flex gap-2">
                  <Button variant="primary" loading={invitationActions.accept.isPending} onClick={() => invitationActions.accept.mutate({ id: pendingInvitation.id })}>
                    Accept invitation
                  </Button>
                  <Button variant="ghost" onClick={() => invitationActions.decline.mutate(pendingInvitation.id)}>
                    Decline
                  </Button>
                </div>
              </div>
            ) : data.joinPolicy === 'open' ? (
              <div className="flex flex-col items-start gap-4">
                <p className="text-sm text-muted">This is an open group. Join to read and send messages.</p>
                <Button variant="primary" loading={action.isPending} onClick={() => action.mutate(() => api.post(`/groups/${data.id}/join`))}>
                  Join group
                </Button>
              </div>
            ) : (
              <p className="text-sm text-muted">You need an invitation to join this group.</p>
            )}
          </Card>
          {data.members.length > 0 ? members : null}
        </div>
      </>
    );
  }

  return (
    <>
      {header}
      <div className="grid gap-6 lg:grid-cols-[1fr_320px]">
        <Card className="flex h-[65dvh] min-h-[420px] flex-col overflow-hidden lg:h-[calc(100dvh-16rem)]">
          <ChatPanel
            conversationId={data.conversationId!}
            participants={data.members.map((member) => member.user)}
            emptyTitle="The group chat is quiet"
            emptyDescription="Send the first message to your group."
            onRead={() => void queryClient.invalidateQueries({ queryKey: queryKeys.groups })}
          />
        </Card>
        {members}
      </div>
      {inviteOpen ? (
        <InviteDialog
          open
          onClose={() => {
            setInviteOpen(false);
            refresh();
          }}
          title={`Invite to ${data.name}`}
          invitePath={`/groups/${data.id}/invitations`}
          excludeIds={[...data.members.map((member) => member.user.id), ...data.pendingInvitations.map((invitation) => invitation.invitee.id)]}
          searchAll
        />
      ) : null}
      {editOpen ? <GroupFormDialog open onClose={() => setEditOpen(false)} group={data} canChangePolicy={role === 'owner'} /> : null}
    </>
  );
}
