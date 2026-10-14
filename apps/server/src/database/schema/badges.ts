import { index, pgTable, primaryKey, timestamp, uuid, varchar } from 'drizzle-orm/pg-core';
import type { BadgeId } from '@akeno/shared';
import { users } from './users.js';

/** Badges granted by administrators or the server operator (derived badges are never stored). */
export const userBadges = pgTable(
  'user_badges',
  {
    userId: uuid('user_id')
      .notNull()
      .references(() => users.id, { onDelete: 'cascade' }),
    badgeId: varchar('badge_id', { length: 32 }).$type<BadgeId>().notNull(),
    /** null when granted through the server command line. */
    grantedById: uuid('granted_by_id').references(() => users.id, { onDelete: 'set null' }),
    note: varchar('note', { length: 100 }).notNull().default(''),
    grantedAt: timestamp('granted_at', { withTimezone: true }).notNull().defaultNow(),
  },
  (t) => [primaryKey({ columns: [t.userId, t.badgeId], name: 'user_badges_pkey' }), index('user_badges_badge_id_idx').on(t.badgeId)],
);
