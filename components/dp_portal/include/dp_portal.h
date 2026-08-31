#pragma once

#include "esp_err.h"

// Starts the shared dc_portal management plane with DragonPWR's identity,
// routes and safety policy. dc_portal owns the HTTP server, the family SPA,
// captive DNS, provisioning, OTA and factory reset; this component owns the
// product API.
esp_err_t dp_portal_start(void);
