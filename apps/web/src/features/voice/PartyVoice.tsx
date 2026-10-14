import { Mic, MicOff } from 'lucide-react';
import type { PartyDto, PartyMemberDto } from '@akeno/shared';

export interface MemberVoiceState {
  connected: boolean;
  muted: boolean | null;
  speaking: boolean;
}

/** Voice state of a party member as reported by the server. */
export function useMemberVoice(member: PartyMemberDto): MemberVoiceState {
  return { connected: member.voice.connected, muted: member.voice.muted, speaking: false };
}

export function VoiceStateLine({ state }: { state: MemberVoiceState }) {
  if (!state.connected) return null;
  return (
    <span className="mt-0.5 flex items-center gap-1 text-xs text-muted">
      {state.muted ? <MicOff aria-hidden className="size-3.5 text-danger" /> : <Mic aria-hidden className="size-3.5 text-success" />}
      {state.muted ? 'In voice · muted' : 'In voice'}
    </span>
  );
}

/** Voice controls for the party; nothing is rendered while voice is unavailable on this server. */
export function PartyVoicePanel({ party }: { party: PartyDto }) {
  if (!party.voice.available) {
    return (
      <p className="flex items-center gap-2 border-b border-line px-5 py-2.5 text-xs text-muted" data-testid="party-voice">
        <MicOff aria-hidden className="size-3.5 shrink-0" />
        {party.voice.reason ?? 'Voice chat is not available.'}
      </p>
    );
  }
  return null;
}

export function MemberVolumeControl(_props: { member: PartyMemberDto }) {
  return null;
}
