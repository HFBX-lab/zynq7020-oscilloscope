#include "scope_spectrum.h"
#include <math.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

static unsigned checks,failures;
#define CHECK(x) do { ++checks; if(!(x)) { ++failures; \
    printf("FAIL line %d: %s\n",__LINE__,#x); } } while(0)
static const double pi=3.1415926535897932384626433832795;
static uint32_t words[SCOPE_SPECTRUM_N],held_words[SCOPE_SPECTRUM_N];
static ScopeSpectrumWorkspace work,held_work;
static ScopeSpectrumResult result,held_result;

static unsigned code(double v) {
    long n=lround(v);return n<0?0:n>1023?1023:(unsigned)n;
}
static void tones(double k0,double a0,double k1,double a1) {
    for(unsigned i=0;i<SCOPE_SPECTRUM_N;i++){
        double phase=2*pi*i/SCOPE_SPECTRUM_N;
        unsigned lo=code(600+a0*sin(phase*k0)+a1*cos(phase*k1));
        unsigned hi=code(350+90*cos(phase*91));
        words[i]=lo|(hi<<16);
    }
}
static double voltage(unsigned i,unsigned ch) {
    return ((words[i]>>(16*ch))&1023u)*10.0/1023.0-5;
}
/* Independent direct DFT; tests never use FFT work buffers as the reference. */
static double reference(unsigned bin,unsigned ch,ScopeSpectrumWindow window,bool remove) {
    double mean=0,re=0,im=0,sum=0;
    for(unsigned i=0;i<SCOPE_SPECTRUM_N;i++)mean+=voltage(i,ch);
    mean/=SCOPE_SPECTRUM_N;
    for(unsigned i=0;i<SCOPE_SPECTRUM_N;i++){
        double w=window==SCOPE_SPECTRUM_HANN?.5-.5*cos(2*pi*i/SCOPE_SPECTRUM_N):1;
        double value=(voltage(i,ch)-(remove?mean:0))*w;
        double phase=2*pi*bin*i/SCOPE_SPECTRUM_N;
        re+=value*cos(phase);im-=value*sin(phase);sum+=w;
    }
    return hypot(re,im)/sum*((bin&&bin!=SCOPE_SPECTRUM_N/2)?sqrt(2.0):1);
}
static void compare_bins(unsigned ch,ScopeSpectrumWindow window,bool remove) {
    static const unsigned bins[]={0,1,2,36,37,38,50,90,91,92,122,123,124,1024,2047,2048};
    memcpy(held_words,words,sizeof(words));
    CHECK(scope_spectrum_analyze(words,SCOPE_SPECTRUM_N,6250000,ch,window,remove,&work,&result)==0);
    CHECK(!memcmp(words,held_words,sizeof(words)));
    CHECK(result.valid&&result.count==4096&&result.rate_hz==6250000&&result.channel==ch);
    CHECK(result.window==(unsigned)window&&result.remove_mean==remove&&!result.overrange);
    CHECK(fabs(result.bin_hz-6250000.0/4096)<1e-12);
    CHECK(fabs(result.coherent_gain-(window==SCOPE_SPECTRUM_HANN?.5:1))<1e-10);
    double mean=0;for(unsigned i=0;i<SCOPE_SPECTRUM_N;i++)mean+=voltage(i,ch);
    CHECK(fabs(result.mean_v-mean/4096)<1e-11);
    for(unsigned i=0;i<sizeof(bins)/sizeof(bins[0]);i++){
        double ref=reference(bins[i],ch,window,remove);
        CHECK(fabs(result.vrms[bins[i]]-ref)<2e-6+2e-5*ref);
    }
    for(unsigned i=0;i<SCOPE_SPECTRUM_BINS;i++)CHECK(isfinite(result.vrms[i])&&result.vrms[i]>=0);
}
static void reset_sentinels(void) {
    memset(&work,0xa5,sizeof(work));memset(&result,0x5a,sizeof(result));
    memcpy(&held_work,&work,sizeof(work));memcpy(&held_result,&result,sizeof(result));
}
static void unchanged(void) {
    CHECK(!memcmp(&work,&held_work,sizeof(work)));
    CHECK(!memcmp(&result,&held_result,sizeof(result)));
}
static void failure(const uint32_t *p,unsigned n,unsigned rate,unsigned ch,ScopeSpectrumWindow window) {
    reset_sentinels();CHECK(scope_spectrum_analyze(p,n,rate,ch,window,true,&work,&result)==-1);unchanged();
}
static void invalid_inputs(void) {
    tones(37,150,0,0);
    failure(NULL,4096,6250000,0,SCOPE_SPECTRUM_HANN);
    failure(words,0,6250000,0,SCOPE_SPECTRUM_HANN);
    failure(words,4095,6250000,0,SCOPE_SPECTRUM_HANN);
    failure(words,4097,6250000,0,SCOPE_SPECTRUM_HANN);
    failure(words,4096,0,0,SCOPE_SPECTRUM_HANN);
    failure(words,4096,50000001,0,SCOPE_SPECTRUM_HANN);
    failure(words,4096,6250000,2,SCOPE_SPECTRUM_HANN);
    failure(words,4096,6250000,0,(ScopeSpectrumWindow)-1);
    failure(words,4096,6250000,0,(ScopeSpectrumWindow)2);
    reset_sentinels();CHECK(scope_spectrum_analyze(words,4096,6250000,0,SCOPE_SPECTRUM_HANN,true,NULL,&result)==-1);unchanged();
    reset_sentinels();CHECK(scope_spectrum_analyze(words,4096,6250000,0,SCOPE_SPECTRUM_HANN,true,&work,NULL)==-1);unchanged();
    for(unsigned bit=0;bit<32;bit++)if(UINT32_C(0xf800f800)&(UINT32_C(1)<<bit)){
        words[4095]|=UINT32_C(1)<<bit;
        failure(words,4096,6250000,0,SCOPE_SPECTRUM_HANN);
        words[4095]&=~(UINT32_C(1)<<bit);
    }
    /* All overlapping input/work/output pairs fail before any buffer changes. */
    reset_sentinels();CHECK(scope_spectrum_analyze((const uint32_t *)&work,4096,6250000,0,SCOPE_SPECTRUM_HANN,true,&work,&result)==-1);unchanged();
    reset_sentinels();CHECK(scope_spectrum_analyze(words,4096,6250000,0,SCOPE_SPECTRUM_HANN,true,&work,(ScopeSpectrumResult *)&work)==-1);unchanged();
    static union {max_align_t align;unsigned char bytes[65536];} alias,copy;
    memset(&alias,0x67,sizeof(alias));memcpy(alias.bytes,words,sizeof(words));memcpy(&copy,&alias,sizeof(alias));
    reset_sentinels();CHECK(scope_spectrum_analyze((const uint32_t *)alias.bytes,4096,6250000,0,SCOPE_SPECTRUM_HANN,true,&work,(ScopeSpectrumResult *)(alias.bytes+16))==-1);
    unchanged();CHECK(!memcmp(&alias,&copy,sizeof(alias)));
    reset_sentinels();CHECK(scope_spectrum_analyze(words,4096,6250000,0,SCOPE_SPECTRUM_HANN,true,(ScopeSpectrumWorkspace *)alias.bytes,(ScopeSpectrumResult *)(alias.bytes+32000))==-1);
    unchanged();CHECK(!memcmp(&alias,&copy,sizeof(alias)));
}
static void no_peak(void) {
    CHECK(!result.peak_valid&&result.peak_bin==0&&result.peak_hz==0&&result.peak_vrms==0&&result.peak_dbv==-120);
}
static void peak_and_boundaries(void) {
    tones(37,150,0,0);
    CHECK(!scope_spectrum_analyze(words,4096,6250000,0,SCOPE_SPECTRUM_HANN,true,&work,&result));
    CHECK(result.peak_valid&&fabs(result.peak_bin-37)<.001);
    CHECK(fabs(result.peak_vrms-150*10.0/1023/sqrt(2.0))<.001);
    CHECK(fabs(result.peak_dbv-20*log10(result.peak_vrms))<1e-12);
    tones(123.27,180,0,0);
    CHECK(!scope_spectrum_analyze(words,4096,50000000,0,SCOPE_SPECTRUM_HANN,true,&work,&result));
    CHECK(result.peak_valid&&fabs(result.peak_bin-123.27)<.02);
    CHECK(fabs(result.peak_vrms-reference(123,0,SCOPE_SPECTRUM_HANN,true))<2e-6);
    tones(50,80,123,180);
    CHECK(!scope_spectrum_analyze(words,4096,6250000,0,SCOPE_SPECTRUM_RECT,true,&work,&result));
    /* Rectangular coherent tones can have unequal quantization-noise neighbours;
     * compare the specified log interpolation to an independent direct DFT. */
    double a=log(reference(122,0,SCOPE_SPECTRUM_RECT,true));
    double b=log(reference(123,0,SCOPE_SPECTRUM_RECT,true));
    double c=log(reference(124,0,SCOPE_SPECTRUM_RECT,true));
    double expected_bin=123+.5*(a-c)/(a-2*b+c);
    CHECK(result.peak_valid&&lround(result.peak_bin)==123&&fabs(result.peak_bin-expected_bin)<.0001&&result.vrms[123]>2*result.vrms[50]);
    /* Adjacent unresolved tones must not turn a frequency interpolation into
     * a fictitious amplified spectral line. Direct DFT is the amplitude oracle. */
    tones(37,100,38,99);
    CHECK(!scope_spectrum_analyze(words,4096,6250000,0,SCOPE_SPECTRUM_RECT,true,&work,&result));
    CHECK(result.peak_valid&&result.peak_vrms<1.0);
    CHECK(fabs(result.peak_vrms-reference(37,0,SCOPE_SPECTRUM_RECT,true))<2e-6);
    for(unsigned i=0;i<4096;i++)words[i]=(i&1?500u:700u)|(512u<<16);
    for(unsigned window=0;window<2;window++){
        CHECK(!scope_spectrum_analyze(words,4096,6250000,0,(ScopeSpectrumWindow)window,true,&work,&result));
        CHECK(result.peak_valid&&result.peak_bin==2048&&result.peak_hz==3125000);
        CHECK(fabs(result.peak_vrms-100*10.0/1023)<1e-6);
    }
    for(unsigned i=0;i<4096;i++)words[i]=1023u|(512u<<16);
    CHECK(!scope_spectrum_analyze(words,4096,1,0,SCOPE_SPECTRUM_RECT,false,&work,&result));
    CHECK(fabs(result.vrms[0]-5)<1e-7&&result.mean_v==5);no_peak();
    CHECK(!scope_spectrum_analyze(words,4096,625,0,SCOPE_SPECTRUM_HANN,true,&work,&result));
    for(unsigned k=0;k<2049;k++)CHECK(result.vrms[k]==0);
    no_peak();
    /* The intentional bin-one gate rejects an unresolved one-cycle record. */
    tones(1,150,0,0);CHECK(!scope_spectrum_analyze(words,4096,6250000,0,SCOPE_SPECTRUM_HANN,true,&work,&result));no_peak();
    tones(37,1,0,0);CHECK(!scope_spectrum_analyze(words,4096,6250000,0,SCOPE_SPECTRUM_HANN,true,&work,&result));no_peak();
    /* A strong retained DC produces the documented Hann bin-one leakage. */
    tones(37,30,0,0);CHECK(!scope_spectrum_analyze(words,4096,6250000,0,SCOPE_SPECTRUM_HANN,false,&work,&result));no_peak();
    tones(37,150,0,0);words[17]|=1u<<26;
    CHECK(!scope_spectrum_analyze(words,4096,6250000,0,SCOPE_SPECTRUM_HANN,true,&work,&result));CHECK(!result.overrange&&result.peak_valid);
    words[18]|=1u<<10;CHECK(!scope_spectrum_analyze(words,4096,6250000,0,SCOPE_SPECTRUM_HANN,true,&work,&result));CHECK(result.overrange);no_peak();
    tones(37,150,0,0);
    CHECK(!scope_spectrum_analyze(words,4096,1,1,SCOPE_SPECTRUM_HANN,true,&work,&result));
    CHECK(result.peak_valid&&fabs(result.peak_hz-91.0/4096)<1e-6);
}
int main(void) {
    invalid_inputs();
    tones(37,150,0,0);
    for(unsigned ch=0;ch<2;ch++)for(unsigned w=0;w<2;w++)for(unsigned mean=0;mean<2;mean++)compare_bins(ch,(ScopeSpectrumWindow)w,mean!=0);
    peak_and_boundaries();
    printf("SPECTRUM_CORE_TEST: %u checks, %u failures\n",checks,failures);
    return failures?1:0;
}
