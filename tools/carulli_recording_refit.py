"""Refit Carulli locally from the public score plus a local reference recording.

    OPENBLAS_NUM_THREADS=1 python tools/carulli_recording_refit.py \
        --recording research/strings/midi/gaps/carulli-op241-5/cV1wc.wav \
        --output build/carulli-refit-new

Requires the usual body-venv dependencies and a C compiler. Does not need private
MIDI/XML: public note timings/strings are sufficient. Never writes docs/ or overwrites
an existing result directory. Candidate scores and audio stay in ignored build/.
--events can use the original unrounded local events for exact research reproduction.
"""
import argparse,copy,hashlib,itertools,json
from pathlib import Path
import numpy as np
from scipy.io import wavfile
from scipy.signal import fftconvolve
from stringlab_audition import ROOT,SR,library
from string_gesture_audition import setup
from paired_string_audition import audio
from guitar_dynamics_fit import Model,templates,fit_room_dynamics_long,physical_cap,loudness_envelope
from guitar_room_fit import power,roomify,fit,room_impulse
from guitar_reference import reference_origin,fit_guard


def native_notes(piece):
    if any(n.get('articulation',0) for n in piece['notes']):
        raise ValueError('This Carulli utility expects ordinary plucked notes')
    return [dict(n,bend=[],slides=[],hammer_to=False,mute=False) for n in piece['notes']]


def main():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--recording',type=Path,required=True);ap.add_argument('--output',type=Path,required=True)
    ap.add_argument('--score',type=Path,default=ROOT/'docs/guitar/pieces/carulli-op241-5/score.json')
    ap.add_argument('--alignment-manifest',type=Path,help='Optional locally verified offsets for a different decoded audio file')
    ap.add_argument('--events',type=Path,help='Optional original unrounded local note events')
    ap.add_argument('--check-inputs',action='store_true',help='Validate provenance and notes without fitting or creating outputs')
    args=ap.parse_args();out=args.output.resolve()
    if not out.is_relative_to((ROOT/'build').resolve()):raise ValueError('Write local fits under ignored build/')
    if out.exists():raise FileExistsError(f'Preserve existing results; choose a new directory: {out}')
    piece=json.loads(args.score.read_text());duration=piece['duration'];notes=native_notes(piece)
    if piece['performance']['gaps_id']!='055_cV1wc':raise ValueError('This utility is for Carulli Op. 241 No. 5')
    origin,alignment=reference_origin(args.recording,2.6363636363636362,'055_cV1wc',args.alignment_manifest)
    if not alignment['verified']:raise ValueError('Verify this recording and provide an alignment manifest before fitting')
    if args.events:
        notes=json.loads(args.events.read_text())
        if len(notes)!=len(piece['notes']) or any(abs(n['start']-p['start'])>1e-5 or n['pitch']!=p['pitch'] for n,p in zip(notes,piece['notes'])):
            raise ValueError('Original events do not match the public score')
    body_path=ROOT/'docs/guitar/bodies'/f"{piece['body']}.wav"
    for n in notes:
        if not (0<=n['start']<n['end']<=duration and 1<=n['string']<=6):raise ValueError('Invalid note or duration')
    if args.check_inputs:
        print(json.dumps(dict(alignment=alignment,notes=len(notes),duration=duration,body=str(body_path)),indent=2));return
    out.mkdir(parents=True)
    lib,_=library();setup(lib);model=Model(lib,'Published nylon model and body',None,False)
    sr,body=wavfile.read(body_path)
    if sr!=SR:raise ValueError('Body sample rate differs from rendering rate')
    body=body.astype(float);body/=np.sqrt(np.sum(body**2));model.body={'impulse':body}
    ref=audio(args.recording,origin,duration)
    dtype=wavfile.read(args.recording,mmap=True)[1].dtype
    if np.issubdtype(dtype,np.integer):ref/=max(abs(np.iinfo(dtype).min),np.iinfo(dtype).max)
    ref=np.pad(ref,(0,max(0,round(duration*SR)-len(ref))))[:round(duration*SR)]
    plain=[dict(n,velocity=100.) for n in notes]
    inputs=dict(alignment=alignment,score_sha256=hashlib.sha256(args.score.read_bytes()).hexdigest(),
        body_sha256=hashlib.sha256(body_path.read_bytes()).hexdigest(),events_sha256=hashlib.sha256(args.events.read_bytes()).hexdigest() if args.events else None)
    (out/'inputs.json').write_text(json.dumps(inputs,indent=2))
    length=min(16.,duration);head=[n for n in plain if n['start']<length-.3]
    V=power(ref[:round(length*SR)]);dry=np.array([power(p) for p in templates(model,head,np.full(len(head),100.),length)])
    rows=[]
    for rl,rh,ratio in [(0,0,0)]+list(itertools.product([.3,.8,1.6,3.],[.25,.7,1.2],[.2,.7,1.5])):
        r=fit(V,dry if ratio==0 else roomify(dry,rl,rh,ratio),[n['start'] for n in head],iterations=150)
        rows.append(dict(rt_low=rl,rt_high=rh,ratio=ratio,loss=r['loss']));print('room',rows[-1],flush=True)
    (out/'room-search.json').write_text(json.dumps(rows,indent=2));room=min(rows,key=lambda r:r['loss'])
    if room['ratio']==0:room=dict(room,rt_low=.3,rt_high=.3)
    v,info=fit_room_dynamics_long(model,plain,ref,duration,room)
    (out/'velocity-estimate.json').write_text(json.dumps(dict(velocity=v.tolist(),info=info),indent=2))
    ir=room_impulse(room['rt_low'],room['rt_high'],room['ratio']);target=loudness_envelope(ref);live=target>target.max()-40
    def render(velocity,impulse=ir):
        y=model.render([dict(n,velocity=float(u)) for n,u in zip(notes,velocity)],duration)
        return fftconvolve(y,impulse)[:len(y)]
    def error(y):
        db=loudness_envelope(y)[live]-target[live];return float(np.sqrt(np.mean((db-np.median(db))**2)))
    calibration=[]
    for gamma in (0,.25,.5,.75,1.,1.25,1.5,2.):
        vel,clipped=physical_cap(100*(v/100)**gamma);y=render(vel)
        calibration.append(dict(gamma=gamma,error_db=error(y),clipped=clipped));print(calibration[-1],flush=True)
    best=min(calibration,key=lambda r:r['error_db']);v,clipped=physical_cap(100*(v/100)**best['gamma'])
    candidate=render(v);constant=render(np.full(len(notes),100.))
    gate=fit_guard(v,error(constant),error(candidate))
    report=dict(**inputs,room=room,calibration=calibration,best=best,notes=len(notes),duration=duration,gate=gate,
        velocity_range=[float(v.min()),float(v.max())],scope='in-sample fit, local blind acceptance still required')
    (out/'report.json').write_text(json.dumps(report,indent=2))
    current=render([n['velocity'] for n in piece['notes']],room_impulse(**piece['room']))
    for name,y in [('candidate',candidate),('constant',constant),('current',current),('reference',ref)]:
        if not np.isfinite(y).all():raise ValueError('Nonfinite rendered audio')
        wavfile.write(out/(name+'.wav'),SR,y.astype(np.float32))
    result=copy.deepcopy(piece)
    for n,u in zip(result['notes'],v):n['velocity']=float(u)
    result['room']={k:room[k] for k in ('rt_low','rt_high','ratio')}
    result['performance']['alignmentCorrection']=alignment
    result['performance']['dynamics']='Refitted to the audio-aligned Jonathan Richter recording with the published body and a jointly fitted room; bounded at 3 mm; awaiting blind listening acceptance'
    (out/'candidate-score.json').write_text(json.dumps(result,separators=(',',':')))
    (out/'fitted-events.json').write_text(json.dumps([dict(n,velocity=float(u)) for n,u in zip(notes,v)],indent=2))
    print('Local candidate ready:',out,flush=True)
if __name__=='__main__':main()
