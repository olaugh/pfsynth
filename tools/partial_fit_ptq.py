"""Identify a pf_partial tonal patch from Pianoteq 'Steinway D Close Mic Classical' renders.
Same identification code as partial_audition.py; writes to experiments/partial-piano-ptq/
and leaves the preferred Salamander-fitted patch untouched."""
import os; os.environ['PF_REF']='ptq'
import ctypes as ct, json, hashlib, zlib, numpy as np
from partial_audition import ROOT, SR, ANCHORS, LAYERS, Patch, name, identify_tuning, TIMES
from refsrc import sample
from scipy import signal
def identify_envelopes(x,midi,tuning):
    """As partial_audition.identify_envelopes, but onset-exact: the recording is shifted so the
    detected onset sits at t=0 (the loader leaves a 5 ms lead-in) and the first knot is measured
    over the first 6 ms after onset instead of a +-8 ms window.  Pianoteq reaches full level within
    ~2 ms of onset; the blurred knots made the tonal model 10-15 dB soft in its first 10 ms."""
    hit=np.flatnonzero(np.abs(x[:int(.03*SR)])>np.abs(x).max()*.02); onset=int(hit[0]) if len(hit) else 0
    x=np.concatenate([x[onset:],np.zeros(onset)])
    f1=440*2**((midi-69)/12)*tuning[0];B=tuning[1]
    env=np.zeros((64,10)); phases=np.zeros(64); t=np.arange(len(x))/SR
    for k in range(64):
        h=k+1;f=f1*h*np.sqrt((1+B*h*h)/(1+B))
        if f>=SR*.44:continue
        width=max(.012,2.5/f1); win=signal.windows.hann(int(width*SR)|1);win/=win.sum()
        base=signal.fftconvolve(x*np.exp(-2j*np.pi*f*t),win,mode='same')*2; envelope=np.abs(base)
        for j,when in enumerate(TIMES):
            if j==0: lo,hi=0,int(.006*SR)
            else:
                center=int(when*SR);radius=int(max(.008,min(.1,when*.07))*SR); lo,hi=max(0,center-radius),min(len(x),center+radius+1)
            env[k,j]=np.sqrt(np.mean(envelope[lo:hi]**2))
        peak=env[k].max(); env[k,env[k]<max(2e-6,peak*.002)]=1e-6; env[k,-1]=min(env[k,-1],env[k,-2])
        phases[k]=np.angle(np.mean(base[int(.025*SR):int(.08*SR)]))%(2*np.pi)
    return np.round(np.clip((20*np.log10(np.maximum(env,1e-6))+120)*2,0,255)).astype(np.uint8),np.round(phases*256/(2*np.pi)).astype(np.uint8)
OUT=ROOT/'experiments/partial-piano-ptq'; OUT.mkdir(parents=True,exist_ok=True)
def main():
    tuning=[];envelopes=[];phases=[]
    for midi in ANCHORS:
        pitch=identify_tuning(sample(int(midi),13),int(midi)); tuning.append(pitch); es=[];ps=[]
        for layer in LAYERS:
            e,p=identify_envelopes(sample(int(midi),layer),int(midi),pitch); es.append(e); ps.append(p)
        envelopes.append(es); phases.append(ps)
        print('Identified',name(midi),'cents',round(1200*np.log2(pitch[0]),2),'B',round(pitch[1],6),flush=True)
    tuning=np.array(tuning,np.float32);envelopes=np.array(envelopes,np.uint8);phases=np.array(phases,np.uint8)
    patch=Patch();ct.memmove(ct.addressof(patch.tuning),tuning.ctypes.data,tuning.nbytes);ct.memmove(ct.addressof(patch.envelope),envelopes.ctypes.data,envelopes.nbytes);ct.memmove(ct.addressof(patch.phase),phases.ctypes.data,phases.nbytes)
    (OUT/'patch.bin').write_bytes(bytes(patch))
    def c_array(a):
        if a.ndim==1:return '{'+','.join(str(int(x)) for x in a)+'}'
        return '{\n'+',\n'.join(c_array(v) for v in a)+'\n}'
    (OUT/'patch.h').write_text('#include "../../src/core/pf_partial.h"\nstatic const pf_partial_patch pf_partial_ptq={\n{'+','.join('{'+','.join(f'{float(x):.9e}f' for x in row)+'}' for row in tuning)+'},\n'+c_array(envelopes)+',\n'+c_array(phases)+'\n};\n')
    (OUT/'report.json').write_text(json.dumps(dict(reference='Pianoteq 6.7.3, preset "Steinway D Close Mic Classical", isolated notes rendered headless (black-box target)',training_notes=[name(m) for m in ANCHORS],velocities=[48,100],onset='exact: shifted to detected onset, first knot over 0-6 ms',patch_bytes=len(bytes(patch)),compressed_patch_bytes=len(zlib.compress(bytes(patch),9)),patch_sha256=hashlib.sha256(bytes(patch)).hexdigest()),indent=2)+'\n')
    print('wrote',OUT/'patch.bin',len(bytes(patch)),'bytes')
if __name__=='__main__':main()
