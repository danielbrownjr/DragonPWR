#!/usr/bin/env python3
"""Host-compile real GPIO7 experiment against pure GPIO/task mocks."""
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
DRIVER = ROOT / "components/dp_relay/dp_v2_gpio7_experiment.c"
PORTAL = (ROOT / "components/dp_portal/dp_portal.c").read_text()
CMAKE = (ROOT / "components/dp_relay/CMakeLists.txt").read_text()
DEFAULTS = (ROOT / "sdkconfig.v2.defaults").read_text()
LAB = (ROOT / "sdkconfig.v2.lab.defaults").read_text()

assert "CONFIG_DP_V2_GPIO7_EXPERIMENT=y" not in DEFAULTS
assert "CONFIG_DP_V2_GPIO7_EXPERIMENT=y" in LAB
assert "if(CONFIG_DP_V2_GPIO7_EXPERIMENT)" in CMAKE
assert '"/api/v2/experimental/gpio7"' in PORTAL
assert 'stored[0] == 0' in PORTAL
assert '"X-Dragon-Lab-Confirm"' in PORTAL
assert "dp_v2_experiment_gpio7_pulse(level, duration)" in PORTAL
assert "dp_relay_set(target, want)" in PORTAL

with tempfile.TemporaryDirectory(prefix="dragonpwr-gpio7-test-") as directory:
    tmp = Path(directory)
    for sub in ("driver", "freertos"):
        (tmp / sub).mkdir()

    (tmp / "sdkconfig.h").write_text("""#define CONFIG_IDF_TARGET_ESP32C3 1
#define CONFIG_DP_V2_GPIO7_EXPERIMENT 1
""")
    (tmp / "esp_err.h").write_text("""#pragma once
typedef int esp_err_t;
#define ESP_OK 0
#define ESP_ERR_INVALID_ARG 0x102
#define ESP_ERR_INVALID_STATE 0x103
""")
    (tmp / "dp_board.h").write_text("""#define DP_V2_PIN_AC_COMMAND 7
""")
    (tmp / "driver/gpio.h").write_text("""#define GPIO_MODE_INPUT 1
#define GPIO_MODE_OUTPUT 2
void gpio_dump_io_configuration(void *stream, unsigned long long mask);
int gpio_set_level(int pin, int level);
int gpio_set_direction(int pin, int direction);
""")
    (tmp / "esp_rom_gpio.h").write_text("""void esp_rom_gpio_pad_select_gpio(int pin);
""")
    (tmp / "esp_system.h").write_text("""void esp_restart(void);
""")
    (tmp / "esp_log.h").write_text("""#define ESP_LOGW(tag, ...) ((void)(tag))
#define ESP_LOGE(tag, ...) ((void)(tag))
#define esp_err_to_name(err) "err"
""")
    (tmp / "freertos/FreeRTOS.h").write_text("""#define pdMS_TO_TICKS(ms) (ms)
""")
    (tmp / "freertos/task.h").write_text("""void vTaskDelay(unsigned int ticks);
""")
    (tmp / "test.c").write_text("""
#include <assert.h>
#include <stdint.h>
#include <stdbool.h>
#include "sdkconfig.h"
#include "driver/gpio.h"
#include "dp_v2_experiment.h"
static int stage;
static int dumps;
static int leases_started, leases_ended;
static int allow_lease = 1;
static int fail_level, fail_enable;
bool dp_v2_pwr_service_lab_begin(void) {
    if (!allow_lease) return false;
    assert(stage == 0 && dumps == 0);
    ++leases_started;
    return true;
}
void dp_v2_pwr_service_lab_end(void) {
    assert(stage == 5 || (stage == 2 && fail_level));
    ++leases_ended;
}
void gpio_dump_io_configuration(void *stream, unsigned long long mask) {
    (void)stream;
    assert(mask == (1ULL << 7));
    // All synchronous dumps must happen outside the GPIO7 drive window.
    assert(stage != 3 && stage != 4);
    if (dumps == 2) assert(stage == 2);
    if (dumps == 3) assert(stage == 5);
    ++dumps;
}
void esp_rom_gpio_pad_select_gpio(int pin) {
    assert(pin == 7 && stage == 0 && dumps == 1);
    stage = 1;
}
void esp_restart(void) { assert(0 && "unexpected restart"); }
static int last_level = -1;
static unsigned int delay_ticks;
int gpio_set_level(int pin, int level) {
    assert(pin == 7 && stage == 1 && dumps == 2);
    last_level = level;
    stage = 2;
    return fail_level ? 0x105 : 0;
}
int gpio_set_direction(int pin, int direction) {
    assert(pin == 7);
    if (stage == 2) {
        assert(dumps == 3);
        assert(direction == GPIO_MODE_OUTPUT);
        stage = 3;
        if (fail_enable) return 0x105;
    } else {
        assert((stage == 4 || (stage == 3 && fail_enable)) && direction == GPIO_MODE_INPUT);
        stage = 5;
    }
    return 0;
}
void vTaskDelay(unsigned int ticks) {
    assert(stage == 3 && dumps == 3);
    delay_ticks = ticks;
    stage = 4;
}
int main(void) {
    assert(dp_v2_experiment_gpio7_pulse(-1, 100) != 0);
    assert(dp_v2_experiment_gpio7_pulse(2, 100) != 0);
    assert(dp_v2_experiment_gpio7_pulse(0, 99) != 0);
    assert(dp_v2_experiment_gpio7_pulse(1, 2001) != 0);
    assert(stage == 0 && leases_started == 0);
    allow_lease = 0;
    assert(dp_v2_experiment_gpio7_pulse(1, 250) == ESP_ERR_INVALID_STATE);
    assert(stage == 0 && leases_started == 0);
    allow_lease = 1;
    assert(dp_v2_experiment_gpio7_pulse(1, 250) == 0);
    assert(stage == 5 && last_level == 1 && delay_ticks >= 250 && dumps == 4);
    assert(leases_started == 1 && leases_ended == 1);
    stage = 0;
    dumps = 0;
    assert(dp_v2_experiment_gpio7_pulse(0, 100) == 0);
    assert(stage == 5 && last_level == 0 && dumps == 4);
    assert(leases_started == 2 && leases_ended == 2);
    stage = 0; dumps = 0; fail_level = 1;
    assert(dp_v2_experiment_gpio7_pulse(1, 150) != 0);
    assert(stage == 2 && leases_started == 3 && leases_ended == 3);
    stage = 0; dumps = 0; fail_level = 0; fail_enable = 1;
    assert(dp_v2_experiment_gpio7_pulse(1, 150) != 0);
    assert(stage == 5 && leases_started == 4 && leases_ended == 4);
}
""")
    cmd = [
        "cc", "-std=c11", "-Wall", "-Wextra", "-Werror",
        "-I", str(tmp), "-I", str(ROOT / "components/dp_relay/include"),
        str(DRIVER), str(tmp / "test.c"), "-o", str(tmp / "test"),
    ]
    subprocess.run(cmd, check=True)
    subprocess.run([str(tmp / "test")], check=True)

print("PASS: opt-in GPIO7 HIGH/LOW pulse and finite input release; default inactive")
