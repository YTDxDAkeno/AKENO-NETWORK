import { sql } from 'drizzle-orm';
import { check, index, pgTable, timestamp, uniqueIndex, uuid } from 'drizzle-orm/pg-core';
import { invitationKindEnum, invitationStatusEnum } from './enums.js';
import { gameSessions } from './game-sessions.js';
import { groups } from './groups.js';
import { parties } from './parties.js';
import { users } from './users.js';

/** Invitations to groups, parties and game sessions share one table with exactly one target column set. */
export const invitations = pgTable(
  'invitations',
  {
    id: uuid('id').primaryKey().defaultRandom(),
    kind: invitationKindEnum('kind').notNull(),
    groupId: uuid('group_id').references(() => groups.id, { onDelete: 'cascade' }),
    partyId: uuid('party_id').references(() => parties.id, { onDelete: 'cascade' }),
    gameSessionId: uuid('game_session_id').references(() => gameSessions.id, { onDelete: 'cascade' }),
    inviterId: uuid('inviter_id')
      .notNull()
      .references(() => users.id, { onDelete: 'cascade' }),
    inviteeId: uuid('invitee_id')
      .notNull()
      .references(() => users.id, { onDelete: 'cascade' }),
    status: invitationStatusEnum('status').notNull().default('pending'),
    createdAt: timestamp('created_at', { withTimezone: true }).notNull().defaultNow(),
    respondedAt: timestamp('responded_at', { withTimezone: true }),
    expiresAt: timestamp('expires_at', { withTimezone: true }).notNull(),
  },
  (t) => [
    check(
      'invitations_target_check',
      sql`(${t.kind} = 'group' AND ${t.groupId} IS NOT NULL AND ${t.partyId} IS NULL AND ${t.gameSessionId} IS NULL)
        OR (${t.kind} = 'party' AND ${t.partyId} IS NOT NULL AND ${t.groupId} IS NULL AND ${t.gameSessionId} IS NULL)
        OR (${t.kind} = 'game_session' AND ${t.gameSessionId} IS NOT NULL AND ${t.groupId} IS NULL AND ${t.partyId} IS NULL)`,
    ),
    check('invitations_not_self_check', sql`${t.inviterId} <> ${t.inviteeId}`),
    uniqueIndex('invitations_pending_group_key')
      .on(t.groupId, t.inviteeId)
      .where(sql`${t.status} = 'pending' AND ${t.kind} = 'group'`),
    uniqueIndex('invitations_pending_party_key')
      .on(t.partyId, t.inviteeId)
      .where(sql`${t.status} = 'pending' AND ${t.kind} = 'party'`),
    uniqueIndex('invitations_pending_game_session_key')
      .on(t.gameSessionId, t.inviteeId)
      .where(sql`${t.status} = 'pending' AND ${t.kind} = 'game_session'`),
    index('invitations_invitee_status_idx').on(t.inviteeId, t.status),
  ],
);
