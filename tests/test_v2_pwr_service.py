#!/usr/bin/env python3
"""Compile the real V2 service and model with deterministic host RTOS mocks."""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
headers = {
    "sdkconfig.h": "#define CONFIG_IDF_TARGET_ESP32C3 1\n",
    "esp_err.h": """#pragma once
typedef int esp_err_t;
#define ESP_OK 0
#define ESP_ERR_INVALID_STATE 0x103
#define ESP_ERR_NO_MEM 0x101
""",
    "esp_timer.h": """#pragma once
#include <stdint.h>
int64_t esp_timer_get_time(void);
""",
    "esp_log.h": """#pragma once
#define ESP_LOGE(tag, ...) ((void)(tag), ++error_logs)
#define ESP_LOGI(tag, ...) ((void)(tag))
""",
    "freertos/FreeRTOS.h": """#pragma once
#define pdTRUE 1
#define pdPASS 1
#define pdMS_TO_TICKS(ms) ((ms)/10)
#define portMAX_DELAY 0xffffffffU
""",
    "freertos/semphr.h": """#pragma once
typedef void *SemaphoreHandle_t;
SemaphoreHandle_t xSemaphoreCreateMutex(void);
int xSemaphoreTake(SemaphoreHandle_t lock, unsigned ticks);
int xSemaphoreGive(SemaphoreHandle_t lock);
void vSemaphoreDelete(SemaphoreHandle_t lock);
""",
    "freertos/task.h": """#pragma once
int xTaskCreate(void (*task)(void *), const char *name, unsigned stack,
                void *arg, unsigned priority, void *handle);
void vTaskDelay(unsigned ticks);
""",
}
test_c = r"""
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <setjmp.h>
#include <stdio.h>
static int error_logs;
#include "dp_v2_pwr_service.c"

static int deny_lock, lock_calls, give_calls, last_wait_ticks;
static int64_t fake_time_us;
static int delay_calls, stop_after;
static int high_calls, low_calls;
static jmp_buf exit_run;
#if defined(CONFIG_DP_V2_GPIO7_EXPERIMENT)
static int lab_high, lab_low;
static bool lab_fail_off;
bool dp_v2_gpio7_drive(bool high, void *context) {
    (void)context;
    if (high) { ++lab_high; return true; }
    ++lab_low;
    return !lab_fail_off;
}
#endif

SemaphoreHandle_t xSemaphoreCreateMutex(void) { return (void *)1; }
int xSemaphoreTake(SemaphoreHandle_t lock, unsigned ticks) {
    assert(lock == (void *)1);
    ++lock_calls;
    last_wait_ticks = (int)ticks;
    assert(ticks > 0 && ticks != portMAX_DELAY);
    return deny_lock ? 0 : pdTRUE;
}
int xSemaphoreGive(SemaphoreHandle_t lock) {
    assert(lock == (void *)1);
    ++give_calls;
    return pdTRUE;
}
void vSemaphoreDelete(SemaphoreHandle_t lock) { assert(lock == (void *)1); }
int xTaskCreate(void (*task)(void *), const char *name, unsigned stack,
                void *arg, unsigned priority, void *handle) {
    (void)task; (void)name; (void)stack; (void)arg; (void)priority; (void)handle;
    return pdPASS;
}
int64_t esp_timer_get_time(void) { return fake_time_us; }
void vTaskDelay(unsigned ticks) {
    assert(ticks > 0);
    if (++delay_calls > stop_after) longjmp(exit_run, 1);
}
static void run_polls(int count) {
    delay_calls = 0;
    stop_after = count;
    if (setjmp(exit_run) == 0) run(NULL);
    assert(delay_calls == count + 1);
}
static bool drive(bool high, void *context) {
    (void)context;
    if (high) ++high_calls; else ++low_calls;
    return true;
}

int main(void) {
    dp_v2_pwr_model_t snapshot = {0};
    // Unstarted service always fails closed.
    assert(dp_v2_pwr_service_set(true, 100) == DP_V2_PWR_UNAVAILABLE);
    assert(dp_v2_pwr_service_snapshot(&snapshot) == DP_V2_PWR_UNAVAILABLE);
    assert(!dp_v2_pwr_service_lab_begin());
    assert(dp_v2_pwr_service_lab_active());
    assert(!dp_v2_pwr_service_maintenance_allowed());

    assert(dp_v2_pwr_service_start() == ESP_OK);
    assert(dp_v2_pwr_service_start() == ESP_ERR_INVALID_STATE);
    assert(dp_v2_pwr_service_maintenance_allowed());
    assert(dp_v2_pwr_service_lab_begin());
    assert(dp_v2_pwr_service_lab_active());
    assert(!dp_v2_pwr_service_maintenance_allowed());
    assert(dp_v2_pwr_service_set(true, 100) == DP_V2_PWR_UNAVAILABLE);

    deny_lock = 1;
    const int before_give = give_calls;
    assert(dp_v2_pwr_service_set(false, 0) == DP_V2_PWR_UNAVAILABLE);
    assert(dp_v2_pwr_service_snapshot(&snapshot) == DP_V2_PWR_UNAVAILABLE);
    assert(!dp_v2_pwr_service_lab_begin());
    assert(dp_v2_pwr_service_lab_active()); // unknown == active
    assert(!dp_v2_pwr_service_maintenance_allowed());
    dp_v2_pwr_service_lab_end(); // cannot silently release ownership
    assert(s_lab_active && give_calls == before_give);
    assert(last_wait_ticks == 6); // 50 ms plus one tick at mock 100 Hz

    const int before_scheduler_logs = error_logs;
    run_polls(2); // consecutive scheduler lock failures log only once
    assert(error_logs == before_scheduler_logs + 1);
    assert(s_lab_active);
    deny_lock = 0;
    dp_v2_pwr_service_lab_end();
    assert(!s_lab_active && !dp_v2_pwr_service_lab_active());
    assert(dp_v2_pwr_service_maintenance_allowed());
    assert(dp_v2_pwr_service_snapshot(&snapshot) == DP_V2_PWR_OK);
    assert(snapshot.drive == NULL && snapshot.context == NULL);

    // Host-only model injection: missed deadline poll must not assert OFF.
    assert(dp_v2_pwr_model_enable(&s_model, drive, NULL, 100) == DP_V2_PWR_OK);
    assert(dp_v2_pwr_model_set(&s_model, true, 100, 10) == DP_V2_PWR_OK);
    fake_time_us = 120000;
    deny_lock = 1;
    run_polls(1);
    assert(s_model.phase == DP_V2_PWR_ON && low_calls == 1);
    assert(!dp_v2_pwr_service_maintenance_allowed());
    deny_lock = 0;
    run_polls(1);
    assert(s_model.phase == DP_V2_PWR_OFF && low_calls == 2);
    assert(!dp_v2_pwr_service_maintenance_allowed()); // model enabled
    assert(dp_v2_pwr_model_disable(&s_model) == DP_V2_PWR_OK);
    assert(dp_v2_pwr_service_maintenance_allowed());
    assert(lock_calls > 0 && high_calls == 1);
#if defined(CONFIG_DP_V2_GPIO7_EXPERIMENT)
    assert(!s_model.enabled);
    assert(dp_v2_pwr_service_lab_command(true, 0) == DP_V2_PWR_INVALID);
    assert(dp_v2_pwr_service_lab_command(true, 300001) == DP_V2_PWR_INVALID);
    assert(dp_v2_pwr_service_lab_begin());
    assert(dp_v2_pwr_service_lab_command(true, 100) == DP_V2_PWR_UNAVAILABLE);
    dp_v2_pwr_service_lab_end();
    deny_lock = 1;
    assert(dp_v2_pwr_service_lab_command(true, 100) == DP_V2_PWR_UNAVAILABLE);
    deny_lock = 0;
    fake_time_us = 200000;
    assert(dp_v2_pwr_service_lab_command(true, 100) == DP_V2_PWR_OK);
    assert(s_model.phase == DP_V2_PWR_ON && s_model.enabled && lab_high == 1);
    assert(lab_low == 1);
    assert(!dp_v2_pwr_service_maintenance_allowed());
    assert(!dp_v2_pwr_service_lab_begin());
    assert(dp_v2_pwr_service_lab_command(true, 100) == DP_V2_PWR_INVALID);
    assert(dp_v2_pwr_service_lab_command(false, 0) == DP_V2_PWR_OK);
    assert(s_model.phase == DP_V2_PWR_OFF && !s_model.enabled);
    assert(dp_v2_pwr_service_maintenance_allowed());
    assert(dp_v2_pwr_service_lab_command(true, 100) == DP_V2_PWR_OK);
    fake_time_us = 400000;
    run_polls(1);
    assert(s_model.phase == DP_V2_PWR_OFF && s_model.enabled);
    assert(!dp_v2_pwr_service_maintenance_allowed());
    assert(dp_v2_pwr_service_lab_command(false, 0) == DP_V2_PWR_OK);
    assert(dp_v2_pwr_service_lab_command(true, 100) == DP_V2_PWR_OK);
    lab_fail_off = true;
    assert(dp_v2_pwr_service_lab_command(false, 0) == DP_V2_PWR_DRIVER_ERROR);
    assert(s_model.phase == DP_V2_PWR_FAULT && !s_model.enabled);
    assert(!dp_v2_pwr_service_maintenance_allowed());
    assert(dp_v2_pwr_service_lab_command(true, 100) == DP_V2_PWR_DRIVER_ERROR);
    lab_fail_off = false;
    assert(dp_v2_pwr_service_lab_command(false, 0) == DP_V2_PWR_OK);
    assert(s_model.phase == DP_V2_PWR_OFF);
    assert(dp_v2_pwr_service_maintenance_allowed());
#endif
    puts("dp_v2_pwr_service mutex contention: PASS");
    return 0;
}
"""

with tempfile.TemporaryDirectory(prefix="dp-v2-service-host-") as directory:
    tmp = Path(directory)
    for name, contents in headers.items():
        path = tmp / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(contents)
    (tmp / "test.c").write_text(test_c)
    binary = tmp / "service-test"
    subprocess.run([
        "cc", "-std=c11", "-Wall", "-Wextra", "-Werror",
        "-fsanitize=address,undefined", "-fno-omit-frame-pointer",
        "-I", str(tmp), "-I", str(root / "components/dp_relay"),
        "-I", str(root / "components/dp_relay/include"),
        str(tmp / "test.c"),
        str(root / "components/dp_relay/dp_v2_pwr_model.c"),
        "-o", str(binary),
    ], check=True)
    subprocess.run([str(binary)], check=True)
    lab_binary = tmp / "service-lab-test"
    subprocess.run([
        "cc", "-std=c11", "-Wall", "-Wextra", "-Werror",
        "-fsanitize=address,undefined", "-fno-omit-frame-pointer",
        "-DCONFIG_DP_V2_GPIO7_EXPERIMENT=1",
        "-I", str(tmp), "-I", str(root / "components/dp_relay"),
        "-I", str(root / "components/dp_relay/include"),
        str(tmp / "test.c"),
        str(root / "components/dp_relay/dp_v2_pwr_model.c"),
        "-o", str(lab_binary),
    ], check=True)
    subprocess.run([str(lab_binary)], check=True)

print("PASS: source-executing bounded service locks and fail-closed guards")
