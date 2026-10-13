import { sql } from 'drizzle-orm';
import { check, index, integer, pgTable, primaryKey, timestamp, uniqueIndex, uuid, varchar } from 'drizzle-orm/pg-core';
import { partyPrivacyEnum } from './enums.js';
import { conversations } from './messaging.js';
import { users } from './users.js';

/**
 * Parties are ephemeral gatherings. A party exists while it has members; when the last
 * member leaves (or the leader closes it) the party, its chat and invitations are deleted.
 */
export const parties = pgTable(
  'parties',
  {
    id: uuid('id').primaryKey().defaultRandom(),
    name: varchar('name', { length: 48 }).notNull(),
    leaderId: uuid('leader_id')
      .notNull()
      .references(() => users.id, { onDelete: 'restrict' }),
    privacy: partyPrivacyEnum('privacy').notNull().default('friends'),
    maxMembers: integer('max_members').notNull().default(8),
    memberCount: integer('member_count').notNull().default(0),
    conversationId: uuid('conversation_id')
      .notNull()
      .references(() => conversations.id, { onDelete: 'cascade' }),
    createdAt: timestamp('created_at', { withTimezone: true }).notNull().defaultNow(),
    updatedAt: timestamp('updated_at', { withTimezone: true }).notNull().defaultNow(),
  },
  (t) => [
    uniqueIndex('parties_conversation_id_key').on(t.conversationId),
    check('parties_max_members_check', sql`${t.maxMembers} BETWEEN 2 AND 16`),
    check('parties_member_count_check', sql`${t.memberCount} >= 0 AND ${t.memberCount} <= ${t.maxMembers}`),
    index('parties_leader_id_idx').on(t.leaderId),
  ],
);

export const partyMembers = pgTable(
  'party_members',
  {
    partyId: uuid('party_id')
      .notNull()
      .references(() => parties.id, { onDelete: 'cascade' }),
    userId: uuid('user_id')
      .notNull()
      .references(() => users.id, { onDelete: 'cascade' }),
    joinedAt: timestamp('joined_at', { withTimezone: true }).notNull().defaultNow(),
  },
  (t) => [
    primaryKey({ columns: [t.partyId, t.userId], name: 'party_members_pkey' }),
    // A user can be in at most one party at a time.
    uniqueIndex('party_members_user_id_key').on(t.userId),
  ],
);
