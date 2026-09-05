"""Closed loop: render tonal+attack at fit and held-out notes; compare band levels
in onset windows against the recordings.  Diagnostic only."""
import ctypes as ct, json, subprocess, numpy as np
from scipy.io import wavfile
from partial_audition import ROOT, BUILD, OUT, SR, Patch, Voice, name
from refsrc import sample, REF
ATT=ROOT/('experiments/attack' if REF=='sal' else 'experiments/attack-ptq')
TONAL=OUT/'patch.bin' if REF=='sal' else ROOT/'experiments/partial-piano-ptq/patch.bin'
class APatch(ct.Structure):
    _fields_=[('mode_hz',ct.c_float*64),('mode_t60',ct.c_float*64),('mode_db',ct.c_float*64),('mode_delay_ms',ct.c_float*64),('pulse_ms',ct.c_float),('pulse_cycles',ct.c_float),
              ('thump_db',(ct.c_float*2)*9),('noise_db',(ct.c_float*2)*9),('noise_ms',(ct.c_float*2)*9),('noise_hz',(ct.c_float*2)*9),('thump_mix',ct.c_float),('noise_mix',ct.c_float)]
class AVoice(ct.Structure):
    _fields_=[('modes',ct.c_int),('age',ct.c_int),('sr',ct.c_double),('a1',ct.c_double*64),('a2',ct.c_double*64),('g',ct.c_double*64),('y1',ct.c_double*64),('y2',ct.c_double*64),('delay',ct.c_int*64),('plen',ct.c_int*64),('pgain',ct.c_double*64),
              ('noise_gain',ct.c_double),('noise_rate',ct.c_double),('lp_a',ct.c_double),('lp_z1',ct.c_double),('lp_z2',ct.c_double),('rng',ct.c_uint)]
def load_apatch(thump_mix=None,noise_mix=None):
    j=json.loads((ATT/'attack.json').read_text()); p=APatch()
    for k in ['mode_hz','mode_t60','mode_db','mode_delay_ms']: getattr(p,k)[:]=[float(v) for v in j[k]]
    p.pulse_ms=j['pulse_ms']; p.pulse_cycles=j.get('pulse_cycles',0.0)
    for k in ['thump_db','noise_db','noise_ms','noise_hz']:
        for a in range(9):
            for l in range(2): getattr(p,k)[a][l]=float(j[k][a][l])
    p.thump_mix=j.get('thump_mix',1.0) if thump_mix is None else thump_mix; p.noise_mix=j.get('noise_mix',1.0) if noise_mix is None else noise_mix; return p
def libs():
    subprocess.run(['cc','-O3','-std=c99','-dynamiclib','src/core/pf_partial.c','src/core/pf_attack.c','-o',str(BUILD/'partial.dylib')],cwd=ROOT,check=True)
    lib=ct.CDLL(str(BUILD/'partial.dylib'))
    lib.pf_partial_init.argtypes=[ct.POINTER(Voice),ct.POINTER(Patch),ct.c_double,ct.c_double,ct.c_double]
    lib.pf_partial_process.argtypes=[ct.POINTER(Voice),ct.POINTER(ct.c_float),ct.c_int]; lib.pf_partial_release.argtypes=[ct.POINTER(Voice)]
    lib.pf_attack_init.argtypes=[ct.POINTER(AVoice),ct.POINTER(APatch),ct.c_double,ct.c_double,ct.c_double]
    lib.pf_attack_process.argtypes=[ct.POINTER(AVoice),ct.POINTER(ct.c_float),ct.c_int]
    return lib
def render(lib,patch,apatch,midi,vel,seconds,hold=None,attack=True,tonal=True,block=4096):
    v=Voice(); a=AVoice(); lib.pf_partial_init(ct.byref(v),ct.byref(patch),SR,float(midi),float(vel)); lib.pf_attack_init(ct.byref(a),ct.byref(apatch),SR,float(midi),float(vel))
    x=np.zeros(int(seconds*SR),np.float32); off=len(x) if hold is None else min(len(x),round(hold*SR)); pos=0
    while pos<len(x):
        if pos==off: lib.pf_partial_release(ct.byref(v))
        n=min(block,len(x)-pos,off-pos if pos<off else len(x)-pos); ptr=x[pos:].ctypes.data_as(ct.POINTER(ct.c_float))
        if tonal: lib.pf_partial_process(ct.byref(v),ptr,n)
        if attack: lib.pf_attack_process(ct.byref(a),ptr,n)
        pos+=n
    assert np.isfinite(x).all(); return x.astype(float)
def band_db(x,edges):
    X=np.abs(np.fft.rfft(x*np.hanning(len(x))))**2; f=np.fft.rfftfreq(len(x),1/SR)
    return [round(float(10*np.log10(X[(f>=a)&(f<b)].sum()+1e-14)),1) for a,b in zip(edges[:-1],edges[1:])]
if __name__=='__main__':
    lib=libs(); patch=Patch.from_buffer_copy(TONAL.read_bytes()); ap=load_apatch()
    edges=[25,50,100,200,400,800,1600,3200,6400,12800]
    # block-equivalence + finite checks for the attack kernel
    a=render(lib,patch,ap,69,.65,1,hold=.5,block=257); b=render(lib,patch,ap,69,.65,1,hold=.5,block=4096); assert np.array_equal(a,b)
    for m in [21,36,60,84,108]:
        for vl in [.01,1.0]: render(lib,patch,ap,m,vl,.3,hold=.1)
    print('kernel checks ok')
    for midi in [48,60,69,72,81,84]:
        for layer,vel in [(6,48/127),(13,100/127)]:
            x=sample(midi,layer,1.0); y=render(lib,patch,ap,midi,vel,1.0); y0=render(lib,patch,ap,midi,vel,1.0,attack=False)
            tag='held-out' if midi in (69,81) else 'fit'
            for (s,e) in [(0,.03),(.03,.1),(.1,.5)]:
                sl=slice(int(s*SR),int(e*SR))
                print(f'{name(midi)}v{layer} {tag} {int(s*1000):3d}-{int(e*1000):3d}ms rec {band_db(x[sl],edges)}')
                print(f'{"":>18} {"":>10} +att {band_db(y[sl],edges)}')
                print(f'{"":>18} {"":>10} tonal {band_db(y0[sl],edges)}')
            if midi in (60,72,84) and layer==13:
                for nm,s in [('rec',x),('tonal',y0),('tonal+attack',y),('attack-only',y-y0)]:
                    wavfile.write(ATT/f'{name(midi)}-v{layer}-{nm}.wav',SR,np.clip(s*.9/np.max(np.abs(x))*32767,-32767,32767).astype(np.int16))
