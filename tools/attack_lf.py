"""Characterize the sub-fundamental (body/key thump) component of Salamander notes."""
import numpy as np, json
from scipy import signal
from partial_audition import SR, name, ROOT
from refsrc import sample, REF
ATT=ROOT/('experiments/attack' if REF=='sal' else 'experiments/attack-ptq')
def lf_part(x, f1, frac=.6):
    sos=signal.butter(8, f1*frac, 'low', fs=SR, output='sos'); return signal.sosfiltfilt(sos, x)
def env_db(x, win=.002):
    n=int(win*SR); return 10*np.log10(np.convolve(x**2,np.ones(n)/n,'same')+1e-14)
def peaks(x, a, b, fmax):
    seg=x[int(a*SR):int(b*SR)]*np.hanning(int((b-a)*SR)); n=1<<18
    X=np.abs(np.fft.rfft(seg,n)); f=np.fft.rfftfreq(n,1/SR); m=f<fmax
    X=20*np.log10(X[m]+1e-12); f=f[m]
    idx,_=signal.find_peaks(X, prominence=6, distance=int(3/(f[1]-f[0])))
    idx=idx[np.argsort(X[idx])[::-1]][:14]
    return sorted([(round(float(f[i]),1), round(float(X[i]-X.max()),1)) for i in idx])
def demod_t60(x, f, a=.02, b=.4):
    t=np.arange(len(x))/SR; w=signal.windows.hann(int(.04*SR)|1); w/=w.sum()
    e=np.abs(signal.fftconvolve(x*np.exp(-2j*np.pi*f*t), w, 'same'))
    seg=20*np.log10(e[int(a*SR):int(b*SR)]+1e-12); tt=t[int(a*SR):int(b*SR)]
    slope=np.polyfit(tt,seg,1)[0]; return round(float(-60/slope),3) if slope<0 else None
out={}
for midi in [60,66,69,72,78,84]:
    f1=440*2**((midi-69)/12)
    for layer in [6,13]:
        x=sample(midi,layer,2.0); lf=lf_part(x,f1); tonal_env=env_db(x); lfe=env_db(lf)
        onset_full=int(np.argmax(tonal_env>tonal_env[:int(.3*SR)].max()-20)); onset_lf=int(np.argmax(lfe>lfe[:int(.3*SR)].max()-20))
        rel=10*np.log10(np.sum(lf**2)/np.sum(x**2))
        windows={f'{int(a*1000)}-{int(b*1000)}':round(float(10*np.log10(np.sum(lf[int(a*SR):int(b*SR)]**2)/np.sum(x[int(a*SR):int(b*SR)]**2)+1e-12)),1) for a,b in [(0,.02),(.02,.05),(.05,.1),(.1,.2),(.2,.4),(.4,.8),(.8,1.6)]}
        pk=peaks(lf,0,.5,min(f1*.6,700)); t60={p[0]:demod_t60(lf,p[0]) for p in pk[:8]}
        lfpeak_ms=float(np.argmax(lfe[:int(.2*SR)])/SR*1000); fullpeak_ms=float(np.argmax(tonal_env[:int(.2*SR)])/SR*1000)
        rec=dict(f1=round(f1,1),lf_rel_total_db=round(float(rel),1),lf_rel_by_window_db=windows,onset_ms=dict(full=onset_full/SR*1000,lf=onset_lf/SR*1000),peak_ms=dict(full=fullpeak_ms,lf=lfpeak_ms),lf_peaks_hz_db=pk,t60_s=t60,
                 lf_env_db_every5ms=[round(float(v),1) for v in lfe[:int(.3*SR):int(.005*SR)]])
        out[f'{name(midi)}-v{layer}']=rec
        print(f"{name(midi)} v{layer} f1={f1:.0f}: LF(<{.6*f1:.0f}Hz) rel total {rel:.1f} dB; by window {windows}")
        print("   onset ms full/lf:",round(onset_full/SR*1000,1),round(onset_lf/SR*1000,1),"peak ms full/lf:",round(fullpeak_ms,1),round(lfpeak_ms,1))
        print("   LF peaks (Hz, dB rel max):",pk); print("   T60 of top LF peaks:",t60)
        print("   LF env every 5ms:",rec['lf_env_db_every5ms'][:30])
(ATT/'lf_analysis.json').write_text(json.dumps(out,indent=1,default=float))
