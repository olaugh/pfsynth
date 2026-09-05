"""Measure what the frozen tonal model leaves out at note onsets.

Decomposes Salamander anchor notes into harmonic / percussive / noise via a
median-filter HPSS (Fitzgerald 2010, Driedger et al. 2014 soft masks with margin),
the same family of separation the STN paper uses as its training target.
Compares to the frozen pf_partial render at the same anchor. Diagnostic only.
"""
from pathlib import Path
import ctypes as ct, json, subprocess, sys
import numpy as np
from scipy import signal, ndimage
from partial_audition import ROOT, BUILD, OUT, SR, Patch, Voice, name
from refsrc import sample, REF
ATT=ROOT/('experiments/attack' if REF=='sal' else 'experiments/attack-ptq')


def hpss(x, n_fft=2048, hop=256, kt=17, kf=17, margin_h=1.0, margin_p=8.0):
    """Return harmonic, percussive, noise time signals (Driedger-style soft masks)."""
    f, t, X = signal.stft(x, SR, nperseg=n_fft, noverlap=n_fft-hop, boundary='zeros', padded=True)
    S = np.abs(X)
    H = ndimage.median_filter(S, size=(1, kt), mode='reflect')
    P = ndimage.median_filter(S, size=(kf, 1), mode='reflect')
    eps = 1e-12
    mh = (H > margin_h*P).astype(float) if margin_h>1 else H**2/(H**2+P**2+eps)
    mp = (P > margin_p*H).astype(float)
    # Soft-mask harmonic (Wiener) so the residual keeps between-partial noise; hard percussive.
    def inv(M):
        _, y = signal.istft(X*M, SR, nperseg=n_fft, noverlap=n_fft-hop, boundary=True)
        return y[:len(x)]
    h = inv(mh); p = inv(mp)
    return h, p, x[:len(h)] - h - p

def load_lib():
    subprocess.run(['cc','-O3','-std=c99','-dynamiclib','src/core/pf_partial.c','-o',str(BUILD/'partial.dylib')],cwd=ROOT,check=True)
    lib=ct.CDLL(str(BUILD/'partial.dylib'))
    lib.pf_partial_init.argtypes=[ct.POINTER(Voice),ct.POINTER(Patch),ct.c_double,ct.c_double,ct.c_double]
    lib.pf_partial_process.argtypes=[ct.POINTER(Voice),ct.POINTER(ct.c_float),ct.c_int]
    lib.pf_partial_release.argtypes=[ct.POINTER(Voice)]
    return lib

def render(lib, patch, midi, velocity, seconds):
    v=Voice(); lib.pf_partial_init(ct.byref(v),ct.byref(patch),SR,float(midi),float(velocity))
    x=np.zeros(int(seconds*SR),np.float32); lib.pf_partial_process(ct.byref(v),x.ctypes.data_as(ct.POINTER(ct.c_float)),len(x))
    return x.astype(float)

def env_db(x, win=0.001):
    n=int(win*SR); k=np.ones(n)/n
    return 10*np.log10(np.convolve(x**2,k,'same')+1e-14)

def band_db(x, edges):
    X=np.abs(np.fft.rfft(x*np.hanning(len(x))))**2; f=np.fft.rfftfreq(len(x),1/SR)
    return [10*np.log10(X[(f>=a)&(f<b)].sum()+1e-14) for a,b in zip(edges[:-1],edges[1:])]

def main():
    ATT.mkdir(exist_ok=True)
    patch=Patch.from_buffer_copy((OUT/'patch.bin').read_bytes()); lib=load_lib()
    edges=[40,80,160,320,640,1280,2560,5120,10240,20000]
    out={}
    for midi in [36,48,60,72,84]:
        for layer,vel in [(6,48/127),(13,100/127)]:
            x=sample(midi,layer,3.0); y=render(lib,patch,midi,vel,3.0)
            # Level-match tonal render to the recording over 0.1-2 s (steady tonal region).
            g=np.sqrt(np.mean(x[int(.1*SR):int(2*SR)]**2)/np.mean(y[int(.1*SR):int(2*SR)]**2)); y=y*g
            h,p,nz=hpss(x)
            tot=np.sum(x**2)
            def e(s,a,b): return float(np.sum(s[int(a*SR):int(b*SR)]**2))
            rec=dict(
                energy_share=dict(harmonic=np.sum(h**2)/tot,percussive=np.sum(p**2)/tot,noise=np.sum(nz**2)/tot),
                # energy of each component in onset windows, dB relative to total energy in the same window
                onset_windows={f'{int(a*1000)}-{int(b*1000)}ms':dict(perc_db=10*np.log10(e(p,a,b)/e(x,a,b)+1e-12),noise_db=10*np.log10(e(nz,a,b)/e(x,a,b)+1e-12)) for a,b in [(0,.01),(.01,.03),(.03,.08),(.08,.2),(.2,.5),(.5,2)]},
                perc_spectrum_0_100ms_db=band_db(p[:int(.1*SR)],edges),
                noise_spectrum_0_100ms_db=band_db(nz[:int(.1*SR)],edges),
                noise_spectrum_100_500ms_db=band_db(nz[int(.1*SR):int(.5*SR)],edges),
                rec_spectrum_0_100ms_db=band_db(x[:int(.1*SR)],edges),
                model_spectrum_0_100ms_db=band_db(y[:int(.1*SR)],edges),
                rec_spectrum_100_500ms_db=band_db(x[int(.1*SR):int(.5*SR)],edges),
                model_spectrum_100_500ms_db=band_db(y[int(.1*SR):int(.5*SR)],edges),
                # onset envelope, 1 ms RMS, every 2 ms for first 60 ms
                rec_env_db=list(np.round(env_db(x)[:int(.06*SR):int(.002*SR)],1)),
                model_env_db=list(np.round(env_db(y)[:int(.06*SR):int(.002*SR)],1)),
                peak_time_ms=dict(rec=float(np.argmax(env_db(x)[:int(.2*SR)])/SR*1000),model=float(np.argmax(env_db(y)[:int(.2*SR)])/SR*1000)),
                match_gain_db=float(20*np.log10(g)))
            out[f'{name(midi)}-v{layer}']=rec
            print(f"{name(midi)} v{layer}: share H/P/N = {rec['energy_share']['harmonic']:.3f}/{rec['energy_share']['percussive']:.4f}/{rec['energy_share']['noise']:.4f}", flush=True)
            print("   onset windows:", {k:(round(v['perc_db'],1),round(v['noise_db'],1)) for k,v in rec['onset_windows'].items()})
            print("   rec  0-100ms bands:", [round(v) for v in rec['rec_spectrum_0_100ms_db']])
            print("   mdl  0-100ms bands:", [round(v) for v in rec['model_spectrum_0_100ms_db']])
            print("   perc 0-100ms bands:", [round(v) for v in rec['perc_spectrum_0_100ms_db']])
            print("   nois 0-100ms bands:", [round(v) for v in rec['noise_spectrum_0_100ms_db']])
            print("   rec  100-500 bands:", [round(v) for v in rec['rec_spectrum_100_500ms_db']])
            print("   mdl  100-500 bands:", [round(v) for v in rec['model_spectrum_100_500ms_db']])
            print("   nois 100-500 bands:", [round(v) for v in rec['noise_spectrum_100_500ms_db']])
            print("   rec env:", rec['rec_env_db'][:20]); print("   mdl env:", rec['model_env_db'][:20]); print("   peak ms:", rec['peak_time_ms'])
            # save components for listening/inspection
            if midi in (48,72) and layer==13:
                from scipy.io import wavfile
                for nm,s in [('rec',x),('model',y),('harm',h),('perc',p),('noise',nz)]:
                    wavfile.write(ATT/f'{name(midi)}-v{layer}-{nm}.wav',SR,np.clip(s/np.max(np.abs(x))*.9*32767,-32767,32767).astype(np.int16))
    (ATT/'analysis.json').write_text(json.dumps(out,indent=1,default=float))
if __name__=='__main__':main()
