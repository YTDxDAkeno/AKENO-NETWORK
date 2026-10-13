import type { FastifyInstance, FastifyReply, FastifyRequest } from 'fastify';
import { CSRF_HEADER } from '@akeno/shared';
import type { AppContext } from '../app/context.js';
import type { AuthContextData, IssuedSession, RequestMeta } from '../modules/auth/service.js';
import { AppError, Errors } from '../utils/errors.js';

declare module 'fastify' {
  interface FastifyRequest {
    auth: AuthContextData | null;
    sessionToken: string | null;
  }
}

const SAFE_METHODS = new Set(['GET', 'HEAD', 'OPTIONS']);

function needsSession(url: string): boolean {
  return url.startsWith('/api/') || url === '/ws' || url.startsWith('/ws?');
}

/** Resolves the session cookie on API and WebSocket requests and attaches the identity. */
export function registerAuthentication(app: FastifyInstance, ctx: AppContext): void {
  app.decorateRequest('auth', null);
  app.decorateRequest('sessionToken', null);
  app.addHook('onRequest', async (request) => {
    if (!needsSession(request.url)) return;
    const token = request.cookies[ctx.config.session.cookieName];
    if (!token) return;
    const auth = await ctx.auth.authenticate(token);
    if (auth) {
      request.auth = auth;
      request.sessionToken = token;
    }
  });
}

/**
 * Route guard: requires a valid session and, for state-changing methods, a matching CSRF token
 * in the X-CSRF-Token header.
 */
export function requireAuth(ctx: AppContext) {
  return async (request: FastifyRequest): Promise<void> => {
    if (!request.auth || !request.sessionToken) throw Errors.unauthenticated();
    if (!SAFE_METHODS.has(request.method)) {
      const header = request.headers[CSRF_HEADER];
      const provided = typeof header === 'string' ? header : undefined;
      if (!ctx.auth.verifyCsrf(request.sessionToken, provided)) {
        throw new AppError(403, 'CSRF_FAILED', 'Your security token is missing or out of date. Reload the page and try again.');
      }
    }
  };
}

export function requireAdmin(ctx: AppContext) {
  const authenticated = requireAuth(ctx);
  return async (request: FastifyRequest): Promise<void> => {
    await authenticated(request);
    if (request.auth?.role !== 'admin') throw Errors.forbidden('Administrator access is required.');
  };
}

/** Returns the authenticated identity; only call from routes guarded by requireAuth. */
export function authOf(request: FastifyRequest): AuthContextData {
  if (!request.auth) throw Errors.unauthenticated();
  return request.auth;
}

export function requestMeta(request: FastifyRequest): RequestMeta {
  const agent = request.headers['user-agent'];
  return { ip: request.ip, userAgent: typeof agent === 'string' ? agent : null };
}

export function setSessionCookie(reply: FastifyReply, ctx: AppContext, session: IssuedSession): void {
  reply.setCookie(ctx.config.session.cookieName, session.rawToken, {
    httpOnly: true,
    secure: ctx.config.session.cookieSecure,
    sameSite: 'lax',
    path: '/',
    expires: session.expiresAt,
  });
}

export function clearSessionCookie(reply: FastifyReply, ctx: AppContext): void {
  reply.clearCookie(ctx.config.session.cookieName, {
    httpOnly: true,
    secure: ctx.config.session.cookieSecure,
    sameSite: 'lax',
    path: '/',
  });
}
