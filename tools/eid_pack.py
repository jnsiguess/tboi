# -*- coding: utf-8 -*-
"""External Item Descriptions: the JSON database into a form the port can read.

The upstream database is 3.3 MB of JSON across twenty languages. Parsing that
on a phone at startup would cost both time and memory for no reason: nothing in
it ever changes at runtime. So it is turned into a flat file once, here, and
the port reads an entry with two lookups and no parsing at all.

    magic   "EIDB"           4
    version u32              1
    langs   u32              how many languages follow
    strings u32              offset of the string pool
    [languages]
        code    char[8]      "ru", "en_us", ... zero padded
        offset  u32          where this language's sections start
    [per language]
        sections u32         how many sections
        [sections]
            kind    u32      EID_COLLECTIBLE, EID_TRINKET, ...
            count   u32      entries, sorted by id
            offset  u32      where the entries start
        [entries]  (sorted by id, so a lookup is a binary search)
            id      u32
            name    u32      offset into the string pool
            text    u32      offset into the string pool
    [string pool]            NUL-terminated UTF-8, shared between languages

Strings are pooled and de-duplicated: the twenty languages repeat a great deal
of punctuation and short markers, and the pool halves the file.
"""
import io
import json
import os
import struct
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

KINDS = {
    'collectibles': 1,
    'trinkets': 2,
    'cards': 3,
    'pills': 4,
    'horsepills': 5,
    'dice': 6,
    'sacrifice': 7,
}


def build(src, dst):
    data = json.load(io.open(src, encoding='utf-8'))
    langs = data['languages']

    pool = bytearray()
    where = {}

    def put(s):
        if s is None:
            s = ''
        if s in where:
            return where[s]
        off = len(pool)
        pool.extend(s.encode('utf-8'))
        pool.append(0)
        where[s] = off
        return off

    put('')                                  # offset zero is the empty string

    bodies = []                              # one per language, already packed
    names = sorted(langs)
    for code in names:
        sections = []
        for kind_name in sorted(langs[code]):
            kind = KINDS.get(kind_name)
            if kind is None:
                continue
            entries = []
            for key, value in langs[code][kind_name].items():
                try:
                    ident = int(key)
                except ValueError:
                    continue
                if isinstance(value, dict):
                    name = value.get('name') or ''
                    text = value.get('description') or ''
                else:
                    name, text = '', str(value)
                entries.append((ident, put(name), put(text)))
            entries.sort()
            sections.append((kind, entries))
        bodies.append(sections)

    # the header, then each language's table of sections, then the entries
    head = struct.pack('<4sIII', b'EIDB', 1, len(names), 0)
    lang_table_size = len(names) * (8 + 4)
    cursor = len(head) + lang_table_size

    lang_offsets = []
    blocks = []
    for sections in bodies:
        lang_offsets.append(cursor)
        block = bytearray()
        block += struct.pack('<I', len(sections))
        entries_at = cursor + 4 + len(sections) * 12
        rows = bytearray()
        for kind, entries in sections:
            block += struct.pack('<III', kind, len(entries), entries_at + len(rows))
            for ident, name, text in entries:
                rows += struct.pack('<III', ident, name, text)
        block += rows
        blocks.append(bytes(block))
        cursor += len(block)

    strings_at = cursor
    out = bytearray()
    out += struct.pack('<4sIII', b'EIDB', 1, len(names), strings_at)
    for code, off in zip(names, lang_offsets):
        out += code.encode('ascii')[:8].ljust(8, b'\0')
        out += struct.pack('<I', off)
    for block in blocks:
        out += block
    out += pool

    io.open(dst, 'wb').write(bytes(out))
    print('%s: %d languages, %d strings, %.2f MB (from %.2f MB of JSON)'
          % (dst, len(names), len(where), len(out) / 1048576.0,
             len(io.open(src, 'rb').read()) / 1048576.0))


if __name__ == '__main__':
    src = sys.argv[1] if len(sys.argv) > 1 else 'descriptions.json'
    dst = sys.argv[2] if len(sys.argv) > 2 else os.path.join(ROOT, 'port', 'app', 'src', 'main', 'assets', 'eid.bin')
    build(src, dst)
