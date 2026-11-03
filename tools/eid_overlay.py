# -*- coding: utf-8 -*-
"""Lay a few sections of one eid.bin over another, and nothing else.

The working database is port/app/src/main/assets/eid.bin itself: it carries
the translations made on 16.09.2026 that work/eid-full.bin does not, so it is
never rebuilt from eid-full.bin again (that lost six languages' descriptions
once, 23.09.2026). New tables are built into a copy with tools/eid_more.py and
then laid over the working file with this tool:

    py -3 tools/eid_overlay.py <working eid.bin> <eid.bin with the new tables> <out>

What is taken from the second file:
    section 18 (GlitchedItemText)          whole
    section 9  (phrases)                   rows 15 and up (the crafting words)
    section 17 (VoidNames / prize words)   rows 0..5 (the stat names)
Everything else is the working file's, byte for byte in meaning.
"""
import io
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from eid_more import read_bin, write_bin  # noqa: E402

WHOLE = (18,)
ROWS_FROM = {9: lambda i: i >= 15, 17: lambda i: 0 <= i <= 5}


def main(base_path, new_path, out_path):
    base, base_pool, version = read_bin(base_path)
    new, new_pool, _ = read_bin(new_path)

    pool = bytearray()
    known = {}

    def put(b):
        if b in known:
            return known[b]
        off = len(pool)
        pool.extend(b)
        pool.append(0)
        known[b] = off
        return off

    def text(p, off):
        end = p.find(b'\0', off)
        return bytes(p[off:end])

    def moved(p, rows):
        return [(i, put(text(p, n)), put(text(p, t))) for i, n, t in rows]

    new_by = dict((code, dict(secs)) for code, secs in new)
    blocks = []
    for code, sections in base:
        theirs = new_by.get(code, {})
        out = []
        for kind, rows in sections:
            if kind in WHOLE:
                continue
            rows = moved(base_pool, rows)
            if kind in ROWS_FROM and kind in theirs:
                keep = ROWS_FROM[kind]
                ids = set(i for i, _, _ in theirs[kind] if keep(i))
                rows = [r for r in rows if r[0] not in ids]
                rows += moved(new_pool, [r for r in theirs[kind] if keep(r[0])])
                rows.sort()
            out.append((kind, rows))
        for kind in WHOLE:
            if kind in theirs:
                out.append((kind, moved(new_pool, theirs[kind])))
        # a phrases section the base did not have at all
        for kind in ROWS_FROM:
            if kind in theirs and not any(k == kind for k, _ in out):
                out.append((kind, moved(new_pool, [r for r in theirs[kind] if ROWS_FROM[kind](r[0])])))
        blocks.append((code, out))
    size = write_bin(out_path, blocks, pool, version)
    print('%s: %d languages, %.2f MB' % (out_path, len(blocks), size / 1e6))


if __name__ == '__main__':
    main(*sys.argv[1:4])
