import type { UserSummary, UserWithPresence } from './users.js';

export type ConversationKind = 'direct' | 'group' | 'party';

export interface MessageDto {
  /** Monotonically increasing per server; doubles as the pagination cursor. */
  id: number;
  conversationId: string;
  sender: UserSummary;
  body: string;
  clientMessageId: string | null;
  createdAt: string;
}

export interface MessagePage {
  /** Always in ascending (chronological) order. */
  messages: MessageDto[];
  hasMore: boolean;
}

export interface DirectConversationDto {
  id: string;
  kind: 'direct';
  otherUser: UserWithPresence;
  lastMessage: MessageDto | null;
  lastMessageAt: string | null;
  unreadCount: number;
  lastReadMessageId: number;
  /** False when blocks or privacy settings currently prevent sending. */
  canSend: boolean;
}

export interface SendMessageResponse {
  message: MessageDto;
  /** False when the request was an idempotent retry of an already stored message. */
  created: boolean;
}
