#include "scope_measurement.h"
#include <assert.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>

static uint32_t words[4096];
static void tone(unsigned rate,double hz){
 for(unsigned i=0;i<4096;i++)words[i]=504u|((uint32_t)lround(563+51*sin(6.283185307179586*hz*i/rate))<<16);
}
int main(void){
 double hz=123;
 const unsigned rates[]={50000000,6250000,625000,625};
 const double tones[]={50000,50000,1000,1};
 for(unsigned i=0;i<4;i++){tone(rates[i],tones[i]);assert(scope_measurement_frequency(words,4096,rates[i],1,&hz));assert(fabs(hz/tones[i]-1)<.005);assert(!scope_measurement_frequency(words,4096,rates[i],0,&hz)&&hz==0);}
 tone(50000000,1000);assert(!scope_measurement_frequency(words,4096,50000000,1,&hz)&&hz==0);
 tone(625000,1000);words[4095]|=0x80000000u;assert(!scope_measurement_frequency(words,4096,625000,1,&hz)&&hz==0);
 assert(!scope_measurement_frequency(NULL,4096,625000,1,&hz)&&hz==0);
 assert(!scope_measurement_frequency(words,4097,625000,1,&hz));
 assert(!scope_measurement_frequency(words,0,625000,1,&hz));
 assert(!scope_measurement_frequency(words,4096,0,1,&hz));
 assert(!scope_measurement_frequency(words,4096,625000,2,&hz));
 assert(!scope_measurement_frequency(words,4096,625000,1,NULL));
 puts("SHARED_FREQUENCY_TEST_PASS: four rates/short record/DC/reserved/invalid arguments");
 return 0;
}
