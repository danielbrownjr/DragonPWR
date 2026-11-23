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

## Historical PWR-001 acceptance notes and remaining activation gates

1. The model FAULT-latch finding was fixed and independently re-reviewed before
   PR #43 merged. Continue to test service-level failure injection before
   enabling its driver.
2. The nonblocking C3 scheduler is present; review its task lifetime and
   mutex serialization. Connect commands to the existing authenticated API
   only after driver and policy gates are reviewed.
3. Audit OFF handling on driver errors, maintenance, and startup; define an
   explicit opt-in production driver with hardware gating and runtime authorization.
4. Keep the normal C3 build inactive, preserving C2 behavior and maintenance guards.
5. Source-executing model and GPIO7 experiment host tests, C2/C3/lab builds and
   independent review passed for merged PR #43. Service-level host execution
   remains a nonblocking testing gap for this software-only foundation.
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
error-path behavior and design runtime authorization that does not silently
enable outlet switching. The placeholder-state maintenance guard was corrected
in PWR-003, but software guards cannot verify physical isolation.

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
The `pwr_control.lab_gpio7_active` field exposes software lease state, not a
live hardware measurement. Because the pulse occupies the HTTP worker, another
HTTP state request cannot normally observe a lease while it is held. HTTP routes share one server task, so a request
queued during a blocking lab pulse may only execute after it finishes.

This is a **software coordination** claim only. It is not a physical
interlock, does not make resets during HIGH safe, and does not verify
whether mains contacts are open. Production driver registration,
maintenance atomicity, real watchdog/fault response, and independent
adversarial review remain outstanding.

## Merge acceptance and deferred physical-activation work (2026-11-23)

[PWR experimental PR #42](https://github.com/danielbrownjr/DragonPWR/pull/42)
merged at `cc5fc96ba3af5824ea1c50f79c7dc7fc4b8b5e30`.
[PWR foundation PR #43](https://github.com/danielbrownjr/DragonPWR/pull/43)
merged at `1af6c364c8ab8d599b5d9bde207e861a04312a20`, after
independent review and fresh exact-head C2, C3, C3-lab and host CI. The
source tree remained identical to the accepted feature head through rebase.
Merging code is **not** evidence that an experimental image was installed,
that a hardware test occurred, or that GPIO7 is safe for unattended operation.

The following findings remain **blocking for future GPIO7 service-driver
registration**, not for the merged non-actuating foundation:

- **F3:** Deadline-triggered OFF failure lacks subsequent retry/escalation.
- **F4:** Shared dragon-core Wi-Fi/AP provisioning reboot paths bypass the
  product maintenance guard.
- **F5:** Driver ownership must be enforced centrally for *all* driver calls,
  not only the existing experimental endpoint lease.
- **F6:** Maintenance needs an atomic hold against new output commands.
- **F9:** Service/guard lock acquisition needs bounded waits and fail-closed
  handling. Source-executing service tests are also outstanding.

The lab-lease field's limited HTTP observability (F8) is documented above.
Physical reset behavior while GPIO7 is HIGH, contact topology, leakage versus
actual loaded switching, and safe output release under load are unverified.
The unloaded approximately 117 VAC HIGH reading alone must not be taken as
proof that the relay contacts closed.
