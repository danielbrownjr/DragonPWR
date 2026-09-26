// SPDX-License-Identifier: MIT
#include "dp_button.h"

#include "dp_board.h"
#include "dp_relay.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "dp_button";

// Both pins are read with no hardware or software interrupt - stock services
// GPIO6 the same way, by polling it from a task (see docs/HARDWARE_ANALYSIS.md,
// app_ctl_task). A stable disagreement for DP_BUTTON_DEBOUNCE_TICKS polls
// (60 ms at the interval below) is treated as a real transition; anything
// shorter is contact bounce or noise.
//
// The tick counters only advance while a pin disagrees with its stable
// level, so they stay bounded by DP_BUTTON_DEBOUNCE_TICKS. Counting every
// poll would overflow a signed int after ~497 days at 20 ms - realistic
// uptime for a plug - and leave the button dead once it wrapped negative.
#define DP_BUTTON_POLL_MS        20
#define DP_BUTTON_DEBOUNCE_TICKS 3

static esp_err_t configure_pins(void)
{
    // Separate gpio_config() calls: the two pins need different pull
    // configuration (GPIO10's internal pull-up vs. GPIO6's externally driven,
    // no-pull input), and one call applies its pull settings to every pin in
    // its bitmask.
    const gpio_config_t button_io = {
        .pin_bit_mask = (1ULL << DP_PIN_BUTTON),
        .mode         = GPIO_MODE_INPUT,
        .pull_up_en   = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };
    esp_err_t err = gpio_config(&button_io);
    if (err != ESP_OK) {
        return err;
    }

    const gpio_config_t toggle_io = {
        .pin_bit_mask = (1ULL << DP_PIN_TOGGLE_IN),
        .mode         = GPIO_MODE_INPUT,
        .pull_up_en   = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };
    return gpio_config(&toggle_io);
}

static void toggle_mains(const char *source)
{
    const bool on = dp_relay_get(DP_OUTPUT_MAINS);
    esp_err_t err = dp_relay_set(DP_OUTPUT_MAINS, !on);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "%s: mains toggle failed: %s", source, esp_err_to_name(err));
    }
}

static void poll_task(void *arg)
{
    (void)arg;

    int button_stable = gpio_get_level(DP_PIN_BUTTON);
    int button_candidate = button_stable;
    int button_ticks = 0;

    int toggle_stable = gpio_get_level(DP_PIN_TOGGLE_IN);
    int toggle_candidate = toggle_stable;
    int toggle_ticks = 0;

    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(DP_BUTTON_POLL_MS));

        const int button_now = gpio_get_level(DP_PIN_BUTTON);
        if (button_now != button_candidate) {
            button_candidate = button_now;
            button_ticks = 0;
        } else if (button_stable != button_candidate && ++button_ticks >= DP_BUTTON_DEBOUNCE_TICKS) {
            button_stable = button_candidate;
            if (button_stable == 0) {   // active low: press is the falling edge
                toggle_mains("button");
            }
        }

        const int toggle_now = gpio_get_level(DP_PIN_TOGGLE_IN);
        if (toggle_now != toggle_candidate) {
            toggle_candidate = toggle_now;
            toggle_ticks = 0;
        } else if (toggle_stable != toggle_candidate && ++toggle_ticks >= DP_BUTTON_DEBOUNCE_TICKS) {
            toggle_stable = toggle_candidate;
            // Logged, not acted on. GPIO6 has no pull resistor - matching
            // stock, since it's meant to be externally driven - and its
            // physical identity is still unconfirmed (docs/ROADMAP.md Phase
            // 0). A floating, unconnected pin drifts and produces exactly
            // this kind of "stable" transition on its own; a bench session
            // on 2026-09-14 caught mains switched on with nobody having
            // touched anything, most likely from this. Wiring an unconfirmed,
            // possibly-floating input to live mains power isn't acceptable -
            // re-enable toggle_mains() here once GPIO6 is confirmed to be a
            // real, deliberately-driven control.
            ESP_LOGI(TAG, "toggle input transition seen (level=%d) - not acted on, "
                          "GPIO6 identity unconfirmed", toggle_stable);
        }
    }
}

esp_err_t dp_button_init(void)
{
    esp_err_t err = configure_pins();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "gpio_config failed: %s", esp_err_to_name(err));
        return err;
    }

    BaseType_t ok = xTaskCreate(poll_task, "dp_button", 2048, NULL,
                                tskIDLE_PRIORITY + 1, NULL);
    if (ok != pdPASS) {
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(TAG, "button(GPIO%d) + toggle(GPIO%d) polling started",
             DP_PIN_BUTTON, DP_PIN_TOGGLE_IN);
    return ESP_OK;
}
