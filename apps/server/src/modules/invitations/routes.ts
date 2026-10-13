import type { FastifyPluginAsync } from 'fastify';
import { idParamsSchema } from '@akeno/shared';
import type { AppContext } from '../../app/context.js';
import { authOf, requireAuth } from '../../middleware/auth.js';
import { parseInput } from '../../utils/validation.js';

export function invitationsRoutes(ctx: AppContext): FastifyPluginAsync {
  return async (app) => {
    app.addHook('preHandler', requireAuth(ctx));

    app.get('/invitations', async (request) => ({
      invitations: await ctx.invitations.listIncoming(authOf(request).userId),
    }));

    app.post('/invitations/:id/accept', async (request) => {
      const { id } = parseInput(idParamsSchema, request.params);
      return ctx.invitations.accept(authOf(request).userId, id);
    });

    app.post('/invitations/:id/decline', async (request, reply) => {
      const { id } = parseInput(idParamsSchema, request.params);
      await ctx.invitations.decline(authOf(request).userId, id);
      return reply.status(204).send();
    });

    app.post('/invitations/:id/cancel', async (request, reply) => {
      const { id } = parseInput(idParamsSchema, request.params);
      await ctx.invitations.cancel(authOf(request).userId, id);
      return reply.status(204).send();
    });
  };
}
