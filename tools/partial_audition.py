"""Compact physics-informed modal piano: offline identification, C rendering, music.
Adaptation of Simionato et al. 2024's quasi-harmonic signal model, not neural inference.
"""
from pathlib import Path
import ctypes as ct
import json, subprocess, hashlib, zlib
import numpy as np
from scipy import signal, optimize
from scipy.io import wavfile
from body_audition import align, rms, normalized, spectral_error, spectrum
ROOT=Path(__file__).resolve().parents[1]
SR=44100; MODES=64
TIMES=np.array([0,.015,.04,.09,.2,.45,.9,1.8,3.6,6])
ANCHORS=np.arange(36,85,6); LAYERS=[6,13]
SAMPLES=ROOT/'calib/SalamanderGrandPianoV3_44.1khz16bit/44.1khz16bit'
OUT=ROOT/'experiments/partial-piano'; BUILD=ROOT/'build/partial-piano'; PUBLIC=ROOT/'audition/public'
NAMES=['C','C#','D','D#','E','F','F#','G','G#','A','A#','B']
def name(m):return NAMES[m%12]+str(m//12-1)
def sample(m,layer,duration=7):
    path=SAMPLES/f'{name(m)}v{layer}.wav'
    sr,x=wavfile.read(path);assert sr==SR
    x=x.astype(float)/32768
    if x.ndim==2:x=x.mean(axis=1)
    return align(x,int(duration*SR))[0]
class Patch(ct.Structure):
    _fields_=[('tuning',(ct.c_float*2)*9),('envelope',(((ct.c_ubyte*10)*64)*2)*9),('phase',((ct.c_ubyte*64)*2)*9)]
class Voice(ct.Structure):
    _fields_=[('count',ct.c_int),('released',ct.c_int),('age',ct.c_int),('sr',ct.c_double),('release_gain',ct.c_double),('release_rate',ct.c_double),('re',(ct.c_double*2)*64),('im',(ct.c_double*2)*64),('cr',(ct.c_double*2)*64),('ci',(ct.c_double*2)*64),('amplitude',(ct.c_double*10)*64)]
def identify_tuning(x,midi):
    f0=440*2**((midi-69)/12)
    segment=x[int(.08*SR):int(2.8*SR)]
    nfft=2**19
    spec=np.abs(np.fft.rfft(segment*np.hanning(len(segment)),nfft))
    freqs=np.fft.rfftfreq(nfft,1/SR)
    h=np.arange(1,min(22,int(13000/f0))+1)
    def error(p):
        f1=f0*2**(p[0]/1200); B=10**p[1]
        f=f1*h*np.sqrt((1+B*h*h)/(1+B))
        power=np.interp(f,freqs,spec)
        return -np.mean(np.log(power+spec.max()*1e-5)/np.sqrt(h))
    fit=optimize.differential_evolution(error,[(-28,28),(-6,-1.6)],seed=midi,popsize=12,maxiter=70,tol=1e-7)
    f1=f0*2**(fit.x[0]/1200);B=10**fit.x[1]
    return f1/f0,B

def identify_envelopes(x,midi,tuning):
    f1=440*2**((midi-69)/12)*tuning[0];B=tuning[1]
    env=np.zeros((64,10)); phases=np.zeros(64)
    t=np.arange(len(x))/SR
    for k in range(64):
        h=k+1;f=f1*h*np.sqrt((1+B*h*h)/(1+B))
        if f>=SR*.44:continue
        # Complex demodulation isolates a partial cluster, retaining its envelope.
        width=max(.012,2.5/f1); win=signal.windows.hann(int(width*SR)|1);win/=win.sum()
        carrier=x*np.exp(-2j*np.pi*f*t)
        base=signal.fftconvolve(carrier,win,mode='same')*2
        envelope=np.abs(base)
        for j,when in enumerate(TIMES):
            center=int(when*SR);radius=int(max(.008,min(.1,when*.07))*SR)
            env[k,j]=np.sqrt(np.mean(envelope[max(0,center-radius):min(len(x),center+radius+1)]**2))
        # Keep late envelope non-growing and prevent fitting the recording noise floor.
        peak=env[k].max()
        env[k,env[k]<max(2e-6,peak*.002)]=1e-6
        env[k,-1]=min(env[k,-1],env[k,-2])
        phases[k]=np.angle(np.mean(base[int(.025*SR):int(.08*SR)]))%(2*np.pi)
    return np.round(np.clip((20*np.log10(np.maximum(env,1e-6))+120)*2,0,255)).astype(np.uint8),np.round(phases*256/(2*np.pi)).astype(np.uint8)

def main():
    for d in [OUT,BUILD,PUBLIC/'audio']:d.mkdir(parents=True,exist_ok=True)
    tuning=[];envelopes=[];phases=[];hashes={}
    for midi in ANCHORS:
        pitch=identify_tuning(sample(int(midi),13),int(midi)); tuning.append(pitch)
        es=[];ps=[]
        for layer in LAYERS:
            x=sample(int(midi),layer);e,p=identify_envelopes(x,int(midi),pitch);es.append(e);ps.append(p)
            path=SAMPLES/f'{name(midi)}v{layer}.wav';hashes[path.name]=hashlib.sha256(path.read_bytes()).hexdigest()
        envelopes.append(es);phases.append(ps)
        print('Identified',name(midi),'cents',round(1200*np.log2(pitch[0]),2),'B',round(pitch[1],6),flush=True)
    tuning=np.array(tuning,np.float32);envelopes=np.array(envelopes,np.uint8);phases=np.array(phases,np.uint8)
    patch=Patch();ct.memmove(ct.addressof(patch.tuning),tuning.ctypes.data,tuning.nbytes);ct.memmove(ct.addressof(patch.envelope),envelopes.ctypes.data,envelopes.nbytes);ct.memmove(ct.addressof(patch.phase),phases.ctypes.data,phases.nbytes)
    (OUT/'patch.bin').write_bytes(bytes(patch))
    def c_array(a):
        if a.ndim==1:return '{'+','.join(str(int(x)) for x in a)+'}'
        return '{\n'+',\n'.join(c_array(v) for v in a)+'\n}'
    header='#include "../../src/core/pf_partial.h"\nstatic const pf_partial_patch pf_partial_experiment={\n{'+','.join('{'+','.join(f'{float(x):.9e}f' for x in row)+'}' for row in tuning)+'},\n'+c_array(envelopes)+',\n'+c_array(phases)+'\n};\n'
    (OUT/'patch.h').write_text(header)
    subprocess.run(['cc','-O3','-std=c99','-dynamiclib','src/core/pf_partial.c','-o',str(BUILD/'partial.dylib')],cwd=ROOT,check=True)
    lib=ct.CDLL(str(BUILD/'partial.dylib'));lib.pf_partial_init.argtypes=[ct.POINTER(Voice),ct.POINTER(Patch),ct.c_double,ct.c_double,ct.c_double]
    lib.pf_partial_process.argtypes=[ct.POINTER(Voice),ct.POINTER(ct.c_float),ct.c_int];lib.pf_partial_release.argtypes=[ct.POINTER(Voice)]
    def render(midi,velocity,seconds=6,hold=None,block=4096):
        voice=Voice();lib.pf_partial_init(ct.byref(voice),ct.byref(patch),SR,float(midi),float(velocity))
        x=np.zeros(int(seconds*SR),np.float32);off=len(x) if hold is None else min(len(x),round(hold*SR));pos=0
        while pos<len(x):
            if pos==off:lib.pf_partial_release(ct.byref(voice))
            n=min(block,len(x)-pos,off-pos if pos<off else len(x)-pos)
            lib.pf_partial_process(ct.byref(voice),x[pos:].ctypes.data_as(ct.POINTER(ct.c_float)),n);pos+=n
        assert np.isfinite(x).all();return x.astype(float)
    def write_trial(id,note,dynamic,split,audio,duration,description):
        # Whole-passage matching keeps musical dynamics, including different velocities.
        audio={k:x/rms(x) for k,x in audio.items()};gain=min(.1,.9/max(np.abs(x).max() for x in audio.values()))
        urls={}
        for key,x in audio.items():
            x=x*gain;x[-int(.05*SR):]*=np.linspace(1,0,int(.05*SR));assert np.max(np.abs(x))<=.901
            f=f'partial-{id}-{key}.wav';wavfile.write(PUBLIC/'audio'/f,SR,np.round(x*32767).astype(np.int16));urls[key]='/audio/'+f
        return dict(id=id,note=note,dynamic=dynamic,split=split,duration=duration,audio=urls,description=description)
    clips=[];metrics=[]
    for midi in [45,57,69,81]:
        for layer,vel in [(6,48/127),(13,100/127)]:
            key=f'{name(midi)}-v{layer}';ref=sample(midi,layer,6);candidate=render(midi,vel)
            path=ROOT/'build/body-fit'/f'{key}.f32'
            original=np.fromfile(path,np.float32).reshape(-1,2);_,offset=align(original[:,0]);current=np.pad(original[offset:,1],(0,offset))
            pr=spectrum(normalized(ref));weights=np.sqrt(pr);weights/=weights.sum()
            def weighted(x):return float(np.sum(weights*np.abs(10*np.log10((spectrum(normalized(x))+1e-10)/(pr+1e-10)))))
            metrics.append(dict(id=key,current=spectral_error(current,ref),candidate=spectral_error(candidate,ref),current_energy_weighted_db=weighted(current),fitted_energy_weighted_db=weighted(candidate)))
            clips.append(write_trial(key,name(midi),'Soft' if layer==6 else 'Loud','held-out',dict(current=current,fitted=candidate,ref=ref),6,'Unseen A-note; tonal parameters interpolated between C and F♯ anchors.'))
    # Original four-bar A-minor study, 104 BPM. Same score and note release in all paths.
    beat=60/104; events=[]
    melody=[[76,74,72,71,69,72,76,74],[72,76,79,76,74,72,71,67],[69,72,77,76,74,72,69,67],[71,74,76,74,72,71,69,69]]
    harmonies=[(45,[57,60,64]),(48,[55,60,64]),(41,[53,57,60]),(40,[52,56,59])]
    for bar in range(4):
        base=bar*4*beat;bass,chord=harmonies[bar]
        events.append(dict(note=bass,start=base,hold=3.8*beat,velocity=70))
        for j,note in enumerate(chord):events.append(dict(note=note,start=base+.012*j,hold=1.7*beat,velocity=54+4*j))
        for j,note in enumerate(chord):events.append(dict(note=note,start=base+2*beat+.009*j,hold=1.65*beat,velocity=62+3*j))
        for j,note in enumerate(melody[bar]):events.append(dict(note=note,start=base+j*.5*beat+.008*(j%2),hold=.43*beat if j<7 else .7*beat,velocity=[83,65,74,68,80,67,88,71][j]-(5 if bar==2 else 0)))
    # Cadential chord, then hear the release tail.
    for j,note in enumerate([45,57,60,64,69]):events.append(dict(note=note,start=16*beat+.012*j,hold=1.3,velocity=65+(8 if j==4 else 0)))
    duration=13; length=duration*SR;music={k:np.zeros(length) for k in ['current','fitted','ref']}
    cache={}
    for ev in events:
        midi=ev['note'];vel=ev['velocity']/127;seconds=min(6,duration-ev['start']);n=int(seconds*SR);start=round(ev['start']*SR);hold=ev['hold'];release=np.ones(n);off=round(hold*SR);release[off:]=np.exp(-6.907755*np.arange(n-off)/(SR*.24))
        candidate=render(midi,vel,seconds,hold)
        cache_key=(midi,ev['velocity'])
        if cache_key not in cache:
            f=BUILD/f'old-{midi}-{ev["velocity"]}.f32'
            subprocess.run([str(ROOT/'build/body-fit/bodyrender'),str(midi),str(vel),'6',str(f)],check=True)
            x=np.fromfile(f,np.float32).reshape(-1,2);_,offset=align(x[:,0]);cache[cache_key]=np.pad(x[offset:,1],(0,offset))
        current=cache[cache_key][:n]*release
        # Reference is an ordinary sampled-piano reconstruction, not a performance recording.
        nearest=min(range(36,85,3),key=lambda p:abs(p-midi));ratio=2**((midi-nearest)/12)
        v=(ev['velocity']-48)/52;v=np.clip(v,0,1)
        ref=(1-v)*sample(nearest,6)+v*sample(nearest,13)
        ref=signal.resample(ref,round(len(ref)/ratio))[:n]*release
        for key,x in [('current',current),('fitted',candidate),('ref',ref)]:music[key][start:start+min(n,length-start)]+=x[:min(n,length-start)]
    clips.insert(0,write_trial('study','Small hours','Mixed','music',music,duration,'Four-bar A-minor study · 104 BPM · melody, chords, bass and releases.'))
    (OUT/'score.json').write_text(json.dumps(dict(title='Small hours',bpm=104,duration=duration,events=events),indent=2)+'\n')
    # Meaningful kernel invariants: block boundaries and release continuity.
    a=render(69,.65,1,hold=.5,block=257);b=render(69,.65,1,hold=.5,block=4096)
    assert np.array_equal(a,b)
    for midi in [21,36,60,84,108]:
        for vel in [.01,1]:render(midi,vel,.25,hold=.12)
    report=dict(paper='Simionato, Fasciani & Holm (2024), Physics-informed Differentiable Method for Piano Modeling',adaptation='Direct offline identification, quantized modal envelopes, two detuned branches; no LSTM, no sampled attack, no nonlinear hammer solver.',training_notes=[name(m) for m in ANCHORS],training_layers=LAYERS,held_out_notes=['A2','A3','A4','A5'],patch_bytes=len(bytes(patch)),compressed_patch_bytes=len(zlib.compress(bytes(patch),9)),metrics=metrics,mean_current=float(np.mean([m['current'] for m in metrics])),mean_candidate=float(np.mean([m['candidate'] for m in metrics])),sample_hashes=hashes,source_hash=hashlib.sha256((ROOT/'src/core/pf_partial.c').read_bytes()).hexdigest(),block_equivalence=True)
    report['energy_weighted_current_db']=float(np.mean([m['current_energy_weighted_db'] for m in metrics]))
    report['energy_weighted_candidate_db']=float(np.mean([m['fitted_energy_weighted_db'] for m in metrics]))
    (OUT/'report.json').write_text(json.dumps(report,indent=2)+'\n')
    (PUBLIC/'partial-audition.json').write_text(json.dumps(dict(clips=clips,sections=64,attribution='Salamander Grand Piano by Alexander Holm, CC BY 3.0. Musical reference assembled with pitch shifts, velocity interpolation, and release envelopes.'),indent=2)+'\n')
    print(json.dumps({k:v for k,v in report.items() if k not in ['sample_hashes','metrics']},indent=2),flush=True)
if __name__=='__main__':main()
