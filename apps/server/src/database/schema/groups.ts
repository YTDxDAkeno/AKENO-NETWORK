import { sql } from 'drizzle-orm';
import { index, pgTable, primaryKey, timestamp, uniqueIndex, uuid, varchar } from 'drizzle-orm/pg-core';
import { groupJoinPolicyEnum, groupRoleEnum } from './enums.js';
import { conversations } from './messaging.js';
import { users } from './users.js';

export const groups = pgTable(
  'groups',
  {
    id: uuid('id').primaryKey().defaultRandom(),
    name: varchar('name', { length: 48 }).notNull(),
    description: varchar('description', { length: 500 }).notNull().default(''),
    joinPolicy: groupJoinPolicyEnum('join_policy').notNull().default('invite_only'),
    /** Deleting the conversation deletes the group, its members and its messages. */
    conversationId: uuid('conversation_id')
      .notNull()
      .references(() => conversations.id, { onDelete: 'cascade' }),
    createdById: uuid('created_by_id').references(() => users.id, { onDelete: 'set null' }),
    createdAt: timestamp('created_at', { withTimezone: true }).notNull().defaultNow(),
    updatedAt: timestamp('updated_at', { withTimezone: true }).notNull().defaultNow(),
  },
  (t) => [
    uniqueIndex('groups_conversation_id_key').on(t.conversationId),
    index('groups_join_policy_idx').on(t.joinPolicy),
  ],
);

export const groupMembers = pgTable(
  'group_members',
  {
    groupId: uuid('group_id')
      .notNull()
      .references(() => groups.id, { onDelete: 'cascade' }),
    userId: uuid('user_id')
      .notNull()
      .references(() => users.id, { onDelete: 'cascade' }),
    role: groupRoleEnum('role').notNull().default('member'),
    joinedAt: timestamp('joined_at', { withTimezone: true }).notNull().defaultNow(),
  },
  (t) => [
    primaryKey({ columns: [t.groupId, t.userId], name: 'group_members_pkey' }),
    // Exactly one owner per group is enforced by this index plus service-level transfer rules.
    uniqueIndex('group_members_single_owner_key')
      .on(t.groupId)
      .where(sql`${t.role} = 'owner'`),
    index('group_members_user_id_idx').on(t.userId),
  ],
);
