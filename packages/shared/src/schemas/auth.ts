import { z } from 'zod';
import { LIMITS, ONLINE_ID_PATTERN } from '../constants.js';
import { singleLineText } from './common.js';

export const onlineIdSchema = z
  .string()
  .trim()
  .min(LIMITS.onlineIdMin, `Online ID must be at least ${LIMITS.onlineIdMin} characters.`)
  .max(LIMITS.onlineIdMax, `Online ID must be at most ${LIMITS.onlineIdMax} characters.`)
  .regex(
    ONLINE_ID_PATTERN,
    'Online ID must start with a letter and contain only letters, numbers, underscores or hyphens.',
  );

export const passwordSchema = z
  .string()
  .min(LIMITS.passwordMin, `Password must be at least ${LIMITS.passwordMin} characters.`)
  .max(LIMITS.passwordMax, `Password must be at most ${LIMITS.passwordMax} characters.`);

/** Invitation codes are displayed as XXXX-XXXX-XXXX-XXXX; normalization removes separators and case. */
export const invitationCodeSchema = z
  .string()
  .trim()
  .max(64, 'Invitation code is too long.')
  .transform((value) => value.toUpperCase().replace(/[^A-Z0-9]/g, ''));

export const registerSchema = z.object({
  onlineId: onlineIdSchema,
  password: passwordSchema,
  displayName: singleLineText(LIMITS.displayNameMax).optional(),
  invitationCode: invitationCodeSchema.optional(),
});
export type RegisterInput = z.input<typeof registerSchema>;

export const loginSchema = z.object({
  onlineId: z.string().trim().min(1, 'Online ID is required.').max(64),
  password: z.string().min(1, 'Password is required.').max(LIMITS.passwordMax),
});
export type LoginInput = z.infer<typeof loginSchema>;

export const changePasswordSchema = z
  .object({
    currentPassword: z.string().min(1, 'Current password is required.').max(LIMITS.passwordMax),
    newPassword: passwordSchema,
  })
  .refine((value) => value.currentPassword !== value.newPassword, {
    message: 'The new password must be different from the current password.',
    path: ['newPassword'],
  });
export type ChangePasswordInput = z.infer<typeof changePasswordSchema>;

export const deleteAccountSchema = z.object({
  password: z.string().min(1, 'Password is required.').max(LIMITS.passwordMax),
  confirmation: z.literal('DELETE', { error: 'Type DELETE to confirm.' }),
});
export type DeleteAccountInput = z.infer<typeof deleteAccountSchema>;

/**
 * Basic password quality checks that complement the length rule.
 * Returns an English error message or null when the password is acceptable.
 */
export function checkPasswordQuality(password: string, onlineId?: string): string | null {
  const lower = password.toLowerCase();
  if (onlineId && onlineId.length >= 3 && lower.includes(onlineId.toLowerCase())) {
    return 'Password must not contain your Online ID.';
  }
  if (/^(.)\1+$/.test(password)) {
    return 'Password must not be a single repeated character.';
  }
  if (COMMON_LONG_PASSWORDS.has(lower)) {
    return 'This password is too common. Choose something less predictable.';
  }
  return null;
}

/** A small denylist of common passwords that satisfy the minimum length. */
const COMMON_LONG_PASSWORDS = new Set([
  '123456789012',
  '1234567890123',
  '12345678901234',
  '123456789abc',
  'password1234',
  'password12345',
  'password123456',
  'passwordpassword',
  'qwertyuiopas',
  'qwertyuiop123',
  'qwerty123456',
  'iloveyou1234',
  'administrator',
  'letmein12345',
  'welcome12345',
  'football1234',
  'baseball1234',
  'abcdefghijkl',
  'abc123456789',
  '111111111111',
  'aaaaaaaaaaaa',
  'changeme1234',
  'playstation5',
  'playstation4',
  'gamergamer12',
  'minecraft123',
  'fortnite1234',
  'akenonetwork',
]);
