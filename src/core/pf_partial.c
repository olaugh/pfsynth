#include "pf_partial.h"
#include <math.h>
#include <string.h>
static const double times[PF_PARTIAL_POINTS]={0,.015,.04,.09,.2,.45,.9,1.8,3.6,6};
static double mix(double a,double b,double t){return a+(b-a)*t;}
/* Pianoteq bass notes reach -3 dB of peak 20-40 ms after onset (A0 42 ms, C2 31, C3 23) while treble
 * notes are at full level within ~2 ms; a struck bass string builds up over several periods where a
 * plucked one would not.  22 ms (131 Hz / f1)^0.4 below C3, falling steeply above it. */
double pf_onset_seconds(double f1)
{
    double T=f1<131?.022*pow(131/f1,.4):.004*pow(131/f1,1.2)+.002;
    if(T<.002)T=.002;if(T>.045)T=.045;return T;
}
void pf_pedal_defaults(pf_pedal_params *p)
{
    /* Fitted to Pianoteq Steinway D Close Mic Classical (experiments/pedal/README.md): the
     * damper engages below CC64 ~82 and is fully seated at ~60, nearly linearly in between,
     * then removes ~80 dB/s; una corda cuts 2.1 dB on the fundamental growing 1.7 dB per octave
     * of partial index (a little more at high velocity) and lets the fundamental ring slower. */
    p->damp_p_hi=.645f;p->damp_p_lo=.472f;p->damp_curve=.95f;
    p->damp_rate_lo=80;p->damp_rate_hi=80;p->damp_floor_db=45;
    p->soft_db=2.1f;p->soft_tilt_db=1.66f;p->soft_tilt_vel=.13f;p->soft_curve=1.65f;
    p->soft_sustain_db_s=5;p->soft_sustain_max_db=6;
}
static void pedal_refresh(pf_partial *v)
{
    double eng=v->key_down?0:v->engaged;
    for(int k=0;k<v->count;k++)v->dfac[k]=exp(-eng*v->drate[k]);
}
void pf_partial_pedal(pf_partial *v,double position)
{
    if(!v->pedal_model)return;
    if(position<0)position=0;if(position>1)position=1;v->pedal_pos=position;
    double e=(v->p_hi-position)/(v->p_hi-v->p_lo);if(e<0)e=0;if(e>1)e=1;v->engaged=pow(e,v->p_curve);
    pedal_refresh(v);
}
void pf_partial_init2(pf_partial *v,const pf_partial_patch *p,double sr,double midi,double velocity,const pf_pedal_params *pp,double soft)
{
    pf_partial_init(v,p,sr,midi,velocity);
    if(!pp)return;
    v->pedal_model=1;v->key_down=1;v->pedal_pos=0;v->p_hi=pp->damp_p_hi;v->p_lo=pp->damp_p_lo;v->p_curve=pp->damp_curve;v->dfloor=pow(10,-pp->damp_floor_db/20);
    if(soft<0)soft=0;if(soft>1)soft=1;
    double s=pow(soft,pp->soft_curve),tilt=pp->soft_tilt_db+pp->soft_tilt_vel*(velocity*127-80)/30;
    double f1=v->count?sr*acos(v->cr[0][0])/6.283185307179586:440;
    for(int k=0;k<v->count;k++){
        double f=sr*acos(v->cr[k][0])/6.283185307179586;
        /* damper: dB/s law in partial frequency -> nepers/sample */
        double u=log(f/100)/log(20.0);if(u<0)u=0;if(u>1)u=1;
        double rate=pp->damp_rate_lo+(pp->damp_rate_hi-pp->damp_rate_lo)*u;
        /* the top ~1.5 octaves of a grand have no dampers: those strings ring until they die */
        if(midi>=90)rate=0;
        v->drate[k]=rate/8.685889638/sr;v->damp[k]=1;v->dfac[k]=1;
        /* una corda: level cut grows with partial index */
        double cut=s*(pp->soft_db+tilt*log(f/f1)/log(2.0));if(cut<0)cut=0;
        double g=pow(10,-cut/20);
        for(int j=0;j<PF_PARTIAL_POINTS;j++)v->amplitude[k][j]*=g;
    }
    /* una corda sustain of the fundamental: partials 1-2 lose less per second */
    v->sgrow=exp(s*pp->soft_sustain_db_s/8.685889638/sr);v->scap=pow(10,s*pp->soft_sustain_max_db/20);
    pf_partial_pedal(v,0);
}
void pf_partial_init(pf_partial *v,const pf_partial_patch *p,double sr,double midi,double velocity)
{
    memset(v,0,sizeof *v);v->sr=sr;v->release_gain=1;v->seg=-1;v->seg_end=0;
    v->release_rate=exp(-6.907755/(sr*.24));
    double key=(midi-21)/3; if(key<0)key=0;if(key>PF_PARTIAL_ANCHORS-1)key=PF_PARTIAL_ANCHORS-1;
    int lo=(int)key,hi=lo<PF_PARTIAL_ANCHORS-1?lo+1:lo;double t=key-lo;
    double vel=(velocity*127-48)/52;if(vel<0)vel=0;if(vel>1)vel=1;
    double extra=velocity*127<48?pow(velocity*127/48,1.5):1;
    if(velocity*127>100)extra=pow(velocity*127/100,1.3);
    double f1=440*pow(2,(midi-69)/12)*mix(p->tuning[lo][0],p->tuning[hi][0],t);
    v->onset_s=pf_onset_seconds(f1);
    double B=exp(mix(log(p->tuning[lo][1]),log(p->tuning[hi][1]),t));
    for(int k=0;k<PF_PARTIAL_MODES;k++){
        double h=k+1,f=f1*h*sqrt((1+B*h*h)/(1+B));if(f>sr*.44)break;v->count=k+1;
        for(int j=0;j<PF_PARTIAL_POINTS;j++){
            double a=mix(p->envelope[lo][0][k][j],p->envelope[lo][1][k][j],vel);
            double b=mix(p->envelope[hi][0][k][j],p->envelope[hi][1][k][j],vel);
            /* 0..255 codes represent -120..7.5 dBFS at half-dB intervals. */
            v->amplitude[k][j]=pow(10,(mix(a,b,t)*.5-120)/20)*extra;
        }
        /* Use a stable nearest-anchor phase. Phases are not linearly interpolated. */
        int anchor=t<.5?lo:hi,layer=vel<.5?0:1;
        double phase=p->phase[anchor][layer][k]*(6.283185307179586/256);
        for(int u=0;u<2;u++){
            /* Weak detuned branch approximates unison beating; not a coupled bridge. */
            double offset=(u?1:-1)*(.045+.00032*f);
            if(midi<40)offset*=.3;
            double w=6.283185307179586*(f+offset)/sr;
            v->re[k][u]=cos(phase);v->im[k][u]=sin(phase);
            v->cr[k][u]=cos(w);v->ci[k][u]=sin(w);
        }
    }
}
void pf_partial_release(pf_partial *v){if(v->pedal_model){v->key_down=0;pedal_refresh(v);}else v->released=1;}
void pf_partial_process(pf_partial *v,float *out,int n)
{
    for(int i=0;i<n;i++,v->age++){
        double t=v->age/v->sr;
        /* Log interpolation between the envelope knots preserves exponential decay.  It is
         * evaluated incrementally (one multiply per partial per sample, re-anchored at every
         * knot) instead of a pow() per partial per sample, which was the CPU bottleneck. */
        if(v->age>=v->seg_end){
            int j=v->seg+1;if(v->seg<0)j=0;
            if(j<=PF_PARTIAL_POINTS-2){
                double len=(times[j+1]-times[j])*v->sr;v->seg=j;v->seg_end=(long)(times[j+1]*v->sr);
                for(int k=0;k<v->count;k++){double e0=v->amplitude[k][j],e1=v->amplitude[k][j+1];v->cur[k]=e0;v->step[k]=pow(e1/e0,1.0/len);}
            }else{ /* past the last knot (6 s): keep the last decay rate, or a 1 s time constant if the tail was rising */
                v->seg=j;v->seg_end=0x7fffffffffffffffL;
                for(int k=0;k<v->count;k++){double e0=v->amplitude[k][PF_PARTIAL_POINTS-2],e1=v->amplitude[k][PF_PARTIAL_POINTS-1];if(e1>e0){v->cur[k]=e1;v->step[k]=exp(-1.0/v->sr);}}
            }
        }
        double sum=0;
        for(int k=0;k<v->count;k++){
            double amp=v->cur[k];v->cur[k]*=v->step[k];
            if(v->pedal_model){
                amp*=v->damp[k];if(v->damp[k]>v->dfloor)v->damp[k]*=v->dfac[k];
                if(k<2&&v->damp[k]<v->scap)v->damp[k]*=v->sgrow;   /* una corda fundamental sustain */
            }
            double s=0;
            for(int u=0;u<2;u++){
                double re=v->re[k][u],im=v->im[k][u];s+=re*(u?.25:.75);
                v->re[k][u]=re*v->cr[k][u]-im*v->ci[k][u];
                v->im[k][u]=re*v->ci[k][u]+im*v->cr[k][u];
            }
            sum+=amp*s;
        }
        if(v->released)v->release_gain*=v->release_rate;
        /* Suppress a discontinuity at voice creation without a synthetic click. */
        double onset=t<v->onset_s?.5-.5*cos(3.141592653589793*t/v->onset_s):1;
        out[i]+=(float)(sum*v->release_gain*onset);
    }
}
