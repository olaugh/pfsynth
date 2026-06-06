/* engine.c - see engine.h. Host-only. */
#include "engine.h"

#include <math.h>
#include <string.h>

#define RETIRE_LEVEL 1.0e-7   /* internal output scale ~3e-4 peak; this is ~ -70 dB */

static double midi_hz(int note)
{
    return 440.0 * pow(2.0, (note - 69) / 12.0);
}

static void rebuild_templates(pf_engine *e)
{
    for (int n = 0; n < 128; n++)
        pf_string_init(&e->templates[n], &e->params, midi_hz(n));
}

void pf_engine_init(pf_engine *e, double sr)
{
    memset(e, 0, sizeof *e);
    e->sr = sr;
    pf_string_defaults(&e->params, sr);
    e->master_gain = 110.0;    /* makeup gain: low enough that dense polyphony doesn't
                                * slam the tanh (500 clipped Bach badly); tanh catches peaks */
    pf_board_params bp;
    pf_board_defaults(&bp, sr);
    pf_board_stereo_init(&e->board, &bp, sr);
    pf_reverb_init(&e->reverb, sr);
    pthread_mutex_init(&e->lock, NULL);
    rebuild_templates(e);
}

void pf_engine_destroy(pf_engine *e)
{
    pf_midi_free(&e->song);
    pthread_mutex_destroy(&e->lock);
}

void pf_engine_set_params(pf_engine *e, const pf_string_params *p)
{
    pthread_mutex_lock(&e->lock);
    double sr = e->params.sample_rate;
    e->params = *p;
    e->params.sample_rate = sr;
    rebuild_templates(e);
    pthread_mutex_unlock(&e->lock);
}

void pf_engine_set_master(pf_engine *e, double gain)
{
    pthread_mutex_lock(&e->lock);
    e->master_gain = gain;
    pthread_mutex_unlock(&e->lock);
}

void pf_engine_set_body(pf_engine *e, double mix)
{
    pthread_mutex_lock(&e->lock);
    pf_board_stereo_set_mix(&e->board, mix);   /* runtime scalar; no rebuild needed */
    pthread_mutex_unlock(&e->lock);
}

void pf_engine_set_reverb(pf_engine *e, double wet)
{
    pthread_mutex_lock(&e->lock);
    pf_reverb_set(&e->reverb, 0.72, 0.35, wet);   /* fixed room/damp, variable wet (~0.3) */
    pthread_mutex_unlock(&e->lock);
}

void pf_engine_set_reference(pf_engine *e, const float *interleaved, long frames)
{
    pthread_mutex_lock(&e->lock);
    e->ref = interleaved;
    e->ref_frames = interleaved ? frames : 0;
    if (!interleaved) e->ref_on = 0;
    pthread_mutex_unlock(&e->lock);
}

void pf_engine_set_ref_on(pf_engine *e, int on)
{
    pthread_mutex_lock(&e->lock);
    e->ref_on = (on && e->ref) ? 1 : 0;
    pthread_mutex_unlock(&e->lock);
}

/* --- voice allocation (caller holds the lock) --- */

static int alloc_slot(pf_engine *e, int note)
{
    int free_i = -1, steal_i = -1;
    double steal_level = 1e30;
    for (int i = 0; i < PF_POLY; i++) {
        if (!e->slots[i].used) { free_i = i; break; }
        /* prefer stealing the quietest voice, released ones first */
        double l = e->slots[i].level;
        if (e->slots[i].held) l += 1.0;          /* keep held keys */
        if (l < steal_level) { steal_level = l; steal_i = i; }
    }
    int i = (free_i >= 0) ? free_i : steal_i;
    pf_slot *s = &e->slots[i];
    s->voice = e->templates[note];   /* fresh state + this note's coefficients */
    s->used = 1; s->note = note; s->held = 1; s->sustained = 0;
    s->age = e->clock; s->level = 1.0;  /* seed high so it isn't instantly stolen */
    return i;
}

static void note_on_locked(pf_engine *e, int note, int vel)
{
    if (note < 0 || note > 127) return;
    int i = alloc_slot(e, note);
    pf_string_strike(&e->slots[i].voice, vel / 127.0);
}

static void note_off_locked(pf_engine *e, int note)
{
    for (int i = 0; i < PF_POLY; i++) {
        pf_slot *s = &e->slots[i];
        if (s->used && s->note == note && s->held) {
            s->held = 0;
            if (e->pedal) s->sustained = 1;
            else          pf_string_release(&s->voice);
        }
    }
}

static void pedal_locked(pf_engine *e, int down)
{
    e->pedal = down;
    if (!down) {
        for (int i = 0; i < PF_POLY; i++) {
            pf_slot *s = &e->slots[i];
            if (s->used && s->sustained && !s->held) {
                s->sustained = 0;
                pf_string_release(&s->voice);
            }
        }
    }
}

void pf_engine_note_on(pf_engine *e, int note, int vel)
{
    pthread_mutex_lock(&e->lock); note_on_locked(e, note, vel); pthread_mutex_unlock(&e->lock);
}
void pf_engine_note_off(pf_engine *e, int note)
{
    pthread_mutex_lock(&e->lock); note_off_locked(e, note); pthread_mutex_unlock(&e->lock);
}
void pf_engine_pedal(pf_engine *e, int down)
{
    pthread_mutex_lock(&e->lock); pedal_locked(e, down); pthread_mutex_unlock(&e->lock);
}

void pf_engine_panic(pf_engine *e)
{
    pthread_mutex_lock(&e->lock);
    for (int i = 0; i < PF_POLY; i++) e->slots[i].used = 0;
    e->pedal = 0;
    pf_board_stereo_reset(&e->board);
    pf_reverb_reset(&e->reverb);
    pthread_mutex_unlock(&e->lock);
}

void pf_engine_load(pf_engine *e, pf_song *song)
{
    pthread_mutex_lock(&e->lock);
    pf_midi_free(&e->song);
    e->song = *song;
    memset(song, 0, sizeof *song);   /* engine now owns the events */
    e->has_song = 1;
    e->playing = 0;
    e->play_pos = 0.0;
    e->ev_cursor = 0;
    for (int i = 0; i < PF_POLY; i++) e->slots[i].used = 0;
    e->pedal = 0;
    pf_board_stereo_reset(&e->board);
    pf_reverb_reset(&e->reverb);
    pthread_mutex_unlock(&e->lock);
}

void pf_engine_play(pf_engine *e)
{
    pthread_mutex_lock(&e->lock);
    if (e->has_song) e->playing = 1;
    pthread_mutex_unlock(&e->lock);
}
void pf_engine_pause(pf_engine *e)
{
    pthread_mutex_lock(&e->lock); e->playing = 0; pthread_mutex_unlock(&e->lock);
}

void pf_engine_seek(pf_engine *e, double seconds)
{
    pthread_mutex_lock(&e->lock);
    if (e->has_song) {
        if (seconds < 0) seconds = 0;
        e->play_pos = seconds;
        for (int i = 0; i < PF_POLY; i++) e->slots[i].used = 0;
        e->pedal = 0;
        pf_board_stereo_reset(&e->board);
        pf_reverb_reset(&e->reverb);
        /* re-place the event cursor */
        int c = 0;
        while (c < e->song.n && e->song.ev[c].t < seconds) c++;
        e->ev_cursor = c;
    }
    pthread_mutex_unlock(&e->lock);
}

/* fire all sequencer events whose time has arrived (caller holds the lock) */
static void fire_due_events(pf_engine *e)
{
    while (e->ev_cursor < e->song.n && e->song.ev[e->ev_cursor].t <= e->play_pos) {
        pf_midi_event *ev = &e->song.ev[e->ev_cursor++];
        switch (ev->type) {
        case PF_EV_NOTE_ON:  note_on_locked(e, ev->note, ev->val); break;
        case PF_EV_NOTE_OFF: note_off_locked(e, ev->note);         break;
        case PF_EV_PEDAL:    pedal_locked(e, ev->val >= 64);       break;
        }
    }
}

/* render `n` frames of all live voices into a contiguous stereo region */
static void render_chunk(pf_engine *e, float *out, int n)
{
    for (int j = 0; j < 2 * n; j++) out[j] = 0.0f;

    for (int i = 0; i < PF_POLY; i++) {
        pf_slot *s = &e->slots[i];
        if (!s->used) continue;

        memset(e->scratch, 0, (size_t)n * sizeof(float));
        pf_string_process(&s->voice, e->scratch, n);

        double pk = 0.0;
        for (int j = 0; j < n; j++) {
            float v = e->scratch[j];
            double a = fabs(v);
            if (a > pk) pk = a;
            out[2 * j]     += v;
            out[2 * j + 1] += v;
        }
        /* peak-hold envelope with slow release for steal/retire decisions */
        s->level = (pk > s->level) ? pk : s->level * 0.85 + pk * 0.15;

        /* retire once the hammer is done and the string has rung out */
        if (!s->voice.ham_engaged && s->level < RETIRE_LEVEL) s->used = 0;
    }

    double g = e->master_gain;
    for (int j = 0; j < n; j++) {
        /* stereo soundboard over the (mono) mix, then room reverb, makeup, clip */
        double l, r;
        pf_board_stereo_tick(&e->board, out[2 * j], &l, &r);
        pf_reverb_tick(&e->reverb, l, r, &l, &r);
        out[2 * j]     = (float)tanh(l * g);
        out[2 * j + 1] = (float)tanh(r * g);
    }
    e->clock += (unsigned long)n;
}

void pf_engine_render(pf_engine *e, float *out, int frames)
{
    pthread_mutex_lock(&e->lock);
    double ref_pos0 = e->play_pos;   /* for the A/B reference, before we advance */
    int i = 0;
    while (i < frames) {
        int chunk = frames - i;
        if (chunk > PF_MAXBLOCK) chunk = PF_MAXBLOCK;

        if (e->playing && e->has_song) {
            fire_due_events(e);
            if (e->ev_cursor < e->song.n) {
                double dt = e->song.ev[e->ev_cursor].t - e->play_pos;
                int until = (int)(dt * e->sr);
                if (until < chunk) chunk = until < 1 ? 1 : until;
            } else if (e->play_pos > e->song.duration) {
                e->playing = 0;
            }
        }

        render_chunk(e, out + 2 * i, chunk);
        if (e->playing) e->play_pos += (double)chunk / e->sr;
        i += chunk;
    }

    /* A/B: overwrite the synth output with the reference recording at the same
     * position (the synth still ran above, so flipping back is seamless). */
    if (e->ref_on && e->ref) {
        long off = (long)(ref_pos0 * e->sr + 0.5);
        for (int j = 0; j < frames; j++) {
            long s = off + j;
            if (s >= 0 && s < e->ref_frames) {
                out[2 * j]     = e->ref[2 * s];
                out[2 * j + 1] = e->ref[2 * s + 1];
            } else {
                out[2 * j] = out[2 * j + 1] = 0.0f;
            }
        }
    }
    pthread_mutex_unlock(&e->lock);
}

int pf_engine_snapshot(pf_engine *e, unsigned char active[128],
                       double *pos, double *dur, int *playing, int *pedal)
{
    pthread_mutex_lock(&e->lock);
    if (active) memset(active, 0, 128);
    int live = 0;
    for (int i = 0; i < PF_POLY; i++) {
        if (e->slots[i].used) {
            live++;
            if (active) {
                int nn = e->slots[i].note;
                if (nn >= 0 && nn < 128) active[nn] = 1;
            }
        }
    }
    if (pos)     *pos = e->play_pos;
    if (dur)     *dur = e->has_song ? e->song.duration : 0.0;
    if (playing) *playing = e->playing;
    if (pedal)   *pedal = e->pedal;
    pthread_mutex_unlock(&e->lock);
    return live;
}
