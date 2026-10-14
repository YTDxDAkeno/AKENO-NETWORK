import { sql } from 'drizzle-orm';
import { check, index, pgTable, text, timestamp, uuid, varchar } from 'drizzle-orm/pg-core';
import type { GameArtworkSource } from '@akeno/shared';
import { mediaObjects } from './media.js';
import { users } from './users.js';

/**
 * Canonical games curated by administrators. Artwork is only ever attached to a verified game
 * record; title IDs reach a game exclusively through game_title_mappings (never by name).
 */
export const games = pgTable(
  'games',
  {
    id: uuid('id').primaryKey().defaultRandom(),
    name: varchar('name', { length: 128 }).notNull(),
    /** Alternative names used for exact (normalized) matching of favorite games and game sessions. */
    aliases: text('aliases').array().notNull().default(sql`'{}'::text[]`),
    iconMediaId: varchar('icon_media_id', { length: 32 }).references(() => mediaObjects.id, { onDelete: 'set null' }),
    coverMediaId: varchar('cover_media_id', { length: 32 }).references(() => mediaObjects.id, { onDelete: 'set null' }),
    bannerMediaId: varchar('banner_media_id', { length: 32 }).references(() => mediaObjects.id, { onDelete: 'set null' }),
    artworkSource: varchar('artwork_source', { length: 32 }).$type<GameArtworkSource>(),
    artworkAttribution: varchar('artwork_attribution', { length: 200 }).notNull().default(''),
    artworkLicense: varchar('artwork_license', { length: 300 }).notNull().default(''),
    updatedById: uuid('updated_by_id').references(() => users.id, { onDelete: 'set null' }),
    createdAt: timestamp('created_at', { withTimezone: true }).notNull().defaultNow(),
    updatedAt: timestamp('updated_at', { withTimezone: true }).notNull().defaultNow(),
  },
  (t) => [index('games_name_idx').on(sql`lower(${t.name})`)],
);

/** Verified mapping of a PlayStation title ID (PPSA01234, CUSA01234) to a canonical game. */
export const gameTitleMappings = pgTable(
  'game_title_mappings',
  {
    titleId: varchar('title_id', { length: 16 }).primaryKey(),
    gameId: uuid('game_id')
      .notNull()
      .references(() => games.id, { onDelete: 'cascade' }),
    /** "admin" (created by an administrator) or "migrated" (from the Alpha 0.2 game_titles table). */
    source: varchar('source', { length: 32 }).notNull().default('admin'),
    updatedById: uuid('updated_by_id').references(() => users.id, { onDelete: 'set null' }),
    createdAt: timestamp('created_at', { withTimezone: true }).notNull().defaultNow(),
    updatedAt: timestamp('updated_at', { withTimezone: true }).notNull().defaultNow(),
  },
  (t) => [
    index('game_title_mappings_game_id_idx').on(t.gameId),
    check('game_title_mappings_title_id_check', sql`${t.titleId} ~ '^[A-Z]{4}[0-9]{5}$'`),
  ],
);
