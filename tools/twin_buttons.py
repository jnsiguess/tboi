# -*- coding: utf-8 -*-
"""The twins' round buttons, one picture per pair of characters (the owner's
item 11, 23.09.2026).

The game has one picture for both round buttons: "Analog Stick Twin Freeze",
Jacob holding Esau, at (400, 0) of ios_buttons.png - and the overlay shows it
for the Forgotten's swap as well. That picture is the HOLD drawing from the
floor of Jacob and Esau's starting room (gfx/backdrop/controls.png, frame 19)
at its own size, its ink turned the button's grey: full ink 149, the half-ink
edges 85, on the empty round button (144, 0). Rebuilt from the floor that way
it matches the game's own to a few pixels of smoothing - so the others are
made the same way from their own floors:

    Twin Swap       the Forgotten and the Soul (16, 17): SWITCH, frame 16
    Twin Soul       Tainted Forgotten and his Soul (35, 40): the soul throwing
                    the body, frame 35 - 69 wide on the floor, too wide for the
                    button, so the throw's motion lines are left out and the
                    body stands a pixel from the soul (the owner's choice,
                    23.09.2026); both keep their height on the floor
    Twin Dark Esau  Tainted Jacob (37, 39): Dark Esau, frame 37

They go into free 64 x 64 cells of every ios_buttons.png (the Japanese set has
its own sheet, same layout), and into every ios_buttons*.anm2 as copies of the
Twin Freeze animation with their own cut. Their names do not start with
"Analog": the stick sizes scale everything that does (the handoff notes), and a
button must not grow with the sticks. hud.c picks the picture by the player.

    py -3 tools/twin_buttons.py
"""
import copy
import glob
import os
import shutil
import xml.etree.ElementTree as ET

import numpy as np
from PIL import Image

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
# M8 (plan 30.09): the game's files in work/app stay vanilla; the port's versions of
# them live in work/port-edit, and tools/port_patch.py makes port_patch/ out of them.
APP = os.path.join(ROOT, 'work', 'port-edit')
FLOOR = os.path.join(ROOT, 'work', 'app', 'repentance-resources', 'data', 'gfx', 'backdrop', 'controls.png')
TOUCH = [os.path.join(APP, 'rebirth-resources', 'data', 'gfx', 'ui', 'touch'),
         os.path.join(APP, 'rebirth-resources', 'data.jp', 'gfx', 'ui', 'touch')]

BLANK = (144, 0)
# name, the floor frame's top, the drawing's box inside the frame - or boxes,
# the pieces of one drawing put side by side a pixel apart (rows kept)
BUTTONS = [
    ('Twin Swap', 1344, (265, 36, 314, 85)),
    ('Twin Soul', 3168, [(83, 91, 113, 132), (131, 91, 158, 132)]),
    ('Twin Dark Esau', 3456, (259, 39, 316, 76)),
]
PIECE_GAP = 1
FULL, HALF = 149, 85


def glyph(floor, top, box):
    if isinstance(box, list):
        pieces = []
        for x0, y0, x1, y1 in box:
            reg = floor[top + y0:top + y1, x0:x1]
            xs = np.nonzero((reg[..., 3] > 0).any(0))[0]
            pieces.append(reg[:, xs.min():xs.max() + 1])
        gap = np.zeros((pieces[0].shape[0], PIECE_GAP, 4), int)
        joined = [pieces[0]]
        for piece in pieces[1:]:
            joined += [gap, piece]
        reg = np.concatenate(joined, 1)
    else:
        x0, y0, x1, y1 = box
        reg = floor[top + y0:top + y1, x0:x1]
    ys, xs = np.nonzero(reg[..., 3] > 0)
    return reg[ys.min():ys.max() + 1, xs.min():xs.max() + 1]


def button(sheet, g):
    """the empty round button with the drawing in its middle, in the button's greys"""
    out = sheet[BLANK[1]:BLANK[1] + 64, BLANK[0]:BLANK[0] + 64].copy()
    h, w = min(g.shape[0], 60), min(g.shape[1], 60)
    g = g[(g.shape[0] - h) // 2:(g.shape[0] - h) // 2 + h, (g.shape[1] - w) // 2:(g.shape[1] - w) // 2 + w]
    oy, ox = (64 - h) // 2, (64 - w) // 2
    sub = out[oy:oy + h, ox:ox + w]
    a = g[..., 3]
    sub[a == 255, :3] = FULL
    sub[(a > 0) & (a < 255), :3] = HALF
    return out


def free_cells(sheet, n):
    """n 64 x 64 cells of the sheet with no ink at all, on a 16-unit grid"""
    a = sheet[..., 3]
    cells = []
    for y in range(0, sheet.shape[0] - 63, 16):
        for x in range(0, sheet.shape[1] - 63, 16):
            if a[y:y + 64, x:x + 64].any():
                continue
            if any(abs(x - cx) < 64 and abs(y - cy) < 64 for cx, cy in cells):
                continue
            cells.append((x, y))
            if len(cells) == n:
                return cells
    raise SystemExit('the sheet has no room for %d buttons' % n)


def main():
    floor = np.asarray(Image.open(FLOOR).convert('RGBA')).astype(int)
    glyphs = [glyph(floor, top, box) for _, top, box in BUTTONS]
    sheets = []
    for folder in TOUCH:
        png = os.path.join(folder, 'ios_buttons.png')
        keep = png + '.before-twins'
        if not os.path.exists(keep):
            shutil.copyfile(png, keep)
        sheets.append((png, np.asarray(Image.open(keep).convert('RGBA')).astype(int)))
    # one cut for every sheet: cells free in all of them at once
    ink = sheets[0][1].copy()
    for _, sh in sheets[1:]:
        ink[..., 3] |= sh[..., 3]
    cells = free_cells(ink, len(BUTTONS))
    base = sheets[0][1]
    for png, sheet in sheets:
        out = sheet.copy()
        # the Japanese sheet came without the Twin Freeze picture at all: it
        # gets the game's own from the main sheet, or its button is empty
        if not sheet[0:64, 400:464, 3].any():
            out[0:64, 400:464] = base[0:64, 400:464]
            print('  %s: had no Twin Freeze picture, the main sheet put in' % png)
        for g, (x, y) in zip(glyphs, cells):
            out[y:y + 64, x:x + 64] = button(base, g)
        Image.fromarray(out.astype(np.uint8)).save(png)
        print('  %s: buttons at %s' % (png, cells))

    anm2s = [p for p in glob.glob(os.path.join(TOUCH[0], 'ios_buttons*.anm2'))
             if not os.path.basename(p).split('.anm2')[1]]      # no .orig, no .before-
    for path in anm2s:
        keep = path + '.before-twins'
        if not os.path.exists(keep):
            shutil.copyfile(path, keep)
        tree = ET.parse(keep)
        root = tree.getroot()
        anims = root.find('Animations')
        src = [a for a in anims if a.get('Name') == 'Analog Stick Twin Freeze'][0]
        for (name, _, _), (x, y) in zip(BUTTONS, cells):
            new = copy.deepcopy(src)
            new.set('Name', name)
            for f in new.iter('Frame'):
                if f.get('XCrop') is not None:
                    f.set('XCrop', str(x))
                    f.set('YCrop', str(y))
            anims.append(new)
        tree.write(path, encoding='utf-8', xml_declaration=True)
        print('  %s: %d animations added' % (os.path.basename(path), len(BUTTONS)))


if __name__ == '__main__':
    main()
