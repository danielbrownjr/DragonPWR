#pragma once

#include "esp_err.h"

// GPIO10 (push button, active low) drives the mains output through
// dp_relay_set(), the same path every other power-off route uses. That means
// the Phase 2 mid-print interlock, once it lands there, protects the button
// too, not just the HTTP API - it does not need a second copy here.
//
// GPIO10's stock role - a "Bind" button for ESP-NOW pairing with a Panda
// Touch, per BTT's manual - is not reproduced: ESP-NOW pairing is a deferred
// non-goal (docs/ROADMAP.md), so rather than leave the button dead, it is
// repurposed here as a manual mains toggle.
//
// GPIO6 (the maintained-contact external toggle input - see
// docs/HARDWARE_ANALYSIS.md) is polled and its transitions are logged, but
// deliberately NOT wired to the relay: its physical identity is unconfirmed
// (docs/ROADMAP.md Phase 0) and it has no pull resistor, matching stock,
// since it's meant to be externally driven. An unconnected pin in that state
// floats, and a bench session caught it producing exactly the kind of
// "stable" transition this code would otherwise have acted on - switching
// live mains power from an input nobody actually touched. Do not wire it to
// dp_relay_set() until GPIO6 is confirmed to be a real, deliberate control.
//
// Configures both pins and starts the polling task that services them. Call
// after dp_relay_init() so the output it drives already exists.
esp_err_t dp_button_init(void);
