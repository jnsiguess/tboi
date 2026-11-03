# -*- coding: utf-8 -*-
"""RESTART TO APPLY on the MODS page after ADD MOD, the owner's road (hotfix 1.75; usermods.c ADD MOD OWES ITS
RESTART; the phone's order of calls: tools/umdesk/test_restart175.py).

The owner on 1.74: "the restart button when adding mods is gone COMPLETELY - I suppose the list grows and it gets
lost somewhere". Every language set, the two phone canvases (480 x 270, FULLSCREEN on 585 x 270), lists of 2, 20, 40
and 60 mods, two adds - three new mods, and an update of a mod that is OFF (1.74 owed nothing for it) - each put in
while the page is open, as the installer's thread does:

  1. the row is in the page's model, last, and the paper is laid out for it;
  2. a finger dragging the list up as far as it goes brings it whole onto the glass, drawn (its word, Extra 10);
  3. the pad reaches it (up from the top goes round to it) and its confirm restarts the game (action 7);
  4. a tap on its word restarts the game (the first puts the cursor there, as on every row).

    bash tools/umdesk/build.sh && bash tools/tweaksdesk/build.sh && py -3 tools/tweaksdesk/test_restart175.py
"""
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile

sys.stdout.reconfigure(encoding='utf-8', errors='replace')
HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
DESK = os.path.join(HERE, 'tweaksdesk.exe')
TOP = os.path.join(tempfile.gettempdir(), 'tweaksdesk-restart175')
R_RESTART, MX_RESTART = 28, 10
failures = []

layout = open(os.path.join(ROOT, 'port', 'app', 'src', 'main', 'cpp', 'tweaks_layout.h'), encoding='utf-8').read()
MX_W = [[int(v) for v in row.split(',')] for row in
        re.findall(r'\{([\d, ]+)\}', layout.split('static const short MX_W[TW_SETS][MX_COUNT] = {')[1].split('};')[0])]


def check(cond, what, detail=''):
    if not cond:
        print('  FAIL  ' + what + ('' if detail == '' else '\n        ' + str(detail)[:900]))
        failures.append(what)
    return cond


def mod(path, ident, name, version='1'):
    os.makedirs(os.path.join(path, 'resources', 'gfx'), exist_ok=True)
    open(os.path.join(path, 'mod.json'), 'w', encoding='utf-8').write(json.dumps({'id': ident, 'name': name,
                                                                                  'version': version}))
    open(os.path.join(path, 'resources', 'gfx', 'x.txt'), 'w').write('x')


def lay_out(tag, n, kind):
    """n listed mods, the first one OFF; the stage holds what ADD MOD brings: three new mods, or the off one's update"""
    top = os.path.join(TOP, tag)
    shutil.rmtree(top, ignore_errors=True)
    cfg, stage = os.path.join(top, 'cfg'), os.path.join(top, 'stage')
    lines = []
    for i in range(n):
        f = '%03d-M%d' % (i + 1, i)
        mod(os.path.join(cfg, 'mods', f), 'm%d' % i, 'Mod %d' % i)
        lines.append(('off ' if i == 0 else 'on ') + f)
    os.makedirs(os.path.join(cfg, 'mods'), exist_ok=True)
    open(os.path.join(cfg, 'mods', 'mods.txt'), 'w').write('# order\n' + ''.join(l + '\n' for l in lines))
    moves = []
    if kind == 'new':
        for k in range(3):
            f = '%03d-N%d' % (n + 1 + k, k)
            mod(os.path.join(stage, f), 'n%d' % k, 'New %d' % k)
            moves += ['put', os.path.join(stage, f), f, '-']
    else:
        f = '%03d-M0' % (n + 1)
        mod(os.path.join(stage, f), 'm0', 'Mod 0', '2')
        moves += ['put', os.path.join(stage, f), f, '001-M0']
    return cfg, moves


def run(cfg, moves, lang, w, h):
    p = subprocess.run([DESK, cfg, str(w), str(h), str(lang)] + moves, capture_output=True)
    return p.stdout.decode('utf-8', 'replace')


def sections(text):
    out, cur = [], []
    for l in text.splitlines():
        cur.append(l)
        if l.startswith('STATE '):
            out.append(cur)
            cur = []
    return out


def state(sec):
    l = [x for x in sec if x.startswith('STATE ')][-1]
    return dict(kv.split('=', 1) for kv in l[6:].split() if '=' in kv)


def rows(sec):
    got = []
    for l in sec:
        m = re.match(r'ROW (\d+) id=(\d+) box=([-\d.]+),([-\d.]+),([-\d.]+),([-\d.]+) name_x=([-\d.]+)', l)
        if m:
            got.append({'i': int(m.group(1)), 'id': int(m.group(2)), 'box': [float(m.group(k)) for k in range(3, 7)],
                        'name_x': float(m.group(7))})
    return got


total = passed = 0
for kind in ('new', 'update-off'):
    for lang in range(7):
        for (w, h) in ((480, 270), (585, 270)):
            for n in (2, 20, 40, 60):
                what = '%s, set %d, %dx%d, %d mods' % (kind, lang, w, h, n)
                total += 1
                before = len(failures)
                tag = '%s-%d-%d-%d' % (kind, lang, w, n)
                cfg, adds = lay_out(tag, n, kind)
                base = ['mods', 'state'] + adds + ['state']
                # 1-2: the model, the paper, and a finger dragging the list up as far as it goes
                t = run(cfg, base + ['drag', '240', '230', '-200'] * (n // 4 + 3) + ['draw'], lang, w, h)
                sec = sections(t)
                puts = len(adds) // 4                       # each `put` is a move of its own: a STATE line each
                s0, s1, s2 = state(sec[1]), state(sec[2 + puts]), state(sec[-1])
                check(s0['restart'] == '0', '%s: nothing owed before the add' % what, s0)
                check(s1['restart'] == '1', '%s: RESTART TO APPLY comes with the add' % what, s1)
                check(int(s1['paper_h']) > int(s0['paper_h']), '%s: the paper laid out again' % what, (s0, s1))
                rr = [r for r in rows(sec[-1]) if r['id'] == R_RESTART]
                if check(len(rr) == 1 and rr[0]['i'] == int(s2['rows']) - 1, '%s: its row, the last' % what,
                         rows(sec[-1])[-2:]):
                    x0, y0, x1, y1 = rr[0]['box']
                    check(abs(float(s2['scrolled']) - float(s2['room'])) < 0.5,
                          '%s: the finger took the list to its end' % what, s2)
                    check(y0 >= 0 and y1 <= h, '%s: whole on the glass at the end (%.0f..%.0f of %d)' % (what, y0, y1, h))
                    drawn = [l for l in sec[-1] if l.startswith('OP render Extra %d ' % MX_RESTART)]
                    check(len(drawn) == 1, '%s: its word drawn (Extra %d)' % (what, MX_RESTART))
                    word = MX_W[lang][MX_RESTART]
                    tx, ty = rr[0]['name_x'] + word / 2, (y0 + y1) / 2
                    t = run(lay_out(tag, n, kind)[0], base + ['drag', '240', '230', '-200'] * (n // 4 + 3)
                            + ['tap', '%.1f' % tx, '%.1f' % ty, 'tap', '%.1f' % tx, '%.1f' % ty], lang, w, h)
                    # as on every row: a tap on a row the cursor is not on puts it there, the next one acts
                    check('ACTION 7' in t.splitlines(), '%s: a tap on its word restarts the game' % what, t[-300:])
                # 3: the pad
                t = run(lay_out(tag, n, kind)[0], base + ['pad', 'up', 'state', 'pad', 'confirm'], lang, w, h)
                st = state(sections(t)[-2])
                check(int(st['cursor_id']) == R_RESTART, '%s: the pad reaches it' % what, st)
                check('ACTION 7' in t.splitlines(), '%s: its confirm restarts the game' % what, t[-300:])
                if len(failures) == before:
                    passed += 1
print('ADD MOD with the page open: %d of %d cases whole' % (passed, total))
print('\n%d failures' % len(failures))
sys.exit(1 if failures else 0)
