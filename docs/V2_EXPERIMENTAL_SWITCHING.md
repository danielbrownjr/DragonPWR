# V2 experimental switching candidate: scope and go/no-go gates

**Status:** pre-implementation specification for [#18](https://github.com/danielbrownjr/DragonPWR/issues/18). Dan approved pursuing an **experimental candidate** on 2026-11-21. This is **not** a released or flashable switching image, does not authorize mains operation, and does not waive hardware qualification. The existing ESP32-C3 inactive-output build remains the default.

## Objective

Develop a deliberately opt-in ESP32-C3 laboratory candidate capable of **explicitly commanding AC OFF/ON and observing actual outlet behavior** in a separately approved, enclosed, unloaded or appropriately rated external test fixture. No board disassembly. Avoid accidental activation via existing REST/legacy routes or automated restore while fundamental electrical behavior remains unknown.

## Evidence and contradiction to resolve

- The **factory firmware static analysis** indicates AC GPIO7 high = commanded ON at MCU level; the desired state initializes ON but the GPIO18 falling-edge handler applies the pending GPIO7 state. It drives USB-A/C GPIO0/1 high at stock initialization.
- Dan subsequently reported **about 240 V AC measured at the outlet when mains was connected** with stock firmware. This is an external observation, not proof of contact closure, timing, load capability, or the mechanism of energization; the firmware has since been replaced on the original unit.
- ESP-IDF v5.3.5 assigns ESP32-C3 GPIO18/19 to USB Serial/JTAG D−/D+; the secondary console defaults to that peripheral unless the generated configuration differs. The inactive C3 product drivers don't touch these pads, but the platform might.
- The currently merged C3 build rejects `dp_relay_set()` and restore writes, returns placeholder OFF, hides output-control capability, and uses `dp_button_v2_inactive.c`.
- The full private factory flash backup is verified as a **source image**, but a completed **stock restore and rollback have never been demonstrated**. NVS initialization can erase credentials for specific errors.

See [safe-boot and recovery contract](V2_SAFE_BOOT_RECOVERY.md) and [V2 board profile](V2_M1_BUILD.md). Do not publish raw firmware, private NVS or device identifiers.

## Two-stage implementation, not one unguarded build

### Stage A: reviewable software-only prototype

- Keep **C3 production/default selection on the inactive driver**. Prototype code must not be selected by default, shipped as a public release binary, or reachable through a mistaken C2 configuration.
- Introduce an explicit experimental build-selection gate with a clearly distinguishable binary/version marker. A C3 experimental build must be impossible to mistake for the normal inactive build. Reject ambiguous combinations at configure/build time.
- Exercise a deterministic, host-testable control state machine with **inhibited initialization**, explicit operator command arbitration, no autonomous ON or persisted LAST/ON restoration, rejection of invalid outputs and failures, and maintenance/restart protection.
- Treat output state as `unknown` without independent verification. Do **not** make existing `dp_relay_get() == false` a physical OFF proof. Keep C3 UI/API output controls disabled until their experimental routes and maintenance guards have been separately reviewed.
- Test the C2 target and C3 inactive target against current behavior; add negative tests proving nonexperimental C3 artifacts include no experimental actuators. Do not weaken current CI inactive-driver checks to make prototype builds pass.
- This stage does **not** drive GPIO7, GPIO18, GPIO0, GPIO1, GPIO6, GPIO19 or other unqualified nets and does not produce a hardware-test authorization.

### Stage B: opt-in actuation candidate, **blocked pending electrical/source gate**

Before any build capable of energizing a mains path or being flashed for switching:

1. Determine a defensible GPIO18 handling strategy from manufacturer information or equivalently strong evidence. In particular, do not assume GPIO18 is zero-cross, and do not let a stock-edge hypothesis silently turn into a timer-driven GPIO7 write.
2. Verify generated C3 `sdkconfig`, USB Serial/JTAG ownership of GPIO18/19, strapping risks, and console recovery path. Any peripheral reassignment must be explicit and reviewed.
3. Define the commanded OFF electrical level separately from **observed** outlet OFF, and characterize default reset/power-loss behavior using approved non-invasive evidence; do not blindly reuse V1 GPIO7 active-low preload.
4. Decide exactly which output(s) are in scope. Start with AC only; keep USB-A/C and SET disabled until separately qualified. Specify operator-only control, bounded command duration/sequence, a fail-closed policy, and how to abort without relying on a browser.
5. Ensure an authorized recovery plan for the exact C3 image, including known ROM access, verified source backup, credentials/NVS preservation as needed, expected partition map and postflash readback. A *hypothetical* stock restore is not a verified fallback.
6. Review all entry points: `POST /api/v2/command`, `POST /set`, provisioning restore, local inputs, UI capability flags, OTA/reboot and boot inactive-slot endpoints. Prevent any path from bypassing experimental command authorization or operating on an unknown physical state.
7. Produce exact-head ESP-IDF 5.3.5 C2/C3 builds and test reports; obtain independent source and electrical-safety reviews **before** asking for a particular flash/energized test procedure.
8. Carry out only separately authorized, externally enclosed, mains-rated test procedures with a recorded firmware digest, protective setup, no printer/load initially, abort criteria and measurements. A DMM voltage observation alone does not establish contact closure or switching transient safety.

## Explicit acceptance and stop

- **Stage A success:** dedicated software-only prototype reviewed, host tests covering state transitions and legacy-route refusal, unchanged default C2/C3 binaries, exact-head native build evidence, and no linkable actuation in default images.
- **Stage B success:** reviewed opt-in driver and guarded control interface, an approved specific external test plan, then actual recorded measurements of commanded ON/OFF, cold startup, restart, and power fault at the output. *Only observed tests count.*
- **Stop immediately** if GPIO18 purpose/ownership, relay polarity or safe default remains ambiguous; an unexpected outlet voltage/state appears; source and generated config disagree; a recovery image cannot be verified; or the test fixture/authorization is inadequate.
- A passed host test is not a mains safety claim. No tags, deployment, flash, powered tests or merge are granted by this document.
