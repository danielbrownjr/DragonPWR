# DragonPWR

Open firmware for the [BIGTREETECH Panda PWR](https://github.com/bigtreetech/PandaPWR),
built on the shared [`dragon-core`](https://github.com/justinh-rahb/dragon-core)
networking and printer-integration components.

**Status: Phase 1 firmware builds; nothing has run on hardware yet.**

The pin map is reverse-engineered from BIGTREETECH's stock images and has
not been confirmed against a board. Do not flash this expecting it to work,
and read [docs/HARDWARE_ANALYSIS.md](docs/HARDWARE_ANALYSIS.md) before you
wire anything to mains.

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

## Building

Requires ESP-IDF 5.3 or newer (built and verified against v5.3.1) and a host
`gzip`, which `dc_ui` uses to make the embedded SPA reproducible.

```bash
idf.py -D IDF_TARGET=esp32c2 build
```

The current Phase 1 image is 778 K against a 1280 K app slot. `dependencies.lock`
is committed: `dragon-core` is pinned by tag and the lock is what makes that
reproducible.

## Layout

| Path | |
|---|---|
| `main/` | `app_main` — brings the outputs up, then hands off to dragon-core |
| `components/dp_board/` | The pin map, and the only place polarity is written down |
| `components/dp_relay/` | Mains + USB1 outputs, safe boot state, restore policy |
| `components/dp_portal/` | Product API v2, stock-compatible routes, safety guards |
| `analysis/` | Static-analysis tooling for the stock firmware |

## Documentation

- [Roadmap](docs/ROADMAP.md) — phases, the flash budget, and what is deliberately out of scope
- [Hardware Analysis](docs/HARDWARE_ANALYSIS.md) — pin map recovery, evidence, open questions
- [analysis/](analysis/) — scripts that produce the evidence
