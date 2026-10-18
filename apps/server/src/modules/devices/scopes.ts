import { API_PREFIX, type DeviceScope, MEDIA_PATH_PREFIX, REALTIME_PATH } from '@akeno/shared';

/**
 * Routes a paired device may call, and the scope each one requires. Everything that is not
 * listed here is refused for device tokens (default deny). In particular, devices can never
 * reach authentication, password, account deletion, privacy, block list, device management or
 * administration routes; those require a signed-in web session.
 *
 * `null` means any paired device may call the route regardless of its scopes.
 */
const API_ROUTES: Record<string, DeviceScope | null> = {
  // The device itself: discovery and sign-out.
  'GET /devices/current': null,
  'DELETE /devices/current': null,
  // Account Bridge: the device describes the local user selected on it (its own row only).
  'PUT /devices/current/console-user': null,
  'DELETE /devices/current/console-user': null,

  'GET /users/me': 'profile:read',
  'GET /users/search': 'profile:read',
  'GET /users/:handle': 'profile:read',

  'PUT /presence': 'presence:write',

  'GET /friends': 'friends:read',
  'GET /friends/requests': 'friends:read',
  'POST /friends/requests': 'friends:write',
  'POST /friends/requests/:id/accept': 'friends:write',
  'POST /friends/requests/:id/decline': 'friends:write',
  'POST /friends/requests/:id/cancel': 'friends:write',

  'GET /conversations': 'messages:read',
  'GET /conversations/:id': 'messages:read',
  'GET /conversations/:id/messages': 'messages:read',
  'POST /conversations': 'messages:write',
  'POST /conversations/:id/messages': 'messages:write',
  'POST /conversations/:id/read': 'messages:write',
  'GET /groups': 'messages:read',
  'GET /groups/:id': 'messages:read',
  'GET /groups/:id/messages': 'messages:read',
  'POST /groups/:id/messages': 'messages:write',
  'GET /parties/:id/messages': 'messages:read',
  'POST /parties/:id/messages': 'messages:write',

  'GET /parties/current': 'parties:read',
  'GET /parties': 'parties:read',
  'GET /parties/:id': 'parties:read',
  'GET /parties/:id/members': 'parties:read',
  // SharePlay status, read-only (the console app shows it; starting, watching and Player 2 need the website).
  'GET /parties/:id/shareplay': 'parties:read',
  'GET /shareplay/status': 'parties:read',
  // The console app opts in to display Player 2 input (it never controls anything).
  'POST /parties/:id/shareplay/receiver': 'parties:read',
  'POST /parties': 'parties:write',
  'POST /parties/:id/join': 'parties:write',
  'POST /parties/:id/leave': 'parties:write',
  'POST /parties/:id/invitations': 'parties:write',
  // Party voice for consoles, through the AKENO voice gateway (docs/PS5_VOICE.md).
  'POST /parties/:id/voice/device-token': 'parties:write',

  'GET /game-sessions': 'sessions:read',
  'GET /game-sessions/mine': 'sessions:read',
  'GET /game-sessions/:id': 'sessions:read',
  'POST /game-sessions': 'sessions:write',
  'PATCH /game-sessions/:id': 'sessions:write',
  'POST /game-sessions/:id/join': 'sessions:write',
  'POST /game-sessions/:id/leave': 'sessions:write',
  'POST /game-sessions/:id/close': 'sessions:write',
  'DELETE /game-sessions/:id/request': 'sessions:write',
  'POST /game-sessions/:id/invitations': 'sessions:write',

  'GET /invitations': 'invitations:read',
  'GET /notifications': 'invitations:read',
  'POST /invitations/:id/accept': 'invitations:write',
  'POST /invitations/:id/decline': 'invitations:write',
  'POST /notifications/read': 'invitations:write',

  // Automatic game activity: only the device's own detection, never history management.
  'POST /activity/heartbeat': 'activity:write',
  'POST /activity/stop': 'activity:write',
  'GET /users/:handle/activity': 'profile:read',
};

const DEVICE_ROUTES = new Map<string, DeviceScope | null>([
  ...Object.entries(API_ROUTES).map(([key, scope]) => {
    const [method, path] = key.split(' ') as [string, string];
    return [`${method} ${API_PREFIX}${path}`, scope] as const;
  }),
  [`GET ${REALTIME_PATH}`, 'realtime'],
  // Profile pictures and game artwork (the console page shows them through its local proxy).
  [`GET ${MEDIA_PATH_PREFIX}/:id/:file`, 'profile:read'],
]);

export type DeviceRouteDecision = { allowed: true } | { allowed: false; scope: DeviceScope | null };

/**
 * Decides whether a device with `granted` scopes may call the route registered as `routeUrl`
 * (Fastify's route pattern, e.g. "/api/v1/friends/requests/:id/accept").
 */
export function deviceRouteDecision(
  method: string,
  routeUrl: string,
  granted: ReadonlySet<DeviceScope>,
): DeviceRouteDecision {
  const normalizedMethod = method === 'HEAD' ? 'GET' : method;
  const key = `${normalizedMethod} ${routeUrl}`;
  if (!DEVICE_ROUTES.has(key)) return { allowed: false, scope: null };
  const scope = DEVICE_ROUTES.get(key) ?? null;
  if (scope === null || granted.has(scope)) return { allowed: true };
  return { allowed: false, scope };
}

/** Every route key devices may use (for tests and documentation). */
export function deviceRouteKeys(): string[] {
  return [...DEVICE_ROUTES.keys()];
}
