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
static const char *TAG = "dp_v2_pwr_service";
static SemaphoreHandle_t s_lock;
static dp_v2_pwr_model_t s_model;
static bool s_started;
static bool s_lab_active;

static uint64_t clock_ms(void)
{
    return (uint64_t)esp_timer_get_time() / 1000U;
}

static void run(void *unused)
{
    (void)unused;
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(DP_V2_PWR_TICK_MS) + 1);
        if (xSemaphoreTake(s_lock, portMAX_DELAY) == pdTRUE) {
            const dp_v2_pwr_result_t result = dp_v2_pwr_model_poll(&s_model, clock_ms());
            if (result != DP_V2_PWR_OK) {
                ESP_LOGE(TAG, "deadline OFF request failed: %d; software state is not physical AC feedback", (int)result);
            }
            xSemaphoreGive(s_lock);
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
    if (xSemaphoreTake(s_lock, portMAX_DELAY) != pdTRUE) return DP_V2_PWR_UNAVAILABLE;
    dp_v2_pwr_result_t result = s_lab_active ? DP_V2_PWR_UNAVAILABLE :
        dp_v2_pwr_model_set(&s_model, on, clock_ms(), duration_ms);
    xSemaphoreGive(s_lock);
    return result;
}

dp_v2_pwr_result_t dp_v2_pwr_service_snapshot(dp_v2_pwr_model_t *out)
{
    if (!out) return DP_V2_PWR_INVALID;
    if (!s_started || !s_lock) return DP_V2_PWR_UNAVAILABLE;
    if (xSemaphoreTake(s_lock, portMAX_DELAY) != pdTRUE) return DP_V2_PWR_UNAVAILABLE;
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
    if (xSemaphoreTake(s_lock, portMAX_DELAY) != pdTRUE) return false;
    const bool permitted = !s_lab_active && !s_model.enabled &&
        s_model.phase == DP_V2_PWR_OFF;
    if (permitted) s_lab_active = true;
    xSemaphoreGive(s_lock);
    return permitted;
}

void dp_v2_pwr_service_lab_end(void)
{
    if (!s_started || !s_lock) return;
    if (xSemaphoreTake(s_lock, portMAX_DELAY) == pdTRUE) {
        s_lab_active = false;
        xSemaphoreGive(s_lock);
    }
}

bool dp_v2_pwr_service_lab_active(void)
{
    // Unknown/unavailable is conservatively treated as active for callers.
    if (!s_started || !s_lock) return true;
    if (xSemaphoreTake(s_lock, portMAX_DELAY) != pdTRUE) return true;
    const bool active = s_lab_active;
    xSemaphoreGive(s_lock);
    return active;
}

bool dp_v2_pwr_service_maintenance_allowed(void)
{
    if (!s_started || !s_lock) return false;
    if (xSemaphoreTake(s_lock, portMAX_DELAY) != pdTRUE) return false;
    const bool allowed = !s_lab_active && !s_model.enabled &&
        s_model.phase == DP_V2_PWR_OFF;
    xSemaphoreGive(s_lock);
    return allowed;
}
