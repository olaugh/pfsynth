// pfsynth AudioWorklet: hosts the WebAssembly synth (docs/pfsynth.wasm, built from src/ by
// tools/build_wasm.sh) and renders 128-frame blocks on the audio thread.
class PfProcessor extends AudioWorkletProcessor {
  constructor() {
    super();
    this.ready = false; this.playing = false; this.ex = null;
    this.blocks = 0; this.busyMs = 0; this.loadWindow = 0; this.load = 0;
    this.port.onmessage = (e) => this.onMessage(e.data);
  }
  wasiStubs() {
    const mem = () => new DataView(this.ex.memory.buffer);
    return {
      clock_time_get: (id, prec, out) => { mem().setBigUint64(out, BigInt(Math.round(Date.now() * 1e6)), true); return 0; },
      fd_close: () => 0, fd_seek: () => 0, fd_prestat_get: () => 8, fd_prestat_dir_name: () => 8,
      fd_write: (fd, iovs, n, nwritten) => {   // stderr from the MIDI loader -> console
        const dv = mem(); let s = '', total = 0;
        for (let i = 0; i < n; i++) { const p = dv.getUint32(iovs + i * 8, true), l = dv.getUint32(iovs + i * 8 + 4, true); s += new TextDecoder().decode(new Uint8Array(this.ex.memory.buffer, p, l)); total += l; }
        dv.setUint32(nwritten, total, true); if (s.trim()) this.port.postMessage({ type: 'log', text: s }); return 0;
      },
      proc_exit: (c) => { throw new Error('wasm exit ' + c); },
    };
  }
  async onMessage(m) {
    switch (m.type) {
      case 'wasm': {
        const { instance } = await WebAssembly.instantiate(m.bytes, { wasi_snapshot_preview1: this.wasiStubs() });
        this.ex = instance.exports; this.ex._initialize(); this.ex.pfw_init(sampleRate); this.ready = true;
        const defaults = []; for (let i = 0; i < 16; i++) defaults.push(this.ex.pfw_default(i));
        this.port.postMessage({ type: 'ready', sampleRate, defaults });
        break;
      }
      case 'load': {
        if (!this.ready) return;
        const bytes = new Uint8Array(m.bytes);
        if (bytes.length > this.ex.pfw_midi_capacity()) { this.port.postMessage({ type: 'error', text: 'MIDI file too large' }); return; }
        new Uint8Array(this.ex.memory.buffer, this.ex.pfw_midi_buffer(), bytes.length).set(bytes);
        const n = this.ex.pfw_load(bytes.length);
        if (n < 0) { this.port.postMessage({ type: 'error', text: 'could not parse the MIDI file' }); return; }
        const ep = this.ex.pfw_events(), sz = this.ex.pfw_event_size(), dv = new DataView(this.ex.memory.buffer);
        const t = new Float64Array(n), type = new Uint8Array(n), note = new Uint8Array(n), val = new Uint8Array(n);
        for (let i = 0; i < n; i++) { const o = ep + i * sz; t[i] = dv.getFloat64(o, true); type[i] = dv.getUint8(o + 8); note[i] = dv.getUint8(o + 9); val[i] = dv.getUint8(o + 10); }
        this.playing = false;
        this.port.postMessage({ type: 'loaded', token: m.token, n, duration: this.ex.pfw_duration(), t, etype: type, note, val }, [t.buffer, type.buffer, note.buffer, val.buffer]);
        this.status();
        break;
      }
      case 'play': if (this.ready) { if (this.ex.pfw_time() >= this.ex.pfw_duration()) this.ex.pfw_seek(0); this.playing = true; this.status(); } break;
      case 'pause': this.playing = false; this.status(); break;
      case 'seek': if (this.ready) { this.ex.pfw_seek(m.t); this.status(); } break;
      case 'set': if (this.ready) this.ex.pfw_set(m.id, m.value); break;
    }
  }
  status() {
    const keys = new Uint8Array(this.ex.memory.buffer, this.ex.pfw_sounding(), 128).slice();
    this.port.postMessage({ type: 'status', time: this.ex.pfw_time(), ctxTime: currentTime, playing: this.playing, active: this.ex.pfw_active(), load: this.load, keys }, [keys.buffer]);
  }
  process(inputs, outputs) {
    const out = outputs[0]; if (!out || !out.length) return true;
    if (!this.ready || !this.playing) return true;
    const n = out[0].length, t0 = Date.now();
    const p = this.ex.pfw_render(n);
    const buf = new Float32Array(this.ex.memory.buffer, p, n);
    for (let c = 0; c < out.length; c++) out[c].set(buf);
    this.busyMs += Date.now() - t0; this.loadWindow += n;
    if (this.loadWindow >= sampleRate / 2) { this.load = this.busyMs / (1000 * this.loadWindow / sampleRate); this.busyMs = 0; this.loadWindow = 0; }
    if (++this.blocks % 8 === 0) {
      if (this.ex.pfw_time() >= this.ex.pfw_duration()) { this.playing = false; this.port.postMessage({ type: 'ended' }); }
      this.status();
    }
    return true;
  }
}
registerProcessor('pfsynth', PfProcessor);
