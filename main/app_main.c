// SPDX-License-Identifier: MIT
//
// DragonPWR - open firmware for the BIGTREETECH Panda PWR.
//
// Phase 1 scope: bring the outputs up in a known-safe state, then hand the
// network and management plane to dragon-core. No meter and no printer
// integration yet - see docs/ROADMAP.md.

#include "dc_evlog.h"
#include "dc_wifi.h"
#include "dp_board.h"
#include "dp_button.h"
#include "dp_portal.h"
#include "dp_printer.h"
#include "dp_relay.h"
#include "driver/uart.h"
#include "esp_err.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_system.h"
#include "nvs_flash.h"

static const char *TAG = "dragonpwr";

// This board's ESP8684 module has a 26MHz crystal (confirmed via esptool's
// hardware-measured chip detection, and via esp_clk_tree forcing a fresh
// calibration: both read 26,000,000 Hz exactly). CONFIG_XTAL_FREQ is set to
// match (sdkconfig.defaults) - required for the Wi-Fi radio to transmit on
// the right frequency at all, and verified fixed: see docs/BENCH_NOTES.md,
// 2026-09-14 session.
//
// The console UART is a separate, known ESP32/ESP32-C2 quirk on top of
// that: the ROM/2nd-stage-bootloader path that sets its baud divisor
// (bootloader_support/src/bootloader_console.c) sometimes samples the
// 26MHz crystal before its startup transient has settled, so the console
// intermittently comes up at 74880 baud instead of the configured 115200.
// This is sporadic and hardware-induced, not something CONFIG_XTAL_FREQ or
// boot ordering can deterministically fix - Espressif confirmed the same
// symptom and the same workaround upstream:
// https://github.com/espressif/esp-idf/issues/2518
// Re-applying the baud rate here narrows the window but does not
// guarantee it; if a bench session's log looks like garbage, retry the
// reset, or fall back to opening the monitor at 74880 baud.
static void reapply_console_baud(void)
{
    esp_err_t err = uart_set_baudrate(CONFIG_ESP_CONSOLE_UART_NUM,
                                       CONFIG_ESP_CONSOLE_UART_BAUDRATE);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "console baud re-apply failed: %s", esp_err_to_name(err));
    }
}

static esp_err_t init_nvs(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    return err;
}

static esp_err_t configure_network_identity(void)
{
    const dc_wifi_identity_t identity = {
        .hostname       = "dragonpwr",
        .instance_name  = "DragonPWR",
        .ap_ssid_prefix = "DragonPWR_",
        .ap_password    = DC_WIFI_DEFAULT_AP_PASSWORD,
    };
    return dc_wifi_set_identity(&identity);
}

// CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE boots a freshly OTA'd image as
// "pending verify" and, unless the app confirms it, rolls back to the previous
// slot on the next reset - any reset, including the power cut a plug sees all
// the time. So without this an OTA works until the mains next blips and then
// silently reverts.
//
// Confirmed once the portal is serving: at that point the device can be
// reached and can take another update, which is the property rollback exists
// to protect. Anything that fails before here hits ESP_ERROR_CHECK, resets,
// and rolls back. It cannot catch an image that serves the portal but is
// unreachable over the air (the wrong-crystal failure in docs/BENCH_NOTES.md
// was exactly that); serial is still the recovery for those.
static const char *ota_state_str(esp_err_t err, esp_ota_img_states_t state)
{
    if (err != ESP_OK) {
        return "unknown";
    }
    switch (state) {
    case ESP_OTA_IMG_NEW:            return "new";
    case ESP_OTA_IMG_PENDING_VERIFY: return "pending";
    case ESP_OTA_IMG_VALID:          return "valid";
    case ESP_OTA_IMG_INVALID:        return "invalid";
    case ESP_OTA_IMG_ABORTED:        return "aborted";
    case ESP_OTA_IMG_UNDEFINED:      return "undefined";
    default:                         return "?";
    }
}

static const char *reset_reason_str(esp_reset_reason_t reason)
{
    switch (reason) {
    case ESP_RST_POWERON:  return "power-on";
    case ESP_RST_SW:       return "restart";
    case ESP_RST_PANIC:    return "panic";
    case ESP_RST_INT_WDT:  return "int-wdt";
    case ESP_RST_TASK_WDT: return "task-wdt";
    case ESP_RST_WDT:      return "wdt";
    case ESP_RST_BROWNOUT: return "brownout";
    default:               return "other";
    }
}

static void confirm_running_image(void)
{
    const esp_partition_t *running = esp_ota_get_running_partition();
    esp_ota_img_states_t state = ESP_OTA_IMG_UNDEFINED;
    const esp_err_t state_err = esp_ota_get_state_partition(running, &state);

    // One line per boot, into the event log the web UI shows. The log is RAM
    // only, so this is the only way to tell from a browser which slot booted,
    // whether the bootloader treated it as a fresh OTA ("pending" - "new"
    // means the bootloader was built without rollback), and why the last
    // reset happened.
    dc_evlog_add("boot %s, image %s, reset %s", running->label,
                 ota_state_str(state_err, state),
                 reset_reason_str(esp_reset_reason()));

    if (state_err != ESP_OK || state != ESP_OTA_IMG_PENDING_VERIFY) {
        return;   // serial-flashed, already confirmed, or no rollback
    }
    esp_err_t err = esp_ota_mark_app_valid_cancel_rollback();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "could not confirm OTA image, it will roll back on the "
                      "next reset: %s", esp_err_to_name(err));
        dc_evlog_add("OTA confirm FAILED: %s", esp_err_to_name(err));
        return;
    }
    ESP_LOGI(TAG, "OTA image confirmed on %s", running->label);
    dc_evlog_add("OTA image confirmed");
}

void app_main(void)
{
    reapply_console_baud();
    ESP_LOGI(TAG, "DragonPWR booting");

    dc_evlog_console_init();
    dc_evlog_init();
    dc_evlog_add("DragonPWR boot");

    ESP_ERROR_CHECK(init_nvs());

    // The outputs come first, before Wi-Fi and before the portal. This device
    // switches mains: the window between reset and a known pin state should be
    // as short as the firmware can make it, and nothing above depends on the
    // network being up.
    ESP_ERROR_CHECK(dp_relay_init());
    ESP_ERROR_CHECK(dp_button_init());

    ESP_ERROR_CHECK(configure_network_identity());
    ESP_ERROR_CHECK(dc_wifi_start());
    ESP_ERROR_CHECK(dp_portal_start());
    // Before the image is confirmed, so a build whose printer client crashes
    // the device at start still rolls back. Never fails the boot itself.
    dp_printer_start();
    confirm_running_image();
    // Everything that runs today is up by here, so this is the baseline any
    // new feature has to fit into. Live numbers are in /api/v2/state.
    dc_evlog_add("heap free %u KB, min %u KB, largest block %u KB",
                 (unsigned)(heap_caps_get_free_size(MALLOC_CAP_8BIT) / 1024),
                 (unsigned)(heap_caps_get_minimum_free_size(MALLOC_CAP_8BIT) / 1024),
                 (unsigned)(heap_caps_get_largest_free_block(MALLOC_CAP_8BIT) / 1024));

    ESP_LOGI(TAG, "up: mains=%s usb1=%s",
             dp_relay_get(DP_OUTPUT_MAINS) ? "on" : "off",
             dp_relay_get(DP_OUTPUT_USB1) ? "on" : "off");
}
