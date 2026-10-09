"""Public Recuerdos edition, independently converted from Mutopia #810 (CC BY-SA 3.0).

Requires python-ly. The source is supplied explicitly; no GAPS score is read here.
Research performance curves are optional inputs, separate from written notes.
"""
import argparse, copy, gzip, hashlib, itertools, json, math, re, subprocess
from fractions import Fraction
from pathlib import Path
import xml.etree.ElementTree as ET
import ly.document, ly.music, ly.pitch.rel2abs
from string_hand_geometry import finger_options

ROOT = Path(__file__).resolve().parents[1]
OPENS = [64,59,55,50,45,40]
STEPS = [0,2,4,5,7,9,11]

def read_source(path):
    doc = ly.document.Document(path.read_text())
    ly.pitch.rel2abs.rel2abs(ly.document.Cursor(doc))
    tree = ly.music.document(doc)
    voices = {}
    def walk(node, scale=Fraction(1)):
        kind = type(node).__name__
        if kind in ('Note', 'Chord', 'Rest', 'Skip'):
            dur = Fraction(node.duration[0]) * node.duration[1] * scale * 4
            if kind in ('Rest','Skip'): yield [], dur; return
            children = list(node) if kind == 'Chord' else [node] + list(node)
            ns=[]
            for child in children:
                if type(child).__name__=='Note':
                    p=child.pitch
                    ns.append(dict(pitch=48 + p.octave*12 + STEPS[p.note] + int(p.alter*2), step='CDEFGAB'[p.note], alter=int(p.alter*2), octave=3+p.octave))
                elif ns:
                    for a in [child]+list(child.descendants()):
                        token=a.token
                        if type(token).__name__=='Fingering': ns[-1]['finger']=int(token)
                        if type(token).__name__=='StringNumber': ns[-1]['string']=int(str(token)[1:])
            yield ns,dur
        elif kind in ('MusicList','Repeat','Alternative','Assignment','Scaler'):
            if kind=='Scaler': scale*=node.scaling
            for child in node: yield from walk(child,scale)
    for a in tree:
        if type(a).__name__!='Assignment' or a.name() not in ('top','pedal','bottom'): continue
        rows=[];beat=Fraction(0)
        for chord,dur in walk(a):
            for n in chord: rows.append(dict(n,q=beat,length=dur,voice=a.name()))
            beat+=dur
        assert beat==(165 if a.name()=='bottom' else 174),(a.name(),beat)
        voices[a.name()]=rows
    merged={}
    for name in ('top','pedal','bottom'):
        for n in voices[name]:
            k=(n['q'],n['pitch']); prev=merged.get(k)
            if prev:
                prev.setdefault('topLength',prev['length']); prev['length']=max(prev['length'],n['length'])
                for field in ('finger','string'):
                    if field in n: prev[field]=n[field]
            else: merged[k]=dict(n)
    notes=sorted(merged.values(),key=lambda n:(n['q'],-n['pitch']))
    for i,n in enumerate(notes): n['id']=i; n['markedFinger']=n.get('finger'); n['markedString']=n.get('string')
    return notes

def assign_strings(notes, *, reach_limits=None, shift_speed=1800, quarter_seconds=60/74):
    """Beam search with held-note geometry and the edition's explicit finger/string hints.
    Written durations stay intact; separate gates release held notes when the hand
    must move. Inferred fingers are only
    used by the audit, not printed as though they came from the edition.
    """
    reach_limits = {(2,3):45} if reach_limits is None else reach_limits
    groups=[]
    for i,n in enumerate(notes):
        if not groups or notes[groups[-1][0]]['q'] != n['q']: groups.append([])
        groups[-1].append(i)
    states=[(0.,{},0.,None)]
    for group in groups:
        q=notes[group[0]]['q']; candidates=[]
        options=[]
        for i in group:
            n=notes[i]; choices=[]
            for s,o in enumerate(OPENS,1):
                f=n['pitch']-o
                if not 0<=f<=19:continue
                if n.get('string') and n['string']!=s:continue
                if n.get('finger')==0 and f!=0:continue
                if n.get('finger',-1)>0 and f==0:continue
                choices.append((s,f))
            options.append(choices)
        for cost,path,anchor,last in states:
            all_held=[i for i in path if path[i][3]>q]
            # A long notated bass may need releasing to move the hand. Prefer
            # keeping it; record any necessary release as a separate gate.
            variants=[tuple(i for k,i in enumerate(all_held) if not (mask>>k)&1) for mask in range(1<<len(all_held))]
            for held in variants:
              released=[i for i in all_held if i not in held]
              if any(q-notes[i]['q']<Fraction(1,8) for i in released):continue
              occupied={path[i][0] for i in held}
              for chosen in itertools.product(*options):
                  ss=[s for s,f in chosen]
                  if len(set(ss))!=len(ss) or occupied.intersection(ss):continue
                  contacts=[(i,path[i][0]-1,path[i][1]) for i in held]+[(i,s-1,f) for i,(s,f) in zip(group,chosen)]
                  fixed={i:path[i][2] for i in held}
                  fixed.update({i:notes[i]['finger'] for i in group if notes[i].get('finger',0)>0})
                  # This edition asks for a middle/ring diagonal reach of ~44 mm.
                  # Explicit local audit assumption; the general tool stays at 35.
                  for fingers in finger_options(contacts,fixed=fixed,reach_limits=reach_limits):
                      stopped=[(i,s,f) for i,s,f in contacts if f]
                      positions=[f-fingers[i]+1 for i,s,f in stopped]
                      pos=sum(positions)/len(positions) if positions else anchor
                      if positions and max(abs(v-pos) for v in positions)>1.5:continue
                      shift=650*abs(2**(-pos/12)-2**(-anchor/12))
                      if last is not None and shift>shift_speed*float(q-last)*quarter_seconds+15:continue
                      new=dict(path)
                      for j in released:new[j]=(*new[j][:3],q)
                      for i,(s,f) in zip(group,chosen):new[i]=(s,f,fingers[i],notes[i]['q']+notes[i]['length'])
                      # Penalize unnecessary shifts and string changes during the tremolo.
                      penalty=sum(8+4*float(path[j][3]-q) for j in released)+.06*shift+.03*sum(f for s,f in chosen)+sum(abs(v-pos) for v in positions)
                      for i,(s,f) in zip(group,chosen):
                          prior=next((j for j in reversed(path) if notes[j]['voice']==notes[i]['voice'] and notes[j]['pitch']==notes[i]['pitch']),None)
                          if prior is not None and notes[i]['q']-notes[prior]['q']<=Fraction(1,2):penalty+=3*(path[prior][0]!=s)+.5*(path[prior][2]!=fingers[i])
                      candidates.append((cost+penalty,new,pos,q if stopped else last))
        if not candidates:
            print('held', [(cost,anchor,float(last),[(j,notes[j],v) for j,v in path.items() if v[3]>q]) for cost,path,anchor,last in states[:2]],flush=True)
            raise ValueError(f'No playable source fingering at beat {float(q):g}, measure {int(q//3)+1}: {[notes[i] for i in group]}')
        diverse={}
        for s in sorted(candidates,key=lambda s:s[0]):
            key=tuple((i,s[1][i]) for i in s[1] if s[1][i][3]>q)
            if key not in diverse:diverse[key]=s
            if len(diverse)>=24:break
        states=list(diverse.values())
    path=min(states,key=lambda s:s[0])[1]
    for i,n in enumerate(notes):n['string'],n['fret'],n['auditFinger'],n['gateQ']=path[i]
    return notes

def musicxml(notes):
    root=ET.Element('score-partwise',version='4.0')
    def sub(parent,tag,text=None,**attrs):
        e=ET.SubElement(parent,tag,attrs)
        if text is not None:e.text=str(text)
        return e
    sub(sub(root,'work'),'work-title','Recuerdos de la Alhambra')
    identification=sub(root,'identification')
    sub(identification,'creator','Francisco Tárrega',type='composer')
    sub(identification,'rights','Stewart Holmes / Mutopia #810, CC BY-SA 3.0. MusicXML and inferred tab adapted by pfsynth.')
    pl=sub(root,'part-list')
    for id,name in [('P1','Guitar'),('P2','Tab')]:sub(sub(pl,'score-part',id=id),'part-name',name,**{'print-object':'no'})
    for pi in range(2):
      part=sub(root,'part',id=f'P{pi+1}')
      for bar in range(1,59):
        m=sub(part,'measure',number=str(bar));ns=[n for n in notes if int(float(n['q'])//3)+1==bar]
        if bar in (1,21):
            attr=sub(m,'attributes');sub(attr,'divisions',48);sub(sub(attr,'key'),'fifths',0 if bar==1 else 3)
            if bar==1:
                tm=sub(attr,'time');sub(tm,'beats',3);sub(tm,'beat-type',4)
                clef=sub(attr,'clef');sub(clef,'sign','G' if pi==0 else 'TAB');sub(clef,'line',2 if pi==0 else 5)
                if pi==0:
                    sub(sub(attr,'transpose'),'chromatic',-12)
                else:
                    sd=sub(attr,'staff-details');sub(sd,'staff-lines',6)
                    for l,(step,octave) in enumerate([('E',2),('A',2),('D',3),('G',3),('B',3),('E',4)],1):
                        st=sub(sd,'staff-tuning',line=str(l));sub(st,'tuning-step',step);sub(st,'tuning-octave',octave)
        if pi==0 and bar in (1,35,37,38):
            dt=sub(sub(m,'direction',placement='above'),'direction-type');sub(dt,'words',{1:'Andante · p–a–m–i',35:'To Coda (on return)',37:'D.C. al Coda',38:'Coda'}[bar])
        if bar==21:sub(sub(m,'barline',location='left'),'repeat',direction='forward')
        if bar in (36,37):sub(sub(m,'barline',location='left'),'ending',number='1' if bar==36 else '2',type='start')
        voices=[('top',[n for n in ns if n['voice']=='top'])]
        if pi==0:
            # Put overlapping pedal and accompaniment durations into distinct
            # voices. MusicXML backup inside one voice is not valid polyphony.
            layers=[]
            for n in [n for n in ns if n['voice']!='top' or n['length']>n.get('topLength',n['length'])]:
                layer=next((v for v in layers if v[-1]['q']+v[-1]['length']<=n['q']),None)
                if layer is None:layer=[];layers.append(layer)
                layer.append(n)
            voices.extend(('bass',v) for v in layers)
        else:voices=[('tab',ns)]
        for vi,(voice,rows) in enumerate(voices,1):
            if vi>1:sub(sub(m,'backup'),'duration',144)
            cursor=Fraction(0);last_start=None
            for n in rows:
                q=Fraction(n['q']).limit_denominator(96)-3*(bar-1)
                length=Fraction(n.get('topLength',n['length']) if voice=='top' else n['length']).limit_denominator(96)
                if pi: # Tab rhythm is carried by the notation; leave space until next attack.
                    length=min(length,Fraction(1,8)) if bar<56 else length
                chord=q==last_start
                if not chord and q>cursor:
                    fwd=sub(m,'forward');sub(fwd,'duration',round((q-cursor)*48));sub(fwd,'voice',vi)
                if not chord and q<cursor:
                    sub(sub(m,'backup'),'duration',round((cursor-q)*48))
                prefix='t' if pi else 'n' if voice=='top' or n['voice']!='top' else 'd'
                e=sub(m,'note',id=f'{prefix}{n["id"]}')
                if chord:sub(e,'chord')
                pitch=sub(e,'pitch');sub(pitch,'step',n['step'])
                if n['alter']:sub(pitch,'alter',n['alter'])
                sub(pitch,'octave',n['octave']+(0 if pi else 1))
                sub(e,'duration',round(length*48));sub(e,'voice',vi)
                typ={Fraction(3):'half',Fraction(2):'half',Fraction(1):'quarter',Fraction(1,2):'eighth',Fraction(1,4):'16th',Fraction(1,8):'32nd',Fraction(1,12):'32nd'}.get(length,'32nd')
                sub(e,'type',typ)
                if length==3:sub(e,'dot')
                if length==Fraction(1,12):
                    tm=sub(e,'time-modification');sub(tm,'actual-notes',3);sub(tm,'normal-notes',2);sub(tm,'normal-type','32nd')
                if not pi:sub(e,'stem','up' if voice=='top' else 'down')
                tech=sub(sub(e,'notations'),'technical')
                if pi:sub(tech,'string',n['string']);sub(tech,'fret',n['fret'])
                elif n.get('markedFinger') is not None and (voice!='top' or n['length']==n.get('topLength',n['length'])):sub(tech,'fingering',n['markedFinger'])
                if not pi and voice=='top' and length<=Fraction(1,8):
                    group=[v for v in rows if int(float(v['q'])*2)==int(float(n['q'])*2)]
                    for beam in range(1,4):sub(e,'beam','begin' if n is group[0] else 'end' if n is group[-1] else 'continue',number=str(beam))
                notations=e.find('notations');e.remove(notations);e.append(notations)
                cursor=q+length;last_start=q
            if cursor<3:
                fwd=sub(m,'forward');sub(fwd,'duration',round((3-cursor)*48));sub(fwd,'voice',vi)
        if bar in (20,36):
            bl=sub(m,'barline',location='right')
            if bar==36:sub(bl,'ending',number='1',type='stop')
            sub(bl,'repeat',direction='backward')
        if bar==37:sub(sub(m,'barline',location='right'),'ending',number='2',type='stop')
        if bar==58:sub(sub(m,'barline',location='right'),'bar-style','light-heavy')
    ET.indent(root)
    return ET.tostring(root,encoding='utf-8',xml_declaration=True)

def ringing_gates(events,end_beat):
    """Extend ringing until a re-pluck or a physically necessary hand release.
    Re-check the complete performed order, including repeat/D.C. boundaries.
    """
    active=[];released=0;span=0.
    groups=[]
    for n in sorted(events,key=lambda n:n['beat']):
        if not groups or abs(n['beat']-groups[-1][0]['beat'])>1e-7:groups.append([])
        groups[-1].append(n)
        n['gateBeat']=end_beat
    for arriving in groups:
        q=arriving[0]['beat'];strings={n['string'] for n in arriving}
        prior=active;active=[n for n in active if n['string'] not in strings]
        for n in prior:
            if n['string'] in strings:n['gateBeat']=q
        choices=[]
        for mask in range(1<<len(active)):
            keep=[n for k,n in enumerate(active) if not (mask>>k)&1]
            drop=[n for k,n in enumerate(active) if (mask>>k)&1]
            posed=keep+arriving
            contacts=[(i,n['string']-1,n['fret']) for i,n in enumerate(posed)]
            fixed={i:n['inferredFinger'] for i,n in enumerate(posed) if n['fret']}
            if next(finger_options(contacts,fixed=fixed,reach_limits={(2,3):45}),None) is None:continue
            cost=sum(1+max(0,n['beat']+n['durationBeats']-q)*4 for n in drop)
            choices.append((cost,keep,drop))
        assert choices,('Unplayable performed hand pose',q)
        _,keep,drop=min(choices,key=lambda v:v[0]);released+=len(drop)
        for n in drop:n['gateBeat']=q
        active=keep+arriving
        frets=[n['fret'] for n in active if n['fret']]
        if frets:span=max(span,650*(2**(-min(frets)/12)-2**(-max(frets)/12)))
    assert all(n['gateBeat']>n['beat'] for n in events)
    return dict(poses=len(groups),releaseCount=released,maxLongitudinalSpanMm=round(span,2),scope='String conflicts, fixed finger contacts, pairwise reach and barre obstruction; full performed order. Designed hand, not player-specific validation.')

def publish_export(notes,out):
    # These are pfsynth's transformed performance controls, not the source
    # recording, dataset XML, MIDI, or raw dataset synchronization annotations.
    import numpy as np
    from scipy.io import loadmat
    from guitar_material_audition import measured_body, loading
    research=ROOT/'build/recuerdos-full-2026-10-07'
    old=json.loads((research/'full-events.json').read_text())
    info=json.loads((research/'performance.json').read_text())
    curve=json.loads((research/'phrase-curve.json').read_text())
    ratios=[.2470933865063632,.23442936708839458,.2149601658396182,.30351708056562416]
    anchors=[]
    for j,m in enumerate(info['measures']):
        if j<125:
            for cyc in range(6):
                t=next(n['start'] for n in old if n['performed_measure']==j+1 and abs(n['beat']-cyc*.5)<1e-7)
                anchors.append(dict(beat=j*3+cyc*.5,seconds=t))
        else:anchors.append(dict(beat=j*3,seconds=m['start']))
    anchors.append(dict(beat=384,seconds=info['musical_end']))
    points=[(v['beat'],v['seconds']) for v in anchors]
    def seconds(q):return float(np.interp(q,*zip(*points)))
    body=measured_body(loadmat(ROOT/'research/strings/body/mores-qualified-selected-impulses.mat')['qualified_selected_impulses'],5)
    loss=loading(body,1);banks=[];bank_ids={};events=[]
    phases=np.r_[0,np.cumsum(ratios)[:3]]
    for j,bar in enumerate(info['repeat_order']):
        ns=[n for n in notes if int(float(n['q'])//3)+1==bar]
        for n in ns:
            local=float(n['q'])-(bar-1)*3;beat=j*3+local
            rawlen=float(n.get('topLength',n['length']))
            triplet=abs(rawlen-1/12)<1e-7
            ornament_cycle=any(abs(float(v.get('topLength',v['length']))-1/12)<1e-7 and int((float(v['q'])-(bar-1)*3)*2)==int(local*2) for v in ns)
            subdivide=bar<=55 and not ornament_cycle
            start=seconds(beat)
            if subdivide:
                cy=math.floor(beat*2+1e-7)/2;slot=round((beat-cy)*8)
                start=seconds(cy)+(seconds(cy+.5)-seconds(cy))*phases[slot]
            pluck='pami'[round(local*8)%4] if bar<=55 else ('p' if n['string']>=4 else {3:'i',2:'m',1:'a'}[n['string']])
            art=0
            if triplet:
                same=[v for v in ns if abs(float(v.get('topLength',v['length']))-1/12)<1e-7]
                k=same.index(n)
                if k%3:art=1 if n['pitch']>same[k-1]['pitch'] else 2;pluck=None
            offset=0
            if bar==57:offset=.035*sorted(ns,key=lambda v:-v['string']).index(n);start+=offset
            key=(n['string'],n['pitch'])
            if key not in bank_ids:bank_ids[key]=len(banks);banks.append([round(float(v),7) for v in loss(n)])
            gate=seconds(j*3+float(n['gateQ'])-(bar-1)*3)
            ids=[f'n{n["id"]}',f't{n["id"]}']
            if n['voice']=='top' and n['length']>n.get('topLength',n['length']):ids.append(f'd{n["id"]}')
            velocity=float(85*10**(np.interp(start,curve['time'],curve['gain_db'])/25))
            e=dict(beat=round(beat,9),durationBeats=float(n['length']),gateBeat=round(j*3+float(n['gateQ'])-(bar-1)*3,9),subdivide=subdivide,
                start=start,end=gate,pitch=n['pitch'],velocity=round(velocity,5),string=n['string'],fret=n['fret'],finger=n['markedFinger'] if n['markedFinger'] is not None else -1,
                articulation=art,ids=ids,measure=bar,performedMeasure=j+1,loadingProfile=bank_ids[key],inferredFinger=n['auditFinger'])
            if pluck:e['pluck']=pluck
            if offset:e['startOffset']=offset
            events.append(e)
    hand_audit=ringing_gates(events,384)
    out.mkdir(parents=True,exist_ok=True)
    # Actual gate mapping is compiled by performance.js; cached seconds are for
    # C/third-party consumers and are re-generated in the validation step.
    piece=dict(format='pfsynth score 1',title='Recuerdos de la Alhambra',composer='Francisco Tárrega',dates='1852–1909',tuning=OPENS,
        duration=info['duration'],body='g05',defaultRoom='dry',instrumentSettings={'Let strings ring':0},
        notes=events,timing=dict(mode='score',rubato=anchors,subdivision=dict(cycleBeats=.5,ratios=ratios),minPluckGapSeconds=.0758276643990925,tailSeconds=3),
        plucking={f:dict(position=.19,toneTiltDbPerOctave=t) for f,t in zip('pami',[0,-.3397562,.3820291,-.1515470])},loadingProfiles=banks,handAudit=hand_audit,
        performanceNote='Live physical-model synthesis. Steady tremolo ratios, broad phrase rubato and smooth swells from the accepted Recuerdos studies. Per-finger tone is a streaming modal approximation of the auditioned spectral slopes. This is the Mutopia edition, with inferred tab and hand releases; no performance recording is streamed.',
        license='Stewart Holmes / Mutopia #810, CC BY-SA 3.0. MusicXML and inferred tab adapted by pfsynth; see SOURCE.txt.',
        scoreSource={'url':'https://www.mutopiaproject.org/cgibin/piece-info.cgi?id=810','license':'CC BY-SA 3.0','changes':'MusicXML conversion, inferred string/fret tab, performance order, independent timing/dynamics and damping gates. Written fingerings retained where marked.'})
    (out/'score.json').write_text(json.dumps(piece,separators=(',',':'))+'\n')
    # Cache seconds from the same compiler as browser playback for legacy readers.
    subprocess.run(['node', '--input-type=module', '-e',
        "import fs from 'node:fs'; import {pathToFileURL} from 'node:url'; "
        "const {preparePerformance}=await import(pathToFileURL(process.argv[1])); "
        "const p=JSON.parse(fs.readFileSync(process.argv[2]));const c=preparePerformance(p); "
        "p.duration=c.duration;p.notes=c.notes.map(({pluck_position,pluck_tilt,...n})=>n); "
        "fs.writeFileSync(process.argv[2],JSON.stringify(p)+'\\n');",
        str(ROOT/'docs/guitar/performance.js'),str(out/'score.json')],check=True)
    xml=musicxml(notes);(out/'score.musicxml').write_bytes(xml);(out/'score.musicxml.gz').write_bytes(gzip.compress(xml,mtime=0))
    (out/'SOURCE.txt').write_text('Recuerdos de la Alhambra — Francisco Tárrega (1852–1909)\n\nScore: Stewart Holmes, Mutopia #810 (2009), based on Orfeo Tracio.\nhttps://www.mutopiaproject.org/cgibin/piece-info.cgi?id=810\nhttps://creativecommons.org/licenses/by-sa/3.0/\n\nAdaptation by pfsynth: MusicXML conversion and inferred tablature; source left-hand markings retained. Notated lengths remain on the staff; explicit gateBeat values model necessary hand releases. The adapted score and tab are distributed under CC BY-SA 3.0. No source recording, GAPS score, MIDI or raw syncpoints is included.\n\nPerformance controls: pfsynth Recuerdos research (owner-selected rounds 2, 4, 5), transformed phrase timing/level curves and aggregate p–a–m–i gap ratios. These are modeling controls, not measured finger anatomy or universal human timing limits. The streaming tone port weights modal radiation rather than applying the offline minimum-phase FIR; it is not a bit-exact copy of that audition.\n\nBody loading/radiation: Robert Mores, CC BY 4.0, https://doi.org/10.5281/zenodo.4604577 . Passive diagonal string-loading approximation, limited to 0–10/s, derived from guitar 5.\n\nTab audit: source finger/string marks constrain a beam search. 650 mm scale, 8 mm lateral string spacing, default pairwise reach bounds from tools/string_hand_geometry.py except middle/ring 45 mm (the edition needs ~44 mm); position-change cap 1800 mm/s plus 15 mm tolerance. Held notes may be released when a finger/position changes; all releases are explicit in gateBeat. This is a designed-hand audit, not a guarantee for every guitarist.\n')
    return piece

if __name__=='__main__':
    ap=argparse.ArgumentParser();ap.add_argument('source',type=Path);ap.add_argument('--output',type=Path,required=True);ap.add_argument('--publish-dir',type=Path);ap.add_argument('--cached',action='store_true');args=ap.parse_args()
    if args.cached:
        notes=json.loads(args.output.read_text());publish_export(notes,args.publish_dir);raise SystemExit
    notes=read_source(args.source)
    print('Source:',len(notes),'notes, 58 measures',flush=True)
    assign_strings(notes)
    args.output.parent.mkdir(parents=True,exist_ok=True)
    args.output.write_text(json.dumps(notes,default=float,indent=2)+'\n')
    print('Fingering geometry passed',flush=True)
    if args.publish_dir:publish_export(notes,args.publish_dir)
