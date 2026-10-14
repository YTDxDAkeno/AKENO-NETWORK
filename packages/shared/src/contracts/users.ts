import type { BadgeId } from '../badges.js';
import type { ProfileThemeId } from '../themes.js';
import type { CurrentGameActivity } from './activity.js';
import type { GameArtwork } from './games.js';
import type {
  AccountStatus,
  FriendRequestPrivacy,
  MessagePrivacy,
  PresencePreference,
  PresenceVisibility,
  ProfileVisibility,
  SelfPresenceStatus,
  UserRole,
  VisiblePresenceStatus,
} from '../constants.js';

/** Minimal public identity of a user. Safe to show to any authenticated, non-blocked viewer. */
export interface UserSummary {
  id: string;
  onlineId: string;
  displayName: string;
  /** Preset avatar ID; always set, used when there is no uploaded picture. */
  avatar: string;
  /** Uploaded profile picture (media ID, see mediaUrl()); null means the preset is shown. */
  avatarImage: string | null;
  /** True for tombstoned (deleted) accounts that still appear as message authors. */
  deleted: boolean;
}

/** Presence as observed by a specific viewer after privacy rules are applied. */
export interface PresenceView {
  status: VisiblePresenceStatus;
  /** ISO timestamp; null when hidden by privacy settings, unknown, or the user is currently visible online. */
  lastSeenAt: string | null;
  /** Self-reported activity text (for example "Playing a racing game"). Never verified by the server. */
  activity: string | null;
  /** Game detected by the user's paired device; null when none or hidden by privacy settings. */
  game: CurrentGameActivity | null;
}

export interface UserWithPresence extends UserSummary {
  presence: PresenceView;
}

export interface PrivacySettings {
  profileVisibility: ProfileVisibility;
  presenceVisibility: PresenceVisibility;
  lastSeenVisibility: PresenceVisibility;
  messagePrivacy: MessagePrivacy;
  friendRequestPrivacy: FriendRequestPrivacy;
  /** Whether paired devices may report the games this user plays. */
  gameActivityTracking: boolean;
  /** Who may see the game this user is playing right now. */
  gameActivityVisibility: PresenceVisibility;
  /** Who may see recently played games and tracked playtime. */
  gameHistoryVisibility: PresenceVisibility;
  /** Whether people who may see the history also see playtime (durations and totals). */
  gamePlaytimeVisible: boolean;
  /** Title IDs that are never tracked; their earlier sessions are hidden from others. */
  gameExcludedTitles: string[];
}

/** Visual customization of a profile. */
export interface ProfileAppearance {
  theme: ProfileThemeId;
  /** Only set for the custom theme (#rrggbb). */
  accentColor: string | null;
  /** Uploaded banner (media ID) or null for the theme's default banner. */
  bannerImage: string | null;
  /** Darken the banner so overlaid elements stay readable. */
  bannerOverlay: boolean;
}

export interface BadgeDto {
  id: BadgeId;
  /** null for derived badges (Administrator). */
  grantedAt: string | null;
}

export interface OwnProfile {
  displayName: string;
  avatar: string;
  avatarImage: string | null;
  bio: string;
  statusMessage: string;
  favoriteGames: string[];
  activity: string | null;
  activityUpdatedAt: string | null;
  appearance: ProfileAppearance;
  /** Up to FEATURED_BADGES_MAX earned badges, in display order. */
  featuredBadges: BadgeId[];
}

/** The authenticated user's own account, including private settings. */
export interface CurrentUser {
  id: string;
  onlineId: string;
  role: UserRole;
  status: AccountStatus;
  createdAt: string;
  profile: OwnProfile;
  privacy: PrivacySettings;
  presencePreference: PresencePreference;
  /** The user's own effective presence as currently computed by the server. */
  presence: SelfPresenceStatus;
  /** Every badge this account has earned. */
  badges: BadgeDto[];
}

export type RelationshipState =
  | 'self'
  | 'none'
  | 'friend'
  | 'request_sent'
  | 'request_received'
  | 'blocked';

export interface Relationship {
  state: RelationshipState;
  /** Pending friend request ID when state is request_sent or request_received. */
  friendRequestId: string | null;
  canMessage: boolean;
  canSendFriendRequest: boolean;
}

export interface PublicProfile {
  user: UserSummary;
  presence: PresenceView;
  joinedAt: string;
  /** True when profile details are hidden by the owner's privacy settings. */
  restricted: boolean;
  bio: string | null;
  statusMessage: string | null;
  favoriteGames: string[];
  /** Favorite games with artwork where the name exactly matches a verified game. */
  favoriteGameDetails: FavoriteGameDto[];
  relationship: Relationship;
  mutualFriendCount: number;
  /** Up to six mutual friends (the viewer already knows them). */
  mutualFriends: UserSummary[];
  /** Theme and banner. The banner is null when profile details are restricted. */
  appearance: ProfileAppearance;
  /** Featured badges first, then the remaining earned badges. */
  badges: BadgeDto[];
  featuredBadges: BadgeId[];
  /** A party of this user that the viewer may see (and may be able to join), else null. */
  party: ProfilePartyDto | null;
}

export interface FavoriteGameDto {
  name: string;
  art: GameArtwork | null;
}

export interface ProfilePartyDto {
  id: string;
  name: string;
  memberCount: number;
  maxMembers: number;
  /** The viewer is a member. */
  isMember: boolean;
}

export interface UserSearchResult extends UserSummary {
  relationship: RelationshipState;
}
