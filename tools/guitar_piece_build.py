"""Build one GAPS piece end to end: fitted performance, listening-room clip, web export.

The same steps as the Bach Prelude and the Sonatina, for any GAPS piece fetched with
gaps_fetch.py whose performance follows its score literally:
  1. GAPS's fine-aligned MIDI matched to the score's strings, frets and fingers
     (apply_edition: repeats as written);
  2. chords under an arpeggio mark re-timed from the GAPS transcription model's onsets
     (run here if missing; build/amt-venv);
  3. score slurs between consecutive notes on one string played as hammer-ons /
     pull-offs (default strengths from guitar_legato_audition); text "Harm." marks as
     natural harmonics (the fitted finger);
  4. strings ring until a hand would stop them (guitar_sustain.let_ring);
  5. the recording's room fitted on its first 24 s (guitar_room_fit.fit_room);
  6. velocities fitted window by window with that room, clipped at a 3 mm pluck;
  7. A (constant velocity) / B (fitted) renders, the closest measured body, a Verovio
     engraving, and the files the web demo needs.
Outputs: build/stringlab/<clip>-events.json, build/stringlab/pieces/<slug>.clip.json
(merged into the listening room by guitar_pieces_merge.py), experiments/string-gestures/
pieces/<slug>.json (report) and docs/guitar/pieces/<slug>/ (web: score, performance,
credits; never the recording).

    build/body-venv/bin/python tools/guitar_piece_build.py <slug> [...]
"""
import copy,gzip,json,re,subprocess,sys
import xml.etree.ElementTree as ET
import numpy as np
import mido
from scipy.io import wavfile
from scipy.signal import fftconvolve
from stringlab_audition import ROOT,OUT,SR,write_clip
from string_gesture_audition import setup,export_midi,render_guitar
from classical_string_audition import read_notes,score
from guitar_edition_fingering import apply_edition,edition_tuning
from paired_string_audition import audio
from guitar_dynamics_fit import Model,fit_room_dynamics_long,physical_cap,loudness_envelope
from guitar_reference import reference_origin,fit_guard
from guitar_room_fit import fit_room,power,room_impulse
from guitar_sustain import let_ring
from guitar_sonatina_audition import strum_from_transcription,transcribed
from guitar_harmonics import touch_at
from score_engrave import engrave
from stringlab_audition import library

GAPS=ROOT/'research/strings/midi/gaps';DOC=ROOT/'experiments/string-gestures';WEB=ROOT/'docs/guitar/pieces'
LEG=json.loads((DOC/'guitar-legato-report.json').read_text())


def fretted_harmonic(words):
    m=re.search(r'\b(?:harm|arm)\.?\s*([XVI]+|\d+)?',words or '',re.I)
    if not m:return None
    v=m.group(1);roman=dict(I=1,V=5,X=10)
    if v and v.isdigit():return int(v)
    if v:
        total=0;prev=0
        for ch in reversed(v.upper()):x=roman[ch];total+=x if x>=prev else -x;prev=max(prev,x)
        return total
    return 12


def body_rank(force,ref,room,slug):
    """Closest measured body: long-term 1/6-octave spectrum of force * body * room vs the recording."""
    from guitar_body_choice import rank_bodies
    irs={}
    for p in sorted((OUT/'bodies').glob('g*.wav')):irs[p.stem]=wavfile.read(p)[1].astype(float)
    (OUT/'pieces').mkdir(parents=True,exist_ok=True);tmp=OUT/'pieces'/f'{slug}.room.json';tmp.write_text(json.dumps(dict(best=room)))
    return rank_bodies(force,ref,irs,tmp)


def build(slug,lib):
    d=GAPS/slug;meta=json.loads((d/'meta.json').read_text());h=meta['scorehash'];cid=f'guitar-piece-{slug}'
    t=0;origin=None
    for msg in mido.MidiFile(d/f'{h}-fine-aligned.mid'):
        t+=msg.time
        if msg.type=='note_on' and msg.velocity:origin=t;break
    audio_origin,alignment=reference_origin(d/f'{h}.wav',origin,meta['gaps_id'])
    sr,raw=wavfile.read(d/f'{h}.wav',mmap=True);length=raw.shape[0]/sr
    notes,_,_=read_notes(d/f'{h}-fine-aligned.mid',1e9)
    duration=round(min(length-audio_origin,max(n['end'] for n in notes)+3),2)
    apply_edition(notes,d/f'{h}.xml','GAPS score');tuning=edition_tuning(d/f'{h}.xml')
    for n in notes:n.update(bend=[],slides=[],hammer_to=False,mute=False)
    log=dict(notes=len(notes),duration=duration,tuning=tuning,alignment=alignment)
    # 2. rolled chords from the transcription
    if any(n.get('score_arpeggiate') for n in notes):
        amt=d/f'{h}-amt.mid'
        if not amt.exists():subprocess.run([str(ROOT/'build/amt-venv/bin/midi_transcription'),str(d/f'{h}.wav'),str(amt),'--instrument','guitar'],check=True,capture_output=True)
        log['arpeggios']=strum_from_transcription(notes,transcribed(amt),audio_origin)
    # 3. slurs and harmonics
    slurs=0
    for i,n in enumerate(notes):
        if not n.get('score_slur_start'):continue
        nxt=next((m for m in notes[i+1:] if m['string']==n['string']),None)
        if nxt is None or not set(n['score_slur_start'])&set(nxt.get('score_slur_stop',[])):continue
        if not (0<nxt['start']-n['start']<1.0) or nxt['pitch']==n['pitch']:continue
        up=nxt['pitch']>n['pitch']
        if up and nxt['fret']==0:continue                     # an open string cannot be hammered
        nxt.update(transition='legato',legato_amount=LEG['clearance_m'] if up else LEG['pull_m'],legato_contact=LEG['contact_s'],articulation=('hammer-on' if up else 'pull-off')+' (score slur)');slurs+=1
    fit=json.loads((DOC/'harmonic-fit-report.json').read_text());finger=fit[fit.get('finger_used','finger')] if fit.get('finger_used') else fit['finger']
    harmonics=0
    for n in notes:
        f=fretted_harmonic(n.get('score_words'))
        if f in (12,7,5,4) and n['fret']==f:
            op=tuning[n['string']-1];n.update(pitch=op,fret=0,sounding=n['pitch'],pluck_position=finger['pluck'],touched_fret=f,
                touch=dict(position=touch_at(n['string'],op,f,finger['offset_mm']*1e-3),rho=finger['rho_per_s'],lift=finger['lift_s'],width=finger['width_fraction']));harmonics+=1
    log.update(slurs=slurs,harmonics=harmonics)
    # 4. strings ring
    log['sustain']=let_ring(notes,duration)
    ref=audio(d/f'{h}.wav',audio_origin,duration);N=round(duration*SR)
    ref=np.pad(ref,(0,max(0,N-len(ref))))[:N]          # rounding can leave the recording a sample short
    model=Model(lib,'nylon / Gil de Avalle body + loading',5,True)
    # 5. room
    head=[dict(n,velocity=100) for n in notes if n['start']<24-.5]
    room=fit_room(model,head,ref,min(24.,duration),lambda s:print(f'  {slug}: {s}',flush=True))['best'];log['room']=room
    # 6. dynamics
    velocity,info=fit_room_dynamics_long(model,notes,ref,duration,room)
    velocity,clipped=physical_cap(velocity);log.update(clipped=clipped,dynamics=dict(exponent=info['gamma'],constant_db=info['constant_error_db'],fitted_db=info['fitted_error_db']))
    fitted=[dict(n,velocity=float(round(v,1))) for n,v in zip(notes,velocity)];plain=[dict(n,velocity=100.) for n in notes]
    # 7. renders, body, engraving
    a=model.render(plain,duration);b=model.render(fitted,duration)
    # Reject a failed dynamics fit under this model, after physical limits and room.
    # Later body selection is separate; final sound still needs blind acceptance.
    ir=room_impulse(room['rt_low'],room['rt_high'],room['ratio'])
    target=loudness_envelope(ref);live=target>target.max()-40
    def audible_error(y):
        db=loudness_envelope(fftconvolve(y,ir)[:len(y)])[live]-target[live]
        return float(np.sqrt(np.mean((db-np.median(db))**2)))
    log['fit_gate']=fit_guard(velocity,audible_error(a),audible_error(b))
    force=render_guitar(lib,fitted,duration,True,0,body=False,material=1,loading=None,velocity_cap=4).astype(float)
    ranking=body_rank(force,ref,room,slug);log['bodies']=ranking[:5]
    title=f"Guitar · {meta['composer'].split()[-1]} · {meta['title']}"
    desc=(f"{meta['composer']} ({meta['born']}–{meta['died']}), {meta['title']}, the whole piece as performed in GAPS recording {meta['gaps_id']} (YouTube {meta['youtube']}), "
        f"from GAPS’s fine-aligned MIDI with the score’s strings, frets and fingers. B uses velocities fitted to the recording with its room modelled "
        f"(reverberation {room['rt_low']:.1f} s low, {room['rt_high']:.2f} s high), clipped at a 3 mm pluck; A plays every note at the same velocity. "
        f"Strings ring until a hand would stop them. {slurs} score slurs played as hammer-ons/pull-offs; {harmonics} harmonics. Closest measured body: {ranking[0]['id']}. "
        'Local research audition: the recording is kept local.')
    c=write_clip(cid,title,f"{meta['title']} · GAPS {meta['gaps_id']}",dict(current=a,fitted=b,ref=ref),
        dict(current='Constant velocity',fitted='Fitted dynamics',ref='Real performance · GAPS recording'),desc,'Real recording','Which is closer to the recording?')
    c['score']=score(fitted,'guitar',f"{meta['composer']} · {meta['title']}",None,None,'https://aim-qmul.github.io/GAPS/')
    points=json.loads((d/f'{h}-syncpoints.json').read_text())
    c['score']['measures']=[0]+[float(p[1])-origin for p in points if 0<float(p[1])-origin<duration]+[duration]
    c['score']['audit']=dict(passed=None,note='Strings, frets and fingers from the GAPS score.');c['score']['inferred']=False
    for n,src in zip(c['score']['notes'],fitted):
        n['velocity']=src['velocity']
        if src.get('transition')=='legato':n['legato']='HO' if src['articulation'].startswith('hammer') else 'PO'
        if 'touch' in src:n.update(pitch=src['sounding'],fret=src['touched_fret'],tab=f"<{src['touched_fret']}>")
    eng,engraved=engrave(d/f'{h}.xml',c['score']['notes'],duration,OUT/f'scores/{slug}',f'scores/{slug}')
    c['score']['engraving']=dict(eng,edition='Engraved from the GAPS score with Verovio',license='GAPS CC BY-NC-SA 4.0',source='https://aim-qmul.github.io/GAPS/',
        caption='The GAPS score engraved with Verovio, one system at a time; tab numbers under their notes.',check=dict(measures_matched=engraved['matched'],unmatched=engraved['unmatched']))
    log['engraved']=f"{engraved['notes_placed']}/{engraved['notes']}"
    c['dynamics']=dict(model=model.name,range_exponent=info['gamma'],room=room,method='room-aware band-power fit',envelope_error_db=dict(constant_with_room=info['constant_error_db'],fitted_with_room=info['fitted_error_db']))
    c['alignment']=dict(**alignment,source='https://aim-qmul.github.io/GAPS/',gaps_id=meta['gaps_id'],youtube=meta['youtube'])
    c['bodyRanking']=ranking[:8]
    (OUT/f'{cid}-events.json').write_text(json.dumps(fitted,indent=1,default=float)+'\n')
    (OUT/'pieces').mkdir(exist_ok=True);(OUT/'pieces'/f'{slug}.clip.json').write_text(json.dumps(c,default=float)+'\n')
    (DOC/'pieces').mkdir(exist_ok=True);(DOC/'pieces'/f'{slug}.json').write_text(json.dumps(log,indent=1,default=float)+'\n')
    export_web(slug,meta,fitted,tuning,duration,room,ranking,d/f'{h}.xml',info)
    print(f'{slug}: done',json.dumps({k:v for k,v in log.items() if k not in ('arpeggios','bodies')},default=float),flush=True)


ART={'hammer-on':1,'pull-off':2}


def export_web(slug,meta,fitted,tuning,duration,room,ranking,xml,info):
    """docs/guitar/pieces/<slug>/: score.json (pf_score as JSON), score.musicxml.gz (the
    GAPS score, notation part only, notes given ids that score.json refers to) and
    credits. Never the recording."""
    out=WEB/slug;out.mkdir(parents=True,exist_ok=True)
    tree=ET.parse(xml);root=tree.getroot();parts=root.findall('part')
    notes=[]
    for n in fitted:
        art=0;param=0.
        if n.get('transition')=='legato':art=ART['hammer-on' if n['articulation'].startswith('hammer') else 'pull-off'];param=n['legato_amount']
        if n.get('touch'):art=4;param=n['touched_fret']
        notes.append(dict(start=round(n['start'],5),end=round(n['end'],5),pitch=n.get('sounding',n['pitch']),velocity=n['velocity'],string=n['string'],
            fret=n.get('touched_fret',n['fret']),finger=n['finger'] if isinstance(n.get('finger'),int) else -1,articulation=art,art_param=param,
            measure=n['score_measure'],xml_id=f"n{n['score_xml_index']}"))
    perf=dict(format='pfsynth score 1',instrument='guitar',tuning=tuning,duration=duration,notes=notes,
        title=meta['title'],composer=meta['composer'],composer_dates=f"{meta['born']}–{meta['died']}",
        performance=dict(source='GAPS v1 fine-aligned MIDI (Riley, Guo, Edwards & Dixon, ISMIR 2024), CC BY-NC-SA 4.0',gaps_id=meta['gaps_id'],youtube=meta['youtube'],video_title=meta['video_title'],
            dynamics='velocities fitted to the recording with its room (pfsynth), clipped at a 3 mm pluck',envelope_error_db=dict(constant=info['constant_error_db'],fitted=info['fitted_error_db'])),
        room=room,body=ranking[0]['id'],license='CC BY-NC-SA 4.0 (derived from GAPS)')
    (out/'score.json').write_text(json.dumps(perf,separators=(',',':'),default=float))
    for k,x in enumerate(parts[0].iter('note')):x.set('id',f'n{k}')     # ids the notes refer to
    for p in parts[1:]:root.remove(p)
    for sp in root.find('part-list').findall('score-part')[1:]:root.find('part-list').remove(sp)
    (out/'score.musicxml.gz').write_bytes(gzip.compress(ET.tostring(root,encoding='utf-8',xml_declaration=True),9))


def main():
    lib,_=library();setup(lib)
    for slug in sys.argv[1:]:build(slug,lib)


if __name__=='__main__':main()
