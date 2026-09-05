/* Physics-informed parametric piano experiment, inspired by Simionato et al.
 * (2024), not a reproduction of their neural estimator. Pure C, no samples. */
#ifndef PF_PARTIAL_H
#define PF_PARTIAL_H
#define PF_PARTIAL_MODES 64
#define PF_PARTIAL_POINTS 10
#define PF_PARTIAL_ANCHORS 9
#define PF_PARTIAL_LAYERS 2
typedef struct {
    float tuning[PF_PARTIAL_ANCHORS][2]; /* measured f1 / equal-tempered f1, B */
    unsigned char envelope[PF_PARTIAL_ANCHORS][PF_PARTIAL_LAYERS][PF_PARTIAL_MODES][PF_PARTIAL_POINTS];
    unsigned char phase[PF_PARTIAL_ANCHORS][PF_PARTIAL_LAYERS][PF_PARTIAL_MODES];
} pf_partial_patch;
typedef struct {
    int count, released, age;
    double sr, release_gain, release_rate;
    double re[PF_PARTIAL_MODES][2], im[PF_PARTIAL_MODES][2];
    double cr[PF_PARTIAL_MODES][2], ci[PF_PARTIAL_MODES][2];
    double amplitude[PF_PARTIAL_MODES][PF_PARTIAL_POINTS];
} pf_partial;
void pf_partial_init(pf_partial *v, const pf_partial_patch *patch, double sr, double midi, double velocity);
void pf_partial_release(pf_partial *v);
void pf_partial_process(pf_partial *v, float *out, int n);
#endif
