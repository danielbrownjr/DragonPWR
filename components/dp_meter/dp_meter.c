// SPDX-License-Identifier: MIT
#include "dp_meter.h"
#include "dp_meter_protocol.h"
#include "sdkconfig.h"
#if defined(CONFIG_IDF_TARGET_ESP32C2)
#include "dp_board.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"

static portMUX_TYPE lock = portMUX_INITIALIZER_UNLOCKED;
static dp_meter_snapshot_t latest;
static bool started;
static dp_meter_diag_t diagnostics;
static dp_meter_diag_t working; /* meter task only */
static QueueHandle_t uart_events;
static uint8_t read_index;

static void drain_events(void)
{
    uart_event_t event;
    /* Bound task work; events are best-effort, not tied to a specific reply. */
    for (unsigned i = 0; i < 16 && xQueueReceive(uart_events, &event, 0) == pdTRUE; ++i) {
        switch (event.type) {
        case UART_PARITY_ERR: ++working.uart_parity; break;
        case UART_FRAME_ERR: ++working.uart_frame; break;
        case UART_FIFO_OVF: ++working.uart_fifo_overflow; break;
        case UART_BUFFER_FULL: ++working.uart_buffer_full; break;
        default: break;
        }
    }
}

static bool flush_rx(void *ctx)
{
    (void)ctx;
    size_t buffered = 0;
    if (uart_get_buffered_data_len(DP_METER_UART, &buffered) == ESP_OK)
        working.preflush_bytes += (uint32_t)buffered;
    else ++working.preflush_query_errors;
    return uart_flush_input(DP_METER_UART) == ESP_OK;
}

static bool send_request(void *ctx, const uint8_t *bytes, size_t count)
{
    (void)ctx;
    return uart_write_bytes(DP_METER_UART, bytes, count) == (int)count;
}

static int receive_reply(void *ctx, uint8_t *bytes, size_t count, unsigned timeout_ms)
{
    (void)ctx;
    return uart_read_bytes(DP_METER_UART, bytes, count,
                           pdMS_TO_TICKS(timeout_ms) + 1);
}

static bool read_reg(void *ctx, uint8_t reg, size_t width, uint32_t *value)
{
    (void)ctx;
    const dp_meter_io_t io = {
        .flush = flush_rx, .send = send_request, .receive = receive_reply,
    };
    dp_meter_tx_diag_t tx;
    bool ok = dp_meter_transaction_observed(&io, reg, width, value, &tx);
    ++working.transaction_counts[tx.result];
    ++read_index;
    if (!ok) {
        working.last_failure = tx;
        working.failed_read_index = read_index;
    }
    drain_events();
    return ok;
}

static void meter_task(void *arg)
{
    (void)arg;
    for (;;) {
        dp_meter_snapshot_t s = {0};
        // No special commands, energy or clear-on-read interrupt registers.
        // Configuration and coefficients are observed on every poll.
        read_index = 0;
        working.attempt_us = esp_timer_get_time();
        portENTER_CRITICAL(&lock);
        diagnostics.attempt_us = working.attempt_us;
        portEXIT_CRITICAL(&lock);
        dp_meter_sweep_observed(&s, read_reg, NULL, &working.sweep);
        s.sampled_us = esp_timer_get_time();
        drain_events();
        ++working.sweeps;
        working.completed_us = s.sampled_us;
        working.partial = s;
        if (s.status == DP_METER_FRESH) {
            ++working.successful_sweeps;
            working.success_us = s.sampled_us;
        } else {
            working.failure_us = s.sampled_us;
            working.failure_sweep = working.sweep;
            working.failed_partial = s;
            if (working.sweep.mismatch_mask) {
                ++working.config_mismatches;
                working.failed_read_index = 0;
                working.last_failure = (dp_meter_tx_diag_t){0};
            }
        }
        working.stack_high_water = uxTaskGetStackHighWaterMark(NULL);
        portENTER_CRITICAL(&lock);
        latest = s;
        diagnostics = working;
        portEXIT_CRITICAL(&lock);
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

esp_err_t dp_meter_start(void)
{
    if (started) return ESP_ERR_INVALID_STATE;
    if (uart_is_driver_installed(DP_METER_UART)) return ESP_ERR_INVALID_STATE;
    const uart_config_t cfg = {
        .baud_rate = DP_METER_BAUD, .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_EVEN, .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE, .source_clk = UART_SCLK_DEFAULT,
    };
    esp_err_t err = uart_driver_install(DP_METER_UART, 256, 0, 16, &uart_events, 0);
    if (err != ESP_OK) return err;
    err = uart_param_config(DP_METER_UART, &cfg);
    if (err == ESP_OK)
        err = uart_set_pin(DP_METER_UART, DP_PIN_METER_TX, DP_PIN_METER_RX,
                           UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    if (err == ESP_OK &&
        xTaskCreate(meter_task, "dp_meter", 3072, NULL, 1, NULL) != pdPASS)
        err = ESP_ERR_NO_MEM;
    if (err != ESP_OK) {
        uart_driver_delete(DP_METER_UART);
        return err;
    }
    started = true;
    return ESP_OK;
}

dp_meter_snapshot_t dp_meter_snapshot(void)
{
    portENTER_CRITICAL(&lock);
    dp_meter_snapshot_t s = latest;
    portEXIT_CRITICAL(&lock);
    s.status = dp_meter_freshness(s.status, s.sampled_us, esp_timer_get_time());
    return s;
}
dp_meter_diag_t dp_meter_diagnostics(void)
{
    portENTER_CRITICAL(&lock);
    dp_meter_diag_t d = diagnostics;
    portEXIT_CRITICAL(&lock);
    return d;
}
#else
esp_err_t dp_meter_start(void) { return ESP_ERR_NOT_SUPPORTED; }
dp_meter_snapshot_t dp_meter_snapshot(void) { return (dp_meter_snapshot_t){0}; }
dp_meter_diag_t dp_meter_diagnostics(void) { return (dp_meter_diag_t){0}; }
#endif
