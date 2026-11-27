// SPDX-License-Identifier: MIT
#pragma once
#include "esp_err.h"
#include "dp_meter_protocol.h"
#include <stdint.h>
#include <stdbool.h>

esp_err_t dp_meter_start(void);
dp_meter_snapshot_t dp_meter_snapshot(void);

typedef struct {
    uint32_t sweeps, successful_sweeps, transaction_counts[DP_METER_TX_COUNT];
    uint32_t config_mismatches, preflush_bytes, preflush_query_errors;
    uint32_t uart_parity, uart_frame, uart_fifo_overflow, uart_buffer_full;
    uint32_t stack_high_water;
    int64_t attempt_us, completed_us, success_us, failure_us;
    dp_meter_tx_diag_t last_failure;
    uint8_t failed_read_index;
    dp_meter_sweep_diag_t sweep, failure_sweep;
    dp_meter_snapshot_t partial, failed_partial;
} dp_meter_diag_t;

dp_meter_diag_t dp_meter_diagnostics(void);
