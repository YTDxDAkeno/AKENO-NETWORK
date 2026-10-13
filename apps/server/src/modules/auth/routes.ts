import type { FastifyPluginAsync } from 'fastify';
import {
  LIMITS,
  changePasswordSchema,
  idParamsSchema,
  loginSchema,
  registerSchema,
  type AuthConfigResponse,
  type AuthSessionResponse,
} from '@akeno/shared';
import type { AppContext } from '../../app/context.js';
import {
  authOf,
  clearSessionCookie,
  requestMeta,
  requireSession,
  setSessionCookie,
} from '../../middleware/auth.js';
import { Errors } from '../../utils/errors.js';
import { parseInput } from '../../utils/validation.js';
import type { IssuedSession } from './service.js';

export function authRoutes(ctx: AppContext): FastifyPluginAsync {
  return async (app) => {
    // Session management is only available to browser sessions, never to paired devices.
    const auth = requireSession(ctx);

    const sessionResponse = async (session: IssuedSession): Promise<AuthSessionResponse> => ({
      user: await ctx.users.getCurrentUser(session.userId),
      csrfToken: ctx.auth.csrfTokenFor(session.rawToken),
      session: { id: session.sessionId, expiresAt: session.expiresAt.toISOString() },
    });

    app.get('/auth/config', async (): Promise<AuthConfigResponse> => ({
      registrationMode: ctx.config.registrationMode,
      passwordMinLength: LIMITS.passwordMin,
    }));

    app.post(
      '/auth/register',
      { config: { rateLimit: { max: 10, timeWindow: '15 minutes' } } },
      async (request, reply) => {
        const input = parseInput(registerSchema, request.body);
        const session = await ctx.auth.register(input, requestMeta(request));
        setSessionCookie(reply, ctx, session);
        return reply.status(201).send(await sessionResponse(session));
      },
    );

    app.post(
      '/auth/login',
      { config: { rateLimit: { max: 20, timeWindow: '1 minute' } } },
      async (request, reply) => {
        const input = parseInput(loginSchema, request.body);
        const session = await ctx.auth.login(input, requestMeta(request));
        setSessionCookie(reply, ctx, session);
        return reply.send(await sessionResponse(session));
      },
    );

    app.post('/auth/logout', { preHandler: auth }, async (request, reply) => {
      await ctx.auth.logout(authOf(request), requestMeta(request));
      clearSessionCookie(reply, ctx);
      return reply.status(204).send();
    });

    app.get('/auth/session', async (request, reply): Promise<AuthSessionResponse> => {
      if (!request.auth) {
        // Remove stale cookies so the browser stops sending them.
        if (request.cookies[ctx.config.session.cookieName]) clearSessionCookie(reply, ctx);
        throw Errors.unauthenticated();
      }
      return {
        user: await ctx.users.getCurrentUser(request.auth.userId),
        csrfToken: request.auth.csrfToken,
        session: { id: request.auth.sessionId, expiresAt: request.auth.sessionExpiresAt.toISOString() },
      };
    });

    app.get('/auth/sessions', { preHandler: auth }, async (request) => {
      const identity = authOf(request);
      return { sessions: await ctx.auth.listSessions(identity.userId, identity.sessionId) };
    });

    app.delete('/auth/sessions/:id', { preHandler: auth }, async (request, reply) => {
      const { id } = parseInput(idParamsSchema, request.params);
      const identity = authOf(request);
      await ctx.auth.revokeSession(identity.userId, id, requestMeta(request));
      if (id === identity.sessionId) clearSessionCookie(reply, ctx);
      return reply.status(204).send();
    });

    app.post('/auth/sessions/revoke-others', { preHandler: auth }, async (request) => {
      const identity = authOf(request);
      const revoked = await ctx.auth.revokeOtherSessions(identity.userId, identity.sessionId, requestMeta(request));
      return { revoked };
    });

    app.post(
      '/auth/password',
      { preHandler: auth, config: { rateLimit: { max: 10, timeWindow: '15 minutes' } } },
      async (request, reply) => {
        const input = parseInput(changePasswordSchema, request.body);
        await ctx.auth.changePassword(authOf(request), input, requestMeta(request));
        return reply.status(204).send();
      },
    );
  };
}
