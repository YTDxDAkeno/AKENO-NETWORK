import type { FastifyError, FastifyInstance } from 'fastify';
import type { ApiErrorBody, ErrorCode } from '@akeno/shared';
import { getPgError } from '../utils/db-errors.js';
import { isAppError } from '../utils/errors.js';

function body(code: ErrorCode, message: string, requestId: string, fields?: Record<string, string>): ApiErrorBody {
  return { error: { code, message, requestId, ...(fields ? { fields } : {}) } };
}

/** Maps every error to a safe, consistent JSON response. Internal details are only logged. */
export function registerErrorHandling(app: FastifyInstance): void {
  app.setErrorHandler((error: FastifyError, request, reply) => {
    if (isAppError(error)) {
      if (error.statusCode === 429) request.log.warn({ code: error.code }, 'Rate limited');
      return reply.status(error.statusCode).send(body(error.code, error.message, request.id, error.fields));
    }
    if (error.code === 'FST_ERR_CTP_BODY_TOO_LARGE') {
      return reply.status(413).send(body('PAYLOAD_TOO_LARGE', 'The request is too large.', request.id));
    }
    if (error.code === 'FST_ERR_CTP_INVALID_MEDIA_TYPE') {
      return reply.status(415).send(body('UNSUPPORTED_MEDIA_TYPE', 'Requests must be sent as JSON.', request.id));
    }
    const pg = getPgError(error);
    if (pg?.code === '40P01' || pg?.code === '40001') {
      request.log.warn({ code: pg.code }, 'Transaction conflict');
      return reply.status(409).send(body('CONFLICT', 'Another change happened at the same time. Please try again.', request.id));
    }
    if (typeof error.statusCode === 'number' && error.statusCode >= 400 && error.statusCode < 500) {
      return reply.status(400).send(body('BAD_REQUEST', 'The request could not be processed.', request.id));
    }
    request.log.error({ err: error }, 'Unhandled error');
    return reply
      .status(500)
      .send(body('INTERNAL_ERROR', 'Something went wrong on our side. Please try again.', request.id));
  });
}
