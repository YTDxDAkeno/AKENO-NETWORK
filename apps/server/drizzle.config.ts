import { defineConfig } from 'drizzle-kit';

/**
 * drizzle-kit is only used at development time to generate SQL migration files from the
 * TypeScript schema. Migrations are applied by the application itself (see src/database/migrate.ts).
 */
export default defineConfig({
  dialect: 'postgresql',
  schema: './src/database/schema/index.ts',
  out: './src/database/migrations',
  strict: true,
  verbose: true,
  dbCredentials: {
    url: process.env.DATABASE_URL ?? 'postgres://akeno:akeno_dev_password@127.0.0.1:5432/akeno_dev',
  },
});
