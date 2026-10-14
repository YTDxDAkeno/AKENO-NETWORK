import { useMutation, useQueryClient } from '@tanstack/react-query';
import { Crown, DoorOpen, Lock, MoreHorizontal, PartyPopper, Plus, UserMinus, UserPlus, Users, XCircle } from 'lucide-react';
import { useState, type FormEvent } from 'react';
import { LIMITS, type PartyDto, type PartyMemberDto, type PartyPrivacy } from '@akeno/shared';
import { Link } from 'react-router';
import { Avatar } from '../../components/Avatar';
import { GameActivityLine } from '../../components/GameActivity';
import { MemberVolumeControl, PartyVoicePanel, VoiceStateLine, useMemberVoice } from '../voice/PartyVoice';
import { Badge } from '../../components/Badge';
import { Button } from '../../components/Button';
import { Card, CardHeader } from '../../components/Card';
import { useConfirm } from '../../components/ConfirmDialog';
import { SelectField, TextField } from '../../components/Field';
import { Menu } from '../../components/Menu';
import { PageHeader } from '../../components/PageHeader';
import { Skeleton } from '../../components/Spinner';
import { EmptyState, ErrorState } from '../../components/States';
import { useToast } from '../../components/Toast';
import { api, errorMessage } from '../../lib/api';
import { useCurrentParty, useInvitations, useJoinableParties } from '../../lib/queries';
import { queryKeys } from '../../lib/query-keys';
import { useCurrentUser } from '../auth/AuthProvider';
import { useInvitationActions } from '../invitations/InvitationsList';
import { InviteDialog } from '../invitations/InviteDialog';
import { ChatPanel } from '../messages/ChatPanel';

function usePartyRefresh() {
  const queryClient = useQueryClient();
  return () => {
    for (const key of [queryKeys.currentParty, queryKeys.joinableParties, queryKeys.invitations]) {
      void queryClient.invalidateQueries({ queryKey: key });
    }
  };
}

function CreatePartyCard() {
  const toast = useToast();
  const refresh = usePartyRefresh();
  const [form, setForm] = useState({ name: '', privacy: 'friends' as PartyPrivacy, maxMembers: String(LIMITS.partyMembersDefault) });
  const create = useMutation({
    mutationFn: () =>
      api.post<PartyDto>('/parties', {
        ...(form.name.trim() ? { name: form.name.trim() } : {}),
        privacy: form.privacy,
        maxMembers: Number(form.maxMembers),
      }),
    onSuccess: () => {
      refresh();
      toast.success('Party started.');
    },
    onError: (error) => toast.error('Could not start the party', errorMessage(error)),
  });
  const onSubmit = (event: FormEvent) => {
    event.preventDefault();
    create.mutate();
  };
  return (
    <Card>
      <CardHeader title="Start a party" description="A private hangout for you and your friends with its own chat." icon={<PartyPopper className="size-5" />} />
      <form className="grid gap-4 p-5 sm:grid-cols-3" onSubmit={onSubmit}>
        <TextField
          label="Party name"
          placeholder="Optional"
          value={form.name}
          maxLength={LIMITS.partyNameMax}
          onChange={(event) => setForm((current) => ({ ...current, name: event.target.value }))}
        />
        <SelectField
          label="Who can join"
          value={form.privacy}
          onChange={(event) => setForm((current) => ({ ...current, privacy: event.target.value as PartyPrivacy }))}
          options={[
            { value: 'friends', label: 'Friends can join' },
            { value: 'invite_only', label: 'Invited people only' },
          ]}
        />
        <SelectField
          label="Maximum members"
          value={form.maxMembers}
          onChange={(event) => setForm((current) => ({ ...current, maxMembers: event.target.value }))}
          options={Array.from({ length: LIMITS.partyMembersMax - 1 }, (_, index) => {
            const value = String(index + 2);
            return { value, label: value };
          })}
        />
        <div className="flex justify-end sm:col-span-3">
          <Button type="submit" variant="primary" loading={create.isPending} icon={<Plus aria-hidden className="size-4" />}>
            Start party
          </Button>
        </div>
      </form>
    </Card>
  );
}

function MemberTile({ member, party, isLeader, meId, onAction }: { member: PartyMemberDto; party: PartyDto; isLeader: boolean; meId: string; onAction: (run: () => Promise<unknown>) => void }) {
  const game = member.user.presence.game;
  const voice = useMemberVoice(member);
  return (
    <li className="flex items-center gap-3 rounded-xl border border-line bg-surface/60 p-3" data-testid="party-member" data-user-id={member.user.id}>
      <Avatar
        avatar={member.user.avatar}
        image={member.user.avatarImage}
        name={member.user.displayName}
        size="lg"
        status={member.user.presence.status}
        speaking={voice.speaking}
      />
      <div className="min-w-0 flex-1">
        <p className="flex items-center gap-1.5 truncate font-medium text-ink">
          <Link to={`/u/${member.user.onlineId}`} className="truncate hover:underline">
            {member.user.displayName}
          </Link>
          {member.isLeader ? <Crown aria-label="Party leader" className="size-3.5 shrink-0 text-warning" /> : null}
          {member.user.id === meId ? <span className="text-xs font-normal text-subtle">(you)</span> : null}
        </p>
        <VoiceStateLine state={voice} />
        {game ? <GameActivityLine game={game} className="mt-0.5 max-w-full text-xs" /> : null}
      </div>
      {voice.connected && member.user.id !== meId ? <MemberVolumeControl member={member} /> : null}
      {isLeader && member.user.id !== meId ? (
        <Menu
          label={`Manage ${member.user.displayName}`}
          triggerClassName="p-1.5 text-muted hover:bg-surface-2"
          trigger={<MoreHorizontal aria-hidden className="size-4" />}
          items={[
            { label: 'Make party leader', icon: <Crown />, onSelect: () => onAction(() => api.post(`/parties/${party.id}/leader`, { userId: member.user.id })) },
            { label: 'Remove from party', icon: <UserMinus />, tone: 'danger', onSelect: () => onAction(() => api.delete(`/parties/${party.id}/members/${member.user.id}`)) },
          ]}
        />
      ) : null}
    </li>
  );
}

function CurrentParty({ party }: { party: PartyDto }) {
  const me = useCurrentUser();
  const toast = useToast();
  const confirm = useConfirm();
  const refresh = usePartyRefresh();
  const [inviteOpen, setInviteOpen] = useState(false);
  const isLeader = party.leaderId === me.id;
  const leader = party.members.find((member) => member.isLeader);
  const action = useMutation({
    mutationFn: (run: () => Promise<unknown>) => run(),
    onSuccess: refresh,
    onError: (error) => toast.error('Action failed', errorMessage(error)),
  });

  const leave = async () => {
    const ok = await confirm({
      title: 'Leave the party?',
      message: party.memberCount === 1 ? 'You are the last member, so the party and its chat will end.' : 'You can rejoin later if the party is still running.',
      confirmLabel: 'Leave party',
    });
    if (ok) action.mutate(() => api.post(`/parties/${party.id}/leave`));
  };
  const close = async () => {
    const ok = await confirm({ title: 'Close the party for everyone?', message: 'All members are removed and the party chat is deleted.', confirmLabel: 'Close party' });
    if (ok) action.mutate(() => api.post(`/parties/${party.id}/close`));
  };

  return (
    <div className="grid gap-6 xl:grid-cols-[minmax(0,1fr)_380px]">
      <section className="card-tinted flex min-w-0 flex-col" aria-labelledby="party-name">
        <header className="flex flex-col gap-4 border-b border-line px-5 py-4 sm:flex-row sm:items-center sm:justify-between">
          <div className="flex min-w-0 items-center gap-3">
            <span className="grid size-11 shrink-0 place-items-center rounded-xl bg-accent-soft text-accent">
              <PartyPopper aria-hidden className="size-5" />
            </span>
            <div className="min-w-0">
              <h2 id="party-name" className="truncate text-lg font-semibold" data-testid="party-name">
                {party.name}
              </h2>
              <p className="truncate text-xs text-muted">
                Led by {leader?.user.displayName ?? 'unknown'} · {party.memberCount}/{party.maxMembers} members ·{' '}
                {party.privacy === 'friends' ? (
                  'Friends can join'
                ) : (
                  <span className="inline-flex items-center gap-1">
                    <Lock aria-hidden className="size-3" /> Invited people only
                  </span>
                )}
              </p>
            </div>
          </div>
          <div className="flex flex-wrap items-center gap-2">
            <Button variant="secondary" size="sm" onClick={() => setInviteOpen(true)} disabled={party.memberCount >= party.maxMembers} icon={<UserPlus aria-hidden className="size-4" />}>
              Invite
            </Button>
            <Button size="sm" variant="ghost" icon={<DoorOpen aria-hidden className="size-4" />} onClick={() => void leave()}>
              Leave
            </Button>
            {isLeader ? (
              <Button size="sm" variant="ghost" className="text-danger hover:text-danger" icon={<XCircle aria-hidden className="size-4" />} onClick={() => void close()}>
                Close
              </Button>
            ) : null}
          </div>
        </header>
        <PartyVoicePanel party={party} />
        <ul className="grid gap-3 p-4 sm:grid-cols-2" data-testid="party-members">
          {party.members.map((member) => (
            <MemberTile key={member.user.id} member={member} party={party} isLeader={isLeader} meId={me.id} onAction={(run) => action.mutate(run)} />
          ))}
        </ul>
      </section>

      <Card className="flex h-[60dvh] min-h-[420px] flex-col overflow-hidden xl:h-[calc(100dvh-14rem)]">
        <h2 className="border-b border-line px-5 py-3.5 text-sm font-semibold">Party chat</h2>
        <ChatPanel
          conversationId={party.conversationId!}
          participants={party.members.map((member) => member.user)}
          emptyTitle="Party chat"
          emptyDescription="Messages here are visible to everyone in the party."
        />
      </Card>
      {inviteOpen ? (
        <InviteDialog
          open
          onClose={() => setInviteOpen(false)}
          title="Invite friends to the party"
          invitePath={`/parties/${party.id}/invitations`}
          excludeIds={party.members.map((member) => member.user.id)}
        />
      ) : null}
    </div>
  );
}

function JoinableParties({ inParty }: { inParty: boolean }) {
  const parties = useJoinableParties();
  const invitations = useInvitations();
  const invitationActions = useInvitationActions();
  const toast = useToast();
  const confirm = useConfirm();
  const refresh = usePartyRefresh();
  const join = useMutation({
    mutationFn: (id: string) => api.post(`/parties/${id}/join`),
    onSuccess: () => {
      refresh();
      toast.success('You joined the party.');
    },
    onError: (error) => toast.error('Could not join the party', errorMessage(error)),
  });

  const joinParty = async (party: PartyDto) => {
    if (inParty) {
      const ok = await confirm({
        title: 'Switch parties?',
        message: 'You can only be in one party at a time. Joining this party leaves your current one.',
        confirmLabel: 'Join party',
        tone: 'primary',
      });
      if (!ok) return;
    }
    const invitation = invitations.data?.find((entry) => entry.kind === 'party' && entry.target.id === party.id);
    if (invitation) invitationActions.accept.mutate(invitation.id);
    else join.mutate(party.id);
  };

  return (
    <Card>
      <CardHeader title="Join a party" description="Parties started by your friends and parties you are invited to." icon={<Users className="size-5" />} />
      {parties.isPending ? (
        <Skeleton className="m-5 h-16" />
      ) : parties.isError ? (
        <ErrorState error={parties.error} onRetry={() => void parties.refetch()} />
      ) : parties.data.length === 0 ? (
        <EmptyState title="No parties to join right now" description="When your friends start a party, it appears here." />
      ) : (
        <ul className="divide-y divide-line">
          {parties.data.map((party) => {
            const invited = invitations.data?.some((entry) => entry.kind === 'party' && entry.target.id === party.id);
            const leader = party.members.find((member) => member.isLeader);
            return (
              <li key={party.id} className="flex flex-col gap-3 px-5 py-3.5 sm:flex-row sm:items-center" data-testid="joinable-party">
                <div className="min-w-0 flex-1">
                  <p className="flex items-center gap-2 truncate font-medium">
                    {party.name}
                    {invited ? <Badge tone="accent">Invited</Badge> : null}
                    {party.privacy === 'invite_only' ? <Lock aria-label="Invitation only" className="size-3.5 text-subtle" /> : null}
                  </p>
                  <p className="truncate text-xs text-muted">
                    Led by {leader?.user.displayName ?? 'a friend'} · {party.memberCount}/{party.maxMembers} members
                  </p>
                </div>
                <Button size="sm" variant="subtle" disabled={party.memberCount >= party.maxMembers} loading={join.isPending && join.variables === party.id} onClick={() => void joinParty(party)}>
                  {party.memberCount >= party.maxMembers ? 'Full' : 'Join'}
                </Button>
              </li>
            );
          })}
        </ul>
      )}
    </Card>
  );
}

export function PartiesPage() {
  const party = useCurrentParty();
  return (
    <>
      <PageHeader title="Parties" description="Hang out with friends in a private party." icon={<PartyPopper />} />
      {party.isPending ? (
        <Skeleton className="h-64" />
      ) : party.isError ? (
        <ErrorState error={party.error} onRetry={() => void party.refetch()} />
      ) : party.data ? (
        <div className="flex flex-col gap-6">
          <CurrentParty party={party.data} />
          <JoinableParties inParty />
        </div>
      ) : (
        <div className="grid gap-6 xl:grid-cols-2">
          <CreatePartyCard />
          <JoinableParties inParty={false} />
        </div>
      )}
    </>
  );
}
