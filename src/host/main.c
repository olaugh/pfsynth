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

    /* Full current model: coupled strings + strike-point comb -> mono mix. */
    pf_string_params params; pf_string_defaults(&params, SR);
    render_song(&params, buf, 1);

    pf_board_params bp;
    pf_board_defaults(&bp, SR);
    printf("soundboard: %d modes  %.0f-%.0f Hz  mix=%.2f\n", bp.modes, bp.f_lo, bp.f_hi, bp.mix);

    /* A/B for this milestone: the same body as a mono bank (L=R) vs the stereo
     * pair (decorrelated L/R banks, centered dry). */
    static pf_board       mono;   pf_board_init(&mono, &bp, SR);
    static pf_board_stereo st;    pf_board_stereo_init(&st, &bp, SR);
    for (int i = 0; i < TOTAL_SAMPLES; i++) {
        double m = pf_board_tick(&mono, buf[i]);
        smo[2 * i] = smo[2 * i + 1] = (float)m;
        double l, r; pf_board_stereo_tick(&st, buf[i], &l, &r);
        sst[2 * i] = (float)l; sst[2 * i + 1] = (float)r;
    }

    int rc = 0;
    rc |= finish_stereo("out_mono.wav", smo, TOTAL_SAMPLES, "mono  ");
    rc |= finish_stereo(OUT_PATH,       sst, TOTAL_SAMPLES, "stereo");

    free(buf); free(sst); free(smo);
    return rc;
}
