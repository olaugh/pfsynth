"""Curate a diverse demo library from the local MAESTRO / ASAP performances, with per-piece
difficulty stats (polyphony, note density, dynamics, repeated notes, pedalling)."""
import csv, json, re, struct
from pathlib import Path
import numpy as np
from pedal_excerpt import events
ROOT=Path(__file__).resolve().parents[1]; M=ROOT/'maestro/midi/maestro-v3.0.0'; A=Path('/Users/john/sources/segno/tools/synth-ab/corpora/asap')
survey={str((ROOT/r['path']).resolve()) if not r['path'].startswith('/') else r['path']:r for r in json.load(open(ROOT/'experiments/pedal/pedal-survey.json'))}
def stats(path):
    ev=events(path); ons=[(t,a,b) for t,st,a,b in ev if st==0x90 and b>0]; offs={}
    T=ev[-1][0]; notes=[]; open_={}
    for t,st,a,b in ev:
        if st==0x90 and b>0: open_.setdefault(a,[]).append(t)
        elif st==0x80 or (st==0x90 and b==0):
            q=open_.get(a,[]); 
            if q: notes.append((q.pop(0),t,a))
    for a,q in open_.items():
        for t0 in q: notes.append((t0,T,a))
    notes.sort(); starts=np.array([n[0] for n in notes]); ends=np.array([n[1] for n in notes])
    # max simultaneous sounding notes (key held)
    times=np.concatenate([starts,ends]); kinds=np.concatenate([np.ones(len(starts)),-np.ones(len(ends))]); o=np.lexsort((-kinds,times)); poly=int(np.max(np.cumsum(kinds[o]))) if len(o) else 0
    vel=np.array([b for _,_,b in ons]); dens=len(ons)/max(T,1)
    pitches=np.array([a for _,a,_ in ons]); high_share=float(np.mean(pitches>=89)) if len(pitches) else 0.0; top=int(np.sum(pitches>=96))
    low_share=float(np.mean(pitches<=40)) if len(pitches) else 0.0; bottom=int(np.sum(pitches<36))
    # repeated notes: same pitch re-struck within 180 ms
    last={}; rep=0
    for t,a,b in ons:
        if a in last and t-last[a]<.18: rep+=1
        last[a]=t
    # peak density in any 2 s window
    ts=np.array([t for t,_,_ in ons]); peak=max((np.sum((ts>=t0)&(ts<t0+2)) for t0 in np.arange(0,max(T-2,1),1.0)),default=0)
    return dict(duration=round(T,1),notes=len(ons),max_polyphony=poly,notes_per_s=round(dens,1),peak_notes_2s=int(peak),vel_p5=int(np.percentile(vel,5)),vel_p95=int(np.percentile(vel,95)),repeated_rate=round(rep/max(len(ons),1),3),high_share=round(high_share,3),notes_above_c7=top,low_share=round(low_share,3),notes_below_c2=bottom)
def meta_maestro():
    out={}
    for r in csv.DictReader(open(M/'maestro-v3.0.0.csv')): out[str(M/r['midi_filename'])]=(r['canonical_composer'],r['canonical_title'])
    return out
def meta_asap():
    out={}
    for r in csv.DictReader(open(A/'metadata.csv')): out[str(A/r['midi_performance'])]=(r['composer'],r['title'].replace('_',' '))
    return out
meta={**meta_maestro(),**meta_asap()}
# Candidates: (regex on "composer title", preferred filename fragment or None, tags, why, excerpt (start,length) or None)
WANT=[
 (r'Beethoven.*(Piano Sonatas 1-1|Sonata.*Op\. 2 No\. 1)','KimG01',['dynamics'],'Op. 2 No. 1, I: our audition passage; sf/p contrasts, sustain-aware releases.',(0,45)),
 (r'Bach.*Fugue bwv 883','GuoE01M',['polyphony','pedalling'],'WTC II fugue in F-sharp minor: four independent voices, constant half pedaling.',(0,45)),
 (r'Liszt.*La campanella','TET03',['repeated notes','polyphony'],'Repeated notes and leaps at speed; very high note density.',(0,40)),
 (r'Scarlatti.*K\. 213',None,['repeated notes','pedalling'],'Baroque repeated-note figures with the soft pedal down throughout.',(0,40)),
 (r'Beethoven.*Piano Sonatas 31-1','Kavalerova01',['pedalling','dynamics'],'Op. 110, I: continuous half pedaling and a moving una corda.',(342.5,25)),
 (r'Beethoven.*Andante favori',None,['pedalling','dynamics'],'WoO 57: cantabile with detailed pedal work.',(490,30)),
 (r'Debussy.*Voiles',None,['pedalling'],'Whole-tone washes: half pedal and una corda.',(0,45)),
 (r'Balakirev.*Islamey','Verbaite04',['polyphony','repeated notes','dynamics'],'The stress test: extreme density, sostenuto pedal, fff.',(0,40)),
 (r'Scriabin.*Sonatas 5','TET02',['dynamics','pedalling'],'Sostenuto pedal, wide dynamics.',(0,40)),
 (r'Schubert.*Impromptu op\.90 D\.899 3','Kociuban10M',['pedalling','dynamics'],'G-flat major Impromptu: singing line over pedaled accompaniment, soft pedal on.',(0,45)),
 (r'Chopin.*Etudes op 25 2','Karpeyev02',['polyphony','repeated notes'],'Op. 25 No. 2: rapid triplet figuration, soft pedal on.',(0,40)),
 (r'Rachmaninoff.*(Etudes?-Tableaux|Prelude).*',None,['polyphony','dynamics'],'Thick chords and big dynamics.',(0,45)),
 (r'Mozart.*Rondo in A Minor',None,['dynamics','pedalling'],'K. 511: classical clarity, subtle pedal.',(0,45)),
 (r'Tchaikovsky.*Lullaby',None,['pedalling'],'Half pedaling 76% of the time.',(0,45)),
 (r'Schubert.*Sonata in B-flat Major D\. 960',None,['dynamics','pedalling'],'D. 960, I: ppp to ff, long pedals.',(0,60)),
 (r'Chopin.*(Ballade|Scherzo).*',None,['dynamics','polyphony'],'Big romantic dynamics and textures.',(0,45)),
 (r'Ravel.*Gaspard de la Nuit 1 Ondine','KotysV11',['high register','pedalling'],'Ondine: shimmering figuration in the top two octaves, soft pedal on.',(0,45)),
 (r'Rachmaninoff.*Prelude.*(C-sharp Minor|Op\. 3)',None,['low register','dynamics'],'C-sharp minor Prelude: bass octaves and huge chords.',(0,45)),
 (r'Chopin.*(Etudes op 10 12|Op\. 10, No\. 12|Revolutionary)',None,['low register','polyphony'],'Revolutionary Etude: relentless left-hand runs into the bass.',(0,40)),
 (r'Beethoven.*(Piano Sonatas 23-1|Appassionata.*I\b)',None,['low register','dynamics'],'Appassionata, I: low tremolos and unison runs.',(0,50)),
 (r'Beethoven.*(Piano Sonatas 8-1|Path[eé]tique)',None,['low register','dynamics'],'Pathetique, I: low tremolo bass under the Allegro.',(60,40)),
 (r'Scriabin.*(Etudes op 8 12|Op\. 8.*No\. 12)',None,['low register','dynamics'],'Op. 8 No. 12: leaping left-hand octaves.',(0,40)),
 (r'Mussorgsky.*Pictures',None,['low register','dynamics'],'Pictures at an Exhibition: Bydlo and the Great Gate live in the bass.',(0,45)),
 (r'Debussy.*(Feux d|Fireworks)',None,['high register','dynamics'],'Feux d\'artifice: glittering top-register runs.',(0,45)),
 (r'Chopin.*Etudes op 10 5','',['high register','repeated notes'],'Black-key etude: fast right hand high up the keyboard.',(0,40)),
]
rows=[]
for path,(comp,title) in meta.items():
    label=f'{comp} {title}'
    for rx,frag,tags,why,exc in WANT:
        if re.search(rx,label,re.I) and (frag is None or frag in path):
            if not Path(path).exists(): continue
            rows.append(dict(path=path,composer=comp,title=title,tags=tags,why=why,excerpt=exc,_rx=rx)); break
# one performance per pattern: prefer the survey's richest pedal usage / most notes; compute stats for the chosen ones
chosen={}
for r in rows:
    s=survey.get(r['path'],{}); score=(s.get('cc64',{}).get('n',0)>0)*1+s.get('cc64',{}).get('share',0)
    if r['_rx'] not in chosen or score>chosen[r['_rx']][0]: chosen[r['_rx']]=(score,r)
lib=[]
for rx,(score,r) in chosen.items():
    st=stats(Path(r['path'])); s=survey.get(r['path'],{})
    tags=set(r['tags'])
    if st['max_polyphony']>=10 or st['peak_notes_2s']>=45: tags.add('polyphony')
    if st['repeated_rate']>=.06: tags.add('repeated notes')
    if st['vel_p95']-st['vel_p5']>=60: tags.add('dynamics')
    if st['high_share']>=.12 or st['notes_above_c7']>=150: tags.add('high register')
    if st['low_share']>=.09: tags.add('low register')
    if s.get('cc64',{}).get('share',0)>.4 or s.get('cc67',{}).get('n',0)>100 or s.get('cc66',{}).get('n',0)>0: tags.add('pedalling')
    if 'low register' in tags and 'low register' in r['tags']:
        ev=events(Path(r['path'])); low=np.array(sorted(t for t,st_,a,b in ev if st_==0x90 and b>0 and a<=40)); T=ev[-1][0]
        if len(low):
            best=max(((np.sum((low>=t0)&(low<t0+40)),t0) for t0 in np.arange(0,max(T-40,1),2.5)),key=lambda x:x[0])
            r['excerpt']=(round(float(best[1]),1),40)
    r.pop('_rx'); r['tags']=sorted(tags); r['stats']=st; r['pedal']=dict(cc64=s.get('cc64',{}).get('n',0),half_share=round(s.get('cc64',{}).get('share',0),2),cc67=s.get('cc67',{}).get('n',0),cc66=s.get('cc66',{}).get('n',0))
    r['corpus']='ASAP' if str(A) in r['path'] else 'MAESTRO'; r['license']='CC BY-NC-SA 4.0'
    lib.append(r)
lib.sort(key=lambda r:(r['composer'],r['title']))
out=ROOT/'app/pieces.json'; out.write_text(json.dumps(lib,indent=1))
for r in lib: print(f"{r['corpus']:7s} {r['composer'][:20]:20s} {r['title'][:40]:40s} {r['stats']['duration']/60:5.1f}min poly {r['stats']['max_polyphony']:2d} peak2s {r['stats']['peak_notes_2s']:3d} vel {r['stats']['vel_p5']:3d}-{r['stats']['vel_p95']:3d} rep {r['stats']['repeated_rate']:.2f} half {r['pedal']['half_share']:.2f} cc67 {r['pedal']['cc67']:4d} cc66 {r['pedal']['cc66']:2d}  {r['tags']}")
print(len(lib),'pieces ->',out)
