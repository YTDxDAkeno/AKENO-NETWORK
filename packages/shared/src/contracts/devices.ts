import type { DeviceScope } from '../constants.js';

export interface DevicePairingStartResponse {
  /** Secret used only by the device to poll the token endpoint. Never display it. */
  deviceCode: string;
  /** Short code the user enters on the website, formatted XXXX-XXXX. */
  userCode: string;
  /** Page where the user signs in and enters the code. */
  verificationUri: string;
  /** verificationUri with the code pre-filled, for QR codes. */
  verificationUriComplete: string;
  expiresIn: number;
  /** Minimum seconds between token polls. */
  interval: number;
}

/** What the signed-in user sees before approving a pairing request. */
export interface DevicePairingPreviewDto {
  deviceName: string;
  platform: string | null;
  clientVersion: string | null;
  requestedScopes: DeviceScope[];
  createdAt: string;
  expiresAt: string;
  requestIp: string | null;
}

export interface DeviceDto {
  id: string;
  name: string;
  platform: string | null;
  clientVersion: string | null;
  scopes: DeviceScope[];
  createdAt: string;
  lastUsedAt: string;
  expiresAt: string;
  ipAddress: string | null;
  /** True when this request was made with this device's token. */
  current: boolean;
}

export interface DeviceTokenResponse {
  /** Opaque bearer token. Store it securely; it is shown only once. */
  accessToken: string;
  tokenType: 'Bearer';
  expiresAt: string;
  device: DeviceDto;
  user: { id: string; onlineId: string; displayName: string };
}

/** Returned to a device about itself, so it can discover what it may do. */
export interface DeviceSelfResponse {
  device: DeviceDto;
  user: { id: string; onlineId: string; displayName: string };
  apiVersion: 1;
  realtimeProtocolVersion: number;
  serverTime: string;
}
