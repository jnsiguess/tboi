# -*- coding: utf-8 -*-
"""Draw a menu line the way the game draws it: with the frame's rotation.

The value frames carry `Rotation: 90`, and without taking it into account
every measurement along X and Y for layer 4 lies — the width of the crop
becomes the height on screen.
"""
import os
import sys
import xml.etree.ElementTree as ET

from PIL import Image, ImageDraw

sys.stdout.reconfigure(encoding='utf-8', errors='replace')
# the port's versions of the menus (M8: work/port-edit; work/app is vanilla)
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
RES = os.path.join(ROOT, 'work', 'port-edit', 'repentance-resources')
MENU = os.path.join('gfx', 'ui', 'main menu')
SHEET_L, SHEET_R = -113, 117
PINK_L, PINK_R = -94, 107


def load_set(lang):
    doc = ET.parse(os.path.join(RES, lang, MENU, 'optionsmenu.anm2'))
    root = doc.getroot()
    sheets = {s.get('Id'): s.get('Path') for s in root.iter('Spritesheet')}
    layers = {l.get('Id'): (l.get('Name'), sheets[l.get('SpritesheetId')])
              for l in root.iter('Layer')}
    cache = {}

    def sheet(name):
        if name not in cache:
            p = os.path.join(RES, lang, MENU, name)
            if not os.path.exists(p):
                p = os.path.join(RES, lang, MENU, name.lower())
            if not os.path.exists(p):
                # some of the sheets exist only in the English set
                p = os.path.join(RES, 'data', MENU, name)
                if not os.path.exists(p):
                    p = os.path.join(RES, 'data', MENU, name.lower())
            cache[name] = Image.open(p).convert('RGBA')
        return cache[name]
    return root, layers, sheet


def piece(root, layers, sheet, frame, layer):
    """The frame's picture and its top left corner on screen."""
    x, y, w, h = (int(frame.get(a)) for a in ('XCrop', 'YCrop', 'Width', 'Height'))
    ox, oy = int(frame.get('XPivot')), int(frame.get('YPivot'))
    X, Y = int(frame.get('XPosition')), int(frame.get('YPosition'))
    angle = int(float(frame.get('Rotation') or 0))
    im = sheet(layers[layer][1]).crop((x, y, x + w, y + h))
    if angle % 360 == 0:
        return im, X - ox, Y - oy
    # rotation about the pivot point; for 90 clockwise (x,y) -> (-y, x)
    im = im.rotate(-angle, expand=True)
    if angle % 360 == 90:
        # the crop's corner (0,0) goes to (0, 0) of the new picture on a 90
        # rotation: the top left corner of the rotated picture lies at
        # (X - (h - oy), Y - ox)
        return im, X - (h - oy), Y - ox
    if angle % 360 == 270:
        return im, X - oy, Y - (w - ox)
    raise SystemExit('unknown angle %d' % angle)


def line(lang, anim_name, layer_frames=None):
    root, layers, sheet = load_set(lang)
    pieces = []
    for anim in root.iter('Animation'):
        if anim.get('Name') != anim_name:
            continue
        for la in anim.iter('LayerAnimation'):
            lid = la.get('LayerId')
            all_frames = [f for f in la.iter('Frame') if f.get('XCrop') is not None]
            if not all_frames:
                continue
            n = (layer_frames or {}).get(lid, 0)
            n = min(n, len(all_frames) - 1)
            fr = all_frames[n]
            if fr.get('Visible') == 'false':
                continue
            im, lx, ly = piece(root, layers, sheet, fr, lid)
            pieces.append((int(lid), layers[lid][0], im, lx, ly))
    pieces.sort(key=lambda t: t[0])
    return pieces


def draw(pieces, path, captions=True):
    W = SHEET_R - SHEET_L
    H = 44
    K = 5
    canvas = Image.new('RGBA', (W, H), (0, 0, 0, 0))
    for _, _, im, lx, ly in pieces:
        canvas.paste(im, (lx - SHEET_L, ly + 8), im)
    big = canvas.resize((W * K, H * K), Image.NEAREST)
    bg = Image.new('RGBA', (W * K, H * K), (247, 243, 236, 255))
    d = ImageDraw.Draw(bg)
    d.rectangle([(PINK_L - SHEET_L) * K, 0, (PINK_R - SHEET_L) * K, H * K], fill=(250, 232, 232, 255))
    bg.paste(big, (0, 0), big)
    bg.save(path)
    if captions:
        for lid, name, im, lx, ly in pieces:
            print('    layer %-3s %-16s %3dx%-3d screen X %4d..%-4d Y %3d..%-3d'
                  % (lid, name, im.size[0], im.size[1], lx, lx + im.size[0], ly, ly + im.size[1]))


if __name__ == '__main__':
    for lang, anim_name, frames in (('data', 'Filter', {'4': 1}),
                                    ('dlc3.ru', 'Filter', {'4': 1}),
                                    ('dlc3.ru', 'AimLock', {'4': 1}),
                                    ('dlc3.ru', 'ChargeBars', {'4': 0})):
        print('%s / %s' % (lang, anim_name))
        pieces = line(lang, anim_name, frames)
        draw(pieces, 'stroka_%s_%s.png' % (lang.replace('.', '_'), anim_name))
