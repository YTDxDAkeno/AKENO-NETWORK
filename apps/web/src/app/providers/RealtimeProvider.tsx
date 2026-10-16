import { type QueryClient, useQueryClient } from '@tanstack/react-query';
import { createContext, useCallback, useContext, useEffect, useRef, useState, type ReactNode } from 'react';
import {
  REALTIME_PROTOCOL_VERSION,
  type ClientRealtimeMessage,
  type ConversationKind,
  type DirectConversationDto,
  type FriendDto,
  type PresenceView,
  type PublicProfile,
  type ServerEventEnvelope,
} from '@akeno/shared';
import { useToast } from '../../components/Toast';
import { useAuth } from '../../features/auth/AuthProvider';
import { SHAREPLAY_EVENT, sharePlayKeys } from '../../features/shareplay/queries';
import { useIdleDetection } from '../../hooks/useIdleDetection';
import { applyRealtimeMessage, catchUpMessages } from '../../lib/message-sync';
import { describeNotification } from '../../lib/notifications';
import { queryKeys } from '../../lib/query-keys';
import { RealtimeClient, type RealtimeStatus } from '../../lib/realtime';
import { typingStore } from '../../lib/typing-store';

interface RealtimeContextValue {
  status: RealtimeStatus;
  send: (message: ClientRealtimeMessage) => boolean;
}

const RealtimeContext = createContext<RealtimeContextValue>({ status: 'idle', send: () => false });

function patchPresence(queryClient: QueryClient, userId: string, presence: PresenceView): void {
  queryClient.setQueryData<{ friends: FriendDto[] }>(queryKeys.friends, (data) =>
    data ? { friends: data.friends.map((friend) => (friend.id === userId ? { ...friend, presence } : friend)) } : data,
  );
  queryClient.setQueryData<{ conversations: DirectConversationDto[] }>(queryKeys.conversations, (data) =>
    data
      ? {
          conversations: data.conversations.map((conversation) =>
            conversation.otherUser.id === userId ? { ...conversation, otherUser: { ...conversation.otherUser, presence } } : conversation,
          ),
        }
      : data,
  );
  queryClient.setQueriesData<PublicProfile>({ queryKey: queryKeys.profiles }, (data) =>
    data && data.user.id === userId ? { ...data, presence } : data,
  );
  void queryClient.invalidateQueries({ queryKey: queryKeys.activity(userId), refetchType: 'active' });
  void queryClient.invalidateQueries({ queryKey: queryKeys.allParties, refetchType: 'active' });
}

/**
 * After a reconnect, missed messages are fetched with the `after` cursor for every cached
 * conversation, then all other server state is refetched from PostgreSQL via the REST API.
 */
async function resynchronize(queryClient: QueryClient): Promise<void> {
  await catchUpMessages(queryClient);
  await queryClient.invalidateQueries({ predicate: (query) => query.queryKey[0] !== 'messages' });
}

export function RealtimeProvider({ children }: { children: ReactNode }) {
  const queryClient = useQueryClient();
  const auth = useAuth();
  const toast = useToast();
  const [status, setStatus] = useState<RealtimeStatus>('idle');
  const clientRef = useRef<RealtimeClient | null>(null);
  const userId = auth.user?.id ?? null;

  const authRef = useRef(auth);
  const toastRef = useRef(toast);
  useEffect(() => {
    authRef.current = auth;
    toastRef.current = toast;
  });

  useEffect(() => {
    if (!userId) return;
    const client = new RealtimeClient();
    clientRef.current = client;

    // Party events come in bursts (a voice join is reported by the voice server and the party
    // service; members joining and leaving in quick succession): refetch once per burst.
    const pending = new Map<string, readonly unknown[]>();
    let flushTimer: ReturnType<typeof setTimeout> | null = null;
    const invalidateSoon = (queryKey: readonly unknown[]) => {
      pending.set(JSON.stringify(queryKey), queryKey);
      flushTimer ??= setTimeout(() => {
        flushTimer = null;
        for (const key of pending.values()) void queryClient.invalidateQueries({ queryKey: key });
        pending.clear();
      }, 150);
    };

    const handle = (event: ServerEventEnvelope, meta: { reconnected: boolean }) => {
      const invalidate = (queryKey: readonly unknown[]) => void queryClient.invalidateQueries({ queryKey });
      switch (event.type) {
        case 'hello':
          // Even the first connection can miss messages stored between the page's initial
          // fetches and the socket opening; everything else is refetched only after reconnects.
          void (meta.reconnected ? resynchronize(queryClient) : catchUpMessages(queryClient));
          break;
        case 'presence.updated':
          patchPresence(queryClient, event.data.userId, event.data.presence);
          break;
        case 'self.presence':
          authRef.current.patchUser({ presence: event.data.status });
          break;
        case 'activity.self': {
          const self = authRef.current.user;
          if (self) {
            queryClient.setQueriesData<PublicProfile>({ queryKey: queryKeys.profiles }, (data) =>
              data && data.user.id === self.id ? { ...data, presence: { ...data.presence, game: event.data.game } } : data,
            );
            invalidate(queryKeys.activity(self.id));
          }
          break;
        }
        case 'friends.changed':
          for (const key of [queryKeys.friends, queryKeys.friendRequests, queryKeys.blocks, queryKeys.profiles, queryKeys.conversations, ['search']]) {
            invalidate(key);
          }
          break;
        case 'message.created': {
          const { conversationId, message } = event.data;
          applyRealtimeMessage(queryClient, conversationId, message);
          typingStore.remove(conversationId, message.sender.id);
          const kind: ConversationKind = event.data.conversationKind;
          if (kind === 'direct') invalidate(queryKeys.conversations);
          if (kind === 'group') invalidate(queryKeys.groups);
          break;
        }
        case 'conversation.read':
          invalidate(queryKeys.conversations);
          invalidate(queryKeys.groups);
          break;
        case 'typing':
          typingStore.add(event.data.conversationId, event.data.userId);
          break;
        case 'notification.created':
          invalidate(queryKeys.notifications);
          toastRef.current.info(describeNotification(event.data.notification.data).text);
          break;
        case 'invitations.changed':
          for (const key of [queryKeys.invitations, queryKeys.joinableParties, queryKeys.allGameSessions, queryKeys.allGameSessionDetails, queryKeys.allGroups]) {
            invalidate(key);
          }
          break;
        case 'group.changed':
          invalidate(queryKeys.groups);
          invalidate(queryKeys.groupsDiscover);
          invalidate(queryKeys.group(event.data.groupId));
          break;
        case 'party.changed':
          invalidateSoon(queryKeys.currentParty);
          invalidateSoon(queryKeys.joinableParties);
          invalidateSoon(queryKeys.party(event.data.partyId));
          break;
        case 'shareplay.changed':
          invalidateSoon(sharePlayKeys.party(event.data.partyId));
          window.dispatchEvent(new CustomEvent(SHAREPLAY_EVENT, { detail: event.data }));
          break;
        case 'shareplay.input':
          // Only paired console apps receive Player 2 input; browsers use the monitor link.
          break;
        case 'game-session.changed':
          invalidate(queryKeys.allGameSessions);
          invalidate(queryKeys.gameSession(event.data.sessionId));
          break;
        case 'profile.changed':
          invalidate(queryKeys.friends);
          invalidate(queryKeys.profiles);
          invalidate(queryKeys.conversations);
          if (event.data.userId === authRef.current.user?.id) void authRef.current.refreshUser().catch(() => {});
          break;
        case 'session.revoked':
          authRef.current.signedOut(event.data.reason);
          break;
        case 'pong':
          break;
      }
    };

    const offEvent = client.onEvent(handle);
    const offStatus = client.onStatus(setStatus);
    const onOnline = () => client.reconnectNow();
    const onVisible = () => {
      if (document.visibilityState === 'visible') client.reconnectNow();
    };
    window.addEventListener('online', onOnline);
    document.addEventListener('visibilitychange', onVisible);
    client.start();

    return () => {
      if (flushTimer) clearTimeout(flushTimer);
      offEvent();
      offStatus();
      window.removeEventListener('online', onOnline);
      document.removeEventListener('visibilitychange', onVisible);
      client.stop();
      clientRef.current = null;
      setStatus('idle');
    };
  }, [userId, queryClient]);

  const send = useCallback((message: ClientRealtimeMessage) => clientRef.current?.send(message) ?? false, []);

  useIdleDetection(
    (idle) => send({ v: REALTIME_PROTOCOL_VERSION, type: 'presence.idle', data: { idle } }),
    status === 'open',
  );

  return <RealtimeContext.Provider value={{ status, send }}>{children}</RealtimeContext.Provider>;
}

export function useRealtime(): RealtimeContextValue {
  return useContext(RealtimeContext);
}
