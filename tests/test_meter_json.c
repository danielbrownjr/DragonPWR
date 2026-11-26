// SPDX-License-Identifier: MIT
#include "dp_meter_json.h"
#include <assert.h>
#include <string.h>
static cJSON *field(cJSON *obj, const char *key)
{
    cJSON *v = cJSON_GetObjectItemCaseSensitive(obj, key);
    assert(v);
    return v;
}
int main(void)
{
    cJSON *obj = dp_meter_json(NULL);
    assert(cJSON_IsNull(obj));
    cJSON_Delete(obj);
    dp_meter_snapshot_t s = {0};
    for (int state = DP_METER_UNAVAILABLE; state <= DP_METER_STALE; ++state) {
        s.status = (dp_meter_status_t)state;
        obj = dp_meter_json(&s);
        assert(strcmp(field(obj, "status")->valuestring, dp_meter_status_name(s.status)) == 0);
        assert(cJSON_IsNull(field(obj, "voltage")));
        assert(cJSON_IsNull(field(obj, "current")));
        assert(cJSON_IsNull(field(obj, "power")));
        assert(cJSON_IsNull(field(obj, "frequency")));
        assert(cJSON_IsNull(field(obj, "energy")));
        assert(!cJSON_GetObjectItemCaseSensitive(obj, "raw"));
        assert(cJSON_IsString(field(field(obj, "reasons"), "current")));
        cJSON_Delete(obj);
    }
    s = (dp_meter_snapshot_t){.status=DP_METER_FRESH, .syscon=0x0a04,
        .emucon2=1, .voltage_raw=4194304, .voltage_coef=6120,
        .current_coef=2000};
    obj = dp_meter_json(&s);
    assert(cJSON_IsNumber(field(obj, "voltage")));
    assert(cJSON_IsNumber(field(obj, "current")) && field(obj, "current")->valuedouble == 0);
    assert(cJSON_IsNull(field(obj, "power")));
    assert(cJSON_IsObject(field(obj, "raw")));
    char *wire = cJSON_PrintUnformatted(obj);
    assert(wire);
    cJSON *parsed = cJSON_Parse(wire);
    assert(parsed && cJSON_IsNull(field(parsed, "energy")));
    cJSON_free(wire);
    cJSON_Delete(parsed);
    cJSON_Delete(obj);
    return 0;
}
