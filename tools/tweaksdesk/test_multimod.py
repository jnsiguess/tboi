# -*- coding: utf-8 -*-
"""ADD MOD while the MODS page is open, with no restart after it (hotfix 1.73), on the real tweaks.c + usermods.c
(tools/tweaksdesk/build.sh). The installer works on its own thread; the page must show what it did by itself.

    bash tools/tweaksdesk/build.sh && py -3 tools/tweaksdesk/test_multimod.py

  1. a new mod put in while the page is open: a row more, RESTART TO APPLY under the list, the paper laid out again
     (taller), the rows above where they were, the cursor where it was;
  2. the new mod pale (its name drawn at the grey's alpha), the note under the list in the page's language (en ru de
     es; jp kr zh read en, as all the page's notes), every letter of it in the small cut of the menu font;
  3. an update of a mod of this run: the same number of rows, the new copy in the old one's place, pale;
  4. a new mod switched off on the page: no longer pale, and the note goes when no pale name is left;
  5. its own page: the title pale, the note under its rows.
"""
import os
import re
import shutil
import struct
import subprocess
import sys
import tempfile
import json

sys.stdout.reconfigure(encoding='utf-8', errors='replace')

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
DESK = os.path.join(HERE, 'tweaksdesk.exe')
TOP = os.path.join(tempfile.gettempdir(), 'tweaksdesk-multimod')
GREY = '%.3f' % (140.0 / 255.0)
failures = []

NOTE = {0: 'Pale names: just added, they work after a restart',
        1: 'Бледные названия: только что добавлены, заработают после перезапуска',
        2: 'Blasse Namen: gerade hinzugefügt, wirken nach einem Neustart',
        3: 'Nombres pálidos: recién añadidos, funcionan tras reiniciar'}


def check(cond, what, detail=''):
    print(('  ok    ' if cond else '  FAIL  ') + what + ('' if cond or detail == '' else '\n        ' + str(detail)[:900]))
    if not cond:
        failures.append(what)


def chars(name):
    b = open(os.path.join(ROOT, 'port', 'app', 'src', 'main', 'assets', name + '.fnt'), 'rb').read()
    at, have = 4, set()
    while at + 5 <= len(b):
        kind, size = b[at], struct.unpack_from('<I', b, at + 1)[0]
        if kind == 4:
            have = {struct.unpack_from('<I', b, i)[0] for i in range(at + 5, at + 5 + size, 20)}
        at += 5 + size
    return have


def mod(path, ident, name):
    os.makedirs(os.path.join(path, 'resources', 'gfx'), exist_ok=True)
    open(os.path.join(path, 'mod.json'), 'w', encoding='utf-8').write(json.dumps({'id': ident, 'name': name, 'version': '1'}))
    open(os.path.join(path, 'resources', 'gfx', 'x.txt'), 'w').write('x')


def lay_out(tag):
    top = os.path.join(TOP, tag)
    shutil.rmtree(top, ignore_errors=True)
    cfg, stage = os.path.join(top, 'cfg'), os.path.join(top, 'stage')
    mod(os.path.join(cfg, 'mods', '001-A'), 'a', 'Mod A')
    mod(os.path.join(cfg, 'mods', '002-B'), 'b', 'Mod B')
    open(os.path.join(cfg, 'mods', 'mods.txt'), 'w').write('# order\non 001-A\non 002-B\n')
    mod(os.path.join(stage, '003-C'), 'c', 'Mod C')
    mod(os.path.join(stage, '004-A'), 'a', 'Mod A')
    return cfg, stage


def run(cfg, moves, lang=0, h=900):
    # a tall canvas: the players' mods are at the list's foot, drawn only when on the glass
    p = subprocess.run([DESK, cfg, '480', str(h), str(lang)] + moves, capture_output=True)
    return p.stdout.decode('utf-8', 'replace')


def states(text):
    return [dict(kv.split('=', 1) for kv in l[6:].split() if '=' in kv) for l in text.splitlines() if l.startswith('STATE ')]


def section(text, k):
    """the lines of the k-th move's output (each move ends at its STATE line)"""
    out, cur = [], []
    for l in text.splitlines():
        cur.append(l)
        if l.startswith('STATE '):
            out.append(cur)
            cur = []
    return out[k]


have10 = chars('port_menufont10')
for lang in (0, 1, 2, 3, 4):
    print('\n== language set %d' % lang)
    cfg, stage = lay_out('set%d' % lang)
    moves = ['mods', 'draw', 'notes',
             'put', os.path.join(stage, '003-C'), '003-C', '-', 'draw', 'notes',
             'put', os.path.join(stage, '004-A'), '004-A', '001-A', 'draw', 'notes']
    t = run(cfg, moves, lang)
    s = states(t)
    if len(s) < 9:
        check(False, 'the stand ran every move', t[-800:])
        continue
    before, after_c, after_a = s[1], s[4], s[7]
    note = NOTE[lang if lang <= 3 else 0]

    print('1. a new mod while the page is open')
    rb, rc = int(before['rows']), int(after_c['rows'])
    check(before['restart'] == '0' and after_c['restart'] == '1', 'RESTART TO APPLY comes', (before['restart'], after_c['restart']))
    check(rc == rb + 2, 'a row for the mod and the restart row: %d -> %d' % (rb, rc))
    check(int(after_c['paper_h']) > int(before['paper_h']), 'the paper laid out again, taller: %s -> %s'
          % (before['paper_h'], after_c['paper_h']))
    check(after_c['cursor'] == before['cursor'], 'the cursor where it was', (before['cursor'], after_c['cursor']))
    rows_b = [l for l in section(t, 1) if l.startswith('ROW ')]
    rows_c = [l for l in section(t, 4) if l.startswith('ROW ')]
    own = [l for l in rows_b if int(re.search(r'id=(\d+)', l).group(1)) < 100][:10]
    check(all(r in rows_c for r in rows_b[:-2]), 'the rows above the players\' mods where they were')

    print('2. pale, and the note says why')
    names_c = [l for l in section(t, 5) if l.startswith('NAME ')]
    check(names_c[:3] == ['NAME 0 pale Mod C', 'NAME 1 ink Mod A', 'NAME 2 ink Mod B'], 'C pale, A and B as they were', names_c)
    notes_c = [l.split(' ', 2)[2] for l in section(t, 5) if l.startswith('NOTE ')]
    joined = ' '.join(notes_c)
    check(joined == note, 'the note under the list: ' + joined)
    draw = section(t, 4)
    at = [i for i, l in enumerate(draw) if l.startswith('OP write') and l.endswith(' Mod C')]
    check(at and draw[at[0] - 1] == 'ALPHA ' + GREY, 'the name drawn at the grey\'s alpha', draw[at[0] - 1] if at else '-')
    plain = [i for i, l in enumerate(draw) if l.startswith('OP write') and l.endswith(' Mod B')]
    check(plain and not draw[plain[0] - 1].startswith('ALPHA'), 'B at full ink')
    missing = sorted({c for c in note if ord(c) not in have10 and c != ' '})
    check(not missing, 'every letter of the note in port_menufont10', missing)

    print('3. an update of a mod of this run')
    check(int(after_a['rows']) == rc, 'as many rows: %s' % after_a['rows'])
    names_a = [l for l in section(t, 8) if l.startswith('NAME ')]
    check(names_a[:3] == ['NAME 0 pale Mod C', 'NAME 1 pale Mod A', 'NAME 2 ink Mod B'], 'the new copy of A in its place, pale',
          names_a)

cfg, stage = lay_out('off')
print('\n== 4. switched off on the page: no longer pale; 5. its own page')
t = run(cfg, ['mods', 'put', os.path.join(stage, '003-C'), '003-C', '-', 'notes', 'user', '0', 'draw', 'notes'])
page = section(t, 5)                    # moves: mods, put, notes, user, draw, notes
check(any(l.startswith('NOTE ') and NOTE[0].split(':')[0] in l for l in page), 'its page: the note under its rows')
dr = section(t, 4)
title = [i for i, l in enumerate(dr) if l.startswith('OP write') and l.endswith(' Mod C')]
check(title and dr[title[0] - 1] == 'ALPHA ' + GREY, 'its title pale')
t = run(cfg, ['mods', 'notes'])
check(any(l == 'NAME 0 ink Mod C' for l in t.splitlines()), 'after a restart (a new start of the stand): ink, not pale')
cfg, stage = lay_out('toggle')
t = run(cfg, ['mods', 'put', os.path.join(stage, '003-C'), '003-C', '-', 'user', '0', 'pad', 'confirm', 'notes'])
names = [l for l in t.splitlines() if l.startswith('NAME 0')]
notes = [l for l in t.splitlines() if l.startswith('NOTE ')]
on_line = [l for l in t.splitlines() if l.startswith('STATE ')]
check(names and names[-1] == 'NAME 0 ink Mod C', 'switched off on its page (ENABLED): ink again', names)
check(not any(NOTE[0].split(':')[0] in l for l in notes[-3:]), 'and no pale note left', notes[-3:])

print('\n%d failures' % len(failures))
sys.exit(1 if failures else 0)
