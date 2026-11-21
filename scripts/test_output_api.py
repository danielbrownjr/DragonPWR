#!/usr/bin/env python3
"""Exercise the portal's output JSON for both ESP-IDF 5.3.5 targets on the host."""
import os
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
JSON_DIR = Path(os.environ["IDF_PATH"]) / "components/json/cJSON"
PORTAL = (ROOT / "components/dp_portal/dp_portal.c").read_text()


def handler(name):
    start = PORTAL.index(f"static esp_err_t {name}(")
    return PORTAL[start:PORTAL.index("\n}", start) + 2]


start = PORTAL.index('    cJSON *caps = cJSON_AddArrayToObject(root, "capabilities");')
end = PORTAL.index('\n\n', start)
source = r'''
#include <assert.h>
#include <stdbool.h>
#include <string.h>
#include "cJSON.h"

typedef cJSON *esp_err_t;
typedef void httpd_req_t;
enum { DP_OUTPUT_MAINS, DP_OUTPUT_USB1, MALLOC_CAP_8BIT };
static esp_err_t send_json(httpd_req_t *req, cJSON *root) { (void)req; return root; }
#if !defined(CONFIG_IDF_TARGET_ESP32C3)
static bool dp_relay_get(int output) { return output == DP_OUTPUT_MAINS; }
static int dp_relay_get_restore(void) { return 0; }
static const char *dp_restore_to_str(int policy) { (void)policy; return "off"; }
#endif
static int heap_caps_get_free_size(int caps) { (void)caps; return 172924; }
static int heap_caps_get_minimum_free_size(int caps) { (void)caps; return 141724; }
static int heap_caps_get_largest_free_block(int caps) { (void)caps; return 114688; }
''' + handler("state_get") + "\n" + handler("stock_state_get") + r'''
#define member cJSON_GetObjectItemCaseSensitive
static cJSON *capabilities(cJSON *root)
{
''' + PORTAL[start:end] + r'''
    return caps;
}
int main(void)
{
    cJSON *info = cJSON_CreateObject();
    cJSON *caps = capabilities(info);
    cJSON *state = state_get(NULL);
    cJSON *outputs = member(state, "outputs");
    cJSON *stock = stock_state_get(NULL);
#if defined(CONFIG_IDF_TARGET_ESP32C3)
    assert(cJSON_GetArraySize(caps) == 1);
    assert(strcmp(cJSON_GetArrayItem(caps, 0)->valuestring, "polling") == 0);
    assert(cJSON_IsNull(member(outputs, "mains")));
    assert(cJSON_IsNull(member(outputs, "usb1")));
    assert(cJSON_IsNull(member(state, "restore")));
    assert(cJSON_IsNull(member(stock, "power_state")));
    assert(cJSON_IsNull(member(stock, "usb_state")));
#else
    assert(cJSON_GetArraySize(caps) == 3);
    assert(strcmp(cJSON_GetArrayItem(caps, 0)->valuestring, "power_switch") == 0);
    assert(strcmp(cJSON_GetArrayItem(caps, 1)->valuestring, "usb_switch") == 0);
    assert(strcmp(cJSON_GetArrayItem(caps, 2)->valuestring, "polling") == 0);
    assert(cJSON_IsTrue(member(outputs, "mains")));
    assert(cJSON_IsFalse(member(outputs, "usb1")));
    assert(strcmp(member(state, "restore")->valuestring, "off") == 0);
    assert(cJSON_IsNumber(member(stock, "power_state")));
    assert(member(stock, "power_state")->valueint == 1);
    assert(cJSON_IsNumber(member(stock, "usb_state")));
    assert(member(stock, "usb_state")->valueint == 0);
#endif
    assert(member(member(state, "heap"), "free")->valueint == 172924);
    cJSON_Delete(stock);
    cJSON_Delete(state);
    cJSON_Delete(info);
}
'''

with tempfile.TemporaryDirectory(prefix="dragonpwr-output-api-") as tmp:
    path = Path(tmp)
    (path / "test.c").write_text(source)
    for target in ("ESP32C2", "ESP32C3"):
        subprocess.run(
            ["cc", "-std=c11", "-Wall", "-Wextra", "-Werror", f"-DCONFIG_IDF_TARGET_{target}=1",
             "-I", str(JSON_DIR), str(path / "test.c"), str(JSON_DIR / "cJSON.c"),
             "-lm", "-o", str(path / "test")], check=True,
        )
        subprocess.run([str(path / "test")], check=True)
        print(f"PASS: {target} capability and output JSON contract")
