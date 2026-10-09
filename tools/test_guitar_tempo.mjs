// Exercise the real AudioWorklet message path and WASM; no browser/audio device needed.
import fs from 'node:fs';
import vm from 'node:vm';
import assert from 'node:assert/strict';
const base=new URL('../',import.meta.url),wasm=fs.readFileSync(new URL('docs/guitar/pfguitar.wasm',base));
const source=fs.readFileSync(new URL('docs/guitar/guitar-worklet.js',base),'utf8');
const tuning=[64,59,55,50,45,40];
async function player(sr,notes,duration=12,tempo=1){
  let Processor;const messages=[];
  const context=vm.createContext({sampleRate:sr,WebAssembly,DataView,Uint8Array,Float32Array,performance,Date,decodeURIComponent,escape,
    AudioWorkletProcessor:class{constructor(){this.port={postMessage:m=>messages.push(m)};}},registerProcessor:(_,c)=>{Processor=c;}});
  vm.runInContext(source,context);const p=new Processor();await p.onMessage({type:'wasm',bytes:wasm});
  const ready=messages.find(m=>m.type==='ready');assert(ready);
  await p.onMessage({type:'param',index:ready.params.find(p=>p.name==='Let strings ring').index,value:0});
  await p.onMessage({type:'score',notes,tuning,duration,tempo});assert(messages.at(-1).ok);
  await p.onMessage({type:'play'});return p;
}
function render(p,n){
  const y=new Float32Array(n);
  for(let i=0;i<n;i+=128){const size=Math.min(128,n-i),out=[new Float32Array(size),new Float32Array(size)];p.process([], [out]);y.set(out[0],i);}
  assert(y.every(Number.isFinite));return y;
}
function same(a,b){assert.deepEqual(Buffer.from(a.buffer),Buffer.from(b.buffer));}
const energy=a=>a.reduce((v,x)=>v+x*x,0)/a.length;
const note=(start,end,pitch=40,fret=0,articulation=0)=>({start,end,pitch,fret,string:6,velocity:100,articulation});
for(const sr of [44100,48000]){
  // Sustain survives without re-plucking, even when the clock's block alignment changes.
  const p=await player(sr,[note(0,8)]),control=await player(sr,[note(0,8)]);
  same(render(p,sr),render(control,sr));
  await p.onMessage({type:'tempo',rate:.5});assert.equal(p.ex.pfiw_time(),2);assert.equal(p.duration,24);
  const continued=render(p,sr);same(continued,render(control,sr));assert(energy(continued)>1e-12);
  await p.onMessage({type:'pause'});
  await p.onMessage({type:'tempo',rate:.25});await p.onMessage({type:'tempo',rate:.75});
  assert.equal(p.playing,false);assert(render(p,128).every(v=>v===0));
  await p.onMessage({type:'play'});same(render(p,512),render(control,512));
  const before=p.ex.pfiw_time();await assert.rejects(p.onMessage({type:'tempo',rate:NaN}));assert.equal(p.ex.pfiw_time(),before);
  // Full schedule must be reconstructed when restarting after a mid-song change.
  await p.onMessage({type:'seek',t:0});const slow=await player(sr,[note(0,8)],12,.75);
  same(render(p,sr),render(slow,sr));
  // Retimed future attacks, connected notes and release: equivalent remaining schedule.
  const phrase=[note(0,2),note(2,3,43,3,1),note(3,4,41,1,2),{...note(4,5,45,5),slide_to:47},note(5,6,47,7,5)];
  const a=await player(sr,phrase),b=await player(sr,phrase.map(n=>({...n,start:n.start===0?0:2*n.start-1,end:2*n.end-1})));
  same(render(a,sr),render(b,sr));await a.onMessage({type:'tempo',rate:.5});
  // Sliding phase should also be stretched; exact block sampling may differ by <128 frames.
  same(render(a,5*sr),render(b,5*sr)); // hammer/pull and the final release of them
  const tail=render(a,5*sr);assert(energy(tail)>0);
  // End gates also move: a held note must not release at its old deadline.
  const gate=await player(sr,[note(0,2)]),gateRef=await player(sr,[note(0,3)]);
  same(render(gate,sr),render(gateRef,sr));await gate.onMessage({type:'tempo',rate:.5});
  same(render(gate,3*sr),render(gateRef,3*sr));
  // Harmonic contact lasts the same physical time across a live tempo change.
  const harmonic={...note(0,3),articulation:4,art_param:12};
  const h=await player(sr,[harmonic]),hc=await player(sr,[harmonic]);
  same(render(h,Math.round(.02*sr)),render(hc,Math.round(.02*sr)));
  await h.onMessage({type:'tempo',rate:.5});same(render(h,Math.round(.1*sr)),render(hc,Math.round(.1*sr)));
  // Identical simultaneous-event ordering after extreme and repeated changes.
  const boundary=await player(sr,[note(0,1),note(1,2)]);render(boundary,sr);
  await boundary.onMessage({type:'tempo',rate:.25});await boundary.onMessage({type:'tempo',rate:1});
  const normal=await player(sr,[note(0,1),note(1,2)]);render(normal,sr);same(render(boundary,sr),render(normal,sr));
  console.log(`Tempo ${sr}: sustain, pause, restart, legato, pending releases, harmonic contact and boundary events PASS`);
}
