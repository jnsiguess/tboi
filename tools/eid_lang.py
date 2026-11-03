# -*- coding: utf-8 -*-
"""What the mod's language packs are missing, and what we translate ourselves.

The mod's conditional lines - "Overridden by {1}", the Car Battery and BFFS!
tables, Tarot Cloth's - are translated language by language, and two of the
game's own languages are largely empty: Russian has 69 of 498 entries and
German 128. The mod does not fall back to English for these on purpose, so on
a Russian or German game those lines simply never appear.

This tool has two jobs:

    py -3 tools/eid_lang.py todo ru      what is missing, as a file to fill in
    py -3 tools/eid_lang.py count        how every language stands

The translations themselves live beside it, one file per language:

    tools/eid_lang/ru.json    { "table|key": value, ... }

`value` is a string, or a list for the mod's replace/find-and-replace shapes -
the same shape the mod's own table has. tools/eid_more.py reads these files
when it packs eid.bin, and they sit on top of the mod's own files, so an
update of the mod does not lose them.

Entries with no WORDS in them - Car Battery turning "40" into "80", BFFS!
turning "75" into "150" - are not here at all: eid_more.py takes those from
English for every language by itself. They are data, not translation.
"""
import io
import json
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from eid_more import loc_tables_of, source_of, LOC_TABLES, FALLBACK
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

MINE = os.path.join(HERE, 'eid_lang')
MOD = os.path.join(os.path.dirname(HERE), 'eid-mod')   # the mod's own text, kept in the tree

# the languages the game itself has
GAME_LANGUAGES = ('en_us', 'de', 'spa', 'ja_jp', 'ko_kr', 'ru', 'zh_cn')


def plain(value):
    """The value as a plain Python thing: a string, or a list of them."""
    if isinstance(value, str):
        return value
    return [v for _, v in value]


def wordless(value):
    """No words at all - only numbers, markers and punctuation.

    Such an entry is the same in every language, so it can be taken from
    English without anyone translating anything.
    """
    for v in ([value] if isinstance(value, str) else plain(value)):
        if isinstance(v, (int, float)):
            continue
        bare = re.sub(r'\{\{[^}]*\}\}', '', str(v))
        if re.search(r'[A-Za-z]', bare):
            return False
    return True


def ours(code):
    """Our own translations for one language, or nothing."""
    path = os.path.join(MINE, code + '.json')
    if not os.path.exists(path):
        return {}
    return json.load(io.open(path, encoding='utf-8'))


def missing(mod_dir, code):
    """{"table|key": English value} for what this language has not got."""
    english = loc_tables_of(source_of(mod_dir, FALLBACK))
    theirs = loc_tables_of(source_of(mod_dir, code))
    mine = ours(code)
    out = {}
    for name in LOC_TABLES:
        for key, value in english.get(name, {}).items():
            if key in theirs.get(name, {}):
                continue
            if wordless(value):
                continue                    # eid_more.py copies these itself
            whole = name + '|' + key
            if whole in mine:
                continue
            out[whole] = plain(value)
    return out


def main(what, mod_dir, code):
    if what == 'count':
        english = loc_tables_of(source_of(mod_dir, FALLBACK))
        total = sum(len(english.get(n, {})) for n in LOC_TABLES)
        print('%-8s %6s %6s %6s' % ('язык', 'есть', 'даром', 'надо'))
        for one in GAME_LANGUAGES:
            theirs = loc_tables_of(source_of(mod_dir, one))
            have = sum(len(theirs.get(n, {})) for n in LOC_TABLES)
            free = sum(1 for n in LOC_TABLES for k, v in english.get(n, {}).items()
                       if k not in theirs.get(n, {}) and wordless(v))
            print('%-8s %6d %6d %6d' % (one, have + len(ours(one)), free,
                                        len(missing(mod_dir, one))))
        print('всего в английском: %d' % total)
        return
    if what == 'todo-desc':
        need = descriptions_todo(os.path.join(ROOT, 'port', 'app', 'src', 'main', 'assets', 'eid.bin'), code)
        path = os.path.join(MINE, 'todo_%s-desc.json' % code)
        if not os.path.isdir(MINE):
            os.makedirs(MINE)
        io.open(path, 'w', encoding='utf-8').write(
            json.dumps(need, ensure_ascii=False, indent=1, sort_keys=True))
        print('%s: %d описаний, %d знаков' %
              (path, len(need), sum(len(v['name']) + len(v['text']) for v in need.values())))
        return
    if what == 'todo':
        need = missing(mod_dir, code)
        path = os.path.join(MINE, 'todo_' + code + '.json')
        if not os.path.isdir(MINE):
            os.makedirs(MINE)
        io.open(path, 'w', encoding='utf-8').write(
            json.dumps(need, ensure_ascii=False, indent=1, sort_keys=True))
        print('%s: %d записей, %d знаков' %
              (path, len(need), sum(len(json.dumps(v, ensure_ascii=False)) for v in need.values())))
        return
    raise SystemExit(__doc__)


# ------------------------------------------------ the descriptions themselves
#
# The German pack of the mod is short of more than conditions: 101 trinkets, 39
# cards and 41 pills have no German text at all, and 101 collectibles carry the
# English text word for word. Those are ordinary descriptions rather than the
# tables above, so they live in their own file, keyed by "<kind>|<id>":
#
#     tools/eid_lang/de-desc.json   { "2|1": {"name": "...", "text": "..."} }
#
# tools/eid_more.py lays them over the packed sections when it builds eid.bin.

DESC_KINDS = {1: 'collectibles', 2: 'trinkets', 3: 'cards', 4: 'pills', 5: 'horsepills'}


def descriptions_todo(binary, code):
    """{"kind|id": {"name","text"}} for what this language has not got in its own words."""
    from eid_more import read_bin
    blocks, pool, _ = read_bin(binary)

    def text_at(off):
        end = pool.find(b'\0', off)
        return pool[off:end].decode('utf-8')

    tables = {}
    for name, sections in blocks:
        tables[name] = dict((kind, dict((i, (text_at(n), text_at(t))) for i, n, t in rows))
                            for kind, rows in sections)
    mine = ours(code + '-desc')
    out = {}
    for kind in DESC_KINDS:
        english = tables[FALLBACK].get(kind, {})
        theirs = tables.get(code, {}).get(kind, {})
        for ident, (name, text) in english.items():
            got = theirs.get(ident)
            # missing, or standing there in English with English words in it
            if got is not None and got[1] != text:
                continue
            if got is not None and not re.search(r'[A-Za-z]{3}', re.sub(r'\{\{[^}]*\}\}', '', text)):
                continue
            key = '%d|%d' % (kind, ident)
            if key not in mine:
                out[key] = {'name': name, 'text': text}
    return out


if __name__ == '__main__':
    args = sys.argv[1:]
    what = args[0] if args else 'count'
    mod = args[1] if len(args) > 1 else MOD
    lang = args[2] if len(args) > 2 else 'ru'
    if what in ('todo',) and len(args) > 1 and not os.path.isdir(mod):
        mod, lang = MOD, args[1]            # the mod folder may be left out
    main(what, mod, lang)
