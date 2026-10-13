import type { FastifyPluginAsync } from 'fastify';
import {
  createConversationSchema,
  idParamsSchema,
  markReadSchema,
  messagePageQuerySchema,
  sendMessageSchema,
} from '@akeno/shared';
import type { AppContext } from '../../app/context.js';
import { authOf, requireAuth } from '../../middleware/auth.js';
import { parseInput } from '../../utils/validation.js';

/** Message send limit per user: bursts are allowed, sustained flooding is not. */
export const SEND_MESSAGE_RATE_LIMIT = { max: 60, timeWindow: '1 minute' };

export function messagesRoutes(ctx: AppContext): FastifyPluginAsync {
  return async (app) => {
    app.addHook('preHandler', requireAuth(ctx));

    app.get('/conversations', async (request) => ({
      conversations: await ctx.messages.listDirect(authOf(request).userId),
    }));

    app.post('/conversations', async (request) => {
      const { userId } = parseInput(createConversationSchema, request.body);
      return ctx.messages.findOrCreateDirect(authOf(request).userId, userId);
    });

    app.get('/conversations/:id', async (request) => {
      const { id } = parseInput(idParamsSchema, request.params);
      return ctx.messages.getDirect(authOf(request).userId, id);
    });

    app.get('/conversations/:id/messages', async (request) => {
      const { id } = parseInput(idParamsSchema, request.params);
      const query = parseInput(messagePageQuerySchema, request.query);
      return ctx.messages.listMessages(authOf(request).userId, id, query);
    });

    app.post(
      '/conversations/:id/messages',
      { config: { rateLimit: SEND_MESSAGE_RATE_LIMIT } },
      async (request, reply) => {
        const { id } = parseInput(idParamsSchema, request.params);
        const input = parseInput(sendMessageSchema, request.body);
        const result = await ctx.messages.send(authOf(request).userId, id, input);
        return reply.status(result.created ? 201 : 200).send(result);
      },
    );

    app.post('/conversations/:id/read', async (request) => {
      const { id } = parseInput(idParamsSchema, request.params);
      const { lastReadMessageId } = parseInput(markReadSchema, request.body);
      return ctx.messages.markRead(authOf(request).userId, id, lastReadMessageId);
    });
  };
}
