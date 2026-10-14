import { Headphones, Mic, MicOff, PhoneOff } from 'lucide-react';
import { useEffect } from 'react';
import { Link } from 'react-router';
import { cn } from '../../components/cn';
import { useCurrentParty } from '../../lib/queries';
import { useVoice, useVoiceController } from './VoiceProvider';

/**
 * Leaves voice when the user is no longer in that party (left, removed, party closed — also when
 * it happened on another device). The server removes them from the room as well.
 */
export function VoicePartyWatcher() {
  const controller = useVoiceController();
  const voice = useVoice();
  const party = useCurrentParty();
  const voicePartyId = voice && voice.status !== 'idle' && voice.status !== 'error' ? voice.partyId : null;
  const currentId = party.data?.id ?? null;
  // Only trust settled data: right after joining a party the cached "current party" is stale
  // until the refetch completes, and acting on it would drop the voice connection just made.
  const loaded = party.isSuccess && !party.isFetching;
  useEffect(() => {
    if (voicePartyId && loaded && currentId !== voicePartyId) void controller?.leave();
  }, [voicePartyId, currentId, loaded, controller]);
  return null;
}

/** Compact voice controls shown on every page while connected (voice continues across pages). */
export function VoiceBar({ className }: { className?: string }) {
  const controller = useVoiceController();
  const voice = useVoice();
  const party = useCurrentParty();
  if (!voice || voice.status === 'idle' || voice.status === 'error' || !voice.partyId) return null;
  const speakers = Object.values(voice.participants).filter((participant) => participant.speaking && !participant.micMuted).length;
  return (
    <div className={cn('flex items-center gap-2 rounded-xl border border-line bg-surface-2 px-3 py-2', className)} data-testid="voice-bar">
      <Link to="/parties" className="flex min-w-0 flex-1 items-center gap-2">
        <Headphones aria-hidden className={cn('size-4 shrink-0', voice.status === 'connected' ? 'text-status-online' : 'text-warning')} />
        <span className="min-w-0">
          <span className="block truncate text-xs font-semibold text-ink">{voice.status === 'connected' ? 'Voice connected' : 'Reconnecting…'}</span>
          <span className="block truncate text-[11px] text-muted">
            {party.data?.name ?? 'Party'}
            {speakers > 0 ? ` · ${speakers} speaking` : ''}
          </span>
        </span>
      </Link>
      <button
        type="button"
        onClick={() => void controller?.setMicMuted(!voice.micMuted)}
        className={cn('rounded-lg p-2', voice.micMuted ? 'text-danger hover:bg-danger-soft' : 'text-muted hover:bg-surface-3 hover:text-ink')}
        aria-label={voice.micMuted ? 'Unmute microphone' : 'Mute microphone'}
        aria-pressed={voice.micMuted}
      >
        {voice.micMuted ? <MicOff aria-hidden className="size-4" /> : <Mic aria-hidden className="size-4" />}
      </button>
      <button type="button" onClick={() => void controller?.leave()} className="rounded-lg p-2 text-danger hover:bg-danger-soft" aria-label="Leave voice">
        <PhoneOff aria-hidden className="size-4" />
      </button>
    </div>
  );
}
