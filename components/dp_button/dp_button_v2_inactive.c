// SPDX-License-Identifier: MIT
// V2 SET key and other inputs have not been electrically identified.
// Deliberately leave all candidate pins untouched.
#include "dp_button.h"

#include "esp_log.h"

static const char *TAG = "dp_button_v2";

esp_err_t dp_button_init(void)
{
    ESP_LOGW(TAG, "V2 physical buttons DISABLED pending pin identification");
    return ESP_OK;
}
