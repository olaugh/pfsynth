// pfsynth guitar AudioWorklet: hosts pfguitar.wasm (built from src/ by
// tools/build_guitar_wasm.sh) and renders the guitar's string signal on the audio thread. The
// page applies the body and room responses (ConvolverNodes) after this node.
const NOTE = 48;
class GuitarProcessor extends AudioWorkletProcessor {
  constructor() {
    super();
    this.ex = null; this.playing = false; this.duration = 0; this.blocks = 0; this.tempo = 1; this.hasScore = false;
    this.busy = 0; this.span = 0; this.load = 0;   // audio-thread CPU: render time / real time, over ~0.5 s
    this.clock = globalThis.performance && performance.now ? () => performance.now() : () => Date.now();
    this.port.onmessage = (e) => this.onMessage(e.data).catch(err => { this.playing = false; this.port.postMessage({type:'error',text:err.message}); });
  }
  cstr(p) { const u = new Uint8Array(this.ex.memory.buffer); let s = ''; while (u[p]) s += String.fromCharCode(u[p++]); return decodeURIComponent(escape(s)); }
  async onMessage(m) {
    if (m.type === 'wasm') {
      const stub = () => 0, wasi = new Proxy({}, { get: (t, k) => k === 'proc_exit' ? (c) => { throw new Error('wasm exit ' + c); } : stub });
      const { instance } = await WebAssembly.instantiate(m.bytes, { wasi_snapshot_preview1: wasi });
      this.ex = instance.exports; if (this.ex._initialize) this.ex._initialize();
      if (this.ex.pfiw_init(1, sampleRate)) { this.port.postMessage({ type: 'error', text: 'guitar did not initialise' }); return; }
      this.soundingSize = this.ex.pfiw_sounding_size();
      if (this.ex.pfiw_note_size() !== NOTE || ![16,20].includes(this.soundingSize) || this.ex.pfiw_guitar_input_size() !== 24) { this.port.postMessage({ type: 'error', text: 'score layout mismatch' }); return; }
      const params = [];
      for (let i = 0; i < this.ex.pfiw_param_count(); i++) params.push({ index: i, name: this.cstr(this.ex.pfiw_param_name(i)), unit: this.cstr(this.ex.pfiw_param_unit(i)), group: this.cstr(this.ex.pfiw_param_group(i)),
        min: this.ex.pfiw_param_min(i), max: this.ex.pfiw_param_max(i), def: this.ex.pfiw_param_default(i), integer: !!this.ex.pfiw_param_integer(i), value: this.ex.pfiw_get(i) });
      this.port.postMessage({ type: 'ready', sampleRate, params });
    } else if (m.type === 'score') {
      const ex = this.ex, n = m.notes.length, base = ex.pfiw_notes(), profiles = m.loadingProfiles || [];
      if (n > ex.pfiw_max_notes() || profiles.length > ex.pfiw_guitar_max_loading()) { this.port.postMessage({type:'error', text:'Score exceeds guitar capacity.'}); return; }
      const dv = new DataView(ex.memory.buffer);
      const ib = ex.pfiw_guitar_inputs(), lb = ex.pfiw_guitar_loading();
      for (let k = 0; k < profiles.length; k++) {
        if (profiles[k].length !== 80 || profiles[k].some(v => !Number.isFinite(v) || v < 0 || v > 10)) { this.port.postMessage({type:'error',text:'Invalid string loading profile.'}); return; }
        profiles[k].forEach((v,j) => dv.setFloat32(lb + (k * 80 + j) * 4, v, true));
      }
      ex.pfiw_guitar_set_loading(m.loadingEnabled === false ? 0 : profiles.length);
      m.notes.slice(0, n).forEach((x, k) => {
        const o = base + NOTE * k;
        dv.setFloat64(o, x.start, true); dv.setFloat64(o + 8, x.end, true);
        dv.setFloat32(o + 16, x.pitch, true); dv.setFloat32(o + 20, x.velocity, true); dv.setFloat32(o + 24, x.art_param || 0, true); dv.setFloat32(o + 28, x.slide_to || 0, true);
        dv.setInt32(o + 32, 0, true); dv.setInt32(o + 36, 0, true);
        dv.setInt8(o + 40, x.string ?? -1); dv.setInt8(o + 41, x.fret ?? -1); dv.setInt8(o + 42, x.finger ?? -1); dv.setUint8(o + 43, x.articulation || 0); dv.setInt32(o + 44, 0, true);
        dv.setFloat64(ib + 24*k, x.pluck_position || 0, true);
        dv.setFloat64(ib + 24*k + 8, x.pluck_tilt || 0, true);
        dv.setInt32(ib + 24*k + 16, Number.isInteger(x.loadingProfile) && x.loadingProfile >= 0 && x.loadingProfile < profiles.length ? x.loadingProfile + 1 : 0, true);
        dv.setInt32(ib + 24*k + 20, 0, true);
      });
      const tun = ex.pfiw_tuning(); (m.tuning || [64, 59, 55, 50, 45, 40]).forEach((v, k) => dv.setInt8(tun + k, v));
      let err = ex.pfiw_load(n, 0, 0, 6, m.duration);
      this.tempo = m.tempo ?? 1;
      if (!err) err = ex.pfiw_tempo(this.tempo);
      this.hasScore = !err; this.duration = m.duration / this.tempo; this.playing = false;
      this.port.postMessage({ type: 'loaded', ok: !err, duration: this.duration, notes: n });
      if (m.seek) ex.pfiw_seek(m.seek);
    } else if (m.type === 'tempo') {
      if (!this.hasScore) return; // loadScore will supply the current slider value
      const previous = this.tempo;
      if (this.ex.pfiw_tempo(m.rate)) throw new Error('Invalid practice tempo.');
      this.tempo = m.rate; this.duration *= previous / this.tempo;
      this.port.postMessage({type:'tempo', tempo:this.tempo, t:this.ex.pfiw_time(), duration:this.duration});
    } else if (m.type === 'loading-enabled') { if (this.ex) this.ex.pfiw_guitar_set_loading(m.count); }
    else if (m.type === 'play') this.playing = !!this.ex;
    else if (m.type === 'pause') this.playing = false;
    else if (m.type === 'seek') { if (this.ex) this.ex.pfiw_seek(m.t); }
    else if (m.type === 'param') { if (this.ex) this.ex.pfiw_set(m.index, m.value); }
  }
  process(inputs, outputs) {
    const out = outputs[0]; if (!this.ex || !this.playing) return true;
    const n = out[0].length, ex = this.ex, t0 = this.clock();
    ex.pfiw_render(n);
    out[0].set(new Float32Array(ex.memory.buffer, ex.pfiw_left(), n));
    if (out[1]) out[1].set(new Float32Array(ex.memory.buffer, ex.pfiw_right(), n));
    this.busy += this.clock() - t0; this.span += n;
    if (this.span >= sampleRate / 2) { this.load = this.busy / (1000 * this.span / sampleRate); this.busy = 0; this.span = 0; }
    if (++this.blocks % 6 === 0) {
      const t = ex.pfiw_time(), c = ex.pfiw_sounding(), b = ex.pfiw_sounding_buffer(), dv = new DataView(ex.memory.buffer), s = [];
      for (let k = 0; k < c; k++) { const o = b + this.soundingSize * k; s.push([dv.getInt32(o, true), dv.getFloat32(o + 8, true), dv.getInt8(o + 12), dv.getInt8(o + 13)]); }
      this.port.postMessage({ type: 'tick', t, tempo:this.tempo, sounding: s, load: this.load });
      if (t >= this.duration) { this.playing = false; this.port.postMessage({ type: 'end' }); }
    }
    return true;
  }
}
registerProcessor('pfguitar', GuitarProcessor);
