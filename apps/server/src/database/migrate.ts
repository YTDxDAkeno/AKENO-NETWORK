import { existsSync } from 'node:fs';
import { fileURLToPath } from 'node:url';
import { migrate } from 'drizzle-orm/node-postgres/migrator';
import { sql } from 'drizzle-orm';
import type { Database } from './client.js';

/** Arbitrary but fixed advisory lock key so concurrent starts never run migrations twice. */
const MIGRATION_LOCK_KEY = 4_206_942_001;

export function resolveMigrationsFolder(configured: string | null): string {
  if (configured) return configured;
  // Development (tsx): src/database/migrations. Production bundle: dist/migrations.
  const candidates = [
    fileURLToPath(new URL('./migrations', import.meta.url)),
    fileURLToPath(new URL('../src/database/migrations', import.meta.url)),
  ];
  const found = candidates.find((candidate) => existsSync(candidate));
  if (!found) throw new Error(`Could not find the migrations folder. Looked in: ${candidates.join(', ')}`);
  return found;
}

/**
 * Applies pending SQL migrations. Each migration runs in a transaction; a PostgreSQL advisory
 * lock serializes concurrent callers. Already applied migrations are skipped.
 */
export async function runMigrations(db: Database, migrationsFolder: string): Promise<void> {
  const client = await db.$client.connect();
  try {
    await client.query('SELECT pg_advisory_lock($1)', [MIGRATION_LOCK_KEY]);
    try {
      await migrate(db, { migrationsFolder });
    } finally {
      await client.query('SELECT pg_advisory_unlock($1)', [MIGRATION_LOCK_KEY]);
    }
  } finally {
    client.release();
  }
}

/** Returns true when the migrations journal table exists and has at least one entry. */
export async function hasAppliedMigrations(db: Database): Promise<boolean> {
  const result = await db.execute<{ applied: boolean }>(sql`
    SELECT EXISTS (
      SELECT 1 FROM information_schema.tables
      WHERE table_schema = 'drizzle' AND table_name = '__drizzle_migrations'
    ) AS applied
  `);
  return Boolean(result.rows[0]?.applied);
}
