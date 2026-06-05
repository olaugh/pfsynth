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
    /* inharmonicity and decay are the REFERENCE values at A4 (440 Hz); pf_string_init
     * scales them by pitch (see below). Fit to a Salamander Grand (Yamaha C5). */
    p->inharmonicity       = 3.5e-4;   /* requested B at A4; the (approximate) dispersion
                                        * realizes ~1.8x this, so output B(A4) ~= 6.5e-4,
                                        * matching the measured Salamander Grand */
    p->inharm_pitch        = 1.5;      /* fit: B ~ f0^1.5 (treble much stiffer) */
    p->decay_t60           = 5.75;     /* T60 at A4; rises toward the bass */
    p->decay_pitch         = -0.6;     /* fit: T60 ~ f0^-0.6 (treble decays faster) */
    p->release_t60         = 0.15;     /* damper kills the note quickly */
    p->damping             = 0.20;
    p->dispersion_sections = 8;

    p->unison_strings      = 2;        /* two coupled loops -> beating + double decay */
    p->unison_detune       = 2.0;      /* cents of spread; sets the beat rate */
    p->coupling            = 0.006;    /* bridge load: common-mode (prompt) decay rate */
    p->strike_pos          = 0.12;     /* hammer ~1/8 along the string (0 = off) */

    p->hammer_mass         = 4.0e-3;   /* ~4 g */
    p->hammer_stiffness    = 4.5e7;    /* harder felt -> brighter, more present attack */
    p->hammer_exponent     = 2.3;
    p->hammer_vmax         = 5.0;
    p->hammer_vel_hardness = 1.0;      /* velocity stiffens the felt: mellow pp, bright ff */
    /* force -> displacement injection per sample = dt/(2Z), Z ~ 2 */
    p->injection           = 1.0 / (2.0 * 2.0) / sample_rate;

    p->output_gain         = 1.0;
    p->output_pitch        = 0.8;      /* treble makeup, fit to the Salamander balance */
}

/* Build one waveguide loop (delay + fractional tuner + loss + dispersion) tuned
 * to f0k. This is the per-string init; a note is several of these, detuned. */
static void init_substring(pf_substring *ss, const pf_string_params *p, double f0k)
{
    double fs = p->sample_rate;
    double w0 = 2.0 * M_PI * f0k / fs;
    double P0 = fs / f0k;                 /* loop delay for the fundamental */
    double B  = p->inharmonicity;
    double d  = p->damping;
    int    M  = p->dispersion_sections;
    int    n, it;

    /* --- loss filter: pick loop gain g for the target fundamental T60 --- */
    double g   = pow(10.0, -3.0 / (f0k * p->decay_t60)); /* g^(f0*T60) = 1e-3 */
    double grl = pow(10.0, -3.0 / (f0k * p->release_t60));
    ss->loss_b0     = g   * (1.0 - d);
    ss->loss_b0_rel = grl * (1.0 - d);
    ss->loss_a1     = d;
    double Dloss0 = pd_onepole(w0, d);

    /* --- dispersion: fit one allpass coefficient to the target stretch --- *
     * Highest partial that still fits below Nyquist (the stretched formula
     * pushes partials up, so this is well below f0-harmonic count for bass). */
    int nmax = 1;
    for (n = 1; n <= 4096; n++) {
        double fn = n * f0k * sqrt(1.0 + B * (double)n * n);
        if (fn >= fs * 0.5) { nmax = n - 1; break; }
        nmax = n;
    }
    if (nmax < 1) nmax = 1;

    /* Fit at a moderate partial: the low partials carry the audible stretch and
     * fitting at the very top would force an extreme coefficient. */
    int nfit = nmax < 24 ? nmax : 24;
    double fn_fit   = nfit * f0k * sqrt(1.0 + B * (double)nfit * nfit);
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
    ss->disp_a = a;

    /* --- size the integer delay; shed dispersion sections if we run out of
     * room (high notes have a short loop). --- */
    double rem = P0 - Dloss0 - M * pd_allpass(w0, a);
    while (M > 0 && rem < 1.0) {
        M--;
        rem = P0 - Dloss0 - M * pd_allpass(w0, a);
    }
    ss->disp_n = M;

    int    Nint = (int)floor(rem);
    double frac = rem - Nint;
    if (Nint < 1) { Nint = 1; frac = rem - 1.0; if (frac < 0.0) frac = 0.0; }
    if (Nint > PF_MAX_DELAY - 1) Nint = PF_MAX_DELAY - 1;
    ss->dl_len = Nint;

    /* first-order allpass tuned to a fractional delay of `frac` samples */
    ss->tune_c = (1.0 - frac) / (1.0 + frac);
}

void pf_string_init(pf_string *s, const pf_string_params *p, double f0)
{
    memset(s, 0, sizeof *s);
    s->sr = p->sample_rate;
    s->f0 = f0;

    int N = p->unison_strings;
    if (N < 1) N = 1;
    if (N > PF_MAX_STRINGS) N = PF_MAX_STRINGS;
    s->n_strings = N;
    s->couple    = (N > 1) ? p->coupling : 0.0;   /* lone string: no bridge load */

    /* Pitch-scale the reference (A4) inharmonicity and decay, fit to a real grand:
     *   B(f0)   = B_A4   * (f0/440)^1.5   -- stiffer/sharper partials in the treble
     *   T60(f0) = T60_A4 * (440/f0)^0.6   -- bass rings long, treble dies fast
     * (curve shape baked in here; the params carry the A4 magnitudes.) */
    pf_string_params sp = *p;
    double pitch = f0 / 440.0;
    sp.inharmonicity = p->inharmonicity * pow(pitch, p->inharm_pitch);
    sp.decay_t60     = p->decay_t60     * pow(pitch, p->decay_pitch);

    for (int k = 0; k < N; k++) {
        /* spread the strings symmetrically around f0 by unison_detune cents */
        double cents = (N > 1) ? ((double)k / (N - 1) - 0.5) * p->unison_detune : 0.0;
        double f0k   = f0 * pow(2.0, cents / 1200.0);
        init_substring(&s->str[k], &sp, f0k);
        /* a slight per-string hammer asymmetry guarantees the slow differential
         * mode is excited, so there's an audible aftersound, not just a prompt */
        s->str[k].ham_gain = 1.0 - 0.04 * (double)k;
    }

    /* strike-point comb delay: beta = strike_pos * (period in samples) */
    s->strike_beta = 0;
    if (p->strike_pos > 0.0 && p->strike_pos < 0.5) {
        int beta = (int)floor(p->strike_pos * (p->sample_rate / f0) + 0.5);
        if (beta >= 1 && beta < PF_MAX_DELAY) s->strike_beta = beta;
    }

    /* shared hammer */
    s->ham_m     = p->hammer_mass;
    s->ham_K0    = p->hammer_stiffness;        /* base; strike scales by velocity */
    s->ham_K     = p->hammer_stiffness;
    s->ham_p     = p->hammer_exponent;
    s->ham_vhard = p->hammer_vel_hardness;
    s->g_inj     = p->injection;
    /* per-register loudness makeup: lift the treble so it isn't buried (real
     * grands radiate the treble far better than a bare waveguide + body do).
     * Capped so the very top octave doesn't overshoot. */
    double og = pow((f0 > 110.0 ? f0 : 110.0) / 110.0, p->output_pitch);
    if (og > 10.0) og = 10.0;
    s->out_gain  = p->output_gain * og;

    /* harness mirrors (string 0) */
    s->dl_len = s->str[0].dl_len;
    s->disp_n = s->str[0].disp_n;
}

void pf_string_strike(pf_string *s, double velocity)
{
    if (velocity < 0.0) velocity = 0.0;
    if (velocity > 1.0) velocity = 1.0;

    /* Velocity stiffens the felt: a fast strike meets a harder hammer (shorter
     * contact -> brighter), a slow one a softer hammer (longer contact -> mellow).
     * This is the main expressive dynamics -> timbre coupling. */
    double vh = pow((velocity > 1e-4 ? velocity : 1e-4) / 0.6, s->ham_vhard);
    if (vh < 0.1) vh = 0.1;
    if (vh > 4.0) vh = 4.0;
    s->ham_K = s->ham_K0 * vh;

    s->ham_pos   = 0.0;
    s->ham_vel   = velocity * /* launch toward the string */ 5.0;
    s->ham_engaged   = 1;
    s->ham_contacted = 0;
    s->last_F    = 0.0;
    s->active    = 1;
    s->released  = 0;
    s->dbg_contact_samples = 0;
}

void pf_string_release(pf_string *s)
{
    s->released = 1;
}

void pf_string_process(pf_string *s, float *out, int n)
{
    double dt = 1.0 / s->sr;
    int    N  = s->n_strings;
    double mu = s->couple;
    int i, k, j;

    for (i = 0; i < n; i++) {
        /* advance every coupled loop to the bridge: delay -> tuner -> loss ->
         * dispersion. b[k] is loop k's wave arriving at the bridge; S is the
         * total bridge displacement (what drives the soundboard). */
        double b[PF_MAX_STRINGS];
        double S = 0.0;
        for (k = 0; k < N; k++) {
            pf_substring *ss = &s->str[k];

            int ri = ss->dl_pos - ss->dl_len;
            if (ri < 0) ri += PF_MAX_DELAY;
            double r = ss->dl[ri];

            double ty = ss->tune_c * (r - ss->tune_y1) + ss->tune_x1;
            ss->tune_x1 = r; ss->tune_y1 = ty; r = ty;

            double b0 = s->released ? ss->loss_b0_rel : ss->loss_b0;
            double ly = b0 * r + ss->loss_a1 * ss->loss_y1;
            ss->loss_y1 = ly; r = ly;

            for (j = 0; j < ss->disp_n; j++) {
                double x = r;
                double y = ss->disp_a * (x - ss->disp_y1[j]) + ss->disp_x1[j];
                ss->disp_x1[j] = x; ss->disp_y1[j] = y; r = y;
            }
            b[k] = r;
            S   += r;
        }

        /* the hammer feels the mean string displacement at the contact point */
        double ys_drive = S / N;

        /* Strike-point comb: the excitation injected beta samples ago has by now
         * reflected off the near termination and returned, inverted, to the
         * contact point. The hammer feels it and the loop receives the
         * difference -> a (1 - z^-beta) comb that notches partials with a node
         * at the strike point. */
        double echo = 0.0;
        if (s->strike_beta) {
            int hi = s->strike_hpos - s->strike_beta;
            if (hi < 0) hi += PF_MAX_DELAY;
            echo = s->strike_hist[hi];
        }

        /* nonlinear felt hammer: resolve the delay-free force loop implicitly.
         * The injected force raises the local string displacement by g_inj*F,
         * which reduces the compression that produced it:
         *     F = K * (delta0 - g_inj*F)^p ,  delta0 = ham_pos - ys_drive + echo
         * Solve with a few Newton iterations, warm-started from last sample. */
        double F = 0.0;
        if (s->ham_engaged) {
            double d0 = s->ham_pos - ys_drive + echo;
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

        /* Bridge coupling + hammer injection, then close each loop and sum.
         * in_k = b_k - mu*S subtracts a bridge load proportional to the TOTAL
         * displacement: the common mode (all strings in phase, S large) loses
         * energy fast -> the bright "prompt" attack-sound, while the differential
         * mode (strings out of phase, S~0) is nearly lossless -> the slow singing
         * aftersound. The cent-scale detune makes the loops beat and bleeds
         * energy from the prompt into the aftersound. */
        double e_raw = s->g_inj * F;          /* raw hammer-force injection */
        double e_inj = e_raw - echo;          /* (1 - z^-beta) strike-point comb */
        if (s->strike_beta) {
            s->strike_hist[s->strike_hpos] = (float)e_raw;
            s->strike_hpos++;
            if (s->strike_hpos >= PF_MAX_DELAY) s->strike_hpos = 0;
        }

        double junc_sum = 0.0;
        for (k = 0; k < N; k++) {
            pf_substring *ss = &s->str[k];
            double in_k = b[k] - mu * S + e_inj * ss->ham_gain;
            ss->dl[ss->dl_pos] = (float)in_k;
            ss->dl_pos++;
            if (ss->dl_pos >= PF_MAX_DELAY) ss->dl_pos = 0;
            junc_sum += in_k;
        }
        junc_sum /= N;   /* keep output level ~constant regardless of string count */

        /* DC-block the output (the one-sided hammer force adds DC) */
        double dy = junc_sum - s->dc_x1 + PF_DC_R * s->dc_y1;
        s->dc_x1 = junc_sum; s->dc_y1 = dy;

        out[i] += (float)(dy * s->out_gain);
    }
}
