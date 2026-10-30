#include "scope_cursor.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
static uint32_t record[4096];
int main(void){
    for(unsigned i=0;i<4096;i++)record[i]=(400+i%100)|((600-i%100)<<16);
    unsigned pos[2]={250,750};ScopeCursorResult r;uint32_t copy[4096];memcpy(copy,record,sizeof(record));
    assert(scope_cursor_measure(record,4096,625000,10.25,100,pos,&r)==0);
    assert(fabs(r.delta_s-.00008)<1e-12&&r.reciprocal_valid&&fabs(r.reciprocal_hz-12500)<1e-6);
    assert(fabs(r.time_s[0]-.00004)<1e-12&&fabs(r.time_s[1]-.00012)<1e-12);
    assert(fabs(r.voltage_v[0][0]-(435.25*10/1023-5))<1e-9);
    assert(fabs(r.delta_v[0]-500.0/1023)<1e-9&&fabs(r.delta_v[1]+500.0/1023)<1e-9);
    assert(r.delta_valid[0]&&r.delta_valid[1]&&!memcmp(copy,record,sizeof(record)));
    pos[0]=750;pos[1]=250;assert(!scope_cursor_measure(record,4096,625000,10.25,100,pos,&r));
    assert(r.delta_s<0&&r.delta_v[0]<0&&r.reciprocal_hz>0);
    pos[0]=pos[1]=500;assert(!scope_cursor_measure(record,4096,625000,10.25,100,pos,&r));
    assert(r.delta_s==0&&r.delta_v[0]==0&&!r.reciprocal_valid);
    pos[0]=0;pos[1]=1000;
    assert(!scope_cursor_measure(record,4096,625000,4094,1,pos,&r)&&r.delta_valid[0]);
    assert(!scope_cursor_measure(record,4096,625000,4094.5,1,pos,&r)&&r.point_valid[0][0]&&!r.point_valid[0][1]&&!r.delta_valid[0]);
    assert(!scope_cursor_measure(record,4096,625000,4095,.1,pos,&r)&&r.point_valid[0][0]&&!r.point_valid[0][1]);
    record[11]|=1u<<10;
    assert(!scope_cursor_measure(record,4096,625000,10.25,10,pos,&r)&&!r.point_valid[0][0]&&r.point_valid[1][0]);
    record[11]&=~(1u<<10);record[20]|=1u<<26;
    assert(!scope_cursor_measure(record,4096,625000,10.25,10,pos,&r)&&!r.point_valid[1][1]&&r.delta_valid[0]);
    record[20]=copy[20];record[10]|=1u<<11;
    assert(scope_cursor_measure(record,4096,625000,10,10,pos,&r)==-1);
    record[10]=copy[10];pos[0]=1001;
    assert(scope_cursor_measure(record,4096,625000,0,10,pos,&r)==-1);
    pos[0]=0;
    assert(scope_cursor_measure(NULL,4096,625000,0,10,pos,&r)==-1);
    assert(scope_cursor_measure(record,4097,625000,0,10,pos,&r)==-1);
    assert(scope_cursor_measure(record,1,625000,0,10,pos,&r)==-1);
    assert(scope_cursor_measure(record,4096,0,0,10,pos,&r)==-1);
    assert(scope_cursor_measure(record,4096,625000,-1,10,pos,&r)==-1);
    assert(scope_cursor_measure(record,4096,625000,NAN,10,pos,&r)==-1);
    assert(scope_cursor_measure(record,4096,625000,0,INFINITY,pos,&r)==-1);
    assert(scope_cursor_measure(record,4096,625000,0,0,pos,&r)==-1);
    assert(scope_cursor_measure(record,4096,625000,0,10,NULL,&r)==-1);
    assert(scope_cursor_measure(record,4096,625000,0,10,pos,NULL)==-1);
    puts("CURSOR_CORE_TEST_PASS: raw interpolation, channel isolation, dt/dV/inverse, reversed/coincident, coverage, OTR, validation");
}
