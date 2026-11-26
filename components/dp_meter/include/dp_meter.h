// SPDX-License-Identifier: MIT
#pragma once
#include "esp_err.h"
#include "dp_meter_protocol.h"
#include <stdint.h>
#include <stdbool.h>

esp_err_t dp_meter_start(void);
dp_meter_snapshot_t dp_meter_snapshot(void);
