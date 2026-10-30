#ifndef SCOPE_SETTINGS_H
#define SCOPE_SETTINGS_H

#include <stddef.h>
#include <stdint.h>

#define SCOPE_SETTINGS_BLOB_BYTES 96u
#define SCOPE_SETTINGS_RISING 1u
#define SCOPE_SETTINGS_NORMAL 2u
#define SCOPE_SETTINGS_DUAL 4u
#define SCOPE_SETTINGS_GRID 8u
#define SCOPE_SETTINGS_CURSORS 16u

typedef struct {
    uint32_t timebase_ns, origin_conversion_hz, selected_channel,
             trigger_channel, flags, scale_index[2], cursor[2], language;
    int32_t trigger_level_mv, position_mdiv[2];
} ScopeSettings;

/* SCP1 v1 is exactly 96 little-endian bytes. CRC32 uses the SCW1 IEEE
 * helper, with bytes 12..15 zeroed. Origin conversion is metadata only.
 * All input/output ranges must be disjoint. Returns 0 on success, -1 on
 * invalid input. Failures preserve outputs except non-NULL written is
 * cleared to zero. Defaults do not change any hardware clock. */
int scope_settings_encode(const ScopeSettings *settings, uint8_t *blob,
                          size_t capacity, size_t *written);
int scope_settings_decode(const uint8_t *blob, size_t length,
                          ScopeSettings *settings);
int scope_settings_defaults(unsigned conversion_hz, ScopeSettings *settings);

#endif
