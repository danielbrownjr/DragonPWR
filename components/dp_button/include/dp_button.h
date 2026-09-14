#pragma once

#include "esp_err.h"

// GPIO10 (push button, active low) and GPIO6 (maintained-contact external
// toggle - see docs/HARDWARE_ANALYSIS.md) both drive the mains output through
// dp_relay_set(), the same path every other power-off route uses. That means
// the Phase 2 mid-print interlock, once it lands there, protects the physical
// controls too, not just the HTTP API - it does not need a second copy here.
//
// GPIO6's physical identity is still unconfirmed (docs/ROADMAP.md Phase 0).
// GPIO10's stock role - a "Bind" button for ESP-NOW pairing with a Panda
// Touch, per BTT's manual - is not reproduced: ESP-NOW pairing is a deferred
// non-goal (docs/ROADMAP.md), so rather than leave the button dead, it is
// repurposed here as a manual mains toggle.
//
// Configures both pins and starts the polling task that services them. Call
// after dp_relay_init() so the output it drives already exists.
esp_err_t dp_button_init(void);
