"""Sanity-check a full-flash backup of a Panda PWR before trusting it.

    python verify_backup.py stock-panda-pwr-backup.bin

BIGTREETECH publishes the app images but never a full flash dump, so the backup
you take before your first install is the only way back to stock. A truncated or
half-read dump looks exactly like a good one until the day you need it, which is
the worst possible time to find out. This checks the things that would make it
useless:

  * the right size for the 4 MB part
  * a bootloader at 0x0
  * a partition table at 0x8000 that matches the known stock layout
  * a real app image in at least one OTA slot, with its descriptor readable

Exit status is 0 when the backup looks restorable, 1 when it does not.
"""
import argparse
import struct
import sys

from esp_image import AppImage

FLASH_SIZE = 4 * 1024 * 1024
BOOTLOADER_OFFSET = 0x0
PARTITION_OFFSET = 0x8000
PARTITION_MAGIC = b"\xaa\x50"
PARTITION_END = b"\xeb\xeb"
IMAGE_MAGIC = 0xE9

# From Recovery_tool.rar (PandaPWR_2024_08_20_partition.bin).
STOCK_TABLE = [
    ("nvs",     1, 2,   0x009000, 0x005000),
    ("otadata", 1, 0,   0x00E000, 0x002000),
    ("app0",    0, 16,  0x010000, 0x140000),
    ("app1",    0, 17,  0x150000, 0x140000),
    ("spiffs",  1, 130, 0x290000, 0x170000),
]

TYPE_NAMES = {0: "app", 1: "data"}


class Report:
    def __init__(self):
        self.failed = False

    def ok(self, text):
        print(f"  ok    {text}")

    def warn(self, text):
        print(f"  warn  {text}")

    def fail(self, text):
        print(f"  FAIL  {text}")
        self.failed = True


def parse_table(blob):
    entries = []
    for offset in range(PARTITION_OFFSET, len(blob), 32):
        entry = blob[offset:offset + 32]
        if len(entry) < 32 or entry[:2] == PARTITION_END:
            break
        if entry[:2] != PARTITION_MAGIC:
            break
        ptype, subtype, off, size = struct.unpack("<BBII", entry[2:12])
        name = entry[12:28].split(b"\0")[0].decode("utf-8", "replace")
        entries.append((name, ptype, subtype, off, size))
    return entries


def check(path):
    report = Report()
    blob = open(path, "rb").read()

    print(f"{path}\n")
    print("size")
    if len(blob) == FLASH_SIZE:
        report.ok(f"{len(blob)} bytes = 4 MB")
    elif len(blob) < FLASH_SIZE:
        report.fail(f"{len(blob)} bytes, expected {FLASH_SIZE} - the read was cut short")
    else:
        report.warn(f"{len(blob)} bytes, more than the 4 MB part holds")

    print("\nbootloader @ 0x0")
    if len(blob) > BOOTLOADER_OFFSET and blob[BOOTLOADER_OFFSET] == IMAGE_MAGIC:
        report.ok("image magic 0xE9 present")
    else:
        got = f"0x{blob[BOOTLOADER_OFFSET]:02x}" if blob else "nothing"
        report.fail(f"expected 0xE9, found {got}")

    print("\npartition table @ 0x8000")
    table = parse_table(blob)
    if not table:
        report.fail("no partition entries found")
    else:
        for name, ptype, subtype, off, size in table:
            print(f"        {name:10} {TYPE_NAMES.get(ptype, ptype):5} "
                  f"sub={subtype:<4} 0x{off:06x}  {size // 1024:>5} K")
        if table == STOCK_TABLE:
            report.ok("matches the known stock layout exactly")
        else:
            report.warn("does NOT match the stock layout recorded in "
                        "docs/HARDWARE_ANALYSIS.md - fine if this device was "
                        "repartitioned, suspicious otherwise")

    print("\napp slots")
    slots = [e for e in table if e[1] == 0] or [
        ("app0", 0, 16, 0x010000, 0x140000), ("app1", 0, 17, 0x150000, 0x140000)]
    found = 0
    for name, _ptype, _subtype, off, size in slots:
        region = blob[off:off + size]
        if not region or region[0] != IMAGE_MAGIC:
            print(f"  ----  {name}: empty (no image magic)")
            continue
        try:
            img = AppImage(f"{path}:{name}", blob=region)
        except Exception as exc:                      # noqa: BLE001
            report.fail(f"{name}: image present but unreadable ({exc})")
            continue
        found += 1
        report.ok(f"{name}: {img.project_name} {img.app_version} "
                  f"({img.chip}, IDF {img.idf_version}, built {img.build_date})")

    if found == 0:
        report.fail("no readable app image in any slot - nothing to restore")

    print()
    if report.failed:
        print("NOT a usable backup. Re-read the flash before you touch this device.")
        return 1
    print("Backup looks restorable. Keep it off the device and out of git:")
    print("  it contains the NVS partition, so any Wi-Fi credentials the unit")
    print("  was configured with are in there.")
    return 0


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("backup", help="full-flash dump, 0x0 to 0x400000")
    args = parser.parse_args()
    return check(args.backup)


if __name__ == "__main__":
    sys.exit(main())
