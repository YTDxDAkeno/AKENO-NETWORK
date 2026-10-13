import { z } from 'zod';
import {
  GAME_PLATFORMS,
  GAME_SESSION_JOIN_MODES,
  GAME_SESSION_STATUSES,
  GAME_SESSION_VISIBILITY,
  LIMITS,
} from '../constants.js';
import { multiLineText, singleLineText } from './common.js';

const optionalText = (max: number) =>
  singleLineText(max)
    .nullable()
    .optional()
    .transform((value) => (value === undefined ? undefined : value === null || value === '' ? null : value));

export const gameIdentifierSchema = z
  .string()
  .trim()
  .max(LIMITS.gameIdentifierMax)
  .regex(/^[A-Za-z0-9._:-]*$/, 'Game identifier may contain letters, numbers, dots, colons, underscores and hyphens.');

export const createGameSessionSchema = z.object({
  gameName: singleLineText(LIMITS.gameNameMax, 1),
  gameIdentifier: gameIdentifierSchema
    .nullable()
    .optional()
    .transform((value) => (value ? value : null)),
  description: multiLineText(LIMITS.gameSessionDescriptionMax).default(''),
  visibility: z.enum(GAME_SESSION_VISIBILITY).default('friends'),
  joinMode: z.enum(GAME_SESSION_JOIN_MODES).default('open'),
  maxParticipants: z.coerce
    .number()
    .int()
    .min(LIMITS.gameSessionParticipantsMin)
    .max(LIMITS.gameSessionParticipantsMax)
    .default(4),
  region: optionalText(LIMITS.gameSessionRegionMax),
  platform: z.enum(GAME_PLATFORMS).nullable().optional(),
  connectionInfo: optionalText(LIMITS.gameSessionConnectionInfoMax),
});
export type CreateGameSessionInput = z.input<typeof createGameSessionSchema>;

export const updateGameSessionSchema = z
  .object({
    description: multiLineText(LIMITS.gameSessionDescriptionMax),
    visibility: z.enum(GAME_SESSION_VISIBILITY),
    joinMode: z.enum(GAME_SESSION_JOIN_MODES),
    maxParticipants: z.coerce
      .number()
      .int()
      .min(LIMITS.gameSessionParticipantsMin)
      .max(LIMITS.gameSessionParticipantsMax),
    status: z.enum(['waiting', 'active']),
    region: optionalText(LIMITS.gameSessionRegionMax),
    platform: z.enum(GAME_PLATFORMS).nullable(),
    connectionInfo: optionalText(LIMITS.gameSessionConnectionInfoMax),
  })
  .partial();
export type UpdateGameSessionInput = z.input<typeof updateGameSessionSchema>;

export const gameSessionListQuerySchema = z.object({
  q: z.string().trim().max(LIMITS.gameNameMax).optional(),
  status: z.enum(GAME_SESSION_STATUSES).optional(),
  platform: z.enum(GAME_PLATFORMS).optional(),
  region: z.string().trim().max(LIMITS.gameSessionRegionMax).optional(),
  joinable: z
    .enum(['true', 'false'])
    .optional()
    .transform((value) => value === 'true'),
  limit: z.coerce.number().int().min(1).max(LIMITS.pageSizeMax).default(50),
});
export type GameSessionListQuery = z.input<typeof gameSessionListQuerySchema>;

export const joinGameSessionSchema = z.object({
  message: singleLineText(LIMITS.joinRequestMessageMax).optional(),
});
