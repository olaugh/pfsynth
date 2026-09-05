"""Find ~25 s windows rich in half pedaling and una corda in candidate performances."""
import struct, sys, json
from pathlib import Path
import numpy as np
def vlq(d,i):
    n=0
    while True:
        b=d[i]; i+=1; n=(n<<7)|(b&0x7f)
        if not b&0x80: return n,i
def events(path):
    d=path.read_bytes(); fmt,ntr,div=struct.unpack('>HHH',d[8:14]); i=14; tempo=[(0,500000)]; raw=[]
    for _ in range(ntr):
        L=struct.unpack('>I',d[i+4:i+8])[0]; j=i+8; end=j+L; run=None; tick=0
        while j<end:
            dt,j=vlq(d,j); tick+=dt; b=d[j]
            if b==0xFF:
                ty=d[j+1]; j+=2; n,j=vlq(d,j)
                if ty==0x51: tempo.append((tick,int.from_bytes(d[j:j+3],'big')))
                j+=n; continue
            if b in (0xF0,0xF7): j+=1; n,j=vlq(d,j); j+=n; continue
            if b&0x80: run=b; j+=1
            st=run&0xF0
            if st in (0x80,0x90,0xB0): raw.append((tick,st,d[j],d[j+1]))
            j+=1 if st in (0xC0,0xD0) else 2
        i=end
    tempo.sort()
    def t_of(tick):
        s=0.0; pt,pu=tempo[0]
        for tk,us in tempo[1:]:
            if tk>=tick: break
            s+=(tk-pt)*pu/1e6/div; pt,pu=tk,us
        return s+(tick-pt)*pu/1e6/div
    return sorted((t_of(tk),st,a,b) for tk,st,a,b in raw)
def windows(path,W=25,step=2.5):
    ev=events(path); T=ev[-1][0]; out=[]
    ons=np.array([t for t,st,a,b in ev if st==0x90 and b>0]); cc64=[(t,b) for t,st,a,b in ev if st==0xB0 and a==64]; cc67=[(t,b) for t,st,a,b in ev if st==0xB0 and a==67]; cc66=[(t,b) for t,st,a,b in ev if st==0xB0 and a==66]
    for t0 in np.arange(0,max(0,T-W),step):
        n=int(np.sum((ons>=t0)&(ons<t0+W)))
        c64=[v for t,v in cc64 if t0<=t<t0+W]; c67=[v for t,v in cc67 if t0<=t<t0+W]; c66=[v for t,v in cc66 if t0<=t<t0+W]
        inz=[60<=v<=84 for v in c64]; half=sum(1 for a,b in zip(inz,inz[1:]) if b and not a)   # entries into Pianoteq's half-pedal zone
        soft_var=float(np.std(c67)) if len(c67)>3 else 0.0; soft_on=float(np.mean([v>24 for v in c67])) if c67 else 0.0
        if 15<=n<=110: out.append((half*(1+soft_var/40),t0,n,half,len(c64),len(c67),round(soft_var),round(soft_on,2),len(c66)))
    return sorted(out,reverse=True)[:3]
M=Path('maestro/midi/maestro-v3.0.0'); A=Path('/Users/john/sources/segno/tools/synth-ab/corpora/asap')
cands={'Beethoven Op.110 I (Kavalerova01, asap)':A/'Beethoven/Piano_Sonatas/31-1/Kavalerova01.mid','Tchaikovsky Lullaby (maestro 2006)':M/'2006/MIDI-Unprocessed_13_R1_2006_01-06_ORIG_MID--AUDIO_13_R1_2006_04_Track04_wav.midi',
 'Scarlatti K.213 (maestro 2018)':M/'2018/MIDI-Unprocessed_Recital9-11_MID--AUDIO_09_R1_2018_wav--5.midi','Schubert Op.90/3 (Kociuban10M, asap)':A/'Schubert/Impromptu_op.90_D.899/3/Kociuban10M.mid',
 'Beethoven Andante favori (maestro 2018)':M/'2018/MIDI-Unprocessed_Chamber6_MID--AUDIO_20_R3_2018_wav--1.midi','Mozart Rondo K.511 (maestro 2011)':M/'2011/MIDI-Unprocessed_05_R1_2011_MID--AUDIO_R1-D2_12_Track12_wav.midi'}
for name,p in cands.items():
    if not p.exists(): print(name,'MISSING',p); continue
    print(name)
    for score,t0,n,half,n64,n67,sv,son,n66 in windows(p): print(f'   t={t0:6.1f}s  notes {n:3d}  half-pedal entries {half:2d}  CC64 ev {n64:3d}  CC67 ev {n67:3d} (std {sv:2d}, on {son:.2f})  CC66 {n66}')
