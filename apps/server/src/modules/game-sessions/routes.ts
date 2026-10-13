import type { FastifyPluginAsync } from 'fastify';
import { z } from 'zod';
import {
  createGameSessionSchema,
  gameSessionListQuerySchema,
  idAndUserParamsSchema,
  idParamsSchema,
  joinGameSessionSchema,
  updateGameSessionSchema,
  userTargetSchema,
  uuidSchema,
} from '@akeno/shared';
import type { AppContext } from '../../app/context.js';
import { authOf, requireAuth } from '../../middleware/auth.js';
import { parseInput } from '../../utils/validation.js';

const requestParams = z.object({ id: uuidSchema, requestId: uuidSchema });

export function gameSessionsRoutes(ctx: AppContext): FastifyPluginAsync {
  return async (app) => {
    app.addHook('preHandler', requireAuth(ctx));

    app.get('/game-sessions', async (request) => {
      const query = parseInput(gameSessionListQuerySchema, request.query);
      return { sessions: await ctx.gameSessions.list(authOf(request).userId, query) };
    });

    app.get('/game-sessions/mine', async (request) => ({
      sessions: await ctx.gameSessions.listMine(authOf(request).userId),
    }));

    app.post('/game-sessions', async (request, reply) => {
      const input = parseInput(createGameSessionSchema, request.body);
      return reply.status(201).send(await ctx.gameSessions.create(authOf(request).userId, input));
    });

    app.get('/game-sessions/:id', async (request) => {
      const { id } = parseInput(idParamsSchema, request.params);
      return ctx.gameSessions.get(authOf(request).userId, id);
    });

    app.patch('/game-sessions/:id', async (request) => {
      const { id } = parseInput(idParamsSchema, request.params);
      const input = parseInput(updateGameSessionSchema, request.body);
      return ctx.gameSessions.update(authOf(request).userId, id, input);
    });

    app.post('/game-sessions/:id/join', async (request) => {
      const { id } = parseInput(idParamsSchema, request.params);
      const { message } = parseInput(joinGameSessionSchema, request.body ?? {});
      return ctx.gameSessions.join(authOf(request).userId, id, message ?? '');
    });

    app.delete('/game-sessions/:id/request', async (request, reply) => {
      const { id } = parseInput(idParamsSchema, request.params);
      await ctx.gameSessions.cancelRequest(authOf(request).userId, id);
      return reply.status(204).send();
    });

    app.post('/game-sessions/:id/leave', async (request, reply) => {
      const { id } = parseInput(idParamsSchema, request.params);
      await ctx.gameSessions.leave(authOf(request).userId, id);
      return reply.status(204).send();
    });

    app.post('/game-sessions/:id/close', async (request) => {
      const { id } = parseInput(idParamsSchema, request.params);
      return ctx.gameSessions.close(authOf(request).userId, id);
    });

    app.post('/game-sessions/:id/requests/:requestId/accept', async (request) => {
      const { id, requestId } = parseInput(requestParams, request.params);
      return ctx.gameSessions.respondToRequest(authOf(request).userId, id, requestId, true);
    });

    app.post('/game-sessions/:id/requests/:requestId/reject', async (request) => {
      const { id, requestId } = parseInput(requestParams, request.params);
      return ctx.gameSessions.respondToRequest(authOf(request).userId, id, requestId, false);
    });

    app.delete('/game-sessions/:id/members/:userId', async (request, reply) => {
      const { id, userId } = parseInput(idAndUserParamsSchema, request.params);
      await ctx.gameSessions.kick(authOf(request).userId, id, userId);
      return reply.status(204).send();
    });

    app.post('/game-sessions/:id/invitations', async (request, reply) => {
      const { id } = parseInput(idParamsSchema, request.params);
      const { userId } = parseInput(userTargetSchema, request.body);
      return reply.status(201).send(await ctx.invitations.create('game_session', id, authOf(request).userId, userId));
    });
  };
}
