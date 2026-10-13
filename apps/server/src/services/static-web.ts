import { existsSync } from 'node:fs';
import { join, resolve, sep } from 'node:path';
import fastifyStatic from '@fastify/static';
import type { FastifyInstance } from 'fastify';

/**
 * Serves the built web client from the same origin as the API, which keeps cookies,
 * CSRF protection and WebSocket origin checks simple. Hashed assets are cached for a year;
 * index.html is always revalidated so deployments take effect immediately.
 */
export async function registerStaticWeb(app: FastifyInstance, directory: string): Promise<boolean> {
  const root = resolve(directory);
  if (!existsSync(join(root, 'index.html'))) {
    app.log.warn({ directory: root }, 'WEB_DIST_DIR does not contain index.html; the web client will not be served');
    return false;
  }
  await app.register(fastifyStatic, {
    root,
    prefix: '/',
    wildcard: false,
    setHeaders(res, filePath) {
      if (filePath.includes(`${sep}assets${sep}`)) {
        res.header('Cache-Control', 'public, max-age=31536000, immutable');
      } else {
        res.header('Cache-Control', 'no-cache');
      }
    },
  });
  return true;
}

/** Paths that must never fall back to the single-page app. */
export function isApiLikePath(url: string): boolean {
  return url.startsWith('/api/') || url === '/api' || url.startsWith('/health') || url === '/ws' || url.startsWith('/ws?');
}
