// Core PCM is signed stereo at 44.1 kHz. Resample when the AudioContext uses
// another rate, and bound queue latency instead of accumulating seconds of audio.
class PlaydateAudio extends AudioWorkletProcessor {
  constructor() {
    super();
    this.buffer = new Float32Array(32768);
    this.read = 0;
    this.write = 0;
    this.phase = 0;
    this.port.onmessage = event => {
      if (event.data.type === 'reset') { this.read = this.write = this.phase = 0; return; }
      const samples = event.data.samples;
      if (!samples) return;
      for (let i = 0; i < samples.length; i++) this.buffer[(this.write + i) % this.buffer.length] = samples[i] / 32768;
      this.write += samples.length;
      if (this.write - this.read > 13230) { this.read = this.write - 8820; this.phase = 0; }
    };
  }
  process(_, outputs) {
    const output = outputs[0];
    const ratio = 44100 / sampleRate;
    for (let i = 0; i < output[0].length; i++) {
      if (this.write - this.read < 4) {
        for (const channel of output) channel[i] = 0;
        continue;
      }
      for (let ch = 0; ch < output.length; ch++) {
        const a = this.buffer[(this.read + Math.min(ch, 1)) % this.buffer.length];
        const b = this.buffer[(this.read + 2 + Math.min(ch, 1)) % this.buffer.length];
        output[ch][i] = a + (b - a) * this.phase;
      }
      this.phase += ratio;
      const consumed = Math.floor(this.phase);
      this.read += consumed * 2;
      this.phase -= consumed;
    }
    return true;
  }
}
registerProcessor('playdate-audio', PlaydateAudio);
