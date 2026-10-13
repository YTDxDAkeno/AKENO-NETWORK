import { z } from 'zod';
import { LIMITS } from '../constants.js';
import { multiLineText, uuidSchema } from './common.js';

export const createConversationSchema = z.object({ userId: uuidSchema });

export const sendMessageSchema = z.object({
  body: multiLineText(LIMITS.messageBodyMax, 1),
  /** Client-generated UUID; retries with the same value never create duplicate messages. */
  clientMessageId: uuidSchema,
});
export type SendMessageInput = z.input<typeof sendMessageSchema>;

const cursorSchema = z.coerce.number().int().min(0).max(Number.MAX_SAFE_INTEGER);

export const messagePageQuerySchema = z
  .object({
    before: cursorSchema.optional(),
    after: cursorSchema.optional(),
    limit: z.coerce.number().int().min(1).max(LIMITS.pageSizeMax).default(40),
  })
  .refine((value) => value.before === undefined || value.after === undefined, {
    message: 'Use either "before" or "after", not both.',
  });
export type MessagePageQuery = z.infer<typeof messagePageQuerySchema>;

export const markReadSchema = z.object({
  lastReadMessageId: cursorSchema,
});
