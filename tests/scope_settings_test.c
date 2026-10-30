#include "scope_settings.h"
#include "scope_record.h"
#include <stdio.h>
#include <string.h>

static unsigned checks,failures;
#define CHECK(x) do { ++checks; if(!(x)) { ++failures; \
    printf("FAIL line %d: %s\n",__LINE__,#x); } } while(0)

/* Independently constructed LE fixture, including fixed CRC 0x0b26c66f.
 * Exercises negative signed fields and the shortest 50M physical timebase. */
static const uint8_t golden[96]={
    0x53,0x43,0x50,0x31,0x01,0x00,0x60,0x00,0x60,0x00,0x00,0x00,0x6f,0xc6,0x26,0x0b,
    0x64,0x00,0x00,0x00,0x80,0xf0,0xfa,0x02,0x01,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x78,0xec,0xff,0xff,0x1f,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x06,0x00,0x00,0x00,
    0x54,0xf2,0xff,0xff,0xac,0x0d,0x00,0x00,0x00,0x00,0x00,0x00,0xe8,0x03,0x00,0x00,
    0x01,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00
};
static ScopeSettings fixture(void) {
    ScopeSettings s={0};
    s.timebase_ns=100;s.origin_conversion_hz=50000000;s.selected_channel=1;
    s.trigger_level_mv=-5000;s.flags=31;s.scale_index[1]=6;
    s.position_mdiv[0]=-3500;s.position_mdiv[1]=3500;
    s.cursor[1]=1000;s.language=1;
    return s;
}
/* A separate MSB-first CRC formulation, anchored to the IEEE check vector. */
static uint32_t independent_crc(const uint8_t *p,size_t n) {
    uint32_t crc=UINT32_MAX,reflected=0;
    while(n--) {
        unsigned byte=*p++,reverse=0;
        for(unsigned j=0;j<8;++j) { reverse=(reverse<<1)|(byte&1u);byte>>=1; }
        crc^=(uint32_t)reverse<<24;
        for(unsigned j=0;j<8;++j)
            crc=(crc<<1)^((crc&0x80000000u)?0x04c11db7u:0u);
    }
    for(unsigned j=0;j<32;++j) { reflected=(reflected<<1)|(crc&1u);crc>>=1; }
    return ~reflected;
}
static void le(uint8_t *p,uint32_t x) {
    p[0]=(uint8_t)x;p[1]=(uint8_t)(x>>8);p[2]=(uint8_t)(x>>16);p[3]=(uint8_t)(x>>24);
}
static void repair_crc(uint8_t *b) {
    memset(b+12,0,4);le(b+12,independent_crc(b,96));
}
static void rejected_blob(const uint8_t *b,size_t n) {
    ScopeSettings out,before;
    memset(&out,0x5a,sizeof(out));before=out;
    CHECK(scope_settings_decode(b,n,&out)==-1);
    CHECK(memcmp(&out,&before,sizeof(out))==0);
}
static void rejected_settings(const ScopeSettings *s) {
    uint8_t b[96],before[96];size_t written=999;
    memset(b,0xa5,sizeof(b));memcpy(before,b,sizeof(b));
    CHECK(scope_settings_encode(s,b,sizeof(b),&written)==-1);
    CHECK(written==0);CHECK(memcmp(b,before,sizeof(b))==0);
}
static void golden_test(void) {
    ScopeSettings s=fixture(),out;uint8_t b[100];size_t written=99;
    memset(b,0xa5,sizeof(b));memset(&out,0,sizeof(out));
    CHECK(independent_crc((const uint8_t *)"123456789",9)==0xcbf43926u);
    CHECK(scope_record_crc32("123456789",9)==0xcbf43926u);
    CHECK(scope_settings_encode(&s,b,sizeof(b),&written)==0);
    CHECK(written==96);CHECK(memcmp(b,golden,96)==0);
    CHECK(b[96]==0xa5&&b[99]==0xa5);
    CHECK(scope_settings_decode(golden,96,&out)==0);
    CHECK(memcmp(&s,&out,sizeof(s))==0);
    CHECK(scope_settings_encode(&s,b,96,NULL)==0);
}
static void defaults_profiles(void) {
    static const unsigned profiles[]={5000000,10000000,25000000,50000000};
    static const uint32_t times[]={100,200,500,1000,2000,5000,10000,20000,
        50000,100000,200000,500000,1000000,2000000,5000000,
        10000000,20000000,50000000,100000000,200000000,500000000};
    for(unsigned c=0;c<4;++c) {
        ScopeSettings s,out;uint8_t b[96];size_t written;
        memset(&s,0xa5,sizeof(s));
        CHECK(scope_settings_defaults(profiles[c],&s)==0);
        CHECK(s.origin_conversion_hz==profiles[c]&&s.timebase_ns==200000);
        CHECK(s.selected_channel==0&&s.trigger_channel==0&&s.trigger_level_mv==500);
        CHECK(s.flags==(SCOPE_SETTINGS_RISING|SCOPE_SETTINGS_GRID));
        CHECK(s.scale_index[0]==2&&s.scale_index[1]==2);
        CHECK(s.position_mdiv[0]==0&&s.position_mdiv[1]==0);
        CHECK(s.cursor[0]==250&&s.cursor[1]==750&&s.language==0);
        for(unsigned t=0;t<21;++t) {
            s.timebase_ns=times[t];
            /* Profile limits from the accepted physical labels, independent
             * of acquisition-plan index implementation. */
            int expected=(c==0?t>=8:c==3?1:t>=3)?0:-1;
            memset(b,0x5a,sizeof(b));
            CHECK(scope_settings_encode(&s,b,96,&written)==expected);
            if(expected==0) {
                CHECK(written==96);CHECK(scope_settings_decode(b,96,&out)==0);
                CHECK(memcmp(&s,&out,sizeof(s))==0);
            } else { CHECK(written==0);CHECK(b[0]==0x5a&&b[95]==0x5a); }
            /* Recompute CRC on source-profile mutations as decode must
             * reject unsupported labels independently from integrity. */
            memcpy(b,golden,96);le(b+16,times[t]);le(b+20,profiles[c]);repair_crc(b);
            if(expected)rejected_blob(b,96);
            else CHECK(scope_settings_decode(b,96,&out)==0);
        }
    }
    ScopeSettings s,before;memset(&s,0x77,sizeof(s));before=s;
    CHECK(scope_settings_defaults(12500000,&s)==-1);
    CHECK(memcmp(&s,&before,sizeof(s))==0);
    CHECK(scope_settings_defaults(0,&s)==-1);
    CHECK(scope_settings_defaults(5000000,NULL)==-1);
}
static void invalid_fields(void) {
    static const struct { unsigned offset;uint32_t value; } bad[]={
        {0,0},{4,2},{4,0x00010001u},{6,95},{6,0x00010060u},{8,95},{8,97},
        {16,0},{16,3000},{20,0},{20,12500000},{24,2},{24,UINT32_MAX},
        {28,2},{32,5001},{32,(uint32_t)-5001},{32,0x80000000u},
        {36,32},{36,UINT32_MAX},{40,7},{44,7},
        {48,3501},{48,(uint32_t)-3501},{52,3501},{52,(uint32_t)-3501},
        {56,1001},{60,1001},{64,2},{64,UINT32_MAX}
    };
    uint8_t b[96];
    for(unsigned i=0;i<sizeof(bad)/sizeof(bad[0]);++i) {
        memcpy(b,golden,96);le(b+bad[i].offset,bad[i].value);repair_crc(b);
        rejected_blob(b,96);
    }
    for(unsigned i=68;i<96;++i) {
        memcpy(b,golden,96);b[i]=1;repair_crc(b);rejected_blob(b,96);
    }
    for(unsigned i=0;i<96;++i) {
        memcpy(b,golden,96);b[i]^=1;rejected_blob(b,96);
    }
    for(size_t n=0;n<96;++n)rejected_blob(golden,n);
    rejected_blob(golden,97);rejected_blob(golden,SIZE_MAX);rejected_blob(NULL,96);
    CHECK(scope_settings_decode(golden,96,NULL)==-1);
    ScopeSettings s;
#define BAD(member,value) do { s=fixture();s.member=(value);rejected_settings(&s); } while(0)
    BAD(timebase_ns,3000);BAD(origin_conversion_hz,12500000);
    BAD(selected_channel,2);BAD(trigger_channel,2);BAD(trigger_level_mv,-5001);
    BAD(trigger_level_mv,5001);BAD(flags,32);BAD(scale_index[0],7);BAD(scale_index[1],7);
    BAD(position_mdiv[0],-3501);BAD(position_mdiv[1],3501);
    BAD(cursor[0],1001);BAD(cursor[1],1001);BAD(language,2);
#undef BAD
    rejected_settings(NULL);
    s=fixture();size_t written=9;memset(b,0x33,96);
    CHECK(scope_settings_encode(&s,b,95,&written)==-1&&written==0&&b[0]==0x33);
    CHECK(scope_settings_encode(&s,NULL,96,&written)==-1&&written==0);
    /* Both valid signed endpoints and unselected boolean combinations. */
    s.trigger_level_mv=5000;s.flags=0;s.language=0;
    s.position_mdiv[0]=3500;s.position_mdiv[1]=-3500;
    CHECK(scope_settings_encode(&s,b,96,&written)==0);
    ScopeSettings out;CHECK(scope_settings_decode(b,96,&out)==0);
    CHECK(memcmp(&s,&out,sizeof(s))==0);
}
static void overlap_test(void) {
    union { max_align_t align; uint8_t b[256]; } u;
    uint8_t before[256];size_t written=99;
    ScopeSettings s=fixture();
    memset(u.b,0x55,sizeof(u.b));memcpy(u.b,&s,sizeof(s));memcpy(before,u.b,sizeof(before));
    CHECK(scope_settings_encode((const ScopeSettings *)(const void *)u.b,u.b+4,96,&written)==-1);
    CHECK(written==0&&memcmp(u.b,before,sizeof(before))==0);
    CHECK(scope_settings_encode((const ScopeSettings *)(const void *)(u.b+8),u.b,96,&written)==-1);
    CHECK(written==0&&memcmp(u.b,before,sizeof(before))==0);
    memcpy(u.b,golden,96);memcpy(before,u.b,sizeof(before));
    CHECK(scope_settings_decode(u.b,96,(ScopeSettings *)(void *)(u.b+4))==-1);
    CHECK(memcmp(u.b,before,sizeof(before))==0);
    CHECK(scope_settings_decode(u.b+4,96,(ScopeSettings *)(void *)u.b)==-1);
    CHECK(memcmp(u.b,before,sizeof(before))==0);
    /* written is an output too: reject aliasing either input or blob,
     * while honoring the explicitly documented written=0 exception. */
    memset(u.b,0x55,sizeof(u.b));memcpy(u.b,&s,sizeof(s));
    CHECK(scope_settings_encode((const ScopeSettings *)(const void *)u.b,u.b+128,96,
                                (size_t *)(void *)u.b)==-1);
    CHECK(*(size_t *)(void *)u.b==0&&u.b[128]==0x55);
    memset(u.b,0x55,sizeof(u.b));
    CHECK(scope_settings_encode(&s,u.b,96,(size_t *)(void *)(u.b+16))==-1);
    CHECK(*(size_t *)(void *)(u.b+16)==0&&u.b[0]==0x55&&u.b[95]==0x55);
    /* Adjacent but nonoverlapping ranges remain usable. */
    memcpy(u.b,&s,sizeof(s));
    CHECK(scope_settings_encode((const ScopeSettings *)(const void *)u.b,u.b+sizeof(s),96,&written)==0);
    CHECK(scope_settings_decode(u.b+sizeof(s),96,(ScopeSettings *)(void *)u.b)==0);
    CHECK(memcmp(u.b,&s,sizeof(s))==0);
}
int main(void) {
    golden_test();defaults_profiles();invalid_fields();overlap_test();
    printf("scope_settings: %u checks, %u failures\n",checks,failures);
    return failures?1:0;
}
