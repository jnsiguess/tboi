# -*- coding: utf-8 -*-
"""THE MODS PAGE WITH 256 MODS (hotfix 1.75, MODS_MAX 64 -> 256; usermods.c's own side: tools/umdesk/test_list256.py).

Every language set, both phone canvases (480 x 270, FULLSCREEN on 585 x 270): 255 mods listed - long names, so each
is cut to the paper (tweaks.c LABELS) - and ADD MOD puts the 256th in while the page is open:

  1. all 256 rows and RESTART TO APPLY after them, the paper laid out for 257 rows (paper_h in a short: room to spare);
  2. the finger drags the list to its very end: the row whole on the glass, its word drawn;
  3. the names past the 64th are cut to the paper like the first ones (no line wider than the paper);
  4. the pad goes round from the top to RESTART TO APPLY, and its confirm restarts the game.

    bash tools/umdesk/build.sh && bash tools/tweaksdesk/build.sh && py -3 tools/tweaksdesk/test_list256.py
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
DESK = os.path.join(HERE, 'tweaksdesk.exe')
TOP = os.path.join(tempfile.gettempdir(), 'tweaksdesk-list256')
R_RESTART, MX_RESTART, R_USER = 28, 10, 1000
failures = []


def check(cond, what, detail=''):
    print(('  ok    ' if cond else '  FAIL  ') + what + ('' if cond or detail == '' else '\n        ' + str(detail)[:600]))
    if not cond:
        failures.append(what)
    return cond


def mod(path, ident, name):
    os.makedirs(os.path.join(path, 'resources', 'gfx'), exist_ok=True)
    open(os.path.join(path, 'mod.json'), 'w', encoding='utf-8').write(json.dumps({'id': ident, 'name': name, 'version': '1'}))
    open(os.path.join(path, 'resources', 'gfx', 'x.txt'), 'w').write('x')


def lay_out(tag):
    top = os.path.join(TOP, tag)
    if not os.path.isdir(os.path.join(top, 'cfg', 'mods', '255-M254')):
        shutil.rmtree(top, ignore_errors=True)
        for i in range(255):
            mod(os.path.join(top, 'cfg', 'mods', '%03d-M%d' % (i + 1, i)), 'm%d' % i,
                'Mod number %d - a long name of a mod, longer than any paper holds' % i)
        open(os.path.join(top, 'cfg', 'mods', 'mods.txt'), 'w').write(
            '# order\n' + ''.join('on %03d-M%d\n' % (i + 1, i) for i in range(255)))
    for leftover in ('256-N0',):
        shutil.rmtree(os.path.join(top, 'cfg', 'mods', leftover), ignore_errors=True)
    open(os.path.join(top, 'cfg', 'mods', 'mods.txt'), 'w').write(
        '# order\n' + ''.join('on %03d-M%d\n' % (i + 1, i) for i in range(255)))
    shutil.rmtree(os.path.join(top, 'stage'), ignore_errors=True)
    mod(os.path.join(top, 'stage', '256-N0'), 'n0', 'The 256th')
    return os.path.join(top, 'cfg'), ['put', os.path.join(top, 'stage', '256-N0'), '256-N0', '-']


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


for lang in range(7):
    for (w, h) in ((480, 270), (585, 270)):
        what = 'set %d, %dx%d' % (lang, w, h)
        cfg, add = lay_out('l%d' % lang)
        drags = ['drag', '240', '250', '-2000'] * 6
        t = run(cfg, ['mods', 'state'] + add + ['state'] + drags + ['draw'], lang, w, h)
        sec = sections(t)
        s0, s1, s2 = state(sec[1]), state(sec[3]), state(sec[-1])
        check(s0['restart'] == '0' and s1['restart'] == '1', '%s: RESTART TO APPLY comes with the 256th' % what, (s0, s1))
        check(int(s1['rows']) == int(s0['rows']) + 2, '%s: the 256th\'s row and the restart row: %s -> %s rows'
              % (what, s0['rows'], s1['rows']))
        check(32767 > int(s1['paper_h']) > int(s0['paper_h']), '%s: the paper laid out again (%s -> %s)'
              % (what, s0['paper_h'], s1['paper_h']))
        check(abs(float(s2['scrolled']) - float(s2['room'])) < 0.5, '%s: the finger takes the list to its end' % what, s2)
        rr = [r for r in rows(sec[-1]) if r['id'] == R_RESTART]
        if check(len(rr) == 1 and rr[0]['i'] == int(s2['rows']) - 1, '%s: RESTART TO APPLY is the last row' % what):
            x0, y0, x1, y1 = rr[0]['box']
            check(y0 >= 0 and y1 <= h, '%s: whole on the glass (%.0f..%.0f of %d)' % (what, y0, y1, h))
            check(any(l.startswith('OP render Extra %d ' % MX_RESTART) for l in sec[-1]), '%s: its word drawn' % what)
        # the names near the end, past the 64th: cut to the paper like the first ones
        paper_l = float(s2['paper_x']) + (w - 480) / 2.0
        paper_r = paper_l + float(s2['paper_w'])
        names = [l for l in sec[-1] if l.startswith('OP write 0 ') and 'Mod number' in l]
        wide = []
        for l in names:
            m = re.match(r'OP write 0 ([-\d.]+) ([-\d.]+) (\d+) (.*)', l)
            if m and float(m.group(1)) + float(m.group(3)) > paper_r + 0.5:
                wide.append(l)
        check(len(names) > 0 and not wide, '%s: %d names on the glass near the end, all cut to the paper'
              % (what, len(names)), wide[:2])
        check(any(l.endswith('…') for l in names), '%s: and cut with "…"' % what, names[:2])
        t = run(cfg, ['mods'] + add + ['pad', 'up', 'state', 'pad', 'confirm'], lang, w, h)
        st = state(sections(t)[-2])
        check(int(st['cursor_id']) == R_RESTART, '%s: the pad goes round to it' % what, st)
        check('ACTION 7' in t.splitlines(), '%s: its confirm restarts the game' % what)
print('\n%d failures' % len(failures))
sys.exit(1 if failures else 0)
