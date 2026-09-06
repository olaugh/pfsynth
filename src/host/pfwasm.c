/* pfwasm.c - WebAssembly entry points for the partial-model piano (docs/ web demo).
 * Built with wasi-sdk as a "reactor" module; the page's AudioWorklet instantiates it
 * with WASI stubs (only fprintf(stderr) from the MIDI loader ever reaches them) and
 * pulls 128-frame blocks.  Everything is static: no file system, one player. */
#include "pfplayer.h"
#include <stdlib.h>
#include <math.h>
#include <string.h>
#define EXPORT(name) __attribute__((export_name(#name))) name
#define MIDI_MAX (4<<20)
static pf_player player;
static pf_player_options opt;
static pf_resonance_params rp;
static pf_song song;
static unsigned char midibuf[MIDI_MAX];
static float outbuf[4096];
static unsigned char keys[128];
static int loaded;
enum { OPT_TONE, OPT_ATTACK, OPT_PEDAL_MODE, OPT_UNA_CORDA, OPT_GAIN_DB, OPT_BODY_DB, OPT_KNOCK_DB, OPT_NOISE_DB, OPT_LIMITER,
       OPT_RESONANCE, OPT_RESONANCE_DB, OPT_RES_COUPLING, OPT_RES_SKIRT, OPT_RES_SUSTAIN, OPT_RES_TILT, OPT_RES_T60, OPT_TREBLE_DB, OPT_COUNT };
static double get_opt(int id)
{
    switch(id){
    case OPT_TONE:return opt.tone;case OPT_ATTACK:return opt.attack;case OPT_PEDAL_MODE:return opt.pedal_mode;case OPT_UNA_CORDA:return opt.una_corda;
    case OPT_GAIN_DB:return 20*log10(opt.gain);case OPT_BODY_DB:return opt.body_db;case OPT_KNOCK_DB:return opt.knock_db;case OPT_NOISE_DB:return opt.noise_db;
    case OPT_LIMITER:return opt.limiter;case OPT_RESONANCE:return opt.resonance;case OPT_RESONANCE_DB:return opt.resonance_db;
    case OPT_RES_COUPLING:return rp.coupling_db;case OPT_RES_SKIRT:return rp.skirt_hz;case OPT_RES_SUSTAIN:return rp.sustain_db;case OPT_RES_TILT:return rp.tilt_db;case OPT_RES_T60:return rp.t60_scale;case OPT_TREBLE_DB:return opt.treble_db;
    }
    return 0;
}
void EXPORT(pfw_init)(double sr)
{
    pf_player_defaults(&opt);opt.gain=2.0;pf_resonance_defaults(&rp);
    pf_player_init(&player,sr,&opt);
    if(loaded)pf_player_load(&player,song.ev,song.n,song.duration);
}
double EXPORT(pfw_default)(int id){pf_player_options d;pf_player_defaults(&d);d.gain=2.0;pf_resonance_params r;pf_resonance_defaults(&r);
    pf_player_options so=opt;pf_resonance_params sr=rp;opt=d;rp=r;double v=get_opt(id);opt=so;rp=sr;return v;}
double EXPORT(pfw_get)(int id){return get_opt(id);}
void EXPORT(pfw_set)(int id,double v)
{
    int res=0;
    switch(id){
    case OPT_TONE:opt.tone=v>.5;break;case OPT_ATTACK:opt.attack=v>.5;break;case OPT_PEDAL_MODE:opt.pedal_mode=v>.5;break;case OPT_UNA_CORDA:opt.una_corda=v>.5;break;
    case OPT_GAIN_DB:opt.gain=pow(10,v/20);break;case OPT_BODY_DB:opt.body_db=v;break;case OPT_KNOCK_DB:opt.knock_db=v;break;case OPT_NOISE_DB:opt.noise_db=v;break;
    case OPT_LIMITER:opt.limiter=v>.5;break;case OPT_RESONANCE:opt.resonance=v>.5;break;case OPT_RESONANCE_DB:opt.resonance_db=v;break;
    case OPT_RES_COUPLING:rp.coupling_db=(float)v;res=1;break;case OPT_RES_SKIRT:rp.skirt_hz=(float)v;res=1;break;case OPT_RES_SUSTAIN:rp.sustain_db=(float)v;res=1;break;
    case OPT_RES_TILT:rp.tilt_db=(float)v;res=1;break;case OPT_RES_T60:rp.t60_scale=(float)v;res=1;break;case OPT_TREBLE_DB:opt.treble_db=v;break;
    default:return;
    }
    pf_player_set_options(&player,&opt);
    if(res)pf_player_set_resonance(&player,&rp);
}
unsigned char *EXPORT(pfw_midi_buffer)(void){return midibuf;}
int EXPORT(pfw_midi_capacity)(void){return MIDI_MAX;}
/* Parse the SMF in the MIDI buffer (len bytes) and load it; returns the event count or -1. */
int EXPORT(pfw_load)(int len)
{
    if(loaded){pf_midi_free(&song);loaded=0;}
    if(len<=0||len>MIDI_MAX||pf_midi_parse(&song,midibuf,len))return -1;
    loaded=1;pf_player_load(&player,song.ev,song.n,song.duration);return song.n;
}
const pf_midi_event *EXPORT(pfw_events)(void){return loaded?song.ev:0;}
int EXPORT(pfw_event_size)(void){return (int)sizeof(pf_midi_event);}
double EXPORT(pfw_duration)(void){return loaded?song.duration:0;}
void EXPORT(pfw_seek)(double t){pf_player_seek(&player,t);}
double EXPORT(pfw_time)(void){return pf_player_time(&player);}
int EXPORT(pfw_active)(void){return pf_player_active(&player);}
float *EXPORT(pfw_render)(int frames){if(frames>4096)frames=4096;pf_player_render(&player,outbuf,frames);return outbuf;}
unsigned char *EXPORT(pfw_sounding)(void){pf_player_sounding(&player,keys);return keys;}
