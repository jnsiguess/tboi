# -*- coding: utf-8 -*-
"""Methods of an Objective-C class with their addresses in the image.

Method lists in newer images are relative: an entry holds not pointers but
offsets from the entry itself. That is why the address is counted from the
place of the entry; otherwise you get numbers that look like the truth but
lead nowhere.
"""
import os, struct, sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
d = open(os.path.join(ROOT, 'work', 'isaac.arm64'), "rb").read()
BASE = 0x100000000


def u64(o): return struct.unpack_from("<Q", d, o)[0]
def u32(o): return struct.unpack_from("<I", d, o)[0]
def cstr(o):
    e = d.find(b"\0", o)
    return d[o:e].decode("ascii", "replace")


def rebase(v):
    """A pointer in the file is stored chained: the meaning is in the low 36 bits."""
    if v == 0: return 0
    if v >> 63: return 0            # a bind to an external name
    return v & 0xFFFFFFFFF


def sections():
    out = {}
    ncmds = u32(16); p = 32
    for _ in range(ncmds):
        cmd, sz = struct.unpack_from("<II", d, p)
        if cmd == 0x19:
            nsects = u32(p + 64)
            q = p + 72
            for _ in range(nsects):
                name = d[q:q + 16].rstrip(b"\0").decode()
                addr, size, off = struct.unpack_from("<QQI", d, q + 32)
                out[name] = (addr - BASE, size)
                q += 80
        p += sz
    return out


def methods(ro):
    ml = rebase(u64(ro + 0x20))
    if not ml: return []
    entsize = u32(ml) & 0xFFFC          # the top bits are flags, not the size
    count = u32(ml + 4)
    rel = (u32(ml) & 0x80000000) != 0
    out = []
    for i in range(count):
        e = ml + 8 + i * entsize
        if rel:
            nameref = e + struct.unpack_from("<i", d, e)[0]
            name = cstr(rebase(u64(nameref)))
            imp = e + 8 + struct.unpack_from("<i", d, e + 8)[0]
        else:
            name = cstr(rebase(u64(e)))
            imp = rebase(u64(e + 16))
        out.append((name, imp))
    return out


s = sections()
lo, size = s["__objc_classlist"]
want = sys.argv[1] if len(sys.argv) > 1 else None
for i in range(size // 8):
    cls = rebase(u64(lo + i * 8))
    if not cls: continue
    ro = rebase(u64(cls + 0x20)) & 0x00007FFFFFFFFFF8
    name = cstr(rebase(u64(ro + 0x18)))
    if want and name != want: continue
    print("=== %s (class %06x) ===" % (name, cls))
    for m, imp in methods(ro):
        print("  %-44s %06x" % (m, imp))
