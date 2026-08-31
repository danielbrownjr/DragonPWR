#pragma once

// The two firmware-controlled outputs: the mains relay and the switched USB
// port. This component owns their polarity, so nothing else in the tree needs
// to know that the relay is active low.

#include <stdbool.h>

#include "esp_err.h"

typedef enum {
    DP_OUTPUT_MAINS = 0,  // the 220 V outlet
    DP_OUTPUT_USB1,       // the switched USB port
    DP_OUTPUT_COUNT,
} dp_output_t;

// What the outputs do after a power cut. Persisted in NVS.
typedef enum {
    DP_RESTORE_OFF = 0,   // default: come up off, whatever happened before
    DP_RESTORE_ON,
    DP_RESTORE_LAST,
    // Parse-failure sentinel. Never persisted, and deliberately out of the
    // valid range so a bad stored byte still fails safe to DP_RESTORE_OFF.
    DP_RESTORE_INVALID = 0xFF,
} dp_restore_t;

// Configures both pins and applies the restore policy. Both outputs are driven
// to their INACTIVE level before they become outputs, so the relay cannot
// glitch on during bring-up. Call this before networking.
esp_err_t dp_relay_init(void);

esp_err_t dp_relay_set(dp_output_t output, bool on);

// Reads the pin back rather than returning a cached flag, which is why both
// pins are configured INPUT_OUTPUT. Stock does the same.
bool dp_relay_get(dp_output_t output);

dp_restore_t dp_relay_get_restore(void);
esp_err_t dp_relay_set_restore(dp_restore_t policy);

const char *dp_restore_to_str(dp_restore_t policy);
dp_restore_t dp_restore_from_str(const char *text, dp_restore_t fallback);

// Clears this component's persisted settings. Wired into the portal's factory
// reset.
esp_err_t dp_relay_clear(void);
