#include "record_stats.h"
#include "scope_measurement.h"

#include <math.h>
#include <string.h>

enum { RECORD_WORDS = 4096, CODE_MASK = 1023, OTR_MASK = 1024 };

static unsigned channel_code(uint32_t word, unsigned channel)
{
    return (unsigned)((word >> (channel * 16)) & CODE_MASK);
}

static uint32_t crc_word(uint32_t crc, uint32_t word)
{
    unsigned byte, bit;
    for (byte = 0; byte < 4; ++byte) {
        crc ^= word & 255u;
        word >>= 8;
        for (bit = 0; bit < 8; ++bit)
            crc = (crc >> 1) ^ ((crc & 1u) ? 0xedb88320u : 0u);
    }
    return crc;
}

static void estimate_frequency(const uint32_t *words, size_t count,
                               uint32_t rate, unsigned channel,
                               AdcLiveChannel *result)
{
    result->frequency_valid=scope_measurement_frequency(words,(unsigned)count,rate,
                                                       channel,&result->frequency_hz);
}

int adc_live_analyze(const uint32_t *words, size_t count,
                     uint32_t sample_rate, AdcLiveReport *report)
{
    AdcLiveReport local;
    double code_sum[2] = {0.0, 0.0};
    double voltage_square_sum[2] = {0.0, 0.0};
    uint32_t crc = 0xffffffffu;
    unsigned channel;
    size_t i;
    if (report == NULL) return -1;
    memset(report, 0, sizeof(*report));
    if (words == NULL || count != RECORD_WORDS || sample_rate == 0) return -1;
    memset(&local, 0, sizeof(local));
    for (channel = 0; channel < 2; ++channel)
        local.channel[channel].min_code = CODE_MASK;
    for (i = 0; i < count; ++i) {
        if ((words[i] & 0xf800f800u) != 0) return -1;
        crc = crc_word(crc, words[i]);
        for (channel = 0; channel < 2; ++channel) {
            AdcLiveChannel *result = &local.channel[channel];
            unsigned code = channel_code(words[i], channel);
            double voltage = code * (10.0 / 1023.0) - 5.0;
            if (code < result->min_code) result->min_code = code;
            if (code > result->max_code) result->max_code = code;
            if (((words[i] >> (channel * 16)) & OTR_MASK) != 0)
                ++result->otr_count;
            code_sum[channel] += code;
            voltage_square_sum[channel] += voltage * voltage;
        }
    }
    local.crc32 = crc ^ 0xffffffffu;
    for (channel = 0; channel < 2; ++channel) {
        AdcLiveChannel *result = &local.channel[channel];
        result->mean_code = code_sum[channel] / count;
        result->vpp_v = (result->max_code - result->min_code) * (10.0 / 1023.0);
        result->rms_v = sqrt(voltage_square_sum[channel] / count);
        estimate_frequency(words, count, sample_rate, channel, result);
    }
    *report = local;
    return 0;
}
