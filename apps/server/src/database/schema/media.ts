import { sql } from 'drizzle-orm';
import { type AnyPgColumn, bigint, check, index, integer, jsonb, pgTable, timestamp, uuid, varchar } from 'drizzle-orm/pg-core';
import type { MediaKind } from '@akeno/shared';
import { users } from './users.js';

export interface StoredMediaVariant {
  file: string;
  width: number;
  height: number;
  bytes: number;
}

/**
 * Uploaded images. The ID is 128 random bits (32 hex characters) and is also the storage key and
 * the public reference; files live under MEDIA_DIR/objects/<id[0..2]>/<id>/. Rows are
 * soft-deleted when replaced (files are removed immediately) and purged later; the soft-deleted
 * rows keep the per-user daily upload quota honest.
 */
export const mediaObjects = pgTable(
  'media_objects',
  {
    id: varchar('id', { length: 32 }).primaryKey(),
    kind: varchar('kind', { length: 16 }).$type<MediaKind>().notNull(),
    ownerId: uuid('owner_id').references((): AnyPgColumn => users.id, { onDelete: 'set null' }),
    /** Source dimensions after orientation and cropping. */
    width: integer('width').notNull(),
    height: integer('height').notNull(),
    variants: jsonb('variants').$type<StoredMediaVariant[]>().notNull(),
    totalBytes: bigint('total_bytes', { mode: 'number' }).notNull(),
    /** SHA-256 of the uploaded bytes (duplicate detection and incident response, never shown). */
    sourceSha256: varchar('source_sha256', { length: 64 }).notNull(),
    createdAt: timestamp('created_at', { withTimezone: true }).notNull().defaultNow(),
    deletedAt: timestamp('deleted_at', { withTimezone: true }),
  },
  (t) => [
    check('media_objects_id_check', sql`${t.id} ~ '^[0-9a-f]{32}$'`),
    check('media_objects_kind_check', sql`${t.kind} IN ('avatar', 'banner', 'game_icon', 'game_cover', 'game_banner')`),
    index('media_objects_owner_created_idx').on(t.ownerId, t.createdAt),
    index('media_objects_deleted_idx').on(t.deletedAt).where(sql`${t.deletedAt} IS NOT NULL`),
  ],
);
