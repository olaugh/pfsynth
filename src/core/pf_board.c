/* pf_board.c - see pf_board.h.
 *
 * The body is a parallel bank of second-order resonators, each one a single mode
 *
 *     H_k(z) = 1 / (1 - 2 R cos(w) z^-1 + R^2 z^-2)
 *
 * whose impulse response is a decaying sinusoid at frequency w with per-sample
 * decay R. Summing the modes gives a coded modal impulse response; adding it on
 * top of a dry path makes the bank a parallel resonant EQ that colors the string
 * tone with the wooden body instead of replacing it.
 *
 * Mode frequencies are log-spaced (constant modes-per-octave, matching how plate
 * modal density grows) with a deterministic jitter so the bank doesn't ring like
 * a comb. Low modes ring longer than high ones, and a spectral tilt darkens the
 * top. Everything is generated from the handful of pf_board_params constants -
 * no tables, no samples - so it compresses to almost nothing.
 */
#include "pf_board.h"
#include <math.h>
#include <string.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/* deterministic LCG -> [0,1); keeps the body reproducible and table-free */
static double rnd01(unsigned *s)
{
    *s = *s * 1664525u + 1013904223u;
    return (*s >> 8) / 16777216.0;   /* top 24 bits */
}

void pf_board_defaults(pf_board_params *p, double sample_rate)
{
    (void)sample_rate;
    p->modes = 40;       /* denser bank reads as a richer, more solid body */
    p->f_lo  = 85.0;     /* lowest body resonance */
    p->f_hi  = 6000.0;   /* top of the radiating range */
    p->t_lo  = 0.18;     /* low modes ring enough to add body weight ... */
    p->t_hi  = 0.05;     /* ... high modes are shorter, coloring the attack */
    p->tilt  = -1.2;     /* dB/oct: gently darker toward the top, like radiation */
    p->color = 0.6;      /* gain irregularity so it reads as wood, not a formant */
    p->dry   = 1.0;      /* keep the full string tone present */
    p->mix   = 0.9;      /* body layer level: fuller, closer (audition knob in host) */
    p->seed  = 0x51A4E3u;
}

void pf_board_init(pf_board *b, const pf_board_params *p, double sample_rate)
{
    int N = p->modes;
    if (N > PF_BOARD_MAXMODES) N = PF_BOARD_MAXMODES;
    if (N < 0) N = 0;
    b->n   = N;
    b->dry = p->dry;
    b->mix = p->mix;

    unsigned s = p->seed ? p->seed : 1u;
    const double fref = 400.0;

    for (int k = 0; k < N; k++) {
        double u   = (k + 0.5) / N;                      /* 0..1 across the bank */
        double jit = (rnd01(&s) - 0.5) * (1.2 / N);      /* break exact log spacing */
        double e   = u + jit;
        if (e < 0.0) e = 0.0;
        if (e > 1.0) e = 1.0;

        double f = p->f_lo * pow(p->f_hi / p->f_lo, e);
        if (f < 20.0)          f = 20.0;
        if (f > 0.45 * sample_rate) f = 0.45 * sample_rate;

        double tau = p->t_lo * pow(p->t_hi / p->t_lo, u);
        double R   = exp(-1.0 / (tau * sample_rate));    /* per-sample mode decay */
        double w   = 2.0 * M_PI * f / sample_rate;

        b->a1[k] = 2.0 * R * cos(w);
        b->a2[k] = R * R;

        /* Mode gain: spectral tilt (dB/oct) + per-mode random color. The (1-R)
         * factor cancels the resonator's own 1/(1-R) resonant gain, so each
         * mode contributes ~env*x at its peak - i.e. the whole bank is a roughly
         * unity-gain coloring filter, comparable in level to the dry path, not a
         * runaway amplifier. (No vector normalization - that was the level bug.) */
        double tiltdb = p->tilt * (log(f / fref) / log(2.0));
        double env    = pow(10.0, tiltdb / 20.0);
        double col    = 1.0 + p->color * (rnd01(&s) - 0.5) * 2.0;
        if (col < 0.05) col = 0.05;

        b->g[k]  = (1.0 - R) * env * col;
        b->y1[k] = b->y2[k] = 0.0;
    }
}

void pf_board_reset(pf_board *b)
{
    for (int k = 0; k < b->n; k++) b->y1[k] = b->y2[k] = 0.0;
}

double pf_board_tick(pf_board *b, double x)
{
    double modal = 0.0;
    for (int k = 0; k < b->n; k++) {
        double y = x + b->a1[k] * b->y1[k] - b->a2[k] * b->y2[k];
        b->y2[k] = b->y1[k];
        b->y1[k] = y;
        modal += b->g[k] * y;
    }
    return b->dry * x + b->mix * modal;
}

void pf_board_process(pf_board *b, float *buf, int n)
{
    for (int i = 0; i < n; i++)
        buf[i] = (float)pf_board_tick(b, buf[i]);
}
