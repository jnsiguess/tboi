# -*- coding: utf-8 -*-
"""The port's signature, laid into the main menu's backdrop.

No code and no hook: the main menu draws layer 0 of `gamemenu.anm2` as the crop
0,0 480x270 of `GameMenu.png` at position 0,0 with no scaling - the whole field,
pixel for pixel. So a mark painted into that sheet appears in the main menu and
nowhere else, at exactly the place it was painted.

Where: the empty patch to the right of the pinned sheet of paper. In the
original it is covered by the daily-run plaque (layers 7..9, x 328..472,
y 31..127), and we switch that plaque off ourselves (`daily_sign_off` in
menu.c), so the patch is free in this build and in no other.

The mark itself is DRAWN, not typeset. Several rounds of setting it from the
game's font came out unreadable, and for a reason worth writing down: at twelve
pixels to a capital there is no room to both turn a line and keep its letters -
a turn moves neighbouring rows by a whole pixel, because half a pixel does not
exist. The owner drew it by hand instead, and it is kept as a picture with its
own transparency (`menu-labels/watermark.png`), laid down as it is. Nothing here
scales, turns or thresholds it - that is the whole point.

The sheets differ from language to language, so the mark is laid into each of
them separately, always onto the untouched copy kept beside it.

    py -3 tools/watermark.py [--check]
"""
import os
import sys

from PIL import Image

# M8 (plan 30.09): the game's files in work/app stay vanilla; the port's versions of
# them live in work/port-edit, and tools/port_patch.py makes port_patch/ out of them.
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
RESOURCES = os.path.join(ROOT, 'work', 'port-edit', 'repentance-resources')
MENU = os.path.join('gfx', 'ui', 'main menu')
SHEET = 'gamemenu.png'
KEEP = SHEET + '.do-watermark'          # pack.py skips anything with `.png.` in the name
MARK = os.path.join(ROOT, 'menu-labels', 'watermark.png')

AT = (334, 79)                           # where the mark's top left corner lands
FIELD_W, FIELD_H = 480, 270


def sets():
    for name in sorted(os.listdir(RESOURCES)):
        p = os.path.join(RESOURCES, name, MENU, SHEET)
        if os.path.exists(p):
            yield name, p


def main():
    check = '--check' in sys.argv
    mark = Image.open(MARK).convert('RGBA')
    x, y = AT
    print('the mark is %dx%d, at %d,%d .. %d,%d of the %dx%d field'
          % (mark.size[0], mark.size[1], x, y,
             x + mark.size[0], y + mark.size[1], FIELD_W, FIELD_H))
    if x < 0 or y < 0 or x + mark.size[0] > FIELD_W or y + mark.size[1] > FIELD_H:
        raise SystemExit('it does not fit in the field')

    for name, path in sets():
        keep = os.path.join(os.path.dirname(path), KEEP)
        source = keep if os.path.exists(keep) else path
        sheet = Image.open(source).convert('RGBA')
        if sheet.size[0] < FIELD_W or sheet.size[1] < FIELD_H:
            print('   %-10s %s is only %dx%d - left alone' % (name, SHEET, *sheet.size))
            continue
        if check:
            print('   %-10s would be signed (from %s)'
                  % (name, 'the kept copy' if source is keep else 'the sheet itself'))
            continue
        if not os.path.exists(keep):
            # copied byte for byte: the extension is ours, and an encoder would
            # have to be told the format anyway
            with open(path, 'rb') as src, open(keep, 'wb') as dst:
                dst.write(src.read())
        sheet.alpha_composite(mark, (x, y))
        sheet.save(path)
        print('   %-10s signed' % name)


RETIRED = ("retired 02.10.2026 (plan 30.09, M8): it lays the mark into gamemenu.png starting from the copy kept on 17.09 (*.png.do-watermark), and so would undo what came after - the title strap's place cleared in the sheet (strap_layers, the owner's backdrop). The mark is not in the menus today (--check: 'would be signed' in every set). Should it come back, it is a layer of the port's gamemenu patch on a port sheet (work/port-edit, tools/port_patch.py --make), drawn over whatever title sheet the game loads.")

if __name__ == '__main__':
    sys.exit(RETIRED)
