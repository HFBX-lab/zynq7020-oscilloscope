#include "scope_spectrum.h"
#include <math.h>
#include <stddef.h>
#include <string.h>

static const double pi=3.1415926535897932384626433832795;

/* Subtraction form avoids wrapping end-address arithmetic. */
static bool overlap(const void *a,size_t an,const void *b,size_t bn) {
    uintptr_t av=(uintptr_t)a,bv=(uintptr_t)b;
    return av<=bv?bv-av<an:av-bv<bn;
}

static void transform(ScopeSpectrumWorkspace *w) {
    for(unsigned i=1,j=0;i<SCOPE_SPECTRUM_N;i++){
        unsigned bit=SCOPE_SPECTRUM_N>>1;
        for(;j&bit;bit>>=1)j^=bit;
        j^=bit;
        if(i<j){
            float value=w->real[i];w->real[i]=w->real[j];w->real[j]=value;
            value=w->imag[i];w->imag[i]=w->imag[j];w->imag[j]=value;
        }
    }
    for(unsigned length=2;length<=SCOPE_SPECTRUM_N;length<<=1){
        unsigned half=length>>1;
        double angle=-2*pi/length,c=cos(angle),s=sin(angle),wr=1,wi=0;
        for(unsigned j=0;j<half;j++){
            for(unsigned base=j;base<SCOPE_SPECTRUM_N;base+=length){
                unsigned other=base+half;
                double tr=wr*w->real[other]-wi*w->imag[other];
                double ti=wr*w->imag[other]+wi*w->real[other];
                double ur=w->real[base],ui=w->imag[base];
                w->real[base]=(float)(ur+tr);w->imag[base]=(float)(ui+ti);
                w->real[other]=(float)(ur-tr);w->imag[other]=(float)(ui-ti);
            }
            /* Double twiddle recurrence limits drift across long FFT stages. */
            double next=wr*c-wi*s;wi=wr*s+wi*c;wr=next;
        }
    }
}

int scope_spectrum_analyze_calibrated(const uint32_t *words,unsigned count,unsigned rate_hz,
    unsigned channel,ScopeSpectrumWindow window,bool remove_mean,
    const ScopeCalibration *profile,
    ScopeSpectrumWorkspace *workspace,ScopeSpectrumResult *out) {
    if(!words||!workspace||!out||count!=SCOPE_SPECTRUM_N||!rate_hz||
       rate_hz>50000000||channel>1||
       (window!=SCOPE_SPECTRUM_RECT&&window!=SCOPE_SPECTRUM_HANN)||!scope_calibration_valid(profile)||
       (profile&&(overlap(profile,sizeof(*profile),workspace,sizeof(*workspace))||
                  overlap(profile,sizeof(*profile),out,sizeof(*out))))||
       overlap(words,SCOPE_SPECTRUM_N*sizeof(*words),workspace,sizeof(*workspace))||
       overlap(words,SCOPE_SPECTRUM_N*sizeof(*words),out,sizeof(*out))||
       overlap(workspace,sizeof(*workspace),out,sizeof(*out)))return -1;
    uint64_t sum=0;
    bool overrange=false;
    for(unsigned i=0;i<SCOPE_SPECTRUM_N;i++){
        if(words[i]&UINT32_C(0xf800f800))return -1;
        unsigned sample=(words[i]>>(channel*16))&2047u;
        sum+=sample&1023u;overrange=overrange||((sample&1024u)!=0);
    }
    /* All rejection points precede the first workspace/output write. */
    double mean=scope_calibration_voltage(profile,channel,(double)sum/SCOPE_SPECTRUM_N);
    double slope=(scope_calibration_voltage(profile,channel,1023)-scope_calibration_voltage(profile,channel,0))/1023;
    double cw=cos(2*pi/SCOPE_SPECTRUM_N),sw=sin(2*pi/SCOPE_SPECTRUM_N);
    double wc=1,ws=0,weight_sum=0;
    for(unsigned i=0;i<SCOPE_SPECTRUM_N;i++){
        double weight=window==SCOPE_SPECTRUM_HANN?.5-.5*wc:1;
        double value=scope_calibration_voltage(profile,channel,(words[i]>>(channel*16))&1023u);
        workspace->real[i]=(float)((value-(remove_mean?mean:0))*weight);
        workspace->imag[i]=0;weight_sum+=weight;
        double next=wc*cw-ws*sw;ws=wc*sw+ws*cw;wc=next;
    }
    transform(workspace);
    memset(out,0,sizeof(*out));
    out->valid=true;out->overrange=overrange;out->remove_mean=remove_mean;
    out->count=count;out->rate_hz=rate_hz;out->channel=channel;out->window=(unsigned)window;
    out->bin_hz=(double)rate_hz/SCOPE_SPECTRUM_N;out->mean_v=mean;
    out->coherent_gain=weight_sum/SCOPE_SPECTRUM_N;out->peak_dbv=-120;
    unsigned maximum=1;
    for(unsigned i=0;i<SCOPE_SPECTRUM_BINS;i++){
        double magnitude=hypot((double)workspace->real[i],(double)workspace->imag[i]);
        double scale=(i&&i!=SCOPE_SPECTRUM_N/2)?1.4142135623730950488:1;
        out->vrms[i]=(float)(magnitude*scale/weight_sum);
        if(i&&out->vrms[i]>out->vrms[maximum])maximum=i;
    }
    if(!overrange&&maximum>=2&&out->vrms[maximum]>=2*slope){
        double bin=maximum,amplitude=out->vrms[maximum];
        if(maximum<=SCOPE_SPECTRUM_N/2-2&&out->vrms[maximum-1]>0&&out->vrms[maximum+1]>0){
            double a=log(out->vrms[maximum-1]),b=log(amplitude),c=log(out->vrms[maximum+1]);
            double curvature=a-2*b+c;
            if(curvature<0){
                double delta=.5*(a-c)/curvature;
                if(delta<-.5)delta=-.5;
                if(delta>.5)delta=.5;
                bin+=delta;
            }
        }
        out->peak_valid=true;out->peak_bin=bin;out->peak_hz=bin*out->bin_hz;
        /* Frequency interpolation does not estimate an unresolved tone's
         * amplitude. Report the actual strongest spectral line. */
        out->peak_vrms=amplitude;out->peak_dbv=20*log10(amplitude);
    }
    return 0;
}

int scope_spectrum_analyze(const uint32_t *words,unsigned count,unsigned rate_hz,
    unsigned channel,ScopeSpectrumWindow window,bool remove_mean,
    ScopeSpectrumWorkspace *workspace,ScopeSpectrumResult *out){
    return scope_spectrum_analyze_calibrated(words,count,rate_hz,channel,window,
        remove_mean,NULL,workspace,out);
}
