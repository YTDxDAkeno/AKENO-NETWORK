import { afterEach, describe, expect, it, vi } from 'vitest';
import { ApiError, api, onUnauthorized, query, setCsrfRefresher, setCsrfToken } from '../../src/lib/api';

function jsonResponse(status: number, body: unknown): Response {
  return new Response(body === null ? null : JSON.stringify(body), { status, headers: { 'Content-Type': 'application/json' } });
}

afterEach(() => {
  setCsrfToken(null);
  onUnauthorized(null);
  setCsrfRefresher(null);
});

describe('api client', () => {
  it('sends JSON with the CSRF header on state-changing requests only', async () => {
    const fetchMock = vi.spyOn(globalThis, 'fetch').mockResolvedValue(jsonResponse(200, { ok: true }));
    setCsrfToken('token-123');
    await api.post('/friends/requests', { userId: 'x' });
    await api.get('/friends');
    const [postUrl, postInit] = fetchMock.mock.calls[0]!;
    expect(postUrl).toBe('/api/v1/friends/requests');
    expect((postInit!.headers as Record<string, string>)['x-csrf-token']).toBe('token-123');
    expect(postInit!.credentials).toBe('same-origin');
    expect(postInit!.body).toBe(JSON.stringify({ userId: 'x' }));
    const getHeaders = fetchMock.mock.calls[1]![1]!.headers as Record<string, string>;
    expect(getHeaders['x-csrf-token']).toBeUndefined();
  });

  it('normalizes API errors with field messages', async () => {
    vi.spyOn(globalThis, 'fetch').mockResolvedValue(
      jsonResponse(400, { error: { code: 'VALIDATION_FAILED', message: 'Bad input.', fields: { onlineId: 'Too short.' } } }),
    );
    const error = await api.post('/auth/register', {}).catch((caught: unknown) => caught);
    expect(error).toBeInstanceOf(ApiError);
    expect(error).toMatchObject({ status: 400, code: 'VALIDATION_FAILED', message: 'Bad input.', fields: { onlineId: 'Too short.' } });
  });

  it('reports network failures in plain English', async () => {
    vi.spyOn(globalThis, 'fetch').mockRejectedValue(new TypeError('Failed to fetch'));
    await expect(api.get('/friends')).rejects.toMatchObject({ code: 'NETWORK_ERROR', message: expect.stringContaining('Could not reach') });
  });

  it('calls the unauthorized handler on 401 unless explicitly allowed', async () => {
    const handler = vi.fn();
    onUnauthorized(handler);
    vi.spyOn(globalThis, 'fetch').mockImplementation(async () => jsonResponse(401, { error: { code: 'UNAUTHENTICATED', message: 'Sign in.' } }));
    await expect(api.get('/friends')).rejects.toBeInstanceOf(ApiError);
    expect(handler).toHaveBeenCalledTimes(1);
    await expect(api.get('/auth/session', { allowUnauthenticated: true })).rejects.toBeInstanceOf(ApiError);
    expect(handler).toHaveBeenCalledTimes(1);
  });

  it('refreshes a stale CSRF token once and retries', async () => {
    setCsrfToken('stale');
    setCsrfRefresher(async () => 'fresh');
    const fetchMock = vi
      .spyOn(globalThis, 'fetch')
      .mockResolvedValueOnce(jsonResponse(403, { error: { code: 'CSRF_FAILED', message: 'Stale token.' } }))
      .mockResolvedValueOnce(new Response(null, { status: 204 }));
    await expect(api.post('/notifications/read', { all: true })).resolves.toBeUndefined();
    expect(fetchMock).toHaveBeenCalledTimes(2);
    expect((fetchMock.mock.calls[1]![1]!.headers as Record<string, string>)['x-csrf-token']).toBe('fresh');
  });

  it('builds query strings without empty values', () => {
    expect(query({ q: 'abc', before: undefined, joinable: false, limit: 20 })).toBe('?q=abc&limit=20');
    expect(query({})).toBe('');
  });
});
