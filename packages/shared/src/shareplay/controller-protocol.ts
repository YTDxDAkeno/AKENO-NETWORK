/**
 * SharePlay remote controller protocol, version 1.
 *
 * A guest who was explicitly granted "Player 2" by the SharePlay host sends the state of a
 * browser gamepad to the AKENO server, which checks every packet against the active grant and
 * forwards it to the host's receivers (the host's browser monitor and the host's paired PS5
 * console app). This module is used by all three sides; the PS5 receiver re-implements the same
 * rules in C (clients/ps5/app/shareplay_rx.c).
 *
 * IMPORTANT: transporting controller state is all this protocol does. Whether a PS5 game can be
 * driven by it depends on a virtual-controller mechanism that does not exist yet
 * (docs/SHAREPLAY_STATUS.md). Nothing here injects input anywhere.
 *
 * Wire format (guest → server, one binary WebSocket message per packet, little endian, 32 bytes):
 *
 *   offset size field
 *   0      1    magic 0xA7
 *   1      1    protocol version (1)
 *   2      1    kind: 1 = state, 2 = release (all buttons up, sticks centred, triggers 0)
 *   3      1    controller slot (2 = Player 2; the only slot in this version)
 *   4      4    grant epoch (u32, issued by the server with the grant; packets of an older
 *               grant are refused, so a previous authorization cannot be replayed)
 *   8      4    sequence number (u32, starts at 1 for each grant, strictly increasing)
 *   12     4    sender clock in ms (u32, monotonic, wraps; for diagnostics only)
 *   16     4    buttons (u32 bitset, bit i = W3C "standard gamepad" button i, see BUTTON)
 *   20     8    left x, left y, right x, right y (i16 each, -32767..32767; browser convention:
 *               x right positive, y down positive)
 *   28     1    left trigger (u8, 0..255)
 *   29     1    right trigger (u8, 0..255)
 *   30     2    reserved, must be 0
 *
 * Continuous state is sent at most INPUT_MAX_RATE_HZ times a second when it changes, and
 * repeated every INPUT_HEARTBEAT_MS while it does not. Receivers apply only packets newer than
 * the last one they applied (sequence numbers; stale or duplicated packets are dropped) and
 * return everything to neutral when no valid packet arrived for INPUT_WATCHDOG_MS.
 */

export const INPUT_PROTOCOL_VERSION = 1;
export const INPUT_PACKET_MAGIC = 0xa7;
export const INPUT_PACKET_BYTES = 32;
/** Receivers release every button and centre the sticks after this long without fresh input. */
export const INPUT_WATCHDOG_MS = 500;
/** Senders repeat an unchanged state this often, so the watchdog only fires on real loss. */
export const INPUT_HEARTBEAT_MS = 100;
/** Highest packet rate a sender may use (state changes); the server drops anything above it. */
export const INPUT_MAX_RATE_HZ = 120;
/** The only remote controller slot in this version. */
export const INPUT_SLOT_PLAYER_2 = 2;
export const INPUT_AXIS_MAX = 32767;

export const INPUT_KIND = { state: 1, release: 2 } as const;
export type InputKind = keyof typeof INPUT_KIND;

/** Button bits: the W3C standard gamepad layout (https://w3c.github.io/gamepad/#remapping). */
export const BUTTON = {
  /** Cross (PlayStation) / A (Xbox) */
  south: 0,
  /** Circle / B */
  east: 1,
  /** Square / X */
  west: 2,
  /** Triangle / Y */
  north: 3,
  l1: 4,
  r1: 5,
  /** Digital part of the left trigger (the analog value is sent separately). */
  l2: 6,
  r2: 7,
  /** Create / Share / View / Back */
  create: 8,
  /** Options / Start / Menu */
  options: 9,
  l3: 10,
  r3: 11,
  dpadUp: 12,
  dpadDown: 13,
  dpadLeft: 14,
  dpadRight: 15,
  /** PS / Xbox / Home button */
  home: 16,
  /** DualSense / DualShock 4 touchpad click (browsers expose it as button 17 when at all) */
  touchpad: 17,
} as const;
export type ButtonName = keyof typeof BUTTON;
export const BUTTON_COUNT = 18;

/**
 * Buttons a remote guest may never send: the PS/Home button (system menu, quick menu) and the
 * Create button (system capture and share menu). They are cleared by the sender and refused by
 * the server. This is the only system-level restriction the protocol can enforce; whether a
 * future console adapter can keep a guest out of system menus is unknown (SHAREPLAY_STATUS.md).
 */
export const BLOCKED_BUTTONS_MASK = (1 << BUTTON.home) | (1 << BUTTON.create);
export const VALID_BUTTONS_MASK = (1 << BUTTON_COUNT) - 1;

export interface ControllerState {
  /** Bitset of pressed buttons (see BUTTON). */
  buttons: number;
  /** Left x, left y, right x, right y in -32767..32767. */
  axes: [number, number, number, number];
  /** Left and right trigger in 0..255. */
  triggers: [number, number];
}

export const NEUTRAL_STATE: ControllerState = Object.freeze({ buttons: 0, axes: [0, 0, 0, 0], triggers: [0, 0] }) as ControllerState;

export interface InputPacket extends ControllerState {
  kind: InputKind;
  slot: number;
  epoch: number;
  seq: number;
  senderTime: number;
}

export function isNeutral(state: ControllerState): boolean {
  return state.buttons === 0 && state.axes.every((value) => value === 0) && state.triggers.every((value) => value === 0);
}

export function sameState(a: ControllerState, b: ControllerState): boolean {
  return (
    a.buttons === b.buttons &&
    a.axes[0] === b.axes[0] &&
    a.axes[1] === b.axes[1] &&
    a.axes[2] === b.axes[2] &&
    a.axes[3] === b.axes[3] &&
    a.triggers[0] === b.triggers[0] &&
    a.triggers[1] === b.triggers[1]
  );
}

function clampInt(value: number, min: number, max: number): number {
  if (!Number.isFinite(value)) return 0;
  return Math.min(max, Math.max(min, Math.round(value)));
}

export function encodeInputPacket(packet: InputPacket): Uint8Array {
  const bytes = new Uint8Array(INPUT_PACKET_BYTES);
  const view = new DataView(bytes.buffer);
  const release = packet.kind === 'release';
  view.setUint8(0, INPUT_PACKET_MAGIC);
  view.setUint8(1, INPUT_PROTOCOL_VERSION);
  view.setUint8(2, INPUT_KIND[packet.kind]);
  view.setUint8(3, clampInt(packet.slot, 0, 255));
  view.setUint32(4, packet.epoch >>> 0, true);
  view.setUint32(8, packet.seq >>> 0, true);
  view.setUint32(12, packet.senderTime >>> 0, true);
  view.setUint32(16, release ? 0 : (packet.buttons & VALID_BUTTONS_MASK & ~BLOCKED_BUTTONS_MASK) >>> 0, true);
  for (let index = 0; index < 4; index += 1) {
    view.setInt16(20 + index * 2, release ? 0 : clampInt(packet.axes[index] ?? 0, -INPUT_AXIS_MAX, INPUT_AXIS_MAX), true);
  }
  view.setUint8(28, release ? 0 : clampInt(packet.triggers[0], 0, 255));
  view.setUint8(29, release ? 0 : clampInt(packet.triggers[1], 0, 255));
  return bytes;
}

export type DecodeResult = { ok: true; packet: InputPacket } | { ok: false; error: InputRejection };

export type InputRejection =
  | 'size'
  | 'magic'
  | 'version'
  | 'kind'
  | 'slot'
  | 'reserved'
  | 'buttons'
  | 'axes'
  | 'blocked-button'
  | 'release-not-neutral';

/**
 * Strict decoding: anything that is not exactly a version-1 packet is refused (never "repaired"),
 * including unknown button bits, the blocked system buttons, -32768 on an axis and non-zero
 * reserved bytes.
 */
export function decodeInputPacket(data: ArrayBuffer | ArrayBufferView): DecodeResult {
  const bytes = data instanceof ArrayBuffer ? new Uint8Array(data) : new Uint8Array(data.buffer, data.byteOffset, data.byteLength);
  if (bytes.byteLength !== INPUT_PACKET_BYTES) return { ok: false, error: 'size' };
  const view = new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength);
  if (view.getUint8(0) !== INPUT_PACKET_MAGIC) return { ok: false, error: 'magic' };
  if (view.getUint8(1) !== INPUT_PROTOCOL_VERSION) return { ok: false, error: 'version' };
  const kindByte = view.getUint8(2);
  const kind: InputKind | null = kindByte === INPUT_KIND.state ? 'state' : kindByte === INPUT_KIND.release ? 'release' : null;
  if (!kind) return { ok: false, error: 'kind' };
  const slot = view.getUint8(3);
  if (slot !== INPUT_SLOT_PLAYER_2) return { ok: false, error: 'slot' };
  if (view.getUint16(30, true) !== 0) return { ok: false, error: 'reserved' };
  const buttons = view.getUint32(16, true);
  if ((buttons & ~VALID_BUTTONS_MASK) !== 0) return { ok: false, error: 'buttons' };
  if ((buttons & BLOCKED_BUTTONS_MASK) !== 0) return { ok: false, error: 'blocked-button' };
  const axes: [number, number, number, number] = [view.getInt16(20, true), view.getInt16(22, true), view.getInt16(24, true), view.getInt16(26, true)];
  if (axes.some((value) => value < -INPUT_AXIS_MAX)) return { ok: false, error: 'axes' };
  const triggers: [number, number] = [view.getUint8(28), view.getUint8(29)];
  const packet: InputPacket = {
    kind,
    slot,
    epoch: view.getUint32(4, true),
    seq: view.getUint32(8, true),
    senderTime: view.getUint32(12, true),
    buttons,
    axes,
    triggers,
  };
  if (kind === 'release' && !isNeutral(packet)) return { ok: false, error: 'release-not-neutral' };
  return { ok: true, packet };
}

/**
 * Keeps only packets newer than the last applied one. Sequence numbers restart at 1 for every
 * grant (a new epoch), so the sequencer is reset whenever the epoch changes.
 */
export class InputSequencer {
  private epoch: number | null = null;
  private last = 0;
  accepted = 0;
  stale = 0;

  /** True when the packet should be applied. */
  accept(epoch: number, seq: number): boolean {
    if (this.epoch !== epoch) {
      this.epoch = epoch;
      this.last = 0;
    }
    if (seq <= this.last || seq === 0) {
      this.stale += 1;
      return false;
    }
    this.last = seq;
    this.accepted += 1;
    return true;
  }

  get lastSeq(): number {
    return this.last;
  }

  reset(): void {
    this.epoch = null;
    this.last = 0;
  }
}

/**
 * Token bucket shared by the server (per guest connection) and tests: INPUT_MAX_RATE_HZ
 * sustained, with a short burst allowance for jittery senders.
 */
export class InputRateLimiter {
  private tokens: number;
  private last: number;

  constructor(
    private readonly ratePerSecond = INPUT_MAX_RATE_HZ,
    private readonly burst = Math.ceil(INPUT_MAX_RATE_HZ / 4),
    now = Date.now(),
  ) {
    this.tokens = burst;
    this.last = now;
  }

  take(now = Date.now()): boolean {
    this.tokens = Math.min(this.burst, this.tokens + ((now - this.last) / 1000) * this.ratePerSecond);
    this.last = now;
    if (this.tokens < 1) return false;
    this.tokens -= 1;
    return true;
  }
}

/**
 * Receiver-side safety: the state to apply right now. Fresh input is used as is; anything older
 * than INPUT_WATCHDOG_MS (or no input at all) becomes the neutral state, so a lost connection,
 * a frozen browser tab or a revoked grant can never leave a button held.
 */
export function watchdogState(last: { state: ControllerState; receivedAt: number } | null, now: number, timeoutMs = INPUT_WATCHDOG_MS): { state: ControllerState; expired: boolean } {
  if (!last || now - last.receivedAt > timeoutMs) return { state: NEUTRAL_STATE, expired: last !== null };
  return { state: last.state, expired: false };
}

// ---- Browser gamepad normalization ------------------------------------------------------------

/** The parts of the browser's Gamepad object the normalizer reads (testable without a browser). */
export interface GamepadLike {
  id: string;
  mapping: string;
  connected: boolean;
  buttons: ReadonlyArray<{ pressed: boolean; value: number }>;
  axes: ReadonlyArray<number>;
}

export interface NormalizeOptions {
  /** Radial dead zone for each stick, 0..0.5 (fraction of full deflection). */
  stickDeadZone: number;
  /** Trigger values below this are treated as released, 0..0.5. */
  triggerDeadZone: number;
}

export const DEFAULT_NORMALIZE_OPTIONS: NormalizeOptions = { stickDeadZone: 0.12, triggerDeadZone: 0.04 };

export type ControllerFamily = 'xbox' | 'dualsense' | 'dualshock4' | 'generic';

/** A display-only guess from the browser's id string (vendor 045e Microsoft, 054c Sony). */
export function controllerFamily(id: string): ControllerFamily {
  const lower = id.toLowerCase();
  if (/dualsense|054c.*0ce6|054c.*0df2|product: 0ce6|product: 0df2/.test(lower)) return 'dualsense';
  if (/dualshock|wireless controller.*054c|054c.*(05c4|09cc|0ba0)|product: (05c4|09cc)/.test(lower)) return 'dualshock4';
  if (/xbox|xinput|045e/.test(lower)) return 'xbox';
  return 'generic';
}

function clampUnit(value: number): number {
  if (!Number.isFinite(value)) return 0;
  return Math.min(1, Math.max(-1, value));
}

/**
 * Radial dead zone with rescaling: inside the dead zone the stick is centred; outside it the
 * remaining travel is stretched back to the full range, so small movements stay precise.
 */
export function applyStickDeadZone(x: number, y: number, deadZone: number): [number, number] {
  const cx = clampUnit(x);
  const cy = clampUnit(y);
  const length = Math.hypot(cx, cy);
  const magnitude = Math.min(1, length);
  if (magnitude <= deadZone || length === 0) return [0, 0];
  const scaled = (magnitude - deadZone) / (1 - deadZone);
  // Direction from the raw vector; length capped at 1 (square-gate corners land on the circle).
  return [(cx / length) * scaled, (cy / length) * scaled];
}

export function applyTriggerDeadZone(value: number, deadZone: number): number {
  const v = Math.min(1, Math.max(0, Number.isFinite(value) ? value : 0));
  if (v <= deadZone) return 0;
  return (v - deadZone) / (1 - deadZone);
}

export type NormalizeResult = { ok: true; state: ControllerState } | { ok: false; reason: string };

/**
 * Converts a browser gamepad into protocol state. Only controllers the browser reports with the
 * "standard" mapping are accepted: for anything else the browser does not tell us which button
 * is which, and guessing could press the wrong buttons in a game.
 */
export function normalizeGamepad(gamepad: GamepadLike, options: NormalizeOptions = DEFAULT_NORMALIZE_OPTIONS): NormalizeResult {
  if (!gamepad.connected) return { ok: false, reason: 'The controller is disconnected.' };
  if (gamepad.mapping !== 'standard') {
    return {
      ok: false,
      reason: 'Your browser does not recognize this controller’s button layout. Use Chrome or Edge, or connect the controller by USB.',
    };
  }
  let buttons = 0;
  for (let index = 0; index < Math.min(BUTTON_COUNT, gamepad.buttons.length); index += 1) {
    const button = gamepad.buttons[index];
    if (!button) continue;
    let pressed = Boolean(button.pressed);
    if (index === BUTTON.l2 || index === BUTTON.r2) pressed = applyTriggerDeadZone(button.value, options.triggerDeadZone) > 0.5 || (pressed && button.value === 0);
    if (pressed) buttons |= 1 << index;
  }
  buttons &= ~BLOCKED_BUTTONS_MASK;
  const [lx, ly] = applyStickDeadZone(gamepad.axes[0] ?? 0, gamepad.axes[1] ?? 0, options.stickDeadZone);
  const [rx, ry] = applyStickDeadZone(gamepad.axes[2] ?? 0, gamepad.axes[3] ?? 0, options.stickDeadZone);
  const toAxis = (value: number) => clampInt(value * INPUT_AXIS_MAX, -INPUT_AXIS_MAX, INPUT_AXIS_MAX);
  const trigger = (index: number) => {
    const button = gamepad.buttons[index];
    if (!button) return 0;
    // Some drivers report a digital trigger only (value stays 0 while pressed).
    const value = button.value === 0 && button.pressed ? 1 : button.value;
    return clampInt(applyTriggerDeadZone(value, options.triggerDeadZone) * 255, 0, 255);
  };
  return {
    ok: true,
    state: { buttons, axes: [toAxis(lx), toAxis(ly), toAxis(rx), toAxis(ry)], triggers: [trigger(BUTTON.l2), trigger(BUTTON.r2)] },
  };
}

/** Names of the pressed buttons, for diagnostics. */
export function pressedButtons(buttons: number): ButtonName[] {
  return (Object.keys(BUTTON) as ButtonName[]).filter((name) => (buttons & (1 << BUTTON[name])) !== 0);
}
