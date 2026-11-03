# -*- coding: utf-8 -*-
"""The bosses the spawner can put in a room, out of the game's entities2.xml.

The owner asked for a way to meet a boss for testing the boss bars
(18.09.2026). A boss is an entity with boss="1" and a portrait; its name in
the file is a string-table key (#LARRY_JR), turned into words here - English
only, which is what the wiki uses too.

    py -3 tools/spawner_bosses.py
"""
import os
import re

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
XML = os.path.join(ROOT, 'work', 'app', 'repentance-resources', 'data', 'entities2.xml')
OUT = os.path.join(ROOT, 'port', 'app', 'src', 'main', 'cpp', 'spawner_bosses.h')

SMALL = {'OF', 'THE', 'AND'}
KEEP = {'II', 'III', 'IV', 'XL', 'CHAD', 'C.H.A.D.'}


def words(key):
    out = []
    for i, w in enumerate(key.lstrip('#').split('_')):
        if not w:
            continue
        if w in KEEP:
            out.append(w)
        elif i and w in SMALL:
            out.append(w.lower())
        else:
            out.append(w[0] + w[1:].lower())
    return ' '.join(out).replace('Jr', 'Jr.')


def main():
    text = open(XML, encoding='utf-8').read()
    seen = set()
    rows = []
    for m in re.finditer(r'<entity ([^>]*)>', text):
        a = dict(re.findall(r'(\w+)="([^"]*)"', m.group(1)))
        if a.get('boss') != '1' or 'portrait' not in a or not a.get('name', '').startswith('#'):
            continue
        key = (int(a['id']), int(a.get('variant', 0)))
        if key in seen:
            continue
        seen.add(key)
        rows.append((key[0], key[1], words(a['name'])))
    with open(OUT, 'w', encoding='utf-8', newline='\n') as f:
        f.write('/* Written by tools/spawner_bosses.py - do not edit by hand, run the tool. */\n')
        f.write('#ifndef SPAWNER_BOSSES_H\n#define SPAWNER_BOSSES_H\n\n')
        f.write('static const struct { unsigned short type, variant; const char *name; } SP_BOSS[%d] = {\n'
                % len(rows))
        for t, v, n in rows:
            f.write('    { %d, %d, "%s" },\n' % (t, v, n.replace('"', '\\"')))
        f.write('};\n\n#endif\n')
    print('%d bosses' % len(rows))


if __name__ == '__main__':
    main()
