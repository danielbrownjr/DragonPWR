// SPDX-License-Identifier: MIT
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
// Only explicitly allowlisted, non-destructive register reads.
bool dp_meter_request(uint8_t reg, size_t width, uint8_t out[2]);
bool dp_meter_decode(uint8_t reg, size_t width, const uint8_t *reply,
                     size_t length, uint32_t *value);

typedef enum {
    DP_METER_UNAVAILABLE = 0, DP_METER_INVALID, DP_METER_STALE, DP_METER_FRESH
} dp_meter_status_t;
typedef struct {
    dp_meter_status_t status;
    int64_t sampled_us;
    uint32_t syscon, emucon, hfconst, emucon2;
    uint32_t voltage_raw, current_raw, power_raw;
    uint32_t voltage_coef, current_coef, power_coef;
    uint32_t frequency_raw;
} dp_meter_snapshot_t;

typedef struct {
    const char *voltage_reason, *current_reason, *power_reason, *frequency_reason;
    float voltage, current, power, frequency;
} dp_meter_values_t;

// NULL reason means a valid scaled observation, including a genuine zero.
dp_meter_values_t dp_meter_values(const dp_meter_snapshot_t *s);
dp_meter_status_t dp_meter_freshness(dp_meter_status_t status, int64_t sampled_us,
                                     int64_t now_us);
const char *dp_meter_status_name(dp_meter_status_t status);

typedef bool (*dp_meter_read_fn)(void *ctx, uint8_t reg, size_t width, uint32_t *value);
void dp_meter_sweep(dp_meter_snapshot_t *s, dp_meter_read_fn read, void *ctx);
