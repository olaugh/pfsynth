import sys,subprocess,itertools,numpy as np
from pathlib import Path
sys.path.insert(0,str(Path(__file__).parent))
import ptq_render
from ptq_render import name,single_note,load_mono
from partial_audition import SR
ROOT=Path(__file__).resolve().parent.parent; PFRENDER=ROOT/'build/pfrender'; OUT=ROOT/'build/treble'
def model(midi,vel,args):
    mid=OUT/f'{name(midi)}v{vel}.mid'; wav=OUT/f'{name(midi)}v{vel}-fit.wav'
    if not mid.exists(): single_note(mid,midi,vel,8.0,1.0)
    subprocess.run([str(PFRENDER),str(mid),str(wav),'0','2','--raw']+args,check=True,capture_output=True); return load_mono(wav)
def align(x):
    i=np.argmax(np.abs(x)>np.abs(x).max()*0.05); return x[max(0,i-int(0.005*SR)):]
edges=[150,600,1200,2000,2900,4800]; wins=[(0,.02),(.02,.05),(.05,.1),(.1,.2),(.2,.4)]
def grid(x):
    x=align(x); g=np.zeros((len(edges)-1,len(wins)))
    for j,(a,b) in enumerate(wins):
        seg=x[int(a*SR):int(b*SR)]*np.hanning(int((b-a)*SR)); X=np.abs(np.fft.rfft(seg))**2; f=np.fft.rfftfreq(len(seg),1/SR)
        for i,(lo,hi) in enumerate(zip(edges[:-1],edges[1:])): g[i,j]=10*np.log10(X[(f>=lo)&(f<hi)].sum()/len(seg)/(b-a)*2+1e-18)
    return g
NOTES=[(99,100),(102,100),(105,100),(102,70),(105,70)]
ref={k:grid(ptq_render.note(*k)) for k in NOTES}
def err(args):
    e=0
    for k in NOTES:
        g=grid(model(k[0],k[1],args)); f1=440*2**((k[0]-69)/12)
        for i,(lo,hi) in enumerate(zip(edges[:-1],edges[1:])):
            if hi>f1*0.98: continue            # only bands below the fundamental (the knock), the tone is the patch's business
            w=1.0 if lo>=1200 else 0.3          # the sub-fundamental knock bands carry the clang
            d=g[i]-ref[k][i]; d[0]*=0.5           # excess over Pianoteq is the clang; a deficit only dulls (quarter weight); the 0-20 ms window half
            e+=w*(np.sum(np.maximum(d,0)**2)+0.25*np.sum(np.minimum(d,0)**2))
    return e
print('baseline (t60 1, db 0):',round(err(['--top-knock-t60','1','--top-knock-db','0']),1))
best=None
for lo,t60,db in itertools.product([.45,.6,.72],[1,.5,.3,.2],[0,3,6]):
    e=err(['--top-knock-t60',str(t60),'--top-knock-db',str(db),'--top-knock-lo',str(lo)]); print(f'{e:8.1f}  lo {lo} t60 x{t60} level {db:+d} dB',flush=True)
    if best is None or e<best[0]: best=(e,lo,t60,db)
print('BEST',best)
