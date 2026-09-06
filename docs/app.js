// pfsynth web demo: WebAssembly piano model in an AudioWorklet + Verovio score following
// over nASAP note alignments.  Static page, no build step (see README.md).
'use strict';
const BUILD = '20260906d';   // bump with index.html's ?v= so GitHub Pages' 10-minute cache doesn't serve a stale script
const OPT = { TONE:0, ATTACK:1, PEDAL_MODE:2, UNA_CORDA:3, GAIN_DB:4, BODY_DB:5, KNOCK_DB:6, NOISE_DB:7, LIMITER:8,
  RESONANCE:9, RESONANCE_DB:10, RES_COUPLING:11, RES_SKIRT:12, RES_SUSTAIN:13, RES_TILT:14, RES_T60:15 };
// Fallback defaults (the wasm module's pfw_default() is the source of truth and replaces these once the audio engine starts).
const FALLBACK_DEFAULTS = { 0:1, 1:1, 2:1, 3:1, 4:6.02, 5:-18, 6:-22, 7:-17, 8:1, 9:1, 10:0, 11:-32, 12:1.5, 13:-15, 14:0, 15:1 };
const GROUPS = [
  { key:'sound', name:'Sound', hint:'' },
  { key:'onset', name:'Onset', hint:'The struck-soundboard layer under every note: trims chosen by ear on 2026-09-05.' },
  { key:'pedals', name:'Pedals', hint:'Half pedaling and una corda, fitted to Pianoteq as a black box.' },
  { key:'experimental', name:'Experimental', hint:'Sympathetic resonance internals (fitted to lone notes with the pedal down vs up). Change with care.', collapsed:true },
  { key:'legacy', name:'Legacy', hint:'Earlier behaviour kept for A/B listening. Not the known-good path.', collapsed:true },
];
const SETTINGS = [
  { id:OPT.GAIN_DB, name:'Gain', group:'sound', unit:'dB', min:-12, max:18, step:0.5, desc:'Makeup gain before the limiter. The model renders at recording level, so it needs some.' },
  { id:OPT.RESONANCE, name:'Sympathetic resonance', group:'sound', type:'toggle', desc:'Undamped strings (pedal down, held keys, the top 1.5 octaves) ring in sympathy with what is played. Prototype.' },
  { id:OPT.RESONANCE_DB, name:'Resonance level', group:'sound', unit:'dB', min:-24, max:12, step:1, desc:'Trim on the sympathetic bank. 0 dB = as fitted (halo 20–28 dB under the note, like Pianoteq).' },
  { id:OPT.ATTACK, name:'Onset layer', group:'onset', type:'toggle', desc:'Soundboard thump, knock modes and hammer noise at each note-on.' },
  { id:OPT.BODY_DB, name:'Body (slow soundboard modes)', group:'onset', unit:'dB', min:-40, max:6, step:1, desc:'Low body/room modes, 59–450 Hz. Fitted level is 0 dB; listening chose −18.' },
  { id:OPT.KNOCK_DB, name:'Knock (fast modes)', group:'onset', unit:'dB', min:-40, max:6, step:1, desc:'Fast soundboard modes up to 2.8 kHz that make the percussive click.' },
  { id:OPT.NOISE_DB, name:'Hammer noise', group:'onset', unit:'dB', min:-40, max:6, step:1, desc:'Short filtered noise burst between the partials in the first tens of ms.' },
  { id:OPT.PEDAL_MODE, name:'Continuous damper (half pedaling)', group:'pedals', type:'toggle', desc:'The damper follows the raw CC64 value. Off = binary sustain with a fixed release (legacy).' },
  { id:OPT.UNA_CORDA, name:'Una corda (CC67)', group:'pedals', type:'toggle', desc:'Soft pedal: −2.1 dB on the fundamental growing 1.7 dB per octave of partial index, plus a slower fundamental decay.' },
  { id:OPT.RES_COUPLING, name:'Resonance coupling', group:'experimental', unit:'dB', min:-50, max:-10, step:1, desc:'Free amplitude of a coincident string partial re the note’s partial at note-on.' },
  { id:OPT.RES_SKIRT, name:'Resonance skirt', group:'experimental', unit:'Hz', min:0.5, max:16, step:0.5, desc:'Width of the excitation around each partial: how much a string 16 Hz away gets vs one on the nose.' },
  { id:OPT.RES_SUSTAIN, name:'Resonance sustain bound', group:'experimental', unit:'dB', min:-40, max:0, step:1, desc:'Cap on the driven build-up of a string sitting exactly on a played partial.' },
  { id:OPT.RES_TILT, name:'Resonance tilt', group:'experimental', unit:'dB/oct', min:-12, max:6, step:0.5, desc:'Coupling change per octave above 250 Hz.' },
  { id:OPT.RES_T60, name:'Resonance decay scale', group:'experimental', unit:'×', min:0.25, max:4, step:0.05, desc:'Multiplies the sympathetic strings’ free decay times (taken from the tonal patch).' },
  { id:OPT.TONE, name:'Tonal patch', group:'legacy', type:'choice', options:[[1,'Pianoteq-fitted (known good)'],[0,'Salamander-fitted (legacy)']], desc:'Which fitted partial patch the voices use. The Salamander fit is the frozen 2026-09-04 baseline.' },
  { id:OPT.LIMITER, name:'Limiter', group:'legacy', type:'toggle', desc:'Block lookahead peak limiter at −0.5 dBFS. Turning it off exposes clipping on fff chords.' },
];
const $ = (s) => document.querySelector(s);
const state = { pieces:[], custom:[], roll:null, piece:null, token:0, events:null, duration:0, playing:false, lastStatus:null, audio:null, node:null, ready:false, defaults:{...FALLBACK_DEFAULTS}, values:{}, score:null, follow:null, pendingLoad:null, pendingLoadToken:0, vrv:null };

// ---------- pieces ----------
async function loadPieces() {
  state.pieces = await (await fetch('pieces.json')).json();
  renderPieces('');
}
function fmtTime(s) { s = Math.max(0, Math.floor(s)); return Math.floor(s / 60) + ':' + String(s % 60).padStart(2, '0'); }
function esc(s) { return String(s).replace(/[&<>"]/g, c => ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;' }[c])); }
function renderPieces(q) {
  const ul = $('#pieces'); ul.innerHTML = '';
  const words = q.toLowerCase().split(/\s+/).filter(Boolean);
  const all = state.custom.concat(state.pieces);
  const list = all.filter(p => { const hay = (p.composer + ' ' + p.title + ' ' + (p.keywords || '') + ' ' + (p.tags || []).join(' ') + ' ' + p.performer).toLowerCase(); return words.every(w => hay.includes(w)); });
  if (!list.length) { ul.innerHTML = '<li class="none">No pieces match.</li>'; return; }
  for (const p of list) {
    const li = document.createElement('li'); li.dataset.id = p.id; if (state.piece && state.piece.id === p.id) li.classList.add('sel'); if (p.custom) li.classList.add('custom');
    li.innerHTML = `<div class="c">${esc(p.composer)}</div><div class="t">${esc(p.title)}</div><div class="m"><span>${p.duration ? fmtTime(p.duration) : ''}</span><span>${esc(p.performer)}</span>${(p.tags || []).map(t => `<span class="tag">${esc(t)}</span>`).join('')}</div>`;
    li.onclick = () => selectPiece(p);
    ul.appendChild(li);
  }
}
// ---------- user MIDI files (drag & drop / Open MIDI…) ----------
let customSeq = 0;
async function addFiles(files) {
  const list = Array.from(files); if (!list.length) return;
  setStatus('Reading ' + (list.length === 1 ? list[0].name : list.length + ' files') + '…');
  let first = null; const rejected = [];
  for (const f of list) {
    const bytes = await f.arrayBuffer();
    const head = new Uint8Array(bytes, 0, Math.min(4, bytes.byteLength));
    const isSMF = head.length === 4 && head[0] === 0x4d && head[1] === 0x54 && head[2] === 0x68 && head[3] === 0x64; // "MThd"
    if (!isSMF) { rejected.push(f.name); continue; }
    const p = { id: 'custom-' + (++customSeq), custom: true, composer: 'Your file', title: f.name.replace(/\.(mid|midi|smf|kar)$/i, ''), performer: (f.size / 1024).toFixed(0) + ' KB', duration: 0, tags: ['your MIDI'], keywords: 'custom file dropped', bytes };
    state.custom.unshift(p); if (!first) first = p;
  }
  renderPieces($('#search').value);
  if (rejected.length) setStatus('Not a Standard MIDI File: ' + rejected.join(', '), true);
  if (first) selectPiece(first);
}
function dragHasFiles(e) {
  const dt = e.dataTransfer; if (!dt) return false;
  const types = Array.from(dt.types || []);
  return types.includes('Files') || types.includes('application/x-moz-file') || Array.from(dt.items || []).some(i => i.kind === 'file');
}
function droppedFiles(dt) {
  let files = Array.from(dt.files || []);
  if (!files.length && dt.items) files = Array.from(dt.items).filter(i => i.kind === 'file').map(i => i.getAsFile()).filter(Boolean);
  return files;
}
function setupDrop() {
  let depth = 0; const overlay = $('#dropzone');
  const hide = () => { depth = 0; overlay.classList.remove('show'); };
  document.addEventListener('dragenter', e => { e.preventDefault(); if (dragHasFiles(e)) { depth++; overlay.classList.add('show'); } });
  document.addEventListener('dragover', e => { e.preventDefault(); if (e.dataTransfer) e.dataTransfer.dropEffect = 'copy'; });
  document.addEventListener('dragleave', e => { if (--depth <= 0) hide(); });
  document.addEventListener('drop', e => {
    e.preventDefault(); e.stopPropagation(); hide();
    const files = e.dataTransfer ? droppedFiles(e.dataTransfer) : [];
    if (files.length) addFiles(files); else setStatus('Drop MIDI files from your disk (.mid)', true);
  });
  // never let the browser navigate to a dropped file, wherever it lands
  window.addEventListener('dragover', e => e.preventDefault()); window.addEventListener('drop', e => e.preventDefault());
  $('#openfile').addEventListener('change', e => { addFiles(e.target.files); e.target.value = ''; });
  $('#openbtn').onclick = () => $('#openfile').click();
}
async function selectPiece(p) {
  if (state.piece && state.piece.id === p.id) return;
  state.piece = p; const token = ++state.token;
  for (const li of document.querySelectorAll('#pieces li')) li.classList.toggle('sel', li.dataset.id === p.id);
  $('#nowplaying').innerHTML = `<b>${esc(p.composer)}</b> — ${esc(p.title)} <span>· ${esc(p.performer)}</span>`;
  setStatus('Loading…'); pause(); clearFollow(); $('#score').innerHTML = ''; $('#score').classList.remove('hidden'); $('#roll').classList.add('hidden'); state.roll = null; $('#scorewrap').scrollTop = 0; $('#placeholder').classList.remove('hidden'); $('#placeholder').innerHTML = '<h2>Rendering score…</h2>';
  try {
    if (p.custom) {   // no score or alignment: piano roll instead
      state.pendingLoad = p.bytes.slice(0); state.pendingLoadToken = token; state.align = null;
      $('#placeholder').classList.add('hidden'); $('#score').classList.add('hidden'); $('#roll').classList.remove('hidden');
      await ensureAudio(); if (token !== state.token) return; sendLoad(); return;
    }
    const [midi, xml, tsv] = await Promise.all([
      fetch(`pieces/${p.id}/perf.mid`).then(r => r.arrayBuffer()),
      fetchGz(`pieces/${p.id}/score.musicxml.gz`), fetchGz(`pieces/${p.id}/align.tsv.gz`)]);
    if (token !== state.token) return;
    state.pendingLoad = midi; state.pendingLoadToken = token; state.align = parseAlign(tsv);
    await ensureAudio();
    if (token !== state.token) return;
    sendLoad();
    await renderScore(xml, token);
  } catch (e) { console.error(e); setStatus('Failed to load: ' + e.message, true); }
}
async function fetchGz(url) {
  const r = await fetch(url); if (!r.ok) throw new Error(url + ' ' + r.status);
  const ds = new DecompressionStream('gzip'); return await new Response(r.body.pipeThrough(ds)).text();
}
function parseAlign(tsv) {
  const rows = []; for (const line of tsv.split('\n')) { const c = line.split('\t'); if (c.length < 6 || c[0] === 'xml_id') continue; rows.push({ id: c[0].replace(/-\d+$/, ''), pitch: +c[4], onset: +c[5] }); }
  return rows;
}
function sendLoad() {
  if (!state.node || !state.pendingLoad) return;
  const bytes = state.pendingLoad; state.pendingLoad = null;
  state.node.port.postMessage({ type: 'load', bytes, token: state.pendingLoadToken }, [bytes]);
}

// ---------- audio ----------
async function ensureAudio() {
  if (state.audio) return state.readyPromise;
  const ctx = new (window.AudioContext || window.webkitAudioContext)({ latencyHint: 'playback' });
  state.audio = ctx;
  state.readyPromise = (async () => {
    await ctx.audioWorklet.addModule('worklet.js?v=' + BUILD);
    const node = new AudioWorkletNode(ctx, 'pfsynth', { numberOfInputs: 0, numberOfOutputs: 1, outputChannelCount: [2] });
    node.connect(ctx.destination); state.node = node;
    node.port.onmessage = (e) => onWorklet(e.data);
    const bytes = await (await fetch('pfsynth.wasm')).arrayBuffer();
    node.port.postMessage({ type: 'wasm', bytes }, [bytes]);
    await new Promise(res => { state.resolveReady = res; });
  })();
  return state.readyPromise;
}
function onWorklet(m) {
  switch (m.type) {
    case 'ready':
      state.ready = true; m.defaults.forEach((v, i) => state.defaults[i] = v);
      for (const id in state.values) state.node.port.postMessage({ type: 'set', id: +id, value: state.values[id] });
      renderSettings($('#setsearch').value); state.resolveReady && state.resolveReady(); sendLoad(); break;
    case 'loaded':
      if (m.token !== state.token) return;
      state.events = { n: m.n, t: m.t, type: m.etype, note: m.note, val: m.val }; state.duration = m.duration;
      $('#seek').max = m.duration; $('#seek').disabled = false; $('#play').disabled = false; $('#seek').value = 0; $('#time').textContent = '0:00 / ' + fmtTime(m.duration);
      if (state.piece && state.piece.custom) { state.piece.duration = m.duration; renderPieces($('#search').value); buildRoll(); }
      else buildFollow();
      setStatus(`${m.n} events · ${fmtTime(m.duration)}`);
      break;
    case 'status': state.lastStatus = m; anchorClock(m); updateKeys(m.keys); if (document.hidden) follow(m.time); $('#voices').textContent = m.active; const pct = Math.round(m.load * 100); $('#loadpct').textContent = pct + '%'; const bar = $('#loadbar'); bar.style.width = Math.min(100, pct) + '%'; bar.classList.toggle('hot', pct > 80); break;
    case 'ended': state.playing = false; $('#play').textContent = 'Play'; break;
    case 'log': console.log('[pfsynth]', m.text); break;
    case 'error': setStatus(m.text, true); break;
  }
}
function play() { if (!state.ready || !state.events) return; state.audio.resume(); state.node.port.postMessage({ type: 'play' }); state.playing = true; $('#play').textContent = 'Pause'; }
function pause() { if (state.node) state.node.port.postMessage({ type: 'pause' }); state.playing = false; $('#play').textContent = 'Play'; }
function seek(t) { if (!state.node) return; state.node.port.postMessage({ type: 'seek', t }); if (state.lastStatus) { state.lastStatus.time = t; } state.clockOffset = t - state.audio.currentTime; $('#seek').value = t; $('#time').textContent = fmtTime(t) + ' / ' + fmtTime(state.duration); resetFollow(t); if (state.roll) drawRoll(t, true); }
// Playhead time from the audio clock: the worklet reports (song time, its own currentTime) for a
// block, and both clocks are the context's, so song time = offset + ctx.currentTime with a constant
// offset while playing.  Re-anchoring only on real changes (seek, start) keeps the roll from jittering.
function currentTime() {
  const s = state.lastStatus; if (!s) return 0;
  if (!(state.playing && s.playing)) return s.time;
  return Math.min(state.duration || Infinity, state.clockOffset + state.audio.currentTime);
}
function anchorClock(m) {
  const off = m.time - m.ctxTime;
  if (state.clockOffset === undefined || Math.abs(off - state.clockOffset) > 0.02) state.clockOffset = off;
  else state.clockOffset += (off - state.clockOffset) * 0.05;
}
function setStatus(text, err) { const el = $('#status'); el.textContent = text; el.classList.toggle('err', !!err); }

// ---------- keyboard strip ----------
function buildKeys() {
  const box = $('#keys'); box.innerHTML = ''; const whites = [];
  for (let n = 21; n <= 108; n++) { const pc = n % 12; if ([1, 3, 6, 8, 10].includes(pc)) continue; const d = document.createElement('div'); d.className = 'w'; d.dataset.n = n; box.appendChild(d); whites.push(n); }
  const W = whites.length;
  for (let n = 21; n <= 108; n++) { const pc = n % 12; if (![1, 3, 6, 8, 10].includes(pc)) continue; const idx = whites.filter(w => w < n).length; const d = document.createElement('div'); d.className = 'b'; d.dataset.n = n; d.style.left = (idx / W * 100) + '%'; box.appendChild(d); }
}
function updateKeys(keys) {
  for (const el of $('#keys').children) {
    const v = keys[+el.dataset.n];
    if (v) { el.classList.add('on'); el.style.background = velocityColor(v, 1); }
    else if (el.classList.contains('on')) { el.classList.remove('on'); el.style.background = ''; }
  }
}

// ---------- score ----------
function vrvReady() {
  if (state.vrv) return Promise.resolve(state.vrv);
  return new Promise((res) => {
    const done = () => { if (!state.vrv) state.vrv = new verovio.toolkit(); res(state.vrv); };
    const tryInit = () => {
      if (state.vrv) { res(state.vrv); return; }
      if (!window.verovio) { setTimeout(tryInit, 100); return; }
      try { done(); return; } catch (e) { /* runtime not initialized yet */ }
      verovio.module.onRuntimeInitialized = done;
      setTimeout(tryInit, 250);
    }; tryInit();
  });
}
async function renderScore(xml, token) {
  const tk = await vrvReady(); if (token !== state.token) return;
  tk.setOptions({ pageWidth: 2100, pageHeight: 2970, scale: 40, adjustPageHeight: true, breaks: 'auto', header: 'none', footer: 'none', svgViewBox: true, pageMarginLeft: 40, pageMarginRight: 40, pageMarginTop: 20, pageMarginBottom: 20, justifyVertically: false });
  await new Promise(r => setTimeout(r, 10));
  tk.loadData(xml); if (token !== state.token) return;
  const pages = tk.getPageCount(); const box = $('#score'); box.innerHTML = ''; $('#placeholder').classList.add('hidden');
  for (let p = 1; p <= pages; p++) {
    if (token !== state.token) return;
    box.insertAdjacentHTML('beforeend', tk.renderToSVG(p));
    if (p === 1) indexScore(); // notes of page 1 available at once
    await new Promise(r => setTimeout(r, 0));
  }
  indexScore();
}
function indexScore() {
  state.score = new Map(); for (const g of document.querySelectorAll('#score g.note')) state.score.set(g.id, g);
  if (state.follow) resetFollow(currentTime());
}
$('#roll').addEventListener('click', (e) => { if (!state.roll || !state.events) return; const r = e.currentTarget.getBoundingClientRect(); const pps = 110; seek(Math.max(0, currentTime() + (e.clientX - r.left - r.width * 0.3) / pps)); });
$('#score').addEventListener('click', (e) => {
  const g = e.target.closest('g.note'); if (!g || !state.follow) return;
  const ev = state.follow.idToEv.get(g.id); if (ev === undefined) return;
  seek(Math.max(0, state.events.t[ev] - 0.15)); if (!state.playing) play();
});

// ---------- following ----------
function buildFollow() {
  const E = state.events; if (!E) return;
  const ons = []; const offOf = new Int32Array(E.n).fill(-1); const lastOn = new Map();
  for (let i = 0; i < E.n; i++) {
    if (E.type[i] === 1) { ons.push(i); const prev = lastOn.get(E.note[i]); if (prev !== undefined && offOf[prev] < 0) offOf[prev] = i; lastOn.set(E.note[i], i); }
    else if (E.type[i] === 0) { const prev = lastOn.get(E.note[i]); if (prev !== undefined && offOf[prev] < 0) offOf[prev] = i; }
  }
  const byPitch = new Map(); for (const i of ons) { if (!byPitch.has(E.note[i])) byPitch.set(E.note[i], []); byPitch.get(E.note[i]).push(i); }
  const evToId = new Map(), idToEv = new Map();
  for (const a of state.align || []) {
    const cands = byPitch.get(a.pitch); if (!cands) continue;
    let best = -1, bd = 0.04; for (const i of cands) { const d = Math.abs(E.t[i] - a.onset); if (d < bd) { bd = d; best = i; } }
    if (best >= 0) { evToId.set(best, a.id); if (!idToEv.has(a.id)) idToEv.set(a.id, best); }
  }
  state.follow = { ons, offOf, evToId, idToEv, ptr: 0, active: [], lastScroll: 0, lastT: -1 };
  resetFollow(0);
}
function clearFollow() { if (state.follow) for (const a of state.follow.active) a.el.classList.remove('on'); state.follow = null; state.events = null; state.roll = null; $('#play').disabled = true; $('#seek').disabled = true; }
function resetFollow(t) {
  const F = state.follow; if (!F) return; const E = state.events;
  for (const a of F.active) a.el.classList.remove('on'); F.active = [];
  let lo = 0, hi = F.ons.length; while (lo < hi) { const mid = (lo + hi) >> 1; if (E.t[F.ons[mid]] < t) lo = mid + 1; else hi = mid; }
  F.ptr = lo; F.lastT = t; F.lastScroll = 0;
  // notes already sounding at t
  for (let k = Math.max(0, lo - 60); k < lo; k++) { const i = F.ons[k]; const off = F.offOf[i] >= 0 ? E.t[F.offOf[i]] : E.t[i] + 1; if (off > t) light(i, off, false); }
}
function light(i, off, scroll) {
  const F = state.follow, id = F.evToId.get(i); if (!id || !state.score) return;
  const el = state.score.get(id); if (!el) return;
  el.classList.add('on'); F.active.push({ el, off: Math.max(off, state.events.t[i] + 0.12) });
  if (scroll) keepVisible(el);
}
function keepVisible(el) {
  const F = state.follow, now = performance.now(); if (now - F.lastScroll < 200) return;
  const wrap = $('#scorewrap'), r = el.getBoundingClientRect(), w = wrap.getBoundingClientRect();
  const y = r.top - w.top;
  // Jump (no smooth scrolling: it stalls in background tabs) so the current system sits a third of the way down.
  if (y < w.height * 0.1 || y > w.height * 0.62) { wrap.scrollTop += y - w.height * 0.3; F.lastScroll = now; }
}
function follow(t) {
  const F = state.follow; if (!F || !state.events) return; const E = state.events;
  if (t < F.lastT - 0.05) resetFollow(t);
  F.lastT = t;
  while (F.ptr < F.ons.length && E.t[F.ons[F.ptr]] <= t) { const i = F.ons[F.ptr++]; const off = F.offOf[i] >= 0 ? E.t[F.offOf[i]] : E.t[i] + 1; light(i, off, true); }
  if (F.active.length) { const keep = []; for (const a of F.active) { if (a.off <= t) a.el.classList.remove('on'); else keep.push(a); } F.active = keep; }
}
function tick() {
  requestAnimationFrame(tick);
  if (!state.events) return;
  const t = currentTime();
  if (state.playing) { $('#seek').value = t; $('#time').textContent = fmtTime(t) + ' / ' + fmtTime(state.duration); }
  if (state.follow) follow(t); else if (state.roll) drawRoll(t);
}

// ---------- piano roll (user files without a score) ----------
function buildRoll() {
  const E = state.events, notes = [], open = new Map();
  for (let i = 0; i < E.n; i++) {
    if (E.type[i] === 1) { const q = open.get(E.note[i]) || []; q.push({ t0: E.t[i], t1: -1, pitch: E.note[i], vel: E.val[i] }); open.set(E.note[i], q); }
    else if (E.type[i] === 0) { const q = open.get(E.note[i]); if (q && q.length) { const n = q.shift(); n.t1 = E.t[i]; notes.push(n); } }
  }
  for (const q of open.values()) for (const n of q) { n.t1 = state.duration; notes.push(n); }
  notes.sort((a, b) => a.t0 - b.t0);
  state.roll = { notes, lastDraw: -1 };
  drawRoll(currentTime(), true);
}
function velocityColor(v, alpha) { const h = 220 - 220 * Math.min(1, v / 110); return `hsla(${h}, 75%, ${45 + 10 * (v / 127)}%, ${alpha})`; }
function buildVelocityLegend() { const el = $('#vlegend'); if (!el) return; const stops = []; for (let v = 1; v <= 127; v += 9) stops.push(velocityColor(v, 1)); el.style.background = `linear-gradient(90deg, ${stops.join(',')})`; }
function drawRoll(t, force) {
  const R = state.roll, c = $('#roll'); if (!R) return;
  const wrap = $('#scorewrap'), W = wrap.clientWidth, H = wrap.clientHeight, dpr = window.devicePixelRatio || 1;
  if (c.width !== Math.round(W * dpr) || c.height !== Math.round(H * dpr)) { c.width = Math.round(W * dpr); c.height = Math.round(H * dpr); c.style.width = W + 'px'; c.style.height = H + 'px'; force = true; }
  if (!force && Math.abs(t - R.lastDraw) < 0.004) return; R.lastDraw = t;
  const g = c.getContext('2d'); g.setTransform(dpr, 0, 0, dpr, 0, 0);
  g.fillStyle = '#fbfaf5'; g.fillRect(0, 0, W, H);
  const pps = 110, x0 = t - W * 0.3 / pps, x1 = x0 + W / pps, top = 24, bottom = H - 30, kh = (bottom - top) / 88;
  const y = p => bottom - (p - 20) * kh;
  for (let p = 21; p <= 108; p++) { const pc = p % 12; if ([1, 3, 6, 8, 10].includes(pc)) { g.fillStyle = 'rgba(0,0,0,0.045)'; g.fillRect(0, y(p) - kh, W, kh); } if (pc === 0) { g.fillStyle = 'rgba(0,0,0,0.12)'; g.fillRect(0, y(p), W, 1); g.fillStyle = '#999'; g.font = '11px sans-serif'; g.fillText('C' + (p / 12 - 1), 4, y(p) - 2); } }
  for (let s = Math.ceil(x0); s <= x1; s++) { const x = (s - x0) * pps; g.fillStyle = s % 10 === 0 ? 'rgba(0,0,0,0.18)' : 'rgba(0,0,0,0.07)'; g.fillRect(x, top, 1, bottom - top); if (s % 10 === 0) { g.fillStyle = '#777'; g.fillText(fmtTime(s), x + 3, H - 12); } }
  const N = R.notes; let lo = 0, hi = N.length; while (lo < hi) { const m = (lo + hi) >> 1; if (N[m].t0 < x0 - 30) lo = m + 1; else hi = m; }
  for (let i = lo; i < N.length && N[i].t0 <= x1; i++) {
    const n = N[i]; if (n.t1 < x0) continue;
    const xa = (n.t0 - x0) * pps, xb = Math.max(xa + 2, (n.t1 - x0) * pps), on = n.t0 <= t && t < n.t1;
    g.fillStyle = velocityColor(n.vel, on ? 1 : 0.75); g.fillRect(xa, y(n.pitch) - kh + 0.5, xb - xa, kh - 1);
    if (on) { g.strokeStyle = '#e0553d'; g.lineWidth = 1.5; g.strokeRect(xa, y(n.pitch) - kh + 0.5, xb - xa, kh - 1); }
  }
  g.fillStyle = '#e0553d'; g.fillRect(W * 0.3, top, 2, bottom - top);
  g.fillStyle = '#666'; g.font = '12px sans-serif'; g.fillText('Piano roll (no score for this file) · velocity = colour', 40, 16);
}

// ---------- settings palette ----------
function fmtVal(s, v) {
  if (s.type === 'toggle') return v > 0.5 ? 'On' : 'Off';
  if (s.type === 'choice') { const o = s.options.find(o => o[0] === Math.round(v)); return o ? o[1] : String(v); }
  const d = s.step < 1 ? (s.step < 0.1 ? 2 : 1) : 0; return (v > 0 && s.unit === 'dB' ? '+' : '') + (+v).toFixed(d) + ' ' + s.unit;
}
function getVal(id) { return id in state.values ? state.values[id] : state.defaults[id]; }
function setVal(id, v, rerender = true) {
  if (Math.abs(v - state.defaults[id]) < 1e-9) delete state.values[id]; else state.values[id] = v;
  if (state.node && state.ready) state.node.port.postMessage({ type: 'set', id, value: v });
  try { localStorage.setItem('pfsynth.settings', JSON.stringify(state.values)); } catch (e) {}
  if (rerender) renderSettings($('#setsearch').value);
}
function renderSettings(q) {
  const list = $('#setlist'); list.innerHTML = ''; const words = q.toLowerCase().split(/\s+/).filter(Boolean); let any = false;
  for (const g of GROUPS) {
    const items = SETTINGS.filter(s => s.group === g.key && words.every(w => (s.name + ' ' + s.desc + ' ' + g.name).toLowerCase().includes(w)));
    if (!items.length) continue; any = true;
    const div = document.createElement('div'); div.className = 'group' + (g.collapsed && !words.length && !state['open_' + g.key] ? ' collapsed' : '');
    div.innerHTML = `<h3><span class="arrow">▾</span>${g.name}</h3>` + (g.hint ? `<div class="hint">${g.hint}</div>` : '') + '<div class="items"></div>';
    div.querySelector('h3').onclick = () => { div.classList.toggle('collapsed'); state['open_' + g.key] = !div.classList.contains('collapsed'); };
    const box = div.querySelector('.items');
    for (const s of items) {
      const v = getVal(s.id), def = state.defaults[s.id], changed = Math.abs(v - def) > 1e-9;
      const c = document.createElement('div'); c.className = 'ctl';
      c.innerHTML = `<div class="row"><span class="name">${s.name}</span><span class="val">${fmtVal(s, v)}</span><span class="def${changed ? ' changed' : ''}" title="Click to reset">${changed ? 'default ' + fmtVal(s, def) : 'default'}</span></div><div class="desc">${s.desc}</div>`;
      c.querySelector('.def').onclick = () => setVal(s.id, def);
      const row = c.querySelector('.row');
      if (s.type === 'toggle') { const sw = document.createElement('span'); sw.className = 'switch' + (v > 0.5 ? ' on' : ''); sw.innerHTML = '<i></i>'; sw.onclick = () => setVal(s.id, v > 0.5 ? 0 : 1); row.insertBefore(sw, row.querySelector('.val')); }
      else if (s.type === 'choice') { const sel = document.createElement('select'); for (const [ov, ol] of s.options) { const o = document.createElement('option'); o.value = ov; o.textContent = ol; if (ov === Math.round(v)) o.selected = true; sel.appendChild(o); } sel.onchange = () => setVal(s.id, +sel.value); row.insertBefore(sel, row.querySelector('.val')); row.querySelector('.val').remove(); }
      else { const r = document.createElement('input'); r.type = 'range'; r.min = s.min; r.max = s.max; r.step = s.step; r.value = v; r.oninput = () => { setVal(s.id, +r.value, false); c.querySelector('.val').textContent = fmtVal(s, +r.value); const d = c.querySelector('.def'); const ch = Math.abs(+r.value - def) > 1e-9; d.classList.toggle('changed', ch); d.textContent = ch ? 'default ' + fmtVal(s, def) : 'default'; }; c.appendChild(r); }
      box.appendChild(c);
    }
    list.appendChild(div);
  }
  if (!any) list.innerHTML = '<div class="empty">No settings match.</div>';
}
function openSettings(open) { const p = $('#settings'); p.classList.toggle('open', open); p.setAttribute('aria-hidden', String(!open)); if (open) { $('#setsearch').focus(); $('#setsearch').select(); } }

// ---------- wiring ----------
function init() {
  buildKeys(); buildVelocityLegend(); renderSettings('');
  try { const saved = JSON.parse(localStorage.getItem('pfsynth.settings') || '{}'); for (const k in saved) state.values[+k] = saved[k]; renderSettings(''); } catch (e) {}
  $('#search').addEventListener('input', e => renderPieces(e.target.value));
  $('#setsearch').addEventListener('input', e => renderSettings(e.target.value));
  $('#play').onclick = () => state.playing ? pause() : play();
  $('#seek').addEventListener('input', e => { const t = +e.target.value; $('#time').textContent = fmtTime(t) + ' / ' + fmtTime(state.duration); seek(t); });
  $('#settingsbtn').onclick = () => openSettings(!$('#settings').classList.contains('open'));
  $('#setclose').onclick = () => openSettings(false);
  $('#resetall').onclick = () => { for (const s of SETTINGS) setVal(s.id, state.defaults[s.id], false); renderSettings($('#setsearch').value); };
  document.addEventListener('keydown', e => {
    const typing = /INPUT|SELECT|TEXTAREA/.test(document.activeElement.tagName);
    if ((e.metaKey || e.ctrlKey) && e.key.toLowerCase() === 'k') { e.preventDefault(); openSettings(true); return; }
    if (e.key === 'Escape') { if ($('#settings').classList.contains('open')) openSettings(false); else document.activeElement.blur(); return; }
    if (typing) return;
    if (e.key === '/') { e.preventDefault(); $('#search').focus(); $('#search').select(); }
    else if (e.key === ' ') { e.preventDefault(); if (!$('#play').disabled) (state.playing ? pause() : play()); }
    else if (e.key === 'ArrowLeft' || e.key === 'ArrowRight') { if (state.events) seek(Math.max(0, Math.min(state.duration, currentTime() + (e.key === 'ArrowLeft' ? -5 : 5)))); }
  });
  setupDrop(); loadPieces(); requestAnimationFrame(tick);
  window.addEventListener('resize', () => { if (state.roll) drawRoll(currentTime(), true); });
}
init();
