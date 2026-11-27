"""Execute the production info handler with host cJSON and ESP mocks."""
from pathlib import Path
import re
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
portal = (ROOT / "components/dp_portal/dp_portal.c").read_text()
defines = "\n".join(
    re.search(r"^#define " + name + r"\s+[^\n]+$", portal, re.M).group()
    for name in ("DP_PRODUCT", "DP_DISPLAY_NAME", "DP_API_VERSION")
)
start = portal.index("static esp_err_t info_get(")
handler = portal[start:portal.index("\n}", start) + 2]
source = r'''
#include <assert.h>
#include <string.h>
#include <stddef.h>
#include "cJSON.h"
typedef cJSON *esp_err_t;
typedef void httpd_req_t;
typedef struct { char version[32]; char project_name[32]; } esp_app_desc_t;
typedef struct { char label[16]; } esp_partition_t;
#define ESP_OK 0
static const char s_device_id[] = "test-device";
static const char s_boot_id[] = "test-boot";
static const esp_app_desc_t app = { .version = "fixture-release" };
static const esp_app_desc_t *esp_app_get_description(void) { return &app; }
static const esp_partition_t *esp_ota_get_next_update_partition(void *p)
{ (void)p; return NULL; }
static int esp_ota_get_partition_description(const esp_partition_t *p, esp_app_desc_t *d)
{ (void)p; (void)d; return 1; }
static esp_err_t send_json(httpd_req_t *req, cJSON *root) { (void)req; return root; }
static esp_err_t send_error(httpd_req_t *req, const char *status, const char *message)
{ (void)req; (void)status; (void)message; return NULL; }
''' + defines + "\n" + handler + r'''
int main(void) {
    cJSON *info = info_get(NULL);
    assert(info);
    char *serialized = cJSON_PrintUnformatted(info);
    assert(serialized);
    cJSON_Delete(info);
    info = cJSON_Parse(serialized);
    cJSON_free(serialized);
    assert(info);
    #define FIELD(o, k) cJSON_GetObjectItemCaseSensitive(o, k)
    cJSON *api = FIELD(info, "api_version");
    assert(cJSON_IsNumber(api) && api->valuedouble == 2.0);
    assert(strcmp(FIELD(info, "product")->valuestring, "dragonpwr") == 0);
    assert(strcmp(FIELD(info, "version")->valuestring, "fixture-release") == 0);
    assert(strcmp(FIELD(info, "firmware")->valuestring, "fixture-release") == 0);
    assert(FIELD(FIELD(info, "ui"), "schema")->valueint == 1);
    cJSON *caps = FIELD(info, "capabilities");
#if defined(CONFIG_IDF_TARGET_ESP32C3)
#if defined(CONFIG_DP_V2_GPIO7_EXPERIMENT)
    assert(cJSON_GetArraySize(caps) == 2);
    assert(strcmp(cJSON_GetArrayItem(caps, 0)->valuestring, "experimental_power") == 0);
#else
    assert(cJSON_GetArraySize(caps) == 1);
#endif
#else
    assert(cJSON_GetArraySize(caps) == 3);
    assert(strcmp(cJSON_GetArrayItem(caps, 0)->valuestring, "power_switch") == 0);
    assert(strcmp(cJSON_GetArrayItem(caps, 1)->valuestring, "usb_switch") == 0);
#endif
    assert(strcmp(cJSON_GetArrayItem(caps, cJSON_GetArraySize(caps)-1)->valuestring, "polling") == 0);
    cJSON_Delete(info);
}
'''
flags = subprocess.check_output(["pkg-config", "--cflags", "--libs", "libcjson"], text=True).split()
with tempfile.TemporaryDirectory(prefix="dragonpwr-info-") as tmp:
    path = Path(tmp)
    (path / "test.c").write_text(source)
    for profile in (["-DCONFIG_IDF_TARGET_ESP32C2=1"],
                    ["-DCONFIG_IDF_TARGET_ESP32C3=1"],
                    ["-DCONFIG_IDF_TARGET_ESP32C3=1", "-DCONFIG_DP_V2_GPIO7_EXPERIMENT=1"]):
        subprocess.run(["cc", "-std=c11", "-Wall", "-Wextra", "-Werror", "-pedantic",
                        *profile, str(path / "test.c"), *flags, "-o", str(path / "test")], check=True)
        subprocess.run([str(path / "test")], check=True)
        print("PASS:", " ".join(profile))
