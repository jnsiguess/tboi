# -*- coding: utf-8 -*-
"""Desk cases for long names on the MODS list (the owner, 06.10.2026: a long name blew the page past both edges of the
screen), on the real tweaks.c + usermods.c (tools/tweaksdesk/build.sh). THE WIDEST PAPER in tweaks.c.

    bash tools/tweaksdesk/build.sh && py -3 tools/tweaksdesk/test_longnames.py [old-desk.exe]

  1. the paper stays on the screen: never wider than the design width less a band of wall each side (424 on 16:9),
     on 16:9, 20:9, 32:9, a 4:3 tablet and an odd canvas, in en ru de es jp;
  2. each row of a player's mod - cursor, name, ON/OFF, arrow - inside the paper's margins;
  3. the label: the name whole when it fits; else the longest start of it (whole characters) the room takes, with
     the font's own "..." (U+2026) - measured here independently from port_menufont16bold.fnt;
  4. the name itself is whole: `notes` reads it back; a red name stays red;
  5. a finger: a tap on a cut row highlights it, a second toggles ON/OFF (mods.txt), a tap on its arrow opens its page;
  6. the mod's page: the whole name over it, on more lines when needed, each inside the paper; the rows under it;
     the list and the page one width;
  7. a setting label too long for the paper is cut the same way;
  8. clear of BACK and SELECT (the owner, 06.10.2026, FULLSCREEN off): the corner papers' ink and BACK's tap zone,
     measured here from BackSelectWidget.anm2 and its sheet, never over a row's label, ON/OFF or arrow - across, as
     every row passes the bottom while the list scrolls; on the list and on a mod's page, in both FULLSCREEN modes;
  9. nothing else moved: with names that fit, every op, row and state is what the build before the change drew
     (when an old desk is given: tools/tweaksdesk/desk.c over tweaks.c.before-longnames).
"""
import os
import re
import shutil
import struct
import subprocess
import sys
import tempfile
import json

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
SAMPLE = os.path.join(ROOT, 'tools', 'umdesk', 'sample-settings')
DESK = os.environ.get('TWEAKSDESK') or os.path.join(HERE, 'tweaksdesk.exe')     # another build to try the cases on
OLD = sys.argv[1] if len(sys.argv) > 1 else None
TOP = os.path.join(tempfile.gettempdir(), 'tweaksdesk-longnames')
failures = []

ELL = '…'
CURSOR_W, GAP, ARROW_W, MARGIN, BAND = 22, 8, 16, 20, 24
ONOFF = {0: (40, 39), 1: (40, 54), 2: (40, 39), 3: (40, 39), 4: (40, 39)}      # tweaks_layout.h MD_SET on_w, off_w


def check(cond, what, detail=''):
    print(('  ok    ' if cond else '  FAIL  ') + what + ('' if cond or not detail else '\n        ' + str(detail)[:900]))
    if not cond:
        failures.append(what)


# ---- the menu font, measured here on its own (the game's 0x8bdec8 sums the advances; the .fnt has no kerning)
def font(name):
    b = open(os.path.join(ROOT, 'port', 'app', 'src', 'main', 'assets', name + '.fnt'), 'rb').read()
    adv, kern, at = {}, 0, 4
    while at + 5 <= len(b):
        kind, size = b[at], struct.unpack_from('<I', b, at + 1)[0]
        body = at + 5
        if kind == 4:
            for i in range(body, body + size, 20):
                cid = struct.unpack_from('<I', b, i)[0]
                adv[cid] = struct.unpack_from('<h', b, i + 16)[0]
        if kind == 5:
            kern += size // 10
        at = body + size
    return adv, kern


ADV, KERN = font('port_menufont16bold')


# ---- the corner papers, measured from the game's own files (gfx/ui/BackSelectWidget.anm2 and its sheet)
def corner_ink():
    from PIL import Image
    res = os.path.join(ROOT, 'work', 'app', 'repentance-resources')
    anm = open(os.path.join(res, 'data', 'gfx', 'ui', 'backselectwidget.anm2'), encoding='utf-8').read()
    out = {}
    for lang in ('data', 'dlc3.de', 'dlc3.es', 'dlc3.jp', 'dlc3.kr', 'dlc3.ru', 'dlc3.zh'):
        png = os.path.join(res, lang, 'gfx', 'ui', 'backselectwidget.png')
        if not os.path.exists(png):
            continue
        a = Image.open(png).convert('RGBA').getchannel('A')
        for name in ('Back', 'Select'):
            fr = re.search(r'<Animation Name="%s".*?<LayerAnimation[^>]*>\s*<Frame ([^>]*)>' % name, anm, re.S).group(1)
            g = dict(re.findall(r'(\w+)="([^"]*)"', fr))
            cx, cy, fw, fh, px, py = (int(g[k]) for k in ('XCrop', 'YCrop', 'Width', 'Height', 'XPivot', 'YPivot'))
            bb = a.crop((cx, cy, cx + fw, cy + fh)).point(lambda v: 255 if v else 0).getbbox()
            r = (bb[0] - px, bb[1] - py, bb[2] - px, bb[3] - py)          # the ink about the sprite's place
            o = out.get(name)
            out[name] = r if not o else (min(o[0], r[0]), min(o[1], r[1]), max(o[2], r[2]), max(o[3], r[3]))
    return out


INK = corner_ink()


def notes(w, h):
    """the rectangles over the page that a row must stay clear of: (x0, y0, x1, y1), x1 and y1 open"""
    low = 20 if (w < 560 and h < 300) else 0                    # screen.c shim_screen_papers
    b, s = INK['Back'], INK['Select']
    back = (10 + b[0], h - 55 + low + b[1], 10 + b[2], h - 55 + low + b[3])            # 0x7699a4: (10, H - 55)
    sel = (w - 80 + s[0], h - 75 + low + s[1], w - 80 + s[2], h - 75 + low + s[3])     # 0x769a60/70: (W - 80, H - 75)
    tap = (-1000, h - 75, 95, h + 1000)                                                 # tweaks.c on_back
    return [('BACK ink', back), ('SELECT ink', sel), ('BACK tap', tap)]


def clear_across(w, h, x0, x1):
    """[x0, x1) clear of every note at every height (rows scroll past the bottom)"""
    return [n for n, r in notes(w, h) if x0 < r[2] and x1 > r[0]]


def width(text):
    return sum(ADV.get(ord(c), 0) for c in text)


def config(name, mods):
    cfg = os.path.join(TOP, name)
    shutil.rmtree(cfg, ignore_errors=True)
    os.makedirs(os.path.join(cfg, 'mods'))
    order = []
    for mid, src, kind in mods:
        d = os.path.join(cfg, 'mods', mid)
        if isinstance(src, str) and os.path.isdir(src):
            shutil.copytree(src, d)
        else:
            os.makedirs(d)
            meta = src if isinstance(src, dict) else {'id': mid, 'name': src, 'version': '1'}
            open(os.path.join(d, 'mod.json'), 'w', encoding='utf-8').write(json.dumps(meta, ensure_ascii=False))
            if kind == 'script':
                open(os.path.join(d, 'main.lua'), 'w').write('-- a PC script mod\n')
            else:
                os.makedirs(os.path.join(d, 'resources'))
                open(os.path.join(d, 'resources', 'x.txt'), 'w').write('x')
        order.append(mid)
    open(os.path.join(cfg, 'mods', 'mods.txt'), 'w').write('# order\n' + ''.join('on %s\n' % m for m in order))
    return cfg


class Run(object):
    def __init__(self, cfg, moves, w=480, h=270, lang=0, exe=DESK):
        p = subprocess.run([exe, cfg, str(w), str(h), str(lang)] + [str(m) for m in moves], capture_output=True)
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
                m = re.match(r'ROW (\d+) id=(\d+) box=([-\d.]+),([-\d.]+),([-\d.]+),([-\d.]+) name_x=([-\d.]+) value_x=([-\d.]+)',
                             line)
                cur['rows'].append(dict(i=int(m.group(1)), id=int(m.group(2)), box=[float(m.group(k)) for k in range(3, 7)],
                                        name_x=float(m.group(7)), value_x=float(m.group(8))))
            elif line.startswith('STATE '):
                cur['state'] = dict(kv.split('=', 1) for kv in line[6:].split() if '=' in kv)
            else:
                cur['lines'].append(line)

    def last(self, move=None):
        bl = [b for b in self.blocks if move is None or b['move'] == move]
        return bl[-1] if bl else {'ops': [], 'rows': [], 'state': {}, 'lines': []}


def st(block, key, cast=int):
    v = block['state'].get(key)
    return cast(float(v)) if v is not None else None


def writes(block):
    out = []
    for op in block['ops']:
        m = re.match(r'write (\d) ([-\d.]+) ([-\d.]+) (\d+) (.*)', op)
        if m:
            out.append((int(m.group(1)), float(m.group(2)), float(m.group(3)), int(m.group(4)), m.group(5)))
    return out


def renders(block, anim):
    out = []
    for op in block['ops']:
        m = re.match(r'render (\S+) (\d+) ([-\d.]+) ([-\d.]+)', op)
        if m and m.group(1) == anim:
            out.append((float(m.group(3)), float(m.group(4))))
    return out


os.makedirs(TOP, exist_ok=True)
print('font: %d glyphs, %d kerning pairs; U+2026 advance %s, "..." %d' % (len(ADV), KERN, ADV.get(0x2026), width('...')))
check(0x2026 in ADV and KERN == 0, 'the menu font has its own "…" and no kerning: a width is the sum of the advances')

LONGS = [
    ('void', 'The Void with 4 charges', 'res'),
    ('taxes', 'Cosmetic Taxes MB: Fiend Folio\'s Rocks and Scenaries Refreshed Overhaul', 'res'),
    ('fy', 'FUCK YOU (over Curse of the Unknown + Lost)', 'res'),
    ('girls', 'Girls of the apocalypse + bestia NSFW version', 'res'),
    ('one-word', 'W' * 60, 'res'),
    ('cyr', 'Очень длинное название мода для проверки обрезки', 'res'),
    ('script', 'A PC script mod with a long name dropped in by hand, nothing for the port', 'script'),
    ('short', 'Cooler', 'res'),
]
cfg = config('long', LONGS)
FULL = [n for _, n, _ in LONGS]
CANVASES = ((480, 270), (585, 270), (600, 270), (720, 270), (480, 360), (481, 271))
print("corner papers' ink about their place: %s" % INK)
for (w, h) in ((480, 270), (585, 270)):
    print('  %dx%d: %s' % (w, h, notes(w, h)))

print('1-3. the list on every canvas and set')
for (w, h) in CANVASES:
    for lang in (0, 1, 2, 3, 4):
        r = Run(cfg, ['mods', 'draw', 'notes'], w=w, h=h, lang=lang)
        d = r.last('draw')
        rs = Run(cfg, ['mods', 'drag', '240', '250', '-400', 'draw'], w=w, h=h, lang=lang).last('draw')
        d = {'state': d['state'], 'rows': rs['rows'], 'ops': rs['ops']}
        pw = st(d, 'paper_w')
        cap = 256 + int((min(w, 480) - 48 - 256) // BAND) * BAND
        # the widest line clear of the corner papers: the paper's middle, the label a cursor right of a line's start
        mid = (w - 480) // 2 + 240
        ns = notes(w, h)
        back_r = max(r[2] for n, r in ns if r[0] < mid)
        sel_l = min(r[0] for n, r in ns if r[0] > mid)
        tcap = min(cap - CURSOR_W - 2 * MARGIN, 2 * (mid + CURSOR_W - back_r), 2 * (sel_l - mid) + 1)
        user = [x for x in d['rows'] if x['id'] >= 1000]
        x0, x1 = d['rows'][0]['box'][0], d['rows'][0]['box'][2]
        tag = '%dx%d set %d' % (w, h, lang)
        check(pw <= cap and x0 >= 0 and x1 <= w, '%s: the paper (%d wide, %.0f..%.0f) on the screen, at most %d'
              % (tag, pw, x0, x1, cap), d['state'])
        on, off = ONOFF[lang]
        valued = GAP + max(on, off) + GAP + ARROW_W
        room = min(pw - 2 * MARGIN - CURSOR_W, tcap) - CURSOR_W - valued
        texts = writes(d)
        arrows = renders(d, 'Arrow')
        bad = []
        for row in user:
            k = row['id'] - 1000
            if row['box'][3] < 0 or row['box'][1] > h:
                continue
            mine = [t for t in texts if t[0] == 0 and abs(t[1] - row['name_x']) < 0.01 and row['box'][1] <= t[2] + 20 <= row['box'][3]]
            if len(mine) != 1:
                bad.append(('no label', k, row))
                continue
            lab = mine[0][4]
            full = FULL[k]
            if width(full) <= room:
                if lab != full:
                    bad.append(('cut though it fits', k, lab))
            else:
                # the longest start of the name that fits with the "…", the spaces before the "…" let go
                n = max(i for i in range(len(full)) if width(full[:i] + ELL) <= room)
                want = full[:n].rstrip(' ') + ELL
                if lab != want or width(lab) > room:
                    bad.append(('not the longest start that fits, with "…"', k, lab, want, width(lab), room))
            if abs(mine[0][3] - width(lab)) > 0.01:
                bad.append(('the desk and the .fnt disagree', lab, mine[0][3], width(lab)))
            left = row['name_x'] - CURSOR_W
            right = row['name_x'] + width(lab) + valued
            if left < x0 + MARGIN - 1 or right > x1 - MARGIN + 1:
                bad.append(('out of the margins', k, left, right, x0, x1))
            arr = [a for a in arrows if row['box'][1] <= a[1] <= row['box'][3]]
            if not arr or arr[0][0] + ARROW_W > x1 - MARGIN + 1:
                bad.append(('arrow', k, arr))
            elif clear_across(w, h, row['name_x'], arr[0][0] + ARROW_W):
                bad.append(('under a corner paper', k, row['name_x'], arr[0][0] + ARROW_W,
                            clear_across(w, h, row['name_x'], arr[0][0] + ARROW_W)))
        check(not bad, '%s: every row inside the paper, each label the name or its longest start with "…"' % tag, bad)
        names = [l for l in r.last('notes')['lines'] if l.startswith('NAME ')]
        whole = all(any(l.endswith(' ' + f) for l in names) for f in FULL[:4] + FULL[6:])
        red = [l for l in names if l.startswith('NAME 6 ')]
        if lang == 0:
            check(whole, '%s: the names themselves are whole (shim_usermods_name)' % tag, names)
            check(red and red[0].split()[2] == 'red', '%s: the script mod\'s name is red, as in 1.71' % tag, red)

print('5. a finger')
for (w, h) in ((480, 270), (600, 270), (480, 360)):
    r = Run(cfg, ['mods', 'draw'], w=w, h=h)
    for row in [x for x in r.last('draw')['rows'] if x['id'] >= 1000][:5]:
        k = row['id'] - 1000
        y = (row['box'][1] + row['box'][3]) / 2.0
        if y < 0 or y > h - 80:
            # lower rows: scrolled up first
            continue
        x = row['name_x'] + 20
        t = Run(cfg, ['mods', 'tap', '%.1f' % x, '%.1f' % y], w=w, h=h)
        check(st(t.last('tap'), 'cursor') == row['i'], '%dx%d: a tap on the cut label of mod %d highlights its row' % (w, h, k),
              t.last('tap')['state'])
        ax = row['value_x'] + 40 + GAP + 8
        t = Run(cfg, ['mods', 'tap', '%.1f' % ax, '%.1f' % y], w=w, h=h)
        s = t.last('tap')['state']
        check(s.get('page') == 'user' and s.get('user') == str(k), '%dx%d: a tap on its arrow opens mod %d\'s page' % (w, h, k), s)
# ON/OFF: the first tap highlights, the second flips; the list is written (mods.txt)
r = Run(cfg, ['mods', 'draw'])
row = [x for x in r.last('draw')['rows'] if x['id'] == 1001][0]
y, x = (row['box'][1] + row['box'][3]) / 2.0, row['name_x'] + 20
Run(cfg, ['mods', 'tap', '%.1f' % x, '%.1f' % y, 'tap', '%.1f' % x, '%.1f' % y])
txt = open(os.path.join(cfg, 'mods', 'mods.txt'), encoding='utf-8').read()
check('off taxes' in txt and 'on void' in txt, 'a second tap on a cut row turns that mod off (mods.txt)', txt)
Run(cfg, ['mods', 'tap', '%.1f' % x, '%.1f' % y, 'tap', '%.1f' % x, '%.1f' % y])
txt = open(os.path.join(cfg, 'mods', 'mods.txt'), encoding='utf-8').read()
check('on taxes' in txt, '... and back on', txt)
r = Run(cfg, ['mods', 'pad', 'down', 'pad', 'down', 'pad', 'down', 'state'])
check(st(r.last('state'), 'cursor') is not None, 'the pad walks the list', r.last('state')['state'])

print('6. the mod\'s page')
for (w, h) in ((480, 270), (585, 270), (600, 270), (480, 360)):
    for lang in (0, 1):
        widths = set()
        rl = Run(cfg, ['mods', 'draw'], w=w, h=h, lang=lang)
        widths.add(st(rl.last('draw'), 'paper_w'))
        for k, full in enumerate(FULL):
            r = Run(cfg, ['mods', 'user', str(k), 'draw'], w=w, h=h, lang=lang)
            d = r.last('draw')
            widths.add(st(d, 'paper_w'))
            x0, x1 = d['rows'][0]['box'][0], d['rows'][-1]['box'][2]
            rows = [x for x in d['rows'] if x['id'] not in (30, 31)]       # the lines over the title are rows too
            first_row = min(x['box'][1] for x in rows)
            title = [t for t in writes(d) if t[0] in (0, 1) and t[2] < first_row - 4 and t[4] != '']
            joined = ' '.join(t[4] for t in title)
            ok = joined == full or (full.startswith('W') and ''.join(t[4] for t in title) == full)
            inside = all(t[1] >= x0 + MARGIN - 1 and t[1] + t[3] <= x1 - MARGIN + 1 for t in title)
            below = all(t[2] + (30 if t[0] == 1 else 20) <= first_row for t in title)
            arrows = renders(d, 'Arrow')
            under = []
            for t in writes(d):
                if t[2] >= first_row - 4 and t[0] in (0, 2) and clear_across(w, h, t[1], t[1] + t[3]):
                    under.append(('text', t))
            for a in arrows:
                if clear_across(w, h, a[0], a[0] + ARROW_W):
                    under.append(('arrow', a))
            for x in rows:
                if x['id'] in (23,) and clear_across(w, h, x['value_x'], x['value_x'] + 54):
                    under.append(('ON/OFF', x))
            check(not under, "%dx%d set %d: mod %d's page rows clear of BACK and SELECT" % (w, h, lang, k), under)
            check(ok and inside and below, '%dx%d set %d: mod %d\'s page has its whole name over the rows, %d line(s), inside the '
                  'paper' % (w, h, lang, k, len(title)), (joined, [(t[1], t[3], t[2]) for t in title], x0, x1, first_row))
        check(len(widths) == 1, '%dx%d set %d: the list and every page one width' % (w, h, lang), widths)

print('7. a setting label too long for the paper')
WIDE = {'id': 'wide-set', 'name': 'Wide settings', 'version': '1',
        'settings': [{'key': 'a', 'kind': 'switch', 'label': 'W' * 24, 'default': False},
                     {'key': 'b', 'kind': 'choice', 'label': 'M' * 24,
                      'values': [{'value': 'x', 'label': 'W' * 24}, {'value': 'y', 'label': 'Y'}], 'default': 'x'}]}
cfg2 = config('wideset', [('wide-set', WIDE, 'res')])
for (w, h) in ((480, 270), (585, 270), (600, 270)):
    r = Run(cfg2, ['mods', 'user', '0', 'draw'], w=w, h=h)
    d = r.last('draw')
    x0, x1 = d['rows'][0]['box'][0], d['rows'][0]['box'][2]
    labels = [t for t in writes(d) if t[0] == 0 and t[4].startswith(('W', 'M'))]
    out = [t for t in labels if t[1] < x0 or t[1] + t[3] > x1 or clear_across(w, h, t[1], t[1] + t[3])]
    check(st(d, 'paper_w') <= 424 and labels and not out, '%dx%d: the settings\' rows stay on the paper' % (w, h),
          (st(d, 'paper_w'), [(t[1], t[3], t[4]) for t in labels], x0, x1))

print('9. nothing else moved')
if not OLD:
    print('  (no old desk given: skipped)')
else:
    SHORT = [('fits', 'Four charges', 'res'), ('short', 'Cooler', 'res'),
             ('script', 'Range fix', 'script'), ('settings-sample', SAMPLE, 'dir')]
    cfg3 = config('short', SHORT)
    moves_list = [['mods', 'draw', 'notes'], ['mods', 'user', '0', 'draw', 'notes'], ['mods', 'user', '3', 'draw'],
                  ['mods', 'user', '2', 'detail', 'draw', 'notes'], ['mods', 'pad', 'down', 'pad', 'down', 'draw'],
                  ['mods', 'drag', '240', '200', '-60', 'draw']]
    diffs = []
    for (w, h) in CANVASES:
        for lang in range(7):
            for moves in moves_list:
                a = Run(cfg3, moves, w=w, h=h, lang=lang, exe=OLD).text
                b = Run(cfg3, moves, w=w, h=h, lang=lang).text
                if a != b:
                    al, bl = a.splitlines(), b.splitlines()
                    first = next((i for i in range(min(len(al), len(bl))) if al[i] != bl[i]), min(len(al), len(bl)))
                    diffs.append(('%dx%d set %d %s' % (w, h, lang, ' '.join(moves)), al[first:first + 1], bl[first:first + 1]))
    check(not diffs, 'names that fit: every op, row and state the same as before the change (%d runs)'
          % (len(CANVASES) * 7 * len(moves_list)), diffs[:4])

print('\n%d failures' % len(failures))
for f in failures:
    print('  - ' + f)
sys.exit(1 if failures else 0)
