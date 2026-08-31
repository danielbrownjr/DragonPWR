"""Disassemble one function of a stripped ESP-IDF image.

Usage: python dumpfn.py <image> <addr> [addr ...] [--limit N]

Function extent is approximated as "up to the next known call target", which is
good enough for reading an init routine. Operands that resolve to a known rodata
string or to another identified call target are annotated inline.
"""
import argparse

from codemap import CodeMap


def dump(cm, start, limit):
    starts = sorted(cm.calls)
    end = next((s for s in starts if s > start), start + limit * 4)
    end = min(end, start + limit * 4)

    i = cm.by_addr.get(start)
    if i is None:
        print(f"  0x{start:08x}: not a decoded instruction boundary")
        return
    out = []
    while i < len(cm.insns) and cm.insns[i].address < end:
        ins = cm.insns[i]
        note = ""
        for target, sites in cm.consts.items():
            if ins.address in sites:
                s = cm.strings.get(target)
                note = f"   ; 0x{target:08x}" + (f' "{s}"' if s else "")
                break
        if ins.mnemonic in ("jal", "c.jal"):
            off = ins.op_str.split(",")[-1].strip()
            try:
                note = f"   ; -> 0x{(ins.address + int(off, 0)) & 0xFFFFFFFF:08x}"
            except ValueError:
                pass
        out.append(f"  0x{ins.address:08x}  {ins.mnemonic:<12} {ins.op_str}{note}")
        i += 1
    print("\n".join(out))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("image")
    ap.add_argument("addrs", nargs="+")
    ap.add_argument("--limit", type=int, default=120)
    args = ap.parse_args()
    cm = CodeMap(args.image)
    for a in args.addrs:
        addr = int(a, 0)
        callers = ", ".join(hex(c) for c in cm.callers(addr)) or "(none found)"
        print(f"######## 0x{addr:08x}   callers: {callers}")
        dump(cm, addr, args.limit)
        print()


if __name__ == "__main__":
    main()
