/* main.c - offline render harness. HOST-ONLY.
 *
 * Drives the pf_string voice from a hardcoded {note, velocity, time} event list
 * (a stand-in for the eventual serialized song format), renders to a PCM buffer,
 * peak-normalizes, and writes a WAV. Prints per-note diagnostics so the physical
 * model can be sanity-checked without listening.
 */
#include "../core/pf_string.h"
#include "wav.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#define SR        44100
#define OUT_PATH  "out.wav"

/* {MIDI note, velocity 0..1, strike time in seconds}. MIDI 69 = A4 = 440 Hz. */
typedef struct { int note; double vel; double t; } event;

static const event SONG[] = {
    { 33, 0.90, 0.0 },   /* A1  (~55 Hz)  - lots of stretched partials */
    { 45, 0.85, 1.6 },   /* A2  (~110 Hz) */
    { 69, 0.75, 3.2 },   /* A4  (440 Hz)  */
    { 93, 0.75, 4.8 },   /* A6  (~1760 Hz)*/
    /* a chord at the end (C4 / E4 / G4) */
    { 60, 0.70, 6.4 },
    { 64, 0.70, 6.4 },
    { 67, 0.70, 6.4 },
};
#define N_EVENTS ((int)(sizeof(SONG) / sizeof(SONG[0])))

#define TOTAL_SECONDS 11.0
#define TOTAL_SAMPLES ((int)(TOTAL_SECONDS * SR))

static double midi_to_hz(int note)
{
    return 440.0 * pow(2.0, (note - 69) / 12.0);
}

int main(void)
{
    float *buf = (float *)calloc(TOTAL_SAMPLES, sizeof(float));
    if (!buf) { fprintf(stderr, "oom\n"); return 1; }

    pf_string_params params;
    pf_string_defaults(&params, SR);

    /* One voice per event (no string coupling yet). Each voice is rendered for
     * the remainder of the timeline and summed into the shared buffer. */
    static pf_string voice;  /* 8 KB+, keep off the stack */

    printf("pfsynth render: %d events, %.1f s @ %d Hz\n",
           N_EVENTS, TOTAL_SECONDS, SR);

    for (int e = 0; e < N_EVENTS; e++) {
        double f0 = midi_to_hz(SONG[e].note);
        int start = (int)(SONG[e].t * SR);
        if (start >= TOTAL_SAMPLES) continue;
        int len = TOTAL_SAMPLES - start;

        pf_string_init(&voice, &params, f0);
        pf_string_strike(&voice, SONG[e].vel);
        pf_string_process(&voice, buf + start, len);

        printf("  note %3d  f0=%7.2f Hz  vel=%.2f  t=%.2fs  "
               "delay=%d disp=%d contact=%ld samp (%.2f ms)\n",
               SONG[e].note, f0, SONG[e].vel, SONG[e].t,
               voice.dl_len, voice.disp_n, voice.dbg_contact_samples,
               1000.0 * voice.dbg_contact_samples / SR);
    }

    /* stats + peak normalize */
    double peak = 0.0, sumsq = 0.0;
    int nan = 0;
    for (int i = 0; i < TOTAL_SAMPLES; i++) {
        float v = buf[i];
        if (v != v) { nan = 1; break; }
        double a = fabs(v);
        if (a > peak) peak = a;
        sumsq += (double)v * v;
    }
    if (nan) { fprintf(stderr, "ERROR: NaN in output\n"); free(buf); return 2; }

    double rms = sqrt(sumsq / TOTAL_SAMPLES);
    printf("pre-norm: peak=%.4g  rms=%.4g\n", peak, rms);

    if (peak > 0.0) {
        float g = (float)(0.89 / peak);   /* ~ -1 dBFS headroom */
        for (int i = 0; i < TOTAL_SAMPLES; i++) buf[i] *= g;
    }

    if (wav_write_mono16(OUT_PATH, buf, TOTAL_SAMPLES, SR)) {
        fprintf(stderr, "ERROR: failed to write %s\n", OUT_PATH);
        free(buf);
        return 3;
    }
    printf("wrote %s (%.1f s, %d samples)\n", OUT_PATH, TOTAL_SECONDS, TOTAL_SAMPLES);

    free(buf);
    return 0;
}
