# DragonPWR V2 roadmap (provisional)

This is an additive V2 planning track. The existing [V1 roadmap](ROADMAP.md)
and V1 behavior remain unchanged. The M0–M7 breakdown below is proposed here;
an earlier proposed breakdown was not available during setup on 2026-11-18.
Issues define future work, not authorization to implement firmware or operate hardware.

## Evidence gate and status

**M0 is open.** No V2 investigation reports or V2-specific factory/recovery
artifacts were available in the execution workspace. No V2 hardware claim is
accepted by copying V1 findings. Reconcile the original investigation privately
before adopting any pin, partition, bootloader, USB or metering assumptions.
Private reports, stock binaries, NVS, credentials and identifying captures must
remain outside the public repository and issues.

Planning completed: inventory of the current source, branches, issues, PRs and
milestones; additive roadmap and actionable issue setup. This is inspection and
planning evidence only, not completion of the V2 hardware evidence baseline.

## Milestones and scoped issues

Sequence is a dependency guide, not a schedule. There are no invented due dates.
M1 follows M0; output and metering work use M1's reviewed board contract; policy
uses output and printer observations; qualification follows implemented capabilities.

| Gate | Exit criterion | Issues |
|---|---|---|
| [V2-M0 — Evidence baseline](https://github.com/danielbrownjr/DragonPWR/milestone/1) | Reconcile V2 identity, private investigation provenance, recovery assets and hardware contracts. Close only after V2-specific evidence is reviewed. | [#14](https://github.com/danielbrownjr/DragonPWR/issues/14), [#15](https://github.com/danielbrownjr/DragonPWR/issues/15) |
| [V2-M1 — Board and safe boot contract](https://github.com/danielbrownjr/DragonPWR/milestone/2) | Define a separate V2 board/build profile and safe initialization from reviewed M0 evidence, preserving V1. | [#16](https://github.com/danielbrownjr/DragonPWR/issues/16), [#17](https://github.com/danielbrownjr/DragonPWR/issues/17) |
| [V2-M2 — Outputs and local controls](https://github.com/danielbrownjr/DragonPWR/milestone/3) | Implement bounded V2 relay, USB and button behavior with centralized control and explicit restore semantics. | [#18](https://github.com/danielbrownjr/DragonPWR/issues/18) |
| [V2-M3 — Metering](https://github.com/danielbrownjr/DragonPWR/milestone/4) | Implement and validate the V2 metering transport and conversions only after confirming its chip and interface. | [#19](https://github.com/danielbrownjr/DragonPWR/issues/19), [#20](https://github.com/danielbrownjr/DragonPWR/issues/20) |
| [V2-M4 — Printer observation](https://github.com/danielbrownjr/DragonPWR/milestone/5) | Integrate printer observation with explicit unknown/stale/absent data semantics and resource budgets. | [#21](https://github.com/danielbrownjr/DragonPWR/issues/21) |
| [V2-M5 — Power policy](https://github.com/danielbrownjr/DragonPWR/milestone/6) | Centralize printer-aware shutdown interlocks and cooldown policy across all control paths. | [#22](https://github.com/danielbrownjr/DragonPWR/issues/22), [#23](https://github.com/danielbrownjr/DragonPWR/issues/23) |
| [V2-M6 — API and integrations](https://github.com/danielbrownjr/DragonPWR/milestone/7) | Expose V2 capabilities through authenticated API/UI and bounded LAN integrations. | [#24](https://github.com/danielbrownjr/DragonPWR/issues/24), [#25](https://github.com/danielbrownjr/DragonPWR/issues/25) |
| [V2-M7 — Qualification and recovery](https://github.com/danielbrownjr/DragonPWR/milestone/8) | Collect exact-candidate build, resource, recovery and separately authorized physical qualification evidence before release consideration. | [#26](https://github.com/danielbrownjr/DragonPWR/issues/26), [#27](https://github.com/danielbrownjr/DragonPWR/issues/27) |

## Existing architecture and reuse candidates

- [Architecture](ARCHITECTURE.md): product hardware and power policy belong here;
  shared networking, provisioning, logs and UI services belong in dragon-core.
- [Board contract](../components/dp_board/include/dp_board.h),
  [relay](../components/dp_relay/dp_relay.c),
  [button](../components/dp_button/dp_button.c) and
  [boot sequence](../main/app_main.c): review V1's inactive-level preload before
  output configuration and default-off restore policy as reusable patterns.
  GPIO readback is not measurement of outlet voltage or relay contacts.
- [Portal](../components/dp_portal/dp_portal.c): API/auth, maintenance guards and
  [temporary UI](../components/dp_portal/power.html). Printer-aware shutdown policy
  does not exist on main and needs a product-level owner across command paths.
- [V1 hardware evidence](HARDWARE_ANALYSIS.md), [bench notes](BENCH_NOTES.md),
  [metering analysis](METERING_REVERSE_ENGINEERING.md) and
  [analysis tools](../analysis/README.md): comparison/reproduction references;
  neither V1 chip identity nor its board conversion constant establishes V2 behavior.
- [PR #9](https://github.com/danielbrownjr/DragonPWR/pull/9) already carries
  printer-observation work. Reconcile it before duplicating printer clients;
  preserve its separate review and V1 acceptance boundaries.
- [Manifest](../main/idf_component.yml), [lock](../dependencies.lock),
  [defaults](../sdkconfig.defaults) and [partition table](../partitions.csv)
  describe V1. Any V2 selection must be explicit and preserve V1 defaults.

## M0 acceptance

Both M0 issues must pass against the actual V2 evidence before closing M0:

- Exact product/revision and report provenance, with private recovery coverage
  and integrity checks; never publish full-flash/NVS data.
- V2 partition names/types/offsets/sizes and bootloader provenance, flash settings
  and rollback behavior, distinguishing static inspection from runtime proof.
- GPIO7/GPIO18 direction, polarity, ordering and timing with evidence references;
  control sequence and reset behavior must not be inferred from V1.
- USB service/power topology and switched/unswitched capabilities; metering chip,
  bus, pins, framing and configuration side effects.
- Explicit unknowns and implementation prerequisites. Safety-critical physical
  checks remain distinct from source inspection and target compilation.

## Verification and stopping boundaries

Every implementation issue needs a separately scoped implementation task.
Record exact source/dependency heads, target/configuration and artifact hashes.
Keep static analysis, host tests, target compile, linked artifacts and physical
acceptance separate. Green CI from V1 or a previous head does not qualify V2.
Do not flash/erase, energize hardware, merge, tag or release under roadmap setup.
Stop roadmap setup after documentation and issue organization, reporting any
missing M0 evidence instead of silently advancing into firmware work.
