/* midi.h - Standard MIDI File loader. HOST-ONLY.
 *
 * Parses SMF format 0/1 (the maestro set is format 1, 480 PPQN), merges all
 * tracks, applies the tempo map, and flattens everything into a single array of
 * timed events in seconds. Only the messages the piano cares about survive:
 * note on, note off, and the sustain pedal (CC64).
 */
#ifndef PF_MIDI_H
#define PF_MIDI_H

enum {
    PF_EV_NOTE_OFF = 0,
    PF_EV_NOTE_ON  = 1,
    PF_EV_PEDAL    = 2   /* val: >=64 down, <64 up */
};

typedef struct {
    double        t;     /* seconds from start */
    unsigned char type;  /* PF_EV_* */
    unsigned char note;  /* MIDI note (note events) */
    unsigned char val;   /* velocity, or pedal value */
} pf_midi_event;

typedef struct {
    pf_midi_event *ev;
    int            n;
    double         duration;  /* seconds, incl. a tail for ring-out */
} pf_song;

/* Load `path` into `song`. Returns 0 on success; on failure song->ev is NULL
 * and a message is written to stderr. */
int  pf_midi_load(pf_song *song, const char *path);
void pf_midi_free(pf_song *song);

#endif
