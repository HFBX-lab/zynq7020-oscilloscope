#ifndef ADC_LIVE_RECORD_STATS_H
#define ADC_LIVE_RECORD_STATS_H

#include <stddef.h>
#include <stdint.h>

typedef struct {
    unsigned min_code, max_code, otr_count;
    double mean_code, vpp_v, rms_v, frequency_hz;
    int frequency_valid;
} AdcLiveChannel;

typedef struct {
    uint32_t crc32;
    AdcLiveChannel channel[2];
} AdcLiveReport;

/* Analyze one frozen 4096-word dual-channel record. Return 0 on success,
 * -1 for invalid input. On failure a non-null report is cleared.
 * Word halves contain 10-bit straight-binary codes and OTR at bit 10.
 * Voltages use the nominal, uncalibrated +/-5 V module conversion;
 * rms_v includes DC. frequency_valid is authoritative; invalid Hz is zero.
 * CRC bytes are explicitly little endian regardless of host architecture.
 * Input and report storage must not overlap. No allocation or OS is used. */
int adc_live_analyze(const uint32_t *words, size_t count,
                     uint32_t sample_rate, AdcLiveReport *report);

#endif
