/* tui.c - notcurses front end for pfsynth. HOST-ONLY, macOS.
 *
 * Three things in one screen:
 *   - a live parameter editor for the piano patch (audition while you tweak),
 *   - a file browser over the maestro MIDI set, and
 *   - a QWERTY "piano" so you can play notes by hand.
 *
 * Audio runs on the CoreAudio thread (see audio.c); this file is the UI thread.
 */
#include "engine.h"
#include "audio.h"
#include "midi.h"

#include <notcurses/notcurses.h>

#include <dirent.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/stat.h>

#define SR 44100.0
#define DEFAULT_LIB "maestro/midi/maestro-v3.0.0"
#define KEY_HOLD_MS 800.0   /* auto note-off when the terminal sends no key-up */

/* ---------------- parameter editor model ---------------- */

enum {
    P_INHARM, P_DECAY, P_RELEASE, P_DAMP, P_DISP,
    P_HMASS, P_HSTIFF, P_HEXP, P_HVMAX, P_INJ, P_MASTER, P_COUNT
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
    [P_HMASS]   = { "Hammer mass",     0.0005, 0.05,   1.1,    1, "%.2f", 1000.0, "g" },
    [P_HSTIFF]  = { "Hammer stiffness",1.0e4,  1.0e10, 1.4,    1, "%.2e", 1.0,    ""  },
    [P_HEXP]    = { "Hammer exponent", 1.5,    4.0,    0.1,    0, "%.2f", 1.0,    ""  },
    [P_HVMAX]   = { "Hammer vmax",     0.5,    20.0,   0.5,    0, "%.2f", 1.0,    ""  },
    [P_INJ]     = { "Injection",       1.0e-7, 1.0e-3, 1.3,    1, "%.2e", 1.0,    ""  },
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
    g_vals[P_HMASS]   = p.hammer_mass;
    g_vals[P_HSTIFF]  = p.hammer_stiffness;
    g_vals[P_HEXP]    = p.hammer_exponent;
    g_vals[P_HVMAX]   = p.hammer_vmax;
    g_vals[P_INJ]     = p.injection;
    g_vals[P_MASTER]  = 2500.0;
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
    p.hammer_mass         = g_vals[P_HMASS];
    p.hammer_stiffness    = g_vals[P_HSTIFF];
    p.hammer_exponent     = g_vals[P_HEXP];
    p.hammer_vmax         = g_vals[P_HVMAX];
    p.injection           = g_vals[P_INJ];
    pf_engine_set_params(e, &p);
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

static void go_parent(void)
{
    char *slash = strrchr(g_dir, '/');
    if (slash && slash != g_dir) *slash = 0;
    else if (slash) g_dir[1] = 0;
    list_dir();
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
        snprintf(status, statuslen, "playing: %s", en->name);
        return 1;
    }
    snprintf(status, statuslen, "failed to load %s", en->name);
    return 0;
}

/* ---------------- QWERTY piano ---------------- */

static int   g_base = 60;            /* base MIDI note (C4) */
static signed char g_keymap[256];    /* char -> semitone offset, -1 = none */
static double g_keydown_ms[256];     /* press time per char, 0 = up */
static int    g_keynote[256];        /* note triggered by that char */

static void keymap_init(void)
{
    memset(g_keymap, -1, sizeof g_keymap);
    const char *low = "zsxdcvgbhnjm,l.";          /* lower row = base octave */
    for (int i = 0; low[i]; i++) g_keymap[(unsigned char)low[i]] = (signed char)i;
    const char *up = "q2w3er5t6y7ui";             /* upper row = base + octave */
    for (int i = 0; up[i]; i++) g_keymap[(unsigned char)up[i]] = (signed char)(12 + i);
}

static double now_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000.0 + ts.tv_nsec / 1.0e6;
}

static void key_press(pf_engine *e, unsigned char c)
{
    int off = g_keymap[c];
    if (off < 0) return;
    if (g_keydown_ms[c] != 0.0) return;            /* already held */
    int note = g_base + off;
    if (note < 0 || note > 127) return;
    g_keynote[c] = note;
    g_keydown_ms[c] = now_ms();
    pf_engine_note_on(e, note, 96);
}

static void key_release(pf_engine *e, unsigned char c)
{
    if (g_keydown_ms[c] == 0.0) return;
    pf_engine_note_off(e, g_keynote[c]);
    g_keydown_ms[c] = 0.0;
}

static void keys_timeout(pf_engine *e)
{
    double t = now_ms();
    for (int c = 0; c < 256; c++)
        if (g_keydown_ms[c] != 0.0 && t - g_keydown_ms[c] > KEY_HOLD_MS)
            key_release(e, (unsigned char)c);
}

static void seek_rel(pf_engine *e, double d)
{
    double pos, dur; int pl;
    pf_engine_snapshot(e, NULL, &pos, &dur, &pl);
    pf_engine_seek(e, pos + d);
}

/* ---------------- drawing ---------------- */

static void hline(struct ncplane *n, int y, unsigned dx)
{
    ncplane_set_fg_rgb8(n, 80, 80, 90);
    for (unsigned x = 0; x < dx; x++) ncplane_putstr_yx(n, y, (int)x, "-");
}

static void draw(struct notcurses *nc, struct ncplane *n, pf_engine *e,
                 int focus_lib, int pedal, const char *status)
{
    unsigned dy, dx;
    ncplane_dim_yx(n, &dy, &dx);
    ncplane_erase(n);

    unsigned char active[128];
    double pos = 0, dur = 0; int playing = 0;
    int voices = pf_engine_snapshot(e, active, &pos, &dur, &playing);

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

    /* library pane */
    ncplane_set_styles(n, NCSTYLE_BOLD);
    ncplane_set_fg_rgb8(n, focus_lib ? 255 : 130, focus_lib ? 255 : 130, focus_lib ? 255 : 140);
    ncplane_printf_yx(n, top - 1, col2, "LIBRARY%s", focus_lib ? "  *" : "");
    ncplane_set_styles(n, NCSTYLE_NONE);
    ncplane_set_fg_rgb8(n, 130, 150, 170);
    {
        const char *d = g_dir;
        int maxw = (int)dx - col2 - 12;
        if (maxw > 0 && (int)strlen(d) > maxw) d += strlen(d) - maxw;
        ncplane_printf_yx(n, top - 1, col2 + 10, "%s", d);
    }
    int rows = (int)dy - top - 7;
    if (rows < 3) rows = 3;
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
        char label[300];
        snprintf(label, sizeof label, "%s%s", en->isdir ? "/" : " ", en->name);
        int w = (int)dx - col2 - 2;
        if (w < 1) w = 1;
        ncplane_printf_yx(n, top + r, col2, " %-*.*s", w, w, label);
        ncplane_set_bg_default(n);
    }

    /* keyboard strip */
    int ky = (int)dy - 5;
    hline(n, ky - 1, dx);
    ncplane_set_styles(n, NCSTYLE_BOLD);
    ncplane_set_fg_rgb8(n, 200, 200, 210);
    ncplane_printf_yx(n, ky, 1, "VOICES %3d   PEDAL %s   PLAY OCTAVE C%d  (z.. / q..)",
                      voices, pedal ? "DOWN" : "up ", g_base / 12 - 1);
    ncplane_set_styles(n, NCSTYLE_NONE);
    int kw = (int)dx - 2;
    for (int k = 0; k < 88 && k < kw; k++) {
        int note = 21 + k;
        int pc = note % 12;
        int black = (pc==1||pc==3||pc==6||pc==8||pc==10);
        if (active[note]) ncplane_set_fg_rgb8(n, 255, 230, 80);
        else if (black)   ncplane_set_fg_rgb8(n, 90, 90, 110);
        else              ncplane_set_fg_rgb8(n, 170, 170, 180);
        ncplane_putstr_yx(n, ky + 2, 1 + k, active[note] ? "#" : (black ? "." : "|"));
    }

    /* footer */
    ncplane_set_fg_rgb8(n, 120, 120, 130);
    ncplane_printf_yx(n, (int)dy - 1, 1,
        "TAB pane  up/dn select  left/right adjust  ENTER load  SPACE play/pause  "
        "[ ] seek  p pedal  +/- octave  Q quit");
    if (status && status[0]) {
        ncplane_set_fg_rgb8(n, 120, 230, 140);
        ncplane_printf_yx(n, (int)dy - 2, 1, "%.*s", (int)dx - 2, status);
    }

    notcurses_render(nc);
}

int main(int argc, char **argv)
{
    const char *lib = (argc > 1) ? argv[1] : DEFAULT_LIB;
    snprintf(g_dir, sizeof g_dir, "%s", lib);

    static pf_engine eng;
    pf_engine_init(&eng, SR);
    vals_from_defaults();
    apply_params(&eng);
    keymap_init();

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

    list_dir();

    int focus_lib = 1, pedal = 0, quit = 0;
    char status[256] = "pick a MIDI file and press ENTER, or play with the keyboard";

    while (!quit) {
        draw(nc, std, &eng, focus_lib, pedal, status);

        ncinput ni;
        uint32_t k;
        while ((k = notcurses_get_nblock(nc, &ni)) != 0) {
            if (k == (uint32_t)-1) { quit = 1; break; }
            int is_release = (ni.evtype == NCTYPE_RELEASE);

            /* music keys are always live, regardless of focused pane */
            if (k < 256 && g_keymap[k] >= 0) {
                if (is_release) key_release(&eng, (unsigned char)k);
                else            key_press(&eng, (unsigned char)k);
                continue;
            }
            if (is_release) continue;

            switch (k) {
            case 'Q': case NCKEY_ESC: quit = 1; break;
            case NCKEY_TAB: focus_lib = !focus_lib; break;
            case ' ': {
                double pos, dur; int pl;
                pf_engine_snapshot(&eng, NULL, &pos, &dur, &pl);
                if (pl) pf_engine_pause(&eng); else pf_engine_play(&eng);
                break;
            }
            case 'p': case 'P':
                pedal = !pedal; pf_engine_pedal(&eng, pedal); break;
            case '+': case '=':
                if (g_base <= 108 - 12) g_base += 12; break;
            case '-': case '_':
                if (g_base >= 12) g_base -= 12; break;
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

        keys_timeout(&eng);

        struct timespec ts = { 0, 16 * 1000 * 1000 };  /* ~60 fps */
        nanosleep(&ts, NULL);
    }

    notcurses_stop(nc);
    pf_audio_stop();
    pf_engine_destroy(&eng);
    free(g_entries);
    return 0;
}
