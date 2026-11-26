// SPDX-License-Identifier: MIT
#include "dp_meter_protocol.h"

bool dp_meter_request(uint8_t reg, size_t width, uint8_t out[2])
{
    size_t expected;
    switch (reg) {
    case 0x00: case 0x01: case 0x02: case 0x13:
    case 0x23: case 0x70: case 0x72: case 0x73: expected = 2; break;
    case 0x24: case 0x26: expected = 3; break;
    case 0x2c: expected = 4; break;
    default: return false;
    }
    if (!out || width != expected) return false;
    out[0] = 0xa5;
    out[1] = reg;
    return true;
}

bool dp_meter_decode(uint8_t reg, size_t width, const uint8_t *reply,
                     size_t length, uint32_t *value)
{
    uint8_t req[2];
    if (!dp_meter_request(reg, width, req) || !reply || !value ||
        length != width + 1) return false;
    uint8_t sum = (uint8_t)(req[0] + req[1]);
    uint32_t raw = 0;
    for (size_t i = 0; i < width; ++i) {
        sum = (uint8_t)(sum + reply[i]);
        raw = (raw << 8) | reply[i];
    }
    if (reply[width] != (uint8_t)(sum ^ 0xffu)) return false;
    *value = raw;
    return true;
}
