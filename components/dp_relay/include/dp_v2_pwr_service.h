#pragma once
// C3-only, non-actuating orchestration. This is NOT physical contact feedback.
#include "esp_err.h"
#include "dp_v2_pwr_model.h"
#ifdef __cplusplus
extern "C" {
#endif
// Start disabled/OFF; creates independent periodic deadline monitor.
// Every service mutex acquisition has a finite wait (50 ms plus one RTOS tick).
// Timeout means software state is unavailable, never evidence of physical OFF.
esp_err_t dp_v2_pwr_service_start(void);
// Serialized requests. ON remains UNAVAILABLE because no driver is enabled.
dp_v2_pwr_result_t dp_v2_pwr_service_set(bool on, uint32_t duration_ms);
dp_v2_pwr_result_t dp_v2_pwr_service_snapshot(dp_v2_pwr_model_t *out);
// Exclusive lease for the legacy laboratory GPIO7 pulse. These calls do not
// drive pins. Future production drivers must participate in this arbitration.
// If lab_end cannot acquire the mutex, the lease remains conservatively held.
bool dp_v2_pwr_service_lab_begin(void);
void dp_v2_pwr_service_lab_end(void);
// Fail-closed software maintenance guard: rejects while lab lease is held,
// model is enabled/active/faulted, or service state cannot be inspected.
// Physical relay state is still unknown without independent feedback.
bool dp_v2_pwr_service_maintenance_allowed(void);
bool dp_v2_pwr_service_lab_active(void);
#ifdef __cplusplus
}
#endif
