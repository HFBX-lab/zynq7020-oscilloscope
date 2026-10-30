#include "adc_format.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do { if (!(x)) { fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#x); exit(1); } } while (0)
static uint32_t record[ADC_RECORD_WORDS];
/* An independent fixture: arithmetic quotient/remainder, not the encoder. */
static uint32_t fixture_word(uint32_t n) {
    unsigned low = n % 1024u, high = (n / 1024u) % 1024u;
    unsigned a = low + ((n / 4u) % 2u) * 1024u;
    unsigned b = ((1023u - low) ^ high) + ((n / 8u) % 2u) * 1024u;
    return a + b * 65536u;
}
static void fixture(uint32_t start) {
    for (unsigned i=0;i<ADC_RECORD_WORDS;i++) record[i]=fixture_word((start+i)%1048576u);
}
int main(void) {
    static const struct { uint32_t n, word; } vectors[] = {
        {0,0x03ff0000u}, {1,0x03fe0001u}, {4,0x03fb0404u},
        {8,0x07f70008u}, {1023,0x040007ffu}, {1024,0x03fe0000u},
        {1048575,0x07ff07ffu}, {1048576,0x03ff0000u}
    };
    for(unsigned i=0;i<sizeof(vectors)/sizeof(vectors[0]);i++) {
        uint32_t decoded=0xffffffffu;
        CHECK(adc_test_word(vectors[i].n)==vectors[i].word);
        CHECK(adc_test_decode(vectors[i].word,&decoded)==0);
        CHECK(decoded==(vectors[i].n&ADC_SEQUENCE_MASK));
    }
    CHECK(adc_crc32((const uint8_t*)"123456789",9)==0xcbf43926u);
    CHECK(adc_crc32((const uint8_t*)"",0)==0u);
    AdcRecordReport report;
    fixture(997u);
    CHECK(adc_validate_record(record,ADC_RECORD_WORDS,&report)==0);
    CHECK(report.first_sequence==997u && report.bad_word==ADC_RECORD_WORDS);
    uint8_t bytes[ADC_RECORD_BYTES];
    for(unsigned i=0;i<ADC_RECORD_WORDS;i++)for(unsigned j=0;j<4;j++)bytes[4*i+j]=(uint8_t)(record[i]>>(8*j));
    CHECK(report.crc32==adc_crc32(bytes,sizeof(bytes)));
    fixture(1048560u);
    CHECK(adc_validate_record(record,ADC_RECORD_WORDS,&report)==0);
    CHECK(report.first_sequence==1048560u);
    fixture(0);for(unsigned i=1024;i<ADC_RECORD_WORDS;i++)record[i]=record[i%1024];
    CHECK(adc_validate_record(record,ADC_RECORD_WORDS,&report)!=0 && report.bad_word==1024u);
    fixture(12);record[203]^=1u;
    CHECK(adc_validate_record(record,ADC_RECORD_WORDS,&report)!=0 && report.bad_word==203u);
    fixture(12);record[77]^=0x400u;
    CHECK(adc_validate_record(record,ADC_RECORD_WORDS,&report)!=0 && report.bad_word==77u);
    fixture(12);record[78]^=0x04000000u;
    CHECK(adc_validate_record(record,ADC_RECORD_WORDS,&report)!=0 && report.bad_word==78u);
    fixture(12);record[0]^=0x400u;
    CHECK(adc_validate_record(record,ADC_RECORD_WORDS,&report)!=0 && report.bad_word==0u);
    for(unsigned bit=0;bit<32;bit++)if(0xf800f800u&(1u<<bit)) {
        uint32_t n;
        CHECK(adc_test_decode(0x03ff0000u|(1u<<bit),&n)!=0);
    }
    fixture(0);
    CHECK(adc_validate_record(NULL,ADC_RECORD_WORDS,&report)!=0);
    CHECK(adc_validate_record(record,ADC_RECORD_WORDS-1,&report)!=0);
    CHECK(adc_validate_record(record,ADC_RECORD_WORDS+1,&report)!=0);
    CHECK(adc_validate_record(record,ADC_RECORD_WORDS,NULL)!=0);
    CHECK(adc_test_decode(0,NULL)!=0);
    puts("ADC_FORMAT_PASS: fixed vectors, CRC32, 4096 words, wrap, alias, sequence, OTR, padding, arguments");
    return 0;
}
