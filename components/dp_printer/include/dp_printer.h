#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "esp_err.h"

// Which printer, if any, DragonPWR follows. Exactly one per boot: the saved
// choice is read once by dp_printer_start(), which starts only that client, so
// a change takes effect after a restart.
//
// Stored through dragon-core's dc_source, mapped onto its values. dc_source
// falls back to Klipper when nothing is saved; DragonPWR falls back to LITE
// instead, so upgrading never silently binds a plug to a printer.
typedef enum {
    DP_SOURCE_LITE = 0,    // plug only - no printer connection
    DP_SOURCE_MOONRAKER,   // Klipper via Moonraker's websocket (dc_moonraker)
    DP_SOURCE_BAMBU,       // Bambu LAN MQTT over TLS (dc_bambu) - experimental
    DP_SOURCE_INVALID,
} dp_source_t;

const char *dp_source_to_str(dp_source_t source);
dp_source_t dp_source_from_str(const char *s);

// What was saved - may differ from what is running until the next boot.
dp_source_t dp_printer_saved_source(void);
esp_err_t dp_printer_set_source(dp_source_t source);

// Starts the saved source's client, if any. Call once, after Wi-Fi is up.
// Bambu is refused - logged, and reported in the status note - when the heap
// cannot carry a TLS session: on the ESP32-C2 that is a real possibility,
// and a refusal is better than an allocation failure mid-handshake.
esp_err_t dp_printer_start(void);

typedef struct {
    dp_source_t source;     // what is running this boot
    bool        started;    // the client is running
    bool        connected;  // receiving printer updates
    const char *link;       // "off", "unconfigured", "connecting", "connected"
    const char *state;      // "unknown", "idle", "preparing", "printing",
                            // "paused", "complete", "error"
    float       bed;        // degrees C; NAN when unknown
    float       bed_target;
    float       nozzle;     // NAN when the source does not report it (Bambu)
    float       progress;   // 0..1; NAN when unknown
    char        filename[64];
    char        note[64];   // why a source is not running, when it is not
} dp_printer_status_t;

void dp_printer_get_status(dp_printer_status_t *out);

// Wipes the source choice and both clients' saved settings. Factory reset.
esp_err_t dp_printer_clear(void);
