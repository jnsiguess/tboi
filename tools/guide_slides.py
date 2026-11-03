# -*- coding: utf-8 -*-
"""The in-game controls guide: its slides, built (the owner's item 22, the look
approved 25.09.2026 from tools/guide_preview.py).

A slide is the game's collection screen: the pinned paper with a picture and
the yellow note under it, three lines of teammeatfont10 at most. The arrows
stand at the paper's sides in the middle of its height (the owner).

TWO TEXELS TO A UNIT. The touch controls' pictures are twice the size they are
drawn at: the game takes ios_buttons.png, the _2x card and pill sheets and the
large bomb at a half (sticks.c: the 128-texel ring is 64 units at 100%). The
first slides drew them at a texel a unit - twice too big - and a D-pad at that
size did not fit, so it was drawn from its pieces and looked nothing like the
game's (the owner, 25.09.2026). So a slide is built at two texels a unit: the
paper, the notes, the words and the 1x pictures (items, the minimap, the
editor's buttons) are doubled pixel for pixel, the 2x pictures go in as they
are, and the .anm2 draws the sheet at 50 - exactly what the game does with its
own touch pictures.

EVERY LANGUAGE OF THE MENUS (approved in English, then translated - the owner,
25.09.2026). The words are tools/guide_text.py; each set is written in the
game's own fonts for that language - the Team Meat cuts for the Latin and
Cyrillic sets, mplus for Japanese, kr_font for Korean, the extended Team Meat
cuts for Chinese - and its titles as our other pages letter them
(tweaks_page.word). Every slide is checked before it is written: no two words
overlapping, nothing past the arrows, no letter the font has not got.

Out, per set suffix (en ru de es jp kr zh):
    assets/port_guide_<suffix>.png    every slide's paper and note, the two arrows
    assets/port_guide_<suffix>.anm2   Wall, Paper (a frame a slide), Note, Next, Back
    cpp/guide_layout.h                the count and where everything stands (units)

THE 20TH ROUND (05.10.2026): two Tweaks slides, the ITEM SPAWN strap without its
straight tail, two slides of the port's own mods and two of the player's - 19
slides; the Tweaks and mods slides are tables (table), a menu row's name and a
plain explanation beside it.

    py -3 tools/guide_slides.py                # the assets
    py -3 tools/guide_slides.py --show [set]   # other stuff/guide/slides-<suffix>.png
    py -3 tools/guide_slides.py --show-spawner [set]
                                               # other stuff/guide-spawner/spawner-<suffix>.png,
                                               # the two ITEM SPAWN slides doubled NEAREST
    py -3 tools/guide_slides.py --show-20 [set]   # other stuff/guide-20-preview/<suffix>-x2.png, -x3.png:
                                               # the slides of the 20th round (Tweaks, spawner, mods)
"""
import os
import sys

import numpy as np
from PIL import Image

import guide_preview as gp
import usermods_preview as up
import dpad_art
import guide_text
import lang
import spawner_stats
import spawner_tabs
import tweaks_page as tp
import touchedit_desk

SET_ORDER = ['en', 'ru', 'de', 'es', 'jp', 'kr', 'zh']   # the editor's sets (editor_tools_sheet.SETS)

ROOT = gp.ROOT
ASSETS = os.path.join(ROOT, 'port', 'app', 'src', 'main', 'assets')
HEADER = os.path.join(ROOT, 'port', 'app', 'src', 'main', 'cpp', 'guide_layout.h')
GFX = os.path.join(ROOT, 'work', 'app', 'repentance-resources', 'data', 'gfx')
REBIRTH_UI = os.path.join(ROOT, 'work', 'app', 'rebirth-resources', 'data', 'gfx', 'ui')
K = 2                                   # texels to a unit

FIELD = (37, 33, 32, 255)               # the menu world's dark, where the editor opens
TOOLS = os.path.join(ASSETS, 'port_touch_tools.png')
# the editor's buttons, Grid Reset Cancel Done, a row per set (port_touch_editor.anm2 Grid0..Done6)
TOOL_ROWS = {
    'en': [(6, 0, 44, 34), (63, 0, 44, 34), (120, 0, 44, 34), (177, 0, 44, 34)],
    'ru': [(6, 34, 44, 34), (63, 34, 44, 34), (120, 34, 44, 34), (177, 34, 44, 34)],
    'de': [(6, 68, 44, 34), (63, 68, 44, 34), (120, 68, 44, 34), (177, 68, 44, 34)],
    'es': [(2, 102, 52, 34), (59, 102, 52, 34), (116, 102, 52, 34), (173, 102, 52, 34)],
    'jp': [(0, 136, 57, 34), (57, 136, 57, 34), (114, 136, 57, 34), (171, 136, 57, 34)],
    'kr': [(6, 170, 44, 34), (63, 170, 44, 34), (120, 170, 44, 34), (177, 170, 44, 34)],
    'zh': [(6, 204, 44, 34), (63, 204, 44, 34), (120, 204, 44, 34), (177, 204, 44, 34)],
}
LOCAL = os.path.join(ROOT, 'work', 'app', 'repentance-resources')
# (label cut, small cut) per set; None: the Team Meat cuts every Latin set uses
FACES = {
    'dlc3.jp': (os.path.join(LOCAL, 'dlc3.jp', 'font', 'mplus_12b.fnt'),
                os.path.join(LOCAL, 'dlc3.jp', 'font', 'mplus_10r.fnt')),
    'dlc3.kr': (os.path.join(LOCAL, 'dlc3.kr', 'font', 'kr_font14.fnt'),
                os.path.join(LOCAL, 'dlc3.kr', 'font', 'kr_font12.fnt')),
    'dlc3.zh': (os.path.join(LOCAL, 'dlc3.zh', 'font', 'teammeatfontextended16bold.fnt'),
                os.path.join(LOCAL, 'dlc3.zh', 'font', 'teammeatfontextended10.fnt')),
}
UNSPACED = ('dlc3.jp', 'dlc3.zh')          # break anywhere between the characters
NO_LINE_START = set('、。，．！？）」』：；,.!?):;ーッャュョァィゥェォっゃゅょぁぃぅぇぉ')


def load(*parts):
    return Image.open(os.path.join(*parts)).convert('RGBA')


def doubled(im):
    return im.resize((im.width * K, im.height * K), Image.NEAREST)


def ghost(im, alpha):
    out = im.copy()
    out.putalpha(out.getchannel('A').point(lambda v: v * alpha // 255))
    return out


def white(im):
    out = Image.new('RGBA', im.size, (255, 255, 255, 255))
    out.putalpha(im.getchannel('A'))
    return out


# ------------------------------------------------------------ a pad's buttons, drawn
# THE PAD'S BUTTONS AS PICTURES (the owner, 06.10.2026: SELECT and START mean
# nothing to a player whose pad says View and Menu, Share and Options, - and +).
# The pictures are the game's own gfx/ui/buttons.png, the XboxOne animation of
# gfx/ui/buttons.anm2 - the very frames the EID mod draws for {{ButtonA}},
# {{ButtonSelect}}, {{ButtonLStick}} ... when a pad plays (eid_data.lua
# getControllerSprite; its own eid_inline_icons sheet has the keyboard's keys
# only). 1x pictures, a unit a texel, doubled with the rest of the paper like
# the items. In the words of tools/guide_text.py a button is written {NAME}:
# a legend's key or a note may carry it, and it stands in the line, its middle
# on the middle of the capitals.
BUTTONS = os.path.join(GFX, 'ui', 'buttons.png')
PAD_ICON_CELLS = {                              # buttons.anm2 XboxOne: frame -> its 32 x 16 cell
    'Y': (64, 0), 'A': (64, 16), 'B': (64, 32), 'X': (64, 48),               # frames 0..3
    'RIGHT': (64, 96), 'LEFT': (64, 112),                                    # 6, 7 (the D-pad's)
    'LS': (96, 0), 'RS': (96, 16),                                           # 8, 9 (the sticks pressed: L3, R3)
    'LB': (96, 32), 'RB': (96, 48), 'LT': (96, 64), 'RT': (96, 80),          # 10..13 (L1 R1 L2 R2)
    'VIEW': (96, 96), 'MENU': (96, 112),                                     # 14, 15 (Select, Start)
}
ICON_AIR = 1                                    # units either side of a button in a line
_ICONS = {}

# THE PAD'S OWN PICTURES (the owner, 1.72): the slides with a button on them are
# baked once per family of pads, and the guide shows the family of the pad the
# last button press came from (pad.c shim_pad_icon_family; Java PadIcons). The
# pictures mean "press this button", so a role is mapped BY POSITION: {A} is the
# bottom face button of every pad - a cross on a PlayStation pad, B on a Switch
# one - {VIEW} the left middle button (Share, -), {MENU} the right (Options, +).
# Every family is an animation of the same buttons.anm2, read off the file: the
# frame of a role is the same in all of them (0 top, 1 bottom, 2 right, 3 left,
# 6 7 the D-pad's right and left, 8 9 the sticks pressed, 10..13 the shoulders),
# but for the middle pair: in XboxOne 14 is View and 15 Menu, in the others 14
# is Start (Options, +) and 15 Select (Share, -). xbox is the main sheet, the
# others a sheet of the slides that carry a button (FAMILY_SLIDES).
FAMILIES = ['xbox', 'ps', 'switch']             # the order is Java's PadIcons and the header's
FAMILY_ANIM = {'xbox': 'XboxOne', 'ps': 'PS4', 'switch': 'Switch_Pro'}
BUTTONS_ANM2 = os.path.join(GFX, 'ui', 'buttons.anm2')
ROLE_FRAME = {'Y': 0, 'A': 1, 'B': 2, 'X': 3, 'RIGHT': 6, 'LEFT': 7, 'LS': 8, 'RS': 9,
              'LB': 10, 'RB': 11, 'LT': 12, 'RT': 13}
MIDDLE_FRAMES = {'XboxOne': (14, 15)}           # (VIEW, MENU); every other animation: (15, 14)
FAMILY = ['xbox']                               # the family being drawn (set_family)
DRAWN = [0]                                     # buttons drawn since the last reset: a slide that has one


def anim_cells(anim):
    """an animation of buttons.anm2: its frames' (x, y) crops, in order"""
    import re
    text = open(BUTTONS_ANM2, encoding='utf-8', errors='replace').read()
    m = re.search(r'<Animation Name="%s".*?</Animation>' % re.escape(anim), text, re.S)
    if not m:
        raise SystemExit('buttons.anm2 has no animation %s' % anim)
    crops = re.findall(r'XCrop="(\d+)" YCrop="(\d+)" Width="(\d+)" Height="(\d+)"', m.group(0))
    if any((w, h) != ('32', '16') for _, _, w, h in crops):
        raise SystemExit('buttons.anm2 %s: a frame is not 32 x 16' % anim)
    return [(int(x), int(y)) for x, y, _, _ in crops]


def family_cells(family):
    """a family's role -> its 32 x 16 cell of buttons.png"""
    anim = FAMILY_ANIM[family]
    frames = anim_cells(anim)
    view, menu = MIDDLE_FRAMES.get(anim, (15, 14))
    roles = dict(ROLE_FRAME, VIEW=view, MENU=menu)
    return {name: frames[k] for name, k in roles.items() if name in PAD_ICON_CELLS}


def set_family(family):
    """draw the pad's buttons from this family from now on"""
    cells = family_cells(family)
    if family == 'xbox' and cells != PAD_ICON_CELLS:          # the pictures the owner approved, read off the file
        raise SystemExit('buttons.anm2 XboxOne is not the approved cells: %s' % cells)
    if sorted(cells) != sorted(PAD_ICON_CELLS):
        raise SystemExit('%s has no picture for %s' % (family, sorted(set(PAD_ICON_CELLS) - set(cells))))
    FAMILY[0] = family
    _CELLS.clear()
    _CELLS.update(cells)
    _ICONS.clear()


_CELLS = dict(PAD_ICON_CELLS)


def pad_icon(name):
    """a button's picture, cropped to its ink"""
    if name not in PAD_ICON_CELLS:
        raise SystemExit('no pad button {%s} (there are %s)' % (name, ' '.join(sorted(PAD_ICON_CELLS))))
    DRAWN[0] += 1
    if name not in _ICONS:
        x, y = _CELLS[name]
        cell = load(BUTTONS).crop((x, y, x + 32, y + 16))
        _ICONS[name] = cell.crop(cell.getchannel('A').getbbox())
    return _ICONS[name]


def units(text):
    """a line's pieces: each character, and each {NAME} whole"""
    out, i = [], 0
    while i < len(text):
        if text[i] == '{':
            j = text.find('}', i)
            if j < 0:
                raise SystemExit('an unclosed { in %r' % text)
            out.append(text[i:j + 1])
            i = j + 1
        else:
            out.append(text[i])
            i += 1
    return out


def is_icon(unit):
    return len(unit) > 2 and unit[0] == '{' and unit[-1] == '}'


def without_icons(text):
    """the letters alone, for the font's check"""
    return ''.join(u for u in units(text) if not is_icon(u))


def pad_keys(shoot_key):
    """GAMEPAD & KEYBOARD: Select, R3, L3, the four shoulders as pictures; the keyboard's keys in words,
    the last R held for a restart, as on the PC (reset.c, 08.10.2026)"""
    return ['{VIEW}', '{RS}', '{LS}', '{LB} {RB} {LT} {RT}', 'TAB', shoot_key, 'ESC', 'R']


def spawner_keys(suffix):
    """SPAWNER CONTROLS: the card's own words, the shoulders as pictures, the keyboard's keys"""
    return ['< %s >' % (spawner_stats.PLAYER[suffix] % 1), '<  >', '3.50', spawner_stats.RESET[suffix],
            '{LB} / {RB}', 'TAB', 'ENTER / ESC']


PAD = 4                                         # texels round a line drawn whole
# the lowest a word may reach: the paper's torn foot is at 170..173 and the
# check let words down to 182 - DIGITAL's second line hung under the paper
# (the owner, 25.09.2026)
PAPER_FOOT = 167


class Checked(object):
    """a font that refuses a letter it has not got, rather than leave a gap"""
    def __init__(self, path):
        self.font = up.BMFont(path)
        self.line = self.font.line
        self.name = os.path.basename(path)
        # a cut drawn white with a dark rim (mplus_10r: for the game's dark panels):
        # the ink is the white alone, or the rim fills every stroke in
        for k, page in enumerate(self.font.pages):
            a = np.asarray(page).astype(int)
            lit = a[..., :3].min(axis=2)
            if (lit[a[..., 3] > 0] < 200).mean() > 0.05:
                a[..., 3] = a[..., 3] * lit // 255
                self.font.pages[k] = Image.fromarray(a.astype('uint8'))

    def draw(self, text):
        if '{' in text:                          # a pad button in it: drawn as a line, cropped to its ink
            im, _ = self.line_of(text)
            return im.crop(im.getchannel('A').getbbox() or (0, 0, 1, 1))
        self.refuse_missing(text)
        return self.font.draw(text)

    def refuse_missing(self, text):
        text = without_icons(text)
        missing = sorted({ch for ch in text if ch != ' ' and ord(ch) not in self.font.chars})
        if missing:
            raise SystemExit('%s has no %s (in %r)' % (self.name, ''.join(missing), text))

    def caps_middle(self):
        """the middle of a capital's ink below the line's top: where a button's middle stands"""
        g = self.font.chars.get(ord('H')) or self.font.chars.get(ord('A'))
        return g[5] + g[3] / 2.0

    def line_of(self, text):
        """the line NOT cropped to its ink - every letter on the font's own baseline -
        and where each piece's advance begins and ends (PAD texels in): a piece is a
        character, or a pad button {NAME} drawn as its picture (pad_icon)"""
        self.refuse_missing(text)
        f = self.font
        pieces = units(text)
        spans, pen = [], PAD
        for u in pieces:
            if is_icon(u):
                step = pad_icon(u[1:-1]).width + 2 * ICON_AIR
            else:
                g = f.chars.get(ord(u))
                step = g[6] if g else f.line // 3
            spans.append((pen, pen + step))
            pen += step
        im = Image.new('RGBA', (pen + 2 * PAD, f.line + 2 * PAD), (0, 0, 0, 0))
        for u, (x0, _) in zip(pieces, spans):
            if is_icon(u):
                icon = pad_icon(u[1:-1])
                top = max(0, PAD + int(round(self.caps_middle() - icon.height / 2.0)))
                if top + icon.height > im.height:
                    raise SystemExit('{%s} is taller than a line of %s' % (u[1:-1], self.name))
                im.alpha_composite(icon, (x0 + ICON_AIR, top))
                continue
            g = f.chars.get(ord(u))
            if not g:
                continue
            x, y, gw, gh, xo, yo, xa, page = g
            if gw and gh:
                glyph = f.pages[page].crop((x, y, x + gw, y + gh))
                ink = Image.new('RGBA', glyph.size, up.INK + (255,))
                ink.putalpha(glyph.getchannel('A'))
                im.alpha_composite(ink, (x0 + xo, PAD + yo))
        return im, spans


class Pen(object):
    """the pictures and one set's fonts; `fine` pictures are 2x, the rest 1x"""
    def __init__(self, set_name='data'):
        self.set_name = set_name
        self.suffix = SUFFIX[set_name]
        self.text = guide_text.TEXT[set_name]
        self.touch = load(gp.TOUCH)
        if set_name in FACES:
            self.label, self.small = (Checked(f) for f in FACES[set_name])
            self.big = None
        else:
            self.label = Checked(os.path.join(up.FONT, 'teammeatfont16bold.fnt'))
            self.small = Checked(os.path.join(up.FONT, 'teammeatfont10.fnt'))
            self.big = Checked(os.path.join(up.FONT, 'teammeatfont20bold.fnt'))
        self.target = lang.native_share(load(tp.RES, set_name, tp.MENU, 'optionsextra.png'))
        # a note line step: the Korean cut stands 12 tall, and at 13 its lines touched
        self.note_step = {'dlc3.kr': 14, 'dlc3.jp': 13, 'dlc3.zh': 13}.get(set_name, 12)

    def option_word(self, text):
        """a value of our Options rows, lettered as those rows letter it (lang, tweaks_page.word)"""
        return tp.word(self.set_name, text, self.target, False)

    def title(self, text):
        """the slide's heading: the 20bold cut, or the set's own lettering where it has no such cut"""
        if self.big is not None:
            return self.big.draw(text)
        return tp.word(self.set_name, text, self.target, True)

    def wrap(self, text, width, font=None):
        font = font or self.small
        if self.set_name not in UNSPACED:
            return gp.wrap(font, text, width)
        lines, line = [], ''
        for ch in units(text):                   # a pad button {NAME} is never broken
            if line and font.draw(line + ch).width > width and ch not in NO_LINE_START:
                lines.append(line)
                line = ch.lstrip()
            else:
                line += ch
        return lines + [line] if line else lines

    def touch_sprite(self, name):                # 2x
        return gp.sprite(self.touch, name)

    def stick(self, knob):                       # 2x: the ring and its knob, as the run draws them
        ring = gp.sprite(self.touch, 'bounds').copy()
        k = gp.sprite(self.touch, knob)
        ring.alpha_composite(k, ((ring.width - k.width) // 2, (ring.height - k.height) // 2))
        return ring

    def dpad(self):                              # 2x: the game's own, tools/dpad_art.py
        a = dpad_art.pad_picture(dpad_art.load(), dpad_art.MOVE)
        im = Image.fromarray(np.clip(a, 0, 255).astype('uint8'))
        return im.crop(im.getchannel('A').getbbox())

    def bomb(self):                              # 2x: gfx/ui/largebomb.png, ui_touchbomb.anm2
        return load(REBIRTH_UI, 'largebomb.png').crop((0, 0, 64, 64))

    def pill(self):                              # 2x
        return load(REBIRTH_UI, 'touch', 'ui_cardspills_2x.png').crop((0, 0, 64, 64))

    def card(self):                              # 2x: 32 x 48 at 1 in the slot's target
        return load(REBIRTH_UI, 'touch', 'ui_cardfronts_2x.png').crop((0, 0, 32, 48))

    def item(self, leaf):                        # 1x
        return load(GFX, 'items', 'collectibles', leaf).crop((0, 0, 32, 32))

    def trinket(self, leaf):                     # 1x
        return load(GFX, 'items', 'trinkets', leaf).crop((0, 0, 32, 32))


class Slide(object):
    """a paper and a note, drawn in UNITS; rendered at K texels a unit"""
    def __init__(self, sheet):
        self.paper = gp.clean_paper(sheet)
        self.note = taller(gp.crop(sheet, gp.NOTE))
        self.on_paper = []                       # (image, fine, left, top), left and top in units
        self.words = []                          # (text, its entry above), for the check

    def put(self, im, centre, fine=False):
        w, h = (im.width / K, im.height / K) if fine else (im.width, im.height)
        self.on_paper.append((im, fine, centre[0] - w / 2.0, centre[1] - h / 2.0))

    def at(self, im, left, top, fine=False):
        self.on_paper.append((im, fine, left, top))

    def text(self, font, words, centre):
        im = font.draw(words)
        self.put(im, centre)
        self.words.append((words, self.on_paper[-1]))

    def picture_word(self, im, words, centre):
        self.put(im, centre)
        self.words.append((words, self.on_paper[-1]))

    def word_at(self, font, words, left, top):
        im = font.draw(words)
        self.at(im, left, top)
        self.words.append((words, self.on_paper[-1]))

    def check(self, title):
        """no two words over each other, none past the arrows or off the paper"""
        boxes = []
        for words, (im, fine, x, y) in self.words:
            w, h = im.size
            box = (x, y, x + w, y + h)
            if x < 35 or x + w > 355 or y < 44 or y + h > PAPER_FOOT:
                raise SystemExit('%s: %r stands off the paper or over an arrow (%.0f..%.0f, %.0f..%.0f)'
                                 % (title, words, x, x + w, y, y + h))
            for other, ob in boxes:
                if box[0] < ob[2] - 1 and ob[0] < box[2] - 1 and box[1] < ob[3] - 1 and ob[1] < box[3] - 1:
                    raise SystemExit('%s: %r runs into %r' % (title, words, other))
            boxes.append((words, box))

    def render(self):
        paper = doubled(self.paper)
        for im, fine, x, y in self.on_paper:
            paper.alpha_composite(im if fine else doubled(im), (int(round(x * K)), int(round(y * K))))
        return paper, doubled(self.note)


# ------------------------------------------------------------ helpers
INSIDE = (36, 354)                              # between the two arrows, in units
AIR = 8                                         # the least room between two columns (the Russian ran together)


AIR4 = 6                                        # four columns: a little less, or the long names do not fit


def column_width(n):
    """how wide a column's words may run, n columns across the paper"""
    return int((INSIDE[1] - INSIDE[0] - (n + 1) * (AIR4 if n > 3 else AIR)) / n)


def column_lines(pen, lines, width):
    """a column's words, each broken to the column's width - a long word in German
    or Russian goes onto a second line instead of into its neighbour"""
    out = []
    for line in lines:
        out += pen.wrap(line, width)
    return out


def two_lines(s, pen, cx, name, words, y, width=100, option=False):
    """a name and what it means under it; an Options value is lettered as Options shows it"""
    if option:
        s.picture_word(pen.option_word(name), name, (cx, y))
    else:
        s.text(pen.label, name, (cx, y))
    for k, line in enumerate(column_lines(pen, [words], width)):
        s.text(pen.small, line, (cx, y + 18 + 13 * k))


def under(s, pen, cx, lines, first, width=74):
    for k, line in enumerate(column_lines(pen, lines, width)):
        s.text(pen.small, line, (cx, first + 13 * k))


def legend(s, pen, rows, x0, y0, key_w, step, inside=INSIDE):
    """keys right-aligned in a column as wide as the widest, what they do beside them;
    the whole block in the middle of the paper when x0 is None"""
    keys = [pen.small.draw(k) for k, _ in rows]
    texts = [pen.small.draw(t) for _, t in rows]
    key_w = max(key_w, max(k.width for k in keys))
    if x0 is None:
        total = key_w + 8 + max(t.width for t in texts)
        x0 = int((inside[0] + inside[1]) / 2.0 - total / 2)
    y = y0
    for (key, text), k, t in zip(rows, keys, texts):
        s.word_at(pen.small, key, x0 + key_w - k.width, y - k.height // 2)
        s.word_at(pen.small, text, x0 + key_w + 8, y - t.height // 2)
        y += step


def lines_of(entry):
    """a key or an explanation: one line, or several written out"""
    return [entry] if isinstance(entry, str) else list(entry)


def baseline_line(font, text):
    """a line cropped to its ink, and how far below the line's top the ink starts:
    lines set one under another, and a key beside its first line, then stand on one
    baseline whatever their letters (font.draw crops each to its ink alone, and a
    small-cap word would ride higher than one in capitals)"""
    im, _ = font.line_of(text)
    box = im.getchannel('A').getbbox()
    return im.crop(box), box[1] - PAD


KEY_MAX = 130                                   # the widest a row's name may stand on one line
TITLE_CLEAR = 50                                # the words start below the title


def table(s, pen, rows, top=TITLE_CLEAR, bottom=PAPER_FOOT, gap=5, inside=INSIDE):
    """THE TWEAKS AND MODS SLIDES (the owner, 05.10.2026: "people do not get it"):
    a row per menu row - its name as the menu writes it, right-aligned, and a
    plain explanation of one or two lines beside it. An explanation too wide for
    the paper is broken at its words (a long German or Russian one), never
    squeezed; a long name too (KEY_MAX). The rows' INK stands in the middle of
    the paper both ways, between the title and the paper's torn foot; when it
    does not fit, the air between the rows gives way first."""
    f = pen.small
    step = pen.note_step
    if s.on_paper:                              # the title, set first (build): its accents and descenders too
        im, _, _, y = s.on_paper[0]
        top = max(top, int(y + im.height + 4))
    keys = [column_lines(pen, lines_of(k), KEY_MAX) for k, _ in rows]
    key_w = max(f.draw(k).width for ks in keys for k in ks)
    room = inside[1] - inside[0] - 4 - key_w - 8
    texts = [column_lines(pen, lines_of(t), room) for _, t in rows]
    text_w = max(f.draw(t).width for ts in texts for t in ts)
    x0 = int((inside[0] + inside[1]) / 2.0 - (key_w + 8 + text_w) / 2.0)
    for air in range(gap, 0, -1):
        placed, y = [], 0
        for ks, ts in zip(keys, texts):
            for j, k in enumerate(ks):
                im, drop = baseline_line(f, k)
                placed.append((k, im, x0 + key_w - im.width, y + j * step + drop))
            for j, line in enumerate(ts):
                im, drop = baseline_line(f, line)
                placed.append((line, im, x0 + key_w + 8, y + j * step + drop))
            y += max(len(ks), len(ts)) * step + air
        ink_top = min(yy for _, _, _, yy in placed)
        ink_bottom = max(yy + im.height for _, im, _, yy in placed)
        if ink_bottom - ink_top <= bottom - top:
            break
    else:
        raise SystemExit('the rows take %d of %d units in height' % (ink_bottom - ink_top, bottom - top))
    shift = top + (bottom - top - (ink_bottom - ink_top)) // 2 - ink_top
    for words, im, x, y in placed:
        s.at(im, x, y + shift)
        s.words.append((words, s.on_paper[-1]))


def slot_with(pen, slot, icon, fine_icon):
    """a HUD slot with what it holds, at 2x"""
    out = pen.touch_sprite(slot).copy()
    pic = icon if fine_icon else doubled(icon)
    out.alpha_composite(pic, ((out.width - pic.width) // 2, (out.height - pic.height) // 2 + 4))
    return out


# ------------------------------------------------------------ the slides


def centres(widths, inside=INSIDE, air=None):
    """columns across the paper with equal air between them and at both ends, each as
    wide as its widest word - a long word in German or Russian takes its room
    instead of running into its neighbour"""
    room = inside[1] - inside[0] - sum(widths)
    gap = room / (len(widths) + 1.0)
    if gap < (air if air is not None else AIR):
        raise SystemExit('columns %s do not fit (%d over)' % (widths, -room))
    out, x = [], inside[0] + gap
    for w in widths:
        out.append(x + w / 2.0)
        x += w + gap
    return out


def widest(pen, words, font=None, least=0, width=None):
    """the widest of the words as they will stand: broken to `width` first, if given"""
    if width:
        words = column_lines(pen, words, width)
    return max([least] + [(font or pen.small).draw(w).width for w in words])


def word_of(pen, i):
    return lang.WORDS[pen.set_name][i]


def s_sticks(s, pen):
    for cx, knob, word in ((110, 'move', pen.text['move_shoot'][0]), (280, 'shoot', pen.text['move_shoot'][1])):
        s.put(pen.stick(knob), (cx, 92), fine=True)
        s.text(pen.label, word, (cx, 144))


def named_columns(pen, names, words, least):
    """three columns of a name and its meaning: the names lettered as Options letters
    them where the three fit side by side, in the small cut where they do not"""
    w = column_width(len(names))
    air = AIR4 if len(names) > 3 else AIR
    for option in (True, False):
        size = [(pen.option_word(n).width if option else pen.small.draw(n).width) for n in names]
        widths = [max(z, widest(pen, [t], width=w), least) for z, t in zip(size, words)]
        if sum(widths) + (len(names) + 1) * air <= INSIDE[1] - INSIDE[0]:
            return centres(widths, air=air), option, w
    raise SystemExit('the names %s do not fit even in the small cut' % names)


def name_and_meaning(s, pen, cx, name, words, y, option, width):
    if option:
        s.picture_word(pen.option_word(name), name, (cx, y))
    else:
        s.text(pen.small, name, (cx, y))
    for k, line in enumerate(column_lines(pen, [words], width)):
        s.text(pen.small, line, (cx, y + 16 + 13 * k))


def s_types(s, pen):
    # the knob alone: the rings round three of them only crowded the page (the owner)
    stick = pen.touch_sprite('move')
    d = pen.text['types']
    names = [word_of(pen, 2), word_of(pen, 3), word_of(pen, 9)]
    (a, b, c), option, w = named_columns(pen, names, d, 52)
    s.put(stick, (a, 92), fine=True)
    s.put(ghost(stick, 70), (b - 10, 88), fine=True)
    # no trail between the two: they stand so close that it hid under both knobs
    # but for two dots where they meet (the owner, 25.09.2026)
    s.put(stick, (b + 10, 94), fine=True)
    for k, alpha in enumerate((45, 80, 130)):
        s.put(ghost(stick, alpha), (c - 14 + k * 6, 86 + k * 3), fine=True)
    s.put(stick, (c + 6, 94), fine=True)
    for x, n, t in zip((a, b, c), names, d):
        name_and_meaning(s, pen, x, n, t, 124, option, w)


def s_modes(s, pen):
    d = pen.text['modes']
    names = [word_of(pen, 7), word_of(pen, 8), word_of(pen, 11)]
    (a, b, c), option, w = named_columns(pen, names, d, 64)
    s.put(pen.stick('move'), (a, 86), fine=True)
    s.put(pen.stick('move'), (b, 86), fine=True)
    s.put(pen.dpad(), (c, 86), fine=True)
    for x, n, t in zip((a, b, c), names, d):
        name_and_meaning(s, pen, x, n, t, 128, option, w)


def s_double_tap(s, pen):
    """THE DOUBLE TAP, BY CONTROL (the owner, 25.09.2026: "a slide of its own for
    the double-tap items like Mars, how it goes on each kind of control").
    sticks.c: a static stick takes two taps on the side meant (the dead zone
    hands the second to the game); a moving one - dynamic and floating alike -
    arms on a plain tap and takes the flick of the next touch (ARM_MS, the
    detector told "the last press was this way"); a D-pad's arrow is pressed
    twice as it is, the game's own detector. Laid out as the map's page: the
    item on the left, a row a kind of control - in four columns the names and
    their words ran together."""
    names = [word_of(pen, 2), word_of(pen, 3), word_of(pen, 9), word_of(pen, 11)]
    s.put(pen.item('collectibles_593_mars.png'), (62, 105))
    legend(s, pen, list(zip(names, pen.text['dtap'])), None, 70, 0, 22, inside=(92, 356))


def gesture_and_deed(line):
    """'TAP: PAUSE' -> ['TAP:', 'PAUSE']; a label without a colon stays whole"""
    for mark in (': ', '：', ':'):
        if mark in line:
            head, tail = line.split(mark, 1)
            return [head + mark.strip(), tail.strip()]
    return [line]


def s_buttons(s, pen):
    b = pen.text['buttons']
    cols = [(pen.touch_sprite('pause'), [b[0], b[1]]),
            (slot_with(pen, 'bomb', pen.bomb(), True), [b[2]]),
            (slot_with(pen, 'item', pen.item('collectibles_105_dice.png'), False), [b[3]]),
            (slot_with(pen, 'pill', pen.pill(), True), [b[4], b[5]]),
            # the trinket is a button of its own: held, it drops (the owner, 25.09.2026:
            # "it can be dropped with no pill or card") - on the HUD it is only the
            # trinket, no slot round it
            (doubled(pen.trinket('trinket_001_swallowedpenny.png')), [b[6]])]
    # five columns: each label is set as its gesture over what it does (TAP: over
    # PAUSE), every column alike - on one line they filled the paper edge to edge
    cols = [(pic, [part for line in lines for part in gesture_and_deed(line)]) for pic, lines in cols]
    w = column_width(len(cols))
    xs = centres([widest(pen, list(lines), least=34, width=w) for _, lines in cols], air=AIR4)
    for cx, (pic, lines) in zip(xs, cols):
        s.put(pic, (cx, 88), fine=True)
        under(s, pen, cx, lines, 114, width=w)


def s_special(s, pen):
    sp = pen.text['special']
    cols = [(pen.touch_sprite('twin'), sp[0]),
            (slot_with(pen, 'item', pen.item('collectibles_394_marked.png'), False), sp[1]),
            (slot_with(pen, 'item', pen.item('collectibles_534_schoolbag.png'), False), sp[2]),
            (slot_with(pen, 'item', pen.item('collectibles_489_dinfinity.png'), False), sp[3])]
    w = column_width(4)
    xs = centres([widest(pen, list(lines), least=34, width=w) for _, lines in cols], air=AIR4)
    for cx, (pic, lines) in zip(xs, cols):
        s.put(pic, (cx, 88), fine=True)
        under(s, pen, cx, list(lines), 114, width=w)


def minimap():
    """The map's page picture: a cross of rooms, the one we are in lit.

    The cells are the game's own, as minimap1.anm2 cuts them - RoomCurrent
    (27, 192) and RoomUnvisited (27, 224), 9 x 8 with the outline all round -
    laid a cell less its outline apart, so neighbours share one line as the
    game's map draws them. The first build cut them by eye from elsewhere on the
    sheet, and the lit cell came out a pixel short on the left (the owner,
    30.09.2026: "the centre cell is cut on the left")."""
    sheet = load(GFX, 'ui', 'minimap1.png')
    out = gp.crop(sheet, (0, 0, 54, 48)).copy()
    here, room = gp.crop(sheet, (27, 192, 9, 8)), gp.crop(sheet, (27, 224, 9, 8))
    cx, cy, sx, sy = 23, 20, 8, 7
    for dx, dy in ((-1, 0), (0, -1), (1, 0), (0, 1)):
        out.alpha_composite(room, (cx + dx * sx, cy + dy * sy))
    out.alpha_composite(here, (cx, cy))
    return out


def s_map(s, pen):
    s.put(minimap(), (62, 105))
    legend(s, pen, pen.text['map'], None, 70, 0, 22, inside=(92, 356))


def s_descriptions(s, pen):
    d = pen.text['desc']
    a, b = centres([widest(pen, d[0], width=150), widest(pen, d[1], width=150)])
    # the bag's column has four lines (when it shows, then its two gestures), so the
    # words start right under the pictures
    s.put(pen.item('collectibles_001_thesadonion.png'), (a, 84))
    under(s, pen, a, d[0], 108, width=150)
    s.put(pen.item('collectibles_710_bagofcrafting.png'), (b, 84))
    under(s, pen, b, d[1], 108, width=150)


def s_layout(s, pen):
    """the editor as it opens: the dark field with its grid and its middle line, its
    four buttons at the top (in this set's words), and what it moves at the size the
    run draws it - the sticks, the button card, the twins' round button, the pause
    button - and the OPACITY slider under the four buttons (touchedit.c, hotfix
    1.71). The field is the paper's, not the screen's shape; the things on it are
    their own size."""
    x0, y0, x1, y1 = 48, 52, 342, 166           # inside the paper: clear of the title, the arrows, the torn foot
    a = np.zeros(((y1 - y0) * K, (x1 - x0) * K, 4), dtype='uint8')
    a[:] = FIELD
    cell = 17 * K                                   # a sixteenth of the canvas height
    a[::cell, :, :3] = (a[::cell, :, :3] * 0.84).astype('uint8')
    a[:, ::cell, :3] = (a[:, ::cell, :3] * 0.84).astype('uint8')
    mid = a.shape[1] // 2
    a[:, mid - 1:mid + 1, :3] = (a[:, mid - 1:mid + 1, :3] * 0.65 + 255 * 0.35).astype('uint8')
    s.at(Image.fromarray(a), x0, y0, fine=True)
    tools = load(TOOLS)
    row = TOOL_ROWS[pen.suffix]
    step = row[0][2]
    left = (x0 + x1) / 2.0 - 2 * step
    for k, box in enumerate(row):
        s.at(gp.crop(tools, box), left + k * step, y0 + 2)
    s.put(ghost(pen.stick('move'), 230), (x0 + 38, y1 - 34), fine=True)
    s.put(ghost(pen.stick('shoot'), 230), (x1 - 38, y1 - 34), fine=True)
    card = [pen.bomb(), pen.pill(), pen.card(), doubled(pen.item('collectibles_105_dice.png'))]
    right = x1 - 38 - 32 - 6
    for k, pic in enumerate(card):
        s.put(pic, (right - 18 - k * 37, y1 - 24), fine=True)
    # the pause button is a piece as well since hotfix 1.71, top right where the game
    # puts it; the twins' button goes to the other corner to make room
    s.put(pen.touch_sprite('pause'), (x1 - 18, y0 + 18), fine=True)
    s.put(pen.touch_sprite('twin'), (x0 + 22, y0 + 20), fine=True)
    # and under the four buttons the OPACITY slider, as the editor draws it (touchedit_desk.py)
    bar = touchedit_desk.slider_image(SET_ORDER.index(pen.suffix), 50)
    s.at(bar, (x0 + x1) / 2.0 - bar.width / 2.0, y0 + 38)


PAD_Y0, PAD_STEP = 56, 15                       # GAMEPAD & KEYBOARD: the first row's middle, the step


def s_pad_keys(s, pen):
    # only what the game's own binding screen does not show; the pad's buttons drawn
    # (pad_keys), the keyboard's keys in words. Eight rows since R (08.10.2026): a
    # step of 16 from 60 put the eighth under the paper's foot, so the rows close up
    legend(s, pen, list(zip(pad_keys(pen.text['shoot_key']), pen.text['pad'])), None, PAD_Y0, 0, PAD_STEP)


def coop_choice():
    """the game's own character choice, as gfx/ui/coop menu.anm2 draws it: the
    Arrows animation's two arrows (crops 128,96 and 160,96 of coop menu.png,
    2 units out from the middle) either side of the Main layer's portrait
    (Isaac, crop 0,0); 1x, 32 units tall"""
    sheet = load(GFX, 'ui', 'coop menu.png')
    left, right = sheet.crop((128, 96, 160, 128)), sheet.crop((160, 96, 192, 128))
    head = sheet.crop((0, 0, 32, 32))
    out = Image.new('RGBA', (32 + 2 + 32 + 2 + 32, 32), (0, 0, 0, 0))
    out.alpha_composite(left, (0, 0))
    out.alpha_composite(head, (34, 0))
    out.alpha_composite(right, (68, 0))
    return out


def s_coop(s, pen):
    # the port's door into the game's co-op (pad.c, coop.c): the gesture on a
    # pad and on a keyboard, then the game's own choice, drawn as the HUD shows it
    # the keyboard's confirm and cancel too (the owner, 28.09.2026): Enter and Backspace are the pad's A and B
    # (MainActivity.keyToPad); its left and right are LEFT / RIGHT already, WASD being the left stick
    legend(s, pen, list(zip(pen.text['coop_keys'], pen.text['coop'])), None, 56, 0, 16)
    # twice its size: at the HUD's own one texel a unit the arrows are a few pixels
    s.put(doubled(coop_choice()), ((INSIDE[0] + INSIDE[1]) / 2.0, 152))


# RUMBLE left the Tweaks page for the game's own Options (hotfix 1.75: the row
# that was FILTER, loader.c rumble_menu_item), and the Options row shows the
# game's own lettering - so the slide names it the way that row reads in each
# set: the game's own English art in the English set, our drawn word in the
# German, Russian and Spanish ones (rumble_row.py OUR_WORD, port_optionsextra.png
# 128,264), the set's own word in the Asian ones
OPTIONS_RUMBLE = {'data': 'RUMBLE', 'dlc3.ru': 'ВИБРАЦИЯ', 'dlc3.de': 'VIBRATION', 'dlc3.es': 'VIBRACIÓN',
                  'dlc3.jp': 'ランブル', 'dlc3.kr': '진동', 'dlc3.zh': '手柄振动'}


def tweaks_keys(set_name):
    """the first Tweaks slide's keys: AIM LOCK, DEAD ZONE, MOVE and AIM SENSITIVITY,
    RENDER SCALE as the page names them (tweaks_page.SETS), then RUMBLE as Options does"""
    names = tp_names(set_name)
    return [names[4], names[5], names[6], names[7], names[2], OPTIONS_RUMBLE[set_name]]   # 3 is FPS COUNTER


def s_tweaks(s, pen):
    """the Tweaks page's control rows (tweaks.c R_AIMLOCK, R_DEAD, R_SENS, R_SENSSHOOT,
    R_SCALE), named as the page names them, and RUMBLE, which is in Options now; AIM
    LOCK has a second line, how to let go of it (aimlock_release - its own key, easy to
    reword)"""
    keys = tweaks_keys(pen.set_name)
    t = pen.text
    rows = [(keys[0], [t['tweaks'][0], t['aimlock_release']])]
    rows += list(zip(keys[1:], t['tweaks'][1:]))
    table(s, pen, rows)


def s_tweaks_more(s, pen):
    """the rest of the Tweaks page (the owner, 05.10.2026: "it does not describe every
    option"): ITEM SPAWN, SPAWNER TROPHIES, FPS COUNTER (21.10.2026), CONFIG FILE, IMPORT and
    EXPORT SAVES"""
    names = tp_names(pen.set_name)
    table(s, pen, list(zip([names[0], names[1], names[3], names[8], names[9], names[10]], pen.text['tweaks2'])))


def s_mods(s, pen, part):
    """the port's own mods on MODS (tweaks.c MOD_KEY: dance planet waves bars | marks
    stats rangefix eid), four a slide, by their names on the MODS page"""
    names = tp_names(pen.set_name)[len(tp.KINDS):len(tp.KINDS) + tp.MOD_ROWS]
    rows = list(zip(names, pen.text['mods']))
    table(s, pen, rows[4 * part:4 * part + 4], gap=7)


def s_mods_first(s, pen):
    s_mods(s, pen, 0)


def s_mods_second(s, pen):
    s_mods(s, pen, 1)


def s_your_mods(s, pen):
    """the player's own mods on MODS: + ADD MOD, the game's files out, a mod's own row
    (tweaks.c R_ADD, R_DUMP, R_USER: a tap switches it, its arrow opens its page)"""
    w = tp.USER_WORDS[pen.set_name]
    table(s, pen, list(zip([w[1], w[2], pen.text['user_key']], pen.text['user'])), gap=8)


def s_mod_page(s, pen):
    """a player's mod's page (tweaks.c R_U_ON, R_U_UP, R_U_DOWN, R_U_EXPORT, R_U_DEL,
    R_SHEAD, R_RESTART), its rows as the page words them"""
    w = tp.USER_WORDS[pen.set_name]
    move = '%s / %s' % (w[4], w[5])
    if pen.small.draw(move).width > KEY_MAX:          # broken at its slash, not inside a word pair
        move = ['%s /' % w[4], w[5]]
    keys = [w[3], move, w[6], w[7], w[10], w[9]]
    table(s, pen, list(zip(keys, pen.text['page'])))


PATCH_RES = os.path.join(ASSETS, 'port_patch', 'repentance-resources')


STRAP_RIM = (124, 103, 93)                       # the strap's outline, as the picture has it
STRAP_RIM2 = (128, 107, 97)                      # the outline's one lighter texel
STRAP_KINK = 19             # the column where the straight tail meets the diagonal piece
STRAP_AIR = 3               # paper between the new end's outline and the first letter (the right end has 3)


def without_tail(im, what):
    """THE STRAIGHT TAIL CUT OFF (the owner, 05.10.2026: on the slide the strap
    looked like a worm crawling out from under the paper - "keep only the
    diagonal piece"). On the pause paper the tail runs under the sheet; on the
    slide it hung in the air. The strap is cut at the kink, or further left where
    this set's letters reach past it (Spanish and Japanese do), keeping
    STRAP_AIR of paper before them; the cut gets the strap's own two-texel
    outline, from its top edge to its bottom edge, and the shadow under the
    tail goes with the tail. Only the slide's copy is cut: the pause paper's
    picture is untouched."""
    a = np.asarray(im).astype(int)
    opaque = a[..., 3] == 255
    rgb = a[..., :3]
    rim = (np.all(rgb == STRAP_RIM, axis=2) | np.all(rgb == STRAP_RIM2, axis=2)) & opaque
    letters = opaque & ~rim & (rgb.sum(axis=2) < 600)
    ys, xs = np.nonzero(letters)
    if not len(xs):
        raise SystemExit('%s: the strap has no letters to keep clear of' % what)
    cut = min(STRAP_KINK, int(xs.min()) - STRAP_AIR - 2)
    for x in (cut, cut + 1):
        # the cut must cross the strap: its own outline above and below, paper between
        col = np.nonzero(opaque[:, x])[0]
        if len(col) < 20 or not rim[col[0], x] or not rim[col[-1], x]:
            raise SystemExit('%s: column %d of the strap is not a clean cut across it' % (what, x))
    out = a[:, cut:].copy()
    for x in (0, 1):
        col = np.nonzero(out[:, x, 3] == 255)[0]
        out[col[0]:col[-1] + 1, x] = STRAP_RIM + (255,)
    # the shadow is the outline's, offset down and right: none left of the new end
    shade = out[:, :2, 3] < 255
    out[:, :2][shade] = 0
    return Image.fromarray(out.astype('uint8'))


def spawner_strap(set_name):
    """the pause paper's ITEM SPAWN strap in this set's words, 1x, as pausescreen.anm2
    draws it (PortStrap0..5 at 100, port_patch) - less its straight tail"""
    return without_tail(load(PATCH_RES, set_name, 'gfx', 'ui', 'port_strap_spawner.png'), set_name)


def s_spawner(s, pen):
    """ITEM SPAWN (the owner, 03.10.2026: "a slide in the GUIDE explaining how to use
    all of it"). Where it opens - the pause paper's strap, drawn as the pause draws
    it - and the card's four tabs, each lettered exactly as the card letters it
    (tools/spawner_tabs.py: tweaks_page.word), with what a tap does on it
    (spawner.c row_tapped: the first tap picks, a tap on the picked one acts; a
    room goes at once, a floor on the second tap; stats_tap)."""
    s.put(spawner_strap(pen.set_name), (76, 96))
    tabs = [pen.option_word(w) for w in spawner_tabs.TABS[pen.suffix]]
    texts = [pen.small.draw(t) for t in pen.text['spawn_tabs']]
    inside = (120, 356)
    key_w = max(t.width for t in tabs)
    total = key_w + 8 + max(t.width for t in texts)
    if total > inside[1] - inside[0]:
        raise SystemExit('the tab rows are %d wide, %d at most' % (total, inside[1] - inside[0]))
    x0 = int((inside[0] + inside[1]) / 2.0 - total / 2)
    y = 62
    for word, tab, line, t in zip(spawner_tabs.TABS[pen.suffix], tabs, pen.text['spawn_tabs'], texts):
        s.at(tab, x0 + key_w - tab.width, y - tab.height // 2)
        s.words.append((word, s.on_paper[-1]))
        s.word_at(pen.small, line, x0 + key_w + 8, y - t.height // 2)
        y += 22


def s_spawner_keys(s, pen):
    """the card's controls: its own words for the player line and RESET
    (spawner_stats.PLAYER, RESET - the card draws "< PLAYER 1: Isaac >"), the arrows
    and the typing of the STATS tab (stats_walk, stats_tap: a tap on the chosen
    value types), the tabs from a pad's shoulders (menu actions 0x1f/0x20) and from
    Tab / Shift+Tab, Enter and Escape (shim_spawner_key)"""
    legend(s, pen, list(zip(spawner_keys(pen.suffix), pen.text['spawn_rows'])), None, 60, 0, 16)


def tp_names(set_name):
    for entry in tp.SETS:
        if entry[0] == set_name:
            return entry[3]
    raise SystemExit('no Tweaks names for ' + set_name)


def tweaks_title(set_name):
    for entry in tp.SETS:
        if entry[0] == set_name:
            return entry[2]
    raise SystemExit('no Tweaks title for ' + set_name)


DRAW = [s_sticks, s_types, s_modes, s_double_tap, s_buttons, s_special, s_map, s_descriptions, s_layout, s_pad_keys,
        s_coop, s_tweaks, s_tweaks_more, s_spawner, s_spawner_keys, s_mods_first, s_mods_second, s_your_mods,
        s_mod_page]
SUFFIX = {entry[0]: entry[1] for entry in tp.SETS}
# the two ITEM SPAWN slides come after the second Tweaks slide, which turns it on
SPAWNER_FIRST = DRAW.index(s_spawner)
# the slides of the 20th round (05.10.2026): the two Tweaks slides, the spawner's
# (its strap cut), the two of the port's mods and the two of the player's
CHANGED_20 = [DRAW.index(d) for d in (s_tweaks, s_tweaks_more, s_spawner, s_mods_first, s_mods_second,
                                      s_your_mods, s_mod_page)]


def slides_of(set_name):
    t = guide_text.TEXT[set_name]
    # the first ITEM SPAWN slide is titled with the Tweaks row itself; the first
    # Tweaks slide with the page's title, the first mods slide with the main
    # menu's MODS, the player's mods with the list's own heading
    titles = t['titles'] + [t['coop_title'], tweaks_title(set_name), t['tweaks2_title'], tp_names(set_name)[0],
                            t['spawn_title'], tp.MODS_WORD[set_name], t['mods2_title'],
                            tp.USER_WORDS[set_name][0], t['page_title']]
    # the first Tweaks note: where the page is, then the one sentence on letting go of AIM LOCK
    tweaks_notes = [t['tweaks_notes'][0] + ' ' + t['aimlock_note']] + t['tweaks_notes'][1:]
    if set_name in UNSPACED:
        tweaks_notes[0] = t['tweaks_notes'][0] + t['aimlock_note']
    notes = t['notes'] + [t['coop_note']] + tweaks_notes + t['spawn_notes'] + t['mods_notes'] + t['user_notes']
    assert len(titles) == len(DRAW) == len(notes), (len(titles), len(DRAW), len(notes))
    return list(zip(titles, DRAW, notes))


def build(title, draw, note, pen, sheet):
    s = Slide(sheet)
    t = pen.title(title)
    if t.width > 360:
        raise SystemExit('%s: the title is %d wide' % (title, t.width))
    s.at(t, (s.paper.width - t.width) // 2 - 4, 22 + max(0, (26 - t.height) // 2))
    try:
        draw(s, pen)
    except SystemExit as e:
        raise SystemExit('%s %s: %s' % (pen.suffix, title, e))
    s.check('%s %s' % (pen.suffix, title))
    lines = pen.wrap(note, s.note.width - 44)            # clear of the torn right end
    if len(lines) > 3:                               # the note holds three lines
        raise SystemExit('%s %s: the note takes %d lines: %r' % (pen.suffix, title, len(lines), lines))
    lay_note(s.note, lines, pen, '%s %s' % (pen.suffix, title))
    return s.render()


NOTE_LEFT = 18
NOTE_MORE = 8                                    # rows the note is lengthened by
NOTE_BAND = 30                                   # from this row of its middle


def taller(note):
    """the note a few rows taller, its middle repeated - the way the pages are
    lengthened. At its own 68 the band between its torn edges is 38 texels and
    three lines of the small cut are 37: they touched the edges (the owner,
    25.09.2026). Its foot still ends above the bottom of the glass: 189 + 76."""
    out = Image.new('RGBA', (note.width, note.height + NOTE_MORE), (0, 0, 0, 0))
    out.alpha_composite(note.crop((0, 0, note.width, NOTE_BAND)), (0, 0))
    band = note.crop((0, NOTE_BAND, note.width, NOTE_BAND + 4))
    for k in range(0, NOTE_MORE, 4):
        out.alpha_composite(band, (0, NOTE_BAND + k))
    out.alpha_composite(note.crop((0, NOTE_BAND, note.width, note.height)), (0, NOTE_BAND + NOTE_MORE))
    return out
NOTE_MARGIN = 2                                  # clear of the torn edge, above and below


def note_edges(note):
    """for every column of the note, the first and the last row of its own paper"""
    a = np.asarray(note).astype(int)
    body = a[20:40, 60:280]
    tone = np.median(body[body[..., 3] == 255][:, :3], axis=0)
    paper = (np.abs(a[..., :3] - tone).sum(axis=2) < 40) & (a[..., 3] == 255)
    tops, bots = [], []
    for x in range(a.shape[1]):
        ys = np.nonzero(paper[:, x])[0]
        tops.append(int(ys.min()) if len(ys) else a.shape[0])
        bots.append(int(ys.max()) if len(ys) else -1)
    return tops, bots


def lay_note(note, lines, pen, what):
    """
    THE NOTE IS SLANTED: its torn top runs from 5 texels down at the left to 14
    at the right, its foot from 46 to 58. Lines laid level cut into the top at
    the right - worst with the Chinese and Japanese, whose marks stand tallest -
    and into the foot at the left (the owner, 25.09.2026). So the block follows
    the note: it is cut into pieces - words, or single characters where the
    language has no spaces - and each piece stands in the middle of the paper's
    own band at its place, all three lines together. Every piece's ink is then
    checked against the edges above and below it.
    """
    tops, bots = note_edges(note)
    drawn = [pen.small.line_of(line) for line in lines]
    step = pen.note_step
    # the block's ink, from the first line's highest mark to the last line's lowest
    ink_top, ink_bottom = None, None
    for k, (im, _) in enumerate(drawn):
        box = im.getchannel('A').getbbox()
        if not box:
            continue
        t, b = box[1] + k * step, box[3] + k * step
        ink_top = t if ink_top is None else min(ink_top, t)
        ink_bottom = b if ink_bottom is None else max(ink_bottom, b)
    height = ink_bottom - ink_top
    # the band's middle along the note, a straight line fitted through it
    xs = [x for x in range(NOTE_LEFT, note.width - NOTE_LEFT) if bots[x] >= 0]
    mids = [(tops[x] + bots[x]) / 2.0 for x in xs]
    slope, middle0 = np.polyfit(xs, mids, 1)
    spaced = pen.set_name not in UNSPACED
    for k, (im, spans) in enumerate(drawn):
        text = units(lines[k])                   # a pad button {NAME} is one piece of the line
        # the pieces: runs between spaces, or every character
        pieces = []
        if spaced:
            start = 0
            for i in range(len(text) + 1):
                if i == len(text) or text[i] == ' ':
                    if i > start:
                        pieces.append((spans[start][0], spans[i - 1][1]))
                    start = i + 1
        else:
            pieces = [sp for ch, sp in zip(text, spans) if ch != ' ']
        for x0, x1 in pieces:
            cut = im.crop((x0 - 1, 0, x1 + 1, im.height))
            box = cut.getchannel('A').getbbox()
            if not box:
                continue
            at_x = NOTE_LEFT + x0 - PAD - 1
            centre = middle0 + slope * (at_x + cut.width / 2.0)
            at_y = int(round(centre - height / 2.0)) - ink_top + k * step
            left, right = at_x + box[0], at_x + box[2]
            top_ink, bottom_ink = at_y + box[1], at_y + box[3]
            allowed_top = max(tops[max(0, left):right]) + NOTE_MARGIN
            allowed_bottom = min(bots[max(0, left):right]) - NOTE_MARGIN + 1
            if top_ink < allowed_top or bottom_ink > allowed_bottom or right > note.width - 12:
                raise SystemExit('%s: the note\'s %r stands off its paper (%d..%d against %d..%d)'
                                 % (what, text, top_ink, bottom_ink, allowed_top, allowed_bottom))
            note.alpha_composite(cut, (at_x, at_y))


def arrows(sheet):
    arrow = doubled(gp.crop(sheet, gp.ARROW))
    return arrow, arrow.transpose(Image.FLIP_LEFT_RIGHT)


def screen_of(paper, note, sheet, k, count):
    """a slide as the glass shows it, in texels (K a unit)"""
    screen = Image.new('RGBA', (480 * K, 270 * K), (48, 42, 40, 255))
    screen.alpha_composite(paper, (gp.PAPER_AT[0] * K, gp.PAPER_AT[1] * K))
    screen.alpha_composite(note, (gp.NOTE_AT[0] * K, gp.NOTE_AT[1] * K))
    nxt, back = arrows(sheet)
    if k + 1 < count:
        screen.alpha_composite(nxt, (gp.ARROW_NEXT_X * K, gp.ARROW_Y * K))
    if k > 0:
        screen.alpha_composite(back, (gp.ARROW_BACK_X * K, gp.ARROW_Y * K))
    return screen


FRAME = ('<Frame XPosition="0" YPosition="0" XPivot="0" YPivot="0" XCrop="{x}" YCrop="{y}" Width="{w}" '
         'Height="{h}" XScale="{s}" YScale="{s}" Delay="1" Visible="true" RedTint="255" GreenTint="255" '
         'BlueTint="255" AlphaTint="255" RedOffset="0" GreenOffset="0" BlueOffset="0" Rotation="0" '
         'Interpolated="false"/>')


def write_anm2(frames, suffix, sheets=None):
    """frames: {layer: (sheet id, scale, [(x, y, w, h), ...])}; sheets: their paths by id -
    the menu's OptionsMenu.png and port_guide_<suffix>.png when not given"""
    names = list(frames)
    T = '\t'
    if sheets is None:
        sheets = ['ui/main menu/OptionsMenu.png', 'port_guide_%s.png' % suffix]
    out = ['<AnimatedActor>',
           T + '<Info CreatedBy="Isaac Android port" CreatedOn="25/09/2026" Version="2" Fps="30"/>',
           T + '<Content>', T * 2 + '<Spritesheets>']
    out += [T * 3 + '<Spritesheet Path="%s" Id="%d"/>' % (path, i) for i, path in enumerate(sheets)]
    out += [T * 2 + '</Spritesheets>', T * 2 + '<Layers>']
    out += [T * 3 + '<Layer Name="%s" Id="%d" SpritesheetId="%d"/>' % (n, i, frames[n][0])
            for i, n in enumerate(names)]
    out += [T * 2 + '</Layers>', T * 2 + '<Nulls/>', T * 2 + '<Events/>', T + '</Content>',
            T + '<Animations DefaultAnimation="Wall">']
    for n in names:
        _, scale, crops = frames[n]
        out.append(T * 2 + '<Animation Name="%s" FrameNum="%d" Loop="false">' % (n, len(crops)))
        out += [T * 3 + '<RootAnimation>',
                T * 4 + '<Frame XPosition="0" YPosition="0" XScale="100" YScale="100" Delay="%d" Visible="true" '
                'RedTint="255" GreenTint="255" BlueTint="255" AlphaTint="255" RedOffset="0" GreenOffset="0" '
                'BlueOffset="0" Rotation="0" Interpolated="false"/>' % len(crops),
                T * 3 + '</RootAnimation>', T * 3 + '<LayerAnimations>']
        for j, other in enumerate(names):
            if other != n:
                out.append(T * 4 + '<LayerAnimation LayerId="%d" Visible="false"/>' % j)
                continue
            out.append(T * 4 + '<LayerAnimation LayerId="%d" Visible="true">' % j)
            out += [T * 5 + FRAME.format(x=x, y=y, w=w, h=h, s=scale) for x, y, w, h in crops]
            out.append(T * 4 + '</LayerAnimation>')
        out += [T * 3 + '</LayerAnimations>', T * 3 + '<NullAnimations/>', T * 3 + '<Triggers/>',
                T * 2 + '</Animation>']
    out += [T + '</Animations>', '</AnimatedActor>', '']
    open(os.path.join(ASSETS, 'port_guide_%s.anm2' % suffix), 'w', newline='\n').write('\n'.join(out))


SPAWNER_SHOW = os.path.join(ROOT, 'other stuff', 'guide-spawner')


def show_spawner(set_name, sheet, built):
    """the ITEM SPAWN slides as the glass shows them, side by side, then doubled
    NEAREST: other stuff/guide-spawner/spawner-<suffix>.png"""
    count = len(built)
    shots = [screen_of(p, n, sheet, k, count) for k, (p, n) in enumerate(built)
             if SPAWNER_FIRST <= k < SPAWNER_FIRST + 2]
    w, h = shots[0].size
    out = Image.new('RGBA', (len(shots) * (w + 10) + 10, h + 20), (20, 18, 17, 255))
    for i, shot in enumerate(shots):
        out.alpha_composite(shot, (10 + i * (w + 10), 10))
    out = out.resize((out.width * 2, out.height * 2), Image.NEAREST)
    if not os.path.isdir(SPAWNER_SHOW):
        os.makedirs(SPAWNER_SHOW)
    path = os.path.join(SPAWNER_SHOW, 'spawner-%s.png' % SUFFIX[set_name])
    out.save(path)
    print(path)


SHOW_20 = os.path.join(ROOT, 'other stuff', 'guide-20-preview')


def show_20(set_name, sheet, built):
    """the slides the 20th round changed, as the glass shows them, two a row:
    other stuff/guide-20-preview/<suffix>-x2.png at two pixels a unit (the
    sheet's own texels) and <suffix>-x3.png at three - the sheet's texels
    stretched 1.5 NEAREST, what a 3x screen makes of them"""
    count = len(built)
    shots = [screen_of(built[k][0], built[k][1], sheet, k, count) for k in CHANGED_20]
    w, h = shots[0].size
    cols = 2
    rows = (len(shots) + cols - 1) // cols
    out = Image.new('RGBA', (cols * (w + 10) + 10, rows * (h + 10) + 10), (20, 18, 17, 255))
    for i, shot in enumerate(shots):
        out.alpha_composite(shot, (10 + (i % cols) * (w + 10), 10 + (i // cols) * (h + 10)))
    if not os.path.isdir(SHOW_20):
        os.makedirs(SHOW_20)
    for scale, name in ((1.0, 'x2'), (1.5, 'x3')):
        im = out if scale == 1.0 else out.resize((int(out.width * scale), int(out.height * scale)), Image.NEAREST)
        path = os.path.join(SHOW_20, '%s-%s.png' % (SUFFIX[set_name], name))
        im.save(path)
        print(path)


ICON_SLIDES = {}                                # set -> the slides that carry a pad button


def build_set(set_name, sheet, only=None):
    """the slides of a set (those numbered in `only`, or all) in the family being
    drawn, and which of them carry a pad button (ICON_SLIDES, for a whole set)"""
    pen = Pen(set_name)
    built, icons = [], []
    for k, (t, d, n) in enumerate(slides_of(set_name)):
        if only is not None and k not in only:
            continue
        DRAWN[0] = 0
        built.append(build(t, d, n, pen, sheet))
        if DRAWN[0]:
            icons.append(k)
    if only is None:
        ICON_SLIDES[set_name] = icons
    return pen, built


def write_set(set_name, sheet, show):
    pen, built = build_set(set_name, sheet)
    count = len(built)
    if show == 'spawner':
        show_spawner(set_name, sheet, built)
        return count
    if show == '20':
        show_20(set_name, sheet, built)
        return count
    if show:
        cols = 2
        rows = (count + cols - 1) // cols
        cw, ch = 480 * K + 20, 270 * K + 20
        out = Image.new('RGBA', (cols * cw + 20, rows * ch + 20), (20, 18, 17, 255))
        for k, (paper, note) in enumerate(built):
            out.alpha_composite(screen_of(paper, note, sheet, k, count), (20 + (k % cols) * cw, 20 + (k // cols) * ch))
        path = os.path.join(ROOT, 'other stuff', 'guide', 'slides-%s.png' % pen.suffix)
        out.save(path)
        print(path)
        return count
    # the sheet: two slides a row - each its paper, then its note beside it - and the
    # arrows under them. A slide a row ran past 4096 at the eleventh (the double
    # tap's page); the anm2 frames carry every piece's box, so the packing is free
    pw, ph = built[0][0].size
    nw, nh = built[0][1].size
    nxt, back = arrows(sheet)
    per_row = 2
    rows = (count + per_row - 1) // per_row
    im = Image.new('RGBA', (per_row * (pw + nw), rows * ph + nxt.height), (0, 0, 0, 0))
    papers, notes = [], []
    for k, (paper, note) in enumerate(built):
        x0, y0 = (k % per_row) * (pw + nw), (k // per_row) * ph
        im.alpha_composite(paper, (x0, y0))
        papers.append((x0, y0, pw, ph))
        im.alpha_composite(note, (x0 + pw, y0))
        notes.append((x0 + pw, y0, nw, nh))
    y = rows * ph
    im.alpha_composite(nxt, (0, y))
    im.alpha_composite(back, (nxt.width, y))
    assert im.width <= 4096 and im.height <= 4096, im.size
    im.save(os.path.join(ASSETS, 'port_guide_%s.png' % pen.suffix))
    half = 100 // K
    write_anm2({'Wall': (0, 100, [(244, 1, 10, 10)]), 'Paper': (1, half, papers), 'Note': (1, half, notes),
                'Next': (1, half, [(0, y, nxt.width, nxt.height)]),
                'Back': (1, half, [(nxt.width, y, back.width, back.height)])}, pen.suffix)
    print('  %s: %d slides, sheet %d x %d' % (pen.suffix, count, im.width, im.height))
    return count


def family_slides(sheet):
    """the slides that carry a pad button in ANY set: the header's one table serves them all"""
    for entry in tp.SETS:
        if entry[0] not in ICON_SLIDES:
            set_family('xbox')
            build_set(entry[0], sheet)
    return sorted(set(k for icons in ICON_SLIDES.values() for k in icons))


def write_family(set_name, sheet, family, slides):
    """port_guide_<suffix>_<family>.png and .anm2: the slides that carry a pad button,
    in this family's pictures, packed as the main sheet packs them (two a row, each
    its paper and its note beside it); the anm2 has their Paper and Note in the
    order of `slides` - the rest of the guide is the main sheet's (tweaks.c)"""
    set_family(family)
    pen, built = build_set(set_name, sheet, only=slides)
    pw, ph = built[0][0].size
    nw, nh = built[0][1].size
    per_row = 2
    rows = (len(built) + per_row - 1) // per_row
    im = Image.new('RGBA', (per_row * (pw + nw), rows * ph), (0, 0, 0, 0))
    papers, notes = [], []
    for k, (paper, note) in enumerate(built):
        x0, y0 = (k % per_row) * (pw + nw), (k // per_row) * ph
        im.alpha_composite(paper, (x0, y0))
        papers.append((x0, y0, pw, ph))
        im.alpha_composite(note, (x0 + pw, y0))
        notes.append((x0 + pw, y0, nw, nh))
    assert im.width <= 4096 and im.height <= 4096, im.size
    name = '%s_%s' % (pen.suffix, family)
    im.save(os.path.join(ASSETS, 'port_guide_%s.png' % name))
    half = 100 // K
    write_anm2({'Paper': (0, half, papers), 'Note': (0, half, notes)}, name,
               sheets=['port_guide_%s.png' % name])
    print('  %s: %d slides of %s, sheet %d x %d' % (pen.suffix, len(built), family, im.width, im.height))


PADICONS_SHOW = os.path.join(ROOT, 'other stuff', 'guide', 'draft')


def show_padicons(sheet, slides, sets=('data', 'dlc3.ru')):
    """for the owner: other stuff/guide/draft/padicons-<family>.png - the slides that
    carry a pad button as the glass shows them, a column a set (English, Russian)"""
    if not os.path.isdir(PADICONS_SHOW):
        os.makedirs(PADICONS_SHOW)
    count = len(DRAW)
    for family in FAMILIES:
        set_family(family)
        cols = []
        for set_name in sets:
            _, built = build_set(set_name, sheet, only=slides)
            cols.append([screen_of(p, n, sheet, k, count) for k, (p, n) in zip(slides, built)])
        w, h = cols[0][0].size
        out = Image.new('RGBA', (len(cols) * (w + 10) + 10, len(slides) * (h + 10) + 10), (20, 18, 17, 255))
        for c, shots in enumerate(cols):
            for r, shot in enumerate(shots):
                out.alpha_composite(shot, (10 + c * (w + 10), 10 + r * (h + 10)))
        path = os.path.join(PADICONS_SHOW, 'padicons-%s.png' % family)
        out.save(path)
        print(path)
    set_family('xbox')


def main():
    sheet = load(gp.MENU, 'collectionmenu.png')
    if '--show-padicons' in sys.argv:
        show_padicons(sheet, family_slides(sheet))
        return
    show = 'spawner' if '--show-spawner' in sys.argv else '20' if '--show-20' in sys.argv else '--show' in sys.argv
    wanted = [a for a in sys.argv[1:] if not a.startswith('--')]
    for a in sys.argv[1:]:                      # --family=ps: the previews in that family's pictures
        if a.startswith('--family='):
            set_family(a.split('=', 1)[1])
    sets = [entry[0] for entry in tp.SETS if not wanted or entry[1] in wanted or entry[0] in wanted]
    counts = {write_set(set_name, sheet, show) for set_name in sets}
    if show:
        return
    assert len(counts) == 1, counts
    count = counts.pop()
    # the other families: the slides that carry a pad button, a sheet a set and a family
    slides = family_slides(sheet)
    for set_name in sets:
        for family in FAMILIES[1:]:
            write_family(set_name, sheet, family, slides)
    set_family('xbox')
    frame_of = [slides.index(k) if k in slides else -1 for k in range(count)]
    L = ['/* Written by tools/guide_slides.py - do not edit by hand, run the tool. */',
         '#ifndef GUIDE_LAYOUT_H', '#define GUIDE_LAYOUT_H', '',
         '/* the slides, and where the collection screen puts its pieces (screen units) */',
         '#define GD_SLIDES %d' % count,
         '#define GD_PAPER_X %d' % gp.PAPER_AT[0], '#define GD_PAPER_Y %d' % gp.PAPER_AT[1],
         '#define GD_PAPER_W %d' % gp.PAPER[2], '#define GD_PAPER_H %d' % gp.PAPER[3],
         '#define GD_NOTE_X %d' % gp.NOTE_AT[0], '#define GD_NOTE_Y %d' % gp.NOTE_AT[1],
         '#define GD_ARROW_W %d' % gp.ARROW[2], '#define GD_ARROW_H %d' % gp.ARROW[3],
         '#define GD_NEXT_X %d' % gp.ARROW_NEXT_X, '#define GD_BACK_X %d' % gp.ARROW_BACK_X,
         '#define GD_ARROW_Y %d' % gp.ARROW_Y, '',
         '/* the families of pad pictures (Java PadIcons, pad.c shim_pad_icon_family): the first is the',
         '   main sheet; the others, port_guide_<set>_<name>.anm2, hold the slides that carry a pad',
         '   button, and GD_FAMILY_FRAME is a slide\'s frame there, -1 for one drawn from the main sheet */',
         '#define GD_FAMILIES %d' % len(FAMILIES),
         '#define GD_FAMILY_NAMES { %s }' % ', '.join('"%s"' % f for f in FAMILIES),
         'static const signed char GD_FAMILY_FRAME[GD_SLIDES] = { %s };' % ', '.join(map(str, frame_of)), '',
         '#endif', '']
    open(HEADER, 'w', newline='\n').write('\n'.join(L))


if __name__ == '__main__':
    main()
