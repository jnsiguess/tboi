# -*- coding: utf-8 -*-
"""Desk cases for a player's mod's page on MODS with the mod's own settings (plan 4.10, part 2; the owner's order of
02.10.2026), on the real tweaks.c + usermods.c (tools/tweaksdesk/build.sh).

    bash tools/tweaksdesk/build.sh && py -3 tools/tweaksdesk/test_page.py

  1. the order: title and description, SETTINGS and a row per setting, then ENABLED, MOVE UP, MOVE DOWN, EXPORT,
     DELETE; RESTART TO APPLY under them while a change waits; a mod without settings keeps the page it had;
  2. a finger finds each row where it is drawn (16:9, 20:9 and a 4:3 tablet, scrolled too): a tap on a row's
     drawn label highlights that row, a tap on the heading does nothing;
  3. whole units: every picture and line of text stands on a whole unit;
  4. the behaviour of Tweaks: a tap on the highlighted switch flips it, on a choice steps it round, on a number
     does nothing; sideways (finger and pad) steps, a number stops at its ends; the value written at once;
  5. a row does not move when its value changes (laid out for the widest value);
  6. RESTART TO APPLY comes with a change and goes when the value is turned back; its tap restarts;
  7. a long page scrolls, and the pad keeps the highlighted row in view down to the last one;
  8. the MODS list and the mod's page are one width;
  9. the page's language: Russian labels in the Russian set, English in the Japanese one.
"""
import os
import re
import shutil
import subprocess
import sys
import tempfile
import json

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
SAMPLE = os.path.join(ROOT, 'tools', 'umdesk', 'sample-settings')
DESK = os.path.join(HERE, 'tweaksdesk.exe')
TOP = os.path.join(tempfile.gettempdir(), 'tweaksdesk')
failures = []

R_SHEAD, R_USET, R_U_ON, R_U_DEL, R_RESTART = 29, 800, 23, 27, 28
OWN = [23, 24, 25, 26, 27]


def check(cond, what, detail=''):
    print(('  ok    ' if cond else '  FAIL  ') + what + ('' if cond or not detail else '\n        ' + str(detail)[:900]))
    if not cond:
        failures.append(what)


def config(name, mods):
    cfg = os.path.join(TOP, name)
    shutil.rmtree(cfg, ignore_errors=True)
    os.makedirs(os.path.join(cfg, 'mods'))
    order = []
    for mid, src in mods:
        d = os.path.join(cfg, 'mods', mid)
        if isinstance(src, str):
            shutil.copytree(src, d)
        else:
            os.makedirs(d)
            open(os.path.join(d, 'mod.json'), 'w', encoding='utf-8').write(json.dumps(src, ensure_ascii=False))
            os.makedirs(os.path.join(d, 'resources'))
            open(os.path.join(d, 'resources', 'x.txt'), 'w').write('x')
        order.append(mid)
    open(os.path.join(cfg, 'mods', 'mods.txt'), 'w').write('# order\n' + ''.join('on %s\n' % m for m in order))
    return cfg


class Run(object):
    """one desk run: the blocks of output after each move"""

    def __init__(self, cfg, moves, w=480, h=270, lang=0):
        p = subprocess.run([DESK, cfg, str(w), str(h), str(lang)] + [str(m) for m in moves], capture_output=True)
        self.text = p.stdout.decode('utf-8', 'replace')
        self.blocks = []
        cur = None
        for line in self.text.splitlines():
            if line.startswith('== '):
                cur = {'move': line[3:], 'ops': [], 'rows': [], 'state': {}, 'lines': []}
                self.blocks.append(cur)
            elif cur is None:
                continue
            elif line.startswith('OP '):
                cur['ops'].append(line[3:])
            elif line.startswith('ROW '):
                m = re.match(r'ROW (\d+) id=(\d+) box=([-\d.]+),([-\d.]+),([-\d.]+),([-\d.]+) name_x=([-\d.]+) value_x=([-\d.]+)'
                             r' heading=(\d) uset=(-?\d+) kind=(-?\d+)', line)
                cur['rows'].append(dict(i=int(m.group(1)), id=int(m.group(2)), box=[float(m.group(k)) for k in range(3, 7)],
                                        name_x=float(m.group(7)), value_x=float(m.group(8)), heading=m.group(9) == '1',
                                        uset=int(m.group(10)), kind=int(m.group(11))))
            elif line.startswith('STATE '):
                cur['state'] = dict(kv.split('=', 1) for kv in line[6:].split() if '=' in kv)
            else:
                cur['lines'].append(line)

    def last(self, move=None):
        bl = [b for b in self.blocks if move is None or b['move'] == move]
        return bl[-1] if bl else {'ops': [], 'rows': [], 'state': {}, 'lines': []}

    def draws(self):
        return [b for b in self.blocks if b['move'] == 'draw']


def st(block, key, cast=int):
    v = block['state'].get(key)
    return cast(float(v)) if v is not None else None


def ini(cfg):
    p = os.path.join(cfg, 'mods', 'mod-settings.ini')
    return open(p, encoding='utf-8').read() if os.path.exists(p) else ''


def label_ops(block):
    """the lines of text a draw wrote: (font, x, y, width, text)"""
    out = []
    for op in block['ops']:
        m = re.match(r'write (\d) ([-\d.]+) ([-\d.]+) (\d+) (.*)', op)
        if m:
            out.append((int(m.group(1)), float(m.group(2)), float(m.group(3)), int(m.group(4)), m.group(5)))
    return out


def tap_point(row):
    """a finger on the row's label: its name's start, the middle of the row's step"""
    return row['name_x'] + 6, (row['box'][1] + row['box'][3]) / 2.0


os.makedirs(TOP, exist_ok=True)
NOSET = {'id': 'plain', 'name': 'Plain Mod', 'version': '1'}
LONG = {'id': 'long', 'name': 'Long Mod', 'version': '2', 'description': 'A mod with sixteen settings, for the scroll.',
        'settings': [{'key': 's%02d' % k, 'kind': ['switch', 'choice', 'number'][k % 3], 'label': 'Setting number %d' % k,
                      **({'default': False} if k % 3 == 0 else
                         {'values': [{'value': 'a', 'label': 'Alpha'}, {'value': 'b', 'label': 'Beta wide'}], 'default': 'a'}
                         if k % 3 == 1 else {'min': -5, 'max': 10, 'step': 5, 'default': 0})} for k in range(16)]}

print('1. the order of the page')
cfg = config('order', [('settings-sample', SAMPLE), ('plain', NOSET)])
r = Run(cfg, ['mods', 'draw', 'user', '0', 'draw', 'pad', 'back', 'user', '1', 'draw'])
d = r.draws()
ids = [x['id'] for x in d[1]['rows']]
check(ids == [R_SHEAD, R_USET, R_USET + 1, R_USET + 2] + OWN, 'with settings: SETTINGS, its three rows, then the mod\'s own', ids)
check(st([b for b in r.blocks if b['move'] == 'user'][0], 'cursor') == 1, 'the cursor starts on the first setting, not the heading', r.last('user')['state'])
ids = [x['id'] for x in d[2]['rows']]
check(ids == OWN, 'a mod with no settings keeps the page it had', ids)
heading = [o for o in d[1]['ops'] if o.startswith('render Extra 11 ')]
check(len(heading) == 1, 'SETTINGS drawn once, from the Extra frame 11 (MX_SETTINGS)', heading)
rows = {x['id']: x for x in d[1]['rows']}
check(rows[R_USET]['box'][1] - rows[R_SHEAD]['box'][3] == 0 and rows[R_U_ON]['box'][1] - rows[R_USET + 2]['box'][3] == 14,
      'the mod\'s own rows stand apart from its settings (MD_SECTION_GAP)', [x['box'] for x in d[1]['rows']])
desc = [l for l in label_ops(d[1]) if l[0] == 2]
check(desc and max(l[2] for l in desc) < rows[R_SHEAD]['box'][1], 'the title and the description above SETTINGS', desc)

print('2. a finger finds each row where it is drawn')
for (w, h) in ((480, 270), (600, 270), (480, 360)):
    for scroll in (0, 1):
        moves = ['mods', 'user', '0']
        if scroll:
            moves += ['drag', '240', '200', '-90']
        moves += ['draw']
        base = Run(cfg, moves, w=w, h=h)
        rows = base.last('draw')['rows']
        texts = label_ops(base.last('draw'))
        bad = []
        for row in rows:
            if row['heading'] or row['box'][3] < 0 or row['box'][1] > h:
                continue
            # the row's drawn label (a text) - its top + the capital's bottom (20) is the row's baseline, inside the box
            mine = [t for t in texts if t[0] == 0 and abs(t[1] - row['name_x']) < 0.01 and row['box'][1] <= t[2] + 20 <= row['box'][3]]
            x, y = tap_point(row)
            moves2 = moves[:-1] + ['tap', '%.1f' % x, '%.1f' % y]
            t = Run(cfg, moves2, w=w, h=h)
            got = st(t.last('tap'), 'cursor')
            if got != row['i']:
                bad.append((row['i'], row['id'], got, x, y))
            if row['uset'] >= 0 and not mine:
                bad.append(('no label drawn in its box', row))
        check(not bad, '%dx%d%s: a tap on each drawn row highlights that row' % (w, h, ' scrolled' if scroll else ''), bad)
    hd = [x for x in Run(cfg, ['mods', 'user', '0', 'draw'], w=w, h=h).last('draw')['rows'] if x['heading']][0]
    t = Run(cfg, ['mods', 'user', '0', 'tap', '%.1f' % (hd['name_x'] + 2), '%.1f' % ((hd['box'][1] + hd['box'][3]) / 2)], w=w, h=h)
    check(st(t.last('tap'), 'cursor') == 1, '%dx%d: a tap on SETTINGS does nothing' % (w, h), t.last('tap')['state'])

print('3. whole units')
# Unscrolled, everything stands on a whole unit. Scrolled, the camera stands wherever the finger left it (the game's
# own camera, 0x768ad0) and the whole page moves with it: every piece keeps the same fraction, so nothing moves
# against anything else - the pieces stand on whole units of the page.
def fractions(op):
    nums = [float(v) for v in re.findall(r'-?\d+\.\d+', op)[:2]]
    return tuple(round(v - (v // 1), 3) % 1.0 for v in nums)
for (w, h) in ((480, 270), (600, 270), (480, 360), (481, 271)):
    r = Run(cfg, ['mods', 'user', '0', 'draw', 'drag', '240', '200', '-37', 'draw'], w=w, h=h)
    d0, d1 = r.draws()
    frac = [op for op in d0['ops'] if any(f != 0.0 for f in fractions(op))]
    check(not frac, '%dx%d: every picture and line on a whole unit' % (w, h), frac[:5])
    page = [op for op in d1['ops'] if not op.startswith('render Wall')]
    fy = set(fractions(op)[1] for op in page if len(fractions(op)) > 1)
    fx = set(fractions(op)[0] for op in page)
    check(len(fy) == 1 and len(fx) == 1, '%dx%d scrolled: one fraction for every piece (nothing moves against another)'
          % (w, h), (fx, fy))

print('4. the behaviour of Tweaks')
cfg = config('behaviour', [('settings-sample', SAMPLE)])
r = Run(cfg, ['mods', 'user', '0', 'draw'])
rows = {x['id']: x for x in r.last('draw')['rows']}
sw, ch, nu = (tap_point(rows[R_USET + k]) for k in range(3))
r = Run(cfg, ['mods', 'user', '0', 'tap', sw[0], sw[1]])
check(ini(cfg) == '[settings-sample]\nblood-tears=on\n', 'a tap on the highlighted switch flips it, written at once', ini(cfg))
r = Run(cfg, ['mods', 'user', '0', 'tap', ch[0], ch[1], 'tap', ch[0], ch[1]])
check('style=retro' in ini(cfg), 'a choice: the first tap highlights, the second steps it on (clean -> retro)', ini(cfg))
r = Run(cfg, ['mods', 'user', '0', 'tap', nu[0], nu[1], 'tap', nu[0], nu[1]])
check('outline' not in ini(cfg), 'a number: a tap on the highlighted row leaves it as it is', ini(cfg))
r = Run(cfg, ['mods', 'user', '0', 'tap', nu[0], nu[1], 'swipe', nu[0], nu[1], 70])
check('outline=3' in ini(cfg), 'a swipe of 70 units: two steps (1 -> 3)', ini(cfg))
r = Run(cfg, ['mods', 'user', '0', 'pad', 'down', 'pad', 'down', 'pad', 'right', 'pad', 'right'])
check('outline=3' in ini(cfg), 'the pad\'s right stops a number at its end', ini(cfg))
check(sum(1 for l in r.text.splitlines() if l.startswith('SOUND 237')) == 2, 'a step plays the sliders\' sound (0xED)')
r = Run(cfg, ['mods', 'user', '0', 'pad', 'down', 'pad', 'down', 'pad', 'confirm'])
check('outline=3' in ini(cfg) and 'style=retro' in ini(cfg), 'the pad\'s confirm leaves a number as it is', ini(cfg))
r = Run(cfg, ['mods', 'user', '0', 'pad', 'down', 'pad', 'left'])
check('style=' not in ini(cfg), 'the pad\'s left steps a choice back (retro -> clean, the default: not written)', ini(cfg))
r = Run(cfg, ['mods', 'user', '0', 'pad', 'down', 'pad', 'left'])
check('style=classic' in ini(cfg), '... and round (clean -> classic)', ini(cfg))
r = Run(cfg, ['mods', 'user', '0', 'pad', 'up'])
check(st(r.last('pad'), 'cursor_id') == R_RESTART or st(r.last('pad'), 'cursor_id') == R_U_DEL,
      'the pad\'s up from the first setting passes the heading by (round to the last row)', r.last('pad')['state'])

print('5. a row does not move when its value changes')
cfg = config('steady', [('settings-sample', SAMPLE)])
r = Run(cfg, ['mods', 'user', '0', 'draw', 'pad', 'down', 'pad', 'right', 'draw', 'pad', 'right', 'draw',
              'pad', 'down', 'pad', 'right', 'pad', 'right', 'draw'])
xs = [[(x['id'], x['name_x'], x['value_x']) for x in b['rows'] if x['uset'] >= 0] for b in r.draws()]
check(all(x == xs[0] for x in xs), 'labels and values stay where they were', xs)
vals = [[t[4] for t in label_ops(b) if t[0] == 0] for b in r.draws()]
check('Retro' in vals[1] and 'Classic' in vals[2] and '3' in vals[3], 'the values shown change (Retro, Classic, 3)', vals)

print('6. RESTART TO APPLY')
cfg = config('restart', [('settings-sample', SAMPLE)])
r = Run(cfg, ['mods', 'user', '0', 'draw', 'pad', 'confirm', 'draw', 'pad', 'confirm', 'draw'])
d = r.draws()
check(d[0]['rows'][-1]['id'] == R_U_DEL and d[1]['rows'][-1]['id'] == R_RESTART and d[2]['rows'][-1]['id'] == R_U_DEL,
      'it comes with a change and goes when the value is back', [x['rows'][-1]['id'] for x in d])
check(d[1]['rows'][-1]['box'][1] - d[1]['rows'][-2]['box'][3] == 14, 'it stands apart from the rows')
r = Run(cfg, ['mods', 'user', '0', 'pad', 'confirm', 'pad', 'up', 'pad', 'confirm'])
check(any(l == 'ACTION 7' for l in r.text.splitlines()), 'its confirm restarts the game (action 7)', r.text[-400:])

print('7. a long page')
cfg = config('long', [('long', LONG), ('settings-sample', SAMPLE)])
moves = ['mods', 'user', '0', 'draw'] + ['pad', 'down'] * 22 + ['draw']
r = Run(cfg, moves)
d = r.draws()
check(st(d[0], 'room') > 0, 'it is taller than the screen: it scrolls', d[0]['state'])
vis = []
for b in r.blocks:
    if b['move'] == 'pad':
        pass
last = d[-1]
cur = st(last, 'cursor')
row = [x for x in last['rows'] if x['i'] == cur][0]
check(row['box'][1] >= 0 and row['box'][3] <= 270, 'the pad keeps the highlighted row in view (row %d)' % cur, row['box'])
check(st(last, 'scrolled') <= st(last, 'room') + 0.01, 'never past the foot', last['state'])
ok_all = True
r2 = Run(cfg, ['mods', 'user', '0'] + sum([['pad', 'down', 'draw'] for _ in range(23)], []))
for b in r2.draws():
    c = st(b, 'cursor')
    rr = [x for x in b['rows'] if x['i'] == c][0]
    if not (rr['box'][1] >= -0.01 and rr['box'][3] <= 270.01):
        ok_all = False
check(ok_all, 'every row the pad goes to is on the screen')
nums = [l for l in label_ops(d[0]) if l[4] in ('-5', '0', '5', '10')]
check(nums, 'numbers on a grid with negatives drawn', label_ops(d[0])[:8])

print('8. one width')
r = Run(cfg, ['mods', 'draw', 'user', '0', 'draw', 'pad', 'back', 'user', '1', 'draw'])
ws = [st(b, 'paper_w') for b in r.draws()]
check(len(set(ws)) == 1, 'the list and both pages: one width', ws)

print('9. the page\'s language')
cfg = config('lang', [('settings-sample', SAMPLE)])
r = Run(cfg, ['mods', 'user', '0', 'draw'], lang=1)
t = [l[4] for l in label_ops(r.last('draw'))]
check('Кровавые слёзы' in t and 'Чистая' in t and 'Пример настроек' in t, 'Russian set: Russian labels', t)
r = Run(cfg, ['mods', 'user', '0', 'draw'], lang=4)
t = [l[4] for l in label_ops(r.last('draw'))]
check('Blood tears' in t and 'Clean' in t and 'Settings Sample' in t, 'Japanese set: English (no CJK in the menu font)', t)

print('\n%d failures' % len(failures))
for f in failures:
    print('  - ' + f)
sys.exit(1 if failures else 0)
