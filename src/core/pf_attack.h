/* pf_attack.h - onset component for the pf_partial tonal piano: body/room "thump"
 * plus a short hammer-noise burst.  Core module: portable C, no allocation.
 *
 * Measurements on the Salamander grand (experiments/attack) show the tonal model
 * reproduces the partials but lacks two things a recording has at every onset:
 *
 *  1. A sub-fundamental THUMP: the hammer impact and key bottoming excite the
 *     soundboard/case and the room broadband.  A fixed set of low modes (~25-450
 *     Hz, 0.5-2 s decays) rings under every note, at an absolute level that is
 *     roughly register-independent and grows with velocity.  It is 15-30 dB under
 *     the fundamental for C4-C6 and without it treble notes sound thin.
 *     Modeled as a bank of second-order resonators (shared mode set, fitted from
 *     data) struck by a short pulse whose gain depends on register and velocity.
 *  2. A broadband NOISE burst (felt/impact noise) between the partials in the
 *     first tens of milliseconds.  Modeled as filtered white noise with an
 *     exponential envelope.
 *
 * This is a compact, generated component in the spirit of the transient/noise
 * modules of Simionato & Fasciani (2025), NOT a reproduction of their neural
 * networks: no stored waveforms, no weights.
 *
 * Because the thump bank is linear, a real-time engine may run ONE shared bank fed
 * by every voice's pulse (commuted synthesis); this per-voice form is equivalent
 * by superposition and is what the offline harness uses.
 */
#ifndef PF_ATTACK_H
#define PF_ATTACK_H
#define PF_ATTACK_MODES 64   /* slow room modes + fast soundboard "knock" modes, one bank */
#define PF_ATTACK_ANCHORS 9   /* C2..C6 every 6 semitones, same grid as pf_partial */
#define PF_ATTACK_LAYERS 2    /* Salamander layers 6 (vel 48) and 13 (vel 100) */
typedef struct {
    float mode_hz[PF_ATTACK_MODES];
    float mode_t60[PF_ATTACK_MODES];
    float mode_db[PF_ATTACK_MODES];      /* shared mode weights, dB (relative) */
    float mode_delay_ms[PF_ATTACK_MODES];/* per-mode strike delay after note-on (the low body/key thump peaks ~15-20 ms after the tone) */
    float pulse_ms;                       /* minimum excitation pulse length (Hann) */
    float pulse_cycles;                   /* pulse length is at least this many periods of the mode (0.5 = half a period: low modes get a gentle strike, no broadband onset skirt) */
    float thump_db[PF_ATTACK_ANCHORS][PF_ATTACK_LAYERS]; /* excitation level, dBFS of the reference recordings */
    float noise_db[PF_ATTACK_ANCHORS][PF_ATTACK_LAYERS]; /* noise burst peak RMS level, dBFS */
    float noise_ms[PF_ATTACK_ANCHORS][PF_ATTACK_LAYERS]; /* noise burst time to -60 dB */
    float noise_hz[PF_ATTACK_ANCHORS][PF_ATTACK_LAYERS]; /* two-pole (2x one-pole) lowpass cutoff of the noise */
    float thump_mix, noise_mix;           /* audition trims, 1 = as fitted, 0 = off */
} pf_attack_patch;
typedef struct {
    int modes, age; double sr;
    double a1[PF_ATTACK_MODES], a2[PF_ATTACK_MODES], g[PF_ATTACK_MODES];
    double y1[PF_ATTACK_MODES], y2[PF_ATTACK_MODES];
    int delay[PF_ATTACK_MODES], plen[PF_ATTACK_MODES]; double pgain[PF_ATTACK_MODES];
    double noise_gain, noise_rate, lp_a, lp_z1, lp_z2; unsigned rng;
} pf_attack;
void pf_attack_init(pf_attack *v, const pf_attack_patch *p, double sr, double midi, double velocity);
void pf_attack_process(pf_attack *v, float *out, int n);   /* additive */
#endif
