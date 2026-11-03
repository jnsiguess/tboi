# -*- coding: utf-8 -*-
"""Which characters a language needs, and whether the font has them.

This is the measurement that found the Korean hole: eid_default (PixelMplus10)
carries Latin, Cyrillic, kana and six thousand ideographs, and not one Hangul
syllable, so Korean descriptions came out as icons with the words missing. The
answer was to carry a Korean face beside it.

The owner now reports the same shape of fault in Chinese - text cut short, and
item names vanishing altogether - so the same question is asked of every
language we ship: take the characters that language actually uses out of
eid.bin, and look each one up in the font.

    py -3 tools/eid_glyphs.py [lang ...]     (default: every language in eid.bin)
"""
import io
import os
import struct
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
ASSETS = os.path.join(ROOT, 'port', 'app', 'src', 'main', 'assets')
BIN = os.path.join(ASSETS, 'eid.bin')
FONTS = {
    'eid_default': os.path.join(ASSETS, 'eid_default.fnt'),
    'eid_korean': os.path.join(ASSETS, 'eid_korean_yoonwebdotum10_shadow.fnt'),
}


def font_chars(path):
    """The character ids in a BMFont binary file (block 4)."""
    data = io.open(path, 'rb').read()
    if data[:3] != b'BMF':
        raise ValueError('%s is not a BMFont binary' % path)
    at = 4
    ids = set()
    while at + 5 <= len(data):
        kind = data[at]
        size = struct.unpack_from('<i', data, at + 1)[0]
        body = at + 5
        if kind == 4:
            for i in range(body, body + size, 20):
                ids.add(struct.unpack_from('<I', data, i)[0])
        at = body + size
    return ids


def read_bin(path):
    """Every string in eid.bin, grouped by language code."""
    data = io.open(path, 'rb').read()
    magic, version, langs, pool_at = struct.unpack_from('<4sIII', data, 0)
    if magic != b'EIDB':
        raise ValueError('%s is not an eid.bin' % path)
    out = {}
    at = 16
    heads = []
    for i in range(langs):
        code = data[at:at + 8].rstrip(b'\0').decode('ascii')
        off = struct.unpack_from('<I', data, at + 8)[0]
        heads.append((code, off))
        at += 12

    def text_at(off):
        end = data.index(b'\0', pool_at + off)
        return data[pool_at + off:end].decode('utf-8', 'replace')

    for code, off in heads:
        seen = []
        sections = struct.unpack_from('<I', data, off)[0]
        p = off + 4
        for s in range(sections):
            kind, count, where = struct.unpack_from('<III', data, p)
            p += 12
            for e in range(count):
                _id, name, text = struct.unpack_from('<III', data, where + e * 12)
                seen.append(text_at(name))
                seen.append(text_at(text))
        out[code] = seen
    return out


def main():
    want = [a for a in sys.argv[1:] if not a.startswith('-')]
    have = dict((n, font_chars(p)) for n, p in FONTS.items() if os.path.isfile(p))
    for n, ids in sorted(have.items()):
        print('%-12s %d glyphs' % (n, len(ids)))
    print('')

    langs = read_bin(BIN)
    print('%-8s %8s %8s %8s   %s' % ('lang', 'strings', 'chars', 'missing', 'worst entries'))
    for code in sorted(langs):
        if want and code not in want:
            continue
        strings = langs[code]
        chars = set()
        for s in strings:
            chars.update(s)
        gone = sorted(c for c in chars if ord(c) not in have.get('eid_default', set()))
        # how many whole strings lose a character
        hurt = sum(1 for s in strings if any(ord(c) not in have.get('eid_default', set())
                                             for c in s))
        print('%-8s %8d %8d %8d   %d strings touched' %
              (code, len(strings), len(chars), len(gone), hurt))
        if gone:
            # printed as code points: the Windows console cannot show most of
            # what is missing, and a row of question marks names nothing
            show = ' '.join('U+%04X' % ord(c) for c in gone[:24])
            print('         missing: %s%s' % (show, ' ...' if len(gone) > 24 else ''))


if __name__ == '__main__':
    main()
