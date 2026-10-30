#include "scope_spectrum.h"
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned char bytes[SCOPE_SPECTRUM_N*4];
static uint32_t words[SCOPE_SPECTRUM_N];
static ScopeSpectrumWorkspace workspace;
static ScopeSpectrumResult result;

static int integer(const char *text,unsigned *out) {
    if(!text||!*text)return -1;
    for(const char *p=text;*p;p++)if(*p<'0'||*p>'9')return -1;
    errno=0;char *end;unsigned long value=strtoul(text,&end,10);
    if(errno||*end||value>UINT_MAX)return -1;
    *out=(unsigned)value;return 0;
}
int main(int argc,char **argv) {
    unsigned mean,channel,rate;ScopeSpectrumWindow window;
    if(argc!=6){fprintf(stderr,"usage: spectrum_fixture raw rect|hann mean(0|1) channel(0|1) rate\n");return 2;}
    if(!strcmp(argv[2],"rect"))window=SCOPE_SPECTRUM_RECT;
    else if(!strcmp(argv[2],"hann"))window=SCOPE_SPECTRUM_HANN;
    else {fprintf(stderr,"invalid window\n");return 2;}
    if(integer(argv[3],&mean)||integer(argv[4],&channel)||integer(argv[5],&rate)||mean>1||channel>1){fprintf(stderr,"invalid numeric argument\n");return 2;}
    FILE *file=fopen(argv[1],"rb");if(!file){perror("raw");return 2;}
    size_t length=fread(bytes,1,sizeof(bytes),file);int trailing=fgetc(file),error=ferror(file);
    if(fclose(file)||error||length!=sizeof(bytes)||trailing!=EOF){fprintf(stderr,"raw must be exactly 16384 bytes\n");return 2;}
    for(unsigned i=0;i<SCOPE_SPECTRUM_N;i++)words[i]=(uint32_t)bytes[4*i]|((uint32_t)bytes[4*i+1]<<8)|((uint32_t)bytes[4*i+2]<<16)|((uint32_t)bytes[4*i+3]<<24);
    if(scope_spectrum_analyze(words,SCOPE_SPECTRUM_N,rate,channel,window,mean!=0,&workspace,&result)){fprintf(stderr,"spectrum input rejected\n");return 1;}
    printf("{\"valid\":%s,\"overrange\":%s,\"peak_valid\":%s,\"remove_mean\":%s,\"count\":%u,\"rate_hz\":%u,\"channel\":%u,\"window\":%u,",
        result.valid?"true":"false",result.overrange?"true":"false",result.peak_valid?"true":"false",result.remove_mean?"true":"false",result.count,result.rate_hz,result.channel,result.window);
    printf("\"bin_hz\":%.17g,\"mean_v\":%.17g,\"coherent_gain\":%.17g,\"peak_bin\":%.17g,\"peak_hz\":%.17g,\"peak_vrms\":%.17g,\"peak_dbv\":%.17g,\"vrms\":[",
        result.bin_hz,result.mean_v,result.coherent_gain,result.peak_bin,result.peak_hz,result.peak_vrms,result.peak_dbv);
    for(unsigned i=0;i<SCOPE_SPECTRUM_BINS;i++)printf(i?",%.9g":"%.9g",(double)result.vrms[i]);
    puts("]}");return 0;
}
