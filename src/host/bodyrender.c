/* Offline body experiment: identical excitation drives raw/current outputs.
 * Output is native float32 interleaved [source, current-body mono], unnormalized.
 * No changes to the live synth, voice defaults, hammer, or attack component. */
#include "../core/pf_string.h"
#include "../core/pf_board.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

int main(int argc, char **argv)
{
    if (argc != 5) {
        fprintf(stderr, "usage: bodyrender NOTE VELOCITY SECONDS OUTPUT.f32\n");
        return 2;
    }
    int note = atoi(argv[1]);
    double vel = atof(argv[2]), secs = atof(argv[3]);
    if (note < 21 || note > 108 || vel <= 0 || vel > 1 || secs <= 0 || secs > 30) return 2;
    const int sr = 44100, n = (int)(secs * sr);
    float *x = calloc((size_t)n, sizeof(float));
    if (!x) return 1;
    static pf_string voice;
    static pf_board_stereo board;
    pf_string_params p; pf_string_defaults(&p, sr);
    pf_board_params bp; pf_board_defaults(&bp, sr);
    pf_string_init(&voice, &p, 440.0 * pow(2.0, (note - 69) / 12.0));
    pf_string_strike(&voice, vel);
    pf_string_process(&voice, x, n);
    pf_board_stereo_init(&board, &bp, sr);
    FILE *f = fopen(argv[4], "wb");
    if (!f) { free(x); return 1; }
    int rc = 0;
    for (int i = 0; i < n; ++i) {
        double l, r;
        pf_board_stereo_tick(&board, x[i], &l, &r);
        float frame[2] = {x[i], (float)(0.5 * (l + r))};
        if (!isfinite(frame[0]) || !isfinite(frame[1]) || fwrite(frame, sizeof(float), 2, f) != 2) {
            rc = 1; break;
        }
    }
    if (fclose(f)) rc = 1;
    free(x);
    return rc;
}
