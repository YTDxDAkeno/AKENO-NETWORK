import { z } from 'zod';
import {
  AVATAR_IDS,
  FRIEND_REQUEST_PRIVACY,
  LIMITS,
  MESSAGE_PRIVACY,
  PRESENCE_PREFERENCES,
  PRESENCE_VISIBILITY,
  PROFILE_VISIBILITY,
} from '../constants.js';
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

export const updatePrivacySchema = z
  .object({
    profileVisibility: z.enum(PROFILE_VISIBILITY),
    presenceVisibility: z.enum(PRESENCE_VISIBILITY),
    lastSeenVisibility: z.enum(PRESENCE_VISIBILITY),
    messagePrivacy: z.enum(MESSAGE_PRIVACY),
    friendRequestPrivacy: z.enum(FRIEND_REQUEST_PRIVACY),
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
