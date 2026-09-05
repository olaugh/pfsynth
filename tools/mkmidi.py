"""Tiny Standard MIDI File (format 0) writer for reference renders."""
import struct
def vlq(n):
    out=[n&0x7f]; n>>=7
    while n: out.append(0x80|(n&0x7f)); n>>=7
    return bytes(reversed(out))
def write_midi(path, events, ppq=960, tempo_us=500000):
    """events: list of (time_seconds, status, data1, data2)."""
    events=sorted(events,key=lambda e:(e[0],e[1]&0xF0!=0x80)); track=bytearray(b'\x00\xff\x51\x03'+struct.pack('>I',tempo_us)[1:])
    last=0
    for t,st,d1,d2 in events:
        tick=int(round(t*ppq*1e6/tempo_us)); track+=vlq(tick-last)+bytes([st,d1,d2]); last=tick
    track+=b'\x00\xff\x2f\x00'
    with open(path,'wb') as f:
        f.write(b'MThd'+struct.pack('>IHHH',6,0,1,ppq)+b'MTrk'+struct.pack('>I',len(track))+track)
def single_note(path, midi, velocity, hold, tail=3.0):
    write_midi(path,[(0.05,0x90,midi,velocity),(0.05+hold,0x80,midi,0),(0.05+hold+tail,0xB0,123,0)])
