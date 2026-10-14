import type { FastifyPluginAsync } from 'fastify';
import { z } from 'zod';
import { gameHeartbeatSchema, gameStopSchema } from '@akeno/shared';
import type { AppContext } from '../../app/context.js';
import { authOf, requireAuth } from '../../middleware/auth.js';
import { parseInput } from '../../utils/validation.js';

const handleParams = z.object({ handle: z.string().trim().min(1).max(64) });

/**
 * Automatic game activity.
 *   POST /activity/heartbeat, POST /activity/stop   paired devices with "activity:write" only
 *   GET  /users/:handle/activity                     anyone signed in; privacy rules applied
 *   DELETE /activity/history                         the account owner on the website
 */
export function activityRoutes(ctx: AppContext): FastifyPluginAsync {
  return async (app) => {
    app.addHook('preHandler', requireAuth(ctx));

    app.post('/activity/heartbeat', { config: { rateLimit: { max: 30, timeWindow: '1 minute' } } }, async (request) => {
      const input = parseInput(gameHeartbeatSchema, request.body);
      return ctx.activity.heartbeat(authOf(request), input);
    });

    app.post('/activity/stop', { config: { rateLimit: { max: 30, timeWindow: '1 minute' } } }, async (request) => {
      const input = parseInput(gameStopSchema, request.body ?? {});
      return ctx.activity.stop(authOf(request), input);
    });

    app.get('/users/:handle/activity', async (request) => {
      const { handle } = parseInput(handleParams, request.params);
      return ctx.activity.activityFor(authOf(request).userId, handle);
    });

    app.delete('/activity/history', async (request) => ctx.activity.clearHistory(authOf(request).userId));
  };
}
