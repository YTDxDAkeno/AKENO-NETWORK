import { createReadStream } from 'node:fs';
import { stat } from 'node:fs/promises';
import type { FastifyInstance, FastifyPluginAsync, FastifyRequest } from 'fastify';
import { z } from 'zod';
import { MEDIA_LIMITS, MEDIA_PATH_PREFIX } from '@akeno/shared';
import type { AppContext } from '../../app/context.js';
import { requireAuth } from '../../middleware/auth.js';
import { AppError, Errors } from '../../utils/errors.js';
import { parseInput } from '../../utils/validation.js';
import { FILE_CONTENT_TYPES } from './service.js';

const mediaParams = z.object({ id: z.string().max(64), file: z.string().max(32) });

/** Content types accepted on upload routes. The bytes are still sniffed: these are only allowed wrappers. */
const UPLOAD_CONTENT_TYPES = [...MEDIA_LIMITS.acceptedTypes, 'application/octet-stream'];

/**
 * Serves stored image variants to signed-in users and paired devices.
 *
 * Media IDs are 128-bit random values that are only disclosed through API responses which
 * already apply profile privacy and blocks. Responses are immutable (a replaced image gets a new
 * ID), so browsers cache them for a year; `private` keeps shared caches from storing them.
 */
export function mediaRoutes(ctx: AppContext): FastifyPluginAsync {
  return async (app) => {
    const authenticated = requireAuth(ctx);
    app.get(
      `${MEDIA_PATH_PREFIX}/:id/:file`,
      { config: { rateLimit: { max: 3000, timeWindow: '1 minute' } } },
      async (request, reply) => {
        await authenticated(request);
        const { id, file } = parseInput(mediaParams, request.params);
        const path = ctx.media.filePath(id, file);
        if (!path) throw Errors.notFound('The image');
        const etag = `"${id}-${file}"`;
        if (request.headers['if-none-match'] === etag) {
          return reply.status(304).header('ETag', etag).header('Cache-Control', 'private, max-age=31536000, immutable').send();
        }
        let size: number;
        try {
          const info = await stat(path);
          if (!info.isFile()) throw new Error('not a file');
          size = info.size;
        } catch {
          throw Errors.notFound('The image');
        }
        return reply
          .header('Content-Type', FILE_CONTENT_TYPES[file] ?? 'application/octet-stream')
          .header('Content-Length', size)
          .header('Content-Disposition', 'inline')
          .header('Cache-Control', 'private, max-age=31536000, immutable')
          .header('ETag', etag)
          .send(createReadStream(path));
      },
    );
  };
}

/**
 * Registers the raw-body parser for image uploads in the calling plugin scope only, so no other
 * route accepts large bodies. Unauthenticated requests are refused before the body is read.
 */
export function acceptImageUploads(app: FastifyInstance, options: { adminOnly?: boolean } = {}): void {
  app.addContentTypeParser(UPLOAD_CONTENT_TYPES, { parseAs: 'buffer', bodyLimit: MEDIA_LIMITS.uploadMaxBytes }, (_request, body, done) => {
    done(null, body);
  });
  app.addHook('onRequest', async (request: FastifyRequest) => {
    if (!request.auth) throw Errors.unauthenticated();
    if (request.auth.credential !== 'session') throw Errors.forbidden('Images can only be uploaded from the AKENO NETWORK website.');
    if (options.adminOnly && request.auth.role !== 'admin') throw Errors.forbidden('Administrator access is required.');
  });
}

/** The uploaded bytes of an image route (validated by the content type parser above). */
export function uploadedImage(request: FastifyRequest): Buffer {
  if (request.body !== undefined && !Buffer.isBuffer(request.body)) {
    throw new AppError(415, 'UNSUPPORTED_MEDIA_TYPE', 'Upload the image file itself (JPEG, PNG, WebP or GIF).');
  }
  if (!Buffer.isBuffer(request.body) || request.body.length === 0) {
    throw Errors.badRequest('Send the image file as the request body.');
  }
  return request.body;
}

/** Upload limit: generous for retries, tight enough to stop abuse. */
export const UPLOAD_RATE_LIMIT = { max: 12, timeWindow: '10 minutes' } as const;
