# -*- coding: utf-8 -*-
"""What each description language lacks, cut into pieces for translators.

    py -3 tools/eid_todo.py [--size 120]

Every row English has and a language does not is written out, a piece of at
most SIZE rows at a time, to work/eid-todo/<code>-<n>.json. Each row carries the
English name and text, and - where a close language has the row - that
language's words as a hint (Ukrainian and Bulgarian get the Russian, Portuguese
the Brazilian, Dutch the German): a translator who can see how a neighbouring
community already names Mom's Knife gets the name right more often.

Only the rows that are WORDS are asked for. The golden trinkets' number tables,
the plural rules and the Consolation Prize's word lists are data, not text, and
are left out; so is anything a language already has.

tools/eid_additions.py reads the answers back (<code>-<n>.out.json) and puts
them into eid.bin.
"""
import io
import json
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from eid_more import read_bin

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
BIN = os.path.join(ROOT, 'port', 'app', 'src', 'main', 'assets', 'eid.bin')
OUT = os.path.join(ROOT, 'work', 'eid-todo')

KIND_NAMES = {1: 'item', 2: 'trinket', 3: 'card', 4: 'pill', 5: 'horse pill',
              8: 'birthright', 10: 'golden trinket', 12: 'condition phrase',
              13: 'heart wording', 15: 'Book of Virtues wisp'}
WORDS = tuple(KIND_NAMES)

HINT = {'uk_ua': 'ru', 'bul': 'ru', 'pt': 'pt_br', 'pt_br': 'pt', 'nl_nl': 'de'}

LANGUAGE = {'bul': 'Bulgarian', 'cs_cz': 'Czech', 'de': 'German', 'el_gr': 'Greek',
            'fr': 'French', 'it': 'Italian', 'ja_jp': 'Japanese', 'ko_kr': 'Korean',
            'nl_nl': 'Dutch', 'pl': 'Polish', 'pt': 'Portuguese (Portugal)',
            'pt_br': 'Portuguese (Brazil)', 'ro_ro': 'Romanian', 'ru': 'Russian',
            'spa': 'Spanish', 'tr_tr': 'Turkish', 'uk_ua': 'Ukrainian',
            'vi': 'Vietnamese', 'zh_cn': 'Simplified Chinese'}


def rows_of(sections, pool):
    def text(off):
        return bytes(pool[off:pool.index(b'\0', off)]).decode('utf-8')
    out = {}
    for kind, rows in sections:
        if kind not in WORDS:
            continue
        for ident, name, body in rows:
            out[(kind, ident)] = (text(name), text(body))
    return out


def main():
    size = 120
    for i, a in enumerate(sys.argv):
        if a == '--size' and i + 1 < len(sys.argv):
            size = int(sys.argv[i + 1])
    blocks, pool, _ = read_bin(BIN)
    table = {code: rows_of(sections, pool) for code, sections in blocks}
    english = table['en_us']
    os.makedirs(OUT, exist_ok=True)
    for f in os.listdir(OUT):
        if f.endswith('.json') and not f.endswith('.out.json'):
            os.remove(os.path.join(OUT, f))
    summary = []
    for code in sorted(table):
        if code == 'en_us':
            continue
        mine = table[code]
        hint = table.get(HINT.get(code, ''), {})
        todo = []
        for key in sorted(english):
            if key in mine:
                continue
            kind, ident = key
            name, body = english[key]
            row = dict(kind=kind, what=KIND_NAMES[kind], id=ident, en_name=name, en_text=body)
            if key in hint:
                row['hint_language'] = LANGUAGE[HINT[code]]
                row['hint_name'], row['hint_text'] = hint[key]
            todo.append(row)
        if not todo:
            continue
        # how this language already words the mod, so the new rows match it:
        # items, trinkets and cards it has, English beside them
        examples = []
        for key in sorted(mine):
            if key[0] in (1, 2, 3) and key in english and len(examples) < 25:
                examples.append(dict(what=KIND_NAMES[key[0]], en_name=english[key][0],
                                     name=mine[key][0], en_text=english[key][1],
                                     text=mine[key][1]))
        pieces = (len(todo) + size - 1) // size
        for n in range(pieces):
            part = todo[n * size:(n + 1) * size]
            path = os.path.join(OUT, '%s-%d.json' % (code, n + 1))
            io.open(path, 'w', encoding='utf-8').write(json.dumps(
                dict(language=LANGUAGE[code], code=code, examples=examples, rows=part),
                ensure_ascii=False, indent=1))
        summary.append((code, len(todo), pieces))
    total = 0
    for code, n, pieces in summary:
        print('%-6s %4d rows in %d piece(s)' % (code, n, pieces))
        total += pieces
    print('%d pieces in %s' % (total, OUT))


if __name__ == '__main__':
    main()
