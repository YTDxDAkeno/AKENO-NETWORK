import { z } from 'zod';
import { GAME_ACTIVITY, GAME_ACTIVITY_PLATFORMS } from '../constants.js';
import { singleLineText } from './common.js';

export const gameTitleIdSchema = z
  .string()
  .trim()
  .regex(GAME_ACTIVITY.titleIdPattern, 'Use a title ID such as PPSA01234 or CUSA01234.');

/** Sent by a paired device while it detects a running game. */
export const gameHeartbeatSchema = z.object({
  titleId: gameTitleIdSchema,
  /** Name the device read from the console's own metadata (optional, shown only for this user). */
  titleName: singleLineText(GAME_ACTIVITY.titleNameMax, 1).nullable().optional(),
  platform: z.enum(GAME_ACTIVITY_PLATFORMS),
});
export type GameHeartbeatInput = z.infer<typeof gameHeartbeatSchema>;

/** Sent by a paired device when the game it reported is no longer running. */
export const gameStopSchema = z.object({ reason: z.enum(['closed', 'shutdown']).optional() });
export type GameStopInput = z.infer<typeof gameStopSchema>;

export const gameTitleNameSchema = z.object({ name: singleLineText(GAME_ACTIVITY.titleNameMax, 1) });
