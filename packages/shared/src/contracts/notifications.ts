import type { InvitationKind } from '../constants.js';
import type { UserSummary } from './users.js';

export type NotificationData =
  | { type: 'friend_request.received'; actor: UserSummary; requestId: string }
  | { type: 'friend_request.accepted'; actor: UserSummary }
  | { type: 'invitation.received'; actor: UserSummary; invitationId: string; kind: InvitationKind; targetId: string; targetName: string }
  | { type: 'game_session.join_requested'; actor: UserSummary; sessionId: string; gameName: string }
  | { type: 'game_session.request_accepted'; actor: UserSummary; sessionId: string; gameName: string }
  | { type: 'game_session.request_rejected'; actor: UserSummary; sessionId: string; gameName: string }
  | { type: 'group.removed'; actor: UserSummary; groupId: string; groupName: string }
  | { type: 'group.role_changed'; actor: UserSummary; groupId: string; groupName: string; role: string };

export type NotificationType = NotificationData['type'];

export interface NotificationDto {
  id: number;
  data: NotificationData;
  createdAt: string;
  readAt: string | null;
}

export interface NotificationPage {
  notifications: NotificationDto[];
  unreadCount: number;
  hasMore: boolean;
}
