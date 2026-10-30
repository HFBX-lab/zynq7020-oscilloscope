#ifndef SCOPE_CALIBRATION_H
#define SCOPE_CALIBRATION_H

#include <stdbool.h>
#include <stdint.h>

#define SCOPE_CALIBRATION_CH1 UINT32_C(1)
#define SCOPE_CALIBRATION_CH2 UINT32_C(2)
#define SCOPE_CALIBRATION_CHANNEL_MASK UINT32_C(3)
#define SCOPE_CALIBRATION_GENERATOR_SETPOINTS UINT32_C(0x100)

/* Coefficients retain their float32 identity in live and saved records; math
 * promotes them to double. A NULL/all-zero profile preserves nominal units.
 * A nonzero channel mask requires GENERATOR_SETPOINTS; other flag bits fail.
 * Active slopes are finite .005f.. .015f V/code with zero code in [256,768].
 * Inactive coefficients are zero. Only the nominal profile requires id=0;
 * calibrated id=0 remains legal because a valid CRC32 can equal zero. */
typedef struct {
    uint32_t flags,id;
    float slope_v_per_code[2],offset_v[2];
} ScopeCalibration;

bool scope_calibration_valid(const ScopeCalibration *profile);

/* Channel is 0/1. voltage accepts interpolated codes in [0,1023]; code returns
 * the unclamped inverse. Invalid profile/channel/nonfinite input returns NAN. */
double scope_calibration_voltage(const ScopeCalibration *profile,
                                 unsigned channel,double code);
double scope_calibration_code(const ScopeCalibration *profile,
                              unsigned channel,double volts);

/* Physical level is -5000..5000 mV. Convert, clamp to [0,1023], then round Q8.
 * Nominal channels use the historical integer quantizer exactly. Return 0 on
 * success; -1 leaves out unchanged. out must not overlap profile storage. */
int scope_calibration_trigger_q8(const ScopeCalibration *profile,
                                 unsigned channel,int level_mv,uint32_t *out);

#endif
