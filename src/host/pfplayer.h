/* pfplayer.h - polyphonic sequencer/renderer for the partial-model piano (pf_partial +
 * pf_attack + pedal model).  Portable C (no platform headers, no allocation): the same
 * code serves offline rendering, the macOS demo app's real-time audio, and later iOS.
 *
 * Feed it the flattened event list from pf_midi_load; pull mono float blocks.  Events are
 * applied sample-accurately.  Sustain (CC64) is either a switch (>= 64) with the legacy
 * fixed release, or a continuous damper position; sostenuto (CC66) captures the keys down
 * at the press; soft pedal (CC67) sets the una corda depth for new notes. */
#ifndef PF_PLAYER_H
#define PF_PLAYER_H
#include "../core/pf_partial.h"
#include "../core/pf_attack.h"
#include "../core/pf_resonance.h"
#include "midi.h"
#define PF_PLAYER_VOICES 96
typedef struct {
    int tone;         /* 0 = Salamander-fitted patch (frozen baseline), 1 = Pianoteq-fitted onset-exact patch */
    int attack;       /* 1 = add the pf_attack onset (soundboard thump + noise) */
    int pedal_mode;   /* 0 = binary sustain + fixed release, 1 = continuous damper following CC64 */
    int una_corda;    /* 1 = apply CC67 as una corda (needs pedal_mode 1) */
    double gain;      /* linear makeup before the soft clip; the model's absolute level is recording dBFS */
    double body_db, knock_db, noise_db; /* onset trims in dB relative to the fit: slow body modes, fast knock modes, noise burst */
    int limiter;      /* 1 = block lookahead peak limiter after the gain (live playback); 0 = none (offline: render in float, normalize afterwards) */
    int resonance;    /* 1 = sympathetic string resonance (pf_resonance) over the mix */
    double resonance_db; /* trim on the sympathetic bank output, 0 = as fitted */
} pf_player_options;
typedef struct {
    pf_partial p; pf_attack a;
    int used, note, held, sustained, sostenuto, attack;
    double start;     /* song time of note-on */
    float level;      /* last block RMS, for stealing and voice retirement */
} pf_player_voice;
typedef struct {
    double sr; pf_player_options opt; pf_pedal_params pedal; pf_attack_patch apatch;
    pf_resonance_params rp; pf_resonance res; int res_tone;   /* sympathetic bank, built for the patch of res_tone */
    const pf_midi_event *ev; int nev, next;
    double t, duration;
    double pedal_pos, soft; int pedal_down, sostenuto_down;
    pf_player_voice v[PF_PLAYER_VOICES];
    long frames;
    double lim_gain;  /* limiter gain state */
} pf_player;
void pf_player_defaults(pf_player_options *o);
void pf_player_init(pf_player *pl, double sr, const pf_player_options *o);
void pf_player_set_options(pf_player *pl, const pf_player_options *o);   /* tone/attack/soft apply to new notes; pedal mode to new and sustained notes */
void pf_player_set_resonance(pf_player *pl, const pf_resonance_params *rp); /* rebuild the sympathetic bank with other coupling parameters (fitting) */
void pf_player_load(pf_player *pl, const pf_midi_event *ev, int n, double duration); /* events must stay valid; sorted by time */
void pf_player_seek(pf_player *pl, double t);        /* silences voices, restores controller state at t */
int  pf_player_render(pf_player *pl, float *out, int frames);  /* mono, overwrites out; returns active voices */
double pf_player_time(const pf_player *pl);
int  pf_player_sounding(const pf_player *pl, unsigned char keys[128]); /* 1 per sounding MIDI note; returns count */
int  pf_player_active(const pf_player *pl);  /* voices in use */
const pf_partial_patch *pf_player_patch(int tone);
#endif
