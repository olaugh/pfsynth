/* pf_reverb.c - see pf_reverb.h. A Freeverb-style stereo reverb.
 *
 * Each channel: 8 parallel feedback comb filters (each with a one-pole lowpass
 * in the loop, so the tail darkens as it decays, like a real room) summed into 4
 * series allpass filters that smear the echoes into diffuse reverberation. The
 * right channel uses the same delays offset by a few samples ("stereo spread")
 * so the two sides decorrelate. The classic Schroeder/Freeverb tunings are used.
 */
#include "pf_reverb.h"
#include <string.h>

/* Freeverb delay tunings at 44.1 kHz (scaled to the actual rate at init). */
static const int COMB_TUNE[PF_REV_NCOMB] = { 1116, 1188, 1277, 1356, 1422, 1491, 1557, 1617 };
static const int AP_TUNE[PF_REV_NAP]     = { 556, 441, 341, 225 };
static const int STEREO_SPREAD = 23;

#define FIXED_GAIN  0.06   /* reverb send level (louder than stock Freeverb's 0.015,
                            * so a moderate wet is actually present over our dry) */
#define SCALE_DAMP  0.4
#define SCALE_ROOM  0.28
#define OFFSET_ROOM 0.7
#define AP_FEEDBACK 0.5

void pf_reverb_init(pf_reverb *r, double sample_rate)
{
    memset(r, 0, sizeof *r);
    double k = sample_rate / 44100.0;          /* scale tunings to the real rate */
    for (int c = 0; c < 2; c++) {
        int off = (c == 1) ? STEREO_SPREAD : 0;
        for (int i = 0; i < PF_REV_NCOMB; i++) {
            int d = (int)(COMB_TUNE[i] * k) + off;
            if (d < 1) d = 1;
            if (d > PF_REV_MAXD) d = PF_REV_MAXD;
            r->cd[c][i] = d;
        }
        for (int i = 0; i < PF_REV_NAP; i++) {
            int d = (int)(AP_TUNE[i] * k) + off;
            if (d < 1) d = 1;
            if (d > PF_REV_MAXD) d = PF_REV_MAXD;
            r->ad[c][i] = d;
        }
    }
    r->apfb = AP_FEEDBACK;
    pf_reverb_set(r, 0.72, 0.35, 0.30);        /* a medium piano room */
}

void pf_reverb_set(pf_reverb *r, double room, double damp, double wet)
{
    if (room < 0) room = 0; if (room > 1) room = 1;
    if (damp < 0) damp = 0; if (damp > 1) damp = 1;
    if (wet  < 0) wet  = 0; if (wet  > 1) wet  = 1;
    r->feedback = room * SCALE_ROOM + OFFSET_ROOM;
    r->damp1    = damp * SCALE_DAMP;
    r->damp2    = 1.0 - r->damp1;
    /* width fixed at full; wet1/wet2 spread the wet signal across L/R */
    r->wet1 = wet * 0.5 * (1.0 + 1.0);   /* width = 1 */
    r->wet2 = wet * 0.5 * (1.0 - 1.0);
    r->dry  = 1.0;
}

void pf_reverb_reset(pf_reverb *r)
{
    memset(r->cb, 0, sizeof r->cb);
    memset(r->ab, 0, sizeof r->ab);
    memset(r->cs, 0, sizeof r->cs);
    memset(r->cp, 0, sizeof r->cp);
    memset(r->ap, 0, sizeof r->ap);
}

/* one damped feedback comb */
static double comb(pf_reverb *r, int c, int i, double in)
{
    float *buf = r->cb[c][i];
    int p = r->cp[c][i];
    double out = buf[p];
    r->cs[c][i] = (float)(out * r->damp2 + r->cs[c][i] * r->damp1);   /* lowpass */
    buf[p] = (float)(in + r->cs[c][i] * r->feedback);
    if (++p >= r->cd[c][i]) p = 0;
    r->cp[c][i] = p;
    return out;
}

/* one allpass */
static double allpass(pf_reverb *r, int c, int i, double in)
{
    float *buf = r->ab[c][i];
    int p = r->ap[c][i];
    double bufout = buf[p];
    double out = -in + bufout;
    buf[p] = (float)(in + bufout * r->apfb);
    if (++p >= r->ad[c][i]) p = 0;
    r->ap[c][i] = p;
    return out;
}

void pf_reverb_tick(pf_reverb *r, double inl, double inr, double *outl, double *outr)
{
    double input = (inl + inr) * FIXED_GAIN;
    double acc[2] = { 0.0, 0.0 };
    for (int c = 0; c < 2; c++) {
        for (int i = 0; i < PF_REV_NCOMB; i++) acc[c] += comb(r, c, i, input);
        for (int i = 0; i < PF_REV_NAP; i++)   acc[c] = allpass(r, c, i, acc[c]);
    }
    *outl = inl * r->dry + acc[0] * r->wet1 + acc[1] * r->wet2;
    *outr = inr * r->dry + acc[1] * r->wet1 + acc[0] * r->wet2;
}
