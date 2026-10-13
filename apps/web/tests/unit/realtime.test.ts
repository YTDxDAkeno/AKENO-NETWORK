import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest';
import { RealtimeClient, type RealtimeStatus } from '../../src/lib/realtime';

class FakeSocket {
  static readonly CONNECTING = 0;
  static readonly OPEN = 1;
  static readonly CLOSING = 2;
  static readonly CLOSED = 3;
  static instances: FakeSocket[] = [];
  readyState = FakeSocket.CONNECTING;
  sent: string[] = [];
  onmessage: ((event: MessageEvent) => void) | null = null;
  onclose: ((event: CloseEvent) => void) | null = null;
  onerror: (() => void) | null = null;

  constructor(readonly url: string) {
    FakeSocket.instances.push(this);
  }
  send(data: string) {
    this.sent.push(data);
  }
  close(code = 1000) {
    this.readyState = FakeSocket.CLOSED;
    this.onclose?.({ code } as CloseEvent);
  }
  // Test helpers
  open() {
    this.readyState = FakeSocket.OPEN;
    this.receive({ v: 1, type: 'hello', ts: '', data: { userId: 'u', connectionId: 'c', serverTime: '', heartbeatIntervalMs: 25000, protocolVersion: 1 } });
  }
  receive(event: unknown) {
    this.onmessage?.({ data: JSON.stringify(event) } as MessageEvent);
  }
  drop(code = 1006) {
    this.readyState = FakeSocket.CLOSED;
    this.onclose?.({ code } as CloseEvent);
  }
}

function createClient() {
  return new RealtimeClient({
    url: 'ws://test/ws',
    WebSocketImpl: FakeSocket as unknown as typeof WebSocket,
    heartbeatIntervalMs: 1000,
    pongTimeoutMs: 500,
    random: () => 1,
  });
}

beforeEach(() => {
  vi.useFakeTimers();
  FakeSocket.instances = [];
});

afterEach(() => {
  vi.useRealTimers();
});

describe('RealtimeClient', () => {
  it('reports status changes and flags reconnects', () => {
    const client = createClient();
    const statuses: RealtimeStatus[] = [];
    const hellos: boolean[] = [];
    client.onStatus((status) => statuses.push(status));
    client.onEvent((event, meta) => {
      if (event.type === 'hello') hellos.push(meta.reconnected);
    });
    client.start();
    FakeSocket.instances[0]!.open();
    FakeSocket.instances[0]!.drop();
    expect(client.getStatus()).toBe('reconnecting');
    vi.advanceTimersByTime(500);
    FakeSocket.instances[1]!.open();
    expect(statuses).toEqual(['connecting', 'open', 'reconnecting', 'open']);
    expect(hellos).toEqual([false, true]);
    client.stop();
  });

  it('backs off exponentially between failed attempts', () => {
    const client = createClient();
    client.start();
    FakeSocket.instances[0]!.drop();
    vi.advanceTimersByTime(499);
    expect(FakeSocket.instances).toHaveLength(1);
    vi.advanceTimersByTime(1);
    expect(FakeSocket.instances).toHaveLength(2);
    FakeSocket.instances[1]!.drop();
    vi.advanceTimersByTime(999);
    expect(FakeSocket.instances).toHaveLength(2);
    vi.advanceTimersByTime(1);
    expect(FakeSocket.instances).toHaveLength(3);
    client.stop();
  });

  it('stops reconnecting when the session is revoked', () => {
    const client = createClient();
    client.start();
    FakeSocket.instances[0]!.open();
    FakeSocket.instances[0]!.drop(4401);
    expect(client.getStatus()).toBe('revoked');
    vi.advanceTimersByTime(60_000);
    expect(FakeSocket.instances).toHaveLength(1);
  });

  it('closes a silent connection when no pong arrives', () => {
    const client = createClient();
    client.start();
    const socket = FakeSocket.instances[0]!;
    socket.open();
    vi.advanceTimersByTime(1000);
    expect(socket.sent.map((raw) => JSON.parse(raw).type)).toContain('ping');
    vi.advanceTimersByTime(500);
    expect(client.getStatus()).toBe('reconnecting');
    client.stop();
  });

  it('keeps the connection when pongs arrive', () => {
    const client = createClient();
    client.start();
    const socket = FakeSocket.instances[0]!;
    socket.open();
    for (let i = 0; i < 3; i += 1) {
      vi.advanceTimersByTime(1000);
      socket.receive({ v: 1, type: 'pong', ts: '', data: { serverTime: '' } });
    }
    expect(client.getStatus()).toBe('open');
    client.stop();
    expect(client.getStatus()).toBe('stopped');
  });
});
