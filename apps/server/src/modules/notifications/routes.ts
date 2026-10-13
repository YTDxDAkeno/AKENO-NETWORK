import type { FastifyPluginAsync } from 'fastify';
import { cursorPageQuerySchema, markNotificationsReadSchema } from '@akeno/shared';
import type { AppContext } from '../../app/context.js';
import { authOf, requireAuth } from '../../middleware/auth.js';
import { parseInput } from '../../utils/validation.js';

export function notificationsRoutes(ctx: AppContext): FastifyPluginAsync {
  return async (app) => {
    app.addHook('preHandler', requireAuth(ctx));

    app.get('/notifications', async (request) => {
      const { before, limit } = parseInput(cursorPageQuerySchema, request.query);
      return ctx.notifications.list(authOf(request).userId, before, limit);
    });

    app.post('/notifications/read', async (request, reply) => {
      const input = parseInput(markNotificationsReadSchema, request.body);
      await ctx.notifications.markRead(authOf(request).userId, input);
      return reply.status(204).send();
    });
  };
}
