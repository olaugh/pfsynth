/* wav.c - see wav.h. Host-only; libc is fair game. */
#include "wav.h"
#include <stdio.h>
#include <stdint.h>

static void put_u32(FILE *f, uint32_t v)
{
    fputc(v & 0xff, f); fputc((v >> 8) & 0xff, f);
    fputc((v >> 16) & 0xff, f); fputc((v >> 24) & 0xff, f);
}
static void put_u16(FILE *f, uint16_t v)
{
    fputc(v & 0xff, f); fputc((v >> 8) & 0xff, f);
}

int wav_write_mono16(const char *path, const float *samples, int n,
                     int sample_rate)
{
    FILE *f = fopen(path, "wb");
    if (!f) return 1;

    uint32_t data_bytes = (uint32_t)n * 2;       /* 16-bit mono */
    uint32_t byte_rate  = (uint32_t)sample_rate * 2;

    fwrite("RIFF", 1, 4, f);
    put_u32(f, 36 + data_bytes);
    fwrite("WAVE", 1, 4, f);

    fwrite("fmt ", 1, 4, f);
    put_u32(f, 16);                 /* PCM fmt chunk size */
    put_u16(f, 1);                  /* PCM */
    put_u16(f, 1);                  /* channels */
    put_u32(f, (uint32_t)sample_rate);
    put_u32(f, byte_rate);
    put_u16(f, 2);                  /* block align */
    put_u16(f, 16);                 /* bits per sample */

    fwrite("data", 1, 4, f);
    put_u32(f, data_bytes);

    for (int i = 0; i < n; i++) {
        float v = samples[i];
        if (v > 1.0f) v = 1.0f;
        if (v < -1.0f) v = -1.0f;
        int s = (int)(v * 32767.0f);
        put_u16(f, (uint16_t)(int16_t)s);
    }

    fclose(f);
    return 0;
}
