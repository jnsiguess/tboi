# -*- coding: utf-8 -*-
"""The PORT CREDITS page: the grown sheet with the words set into it.

THE WORDS ARE BAKED, not drawn by the game. The page is one picture the menu
puts up (credits.c), so everything on it is set here, once, and the port needs
no text engine of its own to keep in step with.

THE FONT is the game's own, and every cut of it is used AT ITS OWN SIZE. Names
and headings are set in the SAME cut - teammeatfont12 - and only the capitals
tell them apart. Setting the headings larger made the page top-heavy and left
the names looking like a footnote (the owner, 13.09.2026); this face writes
lower case as small capitals, so a name in a small cut comes out smaller still
than its size suggests.

    The first try set everything in teammeatfont16 and shrank it - the headings
    by a third, the names by a half. The headings came out well; the names were
    eaten alive, and the owner said so in one word (13.09.2026). The reason is
    plain once measured: the atlases are ALREADY smoothed - of the lit points in
    teammeatfont10 nearly three quarters are halftones - so a glyph eleven
    points tall is mostly rim. Shrinking blurs the rim into the middle, and the
    threshold then eats the stroke.

    So: the names and the small print are set in teammeatfont10 and the headings
    in teammeatfont12, both at their own size, nothing resampled. The rule in
    CLAUDE.md - draw at 100% and bake the reductions - holds for type as much as
    for art, and a cut made for ten points IS the baked reduction.

THE SMOOTHING is the one the main menu's own buttons wear: ink (55,43,45), a
second OPAQUE tone (159,150,152), transparency exactly zero. Since the atlas
brings its own halftones the two tones come straight off it - the solid middle
of a stroke is ink, its rim is the light tone - and nothing has to be blurred to
get them - and WHERE they go is counted off the sheet rather than guessed. See
two_tone below: on the native buttons every single light point sits in a notch
of the letter's staircase, never along a straight edge. Three goes were spent
tuning a threshold that carved the light tone out of the glyph's own rim, which
thins the letter and blurs every edge; the owner is the one who said the
smoothing there is laid OVER the letters rather than eaten out of them, and the
count bore him out to the point.

THE RULE under a heading is the GAME'S OWN. A straight bar was the first try and
read as a ruler laid on the page; a line I wandered by hand was no better. The
challenge menu already has what is wanted - its "Strikeout" layer
(challengemenu.anm2) draws one of eight strokes of 272 by 16 out of
ChallengePH.png, and the three dark ones are inked in (55,43,44), which is the
menu's own ink to the digit. A stroke is taken whole, so both of its ends are
the ends the artist drew, and squeezed across to the width wanted - which is
what the menu itself does to them, at 45 to 50 per cent. The squeeze is by
POINT: dropping columns keeps the stroke as crisp as it was, where a smooth
shrink would grey it out.

THE TESTERS' LIST is meant to grow, so it is not laid out by hand: the names are
poured down the right side in two columns as far as the room allows. Adding a
name to TESTING below changes nothing else on the page.

    py -3 tools/credits_page.py [--show]
"""
import os
import random
import sys
import tempfile

import numpy as np
from PIL import Image, ImageChops

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import credits_paper
import smoothing
import typeset

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
INTO = os.path.join(ROOT, 'port', 'app', 'src', 'main', 'assets',
                    'port_credits.png')
SHOW = os.path.join(tempfile.gettempdir(), 'credits-page.png')

FOLDER = os.path.join(typeset.RESOURCES, 'data', 'font')
HEAD_FONT = 'teammeatfont12.fnt'
BODY_FONT = 'teammeatfont12.fnt'
NOTE_FONT = 'teammeatfont10.fnt'
STRIKES = os.path.join(typeset.RESOURCES, 'data', 'gfx', 'ui', 'main menu',
                       'challengeph.png')
STRIKE_AT = ((0, 832), (0, 848), (0, 864))   # the dark strokes of the eight

INK = (55, 43, 45)                  # the menu's ink
LIGHT = (159, 150, 152)             # the second tone its buttons wear
SILHOUETTE = 128                    # from here up the glyph's own point is ink
STROKE_THIN = 1                     # points taken off the bottom of the game's stroke

# The sheet is 440x250 and everything within 23 points of its edge is torn
# paper, so the words live inside that with a little air of their own.
MARGIN = 23                         # how far in the paper is flat, measured
LEFT_X = 26                         # the single column
LEFT_W = 134
RIGHT_X = 176                       # the double one
SUB_STEP = 120
TOP_Y = 30                          # every y here is the TOP OF A CAPITAL
BOTTOM_Y = 186                      # where the columns must end
NOTE_Y = 196                        # and where the small print begins

LINE = 16                           # from one name to the next
NOTE_LINE = 12
RULE_GAP = 3                        # from the letters down to the stroke
HEAD_GAP = 6                        # from a stroke down to the first name under it
BLOCK_GAP = 16                      # from a block to the heading of the next

PORT_BY = ['Wicked']
THANKS = ['Seredyon', 'emp0ry', 'V-ALPHA', 'Zelitr']
TESTING = ['ablablababa', 'KFW', 'TemichKlevez', 'Xekus', 'BLVNSLE', 'DENiZTR8008',
           'dumkl', 'Anski', 'Guts_UWU', 'GlassesInBlue',
           'Miolekko', 'fahed64', 'Jimszz']

# Repentance is an expansion; the game itself is Rebirth, and the notice names
# the game (the owner corrected this, 13.09.2026).
NOTE = ['The Binding of Isaac: Rebirth (c) Nicalis, Inc.',
        'and Edmund McMillen. All rights reserved.']


def set_line(text, font):
    """One line at the font's own size, in its own line box, as an alpha map.

    Nothing is cropped and nothing is resampled: every line keeps the whole box,
    so lines pasted at the same top sit on the same baseline however tall their
    letters happen to be. Answers the map and how far the pen travelled.

    A glyph is laid in with `put`, NOT with paste(glyph, at, glyph). Handing the
    same picture as both the image and the mask SQUARES every value: a halftone
    of 128 comes out at 64 and one of 64 at 16. That quietly crushed the font's
    own smoothing to a quarter of itself, the threshold then finished off what
    was left, and the letters came out gnawed - which is what the owner saw and
    what he rightly suspected was not the fault of any scaling (13.09.2026).
    """
    common, chars, sheets = typeset._font(FOLDER, font)
    width = 8
    for c in text:
        z = chars.get(ord(c))
        width += (int(z[6] * typeset.SPACE) if c == ' ' else max(z[6], z[4] + z[2])) if z else 6
    canvas = Image.new('L', (width + 8, common['height'] + 8), 0)
    x = 4
    for c in text:
        z = chars.get(ord(c))
        if not z:
            x += 6
            continue
        sx, sy, w, h, xo, yo, xa, page = z
        if c == ' ':
            x += int(xa * typeset.SPACE)
            continue
        if w and h:
            glyph = sheets[page].crop((sx, sy, sx + w, sy + h)).getchannel('A')
            put(canvas, glyph, (x + xo, 4 + yo))
        x += xa
    return canvas, x - 4


def grown(a):
    """One step right and down, with the hairline gaps kept.

    The same treatment the menu labels get (lang.py): at this size the cut alone
    stands a good deal lighter than the game's own lettering, and the page read
    as eaten beside the menu. The thin copy is PADDED to the grown size, never
    resized - preserve_gaps resizes one that does not match, and then it punches
    the gaps through at pixels that have moved.
    """
    w, h = a.size
    base = np.zeros((h + 1, w + 1), dtype=np.uint8)
    base[:h, :w] = np.asarray(a)
    fat = base.copy()
    fat[:, 1:] = np.maximum(fat[:, 1:], base[:, :-1])
    fat[1:, :] = np.maximum(fat[1:, :], base[:-1, :])
    thin = Image.new('RGBA', (w + 1, h + 1), (0, 0, 0, 0))
    thin.putalpha(Image.fromarray(base))
    full = Image.new('RGBA', (w + 1, h + 1), (0, 0, 0, 0))
    full.putalpha(Image.fromarray(fat))
    return typeset.preserve_gaps(full, thin).getchannel('A')


def cap_top(font):
    """How far down the line box a capital begins, so every y here means the
    same thing whatever the cut or the letters of the line."""
    if font not in _cap:
        art, _ = set_line('H', font)
        box = art.getbbox()
        _cap[font] = box[1] if box else 0
    return _cap[font]


_cap = {}


def put(layer, art, at):
    """Lay an alpha map into the page's map, keeping whichever point is darker."""
    box = (at[0], at[1], at[0] + art.width, at[1] + art.height)
    layer.paste(ImageChops.lighter(layer.crop(box), art), at)


def write(layer, text, font, x, top, bold=False):
    """One line with the top of its capitals at `top`. Answers the pen.

    Only the HEADINGS are grown. The menu grows its labels a point because they
    are set in capitals throughout; here the names are mixed case, and this face
    writes lower case as SMALL CAPITALS seven or eight points tall - a whole
    point of growth on those is half the letter, and `ablablababa` turned into a
    blot (13.09.2026).
    """
    art, pen = set_line(text, font)
    put(layer, grown(art) if bold else art, (x, top - cap_top(font)))
    return pen + (1 if bold else 0)


def stroke_profile(strip):
    """For each column: where its run of ink starts and how long it is."""
    px = strip.load()
    w, h = strip.size
    out = []
    for x in range(w):
        top, run = 0, 0
        for y in range(h):
            if px[x, y] >= 128:
                if not run:
                    top = y
                run += 1
        out.append((top, run))
    return out


def thinned(strip, drop):
    """A finer pen, the same hand: `drop` points off the bottom of every run.

    The strokes are drawn for the challenge titles, which are set larger than
    this page - measured, they run four to five points thick, and under a
    capital of twelve that reads as a marker rather than a rule. Taking points
    off the bottom keeps the wander and the hard edge exactly as drawn, which
    a vertical shrink would not.
    """
    out = strip.copy()
    px, src = out.load(), strip.load()
    w, h = strip.size
    for x in range(w):
        y = 0
        while y < h:
            if src[x, y] < 128:
                y += 1
                continue
            start = y
            while y < h and src[x, y] >= 128:
                y += 1
            for k in range(start + max(1, (y - start) - drop), y):
                px[x, k] = 0
    return out


def rule_of(width, which):
    """One of the game's own strikeout strokes, brought to `width`.

    BOTH ENDS ARE THE ARTIST'S. The length is taken out of the MIDDLE, at the
    place where the stroke on the left of the cut and the stroke on the right
    of it run at the same height and the same thickness - so the join cannot be
    seen. Cutting the tail off instead left a square end, which the owner saw at
    once (13.09.2026); squeezing the whole stroke across, which is what the menu
    does to them, thickens the wander and blunts it at this size.
    """
    x, y = STRIKE_AT[which % len(STRIKE_AT)]
    strip = Image.open(STRIKES).convert('RGBA').getchannel('A').crop((x, y, x + 272, y + 16))
    strip = strip.crop(strip.getbbox())
    strip = thinned(strip, STROKE_THIN)
    take = strip.width - width
    if take <= 0:
        return strip
    prof = stroke_profile(strip)
    best = None
    for a in range(8, strip.width - take - 8):      # never cut into either end
        b = a + take
        cost = abs(prof[a][0] - prof[b][0]) * 3 + abs(prof[a][1] - prof[b][1])
        if best is None or cost < best[0]:
            best = (cost, a)
    cut = best[1]
    out = Image.new('L', (width, strip.height), 0)
    out.paste(strip.crop((0, 0, cut, strip.height)), (0, 0))
    out.paste(strip.crop((cut + take, 0, strip.width, strip.height)), (cut, 0))
    return out


def heading(layer, text, x, top, width, which):
    """A heading and the stroke under it. Answers the top of the next line."""
    pen = write(layer, text, HEAD_FONT, x, top, bold=True)
    art = grown(set_line(text, HEAD_FONT)[0])
    ink = art.getbbox()
    foot = top + (ink[3] - cap_top(HEAD_FONT) if ink else 12)
    bar = rule_of(min(pen, width), which)
    put(layer, bar, (x, foot + RULE_GAP))
    return foot + RULE_GAP + bar.height + HEAD_GAP


def column(layer, names, x, top, step=LINE):
    for name in names:
        write(layer, name, BODY_FONT, x, top)
        top += step
    return top


def two_tone(layer):
    """The letter in ink, and the light tone in the notches of its staircase.

    The rule and the count behind it are in tools/smoothing.py - it is the same
    rule for every label and icon in this port. The page is painted onto paper,
    so its notch carries the flat (159,150,152) rather than black at 81.
    """
    return smoothing.smooth(layer, tone=LIGHT + (255,), ink=INK, silhouette=SILHOUETTE)


def main():
    src = Image.open(credits_paper.SRC).convert('RGBA')
    src = src.crop(src.getchannel('A').getbbox())
    paper = credits_paper.build(src)
    layer = Image.new('L', paper.size, 0)

    y = heading(layer, 'PORT BY', LEFT_X, TOP_Y, LEFT_W, 0)
    y = column(layer, PORT_BY, LEFT_X, y)
    y = heading(layer, 'SPECIAL THANKS', LEFT_X, y + BLOCK_GAP, LEFT_W, 1)
    left_end = column(layer, THANKS, LEFT_X, y)

    y = heading(layer, 'TESTING', RIGHT_X, TOP_Y, SUB_STEP * 2 - 8, 2)
    room = max(1, (BOTTOM_Y - y) // LINE + 1)      # how many names one column holds
    rows = min(max(1, (len(TESTING) + 1) // 2), room)
    column(layer, TESTING[:rows], RIGHT_X, y)
    column(layer, TESTING[rows:rows + room], RIGHT_X + SUB_STEP, y)
    right_end = y + rows * LINE

    note_y = NOTE_Y
    for line in NOTE:
        art, pen = set_line(line, NOTE_FONT)
        put(layer, art, ((paper.width - pen) // 2, note_y - cap_top(NOTE_FONT)))
        note_y += NOTE_LINE

    edge = layer.getbbox()
    if edge and (edge[0] < MARGIN or edge[1] < MARGIN
                 or edge[2] > paper.width - MARGIN or edge[3] > paper.height - MARGIN):
        print('WARNING: the words reach %s, past the %d the torn edge leaves' % (edge, MARGIN))

    words, share = two_tone(layer)
    paper.alpha_composite(words)
    where = SHOW if '--show' in sys.argv else INTO
    paper.save(where)
    print('the page: %d x %d, the light tone %.1f%%, nothing resampled'
          % (paper.width, paper.height, share))
    print('the columns end at %d and %d, the small print at %d, the paper allows %d'
          % (left_end, right_end, note_y, paper.height - MARGIN))
    print('the testers: %d named, %d to a column, room for %d more'
          % (len(TESTING), room, 2 * room - len(TESTING)))
    print('written to', where)


if __name__ == '__main__':
    main()
