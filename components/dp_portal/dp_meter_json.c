// SPDX-License-Identifier: MIT
#include "dp_meter_json.h"
cJSON *dp_meter_json(const dp_meter_snapshot_t *sample)
{
    if (!sample) return cJSON_CreateNull();
    cJSON *meter = cJSON_CreateObject();
    if (!meter) return NULL;
    dp_meter_values_t v = dp_meter_values(sample);
    cJSON_AddStringToObject(meter, "status", dp_meter_status_name(sample->status));
    cJSON *reasons = cJSON_AddObjectToObject(meter, "reasons");
    const char *names[] = {"voltage", "current", "power", "frequency"};
    const char *why[] = {v.voltage_reason, v.current_reason, v.power_reason, v.frequency_reason};
    const float values[] = {v.voltage, v.current, v.power, v.frequency};
    for (size_t i = 0; i < 4; ++i) {
        if (why[i]) {
            cJSON_AddNullToObject(meter, names[i]);
            cJSON_AddStringToObject(reasons, names[i], why[i]);
        } else {
            cJSON_AddNumberToObject(meter, names[i], values[i]);
        }
    }
    cJSON_AddNullToObject(meter, "energy");
    cJSON_AddStringToObject(reasons, "energy", "not_implemented");
    if (sample->status == DP_METER_FRESH) {
        cJSON *raw = cJSON_AddObjectToObject(meter, "raw");
        cJSON_AddNumberToObject(raw, "syscon", sample->syscon);
        cJSON_AddNumberToObject(raw, "emucon", sample->emucon);
        cJSON_AddNumberToObject(raw, "hfconst", sample->hfconst);
        cJSON_AddNumberToObject(raw, "emucon2", sample->emucon2);
        cJSON_AddNumberToObject(raw, "voltage", sample->voltage_raw);
        cJSON_AddNumberToObject(raw, "current", sample->current_raw);
        cJSON_AddNumberToObject(raw, "power", sample->power_raw);
        cJSON_AddNumberToObject(raw, "voltage_coef", sample->voltage_coef);
        cJSON_AddNumberToObject(raw, "current_coef", sample->current_coef);
        cJSON_AddNumberToObject(raw, "power_coef", sample->power_coef);
        cJSON_AddNumberToObject(raw, "frequency", sample->frequency_raw);
    }
    return meter;
}
