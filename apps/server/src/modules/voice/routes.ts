import type { FastifyPluginAsync } from 'fastify';
import { idParamsSchema } from '@akeno/shared';
import type { AppContext } from '../../app/context.js';
import { authOf, requireAuth, requireDevice, requireSession } from '../../middleware/auth.js';
import { parseInput } from '../../utils/validation.js';

export const VOICE_WEBHOOK_PATH = '/voice/webhook';

export function voiceRoutes(ctx: AppContext): FastifyPluginAsync {
  return async (app) => {
    // Website sessions only: paired devices have no voice permission in this release.
    await app.register(async (session) => {
      session.addHook('preHandler', requireSession(ctx));
      session.get('/voice/status', async () => ctx.voice.status());
      session.post(
        '/parties/:id/voice/token',
        { config: { rateLimit: { max: 20, timeWindow: '1 minute' } } },
        async (request) => {
          const { id } = parseInput(idParamsSchema, request.params);
          const auth = authOf(request);
          return ctx.voice.issueToken(auth.userId, id, auth.sessionId);
        },
      );
    });

    // Paired consoles: a voice pass for the AKENO voice gateway (apps/voice-gateway). The device
    // route map allows it with the "parties:write" permission; browsers use /voice/token.
    await app.register(async (device) => {
      device.addHook('preHandler', requireAuth(ctx));
      device.addHook('preHandler', requireDevice());
      device.post(
        '/parties/:id/voice/device-token',
        { config: { rateLimit: { max: 20, timeWindow: '1 minute' } } },
        async (request) => {
          const { id } = parseInput(idParamsSchema, request.params);
          const auth = authOf(request);
          return ctx.voice.issueDeviceToken(auth.userId, id, auth.sessionId);
        },
      );
    });

    // Called by LiveKit (server to server). The request is authenticated by its signature, which
    // covers the raw body, so the body is kept as a string for verification.
    await app.register(async (webhook) => {
      webhook.removeContentTypeParser('application/json');
      webhook.addContentTypeParser(['application/webhook+json', 'application/json'], { parseAs: 'string', bodyLimit: 64 * 1024 }, (_request, body, done) => {
        done(null, body);
      });
      webhook.post(VOICE_WEBHOOK_PATH, { config: { rateLimit: { max: 1200, timeWindow: '1 minute' } } }, async (request, reply) => {
        const authorization = request.headers.authorization;
        await ctx.voice.handleWebhook(typeof request.body === 'string' ? request.body : '', typeof authorization === 'string' ? authorization : undefined);
        return reply.status(204).send();
      });
    });
  };
}
