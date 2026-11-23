# Panda PWR V2: relay-control integration

## PWR-001 control-model foundation (software only)

The V2 output's unloaded bench characterization found approximately 117 VAC
during GPIO7 HIGH, no observable response to LOW, and approximately 1–2 VAC
after relinquishing GPIO7. These are observations, not proof of physical contact
isolation, loaded switching, reset timing, or fault behavior.

This first step adds an independently host-testable, ESP-IDF-free command model
in `components/dp_relay/dp_v2_pwr_model.{c,h}`. It **does not connect the model to GPIO7 or the ordinary product API**.
The C3 build starts a serialized FreeRTOS service with an independent 10 ms
model deadline polling task, but **no driver callback is installed**. ON
requests therefore remain unavailable. The model snapshot is software-only.
The existing C3 inactive relay backend and the separate opt-in GPIO7
experimental pulse route remain unchanged. No new actuation path is exposed.

The model begins disabled and OFF regardless of persistence. A future reviewed
driver must accept OFF before enable; ON requires a bounded duration and cannot
silently extend an existing deadline. OFF is idempotent, driver errors latch
FAULT, and expiry requests OFF when polled. The model records software state,
**never** physical AC contact state.

## Remaining integration gates

1. Review model API, including serialization, failure injection and fault clearing.
2. The nonblocking C3 scheduler is present; review its task lifetime and
   mutex serialization. Connect commands to the existing authenticated API
   only after driver and policy gates are reviewed.
3. Audit OFF handling on driver errors, maintenance, and startup; define an
   explicit opt-in production driver with hardware gating and runtime authorization.
4. Keep the normal C3 build inactive, preserving C2 behavior and maintenance guards.
5. Add source-executing host tests, C2/C3 exact-head build matrix and independent
   review before any hardware flash.
6. Characterize MCU reset during HIGH with external capture when available.
   Do not assert physical OFF from software intent or GPIO input mode.

Host-only check:

```sh
cc -std=c11 -Wall -Wextra -Werror -pedantic \
  -I components/dp_relay/include \
  components/dp_relay/dp_v2_pwr_model.c tests/test_v2_pwr_model.c \
  -o /tmp/test_v2_pwr_model
/tmp/test_v2_pwr_model
```

The running service periodically polls its model independently of HTTP, but
without a hardware driver its timing is not a physical fail-off guarantee.
Neither the model nor service is a basis for enabling mains output.

## PWR-002 staged GPIO7 backend (not activated)

The review-only `dp_v2_gpio7_driver.c` candidate compiles **only** in the
ESP32-C3 laboratory configuration. It provides a callback with HIGH preload
before output enable and an INPUT-direction release request, matching the
bounded experimental observations. It is **not registered** with the service,
so the default and lab builds' ordinary PWR controls remain unavailable.
The existing experimental HTTP pulse route remains separate.

C3 `GET /api/v2/state` now reports a `pwr_control` object with software
`enabled` and `requested_state`, plus `physical_ac_state: null`.
The ordinary `outputs.mains` and `outputs.usb1` values remain null. No
`power_switch` capability is advertised and existing switch endpoints remain
nonactuating.

Before considering driver registration, independently review reset-time and
error-path behavior, resolve maintenance guard's placeholder state, and design
a runtime authorization method that does not silently enable outlet switching.

## PWR-003: GPIO7 ownership and maintenance interlocks

The existing laboratory GPIO7 pulse acquires an exclusive lease from the
C3 PWR service before selecting or driving the pad. The lease remains held
through the pulse and pin-release request. Failed commands release it on
their error paths; a failed final INPUT-direction request triggers reboot
as in the existing experiment, without claiming any physical off state.

Future service-backed output switching must be serialized under the same
ownership boundary; attaching a driver without extending the arbitration
is prohibited. The ordinary C3 relay backend still returns unsupported,
the service still installs **no GPIO driver callback**, and the candidate
driver is still unconnected.

Firmware restart, OTA, factory reset, and boot-inactive guards on C3
consult the service model and lab lease rather than calling the
inactive relay's placeholder `dp_relay_get()`. They refuse maintenance
when the model is enabled, active, faulted, in lab use, or unavailable.
The `pwr_control.lab_gpio7_active` field exposes lease activity as
**software-only** status. HTTP routes share one server task, so a request
queued during a blocking lab pulse may only execute after it finishes.

This is a **software coordination** claim only. It is not a physical
interlock, does not make resets during HIGH safe, and does not verify
whether mains contacts are open. Production driver registration,
maintenance atomicity, real watchdog/fault response, and independent
adversarial review remain outstanding.
