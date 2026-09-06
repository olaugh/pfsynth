/* pfrender - render a MIDI file (or an excerpt) with the partial-model piano to WAV.
 *   pfrender in.mid out.wav [start_s [end_s]] [--tone sal|ptq] [--no-attack] [--pedal binary|continuous] [--no-soft] [--gain x] [--raw]
 * Peak-normalizes to -1 dBFS unless --raw. Also prints render speed and stats. */
#include "pfplayer.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>
static void wav_write(const char *path,const float *x,long n,int sr)
{
    FILE *f=fopen(path,"wb");if(!f){perror(path);exit(1);}
    unsigned char h[44]={'R','I','F','F',0,0,0,0,'W','A','V','E','f','m','t',' ',16,0,0,0,1,0,1,0};
    unsigned data=(unsigned)(n*2),riff=36+data;
    h[4]=riff;h[5]=riff>>8;h[6]=riff>>16;h[7]=riff>>24;h[24]=sr;h[25]=sr>>8;h[26]=sr>>16;h[27]=sr>>24;
    unsigned br=(unsigned)sr*2;h[28]=br;h[29]=br>>8;h[30]=br>>16;h[31]=br>>24;h[32]=2;h[34]=16;memcpy(h+36,"data",4);h[40]=data;h[41]=data>>8;h[42]=data>>16;h[43]=data>>24;
    fwrite(h,1,44,f);
    for(long i=0;i<n;i++){double v=x[i];if(v>1)v=1;if(v<-1)v=-1;short s=(short)lrint(v*32767);fputc(s&255,f);fputc((s>>8)&255,f);}
    fclose(f);
}
int main(int argc,char **argv)
{
    if(argc<3){fprintf(stderr,"usage: pfrender in.mid out.wav [start [end]] [--tone sal|ptq] [--no-attack] [--pedal binary|continuous] [--no-soft] [--gain x] [--raw] [--body dB] [--knock dB] [--noise dB] [--treble dB] [--no-resonance] [--resonance-db dB] [--res-coupling dB] [--res-skirt Hz] [--res-sustain dB] [--res-tilt dB/oct] [--res-partials n] [--res-t60 x]\n");return 2;}
    pf_player_options o;pf_player_defaults(&o);o.gain=1;o.limiter=0;double start=0,end=-1;int raw=0,pos=0;
    pf_resonance_params rp;pf_resonance_defaults(&rp);
    for(int i=3;i<argc;i++){
        if(!strcmp(argv[i],"--tone")&&i+1<argc)o.tone=!strcmp(argv[++i],"ptq");
        else if(!strcmp(argv[i],"--no-attack"))o.attack=0;
        else if(!strcmp(argv[i],"--pedal")&&i+1<argc)o.pedal_mode=!strcmp(argv[++i],"continuous");
        else if(!strcmp(argv[i],"--no-soft"))o.una_corda=0;
        else if(!strcmp(argv[i],"--gain")&&i+1<argc)o.gain=atof(argv[++i]);
        else if(!strcmp(argv[i],"--raw"))raw=1;
        else if(!strcmp(argv[i],"--body")&&i+1<argc)o.body_db=atof(argv[++i]);
        else if(!strcmp(argv[i],"--knock")&&i+1<argc)o.knock_db=atof(argv[++i]);
        else if(!strcmp(argv[i],"--noise")&&i+1<argc)o.noise_db=atof(argv[++i]);
        else if(!strcmp(argv[i],"--treble")&&i+1<argc)o.treble_db=atof(argv[++i]);
        else if(!strcmp(argv[i],"--top-knock-t60")&&i+1<argc)o.top_knock_t60=atof(argv[++i]);
        else if(!strcmp(argv[i],"--top-knock-db")&&i+1<argc)o.top_knock_db=atof(argv[++i]);
        else if(!strcmp(argv[i],"--top-knock-lo")&&i+1<argc)o.top_knock_lo=atof(argv[++i]);
        else if(!strcmp(argv[i],"--no-resonance"))o.resonance=0;
        else if(!strcmp(argv[i],"--resonance-db")&&i+1<argc)o.resonance_db=atof(argv[++i]);
        else if(!strcmp(argv[i],"--res-coupling")&&i+1<argc)rp.coupling_db=(float)atof(argv[++i]);
        else if(!strcmp(argv[i],"--res-skirt")&&i+1<argc)rp.skirt_hz=(float)atof(argv[++i]);
        else if(!strcmp(argv[i],"--res-sustain")&&i+1<argc)rp.sustain_db=(float)atof(argv[++i]);
        else if(!strcmp(argv[i],"--res-tilt")&&i+1<argc)rp.tilt_db=(float)atof(argv[++i]);
        else if(!strcmp(argv[i],"--res-partials")&&i+1<argc)rp.partials=atoi(argv[++i]);
        else if(!strcmp(argv[i],"--res-t60")&&i+1<argc)rp.t60_scale=(float)atof(argv[++i]);
        else if(pos==0){start=atof(argv[i]);pos++;}
        else if(pos==1){end=atof(argv[i]);pos++;}
    }
    pf_song song;if(pf_midi_load(&song,argv[1]))return 1;
    if(end<0||end>song.duration)end=song.duration;
    int sr=44100;static pf_player pl;pf_player_init(&pl,sr,&o);pf_player_set_resonance(&pl,&rp);pf_player_load(&pl,song.ev,song.n,song.duration);pf_player_seek(&pl,start);
    long n=(long)((end-start)*sr);float *buf=(float*)calloc((size_t)n,sizeof(float));if(!buf)return 1;
    clock_t c0=clock();int maxv=0;
    for(long i=0;i<n;i+=512){int m=(int)(n-i<512?n-i:512);int a=pf_player_render(&pl,buf+i,m);if(a>maxv)maxv=a;}
    double secs=(double)(clock()-c0)/CLOCKS_PER_SEC;
    double peak=0,ss=0;for(long i=0;i<n;i++){double v=fabs(buf[i]);if(v>peak)peak=v;ss+=buf[i]*buf[i];}
    if(!raw&&peak>0){double g=pow(10,-1.0/20)/peak;for(long i=0;i<n;i++)buf[i]*=(float)g;}
    wav_write(argv[2],buf,n,sr);
    printf("%s: %d events, rendered %.1f s in %.2f s (%.1fx real time), peak %.1f dBFS rms %.1f dBFS, max voices %d\n",argv[1],song.n,(end-start),secs,(end-start)/secs,20*log10(peak+1e-12),10*log10(ss/n+1e-18),maxv);
    free(buf);pf_midi_free(&song);return 0;
}
