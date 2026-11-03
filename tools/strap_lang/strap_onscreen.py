# -*- coding: utf-8 -*-
"""The lengthened straps laid on the owner's own mock-ups, under the paper the
way the game draws them: the settings page (TWEAKS and GUIDE), the pause
paper (ITEM SPAWN), the title sheet (PORT CREDITS)."""
import json, os, sys
import numpy as np
from PIL import Image, ImageDraw, ImageFont
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import strap_base as s2
ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
ART = os.path.join(ROOT, 'menu-labels', 'owner-art', 'straps')
E = 6                     # strap_layers.EXTEND: the strap file starts 6 left of the paper crop
HERE = os.path.dirname(os.path.abspath(__file__))
OUT_DIR = os.path.join(ROOT, 'other stuff', 'straps-lang')   # the straps and previews
os.makedirs(OUT_DIR, exist_ok=True)


# Where a lengthened strap would lie over something drawn beside the paper - the
# card on the pause screen, the chest the owner drew on the title - it is moved
# up or down by the least that clears it (the owner, 30.09.2026). Only what lies
# right of the paper counts: the strap's left end is under the paper anyway.
PAPER_EDGE = {'pause': 236, 'title': 345}   # the paper's own edge reaches 230 on the pause mock-up
# the drawn things a strap must not lie over, measured on the mock-ups (x0, y0, x1, y1):
# the card on the pause screen and the blood under it; the chest and its rays on the title
KEEP_CLEAR = {'pause': [(288, 98, 382, 164), (351, 177, 390, 247), (250, 0, 399, 63)],   # + the seed lines at the top
              # the chest in gamemenu.png is not on the title screen (the owner, 30.09:
              # nothing is drawn right of the credits strap) - the file is a sheet of
              # pieces, not a picture of the screen
              'title': []}


def clear_of(shape, where):
    ob = np.zeros(shape, bool)
    for x0, y0, x1, y1 in KEEP_CLEAR[where]:
        ob[y0:y1 + 1, x0:x1 + 1] = True
    return ob
SHIFTS = {}


def obstacles(mock, owner_strap, at, edge):
    """what is drawn right of the paper, with the owner's own strap taken out"""
    m = np.asarray(mock.convert('RGBA')).copy()
    o = np.asarray(owner_strap)
    x0, y0 = at
    h, w = o.shape[:2]
    sub = m[y0:y0 + h, x0:x0 + w]
    same = (sub[..., :3] == o[..., :3]).all(2) & (o[..., 3] > 0)
    sub[same] = 0
    ob = m[..., 3] > 40
    ob[:, :edge] = False
    return ob


def clear_shift(ob, ours, x0, y0):
    """the least move up or down at which the strap, shadow and all, touches nothing"""
    a = np.asarray(ours)[..., 3] > 0
    h, w = a.shape
    for k in range(0, 60):
        for dy in ((0,) if k == 0 else (-k, k)):
            y = y0 + dy
            if y < 0 or y + h > ob.shape[0] or x0 + w > ob.shape[1]:
                continue
            if not (a & ob[y:y + h, x0:x0 + w]).any():
                return dy
    return None


def under(mock, owner_strap, at, ours, dy=0):
    """mock with the owner's strap taken out (where its pixels show) and ours put under"""
    m = np.asarray(mock.convert('RGBA')).copy()
    o = np.asarray(owner_strap)
    x0, y0 = at
    h, w = o.shape[:2]
    sub = m[y0:y0 + h, x0:x0 + w]
    same = (sub[..., :3] == o[..., :3]).all(2) & (o[..., 3] > 0)
    sub[same] = 0
    canvas = Image.new('RGBA', mock.size, (0, 0, 0, 0))
    rise = ours.size[1] - o.shape[0]
    canvas.alpha_composite(ours, (x0, y0 - rise + dy))
    canvas.alpha_composite(Image.fromarray(m))
    return canvas


def page(suf):
    shots = []
    opt = Image.open(os.path.join(ART, 'options', 'what it should look like.png'))
    t = Image.open(os.path.join(OUT_DIR, '%s_tweaks.png' % suf)) if os.path.exists(os.path.join(OUT_DIR, '%s_tweaks.png' % suf)) else s2.owner('tweaks')
    g = Image.open(os.path.join(OUT_DIR, '%s_guide.png' % suf)) if os.path.exists(os.path.join(OUT_DIR, '%s_guide.png' % suf)) else s2.owner('guide')
    im = under(opt, s2.owner('tweaks'), (210 - E, 49), t)
    # GUIDE lies DROP under TWEAKS, also under the paper; the mock-up predates it
    back = Image.new('RGBA', im.size, (0, 0, 0, 0))
    rise = g.size[1] - s2.owner('guide').size[1]
    back.alpha_composite(g, (210 - E, 49 + 52 - rise))
    back.alpha_composite(im)
    shots.append(('settings', back))
    pause = Image.open(os.path.join(ART, 'pause screen', 'what it should look like.png'))
    sp = os.path.join(OUT_DIR, '%s_spawner.png' % suf)
    spi = Image.open(sp) if os.path.exists(sp) else s2.owner('spawner')
    ob = clear_of((pause.size[1], pause.size[0]), 'pause')
    rise = spi.size[1] - s2.owner('spawner').size[1]
    dy = clear_shift(ob, spi, 214 - E, 85 - rise)
    SHIFTS['%s_spawner' % suf] = dy
    shots.append(('pause', under(pause, s2.owner('spawner'), (214 - E, 85), spi, dy or 0)))
    sheet = Image.open(os.path.join(ART, 'title', 'sheet.png')).convert('RGBA')
    cr = os.path.join(OUT_DIR, '%s_credits.png' % suf)
    c = Image.open(cr) if os.path.exists(cr) else s2.owner('credits')
    title = Image.new('RGBA', sheet.size, (0, 0, 0, 0))
    rise = c.size[1] - s2.owner('credits').size[1]
    ob = clear_of((sheet.size[1], sheet.size[0]), 'title')
    dy = clear_shift(ob, c, 321 - E, 141 - rise)
    SHIFTS['%s_credits' % suf] = dy
    title.alpha_composite(c, (321 - E, 141 - rise + (dy or 0)))
    title.alpha_composite(sheet)
    shots.append(('title', title.crop((0, 0, 480, 270))))
    return shots


def main():
    sys.stdout.reconfigure(encoding='utf-8')
    Z = 2
    rows = []
    for suf in ('ru', 'de', 'es', 'jp', 'kr', 'zh'):
        rows.append((suf, page(suf)))
    W = sum(im.size[0] * Z + 12 for _, im in rows[0][1]) + 50
    H = sum(max(im.size[1] for _, im in r) * Z + 12 for _, r in rows) + 10
    out = Image.new('RGB', (W, H), (20, 18, 17))
    d = ImageDraw.Draw(out)
    lab = ImageFont.truetype('arial.ttf', 18)
    y = 6
    for suf, shots in rows:
        d.text((8, y + 10), suf.upper(), fill=(230, 220, 210), font=lab)
        x = 50
        for _, im in shots:
            big = im.resize((im.size[0] * Z, im.size[1] * Z), Image.NEAREST)
            bg = Image.new('RGBA', big.size, (60, 55, 52, 255))
            bg.alpha_composite(big)
            out.paste(bg.convert('RGB'), (x, y))
            d.rectangle([x, y, x + big.size[0] - 1, y + big.size[1] - 1], outline=(200, 80, 60))
            x += big.size[0] + 12
        y += max(im.size[1] for _, im in shots) * Z + 12
    out.save(os.path.join(OUT_DIR, 'onscreen.png'))
    # the moves, for the layers and the taps (strap_layers.py, tweaks/spawner/credits.c)
    with open(os.path.join(OUT_DIR, 'shifts.json'), 'w') as f:
        json.dump(SHIFTS, f, indent=1, sort_keys=True)
    for k, v in sorted(SHIFTS.items()):
        print('%-12s %s' % (k, 'NO CLEAR PLACE' if v is None else ('in place' if v == 0 else 'moved %+d' % v)))


if __name__ == "__main__":
    main()
