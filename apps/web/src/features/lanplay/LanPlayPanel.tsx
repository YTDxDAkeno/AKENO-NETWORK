import { useQueryClient } from '@tanstack/react-query';
import { Gamepad2, LogOut, Network, Plug, PlugZap, Users, WifiOff, XCircle } from 'lucide-react';
import { useState } from 'react';
import {
  LAN_PLAY_GAMES,
  type LanPlayPlatform,
  type LanPlaySessionDto,
  type PartyDto,
  type PartyLanPlayDto,
} from '@akeno/shared';
import { Badge } from '../../components/Badge';
import { Button } from '../../components/Button';
import { useConfirm } from '../../components/ConfirmDialog';
import { SelectField } from '../../components/Field';
import { useToast } from '../../components/Toast';
import { api, errorMessage } from '../../lib/api';
import { useCurrentUser } from '../auth/AuthProvider';
import { lanPlayKeys, usePartyLanPlay, useLanPlayStatus } from './queries';

const PLATFORMS: Array<{ value: LanPlayPlatform; label: string }> = [
  { value: 'pc', label: 'Windows PC' },
  { value: 'ps5', label: 'PlayStation 5 (through a bridge)' },
];

function memberName(party: PartyDto, userId: string): string {
  return party.members.find((member) => member.user.id === userId)?.user.displayName ?? 'Someone';
}

/**
 * LAN Play panel (experimental). It shows the control-plane state of a party's LAN Play session
 * and is deliberately honest: while no verified network transport is connected it shows
 * "Transport not available" and never claims that game traffic is flowing.
 */
export function LanPlayPanel({ party }: { party: PartyDto }) {
  const me = useCurrentUser();
  const statusQuery = useLanPlayStatus();
  const viewQuery = usePartyLanPlay(party.id, statusQuery.data?.enabled ?? false);
  const queryClient = useQueryClient();
  const toast = useToast();
  const confirm = useConfirm();
  const [platform, setPlatform] = useState<LanPlayPlatform>('pc');
  const [busy, setBusy] = useState(false);

  const status = statusQuery.data;
  const view = viewQuery.data;
  if (!status || !status.enabled) return null; // off by default; nothing shown unless the operator enables it

  const refresh = () => void queryClient.invalidateQueries({ queryKey: lanPlayKeys.party(party.id) });
  const run = async (fn: () => Promise<unknown>) => {
    setBusy(true);
    try {
      await fn();
      refresh();
    } catch (error) {
      toast.error(errorMessage(error));
    } finally {
      setBusy(false);
    }
  };

  const session = view?.session ?? null;

  return (
    <section className="border-t border-line px-4 py-3" data-testid="lanplay-panel">
      <div className="mb-2 flex items-center justify-between">
        <h3 className="flex items-center gap-2 text-sm font-semibold">
          <Network aria-hidden className="size-4" /> LAN Play
          <Badge tone="violet">Experimental</Badge>
        </h3>
        {session ? <Badge tone="neutral">{session.participants.length} in session</Badge> : null}
      </div>

      <TransportNotice status={status} />

      {!session ? (
        view?.you.canCreate ? (
          <div className="flex flex-wrap items-end gap-2">
            <SelectField
              label="Your device"
              labelHidden
              options={PLATFORMS}
              value={platform}
              onChange={(event) => setPlatform(event.target.value as LanPlayPlatform)}
              containerClassName="w-56"
            />
            <Button
              size="sm"
              icon={<PlugZap aria-hidden className="size-4" />}
              disabled={busy}
              onClick={() => void run(() => api.post(`/parties/${party.id}/lanplay`, { platform }))}
            >
              Create LAN session
            </Button>
          </div>
        ) : (
          <p className="text-xs text-muted">No LAN Play session yet. The party host can start one.</p>
        )
      ) : (
        <ActiveSession
          party={party}
          session={session}
          view={view!}
          meId={me.id}
          platform={platform}
          busy={busy}
          onPlatform={setPlatform}
          onJoin={() => void run(() => api.post(`/parties/${party.id}/lanplay/join`, { platform }))}
          onLeave={() => void run(() => api.post(`/parties/${party.id}/lanplay/leave`))}
          onConnect={() => void run(() => api.post(`/parties/${party.id}/lanplay/connect`))}
          onSelectGame={(gameId) => void run(() => api.post(`/parties/${party.id}/lanplay/game`, { gameId }))}
          onEnd={async () => {
            if (await confirm({ title: 'End LAN Play session?', message: 'This ends the LAN Play session for everyone in the party.', confirmLabel: 'End', tone: 'danger' })) {
              void run(() => api.delete(`/parties/${party.id}/lanplay`));
            }
          }}
        />
      )}
    </section>
  );
}

function TransportNotice({ status }: { status: { transport: { available: boolean; reason: string | null } } }) {
  if (status.transport.available) return null;
  return (
    <div className="mb-3 flex items-start gap-2 rounded-md bg-warning/10 px-3 py-2 text-xs text-warning" role="status">
      <WifiOff aria-hidden className="mt-0.5 size-4 shrink-0" />
      <span>
        <strong>Transport not available.</strong>{' '}
        {status.transport.reason ?? 'No LAN Play network transport is connected yet, so no game traffic is carried.'}
      </span>
    </div>
  );
}

function ActiveSession({
  party,
  session,
  view,
  meId,
  platform,
  busy,
  onPlatform,
  onJoin,
  onLeave,
  onConnect,
  onSelectGame,
  onEnd,
}: {
  party: PartyDto;
  session: LanPlaySessionDto;
  view: PartyLanPlayDto;
  meId: string;
  platform: LanPlayPlatform;
  busy: boolean;
  onPlatform: (value: LanPlayPlatform) => void;
  onJoin: () => void;
  onLeave: () => void;
  onConnect: () => void;
  onSelectGame: (gameId: string | null) => void;
  onEnd: () => void;
}) {
  const isHost = session.hostId === meId;
  const gameOptions = [{ value: '', label: 'No game selected' }, ...LAN_PLAY_GAMES.map((game) => ({ value: game.id, label: `${game.title} (${game.platform.toUpperCase()})` }))];

  return (
    <div className="space-y-3">
      <ul className="space-y-1.5" data-testid="lanplay-participants">
        {session.participants.map((participant) => (
          <li key={participant.userId} className="flex items-center justify-between gap-2 text-sm">
            <span className="flex items-center gap-2">
              <Users aria-hidden className="size-4 text-muted" />
              {memberName(party, participant.userId)}
              {participant.userId === session.hostId ? <Badge tone="accent">Host</Badge> : null}
              <span className="text-xs text-muted">{participant.platform.toUpperCase()}</span>
            </span>
            <Badge tone={participant.connected ? 'success' : 'neutral'}>{participant.connected ? 'Connected' : 'Not connected'}</Badge>
          </li>
        ))}
      </ul>

      {isHost ? (
        <SelectField
          label="Game"
          options={gameOptions}
          value={session.gameId ?? ''}
          disabled={busy}
          onChange={(event) => onSelectGame(event.target.value || null)}
          containerClassName="max-w-sm"
          hint="Compatibility is shown honestly; a listed game is not proof it works over the tunnel yet."
        />
      ) : session.gameId ? (
        <p className="flex items-center gap-2 text-sm">
          <Gamepad2 aria-hidden className="size-4 text-muted" />
          {LAN_PLAY_GAMES.find((game) => game.id === session.gameId)?.title ?? session.gameId}
        </p>
      ) : null}

      <div className="flex flex-wrap items-end gap-2">
        {!view.you.inSession ? (
          <>
            <SelectField
              label="Your device"
              labelHidden
              options={PLATFORMS}
              value={platform}
              onChange={(event) => onPlatform(event.target.value as LanPlayPlatform)}
              containerClassName="w-56"
            />
            <Button size="sm" icon={<Plug aria-hidden className="size-4" />} disabled={busy || !view.you.canJoin} onClick={onJoin}>
              Join LAN session
            </Button>
          </>
        ) : (
          <>
            <Button
              size="sm"
              variant="secondary"
              icon={<PlugZap aria-hidden className="size-4" />}
              disabled={busy}
              onClick={onConnect}
              title="Attempts to open a transport connection; reports the reason when none is available."
            >
              Connect
            </Button>
            <Button size="sm" variant="ghost" icon={<LogOut aria-hidden className="size-4" />} disabled={busy} onClick={onLeave}>
              Leave
            </Button>
          </>
        )}
        {view.you.canTerminate ? (
          <Button size="sm" variant="ghost" className="text-danger hover:text-danger" icon={<XCircle aria-hidden className="size-4" />} disabled={busy} onClick={onEnd}>
            End session
          </Button>
        ) : null}
      </div>
    </div>
  );
}
