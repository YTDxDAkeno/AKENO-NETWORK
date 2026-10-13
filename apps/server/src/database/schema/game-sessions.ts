import { sql } from 'drizzle-orm';
import { check, index, integer, pgTable, primaryKey, timestamp, uniqueIndex, uuid, varchar } from 'drizzle-orm/pg-core';
import {
  gameSessionJoinModeEnum,
  gameSessionRoleEnum,
  gameSessionStatusEnum,
  gameSessionVisibilityEnum,
  joinRequestStatusEnum,
} from './enums.js';
import { users } from './users.js';

/** Application-level game session directory entries. These do not provide game networking. */
export const gameSessions = pgTable(
  'game_sessions',
  {
    id: uuid('id').primaryKey().defaultRandom(),
    gameName: varchar('game_name', { length: 64 }).notNull(),
    gameIdentifier: varchar('game_identifier', { length: 64 }),
    description: varchar('description', { length: 500 }).notNull().default(''),
    hostId: uuid('host_id')
      .notNull()
      .references(() => users.id, { onDelete: 'restrict' }),
    visibility: gameSessionVisibilityEnum('visibility').notNull().default('friends'),
    joinMode: gameSessionJoinModeEnum('join_mode').notNull().default('open'),
    maxParticipants: integer('max_participants').notNull().default(4),
    participantCount: integer('participant_count').notNull().default(0),
    status: gameSessionStatusEnum('status').notNull().default('waiting'),
    region: varchar('region', { length: 32 }),
    platform: varchar('platform', { length: 32 }),
    /** Externally supplied connection metadata (e.g. a lobby code); only shown to participants. */
    connectionInfo: varchar('connection_info', { length: 300 }),
    createdAt: timestamp('created_at', { withTimezone: true }).notNull().defaultNow(),
    updatedAt: timestamp('updated_at', { withTimezone: true }).notNull().defaultNow(),
    startedAt: timestamp('started_at', { withTimezone: true }),
    endedAt: timestamp('ended_at', { withTimezone: true }),
  },
  (t) => [
    check('game_sessions_max_participants_check', sql`${t.maxParticipants} BETWEEN 2 AND 64`),
    // Database-level guarantee that capacity can never be exceeded, even under concurrent joins.
    check(
      'game_sessions_participant_count_check',
      sql`${t.participantCount} >= 0 AND ${t.participantCount} <= ${t.maxParticipants}`,
    ),
    index('game_sessions_status_visibility_idx').on(t.status, t.visibility, t.createdAt),
    index('game_sessions_host_id_idx').on(t.hostId),
  ],
);

export const gameSessionMembers = pgTable(
  'game_session_members',
  {
    sessionId: uuid('session_id')
      .notNull()
      .references(() => gameSessions.id, { onDelete: 'cascade' }),
    userId: uuid('user_id')
      .notNull()
      .references(() => users.id, { onDelete: 'cascade' }),
    role: gameSessionRoleEnum('role').notNull().default('player'),
    joinedAt: timestamp('joined_at', { withTimezone: true }).notNull().defaultNow(),
  },
  (t) => [
    primaryKey({ columns: [t.sessionId, t.userId], name: 'game_session_members_pkey' }),
    index('game_session_members_user_id_idx').on(t.userId),
  ],
);

export const gameSessionJoinRequests = pgTable(
  'game_session_join_requests',
  {
    id: uuid('id').primaryKey().defaultRandom(),
    sessionId: uuid('session_id')
      .notNull()
      .references(() => gameSessions.id, { onDelete: 'cascade' }),
    userId: uuid('user_id')
      .notNull()
      .references(() => users.id, { onDelete: 'cascade' }),
    status: joinRequestStatusEnum('status').notNull().default('pending'),
    message: varchar('message', { length: 200 }).notNull().default(''),
    createdAt: timestamp('created_at', { withTimezone: true }).notNull().defaultNow(),
    respondedAt: timestamp('responded_at', { withTimezone: true }),
    respondedById: uuid('responded_by_id').references(() => users.id, { onDelete: 'set null' }),
  },
  (t) => [
    uniqueIndex('game_session_join_requests_pending_key')
      .on(t.sessionId, t.userId)
      .where(sql`${t.status} = 'pending'`),
    index('game_session_join_requests_user_id_idx').on(t.userId),
  ],
);
