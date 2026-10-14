import { z } from 'zod';
import { GRANTABLE_BADGE_IDS } from '../badges.js';
import { GAME_ARTWORK_SOURCES } from '../contracts/games.js';
import { GAME_ACTIVITY, LIMITS } from '../constants.js';
import { gameTitleIdSchema } from './activity.js';
import { singleLineText } from './common.js';

export const GAME_CATALOG_LIMITS = {
  nameMax: GAME_ACTIVITY.titleNameMax,
  aliasesMax: 10,
  aliasMax: 64,
  titleIdsMax: 20,
  attributionMax: 200,
  licenseMax: 300,
} as const;

const aliasesSchema = z
  .array(singleLineText(GAME_CATALOG_LIMITS.aliasMax, 1))
  .max(GAME_CATALOG_LIMITS.aliasesMax, `Up to ${GAME_CATALOG_LIMITS.aliasesMax} alternative names.`);

const provenance = {
  artworkSource: z.enum(GAME_ARTWORK_SOURCES).nullable(),
  artworkAttribution: singleLineText(GAME_CATALOG_LIMITS.attributionMax),
  artworkLicense: singleLineText(GAME_CATALOG_LIMITS.licenseMax),
};

export const createGameSchema = z.object({
  name: singleLineText(GAME_CATALOG_LIMITS.nameMax, 1),
  aliases: aliasesSchema.default([]),
  titleIds: z.array(gameTitleIdSchema).max(GAME_CATALOG_LIMITS.titleIdsMax).default([]),
  artworkSource: provenance.artworkSource.default(null),
  artworkAttribution: provenance.artworkAttribution.default(''),
  artworkLicense: provenance.artworkLicense.default(''),
});
export type CreateGameInput = z.input<typeof createGameSchema>;

export const updateGameSchema = z
  .object({
    name: singleLineText(GAME_CATALOG_LIMITS.nameMax, 1),
    aliases: aliasesSchema,
    ...provenance,
  })
  .partial();
export type UpdateGameInput = z.input<typeof updateGameSchema>;

export const gameListQuerySchema = z.object({
  q: z.string().trim().max(LIMITS.searchQueryMax).optional(),
  missingArtwork: z
    .enum(['true', 'false'])
    .transform((value) => value === 'true')
    .optional(),
  limit: z.coerce.number().int().min(1).max(200).default(100),
});

export const gameArtworkSlotSchema = z.enum(['icon', 'cover', 'banner']);

export const grantBadgeSchema = z.object({
  badgeId: z.enum(GRANTABLE_BADGE_IDS),
  note: singleLineText(LIMITS.invitationNoteMax).default(''),
});
export type GrantBadgeInput = z.input<typeof grantBadgeSchema>;
