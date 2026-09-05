"""Survey MIDI corpora (MAESTRO, ASAP/nASAP) for pedal usage: half pedal (intermediate CC64),
una corda (CC67) and sostenuto (CC66). Prints ranked shortlists for audition passages."""
import csv, struct, sys, json
from pathlib import Path
import numpy as np
MAESTRO=Path('maestro/midi/maestro-v3.0.0'); ASAP=Path('/Users/john/sources/segno/tools/synth-ab/corpora/asap')
def vlq(d,i):
    n=0
    while True:
        b=d[i]; i+=1; n=(n<<7)|(b&0x7f)
        if not b&0x80: return n,i
def scan(path):
    d=path.read_bytes()
    if d[:4]!=b'MThd': return None
    fmt,ntr,div=struct.unpack('>HHH',d[8:14]); i=14; tempo=[(0,500000)]; ccs={64:[],66:[],67:[]}; notes=0; last=0
    for _ in range(ntr):
        if d[i:i+4]!=b'MTrk': break
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
            if st==0xB0 and d[j] in ccs: ccs[d[j]].append((tick,d[j+1]))
            if st==0x90 and d[j+1]>0: notes+=1
            j+=1 if st in (0xC0,0xD0) else 2
            last=max(last,tick)
        i=end
    tempo.sort(); 
    def t_of(tick):
        s=0.0; pt,pu=tempo[0]
        for tk,us in tempo[1:]:
            if tk>=tick: break
            s+=(tk-pt)*pu/1e6/div; pt,pu=tk,us
        return s+(tick-pt)*pu/1e6/div
    dur=t_of(last)
    def stats(ev,lo,hi):
        ev=sorted(ev); 
        if not ev: return dict(n=0,inter=0.0,share=0.0,exc=0)
        vals=np.array([v for _,v in ev]); ts=np.array([t_of(tk) for tk,_ in ev])
        inter=float(np.mean((vals>=8)&(vals<=119)))
        # time share spent in the zone [lo,hi]
        seg=np.diff(np.append(ts,dur)); share=float(np.sum(seg[(vals>=lo)&(vals<=hi)])/max(dur,1e-9))
        inz=(vals>=lo)&(vals<=hi); exc=int(np.sum(inz[1:]&~inz[:-1])+ (1 if inz[0] else 0))
        return dict(n=len(ev),inter=inter,share=share,exc=exc)
    return dict(duration=dur,notes=notes,cc64=stats(ccs[64],48,88),cc67=stats(ccs[67],24,127),cc66=dict(n=len(ccs[66])))
rows=[]
if MAESTRO.exists():
    for r in csv.DictReader(open(MAESTRO/'maestro-v3.0.0.csv')):
        p=MAESTRO/r['midi_filename']
        if p.exists():
            s=scan(p)
            if s: rows.append(dict(corpus='maestro',composer=r['canonical_composer'],title=r['canonical_title'],year=r['year'],path=str(p),**s))
for r in csv.DictReader(open(ASAP/'metadata.csv')):
    p=ASAP/r['midi_performance']
    if p.exists():
        s=scan(p)
        if s: rows.append(dict(corpus='asap',composer=r['composer'],title=r['title'],year='',path=str(p),maestro_audio=bool(r.get('maestro_audio_performance')),**s))
json.dump(rows,open('experiments/pedal/pedal-survey.json','w'),indent=1)
def show(title,key,n=12,filt=lambda r:True):
    print(f'\n== {title} ==')
    for r in sorted([r for r in rows if filt(r)],key=key,reverse=True)[:n]:
        print(f"  {r['corpus']:7s} {r['composer'][:18]:18s} {r['title'][:44]:44s} {r['duration']/60:5.1f}min  CC64 n={r['cc64']['n']:4d} half-zone {r['cc64']['share']*100:4.0f}% ({r['cc64']['exc']:3d} exc)  CC67 n={r['cc67']['n']:4d} on {r['cc67']['share']*100:3.0f}%  CC66 n={r['cc66']['n']}  {Path(r['path']).name}")
print(f'scanned {len(rows)} files: maestro {sum(r["corpus"]=="maestro" for r in rows)}, asap {sum(r["corpus"]=="asap" for r in rows)}')
print(f"files with any CC67: {sum(r['cc67']['n']>0 for r in rows)}, with CC66: {sum(r['cc66']['n']>0 for r in rows)}, with intermediate CC64: {sum(r['cc64']['inter']>0.3 for r in rows)}")
show('Richest half pedal (time in CC64 48-88 zone, >=2 min pieces)',lambda r:r['cc64']['share']*min(1,r['cc64']['exc']/50),filt=lambda r:r['duration']>120)
show('Most una corda (CC67 on-time share, many events)',lambda r:r['cc67']['share']*min(1,r['cc67']['n']/40))
show('Both: half pedal AND una corda',lambda r:r['cc64']['share']*r['cc67']['share']*min(1,r['cc67']['n']/40))
show('Sostenuto (CC66) users',lambda r:r['cc66']['n'],n=10,filt=lambda r:r['cc66']['n']>0)
