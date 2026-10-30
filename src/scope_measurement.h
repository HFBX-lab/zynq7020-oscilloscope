#ifndef SCOPE_MEASUREMENT_H
#define SCOPE_MEASUREMENT_H

#include <stdbool.h>
#include <stdint.h>
#include "scope_calibration.h"

typedef struct {
    bool pulse_valid, rise_valid, fall_valid;
    unsigned cycles;
    double duty_percent, positive_s, negative_s, period_s, rise_s, fall_s;
    double low_v, mid_v, high_v;
} ScopeTimingMeasurement;

typedef struct {
    bool valid, overrange;
    double vmin_v, vmax_v, vmean_v, vpp_v, rms_total_v, rms_ac_v;
    ScopeTimingMeasurement timing;
} ScopeMeasurement;

/* Full-record pulse/edge measurements, using record rate (not current ADC rate).
 * References are record min/max's 10%, 50%, 90% in uncalibrated input volts.
 * Pulse widths use interpolated midpoint crossings, confirmed at 40/60%.
 * Requires >=3 rising crossings, >=2 complete rising-to-rising cycles, >=8
 * samples/cycle and >=2 samples for each polarity. Period and both widths must
 * each stay within 20% of their own mean. Keep the first midpoint candidate
 * through ripple inside 40..60%; returning to the original hysteresis boundary
 * before opposite-boundary confirmation rejects the record. Local endpoint
 * codes/OTR or span<16 reject
 * timing, without rejecting the other channel's measurements/statistics.
 * Rise/fall independently require exactly one directional 10% and 90% crossing
 * in the surrounding opposite-midpoint half-cycle, monotonic traversal lasting
 * >=2 sample intervals, and <=20% duration variation across the same complete
 * cycles used for duty. Unresolved fast edges remain invalid, even with valid
 * duty. First/last partial cycles never contribute to averages. Duty is total
 * positive time / total period; pulse widths and edges average selected cycles.
 * No allocation/input mutation; count=1..4096, rate>0, channel=0/1. words/out
 * must not overlap. Return -1 for invalid arguments/reserved bits (either
 * channel); otherwise 0, with independent validity flags for unreliable input.
 * Clear out first when non-NULL; invalid value fields/cycles remain zero.
 * Reference fields may be retained for legal, unclipped but unreliable input. */
int scope_measurement_timing(const uint32_t *words, unsigned count,
                            unsigned rate, unsigned channel,
                            ScopeTimingMeasurement *out);

/* Analyze both channels across the complete supplied record without allocation
 * or input mutation. Uncalibrated module-input volts = code * 10 / 1023 - 5.
 * CH1/CH2 codes occupy bits 0..9/16..25; OTR bits are 10/26. OTR or an endpoint
 * code (0/1023) sets that channel's overrange while retaining valid statistics.
 * Total RMS includes DC; AC RMS removes the record mean (population variance).
 * words and out must not overlap. count must be 1..4096 and rate must be > 0;
 * rate is the original record metadata, used to convert cached timing results;
 * frequency is still provided by the separate frequency estimator.
 * Return 0 on success or -1 for invalid arguments/reserved bits (0xf800f800).
 * On failure, clear both outputs when out is non-NULL. */
int scope_measurement_analyze(const uint32_t *words, unsigned count,
                              unsigned rate, ScopeMeasurement out[2]);

/* Same raw-data/time validity rules, with a record-owned voltage profile.
 * A NULL profile preserves nominal behavior. profile must not overlap out. */
int scope_measurement_timing_calibrated(const uint32_t *words,unsigned count,
    unsigned rate,unsigned channel,const ScopeCalibration *profile,
    ScopeTimingMeasurement *out);
int scope_measurement_analyze_calibrated(const uint32_t *words,unsigned count,
    unsigned rate,const ScopeCalibration *profile,ScopeMeasurement out[2]);

/* Same hysteretic full-record estimator used by live acquisition and replay.
 * Requires >=3 confirmed crossings, >=4 samples/cycle and cycle variation<=20%.
 * OTR is retained as metadata; consumers separately reject clipped metrics.
 * Invalid/unreliable output is false and a non-NULL frequency is set to zero. */
bool scope_measurement_frequency(const uint32_t *words, unsigned count,
                                 unsigned rate, unsigned channel,
                                 double *frequency_hz);

#endif
