/* pf_reverb.h - compact stereo room reverb for pfsynth.
 *
 * CORE module: pure portable C, no allocation. A piano recording always has a
 * room around it; our render was bone dry (just the soundboard coloration). This
 * is a Freeverb-style reverb (parallel damped feedback combs into series
 * allpasses, stereo), applied once over the final mix to put the instrument in a
 * space. Generated from constants (comb/allpass delay tables), so it ships tiny;
 * the delay buffers are runtime RAM, which the 64k budget doesn't count.
 *
 *     pf_reverb_init(&rev, sample_rate);
 *     pf_reverb_set(&rev, room, damp, wet);     // tweak the space
 *     pf_reverb_tick(&rev, inl, inr, &outl, &outr);
 *     pf_reverb_reset(&rev);                     // clear the tail
 */
#ifndef PF_REVERB_H
#define PF_REVERB_H

#define PF_REV_NCOMB 8
#define PF_REV_NAP   4
#define PF_REV_MAXD  1900   /* longest comb delay + stereo spread, at 44.1 kHz */

typedef struct {
    int   cd[2][PF_REV_NCOMB];                 /* comb delays, [chan][i] */
    int   cp[2][PF_REV_NCOMB];                 /* comb write cursors */
    float cb[2][PF_REV_NCOMB][PF_REV_MAXD];    /* comb buffers */
    float cs[2][PF_REV_NCOMB];                 /* comb damping lowpass state */

    int   ad[2][PF_REV_NAP];                   /* allpass delays */
    int   ap[2][PF_REV_NAP];                   /* allpass write cursors */
    float ab[2][PF_REV_NAP][PF_REV_MAXD];      /* allpass buffers */

    double feedback;          /* comb feedback (room size) */
    double damp1, damp2;      /* damping lowpass coefficients */
    double apfb;              /* allpass feedback */
    double wet1, wet2, dry;   /* output mix (wet1/wet2 encode stereo width) */
} pf_reverb;

void pf_reverb_init(pf_reverb *r, double sample_rate);
void pf_reverb_set(pf_reverb *r, double room, double damp, double wet);
void pf_reverb_reset(pf_reverb *r);
void pf_reverb_tick(pf_reverb *r, double inl, double inr, double *outl, double *outr);

#endif
