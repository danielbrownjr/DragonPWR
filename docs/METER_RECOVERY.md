# Stock HLW8112 metering path

What BIGTREETECH's stock firmware does with the HLW8112, recovered from the app
image far enough to write `dp_meter` without guessing. Hardware context is in
[HARDWARE_ANALYSIS.md](HARDWARE_ANALYSIS.md#energy-meter); the datasheet is
[HLW8110/HLW8112 REV 1.01](https://datasheet.lcsc.com/datasheet/pdf/7618fcc29341bc35e74ce1d001211dc8.pdf?productCode=C970140)
(Hiliwei).

## Reproduce

```bash
cd analysis
python recover_meter.py stock/panda_pwr-v1.0.0.1.bin
python recover_meter.py stock/panda_pwr-v1.0.0.bin
```

`recover_meter.py` finds every piece by code shape, not by address, and prints
the register reads with their byte counts, every register write and special
command, `ele_task`'s call order, and each reader's constants and soft-float
calls in program order. The stock images stay in the gitignored
`analysis/stock/`.

Two builds were analysed:

| Image | Project | Built | Result |
|---|---|---|---|
| `panda_pwr-v1.0.0.1.bin` | `panda_pwr` 08a40b2 | 2025-01-13 | Primary. Addresses below are from this build |
| `panda_pwr-v1.0.0.bin` | `espnow_example` 1f8bd5e | 2024-10-15 | Every register, width, write value, constant and threshold identical; only addresses differ |

`panda_pwr-v1.0.1_beta1.bin` and `panda_pwr_01.00.02.05.bin` were not available
here and were not checked.

Floating point is soft-float through ROM, so each formula below is read directly
off the call sequence: `__floatunsisf`, `__mulsf3`, `__divsf3` and so on at
`0x40000a18`–`0x40000aa4`, per ESP-IDF v5.1.1's `esp32c2.rom.rvfp.ld`.

## Answer

| Output | Register (datasheet name, width) | Coefficient | Stock formula | Units |
|---|---|---|---|---|
| Voltage | `0x26` RmsU, 24-bit | `0x72` RmsUC | `RmsU · RmsUC / 2²² / 0.51 / 100` | V |
| Current | `0x24` RmsIA, 24-bit | `0x70` RmsIAC | `RmsIA · RmsIAC / 2²³ / 1000` | A |
| Active power | `0x2C` PowerPA, 32-bit | `0x73` PowerPAC | `|PowerPA| · PowerPAC / 2³¹ / 0.51` | W |
| Energy | `0x28` Energy_PA, 24-bit | `0x76` EnergyAC | `Σ Energy_PA · EnergyAC / 2²⁹ / 0.51` | kWh |
| Frequency | `0x23` Ufreq, 16-bit | – | `3579545 / (8 · Ufreq)` | Hz |
| Fault flags | `0x42` RIF, 16-bit | – | bit 7 → 0, bit 9 → 1, bit 10 → 2 | – |

- Every read is big-endian (MSB first), exactly the byte count the datasheet gives.
- Stock **does** read the factory coefficients: four of the eight, each once,
  cached for the life of the boot. It never reads `0x71`, `0x74`, `0x75`, `0x77`
  or the coefficient checksum `0x6F`.
- The conversions use **both** factory coefficients and one hard-coded board
  constant, **0.51**. In the datasheet's calibration-free formulas (§10) that is
  `K2 = 0.51` (voltage-channel ratio) and `K1 = 1` (current channel). No
  per-unit calibration is applied.
- Nothing depends on current channel B. Init selects channel A and no channel B
  register is ever read.

## Call structure

| Address | Role |
|---|---|
| `0x42007976` | Meter setup, from `app_main`: install the transport vtable, open UART1 and reset the chip, then init for channel A |
| `0x4200b3fa` | Vtable: `+4` open (UART config), `+8` flush, `+0xc` write, `+0x10` read, `+0x14` no-op |
| `0x4200abc6` | Open: store the port (1), call `+4`, then send `0xEA 0x96` (**chip reset**) |
| `0x4200ad00` | Init(channel). Reached by a tail `j`, not a call |
| `0x4200acf4` | Seed the energy accumulator. **Not** the init routine the older notes named |
| `0x42007b46` | `ele_task` |
| `0x42007a34` | Validity check |
| `0x42007ae8` | Publish: mutex, then `memcpy` 20 bytes into the shared copy at `0x3fcb1224` |

```text
ele_task:
    seed_energy(nvs_power)                      # 0x4200acf4
    loop:
        if ticks_since_last_poll > 99:          # 0x42007ba2
            read_voltage   -> v                 # 0x4200ae42
            read_current   -> i                 # 0x4200af40
            read_power     -> p                 # 0x4200b024
            read_energy    -> e                 # 0x4200b11e
            read_frequency -> f                 # 0x4200b280
            read_rif       -> status            # 0x4200b2f0
            valid = (45 <= f <= 65) and v >= 10 # 0x42007a34
            if i < 0.01: i = p = 0              # 0x42007c28, double compare
            if valid:
                nvs_power = e                   # 0x42007c62
                publish(v, i, p, e, f)          # 0x42007ae8
            # invalid: nothing published; readers keep the last good values
        vTaskDelay(50)                          # 0x42007c54
```

**The poll is about once a second, not every 100 ms.** 100 ticks at 100 Hz. The
tick rate is not stored in the image; it is read off the firmware's own
`pdMS_TO_TICKS` expansion, `ms × 100 / 1000`, at `0x420079c8` and `0x420076d0`.
The same rate makes the NVS save below a 60 s period. The older HARDWARE_ANALYSIS
statement of 100 ms was wrong.

## Transport

All five routines are in v1.0.0.1; `recover_meter.py` finds them in both builds.

| Routine | Behaviour |
|---|---|
| Frame writer `0x4200ab14` | `[0xA5][reg\|0x80][data…][~(0xA5+cmd+data)]`, at most 4 data bytes. `0xEA` is sent without `\|0x80`. Then `vTaskDelay(5)` (50 ms) |
| Special `0x4200abaa` | The frame writer with `reg = 0xEA` and one data byte |
| Write16 `0x4200acd6` | The frame writer with 2 bytes, high byte first |
| Read `0x4200ac0e` | Send `[0xA5][reg&0x7F]`, then read `len + 1` bytes. The check byte must equal `~(0xA5 + reg + data)`. Refuses `0xEA` and `len > 4` |
| UART read `0x4200b328` | `(port 1, buf, len, 3 ticks)`: a 30 ms timeout per read. A short read is an error |

Read return codes: 0 = ok, −100 = checksum mismatch, other negatives = UART
failure. The data bytes are copied out **before** the checksum is compared, so a
failed read can leave partial or wrong bytes in the caller's buffer.

The UART helpers are identified by argument shape; this build leaves no name
strings for them. Read is `(1, buf, len, ticks)` and write is `(1, buf, len)`.

## Init: `0x4200ad00`

```text
read 0x01, 0x40, 0x13, 0x1D           # 2 bytes each; any failure aborts
0xEA 0xE5                             # write enable
0xEA 0x5A                             # channel A; 0xA5 only if the argument is non-zero; setup passes 0
write 0x00 = 0x0A04                   # SYSCON
write 0x01 = 0x0181                   # EMUCON
write 0x13 = 0x046D                   # EMUCON2
write 0x1D = 0x3219                   # INT
write 0x40 = 0x4680                   # IE
0xEA 0xDC                             # write protect
read back 0x00, 0x01, 0x40, 0x13, 0x1D
```

The read-back values are not compared against what was written. The bit
meanings are decoded in
[HARDWARE_ANALYSIS.md](HARDWARE_ANALYSIS.md#reconciliation-with-the-hlw8112-datasheet).
Stock never writes `HFConst` (`0x02`), so it stays at its reset value `0x1000`
(4096).

## Per-quantity arithmetic

In every reader the coefficient is read once. It is cached in RAM
(`0x3fcb5418` RmsUC, `5414` RmsIAC, `5410` PowerPAC, `540c` EnergyAC) and
re-read only while the cached value is 0. The raw value and coefficient go
through `__floatunsisf`, so all arithmetic is single-precision float. Each
formula also ends `× 1000 / 1000`, a float no-op that is omitted below.

### Voltage: `0x4200ae42`

```text
raw = read(0x26, 3)                   # 0x4200ae62, big-endian 24-bit
if read failed: return error          # the output keeps its previous value
coef = cached(0x72, 2)                # 0x4200ae92; read failure checked
if raw & 0x800000: v = 0              # 0x4200aeac
else: v = raw * coef * 2^-22 / 0.51 / 100
```

The datasheet gives `Urms = RmsU·RmsUC / (K2·2²²)` in units of 10 mV, with K2
the voltage-channel ratio. So `/100` converts to volts, and 0.51 is K2. The
datasheet also says an RMS reading with bit 23 set is to be treated as zero;
stock does exactly that.

### Current: `0x4200af40`

```text
raw  = read(0x24, 3)                  # 0x4200af60
coef = cached(0x70, 2)                # 0x4200af90; failure NOT checked
if raw & 0x800000: i = 0
else: i = raw * coef * 2^-23 / 1000
```

The datasheet gives `Irms = RmsIA·RmsIAC / (K1·2²³)` in mA. So `/1000`
converts to amps, and K1 = 1: there is no board factor on current.

### Active power: `0x4200b024`

```text
raw  = read(0x2C, 4)                  # 0x4200b044, big-endian 32-bit
if raw == 0xFFFFFFFF: p = 0           # 0x4200b06e
coef = cached(0x73, 2)                # 0x4200b086; failure NOT checked
mag  = ~raw if raw > 0x80000000 else raw     # 0x4200b09e, unsigned compare
p    = mag * coef * 2^-31 / 0.51
```

`PowerPA` is 32-bit two's complement. Stock reports only a magnitude, and
computes it as the one's complement, so a negative reading comes out 1 LSB low.
A raw `0xFFFFFFFF` (−1) becomes 0, and `0x80000000` is not negated at all. The
datasheet formula is `P = PowerPA·PowerPAC / (K1·K2·2³¹)`, in W. With K1·K2 =
0.51 that is consistent with the two readers above.

### Energy: `0x4200b11e`

```text
raw   = read(0x28, 3)                 # 0x4200b13e; EPA_CA=1, so reading does NOT clear
coef  = cached(0x76, 2)               # 0x4200b170; failure NOT checked
delta = raw * coef * 2^-29 / 0.51     # kWh
if (double)delta >= 0.001:            # 0x4200b200
    acc += delta                      # float at 0x3fcb5408
    0xEA 0xE5
    write 0x13 = 0x006D               # EPA_CA=0: the next read clears Energy_PA
    read(0x28, 3); read(0x28, 3)      # clear the chip counter
    write 0x13 = 0x046D               # back to not-cleared-on-read
    0xEA 0xDC
e = acc
```

The datasheet formula is `E = Energy_PA·EnergyAC·HFconst / (K1·K2·2²⁹·4096)`,
in kWh. `HFconst/4096` is 1 because `HFConst` stays at its reset value, which
leaves exactly the stock expression.

### Frequency: `0x4200b280`

```text
raw = read(0x23, 2)
if raw == 0: return error             # output keeps its previous value
f = 3579545 / (8 * raw)
```

This is the datasheet formula `f = CLKI / 8 / Ufreq`, with CLKI fixed at the
3.579545 MHz nominal clock.

### Fault flags: `0x4200b2f0`

`RIF` (`0x42`, 16-bit, **cleared by reading**) is stored raw at `0x3fcb53d8`.
Two message builders, `0x42006d50` and `0x42006e5a`, reduce it to three bits:

| Bit out | RIF bit | Datasheet meaning |
|---|---|---|
| 0 | 7 `OIAIF` | Channel A overcurrent |
| 1 | 9 `OVIF` | Overvoltage |
| 2 | 10 `OPIF` | Active power overload |

Stock never writes the thresholds `OVLVL`/`OIALVL`/`OPLVL` (`0x19`–`0x1C`),
which reset to `0xFFFF`. So these flags should never set in practice.

## What the stock API exposes

`/update_ele_data`, built at `0x42005bc8` from the published copy:

| Field | Source | Format |
|---|---|---|
| `voltage` | V | `%ld` of `__fixunssfsi(v)`: **truncated** to whole volts |
| `current` | I | `%f`, 6 decimals |
| `power` | P | `%ld` of `__fixunssfsi(p)`: **truncated** to whole watts |
| `ele` | E | `%f`, kWh |

Frequency and the fault flags are **not** in `/update_ele_data`. Frequency
exists only to gate validity; the fault byte goes to the two other message
builders above. The older statement that the HTTP API returns six meter fields
was wrong: it returns four.

## Energy counter lifecycle

- **Seed at boot:** from NVS namespace `nvs`, key `power` (4-byte float, kWh),
  through `0x4200acf4`.
- **Mirror:** every valid poll copies the running total to `0x3fcb5370`.
- **Persist:** `app_ctl_task` requests a save every 6000 ticks (60 s,
  `0x420076a8`). The storage task writes `power` on request code 3
  (`0x420044f6`). Up to a minute of energy is lost on power-off.
- **Reset:** `0x42007aac` zeroes the published value, the accumulator and the
  mirror. It is called from the stock `/set` handler's `reset_usage` parameter
  (`0x420068b2`), from factory reset (`0x420045c2`), and from two sites not
  identified here (`0x420057f0`, `0x42006bc4`).
- **Chip counter wrap:** the chip's `Energy_PA` is cleared whenever an
  increment of at least 0.001 kWh is committed, so it stays far below its
  24-bit limit and never wraps in normal operation.
- **Commit size:** at 1 s polls a delta only reaches 0.001 kWh at 3.6 kW or
  more. Below that the chip counter keeps accumulating across polls until it
  does, so commits are typically just over 0.001 kWh.
- **Accumulator resolution:** the running total is a float32, and its step
  size grows with the total. From about 8192 kWh the step (≈0.001 kWh) is as
  large as a typical commit, so commits are rounded noticeably. Above 32768 kWh
  a 0.001 kWh commit is less than half a step and rounds away entirely.

## Stock behaviour `dp_meter` should not copy

1. The current, power and energy readers cache their coefficient **without
   checking the read**. A failed or corrupt coefficient read caches whatever
   bytes are in the buffer, usually the previous metrology bytes, until reboot.
2. Negative power is folded to a one's-complement magnitude. `0xFFFFFFFF`
   reads as 0 W.
3. Energy that accumulates between the value read and the two clearing reads
   is discarded.
4. The float32 accumulator loses resolution as it grows.
5. An invalid reading (frequency outside 45–65 Hz, or voltage below 10 V) is
   not published, so the API shows stale values rather than zero or an error.
6. Frequency assumes the nominal 3.579545 MHz clock.
7. Voltage and power are truncated, not rounded, in the stock API.

## Recipe for a read-only `dp_meter`

- **UART:** UART1, TX GPIO2, RX GPIO3, 9600 8E1. For each read send
  `[0xA5, reg]`, read `len + 1` bytes with a timeout of at least 30 ms, and
  check `~(0xA5 + reg + Σdata) & 0xFF`.
- **Reads:** `0x26`/3, `0x24`/3, `0x2C`/4, `0x28`/3, `0x23`/2, all big-endian.
- **Coefficients:** read `0x72`, `0x70`, `0x73`, `0x76` at start, 2 bytes each,
  big-endian. Check each read, and retry on failure or 0. Consider verifying
  them with the coefficient checksum: `0x6F` should equal the low 16 bits of
  `~(0xFFFF + RmsIAC + … + EnergyBC)` (datasheet p. 58).
- **Conversions:** the formulas in the table above, with `K2 = 0.51` and
  `K1 = 1`. The 0.51 is BTT's board constant and its accuracy has not been
  measured.
- **Power sign:** signed 32-bit two's complement, correctly negated.
- **Voltage and current:** reject bit 23 as the datasheet says.
- **Poll interval:** 1 s matches stock. Averaged registers update at 3.4 Hz
  under stock's config.

**What "read-only" can and cannot cover.** Stock configures the chip on every
boot (reset, then the five writes above), and the chip's reset defaults differ:

- `EMUCON` resets to `0x0000`. `PARUN` is then off, so `Energy_PA` does not
  accumulate.
- `EMUCON2` resets to `0x0001`. `ZxEN` is then off, so there is no frequency
  measurement.

A driver that never writes can therefore rely on voltage, current and power
from the reset defaults (`SYSCON` resets to U and channel A on, PGA 16), but not
on energy or frequency after a cold boot of the chip. The five writes that
enable them are known exactly and are what stock sends on every boot, so they
are not guesswork. Whether to replicate them is the next decision.

## Still open

- The two unidentified callers of the energy reset, `0x420057f0` and
  `0x42006bc4`.
- The tick rate is inferred from the `pdMS_TO_TICKS` expansion. Confirm the
  1 s poll on hardware.
- Physical continuity: HLW8112 TX/RX to GPIO3/GPIO2, and whether INT1/INT2 reach
  any ESP pin. See HARDWARE_ANALYSIS open question 6.
- The factory coefficient values are per chip and are not in the image. The
  first `dp_meter` run should log them.
- The v1.0.1_beta1 and 01.00.02.05 images are unchecked.
- Stock only acts on a GPIO6 change when the meter reading is valid (a
  `0x42007a34(NULL)` call at `0x4200760c`). That ties GPIO6 handling to mains
  presence, though not necessarily GPIO6 itself. Recorded here, not
  interpreted.
