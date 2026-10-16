import type { FastifyPluginAsync } from 'fastify';
import {
  SHAREPLAY_INPUT_PATH,
  API_PREFIX,
  idParamsSchema,
  sharePlayGuestSchema,
  sharePlayInputQuerySchema,
  sharePlayQualitySchema,
  startSharePlaySchema,
} from '@akeno/shared';
import type { AppContext } from '../../app/context.js';
import { authOf, requireAuth, requireDevice, requireSession } from '../../middleware/auth.js';
import { isAllowedOrigin } from '../../middleware/origin.js';
import { AppError, Errors } from '../../utils/errors.js';
import { parseInput } from '../../utils/validation.js';

const SHAREPLAY_ACTION_LIMIT = { rateLimit: { max: 30, timeWindow: '1 minute' } };

export function sharePlayRoutes(ctx: AppContext): FastifyPluginAsync {
  return async (app) => {
    // Read access: website sessions, and paired devices with "parties:read" (the console app
    // shows SharePlay status read-only; see devices/scopes.ts).
    await app.register(async (read) => {
      read.addHook('preHandler', requireAuth(ctx));
      read.get('/shareplay/status', async () => ctx.shareplay.status());
      read.get('/parties/:id/shareplay', async (request) => {
        const { id } = parseInput(idParamsSchema, request.params);
        return ctx.shareplay.partyView(authOf(request).userId, id);
      });
    });

    // A paired console app opts in to receive Player 2 input for its diagnostic view.
    await app.register(async (device) => {
      device.addHook('preHandler', requireDevice());
      device.post('/parties/:id/shareplay/receiver', { config: SHAREPLAY_ACTION_LIMIT }, async (request) => {
        const { id } = parseInput(idParamsSchema, request.params);
        const auth = authOf(request);
        return ctx.shareplay.registerReceiver(auth.userId, auth.sessionId, id);
      });
    });

    // Everything that starts, joins or controls anything needs a signed-in browser session.
    await app.register(async (session) => {
      session.addHook('preHandler', requireSession(ctx));
      session.post('/parties/:id/shareplay/start', { config: SHAREPLAY_ACTION_LIMIT }, async (request, reply) => {
        const { id } = parseInput(idParamsSchema, request.params);
        const input = parseInput(startSharePlaySchema, request.body);
        const auth = authOf(request);
        return reply.status(201).send(await ctx.shareplay.start(auth.userId, id, input.source, input.quality, auth.sessionId));
      });
      session.post('/parties/:id/shareplay/stop', { config: SHAREPLAY_ACTION_LIMIT }, async (request, reply) => {
        const { id } = parseInput(idParamsSchema, request.params);
        await ctx.shareplay.stop(authOf(request).userId, id);
        return reply.status(204).send();
      });
      session.post('/parties/:id/shareplay/quality', { config: SHAREPLAY_ACTION_LIMIT }, async (request) => {
        const { id } = parseInput(idParamsSchema, request.params);
        const { quality } = parseInput(sharePlayQualitySchema, request.body);
        return ctx.shareplay.setQuality(authOf(request).userId, id, quality);
      });
      session.post('/parties/:id/shareplay/token', { config: { rateLimit: { max: 20, timeWindow: '1 minute' } } }, async (request) => {
        const { id } = parseInput(idParamsSchema, request.params);
        const auth = authOf(request);
        return ctx.shareplay.issueToken(auth.userId, id, auth.sessionId);
      });
      session.post('/parties/:id/shareplay/controller/request', { config: SHAREPLAY_ACTION_LIMIT }, async (request, reply) => {
        const { id } = parseInput(idParamsSchema, request.params);
        await ctx.shareplay.requestController(authOf(request).userId, id);
        return reply.status(204).send();
      });
      session.post('/parties/:id/shareplay/controller/cancel', { config: SHAREPLAY_ACTION_LIMIT }, async (request, reply) => {
        const { id } = parseInput(idParamsSchema, request.params);
        await ctx.shareplay.cancelRequest(authOf(request).userId, id);
        return reply.status(204).send();
      });
      session.post('/parties/:id/shareplay/controller/approve', { config: SHAREPLAY_ACTION_LIMIT }, async (request) => {
        const { id } = parseInput(idParamsSchema, request.params);
        const { userId } = parseInput(sharePlayGuestSchema, request.body);
        return ctx.shareplay.approveController(authOf(request).userId, id, userId);
      });
      session.post('/parties/:id/shareplay/controller/deny', { config: SHAREPLAY_ACTION_LIMIT }, async (request, reply) => {
        const { id } = parseInput(idParamsSchema, request.params);
        const { userId } = parseInput(sharePlayGuestSchema, request.body);
        await ctx.shareplay.denyController(authOf(request).userId, id, userId);
        return reply.status(204).send();
      });
      session.post('/parties/:id/shareplay/controller/revoke', { config: SHAREPLAY_ACTION_LIMIT }, async (request, reply) => {
        const { id } = parseInput(idParamsSchema, request.params);
        await ctx.shareplay.revokeController(authOf(request).userId, id);
        return reply.status(204).send();
      });
    });

    // Player 2 controller link (sender: the granted guest) and the host's input monitor.
    // Browser sessions only; authorization is decided before the upgrade, from server state.
    app.get(
      SHAREPLAY_INPUT_PATH.slice(API_PREFIX.length),
      {
        websocket: true,
        config: { rateLimit: { max: 30, timeWindow: '1 minute' } },
        preValidation: async (request) => {
          if (request.usesBearer || !isAllowedOrigin(request, ctx.config.allowedOrigins)) {
            throw new AppError(403, 'ORIGIN_REJECTED', 'The controller link is only available in the AKENO NETWORK app.');
          }
          const auth = request.auth;
          if (!auth || auth.credential !== 'session') throw Errors.unauthenticated();
          const query = parseInput(sharePlayInputQuerySchema, request.query);
          if (!ctx.shareplay.remoteInputEnabled) throw new AppError(503, 'REMOTE_INPUT_DISABLED', 'Remote controller input is switched off on this server.');
          if ((await ctx.parties.currentPartyId(auth.userId)) !== query.party) throw new AppError(403, 'NOT_MEMBER', 'You are not in this party.');
          const session = ctx.shareplay.sessionOf(query.party);
          if (!session) throw new AppError(404, 'NO_SHAREPLAY', 'Nobody is streaming in this party right now.');
          if (query.role === 'monitor') {
            if (session.hostId !== auth.userId) throw Errors.forbidden('Only the person streaming can monitor Player 2.');
            return;
          }
          const grant = session.grant;
          if (!grant || grant.guestId !== auth.userId) throw Errors.forbidden('You do not have Player 2 access.');
          if (query.epoch !== grant.epoch) throw new AppError(409, 'GRANT_CHANGED', 'This Player 2 access is no longer current.');
        },
      },
      (socket, request) => {
        const auth = request.auth!;
        const query = parseInput(sharePlayInputQuerySchema, request.query);
        const session = ctx.shareplay.sessionOf(query.party);
        // Re-check after the upgrade: the grant may have ended in between.
        if (query.role === 'monitor') {
          if (session?.hostId !== auth.userId) return socket.close(4403, 'Not the host');
          ctx.shareplay.relay.attachMonitor(socket, query.party, auth.userId);
          return;
        }
        const grant = session?.grant;
        if (!grant || grant.guestId !== auth.userId || grant.epoch !== query.epoch) return socket.close(4410, 'Player 2 access has ended.');
        ctx.shareplay.relay.attachSender(socket, query.party, auth.userId, auth.sessionId, grant);
      },
    );
  };
}
