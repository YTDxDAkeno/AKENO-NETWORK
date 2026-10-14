// Emitted as a same-origin file: the CSP (script-src 'self') does not allow data: modules.
import workletUrl from './gate-worklet.js?url&no-inline';

export interface MicSettings {
  gain: number;
  mode: 'open' | 'voice' | 'ptt';
  /** 0 (most sensitive) … 1. */
  sensitivity: number;
  pttOpen: boolean;
}

/** Maps the 0..1 sensitivity slider to an RMS threshold (roughly -60 dBFS … -20 dBFS). */
export function thresholdFor(sensitivity: number): number {
  return 10 ** ((-60 + 40 * Math.min(1, Math.max(0, sensitivity))) / 20);
}

/** Where the sensitivity threshold sits on the level meters (same -60…0 dBFS scale as the level). */
export function meterPositionFor(sensitivity: number): number {
  return (40 * Math.min(1, Math.max(0, sensitivity))) / 60;
}

const loaded = new WeakSet<BaseAudioContext>();

/** Loads the processor module into context once; fails after timeoutMs (for example when the file cannot be fetched). */
export async function loadMicWorklet(context: BaseAudioContext, timeoutMs = 8000): Promise<void> {
  if (loaded.has(context)) return;
  let timer: ReturnType<typeof setTimeout> | undefined;
  try {
    await Promise.race([
      context.audioWorklet.addModule(workletUrl),
      new Promise<never>((_, reject) => {
        timer = setTimeout(() => reject(new Error('The microphone processor did not load in time.')), timeoutMs);
      }),
    ]);
  } finally {
    clearTimeout(timer);
  }
  loaded.add(context);
}

/**
 * Builds the microphone processing graph: source → AudioWorklet (gain + gate) → destination.
 * Implements LiveKit's TrackProcessor contract so it can sit on the published microphone track,
 * and is reused by the "Test microphone" view. Nothing is recorded.
 */
export class MicProcessor {
  readonly name = 'akeno-mic';
  processedTrack?: MediaStreamTrack;
  /** Input level on a -60…0 dBFS scale (0..1) and whether the gate is open (updated ~20×/s). */
  level = 0;
  open = true;
  onLevel: ((level: number, open: boolean) => void) | null = null;

  private context: AudioContext | null = null;
  private ownsContext = false;
  private source: MediaStreamAudioSourceNode | null = null;
  private node: AudioWorkletNode | null = null;
  private destination: MediaStreamAudioDestinationNode | null = null;

  constructor(private settings: MicSettings) {}

  async init(opts: { track: MediaStreamTrack; audioContext?: AudioContext }): Promise<void> {
    this.ownsContext = !opts.audioContext;
    const context = opts.audioContext ?? new AudioContext({ latencyHint: 'interactive' });
    this.context = context;
    await loadMicWorklet(context);
    this.source = context.createMediaStreamSource(new MediaStream([opts.track]));
    this.node = new AudioWorkletNode(context, 'akeno-gate', { numberOfInputs: 1, numberOfOutputs: 1, channelCount: 1, channelCountMode: 'explicit' });
    this.node.port.onmessage = (event: MessageEvent<{ level: number; open: boolean }>) => {
      this.level = event.data.level;
      this.open = event.data.open;
      this.onLevel?.(this.level, this.open);
    };
    this.destination = context.createMediaStreamDestination();
    this.source.connect(this.node);
    this.node.connect(this.destination);
    this.processedTrack = this.destination.stream.getAudioTracks()[0];
    this.update(this.settings);
    if (context.state === 'suspended') await context.resume().catch(() => {});
  }

  async restart(opts: { track: MediaStreamTrack; audioContext?: AudioContext }): Promise<void> {
    await this.destroy();
    await this.init(opts);
  }

  async destroy(): Promise<void> {
    this.node?.port.close();
    this.source?.disconnect();
    this.node?.disconnect();
    this.processedTrack?.stop();
    if (this.ownsContext) await this.context?.close().catch(() => {});
    this.source = null;
    this.node = null;
    this.destination = null;
    this.context = null;
    this.processedTrack = undefined;
  }

  update(settings: Partial<MicSettings>): void {
    this.settings = { ...this.settings, ...settings };
    this.node?.port.postMessage({
      gain: this.settings.gain,
      mode: this.settings.mode,
      threshold: thresholdFor(this.settings.sensitivity),
      pttOpen: this.settings.pttOpen,
    });
  }
}
