"""Public Lágrima edition, entered from Alier I.5230 A (public domain).

No GAPS score or recording is read. The web performance is deliberately score-led;
recording fits are separate local auditions. Rebuild with the project's Python venv.
"""
import argparse, gzip, json, math
from fractions import Fraction
from pathlib import Path
import xml.etree.ElementTree as ET
from recuerdos_web_export import assign_strings
from string_hand_geometry import finger_options
ROOT=Path(__file__).resolve().parents[1]
OPENS=[64,59,55,50,45,40]
ORDER=list(range(1,9))*2+list(range(9,17))*2+list(range(1,9))
SOURCE='https://imslp.org/wiki/Special:ReverseLookup/292928'

def notation(notes):
    def sub(p,tag,text=None,**attrs):
        e=ET.SubElement(p,tag,attrs)
        if text is not None:e.text=str(text)
        return e
    root=ET.Element('score-partwise',version='4.0')
    sub(sub(root,'work'),'work-title','Lágrima · Preludio')
    ident=sub(root,'identification');sub(ident,'creator','Francisco Tárrega',type='composer')
    sub(ident,'rights','Public-domain Alier edition I.5230 A. MusicXML and inferred tablature: pfsynth (CC0).')
    pl=sub(root,'part-list')
    for i,name in enumerate(['Guitar','Tab'],1):sub(sub(pl,'score-part',id=f'P{i}'),'part-name',name,**{'print-object':'no'})
    for pi in range(2):
      part=sub(root,'part',id=f'P{pi+1}')
      for bar in range(1,17):
        m=sub(part,'measure',number=str(bar));ns=[n for n in notes if n['measure']==bar]
        if bar in (1,9):
          a=sub(m,'attributes');sub(a,'divisions',8);sub(sub(a,'key'),'fifths',4 if bar==1 else 1)
          if bar==1:
            time=sub(a,'time');sub(time,'beats',3);sub(time,'beat-type',4)
            clef=sub(a,'clef');sub(clef,'sign','G' if pi==0 else 'TAB');sub(clef,'line',2 if pi==0 else 5)
            if pi==0:sub(sub(a,'transpose'),'chromatic',-12)
            else:
              sd=sub(a,'staff-details');sub(sd,'staff-lines',6)
              for line,(step,octave) in enumerate([('E',2),('A',2),('D',3),('G',3),('B',3),('E',4)],1):
                st=sub(sd,'staff-tuning',line=str(line));sub(st,'tuning-step',step);sub(st,'tuning-octave',octave)
        if not pi and bar in (1,6,8,9,15,16):
          words={1:'Andante',6:'rit.',8:'Fine',9:'a tempo',15:'rit.',16:'D.C. al Fine (senza replica)'}
          d=sub(m,'direction',placement='above');sub(sub(d,'direction-type'),'words',words[bar])
        if bar in (1,9):sub(sub(m,'barline',location='left'),'repeat',direction='forward')
        voices=['top','bass','middle']
        for vi,voice in enumerate(voices,1):
          if vi>1:sub(sub(m,'backup'),'duration',24)
          rows=[n for n in ns if n['voice']==voice];cursor=0
          for q in sorted(set(n['q'] for n in rows)):
            group=[n for n in rows if n['q']==q];local=q-(bar-1)*3
            if local>cursor:
              f=sub(m,'forward');sub(f,'duration',round((local-cursor)*8));sub(f,'voice',vi)
            for j,n in enumerate(group):
              e=sub(m,'note',id=f'{"t" if pi else "n"}{n["id"]}')
              if j:sub(e,'chord')
              pitch=n['pitch']+(0 if pi else 12)
              # Use sharp spelling for these E-major/E-minor source notes.
              step,alt=[('C',0),('C',1),('D',0),('D',1),('E',0),('F',0),('F',1),('G',0),('G',1),('A',0),('A',1),('B',0)][pitch%12]
              p=sub(e,'pitch');sub(p,'step',step)
              if alt:sub(p,'alter',alt)
              sub(p,'octave',pitch//12-1);sub(e,'duration',round(n['length']*8));sub(e,'voice',vi)
              sub(e,'type',{.5:'eighth',1:'quarter',2:'half',3:'half'}[n['length']])
              if n['length']==3:sub(e,'dot')
              sub(e,'stem','none' if pi else 'up' if voice=='top' else 'down')
              if not pi and j==0 and n['length']==Fraction(1,2):
                prior=any(x['q']==q-Fraction(1,2) and x['length']==Fraction(1,2) for x in rows)
                after=any(x['q']==q+Fraction(1,2) and x['length']==Fraction(1,2) for x in rows)
                if prior or after:sub(e,'beam','continue' if prior and after else 'end' if prior else 'begin',number='1')
              tech=sub(sub(e,'notations'),'technical')
              if pi:sub(tech,'string',n['string']);sub(tech,'fret',n['fret'])
              elif n.get('markedFinger') is not None:sub(tech,'fingering',n['markedFinger'])
            cursor=local+group[0]['length']
          if cursor<3:
            # The final quarter-rest at each section ending is explicit.
            if voice=='top' and bar in (8,16):
              rest=sub(m,'note',**({'print-object':'no'} if pi else {}));sub(rest,'rest');sub(rest,'duration',8);sub(rest,'voice',vi);sub(rest,'type','quarter')
            else:
              f=sub(m,'forward');sub(f,'duration',round((3-cursor)*8));sub(f,'voice',vi)
        if bar in (8,16):sub(sub(m,'barline',location='right'),'repeat',direction='backward')
    ET.indent(root)
    return ET.tostring(root,encoding='utf-8',xml_declaration=True)

def build(out):
    notes=json.loads((ROOT/'tools/scores/tarrega-lagrima.json').read_text())
    for n in notes:n['q']=Fraction(n['q']);n['length']=Fraction(n['length'])
    # Original m.6 requires ~53 mm diagonally between index and middle.
    assign_strings(notes,reach_limits={(1,2):55},shift_speed=900,quarter_seconds=.75)
    anchors=[dict(beat=0,seconds=.12)];t=.12
    for played,bar in enumerate(ORDER):
      for q in range(3):
        factor=1+.035*math.sin((bar-1)*math.pi/4)
        if bar in (7,15):factor*=1+.10*q
        if bar in (8,16):factor*=1.15+.08*q
        t+=60/76*factor;anchors.append(dict(beat=played*3+q+1,seconds=round(t,8)))
    def sec(b):
      i=min(int(b),len(anchors)-2);a,z=anchors[i:i+2]
      return a['seconds']+(z['seconds']-a['seconds'])*(float(b)-i)
    events=[]
    for played,bar in enumerate(ORDER):
      for n in notes:
        if n['measure']!=bar:continue
        beat=played*3+float(n['q'])-(bar-1)*3;gate=played*3+float(n['gateQ'])-(bar-1)*3
        base={'top':92,'bass':78,'middle':69}[n['voice']]
        phrase=1+.035*math.sin((bar-1)*math.pi/4)
        events.append(dict(beat=beat,durationBeats=float(n['length']),gateBeat=gate,start=sec(beat),end=sec(gate),pitch=n['pitch'],velocity=round(base*phrase,3),
          string=n['string'],fret=n['fret'],finger=n['auditFinger'],articulation=0,measure=bar,performedMeasure=played+1,
          pluck='p' if n['string']>=4 else {3:'i',2:'m',1:'a'}[n['string']],ids=[f'n{n["id"]}',f't{n["id"]}']))
    events.sort(key=lambda n:(n['start'],n['pitch']))
    span=0;max_shift=0;old_pos=None;old_time=None
    for t0 in sorted(set(n['start'] for n in events)):
      active=[(i,n['string']-1,n['fret']) for i,n in enumerate(events) if n['start']<=t0+1e-7 and n['end']>t0+1e-7]
      assert next(finger_options(active,fixed={i:events[i]['finger'] for i,_,_ in active},reach_limits={(1,2):55}),None) is not None
      stopped=[f for _,_,f in active if f]
      positions=[f-events[i]['finger']+1 for i,_,f in active if f]
      if stopped:span=max(span,650*(2**(-min(stopped)/12)-2**(-max(stopped)/12)))
      if positions:
        pos=sum(positions)/len(positions)
        if old_pos is not None:
          shift=650*abs(2**(-pos/12)-2**(-old_pos/12));max_shift=max(max_shift,shift)
          assert shift<=900*(t0-old_time)+15
        old_pos,old_time=pos,t0
    piece=dict(format='pfsynth score 1',instrument='guitar',title='Lágrima',composer='Francisco Tárrega',dates='1852–1909',tuning=OPENS,
      duration=anchors[-1]['seconds']+3,body='g05',defaultRoom='dry',instrumentSettings={'Let strings ring':0},notes=events,
      timing=dict(mode='score',rubato=anchors,tailSeconds=3),
      handAudit=dict(passed=True,performedNotes=len(events),maxLongitudinalSpanMm=round(span,2),maxPositionShiftMm=round(max_shift,2),indexMiddleReachMm=55),
      performanceNote='Live physical-model guitar. Complete A–A–B–B–A form, with gentle score-led phrasing and cadential slowing. Recording-fitted timing and dynamics are being evaluated separately; this version is not a recording fit. The original score and inferred tablature follow playback.',
      license='Alier I.5230 A: public domain. New MusicXML and tablature: CC0',
      scoreSource=dict(url=SOURCE,license='Public domain / CC0',changes='Independent note entry, MusicXML, inferred tab, performance order, phrase timing and explicit hand-release gates.'))
    assert len(notes)==132 and len(events)==325
    out.mkdir(parents=True,exist_ok=True)
    (out/'score.json').write_text(json.dumps(piece,ensure_ascii=False,separators=(',',':'))+'\n')
    xml=notation(notes);(out/'score.musicxml').write_bytes(xml);(out/'score.musicxml.gz').write_bytes(gzip.compress(xml,mtime=0))
    (out/'SOURCE.txt').write_text('''Lágrima — Francisco Tárrega (1852–1909)

Independent note entry from the public-domain original solo edition:
Madrid: Ildefonso Alier, plate I.5230 A, page 2; Gaylord Music Library scan.
https://imslp.org/wiki/Special:ReverseLookup/292928

New MusicXML and inferred tablature by pfsynth, dedicated under CC0:
https://creativecommons.org/publicdomain/zero/1.0/
Selected printed fingers and string indications constrain the inferred tab.
No GAPS score, MIDI, alignment file or performance recording is distributed.
16 written bars; A A B B A, 40 performed bars and 325 note attacks.
The repeated F-sharp in bar 7 is a tied continuation, not a new pluck.
The grace slide into bar 9's C is represented by its destination note.
Timing and levels here are score-led interpretation, not measured player controls.

Tab audit: four fingers, held-contact conflicts, index-only barres, 650 mm scale,
8 mm string spacing, default pairwise reaches in tools/string_hand_geometry.py
except index/middle 55 mm (original bar 6 needs about 53 mm diagonally).
Position changes capped at 900 mm/s plus 15 mm tolerance; complete repeat order
checked. Necessary releases are explicit gateBeat values; notation retains full
written duration. These are designed adult-hand limits, not a guarantee for every
player. Unmarked contacts use inferred fingers; no default-all-index assignment.

Measured body: Robert Mores, CC BY 4.0, https://doi.org/10.5281/zenodo.4604577
Playback uses the existing physical guitar engine; no recorded audio is streamed.
''')
    return piece

if __name__=='__main__':
    ap=argparse.ArgumentParser();ap.add_argument('--output',type=Path,default=ROOT/'docs/guitar/pieces/tarrega-lagrima');args=ap.parse_args()
    p=build(args.output);print(f'Lágrima: {len(p["notes"])} notes, {p["duration"]:.2f} seconds; hand audit passed.')
