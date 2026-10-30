#ifndef ADC_FORMAT_H
#define ADC_FORMAT_H
#include <stddef.h>
#include <stdint.h>
#define ADC_RECORD_WORDS 4096u
#define ADC_RECORD_BYTES (ADC_RECORD_WORDS * 4u)
#define ADC_SEQUENCE_MASK 0xfffffu
typedef struct {
    uint32_t first_sequence;
    uint32_t crc32;
    size_t bad_word;
} AdcRecordReport;
uint32_t adc_test_word(uint32_t sequence);
int adc_test_decode(uint32_t word, uint32_t *sequence);
uint32_t adc_crc32(const uint8_t *bytes, size_t count);
int adc_validate_record(const uint32_t *words, size_t count, AdcRecordReport *report);
#endif
