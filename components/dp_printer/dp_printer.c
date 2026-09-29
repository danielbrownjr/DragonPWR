// SPDX-License-Identifier: MIT
#include "dp_printer.h"

#include <math.h>
#include <string.h>

#include "dc_evlog.h"
#include "dc_source.h"
#if DP_WITH_BAMBU
#include "dc_bambu.h"
#endif
#if DP_WITH_MOONRAKER
#include "dc_moonraker.h"
#endif
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "nvs.h"

static const char *TAG = "dp_printer";

// dc_source's own namespace and key. Read directly only to tell "never saved"
// apart from "saved as Klipper", which dc_source_get() cannot: it returns
// Klipper for both.
#define DC_SOURCE_NAMESPACE "app_nvs"
#define DC_SOURCE_KEY       "ctl_src"

// Refuse to start Bambu below these. A TLS session wants a ~16 KB record
// buffer in one piece plus dc_bambu's 16 KB report buffer, and the handshake
// peaks well above that; starting it without the room trades a clear
// "not enough memory" for an allocation failure mid-handshake.
#define DP_BAMBU_MIN_FREE    (60 * 1024)
#define DP_BAMBU_MIN_LARGEST (24 * 1024)

// Set once by dp_printer_start(). The clients' get_status() take a lock their
// start() creates, so only the one that actually started may be asked.
static dp_source_t s_running = DP_SOURCE_LITE;
static bool        s_started;
static char        s_note[64];

const char *dp_source_to_str(dp_source_t source)
{
    switch (source) {
    case DP_SOURCE_MOONRAKER: return "moonraker";
    case DP_SOURCE_BAMBU:     return "bambu";
    case DP_SOURCE_LITE:      return "lite";
    default:                  return "invalid";
    }
}

dp_source_t dp_source_from_str(const char *s)
{
    if (!s) {
        return DP_SOURCE_INVALID;
    }
    if (strcmp(s, "lite") == 0) {
        return DP_SOURCE_LITE;
    }
    if (strcmp(s, "moonraker") == 0) {
        return DP_SOURCE_MOONRAKER;
    }
    if (strcmp(s, "bambu") == 0) {
        return DP_SOURCE_BAMBU;
    }
    return DP_SOURCE_INVALID;
}

static bool source_saved(void)
{
    nvs_handle_t handle;
    if (nvs_open(DC_SOURCE_NAMESPACE, NVS_READONLY, &handle) != ESP_OK) {
        return false;
    }
    uint8_t value;
    const bool present = nvs_get_u8(handle, DC_SOURCE_KEY, &value) == ESP_OK;
    nvs_close(handle);
    return present;
}

dp_source_t dp_printer_saved_source(void)
{
    if (!source_saved()) {
        return DP_SOURCE_LITE;
    }
    // Anything dc_source knows that DragonPWR does not drive (Home Assistant,
    // PrusaLink, Klipper-over-MQTT) reads as plug-only rather than guessing.
    switch (dc_source_get()) {
    case DC_SRC_KLIPPER: return DP_SOURCE_MOONRAKER;
    case DC_SRC_BAMBU:   return DP_SOURCE_BAMBU;
    default:             return DP_SOURCE_LITE;
    }
}

esp_err_t dp_printer_set_source(dp_source_t source)
{
    switch (source) {
    case DP_SOURCE_LITE:      return dc_source_set(DC_SRC_NONE);
    case DP_SOURCE_MOONRAKER: return dc_source_set(DC_SRC_KLIPPER);
    case DP_SOURCE_BAMBU:     return dc_source_set(DC_SRC_BAMBU);
    default:                  return ESP_ERR_INVALID_ARG;
    }
}

bool dp_printer_source_available(dp_source_t source)
{
    switch (source) {
    case DP_SOURCE_LITE:      return true;
    case DP_SOURCE_MOONRAKER: return DP_WITH_MOONRAKER;
    case DP_SOURCE_BAMBU:     return DP_WITH_BAMBU;
    default:                  return false;
    }
}

const char *dp_printer_variant(void)
{
    if (DP_WITH_MOONRAKER && DP_WITH_BAMBU) {
        return "full";
    }
    if (DP_WITH_MOONRAKER) {
        return "moonraker";
    }
    return DP_WITH_BAMBU ? "bambu" : "lite";
}

#if DP_WITH_BAMBU
static esp_err_t start_bambu(void)
{
    const size_t free_bytes = heap_caps_get_free_size(MALLOC_CAP_8BIT);
    const size_t largest = heap_caps_get_largest_free_block(MALLOC_CAP_8BIT);
    if (free_bytes < DP_BAMBU_MIN_FREE || largest < DP_BAMBU_MIN_LARGEST) {
        snprintf(s_note, sizeof(s_note), "not started: %u KB free, %u KB block",
                 (unsigned)(free_bytes / 1024), (unsigned)(largest / 1024));
        ESP_LOGW(TAG, "bambu %s", s_note);
        dc_evlog_add("bambu %s", s_note);
        return ESP_ERR_NO_MEM;
    }
    return dc_bambu_start();
}
#endif

esp_err_t dp_printer_start(void)
{
    s_running = dp_printer_saved_source();
    s_note[0] = '\0';
    esp_err_t err = ESP_OK;

    // A choice saved by a build that carried the client, now running on one
    // that does not (say, a lite image OTA'd over a full one): run plug-only
    // and say why, rather than fail. The saved choice is left as it is.
    if (!dp_printer_source_available(s_running)) {
        snprintf(s_note, sizeof(s_note), "%s is not in this build (%s)",
                 dp_source_to_str(s_running), dp_printer_variant());
        dc_evlog_add("printer source: %s", s_note);
        s_running = DP_SOURCE_LITE;
        return ESP_OK;
    }

    switch (s_running) {
#if DP_WITH_MOONRAKER
    case DP_SOURCE_MOONRAKER:
        err = dc_moonraker_start();
        break;
#endif
#if DP_WITH_BAMBU
    case DP_SOURCE_BAMBU:
        err = start_bambu();
        break;
#endif
    default:
        break;
    }

    s_started = s_running != DP_SOURCE_LITE && err == ESP_OK;
    if (s_running != DP_SOURCE_LITE && err != ESP_OK && !s_note[0]) {
        snprintf(s_note, sizeof(s_note), "not started: %s", esp_err_to_name(err));
    }
    ESP_LOGI(TAG, "source %s (build %s)%s%s", dp_source_to_str(s_running),
             dp_printer_variant(), s_note[0] ? ": " : "", s_note);
    dc_evlog_add("printer source: %s%s%s", dp_source_to_str(s_running),
                 s_note[0] ? ", " : "", s_note);
    // A printer client failing is not a reason to fail the boot: the plug is
    // still a working plug. It is reported, not propagated.
    return ESP_OK;
}

#if DP_WITH_MOONRAKER || DP_WITH_BAMBU
static const char *link_str(bool configured, bool connected)
{
    if (connected) {
        return "connected";
    }
    return configured ? "connecting" : "unconfigured";
}
#endif

#if DP_WITH_MOONRAKER
static const char *moonraker_state_str(dc_printer_state_t state)
{
    switch (state) {
    case DC_PRINTER_IDLE:      return "idle";
    case DC_PRINTER_PREPARING: return "preparing";
    case DC_PRINTER_PRINTING:  return "printing";
    case DC_PRINTER_PAUSED:    return "paused";
    case DC_PRINTER_COMPLETE:  return "complete";
    case DC_PRINTER_ERROR:     return "error";
    default:                   return "unknown";
    }
}

#endif

#if DP_WITH_BAMBU
static const char *bambu_state_str(dc_bambu_print_state_t state)
{
    switch (state) {
    case DC_BAMBU_PRINT_IDLE:        return "idle";
    case DC_BAMBU_PRINT_DOWNLOADING: // the printer is fetching the job: a print
    case DC_BAMBU_PRINT_PREPARING:   // has been started, it just is not moving yet
        return "preparing";
    case DC_BAMBU_PRINT_PRINTING:    return "printing";
    case DC_BAMBU_PRINT_PAUSED:      return "paused";
    case DC_BAMBU_PRINT_COMPLETE:    return "complete";
    case DC_BAMBU_PRINT_ERROR:       return "error";
    default:                         return "unknown";
    }
}

#endif

void dp_printer_get_status(dp_printer_status_t *out)
{
    memset(out, 0, sizeof(*out));
    out->source = s_running;
    out->started = s_started;
    out->link = "off";
    out->state = "unknown";
    out->bed = out->bed_target = out->nozzle = out->progress = NAN;
    strlcpy(out->note, s_note, sizeof(out->note));

    if (!s_started) {
        return;
    }
#if DP_WITH_MOONRAKER
    if (s_running == DP_SOURCE_MOONRAKER) {
        dc_moonraker_status_t st;
        dc_moonraker_get_status(&st);
        out->connected = st.state == DC_MK_SUBSCRIBED;
        out->link = link_str(st.state != DC_MK_DISABLED, out->connected);
        // Until updates arrive, the cached state is whatever was last seen -
        // possibly from before a disconnect - so do not report it.
        if (out->connected) {
            out->state = moonraker_state_str(st.printer);
            out->bed = st.bed_temp;
            out->bed_target = st.bed_target;
            out->nozzle = st.extruder_temp;
            out->progress = st.progress;
            strlcpy(out->filename, st.filename, sizeof(out->filename));
        }
    }
#endif
#if DP_WITH_BAMBU
    if (s_running == DP_SOURCE_BAMBU) {
        dc_bambu_status_t st;
        dc_bambu_get_status(&st);
        out->connected = st.state == DC_BAMBU_SUBSCRIBED;
        out->link = link_str(st.state != DC_BAMBU_DISABLED, out->connected);
        if (out->connected) {
            out->state = bambu_state_str(st.print_state);
            out->bed = st.bed_temp;
            out->bed_target = st.bed_target;
            out->progress = st.progress >= 0 ? st.progress : NAN;
        }
    }
#endif
}

esp_err_t dp_printer_clear(void)
{
    // A client not in this build cannot clear its own keys, so do it here:
    // a factory reset must not leave, say, a Bambu access code behind in NVS
    // for a later full build to pick up.
    esp_err_t err = ESP_OK;
#if DP_WITH_MOONRAKER
    err = dc_moonraker_clear_config();
#endif
#if DP_WITH_BAMBU
    const esp_err_t bambu_err = dc_bambu_clear_config();
    if (err == ESP_OK) {
        err = bambu_err;
    }
#endif
    nvs_handle_t handle;
    if (nvs_open(DC_SOURCE_NAMESPACE, NVS_READWRITE, &handle) == ESP_OK) {
        static const char *const KEYS[] = {
            DC_SOURCE_KEY,
#if !DP_WITH_MOONRAKER
            // dc_moonraker's keys (dc_moonraker.c)
            "mk_host", "mk_port", "mk_apikey",
#endif
#if !DP_WITH_BAMBU
            // dc_bambu's keys (dc_bambu.c)
            "bb_host", "bb_serial", "bb_code",
#endif
        };
        for (size_t i = 0; i < sizeof(KEYS) / sizeof(KEYS[0]); i++) {
            const esp_err_t erase = nvs_erase_key(handle, KEYS[i]);
            if (erase != ESP_OK && erase != ESP_ERR_NVS_NOT_FOUND && err == ESP_OK) {
                err = erase;
            }
        }
        nvs_commit(handle);
        nvs_close(handle);
    }
    return err;
}
