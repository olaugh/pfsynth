"""Render reference audio with the user's licensed Pianoteq 6 (black-box target).
Preset: "Steinway D Close Mic Classical".  Cached under calib/pianoteq-ref/closemic/.
"""
from pathlib import Path
import subprocess, json, numpy as np
from scipy.io import wavfile
from mkmidi import write_midi, single_note
ROOT=Path(__file__).resolve().parents[1]; SR=44100
BIN='/Applications/Pianoteq 6/Pianoteq 6.app/Contents/MacOS/Pianoteq 6'; PRESET='Steinway D Close Mic Classical'
CACHE=ROOT/'calib/pianoteq-ref/closemic'; CACHE.mkdir(parents=True,exist_ok=True)
NAMES=['C','C#','D','D#','E','F','F#','G','G#','A','A#','B']
def name(m):return NAMES[m%12]+str(m//12-1)
def run_ptq(midi_path,wav_path):
    subprocess.run([BIN,'--headless','--quiet','--preset',PRESET,'--rate','44100','--bit-depth','16','--midi',str(midi_path),'--wav',str(wav_path)],check=True,stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL,timeout=600)
def load_mono(path):
    sr,x=wavfile.read(path); assert sr==SR; x=x.astype(float)/32768
    return x.mean(axis=1) if x.ndim==2 else x
def note(midi,vel,hold=8.0,tail=1.0):
    """Isolated note, key held `hold` s.  Returns mono float, onset-aligned like the Salamander loader (5 ms lead-in)."""
    wav=CACHE/f'{name(midi)}v{vel}.wav'
    if not wav.exists():
        mid=CACHE/f'{name(midi)}v{vel}.mid'; single_note(mid,midi,vel,hold,tail); run_ptq(mid,wav)
    return load_mono(wav)
def beethoven(score_json,out_wav):
    """Render the excerpt: original key releases + CC64, notes still down at the cutoff are released there."""
    d=json.loads(Path(score_json).read_text()); cut=d['source_cutoff']-d['source_start']; dur=d['duration']; ev=[]
    for n in d['notes']:
        off=n['key_release'] if n['key_release'] is not None else cut
        ev.append((n['start'],0x90,n['note'],n['velocity'])); ev.append((min(off,cut),0x80,n['note'],0))
    ev.append((0.0,0xB0,64,0))
    for p in d['pedal_events']:
        if p['time']<cut: ev.append((p['time'],0xB0,64,p['value']))
    ev.append((cut,0xB0,64,0)); ev.append((dur-0.01,0xB0,123,0))
    mid=Path(out_wav).with_suffix('.mid'); write_midi(mid,ev); run_ptq(mid,out_wav)
    x=load_mono(out_wav); total=int(dur*SR); x=np.pad(x[:total],(0,max(0,total-len(x)))); return x
if __name__=='__main__':
    import sys
    for midi in [36,42,48,54,60,66,72,78,84,45,57,69,81]:
        for vel in (48,100):
            x=note(midi,vel); print(name(midi),vel,len(x)/SR,'s peak',round(float(np.abs(x).max()),3),flush=True)
    x=beethoven(ROOT/'experiments/partial-piano/beethoven-score.json',CACHE/'beethoven-op2-no1-ptq.wav'); print('beethoven',len(x)/SR,'s peak',round(float(np.abs(x).max()),3))
