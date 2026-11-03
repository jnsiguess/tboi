# -*- coding: utf-8 -*-
"""The owner's labels into the atlas: shrink, edge, place, fix the markup.

The owner draws WITHOUT anti-aliasing and larger than the target. He does not
need to draw the edge: it appears by itself when `LANCZOS` shrinks the picture,
and after that the alpha is clipped by a threshold — points that are too weak
we put out, points that are too strong we take up to solid.

The threshold is picked FOR EACH LABEL separately, not one for all. The reason:
the labels shrink by different amounts, and with a common threshold the
halftone share wanders from 11 to 22% — some come out harsher than the native
ones, some go blurry. We aim at the native average: across the eight native
labels of `optionsextra.png` the halftone share is 15.4..20.0%, average 17.5.

IMPORTANT about whole multiples. At a whole multiple no edge appears at all: a
2x2 block is uniform, and shrinking gives back the same solid pixels. Measured
on the ANALOGUE label: a drawing 30 high (exactly double) gives 0% halftones,
45 (triple) also 0%, and 22 (one and a half times) — 18.9%. Hence the request
to draw at a height of about 22.

We set the ink colour ourselves: (54,47,45) at any alpha. That way the edge
comes out "the same shade with a transparency of about 102", as in the native
ones.

The width is limited not by the box but by the paper: ink must not go further
right than 99, past that is the torn edge of the sheet. If at the native letter
height the label does not fit there, we shrink it harder and report the height
that came out.

    py -3 tools/labels.py <lang>          # dlc3.ru, dlc3.de, ...
    py -3 tools/labels.py <lang> --show   # count it, touch nothing

Takes files from menu-labels/assets/<lang>/labels/, puts them into the atlas
work/port-edit/repentance-resources/<lang>/gfx/ui/main menu/optionsextra.png and
fixes optionsmenu.anm2 in the same place, if the language has its own.
"""
import io
import json
import os
import sys
import xml.etree.ElementTree as ET

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import smoothing
from PIL import Image

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
# M8 (plan 30.09): the game's files in work/app stay vanilla; the port's versions of
# them live in work/port-edit, and tools/port_patch.py makes port_patch/ out of them.
RESOURCES = os.path.join(ROOT, 'work', 'port-edit', 'repentance-resources')
ASSETS = os.path.join(ROOT, 'menu-labels', 'assets')
MARKUP = os.path.join(ROOT, 'menu-labels', 'layout.json')

INK = (54, 47, 45)
TARGET = 17.5                      # native average of the halftone share
NATIVE_LOW, NATIVE_HIGH = 15.4, 20.0
MIDDLE = 9                         # where the middle of the row sits vertically


def halftone_share(img):
    h = img.getchannel('A').histogram()
    solid, half = h[255], sum(h[1:255])
    return 100.0 * half / (solid + half) if (solid + half) else 0.0


# The smoothing itself lives in tools/smoothing.py, counted off the untouched
# sheets: solid ink, one point of tone in each notch of the staircase. What
# stood here before picked a threshold width around 128 and aimed the halftone
# share at 17.5 - which carves the tone out of the letter's own rim and softens
# every edge, straight ones included (13.09.2026).


def place(drawing, height, limit=None):
    """Trim the margins, shrink, smooth the way the game does.

    Returns (image, halftone share, None) or None.
    """
    drawing = drawing.convert('RGBA')
    bbox = drawing.getchannel('A').getbbox()
    if not bbox:
        return None
    cropped = drawing.crop(bbox)
    w0, h0 = cropped.size
    w1 = max(1, round(w0 * height / h0))
    if limit and w1 > limit:               # does not fit by height — squeeze by width
        k = limit / w0
        height = max(1, round(h0 * k))
        w1 = limit
    small = cropped.resize((w1, height), Image.LANCZOS) if h0 != height else cropped

    out, share = smoothing.smooth(small)
    return out, share, None


def main():
    if len(sys.argv) < 2:
        raise SystemExit(__doc__)
    lang = sys.argv[1]
    show = '--show' in sys.argv

    markup = json.load(io.open(MARKUP, encoding='utf-8'))
    src = os.path.join(ASSETS, lang, 'labels')
    menu = os.path.join(RESOURCES, lang, 'gfx', 'ui', 'main menu')
    if not os.path.isdir(src):
        raise SystemExit('no folder with labels: %s' % src)

    atlas = Image.open(os.path.join(menu, 'optionsextra.png')).convert('RGBA')
    anm2 = os.path.join(menu, 'optionsmenu.anm2')
    own_markup = os.path.exists(anm2)
    if own_markup:
        tree = ET.parse(anm2)
        anims = {a.get('Name'): a for a in tree.getroot().iter('Animation')}

    done = 0
    for name in sorted(markup):
        m = markup[name]
        path = os.path.join(src, name + '.png')
        if not os.path.exists(path):
            print('   %-12s no file' % name)
            continue
        answer = place(Image.open(path), m['height'], m.get('limit'))
        if answer is None:
            print('   %-12s no ink in the file' % name)
            continue
        ready, half, d = answer
        w, h = ready.size
        notes = []
        if h != m['height']:
            notes.append('squeezed down to height %d' % h)
        if half < NATIVE_LOW or half > NATIVE_HIGH:
            notes.append('OUTSIDE the native range %.1f..%.1f' % (NATIVE_LOW, NATIVE_HIGH))
        line = '   %-12s %3dx%-3d halftones %4.1f%%%s' % (name, w, h, half, '')
        if 'X' in m:
            line += '  row %d..%d' % (m['X'], m['X'] + w)
        print(line + ('   ' + ', '.join(notes) if notes else ''))
        if show:
            continue

        ox = m.get('offset', 0)
        oy = 0 if 'X' in m else max(0, MIDDLE - h // 2)
        atlas.paste(Image.new('RGBA', (m['w'], m['h']), (0, 0, 0, 0)), (m['x'], m['y']))
        atlas.alpha_composite(ready, (m['x'] + ox, m['y'] + oy))

        if own_markup and 'X' in m:
            for la in anims[m['animation']].iter('LayerAnimation'):
                if la.get('LayerId') != m['layer']:
                    continue
                frames = [f for f in la.iter('Frame')]
                for i in m['frames']:
                    f = frames[i]
                    f.set('XCrop', str(m['x'])); f.set('YCrop', str(m['y']))
                    f.set('Width', str(m['w'])); f.set('Height', str(m['h']))
                    f.set('XPivot', '0'); f.set('YPivot', '0')
                    f.set('XPosition', str(m['X']))
                    f.set('YPosition', str(max(0, MIDDLE - h // 2)))
        done += 1

    if show:
        print('show only, nothing touched')
        return
    atlas.save(os.path.join(menu, 'optionsextra.png'))
    if own_markup:
        tree.write(anm2, encoding='UTF-8', xml_declaration=True)
    print('labels placed: %d' % done)


if __name__ == '__main__':
    main()
