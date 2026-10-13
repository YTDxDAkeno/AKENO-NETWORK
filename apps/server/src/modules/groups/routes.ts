import type { FastifyPluginAsync } from 'fastify';
import {
  createGroupSchema,
  idAndUserParamsSchema,
  idParamsSchema,
  messagePageQuerySchema,
  sendMessageSchema,
  transferOwnershipSchema,
  updateGroupMemberSchema,
  updateGroupSchema,
  userTargetSchema,
} from '@akeno/shared';
import type { AppContext } from '../../app/context.js';
import { authOf, requireAuth } from '../../middleware/auth.js';
import { Errors } from '../../utils/errors.js';
import { parseInput } from '../../utils/validation.js';
import { SEND_MESSAGE_RATE_LIMIT } from '../messages/routes.js';

export function groupsRoutes(ctx: AppContext): FastifyPluginAsync {
  return async (app) => {
    app.addHook('preHandler', requireAuth(ctx));

    /** Resolves the group conversation for members only. */
    const conversationFor = async (userId: string, groupId: string): Promise<string> => {
      const group = await ctx.groups.get(userId, groupId);
      if (!group.conversationId) throw Errors.notFound('The group chat');
      return group.conversationId;
    };

    app.get('/groups', async (request) => ({ groups: await ctx.groups.listMine(authOf(request).userId) }));

    app.get('/groups/discover', async (request) => ({ groups: await ctx.groups.discover(authOf(request).userId) }));

    app.post('/groups', async (request, reply) => {
      const input = parseInput(createGroupSchema, request.body);
      return reply.status(201).send(await ctx.groups.create(authOf(request).userId, input));
    });

    app.get('/groups/:id', async (request) => {
      const { id } = parseInput(idParamsSchema, request.params);
      return ctx.groups.get(authOf(request).userId, id);
    });

    app.patch('/groups/:id', async (request) => {
      const { id } = parseInput(idParamsSchema, request.params);
      const input = parseInput(updateGroupSchema, request.body);
      return ctx.groups.update(authOf(request).userId, id, input);
    });

    app.delete('/groups/:id', async (request, reply) => {
      const { id } = parseInput(idParamsSchema, request.params);
      await ctx.groups.delete(authOf(request).userId, id);
      return reply.status(204).send();
    });

    app.get('/groups/:id/members', async (request) => {
      const { id } = parseInput(idParamsSchema, request.params);
      return { members: (await ctx.groups.get(authOf(request).userId, id)).members };
    });

    app.post('/groups/:id/join', async (request) => {
      const { id } = parseInput(idParamsSchema, request.params);
      return ctx.groups.join(authOf(request).userId, id);
    });

    app.post('/groups/:id/leave', async (request) => {
      const { id } = parseInput(idParamsSchema, request.params);
      return ctx.groups.leave(authOf(request).userId, id);
    });

    app.delete('/groups/:id/members/:userId', async (request, reply) => {
      const { id, userId } = parseInput(idAndUserParamsSchema, request.params);
      await ctx.groups.removeMember(authOf(request).userId, id, userId);
      return reply.status(204).send();
    });

    app.patch('/groups/:id/members/:userId', async (request) => {
      const { id, userId } = parseInput(idAndUserParamsSchema, request.params);
      const { role } = parseInput(updateGroupMemberSchema, request.body);
      await ctx.groups.setRole(authOf(request).userId, id, userId, role);
      return ctx.groups.get(authOf(request).userId, id);
    });

    app.post('/groups/:id/transfer-ownership', async (request) => {
      const { id } = parseInput(idParamsSchema, request.params);
      const { userId } = parseInput(transferOwnershipSchema, request.body);
      await ctx.groups.transferOwnership(authOf(request).userId, id, userId);
      return ctx.groups.get(authOf(request).userId, id);
    });

    app.get('/groups/:id/invitations', async (request) => {
      const { id } = parseInput(idParamsSchema, request.params);
      return { invitations: (await ctx.groups.get(authOf(request).userId, id)).pendingInvitations };
    });

    app.post('/groups/:id/invitations', async (request, reply) => {
      const { id } = parseInput(idParamsSchema, request.params);
      const { userId } = parseInput(userTargetSchema, request.body);
      return reply.status(201).send(await ctx.invitations.create('group', id, authOf(request).userId, userId));
    });

    app.get('/groups/:id/messages', async (request) => {
      const { id } = parseInput(idParamsSchema, request.params);
      const query = parseInput(messagePageQuerySchema, request.query);
      const userId = authOf(request).userId;
      return ctx.messages.listMessages(userId, await conversationFor(userId, id), query);
    });

    app.post('/groups/:id/messages', { config: { rateLimit: SEND_MESSAGE_RATE_LIMIT } }, async (request, reply) => {
      const { id } = parseInput(idParamsSchema, request.params);
      const input = parseInput(sendMessageSchema, request.body);
      const userId = authOf(request).userId;
      const result = await ctx.messages.send(userId, await conversationFor(userId, id), input);
      return reply.status(result.created ? 201 : 200).send(result);
    });
  };
}
