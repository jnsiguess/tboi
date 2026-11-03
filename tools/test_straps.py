# -*- coding: utf-8 -*-
"""Placeholder straps on the menu sheets, so the buttons can be built and tapped.

The owner will draw the real ones. Until then these are deliberately plain - a
flat plaque in the paper's own tone with a dark edge - so that nobody mistakes
them for finished art and so the box they occupy is obvious at a glance.

Every set gets the same plaque, which is safe here and measured: the first
frame of gamemenu.png is identical across all seven language sets once fully
transparent pixels are compared as equal (tools/menu_safe.py). The pause sheet
is not identical everywhere, so its plaque is placed inside the region that is.

Each sheet is composed afresh from the untouched copy kept beside it, so running
this twice gives the same thing as running it once.

    py -3 tools/test_straps.py [--undo]
"""
import os
import sys

from PIL import Image, ImageDraw

# the menus as the port shows them (M8: the port's versions live in work/port-edit)
PROJECT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
ROOT = os.path.join(PROJECT, 'work', 'port-edit', 'repentance-resources')
MENU = os.path.join('gfx', 'ui', 'main menu')
SETS = ['data', 'dlc3.de', 'dlc3.es', 'dlc3.jp', 'dlc3.kr', 'dlc3.ru', 'dlc3.zh']

PAPER = (236, 209, 196, 255)        # the credits strap's own tone
EDGE = (120, 96, 88, 255)
MARK = (168, 132, 120, 255)

# sheet -> (box, the tail we keep the untouched copy under)
# The main menu strap mirrors the credits one across the paper: credits sits at
# x 328..391, the paper's right edge; this sits at its left.
# The Tweaks strap belongs on the SETTINGS paper, not the main menu - the owner
# said so once the first test button was up, and he is right: it is a settings
# screen and it should hang off the settings sheet. The same paper is what the
# pause menu draws, so a strap baked here shows in both places; the spawner's
# own strap will therefore NOT be baked but drawn by us, only while a run is on.
#
# The paper's right side is the per-language ON/OFF column (measured by
# tools/menu_safe.py), so this hangs off the LEFT edge, inside the region every
# language set agrees on: x 0..234, y 75..240.
STRAPS = {
    'optionsmenu.png': ((4, 120, 52, 158), '.before-straps'),
}


def keep_of(path, tail):
    """The untouched copy beside the sheet; made once, read every time after."""
    spare = path + tail
    if not os.path.isfile(spare):
        # copied as bytes: the tail is not an extension PIL knows how to write
        with open(path, 'rb') as a, open(spare, 'wb') as b:
            b.write(a.read())
    return Image.open(spare).convert('RGBA')


def draw_strap(im, box):
    d = ImageDraw.Draw(im)
    x0, y0, x1, y1 = box
    d.rectangle([x0, y0, x1, y1], fill=PAPER, outline=EDGE)
    # two bands across it, so it reads as a placeholder and not as a blank label
    for y in (y0 + (y1 - y0) // 3, y0 + 2 * (y1 - y0) // 3):
        d.line([(x0 + 4, y), (x1 - 4, y)], fill=MARK)
    return im


def main():
    undo = '--undo' in sys.argv
    done = 0
    for sheet, (box, tail) in STRAPS.items():
        for s in SETS:
            path = os.path.join(ROOT, s, MENU, sheet)
            if not os.path.isfile(path):
                continue
            spare = path + tail
            if undo:
                if os.path.isfile(spare):
                    with open(spare, 'rb') as a, open(path, 'wb') as b:
                        b.write(a.read())
                    done += 1
                continue
            im = keep_of(path, tail)
            draw_strap(im, box)
            im.save(path)
            done += 1
    print('%d sheets %s' % (done, 'restored' if undo else 'given a placeholder strap'))
    if not undo:
        for sheet, (box, _t) in STRAPS.items():
            print('   %-16s x %d..%d  y %d..%d' % ((sheet,) + box[0:1] + box[2:3] + box[1:2] + box[3:4]))


RETIRED = ("retired 02.10.2026 (plan 30.09, M8): it bakes PLACEHOLDER straps into optionsmenu.png from the copies of 16.09 (*.before-straps) - an old step, long replaced by the owner's straps as layers (port_patch/). Run after M8 it wrote placeholders into all seven work/port-edit sheets (02.10, 22:44) and made port_patch stale; nothing should run it again.")

if __name__ == '__main__':
    sys.exit(RETIRED)
