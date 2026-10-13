import type { FastifyPluginAsync } from 'fastify';
import {
  REALTIME_CLOSE_CODES,
  REALTIME_HEARTBEAT_INTERVAL_MS,
  REALTIME_PATH,
  REALTIME_PROTOCOL_VERSION,
  REALTIME_STALE_TIMEOUT_MS,
  clientRealtimeMessageSchema,
} from '@akeno/shared';
import type { AppContext } from '../app/context.js';
import { isAllowedOrigin } from '../middleware/origin.js';
import { AppError, Errors } from '../utils/errors.js';
import { ClientConnection } from './hub.js';

/**
 * Authenticated WebSocket gateway.
 *
 * The handshake must carry a valid session cookie and an allowed Origin header; otherwise the
 * upgrade is rejected with an HTTP error before any socket is created. The identity is always
 * taken from the server-side session, never from client-supplied data.
 */
export function realtimeGateway(ctx: AppContext): FastifyPluginAsync {
  return async (app) => {
    app.get(
      REALTIME_PATH,
      {
        websocket: true,
        config: { rateLimit: { max: 60, timeWindow: '1 minute' } },
        preValidation: async (request) => {
          if (!isAllowedOrigin(request, ctx.config.allowedOrigins)) {
            throw new AppError(403, 'ORIGIN_REJECTED', 'WebSocket connections are only accepted from the AKENO NETWORK app.');
          }
          if (!request.auth) throw Errors.unauthenticated();
        },
      },
      (socket, request) => {
        const auth = request.auth!;
        const connection = new ClientConnection(socket, auth.userId, auth.sessionId, auth.sessionExpiresAt);
        ctx.hub.add(connection);
        connection.send({
          type: 'hello',
          data: {
            userId: auth.userId,
            connectionId: connection.id,
            serverTime: new Date().toISOString(),
            heartbeatIntervalMs: REALTIME_HEARTBEAT_INTERVAL_MS,
            protocolVersion: REALTIME_PROTOCOL_VERSION,
          },
        });
        ctx.presence.handleConnect(connection).catch((error: unknown) => {
          request.log.error({ err: error }, 'Presence connect handling failed');
        });

        socket.on('pong', () => connection.touch());

        socket.on('message', (raw, isBinary) => {
          connection.touch();
          if (isBinary) {
            connection.close(REALTIME_CLOSE_CODES.protocolError, 'Binary frames are not supported');
            return;
          }
          if (!connection.consumeToken()) return; // Silently drop floods.
          let payload: unknown;
          try {
            payload = JSON.parse(raw.toString());
          } catch {
            connection.close(REALTIME_CLOSE_CODES.protocolError, 'Invalid JSON');
            return;
          }
          const parsed = clientRealtimeMessageSchema.safeParse(payload);
          if (!parsed.success) {
            connection.close(REALTIME_CLOSE_CODES.protocolError, 'Unsupported message');
            return;
          }
          const message = parsed.data;
          switch (message.type) {
            case 'ping':
              connection.send({ type: 'pong', data: { serverTime: new Date().toISOString() } });
              break;
            case 'presence.idle':
              ctx.presence.handleIdle(connection, message.data.idle).catch((error: unknown) => {
                request.log.error({ err: error }, 'Presence idle handling failed');
              });
              break;
            case 'typing':
              if (connection.allowTyping()) {
                ctx.messages.relayTyping(auth.userId, message.data.conversationId).catch(() => {
                  // Unauthorized or unknown conversations are ignored without feedback.
                });
              }
              break;
          }
        });

        socket.on('close', () => {
          const { lastForUser } = ctx.hub.remove(connection);
          ctx.presence.handleDisconnect(connection, lastForUser).catch((error: unknown) => {
            request.log.error({ err: error }, 'Presence disconnect handling failed');
          });
        });

        socket.on('error', (error) => {
          request.log.warn({ err: error }, 'WebSocket error');
        });
      },
    );
  };
}

/**
 * Background maintenance for live connections:
 * - every 20 s: terminate connections silent for longer than the stale timeout, close expired
 *   sessions, and send protocol pings so dead TCP connections are detected;
 * - every 5 min: re-validate sessions, keep active sessions fresh, and persist last-seen.
 */
export function startRealtimeMaintenance(ctx: AppContext): () => void {
  const heartbeat = setInterval(() => {
    const now = Date.now();
    for (const connection of ctx.hub.allConnections()) {
      if (now - connection.lastActivityAt > REALTIME_STALE_TIMEOUT_MS) {
        connection.socket.terminate();
        continue;
      }
      if (connection.sessionExpiresAt.getTime() <= now) {
        connection.send({ type: 'session.revoked', data: { reason: 'Your session expired. Please sign in again.' } });
        connection.close(REALTIME_CLOSE_CODES.sessionRevoked, 'Session expired');
        continue;
      }
      try {
        connection.socket.ping();
      } catch {
        connection.socket.terminate();
      }
    }
  }, 20_000);

  const sessions = setInterval(() => {
    void (async () => {
      const sessionIds = new Set(ctx.hub.allConnections().map((connection) => connection.sessionId));
      for (const sessionId of sessionIds) {
        if (await ctx.auth.isSessionValid(sessionId)) {
          await ctx.auth.touchSession(sessionId);
        } else {
          ctx.hub.disconnectSession(sessionId, 'Your session is no longer valid.', REALTIME_CLOSE_CODES.sessionRevoked);
        }
      }
      await ctx.presence.flushLastSeen();
    })().catch((error: unknown) => ctx.log.error({ err: error }, 'Realtime session maintenance failed'));
  }, 5 * 60_000);

  heartbeat.unref();
  sessions.unref();
  return () => {
    clearInterval(heartbeat);
    clearInterval(sessions);
  };
}
