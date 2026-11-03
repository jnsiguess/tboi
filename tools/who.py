# -*- coding: utf-8 -*-
"""Direct calls of the given address: BL and tail B."""
import os, struct, sys
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
d = open(os.path.join(ROOT, 'work', 'isaac.arm64'), "rb").read()

def calls(target, lo=0x4000, hi=0x8fff30):
    bl, b = [], []
    for off in range(lo, hi, 4):
        w = struct.unpack_from("<I", d, off)[0]
        kind = None
        if (w & 0xFC000000) == 0x94000000: kind = bl
        elif (w & 0xFC000000) == 0x14000000: kind = b
        if kind is None: continue
        imm = w & 0x03FFFFFF
        if imm & 0x02000000: imm -= 0x04000000
        if off + imm * 4 == target: kind.append(off)
    return bl, b

for a in sys.argv[1:]:
    t = int(a, 16)
    bl, b = calls(t)
    print("%x: bl from %s ; b from %s" % (t, ["%x" % x for x in bl], ["%x" % x for x in b]))
