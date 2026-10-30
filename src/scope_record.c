#include "scope_record.h"
#include "scope_acquisition.h"

#include <math.h>
#include <float.h>
#include <string.h>

_Static_assert(sizeof(float)==4&&FLT_RADIX==2&&FLT_MANT_DIG==24&&FLT_MAX_EXP==128,
               "SCW1 v2 requires IEEE754 binary32 float");

#define VALID_FLAGS 63u
#define RESERVED_WORD_BITS UINT32_C(0xf800f800)
#define Q16_TOLERANCE (2.0/65536.0)

static uint32_t get32(const uint8_t *p) {
    return (uint32_t)p[0]|((uint32_t)p[1]<<8)|
           ((uint32_t)p[2]<<16)|((uint32_t)p[3]<<24);
}
static void put32(uint8_t *p,uint32_t x) {
    p[0]=(uint8_t)x;p[1]=(uint8_t)(x>>8);
    p[2]=(uint8_t)(x>>16);p[3]=(uint8_t)(x>>24);
}
static void put_float(uint8_t *p,float value) {
    uint32_t bits;memcpy(&bits,&value,sizeof(bits));put32(p,bits);
}
static float get_float(const uint8_t *p) {
    uint32_t bits=get32(p);float value;memcpy(&value,&bits,sizeof(value));return value;
}
static int32_t signed32(uint32_t x) {
    return x<=INT32_MAX?(int32_t)x:-1-(int32_t)(UINT32_MAX-x);
}
static int overlap(const void *a,size_t an,const void *b,size_t bn) {
    uintptr_t av=(uintptr_t)a,bv=(uintptr_t)b;
    return av<=bv?bv-av<an:av-bv<bn;
}
static uint32_t crc_byte(uint32_t crc,uint8_t byte) {
    crc^=byte;
    for(unsigned bit=0;bit<8;++bit)
        crc=(crc>>1)^((crc&1u)?UINT32_C(0xedb88320):0u);
    return crc;
}
uint32_t scope_record_crc32(const void *data,size_t length) {
    const uint8_t *bytes=data;
    uint32_t crc=UINT32_MAX;
    if(!data&&length)return 0;
    for(size_t i=0;i<length;++i)crc=crc_byte(crc,bytes[i]);
    return ~crc;
}
static uint32_t blob_crc(const uint8_t *blob) {
    uint32_t crc=UINT32_MAX;
    for(size_t i=0;i<SCOPE_RECORD_BLOB_BYTES;++i)
        crc=crc_byte(crc,i>=12&&i<16?0:blob[i]);
    return ~crc;
}
/* Decode validates words in their LE representation without a second 16 KiB
 * temporary array; encode validates native words through the same reader. */
static uint32_t read_word(const uint32_t *native,const uint8_t *little,unsigned i) {
    return native?native[i]:get32(little+4u*i);
}
static int valid_timebase(uint32_t ns) {
    static const uint32_t values[]={100,200,500,1000,2000,5000,10000,
        20000,50000,100000,200000,500000,1000000,2000000,5000000,
        10000000,20000000,50000000,100000000,200000000,500000000};
    for(unsigned i=0;i<sizeof(values)/sizeof(values[0]);++i)
        if(values[i]==ns)return 1;
    return 0;
}
static int valid_rate(const ScopeRecordMeta *m) {
    ScopeCapturePlan plan;
    /* A stopped record can be viewed at a different timebase. Match the
     * original hardware profile somewhere, never to the current view label. */
    for(unsigned i=0;i<SCOPE_50M_TIMEBASE_COUNT;++i)
        if(scope_acquisition_plan_for(i,m->conversion_hz,&plan)==0&&
           plan.rate_hz==m->rate_hz&&plan.filter_kind==m->filter_kind)return 1;
    return 0;
}
static int validate(const ScopeRecordMeta *m,const uint32_t *native,const uint8_t *little) {
    if(m->count!=SCOPE_RECORD_WORDS||!valid_rate(m)||!valid_timebase(m->timebase_ns)||
       (m->flags&~VALID_FLAGS)||m->trigger_channel>1||m->selected_channel>1||
       m->trigger_level_mv < -5000||m->trigger_level_mv>5000||
       m->scale_index[0]>6||m->scale_index[1]>6||
       m->position_mdiv[0] < -3500||m->position_mdiv[0]>3500||
       m->position_mdiv[1] < -3500||m->position_mdiv[1]>3500||
       m->cursor[0]>1000||m->cursor[1]>1000||
       m->trigger_crossing_q16>4095u*65536u||
       !scope_calibration_valid(&m->calibration))return 0;
    uint32_t level_q8;
    if(scope_calibration_trigger_q8(&m->calibration,m->trigger_channel,
                                  m->trigger_level_mv,&level_q8))return 0;
    if(m->calibration.flags?m->trigger_level_q8!=level_q8:m->trigger_level_q8!=0)return 0;
    double span=(double)m->timebase_ns*(double)m->rate_hz/100000000.0;
    double start=m->view_start_q16/65536.0;
    if(span<1.0||span>4095.0||start+span>4095.0+Q16_TOLERANCE)return 0;
    for(unsigned i=0;i<SCOPE_RECORD_WORDS;++i)
        if(read_word(native,little,i)&RESERVED_WORD_BITS)return 0;
    if(!(m->flags&SCOPE_RECORD_TRIGGERED))return m->trigger_after_index==0;
    if(m->trigger_after_index==0||m->trigger_after_index>=SCOPE_RECORD_WORDS)return 0;
    unsigned shift=16u*m->trigger_channel;
    uint32_t before=(read_word(native,little,m->trigger_after_index-1)>>shift)&0x7ffu;
    uint32_t after=(read_word(native,little,m->trigger_after_index)>>shift)&0x7ffu;
    if((before|after)&0x400u)return 0;
    /* The profile and physical level bind the actual hardware Q8 threshold.
     * Nominal v1 still uses the historical integer quantizer exactly. */
    double level=level_q8/256.0;
    int rising=(m->flags&SCOPE_RECORD_RISING)!=0;
    if(rising?!(before<=level&&after>level):!(before>=level&&after<level))return 0;
    double crossing=m->trigger_after_index-1+(level-before)/((double)after-before);
    double expected_start=crossing-.2*span;
    return expected_start>=-Q16_TOLERANCE&&
        expected_start+span<=4095.0+Q16_TOLERANCE&&
        fabs(m->trigger_crossing_q16/65536.0-crossing)<=Q16_TOLERANCE&&
        fabs(start-expected_start)<=Q16_TOLERANCE;
}
static void write_header(uint8_t *b,const ScopeRecordMeta *m) {
    memset(b,0,SCOPE_RECORD_HEADER_BYTES);
    memcpy(b,"SCW1",4);b[4]=m->calibration.flags?2:1;b[6]=128;
    put32(b+8,SCOPE_RECORD_BLOB_BYTES);put32(b+16,m->count);
    put32(b+20,m->rate_hz);put32(b+24,m->conversion_hz);
    put32(b+28,m->filter_kind);put32(b+32,m->record_id);
    put32(b+36,m->capture_epoch);put32(b+40,m->timebase_ns);
    put32(b+44,m->view_start_q16);put32(b+48,m->trigger_crossing_q16);
    put32(b+52,m->flags);put32(b+56,m->trigger_channel);
    put32(b+60,(uint32_t)m->trigger_level_mv);put32(b+64,m->selected_channel);
    put32(b+68,m->scale_index[0]);put32(b+72,m->scale_index[1]);
    put32(b+76,(uint32_t)m->position_mdiv[0]);put32(b+80,(uint32_t)m->position_mdiv[1]);
    put32(b+84,m->cursor[0]);put32(b+88,m->cursor[1]);
    put32(b+92,m->calibration.flags?2:1);put32(b+96,m->trigger_after_index);
    if(m->calibration.flags) {
        put32(b+100,m->trigger_level_q8);put32(b+104,m->calibration.id);
        for(unsigned ch=0;ch<2;++ch) {
            put_float(b+108+8u*ch,m->calibration.slope_v_per_code[ch]);
            put_float(b+112+8u*ch,m->calibration.offset_v[ch]);
        }
        put32(b+124,m->calibration.flags);
    }
}
static void read_header(const uint8_t *b,ScopeRecordMeta *m) {
    memset(m,0,sizeof(*m));
    m->count=get32(b+16);m->rate_hz=get32(b+20);m->conversion_hz=get32(b+24);
    m->filter_kind=get32(b+28);m->record_id=get32(b+32);m->capture_epoch=get32(b+36);
    m->timebase_ns=get32(b+40);m->view_start_q16=get32(b+44);
    m->trigger_crossing_q16=get32(b+48);m->flags=get32(b+52);
    m->trigger_channel=get32(b+56);m->trigger_level_mv=signed32(get32(b+60));
    m->selected_channel=get32(b+64);m->scale_index[0]=get32(b+68);
    m->scale_index[1]=get32(b+72);m->position_mdiv[0]=signed32(get32(b+76));
    m->position_mdiv[1]=signed32(get32(b+80));m->cursor[0]=get32(b+84);
    m->cursor[1]=get32(b+88);m->trigger_after_index=get32(b+96);
    if(b[4]==2) {
        m->trigger_level_q8=get32(b+100);m->calibration.id=get32(b+104);
        for(unsigned ch=0;ch<2;++ch) {
            m->calibration.slope_v_per_code[ch]=get_float(b+108+8u*ch);
            m->calibration.offset_v[ch]=get_float(b+112+8u*ch);
        }
        m->calibration.flags=get32(b+124);
    }
}
int scope_record_encode(const ScopeRecordMeta *meta,const uint32_t *words,
                        uint8_t *blob,size_t capacity,size_t *written) {
    if(written)*written=0;
    if(!meta||!words||!blob||capacity<SCOPE_RECORD_BLOB_BYTES||
       overlap(meta,sizeof(*meta),blob,SCOPE_RECORD_BLOB_BYTES)||
       overlap(words,SCOPE_RECORD_WORDS*sizeof(*words),blob,SCOPE_RECORD_BLOB_BYTES)||
       (written&&(overlap(written,sizeof(*written),meta,sizeof(*meta))||
                  overlap(written,sizeof(*written),words,SCOPE_RECORD_WORDS*sizeof(*words))||
                  overlap(written,sizeof(*written),blob,SCOPE_RECORD_BLOB_BYTES)))||
       !validate(meta,words,NULL))return -1;
    write_header(blob,meta);
    for(unsigned i=0;i<SCOPE_RECORD_WORDS;++i)
        put32(blob+SCOPE_RECORD_HEADER_BYTES+4u*i,words[i]);
    put32(blob+12,blob_crc(blob));
    if(written)*written=SCOPE_RECORD_BLOB_BYTES;
    return 0;
}
int scope_record_decode(const uint8_t *blob,size_t length,ScopeRecordMeta *meta,
                        uint32_t *words,size_t word_capacity) {
    ScopeRecordMeta checked;
    if(!blob||!meta||!words||length!=SCOPE_RECORD_BLOB_BYTES||word_capacity<SCOPE_RECORD_WORDS||
       overlap(blob,length,meta,sizeof(*meta))||
       overlap(blob,length,words,SCOPE_RECORD_WORDS*sizeof(*words))||
       overlap(meta,sizeof(*meta),words,SCOPE_RECORD_WORDS*sizeof(*words))||
       memcmp(blob,"SCW1",4)||(blob[4]!=1&&blob[4]!=2)||blob[5]!=0||blob[6]!=128||blob[7]!=0||
       get32(blob+8)!=SCOPE_RECORD_BLOB_BYTES||get32(blob+92)!=blob[4])return -1;
    if(blob[4]==1) {
        for(unsigned i=100;i<SCOPE_RECORD_HEADER_BYTES;++i)if(blob[i])return -1;
    }else if(!(get32(blob+124)&SCOPE_CALIBRATION_CHANNEL_MASK))return -1;
    if(get32(blob+12)!=blob_crc(blob))return -1;
    read_header(blob,&checked);
    if(!validate(&checked,NULL,blob+SCOPE_RECORD_HEADER_BYTES))return -1;
    *meta=checked;
    for(unsigned i=0;i<SCOPE_RECORD_WORDS;++i)
        words[i]=get32(blob+SCOPE_RECORD_HEADER_BYTES+4u*i);
    return 0;
}
