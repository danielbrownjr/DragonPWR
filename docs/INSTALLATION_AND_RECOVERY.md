# Installation and recovery

DragonPWR switches mains power. Firmware work should be done with the IEC input
unplugged unless a specific energized test requires mains and has an appropriate
mains-safe setup.

## The important installation fact

The stock Panda PWR firmware does **not** expose a local firmware-upload route,
and restoring stock with blank NVS does not produce an AP-provisioning fallback.
Therefore the **first DragonPWR install is serial** through the USB-C programming
port.

The stock partition table is still valuable: DragonPWR preserves it byte-for-byte
so the two OTA slots and full-flash recovery layout remain compatible. After
DragonPWR is installed, subsequent DragonPWR updates can be uploaded from the
browser.

## 1. Back up stock before changing anything

Power the board from USB-C only. Leave the IEC inlet unplugged and the C13 outlet
empty.

Identify the flash:

```text
python -m esptool --chip esp32c2 -p COM5 flash_id
```

Read the complete 4 MiB flash:

```text
python -m esptool --chip esp32c2 -p COM5 -b 460800 read_flash 0x0 0x400000 stock-panda-pwr-backup.bin
```

Verify the dump:

```text
python analysis/verify_backup.py stock-panda-pwr-backup.bin
```

Keep the backup private and out of git. It contains NVS and may contain Wi-Fi
credentials.

## 2. Build DragonPWR

ESP-IDF 5.3 or newer is required; the current bench build is verified with
ESP-IDF 5.3.5. The target is ESP32-C2:

```text
idf.py -D IDF_TARGET=esp32c2 build
```

`dependencies.lock` pins the dragon-core components. On Windows, disable
`core.autocrlf` for the component-manager fetch if hash verification reports a
corrupted dragon-core component; the README has the exact PowerShell command.

## 3. First install: serial

With the board still powered from USB-C only:

```text
idf.py -p COM6 flash
```

The board has a confirmed 26 MHz crystal. `sdkconfig.defaults` must keep the
target crystal configuration consistent with that hardware.

A separate console-UART quirk can occasionally make a 115200-baud console appear
at 74880. If the console is garbage after reset, retry the reset or monitor at
74880; see the 2026-09-14 bench notes.

## 4. Provision and verify

After a successful DragonPWR boot:

1. Complete the dragon-core Wi-Fi provisioning flow.
2. Confirm `/api/v2/info` reports product `dragonpwr`.
3. Confirm `/api/v2/state` reports the expected relay states and sane heap
   headroom.
4. Use `/power` for the temporary DragonPWR-specific control page until the
   shared `dc_ui` gains a dedicated DragonPWR surface.
5. Keep the mains output off while validating maintenance operations.

## 5. Browser OTA after DragonPWR is installed

DragonPWR inherits browser OTA from dragon-core. Rebooting operations are guarded:
the mains outlet must be off before firmware update, restart, factory reset or
booting the inactive slot.

Rollback protection also depends on the **bootloader**, not just the application.
The original bench unit's bootloader was built without rollback support. One
serial flash of a current DragonPWR build is required before pending-image
rollback protection is actually present.

A newly OTA'd DragonPWR image confirms itself after Wi-Fi and the portal are up.
An image that fails before that point can roll back when the bootloader supports
rollback.

## 6. Return to stock

The authoritative recovery path is the full flash backup taken before first
installation:

```text
python -m esptool --chip esp32c2 -p COM5 -b 460800 write_flash 0x0 stock-panda-pwr-backup.bin
```

Do not rely on "boot inactive slot" as a permanent stock restore. With a
rollback-enabled DragonPWR bootloader, stock firmware does not confirm itself as
a pending image, so that slot switch can revert on a later reset.

## Control-token recovery

If a configured control token is lost, clear the DragonPWR NVS partition over
serial. This also removes saved Wi-Fi credentials:

```text
python -m esptool --chip esp32c2 -p COM6 erase_region 0x9000 0x5000
```

See [API](API.md) for the authorization model and [Bench Notes](BENCH_NOTES.md)
for the hardware evidence behind these procedures.
