/**
 * Voice & Audio preferences of this device (localStorage). Devices differ per computer or
 * phone, so nothing here is stored on the server; microphone audio is never stored anywhere.
 */
export type MicMode = 'open' | 'voice' | 'ptt';

export interface VoicePreferences {
  inputDeviceId: string;
  outputDeviceId: string;
  /** Microphone volume, 0–2 (1 = unchanged). */
  inputGain: number;
  /** Volume of everyone you hear, 0–1. */
  outputVolume: number;
  /** open: always transmit; voice: transmit when louder than the sensitivity; ptt: while the key is held. */
  mode: MicMode;
  /** Voice-activity threshold, 0 (most sensitive) – 1. */
  sensitivity: number;
  /** KeyboardEvent.code for push-to-talk. */
  pttKey: string;
  echoCancellation: boolean;
  noiseSuppression: boolean;
  autoGainControl: boolean;
  /** Per-person volume (0–1) by user ID. */
  userVolumes: Record<string, number>;
  /** People you muted for yourself, by user ID. */
  localMutes: Record<string, true>;
}

export const DEFAULT_VOICE_PREFERENCES: VoicePreferences = {
  inputDeviceId: '',
  outputDeviceId: '',
  inputGain: 1,
  outputVolume: 1,
  mode: 'open',
  sensitivity: 0.25,
  pttKey: 'KeyV',
  echoCancellation: true,
  noiseSuppression: true,
  autoGainControl: true,
  userVolumes: {},
  localMutes: {},
};

const KEY = 'akeno.voice';
const listeners = new Set<(prefs: VoicePreferences) => void>();

function clamp(value: unknown, min: number, max: number, fallback: number): number {
  return typeof value === 'number' && Number.isFinite(value) ? Math.min(max, Math.max(min, value)) : fallback;
}

function isRecord(value: unknown): value is Record<string, unknown> {
  return typeof value === 'object' && value !== null && !Array.isArray(value);
}

/** iPhone and iPad browsers ignore media element volume (only the device buttons change it). */
export function playbackVolumeAdjustable(): boolean {
  try {
    const probe = new Audio();
    probe.volume = 0.5;
    return probe.volume === 0.5;
  } catch {
    return false;
  }
}

export function loadVoicePreferences(): VoicePreferences {
  let stored: Partial<VoicePreferences>;
  try {
    stored = JSON.parse(window.localStorage.getItem(KEY) ?? '{}') as Partial<VoicePreferences>;
  } catch {
    stored = {};
  }
  const d = DEFAULT_VOICE_PREFERENCES;
  return {
    inputDeviceId: typeof stored.inputDeviceId === 'string' ? stored.inputDeviceId : d.inputDeviceId,
    outputDeviceId: typeof stored.outputDeviceId === 'string' ? stored.outputDeviceId : d.outputDeviceId,
    inputGain: clamp(stored.inputGain, 0, 2, d.inputGain),
    outputVolume: clamp(stored.outputVolume, 0, 1, d.outputVolume),
    mode: stored.mode === 'voice' || stored.mode === 'ptt' || stored.mode === 'open' ? stored.mode : d.mode,
    sensitivity: clamp(stored.sensitivity, 0, 1, d.sensitivity),
    pttKey: typeof stored.pttKey === 'string' && /^[A-Za-z0-9]{2,20}$/.test(stored.pttKey) ? stored.pttKey : d.pttKey,
    echoCancellation: typeof stored.echoCancellation === 'boolean' ? stored.echoCancellation : d.echoCancellation,
    noiseSuppression: typeof stored.noiseSuppression === 'boolean' ? stored.noiseSuppression : d.noiseSuppression,
    autoGainControl: typeof stored.autoGainControl === 'boolean' ? stored.autoGainControl : d.autoGainControl,
    userVolumes: Object.fromEntries(
      Object.entries(isRecord(stored.userVolumes) ? stored.userVolumes : {})
        .filter(([, value]) => typeof value === 'number' && Number.isFinite(value))
        .map(([id, value]) => [id, clamp(value, 0, 1, 1)]),
    ),
    localMutes: Object.fromEntries(Object.entries(isRecord(stored.localMutes) ? stored.localMutes : {}).filter(([, value]) => value === true)) as Record<string, true>,
  };
}

export function saveVoicePreferences(patch: Partial<VoicePreferences>): VoicePreferences {
  const next = { ...loadVoicePreferences(), ...patch };
  try {
    window.localStorage.setItem(KEY, JSON.stringify(next));
  } catch {
    // Storage unavailable: the change applies to this visit only.
  }
  for (const listener of listeners) listener(next);
  return next;
}

export function resetVoicePreferences(): VoicePreferences {
  return saveVoicePreferences({ ...DEFAULT_VOICE_PREFERENCES, userVolumes: {}, localMutes: {} });
}

export function onVoicePreferences(listener: (prefs: VoicePreferences) => void): () => void {
  listeners.add(listener);
  return () => listeners.delete(listener);
}

/** "KeyV" → "V", "Space" → "Space", "Backquote" → "`". */
export function keyLabel(code: string): string {
  if (code.startsWith('Key')) return code.slice(3);
  if (code.startsWith('Digit')) return code.slice(5);
  const names: Record<string, string> = { Backquote: '`', CapsLock: 'Caps Lock', ShiftLeft: 'Left Shift', ControlLeft: 'Left Ctrl', AltLeft: 'Left Alt' };
  return names[code] ?? code;
}

/** Microphone capture constraints from the preferences. */
export function captureOptions(prefs: VoicePreferences) {
  return {
    ...(prefs.inputDeviceId ? { deviceId: prefs.inputDeviceId } : {}),
    echoCancellation: prefs.echoCancellation,
    noiseSuppression: prefs.noiseSuppression,
    autoGainControl: prefs.autoGainControl,
    channelCount: 1,
  };
}
