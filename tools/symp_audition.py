"""A/B the sympathetic resonance on pedal-rich excerpts: model without / with pf_resonance / Pianoteq.
Writes WAV + MP3 to build/demo/symp-ab/.  Usage: symp_audition.py  (PF_REF=ptq)"""
import subprocess,sys,shutil
from pathlib import Path
sys.path.insert(0,str(Path(__file__).parent))
import ptq_render
from mkmidi import write_midi
from pedal_excerpt import events
ROOT=Path(__file__).resolve().parent.parent; ASAP=Path('/Users/john/sources/segno/tools/synth-ab/corpora/asap')
OUT=ROOT/'build/demo/symp-ab'; OUT.mkdir(parents=True,exist_ok=True); PFRENDER=ROOT/'build/pfrender'
EXCERPTS=[('chopin-op25-1','Chopin/Etudes_op_25/1/TongB02M.mid',0,30),('debussy-reflets','Debussy/Images_Book_1/1_Reflets_dans_lEau/ParkJH13M.mid',0,35),('beethoven-op110','Beethoven/Piano_Sonatas/31-1/Kavalerova01.mid',0,30)]
def excerpt_midi(src,t0,t1,dst):
    ev=[(t-t0,st,a,b) for t,st,a,b in events(src) if t0<=t<t1]
    # controller state at t0
    state={}
    for t,st,a,b in events(src):
        if t>=t0: break
        if st==0xB0 and a in (64,66,67): state[a]=b
    pre=[(0.0,0xB0,cc,v) for cc,v in state.items()]
    ev=pre+ev+[(t1-t0,0xB0,64,0),(t1-t0,0xB0,66,0),(t1-t0+3,0xB0,123,0)]
    write_midi(dst,ev)
def mp3(wav):
    subprocess.run(['/opt/homebrew/bin/lame','--quiet','-V2',str(wav),str(wav.with_suffix('.mp3'))],check=True)
for tag,rel,t0,t1 in EXCERPTS:
    mid=OUT/f'{tag}.mid'; excerpt_midi(ASAP/rel,t0,t1,mid)
    for var,args in [('dry',['--no-resonance']),('symp',[])]:
        wav=OUT/f'{tag}-{var}.wav'
        r=subprocess.run([str(PFRENDER),str(mid),str(wav)]+args,check=True,capture_output=True,text=True); print(r.stdout.strip()); mp3(wav)
    ref=ptq_render.CACHE/f'sympab-{tag}.wav'
    if not ref.exists(): ptq_render.run_ptq(mid,ref)
    shutil.copyfile(ref,OUT/f'{tag}-pianoteq.wav'); mp3(OUT/f'{tag}-pianoteq.wav')
print('done'); print('\n'.join(str(p) for p in sorted(OUT.glob('*.mp3'))))
