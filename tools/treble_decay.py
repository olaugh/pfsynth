import sys,subprocess,numpy as np
from pathlib import Path
sys.path.insert(0,str(Path(__file__).parent))
import ptq_render
from ptq_render import name,single_note,load_mono
from partial_audition import SR
ROOT=Path(__file__).resolve().parent.parent; PFRENDER=ROOT/'build/pfrender'; OUT=ROOT/'build/treble'
def model(midi,vel,args,tag):
    mid=OUT/f'{name(midi)}v{vel}.mid'; wav=OUT/f'{name(midi)}v{vel}-{tag}.wav'
    if not mid.exists(): single_note(mid,midi,vel,8.0,1.0)
    subprocess.run([str(PFRENDER),str(mid),str(wav),'0','3','--raw']+args,check=True,capture_output=True); return load_mono(wav)
def align(x):
    i=np.argmax(np.abs(x)>np.abs(x).max()*0.05); return x[max(0,i-int(0.005*SR)):]
edges=[150,600,1200,2000,2900,4800,9600]
wins=[(0,.02),(.02,.05),(.05,.1),(.1,.2),(.2,.4),(.4,.8)]
def level(x,a,b,lo,hi):
    seg=x[int(a*SR):int(b*SR)]*np.hanning(int((b-a)*SR)); X=np.abs(np.fft.rfft(seg))**2; f=np.fft.rfftfreq(len(seg),1/SR)
    return 10*np.log10(X[(f>=lo)&(f<hi)].sum()/len(seg)/(b-a)*2+1e-18)
for midi,vel in [(99,100),(102,100),(105,100)]:
    p=align(ptq_render.note(midi,vel)); m=align(model(midi,vel,[],'full')); k=align(model(midi,vel,['--treble','-12'],'k12'))
    print(f'{name(midi)} v{vel}  f1={440*2**((midi-69)/12):.0f} Hz   (rows: band; columns: windows 0-20 20-50 50-100 100-200 200-400 400-800 ms; ptq | model | model treble-12)')
    for lo,hi in zip(edges[:-1],edges[1:]):
        pr=' '.join(f'{level(p,a,b,lo,hi):6.1f}' for a,b in wins); mr=' '.join(f'{level(m,a,b,lo,hi):6.1f}' for a,b in wins); kr=' '.join(f'{level(k,a,b,lo,hi):6.1f}' for a,b in wins)
        print(f'  {lo:5d}-{hi:5d}: {pr} | {mr} | {kr}')
