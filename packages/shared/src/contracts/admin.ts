import type { AccountStatus, UserRole } from '../constants.js';
import type { UserSummary } from './users.js';

export type RegistrationInvitationStatus = 'active' | 'expired' | 'exhausted' | 'revoked';

export interface RegistrationInvitationDto {
  id: string;
  /** First characters of the code, for recognition only. The full code is never stored. */
  codeHint: string;
  note: string;
  maxUses: number;
  useCount: number;
  expiresAt: string | null;
  revokedAt: string | null;
  createdAt: string;
  createdBy: UserSummary | null;
  status: RegistrationInvitationStatus;
}

export interface CreatedRegistrationInvitation {
  invitation: RegistrationInvitationDto;
  /** Shown exactly once. Only a hash is stored on the server. */
  code: string;
}

export interface AdminUserDto extends UserSummary {
  role: UserRole;
  status: AccountStatus;
  createdAt: string;
  lastSeenAt: string | null;
  activeSessionCount: number;
  connected: boolean;
}

export interface AuditEventDto {
  id: number;
  action: string;
  actor: UserSummary | null;
  targetType: string | null;
  targetId: string | null;
  metadata: Record<string, unknown>;
  ipAddress: string | null;
  createdAt: string;
}

export interface AdminStatsDto {
  users: { total: number; active: number; suspended: number };
  realtime: { connectedUsers: number; connections: number };
  process: {
    uptimeSeconds: number;
    rssBytes: number;
    heapUsedBytes: number;
    nodeVersion: string;
  };
  database: { totalConnections: number; idleConnections: number; waitingRequests: number };
}
