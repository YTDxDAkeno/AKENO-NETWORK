import { createServer, type IncomingMessage, type Server } from 'node:http';
import type { Duplex } from 'node:stream';
import type { Logger } from 'pino';
import { type WebSocket, WebSocketServer } from 'ws';
import type { GatewayConfig } from './config.js';
import { MAX_MESSAGE_BYTES } from './protocol.js';
import { Session, type SessionRegistry } from './session.js';

export interface Gateway {
  server: Server;
  registry: SessionRegistry;
  close(): Promise<void>;
}

function reject(socket: Duplex, status: number, text: string): void {
  socket.write(`HTTP/1.1 ${status} ${text}\r\nConnection: close\r\nContent-Length: 0\r\n\r\n`);
  socket.destroy();
}

/**
 * The gateway's HTTP server: GET /healthz, and WebSocket upgrades on the configured path. It is
 * meant to sit behind the AKENO reverse proxy (Caddy terminates TLS for wss://<domain>/voice-gateway).
 * Requests that carry an Origin header (browsers) are refused: browsers use LiveKit directly.
 */
export function createGateway(config: GatewayConfig, log: Logger): Gateway {
  const registry: SessionRegistry = { sessions: new Map(), maxSessions: config.maxSessions };
  const wss = new WebSocketServer({ noServer: true, maxPayload: MAX_MESSAGE_BYTES, perMessageDeflate: false });
  const sockets = new Set<WebSocket>();

  const server = createServer((request, response) => {
    if (request.method === 'GET' && request.url === '/healthz') {
      response.writeHead(200, { 'content-type': 'application/json', 'cache-control': 'no-store' });
      response.end(JSON.stringify({ ok: true, sessions: registry.sessions.size }));
      return;
    }
    response.writeHead(404, { 'content-type': 'text/plain' });
    response.end('Not found');
  });

  server.on('upgrade', (request: IncomingMessage, socket: Duplex, head: Buffer) => {
    const path = (request.url ?? '').split('?')[0];
    if (path !== config.path) return reject(socket, 404, 'Not Found');
    if (request.headers.origin) return reject(socket, 403, 'Forbidden');
    if (sockets.size >= config.maxSessions * 2) return reject(socket, 503, 'Service Unavailable');
    wss.handleUpgrade(request, socket, head, (ws) => {
      sockets.add(ws);
      ws.on('close', () => sockets.delete(ws));
      new Session(ws, config, log, registry);
    });
  });

  return {
    server,
    registry,
    async close() {
      await Promise.all([...registry.sessions.values()].map((session) => session.shutdown(1001, 'Gateway shutting down.')));
      for (const ws of sockets) ws.terminate();
      wss.close();
      await new Promise<void>((resolve) => server.close(() => resolve()));
    },
  };
}
