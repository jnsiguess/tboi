# -*- coding: utf-8 -*-
"""Variants of the touch editor's four tool buttons, to be chosen by eye (the
owner's item 4, 23.09.2026: "pixel ones in the style of the EID fonts, with a
black outline - make several variants and show me").

The editor's buttons today are four dark glyphs in 32 x 32 cells
(port_touch_tools.png): the grid, reset, cancel, done. Each variant keeps the
cell and the order:

    A  the same four icons redrawn as pixel icons, light with a 1-px black edge
    B  the words alone, in the EID face (PixelMplus10, its outline baked in)
    C  icon over word
    D  the icon on a dark plate the colour of the EID panel

Nothing here goes into the game; it writes a sheet to look at.

    py -3 tools/editor_buttons_preview.py
"""
import os
import sys

import numpy as np
from PIL import Image

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from eid_font_preview import Font      # noqa: E402

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
FONT = os.path.join(ROOT, 'port', 'app', 'src', 'main', 'assets', 'eid_default.fnt')
OUT = os.path.join(ROOT, 'other stuff', 'editor-buttons')

LIGHT = (236, 232, 226, 255)
EDGE = (14, 13, 16, 255)
PLATE = (34, 32, 39, 235)

ICONS = {
    'grid': [
        '...#....#...',
        '...#....#...',
        '############',
        '...#....#...',
        '...#....#...',
        '...#....#...',
        '...#....#...',
        '############',
        '...#....#...',
        '...#....#...',
    ],
    'reset': [
        '...####.#.',
        '.##....###',
        '.#....####',
        '#.........',
        '#.........',
        '#........#',
        '.#......#.',
        '.##....##.',
        '...####...',
    ],
    'cancel': [
        '##......##',
        '###....###',
        '.###..###.',
        '..######..',
        '...####...',
        '..######..',
        '.###..###.',
        '###....###',
        '##......##',
    ],
    'done': [
        '..........##',
        '.........###',
        '........###.',
        '.......###..',
        '##....###...',
        '###..###....',
        '.######.....',
        '..####......',
        '...##.......',
    ],
}
ORDER = ['grid', 'reset', 'cancel', 'done']
WORDS = {'en': ['GRID', 'RESET', 'CANCEL', 'DONE'], 'ru': ['СЕТКА', 'СБРОС', 'ОТМЕНА', 'ГОТОВО']}


def icon(name, scale=1):
    rows = ICONS[name]
    h, w = len(rows), max(len(r) for r in rows)
    m = np.zeros((h * scale, w * scale), bool)
    for y, r in enumerate(rows):
        for x, ch in enumerate(r):
            if ch == '#':
                m[y * scale:(y + 1) * scale, x * scale:(x + 1) * scale] = True
    # the black edge: every pixel next to the ink (8 neighbours), one pixel out
    pad = np.pad(m, 1)
    edge = np.zeros_like(pad)
    for dy in (-1, 0, 1):
        for dx in (-1, 0, 1):
            edge |= np.roll(np.roll(pad, dy, 0), dx, 1)
    out = np.zeros(pad.shape + (4,), np.uint8)
    out[edge] = EDGE
    out[pad] = LIGHT
    return Image.fromarray(out)


def word(font, text):
    w = font.width(text) + 4
    im = Image.new('RGBA', (w, font.line + 4), (0, 0, 0, 0))
    x = 2
    for ch in text:
        c = font.chars.get(ord(ch))
        if not c:
            continue
        gx, gy, gw, gh, xo, yo, xa, page = c
        glyph = font.pages[page].crop((gx, gy, gx + gw, gy + gh))
        im.alpha_composite(glyph, (x + xo, 2 + yo))
        x += xa
    box = im.getchannel('A').getbbox()
    return im.crop(box) if box else im


def cell(parts, plate=False):
    c = Image.new('RGBA', (32, 32), (0, 0, 0, 0))
    if plate:
        p = np.zeros((32, 32, 4), np.uint8)
        p[2:30, 2:30] = PLATE
        for (y, x) in ((2, 2), (2, 29), (29, 2), (29, 29)):
            p[y, x] = (0, 0, 0, 0)                  # the corners rounded by a pixel
        c.alpha_composite(Image.fromarray(p))
    total = sum(im.height for im in parts) + 2 * (len(parts) - 1)
    y = (32 - total) // 2
    for im in parts:
        if im.width > 32:                           # a word too long for the cell is left as it is
            im = im.crop(((im.width - 32) // 2, 0, (im.width - 32) // 2 + 32, im.height))
        c.alpha_composite(im, ((32 - im.width) // 2, max(0, y)))
        y += im.height + 2
    return c


def main():
    os.makedirs(OUT, exist_ok=True)
    font = Font(FONT)
    rows = []
    rows.append(('A', [cell([icon(n, 2)]) for n in ORDER]))
    for lang in ('en', 'ru'):
        rows.append(('B ' + lang, [cell([word(font, w)]) for w in WORDS[lang]]))
        rows.append(('C ' + lang, [cell([icon(n, 1), word(font, w)]) for n, w in zip(ORDER, WORDS[lang])]))
    rows.append(('D', [cell([icon(n, 2)], plate=True) for n in ORDER]))

    for label, cells in rows:
        strip = Image.new('RGBA', (128, 32), (0, 0, 0, 0))
        for i, c in enumerate(cells):
            strip.alpha_composite(c, (i * 32, 0))
        strip.save(os.path.join(OUT, 'variant_%s.png' % label.replace(' ', '_')))

    # the sheet to look at: every variant on the game's wall grey, four times over
    sheet = Image.new('RGBA', (128 + 40, 40 * len(rows)), (122, 112, 104, 255))
    for i, (label, cells) in enumerate(rows):
        for k, c in enumerate(cells):
            sheet.alpha_composite(c, (36 + k * 32, i * 40 + 4))
        tag = word(font, label)
        sheet.alpha_composite(tag, (2, i * 40 + 14))
    sheet = sheet.resize((sheet.width * 4, sheet.height * 4), Image.NEAREST)
    sheet.save(os.path.join(OUT, 'variants_x4.png'))
    print('  written to', OUT)


if __name__ == '__main__':
    main()
