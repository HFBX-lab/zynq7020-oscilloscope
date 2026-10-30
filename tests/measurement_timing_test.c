#include "scope_measurement.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

static uint32_t words[4096], backup[4096];
static unsigned checks, failures;
#define CHECK(x) do { ++checks; if (!(x)) { ++failures; \
    printf("FAIL line %u: %s\n", (unsigned)__LINE__, #x); } } while (0)
static uint32_t pack(unsigned a, unsigned b) { return a | ((uint32_t)b << 16); }
static double volts(double code) { return code * 10.0 / 1023.0 - 5.0; }
static void close_to(double actual, double expected, double tolerance) {
    ++checks;
    if (!isfinite(actual) || fabs(actual - expected) > tolerance) {
        ++failures; printf("FAIL numeric: %.12g expected %.12g tolerance %.4g\n",
                           actual, expected, tolerance);
    }
}
static ScopeTimingMeasurement analyze(unsigned count, unsigned rate, unsigned ch) {
    ScopeTimingMeasurement result;
    memcpy(backup, words, sizeof(words));
    CHECK(scope_measurement_timing(words, count, rate, ch, &result) == 0);
    CHECK(memcmp(backup, words, sizeof(words)) == 0);
    return result;
}
static void square(unsigned period, unsigned high_width, unsigned offset,
                   unsigned low, unsigned high) {
    for (unsigned i = 0; i < 4096; ++i) {
        unsigned code = (i + offset) % period < high_width ? high : low;
        words[i] = pack(code, code + 90);
    }
}
static void test_square_duty_and_metadata(void) {
    const unsigned widths[] = {20, 50, 80};
    const unsigned rates[] = {625, 6250, 62500, 625000, 6250000, 50000000};
    for (unsigned w = 0; w < 3; ++w) for (unsigned offset = 0; offset < 100; offset += 7) {
        square(100, widths[w], offset, 170, 770);
        for (unsigned ch = 0; ch < 2; ++ch) for (unsigned r = 0; r < 6; ++r) {
            unsigned rate = rates[r];
            ScopeTimingMeasurement t = analyze(4096, rate, ch);
            CHECK(t.pulse_valid && t.cycles >= 39);
            CHECK(!t.rise_valid && !t.fall_valid);
            CHECK(t.rise_s == 0 && t.fall_s == 0);
            close_to(t.duty_percent, widths[w], 1e-10);
            close_to(t.period_s, 100.0 / rate, 1e-14);
            close_to(t.positive_s, (double)widths[w] / rate, 1e-14);
            close_to(t.negative_s, (100.0 - widths[w]) / rate, 1e-14);
            close_to(t.positive_s + t.negative_s, t.period_s, 1e-14);
            close_to(t.low_v, volts(230 + ch * 90), 1e-12);
            close_to(t.mid_v, volts(470 + ch * 90), 1e-12);
            close_to(t.high_v, volts(710 + ch * 90), 1e-12);
        }
    }
}
static void test_sine_analytic_edges_and_channels(void) {
    const double pi = acos(-1.0);
    for (unsigned i = 0; i < 4096; ++i) {
        unsigned a = (unsigned)lround(480 + 250 * sin(2 * pi * (i + 17) / 128));
        unsigned b = (unsigned)lround(650 + 180 * sin(2 * pi * (i + 63) / 256));
        words[i] = pack(a, b);
    }
    for (unsigned ch = 0; ch < 2; ++ch) {
        const double period = ch ? 256 : 128;
        ScopeTimingMeasurement t = analyze(4096, 1000000, ch);
        CHECK(t.pulse_valid && t.rise_valid && t.fall_valid);
        close_to(t.duty_percent, 50, .001);
        close_to(t.period_s, period / 1000000, 1e-12);
        /* sin(theta)=-0.8 to +0.8: delta=2 asin(0.8). */
        /* 0.5-code ADC quantization at the 90% sine slope contributes up
         * to 0.38 sample across the two crossings for CH2. */
        close_to(t.rise_s, period * asin(.8) / pi / 1000000, .45e-6);
        close_to(t.fall_s, period * asin(.8) / pi / 1000000, .45e-6);
    }
    ScopeMeasurement all[2];
    CHECK(scope_measurement_analyze(words, 4096, 1000000, all) == 0);
    CHECK(all[0].timing.pulse_valid && all[1].timing.rise_valid);
    close_to(all[0].timing.period_s, 128e-6, 1e-12);
}
static void test_linear_triangle_and_asymmetric_ramps(void) {
    for (unsigned i = 0; i < 4096; ++i) {
        unsigned phase = (i + 7) % 200;
        unsigned code = phase <= 100 ? 200 + 6 * phase : 1400 - 6 * phase;
        unsigned p = (i + 21) % 200;
        unsigned trapezoid;
        if (p < 20) trapezoid = 200 + 30 * p;
        else if (p < 80) trapezoid = 800;
        else if (p < 120) trapezoid = 800 - 15 * (p - 80);
        else trapezoid = 200;
        words[i] = pack(code, trapezoid);
    }
    ScopeTimingMeasurement triangle = analyze(4096, 1000000, 0);
    CHECK(triangle.pulse_valid && triangle.rise_valid && triangle.fall_valid);
    close_to(triangle.duty_percent, 50, 1e-10);
    close_to(triangle.rise_s, 80e-6, 1e-12);
    close_to(triangle.fall_s, 80e-6, 1e-12);
    ScopeTimingMeasurement ramp = analyze(4096, 1000000, 1);
    CHECK(ramp.pulse_valid && ramp.rise_valid && ramp.fall_valid);
    close_to(ramp.duty_percent, 45, 1e-10);
    close_to(ramp.positive_s, 90e-6, 1e-12);
    close_to(ramp.negative_s, 110e-6, 1e-12);
    close_to(ramp.rise_s, 16e-6, 1e-12);
    close_to(ramp.fall_s, 32e-6, 1e-12);
}
static void test_rejections_and_clipping_locality(void) {
    ScopeTimingMeasurement t;
    for (unsigned i = 0; i < 4096; ++i) words[i] = pack(511, 530);
    t = analyze(4096, 1, 0); CHECK(!t.pulse_valid);
    square(100, 50, 30, 500, 515);
    t = analyze(4096, 1, 0); CHECK(!t.pulse_valid);
    square(100, 50, 30, 500, 516);
    t = analyze(4096, 1, 0); CHECK(t.pulse_valid);
    square(7, 3, 1, 200, 800);
    t = analyze(4096, 1, 0); CHECK(!t.pulse_valid);
    square(100, 1, 30, 200, 800);
    t = analyze(4096, 1, 0); CHECK(!t.pulse_valid);
    square(100, 99, 30, 200, 800);
    t = analyze(4096, 1, 0); CHECK(!t.pulse_valid);
    square(100, 50, 30, 200, 800);
    t = analyze(200, 1, 0); CHECK(!t.pulse_valid);
    t = analyze(300, 1, 0); CHECK(t.pulse_valid && t.cycles == 2);
    words[4095] |= 1u << 10;
    t = analyze(4096, 1, 0); CHECK(!t.pulse_valid && !t.rise_valid && !t.fall_valid);
    t = analyze(4096, 1, 1); CHECK(t.pulse_valid);
    square(100, 50, 30, 200, 800); words[4095] |= 1u << 26;
    t = analyze(4096, 1, 0); CHECK(t.pulse_valid);
    t = analyze(4096, 1, 1); CHECK(!t.pulse_valid);
    square(100, 50, 30, 0, 800);
    t = analyze(4096, 1, 0); CHECK(!t.pulse_valid);
    square(100, 50, 30, 200, 800); words[32] = pack(1023, 500);
    t = analyze(4096, 1, 0); CHECK(!t.pulse_valid);
    /* A single ramp, and only partial pulses, contain no full cycles. */
    for (unsigned i = 0; i < 4096; ++i) words[i] = pack(200 + i / 8, 500);
    t = analyze(4096, 1, 0); CHECK(!t.pulse_valid);
}
static void test_unstable_frequency_duty_ringing(void) {
    ScopeTimingMeasurement t;
    unsigned phase = 0, cycle = 0;
    for (unsigned i = 0; i < 4096; ++i) {
        unsigned period = cycle & 1u ? 160 : 100;
        words[i] = pack(phase < period / 2 ? 800 : 200, 500);
        if (++phase == period) { phase = 0; ++cycle; }
    }
    t = analyze(4096, 1, 0); CHECK(!t.pulse_valid);
    for (unsigned i = 0; i < 4096; ++i)
        words[i] = pack(i % 100 < ((i / 100) & 1u ? 20u : 80u) ? 800 : 200, 500);
    t = analyze(4096, 1, 0); CHECK(!t.pulse_valid);
    square(100, 50, 30, 200, 800);
    /* Small midpoint ripple stays within the hysteretic confirmation band.
     * The first candidate is retained, instead of following each recross. */
    for (unsigned i = 71; i + 3 < 4096; i += 100) {
        words[i - 1] = pack(490, 500); words[i] = pack(510, 500);
        words[i + 1] = pack(490, 500); words[i + 2] = pack(800, 500);
    }
    t = analyze(4096, 1, 0); CHECK(t.pulse_valid);
    square(100, 50, 30, 200, 800);
    /* Returning fully below 40% before reaching 60% is a failed edge,
     * rather than bounded midpoint ripple. */
    for (unsigned i = 71; i + 3 < 4096; i += 100) {
        words[i - 1] = pack(490, 500); words[i] = pack(510, 500);
        words[i + 1] = pack(430, 500); words[i + 2] = pack(800, 500);
    }
    t = analyze(4096, 1, 0); CHECK(!t.pulse_valid);
    /* Ringing at the 90% threshold leaves midpoint duty reliable, but
     * must reject the repeated threshold's rise time. */
    for (unsigned i = 0; i < 4096; ++i) {
        unsigned p = (i + 21) % 200;
        unsigned a = p < 40 ? 200 + 15 * p : p < 100 ? 800 : 200;
        if (p == 40) a = 720;
        words[i] = pack(a, 500);
    }
    t = analyze(4096, 1000000, 0);
    CHECK(t.pulse_valid && !t.rise_valid);
    /* Same midpoint period/duty, alternating slopes: inconsistent edge times. */
    for (unsigned i = 0; i < 4096; ++i) {
        unsigned p = i % 200, half_ramp = (i / 200) & 1u ? 10 : 30;
        unsigned a;
        if (p < 50 - half_ramp) a = 200;
        else if (p < 50 + half_ramp) a = 200 + 600 * (p - 50 + half_ramp) / (2 * half_ramp);
        else if (p < 150) a = 800;
        else a = 200;
        words[i] = pack(a, 500);
    }
    t = analyze(4096, 1000000, 0); CHECK(t.pulse_valid && !t.rise_valid);
}
static void test_cycle_boundaries_fractional_phase_and_resolution(void) {
    ScopeTimingMeasurement t;
    square(8, 2, 6, 200, 800);
    t = analyze(4096, 1000000, 0);
    CHECK(t.pulse_valid && t.cycles > 500);
    close_to(t.duty_percent, 25, 1e-10);
    close_to(t.positive_s, 2e-6, 1e-12);
    close_to(t.negative_s, 6e-6, 1e-12);
    /* Exact two-sample 10..90% intervals must be measurable. */
    for (unsigned i = 0; i < 4096; ++i) {
        unsigned p = i % 32, a = 200;
        if (p == 1 || p == 19) a = 260;
        else if (p == 2 || p == 18) a = 500;
        else if (p == 3 || p == 17) a = 740;
        else if (p >= 4 && p <= 16) a = 800;
        words[i] = pack(a, a);
    }
    t = analyze(4096, 1000000, 0);
    CHECK(t.pulse_valid && t.rise_valid && t.fall_valid);
    close_to(t.rise_s, 2e-6, 1e-12);
    close_to(t.fall_s, 2e-6, 1e-12);
    /* A tiny acceleration makes the traversal strictly <2 samples. */
    for (unsigned i = 0; i < 4096; ++i) {
        unsigned p = i % 32;
        if (p == 1 || p == 19) words[i] = pack(259, 259);
        else if (p == 3 || p == 17) words[i] = pack(741, 741);
    }
    t = analyze(4096, 1000000, 0);
    CHECK(t.pulse_valid && !t.rise_valid && !t.fall_valid);
    for (unsigned offset = 0; offset < 4; ++offset) {
        double phase_offset = offset * .25;
        for (unsigned i = 0; i < 4096; ++i) {
            double p = fmod(i + phase_offset, 80);
            double a = p < 8 ? 200 + 75 * p : p < 32 ? 800 :
                       p < 40 ? 800 - 75 * (p - 32) : 200;
            words[i] = pack((unsigned)lround(a), (unsigned)lround(a));
        }
        /* The last boundary rise is confirmed at 60% but ends before 90%.
         * It completes the previous period; its own rise is not selected. */
        t = analyze(246, 1000000, 0);
        CHECK(t.pulse_valid && t.rise_valid && t.fall_valid && t.cycles == 3);
        close_to(t.duty_percent, 40, .02);
        close_to(t.positive_s, 32e-6, .02e-6);
        close_to(t.negative_s, 48e-6, .02e-6);
        close_to(t.rise_s, 6.4e-6, .03e-6);
        close_to(t.fall_s, 6.4e-6, .03e-6);
    }
    /* Starting after the 10% crossing must not invent that first edge,
     * although its rising-to-rising pulse cycle is complete. */
    for (unsigned i = 0; i < 4096; ++i) {
        unsigned p = (i + 17) % 200;
        words[i] = pack(p <= 100 ? 200 + 6 * p : 1400 - 6 * p, 500);
    }
    t = analyze(4096, 1000000, 0);
    CHECK(t.pulse_valid && !t.rise_valid && t.fall_valid);
    CHECK(t.rise_s == 0);
}
static void invalid(const uint32_t *data, unsigned count, unsigned rate, unsigned channel) {
    ScopeTimingMeasurement result, zero;
    memset(&result, 0xa5, sizeof(result)); memset(&zero, 0, sizeof(zero));
    CHECK(scope_measurement_timing(data, count, rate, channel, &result) == -1);
    CHECK(memcmp(&result, &zero, sizeof(result)) == 0);
}
static void test_malformed_and_clearing(void) {
    square(100, 50, 30, 200, 800);
    invalid(NULL, 4096, 100, 0); invalid(words, 0, 100, 0);
    invalid(words, 4097, 100, 0); invalid(words, 4096, 0, 0);
    invalid(words, 4096, 100, 2);
    CHECK(scope_measurement_timing(words, 4096, 100, 0, NULL) == -1);
    for (unsigned bit = 0; bit < 32; ++bit) if ((1u << bit) & 0xf800f800u) {
        square(100, 50, 30, 200, 800); words[4095] |= 1u << bit;
        invalid(words, 4096, 100, 0); invalid(words, 4096, 100, 1);
    }
    square(100, 50, 30, 200, 800);
    ScopeTimingMeasurement t = analyze(1, 100, 0);
    CHECK(!t.pulse_valid && !t.rise_valid && !t.fall_valid && t.cycles == 0);
}
int main(void) {
    test_square_duty_and_metadata();
    test_sine_analytic_edges_and_channels();
    test_linear_triangle_and_asymmetric_ramps();
    test_rejections_and_clipping_locality();
    test_unstable_frequency_duty_ringing();
    test_cycle_boundaries_fractional_phase_and_resolution();
    test_malformed_and_clearing();
    printf("timing measurement: %u checks, %u failures\n", checks, failures);
    return failures ? 1 : 0;
}
