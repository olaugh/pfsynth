/* midirender.c - render a MIDI file through the engine to a stereo WAV. HOST-ONLY.
 *
 * Offline path mirroring exactly what the TUI's audio thread does (same engine,
 * same params, same master gain + tanh clip), so we can reproduce live-playback
 * issues (clipping, balance) and compare against a real recording.
 *
 *   midirender <file.mid> <out.wav> [seconds] [master_gain]
 */
#include "engine.h"
#include "midi.h"
#include "wav.h"

#include <stdio.h>
#include <stdlib.h>
#include <math.h>

#define SR 44100
#define CHUNK 1024

int main(int argc, char **argv)
{
    if (argc < 3) { fprintf(stderr, "usage: midirender <file.mid> <out.wav> [secs] [master]\n"); return 2; }

    pf_song song;
    if (pf_midi_load(&song, argv[1])) return 1;

    static pf_engine eng;
    pf_engine_init(&eng, SR);
    if (argc > 4) pf_engine_set_master(&eng, atof(argv[4]));
    /* optional param overrides for spectral-matching experiments:
     * [5]=damping [6]=output_pitch [7]=reverb_wet */
    if (argc > 5 || argc > 6) {
        pf_string_params p; pf_string_defaults(&p, SR);
        if (argc > 5) p.damping      = atof(argv[5]);
        if (argc > 6) p.output_pitch = atof(argv[6]);
        pf_engine_set_params(&eng, &p);
    }
    if (argc > 7) pf_engine_set_reverb(&eng, atof(argv[7]));
    pf_engine_load(&eng, &song);   /* takes ownership, zeroes our copy */
    pf_engine_play(&eng);

    double secs = (argc > 3) ? atof(argv[3]) : 0.0;
    double dur;
    pf_engine_snapshot(&eng, NULL, NULL, &dur, NULL, NULL);
    if (secs <= 0.0 || secs > dur) secs = dur;
    long total = (long)(secs * SR);

    float *buf = (float *)calloc((size_t)2 * total, sizeof(float));
    if (!buf) { fprintf(stderr, "oom\n"); return 1; }

    for (long i = 0; i < total; i += CHUNK) {
        int n = (int)(total - i < CHUNK ? total - i : CHUNK);
        pf_engine_render(&eng, buf + 2 * i, n);
    }

    /* clipping stats: the engine's tanh saturates; count near-rail samples */
    double peak = 0; long clipped = 0; double sumsq = 0;
    for (long i = 0; i < 2 * total; i++) {
        double a = fabs(buf[i]);
        if (a > peak) peak = a;
        if (a > 0.98) clipped++;
        sumsq += buf[i] * buf[i];
    }
    printf("rendered %.1fs  peak=%.3f  rms=%.4f  near-clip samples=%ld (%.3f%%)\n",
           secs, peak, sqrt(sumsq / (2 * total)), clipped, 100.0 * clipped / (2 * total));

    int rc = wav_write_stereo16(argv[2], buf, total, SR);
    if (rc == 0) printf("wrote %s\n", argv[2]);
    free(buf);
    pf_engine_destroy(&eng);
    return rc;
}
