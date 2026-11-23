#pragma once
// Laboratory-only GPIO7 candidate, not installed by the PWR service.
// Never expose this callback through ordinary /api/v2/command or /set.
#include <stdbool.h>
#include "sdkconfig.h"
#if !defined(CONFIG_IDF_TARGET_ESP32C3) || !defined(CONFIG_DP_V2_GPIO7_EXPERIMENT)
#error "GPIO7 candidate is only available in ESP32-C3 experimental builds"
#endif
bool dp_v2_gpio7_drive(bool high, void *context);
