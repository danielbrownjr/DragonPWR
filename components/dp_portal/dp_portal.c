// SPDX-License-Identifier: MIT
#include "dp_portal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cJSON.h"
#include "dc_evlog.h"
#include "dc_portal.h"
#include "dp_relay.h"
#include "esp_app_desc.h"
#include "esp_log.h"
#include "nvs.h"
#include "nvs_flash.h"

static const char *TAG = "dp_portal";

#define DP_PRODUCT      "dragonpwr"
#define DP_DISPLAY_NAME "DragonPWR"

// The family shares one NVS namespace, same as dp_relay and the stock firmware.
#define DP_NVS_NAMESPACE "app_nvs"

// ---------------------------------------------------------------- helpers

static esp_err_t send_json(httpd_req_t *req, cJSON *root)
{
    char *body = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!body) {
        httpd_resp_set_status(req, "500 Internal Server Error");
        return httpd_resp_sendstr(req, "{\"error\":\"out of memory\"}");
    }
    httpd_resp_set_type(req, "application/json");
    esp_err_t err = httpd_resp_sendstr(req, body);
    cJSON_free(body);
    return err;
}

static esp_err_t send_error(httpd_req_t *req, const char *status, const char *message)
{
    httpd_resp_set_status(req, status);
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "error", message);
    return send_json(req, root);
}

// Reads a request body into a caller-supplied buffer, NUL-terminated. Bodies
// here are tiny (a form pair or a two-key object); anything larger is refused
// rather than grown.
static int recv_body(httpd_req_t *req, char *buf, size_t size)
{
    if (req->content_len <= 0 || (size_t)req->content_len >= size) {
        return -1;
    }
    int total = 0;
    while (total < req->content_len) {
        int got = httpd_req_recv(req, buf + total, req->content_len - total);
        if (got <= 0) {
            return -1;
        }
        total += got;
    }
    buf[total] = '\0';
    return total;
}

// --------------------------------------------------------- control token
// Matches the family's db_tok scheme (see dc_ui's dcAuthHeaders/'web' sentinel
// and managed_components/dc_portal's /console page): the browser always sends
// a non-empty X-Dragon-Auth / X-DragonBreath-Auth header, defaulting to the
// 'web' CSRF sentinel when no real token is configured. A cross-origin <form>
// POST cannot set custom headers, so requiring *some* value here defeats CSRF
// even before a token is set; once one is set, only an exact match passes.
//
// The stock-compatible /set route cannot use the presence tier - HA-Panda-PWR
// and other clients written against the stock API send no header - so it has
// its own gate, stock_authorize(), below.

#define DP_NVS_TOKEN "ctrl_tok"
#define DP_TOKEN_MAX 64

// Printable ASCII with no spaces: the token travels in a header and httpd
// trims surrounding whitespace, so a token with spaces could never match
// itself - and clearing it needs a match, so it would lock the owner out.
static bool token_valid(const char *token)
{
    const size_t len = strlen(token);
    if (len == 0 || len > DP_TOKEN_MAX) {
        return false;
    }
    for (size_t i = 0; i < len; i++) {
        if (token[i] < 0x21 || token[i] > 0x7e) {
            return false;
        }
    }
    return true;
}

static esp_err_t token_store(const char *token)
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open(DP_NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        return err;
    }
    if (token[0] == '\0') {
        err = nvs_erase_key(handle, DP_NVS_TOKEN);
        if (err == ESP_ERR_NVS_NOT_FOUND) {
            err = ESP_OK;
        }
    } else {
        err = nvs_set_str(handle, DP_NVS_TOKEN, token);
    }
    if (err == ESP_OK) {
        err = nvs_commit(handle);
    }
    nvs_close(handle);
    return err;
}

// ESP_OK with out empty when no token is set. Any other failure - a read
// error, or a stored value token_store could not have written - is returned
// as an error so authorize() fails closed: treating it as "no token" would
// silently drop the lock the owner set.
static esp_err_t token_load(char *out, size_t out_size)
{
    out[0] = '\0';
    nvs_handle_t handle;
    esp_err_t err = nvs_open(DP_NVS_NAMESPACE, NVS_READONLY, &handle);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        return ESP_OK;   // namespace not created yet
    }
    if (err != ESP_OK) {
        return err;
    }
    size_t len = out_size;
    err = nvs_get_str(handle, DP_NVS_TOKEN, out, &len);
    nvs_close(handle);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        out[0] = '\0';
        return ESP_OK;
    }
    if (err == ESP_OK && !token_valid(out)) {
        err = ESP_ERR_INVALID_STATE;
    }
    if (err != ESP_OK) {
        out[0] = '\0';
    }
    return err;
}

// Compares every byte of the stored token whatever the input, so response
// time does not leak how long a matching prefix was.
static bool token_matches(const char *given, const char *stored)
{
    const size_t want_len = strlen(stored);
    const size_t given_len = strnlen(given, DP_TOKEN_MAX + 1);
    unsigned diff = want_len ^ given_len;
    for (size_t i = 0; i < want_len; i++) {
        const char g = i < given_len ? given[i] : 0;
        diff |= (unsigned char)(stored[i] ^ g);
    }
    return diff == 0;
}

// Loads the token for a gate. Returns false when it cannot be read, in which
// case nothing may pass. The way out is erasing NVS over serial (README).
static bool token_read(char *out, size_t out_size)
{
    const esp_err_t err = token_load(out, out_size);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "control token unreadable (%s); refusing commands",
                 esp_err_to_name(err));
        return false;
    }
    return true;
}

static bool auth_header(httpd_req_t *req, char *out, size_t out_size)
{
    return (httpd_req_get_hdr_value_str(req, "X-Dragon-Auth", out, out_size) == ESP_OK && out[0]) ||
           (httpd_req_get_hdr_value_str(req, "X-DragonBreath-Auth", out, out_size) == ESP_OK && out[0]);
}

static bool authorize(httpd_req_t *req, void *ctx)
{
    (void)ctx;
    char header[DP_TOKEN_MAX + 1] = {0};
    if (!auth_header(req, header, sizeof(header))) {
        return false;
    }
    char stored[DP_TOKEN_MAX + 1] = {0};
    if (!token_read(stored, sizeof(stored))) {
        return false;
    }
    if (stored[0] == '\0') {
        return true;   // presence-only tier: no token configured yet
    }
    return token_matches(header, stored);
}

// "http://host[:port]" -> "host[:port]", or NULL if it is not an http(s) origin.
static const char *origin_host(const char *origin)
{
    if (strncmp(origin, "http://", 7) == 0) {
        return origin + 7;
    }
    if (strncmp(origin, "https://", 8) == 0) {
        return origin + 8;
    }
    return NULL;
}

// Gate for the stock /set route. Stock clients send no auth header and cannot
// be taught to, so with no token configured the CSRF gate is an Origin check
// instead: a browser always sends Origin on a cross-origin POST, and the stock
// clients never do. Once a token is configured /set needs it like any other
// command - otherwise the token would protect everything except the route that
// switches mains. Stock clients stop working then, which the owner opted into.
static bool stock_authorize(httpd_req_t *req)
{
    if (authorize(req, NULL)) {
        return true;
    }
    char stored[DP_TOKEN_MAX + 1] = {0};
    if (!token_read(stored, sizeof(stored)) || stored[0] != '\0') {
        return false;
    }
    if (httpd_req_get_hdr_value_len(req, "Origin") == 0) {
        return true;
    }
    char origin[128];
    char host[64];
    if (httpd_req_get_hdr_value_str(req, "Origin", origin, sizeof(origin)) != ESP_OK ||
        httpd_req_get_hdr_value_str(req, "Host", host, sizeof(host)) != ESP_OK) {
        return false;
    }
    const char *from = origin_host(origin);
    return from && strcmp(from, host) == 0;
}

// ------------------------------------------------------------ product API

static esp_err_t info_get(httpd_req_t *req)
{
    const esp_app_desc_t *app = esp_app_get_description();
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "product", DP_PRODUCT);
    cJSON_AddStringToObject(root, "display_name", DP_DISPLAY_NAME);
    cJSON_AddStringToObject(root, "version", app ? app->version : "unknown");

    // The shared SPA gates optional screens on these. There is no dragonpwr
    // surface in dc_ui yet, so today this only selects the common setup and
    // recovery overlay - see docs/ROADMAP.md.
    cJSON *caps = cJSON_AddArrayToObject(root, "capabilities");
    cJSON_AddItemToArray(caps, cJSON_CreateString("power_switch"));
    cJSON_AddItemToArray(caps, cJSON_CreateString("usb_switch"));
    cJSON_AddItemToArray(caps, cJSON_CreateString("polling"));

    cJSON *ui = cJSON_AddObjectToObject(root, "ui");
    cJSON_AddNumberToObject(ui, "schema", 1);
    cJSON_AddStringToObject(ui, "product", DP_PRODUCT);
    cJSON_AddStringToObject(ui, "display_name", DP_DISPLAY_NAME);
    return send_json(req, root);
}

static esp_err_t state_get(httpd_req_t *req)
{
    cJSON *root = cJSON_CreateObject();
    cJSON *outputs = cJSON_AddObjectToObject(root, "outputs");
    cJSON_AddBoolToObject(outputs, "mains", dp_relay_get(DP_OUTPUT_MAINS));
    cJSON_AddBoolToObject(outputs, "usb1", dp_relay_get(DP_OUTPUT_USB1));
    cJSON_AddStringToObject(root, "restore",
                            dp_restore_to_str(dp_relay_get_restore()));
    // No meter and no printer source yet; both are declared null rather than
    // zeroed so a client can tell "not implemented" from "measured zero".
    cJSON_AddNullToObject(root, "meter");
    cJSON_AddNullToObject(root, "printer");
    return send_json(req, root);
}

static esp_err_t command_post(httpd_req_t *req)
{
    if (!authorize(req, NULL)) {
        return send_error(req, "403 Forbidden", "authorization required");
    }
    char body[128];
    if (recv_body(req, body, sizeof(body)) < 0) {
        return send_error(req, "400 Bad Request", "body required");
    }
    cJSON *root = cJSON_Parse(body);
    if (!root) {
        return send_error(req, "400 Bad Request", "invalid JSON");
    }
    const cJSON *output = cJSON_GetObjectItemCaseSensitive(root, "output");
    const cJSON *on = cJSON_GetObjectItemCaseSensitive(root, "on");
    if (!cJSON_IsString(output) || !cJSON_IsBool(on)) {
        cJSON_Delete(root);
        return send_error(req, "400 Bad Request", "output and on are required");
    }

    dp_output_t target;
    if (strcmp(output->valuestring, "mains") == 0) {
        target = DP_OUTPUT_MAINS;
    } else if (strcmp(output->valuestring, "usb1") == 0) {
        target = DP_OUTPUT_USB1;
    } else {
        cJSON_Delete(root);
        return send_error(req, "400 Bad Request", "unknown output");
    }
    const bool want = cJSON_IsTrue(on);
    cJSON_Delete(root);

    // TODO(phase 2): the mid-print interlock belongs here - refuse to switch
    // the mains output off while the bound printer reports printing, paused or
    // heating. Every power-off path must funnel through one gate.
    esp_err_t err = dp_relay_set(target, want);
    if (err != ESP_OK) {
        return send_error(req, "500 Internal Server Error", esp_err_to_name(err));
    }
    return state_get(req);
}

// ------------------------------------------- stock-compatible API surface
// Kept so integrations written against the stock firmware - notably
// juanillo62gm/HA-Panda-PWR - keep working across the firmware swap.

// Stock accepts a raw form body such as "usb=1&power=1", not JSON.
static bool form_flag(const char *body, const char *key, bool *out)
{
    const size_t key_len = strlen(key);
    for (const char *p = body; *p; p++) {
        if ((p == body || p[-1] == '&') &&
            strncmp(p, key, key_len) == 0 && p[key_len] == '=') {
            *out = p[key_len + 1] != '0';
            return true;
        }
    }
    return false;
}

static esp_err_t stock_set_post(httpd_req_t *req)
{
    if (!stock_authorize(req)) {
        return send_error(req, "403 Forbidden", "authorization required");
    }
    char body[128];
    if (recv_body(req, body, sizeof(body)) < 0) {
        return send_error(req, "400 Bad Request", "body required");
    }
    bool value = false;
    bool touched = false;
    if (form_flag(body, "power", &value)) {
        dp_relay_set(DP_OUTPUT_MAINS, value);
        touched = true;
    }
    if (form_flag(body, "usb", &value)) {
        dp_relay_set(DP_OUTPUT_USB1, value);
        touched = true;
    }
    if (!touched) {
        return send_error(req, "400 Bad Request", "no recognised parameter");
    }
    httpd_resp_set_type(req, "text/plain");
    return httpd_resp_sendstr(req, "ok");
}

static esp_err_t stock_state_get(httpd_req_t *req)
{
    // The stock field set, so existing clients parse it unchanged. Meter values
    // read zero until dp_meter lands; power_state and usb_state are real.
    cJSON *root = cJSON_CreateObject();
    cJSON_AddNumberToObject(root, "countdown_state", 0);
    cJSON_AddStringToObject(root, "auto_poweroff", "0");
    cJSON_AddStringToObject(root, "countdown", "0");
    cJSON_AddNumberToObject(root, "voltage", 0);
    cJSON_AddNumberToObject(root, "current", 0);
    cJSON_AddNumberToObject(root, "power", 0);
    cJSON_AddNumberToObject(root, "power_state", dp_relay_get(DP_OUTPUT_MAINS) ? 1 : 0);
    cJSON_AddNumberToObject(root, "usb_state", dp_relay_get(DP_OUTPUT_USB1) ? 1 : 0);
    cJSON_AddNumberToObject(root, "ele", 0);
    return send_json(req, root);
}

static esp_err_t token_post(httpd_req_t *req)
{
    if (!authorize(req, NULL)) {
        return send_error(req, "403 Forbidden", "authorization required");
    }
    char body[128];
    if (recv_body(req, body, sizeof(body)) < 0) {
        return send_error(req, "400 Bad Request", "body required");
    }
    cJSON *root = cJSON_Parse(body);
    if (!root) {
        return send_error(req, "400 Bad Request", "invalid JSON");
    }
    const cJSON *token = cJSON_GetObjectItemCaseSensitive(root, "token");
    if (!cJSON_IsString(token) ||
        (token->valuestring[0] != '\0' && !token_valid(token->valuestring))) {
        cJSON_Delete(root);
        return send_error(req, "400 Bad Request",
                          "token (1-64 printable characters, no spaces; empty "
                          "clears it) is required");
    }
    esp_err_t err = token_store(token->valuestring);
    const bool token_set = token->valuestring[0] != '\0';
    cJSON_Delete(root);
    if (err != ESP_OK) {
        return send_error(req, "500 Internal Server Error", esp_err_to_name(err));
    }
    dc_evlog_add(token_set ? "control token set" : "control token cleared");
    cJSON *reply = cJSON_CreateObject();
    cJSON_AddBoolToObject(reply, "ok", true);
    cJSON_AddBoolToObject(reply, "token_set", token_set);
    return send_json(req, reply);
}

// ------------------------------------------------------ dc_portal callbacks

static cJSON *describe_product(void *ctx)
{
    (void)ctx;
    cJSON *root = cJSON_CreateObject();
    cJSON *sections = cJSON_AddArrayToObject(root, "sections");

    cJSON *section = cJSON_CreateObject();
    cJSON_AddStringToObject(section, "title", "Power");
    cJSON_AddStringToObject(section, "description",
                            "What the outlet does when mains power returns.");
    cJSON *fields = cJSON_AddArrayToObject(section, "fields");

    cJSON *field = cJSON_CreateObject();
    cJSON_AddStringToObject(field, "key", "restore");
    cJSON_AddStringToObject(field, "label", "After a power cut");
    cJSON_AddStringToObject(field, "type", "select");
    cJSON_AddStringToObject(field, "value", dp_restore_to_str(dp_relay_get_restore()));
    cJSON_AddStringToObject(field, "hint",
                            "Off is the safe default: the outlet stays off until "
                            "something asks for it.");
    cJSON *options = cJSON_AddArrayToObject(field, "options");
    static const char *const VALUES[] = { "off", "on", "last" };
    static const char *const LABELS[] = { "Stay off", "Switch on", "Restore last state" };
    for (int i = 0; i < 3; i++) {
        cJSON *option = cJSON_CreateObject();
        cJSON_AddStringToObject(option, "value", VALUES[i]);
        cJSON_AddStringToObject(option, "label", LABELS[i]);
        cJSON_AddItemToArray(options, option);
    }
    cJSON_AddItemToArray(fields, field);
    cJSON_AddItemToArray(sections, section);
    return root;
}

static esp_err_t apply_product(const cJSON *values, void *ctx,
                               char *message, size_t message_size)
{
    (void)ctx;
    const cJSON *restore = cJSON_GetObjectItemCaseSensitive(values, "restore");
    if (cJSON_IsString(restore)) {
        // Every field is optional on POST, but an unrecognised value is a real
        // error rather than something to silently coerce to the default.
        dp_restore_t parsed = dp_restore_from_str(restore->valuestring, DP_RESTORE_INVALID);
        if (parsed == DP_RESTORE_INVALID) {
            snprintf(message, message_size, "unknown restore policy");
            return ESP_ERR_INVALID_ARG;
        }
        esp_err_t err = dp_relay_set_restore(parsed);
        if (err != ESP_OK) {
            snprintf(message, message_size, "could not save: %s", esp_err_to_name(err));
            return err;
        }
        snprintf(message, message_size, "restore policy set to %s",
                 dp_restore_to_str(parsed));
    }
    return ESP_OK;
}

static esp_err_t guard_operation(dc_portal_operation_t operation, void *ctx,
                                 char *message, size_t message_size)
{
    (void)ctx;
    // Both operations reboot, and a reboot drops the relay: the outputs are
    // re-driven from scratch by dp_relay_init. Cutting mains under whatever is
    // plugged in - mid-print, most likely - is not something to do silently, so
    // require the user to switch the outlet off first.
    if (dp_relay_get(DP_OUTPUT_MAINS)) {
        snprintf(message, message_size,
                 "Switch the outlet off first: %s reboots the device, which drops "
                 "the relay and cuts power to whatever is plugged in.",
                 operation == DC_PORTAL_OPERATION_OTA ? "updating firmware"
                                                      : "a factory reset");
        return ESP_ERR_INVALID_STATE;
    }
    return ESP_OK;
}

static esp_err_t factory_reset(void *ctx)
{
    (void)ctx;
    esp_err_t token_err = token_store("");
    esp_err_t relay_err = dp_relay_clear();
    return relay_err != ESP_OK ? relay_err : token_err;
}

// ------------------------------------------------------------------ routes

static const httpd_uri_t ROUTES[] = {
    { .uri = "/api/v2/info",       .method = HTTP_GET,  .handler = info_get },
    { .uri = "/api/v2/state",      .method = HTTP_GET,  .handler = state_get },
    { .uri = "/api/v2/command",    .method = HTTP_POST, .handler = command_post },
    { .uri = "/api/v2/token",      .method = HTTP_POST, .handler = token_post },
    // Stock compatibility.
    { .uri = "/set",               .method = HTTP_POST, .handler = stock_set_post },
    { .uri = "/update_ele_data",   .method = HTTP_GET,  .handler = stock_state_get },
};

esp_err_t dp_portal_start(void)
{
    const dc_portal_config_t config = {
        .product             = DP_PRODUCT,
        .display_name        = DP_DISPLAY_NAME,
        .product_routes      = ROUTES,
        .product_route_count = sizeof(ROUTES) / sizeof(ROUTES[0]),
        .describe_product    = describe_product,
        .apply_product       = apply_product,
        .authorize           = authorize,
        .guard_operation     = guard_operation,
        .factory_reset       = factory_reset,
    };
    esp_err_t err = dc_portal_start(&config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "dc_portal_start failed: %s", esp_err_to_name(err));
        return err;
    }
    dc_evlog_add("portal up");
    return ESP_OK;
}
