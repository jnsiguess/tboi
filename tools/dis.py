# -*- coding: utf-8 -*-
"""Disassemble a function of the image with hints: strings from adrp+add and call targets.

The idea behind the hints is simple: a bare arm64 listing reads badly, while
the whole meaning of the game's code is in which names it assembles and whom
it calls. So an adrp/add pair is expanded into the string right away, if there
is text at that address.
"""
import os, struct, sys
from capstone import *

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
BIN = os.path.join(ROOT, 'work', 'isaac.arm64')
d = open(BIN, "rb").read()
md = Cs(CS_ARCH_ARM64, CS_MODE_ARM)
md.detail = True


def text_at(off, n=80):
    if off < 0 or off >= len(d):
        return None
    s = d[off:off + n]
    end = s.find(b"\0")
    if end <= 0:
        return None
    s = s[:end]
    if not all(32 <= c < 127 for c in s):
        return None
    return s.decode("ascii")


def show(start, count=200):
    pages = {}
    for i in md.disasm(d[start:start + count * 4], start):
        note = ""
        if i.mnemonic == "adrp":
            r, imm = i.op_str.split(", ")
            pages[r] = int(imm.lstrip("#"), 0)
            note = "  ; page %x" % pages[r]
        elif i.mnemonic in ("add", "ldr") and "#" in i.op_str:
            parts = [p.strip() for p in i.op_str.replace("[", "").replace("]", "").split(",")]
            if len(parts) >= 2 and parts[1] in pages:
                try:
                    off = pages[parts[1]] + (int(parts[2].lstrip("#"), 0) if len(parts) > 2 else 0)
                except ValueError:
                    off = None
                if off is not None:
                    t = text_at(off)
                    note = "  ; %x" % off + ('  "%s"' % t if t else "")
        elif i.mnemonic in ("bl", "b"):
            note = "  ; -> %s" % i.op_str
        print("%08x  %-8s %s%s" % (i.address, i.mnemonic, i.op_str, note))


if __name__ == "__main__":
    show(int(sys.argv[1], 16), int(sys.argv[2]) if len(sys.argv) > 2 else 200)
