# -*- coding: utf-8 -*-
"""A whole language: the menu labels and the title phrase.

The menu the same way as the English one: typeset in the game's font,
thickened, jitter 0.35, an anti-aliasing threshold for each word separately (we
aim at the native 17.5% of halftones), placed in the centre of the available
space.

MODE we neither translate nor typeset: as in English, it is a cut-out of the
native TOUCH MODE label of the same language. The native labels we do not
touch.

The title is one line, a tilt of 5.3 degrees and a bow of -1. The numbers are
taken off the native English touch phrase: the bottom of its letters runs at a
slope of 0.0924, and the deviation from a straight line is less than a pixel.
The title is drawn with a hard pixel: across all the native titles the
halftones are exactly zero.

    py -3 tools/lang.py <lang> [--show] [--only-menu] [--only-title]
"""
import os
import sys
import copy
import math
import xml.etree.ElementTree as ET

import numpy as np
from PIL import Image, ImageDraw, ImageFont

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import smoothing
import typeset

# M8 (plan 30.09): the game's files in work/app stay vanilla; the port's versions of
# them live in work/port-edit, and tools/port_patch.py makes port_patch/ out of them.
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
R = os.path.join(ROOT, 'work', 'port-edit', 'repentance-resources')
GAME_R = os.path.join(ROOT, 'work', 'app', 'repentance-resources')   # read only: fonts
M = os.path.join('gfx', 'ui', 'main menu')
INK = (54, 47, 45)
# The halftone share we take not as a common one but per language: in Chinese
# the native labels are anti-aliased at 15..18%, in Japanese and Korean they are
# drawn with no anti-aliasing at all, in German it is mixed. AimLock and
# TouchMode we leave out of the count — that is English Latin, copied into all
# the sets.
JITTER = 0.35
NAME_H, VALUE_H, GAP = 14, 14, 17
CURSOR_GAP = 5
FONTS = os.path.join(ROOT, 'menu-labels', 'fonts')
STEPS = {1.5: [(1, 0), (0, 1), (-1, 0)],
         2: [(1, 0), (-1, 0), (0, 1), (0, -1)]}

# Each language gets its own set, picked by measurement and by eye.
#
# Latin is typeset in the native Team Meat with whole steps — that is settled.
# For ideographs and Hangul a whole step is too coarse: between "thin" and "the
# gaps are clogged" they have exactly one step, so Chinese has fractional
# thickening, while Japanese and Korean are taken in ordinary fonts (OFL, they
# lie in menu-labels). The game's own Asian faces are angular at this size,
# while Mochiy Pop and Gamja Flower are drawn with a marker, like Team Meat.
#
# `gaps` — restoring the hairline gaps after the thickening. Without it the
# Korean `ㅌ` stuck together into a black block and the word `디지털` could only
# be read by guesswork.
#
# `own` - THE CUT IS USED AT ITS OWN SIZE AND NOTHING IS RESAMPLED.
#
# This is the root of everything that went wrong with these labels. A bitmap
# font is drawn at one size and already smoothed; asking compose() for a height
# it was not drawn at shrinks it by a ratio that is not whole, every stroke edge
# lands between texels, and the threshold that follows then keeps a stroke that
# resampled to 0.51 of a point and throws away its neighbour at 0.49. Two
# strokes the artist drew alike come out different - the gnawed look. The
# thickening was then bolted on to make up for it, and closed the counters of
# the letters on top. CLAUDE.md has said all along: draw at 100%, bake the
# reductions. A cut made for twelve points IS the baked reduction.
#
# Measured on STICK TYPE, the 12-cut at its own size:
#
#     untouched                      stroke 3.04  holes 1   (the P, and nothing else)
#     + a whole cross                stroke 6.29  holes 6   - blotted
#     + right and down, gaps kept    stroke 4.75  holes 1   - the native names are 5.07
#
# So: NAMES get the two-way growth with the gap pass, VALUES are left exactly as
# the cut draws them - the native values measure 2.98 and the bare cut 3.04.
#
# THE WEIGHT IS MEASURED, AND NAMES AND VALUES ARE NOT THE SAME WEIGHT.
# Every native label pulled out into menu-labels/ was counted (the name in the
# file says which box it belongs to):
#
#     names  (a box 20 tall): ink 14..18 high, mean 15.9; stroke 3.6..6.9, mean 5.07
#     values (a box 16 tall): ink 15..16 high, mean 15.9; stroke 2.8..4.5, mean 2.98
#
# So a native value stands as TALL as a name and is drawn far THINNER. We were
# setting both at 14 high and growing both by a whole step, which came out
# short and heavy - and heaviest exactly where the game is lightest. Now: both
# at 16, the names grown by half a point with the fine thickening, the values
# not grown at all. On STICK TYPE that gives stroke 5.04 against the native
# 5.07, and the letters keep their counters - the whole steps were closing them
# (13.09.2026, the owner called it a crude fattening and was right).
SETTINGS = {
    'data':    dict(font='teammeatfont12.fnt', height=16, own=True, pick_cut=True,
                    max_cut='teammeatfont12.fnt'),
    'dlc3.ru': dict(font='teammeatfont12.fnt', height=16, own=True, pick_cut=True,
                    max_cut='teammeatfont12.fnt'),
    'dlc3.de': dict(font='teammeatfont12.fnt', height=16, own=True, pick_cut=True,
                    max_cut='teammeatfont12.fnt'),
    'dlc3.es': dict(font='teammeatfont12.fnt', height=16, own=True, pick_cut=True,
                    max_cut='teammeatfont12.fnt'),
    # `tone` — anti-aliasing with a second opaque colour instead of halftones.
    # In these two the native labels are anti-aliased in exactly that way: a
    # measurement of the transparency shows zero, while the light tone in them
    # is 19% and 17%. As long as we aimed at the transparency, our items came
    # out a bare contour with no anti-aliasing at all.
    # Japanese and Korean keep the faces that were picked for them - Mochiy Pop
    # and Gamja Flower are drawn with a marker, like Team Meat, where the game's
    # own Asian cuts are angular at this size. What is gone is the GROWTH: a
    # whole point of it welded the kana together and filled in the circles of
    # the Hangul, and it was only ever there to make up for the old smoothing
    # eating the rim. Japanese is now drawn STRAIGHT at eighteen points - a
    # vector rasterised at the size wanted, with nothing shrunk afterwards.
    'dlc3.jp': dict(font='MochiyPopOne-Regular.ttf', folder=FONTS, own=True,
                    direct=18, grow=False, height=18, tone=19.0),
    # Korean is drawn straight at twenty-two and grown a point: without the
    # growth it came out thinner than anything beside it (the owner, 13.09.2026)
    # - 2.88 of stroke against the native 4.33. Grown it measures 4.60 and the
    # circles of the Hangul stay open. Twenty-two is as tall as the row will
    # take: the rows stand 22 apart and the label comes out 21 high.
    'dlc3.kr': dict(font='GamjaFlower-Regular.ttf', folder=FONTS, own=True,
                    direct=22, grow=True, height=18, tone=17.0),
    # Chinese was the 12-cut STRETCHED to eighteen, one and a half times, and
    # that is what made the ideographs unreadable. The 12-cut at its own size is
    # clean but it stands 15 high and light, where the game's own Chinese labels
    # are BOLD and 20 - which is why it read as a different face altogether. The
    # 16bold cut, at its own size, comes out 19 high with the same letterforms
    # the stretched one was reaching for: the owner picked it (13.09.2026).
    'dlc3.zh': dict(font='teammeatfontextended16bold.fnt',
                    folder=os.path.join(GAME_R, 'dlc3.zh', 'font'), own=True, grow=False,
                    height=18, sprite_tone=(54, 47, 45, 128)),
}
# the language's native labels, the ones we take the measurements from
OWN_NATIVE_FULL = [(0, 0, 176, 32), (0, 32, 112, 32), (0, 64, 128, 32),
                   (0, 96, 128, 32), (0, 128, 128, 32), (0, 160, 128, 32)]


def density(im):
    px = im.load()
    w, h = im.size
    inked = sum(1 for y in range(h) for x in range(w) if px[x, y][3] > 140)
    return 100.0 * inked / (w * h) if w * h else 0.0


def native_density(atlas):
    vals = []
    for x, y, w, h in OWN_NATIVE_FULL:
        im = atlas.crop((x, y, x + w, y + h))
        bb = im.getchannel('A').getbbox()
        if bb:
            vals.append(density(im.crop(bb)))
    return sum(vals) / len(vals) if vals else 42.0


# Measured off the native markup (optionsmenu.anm2, layers Option Name, OnOff,
# Adjustor1): the native rows themselves live between -74 and 102, so 102 is the
# sheet's own right edge and not our caution.
LEFT, RIGHT = -80, 102
TITLE_H, TITLE_TILT, TITLE_BOW = 15, -5.3, -1
TITLE_CUT = 156                   # the width of one copy of the phrase
TITLE_COPIES = (0, 160)           # where the copies start in the sheet
TITLE_TOP = 499                   # which row of the sheet the text runs from

FONT = {'data': 'teammeatfont16.fnt', 'dlc3.ru': 'teammeatfont16.fnt',
        'dlc3.de': 'teammeatfont16.fnt',
        'dlc3.es': 'teammeatfont16.fnt', 'dlc3.jp': 'teammeatfontextended16bold.fnt',
        'dlc3.kr': 'kr_meatfont14.fnt', 'dlc3.zh': 'teammeatfontextended16bold.fnt'}

WORDS = {
    # The line names are TYPE and SIZE in every language (the owner, 23.09.2026):
    # they set the sticks and the D-pads alike, beside MODE - mode, type, size.
    'data': ['TYPE', 'SIZE', 'STATIC', 'DYNAMIC', 'SMALL',
             'MEDIUM', 'BIG', 'ANALOGUE', 'DIGITAL', 'FLOATING', 'VERY BIG', 'BUTTONS'],
    # The line names are short: with the Russian STICK TYPE and STICK SIZE the line
    # came out at 199 against a ceiling of 181 and ran off the edge of the sheet.
    'dlc3.ru': ['ТИП', 'РАЗМЕР', 'СТАТИЧНЫЙ', 'ДИНАМИЧНЫЙ', 'МАЛЫЙ',
                'СРЕДНИЙ', 'БОЛЬШОЙ', 'АНАЛОГ', 'ЦИФРОВОЙ', 'ПЛАВАЮЩИЙ', 'ОГРОМНЫЙ', 'КНОПКИ'],
    'dlc3.de': ['TYP', 'GRÖSSE', 'STATISCH', 'DYNAMISCH', 'KLEIN',
                'MITTEL', 'GROSS', 'ANALOG', 'DIGITAL', 'SCHWEBEND', 'SEHR GROSS', 'TASTEN'],
    'dlc3.es': ['TIPO', 'TAMAÑO', 'ESTÁTICO', 'DINÁMICO', 'PEQUEÑO',
                'MEDIO', 'GRANDE', 'ANALÓGICO', 'DIGITAL', 'FLOTANTE', 'MUY GRANDE', 'BOTONES'],
    # Japanese we write in kana, like the native menu (`じゅうでんバー`):
    # ideographs do not fit into fourteen pixels — `追従` has twenty-odd strokes,
    # and they merge at any thickness. The line names are shortened to `タイプ`
    # and `サイズ`: the full `スティックタイプ` gives a line of 201 against a
    # ceiling of 181.
    'dlc3.jp': ['タイプ', 'サイズ', 'こてい', 'ついじゅう', 'ちいさい',
                'ふつう', 'おおきい', 'アナログ', 'デジタル', 'ふゆう', 'とくだい', 'ボタン'],
    'dlc3.kr': ['종류', '크기', '고정', '동적', '작게', '보통', '크게',
                '아날로그', '디지털', '유동', '매우 크게', '버튼'],
    'dlc3.zh': ['类型', '大小', '固定', '跟随', '小', '中', '大', '模拟', '数字', '浮动', '特大', '按键'],
}
# By default MODE we neither translate nor typeset — it is a cut-out of the
# native TOUCH MODE label of the same language. Where a translation is given
# here, the word is typeset on a par with the rest of our names.
MODE_NAME = {'dlc3.ru': 'РЕЖИМ'}

TITLE = {'dlc3.ru': 'КОСНИТЕСЬ ЭКРАНА', 'dlc3.de': 'TIPPE ZUM START', 'dlc3.es': 'TOCA PARA JUGAR',
         'dlc3.kr': '터치하세요', 'dlc3.zh': '点击开始'}

# The Asian languages have their own title typesetting. Latin is typeset at 15
# pixels with the default thickening — that is settled, we do not touch it.
#
# Korean and Chinese at 15 were unreadable: ideographs lose their strokes at
# such a size, and the hard threshold (across all the native titles the
# halftones are exactly zero) finishes off what is left. There was twice as
# much room as was taken at that — 79 and 55 out of 156.
#
# Korean had its font changed: `kr_meatfont14` stays a blot at any height,
# checked up to 30. Chinese had its thickening reduced: by default that is six
# shifts, the fattest there is, and the strokes stuck together into artefacts.
# The word was replaced too — `点击开始` is three times simpler in strokes than
# `触摸开始`, at the same meaning.
TITLE_SET = {
    'dlc3.kr': dict(font='GamjaFlower-Regular.ttf', folder=FONTS, height=24,
                    strength=1, gaps=True),
    'dlc3.zh': dict(height=26, strength=1 / 6.0, gaps=True),
}

# the order is the same as in English: three lines, each with a name and values
LINES = [
    {'key': 'MODE', 'animation': 'TouchMode', 'native_name': True,
     'name_box': (8, 264, 60, 20),
     # BUTTONS, the touch D-pads (the owner's item 19, 23.09.2026): the third
     # value, beside DIGITAL at 110 - free on all seven sheets and frames.
     # Not beside ANALOGUE: set in the 16-cut that word is 110 wide and its
     # crop runs into the column.
     'values': [(7, (8, 440, 95, 20), [0, 1]), (8, (8, 462, 95, 20), [2]),
                (11, (110, 462, 95, 20), [3])]},
    {'key': 0, 'animation': 'PenOpacity', 'name_box': (8, 286, 110, 20),
     # The third value has no room left in the column - the rows below are
     # spoken for down to 482 - so its box stands beside the first, at 110.
     # Checked against every rectangle layers 16 and 18 read, in all seven
     # sets, and against the ink of all seven sheets: nothing is there.
     'values': [(2, (8, 330, 95, 20), [0]), (3, (8, 352, 95, 20), [1]),
                (9, (110, 330, 95, 20), [2])]},
    {'key': 1, 'animation': 'Rumble', 'name_box': (8, 308, 110, 20),
     # The fourth size, VERY BIG (23.09.2026), beside SMALL at 110 - checked
     # free on all seven sheets and against every frame on them.
     'values': [(4, (8, 374, 95, 20), [0]), (5, (8, 396, 95, 20), [1]),
                (6, (8, 418, 95, 20), [2]), (10, (110, 374, 95, 20), [3])]},
]


OWN_NATIVE = [(0, 0, 176, 32), (0, 32, 112, 32), (0, 64, 128, 32),
              (0, 96, 128, 32), (0, 128, 128, 32), (0, 160, 128, 32)]


def native_share(atlas):
    vals = []
    for x, y, w, h in OWN_NATIVE:
        im = atlas.crop((x, y, x + w, y + h))
        hi = im.getchannel('A').histogram()
        full, half = hi[255], sum(hi[1:255])
        if full + half > 50:
            vals.append(100.0 * half / (full + half))
    return sum(vals) / len(vals) if vals else 17.5


def halftone_share(im):
    h = im.getchannel('A').histogram()
    full, half = h[255], sum(h[1:255])
    return 100.0 * half / (full + half) if (full + half) else 0.0


# The smoothing itself is tools/smoothing.py, counted off the untouched sheets:
# solid ink, one point of tone in every notch of the letter's staircase. The
# threshold band that used to be picked here - and the 17.5% it was aimed at -
# carved the tone out of the letter's own rim instead, which thins the letter
# and softens every edge, straight ones included (13.09.2026).


def match_native(im, target, tone=None):
    """A label drawn as a sprite: its notch carries black at 81.

    Chinese is the exception and it is the game's own doing: in that set the
    native labels smooth with (54,47,45) at 128, which shows a good deal darker
    on the paper - (143,132,134) against (159,149,152). Counted, not chosen.
    """
    return smoothing.smooth(im, tone=tone) if tone else smoothing.smooth(im)


LIGHT = (158, 152, 151)        # the edge in the native titles and Asian labels


def two_tone(im, target):
    """A title, and the menu of a language whose letters are dense.

    The same rule, with the tone already flattened onto the paper: the native
    Japanese menu carries 937 points of (158,152,151) and EVERY ONE of them sits
    in a notch. Transparency on them is exactly zero, which is why these cannot
    be done the sprite way.
    """
    return smoothing.smooth(im, tone=LIGHT + (255,))


def without_specks(im, limit=2):
    """Remove the torn-off specks of one or two dots.

    The tilt and the bow leave lone pixels at the edges of the phrase: in the
    German title one of them hung on the left above the line and read as stray
    rubbish.
    """
    a = im.getchannel('A').load()
    w, h = im.size
    seen = [[False] * h for _ in range(w)]
    drop = []
    for y in range(h):
        for x in range(w):
            if a[x, y] <= 140 or seen[x][y]:
                continue
            stack, dots = [(x, y)], []
            seen[x][y] = True
            while stack:
                cx, cy = stack.pop()
                dots.append((cx, cy))
                for dx in (-1, 0, 1):
                    for dy in (-1, 0, 1):
                        nx, ny = cx + dx, cy + dy
                        if 0 <= nx < w and 0 <= ny < h and not seen[nx][ny] and a[nx, ny] > 140:
                            seen[nx][ny] = True
                            stack.append((nx, ny))
            if len(dots) <= limit:
                drop += dots
    if not drop:
        return im
    out = im.copy()
    opx = out.load()
    for x, y in drop:
        opx[x, y] = (0, 0, 0, 0)
    return out


def hard(im):
    out = im.copy()
    px = out.load()
    for y in range(out.size[1]):
        for x in range(out.size[0]):
            px[x, y] = INK + (255,) if px[x, y][3] >= 128 else (0, 0, 0, 0)
    return out


def native_MODE(atlas):
    im = atlas.crop((0, 224, 128, 256))
    bb = im.getchannel('A').getbbox()
    if not bb:
        return None
    im = im.crop(bb)
    px = im.load()
    busy = [any(px[x, y][3] > 100 for y in range(im.size[1])) for x in range(im.size[0])]
    gaps, prev = [], None
    for x in range(im.size[0]):
        if busy[x]:
            if prev is not None and x - prev > 1:
                gaps.append((x - prev, prev, x))
            prev = x
    if not gaps:
        return im
    gaps.sort(reverse=True)
    word = im.crop((gaps[0][2], 0, im.size[0], im.size[1]))
    return word.crop(word.getchannel('A').getbbox())


def _grow(anim, layer, need):
    """Give a row as many frames as its values need.

    The game picks a value by FRAME NUMBER — it calls 0x89ebf8(sprite, layer,
    frame) with the byte out of the settings — so a third value on a line means
    a third frame on layer 18. And a longer animation with it: the length is
    written in three places at once, and `Rumble`, which already carries three
    values, shows exactly which. FrameNum, the root's Delay, and the Delay of
    every layer whose single frame has to cover the whole animation.
    """
    la = None
    for cand in anim.iter('LayerAnimation'):
        if cand.get('LayerId') == layer:
            la = cand
    if la is None:
        return
    frames = list(la.iter('Frame'))
    if not frames or len(frames) >= need:
        return
    for _ in range(need - len(frames)):
        la.append(copy.deepcopy(frames[-1]))
    anim.set('FrameNum', str(need))
    root = anim.find('RootAnimation')
    if root is not None:
        for fr in root.iter('Frame'):
            fr.set('Delay', str(need))
    for cand in anim.iter('LayerAnimation'):
        one = list(cand.iter('Frame'))
        if len(one) == 1:
            one[0].set('Delay', str(need))
    print('   %s: grown to %d frames' % (anim.get('Name'), need))


def _place(atlas, anim, layer, frames, box, picture, X):
    x, y, w, h = box
    w = max(w, picture.size[0])
    h = max(h, picture.size[1])
    # The box is cleared first, so the picture goes down PLAIN. Pasting it with
    # itself as the mask is what turned our tone of 81 into 26 in the sheet -
    # a*a/255 - and the smoothing became invisible beside the native labels.
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
            fr.set('XScale', '100'); fr.set('YScale', '100')
            fr.set('XPosition', str(X))
            fr.set('YPosition', str(max(0, 9 - picture.size[1] // 2)))


# The native lines that stayed English Latin in the language's set: in Russian
# those are `AIM LOCK` and `FOUND HUD` with a Russian HUD (the second one the game itself
# translated halfway). Translating them is an addition, not an edit of the
# native.
#
# The ink coordinates we set explicitly rather than inherit from the previous
# label: Russian words are longer than English ones, and the Russian AIM LOCK ran
# over its own value — the name is drawn on top, and the Russian OFF looked cut off. The
# native Russian lines put the name at -74..-82 and the value at about 61
# (the Russian CHARGE BAR: -74..42 and 61..78), and those are the fields we aim at.
NATIVE_TRANSLATIONS = {
    'dlc3.ru': [
        # The native Russian names run at a height of 15..21. The Russian FOUND was
        # raised to 17, while the Russian AIM LOCK at 17 took up 139 pixels and
        # stopped fitting into the line together with its value — it was left
        # at 14.
        dict(animation='AimLock', box=(0, 192, 96, 32), text='БЛОК СТРЕЛЬБЫ',
             left=-74, top=2, height=14),
        dict(animation='FoundHUD', box=(0, 32, 112, 32), text='НАЙДЕННОЕ',
             left=-74, top=2, height=17),
    ],
}


def translate_native(lang, atlas, anims, target, show, s, menu_path, layer_sheets):
    for t in NATIVE_TRANSLATIONS.get(lang, []):
        x, y, w, h = t['box']
        ready, share = compose_like_native(t['text'], t.get('height', NAME_H), s, target)
        print('   %-16s %3dx%-3d in the native box %d,%d  ink X %d..%d  %4.1f%%'
              % (t['text'], ready.size[0], ready.size[1], x, y,
                 t['left'], t['left'] + ready.size[0], share))
        if show:
            continue
        nw, nh = max(w, ready.size[0]), max(h, ready.size[1])
        atlas.paste(Image.new('RGBA', (nw, nh), (0, 0, 0, 0)), (x, y))
        atlas.paste(ready, (x, y))
        for anim in anims.values():
            if anim.get('Name') != t['animation']:
                continue
            for la in anim.iter('LayerAnimation'):
                if la.get('LayerId') == '16':
                    for fr in la.iter('Frame'):
                        if fr.get('XCrop') is None:
                            continue
                        # pivot to zero: the ink starts at the top left corner
                        # of the crop, and the position is given directly,
                        # without corrections
                        fr.set('XCrop', str(x)); fr.set('YCrop', str(y))
                        fr.set('Width', str(nw)); fr.set('Height', str(nh))
                        fr.set('XPivot', '0'); fr.set('YPivot', '0')
                        fr.set('XScale', '100'); fr.set('YScale', '100')
                        fr.set('XPosition', str(t['left']))
                        fr.set('YPosition', str(t['top']))
                elif la.get('LayerId') == '5':
                    # We move the arrow cursor along with the name: our names
                    # are longer than the native ones and have gone left, while
                    # the arrow stayed put and ran over the first letter. In
                    # the native lines the gap is +2..+8.
                    for fr in la.iter('Frame'):
                        if fr.get('XCrop') is None:
                            continue
                        cx, cy = int(fr.get('XCrop')), int(fr.get('YCrop'))
                        cw, ch = int(fr.get('Width')), int(fr.get('Height'))
                        pivot = int(fr.get('XPivot'))
                        bb = layer_sheets['4'].crop(
                            (cx, cy, cx + cw, cy + ch)).getchannel('A').getbbox()
                        if not bb:
                            continue
                        right = int(fr.get('XPosition')) - pivot + bb[2]
                        want = t['left'] - CURSOR_GAP
                        fr.set('XPosition', str(int(fr.get('XPosition')) + want - right))


BOLD = [(1, 0), (0, 1)]           # right and down: a point of growth, not two


def grown(a):
    """One step right and down on a bare map, with the hairline gaps kept.

    The mending must be handed a thin copy OF THE SAME SIZE. preserve_gaps
    resizes one that is not, and the growth makes it a point larger - so it was
    resampling the thin copy by 88/87 and 13/12, finding the gaps in the
    resampled picture and punching them through the grown one at pixels that had
    moved. That is what chewed the letters, twice over (13.09.2026). The growth
    goes right and down, so the two share their top left corner and the thin one
    only has to be PADDED.
    """
    w, h = a.size
    base = np.zeros((h + 1, w + 1), dtype=np.uint8)
    base[:h, :w] = np.asarray(a)
    fat = base.copy()
    fat[:, 1:] = np.maximum(fat[:, 1:], base[:, :-1])
    fat[1:, :] = np.maximum(fat[1:, :], base[:-1, :])
    thin = Image.new('RGBA', (w + 1, h + 1), (0, 0, 0, 0))
    thin.putalpha(Image.fromarray(base))
    full = Image.new('RGBA', (w + 1, h + 1), (0, 0, 0, 0))
    full.putalpha(Image.fromarray(fat))
    return typeset.preserve_gaps(full, thin).getchannel('A')


def drawn_straight(text, font, folder, size):
    """A vector face rasterised AT the size wanted, with nothing shrunk after.

    typeset.compose draws a TTF at fifty-four points and brings it down, which
    is right when a height has to be hit exactly; here the size is ours to
    choose, so the rasteriser is simply asked for it and no resampling happens
    at all.
    """
    face = ImageFont.truetype(os.path.join(folder, font), size)
    im = Image.new('L', (1200, size * 3), 0)
    ImageDraw.Draw(im).text((40, size), text, font=face, fill=255)
    box = im.getbbox()
    return im.crop(box) if box else im


def compose_like_native(text, height, s, target, weight=None, bold=True):
    """Typeset the word and bring its anti-aliasing to match the native one.

    The gaps we restore from the ungrown typesetting of the same word — that is
    what shows where the gap was before the thickening.
    """
    common = dict(jitter=JITTER, font=s['font'], folder=s.get('folder'))
    if s.get('own'):
        if s.get('direct'):
            plain = drawn_straight(text, s['font'], s.get('folder'), s['direct'])
        else:
            plain = typeset.compose(text, None, thicken=False, **common).getchannel('A')
        if s.get('grow', True):
            # Names and values are grown ALIKE. They were parted for a while on
            # a measurement that turned out to be of the wrong thing: the values
            # were counted over the numeric strips (HUDOffset, SFX, MUSIC - "0.0",
            # "10"), which are thin, and that pulled the figure down to 2.98. Taken
            # from one row at a time, a native value runs 3.6..4.7 against a name's
            # 4.4..6.4 - a quarter lighter, not half. The owner asked for them to be
            # set alike, which is well within that (13.09.2026).
            raw = grown(plain)
        else:
            raw = plain
        return (two_tone(raw, s['tone']) if 'tone' in s
                else match_native(raw, target, s.get('sprite_tone')))
    fractional = 'strength' in s
    strength = s.get('strength') if weight is None else weight
    raw = typeset.compose(
        text, height, strength=strength if fractional else None,
        thicken=not fractional, shifts=None if fractional else STEPS[s['step']], **common)
    if 'tone' in s:
        ready, share = two_tone(raw, s['tone'])
        thin, _ = two_tone(typeset.compose(text, height, strength=0, **common), s['tone'])
    else:
        ready, share = match_native(raw, target)
        thin, _ = match_native(typeset.compose(text, height, strength=0, **common), target)             if s.get('gaps') else (None, 0)
    if s.get('gaps') and thin is not None:
        ready = typeset.preserve_gaps(ready, thin)
    return ready, share


# The least we will let a line close up to. Seven rather than eight because
# German misses the 12-cut by ONE point at eight, and a point of gap cannot be
# seen where a whole cut smaller can. The layout below uses the same number, so
# what the cut is chosen against is what the cut is laid out with.
OURS_START = 264                  # from this row down the sheet is ours alone
OURS_LEFT = 8                     # and from this column across
MIN_GAP = 7
CUTS = ('teammeatfont16.fnt', 'teammeatfont12.fnt', 'teammeatfont10.fnt')


def fitting_cut(lang, s, target, words):
    """The largest cut of the font whose own size the widest line will take.

    The reduction is chosen by PICKING A CUT, never by resampling one: the cuts
    are the reductions, baked by the people who drew the face. English and
    German and Russian take the 12; Spanish does not - TAMAÑO STICK against
    PEQUEÑO wants 192 points of 174 - so Spanish is set in the 10.
    """
    room = RIGHT - LEFT - MIN_GAP
    missed = 0
    # NEVER ABOVE THE 12 (the owner, 24.09.2026). When STICK TYPE became TYPE the
    # English and Spanish lines fitted the 16-cut, and the picker took it: the
    # words came out a size above the game's own rows, and the rows looked
    # squeezed together. The 12 is what every Latin and Cyrillic set had.
    cuts = CUTS[CUTS.index(s['max_cut']):] if s.get('max_cut') in CUTS else CUTS
    for cut in cuts:
        trial = dict(s, font=cut)
        worst = 0
        for ln in LINES:
            if ln.get('native_name') and lang not in MODE_NAME:
                name_w = native_MODE_width(lang)
            else:
                text = MODE_NAME[lang] if ln.get('native_name') else words[ln['key']]
                name_w = compose_like_native(text, None, trial, target)[0].size[0]
            widest = max(compose_like_native(words[key], None, trial, target,
                                             bold=False)[0].size[0]
                         for key, _, _ in ln['values'])
            worst = max(worst, name_w + widest)
        if worst <= room:
            return cut, missed
        missed = worst - room                 # by how much the larger cut missed
    return CUTS[-1], missed

LOW_H = 12                        # and the smallest we will ever set it


def fitting_height(lang, s, target, words, want):
    """The tallest letter the widest line of this language will take.

    The native labels stand about 16 high and that is what we ask for, but a
    long word in a long language can run past the torn edge of the sheet at that
    size - English STICK TYPE against FLOATING does, by nineteen points. So the
    words are laid out once at the height we want, the worst line is measured,
    and the height is brought down by exactly that ratio rather than by a guess.
    """
    room = RIGHT - LEFT - MIN_GAP
    worst = 0.0
    for ln in LINES:
        if ln.get('native_name') and lang not in MODE_NAME:
            name_w = native_MODE_width(lang)
        else:
            text = MODE_NAME[lang] if ln.get('native_name') else words[ln['key']]
            name_w = compose_like_native(text, want, s, target)[0].size[0]
        widest = 0
        for key, _, _ in ln['values']:
            im = compose_like_native(words[key], want, s, target,
                                     s.get('value_strength'), bold=False)[0]
            widest = max(widest, im.size[0])
        worst = max(worst, (name_w + widest) / float(room))
    if s.get('own') or worst <= 1.0:      # at the face's own size there is nothing to scale
        return want
    return max(LOW_H, int(want / worst))


def native_MODE_width(lang):
    """The cut-out MODE is not ours to resize, so it counts at its own width."""
    atlas = Image.open(os.path.join(R, lang, M, 'optionsextra.png')).convert('RGBA')
    im = native_MODE(atlas)
    return im.size[0] if im else 0


def menu(lang, show):
    words = WORDS[lang]
    s = SETTINGS[lang]
    name_h = value_h = s['height']
    menu_path = os.path.join(R, lang, M)
    atlas = Image.open(os.path.join(menu_path, 'optionsextra.png')).convert('RGBA')
    target = native_share(atlas)
    target_density = native_density(atlas)
    # We clear our own boxes entirely: the previous typesetting could have been
    # wider than the current one, and the tail of the old word would have been
    # left lying next to the new one.
    if not show:
        # Clearing by the NOMINAL box is not enough. _place writes with a crop
        # grown to the picture (`w = max(w, picture)`), so a taller or wider
        # typesetting reaches past the box - and the next run, clearing only the
        # box, leaves its bottom rows behind. In the Russian sheet that left two
        # rows of somebody else's ink under every value (394-395, 416-417,
        # 438-439, 460-461), and the owner spotted them (13.09.2026).
        #
        # The whole band below OURS_START is ours by construction: the game's own
        # optionsextra is 256 tall (menu-labels/optionsextra-original.png), and
        # the 512 was made by us to have room. So the band is cleared outright.
        # ...all but the RUMBLE row's word (tools/rumble_row.py, hotfix 1.75): it is
        # lettered there, not here, and the box is kept as it stands
        import rumble_row
        bx, by = rumble_row.LABEL_AT
        keep = atlas.crop((bx, by, bx + rumble_row.LABEL_ROOM[0], by + rumble_row.LABEL_ROOM[1]))
        atlas.paste(Image.new('RGBA', (atlas.width - OURS_LEFT, atlas.height - OURS_START),
                              (0, 0, 0, 0)), (OURS_LEFT, OURS_START))
        atlas.paste(keep, (bx, by))
    print('   native: halftones %.1f%%, density %.1f%%, glyph height %d'
          % (target, target_density, value_h))
    print('   font %s, thickening %s%s'
          % (s['font'],
             'the cut own weight' if s.get('own') else
             (s['strength'] if 'strength' in s else 'step %s' % s['step']),
             ', gaps' if s.get('gaps') else ''))
    anm = os.path.join(menu_path, 'optionsmenu.anm2')
    own_markup = os.path.exists(anm)
    if own_markup:
        tree = ET.parse(anm)
        anims = {a.get('Name'): a for a in tree.getroot().iter('Animation')}
    else:
        print('   the language has no markup of its own — it takes the English one, we lay the labels down anyway')
        tree, anims = None, {}

    if s.get('pick_cut'):
        cut, missed = fitting_cut(lang, s, target, words)
        if cut != s['font']:
            print('   %s is too wide for the line by %d, so the cut drops to %s'
                  % (s['font'], missed, cut))
        s = dict(s, font=cut)
    name_h = value_h = fitting_height(lang, s, target, words, name_h)
    print('   the letters stand %d high (the native labels 14..18, mean 16)%s'
          % (name_h, ', at the cut own size, nothing resampled' if s.get('own') else ''))

    for ln in LINES:
        if ln.get('native_name') and lang not in MODE_NAME:
            name_im = native_MODE(atlas)
            name_share = halftone_share(name_im) if name_im else 0
            caption = 'MODE (native cut-out)'
        else:
            text = MODE_NAME[lang] if ln.get('native_name') else words[ln['key']]
            name_im, name_share = compose_like_native(text, name_h, s, target)
            caption = text
        values = []
        for key, box, frames in ln['values']:
            text = words[key]
            ready, share = compose_like_native(text, value_h, s, target,
                                               s.get('value_strength'), bold=False)
            values.append((text, box, frames, ready, share))

        widest = max(r.size[0] for _, _, _, r, _ in values)
        gap = GAP
        total = name_im.size[0] + gap + widest
        room = RIGHT - LEFT
        if total > room:
            gap = max(MIN_GAP, gap - (total - room))
            total = name_im.size[0] + gap + widest
        nx = LEFT + max(0, (room - total) // 2)
        vx = nx + name_im.size[0] + gap

        print('   %-16s %3dx%-3d %5d..%-5d %4.1f%%'
              % (caption, name_im.size[0], name_im.size[1], nx, nx + name_im.size[0], name_share))
        for text, box, frames, ready, share in values:
            print('   %-16s %3dx%-3d %5d..%-5d %4.1f%%'
                  % ('   ' + text, ready.size[0], ready.size[1], vx, vx + ready.size[0], share))
        if show or not own_markup:
            continue
        anim = anims[ln['animation']]
        _grow(anim, '18', 1 + max(n for _, _, fr in ln['values'] for n in fr))
        _place(atlas, anim, '16', [0], ln['name_box'], name_im, nx)
        for text, box, frames, ready, share in values:
            _place(atlas, anim, '18', frames, box, ready, vx)

    # the sheet of values (layer 4 "OnOff") lies in a different file from our labels
    layer_sheets = {}
    if NATIVE_TRANSLATIONS.get(lang):
        p = os.path.join(menu_path, 'optionsmenu.png')
        if not os.path.exists(p):
            p = os.path.join(menu_path, 'OptionsMenu.png')
        layer_sheets['4'] = Image.open(p).convert('RGBA')
    translate_native(lang, atlas, anims, target, show, s, menu_path, layer_sheets)

    if show:
        return
    atlas.save(os.path.join(menu_path, 'optionsextra.png'))
    if own_markup:
        tree.write(anm, encoding='UTF-8', xml_declaration=True)


def bow(im, strength):
    w, h = im.size
    pad = int(abs(strength)) + 2
    out = Image.new('RGBA', (w, h + pad * 2), (0, 0, 0, 0))
    for x in range(w):
        t = (2.0 * x / max(1, w - 1)) - 1.0
        shift = int(round(strength * (1.0 - t * t)))
        strip = im.crop((x, 0, x + 1, h))
        out.alpha_composite(strip, (x, max(0, pad - shift)))
    bb = out.getchannel('A').getbbox()
    return out.crop(bb) if bb else out


TITLES = os.path.join(ROOT, 'menu-labels', 'titles')
# Clean sheets: the owner removed the native phrase from them entirely. Cleaning
# them ourselves would not do — `PRESS START` begins ABOVE row 499 (in Japanese
# from 490, in German from 496), and we cleaned from 499, so the tops of the old
# letters were left hanging above ours.
# The English sheet does for five languages: in Russian, German, Spanish,
# Korean and Chinese `titlemenu.png` differs from it only in the phrase strip.
# In Japanese the splash above differs as well, so its sheet is its own.
CLEAN = {'dlc3.jp': 'clean-japanese.png'}
CLEAN_DEFAULT = 'clean-english.png'
TITLE_TONE = 16.0                 # light tone share in the native titles: eng 15.3, jap 17.1
#
# THE TITLE IS SET AT THE FONT'S OWN SIZE AND IS NEVER RESAMPLED.
#
# It used to be set in teammeatfont16 - letters 20 tall - and fitted down to 15,
# then turned by -5.3 degrees. Both steps resample, and the second one resamples
# the mush left by the first, so the threshold that follows had nothing sound to
# cut: every weight came out either fat or gnawed. Worse, THE GAPS BETWEEN THE
# LETTERS ARE DRAWN ONE OR TWO PIXELS WIDE - a fit of 0.72 leaves less than a
# pixel of them, and the letters run together. Counted on the sheet that shipped:
# the Russian title held 2 connected pieces of ink for 15 letters, the Spanish 3
# for 13 (14.09.2026).
#
# teammeatfont12 draws the line at 153 wide against a cut of 156, so nothing has
# to be squeezed at all. The slope and the bow are laid by whole letters through
# `lean`/`curve`, which is what _assemble is for; turning the raster instead
# leaves one half of a letter a pixel above the other. The weight is made up by
# ONE STEP DOWNWARDS - growth downwards cannot close a gap between neighbours,
# that gap being sideways - and it lands the light tone at 13..17%, which is the
# band the native titles wear.
TITLE_FONT = 'teammeatfont12.fnt'
TITLE_BOLD = [(0, 1)]
TITLE_LEAN = -math.tan(math.radians(abs(TITLE_TILT)))   # the far end goes down
TITLE_CURVE = 1.0                                       # and the middle sags by a point
#
# THE IDEOGRAPHS HAVE THE OPPOSITE TROUBLE. The widest face we have draws the
# Chinese phrase 75 across where the screen wants 107, so it is ENLARGED, not
# squeezed - and letters cannot run together on the way up, the gaps only widen.
# What suffered instead was the stroke: LANCZOS on the way up rings, the
# threshold after it cut into the ringing, and the thinnest stroke on the sheet
# that shipped came out ONE POINT across. Enlarged the true way - NEAREST up,
# an average of the points on the way down - the same stroke is three.
# The numbers are up, down, and the growth in SIXTHS of an original point.
TITLE_BIG = {'dlc3.zh': (12, 9, 1)}


def clean_sheet(lang):
    name = CLEAN.get(lang, CLEAN_DEFAULT)
    return Image.open(os.path.join(TITLES, name)).convert('RGBA')


def place_title(lang, phrase):
    sheet = clean_sheet(lang)
    for x0 in TITLE_COPIES:
        shift = (TITLE_CUT - phrase.size[0]) // 2
        sheet.alpha_composite(phrase, (max(0, x0 + shift), TITLE_TOP))
    sheet.save(os.path.join(R, lang, M, 'titlemenu_ios.png'))


def japanese_title(show):
    """For the Japanese the developers drew the touch phrase — we carry it over.

    It lies in the rebirth set (`rebirth-resources/data.jp`), and the English
    one from repentance covers it. The owner cut it out by hand; my own cutting
    by columns caught the scrawl above the line. The anti-aliasing in it is
    native — 83% ink and 17% light tone.
    """
    phrase = Image.open(os.path.join(TITLES, 'phrase-japanese.png')).convert('RGBA')
    print('   japanese title: ready phrase %dx%d' % phrase.size)
    if show:
        return
    place_title('dlc3.jp', phrase)


def title(lang, show):
    if lang == 'dlc3.jp':
        japanese_title(show)
        return
    text = TITLE.get(lang)
    if not text:
        print('   we do not typeset a title for this language')
        return
    ts = TITLE_SET.get(lang, {})
    if lang in TITLE_BIG:
        up, down, sixths = TITLE_BIG[lang]
        phrase = typeset.compose(text, None, lang=lang, jitter=JITTER, font=FONT[lang],
                                 thicken=False, lean=TITLE_LEAN, curve=TITLE_CURVE)
        phrase = typeset.enlarged(phrase, up, down, int(round(sixths * up / 6.0)))
    elif ts:
        # The Korean comes from a TTF, which has no own size to be set at, so it
        # keeps the old path until it is measured in its own right.
        height = ts.get('height', TITLE_H)
        font, folder = ts.get('font', FONT[lang]), ts.get('folder')
        kw = {'strength': ts['strength']} if 'strength' in ts else {}
        phrase = typeset.compose(text, height, lang=lang, jitter=JITTER, font=font,
                                 folder=folder, **kw)
        phrase = bow(phrase, TITLE_BOW)
        phrase = phrase.rotate(TITLE_TILT, resample=Image.BICUBIC, expand=True)
    else:
        phrase = typeset.compose(text, None, lang=lang, jitter=JITTER, font=TITLE_FONT,
                                 shifts=TITLE_BOLD, lean=TITLE_LEAN, curve=TITLE_CURVE)
    # Anti-aliasing with a second tone, not with a hard threshold: the native
    # titles are anti-aliased in exactly that way, and a measurement of the
    # transparency on them shows zero.
    phrase, share = two_tone(phrase, TITLE_TONE)
    phrase = without_specks(phrase)
    bb = phrase.getchannel('A').getbbox()
    phrase = phrase.crop(bb)
    note = 'fits' if phrase.size[0] <= TITLE_CUT else 'WIDER by %d' % (phrase.size[0] - TITLE_CUT)
    print('   title "%s"  %dx%d  light tone %.1f%%  %s'
          % (text, phrase.size[0], phrase.size[1], share, note))
    if show:
        return
    place_title(lang, phrase)


def main():
    # the Windows console has an encoding of its own, and our words are in six
    # languages
    try:
        sys.stdout.reconfigure(encoding='utf-8', errors='replace')
    except Exception:
        pass
    if len(sys.argv) < 2:
        raise SystemExit(__doc__)
    lang = sys.argv[1]
    show = '--show' in sys.argv
    print('=== %s ===' % lang)
    if '--only-title' not in sys.argv:
        menu(lang, show)
    if '--only-menu' not in sys.argv:
        title(lang, show)
    print('   %s' % ('show only, nothing touched' if show else 'written'))


if __name__ == '__main__':
    main()
