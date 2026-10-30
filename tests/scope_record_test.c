#include "scope_record.h"
#include "scope_acquisition.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

static unsigned checks, failures;
#define CHECK(x) do { ++checks; if (!(x)) { ++failures; \
    printf("FAIL line %d: %s\n", __LINE__, #x); } } while (0)
static uint32_t raw[SCOPE_RECORD_WORDS], decoded[SCOPE_RECORD_WORDS];
static uint8_t blob[SCOPE_RECORD_BLOB_BYTES], good[SCOPE_RECORD_BLOB_BYTES];
static uint8_t golden[SCOPE_RECORD_BLOB_BYTES], sentinel[SCOPE_RECORD_BLOB_BYTES];
static ScopeRecordMeta base;

/* Test-side implementation uses the conventional byte/polynomial algorithm,
 * not the codec's CRC helper. Also anchors to a published standard check. */
static uint32_t independent_crc(const uint8_t *p,size_t n) {
    uint32_t crc=0xffffffffu;
    while(n--) { crc^=*p++; for(unsigned j=0;j<8;++j)
        crc=(crc>>1)^((crc&1u)?0xedb88320u:0u); }
    return ~crc;
}
static void le(uint8_t *p,uint32_t value) {
    p[0]=(uint8_t)value;p[1]=(uint8_t)(value>>8);
    p[2]=(uint8_t)(value>>16);p[3]=(uint8_t)(value>>24);
}
static void repair_crc(void) {
    memset(blob+12,0,4);le(blob+12,independent_crc(blob,sizeof(blob)));
}
static void fixture(void) {
    memset(&base,0,sizeof(base));
    base.count=4096;base.rate_hz=6250000;base.conversion_hz=50000000;
    base.filter_kind=3;base.record_id=0x12345678u;base.capture_epoch=0x90abcdefu;
    base.timebase_ns=10000;base.flags=SCOPE_RECORD_RISING|SCOPE_RECORD_DUAL|SCOPE_RECORD_GRID;
    base.trigger_channel=1;base.selected_channel=0;base.trigger_level_mv=500;
    base.scale_index[0]=1;base.scale_index[1]=6;
    base.position_mdiv[0]=-3500;base.position_mdiv[1]=3500;
    base.cursor[0]=0;base.cursor[1]=1000;
    for(unsigned i=0;i<4096;++i) raw[i]=(i&1023u)|(((i*3u)&1023u)<<16);
    raw[0]=0x04000400u;raw[4095]=0x07ff07ffu;
}
static void reject_decode(void) {
    ScopeRecordMeta out, before;
    memset(&out,0xa5,sizeof(out));before=out;
    memset(decoded,0x5a,sizeof(decoded));
    CHECK(scope_record_decode(blob,sizeof(blob),&out,decoded,4096)==-1);
    CHECK(memcmp(&out,&before,sizeof(out))==0);
    for(unsigned i=0;i<4096;++i) CHECK(decoded[i]==0x5a5a5a5au);
}
static void reject_encode(ScopeRecordMeta *meta) {
    size_t written=999;
    memset(blob,0xc7,sizeof(blob));
    CHECK(scope_record_encode(meta,raw,blob,sizeof(blob),&written)==-1);
    CHECK(written==0);
    for(unsigned i=0;i<sizeof(blob);++i) CHECK(blob[i]==0xc7);
}
static void golden_bytes(void) {
    size_t written=0;
    CHECK(scope_record_crc32("123456789",9)==0xcbf43926u);
    CHECK(scope_record_crc32("",0)==0);
    CHECK(scope_record_encode(&base,raw,blob,sizeof(blob),&written)==0);
    CHECK(written==sizeof(blob));
    memset(golden,0,sizeof(golden));
    memcpy(golden,"SCW1",4);golden[4]=1;golden[6]=128;
    le(golden+8,16512);le(golden+16,4096);le(golden+20,6250000);
    le(golden+24,50000000);le(golden+28,3);le(golden+32,0x12345678u);
    le(golden+36,0x90abcdefu);le(golden+40,10000);le(golden+52,42);
    le(golden+56,1);le(golden+60,500);le(golden+68,1);le(golden+72,6);
    le(golden+76,0xfffff254u);le(golden+80,3500);le(golden+88,1000);
    le(golden+92,1);
    for(unsigned i=0;i<4096;++i) le(golden+128+i*4,raw[i]);
    le(golden+12,independent_crc(golden,sizeof(golden)));
    CHECK(memcmp(blob,golden,sizeof(blob))==0);
    memcpy(good,golden,sizeof(good));
    ScopeRecordMeta out;
    memset(&out,0,sizeof(out));
    CHECK(scope_record_decode(golden,sizeof(golden),&out,decoded,4096)==0);
    CHECK(memcmp(raw,decoded,sizeof(raw))==0);
    CHECK(memcmp(&base,&out,sizeof(base))==0);
}
static void invalid_fields(void) {
    static const struct { unsigned offset;uint32_t value; } bad[]={
        {0,0},{4,0x00800002u},{6,127},{8,16511},{16,4095},
        {20,12345},{24,12500000},{28,0},{40,3000},{40,0},
        {40,500000000},{44,4095u*65536u},{48,4095u*65536u+1},
        {52,64},{56,2},{60,5001},{60,0xffffec77u},{64,2},
        {68,7},{72,7},{76,3501},{80,0xfffff253u},{84,1001},
        {88,1001},{92,0},{92,2},{96,1},{100,1},{124,0x80000000u}
    };
    for(unsigned i=0;i<sizeof(bad)/sizeof(bad[0]);++i) {
        memcpy(blob,good,sizeof(blob));le(blob+bad[i].offset,bad[i].value);
        repair_crc();reject_decode();
    }
    /* Every byte in reserved and the final sample, including late corruption. */
    for(unsigned i=100;i<128;++i) {
        memcpy(blob,good,sizeof(blob));blob[i]=1;repair_crc();reject_decode();
    }
    for(unsigned i=0;i<sizeof(blob);++i) {
        memcpy(blob,good,sizeof(blob));blob[i]^=1;
        ScopeRecordMeta out;
        CHECK(scope_record_decode(blob,sizeof(blob),&out,decoded,4096)==-1);
    }
    memcpy(blob,good,sizeof(blob));le(blob+128+4095*4,0x08000000u);
    repair_crc();reject_decode();
    ScopeRecordMeta meta=base;meta.count=0;reject_encode(&meta);
    meta=base;meta.flags=0x80000000u;reject_encode(&meta);
    meta=base;meta.timebase_ns=100;meta.rate_hz=625;reject_encode(&meta); /* <1 sample */
    raw[4095]|=0x80000000u;reject_encode(&base);fixture();
}
static void capacity_and_overlap(void) {
    size_t written=99;
    ScopeRecordMeta out, before;
    memset(&out,0xa5,sizeof(out));before=out;
    memcpy(blob,good,sizeof(blob));memcpy(sentinel,blob,sizeof(blob));
    CHECK(scope_record_encode(&base,raw,blob,sizeof(blob)-1,&written)==-1&&written==0);
    CHECK(memcmp(blob,sentinel,sizeof(blob))==0);
    CHECK(scope_record_encode(NULL,raw,blob,sizeof(blob),&written)==-1&&written==0);
    CHECK(scope_record_encode(&base,NULL,blob,sizeof(blob),&written)==-1&&written==0);
    CHECK(scope_record_encode(&base,raw,NULL,sizeof(blob),&written)==-1&&written==0);
    CHECK(scope_record_decode(blob,sizeof(blob)-1,&out,decoded,4096)==-1);
    CHECK(scope_record_decode(blob,sizeof(blob)+1,&out,decoded,4096)==-1);
    CHECK(scope_record_decode(blob,sizeof(blob),&out,decoded,4095)==-1);
    CHECK(memcmp(&out,&before,sizeof(out))==0);
    CHECK(scope_record_decode(NULL,sizeof(blob),&out,decoded,4096)==-1);
    CHECK(scope_record_decode(blob,sizeof(blob),NULL,decoded,4096)==-1);
    CHECK(scope_record_decode(blob,sizeof(blob),&out,NULL,4096)==-1);
    CHECK(scope_record_encode(&base,raw,(uint8_t *)raw,sizeof(blob),&written)==-1&&written==0);
    CHECK(scope_record_encode((ScopeRecordMeta *)blob,raw,blob,sizeof(blob),&written)==-1);
    CHECK(scope_record_decode(blob,sizeof(blob),(ScopeRecordMeta *)blob,decoded,4096)==-1);
    CHECK(scope_record_decode(blob,sizeof(blob),&out,(uint32_t *)(void *)(blob+128),4096)==-1);
    CHECK(scope_record_decode(blob,sizeof(blob),&out,(uint32_t *)(void *)&out,4096)==-1);
    CHECK(memcmp(blob,sentinel,sizeof(blob))==0);
    CHECK(memcmp(&out,&before,sizeof(out))==0);
}
static void signed_extremes_and_nontrigger_otr(void) {
    ScopeRecordMeta m=base,out;
    m.record_id=UINT32_MAX;m.capture_epoch=UINT32_MAX;
    m.trigger_level_mv=-5000;m.position_mdiv[0]=3500;m.position_mdiv[1]=-3500;
    m.flags=63u&~SCOPE_RECORD_TRIGGERED;
    m.trigger_crossing_q16=4095u*65536u;
    for(unsigned i=0;i<4096;++i)raw[i]=0x07ff07ffu;
    CHECK(scope_record_encode(&m,raw,blob,sizeof(blob),NULL)==0);
    CHECK(scope_record_decode(blob,sizeof(blob),&out,decoded,4096)==0);
    CHECK(memcmp(&out,&m,sizeof(m))==0&&memcmp(raw,decoded,sizeof(raw))==0);
    m.trigger_level_mv=5000;
    CHECK(scope_record_encode(&m,raw,blob,sizeof(blob),NULL)==0);
    CHECK(scope_record_decode(blob,sizeof(blob),&out,decoded,4096)==0);
    CHECK(out.trigger_level_mv==5000);
    fixture();
}
static void rates_and_windows(void) {
    static const uint32_t times[]={100,200,500,1000,2000,5000,10000,
        20000,50000,100000,200000,500000,1000000,2000000,5000000,
        10000000,20000000,50000000,100000000,200000000,500000000};
    static const unsigned conv[]={5000000,10000000,25000000,50000000};
    for(unsigned c=0;c<4;++c) for(unsigned i=0;i<21;++i) {
        ScopeCapturePlan p;
        if(scope_acquisition_plan_for(i,conv[c],&p)!=0) continue;
        for(unsigned t=0;t<21;++t) {
            ScopeRecordMeta m=base,out;
            size_t written=99;
            m.rate_hz=p.rate_hz;m.conversion_hz=conv[c];m.filter_kind=p.filter_kind;
            m.timebase_ns=times[t];
            double span=times[t]*1e-8*p.rate_hz;
            int valid=span>=1&&span<=4095;
            int result=scope_record_encode(&m,raw,blob,sizeof(blob),&written);
            CHECK(result==(valid?0:-1));
            if(valid) {
                CHECK(scope_record_decode(blob,sizeof(blob),&out,decoded,4096)==0);
                CHECK(memcmp(raw,decoded,sizeof(raw))==0);
                CHECK(memcmp(&out,&m,sizeof(m))==0);
                m.view_start_q16=(uint32_t)llround((4095-span)*65536.0);
                CHECK(scope_record_encode(&m,raw,blob,sizeof(blob),&written)==0);
                m.view_start_q16+=4;
                CHECK(scope_record_encode(&m,raw,blob,sizeof(blob),&written)==-1);
            } else CHECK(written==0);
        }
    }
    /* Valid frozen 50M raw record shown at 100ns despite plan selecting RAW
       at another time. Every supported original rate is independent of time. */
    ScopeRecordMeta m=base;
    m.rate_hz=50000000;m.filter_kind=0;m.timebase_ns=100;
    CHECK(scope_record_encode(&m,raw,blob,sizeof(blob),NULL)==0);
    m.filter_kind=3;reject_encode(&m);
    m=base;m.rate_hz=6250000;m.conversion_hz=10000000;reject_encode(&m);
    m=base;m.rate_hz=50000000;m.conversion_hz=5000000;m.filter_kind=0;reject_encode(&m);
}
static void triggered(void) {
    for(unsigned channel=0;channel<2;++channel) for(unsigned rising=0;rising<2;++rising) {
        ScopeRecordMeta m=base,out;
        m.flags|=SCOPE_RECORD_TRIGGERED;
        if(!rising)m.flags&=~SCOPE_RECORD_RISING;
        m.trigger_channel=channel;m.trigger_after_index=1000;
        unsigned shift=16*channel;
        uint32_t mask=0x7ffu<<shift;
        raw[999]=(raw[999]&~mask)|((rising?500u:650u)<<shift);
        raw[1000]=(raw[1000]&~mask)|((rising?650u:500u)<<shift);
        /* Q8 hardware level: round(5.5*102.3*256) = 144038. */
        double q8_level=144038.0/256.0;
        double cross=999+(q8_level-(rising?500:650))/(rising?150:-150);
        m.trigger_crossing_q16=(uint32_t)llround(cross*65536.0);
        m.view_start_q16=(uint32_t)llround((cross-125)*65536.0);
        CHECK(scope_record_encode(&m,raw,blob,sizeof(blob),NULL)==0);
        CHECK(scope_record_decode(blob,sizeof(blob),&out,decoded,4096)==0);
        CHECK(memcmp(raw,decoded,sizeof(raw))==0);
        memcpy(good,blob,sizeof(good));
        m.trigger_after_index=999;reject_encode(&m);
        m.trigger_after_index=1000;m.view_start_q16+=4;reject_encode(&m);
        m.view_start_q16-=4;m.trigger_crossing_q16+=4;reject_encode(&m);
        m.trigger_crossing_q16-=4;
        raw[1000]|=0x400u<<shift;reject_encode(&m);raw[1000]&=~(0x400u<<shift);
        memcpy(blob,good,sizeof(blob));le(blob+96,999);repair_crc();reject_decode();
        memcpy(blob,good,sizeof(blob));le(blob+48,m.trigger_crossing_q16+4);repair_crc();reject_decode();
        fixture();
    }
    /* Exact integer crossing belongs to the named after pair even though
       ceil(cross_q16) names a different pair. Non-selected OTR is legal. */
    ScopeRecordMeta m=base;
    m.flags|=SCOPE_RECORD_TRIGGERED;m.trigger_channel=0;
    m.trigger_level_mv=-5000;
    raw[999]=0x04000000u;raw[1000]=0x040003ffu;
    m.trigger_after_index=1000;
    m.trigger_crossing_q16=999u*65536u;m.view_start_q16=874u*65536u;
    CHECK(scope_record_encode(&m,raw,blob,sizeof(blob),NULL)==0);
    m.trigger_after_index=999;reject_encode(&m);
    /* Find a true Q8 crossing less than half a Q16 step after an integer.
       This catches reconstructing the edge index from rounded crossing. */
    int found_near_integer=0;
    for(int mv=-4999;mv<5000;++mv) {
        unsigned q8=(unsigned)lround((mv+5000)*1023.0*256.0/10000.0);
        unsigned before=q8/256u;
        double offset=(q8/256.0-before)/(1023-before);
        if(offset>0&&offset<.5/65536) {
            m.trigger_level_mv=mv;m.trigger_after_index=1000;
            raw[999]=0x04000000u|before;raw[1000]=0x040003ffu;
            m.trigger_crossing_q16=(uint32_t)llround((999+offset)*65536);
            m.view_start_q16=(uint32_t)llround((874+offset)*65536);
            CHECK(m.trigger_crossing_q16==999u*65536u);
            CHECK(scope_record_encode(&m,raw,blob,sizeof(blob),NULL)==0);
            found_near_integer=1;
            break;
        }
    }
    CHECK(found_near_integer);
    fixture();
}
int main(int argc,char **argv) {
    fixture();golden_bytes();invalid_fields();capacity_and_overlap();
    signed_extremes_and_nontrigger_otr();rates_and_windows();fixture();triggered();
    if(argc>1) {
        FILE *f=fopen(argv[1],"wb");
        CHECK(f!=NULL);
        if(f) { CHECK(fwrite(golden,1,sizeof(golden),f)==sizeof(golden));CHECK(fclose(f)==0); }
    }
    printf("record codec: %u checks, %u failures\n",checks,failures);
    return failures?1:0;
}
