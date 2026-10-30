#include "scope_model.h"
#include <math.h>

void scope_model_init(ScopeModel *s) {
    *s = (ScopeModel){.wave=SCOPE_SINE, .running=true,
                     .frequency_hz=1000, .amplitude_v=2, .elapsed_s=0};
}
double scope_sample(const ScopeModel *s, double time_s) {
    double p = time_s * s->frequency_hz;
    p -= floor(p);
    if (s->wave == SCOPE_SQUARE) return (p < .5 ? 1 : -1) * s->amplitude_v;
    if (s->wave == SCOPE_TRIANGLE) return (1 - 4*fabs(p - .25 - floor(p + .25))) * s->amplitude_v;
    return sin(6.283185307179586 * p) * s->amplitude_v;
}
void scope_advance(ScopeModel *s, double dt_s) {
    if (s->running && dt_s > 0) s->elapsed_s += dt_s;
}
int scope_voltage_to_plot(double v, double volts_per_div) {
    if (volts_per_div <= 0) return 0;
    double value = v * 1000 / volts_per_div;
    if (value > 4000) return 4000;
    if (value < -4000) return -4000;
    return (int)lround(value);
}
double scope_rms(const ScopeModel *s) {
    if (s->wave == SCOPE_SQUARE) return s->amplitude_v;
    return s->amplitude_v / sqrt(s->wave == SCOPE_SINE ? 2.0 : 3.0);
}
