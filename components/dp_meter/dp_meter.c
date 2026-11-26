// SPDX-License-Identifier: MIT
#include "dp_meter.h"
#include "dp_meter_protocol.h"
#include "sdkconfig.h"
#if defined(CONFIG_IDF_TARGET_ESP32C2)
#include "dp_board.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static portMUX_TYPE lock = portMUX_INITIALIZER_UNLOCKED;
static dp_meter_snapshot_t latest;
static bool started;

static bool read_reg(uint8_t reg, size_t width, uint32_t *value)
{
    uint8_t req[2], reply[5];
    if (!dp_meter_request(reg, width, req)) return false;
    if (uart_flush_input(DP_METER_UART) != ESP_OK) return false;
    if (uart_write_bytes(DP_METER_UART, req, sizeof(req)) != (int)sizeof(req))
        return false;
    int n = uart_read_bytes(DP_METER_UART, reply, width + 1,
                            pdMS_TO_TICKS(80) + 1);
    return n >= 0 && dp_meter_decode(reg, width, reply, (size_t)n, value);
}

static void meter_task(void *arg)
{
    (void)arg;
    for (;;) {
        dp_meter_snapshot_t s = {0};
        // No special commands, energy or clear-on-read interrupt registers.
        // Configuration and coefficients are observed on every poll.
        bool ok =
            read_reg(0x00, 2, &s.syscon) &&
            read_reg(0x01, 2, &s.emucon) &&
            read_reg(0x02, 2, &s.hfconst) &&
            read_reg(0x13, 2, &s.emucon2) &&
            read_reg(0x72, 2, &s.voltage_coef) &&
            read_reg(0x70, 2, &s.current_coef) &&
            read_reg(0x73, 2, &s.power_coef) &&
            read_reg(0x26, 3, &s.voltage_raw) &&
            read_reg(0x24, 3, &s.current_raw) &&
            read_reg(0x2c, 4, &s.power_raw) &&
            read_reg(0x23, 2, &s.frequency_raw);
        // Recheck configuration to reject a reset/change during the sweep.
        uint32_t syscon, emucon, hfconst, emucon2;
        ok = ok && read_reg(0x00, 2, &syscon) &&
            read_reg(0x01, 2, &emucon) && read_reg(0x02, 2, &hfconst) &&
            read_reg(0x13, 2, &emucon2) &&
            syscon == s.syscon && emucon == s.emucon &&
            hfconst == s.hfconst && emucon2 == s.emucon2;
        s.status = ok ? DP_METER_FRESH : DP_METER_INVALID;
        s.sampled_us = esp_timer_get_time();
        portENTER_CRITICAL(&lock);
        latest = s;
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
    esp_err_t err = uart_driver_install(DP_METER_UART, 256, 0, 0, NULL, 0);
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
#else
esp_err_t dp_meter_start(void) { return ESP_ERR_NOT_SUPPORTED; }
dp_meter_snapshot_t dp_meter_snapshot(void) { return (dp_meter_snapshot_t){0}; }
#endif
