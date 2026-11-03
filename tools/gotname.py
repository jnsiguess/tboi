# -*- coding: utf-8 -*-
"""The name of an external function, from the address of its slot in the jump table.

The stub loads the address from the slot; which name the loader will put in
there is recorded in the chained fixups. That is how the name is found out
from the stub.
"""
import os, struct, sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
BIN = os.path.join(ROOT, 'work', 'isaac.arm64')
d = open(BIN, "rb").read()

ncmds = struct.unpack_from("<I", d, 16)[0]
p, fx = 32, None
segs = []
for _ in range(ncmds):
    cmd, sz = struct.unpack_from("<II", d, p)
    if cmd == 0x19:
        name = d[p + 8:p + 24].rstrip(b"\0").decode()
        vm, vmsz, fo, fsz = struct.unpack_from("<QQQQ", d, p + 24)
        segs.append((name, vm - 0x100000000, fo))
    elif cmd == 0x80000034:
        off, size = struct.unpack_from("<II", d, p + 8)
        fx = off
    p += sz

(fver, starts_off, imports_off, symbols_off, imports_count,
 imports_format, symbols_format) = struct.unpack_from("<7I", d, fx)
symbols = fx + symbols_off
imports = fx + imports_off


def name_of(i):
    rec = struct.unpack_from("<I", d, imports + 4 * i)[0]
    off = rec >> 9
    e = d.find(b"\0", symbols + off)
    return d[symbols + off:e].decode()


def bind_at(vaddr):
    """The value in the file at the slot address: top bit means a bind, ordinal in the low bits."""
    v = struct.unpack_from("<Q", d, vaddr)[0]
    if not (v >> 63):
        return None
    return name_of(v & 0xFFFFFF)


for a in sys.argv[1:]:
    v = int(a, 16)
    print("%x: %s" % (v, bind_at(v)))
