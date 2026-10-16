import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest';
import { BUTTON, NEUTRAL_STATE, SHAREPLAY_INPUT_CLOSE, decodeInputPacket, type SharePlayInputRelay } from '@akeno/shared';
import { PlayerTwoMonitor, PlayerTwoSender, isPlayerTwoActive } from '../../src/features/shareplay/player-two';
import { captureErrorMessage } from '../../src/features/shareplay/stream-controller';

/** A stand-in WebSocket that records what is sent (test only). */
class FakeSocket extends EventTarget {
  static OPEN = 1;
  static last: FakeSocket | null = null;
  readonly OPEN = 1;
  readyState = 1;
  binaryType = 'blob';
  readonly sent: Uint8Array[] = [];
  closedWith: number | null = null;
  constructor(readonly url: string) {
    super();
    FakeSocket.last = this;
  }
  send(data: Uint8Array): void {
    this.sent.push(data);
  }
  close(code = 1000): void {
    this.closedWith = code;
    this.readyState = 3;
  }
  serverText(data: unknown): void {
    this.dispatchEvent(new MessageEvent('message', { data: JSON.stringify(data) }));
  }
  serverClose(code: number, reason = ''): void {
    this.readyState = 3;
    const event = new Event('close') as Event & { code: number; reason: string };
    Object.assign(event, { code, reason });
    this.dispatchEvent(event);
  }
  packets() {
    return this.sent.map((bytes) => decodeInputPacket(bytes)).map((result) => (result.ok ? result.packet : null));
  }
}

function pad(pressed: number[] = [], axes = [0, 0, 0, 0]) {
  return {
    id: 'Xbox Wireless Controller (STANDARD GAMEPAD Vendor: 045e Product: 0b13)',
    index: 0,
    mapping: 'standard',
    connected: true,
    axes,
    buttons: Array.from({ length: 17 }, (_, index) => ({ pressed: pressed.includes(index), value: pressed.includes(index) ? 1 : 0 })),
  };
}

const relay = (patch: Partial<SharePlayInputRelay>): SharePlayInputRelay => ({
  sessionId: 's',
  grantId: 'g',
  epoch: 9,
  guestId: 'u',
  slot: 2,
  seq: 1,
  kind: 'state',
  buttons: 0,
  axes: [0, 0, 0, 0],
  triggers: [0, 0],
  senderTime: 0,
  serverTime: 0,
  ...patch,
});

beforeEach(() => {
  vi.stubGlobal('WebSocket', FakeSocket);
  vi.spyOn(document, 'hasFocus').mockReturnValue(true);
});

afterEach(() => {
  vi.useRealTimers();
  vi.unstubAllGlobals();
  vi.restoreAllMocks();
});

describe('Player 2 sender', () => {
  it('sends nothing before the server is ready, then continues after the server’s last sequence number', () => {
    let current = pad([BUTTON.south]);
    const sender = new PlayerTwoSender(() => [current]);
    sender.start('party', 9, 0);
    expect(isPlayerTwoActive()).toBe(true);
    const socket = FakeSocket.last!;
    expect(socket.url).toContain('/api/v1/shareplay/input?party=party&role=sender&epoch=9');
    sender.poll();
    expect(socket.sent).toHaveLength(0);
    socket.serverText({ type: 'ready', epoch: 9, lastSeq: 41 });
    sender.poll();
    expect(socket.packets()[0]).toMatchObject({ kind: 'state', epoch: 9, seq: 42, slot: 2, buttons: 1 << BUTTON.south });
    // The PS button is never sent.
    current = pad([BUTTON.home, BUTTON.north]);
    vi.spyOn(performance, 'now').mockReturnValue(performance.now() + 50);
    sender.poll();
    expect(socket.packets().at(-1)).toMatchObject({ seq: 43, buttons: 1 << BUTTON.north });
    sender.stop();
    expect(socket.packets().at(-1)).toMatchObject({ kind: 'release', ...NEUTRAL_STATE });
    expect(isPlayerTwoActive()).toBe(false);
  });

  it('releases everything when the window loses focus or the tab is hidden, and the controller disconnects', () => {
    const sender = new PlayerTwoSender(() => [pad([BUTTON.r1], [1, 0, 0, 0])]);
    sender.start('party', 3, 0);
    const socket = FakeSocket.last!;
    socket.serverText({ type: 'ready', epoch: 3, lastSeq: 0 });
    sender.poll();
    expect(socket.packets().at(-1)?.buttons).toBe(1 << BUTTON.r1);
    window.dispatchEvent(new Event('blur'));
    expect(socket.packets().at(-1)).toMatchObject({ kind: 'release', buttons: 0, axes: [0, 0, 0, 0] });
    expect(sender.getSnapshot().status).toBe('paused');
    // Paused: polling does not send held buttons.
    const count = socket.sent.length;
    sender.poll();
    expect(socket.sent).toHaveLength(count);
    window.dispatchEvent(new Event('focus'));
    expect(sender.getSnapshot().status).toBe('sending');
    const disconnect = new Event('gamepaddisconnected') as Event & { gamepad: { index: number } };
    disconnect.gamepad = { index: 0 };
    window.dispatchEvent(disconnect);
    expect(socket.packets().at(-1)?.kind).toBe('release');
    expect(sender.getSnapshot().status).toBe('waiting-for-controller');
    sender.stop();
  });

  it('stops for good when the grant ends, and refuses controllers without a standard layout', () => {
    const sender = new PlayerTwoSender(() => [{ ...pad([BUTTON.south]), mapping: '' }]);
    sender.start('party', 5, 0);
    const socket = FakeSocket.last!;
    socket.serverText({ type: 'ready', epoch: 5, lastSeq: 0 });
    sender.poll();
    expect(socket.packets().every((packet) => packet?.kind === 'release')).toBe(true);
    expect(sender.getSnapshot().notice).toMatch(/button layout/);
    socket.serverClose(SHAREPLAY_INPUT_CLOSE.grantEnded, 'The host revoked Player 2.');
    expect(sender.getSnapshot()).toMatchObject({ status: 'ended', notice: 'The host revoked Player 2.' });
    expect(isPlayerTwoActive()).toBe(false);
  });
});

describe('Player 2 monitor (host receiver)', () => {
  it('applies only newer packets, always applies releases, and returns to neutral after the watchdog', () => {
    vi.useFakeTimers({ toFake: ['setInterval', 'clearInterval', 'setTimeout', 'clearTimeout', 'performance'] });
    const monitor = new PlayerTwoMonitor();
    monitor.start('party');
    monitor.onRelay(relay({ seq: 5, buttons: 1 }));
    monitor.onRelay(relay({ seq: 4, buttons: 2 })); // late
    monitor.onRelay(relay({ seq: 5, buttons: 2 })); // duplicate
    expect(monitor.getSnapshot()).toMatchObject({ state: { buttons: 1 }, stale: 2 });
    monitor.onRelay(relay({ seq: 3, kind: 'release', reason: 'Player 2’s connection dropped.' }));
    expect(monitor.getSnapshot()).toMatchObject({ state: NEUTRAL_STATE, lastReason: 'Player 2’s connection dropped.' });
    monitor.onRelay(relay({ seq: 6, buttons: 8 }));
    expect(monitor.getSnapshot().state.buttons).toBe(8);
    vi.advanceTimersByTime(450);
    expect(monitor.getSnapshot()).toMatchObject({ expired: false, state: { buttons: 8 } });
    vi.advanceTimersByTime(150);
    expect(monitor.getSnapshot()).toMatchObject({ expired: true, state: NEUTRAL_STATE });
    // A new grant (epoch) starts its numbering again.
    monitor.onRelay(relay({ epoch: 10, seq: 1, buttons: 4 }));
    expect(monitor.getSnapshot().state.buttons).toBe(4);
    monitor.stop();
  });
});

describe('capture errors', () => {
  it('explains cancelled and failed captures', () => {
    expect(captureErrorMessage(new DOMException('x', 'NotAllowedError'), 'screen')).toMatch(/cancelled\. Nothing was streamed/);
    expect(captureErrorMessage(new DOMException('x', 'NotReadableError'), 'capture-device')).toMatch(/in use/);
  });
});
