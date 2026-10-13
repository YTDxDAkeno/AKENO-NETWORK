import type { FastifyPluginAsync } from 'fastify';
import {
  createPartySchema,
  idAndUserParamsSchema,
  idParamsSchema,
  messagePageQuerySchema,
  sendMessageSchema,
  transferLeaderSchema,
  updatePartySchema,
  userTargetSchema,
} from '@akeno/shared';
import type { AppContext } from '../../app/context.js';
import { authOf, requireAuth } from '../../middleware/auth.js';
import { Errors } from '../../utils/errors.js';
import { parseInput } from '../../utils/validation.js';
import { SEND_MESSAGE_RATE_LIMIT } from '../messages/routes.js';

export function partiesRoutes(ctx: AppContext): FastifyPluginAsync {
  return async (app) => {
    app.addHook('preHandler', requireAuth(ctx));

    const conversationFor = async (userId: string, partyId: string): Promise<string> => {
      const party = await ctx.parties.get(userId, partyId);
      if (!party.conversationId) throw Errors.notFound('The party chat');
      return party.conversationId;
    };

    app.get('/parties/current', async (request) => ({ party: await ctx.parties.current(authOf(request).userId) }));

    app.get('/parties', async (request) => ({ parties: await ctx.parties.listJoinable(authOf(request).userId) }));

    app.post('/parties', async (request, reply) => {
      const input = parseInput(createPartySchema, request.body);
      return reply.status(201).send(await ctx.parties.create(authOf(request).userId, input));
    });

    app.get('/parties/:id', async (request) => {
      const { id } = parseInput(idParamsSchema, request.params);
      return ctx.parties.get(authOf(request).userId, id);
    });

    app.patch('/parties/:id', async (request) => {
      const { id } = parseInput(idParamsSchema, request.params);
      const input = parseInput(updatePartySchema, request.body);
      return ctx.parties.update(authOf(request).userId, id, input);
    });

    app.get('/parties/:id/members', async (request) => {
      const { id } = parseInput(idParamsSchema, request.params);
      return { members: (await ctx.parties.get(authOf(request).userId, id)).members };
    });

    app.post('/parties/:id/join', async (request) => {
      const { id } = parseInput(idParamsSchema, request.params);
      return ctx.parties.join(authOf(request).userId, id);
    });

    app.post('/parties/:id/leave', async (request, reply) => {
      const { id } = parseInput(idParamsSchema, request.params);
      await ctx.parties.leave(authOf(request).userId, id);
      return reply.status(204).send();
    });

    app.post('/parties/:id/close', async (request, reply) => {
      const { id } = parseInput(idParamsSchema, request.params);
      await ctx.parties.close(authOf(request).userId, id);
      return reply.status(204).send();
    });

    app.post('/parties/:id/leader', async (request) => {
      const { id } = parseInput(idParamsSchema, request.params);
      const { userId } = parseInput(transferLeaderSchema, request.body);
      return ctx.parties.transferLeader(authOf(request).userId, id, userId);
    });

    app.delete('/parties/:id/members/:userId', async (request, reply) => {
      const { id, userId } = parseInput(idAndUserParamsSchema, request.params);
      await ctx.parties.kick(authOf(request).userId, id, userId);
      return reply.status(204).send();
    });

    app.post('/parties/:id/invitations', async (request, reply) => {
      const { id } = parseInput(idParamsSchema, request.params);
      const { userId } = parseInput(userTargetSchema, request.body);
      return reply.status(201).send(await ctx.invitations.create('party', id, authOf(request).userId, userId));
    });

    app.get('/parties/:id/messages', async (request) => {
      const { id } = parseInput(idParamsSchema, request.params);
      const query = parseInput(messagePageQuerySchema, request.query);
      const userId = authOf(request).userId;
      return ctx.messages.listMessages(userId, await conversationFor(userId, id), query);
    });

    app.post('/parties/:id/messages', { config: { rateLimit: SEND_MESSAGE_RATE_LIMIT } }, async (request, reply) => {
      const { id } = parseInput(idParamsSchema, request.params);
      const input = parseInput(sendMessageSchema, request.body);
      const userId = authOf(request).userId;
      const result = await ctx.messages.send(userId, await conversationFor(userId, id), input);
      return reply.status(result.created ? 201 : 200).send(result);
    });
  };
}
