import type { FastifyInstance, FastifyRequest } from 'fastify';
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
 * CSRF defense in depth: every state-changing API request must come from an allowed origin.
 * This also protects unauthenticated endpoints (login, registration) against login CSRF.
 */
export function registerOriginCheck(app: FastifyInstance, ctx: AppContext): void {
  app.addHook('onRequest', async (request) => {
    if (SAFE_METHODS.has(request.method) || !request.url.startsWith('/api/')) return;
    if (!isAllowedOrigin(request, ctx.config.allowedOrigins)) {
      throw new AppError(403, 'ORIGIN_REJECTED', 'This request was blocked because it did not come from the AKENO NETWORK app.');
    }
  });
}
