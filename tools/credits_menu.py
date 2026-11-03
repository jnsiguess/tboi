# -*- coding: utf-8 -*-
"""The PORT CREDITS plaque into the main menu's sheet.

The menu draws layer 0 of gamemenu.anm2 as the crop 0,0 480x270 of the sheet,
one pixel to one, so what is painted into the sheet appears in the menu where it
was painted (tools/watermark.py learnt this the same way).

WHAT IS TAKEN FROM WHERE.

    the plaque   the owner's own backdrop, `custom bg.png`, which IS the whole
                 480x540 sheet (it used to be a 469x525 piece at 11,15). Only
                 the rectangle he actually changed is taken - x 328..402 and
                 y 141..245 - so nothing else of a language's own art is
                 disturbed. See the note by PLAQUE for what that measurement
                 found the second time round.
    the words    `credits button.png`, HIS DRAWING, LAID DOWN AS IT IS.

                 This tool used to shrink the words and give them a second tone
                 itself, aiming at the share its neighbours wear - measured on
                 the five buttons of the untouched sheet:

                     NEW RUN 14.2%   CONTINUE 14.3%   CHALLENGES 14.6%
                     STATS 13.5%     OPTIONS 14.2%    - median 14.2

                 and transparency exactly zero in every one of them. The owner
                 then drew the smoothing himself, by the same measure, and asked
                 for his own drawing to be used (13.09.2026). So the machinery
                 is gone and what is left is a CHECK: the drawing must be those
                 two tones and nothing else, and the share is reported so a
                 later redraw that wanders off is noticed at once.

Every sheet is composed afresh from the untouched copy kept beside it, so
running this twice gives the same thing as running it once.

    py -3 tools/credits_menu.py [--show]
"""
import os
import sys

sys.exit("retired 17.09.2026: the plaque is a layer of gamemenu.anm2 now and the title sheet "
         "is written by tools/strap_layers.py - running this would paint the plaque back into the sheet")
from collections import Counter

from PIL import Image

# M8 (plan 30.09): the game's files in work/app stay vanilla; the port's versions of
# them live in work/port-edit, and tools/port_patch.py makes port_patch/ out of them.
PROJECT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
ROOT = os.path.join(PROJECT, 'work', 'port-edit', 'repentance-resources')
MENU = os.path.join('gfx', 'ui', 'main menu')
SHEET = 'gamemenu.png'
KEEP = SHEET + '.do-watermark'          # the untouched copy; pack.py skips `.png.`
FROM = os.path.join(PROJECT, 'menu-labels', 'owner-art')

BACKDROP = 'custom bg.png'
BACKDROP_AT = (0, 0)                    # he draws the WHOLE 480x540 sheet now
PLAQUE = (328, 141, 403, 246)           # the rectangle he changed
WORDS = 'credits button.png'            # HIS DRAWING of the words, smoothing and all
SAMPLE = 'how it should look like.png'  # the backdrop with the words: read for their PLACE
WORDS_AT = (333, 150)                   # where they sit now (moved 14.09.2026)
#
# ONLY THE PLAQUE IS TAKEN, and the measurement says why. His sheet differs from
# the untouched one over rows 141..522 - but of those bands only 141..245,
# columns 329..402, comes out THE SAME against every language: 4133 points, to
# the pixel. Everything below row 270 is the sheet's SECOND FRAME, where each
# language carries its own words, and it differs from set to set by tens of
# thousands of points. Laying his whole sheet down would have put one language's
# text into all six (14.09.2026).
INK = (55, 43, 45)                      # the menu's ink, and the owner's own
LIGHT = (159, 150, 152)                 # the edge the menu's own buttons wear
TARGET = 14.2                           # the median share of that edge, measured


def words_of(sample, backdrop):
    """The words alone: where his sample departs from his own bare backdrop."""
    a, b = sample.load(), backdrop.load()
    x0, y0, x1, y1 = 0, 0, 0, 0
    found = []
    for y in range(sample.height):
        for x in range(sample.width):
            if a[x, y] != b[x, y]:
                found.append((x, y))
    if not found:
        return Image.new('RGBA', (1, 1), (0, 0, 0, 0))
    x0 = min(p[0] for p in found); x1 = max(p[0] for p in found) + 1
    y0 = min(p[1] for p in found); y1 = max(p[1] for p in found) + 1
    if (x0, y0) != WORDS_AT:
        print('NOTE: the words have moved to %d,%d - WORDS_AT says %d,%d'
              % (x0, y0, WORDS_AT[0], WORDS_AT[1]))
    out = Image.new('RGBA', (x1 - x0, y1 - y0), (0, 0, 0, 0))
    o = out.load()
    for x, y in found:
        o[x - x0, y - y0] = a[x, y]
    return out


def looked_over(words):
    """Say what the drawing is made of, and complain if it is not the two tones."""
    seen = Counter(p for p in words.getdata() if p[3])
    lit = sum(seen.values())
    light = seen.get(LIGHT + (255,), 0)
    share = 100.0 * light / lit if lit else 0.0
    print('the words: %d x %d, %d points lit, the light tone %.1f%% (the menu owns %.1f)'
          % (words.width, words.height, lit, share, TARGET))
    stray = {p: n for p, n in seen.items() if p not in (INK + (255,), LIGHT + (255,))}
    if stray:
        print('WARNING: the drawing carries tones the menu does not:')
        for p, n in sorted(stray.items(), key=lambda kv: -kv[1])[:8]:
            print('    %-22s %d points' % (str(p), n))


def main():
    show = '--show' in sys.argv
    backdrop = Image.open(os.path.join(FROM, BACKDROP)).convert('RGBA')
    words = Image.open(os.path.join(FROM, WORDS)).convert('RGBA')
    looked_over(words)

    #
    # THE WORDS ARE HIS FILE, THE PLACE IS HIS SAMPLE. `credits button.png`
    # carries the smoothing he drew by hand - 106 points of the light tone -
    # and the flattened sample does not: laid down from the sample alone the
    # words came out bare, and he said so (14.09.2026). So the sample is read
    # for one thing only, where the words stand (WORDS_AT, measured as the box
    # where it departs from the bare backdrop), and the words themselves are
    # composed from his drawing as they always were.
    #
    x0, y0, x1, y1 = PLAQUE
    patch = backdrop.crop(PLAQUE)
    print('the words go at %d,%d' % WORDS_AT)

    done = 0
    for name in sorted(os.listdir(ROOT)):
        keep = os.path.join(ROOT, name, MENU, KEEP)
        live = os.path.join(ROOT, name, MENU, SHEET)
        if not os.path.exists(keep):
            continue
        sheet = Image.open(keep).convert('RGBA')
        sheet.paste(patch, (x0, y0))                 # the plaque replaces what was there
        sheet.alpha_composite(words, WORDS_AT)       # and the words lie on it
        if not show:
            sheet.save(live)
        done += 1
        print('%-10s %s' % (name, 'counted' if show else 'written'))
    print('sheets:', done)


main()
