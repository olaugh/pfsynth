/* Guitar-only web binding. Keeps the shared pfi.wasm and its consumers unchanged. */
#include "pf_guitar.h"
#include <string.h>
#include <math.h>
#define EXPORT(name) __attribute__((export_name(#name))) name
#define MAX_LOADING 256
static pf_guitar guitar;
static pf_note notes[PF_GUITAR_MAX_NOTES];
static pf_guitar_note_input inputs[PF_GUITAR_MAX_NOTES];
static float loading[MAX_LOADING][PF_PLUCK_MODES];
static signed char tuning[6];static pf_score score;
/* Original score coordinates remain immutable between loads. Tempo changes only
 * scheduling; the six vibrating strings, their owners and modal state survive. */
static double note_start[PF_GUITAR_MAX_NOTES], note_end[PF_GUITAR_MAX_NOTES];
static double gate_end[PF_GUITAR_MAX_NOTES], base_duration, tempo=1;
static int loaded;
static float left[4096],right[4096];static pf_sounding sounding[6];
#define I pf_instrument_guitar
int EXPORT(pfiw_init)(int which,double sr){if(which!=1||sr<8000||sr>192000)return 1;I.init(&guitar,sr);loaded=0;tempo=1;return 0;}
int EXPORT(pfiw_param_count)(void){return I.param_count();}
const char *EXPORT(pfiw_param_name)(int i){const pf_param_info *p=I.param_info(i);return p?p->name:"";}
const char *EXPORT(pfiw_param_unit)(int i){const pf_param_info *p=I.param_info(i);return p?p->unit:"";}
const char *EXPORT(pfiw_param_group)(int i){const pf_param_info *p=I.param_info(i);return p?p->group:"";}
double EXPORT(pfiw_param_min)(int i){const pf_param_info *p=I.param_info(i);return p?p->min:0;}
double EXPORT(pfiw_param_max)(int i){const pf_param_info *p=I.param_info(i);return p?p->max:0;}
double EXPORT(pfiw_param_default)(int i){const pf_param_info *p=I.param_info(i);return p?p->def:0;}
int EXPORT(pfiw_param_integer)(int i){const pf_param_info *p=I.param_info(i);return p?p->integer:0;}
double EXPORT(pfiw_get)(int i){return I.get(&guitar,i);}
void EXPORT(pfiw_set)(int i,double v){I.set(&guitar,i,v);}
pf_note *EXPORT(pfiw_notes)(void){return notes;}
signed char *EXPORT(pfiw_tuning)(void){return tuning;}
int EXPORT(pfiw_max_notes)(void){return PF_GUITAR_MAX_NOTES;}
int EXPORT(pfiw_note_size)(void){return sizeof(pf_note);}
int EXPORT(pfiw_sounding_size)(void){return sizeof(pf_sounding);}
pf_guitar_note_input *EXPORT(pfiw_guitar_inputs)(void){return inputs;}
int EXPORT(pfiw_guitar_input_size)(void){return sizeof(pf_guitar_note_input);}
float *EXPORT(pfiw_guitar_loading)(void){return &loading[0][0];}
int EXPORT(pfiw_guitar_max_loading)(void){return MAX_LOADING;}
int EXPORT(pfiw_guitar_set_loading)(int count){if(count<0||count>MAX_LOADING)return 1;guitar.loading=loading;guitar.loading_count=count;return 0;}
int EXPORT(pfiw_load)(int n,int nc,int nb,int ns,double duration){
    if(n<0||n>PF_GUITAR_MAX_NOTES||nc||nb||ns!=6||duration<=0)return 1;
    score=(pf_score){.notes=notes,.n_notes=n,.tuning=tuning,.n_strings=6,.duration=duration};
    guitar.note_inputs=inputs;
    int error=I.load(&guitar,&score);loaded=!error;tempo=1;base_duration=duration;
    if(!error)for(int i=0;i<n;i++){
        note_start[i]=notes[i].start;note_end[i]=notes[i].end;gate_end[i]=guitar.end[i];
    }
    return error;
}
/* Called between render blocks. Cursor keeps consumed events consumed; pending
 * releases and future attacks move to the new score clock. Harmonic finger contact
 * remains a physical duration, rather than stretching with the musical tempo. */
int EXPORT(pfiw_tempo)(double rate){
    if(!loaded||!isfinite(rate)||rate<.25||rate>1)return 1;
    if(rate==tempo)return 0;
    long old_pos=guitar.pos,new_pos=lrint((double)old_pos*tempo/rate);
    for(int i=0;i<guitar.n;i++){
        notes[i].start=note_start[i]/rate;notes[i].end=note_end[i]/rate;
        guitar.end[i]=gate_end[i]/rate;
    }
    for(int k=guitar.cursor;k<guitar.nev;k++){
        pf_guitar_event *e=&guitar.ev[k];int i=e->note,s=guitar.string[i];
        if(e->kind==0)e->frame=lrint(guitar.end[i]*guitar.sr);
        else if(e->kind==1)e->frame=lrint(notes[i].start*guitar.sr);
        else if(s>=1&&s<=6&&guitar.owner[s-1]==i)e->frame=new_pos+(e->frame-old_pos);
        else e->frame=lrint((notes[i].start+guitar.p[PF_GUITAR_HARM_TIME]/1000)*guitar.sr);
        if(e->frame<new_pos)e->frame=new_pos; /* rounding at a pending boundary */
    }
    /* Constant-duration touch lifts can change order relative to scaled releases. */
    for(int i=guitar.cursor+1;i<guitar.nev;i++){
        pf_guitar_event x=guitar.ev[i];int j=i-1;
        while(j>=guitar.cursor&&(guitar.ev[j].frame>x.frame||
             (guitar.ev[j].frame==x.frame&&(guitar.ev[j].kind>x.kind||
              (guitar.ev[j].kind==x.kind&&guitar.ev[j].note>x.note))))){
            guitar.ev[j+1]=guitar.ev[j];j--;
        }
        guitar.ev[j+1]=x;
    }
    guitar.pos=new_pos;score.duration=base_duration/rate;
    guitar.total=lrint(score.duration*guitar.sr);tempo=rate;return 0;
}
void EXPORT(pfiw_seek)(double t){
    /* Retiming only changed pending events. A deliberate seek must reconstruct
     * the complete schedule, including already-consumed attacks at the new rate. */
    int e=0;
    if(loaded){
        for(int i=0;i<guitar.n;i++){
            guitar.ev[e++]=(pf_guitar_event){lrint(notes[i].start*guitar.sr),1,i};
            guitar.ev[e++]=(pf_guitar_event){lrint(guitar.end[i]*guitar.sr),0,i};
            if(notes[i].articulation==PF_ART_HARMONIC)
                guitar.ev[e++]=(pf_guitar_event){lrint((notes[i].start+guitar.p[PF_GUITAR_HARM_TIME]/1000)*guitar.sr),2,i};
        }
        guitar.nev=e;
        for(int i=1;i<e;i++){
            pf_guitar_event x=guitar.ev[i];int j=i-1;
            while(j>=0&&(guitar.ev[j].frame>x.frame||
                (guitar.ev[j].frame==x.frame&&(guitar.ev[j].kind>x.kind||
                 (guitar.ev[j].kind==x.kind&&guitar.ev[j].note>x.note))))){
                guitar.ev[j+1]=guitar.ev[j];j--;
            }
            guitar.ev[j+1]=x;
        }
    }
    I.seek(&guitar,t);
}
double EXPORT(pfiw_time)(void){return I.time(&guitar);}
int EXPORT(pfiw_render)(int n){if(n<0)return 0;if(n>4096)n=4096;return I.render(&guitar,left,right,n);}
float *EXPORT(pfiw_left)(void){return left;}
float *EXPORT(pfiw_right)(void){return right;}
int EXPORT(pfiw_sounding)(void){return I.sounding(&guitar,sounding,6);}
pf_sounding *EXPORT(pfiw_sounding_buffer)(void){return sounding;}
