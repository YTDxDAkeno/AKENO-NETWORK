import { randomUUID } from 'node:crypto';
import cookie from '@fastify/cookie';
import helmet from '@fastify/helmet';
import rateLimit from '@fastify/rate-limit';
import websocket from '@fastify/websocket';
import Fastify, { type FastifyBaseLogger, type FastifyInstance } from 'fastify';
import type { Logger } from 'pino';
import { API_PREFIX, REALTIME_CLOSE_CODES } from '@akeno/shared';
import type { AppConfig } from '../config/env.js';
import { createDatabase, type DatabaseHandle } from '../database/client.js';
import { registerAuthentication } from '../middleware/auth.js';
import { registerErrorHandling } from '../middleware/error-handler.js';
import { registerOriginCheck } from '../middleware/origin.js';
import { administrationRoutes } from '../modules/administration/routes.js';
import { activityRoutes } from '../modules/activity/routes.js';
import { authRoutes } from '../modules/auth/routes.js';
import { devicesRoutes } from '../modules/devices/routes.js';
import { friendsRoutes } from '../modules/friends/routes.js';
import { gameSessionsRoutes } from '../modules/game-sessions/routes.js';
import { groupsRoutes } from '../modules/groups/routes.js';
import { healthRoutes } from '../modules/health/routes.js';
import { invitationsRoutes } from '../modules/invitations/routes.js';
import { mediaRoutes } from '../modules/media/routes.js';
import { messagesRoutes } from '../modules/messages/routes.js';
import { notificationsRoutes } from '../modules/notifications/routes.js';
import { partiesRoutes } from '../modules/parties/routes.js';
import { usersRoutes } from '../modules/users/routes.js';
import { realtimeGateway, startRealtimeMaintenance } from '../realtime/gateway.js';
import { isApiLikePath, registerStaticWeb } from '../services/static-web.js';
import { AppError } from '../utils/errors.js';
import { type AppContext, createContext } from './context.js';
import { createLogger } from './logger.js';

export interface BuiltApp {
  app: FastifyInstance;
  ctx: AppContext;
  database: DatabaseHandle;
}

export interface BuildAppOptions {
  config: AppConfig;
  /** Provide an existing database handle (tests); otherwise one is created and closed with the app. */
  database?: DatabaseHandle;
  logger?: Logger;
  /** Disable background maintenance timers (tests). */
  maintenance?: boolean;
}

export async function buildApp(options: BuildAppOptions): Promise<BuiltApp> {
  const { config } = options;
  const logger = options.logger ?? createLogger(config);
  const ownsDatabase = !options.database;
  const database = options.database ?? createDatabase({ url: config.databaseUrl, max: config.dbPoolMax });

  const app = Fastify({
    loggerInstance: logger as FastifyBaseLogger,
    trustProxy: config.trustProxy,
    bodyLimit: 64 * 1024,
    // Never trust client-supplied request IDs.
    requestIdHeader: false,
    genReqId: () => randomUUID(),
    return503OnClosing: true,
  });

  const ctx = createContext(config, database.db, app.log);

  // Accept JSON requests with an empty body (e.g. action endpoints such as /accept) while
  // keeping Fastify's default parser and its prototype-poisoning protection for everything else.
  const defaultJsonParser = app.getDefaultJsonParser('error', 'error');
  app.removeContentTypeParser('application/json');
  app.addContentTypeParser('application/json', { parseAs: 'string' }, (request, body, done) => {
    const text = typeof body === 'string' ? body : body.toString('utf8');
    if (text.trim() === '') {
      done(null, undefined);
      return;
    }
    defaultJsonParser(request, text, done);
  });

  await app.register(cookie);
  await app.register(helmet, {
    contentSecurityPolicy: {
      useDefaults: false,
      directives: {
        'default-src': ["'self'"],
        'base-uri': ["'self'"],
        'connect-src': ["'self'", ...config.allowedOrigins.map((origin) => origin.replace(/^http/, 'ws'))],
        'font-src': ["'self'", 'data:'],
        'form-action': ["'self'"],
        'frame-ancestors': ["'none'"],
        'img-src': ["'self'", 'data:', 'blob:'],
        'manifest-src': ["'self'"],
        'object-src': ["'none'"],
        'script-src': ["'self'"],
        'style-src': ["'self'"],
        'worker-src': ["'self'"],
        ...(config.session.cookieSecure ? { 'upgrade-insecure-requests': [] } : {}),
      },
    },
    hsts: config.session.cookieSecure ? { maxAge: 31_536_000, includeSubDomains: false } : false,
    crossOriginEmbedderPolicy: false,
    crossOriginResourcePolicy: { policy: 'same-origin' },
    referrerPolicy: { policy: 'same-origin' },
  });

  // Session resolution must run before rate limiting so limits can be keyed per user.
  registerAuthentication(app, ctx);
  registerOriginCheck(app, ctx);
  registerErrorHandling(app);

  if (config.rateLimit.enabled) {
    await app.register(rateLimit, {
      global: true,
      max: config.rateLimit.maxPerMinute,
      timeWindow: '1 minute',
      keyGenerator: (request) => (request.auth ? `user:${request.auth.userId}` : `ip:${request.ip}`),
      errorResponseBuilder: (_request, context) =>
        new AppError(429, 'RATE_LIMITED', `Too many requests. Please try again in ${context.after}.`),
    });
  }

  app.addHook('onSend', async (request, reply) => {
    if (request.url.startsWith('/api/')) reply.header('Cache-Control', 'no-store');
  });

  await app.register(websocket, { options: { maxPayload: 16 * 1024 } });
  await app.register(healthRoutes(ctx));
  await app.register(
    async (api) => {
      await api.register(authRoutes(ctx));
      await api.register(devicesRoutes(ctx));
      await api.register(usersRoutes(ctx));
      await api.register(friendsRoutes(ctx));
      await api.register(messagesRoutes(ctx));
      await api.register(groupsRoutes(ctx));
      await api.register(partiesRoutes(ctx));
      await api.register(gameSessionsRoutes(ctx));
      await api.register(invitationsRoutes(ctx));
      await api.register(notificationsRoutes(ctx));
      await api.register(administrationRoutes(ctx));
      await api.register(activityRoutes(ctx));
    },
    { prefix: API_PREFIX },
  );
  await app.register(realtimeGateway(ctx));
  await app.register(mediaRoutes(ctx));

  const servesWeb = config.webDistDir ? await registerStaticWeb(app, config.webDistDir) : false;
  app.setNotFoundHandler((request, reply) => {
    if (servesWeb && request.method === 'GET' && !isApiLikePath(request.url)) {
      return reply.header('Cache-Control', 'no-cache').sendFile('index.html');
    }
    return reply.status(404).send({
      error: { code: 'NOT_FOUND', message: 'The requested endpoint was not found.', requestId: request.id },
    });
  });

  let stopMaintenance: (() => void) | null = null;
  const housekeeping = setInterval(() => {
    void Promise.all([
      ctx.auth.deleteExpiredSessions(),
      ctx.devices.deleteExpired(),
      ctx.invitations.expireStale(),
      ctx.notifications.prune(),
      ctx.media.maintenance(),
    ])
      .then(() => ctx.auth.throttle.prune())
      .catch((error: unknown) => ctx.log.error({ err: error }, 'Housekeeping failed'));
  }, 15 * 60_000);
  housekeeping.unref();

  let activitySweep: NodeJS.Timeout | null = null;
  app.addHook('onReady', async () => {
    await ctx.media.init();
    await ctx.games.load();
    await ctx.activity.load();
    if (options.maintenance !== false) {
      stopMaintenance = startRealtimeMaintenance(ctx);
      // Game sessions whose device stopped sending heartbeats end at their last heartbeat.
      activitySweep = setInterval(() => {
        ctx.activity.endStale().catch((error: unknown) => ctx.log.error({ err: error }, 'Game activity sweep failed'));
      }, 30_000);
      activitySweep.unref();
    }
  });

  app.addHook('preClose', async () => {
    clearInterval(housekeeping);
    if (activitySweep) clearInterval(activitySweep);
    stopMaintenance?.();
    try {
      await ctx.presence.flushLastSeen();
    } catch (error) {
      ctx.log.warn({ err: error }, 'Could not persist last-seen during shutdown');
    }
    // Tell clients to reconnect with backoff instead of treating this as a failure.
    ctx.hub.closeAll(REALTIME_CLOSE_CODES.serviceRestart, 'Server restarting');
  });

  app.addHook('onClose', async () => {
    if (ownsDatabase) await database.close();
  });

  return { app, ctx, database };
}
