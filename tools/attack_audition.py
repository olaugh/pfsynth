"""Experiment 03: Beethoven Op. 2 No. 1 opening with the onset model, against Pianoteq.

Two new comparisons are added to the audition (existing clips, audio and vote storage untouched):
  beethoven-attack   A/B = tonal baseline (frozen Salamander-fitted patch)  vs  baseline + pf_attack
  beethoven-ptq-fit  A/B = baseline + pf_attack  vs  Pianoteq-fitted onset-exact tonal patch + pf_attack
Reference for both: Pianoteq 6 "Steinway D Close Mic Classical" rendering the same MIDI events
(original key releases and CC64 pedal; notes still down at the excerpt boundary released there).
Same note schedule and 240 ms damper release as experiment 02; whole-clip RMS matching per clip.
"""
import os; os.environ.setdefault('PF_REF','ptq')
import ctypes as ct, hashlib, json, numpy as np
from scipy.io import wavfile
from partial_audition import ROOT, SR, Patch, Voice, name
from body_audition import rms
import attack_verify as av
import ptq_render
SCORE=ROOT/'experiments/partial-piano/beethoven-score.json'; PUBLIC=ROOT/'audition/public'; ATT=ROOT/'experiments/attack-ptq'
PATCH_SAL=ROOT/'experiments/partial-piano/patch.bin'; PATCH_PTQ=ROOT/'experiments/partial-piano-ptq/patch.bin'

def render_passage(lib,patch,apatch,notes,total,attack):
    music=np.zeros(total)
    for note in notes:
        midi=note['note']; vel=note['velocity']/127; start=round(note['start']*SR); hold=round(note['hold']*SR)
        length=min(total-start,hold+int(1.2*SR)); buf=np.zeros(length,np.float32)
        v=Voice(); lib.pf_partial_init(v,patch,SR,float(midi),vel)
        if hold: lib.pf_partial_process(v,buf.ctypes.data_as(ct.POINTER(ct.c_float)),hold)
        lib.pf_partial_release(v); lib.pf_partial_process(v,buf[hold:].ctypes.data_as(ct.POINTER(ct.c_float)),length-hold)
        if attack:
            a=av.AVoice(); lib.pf_attack_init(a,apatch,SR,float(midi),vel); lib.pf_attack_process(a,buf.ctypes.data_as(ct.POINTER(ct.c_float)),length)
        assert np.isfinite(buf).all(); music[start:start+length]+=buf
    return music

def write_clip(clip_id,versions,labels,note,dynamic,description,experiment,duration):
    audio={k:x/rms(x) for k,x in versions.items()}; gain=min(.1,.9/max(np.abs(x).max() for x in audio.values())); urls={}; levels=[]
    for key,x in audio.items():
        x=x*gain; x[-int(.08*SR):]*=np.linspace(1,0,int(.08*SR)); assert np.max(np.abs(x))<=.901
        fn=f'{clip_id}-{key}.wav'; wavfile.write(PUBLIC/'audio'/fn,SR,np.round(x*32767).astype(np.int16)); urls[key]='/audio/'+fn
        sr,y=wavfile.read(PUBLIC/'audio'/fn); assert sr==SR and len(y)==duration*SR; levels.append(rms(y.astype(float)))
    assert 20*np.log10(max(levels)/min(levels))<.01
    return dict(id=clip_id,note=note,dynamic=dynamic,split='music',duration=duration,audio=urls,labels=labels,description=description,experiment=experiment)

def main():
    score=json.loads(SCORE.read_text()); notes=score['notes']; duration=score['duration']; total=duration*SR
    lib=av.libs(); apatch=av.load_apatch()
    sal=Patch.from_buffer_copy(PATCH_SAL.read_bytes()); ptq=Patch.from_buffer_copy(PATCH_PTQ.read_bytes())
    ref=ptq_render.beethoven(SCORE,ptq_render.CACHE/'beethoven-op2-no1-ptq.wav'); assert len(ref)==total
    base=render_passage(lib,sal,apatch,notes,total,attack=False); print('rendered baseline',flush=True)
    base_att=render_passage(lib,sal,apatch,notes,total,attack=True); print('rendered baseline+attack',flush=True)
    ptq_att=render_passage(lib,ptq,apatch,notes,total,attack=True); print('rendered ptq-fit+attack',flush=True)
    clips=[write_clip('beethoven-attack',dict(current=base,fitted=base_att,ref=ref),dict(current='Tonal baseline',fitted='Baseline + attack',ref='Pianoteq Steinway D'),
                      'Beethoven · attack model','I. Allegro','Op. 2 No. 1 opening · frozen tonal baseline with and without the generated thump/noise onset · reference: Pianoteq Steinway D Close Mic Classical.','Experiment 03 · does the onset component help?',duration),
           write_clip('beethoven-ptq-fit',dict(current=base_att,fitted=ptq_att,ref=ref),dict(current='Baseline + attack',fitted='Pianoteq-fit tone + attack',ref='Pianoteq Steinway D'),
                      'Beethoven · Pianoteq-fitted tone','I. Allegro','Op. 2 No. 1 opening · both with the onset component · B swaps the tonal patch for one identified from Pianoteq notes with onset-exact envelopes.','Experiment 03 · does refitting the tone to Pianoteq help?',duration)]
    manifest=json.loads((PUBLIC/'partial-audition.json').read_text()); keep=[c for c in manifest['clips'] if c['id'] not in {c2['id'] for c2 in clips}]
    manifest['clips']=clips+keep; manifest['attribution']=manifest['attribution']+' Experiment 03 reference: Pianoteq 6 (Modartt) Steinway D Close Mic Classical, rendered from the same MIDI; used as a measured black-box target, nothing copied.'
    (PUBLIC/'partial-audition.json').write_text(json.dumps(manifest,indent=2)+'\n')
    sha=lambda p:hashlib.sha256(Path(p).read_bytes()).hexdigest()
    (ATT/'beethoven-report.json').write_text(json.dumps(dict(score=str(SCORE.relative_to(ROOT)),score_sha256=sha(SCORE),patch_sal_sha256=sha(PATCH_SAL),patch_ptq_sha256=sha(PATCH_PTQ),attack_json_sha256=sha(ATT/'attack.json'),
        pf_attack_c_sha256=sha(ROOT/'src/core/pf_attack.c'),pf_partial_c_sha256=sha(ROOT/'src/core/pf_partial.c'),reference='Pianoteq 6.7.3 preset "Steinway D Close Mic Classical", MIDI: original key releases + CC64, boundary release at cutoff',clips=[c['id'] for c in clips],
        peak_dbfs={c['id']:{k:float(20*np.log10(np.abs(wavfile.read(PUBLIC/'audio'/f'{c["id"]}-{k}.wav')[1].astype(float)/32768).max())) for k in c['audio']} for c in clips}),indent=2)+'\n')
    print('done:',[c['id'] for c in clips])
from pathlib import Path
if __name__=='__main__':main()
