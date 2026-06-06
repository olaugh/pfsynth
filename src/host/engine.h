/* engine.h - polyphonic piano engine driving the pf_string core. HOST-ONLY.
 *
 * Owns a pool of voices, a per-note bank of pre-initialized voice templates (so
 * note-on is just a memcpy + strike, never an init in the audio thread), the
 * sustain pedal state, and a sample-accurate sequencer that plays a loaded song.
 *
 * The audio backend calls pf_engine_render() from its real-time thread; the UI
 * thread calls the note/pedal/param/transport functions. A single mutex guards
 * the whole engine - not strictly RT-safe, but glitches only occur exactly when
 * the user twiddles a knob, which is fine for a dev tool.
 */
#ifndef PF_ENGINE_H
#define PF_ENGINE_H

#include "../core/pf_string.h"
#include "../core/pf_board.h"
#include "../core/pf_reverb.h"
#include "midi.h"
#include <pthread.h>

#define PF_POLY      96      /* simultaneous ringing strings */
#define PF_MAXBLOCK  2048

typedef struct {
    pf_string voice;
    int           used;
    int           note;
    int           held;       /* key currently down */
    int           sustained;  /* released but kept ringing by the pedal */
    unsigned long age;        /* strike clock, for voice stealing */
    double        level;      /* recent output peak (for steal/retire) */
} pf_slot;

typedef struct {
    double sr;
    pf_string_params params;
    pf_string templates[128];     /* one per MIDI note, rebuilt on param change */

    pf_slot slots[PF_POLY];
    int     pedal;                /* sustain pedal down */
    unsigned long clock;
    double  master_gain;
    pf_board_stereo board;        /* one shared stereo modal soundboard over the mix */
    pf_reverb       reverb;       /* room reverb over the final stereo mix */

    /* A/B reference: a real recording of the loaded piece, output in place of the
     * synth when ref_on (synth still renders underneath, so toggling is instant).
     * The host owns the buffer; the engine just borrows the pointer. */
    const float *ref;             /* interleaved stereo, host-owned */
    long         ref_frames;
    int          ref_on;

    /* sequencer */
    pf_song song;
    int     has_song;
    int     playing;
    double  play_pos;             /* seconds */
    int     ev_cursor;

    float   scratch[PF_MAXBLOCK];
    pthread_mutex_t lock;
} pf_engine;

void pf_engine_init(pf_engine *e, double sr);
void pf_engine_destroy(pf_engine *e);

/* Replace patch parameters and rebuild the note templates. */
void pf_engine_set_params(pf_engine *e, const pf_string_params *p);
void pf_engine_set_master(pf_engine *e, double gain);
void pf_engine_set_body(pf_engine *e, double mix);   /* soundboard amount, 0 = bypass */
void pf_engine_set_reverb(pf_engine *e, double wet);  /* room amount, 0 = dry */

/* A/B reference recording (interleaved stereo at the engine sample rate, host-
 * owned). Pass NULL to clear. set_ref_on toggles output between synth and ref. */
void pf_engine_set_reference(pf_engine *e, const float *interleaved, long frames);
void pf_engine_set_ref_on(pf_engine *e, int on);

/* Live triggers (UI keyboard). velocity 1..127 like MIDI. */
void pf_engine_note_on(pf_engine *e, int note, int vel);
void pf_engine_note_off(pf_engine *e, int note);
void pf_engine_pedal(pf_engine *e, int down);
void pf_engine_panic(pf_engine *e);   /* silence everything immediately */

/* Transport. load takes ownership of *song (zeroes the caller's copy). */
void pf_engine_load(pf_engine *e, pf_song *song);
void pf_engine_play(pf_engine *e);
void pf_engine_pause(pf_engine *e);
void pf_engine_seek(pf_engine *e, double seconds);

/* RT render: interleaved stereo float32, `frames` frames. */
void pf_engine_render(pf_engine *e, float *out, int frames);

/* Snapshot for the UI (all under one lock). active[128] gets 1 for sounding
 * notes; returns live voice count. pos and dur in seconds, playing and pedal
 * (sustain currently engaged, whether by the UI or a MIDI CC64) if non-NULL. */
int  pf_engine_snapshot(pf_engine *e, unsigned char active[128],
                        double *pos, double *dur, int *playing, int *pedal);

#endif
