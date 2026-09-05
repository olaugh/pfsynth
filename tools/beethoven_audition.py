"""Render the local (n)ASAP Beethoven Op.2 No.1 performance into existing audition.
Reuses the frozen, listener-selected partial patch; no fitting or tone changes.
"""
from pathlib import Path
import ctypes as ct
import hashlib,json,subprocess,math
import numpy as np
from scipy import signal
from scipy.io import wavfile
from partial_audition import ROOT,BUILD,OUT,PUBLIC,SR,Patch,Voice,sample
from body_audition import align,rms
SOURCE=Path('/Users/john/sources/segno/tools/synth-ab/corpora/asap/Beethoven/Piano_Sonatas/1-1/KimG01.mid')
class Event(ct.Structure):_fields_=[('t',ct.c_double),('type',ct.c_ubyte),('note',ct.c_ubyte),('val',ct.c_ubyte)]
class Song(ct.Structure):_fields_=[('ev',ct.POINTER(Event)),('n',ct.c_int),('duration',ct.c_double)]

def main():
    subprocess.run(['cc','-O2','-std=c99','-dynamiclib','src/host/midi.c','-o',str(BUILD/'midi.dylib')],cwd=ROOT,check=True)
    loader=ct.CDLL(str(BUILD/'midi.dylib'));loader.pf_midi_load.argtypes=[ct.POINTER(Song),ct.c_char_p];loader.pf_midi_free.argtypes=[ct.POINTER(Song)]
    song=Song();assert loader.pf_midi_load(ct.byref(song),str(SOURCE).encode())==0
    ev=[(e.t,e.type,e.note,e.val) for e in song.ev[:song.n]];loader.pf_midi_free(ct.byref(song))
    first=min(t for t,kind,n,v in ev if kind==1 and v>0);origin=first-.04
    # Stop note admission at a downbeat after about 20 seconds; release existing
    # voices naturally, then keep two seconds for the final decay.
    downbeats=[float(line.split('\t')[0]) for line in SOURCE.with_name('KimG01_annotations.txt').read_text().splitlines() if line.split('\t')[-1].startswith('db')]
    cutoff=next(t for t in downbeats if t-origin>=20);duration=math.ceil(cutoff-origin+2)
    notes=[];held={};sustained=[];pedal=False;pedals=[]
    for t,kind,n,val in ev:
        if t>=cutoff:break
        if kind==1 and val:
            note=dict(note=n,velocity=val,start=t-origin,key_release=None,end=None);notes.append(note);held.setdefault(n,[]).append(note)
        elif kind==0 or (kind==1 and val==0):
            q=held.get(n,[])
            if q:
                note=q.pop(0);note['key_release']=t-origin
                if pedal:sustained.append(note)
                else:note['end']=t-origin
        elif kind==2:
            down=val>=64;pedals.append(dict(time=t-origin,value=val))
            if pedal and not down:
                for note in sustained:note['end']=t-origin
                sustained=[]
            pedal=down
    for note in notes:
        if note['end'] is None:note['end']=cutoff-origin
        note['hold']=max(0,note['end']-note['start'])
    patch=Patch.from_buffer_copy((OUT/'patch.bin').read_bytes())
    subprocess.run(['cc','-O3','-std=c99','-dynamiclib','src/core/pf_partial.c','-o',str(BUILD/'partial.dylib')],cwd=ROOT,check=True)
    lib=ct.CDLL(str(BUILD/'partial.dylib'));lib.pf_partial_init.argtypes=[ct.POINTER(Voice),ct.POINTER(Patch),ct.c_double,ct.c_double,ct.c_double];lib.pf_partial_process.argtypes=[ct.POINTER(Voice),ct.POINTER(ct.c_float),ct.c_int];lib.pf_partial_release.argtypes=[ct.POINTER(Voice)]
    total=duration*SR;music={k:np.zeros(total) for k in ['current','fitted','ref']};cache={};refs={}
    for index,note in enumerate(notes):
        midi=note['note'];vel=note['velocity']/127;start=round(note['start']*SR);hold=round(note['hold']*SR)
        length=min(total-start,hold+int(1.2*SR));release=np.ones(length);release[hold:]=np.exp(-6.907755*np.arange(length-hold)/(SR*.24))
        voice=Voice();lib.pf_partial_init(ct.byref(voice),ct.byref(patch),SR,midi,vel);new=np.zeros(length,np.float32)
        if hold:lib.pf_partial_process(ct.byref(voice),new.ctypes.data_as(ct.POINTER(ct.c_float)),hold)
        lib.pf_partial_release(ct.byref(voice));lib.pf_partial_process(ct.byref(voice),new[hold:].ctypes.data_as(ct.POINTER(ct.c_float)),length-hold)
        key=(midi,note['velocity'],length)
        if key not in cache:
            raw=BUILD/f'beethoven-old-{index}.f32';secs=(length+1000)/SR
            subprocess.run([str(ROOT/'build/body-fit/bodyrender'),str(midi),str(vel),str(secs),str(raw)],check=True)
            x=np.fromfile(raw,np.float32).reshape(-1,2);_,offset=align(x[:,0],len(x));cache[key]=np.pad(x[offset:,1],(0,offset))[:length]
        old=cache[key]*release
        nearest=min(range(24,109,3),key=lambda p:abs(p-midi));ratio=2**((midi-nearest)/12)
        refkey=(nearest,midi)
        if refkey not in refs:
            refs[refkey]=[signal.resample(sample(nearest,layer,9),round(9*SR/ratio)) for layer in [6,13]]
        v=np.clip((note['velocity']-48)/52,0,1);ref=((1-v)*refs[refkey][0][:length]+v*refs[refkey][1][:length])*release
        # Match the same velocity extrapolation used by the new model below/above fitted layers.
        extra=(note['velocity']/48)**1.5 if note['velocity']<48 else (note['velocity']/100)**1.3 if note['velocity']>100 else 1
        ref*=extra
        for key,x in [('current',old),('fitted',new),('ref',ref)]:
            assert np.isfinite(x).all();music[key][start:start+length]+=x
        if index%30==0:print('Rendered',index+1,'/',len(notes),flush=True)
    audio={k:x/rms(x) for k,x in music.items()};gain=min(.1,.9/max(np.abs(x).max() for x in audio.values()));urls={};levels=[]
    for key,x in audio.items():
        x=x*gain;x[-int(.08*SR):]*=np.linspace(1,0,int(.08*SR));assert np.max(np.abs(x))<=.901
        filename=f'beethoven-op2-no1-{key}.wav';wavfile.write(PUBLIC/'audio'/filename,SR,np.round(x*32767).astype(np.int16));urls[key]='/audio/'+filename
        sr,y=wavfile.read(PUBLIC/'audio'/filename);assert len(y)==total and sr==SR;levels.append(rms(y.astype(float)))
    mismatch=20*np.log10(max(levels)/min(levels));assert mismatch<.01
    clip=dict(id='beethoven-op2-no1',note='Beethoven · Sonata No. 1',dynamic='I. Allegro',split='music',duration=duration,audio=urls,description='Op. 2 No. 1 in F minor · opening · (n)ASAP KimG01 performance MIDI · sustain-aware release timing.')
    manifest=json.loads((PUBLIC/'partial-audition.json').read_text());manifest['clips']=[clip]+[c for c in manifest['clips'] if c['id']!=clip['id']];(PUBLIC/'partial-audition.json').write_text(json.dumps(manifest,indent=2)+'\n')
    report=dict(title='Beethoven Piano Sonata No.1 in F minor, Op.2 No.1, I. Allegro',source=str(SOURCE),source_sha256=hashlib.sha256(SOURCE.read_bytes()).hexdigest(),patch_sha256=hashlib.sha256((OUT/'patch.bin').read_bytes()).hexdigest(),source_start=origin,source_cutoff=cutoff,duration=duration,notes=notes,pedal_events=pedals,rms_mismatch_db=mismatch,reference='Sampled reconstruction, not original performance audio',license='(n)ASAP: CC BY-NC-SA 4.0; Salamander: Alexander Holm, CC BY 3.0')
    (OUT/'beethoven-score.json').write_text(json.dumps(report,indent=2)+'\n');print('Finished:',duration,'seconds;',len(notes),'notes;',len(pedals),'pedal events; RMS mismatch',mismatch,flush=True)
if __name__=='__main__':main()
