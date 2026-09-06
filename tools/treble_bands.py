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
edges=[150,300,600,1200,2400,4800,9600,19000]
def bands(x,a,b):
    x=align(x); seg=x[int(a*SR):int(b*SR)]*np.hanning(int((b-a)*SR)); X=np.abs(np.fft.rfft(seg))**2; f=np.fft.rfftfreq(len(seg),1/SR)
    tot=(b-a); return [10*np.log10(X[(f>=lo)&(f<hi)].sum()/len(seg)/tot*2+1e-18) for lo,hi in zip(edges[:-1],edges[1:])]
print('octave-band levels (dB, arbitrary but comparable): bands 150-300 300-600 600-1.2k 1.2-2.4k 2.4-4.8k 4.8-9.6k 9.6-19k')
for midi,vel in [(87,90),(93,90),(99,90),(105,90)]:
    p=ptq_render.note(midi,vel); full=model(midi,vel,[],'full'); fit=model(midi,vel,['--body','0','--knock','0','--noise','0'],'fit'); tone=model(midi,vel,['--no-attack','--no-resonance'],'tone')
    print(f'{name(midi)} v{vel}  f1={440*2**((midi-69)/12):.0f} Hz')
    for a,b,lab in [(0,.03,'0-30ms  '),(.03,.2,'30-200ms'),(.2,1,'0.2-1s  ')]:
        for tag,x in [('pianoteq',p),('model   ',full),('fit-lvl ',fit),('tone    ',tone)]:
            print(f'   {lab} {tag}',' '.join(f'{v:6.1f}' for v in bands(x,a,b)))
