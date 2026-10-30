#include "scope_settings.h"
#include "scope_acquisition.h"
#include "scope_record.h"
#include <string.h>

static uint32_t get32(const uint8_t *p) {
    return (uint32_t)p[0]|((uint32_t)p[1]<<8)|
           ((uint32_t)p[2]<<16)|((uint32_t)p[3]<<24);
}
static void put32(uint8_t *p,uint32_t x) {
    p[0]=(uint8_t)x;p[1]=(uint8_t)(x>>8);
    p[2]=(uint8_t)(x>>16);p[3]=(uint8_t)(x>>24);
}
static int32_t signed32(uint32_t x) {
    return x<=INT32_MAX?(int32_t)x:-1-(int32_t)(UINT32_MAX-x);
}
static int overlap(const void *a,size_t an,const void *b,size_t bn) {
    uintptr_t av=(uintptr_t)a,bv=(uintptr_t)b;
    return av<=bv?bv-av<an:av-bv<bn;
}
static int valid_timebase(const ScopeSettings *s) {
    static const uint32_t ns[]={100,200,500,1000,2000,5000,10000,20000,
        50000,100000,200000,500000,1000000,2000000,5000000,
        10000000,20000000,50000000,100000000,200000000,500000000};
    unsigned first;
    ScopeCapturePlan plan;
    switch(s->origin_conversion_hz) {
    case 5000000:first=8;break;
    case 10000000:case 25000000:first=3;break;
    case 50000000:first=0;break;
    default:return 0;
    }
    for(unsigned i=first;i<sizeof(ns)/sizeof(ns[0]);++i)
        if(ns[i]==s->timebase_ns)
            return scope_acquisition_plan_for(i-first,s->origin_conversion_hz,&plan)==0;
    return 0;
}
static int valid(const ScopeSettings *s) {
    return valid_timebase(s)&&!(s->flags&~31u)&&s->selected_channel<=1&&
        s->trigger_channel<=1&&s->trigger_level_mv>=-5000&&s->trigger_level_mv<=5000&&
        s->scale_index[0]<=6&&s->scale_index[1]<=6&&
        s->position_mdiv[0]>=-3500&&s->position_mdiv[0]<=3500&&
        s->position_mdiv[1]>=-3500&&s->position_mdiv[1]<=3500&&
        s->cursor[0]<=1000&&s->cursor[1]<=1000&&s->language<=1;
}
int scope_settings_encode(const ScopeSettings *s,uint8_t *b,size_t n,size_t *w) {
    if(w)*w=0;
    if(!s||!b||n<SCOPE_SETTINGS_BLOB_BYTES||
       overlap(s,sizeof(*s),b,SCOPE_SETTINGS_BLOB_BYTES)||
       (w&&(overlap(w,sizeof(*w),s,sizeof(*s))||
            overlap(w,sizeof(*w),b,SCOPE_SETTINGS_BLOB_BYTES)))||!valid(s))return -1;
    memset(b,0,SCOPE_SETTINGS_BLOB_BYTES);
    memcpy(b,"SCP1",4);b[4]=1;b[6]=SCOPE_SETTINGS_BLOB_BYTES;
    put32(b+8,SCOPE_SETTINGS_BLOB_BYTES);
    put32(b+16,s->timebase_ns);put32(b+20,s->origin_conversion_hz);
    put32(b+24,s->selected_channel);put32(b+28,s->trigger_channel);
    put32(b+32,(uint32_t)s->trigger_level_mv);put32(b+36,s->flags);
    put32(b+40,s->scale_index[0]);put32(b+44,s->scale_index[1]);
    put32(b+48,(uint32_t)s->position_mdiv[0]);put32(b+52,(uint32_t)s->position_mdiv[1]);
    put32(b+56,s->cursor[0]);put32(b+60,s->cursor[1]);put32(b+64,s->language);
    put32(b+12,scope_record_crc32(b,SCOPE_SETTINGS_BLOB_BYTES));
    if(w)*w=SCOPE_SETTINGS_BLOB_BYTES;
    return 0;
}
int scope_settings_decode(const uint8_t *b,size_t n,ScopeSettings *s) {
    uint8_t checked_blob[SCOPE_SETTINGS_BLOB_BYTES];
    ScopeSettings checked;
    if(!b||!s||n!=SCOPE_SETTINGS_BLOB_BYTES||overlap(b,n,s,sizeof(*s))||
       memcmp(b,"SCP1",4)||b[4]!=1||b[5]!=0||
       b[6]!=SCOPE_SETTINGS_BLOB_BYTES||b[7]!=0||get32(b+8)!=SCOPE_SETTINGS_BLOB_BYTES)return -1;
    for(unsigned i=68;i<SCOPE_SETTINGS_BLOB_BYTES;++i)if(b[i])return -1;
    memcpy(checked_blob,b,sizeof(checked_blob));memset(checked_blob+12,0,4);
    if(get32(b+12)!=scope_record_crc32(checked_blob,sizeof(checked_blob)))return -1;
    memset(&checked,0,sizeof(checked));
    checked.timebase_ns=get32(b+16);checked.origin_conversion_hz=get32(b+20);
    checked.selected_channel=get32(b+24);checked.trigger_channel=get32(b+28);
    checked.trigger_level_mv=signed32(get32(b+32));checked.flags=get32(b+36);
    checked.scale_index[0]=get32(b+40);checked.scale_index[1]=get32(b+44);
    checked.position_mdiv[0]=signed32(get32(b+48));checked.position_mdiv[1]=signed32(get32(b+52));
    checked.cursor[0]=get32(b+56);checked.cursor[1]=get32(b+60);checked.language=get32(b+64);
    if(!valid(&checked))return -1;
    *s=checked;
    return 0;
}
int scope_settings_defaults(unsigned c,ScopeSettings *s) {
    ScopeSettings checked;
    if(!s)return -1;
    memset(&checked,0,sizeof(checked));
    checked.timebase_ns=200000;checked.origin_conversion_hz=c;
    checked.trigger_level_mv=500;checked.flags=SCOPE_SETTINGS_RISING|SCOPE_SETTINGS_GRID;
    checked.scale_index[0]=2;checked.scale_index[1]=2;
    checked.cursor[0]=250;checked.cursor[1]=750;
    if(!valid(&checked))return -1;
    *s=checked;
    return 0;
}
