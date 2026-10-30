#include "scope_model.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>

int main(void) {
    ScopeModel s;
    scope_model_init(&s);
    assert(s.running && s.wave == SCOPE_SINE);
    s.frequency_hz = 1000;
    s.amplitude_v = 2;
    assert(fabs(scope_sample(&s, 0.00025) - 2) < 0.0001);
    assert(fabs(scope_sample(&s, 0.00075) + 2) < 0.0001);
    s.wave = SCOPE_SQUARE;
    assert(scope_sample(&s, 0.00025) == 2);
    assert(scope_sample(&s, 0.00075) == -2);
    s.wave = SCOPE_TRIANGLE;
    assert(fabs(scope_sample(&s, 0)) < 0.0001);
    assert(fabs(scope_sample(&s, 0.00025) - 2) < 0.0001);
    assert(fabs(scope_sample(&s, 0.00075) + 2) < 0.0001);
    scope_advance(&s, 0.04);
    double saved = s.elapsed_s;
    assert(saved > 0);
    s.running = false;
    scope_advance(&s, 1);
    assert(s.elapsed_s == saved);
    assert(scope_voltage_to_plot(100, 1) == 4000);
    assert(scope_voltage_to_plot(-100, 1) == -4000);
    assert(scope_voltage_to_plot(1, 1) == 1000);
    assert(fabs(scope_rms(&s) - 2 / sqrt(3.0)) < 0.0001);
    puts("PASS: sine/square/triangle, physical scale, clipping, stop, RMS");
    return 0;
}
