/* pf_resonance.h - sympathetic string resonance for the partial-model piano (pf_partial).
 * Core module: portable C, no allocation, no platform headers.
 *
 * A struck string's vibration reaches the bridge and every other string whose damper is
 * off (held keys, sostenuto-captured keys, the undamped top ~1.5 octaves, and with the
 * sustain pedal down all 88).  Those strings ring at THEIR OWN partial frequencies: with
 * the pedal down a lone C4 on Pianoteq "Steinway D Close Mic Classical" grows a halo of
 * other strings' partials 20-28 dB under its own (43-60 dB under with the pedal up),
 * strongest at the semitone neighbours (B3, C#4, B4, C#5 for a C4), concentrated in the
 * long-ringing 120-1000 Hz strings, and decaying slower than the note; partials of other
 * strings that coincide with the note's own extend its sustain (+9.5 dB at 1.5-3 s).
 *
 * Model (after Bank, Zambon & Fontana 2010, "secondary resonators"): one bank of second-
 * order resonators per string, one per partial (frequencies and free decay taken from the
 * tonal patch so they agree with the played voices).  Two couplings:
 *  1. Impulsive: a note-on injects free vibration into every open string's resonators,
 *     proportional to the new note's partial amplitudes seen through a narrow Lorentzian
 *     skirt |1/(1 + j(f - f_p)/skirt_hz)|: the sudden onset of each partial excites nearby
 *     string modes, less as they lie further away (6 dB per doubling of the detuning),
 *     which reproduces the semitone-neighbour hierarchy of the measurement.
 *  2. Sustained: the resonators are also driven by the mix with a gain normalized so a
 *     partial sitting exactly on a string mode builds up (over that mode's time constant)
 *     to at most sustain_db of the driver.  A raw driven resonator has no such bound (its
 *     resonant gain is ~1/(1-r), +60 dB for a bass string) because the model has no energy
 *     exchange back to the driver; the bound stands in for that.
 * Dampers follow the same law as the voices' (pf_pedal_params): a seated damper adds
 * damp_rate_db_s of decay, scaled by the engagement, and strings from `undamped_from` up
 * have none.  A string being played by a voice is not driven (the voice already is the
 * string) and is skipped at its own note-on. */
#ifndef PF_RESONANCE_H
#define PF_RESONANCE_H
#include "pf_partial.h"
#define PF_RES_STRINGS 88
#define PF_RES_PARTIALS 8
typedef struct {
    float coupling_db;   /* impulsive coupling: free amplitude of a coincident string partial re the note's partial */
    float skirt_hz;      /* Lorentzian half-width of the impulsive excitation around each partial of the note */
    float sustain_db;    /* bound of the driven (steady-state) response of a coincident partial re the driver */
    float tilt_db;       /* coupling change per octave of resonator frequency above 250 Hz (bridge admittance / radiation) */
    int   partials;      /* resonators per string, <= PF_RES_PARTIALS */
    float t60_scale, t60_min, t60_max;   /* free decay: from the patch's late envelope slope, scaled and clamped (s) */
    float damp_rate_db_s;                /* extra decay with the damper seated */
    float damp_p_hi, damp_p_lo, damp_curve; /* pedal engagement, as pf_pedal_params */
    int   undamped_from; /* MIDI note from which strings have no damper */
} pf_resonance_params;
typedef struct {
    double sr; int np; double mix;
    int count[PF_RES_STRINGS];                                  /* resonators of this string (below Nyquist) */
    double f[PF_RES_STRINGS][PF_RES_PARTIALS];                  /* Hz */
    double r[PF_RES_STRINGS][PF_RES_PARTIALS];                  /* free-decay radius per sample */
    double drate[PF_RES_STRINGS][PF_RES_PARTIALS];              /* seated-damper extra decay, nepers per sample */
    double a1[PF_RES_STRINGS][PF_RES_PARTIALS], a2[PF_RES_STRINGS][PF_RES_PARTIALS]; /* current coefficients (with damper) */
    double gimp[PF_RES_STRINGS][PF_RES_PARTIALS];               /* impulsive coupling gain (incl. sin w normalization) */
    double gsus[PF_RES_STRINGS][PF_RES_PARTIALS];               /* driven coupling gain (bounded) */
    double y1[PF_RES_STRINGS][PF_RES_PARTIALS], y2[PF_RES_STRINGS][PF_RES_PARTIALS];
    double open[PF_RES_STRINGS];                                /* 1 = damper off the string, 0 = seated */
    int active[PF_RES_STRINGS];                                 /* has energy or is open: processed this block */
    double p_hi, p_lo, p_curve; float skirt; double sin_w[PF_RES_STRINGS][PF_RES_PARTIALS];
} pf_resonance;
void pf_resonance_defaults(pf_resonance_params *p);
void pf_resonance_init(pf_resonance *r, const pf_resonance_params *p, const pf_partial_patch *patch, double sr);
void pf_resonance_reset(pf_resonance *r);                          /* silence, keep the damper state */
double pf_resonance_engaged(const pf_resonance *r, double pedal);  /* damper engagement 0..1 for a pedal position */
void pf_resonance_open(pf_resonance *r, int midi, double open);    /* damper state of one string, 0 (seated) .. 1 (free) */
void pf_resonance_strike(pf_resonance *r, const pf_partial *voice, int midi); /* note-on: impulsive excitation of the open strings */
/* Adds the sympathetic output to out[0..n) while reading the dry mix from in[] (in may be out).
 * voiced[s] != 0 marks strings currently played by a voice: they are not driven. */
void pf_resonance_process(pf_resonance *r, const float *in, float *out, int n, const unsigned char *voiced);
#endif
