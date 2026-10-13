import pg from 'pg';
import { createDatabase } from '../src/database/client.js';
import { resolveMigrationsFolder, runMigrations } from '../src/database/migrate.js';
import { TEMPLATE_DATABASE, TEST_ADMIN_URL, databaseUrl } from './test-env.js';

/** Creates a freshly migrated template database that every test file clones. */
export default async function setup(): Promise<() => Promise<void>> {
  const admin = new pg.Client({ connectionString: TEST_ADMIN_URL });
  await admin.connect();
  try {
    await admin.query(`DROP DATABASE IF EXISTS ${TEMPLATE_DATABASE} WITH (FORCE)`);
    await admin.query(`CREATE DATABASE ${TEMPLATE_DATABASE}`);
  } finally {
    await admin.end();
  }
  const handle = createDatabase({ url: databaseUrl(TEMPLATE_DATABASE), max: 2 });
  try {
    await runMigrations(handle.db, resolveMigrationsFolder(null));
  } finally {
    await handle.close();
  }

  return async () => {
    const cleanup = new pg.Client({ connectionString: TEST_ADMIN_URL });
    await cleanup.connect();
    try {
      const leftovers = await cleanup.query<{ datname: string }>(
        "SELECT datname FROM pg_database WHERE datname LIKE 'akeno_test_%'",
      );
      for (const row of leftovers.rows) {
        await cleanup.query(`DROP DATABASE IF EXISTS ${row.datname} WITH (FORCE)`);
      }
    } finally {
      await cleanup.end();
    }
  };
}
