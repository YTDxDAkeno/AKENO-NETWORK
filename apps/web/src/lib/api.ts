import { API_PREFIX, CSRF_HEADER, type ApiErrorBody } from '@akeno/shared';

/** Normalized API error with an English, user-presentable message. */
export class ApiError extends Error {
  readonly status: number;
  readonly code: string;
  readonly fields: Record<string, string>;

  constructor(status: number, code: string, message: string, fields: Record<string, string> = {}) {
    super(message);
    this.name = 'ApiError';
    this.status = status;
    this.code = code;
    this.fields = fields;
  }
}

/**
 * The CSRF token lives only in memory (never in localStorage). It is obtained from the
 * session endpoint after login or page load and sent with every state-changing request.
 */
let csrfToken: string | null = null;
let unauthorizedHandler: (() => void) | null = null;
let csrfRefresher: (() => Promise<string | null>) | null = null;

export function setCsrfToken(token: string | null): void {
  csrfToken = token;
}

export function onUnauthorized(handler: (() => void) | null): void {
  unauthorizedHandler = handler;
}

export function setCsrfRefresher(refresher: (() => Promise<string | null>) | null): void {
  csrfRefresher = refresher;
}

type Method = 'GET' | 'POST' | 'PUT' | 'PATCH' | 'DELETE';

interface RequestOptions {
  signal?: AbortSignal;
  /** Do not trigger the global "signed out" handler on 401 (used by the session probe). */
  allowUnauthenticated?: boolean;
}

async function send(method: Method, path: string, body: unknown, options: RequestOptions): Promise<Response> {
  const headers: Record<string, string> = { Accept: 'application/json' };
  if (body !== undefined) headers['Content-Type'] = 'application/json';
  if (method !== 'GET' && csrfToken) headers[CSRF_HEADER] = csrfToken;
  try {
    return await fetch(`${API_PREFIX}${path}`, {
      method,
      headers,
      body: body !== undefined ? JSON.stringify(body) : undefined,
      credentials: 'same-origin',
      cache: 'no-store',
      signal: options.signal,
    });
  } catch (error) {
    if (error instanceof DOMException && error.name === 'AbortError') throw error;
    throw new ApiError(0, 'NETWORK_ERROR', 'Could not reach AKENO NETWORK. Check your connection and try again.');
  }
}

async function request<T>(method: Method, path: string, body?: unknown, options: RequestOptions = {}): Promise<T> {
  let response = await send(method, path, body, options);

  // A stale CSRF token (e.g. after signing in on another tab) is refreshed once transparently.
  if (response.status === 403 && method !== 'GET' && csrfRefresher) {
    const peek = (await response.clone().json().catch(() => null)) as ApiErrorBody | null;
    if (peek?.error.code === 'CSRF_FAILED') {
      const token = await csrfRefresher();
      if (token) {
        csrfToken = token;
        response = await send(method, path, body, options);
      }
    }
  }

  if (response.status === 204) return undefined as T;
  const data = (await response.json().catch(() => null)) as (ApiErrorBody & T) | null;
  if (!response.ok) {
    if (response.status === 401 && !options.allowUnauthenticated) unauthorizedHandler?.();
    const error = data?.error;
    throw new ApiError(
      response.status,
      error?.code ?? 'UNKNOWN',
      error?.message ?? `The request failed (HTTP ${response.status}).`,
      error?.fields ?? {},
    );
  }
  return data as T;
}

export const api = {
  get: <T>(path: string, options?: RequestOptions) => request<T>('GET', path, undefined, options),
  post: <T>(path: string, body: unknown = {}, options?: RequestOptions) => request<T>('POST', path, body, options),
  put: <T>(path: string, body: unknown, options?: RequestOptions) => request<T>('PUT', path, body, options),
  patch: <T>(path: string, body: unknown, options?: RequestOptions) => request<T>('PATCH', path, body, options),
  delete: <T>(path: string, body?: unknown, options?: RequestOptions) => request<T>('DELETE', path, body, options),
};

/** Builds a query string, skipping empty values. */
export function query(params: Record<string, string | number | boolean | undefined | null>): string {
  const search = new URLSearchParams();
  for (const [key, value] of Object.entries(params)) {
    if (value === undefined || value === null || value === '' || value === false) continue;
    search.set(key, String(value));
  }
  const text = search.toString();
  return text ? `?${text}` : '';
}

export function errorMessage(error: unknown, fallback = 'Something went wrong. Please try again.'): string {
  if (error instanceof ApiError) return error.message;
  if (error instanceof Error && error.message) return error.message;
  return fallback;
}
