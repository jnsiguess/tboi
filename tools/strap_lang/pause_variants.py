# -*- coding: utf-8 -*-
"""The pause screen for Korean and Chinese: the full word in place, the full word
moved to touch the least, and the short word at the strap's own length."""
import os, sys
import numpy as np
from PIL import Image, ImageDraw, ImageFont
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import strap_lang as L
import strap_onscreen as O

sys.stdout.reconfigure(encoding='utf-8')
Ms = {n: L.measure(n) for n in L.s2.ORDER}
pause = Image.open(os.path.join(O.ART, 'pause screen', 'what it should look like.png'))
ob = O.clear_of((pause.size[1], pause.size[0]), 'pause')


def least(ours):
    a = np.asarray(ours)[..., 3] > 0
    h, w = a.shape
    rise = ours.size[1] - O.s2.owner('spawner').size[1]
    y0 = 85 - rise
    best = None
    for dy in range(-30, 31):
        y = y0 + dy
        if y < 0 or y + h > ob.shape[0]:
            continue
        hit = int((a & ob[y:y + h, 208:208 + w]).sum())
        if best is None or hit < best[0] or (hit == best[0] and abs(dy) < abs(best[1])):
            best = (hit, dy)
    return best[1]


rows = []
# Korean: the full word (already made), and 스폰
kr_full = Image.open(os.path.join(O.OUT_DIR, 'kr_spawner.png'))
L.FORCE[('dlc3.kr', 'spawner')] = dict(word='스폰', squeeze=0.95, stretch=1.10, shrink=0.85)
kr_short, _ = L.build('스폰', 'dlc3.kr', 'spawner', Ms['spawner'])
zh_full = Image.open(os.path.join(O.OUT_DIR, 'zh_spawner.png'))
zh_h, _ = L.zh_common({n: L.WORDS['dlc3.zh'][n][0] for n in L.s2.ORDER}, Ms)
_, zh_short, _ = L.zh_fits('生成', 'spawner', Ms['spawner'], zh_h)
for lab, full, short in (('KR', kr_full, kr_short), ('ZH', zh_full, zh_short)):
    d = least(full)
    rows.append((lab, [('full word, in place', O.under(pause, O.s2.owner('spawner'), (214 - O.E, 85), full, 0)),
                       ('full word, moved %+d' % d, O.under(pause, O.s2.owner('spawner'), (214 - O.E, 85), full, d)),
                       ('short word', O.under(pause, O.s2.owner('spawner'), (214 - O.E, 85), short, 0))]))
Z = 2
W = 60 + 3 * (400 * Z + 12)
H = len(rows) * (256 * Z + 40) + 10
out = Image.new('RGB', (W, H), (20, 18, 17))
dr = ImageDraw.Draw(out)
f = ImageFont.truetype('arial.ttf', 20)
y = 10
for lab, shots in rows:
    dr.text((10, y + 30), lab, fill=(230, 220, 210), font=f)
    x = 60
    for cap, im in shots:
        dr.text((x, y), cap, fill=(230, 220, 210), font=f)
        big = im.resize((im.size[0] * Z, im.size[1] * Z), Image.NEAREST)
        bg = Image.new('RGBA', big.size, (60, 55, 52, 255))
        bg.alpha_composite(big)
        out.paste(bg.convert('RGB'), (x, y + 28))
        x += big.size[0] + 12
    y += 256 * Z + 40
out.save(os.path.join(O.OUT_DIR, 'pause_variants.png'))
print('ok')
