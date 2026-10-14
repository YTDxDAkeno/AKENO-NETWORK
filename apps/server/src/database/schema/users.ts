import { sql } from 'drizzle-orm';
import {
  type AnyPgColumn,
  boolean,
  check,
  index,
  integer,
  jsonb,
  pgTable,
  text,
  timestamp,
  uniqueIndex,
  uuid,
  varchar,
} from 'drizzle-orm/pg-core';
import {
  accountStatusEnum,
  friendRequestPrivacyEnum,
  messagePrivacyEnum,
  presencePreferenceEnum,
  presenceVisibilityEnum,
  profileVisibilityEnum,
  userRoleEnum,
} from './enums.js';

const timestamps = {
  createdAt: timestamp('created_at', { withTimezone: true }).notNull().defaultNow(),
  updatedAt: timestamp('updated_at', { withTimezone: true }).notNull().defaultNow(),
};

export const users = pgTable(
  'users',
  {
    id: uuid('id').primaryKey().defaultRandom(),
    /** Online ID as the user typed it (display casing). */
    onlineId: varchar('online_id', { length: 64 }).notNull(),
    /** Lower-cased online ID used for case-insensitive uniqueness and lookups. */
    onlineIdNormalized: varchar('online_id_normalized', { length: 64 }).notNull(),
    /** Argon2id PHC string. Null only for deleted (tombstoned) accounts. */
    passwordHash: text('password_hash'),
    role: userRoleEnum('role').notNull().default('user'),
    status: accountStatusEnum('status').notNull().default('active'),
    invitedByInvitationId: uuid('invited_by_invitation_id').references(
      (): AnyPgColumn => registrationInvitations.id,
      { onDelete: 'set null' },
    ),
    /** Last time the user was visibly online. Not updated while invisible. */
    lastSeenAt: timestamp('last_seen_at', { withTimezone: true }),
    passwordChangedAt: timestamp('password_changed_at', { withTimezone: true }),
    deletedAt: timestamp('deleted_at', { withTimezone: true }),
    ...timestamps,
  },
  (t) => [
    uniqueIndex('users_online_id_normalized_key').on(t.onlineIdNormalized),
    index('users_status_idx').on(t.status),
  ],
);

export const profiles = pgTable(
  'profiles',
  {
    userId: uuid('user_id')
      .primaryKey()
      .references(() => users.id, { onDelete: 'cascade' }),
    displayName: varchar('display_name', { length: 64 }).notNull(),
    avatar: varchar('avatar', { length: 32 }).notNull().default('nova'),
    bio: varchar('bio', { length: 500 }).notNull().default(''),
    statusMessage: varchar('status_message', { length: 80 }).notNull().default(''),
    favoriteGames: jsonb('favorite_games').$type<string[]>().notNull().default(sql`'[]'::jsonb`),
    activity: varchar('activity', { length: 64 }),
    activityUpdatedAt: timestamp('activity_updated_at', { withTimezone: true }),
    presencePreference: presencePreferenceEnum('presence_preference').notNull().default('online'),
    profileVisibility: profileVisibilityEnum('profile_visibility').notNull().default('everyone'),
    presenceVisibility: presenceVisibilityEnum('presence_visibility').notNull().default('friends'),
    lastSeenVisibility: presenceVisibilityEnum('last_seen_visibility').notNull().default('friends'),
    messagePrivacy: messagePrivacyEnum('message_privacy').notNull().default('friends'),
    friendRequestPrivacy: friendRequestPrivacyEnum('friend_request_privacy').notNull().default('everyone'),
    /** Paired devices may report the games this user plays (automatic game activity). */
    gameActivityTracking: boolean('game_activity_tracking').notNull().default(true),
    gameActivityVisibility: presenceVisibilityEnum('game_activity_visibility').notNull().default('friends'),
    gameHistoryVisibility: presenceVisibilityEnum('game_history_visibility').notNull().default('friends'),
    ...timestamps,
  },
  (t) => [index('profiles_display_name_idx').on(sql`lower(${t.displayName})`)],
);

export const registrationInvitations = pgTable(
  'registration_invitations',
  {
    id: uuid('id').primaryKey().defaultRandom(),
    /** SHA-256 of the normalized invitation code. The code itself is never stored. */
    codeHash: varchar('code_hash', { length: 64 }).notNull(),
    codeHint: varchar('code_hint', { length: 8 }).notNull(),
    note: varchar('note', { length: 100 }).notNull().default(''),
    maxUses: integer('max_uses').notNull().default(1),
    useCount: integer('use_count').notNull().default(0),
    expiresAt: timestamp('expires_at', { withTimezone: true }),
    revokedAt: timestamp('revoked_at', { withTimezone: true }),
    createdById: uuid('created_by_id').references((): AnyPgColumn => users.id, { onDelete: 'set null' }),
    createdAt: timestamp('created_at', { withTimezone: true }).notNull().defaultNow(),
  },
  (t) => [
    uniqueIndex('registration_invitations_code_hash_key').on(t.codeHash),
    check('registration_invitations_max_uses_check', sql`${t.maxUses} BETWEEN 1 AND 100`),
    check('registration_invitations_use_count_check', sql`${t.useCount} >= 0 AND ${t.useCount} <= ${t.maxUses}`),
  ],
);
