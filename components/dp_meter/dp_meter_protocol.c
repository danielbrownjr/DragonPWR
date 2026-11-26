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

const char *dp_meter_status_name(dp_meter_status_t status)
{
    switch (status) {
    case DP_METER_FRESH: return "fresh";
    case DP_METER_INVALID: return "invalid";
    case DP_METER_STALE: return "stale";
    default: return "unavailable";
    }
}

dp_meter_status_t dp_meter_freshness(dp_meter_status_t status, int64_t sampled_us,
                                     int64_t now_us)
{
    if (status == DP_METER_FRESH &&
        (sampled_us < 0 || now_us < sampled_us || now_us - sampled_us > 3000000))
        return DP_METER_STALE;
    return status;
}

static const char *rms_reason(uint32_t syscon, uint32_t enable,
                              uint32_t mask, uint32_t gain, uint32_t coef)
{
    if (!(syscon & enable)) return "adc_disabled";
    if ((syscon & mask) != gain) return "unsupported_gain";
    if (!coef) return "zero_coefficient";
    if (coef == 0xffffu) return "uncalibrated_coefficient";
    return NULL;
}

static float rms(uint32_t raw)
{
    return (raw & 0x800000u) ? 0.0f : (float)raw;
}

dp_meter_values_t dp_meter_values(const dp_meter_snapshot_t *s)
{
    dp_meter_values_t v = {0};
    if (s->status != DP_METER_FRESH) {
        v.voltage_reason = v.current_reason = v.power_reason =
            v.frequency_reason = dp_meter_status_name(s->status);
        return v;
    }
    // Recovered BTT board factors, not reference-meter calibrated.
    const float k2 = 0.51f;
    v.voltage_reason = rms_reason(s->syscon, 1u << 11, 0x38u, 0,
                                  s->voltage_coef);
    v.current_reason = rms_reason(s->syscon, 1u << 9, 4u, 4u,
                                  s->current_coef);
    const char *common = !(s->emucon2 & 1u) ? "invalid_reference" :
                         (s->emucon & (1u << 9)) ? "dc_mode" : NULL;
    if (common) {
        v.voltage_reason = v.current_reason = v.power_reason =
            v.frequency_reason = common;
        return v;
    }
    if (!v.voltage_reason && (s->emucon & (1u << 4)))
        v.voltage_reason = "hpf_disabled";
    if (!v.current_reason && (s->emucon & (1u << 5)))
        v.current_reason = "hpf_disabled";
    if (!v.voltage_reason)
        v.voltage = rms(s->voltage_raw) * s->voltage_coef / 4194304.0f / k2 / 100.0f;
    if (!v.current_reason) {
        v.current = rms(s->current_raw) * s->current_coef / 8388608.0f / 1000.0f;
        if (v.current < 0.01f) v.current = 0;
    }
    // Require both ADC/gain paths and PARUN conservatively; do not infer
    // power availability from the raw register alone.
    v.power_reason = v.voltage_reason ? v.voltage_reason : v.current_reason;
    if (!v.power_reason && !(s->emucon & 1u)) v.power_reason = "parun_disabled";
    if (!v.power_reason && !s->power_coef) v.power_reason = "zero_coefficient";
    if (!v.power_reason && s->power_coef == 0xffffu) v.power_reason = "uncalibrated_coefficient";
    if (!v.power_reason) {
        // Explicit sign extension without implementation-defined unsigned cast.
        int64_t signed_raw = (int64_t)s->power_raw;
        if (s->power_raw & 0x80000000u) signed_raw -= INT64_C(4294967296);
        v.power = (float)signed_raw * s->power_coef / 2147483648.0f / k2;
        if (v.current == 0) v.power = 0;
    }
    if ((s->emucon2 & 0x24u) != 0x24u)
        v.frequency_reason = "frequency_disabled";
    else if (!s->frequency_raw)
        v.frequency_reason = "no_frequency";
    else {
        v.frequency = 3579545.0f / (8.0f * s->frequency_raw);
        if (v.frequency < 45.0f || v.frequency > 65.0f)
            v.frequency_reason = "frequency_out_of_range";
    }
    // Reset defaults disable frequency acquisition. Permit qualified RMS
    // without frequency only when configuration explicitly disables it.
    // An enabled but invalid frequency still suppresses scaled values.
    const char *gate = v.voltage_reason ? "voltage_unqualified" :
                       v.voltage < 10.0f ? "voltage_below_10v" :
                       (s->emucon2 & 0x24u) == 0x24u && v.frequency_reason ?
                           "frequency_unqualified" : NULL;
    if (gate) {
        if (!v.voltage_reason) v.voltage_reason = gate;
        if (!v.current_reason) v.current_reason = gate;
        if (!v.power_reason) v.power_reason = gate;
    }
    if (!v.power_reason && v.frequency_reason)
        v.power_reason = "frequency_unqualified";
    return v;
}
