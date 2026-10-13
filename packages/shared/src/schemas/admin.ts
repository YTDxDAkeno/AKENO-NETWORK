import { z } from 'zod';
import { ACCOUNT_STATUSES, LIMITS, USER_ROLES } from '../constants.js';
import { singleLineText } from './common.js';

export const createRegistrationInvitationSchema = z.object({
  maxUses: z.coerce.number().int().min(1).max(LIMITS.invitationMaxUses).default(1),
  expiresInHours: z.coerce
    .number()
    .int()
    .min(1)
    .max(LIMITS.invitationMaxLifetimeHours)
    .nullable()
    .default(72),
  note: singleLineText(LIMITS.invitationNoteMax).default(''),
});
export type CreateRegistrationInvitationInput = z.input<typeof createRegistrationInvitationSchema>;

export const adminUpdateUserSchema = z
  .object({
    status: z.enum(ACCOUNT_STATUSES).exclude(['deleted']),
    role: z.enum(USER_ROLES),
  })
  .partial();
export type AdminUpdateUserInput = z.infer<typeof adminUpdateUserSchema>;

export const adminUserListQuerySchema = z.object({
  q: z.string().trim().max(LIMITS.searchQueryMax).optional(),
  limit: z.coerce.number().int().min(1).max(LIMITS.pageSizeMax).default(50),
});

export const cursorPageQuerySchema = z.object({
  before: z.coerce.number().int().min(1).optional(),
  limit: z.coerce.number().int().min(1).max(LIMITS.pageSizeMax).default(50),
});
