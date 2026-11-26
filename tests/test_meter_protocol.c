// SPDX-License-Identifier: MIT
#include "dp_meter_protocol.h"
#include <assert.h>
#include <math.h>
int main(void)
{
    uint8_t req[2];
    uint32_t value = 123;
    assert(dp_meter_request(0x26, 3, req));
    assert(req[0] == 0xa5 && req[1] == 0x26);
    const uint8_t good[] = {0x01, 0x02, 0x03, 0x2e};
    assert(dp_meter_decode(0x26, 3, good, sizeof(good), &value));
    assert(value == 0x010203);
    assert(!dp_meter_decode(0x26, 3, good, 3, &value));
    assert(!dp_meter_decode(0x24, 3, good, 4, &value));
    assert(value == 0x010203);
    assert(!dp_meter_request(0xea, 2, req));
    assert(!dp_meter_request(0x42, 2, req));
    assert(!dp_meter_request(0x28, 3, req));
    assert(!dp_meter_request(0x26, 4, req));
    assert(!dp_meter_request(0xa6, 3, req));
    assert(!dp_meter_request(0x26, 0, req));
    assert(!dp_meter_decode(0x26, 3, NULL, 4, &value));
    const uint8_t corrupt[] = {1, 2, 3, 0x2f};
    assert(!dp_meter_decode(0x26, 3, corrupt, 4, &value));
    assert(value == 0x010203);
    // Synthetic register vectors using the recovered BTT equations.
    dp_meter_snapshot_t s = {
        .status = DP_METER_FRESH, .syscon = 0x0a04, .emucon = 0x0181,
        .emucon2 = 0x046d, .voltage_raw = 4194304, .voltage_coef = 6120,
        .current_raw = 4194304, .current_coef = 2000,
        .power_raw = 0x40000000, .power_coef = 102,
        .frequency_raw = 7457
    };
    dp_meter_values_t v = dp_meter_values(&s);
    assert(!v.voltage_reason && fabsf(v.voltage - 120) < 0.001f);
    assert(!v.current_reason && fabsf(v.current - 1) < 0.001f);
    assert(!v.power_reason && fabsf(v.power - 100) < 0.001f);
    assert(!v.frequency_reason && fabsf(v.frequency - 60) < 0.01f);
    s.power_raw = 0xc0000000;
    v = dp_meter_values(&s);
    assert(!v.power_reason && fabsf(v.power + 100) < 0.001f);
    s.current_raw = 0x800000;
    v = dp_meter_values(&s);
    assert(!v.current_reason && v.current == 0 && v.power == 0);
    s.current_raw = 4194304;
    s.syscon ^= 1u << 9;
    v = dp_meter_values(&s);
    assert(v.current_reason && v.power_reason && !v.voltage_reason);
    s.syscon = 0x0a05;
    v = dp_meter_values(&s);
    assert(v.current_reason && v.power_reason);
    s.syscon = 0x0a04;
    s.power_coef = 0;
    assert(dp_meter_values(&s).power_reason);
    s.power_coef = 102;
    s.emucon = 0;
    assert(dp_meter_values(&s).power_reason);
    s.emucon = 0x0181;
    s.emucon2 = 0;
    v = dp_meter_values(&s);
    assert(v.frequency_reason && v.voltage_reason && v.current_reason && v.power_reason);
    s.emucon2 = 0x046d;
    s.frequency_raw = 0;
    assert(dp_meter_values(&s).frequency_reason);
    s.frequency_raw = 7457;
    s.status = DP_METER_INVALID;
    assert(dp_meter_values(&s).voltage_reason);
    s.status = DP_METER_FRESH;
    assert(!dp_meter_values(&s).voltage_reason);
    assert(dp_meter_freshness(DP_METER_FRESH, 100, 3000100) == DP_METER_FRESH);
    assert(dp_meter_freshness(DP_METER_FRESH, 100, 3000101) == DP_METER_STALE);
    assert(dp_meter_freshness(DP_METER_INVALID, 100, 3000101) == DP_METER_INVALID);
    assert(dp_meter_freshness(DP_METER_UNAVAILABLE, 0, 3000101) == DP_METER_UNAVAILABLE);
    s.status = DP_METER_STALE;
    assert(dp_meter_values(&s).power_reason);
    return 0;
}
