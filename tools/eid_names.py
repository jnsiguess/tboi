# -*- coding: utf-8 -*-
"""Item names in the game's own words, for the languages the mod has not got.

The mod carries its own name tables (descriptions/names/<code>.lua), and for
German they are half empty: 92 of 189 trinkets, 60 of 97 cards, 10 of 51 pills.
The GAME knows all of them - it has to, it prints them itself - so they are
taken from its own string table rather than invented here.

    py -3 tools/eid_names.py [work/app/repentance-resources/data]

stringtable.sta is XML: one <key name="THE_SAD_ONION_NAME"> per string with one
<string> per language, in the order the file's own header gives - English,
Japanese, Korean, Chinese, Russian, German, Spanish, French. items.xml and
pocketitems.xml say which key belongs to which item.

The result is written as tools/eid_lang/names.json:

    { "de": { "2|1": "Verschluckter Penny", ... }, "ru": {...} }

tools/eid_more.py puts a name from here over the packed one ONLY where the
language is still showing the English name - what the mod itself translated is
left alone.
"""
import io
import json
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DATA = os.path.join(ROOT, 'work', 'app', 'repentance-resources', 'data')
OUT = os.path.join(ROOT, 'tools', 'eid_lang', 'names.json')

# which <string> of a key belongs to which of the database's languages
COLUMN = {'en_us': 0, 'ja_jp': 1, 'ko_kr': 2, 'zh_cn': 3, 'ru': 4, 'de': 5, 'spa': 6, 'fr': 7}

KIND_COLLECTIBLE, KIND_TRINKET, KIND_CARD, KIND_PILL = 1, 2, 3, 4


def string_table(path):
    """{key: [string per language]} out of stringtable.sta."""
    text = io.open(path, encoding='utf-8', errors='replace').read()
    out = {}
    for block in re.finditer(r'<key name="([^"]+)">(.*?)</key>', text, re.S):
        out[block.group(1)] = [unescape(s) for s in
                               re.findall(r'<string>(.*?)</string>', block.group(2), re.S)]
    return out


def unescape(text):
    for was, now in (('&amp;', '&'), ('&lt;', '<'), ('&gt;', '>'),
                     ('&quot;', '"'), ('&apos;', "'")):
        text = text.replace(was, now)
    return text


def items_of(path, tag, kind):
    """[(kind, id, name key)] for one of the game's item files."""
    text = io.open(path, encoding='utf-8', errors='replace').read()
    found = []
    for one in re.findall(r'<' + tag + r'\b[^>]*>', text):
        ident = re.search(r'\bid="(\d+)"', one)
        name = re.search(r'\bname="#([^"]+)"', one)
        if ident and name:
            found.append((kind, int(ident.group(1)), name.group(1)))
    return found


def main(data, out):
    table = string_table(os.path.join(data, 'stringtable.sta'))
    items = (items_of(os.path.join(data, 'items.xml'), 'passive', KIND_COLLECTIBLE)
             + items_of(os.path.join(data, 'items.xml'), 'active', KIND_COLLECTIBLE)
             + items_of(os.path.join(data, 'items.xml'), 'familiar', KIND_COLLECTIBLE)
             + items_of(os.path.join(data, 'items.xml'), 'trinket', KIND_TRINKET)
             + items_of(os.path.join(data, 'pocketitems.xml'), 'card', KIND_CARD)
             + items_of(os.path.join(data, 'pocketitems.xml'), 'rune', KIND_CARD)
             + items_of(os.path.join(data, 'pocketitems.xml'), 'pilleffect', KIND_PILL))
    names = {}
    for code, column in COLUMN.items():
        mine = {}
        for kind, ident, key in items:
            row = table.get(key)
            if not row or column >= len(row):
                continue
            said = row[column].strip()
            if said and said != row[0].strip():
                # the pill effects the database counts from one
                if kind == KIND_PILL:
                    ident += 1
                mine['%d|%d' % (kind, ident)] = said
        names[code] = mine
        print('%-6s %d названий' % (code, len(mine)))
    if not os.path.isdir(os.path.dirname(out)):
        os.makedirs(os.path.dirname(out))
    io.open(out, 'w', encoding='utf-8').write(
        json.dumps(names, ensure_ascii=False, indent=1, sort_keys=True))
    print(out)


if __name__ == '__main__':
    main(sys.argv[1] if len(sys.argv) > 1 else DATA,
         sys.argv[2] if len(sys.argv) > 2 else OUT)
