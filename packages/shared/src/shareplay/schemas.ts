import { z } from 'zod';
import { uuidSchema } from '../schemas/common.js';
import { SHAREPLAY_SOURCES } from './contracts.js';
import { SHAREPLAY_QUALITIES } from './quality.js';

export const startSharePlaySchema = z.object({
  source: z.enum(SHAREPLAY_SOURCES),
  quality: z.enum(SHAREPLAY_QUALITIES).optional(),
});
export type StartSharePlayInput = z.input<typeof startSharePlaySchema>;

export const sharePlayQualitySchema = z.object({ quality: z.enum(SHAREPLAY_QUALITIES) });

export const sharePlayGuestSchema = z.object({ userId: uuidSchema });

/** Query of the controller WebSocket. */
export const sharePlayInputQuerySchema = z.object({
  party: uuidSchema,
  role: z.enum(['sender', 'monitor']),
  /** Sender only: the epoch of the grant the connection is for. */
  epoch: z.coerce.number().int().min(0).max(0xffffffff).optional(),
});
