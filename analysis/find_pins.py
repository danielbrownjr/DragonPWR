"""Recover the Panda PWR pin map from a stock ESP-IDF app image.

Everything asserted in docs/HARDWARE_ANALYSIS.md is reproduced by:

    python find_pins.py stock/panda_pwr-v1.0.0.1.bin

Three passes:

  drivers   which ESP-IDF components are linked at all, which upper-bounds what
            the hardware can possibly be
  gpio      every gpio_config() call site, with its gpio_config_t rebuilt from
            the immediate stores that construct it on the stack
  usage     gpio_set_level() / gpio_get_level() call sites that name a pin
            outright, which is what ties a pin to a product function
  buses     uart_set_pin() and spi_bus_initialize() argument constants, which
            carry the bus pin assignments

The stack reconstruction is deliberately simple: it tracks immediates into
registers and the spills of those registers into sp-relative slots. Anything
computed at run time is reported as "?" rather than guessed.
"""
import argparse
import itertools
import re

from codemap import CodeMap

GPIO_MODES = {0: "DISABLE", 1: "INPUT", 2: "OUTPUT", 3: "INPUT_OUTPUT",
              6: "OUTPUT_OD", 7: "INPUT_OUTPUT_OD"}
GPIO_INTR = {0: "DISABLE", 1: "POSEDGE", 2: "NEGEDGE", 3: "ANYEDGE",
             4: "LOW_LEVEL", 5: "HIGH_LEVEL"}

_MEMOP = re.compile(r"^(-?(?:0x)?[0-9a-fA-F]+)\((\w+)\)$")
_COMPONENT = re.compile(r"components/([\w/-]+?)/[\w.-]+\.[ch]$")
_CALLER_SAVED = ("ra", "a0", "a1", "a2", "a3", "a4", "a5", "a6", "a7",
                 "t0", "t1", "t2", "t3", "t4", "t5", "t6")


def _int(text):
    text = text.strip()
    try:
        if text.lower().startswith(("0x", "-0x")):
            return int(text, 16)
        return int(text, 10)
    except ValueError:
        return None


class StackState:
    """Replay the immediates a basic block spills onto its stack frame."""

    def __init__(self, cm, call_site, back):
        self.slots = {}   # sp-relative offset -> value
        self.regs = {}    # register -> immediate
        self.ptrs = {}    # register -> sp-relative offset it points at
        i = cm.by_addr[call_site]
        for ins in cm.insns[max(0, i - back):i]:
            self._step(ins)

    def _step(self, ins):
        ops = [p.strip() for p in ins.op_str.split(",")]
        if not ops:
            return
        rd = ops[0]

        if ins.mnemonic in ("c.swsp", "sw", "c.sw"):
            off = self._sp_offset(ops[-1])
            if off is not None:
                self.slots[off] = 0 if rd == "zero" else self.regs.get(rd)
            return
        if ins.mnemonic in ("jal", "jalr", "c.jal", "c.jalr"):
            # A call clobbers every caller-saved register, so anything we think
            # we know about a0-a7/t0-t6 stops being true here. Without this the
            # replay happily reports a stale immediate as a return value.
            for reg in _CALLER_SAVED:
                self.regs.pop(reg, None)
                self.ptrs.pop(reg, None)
            return
        if ins.mnemonic == "c.li" and len(ops) == 2:
            self.regs[rd] = _int(ops[1])
        elif ins.mnemonic == "addi" and len(ops) == 3 and ops[1] == "zero":
            self.regs[rd] = _int(ops[2])
        elif ins.mnemonic in ("lui", "c.lui") and len(ops) == 2:
            value = _int(ops[1])
            self.regs[rd] = None if value is None else value << 12
        elif ins.mnemonic in ("addi", "c.addi"):
            src = ops[1] if len(ops) == 3 else rd
            imm = _int(ops[-1])
            # `addi a4, s0, -0x48` re-bases a frame pointer: keep tracking it as
            # a stack slot address, not as an arithmetic value.
            if src in self.ptrs and imm is not None:
                self.ptrs[rd] = self.ptrs[src] + imm
                self.regs.pop(rd, None)
                return
            base = self.regs.get(src)
            self.regs[rd] = None if base is None or imm is None else base + imm
        elif ins.mnemonic == "c.addi4spn" and len(ops) == 3 and ops[1] == "sp":
            self.ptrs[rd] = _int(ops[2])
            self.regs.pop(rd, None)
        elif ins.mnemonic == "c.mv" and len(ops) == 2:
            for table in (self.regs, self.ptrs):
                if ops[1] in table:
                    table[rd] = table[ops[1]]
                else:
                    table.pop(rd, None)
        elif ins.mnemonic != "c.addi16sp":
            self.regs.pop(rd, None)
            self.ptrs.pop(rd, None)

    def _sp_offset(self, operand):
        m = _MEMOP.match(operand)
        if not m:
            return None
        off, base = _int(m.group(1)), m.group(2)
        if base == "sp":
            return off
        anchor = self.ptrs.get(base)
        if anchor is None or off is None:
            return None
        return anchor + off

    def struct_at(self, base, count):
        return [self.slots.get(base + 4 * n) for n in range(count)]


def fmt(value, table=None):
    if value is None:
        return "?"
    if table is not None:
        return table.get(value, str(value))
    return str(value)


def pins_from_mask(lo, hi, max_gpio=20):
    if lo is None:
        return None
    mask = (lo & 0xFFFFFFFF) | ((hi or 0) << 32)
    return [b for b in range(max_gpio + 1) if mask & (1 << b)]


def report_drivers(cm):
    print("== ESP-IDF components linked into the image")
    seen = set()
    for _, text in cm.strings.items():
        m = _COMPONENT.search(text)
        if m:
            seen.add(m.group(1))
    for name in sorted(seen):
        print("   " + name)


def report_gpio(cm):
    named = cm.string_addr("gpio_config")
    if not named:
        print("\n== gpio_config(): no __FUNCTION__ string, cannot locate")
        return
    fn = cm.enclosing_function(cm.xrefs(named[0])[0])
    print(f"\n== gpio_config() at 0x{fn:08x} -- {len(cm.callers(fn))} call sites")
    for site in cm.callers(fn):
        state = StackState(cm, site, back=44)
        base = state.ptrs.get("a0")
        if base is None:
            print(f"   0x{site:08x}  struct pointer is not sp-relative "
                  f"(in fn 0x{cm.enclosing_function(site):08x})")
            continue
        lo, hi, mode, pull_up, pull_down, intr = state.struct_at(base, 6)
        pins = pins_from_mask(lo, hi)
        label = ",".join(f"GPIO{p}" for p in pins) if pins else "?"
        print(f"   0x{site:08x}  {label:<20} mode={fmt(mode, GPIO_MODES):<14}"
              f" pull_up={fmt(pull_up)} pull_down={fmt(pull_down)}"
              f" intr={fmt(intr, GPIO_INTR):<9}"
              f" (in fn 0x{cm.enclosing_function(site):08x})")


APP_CODE = range(0x42004000, 0x4200D000)


def _reg_immediate(cm, site, reg, back=20):
    """Immediate in `reg` at a call site, or None if it is computed."""
    i = cm.by_addr[site]
    for j in range(i - 1, max(0, i - back), -1):
        ins = cm.insns[j]
        if ins.mnemonic in ("jal", "jalr", "c.jal", "c.jalr"):
            return None       # an intervening call clobbers the arg registers
        ops = [p.strip() for p in ins.op_str.split(",")]
        if not ops or ops[0] != reg:
            continue
        if ins.mnemonic == "c.li" and len(ops) == 2:
            return _int(ops[1])
        if ins.mnemonic == "addi" and len(ops) == 3 and ops[1] == "zero":
            return _int(ops[2])
        return None
    return None


def _reads_a1(cm, fn):
    """Whether a function takes a second argument.

    Bounded by the next known function start: gpio_get_level is only a handful
    of instructions, so a fixed instruction budget runs straight past its return
    and picks up the next function's registers.
    """
    i = cm.by_addr.get(fn)
    if i is None:
        return False
    end = min((s for s in cm.calls if s > fn), default=fn + 0x40)
    body = itertools.takewhile(lambda ins: ins.address < end, cm.insns[i:])
    return any("a1" in ins.op_str for ins in body)


def report_pin_usage(cm):
    """gpio_set_level / gpio_get_level call sites that name a pin outright.

    Both live in gpio.c just below gpio_config and neither names itself in
    rodata, so they are found positionally and then told apart by arity:
    gpio_set_level takes a level in a1, gpio_get_level does not.
    """
    named = cm.string_addr("gpio_config")
    if not named:
        return
    config_fn = cm.enclosing_function(cm.xrefs(named[0])[0])
    print("\n== GPIO leaf calls from application code")
    window = range(config_fn - 0x400, config_fn)
    for fn in sorted(t for t in cm.calls if t in window):
        sites = [s for s in cm.callers(fn) if s in APP_CODE]
        pins = [(s, _reg_immediate(cm, s, "a0")) for s in sites]
        if not any(p is not None and 0 <= p <= 20 for _, p in pins):
            continue
        takes_level = _reads_a1(cm, fn)
        print(f"   0x{fn:08x}  {'gpio_set_level' if takes_level else 'gpio_get_level'}"
              f" ({len(sites)} application call sites)")
        for site, pin in pins:
            level = f"  level={fmt(_reg_immediate(cm, site, 'a1'))}" if takes_level else ""
            label = f"GPIO{pin}" if pin is not None else "GPIO?"
            print(f"      0x{site:08x}  {label:<8}{level}"
                  f"   (in fn 0x{cm.enclosing_function(site):08x})")


def report_buses(cm):
    print("\n== bus pin assignments")

    # uart_set_pin(port, tx, rx, rts, cts) does not name itself in rodata, so it
    # is matched structurally: five small immediates with rts and cts pinned off.
    for fn, sites in sorted(cm.calls.items()):
        for site in sites:
            state = StackState(cm, site, back=20)
            port, tx, rx, rts, cts = (state.regs.get(r) for r in
                                      ("a0", "a1", "a2", "a3", "a4"))
            if None in (port, tx, rx) or rts != -1 or cts != -1:
                continue
            if not (0 <= port <= 2 and 0 <= tx <= 20 and 0 <= rx <= 20):
                continue
            print(f"   uart_set_pin  0x{fn:08x} from 0x{site:08x}: "
                  f"UART{port} tx=GPIO{tx} rx=GPIO{rx} "
                  f"(in fn 0x{cm.enclosing_function(site):08x})")

    # spi_bus_initialize is reached from the alloc_dma_chan assert string.
    alloc = cm.string_addr("alloc_dma_chan")
    if not alloc:
        return
    dma_fn = cm.enclosing_function(cm.xrefs(alloc[0])[0])
    for call in cm.callers(dma_fn):
        bus_init = cm.enclosing_function(call)
        for site in cm.callers(bus_init):
            state = StackState(cm, site, back=60)
            base = state.ptrs.get("a1")
            if base is None:
                print(f"   spi_bus_initialize 0x{bus_init:08x} from 0x{site:08x}: "
                      f"config is not sp-relative")
                continue
            mosi, miso, sclk, quadwp, quadhd = state.struct_at(base, 5)
            print(f"   spi_bus_initialize 0x{bus_init:08x} from 0x{site:08x}: "
                  f"mosi={fmt(mosi)} miso={fmt(miso)} sclk={fmt(sclk)} "
                  f"quadwp={fmt(quadwp)} quadhd={fmt(quadhd)} "
                  f"(in fn 0x{cm.enclosing_function(site):08x})")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("image")
    args = parser.parse_args()
    cm = CodeMap(args.image)
    print(cm.img.describe())
    print()
    report_drivers(cm)
    report_gpio(cm)
    report_pin_usage(cm)
    report_buses(cm)


if __name__ == "__main__":
    main()
