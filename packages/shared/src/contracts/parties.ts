import type { PartyPrivacy } from '../constants.js';
import type { UserWithPresence } from './users.js';

export interface PartyMemberDto {
  user: UserWithPresence;
  joinedAt: string;
  isLeader: boolean;
}

export interface PartyVoiceStatus {
  /** Voice chat is not part of the stable V1 release. */
  available: false;
  reason: string;
}

export interface PartyDto {
  id: string;
  name: string;
  privacy: PartyPrivacy;
  leaderId: string;
  maxMembers: number;
  memberCount: number;
  createdAt: string;
  isMember: boolean;
  /** Present only for members. */
  conversationId: string | null;
  members: PartyMemberDto[];
  voice: PartyVoiceStatus;
}

export interface CurrentPartyResponse {
  party: PartyDto | null;
}
