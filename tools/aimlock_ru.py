# -*- coding: utf-8 -*-
"""Shrink ON/OFF in the Russian AIM LOCK — this time properly.

Three things I burned myself on, all written down here so as not to repeat them:

1. **The value frames are rotated** (`Rotation: 90`): the sprite lies sideways
   in the sheet. The width of the crop becomes the height on screen, and with a
   zero pivot point `XPosition` sets the RIGHT edge on screen, `YPosition` the
   top one.
2. **The Russian OFF lies at `236,62` and is 54 high**, while the frame
   pointed at the English crop `236,74 21x39` — the English frame cut the
   Russian word in half. That is what the "cropping" was, not the size.
3. **Storing it sideways is not compulsory.** We lay the shrunk sprite into the
   sheet straight and set the frame to `Rotation: 0` — otherwise there is
   simply nowhere to put `17x46`, whereas `46x17` does find room. The position
   arithmetic becomes an ordinary one into the bargain.
4. **Free pixels do not mean free room.** A transparent patch may perfectly
   well lie inside somebody else's crop: I put the sprites at `(68,438)`, and
   the crop of the name FILTER (Russian, `0,440 80x20`) runs through there — and the
   shrunk ON came out as a blot in the middle of somebody else's line. We
   look for room going around ALL the rectangles that the markup mentions.
"""
import os
import shutil
import sys
import xml.etree.ElementTree as ET

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import smoothing
from PIL import Image

sys.stdout.reconfigure(encoding='utf-8', errors='replace')
# M8 (plan 30.09): the game's files in work/app stay vanilla; the port's versions of
# them live in work/port-edit, and tools/port_patch.py makes port_patch/ out of them.
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
R = os.path.join(ROOT, 'work', 'port-edit', 'repentance-resources')
M = os.path.join('gfx', 'ui', 'main menu')
LANG, SHEET = 'dlc3.ru', 'OptionsMenu.png'
# The same as for the item name. It also keeps watch that the shrinking is a
# real one: in the Russian ON the ink in the source is exactly 17 wide, and at a height
# of 17 the factor came out as 1.0 — the sprite was not shrunk at all and
# stayed without anti-aliasing, unlike the neighbouring Russian OFF.
SCREEN_HEIGHT = 14
RIGHT_EDGE = 83           # the name ends at 40, the gap as in the neighbouring lines
TOP = 2                   # flush with the ink of the name
SOURCES = [(235, 127, 22, 40), (236, 62, 21, 54)]      # ON and OFF in Russian
ERASE = (68, 436, 40, 40)                              # the earlier miss

sheet_path = os.path.join(R, LANG, M, 'optionsmenu.png')
anm_path = os.path.join(R, LANG, M, 'optionsmenu.anm2')
sheet = Image.open(sheet_path).convert('RGBA')
tree = ET.parse(anm_path)
root = tree.getroot()
sheets = {sh.get('Id'): sh.get('Path') for sh in root.iter('Spritesheet')}
layer_sheet = {ly.get('Id'): sheets[ly.get('SpritesheetId')] for ly in root.iter('Layer')}

# every rectangle that the markup reads out of this sheet
taken = []
for anim in root.iter('Animation'):
    for la in anim.iter('LayerAnimation'):
        lid = la.get('LayerId')
        if layer_sheet.get(lid) != SHEET:
            continue
        for fr in la.iter('Frame'):
            if fr.get('XCrop') is None:
                continue
            taken.append(tuple(int(fr.get(a)) for a in ('XCrop', 'YCrop', 'Width', 'Height')))
taken = sorted(set(taken))
print('crops out of %s in the markup: %d' % (SHEET, len(taken)))

# we erase the earlier miss, it lay inside the crop of the name FILTER (Russian)
x, y, w, h = ERASE
sheet.paste(Image.new('RGBA', (w, h), (0, 0, 0, 0)), (x, y))

# and we erase our own previous laying out, otherwise every run breeds copies in the sheet
_src = set(SOURCES)
for anim in root.iter('Animation'):
    if anim.get('Name') != 'AimLock':
        continue
    for la in anim.iter('LayerAnimation'):
        if la.get('LayerId') != '4':
            continue
        for fr in la.iter('Frame'):
            if fr.get('XCrop') is None:
                continue
            rect = tuple(int(fr.get(a)) for a in ('XCrop', 'YCrop', 'Width', 'Height'))
            if rect not in _src:
                sheet.paste(Image.new('RGBA', rect[2:], (0, 0, 0, 0)), rect[:2])
                print('  previous laying out %s erased' % (rect,))

smalls = []
for cx, cy, cw, ch in SOURCES:
    clean = sheet.crop((cx, cy, cx + cw, cy + ch))
    bb = clean.getchannel('A').getbbox()
    assert bb, 'source %s is empty' % ((cx, cy, cw, ch),)
    clean = clean.crop(bb)
    k = SCREEN_HEIGHT / float(clean.size[0])
    im = clean.resize((SCREEN_HEIGHT, max(1, round(clean.size[1] * k))), Image.LANCZOS)
    bb2 = im.getchannel('A').getbbox()
    if bb2:
        im = im.crop(bb2)
    im = im.rotate(-90, expand=True)    # we lay it straight, the frame gets Rotation 0
    if abs(k - 1.0) < 0.02:
        print('     ATTENTION: no shrinking (k=%.2f), no anti-aliasing will appear' % k)
    # The smoothing the game itself uses (tools/smoothing.py): solid ink, one
    # point of tone in every notch of the staircase. The shrink leaves a full
    # gradient along every edge, which is not how a native label is made.
    im, half = smoothing.smooth(im)
    print('     the tone sits on %.1f%% of it (the native labels 12..15%%)' % half)
    smalls.append(im)
    print('  %s: ink %dx%d -> on screen %dx%d'
          % ((cx, cy, cw, ch), clean.size[0], clean.size[1], im.size[0], im.size[1]))

def intersects(x, y, w, h):
    for tx, ty, tw, th in taken:
        if x < tx + tw and tx < x + w and y < ty + th and ty < y + h:
            return (tx, ty, tw, th)
    return None


def find(w, h, used):
    """A free patch: the pixels are empty AND it falls into nobody else's crop."""
    a = sheet.getchannel('A')
    for yy in range(sheet.size[1] - h):
        for xx in range(sheet.size[0] - w):
            if intersects(xx, yy, w, h):
                continue
            if any(xx < ux + uw and ux < xx + w and yy < uy + uh and uy < yy + h
                   for ux, uy, uw, uh in used):
                continue
            if a.crop((xx, yy, xx + w, yy + h)).getbbox():
                continue
            return (xx, yy)
    return None


spots, used = [], []
for im in smalls:
    spot = find(im.size[0], im.size[1], used)
    assert spot, 'no room %dx%d outside other crops' % im.size
    spots.append(spot)
    used.append((spot[0], spot[1], im.size[0], im.size[1]))
    print('  room %s for %dx%d — empty and outside every crop' % (spot, im.size[0], im.size[1]))

frames = []
for anim in root.iter('Animation'):
    if anim.get('Name') != 'AimLock':
        continue
    for la in anim.iter('LayerAnimation'):
        if la.get('LayerId') == '4':
            frames = [fr for fr in la.iter('Frame') if fr.get('XCrop') is not None]
assert len(frames) == len(smalls)

for path in (sheet_path, anm_path):
    backup = path + '.before-aimlock2'
    if not os.path.exists(backup):
        shutil.copy2(path, backup)

for fr, im, (fx, fy) in zip(frames, smalls, spots):
    sheet.alpha_composite(im, (fx, fy))
    fr.set('XCrop', str(fx)); fr.set('YCrop', str(fy))
    fr.set('Width', str(im.size[0])); fr.set('Height', str(im.size[1]))
    fr.set('XPivot', '0'); fr.set('YPivot', '0')
    fr.set('XScale', '100'); fr.set('YScale', '100')
    fr.set('Rotation', '0')
    fr.set('XPosition', str(RIGHT_EDGE - im.size[0])); fr.set('YPosition', str(TOP))
    print('    crop %d,%d %dx%d -> screen X %d..%d Y %d..%d'
          % (fx, fy, im.size[0], im.size[1], RIGHT_EDGE - im.size[0], RIGHT_EDGE,
             TOP, TOP + im.size[1]))

sheet.save(sheet_path)
tree.write(anm_path, encoding='utf-8', xml_declaration=True)
print('written')
