import { randomUUID } from 'node:crypto';
import type { WebSocket } from 'ws';
import { REALTIME_PROTOCOL_VERSION, type ServerEvent } from '@akeno/shared';

/** One authenticated WebSocket connection (a single browser tab or device). */
export class ClientConnection {
  readonly id = randomUUID();
  lastActivityAt = Date.now();
  idle = false;
  /** Per-connection token bucket for inbound client messages. */
  private tokens = 30;
  private lastRefill = Date.now();
  private lastTypingAt = 0;

  constructor(
    readonly socket: WebSocket,
    readonly userId: string,
    readonly sessionId: string,
    readonly sessionExpiresAt: Date,
  ) {}

  send(event: ServerEvent): void {
    if (this.socket.readyState !== this.socket.OPEN) return;
    this.socket.send(JSON.stringify({ v: REALTIME_PROTOCOL_VERSION, ts: new Date().toISOString(), ...event }));
  }

  touch(): void {
    this.lastActivityAt = Date.now();
  }

  /** Allows bursts of 30 messages, refilling at 3 messages per second. */
  consumeToken(): boolean {
    const now = Date.now();
    this.tokens = Math.min(30, this.tokens + ((now - this.lastRefill) / 1000) * 3);
    this.lastRefill = now;
    if (this.tokens < 1) return false;
    this.tokens -= 1;
    return true;
  }

  /** Typing indicators are throttled to one every 3 seconds per connection. */
  allowTyping(): boolean {
    const now = Date.now();
    if (now - this.lastTypingAt < 3000) return false;
    this.lastTypingAt = now;
    return true;
  }

  close(code: number, reason: string): void {
    try {
      this.socket.close(code, reason);
    } catch {
      this.socket.terminate();
    }
  }
}

/**
 * In-memory registry of live connections for this single backend instance.
 * Durable state lives in PostgreSQL; this hub only fans out realtime events.
 * A future multi-instance deployment would replace this class with a shared adapter
 * (e.g. PostgreSQL LISTEN/NOTIFY or Redis pub/sub) behind the same interface.
 */
export class RealtimeHub {
  private readonly byUser = new Map<string, Set<ClientConnection>>();

  add(connection: ClientConnection): { firstForUser: boolean } {
    let set = this.byUser.get(connection.userId);
    const firstForUser = !set || set.size === 0;
    if (!set) {
      set = new Set();
      this.byUser.set(connection.userId, set);
    }
    set.add(connection);
    return { firstForUser };
  }

  remove(connection: ClientConnection): { lastForUser: boolean } {
    const set = this.byUser.get(connection.userId);
    if (!set || !set.delete(connection)) return { lastForUser: false };
    if (set.size === 0) {
      this.byUser.delete(connection.userId);
      return { lastForUser: true };
    }
    return { lastForUser: false };
  }

  connectionsOf(userId: string): ReadonlySet<ClientConnection> {
    return this.byUser.get(userId) ?? new Set();
  }

  isConnected(userId: string): boolean {
    return (this.byUser.get(userId)?.size ?? 0) > 0;
  }

  connectedUserIds(): string[] {
    return [...this.byUser.keys()];
  }

  allConnections(): ClientConnection[] {
    return [...this.byUser.values()].flatMap((set) => [...set]);
  }

  connectionCount(): number {
    let count = 0;
    for (const set of this.byUser.values()) count += set.size;
    return count;
  }

  sendToUser(userId: string, event: ServerEvent): void {
    for (const connection of this.byUser.get(userId) ?? []) connection.send(event);
  }

  sendToUsers(userIds: Iterable<string>, event: ServerEvent): void {
    for (const userId of new Set(userIds)) this.sendToUser(userId, event);
  }

  /** Notifies and closes every connection that belongs to a revoked session. */
  disconnectSession(sessionId: string, reason: string, closeCode: number): void {
    for (const connection of this.allConnections()) {
      if (connection.sessionId === sessionId) {
        connection.send({ type: 'session.revoked', data: { reason } });
        connection.close(closeCode, 'Session revoked');
      }
    }
  }

  disconnectUser(userId: string, reason: string, closeCode: number, exceptSessionId?: string): void {
    for (const connection of [...this.connectionsOf(userId)]) {
      if (exceptSessionId && connection.sessionId === exceptSessionId) continue;
      connection.send({ type: 'session.revoked', data: { reason } });
      connection.close(closeCode, 'Session revoked');
    }
  }

  closeAll(code: number, reason: string): void {
    for (const connection of this.allConnections()) connection.close(code, reason);
  }
}
