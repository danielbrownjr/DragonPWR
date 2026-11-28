"""Exercise the production meter diagnostic handler on host cJSON."""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
portal = (root / "components/dp_portal/dp_portal.c").read_text()
start = portal.index("static esp_err_t meter_diag_get(")
handler = portal[start:portal.index("\n}", start)+2]
assert "authorize(" not in handler
assert '.uri = "/api/v2/diag/meter", .method = HTTP_GET' in portal
source = r'''
#include <assert.h>
#include <string.h>
#include "cJSON.h"
#include "dp_meter.h"
typedef cJSON *esp_err_t;
typedef void httpd_req_t;
static const char *error;
#if defined(CONFIG_IDF_TARGET_ESP32C2)
static dp_meter_diag_t fixture;
dp_meter_diag_t dp_meter_diagnostics(void) { return fixture; }
static void httpd_resp_set_hdr(httpd_req_t *r, const char *k, const char *v)
{ (void)r; assert(!strcmp(k,"Cache-Control") && !strcmp(v,"no-store")); }
static esp_err_t send_json(httpd_req_t *r, cJSON *j) { (void)r; return j; }
#endif
static esp_err_t send_error(httpd_req_t *r, const char *s, const char *m)
{ (void)r; (void)m; error=s; return NULL; }
''' + handler + r'''
int main(void) {
#if defined(CONFIG_IDF_TARGET_ESP32C2)
#define F(o,k) cJSON_GetObjectItemCaseSensitive(o,k)
    cJSON *j = meter_diag_get(NULL);
    assert(j && !strcmp(F(F(j,"last_failure"),"reason")->valuestring,"none"));
    cJSON_Delete(j);
    fixture.now_us=200;
    fixture.frame_monitor_enabled=true;
    fixture.position_attempts[7]=3;
    fixture.position_timeouts[7]=1;
    fixture.failure_us=100;
    fixture.failed_read_index=8;
    fixture.failure_sweep.valid_mask=127;
    fixture.last_failure=(dp_meter_tx_diag_t){.result=DP_METER_TX_CHECKSUM,
        .reg=0x26,.width=3,.received=4,.receive_attempted=true,
        .checksum_available=true,.expected_checksum=46,.reply={1,2,3,47}};
    fixture.transaction_counts[DP_METER_TX_CHECKSUM]=1;
    j=meter_diag_get(NULL);
    assert(j && !error);
    assert(F(j,"now_us")->valueint==200);
    assert(cJSON_IsTrue(F(j,"frame_monitor_enabled")));
    assert(cJSON_GetArraySize(F(j,"positions"))==15);
    assert(F(cJSON_GetArrayItem(F(j,"positions"),7),"timeouts")->valueint==1);
    cJSON *f=F(j,"last_failure");
    assert(!strcmp(F(f,"reason")->valuestring,"checksum"));
    assert(F(f,"register")->valueint==38);
    assert(F(f,"read_index")->valueint==8);
    assert(F(f,"expected_checksum")->valueint==46);
    assert(cJSON_GetArrayItem(F(f,"reply"),3)->valueint==47);
    assert(F(F(j,"transactions"),"checksum")->valueint==1);
    assert(!F(f,"guard_result"));
    cJSON_Delete(j);
    fixture.failure_sweep.mismatch_mask=1;
    fixture.failure_sweep.valid_mask=0x7fff;
    fixture.failure_sweep.reread[0]=1;
    j=meter_diag_get(NULL);
    assert(j && !strcmp(F(F(j,"last_failure"),"reason")->valuestring,"config_mismatch"));
    assert(F(F(F(j,"last_failure"),"config_after"),"syscon")->valueint==1);
    cJSON_Delete(j);
#else
    assert(!meter_diag_get(NULL) && !strcmp(error,"404 Not Found"));
#endif
}
'''
flags = subprocess.check_output(["pkg-config","--cflags","--libs","libcjson"],text=True).split()
with tempfile.TemporaryDirectory() as tmp:
    p=Path(tmp)
    (p/"esp_err.h").write_text("typedef int dp_unused_esp_err_t;\n") # actual header needs esp_err_t
    # The host handler models esp_err_t as a JSON pointer; define before dp_meter.h.
    source=source.replace('#include "dp_meter.h"\ntypedef cJSON *esp_err_t;', 'typedef cJSON *esp_err_t;\n#include "dp_meter.h"')
    (p/"test.c").write_text(source)
    for target in ("ESP32C2","ESP32C3"):
        subprocess.run(["cc","-std=c11","-Wall","-Wextra","-Werror","-pedantic",
            "-DCONFIG_IDF_TARGET_"+target+"=1","-I",str(p),
            "-I",str(root/"components/dp_meter/include"),str(p/"test.c"),
            *flags,"-o",str(p/"test")],check=True)
        subprocess.run([str(p/"test")],check=True)
        print("PASS:",target)
