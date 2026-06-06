/* wav.c - see wav.h. Host-only; libc is fair game. */
#include "wav.h"
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

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

int wav_write_stereo16(const char *path, const float *interleaved, int n,
                       int sample_rate)
{
    FILE *f = fopen(path, "wb");
    if (!f) return 1;

    uint32_t data_bytes = (uint32_t)n * 4;       /* 16-bit, 2 channels */
    uint32_t byte_rate  = (uint32_t)sample_rate * 4;

    fwrite("RIFF", 1, 4, f);
    put_u32(f, 36 + data_bytes);
    fwrite("WAVE", 1, 4, f);

    fwrite("fmt ", 1, 4, f);
    put_u32(f, 16);                 /* PCM fmt chunk size */
    put_u16(f, 1);                  /* PCM */
    put_u16(f, 2);                  /* channels */
    put_u32(f, (uint32_t)sample_rate);
    put_u32(f, byte_rate);
    put_u16(f, 4);                  /* block align */
    put_u16(f, 16);                 /* bits per sample */

    fwrite("data", 1, 4, f);
    put_u32(f, data_bytes);

    for (int i = 0; i < 2 * n; i++) {
        float v = interleaved[i];
        if (v > 1.0f) v = 1.0f;
        if (v < -1.0f) v = -1.0f;
        int s = (int)(v * 32767.0f);
        put_u16(f, (uint16_t)(int16_t)s);
    }

    fclose(f);
    return 0;
}

int wav_read_stereo(const char *path, float **out, long *frames, int *sample_rate)
{
    FILE *f = fopen(path, "rb");
    if (!f) return 1;
    unsigned char h[12];
    if (fread(h, 1, 12, f) != 12 || memcmp(h, "RIFF", 4) || memcmp(h + 8, "WAVE", 4)) {
        fclose(f); return 1;
    }
    int ch = 0, sr = 0, bits = 0;
    long doff = -1; unsigned long dlen = 0;
    for (;;) {
        unsigned char c[8];
        if (fread(c, 1, 8, f) != 8) break;
        unsigned long sz = c[4] | (c[5] << 8) | (c[6] << 16) | ((unsigned long)c[7] << 24);
        if (!memcmp(c, "fmt ", 4)) {
            unsigned char fm[16];
            if (fread(fm, 1, 16, f) != 16) break;
            ch   = fm[2] | (fm[3] << 8);
            sr   = fm[4] | (fm[5] << 8) | (fm[6] << 16) | ((unsigned long)fm[7] << 24);
            bits = fm[14] | (fm[15] << 8);
            if (sz > 16) fseek(f, (long)sz - 16, SEEK_CUR);
        } else if (!memcmp(c, "data", 4)) {
            doff = ftell(f); dlen = sz; break;
        } else {
            fseek(f, (long)(sz + (sz & 1)), SEEK_CUR);
        }
    }
    if (doff < 0 || bits != 16 || ch < 1) { fclose(f); return 1; }

    long fr = (long)(dlen / (2u * (unsigned)ch));
    float *buf = (float *)malloc((size_t)fr * 2 * sizeof(float));
    if (!buf) { fclose(f); return 1; }
    fseek(f, doff, SEEK_SET);

    double pk = 0.0;
    for (long i = 0; i < fr; i++) {
        short s0 = 0, s1 = 0;
        if (fread(&s0, 2, 1, f) != 1) { fr = i; break; }
        double l, r;
        if (ch == 1) { l = r = s0 / 32768.0; }
        else {
            if (fread(&s1, 2, 1, f) != 1) { fr = i; break; }
            l = s0 / 32768.0; r = s1 / 32768.0;
            if (ch > 2) fseek(f, 2 * (ch - 2), SEEK_CUR);
        }
        buf[2*i] = (float)l; buf[2*i+1] = (float)r;
        double a = fabs(l); if (a > pk) pk = a;
        a = fabs(r); if (a > pk) pk = a;
    }
    fclose(f);
    if (pk > 0.0) { float g = (float)(0.7 / pk); for (long i = 0; i < fr * 2; i++) buf[i] *= g; }

    *out = buf; *frames = fr; if (sample_rate) *sample_rate = sr;
    return 0;
}
