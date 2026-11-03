# -*- coding: utf-8 -*-
"""The editor's four buttons into their sheet, by the menu's own procedure.

THE PROCEDURE IS THE ONE ALREADY WORKED OUT for the menu items (tools/lang.py,
tools/labels.py) and it is not to be reinvented: the drawing is made WITHOUT
anti-aliasing, it is SHRUNK with `LANCZOS` - the edge appears by itself in the
shrink, nothing is blurred - and the alpha is then clipped by a threshold picked
for each drawing separately so that the share of halftones lands on the native
one. Across the native menu that share is 15.4..20.0%, on average 17.5, and the
edge comes out "the same shade at a transparency of about 102" because every lit
point is repainted in the drawing's own ink and only the alpha carries the edge.

The RATIO matters. At a whole multiple no edge appears at all - a 2x2 block is
uniform and the shrink hands back the same solid points - which is why the four
drawings are taken down by one ratio that is not whole. They are drawn to look
right beside each other, so the ratio is one for all of them: their sizes stay
in proportion.

    py -3 tools/tool_icons.py                 # from the owner's Downloads
    py -3 tools/tool_icons.py <folder>
    py -3 tools/tool_icons.py --show          # count it, touch nothing
"""
import collections
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import smoothing
from PIL import Image

CELL = 32
# The drawings are used AT 100%. They used to be shrunk to nine tenths, on the
# theory that the edge had to appear by itself in the shrink - which it did, and
# it took the drawing with it: a non-whole shrink of pixel art puts every edge
# between texels, and the owner saw the result as sprouts growing off the icons
# (13.09.2026). The smoothing does not come from a shrink any more, it is laid
# into the staircase notches (tools/smoothing.py), so there is nothing left to
# shrink for.
TARGET = 17.5                       # the native average of the halftone share
NAMES = ('grid', 'reset', 'x', 'checkmark')
FROM = os.path.join(os.path.expanduser('~'), 'Downloads')
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SHEET = os.path.join(ROOT, 'port', 'app', 'src', 'main', 'assets', 'port_touch_tools.png')
# The untouched copy lives in the workbench, NOT beside the sheet: anything in
# assets/ is packed into the APK, and two dead reference pictures rode into
# every build that way until 16.09.2026.
KEEP = os.path.join(ROOT, 'menu-labels', 'reference', 'port_touch_tools-REFERENCE.png')


def halftone_share(im):
    h = im.getchannel('A').histogram()
    full, half = h[255], sum(h[1:255])
    return 100.0 * half / (full + half) if (full + half) else 0.0


# The smoothing is tools/smoothing.py: solid ink, one point of tone in each
# notch of the staircase, counted off the untouched sheets. The threshold width
# that used to be picked here carved the tone out of the drawing's own rim.


def own_ink(art):
    px = art.load()
    c = collections.Counter(px[x, y][:3]
                            for y in range(art.height) for x in range(art.width)
                            if px[x, y][3] > 200)
    return c.most_common(1)[0][0] if c else (46, 36, 37)


def from_the_sheet(show):
    """Smooth the sheet again when the owner's drawings are no longer to hand.

    Nothing is lost by doing it this way. The old smoothing put a point out only
    when it fell BELOW the threshold band, kept the band as it was, and took
    everything above it up to solid - so a point that belongs to the letter
    (alpha 128 or more) is still 128 or more in the sheet, and one that does not
    is still below it. The silhouette therefore comes back out of the sheet
    exactly as it went in, and the new rule is laid on that. Run again on its own
    output it gives the same sheet, since a notch carries 81 and ink 255.
    """
    made = Image.open(KEEP if os.path.exists(KEEP) else SHEET).convert('RGBA')
    out, share = smoothing.smooth(made, ink=own_ink(made))
    print('from the sheet itself: %d x %d, the tone sits on %.1f%% of it'
          % (out.width, out.height, share))
    if show:
        print('nothing written')
        return
    out.save(SHEET)
    out.save(KEEP)
    print('written:', SHEET)


def main():
    args = [a for a in sys.argv[1:] if not a.startswith('--')]
    show = '--show' in sys.argv
    folder = args[0] if args else FROM

    if not all(os.path.exists(os.path.join(folder, n + '.png')) for n in NAMES):
        return from_the_sheet(show)

    sheet = Image.new('RGBA', (CELL * len(NAMES), CELL), (0, 0, 0, 0))
    for i, name in enumerate(NAMES):
        path = os.path.join(folder, name + '.png')
        drawing = Image.open(path).convert('RGBA')
        bbox = drawing.getchannel('A').getbbox()
        if not bbox:
            raise SystemExit(name + ' is empty')
        art = drawing.crop(bbox)
        ink = own_ink(art)
        out, share = smoothing.smooth(art, ink=ink)
        if out.width > CELL or out.height > CELL:
            raise SystemExit('%s does not fit the cell: %dx%d' % (name, out.width, out.height))
        print('%-10s %2dx%-2d -> %2dx%-2d  ink %-15s  the tone sits on %4.1f%% of it'
              % (name, art.width, art.height, out.width, out.height, str(ink), share))
        sheet.paste(out, (i * CELL + (CELL - out.width) // 2, (CELL - out.height) // 2))

    if show:
        print('nothing written')
        return
    sheet.save(SHEET)
    sheet.save(KEEP)
    print('written:', SHEET)


main()
