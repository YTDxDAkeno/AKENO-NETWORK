import type { ErrorCode } from '@akeno/shared';

/**
 * An expected, client-facing error. The message is shown to users, so it must be English,
 * actionable and free of internal details.
 */
export class AppError extends Error {
  readonly statusCode: number;
  readonly code: ErrorCode;
  readonly fields: Record<string, string> | undefined;

  constructor(statusCode: number, code: ErrorCode, message: string, fields?: Record<string, string>) {
    super(message);
    this.name = 'AppError';
    this.statusCode = statusCode;
    this.code = code;
    this.fields = fields;
  }
}

export const Errors = {
  badRequest: (message = 'The request is invalid.') => new AppError(400, 'BAD_REQUEST', message),
  validation: (message: string, fields?: Record<string, string>) =>
    new AppError(400, 'VALIDATION_FAILED', message, fields),
  unauthenticated: (message = 'You need to sign in to continue.') => new AppError(401, 'UNAUTHENTICATED', message),
  forbidden: (message = 'You do not have permission to do that.') => new AppError(403, 'FORBIDDEN', message),
  notFound: (what = 'The requested resource') => new AppError(404, 'NOT_FOUND', `${what} was not found.`),
  conflict: (message: string, code: ErrorCode = 'CONFLICT') => new AppError(409, code, message),
  rateLimited: (message = 'Too many requests. Please slow down and try again shortly.') =>
    new AppError(429, 'RATE_LIMITED', message),
};

export function isAppError(error: unknown): error is AppError {
  return error instanceof AppError;
}
