# -*- coding: utf-8 -*-
"""Who branches INTO a given region of code.

The rule "collect every branch target before editing" cannot be followed
without this: menu branches share tails, and a branch that looks free may be
somebody's entry point. B, BL, B.cond, CBZ/CBNZ, TBZ/TBNZ and ADR are counted;
the last one matters in its own right, because it is what builds the base of a
branch table.

Branches from inside the region are printed too: they show where its seams are.
The "FROM OUTSIDE" mark is what the whole thing was built for.

    py -3 tools/into.py 6e337c 6e36b0
"""
import os, struct, sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
BIN = os.path.join(ROOT, 'work', 'isaac.arm64')
TEXT_LO, TEXT_HI = 0x4000, 0x8fff30
d = open(BIN, "rb").read()


def sign(v, bit):
    return v - (1 << bit) if v & (1 << (bit - 1)) else v


def target(off):
    """Destination address of the instruction at offset off, or None."""
    w = struct.unpack_from("<I", d, off)[0]
    if (w & 0x7C000000) == 0x14000000:                       # B / BL
        return off + sign(w & 0x03FFFFFF, 26) * 4, "bl" if (w >> 31) else "b"
    if (w & 0xFF000010) == 0x54000000:                       # B.cond
        return off + sign((w >> 5) & 0x7FFFF, 19) * 4, "b.cond"
    if (w & 0x7E000000) == 0x34000000:                       # CBZ / CBNZ
        return off + sign((w >> 5) & 0x7FFFF, 19) * 4, "cbz"
    if (w & 0x7E000000) == 0x36000000:                       # TBZ / TBNZ
        return off + sign((w >> 5) & 0x3FFF, 14) * 4, "tbz"
    if (w & 0x9F000000) == 0x10000000:                       # ADR
        imm = ((w >> 5) & 0x7FFFF) << 2 | ((w >> 29) & 3)
        return off + sign(imm, 21), "adr"
    return None


def find(lo, hi):
    inside, outside = [], []
    for off in range(TEXT_LO, TEXT_HI, 4):
        c = target(off)
        if not c or not (lo <= c[0] < hi):
            continue
        (inside if lo <= off < hi else outside).append((off, c[1], c[0]))
    return inside, outside


if __name__ == "__main__":
    lo, hi = (int(a, 16) for a in sys.argv[1:3])
    inside, outside = find(lo, hi)
    for off, kind, dst in outside:
        print("%06x  %-6s -> %06x   <<< FROM OUTSIDE" % (off, kind, dst))
    for off, kind, dst in inside:
        print("%06x  %-6s -> %06x" % (off, kind, dst))
    targets = sorted(set(dst for _, _, dst in inside + outside))
    print("\ntotal entries %d, of them from outside %d" % (len(inside) + len(outside), len(outside)))
    print("entry points in use: " + " ".join("%06x" % t for t in targets))
