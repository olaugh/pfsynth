/* Experimental fixed-pole parallel body filter. Not selected by live engine. */
#ifndef PF_BODYFIT_H
#define PF_BODYFIT_H
#define PF_BODYFIT_MAX 64
typedef struct {
    int count;
    float direct;
    /* Each section: b0, b1, a1, a2. a0 is one; b2 is zero. */
    float section[PF_BODYFIT_MAX][4];
} pf_bodyfit_params;
typedef struct {
    pf_bodyfit_params params;
    double z1[PF_BODYFIT_MAX], z2[PF_BODYFIT_MAX];
} pf_bodyfit;
void pf_bodyfit_init(pf_bodyfit *b, const pf_bodyfit_params *p);
void pf_bodyfit_process(pf_bodyfit *b, const float *input, float *output, int n);
#endif
