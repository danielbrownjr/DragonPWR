# DragonPWR documentation index

This directory is the canonical technical record for DragonPWR. The GitHub Wiki
is a generated reading surface; edit these source documents in the repository,
not the generated wiki copies.

## Current product status

DragonPWR is at a bench-verified Phase 1 baseline on real Panda PWR hardware:

- ESP8684 / ESP32-C2 target boots and provisions through dragon-core.
- Mains relay and USB1 switching are verified.
- Safe boot and power-restore policy are implemented.
- Product API, stock-compatible routes, control-token authorization, event log,
  browser OTA, maintenance routes and the temporary `/power` UI are implemented.
- The HLW8112 metering hardware and stock metering algorithm are recovered, but
  `dp_meter` is not implemented yet.
- Printer integration and the print-aware power interlock are not implemented yet.

Open hardware questions remain intentionally recorded. They are not documentation
gaps: where a component or trace is unverified, the docs say so.

## Read by task

| Need | Canonical document |
|---|---|
| Install DragonPWR, back up stock, recover a unit | [Installation & Recovery](INSTALLATION_AND_RECOVERY.md) |
| Understand boot order and component boundaries | [Architecture](ARCHITECTURE.md) |
| Integrate against the HTTP surface | [API](API.md) |
| See the hardware pin map and teardown evidence | [Hardware Analysis](HARDWARE_ANALYSIS.md) |
| Implement or review metering | [Metering Reverse Engineering](METERING_REVERSE_ENGINEERING.md) |
| Follow real bench evidence chronologically | [Bench Notes](BENCH_NOTES.md) |
| See planned work and deliberate non-goals | [Roadmap](ROADMAP.md) |
| Reproduce stock-firmware analysis | [analysis/README.md](../analysis/README.md) |
| Maintain the GitHub Wiki mirror | [Wiki publishing](WIKI.md) |

## Authority and chronology

- **README.md** is the project front door and quick-start.
- **Architecture/API/Installation** describe current DragonPWR behavior.
- **Hardware Analysis** is the consolidated hardware reverse-engineering record.
- **Metering Reverse Engineering** is the consolidated stock-HLW8112 behavior
  record and the implementation contract for the future `dp_meter`.
- **Bench Notes** are chronological lab notes. Later sessions supersede earlier
  hypotheses where the two conflict.
- **Roadmap** distinguishes shipped behavior from future phases.

Do not promote an inference from the bench log into a confirmed hardware fact
without updating the consolidated document that owns that fact.
