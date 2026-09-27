# DragonPWR Wiki

DragonPWR is open firmware for the BIGTREETECH Panda PWR, built on pinned
dragon-core components.

**Current status:** Phase 1 is bench-verified on real hardware. Mains and USB1
switching, safe boot, restore policy, Wi-Fi, portal, browser OTA, maintenance,
stock-compatible routes and the control token work. The HLW8112 stock metering
path is fully reverse-engineered; live `dp_meter` support is the next product
slice. Printer-aware power policy comes after that.

## Start here

- **Installing or recovering a unit:** [Installation and Recovery](Installation-and-Recovery)
- **How the firmware is divided:** [Architecture](Architecture)
- **HTTP integration:** [API](API)
- **Complete documentation map:** [Documentation Index](Documentation-Index)

## Hardware and reverse engineering

- [Hardware Analysis](Hardware-Analysis) — pin map, teardown, identified parts,
  evidence levels and open board questions
- [Metering Reverse Engineering](Metering-Reverse-Engineering) — HLW8112 UART,
  registers, calibration coefficients, conversion formulas, energy behavior,
  timing and the future `dp_meter` contract
- [Bench Notes](Bench-Notes) — chronological real-hardware sessions
- [Stock Firmware Analysis Tools](Stock-Firmware-Analysis-Tools) — reproducible
  scripts used to recover the stock behavior

## Project direction

- [Roadmap](Roadmap) — current phase, RAM/flash budget, future printer awareness,
  metering, integrations and deliberate non-goals
- [Project Overview](Project-Overview) — repository README and quick-start

## Source of truth

The wiki is generated from Markdown committed in the DragonPWR repository.
Direct wiki edits can be overwritten by the next publish. If something is wrong,
fix the canonical repository document so the evidence and the wiki stay in lockstep.
