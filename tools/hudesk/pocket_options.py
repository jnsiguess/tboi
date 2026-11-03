# -*- coding: utf-8 -*-
"""Where the touch pocket buttons could stand in co-op: option pictures for the
owner (05.10.2026). The game's own blocks come from tools/hudesk/cases.json
(desk.exe over the real hudgeom.c); the touch pieces are drawn as the port
places them today (hud.c shim_hud_row: the row's right end 24 before the
shooting stick's ring, one button a pocket, the second player's row one button
below; sticks.c: the row is item 2 of the touch editor and the player can move
it already).

    py -3 tools/hudesk/pocket_options.py

Pictures: other stuff/hud-mockup/fable/pocket-options/.
"""
import json, os, sys
from PIL import Image, ImageDraw, ImageFont

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import draw as D

OUT = os.path.join(D.ROOT, 'other stuff', 'hud-mockup', 'fable', 'pocket-options')
os.makedirs(OUT, exist_ok=True)
K = D.K
CASE = '20:9 HudOffset 1.0 touch 4p J&E + T.Isaac + T.??? + J&E'
cases = json.load(open(os.path.join(HERE, 'cases.json'), encoding='utf-8'))
c = next(x for x in cases if x['title'] == CASE)
W, H = c['W'], c['H']

# the touch pieces as the phone has them (hud-3.jpg, hud.c): the shooting ring's
# left edge at 496 (report: "stick 495.8"), ring 70 across; a pocket button is
# 36 x 30, the gap 1.5, the row's right end 24 before the ring
RING_R = (496, 167, 566, 237)
RING_L = (19, 167, 89, 237)
BW, BH, GAPB, ROW_GAP = 36.0, 30.0, 1.5, 24.0
TWIN_BTN = (W - 150, 16, W - 118, 48)
PAUSE = (W - 108, 20, W - 87, 42)


def rect_from_parts(parts, entry):
    xs0 = [b[0] for k, n, b in parts if k == entry]
    if not xs0: return None
    return (min(b[0] for k, n, b in parts if k == entry), min(b[1] for k, n, b in parts if k == entry),
            max(b[2] for k, n, b in parts if k == entry), max(b[3] for k, n, b in parts if k == entry))


def meets(a, b):
    return a[0] < b[2] and b[0] < a[2] and a[1] < b[3] and b[1] < a[3]


def covered(buttons):
    hits = []
    for bb in buttons:
        for k, name, b in c['parts']:
            if meets(bb, b):
                hits.append('%s %s' % (D.NAMES and ['игрок 1', 'игрок 2', 'игрок 3', 'игрок 4', 'Исав 1', 'Исав 2', 'Исав 3', 'Исав 4'][k], name))
    return sorted(set(hits))


def button(im, box, face, dim=False):
    d = ImageDraw.Draw(im)
    x0, y0, x1, y1 = box
    fill = (70, 70, 70, 140) if dim else (95, 95, 95, 200)
    d.rounded_rectangle([D.at(x0, y0), D.at(x1, y1)], radius=round(4 * K), fill=fill, outline=(160, 160, 160, 255), width=2)
    D.anm(im, D.cards, face, (x0 + x1) / 2, (y0 + y1) / 2, 1.0)


def ring(im, box, text):
    d = ImageDraw.Draw(im)
    d.ellipse([D.at(box[0], box[1]), D.at(box[2], box[3])], outline=(150, 150, 150, 255), width=3)
    D.label(im, text, box[0] + 10, (box[1] + box[3]) / 2 - 5, 10, (190, 190, 190, 255))


def base():
    im = D.canvas(W, H)
    # the game's and our blocks, player 1's HUD included; no grey boxes for touch pieces
    c2 = dict(c); c2['fixed'] = [f for f in c['fixed'] if f[0] in ('minimap', 'pickups', 'stats')]
    D.draw_fixed(im, c2)
    for k, e in enumerate(c['e']):
        D.draw_entry(im, e, k, False)
    ring(im, RING_L, 'стик хода')
    ring(im, RING_R, 'стик стрельбы')
    d = ImageDraw.Draw(im)
    d.ellipse([D.at(TWIN_BTN[0], TWIN_BTN[1]), D.at(TWIN_BTN[2], TWIN_BTN[3])], outline=(150, 150, 150, 255), width=3)
    D.label(im, 'близн.', TWIN_BTN[0] + 2, TWIN_BTN[1] + 11, 9, (190, 190, 190, 255))
    d.rounded_rectangle([D.at(PAUSE[0], PAUSE[1]), D.at(PAUSE[2], PAUSE[3])], radius=round(3 * K), outline=(150, 150, 150, 255), width=3)
    D.label(im, 'II', PAUSE[0] + 7, PAUSE[1] + 6, 11, (190, 190, 190, 255))
    return im


def option(name, title, trade, buttons):
    im = base()
    for i, bb in enumerate(buttons):
        button(im, bb, D.FACES[0] if i == 0 else D.FACES[1])
    hits = covered(buttons)
    D.label(im, title, 4, 2, 12)
    D.label(im, trade, 4, 14, 9, (220, 220, 220, 255))
    D.label(im, ('закрывает: ' + '; '.join(hits)) if hits else 'ничего из HUD не закрывает', 4, H - 12, 9,
            (255, 120, 120, 255) if hits else (120, 255, 120, 255))
    path = os.path.join(OUT, name + '.png')
    im.convert('RGB').save(path)
    print(path, hits)
    return im


right_end = RING_R[0] - ROW_GAP                       # 472
# A: today - left of the shooting stick, the two rows stacked, the lower one at the ring's middle band
a1 = (right_end - BW, 193, right_end, 193 + BH)
a2 = (right_end - BW, 193 + BH, right_end, 193 + 2 * BH)
A = option('A-today', 'А. Сегодня: левее стика стрельбы, два ряда в столбик (место игры)',
           'Так ставит hud.c: правый край ряда в 24 от кольца. Лежит на углу игрока 4 - у iOS тоже.', [a1, a2])

# B: above the shooting stick, a horizontal row, the rows side by side, the right end at the ring's right edge
y_b1 = RING_R[1] - ROW_GAP - BH
b1 = (RING_R[2] - BW, y_b1, RING_R[2], y_b1 + BH)
b2 = (RING_R[2] - 2 * BW - GAPB, y_b1, RING_R[2] - BW - GAPB, y_b1 + BH)
B = option('B-above-stick', 'Б. Над стиком стрельбы, в ряд (ряд Иакова справа, ряд Исава слева)',
           'Палец идёт вверх от стика, зазор 24 как сегодня. Над кольцом - туда же уходит ручка при стрельбе вверх.', [b1, b2])

# C: a column at the right edge above the stick, the first row nearest the stick
c1 = (W - BW, RING_R[1] - ROW_GAP - BH, W, RING_R[1] - ROW_GAP)
c2 = (W - BW, RING_R[1] - ROW_GAP - 2 * BH - GAPB, W, RING_R[1] - ROW_GAP - BH - GAPB)
C = option('C-right-edge-column', 'В. Столбик у правого края над стиком (ряд Иакова ниже, Исава выше)',
           'У самого края экрана, под миникартой; ничего не закрывает ни в коопе, ни одному.', [c1, c2])

# F: today's x, raised above player 4's block
p4 = rect_from_parts(c['parts'], 3)
e4 = rect_from_parts(c['parts'], 7)
top4 = min(p4[1], e4[1]) if e4 else p4[1]
f2 = (right_end - BW, top4 - 2 - BH, right_end, top4 - 2)
f1 = (right_end - BW, top4 - 2 - 2 * BH - GAPB, right_end, top4 - 2 - BH - GAPB)
F = option('D-today-raised', 'Г. Как сегодня, но поднято над блоком игрока 4 (только когда он есть)',
           'Та же сторона стика, на 42 выше; одному игроку остаётся место игры. Над кольцом по высоте - почти у его верха.', [f1, f2])

# the four side by side, the owner's sheet
sheet = Image.new('RGB', (A.width * 2 + 30, A.height * 2 + 30), (14, 14, 14))
for i, im in enumerate((A, B, C, F)):
    sheet.paste(im.convert('RGB'), (10 + (i % 2) * (A.width + 10), 10 + (i // 2) * (A.height + 10)))
path = os.path.join(OUT, 'pocket-options-sheet.png')
sheet.save(path)
print(path)
