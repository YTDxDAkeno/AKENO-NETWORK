import { useQueryClient } from '@tanstack/react-query';
import { Cast, Eye, LogOut, Maximize, Minimize, MonitorPlay, MonitorUp, Radio, Signal, SignalLow, SquareStop, Users, Volume2, VolumeX, WifiOff } from 'lucide-react';
import { useEffect, useRef, useState } from 'react';
import {
  SHAREPLAY_QUALITY_PRESETS,
  type PartyDto,
  type PartySharePlayDto,
  type SharePlayQuality,
  type SharePlaySessionDto,
  type SharePlaySource,
  type SharePlayStatusDto,
} from '@akeno/shared';
import { Badge } from '../../components/Badge';
import { Button } from '../../components/Button';
import { cn } from '../../components/cn';
import { useConfirm } from '../../components/ConfirmDialog';
import { SelectField } from '../../components/Field';
import { useToast } from '../../components/Toast';
import { api, errorMessage } from '../../lib/api';
import { useCurrentUser } from '../auth/AuthProvider';
import { PlayerTwoPanel } from './PlayerTwoPanel';
import { sharePlayKeys, usePartySharePlay, useSharePlayStatus } from './queries';
import { isStreamActive, useStream, useStreamController } from './SharePlayProvider';
import { canWatchStreams, captureSupport, type StreamSnapshot } from './stream-controller';

const SOURCE_LABEL: Record<SharePlaySource, string> = {
  screen: 'A window or screen (for example PS Remote Play)',
  'capture-device': 'Capture card (HDMI from the PS5)',
  'ps5-native': 'Directly from the PS5 (not supported yet)',
};

function memberName(party: PartyDto, userId: string): string {
  return party.members.find((member) => member.user.id === userId)?.user.displayName ?? 'Someone';
}

function useCaptureDevices(enabled: boolean): { devices: MediaDeviceInfo[]; refresh: () => void } {
  const [devices, setDevices] = useState<MediaDeviceInfo[]>([]);
  const refresh = () => {
    void navigator.mediaDevices
      ?.enumerateDevices()
      .then((all) => setDevices(all.filter((device) => device.kind === 'videoinput')))
      .catch(() => setDevices([]));
  };
  useEffect(() => {
    if (enabled) refresh();
  }, [enabled]);
  return { devices, refresh };
}

function StartSharePlay({ party, status }: { party: PartyDto; status: SharePlayStatusDto }) {
  const controller = useStreamController();
  const confirm = useConfirm();
  const support = captureSupport();
  const [source, setSource] = useState<SharePlaySource>(support.screen ? 'screen' : 'capture-device');
  const [quality, setQuality] = useState<SharePlayQuality>(status.defaultQuality);
  const [deviceId, setDeviceId] = useState('');
  const { devices, refresh } = useCaptureDevices(source === 'capture-device');

  if (!support.screen && !support['capture-device']) {
    return <p className="text-xs text-muted">Streaming needs a desktop browser (Chrome or Edge on Windows). You can still watch streams here.</p>;
  }

  const start = async () => {
    if (source === 'ps5-native') return;
    const ok = await confirm({
      title: 'Start SharePlay?',
      tone: 'primary',
      confirmLabel: source === 'screen' ? 'Choose what to share' : 'Start streaming',
      message: (
        <span className="flex flex-col gap-2">
          <span>Everyone in this party who chooses to watch will see and hear what you share, live. Nothing is recorded.</span>
          {source === 'screen' ? (
            <span>
              Share <strong>a single window</strong> (for example PS Remote Play) rather than your whole screen: notifications, messages and passwords
              shown on a shared screen are streamed too. The AKENO tab itself cannot be selected.
            </span>
          ) : (
            <span>The capture card’s picture and sound are streamed. If the picture stays black, turn off HDCP on the PS5 (Settings → System → HDMI).</span>
          )}
        </span>
      ),
    });
    if (ok) await controller?.startBroadcast(party.id, source, quality, deviceId || undefined);
  };

  const sourceOptions = (Object.keys(SOURCE_LABEL) as SharePlaySource[]).filter((key) => key === 'ps5-native' || support[key as 'screen' | 'capture-device']);
  return (
    <div className="flex flex-col gap-3" data-testid="shareplay-start">
      <div className="grid gap-3 sm:grid-cols-3">
        <SelectField
          label="Stream from"
          value={source}
          onChange={(event) => setSource(event.target.value as SharePlaySource)}
          options={sourceOptions.map((key) => ({ value: key, label: SOURCE_LABEL[key] }))}
          data-testid="shareplay-source"
        />
        {source === 'capture-device' ? (
          <SelectField
            label="Capture device"
            value={deviceId}
            onFocus={refresh}
            onChange={(event) => setDeviceId(event.target.value)}
            options={[{ value: '', label: 'Default camera input' }, ...devices.map((device, index) => ({ value: device.deviceId, label: device.label || `Video input ${index + 1}` }))]}
          />
        ) : null}
        <SelectField
          label="Quality"
          value={quality}
          onChange={(event) => setQuality(event.target.value as SharePlayQuality)}
          options={status.qualities.map((id) => ({ value: id, label: SHAREPLAY_QUALITY_PRESETS[id].label }))}
          data-testid="shareplay-quality"
        />
      </div>
      {source === 'ps5-native' ? (
        <p className="text-xs text-warning" data-testid="shareplay-ps5-native">
          {status.sources['ps5-native'].reason}
        </p>
      ) : null}
      <div className="flex flex-wrap items-center gap-3">
        <Button variant="primary" icon={<Cast aria-hidden className="size-4" />} disabled={source === 'ps5-native'} onClick={() => void start()} data-testid="shareplay-start-button">
          Start SharePlay
        </Button>
        <p className="text-xs text-muted">Up to {status.maxViewers} viewers. Your browser asks what to share; nothing starts before that.</p>
      </div>
    </div>
  );
}

function QualityIcon({ quality }: { quality: StreamSnapshot['quality'] }) {
  if (quality === 'lost') return <WifiOff aria-label="Connection lost" className="size-4 text-danger" />;
  if (quality === 'poor') return <SignalLow aria-label="Poor connection" className="size-4 text-warning" />;
  return <Signal aria-label={quality === 'unknown' ? 'Connecting' : `${quality} connection`} className="size-4 text-status-online" />;
}

function statsLine(stream: StreamSnapshot): string[] {
  const parts: string[] = [];
  const stats = stream.role === 'host' ? stream.send : stream.receive;
  if (!stats) return parts;
  if (stats.width && stats.height) parts.push(`${stats.width}×${stats.height}`);
  if (stats.framesPerSecond) parts.push(`${Math.round(stats.framesPerSecond)} fps`);
  if (stats.bitrateKbps !== null) parts.push(stats.bitrateKbps >= 1000 ? `${(stats.bitrateKbps / 1000).toFixed(1)} Mbit/s` : `${stats.bitrateKbps} kbit/s`);
  if (stats.codec) parts.push(stats.codec);
  if (stats.roundTripMs !== null) parts.push(`RTT ${stats.roundTripMs} ms`);
  if (stream.receive && stream.role === 'viewer') {
    if (stream.receive.bufferMs !== null) parts.push(`buffer ${stream.receive.bufferMs} ms`);
    if (stream.receive.packetLoss !== null) parts.push(`loss ${(stream.receive.packetLoss * 100).toFixed(1)}%`);
  }
  if (stream.send?.limitation) parts.push(`limited by ${stream.send.limitation}`);
  return parts;
}

function HostControls({ party, session, status }: { party: PartyDto; session: SharePlaySessionDto; status: SharePlayStatusDto }) {
  const controller = useStreamController();
  const stream = useStream();
  const toast = useToast();
  const queryClient = useQueryClient();
  const broadcasting = isStreamActive(stream) && stream?.role === 'host' && stream.partyId === party.id;
  const stop = async () => {
    if (broadcasting) await controller?.stopBroadcast();
    else await controller?.leave();
    if (!broadcasting) {
      // This tab is not the one capturing (reloaded, or another device): stop on the server.
      await api.post(`/parties/${party.id}/shareplay/stop`).catch((error: unknown) => toast.error('Could not stop SharePlay', errorMessage(error)));
    }
    void queryClient.invalidateQueries({ queryKey: sharePlayKeys.party(party.id) });
  };
  return (
    <div className="flex flex-col gap-2" data-testid="shareplay-host">
      {broadcasting ? (
        <p className="flex flex-wrap items-center gap-2 text-sm" role="status" data-testid="shareplay-capture-indicator">
          <span className="relative flex size-2.5">
            <span className="absolute inline-flex size-full animate-ping rounded-full bg-danger opacity-75" />
            <span className="relative inline-flex size-2.5 rounded-full bg-danger" />
          </span>
          <span className="font-medium text-ink">You are sharing{stream?.captureLabel ? `: ${stream.captureLabel}` : ''}</span>
          {stream?.hasAudio ? <Badge tone="accent">with sound</Badge> : <Badge>no sound</Badge>}
          {stream ? <QualityIcon quality={stream.quality} /> : null}
          <span className="text-xs text-muted" data-testid="shareplay-send-stats">
            {stream ? statsLine(stream).join(' · ') : ''}
          </span>
        </p>
      ) : (
        <p className="text-xs text-warning" role="status">
          This browser tab is not capturing anything for your stream (it was reloaded or started elsewhere). Stop SharePlay and start it again to share.
        </p>
      )}
      <div className="flex flex-wrap items-end gap-2">
        {broadcasting ? (
          <SelectField
            label="Quality"
            containerClassName="w-56"
            value={session.quality}
            onChange={(event) => void controller?.setQuality(event.target.value as SharePlayQuality).catch((error: unknown) => toast.error('Could not change quality', errorMessage(error)))}
            options={status.qualities.map((id) => ({ value: id, label: SHAREPLAY_QUALITY_PRESETS[id].label }))}
          />
        ) : null}
        <Button variant="danger" icon={<SquareStop aria-hidden className="size-4" />} onClick={() => void stop()} data-testid="shareplay-stop">
          Stop SharePlay
        </Button>
      </div>
    </div>
  );
}

function StreamPlayer({ party, session }: { party: PartyDto; session: SharePlaySessionDto }) {
  const controller = useStreamController();
  const stream = useStream();
  const videoRef = useRef<HTMLVideoElement>(null);
  const frameRef = useRef<HTMLDivElement>(null);
  const [fullscreen, setFullscreen] = useState(false);
  useEffect(() => {
    controller?.attachVideo(videoRef.current);
    return () => controller?.attachVideo(null);
  }, [controller]);
  useEffect(() => {
    const onChange = () => setFullscreen(document.fullscreenElement === frameRef.current && frameRef.current !== null);
    document.addEventListener('fullscreenchange', onChange);
    return () => document.removeEventListener('fullscreenchange', onChange);
  }, []);
  if (!stream) return null;
  const toggleFullscreen = () => {
    if (document.fullscreenElement) void document.exitFullscreen().catch(() => {});
    else void frameRef.current?.requestFullscreen?.().catch(() => {});
  };
  const label = memberName(party, session.hostId);
  return (
    <div className="flex flex-col gap-2" data-testid="shareplay-viewer" data-stream-status={stream.status}>
      <div ref={frameRef} className={cn('relative overflow-hidden rounded-xl border border-line bg-black', fullscreen ? 'flex items-center' : 'aspect-video')}>
        <video ref={videoRef} className="size-full object-contain" autoPlay playsInline muted data-testid="shareplay-video" onDoubleClick={toggleFullscreen} />
        {!stream.receiving ? (
          <div className="absolute inset-0 grid place-items-center text-center text-sm text-muted">
            {stream.status === 'reconnecting' ? 'Reconnecting…' : session.state === 'host-reconnecting' ? `Waiting for ${label} to reconnect…` : stream.status === 'connecting' ? 'Connecting…' : `Waiting for ${label}’s video…`}
          </div>
        ) : null}
        <div className="pointer-events-none absolute left-2 top-2 flex items-center gap-1.5">
          <Badge tone="danger" icon={<Radio aria-hidden className="size-3" />}>
            LIVE
          </Badge>
          <span className="rounded-md bg-black/60 px-1.5 py-0.5 text-[11px] text-white" data-testid="shareplay-receive-stats">
            {statsLine(stream).join(' · ')}
          </span>
        </div>
        <div className="absolute inset-x-0 bottom-0 flex items-center gap-2 bg-gradient-to-t from-black/70 to-transparent px-3 py-2 text-white">
          <button type="button" className="rounded-lg p-1.5 hover:bg-white/15" onClick={() => controller?.setMuted(!stream.muted)} aria-label={stream.muted ? 'Unmute stream' : 'Mute stream'} aria-pressed={stream.muted} data-testid="shareplay-mute">
            {stream.muted ? <VolumeX aria-hidden className="size-4" /> : <Volume2 aria-hidden className="size-4" />}
          </button>
          <input
            type="range"
            min={0}
            max={1}
            step={0.05}
            value={stream.volume}
            onChange={(event) => controller?.setVolume(Number(event.target.value))}
            aria-label="Stream volume"
            className="w-24 accent-[var(--akeno-accent)]"
            disabled={!stream.hasAudio}
          />
          <span className="truncate text-xs">{stream.hasAudio ? `${label}’s game` : 'No game sound in this stream'}</span>
          <QualityIcon quality={stream.quality} />
          <button type="button" className="ml-auto rounded-lg p-1.5 hover:bg-white/15" onClick={toggleFullscreen} aria-label={fullscreen ? 'Exit full screen' : 'Full screen'} data-testid="shareplay-fullscreen">
            {fullscreen ? <Minimize aria-hidden className="size-4" /> : <Maximize aria-hidden className="size-4" />}
          </button>
        </div>
      </div>
      {!stream.canPlayback ? (
        <Button size="sm" variant="primary" className="w-fit" icon={<Volume2 aria-hidden className="size-4" />} onClick={() => void controller?.startPlayback()}>
          Tap to hear the game
        </Button>
      ) : null}
    </div>
  );
}

function stateBadge(session: SharePlaySessionDto) {
  if (session.state === 'live') return <Badge tone="danger">LIVE</Badge>;
  if (session.state === 'host-reconnecting') return <Badge tone="warning">Host reconnecting</Badge>;
  return <Badge tone="warning">Starting</Badge>;
}

/** SharePlay section of the party page. Only what this browser and server support is offered. */
export function SharePlayPanel({ party }: { party: PartyDto }) {
  const me = useCurrentUser();
  const status = useSharePlayStatus();
  const view = usePartySharePlay(party.id, status.data?.enabled === true);
  const controller = useStreamController();
  const stream = useStream();

  if (!status.data) return null;
  if (!status.data.enabled) {
    return (
      <p className="flex items-center gap-2 border-b border-line px-5 py-2.5 text-xs text-muted" data-testid="shareplay-panel" data-shareplay="unavailable">
        <MonitorPlay aria-hidden className="size-3.5 shrink-0" />
        {status.data.reason ?? 'SharePlay is not available.'}
      </p>
    );
  }
  const data: PartySharePlayDto | undefined = view.data;
  const session = data?.session ?? null;
  const watchingHere = isStreamActive(stream) && stream?.role === 'viewer' && stream.partyId === party.id;
  const ownError = stream && stream.partyId === null && (stream.status === 'error' || stream.status === 'ended') ? (stream.error ?? stream.notice) : null;

  return (
    <section className="flex flex-col gap-3 border-b border-line px-5 py-4" aria-labelledby="shareplay-title" data-testid="shareplay-panel" data-shareplay={session ? session.state : 'none'}>
      <div className="flex flex-wrap items-center gap-2">
        <h3 id="shareplay-title" className="flex items-center gap-2 text-sm font-semibold">
          <MonitorUp aria-hidden className="size-4 text-accent" /> SharePlay
        </h3>
        <Badge tone="violet">Experimental</Badge>
        {session ? (
          <>
            {stateBadge(session)}
            <span className="text-xs text-muted" data-testid="shareplay-summary">
              Streaming: <strong className="text-ink">{memberName(party, session.hostId)}</strong> · {SHAREPLAY_QUALITY_PRESETS[session.quality].label.split(' (')[0]}
            </span>
            <span className="inline-flex items-center gap-1 text-xs text-muted" data-testid="shareplay-viewer-count">
              <Users aria-hidden className="size-3.5" /> {session.viewerIds.length}/{session.maxViewers} watching
            </span>
          </>
        ) : null}
      </div>

      {!session && data?.you.canStart ? <StartSharePlay party={party} status={status.data} /> : null}
      {!session && !data?.you.canStart && data ? <p className="text-xs text-muted">{data.reason ?? 'Nobody is streaming.'}</p> : null}

      {session && session.hostId === me.id ? <HostControls party={party} session={session} status={status.data} /> : null}

      {session && session.hostId !== me.id ? (
        canWatchStreams() ? (
          <div className="flex flex-wrap items-center gap-2">
            {watchingHere ? (
              <Button variant="ghost" icon={<LogOut aria-hidden className="size-4" />} onClick={() => void controller?.leave()} data-testid="shareplay-leave">
                Leave stream
              </Button>
            ) : (
              <Button variant="primary" icon={<Eye aria-hidden className="size-4" />} disabled={session.state !== 'live'} onClick={() => void controller?.watch(party.id)} data-testid="shareplay-watch">
                Watch stream
              </Button>
            )}
            {session.state !== 'live' && !watchingHere ? <span className="text-xs text-muted">The stream starts as soon as {memberName(party, session.hostId)} shares something.</span> : null}
          </div>
        ) : (
          <p className="text-xs text-muted">This browser cannot play live streams (no WebRTC). Watch on a PC or phone instead.</p>
        )
      ) : null}

      {session && watchingHere ? <StreamPlayer party={party} session={session} /> : null}
      {session && data ? <PlayerTwoPanel party={party} session={session} view={data} status={status.data} /> : null}

      {stream?.notice && (stream.partyId === party.id || stream.status === 'ended') ? (
        <p className="text-xs text-warning" role="status" data-testid="shareplay-notice">
          {stream.notice}
        </p>
      ) : null}
      {ownError && stream?.status === 'error' ? (
        <p className="text-sm text-danger" role="alert" data-testid="shareplay-error">
          {ownError}
        </p>
      ) : null}
    </section>
  );
}
