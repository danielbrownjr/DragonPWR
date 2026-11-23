// SPDX-License-Identifier: MIT
#include "dp_v2_pwr_model.h"
#include <stddef.h>
void dp_v2_pwr_model_init(dp_v2_pwr_model_t *m) {
    if (m) *m=(dp_v2_pwr_model_t){.phase=DP_V2_PWR_OFF};
}
static dp_v2_pwr_result_t off(dp_v2_pwr_model_t *m) {
    m->deadline_ms=0;
    if (!m->drive || !m->drive(false,m->context)) {
        m->phase=DP_V2_PWR_FAULT;
        return DP_V2_PWR_DRIVER_ERROR;
    }
    m->phase=DP_V2_PWR_OFF;
    m->deadline_off_retries_remaining=0;
    return DP_V2_PWR_OK;
}
dp_v2_pwr_result_t dp_v2_pwr_model_enable(dp_v2_pwr_model_t *m, dp_v2_pwr_drive_fn drive, void *ctx, uint32_t max_ms) {
    if (!m || !drive || !max_ms || m->enabled || m->phase!=DP_V2_PWR_OFF) return DP_V2_PWR_INVALID;
    // Retain the callback before the initial OFF attempt. If it fails,
    // FAULT must remain recoverable only through a subsequent real OFF call.
    m->drive=drive; m->context=ctx; m->max_on_ms=max_ms;
    if (!drive(false,ctx)) { m->phase=DP_V2_PWR_FAULT; return DP_V2_PWR_DRIVER_ERROR; }
    m->enabled=true;
    return DP_V2_PWR_OK;
}
dp_v2_pwr_result_t dp_v2_pwr_model_disable(dp_v2_pwr_model_t *m) {
    if (!m) return DP_V2_PWR_INVALID;
    m->enabled=false;
    if (!m->drive) {
        if (m->phase == DP_V2_PWR_FAULT) return DP_V2_PWR_DRIVER_ERROR;
        m->phase=DP_V2_PWR_OFF; m->deadline_ms=0;
        return DP_V2_PWR_OK;
    }
    return off(m);
}
dp_v2_pwr_result_t dp_v2_pwr_model_set(dp_v2_pwr_model_t *m, bool on, uint64_t now, uint32_t duration) {
    if (!m) return DP_V2_PWR_INVALID;
    if (!on) {
        if (m->phase==DP_V2_PWR_FAULT) return off(m); // OFF retry is always permitted
        if (m->phase==DP_V2_PWR_OFF) return DP_V2_PWR_OK;
        return off(m);
    }
    if (!m->enabled || !m->drive) return DP_V2_PWR_UNAVAILABLE;
    if (m->phase==DP_V2_PWR_FAULT) return DP_V2_PWR_DRIVER_ERROR;
    if (!duration || duration>m->max_on_ms || now>UINT64_MAX-duration) return DP_V2_PWR_INVALID;
    if (m->phase==DP_V2_PWR_ON) return DP_V2_PWR_INVALID; // no implicit deadline extension
    if (!m->drive(true,m->context)) {
        m->phase=DP_V2_PWR_FAULT; m->deadline_ms=0;
        (void)m->drive(false,m->context); // best-effort only
        return DP_V2_PWR_DRIVER_ERROR;
    }
    m->deadline_ms=now+duration; m->phase=DP_V2_PWR_ON;
    return DP_V2_PWR_OK;
}
dp_v2_pwr_result_t dp_v2_pwr_model_poll(dp_v2_pwr_model_t *m, uint64_t now) {
    if (!m) return DP_V2_PWR_INVALID;
    if (m->phase==DP_V2_PWR_ON && now>=m->deadline_ms) {
        const dp_v2_pwr_result_t result = off(m);
        if (result!=DP_V2_PWR_OK) {
            // Schedule bounded retries on subsequent scheduler polls only.
            // A failed OFF never changes the software phase back to OFF.
            m->deadline_off_retries_remaining=DP_V2_PWR_DEADLINE_OFF_RETRIES;
        }
        return result;
    }
    if (m->phase==DP_V2_PWR_FAULT && m->deadline_off_retries_remaining) {
        --m->deadline_off_retries_remaining;
        return off(m);
    }
    return DP_V2_PWR_OK;
}
