/** Stable machine-readable error codes returned by the API. */
export const ERROR_CODES = [
  'BAD_REQUEST',
  'VALIDATION_FAILED',
  'UNAUTHENTICATED',
  'INVALID_CREDENTIALS',
  'ACCOUNT_SUSPENDED',
  'FORBIDDEN',
  'CSRF_FAILED',
  'ORIGIN_REJECTED',
  'NOT_FOUND',
  'CONFLICT',
  'ONLINE_ID_TAKEN',
  'INVITATION_REQUIRED',
  'INVITATION_INVALID',
  'ALREADY_FRIENDS',
  'REQUEST_PENDING',
  'BLOCKED',
  'PRIVACY_RESTRICTED',
  'ALREADY_MEMBER',
  'NOT_MEMBER',
  'CAPACITY_REACHED',
  'SESSION_CLOSED',
  'PAYLOAD_TOO_LARGE',
  'UNSUPPORTED_MEDIA_TYPE',
  'RATE_LIMITED',
  'SERVICE_UNAVAILABLE',
  'INTERNAL_ERROR',
] as const;
export type ErrorCode = (typeof ERROR_CODES)[number];

export interface ApiErrorBody {
  error: {
    code: ErrorCode;
    message: string;
    /** Field-level validation messages keyed by dotted path. */
    fields?: Record<string, string>;
    requestId?: string;
  };
}
