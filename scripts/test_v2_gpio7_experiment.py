#!/usr/bin/env python3
"""Compile and run the actual opt-in GPIO7 pulse driver against host GPIO mocks."""
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
assert "dp_relay_set(target, want)" in PORTAL  # legacy path still uses inactive C3
assert "dp_v2_experiment_gpio7_pulse(level, duration)" in PORTAL

with tempfile.TemporaryDirectory(prefix="dragonpwr-gpio7-lab-") as directory:
    tmp = Path(directory)
    for directory_name in ("driver", "freertos"):
        (tmp / directory_name).mkdir()
    (tmp / "sdkconfig.h").write_text(
        "#define CONFIG_IDF_TARGET_ESP32C3 1\\n"
        "#define CONFIG_DP_V2_GPIO7_EXPERIMENT 1\\n".replace("\\\\n", "\\n")
    )
    (tmp / "esp_err.h").write_text(
        "typedef int esp_err_t;\\n#define ESP_OK 0\\n#define ESP_ERR_INVALID_ARG 0x102\\n"
        .replace("\\\\n", "\\n")
    )
    (tmp / "dp_board.h").write_text("#define DP_V2_PIN_AC_COMMAND 7\\n".replace("\\\\n", "\\n"))
    (tmp / "driver/gpio.h").write_text(
        "#define GPIO_MODE_INPUT 1\\n#define GPIO_MODE_OUTPUT 2\\n"
        "int gpio_set_level(int pin, int level);\\nint gpio_set_direction(int pin, int direction);\\n"
        .replace("\\\\n", "\\n")
    )
    (tmp / "esp_log.h").write_text(
        '#define ESP_LOGW(...) ((void)0)\\n#define ESP_LOGE(...) ((void)0)\\n'
        '#define esp_err_to_name(err) "err"\\n'.replace("\\\\n", "\\n")
    )
    (tmp / "freertos/FreeRTOS.h").write_text(
        "#define pdMS_TO_TICKS(ms) (ms)\\n".replace("\\\\n", "\\n")
    )
    (tmp / "freertos/task.h").write_text(
        "void vTaskDelay(unsigned int ticks);\\n".replace("\\\\n", "\\n")
    )
    (tmp / "test.c").write_text(r'''
#include <assert.h>
#include <stdint.h>
#include "dp_v2_experiment.h"
static int stage;
static int last_level = -1;
static unsigned int delay_ticks;
int gpio_set_level(int pin, int level) {
    assert(pin == 7 && stage == 0);
    last_level = level;
    stage = 1;
    return 0;
}
int gpio_set_direction(int pin, int direction) {
    assert(pin == 7);
    if (stage == 1) { assert(direction == 2); stage = 2; }
    else { assert(stage == 3 && direction == 1); stage = 4; }
    return 0;
}
void vTaskDelay(unsigned int ticks) {
    assert(stage == 2);
    delay_ticks = ticks;
    stage = 3;
}
int main(void) {
    assert(dp_v2_experiment_gpio7_pulse(-1, 100) != 0);
    assert(dp_v2_experiment_gpio7_pulse(2, 100) != 0);
    assert(dp_v2_experiment_gpio7_pulse(0, 99) != 0);
    assert(dp_v2_experiment_gpio7_pulse(1, 2001) != 0);
    assert(stage == 0);
    assert(dp_v2_experiment_gpio7_pulse(1, 250) == 0);
    assert(stage == 4 && last_level == 1 && delay_ticks >= 250);
    stage = 0;
    assert(dp_v2_experiment_gpio7_pulse(0, 100) == 0);
    assert(stage == 4 && last_level == 0);
    return 0;
}
''')
    cmd = [
        "cc", "-std=c11", "-Wall", "-Wextra", "-Werror",
        "-I", str(tmp), "-I", str(ROOT / "components/dp_relay/include"),
        str(DRIVER), str(tmp / "test.c"), "-o", str(tmp / "test"),
    ]
    subprocess.run(cmd, check=True)
    subprocess.run([str(tmp / "test")], check=True)
print("PASS: explicit opt-in, bounded GPIO7 pulse, release, and inactive V2 defaults")
