/* tui.c - notcurses front end for pfsynth. HOST-ONLY, macOS.
 *
 * Two things in one screen:
 *   - a live parameter editor for the piano patch (audition while you tweak), and
 *   - a file browser over the maestro MIDI set (labelled with composer / title /
 *     duration from the dataset CSV) plus a `/` search by composer or title.
 *
 * A now-playing strip shows the 88 keys lighting up as the song sounds.
 * Audio runs on the CoreAudio thread (see audio.c); this file is the UI thread.
 */
#include "engine.h"
#include "audio.h"
#include "midi.h"

#include <notcurses/notcurses.h>

#include <ctype.h>
#include <dirent.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/stat.h>

#define SR 44100.0
#define DEFAULT_LIB "maestro/midi/maestro-v3.0.0"

/* ---------------- parameter editor model ---------------- */

enum {
    P_INHARM, P_DECAY, P_RELEASE, P_DAMP, P_DISP, P_STRIKE,
    P_HMASS, P_HSTIFF, P_HEXP, P_VHARD, P_HVMAX, P_INJ, P_BODY, P_REVERB, P_MASTER, P_COUNT
};

typedef struct {
    const char *name;
    double min, max, step;
    int    mul;          /* step is a multiplier rather than an addend */
    const char *fmt;     /* printf format for the (scaled) value */
    double scale;        /* display = value * scale */
    const char *unit;
} pdesc;

static const pdesc PD[P_COUNT] = {
    [P_INHARM]  = { "Inharmonicity B", 0.0,    0.05,   1.5,    1, "%.5f", 1.0,    ""  },
    [P_DECAY]   = { "Decay T60",       0.3,    30.0,   0.5,    0, "%.2f", 1.0,    "s" },
    [P_RELEASE] = { "Release T60",     0.03,   3.0,    0.02,   0, "%.2f", 1.0,    "s" },
    [P_DAMP]    = { "Damping",         0.0,    0.85,   0.02,   0, "%.2f", 1.0,    ""  },
    [P_DISP]    = { "Dispersion secs", 0.0,    16.0,   1.0,    0, "%.0f", 1.0,    ""  },
    [P_STRIKE]  = { "Strike pos",      0.0,    0.49,   0.01,   0, "%.2f", 1.0,    ""  },
    [P_HMASS]   = { "Hammer mass",     0.0005, 0.05,   1.1,    1, "%.2f", 1000.0, "g" },
    [P_HSTIFF]  = { "Hammer stiffness",1.0e4,  1.0e10, 1.4,    1, "%.2e", 1.0,    ""  },
    [P_HEXP]    = { "Hammer exponent", 1.5,    4.0,    0.1,    0, "%.2f", 1.0,    ""  },
    [P_VHARD]   = { "Vel->brightness", 0.0,    2.5,    0.1,    0, "%.2f", 1.0,    ""  },
    [P_HVMAX]   = { "Hammer vmax",     0.5,    20.0,   0.5,    0, "%.2f", 1.0,    ""  },
    [P_INJ]     = { "Injection",       1.0e-7, 1.0e-3, 1.3,    1, "%.2e", 1.0,    ""  },
    [P_BODY]    = { "Body (soundboard)",0.0,   1.5,    0.05,   0, "%.2f", 1.0,    ""  },
    [P_REVERB]  = { "Reverb (room)",    0.0,   0.6,    0.02,   0, "%.2f", 1.0,    ""  },
    [P_MASTER]  = { "Master gain",     50.0,   50000.0,1.25,   1, "%.0f", 1.0,    ""  },
};

static double g_vals[P_COUNT];
static int    g_param_sel;

static void vals_from_defaults(void)
{
    pf_string_params p;
    pf_string_defaults(&p, SR);
    g_vals[P_INHARM]  = p.inharmonicity;
    g_vals[P_DECAY]   = p.decay_t60;
    g_vals[P_RELEASE] = p.release_t60;
    g_vals[P_DAMP]    = p.damping;
    g_vals[P_DISP]    = p.dispersion_sections;
    g_vals[P_STRIKE]  = p.strike_pos;
    g_vals[P_HMASS]   = p.hammer_mass;
    g_vals[P_HSTIFF]  = p.hammer_stiffness;
    g_vals[P_HEXP]    = p.hammer_exponent;
    g_vals[P_VHARD]   = p.hammer_vel_hardness;
    g_vals[P_HVMAX]   = p.hammer_vmax;
    g_vals[P_INJ]     = p.injection;
    g_vals[P_BODY]    = 0.8;      /* matches pf_board_defaults mix */
    g_vals[P_REVERB]  = 0.30;     /* matches pf_reverb_init wet */
    g_vals[P_MASTER]  = 110.0;    /* low enough that dense polyphony doesn't clip the tanh */
}

static void apply_params(pf_engine *e)
{
    pf_string_params p;
    pf_string_defaults(&p, SR);
    p.inharmonicity       = g_vals[P_INHARM];
    p.decay_t60           = g_vals[P_DECAY];
    p.release_t60         = g_vals[P_RELEASE];
    p.damping             = g_vals[P_DAMP];
    p.dispersion_sections = (int)(g_vals[P_DISP] + 0.5);
    p.strike_pos          = g_vals[P_STRIKE];
    p.hammer_mass         = g_vals[P_HMASS];
    p.hammer_stiffness    = g_vals[P_HSTIFF];
    p.hammer_exponent     = g_vals[P_HEXP];
    p.hammer_vel_hardness = g_vals[P_VHARD];
    p.hammer_vmax         = g_vals[P_HVMAX];
    p.injection           = g_vals[P_INJ];
    pf_engine_set_params(e, &p);
    pf_engine_set_body(e, g_vals[P_BODY]);
    pf_engine_set_reverb(e, g_vals[P_REVERB]);
    pf_engine_set_master(e, g_vals[P_MASTER]);
}

static void adjust_param(pf_engine *e, int idx, int dir)
{
    const pdesc *d = &PD[idx];
    double v = g_vals[idx];
    if (d->mul) v *= (dir > 0) ? d->step : 1.0 / d->step;
    else        v += dir * d->step;
    if (v < d->min) v = d->min;
    if (v > d->max) v = d->max;
    g_vals[idx] = v;
    apply_params(e);
}

/* ---------------- file browser ---------------- */

typedef struct { char name[256]; int isdir; } entry;

static char   g_dir[1024];
static char   g_lib_root[1024];      /* initial library dir; CSV keys are relative to it */
static entry *g_entries;
static int    g_nentries, g_capentries;
static int    g_sel, g_scroll;

static int ent_cmp(const void *a, const void *b)
{
    const entry *x = a, *y = b;
    if (x->isdir != y->isdir) return y->isdir - x->isdir;  /* dirs first */
    return strcmp(x->name, y->name);
}

static int has_midi_ext(const char *n)
{
    const char *dot = strrchr(n, '.');
    return dot && (!strcmp(dot, ".mid") || !strcmp(dot, ".midi"));
}

static void ent_push(const char *name, int isdir)
{
    if (g_nentries == g_capentries) {
        g_capentries = g_capentries ? g_capentries * 2 : 64;
        g_entries = realloc(g_entries, (size_t)g_capentries * sizeof(entry));
    }
    snprintf(g_entries[g_nentries].name, sizeof g_entries[0].name, "%s", name);
    g_entries[g_nentries].isdir = isdir;
    g_nentries++;
}

static void list_dir(void)
{
    g_nentries = 0; g_sel = 0; g_scroll = 0;
    DIR *dp = opendir(g_dir);
    if (!dp) return;

    if (strcmp(g_dir, "/") != 0) ent_push("..", 1);

    struct dirent *de;
    while ((de = readdir(dp))) {
        if (de->d_name[0] == '.') continue;        /* hidden, ., .. */
        char full[2048];
        snprintf(full, sizeof full, "%s/%s", g_dir, de->d_name);
        struct stat st;
        if (stat(full, &st) != 0) continue;
        int isdir = S_ISDIR(st.st_mode);
        if (!isdir && !has_midi_ext(de->d_name)) continue;
        ent_push(de->d_name, isdir);
    }
    closedir(dp);
    qsort(g_entries, (size_t)g_nentries, sizeof(entry), ent_cmp);
}

/* at the top of the browsable tree (the library root, or the filesystem root)? */
static int at_top(void)
{
    return strcmp(g_dir, g_lib_root) == 0 || strcmp(g_dir, "/") == 0;
}

static void go_parent(void)
{
    char *slash = strrchr(g_dir, '/');
    if (slash && slash != g_dir) *slash = 0;
    else if (slash) g_dir[1] = 0;
    list_dir();
}

/* ---------------- maestro metadata (composer / title / duration) ----------------
 *
 * The MIDI files carry no text meta-events, so the dataset CSV is the only source.
 * We load <lib-root>/maestro-*.csv once, key it by the relative midi_filename, and
 * use it both to label files in the browser and to drive a global search view.
 */

typedef struct {
    char   path[176];    /* midi_filename, e.g. "2018/MIDI-Unprocessed_...midi" */
    char   composer[80];
    char   title[128];
    double duration;     /* seconds */
} meta_row;

static meta_row *g_meta;
static int       g_nmeta;

/* extract 0-based CSV field `idx` from `line` into dst (handles "quoted, fields") */
static int csv_field(const char *line, int idx, char *dst, size_t dstlen)
{
    const char *p = line;
    int cur = 0, inq = 0;
    size_t n = 0;
    dst[0] = 0;
    while (*p) {
        char c = *p;
        if (inq) {
            if (c == '"') {
                if (p[1] == '"') {                  /* "" -> literal quote */
                    if (cur == idx && n + 1 < dstlen) dst[n++] = '"';
                    p += 2; continue;
                }
                inq = 0; p++; continue;             /* closing quote */
            }
            if (cur == idx && n + 1 < dstlen) dst[n++] = c;
            p++; continue;
        }
        if (c == '"') { inq = 1; p++; continue; }
        if (c == ',') {
            if (cur == idx) { dst[n] = 0; return 1; }
            cur++; n = 0; p++; continue;
        }
        if (c == '\n' || c == '\r') break;
        if (cur == idx && n + 1 < dstlen) dst[n++] = c;
        p++;
    }
    if (cur == idx) { dst[n] = 0; return 1; }
    return 0;
}

static int meta_cmp(const void *a, const void *b)
{
    const meta_row *x = a, *y = b;
    int c = strcmp(x->composer, y->composer);
    return c ? c : strcmp(x->title, y->title);
}

static void meta_load(const char *root)
{
    char csv[1200];
    snprintf(csv, sizeof csv, "%s/maestro-v3.0.0.csv", root);
    FILE *f = fopen(csv, "rb");
    if (!f) {                                       /* fall back to any *.csv in root */
        DIR *dp = opendir(root);
        if (dp) {
            struct dirent *de;
            while ((de = readdir(dp))) {
                const char *dot = strrchr(de->d_name, '.');
                if (dot && !strcmp(dot, ".csv")) {
                    snprintf(csv, sizeof csv, "%s/%s", root, de->d_name);
                    f = fopen(csv, "rb");
                    break;
                }
            }
            closedir(dp);
        }
    }
    if (!f) return;

    char line[2048];
    if (!fgets(line, sizeof line, f)) { fclose(f); return; }   /* header row */
    int cap = 0;
    while (fgets(line, sizeof line, f)) {
        char path[176], composer[80], title[128], durs[40];
        if (!csv_field(line, 4, path, sizeof path) || !path[0]) continue;
        csv_field(line, 0, composer, sizeof composer);
        csv_field(line, 1, title,    sizeof title);
        csv_field(line, 6, durs,     sizeof durs);
        if (g_nmeta == cap) {
            cap = cap ? cap * 2 : 256;
            g_meta = realloc(g_meta, (size_t)cap * sizeof(meta_row));
        }
        meta_row *m = &g_meta[g_nmeta++];
        snprintf(m->path,     sizeof m->path,     "%s", path);
        snprintf(m->composer, sizeof m->composer, "%s", composer);
        snprintf(m->title,    sizeof m->title,    "%s", title);
        m->duration = atof(durs);
    }
    fclose(f);
    qsort(g_meta, (size_t)g_nmeta, sizeof(meta_row), meta_cmp);
}

/* current g_dir as a path relative to g_lib_root, or NULL if we've navigated out */
static const char *rel_under_root(void)
{
    size_t rl = strlen(g_lib_root);
    if (strncmp(g_dir, g_lib_root, rl) != 0) return NULL;
    const char *s = g_dir + rl;
    while (*s == '/') s++;
    return s;                                        /* "" at the root, else "2018" etc. */
}

/* look up the CSV row for a browser entry (NULL for dirs / non-maestro files) */
static const meta_row *entry_meta(const entry *en)
{
    if (en->isdir || g_nmeta == 0) return NULL;
    const char *rel = rel_under_root();
    if (!rel) return NULL;
    char key[256];
    if (*rel) snprintf(key, sizeof key, "%s/%s", rel, en->name);
    else      snprintf(key, sizeof key, "%s", en->name);
    for (int i = 0; i < g_nmeta; i++)
        if (!strcmp(g_meta[i].path, key)) return &g_meta[i];
    return NULL;
}

/* ---------------- search view (by composer / title, across the whole set) ------- */

static int   g_confirm;                 /* quit-confirmation modal is up */
static int   g_search;                  /* search mode active */
static char  g_query[128];
static int  *g_results;                 /* indices into g_meta */
static int   g_nresults, g_capresults;
static int   g_rsel, g_rscroll;

/* case-insensitive substring test */
static int ci_contains(const char *hay, const char *needle)
{
    if (!*needle) return 1;
    for (; *hay; hay++) {
        const char *h = hay, *q = needle;
        while (*h && *q && tolower((unsigned char)*h) == tolower((unsigned char)*q)) { h++; q++; }
        if (!*q) return 1;
    }
    return 0;
}

static void search_rebuild(void)
{
    g_nresults = 0; g_rsel = 0; g_rscroll = 0;
    for (int i = 0; i < g_nmeta; i++) {
        if (ci_contains(g_meta[i].composer, g_query) ||
            ci_contains(g_meta[i].title,    g_query)) {
            if (g_nresults == g_capresults) {
                g_capresults = g_capresults ? g_capresults * 2 : 256;
                g_results = realloc(g_results, (size_t)g_capresults * sizeof(int));
            }
            g_results[g_nresults++] = i;
        }
    }
}

/* load a search result by its g_meta index. returns 1 if a file was loaded */
static int load_result(pf_engine *e, int mi, char *status, size_t statuslen)
{
    const meta_row *m = &g_meta[mi];
    char full[1400];
    snprintf(full, sizeof full, "%s/%s", g_lib_root, m->path);
    pf_song song;
    if (pf_midi_load(&song, full) == 0) {
        pf_engine_load(e, &song);
        pf_engine_play(e);
        snprintf(status, statuslen, "playing: %s - %s", m->composer, m->title);
        return 1;
    }
    snprintf(status, statuslen, "failed to load %s", m->path);
    return 0;
}

/* returns 1 if a file was loaded */
static int browser_enter(pf_engine *e, char *status, size_t statuslen)
{
    if (g_sel < 0 || g_sel >= g_nentries) return 0;
    entry *en = &g_entries[g_sel];

    if (en->isdir) {
        if (!strcmp(en->name, "..")) {
            go_parent();
        } else {
            char full[2048];
            snprintf(full, sizeof full, "%s/%s", g_dir, en->name);
            snprintf(g_dir, sizeof g_dir, "%s", full);
            list_dir();
        }
        return 0;
    }

    char full[2048];
    snprintf(full, sizeof full, "%s/%s", g_dir, en->name);
    pf_song song;
    if (pf_midi_load(&song, full) == 0) {
        pf_engine_load(e, &song);
        pf_engine_play(e);
        const meta_row *m = entry_meta(en);
        if (m) snprintf(status, statuslen, "playing: %s - %s", m->composer, m->title);
        else   snprintf(status, statuslen, "playing: %s", en->name);
        return 1;
    }
    snprintf(status, statuslen, "failed to load %s", en->name);
    return 0;
}

static void seek_rel(pf_engine *e, double d)
{
    double pos, dur; int pl;
    pf_engine_snapshot(e, NULL, &pos, &dur, &pl, NULL);
    pf_engine_seek(e, pos + d);
}

/* ---------------- drawing ---------------- */

static void hline(struct ncplane *n, int y, unsigned dx)
{
    ncplane_set_fg_rgb8(n, 80, 80, 90);
    for (unsigned x = 0; x < dx; x++) ncplane_putstr_yx(n, y, (int)x, "-");
}

/* ---------------- pixel-graphics piano (kitty/sixel via notcurses) ---------- */

static int             g_pix = 0;          /* terminal supports pixel blitting */
static struct ncplane *g_kbpl = NULL;      /* dedicated plane for the keyboard image */
static int             g_kb_py, g_kb_px, g_kb_rows, g_kb_cols;
static unsigned char   g_kb_last[128];     /* last-drawn active set (re-blit on change) */
static int             g_kb_force = 1;
static double          g_kb_last_ms = 0;

static double pix_now_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000.0 + ts.tv_nsec / 1.0e6;
}
static int clmp8(int v) { return v < 0 ? 0 : (v > 255 ? 255 : v); }

static int note_is_black(int n) { int pc = n % 12; return pc==1||pc==3||pc==6||pc==8||pc==10; }

/* paint a realistic 88-key keyboard (A0..C8) into an RGBA buffer */
static void piano_rgba(unsigned char *buf, int W, int H, const unsigned char active[128])
{
    for (long i = 0; i < (long)W * H; i++) {        /* dark felt background */
        buf[i*4] = 16; buf[i*4+1] = 16; buf[i*4+2] = 20; buf[i*4+3] = 255;
    }
    int whites = 52, keyw = W / whites; if (keyw < 1) keyw = 1;
    int bw = (int)(keyw * 0.64); if (bw < 1) bw = 1;
    int bh = (int)(H * 0.62);

    /* white keys (full height), left to right */
    int wi = 0;
    for (int note = 21; note <= 108; note++) {
        if (note_is_black(note)) continue;
        int x0 = wi * keyw, x1 = x0 + keyw - 1;     /* 1px gap on the right */
        int on = active[note];
        for (int y = 0; y < H; y++) {
            double shade = (y > H * 0.86) ? 0.78 : 1.0;     /* bottom shadow */
            int r, g, b;
            if (on) { r = 255; g = 206; b = 96; }
            else    { r = 244; g = 244; b = 237; }
            r = clmp8((int)(r*shade)); g = clmp8((int)(g*shade)); b = clmp8((int)(b*shade));
            for (int x = x0; x < x1 && x < W; x++) {
                long o = ((long)y * W + x) * 4;
                buf[o]=r; buf[o+1]=g; buf[o+2]=b; buf[o+3]=255;
            }
        }
        wi++;
    }
    /* black keys on top (upper portion only), centered on white-key boundaries */
    wi = 0;
    for (int note = 21; note <= 108; note++) {
        if (!note_is_black(note)) { wi++; continue; }
        int cx = wi * keyw, x0 = cx - bw/2, x1 = cx + bw/2;
        int on = active[note];
        for (int y = 0; y < bh; y++) {
            int bevel = (y < bh * 0.16) ? 38 : 0;   /* lighter top edge */
            int r, g, b;
            if (on) { r = 255; g = 162; b = 52; }
            else    { r = 24;  g = 24;  b = 28; }
            r = clmp8(r+bevel); g = clmp8(g+bevel); b = clmp8(b+bevel);
            for (int x = x0; x < x1; x++) {
                if (x < 0 || x >= W) continue;
                long o = ((long)y * W + x) * 4;
                buf[o]=r; buf[o+1]=g; buf[o+2]=b; buf[o+3]=255;
            }
        }
    }
}

/* (re)build and blit the keyboard image; throttled, and only when notes change */
static void kb_update(struct notcurses *nc, struct ncplane *std,
                      const unsigned char active[128])
{
    unsigned dy, dx; ncplane_dim_yx(std, &dy, &dx);
    int rows = 2, cols = (int)dx - 2, py = (int)dy - 4, px = 1;  /* below the VOICES line */
    if (cols < 8 || py < 1) return;

    if (!g_kbpl || g_kb_rows != rows || g_kb_cols != cols || g_kb_py != py || g_kb_px != px) {
        if (g_kbpl) { ncplane_destroy(g_kbpl); g_kbpl = NULL; }
        struct ncplane_options o; memset(&o, 0, sizeof o);
        o.y = py; o.x = px; o.rows = rows; o.cols = cols;
        g_kbpl = ncplane_create(std, &o);
        g_kb_py = py; g_kb_px = px; g_kb_rows = rows; g_kb_cols = cols; g_kb_force = 1;
    }
    if (!g_kbpl) return;

    double now = pix_now_ms();
    if (!g_kb_force && (memcmp(g_kb_last, active, 128) == 0 || now - g_kb_last_ms < 40.0))
        return;
    memcpy(g_kb_last, active, 128);
    g_kb_force = 0; g_kb_last_ms = now;

    unsigned pxy, pxx;
    ncplane_pixel_geom(g_kbpl, &pxy, &pxx, NULL, NULL, NULL, NULL);
    int Wp = (int)pxx, Hp = (int)pxy;
    if (Wp < 8 || Hp < 4) return;
    unsigned char *rgba = malloc((size_t)Wp * Hp * 4);
    if (!rgba) return;
    piano_rgba(rgba, Wp, Hp, active);
    struct ncvisual *v = ncvisual_from_rgba(rgba, Hp, Wp * 4, Wp);
    if (v) {
        struct ncvisual_options vo; memset(&vo, 0, sizeof vo);
        vo.n = g_kbpl; vo.blitter = NCBLIT_PIXEL; vo.scaling = NCSCALE_NONE;
        ncvisual_blit(nc, v, &vo);
        ncvisual_destroy(v);
    }
    free(rgba);
}

static void draw(struct notcurses *nc, struct ncplane *n, pf_engine *e,
                 int focus_lib, const char *status)
{
    unsigned dy, dx;
    ncplane_dim_yx(n, &dy, &dx);
    ncplane_erase(n);

    unsigned char active[128];
    double pos = 0, dur = 0; int playing = 0, pedal = 0;
    int voices = pf_engine_snapshot(e, active, &pos, &dur, &playing, &pedal);

    /* header */
    ncplane_set_styles(n, NCSTYLE_BOLD);
    ncplane_set_fg_rgb8(n, 120, 200, 255);
    ncplane_printf_yx(n, 0, 1, "pfsynth");
    ncplane_set_styles(n, NCSTYLE_NONE);
    ncplane_set_fg_rgb8(n, 150, 150, 160);
    ncplane_printf_yx(n, 0, 10, "physical-modeling piano");
    ncplane_set_fg_rgb8(n, playing ? 120 : 210, 220, 140);
    ncplane_printf_yx(n, 0, (int)dx - 30, "%s  %d:%05.2f / %d:%05.2f",
                      playing ? "PLAY " : "PAUSE",
                      (int)pos / 60, fmod(pos, 60.0),
                      (int)dur / 60, fmod(dur, 60.0));
    hline(n, 1, dx);

    int col2 = (int)dx / 2 + 2;
    int top  = 3;

    /* parameters pane */
    ncplane_set_styles(n, NCSTYLE_BOLD);
    ncplane_set_fg_rgb8(n, focus_lib ? 130 : 255, focus_lib ? 130 : 255, 255);
    ncplane_printf_yx(n, top - 1, 1, "PATCH%s", focus_lib ? "" : "  *");
    ncplane_set_styles(n, NCSTYLE_NONE);
    for (int i = 0; i < P_COUNT; i++) {
        int sel = (!focus_lib && i == g_param_sel);
        char valbuf[32];
        snprintf(valbuf, sizeof valbuf, PD[i].fmt, g_vals[i] * PD[i].scale);
        if (sel) { ncplane_set_fg_rgb8(n, 30, 30, 40); ncplane_set_bg_rgb8(n, 200, 200, 120); }
        else     { ncplane_set_fg_rgb8(n, 200, 200, 210); ncplane_set_bg_default(n); }
        ncplane_printf_yx(n, top + i, 1, " %-16s %10s %-2s ",
                          PD[i].name, valbuf, PD[i].unit);
        ncplane_set_bg_default(n);
    }

    /* library pane: dir browser, or the global search view when active */
    int rows = (int)dy - top - 7;
    if (rows < 3) rows = 3;
    int w = (int)dx - col2 - 2;
    if (w < 1) w = 1;

    ncplane_set_styles(n, NCSTYLE_BOLD);
    ncplane_set_fg_rgb8(n, focus_lib ? 255 : 130, focus_lib ? 255 : 130, focus_lib ? 255 : 140);
    ncplane_printf_yx(n, top - 1, col2, "%s%s", g_search ? "SEARCH " : "LIBRARY",
                      focus_lib ? "  *" : "");
    ncplane_set_styles(n, NCSTYLE_NONE);

    if (g_search) {
        ncplane_set_fg_rgb8(n, 220, 220, 130);
        ncplane_printf_yx(n, top - 1, col2 + 9, "%s_   (%d)", g_query, g_nresults);
        if (g_rsel < g_rscroll) g_rscroll = g_rsel;
        if (g_rsel >= g_rscroll + rows) g_rscroll = g_rsel - rows + 1;
        for (int r = 0; r < rows; r++) {
            int idx = g_rscroll + r;
            if (idx >= g_nresults) break;
            const meta_row *m = &g_meta[g_results[idx]];
            int sel = (idx == g_rsel);
            if (sel) { ncplane_set_fg_rgb8(n, 30, 30, 40); ncplane_set_bg_rgb8(n, 180, 200, 255); }
            else     { ncplane_set_fg_rgb8(n, 200, 200, 210); ncplane_set_bg_default(n); }
            int s = (int)(m->duration + 0.5);
            char label[400];
            snprintf(label, sizeof label, " %d:%02d  %s - %s",
                     s / 60, s % 60, m->composer, m->title);
            ncplane_printf_yx(n, top + r, col2, " %-*.*s", w, w, label);
            ncplane_set_bg_default(n);
        }
        if (g_nresults == 0) {
            ncplane_set_fg_rgb8(n, 150, 150, 160);
            ncplane_printf_yx(n, top, col2, " (no matches)");
        }
    } else {
        ncplane_set_fg_rgb8(n, 130, 150, 170);
        {
            const char *d = g_dir;
            int maxw = (int)dx - col2 - 12;
            if (maxw > 0 && (int)strlen(d) > maxw) d += strlen(d) - maxw;
            ncplane_printf_yx(n, top - 1, col2 + 10, "%s", d);
        }
        if (g_sel < g_scroll) g_scroll = g_sel;
        if (g_sel >= g_scroll + rows) g_scroll = g_sel - rows + 1;
        for (int r = 0; r < rows; r++) {
            int idx = g_scroll + r;
            if (idx >= g_nentries) break;
            entry *en = &g_entries[idx];
            int sel = (focus_lib && idx == g_sel);
            if (sel) { ncplane_set_fg_rgb8(n, 30, 30, 40); ncplane_set_bg_rgb8(n, 180, 200, 255); }
            else if (en->isdir) { ncplane_set_fg_rgb8(n, 140, 200, 255); ncplane_set_bg_default(n); }
            else { ncplane_set_fg_rgb8(n, 200, 200, 210); ncplane_set_bg_default(n); }
            const meta_row *m = entry_meta(en);
            char label[400];
            if (m) snprintf(label, sizeof label, " %s - %s", m->composer, m->title);
            else   snprintf(label, sizeof label, "%s%s", en->isdir ? "/" : " ", en->name);
            ncplane_printf_yx(n, top + r, col2, " %-*.*s", w, w, label);
            ncplane_set_bg_default(n);
        }
        /* detail line for the highlighted file: duration + raw filename */
        if (focus_lib && g_sel >= 0 && g_sel < g_nentries && !g_entries[g_sel].isdir) {
            int dyr = top + rows;
            if (dyr <= (int)dy - 7) {
                const meta_row *m = entry_meta(&g_entries[g_sel]);
                char info[400];
                if (m) { int s = (int)(m->duration + 0.5);
                         snprintf(info, sizeof info, "%d:%02d   %s",
                                  s / 60, s % 60, g_entries[g_sel].name); }
                else     snprintf(info, sizeof info, "%s", g_entries[g_sel].name);
                ncplane_set_fg_rgb8(n, 150, 170, 150);
                ncplane_printf_yx(n, dyr, col2, " %-*.*s", w, w, info);
            }
        }
    }

    /* now-playing keyboard strip (88 keys, lit as notes sound) */
    int ky = (int)dy - 5;
    hline(n, ky - 1, dx);
    ncplane_set_styles(n, NCSTYLE_BOLD);
    ncplane_set_fg_rgb8(n, 200, 200, 210);
    ncplane_printf_yx(n, ky, 1, "VOICES %3d   PEDAL %s",
                      voices, pedal ? "DOWN" : "up ");
    ncplane_set_styles(n, NCSTYLE_NONE);
    if (g_pix) {
        kb_update(nc, n, active);          /* pixel-graphics keyboard (kitty/sixel) */
    } else {
        int kw = (int)dx - 2;
        for (int k = 0; k < 88 && k < kw; k++) {
            int note = 21 + k;
            int black = note_is_black(note);
            if (active[note]) ncplane_set_fg_rgb8(n, 255, 230, 80);
            else if (black)   ncplane_set_fg_rgb8(n, 90, 90, 110);
            else              ncplane_set_fg_rgb8(n, 170, 170, 180);
            ncplane_putstr_yx(n, ky + 2, 1 + k, active[note] ? "#" : (black ? "." : "|"));
        }
    }

    /* footer */
    ncplane_set_fg_rgb8(n, 120, 120, 130);
    ncplane_printf_yx(n, (int)dy - 1, 1,
        "TAB pane  up/dn select  l/r adjust  / search  ENTER load  SPACE play  "
        "[ ] seek  Esc up/quit  Q quit");
    if (status && status[0]) {
        ncplane_set_fg_rgb8(n, 120, 230, 140);
        ncplane_printf_yx(n, (int)dy - 2, 1, "%.*s", (int)dx - 2, status);
    }

    /* quit-confirmation modal, centered over everything */
    if (g_confirm) {
        int bw = 42, bh = 5;
        int by = (int)dy / 2 - bh / 2, bx = (int)dx / 2 - bw / 2;
        if (by < 0) by = 0;
        if (bx < 0) bx = 0;
        ncplane_set_bg_rgb8(n, 130, 40, 45);
        ncplane_set_fg_rgb8(n, 255, 255, 255);
        for (int r = 0; r < bh; r++)
            for (int c = 0; c < bw; c++)
                ncplane_putstr_yx(n, by + r, bx + c, " ");
        ncplane_set_styles(n, NCSTYLE_BOLD);
        ncplane_printf_yx(n, by + 1, bx + 3, "Quit pfsynth?");
        ncplane_set_styles(n, NCSTYLE_NONE);
        ncplane_set_fg_rgb8(n, 235, 220, 220);
        ncplane_printf_yx(n, by + 3, bx + 3, "y / Enter = quit     n / Esc = cancel");
        ncplane_set_bg_default(n);
    }

    notcurses_render(nc);
}

int main(int argc, char **argv)
{
    const char *lib = (argc > 1) ? argv[1] : DEFAULT_LIB;
    snprintf(g_lib_root, sizeof g_lib_root, "%s", lib);
    size_t rl = strlen(g_lib_root);                 /* strip trailing slashes so keys line up */
    while (rl > 1 && g_lib_root[rl - 1] == '/') g_lib_root[--rl] = 0;
    snprintf(g_dir, sizeof g_dir, "%s", g_lib_root);
    meta_load(g_lib_root);

    static pf_engine eng;
    pf_engine_init(&eng, SR);
    vals_from_defaults();
    apply_params(&eng);

    if (pf_audio_start(&eng)) {
        fprintf(stderr, "could not start audio\n");
        return 1;
    }

    struct notcurses_options opts;
    memset(&opts, 0, sizeof opts);
    /* full-screen alternate-screen TUI; terminal is restored cleanly on exit */
    opts.flags = NCOPTION_SUPPRESS_BANNERS;
    struct notcurses *nc = notcurses_init(&opts, NULL);
    if (!nc) { pf_audio_stop(); fprintf(stderr, "notcurses init failed\n"); return 1; }
    struct ncplane *std = notcurses_stdplane(nc);
    g_pix = (notcurses_check_pixel_support(nc) != NCPIXEL_NONE);  /* kitty/sixel keyboard? */

    list_dir();

    int focus_lib = 1, quit = 0;
    char status[256] = "pick a MIDI file and press ENTER  ( / to search by composer or title )";

    while (!quit) {
        draw(nc, std, &eng, focus_lib, status);

        ncinput ni;
        uint32_t k;
        while ((k = notcurses_get_nblock(nc, &ni)) != 0) {
            if (k == (uint32_t)-1) { quit = 1; break; }
            int is_release = (ni.evtype == NCTYPE_RELEASE);

            /* quit-confirmation modal owns the keyboard while it's up */
            if (g_confirm) {
                if (is_release) continue;
                if (k == 'y' || k == 'Y' || k == NCKEY_ENTER || k == '\r' || k == '\n')
                    quit = 1;
                else if (k == 'n' || k == 'N' || k == NCKEY_ESC)
                    g_confirm = 0;
                continue;                   /* any other key: stay in the modal */
            }

            /* search mode owns the keyboard: letters edit the query, not notes */
            if (g_search) {
                if (is_release) continue;
                switch (k) {
                case NCKEY_ESC: g_search = 0; break;
                case NCKEY_ENTER: case '\r': case '\n':
                    if (g_rsel >= 0 && g_rsel < g_nresults)
                        load_result(&eng, g_results[g_rsel], status, sizeof status);
                    break;
                case NCKEY_UP:   if (g_rsel > 0) g_rsel--; break;
                case NCKEY_DOWN: if (g_rsel < g_nresults - 1) g_rsel++; break;
                case NCKEY_BACKSPACE: case 0x7f: {
                    size_t L = strlen(g_query);
                    if (L) { g_query[L - 1] = 0; search_rebuild(); }
                    break;
                }
                default:
                    if (k >= 0x20 && k < 0x7f) {
                        size_t L = strlen(g_query);
                        if (L + 1 < sizeof g_query) {
                            g_query[L] = (char)k; g_query[L + 1] = 0; search_rebuild();
                        }
                    }
                    break;
                }
                continue;
            }

            if (is_release) continue;

            switch (k) {
            case 'Q':                       /* shift+Q: confirm-quit from anywhere */
                g_confirm = 1; break;
            case NCKEY_ESC:
                /* in the browser, Esc backs up a directory; at the top level (or
                 * when the params pane is focused) it asks to quit */
                if (focus_lib && !at_top()) go_parent();
                else                        g_confirm = 1;
                break;
            case NCKEY_TAB: focus_lib = !focus_lib; break;
            case ' ': {
                double pos, dur; int pl;
                pf_engine_snapshot(&eng, NULL, &pos, &dur, &pl, NULL);
                if (pl) pf_engine_pause(&eng); else pf_engine_play(&eng);
                break;
            }
            case NCKEY_UP:
                if (focus_lib) { if (g_sel > 0) g_sel--; }
                else if (g_param_sel > 0) g_param_sel--;
                break;
            case NCKEY_DOWN:
                if (focus_lib) { if (g_sel < g_nentries - 1) g_sel++; }
                else if (g_param_sel < P_COUNT - 1) g_param_sel++;
                break;
            case NCKEY_LEFT:
                if (!focus_lib) adjust_param(&eng, g_param_sel, -1);
                break;
            case NCKEY_RIGHT:
                if (!focus_lib) adjust_param(&eng, g_param_sel, +1);
                break;
            case NCKEY_ENTER: case '\r': case '\n':
                if (focus_lib) browser_enter(&eng, status, sizeof status);
                break;
            case NCKEY_BACKSPACE: case 0x7f:
                if (focus_lib) go_parent();
                break;
            case '[': seek_rel(&eng, -5.0); break;
            case ']': seek_rel(&eng, +5.0); break;
            default: break;
            }
        }

        struct timespec ts = { 0, 16 * 1000 * 1000 };  /* ~60 fps */
        nanosleep(&ts, NULL);
    }

    notcurses_stop(nc);
    pf_audio_stop();
    pf_engine_destroy(&eng);
    free(g_entries);
    free(g_meta);
    free(g_results);
    return 0;
}
