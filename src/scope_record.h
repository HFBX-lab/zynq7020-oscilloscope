#ifndef SCOPE_RECORD_H
#define SCOPE_RECORD_H

#include <stddef.h>
#include <stdint.h>
#include "scope_calibration.h"

#define SCOPE_RECORD_WORDS 4096u
#define SCOPE_RECORD_HEADER_BYTES 128u
#define SCOPE_RECORD_BLOB_BYTES 16512u
#define SCOPE_RECORD_TRIGGERED 1u
#define SCOPE_RECORD_RISING 2u
#define SCOPE_RECORD_NORMAL 4u
#define SCOPE_RECORD_DUAL 8u
#define SCOPE_RECORD_CURSORS 16u
#define SCOPE_RECORD_GRID 32u

typedef struct {
    uint32_t count, rate_hz, conversion_hz, filter_kind, record_id,
             capture_epoch, timebase_ns, view_start_q16, trigger_crossing_q16,
             flags, trigger_channel, selected_channel, scale_index[2],
             cursor[2], trigger_after_index;
    int32_t trigger_level_mv, position_mdiv[2];
    ScopeCalibration calibration;
    uint32_t trigger_level_q8;
} ScopeRecordMeta;

/* SCW1 v1/v2 is exactly 128 little-endian header bytes and 4096 raw dual words.
 * No input/output ranges may overlap. All failures leave outputs unchanged,
 * except a non-NULL written is always cleared before validation.
 * ADC OTR bits are preserved. An all-zero calibration and trigger_level_q8
 * encode legacy v1 nominal units exactly. A valid nonzero calibration mask
 * encodes v2 float32 coefficients and the actual trigger Q8, which must match
 * the stored physical mV level even when no trigger was matched. v1 decode
 * supplies a zero calibration and zero trigger_level_q8.
 * CRC32 is IEEE reflected (123456789 -> cbf43926). Blob CRC treats 12..15 as 0.
 * Encode/decode return 0 on success, -1 on any invalid input. */
int scope_record_encode(const ScopeRecordMeta *meta, const uint32_t *words,
                        uint8_t *blob, size_t capacity, size_t *written);
int scope_record_decode(const uint8_t *blob, size_t length,
                        ScopeRecordMeta *meta, uint32_t *words,
                        size_t word_capacity);
uint32_t scope_record_crc32(const void *data, size_t length);

#endif
