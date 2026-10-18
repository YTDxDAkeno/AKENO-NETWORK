import { FRAME_SAMPLES } from './protocol.js';

/** 0..100 meter for a block (RMS, -60..0 dBFS), as the console shows it. */
export function levelOf(pcm: Int16Array): number {
  if (pcm.length === 0) return 0;
  let acc = 0;
  for (const v of pcm) acc += v * v;
  const rms = Math.sqrt(acc / pcm.length) / 32768;
  if (rms < 1e-5) return 0;
  const db = 20 * Math.log10(rms);
  return Math.max(0, Math.min(100, Math.round(((db + 60) * 100) / 60)));
}

/**
 * Mixes the other participants' audio for one console: every remote audio track feeds 20 ms
 * frames into its own short queue; each tick takes at most one frame per source, sums them with
 * saturation and reports per-source levels (speaking indicators). Queues are bounded so a source
 * that runs ahead cannot add delay.
 */
export class Mixer {
  private readonly queues = new Map<string, Int16Array[]>();
  private readonly levels = new Map<string, number>();
  static readonly MAX_QUEUE = 5;

  add(source: string): void {
    if (!this.queues.has(source)) this.queues.set(source, []);
  }

  remove(source: string): void {
    this.queues.delete(source);
    this.levels.delete(source);
  }

  get sources(): number {
    return this.queues.size;
  }

  push(source: string, frame: Int16Array): void {
    const queue = this.queues.get(source);
    if (!queue || frame.length !== FRAME_SAMPLES) return;
    queue.push(frame);
    while (queue.length > Mixer.MAX_QUEUE) queue.shift();
  }

  level(source: string): number {
    return this.levels.get(source) ?? 0;
  }

  /** One 20 ms output frame, or null when no source had audio. */
  tick(): Int16Array | null {
    let mixed: Int32Array | null = null;
    for (const [source, queue] of this.queues) {
      const frame = queue.shift();
      if (!frame) {
        this.levels.set(source, 0);
        continue;
      }
      this.levels.set(source, levelOf(frame));
      mixed ??= new Int32Array(FRAME_SAMPLES);
      for (let i = 0; i < FRAME_SAMPLES; i++) mixed[i] = (mixed[i] ?? 0) + (frame[i] ?? 0);
    }
    if (!mixed) return null;
    const out = new Int16Array(FRAME_SAMPLES);
    for (let i = 0; i < FRAME_SAMPLES; i++) out[i] = Math.max(-32768, Math.min(32767, mixed[i] ?? 0));
    return out;
  }
}
