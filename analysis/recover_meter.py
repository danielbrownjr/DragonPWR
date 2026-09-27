"""Recover the stock HLW8112 metering path from a Panda PWR app image.

Usage: python recover_meter.py <image>

Nothing is looked up by address. Each piece is found by shape, so the same
script reads other builds:

- read primitive: the function that masks the register with `andi ..,0x7f`
  and checks for the `0xEA` special-command code (`0x4200ac0e` in v1.0.0.1)
- frame writer: the function that sets bit 7 with `ori a1,a1,0x80`
- special-command and 16-bit write wrappers: small functions that call the
  frame writer with `a1 = 0xEA` / `a2 = 2`
- ele_task: the entry point stored next to the "ele_task" name string

For every call to the read primitive the register (`a1`) and byte count
(`a3`) are recovered from the immediates set just before the call. Each
reader's float/double constants and soft-float ROM calls are printed in
program order, which is the stock conversion formula. The results are
written up in docs/METERING_REVERSE_ENGINEERING.md.
"""
import argparse
import struct
import sys

from codemap import _MEMOP, CodeMap, _imm, _sext

# ESP32-C2 ROM entry points, from ESP-IDF v5.1.1 (Apache-2.0): soft float from
# esp_rom/esp32c2/ld/esp32c2.rom.rvfp.ld, memcpy/sprintf from the ROM newlib map.
ROM = {
    0x40000a18: "__adddf3", 0x40000a1c: "__addsf3", 0x40000a20: "__divsf3",
    0x40000a24: "__eqdf2", 0x40000a28: "__eqsf2", 0x40000a2c: "__extendsfdf2",
    0x40000a34: "__fixdfsi", 0x40000a3c: "__fixsfsi", 0x40000a40: "__fixunsdfsi",
    0x40000a48: "__fixunssfsi", 0x40000a54: "__floatsidf", 0x40000a58: "__floatsisf",
    0x40000a64: "__floatunsidf", 0x40000a68: "__floatunsisf", 0x40000a6c: "__gedf2",
    0x40000a70: "__gesf2", 0x40000a74: "__gtdf2", 0x40000a78: "__gtsf2",
    0x40000a7c: "__ledf2", 0x40000a80: "__lesf2", 0x40000a84: "__ltdf2",
    0x40000a88: "__ltsf2", 0x40000a8c: "__muldf3", 0x40000a90: "__mulsf3",
    0x40000a9c: "__subdf3", 0x40000aa0: "__subsf3", 0x40000aa4: "__truncdfsf2",
    0x4000048c: "memcpy", 0x400006a8: "sprintf",
}
HLW8112 = {  # address: (name, bytes) per the HLW8110/HLW8112 datasheet, table 9
    0x00: ("SYSCON", 2), 0x01: ("EMUCON", 2), 0x02: ("HFConst", 2),
    0x13: ("EMUCON2", 2), 0x1D: ("INT", 2), 0x23: ("Ufreq", 2),
    0x24: ("RmsIA", 3), 0x25: ("RmsIB", 3), 0x26: ("RmsU", 3), 0x27: ("PF", 3),
    0x28: ("Energy_PA", 3), 0x29: ("Energy_PB", 3), 0x2C: ("PowerPA", 4),
    0x2D: ("PowerPB", 4), 0x2E: ("PowerS", 4), 0x2F: ("EMUStatus", 3),
    0x40: ("IE", 2), 0x41: ("IF", 2), 0x42: ("RIF", 2), 0x43: ("SysStatus", 1),
    0x6F: ("Coeff_chksum", 2), 0x70: ("RmsIAC", 2), 0x71: ("RmsIBC", 2),
    0x72: ("RmsUC", 2), 0x73: ("PowerPAC", 2), 0x74: ("PowerPBC", 2),
    0x75: ("PowerSC", 2), 0x76: ("EnergyAC", 2), 0x77: ("EnergyBC", 2),
}
SPECIAL = {0xE5: "write enable", 0xDC: "write protect", 0x5A: "select channel A",
           0xA5: "select channel B", 0x96: "chip reset"}
APP = (0x42000000, 0x42010000)


def ops(ins):
    return [p.strip() for p in ins.op_str.split(",")]


def branch_target(ins):
    try:
        return (ins.address + int(ops(ins)[-1], 0)) & 0xFFFFFFFF
    except ValueError:
        return None


class Meter:
    def __init__(self, path):
        self.cm = CodeMap(path)
        self.img = self.cm.img
        starts = {a for a in self.cm.calls if APP[0] <= a < APP[1]}
        # Tail calls: `c.addi sp, +N` (frame pop) then `j target`.
        insns = self.cm.insns
        for k in range(1, len(insns)):
            ins, prev = insns[k], insns[k - 1]
            if ins.mnemonic == "j" and prev.mnemonic in ("c.addi", "c.addi16sp", "addi") \
                    and ops(prev)[0] == "sp" and not ops(prev)[-1].startswith("-"):
                t = branch_target(ins)
                if t is not None and APP[0] <= t < APP[1]:
                    starts.add(t)
        self.starts = sorted(starts)
        self.site_const = {s: t for t, sites in self.cm.consts.items() for s in sites}

    # -- function bodies -------------------------------------------------------
    def body(self, start, limit=400):
        nxt = next((s for s in self.starts if s > start), start + limit * 4)
        i = self.cm.by_addr.get(start)
        out = []
        while i is not None and i < len(self.cm.insns) and self.cm.insns[i].address < nxt:
            out.append(self.cm.insns[i])
            i += 1
        return out

    def find(self, pred):
        return [s for s in self.starts if pred(self.body(s))]

    def calls_from(self, insns):
        """Direct and auipc+jalr call targets, in program order."""
        out = []
        for k, ins in enumerate(insns):
            if ins.mnemonic in ("jal", "c.jal") and (len(ops(ins)) == 1 or ops(ins)[0] == "ra"):
                out.append((ins.address, branch_target(ins)))
            elif ins.mnemonic == "j":
                out.append((ins.address, branch_target(ins)))
            elif ins.mnemonic == "jalr" and k and insns[k - 1].mnemonic == "auipc":
                prev = insns[k - 1]
                hi = _imm(ops(prev)[1])
                off = _imm(ops(ins)[2]) or 0
                out.append((ins.address, (prev.address + (_sext(hi, 20) << 12) + off) & 0xFFFFFFFF))
        return out

    # -- register-value recovery before a call --------------------------------
    def reg_values(self, site, reg, window=24):
        """Immediate values `reg` can hold at `site`, scanning back to the
        previous call. More than one value means the choice is conditional."""
        i = self.cm.by_addr[site]
        seen = []
        for j in range(i - 1, max(0, i - window) - 1, -1):
            ins = self.cm.insns[j]
            if ins.mnemonic in ("jal", "c.jal", "jalr", "c.jalr"):
                break
            o = ops(ins)
            if not o or o[0] != reg:
                continue
            m = ins.mnemonic
            if m == "c.li" or (m == "addi" and o[1] == "zero"):
                seen.append(_imm(o[-1]) & 0xFFFFFFFF)
                continue
            if m == "addi" and o[1] == reg:
                lo = _imm(o[2])
                for p in reversed(self.cm.insns[max(0, j - 4):j]):
                    po = ops(p)
                    if p.mnemonic in ("lui", "c.lui") and po[0] == reg:
                        seen.append(((_sext(_imm(po[1]), 20) << 12) + lo) & 0xFFFFFFFF)
                        break
                break
            break
        return seen

    def reg_before(self, site, reg, window=24):
        vals = self.reg_values(site, reg, window)
        return vals[0] if vals else None

    # -- pieces ---------------------------------------------------------------
    def locate(self):
        def has(insns, mnem, tail):
            return any(i.mnemonic == mnem and i.op_str.replace(" ", "").endswith(tail) for i in insns)

        self.read = self.find(lambda b: has(b, "andi", ",0x7f") and has(b, "addi", "zero,0xea")
                              and has(b, "addi", "zero,-0x5b"))
        self.frame = self.find(lambda b: has(b, "ori", "a1,0x80") and has(b, "addi", "zero,-0x5b"))
        frame = set(self.frame)
        self.special, self.write16 = [], []
        for s in self.starts:
            b = self.body(s, 40)
            if len(b) > 16 or not any(t in frame for _, t in self.calls_from(b)):
                continue
            if has(b, "addi", "a1,zero,0xea"):
                self.special.append(s)
            elif has(b, "slli", "a2,8") or has(b, "slli", "a5,a2,8"):
                self.write16.append(s)

        self.ele_task = None
        for a in self.cm.string_addr("ele_task"):
            for site in self.cm.xrefs(a):
                i = self.cm.by_addr[site]
                for ins in self.cm.insns[i:i + 6]:
                    t = self.site_const.get(ins.address)
                    if t is not None and APP[0] <= t < APP[1]:
                        self.ele_task = t
        return self

    def reads(self):
        read = set(self.read)
        out = []
        for s in self.starts:
            for site, t in self.calls_from(self.body(s)):
                if t in read:
                    out.append((s, site, self.reg_before(site, "a1"), self.reg_before(site, "a3")))
        return out

    def writes(self):
        out = []
        for s in self.starts:
            for site, t in self.calls_from(self.body(s)):
                if t in self.special:
                    out.append((s, site, "special", self.reg_values(site, "a1"), None))
                elif t in self.write16:
                    out.append((s, site, "write16", self.reg_before(site, "a1"), self.reg_before(site, "a2")))
        return out

    def formula(self, start):
        """Rodata constants and soft-float calls, in program order. `lui`
        bases are tracked per register so a base reused across several `lw`
        is still resolved; a `lw` pair from addr and addr+4 is a double."""
        steps = []
        insns = self.body(start)
        calls = dict(self.calls_from(insns))
        base = {}
        last = None
        for ins in insns:
            o = ops(ins)
            m = _MEMOP.match(o[-1]) if o else None
            if ins.mnemonic in ("lui", "c.lui") and len(o) == 2:
                base[o[0]] = (_sext(_imm(o[1]), 20) << 12) & 0xFFFFFFFF
                continue
            if ins.mnemonic in ("lw", "c.lw") and m and m.group(2) in base:
                t = (base[m.group(2)] + _imm(m.group(1))) & 0xFFFFFFFF
                if 0x3C000000 <= t < 0x3C400000:
                    if last is not None and t == last + 4 and steps and steps[-1].startswith("f32"):
                        steps[-1] = f"f64@0x{last:08x}={self.rodata_double(last):.9g}"
                    else:
                        steps.append(f"f32@0x{t:08x}={self.rodata_float(t):.9g}")
                    last = t
                    continue
            if o and o[0] in base and not ins.mnemonic.startswith(("s", "c.s", "b", "c.b")):
                base.pop(o[0])
            if ins.address in calls:
                tgt = calls[ins.address]
                name = ROM.get(tgt)
                if name:
                    steps.append(name)
        return steps

    def rodata_float(self, addr):
        return struct.unpack("<f", self.img.read(addr, 4))[0]

    def rodata_double(self, addr):
        return struct.unpack("<d", self.img.read(addr, 8))[0]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("image")
    args = ap.parse_args()
    m = Meter(args.image).locate()
    img = m.img
    print(f"image        {args.image}")
    print(f"project      {img.project_name} {img.app_version}  built {img.build_date} {img.build_time}")
    print(f"read prim    {', '.join(hex(a) for a in m.read) or 'NOT FOUND'}")
    print(f"frame write  {', '.join(hex(a) for a in m.frame) or 'NOT FOUND'}")
    print(f"special cmd  {', '.join(hex(a) for a in m.special) or 'NOT FOUND'}")
    print(f"write16      {', '.join(hex(a) for a in m.write16) or 'NOT FOUND'}")
    print(f"ele_task     {hex(m.ele_task) if m.ele_task else 'NOT FOUND'}")

    print("\n== register writes and special commands (program order) ==")
    for fn, site, kind, a, b in sorted(m.writes(), key=lambda r: r[1]):
        if kind == "special":
            codes = " or ".join(f"0xEA 0x{v:02X} {SPECIAL.get(v, '?')}" for v in reversed(a))
            print(f"  0x{site:08x} in 0x{fn:08x}  {codes}")
        else:
            name = HLW8112.get(a, ("?",))[0]
            val = f"0x{b:04X}" if b is not None else "?"
            print(f"  0x{site:08x} in 0x{fn:08x}  write 0x{a:02X} {name:<9} = {val}")

    print("\n== register reads ==")
    by_fn = {}
    for fn, site, reg, n in m.reads():
        by_fn.setdefault(fn, []).append((site, reg, n))
        name, width = HLW8112.get(reg, ("?", None))
        flag = "" if width in (None, n) else f"   <-- datasheet width {width}"
        print(f"  0x{site:08x} in 0x{fn:08x}  reg 0x{reg:02X} {name:<12} {n} bytes{flag}")

    if m.ele_task:
        print(f"\n== ele_task 0x{m.ele_task:08x}: calls in order ==")
        for site, t in m.calls_from(m.body(m.ele_task, 200)):
            regs = [f"0x{r:02X} {HLW8112.get(r, ('?',))[0]}" for _, r, _ in by_fn.get(t, [])]
            out = m.reg_before(site, "a1")
            name = ROM.get(t, "")
            extra = f"  out=0x{out:08x}" if out and 0x3FC00000 <= out < 0x40000000 else ""
            print(f"  0x{site:08x} -> 0x{t:08x} {name}{extra}  {' '.join(regs)}")

    if m.ele_task:
        print("\n== post-processing: ele_task and its non-reader callees ==")
        for fn in [m.ele_task] + [t for _, t in m.calls_from(m.body(m.ele_task, 200))
                                  if APP[0] <= t < APP[1] and t not in by_fn]:
            steps = [x for x in m.formula(fn) if x.startswith(("f32", "f64")) or x.startswith("__")]
            if any(x.startswith(("f32", "f64")) for x in steps):
                print(f"  0x{fn:08x}: " + " -> ".join(steps))

    print("\n== conversion steps per reader (constants and soft-float calls, in order) ==")
    for fn in sorted(by_fn):
        regs = ", ".join(f"0x{r:02X} {HLW8112.get(r, ('?',))[0]}" for _, r, _ in by_fn[fn])
        steps = m.formula(fn)
        if not steps:
            continue
        print(f"  0x{fn:08x} [{regs}]")
        print("      " + " -> ".join(steps))


if __name__ == "__main__":
    sys.exit(main())
