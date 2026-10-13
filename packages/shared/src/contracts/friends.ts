import type { UserSummary, UserWithPresence } from './users.js';

export interface FriendDto extends UserWithPresence {
  friendsSince: string;
  statusMessage: string;
}

export interface FriendRequestDto {
  id: string;
  direction: 'incoming' | 'outgoing';
  user: UserSummary;
  createdAt: string;
}

export interface FriendRequestsResponse {
  incoming: FriendRequestDto[];
  outgoing: FriendRequestDto[];
}

export interface SendFriendRequestResponse {
  /** "pending" for a new request, "accepted" when a reverse request already existed. */
  status: 'pending' | 'accepted';
  request: FriendRequestDto | null;
}

export interface BlockedUserDto {
  user: UserSummary;
  blockedAt: string;
}
