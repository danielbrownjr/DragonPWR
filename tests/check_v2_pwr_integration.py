#!/usr/bin/env python3
"""Static negative contract: no ordinary C3 V2 mains actuation path."""
from pathlib import Path

relay = Path("components/dp_relay/CMakeLists.txt").read_text()
service = Path("components/dp_relay/dp_v2_pwr_service.c").read_text()
main = Path("main/app_main.c").read_text()
portal = Path("components/dp_portal/dp_portal.c").read_text()
candidate = Path("components/dp_relay/dp_v2_gpio7_driver.c").read_text()
experiment = Path("components/dp_relay/dp_v2_gpio7_experiment.c").read_text()
service_header = Path("components/dp_relay/include/dp_v2_pwr_service.h").read_text()

assert 'dp_relay_v2_inactive.c' in relay
assert 'dp_v2_pwr_service.c' in relay
assert 'dp_v2_gpio7_driver.c' in relay
assert 'if(CONFIG_DP_V2_GPIO7_EXPERIMENT)' in relay
assert 'dp_v2_pwr_model_enable(' not in service
assert 'dp_v2_gpio7_drive(' not in service
assert 'dp_v2_pwr_service_start()' in main
assert 'cJSON_AddNullToObject(outputs, "mains")' in portal
assert 'cJSON_AddNullToObject(outputs, "usb1")' in portal
assert 'dp_relay_set(target, want)' in portal
assert 'return ESP_ERR_NOT_SUPPORTED;' in Path("components/dp_relay/dp_relay_v2_inactive.c").read_text()
assert 'gpio_set_direction(DP_V2_PIN_AC_COMMAND, GPIO_MODE_INPUT)' in candidate
assert 'dp_v2_pwr_service_lab_begin()' in experiment
assert 'dp_v2_pwr_service_lab_end()' in experiment
assert 'dp_v2_pwr_service_maintenance_allowed()' in portal
assert 'dp_v2_pwr_service_lab_active()' in portal
assert 'dp_v2_pwr_service_lab_begin(void)' in service_header
assert 's_lab_active = true;' in service
assert 's_lab_active = false;' in service
assert 's_lab_active ? DP_V2_PWR_UNAVAILABLE' in service
assert '!s_lab_active && !s_model.enabled' in service
assert 'dp_v2_gpio7_drive(' not in experiment
print("V2 PWR wiring contract: PASS (not a substitute for compilation or physical tests)")
