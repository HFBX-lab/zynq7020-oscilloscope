#ifndef SCOPE_MODEL_H
#define SCOPE_MODEL_H
#include <stdbool.h>
typedef enum { SCOPE_SINE, SCOPE_SQUARE, SCOPE_TRIANGLE } ScopeWave;
typedef struct {
    ScopeWave wave;
    bool running;
    double frequency_hz, amplitude_v, elapsed_s;
} ScopeModel;
void scope_model_init(ScopeModel *s);
double scope_sample(const ScopeModel *s, double time_s);
void scope_advance(ScopeModel *s, double dt_s);
int scope_voltage_to_plot(double v, double volts_per_div);
double scope_rms(const ScopeModel *s);
#endif
