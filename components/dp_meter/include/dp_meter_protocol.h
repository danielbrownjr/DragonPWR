// SPDX-License-Identifier: MIT
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
// Only explicitly allowlisted, non-destructive register reads.
bool dp_meter_request(uint8_t reg, size_t width, uint8_t out[2]);
bool dp_meter_decode(uint8_t reg, size_t width, const uint8_t *reply,
                     size_t length, uint32_t *value);

typedef enum {
    DP_METER_UNAVAILABLE = 0, DP_METER_INVALID, DP_METER_STALE, DP_METER_FRESH
} dp_meter_status_t;
typedef struct {
    dp_meter_status_t status;
    int64_t sampled_us;
    uint32_t syscon, emucon, hfconst, emucon2;
    uint32_t voltage_raw, current_raw, power_raw;
    uint32_t voltage_coef, current_coef, power_coef;
    uint32_t frequency_raw;
} dp_meter_snapshot_t;

typedef struct {
    const char *voltage_reason, *current_reason, *power_reason, *frequency_reason;
    float voltage, current, power, frequency;
} dp_meter_values_t;

// NULL reason means a valid scaled observation, including a genuine zero.
dp_meter_values_t dp_meter_values(const dp_meter_snapshot_t *s);
dp_meter_status_t dp_meter_freshness(dp_meter_status_t status, int64_t sampled_us,
                                     int64_t now_us);
const char *dp_meter_status_name(dp_meter_status_t status);

typedef bool (*dp_meter_read_fn)(void *ctx, uint8_t reg, size_t width, uint32_t *value);
void dp_meter_sweep(dp_meter_snapshot_t *s, dp_meter_read_fn read, void *ctx);

typedef struct {
    void *ctx;
    bool (*flush)(void *ctx);
    bool (*send)(void *ctx, const uint8_t *bytes, size_t count);
    int (*receive)(void *ctx, uint8_t *bytes, size_t count, unsigned timeout_ms);
} dp_meter_io_t;
bool dp_meter_transaction(const dp_meter_io_t *io, uint8_t reg,
                          size_t width, uint32_t *value);

/* Observations only; no change to transaction or sweep acceptance. */
typedef enum {
    DP_METER_TX_OK = 0, DP_METER_TX_ARGUMENT, DP_METER_TX_FLUSH,
    DP_METER_TX_SEND, DP_METER_TX_RX_ERROR, DP_METER_TX_TIMEOUT,
    DP_METER_TX_LENGTH, DP_METER_TX_GUARD_ERROR, DP_METER_TX_TRAILING,
    DP_METER_TX_CHECKSUM, DP_METER_TX_COUNT
} dp_meter_tx_result_t;

typedef struct {
    dp_meter_tx_result_t result;
    uint8_t reg, width, reply[5], expected_checksum, guard_byte;
    int received, guard_result;
    bool receive_attempted, guard_attempted, checksum_available;
} dp_meter_tx_diag_t;

typedef struct {
    uint16_t valid_mask; /* bits 0..14 in existing sweep order */
    uint8_t mismatch_mask; /* configuration order: 00, 01, 02, 13 */
    uint32_t reread[4];
} dp_meter_sweep_diag_t;

bool dp_meter_transaction_observed(const dp_meter_io_t *io, uint8_t reg,
    size_t width, uint32_t *value, dp_meter_tx_diag_t *diag);
void dp_meter_sweep_observed(dp_meter_snapshot_t *s, dp_meter_read_fn read,
    void *ctx, dp_meter_sweep_diag_t *diag);
