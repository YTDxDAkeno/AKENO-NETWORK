import type { PartyPrivacy } from '../constants.js';
import type { UserWithPresence } from './users.js';
import type { PartyMemberVoiceState } from './voice.js';

export interface PartyMemberDto {
  user: UserWithPresence;
  joinedAt: string;
  isLeader: boolean;
  /** Voice connection of this member (only reported to party members). */
  voice: PartyMemberVoiceState;
}

export interface PartyVoiceStatus {
  /** Voice chat is configured on this server and the viewer may use it in this party. */
  available: boolean;
  /** English explanation when voice is unavailable; null when available. */
  reason: string | null;
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
