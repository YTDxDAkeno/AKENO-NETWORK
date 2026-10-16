import { useMutation, useQueryClient } from '@tanstack/react-query';
import { Check, Gamepad2, Hand, ShieldAlert, Undo2, X } from 'lucide-react';
import { useEffect, useState, useSyncExternalStore } from 'react';
import { BUTTON, INPUT_AXIS_MAX, type ButtonName, type ControllerState, type PartyDto, type PartySharePlayDto, type SharePlaySessionDto, type SharePlayStatusDto } from '@akeno/shared';
import { Badge } from '../../components/Badge';
import { Button } from '../../components/Button';
import { cn } from '../../components/cn';
import { useToast } from '../../components/Toast';
import { api, errorMessage } from '../../lib/api';
import { useCurrentUser } from '../auth/AuthProvider';
import { listPads, type PadInfo } from './player-two';
import { sharePlayKeys } from './queries';
import { usePlayerTwo } from './SharePlayProvider';

const LABELS: Array<[ButtonName, string]> = [
  ['south', 'A / ✕'],
  ['east', 'B / ○'],
  ['west', 'X / □'],
  ['north', 'Y / △'],
  ['l1', 'L1'],
  ['r1', 'R1'],
  ['l2', 'L2'],
  ['r2', 'R2'],
  ['options', 'Options'],
  ['l3', 'L3'],
  ['r3', 'R3'],
  ['dpadUp', '↑'],
  ['dpadDown', '↓'],
  ['dpadLeft', '←'],
  ['dpadRight', '→'],
  ['touchpad', 'Touchpad'],
];

function Stick({ x, y, label }: { x: number; y: number; label: string }) {
  const left = 50 + (x / INPUT_AXIS_MAX) * 42;
  const top = 50 + (y / INPUT_AXIS_MAX) * 42;
  return (
    <div className="flex flex-col items-center gap-1">
      <div className="relative size-16 rounded-full border border-line-strong bg-surface-3" aria-label={`${label} stick x ${x}, y ${y}`} role="img">
        <span className="absolute size-3 -translate-x-1/2 -translate-y-1/2 rounded-full bg-accent" style={{ left: `${left}%`, top: `${top}%` }} />
      </div>
      <span className="text-[11px] text-muted">{label}</span>
    </div>
  );
}

function Trigger({ value, label }: { value: number; label: string }) {
  return (
    <div className="flex flex-col items-center gap-1">
      <div className="relative h-16 w-3 overflow-hidden rounded-full bg-surface-3" role="img" aria-label={`${label} ${value}`}>
        <span className="absolute inset-x-0 bottom-0 bg-accent" style={{ height: `${(value / 255) * 100}%` }} />
      </div>
      <span className="text-[11px] text-muted">{label}</span>
    </div>
  );
}

/** Live view of a controller state (buttons, sticks, triggers). */
export function ControllerStateView({ state, testId }: { state: ControllerState; testId: string }) {
  const pressed = LABELS.filter(([name]) => (state.buttons & (1 << BUTTON[name])) !== 0).map(([name]) => name);
  return (
    <div className="flex flex-wrap items-center gap-4 rounded-xl border border-line bg-surface/60 p-3" data-testid={testId} data-buttons={state.buttons} data-pressed={pressed.join(' ')} data-axes={state.axes.join(',')} data-triggers={state.triggers.join(',')}>
      <div className="flex max-w-xs flex-wrap gap-1">
        {LABELS.map(([name, label]) => (
          <span key={name} className={cn('rounded-md px-1.5 py-0.5 text-[11px] font-medium', (state.buttons & (1 << BUTTON[name])) !== 0 ? 'bg-accent text-on-accent' : 'bg-surface-3 text-subtle')}>
            {label}
          </span>
        ))}
      </div>
      <Stick x={state.axes[0]} y={state.axes[1]} label="Left" />
      <Stick x={state.axes[2]} y={state.axes[3]} label="Right" />
      <Trigger value={state.triggers[0]} label="L2" />
      <Trigger value={state.triggers[1]} label="R2" />
    </div>
  );
}

function memberName(party: PartyDto, userId: string): string {
  return party.members.find((member) => member.user.id === userId)?.user.displayName ?? 'Someone';
}

function usePads(): PadInfo[] {
  const [pads, setPads] = useState<PadInfo[]>(() => (typeof navigator !== 'undefined' ? listPads() : []));
  useEffect(() => {
    const refresh = () => setPads(listPads());
    window.addEventListener('gamepadconnected', refresh);
    window.addEventListener('gamepaddisconnected', refresh);
    const timer = window.setInterval(refresh, 1000);
    return () => {
      window.removeEventListener('gamepadconnected', refresh);
      window.removeEventListener('gamepaddisconnected', refresh);
      window.clearInterval(timer);
    };
  }, []);
  return pads;
}

function GuestPlayerTwo({ party, session, view }: { party: PartyDto; session: SharePlaySessionDto; view: PartySharePlayDto }) {
  const { sender } = usePlayerTwo();
  const snapshot = useSyncExternalStore(sender.subscribe, sender.getSnapshot);
  const toast = useToast();
  const queryClient = useQueryClient();
  const pads = usePads();
  const [padIndex, setPadIndex] = useState<number | null>(null);
  const grant = session.controller.grant;
  const mine = grant !== null && view.you.hasControllerGrant;
  const action = useMutation({
    mutationFn: (path: string) => api.post(`/parties/${party.id}/shareplay/controller/${path}`),
    onSettled: () => void queryClient.invalidateQueries({ queryKey: sharePlayKeys.party(party.id) }),
    onError: (error) => toast.error('Player 2', errorMessage(error)),
  });

  // The grant ended (revoked, expired, new epoch): stop sending at once.
  useEffect(() => {
    if (snapshot.partyId === party.id && snapshot.status !== 'idle' && (!mine || grant?.epoch !== snapshot.epoch)) sender.stop();
  }, [mine, grant?.epoch, snapshot.partyId, snapshot.status, snapshot.epoch, party.id, sender]);
  useEffect(() => () => sender.stop(), [sender]);

  const sending = snapshot.partyId === party.id && snapshot.status !== 'idle' && snapshot.status !== 'ended' && snapshot.status !== 'error';
  if (grant && !mine) {
    return (
      <p className="text-xs text-muted" data-testid="player2-taken">
        <Gamepad2 aria-hidden className="mr-1 inline size-3.5" /> {memberName(party, grant.guestId)} is Player 2.
      </p>
    );
  }
  if (!mine) {
    return (
      <div className="flex flex-wrap items-center gap-2" data-testid="player2-guest">
        {view.you.controllerRequestPending ? (
          <>
            <span className="text-sm text-muted" role="status" data-testid="player2-pending">
              Waiting for {memberName(party, session.hostId)} to answer…
            </span>
            <Button size="sm" variant="ghost" onClick={() => action.mutate('cancel')}>
              Cancel request
            </Button>
          </>
        ) : (
          <Button size="sm" variant="secondary" icon={<Hand aria-hidden className="size-4" />} disabled={!view.you.canRequestController} loading={action.isPending} onClick={() => action.mutate('request')} data-testid="player2-request">
            Request Player 2
          </Button>
        )}
        {snapshot.status === 'ended' && snapshot.notice ? <span className="text-xs text-warning">{snapshot.notice}</span> : null}
      </div>
    );
  }
  return (
    <div className="flex flex-col gap-2" data-testid="player2-granted" data-sender-status={snapshot.status}>
      <p className="flex flex-wrap items-center gap-2 text-sm">
        <Badge tone="success" icon={<Check aria-hidden className="size-3" />}>
          Player 2 granted
        </Badge>
        <span className="text-muted">{memberName(party, session.hostId)} gave you Player 2 until {new Date(grant!.expiresAt).toLocaleTimeString()}.</span>
      </p>
      {!sending ? (
        <div className="flex flex-wrap items-end gap-2">
          <label className="flex flex-col gap-1 text-xs font-medium text-muted">
            Controller
            <select className="h-9 rounded-lg border border-line-strong bg-surface-2 px-2 text-sm text-ink" value={padIndex ?? ''} onChange={(event) => setPadIndex(event.target.value === '' ? null : Number(event.target.value))} data-testid="player2-pad-select">
              <option value="">{pads.length === 0 ? 'Press a button on your controller…' : 'First controller'}</option>
              {pads.map((pad) => (
                <option key={pad.index} value={pad.index}>
                  {pad.id.slice(0, 60)}
                  {pad.standard ? '' : ' (layout not recognized)'}
                </option>
              ))}
            </select>
          </label>
          <Button size="sm" variant="primary" icon={<Gamepad2 aria-hidden className="size-4" />} onClick={() => sender.start(party.id, grant!.epoch, padIndex)} data-testid="player2-use-controller">
            Use my controller
          </Button>
        </div>
      ) : (
        <>
          <p className="text-xs text-muted" role="status" data-testid="player2-sender-status">
            {snapshot.status === 'sending'
              ? `Sending ${snapshot.pad?.id ? `from ${snapshot.pad.id.slice(0, 40)}` : ''} · ${snapshot.packetsSent} packets`
              : snapshot.status === 'paused'
                ? 'Paused (input released)'
                : snapshot.status === 'waiting-for-controller'
                  ? 'Press a button on your controller…'
                  : snapshot.status === 'reconnecting'
                    ? 'Reconnecting…'
                    : 'Connecting…'}
          </p>
          <ControllerStateView state={snapshot.state} testId="player2-local-state" />
        </>
      )}
      {snapshot.notice ? <p className="text-xs text-warning">{snapshot.notice}</p> : null}
      {snapshot.error ? (
        <p className="text-xs text-danger" role="alert">
          {snapshot.error}
        </p>
      ) : null}
      <div className="flex flex-wrap gap-2">
        {sending ? (
          <Button size="sm" variant="ghost" onClick={() => sender.stop()}>
            Stop using my controller
          </Button>
        ) : null}
        <Button
          size="sm"
          variant="ghost"
          icon={<Undo2 aria-hidden className="size-4" />}
          onClick={() => {
            sender.stop();
            action.mutate('revoke');
          }}
          data-testid="player2-give-back"
        >
          Give back Player 2
        </Button>
      </div>
    </div>
  );
}

function HostPlayerTwo({ party, session }: { party: PartyDto; session: SharePlaySessionDto }) {
  const { monitor } = usePlayerTwo();
  const received = useSyncExternalStore(monitor.subscribe, monitor.getSnapshot);
  const toast = useToast();
  const queryClient = useQueryClient();
  const grant = session.controller.grant;
  const action = useMutation({
    mutationFn: ({ path, userId }: { path: string; userId?: string }) => api.post(`/parties/${party.id}/shareplay/controller/${path}`, userId ? { userId } : {}),
    onSettled: () => void queryClient.invalidateQueries({ queryKey: sharePlayKeys.party(party.id) }),
    onError: (error) => toast.error('Player 2', errorMessage(error)),
  });
  const grantId = grant?.grantId ?? null;
  useEffect(() => {
    if (!grantId) return;
    monitor.start(party.id);
    return () => monitor.stop();
  }, [grantId, party.id, monitor]);

  return (
    <div className="flex flex-col gap-2" data-testid="player2-host">
      {session.controller.requests.map((request) => (
        <div key={request.userId} className="flex flex-wrap items-center gap-2 rounded-xl border border-accent/40 bg-accent-soft/40 p-2.5" data-testid="player2-request-prompt" role="alert">
          <Gamepad2 aria-hidden className="size-4 text-accent" />
          <span className="text-sm font-medium">{memberName(party, request.userId)} wants to join as Player 2.</span>
          <span className="ml-auto flex gap-2">
            <Button size="sm" variant="primary" icon={<Check aria-hidden className="size-4" />} onClick={() => action.mutate({ path: 'approve', userId: request.userId })} data-testid="player2-allow">
              Allow
            </Button>
            <Button size="sm" variant="ghost" icon={<X aria-hidden className="size-4" />} onClick={() => action.mutate({ path: 'deny', userId: request.userId })} data-testid="player2-deny">
              Deny
            </Button>
          </span>
        </div>
      ))}
      {grant ? (
        <div className="flex flex-col gap-2" data-testid="player2-host-grant" data-monitor-connected={received.connected} data-input-connected={grant.inputConnected}>
          <p className="flex flex-wrap items-center gap-2 text-sm">
            <Badge tone="success">Player 2</Badge>
            <span>Player 2 control granted to {memberName(party, grant.guestId)}.</span>
            <span className="text-xs text-muted">
              {grant.inputConnected ? 'Controller connected' : 'Controller not connected yet'} · {received.packetsPerSecond} packets/s · {received.received} received
              {received.expired ? ' · no fresh input: neutral' : ''}
            </span>
            <Button size="sm" variant="danger" className="ml-auto" onClick={() => action.mutate({ path: 'revoke' })} data-testid="player2-revoke">
              Revoke Player 2
            </Button>
          </p>
          <ControllerStateView state={received.state} testId="player2-monitor-state" />
          {received.lastReason ? <p className="text-xs text-muted">{received.lastReason}</p> : null}
        </div>
      ) : null}
    </div>
  );
}

/** Player 2 (remote controller) section. Says plainly that it does not control PS5 games. */
export function PlayerTwoPanel({ party, session, view, status }: { party: PartyDto; session: SharePlaySessionDto; view: PartySharePlayDto; status: SharePlayStatusDto }) {
  const me = useCurrentUser();
  if (!status.remoteInput.supported) return null;
  const isHost = session.hostId === me.id;
  return (
    <div className="flex flex-col gap-2 rounded-xl border border-line bg-surface/40 p-3" data-testid="player2-panel">
      <p className="flex items-start gap-2 text-xs text-muted">
        <ShieldAlert aria-hidden className="mt-0.5 size-3.5 shrink-0 text-warning" />
        <span>
          <strong className="text-ink">Player 2 (experimental):</strong> {status.virtualController.reason} Only the host can allow it; the PS and Create buttons are never sent.
        </span>
      </p>
      {isHost ? <HostPlayerTwo party={party} session={session} /> : <GuestPlayerTwo party={party} session={session} view={view} />}
    </div>
  );
}
