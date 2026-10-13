import type { FastifyPluginAsync } from 'fastify';
import { idParamsSchema, userIdParamsSchema, userTargetSchema } from '@akeno/shared';
import type { AppContext } from '../../app/context.js';
import { authOf, requireAuth } from '../../middleware/auth.js';
import { parseInput } from '../../utils/validation.js';

export function friendsRoutes(ctx: AppContext): FastifyPluginAsync {
  return async (app) => {
    app.addHook('preHandler', requireAuth(ctx));

    app.get('/friends', async (request) => ({ friends: await ctx.friends.listFriends(authOf(request).userId) }));

    app.delete('/friends/:userId', async (request, reply) => {
      const { userId } = parseInput(userIdParamsSchema, request.params);
      await ctx.friends.removeFriend(authOf(request).userId, userId);
      return reply.status(204).send();
    });

    app.get('/friends/requests', async (request) => ctx.friends.listRequests(authOf(request).userId));

    app.post(
      '/friends/requests',
      { config: { rateLimit: { max: 30, timeWindow: '1 minute' } } },
      async (request, reply) => {
        const { userId } = parseInput(userTargetSchema, request.body);
        const result = await ctx.friends.sendRequest(authOf(request).userId, userId);
        return reply.status(result.status === 'pending' ? 201 : 200).send(result);
      },
    );

    app.post('/friends/requests/:id/accept', async (request) => {
      const { id } = parseInput(idParamsSchema, request.params);
      await ctx.friends.acceptRequest(authOf(request).userId, id);
      return { status: 'accepted' };
    });

    app.post('/friends/requests/:id/decline', async (request) => {
      const { id } = parseInput(idParamsSchema, request.params);
      await ctx.friends.declineRequest(authOf(request).userId, id);
      return { status: 'declined' };
    });

    app.post('/friends/requests/:id/cancel', async (request) => {
      const { id } = parseInput(idParamsSchema, request.params);
      await ctx.friends.cancelRequest(authOf(request).userId, id);
      return { status: 'cancelled' };
    });

    app.get('/blocks', async (request) => ({ blocked: await ctx.friends.listBlocked(authOf(request).userId) }));

    app.post('/blocks', async (request, reply) => {
      const { userId } = parseInput(userTargetSchema, request.body);
      await ctx.friends.block(authOf(request).userId, userId);
      return reply.status(204).send();
    });

    app.delete('/blocks/:userId', async (request, reply) => {
      const { userId } = parseInput(userIdParamsSchema, request.params);
      await ctx.friends.unblock(authOf(request).userId, userId);
      return reply.status(204).send();
    });
  };
}
