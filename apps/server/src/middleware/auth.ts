import type { FastifyInstance, FastifyReply, FastifyRequest } from 'fastify';
import { CSRF_HEADER } from '@akeno/shared';
import type { AppContext } from '../app/context.js';
import type { AuthContextData, IssuedSession, RequestMeta } from '../modules/auth/service.js';
import { deviceRouteDecision } from '../modules/devices/scopes.js';
import { AppError, Errors } from '../utils/errors.js';

declare module 'fastify' {
  interface FastifyRequest {
    auth: AuthContextData | null;
    sessionToken: string | null;
    /** True when the request carries an `Authorization: Bearer` header (paired devices). */
    usesBearer: boolean;
  }
}

const BEARER_PATTERN = /^Bearer[ ]+(\S+)$/i;

const SAFE_METHODS = new Set(['GET', 'HEAD', 'OPTIONS']);

function needsSession(url: string): boolean {
  return url.startsWith('/api/') || url === '/ws' || url.startsWith('/ws?');
}

/**
 * Resolves the identity on API and WebSocket requests: a paired-device bearer token when an
 * `Authorization: Bearer` header is present (cookies are then ignored entirely), otherwise the
 * browser session cookie. Device requests are then restricted to the routes their scopes allow.
 */
export function registerAuthentication(app: FastifyInstance, ctx: AppContext): void {
  app.decorateRequest('auth', null);
  app.decorateRequest('sessionToken', null);
  app.decorateRequest('usesBearer', false);
  app.addHook('onRequest', async (request) => {
    if (!needsSession(request.url)) return;
    const authorization = request.headers.authorization;
    if (typeof authorization === 'string' && /^Bearer\b/i.test(authorization)) {
      request.usesBearer = true;
      const match = BEARER_PATTERN.exec(authorization.trim());
      const auth = match?.[1] ? await ctx.devices.authenticate(match[1]) : null;
      if (auth) request.auth = auth;
      return;
    }
    const token = request.cookies[ctx.config.session.cookieName];
    if (!token) return;
    const auth = await ctx.auth.authenticate(token);
    if (auth) {
      request.auth = auth;
      request.sessionToken = token;
    }
  });

  // Default deny for devices: only routes mapped to a granted scope may be called. This runs
  // at preValidation (before every route guard) because @fastify/websocket only tears down a
  // refused upgrade cleanly when it is rejected at that stage or later.
  app.addHook('preValidation', async (request) => {
    const auth = request.auth;
    if (auth?.credential !== 'device' || !auth.scopes) return;
    const routeUrl = request.routeOptions.url;
    if (!routeUrl) return; // Unknown route: the not-found handler answers.
    const decision = deviceRouteDecision(request.method, routeUrl, auth.scopes);
    if (!decision.allowed) {
      throw new AppError(
        403,
        'DEVICE_SCOPE_DENIED',
        decision.scope
          ? `This device was not granted the "${decision.scope}" permission. Pair it again to change permissions.`
          : 'Paired devices cannot use this feature. Use the AKENO NETWORK website instead.',
      );
    }
  });
}

/**
 * Route guard: requires a valid session and, for state-changing methods, a matching CSRF token
 * in the X-CSRF-Token header.
 */
export function requireAuth(ctx: AppContext) {
  return async (request: FastifyRequest): Promise<void> => {
    if (!request.auth) throw Errors.unauthenticated();
    // Bearer tokens are never sent automatically by browsers, so they need no CSRF token.
    if (request.auth.credential === 'device') return;
    if (!request.sessionToken) throw Errors.unauthenticated();
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
  const authenticated = requireSession(ctx);
  return async (request: FastifyRequest): Promise<void> => {
    await authenticated(request);
    if (request.auth?.role !== 'admin') throw Errors.forbidden('Administrator access is required.');
  };
}

/** Like requireAuth, but only a signed-in browser session qualifies (never a device token). */
export function requireSession(ctx: AppContext) {
  const authenticated = requireAuth(ctx);
  return async (request: FastifyRequest): Promise<void> => {
    await authenticated(request);
    if (request.auth?.credential !== 'session') {
      throw new AppError(403, 'DEVICE_SCOPE_DENIED', 'Paired devices cannot use this feature. Use the AKENO NETWORK website instead.');
    }
  };
}

/** Requires a paired-device bearer token. */
export function requireDevice() {
  return async (request: FastifyRequest): Promise<void> => {
    if (!request.auth) throw Errors.unauthenticated('This device is not paired or was unpaired. Pair it again.');
    if (request.auth.credential !== 'device') throw Errors.badRequest('This endpoint is only for paired devices.');
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
