#include "record_stats.h"
#include "fixture_expected.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define COUNT 4096u
#define RATE 625000u
#define PI 3.14159265358979323846

static uint32_t words[COUNT];
static unsigned failures, checks;

#define CHECK(condition) do { \
    ++checks; \
    if (!(condition)) { \
        ++failures; \
        fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #condition); \
    } \
} while (0)

static unsigned code(double volts)
{
    double x = (volts + 5.0) * 1023.0 / 10.0;
    if (x < 0.0) return 0;
    if (x > 1023.0) return 1023;
    return (unsigned)(x + 0.5);
}

static uint32_t pack(unsigned a, unsigned b)
{
    return (uint32_t)a | ((uint32_t)b << 16);
}

static void fill_dc(unsigned a, unsigned b)
{
    unsigned i;
    for (i = 0; i < COUNT; ++i) words[i] = pack(a, b);
}

static void test_dc(void)
{
    AdcLiveReport r;
    fill_dc(512, 1023);
    CHECK(adc_live_analyze(words, COUNT, RATE, &r) == 0);
    CHECK(r.channel[0].min_code == 512 && r.channel[0].max_code == 512);
    CHECK(r.channel[0].mean_code == 512.0);
    CHECK(r.channel[0].vpp_v == 0.0);
    CHECK(fabs(r.channel[0].rms_v - 5.0 / 1023.0) < 1e-10);
    CHECK(fabs(r.channel[1].rms_v - 5.0) < 1e-10);
    CHECK(!r.channel[0].frequency_valid && r.channel[0].frequency_hz == 0);
    CHECK(!r.channel[1].frequency_valid);
    fill_dc(0, 0);
    CHECK(adc_live_analyze(words, COUNT, RATE, &r) == 0);
    CHECK(r.channel[0].mean_code == 0 && r.channel[0].vpp_v == 0);
    CHECK(fabs(r.channel[0].rms_v - 5.0) < 1e-10);
    CHECK(!r.channel[0].frequency_valid);
}

static void test_sine(void)
{
    unsigned i;
    AdcLiveReport r;
    double sum = 0.0, sum_sq = 0.0;
    for (i = 0; i < COUNT; ++i) {
        unsigned a = code(1.25 + 0.5 * sin(2.0 * PI * 1000.0 * i / RATE));
        double v = a * 10.0 / 1023.0 - 5.0;
        words[i] = pack(a, code(-0.3 + sin(2.0 * PI * 2000.0 * i / RATE)));
        sum += a;
        sum_sq += v * v;
    }
    CHECK(adc_live_analyze(words, COUNT, RATE, &r) == 0);
    CHECK(fabs(r.channel[0].mean_code - sum / COUNT) < 1e-10);
    CHECK(fabs(r.channel[0].rms_v - sqrt(sum_sq / COUNT)) < 1e-10);
    CHECK(fabs(r.channel[0].vpp_v - 1.0) < 0.02);
    CHECK(r.channel[0].frequency_valid);
    CHECK(fabs(r.channel[0].frequency_hz - 1000.0) < 2.0);
    CHECK(r.channel[1].frequency_valid);
    CHECK(fabs(r.channel[1].frequency_hz - 2000.0) < 5.0);
}

static void test_square_and_minimum_period(void)
{
    unsigned i;
    AdcLiveReport r;
    for (i = 0; i < COUNT; ++i)
        words[i] = pack(i % 100 < 50 ? 400 : 624, i % 4 < 2 ? 300 : 700);
    CHECK(adc_live_analyze(words, COUNT, RATE, &r) == 0);
    CHECK(r.channel[0].frequency_valid);
    CHECK(fabs(r.channel[0].frequency_hz - RATE / 100.0) < 0.01);
    CHECK(r.channel[1].frequency_valid);
    CHECK(fabs(r.channel[1].frequency_hz - RATE / 4.0) < 0.01);
    for (i = 0; i < COUNT; ++i) words[i] = pack(i % 3 ? 400 : 624, 512);
    CHECK(adc_live_analyze(words, COUNT, RATE, &r) == 0);
    CHECK(!r.channel[0].frequency_valid);
}

static void test_otr_and_reserved(void)
{
    unsigned i;
    AdcLiveReport r;
    fill_dc(512, 512);
    words[0] |= 0x04000400u;
    words[COUNT - 1] |= 0x04000000u;
    CHECK(adc_live_analyze(words, COUNT, RATE, &r) == 0);
    CHECK(r.channel[0].otr_count == 1 && r.channel[1].otr_count == 2);
    CHECK(r.channel[0].min_code == 512 && r.channel[1].mean_code == 512);
    for (i = 0; i < 32; ++i) {
        if ((0xf800f800u & ((uint32_t)1 << i)) == 0) continue;
        fill_dc(512, 512);
        words[COUNT - 1] |= (uint32_t)1 << i;
        memset(&r, 0xff, sizeof(r));
        CHECK(adc_live_analyze(words, COUNT, RATE, &r) == -1);
        CHECK(r.crc32 == 0 && r.channel[0].mean_code == 0);
    }
}

static void test_invalid_input(void)
{
    AdcLiveReport r;
    fill_dc(512, 512);
    CHECK(adc_live_analyze(NULL, COUNT, RATE, &r) == -1);
    CHECK(adc_live_analyze(words, COUNT, RATE, NULL) == -1);
    CHECK(adc_live_analyze(words, COUNT - 1, RATE, &r) == -1);
    CHECK(adc_live_analyze(words, COUNT + 1, RATE, &r) == -1);
    CHECK(adc_live_analyze(words, 0, RATE, &r) == -1);
    CHECK(adc_live_analyze(words, COUNT, 0, &r) == -1);
}

static uint32_t random_next(uint32_t *state)
{
    *state = *state * 1664525u + 1013904223u;
    return *state;
}

static void test_no_frequency(void)
{
    unsigned i;
    uint32_t seed = 1234567u;
    AdcLiveReport r;
    for (i = 0; i < COUNT; ++i)
        words[i] = pack(512 + (random_next(&seed) >> 29),
                        400 + (random_next(&seed) >> 24));
    CHECK(adc_live_analyze(words, COUNT, RATE, &r) == 0);
    CHECK(!r.channel[0].frequency_valid);
    CHECK(!r.channel[1].frequency_valid);
    for (i = 0; i < COUNT; ++i) {
        double phase = 2 * PI * (0.0005 * i + 0.0000004 * i * i);
        words[i] = pack(code(sin(phase)), i / 4);
    }
    CHECK(adc_live_analyze(words, COUNT, RATE, &r) == 0);
    CHECK(!r.channel[0].frequency_valid);
    CHECK(!r.channel[1].frequency_valid);
    /* A short record with only one complete period cannot establish frequency. */
    for (i = 0; i < COUNT; ++i)
        words[i] = pack(code(sin(2 * PI * 1.5 * i / COUNT)), 512);
    CHECK(adc_live_analyze(words, COUNT, RATE, &r) == 0);
    CHECK(!r.channel[0].frequency_valid);
}

static void test_crc(const char *path)
{
    unsigned i, j;
    AdcLiveReport r;
    FILE *file;
    for (i = 0; i < COUNT; ++i)
        words[i] = pack((i * 7 + 13) % 1024 | ((i % 11 == 0) ? 1024 : 0),
                        (i * 19 + 23) % 1024 | ((i % 17 == 0) ? 1024 : 0));
    CHECK(adc_live_analyze(words, COUNT, RATE, &r) == 0);
    CHECK(r.crc32 == FIXTURE_CRC32);
    printf("CRC_FIXTURE=%08lX\n", (unsigned long)r.crc32);
    file = fopen(path, "wb");
    CHECK(file != NULL);
    if (file == NULL) return;
    for (i = 0; i < COUNT; ++i) {
        for (j = 0; j < 4; ++j) {
            int result = fputc((int)((words[i] >> (8 * j)) & 255), file);
            CHECK(result != EOF);
        }
    }
    CHECK(fclose(file) == 0);
}

int main(int argc, char **argv)
{
    if (argc != 2) return 2;
    test_dc();
    test_sine();
    test_square_and_minimum_period();
    test_otr_and_reserved();
    test_invalid_input();
    test_no_frequency();
    test_crc(argv[1]);
    printf("ADC_LIVE_STATS checks=%u failures=%u\n", checks, failures);
    return failures ? 1 : 0;
}
