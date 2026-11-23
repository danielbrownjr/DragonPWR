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

## PWR-001 acceptance and remaining gates

- **Done in PR #43:** The model's FAULT-latch defect (F1) was fixed and
  independently re-reviewed; source-executing model tests and GPIO7
  experimental host tests, plus C2/C3/lab builds, passed.
- **Done in PR #43:** The new host workflow's unpinned checkout/Node.js 20
  deprecation finding (F2) was resolved by using the repository-pinned
  checkout v5 action and correcting workflow path filters.
- **Done in PR #43:** The serialized C3 model-polling service is present.
  Its independent task timing and mutex design were reviewed for this
  **non-actuating** milestone, not qualified as an electrical watchdog.
- **Standing constraint:** Keep the default C3 build inactive and retain
  existing C2 behavior; no normal C3 output-control capability is advertised.
- **Before driver activation:** Add source-executing service and failure
  injection tests, address OFF errors and runtime authorization, and finish
  the activation blockers listed below.
- **Physical qualification still required:** Characterize reset during GPIO7
  HIGH and actual contact behavior. Software OFF and input mode do not prove
  physical mains-off state.

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

Finding IDs F1–F9 refer to the independent review of PR #43; F1 (FAULT
latch) and F2 (checkout pin/Node.js 20 warning) were closed before merge.
The following findings remain **blocking for future GPIO7 service-driver
registration**, not for the merged non-actuating foundation:

- **F3 (partially mitigated by proposed PR #45):** The model now makes at most
  three further OFF attempts on subsequent scheduler polls after a failed
  deadline-triggered OFF, and retains FAULT if all fail. Recovery after
  exhaustion, bounded task scheduling, safe physical fallback and any reboot
  escalation remain **unqualified** and block future driver registration.
- **F4:** Shared dragon-core Wi-Fi/AP provisioning reboot paths bypass the
  product maintenance guard.
- **F5:** Driver ownership must be enforced centrally for *all* driver calls,
  not only the existing experimental endpoint lease.
- **F6:** Maintenance needs an atomic hold against new output commands.
- **F9 (addressed in proposed PR #46):** Service and guard mutex acquisition
  now waits at most 50 ms plus one FreeRTOS tick. Busy/unavailable callers
  fail closed, including the lease-release path; a missed monitor poll is
  logged but does not claim physical OFF. The service is exercised with
  host RTOS contention stubs. This addresses indefinite lock acquisition
  and the prior service-execution test gap, **not** a hung driver callback,
  deadline lateness or a physical fail-off guarantee.

The lab-lease field's limited HTTP observability (F8) is documented above.
Physical reset behavior while GPIO7 is HIGH, contact topology, leakage versus
actual loaded switching, and safe output release under load are unverified.
The unloaded approximately 117 VAC HIGH reading alone must not be taken as
proof that the relay contacts closed.

## PR #45: deadline-OFF retry policy (non-actuating software only)

This incremental change adds **three additional automatic OFF callbacks**
after a deadline-triggered OFF failure, one per subsequent model poll. The
software model remains in FAULT throughout unsuccessful attempts. A successful
OFF clears the retry count and returns the model to OFF; exhaustion leaves
FAULT latched and the service logs a terminal error. A deliberate OFF command
can still attempt recovery afterward.

This is a bounded **software retry budget**, not a watchdog, deadline
guarantee or power-isolation mechanism. The 10 ms nominal task interval is
not a hard timing bound; a blocked scheduler, hung driver, failed output pin
or MCU reset can leave physical AC in an unknown state. Automatic reboot is
**not implemented** because GPIO7 behavior during reset while HIGH has not
been measured. Driver registration, HTTP ON controls, additional pin access,
new production capabilities and physical tests are out of scope.

The original F3 finding is therefore **partially mitigated**, not closed for
hardware activation. F4–F6, F9 and independent contact/reset evidence remain
prerequisites. Host regression tests exercise failed and successful OFF
retries, budget exhaustion, manual recovery and initial-enable errors.

## PR #46: bounded V2 service locking (software-only)

Every `dp_v2_pwr_service` mutex request now uses a bounded FreeRTOS wait
(`pdMS_TO_TICKS(50) + 1` ticks), rather than `portMAX_DELAY`. The extra tick
avoids a zero-tick acquisition on coarser FreeRTOS configurations. The exact
elapsed wait depends on scheduling and the configured tick rate; this is
**not** a hard real-time deadline or a guarantee that a driver callback
will finish. It only bounds the waiting task's lock-acquisition attempt.

If lock acquisition fails, the service refuses set/snapshot and lab lease
acquisition, reports lab activity as unknown/active, and rejects maintenance.
A timed-out lab lease release does **not** mark the lease free and logs an
error. The independent polling task reports one error per continuous lock
contention episode rather than blocking indefinitely; missed polls can still
make a physical output remain on past its software deadline. The recovery
mechanism for a permanently wedged callback or lost lease is not established.

The host regression compiles and executes the real model and service against
FreeRTOS semaphore and timer stubs, injecting lock timeouts for each exposed
service operation and for deadline polling. Default V2 output switching is
still inactive; no GPIO driver is registered, no reboot escalation is added,
and no physical contact behavior is asserted.
