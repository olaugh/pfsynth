"""Fit one global radiation correction, export C coefficients and A/B assets.

Run with build/body-venv/bin/python tools/body_audition.py from any directory.
Requires numpy/scipy. Reference data stays in calib; only short attributed,
mono, level-matched excerpts go to the audition site. No fitting to holdouts.
"""
from pathlib import Path
import ctypes as ct
import hashlib
import json
import subprocess
import zlib
import numpy as np
from scipy import signal, optimize
from scipy.io import wavfile

ROOT = Path(__file__).resolve().parents[1]
BUILD = ROOT / 'build/body-fit'
OUT = ROOT / 'experiments/body-fit'
PUBLIC = ROOT / 'audition/public'
SAMPLES = ROOT / 'calib/SalamanderGrandPianoV3_44.1khz16bit/44.1khz16bit'
SR, SECONDS, SECTIONS = 44100, 6, 32
N = SR * SECONDS
FREQ = np.geomspace(45, 14000, 256)

def run(args):
    subprocess.run([str(a) for a in args], cwd=ROOT, check=True)

def rms(x):
    return np.sqrt(np.mean(np.asarray(x, dtype=np.float64)**2)) + 1e-15

def align(x, length=N):
    # Detect direct onset rather than the body peak. Preserve a 5 ms lead-in.
    hit = np.flatnonzero(np.abs(x) > np.max(np.abs(x)) * 0.01)
    offset = max(0, int(hit[0]) - int(.005 * SR)) if len(hit) else 0
    return np.pad(x[offset:offset+length], (0, max(0, length-len(x[offset:offset+length])))), offset

def spectrum(x, start=0, end=3):
    a = np.asarray(x[int(start*SR):int(end*SR)], dtype=np.float64)
    freq, power = signal.welch(a, SR, nperseg=min(8192, len(a)), scaling='spectrum')
    # Smooth power in log frequency; avoids fitting narrow detuning differences.
    weights = np.exp(-0.5*(np.log(np.maximum(freq[None,:], 1)/FREQ[:,None])/.06)**2)
    return weights @ power / np.maximum(weights.sum(axis=1), 1e-12)

def normalized(x):
    return x / rms(x[:2*SR])

def spectral_error(x, ref, start=0, end=3):
    px, pr = spectrum(normalized(x), start, end), spectrum(normalized(ref), start, end)
    mask = pr > max(pr.max()*1e-5, 1e-12)
    return float(np.mean(np.abs(10*np.log10((px[mask]+1e-10)/(pr[mask]+1e-10)))))

class Params(ct.Structure):
    _fields_ = [('count',ct.c_int),('direct',ct.c_float),('section',(ct.c_float*4)*64)]

class Body(ct.Structure):
    _fields_ = [('params',Params),('z1',ct.c_double*64),('z2',ct.c_double*64)]

def main():
    for d in [BUILD, OUT, PUBLIC/'audio']:
        d.mkdir(parents=True, exist_ok=True)
    run(['cc','-O3','-std=c99','src/core/pf_string.c','src/core/pf_board.c',
         'src/host/bodyrender.c','-lm','-o',BUILD/'bodyrender'])
    run(['cc','-O2','-std=c99','-dynamiclib','src/core/pf_bodyfit.c','-o',BUILD/'bodyfit.dylib'])
    rows = []
    # Entire A-note family is held out, including the familiar A4 test note.
    for split, pitches in [('fit',[('C2',36),('C3',48),('C4',60),('C5',72),('C6',84)]),
                           ('held-out',[('A2',45),('A3',57),('A4',69),('A5',81),('A6',93)])]:
        for note, midi in pitches:
            for layer, vel, dynamic in [(6,48/127,'Soft'),(13,100/127,'Loud')]:
                key = f'{note}-v{layer}'
                path = BUILD/(key+'.f32')
                run([BUILD/'bodyrender',midi,vel,SECONDS,path])
                data = np.fromfile(path,dtype=np.float32).reshape(-1,2)
                assert len(data)==N and np.isfinite(data).all()
                src, offset = align(data[:,0])
                current = np.pad(data[offset:,1],(0,offset))
                sample_path = SAMPLES/f'{note}v{layer}.wav'
                sr, ref = wavfile.read(sample_path)
                assert sr==SR and ref.dtype==np.int16
                ref = ref.astype(np.float64)/32768
                if ref.ndim==2: ref=ref.mean(axis=1)
                ref, ref_offset = align(ref)
                rows.append(dict(id=key,note=note,midi=midi,velocity=vel,dynamic=dynamic,
                                 split=split,src=src,current=current,ref=ref,
                                 source_hash=hashlib.sha256(sample_path.read_bytes()).hexdigest(),
                                 onset_offset_samples=ref_offset))
                print('Rendered',key,split,flush=True)

    # Fit log-magnitude shape and account for each note's level normalization.
    # Use reference-supported bands, even when the source is weak: otherwise
    # the target estimator simply ignores the deficits we are trying to fix.
    knots = np.geomspace(FREQ[0], FREQ[-1], 24)
    interp = np.column_stack([np.interp(np.log(FREQ),np.log(knots),v) for v in np.eye(len(knots))])
    training = []
    for r in rows:
        if r['split'] != 'fit': continue
        src, ref = normalized(r['src']), normalized(r['ref'])
        ps, pr = spectrum(src), spectrum(ref)
        mask = pr > pr.max()*1e-5
        ef, ep = signal.welch(src[:2*SR], SR, nperseg=8192, scaling='spectrum')
        ep /= ep.sum()
        training.append((10*np.log10(ps+1e-10),10*np.log10(pr+1e-10),mask,ef,ep))
    def residual(values):
        curve = interp@values
        result = []
        for source_db, ref_db, mask, ef, ep in training:
            energy_gain = np.sum(ep*10**(np.interp(ef,FREQ,curve)/10))
            error = source_db+curve-10*np.log10(energy_gain)-ref_db
            result.extend(error[mask]/np.sqrt(mask.sum()))
        result.extend(np.diff(values,2)*.35)
        result.extend(values*.015)
        return result
    solution = optimize.least_squares(residual,np.zeros(len(knots)),bounds=(-24,24),
        max_nfev=150,ftol=1e-8,xtol=1e-8,gtol=1e-8)
    target_db = interp@solution.x
    amplitude = 10**(target_db/20)

    # Fixed-pole parallel sections, Bank-style real weighted LS numerator fit.
    # Bandwidths track adjacent log-spaced pole frequency distances.
    poles = np.geomspace(40,16000,SECTIONS)
    theta = 2*np.pi*poles/SR
    bandwidth = np.gradient(theta)
    radius = np.exp(-bandwidth/2)
    a1,a2 = -2*radius*np.cos(theta),radius**2
    z = np.exp(-2j*np.pi*FREQ/SR)
    den = 1+a1[None,:]*z[:,None]+a2[None,:]*z[:,None]**2
    basis = np.empty((len(FREQ),2*SECTIONS+1),dtype=complex)
    basis[:,0::2][:,:SECTIONS] = (1-radius)[None,:]/den
    basis[:,1::2] = z[:,None]*(1-radius)[None,:]/den
    basis[:,-1] = 1
    # Start with minimum-phase target obtained from the real cepstrum.
    grid = np.linspace(0,SR/2,8193)
    logmag = np.interp(grid,FREQ,np.log(amplitude))
    cep = np.fft.irfft(logmag)
    mincep = np.zeros_like(cep); mincep[0]=cep[0]
    mincep[1:len(cep)//2]=2*cep[1:len(cep)//2]; mincep[len(cep)//2]=cep[len(cep)//2]
    h = np.exp(np.fft.rfft(mincep))
    phase = np.interp(FREQ,grid,np.unwrap(np.angle(h)))
    w = 1/np.maximum(amplitude,.1)
    design = np.concatenate([basis.real*w[:,None],basis.imag*w[:,None]])
    # Ridge on normalized basis limits large canceling numerator coefficients.
    design = np.vstack([design, np.eye(design.shape[1])*.02])
    for _ in range(15):
        goal = amplitude*np.exp(1j*phase)
        rhs = np.r_[goal.real*w,goal.imag*w,np.zeros(design.shape[1])]
        coeff = np.linalg.lstsq(design,rhs,rcond=1e-9)[0]
        response = basis@coeff
        phase = np.angle(response)
    sections = np.column_stack([coeff[:-1:2]*(1-radius),coeff[1:-1:2]*(1-radius),a1,a2]).astype(np.float32)
    direct = np.float32(coeff[-1])
    qroots=np.array([np.roots([1,*s[2:]]) for s in sections])
    assert np.max(np.abs(qroots))<1
    params=Params(); params.count=SECTIONS; params.direct=float(direct)
    for k, s in enumerate(sections):
        for j, v in enumerate(s): params.section[k][j]=float(v)
    lib=ct.CDLL(str(BUILD/'bodyfit.dylib'))
    lib.pf_bodyfit_init.argtypes=[ct.POINTER(Body),ct.POINTER(Params)]
    ptr=ct.POINTER(ct.c_float)
    lib.pf_bodyfit_process.argtypes=[ct.POINTER(Body),ptr,ptr,ct.c_int]
    max_difference=0
    def apply(x):
        nonlocal max_difference
        x=np.ascontiguousarray(x,dtype=np.float32)
        result=np.empty_like(x); b=Body(); lib.pf_bodyfit_init(ct.byref(b),ct.byref(params))
        # Irregular block lengths exercise state continuity as well as samples.
        for pos in range(0,len(x),257):
            n=min(257,len(x)-pos)
            lib.pf_bodyfit_process(ct.byref(b),x[pos:].ctypes.data_as(ptr),result[pos:].ctypes.data_as(ptr),n)
        expected=np.asarray(x,dtype=np.float64)*direct
        for s in sections:
            expected+=signal.lfilter(s[:2],[1,*s[2:]],x)
        difference=float(np.max(np.abs(expected-result)))
        max_difference=max(max_difference,difference)
        assert difference < max(1e-7,float(np.max(np.abs(expected)))*2e-6)
        assert np.isfinite(result).all()
        return result.astype(np.float64)
    # Impulse must decay rather than retain an unstable residual.
    impulse=np.zeros(SR*3,dtype=np.float32); impulse[0]=1
    impulse=apply(impulse)
    assert np.max(np.abs(impulse[-SR//2:]))<1e-8

    (OUT/'coefficients.json').write_text(json.dumps(dict(sample_rate=SR,direct=float(direct),
        sections=sections.tolist(),pole_frequencies_hz=poles.tolist(),target_frequencies_hz=FREQ.tolist(),
        target_db=target_db.tolist()),indent=2)+'\n')
    header=['/* Generated by tools/body_audition.py; 44.1 kHz only. */',
            '#include "../../src/core/pf_bodyfit.h"',
            'static const pf_bodyfit_params pf_bodyfit_experiment = {',
            f'    {SECTIONS}, {float(direct):.9e}f, {{']
    header += ['        {'+', '.join(f'{float(v):.9e}f' for v in s)+'},' for s in sections]
    header += ['    }','};','']
    (OUT/'coefficients.h').write_text('\n'.join(header))
    clips=[]; metrics=[]
    for r in rows:
        r['fitted']=apply(r['src'])
        m={k:r[k] for k in ['id','split','note','dynamic']}
        for version in ['current','fitted']:
            m[version]={label:spectral_error(r[version],r['ref'],lo,hi)
                for label,lo,hi in [('overall',0,3),('attack',0,.12),('tail',2,5.8)]}
        metrics.append(m)
        # A/B/reference equal RMS over first 2 seconds. Common peak protection.
        audio={k:normalized(r[k]) for k in ['current','fitted','ref']}
        gain=min(.12,.92/max(np.max(np.abs(a)) for a in audio.values()))
        urls={}
        for k,a in audio.items():
            a=a*gain
            a[-int(.03*SR):]*=np.linspace(1,0,int(.03*SR))
            assert np.max(np.abs(a))<=.920001
            filename=f"{r['id']}-{k}.wav"
            wavfile.write(PUBLIC/'audio'/filename,SR,np.round(a*32767).astype(np.int16))
            urls[k]='/audio/'+filename
        clips.append({**{k:r[k] for k in ['id','note','midi','dynamic','split','velocity']},
                      'duration':SECONDS,'audio':urls,'rms_dbfs':float(20*np.log10(gain))})

    # Held-out phrase assembled from the same raw A-family sources, no refitting.
    phrase_length=9*SR
    phrase={k:np.zeros(phrase_length) for k in ['current','fitted','ref']}
    for key,onset,scale in [('A2-v6',0,.8),('A3-v6',.6,.7),('A4-v6',1.2,.65),
                             ('A5-v6',1.8,.55),('A4-v13',2.4,.5),('A3-v13',3,.5)]:
        r=next(r for r in rows if r['id']==key)
        # Equal per-note scale within each family; shared across phrase versions.
        for k in phrase:
            a=(r[k]/rms(r['src'][:2*SR]) if k!='ref' else normalized(r['ref']))*scale; pos=round(onset*SR)
            phrase[k][pos:pos+N]+=a
    audio={k:normalized(a) for k,a in phrase.items()}
    gain=min(.12,.92/max(np.max(np.abs(a)) for a in audio.values()))
    urls={}
    for k,a in audio.items():
        a*=gain; a[-int(.05*SR):]*=np.linspace(1,0,int(.05*SR))
        wavfile.write(PUBLIC/'audio'/f'phrase-{k}.wav',SR,np.round(a*32767).astype(np.int16))
        urls[k]=f'/audio/phrase-{k}.wav'
    clips.insert(0,dict(id='phrase',note='A-register arpeggio',midi=0,dynamic='Mixed',split='held-out',
                        duration=9,audio=urls,velocity=None,rms_dbfs=float(20*np.log10(gain))))
    summary={}
    for split in ['fit','held-out']:
        summary[split]={v:{period:float(np.mean([m[v][period] for m in metrics if m['split']==split]))
            for period in ['overall','attack','tail']} for v in ['current','fitted']}
    payload=sections.tobytes()+direct.tobytes()
    report=dict(method='32 fixed-pole parallel sections; one global magnitude correction fitted only to C notes',
        sample_rate=SR,sections=SECTIONS,coefficient_bytes=len(payload),
        coefficient_zlib_bytes=len(zlib.compress(payload)),max_pole_radius=float(np.max(np.abs(qroots))),
        max_c_scipy_difference=max_difference,summary=summary,notes=metrics,
        source_hashes={p:hashlib.sha256((ROOT/p).read_bytes()).hexdigest() for p in
            ['src/core/pf_string.c','src/core/pf_string.h','src/core/pf_board.c','src/core/pf_board.h']},
        references=[{k:r[k] for k in ['id','split','velocity','source_hash','onset_offset_samples']} for r in rows])
    (OUT/'report.json').write_text(json.dumps(report,indent=2)+'\n')
    (PUBLIC/'audition.json').write_text(json.dumps(dict(clips=clips,summary=summary,sections=SECTIONS,
        attribution='Salamander Grand Piano by Alexander Holm, CC BY 3.0. Excerpts trimmed, downmixed and level-matched.'),indent=2)+'\n')
    print(json.dumps(summary,indent=2),flush=True)

if __name__=='__main__': main()
