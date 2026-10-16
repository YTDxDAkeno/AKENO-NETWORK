import { describe, expect, it } from 'vitest';
import {
  BLOCKED_BUTTONS_MASK,
  BUTTON,
  INPUT_AXIS_MAX,
  INPUT_PACKET_BYTES,
  INPUT_WATCHDOG_MS,
  InputRateLimiter,
  InputSequencer,
  NEUTRAL_STATE,
  applyStickDeadZone,
  controllerFamily,
  decodeInputPacket,
  encodeInputPacket,
  normalizeGamepad,
  pressedButtons,
  watchdogState,
  type GamepadLike,
  type InputPacket,
} from './controller-protocol.js';
import { SHAREPLAY_QUALITY_PRESETS, qualitiesUpTo } from './quality.js';

function pad(overrides: Partial<GamepadLike> & { pressed?: number[]; values?: Record<number, number> } = {}): GamepadLike {
  const buttons = Array.from({ length: overrides.buttons?.length ?? 17 }, (_, index) => ({
    pressed: overrides.pressed?.includes(index) ?? false,
    value: overrides.values?.[index] ?? (overrides.pressed?.includes(index) ? 1 : 0),
  }));
  return { id: 'Xbox 360 Controller (XInput STANDARD GAMEPAD)', mapping: 'standard', connected: true, axes: [0, 0, 0, 0], ...overrides, buttons };
}

const base: InputPacket = { kind: 'state', slot: 2, epoch: 77, seq: 5, senderTime: 1234, buttons: 0, axes: [0, 0, 0, 0], triggers: [0, 0] };

describe('gamepad normalization', () => {
  it('maps an Xbox controller in the standard layout', () => {
    const result = normalizeGamepad(pad({ pressed: [BUTTON.south, BUTTON.dpadLeft, BUTTON.options], axes: [1, 0, 0, 0.5] }));
    expect(result.ok).toBe(true);
    if (!result.ok) return;
    expect(pressedButtons(result.state.buttons)).toEqual(['south', 'options', 'dpadLeft']);
    expect(result.state.axes[0]).toBe(INPUT_AXIS_MAX);
    expect(result.state.axes[1]).toBe(0);
    expect(result.state.axes[2]).toBe(0);
    const up = normalizeGamepad(pad({ axes: [0, -1, 0, 0] }));
    expect(up.ok && up.state.axes[1]).toBe(-INPUT_AXIS_MAX);
    // A full diagonal keeps its direction at full length (about 0.707 per axis).
    const diagonal = normalizeGamepad(pad({ axes: [1, -1, 0, 0] }));
    expect(diagonal.ok && diagonal.state.axes[0]).toBeCloseTo(23170, -1);
    expect(diagonal.ok && diagonal.state.axes[1]).toBeCloseTo(-23170, -1);
    expect(result.state.axes[3]).toBeGreaterThan(14000);
    expect(result.state.axes[3]).toBeLessThan(INPUT_AXIS_MAX / 2);
  });

  it('maps a DualSense in Chrome (18 buttons incl. touchpad) and never sends PS or Create', () => {
    const dualsense = pad({
      id: 'DualSense Wireless Controller (STANDARD GAMEPAD Vendor: 054c Product: 0ce6)',
      buttons: Array.from({ length: 18 }, () => ({ pressed: false, value: 0 })),
      pressed: [BUTTON.home, BUTTON.create, BUTTON.touchpad, BUTTON.north],
    });
    expect(controllerFamily(dualsense.id)).toBe('dualsense');
    const result = normalizeGamepad(dualsense);
    expect(result.ok && pressedButtons(result.state.buttons)).toEqual(['north', 'touchpad']);
    expect(result.ok && (result.state.buttons & BLOCKED_BUTTONS_MASK)).toBe(0);
    expect(controllerFamily('Wireless Controller (STANDARD GAMEPAD Vendor: 054c Product: 09cc)')).toBe('dualshock4');
    expect(controllerFamily('Xbox Wireless Controller (STANDARD GAMEPAD Vendor: 045e Product: 0b13)')).toBe('xbox');
    expect(controllerFamily('8BitDo Pro 2')).toBe('generic');
  });

  it('applies stick dead zones with rescaling and clamps out-of-range values', () => {
    expect(applyStickDeadZone(0.1, 0.05, 0.12)).toEqual([0, 0]);
    const [x] = applyStickDeadZone(0.13, 0, 0.12);
    expect(x).toBeGreaterThan(0);
    expect(x).toBeLessThan(0.02);
    expect(applyStickDeadZone(1, 0, 0.12)[0]).toBeCloseTo(1);
    // A corner of a square gate is clamped to the unit circle.
    const [cx, cy] = applyStickDeadZone(1, 1, 0.12);
    expect(Math.hypot(cx, cy)).toBeLessThanOrEqual(1.0000001);
    const result = normalizeGamepad(pad({ axes: [5, Number.NaN, -Infinity, 0.05] }));
    expect(result.ok && result.state.axes).toEqual([INPUT_AXIS_MAX, 0, 0, 0]);
  });

  it('maps analog triggers with a dead zone and digital-only triggers as full presses', () => {
    const analog = normalizeGamepad(pad({ values: { [BUTTON.l2]: 0.02, [BUTTON.r2]: 0.6 }, pressed: [BUTTON.r2] }));
    expect(analog.ok && analog.state.triggers[0]).toBe(0);
    expect(analog.ok && analog.state.triggers[1]).toBeGreaterThan(140);
    expect(analog.ok && pressedButtons(analog.state.buttons)).toEqual(['r2']);
    const digital = normalizeGamepad(pad({ pressed: [BUTTON.l2], values: { [BUTTON.l2]: 0 } }));
    expect(digital.ok && digital.state.triggers[0]).toBe(255);
    expect(digital.ok && pressedButtons(digital.state.buttons)).toEqual(['l2']);
  });

  it('refuses controllers without the standard layout and disconnected ones', () => {
    expect(normalizeGamepad(pad({ mapping: '' }))).toMatchObject({ ok: false });
    expect(normalizeGamepad(pad({ connected: false }))).toMatchObject({ ok: false });
  });
});

describe('packet codec', () => {
  it('round-trips a 32-byte packet', () => {
    const packet: InputPacket = { ...base, buttons: (1 << BUTTON.south) | (1 << BUTTON.r1), axes: [-32767, 32767, 1, -1], triggers: [0, 255] };
    const bytes = encodeInputPacket(packet);
    expect(bytes.byteLength).toBe(INPUT_PACKET_BYTES);
    expect(decodeInputPacket(bytes)).toEqual({ ok: true, packet });
  });

  it('clears blocked buttons when encoding and zeroes a release', () => {
    const encoded = decodeInputPacket(encodeInputPacket({ ...base, buttons: (1 << BUTTON.home) | (1 << BUTTON.south) }));
    expect(encoded.ok && encoded.packet.buttons).toBe(1 << BUTTON.south);
    const release = decodeInputPacket(encodeInputPacket({ ...base, kind: 'release', buttons: 1, axes: [9, 9, 9, 9], triggers: [9, 9] }));
    expect(release.ok && release.packet).toMatchObject({ kind: 'release', ...NEUTRAL_STATE });
  });

  it('refuses anything that is not exactly a version-1 packet', () => {
    const mutate = (offset: number, value: number) => {
      const bytes = encodeInputPacket(base);
      bytes[offset] = value;
      return decodeInputPacket(bytes);
    };
    expect(decodeInputPacket(new Uint8Array(31))).toEqual({ ok: false, error: 'size' });
    expect(decodeInputPacket(new Uint8Array(33))).toEqual({ ok: false, error: 'size' });
    expect(mutate(0, 0)).toEqual({ ok: false, error: 'magic' });
    expect(mutate(1, 2)).toEqual({ ok: false, error: 'version' });
    expect(mutate(2, 9)).toEqual({ ok: false, error: 'kind' });
    expect(mutate(3, 1)).toEqual({ ok: false, error: 'slot' });
    expect(mutate(30, 1)).toEqual({ ok: false, error: 'reserved' });
    expect(mutate(18, 0x80)).toEqual({ ok: false, error: 'buttons' }); // bit 23
    expect(mutate(18, 0x01)).toEqual({ ok: false, error: 'blocked-button' }); // bit 16 (PS)
    const axis = encodeInputPacket(base);
    new DataView(axis.buffer).setInt16(20, -32768, true);
    expect(decodeInputPacket(axis)).toEqual({ ok: false, error: 'axes' });
    const release = encodeInputPacket({ ...base, kind: 'release' });
    release[28] = 1;
    expect(decodeInputPacket(release)).toEqual({ ok: false, error: 'release-not-neutral' });
  });
});

describe('ordering, rate limit and watchdog', () => {
  it('never applies stale or duplicated packets and restarts per grant epoch', () => {
    const sequencer = new InputSequencer();
    expect(sequencer.accept(1, 1)).toBe(true);
    expect(sequencer.accept(1, 3)).toBe(true);
    expect(sequencer.accept(1, 2)).toBe(false); // arrived late
    expect(sequencer.accept(1, 3)).toBe(false); // duplicate
    expect(sequencer.accept(1, 0)).toBe(false);
    expect(sequencer.accept(2, 1)).toBe(true); // a new grant starts again at 1
    expect(sequencer.accept(1, 4)).toBe(true); // switching back resets as well (the server refuses old epochs)
    expect(sequencer).toMatchObject({ accepted: 4, stale: 3 });
  });

  it('allows the configured rate with a small burst', () => {
    const limiter = new InputRateLimiter(120, 30, 0);
    let allowed = 0;
    for (let index = 0; index < 100; index += 1) if (limiter.take(0)) allowed += 1;
    expect(allowed).toBe(30);
    expect(limiter.take(5)).toBe(false); // 0.6 tokens refilled
    expect(limiter.take(10)).toBe(true); // 1.2 tokens
    let sustained = 0;
    for (let t = 1000; t < 2000; t += 4) if (limiter.take(t)) sustained += 1; // 250 attempts in one second
    expect(sustained).toBeGreaterThanOrEqual(119);
    expect(sustained).toBeLessThanOrEqual(150);
  });

  it('returns to neutral after the watchdog timeout', () => {
    const pressed = { buttons: 1, axes: [100, 0, 0, 0] as [number, number, number, number], triggers: [0, 0] as [number, number] };
    expect(watchdogState(null, 1000)).toEqual({ state: NEUTRAL_STATE, expired: false });
    expect(watchdogState({ state: pressed, receivedAt: 1000 }, 1000 + INPUT_WATCHDOG_MS)).toEqual({ state: pressed, expired: false });
    expect(watchdogState({ state: pressed, receivedAt: 1000 }, 1001 + INPUT_WATCHDOG_MS)).toEqual({ state: NEUTRAL_STATE, expired: true });
  });
});

describe('quality presets', () => {
  it('are ordered and capped by the server limit', () => {
    expect(qualitiesUpTo('720p30')).toEqual(['480p30', '720p30']);
    expect(qualitiesUpTo('1080p60')).toHaveLength(5);
    for (const preset of Object.values(SHAREPLAY_QUALITY_PRESETS)) {
      expect(preset.width / preset.height).toBeCloseTo(16 / 9, 1);
      if (preset.lowLayer) expect(preset.lowLayer.maxBitrate).toBeLessThan(preset.maxBitrate);
    }
  });
});
