import {
  REALTIME_CLOSE_CODES,
  REALTIME_HEARTBEAT_INTERVAL_MS,
  REALTIME_PATH,
  REALTIME_PROTOCOL_VERSION,
  type ClientRealtimeMessage,
  type ServerEventEnvelope,
} from '@akeno/shared';

export type RealtimeStatus = 'idle' | 'connecting' | 'open' | 'reconnecting' | 'revoked' | 'stopped';

export interface RealtimeOptions {
  url?: string;
  /** Injectable for tests. */
  WebSocketImpl?: typeof WebSocket;
  heartbeatIntervalMs?: number;
  pongTimeoutMs?: number;
  maxBackoffMs?: number;
  random?: () => number;
}

type EventListener = (event: ServerEventEnvelope, meta: { reconnected: boolean }) => void;
type StatusListener = (status: RealtimeStatus) => void;

function defaultUrl(): string {
  const protocol = window.location.protocol === 'https:' ? 'wss:' : 'ws:';
  return `${protocol}//${window.location.host}${REALTIME_PATH}`;
}

/**
 * Resilient WebSocket client.
 *
 * - Reconnects with jittered exponential backoff (0.5 s doubling up to 30 s).
 * - Sends an application ping every 25 s and reconnects if no pong arrives within 10 s.
 * - Stops reconnecting when the server revokes the session (close code 4401).
 * - Emits `hello` with `reconnected: true` after a reconnect so callers can re-synchronize
 *   state through the REST API.
 */
export class RealtimeClient {
  private socket: WebSocket | null = null;
  private status: RealtimeStatus = 'idle';
  private attempts = 0;
  private everConnected = false;
  private heartbeatTimer: ReturnType<typeof setInterval> | null = null;
  private pongTimer: ReturnType<typeof setTimeout> | null = null;
  private reconnectTimer: ReturnType<typeof setTimeout> | null = null;
  private readonly listeners = new Set<EventListener>();
  private readonly statusListeners = new Set<StatusListener>();
  private readonly options: Required<Omit<RealtimeOptions, 'url' | 'WebSocketImpl'>> & {
    url: () => string;
    WebSocketImpl: typeof WebSocket;
  };

  constructor(options: RealtimeOptions = {}) {
    this.options = {
      url: options.url ? () => options.url! : defaultUrl,
      WebSocketImpl: options.WebSocketImpl ?? WebSocket,
      heartbeatIntervalMs: options.heartbeatIntervalMs ?? REALTIME_HEARTBEAT_INTERVAL_MS,
      pongTimeoutMs: options.pongTimeoutMs ?? 10_000,
      maxBackoffMs: options.maxBackoffMs ?? 30_000,
      random: options.random ?? Math.random,
    };
  }

  getStatus(): RealtimeStatus {
    return this.status;
  }

  onEvent(listener: EventListener): () => void {
    this.listeners.add(listener);
    return () => this.listeners.delete(listener);
  }

  onStatus(listener: StatusListener): () => void {
    this.statusListeners.add(listener);
    return () => this.statusListeners.delete(listener);
  }

  start(): void {
    if (this.status !== 'idle' && this.status !== 'stopped' && this.status !== 'revoked') return;
    this.attempts = 0;
    this.connect();
  }

  stop(): void {
    this.clearTimers();
    this.setStatus('stopped');
    const socket = this.socket;
    this.socket = null;
    socket?.close(1000, 'Client stopped');
  }

  /** Reconnect immediately (e.g. when the browser reports it is back online). */
  reconnectNow(): void {
    if (this.status === 'reconnecting' || this.status === 'connecting') {
      if (this.reconnectTimer) {
        clearTimeout(this.reconnectTimer);
        this.reconnectTimer = null;
        this.connect();
      }
    }
  }

  send(message: ClientRealtimeMessage): boolean {
    if (!this.socket || this.socket.readyState !== this.options.WebSocketImpl.OPEN) return false;
    this.socket.send(JSON.stringify(message));
    return true;
  }

  private setStatus(status: RealtimeStatus): void {
    if (this.status === status) return;
    this.status = status;
    for (const listener of this.statusListeners) listener(status);
  }

  private connect(): void {
    this.setStatus(this.everConnected ? 'reconnecting' : 'connecting');
    let socket: WebSocket;
    try {
      socket = new this.options.WebSocketImpl(this.options.url());
    } catch {
      this.scheduleReconnect();
      return;
    }
    this.socket = socket;

    socket.onmessage = (message: MessageEvent) => {
      if (this.socket !== socket) return;
      let event: ServerEventEnvelope;
      try {
        event = JSON.parse(String(message.data)) as ServerEventEnvelope;
      } catch {
        return;
      }
      if (event.type === 'pong') {
        if (this.pongTimer) clearTimeout(this.pongTimer);
        this.pongTimer = null;
      }
      const reconnected = event.type === 'hello' && this.everConnected;
      if (event.type === 'hello') {
        this.everConnected = true;
        this.attempts = 0;
        this.setStatus('open');
        this.startHeartbeat();
      }
      for (const listener of this.listeners) listener(event, { reconnected });
    };

    socket.onclose = (event: CloseEvent) => {
      if (this.socket !== socket) return;
      this.socket = null;
      this.clearTimers();
      if (this.status === 'stopped') return;
      if (event.code === REALTIME_CLOSE_CODES.sessionRevoked) {
        this.setStatus('revoked');
        return;
      }
      this.scheduleReconnect();
    };

    socket.onerror = () => {
      // The close handler performs reconnection.
    };
  }

  private scheduleReconnect(): void {
    this.setStatus('reconnecting');
    const base = Math.min(this.options.maxBackoffMs, 500 * 2 ** this.attempts);
    const delay = Math.round(base * (0.5 + this.options.random() * 0.5));
    this.attempts += 1;
    this.reconnectTimer = setTimeout(() => {
      this.reconnectTimer = null;
      this.connect();
    }, delay);
  }

  private startHeartbeat(): void {
    if (this.heartbeatTimer) clearInterval(this.heartbeatTimer);
    this.heartbeatTimer = setInterval(() => {
      if (!this.send({ v: REALTIME_PROTOCOL_VERSION, type: 'ping' })) return;
      if (this.pongTimer) return;
      this.pongTimer = setTimeout(() => {
        // No pong: the connection is dead even if the browser has not noticed yet.
        this.pongTimer = null;
        this.socket?.close(4000, 'Heartbeat timeout');
      }, this.options.pongTimeoutMs);
    }, this.options.heartbeatIntervalMs);
  }

  private clearTimers(): void {
    if (this.heartbeatTimer) clearInterval(this.heartbeatTimer);
    if (this.pongTimer) clearTimeout(this.pongTimer);
    if (this.reconnectTimer) clearTimeout(this.reconnectTimer);
    this.heartbeatTimer = null;
    this.pongTimer = null;
    this.reconnectTimer = null;
  }
}
