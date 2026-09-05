#include "pf_bodyfit.h"
#include <string.h>

void pf_bodyfit_init(pf_bodyfit *b, const pf_bodyfit_params *p)
{
    memset(b, 0, sizeof *b);
    b->params = *p;
    if (b->params.count < 0) b->params.count = 0;
    if (b->params.count > PF_BODYFIT_MAX) b->params.count = PF_BODYFIT_MAX;
}

void pf_bodyfit_process(pf_bodyfit *b, const float *input, float *output, int n)
{
    for (int i = 0; i < n; ++i) {
        double x = input[i], sum = b->params.direct * x;
        for (int k = 0; k < b->params.count; ++k) {
            const float *c = b->params.section[k];
            double y = c[0] * x + b->z1[k];
            b->z1[k] = c[1] * x - c[2] * y + b->z2[k];
            b->z2[k] = -c[3] * y;
            sum += y;
        }
        output[i] = (float)sum;
    }
}
