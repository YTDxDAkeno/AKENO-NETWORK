import type { SharePlayQuality } from './quality.js';

/**
 * SharePlay (Alpha 0.5, experimental): a party member streams gameplay to the other members of
 * the same AKENO party, and may give one member "Player 2" controller access. Video and game
 * audio go through the party's LiveKit server in a room separate from voice
 * (akeno-shareplay-<party id>); the AKENO server only authorizes. Nothing is recorded.
 */

/**
 * Where the broadcaster's video comes from.
 * - screen: a window, tab or screen shared by the browser (getDisplayMedia), for example the
 *   official PS Remote Play app's window on a PC.
 * - capture-device: a video capture device seen by the browser as a camera (getUserMedia), for
 *   example an HDMI capture card connected to the PS5.
 * - ps5-native: capture inside the PS5 payload. UNSUPPORTED: no verified capture interface is
 *   available to a payload (docs/SHAREPLAY_STATUS.md). Starting it is always refused.
 */
export const SHAREPLAY_SOURCES = ['screen', 'capture-device', 'ps5-native'] as const;
export type SharePlaySource = (typeof SHAREPLAY_SOURCES)[number];

export interface SharePlayCapability {
  supported: boolean;
  /** English explanation when unsupported or disabled. */
  reason: string | null;
}

/** GET /shareplay/status: what this server offers. */
export interface SharePlayStatusDto {
  /** SharePlay is enabled by the operator and the media server is configured. */
  enabled: boolean;
  reason: string | null;
  maxViewers: number;
  qualities: SharePlayQuality[];
  defaultQuality: SharePlayQuality;
  sources: Record<SharePlaySource, SharePlayCapability>;
  remoteInput: SharePlayCapability & { watchdogMs: number; heartbeatMs: number; maxRateHz: number; protocolVersion: number };
  /**
   * Turning remote input into a real second controller on the PS5. Always unsupported in this
   * version: no verified virtual-controller interface exists for PS5 payloads.
   */
  virtualController: SharePlayCapability;
}

export type SharePlayState = 'starting' | 'live' | 'host-reconnecting';
export type SharePlayRole = 'host' | 'viewer';

export interface SharePlayControllerRequestDto {
  userId: string;
  requestedAt: string;
  expiresAt: string;
}

export interface SharePlayControllerGrantDto {
  grantId: string;
  /** Number the guest's packets must carry; a new grant always has a new epoch. */
  epoch: number;
  guestId: string;
  slot: number;
  grantedAt: string;
  expiresAt: string;
  /** The guest's controller link to the server is open. */
  inputConnected: boolean;
  lastInputAt: string | null;
  packetsAccepted: number;
  packetsRejected: number;
}

export interface SharePlaySessionDto {
  id: string;
  partyId: string;
  hostId: string;
  source: SharePlaySource;
  quality: SharePlayQuality;
  state: SharePlayState;
  startedAt: string;
  liveAt: string | null;
  hostConnected: boolean;
  /** Members currently connected as viewers (as the media server reported). */
  viewerIds: string[];
  maxViewers: number;
  controller: {
    /** Remote input is enabled on this server. */
    enabled: boolean;
    requests: SharePlayControllerRequestDto[];
    grant: SharePlayControllerGrantDto | null;
  };
}

/** GET /parties/:id/shareplay */
export interface PartySharePlayDto {
  available: boolean;
  reason: string | null;
  session: SharePlaySessionDto | null;
  /** What the viewer of this response may do (the server enforces the same rules). */
  you: {
    role: SharePlayRole | null;
    canStart: boolean;
    canStop: boolean;
    canWatch: boolean;
    canRequestController: boolean;
    controllerRequestPending: boolean;
    hasControllerGrant: boolean;
  };
}

/** POST /parties/:id/shareplay/token */
export interface SharePlayTokenResponse {
  url: string;
  token: string;
  room: string;
  identity: string;
  role: SharePlayRole;
  sessionId: string;
  expiresAt: string;
}

/**
 * Forwarded controller state (server → the host's receivers). Sent as the realtime event
 * "shareplay.input" to the host's paired console app, and as JSON text frames on the host's
 * monitor connection.
 */
export interface SharePlayInputRelay {
  sessionId: string;
  grantId: string;
  epoch: number;
  guestId: string;
  slot: number;
  seq: number;
  kind: 'state' | 'release';
  buttons: number;
  axes: [number, number, number, number];
  triggers: [number, number];
  senderTime: number;
  /** Server time (ms since the Unix epoch) the packet was accepted. */
  serverTime: number;
  /** Why a release was sent by the server (grant revoked, guest disconnected, …). */
  reason?: string;
}

/** Close codes of the controller WebSocket (/api/v1/shareplay/input). */
export const SHAREPLAY_INPUT_CLOSE = {
  /** The grant ended (revoked, expired, session ended, membership ended). Do not reconnect. */
  grantEnded: 4410,
  /** The connection broke the protocol (malformed packets, flooding). */
  protocol: 4400,
  /** Replaced by a newer connection of the same user. */
  replaced: 4409,
  /** Not authorized for this role. */
  forbidden: 4403,
} as const;

export const SHAREPLAY_INPUT_PATH = '/api/v1/shareplay/input';
