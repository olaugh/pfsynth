/* main.c - offline render harness. HOST-ONLY.
 *
 * Drives the pf_string voice from a hardcoded {note, velocity, time} event list
 * (a stand-in for the eventual serialized song format), renders to a PCM buffer,
 * peak-normalizes, and writes a WAV. Prints per-note diagnostics so the physical
 * model can be sanity-checked without listening.
 */
#include "../core/pf_string.h"
#include "../core/pf_board.h"
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
    /* an octave arpeggio A1..A6, left to ring: with the pitch-fitted model the
     * bass sustains for many seconds while the treble dies fast and shimmers
     * (rising inharmonicity), the way a real piano does. */
    { 33, 0.75,  0.0 },   /* A1 ~55 Hz   */
    { 45, 0.75,  1.6 },   /* A2 ~110 Hz  */
    { 57, 0.75,  3.2 },   /* A3 ~220 Hz  */
    { 69, 0.75,  4.8 },   /* A4 440 Hz   */
    { 81, 0.75,  6.4 },   /* A5 ~880 Hz  */
    { 93, 0.75,  8.0 },   /* A6 ~1760 Hz */
};
#define N_EVENTS ((int)(sizeof(SONG) / sizeof(SONG[0])))

#define TOTAL_SECONDS 14.0
#define TOTAL_SAMPLES ((int)(TOTAL_SECONDS * SR))

static double midi_to_hz(int note)
{
    return 440.0 * pow(2.0, (note - 69) / 12.0);
}

/* print stats, NaN-check, peak-normalize (L/R together), and write a stereo WAV.
 * `il` is 2*n interleaved samples. Returns 0 on success. */
static int finish_stereo(const char *path, float *il, int n, const char *tag)
{
    double peak = 0.0, sumsq = 0.0;
    for (int i = 0; i < 2 * n; i++) {
        float v = il[i];
        if (v != v) { fprintf(stderr, "ERROR: NaN in %s output\n", tag); return 2; }
        double a = fabs(v);
        if (a > peak) peak = a;
        sumsq += (double)v * v;
    }
    printf("  [%s] pre-norm peak=%.4g  rms=%.4g\n", tag, peak, sqrt(sumsq / (2 * n)));

    if (peak > 0.0) {
        float g = (float)(0.89 / peak);   /* scale both channels equally (keep balance) */
        for (int i = 0; i < 2 * n; i++) il[i] *= g;
    }
    if (wav_write_stereo16(path, il, n, SR)) {
        fprintf(stderr, "ERROR: failed to write %s\n", path);
        return 3;
    }
    printf("  wrote %s\n", path);
    return 0;
}

/* Render the whole SONG with the given patch into buf (one voice per event,
 * summed). Prints per-note diagnostics when verbose. */
static void render_song(const pf_string_params *params, float *buf, int verbose)
{
    static pf_string voice;  /* 24 KB+, keep off the stack */
    memset(buf, 0, (size_t)TOTAL_SAMPLES * sizeof(float));

    for (int e = 0; e < N_EVENTS; e++) {
        double f0 = midi_to_hz(SONG[e].note);
        int start = (int)(SONG[e].t * SR);
        if (start >= TOTAL_SAMPLES) continue;
        int len = TOTAL_SAMPLES - start;

        pf_string_init(&voice, params, f0);
        pf_string_strike(&voice, SONG[e].vel);
        pf_string_process(&voice, buf + start, len);

        if (verbose)
            printf("  note %3d  f0=%7.2f Hz  vel=%.2f  t=%.2fs  "
                   "delay=%d disp=%d contact=%ld samp (%.2f ms)\n",
                   SONG[e].note, f0, SONG[e].vel, SONG[e].t,
                   voice.dl_len, voice.disp_n, voice.dbg_contact_samples,
                   1000.0 * voice.dbg_contact_samples / SR);
    }
}

int main(void)
{
    float *buf  = (float *)calloc(TOTAL_SAMPLES, sizeof(float));       /* mono mix */
    float *sst  = (float *)calloc((size_t)2 * TOTAL_SAMPLES, sizeof(float)); /* stereo body */
    float *smo  = (float *)calloc((size_t)2 * TOTAL_SAMPLES, sizeof(float)); /* mono body */
    if (!buf || !sst || !smo) { fprintf(stderr, "oom\n"); return 1; }

    printf("pfsynth render: %d events, %.1f s @ %d Hz\n",
           N_EVENTS, TOTAL_SECONDS, SR);

    pf_board_params bp;
    pf_board_defaults(&bp, SR);

    /* A/B for this milestone: the inharmonicity and decay held FLAT across the
     * keyboard (pitch slopes 0) vs scaled by pitch from the Salamander fit. Same
     * A4 tone in both; the difference is how bass and treble behave. */
    pf_string_params pon;  pf_string_defaults(&pon, SR);                       /* fitted */
    pf_string_params poff = pon;                                              /* flat */
    poff.inharm_pitch = 0.0; poff.decay_pitch = 0.0;
    printf("pitch fit: B~f0^%.2f  T60~f0^%.2f  (flat = 0)\n",
           pon.inharm_pitch, pon.decay_pitch);

    render_song(&poff, buf, 0);
    { static pf_board_stereo st; pf_board_stereo_init(&st, &bp, SR);
      for (int i = 0; i < TOTAL_SAMPLES; i++) {
          double l, r; pf_board_stereo_tick(&st, buf[i], &l, &r);
          smo[2 * i] = (float)l; smo[2 * i + 1] = (float)r; } }

    render_song(&pon, buf, 1);
    { static pf_board_stereo st; pf_board_stereo_init(&st, &bp, SR);
      for (int i = 0; i < TOTAL_SAMPLES; i++) {
          double l, r; pf_board_stereo_tick(&st, buf[i], &l, &r);
          sst[2 * i] = (float)l; sst[2 * i + 1] = (float)r; } }

    int rc = 0;
    rc |= finish_stereo("out_flat.wav", smo, TOTAL_SAMPLES, "flat ");
    rc |= finish_stereo(OUT_PATH,       sst, TOTAL_SAMPLES, "fit  ");

    free(buf); free(sst); free(smo);
    return rc;
}
