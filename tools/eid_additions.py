# -*- coding: utf-8 -*-
"""Translated rows back into eid.bin.

    py -3 tools/eid_additions.py [eid.bin]

Reads every work/eid-todo/<code>-<n>.out.json written for the pieces
tools/eid_todo.py cut, checks each row, and puts the rows that pass into that
language's sections, kept sorted by id (the port looks rows up by binary
search, so the order is not optional).

A row is checked against its English source before it may go in, because the
panel reads markup out of these strings and a translator's slip there breaks
the drawing, not just the wording:

    - every {{marker}} of the English must be there, the same number of times
      (icons, colours, the stat arrows);
    - every {n} placeholder, the same;
    - the same number of '#' - each starts a line of the description;
    - it must not be empty where the English is not.

Rows that fail are left out and listed, so they can be asked for again. A row
the language already has is never overwritten. Run after eid_fixes.py, and
running it twice adds nothing the second time.
"""
import io
import json
import os
import re
import sys
from collections import Counter

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from eid_more import read_bin, write_bin

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
TODO = os.path.join(ROOT, 'work', 'eid-todo')

MARK = re.compile(r'\{\{[^{}]*\}\}')
SLOT = re.compile(r'\{\d+\}')


def signature(s):
    return (Counter(MARK.findall(s)), Counter(SLOT.findall(s)), s.count('#'))


def main(target):
    blocks, pool, version = read_bin(target)
    by_code = dict(blocks)

    def text(off):
        return bytes(pool[off:pool.index(b'\0', off)]).decode('utf-8')

    english = {}
    for kind, rows in by_code['en_us']:
        for ident, name, body in rows:
            english[(kind, ident)] = (text(name), text(body))

    strings = {}

    def put(s):
        if s == '':
            return 0
        if s not in strings:
            strings[s] = len(pool)
            pool.extend(s.encode('utf-8') + b'\0')
        return strings[s]

    added, refused = Counter(), []
    for f in sorted(os.listdir(TODO)):
        if not f.endswith('.out.json'):
            continue
        # condition phrases carry the unit separator (0x1f), and some answers write it raw
        answer = json.load(io.open(os.path.join(TODO, f), encoding='utf-8'), strict=False)
        code = answer['code']
        sections = by_code[code]
        have = {(kind, ident) for kind, rows in sections for ident, _, _ in rows}
        for row in answer['rows']:
            key = (int(row['kind']), int(row['id']))
            if key in have or key not in english:
                continue
            en_name, en_text = english[key]
            name, body = row.get('name', ''), row.get('text', '')
            why = None
            if signature(body) != signature(en_text):
                why = 'markup of the text differs from the English'
            elif signature(name) != signature(en_name):
                why = 'markup of the name differs from the English'
            elif en_text and not body.strip():
                why = 'the text is empty'
            if why:
                refused.append((f, key, why))
                continue
            for s, (kind, rows) in enumerate(sections):
                if kind == key[0]:
                    rows.append((key[1], put(name), put(body)))
                    rows.sort(key=lambda r: r[0])
                    break
            else:
                sections.append((key[0], [(key[1], put(name), put(body))]))
                sections.sort(key=lambda s: s[0])
            have.add(key)
            added[code] += 1

    write_bin(target, blocks, pool, version)
    for code in sorted(added):
        print('%-6s %d rows added' % (code, added[code]))
    print('%d refused' % len(refused))
    for f, key, why in refused[:40]:
        print('   %s kind %d id %d: %s' % (f, key[0], key[1], why))


if __name__ == '__main__':
    main(sys.argv[1] if len(sys.argv) > 1 else os.path.join(ROOT, 'port', 'app', 'src', 'main',
                                                             'assets', 'eid.bin'))
