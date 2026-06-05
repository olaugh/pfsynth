/* pf_board.h - modal soundboard / body resonator for the pfsynth piano.
 *
 * CORE module: pure portable C, no allocation, struct fully defined here so the
 * host can stack/static-allocate it. Ships inside the intro as part of the
 * instrument, so it stays small and is generated from a handful of constants
 * rather than any sampled impulse response.
 *
 * The string voice on its own is a bare vibrating wire - thin and synthetic,
 * because in a real piano you mostly hear the SOUNDBOARD the strings drive, not
 * the strings. This models that body as a parallel bank of second-order modal
 * resonators (a coded modal impulse response) added on top of a dry path.
 *
 * Why this is the *right* place for it, not a hack: after the hammer leaves the
 * string the instrument is ~linear and time-invariant, and there is exactly ONE
 * shared soundboard. So filtering the summed mix of all voices through one body
 * is mathematically identical to convolving each note's excitation with the body
 * response ("commuted synthesis") - but costs one filter bank instead of one per
 * voice. The host therefore runs a single pf_board over the final mix.
 *
 *     mix of all string voices --> [ dry + Sigma modal resonators ] --> body tone
 *
 * Usage:
 *     pf_board_defaults(&bp, sample_rate);     // sensible piano-body defaults
 *     pf_board_init(&board, &bp, sample_rate); // build the modal bank
 *     ... per sample: y = pf_board_tick(&board, x);
 *     pf_board_reset(&board);                  // clear ring (on load/seek/panic)
 */
#ifndef PF_BOARD_H
#define PF_BOARD_H

#define PF_BOARD_MAXMODES 48

typedef struct {
    int    modes;       /* number of resonant modes to generate (<= MAXMODES) */
    double f_lo, f_hi;  /* lowest / highest mode frequency (Hz) */
    double t_lo, t_hi;  /* mode decay time at f_lo / f_hi (seconds) */
    double tilt;        /* spectral tilt of mode gains (dB/octave, <0 = darker) */
    double color;       /* mode-gain irregularity 0..1 (0 = smooth, 1 = bumpy) */
    double dry;         /* straight-through string level */
    double mix;         /* modal (body) level; 0 = bypass, audition the bare string */
    unsigned seed;      /* deterministic jitter seed (so the body is reproducible) */
} pf_board_params;

typedef struct {
    int    n;
    double a1[PF_BOARD_MAXMODES];   /* 2 R cos w   */
    double a2[PF_BOARD_MAXMODES];   /* R^2         */
    double g [PF_BOARD_MAXMODES];   /* mode output gain (peak-normalized) */
    double y1[PF_BOARD_MAXMODES];   /* resonator state */
    double y2[PF_BOARD_MAXMODES];
    double dry, mix;
} pf_board;

void   pf_board_defaults(pf_board_params *p, double sample_rate);
void   pf_board_init(pf_board *b, const pf_board_params *p, double sample_rate);
void   pf_board_reset(pf_board *b);                 /* clear resonator state */
double pf_board_tick(pf_board *b, double x);        /* one sample */
void   pf_board_process(pf_board *b, float *buf, int n);  /* in place over a block */

#endif
