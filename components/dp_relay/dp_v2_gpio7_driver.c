// SPDX-License-Identifier: MIT
// Unconnected, opt-in GPIO7 electrical adapter for independent review.
// The hardware has no contact feedback and reset-under-HIGH is unverified.
#include "dp_v2_gpio7_driver.h"
#include "dp_board.h"
#include "driver/gpio.h"
#include "esp_rom_gpio.h"
#include "esp_log.h"

static const char *TAG = "dp_v2_gpio7_candidate";

bool dp_v2_gpio7_drive(bool high, void *context)
{
    (void)context;
    // HIGH was observed to energize unloaded AC output. No assumption that
    // LOW or INPUT guarantees open contacts under all hardware conditions.
    esp_rom_gpio_pad_select_gpio(DP_V2_PIN_AC_COMMAND);
    if (!high) {
        esp_err_t err = gpio_set_direction(DP_V2_PIN_AC_COMMAND, GPIO_MODE_INPUT);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "GPIO7 relinquish failed: %s", esp_err_to_name(err));
            return false;
        }
        return true;
    }
    // Output latch is preloaded before output enable, as characterized in #42.
    esp_err_t err = gpio_set_level(DP_V2_PIN_AC_COMMAND, 1);
    if (err != ESP_OK) {
        return false;
    }
    err = gpio_set_direction(DP_V2_PIN_AC_COMMAND, GPIO_MODE_OUTPUT);
    if (err != ESP_OK) {
        (void)gpio_set_direction(DP_V2_PIN_AC_COMMAND, GPIO_MODE_INPUT);
        return false;
    }
    return true;
}
