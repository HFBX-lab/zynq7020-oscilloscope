#ifndef SCOPE_TRIGGER_H
#define SCOPE_TRIGGER_H

#include <stddef.h>
#include <stdint.h>

typedef struct {
    unsigned channel;
    int rising;
    double level_code;
    unsigned hysteresis_code;
    double span_samples;
    double pretrigger_fraction;
} ScopeTriggerConfig;

typedef struct {
    double crossing_sample;
    double display_start_sample;
} ScopeTriggerResult;

/* Find a hysteresis-confirmed edge in one immutable dual-10-bit record.
 * Return 1 for a complete display window, 0 for no eligible edge (including
 * a span greater than count - 1), or -1 for invalid arguments/record format.
 * count is 2..4096. Channel is 0/1; rising is 0/1; level is finite 0..1023;
 * hysteresis is 1..1023; span is finite >0; pretrigger is finite 0..1.
 * Selected-channel OTR breaks edge continuity. Reserved bits reject the record.
 * On failure, a non-NULL result is cleared. No allocation or input mutation.
 */
int scope_trigger_find(const uint32_t *words, size_t count,
                       const ScopeTriggerConfig *config,
                       ScopeTriggerResult *result);

/* Interpolate only words[after_index-1..after_index]. No edge search or
 * hysteresis confirmation: the hardware already owns rearm and matching.
 * Uses the same argument/format/window rules as find; after_index is 1..count-1.
 * Return 1 for a valid named crossing/window, 0 for OTR/noncrossing/window,
 * -1 for invalid arguments/reserved bits. Clears a non-NULL result on failure. */
int scope_trigger_interpolate_at(const uint32_t *words,size_t count,unsigned after_index,
                                 const ScopeTriggerConfig *config,ScopeTriggerResult *result);

#endif
