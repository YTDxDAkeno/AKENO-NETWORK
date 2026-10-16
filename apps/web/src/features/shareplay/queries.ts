import { useQuery } from '@tanstack/react-query';
import type { PartySharePlayDto, SharePlayStatusDto } from '@akeno/shared';
import { api } from '../../lib/api';

/** SharePlay query keys (kept here, not in lib/query-keys.ts, to keep the feature self-contained). */
export const sharePlayKeys = {
  all: ['shareplay'] as const,
  status: ['shareplay', 'status'] as const,
  party: (partyId: string) => ['shareplay', 'party', partyId] as const,
};

export function useSharePlayStatus() {
  return useQuery({ queryKey: sharePlayKeys.status, queryFn: () => api.get<SharePlayStatusDto>('/shareplay/status'), staleTime: 5 * 60_000 });
}

export function usePartySharePlay(partyId: string | null | undefined, enabled = true) {
  return useQuery({
    queryKey: sharePlayKeys.party(partyId ?? 'none'),
    queryFn: () => api.get<PartySharePlayDto>(`/parties/${partyId}/shareplay`),
    enabled: Boolean(partyId) && enabled,
  });
}

/** Fired by the realtime provider for "shareplay.changed"; the SharePlay provider listens. */
export const SHAREPLAY_EVENT = 'akeno:shareplay';
export interface SharePlayEventDetail {
  partyId: string;
  ended: boolean;
}
