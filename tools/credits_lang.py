# -*- coding: utf-8 -*-
"""The PORT CREDITS page in every language set (the owner, 30.09.2026: the page
in every language, the nicknames as they are).

Built with tools/credits_page.py's own functions (paper, headings, strokes,
two-tone smoothing). The English page stays port_credits.png/.anm2 exactly as
credits_page.py makes it (check 0 refuses to go on otherwise); every other set
gets port_credits_<suffix>.png/.anm2 beside it in assets/, and credits.c picks
the page by the language (credits.c, paper_ready).

The notice under the lists: the game's name and the companies stay as they are
written; the joining words and "all rights reserved" are the language's, set in
that language's menu face at a size that reads (kana in Mochiy Pop, Hangul in
Gamja Flower, Chinese in the game's own 12 cut), on the Latin line's baseline.
"All rights reserved." stays in English in all three, as Japanese and Korean
releases print it: Hangul and ideographs at the notice's size do not read (tried,
30.09) - only the joining word is the language's.

    py -3 tools/credits_lang.py            # assets + other stuff/credits-lang/preview.png
    py -3 tools/credits_lang.py --show     # the preview only
"""
import os
import sys

import numpy as np
from PIL import Image, ImageDraw, ImageFont

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, 'tools'))
import credits_page as cp          # noqa: E402
import credits_paper               # noqa: E402
import lang                        # noqa: E402
import typeset                     # noqa: E402

OUT = os.path.join(ROOT, 'other stuff', 'credits-lang')
ASSETS = os.path.join(ROOT, 'port', 'app', 'src', 'main', 'assets')
os.makedirs(OUT, exist_ok=True)
SHIPPED = os.path.join(ROOT, 'port', 'app', 'src', 'main', 'assets', 'port_credits.png')
# credits_paper.SRC points at Downloads/custom credits/, which is gone; the same
# file is kept in the project
PAPER_SRC = os.path.join(ROOT, 'menu-labels', 'owner-art', 'creditsbg.png')

SETS = [('data', 'en'), ('dlc3.ru', 'ru'), ('dlc3.de', 'de'), ('dlc3.es', 'es'),
        ('dlc3.jp', 'jp'), ('dlc3.kr', 'kr'), ('dlc3.zh', 'zh')]

HEADS = {
    'data':    ['PORT BY', 'SPECIAL THANKS', 'TESTING'],
    'dlc3.ru': ['ПОРТИРОВАЛ', 'БЛАГОДАРНОСТИ', 'ТЕСТИРОВАНИЕ'],
    'dlc3.de': ['PORTIERT VON', 'DANKSAGUNG', 'GETESTET VON'],
    'dlc3.es': ['PORTADO POR', 'GRACIAS A', 'PRUEBAS'],
    'dlc3.jp': ['いしょく', 'スペシャルサンクス', 'テストプレイ'],
    'dlc3.kr': ['이식', '특별 감사', '테스트'],
    'dlc3.zh': ['移植', '特别鸣谢', '测试'],
}
NOTE_EN = cp.NOTE
NOTES = {
    'dlc3.ru': ['The Binding of Isaac: Rebirth (c) Nicalis, Inc.',
                'и Edmund McMillen. Все права защищены.'],
    'dlc3.de': ['The Binding of Isaac: Rebirth (c) Nicalis, Inc.',
                'und Edmund McMillen. Alle Rechte vorbehalten.'],
    'dlc3.es': ['The Binding of Isaac: Rebirth (c) Nicalis, Inc.',
                'y Edmund McMillen. Todos los derechos reservados.'],
    'dlc3.jp': ['The Binding of Isaac: Rebirth (c) Nicalis, Inc.',
                'および Edmund McMillen. All rights reserved.'],
    # Korean keeps the English notice whole: 및 at this size is a blot (30.09)
    'dlc3.zh': ['The Binding of Isaac: Rebirth (c) Nicalis, Inc.',
                '及 Edmund McMillen. All rights reserved.'],
}


def cjk_run(set_name, text):
    """A run of kana, Hangul or ideographs for the notice, at a size that reads,
    as a bare map cropped to its ink"""
    # no taller than the Latin line's capitals and two points: the lines stand 12 apart
    if set_name == 'dlc3.jp':
        a = lang.drawn_straight(text, 'MochiyPopOne-Regular.ttf', FONTS, 11)
    elif set_name == 'dlc3.kr':
        a = cp.grown(lang.drawn_straight(text, 'GamjaFlower-Regular.ttf', FONTS, 13))
    else:
        a = typeset.compose(text, None, lang=set_name, jitter=False,
                            font='teammeatfontextended10.fnt', folder=ZH, thicken=False).getchannel('A')
    return a.crop(a.getbbox())


def note_line(set_name, text):
    """A notice line of Latin and CJK runs: the Latin in the note cut's own line
    box, a CJK run standing on the capitals' baseline. Answers (map, pen)."""
    # a space goes with the run it stands in: the words of a Korean phrase are
    # one run, drawn together
    runs, cur, kind = [], '', None
    for c in text:
        k = (ord(c) < 0x2000) if c != ' ' else kind
        if kind is None or k == kind:
            cur += c
            kind = k if kind is None else kind
        else:
            runs.append((kind, cur))
            cur, kind = c, k
    if cur:
        runs.append((kind, cur))
    h_art, _ = cp.set_line('H', cp.NOTE_FONT)
    box = h_art.getbbox()
    base = box[3]                               # the capitals' foot in the line box
    parts, pen = [], 0
    for latin, run in runs:
        if latin:
            art, w = cp.set_line(run, cp.NOTE_FONT)
            parts.append((art, pen - 4, 0))
            pen += w
        else:
            art = cjk_run(set_name, run.strip())
            lead = 3 if run[:1] == ' ' or pen else 0
            parts.append((art, pen + lead, base - art.height))
            pen += lead + art.width + (5 if run[-1:] == ' ' else 1)
    top = min(y for _, _, y in parts)
    height = max(y + a.height for a, _, y in parts) - top
    canvas = Image.new('L', (pen + 16, height + 8), 0)
    for art, x, y in parts:
        cp.put(canvas, art, (x + 4, y - top))
    return canvas, pen, -top

FONTS = lang.FONTS
ZH = os.path.join(typeset.RESOURCES, 'dlc3.zh', 'font')


def cjk_map(set_name, text):
    """A CJK heading as a bare coverage map, at its face's own size."""
    if set_name == 'dlc3.jp':
        a = lang.drawn_straight(text, 'MochiyPopOne-Regular.ttf', FONTS, 15)
    elif set_name == 'dlc3.kr':
        a = cp.grown(lang.drawn_straight(text, 'GamjaFlower-Regular.ttf', FONTS, 22))
    else:
        im = typeset.compose(text, None, lang=set_name, jitter=False,
                             font='teammeatfontextended16bold.fnt', folder=ZH, thicken=False)
        a = im.getchannel('A')
    return a.crop(a.getbbox())


def heading(set_name, layer, text, x, top, width, which):
    if set_name in ('dlc3.jp', 'dlc3.kr', 'dlc3.zh'):
        # the heading stands on the line a Latin heading stands on and grows
        # UPWARD, so every column below starts where the English one does
        art = cjk_map(set_name, text)
        h = cp.grown(cp.set_line('H', cp.HEAD_FONT)[0])
        box = h.getbbox()
        foot = top + (box[3] - cp.cap_top(cp.HEAD_FONT))
        cp.put(layer, art, (x, foot - art.height))
        bar = cp.rule_of(min(art.width + 1, width), which)
        cp.put(layer, bar, (x, foot + cp.RULE_GAP))
        return foot + cp.RULE_GAP + bar.height + cp.HEAD_GAP, art.width
    w = cp.set_line(text, cp.HEAD_FONT)[1] + 1
    return cp.heading(layer, text, x, top, width, which), w


def page(set_name):
    cp.STROKE_THIN = 1
    src = Image.open(PAPER_SRC).convert('RGBA')
    src = src.crop(src.getchannel('A').getbbox())
    paper = credits_paper.build(src)
    layer = Image.new('L', paper.size, 0)
    h = HEADS[set_name]
    widths = []

    y, w = heading(set_name, layer, h[0], cp.LEFT_X, cp.TOP_Y, cp.LEFT_W, 0)
    widths.append(w)
    y = cp.column(layer, cp.PORT_BY, cp.LEFT_X, y)
    y, w = heading(set_name, layer, h[1], cp.LEFT_X, y + cp.BLOCK_GAP, cp.LEFT_W, 1)
    widths.append(w)
    left_end = cp.column(layer, cp.THANKS, cp.LEFT_X, y)

    y, w = heading(set_name, layer, h[2], cp.RIGHT_X, cp.TOP_Y, cp.SUB_STEP * 2 - 8, 2)
    widths.append(w)
    room = max(1, (cp.BOTTOM_Y - y) // cp.LINE + 1)
    rows = min(max(1, (len(cp.TESTING) + 1) // 2), room)
    cp.column(layer, cp.TESTING[:rows], cp.RIGHT_X, y)
    cp.column(layer, cp.TESTING[rows:rows + room], cp.RIGHT_X + cp.SUB_STEP, y)

    note_y = cp.NOTE_Y
    for line in NOTES.get(set_name, NOTE_EN):
        if any(ord(c) >= 0x2000 for c in line):
            art, pen, lift = note_line(set_name, line)
            cp.put(layer, art, ((paper.width - pen) // 2 - 4, note_y - cp.cap_top(cp.NOTE_FONT) - lift))
        else:
            art, pen = cp.set_line(line, cp.NOTE_FONT)
            cp.put(layer, art, ((paper.width - pen) // 2, note_y - cp.cap_top(cp.NOTE_FONT)))
        note_y += cp.NOTE_LINE

    edge = layer.getbbox()
    inside = not (edge[0] < cp.MARGIN or edge[1] < cp.MARGIN or edge[2] > paper.width - cp.MARGIN
                  or edge[3] > paper.height - cp.MARGIN)
    words, share = cp.two_tone(layer)
    paper.alpha_composite(words)
    return paper, dict(left_end=left_end, widths=widths, share=share, edge=edge, inside=inside)


def main():
    try:
        sys.stdout.reconfigure(encoding='utf-8')
    except Exception:
        pass
    pages = []
    anm2 = open(os.path.join(ASSETS, 'port_credits.anm2'), encoding='utf-8').read()
    for set_name, suffix in SETS:
        im, info = page(set_name)
        im.save(os.path.join(OUT, 'port_credits_%s.png' % suffix))
        if set_name != 'data' and '--show' not in sys.argv:
            im.save(os.path.join(ASSETS, 'port_credits_%s.png' % suffix))
            text = anm2.replace('Path="port_credits.png"', 'Path="port_credits_%s.png"' % suffix)
            open(os.path.join(ASSETS, 'port_credits_%s.anm2' % suffix), 'w', encoding='utf-8',
                 newline='\n').write(text)
        flag = ''
        if info['widths'][0] > cp.LEFT_W or info['widths'][1] > cp.LEFT_W:
            flag += ' LEFT HEADING TOO WIDE'
        if info['widths'][2] > 232:
            flag += ' RIGHT HEADING TOO WIDE'
        if info['left_end'] > cp.BOTTOM_Y + cp.LINE:
            flag += ' LEFT COLUMN RUNS INTO THE NOTE (%d)' % info['left_end']
        if not info['inside']:
            flag += ' PAST THE TORN EDGE'
        print('%-3s %dx%d left column ends %d, headings %s light %.1f%% words %s%s'
              % (suffix, im.width, im.height, info['left_end'], info['widths'], info['share'], info['edge'], flag))
        pages.append((suffix, im))
        if set_name == 'data':
            ship = np.asarray(Image.open(SHIPPED).convert('RGBA')).astype(int)
            mine = np.asarray(im).astype(int)
            same = ship.shape == mine.shape and not np.abs(ship - mine).any()
            print('    check 0: English page identical to the shipped one: %s%s'
                  % (same, '' if same else ' (%d points differ)' % int((np.abs(ship - mine).sum(axis=2) > 0).sum())))
            if not same:
                sys.exit('the English page would change - refusing')
    sheet(pages)


def sheet(pages):
    """Contact sheet: seven pages at 1:1 in a 2-column grid, with a label; and x2 crops."""
    lab = ImageFont.truetype('arial.ttf', 16)
    W, H = pages[0][1].size
    cols, pad, top = 2, 12, 24
    rows = (len(pages) + cols - 1) // cols
    out = Image.new('RGBA', (cols * (W * 2 + pad) + pad, rows * (H * 2 + top + pad) + pad), (40, 36, 34, 255))
    d = ImageDraw.Draw(out)
    names = {'en': 'EN (data)', 'ru': 'RU (dlc3.ru)', 'de': 'DE (dlc3.de)', 'es': 'ES (dlc3.es)',
             'jp': 'JP (dlc3.jp)', 'kr': 'KR (dlc3.kr)', 'zh': 'ZH (dlc3.zh)'}
    for i, (suffix, im) in enumerate(pages):
        x = pad + (i % cols) * (W * 2 + pad)
        y = pad + (i // cols) * (H * 2 + top + pad)
        d.text((x, y), names[suffix], fill=(240, 230, 220), font=lab)
        out.alpha_composite(im.resize((W * 2, H * 2), Image.NEAREST), (x, y + top))
    out.save(os.path.join(OUT, 'preview.png'))


if __name__ == '__main__':
    main()
