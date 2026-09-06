"""Top register vs Pianoteq: isolated notes, model (full / no onset layer / no resonance) vs reference.
Prints peak, windowed RMS, centroid and > 4 kHz share.  PF_REF=ptq."""
import sys,subprocess,numpy as np
from pathlib import Path
sys.path.insert(0,str(Path(__file__).parent))
import ptq_render
from ptq_render import name,single_note,load_mono
from partial_audition import SR
ROOT=Path(__file__).resolve().parent.parent; PFRENDER=ROOT/'build/pfrender'; OUT=ROOT/'build/treble'; OUT.mkdir(parents=True,exist_ok=True)
def model(midi,vel,args,tag):
    mid=OUT/f'{name(midi)}v{vel}.mid'
    if not mid.exists(): single_note(mid,midi,vel,8.0,1.0)
    wav=OUT/f'{name(midi)}v{vel}-{tag}.wav'
    subprocess.run([str(PFRENDER),str(mid),str(wav),'0','3','--raw']+args,check=True,capture_output=True)
    x=load_mono(wav); return x
def align(x):
    i=np.argmax(np.abs(x)>np.abs(x).max()*0.05); return x[max(0,i-int(0.005*SR)):]
def metrics(x):
    x=align(x); n=lambda a,b:x[int(a*SR):int(b*SR)]
    rms=lambda s:20*np.log10(np.sqrt(np.mean(s**2))+1e-12)
    seg=n(0,.2)*np.hanning(int(.2*SR)); X=np.abs(np.fft.rfft(seg))**2; f=np.fft.rfftfreq(len(seg),1/SR)
    cen=(f*X).sum()/X.sum(); hi=10*np.log10(X[f>4000].sum()/X.sum()+1e-12)
    return dict(peak=20*np.log10(np.abs(x).max()+1e-12),r30=rms(n(0,.03)),r200=rms(n(.03,.2)),r1000=rms(n(.2,1)),cen=cen,hi4k=hi)
print('note  vel |  peak dBFS          | rms 0-30ms          | rms 30-200ms        | rms 0.2-1s          | centroid Hz    | >4k share dB')
print('          |  ptq  full  tone  |  ptq  full  tone  |  ptq  full  tone  |  ptq  full  tone  |  ptq  full  tone |  ptq  full  tone')
for midi in [72,78,84,90,96,99,102,105,108]:
    for vel in [60,90,115]:
        p=metrics(ptq_render.note(midi,vel)); a=metrics(model(midi,vel,[],'full')); b=metrics(model(midi,vel,['--no-attack','--no-resonance'],'tone'))
        row=f'{name(midi):4s} {vel:3d} |'
        for k in ['peak','r30','r200','r1000']: row+=f' {p[k]:5.1f} {a[k]:5.1f} {b[k]:5.1f} |'
        row+=f' {p["cen"]:5.0f} {a["cen"]:5.0f} {b["cen"]:5.0f} |'
        row+=f' {p["hi4k"]:5.1f} {a["hi4k"]:5.1f} {b["hi4k"]:5.1f}'
        print(row,flush=True)
