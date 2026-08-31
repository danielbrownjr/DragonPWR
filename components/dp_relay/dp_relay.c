// SPDX-License-Identifier: MIT
#include "dp_relay.h"

#include <string.h>

#include "dp_board.h"
#include "esp_log.h"
#include "nvs.h"
#include "nvs_flash.h"

static const char *TAG = "dp_relay";

// The family shares one NVS namespace, and it is the same one the stock
// firmware used, so a device migrated from stock keeps its Wi-Fi credentials.
#define DP_NVS_NAMESPACE "app_nvs"
#define DP_NVS_RESTORE   "pwr_restore"

typedef struct {
    gpio_num_t  gpio;
    int         active_level;
    const char *last_key;   // NVS key holding the last state, for DP_RESTORE_LAST
    const char *name;
} dp_output_desc_t;

static const dp_output_desc_t OUTPUTS[DP_OUTPUT_COUNT] = {
    [DP_OUTPUT_MAINS] = { DP_PIN_RELAY, DP_RELAY_ACTIVE_LEVEL, "pwr_last", "mains" },
    [DP_OUTPUT_USB1]  = { DP_PIN_USB1,  DP_USB1_ACTIVE_LEVEL,  "usb_last", "usb1"  },
};

static int inactive_level(const dp_output_desc_t *out)
{
    return out->active_level ? 0 : 1;
}

static esp_err_t store_u8(const char *key, uint8_t value)
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open(DP_NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        return err;
    }
    err = nvs_set_u8(handle, key, value);
    if (err == ESP_OK) {
        err = nvs_commit(handle);
    }
    nvs_close(handle);
    return err;
}

static uint8_t load_u8(const char *key, uint8_t fallback)
{
    nvs_handle_t handle;
    if (nvs_open(DP_NVS_NAMESPACE, NVS_READONLY, &handle) != ESP_OK) {
        return fallback;
    }
    uint8_t value = fallback;
    if (nvs_get_u8(handle, key, &value) != ESP_OK) {
        value = fallback;
    }
    nvs_close(handle);
    return value;
}

esp_err_t dp_relay_init(void)
{
    // Order matters. Writing the output register BEFORE the pin becomes an
    // output means the driver starts driving the value we chose, instead of
    // whatever the register happened to hold. On an active-low relay behind a
    // mains outlet, getting this backwards switches the load on at every boot.
    for (int i = 0; i < DP_OUTPUT_COUNT; i++) {
        gpio_set_level(OUTPUTS[i].gpio, inactive_level(&OUTPUTS[i]));
    }

    const gpio_config_t io = {
        .pin_bit_mask = (1ULL << DP_PIN_RELAY) | (1ULL << DP_PIN_USB1),
        // INPUT_OUTPUT, not OUTPUT, so the level can be read back. Stock builds
        // its whole state JSON out of gpio_get_level on these two pins.
        .mode         = GPIO_MODE_INPUT_OUTPUT,
        .pull_up_en   = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };
    esp_err_t err = gpio_config(&io);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "gpio_config failed: %s", esp_err_to_name(err));
        return err;
    }

    const dp_restore_t policy = dp_relay_get_restore();
    for (int i = 0; i < DP_OUTPUT_COUNT; i++) {
        bool on = false;
        if (policy == DP_RESTORE_ON) {
            on = true;
        } else if (policy == DP_RESTORE_LAST) {
            on = load_u8(OUTPUTS[i].last_key, 0) != 0;
        }
        dp_relay_set((dp_output_t)i, on);
    }

    ESP_LOGI(TAG, "outputs up, restore policy=%s", dp_restore_to_str(policy));
    return ESP_OK;
}

esp_err_t dp_relay_set(dp_output_t output, bool on)
{
    if (output < 0 || output >= DP_OUTPUT_COUNT) {
        return ESP_ERR_INVALID_ARG;
    }
    const dp_output_desc_t *desc = &OUTPUTS[output];
    esp_err_t err = gpio_set_level(desc->gpio,
                                   on ? desc->active_level : inactive_level(desc));
    if (err != ESP_OK) {
        return err;
    }
    ESP_LOGI(TAG, "%s -> %s", desc->name, on ? "on" : "off");

    if (dp_relay_get_restore() == DP_RESTORE_LAST) {
        store_u8(desc->last_key, on ? 1 : 0);
    }
    return ESP_OK;
}

bool dp_relay_get(dp_output_t output)
{
    if (output < 0 || output >= DP_OUTPUT_COUNT) {
        return false;
    }
    const dp_output_desc_t *desc = &OUTPUTS[output];
    return gpio_get_level(desc->gpio) == desc->active_level;
}

dp_restore_t dp_relay_get_restore(void)
{
    uint8_t stored = load_u8(DP_NVS_RESTORE, DP_RESTORE_OFF);
    if (stored > DP_RESTORE_LAST) {
        return DP_RESTORE_OFF;   // fail safe, not fail last-known
    }
    return (dp_restore_t)stored;
}

esp_err_t dp_relay_set_restore(dp_restore_t policy)
{
    if (policy > DP_RESTORE_LAST) {
        return ESP_ERR_INVALID_ARG;
    }
    return store_u8(DP_NVS_RESTORE, (uint8_t)policy);
}

const char *dp_restore_to_str(dp_restore_t policy)
{
    switch (policy) {
    case DP_RESTORE_ON:   return "on";
    case DP_RESTORE_LAST: return "last";
    case DP_RESTORE_OFF:
    default:              return "off";
    }
}

dp_restore_t dp_restore_from_str(const char *text, dp_restore_t fallback)
{
    if (!text) {
        return fallback;
    }
    if (strcmp(text, "on") == 0)   return DP_RESTORE_ON;
    if (strcmp(text, "last") == 0) return DP_RESTORE_LAST;
    if (strcmp(text, "off") == 0)  return DP_RESTORE_OFF;
    return fallback;
}

esp_err_t dp_relay_clear(void)
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open(DP_NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        return err;
    }
    nvs_erase_key(handle, DP_NVS_RESTORE);
    for (int i = 0; i < DP_OUTPUT_COUNT; i++) {
        nvs_erase_key(handle, OUTPUTS[i].last_key);
    }
    err = nvs_commit(handle);
    nvs_close(handle);
    return err;
}
