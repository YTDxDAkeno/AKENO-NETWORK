import { pgEnum } from 'drizzle-orm/pg-core';
import {
  ACCOUNT_STATUSES,
  FRIEND_REQUEST_PRIVACY,
  GAME_SESSION_JOIN_MODES,
  GAME_SESSION_STATUSES,
  GAME_SESSION_VISIBILITY,
  GROUP_JOIN_POLICIES,
  GROUP_ROLES,
  INVITATION_KINDS,
  MESSAGE_PRIVACY,
  PARTY_PRIVACY,
  PRESENCE_PREFERENCES,
  PRESENCE_VISIBILITY,
  PROFILE_VISIBILITY,
  USER_ROLES,
} from '@akeno/shared';

export const userRoleEnum = pgEnum('user_role', USER_ROLES);
export const accountStatusEnum = pgEnum('account_status', ACCOUNT_STATUSES);
export const profileVisibilityEnum = pgEnum('profile_visibility', PROFILE_VISIBILITY);
export const presenceVisibilityEnum = pgEnum('presence_visibility', PRESENCE_VISIBILITY);
export const messagePrivacyEnum = pgEnum('message_privacy', MESSAGE_PRIVACY);
export const friendRequestPrivacyEnum = pgEnum('friend_request_privacy', FRIEND_REQUEST_PRIVACY);
export const presencePreferenceEnum = pgEnum('presence_preference', PRESENCE_PREFERENCES);
export const friendRequestStatusEnum = pgEnum('friend_request_status', ['pending', 'accepted', 'declined', 'cancelled']);
export const conversationKindEnum = pgEnum('conversation_kind', ['direct', 'group', 'party']);
export const groupJoinPolicyEnum = pgEnum('group_join_policy', GROUP_JOIN_POLICIES);
export const groupRoleEnum = pgEnum('group_role', GROUP_ROLES);
export const partyPrivacyEnum = pgEnum('party_privacy', PARTY_PRIVACY);
export const gameSessionVisibilityEnum = pgEnum('game_session_visibility', GAME_SESSION_VISIBILITY);
export const gameSessionJoinModeEnum = pgEnum('game_session_join_mode', GAME_SESSION_JOIN_MODES);
export const gameSessionStatusEnum = pgEnum('game_session_status', GAME_SESSION_STATUSES);
export const gameSessionRoleEnum = pgEnum('game_session_role', ['host', 'player']);
export const joinRequestStatusEnum = pgEnum('join_request_status', ['pending', 'accepted', 'rejected', 'cancelled']);
export const invitationKindEnum = pgEnum('invitation_kind', INVITATION_KINDS);
export const invitationStatusEnum = pgEnum('invitation_status', ['pending', 'accepted', 'declined', 'cancelled', 'expired']);
