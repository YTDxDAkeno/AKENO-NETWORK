import type { FastifyInstance, FastifyRequest } from 'fastify';
import { API_PREFIX } from '@akeno/shared';
import type { AppContext } from '../app/context.js';
import { AppError } from '../utils/errors.js';

const SAFE_METHODS = new Set(['GET', 'HEAD', 'OPTIONS']);

/** True when the request's Origin (or, as a fallback, Referer) is one of the configured origins. */
export function isAllowedOrigin(request: FastifyRequest, allowedOrigins: string[]): boolean {
  const origin = request.headers.origin;
  if (typeof origin === 'string') return allowedOrigins.includes(origin);
  const referer = request.headers.referer;
  if (typeof referer === 'string') {
    try {
      return allowedOrigins.includes(new URL(referer).origin);
    } catch {
      return false;
    }
  }
  return false;
}

/**
 * Device pairing endpoints are called by consoles and other non-browser clients, which send no
 * Origin header. They neither read nor set cookies, so they are not exposed to CSRF.
 */
const NON_BROWSER_PATHS = new Set([`${API_PREFIX}/devices/pairings`, `${API_PREFIX}/devices/pairings/token`]);

/**
 * CSRF defense in depth: every state-changing API request must come from an allowed origin.
 * This also protects unauthenticated endpoints (login, registration) against login CSRF.
 * Requests authenticated with a bearer token are exempt: cookies are ignored on them, and
 * browsers never attach such a header cross-origin without a CORS preflight, which is refused.
 */
export function registerOriginCheck(app: FastifyInstance, ctx: AppContext): void {
  app.addHook('onRequest', async (request) => {
    if (SAFE_METHODS.has(request.method) || !request.url.startsWith('/api/')) return;
    if (request.usesBearer) return;
    if (NON_BROWSER_PATHS.has(request.url.split('?')[0] ?? '')) return;
    if (!isAllowedOrigin(request, ctx.config.allowedOrigins)) {
      throw new AppError(403, 'ORIGIN_REJECTED', 'This request was blocked because it did not come from the AKENO NETWORK app.');
    }
  });
}
