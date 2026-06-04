/* audio.h - CoreAudio output. HOST-ONLY, macOS. Drives pf_engine_render from
 * the audio thread. */
#ifndef PF_AUDIO_H
#define PF_AUDIO_H

#include "engine.h"

/* Open the default output device and start pulling audio from `e`.
 * Returns 0 on success. */
int  pf_audio_start(pf_engine *e);
void pf_audio_stop(void);

#endif
