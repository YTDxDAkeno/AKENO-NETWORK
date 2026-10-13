import { z } from 'zod';
import { LIMITS, PARTY_PRIVACY } from '../constants.js';
import { singleLineText, uuidSchema } from './common.js';

export const createPartySchema = z.object({
  name: singleLineText(LIMITS.partyNameMax).optional(),
  privacy: z.enum(PARTY_PRIVACY).default('friends'),
  maxMembers: z.coerce
    .number()
    .int()
    .min(LIMITS.partyMembersMin)
    .max(LIMITS.partyMembersMax)
    .default(LIMITS.partyMembersDefault),
});
export type CreatePartyInput = z.input<typeof createPartySchema>;

export const updatePartySchema = z
  .object({
    name: singleLineText(LIMITS.partyNameMax, 1),
    privacy: z.enum(PARTY_PRIVACY),
  })
  .partial();
export type UpdatePartyInput = z.input<typeof updatePartySchema>;

export const transferLeaderSchema = z.object({ userId: uuidSchema });
