#!/usr/bin/env python3
"""Check linked board selection, recovered partitions and image sizes (no device I/O)."""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import subprocess

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("area", type=Path, help="build/native/v5.3.5/c2 or v2; activate that IDF first")
args = parser.parse_args()
area = args.area.resolve()
board = area.name
if board not in ("c2", "v2"):
    parser.error("area must end with c2 or v2")
target, xtal, slot = (("esp32c2", 26, 0x140000) if board == "c2"
                      else ("esp32c3", 40, 0x1f0000))
out = area / "out"
cfg = (area / "sdkconfig").read_text()
for setting in (f'CONFIG_IDF_TARGET="{target}"', f"CONFIG_XTAL_FREQ={xtal}",
                'CONFIG_ESPTOOLPY_FLASHMODE="dio"', 'CONFIG_ESPTOOLPY_FLASHSIZE="4MB"',
                "CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE=y"):
    assert setting in cfg, setting
commands = json.loads((out / "compile_commands.json").read_text())
drivers = [x["file"] for x in commands if "/dp_relay/" in x["file"] or "/dp_button/" in x["file"]]
assert len(drivers) == 2
assert all(("v2_inactive" in x) == (board == "v2") for x in drivers)
part = (out / "partition_table/partition-table.bin").read_bytes()
entries = []
md5_ok = False
for i in range(0, len(part), 32):
    magic, _, _, offset, size, label, _ = struct.unpack("<HBBII16sI", part[i:i + 32])
    if magic == 0xebeb:
        assert part[i + 16:i + 32] == hashlib.md5(part[:i]).digest()
        md5_ok = True
        break
    assert magic == 0x50aa
    entries.append([label.rstrip(b"\0").decode(), offset, size])
# Independently recorded factory offsets, rather than deriving expectations
# from the CSV under test. V2 provenance: docs/V2_M1_BUILD.md.
expected = [["nvs", 0x9000, 0x5000], ["otadata", 0xe000, 0x2000],
            ["app0", 0x10000, slot],
            ["app1", 0x150000 if board == "c2" else 0x200000, slot],
            ["spiffs", 0x290000 if board == "c2" else 0x3f0000,
             0x170000 if board == "c2" else 0x10000]]
assert md5_ok and entries == expected
symbols = {}
for component in ("dp_relay", "dp_button"):
    symbols[component] = subprocess.check_output(
        ["riscv32-esp-elf-nm", "-u", str(out / f"esp-idf/{component}/lib{component}.a")], text=True)
    if board == "v2":
        assert "gpio_" not in symbols[component] and "nvs_" not in symbols[component]
artifacts = {}
for name in ("dragonpwr.bin", "dragonpwr.elf", "bootloader/bootloader.bin",
             "partition_table/partition-table.bin", "ota_data_initial.bin", "flasher_args.json"):
    data = (out / name).read_bytes()
    artifacts[name] = {"bytes": len(data), "sha256": hashlib.sha256(data).hexdigest()}
assert artifacts["dragonpwr.bin"]["bytes"] <= slot
assert artifacts["bootloader/bootloader.bin"]["bytes"] <= 0x8000
flash = json.loads((out / "flasher_args.json").read_text())
assert flash["extra_esptool_args"]["chip"] == target
assert flash["flash_settings"]["flash_mode"] == "dio"
assert flash["flash_settings"]["flash_size"] == "4MB"
assert flash["partition-table"]["offset"] == "0x8000"
result = {"target": target, "xtal": xtal, "partitions": entries,
          "drivers": [str(Path(x).relative_to(area / "source")) for x in drivers],
          "undefined_symbols": symbols, "artifacts": artifacts}
(area / "artifact-verification.json").write_text(json.dumps(result, indent=2) + "\n")
print(f"PASS: {target}, {xtal} MHz, drivers, partition MD5/offsets, app/bootloader fit")
