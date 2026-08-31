# analysis

Static-analysis tooling for BIGTREETECH's stock Panda PWR firmware. Results and
provenance live in [../docs/HARDWARE_ANALYSIS.md](../docs/HARDWARE_ANALYSIS.md).

## Setup

```bash
pip install capstone
```

Then drop the stock images into `stock/` (gitignored — they are BTT's to
distribute). From https://github.com/bigtreetech/PandaPWR:

- `Firmware/1.0.0.1/panda_pwr-v1.0.0.1.bin` — the primary subject
- `Recovery_tool/Recovery_tool.rar` — contains the stock bootloader and the
  partition table, neither of which is published anywhere else

## Use

```bash
python find_pins.py stock/panda_pwr-v1.0.0.1.bin     # the whole report
python esp_image.py stock/panda_pwr-v1.0.0.1.bin     # header + segments only
python dumpfn.py   stock/panda_pwr-v1.0.0.1.bin 0x42007232
```

`find_gpio_config.py` scans rodata for `gpio_config_t` initializer templates. It
finds nothing on these images, but it is cheap and it is the right first attempt
on any new one.

## How function identification works

The images are stripped. ESP-IDF's error macros bake `__FUNCTION__` into rodata,
so any function that logs or asserts carries its own name as a string referenced
from inside its own body. `codemap.py` recovers `lui`/`auipc` + `addi` address
pairs, which turns "find `gpio_config()`" into "find the code that materializes
the address of the string `gpio_config`". Call targets then give function
starts, and the call graph gives callers.
