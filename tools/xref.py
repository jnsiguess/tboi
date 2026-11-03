# -*- coding: utf-8 -*-
"""Who in the code assembles the given address (adrp+add / adrp+ldr).

An exact search for references beats reading everything through: it leads
straight to the function that works with the string or variable you need.
"""
import os, struct, sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
d = open(os.path.join(ROOT, 'work', 'isaac.arm64'), "rb").read()


def xrefs(targets, lo=0x4000, hi=0x8fff30):
    pages, res = {}, []
    for off in range(lo, hi, 4):
        w = struct.unpack_from("<I", d, off)[0]
        if (w & 0x9F000000) == 0x90000000:
            rd = w & 31
            imm = (((w >> 5) & 0x7FFFF) << 2) | ((w >> 29) & 3)
            if imm & (1 << 20):
                imm -= (1 << 21)
            pages[rd] = ((off & ~0xFFF) + (imm << 12)) & 0xFFFFFFFFF
        elif (w & 0xFF800000) == 0x91000000:
            rn, rd, imm = (w >> 5) & 31, w & 31, (w >> 10) & 0xFFF
            if rn in pages:
                v = pages[rn] + imm
                if v in targets:
                    res.append((off, v))
                pages[rd] = v
            else:
                pages.pop(rd, None)
        elif (w & 0xFFC00000) == 0xF9400000:
            rn, rd, imm = (w >> 5) & 31, w & 31, ((w >> 10) & 0xFFF) * 8
            if rn in pages and pages[rn] + imm in targets:
                res.append((off, pages[rn] + imm))
            pages.pop(rd, None)
    return res


if __name__ == "__main__":
    t = set(int(a, 16) for a in sys.argv[1:])
    for off, v in xrefs(t):
        print("%08x -> %x" % (off, v))
