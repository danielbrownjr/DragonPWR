#pragma once
// ESP32-C3 V2 software-only command model. NOT physical relay feedback.
// No GPIO access, ESP-IDF dependency, or automatic restore.
#include <stdbool.h>
#include <stdint.h>
typedef enum { DP_V2_PWR_OFF=0, DP_V2_PWR_ON, DP_V2_PWR_FAULT } dp_v2_pwr_phase_t;
typedef enum { DP_V2_PWR_OK=0, DP_V2_PWR_UNAVAILABLE, DP_V2_PWR_INVALID, DP_V2_PWR_DRIVER_ERROR } dp_v2_pwr_result_t;
typedef bool (*dp_v2_pwr_drive_fn)(bool high, void *context);
typedef struct {
    dp_v2_pwr_phase_t phase;
    bool enabled;
    uint64_t deadline_ms;
    uint32_t max_on_ms;
    dp_v2_pwr_drive_fn drive;
    void *context;
} dp_v2_pwr_model_t;
// Starts disabled/OFF. Does not touch the hardware.
void dp_v2_pwr_model_init(dp_v2_pwr_model_t *model);
// Separate reviewed driver required; successful OFF prerequisite for ON.
dp_v2_pwr_result_t dp_v2_pwr_model_enable(dp_v2_pwr_model_t *model, dp_v2_pwr_drive_fn drive, void *context, uint32_t max_on_ms);
dp_v2_pwr_result_t dp_v2_pwr_model_disable(dp_v2_pwr_model_t *model);
// Caller must serialize operations and invoke poll from an independent
// timer/task: without that, the deadline is NOT a hardware/software watchdog.
// now_ms must be monotonic with no wrap during the interval.
dp_v2_pwr_result_t dp_v2_pwr_model_set(dp_v2_pwr_model_t *model, bool on, uint64_t now_ms, uint32_t duration_ms);
dp_v2_pwr_result_t dp_v2_pwr_model_poll(dp_v2_pwr_model_t *model, uint64_t now_ms);
