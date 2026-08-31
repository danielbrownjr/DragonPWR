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
#include "dp_portal.h"
#include "dp_relay.h"
#include "esp_err.h"
#include "esp_log.h"
#include "nvs_flash.h"

static const char *TAG = "dragonpwr";

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

void app_main(void)
{
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

    ESP_ERROR_CHECK(configure_network_identity());
    ESP_ERROR_CHECK(dc_wifi_start());
    ESP_ERROR_CHECK(dp_portal_start());

    ESP_LOGI(TAG, "up: mains=%s usb1=%s",
             dp_relay_get(DP_OUTPUT_MAINS) ? "on" : "off",
             dp_relay_get(DP_OUTPUT_USB1) ? "on" : "off");
}
