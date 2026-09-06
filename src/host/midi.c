/* midi.c - see midi.h. Host-only. */
#include "midi.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* intermediate event with absolute tick, before tempo conversion */
typedef struct {
    long          tick;
    int           order;   /* preserve input order for stable sort */
    unsigned char type;    /* PF_EV_* or 3 == tempo */
    unsigned char note;
    unsigned char val;
    long          tempo;   /* us per quarter, when type==3 */
} raw_ev;

#define RAW_TEMPO 255

typedef struct {
    const unsigned char *p, *end;
    int ok;
} reader;

static unsigned char rd_u8(reader *r)
{
    if (r->p >= r->end) { r->ok = 0; return 0; }
    return *r->p++;
}
static unsigned long rd_u32(reader *r)
{
    unsigned long v = 0;
    for (int i = 0; i < 4; i++) v = (v << 8) | rd_u8(r);
    return v;
}
static unsigned int rd_u16(reader *r)
{
    unsigned int v = rd_u8(r);
    v = (v << 8) | rd_u8(r);
    return v;
}
/* MIDI variable-length quantity */
static unsigned long rd_varlen(reader *r)
{
    unsigned long v = 0;
    for (int i = 0; i < 4; i++) {
        unsigned char c = rd_u8(r);
        v = (v << 7) | (c & 0x7f);
        if (!(c & 0x80)) break;
    }
    return v;
}

/* growable raw-event list */
typedef struct { raw_ev *a; int n, cap; } raw_list;
static void raw_push(raw_list *L, raw_ev e)
{
    if (L->n == L->cap) {
        L->cap = L->cap ? L->cap * 2 : 1024;
        L->a = (raw_ev *)realloc(L->a, (size_t)L->cap * sizeof(raw_ev));
    }
    L->a[L->n++] = e;
}

static int cmp_raw(const void *pa, const void *pb)
{
    const raw_ev *a = (const raw_ev *)pa, *b = (const raw_ev *)pb;
    if (a->tick != b->tick) return a->tick < b->tick ? -1 : 1;
    return a->order - b->order;  /* stable within a tick */
}

/* parse one MTrk chunk body [p, p+len) into the raw list */
static void parse_track(raw_list *L, const unsigned char *body, unsigned long len,
                        int *order)
{
    reader r = { body, body + len, 1 };
    long tick = 0;
    unsigned char status = 0;

    while (r.ok && r.p < r.end) {
        tick += (long)rd_varlen(&r);
        unsigned char b = rd_u8(&r);

        if (b == 0xff) {                    /* meta */
            unsigned char meta = rd_u8(&r);
            unsigned long mlen = rd_varlen(&r);
            const unsigned char *md = r.p;
            r.p += mlen;
            if (r.p > r.end) { r.ok = 0; break; }
            if (meta == 0x51 && mlen == 3) {
                long tempo = ((long)md[0] << 16) | ((long)md[1] << 8) | md[2];
                raw_ev e = { tick, (*order)++, RAW_TEMPO, 0, 0, tempo };
                raw_push(L, e);
            } else if (meta == 0x2f) {
                break;                       /* end of track */
            }
            continue;
        }
        if (b == 0xf0 || b == 0xf7) {       /* sysex: skip */
            unsigned long slen = rd_varlen(&r);
            r.p += slen;
            continue;
        }

        unsigned char d0, d1;
        if (b & 0x80) { status = b; d0 = rd_u8(&r); }  /* new status */
        else          { d0 = b; }                       /* running status */

        switch (status & 0xf0) {
        case 0x80:                                       /* note off */
            d1 = rd_u8(&r);
            { raw_ev e = { tick, (*order)++, PF_EV_NOTE_OFF, d0, d1, 0 };
              raw_push(L, e); }
            break;
        case 0x90:                                       /* note on */
            d1 = rd_u8(&r);
            { unsigned char ty = d1 ? PF_EV_NOTE_ON : PF_EV_NOTE_OFF;
              raw_ev e = { tick, (*order)++, ty, d0, d1, 0 };
              raw_push(L, e); }
            break;
        case 0xb0:                                       /* control change */
            d1 = rd_u8(&r);
            if (d0 == 64 || d0 == 66 || d0 == 67) {      /* sustain / sostenuto / soft pedal */
                unsigned char ty = d0 == 64 ? PF_EV_PEDAL : d0 == 66 ? PF_EV_SOSTENUTO : PF_EV_SOFT;
                raw_ev e = { tick, (*order)++, ty, 0, d1, 0 };
                raw_push(L, e);
            }
            break;
        case 0xa0:                                       /* 2-byte, ignore */
        case 0xe0:
            rd_u8(&r);
            break;
        case 0xc0:                                       /* 1-byte, ignore */
        case 0xd0:
            break;
        default:
            r.ok = 0;                                    /* desync; bail track */
            break;
        }
    }
}

int pf_midi_parse(pf_song *song, const unsigned char *buf, long fsize)
{
    song->ev = NULL; song->n = 0; song->duration = 0.0;
    if (fsize <= 14) { fprintf(stderr, "midi: too small\n"); return 1; }

    reader r = { buf, buf + fsize, 1 };
    if (memcmp(r.p, "MThd", 4) != 0) { fprintf(stderr, "midi: not an SMF\n"); return 1; }
    r.p += 4;
    unsigned long hlen = rd_u32(&r);
    rd_u16(&r);                              /* format (0/1, both handled) */
    unsigned int ntracks = rd_u16(&r);
    int division = (int)rd_u16(&r);
    r.p += (hlen > 6) ? (long)(hlen - 6) : 0;
    if (division <= 0) {                     /* SMPTE timecode unsupported */
        fprintf(stderr, "midi: SMPTE timing unsupported\n"); return 1;
    }

    raw_list L = { NULL, 0, 0 };
    int order = 0;
    for (unsigned int t = 0; t < ntracks && r.p + 8 <= r.end; t++) {
        if (memcmp(r.p, "MTrk", 4) != 0) break;
        r.p += 4;
        unsigned long tlen = rd_u32(&r);
        const unsigned char *body = r.p;
        if (body + tlen > r.end) tlen = (unsigned long)(r.end - body);
        parse_track(&L, body, tlen, &order);
        r.p = body + tlen;
    }

    if (L.n == 0) { free(L.a); fprintf(stderr, "midi: no events\n"); return 1; }

    qsort(L.a, (size_t)L.n, sizeof(raw_ev), cmp_raw);

    /* tick -> seconds with the tempo map, dropping tempo markers as we go */
    pf_midi_event *out = (pf_midi_event *)malloc((size_t)L.n * sizeof(pf_midi_event));
    int nout = 0;
    double cur_s = 0.0, tempo_us = 500000.0;  /* 120 bpm default */
    long last_tick = 0;
    for (int i = 0; i < L.n; i++) {
        raw_ev *e = &L.a[i];
        cur_s += (double)(e->tick - last_tick) * (tempo_us / 1.0e6) / division;
        last_tick = e->tick;
        if (e->type == RAW_TEMPO) { tempo_us = (double)e->tempo; continue; }
        out[nout].t    = cur_s;
        out[nout].type = e->type;
        out[nout].note = e->note;
        out[nout].val  = e->val;
        nout++;
    }
    free(L.a);

    song->ev = out;
    song->n  = nout;
    song->duration = (nout ? out[nout - 1].t : 0.0) + 4.0;  /* ring-out tail */
    return 0;
}

int pf_midi_load(pf_song *song, const char *path)
{
    song->ev = NULL; song->n = 0; song->duration = 0.0;
    FILE *f = fopen(path, "rb");
    if (!f) { fprintf(stderr, "midi: cannot open %s\n", path); return 1; }
    fseek(f, 0, SEEK_END);
    long fsize = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (fsize <= 0) { fclose(f); fprintf(stderr, "midi: empty\n"); return 1; }
    unsigned char *buf = (unsigned char *)malloc((size_t)fsize);
    if (fread(buf, 1, (size_t)fsize, f) != (size_t)fsize) {
        fclose(f); free(buf); fprintf(stderr, "midi: short read\n"); return 1;
    }
    fclose(f);
    int rc = pf_midi_parse(song, buf, fsize);
    free(buf);
    return rc;
}

void pf_midi_free(pf_song *song)
{
    free(song->ev);
    song->ev = NULL;
    song->n = 0;
}
