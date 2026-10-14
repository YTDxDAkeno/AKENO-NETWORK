import { afterEach, describe, expect, it, vi } from 'vitest';
import { meterPositionFor, thresholdFor } from '../../src/features/voice/mic-processor';
import { microphoneErrorMessage } from '../../src/features/voice/voice-controller';
import {
  DEFAULT_VOICE_PREFERENCES,
  captureOptions,
  keyLabel,
  loadVoicePreferences,
  onVoicePreferences,
  resetVoicePreferences,
  saveVoicePreferences,
} from '../../src/features/voice/voice-preferences';

afterEach(() => window.localStorage.clear());

describe('voice preferences', () => {
  it('uses defaults when nothing is stored or storage is corrupt', () => {
    expect(loadVoicePreferences()).toEqual(DEFAULT_VOICE_PREFERENCES);
    window.localStorage.setItem('akeno.voice', '{not json');
    expect(loadVoicePreferences()).toEqual(DEFAULT_VOICE_PREFERENCES);
  });

  it('sanitizes stored values (ranges, types, unknown modes and keys)', () => {
    window.localStorage.setItem(
      'akeno.voice',
      JSON.stringify({
        inputGain: 9,
        outputVolume: 1.8,
        sensitivity: -1,
        mode: 'always',
        pttKey: '<script>',
        echoCancellation: 'yes',
        inputDeviceId: 42,
        userVolumes: { a: 1.5, b: -2, c: 'loud', d: 0.4 },
        localMutes: { a: true, b: 1, c: 'true' },
      }),
    );
    const prefs = loadVoicePreferences();
    expect(prefs.inputGain).toBe(2);
    expect(prefs.outputVolume).toBe(1);
    expect(prefs.sensitivity).toBe(0);
    expect(prefs.mode).toBe('open');
    expect(prefs.pttKey).toBe('KeyV');
    expect(prefs.echoCancellation).toBe(true);
    expect(prefs.inputDeviceId).toBe('');
    expect(prefs.userVolumes).toEqual({ a: 1, b: 0, d: 0.4 });
    expect(prefs.localMutes).toEqual({ a: true });

    window.localStorage.setItem('akeno.voice', JSON.stringify({ userVolumes: [1, 2], localMutes: null }));
    expect(loadVoicePreferences().userVolumes).toEqual({});
    expect(loadVoicePreferences().localMutes).toEqual({});
  });

  it('saves, notifies listeners and resets', () => {
    const listener = vi.fn();
    const off = onVoicePreferences(listener);
    saveVoicePreferences({ mode: 'ptt', pttKey: 'Space', localMutes: { u1: true } });
    expect(listener).toHaveBeenCalledWith(expect.objectContaining({ mode: 'ptt', pttKey: 'Space' }));
    expect(loadVoicePreferences()).toMatchObject({ mode: 'ptt', pttKey: 'Space', localMutes: { u1: true } });
    resetVoicePreferences();
    expect(loadVoicePreferences()).toEqual(DEFAULT_VOICE_PREFERENCES);
    off();
    saveVoicePreferences({ mode: 'voice' });
    expect(listener).toHaveBeenCalledTimes(2);
  });

  it('builds capture constraints and key labels', () => {
    expect(captureOptions(DEFAULT_VOICE_PREFERENCES)).toEqual({ echoCancellation: true, noiseSuppression: true, autoGainControl: true, channelCount: 1 });
    expect(captureOptions({ ...DEFAULT_VOICE_PREFERENCES, inputDeviceId: 'mic-2', noiseSuppression: false })).toMatchObject({ deviceId: 'mic-2', noiseSuppression: false });
    expect(keyLabel('KeyV')).toBe('V');
    expect(keyLabel('Digit4')).toBe('4');
    expect(keyLabel('Backquote')).toBe('`');
    expect(keyLabel('F13')).toBe('F13');
  });
});

describe('microphone processing', () => {
  it('maps sensitivity to a -60…-20 dBFS gate threshold', () => {
    expect(20 * Math.log10(thresholdFor(0))).toBeCloseTo(-60);
    expect(20 * Math.log10(thresholdFor(1))).toBeCloseTo(-20);
    expect(thresholdFor(5)).toBe(thresholdFor(1));
  });

  it('places the threshold marker on the same scale as the level meter', () => {
    for (const sensitivity of [0, 0.25, 0.5, 1]) {
      const meterLevelOfThreshold = (20 * Math.log10(thresholdFor(sensitivity)) + 60) / 60;
      expect(meterPositionFor(sensitivity)).toBeCloseTo(meterLevelOfThreshold);
    }
  });

  it('explains microphone failures in plain English', () => {
    expect(microphoneErrorMessage({ name: 'NotAllowedError' })).toMatch(/blocked/);
    expect(microphoneErrorMessage({ name: 'NotFoundError' })).toMatch(/No microphone/);
    expect(microphoneErrorMessage({ name: 'NotReadableError' })).toMatch(/in use/);
    expect(microphoneErrorMessage(new Error('x'))).toBe('The microphone could not be started.');
  });
});
