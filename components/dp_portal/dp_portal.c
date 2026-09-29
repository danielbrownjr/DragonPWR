// SPDX-License-Identifier: MIT
#include "dp_portal.h"

#include <ctype.h>
#include <inttypes.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cJSON.h"
#include "dc_evlog.h"
#if DP_WITH_BAMBU
#include "dc_bambu.h"
#endif
#if DP_WITH_MOONRAKER
#include "dc_moonraker.h"
#endif
#include "dc_portal.h"
#include "dc_wifi.h"
#include "dp_printer.h"
#include "dp_relay.h"
#include "esp_app_desc.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_ota_ops.h"
#include "esp_random.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs.h"
#include "nvs_flash.h"

static const char *TAG = "dp_portal";

#define DP_PRODUCT      "dragonpwr"
#define DP_DISPLAY_NAME "DragonPWR"

// The family shares one NVS namespace, same as dp_relay and the stock firmware.
#define DP_NVS_NAMESPACE "app_nvs"

// Fixed per boot: lets a client tell a reboot from a reconnect.
static char s_boot_id[9];
// Base MAC as 12 hex digits, so the same unit reads the same across reflashes.
static char s_device_id[13];

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
    // dc_ui reads the running version from "firmware" (footer, Maintenance,
    // and the only on-screen way to tell an OTA stuck); "version" stays for
    // anything already reading it.
    cJSON_AddStringToObject(root, "firmware", app ? app->version : "unknown");
    cJSON_AddStringToObject(root, "device_id", s_device_id);
    cJSON_AddStringToObject(root, "variant", dp_printer_variant());
    cJSON_AddStringToObject(root, "boot_id", s_boot_id);

    // What "Boot inactive slot" would boot. dc_ui shows the button only when
    // this is present, so leave it out when the slot has no readable app.
    const esp_partition_t *other = esp_ota_get_next_update_partition(NULL);
    esp_app_desc_t other_desc;
    if (other && esp_ota_get_partition_description(other, &other_desc) == ESP_OK) {
        cJSON *slot = cJSON_AddObjectToObject(root, "inactive_slot");
        cJSON_AddStringToObject(slot, "label", other->label);
        cJSON_AddStringToObject(slot, "project", other_desc.project_name);
        cJSON_AddStringToObject(slot, "version", other_desc.version);
    }

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

// NaN (not reported) goes out as null; JSON has no NaN.
static void add_temp(cJSON *obj, const char *key, float value)
{
    if (isfinite(value)) {
        cJSON_AddNumberToObject(obj, key, value);
    } else {
        cJSON_AddNullToObject(obj, key);
    }
}

static esp_err_t state_get(httpd_req_t *req)
{
    cJSON *root = cJSON_CreateObject();
    cJSON *outputs = cJSON_AddObjectToObject(root, "outputs");
    cJSON_AddBoolToObject(outputs, "mains", dp_relay_get(DP_OUTPUT_MAINS));
    cJSON_AddBoolToObject(outputs, "usb1", dp_relay_get(DP_OUTPUT_USB1));
    cJSON_AddStringToObject(root, "restore",
                            dp_restore_to_str(dp_relay_get_restore()));
    // No meter yet: null rather than zeros, so a client can tell "not
    // implemented" from "measured zero". The printer is null in plug-only mode.
    cJSON_AddNullToObject(root, "meter");
    dp_printer_status_t pr;
    dp_printer_get_status(&pr);
    if (pr.source == DP_SOURCE_LITE) {
        cJSON_AddNullToObject(root, "printer");
    } else {
        cJSON *printer = cJSON_AddObjectToObject(root, "printer");
        cJSON_AddStringToObject(printer, "source", dp_source_to_str(pr.source));
        cJSON_AddStringToObject(printer, "link", pr.link);
        cJSON_AddBoolToObject(printer, "connected", pr.connected);
        cJSON_AddStringToObject(printer, "state", pr.state);
        add_temp(printer, "bed", pr.bed);
        add_temp(printer, "bed_target", pr.bed_target);
        add_temp(printer, "nozzle", pr.nozzle);
        add_temp(printer, "progress", pr.progress);
        cJSON_AddStringToObject(printer, "filename", pr.filename);
        if (pr.note[0]) {
            cJSON_AddStringToObject(printer, "note", pr.note);
        }
    }

    // Headroom for whatever comes next - a TLS printer client is the obvious
    // candidate. largest_block matters as much as free: mbedTLS wants
    // contiguous buffers, and a fragmented heap fails a handshake with plenty
    // free in total. min_free is the low-water mark since boot.
    cJSON *heap = cJSON_AddObjectToObject(root, "heap");
    cJSON_AddNumberToObject(heap, "free", heap_caps_get_free_size(MALLOC_CAP_8BIT));
    cJSON_AddNumberToObject(heap, "min_free",
                            heap_caps_get_minimum_free_size(MALLOC_CAP_8BIT));
    cJSON_AddNumberToObject(heap, "largest_block",
                            heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));
    return send_json(req, root);
}

// dc_ui's event log card. Same body as dc_portal's /api/v1/system/logs, but
// readable without a token like /api/v2/state: the card fetches it with no
// auth header, and the log holds no secrets - token changes are logged only as
// "set" or "cleared".
static esp_err_t logs_get(httpd_req_t *req)
{
    // ~6 KiB: too big for the httpd task's stack, so take it from the heap.
    dc_evlog_entry_t *entries = calloc(DC_EVLOG_MAX_ENTRIES, sizeof(*entries));
    if (!entries) {
        return send_error(req, "500 Internal Server Error", "out of memory");
    }
    const size_t count = dc_evlog_snapshot(entries, DC_EVLOG_MAX_ENTRIES);
    cJSON *root = cJSON_CreateObject();
    cJSON *list = cJSON_AddArrayToObject(root, "entries");
    if (!list) {
        cJSON_Delete(root);
        free(entries);
        return send_error(req, "500 Internal Server Error", "out of memory");
    }
    for (size_t i = 0; i < count; i++) {
        cJSON *entry = cJSON_CreateObject();
        cJSON_AddNumberToObject(entry, "ms", entries[i].ms);
        cJSON_AddStringToObject(entry, "text", entries[i].text);
        cJSON_AddItemToArray(list, entry);
    }
    free(entries);
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
    // First failure wins. Both outputs are still attempted, as stock would,
    // but a client is never told "ok" about a switch that did not happen.
    esp_err_t err = ESP_OK;
    if (form_flag(body, "power", &value)) {
        esp_err_t e = dp_relay_set(DP_OUTPUT_MAINS, value);
        if (err == ESP_OK) {
            err = e;
        }
        touched = true;
    }
    if (form_flag(body, "usb", &value)) {
        esp_err_t e = dp_relay_set(DP_OUTPUT_USB1, value);
        if (err == ESP_OK) {
            err = e;
        }
        touched = true;
    }
    if (!touched) {
        return send_error(req, "400 Bad Request", "no recognised parameter");
    }
    if (err != ESP_OK) {
        return send_error(req, "500 Internal Server Error", esp_err_to_name(err));
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

static cJSON *add_section(cJSON *sections, const char *title, const char *description)
{
    cJSON *section = cJSON_CreateObject();
    cJSON_AddStringToObject(section, "title", title);
    if (description) {
        cJSON_AddStringToObject(section, "description", description);
    }
    cJSON_AddItemToArray(sections, section);
    return section;
}

static cJSON *add_field(cJSON *section, const char *key, const char *label,
                        const char *type, const char *hint)
{
    cJSON *fields = cJSON_GetObjectItemCaseSensitive(section, "fields");
    if (!fields) {
        fields = cJSON_AddArrayToObject(section, "fields");
    }
    cJSON *field = cJSON_CreateObject();
    cJSON_AddStringToObject(field, "key", key);
    cJSON_AddStringToObject(field, "label", label);
    cJSON_AddStringToObject(field, "type", type);
    if (hint) {
        cJSON_AddStringToObject(field, "hint", hint);
    }
    cJSON_AddItemToArray(fields, field);
    return field;
}

static void add_option(cJSON *field, const char *value, const char *label)
{
    cJSON *options = cJSON_GetObjectItemCaseSensitive(field, "options");
    if (!options) {
        options = cJSON_AddArrayToObject(field, "options");
    }
    cJSON *option = cJSON_CreateObject();
    cJSON_AddStringToObject(option, "value", value);
    cJSON_AddStringToObject(option, "label", label);
    cJSON_AddItemToArray(options, option);
}

#if DP_WITH_MOONRAKER || DP_WITH_BAMBU
// dc_ui shows a section with visible_when only while that field holds value.
static void show_when_source(cJSON *section, const char *source)
{
    cJSON *when = cJSON_AddObjectToObject(section, "visible_when");
    cJSON_AddStringToObject(when, "field", "source");
    cJSON_AddStringToObject(when, "value", source);
}
#endif

#if DP_WITH_BAMBU
// Secrets are never sent back to the browser. The field reads blank, and a
// blank on save keeps what is stored (see apply_product).
static void add_secret(cJSON *section, const char *key, const char *label,
                       bool stored, const char *hint)
{
    cJSON *field = add_field(section, key, label, "text", hint);
    cJSON_AddBoolToObject(field, "secret", true);
    cJSON_AddStringToObject(field, "value", "");
    cJSON_AddStringToObject(field, "placeholder",
                            stored ? "Saved - leave blank to keep" : "");
}
#endif

static const char *source_label(dp_source_t source)
{
    switch (source) {
    case DP_SOURCE_MOONRAKER: return "Klipper (Moonraker)";
    case DP_SOURCE_BAMBU:     return "Bambu Lab";
    default:                  return "no printer";
    }
}

static cJSON *describe_product(void *ctx)
{
    (void)ctx;
    cJSON *root = cJSON_CreateObject();
    cJSON *sections = cJSON_AddArrayToObject(root, "sections");

    cJSON *power = add_section(sections, "Power",
                               "What the outlet does when mains power returns.");
    cJSON *field = add_field(power, "restore", "After a power cut", "select",
                             "Off is the safe default: the outlet stays off until "
                             "something asks for it.");
    cJSON_AddStringToObject(field, "value", dp_restore_to_str(dp_relay_get_restore()));
    add_option(field, "off", "Stay off");
    add_option(field, "on", "Switch on");
    add_option(field, "last", "Restore last state");

    // Which printer, if any, the plug follows - and what is running right now,
    // which differs from the saved choice until the next restart.
    dp_printer_status_t st;
    dp_printer_get_status(&st);
    const dp_source_t saved = dp_printer_saved_source();
    char now[160];
    if (st.source == DP_SOURCE_LITE) {
        snprintf(now, sizeof(now), "Running now: plug only%s%s.",
                 st.note[0] ? " - " : "", st.note);
    } else {
        snprintf(now, sizeof(now), "Running now: %s, %s%s%s.",
                 source_label(st.source), st.link,
                 st.note[0] ? " - " : "", st.note);
    }
    if (saved != st.source) {
        const size_t len = strlen(now);
        snprintf(now + len, sizeof(now) - len, " Saved: %s - restart to switch.",
                 source_label(saved));
    }
    // Plug-only builds have nothing to choose, so no Printer section at all.
    if (!dp_printer_source_available(DP_SOURCE_MOONRAKER) &&
        !dp_printer_source_available(DP_SOURCE_BAMBU)) {
        return root;
    }
    cJSON *printer = add_section(sections, "Printer", now);
    field = add_field(printer, "source", "Printer to follow", "select",
                      DP_WITH_BAMBU
                          ? "Takes effect after a restart. Bambu is experimental: "
                            "its encrypted connection needs more memory than this "
                            "chip reliably has spare."
                          : "Takes effect after a restart.");
    cJSON_AddStringToObject(field, "value",
                            dp_printer_source_available(saved) ? dp_source_to_str(saved)
                                                               : "lite");
    add_option(field, "lite", "None - plug only");
#if DP_WITH_MOONRAKER
    add_option(field, "moonraker", "Klipper (Moonraker)");
#endif
#if DP_WITH_BAMBU
    add_option(field, "bambu", "Bambu Lab (experimental)");
#endif

#if DP_WITH_MOONRAKER
    dc_moonraker_config_t mk = { 0 };
    dc_moonraker_get_config(&mk);
    // dc_moonraker stores an API key but never sends one, so no field for it:
    // Moonraker has to trust this plug's address instead.
    cJSON *moonraker = add_section(sections, "Moonraker",
                                   "The Klipper printer's Moonraker server. It "
                                   "must trust this plug's IP address "
                                   "([authorization] trusted_clients).");
    show_when_source(moonraker, "moonraker");
    field = add_field(moonraker, "mk_host", "Host", "text",
                      "IP address or hostname, e.g. 192.168.1.50");
    cJSON_AddStringToObject(field, "value", mk.host);
    field = add_field(moonraker, "mk_port", "Port", "number", "Usually 7125.");
    cJSON_AddNumberToObject(field, "value", mk.port ? mk.port : 7125);
    cJSON_AddNumberToObject(field, "min", 1);
    cJSON_AddNumberToObject(field, "max", 65535);

#endif
#if DP_WITH_BAMBU
    dc_bambu_config_t bb = { 0 };
    dc_bambu_get_config(&bb);
    cJSON *bambu = add_section(sections, "Bambu Lab",
                               "The printer must be in LAN mode. Read-only: "
                               "DragonPWR never sends the printer commands. "
                               "Saved settings apply after a restart.");
    show_when_source(bambu, "bambu");
    field = add_field(bambu, "bb_host", "Printer IP", "text", NULL);
    cJSON_AddStringToObject(field, "value", bb.host);
    field = add_field(bambu, "bb_serial", "Serial number", "text",
                      "On the printer's screen, or in Bambu Studio.");
    cJSON_AddStringToObject(field, "value", bb.serial);
    add_secret(bambu, "bb_code", "LAN access code", bb.code[0] != '\0',
               "On the printer's screen under LAN mode.");
#endif
    return root;
}

static void append_message(char *message, size_t size, const char *text)
{
    const size_t len = strlen(message);
    if (len + 2 < size) {
        snprintf(message + len, size - len, "%s%s", len ? "; " : "", text);
    }
}

#if DP_WITH_MOONRAKER || DP_WITH_BAMBU
// Host names and IPs only: no scheme, no spaces, no path.
static bool host_valid(const char *host, size_t max)
{
    const size_t len = strlen(host);
    if (len == 0 || len >= max) {
        return false;
    }
    for (size_t i = 0; i < len; i++) {
        const char c = host[i];
        if (!(isalnum((unsigned char)c) || c == '.' || c == '-' || c == ':')) {
            return false;
        }
    }
    return true;
}
#endif

#if DP_WITH_BAMBU
// A blank secret keeps what is stored; see add_secret().
static void take_secret(const cJSON *item, char *dst, size_t size)
{
    if (cJSON_IsString(item) && item->valuestring[0]) {
        strlcpy(dst, item->valuestring, size);
    }
}
#endif

static esp_err_t apply_product(const cJSON *values, void *ctx,
                               char *message, size_t message_size)
{
    (void)ctx;
    message[0] = '\0';
    char text[96];
    esp_err_t err;

    // Every field is optional on POST - each section saves only its own - but
    // an unrecognised value is a real error rather than something to coerce.
    const cJSON *restore = cJSON_GetObjectItemCaseSensitive(values, "restore");
    if (cJSON_IsString(restore)) {
        dp_restore_t parsed = dp_restore_from_str(restore->valuestring, DP_RESTORE_INVALID);
        if (parsed == DP_RESTORE_INVALID) {
            snprintf(message, message_size, "unknown restore policy");
            return ESP_ERR_INVALID_ARG;
        }
        err = dp_relay_set_restore(parsed);
        if (err != ESP_OK) {
            snprintf(message, message_size, "could not save: %s", esp_err_to_name(err));
            return err;
        }
        snprintf(text, sizeof(text), "restore policy set to %s", dp_restore_to_str(parsed));
        append_message(message, message_size, text);
    }

    const cJSON *source = cJSON_GetObjectItemCaseSensitive(values, "source");
    if (cJSON_IsString(source)) {
        const dp_source_t parsed = dp_source_from_str(source->valuestring);
        if (parsed == DP_SOURCE_INVALID) {
            snprintf(message, message_size, "unknown printer source");
            return ESP_ERR_INVALID_ARG;
        }
        if (!dp_printer_source_available(parsed)) {
            snprintf(message, message_size, "%s is not in this build (%s)",
                     dp_source_to_str(parsed), dp_printer_variant());
            return ESP_ERR_INVALID_ARG;
        }
        err = dp_printer_set_source(parsed);
        if (err != ESP_OK) {
            snprintf(message, message_size, "could not save: %s", esp_err_to_name(err));
            return err;
        }
        snprintf(text, sizeof(text), "printer set to %s - restart to apply",
                 source_label(parsed));
        append_message(message, message_size, text);
    }

#if DP_WITH_MOONRAKER
    const cJSON *mk_host = cJSON_GetObjectItemCaseSensitive(values, "mk_host");
    if (cJSON_IsString(mk_host)) {
        dc_moonraker_config_t mk = { 0 };
        dc_moonraker_get_config(&mk);
        if (!host_valid(mk_host->valuestring, sizeof(mk.host))) {
            snprintf(message, message_size, "Moonraker host must be an IP or hostname");
            return ESP_ERR_INVALID_ARG;
        }
        strlcpy(mk.host, mk_host->valuestring, sizeof(mk.host));
        const cJSON *port = cJSON_GetObjectItemCaseSensitive(values, "mk_port");
        if (port) {
            const double p = cJSON_IsNumber(port) ? port->valuedouble
                           : cJSON_IsString(port) ? atof(port->valuestring) : 0;
            if (p < 1 || p > 65535) {
                snprintf(message, message_size, "Moonraker port must be 1-65535");
                return ESP_ERR_INVALID_ARG;
            }
            mk.port = (uint16_t)p;
        }
        // Reconnects at once when Moonraker is the running source.
        err = dc_moonraker_set_config(&mk);
        if (err != ESP_OK) {
            snprintf(message, message_size, "could not save: %s", esp_err_to_name(err));
            return err;
        }
        append_message(message, message_size, "Moonraker settings saved");
    }

#endif
#if DP_WITH_BAMBU
    const cJSON *bb_host = cJSON_GetObjectItemCaseSensitive(values, "bb_host");
    if (cJSON_IsString(bb_host)) {
        dc_bambu_config_t bb = { 0 };
        dc_bambu_get_config(&bb);
        if (!host_valid(bb_host->valuestring, sizeof(bb.host))) {
            snprintf(message, message_size, "printer IP must be an IP or hostname");
            return ESP_ERR_INVALID_ARG;
        }
        strlcpy(bb.host, bb_host->valuestring, sizeof(bb.host));
        const cJSON *serial = cJSON_GetObjectItemCaseSensitive(values, "bb_serial");
        if (cJSON_IsString(serial)) {
            if (strlen(serial->valuestring) >= sizeof(bb.serial)) {
                snprintf(message, message_size, "serial number is too long");
                return ESP_ERR_INVALID_ARG;
            }
            strlcpy(bb.serial, serial->valuestring, sizeof(bb.serial));
        }
        take_secret(cJSON_GetObjectItemCaseSensitive(values, "bb_code"),
                    bb.code, sizeof(bb.code));
        err = dc_bambu_set_config(&bb);
        if (err != ESP_OK) {
            snprintf(message, message_size, "could not save: %s", esp_err_to_name(err));
            return err;
        }
        append_message(message, message_size, "Bambu settings saved - restart to apply");
    }
#endif
    return ESP_OK;
}

// Every path that reboots goes through here. A reboot drops the relay: the
// outputs are re-driven from scratch by dp_relay_init. Cutting mains under
// whatever is plugged in - mid-print, most likely - is not something to do
// silently, so require the user to switch the outlet off first.
static esp_err_t refuse_reboot_if_on(const char *what, char *message,
                                     size_t message_size)
{
    if (dp_relay_get(DP_OUTPUT_MAINS)) {
        snprintf(message, message_size,
                 "Switch the outlet off first: %s reboots the device, which drops "
                 "the relay and cuts power to whatever is plugged in.", what);
        return ESP_ERR_INVALID_STATE;
    }
    return ESP_OK;
}

static esp_err_t guard_operation(dc_portal_operation_t operation, void *ctx,
                                 char *message, size_t message_size)
{
    (void)ctx;
    return refuse_reboot_if_on(operation == DC_PORTAL_OPERATION_OTA
                                   ? "updating firmware" : "a factory reset",
                               message, message_size);
}

static esp_err_t factory_reset(void *ctx)
{
    (void)ctx;
    esp_err_t token_err = token_store("");
    esp_err_t relay_err = dp_relay_clear();
    esp_err_t printer_err = dp_printer_clear();
    return relay_err != ESP_OK ? relay_err
         : token_err != ESP_OK ? token_err : printer_err;
}

// -------------------------------------------------------------- /power
// Stopgap control page (components/dp_portal/power.html): dc_ui has no
// DragonPWR surface yet, so nothing in the SPA can switch the outputs. The page
// itself is static and open; the command it POSTs is token-gated as usual.

extern const char power_html_start[] asm("_binary_power_html_start");

static esp_err_t power_page_get(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    // EMBED_TXTFILES NUL-terminates the blob.
    return httpd_resp_sendstr(req, power_html_start);
}

// ------------------------------------------------------ maintenance routes
// The routes dc_ui's Maintenance card calls. They are DragonBreath's contract;
// DragonPWR served none of them, so its Restart, Factory reset and Boot
// inactive slot buttons all failed with "Refused: error".

static bool require_auth(httpd_req_t *req)
{
    if (authorize(req, NULL)) {
        return true;
    }
    send_error(req, "403 Forbidden", "authorization required");
    return false;
}

// Answers, lets the response leave, then reboots. Runs on the httpd task, the
// same way dc_portal's own reset does.
static esp_err_t reply_and_restart(httpd_req_t *req, const char *event)
{
    dc_evlog_add("%s", event);
    cJSON *reply = cJSON_CreateObject();
    cJSON_AddBoolToObject(reply, "ok", true);
    cJSON_AddBoolToObject(reply, "rebooting", true);
    send_json(req, reply);
    vTaskDelay(pdMS_TO_TICKS(500));
    esp_restart();
    return ESP_OK; // unreachable
}

static bool maintenance_allowed(httpd_req_t *req, const char *what)
{
    if (!require_auth(req)) {
        return false;
    }
    char message[160];
    if (refuse_reboot_if_on(what, message, sizeof(message)) != ESP_OK) {
        send_error(req, "409 Conflict", message);
        return false;
    }
    return true;
}

static esp_err_t restart_post(httpd_req_t *req)
{
    if (!maintenance_allowed(req, "a restart")) {
        return ESP_OK;
    }
    return reply_and_restart(req, "restart requested");
}

static esp_err_t factory_reset_post(httpd_req_t *req)
{
    if (!require_auth(req)) {
        return ESP_OK;
    }
    // Same guard as dc_portal's /api/v1/system/reset: the query string is
    // what separates a deliberate erase from a stray POST.
    char query[48];
    char value[24];
    if (httpd_req_get_url_query_str(req, query, sizeof(query)) != ESP_OK ||
        httpd_query_key_value(query, "confirm", value, sizeof(value)) != ESP_OK ||
        strcmp(value, "factory-reset") != 0) {
        return send_error(req, "400 Bad Request",
                          "factory reset requires confirm=factory-reset");
    }
    char message[160];
    if (refuse_reboot_if_on("a factory reset", message, sizeof(message)) != ESP_OK) {
        return send_error(req, "409 Conflict", message);
    }
    esp_err_t err = factory_reset(NULL);
    if (err == ESP_OK) {
        err = dc_wifi_clear_creds();
    }
    if (err != ESP_OK) {
        return send_error(req, "500 Internal Server Error", esp_err_to_name(err));
    }
    return reply_and_restart(req, "factory reset");
}

static esp_err_t boot_inactive_post(httpd_req_t *req)
{
    if (!maintenance_allowed(req, "switching firmware slot")) {
        return ESP_OK;
    }
    const esp_partition_t *other = esp_ota_get_next_update_partition(NULL);
    esp_app_desc_t desc;
    if (!other || esp_ota_get_partition_description(other, &desc) != ESP_OK) {
        return send_error(req, "409 Conflict",
                          "the other slot holds no firmware to boot");
    }
    // Verifies the whole image before it becomes the boot partition, so a
    // blank or damaged slot is refused here rather than failing at boot.
    //
    // With a rollback-enabled bootloader, the image booted this way has to
    // confirm itself like any OTA. Stock firmware never does, so a switch back
    // to stock lasts until its next reset and then returns to DragonPWR. A
    // permanent return to stock is the serial restore in the README.
    esp_err_t err = esp_ota_set_boot_partition(other);
    if (err != ESP_OK) {
        return send_error(req, "409 Conflict", esp_err_to_name(err));
    }
    char event[DC_EVLOG_TEXT_BYTES];
    snprintf(event, sizeof(event), "booting %s: %s %s", other->label,
             desc.project_name, desc.version);
    return reply_and_restart(req, event);
}

// ------------------------------------------------------------------ routes

static const httpd_uri_t ROUTES[] = {
    { .uri = "/power",             .method = HTTP_GET,  .handler = power_page_get },
    { .uri = "/api/v2/info",       .method = HTTP_GET,  .handler = info_get },
    { .uri = "/api/v2/state",      .method = HTTP_GET,  .handler = state_get },
    { .uri = "/api/v2/logs",       .method = HTTP_GET,  .handler = logs_get },
    { .uri = "/api/v2/command",    .method = HTTP_POST, .handler = command_post },
    { .uri = "/api/v2/token",      .method = HTTP_POST, .handler = token_post },
    { .uri = "/api/v2/restart",       .method = HTTP_POST, .handler = restart_post },
    { .uri = "/api/v2/factory-reset", .method = HTTP_POST, .handler = factory_reset_post },
    { .uri = "/api/v2/boot-inactive", .method = HTTP_POST, .handler = boot_inactive_post },
    // Stock compatibility.
    { .uri = "/set",               .method = HTTP_POST, .handler = stock_set_post },
    { .uri = "/update_ele_data",   .method = HTTP_GET,  .handler = stock_state_get },
};

esp_err_t dp_portal_start(void)
{
    uint8_t mac[6] = {0};
    esp_efuse_mac_get_default(mac);
    snprintf(s_device_id, sizeof(s_device_id), "%02x%02x%02x%02x%02x%02x",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    // Wi-Fi is already up (app_main starts it first), so esp_random() is
    // drawing on the RF noise source here, not the boot-time fallback.
    snprintf(s_boot_id, sizeof(s_boot_id), "%08" PRIx32, esp_random());

    // RAM: every open socket holds TCP send/receive buffers, and a browser
    // loading the UI opens up to six at once. Cap at four and let the server
    // recycle the least recently used one instead of refusing a fifth.
    // dc_portal still applies its stack floor on top of this.
    httpd_config_t http = HTTPD_DEFAULT_CONFIG();
    http.max_open_sockets = 4;
    http.lru_purge_enable = true;

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
        .httpd_config        = &http,
    };
    esp_err_t err = dc_portal_start(&config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "dc_portal_start failed: %s", esp_err_to_name(err));
        return err;
    }
    dc_evlog_add("portal up");
    return ESP_OK;
}
