# -*- coding: utf-8 -*-
"""Parsing the Mach-O chained fixups (LC_DYLD_CHAINED_FIXUPS).

The whole loader rests on them: this is the list of what has to be corrected in
the image after it is mapped into memory. Our own pointers get shifted by the
base, foreign ones get substituted from our stubs. The format is new but
finite, and here it is walked through in full, to make sure it reads with no
blank spots.
"""
import os, struct, sys, collections

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
BIN = os.path.join(ROOT, 'work', 'isaac.arm64')

LC_SEGMENT_64 = 0x19
LC_DYLD_CHAINED_FIXUPS = 0x80000034
LC_SYMTAB = 0x2

# kinds of pointer in the chain
PTR_ARM64E = 1
PTR_64 = 2
PTR_32 = 3
PTR_64_OFFSET = 6
PTR_ARM64E_USERLAND24 = 12

KINDS = {1: "arm64e", 2: "64", 3: "32", 6: "64_offset", 12: "arm64e_userland24"}


def load(path):
    d = open(path, "rb").read()
    cpu, sub, ftype, ncmds, sizeofcmds, flags, _ = struct.unpack_from("<IIIIIII", d, 4)
    segs, fix = [], None
    p = 32
    for _ in range(ncmds):
        cmd, sz = struct.unpack_from("<II", d, p)
        if cmd == LC_SEGMENT_64:
            name = d[p + 8:p + 24].split(b"\0", 1)[0].decode()
            vmaddr, vmsize, fileoff, filesize = struct.unpack_from("<QQQQ", d, p + 24)
            segs.append((name, vmaddr, vmsize, fileoff, filesize))
        elif cmd == LC_DYLD_CHAINED_FIXUPS:
            off, size = struct.unpack_from("<II", d, p + 8)
            fix = (off, size)
        p += sz
    return d, segs, fix


def cstr(d, at):
    e = d.index(b"\0", at)
    return d[at:e].decode("utf-8", "replace")


def imports(d, base, hdr):
    fmt, count, off = hdr["imports_format"], hdr["imports_count"], hdr["imports_offset"]
    syms = base + hdr["symbols_offset"]
    out = []
    for i in range(count):
        if fmt == 1:                                  # DYLD_CHAINED_IMPORT
            v = struct.unpack_from("<I", d, base + off + i * 4)[0]
            lib = v & 0xFF
            weak = (v >> 8) & 1
            name = v >> 9
        elif fmt == 2:                                # ..._ADDEND
            v, addend = struct.unpack_from("<Ii", d, base + off + i * 8)
            lib = v & 0xFF; weak = (v >> 8) & 1; name = v >> 9
        else:                                         # ..._ADDEND64
            v = struct.unpack_from("<Q", d, base + off + i * 16)[0]
            lib = v & 0xFFFF; weak = (v >> 16) & 1; name = v >> 32
        out.append((cstr(d, syms + name), lib, weak))
    return out


def walk(d, segs, fix):
    base, size = fix
    (version, starts_off, imports_off, symbols_off,
     imports_count, imports_format, symbols_format) = struct.unpack_from("<7I", d, base)
    hdr = dict(imports_offset=imports_off, imports_count=imports_count,
               imports_format=imports_format, symbols_offset=symbols_off)
    print("header version %d, symbols in format %d, binds %d, record kind %d"
          % (version, symbols_format, imports_count, imports_format))
    imps = imports(d, base, hdr)

    st = base + starts_off
    seg_count = struct.unpack_from("<I", d, st)[0]
    binds = collections.Counter()
    rebases = 0
    kinds = collections.Counter()
    pages = 0

    for i in range(seg_count):
        seg_info = struct.unpack_from("<I", d, st + 4 + i * 4)[0]
        if seg_info == 0:
            continue
        s = st + seg_info
        (size_, page_size, ptr_format, segment_offset,
         max_valid, page_count) = struct.unpack_from("<IHHQIH", d, s)
        kinds[KINDS.get(ptr_format, ptr_format)] += page_count
        pages += page_count
        starts = struct.unpack_from("<%dH" % page_count, d, s + 22)
        segname = segs[i][0] if i < len(segs) else "?"
        for pi, first in enumerate(starts):
            if first == 0xFFFF:                       # no fixups on this page
                continue
            # offset in the file: the segment is mapped one to one
            seg = segs[i]
            at = seg[3] + pi * page_size + first      # the segment lands in the file one to one
            while True:
                raw = struct.unpack_from("<Q", d, at)[0]
                is_bind = (raw >> 63) & 1
                nxt = (raw >> 51) & 0xFFF
                if is_bind:
                    ordinal = raw & 0xFFFFFF
                    if ordinal < len(imps):
                        binds[imps[ordinal][0]] += 1
                    else:
                        binds["?ordinal%d" % ordinal] += 1
                else:
                    rebases += 1
                if nxt == 0:
                    break
                at += nxt * 4
    print("segments with fixups: %d, pages: %d" % (seg_count, pages))
    print("pointer kinds:", dict(kinds))
    print("own pointers (rebase): %d" % rebases)
    print("foreign (bind): %d, distinct names: %d" % (sum(binds.values()), len(binds)))
    return imps, binds


if __name__ == "__main__":
    d, segs, fix = load(BIN)
    print("=== segments ===")
    for name, vmaddr, vmsize, fileoff, filesize in segs:
        print("  %-12s vm %012x + %-9x file %9d + %d" % (name, vmaddr, vmsize, fileoff, filesize))
    if not fix:
        raise SystemExit("no chained fixups")
    print("\n=== chained fixups ===")
    imps, binds = walk(d, segs, fix)
    print("\n=== most often substituted ===")
    for n, c in binds.most_common(15):
        print("   %-42s %d" % (n, c))
