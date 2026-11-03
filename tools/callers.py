# -*- coding: utf-8 -*-
"""Who calls whom: the start of the function at an address, and its callers.

On arm64 instructions are all one length, so a straight linear scan is
reliable: branches are found exactly, with no risk of falling out of step,
which is what would happen on x86.
"""
import os, struct, sys
from capstone import *

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
BIN = os.path.join(ROOT, 'work', 'isaac.arm64')
TEXT_LO, TEXT_HI = 0x4000, 0x8fff30

d = open(BIN, "rb").read()
md = Cs(CS_ARCH_ARM64, CS_MODE_ARM)


def func_start(addr, limit=0x4000):
    """Back up to the prologue: an x29/x30 store with pre-decrement, or sub sp."""
    for off in range(addr & ~3, max(TEXT_LO, addr - limit), -4):
        w = struct.unpack_from("<I", d, off)[0]
        ins = list(md.disasm(d[off:off + 4], off))
        if not ins:
            continue
        i = ins[0]
        if i.mnemonic == "sub" and i.op_str.startswith("sp, sp, #"):
            return off
        if i.mnemonic == "stp" and i.op_str.startswith("x29, x30, [sp, #-"):
            return off
    return None


def callers(lo, hi):
    out = []
    for off in range(TEXT_LO, TEXT_HI, 4):
        w = struct.unpack_from("<I", d, off)[0]
        if (w & 0xFC000000) != 0x94000000:      # BL
            continue
        imm = w & 0x03FFFFFF
        if imm & 0x02000000:
            imm -= 0x04000000
        t = off + imm * 4
        if lo <= t <= hi:
            out.append((off, t))
    return out


def show(addr, depth=0, seen=None):
    seen = seen if seen is not None else set()
    s = func_start(addr)
    if s is None or s in seen:
        print("%s%06x — start not found or already seen" % ("  " * depth, addr))
        return
    seen.add(s)
    cs = callers(s, addr)
    print("%s%06x  (function from %06x)  callers: %d" % ("  " * depth, addr, s, len(cs)))
    if depth >= 6:
        return
    for off, _ in cs[:4]:
        show(off, depth + 1, seen)


if __name__ == "__main__":
    show(int(sys.argv[1], 16))
