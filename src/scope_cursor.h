#ifndef SCOPE_CURSOR_H
#define SCOPE_CURSOR_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "scope_calibration.h"
typedef struct {
    double time_s[2],voltage_v[2][2],delta_s,delta_v[2],reciprocal_hz;
    bool point_valid[2][2],delta_valid[2],reciprocal_valid;
} ScopeCursorResult;
/* Cursor positions are 0..1000 fractions of the visible span. Voltages are
 * uncalibrated module-input values. No allocation or record mutation. */
int scope_cursor_measure(const uint32_t *words,size_t count,unsigned rate,
                         double start,double span,const unsigned positions[2],
                         ScopeCursorResult *result);
/* Record-owned calibrated voltage; NULL profile is nominal. No input buffer
 * (including profile) may overlap result. Time/OTR validity is unchanged. */
int scope_cursor_measure_calibrated(const uint32_t *words,size_t count,unsigned rate,
    double start,double span,const unsigned positions[2],
    const ScopeCalibration *profile,ScopeCursorResult *result);
#endif
