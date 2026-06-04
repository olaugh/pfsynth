/* wav.h - minimal mono 16-bit PCM WAV writer. HOST-ONLY (dev machine). */
#ifndef PF_WAV_H
#define PF_WAV_H

/* Write `n` float samples (nominally [-1,1]) as a mono 16-bit WAV at `path`.
 * Returns 0 on success, nonzero on error. */
int wav_write_mono16(const char *path, const float *samples, int n,
                     int sample_rate);

#endif
