# Hephaestus native ESP-IDF validation — 2026-11-18

## Decision

Heph can natively build both DragonPWR board profiles using **ESP-IDF v5.3.5**.
Retain that release as the primary development baseline for pinned dragon-core
v0.30.0. **v6.1.1**, the latest stable release reported by Espressif's release
API on this date, supports both chips but is blocked by upstream component
requirements. It is installed for evaluation, not selected for production.
This comparison does not establish that every intervening 5.x release is
incompatible, nor certify v5.3.5 as free of security defects.

Scope: installation, source/static checks, host test, native target compilation,
linked artifacts and partition validation. No flashing, erasure, device access,
physical switching, merge or release was performed. V2 actuation remains disabled.

## Provenance and host

- Repository: `danielbrownjr/DragonPWR`, branch `feat/v2-c3-board-foundation`,
  draft [PR #29](https://github.com/danielbrownjr/DragonPWR/pull/29).
- Initial synchronized source: `05255bcd2895a391a031f7f90b5efb5a1da8e13d`.
- Native four-cell validation input HEAD:
  `45d5e71ee723f76952bc5fdf1c0be63c4c22f984`. Subsequent changes
  are the artifact verifier, developer/report documentation and a root lock
  metadata refresh; no C firmware implementation changed.
- Host: Ubuntu 26.04.1 LTS, Linux x86-64, kernel `7.0.0-43-generic`;
  approximately 3.4 TiB disk free initially, 14 GiB RAM and 4 GiB swap.
- Initially no native IDF installation was found. System Python 3.14.4,
  CMake 4.2.3, Ninja 1.13.2, Git, host GCC and gzip were already available.
  Builds succeeded with that CMake/Ninja pair. The release manifests recommend
  CMake 3.30.2 (5.3.5), CMake 4.0.3 (6.1.1), and Ninja 1.12.1.
- Existing `uv` installed user-owned CPython **3.11.18**. System Python,
  host packages, services, networking, permissions and shell rc files were unchanged.

## Installed environments

| Item | Baseline | Candidate |
|---|---|---|
| IDF tag | v5.3.5 | v6.1.1 |
| IDF source commit | `0c45a102a96e82000f145b786a20de229b9f8557` | `051e4c4eb20346e043cdd47bc388fde75ff8af63` |
| RISC-V toolchain | `esp-13.2.0_20250707` | `esp-15.2.0_20251204` |
| GCC / G++ | 13.2.0 | 15.2.0 |
| GNU linker | 2.41 | 2.45 |
| RISC-V GDB package | `16.3_20250913` | `17.1_20260402` |
| Python | 3.11.18 | 3.11.18 |
| Tools/venv root | `~/.espressif-v5.3.5` | `~/.espressif-v6.1.1` |
| IDF source | `~/esp/esp-idf-v5.3.5` | `~/esp/esp-idf-v6.1.1` |

Both are exact Espressif tags with recursively pinned submodules. Tools were
installed with official `install.sh esp32c2,esp32c3`, using each release's own
`tools.json`, Python requirements and downloaded constraints. The repository's
`setup_idf.sh` was subsequently executed successfully against both installed
releases, exercising its repeat-install path. Bash and Zsh activation were checked.
No EIM or system-wide pip installation was needed; Espressif still documents
this native legacy workflow for v6.1.1.

Activate each in a fresh shell:

```sh
. "$HOME/esp/activate-idf-v5.3.5.sh"
# Or, in a separate shell:
. "$HOME/esp/activate-idf-v6.1.1.sh"
```

See [developer instructions](../NATIVE_IDF.md) for installation and rebuild commands.
Full compiler/linker and Python package inventories are retained locally in
`~/.local/state/dragonpwr-idf/versions-5.3.5.txt` and `versions-6.1.1.txt`.

## Four-configuration build matrix

Actual file sizes in bytes, rather than the slightly smaller ELF section sum:

| Hardware/profile | IDF | Compiler/linker result | App `.bin` | Bootloader `.bin` | Partition validation |
|---|---|---|---:|---:|---|
| V1 ESP32-C2 | 5.3.5 | PASS, GCC 13.2.0 / ld 2.41 | 796,544 | 19,856 | PASS, binary MD5 and all factory offsets; app fits 1,310,720-byte slots |
| V2 ESP32-C3 | 5.3.5 | PASS, GCC 13.2.0 / ld 2.41 | 911,184 | 21,536 | PASS, binary MD5 and all recovered offsets; app fits 2,031,616-byte slots |
| V1 ESP32-C2 | 6.1.1 | BLOCKED during CMake component resolution | N/A | N/A | No product binary generated |
| V2 ESP32-C3 | 6.1.1 | BLOCKED during CMake component resolution | N/A | N/A | No product binary generated |

Both baseline compile/link logs contain **zero warning diagnostics**. The
`idf.py size` section totals were 796,438 bytes (C2) and 911,074 bytes (C3).
Both partition files are 3,072 bytes; both bootloaders fit before the partition
table at `0x8000`. Generated flash settings are DIO, 4 MiB and 80 MHz.
C2 retains its 26 MHz crystal profile; C3 selects 40 MHz.

The candidate's installed compiler/linker versions were queried successfully,
but product compilation/linking did not run after configuration failed. No
candidate warning-free build, app size, bootloader size or linked-hardware
acceptance is claimed. The candidate SDK's `esp32c2`/`esp32c3` support is
separate from DragonPWR's dependency compatibility.

Each cell used `build/native/<release>/<c2|v2>/source`, its own sdkconfig,
managed components, Component Manager cache and output directory. Build
invocation: activate the chosen release, then `python scripts/native_build.py
c2` or `python scripts/native_build.py v2`. The wrapper executes reconfigure,
Ninja with four workers, then IDF size reporting. Input-file hashes and HEAD
are retained per cell. The validated logs are
`~/.local/state/dragonpwr-idf/build-<version>-<board>-validated.log`.

The optional workflow host-test step and trigger changes were removed after
GitHub rejected publication with the available OAuth credential's missing
`workflow` scope. The existing workflow is preserved. The host test remains
available through the documented local command.

## Dependency discipline and candidate failure

All four final resolutions retained these same source pins:

- dragon-core v0.30.0, annotated tag object
  `8d5a61de8544a5b82cc3c6d9c7dc593ce06bb8bd`, peeled source commit
  `4c04c7a1ffc5c61c31e66313af9515c45f6fba0c`;
- existing component content hashes for `dc_evlog`, `dc_wifi`, `dc_ui` and
  `dc_portal` unchanged;
- mDNS **1.12.0**, content hash
  `3ba256ac95e07c274be53cbd73f06cb846c403b61e8fbdf1be57bdb79db7a63e`.

Adding the conditional cJSON manifest caused dependency re-resolution, which
initially selected newer mDNS 1.15.1 in candidate scratch profiles. That was
corrected by explicitly constraining the pre-existing 1.12.0 revision before
the final matrix. Root `dependencies.lock` changed only its manifest hash and
direct-dependency metadata; no existing version or component content hash changed.
Candidate scratch lockfiles use Component Manager's newer lock format and
record IDF 6.1.1 plus cJSON 1.7.19; they do not replace the baseline lock.

**Observed candidate blocker on both chips after DragonPWR corrections:**

```text
Failed to resolve component 'json' required by component 'dc_portal':
unknown name.
```

This is upstream: pinned dragon-core's `components/dc_portal/CMakeLists.txt:4`
requires the built-in `json` component removed in IDF 6.0. Its manifest's
`idf: ">=5.3"` declaration does not guarantee compatibility with later majors.
The same requirement initially existed in DragonPWR's own `dp_portal`; that
local requirement was corrected and the final failure identifies upstream.

**Additional incompatibility established by source inspection, not reached
by the product compiler:** pinned `dc_portal.c:21` includes
`mbedtls/sha256.h`, and its OTA upload implementation at lines 441–459 uses
`mbedtls_sha256_context`, init, starts, update, finish and free. IDF 6.1.1's
Mbed TLS 4/TF-PSA source no longer supplies that public header/API. The private
internal SHA header is not a supported replacement.

**Smallest upstream proposal, not implemented:**

1. In `dc_portal` CMake and its manifest, conditionally use exact registry
   cJSON on IDF 6.x while preserving built-in `json` on the baseline.
2. In `dc_portal.c`, use supported PSA SHA-256 operations for IDF 6.x, with
   checked initialization/setup/update/finish status and hash/OTA abort cleanup
   on failure; preserve the 5.3 hashing path or independently validate PSA there.
3. Rebuild both chips under both IDFs and test OTA hash success, receive/write
   failure and crypto failure cleanup. Only then consider an approved new pin.

Estimated implementation: three upstream files, roughly 40–70 changed lines
plus focused error-path tests. That estimate covers these identified blockers;
further compiler/runtime incompatibilities may emerge afterward. Upstream
changes are required to use unmodified supported dependencies with IDF 6.1.1.
No managed dependency was patched, forked, repinned or upgraded, and no upstream
issue/PR was published as part of this task.

## DragonPWR corrections and verification

- Explicit `esp_driver_gpio`, `esp_driver_spi` and `esp_driver_uart` build
  dependencies replace reliance on the deprecated umbrella `driver` component.
  IDF 6.0 removed its former public SPI/UART dependency exports.
- DragonPWR's `dp_portal` conditionally selects registry cJSON **1.7.19** on
  IDF 6.x and retains built-in `json` on 5.3.5.
- An explicit mDNS constraint preserves the lock's existing version across
  profile resolution. Isolated component caches prevent the Git index-lock
  collision observed during the first simultaneous build attempts.
- The inactive-driver host test passes with `-std=c11 -Wall -Wextra -Werror`:
  both outputs reject ON and OFF, all supported restore policies are rejected,
  invalid output indexes fail, software readback stays false, and the actual
  inactive driver sources link without GPIO/NVS APIs.
- C2 relay/button C source, original board mapping, defaults and partition CSV
  are byte-unchanged from synchronized source. Compile commands select the
  original drivers, with GPIO/NVS symbols present as expected. This preserves
  the original implementation; it is not a new physical regression test.
- C3 compile commands select only `dp_relay_v2_inactive.c` and
  `dp_button_v2_inactive.c`. `nm -u` of their built archives shows logging and
  string comparison references only, with no GPIO/NVS references. Portal
  command and stock `/set` paths propagate driver failures by source inspection;
  an HTTP server was not executed on hardware.
- No new metering, LED, AC timing, USB-A/C, button or other hardware actuation
  implementation was introduced. V2's false output values remain software
  placeholders, not measured electrical/contact state.

There was no pre-existing automated test suite. The new host test and artifact
verifier were executed; Python syntax checks, setup shell syntax checks and
both installer repeat paths passed. Reproduce artifact checks with the
baseline environment active:

```sh
python scripts/test_v2_inactive.py
python scripts/verify_native_build.py build/native/v5.3.5/c2
python scripts/verify_native_build.py build/native/v5.3.5/v2
```

## V2 recovered layout and artifacts

The private 2026-11-18 factory reverse-engineering report was read from the
existing DragonPWR Drive folder and compared with the CSV and generated binary.
Its partition table was reported recovered from the verified full-flash image,
with a valid MD5 trailer. This task did not reacquire the factory image or
repeat its original physical acquisition. No raw image, NVS or device identifiers
were copied into this repository.

| Partition | Offset | Size |
|---|---:|---:|
| nvs | `0x9000` | `0x5000` |
| otadata | `0xe000` | `0x2000` |
| app0 | `0x10000` | `0x1f0000` |
| app1 | `0x200000` | `0x1f0000` |
| spiffs | `0x3f0000` | `0x10000` |

Baseline artifact SHA-256 values for the recorded validation input:

| Artifact | C2 | C3 V2 |
|---|---|---|
| App | `9683f403eb4c7fc6ccf71aee97de657abdcfda71b79447af87268767b1a4958a` | `813603b52a74531f15fc683e025e6981c7cb33315bc08e9939b86effe3367fef` |
| Bootloader | `f6f05c305a2289e01df22dbc59848aecc1133fcbb103b9dd3786b829e3f5ea6b` | `41597ba399b7e520433facfe5deca33909a9843a7f157bfd8f46595e1817ced3` |
| Partition table | `efba4421982bd177695a2e2091828fe3b6aa42076be3844a84f0fb08085cead4` | `bee384e6253509f4f1b298569b85159887bf1da2ce7454d38219cac533f363e8` |

Each profile's `artifact-verification.json` also inventories the ELF, initial
OTA data and flash arguments. These are development build outputs, not a
qualified release/recovery package. Rebuilds may change hashes through the
project version/build metadata even when behavior is unchanged.

## Acceptance boundary

Native environment installation and baseline builds for both targets: **PASS**.
Latest-stable compatibility evaluation: **complete, candidate rejected for the
current dependency pin**. Host inactive-driver contract and static artifact
checks: **PASS**. V2 physical behavior, startup levels, recovery, OTA migration,
electrical safety and hardware acceptance: **not tested**. Independent review
was not performed in this task. CI must be evaluated on its actual pushed SHA;
prior green checks are not evidence for this change.

The authorized next dependency step is proposal/review of the bounded upstream
work above, followed by separate implementation authorization. Keep using the
working native v5.3.5 profiles until that gate is resolved.

Sources: [Espressif v6.1.1 release](https://github.com/espressif/esp-idf/releases/tag/v6.1.1),
[official native installation](https://docs.espressif.com/projects/esp-idf/en/v6.1.1/esp32c3/get-started/linux-macos-setup-legacy.html),
[protocol migration](https://github.com/espressif/esp-idf/blob/v6.1.1/docs/en/migration-guides/release-6.x/6.0/protocols.rst),
[security migration](https://github.com/espressif/esp-idf/blob/v6.1.1/docs/en/migration-guides/release-6.x/6.0/security.rst),
[driver migration](https://github.com/espressif/esp-idf/blob/v6.1.1/docs/en/migration-guides/release-6.x/6.0/peripherals.rst).
