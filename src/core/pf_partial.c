#include "pf_partial.h"
#include <math.h>
#include <string.h>
static const double times[PF_PARTIAL_POINTS]={0,.015,.04,.09,.2,.45,.9,1.8,3.6,6};
static double mix(double a,double b,double t){return a+(b-a)*t;}
void pf_partial_init(pf_partial *v,const pf_partial_patch *p,double sr,double midi,double velocity)
{
    memset(v,0,sizeof *v);v->sr=sr;v->release_gain=1;
    v->release_rate=exp(-6.907755/(sr*.24));
    double key=(midi-36)/6; if(key<0)key=0;if(key>8)key=8;
    int lo=(int)key,hi=lo<8?lo+1:lo;double t=key-lo;
    double vel=(velocity*127-48)/52;if(vel<0)vel=0;if(vel>1)vel=1;
    double extra=velocity*127<48?pow(velocity*127/48,1.5):1;
    if(velocity*127>100)extra=pow(velocity*127/100,1.3);
    double f1=440*pow(2,(midi-69)/12)*mix(p->tuning[lo][0],p->tuning[hi][0],t);
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
void pf_partial_release(pf_partial *v){v->released=1;}
void pf_partial_process(pf_partial *v,float *out,int n)
{
    for(int i=0;i<n;i++,v->age++){
        double t=v->age/v->sr;int j=0;while(j<PF_PARTIAL_POINTS-2&&t>times[j+1])j++;
        double a=(t-times[j])/(times[j+1]-times[j]);if(a<0)a=0;
        double sum=0;
        for(int k=0;k<v->count;k++){
            double e0=v->amplitude[k][j],e1=v->amplitude[k][j+1];
            /* Log interpolation preserves exponential decay between control points. */
            double amp=e0*pow(e1/e0,a);if(t>6&&e1>e0)amp=e1*exp(-(t-6));
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
        double onset=t<.004?.5-.5*cos(3.141592653589793*t/.004):1;
        out[i]+=(float)(sum*v->release_gain*onset);
    }
}
