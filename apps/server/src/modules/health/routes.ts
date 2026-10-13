import type { FastifyPluginAsync } from 'fastify';
import { sql } from 'drizzle-orm';
import type { AppContext } from '../../app/context.js';
import { hasAppliedMigrations } from '../../database/migrate.js';

/**
 * Liveness: the process is running and serving HTTP.
 * Readiness: PostgreSQL answers and the schema has been migrated.
 */
export function healthRoutes(ctx: AppContext): FastifyPluginAsync {
  return async (app) => {
    app.get('/health/live', { config: { rateLimit: false } }, async () => ({ status: 'ok' }));

    app.get('/health/ready', { config: { rateLimit: false } }, async (_request, reply) => {
      try {
        await ctx.db.execute(sql`SELECT 1`);
        const migrated = await hasAppliedMigrations(ctx.db);
        if (!migrated) {
          return reply.status(503).send({ status: 'unavailable', database: 'connected', migrations: 'pending' });
        }
        return { status: 'ok', database: 'connected', migrations: 'applied' };
      } catch (error) {
        ctx.log.warn({ err: error }, 'Readiness check failed');
        return reply.status(503).send({ status: 'unavailable', database: 'unreachable' });
      }
    });
  };
}
