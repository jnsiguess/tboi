# -*- coding: utf-8 -*-
"""The straps of every language into the game's files - and nothing else.

strap_lang.py draws them (other stuff/straps-lang/<set>_<strap>.png); this puts
each set's picture into that set's folder and makes the strap layers of that
set's .anm2 files fit it. A strap lengthened for its word is wider, and one that
rises with its slope is taller at the top: the layer's frames take the picture's
size, and the frame is raised by the rise, so the strap's own rows stay where
the owner's are and the paper still covers its left end.

The .anm2 files are edited IN PLACE: only the Width, Height and (where the
strap is shown) YPosition of the frames of the strap layers are set, from the
fixed places below - never from what the file says - so running it twice gives
the same as once. strap_layers.py / guide_strap.py are NOT run: they rebuild
their files from copies older than later edits.

It also writes port/app/src/main/cpp/strap_sizes.h, so the taps (tweaks.c,
credits.c, spawner.c) take the same size as the picture and cannot part.

    py -3 tools/strap_lang/strap_install.py
"""
import os
import re
import shutil
import sys

from PIL import Image

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
# M8 (plan 30.09): the game's files in work/app stay vanilla; the port's versions of
# them live in work/port-edit, and tools/port_patch.py makes port_patch/ out of them.
RES = os.path.join(ROOT, 'work', 'port-edit', 'repentance-resources')
SRC = os.path.join(ROOT, 'other stuff', 'straps-lang')
HEADER = os.path.join(ROOT, 'port', 'app', 'src', 'main', 'cpp', 'strap_sizes.h')
KEEP = '.before-lang'
EXTEND = 6                                    # strap_layers.EXTEND

# in the order of tweaks_layout.h's TW_SET
SETS = [('data', 'en'), ('dlc3.ru', 'ru'), ('dlc3.de', 'de'), ('dlc3.es', 'es'),
        ('dlc3.jp', 'jp'), ('dlc3.kr', 'kr'), ('dlc3.zh', 'zh')]
KINDS = ['tweaks', 'guide', 'spawner', 'credits']
PNG = {'tweaks': ('gfx', 'ui', 'main menu', 'port_strap_tweaks.png'),
       'guide': ('gfx', 'ui', 'main menu', 'port_strap_guide.png'),
       'spawner': ('gfx', 'ui', 'port_strap_spawner.png'),
       'credits': ('gfx', 'ui', 'main menu', 'port_strap_credits.png')}
# where each strap's paper top-left stands (strap_layers.TWEAKS_AT, guide_strap
# GUIDE_AT, strap_layers.CREDITS_AT), and the animation it is shown in
SHOWN = {'tweaks': ('optionsmenu.anm2', 'PortStrap', 'Paper13', 64),
         'guide': ('optionsmenu.anm2', 'GuideStrap', 'Paper13', 64 + 52),
         'credits': ('gamemenu.anm2', 'PortStrap', 'Idle', 141)}


def keep_once(path):
    if os.path.exists(path) and not os.path.exists(path + KEEP):
        shutil.copyfile(path, path + KEEP)


def picture(folder, suf, kind):
    """the set's strap: the generated one, or the owner's (data) where the word is his"""
    own = os.path.join(RES, 'data', *PNG[kind])
    if suf == 'en':
        return Image.open(own).convert('RGBA'), own
    gen = os.path.join(SRC, '%s_%s.png' % (suf, kind))
    if os.path.exists(gen):
        return Image.open(gen).convert('RGBA'), gen
    return Image.open(own).convert('RGBA'), own


def layer_ids(text, prefix):
    return {int(i) for i in re.findall(r'<Layer Name="%s\d+" Id="(\d+)"' % prefix, text)}


def fit_layers(path, prefix, shown_anim, y, w, h):
    """Width/Height of every frame of the strap layers; YPosition where shown"""
    text = open(path, encoding='utf-8').read()
    ids = layer_ids(text, prefix)
    if not ids:
        sys.exit('%s: no %s layers' % (path, prefix))

    def per_anim(m):
        name, body = m.group(2), m.group(3)

        def per_layer(lm):
            if int(lm.group(2)) not in ids:
                return lm.group(0)
            frames = lm.group(3)
            frames = re.sub(r'Width="\d+"', 'Width="%d"' % w, frames)
            frames = re.sub(r'Height="\d+"', 'Height="%d"' % h, frames)
            if name == shown_anim:
                frames = re.sub(r'YPosition="[-\d.]+"', 'YPosition="%d"' % y, frames)
            return lm.group(1) + frames + lm.group(4)
        body = re.sub(r'(<LayerAnimation LayerId="(\d+)"[^>/]*>)(.*?)(</LayerAnimation>)', per_layer, body, flags=re.S)
        return m.group(1) + body + m.group(4)
    new = re.sub(r'(<Animation Name="([^"]*)"[^>]*>)(.*?)(</Animation>)', per_anim, text, flags=re.S)
    if new != text:
        keep_once(path)
        open(path, 'w', encoding='utf-8', newline='\n').write(new)
    return new != text


def main():
    sys.stdout.reconfigure(encoding='utf-8')
    owner_size = {k: Image.open(os.path.join(RES, 'data', *PNG[k])).size for k in KINDS}
    table = []
    for folder, suf in SETS:
        row = []
        for kind in KINDS:
            im, src = picture(folder, suf, kind)
            w, h = im.size
            rise = h - owner_size[kind][1]
            if rise < 0 or w < owner_size[kind][0]:
                sys.exit('%s %s: smaller than the owner\'s strap' % (suf, kind))
            dst = os.path.join(RES, folder, *PNG[kind])
            if suf != 'en':
                same = os.path.exists(dst) and Image.open(dst).convert('RGBA').tobytes() == im.tobytes()                     and Image.open(dst).size == im.size
                if not same:
                    keep_once(dst)
                    os.makedirs(os.path.dirname(dst), exist_ok=True)
                    im.save(dst)
            if kind in SHOWN:
                anm2, prefix, anim, y = SHOWN[kind]
                path = os.path.join(RES, folder, 'gfx', 'ui', 'main menu', anm2)
                changed = fit_layers(path, prefix, anim, y - rise, w, h)
            else:
                # the pause strap copies the paper's frames with a fixed pivot, and
                # es/jp/kr use data's pausescreen.anm2: one size for every set
                if (w, h) != owner_size[kind]:
                    sys.exit('%s spawner: %dx%d - the pause strap must keep the owner\'s size' % (suf, w, h))
                changed = False
            row.append((w, h, rise))
            print('%s %-8s %3dx%-3d rise %2d  %s%s' % (suf, kind, w, h, rise, os.path.basename(src),
                                                    '  (layers fitted)' if changed else ''))
        table.append((suf, row))

    lines = ['/* Written by tools/strap_lang/strap_install.py - do not edit by hand, run the tool. */',
             '#ifndef STRAP_SIZES_H', '#define STRAP_SIZES_H', '',
             '/*',
             ' * Each language set\'s straps as drawn (other stuff/straps-lang): the picture\'s',
             ' * width and height and how much taller than the owner\'s it rises at the top.',
             ' * A tap takes the strap\'s box from here: x from the paper edge to the',
             ' * picture\'s right end (width - %d, the columns lengthened under the paper),' % EXTEND,
             ' * y from the owner\'s top minus the rise, the picture\'s height down.',
             ' */',
             'enum { STRAP_TWEAKS, STRAP_GUIDE, STRAP_SPAWNER, STRAP_CREDITS, STRAP_KINDS };',
             '#define STRAP_EXTEND %d' % EXTEND,
             'static const struct { short w, h, rise; } STRAP_SIZE[%d][STRAP_KINDS] = {' % len(table)]
    for suf, row in table:
        lines.append('    { %s },  /* %s */' % (', '.join('{ %d, %d, %d }' % r for r in row), suf))
    lines += ['};', '', '#endif', '']
    text = '\n'.join(lines)
    if not os.path.exists(HEADER) or open(HEADER, encoding='utf-8').read() != text:
        open(HEADER, 'w', encoding='utf-8', newline='\n').write(text)
        print('wrote', HEADER)


if __name__ == '__main__':
    main()
