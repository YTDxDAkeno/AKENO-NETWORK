import type { GroupJoinPolicy, GroupRole } from '../constants.js';
import type { UserSummary, UserWithPresence } from './users.js';

export interface GroupDto {
  id: string;
  name: string;
  description: string;
  joinPolicy: GroupJoinPolicy;
  owner: UserSummary;
  memberCount: number;
  myRole: GroupRole | null;
  /** Present only for members. */
  conversationId: string | null;
  unreadCount: number;
  createdAt: string;
  updatedAt: string;
}

export interface GroupMemberDto {
  user: UserWithPresence;
  role: GroupRole;
  joinedAt: string;
}

export interface GroupDetailDto extends GroupDto {
  members: GroupMemberDto[];
  /** Pending outgoing invitations, visible to owners and moderators. */
  pendingInvitations: Array<{ id: string; invitee: UserSummary; inviter: UserSummary; createdAt: string }>;
  hasPendingInvitation: boolean;
}
