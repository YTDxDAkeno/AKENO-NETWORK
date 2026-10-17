import { useQuery } from '@tanstack/react-query';
import type { LanPlayStatusDto, PartyLanPlayDto } from '@akeno/shared';
import { api } from '../../lib/api';

/** LAN Play query keys, kept in the feature to stay self-contained (like SharePlay). */
export const lanPlayKeys = {
  all: ['lanplay'] as const,
  status: ['lanplay', 'status'] as const,
  party: (partyId: string) => ['lanplay', 'party', partyId] as const,
};

export function useLanPlayStatus() {
  return useQuery({ queryKey: lanPlayKeys.status, queryFn: () => api.get<LanPlayStatusDto>('/lanplay/status'), staleTime: 5 * 60_000 });
}

export function usePartyLanPlay(partyId: string | null | undefined, enabled = true) {
  return useQuery({
    queryKey: lanPlayKeys.party(partyId ?? 'none'),
    queryFn: () => api.get<PartyLanPlayDto>(`/parties/${partyId}/lanplay`),
    enabled: Boolean(partyId) && enabled,
  });
}
