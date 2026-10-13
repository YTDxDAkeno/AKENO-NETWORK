import { sql } from 'drizzle-orm';
import { check, index, pgTable, primaryKey, timestamp, uniqueIndex, uuid } from 'drizzle-orm/pg-core';
import { friendRequestStatusEnum } from './enums.js';
import { users } from './users.js';

export const friendRequests = pgTable(
  'friend_requests',
  {
    id: uuid('id').primaryKey().defaultRandom(),
    senderId: uuid('sender_id')
      .notNull()
      .references(() => users.id, { onDelete: 'cascade' }),
    recipientId: uuid('recipient_id')
      .notNull()
      .references(() => users.id, { onDelete: 'cascade' }),
    status: friendRequestStatusEnum('status').notNull().default('pending'),
    createdAt: timestamp('created_at', { withTimezone: true }).notNull().defaultNow(),
    respondedAt: timestamp('responded_at', { withTimezone: true }),
  },
  (t) => [
    check('friend_requests_not_self_check', sql`${t.senderId} <> ${t.recipientId}`),
    // At most one pending request per unordered pair of users, in either direction.
    uniqueIndex('friend_requests_pending_pair_key')
      .on(sql`least(${t.senderId}, ${t.recipientId})`, sql`greatest(${t.senderId}, ${t.recipientId})`)
      .where(sql`${t.status} = 'pending'`),
    index('friend_requests_recipient_status_idx').on(t.recipientId, t.status),
    index('friend_requests_sender_status_idx').on(t.senderId, t.status),
  ],
);

/** Friendships are stored once per pair with user_low_id < user_high_id. */
export const friendships = pgTable(
  'friendships',
  {
    userLowId: uuid('user_low_id')
      .notNull()
      .references(() => users.id, { onDelete: 'cascade' }),
    userHighId: uuid('user_high_id')
      .notNull()
      .references(() => users.id, { onDelete: 'cascade' }),
    createdAt: timestamp('created_at', { withTimezone: true }).notNull().defaultNow(),
  },
  (t) => [
    primaryKey({ columns: [t.userLowId, t.userHighId], name: 'friendships_pkey' }),
    check('friendships_ordered_pair_check', sql`${t.userLowId} < ${t.userHighId}`),
    index('friendships_user_high_id_idx').on(t.userHighId),
  ],
);

export const userBlocks = pgTable(
  'user_blocks',
  {
    blockerId: uuid('blocker_id')
      .notNull()
      .references(() => users.id, { onDelete: 'cascade' }),
    blockedId: uuid('blocked_id')
      .notNull()
      .references(() => users.id, { onDelete: 'cascade' }),
    createdAt: timestamp('created_at', { withTimezone: true }).notNull().defaultNow(),
  },
  (t) => [
    primaryKey({ columns: [t.blockerId, t.blockedId], name: 'user_blocks_pkey' }),
    check('user_blocks_not_self_check', sql`${t.blockerId} <> ${t.blockedId}`),
    index('user_blocks_blocked_id_idx').on(t.blockedId),
  ],
);
