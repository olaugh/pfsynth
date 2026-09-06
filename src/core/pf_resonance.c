#include "pf_resonance.h"
#include <math.h>
#include <string.h>
static double mixd(double a,double b,double t){return a+(b-a)*t;}
void pf_resonance_defaults(pf_resonance_params *p)
{
    p->coupling_db=-32;p->skirt_hz=1.5f;p->sustain_db=-15;p->tilt_db=0;   /* fitted to Pianoteq lone notes with the pedal down vs up (tools/symp_fit.py, experiments/sympathetic) */p->partials=PF_RES_PARTIALS;
    p->t60_scale=1;p->t60_min=.5f;p->t60_max=25;p->damp_rate_db_s=80;
    p->damp_p_hi=.645f;p->damp_p_lo=.472f;p->damp_curve=.95f;p->undamped_from=90;
}
static void coeffs(pf_resonance *r,int s)
{
    double eng=1-r->open[s];
    for(int k=0;k<r->count[s];k++){
        double rr=r->r[s][k]*exp(-eng*r->drate[s][k]),w=6.283185307179586*r->f[s][k]/r->sr;
        r->a1[s][k]=2*rr*cos(w);r->a2[s][k]=rr*rr;
    }
}
void pf_resonance_init(pf_resonance *r,const pf_resonance_params *p,const pf_partial_patch *patch,double sr)
{
    memset(r,0,sizeof *r);r->sr=sr;r->mix=1;r->np=p->partials>PF_RES_PARTIALS?PF_RES_PARTIALS:p->partials;
    r->p_hi=p->damp_p_hi;r->p_lo=p->damp_p_lo;r->p_curve=p->damp_curve;r->skirt=p->skirt_hz;
    static const double times[PF_PARTIAL_POINTS]={0,.015,.04,.09,.2,.45,.9,1.8,3.6,6};
    for(int s=0;s<PF_RES_STRINGS;s++){
        double midi=21+s,key=(midi-21)/3;if(key>PF_PARTIAL_ANCHORS-1)key=PF_PARTIAL_ANCHORS-1;
        int lo=(int)key,hi=lo<PF_PARTIAL_ANCHORS-1?lo+1:lo;double t=key-lo;
        double f1=440*pow(2,(midi-69)/12)*mixd(patch->tuning[lo][0],patch->tuning[hi][0],t);
        double B=exp(mixd(log(patch->tuning[lo][1]),log(patch->tuning[hi][1]),t));
        r->count[s]=0;
        for(int k=0;k<r->np;k++){
            double h=k+1,f=f1*h*sqrt((1+B*h*h)/(1+B));if(f>sr*.44)break;r->count[s]=k+1;r->f[s][k]=f;
            /* free decay from the late slope of the patch envelope (velocity layer 1), else a T60 law */
            double db[PF_PARTIAL_POINTS];
            for(int j=0;j<PF_PARTIAL_POINTS;j++)db[j]=mixd(patch->envelope[lo][1][k][j],patch->envelope[hi][1][k][j],t)*.5-120;
            double rate=0;int ok=0;
            if(db[6]>-100&&db[8]>-112){rate=(db[8]-db[6])/(times[8]-times[6]);ok=rate<-1;}
            if(!ok&&db[6]>-100&&db[7]>-112){rate=(db[7]-db[6])/(times[7]-times[6]);ok=rate<-1;}
            double t60=ok?-60/rate:8*pow(f/110,-.6);
            t60*=p->t60_scale;if(t60<p->t60_min)t60=p->t60_min;if(t60>p->t60_max)t60=p->t60_max;
            r->r[s][k]=exp(-6.907755/(t60*sr));
            double u=log(f/100)/log(20.0);if(u<0)u=0;if(u>1)u=1;(void)u;
            r->drate[s][k]=midi>=p->undamped_from?0:p->damp_rate_db_s/8.685889638/sr;
            double w=6.283185307179586*f/sr,sw=sin(w);r->sin_w[s][k]=sw;
            double cdb=p->coupling_db+(f>250?p->tilt_db*log(f/250)/log(2.0):0);
            r->gimp[s][k]=pow(10,cdb/20)*sw;                     /* one-sample input of a*sin(w) rings with free amplitude a */
            r->gsus[s][k]=pow(10,p->sustain_db/20)*2*(1-r->r[s][k])*sw; /* resonant gain bounded at sustain_db */
        }
        r->open[s]=midi>=p->undamped_from?1:0;coeffs(r,s);r->active[s]=r->open[s]>0;
    }
}
void pf_resonance_reset(pf_resonance *r)
{
    memset(r->y1,0,sizeof r->y1);memset(r->y2,0,sizeof r->y2);
    for(int s=0;s<PF_RES_STRINGS;s++)r->active[s]=r->open[s]>.01;
}
double pf_resonance_engaged(const pf_resonance *r,double pedal)
{
    double e=(r->p_hi-pedal)/(r->p_hi-r->p_lo);if(e<0)e=0;if(e>1)e=1;return pow(e,r->p_curve);
}
void pf_resonance_open(pf_resonance *r,int midi,double open)
{
    int s=midi-21;if(s<0||s>=PF_RES_STRINGS)return;if(open<0)open=0;if(open>1)open=1;
    if(r->drate[s][0]==0)open=1;                                  /* no damper on this string */
    if(open==r->open[s])return;r->open[s]=open;coeffs(r,s);if(open>.01)r->active[s]=1;
}
void pf_resonance_strike(pf_resonance *r,const pf_partial *v,int midi)
{
    int self=midi-21;double fp[PF_PARTIAL_MODES],ap[PF_PARTIAL_MODES];int np=0;
    for(int k=0;k<v->count;k++){
        double f=r->sr*acos(v->cr[k][0])/6.283185307179586,a=v->amplitude[k][1];
        if(a>1e-7){fp[np]=f;ap[np]=a;np++;}
    }
    if(!np)return;
    double sk=r->skirt;
    for(int s=0;s<PF_RES_STRINGS;s++){
        if(s==self||r->open[s]<=.01)continue;
        for(int k=0;k<r->count[s];k++){
            double f=r->f[s][k],e=0;
            for(int p=0;p<np;p++){double d=(f-fp[p])/sk;e+=ap[p]/sqrt(1+d*d);}
            r->y1[s][k]+=e*r->open[s]*r->gimp[s][k];             /* impulse through the input: y[n] = g*x[n] + ... */
        }
        r->active[s]=1;
    }
}
void pf_resonance_process(pf_resonance *r,const float *in,float *out,int n,const unsigned char *voiced)
{
    for(int s=0;s<PF_RES_STRINGS;s++){
        if(!r->active[s])continue;
        int c=r->count[s];int drive=r->open[s]>.01&&!(voiced&&voiced[s]);
        double *y1=r->y1[s],*y2=r->y2[s],*a1=r->a1[s],*a2=r->a2[s],*g=r->gsus[s],e=0;
        for(int i=0;i<n;i++){
            double x=drive?in[i]:0,sum=0;
            for(int k=0;k<c;k++){
                double y=g[k]*x+a1[k]*y1[k]-a2[k]*y2[k];
                y2[k]=y1[k];y1[k]=y;sum+=y;
            }
            out[i]+=(float)(sum*r->mix);
        }
        for(int k=0;k<c;k++)e+=y1[k]*y1[k];
        if(!drive&&e<1e-20){r->active[s]=0;for(int k=0;k<c;k++)y1[k]=y2[k]=0;}
    }
}
