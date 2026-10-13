import { eq } from 'drizzle-orm';
import { buildApp } from './app/build-app.js';
import { createLogger } from './app/logger.js';
import { loadConfig } from './config/env.js';
import { loadEnvFiles } from './config/load-env-file.js';
import { createDatabase } from './database/client.js';
import { resolveMigrationsFolder, runMigrations } from './database/migrate.js';
import { users } from './database/schema/index.js';

async function main(): Promise<void> {
  const envFile = loadEnvFiles();
  const config = loadConfig();
  const logger = createLogger(config);
  if (envFile) logger.info({ envFile }, 'Loaded environment file');

  const database = createDatabase({ url: config.databaseUrl, max: config.dbPoolMax });
  if (config.migrateOnStart) {
    logger.info('Applying database migrations');
    await runMigrations(database.db, resolveMigrationsFolder(config.migrationsDir));
    logger.info('Database migrations are up to date');
  }

  const { app } = await buildApp({ config, database, logger });

  let shuttingDown = false;
  const shutdown = async (signal: string) => {
    if (shuttingDown) return;
    shuttingDown = true;
    logger.info({ signal }, 'Shutting down gracefully');
    const force = setTimeout(() => {
      logger.error('Graceful shutdown timed out; exiting');
      process.exit(1);
    }, 10_000);
    force.unref();
    try {
      await app.close();
      await database.close();
      process.exit(0);
    } catch (error) {
      logger.error({ err: error }, 'Error during shutdown');
      process.exit(1);
    }
  };
  process.on('SIGTERM', () => void shutdown('SIGTERM'));
  process.on('SIGINT', () => void shutdown('SIGINT'));
  process.on('unhandledRejection', (reason) => logger.error({ err: reason }, 'Unhandled promise rejection'));

  await app.listen({ host: config.host, port: config.port });
  logger.info(
    { origin: config.publicOrigin, registrationMode: config.registrationMode, environment: config.env },
    'AKENO NETWORK is running',
  );

  try {
    const [admin] = await database.db.select({ id: users.id }).from(users).where(eq(users.role, 'admin')).limit(1);
    if (!admin) {
      logger.warn(
        'No administrator account exists yet. Create one with: pnpm admin:create --online-id <id> (or "node dist/cli.js create-admin --online-id <id>" in Docker).',
      );
    }
  } catch (error) {
    logger.warn({ err: error }, 'Could not check for administrator accounts (are migrations applied?)');
  }
}

main().catch((error: unknown) => {
  console.error(error instanceof Error ? error.message : error);
  process.exit(1);
});
