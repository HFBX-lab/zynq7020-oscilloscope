#include "scope_trigger.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#define COUNT 4096u
#define PI 3.14159265358979323846
static uint32_t words[COUNT], saved[COUNT];
static unsigned checks, failures;

#define CHECK(condition) do { \
    ++checks; \
    if (!(condition)) { \
        ++failures; \
        fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #condition); \
    } \
} while (0)

static ScopeTriggerConfig config(void)
{
    ScopeTriggerConfig c = {0, 1, 500.0, 10, 4.0, 0.5};
    return c;
}

static void fill(const unsigned *samples, size_t count, unsigned channel)
{
    size_t i;
    for (i = 0; i < count; ++i)
        words[i] = channel == 0 ? samples[i] | (777u << 16)
                                : 777u | (samples[i] << 16);
}

static void failure_is_clear(int expected, const uint32_t *input, size_t count,
                             const ScopeTriggerConfig *c)
{
    ScopeTriggerResult r = {123.0, 456.0};
    CHECK(scope_trigger_find(input, count, c, &r) == expected);
    CHECK(r.crossing_sample == 0.0 && r.display_start_sample == 0.0);
}

static void test_channels_and_directions(void)
{
    const unsigned rise[] = {470, 480, 490, 498, 506, 520, 530, 540};
    const unsigned fall[] = {530, 520, 510, 502, 494, 480, 470, 460};
    unsigned ch, rising;
    for (ch = 0; ch < 2; ++ch) {
        for (rising = 0; rising < 2; ++rising) {
            ScopeTriggerConfig c = config();
            ScopeTriggerResult r;
            c.channel = ch;
            c.rising = (int)rising;
            fill(rising ? rise : fall, 8, ch);
            memcpy(saved, words, sizeof words);
            CHECK(scope_trigger_find(words, 8, &c, &r) == 1);
            CHECK(fabs(r.crossing_sample - 3.25) < 1e-12);
            CHECK(fabs(r.display_start_sample - 1.25) < 1e-12);
            CHECK(memcmp(saved, words, sizeof words) == 0);
            c.channel ^= 1u;
            failure_is_clear(0, words, 8, &c);
        }
    }
}

static void test_hysteresis_and_recrossing(void)
{
    const unsigned unarmed[] = {499, 501, 499, 503, 509, 499, 510, 520};
    const unsigned unconfirmed[] = {480, 499, 501, 499, 504, 509, 499, 501};
    const unsigned recross[] = {480, 499, 501, 499, 503, 505, 510, 520};
    const unsigned rearm[] = {480, 499, 501, 480, 499, 503, 510, 520};
    ScopeTriggerConfig c = config();
    ScopeTriggerResult r;
    c.pretrigger_fraction = 0.0;
    c.span_samples = 1.0;
    fill(unarmed, 8, 0);
    failure_is_clear(0, words, 8, &c);
    fill(unconfirmed, 8, 0);
    failure_is_clear(0, words, 8, &c);
    fill(recross, 8, 0);
    CHECK(scope_trigger_find(words, 8, &c, &r) == 1);
    CHECK(fabs(r.crossing_sample - 3.25) < 1e-12);
    fill(rearm, 8, 0);
    CHECK(scope_trigger_find(words, 8, &c, &r) == 1);
    CHECK(fabs(r.crossing_sample - 4.25) < 1e-12);
}

static void test_window_bounds_and_earliest(void)
{
    const unsigned edges[] = {480, 520, 480, 520, 480, 520, 480, 520};
    const unsigned exact[] = {480, 490, 500, 520, 530, 540, 550, 560};
    const unsigned late_confirm[] = {480, 490, 500, 501, 502, 505, 509, 510};
    ScopeTriggerConfig c = config();
    ScopeTriggerResult r;
    fill(edges, 8, 0);
    CHECK(scope_trigger_find(words, 8, &c, &r) == 1);
    CHECK(r.crossing_sample == 2.5 && r.display_start_sample == 0.5);
    c.pretrigger_fraction = 0.0;
    c.span_samples = 1.0;
    CHECK(scope_trigger_find(words, 8, &c, &r) == 1);
    CHECK(r.crossing_sample == 0.5);
    c.pretrigger_fraction = 1.0;
    c.span_samples = 6.0;
    CHECK(scope_trigger_find(words, 8, &c, &r) == 1);
    CHECK(r.crossing_sample == 6.5 && r.display_start_sample == 0.5);
    c.span_samples = 7.0;
    failure_is_clear(0, words, 8, &c);
    c.span_samples = 7.0001;
    failure_is_clear(0, words, 8, &c);
    fill(exact, 8, 0);
    c.span_samples = 2.0;
    CHECK(scope_trigger_find(words, 8, &c, &r) == 1);
    CHECK(r.crossing_sample == 2.0 && r.display_start_sample == 0.0);
    c.span_samples = 5.0;
    c.pretrigger_fraction = 0.0;
    CHECK(scope_trigger_find(words, 8, &c, &r) == 1);
    CHECK(r.display_start_sample + c.span_samples == 7.0);
    c.span_samples = 5.0001;
    failure_is_clear(0, words, 8, &c);
    fill(late_confirm, 8, 0);
    c.span_samples = 1.0;
    CHECK(scope_trigger_find(words, 8, &c, &r) == 1);
    CHECK(r.crossing_sample == 2.0 && r.display_start_sample == 2.0);
}

static void test_otr_breaks_continuity(void)
{
    const unsigned samples[] = {480, 499, 501, 505, 510, 520, 530, 540};
    const unsigned recover[] = {480, 499, 501, 510, 480, 499, 501, 520, 530, 540};
    unsigned ch, position;
    ScopeTriggerConfig c = config();
    ScopeTriggerResult r;
    c.span_samples = 1.0;
    c.pretrigger_fraction = 0.0;
    for (ch = 0; ch < 2; ++ch) {
        c.channel = ch;
        for (position = 0; position <= 4; ++position) {
            fill(samples, 8, ch);
            words[position] |= 1u << (10 + 16 * ch);
            failure_is_clear(0, words, 8, &c);
            fill(samples, 8, ch);
            words[position] |= 1u << (10 + 16 * (ch ^ 1u));
            CHECK(scope_trigger_find(words, 8, &c, &r) == 1);
            CHECK(r.crossing_sample == 1.5);
        }
        fill(samples, 8, ch);
        words[7] |= 1u << (10 + 16 * ch);
        CHECK(scope_trigger_find(words, 8, &c, &r) == 1);
        /* An invalid point cancels the pending edge; a later complete edge
         * can rearm normally and must report its own crossing. */
        fill(recover, 10, ch);
        words[3] |= 1u << (10 + 16 * ch);
        CHECK(scope_trigger_find(words, 10, &c, &r) == 1);
        CHECK(r.crossing_sample == 5.5);
    }
}

static void test_invalid_and_format(void)
{
    ScopeTriggerConfig base = config(), c;
    unsigned bit;
    memset(words, 0, sizeof words);
    failure_is_clear(-1, NULL, 8, &base);
    failure_is_clear(-1, words, 0, &base);
    failure_is_clear(-1, words, 1, &base);
    failure_is_clear(-1, words, 4097, &base);
    failure_is_clear(-1, words, 8, NULL);
    CHECK(scope_trigger_find(words, 8, &base, NULL) == -1);
#define BAD(field, value) do { c = base; c.field = (value); \
    failure_is_clear(-1, words, 8, &c); } while (0)
    BAD(channel, 2u);
    BAD(rising, -1);
    BAD(rising, 2);
    BAD(level_code, -0.1);
    BAD(level_code, 1023.1);
    BAD(level_code, NAN);
    BAD(level_code, INFINITY);
    BAD(hysteresis_code, 0u);
    BAD(hysteresis_code, 1024u);
    BAD(span_samples, 0.0);
    BAD(span_samples, -1.0);
    BAD(span_samples, NAN);
    BAD(span_samples, INFINITY);
    BAD(pretrigger_fraction, -0.1);
    BAD(pretrigger_fraction, 1.1);
    BAD(pretrigger_fraction, NAN);
    BAD(pretrigger_fraction, INFINITY);
#undef BAD
    for (bit = 0; bit < 32; ++bit) {
        if ((0xf800f800u & (1u << bit)) != 0) {
            words[7] = 1u << bit;
            failure_is_clear(-1, words, 8, &base);
            words[7] = 0;
        }
    }
    /* Reserved bits reject even a record that has an earlier eligible edge. */
    words[0] = 480;
    words[1] = 520;
    words[7] = 0x80000000u;
    c = base;
    c.span_samples = 1.0;
    c.pretrigger_fraction = 0.0;
    failure_is_clear(-1, words, 8, &c);
    c.span_samples = 100.0;
    failure_is_clear(-1, words, 8, &c);
    memset(words, 0, sizeof words);
    c = base;
    c.level_code = 0.0;
    c.hysteresis_code = 1023;
    failure_is_clear(0, words, 8, &c);
    c.level_code = 1023.0;
    failure_is_clear(0, words, 8, &c);
}

static void test_minimum_record_and_fractional_level(void)
{
    const unsigned samples[] = {480, 520};
    ScopeTriggerConfig c = config();
    ScopeTriggerResult r;
    fill(samples, 2, 0);
    c.span_samples = 1.0;
    CHECK(scope_trigger_find(words, 2, &c, &r) == 1);
    CHECK(r.crossing_sample == 0.5 && r.display_start_sample == 0.0);
    c.level_code = 500.5;
    c.span_samples = 0.5;
    c.pretrigger_fraction = 0.0;
    failure_is_clear(0, words, 2, &c);
    c.pretrigger_fraction = 1.0;
    CHECK(scope_trigger_find(words, 2, &c, &r) == 1);
    CHECK(fabs(r.crossing_sample - 0.5125) < 1e-12);
}

static void test_phased_sine_anchor(void)
{
    unsigned phase, direction, ch;
    size_t i;
    for (ch = 0; ch < 2; ++ch) {
        for (direction = 0; direction < 2; ++direction) {
            for (phase = 0; phase < 12; ++phase) {
                ScopeTriggerConfig c = config();
                ScopeTriggerResult r;
                double sample, value, expected;
                c.channel = ch;
                c.rising = (int)direction;
                c.level_code = 512;
                c.span_samples = 1250;
                c.pretrigger_fraction = 0.2;
                for (i = 0; i < COUNT; ++i) {
                    unsigned v = (unsigned)(512.0 + 100.0 *
                        sin(2.0 * PI * ((double)i / 625.0 + phase / 12.0)) + 0.5);
                    words[i] = ch == 0 ? v | (777u << 16) : 777u | (v << 16);
                }
                CHECK(scope_trigger_find(words, COUNT, &c, &r) == 1);
                CHECK(fabs((r.crossing_sample - r.display_start_sample) /
                           c.span_samples - 0.2) < 1e-12);
                CHECK(r.display_start_sample >= 0 &&
                      r.display_start_sample + c.span_samples <= COUNT - 1);
                /* Analytic crossings verify phase alignment independently of
                 * quantized interpolation (tolerance: one ADC code in time). */
                expected = direction ? 0.0 : 0.5;
                sample = r.crossing_sample / 625.0 + phase / 12.0 - expected;
                CHECK(fabs(sample - round(sample)) < 0.002);
                value = sin(2.0 * PI * (r.crossing_sample / 625.0 + phase / 12.0));
                CHECK(fabs(value) < 0.006);
            }
        }
    }
}

static void test_hardware_named_edge(void)
{
    const unsigned rise[] = {480,499,501,480,499,501,499,499};
    const unsigned fall[] = {520,501,499,520,501,499,501,501};
    unsigned channel,rising;
    for(channel=0;channel<2;channel++)for(rising=0;rising<2;rising++){
        ScopeTriggerConfig c=config();ScopeTriggerResult r;
        c.channel=channel;c.rising=(int)rising;c.span_samples=2;c.pretrigger_fraction=.2;
        fill(rising?rise:fall,8,channel);
        CHECK(scope_trigger_interpolate_at(words,8,5,&c,&r)==1);
        CHECK(fabs(r.crossing_sample-4.5)<1e-12&&fabs(r.display_start_sample-4.1)<1e-12);
        /* Neither Schmitt confirmation nor a search for the first edge. */
        CHECK(scope_trigger_interpolate_at(words,8,2,&c,&r)==1&&fabs(r.crossing_sample-1.5)<1e-12);
        words[4]|=UINT32_C(0x400)<<(channel*16);
        CHECK(scope_trigger_interpolate_at(words,8,5,&c,&r)==0&&r.crossing_sample==0);
        fill(rising?rise:fall,8,channel);words[7]|=UINT32_C(0x80000000);
        CHECK(scope_trigger_interpolate_at(words,8,5,&c,&r)==-1);
        fill(rising?rise:fall,8,channel);c.rising=!c.rising;
        CHECK(scope_trigger_interpolate_at(words,8,5,&c,&r)==0);
    }
    ScopeTriggerConfig c=config();ScopeTriggerResult r;
    CHECK(scope_trigger_interpolate_at(words,8,0,&c,&r)==-1);
    CHECK(scope_trigger_interpolate_at(words,8,8,&c,&r)==-1);
    c.level_code=NAN;CHECK(scope_trigger_interpolate_at(words,8,5,&c,&r)==-1);
    c=config();c.span_samples=INFINITY;CHECK(scope_trigger_interpolate_at(words,8,5,&c,&r)==-1);
    CHECK(scope_trigger_interpolate_at(NULL,8,5,&c,&r)==-1);
    CHECK(scope_trigger_interpolate_at(words,8,5,NULL,&r)==-1);
    CHECK(scope_trigger_interpolate_at(words,8,5,&c,NULL)==-1);
}

int main(void)
{
    test_channels_and_directions();
    test_hysteresis_and_recrossing();
    test_window_bounds_and_earliest();
    test_otr_breaks_continuity();
    test_invalid_and_format();
    test_minimum_record_and_fractional_level();
    test_phased_sine_anchor();
    test_hardware_named_edge();
    printf("SCOPE_TRIGGER_TEST %s checks=%u failures=%u\n",
           failures ? "FAIL" : "PASS", checks, failures);
    return failures ? 1 : 0;
}
