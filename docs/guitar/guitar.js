// pfsynth guitar demo: live physical-model guitar (pfguitar.wasm in an AudioWorklet), score
// and tab (Verovio) following the performance, a fretboard showing the strings, measured
// guitar bodies and the room fitted to each recording (WebAudio convolution).
import { parseMIDI, parseMusicXML } from './import.js?v=20261007r';
import { preparePerformance } from './performance.js?v=20261007r';
const $ = (s) => document.querySelector(s);
const escapeHTML = s => String(s).replace(/[&<>"']/g, c => ({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;',"'":'&#39;'}[c]));
const escapeFields = o => Object.fromEntries(Object.entries(o).map(([k,v])=>[k,typeof v === 'string' ? escapeHTML(v) : v]));
const state = { ctx: null, node: null, ready: null, params: [], piece: null, base: null, rate: 1, notes: [], duration: 0, playing: false, tick: { t: 0, at: 0 }, sounding: [],
  lit: [], ptr: 0, elements: new Map(), vrv: null, token: 0, bodies: [], bodyBuffers: new Map(), roomBuffer: null };
const fmt = (s) => `${Math.floor(s / 60)}:${String(Math.floor(s % 60)).padStart(2, '0')}`;

// ---------- colour: pluck strength -> hue (as the videos: blue soft ... red loud) ----------
function oklch(L, C, h) {
  const a = C * Math.cos(h * Math.PI / 180), b = C * Math.sin(h * Math.PI / 180);
  const l = (L + .3963377774 * a + .2158037573 * b) ** 3, m = (L - .1055613458 * a - .0638541728 * b) ** 3, s = (L - .0894841775 * a - 1.2914855480 * b) ** 3;
  const lin = [4.0767416621 * l - 3.3077115913 * m + .2309699292 * s, -1.2684380046 * l + 2.6097574011 * m - .3413193965 * s, -.0041960863 * l - .7034186147 * m + 1.7076147010 * s];
  return 'rgb(' + lin.map(v => Math.round(255 * Math.min(1, Math.max(0, v <= .0031308 ? 12.92 * v : 1.055 * v ** (1 / 2.4) - .055)))).join(',') + ')';
}
const hue = (velocity) => { const db = 25 * Math.log10(Math.max(velocity, 1) / 100), u = Math.min(1, Math.max(0, (db + 12) / 24)); return oklch(.62, .15, 262 - u * 234); };

// ---------- audio ----------
async function audio() {
  if (state.ready) return state.ready;
  state.ready = (async () => {
    const ctx = state.ctx = new AudioContext({ latencyHint: 'playback' });
    if (!ctx.audioWorklet) throw new Error(`The guitar engine needs a secure context (AudioWorklet): open this page over https, or locally from http://localhost:${location.port || 80}/ rather than ${location.host}.`);
    await ctx.audioWorklet.addModule('guitar-worklet.js?v=20261008t2');
    const node = state.node = new AudioWorkletNode(ctx, 'pfguitar', { numberOfInputs: 0, numberOfOutputs: 1, outputChannelCount: [2] });
    state.body = ctx.createConvolver(); state.body.normalize = false; state.bodyGain = ctx.createGain();
    state.room = ctx.createConvolver(); state.room.normalize = false; state.roomGain = ctx.createGain();
    state.master = ctx.createGain(); const limiter = ctx.createDynamicsCompressor();
    limiter.threshold.value = -3; limiter.knee.value = 0; limiter.ratio.value = 20; limiter.attack.value = .002; limiter.release.value = .1;
    state.master.connect(limiter).connect(ctx.destination);
    const response = await fetch('pfguitar.wasm?v=20261008t2'); if (!response.ok) throw new Error('Could not load the guitar engine.');
    const bytes = await response.arrayBuffer();
    const ready = new Promise((res, rej) => { node.port.onmessage = (e) => { if (e.data.type === 'ready') res(e.data); else if(e.data.type === 'error') rej(new Error(e.data.text)); }; });
    node.port.postMessage({ type: 'wasm', bytes }, [bytes]);
    const info = await ready; state.params = info.params; buildParams();
    node.port.onmessage = (e) => onWorklet(e.data);
    route(); setVolume();
  })().catch(e => { state.ready = null; if (state.ctx) state.ctx.close().catch(() => {}); state.ctx = state.node = null; throw e; });   // let Play retry
  return state.ready;
}
function route() {   // worklet -> [body] -> [room] -> master
  if (!state.node) return;
  for (const n of [state.node, state.body, state.bodyGain, state.room, state.roomGain]) n.disconnect();
  let at = state.node;
  if ($('#body').value !== 'none' && state.body.buffer) { at.connect(state.body); state.body.connect(state.bodyGain); at = state.bodyGain; }
  if ($('#room').value !== 'dry' && state.room.buffer) { at.connect(state.room); state.room.connect(state.roomGain); at = state.roomGain; }
  at.connect(state.master);
}
function setVolume() { if (state.master) state.master.gain.value = 3.5 * 10 ** (+$('#volume').value / 20); }
function onWorklet(m) {
  if (m.type === 'tick') { if (m.tempo !== state.rate) return; state.tick = { t: m.t, at: state.ctx.currentTime }; state.sounding = m.sounding; state.load = m.load; }
  else if (m.type === 'tempo') { if (m.tempo !== state.rate) return; state.tick = {t:m.t,at:state.ctx.currentTime}; resetFollow(m.t); }
  else if (m.type === 'end') { pause(); seek(0); }
  else if (m.type === 'loaded' && !m.ok) { pause(); alertBox('The guitar could not load this score.'); }
  else if (m.type === 'error') { pause(); alertBox(m.text); }
}
const now = () => state.playing ? state.tick.t + Math.max(0, state.ctx.currentTime - state.tick.at) : state.tick.t;
// The worklet reports the time it has rendered; you hear it after the output latency.
const heard = () => state.playing ? Math.max(0, now() - (state.ctx.outputLatency || 0) - (state.ctx.baseLatency || 0)) : now();

// ---------- bodies and rooms ----------
async function loadBodies() {
  const j = await (await fetch('bodies.json')).json(); state.bodies = j.bodies;
  const sel = $('#body'); sel.innerHTML = '<option value="none">Bare strings, no body</option>' +
    j.bodies.map(b => `<option value="${b.id}">${b.maker}${b.year ? ', ' + b.year : ''}${b.place ? ' · ' + b.place : ''}</option>`).join('');
  sel.onchange = setBody;
}
async function setBody() {
  const id = $('#body').value; if (!state.ctx) return;
  if (id !== 'none') {
    if (!state.bodyBuffers.has(id)) state.bodyBuffers.set(id, await state.ctx.decodeAudioData(await (await fetch(`bodies/${id}.wav`)).arrayBuffer()));
    if ($('#body').value !== id) return;
    const buf = state.bodyBuffers.get(id), x = buf.getChannelData(0); let e = 0; for (const v of x) e += v * v;
    state.body.buffer = buf; state.bodyGain.gain.value = 1 / Math.sqrt(e || 1);
  }
  if (state.piece?.loadingProfiles) state.node.port.postMessage({type:'loading-enabled',count:id === state.piece.body ? state.piece.loadingProfiles.length : 0});
  route(); credits();
}
function roomImpulse(rtLow, rtHigh, ratio, sr) {   // statistical room (tools/guitar_room_fit.room_impulse), stereo
  const rt = (f) => Math.exp(Math.log(rtLow) + (Math.log(rtHigh) - Math.log(rtLow)) * (Math.log(f) - Math.log(200)) / (Math.log(4000) - Math.log(200)));
  const n = Math.round(1.3 * Math.max(rtLow, rtHigh) * sr), pre = Math.round(.012 * sr), buf = state.ctx.createBuffer(2, n + pre, sr);
  const edges = [44, 88, 177, 355, 710, 1420, 2840, 5680, 11360, 20000];
  for (let ch = 0; ch < 2; ch++) {
    const out = buf.getChannelData(ch); out[0] = 1; let seed = 1 + ch * 7919;
    const rand = () => { seed = (seed * 1103515245 + 12345) & 0x7fffffff; return seed / 0x7fffffff * 2 - 1; };
    for (let b = 0; b + 1 < edges.length; b++) {
      const lo = edges[b], hi = Math.min(edges[b + 1], sr * .49), fc = Math.sqrt(lo * hi), q = fc / (hi - lo), w = 2 * Math.PI * fc / sr, al = Math.sin(w) / (2 * q);
      const b0 = al / (1 + al), b2 = -b0, a1 = -2 * Math.cos(w) / (1 + al), a2 = (1 - al) / (1 + al), T = rt(fc), band = new Float32Array(n);
      let x1 = 0, x2 = 0, y1 = 0, y2 = 0, u1 = 0, u2 = 0, v1 = 0, v2 = 0;
      for (let i = 0; i < n; i++) {   // two bandpass sections, then the band's decay
        const x = rand(); let y = b0 * x + b2 * x2 - a1 * y1 - a2 * y2; x2 = x1; x1 = x; y2 = y1; y1 = y;
        const z = b0 * y + b2 * u2 - a1 * v1 - a2 * v2; u2 = u1; u1 = y; v2 = v1; v1 = z;
        band[i] = z * Math.exp(-6.9078 * i / sr / T);
      }
      let e = 0; for (const v of band) e += v * v; const g = Math.sqrt(ratio * 2 * (hi - lo) / sr / (e || 1));
      for (let i = 0; i < n; i++) out[pre + i] += g * band[i];
    }
  }
  return buf;
}
function setRoom() {
  if (!state.ctx) return; const v = $('#room').value;
  if (v === 'fitted' && state.piece?.room) { const r = state.piece.room; state.room.buffer = roomImpulse(r.rt_low, r.rt_high, r.ratio, state.ctx.sampleRate); state.roomGain.gain.value = 1 / Math.sqrt(1 + r.ratio); }
  else if (v === 'hall') { state.room.buffer = roomImpulse(2.2, .9, .8, state.ctx.sampleRate); state.roomGain.gain.value = 1 / Math.sqrt(1.8); }
  else if (v === 'studio') { state.room.buffer = roomImpulse(.5, .3, .25, state.ctx.sampleRate); state.roomGain.gain.value = 1 / Math.sqrt(1.25); }
  route(); credits();
}
function roomOptions() {
  const fitted = state.piece?.room ? `<option value="fitted">Fitted to this recording (${state.piece.room.rt_low.toFixed(1)} s / ${state.piece.room.rt_high.toFixed(2)} s)</option>` : '';
  $('#room').innerHTML = fitted + '<option value="studio">Small studio</option><option value="hall">Concert hall</option><option value="dry">Dry</option>';
  $('#room').onchange = setRoom;
}

// ---------- parameters ----------
const RELOAD = new Set(['Let strings ring', 'Harmonic touch time']);   // applied when the score is loaded
function buildParams() {
  const box = $('#params'); box.innerHTML = ''; let group = '';
  for (const p of state.params) {
    if (p.group !== group) { group = p.group; box.insertAdjacentHTML('beforeend', `<h3>${group}</h3>`); }
    const id = 'param' + p.index, step = p.integer ? 1 : (p.max - p.min) / 200;
    box.insertAdjacentHTML('beforeend', `<label title="${p.unit}">${p.name}<input id="${id}" type="range" min="${p.min}" max="${p.max}" step="${step}" value="${p.value}"></label><label class="value"><span></span><span id="${id}v">${show(p, p.value)}</span></label>`);
    const el = $('#' + id);
    el.oninput = () => { p.value = +el.value; $('#' + id + 'v').textContent = show(p, p.value); state.node.port.postMessage({ type: 'param', index: p.index, value: p.value }); };
    el.onchange = () => { if (RELOAD.has(p.name)) sendScore(now()); };
  }
}
const show = (p, v) => p.integer && p.unit.includes(',') ? (p.unit.split(',').find(s => s.trim().startsWith(String(Math.round(v)))) || v).toString().replace(/^\s*\d+\s*/, '') : (+v).toFixed(p.integer ? 0 : 2) + (p.unit && !p.unit.includes(' ') ? ' ' + p.unit : '');

// ---------- score display (Verovio) ----------
function vrvReady() {
  if (state.vrv) return Promise.resolve(state.vrv);
  return new Promise((res) => {
    const done = () => { if (!state.vrv) state.vrv = new verovio.toolkit(); res(state.vrv); };
    const tryInit = () => {
      if (state.vrv) { res(state.vrv); return; }
      if (!window.verovio) { setTimeout(tryInit, 100); return; }
      try { done(); return; } catch (e) { /* runtime still starting */ }
      verovio.module.onRuntimeInitialized = done; setTimeout(tryInit, 250);
    }; tryInit();
  });
}
// One system at a time: Verovio lays out one system per page (a tiny page height), the
// page shows the system being played and switches when playback reaches the next one.
async function renderScore(xml, token) {
  const box = $('#score'); state.pages = []; state.idPage = new Map(); state.page = -1;
  if (!xml) { box.innerHTML = '<p class="placeholder">No notation for this file — the fretboard below shows the strings.</p>'; state.elements = new Map(); $('#system-position').textContent = 'No notation'; $('#previous-system').disabled = $('#next-system').disabled = true; return; }
  box.innerHTML = '<p class="placeholder">Engraving the score…</p>';
  const tk = await vrvReady(); if (token !== state.token) return;
  tk.setOptions({ pageWidth: 2600, pageHeight: 100, scale: 40, adjustPageHeight: true, breaks: 'auto', header: 'none', footer: 'none', svgViewBox: true,
    pageMarginLeft: 30, pageMarginRight: 30, pageMarginTop: 30, pageMarginBottom: 20, justifyVertically: false });
  tk.loadData(xml); if (token !== state.token) return;
  let ratio = 0;
  for (let p = 1; p <= tk.getPageCount(); p++) {
    if (token !== state.token) return;
    const svg = tk.renderToSVG(p); state.pages.push(svg);
    for (const m of svg.matchAll(/<g id="([^"]+)" class="note/g)) state.idPage.set(m[1], p - 1);
    const vb = svg.match(/viewBox="0 0 ([\d.]+) ([\d.]+)"/); if (vb) ratio = Math.max(ratio, +vb[2] / +vb[1]);
    if (p % 8 === 0) await new Promise(r => setTimeout(r, 0));
  }
  if (token !== state.token) return;
  state.ratio = ratio; fitScore(); showPage(0);
}
// Right-hand fingers (p i m a): Verovio does not draw MusicXML <pluck>, so the page writes
// them, italic, centred on the notehead: above the highest note sounding at that moment,
// below the others (usually the thumb's bass). Inside g.note, so they light with the note.
function drawPlucks() {
  const top = new Map(); for (const n of state.notes) { const k = n.start.toFixed(3); top.set(k, Math.max(top.get(k) ?? -1, n.pitch)); }
  const drawn = new Set();
  for (const n of state.notes) {
    if (!n.pluck || !n.ids?.length || drawn.has(n.ids[0])) continue; const g = state.elements.get(n.ids[0]); if (!g) continue; drawn.add(n.ids[0]);
    const head = g.querySelector('.notehead') || g; let b; try { b = head.getBBox(); } catch (e) { continue; }
    const size = b.height * 1.7, above = n.pitch >= top.get(n.start.toFixed(3));
    const t = document.createElementNS('http://www.w3.org/2000/svg', 'text');
    t.setAttribute('class', 'pluck'); t.setAttribute('x', b.x + b.width / 2); t.setAttribute('text-anchor', 'middle');
    t.setAttribute('y', above ? b.y - size * .3 : b.y + b.height + size * .85); t.setAttribute('font-size', size);
    t.textContent = n.pluck; g.appendChild(t);
  }
}
function fitScore() { const box = $('#score'); if (state.ratio) box.style.height = Math.round(Math.max(box.clientWidth, window.innerWidth <= 900 ? 640 : 0) * state.ratio + 24) + 'px'; }
window.addEventListener('resize', fitScore);
function showPage(k) {
  if (k === state.page || k < 0 || k >= state.pages.length) return;
  const box = $('#score'); box.innerHTML = state.pages[k]; state.page = k;
  state.elements = new Map(); for (const g of box.querySelectorAll('g.note')) state.elements.set(g.id, g);
  drawPlucks();
  $('#system-position').textContent = `System ${k + 1} / ${state.pages.length}`;
  $('#previous-system').disabled = k === 0; $('#next-system').disabled = k + 1 === state.pages.length;
  for (const l of state.lit) l.els = (l.ids || []).map(id => state.elements.get(id)).filter(Boolean);
  for (const l of state.lit) for (const el of l.els) { el.style.setProperty('--vc', l.color); el.classList.add('on'); }
}
$('#score').addEventListener('click', (e) => {
  const g = e.target.closest('g.note'); if (!g) return;
  const matches = state.notes.filter(n => n.ids?.includes(g.id)); if (!matches.length) return;
  const n = matches.reduce((a,b) => Math.abs(b.start-heard()) < Math.abs(a.start-heard()) ? b : a);
  seek(Math.max(0, n.start - .05)); play().catch(e => alertBox(e.message));
});
$('#previous-system').onclick = () => { pause(); showPage(state.page - 1); };
$('#next-system').onclick = () => { pause(); showPage(state.page + 1); };

// ---------- following ----------
function unlightAll() { for (const l of state.lit) for (const el of l.els) el.classList.remove('on'); state.lit = []; }
function follow(t) {
  const N = state.notes;
  while (state.ptr < N.length && N[state.ptr].start <= t) {
    const n = N[state.ptr++], ids = n.ids || []; if (!ids.length) continue;
    const page = state.idPage?.get(ids[0]); if (page !== undefined) showPage(page);
    const els = ids.map(id => state.elements.get(id)).filter(Boolean), color = hue(n.velocity);
    for (const el of els) { el.style.setProperty('--vc', color); el.classList.add('on'); }
    state.lit.push({ ids, els, color, off: n.start + Math.min(Math.max(.25, n.end - n.start), 1.0) });
  }
  state.lit = state.lit.filter(l => { if (l.off > t) return true; for (const el of l.els) el.classList.remove('on'); return false; });
}
function resetFollow(t) {
  unlightAll(); let lo = 0, hi = state.notes.length;
  while (lo < hi) { const m = (lo + hi) >> 1; if (state.notes[m].start < t) lo = m + 1; else hi = m; }
  state.ptr = Math.max(0, lo - 1);
  const n = state.notes[state.ptr], page = n && state.idPage?.get((n.ids || [])[0]); if (page !== undefined) showPage(page);
  follow(t);
}

// ---------- fretboard ----------
const cv = $('#fretboard'), g2 = cv.getContext('2d');
const NUT = 70, BRIDGE = 1530, MID = 150;
const fx = (f) => NUT + (BRIDGE - NUT) * (1 - 2 ** (-f / 12));
const sy = (s, x) => MID + (s - 3.5) * (16 + 9 * (x - NUT) / (BRIDGE - NUT));
function drawBoard() {
  const W = cv.width, H = cv.height; g2.clearRect(0, 0, W, H);
  // body, soundhole, bridge
  const x12 = fx(12); g2.fillStyle = '#f0e6d3'; g2.strokeStyle = '#dccbb0'; g2.lineWidth = 1.5;
  g2.beginPath(); g2.moveTo(x12 - 8, MID - 92); g2.bezierCurveTo(x12 + 240, MID - 116, 1420, MID - 132, W, MID - 106); g2.lineTo(W, MID + 106);
  g2.bezierCurveTo(1420, MID + 132, x12 + 240, MID + 116, x12 - 8, MID + 92); g2.closePath(); g2.fill(); g2.stroke();
  const hole = NUT + .745 * (BRIDGE - NUT);
  g2.lineWidth = 5; g2.strokeStyle = '#c9b28e'; g2.beginPath(); g2.arc(hole, MID, 88, 0, 7); g2.stroke();
  g2.fillStyle = 'rgba(74,64,54,.88)'; g2.beginPath(); g2.arc(hole, MID, 74, 0, 7); g2.fill();
  g2.fillStyle = '#b5966e'; g2.fillRect(BRIDGE - 6, MID - 72, 64, 144); g2.fillStyle = '#f4eee2'; g2.fillRect(BRIDGE - 4, MID - 66, 7, 132);
  // fingerboard and frets
  const end = fx(19.4), top = (x) => sy(1, x) - 13, bot = (x) => sy(6, x) + 13;
  g2.fillStyle = '#e4d5bc'; g2.beginPath(); g2.moveTo(NUT, top(NUT) - 3); g2.lineTo(end, top(end) - 5); g2.lineTo(end, bot(end) + 5); g2.lineTo(NUT, bot(NUT) + 3); g2.closePath(); g2.fill();
  g2.strokeStyle = '#a99c8a'; g2.lineWidth = 2.2; for (let f = 1; f <= 19; f++) { const x = fx(f); g2.beginPath(); g2.moveTo(x, top(x) - 2); g2.lineTo(x, bot(x) + 2); g2.stroke(); }
  g2.fillStyle = '#f2ecdf'; g2.fillRect(NUT - 9, top(NUT) - 5, 9, bot(NUT) - top(NUT) + 10);
  g2.fillStyle = '#6b727c'; g2.font = '14px "Source Sans 3", sans-serif'; g2.textAlign = 'center';
  for (const f of [3, 5, 7, 9, 12, 15, 17, 19]) { const x = (fx(f - 1) + fx(f)) / 2; g2.fillText(String(f), x, bot(x) + 26); }
  // strings
  const t = heard(), active = new Map(); for (const [i, level, s, fret] of state.sounding) active.set(s, { i, level, fret });
  for (let s = 1; s <= 6; s++) {
    const a = active.get(s), n = a && state.notes[a.i], harmonic = n && n.articulation === 4, fret = a ? (harmonic ? 0 : Math.max(0, a.fret)) : 0;
    const xa = fret ? fx(fret) : NUT, col = n ? hue(n.velocity) : '#999', width = 1.1 + .45 * s;
    g2.strokeStyle = s >= 4 ? '#8c7a62' : '#9a9ea4'; g2.lineWidth = width;
    g2.beginPath(); g2.moveTo(NUT, sy(s, NUT)); g2.lineTo(BRIDGE, sy(s, BRIDGE)); g2.stroke();
    if (!a || a.level < .01) continue;
    if (n?.pluck && state.playing && t >= n.start && t - n.start < .13) {
      const px = BRIDGE - (BRIDGE - xa) * (n.pluck_position || .19);
      g2.fillStyle = col; g2.font = 'bold 22px Georgia'; g2.fillText(n.pluck, px, sy(s,px) - 12);
    }
    const amp = 15 * Math.min(1, Math.sqrt(a.level)), phase = Math.cos(t * 2 * Math.PI * 3.7 + s * 1.3);
    const shape = (u) => harmonic ? Math.sin(2 * Math.PI * u) : Math.sin(Math.PI * u);
    g2.fillStyle = col; g2.globalAlpha = .2; g2.beginPath();
    for (let k = 0; k <= 60; k++) { const u = k / 60, x = xa + (BRIDGE - xa) * u; g2.lineTo(x, sy(s, x) - amp * Math.abs(shape(u))); }
    for (let k = 60; k >= 0; k--) { const u = k / 60, x = xa + (BRIDGE - xa) * u; g2.lineTo(x, sy(s, x) + amp * Math.abs(shape(u))); }
    g2.fill(); g2.globalAlpha = 1; g2.strokeStyle = col; g2.lineWidth = width;
    g2.beginPath(); for (let k = 0; k <= 60; k++) { const u = k / 60, x = xa + (BRIDGE - xa) * u; g2.lineTo(x, sy(s, x) + amp * shape(u) * phase); } g2.stroke();
    if (fret && n && t <= n.end) { const x = fx(fret - 1) + .72 * (fx(fret) - fx(fret - 1)); g2.fillStyle = col; g2.beginPath(); g2.arc(x, sy(s, x), 9, 0, 7); g2.fill(); g2.strokeStyle = '#fff'; g2.lineWidth = 1.5; g2.stroke(); }
    if (harmonic && t < n.start + .6) { const x = fx(n.art_param || 12); g2.strokeStyle = col; g2.lineWidth = 2.4; g2.beginPath(); g2.arc(x, sy(s, x), 11, 0, 7); g2.stroke(); }
  }
  g2.fillStyle = '#6b727c'; g2.font = '600 14px "Source Sans 3", sans-serif';
  const tun = state.piece?.tuning || [64, 59, 55, 50, 45, 40], names = ['C', 'C♯', 'D', 'E♭', 'E', 'F', 'F♯', 'G', 'A♭', 'A', 'B♭', 'B'];
  for (let s = 1; s <= 6; s++) g2.fillText(s === 1 ? names[tun[0] % 12].toLowerCase() : names[tun[s - 1] % 12], NUT - 26, sy(s, NUT) + 5);
}

// ---------- transport ----------
function frame() {
  const t = heard();
  if (state.playing) follow(t);
  $('#time').textContent = `${fmt(Math.min(t, state.duration))} / ${fmt(state.duration)}`;
  const cpu = $('#cpu'); if (state.playing && state.load !== undefined) cpu.textContent = `audio CPU ${(100 * state.load).toFixed(state.load < .1 ? 1 : 0)}%`; else if (!state.playing) cpu.textContent = '';
  if (!$('#seek').matches(':active')) $('#seek').value = t;
  drawBoard(); requestAnimationFrame(frame);
}
async function play() { await audio(); await state.ctx.resume(); if (!state.notes.length) return; state.node.port.postMessage({ type: 'play' }); state.tick.at = state.ctx.currentTime; state.playing = true; $('#play').textContent = 'Pause'; $('#play').setAttribute('aria-label','Pause'); }
function pause() { if (state.node) state.node.port.postMessage({ type: 'pause' }); state.tick.t = now(); state.playing = false; $('#play').textContent = 'Play'; $('#play').setAttribute('aria-label','Play'); }
function seek(t) { if (state.node) state.node.port.postMessage({ type: 'seek', t }); state.tick = { t, at: state.ctx ? state.ctx.currentTime : 0 }; resetFollow(t); }
$('#play').onclick = () => state.playing ? pause() : play().catch(e => alertBox(e.message));
$('#seek').oninput = () => seek(+$('#seek').value);
$('#volume').oninput = setVolume;
// Practice tempo changes the score clock without reloading or re-plucking strings.
// The worklet applies it at its next block and acknowledges the actual position.
const tempo = () => +$('#tempo').value / 100;
function stretch() {
  $('#tempov').textContent = `${$('#tempo').value}%`; if (!state.base) return;
  state.rate = tempo(); const k = 1 / state.rate;
  state.notes = state.base.notes.map(n => ({ ...n, start: n.start * k, end: n.end * k }));
  state.duration = state.base.duration * k; $('#seek').max = state.duration;
}
$('#tempo').oninput = () => { $('#tempov').textContent = `${$('#tempo').value}%`; };
$('#tempo').onchange = () => {
  if (!state.base || !state.node) { stretch(); return; }
  const at = now() / state.duration;
  stretch(); state.tick = {t:Math.min(at * state.duration,state.duration),at:state.ctx.currentTime};
  resetFollow(state.tick.t); state.node.port.postMessage({type:'tempo',rate:state.rate});
};

// ---------- loading ----------
function sendScore(at = 0) {
  state.node.port.postMessage({ type: 'score', notes: state.base.notes, tuning: state.piece.tuning, duration: state.base.duration, tempo: state.rate, seek: at, loadingProfiles: state.piece.loadingProfiles, loadingEnabled: $('#body').value === state.piece.body });
  state.playing = false; $('#play').textContent = 'Play'; $('#play').setAttribute('aria-label','Play'); state.tick = { t: at, at: state.ctx.currentTime }; resetFollow(at);
}
async function loadScore(piece, notes, xml) {
  const token = ++state.token; pause();
  piece = preparePerformance({...piece, notes}); notes = piece.notes;
  state.piece = piece; state.base = { notes: notes.slice().sort((a, b) => a.start - b.start), duration: piece.duration }; stretch();
  $('#seek').disabled = true; $('#play').disabled = true;
  await audio(); if (token !== state.token) return;
  for (const p of state.params) {
    p.value = piece.instrumentSettings?.[p.name] ?? p.def;
    state.node.port.postMessage({type:'param',index:p.index,value:p.value});
  }
  buildParams(); sendScore(0); roomOptions(); if(piece.defaultRoom) $('#room').value = piece.defaultRoom; setRoom();
  if (piece.body && state.bodies.some(b => b.id === piece.body)) $('#body').value = piece.body; await setBody();
  credits(); await renderScore(xml, token);
  if (token === state.token) { $('#seek').disabled = false; $('#play').disabled = false; }
}
async function loadPiece(p, button) {
  const request = state.request = (state.request || 0) + 1; pause(); $('#play').disabled = true;
  for (const b of document.querySelectorAll('.pieces button')) b.classList.toggle('active', b === button);
  const base = `pieces/${p.slug}/`, s = await (await fetch(base + 'score.json')).json();
  const gz = await fetch(base + 'score.musicxml.gz'); const xml = await new Response(gz.body.pipeThrough(new DecompressionStream('gzip'))).text();
  if (request !== state.request) return;
  state.downloadBase = base;
  await loadScore(s, s.notes, xml);
  if (request === state.request) history.replaceState(null,'',`?piece=${encodeURIComponent(p.slug)}`);
}
$('#file').onchange = async (e) => {
  const f = e.target.files[0]; if (!f) return; const name = f.name.toLowerCase();
  try {
    let piece, xml = null;
    if (name.endsWith('.json')) { piece = JSON.parse(await f.text()); }
    else if (name.endsWith('.mid') || name.endsWith('.midi')) { const r = parseMIDI(await f.arrayBuffer()); piece = { ...r, title: f.name }; }
    else { const r = parseMusicXML(await f.text()); xml = r.xml; piece = { ...r, title: r.title || f.name }; }
    state.request = (state.request || 0) + 1; state.downloadBase = null;
    for (const b of document.querySelectorAll('.pieces button')) b.classList.remove('active');
    await loadScore(piece, piece.notes, xml);
  } catch (err) { alertBox(err.message); }
};
function alertBox(text) { const p = document.createElement('p');p.className='placeholder';p.textContent=text;$('#score').replaceChildren(p); }
function credits() {
  const p = state.piece && escapeFields(state.piece); if (!p) { $('#credits').innerHTML = ''; return; }
  const b = state.bodies.find(x => x.id === $('#body').value), perf = p.performance && escapeFields(p.performance);
  $('#credits').innerHTML = `<h2>About this performance</h2><p><strong>${p.title || ''}</strong>${p.composer ? ` — ${p.composer}${p.dates ? ` (${p.dates})` : ''}` : ''}${p.arranger ? `<br>Arranged by ${p.arranger}` : ''}</p>` +
    (p.performanceNote ? `<p>${p.performanceNote}</p>` : perf ? `<p>Timing from ${perf.performer ? perf.performer + '’s recording' : 'the recording'} <a href="https://www.youtube.com/watch?v=${encodeURIComponent(perf.youtube)}" target="_blank" rel="noopener">“${perf.video_title || 'on YouTube'}”</a>, aligned in GAPS; dynamics ${perf.dynamics}.</p>` : '<p>Your file, played by the model guitar.</p>') +
    `<p>Body: ${b ? `${b.maker}${b.year ? ', ' + b.year : ''} (measured by R. Mores)` : 'none'}.${p.license ? ` Score data: ${p.license}.` : ''}</p>` +
    (state.downloadBase ? `<p><a href="${state.downloadBase}score.json" download>Performance JSON</a> · <a href="FORMAT.md">JSON controls</a> · <a href="${state.downloadBase}score.musicxml.gz" download>Score + tab (MusicXML, gzip)</a>${p.scoreSource ? ` · <a href="${state.downloadBase}score.musicxml" download>Uncompressed score</a> · <a href="${state.downloadBase}SOURCE.txt">Sources and tab audit</a>` : ''}</p>` : '');
}
async function init() {
  await loadBodies(); roomOptions();
  const { pieces } = await (await fetch('pieces.json')).json();
  const box = $('#pieces');
  for (const p of pieces) {
    const b = document.createElement('button'); b.innerHTML = `${p.title}<small>${p.composer}${p.performer ? ' · ' + p.performer : ''} · ${fmt(p.duration)}</small>`;
    b.onclick = () => loadPiece(p, b).catch(e => alertBox(e.message)); box.append(b);
    if (new URLSearchParams(location.search).get('piece') === p.slug) state.initialPiece = [p,b];
  }
  requestAnimationFrame(frame);
  if (state.initialPiece) await loadPiece(...state.initialPiece);
}
window.__pfguitar = state;   // for debugging from the console
init().catch(e => alertBox(e.message));
