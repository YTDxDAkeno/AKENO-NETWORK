import { Headphones, Keyboard, Mic, PhoneOff, Play, RotateCcw, Square, Volume2 } from 'lucide-react';
import { useCallback, useEffect, useRef, useState } from 'react';
import { Button } from '../../components/Button';
import { Card, CardHeader } from '../../components/Card';
import { cn } from '../../components/cn';
import { SelectField, Switch } from '../../components/Field';
import { InlineError, Notice } from '../../components/States';
import { MicProcessor, meterPositionFor } from '../voice/mic-processor';
import { microphoneErrorMessage } from '../voice/voice-controller';
import { useVoice, useVoiceController } from '../voice/VoiceProvider';
import {
  captureOptions,
  keyLabel,
  loadVoicePreferences,
  onVoicePreferences,
  playbackVolumeAdjustable,
  resetVoicePreferences,
  saveVoicePreferences,
  type MicMode,
  type VoicePreferences,
} from '../voice/voice-preferences';

function usePrefs(): VoicePreferences {
  const [prefs, setPrefs] = useState(loadVoicePreferences);
  useEffect(() => onVoicePreferences(setPrefs), []);
  return prefs;
}

const OUTPUT_SELECTION = typeof HTMLMediaElement !== 'undefined' && 'setSinkId' in HTMLMediaElement.prototype;
const VOLUME_ADJUSTABLE = playbackVolumeAdjustable();

function useDevices() {
  const [devices, setDevices] = useState<MediaDeviceInfo[]>([]);
  const refresh = useCallback(async () => {
    if (!navigator.mediaDevices?.enumerateDevices) return;
    setDevices(await navigator.mediaDevices.enumerateDevices());
  }, []);
  useEffect(() => {
    // Initial listing; later updates come from devicechange (and refresh() after permission).
    navigator.mediaDevices?.enumerateDevices?.().then(setDevices, () => {});
    navigator.mediaDevices?.addEventListener?.('devicechange', refresh);
    return () => navigator.mediaDevices?.removeEventListener?.('devicechange', refresh);
  }, [refresh]);
  const inputs = devices.filter((device) => device.kind === 'audioinput');
  const outputs = devices.filter((device) => device.kind === 'audiooutput');
  // Labels are empty until the site has microphone permission.
  const labelled = inputs.some((device) => device.label);
  return { inputs, outputs, labelled, refresh };
}

function Slider({ label, value, min, max, step, onChange, format }: { label: string; value: number; min: number; max: number; step: number; onChange: (value: number) => void; format: (value: number) => string }) {
  return (
    <label className="flex flex-col gap-1.5 text-sm font-medium">
      <span className="flex justify-between">
        {label}
        <span className="font-normal text-muted">{format(value)}</span>
      </span>
      <input type="range" min={min} max={max} step={step} value={value} onChange={(event) => onChange(Number(event.target.value))} className="accent-[var(--akeno-accent)]" />
    </label>
  );
}

/** Live microphone test with the same processing as in voice (gain, gate). Never recorded. */
function MicrophoneTest({ prefs }: { prefs: VoicePreferences }) {
  const [running, setRunning] = useState(false);
  const [error, setError] = useState<string | null>(null);
  const [level, setLevel] = useState(0);
  const [open, setOpen] = useState(false);
  const [hear, setHear] = useState(false);
  const stream = useRef<MediaStream | null>(null);
  const processor = useRef<MicProcessor | null>(null);
  const audio = useRef<HTMLAudioElement | null>(null);
  const context = useRef<AudioContext | null>(null);

  const stop = useCallback(async () => {
    audio.current?.pause();
    audio.current = null;
    await processor.current?.destroy();
    processor.current = null;
    for (const track of stream.current?.getTracks() ?? []) track.stop();
    stream.current = null;
    await context.current?.close().catch(() => {});
    context.current = null;
    setRunning(false);
    setLevel(0);
  }, []);

  useEffect(() => () => void stop(), [stop]);
  useEffect(() => {
    processor.current?.update({ gain: prefs.inputGain, mode: prefs.mode === 'ptt' ? 'open' : prefs.mode, sensitivity: prefs.sensitivity });
  }, [prefs.inputGain, prefs.mode, prefs.sensitivity]);
  useEffect(() => {
    if (!running || !processor.current?.processedTrack) return;
    if (hear) {
      const element = new Audio();
      element.srcObject = new MediaStream([processor.current.processedTrack]);
      if (prefs.outputDeviceId && OUTPUT_SELECTION) void (element as HTMLAudioElement & { setSinkId: (id: string) => Promise<void> }).setSinkId(prefs.outputDeviceId).catch(() => {});
      void element.play().catch(() => {});
      audio.current = element;
    }
    return () => {
      audio.current?.pause();
      audio.current = null;
    };
  }, [hear, running, prefs.outputDeviceId]);

  const start = async () => {
    setError(null);
    // Created during the click, before any await: browsers (Safari in particular) only let audio
    // start from a user gesture, and a suspended context would leave the meter at zero.
    const audioContext = new AudioContext({ latencyHint: 'interactive' });
    void audioContext.resume().catch(() => {});
    context.current = audioContext;
    try {
      const media = await navigator.mediaDevices.getUserMedia({ audio: captureOptions(prefs) });
      stream.current = media;
      const next = new MicProcessor({ gain: prefs.inputGain, mode: prefs.mode === 'ptt' ? 'open' : prefs.mode, sensitivity: prefs.sensitivity, pttOpen: false });
      next.onLevel = (value, gateOpen) => {
        setLevel(value);
        setOpen(gateOpen);
      };
      await next.init({ track: media.getAudioTracks()[0]!, audioContext });
      processor.current = next;
      setRunning(true);
    } catch (failure) {
      setError(microphoneErrorMessage(failure));
      await stop();
    }
  };

  const thresholdPercent = meterPositionFor(prefs.sensitivity) * 100;
  return (
    <div className="flex flex-col gap-3 rounded-xl border border-line p-4" data-testid="mic-test">
      <div className="flex flex-wrap items-center gap-3">
        {running ? (
          <Button size="sm" variant="secondary" icon={<Square aria-hidden className="size-4" />} onClick={() => void stop()}>
            Stop test
          </Button>
        ) : (
          <Button size="sm" variant="secondary" icon={<Mic aria-hidden className="size-4" />} onClick={() => void start()}>
            Test microphone
          </Button>
        )}
        <span className="text-sm text-muted">{running ? (open ? 'Your voice would be sent' : 'Below the sensitivity: not sent') : 'Speak to see the level.'}</span>
      </div>
      <div className="relative h-3 overflow-hidden rounded-full bg-surface-3" role="meter" aria-label="Microphone level" aria-valuemin={0} aria-valuemax={100} aria-valuenow={Math.round(level * 100)}>
        <div className={cn('h-full rounded-full transition-[width] duration-75', open ? 'bg-status-online' : 'bg-subtle')} style={{ width: `${Math.round(level * 100)}%` }} />
        {prefs.mode === 'voice' ? <div className="absolute inset-y-0 w-0.5 bg-warning" style={{ left: `${thresholdPercent}%` }} title="Sensitivity" /> : null}
      </div>
      {running ? (
        <Switch label="Hear yourself" description="Plays your microphone back to you. Use headphones to avoid echo." checked={hear} onChange={setHear} />
      ) : null}
      <InlineError message={error} />
    </div>
  );
}

function PushToTalkKey({ value, onChange }: { value: string; onChange: (code: string) => void }) {
  const [listening, setListening] = useState(false);
  useEffect(() => {
    if (!listening) return;
    const capture = (event: KeyboardEvent) => {
      event.preventDefault();
      if (event.code !== 'Escape') onChange(event.code);
      setListening(false);
    };
    window.addEventListener('keydown', capture, { once: true, capture: true });
    return () => window.removeEventListener('keydown', capture, { capture: true });
  }, [listening, onChange]);
  return (
    <div className="flex items-center gap-3">
      <Button size="sm" variant="secondary" icon={<Keyboard aria-hidden className="size-4" />} onClick={() => setListening(true)}>
        {listening ? 'Press a key… (Esc cancels)' : `Push-to-talk key: ${keyLabel(value)}`}
      </Button>
    </div>
  );
}

function playTestSound(deviceId: string, volume: number): void {
  const context = new AudioContext();
  const oscillator = context.createOscillator();
  const gain = context.createGain();
  oscillator.frequency.value = 660;
  gain.gain.setValueAtTime(0, context.currentTime);
  gain.gain.linearRampToValueAtTime(0.2 * volume, context.currentTime + 0.05);
  gain.gain.linearRampToValueAtTime(0, context.currentTime + 0.6);
  oscillator.connect(gain);
  const sinkContext = context as AudioContext & { setSinkId?: (id: string) => Promise<void> };
  if (deviceId && sinkContext.setSinkId) void sinkContext.setSinkId(deviceId).catch(() => {});
  gain.connect(context.destination);
  oscillator.start();
  oscillator.stop(context.currentTime + 0.65);
  oscillator.onended = () => void context.close();
}

export function VoiceSettings() {
  const prefs = usePrefs();
  const voice = useVoice();
  const controller = useVoiceController();
  const { inputs, outputs, labelled, refresh } = useDevices();
  const [permissionError, setPermissionError] = useState<string | null>(null);
  const supported = typeof window !== 'undefined' && window.isSecureContext && Boolean(navigator.mediaDevices?.getUserMedia);
  const connected = voice && voice.status !== 'idle' && voice.status !== 'error';

  const allowDevices = async () => {
    setPermissionError(null);
    try {
      const media = await navigator.mediaDevices.getUserMedia({ audio: true });
      for (const track of media.getTracks()) track.stop();
      await refresh();
    } catch (error) {
      setPermissionError(microphoneErrorMessage(error));
    }
  };

  const percent = (value: number) => `${Math.round(value * 100)}%`;
  return (
    <div className="flex flex-col gap-6">
      {!supported ? <Notice tone="warning">This browser or connection cannot use the microphone. Voice needs HTTPS and a current browser.</Notice> : null}
      <Card>
        <CardHeader
          title="Microphone"
          icon={<Mic className="size-5" />}
          description="Settings are saved in this browser. AKENO never records, stores or analyses voice conversations."
          action={
            <Button size="sm" variant="ghost" icon={<RotateCcw aria-hidden className="size-4" />} onClick={() => resetVoicePreferences()}>
              Reset
            </Button>
          }
        />
        <div className="grid gap-5 p-5 md:grid-cols-2">
          <div className="flex flex-col gap-2">
            {labelled ? (
              <SelectField
                label="Input device"
                value={prefs.inputDeviceId}
                onChange={(event) => saveVoicePreferences({ inputDeviceId: event.target.value })}
                options={[{ value: '', label: 'System default' }, ...inputs.filter((d) => d.deviceId !== 'default').map((device) => ({ value: device.deviceId, label: device.label || 'Microphone' }))]}
              />
            ) : (
              <div className="flex flex-col gap-1.5">
                <p className="text-sm font-medium">Input device</p>
                <Button size="sm" variant="secondary" className="w-fit" onClick={() => void allowDevices()} disabled={!supported}>
                  Allow microphone access to choose a device
                </Button>
                <InlineError message={permissionError} />
              </div>
            )}
          </div>
          <Slider label="Input volume" value={prefs.inputGain} min={0} max={2} step={0.05} onChange={(inputGain) => saveVoicePreferences({ inputGain })} format={percent} />
          <fieldset className="md:col-span-2">
            <legend className="mb-2 text-sm font-medium">When to send your voice</legend>
            <div className="grid gap-2 sm:grid-cols-3" role="radiogroup">
              {(
                [
                  ['open', 'Always (open mic)', 'Your microphone is live while you are unmuted.'],
                  ['voice', 'Voice activity', 'Sent only when you speak louder than the sensitivity.'],
                  ['ptt', 'Push-to-talk', 'Sent only while you hold the key or the on-screen button.'],
                ] as Array<[MicMode, string, string]>
              ).map(([value, label, description]) => (
                <button
                  key={value}
                  type="button"
                  role="radio"
                  aria-checked={prefs.mode === value}
                  onClick={() => saveVoicePreferences({ mode: value })}
                  className={cn('rounded-xl border p-3 text-left', prefs.mode === value ? 'border-accent bg-accent-soft' : 'border-line hover:bg-surface-2')}
                >
                  <span className="block text-sm font-medium text-ink">{label}</span>
                  <span className="mt-0.5 block text-xs text-muted">{description}</span>
                </button>
              ))}
            </div>
          </fieldset>
          {prefs.mode === 'voice' ? (
            <Slider
              label="Microphone sensitivity"
              value={prefs.sensitivity}
              min={0}
              max={1}
              step={0.01}
              onChange={(sensitivity) => saveVoicePreferences({ sensitivity })}
              format={(value) => (value < 0.2 ? 'Very sensitive' : value < 0.5 ? 'Normal' : value < 0.8 ? 'Strict' : 'Very strict')}
            />
          ) : null}
          {prefs.mode === 'ptt' ? (
            <div className="flex flex-col gap-1.5">
              <PushToTalkKey value={prefs.pttKey} onChange={(pttKey) => saveVoicePreferences({ pttKey })} />
              <p className="text-xs text-muted">
                Works while the AKENO tab is focused (browsers do not allow global hotkeys). On phones and tablets, hold the on-screen “Hold to talk” button.
              </p>
            </div>
          ) : null}
          <div className="md:col-span-2">
            <MicrophoneTest prefs={prefs} />
          </div>
          <div className="flex flex-col gap-4 md:col-span-2">
            <p className="text-sm font-medium">Browser audio processing</p>
            <Switch label="Echo cancellation" description="Removes your speakers' sound from your microphone." checked={prefs.echoCancellation} onChange={(echoCancellation) => saveVoicePreferences({ echoCancellation })} />
            <Switch label="Noise suppression" description="Reduces steady background noise (provided by your browser)." checked={prefs.noiseSuppression} onChange={(noiseSuppression) => saveVoicePreferences({ noiseSuppression })} />
            <Switch label="Automatic gain control" description="Keeps your volume steady. Turn it off if you set the input volume yourself." checked={prefs.autoGainControl} onChange={(autoGainControl) => saveVoicePreferences({ autoGainControl })} />
            <p className="text-xs text-subtle">These use the browser's built-in processing; how well each works depends on the browser and device.</p>
          </div>
        </div>
      </Card>

      <Card>
        <CardHeader title="Speaker" icon={<Headphones className="size-5" />} />
        <div className="grid gap-5 p-5 md:grid-cols-2">
          {OUTPUT_SELECTION && labelled && outputs.length > 0 ? (
            <SelectField
              label="Output device"
              value={prefs.outputDeviceId}
              onChange={(event) => saveVoicePreferences({ outputDeviceId: event.target.value })}
              options={[{ value: '', label: 'System default' }, ...outputs.filter((d) => d.deviceId !== 'default').map((device) => ({ value: device.deviceId, label: device.label || 'Speaker' }))]}
            />
          ) : (
            <p className="text-sm text-muted">
              {OUTPUT_SELECTION ? 'Allow microphone access above to list speakers.' : 'This browser plays voice on the system’s current output (it cannot choose a speaker).'}
            </p>
          )}
          {VOLUME_ADJUSTABLE ? (
            <Slider label="Output volume" value={prefs.outputVolume} min={0} max={1} step={0.05} onChange={(outputVolume) => saveVoicePreferences({ outputVolume })} format={percent} />
          ) : (
            <p className="text-sm text-muted">This browser does not let websites change playback volume: use your device’s volume buttons. “Mute for me” on a party member still works.</p>
          )}
          <Button size="sm" variant="secondary" className="w-fit" icon={<Play aria-hidden className="size-4" />} onClick={() => playTestSound(prefs.outputDeviceId, prefs.outputVolume)}>
            Play test sound
          </Button>
        </div>
      </Card>

      <Card>
        <CardHeader title="Voice connection" icon={<Volume2 className="size-5" />} />
        <div className="flex flex-col gap-4 p-5">
          {connected ? (
            <Button variant="secondary" className="w-fit text-danger" icon={<PhoneOff aria-hidden className="size-4" />} onClick={() => void controller?.leave()}>
              Disconnect from voice
            </Button>
          ) : (
            <p className="text-sm text-muted">You are not connected to voice. Join from your party.</p>
          )}
          <Notice>
            <strong className="font-medium">Phones and tablets:</strong> voice works in current mobile browsers while AKENO is open. When you switch apps or
            lock the screen, iOS and Android may pause the browser and its microphone; AKENO reconnects when you come back. Uninterrupted
            background voice would need a native app, which AKENO does not have yet.
          </Notice>
        </div>
      </Card>
    </div>
  );
}
