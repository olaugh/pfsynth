/* pf_string.c - see pf_string.h.
 *
 * A single string is a feedback loop:
 *
 *     +--> [delay N] -> [tuner allpass] -> [loss lowpass] -> [M dispersion APs] --+
 *     |                                                                           |
 *     +--------------------------- (+ hammer force) <----------------------------+
 *
 * The loop resonates wherever its total round-trip phase delay equals an integer
 * number of samples per period. With no dispersion that's the harmonic series.
 * The dispersion allpasses make the loop SHORTER at high frequencies, so high
 * partials resonate sharp -> stiff-string inharmonicity. The loss lowpass makes
 * high partials decay faster. The nonlinear hammer injects the strike.
 */
#include "pf_string.h"
#include <math.h>
#include <string.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define PF_DC_R 0.999  /* DC blocker pole */

/* ---- phase-delay helpers (init only; never called in the audio path) ---- */

/* Phase delay (samples) of one-pole lowpass H(z) = (1-d)/(1 - d z^-1) at w. */
static double pd_onepole(double w, double d)
{
    return atan2(d * sin(w), 1.0 - d * cos(w)) / w;
}

/* Phase delay (samples) of first-order allpass A(z) = (a + z^-1)/(1 + a z^-1). */
static double pd_allpass(double w, double a)
{
    double pn  = atan2(-sin(w),     a + cos(w));
    double pd  = atan2(-a * sin(w), 1.0 + a * cos(w));
    return -(pn - pd) / w;
}

void pf_string_defaults(pf_string_params *p, double sample_rate)
{
    p->sample_rate         = sample_rate;
    p->inharmonicity       = 4.0e-4;   /* moderate piano stiffness */
    p->decay_t60           = 7.0;
    p->damping             = 0.20;
    p->dispersion_sections = 8;

    p->hammer_mass         = 4.0e-3;   /* ~4 g */
    p->hammer_stiffness    = 6.0e6;
    p->hammer_exponent     = 2.3;
    p->hammer_vmax         = 5.0;
    /* force -> displacement injection per sample = dt/(2Z), Z ~ 2 */
    p->injection           = 1.0 / (2.0 * 2.0) / sample_rate;

    p->output_gain         = 1.0;
}

void pf_string_init(pf_string *s, const pf_string_params *p, double f0)
{
    double fs = p->sample_rate;
    double w0 = 2.0 * M_PI * f0 / fs;
    double P0 = fs / f0;                 /* loop delay for the fundamental */
    double B  = p->inharmonicity;
    double d  = p->damping;
    int    M  = p->dispersion_sections;
    int    n, it;

    memset(s, 0, sizeof *s);
    s->sr = fs;
    s->f0 = f0;

    /* --- loss filter: pick loop gain g for the target fundamental T60 --- */
    {
        double g = pow(10.0, -3.0 / (f0 * p->decay_t60)); /* g^(f0*T60) = 1e-3 */
        s->loss_b0 = g * (1.0 - d);
        s->loss_a1 = d;
    }
    double Dloss0 = pd_onepole(w0, d);

    /* --- dispersion: fit one allpass coefficient to the target stretch --- *
     * Highest partial that still fits below Nyquist (the stretched formula
     * pushes partials up, so this is well below f0-harmonic count for bass). */
    int nmax = 1;
    for (n = 1; n <= 4096; n++) {
        double fn = n * f0 * sqrt(1.0 + B * (double)n * n);
        if (fn >= fs * 0.5) { nmax = n - 1; break; }
        nmax = n;
    }
    if (nmax < 1) nmax = 1;

    /* Fit at a moderate partial: the low partials carry the audible stretch and
     * fitting at the very top would force an extreme coefficient. */
    int nfit = nmax < 24 ? nmax : 24;
    double fn_fit   = nfit * f0 * sqrt(1.0 + B * (double)nfit * nfit);
    double wt       = 2.0 * M_PI * fn_fit / fs;
    double target_t = P0 / sqrt(1.0 + B * (double)nfit * nfit);

    double a = 0.0;
    if (M > 0 && B > 0.0 && wt < M_PI && nfit > 1) {
        /* D_total(wt) is monotonically increasing in a over (-0.99, 0).
         * f(a) = D_total(wt) - target_t : negative at a=-0.99, positive near 0. */
        double alo = -0.99, ahi = -1e-4;
        for (it = 0; it < 60; it++) {
            double am    = 0.5 * (alo + ahi);
            double rem0  = P0 - Dloss0 - M * pd_allpass(w0, am); /* tuner+intdelay */
            double dtot  = rem0 + pd_onepole(wt, d) + M * pd_allpass(wt, am);
            double f     = dtot - target_t;
            if (f > 0.0) ahi = am; else alo = am;
        }
        a = 0.5 * (alo + ahi);
    }
    s->disp_a = a;

    /* --- size the integer delay; shed dispersion sections if we run out of
     * room (high notes have a short loop). --- */
    double rem = P0 - Dloss0 - M * pd_allpass(w0, a);
    while (M > 0 && rem < 1.0) {
        M--;
        rem = P0 - Dloss0 - M * pd_allpass(w0, a);
    }
    s->disp_n = M;

    int    Nint = (int)floor(rem);
    double frac = rem - Nint;
    if (Nint < 1) { Nint = 1; frac = rem - 1.0; if (frac < 0.0) frac = 0.0; }
    if (Nint > PF_MAX_DELAY - 1) Nint = PF_MAX_DELAY - 1;
    s->dl_len = Nint;

    /* first-order allpass tuned to a fractional delay of `frac` samples */
    s->tune_c = (1.0 - frac) / (1.0 + frac);

    /* --- hammer --- */
    s->ham_m   = p->hammer_mass;
    s->ham_K   = p->hammer_stiffness;
    s->ham_p   = p->hammer_exponent;
    s->g_inj   = p->injection;
    s->out_gain = p->output_gain;
}

void pf_string_strike(pf_string *s, double velocity)
{
    if (velocity < 0.0) velocity = 0.0;
    if (velocity > 1.0) velocity = 1.0;
    s->ham_pos   = 0.0;
    s->ham_vel   = velocity * /* launch toward the string */ 5.0;
    s->ham_engaged   = 1;
    s->ham_contacted = 0;
    s->last_F    = 0.0;
    s->active    = 1;
    s->dbg_contact_samples = 0;
}

void pf_string_process(pf_string *s, float *out, int n)
{
    double dt = 1.0 / s->sr;
    int i, k;

    for (i = 0; i < n; i++) {
        /* read the wave that has travelled around the loop */
        int ri = s->dl_pos - s->dl_len;
        if (ri < 0) ri += PF_MAX_DELAY;
        double r = s->dl[ri];

        /* fractional-delay tuner allpass */
        double ty = s->tune_c * (r - s->tune_y1) + s->tune_x1;
        s->tune_x1 = r; s->tune_y1 = ty; r = ty;

        /* loss / loop gain (frequency-dependent decay) */
        double ly = s->loss_b0 * r + s->loss_a1 * s->loss_y1;
        s->loss_y1 = ly; r = ly;

        /* dispersion allpass cascade */
        for (k = 0; k < s->disp_n; k++) {
            double x = r;
            double y = s->disp_a * (x - s->disp_y1[k]) + s->disp_x1[k];
            s->disp_x1[k] = x; s->disp_y1[k] = y; r = y;
        }

        /* string displacement at the contact point, before this sample's strike */
        double ys_free = r;

        /* nonlinear felt hammer: resolve the delay-free force loop implicitly.
         * The injected force raises the local string displacement by g_inj*F,
         * which reduces the compression that produced it:
         *     F = K * (delta0 - g_inj*F)^p ,  delta0 = ham_pos - ys_free
         * Solve with a few Newton iterations, warm-started from last sample. */
        double F = 0.0;
        if (s->ham_engaged) {
            double d0 = s->ham_pos - ys_free;
            if (d0 > 0.0) {
                F = s->last_F;
                for (k = 0; k < 5; k++) {
                    double comp = d0 - s->g_inj * F;
                    if (comp <= 0.0) { F = 0.0; break; }
                    double cp   = pow(comp, s->ham_p);
                    double cpm  = pow(comp, s->ham_p - 1.0);
                    double g    = F - s->ham_K * cp;
                    double gp   = 1.0 + s->ham_K * s->ham_p * cpm * s->g_inj;
                    F -= g / gp;
                    if (F < 0.0) F = 0.0;
                }
            }
            s->last_F = F;

            if (F > 0.0) { s->ham_contacted = 1; s->dbg_contact_samples++; }
            else if (s->ham_contacted) s->ham_engaged = 0; /* hammer has left */

            /* hammer dynamics: the string force decelerates the hammer */
            s->ham_vel -= (F / s->ham_m) * dt;
            s->ham_pos += s->ham_vel * dt;
        }

        /* inject the hammer force and close the loop */
        double junction = ys_free + s->g_inj * F;
        s->dl[s->dl_pos] = (float)junction;
        s->dl_pos++;
        if (s->dl_pos >= PF_MAX_DELAY) s->dl_pos = 0;

        /* DC-block the output (the one-sided hammer force adds DC) */
        double dy = junction - s->dc_x1 + PF_DC_R * s->dc_y1;
        s->dc_x1 = junction; s->dc_y1 = dy;

        out[i] += (float)(dy * s->out_gain);
    }
}
