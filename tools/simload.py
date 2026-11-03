# -*- coding: utf-8 -*-
"""A trial load of the image: map it, apply the fixups, read the classes.

This is a proof of the loader's logic before it gets written in C for the
phone. If the list of Objective-C classes reads out as names once the fixups
have been applied, then the mapping, the chains and the address arithmetic are
all correct.
"""
import os, struct, collections

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
BIN = os.path.join(ROOT, 'work', 'isaac.arm64')
LC_SEGMENT_64 = 0x19
LC_DYLD_CHAINED_FIXUPS = 0x80000034
LC_MAIN = 0x80000028

BASE = 0x200000000          # where we "map" to: anything, as long as it differs from the original


class Image:
    def __init__(self, path):
        self.raw = open(path, "rb").read()
        self.segs = []
        self.sects = {}
        self.fix = None
        self.entryoff = None
        self._commands()
        self._map()

    def _commands(self):
        d = self.raw
        ncmds = struct.unpack_from("<I", d, 16)[0]
        p = 32
        for _ in range(ncmds):
            cmd, sz = struct.unpack_from("<II", d, p)
            if cmd == LC_SEGMENT_64:
                name = d[p + 8:p + 24].split(b"\0", 1)[0].decode()
                vmaddr, vmsize, fileoff, filesize = struct.unpack_from("<QQQQ", d, p + 24)
                nsects = struct.unpack_from("<I", d, p + 64)[0]
                self.segs.append(dict(name=name, vmaddr=vmaddr, vmsize=vmsize,
                                      fileoff=fileoff, filesize=filesize))
                q = p + 72
                for _ in range(nsects):
                    sn = d[q:q + 16].split(b"\0", 1)[0].decode()
                    sg = d[q + 16:q + 32].split(b"\0", 1)[0].decode()
                    addr, size = struct.unpack_from("<QQ", d, q + 32)
                    self.sects[(sg, sn)] = (addr, size)
                    q += 80
            elif cmd == LC_DYLD_CHAINED_FIXUPS:
                self.fix = struct.unpack_from("<II", d, p + 8)
            elif cmd == LC_MAIN:
                self.entryoff = struct.unpack_from("<Q", d, p + 8)[0]
            p += sz
        self.imagebase = min(s["vmaddr"] for s in self.segs)   # __PAGEZERO, that is 0

    def _map(self):
        """Mapping: the segments lie at their own addresses, the holes stay zero."""
        top = max(s["vmaddr"] + s["vmsize"] for s in self.segs if s["name"] != "__PAGEZERO")
        lo = min(s["vmaddr"] for s in self.segs if s["name"] != "__PAGEZERO")
        self.lo, self.hi = lo, top
        self.mem = bytearray(top - lo)
        for s in self.segs:
            if s["name"] == "__PAGEZERO" or s["filesize"] == 0:
                continue
            at = s["vmaddr"] - lo
            self.mem[at:at + s["filesize"]] = self.raw[s["fileoff"]:s["fileoff"] + s["filesize"]]
        # slide: original address -> new one
        self.slide = BASE - lo

    # --- access by the image's "real" address
    def at(self, addr):
        return addr - BASE + self.lo - self.lo if False else addr - BASE

    def u64(self, addr):
        return struct.unpack_from("<Q", self.mem, addr - BASE)[0]

    def put64(self, addr, v):
        struct.pack_into("<Q", self.mem, addr - BASE, v & 0xFFFFFFFFFFFFFFFF)

    def cstr(self, addr):
        o = addr - BASE
        e = self.mem.index(b"\0", o)
        return self.mem[o:e].decode("utf-8", "replace")

    # ------------------------------------------------------------------

    def cstr_raw(self, off):
        e = self.raw.index(b"\0", off)
        return self.raw[off:e].decode("utf-8", "replace")

    def apply_fixups(self):
        d = self.raw
        base, _ = self.fix
        (version, starts_off, imports_off, symbols_off,
         imports_count, imports_format, symbols_format) = struct.unpack_from("<7I", d, base)
        syms = base + symbols_off
        names = []
        for i in range(imports_count):
            v = struct.unpack_from("<I", d, base + imports_off + i * 4)[0]
            names.append(self.cstr_raw(syms + (v >> 9)))

        # for now the foreign names point into a "swamp": a conspicuous address
        # that makes it obvious at once that no stub has been installed
        SWAMP = 0xDEAD0000_00000000
        stubs = [SWAMP | i for i in range(len(names))]

        st = base + starts_off
        seg_count = struct.unpack_from("<I", d, st)[0]
        nreb = nbind = 0
        for i in range(seg_count):
            info = struct.unpack_from("<I", d, st + 4 + i * 4)[0]
            if info == 0:
                continue
            s = st + info
            (size_, page_size, ptr_format, segment_offset,
             max_valid, page_count) = struct.unpack_from("<IHHQIH", d, s)
            starts = struct.unpack_from("<%dH" % page_count, d, s + 22)
            seg = self.segs[i]
            for pi, first in enumerate(starts):
                if first == 0xFFFF:
                    continue
                addr = seg["vmaddr"] + pi * page_size + first + self.slide
                while True:
                    raw = self.u64(addr)
                    is_bind = (raw >> 63) & 1
                    nxt = (raw >> 51) & 0xFFF
                    if is_bind:
                        ordinal = raw & 0xFFFFFF
                        self.put64(addr, stubs[ordinal])
                        nbind += 1
                    else:
                        # 64_offset: the offset is counted from the FIRST mapped
                        # segment (__TEXT), not from __PAGEZERO at address zero
                        target = raw & 0xFFFFFFFFF
                        self.put64(addr, BASE + target)
                        nreb += 1
                    if nxt == 0:
                        break
                    addr += nxt * 4
        self.imports = names
        return nreb, nbind


FAST_DATA_MASK = 0x00007FFFFFFFFFF8


def classes(im):
    """The list of Objective-C classes: their names lie behind two pointers."""
    key = ("__DATA_CONST", "__objc_classlist")
    if key not in im.sects:
        key = ("__DATA", "__objc_classlist")
    addr, size = im.sects[key]
    addr += im.slide
    out = []
    for i in range(size // 8):
        cls = im.u64(addr + i * 8)
        if not (im.lo + im.slide <= cls < im.hi + im.slide):
            out.append("<not in the image: %016x>" % cls)
            continue
        data = im.u64(cls + 0x20) & FAST_DATA_MASK     # class_rw_t / class_ro_t
        if not (im.lo + im.slide <= data < im.hi + im.slide):
            out.append("<data outside the image>")
            continue
        name_ptr = im.u64(data + 0x18)                 # class_ro_t.name
        if not (im.lo + im.slide <= name_ptr < im.hi + im.slide):
            out.append("<name outside the image>")
            continue
        out.append(im.cstr(name_ptr))
    return out


if __name__ == "__main__":
    im = Image(BIN)
    print("image %012x..%012x, mapped at %012x" % (im.lo, im.hi, BASE))
    print("entry point: offset %x" % (im.entryoff or 0))
    nreb, nbind = im.apply_fixups()
    print("fixups applied: own %d, foreign %d, names %d" % (nreb, nbind, len(im.imports)))
    print("\n=== Objective-C classes in the image ===")
    cl = classes(im)
    bad = [c for c in cl if c.startswith("<")]
    print("total %d, of them unreadable %d" % (len(cl), len(bad)))
    for c in cl:
        print("   ", c)
