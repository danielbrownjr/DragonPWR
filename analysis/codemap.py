"""RISC-V (RV32IMC) static map of a stripped ESP-IDF app image.

The stock Panda PWR image has no symbol table, so everything here is built from
two observations:

1. ESP-IDF's error macros bake `__FUNCTION__` into .rodata. A function that ever
   calls ESP_RETURN_ON_FALSE / ESP_LOGx therefore has its own NAME as a string,
   referenced from inside its own body. That turns "find gpio_config()" into
   "find the code that materializes the address of the string 'gpio_config'".

2. GCC materializes absolute addresses as `lui rd, %hi` + `addi rd, rd, %lo`
   (and PC-relative ones as `auipc` + `addi`/`jalr`), almost always adjacently.
   Pairing those two instructions recovers the constant.

From there: xrefs give function bodies, call targets give function starts, and
the call graph gives callers.
"""
import re
from collections import defaultdict

from capstone import CS_ARCH_RISCV, CS_MODE_RISCV32, CS_MODE_RISCVC, Cs

from esp_image import AppImage, region_of

_IMM = re.compile(r"^-?0x[0-9a-fA-F]+$|^-?\d+$")
_MEMOP = re.compile(r"^(-?(?:0x)?[0-9a-fA-F]+)\((\w+)\)$")


def _imm(text):
    text = text.strip()
    if not _IMM.match(text):
        return None
    return int(text, 16) if "x" in text.lower() else int(text, 10)


def _sext(value, bits):
    sign = 1 << (bits - 1)
    return (value & (sign - 1)) - (value & sign)


class CodeMap:
    def __init__(self, path):
        self.img = AppImage(path)
        self.insns = []
        self.by_addr = {}
        self._disassemble()
        self.consts = defaultdict(list)
        self.calls = defaultdict(list)
        self._analyze()
        self.strings = self._string_index()

    def _disassemble(self):
        md = Cs(CS_ARCH_RISCV, CS_MODE_RISCV32 | CS_MODE_RISCVC)
        for seg in self.img.segments:
            if "code" not in region_of(self.img.chip, seg.load_addr):
                continue
            for ins in md.disasm(seg.data, seg.load_addr):
                self.by_addr[ins.address] = len(self.insns)
                self.insns.append(ins)

    def _analyze(self):
        for i, ins in enumerate(self.insns):
            self._note_const(i, ins)
            self._note_call(i, ins)

    def _note_const(self, i, ins):
        if ins.mnemonic not in ("lui", "auipc", "c.lui"):
            return
        parts = [p.strip() for p in ins.op_str.split(",")]
        if len(parts) != 2:
            return
        rd, hi = parts[0], _imm(parts[1])
        if hi is None:
            return
        base = _sext(hi, 20) << 12
        if ins.mnemonic == "auipc":
            base = (ins.address + base) & 0xFFFFFFFF

        for j in range(i + 1, min(i + 9, len(self.insns))):
            nxt = self.insns[j]
            lo = self._lo_for(nxt, rd)
            if lo is None:
                if self._clobbers(nxt, rd):
                    break
                continue
            self.consts[(base + lo) & 0xFFFFFFFF].append(ins.address)
            break

    @staticmethod
    def _lo_for(ins, rd):
        ops = [p.strip() for p in ins.op_str.split(",")]
        if not ops:
            return None
        if ins.mnemonic == "addi" and len(ops) == 3 and ops[0] == rd and ops[1] == rd:
            return _imm(ops[2])
        if ins.mnemonic == "c.addi" and len(ops) == 2 and ops[0] == rd:
            return _imm(ops[1])
        m = _MEMOP.match(ops[-1])
        if m and m.group(2) == rd:
            return _imm(m.group(1))
        if ins.mnemonic == "jalr" and len(ops) == 3 and ops[1] == rd:
            return _imm(ops[2])
        return None

    @staticmethod
    def _clobbers(ins, rd):
        ops = [p.strip() for p in ins.op_str.split(",")]
        if not ops or ops[0] != rd:
            return False
        return not ins.mnemonic.startswith(("b", "s", "c.s", "c.b"))

    def _note_call(self, i, ins):
        if ins.mnemonic in ("jal", "c.jal"):
            ops = [p.strip() for p in ins.op_str.split(",")]
            off = _imm(ops[-1])
            if off is not None and (len(ops) == 1 or ops[0] == "ra"):
                self.calls[(ins.address + off) & 0xFFFFFFFF].append(ins.address)
            return
        if ins.mnemonic == "jalr":
            ops = [p.strip() for p in ins.op_str.split(",")]
            if len(ops) == 3 and ops[0] == "ra" and ops[1] == "ra" and i:
                prev = self.insns[i - 1]
                if prev.mnemonic == "auipc":
                    hi = _imm(prev.op_str.split(",")[1])
                    off = _imm(ops[2]) or 0
                    if hi is not None:
                        target = (prev.address + (_sext(hi, 20) << 12) + off) & 0xFFFFFFFF
                        self.calls[target].append(prev.address)

    def _string_index(self):
        index = {}
        for seg in self.img.segments:
            if "rodata" not in region_of(self.img.chip, seg.load_addr):
                continue
            for m in re.finditer(rb"[\x20-\x7e]{3,}\x00", seg.data):
                index[seg.load_addr + m.start()] = m.group()[:-1].decode()
        return index

    def string_addr(self, text, exact=True):
        return [a for a, s in sorted(self.strings.items())
                if (s == text if exact else text in s)]

    def xrefs(self, addr):
        return sorted(self.consts.get(addr, []))

    def enclosing_function(self, addr):
        best = None
        for start in self.calls:
            if start <= addr and (best is None or start > best):
                best = start
        return best

    def callers(self, func_addr):
        return sorted(self.calls.get(func_addr, []))

    def window(self, addr, before=0, after=24):
        i = self.by_addr.get(addr)
        if i is None:
            return []
        return self.insns[max(0, i - before):i + after]

    @staticmethod
    def fmt(insns):
        return "\n".join("  0x%08x  %-12s %s" % (i.address, i.mnemonic, i.op_str)
                         for i in insns)
