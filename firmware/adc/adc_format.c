#include "adc_format.h"
uint32_t adc_test_word(uint32_t n) {
    uint32_t d1=n&1023u;
    uint32_t d2=(1023u-d1)^((n>>10)&1023u);
    return d1|(((n>>2)&1u)<<10)|(d2<<16)|(((n>>3)&1u)<<26);
}
int adc_test_decode(uint32_t word,uint32_t *n) {
    if(!n || (word&0xf800f800u))return -1;
    uint32_t d1=word&1023u;
    uint32_t high=((word>>16)&1023u)^(1023u-d1);
    uint32_t value=d1|(high<<10);
    if(adc_test_word(value)!=word)return -1;
    *n=value;return 0;
}
static uint32_t crc_byte(uint32_t crc,uint8_t byte) {
    crc^=byte;
    for(unsigned i=0;i<8;i++)crc=(crc>>1)^((0u-(crc&1u))&0xedb88320u);
    return crc;
}
uint32_t adc_crc32(const uint8_t *p,size_t n) {
    uint32_t crc=0xffffffffu;
    while(n--)crc=crc_byte(crc,*p++);
    return ~crc;
}
int adc_validate_record(const uint32_t *p,size_t n,AdcRecordReport *r) {
    if(!p || !r || n!=ADC_RECORD_WORDS)return -1;
    r->first_sequence=0;r->crc32=0;r->bad_word=0;
    if(adc_test_decode(p[0],&r->first_sequence))return -1;
    uint32_t crc=0xffffffffu;
    for(size_t i=0;i<n;i++) {
        if(p[i]!=adc_test_word((r->first_sequence+(uint32_t)i)&ADC_SEQUENCE_MASK)) {
            r->bad_word=i;return -1;
        }
        for(unsigned byte=0;byte<4;byte++)crc=crc_byte(crc,(uint8_t)(p[i]>>(8*byte)));
    }
    r->bad_word=n;r->crc32=~crc;return 0;
}
