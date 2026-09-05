#include "pfplayer.h"
#include <math.h>
#include <string.h>
/* The fitted parameter sets are compiled in (no runtime file access, iOS-friendly). */
#include "../../experiments/partial-piano-wide/salamander.h" /* pf_partial_salamander: Salamander-fitted, C1..C8 */
#include "../../experiments/partial-piano-wide/pianoteq.h"   /* pf_partial_pianoteq: Pianoteq-fitted, onset-exact, C1..C8 */
#include "../../experiments/attack-ptq/patch_attack.h"   /* pf_attack_experiment */

const pf_partial_patch *pf_player_patch(int tone){return tone?&pf_partial_pianoteq:&pf_partial_salamander;}
void pf_player_defaults(pf_player_options *o){o->tone=1;o->attack=1;o->pedal_mode=1;o->una_corda=1;o->gain=4.0;}
void pf_player_init(pf_player *pl,double sr,const pf_player_options *o)
{
    memset(pl,0,sizeof *pl);pl->sr=sr;pl->opt=*o;pf_pedal_defaults(&pl->pedal);
}
static void voice_pedal(pf_player *pl,pf_player_voice *v)
{
    if(pl->opt.pedal_mode)pf_partial_pedal(&v->p,v->sostenuto?1.0:pl->pedal_pos);
}
void pf_player_set_options(pf_player *pl,const pf_player_options *o)
{
    pl->opt=*o;
    for(int i=0;i<PF_PLAYER_VOICES;i++)if(pl->v[i].used)voice_pedal(pl,&pl->v[i]);
}
static void kill_all(pf_player *pl){for(int i=0;i<PF_PLAYER_VOICES;i++)pl->v[i].used=0;}
static void restore_state(pf_player *pl,double t)
{
    pl->pedal_pos=0;pl->pedal_down=0;pl->soft=0;pl->sostenuto_down=0;pl->next=0;
    while(pl->next<pl->nev&&pl->ev[pl->next].t<t){
        const pf_midi_event *e=&pl->ev[pl->next++];
        if(e->type==PF_EV_PEDAL){pl->pedal_pos=e->val/127.0;pl->pedal_down=e->val>=64;}
        else if(e->type==PF_EV_SOFT)pl->soft=e->val/127.0;
        else if(e->type==PF_EV_SOSTENUTO)pl->sostenuto_down=e->val>=64;
    }
}
void pf_player_load(pf_player *pl,const pf_midi_event *ev,int n,double duration)
{
    pl->ev=ev;pl->nev=n;pl->duration=duration;kill_all(pl);pl->t=0;pl->frames=0;restore_state(pl,0);
}
void pf_player_seek(pf_player *pl,double t)
{
    if(t<0)t=0;kill_all(pl);pl->t=t;pl->frames=(long)(t*pl->sr);restore_state(pl,t);
}
double pf_player_time(const pf_player *pl){return pl->t;}
static pf_player_voice *alloc_voice(pf_player *pl)
{
    int best=-1;float worst=1e30f;
    for(int i=0;i<PF_PLAYER_VOICES;i++){
        pf_player_voice *v=&pl->v[i];
        if(!v->used)return v;
        float l=v->level+(v->held?10.f:0)+(v->sustained||v->sostenuto?1.f:0);  /* steal released, quiet voices first */
        if(l<worst){worst=l;best=i;}
    }
    return &pl->v[best];
}
static void note_on(pf_player *pl,int note,int vel)
{
    pf_player_voice *v=alloc_voice(pl);memset(v,0,sizeof *v);
    v->used=1;v->note=note;v->held=1;v->start=pl->t;v->level=1;
    double velocity=vel/127.0;
    if(pl->opt.pedal_mode){
        pf_partial_init2(&v->p,pf_player_patch(pl->opt.tone),pl->sr,note,velocity,&pl->pedal,pl->opt.una_corda?pl->soft:0);
        pf_partial_pedal(&v->p,pl->pedal_pos);
    }else pf_partial_init(&v->p,pf_player_patch(pl->opt.tone),pl->sr,note,velocity);
    v->attack=pl->opt.attack;
    if(v->attack)pf_attack_init(&v->a,&pf_attack_experiment,pl->sr,note,velocity);
}
static void release_voice(pf_player *pl,pf_player_voice *v){(void)pl;v->sustained=0;pf_partial_release(&v->p);}
static void note_off(pf_player *pl,int note)
{
    for(int i=0;i<PF_PLAYER_VOICES;i++){
        pf_player_voice *v=&pl->v[i];
        if(!v->used||v->note!=note||!v->held)continue;
        v->held=0;
        if(pl->opt.pedal_mode){pf_partial_release(&v->p);}          /* the damper follows the pedal position */
        else if(pl->pedal_down||v->sostenuto)v->sustained=1;         /* binary: keep ringing until pedal up */
        else release_voice(pl,v);
    }
}
static void pedal(pf_player *pl,int val)
{
    pl->pedal_pos=val/127.0;int down=val>=64;
    if(pl->opt.pedal_mode){for(int i=0;i<PF_PLAYER_VOICES;i++)if(pl->v[i].used)voice_pedal(pl,&pl->v[i]);}
    else if(pl->pedal_down&&!down){
        for(int i=0;i<PF_PLAYER_VOICES;i++){pf_player_voice *v=&pl->v[i];if(v->used&&v->sustained&&!v->held&&!v->sostenuto)release_voice(pl,v);}
    }
    pl->pedal_down=down;
}
static void sostenuto(pf_player *pl,int val)
{
    int down=val>=64;pl->sostenuto_down=down;
    for(int i=0;i<PF_PLAYER_VOICES;i++){
        pf_player_voice *v=&pl->v[i];if(!v->used)continue;
        if(down){if(v->held)v->sostenuto=1;}
        else if(v->sostenuto){
            v->sostenuto=0;
            if(pl->opt.pedal_mode)voice_pedal(pl,v);
            else if(!v->held&&!pl->pedal_down)release_voice(pl,v);
        }
    }
}
static void apply(pf_player *pl,const pf_midi_event *e)
{
    switch(e->type){
    case PF_EV_NOTE_ON:note_on(pl,e->note,e->val);break;
    case PF_EV_NOTE_OFF:note_off(pl,e->note);break;
    case PF_EV_PEDAL:pedal(pl,e->val);break;
    case PF_EV_SOSTENUTO:sostenuto(pl,e->val);break;
    case PF_EV_SOFT:pl->soft=e->val/127.0;break;
    default:break;
    }
}
int pf_player_render(pf_player *pl,float *out,int frames)
{
    memset(out,0,sizeof(float)*(size_t)frames);
    int done=0,active=0;
    while(done<frames){
        /* run up to the next event (sample-accurate) */
        int n=frames-done;
        while(pl->next<pl->nev){
            long at=(long)(pl->ev[pl->next].t*pl->sr);
            if(at<=pl->frames){apply(pl,&pl->ev[pl->next++]);continue;}
            if(at-pl->frames<n)n=(int)(at-pl->frames);
            break;
        }
        if(n<=0)n=1;
        for(int i=0;i<PF_PLAYER_VOICES;i++){
            pf_player_voice *v=&pl->v[i];if(!v->used)continue;
            float tmp[1024];int pos=0;double ss=0;
            while(pos<n){
                int m=n-pos;if(m>1024)m=1024;memset(tmp,0,sizeof(float)*(size_t)m);
                pf_partial_process(&v->p,tmp,m);if(v->attack)pf_attack_process(&v->a,tmp,m);
                for(int k=0;k<m;k++){out[done+pos+k]+=tmp[k];ss+=(double)tmp[k]*tmp[k];}
                pos+=m;
            }
            v->level=(float)sqrt(ss/n);
            /* retire voices that have died away (or whose fixed release is long over) */
            if(v->level<1e-6f&&pl->t-v->start>.5)v->used=0;
        }
        done+=n;pl->frames+=n;pl->t=pl->frames/pl->sr;
    }
    for(int i=0;i<PF_PLAYER_VOICES;i++)active+=pl->v[i].used;
    double g=pl->opt.gain;
    for(int k=0;k<frames;k++){double x=out[k]*g;out[k]=(float)(x<-3?-1:x>3?1:tanh(x));}
    return active;
}
int pf_player_sounding(const pf_player *pl,unsigned char keys[128])
{
    memset(keys,0,128);int c=0;
    for(int i=0;i<PF_PLAYER_VOICES;i++){const pf_player_voice *v=&pl->v[i];if(v->used&&v->level>1e-4f&&(v->held||v->sustained||v->sostenuto||v->p.pedal_pos>.6||pl->t-v->start<.4)){if(!keys[v->note])c++;keys[v->note]=1;}}
    return c;
}
int pf_player_active(const pf_player *pl){int n=0;for(int i=0;i<PF_PLAYER_VOICES;i++)n+=pl->v[i].used;return n;}
