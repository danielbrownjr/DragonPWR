"""Static contract checks for the bounded HTTP diagnostics path.

Native C2/C3 CI separately compiles the actual ESP-IDF callback signatures.
This guard prevents accidentally dropping socket close or broadening scope.
"""
from pathlib import Path
import re

src = Path("components/dp_portal/dp_portal.c").read_text()
def block(signature):
    start = src.index(signature)
    opening = src.index("{", start)
    depth = 0
    for i in range(opening, len(src)):
        if src[i] == "{":
            depth += 1
        elif src[i] == "}":
            depth -= 1
            if depth == 0:
                return src[opening:i + 1]
    raise AssertionError("unterminated: " + signature)

assert '#define DP_HTTP_MAX_CLIENTS 4' in src
assert 'http.max_open_sockets = DP_HTTP_MAX_CLIENTS;' in src
assert 'http.lru_purge_enable = true;' in src
assert 'http.open_fn = http_session_open;' in src
assert 'http.close_fn = http_session_close;' in src
opening = block('static esp_err_t http_session_open(')
closing = block('static void http_session_close(')
endpoint = block('static esp_err_t http_diag_get(')
assert 'return ESP_OK;' in opening
assert '++s_http_sessions.opened;' in opening
assert '++s_http_sessions.active;' in opening
assert '++s_http_sessions.closed;' in closing
assert '--s_http_sessions.active;' in closing
assert '(void)close(sockfd);' in closing, "a custom close hook must close the TCP fd"
assert 'if (!authorize(req, NULL))' in endpoint
assert 'httpd_resp_set_hdr(req, "Cache-Control", "no-store");' in endpoint
for field in ('active', 'peak', 'opened', 'closed', 'opened_at_limit',
              'closed_at_limit', 'limit', 'lru_enabled', 'free', 'min_free',
              'largest_block'):
    assert '\\\"' + field + '\\\"' in endpoint, field
assert re.search(r'\.uri\s*=\s*"/api/v2/diag/http".*'
                 r'\.handler\s*=\s*http_diag_get', src)
for callback in (opening, closing, endpoint):
    assert not re.search(r'\bdp_relay_|\bdp_button_|\bgpio_', callback)
print("HTTP session diagnostics contract: PASS")
