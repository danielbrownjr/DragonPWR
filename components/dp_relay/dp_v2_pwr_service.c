// SPDX-License-Identifier: MIT
// No V2 GPIO configuration or driving occurs in this orchestration layer.
#include "sdkconfig.h"
#ifndef CONFIG_IDF_TARGET_ESP32C3
#error "V2 PWR service must only compile for ESP32-C3"
#endif
#include "dp_v2_pwr_service.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include <string.h>

#define DP_V2_PWR_TICK_MS 10
// Bounded acquisition. Extra tick prevents a zero-tick timeout on coarse RTOS ticks.
#define DP_V2_PWR_LOCK_WAIT_TICKS (pdMS_TO_TICKS(50) + 1U)
static const char *TAG = "dp_v2_pwr_service";
static SemaphoreHandle_t s_lock;
static dp_v2_pwr_model_t s_model;
static bool s_started;
static bool s_lab_active;

static bool lock_service(void)
{
    return s_lock && xSemaphoreTake(s_lock, DP_V2_PWR_LOCK_WAIT_TICKS) == pdTRUE;
}

static uint64_t clock_ms(void)
{
    return (uint64_t)esp_timer_get_time() / 1000U;
}

static void run(void *unused)
{
    (void)unused;
    bool lock_wait_reported = false;
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(DP_V2_PWR_TICK_MS) + 1);
        if (lock_service()) {
            lock_wait_reported = false;
            const bool was_retry = s_model.phase == DP_V2_PWR_FAULT &&
                s_model.deadline_off_retries_remaining > 0;
            const dp_v2_pwr_result_t result = dp_v2_pwr_model_poll(&s_model, clock_ms());
            if (result != DP_V2_PWR_OK) {
                ESP_LOGE(TAG, "deadline OFF request failed: %d; software state is not physical AC feedback", (int)result);
                if (was_retry && s_model.phase == DP_V2_PWR_FAULT &&
                    s_model.deadline_off_retries_remaining == 0) {
                    ESP_LOGE(TAG, "deadline OFF retry budget exhausted; FAULT remains latched; physical AC state UNKNOWN");
                }
            }
            xSemaphoreGive(s_lock);
        } else if (!lock_wait_reported) {
            // The monitor must not wait indefinitely behind a driver callback.
            // A skipped poll is NOT a successful OFF or a bounded release time.
            ESP_LOGE(TAG, "PWR scheduler lock unavailable; deadline safety unverified");
            lock_wait_reported = true;
        }
    }
}

esp_err_t dp_v2_pwr_service_start(void)
{
    if (s_started) return ESP_ERR_INVALID_STATE;
    dp_v2_pwr_model_init(&s_model);
    s_lab_active = false;
    s_lock = xSemaphoreCreateMutex();
    if (!s_lock) return ESP_ERR_NO_MEM;
    // No callback is installed, so ON cannot be accepted.
    if (xTaskCreate(run, "dp_v2_pwr", 3072, NULL, 5, NULL) != pdPASS) {
        vSemaphoreDelete(s_lock);
        s_lock = NULL;
        return ESP_ERR_NO_MEM;
    }
    s_started = true;
    ESP_LOGI(TAG, "V2 power scheduler started disabled; no output driver installed");
    return ESP_OK;
}

dp_v2_pwr_result_t dp_v2_pwr_service_set(bool on, uint32_t duration_ms)
{
    if (!s_started || !s_lock) return DP_V2_PWR_UNAVAILABLE;
    if (!lock_service()) return DP_V2_PWR_UNAVAILABLE;
    dp_v2_pwr_result_t result = s_lab_active ? DP_V2_PWR_UNAVAILABLE :
        dp_v2_pwr_model_set(&s_model, on, clock_ms(), duration_ms);
    xSemaphoreGive(s_lock);
    return result;
}

dp_v2_pwr_result_t dp_v2_pwr_service_snapshot(dp_v2_pwr_model_t *out)
{
    if (!out) return DP_V2_PWR_INVALID;
    if (!s_started || !s_lock) return DP_V2_PWR_UNAVAILABLE;
    if (!lock_service()) return DP_V2_PWR_UNAVAILABLE;
    *out = s_model;
    // Never expose a driver callback or private context in a snapshot.
    out->drive = NULL;
    out->context = NULL;
    xSemaphoreGive(s_lock);
    return DP_V2_PWR_OK;
}

bool dp_v2_pwr_service_lab_begin(void)
{
    if (!s_started || !s_lock) return false;
    if (!lock_service()) return false;
    const bool permitted = !s_lab_active && !s_model.enabled &&
        s_model.phase == DP_V2_PWR_OFF;
    if (permitted) s_lab_active = true;
    xSemaphoreGive(s_lock);
    return permitted;
}

void dp_v2_pwr_service_lab_end(void)
{
    if (!s_started || !s_lock) return;
    if (lock_service()) {
        s_lab_active = false;
        xSemaphoreGive(s_lock);
    } else {
        // Do not announce release of ownership if arbitration is unavailable.
        ESP_LOGE(TAG, "GPIO7 lab lease release unavailable; lease retained");
    }
}

bool dp_v2_pwr_service_lab_active(void)
{
    // Unknown/unavailable is conservatively treated as active for callers.
    if (!s_started || !s_lock) return true;
    if (!lock_service()) return true;
    const bool active = s_lab_active;
    xSemaphoreGive(s_lock);
    return active;
}

bool dp_v2_pwr_service_maintenance_allowed(void)
{
    if (!s_started || !s_lock) return false;
    if (!lock_service()) return false;
    const bool allowed = !s_lab_active && !s_model.enabled &&
        s_model.phase == DP_V2_PWR_OFF;
    xSemaphoreGive(s_lock);
    return allowed;
}
