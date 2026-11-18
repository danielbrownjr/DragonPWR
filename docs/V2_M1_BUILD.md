# Panda PWR V2 M1: build-only C3 foundation

**Status: provisional source implementation, NOT target-build-verified or hardware-accepted.**
This document describes the separate V2 ESP32-C3 build profile. It does not
authorize flashing. See the private 2026-11-18 engineering and factory
reverse-engineering reports for full provenance; do not put the raw factory
image or NVS in this repository.

## Board separation

- Original Panda PWR is ESP32-C2/ESP8684 with a **26 MHz crystal**,
  `sdkconfig.defaults`, `partitions.csv`, and its existing active-low
  GPIO7 relay / GPIO18 USB1 code.
- Panda PWR V2 is ESP32-C3 (ROM identified), **40 MHz crystal**, 4 MiB DIO.
  Use `sdkconfig.v2.defaults` and `partitions_v2.csv`. OTA slots are
  `0x10000..0x1fffff` and `0x200000..0x3effff` (each 0x1f0000).
- The V2 build compiles **non-actuating** `dp_relay_v2_inactive.c` and
  `dp_button_v2_inactive.c`, rather than original-board GPIO drivers.
  All `dp_relay_set()` calls return `ESP_ERR_NOT_SUPPORTED` for a valid
  output, even if an HTTP endpoint attempts to request ON.
  `dp_relay_get()` always returns false as a placeholder; it is **not**
  physical contact or outlet readback. V2 reports from pre-existing portal
  endpoints should not be interpreted as measured output states.
  No V2 GPIO is configured by either inactive component.
- V2 `dp_board.h` macros are evidence labels rather than permission to
  configure pins. AC GPIO7/GPIO18 behavior and SET are unqualified.

## Proposed native build commands (not yet executed)

Use an installed ESP-IDF **v5.3.5** environment and a clean checkout.
Keep output directories and sdkconfig files distinct; do not reuse a C2
`sdkconfig` in a C3 build.

Original C2 baseline, in its **own** build workspace:

```sh
idf.py -B build-c2 -D SDKCONFIG=sdkconfig.c2 -D SDKCONFIG_DEFAULTS=sdkconfig.defaults set-target esp32c2
idf.py -B build-c2 -D SDKCONFIG=sdkconfig.c2 -D SDKCONFIG_DEFAULTS=sdkconfig.defaults build
```

V2 C3, using the explicit alternative defaults:

```sh
idf.py -B build-v2 -D SDKCONFIG=sdkconfig.v2 -D SDKCONFIG_DEFAULTS=sdkconfig.v2.defaults set-target esp32c3
idf.py -B build-v2 -D SDKCONFIG=sdkconfig.v2 -D SDKCONFIG_DEFAULTS=sdkconfig.v2.defaults build
```

Verify generated `sdkconfig.v2` has the C3 target, 40 MHz crystal,
`partitions_v2.csv`, and 4 MiB DIO. Check the partition binary against the
private V2 report and that the linked application fits each 0x1f0000 app
slot. If the IDF CLI rejects these invocations or selects another target,
correct the instructions based on actual tool output before claiming success.
Do not alter the pinned dragon-core dependencies casually.

## M1 acceptance checks

- Both C2 and C3 native target builds pass with no unresolved warnings.
- C2 still selects its original relay/button implementation and V1 partitions.
- C3 selects only inactive outputs/buttons; disassembly or link inspection
  confirms no reachable output-control GPIO calls from those components.
- Verify portal output requests fail and never report a successful state
  transition on V2; assess maintenance and startup paths before flashing.
- Check bootloader, flash parameters, OTA table and image size of each
  generated firmware artifact.
- No V2 image is flashed or presented as electrically safe before separate
  physical characterization and explicit deployment authorization.

## Deliberately deferred

GPIO18 edge synchronization/feedback, GPIO7 physical polarity and startup
transients, USB-A/C output actuation, GPIO6 role, SET wiring, metering readout,
power protection, calibrated data and recovery/rollback testing.

Factory readback and a working USB ROM interface establish a useful recovery
path but do **not** demonstrate a tested restoration or a safe replacement
firmware startup.
