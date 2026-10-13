import { z } from 'zod';
import { GROUP_JOIN_POLICIES, LIMITS } from '../constants.js';
import { multiLineText, singleLineText, uuidSchema } from './common.js';

export const createGroupSchema = z.object({
  name: singleLineText(LIMITS.groupNameMax, LIMITS.groupNameMin),
  description: multiLineText(LIMITS.groupDescriptionMax).default(''),
  joinPolicy: z.enum(GROUP_JOIN_POLICIES).default('invite_only'),
});
export type CreateGroupInput = z.input<typeof createGroupSchema>;

export const updateGroupSchema = z
  .object({
    name: singleLineText(LIMITS.groupNameMax, LIMITS.groupNameMin),
    description: multiLineText(LIMITS.groupDescriptionMax),
    joinPolicy: z.enum(GROUP_JOIN_POLICIES),
  })
  .partial();
export type UpdateGroupInput = z.input<typeof updateGroupSchema>;

export const updateGroupMemberSchema = z.object({
  role: z.enum(['moderator', 'member']),
});

export const transferOwnershipSchema = z.object({ userId: uuidSchema });
