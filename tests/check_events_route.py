"""Verify DragonPWR does not serve the dashboard for unsupported SSE requests.

The actual HTTP behavior needs device validation; this prevents route deletion
from silently reintroducing the shared portal's SPA wildcard fallback.
"""
from pathlib import Path
import re

source = Path("components/dp_portal/dp_portal.c").read_text()
assert re.search(r'static esp_err_t unsupported_events_get\(httpd_req_t \*req\)', source)
handler = source.split("static esp_err_t unsupported_events_get", 1)[1].split("\n}", 1)[0]
assert "HTTPD_404_NOT_FOUND" in handler and "httpd_resp_send_err" in handler
assert re.search(
    r'\.uri\s*=\s*"/api/v2/events"\s*,\s*\.method\s*=\s*HTTP_GET\s*,'
    r'\s*\.handler\s*=\s*unsupported_events_get', source
)
print("DragonPWR unsupported SSE route contract: PASS")
