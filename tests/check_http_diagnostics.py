"""Compile and exercise the production HTTP session callbacks with host stubs.

This test extracts the source functions, rather than reimplementing their logic.
Native ESP-IDF CI separately verifies their actual target signatures/linkage.
"""
from pathlib import Path
import re
import subprocess
import tempfile

source = Path("components/dp_portal/dp_portal.c").read_text()
begin = source.index("#define DP_HTTP_MAX_CLIENTS 4")
end = source.index("// Meter diagnostics are a bounded RAM snapshot", begin)
functions = source[begin:end]

# Preserve the existing four-client policy and diagnostic route registration.
assert "http.max_open_sockets = DP_HTTP_MAX_CLIENTS;" in source
assert "http.lru_purge_enable = true;" in source
assert "http.open_fn = http_session_open;" in source
assert "http.close_fn = http_session_close;" in source
assert re.search(r'\.uri\s*=\s*"/api/v2/diag/http".*'
                 r'\.handler\s*=\s*http_diag_get', source)
assert "memset(&s_http_sessions, 0, sizeof(s_http_sessions));" in source

harness = r"""
#include <assert.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
typedef int esp_err_t;
typedef void *httpd_handle_t;
typedef struct { int unused; } httpd_req_t;
#define ESP_OK 0
#define HTTPD_500_INTERNAL_SERVER_ERROR 500
#define TAG "dp_portal"
#define MALLOC_CAP_8BIT 0
#define ESP_LOGD(tag, fmt, ...) ((void)0)

static int closed_fds[16], close_count;
static int test_close(int fd) {
    assert(close_count < 16);
    closed_fds[close_count++] = fd;
    return 0;
}
#define close test_close

static bool allow_auth = true;
static int error_count, body_count;
static char body[512];

static bool authorize(httpd_req_t *req, void *ctx) {
    (void)req; (void)ctx; return allow_auth;
}
static esp_err_t send_error(httpd_req_t *req, const char *status,
                            const char *message) {
    (void)req; (void)message;
    assert(strcmp(status, "403 Forbidden") == 0);
    error_count++;
    return -403;
}
static esp_err_t httpd_resp_send_err(httpd_req_t *req, int code, const char *message) {
    (void)req; (void)code; (void)message;
    return -500;
}
static esp_err_t httpd_resp_set_type(httpd_req_t *req, const char *type) {
    (void)req; assert(strcmp(type, "application/json") == 0); return ESP_OK;
}
static esp_err_t httpd_resp_set_hdr(httpd_req_t *req, const char *key,
                                    const char *value) {
    (void)req;
    assert(strcmp(key, "Cache-Control") == 0);
    assert(strcmp(value, "no-store") == 0);
    return ESP_OK;
}
static esp_err_t httpd_resp_sendstr(httpd_req_t *req, const char *str) {
    (void)req; body_count++;
    assert(strlen(str) < sizeof(body));
    strcpy(body, str);
    return ESP_OK;
}
static size_t heap_caps_get_free_size(int caps) {
    (void)caps; return 150000;
}
static size_t heap_caps_get_minimum_free_size(int caps) {
    (void)caps; return 120000;
}
static size_t heap_caps_get_largest_free_block(int caps) {
    (void)caps; return 90000;
}
"""

tests = r"""
int main(void) {
    httpd_req_t req = {0};

    // The request must not bypass the authentication gate.
    allow_auth = false;
    assert(http_diag_get(&req) == -403);
    assert(error_count == 1);
    assert(body_count == 0);

    allow_auth = true;
    for (int i = 0; i < 4; i++)
        assert(http_session_open(NULL, 100 + i) == ESP_OK);
    assert(s_http_sessions.active == 4);
    assert(s_http_sessions.peak == 4);
    assert(s_http_sessions.opened_at_limit == 1);

    // An LRU-style close happens before the replacement connection opens.
    http_session_close(NULL, 100);
    assert(s_http_sessions.active == 3);
    assert(closed_fds[0] == 100);
    assert(http_session_open(NULL, 104) == ESP_OK);
    assert(s_http_sessions.active == 4);
    assert(s_http_sessions.opened_at_limit == 2);
    assert(s_http_sessions.closed_at_limit == 1);

    assert(http_diag_get(&req) == ESP_OK);
    assert(body_count == 1);
    assert(strstr(body, "\"active\":4") != NULL);
    assert(strstr(body, "\"peak\":4") != NULL);
    assert(strstr(body, "\"opened\":5") != NULL);
    assert(strstr(body, "\"closed\":1") != NULL);
    assert(strstr(body, "\"limit\":4") != NULL);
    assert(strstr(body, "\"min_free\":120000") != NULL);

    http_session_close(NULL, 101);
    http_session_close(NULL, 102);
    http_session_close(NULL, 103);
    http_session_close(NULL, 104);
    assert(s_http_sessions.active == 0);
    assert(s_http_sessions.opened == 5);
    assert(s_http_sessions.closed == 5);
    assert(close_count == 5);

    // The callback can be reached for an invalid/already-closed descriptor.
    http_session_close(NULL, -1);
    assert(s_http_sessions.active == 0);
    assert(close_count == 6);
    assert(closed_fds[5] == -1);
    puts("HTTP diagnostics callback and auth lifecycle: PASS");
    return 0;
}
"""

with tempfile.TemporaryDirectory(prefix="dp-http-diag-") as tmp:
    src = Path(tmp) / "http_diag_test.c"
    exe = Path(tmp) / "http_diag_test"
    src.write_text(harness + "\n" + functions + "\n" + tests)
    subprocess.run(
        ["cc", "-std=c11", "-Wall", "-Wextra", "-Werror",
         "-Wno-unused-parameter", str(src), "-o", str(exe)],
        check=True,
    )
    subprocess.run([str(exe)], check=True)
