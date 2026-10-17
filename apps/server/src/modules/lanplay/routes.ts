import type { FastifyPluginAsync } from 'fastify';
import {
  LAN_PLAY_GAMES,
  createLanSessionSchema,
  idParamsSchema,
  joinLanSessionSchema,
  selectLanGameSchema,
} from '@akeno/shared';
import type { AppContext } from '../../app/context.js';
import { authOf, requireAuth, requireSession } from '../../middleware/auth.js';
import { parseInput } from '../../utils/validation.js';

const LAN_ACTION_LIMIT = { rateLimit: { max: 30, timeWindow: '1 minute' } };

/**
 * AKENO LAN Play (experimental) control-plane routes under /api/v1.
 *
 * Reading status and a party's session is allowed for website sessions and paired devices with
 * `parties:read` (the console app shows LAN Play read-only). Creating, joining, ending and
 * connecting need a signed-in website session. No route carries game traffic.
 */
export function lanPlayRoutes(ctx: AppContext): FastifyPluginAsync {
  return async (app) => {
    await app.register(async (read) => {
      read.addHook('preHandler', requireAuth(ctx));
      read.get('/lanplay/status', async () => ctx.lanplay.status());
      read.get('/lanplay/games', async () => ({ games: LAN_PLAY_GAMES }));
      read.get('/parties/:id/lanplay', async (request) => {
        const { id } = parseInput(idParamsSchema, request.params);
        return ctx.lanplay.partyView(authOf(request).userId, id);
      });
    });

    await app.register(async (session) => {
      session.addHook('preHandler', requireSession(ctx));

      session.post('/parties/:id/lanplay', { config: LAN_ACTION_LIMIT }, async (request, reply) => {
        const { id } = parseInput(idParamsSchema, request.params);
        const input = parseInput(createLanSessionSchema, request.body);
        const auth = authOf(request);
        return reply.status(201).send(await ctx.lanplay.create(auth.userId, id, input.platform, input.gameId ?? null, auth.sessionId));
      });

      session.post('/parties/:id/lanplay/join', { config: LAN_ACTION_LIMIT }, async (request) => {
        const { id } = parseInput(idParamsSchema, request.params);
        const input = parseInput(joinLanSessionSchema, request.body);
        const auth = authOf(request);
        return ctx.lanplay.join(auth.userId, id, input.platform, auth.sessionId);
      });

      session.post('/parties/:id/lanplay/game', { config: LAN_ACTION_LIMIT }, async (request) => {
        const { id } = parseInput(idParamsSchema, request.params);
        const { gameId } = parseInput(selectLanGameSchema, request.body);
        return ctx.lanplay.selectGame(authOf(request).userId, id, gameId);
      });

      session.post('/parties/:id/lanplay/connect', { config: LAN_ACTION_LIMIT }, async (request) => {
        const { id } = parseInput(idParamsSchema, request.params);
        const auth = authOf(request);
        return ctx.lanplay.connect(auth.userId, id, auth.sessionId);
      });

      session.post('/parties/:id/lanplay/leave', { config: LAN_ACTION_LIMIT }, async (request, reply) => {
        const { id } = parseInput(idParamsSchema, request.params);
        await ctx.lanplay.leave(authOf(request).userId, id);
        return reply.status(204).send();
      });

      session.delete('/parties/:id/lanplay', { config: LAN_ACTION_LIMIT }, async (request, reply) => {
        const { id } = parseInput(idParamsSchema, request.params);
        await ctx.lanplay.terminate(authOf(request).userId, id);
        return reply.status(204).send();
      });
    });
  };
}
