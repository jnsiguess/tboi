# -*- coding: utf-8 -*-
"""The in-game controls guide (the owner's item 22): a picture of its slides to
agree on before anything is built (25.09.2026).

A slide is the game's own collection screen (collectionmenu.anm2 - STUFF I
FOUND): the pinned paper holds the picture, the yellow note under it the words.
The pictures are the game's own touch controls (ios_buttons.png) drawn at 100%,
with short labels in the menu font; the note is teammeatfont10 in the menus'
ink, three lines at most. English only (the owner: the guide is strictly English).

    py -3 tools/guide_preview.py        # -> other stuff/guide/preview.png
"""
import os

import numpy as np
from PIL import Image

import usermods_preview as up

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
# the port's versions of the menus and buttons (M8: work/port-edit)
MENU = os.path.join(ROOT, 'work', 'port-edit', 'repentance-resources', 'data', 'gfx', 'ui', 'main menu')
TOUCH = os.path.join(ROOT, 'work', 'port-edit', 'rebirth-resources', 'data', 'gfx', 'ui', 'touch', 'ios_buttons.png')
OUT = os.path.join(ROOT, 'other stuff', 'guide')

PAPER = (0, 0, 390, 185)          # collectionmenu.anm2, Paper: at (84, 22) less the pivot (16, 16)
PAPER_AT = (68, 6)
NOTE = (8, 185, 354, 68)          # ItemNote: at (96, 205) less (16, 16)
NOTE_AT = (80, 189)
ARROW = (432, 32, 16, 32)         # NextPage's picture
# the arrows at the paper's sides, in the middle of its height (the owner,
# 25.09.2026: the collection's pair stands at the top and the bottom)
ARROW_Y = PAPER_AT[1] + (PAPER[3] - ARROW[3]) // 2
ARROW_NEXT_X = 424
ARROW_BACK_X = PAPER_AT[0] + PAPER[2] - ARROW_NEXT_X - ARROW[2] + PAPER_AT[0]
WALL = (434, 1, 12, 9)

SPRITES = {                       # ios_buttons.anm2: crop, pivot
    'bounds': ((0, 0, 128, 128), (64, 64)),
    'move': ((208, 0, 64, 64), (32, 32)),
    'shoot': ((272, 0, 64, 64), (32, 32)),
    'item': ((0, 192, 64, 64), (32, 32)),
    'bomb': ((64, 192, 64, 64), (32, 32)),
    'pill': ((128, 192, 64, 64), (32, 32)),
    'pause': ((192, 192, 50, 50), (25, 25)),
    'twin': ((400, 0, 64, 64), (32, 32)),           # hud.c: the round button's own picture
}


def crop(im, box):
    x, y, w, h = box
    return im.crop((x, y, x + w, y + h))


def clean_paper(sheet):
    """the paper without STUFF I FOUND: every pixel of the title band that is not the paper's tone"""
    paper = crop(sheet, PAPER)
    a = np.asarray(paper).astype(int)
    body = a[60:150, 60:330]
    tone = np.median(body[body[..., 3] == 255][:, :3], axis=0)
    band = a[20:50, 95:290]
    off = (np.abs(band[..., :3] - tone).sum(axis=2) > 12) & (band[..., 3] > 0)
    band[off] = tuple(int(v) for v in tone) + (255,)
    return Image.fromarray(a.astype('uint8'))


def put(canvas, im, centre):
    canvas.alpha_composite(im, (int(centre[0] - im.width / 2), int(centre[1] - im.height / 2)))


def sprite(touch, name):
    return crop(touch, SPRITES[name][0])


def wrap(font, text, width):
    return up.wrap(font, text, width)


def slide(title, draw, note, back=False, on=True):
    sheet = Image.open(os.path.join(MENU, 'collectionmenu.png')).convert('RGBA')
    touch = Image.open(TOUCH).convert('RGBA')
    big = up.BMFont(os.path.join(up.FONT, 'teammeatfont20bold.fnt'))
    label = up.BMFont(os.path.join(up.FONT, 'teammeatfont16bold.fnt'))
    small = up.BMFont(os.path.join(up.FONT, 'teammeatfont10.fnt'))
    screen = Image.new('RGBA', (480, 270), (48, 42, 40, 255))    # the menus' wall, plain here
    paper = clean_paper(sheet)
    t = big.draw(title)
    paper.alpha_composite(t, ((paper.width - t.width) // 2 - 4, 22))
    draw(paper, touch, label, small)
    screen.alpha_composite(paper, PAPER_AT)
    n = crop(sheet, NOTE)
    lines = []
    for part in note.split('\n'):
        lines += wrap(small, part, n.width - 36)
    y = 12
    assert len(lines) <= 3, lines           # the note holds three lines
    for line in lines:
        im = small.draw(line)
        n.alpha_composite(im, (18, y))
        y += 12
    screen.alpha_composite(n, NOTE_AT)
    arrow = crop(sheet, ARROW)
    if on:
        screen.alpha_composite(arrow, (ARROW_NEXT_X, ARROW_Y))
    if back:
        screen.alpha_composite(arrow.transpose(Image.FLIP_LEFT_RIGHT), (ARROW_BACK_X, ARROW_Y))
    return screen


def labelled(paper, label, text, centre):
    im = label.draw(text)
    put(paper, im, centre)


def move_and_shoot(paper, touch, label, small):
    for cx, knob, word in ((110, 'move', 'MOVE'), (275, 'shoot', 'SHOOT')):
        put(paper, sprite(touch, knob), (cx, 100))
        labelled(paper, label, word, (cx, 150))


ICONS = {
    'item': (os.path.join(ROOT, 'work', 'app', 'repentance-resources', 'data', 'gfx', 'items', 'collectibles',
                          'collectibles_105_dice.png'), (0, 0, 32, 32)),
    'pill': (os.path.join(ROOT, 'work', 'app', 'repentance-resources', 'data', 'gfx', 'ui', 'ui_cardspills.png'),
             (0, 0, 32, 32)),
}


def buttons(paper, touch, label, small):
    for i, (name, a, b) in enumerate((('pause', 'TAP: PAUSE', 'HOLD: RESTART'),
                                     ('item', 'TAP: USE', ''),
                                     ('pill', 'TAP: USE', 'HOLD: DROP'))):
        cx = 85 + i * 110
        put(paper, sprite(touch, name), (cx, 100))
        if name in ICONS:
            put(paper, crop(Image.open(ICONS[name][0]).convert('RGBA'), ICONS[name][1]), (cx, 104))
        labelled(paper, small, a, (cx, 142))
        if b:
            labelled(paper, small, b, (cx, 156))


def main():
    os.makedirs(OUT, exist_ok=True)
    a = slide('MOVE AND SHOOT', move_and_shoot,
              'Left stick moves, right stick shoots. Stick type, size and touch mode are in '
              'Options. Double-tap a stick to dash with items like Mars.')
    b = slide('BUTTONS', buttons,
              'Hold pause to restart the run; let go during the fade to cancel. '
              'Hold a card or pill to drop it.', back=True)
    sheet = Image.new('RGBA', (480 * 2 + 30, 270 + 20), (20, 18, 17, 255))
    sheet.alpha_composite(a, (10, 10))
    sheet.alpha_composite(b, (500, 10))
    sheet = sheet.resize((sheet.width * 2, sheet.height * 2), Image.NEAREST)
    path = os.path.join(OUT, 'preview.png')
    sheet.save(path)
    print(path)


if __name__ == '__main__':
    main()
