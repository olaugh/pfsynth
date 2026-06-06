/* wav.h - minimal mono 16-bit PCM WAV writer. HOST-ONLY (dev machine). */
#ifndef PF_WAV_H
#define PF_WAV_H

/* Write `n` float samples (nominally [-1,1]) as a mono 16-bit WAV at `path`.
 * Returns 0 on success, nonzero on error. */
int wav_write_mono16(const char *path, const float *samples, int n,
                     int sample_rate);

/* Write `n` interleaved L,R float frames (2*n samples) as a stereo 16-bit WAV. */
int wav_write_stereo16(const char *path, const float *interleaved, int n,
                       int sample_rate);

/* Read a 16-bit PCM WAV into a malloc'd interleaved-stereo float buffer (mono
 * is duplicated to both channels, extra channels dropped), peak-normalized to
 * ~0.7. Caller frees *out. Returns 0 on success; *frames = stereo frames. */
int wav_read_stereo(const char *path, float **out, long *frames, int *sample_rate);

#endif
