# DragonPWR

Open firmware for the [BIGTREETECH Panda PWR](https://github.com/bigtreetech/PandaPWR),
built on the shared [`dragon-core`](https://github.com/justinh-rahb/dragon-core)
networking and printer-integration components.

**Status: Phase 1 is bench-verified on a real unit.** The relay, USB1, the Bind
button, the web UI, the control token and OTA from the browser all work. There
is no metering and no printer integration yet - see the
[roadmap](docs/ROADMAP.md).

The pin map was reverse-engineered from BIGTREETECH's stock images. The outputs
and the button are confirmed on a board; GPIO6 and the metering chip are not.
Read [docs/HARDWARE_ANALYSIS.md](docs/HARDWARE_ANALYSIS.md) before you wire
anything to mains.

Once installed, the outlet is switched from `http://<device>/power`. The
device's main page is the shared family UI, which has no DragonPWR dashboard
yet; its Settings screen works.

## Hardware

| | |
|---|---|
| Module | ESP8684-MINI-1-H4 (**ESP32-C2**, RISC-V, 272 KB SRAM) |
| Flash | 4 MB (GD25Q32, `c8 4016`), DIO |
| Stock IDF | v5.1.1 (`panda_pwr` 1.0.0.x) / v5.3.3 (MQTT-broker builds) |
| Programming | USB Type-C -> USB-UART bridge, auto-reset (`before=default_reset`) |

Stock partition table (recovered from `Recovery_tool.rar`):

| Partition | Type | Offset | Size |
|---|---|---|---|
| `nvs` | data/nvs | `0x009000` | 20 K |
| `otadata` | data/ota | `0x00e000` | 8 K |
| `app0` | app/ota_0 | `0x010000` | 1280 K |
| `app1` | app/ota_1 | `0x150000` | 1280 K |
| `spiffs` | data/spiffs | `0x290000` | 1472 K |

DragonPWR targets this table byte-for-byte so it can be installed by OTA from the
stock web UI, the same way DragonVent and DragonStatus install over their stock
firmware.

## Back up stock first

BIGTREETECH publishes the app images but never a full flash dump, so the backup
you take before your first install is the only way back to stock.

Power the unit from the Type-C `Prog` port only — leave the IEC input unplugged
and nothing in the C13 socket. The board runs happily off USB, and there is no
reason for mains to be present while you are talking to the bootloader.

```
python -m esptool --chip esp32c2 -p COM5 flash_id
```

```
python -m esptool --chip esp32c2 -p COM5 -b 460800 read_flash 0x0 0x400000 stock-panda-pwr-backup.bin
```

Then check it actually came out whole. A truncated read looks exactly like a good
one until the day you need it:

```
python analysis/verify_backup.py stock-panda-pwr-backup.bin
```

Keep the dump off the device and out of git — it contains the NVS partition, so
any Wi-Fi credentials the unit was configured with are in there.

To restore:

```
python -m esptool --chip esp32c2 -p COM5 -b 460800 write_flash 0x0 stock-panda-pwr-backup.bin
```

## Building

Requires ESP-IDF 5.3 or newer (built and verified against v5.3.5) and a host
`gzip`, which `dc_ui` uses to make the embedded SPA reproducible.

On the Windows dev machine, ESP-IDF v5.3.5 lives at `C:\esp\v5.3.5\esp-idf`
(not on `PATH` by default — a separate install from whatever the Espressif
installer put under `C:\Espressif`). Source its environment once per shell
before running `idf.py`:

```powershell
cd C:\esp\v5.3.5\esp-idf
. .\export.ps1
cd C:\Users\danie\Coding\DragonPWR
```

```bash
idf.py -D IDF_TARGET=esp32c2 build
```

To flash and watch the console over the CH340 bridge (shows up as a COM
port, e.g. `COM6`): `idf.py -p COM6 flash`. The console UART on this board
is subject to a sporadic hardware quirk — see docs/BENCH_NOTES.md,
2026-09-14 session — where it sometimes comes up at 74880 baud instead of
the configured 115200; retry the reset or open the monitor at 74880 if a
log looks like garbage.

The current image is about 793 K against a 1280 K app slot. `dependencies.lock`
is committed: `dragon-core` is pinned by tag and the lock is what makes that
reproducible.

The lock's hashes are of an **LF** checkout. The component manager clones
`dragon-core` with your global git settings and does not normalise line endings,
so with `core.autocrlf=true` (the Git for Windows default) the fetch fails with
*"The downloaded component "dc_evlog" is corrupted"*. Turn it off in the build
shell, after `export.ps1`, then delete `managed_components/` and build again:

```powershell
$env:GIT_CONFIG_COUNT=1; $env:GIT_CONFIG_KEY_0="core.autocrlf"; $env:GIT_CONFIG_VALUE_0="false"
```

If it still fails, the component manager's cache holds a CRLF checkout from an
earlier fetch; clear it too.

## Control token

Out of the box, commands are open to anything on the LAN that sends an
`X-Dragon-Auth` header with any value. That blocks cross-site requests from a
web page, but it is not a password: anything on the network that means to can
still switch the outlet.

To lock it down:

```
curl -X POST http://<device>/api/v2/token -H 'X-Dragon-Auth: web' \
     -H 'Content-Type: application/json' -d '{"token":"<1-64 chars, no spaces>"}'
```

After that every command, OTA and factory reset needs `X-Dragon-Auth: <token>`,
and so does stock `/set`. `/api/v2/info`, `/api/v2/state` and `/update_ele_data`
stay readable. Clear it by posting `{"token":""}` with the current token.

- **HA-Panda-PWR stops switching the outlet** once a token is set. It speaks the
  stock API and cannot send the header. Readback keeps working. Before a token
  is set, `/set` accepts it but refuses browsers posting from another site.
- **A forgotten or unreadable token can only be cleared by erasing NVS over
  serial**, which also forgets Wi-Fi:
  `python -m esptool --chip esp32c2 -p COM6 erase_region 0x9000 0x5000`

## Printer source

DragonPWR can follow one printer, picked in **Settings > Device setup > Printer**
and applied after a restart:

| | |
|---|---|
| **None - plug only** | The default. No printer connection. |
| **Klipper (Moonraker)** | Moonraker's websocket, plain HTTP. Moonraker must trust the plug's IP (`[authorization] trusted_clients`). |
| **Bambu Lab (experimental)** | LAN-mode MQTT over TLS, read-only. Its TLS session needs more memory than this chip reliably has spare; the plug refuses to start it when the heap is too low and says so on `/power`. |

Today the source is reported, not acted on: `/power` and `/api/v2/state` show the
printer's state and temperatures. The mid-print interlock comes next.

To try the Klipper source without a printer, run the fake Moonraker on any
machine on the LAN and point the plug at it:

```
pip install websockets
python tools/fake_moonraker.py
```

Type `print`, `pause`, `bed 60`, `shutdown` and so on to drive it; the commands
are listed at the top of the file.

## Layout

| Path | |
|---|---|
| `main/` | `app_main` — brings the outputs up, then hands off to dragon-core |
| `components/dp_board/` | The pin map, and the only place polarity is written down |
| `components/dp_relay/` | Mains + USB1 outputs, safe boot state, restore policy |
| `components/dp_portal/` | Product API v2, stock-compatible routes, safety guards, `/power` |
| `components/dp_printer/` | Printer source selection (none / Moonraker / Bambu) and one status for all |
| `tools/` | `fake_moonraker.py`, for testing the Klipper source without a printer |
| `analysis/` | Static-analysis tooling for the stock firmware, and the backup verifier |

## Documentation

- [Roadmap](docs/ROADMAP.md) — phases, the flash budget, and what is deliberately out of scope
- [Hardware Analysis](docs/HARDWARE_ANALYSIS.md) — pin map recovery, evidence, open questions
- [analysis/](analysis/) — scripts that produce the evidence
