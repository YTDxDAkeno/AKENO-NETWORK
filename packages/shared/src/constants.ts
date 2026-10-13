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
