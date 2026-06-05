/* pf_string.h - single dispersive waveguide piano string voice.
 *
 * Part of pfsynth's SYNTHESIS KERNEL. This code eventually ships inside a 64k
 * intro as the replayer, so: portable C only, no platform headers, no dynamic
 * allocation. The struct is fully defined here so the host can stack/static-
 * allocate voices. See CLAUDE.md.
 */
#ifndef PF_STRING_H
#define PF_STRING_H

/* Longest delay line. 2048 samples @ 44.1kHz reaches down to ~21.5 Hz, below
 * the lowest piano note (A0, 27.5 Hz). */
#define PF_MAX_DELAY 2048
/* Most dispersion allpass sections we'll ever cascade. */
#define PF_MAX_DISPERSION 16
/* Coupled strings per note (the 2-3 unison strings + polarizations). */
#define PF_MAX_STRINGS 3

/* Patch parameters. A "song" eventually carries a handful of these floats. */
typedef struct {
    double sample_rate;          /* Hz, e.g. 44100 */

    /* String / loop */
    double inharmonicity;        /* B in f_n ~ n*f0*sqrt(1+B*n^2), at A4 (440 Hz) */
    double inharm_pitch;         /* B scales as (f0/440)^this across the keyboard */
    double decay_t60;            /* seconds for the fundamental to drop 60 dB, at A4 */
    double decay_pitch;          /* T60 scales as (f0/440)^this (negative: treble faster) */
    double release_t60;          /* seconds for the fundamental to drop 60 dB once
                                  * the damper falls (note-off). ~0.1..0.3 */
    double damping;              /* extra high-frequency loss, [0,1). Brightness. */
    int    dispersion_sections;  /* M first-order allpasses (0..PF_MAX_DISPERSION) */

    /* Coupled strings (beating + two-stage decay) */
    int    unison_strings;       /* 1..PF_MAX_STRINGS loops coupled at the bridge */
    double unison_detune;        /* total pitch spread across them, in cents */
    double coupling;             /* bridge-load coupling; sets the prompt-decay rate */

    /* Hammer strike point: fraction of the string length (~1/8). Notches the
     * partials with a node there (n = 1/pos, 2/pos, ...). 0 = no comb. */
    double strike_pos;

    /* Nonlinear felt hammer */
    double hammer_mass;          /* kg-ish */
    double hammer_stiffness;     /* K in F = K*delta^p (at the reference velocity) */
    double hammer_exponent;      /* p, felt hardening (~2..3) */
    double hammer_vmax;          /* hammer launch velocity at velocity==1 */
    double hammer_vel_hardness;  /* how strongly strike velocity stiffens the felt:
                                  * K scales as (vel/0.6)^this. 0 = velocity-independent;
                                  * higher = harder/brighter ff, softer/mellower pp. */
    double injection;            /* g_inj: force->displacement coupling per sample */

    double output_gain;          /* final scaling (host may re-normalize anyway) */
    double output_pitch;         /* per-register loudness: out_gain *= (f0/110)^this above
                                  * 110 Hz. Fit so treble isn't ~20 dB too quiet vs a real
                                  * grand (the bare string + body under-radiate the treble). */
} pf_string_params;

/* One waveguide loop: delay line + fractional tuner + loss + dispersion.
 * A note is PF_MAX_STRINGS of these, slightly detuned and coupled at the bridge. */
typedef struct {
    float  dl[PF_MAX_DELAY];     /* the loop */
    int    dl_len;               /* integer part of the loop delay */
    int    dl_pos;               /* circular write cursor */

    double tune_c, tune_x1, tune_y1;            /* fractional-delay tuner allpass */

    double loss_b0, loss_b0_rel, loss_a1, loss_y1;  /* one-pole loss / loop gain */

    int    disp_n;                              /* dispersion allpass count */
    double disp_a;                              /* dispersion coefficient */
    double disp_x1[PF_MAX_DISPERSION];
    double disp_y1[PF_MAX_DISPERSION];

    double ham_gain;             /* how strongly the hammer drives this loop */
} pf_substring;

typedef struct pf_string {
    int    active;
    double sr;
    double f0;
    int    released;             /* damper has fallen (note-off) */

    /* Coupled string bank. */
    int          n_strings;
    pf_substring str[PF_MAX_STRINGS];
    double       couple;         /* bridge-load coupling coefficient (mu) */

    /* Hammer strike-point comb: the injected excitation is high-passed by
     * (1 - z^-beta), notching partials with a node at the strike point. The
     * delayed term is the wave reflecting off the near end back to the hammer. */
    int    strike_beta;          /* comb delay (samples); 0 = disabled */
    int    strike_hpos;          /* excitation-history write cursor */
    float  strike_hist[PF_MAX_DELAY];

    /* Nonlinear felt hammer (shared across the coupled strings). */
    int    ham_engaged;          /* hammer is in flight / touching */
    int    ham_contacted;        /* has been in compression at least once */
    double ham_pos, ham_vel;     /* hammer displacement / velocity */
    double ham_m, ham_K, ham_p;
    double ham_K0;               /* base stiffness; strike scales ham_K from this */
    double ham_vhard;            /* velocity -> stiffness exponent */
    double last_F;               /* warm-start for the implicit solve */

    double g_inj;
    double out_gain;

    /* DC blocker on the output (the nonlinear injection has a DC component). */
    double dc_x1, dc_y1;

    /* Diagnostics for the dev harness (mirror string 0; not used by the model). */
    int    dl_len, disp_n;
    long   dbg_contact_samples;
} pf_string;

/* Fill params with sensible mid-piano defaults. */
void pf_string_defaults(pf_string_params *p, double sample_rate);

/* Build one voice tuned to f0 (Hz). Resets all state. */
void pf_string_init(pf_string *s, const pf_string_params *p, double f0);

/* Launch the hammer. velocity in (0,1]. Does NOT reset string state, so a
 * re-strike interacts with the still-ringing string. Clears the damper. */
void pf_string_strike(pf_string *s, double velocity);

/* Drop the damper onto the string (note-off): the loop switches to the much
 * faster release decay. A held sustain pedal means the host simply doesn't
 * call this. */
void pf_string_release(pf_string *s);

/* Render n samples, ADDING them into out[] (mix). */
void pf_string_process(pf_string *s, float *out, int n);

#endif /* PF_STRING_H */
