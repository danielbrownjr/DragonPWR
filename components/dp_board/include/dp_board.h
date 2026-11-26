#pragma once

// Compile-time board boundary. ESP32-C3 V2 does NOT share the original
// Panda PWR's electrical GPIO contract. Do not port V1 output GPIO macros.
#include "sdkconfig.h"
#include "driver/gpio.h"
#include "driver/spi_common.h"
#include "driver/uart.h"

#if defined(CONFIG_IDF_TARGET_ESP32C3)

// Panda PWR V2: static factory-firmware candidates, NOT qualified drive pins.
// AC output is gated by the inactive V2 dp_relay implementation; no caller
// may configure/drive these GPIOs until independent hardware validation.
#define DP_V2_BOARD                  1
#define DP_V2_PIN_AC_COMMAND         GPIO_NUM_7
#define DP_V2_PIN_AC_EDGE_INPUT      GPIO_NUM_18
#define DP_V2_PIN_USB_A_ENABLE       GPIO_NUM_0
#define DP_V2_PIN_USB_C_ENABLE       GPIO_NUM_1
#define DP_V2_PIN_METER_TX           GPIO_NUM_2
#define DP_V2_PIN_METER_RX           GPIO_NUM_3
#define DP_V2_METER_UART             UART_NUM_1
#define DP_V2_METER_BAUD             9600
// Physical purpose of GPIO6 and exact SET pins remain unidentified.
// No outputs, strap GPIOs or key inputs are configured by M1.

#elif defined(CONFIG_IDF_TARGET_ESP32C2)

// Panda PWR hardware pin map.
//
// Recovered from the stock firmware (panda_pwr-v1.0.0.1.bin) by static
// analysis. See docs/HARDWARE_ANALYSIS.md for the evidence behind every line.
//
// NOT YET CONFIRMED AGAINST A PHYSICAL BOARD. The relay polarity in particular
// is load-bearing: it is derived from the stock firmware computing power_state
// as `snez(gpio_get_level(7) - 1)`, i.e. reported ON when the pin reads LOW.


// --- Mains relay -----------------------------------------------------------
// ACTIVE LOW: driving this pin low energises the relay and switches the outlet
// ON. Everything that touches it must go through dp_relay, which owns the
// inversion so it is written down exactly once.
#define DP_PIN_RELAY             GPIO_NUM_7
#define DP_RELAY_ACTIVE_LEVEL    0

// --- Switched USB port (USB1) ----------------------------------------------
// Active high. USB2 is unswitched on this hardware and is not under firmware
// control at all.
#define DP_PIN_USB1              GPIO_NUM_18
#define DP_USB1_ACTIVE_LEVEL     1

// --- External toggle input -------------------------------------------------
// No internal pull, so it is externally driven. Stock inverts the desired power
// state on EVERY transition, which is how a maintained-contact (rocker or
// latching) switch is serviced rather than a momentary one.
#define DP_PIN_TOGGLE_IN         GPIO_NUM_6

// --- Push button -----------------------------------------------------------
// Input with the internal pull-up engaged, so active low.
#define DP_PIN_BUTTON            GPIO_NUM_10

// --- Status LED ------------------------------------------------------------
// A WS2812-style device on SPI2 MOSI. The ESP32-C2 has no RMT peripheral, so
// the SPI backend is the only way to drive it.
#define DP_PIN_STATUS_LED        GPIO_NUM_4
#define DP_STATUS_LED_SPI_HOST   SPI2_HOST
#define DP_STATUS_LED_CLOCK_HZ   6000000

// --- Energy meter ----------------------------------------------------------
// Register-based metering IC. Framing is [0xA5][reg | 0x80 for writes][data...]
// [~sum]; register 0xEA gates writes (0xE5 unlocks, 0xDC re-locks). The part
// number is HLW8112, confirmed by teardown and stock analysis. See
// docs/METERING_REVERSE_ENGINEERING.md.
#define DP_PIN_METER_TX          GPIO_NUM_2
#define DP_PIN_METER_RX          GPIO_NUM_3
#define DP_METER_UART            UART_NUM_1
#define DP_METER_BAUD            9600

// --- Reserved by the C2 / module -------------------------------------------
// GPIO8, GPIO9  strapping (GPIO9 selects boot mode)
// GPIO12..17    SPI flash on the ESP8684-MINI-1
// GPIO19, 20    UART0 console (RX, TX)

#else
#error "DragonPWR supports only ESP32-C2 original and ESP32-C3 V2 targets"
#endif
