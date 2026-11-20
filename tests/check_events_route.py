"""Guard DragonPWR's explicit unsupported-SSE route against SPA fallback.

Source-level contract only: an on-device HTTP check is still required.
"""
from pathlib import Path
import re


def strip_c_comments(source: str) -> str:
    """Remove C comments without treating comment markers in literals as comments."""
    token = re.compile(r'"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'|//[^\n]*|/\*[\s\S]*?\*/')
    return token.sub(lambda m: " " if m.group().startswith(("//", "/*")) else m.group(), source)


def route_is_registered(source: str) -> bool:
    code = strip_c_comments(source)
    table = re.search(
        r"static\s+const\s+httpd_uri_t\s+ROUTES\s*\[\s*\]\s*=\s*\{(.*?)\};",
        code, flags=re.S,
    )
    if not table:
        return False
    entry = re.search(
        r"\{\s*\.uri\s*=\s*\"/api/v2/events\"\s*,"
        r"\s*\.method\s*=\s*HTTP_GET\s*,"
        r"\s*\.handler\s*=\s*unsupported_events_get\s*\}",
        table.group(1), flags=re.S,
    )
    return bool(entry and re.search(r"\.product_routes\s*=\s*ROUTES\s*[,}]", code))


def handler_is_404(source: str) -> bool:
    code = strip_c_comments(source)
    handler = re.search(
        r"static\s+esp_err_t\s+unsupported_events_get\s*"
        r"\(\s*httpd_req_t\s*\*\s*req\s*\)\s*\{([^{}]*)\}",
        code, flags=re.S,
    )
    return bool(handler and re.search(
        r"return\s+httpd_resp_send_err\s*\(\s*req\s*,\s*HTTPD_404_NOT_FOUND\s*,",
        handler.group(1),
    ))


def test_parser_regressions() -> None:
    route = '{ .uri = "/api/v2/events", .method = HTTP_GET, .handler = unsupported_events_get },'
    config = 'dc_portal_config_t config = { .product_routes = ROUTES, };'
    def source(table: str, extra: str = "") -> str:
        return f"static const httpd_uri_t ROUTES[] = {{ {table} }};\n{config}\n{extra}"
    assert route_is_registered(source(route)), "live entry must pass"
    assert not route_is_registered(source("// " + route)), "commented entry must fail"
    assert not route_is_registered(source("/* " + route + " */")), "block-commented entry must fail"
    assert not route_is_registered(source("", f"static const httpd_uri_t OTHER[] = {{ {route} }};")), "wrong table must fail"
    assert not route_is_registered(source("").replace(config, "")), "unwired table must fail"


test_parser_regressions()
firmware = Path(__file__).resolve().parents[1] / "components/dp_portal/dp_portal.c"
source = firmware.read_text(encoding="utf-8")
assert handler_is_404(source), "unsupported SSE handler must return HTTP 404"
assert route_is_registered(source), "GET /api/v2/events must appear in the registered ROUTES table"
print("DragonPWR unsupported SSE route contract and mutation fixtures: PASS")
