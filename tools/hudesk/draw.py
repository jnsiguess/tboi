# -*- coding: utf-8 -*-
"""Draw desk cases (tools/hudesk/cases.json, written by desk.exe from the real
hudgeom.c) with the game's own art, at the places the game's renderers would
put them (the rules are in hudgeom.c's head: hearts centred on the anchor,
items from pos + (2, 2) s, the bar's pivot at pos + (34, 17) s, the card centred,
its name as 0x6e8bc0 writes it).

    py -3 tools/hudesk/draw.py [out-dir] [filter words...]

Without filters the owner's cases go to other stuff/hud-mockup/fable/. The
game's own fixed pieces - minimap, pickups, stats, the touch buttons and rings -
are grey boxes; player 1's own HUD is drawn with the art, as the game draws it.
"""
import json, os, struct, sys
from PIL import Image, ImageDraw, ImageFont

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
sys.path.insert(0, os.path.join(ROOT, 'tools'))
import anm2render as A

HERE = os.path.dirname(os.path.abspath(__file__))
K = 4.0                                     # pixels a canvas unit (the phone: 5.333)
FONTDIR = os.path.join(ROOT, 'work', 'app', 'repentance-resources', 'data', 'font')
LABEL = r'C:\Windows\Fonts\arialbd.ttf'

hearts = A.Anm2('ui/ui_hearts.anm2')
cards = A.Anm2('ui/ui_cardfronts.anm2')
bar = A.Anm2('ui/ui_chargebar.anm2')
poops = A.Anm2('ui/ui_poops.anm2')
ITEMS = os.path.dirname(A.find('gfx/items/collectibles/collectibles_001_thesadonion.png'))
TRINK = os.path.dirname(A.find('gfx/items/trinkets/trinket_001_swallowedpenny.png'))
FACES = ['00_TheFool', '17_TheStars', '19_TheSun', '06_TheLovers', '13_Death', '10_WheelOfFortune', '15_TheDevil', '11_Strength']
MIX = ['RedHeartFull'] * 6 + ['BoneHeartFull'] * 2 + ['BlueHeartFull'] * 2 + ['BlackHeartFull'] * 2
NAMES = {0: 'XVII - The Stars', 1: 'Get Out Of Jail Free Card', 2: 'XVII - The Stars', 3: 'I - The Magician',
         4: '0 - The Fool', 5: 'XIX - The Sun', 6: 'VI - The Lovers', 7: 'XIII - Death'}
LONG = "Feels like I'm walking on sunshine!"
ITEM_OF = {0: 105, 1: 105, 2: 118, 3: 158, 4: 486, 5: 486, 6: 486, 7: 486}      # D6, Blank Card, Yum Heart, Anarchist Cookbook, Esau: Rock Bottom-ish
TRINKETS = {0: (1, 5), 1: (12, 21), 2: (3, 4), 3: (6, 7), 4: (8, 9), 5: (10, 11), 6: (13, 14), 7: (15, 16)}
GRID = [1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12]


def load_bmfont(name):
    d = open(os.path.join(FONTDIR, name + '.fnt'), 'rb').read()
    p, chars, kern = 4, {}, {}
    while p < len(d):
        t, n = d[p], struct.unpack_from('<I', d, p + 1)[0]
        b = d[p + 5:p + 5 + n]
        if t == 4:
            for i in range(n // 20):
                c = struct.unpack_from('<IHHHHhhhBB', b, i * 20)
                chars[c[0]] = c
        if t == 5:
            for i in range(n // 10):
                a, bb, amt = struct.unpack_from('<IIh', b, i * 10)
                kern[(a, bb)] = amt
        p += 5 + n
    sheet = Image.open(os.path.join(FONTDIR, name + '_0.png')).convert('RGBA')
    return chars, kern, sheet


FONT = load_bmfont('pftempestasevencondensed')


def text_width(s):
    chars, kern, _ = FONT
    w, prev = 0, None
    for ch in s:
        c = chars.get(ord(ch))
        if not c: continue
        if prev is not None: w += kern.get((prev, ord(ch)), 0)
        w += c[7]
        prev = ord(ch)
    return w


def draw_text(im, s, x, y, scale):
    chars, kern, sheet = FONT
    pen, prev = 0.0, None
    for ch in s:
        c = chars.get(ord(ch))
        if not c: continue
        if prev is not None: pen += kern.get((prev, ord(ch)), 0)
        _, cx, cy, cw, chh, xo, yo, xa, _, _ = c
        if cw and chh:
            g = sheet.crop((cx, cy, cx + cw, cy + chh))
            g = g.resize((max(1, round(cw * scale * K)), max(1, round(chh * scale * K))), Image.NEAREST)
            im.alpha_composite(g, (round((x + (pen + xo) * scale) * K), round((y + yo * scale) * K)))
        pen += xa
        prev = ord(ch)


def at(x, y): return (round(x * K), round(y * K))


def anm(im, a, name, x, y, scale=1.0, t=0, alpha=1.0, flip=False):
    layer = Image.new('RGBA', im.size, (0, 0, 0, 0))
    a.draw(layer, name, t, 0, 0, scale * K, at(x, y))
    if flip:
        # mirror about x: redraw into a mirrored layer (the hearts' FlipX for a leftward row)
        layer = layer.transpose(Image.FLIP_LEFT_RIGHT)
        dx = round(2 * x * K) - layer.width
        tmp = Image.new('RGBA', im.size, (0, 0, 0, 0))
        tmp.alpha_composite(layer, (dx, 0) if dx >= 0 else (0, 0), (0, 0) if dx >= 0 else (-dx, 0))
        layer = tmp
    if alpha < 1.0:
        r, g, b, al = layer.split()
        layer = Image.merge('RGBA', (r, g, b, al.point(lambda v: int(v * alpha))))
    im.alpha_composite(layer)


def png(im, folder, prefix, x, y, scale=1.0):
    f = [n for n in os.listdir(folder) if n.lower().startswith(prefix.lower())]
    if not f: return
    src = Image.open(os.path.join(folder, f[0])).convert('RGBA')
    size = (max(1, round(src.width * scale * K)), max(1, round(src.height * scale * K)))
    im.alpha_composite(src.resize(size, Image.NEAREST), at(x, y))


def label(im, text, x, y, size=11, color=(255, 230, 90, 255)):
    d = ImageDraw.Draw(im)
    f = ImageFont.truetype(LABEL, max(8, round(size * K / 2)))
    X, Y = at(x, y)
    for dx, dy in ((1, 1), (-1, 1), (1, -1), (-1, -1)):
        d.text((X + dx, Y + dy), text, font=f, fill=(0, 0, 0, 255))
    d.text((X, Y), text, font=f, fill=color)


def box(im, b, color=(120, 120, 120, 255), text=None):
    d = ImageDraw.Draw(im)
    d.rectangle([at(b[0], b[1]), at(b[2], b[3])], outline=color, width=2)
    if text: label(im, text, b[0] + 1, b[1] + 1, 9, (170, 170, 170, 255))


# ---- the game's renderers, in picture form ------------------------------------------

def hearts_row(im, x, y, hs, leftward, n=12):
    for i in range(n):
        c, r = i % 6, i // 6
        hx = x - 12 * hs * c if leftward else x + 12 * hs * c
        anm(im, hearts, MIX[i], hx, y + 10 * hs * r, hs, flip=leftward)


def active(im, pos, s, item, mirror, slot1=False, alpha=1.0):
    x, y = pos
    f = 0.5 if slot1 else 1.0
    png(im, ITEMS, 'collectibles_%03d_' % item, x + 2 * s * f, y + 2 * s * f, s * f)
    if slot1:
        bx = x + (38 if mirror else -2) * s
    else:
        bx = x + (0 if mirror else 34) * s
    anm(im, bar, 'BarEmpty', bx, y + 17 * s, s)
    anm(im, bar, 'BarFull', bx, y + 17 * s, s)


def trinket(im, pos, s, t):
    png(im, TRINK, 'trinket_%03d_' % t, pos[0] + 2 * s, pos[1] + 2 * s, s)


def card(im, pos, s, face, name, name_right, alpha=1.0):
    x, y = pos
    anm(im, cards, face, x, y, s, alpha=alpha)
    if name_right:
        tx = x + 13 * s
    else:
        tx = x - 16 * s - text_width(name) * s
    draw_text(im, name, tx, y - 10 * s, s)


def grid(im, pos, s, n=12):
    cols = max(1, n // 2)
    for k in range(n):
        c, r = k % cols, k // cols
        png(im, ITEMS, 'collectibles_%03d_' % GRID[k], pos[0] + (12 * c + 1) * s, pos[1] + (12 * r + 1) * s, 0.5 * s)


def spells(im, pos, s):
    for i in range(6):
        anm(im, poops, 'Idle' if i == 0 else 'IdleSmall', pos[0] + (8 + 12 * i) * s, pos[1] + 8 * s, s, t=i, alpha=1.0 if i < 4 else 0.3)


# ---- a case ---------------------------------------------------------------------------

def canvas(W, H):
    im = Image.new('RGBA', (round(W * K), round(H * K)), (28, 20, 16, 255))
    d = ImageDraw.Draw(im)
    # a room: the floor and the walls, roughly where the game's 13 x 7 room sits
    rw, rh = 13 * 26 * (H / 270.0), 7 * 26 * (H / 270.0)
    fx0, fy0 = (W - rw) / 2, (H - rh) / 2
    d.rectangle([at(fx0 - 22, fy0 - 22), at(fx0 + rw + 22, fy0 + rh + 22)], fill=(66, 46, 36, 255))
    d.rectangle([at(fx0, fy0), at(fx0 + rw, fy0 + rh)], fill=(92, 66, 52, 255))
    return im


def draw_fixed(im, c):
    W, H, f = c['W'], c['H'], 1 - c['hudoffset']
    for name, b in c['fixed']:
        if name.startswith('player 1') or name.startswith('Esau') or name.startswith('Jacob') or name == 'the twins cards':
            continue
        box(im, b, text=name)
    # player 1's own HUD, as the game draws it
    kind0 = c['kind'][0]
    if kind0:
        c0 = (68 - 20 * f, 24 - 12 * f)
        hearts_row(im, c0[0], c0[1], 1.0, False, 12)
        active(im, (c0[0] - 53, c0[1] - 12), 1.0, 534, False, slot1=True)
        active(im, (c0[0] - 44, c0[1] - 12), 1.0, ITEM_OF[0], False)
        if not c['twins_button']:
            trinket(im, (20 - 22 * f, H - 68 + 16 * f), 1.0, 1)
            trinket(im, (48 - 22 * f, H - 44 + 16 * f), 1.0, 5)
        if c['extra'][0] == 1: grid(im, (58 - 20 * f, 45 - 12 * f), 1.0, 8)
        if c['extra'][0] == 2: spells(im, (58 - 20 * f, 49 - 12 * f), 1.0)
        if kind0 == 2 and c['jacob0']:
            if not c['cards']:
                card(im, (c0[0] - 37, c0[1] + 29), 1.0, FACES[0], NAMES[0], True)
        else:
            card(im, (W - 28 + 16 * f, H - 18 + 6 * f), 1.0, FACES[0], NAMES[0], False)
        if kind0 == 2 and not c['e'][4]['draw']:
            c4 = (W - 64 + 16 * f, H - 33 + 6 * f)
            hearts_row(im, c4[0], c4[1], 1.0, True, 12)
            active(im, (c4[0] + 38, c4[1] - 12), 1.0, 534, True, slot1=True)
            active(im, (c4[0] + 12, c4[1] - 12), 1.0, ITEM_OF[4], True)
            trinket(im, (W - 76 + 16 * f, H - 65 + 6 * f), 1.0, 8)
            trinket(im, (W - 48 + 16 * f, H - 65 + 6 * f), 1.0, 9)
            if not c['cards']:
                card(im, (c4[0] + 41, c4[1] - 17), 1.0, FACES[1], NAMES[1], False)
    if c['cards']:
        card(im, tuple(c['card_esau']), 1.0, FACES[1], NAMES[1], False)
        card(im, tuple(c['card_jacob']), 1.0, FACES[0], NAMES[0], False)


def draw_entry(im, e, k, long_names):
    if not e['draw']: return
    s, hs = e['s'], e['hs']
    hearts_row(im, e['hearts'][0], e['hearts'][1], hs, bool(e['leftward']), 12)
    mirror = k >= 4
    active(im, e['act1'], s, 534, mirror, slot1=True)
    active(im, e['act0'], s, ITEM_OF[k], mirror)
    if e['trinkets']:
        trinket(im, e['tr1'], s, TRINKETS[k][1])
        trinket(im, e['tr0'], s, TRINKETS[k][0])
    if e['extra'] == 1: grid(im, e['xpos'], s, 12)
    if e['extra'] == 2: spells(im, e['xpos'], s)
    card(im, e['card'], s, FACES[k], LONG if long_names else NAMES[k], bool(e['name_right']))


def draw_case(c, out, long_names=False, boxes=False):
    im = canvas(c['W'], c['H'])
    draw_fixed(im, c)
    for k, e in enumerate(c['e']):
        draw_entry(im, e, k, long_names)
    if boxes:
        for k, name, b in c['parts']:
            box(im, b, (255, 220, 0, 160))
    label(im, c['title'] + ('   (longest names)' if long_names else ''), 4, c['H'] - 10, 10)
    real = [pr for pr in c['problems_list'] if not pr[2]]
    if real:
        label(im, '; '.join('%s x %s' % (pr[0], pr[1]) for pr in real[:4]), 4, c['H'] - 20, 9, (255, 120, 120, 255))
    name = c['title'].replace(':', '_').replace(' ', '_').replace('&', '').replace('?', '_').replace('+', '') + ('-long' if long_names else '') + '.png'
    path = os.path.join(out, name)
    im.convert('RGB').save(path)
    print(path)


OWNER = [
    '20:9 HudOffset 1.0 touch 2p Isaac + T.Isaac',
    '20:9 HudOffset 1.0 touch 2p Isaac + T.???',
    '20:9 HudOffset 1.0 touch 2p J&E + J&E',
    '20:9 HudOffset 1.0 touch solo J&E',
    '20:9 HudOffset 1.0 pad 2p Isaac + Isaac',
    '20:9 HudOffset 1.0 pad 4p singles T.Isaac P4',
    '20:9 HudOffset 1.0 touch 4p J&E P1 + singles',
    '20:9 HudOffset 1.0 touch 4p all J&E',
    '20:9 HudOffset 1.0 touch 4p J&E + T.Isaac + T.??? + J&E',
    '20:9 HudOffset 1.0 touch 3p T.Isaac + J&E + Isaac',
    '20:9 HudOffset 0.5 touch 2p Isaac + T.Isaac',
    '16:9 HudOffset 1.0 pad 3p Isaac + T.Isaac + T.???',
]

if __name__ == '__main__':
    args = sys.argv[1:]
    out = args[0] if args and (os.path.isdir(args[0]) or args[0].endswith('/') or args[0].endswith('\\')) else os.path.join(ROOT, 'other stuff', 'hud-mockup', 'fable')
    filters = args[1:] if args and out == args[0] else args
    os.makedirs(out, exist_ok=True)
    cases = json.load(open(os.path.join(HERE, 'cases.json'), encoding='utf-8'))
    for c in cases:
        t = c['title']
        if filters:
            if not all(w in t for w in filters): continue
        elif t not in OWNER:
            continue
        draw_case(c, out)
        if not filters: draw_case(c, out, long_names=True)
