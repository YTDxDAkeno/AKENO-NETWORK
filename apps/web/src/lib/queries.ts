import { useInfiniteQuery, useQuery } from '@tanstack/react-query';
import type {
  AdminStatsDto,
  AdminUserDto,
  AuditEventDto,
  AuthSessionDto,
  DeviceDto,
  BlockedUserDto,
  CurrentPartyResponse,
  DirectConversationDto,
  FriendDto,
  FriendRequestsResponse,
  GameActivityResponse,
  GameSessionDetailDto,
  GameSessionDto,
  GroupDetailDto,
  GroupDto,
  InvitationDto,
  MessagePage,
  NotificationPage,
  PartyDto,
  PublicProfile,
  RegistrationInvitationDto,
  UserSearchResult,
  VoiceStatusDto,
} from '@akeno/shared';
import { api, query } from './api';
import { queryKeys } from './query-keys';

export function useFriends() {
  return useQuery({
    queryKey: queryKeys.friends,
    queryFn: () => api.get<{ friends: FriendDto[] }>('/friends'),
    select: (data) => data.friends,
  });
}

export function useFriendRequests() {
  return useQuery({
    queryKey: queryKeys.friendRequests,
    queryFn: () => api.get<FriendRequestsResponse>('/friends/requests'),
  });
}

export function useBlockedUsers() {
  return useQuery({
    queryKey: queryKeys.blocks,
    queryFn: () => api.get<{ blocked: BlockedUserDto[] }>('/blocks'),
    select: (data) => data.blocked,
  });
}

export function useUserSearch(q: string) {
  const trimmed = q.trim();
  return useQuery({
    queryKey: queryKeys.search(trimmed),
    queryFn: () => api.get<{ users: UserSearchResult[] }>(`/users/search${query({ q: trimmed })}`),
    select: (data) => data.users,
    enabled: trimmed.length >= 1,
    staleTime: 10_000,
  });
}

export function useProfile(handle: string) {
  return useQuery({
    queryKey: queryKeys.profile(handle),
    queryFn: () => api.get<PublicProfile>(`/users/${encodeURIComponent(handle)}`),
    retry: false,
  });
}

/** Detected game, recent games and tracked playtime of a user (privacy rules applied by the server). */
export function useGameActivity(userId: string | undefined) {
  return useQuery({
    queryKey: queryKeys.activity(userId ?? ''),
    queryFn: () => api.get<GameActivityResponse>(`/users/${encodeURIComponent(userId ?? '')}/activity`),
    enabled: Boolean(userId),
    staleTime: 30_000,
  });
}

export function useConversations() {
  return useQuery({
    queryKey: queryKeys.conversations,
    queryFn: () => api.get<{ conversations: DirectConversationDto[] }>('/conversations'),
    select: (data) => data.conversations,
  });
}

export function useMessages(conversationId: string | null) {
  return useInfiniteQuery({
    queryKey: queryKeys.messages(conversationId ?? 'none'),
    queryFn: ({ pageParam }) =>
      api.get<MessagePage>(`/conversations/${conversationId}/messages${query({ before: pageParam, limit: 40 })}`),
    initialPageParam: undefined as number | undefined,
    // "Next" pages are older messages.
    getNextPageParam: (lastPage) => (lastPage.hasMore ? lastPage.messages[0]?.id : undefined),
    enabled: Boolean(conversationId),
    staleTime: 30_000,
  });
}

export function useGroups() {
  return useQuery({
    queryKey: queryKeys.groups,
    queryFn: () => api.get<{ groups: GroupDto[] }>('/groups'),
    select: (data) => data.groups,
  });
}

export function useDiscoverGroups() {
  return useQuery({
    queryKey: queryKeys.groupsDiscover,
    queryFn: () => api.get<{ groups: GroupDto[] }>('/groups/discover'),
    select: (data) => data.groups,
  });
}

export function useGroup(id: string) {
  return useQuery({
    queryKey: queryKeys.group(id),
    queryFn: () => api.get<GroupDetailDto>(`/groups/${id}`),
    retry: false,
  });
}

export function useCurrentParty() {
  return useQuery({
    queryKey: queryKeys.currentParty,
    queryFn: () => api.get<CurrentPartyResponse>('/parties/current'),
    select: (data) => data.party,
  });
}

/** Whether party voice is set up on this server (rarely changes). */
export function useVoiceStatus() {
  return useQuery({
    queryKey: queryKeys.voiceStatus,
    queryFn: () => api.get<VoiceStatusDto>('/voice/status'),
    staleTime: 5 * 60_000,
  });
}

export function useJoinableParties() {
  return useQuery({
    queryKey: queryKeys.joinableParties,
    queryFn: () => api.get<{ parties: PartyDto[] }>('/parties'),
    select: (data) => data.parties,
  });
}

export interface SessionFilters {
  q?: string;
  status?: string;
  platform?: string;
  region?: string;
  joinable?: boolean;
}

export function useGameSessions(filters: SessionFilters) {
  return useQuery({
    queryKey: queryKeys.gameSessions({ ...filters }),
    queryFn: () => api.get<{ sessions: GameSessionDto[] }>(`/game-sessions${query({ ...filters })}`),
    select: (data) => data.sessions,
    refetchInterval: 30_000,
  });
}

export function useMyGameSessions() {
  return useQuery({
    queryKey: queryKeys.myGameSessions,
    queryFn: () => api.get<{ sessions: GameSessionDto[] }>('/game-sessions/mine'),
    select: (data) => data.sessions,
  });
}

export function useGameSession(id: string) {
  return useQuery({
    queryKey: queryKeys.gameSession(id),
    queryFn: () => api.get<GameSessionDetailDto>(`/game-sessions/${id}`),
    retry: false,
  });
}

export function useInvitations() {
  return useQuery({
    queryKey: queryKeys.invitations,
    queryFn: () => api.get<{ invitations: InvitationDto[] }>('/invitations'),
    select: (data) => data.invitations,
  });
}

export function useNotifications() {
  return useQuery({
    queryKey: queryKeys.notifications,
    queryFn: () => api.get<NotificationPage>('/notifications?limit=30'),
  });
}

export function useAuthSessions() {
  return useQuery({
    queryKey: queryKeys.authSessions,
    queryFn: () => api.get<{ sessions: AuthSessionDto[] }>('/auth/sessions'),
    select: (data) => data.sessions,
  });
}

export function useDevices() {
  return useQuery({
    queryKey: queryKeys.devices,
    queryFn: () => api.get<{ devices: DeviceDto[] }>('/devices'),
    select: (data) => data.devices,
  });
}

export function useAdminInvitations() {
  return useQuery({
    queryKey: queryKeys.admin.invitations,
    queryFn: () => api.get<{ invitations: RegistrationInvitationDto[] }>('/admin/invitations'),
    select: (data) => data.invitations,
  });
}

export function useAdminUsers(q: string) {
  return useQuery({
    queryKey: queryKeys.admin.users(q),
    queryFn: () => api.get<{ users: AdminUserDto[] }>(`/admin/users${query({ q })}`),
    select: (data) => data.users,
  });
}

export function useAuditEvents() {
  return useInfiniteQuery({
    queryKey: queryKeys.admin.audit,
    queryFn: ({ pageParam }) =>
      api.get<{ events: AuditEventDto[]; hasMore: boolean }>(`/admin/audit-events${query({ before: pageParam, limit: 50 })}`),
    initialPageParam: undefined as number | undefined,
    getNextPageParam: (lastPage) => (lastPage.hasMore ? lastPage.events[lastPage.events.length - 1]?.id : undefined),
  });
}

export function useAdminStats() {
  return useQuery({
    queryKey: queryKeys.admin.stats,
    queryFn: () => api.get<AdminStatsDto>('/admin/stats'),
    refetchInterval: 15_000,
  });
}
