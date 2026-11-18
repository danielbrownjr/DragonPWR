# Native ESP-IDF development

Use **ESP-IDF v5.3.5** as the primary development environment with the current
dragon-core v0.30.0 pin. The newest stable release evaluated on 2026-11-18,
v6.1.1, supports both chips but cannot configure pinned `dc_portal`.
See the [measured compatibility matrix](validation/2026-11-18-native-idf.md)
before selecting a different release. Neither environment implies permission
to flash or qualification of the V2 outputs.

## Install

Prerequisites: Linux x86-64, Git, CMake, Ninja, a C compiler for host tests,
gzip, network access, and a **Python 3.11** installation with venv/pip support.
The official installers supply the RISC-V tools and constrained Python packages.
On Heph, existing `uv` installed user-owned CPython 3.11.18 using
`uv python install 3.11`; system Python 3.14 was left unchanged.

From the repository, using the absolute path returned by `uv python find 3.11`
or another existing Python 3.11 executable:

```sh
bash scripts/setup_idf.sh v5.3.5 /absolute/path/to/python3.11
bash scripts/setup_idf.sh v6.1.1 /absolute/path/to/python3.11
```

The script clones exact official release tags with recursive submodules and
runs Espressif's `install.sh esp32c2,esp32c3`. It creates:

| Release | Source | Tools and Python virtual environment |
|---|---|---|
| v5.3.5 | `~/esp/esp-idf-v5.3.5` | `~/.espressif-v5.3.5` |
| v6.1.1 | `~/esp/esp-idf-v6.1.1` | `~/.espressif-v6.1.1` |

Installation writes only these user-owned development directories and normal
download caches. It does not install OS packages, edit shell startup files,
install udev rules or access devices. Re-running verifies the selected tag and
submodules, then lets the official installer verify/update packages within its
release constraints. Preserve the package inventory when reproducing a dated
result: constraints alone are not a complete Python package lock.

## Activate independently

Use a **fresh Bash or Zsh shell** for each release:

```sh
. "$HOME/esp/activate-idf-v5.3.5.sh"
idf.py --version
```

For evaluation instead:

```sh
. "$HOME/esp/activate-idf-v6.1.1.sh"
idf.py --version
```

The generated activation files export the corresponding `IDF_TOOLS_PATH`,
put the chosen Python directory on PATH, and source that release's official
`export.sh`. The Python environments on Heph are
`python_env/idf5.3_py3.11_env` and `python_env/idf6.1_py3.11_env` within their
respective tools directories. Close the shell when finished.

## Build and inspect

After activation, from the repository:

```sh
python scripts/native_build.py c2
python scripts/native_build.py v2
```

Each combination owns `build/native/<release>/<c2|v2>/` containing:

- `source/`: a snapshot of tracked working-tree files, its own
  `managed_components/` and generated lockfile;
- `sdkconfig`: a distinct generated configuration;
- `component-cache/`: a distinct Component Manager cache, avoiding concurrent
  Git index locking between profiles;
- `out/`: CMake/Ninja output, ELF, application, bootloader, partition table and
  flash argument files;
- `source-head.txt` and `source-files.json`: input HEAD and SHA-256 hashes of
  the snapshot's input files. Uncommitted edits to tracked files are included;
  untracked files must be staged before building.

The wrapper uses explicit `IDF_TARGET`, `SDKCONFIG` and `SDKCONFIG_DEFAULTS`:
C2 uses `sdkconfig.defaults`; C3 uses the alternative `sdkconfig.v2.defaults`.
It runs `idf.py reconfigure`, `ninja -j4` and `idf.py size`. Change concurrency
with `--jobs N`. The root sdkconfig, managed dependencies and lock remain
separate from generated state. To inspect a generated profile with `idf.py`,
repeat the wrapper's printed `-C`, `-B` and `-D` arguments.

After a successful baseline build, with that IDF still active:

```sh
python scripts/verify_native_build.py build/native/v5.3.5/c2
python scripts/verify_native_build.py build/native/v5.3.5/v2
python scripts/test_v2_inactive.py
```

The verifier checks board source selection, crystal/flash/rollback settings,
partition binary MD5 and recovered offsets, app/bootloader fit, and V2 archive
symbols. It writes an artifact size/hash inventory in the profile directory.
The host test compiles and executes the actual inactive drivers against minimal
error/log stubs, requiring every AC/USB ON/OFF and restore request to fail.
Neither command opens a device. No pre-existing automated test suite was found.

## Dependency and migration boundary

Keep dragon-core at v0.30.0. Its annotated tag object is
`8d5a61de8544a5b82cc3c6d9c7dc593ce06bb8bd`; the peeled source commit is
`4c04c7a1ffc5c61c31e66313af9515c45f6fba0c`.
mDNS is explicitly held at the existing lock's **1.12.0** so resolving a
different target/IDF does not upgrade it. IDF 6.x alone adds cJSON **1.7.19**
for DragonPWR; IDF 5.3.5 continues to use the built-in `json` component.

IDF 6.x still fails in unchanged upstream `dc_portal`, which requires `json`
and uses removed legacy Mbed TLS SHA-256 APIs. The report proposes the minimal
upstream migration. Do not work around this by editing `managed_components`,
aliasing away the missing component, changing pinned revisions, or adopting
private Mbed TLS headers. Re-evaluate both boards after an approved upstream
fix. CI remains on the demonstrated 5.3.5 baseline.

Official references: [v6.1.1 release](https://github.com/espressif/esp-idf/releases/tag/v6.1.1),
[official legacy native installation](https://docs.espressif.com/projects/esp-idf/en/v6.1.1/esp32c3/get-started/linux-macos-setup-legacy.html),
[6.0 protocol migration](https://github.com/espressif/esp-idf/blob/v6.1.1/docs/en/migration-guides/release-6.x/6.0/protocols.rst),
[6.0 security migration](https://github.com/espressif/esp-idf/blob/v6.1.1/docs/en/migration-guides/release-6.x/6.0/security.rst),
[6.0 driver migration](https://github.com/espressif/esp-idf/blob/v6.1.1/docs/en/migration-guides/release-6.x/6.0/peripherals.rst).
