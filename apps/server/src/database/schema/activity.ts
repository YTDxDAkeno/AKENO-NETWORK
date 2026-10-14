import { sql } from 'drizzle-orm';
import { check, index, integer, pgTable, timestamp, uniqueIndex, uuid, varchar } from 'drizzle-orm/pg-core';
import { deviceCredentials } from './devices.js';
import { users } from './users.js';

/**
 * Game sessions reported by paired devices (automatic game activity).
 *
 * All timestamps come from the server. A session is active while `ended_at` is null; a user has
 * at most one active session per title, so two devices reporting the same game share one session
 * and nothing is counted twice. Sessions without a heartbeat for GAME_ACTIVITY.staleAfterSeconds
 * end at their last heartbeat, so rest mode, crashes and lost connections never add time.
 */
export const gameActivitySessions = pgTable(
  'game_activity_sessions',
  {
    id: uuid('id').primaryKey().defaultRandom(),
    userId: uuid('user_id')
      .notNull()
      .references(() => users.id, { onDelete: 'cascade' }),
    titleId: varchar('title_id', { length: 16 }).notNull(),
    /** Name the reporting device read from the console's own metadata (shown for this user only). */
    titleName: varchar('title_name', { length: 128 }),
    platform: varchar('platform', { length: 16 }).notNull(),
    startedAt: timestamp('started_at', { withTimezone: true }).notNull().defaultNow(),
    lastHeartbeatAt: timestamp('last_heartbeat_at', { withTimezone: true }).notNull().defaultNow(),
    endedAt: timestamp('ended_at', { withTimezone: true }),
    endReason: varchar('end_reason', { length: 24 }),
    startedByDeviceId: uuid('started_by_device_id').references(() => deviceCredentials.id, { onDelete: 'set null' }),
    lastDeviceId: uuid('last_device_id').references(() => deviceCredentials.id, { onDelete: 'set null' }),
    heartbeatCount: integer('heartbeat_count').notNull().default(1),
  },
  (t) => [
    uniqueIndex('game_activity_sessions_active_key').on(t.userId, t.titleId).where(sql`${t.endedAt} IS NULL`),
    index('game_activity_sessions_user_started_idx').on(t.userId, t.startedAt),
    index('game_activity_sessions_active_heartbeat_idx').on(t.lastHeartbeatAt).where(sql`${t.endedAt} IS NULL`),
    check('game_activity_sessions_heartbeat_check', sql`${t.lastHeartbeatAt} >= ${t.startedAt}`),
    check('game_activity_sessions_end_check', sql`${t.endedAt} IS NULL OR ${t.endedAt} >= ${t.startedAt}`),
    check('game_activity_sessions_reason_check', sql`(${t.endedAt} IS NULL) = (${t.endReason} IS NULL)`),
  ],
);

/** Game names maintained by administrators; they take precedence over names reported by devices. */
export const gameTitles = pgTable('game_titles', {
  titleId: varchar('title_id', { length: 16 }).primaryKey(),
  name: varchar('name', { length: 128 }).notNull(),
  updatedById: uuid('updated_by_id').references(() => users.id, { onDelete: 'set null' }),
  createdAt: timestamp('created_at', { withTimezone: true }).notNull().defaultNow(),
  updatedAt: timestamp('updated_at', { withTimezone: true }).notNull().defaultNow(),
});
