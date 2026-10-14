import type { GameActivityPlatform, GameSessionEndReason } from '../constants.js';

/**
 * A game a user is playing right now, as detected by one of their paired devices.
 * "Detected" means the device saw the game running; it is not verified by any platform service.
 */
export interface CurrentGameActivity {
  titleId: string;
  /** Curated name, else the name the device read from the console's own metadata; null if unknown. */
  name: string | null;
  platform: GameActivityPlatform;
  /** Server time when this session started. */
  startedAt: string;
}

/** One game in a user's tracked history, aggregated over all tracked sessions. */
export interface GameHistoryEntry {
  titleId: string;
  name: string | null;
  platform: GameActivityPlatform;
  /** End of the most recent session (or its last heartbeat while it is still running). */
  lastPlayedAt: string;
  lastSessionSeconds: number;
  /** Total tracked playtime; time before AKENO tracked this user is unknown and not included. */
  totalSeconds: number;
  sessionCount: number;
  playingNow: boolean;
}

export interface GameSessionEntry {
  id: string;
  titleId: string;
  name: string | null;
  platform: GameActivityPlatform;
  startedAt: string;
  /** null while the session is running. */
  endedAt: string | null;
  durationSeconds: number;
  endReason: GameSessionEndReason | null;
}

export interface GameActivityResponse {
  /** null when nothing is running or the viewer may not see it. */
  current: CurrentGameActivity | null;
  /** False when the owner's privacy settings hide their history from this viewer. */
  historyVisible: boolean;
  recentGames: GameHistoryEntry[];
  recentSessions: GameSessionEntry[];
  /** First tracked session; playtime before this is not known to AKENO. */
  trackedSince: string | null;
  /** Only for the owner: whether automatic tracking is on. */
  trackingEnabled?: boolean;
}

/** Returned to a device after a heartbeat. */
export interface GameHeartbeatResponse {
  session: { id: string; titleId: string; startedAt: string; created: boolean };
  heartbeatIntervalSeconds: number;
  staleAfterSeconds: number;
}

export interface GameTitleDto {
  titleId: string;
  name: string;
  updatedAt: string;
}
