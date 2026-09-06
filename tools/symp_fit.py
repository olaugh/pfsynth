"""Fit pf_resonance (sympathetic string resonance) to Pianoteq as a black box.

Renders lone notes (C3, C4, C5, v80, held 3 s) with the sustain pedal DOWN vs UP through
Pianoteq (cached) and through build/pfrender with the resonance parameters under test, and
compares, per window: (a) energy of the note's own partials pedal-down vs up (coincident
strings extending the sustain), (b) the halo of other strings' partials re the own partials.
Usage: symp_fit.py [measure|grid|best]  (PF_REF=ptq)"""
import sys,json,subprocess,itertools,numpy as np
from pathlib import Path
sys.path.insert(0,str(Path(__file__).parent))
from partial_audition import SR,name
import ptq_render
from mkmidi import write_midi
ROOT=Path(__file__).resolve().parent.parent; PFRENDER=ROOT/'build/pfrender'; OUT=ROOT/'experiments/sympathetic'; OUT.mkdir(exist_ok=True)
NOTES=[(48,130.8,0.000115),(60,261.6,0.000325),(72,523.3,0.00095)]
WINDOWS=[(0.15,0.6),(0.6,1.5),(1.5,3.0)]
def events(midi,down):
    return ([(0.0,0xB0,64,127)] if down else [(0.0,0xB0,64,0)])+[(0.1,0x90,midi,80),(3.1,0x80,midi,0)]+([(4.5,0xB0,64,0)] if down else [])+[(5.0,0xB0,123,0)]
def midi_path(midi,down):
    p=ptq_render.CACHE/f"symp-{'down' if down else 'up'}-{midi}.mid"
    if not p.exists(): write_midi(p,events(midi,down))
    return p
def ptq(midi,down):
    wav=ptq_render.CACHE/f"symp-{'down' if down else 'up'}-{midi}.wav"
    if not wav.exists(): ptq_render.run_ptq(midi_path(midi,down),wav)
    return ptq_render.load_mono(wav)
def model(midi,down,args):
    wav=OUT/f"model-{'down' if down else 'up'}-{midi}.wav"
    subprocess.run([str(PFRENDER),str(midi_path(midi,down)),str(wav),'0','5','--raw']+args,check=True,capture_output=True)
    return ptq_render.load_mono(wav)
def spec(x,a,b):
    seg=x[int(a*SR):int(b*SR)]; seg=seg*np.hanning(len(seg)); X=np.abs(np.fft.rfft(seg,1<<17))**2; return np.fft.rfftfreq(1<<17,1/SR),X
def metrics(up,dn,f1,B):
    ks=np.arange(1,60); fk=f1*ks*np.sqrt((1+B*ks*ks)/(1+B)); m=[]
    for a,b in WINDOWS:
        f,Xu=spec(up,a,b); _,Xd=spec(dn,a,b); own=np.zeros(len(f),bool)
        for fx in fk[fk<12000]: own|=np.abs(f-fx)<max(6,fx*0.006)
        band=(f>=30)&(f<8000)
        m.append(dict(own_gain=10*np.log10(Xd[band&own].sum()/Xu[band&own].sum()), halo=10*np.log10(Xd[band&~own].sum()/Xd[band&own].sum()), halo_up=10*np.log10(Xu[band&~own].sum()/Xu[band&own].sum())))
    f,Xu=spec(up,.6,3); _,Xd=spec(dn,.6,3); own=np.zeros(len(f),bool)
    for fx in fk[fk<12000]: own|=np.abs(f-fx)<max(6,fx*0.006)
    d=np.maximum(Xd-Xu,0); tot=Xd[(f>=30)&(f<8000)].sum(); edges=[60,120,250,500,1000,2000,4000]
    bands=[float(10*np.log10(d[(f>=lo)&(f<hi)&~own].sum()/tot+1e-12)) for lo,hi in zip(edges[:-1],edges[1:])]
    return m,bands
def measure(args,label,quiet=False):
    err=0;rows=[]
    for midi,f1,B in NOTES:
        mp,bp=metrics(ptq(midi,0),ptq(midi,1),f1,B)
        mm,bm=metrics(model(midi,0,args),model(midi,1,args),f1,B)
        for w,(a,b) in enumerate(WINDOWS):
            err+=(mm[w]['halo']-mp[w]['halo'])**2+ (mm[w]['own_gain']-mp[w]['own_gain'])**2*(0.5 if w<2 else 1)
            rows.append((name(midi),f'{a:.2f}-{b:.1f}',mp[w]['halo'],mm[w]['halo'],mp[w]['own_gain'],mm[w]['own_gain']))
        err+=0.15*sum((x-y)**2 for x,y in zip(bm,bp))
        if not quiet: print(f'   {name(midi)} halo bands 60-120..2k-4k  ptq {np.round(bp,1)}  model {np.round(bm,1)}')
    if not quiet:
        print(f'{label}: error {err:.1f}'); print('   note  window     halo ptq / model     own-gain ptq / model')
        for r in rows: print(f'   {r[0]:4s} {r[1]:9s}  {r[2]:6.1f} / {r[3]:6.1f}       {r[4]:5.1f} / {r[5]:5.1f}')
    return err
if __name__=='__main__':
    mode=sys.argv[1] if len(sys.argv)>1 else 'measure'
    if mode=='measure':
        args=sys.argv[2:]; measure(['--no-resonance'],'no resonance'); measure(args,'resonance '+' '.join(args))
    elif mode=='grid':
        best=None
        for c,sk,su,ti in itertools.product([-32,-34,-36],[1,1.5,2,3],[-12,-15,-18],[0]):
            args=['--res-coupling',str(c),'--res-skirt',str(sk),'--res-sustain',str(su),'--res-tilt',str(ti)]
            e=measure(args,'',quiet=True); print(f'{e:7.1f}  coupling {c} skirt {sk} sustain {su} tilt {ti}',flush=True)
            if best is None or e<best[0]: best=(e,args)
        print('BEST',best); measure(best[1],'best')
        json.dump({'args':best[1],'error':best[0]},open(OUT/'fit.json','w'),indent=1)
