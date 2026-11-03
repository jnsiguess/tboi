# -*- coding: utf-8 -*-
"""RESTART TO APPLY on the MODS page after ADD MOD (hotfix 1.74), on the real tweaks.c + usermods.c
(tools/tweaksdesk/build.sh; usermods.c's own side: tools/umdesk/test_restart174.py).

The players on 1.73: "RESTART disappeared after installing mods". Every language set, three canvases (480 x 270,
FULLSCREEN on 585 x 270, a 4:3 tablet's 480 x 360), a short list, a long one and a full one (64):

  1. the mods put in while the page is open: the row is there, last, and the paper laid out for it;
  2. the cursor reaches it (up from the top goes round to it) and the page scrolls it fully onto the glass;
  3. it is drawn - its word, the Extra frame of the language set - clear of BACK and SELECT's corners;
  4. its confirm and a tap on its word restart the game (action 7);
  5. a folder put into mods/ by hand while the page is open: the row comes by itself, the paper grows with it,
     and it is reachable as well;
  6. a mod switched on and off again on the list: the row comes and goes, the paper with it, the cursor stays on
     the glass.

    bash tools/umdesk/build.sh && bash tools/tweaksdesk/build.sh && py -3 tools/tweaksdesk/test_restart174.py
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
TOP = os.path.join(tempfile.gettempdir(), 'tweaksdesk-restart174')
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


def mod(path, ident, name):
    os.makedirs(os.path.join(path, 'resources', 'gfx'), exist_ok=True)
    open(os.path.join(path, 'mod.json'), 'w', encoding='utf-8').write(json.dumps({'id': ident, 'name': name, 'version': '1'}))
    open(os.path.join(path, 'resources', 'gfx', 'x.txt'), 'w').write('x')


def lay_out(tag, n, new=3):
    top = os.path.join(TOP, tag)
    shutil.rmtree(top, ignore_errors=True)
    cfg, stage = os.path.join(top, 'cfg'), os.path.join(top, 'stage')
    lines = []
    for i in range(n):
        f = '%03d-M%d' % (i + 1, i)
        mod(os.path.join(cfg, 'mods', f), 'm%d' % i, 'Mod %d' % i)
        lines.append('on ' + f)
    os.makedirs(os.path.join(cfg, 'mods'), exist_ok=True)
    open(os.path.join(cfg, 'mods', 'mods.txt'), 'w').write('# order\n' + ''.join(l + '\n' for l in lines))
    staged = []
    for k in range(new):
        f = '%03d-N%d' % (n + 1 + k, k)
        mod(os.path.join(stage, f), 'n%d' % k, 'New %d' % k)
        staged.append((os.path.join(stage, f), f))
    return cfg, staged


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


def reach_and_tap(fresh, base_moves, lang, w, h, what):
    """up from the top goes round to the last row: it must be RESTART TO APPLY, whole on the glass, drawn, clear of
    the corners; its confirm and a tap on its word restart the game. Each run of the stand is a start of the game:
    `fresh()` lays the folders out again for it"""
    cfg = fresh()
    t = run(cfg, base_moves + ['pad', 'up', 'draw'], lang, w, h)
    sec = sections(t)
    st = state(sec[-1])
    ok = check(int(st['cursor_id']) == R_RESTART, '%s: the cursor reaches RESTART TO APPLY' % what, st)
    rr = [r for r in rows(sec[-1]) if r['id'] == R_RESTART]
    if not check(len(rr) == 1, '%s: its row laid out' % what, rows(sec[-1])[-2:]):
        return
    r = rr[0]
    x0, y0, x1, y1 = r['box']
    ok &= check(y0 >= 0 and y1 <= h, '%s: whole on the glass (%.0f..%.0f of %d)' % (what, y0, y1, h), r)
    drawn = [l for l in sec[-1] if l.startswith('OP render Extra %d ' % MX_RESTART)]
    ok &= check(len(drawn) == 1, '%s: its word drawn (Extra %d)' % (what, MX_RESTART), [l for l in sec[-1] if 'Extra' in l])
    word = MX_W[lang][MX_RESTART]
    ok &= check(r['name_x'] >= 95 and r['name_x'] + word <= w - 93,
                '%s: clear of BACK (x < 95) and SELECT (x > W - 93): %.0f..%.0f' % (what, r['name_x'], r['name_x'] + word))
    t = run(fresh(), base_moves + ['pad', 'up', 'pad', 'confirm'], lang, w, h)
    ok &= check('ACTION 7' in t.splitlines(), '%s: its confirm restarts the game' % what, t[-300:])
    tx, ty = r['name_x'] + word / 2, (y0 + y1) / 2
    t = run(fresh(), base_moves + ['pad', 'up', 'tap', '%.1f' % tx, '%.1f' % ty], lang, w, h)
    ok &= check('ACTION 7' in t.splitlines(), '%s: a tap on its word restarts the game' % what, t[-300:])
    return ok


CANVASES = [(480, 270), (585, 270), (480, 360)]
LISTS = [2, 30, 61]
total = passed = 0
for lang in range(7):
    for (w, h) in CANVASES:
        for n in LISTS:
            what = 'set %d, %dx%d, %d mods' % (lang, w, h, n)
            total += 1
            before = len(failures)
            cfg, staged = lay_out('s%d-%d-%d-%d' % (lang, w, h, n), n)
            moves = ['mods', 'state']
            for d, f in staged:
                moves += ['put', d, f, '-']
            moves += ['state']
            t = run(cfg, moves, lang, w, h)
            sec = sections(t)
            s0, s1 = state(sec[1]), state(sec[-1])
            check(s0['restart'] == '0' and s1['restart'] == '1', '%s: RESTART TO APPLY comes with the mods' % what,
                  (s0['restart'], s1['restart']))
            check(int(s1['rows']) == int(s0['rows']) + len(staged) + 1, '%s: the mods\' rows and the row' % what)
            check(int(s1['paper_h']) > int(s0['paper_h']), '%s: the paper laid out again' % what)
            reach_and_tap(lambda: lay_out('s%d-%d-%d-%d' % (lang, w, h, n), n)[0], moves[:], lang, w, h, what)
            if len(failures) == before:
                passed += 1
print('1-4. ADD MOD while the page is open: %d of %d cases whole' % (passed, total))

print('5. a folder put in by hand while the page is open')
for lang in range(7):
    for (w, h) in CANVASES:
        what = 'by hand, set %d, %dx%d' % (lang, w, h)
        cfg, staged = lay_out('h%d-%d-%d' % (lang, w, h), 30, new=1)
        moves = ['mods', 'state', 'disk', 'drop', staged[0][0], staged[0][1], 'state', 'disk']
        t = run(cfg, moves, lang, w, h)
        sec = sections(t)
        s0, s1 = state(sec[2]), state(sec[-1])
        check('== disk changed=0' in t and '== disk changed=1' in t, '%s: owed once the folder is there' % what, t[-400:])
        check(s0['restart'] == '0' and s1['restart'] == '1', '%s: the row comes by itself' % what, (s0, s1))
        check(int(s1['paper_h']) > int(s0['paper_h']), '%s: the paper grows with it (%s -> %s)'
              % (what, s0['paper_h'], s1['paper_h']))
        check(float(s1['room']) > float(s0['room']), '%s: and the scroll\'s room' % what)
        reach_and_tap(lambda: lay_out('h%d-%d-%d' % (lang, w, h), 30, new=1)[0], moves, lang, w, h, what)
print('   %d cases' % (7 * len(CANVASES)))

print('6. switched on and off again on the list')
for lang in range(7):
    cfg, _ = lay_out('t%d' % lang, 30, new=0)
    # the first player's mod is row MD_OWN; reach it by pad from the top: find its index from a draw
    t = run(cfg, ['mods', 'draw'], lang, 480, 270)
    first = [r for r in rows(sections(t)[-1]) if r['id'] == 1000][0]['i']
    moves = ['mods'] + ['pad', 'down'] * (first - 1) + ['state', 'pad', 'right', 'state', 'pad', 'right', 'state']
    t = run(cfg, moves, lang, 480, 270)
    sec = sections(t)
    a, b, c = state(sec[-5]), state(sec[-3]), state(sec[-1])
    check(int(a['cursor_id']) == 1000, 'set %d: on the first player\'s mod' % lang, a)
    check((a['restart'], b['restart'], c['restart']) == ('0', '1', '0'), 'set %d: the row comes and goes' % lang,
          (a['restart'], b['restart'], c['restart']))
    check(int(b['paper_h']) > int(a['paper_h']) and c['paper_h'] == a['paper_h'], 'set %d: the paper with it' % lang,
          (a['paper_h'], b['paper_h'], c['paper_h']))
    check(int(c['cursor']) < int(c['rows']), 'set %d: the cursor on a row' % lang, c)

print('\n%d failures' % len(failures))
sys.exit(1 if failures else 0)
