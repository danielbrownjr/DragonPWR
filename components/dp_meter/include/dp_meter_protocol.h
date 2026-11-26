// SPDX-License-Identifier: MIT
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
// Only explicitly allowlisted, non-destructive register reads.
bool dp_meter_request(uint8_t reg, size_t width, uint8_t out[2]);
bool dp_meter_decode(uint8_t reg, size_t width, const uint8_t *reply,
                     size_t length, uint32_t *value);
