import { sql } from 'drizzle-orm';
import { bigint, check, index, pgTable, primaryKey, text, timestamp, uniqueIndex, uuid } from 'drizzle-orm/pg-core';
import { conversationKindEnum } from './enums.js';
import { users } from './users.js';

/**
 * A conversation is the message container for a direct chat, a group or a party.
 * Direct conversations store their two participants as an ordered pair.
 */
export const conversations = pgTable(
  'conversations',
  {
    id: uuid('id').primaryKey().defaultRandom(),
    kind: conversationKindEnum('kind').notNull(),
    directUserLowId: uuid('direct_user_low_id').references(() => users.id, { onDelete: 'restrict' }),
    directUserHighId: uuid('direct_user_high_id').references(() => users.id, { onDelete: 'restrict' }),
    createdAt: timestamp('created_at', { withTimezone: true }).notNull().defaultNow(),
    lastMessageAt: timestamp('last_message_at', { withTimezone: true }),
  },
  (t) => [
    check(
      'conversations_direct_pair_check',
      sql`(${t.kind} = 'direct' AND ${t.directUserLowId} IS NOT NULL AND ${t.directUserHighId} IS NOT NULL AND ${t.directUserLowId} < ${t.directUserHighId})
        OR (${t.kind} <> 'direct' AND ${t.directUserLowId} IS NULL AND ${t.directUserHighId} IS NULL)`,
    ),
    uniqueIndex('conversations_direct_pair_key')
      .on(t.directUserLowId, t.directUserHighId)
      .where(sql`${t.kind} = 'direct'`),
    index('conversations_direct_high_idx').on(t.directUserHighId),
  ],
);

export const messages = pgTable(
  'messages',
  {
    /** Global, monotonically increasing identifier used as the pagination and sync cursor. */
    id: bigint('id', { mode: 'number' }).primaryKey().generatedAlwaysAsIdentity(),
    conversationId: uuid('conversation_id')
      .notNull()
      .references(() => conversations.id, { onDelete: 'cascade' }),
    senderId: uuid('sender_id')
      .notNull()
      .references(() => users.id, { onDelete: 'restrict' }),
    body: text('body').notNull(),
    /** Client-generated idempotency key; unique per sender. */
    clientMessageId: uuid('client_message_id'),
    createdAt: timestamp('created_at', { withTimezone: true }).notNull().defaultNow(),
  },
  (t) => [
    check('messages_body_length_check', sql`char_length(${t.body}) BETWEEN 1 AND 4000`),
    index('messages_conversation_id_id_idx').on(t.conversationId, t.id),
    uniqueIndex('messages_sender_client_message_id_key').on(t.senderId, t.clientMessageId),
  ],
);

export const conversationReadStates = pgTable(
  'conversation_read_states',
  {
    conversationId: uuid('conversation_id')
      .notNull()
      .references(() => conversations.id, { onDelete: 'cascade' }),
    userId: uuid('user_id')
      .notNull()
      .references(() => users.id, { onDelete: 'cascade' }),
    lastReadMessageId: bigint('last_read_message_id', { mode: 'number' }).notNull().default(0),
    updatedAt: timestamp('updated_at', { withTimezone: true }).notNull().defaultNow(),
  },
  (t) => [
    primaryKey({ columns: [t.conversationId, t.userId], name: 'conversation_read_states_pkey' }),
    index('conversation_read_states_user_id_idx').on(t.userId),
  ],
);
