import {
  DEFAULT_NORMALIZE_OPTIONS,
  INPUT_HEARTBEAT_MS,
  INPUT_SLOT_PLAYER_2,
  INPUT_WATCHDOG_MS,
  InputSequencer,
  NEUTRAL_STATE,
  SHAREPLAY_INPUT_CLOSE,
  SHAREPLAY_INPUT_PATH,
  controllerFamily,
  encodeInputPacket,
  normalizeGamepad,
  sameState,
  watchdogState,
  type ControllerFamily,
  type ControllerState,
  type GamepadLike,
  type SharePlayInputRelay,
} from '@akeno/shared';

/**
 * Player 2 in the browser: reading a controller with the standard Gamepad API, sending its state
 * over the authenticated controller link while (and only while) the host's grant is valid, and
 * the host's monitor that shows what arrives.
 *
 * Safety policy (sender): when the tab is hidden, the window loses focus, the controller
 * disconnects, polling stalls or the person stops, a release packet is sent at once and nothing
 * more until input resumes. Receivers additionally return to neutral after INPUT_WATCHDOG_MS
 * without fresh packets, so no button can stay held.
 */

/** True while a Player 2 controller is captured (pauses the app's gamepad UI navigation). */
let captured = false;
export function isPlayerTwoActive(): boolean {
  return captured;
}

export interface PadInfo {
  index: number;
  id: string;
  family: ControllerFamily;
  standard: boolean;
}

export type SenderStatus = 'idle' | 'waiting-for-controller' | 'connecting' | 'sending' | 'paused' | 'reconnecting' | 'ended' | 'error';

export interface PlayerTwoSnapshot {
  status: SenderStatus;
  partyId: string | null;
  epoch: number | null;
  pad: PadInfo | null;
  /** The state as sent (after normalization and dead zones). */
  state: ControllerState;
  /** Why input is paused or refused (focus lost, layout not standard, …). */
  notice: string | null;
  error: string | null;
  packetsSent: number;
  lastSentAt: number | null;
  /** Raw browser values for the controller test view (before normalization). */
  raw: { buttons: number[]; axes: number[] } | null;
}

const IDLE: PlayerTwoSnapshot = {
  status: 'idle',
  partyId: null,
  epoch: null,
  pad: null,
  state: NEUTRAL_STATE,
  notice: null,
  error: null,
  packetsSent: 0,
  lastSentAt: null,
  raw: null,
};

/** Minimum gap between two state packets (the protocol allows up to 120 per second). */
const MIN_SEND_GAP_MS = 8;
/** Polling that stops for this long (frozen tab, debugger) counts as a stall: release. */
const STALL_MS = 250;

export function listPads(source: Navigator = navigator): PadInfo[] {
  if (typeof source.getGamepads !== 'function') return [];
  return Array.from(source.getGamepads())
    .filter((pad): pad is Gamepad => Boolean(pad?.connected))
    .map((pad) => ({ index: pad.index, id: pad.id, family: controllerFamily(pad.id), standard: pad.mapping === 'standard' }));
}

function inputUrl(params: Record<string, string>): string {
  const url = new URL(SHAREPLAY_INPUT_PATH, window.location.href);
  url.protocol = url.protocol === 'https:' ? 'wss:' : 'ws:';
  for (const [key, value] of Object.entries(params)) url.searchParams.set(key, value);
  return url.toString();
}

/** The guest's side: controller → server. */
export class PlayerTwoSender {
  private snapshot: PlayerTwoSnapshot = IDLE;
  private readonly listeners = new Set<() => void>();
  private socket: WebSocket | null = null;
  private ready = false;
  private seq = 0;
  private padIndex: number | null = null;
  private frame = 0;
  private heartbeat: number | null = null;
  private reconnectTimer: number | null = null;
  private reconnects = 0;
  private lastPollAt = 0;
  private lastSent: ControllerState = NEUTRAL_STATE;
  private lastSentAt = 0;
  private paused = false;
  private readonly onBlur = () => this.pause('The AKENO window lost focus: Player 2 input is released until you come back.');
  private readonly onFocus = () => this.resume();
  private readonly onVisibility = () => (document.visibilityState === 'hidden' ? this.pause('The AKENO tab is in the background: Player 2 input is released.') : this.resume());
  private readonly onDisconnect = (event: GamepadEvent) => {
    if (event.gamepad.index === this.padIndex) {
      this.release();
      this.padIndex = null;
      this.set({ status: 'waiting-for-controller', pad: null, notice: 'The controller disconnected. Reconnect it and press a button.' });
    }
  };
  private readonly onConnect = () => {
    if (this.snapshot.status === 'waiting-for-controller') this.pickPad();
  };

  constructor(private readonly getPads: () => ReadonlyArray<GamepadLike & { index: number }> = () => Array.from(navigator.getGamepads?.() ?? []).filter(Boolean) as Gamepad[]) {}

  subscribe = (listener: () => void): (() => void) => {
    this.listeners.add(listener);
    return () => this.listeners.delete(listener);
  };

  getSnapshot = (): PlayerTwoSnapshot => this.snapshot;

  private set(patch: Partial<PlayerTwoSnapshot>): void {
    this.snapshot = { ...this.snapshot, ...patch };
    for (const listener of this.listeners) listener();
  }

  /**
   * Starts sending for a grant. Must follow a click ("Use my controller"): browsers only expose
   * controllers after the person interacts with the page and presses a controller button.
   */
  start(partyId: string, epoch: number, padIndex: number | null): void {
    this.stop();
    captured = true;
    this.padIndex = padIndex;
    this.seq = 0;
    this.reconnects = 0;
    this.set({ ...IDLE, status: 'connecting', partyId, epoch });
    window.addEventListener('blur', this.onBlur);
    window.addEventListener('focus', this.onFocus);
    document.addEventListener('visibilitychange', this.onVisibility);
    window.addEventListener('gamepaddisconnected', this.onDisconnect);
    window.addEventListener('gamepadconnected', this.onConnect);
    this.paused = document.visibilityState === 'hidden' || (typeof document.hasFocus === 'function' && !document.hasFocus());
    if (this.padIndex === null) this.pickPad();
    this.open();
  }

  private pickPad(): void {
    const pad = this.getPads().find((candidate) => candidate.connected && candidate.mapping === 'standard') ?? this.getPads().find((candidate) => candidate.connected);
    if (!pad) {
      this.set({ status: 'waiting-for-controller', notice: 'Press a button on your controller so the browser shows it.' });
      return;
    }
    this.padIndex = pad.index;
    this.set({ pad: { index: pad.index, id: pad.id, family: controllerFamily(pad.id), standard: pad.mapping === 'standard' } });
    if (this.ready) this.set({ status: this.paused ? 'paused' : 'sending', notice: null });
  }

  private open(): void {
    const { partyId, epoch } = this.snapshot;
    if (!partyId || epoch === null) return;
    const socket = new WebSocket(inputUrl({ party: partyId, role: 'sender', epoch: String(epoch) }));
    socket.binaryType = 'arraybuffer';
    this.socket = socket;
    this.ready = false;
    socket.addEventListener('message', (event) => {
      if (typeof event.data !== 'string') return;
      try {
        const message = JSON.parse(event.data) as { type?: string; lastSeq?: number };
        if (message.type === 'ready') {
          // Continue after the last packet the server accepted for this grant.
          this.seq = Math.max(this.seq, Number(message.lastSeq ?? 0));
          this.ready = true;
          this.reconnects = 0;
          this.set({ status: this.padIndex === null ? 'waiting-for-controller' : this.paused ? 'paused' : 'sending', error: null });
          this.loop();
        }
      } catch {
        // Ignore anything else.
      }
    });
    socket.addEventListener('close', (event) => {
      if (this.socket !== socket) return;
      this.socket = null;
      this.ready = false;
      if (event.code === SHAREPLAY_INPUT_CLOSE.grantEnded || event.code === 4403) return this.finish('ended', event.reason || 'Player 2 access has ended.');
      if (event.code === SHAREPLAY_INPUT_CLOSE.replaced) return this.finish('ended', 'Your controller is now used from another tab or device.');
      if (event.code === SHAREPLAY_INPUT_CLOSE.protocol) return this.finish('error', event.reason || 'The controller link was closed.');
      // Network problem: the server keeps the grant for a few seconds; try to come back.
      if (this.reconnects >= 3) return this.finish('error', 'The controller link was lost. Ask for Player 2 again.');
      this.reconnects += 1;
      this.set({ status: 'reconnecting', notice: 'Controller link lost. Reconnecting…' });
      this.reconnectTimer = window.setTimeout(() => this.open(), 700 * this.reconnects);
    });
  }

  private loop(): void {
    if (this.frame) return;
    this.lastPollAt = performance.now();
    const tick = () => {
      this.frame = requestAnimationFrame(tick);
      this.poll();
    };
    this.frame = requestAnimationFrame(tick);
    if (this.heartbeat === null) {
      this.heartbeat = window.setInterval(() => {
        // rAF stops in hidden tabs; a stalled loop must not keep the last state alive.
        if (performance.now() - this.lastPollAt > STALL_MS) {
          if (!this.paused) this.pause('Controller polling stalled: input released.');
          return;
        }
        if (!this.paused && performance.now() - this.lastSentAt >= INPUT_HEARTBEAT_MS) this.send('state', this.lastSent);
      }, INPUT_HEARTBEAT_MS / 2);
    }
  }

  /** Reads the controller once (also called by tests). */
  poll(): void {
    this.lastPollAt = performance.now();
    if (!this.ready || this.padIndex === null) return;
    const pad = this.getPads().find((candidate) => candidate.index === this.padIndex);
    if (!pad) return;
    const result = normalizeGamepad(pad, DEFAULT_NORMALIZE_OPTIONS);
    const raw = { buttons: pad.buttons.map((button) => Math.round(button.value * 100) / 100), axes: pad.axes.map((value) => Math.round(value * 100) / 100) };
    if (!result.ok) {
      this.release();
      this.set({ notice: result.reason, raw });
      return;
    }
    if (this.paused) {
      this.set({ raw });
      return;
    }
    const now = performance.now();
    if (!sameState(result.state, this.lastSent) && now - this.lastSentAt >= MIN_SEND_GAP_MS) this.send('state', result.state);
    if (!sameState(result.state, this.snapshot.state) || this.snapshot.raw === null) this.set({ state: result.state, raw });
  }

  private send(kind: 'state' | 'release', state: ControllerState): void {
    if (!this.socket || !this.ready || this.socket.readyState !== WebSocket.OPEN || this.snapshot.epoch === null) return;
    this.seq += 1;
    this.socket.send(encodeInputPacket({ kind, slot: INPUT_SLOT_PLAYER_2, epoch: this.snapshot.epoch, seq: this.seq, senderTime: Math.floor(performance.now()) >>> 0, ...state }));
    this.lastSent = kind === 'release' ? NEUTRAL_STATE : state;
    this.lastSentAt = performance.now();
    this.set({ packetsSent: this.snapshot.packetsSent + 1, lastSentAt: Date.now() });
  }

  /** Everything up, sticks centred. */
  release(): void {
    this.send('release', NEUTRAL_STATE);
    this.set({ state: NEUTRAL_STATE });
  }

  private pause(reason: string): void {
    if (this.snapshot.status === 'idle') return;
    this.paused = true;
    this.release();
    this.set({ status: this.ready ? 'paused' : this.snapshot.status, notice: reason });
  }

  private resume(): void {
    if (this.snapshot.status === 'idle' || document.visibilityState === 'hidden') return;
    this.paused = false;
    this.lastPollAt = performance.now();
    if (this.ready) this.set({ status: this.padIndex === null ? 'waiting-for-controller' : 'sending', notice: null });
  }

  private finish(status: 'ended' | 'error', message: string): void {
    this.stop();
    this.set({ ...IDLE, status, ...(status === 'error' ? { error: message } : { notice: message }) });
  }

  /** Releases everything and closes the link. */
  stop(): void {
    if (this.ready) this.release();
    captured = false;
    cancelAnimationFrame(this.frame);
    this.frame = 0;
    if (this.heartbeat !== null) window.clearInterval(this.heartbeat);
    this.heartbeat = null;
    if (this.reconnectTimer !== null) window.clearTimeout(this.reconnectTimer);
    this.reconnectTimer = null;
    const socket = this.socket;
    this.socket = null;
    this.ready = false;
    socket?.close(1000, 'Stopped');
    window.removeEventListener('blur', this.onBlur);
    window.removeEventListener('focus', this.onFocus);
    document.removeEventListener('visibilitychange', this.onVisibility);
    window.removeEventListener('gamepaddisconnected', this.onDisconnect);
    window.removeEventListener('gamepadconnected', this.onConnect);
    this.lastSent = NEUTRAL_STATE;
    if (this.snapshot.status !== 'idle') this.set({ ...IDLE });
  }
}

export interface MonitorSnapshot {
  connected: boolean;
  /** The state currently applied (neutral after the watchdog). */
  state: ControllerState;
  expired: boolean;
  last: SharePlayInputRelay | null;
  received: number;
  stale: number;
  /** Server → this browser delay is not measurable without synchronized clocks; packet age is. */
  packetsPerSecond: number;
  lastReason: string | null;
}

const MONITOR_IDLE: MonitorSnapshot = { connected: false, state: NEUTRAL_STATE, expired: false, last: null, received: 0, stale: 0, packetsPerSecond: 0, lastReason: null };

/**
 * The host's diagnostic receiver: shows Player 2's input as it arrives at this browser, with the
 * same ordering and watchdog rules a console adapter would use. It controls nothing.
 */
export class PlayerTwoMonitor {
  private snapshot: MonitorSnapshot = MONITOR_IDLE;
  private readonly listeners = new Set<() => void>();
  private socket: WebSocket | null = null;
  private readonly sequencer = new InputSequencer();
  private lastApplied: { state: ControllerState; receivedAt: number } | null = null;
  private timer: number | null = null;
  private window: number[] = [];

  subscribe = (listener: () => void): (() => void) => {
    this.listeners.add(listener);
    return () => this.listeners.delete(listener);
  };

  getSnapshot = (): MonitorSnapshot => this.snapshot;

  private set(patch: Partial<MonitorSnapshot>): void {
    this.snapshot = { ...this.snapshot, ...patch };
    for (const listener of this.listeners) listener();
  }

  start(partyId: string): void {
    this.stop();
    const socket = new WebSocket(inputUrl({ party: partyId, role: 'monitor' }));
    this.socket = socket;
    socket.addEventListener('open', () => this.set({ connected: true }));
    socket.addEventListener('message', (event) => {
      if (typeof event.data === 'string') this.onRelay(JSON.parse(event.data) as SharePlayInputRelay);
    });
    socket.addEventListener('close', () => {
      if (this.socket === socket) {
        this.socket = null;
        this.lastApplied = null;
        this.set({ connected: false, state: NEUTRAL_STATE });
      }
    });
    this.timer = window.setInterval(() => this.tick(), 50);
  }

  /** Applies one relayed packet (exported for tests through the instance). */
  onRelay(relay: SharePlayInputRelay): void {
    const now = performance.now();
    if (relay.kind === 'release') {
      // Releases are always applied: going to neutral is always safe.
      this.lastApplied = { state: NEUTRAL_STATE, receivedAt: now };
      this.set({ state: NEUTRAL_STATE, last: relay, lastReason: relay.reason ?? 'Player 2 released the controller.', expired: false, received: this.snapshot.received + 1 });
      return;
    }
    if (!this.sequencer.accept(relay.epoch, relay.seq)) {
      this.set({ stale: this.snapshot.stale + 1 });
      return;
    }
    const state: ControllerState = { buttons: relay.buttons, axes: relay.axes, triggers: relay.triggers };
    this.lastApplied = { state, receivedAt: now };
    this.window.push(now);
    this.set({ state, last: relay, expired: false, lastReason: null, received: this.snapshot.received + 1 });
  }

  private tick(): void {
    const now = performance.now();
    this.window = this.window.filter((at) => now - at < 1000);
    const watchdog = watchdogState(this.lastApplied, now, INPUT_WATCHDOG_MS);
    if (watchdog.expired !== this.snapshot.expired || this.window.length !== this.snapshot.packetsPerSecond || (watchdog.expired && !sameState(this.snapshot.state, NEUTRAL_STATE))) {
      this.set({ expired: watchdog.expired, state: watchdog.state, packetsPerSecond: this.window.length });
    }
  }

  stop(): void {
    if (this.timer !== null) window.clearInterval(this.timer);
    this.timer = null;
    const socket = this.socket;
    this.socket = null;
    socket?.close(1000, 'Stopped');
    this.sequencer.reset();
    this.lastApplied = null;
    this.window = [];
    this.snapshot = MONITOR_IDLE;
    for (const listener of this.listeners) listener();
  }
}
