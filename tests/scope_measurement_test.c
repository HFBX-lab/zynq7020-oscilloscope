#include "scope_measurement.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

static uint32_t words[4096], original[4096];
static unsigned checks, failures;
#define CHECK(condition) do { ++checks; if (!(condition)) { \
    ++failures; printf("FAIL line %u: %s\n", (unsigned)__LINE__, #condition); \
} } while (0)

static uint32_t packed(unsigned a, unsigned b) {
    return (uint32_t)a | ((uint32_t)b << 16);
}

static double volts(unsigned code) {
    return (double)code * 10.0 / 1023.0 - 5.0;
}

/* Independent two-pass long-double oracle in code units: production uses
 * streaming voltage statistics, never this sum/second-pass calculation. */
static void check_statistics(unsigned count, unsigned rate) {
    ScopeMeasurement result[2];
    memcpy(original, words, count * sizeof(words[0]));
    CHECK(scope_measurement_analyze(words, count, rate, result) == 0);
    CHECK(memcmp(words, original, count * sizeof(words[0])) == 0);
    for (unsigned ch = 0; ch < 2; ++ch) {
        unsigned low = 1023, high = 0;
        long double sum = 0, square_sum = 0, variance_sum = 0;
        bool overrange = false;
        for (unsigned i = 0; i < count; ++i) {
            unsigned sample = (words[i] >> (16 * ch)) & 1023u;
            long double voltage = (long double)sample * 10.0L / 1023.0L - 5.0L;
            if (sample < low) low = sample;
            if (sample > high) high = sample;
            sum += sample;
            square_sum += voltage * voltage;
            if (sample == 0 || sample == 1023 ||
                (words[i] & (1u << (10 + 16 * ch)))) overrange = true;
        }
        long double mean_code = sum / count;
        for (unsigned i = 0; i < count; ++i) {
            long double delta = ((words[i] >> (16 * ch)) & 1023u) - mean_code;
            variance_sum += delta * delta;
        }
        double mean = (double)(mean_code * 10.0L / 1023.0L - 5.0L);
        double total = (double)sqrtl(square_sum / count);
        double ac = (double)(sqrtl(variance_sum / count) * 10.0L / 1023.0L);
        CHECK(result[ch].valid);
        CHECK(result[ch].overrange == overrange);
        CHECK(fabs(result[ch].vmin_v - volts(low)) < 1e-12);
        CHECK(fabs(result[ch].vmax_v - volts(high)) < 1e-12);
        CHECK(fabs(result[ch].vpp_v - (volts(high) - volts(low))) < 1e-12);
        CHECK(fabs(result[ch].vmean_v - mean) < 1e-12);
        CHECK(fabs(result[ch].rms_total_v - total) < 1e-12);
        CHECK(fabs(result[ch].rms_ac_v - ac) < 1e-12);
        CHECK(fabs(result[ch].rms_total_v * result[ch].rms_total_v -
                   result[ch].vmean_v * result[ch].vmean_v -
                   result[ch].rms_ac_v * result[ch].rms_ac_v) < 1e-12);
    }
}

static void test_dc_and_single_sample(void) {
    ScopeMeasurement result[2];
    for (unsigned i = 0; i < 4096; ++i) words[i] = packed(311, 897);
    check_statistics(4096, 50000000);
    CHECK(scope_measurement_analyze(words, 4096, 625, result) == 0);
    CHECK(result[0].rms_ac_v == 0.0 && result[1].rms_ac_v == 0.0);
    CHECK(result[0].vpp_v == 0.0 && result[1].vpp_v == 0.0);
    check_statistics(1, 1);
}

static void test_sine_and_square(void) {
    const double pi = 3.14159265358979323846;
    for (unsigned i = 0; i < 4096; ++i) {
        unsigned a = (unsigned)lround(400.0 + 150.0 * sin(2 * pi * i / 128.0));
        unsigned b = (unsigned)lround(720.0 + 80.0 * cos(2 * pi * i / 64.0));
        words[i] = packed(a, b);
    }
    check_statistics(4096, 6250000);
    check_statistics(4095, 625000);
    for (unsigned i = 0; i < 4096; ++i)
        words[i] = packed(i % 4 ? 450 : 150, i % 8 < 3 ? 901 : 701);
    check_statistics(4096, 62500);
}

static void test_small_ripple_at_large_offset(void) {
    ScopeMeasurement result[2];
    for (unsigned i = 0; i < 4096; ++i)
        words[i] = packed(1000 + (i & 1u), 20 + ((i / 2) & 1u));
    check_statistics(4096, 6250);
    CHECK(scope_measurement_analyze(words, 4096, 6250, result) == 0);
    CHECK(fabs(result[0].rms_ac_v - 5.0 / 1023.0) < 1e-13);
    CHECK(fabs(result[1].rms_ac_v - 5.0 / 1023.0) < 1e-13);
}

static void test_overrange_is_channel_local_and_keeps_statistics(void) {
    ScopeMeasurement result[2];
    for (unsigned i = 0; i < 4096; ++i) words[i] = packed(400, 600);
    words[4095] |= 1u << 10;
    check_statistics(4096, 625000);
    CHECK(scope_measurement_analyze(words, 4096, 625000, result) == 0);
    CHECK(result[0].overrange && !result[1].overrange);
    words[4095] = packed(400, 600) | (1u << 26);
    check_statistics(4096, 625000);
    CHECK(scope_measurement_analyze(words, 4096, 625000, result) == 0);
    CHECK(!result[0].overrange && result[1].overrange);
    words[0] = packed(0, 600);
    words[4095] = packed(1023, 600);
    check_statistics(4096, 625000);
    CHECK(scope_measurement_analyze(words, 4096, 625000, result) == 0);
    CHECK(result[0].overrange && !result[1].overrange);
    words[0] = packed(400, 0);
    words[4095] = packed(400, 1023);
    check_statistics(4096, 625000);
    CHECK(scope_measurement_analyze(words, 4096, 625000, result) == 0);
    CHECK(!result[0].overrange && result[1].overrange);
}

static void check_rejected(const uint32_t *input, unsigned count, unsigned rate) {
    ScopeMeasurement result[2], zero[2];
    memset(result, 0xa5, sizeof(result));
    memset(zero, 0, sizeof(zero));
    CHECK(scope_measurement_analyze(input, count, rate, result) == -1);
    CHECK(memcmp(result, zero, sizeof(result)) == 0);
}

static void test_invalid_record_clears_both_outputs(void) {
    for (unsigned i = 0; i < 4096; ++i) words[i] = packed(400, 600);
    for (unsigned bit = 0; bit < 32; ++bit) {
        if ((UINT32_C(0xf800f800) & (UINT32_C(1) << bit)) == 0) continue;
        words[3001] |= UINT32_C(1) << bit;
        check_rejected(words, 4096, 50000000);
        words[3001] = packed(400, 600);
    }
    words[4095] |= 1u << 11;
    check_rejected(words, 4096, 625);
    words[4095] = packed(400, 600);
    check_rejected(NULL, 4096, 625);
    check_rejected(words, 0, 625);
    check_rejected(words, 4097, 625);
    check_rejected(words, ~0u, 625);
    check_rejected(words, 4096, 0);
    CHECK(scope_measurement_analyze(words, 4096, 625, NULL) == -1);
}

int main(void) {
    test_dc_and_single_sample();
    test_sine_and_square();
    test_small_ripple_at_large_offset();
    test_overrange_is_channel_local_and_keeps_statistics();
    test_invalid_record_clears_both_outputs();
    printf("MEASUREMENT_CORE_TEST_%s: %u checks, %u failures\n",
           failures ? "FAIL" : "PASS", checks, failures);
    return failures ? 1 : 0;
}
