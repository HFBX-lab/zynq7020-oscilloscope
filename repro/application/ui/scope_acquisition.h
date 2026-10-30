#ifndef SCOPE_ACQUISITION_H
#define SCOPE_ACQUISITION_H
#include <stdint.h>
#include <stdbool.h>

#define SCOPE_TIMEBASE_COUNT 13u
#define SCOPE_HIGHSPEED_TIMEBASE_COUNT 18u
#define SCOPE_50M_TIMEBASE_COUNT 21u
#define SCOPE_FILTER_RAW 0u
#define SCOPE_FILTER_CIC3 3u
typedef struct {
    unsigned rate_index,rate_hz,count,conversion_hz,timeout_ms,filter_kind;
} ScopeCapturePlan;
typedef struct {
    uint32_t capture_epoch;
    unsigned rate_index,conversion_hz,filter_kind;
} ScopeRecordInfo;
typedef struct {
    unsigned control,level_q8,pre_samples,hysteresis_codes;
} ScopeHardwareConfig;
typedef struct {
    bool matched,auto_forced;
    unsigned after_index;
} ScopeHardwareTrigger;

/* Returns zero on success, -1 for an invalid index or null output. The
   deadline includes 100 ms for filter warmup, configuration and scheduling. */
int scope_acquisition_plan(unsigned timebase_index,ScopeCapturePlan *out);
/* Profiles: 5 MHz keeps 13 indices, 10/25 MHz share 18 time labels,
   and 50 MHz adds three nanosecond labels for 21 indices. Record rates
   and RAW/CIC3 selection depend on the conversion profile. */
int scope_acquisition_plan_for(unsigned timebase_index,unsigned conversion_hz,ScopeCapturePlan *out);
#endif
