import { z } from 'zod';
import { uuidSchema } from '../schemas/common.js';
import { LAN_PLAY_PLATFORMS } from './contracts.js';

/** POST /parties/:id/lanplay — create a session. The caller becomes the host. */
export const createLanSessionSchema = z.object({
  platform: z.enum(LAN_PLAY_PLATFORMS),
  /** Optional game chosen from the compatibility database (id). */
  gameId: z.string().trim().min(1).max(64).optional(),
});
export type CreateLanSessionInput = z.input<typeof createLanSessionSchema>;

/** POST /parties/:id/lanplay/join — join the party's open session. */
export const joinLanSessionSchema = z.object({
  platform: z.enum(LAN_PLAY_PLATFORMS),
});
export type JoinLanSessionInput = z.input<typeof joinLanSessionSchema>;

/** POST /parties/:id/lanplay/game — host selects (or clears) the game. */
export const selectLanGameSchema = z.object({
  gameId: z.string().trim().min(1).max(64).nullable(),
});

export const lanParticipantSchema = z.object({ userId: uuidSchema });
