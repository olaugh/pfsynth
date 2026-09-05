"""Experiment 04: half pedaling and una corda on real performances, against Pianoteq.

For each excerpt: A = binary pedal (CC64 >= 64 sustains, fixed 240 ms release, CC67 ignored: the
behaviour of experiments 02/03); B = continuous damper following the raw CC64 position plus the
una corda timbre from CC67 (pf_partial_init2/pf_partial_pedal), sostenuto captured if present.
Both on the Pianoteq-fitted tone + pf_attack.  Reference: Pianoteq rendering the same events.
"""
import os; os.environ.setdefault('PF_REF','ptq')
import ctypes as ct, hashlib, json, math, subprocess, numpy as np
from pathlib import Path
from scipy.io import wavfile
from partial_audition import ROOT, BUILD, SR, Patch, Voice, PedalParams, name
from body_audition import rms
import attack_verify as av, ptq_render
from mkmidi import write_midi
PUBLIC=ROOT/'audition/public'; OUT=ROOT/'experiments/pedal'; PATCH=ROOT/'experiments/partial-piano-wide/pianoteq.bin'
class Event(ct.Structure):_fields_=[('t',ct.c_double),('type',ct.c_ubyte),('note',ct.c_ubyte),('val',ct.c_ubyte)]
class Song(ct.Structure):_fields_=[('ev',ct.POINTER(Event)),('n',ct.c_int),('duration',ct.c_double)]
NOTE_OFF,NOTE_ON,PEDAL,SOSTENUTO,SOFT=0,1,2,3,4
EXCERPTS=[dict(id='pedal-op110',title='Beethoven · Sonata No. 31',dynamic='Op. 110 · I. coda',source='/Users/john/sources/segno/tools/synth-ab/corpora/asap/Beethoven/Piano_Sonatas/31-1/Kavalerova01.mid',start=342.5,length=25,annotations=True,license='(n)ASAP Kavalerova01, CC BY-NC-SA 4.0'),
          dict(id='pedal-andante',title='Beethoven · Andante favori',dynamic='WoO 57 · closing pages',source=str(ROOT/'maestro/midi/maestro-v3.0.0/2018/MIDI-Unprocessed_Chamber6_MID--AUDIO_20_R3_2018_wav--1.midi'),start=490.0,length=25,annotations=False,license='MAESTRO v3.0.0, CC BY-NC-SA 4.0')]

def load_events(path):
    subprocess.run(['cc','-O2','-std=c99','-dynamiclib','src/host/midi.c','-o',str(BUILD/'midi.dylib')],cwd=ROOT,check=True)
    L=ct.CDLL(str(BUILD/'midi.dylib')); L.pf_midi_load.argtypes=[ct.POINTER(Song),ct.c_char_p]; L.pf_midi_free.argtypes=[ct.POINTER(Song)]
    song=Song(); assert L.pf_midi_load(song,str(path).encode())==0
    ev=[(e.t,e.type,e.note,e.val) for e in song.ev[:song.n]]; L.pf_midi_free(song); return ev

def excerpt(ex):
    ev=load_events(ex['source']); t0=ex['start']; cutoff=t0+ex['length']
    if ex['annotations']:
        ann=Path(ex['source']).with_name(Path(ex['source']).stem+'_annotations.txt')
        dbs=[float(l.split('\t')[0]) for l in ann.read_text().splitlines() if l.split('\t')[-1].startswith('db')]
        cutoff=next((t for t in dbs if t>=t0+ex['length']-3),cutoff)
    # controller state in effect at t0
    state={PEDAL:0,SOSTENUTO:0,SOFT:0}
    for t,ty,n,v in ev:
        if t>=t0: break
        if ty in state: state[ty]=v
    notes=[]; held={}; ccs=[]
    for t,ty,n,v in ev:
        if t<t0 or t>=cutoff: continue
        if ty==NOTE_ON: note=dict(note=n,velocity=v,start=t-t0,key_release=None); notes.append(note); held.setdefault(n,[]).append(note)
        elif ty==NOTE_OFF:
            q=held.get(n,[])
            if q: q.pop(0)['key_release']=t-t0
        else: ccs.append((t-t0,ty,v))
    for n in notes:
        if n['key_release'] is None: n['key_release']=cutoff-t0
    # the excerpt ends with pedals up so every version (and Pianoteq) damps the same way at the boundary
    ccs+=[(cutoff-t0,PEDAL,0),(cutoff-t0,SOSTENUTO,0)]
    dur=math.ceil(cutoff-t0+3)
    return dict(notes=notes,ccs=ccs,state0=state,duration=dur,cutoff=cutoff-t0,source_start=t0,source_cutoff=cutoff)

def cc_track(ccs,state0,ty,dur):
    """Piecewise-constant controller value over time as list of (t,val), starting at t=0."""
    tr=[(0.0,state0[ty])]+[(t,v) for t,k,v in ccs if k==ty]; return tr

def render_model(lib,patch,apatch,pp,ex,continuous):
    total=ex['duration']*SR; music=np.zeros(total)
    ped=cc_track(ex['ccs'],ex['state0'],PEDAL,ex['duration']); sos=cc_track(ex['ccs'],ex['state0'],SOSTENUTO,ex['duration']); soft=cc_track(ex['ccs'],ex['state0'],SOFT,ex['duration'])
    def value(tr,t):
        v=tr[0][1]
        for tt,vv in tr:
            if tt<=t: v=vv
            else: break
        return v
    # sostenuto capture: notes whose key was down when CC66 went >= 64 are held until it goes < 64
    sos_edges=[(t,v>=64) for t,v in sos]
    for n in ex['notes']:
        start=n['start']; rel=n['key_release']; midi=n['note']; vel=n['velocity']/127
        # effective end of sustain for this note (binary logic): key up, and pedal (or sostenuto capture) not holding it
        def held_by_pedal(t):
            p=value(ped,t)>=64
            cap=any(t_e>=start and t_e<rel and d for t_e,d in sos_edges) and value(sos,t)>=64  # captured while key down and CC66 still down
            return p or cap
        # find the moment damping starts for the binary model: first time >= rel where nothing holds the note
        t=rel; events_after=sorted(set([tt for tt,_,_ in ex['ccs'] if tt>=rel]+[rel]))
        damp_t=None
        for tt in events_after:
            if not held_by_pedal(tt): damp_t=tt; break
        if damp_t is None: damp_t=ex['cutoff']
        s0=round(start*SR); length=min(total-s0,int((min(damp_t,ex['cutoff'])-start)*SR)+int(3.0*SR))
        buf=np.zeros(length,np.float32); v=Voice()
        if continuous:
            lib.pf_partial_init2(v,patch,SR,float(midi),vel,pp,value(soft,start)/127)
            # segment boundaries: key release, every CC64 change while the note lives; sostenuto capture holds the damper up
            marks=sorted(set([0.0,rel-start]+[tt-start for tt,k,_ in ex['ccs'] if k in (PEDAL,SOSTENUTO) and start<tt<start+length/SR]+[length/SR]))
            pos=0
            for a,b in zip(marks,marks[1:]):
                tabs=start+a
                captured=any(t_e>=start and t_e<rel and d for t_e,d in sos_edges) and value(sos,tabs)>=64
                lib.pf_partial_pedal(v,1.0 if captured else value(ped,tabs)/127)
                if a>=rel-start-1e-9 and v.key_down: lib.pf_partial_release(v)
                nseg=min(length,int(round(b*SR)))-pos
                if nseg>0: lib.pf_partial_process(v,buf[pos:].ctypes.data_as(ct.POINTER(ct.c_float)),nseg); pos+=nseg
        else:
            lib.pf_partial_init(v,patch,SR,float(midi),vel); hold=min(length,int((damp_t-start)*SR))
            if hold>0: lib.pf_partial_process(v,buf.ctypes.data_as(ct.POINTER(ct.c_float)),hold)
            lib.pf_partial_release(v); lib.pf_partial_process(v,buf[hold:].ctypes.data_as(ct.POINTER(ct.c_float)),length-hold)
        a=av.AVoice(); lib.pf_attack_init(a,apatch,SR,float(midi),vel); lib.pf_attack_process(a,buf.ctypes.data_as(ct.POINTER(ct.c_float)),length)
        assert np.isfinite(buf).all(); music[s0:s0+length]+=buf
    return music

def render_ptq(ex,tag):
    ev=[(0.0,0xB0,cc,ex['state0'][ty]) for ty,cc in ((PEDAL,64),(SOSTENUTO,66),(SOFT,67))]
    for n in ex['notes']: ev.append((n['start'],0x90,n['note'],n['velocity'])); ev.append((min(n['key_release'],ex['cutoff']),0x80,n['note'],0))
    for t,ty,v in ex['ccs']: ev.append((t,0xB0,{PEDAL:64,SOSTENUTO:66,SOFT:67}[ty],v))
    ev+=[(ex['duration']-.01,0xB0,123,0)]
    wav=ptq_render.CACHE/f'{tag}-ptq.wav'; mid=wav.with_suffix('.mid'); import io; buf=io.BytesIO(); write_midi(buf,ev); new=buf.getvalue()
    if not (wav.exists() and mid.exists() and mid.read_bytes()==new): mid.write_bytes(new); ptq_render.run_ptq(mid,wav)   # Pianoteq is not bit-deterministic
    x=ptq_render.load_mono(wav); total=ex['duration']*SR; return np.pad(x[:total],(0,max(0,total-len(x))))

def write_clip(clip_id,versions,labels,note,dynamic,description,experiment,duration):
    audio={k:x/rms(x) for k,x in versions.items()}; gain=min(.1,.9/max(np.abs(x).max() for x in audio.values())); urls={}; levels=[]
    for key,x in audio.items():
        x=x*gain; x[-int(.08*SR):]*=np.linspace(1,0,int(.08*SR)); assert np.max(np.abs(x))<=.901
        fn=f'{clip_id}-{key}.wav'; wavfile.write(PUBLIC/'audio'/fn,SR,np.round(x*32767).astype(np.int16)); urls[key]='/audio/'+fn
        sr,y=wavfile.read(PUBLIC/'audio'/fn); assert sr==SR and len(y)==duration*SR; levels.append(rms(y.astype(float)))
    assert 20*np.log10(max(levels)/min(levels))<.01
    return dict(id=clip_id,note=note,dynamic=dynamic,split='music',duration=duration,audio=urls,labels=labels,description=description,experiment=experiment)

def main():
    lib=av.libs(); lib.pf_partial_init2.argtypes=[ct.POINTER(Voice),ct.POINTER(Patch),ct.c_double,ct.c_double,ct.c_double,ct.POINTER(PedalParams),ct.c_double]
    lib.pf_partial_pedal.argtypes=[ct.POINTER(Voice),ct.c_double]; lib.pf_pedal_defaults.argtypes=[ct.POINTER(PedalParams)]
    patch=Patch.from_buffer_copy(PATCH.read_bytes()); apatch=av.load_apatch(); pp=PedalParams(); lib.pf_pedal_defaults(pp)
    clips=[]; report=[]
    for ex_def in EXCERPTS:
        ex=excerpt(ex_def); print(ex_def['id'],'notes',len(ex['notes']),'cc events',len(ex['ccs']),'duration',ex['duration'],'state at start',ex['state0'],flush=True)
        ref=render_ptq(ex,ex_def['id']); a=render_model(lib,patch,apatch,pp,ex,False); b=render_model(lib,patch,apatch,pp,ex,True); print('  rendered',flush=True)
        n64=sum(1 for _,k,_ in ex['ccs'] if k==PEDAL); n67=sum(1 for _,k,_ in ex['ccs'] if k==SOFT); n66=sum(1 for _,k,_ in ex['ccs'] if k==SOSTENUTO)
        clips.append(write_clip(ex_def['id'],dict(current=a,fitted=b,ref=ref),dict(current='Binary pedal',fitted='Half pedal + una corda',ref='Pianoteq Steinway D'),ex_def['title'],ex_def['dynamic'],
            f"{ex_def['dynamic']} · {len(ex['notes'])} notes, {n64} sustain-pedal moves, {n67} soft-pedal moves{', '+str(n66)+' sostenuto' if n66 else ''} · A: CC64 thresholded at 64, fixed release, CC67 ignored · B: damper follows the pedal position, una corda from CC67.",
            'Experiment 04 · does continuous pedaling help?',ex['duration']))
        report.append(dict(id=ex_def['id'],source=ex_def['source'],source_sha256=hashlib.sha256(Path(ex_def['source']).read_bytes()).hexdigest(),source_start=ex['source_start'],source_cutoff=ex['source_cutoff'],duration=ex['duration'],notes=len(ex['notes']),cc64=n64,cc67=n67,cc66=n66,state0=ex['state0'],license=ex_def['license']))
    manifest=json.loads((PUBLIC/'partial-audition.json').read_text()); ids={c['id'] for c in clips}
    manifest['clips']=clips+[c for c in manifest['clips'] if c['id'] not in ids]; (PUBLIC/'partial-audition.json').write_text(json.dumps(manifest,indent=2)+'\n')
    (OUT/'audition-report.json').write_text(json.dumps(dict(excerpts=report,pedal_params={n:round(getattr(pp,n),4) for n,_ in pp._fields_},pf_partial_c_sha256=hashlib.sha256((ROOT/'src/core/pf_partial.c').read_bytes()).hexdigest()),indent=2)+'\n')
    print('done',[c['id'] for c in clips])
if __name__=='__main__':main()
