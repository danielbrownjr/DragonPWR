// SPDX-License-Identifier: MIT
#pragma once
#include "cJSON.h"
#include "dp_meter_protocol.h"
cJSON *dp_meter_json(const dp_meter_snapshot_t *sample);
