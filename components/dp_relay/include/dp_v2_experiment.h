#pragma once
// Experimental ESP32-C3 GPIO7 latch write. This is NOT relay-state reporting.
#include <stdint.h>
#include "esp_err.h"

#ifdef CONFIG_DP_V2_GPIO7_EXPERIMENT
// Called synchronously only by the dedicated, independently gated manual route.
// Releases the pin to high impedance after the specified interval.
esp_err_t dp_v2_experiment_gpio7_pulse(int level, uint32_t milliseconds);
#endif
