/* audio.c - see audio.h. macOS CoreAudio default-output AudioUnit, C API.
 * The unit sample-rate-converts our 44.1k float stream to whatever the device
 * runs at, so the engine always sees a fixed rate. */
#include "audio.h"

#include <AudioUnit/AudioUnit.h>
#include <AudioToolbox/AudioToolbox.h>
#include <stdio.h>

static AudioUnit g_unit;
static int       g_running;

static OSStatus render_cb(void *ref,
                          AudioUnitRenderActionFlags *flags,
                          const AudioTimeStamp *ts,
                          UInt32 bus, UInt32 nframes,
                          AudioBufferList *io)
{
    (void)flags; (void)ts; (void)bus;
    pf_engine *e = (pf_engine *)ref;
    float *out = (float *)io->mBuffers[0].mData;  /* interleaved stereo */
    pf_engine_render(e, out, (int)nframes);
    return noErr;
}

int pf_audio_start(pf_engine *e)
{
    AudioComponentDescription desc = {0};
    desc.componentType    = kAudioUnitType_Output;
    desc.componentSubType = kAudioUnitSubType_DefaultOutput;
    desc.componentManufacturer = kAudioUnitManufacturer_Apple;

    AudioComponent comp = AudioComponentFindNext(NULL, &desc);
    if (!comp) { fprintf(stderr, "audio: no default output\n"); return 1; }
    if (AudioComponentInstanceNew(comp, &g_unit) != noErr) {
        fprintf(stderr, "audio: cannot create unit\n"); return 1;
    }

    AudioStreamBasicDescription fmt = {0};
    fmt.mSampleRate       = e->sr;
    fmt.mFormatID         = kAudioFormatLinearPCM;
    fmt.mFormatFlags      = kAudioFormatFlagIsFloat | kAudioFormatFlagIsPacked;
    fmt.mFramesPerPacket  = 1;
    fmt.mChannelsPerFrame = 2;
    fmt.mBitsPerChannel   = 32;
    fmt.mBytesPerFrame    = 8;   /* 2 ch * 4 bytes, interleaved */
    fmt.mBytesPerPacket   = 8;
    if (AudioUnitSetProperty(g_unit, kAudioUnitProperty_StreamFormat,
                             kAudioUnitScope_Input, 0, &fmt, sizeof fmt) != noErr) {
        fprintf(stderr, "audio: cannot set format\n");
        AudioComponentInstanceDispose(g_unit); return 1;
    }

    AURenderCallbackStruct cb = { render_cb, e };
    if (AudioUnitSetProperty(g_unit, kAudioUnitProperty_SetRenderCallback,
                             kAudioUnitScope_Input, 0, &cb, sizeof cb) != noErr) {
        fprintf(stderr, "audio: cannot set callback\n");
        AudioComponentInstanceDispose(g_unit); return 1;
    }

    if (AudioUnitInitialize(g_unit) != noErr ||
        AudioOutputUnitStart(g_unit) != noErr) {
        fprintf(stderr, "audio: cannot start\n");
        AudioComponentInstanceDispose(g_unit); return 1;
    }
    g_running = 1;
    return 0;
}

void pf_audio_stop(void)
{
    if (!g_running) return;
    AudioOutputUnitStop(g_unit);
    AudioUnitUninitialize(g_unit);
    AudioComponentInstanceDispose(g_unit);
    g_running = 0;
}
