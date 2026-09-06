"""Fit the compact onset model (pf_attack) to Salamander C/F# anchors; A notes held out.

Three generated parts, all struck by one short pulse per note whose level depends
on register and velocity:
  * slow "room" modes  (25-450 Hz, 1-2.5 s)  fitted from the sub-fundamental spectrum
  * fast "knock" modes (70-1000 Hz, ~0.2 s)   fitted from the early excess the slow
    modes + tonal model leave in the first 60 ms (only observable below each note's
    fundamental, so the upper bands come from the treble anchors)
  * hiss: filtered-noise burst above ~1 kHz from the between-partial floor
Writes experiments/attack/attack.json and patch_attack.h.
"""
import json, numpy as np
from pathlib import Path
from scipy import signal, ndimage
from partial_audition import SR, name, ROOT, ANCHORS, Patch
from refsrc import sample, REF
import attack_verify as av
ATT=av.ATT; TONAL=av.TONAL; SLOW=24; FAST=40; PULSE_MS=0.5; PULSE_CYCLES=0.5   # 24 slow + 23 midpoints + 17 upper knock modes to 2.8 kHz = 64
# Passage-level calibration: lone onsets inside the Beethoven passage (velocities 40-77) measured
# +4 dB at 40-160 Hz against Pianoteq's in-passage thumps after the isolated-note fit; trim the bank.
PASSAGE_TRIM_DB=-3.0
FIT=[48,54,60,66,72,78,84,90,96,102,108]; HOLD=[69,81,93,105]

def hires(x,a,b,n=1<<18):
    seg=x[int(a*SR):int(b*SR)]; X=np.abs(np.fft.rfft(seg*np.hanning(len(seg)),n)); return np.fft.rfftfreq(n,1/SR),X
def demod(x,f,win_s):
    t=np.arange(len(x))/SR; w=signal.windows.hann(int(win_s*SR)|1); w/=w.sum()
    return 2*np.abs(signal.fftconvolve(x*np.exp(-2j*np.pi*f*t),w,'same'))
def f1_of(m): return 440*2**((m-69)/12)
def pulse_sig(n):
    L=max(1,int(PULSE_MS*.001*SR)); p=np.zeros(n); p[:L]=(.5-.5*np.cos(2*np.pi*(np.arange(L)+.5)/L))*2/L; return p
def synth_bank(hz,t60,db,level_db,n,delay_ms=None):
    """Mirror of pf_attack: per-mode delayed unit-area Hann pulse (>= PULSE_MS and >= PULSE_CYCLES periods
    of the mode) -> resonators g*sin(w)/(1-2Rcos(w)z^-1+R^2 z^-2), alternating signs."""
    y=np.zeros(n); L0=max(1,int(PULSE_MS*.001*SR))
    if delay_ms is None: delay_ms=np.zeros(len(hz))
    for k,(f,t,d,dl) in enumerate(zip(hz,t60,db,delay_ms)):
        L=max(L0,int(PULSE_CYCLES*SR/f)); x=np.zeros(n); s0=int(dl*.001*SR); x[s0:s0+L]=(.5-.5*np.cos(2*np.pi*(np.arange(L)+.5)/L))*2/L*10**(level_db/20)
        R=np.exp(-6.907755/(t*SR)); w=2*np.pi*f/SR
        y+=signal.lfilter([(-1)**k*10**(d/20)*np.sin(w)],[1,-2*R*np.cos(w),R*R],x)
    return y
def band_power(x,a,b,edges):
    seg=x[int(a*SR):int(b*SR)]; w=np.hanning(len(seg)); X=np.abs(np.fft.rfft(seg*w,1<<15))**2; f=np.fft.rfftfreq(1<<15,1/SR)
    return np.array([X[(f>=lo)&(f<hi)].sum() for lo,hi in zip(edges[:-1],edges[1:])])/(np.sum(w**2)*(1<<15)/2)

def fit_slow(notes):
    f,_=hires(np.zeros(SR),0,1); acc=np.zeros_like(f); cnt=np.zeros_like(f)
    for midi,layer,x in notes:
        _,X=hires(x,0,1.0); db=20*np.log10(X+1e-9); m=(f>=22)&(f<.6*f1_of(midi))
        db-=np.median(db[m]); acc[m]+=db[m]; cnt[m]+=1
    valid=(cnt>0)&(f>=25)&(f<450); avg=np.where(valid,acc/np.maximum(cnt,1),np.nan); avg=np.where(valid,avg,np.nanmedian(avg))
    base=ndimage.uniform_filter1d(avg,int(40/(f[1]-f[0]))); prom=np.where(valid,avg-base,-1e9)
    idx,_=signal.find_peaks(prom,prominence=4,distance=int(3.5/(f[1]-f[0]))); idx=idx[(f[idx]>=27)&(f[idx]<445)]
    idx=np.sort(idx[np.argsort(prom[idx])[::-1]][:SLOW]); hz=f[idx]
    T60=np.zeros(len(hz)); A=np.full((len(notes),len(hz)),np.nan); D=np.zeros(len(hz)); t=np.arange(len(notes[0][2]))/SR; sl=slice(int(.06*SR),int(.7*SR))
    onsets=[int(np.flatnonzero(np.abs(x[:int(.03*SR)])>np.abs(x).max()*.02)[0]) for _,_,x in notes]
    for k,fm in enumerate(hz):
        slopes=[];peaks=[];lines=[]
        for i,(midi,layer,x) in enumerate(notes):
            if fm>=.6*f1_of(midi):continue
            e=20*np.log10(demod(x,fm,.12)[sl]+1e-9); p=np.polyfit(t[sl],e,1)
            if p[0]<0:slopes.append(p[0])
            lines.append((i,p))
            # strike delay: when this mode's envelope peaks after the tone onset (20 ms demod window)
            fine=demod(x,fm,.02)[onsets[i]:onsets[i]+int(.08*SR)]; peaks.append(np.argmax(fine)/SR)
        T60[k]=np.clip(-60/np.median(slopes),.25,2.5) if slopes else 1.0
        D[k]=float(np.clip(np.median(peaks)-.002,0,.03)) if peaks else 0.0
        for i,p in lines: A[i,k]=np.polyval(p,D[k]+onsets[i]/SR)  # amplitude at the strike moment
    W=np.nanmean(A-np.nanmean(A,axis=1,keepdims=True),axis=0); W-=W.max(); G=np.nanmean(A-W[None,:],axis=1)
    return hz,T60,W,G,D,A   # pulse coloration is absorbed by the closed-loop energy pass; A = per-note mode amplitudes (dB, NaN where unobservable)

def write_header(path,j):
    """C initializer in pf_attack_patch field order: modes (hz, t60, db, delay), pulse_ms, pulse_cycles, tables, mixes."""
    def cf(v):
        t=f'{float(v):.6g}'; return t+('f' if ('.' in t or 'e' in t) else '.0f')
    def arr(a): return '{'+','.join(cf(v) for v in a)+'}'
    def arr2(a): return '{'+','.join(arr(r) for r in a)+'}'
    Path(path).write_text('#include "../../src/core/pf_attack.h"\nstatic const pf_attack_patch pf_attack_experiment={\n'+arr(j['mode_hz'])+',\n'+arr(j['mode_t60'])+',\n'+arr(j['mode_db'])+',\n'+arr(j['mode_delay_ms'])+',\n'+arr2(j.get('mode_db_key',[[0]*64]*30))+',\n'+cf(j['pulse_ms'])+','+cf(j['pulse_cycles'])+',\n'+arr2(j['thump_db'])+',\n'+arr2(j['noise_db'])+',\n'+arr2(j['noise_ms'])+',\n'+arr2(j['noise_hz'])+',\n'+cf(j['thump_mix'])+','+cf(j['noise_mix'])+','+cf(j.get('slow_mix',1.0))+','+cf(j.get('knock_mix',1.0))+'\n};\n')
def main():
    ATT.mkdir(exist_ok=True)
    notes=[(m,l,sample(m,l,2.5)) for m in FIT for l in (6,13)]; hold=[(m,l,sample(m,l,2.5)) for m in HOLD for l in (6,13)]
    hz_s,t60_s,w_s,G,d_s,A_s=fit_slow(notes)
    R_slow=np.nan_to_num(A_s-G[:,None]-w_s[None,:])   # per-note residual weights of the slow modes
    print('slow modes:',np.round(hz_s,1)); print('slow T60:',np.round(t60_s,2)); print('slow weights dB:',np.round(w_s,1)); print('slow delays ms:',np.round(d_s*1000,1))
    print('note gains dB:',{f'{name(m)}v{l}':round(float(g),1) for (m,l,_),g in zip(notes,G)})
    # ---- fast knock bank from the early excess (rec - tonal - slow bank), window 0-60 ms
    lib=av.libs(); patch=Patch.from_buffer_copy(TONAL.read_bytes())
    # Fast "knock" modes at the log-midpoints between consecutive slow modes (never coincident with
    # them), extended above the slow range up to 1 kHz.  The final bank is the union sorted by frequency.
    hz_f=np.concatenate([np.sqrt(hz_s[:-1]*hz_s[1:]),np.geomspace(hz_s[-1]*1.06,2800,max(4,64-len(hz_s)-(len(hz_s)-1)))])   # knock content reaches ~2.5 kHz under the top octave
    hz_f=hz_f[(hz_f>=45)]; FASTN=len(hz_f)
    order=np.argsort(np.concatenate([hz_s,hz_f])); nS=len(hz_s)
    def combined(t60_fast,w_fast,w_slow=None):
        hz=np.concatenate([hz_s,hz_f])[order]; t60=np.concatenate([t60_s,[t60_fast]*FASTN])[order]
        db=np.concatenate([w_s if w_slow is None else w_slow,w_fast])[order]; dl=np.concatenate([d_s*1000,np.zeros(FASTN)])[order]; return hz,t60,db,dl
    edges=np.array([56,70,88,111,140,176,222,280,353,445,561,707,891,1122,1414,1782,2245,2828]); centers=np.sqrt(edges[:-1]*edges[1:])
    n=int(.5*SR); tonal={}
    for (midi,layer,x) in notes+hold:
        v=av.Voice(); lib.pf_partial_init(v,patch,SR,float(midi),[48/127,100/127][layer==13]); y=np.zeros(n,np.float32); lib.pf_partial_process(v,y.ctypes.data_as(av.ct.POINTER(av.ct.c_float)),n); tonal[(midi,layer)]=y.astype(float)
    def excess_weights(t60_fast,win=(0,.06)):
        Wf=np.full((len(notes),len(centers)),np.nan)
        for i,(midi,layer,x) in enumerate(notes):
            hz_,t_,db_,dl_=combined(t60_fast,np.full(FASTN,-200.0)); model=tonal[(midi,layer)]+synth_bank(hz_,t_,db_,G[i],n,dl_)
            hz_,t_,db_,dl_=combined(t60_fast,np.zeros(FASTN),np.full(nS,-200.0)); unit=synth_bank(hz_,t_,db_,G[i],n,dl_)
            pr,pm,pu=band_power(x,*win,edges),band_power(model,*win,edges),band_power(unit,*win,edges)
            ok=edges[1:]<.7*f1_of(midi); ex=np.maximum(pr-pm,pr*.02)
            Wf[i,ok]=10*np.log10(ex[ok]/pu[ok])
        wb=np.nanmean(Wf,axis=0)
        # bands no note can observe (above the highest anchor's fundamental): hold the last measured value
        for b in range(len(wb)):
            if np.isnan(wb[b]): wb[b]=wb[b-1]
        return wb,Wf
    best=None
    for t60_fast in [.1,.14,.18,.25,.35]:
        wb,_=excess_weights(t60_fast); wmode=np.interp(np.log(hz_f),np.log(centers),wb)
        err=[]
        for i,(midi,layer,x) in enumerate(notes):
            hz_,t_,db_,dl_=combined(t60_fast,wmode); model=tonal[(midi,layer)]+synth_bank(hz_,t_,db_,G[i],n,dl_)
            for win in [(.06,.15),(.15,.25)]:
                pr,pm=band_power(x,*win,edges),band_power(model,*win,edges); ok=edges[1:]<.7*f1_of(midi)
                err.append(np.mean(np.abs(10*np.log10((pm[ok]+1e-18)/(pr[ok]+1e-18)))))
        e=float(np.nanmean(err)); print(f'knock T60 {t60_fast}: later-window band error {e:.2f} dB')
        if best is None or e<best[0]: best=(e,t60_fast,wmode,wb)
    _,t60_fast,w_f,wb=best; print('chosen knock T60',t60_fast,'band weights dB:',np.round(wb,1))
    _,Wf=excess_weights(t60_fast); Rb=np.nan_to_num(Wf-wb[None,:])
    R_fast=np.array([np.interp(np.log(hz_f),np.log(centers),Rb[i]) for i in range(len(notes))])
    # per-note offsets on the combined, frequency-sorted bank
    OFF=np.concatenate([R_slow,R_fast],axis=1)[:,order]
    # ---- closed loop: the reference thump is two-stage (burst, then long quiet tail) and varies +-7 dB
    # per key, so single exponentials through the late slope misplace energy.  Correct every mode so the
    # TOTAL 0-200 ms energy per sub-fundamental band matches the reference on average over the fit notes
    # (the passage has an onset every ~150 ms, so the early window is what the ear and the mix see).
    hzc,tc,dbc,dlc=combined(t60_fast,w_f)
    for it in range(3):
        rows_=[]
        for i,(midi,layer,x) in enumerate(notes):
            model=tonal[(midi,layer)]+synth_bank(hzc,tc,dbc+OFF[i],G[i],n,dlc)
            pr,pm=band_power(x,0,.2,edges),band_power(model,0,.2,edges); ok=edges[1:]<.6*f1_of(midi)
            row=np.full(len(centers),np.nan); row[ok]=10*np.log10((pr[ok]+1e-18)/(pm[ok]+1e-18)); rows_.append(row)
        corr=np.nanmean(rows_,axis=0)
        for b in range(len(corr)):
            if np.isnan(corr[b]): corr[b]=corr[b-1] if b>0 else 0.0
        corr=np.clip(corr,-20,20); dbc=dbc+np.interp(np.log(hzc),np.log(centers),corr)
        print(f'energy correction pass {it}: band dB',np.round(corr,1))
    w_slow_c=np.zeros(nS); w_fast_c=np.zeros(FASTN); inv=np.argsort(order); allw=dbc[inv]; w_s[:]=allw[:nS]; w_f=allw[nS:]
    # per anchor/layer excitation table, fitted where measurable, held constant toward the bass
    thump=np.zeros((len(ANCHORS),2))
    for a,midi in enumerate(ANCHORS):
        for li,layer in enumerate((6,13)):
            ms=[m for (m,l,_) in notes if l==layer]; src=[g for (m,l,_),g in zip(notes,G) if l==layer]; thump[a,li]=np.interp(midi,ms,src)
    # ---- hiss: between-partial floor above max(1 kHz, 1.2 f1), 10-40 ms; two-pole lowpass shape
    hedges=np.array([1000,2000,4000,8000,16000]); hc=np.sqrt(hedges[:-1]*hedges[1:]); rows={}
    def floor_est(x,a,b,f1):
        seg=x[int(a*SR):int(b*SR)]; w=np.hanning(len(seg)); X=np.abs(np.fft.rfft(seg*w,8192))**2; f=np.fft.rfftfreq(8192,1/SR); lv=[]
        for lo,hi in zip(hedges[:-1],hedges[1:]):
            if lo<1.2*f1: lv.append(np.nan); continue
            m=(f>=lo)&(f<hi); lv.append(np.percentile(X[m],10)*m.sum())
        return np.array(lv)/(np.sum(w**2)*8192/2.0)
    for (midi,layer,x) in notes+hold:
        f1=f1_of(midi); band=floor_est(x,.01,.04,f1); ok=~np.isnan(band)
        fc_grid=np.geomspace(600,8000,80); target=10*np.log10(band[ok]/np.nanmax(band)+1e-12)
        def shape(fc): h=20*np.log10(1/(1+(hc[ok]/fc)**2)); return h-h.max()
        fc=fc_grid[int(np.argmin([np.mean((target-shape(fc))**2) for fc in fc_grid]))] if ok.sum()>=2 else 1500.0
        wins=[(.006,.02),(.02,.035),(.035,.05),(.05,.08),(.08,.12)]; trace=[10*np.log10(np.nansum(floor_est(x,a,b,f1))+1e-18) for a,b in wins]
        tc=np.array([(a+b)/2 for a,b in wins]); p=np.polyfit(tc,trace,1); t60=float(np.clip(-60/p[0]*1000,20,400)) if p[0]<0 else 400.0
        # in-band (>= first valid edge) window-mean power -> total output RMS at t=0 of the modelled burst
        f=np.linspace(1,20000,4000); H2=1/(1+(f/fc)**2)**2; lo=hedges[:-1][ok][0]; frac=H2[f>=lo].sum()/H2.sum()
        tau=t60/1000/6.907755; wmean=(tau/2)*(np.exp(-2*.01/tau)-np.exp(-2*.04/tau))/.03
        peak_db=10*np.log10(np.nansum(band)/frac/wmean+1e-18)
        rows[f'{name(midi)}v{layer}']=dict(inband_floor_db=round(float(10*np.log10(np.nansum(band)+1e-18)),1),bands_db=[None if np.isnan(b) else round(float(10*np.log10(b+1e-18)),1) for b in band],fc=round(float(fc)),t60_ms=round(t60,1),peak_db=round(float(peak_db),1))
        print(f'hiss {name(midi)}v{layer}: bands {rows[f"{name(midi)}v{layer}"]["bands_db"]} fc {fc:.0f} t60 {t60:.0f} ms -> burst peak {peak_db:.1f} dBFS')
    noise_db=np.zeros((len(ANCHORS),2)); noise_ms=np.zeros((len(ANCHORS),2)); noise_hz=np.zeros((len(ANCHORS),2))
    for a,midi in enumerate(ANCHORS):
        for li,layer in enumerate((6,13)):
            ms=[m for (m,l,_) in notes if l==layer]; r=[rows[f'{name(m)}v{layer}'] for m in ms]
            noise_db[a,li]=np.interp(midi,ms,[q['peak_db'] for q in r]); noise_hz[a,li]=np.exp(np.interp(midi,ms,np.log([q['fc'] for q in r]))); noise_ms[a,li]=np.interp(midi,ms,[q['t60_ms'] for q in r])
    note_midis=sorted(set(m for m,_,_ in notes)); OFFn=np.array([np.mean([OFF[i] for i,(m,l,_) in enumerate(notes) if m==mm],axis=0) for mm in note_midis])
    mode_db_key=np.array([[np.interp(a,note_midis,OFFn[:,k]) for k in range(OFFn.shape[1])] for a in ANCHORS])
    mode_hz,mode_t60,mode_db,mode_delay=combined(t60_fast,w_f)
    pad=64-len(mode_hz); assert pad>=0; mode_hz=np.pad(mode_hz,(0,pad)); mode_t60=np.pad(mode_t60,(0,pad),constant_values=1); mode_db=np.pad(mode_db,(0,pad),constant_values=-120); mode_delay=np.pad(mode_delay,(0,pad)); mode_db_key=np.pad(mode_db_key,((0,0),(0,pad)))
    j=dict(mode_hz=[round(float(v),2) for v in mode_hz],mode_t60=[round(float(v),3) for v in mode_t60],mode_db=[round(float(v),2) for v in mode_db],mode_delay_ms=[round(float(v),1) for v in mode_delay],mode_db_key=np.round(mode_db_key,1).tolist(),pulse_ms=PULSE_MS,pulse_cycles=PULSE_CYCLES,
           thump_db=np.round(thump,2).tolist(),noise_db=np.round(noise_db,2).tolist(),noise_ms=np.round(noise_ms,1).tolist(),noise_hz=np.round(noise_hz,0).tolist(),thump_mix=round(10**(PASSAGE_TRIM_DB/20),4),noise_mix=1.0,passage_trim_db=PASSAGE_TRIM_DB,
           slow_modes=len(hz_s),fast_modes=FASTN,fast_hz=[round(float(v),1) for v in hz_f],knock_t60=t60_fast,knock_band_centers=[round(float(c)) for c in centers],knock_band_db=[None if np.isnan(v) else round(float(v),1) for v in wb],
           reference=REF,tonal_patch=str(TONAL.relative_to(ROOT)),fit_notes=[f'{name(m)}v{l}' for m,l,_ in notes],held_out=[f'{name(m)}v{l}' for m,l,_ in hold],per_note_thump_gain_db={f'{name(m)}v{l}':round(float(g),2) for (m,l,_),g in zip(notes,G)},hiss_rows=rows)
    (ATT/'attack.json').write_text(json.dumps(j,indent=1))
    def cf(v):
        t=f'{float(v):.6g}'; return t+('f' if ('.' in t or 'e' in t) else '.0f')
    def arr(a): return '{'+','.join(cf(v) for v in a)+'}'
    def arr2(a): return '{'+','.join(arr(r) for r in a)+'}'
    write_header(ATT/'patch_attack.h',j)
    print('thump table dB:',np.round(thump,1).tolist()); print('hiss table dB:',np.round(noise_db,1).tolist()); print('hiss fc:',noise_hz.round().tolist()); print('hiss t60 ms:',noise_ms.round().tolist())
if __name__=='__main__':main()
