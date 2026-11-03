# -*- coding: utf-8 -*-
"""The image's own pointers (chained-fixup rebases): where they stand and what
they point to. With an address: every pointer to it (vtables to typeinfo, a
typeinfo to its name, tables to functions).

    py -3 tools/ptrs.py ADDR [ADDR...]       who holds a pointer to ADDR
    py -3 tools/ptrs.py --at ADDR N          N pointers read from ADDR on

The image maps at 0 (vmaddr = offset), as everywhere in tools/.
"""
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import fixups  # noqa: E402

_CACHE = None


def rebases():
    """{location: target} for every own pointer of the image."""
    global _CACHE
    if _CACHE is not None:
        return _CACHE
    d, segs, fix = fixups.load(fixups.BIN)
    base, _ = fix
    starts_off = struct.unpack_from("<7I", d, base)[1]
    st = base + starts_off
    seg_count = struct.unpack_from("<I", d, st)[0]
    out = {}
    for i in range(seg_count):
        seg_info = struct.unpack_from("<I", d, st + 4 + i * 4)[0]
        if seg_info == 0:
            continue
        s = st + seg_info
        (_, page_size, ptr_format, _, _, page_count) = struct.unpack_from("<IHHQIH", d, s)
        starts = struct.unpack_from("<%dH" % page_count, d, s + 22)
        seg = segs[i]
        for pi, first in enumerate(starts):
            if first == 0xFFFF:
                continue
            at = seg[3] + pi * page_size + first
            while True:
                raw = struct.unpack_from("<Q", d, at)[0]
                nxt = (raw >> 51) & 0xFFF
                if not (raw >> 63) & 1:
                    # DYLD_CHAINED_PTR_64 / 64_OFFSET: target in the low 36 bits, high8 at 36
                    target = raw & 0xFFFFFFFFF
                    loc = at - seg[3] + seg[1] - 0x100000000   # image offsets, as tools/ use
                    out[loc] = target
                if nxt == 0:
                    break
                at += nxt * 4
    _CACHE = out
    return out


def main():
    r = rebases()
    if sys.argv[1] == '--at':
        a, n = int(sys.argv[2], 16), int(sys.argv[3])
        for k in range(n):
            loc = a + 8 * k
            print('%x: %s' % (loc, ('%x' % r[loc]) if loc in r else '-'))
        return
    want = {int(x, 16) for x in sys.argv[1:]}
    for loc, t in sorted(r.items()):
        if t in want:
            print('%x -> %x' % (loc, t))


if __name__ == '__main__':
    main()
