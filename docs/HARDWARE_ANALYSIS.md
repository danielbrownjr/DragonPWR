# Panda PWR Hardware Analysis

> Reverse-engineered from BIGTREETECH's stock firmware images and the recovery
> tool published at https://github.com/bigtreetech/PandaPWR. No schematic exists
> publicly; nothing here has been confirmed against a physical board yet.

Reproduce every claim below with:

```bash
python analysis/find_pins.py analysis/stock/panda_pwr-v1.0.0.1.bin
```

Images analysed (all four decode identically where they overlap):

| File | Project | IDF | Built |
|---|---|---|---|
| `panda_pwr-v1.0.0.bin` | `espnow_example` | v5.1.1 | 2024-10-15 |
| `panda_pwr-v1.0.0.1.bin` | `panda_pwr` | v5.1.1 | 2025-01-13 |
| `panda_pwr-v1.0.1_beta1.bin` | `panda_pwr` | v5.1.1 | 2025-01-13 |
| `panda_pwr_01.00.02.05.bin` | `panda_pwr` | v5.3.3 | (stripped) |

`v1.0.0.1` is the primary subject: it is the newest non-beta build of the
classic (non-MQTT-broker) firmware and retains the most `__FUNCTION__` strings.

## Module

| | |
|---|---|
| Module | ESP8684-MINI-1-H4 — **ESP32-C2**, RISC-V, 272 KB SRAM |
| Chip ID | `0x000C` in the image header |
| Flash | 4 MB, DIO. Factory log reports `c8 4016` = GD25Q32 |
| Console | UART0, which is GPIO20 (TX) / GPIO19 (RX) on the C2 |
| Programming | USB Type-C through a USB-UART bridge; the factory tool uses `before=default_reset`, so auto-reset into download mode works |

Image segments (from `esp_image.py`):

| Seg | Load addr | Length | Region |
|---|---|---|---|
| 0 | `0x3c090020` | `0x24928` | DROM (flash rodata) |
| 1 | `0x3fcacf30` | `0x232c` | DRAM |
| 2 | `0x40380000` | `0x9394` | IRAM |
| 3 | `0x42000020` | `0x8e5d0` | IROM (flash code) |
| 4 | `0x40389394` | `0x3b90` | IRAM |

`gp` (`__global_pointer$`) resolves to `0x3fcad730`, from the `auipc gp` /
`addi gp` pair at `0x40380438`. Several board constants are addressed
gp-relative, so this is needed to read them.

## Stock partition table

Recovered from `Recovery_tool.rar` (`PandaPWR_2024_08_20_partition.bin`), not
inferred:

| Partition | Type | Offset | Size |
|---|---|---|---|
| `nvs` | data/nvs | `0x009000` | 20 K |
| `otadata` | data/ota | `0x00e000` | 8 K |
| `app0` | app/ota_0 | `0x010000` | 1280 K |
| `app1` | app/ota_1 | `0x150000` | 1280 K |
| `spiffs` | data/spiffs | `0x290000` | 1472 K |

The recovery archive also carries the stock bootloader
(`PandaPWR_2024_08_20_bootloader.bin`, flashed at `0x0`) and the factory
`flash_download_tool` config, which confirms `chip=esp32c2`, DIO, and the
`0x0 / 0x8000 / 0x10000` layout.

## What is linked — and what that rules out

The set of ESP-IDF components present in the image is a hard upper bound on what
the hardware can be. Only two peripheral drivers appear:

- `driver/gpio`
- `driver/spi` (`gpspi` master, `spi_bus_lock`, plus `esp_hw_support/dma`)

**Not** linked: LEDC, RMT, I2C, ADC oneshot, pulse counter. The ESP32-C2 has no
RMT or I2S peripheral at all, so an addressable LED cannot be driven the usual
way — and indeed the status LED turns out to run over SPI + DMA.

The UART driver *is* used (see below) even though `uart_*` functions leave no
`__FUNCTION__` strings in this build; absence of a name string means "not
identifiable by name", not "not linked". `find_pins.py` locates `uart_set_pin`
structurally instead.

Application code occupies roughly `0x42004000`–`0x4200d000`; everything above
`0x42010000` is IDF. Task entry points created by `app_main` (`0x4200412c`):

| Task | Entry |
|---|---|
| (unnamed) | `0x42004458` |
| `app_ctl_task` | `0x420073e4` |
| `wifi_state` | `0x42004bb6` |
| `ele_task` | `0x42007b46` |

## Pin map

### Confirmed from the binary

| GPIO | Function | Evidence |
|---|---|---|
| **2** | Energy meter UART1 **TX** | `uart_set_pin(UART1, 2, 3, -1, -1)` at `0x4200b3cc` |
| **3** | Energy meter UART1 **RX** | same call |
| **4** | Status LED data (WS2812-style, SPI2 MOSI) | `spi_bus_config_t` at `0x4200b77c` takes `mosi` from the config object's first byte; that object is memcpy'd at `0x420072c6` from the immediate `0x00c0f004`, whose low byte is `0x04` |

### High confidence, one inference step

| GPIO | Function | Evidence |
|---|---|---|
| **10** | Push button — `INPUT`, internal pull-up | `gpio_config` at `0x42007434` builds its mask as `1ULL << *(uint8_t *)(gp - 0x544)`; that byte is `0x0a`. Mode `INPUT`, `pull_up_en = 1` |

The descriptor at `gp - 0x544` (`0x3fcad1ec`) reads
`0a 00 00 00 | 0a 00 00 00 | 05 00 00 00 | 64 00 00 00`. Only the first byte is
proven to be the pin; the rest is unread.

### Configured, function not yet determined

`fn 0x42007232`, called directly from `app_main`, configures exactly three pins
and nothing else does:

| GPIO | Mode | Pull | Interrupt |
|---|---|---|---|
| **7** | `INPUT_OUTPUT` | none | disabled |
| **18** | `INPUT_OUTPUT` | none | disabled |
| **6** | `INPUT` | none | disabled |

GPIO7 and GPIO18 are the only firmware-driven output-capable pins in the image,
so they **are** the mains relay and the USB1 switch — but which is which is not
yet established, because the `gpio_set_level` call sites pass the pin from a
variable rather than an immediate. `INPUT_OUTPUT` rather than `OUTPUT` is
consistent with firmware that reads back the level it drove.

GPIO6's role is open. Candidates: relay/USB feedback, a USB power-good input, or
a zero-cross line. It has no interrupt configured, which argues against
zero-cross.

### Reserved by the C2 / module

| GPIO | Note |
|---|---|
| 8, 9 | Strapping pins; GPIO9 selects boot mode |
| 12–17 | SPI flash on the ESP8684-MINI-1 |
| 19, 20 | UART0 console (RX / TX) |

## Energy meter

A register-based metering IC on **UART1, 9600 baud, 8E1** (`uart_param_config`
at `0x4200b3bc`: `baud=0x2580`, `data_bits=3`, `parity=2`, `stop_bits=1`;
`uart_driver_install` with 256-byte RX and TX buffers).

Wire format, built in `0x4200ab14`:

```
[0xA5] [reg | 0x80 for writes] [data ...] [~(sum of all previous bytes)]
```

Register `0xEA` is exempt from the `| 0x80` write flag and acts as the
write-enable gate: `0xE5` unlocks register writes, `0xDC` re-locks them, and
`0x5A` / `0xA5` select a mode. Reads are 2 bytes per register.

The init sequence (`0x4200acf4` onward) reads registers `0x01`, `0x40`, `0x13`,
`0x1D`, then writes:

| Register | Value |
|---|---|
| `0x00` | `0x0A04` |
| `0x01` | `0x0181` |
| `0x13` | `0x046D` |
| `0x1D` | `0x3219` |
| `0x40` | `0x4680` |

`ele_task` (`0x42007b46`) polls the device roughly every 100 ms through a vtable
installed at `0x4200b3fa` (`+0x4` open, `+0x8` flush, `+0xc` write, `+0x10`
read, `+0x14` no-op) and populates six values — matching the six fields the
stock HTTP API returns (voltage, current, power, energy, frequency, plus a
status word).

A `0xEA` write-protect register unlocked with `0xE5` and relocked with `0xDC` is
the convention used by the ATT705x / V92xx metering families, but **the part
number is not established** — a photo of the board settles it in seconds and
should be taken before anyone writes a driver.

## Status LED

Driven as a WS2812-style strip over **SPI2, MOSI only** — `miso`, `sclk`,
`quadwp` and `quadhd` are all `-1`, `max_transfer_sz = 10000`, and the device is
added with `spics_io_num = -1`, `queue_size = 7`, clock **6 MHz**
(`0x4200b77c`). That is the standard `led_strip_spi` shape.

The surrounding object (`0x3fcb11d8`) is a small indicator library with two
backends selected by a type argument: type 0 configures a plain GPIO output
(`0x4200b510`) and type 1 opens the SPI strip (`0x4200b77c`). The Panda PWR
initialises **type 1** at `0x420072d6`. Its timing fields default to 300 / 1500 /
500 ms, consistent with the solid / pulsing / flashing behaviours the BTT wiki
documents (blue, blue pulsing, green, green flashing, red pulsing).

## Panda Touch link

The image contains an `esp_now_device` component, ESP-NOW init/deinit logging,
and the LMK string `@lmk_panda_power`. Pairing with a Panda Touch is ESP-NOW,
not Wi-Fi. The framed protocol documented in BTT's `pwr_api.md`
(`AA <cmd> <len> <data> <crc16> 5A A5`) is the payload format. DragonPWR will
lose Panda Touch pairing unless this is reimplemented; that is out of scope for
the first milestone.

## Open questions

1. **Which of GPIO7 / GPIO18 is the relay and which is USB1?** Resolvable on the
   bench in under a minute: drive one high with the load disconnected and listen
   for the relay. Do this before anything is wired to mains.
2. **What is GPIO6?** Trace it, or watch it while toggling the relay and USB.
3. **Which metering IC?** Board photo. The register map above then either matches
   a datasheet or it does not.
4. **Is the relay latching or momentary?** Determines power-loss behaviour and
   whether the boot-time default can be "off" safely.
5. **What are the remaining fields of the button descriptor at `0x3fcad1ec`?**
   Long-press thresholds are documented by BTT as 3 s (pair) and 8 s (factory
   reset); confirm against the binary rather than trusting the wiki.

## Tools

| Script | Purpose |
|---|---|
| `analysis/esp_image.py` | ESP-IDF app-image reader: header facts, segments, load-address reads. No esptool dependency |
| `analysis/codemap.py` | RV32IMC disassembly, `lui`/`auipc` address recovery, string index, call graph, `__FUNCTION__`-based function identification |
| `analysis/find_pins.py` | The three reports above |
| `analysis/dumpfn.py` | Disassemble one function with string and call-target annotations |
| `analysis/find_gpio_config.py` | Scans rodata for `gpio_config_t` initializer templates. Finds nothing on this image — GCC builds the struct with inline immediates rather than copying a template — which is why `find_pins.py` replays the stack instead. Kept because it is the first thing to try on a new image |

Stock binaries live in `analysis/stock/` and are gitignored; they are BTT's to
distribute, not ours.
