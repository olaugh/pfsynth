#include "pf_attack.h"
#include "pf_partial.h"
#include <math.h>
#include <string.h>
static double mix(double a,double b,double t){return a+(b-a)*t;}
/* Interpolate an anchor/layer table in dB: linear in key between anchors, linear
 * in dB across the two velocity layers, extrapolated with the same slope. */
static double table(const float t[PF_ATTACK_ANCHORS][PF_ATTACK_LAYERS],double key,double vel)
{
    int lo=(int)key,hi=lo<PF_ATTACK_ANCHORS-1?lo+1:lo;double f=key-lo;
    double a=mix(t[lo][0],t[hi][0],f),b=mix(t[lo][1],t[hi][1],f);
    return mix(a,b,vel);
}
void pf_attack_init(pf_attack *v,const pf_attack_patch *p,double sr,double midi,double velocity)
{
    memset(v,0,sizeof *v);v->sr=sr;
    double key=(midi-21)/3;if(key<0)key=0;if(key>PF_ATTACK_ANCHORS-1)key=PF_ATTACK_ANCHORS-1;
    /* Layers sit at MIDI velocities 48 and 100.  Interpolate/extrapolate the dB tables
     * linearly in MIDI velocity: Pianoteq's thump level follows that within ~1 dB from
     * v30 to v120 (log-velocity interpolation overshoots by ~2 dB around v75). */
    double mv=velocity*127;if(mv<8)mv=8;if(mv>127)mv=127;
    double vel=(mv-48)/52;
    int klo=(int)key,khi=klo<PF_ATTACK_ANCHORS-1?klo+1:klo;double kf=key-klo;
    v->modes=0;
    for(int k=0;k<PF_ATTACK_MODES;k++){
        double f=p->mode_hz[k];if(f<=0||f>=sr*.45)continue;
        double kdb=mix(p->mode_db_key[klo][k],p->mode_db_key[khi][k],kf);
        double R=exp(-6.907755/(p->mode_t60[k]*sr)),w=6.283185307179586*f/sr;
        int m=v->modes++;
        v->a1[m]=2*R*cos(w);v->a2[m]=R*R;
        /* Impulse response of 1/(1-2Rcos w z^-1+R^2 z^-2) is R^n sin((n+1)w)/sin w:
         * scale by sin w so a unit impulse yields a sinusoid of unit peak.  Alternate
         * the sign of neighbouring modes (a soundboard's modes have mixed signs at the
         * bridge): with equal signs the dense modes cancel between resonances and the
         * bank's response has deep notches. */
        double grp=p->mode_t60[k]>=.5f?p->slow_mix:p->knock_mix;
        v->g[m]=(k&1?-1:1)*pow(10,(p->mode_db[k]+kdb)/20)*sin(w)*grp;
        v->delay[m]=(int)(p->mode_delay_ms[k]*.001*sr);
    }
    /* Per-mode Hann pulse, normalized to unit area (impulse-equivalent at low frequency):
     * at least pulse_ms long, and at least pulse_cycles periods of the mode. */
    double level=pow(10,table(p->thump_db,key,vel)/20)*p->thump_mix;int plen0=(int)(p->pulse_ms*.001*sr);if(plen0<1)plen0=1;
    for(int m=0,k=0;k<PF_ATTACK_MODES;k++){
        double f=p->mode_hz[k];if(f<=0||f>=sr*.45)continue;
        int L=(int)(p->pulse_cycles*sr/f);if(L<plen0)L=plen0;v->plen[m]=L;v->pgain[m]=level*2.0/L;m++;
    }
    v->noise_gain=pow(10,table(p->noise_db,key,vel)/20)*p->noise_mix;
    /* Tables are extrapolated beyond the two layers; keep the derived filter sane. */
    double nms=table(p->noise_ms,key,vel);if(nms<5)nms=5;
    v->noise_rate=exp(-6.907755/(nms*.001*sr));
    double fc=table(p->noise_hz,key,vel);if(fc<100)fc=100;if(fc>sr*.45)fc=sr*.45;
    v->lp_a=1-exp(-6.283185307179586*fc/sr);
    {double r=(1-v->lp_a)*(1-v->lp_a),pg=pow(v->lp_a,4)*(1+r)/((1-r)*(1-r)*(1-r));v->noise_comp=1/sqrt(pg);}
    v->rng=(unsigned)(midi*7919+velocity*104729)|1;
    v->onset_s=pf_onset_seconds(440*pow(2,(midi-69)/12));
}
void pf_attack_process(pf_attack *v,float *out,int n)
{
    for(int i=0;i<n;i++,v->age++){
        double sum=0;
        for(int m=0;m<v->modes;m++){
            int d=v->age-v->delay[m];double x=0;
            if(d>=0&&d<v->plen[m])x=v->pgain[m]*(.5-.5*cos(6.283185307179586*(d+.5)/v->plen[m]));
            double y=v->g[m]*x+v->a1[m]*v->y1[m]-v->a2[m]*v->y2[m];
            v->y2[m]=v->y1[m];v->y1[m]=y;sum+=y;
        }
        /* Gaussian-ish white noise from three uniform LCG draws (unit RMS), one-pole
         * lowpassed, exponentially enveloped; 1 ms fade-in avoids a step. */
        double u=0;for(int j=0;j<3;j++){v->rng=v->rng*1664525u+1013904223u;u+=(v->rng>>8)*(1.0/16777216)-.5;}
        u*=2.0;v->lp_z1+=v->lp_a*(u-v->lp_z1);v->lp_z2+=v->lp_a*(v->lp_z1-v->lp_z2);
        /* Two cascaded one-poles: white-noise power gain a^4(1+r)/(1-r)^3, r=(1-a)^2.
         * Compensate so noise_db is the actual output RMS at t=0. */
        double env=v->noise_gain*v->noise_comp;
        double t=v->age/v->sr;if(t<.001)env*=t*1000;
        sum+=env*v->lp_z2;
        v->noise_gain*=v->noise_rate;
        double ramp=t<v->onset_s?.5-.5*cos(3.141592653589793*t/v->onset_s):1;
        out[i]+=(float)(sum*ramp);
    }
}
