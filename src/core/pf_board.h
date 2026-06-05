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
    double stereo_width;/* 0 = mono body, 1 = fully decorrelated L/R (stereo only) */
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

/* Stereo body: one modal bank, then a different Schroeder allpass chain per
 * channel. A (delay-based) allpass has flat magnitude, so both channels keep the
 * bank's exact spectrum and energy -> the image stays perfectly balanced for any
 * signal; only the phase differs, which spreads the resonance L/R. The delay is
 * what lets it decorrelate the low-mid body (a plain one-pole allpass only
 * disperses the highs). The dry string path stays centered so the note image
 * holds. Turns the mono "behind glass" body into something that sits in the room. */
#define PF_BOARD_DECORR     4     /* Schroeder allpass sections per channel */
#define PF_BOARD_DECORR_MAXD 512  /* max delay per section (samples) */

typedef struct {
    pf_board bank;       /* single shared modal bank (dry disabled) */
    double   dry;        /* shared, centered direct path */
    int      nap;        /* active allpass sections per side */
    double   g;          /* allpass feedback gain */
    int      dl[PF_BOARD_DECORR], dr[PF_BOARD_DECORR];   /* per-section delays */
    int      pl[PF_BOARD_DECORR], pr[PF_BOARD_DECORR];   /* ring write cursors */
    float    bl[PF_BOARD_DECORR][PF_BOARD_DECORR_MAXD];
    float    br[PF_BOARD_DECORR][PF_BOARD_DECORR_MAXD];
} pf_board_stereo;

void pf_board_stereo_init(pf_board_stereo *b, const pf_board_params *p, double sample_rate);
void pf_board_stereo_reset(pf_board_stereo *b);
void pf_board_stereo_set_mix(pf_board_stereo *b, double mix);   /* body amount, both sides */
void pf_board_stereo_tick(pf_board_stereo *b, double x, double *outl, double *outr);

#endif
