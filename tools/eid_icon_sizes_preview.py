# -*- coding: utf-8 -*-
"""The EID's pictures as they are and as proposed, at the phone's own scale.

The owner's tenth round (24.09.2026): in the Bag of Crafting's list and the
inventory the pictures stand taller than the line and lie on one another, and
they differ - the Sad Onion is small, Brimstone fills its frame. Scaling them
down at an odd scale crushed them (texels dropped and mixed); baking them
smaller did the same.

What keeps a picture whole is a scale whose texel is a whole number of screen
pixels. On the owner's phone a unit is 1440 / 270 = 5.333 pixels, so the
scales 0.375, 0.5625, 0.75 and 0.9375 give 2, 3, 4 and 5 pixels a texel. Each
picture takes the largest of them at which its OWN drawing - not its frame -
fits one box, and is centred in it: every picture comes out one size, give or
take a step, and every one is drawn whole.

    py -3 tools/eid_icon_sizes_preview.py      # -> other stuff/eid-icons/sizes.png
"""
import os
import re
from PIL import Image

from eid_font_preview import Font

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
ASSETS = os.path.join(ROOT, 'port', 'app', 'src', 'main', 'assets')
ITEMS = os.path.join(ROOT, 'work', 'app', 'repentance-resources', 'data', 'gfx', 'items', 'collectibles')
OUT = os.path.join(ROOT, 'other stuff', 'eid-icons')
S = 1440 / 270.0                      # screen pixels a unit on the owner's phone
WHOLE = (0.375, 0.5625, 0.75, 0.9375)  # 2, 3, 4, 5 pixels a texel
GROUND = (60, 44, 40, 255)


def frame(anm2, anim, index=0):
    s = open(os.path.join(ASSETS, anm2), encoding='utf-8').read()
    m = re.search(r'<Animation Name="%s"[^>]*>(.*?)</Animation>' % re.escape(anim), s, re.S)
    f = re.findall(r'<Frame ([^>]*XCrop[^>]*)>', m.group(1))[index]
    g = lambda k: int(float(re.search(k + r'="([^"]*)"', f).group(1)))
    sheet = re.search(r'<Spritesheet Path="([^"]*)"', s).group(1)
    im = Image.open(os.path.join(ASSETS, sheet)).convert('RGBA')
    x, y, w, h = g('XCrop'), g('YCrop'), g('Width'), g('Height')
    return im.crop((x, y, x + w, y + h))


def put(canvas, im, ux, uy, scale=1.0):
    """a picture at unit (ux, uy), drawn at scale: every texel 5.333 * scale pixels"""
    w = max(1, round(im.width * S * scale))
    h = max(1, round(im.height * S * scale))
    canvas.alpha_composite(im.resize((w, h), Image.NEAREST), (round(ux * S), round(uy * S)))


EXACT = [False]                        # True: every drawing exactly the box, texels uneven


def boxed(canvas, im, ux, uy, box):
    """the drawing (its bounding box, not its frame) at the largest whole-pixel
    scale that fits box units, centred in the box at (ux, uy); with EXACT, at
    the one scale that makes its longer side the box, whatever the texels do"""
    bb = im.getchannel('A').point(lambda v: 255 if v >= 128 else 0).getbbox() or (0, 0, im.width, im.height)
    art = im.crop(bb)
    if EXACT[0] == 'area':
        # the same weight for all: the area a typical drawing has at this box
        # (0.549 of the square of its longer side, the median of all 910 items),
        # and never past the box
        filled = sum(1 for v in im.getchannel('A').getdata() if v >= 128)
        s = min((0.549 * box * box / max(filled, 1)) ** 0.5, box / max(art.width, art.height))
        put(canvas, art, ux + (box - art.width * s) / 2, uy + (box - art.height * s) / 2, s)
        return
    if EXACT[0]:
        s = box / max(art.width, art.height)
        put(canvas, art, ux + (box - art.width * s) / 2, uy + (box - art.height * s) / 2, s)
        return
    fits = [s for s in WHOLE if max(art.width, art.height) * s <= box]
    s = fits[-1] if fits else WHOLE[0]
    put(canvas, art, ux + (box - art.width * s) / 2, uy + (box - art.height * s) / 2, s)


def text(canvas, font, words, ux, uy):
    put(canvas, font.draw(words), ux, uy - 1)


def panel_now(font, item, recipe, stat, form, arrow, coin):
    line = 11
    c = Image.new('RGBA', (round(150 * S), round(11 * 22 * S)), GROUND)
    y = 4
    put(c, item, -3 - 4, y - 4 - 4, 0.75)
    text(c, font, 'Bag of Crafting', 16, y)
    y += line
    text(c, font, '- Collects up to 8 pickups', 2, y)
    y += line
    put(c, arrow, 2, y + 1)
    put(c, stat, 13, y - 2)
    text(c, font, '+0.5 Damage', 25, y)
    y += line
    put(c, form, 1, y - 1, 0.75)
    text(c, font, 'Guppy (1/3)', 16, y)
    y += line
    for it in recipe:
        put(c, it, -2 - 4, y - 2 - 4, 0.75)
        text(c, font, '=1', 14, y)
        put(c, coin, 25, y + 1)
        text(c, font, '5', 34, y)
        put(c, coin, 41, y + 1)
        y += line
    return c


def centred_on(canvas, im, ux, mid, scale=1.0):
    """a small picture with its drawing's middle on the line's middle"""
    bb = im.getchannel('A').point(lambda v: 255 if v >= 128 else 0).getbbox() or (0, 0, im.width, im.height)
    art = im.crop(bb)
    put(canvas, art, ux, mid - art.height * scale / 2, scale)


def panel_boxed(font, item, recipe, stat, form, arrow, coin, line, box, small, pic_line=None):
    """items and transformations by area in box; the small pictures (arrows,
    coins) at scale small, the stat icons at an arrow's height; every one
    with its middle on the words' middle. A line with a big picture is
    pic_line tall, the rest line."""
    pic_line = pic_line or line
    c = Image.new('RGBA', (round(170 * S), round(11 * 22 * S)), GROUND)
    y = 4 + (pic_line - line) / 2
    mid_of = lambda y: y + 5.5            # the words' middle (the arrow's own, at the mod's offset)
    def big(im, ux, y):
        boxed(c, im, ux, mid_of(y) - box / 2, box)
    def little(im, ux, y, fit=None):
        bb = im.getchannel('A').point(lambda v: 255 if v >= 128 else 0).getbbox()
        art = im.crop(bb)
        sc = small
        if fit is not None:
            sc = min(small, fit / max(art.width, art.height))
        put(c, art, ux, mid_of(y) - art.height * sc / 2, sc)
        return art.width * sc
    text_x = box + 1
    # a line with a big picture: half the extra room above it, half below
    def step(y, was_big, now_big):
        a = (pic_line - line) / 2 if was_big else 0
        b = (pic_line - line) / 2 if now_big else 0
        return y + line + a + b
    big(item, -2, y)
    text(c, font, 'Bag of Crafting', text_x, y)
    y = step(y, True, False)
    text(c, font, '- Collects up to 8 pickups', 2, y)
    y = step(y, False, False)
    w = little(arrow, 2, y)
    w2 = little(stat, 2 + w + 1, y, fit=9 * small)
    text(c, font, '+0.5 Damage', 2 + w + 1 + w2 + 2, y)
    y = step(y, False, False)
    w = little(coin, 3, y)
    text(c, font, 'Spawns 7 random coins', 3 + w + 3, y)
    y = step(y, False, True)
    big(form, -2, y)
    text(c, font, 'Guppy (1/3)', text_x, y)
    for it in recipe:
        y = step(y, True, True)
        big(it, -2, y)
        text(c, font, '=1', text_x, y)
        w = little(coin, text_x + 11, y)
        text(c, font, '5', text_x + 11 + w + 2, y)
        little(coin, text_x + 11 + w + 9, y)
    return c


def main():
    font = Font(os.path.join(ASSETS, 'eid_default.fnt'))
    arrow = frame('eid_inline_icons.anm2', 'ArrowUp')
    coin = frame('eid_inline_icons.anm2', 'pickups', 4)
    stat = frame('eid_inline_icons.anm2', 'Stats', 0)
    form = frame('eid_transform_icons.anm2', 'Transformation1')
    names = ['collectibles_710_bagofcrafting.png', 'collectibles_118_brimstone.png',
             'collectibles_001_thesadonion.png', 'collectibles_182_sacredheart.png',
             'collectibles_114_momsknife.png', 'collectibles_169_polyphemus.png',
             'collectibles_081_deadcat.png', 'collectibles_245_2020.png']
    items = [Image.open(os.path.join(ITEMS, n)).convert('RGBA') for n in names]
    def exact(mode, *args):
        EXACT[0] = mode
        try:
            return panel_boxed(*args)
        finally:
            EXACT[0] = False
    panels = [('IN THE GAME NOW', exact('area', font, items[0], items[1:], stat, form, arrow, coin, 16, 15, 1.125)),
              ('BIG x1.5, TEXT LINES 13', exact('area', font, items[0], items[1:], stat, form, arrow, coin, 13, 19, 1.125, 20)),
              ('BIG x1.5, TEXT LINES 12', exact('area', font, items[0], items[1:], stat, form, arrow, coin, 12, 19, 1.125, 20))]
    gap = 40
    w = panels[0][1].width
    sheet = Image.new('RGBA', (w * 3 + gap * 4, panels[0][1].height + 110), (30, 26, 25, 255))
    for k, (label, im) in enumerate(panels):
        x = gap + k * (w + gap)
        sheet.alpha_composite(im, (x, 90))
        put(sheet, font.draw(label), x / S, 4)
    os.makedirs(OUT, exist_ok=True)
    path = os.path.join(OUT, 'sizes.png')
    sheet.save(path)
    print(path, sheet.size, len(items), 'items')


if __name__ == '__main__':
    main()
