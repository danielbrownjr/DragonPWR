// SPDX-License-Identifier: MIT
// Experimental V2-only GPIO7 command, NOT a characterized AC relay driver.
// No boot-time pin access. GPIO18/19, USB enables and local inputs untouched.
#include "sdkconfig.h"
#if !defined(CONFIG_IDF_TARGET_ESP32C3) || !defined(CONFIG_DP_V2_GPIO7_EXPERIMENT)
#error "Experimental GPIO7 pulse requires explicit ESP32-C3 build opt-in"
#endif

#include "dp_v2_experiment.h"
#include "dp_board.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "dp_v2_gpio7_exp";

esp_err_t dp_v2_experiment_gpio7_pulse(int level, uint32_t milliseconds)
{
    if ((level != 0 && level != 1) || milliseconds < 100 || milliseconds > 2000) {
        return ESP_ERR_INVALID_ARG;
    }

    // There is intentionally no assumed inactive level. Only drive after an
    // explicit operator request; on completion relinquish pin ownership.
    // High impedance does NOT imply the outlet is off.
    esp_err_t err = gpio_set_level(DP_V2_PIN_AC_COMMAND, level);
    if (err != ESP_OK) {
        return err;
    }
    err = gpio_set_direction(DP_V2_PIN_AC_COMMAND, GPIO_MODE_OUTPUT);
    if (err != ESP_OK) {
        (void)gpio_set_direction(DP_V2_PIN_AC_COMMAND, GPIO_MODE_INPUT);
        return err;
    }

    ESP_LOGW(TAG, "EXPERIMENT: GPIO7 driven %d for %lu ms; physical AC state UNKNOWN",
             level, (unsigned long)milliseconds);
    vTaskDelay(pdMS_TO_TICKS(milliseconds) + 1);

    // Bounded command, even if the client drops the connection.
    err = gpio_set_direction(DP_V2_PIN_AC_COMMAND, GPIO_MODE_INPUT);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "GPIO7 release FAILED: %s", esp_err_to_name(err));
        return err;
    }
    ESP_LOGW(TAG, "EXPERIMENT: GPIO7 released to input; AC state UNKNOWN");
    return ESP_OK;
}
