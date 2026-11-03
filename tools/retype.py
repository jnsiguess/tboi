# -*- coding: utf-8 -*-
"""Retype OUR added labels in the game's font and lay them out along the line.

The native labels we do not touch. MODE is native too: it is cut out of the
native TOUCH MODE, and that is where we take it from — we neither draw it nor
scale it.

The sizes and the gaps are taken from the native FILTER line, measured off a
screenshot: name 14 units, value 16, the gap between them 17. Values we align
by their right edge at 99 — beyond that comes the torn edge of the sheet; the
native lines do the same, their values end at 66..99 while some start at 31
and others at 40.

Antialiasing is picked for each word separately: in the native labels the
share of halftones is 15.4..20.0%, average 17.5, and that is what we aim at.
One threshold for all will not do — the words shrink by different amounts.

    py -3 tools/retype.py <language> [--show]
"""
import os
import sys
import xml.etree.ElementTree as ET

from PIL import Image

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import smoothing
import typeset

# M8 (plan 30.09): the game's files in work/app stay vanilla; the port's versions of
# them live in work/port-edit, and tools/port_patch.py makes port_patch/ out of them.
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
RESOURCES = os.path.join(ROOT, 'work', 'port-edit', 'repentance-resources')
INK = (54, 47, 45)
TARGET = 17.5
JITTER = 0.35
# Values we set flush with the names. The native lines do it differently — for
# them the name is 13.9 and the value 16.0 — but the owner asks for them to be
# in proportion with the headings.
NAME_H, VALUE_H, GAP = 14, 14, 17
# AIM LOCK: the value there is the native Russian ON/OFF sprite, and it can only be
# squeezed by the scale in the markup. 15.0 -> 13.2, that is 88 percent.
AIMLOCK_SCALE = 88
# The limits on the paper. Further left than -80 is not allowed: the selection
# arrow is there, its ink occupies -93..-81. Further right than 101 the torn
# edge of the sheet begins.
LEFT, RIGHT = -80, 101

# line -> (animation, name layer, value layer, name box, value boxes)
LINES = [
    {'name': 'MODE', 'animation': 'TouchMode', 'native_name': True,
     'name_box': (8, 264, 60, 20),
     'values': [('ANALOGUE', (8, 440, 95, 20), [0, 1]), ('DIGITAL', (8, 462, 95, 20), [2])]},
    {'name': 'STICK TYPE', 'animation': 'PenOpacity',
     'name_box': (8, 286, 110, 20),
     'values': [('STATIC', (8, 330, 95, 20), [0]), ('DYNAMIC', (8, 352, 95, 20), [1])]},
    {'name': 'STICK SCALE', 'animation': 'Rumble',
     'name_box': (8, 308, 110, 20),
     'values': [('SMALL', (8, 374, 95, 20), [0]), ('MEDIUM', (8, 396, 95, 20), [1]),
                ('BIG', (8, 418, 95, 20), [2])]},
]


def halftone_share(im):
    h = im.getchannel('A').histogram()
    full, half = h[255], sum(h[1:255])
    return 100.0 * half / (full + half) if (full + half) else 0.0


# The smoothing is tools/smoothing.py: solid ink, one point of tone in every
# notch of the staircase, counted off the untouched sheets.


def match_native(im):
    return smoothing.smooth(im) + (None,)


def native_MODE(atlas):
    """Cut the word MODE out of the native TOUCH MODE label."""
    im = atlas.crop((0, 224, 128, 256))
    bb = im.getchannel('A').getbbox()
    im = im.crop(bb)
    px = im.load()
    busy = [any(px[x, y][3] > 100 for y in range(im.size[1])) for x in range(im.size[0])]
    gaps, prev = [], None
    for x in range(im.size[0]):
        if busy[x]:
            if prev is not None and x - prev > 1:
                gaps.append((x - prev, prev, x))
            prev = x
    gaps.sort(reverse=True)
    word = im.crop((gaps[0][2], 0, im.size[0], im.size[1]))
    return word.crop(word.getchannel('A').getbbox())


def main():
    lang = sys.argv[1] if len(sys.argv) > 1 else 'data'
    show = '--show' in sys.argv
    menu = os.path.join(RESOURCES, lang, 'gfx', 'ui', 'main menu')
    atlas = Image.open(os.path.join(menu, 'optionsextra.png')).convert('RGBA')
    anm = os.path.join(menu, 'optionsmenu.anm2')
    tree = ET.parse(anm)
    anims = {a.get('Name'): a for a in tree.getroot().iter('Animation')}

    print('%-13s %-9s %-16s %s' % ('label', 'size', 'on the line', 'halftones'))
    for ln in LINES:
        # name
        if ln.get('native_name'):
            name_im = native_MODE(atlas)
            name_share = halftone_share(name_im)
        else:
            typed = typeset.compose(ln['name'], NAME_H, lang=lang, jitter=JITTER)
            name_im, name_share, _ = match_native(typed)
        # values
        values = []
        for word, box, frames in ln['values']:
            typed = typeset.compose(word, VALUE_H, lang=lang, jitter=JITTER)
            ready, share, _ = match_native(typed)
            values.append((word, box, frames, ready, share))
        # The line we put in the centre of the available space, like the native
        # ones: their values are not in a column, each line is placed on its own.
        widest = max(r.size[0] for _, _, _, r, _ in values)
        gap = GAP
        total = name_im.size[0] + gap + widest
        room = RIGHT - LEFT
        if total > room:                       # does not fit — squeeze the gap
            gap = max(8, gap - (total - room))
            total = name_im.size[0] + gap + widest
        nx = LEFT + max(0, (room - total) // 2)
        vx = nx + name_im.size[0] + gap

        print('%-13s %3dx%-3d  %5d..%-5d  %4.1f%%'
              % (ln['name'], name_im.size[0], name_im.size[1], nx, nx + name_im.size[0], name_share))
        for word, box, frames, ready, share in values:
            print('%-13s %3dx%-3d  %5d..%-5d  %4.1f%%'
                  % ('   ' + word, ready.size[0], ready.size[1], vx, vx + ready.size[0], share))
        if show:
            continue

        anim = anims[ln['animation']]
        _place(atlas, anim, '16', [0], ln['name_box'], name_im, nx)
        for word, box, frames, ready, share in values:
            _place(atlas, anim, '18', frames, box, ready, vx)

    # AIM LOCK — the line is native, but its gap is 24 against the native 17:
    # the name ends at 16, and the Russian ON/OFF starts at 40. We move the value to the
    # left, the picture itself we do not touch.
    for la in anims['AimLock'].iter('LayerAnimation'):
        if la.get('LayerId') != '4':
            continue
        for fr in la.iter('Frame'):
            if fr.get('XPosition') is None:
                continue
            was_X = int(fr.get('XPosition'))
            if show:
                print('%-13s value X %d, scale %d%%'
                      % ('AIM LOCK', was_X, AIMLOCK_SCALE))
            else:
                fr.set('XScale', str(AIMLOCK_SCALE))
                fr.set('YScale', str(AIMLOCK_SCALE))

    if show:
        print('show only, nothing touched')
        return
    atlas.save(os.path.join(menu, 'optionsextra.png'))
    tree.write(anm, encoding='UTF-8', xml_declaration=True)
    print('done: names %d, values %d, gap %d' % (NAME_H, VALUE_H, GAP))


def _place(atlas, anim, layer, frames, box, picture, X):
    x, y, w, h = box
    w = max(w, picture.size[0])
    h = max(h, picture.size[1])
    atlas.paste(Image.new('RGBA', (w, h), (0, 0, 0, 0)), (x, y))
    atlas.paste(picture, (x, y))
    for la in anim.iter('LayerAnimation'):
        if la.get('LayerId') != layer:
            continue
        all_frames = [fr for fr in la.iter('Frame')]
        for n in frames:
            fr = all_frames[n]
            fr.set('XCrop', str(x)); fr.set('YCrop', str(y))
            fr.set('Width', str(w)); fr.set('Height', str(h))
            fr.set('XPivot', '0'); fr.set('YPivot', '0')
            fr.set('XPosition', str(X))
            fr.set('YPosition', str(max(0, 9 - picture.size[1] // 2)))


if __name__ == '__main__':
    main()
