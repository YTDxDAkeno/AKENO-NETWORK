import type {
  GamePlatform,
  GameSessionJoinMode,
  GameSessionStatus,
  GameSessionVisibility,
} from '../constants.js';
import type { GameArtwork } from './games.js';
import type { UserSummary, UserWithPresence } from './users.js';

export type GameSessionViewerState = 'host' | 'member' | 'requested' | 'invited' | 'none';

export interface GameSessionDto {
  id: string;
  gameName: string;
  gameIdentifier: string | null;
  /** Artwork when the identifier is a mapped title ID or the name exactly matches a verified game. */
  art: GameArtwork | null;
  description: string;
  host: UserSummary;
  visibility: GameSessionVisibility;
  joinMode: GameSessionJoinMode;
  maxParticipants: number;
  participantCount: number;
  status: GameSessionStatus;
  region: string | null;
  platform: GamePlatform | null;
  createdAt: string;
  updatedAt: string;
  startedAt: string | null;
  endedAt: string | null;
  viewerState: GameSessionViewerState;
  /** Externally supplied connection details, only revealed to participants. */
  connectionInfo: string | null;
}

export interface GameSessionMemberDto {
  user: UserWithPresence;
  role: 'host' | 'player';
  joinedAt: string;
}

export interface GameSessionJoinRequestDto {
  id: string;
  user: UserSummary;
  message: string;
  createdAt: string;
}

export interface GameSessionDetailDto extends GameSessionDto {
  members: GameSessionMemberDto[];
  /** Only populated for the host. */
  joinRequests: GameSessionJoinRequestDto[];
  /** Pending invitation ID for the viewer, if any. */
  pendingInvitationId: string | null;
}

export interface JoinGameSessionResponse {
  outcome: 'joined' | 'requested' | 'already_member';
  session: GameSessionDetailDto;
}
