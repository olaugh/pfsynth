"""Reference-note source switch: PF_REF=sal (Salamander samples) or ptq (Pianoteq renders).
Layers 6/13 map to MIDI velocities 48/100 in both cases; onset-aligned with a 5 ms lead-in."""
import os, numpy as np
from partial_audition import SR, name
import partial_audition
from body_audition import align
REF=os.environ.get('PF_REF','sal')
LAYER_VEL={6:48,13:100}
def sample(midi,layer,duration=7):
    if REF=='ptq':
        import ptq_render
        x=ptq_render.note(int(midi),LAYER_VEL[layer])
        return align(x,int(duration*SR))[0]
    return partial_audition.sample(midi,layer,duration)
