// SPDX-License-Identifier: MIT
#pragma once
#include "esp_err.h"
#include <stdint.h>
#include <stdbool.h>
typedef struct {
    bool valid;
    int64_t sampled_us;
    uint32_t syscon, emucon, hfconst, emucon2;
    uint32_t voltage_raw, current_raw, power_raw;
    uint32_t voltage_coef, current_coef, power_coef;
    uint32_t frequency_raw;
} dp_meter_snapshot_t;
esp_err_t dp_meter_start(void);
dp_meter_snapshot_t dp_meter_snapshot(void);
