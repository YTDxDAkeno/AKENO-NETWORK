import type { REALTIME_PROTOCOL_VERSION, SelfPresenceStatus } from '../constants.js';
import type { ConversationKind, MessageDto } from './messages.js';
import type { NotificationDto } from './notifications.js';
import type { PresenceView } from './users.js';

/**
 * Server-to-client realtime events (protocol version 1).
 *
 * Realtime delivery is a latency optimization only. PostgreSQL is authoritative: after a
 * reconnect, clients re-synchronize through the REST API (see docs/API.md, "Recovery").
 */
export type ServerEvent =
  | {
      type: 'hello';
      data: {
        userId: string;
        connectionId: string;
        serverTime: string;
        heartbeatIntervalMs: number;
        protocolVersion: number;
      };
    }
  | { type: 'pong'; data: { serverTime: string } }
  | { type: 'presence.updated'; data: { userId: string; presence: PresenceView } }
  | { type: 'self.presence'; data: { status: SelfPresenceStatus } }
  | {
      type: 'friends.changed';
      data: {
        reason:
          | 'request_received'
          | 'request_cancelled'
          | 'request_declined'
          | 'request_accepted'
          | 'friend_removed'
          | 'blocked'
          | 'unblocked';
        userId: string;
      };
    }
  | { type: 'message.created'; data: { conversationId: string; conversationKind: ConversationKind; message: MessageDto } }
  | { type: 'conversation.read'; data: { conversationId: string; lastReadMessageId: number } }
  | { type: 'typing'; data: { conversationId: string; userId: string } }
  | { type: 'notification.created'; data: { notification: NotificationDto } }
  | { type: 'invitations.changed'; data: Record<string, never> }
  | { type: 'group.changed'; data: { groupId: string; removed: boolean } }
  | { type: 'party.changed'; data: { partyId: string; removed: boolean } }
  | { type: 'game-session.changed'; data: { sessionId: string } }
  | { type: 'profile.changed'; data: { userId: string } }
  | { type: 'session.revoked'; data: { reason: string } };

export type ServerEventType = ServerEvent['type'];

export type ServerEventEnvelope<E extends ServerEvent = ServerEvent> = E & {
  v: typeof REALTIME_PROTOCOL_VERSION;
  ts: string;
};

/** WebSocket close codes used by the server. */
export const REALTIME_CLOSE_CODES = {
  /** The server is restarting; reconnect with backoff. */
  serviceRestart: 1012,
  /** The session was revoked or expired; do not reconnect until re-authenticated. */
  sessionRevoked: 4401,
  /** The connection violated the protocol. */
  protocolError: 4400,
  /** The connection was idle for too long. */
  stale: 4408,
} as const;
