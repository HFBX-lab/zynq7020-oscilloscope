#include "scope_record.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

static unsigned checks,failures;
#define CHECK(x) do { ++checks; if(!(x)) { ++failures; \
    printf("FAIL line %d: %s\n",__LINE__,#x); } } while(0)
static uint32_t words[SCOPE_RECORD_WORDS],decoded[SCOPE_RECORD_WORDS];
static uint8_t blob[SCOPE_RECORD_BLOB_BYTES],saved[SCOPE_RECORD_BLOB_BYTES];

static void put32(uint8_t *p,uint32_t v) {
    for(unsigned i=0;i<4;++i)p[i]=(uint8_t)(v>>(8*i));
}
static uint32_t get32(const uint8_t *p) {
    return (uint32_t)p[0]|((uint32_t)p[1]<<8)|((uint32_t)p[2]<<16)|((uint32_t)p[3]<<24);
}
static uint32_t crc(const uint8_t *p,size_t n) {
    uint32_t c=UINT32_MAX;
    while(n--) {c^=*p++;for(unsigned i=0;i<8;++i)c=(c>>1)^((c&1u)?UINT32_C(0xedb88320):0);}
    return ~c;
}
static void reseal(void) {
    memset(blob+12,0,4);put32(blob+12,crc(blob,sizeof(blob)));
}
static ScopeRecordMeta fixture(unsigned mask) {
    ScopeRecordMeta m={0};
    m.count=4096;m.rate_hz=6250000;m.conversion_hz=50000000;m.filter_kind=3;
    m.timebase_ns=10000;m.record_id=12;m.capture_epoch=17;
    m.flags=SCOPE_RECORD_RISING|SCOPE_RECORD_DUAL|SCOPE_RECORD_GRID;
    m.trigger_channel=1;m.trigger_level_mv=500;
    m.scale_index[0]=2;m.scale_index[1]=3;m.cursor[0]=250;m.cursor[1]=750;
    if(mask) {
        m.calibration.flags=UINT32_C(0x100)|mask;m.calibration.id=UINT32_C(0x8b012345);
        if(mask&1u) {m.calibration.slope_v_per_code[0]=.009f;m.calibration.offset_v[0]=-4.5f;}
        if(mask&2u) {m.calibration.slope_v_per_code[1]=.011f;m.calibration.offset_v[1]=-5.7f;}
        CHECK(scope_calibration_trigger_q8(&m.calibration,m.trigger_channel,m.trigger_level_mv,&m.trigger_level_q8)==0);
    }
    for(unsigned i=0;i<4096;++i)words[i]=(i&1023u)|(((i*3u)&1023u)<<16);
    return m;
}
static void reject_decode(void) {
    ScopeRecordMeta m,before;memset(&m,0xa5,sizeof(m));before=m;
    memset(decoded,0x5a,sizeof(decoded));
    CHECK(scope_record_decode(blob,sizeof(blob),&m,decoded,4096)==-1);
    CHECK(memcmp(&m,&before,sizeof(m))==0);
    for(unsigned i=0;i<4096;++i)CHECK(decoded[i]==UINT32_C(0x5a5a5a5a));
}
static void reject_encode(const ScopeRecordMeta *m) {
    memset(blob,0xc7,sizeof(blob));size_t written=123;
    CHECK(scope_record_encode(m,words,blob,sizeof(blob),&written)==-1);
    CHECK(written==0);
    for(unsigned i=0;i<sizeof(blob);++i)CHECK(blob[i]==0xc7);
}
static void roundtrip(void) {
    for(unsigned mask=0;mask<=3;++mask) {
        ScopeRecordMeta m=fixture(mask),out;
        CHECK(scope_record_encode(&m,words,blob,sizeof(blob),NULL)==0);
        CHECK(blob[4]==(mask?2:1)&&get32(blob+92)==(mask?2u:1u));
        CHECK(scope_record_decode(blob,sizeof(blob),&out,decoded,4096)==0);
        CHECK(memcmp(&m,&out,sizeof(m))==0&&memcmp(words,decoded,sizeof(words))==0);
        if(mask) {
            uint32_t bits;memcpy(&bits,&m.calibration.slope_v_per_code[0],4);
            CHECK(get32(blob+108)==bits&&get32(blob+100)==m.trigger_level_q8);
            CHECK(get32(blob+104)==m.calibration.id&&get32(blob+124)==m.calibration.flags);
            m.calibration.id=0; /* a valid manifest CRC may equal zero */
            CHECK(scope_record_encode(&m,words,blob,sizeof(blob),NULL)==0);
            CHECK(scope_record_decode(blob,sizeof(blob),&out,decoded,4096)==0&&out.calibration.id==0);
        }else for(unsigned i=100;i<128;++i)CHECK(blob[i]==0);
    }
}
static void matched(void) {
    for(unsigned mask=1;mask<=3;++mask)for(unsigned channel=0;channel<2;++channel)
        for(unsigned rising=0;rising<2;++rising) {
            ScopeRecordMeta m=fixture(mask),out;m.trigger_channel=channel;
            CHECK(scope_calibration_trigger_q8(&m.calibration,channel,m.trigger_level_mv,&m.trigger_level_q8)==0);
            m.flags|=SCOPE_RECORD_TRIGGERED;
            if(!rising)m.flags&=~SCOPE_RECORD_RISING;
            unsigned low=m.trigger_level_q8/256u,high=low+1;
            unsigned a=rising?low:high,b=rising?high:low,shift=16*channel;
            words[999]=(words[999]&~(UINT32_C(0x7ff)<<shift))|(a<<shift);
            words[1000]=(words[1000]&~(UINT32_C(0x7ff)<<shift))|(b<<shift);
            m.trigger_after_index=1000;
            double cross=999+(m.trigger_level_q8/256.0-a)/((double)b-a);
            m.trigger_crossing_q16=(uint32_t)lround(cross*65536);
            m.view_start_q16=(uint32_t)lround((cross-125)*65536);
            CHECK(scope_record_encode(&m,words,blob,sizeof(blob),NULL)==0);
            CHECK(scope_record_decode(blob,sizeof(blob),&out,decoded,4096)==0);
            CHECK(memcmp(&m,&out,sizeof(m))==0);
            memcpy(saved,blob,sizeof(saved));
            m.trigger_level_q8++;reject_encode(&m);m.trigger_level_q8--;
            m.trigger_after_index=999;reject_encode(&m);m.trigger_after_index=1000;
            m.view_start_q16+=4;reject_encode(&m);m.view_start_q16-=4;
            words[1000]|=UINT32_C(0x400)<<shift;reject_encode(&m);
            memcpy(blob,saved,sizeof(blob));put32(blob+100,m.trigger_level_q8+1);reseal();reject_decode();
        }
}
static void bad_profiles(void) {
    static const struct {unsigned offset;uint32_t value;} bad[]={
        {4,3},{92,1},{100,0},{100,UINT32_C(0x40000)},
        {108,UINT32_C(0x7fc00000)},{108,UINT32_C(0x7f800000)},
        {108,0},{108,UINT32_C(0xbf800000)},{112,UINT32_C(0x7fc00000)},
        {116,UINT32_C(0x7f800000)},{120,UINT32_C(0xff800000)},
        {124,0},{124,3},{124,UINT32_C(0x100)},{124,UINT32_C(0x107)},
    };
    ScopeRecordMeta m=fixture(3);
    CHECK(scope_record_encode(&m,words,blob,sizeof(blob),NULL)==0);memcpy(saved,blob,sizeof(saved));
    for(unsigned i=0;i<sizeof(bad)/sizeof(bad[0]);++i) {
        memcpy(blob,saved,sizeof(blob));put32(blob+bad[i].offset,bad[i].value);reseal();reject_decode();
    }
    /* Inactive-channel coefficients may not silently affect old units. */
    m=fixture(1);m.calibration.slope_v_per_code[1]=.01f;reject_encode(&m);
    m=fixture(3);m.calibration.offset_v[0]=0;reject_encode(&m);
    m=fixture(3);m.calibration.slope_v_per_code[0]=NAN;reject_encode(&m);
    m=fixture(3);m.trigger_level_q8^=1;reject_encode(&m); /* unmatched also binds Q8 */
    m=fixture(0);m.trigger_level_q8=1;reject_encode(&m);
    m=fixture(0);m.calibration.id=1;reject_encode(&m);
    m=fixture(0);m.calibration.offset_v[1]=NAN;reject_encode(&m);
    memcpy(blob,saved,sizeof(blob));blob[100]^=1;reject_decode(); /* no reseal */
}
static void endpoints(void) {
    for(unsigned channel=0;channel<2;++channel)for(int mv=-5000;mv<=5000;mv+=10000) {
        ScopeRecordMeta m=fixture(3),out;m.trigger_channel=channel;m.trigger_level_mv=mv;
        CHECK(scope_calibration_trigger_q8(&m.calibration,channel,mv,&m.trigger_level_q8)==0);
        CHECK(scope_record_encode(&m,words,blob,sizeof(blob),NULL)==0);
        CHECK(scope_record_decode(blob,sizeof(blob),&out,decoded,4096)==0);
        CHECK(out.trigger_level_q8==m.trigger_level_q8);
    }
}
int main(int argc,char **argv) {
    roundtrip();matched();bad_profiles();endpoints();
    if(argc==3&&(strcmp(argv[1],"--verify-valid")==0||strcmp(argv[1],"--verify-invalid")==0||
                strcmp(argv[1],"--verify-legacy")==0)) {
        FILE *f=fopen(argv[2],"rb");CHECK(f!=NULL);
        if(f) {
            CHECK(fread(blob,1,sizeof(blob),f)==sizeof(blob));CHECK(fgetc(f)==EOF);CHECK(fclose(f)==0);
            if(strcmp(argv[1],"--verify-invalid")==0)reject_decode();
            else {
                ScopeRecordMeta out;
                CHECK(scope_record_decode(blob,sizeof(blob),&out,decoded,4096)==0);
                if(strcmp(argv[1],"--verify-legacy")==0)
                    CHECK(out.calibration.flags==0&&out.trigger_level_q8==0);
                else CHECK(out.calibration.flags!=0&&out.trigger_level_q8==get32(blob+100));
            }
        }
    }else if(argc>1) {
        ScopeRecordMeta m=fixture(3);
        CHECK(scope_record_encode(&m,words,blob,sizeof(blob),NULL)==0);
        FILE *f=fopen(argv[1],"wb");CHECK(f!=NULL);
        if(f) {CHECK(fwrite(blob,1,sizeof(blob),f)==sizeof(blob));CHECK(fclose(f)==0);}
    }
    printf("record calibration codec: %u checks, %u failures\n",checks,failures);
    return failures?1:0;
}
