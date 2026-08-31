"""Scan an app image for `gpio_config_t` initializer templates.

The usual ESP-IDF idiom is

    gpio_config_t io = { .pin_bit_mask = 1ULL << PIN, .mode = ..., ... };
    gpio_config(&io);

and GCC emits the 24-byte initializer as a .rodata template that the prologue
copies onto the stack. Those templates are what we are looking for: they carry
the pin mask and the direction/pull/interrupt intent verbatim.

    struct gpio_config_t {          // sizeof 24, 8-byte aligned
        uint64_t pin_bit_mask;      // +0
        gpio_mode_t mode;           // +8
        gpio_pullup_t pull_up_en;   // +12
        gpio_pulldown_t pull_down_en;// +16
        gpio_int_type_t intr_type;  // +20
    };

Filtering is deliberately strict -- a random 24 bytes matching all five field
constraints at once is rare -- but every hit is still a CANDIDATE until it is
confirmed against the board.
"""
import argparse
import struct
import sys

from esp_image import AppImage

MODES = {0: "DISABLE", 1: "INPUT", 2: "OUTPUT", 3: "INPUT_OUTPUT",
         6: "OUTPUT_OD", 7: "INPUT_OUTPUT_OD"}
INTR = {0: "DISABLE", 1: "POSEDGE", 2: "NEGEDGE", 3: "ANYEDGE",
        4: "LOW_LEVEL", 5: "HIGH_LEVEL"}

# Highest GPIO number that physically exists, per target. Anything above this in
# a pin_bit_mask means we are looking at unrelated data.
MAX_GPIO = {"esp32c2": 20, "esp32c3": 21, "esp32": 39, "esp32s3": 48}


def pins_of(mask, max_gpio):
    return [b for b in range(max_gpio + 1) if mask & (1 << b)]


def scan(img, max_pins):
    max_gpio = MAX_GPIO.get(img.chip)
    if max_gpio is None:
        sys.exit(f"unsupported target for the pin-mask filter: {img.chip}")
    legal = (1 << (max_gpio + 1)) - 1

    hits = []
    for seg in img.segments:
        # Flash/SRAM code segments hold instructions, not initializer templates.
        if "ROM (flash code)" in _region(img, seg) or "IRAM" in _region(img, seg):
            continue
        data = seg.data
        for off in range(0, len(data) - 24 + 1, 4):
            mask, mode, pu, pd, intr = struct.unpack("<QIIII", data[off:off + 24])
            if mask == 0 or mask & ~legal:
                continue
            if mode not in MODES or intr not in INTR or pu > 1 or pd > 1:
                continue
            pins = pins_of(mask, max_gpio)
            if len(pins) > max_pins:
                continue
            hits.append({
                "addr": seg.load_addr + off, "seg": seg.index, "pins": pins,
                "mode": MODES[mode], "pu": pu, "pd": pd, "intr": INTR[intr],
            })
    return hits


def _region(img, seg):
    from esp_image import region_of
    return region_of(img.chip, seg.load_addr)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("images", nargs="+")
    ap.add_argument("--max-pins", type=int, default=4,
                    help="reject masks touching more pins than this (default 4)")
    args = ap.parse_args()

    for path in args.images:
        img = AppImage(path)
        hits = scan(img, args.max_pins)
        print(f"== {path}  ({img.chip}, {img.project_name} {img.app_version})")
        if not hits:
            print("   no candidates")
        for h in hits:
            pins = ",".join(f"GPIO{p}" for p in h["pins"])
            print(f"   0x{h['addr']:08x} seg{h['seg']}  {pins:<28} "
                  f"{h['mode']:<14} pu={h['pu']} pd={h['pd']} intr={h['intr']}")
        print()


if __name__ == "__main__":
    main()
