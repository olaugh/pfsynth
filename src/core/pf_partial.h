/* Physics-informed parametric piano experiment, inspired by Simionato et al.
 * (2024), not a reproduction of their neural estimator. Pure C, no samples. */
#ifndef PF_PARTIAL_H
#define PF_PARTIAL_H
#define PF_PARTIAL_MODES 64
#define PF_PARTIAL_POINTS 10
#define PF_PARTIAL_ANCHORS 30   /* A0..C8 every 3 semitones (MIDI 21..108): the Salamander sample grid */
#define PF_PARTIAL_LAYERS 2
typedef struct {
    float tuning[PF_PARTIAL_ANCHORS][2]; /* measured f1 / equal-tempered f1, B */
    unsigned char envelope[PF_PARTIAL_ANCHORS][PF_PARTIAL_LAYERS][PF_PARTIAL_MODES][PF_PARTIAL_POINTS];
    unsigned char phase[PF_PARTIAL_ANCHORS][PF_PARTIAL_LAYERS][PF_PARTIAL_MODES];
} pf_partial_patch;
/* Optional pedal model (experiments/pedal): a continuous damper for the sustain pedal and
 * an una corda (soft pedal) timbre.  Fitted as a black box to Pianoteq "Steinway D Close Mic
 * Classical".  Passing NULL to pf_partial_init2 (or using pf_partial_init) keeps the legacy
 * behaviour: fixed 240 ms release, no soft pedal, pedal position ignored. */
typedef struct {
    /* Sustain pedal position p in 0..1 (1 = fully down).  The damper felt first touches the
     * string at p_hi and is fully seated at p_lo; engagement = clamp((p_hi-p)/(p_hi-p_lo))^curve. */
    float damp_p_hi, damp_p_lo, damp_curve;
    /* Full-damper extra decay rate (dB/s) at 100 Hz and at >= 2 kHz, log-interpolated in partial frequency,
     * and the floor (dB) below which the damper stops acting: the second string polarization survives the
     * felt, so Pianoteq's damping stalls ~45 dB under the level at release and the residual decays naturally. */
    float damp_rate_lo, damp_rate_hi, damp_floor_db;
    /* Una corda at full soft pedal (s = 1): level cut (dB) of partial 1, extra cut per octave of
     * partial index at velocity 80, its change per 30 velocity steps, and the curve s^soft_curve. */
    float soft_db, soft_tilt_db, soft_tilt_vel, soft_curve;
    /* Una corda: the unstruck third string sustains the fundamental (partials 1-2 decay slower), dB/s at s = 1, capped. */
    float soft_sustain_db_s, soft_sustain_max_db;
} pf_pedal_params;
typedef struct {
    int count, released, age;
    double sr, release_gain, release_rate;
    double re[PF_PARTIAL_MODES][2], im[PF_PARTIAL_MODES][2];
    double cr[PF_PARTIAL_MODES][2], ci[PF_PARTIAL_MODES][2];
    double amplitude[PF_PARTIAL_MODES][PF_PARTIAL_POINTS];
    /* pedal model state (unused when pedal_model == 0) */
    int pedal_model, key_down;
    double pedal_pos, engaged, p_hi, p_lo, p_curve;
    double drate[PF_PARTIAL_MODES];   /* full-damper decay, nepers per sample */
    double dfac[PF_PARTIAL_MODES];    /* current per-sample damping factor */
    double damp[PF_PARTIAL_MODES];    /* accumulated damper gain */
    double sgrow, scap;               /* una corda fundamental sustain: per-sample growth and cap */
    double onset_s;                   /* raised-cosine onset length: 4 ms from C3 up, longer below (bass notes build over 20-40 ms) */
    int seg; long seg_end;            /* envelope segment being played and the sample index where it ends */
    double cur[PF_PARTIAL_MODES], step[PF_PARTIAL_MODES]; /* incremental envelope: current amplitude and per-sample ratio */
    double dfloor;                    /* damper gain floor */
} pf_partial;
void pf_pedal_defaults(pf_pedal_params *p);
double pf_onset_seconds(double f1);   /* register-dependent onset ramp length (shared with pf_attack) */
void pf_partial_init(pf_partial *v, const pf_partial_patch *patch, double sr, double midi, double velocity);
/* As pf_partial_init, with the pedal model; `soft` is the una corda amount 0..1 at note-on. */
void pf_partial_init2(pf_partial *v, const pf_partial_patch *patch, double sr, double midi, double velocity, const pf_pedal_params *pedal, double soft);
void pf_partial_release(pf_partial *v);                 /* key up: the damper follows the pedal position */
void pf_partial_pedal(pf_partial *v, double position);  /* sustain pedal 0..1; may change at any time */
void pf_partial_process(pf_partial *v, float *out, int n);
#endif
