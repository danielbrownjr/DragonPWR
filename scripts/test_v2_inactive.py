#!/usr/bin/env python3
"""Host test of the actual V2 drivers; no ESP-IDF or hardware required."""
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory(prefix="dragonpwr-v2-test-") as tmp:
    path = Path(tmp)
    (path / "esp_err.h").write_text("""#pragma once
typedef int esp_err_t;
#define ESP_OK 0
#define ESP_ERR_INVALID_ARG 0x102
#define ESP_ERR_NOT_SUPPORTED 0x106
""")
    (path / "esp_log.h").write_text("""#pragma once
#define ESP_LOGW(tag, ...) ((void)(tag))
""")
    (path / "test.c").write_text("""#include <assert.h>
#include <stddef.h>
#include "dp_relay.h"
#include "dp_button.h"
int main(void) {
    assert(dp_relay_init() == ESP_OK);
    assert(dp_button_init() == ESP_OK);
    for (int output = 0; output < DP_OUTPUT_COUNT; ++output) {
        assert(dp_relay_set(output, true) == ESP_ERR_NOT_SUPPORTED);
        assert(dp_relay_set(output, false) == ESP_ERR_NOT_SUPPORTED);
        assert(!dp_relay_get(output));
    }
    assert(dp_relay_set(-1, true) == ESP_ERR_INVALID_ARG);
    assert(dp_relay_set(DP_OUTPUT_COUNT, true) == ESP_ERR_INVALID_ARG);
    for (int policy = DP_RESTORE_OFF; policy <= DP_RESTORE_LAST; ++policy)
        assert(dp_relay_set_restore(policy) == ESP_ERR_NOT_SUPPORTED);
    assert(dp_relay_get_restore() == DP_RESTORE_OFF);
    assert(dp_restore_from_str("on", DP_RESTORE_INVALID) == DP_RESTORE_ON);
    assert(dp_restore_from_str(NULL, DP_RESTORE_INVALID) == DP_RESTORE_INVALID);
    assert(dp_restore_from_str("unknown", DP_RESTORE_INVALID) == DP_RESTORE_INVALID);
    assert(dp_relay_clear() == ESP_OK);
}
""")
    subprocess.run(["cc", "-std=c11", "-Wall", "-Wextra", "-Werror", "-I", tmp,
                    "-I", str(ROOT / "components/dp_relay/include"),
                    "-I", str(ROOT / "components/dp_button/include"),
                    str(ROOT / "components/dp_relay/dp_relay_v2_inactive.c"),
                    str(ROOT / "components/dp_button/dp_button_v2_inactive.c"),
                    str(path / "test.c"), "-o", str(path / "test")], check=True)
    subprocess.run([str(path / "test")], check=True)
print("PASS: V2 AC/USB commands and restore requests rejected; drivers link without GPIO/NVS APIs")
