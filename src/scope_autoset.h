#ifndef SCOPE_AUTOSET_H
#define SCOPE_AUTOSET_H

#include "scope_measurement.h"

typedef enum {
    SCOPE_AUTOSET_OK = 0,
    SCOPE_AUTOSET_INVALID,
    SCOPE_AUTOSET_NO_SIGNAL,
    SCOPE_AUTOSET_OVERRANGE,
    SCOPE_AUTOSET_NO_TIMEBASE
} ScopeAutosetResult;

typedef struct {
    unsigned timebase_index, scale_index[2], channel_mask, trigger_channel;
    int position_mdiv[2], level_mv;
    double frequency_hz;
} ScopeAutosetPlan;

/* Pure one-shot plan from a complete record; channel is zero-based.
 * Failure clears out when non-NULL. channel_mask marks channels whose
 * independent display settings should be applied; preserve all others.
 * Uses nominal uncalibrated volts and only digital acquisition limits. */
ScopeAutosetResult scope_autoset_plan(unsigned conversion_hz, unsigned count,
    unsigned source_rate, unsigned channel, double frequency_hz,
    const ScopeMeasurement measurements[2], ScopeAutosetPlan *out);
/* Uses physical statistics from this same record-owned profile. Raw-code
 * signal floor and representable voltage limits follow that profile. */
ScopeAutosetResult scope_autoset_plan_calibrated(unsigned conversion_hz,unsigned count,
    unsigned source_rate,unsigned channel,double frequency_hz,
    const ScopeMeasurement measurements[2],const ScopeCalibration *profile,ScopeAutosetPlan *out);

#endif
