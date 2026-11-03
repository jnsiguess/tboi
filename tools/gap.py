# -*- coding: utf-8 -*-
"""Where a row's name ends on screen and its value begins.

The name lies on the sheet the right way up (Rotation 0, pivot point 0,0): the
screen x of a pixel equals XPosition + px.

The value lies on its side (Rotation 90, pivot 16,16). A counter-clockwise turn
maps a crop pixel (px, py) onto the offset (py - 16, 16 - px) from the pivot
point, that is, the screen x equals XPosition + py - 16, and the word reads
along py. We check this on the rows that already look right.
"""
import os, re, io
from PIL import Image

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
D = os.path.join(ROOT, 'work', 'app', 'repentance-resources', 'dlc3.ru', 'gfx', 'ui', 'main menu')
sheet = Image.open(os.path.join(D, 'optionsmenu.png')).convert('RGBA')
markup = io.open(os.path.join(D, 'optionsmenu.anm2'), encoding='utf-8').read()

def ink(x, y, w, h):
    """Bounds of the opaque part inside the crop: (min_px, max_px, min_py, max_py)."""
    patch = sheet.crop((x, y, x + w, y + h))
    pixels = patch.load()
    xs, ys = [], []
    for py in range(h):
        for px in range(w):
            if pixels[px, py][3] > 40:
                xs.append(px); ys.append(py)
    if not xs:
        return None
    return min(xs), max(xs), min(ys), max(ys)

def frames(name):
    m = re.search(r'<Animation Name="%s"(.*?)</Animation>' % name, markup, re.S)
    out = []
    for la in re.finditer(r'<LayerAnimation LayerId="(\d+)"[^>]*>(.*?)</LayerAnimation>', m.group(1), re.S):
        for f in re.finditer(r'<Frame ([^/]*)/>', la.group(2)):
            a = dict(re.findall(r'(\w+)="([^"]*)"', f.group(1)))
            if 'XCrop' in a:
                a['layer'] = la.group(1)
                out.append(a)
    return out

print('%-12s %-22s %s' % ('row', 'what', 'screen bounds of the ink'))
for name in ('Filter', 'Fullscreen', 'Popups', 'AimLock'):
    for a in frames(name):
        x, y = int(a['XCrop']), int(a['YCrop'])
        w, h = int(a['Width']), int(a['Height'])
        X = int(a['XPosition'])
        bounds = ink(x, y, w, h)
        if not bounds:
            print('%-12s %-22s empty' % (name, 'layer ' + a['layer']))
            continue
        minpx, maxpx, minpy, maxpy = bounds
        if a['Rotation'] == '0' and a['layer'] == '14':
            left, right = X + minpx, X + maxpx
            what = 'name'
        elif a['Rotation'] == '90':
            left, right = X + minpy - 16, X + maxpy - 16
            what = 'value (%s,%s)' % (x, y)
        else:
            continue
        print('%-12s %-22s %4d .. %4d' % (name, what, left, right))
