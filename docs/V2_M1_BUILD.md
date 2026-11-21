# Panda PWR V2 M1: C3 foundation and USB-only acceptance

**Status: C2 and C3 builds verified with IDF v5.3.5. USB-only runtime
acceptance passed for `ec16bf5`. Electrical qualification remains open.** See the private 2026-11-18 engineering and factory
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

## Verified native build profiles

Use an installed ESP-IDF **v5.3.5** environment.
The [native development guide](NATIVE_IDF.md) provides an installer and isolated
build wrapper. The [2026-11-18 validation](validation/2026-11-18-native-idf.md)
records both successful native builds, artifact sizes/hashes, inactive-driver
checks and the IDF v6.1.1 upstream compatibility blocker.
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

The matrix verified the generated C3 configuration has the C3 target, 40 MHz crystal,
`partitions_v2.csv`, and 4 MiB DIO. Check the partition binary against the
private V2 report and that the linked application fits each 0x1f0000 app
slot when rebuilding. The matrix verified those offsets and the binary's MD5.
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
- Keep USB-only installation and diagnostics within explicit deployment
  authorization. Qualify output behavior separately before mains or load testing.

## Issue #16: M1 board/profile acceptance reconciliation (2026-11-21)

This is a **documentation and source-evidence reconciliation**, not new hardware
characterization. The current implemented V2 foundation satisfies the *profile
separation* portion of [#16](https://github.com/danielbrownjr/DragonPWR/issues/16);
the electrically safe pin contract and broad runtime/resource qualification
remain open under [#15](https://github.com/danielbrownjr/DragonPWR/issues/15),
[safe-boot contract](V2_SAFE_BOOT_RECOVERY.md), and later M2/M7 gates.

| Acceptance dimension | Current evidence / implementation | Outstanding gate |
|---|---|---|
| Target/profile selection | Native ESP-IDF 5.3.5 C2 and C3 builds previously verified; `sdkconfig.defaults` uses C2/26 MHz and `sdkconfig.v2.defaults` uses C3/40 MHz with a distinct generated `sdkconfig`, output directory and `partitions_v2.csv`. The V1 `sdkconfig.defaults` and `partitions.csv` are unchanged by this reconciliation (last changed in `47d1b25`, before V2 work) | Regenerate and verify exact artifacts against each future head; historical builds are not current-head qualification |
| Flash/OTA layout | C3 image uses 4 MiB DIO and V2 `app0` `0x10000/0x1f0000`, `app1` `0x200000/0x1f0000`, NVS `0x9000/0x5000`, otadata `0xe000/0x2000` and SPIFFS `0x3f0000/0x10000`; previous partition binary MD5 and app/bootloader size checks passed | No tested factory stock restoration, OTA rollback, or destructive recovery. Re-verify offsets and fit on each candidate |
| Driver selection | `components/dp_relay/CMakeLists.txt` and `components/dp_button/CMakeLists.txt` select only `dp_relay_v2_inactive.c`/`dp_button_v2_inactive.c` for C3. The original C2 driver sources stay selected for C2. C3 output and restore commands fail with `ESP_ERR_NOT_SUPPORTED` and no V2 product output/input GPIO is configured by those inactive modules | Do not infer physical OFF, or that ROM/bootloader/platform peripherals leave every pad untouched |
| V2 pin/polarity provenance | Factory **static** evidence: GPIO7 AC command high means desired ON at MCU level; desired state is applied on GPIO18 falling edge. GPIO0/1 are USB-A/C commands, high means desired ON; **stock initialization drives both high and stores desired AC = ON (static evidence only)**. GPIO6 auxiliary role unknown, GPIO10 meter-side input, SET among GPIO5/8/9/19 candidates. V1 GPIO7 polarity and V1 GPIO18 USB1 assignment are **not** portable | Physical relay polarity/contact voltage, GPIO18 signal source and timing, USB rails, GPIO6/SET wiring, reset transients, calibrated metering and independent protections are unknown. No output driver may be activated |
| Platform pin ownership | ESP32-C3 GPIO18/19 map to USB Serial/JTAG D−/D+. ESP-IDF 5.3.5 **defaults** the secondary console to USB Serial/JTAG; V2 defaults do not override this, but the generated `sdkconfig` was not inspected for this reconciliation. This is **source/config inference**, not measured routing or pad voltage | Inspect **generated** `sdkconfig`, USB PHY state and PCB nets before using GPIO18 as AC-edge input or GPIO19 as a SET candidate. A future console/pin-mux decision belongs to separately scoped implementation |
| Boot sequence | `app_main`: console-baud attempt → event log → NVS initialization → relay init → button init → identity/Wi-Fi → portal → `confirm_running_image()`. The C3 relay/button steps currently return without configuring pins. Pending OTA image confirmation happens after Wi-Fi and portal **start functions return OK**, not after a network client demonstrates reachability | `init_nvs()` automatically erases default NVS on `NO_FREE_PAGES` or `NEW_VERSION_FOUND`; other init failures may reset-loop. Establish NVS backup and authorization before any fault-injection test. Reset/brownout physical states are not qualified |
| V1 compatibility | V1 source/profile/partition table remain the C2 defaults; C2 native-build, provenance and prior hardware results are documented independently | No new C2 regression execution was run for this documentation change; future mixed-target candidate requires exact-head C2 and C3 CI evidence |
| Resource budget | The November 18 operator USB-only C3 sample at firmware `ec16bf5` reported **172,924 B free heap**, **141,724 B minimum free**, **114,688 B largest block** during an AP/captive-portal session; the native-IDF validation candidate `45d5e71` measured **911,184 B app** and **21,536 B bootloader** (a different candidate). These are provenance-specific observations, not universal limits | No C3 long-soak baseline, worst-case AP/STA/OTA/diagnostic peak, fragmentation/stack margin or physical-output feature overhead established. Capture these per exact candidate before budgeting new drivers/clients; do not import the C2 memory budget or invent safety thresholds |

**M1 disposition:** Isolated C3 target/build/partition and inactive-output driver
implementation is present and source-reviewed, with previously recorded build
and USB-only runtime evidence. #16's electrical pin/polarity review is an
**evidence inventory**, not an approved drive contract. Hardware-safe boot
requirements and detailed test scenarios live in
[V2 safe-boot and recovery](V2_SAFE_BOOT_RECOVERY.md). Keep #16/M1 open until
the board contract is independently reviewed and unresolved physical gates
are explicitly assigned without treating them as passed.

## USB-only runtime acceptance, 2026-11-18

- **Installed firmware:** `ec16bf5df411a5027ed0774365eed5842015acfb`,
  embedded version `0.0.1rc1-13-gec16bf5`, ESP-IDF v5.3.5. Application
  SHA-256 `f43e04d0fb1f2bde0b4c8dc1f072c8b29f5bc78687e309f7e0895f29e9f9430c`.
  Bootloader and application passed independent readback verification.
- **Runtime:** Two controlled USB-only startups selected `app0` at `0x10000`
  and finished `app_main`. The 25- and 35-second serial captures showed no
  unexpected reset. The second-stage bootloader logged 4 MiB DIO at 80 MHz.
  The ROM startup banner and application reset-reason value were absent
  from both captures. NVS initialization returned successfully and Wi-Fi
  reported a first-boot PHY calibration save.
- **Network and heap:** Dan confirmed the Wi-Fi AP, captive portal,
  `GET /api/v2/info` and `GET /api/v2/state`. His application heap sample
  was 172,924 B free, 141,724 B minimum free and 114,688 B largest block.
- **Output command evidence:** The portal's `POST /api/v2/command` and legacy
  `POST /set` both reach `dp_relay_set`. Restore writes through
  `POST /api/v1/provisioning/product` call `dp_relay_set_restore`. The
  linked V2 implementations reject AC and USB1 ON/OFF and every valid
  restore policy with `ESP_ERR_NOT_SUPPORTED`. V2 startup configures no
  output or button GPIO through these drivers and applies no saved restore
  policy. Reset/OTA and slot-selection routes can reboot or change the
  firmware and were outside runtime testing. No MQTT or WebSocket output
  handlers were found in the application or its linked portal/network
  code. The Wi-Fi component's stock MQTT credential migration is storage
  handling. The host regression executes the actual inactive drivers
  without GPIO/NVS stubs. No output command was sent to the Panda.
- **API follow-up:** `42bd693` fixes capability/state reporting. V2
  advertises only polling, returns null switch and restore state, and
  disables the power page controls. V1 keeps its existing API fields and
  values. This separate change was not installed on the Panda. On
  `ec16bf5`, advertised switch capabilities and placeholder OFF values
  must not be used as electrical evidence.
- **Recovery:** ROM download mode responded after the first boot. The
  private verified 4 MiB backup matched the preflash device digest and
  preserves the original bootloader, partition table, OTA metadata,
  NVS, and application. Recovery uses the established USB ROM
  programming connection to write the raw backup at zero with the flash
  header settings kept, then verify the full range. No restore was run.

## Remaining electrical and recovery qualification

Measure output levels and transients through cold boot, reset, brownout,
restore, and failed update with independent physical safeguards. GPIO7
polarity/startup behavior, GPIO18 timing and role, GPIO6/SET, USB-A/C
switch behavior, and actual relay contact and outlet voltage remain
unqualified. Metering, protection, OTA fallback, and destructive factory
restore also remain outside this acceptance.
