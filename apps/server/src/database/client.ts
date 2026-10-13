import { drizzle, type NodePgDatabase, type NodePgQueryResultHKT } from 'drizzle-orm/node-postgres';
import type { PgDatabase } from 'drizzle-orm/pg-core';
import pg from 'pg';
import * as schema from './schema/index.js';

export type Schema = typeof schema;
export type Database = NodePgDatabase<Schema> & { $client: pg.Pool };
/** Either the root database handle or an open transaction. */
export type DbExecutor = PgDatabase<NodePgQueryResultHKT, Schema>;

export interface DatabaseHandle {
  pool: pg.Pool;
  db: Database;
  close: () => Promise<void>;
}

export function createDatabase(options: { url: string; max: number; applicationName?: string }): DatabaseHandle {
  const pool = new pg.Pool({
    connectionString: options.url,
    max: options.max,
    idleTimeoutMillis: 30_000,
    connectionTimeoutMillis: 5_000,
    application_name: options.applicationName ?? 'akeno-network',
  });
  const db = drizzle({ client: pool, schema });
  return {
    pool,
    db,
    close: () => pool.end(),
  };
}
