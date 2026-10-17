/**
 * AKENO LAN Play (experimental) control-plane contracts.
 *
 * LAN Play lets members of an AKENO party set up an authenticated "virtual LAN" session so that
 * genuinely LAN-capable games can be played together over the Internet. This file describes only
 * the **control plane**: who may create a session, who is in it, the roles, and the short-lived
 * credentials a transport would use. It deliberately contains no packet transport: the data plane
 * (capturing and forwarding a game's LAN traffic) is provided by a separate, pluggable transport
 * adapter. No verified transport is wired in this build, so LAN Play reports
 * "Transport not available" and never claims a connection it cannot make.
 *
 * LAN Play is NOT SharePlay. SharePlay streams one device's screen to watchers and relays one
 * controller; LAN Play would carry two separate game instances' network traffic. They share the
 * party UI and authorization but nothing else, and are enabled independently.
 *
 * See docs/LAN_PLAY_ARCHITECTURE.md, docs/LAN_PLAY_FEASIBILITY.md and docs/LAN_PLAY_SECURITY.md.
 */

/** Platforms a participant's game device may run on. `ps4` is reserved for future support. */
export const LAN_PLAY_PLATFORMS = ['pc', 'ps5', 'ps4'] as const;
export type LanPlayPlatform = (typeof LAN_PLAY_PLATFORMS)[number];

/** A participant either hosts the actual game session or joins it. */
export type LanPlayRole = 'host' | 'player';

/**
 * How a participant's device is expected to reach the tunnel. This is a statement of the planned
 * topology, not proof it works. `ps5` consoles cannot run a tunnel inside a normal payload, so a
 * nearby companion bridge is required; a PC can run the bridge itself.
 */
export const LAN_PLAY_CONNECTIONS = ['direct', 'bridge', 'unavailable'] as const;
export type LanPlayConnectionMode = (typeof LAN_PLAY_CONNECTIONS)[number];

/**
 * Honest compatibility labels (see the master spec). They describe evidence, never marketing.
 * A pair is only "hardware-tested" when two real devices of that exact build connected and
 * exchanged gameplay.
 */
export const LAN_PLAY_EVIDENCE = [
  'hardware-tested',
  'tested-build-pair',
  'community-reported',
  'lan-documented',
  'not-verified',
  'incompatible',
  'online-only',
  'requires-bridge',
  'blocked',
] as const;
export type LanPlayEvidence = (typeof LAN_PLAY_EVIDENCE)[number];

export interface LanPlayCapability {
  supported: boolean;
  /** English explanation, always present when unsupported or unverified. */
  reason: string | null;
  evidence: LanPlayEvidence;
}

/** A platform pair and what is honestly known about running LAN Play across it. */
export interface LanPlayPlatformPair {
  a: LanPlayPlatform;
  b: LanPlayPlatform;
  capability: LanPlayCapability;
}

/** State of the transport adapter the server is configured with. */
export interface LanPlayTransportStatus {
  /** Adapter identifier, e.g. "none" for the built-in unavailable adapter. */
  id: string;
  /** Human-readable adapter name. */
  label: string;
  /** The adapter can currently authorize real connections. Always false until one is verified. */
  available: boolean;
  reason: string | null;
}

/** GET /lanplay/status: what this server offers, independent of any party. */
export interface LanPlayStatusDto {
  /** The operator enabled LAN Play. */
  enabled: boolean;
  reason: string | null;
  transport: LanPlayTransportStatus;
  maxParticipants: number;
  sessionTtlSeconds: number;
  credentialTtlSeconds: number;
  platforms: LanPlayPlatformPair[];
}

export type LanPlaySessionState = 'open' | 'closed';

export interface LanPlayParticipantDto {
  userId: string;
  role: LanPlayRole;
  platform: LanPlayPlatform;
  joinedAt: string;
  /** The participant holds a current connection credential and the transport reported it linked. */
  connected: boolean;
  connectionMode: LanPlayConnectionMode;
  lastConnectedAt: string | null;
}

export interface LanPlaySessionDto {
  id: string;
  partyId: string;
  hostId: string;
  state: LanPlaySessionState;
  /** Selected game from the compatibility database, if any. */
  gameId: string | null;
  createdAt: string;
  expiresAt: string;
  maxParticipants: number;
  participants: LanPlayParticipantDto[];
  /** Live transport status for this session (never "ready" until a verified transport exists). */
  transport: LanPlayTransportStatus;
}

/** GET /parties/:id/lanplay */
export interface PartyLanPlayDto {
  available: boolean;
  reason: string | null;
  session: LanPlaySessionDto | null;
  /** What the viewer of this response may do; the server enforces the same rules. */
  you: {
    role: LanPlayRole | null;
    canCreate: boolean;
    canJoin: boolean;
    canTerminate: boolean;
    inSession: boolean;
    connected: boolean;
  };
}

/**
 * POST /parties/:id/lanplay/connect — a participant asks the transport for a connection.
 * Until a verified transport exists this always fails with LAN_TRANSPORT_UNAVAILABLE; the shape
 * is defined so a future adapter can return a real descriptor without changing the control plane.
 */
export interface LanPlayConnectionDto {
  sessionId: string;
  /** Opaque, single-session credential the participant's transport would present. Shown once. */
  credential: string;
  expiresAt: string;
  connectionMode: LanPlayConnectionMode;
  transport: {
    id: string;
    label: string;
    /** The descriptor can actually carry traffic. Always false for the unavailable adapter. */
    connectable: boolean;
    /** Adapter-specific, non-secret connection hints (endpoints, relay, etc.). */
    details: Record<string, string>;
  };
}

/** A game in the LAN Play compatibility database. */
export interface LanPlayGameEntry {
  id: string;
  title: string;
  platform: LanPlayPlatform;
  /** PS5-native vs the PS4 build running through backward compatibility, when it matters. */
  edition: string | null;
  /** Whether an offline LAN / system-link mode exists at all. */
  offlineLan: boolean;
  lanModeType: string | null;
  maxPlayers: number | null;
  /** Whether the title's LAN mode needs account/online authentication to reach. */
  authRequired: boolean;
  crossPlatform: LanPlayEvidence;
  evidence: LanPlayEvidence;
  notes: string;
  sources: { url: string; verifiedAt: string }[];
}
