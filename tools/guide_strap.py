# -*- coding: utf-8 -*-
"""The GUIDE strap: a second strap on the settings paper, under TWEAKS (the
owner's item 22, 25.09.2026 - "another strap, in the ring and with the slide").

The strap is the owner's TWEAKS strap (menu-labels/owner-art/straps/options/
paper.png) lengthened as tools/strap_layers.py lengthens it, with GUIDE on it
instead of his hand-drawn TWEAKS. The word is the menu font
(teammeatfont12, the largest cut that fits), one letter at a time, each a step higher than the one
before - the rise his TWEAKS has - in his two colours; nothing is turned or
scaled, so no pixel falls between texels.

Its layers go into the CURRENT optionsmenu.anm2 of every set, after the TWEAKS
strap's, from a copy kept beside it on the first run (*.before-guide-strap):
strap_layers.py rebuilds from a copy older than later edits of that file, so
running it would undo them; this tool never does.

    py -3 tools/guide_strap.py            # the layers and port_strap_guide.png
    py -3 tools/guide_strap.py --show     # other stuff/guide/strap.png, on the owner's mock-up
"""
import os
import re
import sys

import numpy as np
from PIL import Image

import strap_layers as sl
import tweaks_page as tp

DROP = 52                                               # a little under TWEAKS (the owner)
GUIDE_AT = (sl.TWEAKS_AT[0], sl.TWEAKS_AT[1] + DROP)
NAMES = ['GuideStrap%d' % k for k in range(sl.STEPS)]
KEEP = '.before-guide-strap'
# THE OWNER'S OWN STRAP (25.09.2026): his paper and his lettering, drawn for it
# (menu-labels/owner-art/straps/options/guidepaper.png, guidebutton.png; he left
# them in other stuff/options). The lettering came on a canvas of its own, so it
# is set in the middle of the strap's light body. The strap is lengthened on the
# left for the slide as the TWEAKS strap is (strap_layers.lengthened).
PAPER = os.path.join(sl.ART, 'straps', 'options', 'guidepaper.png')
VISIBLE_FROM = 12                                       # where the Options paper's edge leaves the strap
WORDS = os.path.join(sl.ART, 'straps', 'options', 'guidebutton.png')


def body_box(paper):
    """the strap's light body: its fill, without the outline and the shadow"""
    a = np.asarray(paper).astype(int)
    light = (a[..., 3] == 255) & (a[..., :3].sum(axis=2) > 3 * 180)
    ys, xs = np.nonzero(light)
    return xs.min(), ys.min(), xs.max() + 1, ys.max() + 1


def lettering_box(words):
    """the lettering's own box: a stray dot on its canvas, well above the word,
    stretched the whole box and set the word low"""
    a = np.asarray(words)[..., 3] > 0
    rows = np.nonzero(a.sum(axis=1) >= 3)[0]
    cols = np.nonzero(a[rows.min():rows.max() + 1].sum(axis=0) >= 1)[0]
    return cols.min(), rows.min(), cols.max() + 1, rows.max() + 1


# His colours were picked off the screen, where the game shows TWEAKS a shade
# lighter than its file holds, so drawn by the game GUIDE came out lighter
# again (the owner, 25.09.2026: "a different colour, though I picked it"). Each
# of his four is the TWEAKS strap's own, one for one (tools: the two palettes).
TO_TWEAKS = {(255, 211, 198): (246, 210, 197),      # the body
             (143, 103, 91): (132, 103, 92),        # its outline
             (59, 34, 36): (56, 39, 41),            # the ink
             (170, 152, 156): (164, 151, 154)}      # the ink's edge


def in_tweaks_colours(im):
    a = np.asarray(im).copy()
    for was, now in TO_TWEAKS.items():
        hit = (a[..., 0] == was[0]) & (a[..., 1] == was[1]) & (a[..., 2] == was[2]) & (a[..., 3] > 0)
        a[..., :3][hit] = now
    return Image.fromarray(a)


def strap_image():
    paper = in_tweaks_colours(Image.open(PAPER).convert('RGBA'))
    words = in_tweaks_colours(Image.open(WORDS).convert('RGBA'))
    words = words.crop(lettering_box(words))
    out = sl.lengthened(paper)
    x0, y0, x1, y1 = body_box(paper)
    # in the middle of the part that shows: the Options paper covers the strap's
    # first VISIBLE_FROM columns, and a word centred on the whole strap lost half
    # its G under it
    x0 = max(x0, VISIBLE_FROM)
    at = (sl.EXTEND + (x0 + x1 - words.width) // 2, (y0 + y1 - words.height) // 2)
    out.alpha_composite(words, at)
    return out


def kept(path):
    keep = path + KEEP
    if not os.path.exists(keep):
        with open(path, 'rb') as a, open(keep, 'wb') as b:
            b.write(a.read())
    return open(keep, encoding='utf-8').read()


def settings(folder, image):
    path = os.path.join(sl.RES, folder, 'gfx', 'ui', 'main menu', 'optionsmenu.anm2')
    text, first = sl.add_sheet_and_layers(kept(path), 'port_strap_guide.png', NAMES)
    w, h = image.size

    def fn(name, frames, body):
        if name != 'Paper13':
            return sl.with_hidden(body, first, w, h, frames)
        block = ''.join(sl.layer_block(first + k, [sl.FRAME.format(
            x=GUIDE_AT[0] - sl.EXTEND + k, y=GUIDE_AT[1], px=0, py=0, w=w, h=h, d=frames, v='true')])
            for k in range(sl.STEPS))
        # beside the TWEAKS strap's layers: after the sketches, before the paper
        at = re.search(r'<LayerAnimation LayerId="10".*?</LayerAnimation>\n', body, re.S)
        return at_rest(body[:at.end()] + block + body[at.end():], first)
    open(path, 'w', encoding='utf-8', newline='\n').write(sl.each_animation(text, fn))
    return first


def at_rest(body, guide_first):
    """
    ONLY THE STRAPS IN PLACE ARE VISIBLE IN THE FILE. Changing the language
    reloads the settings sprite, and a reloaded sprite shows every layer until
    straps.c hides the unchosen ones between frames: for a frame all six steps of
    each strap were drawn, the fully slid-out one on top - the straps "slid out
    and back" at every change of language (the owner, 25.09.2026). The slide's
    steps 1..5 are hidden in Paper13 itself (the layer animation's Visible, +0x14
    of its record, which the layer draw 0x89d5d4 checks), so a reload comes up at
    rest; straps.c turns the chosen step on there.
    """
    tweaks_first = guide_first - sl.STEPS
    for first in (tweaks_first, guide_first):
        for k in range(1, sl.STEPS):
            body = body.replace('<LayerAnimation LayerId="%d" Visible="true">' % (first + k),
                                '<LayerAnimation LayerId="%d" Visible="false">' % (first + k))
    return body


def show(image):
    mock = Image.open(os.path.join(sl.ART, 'straps', 'options', 'what it should look like.png')).convert('RGBA')
    mock = mock.crop((0, 0, mock.width, 230))
    # the mock-up's TWEAKS is at (210, 49); GUIDE hangs DROP lower, from under the paper
    layer = Image.new('RGBA', mock.size, (0, 0, 0, 0))
    layer.alpha_composite(image, (210 - sl.EXTEND, 49 + DROP))
    under = Image.alpha_composite(layer, mock)          # the paper over the strap, as the game draws it
    bg = Image.new('RGBA', mock.size, (48, 42, 40, 255))
    bg.alpha_composite(under)
    out = os.path.join(tp.ROOT, 'other stuff', 'guide', 'strap.png')
    bg.resize((bg.width * 3, bg.height * 3), Image.NEAREST).save(out)
    print(out)


def main():
    image = strap_image()
    if '--show' in sys.argv:
        show(image)
        return
    firsts = {}
    for folder in sorted(os.listdir(sl.RES)):
        if not os.path.isdir(os.path.join(sl.RES, folder, 'gfx', 'ui', 'main menu')):
            continue
        image.save(os.path.join(sl.RES, folder, 'gfx', 'ui', 'main menu', 'port_strap_guide.png'))
        firsts.setdefault(settings(folder, image), []).append(folder)
    for first, folders in firsts.items():
        print('guide strap layers from %d <- %s' % (first, ', '.join(folders)))
    if len(firsts) != 1:
        sys.exit('the layer ids differ between the language sets - the port reads one id')


if __name__ == '__main__':
    main()
