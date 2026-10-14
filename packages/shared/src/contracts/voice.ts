/**
 * Voice parties (Alpha 0.4). Audio is carried by a self-hosted WebRTC SFU (LiveKit); the AKENO
 * server only authorizes: it checks party membership and blocks, issues short-lived join tokens
 * and removes participants whose membership ends. No audio is recorded or stored.
 */

/** Response of POST /parties/:id/voice/token. */
export interface VoiceTokenResponse {
  /** WebSocket URL of the media server (wss://… in production). */
  url: string;
  /** Short-lived JWT for exactly this party's room. */
  token: string;
  room: string;
  /** The participant identity is the AKENO user ID. */
  identity: string;
  expiresAt: string;
}

/** Voice state of a party member as the AKENO server knows it (from the media server). */
export interface PartyMemberVoiceState {
  connected: boolean;
  /** Microphone muted; null when unknown or not connected. */
  muted: boolean | null;
}

export interface VoiceStatusDto {
  /** Voice is configured and enabled on this server. */
  enabled: boolean;
  /** English explanation when voice is unavailable. */
  reason: string | null;
}
