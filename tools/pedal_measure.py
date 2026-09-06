"""Measure Pianoteq's response to the sustain pedal position (CC64, half pedal) and the
soft pedal (CC67, una corda), as a black-box target for pf_partial's damper/soft-pedal model.

Half pedal: note held 0.5 s then released under a constant CC64 value; per-partial decay
rate after release vs. the rate while held gives the damper's extra damping vs position.
Una corda: per-partial initial level and decay vs CC67 at several velocities.
"""
import json, numpy as np
from scipy import signal
from partial_audition import ROOT, SR, name, Patch
from body_audition import align
import ptq_render
from mkmidi import write_midi
OUT=ROOT/'experiments/pedal'; OUT.mkdir(exist_ok=True)
patch=Patch.from_buffer_copy((ROOT/'experiments/partial-piano-wide/pianoteq.bin').read_bytes())
def tuning(midi):
    key=min(max((midi-21)/3,0),29); lo=int(key); hi=min(lo+1,29); t=key-lo
    f1=440*2**((midi-69)/12)*(patch.tuning[lo][0]*(1-t)+patch.tuning[hi][0]*t); B=np.exp(np.log(patch.tuning[lo][1])*(1-t)+np.log(patch.tuning[hi][1])*t); return f1,B
def render(tag,events):
    wav=ptq_render.CACHE/f'pedal-{tag}.wav'
    if not wav.exists(): mid=wav.with_suffix('.mid'); write_midi(mid,events); ptq_render.run_ptq(mid,wav)
    return ptq_render.load_mono(wav)
def demod(x,f,win=.03):
    t=np.arange(len(x))/SR; w=signal.windows.hann(int(win*SR)|1); w/=w.sum(); return 2*np.abs(signal.fftconvolve(x*np.exp(-2j*np.pi*f*t),w,'same'))
def partials(midi,nmax=24):
    f1,B=tuning(midi); fs=[]
    for k in range(1,nmax+1):
        f=f1*k*np.sqrt((1+B*k*k)/(1+B))
        if f<15000: fs.append(f)
    return np.array(fs)
def slope(env,a,b):
    t=np.arange(len(env))/SR; sl=slice(int(a*SR),int(b*SR)); e=20*np.log10(env[sl]+1e-9)
    return float(np.polyfit(t[sl],e,1)[0])   # dB/s
def main():
    report={'half_pedal':{},'una_corda':{}}
    # ---- half pedal: CC64 = V before the note; key 0.1-0.6 s; measure 0.15-0.5 (held) vs 0.7-1.3 (released)
    values=[0,16,32,40,48,56,60,64,68,72,80,96,127]
    for midi in [48,60,72]:
        fs=partials(midi); rows={}
        for V in values:
            ev=[(0.0,0xB0,64,V),(0.1,0x90,midi,80),(0.6,0x80,midi,0),(4.0,0xB0,64,0),(4.5,0xB0,123,0)]
            x=render(f'half-{midi}-{V}',ev); on=0.1
            held=[];rel=[]
            for f in fs:
                e=demod(x,f); held.append(slope(e,on+.08,on+.48)); rel.append(slope(e,on+.62,on+1.2))
            rows[V]=dict(held_db_s=[round(v,1) for v in held],released_db_s=[round(v,1) for v in rel])
            extra=np.array(rel)-np.array(held)
            print(f'half {name(midi)} CC64={V:3d}: extra damping after release (dB/s) p1 {extra[0]:7.1f}  p2-3 {extra[1:3].mean():7.1f}  p4-8 {extra[3:8].mean():7.1f}  p9+ {extra[8:].mean():7.1f}',flush=True)
        report['half_pedal'][name(midi)]=dict(partial_hz=[round(float(f),1) for f in fs],rows=rows)
    # ---- una corda: CC67 = S before the note, note held 3 s; initial level (0.02-0.12 s) and decay per partial
    for midi in [48,60,72]:
        fs=partials(midi); rows={}
        for vel in [50,80,110]:
            base=None
            for S in [0,32,64,96,127]:
                ev=[(0.0,0xB0,67,S),(0.1,0x90,midi,vel),(3.1,0x80,midi,0),(4.0,0xB0,123,0)]
                x=render(f'soft-{midi}-{vel}-{S}',ev); on=0.1
                lev=[];dec=[]
                for f in fs:
                    e=demod(x,f); lev.append(20*np.log10(np.sqrt(np.mean(e[int((on+.02)*SR):int((on+.12)*SR)]**2))+1e-9)); dec.append(slope(e,on+.15,on+1.5))
                lev=np.array(lev); dec=np.array(dec)
                if base is None: base=(lev,dec)
                rows[f'{vel}-{S}']=dict(level_db=[round(float(v),1) for v in lev],decay_db_s=[round(float(v),1) for v in dec],delta_level_db=[round(float(v),1) for v in lev-base[0]],delta_decay_db_s=[round(float(v),1) for v in dec-base[1]])
                d=lev-base[0]; rms=20*np.log10(np.sqrt(np.mean(x[int(on*SR):int((on+1)*SR)]**2)))
                print(f'soft {name(midi)} v{vel:3d} CC67={S:3d}: rms(1s) {rms:6.1f}  d-level p1 {d[0]:5.1f} p2-3 {d[1:3].mean():5.1f} p4-8 {d[3:8].mean():5.1f} p9+ {d[8:].mean():5.1f} dB | d-decay p1 {dec[0]-base[1][0]:6.1f} p4-8 {(dec[3:8]-base[1][3:8]).mean():6.1f} dB/s',flush=True)
        report['una_corda'][name(midi)]=dict(partial_hz=[round(float(f),1) for f in fs],rows=rows)
    (OUT/'pianoteq-pedal-measurements.json').write_text(json.dumps(report,indent=1))
if __name__=='__main__':main()
