"""Identify pf_partial tonal patches on the C1..C8 anchor grid (15 anchors, every 6 semitones).
salamander.{bin,h}: Salamander samples with the experiment-02 identification (unchanged code, so the
C2..C6 anchors reproduce the frozen 9-anchor patch byte for byte). pianoteq.{bin,h}: Pianoteq
"Steinway D Close Mic Classical" renders with the onset-exact identification from experiment 03."""
import os, sys, ctypes as ct, json, hashlib, zlib, numpy as np
which=sys.argv[1] if len(sys.argv)>1 else 'both'
from partial_audition import ROOT, SR, ANCHORS, LAYERS, Patch, name, identify_tuning
import partial_audition
OUT=ROOT/'experiments/partial-piano-wide'; OUT.mkdir(parents=True,exist_ok=True)
def run(tag,sample,identify_envelopes,symbol,reference):
    tuning=[];envelopes=[];phases=[]
    for midi in ANCHORS:
        pitch=identify_tuning(sample(int(midi),13),int(midi)); tuning.append(pitch); es=[];ps=[]
        for layer in LAYERS:
            e,p=identify_envelopes(sample(int(midi),layer),int(midi),pitch); es.append(e); ps.append(p)
        envelopes.append(es); phases.append(ps); print(tag,'identified',name(midi),'cents',round(1200*np.log2(pitch[0]),2),'B',round(pitch[1],6),flush=True)
    tuning=np.array(tuning,np.float32);envelopes=np.array(envelopes,np.uint8);phases=np.array(phases,np.uint8)
    patch=Patch();ct.memmove(ct.addressof(patch.tuning),tuning.ctypes.data,tuning.nbytes);ct.memmove(ct.addressof(patch.envelope),envelopes.ctypes.data,envelopes.nbytes);ct.memmove(ct.addressof(patch.phase),phases.ctypes.data,phases.nbytes)
    (OUT/f'{tag}.bin').write_bytes(bytes(patch))
    def c_array(a):
        if a.ndim==1:return '{'+','.join(str(int(x)) for x in a)+'}'
        return '{\n'+',\n'.join(c_array(v) for v in a)+'\n}'
    (OUT/f'{tag}.h').write_text('#include "../../src/core/pf_partial.h"\nstatic const pf_partial_patch '+symbol+'={\n{'+','.join('{'+','.join(f'{float(x):.9e}f' for x in row)+'}' for row in tuning)+'},\n'+c_array(envelopes)+',\n'+c_array(phases)+'\n};\n')
    (OUT/f'{tag}-report.json').write_text(json.dumps(dict(reference=reference,anchors=[name(m) for m in ANCHORS],layers=LAYERS,patch_bytes=len(bytes(patch)),compressed_patch_bytes=len(zlib.compress(bytes(patch),9)),patch_sha256=hashlib.sha256(bytes(patch)).hexdigest()),indent=2)+'\n')
    print('wrote',OUT/f'{tag}.bin',len(bytes(patch)),'bytes',flush=True)
if which in ('salamander','both'):
    run('salamander',partial_audition.sample,partial_audition.identify_envelopes,'pf_partial_salamander','Salamander Grand Piano V3 (Alexander Holm, CC BY 3.0), experiment-02 identification')
if which in ('pianoteq','both'):
    os.environ['PF_REF']='ptq'
    import refsrc, partial_fit_ptq
    run('pianoteq',refsrc.sample,partial_fit_ptq.identify_envelopes,'pf_partial_pianoteq','Pianoteq 6.7.3 "Steinway D Close Mic Classical" renders, onset-exact identification')
