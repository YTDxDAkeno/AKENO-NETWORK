/*
 * AKENO microphone processor (AudioWorklet, runs on the audio thread so it is not throttled in
 * background tabs). Applies the input gain and opens or closes the microphone:
 *   open  - always open
 *   voice - open while the level is above the threshold (with a short hold so words are not cut)
 *   ptt   - open while push-to-talk is held
 * Posts the input level about 20 times per second for meters (0 = -60 dBFS or quieter, 1 = 0 dBFS).
 * Audio is never stored.
 */
class AkenoGateProcessor extends AudioWorkletProcessor {
  constructor() {
    super();
    this.gain = 1;
    this.mode = 'open';
    this.threshold = 0.02;
    this.pttOpen = false;
    this.current = 1;
    this.holdUntil = 0;
    this.level = 0;
    this.lastPost = 0;
    this.port.onmessage = (event) => {
      const data = event.data || {};
      if (typeof data.gain === 'number') this.gain = data.gain;
      if (data.mode === 'open' || data.mode === 'voice' || data.mode === 'ptt') this.mode = data.mode;
      if (typeof data.threshold === 'number') this.threshold = data.threshold;
      if (typeof data.pttOpen === 'boolean') this.pttOpen = data.pttOpen;
    };
  }

  process(inputs, outputs) {
    const input = inputs[0];
    const output = outputs[0];
    if (!input || input.length === 0 || !output) return true;
    const channel = input[0];
    let sum = 0;
    for (let i = 0; i < channel.length; i++) sum += channel[i] * channel[i];
    const rms = Math.sqrt(sum / Math.max(1, channel.length));
    // Smoothed level for meters (fast attack, slow release).
    this.level = rms > this.level ? rms : this.level * 0.92 + rms * 0.08;

    const now = currentTime;
    let open = true;
    if (this.mode === 'ptt') open = this.pttOpen;
    else if (this.mode === 'voice') {
      if (rms >= this.threshold) this.holdUntil = now + 0.35;
      open = now < this.holdUntil;
    }
    const target = open ? this.gain : 0;
    for (let c = 0; c < output.length; c++) {
      const src = input[c] || channel;
      const dst = output[c];
      for (let i = 0; i < dst.length; i++) {
        // Short ramps avoid clicks when the gate opens or closes.
        this.current += (target - this.current) * 0.02;
        dst[i] = src[i] * this.current;
      }
    }
    if (now - this.lastPost > 0.05) {
      this.lastPost = now;
      const db = 20 * Math.log10(Math.max(this.level, 1e-6));
      this.port.postMessage({ level: Math.min(1, Math.max(0, (db + 60) / 60)), open });
    }
    return true;
  }
}

registerProcessor('akeno-gate', AkenoGateProcessor);
