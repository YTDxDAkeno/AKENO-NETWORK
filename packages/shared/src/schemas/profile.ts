import { z } from 'zod';
import {
  AVATAR_IDS,
  FRIEND_REQUEST_PRIVACY,
  GAME_ACTIVITY,
  LIMITS,
  MESSAGE_PRIVACY,
  PRESENCE_PREFERENCES,
  PRESENCE_VISIBILITY,
  PROFILE_VISIBILITY,
} from '../constants.js';
import { BADGE_IDS, FEATURED_BADGES_MAX } from '../badges.js';
import { HEX_COLOR_PATTERN, PROFILE_THEME_IDS } from '../themes.js';
import { gameTitleIdSchema } from './activity.js';
import { multiLineText, singleLineText } from './common.js';

export const avatarSchema = z.enum(AVATAR_IDS, { error: 'Choose one of the available avatars.' });

export const updateProfileSchema = z
  .object({
    displayName: singleLineText(LIMITS.displayNameMax, 1),
    avatar: avatarSchema,
    bio: multiLineText(LIMITS.bioMax),
    statusMessage: singleLineText(LIMITS.statusMessageMax),
    favoriteGames: z
      .array(singleLineText(LIMITS.favoriteGameNameMax, 1))
      .max(LIMITS.favoriteGamesMax, `You can list up to ${LIMITS.favoriteGamesMax} favorite games.`),
    activity: singleLineText(LIMITS.activityMax).nullable(),
  })
  .partial();
export type UpdateProfileInput = z.input<typeof updateProfileSchema>;

export const hexColorSchema = z
  .string()
  .trim()
  .toLowerCase()
  .regex(HEX_COLOR_PATTERN, 'Use a colour such as #4f7cff.');

/** Theme, banner overlay and featured badges. Uploaded images have their own endpoints. */
export const updateAppearanceSchema = z
  .object({
    theme: z.enum(PROFILE_THEME_IDS),
    accentColor: hexColorSchema.nullable(),
    bannerOverlay: z.boolean(),
    featuredBadges: z
      .array(z.enum(BADGE_IDS))
      .max(FEATURED_BADGES_MAX, `You can feature up to ${FEATURED_BADGES_MAX} badges.`)
      .refine((ids) => new Set(ids).size === ids.length, 'Each badge can only be featured once.'),
  })
  .partial();
export type UpdateAppearanceInput = z.infer<typeof updateAppearanceSchema>;

export const updatePrivacySchema = z
  .object({
    profileVisibility: z.enum(PROFILE_VISIBILITY),
    presenceVisibility: z.enum(PRESENCE_VISIBILITY),
    lastSeenVisibility: z.enum(PRESENCE_VISIBILITY),
    messagePrivacy: z.enum(MESSAGE_PRIVACY),
    friendRequestPrivacy: z.enum(FRIEND_REQUEST_PRIVACY),
    gameActivityTracking: z.boolean(),
    gameActivityVisibility: z.enum(PRESENCE_VISIBILITY),
    gameHistoryVisibility: z.enum(PRESENCE_VISIBILITY),
    gamePlaytimeVisible: z.boolean(),
    gameExcludedTitles: z
      .array(gameTitleIdSchema)
      .max(GAME_ACTIVITY.excludedTitlesMax, `You can exclude up to ${GAME_ACTIVITY.excludedTitlesMax} games.`),
  })
  .partial();
export type UpdatePrivacyInput = z.infer<typeof updatePrivacySchema>;

export const updatePresenceSchema = z.object({
  status: z.enum(PRESENCE_PREFERENCES),
});
export type UpdatePresenceInput = z.infer<typeof updatePresenceSchema>;

export const userSearchSchema = z.object({
  q: z.string().trim().min(1, 'Enter a search term.').max(LIMITS.searchQueryMax),
  limit: z.coerce.number().int().min(1).max(25).default(20),
});
