# -*- coding: utf-8 -*-
"""Throw out of eid.bin the languages the game cannot be set to.

    py -3 tools/eid_trim.py [eid.bin]

The mod ships twenty languages; this build of the game offers eight, and the
panel follows the game's own setting (`db_code_for` in eid.c reads the field at
`0x564f8` and turns it into one of these codes). The other twelve can never be
chosen, so they are weight in the pack and nothing else.

The string pool is written afresh, keeping only what the languages that stay
point at, so the file loses their strings as well as their tables.

The whole twenty-language database is kept as `work/eid-full.bin`: the pack was
made once by eid_pack.py and the JSON it was made from is gone, so a language
thrown out here cannot be packed again from the mod alone.
"""
import io
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from eid_more import read_bin, write_bin

# what db_code_for() in port/app/src/main/cpp/eid.c can answer, English first
KEEP = ('en_us', 'ja_jp', 'fr', 'spa', 'de', 'ru', 'ko_kr', 'zh_cn')

FULL = 'work/eid-full.bin'


def main(target):
    # RETIRED, 16.09.2026. The owner asked for every language the mod carries
    # back, with eidlang in port-touch.ini (and the Tweaks page) able to pick
    # any of them, not only the eight the game's own setting can reach. Running
    # this would quietly throw twelve of them out again.
    raise SystemExit('eid_trim.py is retired: all twenty languages stay (see ПЕРЕДАЧА)')
    blocks, pool, version = read_bin(target)
    have = [code for code, _ in blocks]
    for code in KEEP:
        if code not in have:
            raise SystemExit('%s: no %s in the pack' % (target, code))
    if not [code for code in have if code not in KEEP]:
        print('%s: nothing to throw out, %d languages' % (target, len(have)))
        return

    keep_full = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))),
                             *FULL.split('/'))
    if not os.path.exists(keep_full):
        io.open(keep_full, 'wb').write(io.open(target, 'rb').read())
        print('%s: the whole pack kept here' % FULL)

    fresh = bytearray(1)                # the empty string, which rows point at by nought
    known = {b'': 0}

    def put(at):
        """One string, moved into the new pool; the same string moves once."""
        end = pool.find(b'\0', at)
        word = bytes(pool[at:end if end >= 0 else len(pool)])
        if word in known:
            return known[word]
        off = len(fresh)
        fresh.extend(word)
        fresh.append(0)
        known[word] = off
        return off

    put(pool.find(b'\0'))                       # keep the empty string at nought
    out = []
    for code, sections in blocks:
        if code not in KEEP:
            continue
        moved = [(kind, [(ident, put(name), put(text)) for ident, name, text in rows])
                 for kind, rows in sections]
        out.append((code, moved))
    was = os.path.getsize(target)
    now = write_bin(target, out, fresh, version)
    print('%s: %d languages of %d, %.2f MB of %.2f MB'
          % (target, len(out), len(blocks), now / 1048576.0, was / 1048576.0))


if __name__ == '__main__':
    main(sys.argv[1] if len(sys.argv) > 1 else 'port/app/src/main/assets/eid.bin')
