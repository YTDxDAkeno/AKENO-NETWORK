import { Headphones, HeadphoneOff, Mic, MicOff, PhoneOff, Radio, Settings2, Signal, SignalLow, SignalMedium, Volume2, VolumeX, WifiOff } from 'lucide-react';
import { useEffect, useRef, useState } from 'react';
import { Link } from 'react-router';
import type { PartyDto, PartyMemberDto } from '@akeno/shared';
import { Button } from '../../components/Button';
import { cn } from '../../components/cn';
import { useVoice, useVoiceController } from './VoiceProvider';
import type { VoiceQuality, VoiceSnapshot } from './voice-controller';
import { keyLabel, loadVoicePreferences, onVoicePreferences, playbackVolumeAdjustable, saveVoicePreferences, type VoicePreferences } from './voice-preferences';

const VOLUME_ADJUSTABLE = typeof window !== 'undefined' && playbackVolumeAdjustable();

export interface MemberVoiceState {
  connected: boolean;
  muted: boolean | null;
  speaking: boolean;
  quality: VoiceQuality | null;
}

function useVoicePreferences(): VoicePreferences {
  const [prefs, setPrefs] = useState(loadVoicePreferences);
  useEffect(() => onVoicePreferences(setPrefs), []);
  return prefs;
}

/** In this party's voice room (connected, connecting or reconnecting)? */
function inPartyVoice(voice: VoiceSnapshot | null, partyId: string): boolean {
  return Boolean(voice && voice.partyId === partyId && voice.status !== 'idle' && voice.status !== 'error');
}

/**
 * A member's voice state: live from the room when this user is connected to the same party
 * (speaking, muted, quality), otherwise what the server reports (connected or not).
 */
export function useMemberVoice(member: PartyMemberDto, partyId?: string): MemberVoiceState {
  const voice = useVoice();
  const live = voice && partyId && voice.partyId === partyId && voice.status === 'connected' ? voice.participants[member.user.id] : undefined;
  if (live) return { connected: true, muted: live.micMuted, speaking: live.speaking && !live.micMuted, quality: live.quality };
  return { connected: member.voice.connected, muted: member.voice.muted, speaking: false, quality: null };
}

export function VoiceStateLine({ state }: { state: MemberVoiceState }) {
  if (!state.connected) return null;
  return (
    <span className="mt-0.5 flex items-center gap-1 text-xs text-muted" data-testid="member-voice-state">
      {state.muted ? <MicOff aria-hidden className="size-3.5 text-danger" /> : <Mic aria-hidden className={cn('size-3.5', state.speaking ? 'text-status-online' : 'text-subtle')} />}
      {state.speaking ? <span className="font-medium text-status-online">Speaking</span> : state.muted ? 'In voice · muted' : 'In voice'}
      {state.quality === 'poor' || state.quality === 'lost' ? <SignalLow aria-label="Poor connection" className="size-3.5 text-warning" /> : null}
    </span>
  );
}

const QUALITY_LABEL: Record<VoiceQuality, string> = { excellent: 'Excellent connection', good: 'Good connection', poor: 'Poor connection', lost: 'Connection lost', unknown: 'Connecting' };

function QualityIcon({ quality }: { quality: VoiceQuality }) {
  const Icon = quality === 'excellent' ? Signal : quality === 'good' ? SignalMedium : quality === 'poor' ? SignalLow : quality === 'lost' ? WifiOff : Signal;
  return <Icon aria-label={QUALITY_LABEL[quality]} className={cn('size-4', quality === 'poor' ? 'text-warning' : quality === 'lost' ? 'text-danger' : 'text-status-online')} />;
}

function MicMeter({ level, active }: { level: number; active: boolean }) {
  return (
    <span className="inline-flex h-4 items-end gap-0.5" aria-hidden>
      {[0.12, 0.3, 0.5, 0.72].map((threshold) => (
        <span key={threshold} className={cn('w-1 rounded-sm transition-colors', level > threshold && active ? 'bg-status-online' : 'bg-line-strong')} style={{ height: `${6 + threshold * 12}px` }} />
      ))}
    </span>
  );
}

/** Press-and-hold button for push-to-talk on touch screens (and as a mouse alternative). */
function PushToTalkButton() {
  const controller = useVoiceController();
  const voice = useVoice();
  const prefs = useVoicePreferences();
  const release = () => controller?.setPushToTalk(false);
  return (
    <button
      type="button"
      className={cn(
        'inline-flex h-10 select-none items-center gap-2 rounded-xl border px-4 text-sm font-medium [touch-action:none]',
        voice?.pttHeld ? 'border-status-online bg-status-online/15 text-ink' : 'border-line-strong bg-surface-2 text-muted',
      )}
      onPointerDown={(event) => {
        event.currentTarget.setPointerCapture(event.pointerId);
        controller?.setPushToTalk(true);
      }}
      onPointerUp={release}
      onPointerCancel={release}
      onContextMenu={(event) => event.preventDefault()}
      aria-pressed={voice?.pttHeld ?? false}
    >
      <Radio aria-hidden className="size-4" /> {voice?.pttHeld ? 'Talking…' : `Hold to talk (${keyLabel(prefs.pttKey)})`}
    </button>
  );
}

/** Voice controls of the party page. Nothing is shown as available that is not. */
export function PartyVoicePanel({ party }: { party: PartyDto }) {
  const controller = useVoiceController();
  const voice = useVoice();
  const prefs = useVoicePreferences();

  if (!party.voice.available) {
    return (
      <p className="flex items-center gap-2 border-b border-line px-5 py-2.5 text-xs text-muted" data-testid="party-voice">
        <MicOff aria-hidden className="size-3.5 shrink-0" />
        {party.voice.reason ?? 'Voice chat is not available.'}
      </p>
    );
  }

  const here = inPartyVoice(voice, party.id);
  const elsewhere = voice && voice.partyId && voice.partyId !== party.id && voice.status !== 'idle' && voice.status !== 'error';
  const status = here ? voice!.status : 'idle';

  if (!here) {
    return (
      <div className="flex flex-col gap-2 border-b border-line px-5 py-3.5" data-testid="party-voice">
        <div className="flex flex-wrap items-center gap-3">
          <Button
            variant="primary"
            icon={<Headphones aria-hidden className="size-4" />}
            onClick={() => void controller?.join(party.id)}
            data-testid="join-voice"
          >
            Join voice
          </Button>
          <p className="text-xs text-muted">Your browser asks for microphone access the first time. Nothing is recorded.</p>
        </div>
        {voice?.status === 'error' && voice.error ? (
          <p role="alert" className="text-sm text-danger" data-testid="voice-error">
            {voice.error}
          </p>
        ) : null}
        {elsewhere ? <p className="text-xs text-warning">Joining here leaves the voice chat of your previous party.</p> : null}
      </div>
    );
  }

  const v = voice!;
  const connecting = status === 'requesting-microphone' || status === 'connecting';
  return (
    <div className="flex flex-col gap-2 border-b border-line bg-surface/40 px-5 py-3" data-testid="party-voice" data-voice-status={status}>
      <div className="flex flex-wrap items-center gap-2">
        <span className="mr-1 inline-flex items-center gap-2 text-sm font-medium" data-testid="voice-status">
          {status === 'connected' ? <QualityIcon quality={v.quality} /> : <span className="size-2 animate-pulse rounded-full bg-warning" aria-hidden />}
          {status === 'connected'
            ? 'Connected to voice'
            : status === 'requesting-microphone'
              ? 'Waiting for microphone access…'
              : status === 'reconnecting'
                ? 'Reconnecting…'
                : 'Connecting…'}
        </span>
        {status === 'connected' ? <MicMeter level={v.micLevel} active={v.transmitting} /> : null}
        <div className="ml-auto flex flex-wrap items-center gap-2">
          <Button
            size="sm"
            variant={v.micMuted ? 'danger' : 'secondary'}
            disabled={connecting}
            onClick={() => void controller?.setMicMuted(!v.micMuted)}
            icon={v.micMuted ? <MicOff aria-hidden className="size-4" /> : <Mic aria-hidden className="size-4" />}
            aria-pressed={v.micMuted}
            data-testid="toggle-mute"
          >
            {v.micMuted ? 'Unmute' : 'Mute'}
          </Button>
          <Button
            size="sm"
            variant={v.deafened ? 'danger' : 'ghost'}
            disabled={connecting}
            onClick={() => void controller?.setDeafened(!v.deafened)}
            icon={v.deafened ? <HeadphoneOff aria-hidden className="size-4" /> : <Headphones aria-hidden className="size-4" />}
            aria-pressed={v.deafened}
            title="Deafen: hear nobody and mute your microphone"
          >
            {v.deafened ? 'Undeafen' : 'Deafen'}
          </Button>
          <Link to="/settings?tab=voice" className="rounded-lg p-2 text-muted hover:bg-surface-2 hover:text-ink" aria-label="Voice and audio settings">
            <Settings2 aria-hidden className="size-4" />
          </Link>
          <Button size="sm" variant="ghost" className="text-danger hover:text-danger" icon={<PhoneOff aria-hidden className="size-4" />} onClick={() => void controller?.leave()} data-testid="leave-voice">
            Leave voice
          </Button>
        </div>
      </div>
      {prefs.mode === 'ptt' && status === 'connected' ? <PushToTalkButton /> : null}
      {!v.canPlaybackAudio ? (
        <Button size="sm" variant="primary" className="w-fit" icon={<Volume2 aria-hidden className="size-4" />} onClick={() => void controller?.startAudio()}>
          Tap to hear the party
        </Button>
      ) : null}
      {v.notice ? (
        <p className="flex flex-wrap items-center gap-2 text-xs text-warning" role="status">
          {v.notice}
          {/microphone was interrupted/i.test(v.notice) ? (
            <button type="button" className="font-medium underline" onClick={() => void controller?.resumeMicrophone()}>
              Resume microphone
            </button>
          ) : null}
        </p>
      ) : null}
    </div>
  );
}

/** Per-person volume and local mute (only affects what you hear). */
export function MemberVolumeControl({ member }: { member: PartyMemberDto }) {
  const prefs = useVoicePreferences();
  const [open, setOpen] = useState(false);
  const ref = useRef<HTMLDivElement>(null);
  const id = member.user.id;
  const volume = prefs.userVolumes[id] ?? 1;
  const mutedLocally = Boolean(prefs.localMutes[id]);
  useEffect(() => {
    if (!open) return;
    const close = (event: PointerEvent) => {
      if (!ref.current?.contains(event.target as Node)) setOpen(false);
    };
    document.addEventListener('pointerdown', close);
    return () => document.removeEventListener('pointerdown', close);
  }, [open]);
  return (
    <div className="relative" ref={ref}>
      <button
        type="button"
        onClick={() => setOpen((value) => !value)}
        className="rounded-lg p-1.5 text-muted hover:bg-surface-2 hover:text-ink"
        aria-label={`Volume of ${member.user.displayName}`}
        aria-expanded={open}
      >
        {mutedLocally ? <VolumeX aria-hidden className="size-4 text-danger" /> : <Volume2 aria-hidden className="size-4" />}
      </button>
      {open ? (
        <div className="absolute right-0 top-full z-20 mt-1 w-56 rounded-xl border border-line bg-surface p-3 shadow-card">
          {VOLUME_ADJUSTABLE ? (
            <label className="flex flex-col gap-1.5 text-xs font-medium text-muted">
              Volume for you · {Math.round(volume * 100)}%
              <input
                type="range"
                min={0}
                max={1}
                step={0.05}
                value={volume}
                onChange={(event) => saveVoicePreferences({ userVolumes: { ...prefs.userVolumes, [id]: Number(event.target.value) } })}
                className="accent-[var(--akeno-accent)]"
              />
            </label>
          ) : (
            <p className="text-xs text-muted">This browser does not allow per-person volume. You can still mute them for yourself.</p>
          )}
          <button
            type="button"
            className="mt-2 inline-flex items-center gap-1.5 text-xs font-medium text-ink hover:underline"
            onClick={() => {
              const next = { ...prefs.localMutes };
              if (mutedLocally) delete next[id];
              else next[id] = true;
              saveVoicePreferences({ localMutes: next });
            }}
          >
            {mutedLocally ? <Volume2 aria-hidden className="size-3.5" /> : <VolumeX aria-hidden className="size-3.5" />}
            {mutedLocally ? 'Unmute for me' : 'Mute for me'}
          </button>
        </div>
      ) : null}
    </div>
  );
}
