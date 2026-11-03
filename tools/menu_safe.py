# -*- coding: utf-8 -*-
"""Where a new strap may be drawn on a menu sheet, measured rather than guessed.

Every menu sheet exists once per language set - data, dlc3.de, .es, .jp, .kr,
.ru, .zh - and the seven copies are NOT the same picture. Anything drawn where
they differ can only be applied to one of them; laid down on all seven it would
put one language's artwork into the other six. That mistake has already been
made once on this menu (14.09.2026), and the credits plaque survived only
because its box happens to be identical in all seven, to the pixel.

So before the owner draws, this says where he may: the largest rectangles in
which every language set already agrees. It also writes a picture with those
rectangles drawn on the sheet, because a list of numbers is not something you
can aim a brush at.

    py -3 tools/menu_safe.py gamemenu.png [--frame 270] [--out FILE]
    py -3 tools/menu_safe.py optionsmenu.png
"""
import os
import sys

import numpy as np
from PIL import Image, ImageDraw

# the menus as the port shows them (M8: the port's versions live in work/port-edit)
PROJECT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
ROOT = os.path.join(PROJECT, 'work', 'port-edit', 'repentance-resources')
MENU = os.path.join('gfx', 'ui', 'main menu')
SETS = ['data', 'dlc3.de', 'dlc3.es', 'dlc3.jp', 'dlc3.kr', 'dlc3.ru', 'dlc3.zh']


def agreement(sheet):
    """True where all seven language sets LOOK the same.

    Compared by appearance, not by bytes. A fully transparent pixel shows
    nothing whatever its colour channels say, and the German sheet turned out
    to carry white in its transparent areas where the others carry black - 84442
    points of difference that no one can see. Comparing raw bytes reported that
    as "the sets disagree over most of the menu" and would have sent the owner
    hunting for a corner to draw in that he did not need (16.09.2026).
    """
    first = None
    same = None
    for s in SETS:
        p = os.path.join(ROOT, s, MENU, sheet)
        if not os.path.isfile(p):
            continue
        a = np.asarray(Image.open(p).convert('RGBA')).astype(int)
        clear = a[..., 3] == 0
        a[clear] = 0                          # what cannot be seen cannot differ
        if first is None:
            first, same = a, np.ones(a.shape[:2], bool)
        else:
            same &= (a == first).all(axis=2)
    return first.astype(np.uint8), same


def biggest(mask, want=6, least=24):
    """The largest all-true rectangles, taken one at a time and blanked out.

    The classic largest-rectangle-under-a-histogram sweep, run once per wanted
    rectangle. Overlapping answers are useless to someone deciding where to
    draw, so each one found is struck out before the next is looked for.
    """
    work = mask.copy()
    found = []
    h, w = work.shape
    for _ in range(want):
        heights = np.zeros(w, int)
        best = (0, 0, 0, 0, 0)          # area, y0, x0, y1, x1
        for y in range(h):
            heights = np.where(work[y], heights + 1, 0)
            stack = []
            for x in range(w + 1):
                cur = heights[x] if x < w else 0
                start = x
                while stack and stack[-1][1] >= cur:
                    sx, sh = stack.pop()
                    area = sh * (x - sx)
                    if area > best[0]:
                        best = (area, y - sh + 1, sx, y, x - 1)
                    start = sx
                stack.append((start, cur))
        if best[0] < least * least:
            break
        _, y0, x0, y1, x1 = best
        found.append((x0, y0, x1, y1, best[0]))
        work[y0:y1 + 1, x0:x1 + 1] = False
    return found


def main():
    sheet = sys.argv[1] if len(sys.argv) > 1 else 'gamemenu.png'
    frame = None
    out = None
    for i, a in enumerate(sys.argv):
        if a == '--frame' and i + 1 < len(sys.argv):
            frame = int(sys.argv[i + 1])
        if a == '--out' and i + 1 < len(sys.argv):
            out = sys.argv[i + 1]

    art, same = agreement(sheet)
    if art is None:
        print('%s is in none of the sets' % sheet)
        return
    h, w = same.shape
    top = frame if frame else h
    mask = same[:top]
    print('%s is %d x %d; the first frame is %d tall' % (sheet, w, h, top))
    print('the seven sets agree on %.1f%% of it' % (100.0 * mask.sum() / mask.size))

    boxes = biggest(mask)
    print('largest rectangles where they agree - safe to draw in:')
    for x0, y0, x1, y1, area in boxes:
        print('   x %3d..%3d   y %3d..%3d   %3d x %3d' % (x0, x1, y0, y1, x1 - x0 + 1, y1 - y0 + 1))

    shot = Image.fromarray(art[:top]).convert('RGBA')
    over = Image.new('RGBA', shot.size, (0, 0, 0, 0))
    d = ImageDraw.Draw(over)
    # everything the sets disagree on, dimmed out: do not draw there
    bad = np.zeros(shot.size[::-1] + (4,), np.uint8)
    bad[~mask] = (200, 40, 40, 90)
    over = Image.alpha_composite(over, Image.fromarray(bad))
    d = ImageDraw.Draw(over)
    for i, (x0, y0, x1, y1, _a) in enumerate(boxes):
        d.rectangle([x0, y0, x1, y1], outline=(60, 200, 90, 255))
    shot = Image.alpha_composite(shot, over)
    shot = shot.resize((shot.width * 3, shot.height * 3), Image.NEAREST)
    where = out or os.path.join(os.environ.get('TEMP', '.'), 'safe-' + sheet)
    shot.save(where)
    print('written to', where)


if __name__ == '__main__':
    main()
