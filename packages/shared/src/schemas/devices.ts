import { z } from 'zod';
import { DEVICE_CODE_PREFIX, DEVICE_LIMITS, DEVICE_SCOPES } from '../constants.js';

const printable = (max: number) =>
  z
    .string()
    .trim()
    .min(1)
    .max(max)
    .regex(/^[\p{L}\p{N} ._()+:/-]+$/u, 'Use letters, digits, spaces and . _ ( ) + : / - only.');

/** Started by an unauthenticated device that wants to be paired with an account. */
export const startDevicePairingSchema = z.object({
  deviceName: printable(DEVICE_LIMITS.nameMax),
  platform: printable(DEVICE_LIMITS.platformMax).optional(),
  clientVersion: printable(DEVICE_LIMITS.clientVersionMax).optional(),
  /** Requested permissions; omitted means every scope. Duplicates are ignored. */
  scopes: z.array(z.enum(DEVICE_SCOPES)).min(1).max(DEVICE_SCOPES.length).optional(),
});
export type StartDevicePairingInput = z.infer<typeof startDevicePairingSchema>;

/** User codes are shown as XXXX-XXXX; spaces, hyphens and case are ignored when entered. */
export const userCodeSchema = z
  .string()
  .transform((value) => value.replace(/[\s-]/g, '').toUpperCase())
  .pipe(z.string().regex(/^[A-HJKMNP-Z2-9]{8}$/, 'Enter the 8-character code shown on your device.'));

export const devicePairingCodeSchema = z.object({ userCode: userCodeSchema });
export type DevicePairingCodeInput = z.infer<typeof devicePairingCodeSchema>;

export const approveDevicePairingSchema = z.object({
  userCode: userCodeSchema,
  /** The scopes the user agrees to grant; must be a subset of the requested scopes. */
  scopes: z.array(z.enum(DEVICE_SCOPES)).min(1).max(DEVICE_SCOPES.length).optional(),
});
export type ApproveDevicePairingInput = z.infer<typeof approveDevicePairingSchema>;

/**
 * Changes the permissions of an already paired device (website only, by its owner). This is how
 * a console paired before a permission existed (e.g. "activity:write") is reauthorized without
 * pairing it again. Nothing is ever granted without this explicit request.
 */
export const updateDeviceScopesSchema = z.object({
  scopes: z.array(z.enum(DEVICE_SCOPES)).min(1).max(DEVICE_SCOPES.length),
});
export type UpdateDeviceScopesInput = z.infer<typeof updateDeviceScopesSchema>;

export const devicePairingTokenSchema = z.object({
  deviceCode: z
    .string()
    .regex(new RegExp(`^${DEVICE_CODE_PREFIX}[A-Za-z0-9_-]{43}$`), 'The device code is malformed.'),
});
export type DevicePairingTokenInput = z.infer<typeof devicePairingTokenSchema>;
