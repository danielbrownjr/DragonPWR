# Stock Panda PWR metering: reverse engineering

What BIGTREETECH's stock firmware does with the HLW8112, from its register
transactions to the JSON it serves. It is enough to specify `dp_meter`. The
hardware record is [HARDWARE_ANALYSIS.md](HARDWARE_ANALYSIS.md#energy-meter),
and the datasheet is
[HLW8110/HLW8112 REV 1.01](https://datasheet.lcsc.com/datasheet/pdf/7618fcc29341bc35e74ce1d001211dc8.pdf?productCode=C970140)
(Hiliwei), cited below as "DS".

Addresses are from `panda_pwr-v1.0.0.1.bin` unless noted.

## Executive result

**Yes: `dp_meter` can be implemented without further hardware tracing.**

- **Transport is settled.** The firmware's own `uart_set_pin(UART1, 2, 3, -1, -1)`
  (`0x4200b3cc`)
  fixes the pins, the protocol matches the datasheet exactly, and stock uses
  them successfully on this board.
- **Math is settled.** Every conversion is recovered to the instruction, and
  every constant is either explained by the datasheet or identified by address
  and value.

These are decisions or runtime facts, not blockers:

1. **Init writes.** Energy and frequency need the chip configured. Stock
   writes five registers at every boot, and the values are known exactly.
   Voltage, current and power work from the chip's reset defaults with no writes.
2. **Factory coefficients** are per chip. They must be read at runtime and are
   not in the image.
3. **The board constant 0.51** is BTT's. Its accuracy against a reference meter
   has not been measured.

Metering itself needs no continuity work. Tracing HLW8112 TX/RX to GPIO3/GPIO2
would only confirm what the firmware already establishes.

## Reproduce

```bash
cd analysis
python recover_meter.py stock/panda_pwr-v1.0.0.1.bin
python recover_meter.py stock/panda_pwr-v1.0.0.bin
```

The stock images come from https://github.com/bigtreetech/PandaPWR
(`Firmware/1.0.0.1/`, `Firmware/1.0.0/`) and stay in the gitignored
`analysis/stock/`. The output is deterministic: two runs give byte-identical
reports. `recover_meter.py` finds the code by shape, not by address.

Floating point is soft-float in ROM. The ROM entry points come from ESP-IDF
v5.1.1 `esp32c2.rom.rvfp.ld`: `__floatunsisf 0x40000a68`, `__mulsf3 0x40000a90`,
`__divsf3 0x40000a20`, `__addsf3 0x40000a1c`, `__extendsfdf2 0x40000a2c`,
`__gedf2 0x40000a6c`, `__ltdf2 0x40000a84`, `__ltsf2 0x40000a88`,
`__gtsf2 0x40000a78` and `__fixunssfsi 0x40000a48`.

## Stock register transaction table

`R` = read, `W` = write, `S` = special command (`0xEA` + code). "DS width" is
from DS table 9.

| Addr | Name | DS width | Dir | Read as | Purpose | Evidence |
|---|---|---|---|---|---|---|
| `0xEA` | special | – | S | – | `0x96` chip reset | open `0x4200abc6` → `0x4200abfc` |
| `0xEA` | special | – | S | – | `0xE5` write enable, then `0xDC` write protect | init `0x4200ad54` / `0x4200adee`; energy `0x4200b224` / `0x4200b262` |
| `0xEA` | special | – | S | – | `0x5A` select channel A (`0xA5`, channel B, only if init's argument is non-zero; setup passes 0) | `0x4200ad74` |
| `0x00` | SYSCON | 2 | W, R | 2 | write `0x0A04`; read back | `0x4200ad86`, `0x4200adfe` |
| `0x01` | EMUCON | 2 | R, W, R | 2 | read, write `0x0181`, read back | `0x4200ad1c`, `0x4200ad9a`, `0x4200ae0c` |
| `0x13` | EMUCON2 | 2 | R, W, R | 2 | write `0x046D`; energy clear writes `0x006D` then `0x046D` | `0x4200ad3a`, `0x4200adae`, `0x4200ae2a`, `0x4200b230`, `0x4200b258` |
| `0x1D` | INT | 2 | R, W, R | 2 | write `0x3219` | `0x4200ad48`, `0x4200adc4`, `0x4200ae38` |
| `0x40` | IE | 2 | R, W, R | 2 | write `0x4680` | `0x4200ad2c`, `0x4200addc`, `0x4200ae1c` |
| `0x26` | RmsU | 3 | R | 3 | voltage | `0x4200ae62` |
| `0x72` | RmsUC | 2 | R | 2 | voltage coefficient, once | `0x4200ae92` |
| `0x24` | RmsIA | 3 | R | 3 | current, channel A | `0x4200af60` |
| `0x70` | RmsIAC | 2 | R | 2 | current coefficient, once | `0x4200af90` |
| `0x2C` | PowerPA | 4 | R | 4 | active power, channel A | `0x4200b044` |
| `0x73` | PowerPAC | 2 | R | 2 | power coefficient, once | `0x4200b086` |
| `0x28` | Energy_PA | 3 | R | 3 | energy pulses, channel A; read twice more to clear | `0x4200b13e`, `0x4200b23e`, `0x4200b24c` |
| `0x76` | EnergyAC | 2 | R | 2 | energy coefficient, once | `0x4200b170` |
| `0x23` | Ufreq | 2 | R | 2 | line frequency | `0x4200b298` |
| `0x42` | RIF | 2 | R | 2 | interrupt flags, cleared by the read | `0x4200b306` |

The init reads of `0x01`, `0x40`, `0x13` and `0x1D` before the writes act as a
communication check: any failure aborts init. Their values, and all the
read-backs, are discarded. The read-backs are not compared with what was
written.

**Never touched:** `HFConst` (`0x02`, left at its reset value `0x1000`); all of
channel B (`0x25 RmsIB`, `0x29 Energy_PB`, `0x2D PowerPB`); `0x27 PF`;
`0x2E PowerS`; `0x2F EMUStatus`; `0x41 IF`; `0x43 SysStatus`; the thresholds
`0x19`–`0x1C`; and the coefficients `0x71`, `0x74`, `0x75`, `0x77` and checksum
`0x6F`.

## Transport

| Routine | Detail |
|---|---|
| Read `0x4200ac0e` | Send `[0xA5, reg & 0x7F]`, then read `len + 1` bytes. Accept if `recv[len] == ~(0xA5 + reg + Σdata) & 0xFF`, 8-bit wrapping sum. Refuses `reg == 0xEA` and `len > 4`. Returns 0 on success, −100 on checksum mismatch, negative on UART error |
| Frame write `0x4200ab14` | `[0xA5][reg \| 0x80][data…][~(0xA5 + cmd + Σdata)]`, with `0xEA` sent without `\| 0x80`. Up to 4 data bytes. Then `vTaskDelay(5)` |
| UART read `0x4200b328` | `(port 1, buf, len, 3 ticks)`. A short read is an error |
| UART write `0x4200b358` | `(port 1, buf, len)` |
| Open `0x4200abc6` | Port 1, UART setup (9600 8E1, GPIO2 TX, GPIO3 RX, 256-byte buffers), then `0xEA 0x96` reset |

The UART and FreeRTOS helpers (`0x4200b328`/`b358`, `vTaskDelay 0x403877a0`,
`xTaskGetTickCount 0x403874a2`) are identified by argument shape and call
pattern; this build leaves no name strings for them.

**The read copies data out before comparing the checksum.** On a mismatch the
caller's buffer already holds the bad bytes. On a UART failure it keeps its old
contents.

## Integer detail per reader

All raw values are **unsigned**. Bytes are assembled MSB first with shifts and
`add` (never sign-extending loads). Every coefficient is `lhu` followed by a
16-bit byte swap, `(x << 8 | x >> 8)`, then `<< 16 >> 16` to zero-extend. The
result is **u16 big-endian, zero-extended**.

| Reader | Assembly | Sign handling | Guard |
|---|---|---|---|
| Voltage `0x4200ae42` | `b0<<16 + b1<<8 + b2` (u24) | `raw & 0x800000` → result 0 | Metrology read failure: return, output unchanged. Coefficient read failure is checked |
| Current `0x4200af40` | same (u24) | `raw & 0x800000` → 0 | Metrology read checked. **Coefficient read failure not checked** |
| Power `0x4200b024` | `b0<<24 + b1<<16 + b2<<8 + b3` (u32) | `raw == 0xFFFFFFFF` → 0; else `raw >u 0x80000000` → `~raw`, a one's-complement magnitude | Metrology read checked. **Coefficient not checked** |
| Energy `0x4200b11e` | u24 as voltage | none; unsigned per DS | Metrology read checked. **Coefficient not checked** |
| Frequency `0x4200b280` | `lhu` + swap (u16) | none | `raw == 0` → error, output unchanged |
| RIF `0x4200b2f0` | `lhu` + swap, stored with `sh` (u16) | none | read failure: output unchanged |

Each coefficient is cached in RAM and re-read only while its cached value is 0:
`0x3fcb5418` RmsUC, `0x3fcb5414` RmsIAC, `0x3fcb5410` PowerPAC and `0x3fcb540c`
EnergyAC. The unchecked reads mean a failed coefficient read can cache stale
buffer bytes until reboot.

## Conversion table

Every step is **IEEE single-precision float** through ROM soft-float, except
the two thresholds, which are compared as doubles. Raw value and coefficient
are each converted by `__floatunsisf` (unsigned int to float). The operations
are listed in **exact program order**, with rodata constants given by address
and value.

| Qty | Raw | Calibration | Exact operation order | Unit | Confidence |
|---|---|---|---|---|---|
| Voltage | RmsU u24 | RmsUC | `raw·coef` → `× 2⁻²² [acd4]` → `÷ 0.51 [acd8]` → `÷ 100 [acdc]` → `× 1000 [ace0]` → `÷ 1000 [ace0]` | V | High |
| Current | RmsIA u24 | RmsIAC | `raw·coef` → `× 2⁻²³ [ace4]` → `÷ 1000 [ace0]` → `× 1000` → `÷ 1000` | A | High |
| Power | PowerPA magnitude | PowerPAC | `mag·coef` → `× 2⁻³¹ [ace8]` → `÷ 0.51 [acd8]` → `× 1000` → `÷ 1000` | W | High |
| Energy | Energy_PA u24 | EnergyAC | `raw·coef` → `× 2⁻²⁹ [acec]` → `÷ 0.51` → `× 1000` → `÷ 1000` = Δ; commit if `(double)Δ ≥ 0.001 [acf8, f64]` | kWh | High |
| Frequency | Ufreq u16 | none | `3579545 [acf4] ÷ (raw × 8 [acf0])` | Hz | High |
| Faults | RIF u16 | none | bit 7 → out 0, bit 9 → out 1, bit 10 → out 2 | flags | High |

Bracketed constants are `0x3c09` + the four digits shown, e.g. `[acd4]` is
`0x3c09acd4`. The validity limits are at `0x3c09a9dc` (45.0), `0x3c09a9e0`
(65.0) and `0x3c09a9e4` (10.0). The current floor is a double at `0x3c09a9e8`
(0.01).
`2⁻²²` is stored as `0x34800000`, `2⁻²³` as `0x34000000`, `2⁻³¹` as
`0x30000000` and `2⁻²⁹` as `0x31000000`.

**Datasheet reconciliation** (DS §10, "calibration-free" formulas):

| DS formula | DS unit | Stock | Constants implied |
|---|---|---|---|
| `Urms = RmsU·RmsUC / (K2·2²²)` | 10 mV | `/100` → V | K2 = 0.51 |
| `Irms = RmsIA·RmsIAC / (K1·2²³)` | mA | `/1000` → A | K1 = 1 |
| `P = PowerPA·PowerPAC / (K1·K2·2³¹)` | W | – | K1·K2 = 0.51 |
| `E = Energy_PA·EnergyAC·HFconst / (K1·K2·2²⁹·4096)` | kWh | HFconst stays 4096, so the term is 1 | K1·K2 = 0.51 |
| `f = CLKI / 8 / Ufreq` | Hz | – | CLKI = 3.579545 MHz nominal |
| RMS bit 23 = 1 means "do zero processing" | – | → 0 | – |

**Rounding.** All float operations round per IEEE 754 single precision.
Conversion to the API is `__fixunssfsi`, which **truncates toward zero**.
Nothing is rounded to nearest, and no other clipping or filtering is applied.

**Unresolved or unexplained:**

- **The trailing `× 1000 ÷ 1000`** on every metrology value. It is computed
  (`0x4200af0e`–`0x4200af22` in the voltage reader, and the same in each other reader) and is almost an
  identity in float, changing at most the last bit or so. Why it is there is
  not known; possibly source-level unit handling that the compiler kept. It
  has no effect a driver needs to reproduce.
- **0.51 as K2.** Its role is established: it sits exactly where the
  datasheet's K2 goes, and it reproduces K1·K2 in both the power and energy
  formulas. Its physical derivation (the ZMPT107-1 burden and series resistors)
  has **not** been verified on the board.

## Call and data-flow map

```text
app_main ── 0x42007976 meter setup
             ├─ 0x4200b3fa  install vtable {open, flush, write, read, nop}
             ├─ 0x4200abc6  open(port 1): UART 9600 8E1 GPIO2/3, then 0xEA 0x96 reset
             └─ 0x4200ad00  init(channel A): E5, 5A, five writes, DC

xTaskCreate(ele_task 0x42007b46)   ; name string referenced at 0x420041d2
  0x4200acf4  acc = NVS "power"    ; float kWh, seeded once
  loop, every 100 ticks (~1 s):
    0x4200ae42  RmsU,RmsUC      → work.v  0x3fcb1210 (+0x00) float V
    0x4200af40  RmsIA,RmsIAC    → work.i  0x3fcb1214 (+0x04) float A
    0x4200b024  PowerPA,PAC     → work.p  0x3fcb1218 (+0x08) float W
    0x4200b11e  Energy_PA,EAC   → work.e  0x3fcb121c (+0x0c) float kWh (= acc 0x3fcb5408)
    0x4200b280  Ufreq           → work.f  0x3fcb1220 (+0x10) float Hz
    0x4200b2f0  RIF             → rif     0x3fcb53d8         u16
    if (double)work.i < 0.01: work.i = work.p = 0
    if 0x42007a34(work) [45 <= f <= 65 and v >= 10]:
        nvs_mirror 0x3fcb5370 = work.e
        0x42007ae8 publish: memcpy(0x3fcb1224, work, 20) under mutex
    vTaskDelay(50)

/update_ele_data (0x42005bc8) ── 0x42007a08 copy of 0x3fcb1224
    +0x00 v → __fixunssfsi → "voltage":%ld
    +0x04 i →  (double)   → "current":%f
    +0x08 p → __fixunssfsi → "power":%ld
    +0x0c e →  (double)   → "ele":%f
    +0x10 f → not in this response

0x42006d50 / 0x42006e5a (two message builders)
    fault byte = rif bits {7, 9, 10} → frame +0
    all 20 bytes of the published struct (v, i, p, e, f) → frame +0x14
    frame → 0x42008cd0   ; destination not identified; likely the Panda Touch / ESP-NOW path

app_ctl_task: every 6000 ticks (60 s, 0x420076a8) → storage request 3
    → NVS namespace "nvs", key "power" = nvs_mirror (4 bytes)
energy reset 0x42007aac: /set reset_usage (0x420068b2), factory reset (0x420045c2),
                         and 0x420057f0, 0x42006bc4 (not identified)
```

## Definitive JSON mapping

| HLW register → raw | Conversion | Stored | Published | `/update_ele_data` field |
|---|---|---|---|---|
| `0x26` RmsU u24 | `0x4200ae42` | `0x3fcb1210` | `0x3fcb1224 +0x00` | **`voltage`**, `%ld` of truncated `u32` |
| `0x24` RmsIA u24 | `0x4200af40` | `0x3fcb1214` | `+0x04` | **`current`**, `%f` |
| `0x2C` PowerPA u32 | `0x4200b024` | `0x3fcb1218` | `+0x08` | **`power`**, `%ld` of truncated `u32` |
| `0x28` Energy_PA u24, accumulated | `0x4200b11e` | `0x3fcb121c` | `+0x0c` | **`ele`**, `%f`, kWh |
| `0x23` Ufreq u16 | `0x4200b280` | `0x3fcb1220` | `+0x10` | none; only in the binary frames |
| `0x42` RIF u16 | `0x4200b2f0` | `0x3fcb53d8` | – | none; fault byte in the binary frames |

The JSON field names come verbatim from rodata: `"voltage":%ld,`
(`0x3c092704`), `"current":%f,` (`0x3c092714`), `"power":%ld,` (`0x3c092724`)
and `"ele":%f}` (`0x3c092758`). The same response also carries
`countdown_state`, `auto_poweroff`, `countdown`, `power_state` and `usb_state`;
those are not meter data.

The builder's loads were checked against these struct offsets
(`0x42005c54 lw 0xc(sp)` = +0, `0x42005c76` = +4, `0x42005c9a` = +8,
`0x42005cf6` = +0xc, all relative to the copy at `sp+0xc`). Voltage, current,
power and energy are not swapped anywhere along the path.

## Energy counter behaviour

- **Accumulator:** a float32 kWh total at `0x3fcb5408`, seeded from NVS
  `power` at boot.
- **Chip counter:** `Energy_PA` runs with `EPA_CA = 1`, so reading it does not
  clear it. Each poll converts the whole counter to Δ kWh.
- **Commit:** when Δ ≥ 0.001 kWh, stock adds Δ to the total. It then clears the
  chip counter: `E5`, `EMUCON2 = 0x006D` (clear on read), two reads of
  `Energy_PA`, `EMUCON2 = 0x046D`, `DC`. At a 1 s poll, Δ only reaches 0.001 kWh
  at 3.6 kW or more; below that the counter accumulates across polls first.
- **Losses:** energy that accumulates during the clear sequence is discarded.
  So is anything since the last NVS save (up to 60 s) on power loss.
- **Wrap:** the chip counter is cleared at about 0.001 kWh, so its 24-bit range
  is never approached. The float32 total's step size grows with it: about
  0.001 kWh near 8192 kWh, and above 32768 kWh a 0.001 kWh commit rounds away.
- **Reset:** see the energy-reset line in the map above.

## Cross-build comparison

| Question | v1.0.0 (`espnow_example`, 2024-10-15) vs v1.0.0.1 |
|---|---|
| Same register set and widths? | Yes |
| Same init values and special commands? | Yes |
| Same calibration strategy? | Yes: the same four coefficients, cached, K = 0.51 |
| Same conversion constants and thresholds? | Yes: every rodata constant, the 0.01 A floor, the 45/65 Hz and 10 V validity limits, the 0.001 kWh commit |
| Same sign and guard logic? | Yes, including the unchecked coefficient reads |
| Behaviour changes | None found |

The `recover_meter.py` reports for the two builds are identical once addresses
are stripped. The sign and guard instructions were compared by hand. The
v1.0.1_beta1 and 01.00.02.05 images were not available and are unchecked.

## Remaining unknowns

- The two energy-reset callers `0x420057f0` and `0x42006bc4`, and where
  `0x42008cd0` sends its frames. None of these affect `dp_meter`.
- The tick rate is 100 Hz by inference from the firmware's own `pdMS_TO_TICKS`
  expansion (`ms × 100 / 1000` at `0x420079c8` and `0x420076d0`), not from a
  stored config value.
- Why the trailing `× 1000 ÷ 1000` exists (see above).
- The physical derivation of 0.51.
- Why a GPIO6 change is acted on only when `0x42007a34(NULL)` passes
  (`0x4200760c`). It is recorded, not interpreted, and it isn't metering.

## Recommended `dp_meter` contract

This is a specification for the next PR, not an implementation.

**Transport**
- UART1 on GPIO2 (TX) and GPIO3 (RX), 9600 baud, 8E1.
- Read: send `[0xA5, reg]`, then receive `len + 1` bytes within at least 30 ms.
- Verify `~(0xA5 + reg + Σdata) & 0xFF`. Unlike stock, reject the frame before
  using any of its bytes.

**Init: two modes**
- **Read-only (default for first bring-up):** no writes at all. Voltage,
  current and power come from reset defaults (`SYSCON` 0x0A04: U and IA on,
  PGA 16). Report energy and frequency as unavailable, because reset `EMUCON`
  0x0000 disables `PARUN` and reset `EMUCON2` 0x0001 disables `ZxEN`.
- **Stock-equivalent (opt-in):** send exactly stock's sequence, verified at the
  instruction level: `0x96` reset; `E5`; `5A`; `0x00=0x0A04`; `0x01=0x0181`;
  `0x13=0x046D`; `0x1D=0x3219`; `0x40=0x4680`; `DC`. Read each register back
  and compare, which stock doesn't do.

**Factory coefficients: must be read at boot**
- Read RmsIAC `0x70`, RmsUC `0x72`, PowerPAC `0x73` and EnergyAC `0x76`, as u16
  big-endian.
- Check every read, and retry on failure or 0.
- Optionally verify the checksum: `0x6F` should equal the low 16 bits of
  `~(0xFFFF + 0x70 + … + 0x77)` (DS p. 58).
- Log the four values.

**Reads each sample**
- `0x26`/3, `0x24`/3, `0x2C`/4, then `0x28`/3 and `0x23`/2 only in
  stock-equivalent mode.
- Keep the raw values alongside the converted ones.

**Cadence:** 1 s, matching stock. Averaged registers update at 3.4 Hz under
stock's `DUPSEL`.

**Conversions**
- V = RmsU·RmsUC / 2²² / 0.51 / 100
- I = RmsIA·RmsIAC / 2²³ / 1000
- P = PowerPA (signed, true two's complement) · PowerPAC / 2³¹ / 0.51
- f = 3579545 / (8·Ufreq)
- Treat an RMS value with bit 23 set as 0, and Ufreq = 0 as "no frequency".
- Use K2 = 0.51 and K1 = 1 as named constants, marked as BTT's unverified
  board constants.
- Converted types: float, or a fixed-point integer such as mV, mA and mW.
  Raw types: u24 for RMS, s32 for power, u24 for energy, u16 for Ufreq.

**Energy (stock-equivalent mode only)**
- Accumulate `Energy_PA` deltas in an integer or a double, not a float32.
- Either clear the chip counter the way stock does, accepting the small
  discard, or track a running u24 and handle its wrap yourself.
- Persist on a bounded schedule, and document the maximum loss on power-off.

**Validity**
- Stock publishes only when 45 ≤ f ≤ 65 Hz and V ≥ 10 V, and zeroes current
  and power below 0.01 A.
- Keep the 0.01 A floor for parity. Report "invalid" or "no mains" explicitly
  rather than holding stale values.

**State across samples:** the four coefficients, the energy accumulator, the
last persisted energy total, and a validity flag. Nothing else.

**Stock API parity:** in `/update_ele_data`, send `voltage` and `power` as
truncated integers, and `current` and `ele` (kWh) as floats.
