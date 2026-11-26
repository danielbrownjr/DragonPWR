// SPDX-License-Identifier: MIT
#include "dp_meter_json.h"
cJSON *dp_meter_json(const dp_meter_snapshot_t *sample)
{
    if (!sample) return cJSON_CreateNull();
    cJSON *meter = cJSON_CreateObject();
    if (!meter) return NULL;
    dp_meter_values_t v = dp_meter_values(sample);
    if (!cJSON_AddStringToObject(meter, "status", dp_meter_status_name(sample->status))) goto fail;
    cJSON *reasons = cJSON_AddObjectToObject(meter, "reasons");
    if (!reasons) goto fail;
    const char *names[] = {"voltage", "current", "power", "frequency"};
    const char *why[] = {v.voltage_reason, v.current_reason, v.power_reason, v.frequency_reason};
    const float values[] = {v.voltage, v.current, v.power, v.frequency};
    for (size_t i = 0; i < 4; ++i) {
        if (why[i]) {
            if (!cJSON_AddNullToObject(meter, names[i])) goto fail;
            if (!cJSON_AddStringToObject(reasons, names[i], why[i])) goto fail;
        } else {
            if (!cJSON_AddNumberToObject(meter, names[i], values[i])) goto fail;
        }
    }
    if (!cJSON_AddNullToObject(meter, "energy")) goto fail;
    if (!cJSON_AddStringToObject(reasons, "energy", "not_implemented")) goto fail;
    if (sample->status == DP_METER_FRESH) {
        cJSON *raw = cJSON_AddObjectToObject(meter, "raw");
        if (!raw) goto fail;
        if (!cJSON_AddNumberToObject(raw, "syscon", sample->syscon)) goto fail;
        if (!cJSON_AddNumberToObject(raw, "emucon", sample->emucon)) goto fail;
        if (!cJSON_AddNumberToObject(raw, "hfconst", sample->hfconst)) goto fail;
        if (!cJSON_AddNumberToObject(raw, "emucon2", sample->emucon2)) goto fail;
        if (!cJSON_AddNumberToObject(raw, "voltage", sample->voltage_raw)) goto fail;
        if (!cJSON_AddNumberToObject(raw, "current", sample->current_raw)) goto fail;
        if (!cJSON_AddNumberToObject(raw, "power", sample->power_raw)) goto fail;
        if (!cJSON_AddNumberToObject(raw, "voltage_coef", sample->voltage_coef)) goto fail;
        if (!cJSON_AddNumberToObject(raw, "current_coef", sample->current_coef)) goto fail;
        if (!cJSON_AddNumberToObject(raw, "power_coef", sample->power_coef)) goto fail;
        if (!cJSON_AddNumberToObject(raw, "frequency", sample->frequency_raw)) goto fail;
    }
    return meter;
fail:
    cJSON_Delete(meter);
    return NULL;
}
