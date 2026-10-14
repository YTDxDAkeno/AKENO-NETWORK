/**
 * Product-wide constants shared by the server and the web client.
 * Keep validation limits here so both sides enforce identical rules.
 */

export const PRODUCT_NAME = 'AKENO NETWORK';

export const API_PREFIX = '/api/v1';

export const LIMITS = {
  onlineIdMin: 3,
  onlineIdMax: 16,
  displayNameMax: 32,
  passwordMin: 12,
  passwordMax: 128,
  bioMax: 500,
  statusMessageMax: 80,
  activityMax: 64,
  favoriteGamesMax: 6,
  favoriteGameNameMax: 48,
  messageBodyMax: 2000,
  groupNameMin: 3,
  groupNameMax: 48,
  groupDescriptionMax: 500,
  partyNameMax: 48,
  partyMembersMin: 2,
  partyMembersMax: 16,
  partyMembersDefault: 8,
  gameNameMax: 64,
  gameIdentifierMax: 64,
  gameSessionDescriptionMax: 500,
  gameSessionParticipantsMin: 2,
  gameSessionParticipantsMax: 64,
  gameSessionRegionMax: 32,
  gameSessionConnectionInfoMax: 300,
  joinRequestMessageMax: 200,
  invitationNoteMax: 100,
  invitationMaxUses: 100,
  invitationMaxLifetimeHours: 24 * 90,
  searchQueryMax: 32,
  pageSizeMax: 100,
} as const;

/** Online IDs start with a letter and may contain letters, digits, underscores and hyphens. */
export const ONLINE_ID_PATTERN = /^[A-Za-z][A-Za-z0-9_-]{2,15}$/;

/** Online IDs that ordinary registration may not claim (compared case-insensitively). */
export const RESERVED_ONLINE_IDS = [
  'admin',
  'administrator',
  'akeno',
  'akenonetwork',
  'deleted',
  'help',
  'moderator',
  'mod',
  'official',
  'root',
  'security',
  'staff',
  'support',
  'system',
] as const;

/** Presence preferences a user can choose. */
export const PRESENCE_PREFERENCES = ['online', 'away', 'dnd', 'invisible'] as const;
export type PresencePreference = (typeof PRESENCE_PREFERENCES)[number];

/** Presence states other users can observe. "invisible" is never exposed to other users. */
export const VISIBLE_PRESENCE_STATUSES = ['online', 'away', 'dnd', 'offline'] as const;
export type VisiblePresenceStatus = (typeof VISIBLE_PRESENCE_STATUSES)[number];

/** Presence as the owner sees it (may include "invisible"). */
export type SelfPresenceStatus = VisiblePresenceStatus | 'invisible';

export const PRESENCE_LABELS: Record<SelfPresenceStatus, string> = {
  online: 'Online',
  away: 'Away',
  dnd: 'Do Not Disturb',
  invisible: 'Invisible',
  offline: 'Offline',
};

export const PROFILE_VISIBILITY = ['everyone', 'friends'] as const;
export type ProfileVisibility = (typeof PROFILE_VISIBILITY)[number];

export const PRESENCE_VISIBILITY = ['everyone', 'friends', 'nobody'] as const;
export type PresenceVisibility = (typeof PRESENCE_VISIBILITY)[number];

export const MESSAGE_PRIVACY = ['everyone', 'friends'] as const;
export type MessagePrivacy = (typeof MESSAGE_PRIVACY)[number];

export const FRIEND_REQUEST_PRIVACY = ['everyone', 'nobody'] as const;
export type FriendRequestPrivacy = (typeof FRIEND_REQUEST_PRIVACY)[number];

export const DEFAULT_PRIVACY = {
  profileVisibility: 'everyone',
  presenceVisibility: 'friends',
  lastSeenVisibility: 'friends',
  messagePrivacy: 'friends',
  friendRequestPrivacy: 'everyone',
} as const satisfies {
  profileVisibility: ProfileVisibility;
  presenceVisibility: PresenceVisibility;
  lastSeenVisibility: PresenceVisibility;
  messagePrivacy: MessagePrivacy;
  friendRequestPrivacy: FriendRequestPrivacy;
};

export const USER_ROLES = ['user', 'admin'] as const;
export type UserRole = (typeof USER_ROLES)[number];

export const ACCOUNT_STATUSES = ['active', 'suspended', 'deleted'] as const;
export type AccountStatus = (typeof ACCOUNT_STATUSES)[number];

export const GROUP_ROLES = ['owner', 'moderator', 'member'] as const;
export type GroupRole = (typeof GROUP_ROLES)[number];

export const GROUP_JOIN_POLICIES = ['invite_only', 'open'] as const;
export type GroupJoinPolicy = (typeof GROUP_JOIN_POLICIES)[number];

/** invite_only: only invited users may join. friends: friends of the leader may join directly. */
export const PARTY_PRIVACY = ['invite_only', 'friends'] as const;
export type PartyPrivacy = (typeof PARTY_PRIVACY)[number];

/** public: listed for everyone. friends: listed for the host's friends. private: invitation only. */
export const GAME_SESSION_VISIBILITY = ['public', 'friends', 'private'] as const;
export type GameSessionVisibility = (typeof GAME_SESSION_VISIBILITY)[number];

/** open: eligible users join instantly. approval: users request to join and the host decides. */
export const GAME_SESSION_JOIN_MODES = ['open', 'approval'] as const;
export type GameSessionJoinMode = (typeof GAME_SESSION_JOIN_MODES)[number];

export const GAME_SESSION_STATUSES = ['waiting', 'active', 'finished'] as const;
export type GameSessionStatus = (typeof GAME_SESSION_STATUSES)[number];

export const GAME_SESSION_STATUS_LABELS: Record<GameSessionStatus, string> = {
  waiting: 'Waiting for players',
  active: 'In progress',
  finished: 'Finished',
};

/**
 * Self-declared platform labels for game sessions. These are informational labels only:
 * AKENO NETWORK does not connect to or verify any console or platform service.
 */
export const GAME_PLATFORMS = [
  'PC',
  'PlayStation 5',
  'PlayStation 4',
  'Xbox Series X|S',
  'Nintendo Switch',
  'Mobile',
  'Cross-platform',
  'Other',
] as const;
export type GamePlatform = (typeof GAME_PLATFORMS)[number];

export const SUGGESTED_REGIONS = [
  'Europe West',
  'Europe Central',
  'Europe North',
  'North America East',
  'North America West',
  'South America',
  'Asia',
  'Oceania',
  'Middle East',
  'Africa',
] as const;

export const INVITATION_KINDS = ['group', 'party', 'game_session'] as const;
export type InvitationKind = (typeof INVITATION_KINDS)[number];

export const INVITATION_LIFETIME_HOURS: Record<InvitationKind, number> = {
  group: 24 * 14,
  party: 12,
  game_session: 24,
};

/**
 * Curated, original preset avatars. Each preset is rendered by the client as a gradient
 * badge with an icon; no user-supplied images are stored by the server.
 */
export const AVATAR_PRESETS = [
  { id: 'nova', label: 'Nova', from: '#4f7cff', to: '#8b5cf6', icon: 'sparkles' },
  { id: 'pulse', label: 'Pulse', from: '#06b6d4', to: '#3b82f6', icon: 'zap' },
  { id: 'ember', label: 'Ember', from: '#f97316', to: '#e11d48', icon: 'flame' },
  { id: 'phantom', label: 'Phantom', from: '#6366f1', to: '#1e1b4b', icon: 'ghost' },
  { id: 'orbit', label: 'Orbit', from: '#14b8a6', to: '#0f766e', icon: 'orbit' },
  { id: 'crown', label: 'Crown', from: '#eab308', to: '#b45309', icon: 'crown' },
  { id: 'blade', label: 'Blade', from: '#94a3b8', to: '#334155', icon: 'sword' },
  { id: 'rocket', label: 'Rocket', from: '#ec4899', to: '#7c3aed', icon: 'rocket' },
  { id: 'moon', label: 'Moon', from: '#818cf8', to: '#312e81', icon: 'moon' },
  { id: 'circuit', label: 'Circuit', from: '#22c55e', to: '#15803d', icon: 'cpu' },
  { id: 'skull', label: 'Skull', from: '#a855f7', to: '#581c87', icon: 'skull' },
  { id: 'target', label: 'Target', from: '#ef4444', to: '#7f1d1d', icon: 'crosshair' },
  { id: 'shield', label: 'Shield', from: '#0ea5e9', to: '#1e3a8a', icon: 'shield' },
  { id: 'star', label: 'Star', from: '#facc15', to: '#ea580c', icon: 'star' },
  { id: 'gamepad', label: 'Controller', from: '#3b82f6', to: '#1e40af', icon: 'gamepad' },
  { id: 'bot', label: 'Bot', from: '#64748b', to: '#0f172a', icon: 'bot' },
] as const;
export type AvatarPreset = (typeof AVATAR_PRESETS)[number];
export type AvatarId = AvatarPreset['id'];
export const AVATAR_IDS = AVATAR_PRESETS.map((preset) => preset.id) as [AvatarId, ...AvatarId[]];
export const DEFAULT_AVATAR: AvatarId = 'nova';

/** Realtime protocol version carried in every WebSocket envelope. */
export const REALTIME_PROTOCOL_VERSION = 1;
export const REALTIME_PATH = '/ws';
/** Client should send an application-level ping at this interval. */
export const REALTIME_HEARTBEAT_INTERVAL_MS = 25_000;
/** Server terminates connections that have been silent for longer than this. */
export const REALTIME_STALE_TIMEOUT_MS = 70_000;

/** Header used to carry the per-session CSRF token on state-changing requests. */
export const CSRF_HEADER = 'x-csrf-token';

/**
 * Permissions a paired device (for example a console homebrew client) may request.
 * A device token can only call the API routes mapped to the scopes the user granted; account,
 * security, privacy, device-management and administration routes are never available to devices.
 */
export const DEVICE_SCOPES = [
  'profile:read',
  'presence:write',
  'friends:read',
  'friends:write',
  'messages:read',
  'messages:write',
  'parties:read',
  'parties:write',
  'sessions:read',
  'sessions:write',
  'invitations:read',
  'invitations:write',
  'realtime',
  'activity:write',
] as const;
export type DeviceScope = (typeof DEVICE_SCOPES)[number];

export const DEVICE_SCOPE_LABELS: Record<DeviceScope, string> = {
  'profile:read': 'See your profile and search for members',
  'presence:write': 'Change your online status',
  'friends:read': 'See your friends and friend requests',
  'friends:write': 'Send, accept and decline friend requests',
  'messages:read': 'Read your direct, group and party messages',
  'messages:write': 'Send messages as you',
  'parties:read': 'See parties',
  'parties:write': 'Create, join and leave parties and invite friends',
  'sessions:read': 'See game sessions',
  'sessions:write': 'Join, leave and host game sessions and invite friends',
  'invitations:read': 'See your invitations and notifications',
  'invitations:write': 'Accept and decline invitations',
  'realtime': 'Receive live updates and appear online while connected',
  'activity:write': 'Share the game you are playing, detected by this device',
};

/**
 * Automatic game activity reported by paired devices (see docs/GAME_ACTIVITY.md).
 * Timestamps are always the server's; devices only report what is running now.
 */
export const GAME_ACTIVITY = {
  /** Devices send a heartbeat this often while a game is running. */
  heartbeatIntervalSeconds: 60,
  /** A session without a heartbeat for this long ends at its last heartbeat. */
  staleAfterSeconds: 180,
  /** PlayStation title IDs: four letters and five digits (e.g. PPSA01234, CUSA01234). */
  titleIdPattern: /^[A-Z]{4}[0-9]{5}$/,
  titleNameMax: 128,
  recentGamesMax: 10,
  recentSessionsMax: 10,
} as const;

/** Console platforms a device may report activity from. */
export const GAME_ACTIVITY_PLATFORMS = ['PS5'] as const;
export type GameActivityPlatform = (typeof GAME_ACTIVITY_PLATFORMS)[number];

/** Why a tracked session ended. */
export const GAME_SESSION_END_REASONS = ['stopped', 'switched', 'stale', 'tracking_disabled', 'device_revoked'] as const;
export type GameSessionEndReason = (typeof GAME_SESSION_END_REASONS)[number];

export const DEVICE_LIMITS = {
  nameMax: 48,
  platformMax: 48,
  clientVersionMax: 32,
  /** Pairing requests expire after this many seconds. */
  pairingTtlSeconds: 600,
  /** Minimum polling interval for the token endpoint. */
  pairingPollIntervalSeconds: 5,
  /** Paired devices per user; the least recently used device is unpaired beyond this. */
  maxPerUser: 10,
} as const;

/** Prefix of opaque device access tokens ("akd_" + 43 base64url characters). */
export const DEVICE_TOKEN_PREFIX = 'akd_';
/** Prefix of the secret device code used only to poll for pairing completion. */
export const DEVICE_CODE_PREFIX = 'akp_';
