// SPDX-License-Identifier: MIT
// Panda PWR V2 build-only hardware gate.
// No V2 output GPIO is configured or driven by this component.
#include "dp_relay.h"

#include <string.h>

#include "esp_log.h"

static const char *TAG = "dp_relay_v2";

esp_err_t dp_relay_init(void)
{
    ESP_LOGW(TAG, "V2 output control DISABLED: AC/USB hardware not qualified");
    return ESP_OK;
}

esp_err_t dp_relay_set(dp_output_t output, bool on)
{
    (void)on;
    if (output < 0 || output >= DP_OUTPUT_COUNT) {
        return ESP_ERR_INVALID_ARG;
    }
    // This blocks every existing API and button path from driving outputs.
    return ESP_ERR_NOT_SUPPORTED;
}

bool dp_relay_get(dp_output_t output)
{
    (void)output;
    // Inactive is a software contract, NOT physical relay-contact readback.
    return false;
}

dp_restore_t dp_relay_get_restore(void)
{
    return DP_RESTORE_OFF;
}

esp_err_t dp_relay_set_restore(dp_restore_t policy)
{
    (void)policy;
    // Do not persist a future ON/LAST request while V2 actuation is disabled.
    return ESP_ERR_NOT_SUPPORTED;
}

const char *dp_restore_to_str(dp_restore_t policy)
{
    switch (policy) {
    case DP_RESTORE_OFF: return "off";
    case DP_RESTORE_ON: return "on";
    case DP_RESTORE_LAST: return "last";
    default: return "invalid";
    }
}

dp_restore_t dp_restore_from_str(const char *text, dp_restore_t fallback)
{
    if (!text) return fallback;
    if (strcmp(text, "off") == 0) return DP_RESTORE_OFF;
    if (strcmp(text, "on") == 0) return DP_RESTORE_ON;
    if (strcmp(text, "last") == 0) return DP_RESTORE_LAST;
    return fallback;
}

esp_err_t dp_relay_clear(void)
{
    // No V2 output settings exist in this preliminary build.
    return ESP_OK;
}
