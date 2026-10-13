import type { InvitationKind } from '../constants.js';
import type { UserSummary } from './users.js';

export interface InvitationDto {
  id: string;
  kind: InvitationKind;
  inviter: UserSummary;
  invitee: UserSummary;
  target: {
    id: string;
    name: string;
  };
  createdAt: string;
  expiresAt: string;
}

export interface AcceptInvitationResponse {
  kind: InvitationKind;
  targetId: string;
}
