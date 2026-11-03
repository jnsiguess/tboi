# -*- coding: utf-8 -*-
"""The spawner's ENEMIES tab: every monster of the game's bestiary, its icon, its
champions and boss colours, and its name in the seven language sets.

THE LIST is the game's own: every entities2.xml entity that carries <bestiary>
(the game's "these are the monsters"), types 10..999, plus the training dummy.
Bosses are the ones with boss="1" and a bossID, minibosses boss="1" without
one, monsters the rest. Bosses go by the first floor pool that holds them
(bosspools.xml), then by bossID; minibosses and monsters by type.variant.subtype.
The dummy (964.0, Entity_Dummy, no bestiary entry) heads the list under its own
TRAINING heading, with names of our own.

THE ICONS, as the owner approved them (04.10.2026, other stuff/spawner-19-mockups,
the notes v1..v6). Everything at 100 %, nothing resampled by a fraction:

  - a boss or a miniboss wears its Enhanced Boss Bars icon (bossbars_data.h maps
    type.variant to it): the 32 ones as drawn, the 64 ones (Mega Satan, the
    Ultra horsemen) their whole ink - none of them is pixel-doubled, so none can
    be brought down without loss;
  - every other one is its body in its RESTING POSE (survey5 in the mock-ups):
      1. "Idle" when the .anm2 has one;
      2. the bestiary's animation when it is a standing move (Walk, Fly, Float...),
         with its overlay;
      3. the DefaultAnimation unless it is an entrance or an exit;
      4. the default is an entrance (Appear...): its LAST frame, the pose the
         creature settles in;
      5. the bestiary's animation;
    frame 0 (4: the last); a pose empty or under half the ink of the fullest
    frame of the bestiary's animation takes that animation's first frame with
    85 % of the ink. COLOUR VARIANTS of one monster (the same .anm2 layout) are a
    family and take the pose the first of them took;
  - layers named "shadow" are not drawn (a ground shadow, not the creature); a
    faint glow is not counted in the size (the box is the ink over a quarter);
  - hanging vessels, threads and ropes (Mom's Heart, It Lives, Daddy Long Legs,
    Triachnid, the Hanger) keep a stub of 4;
  - a body over 48 on either side comes down by the smallest whole factor that
    brings it inside, each block its majority colour (ties: the darkest) - no
    colour is mixed; the small ones stay as they are. Mother's Shadow, the one
    miniboss with no Boss Bars icon: its whole body /3 (45 x 40, the owner).

Rows grow to their icon; the icon column is 64 wide (Mega Satan's 63).

THE CHAMPIONS (new, 04.10.2026). A monster with champion="1" (234, and Super
Envy) gets a sub-list of all 26 champion colours; a boss with bosscolors.xml
entries gets a sub-list of those colours, each with the Boss Bars icon of that
colour (monstro_red...). How the game paints a champion was read in the image:

  - MakeChampion 0x6825bc -> 0x6816dc reloads every layer whose sheet has a
    "<sheet>_champion.png" (346 such sheets): the same art, the part that takes
    the colour drawn at alpha 128 - the mask;
  - with such layers (0x689234 -> 0x6893c0) the entity's colour (+0x138) is the
    table entry's own Color (0xac28f8 + k*0x40 + 0x14) and the champion colour
    (+0x164) is the entry's KColor, laid over the mask: rgb * KColor. Without
    them the whole sprite takes Color(tint = KColor) * the entry's Color
    (0x689540);
  - so each champion-able icon is baked in two parts from the champion sheets
    (same pose, same crop, same whole-factor reduction): "base" (everything
    but the mask) and "mask" (the mask, opaque); the port draws the base with
    the entity's colour and the mask with it times the KColor - which is what
    the game's renderer makes of it, pixel for pixel; and it can do it every
    frame, for the colours the game animates (spawner.c, champion_colour);
  - TINY is the same two parts brought down by 2 (majority colour), GIANT as is.

WHAT THE 20th PASS SET RIGHT (05.10.2026, the research notes of 05.10.2026 (spawner, co-op, champions)):

  - the mask takes the KColor on its LUMINANCE, not on its colour
    (coloroffset_champion.fs: mix(rgb, KColor * dot(rgb, lum), KColor.a * weight),
    weight (1 - a) * 2.00788 for a >= 128): "Lum" carries the mask's luminance
    with its weight in the alpha, drawn over the sprite with tint * KColor;
  - Pulsating grows by 1 + clamp(sin 0.2f, 0, 1) * 0.15 about the entity's
    origin (render 0x6978c4): its sizes are baked whole-pixel steps in
    port_enemy_pulse.png/.anm2 "Pulse", nearest texel, the pivot from the pose;
  - Crown and Skull hang gfx/StatusEffects.anm2 "Crown"/"Skull" at the body's
    OverlayEffect null (render 0x69828c): the null's place in the icon is
    written for the card (SP_CHAMP_ART ox, oy).

The measurements are checked against the wiki's own in-game pictures (Champion_*.webp):
a red gaper's skin 227,198,197 -> 228,11,11, the blood and outline untouched.

    gfx/port_enemy_icons.png/.anm2   "Icons" (one per enemy), "Champ" (base, mask,
                                     tiny base, tiny mask per champion-able one),
                                     "Colours" (one per boss colour), "Lum" (one per
                                     champion-able one); pivot top left
    gfx/port_enemy_pulse.png/.anm2   "Pulse": Pulsating's sizes
    port/app/src/main/cpp/spawner_enemies.h

    py -3 tools/spawner_enemies.py
"""
import os
import re
import sys
import json
import xml.etree.ElementTree as ET
from collections import Counter

import numpy as np
from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import spawner_common as sc    # noqa: E402
import anm2render as AR        # noqa: E402

COL = 64                       # the icon column
BAND = 48                      # a body larger than this on either side comes down
MS_FACTOR = 3                  # Mother's Shadow, the owner
STUB = 4
HEADER = os.path.join(sc.CPP, 'spawner_enemies.h')
GROUP = ['SP_GROUP_BOSS', 'SP_GROUP_MINIBOSS', 'SP_GROUP_MONSTER', 'SP_GROUP_TRAINING']
SHOWN_ORDER = {3: 0, 0: 1, 1: 2, 2: 3}         # the training dummy first, then as before
BOSSBARS_H = os.path.join(sc.CPP, 'bossbars_data.h')
BOSSBARS_ICONS = os.path.join(sc.ASSETS, 'port_bossbars')    # gfx/ui/<...> lives at <...> here
CANVAS = 768
UI = os.path.join(sc.DATA, 'gfx', 'ui')

STANDING = re.compile(r'^(idle|walk|fly|float|move|hover|run)', re.I)
ENTRANCE = re.compile(r'(appear|spawn|death|die|dead|emerge|enter|exit|explode|burst|bombed)', re.I)
APPEAR = re.compile(r'^appear', re.I)
TETHERED = {'#MOMS_HEART', '#IT_LIVES', '#DADDY_LONG_LEGS', '#TRIACHNID', '#HANGER'}

# added by hand: (type, variant, subtype, group, names in the sets' order)
EXTRA = [
    (964, 0, 0, 3, ['Dummy', 'Манекен', 'Trainingspuppe', 'Muñeco', 'ダミー', '허수아비', '木桩']),
]

# the champion colours: the game has no names for them (stringtable, EID); the
# English are the wiki's (Champions, the "Appearance" column), the rest ours,
# each the plain words of its language for the same colour or look
CHAMP_NAMES = {
    'en': ['Red', 'Yellow', 'Green', 'Orange', 'Dark Blue', 'Dark Cyan', 'White', 'Gray', 'Translucent',
           'Black', 'Magenta', 'Violet', 'Dark Red', 'Light Blue', 'Camouflage', 'Pulsing Green',
           'Pulsing Gray', 'Light White', 'Small', 'Large', 'Pulsing Red', 'Pulsating', 'Crown', 'Skull',
           'Brown', 'Rainbow'],
    'ru': ['Красный', 'Жёлтый', 'Зелёный', 'Оранжевый', 'Тёмно-синий', 'Тёмно-бирюзовый', 'Белый', 'Серый',
           'Полупрозрачный', 'Чёрный', 'Пурпурный', 'Фиолетовый', 'Тёмно-красный', 'Голубой', 'Камуфляж',
           'Пульсирующий зелёный', 'Пульсирующий серый', 'Светло-белый', 'Маленький', 'Большой',
           'Пульсирующий красный', 'Пульсирующий', 'Корона', 'Череп', 'Коричневый', 'Радужный'],
    'de': ['Rot', 'Gelb', 'Grün', 'Orange', 'Dunkelblau', 'Dunkeltürkis', 'Weiß', 'Grau', 'Durchscheinend',
           'Schwarz', 'Magenta', 'Violett', 'Dunkelrot', 'Hellblau', 'Tarnung', 'Pulsierend grün',
           'Pulsierend grau', 'Hellweiß', 'Klein', 'Groß', 'Pulsierend rot', 'Pulsierend', 'Krone', 'Schädel',
           'Braun', 'Regenbogen'],
    'es': ['Rojo', 'Amarillo', 'Verde', 'Naranja', 'Azul oscuro', 'Cian oscuro', 'Blanco', 'Gris', 'Translúcido',
           'Negro', 'Magenta', 'Violeta', 'Rojo oscuro', 'Celeste', 'Camuflaje', 'Verde pulsante',
           'Gris pulsante', 'Blanco claro', 'Pequeño', 'Grande', 'Rojo pulsante', 'Pulsante', 'Corona',
           'Calavera', 'Marrón', 'Arcoíris'],
    'jp': ['赤', '黄', '緑', 'オレンジ', '紺', '青緑', '白', '灰', '半透明', '黒', 'マゼンタ', '紫', '暗い赤', '水色',
           '迷彩', '緑の脈動', '灰の脈動', '淡い白', '小', '大', '赤の脈動', '脈動', '王冠', 'ドクロ', '茶', '虹'],
    'kr': ['빨강', '노랑', '초록', '주황', '남색', '청록', '하양', '회색', '반투명', '검정', '자홍', '보라', '검붉은',
           '하늘색', '위장', '초록 맥동', '회색 맥동', '연한 하양', '작은', '큰', '빨강 맥동', '맥동', '왕관', '해골',
           '갈색', '무지개'],
    'zh': ['红色', '黄色', '绿色', '橙色', '深蓝', '深青', '白色', '灰色', '半透明', '黑色', '品红', '紫色', '暗红', '浅蓝',
           '迷彩', '绿色脉动', '灰色脉动', '淡白', '小型', '大型', '红色脉动', '脉动', '王冠', '骷髅', '棕色', '彩虹'],
}
# how the game animates a colour every frame (spawner.c reads the kind)
CHAMP_ANIM = {9: 'SP_CA_FLICKER', 14: 'SP_CA_CAMO', 15: 'SP_CA_PULSE', 16: 'SP_CA_PULSE', 20: 'SP_CA_PULSE',
              21: 'SP_CA_SIZE', 25: 'SP_CA_RAINBOW'}
CHAMP_TINY = 18

# bosscolors.xml suffixes
BOSS_COLOURS = ['red', 'grey', 'green', 'blue', 'black', 'yellow', 'pink', 'orange', 'cyan', 'brown']
BOSS_COLOUR_NAMES = {
    'en': ['Red', 'Gray', 'Green', 'Blue', 'Black', 'Yellow', 'Pink', 'Orange', 'Cyan', 'Brown'],
    'ru': ['Красный', 'Серый', 'Зелёный', 'Синий', 'Чёрный', 'Жёлтый', 'Розовый', 'Оранжевый', 'Бирюзовый',
           'Коричневый'],
    'de': ['Rot', 'Grau', 'Grün', 'Blau', 'Schwarz', 'Gelb', 'Rosa', 'Orange', 'Türkis', 'Braun'],
    'es': ['Rojo', 'Gris', 'Verde', 'Azul', 'Negro', 'Amarillo', 'Rosa', 'Naranja', 'Cian', 'Marrón'],
    'jp': ['赤', '灰', '緑', '青', '黒', '黄', 'ピンク', 'オレンジ', 'シアン', '茶'],
    'kr': ['빨강', '회색', '초록', '파랑', '검정', '노랑', '분홍', '주황', '청록', '갈색'],
    'zh': ['红色', '灰色', '绿色', '蓝色', '黑色', '黄色', '粉色', '橙色', '青色', '棕色'],
}


# ------------------------------------------------------------------ the list
def entities():
    rows = []
    for e in ET.parse(os.path.join(sc.DATA, 'entities2.xml')).getroot().findall('entity'):
        a = dict(e.attrib)
        b = e.find('bestiary')
        a['_bestiary'] = dict(b.attrib) if b is not None else None
        a['_type'] = int(a['id'])
        a['_var'] = int(a.get('variant', 0))
        a['_sub'] = int(a.get('subtype', 0))
        rows.append(a)
    return rows


def bestiary_of(a, byid):
    b = dict(a['_bestiary'])
    src = a
    if 'parent' in b:
        t, v = (int(x) for x in b.pop('parent').split('.'))
        src = byid[(t, v)]
        b = dict(src['_bestiary'] or {}, **b)
    tr = [float(x) if x else 0 for x in (b.get('transform', '0,0,1').split(',') + ['1'])[:3]]
    return b.get('anm2path', src['anm2path']), b.get('anim'), b.get('overlay'), tr


def boss_floor_order():
    # the file is not well-formed XML (a stray tag near its end): read it by pattern
    text = open(os.path.join(sc.DATA, 'bosspools.xml'), encoding='utf-8').read()
    order = {}
    for i, m in enumerate(re.finditer(r'<pool name="([^"]*)"(.*?)</pool>', text, re.S)):
        for bid in re.findall(r'<boss id="(\d+)"', m.group(2)):
            order.setdefault(bid, i)
    return order


def group(a):
    if '_group' in a:
        return a['_group']
    return 0 if a['boss'] == '1' and 'bossID' in a else 1 if a['boss'] == '1' else 2


def names_of(a, strings, fallbacks):
    key = a['name'][1:] if a['name'].startswith('#') else None
    got = strings.get(key) if key else None
    en = (got or {}).get('en') or a['name']
    out = []
    for lg in sc.LANGS:
        n = (got or {}).get(lg) or ''
        if not n:
            if lg != 'en':
                fallbacks.append((lg, en, 'no name in the stringtable'))
            n = en
        else:
            miss = sc.desc_missing(lg, n)
            if miss:
                fallbacks.append((lg, en, '%s lacks %s' % (sc.DESC_FACE[lg], ' '.join(
                    '%s U+%04X' % (c, ord(c)) for c in miss))))
                n = en
        out.append(n)
    return out


def checked_names(names, fallbacks, what):
    """Names of our own, each checked against its set's description face."""
    out = []
    for lg, n in zip(sc.LANGS, names):
        miss = sc.desc_missing(lg, n)
        if miss:
            fallbacks.append((lg, what + ' ' + names[0], '%s lacks %s' % (sc.DESC_FACE[lg], ' '.join(
                '%s U+%04X' % (c, ord(c)) for c in miss))))
            n = names[0]
        out.append(n)
    return out


def ordered():
    rows = entities()
    byid = dict(((a['_type'], a['_var']), a) for a in rows if a['_sub'] == 0)
    B = [a for a in rows if a['_bestiary'] is not None and 10 <= a['_type'] < 1000]
    for t, v, sub, g, names in EXTRA:
        a = dict(byid[(t, v)])
        a['_sub'], a['_group'], a['_names'] = sub, g, names
        B.append(a)
    floor = boss_floor_order()

    def key(a):
        g = group(a)
        if g == 0:
            return (SHOWN_ORDER[0], floor.get(a['bossID'], 100), int(a['bossID']), a['_type'], a['_var'], a['_sub'])
        return (SHOWN_ORDER[g], a['_type'], a['_var'], a['_sub'])
    B.sort(key=key)
    return B, byid


# ------------------------------------------------------------------ drawing a pose
def anim_len(a, anim):
    x = a.anims.get(anim)
    return int(x.get('FrameNum', 1)) if x is not None else 1


def drop_shadows(a):
    t = ET.parse(a.path).getroot()
    for l in t.iter('Layer'):
        if 'shadow' in (l.get('Name') or '').lower():
            a.layers[l.get('Id')] = None


def layer_names(a):
    return {l.get('Id'): (l.get('Name') or '') for l in ET.parse(a.path).getroot().iter('Layer')}


def frame(a, anim, ov, ox, oy, t):
    c = Image.new('RGBA', (CANVAS, CANVAS))
    a.draw(c, anim, t, ox, oy, 1.0, (CANVAS // 2, CANVAS // 2))
    if ov and ov in a.anims:
        a.draw(c, ov, t, ox, oy, 1.0, (CANVAS // 2, CANVAS // 2))
    return c


def ink(c):
    return int((np.asarray(c.getchannel('A')) > 128).sum())


def choose(a, banim):
    if 'Idle' in a.anims:
        return 'Idle', 0
    if banim in a.anims and STANDING.match(banim):
        return banim, 0
    if a.default in a.anims and not ENTRANCE.search(a.default or ''):
        return a.default, 0
    if a.default in a.anims and APPEAR.match(a.default):
        return a.default, anim_len(a, a.default) - 1
    return (banim if banim in a.anims else a.default), 0


def settle(a, anim, t, ov, ox, oy, banim):
    """the pose drawn; an empty or half-drawn one is replaced (survey5)"""
    n = anim_len(a, anim)
    c = frame(a, anim, ov, ox, oy, t)
    while not c.getbbox() and t + 1 < min(n, 120):
        t += 1
        c = frame(a, anim, ov, ox, oy, t)
    most = 0
    for tt in range(0, min(anim_len(a, banim), 60), 2):
        most = max(most, ink(frame(a, banim, ov, ox, oy, tt)))
    if not c.getbbox() or (most and ink(c) < 0.5 * most):
        frames = [frame(a, banim, ov, ox, oy, k) for k in range(min(anim_len(a, banim), 120))]
        inks = [ink(f) for f in frames]
        k = next(j for j, v in enumerate(inks) if v >= 0.85 * max(inks))
        anim, t = banim, k
    return anim, t


def signature(a):
    anims = tuple(sorted((k, int(v.get('FrameNum', 1))) for k, v in a.anims.items()))
    crops = []
    d = a.anims.get(a.default)
    if d is not None:
        for la in d.find('LayerAnimations').findall('LayerAnimation'):
            fr = la.find('Frame')
            if fr is not None:
                crops.append(tuple(fr.get(k) for k in ('Width', 'Height', 'XPivot', 'YPivot')))
    return anims, tuple(crops)


# ------------------------------------------------------------------ the champion sheets
def champion_sheet(path):
    """'<sheet>_champion.png' beside a sheet, as the game names it (0x6816dc)."""
    if not path:
        return None
    stem, ext = os.path.splitext(path)
    rel = os.path.relpath(stem + '_champion' + ext, [r for r in AR.ROOTS if path.startswith(r)][0])
    return AR.find(rel)


def with_sheets(a, how):
    """a copy of the .anm2's sheet table with every layer's picture made by how(path, is_champion_layer)"""
    names = layer_names(a)
    keep = dict(a.sheets)
    new = {}
    for sid, path in keep.items():
        new[sid] = path
    layers = dict(a.layers)
    for lid, sid in layers.items():
        if sid is None:
            continue
        nm = names.get(lid, '').lower()
        path = keep.get(sid)
        champ = champion_sheet(path) if path and 'hole' not in nm and 'dirt' not in nm else None
        key = 'PSEUDO::%s::%s::%s' % (how.__name__, lid, path)
        if key not in AR._sheets:
            AR._sheets[key] = how(path, champ)
        fake = 'L%s' % lid
        new[fake] = key
        layers[lid] = fake
    return new, layers


def opaque_mask_sheet(path, champ):
    """the champion layer as drawn by the game: its mask part made opaque"""
    if champ is None:
        return AR.sheet(path)
    im = AR.sheet(champ).copy()
    a = np.asarray(im).copy()
    m = (a[..., 3] > 0) & (a[..., 3] < 250)
    a[..., 3][m] = 255
    return Image.fromarray(a, 'RGBA')


def mask_sheet(path, champ):
    """white where the champion layer takes the colour, black (opaque as drawn) elsewhere"""
    src = AR.sheet(champ if champ is not None else path)
    a = np.asarray(src).copy()
    out = np.zeros_like(a)
    solid = a[..., 3] > 0
    out[..., 3][solid] = 255
    if champ is not None:
        m = solid & (a[..., 3] < 250)
        out[..., :3][m] = 255
    return Image.fromarray(out, 'RGBA')


def weight_sheet(path, champ):
    """how much of the champion colour a pixel takes, as coloroffset_champion.fs reads its
    alpha: (1 - a) * 2.00788 for a >= 0.5 (128 the whole colour, 255 none), nothing below
    (a halved alpha of its own, _build_champion.luax); grey in rgb, opaque where drawn"""
    src = AR.sheet(champ if champ is not None else path)
    a = np.asarray(src).copy()
    out = np.zeros_like(a)
    solid = a[..., 3] > 0
    out[..., 3][solid] = 255
    if champ is not None:
        al = a[..., 3].astype(np.float64)
        w = np.clip((255.0 - al) / 255.0 * 2.00788, 0.0, 1.0)
        w[al < 128] = 0.0
        g = np.round(w * 255.0).astype(np.uint8)
        for k in range(3):
            out[..., k][solid] = g[solid]
    return Image.fromarray(out, 'RGBA')


LUM = (0.212671, 0.715160, 0.072169)           # coloroffset_champion.fs, _lum


def lum_frame(colour, weight):
    """the champion layer's luminance, at its weight: rgb = dot(rgb, _lum), alpha = the weight
    - drawn over the sprite with tint * KColor at the KColor's alpha it makes the shader's
    mix(rgb, KColor * lum, KColor.a * weight)"""
    c = np.asarray(colour).astype(np.float64)
    w = np.asarray(weight)
    out = np.zeros(c.shape, np.uint8)
    l = np.round(c[..., 0] * LUM[0] + c[..., 1] * LUM[1] + c[..., 2] * LUM[2]).clip(0, 255).astype(np.uint8)
    for k in range(3):
        out[..., k] = l
    out[..., 3] = np.where(c[..., 3] > 0, w[..., 0], 0)
    return Image.fromarray(out, 'RGBA')


def has_champion_layers(a):
    names = layer_names(a)
    for lid, sid in a.layers.items():
        if sid is None:
            continue
        nm = names.get(lid, '').lower()
        if 'hole' in nm or 'dirt' in nm:
            continue
        if champion_sheet(a.sheets.get(sid)):
            return True
    return False


def frame_with(a, how, anim, ov, ox, oy, t):
    sheets, layers = with_sheets(a, how)
    keep = (a.sheets, a.layers)
    a.sheets, a.layers = sheets, layers
    try:
        return frame(a, anim, ov, ox, oy, t)
    finally:
        a.sheets, a.layers = keep


# ------------------------------------------------------------------ the overlay and the pulse
OVERLAY_NULL = 'OverlayEffect'     # where the game hangs a champion's crown or skull (render 0x69828c)
PULSE_MOST = 0.15                  # Pulsating: 1 + clamp(sin 0.2f, 0, 1) * 0.15 (render 0x6978c4)
PULSE_FRAMES = 6                   # at most: one a whole pixel of growth, up to this many


def overlay_point(a, anims):
    """the OverlayEffect null of the pose, as the game reads it: the raw keyframe at t
    (0x89f658 / 0x89f444); (x, y, scale %) from the entity's origin, or None when the
    .anm2 has no such null or it is hidden there"""
    root = ET.parse(a.path).getroot()
    nid = next((n.get('Id') for n in root.iter('Null') if n.get('Name') == OVERLAY_NULL), None)
    if nid is None:
        return None
    for anim, t in anims:
        node = a.anims.get(anim)
        if node is None:
            continue
        for na in node.iter('NullAnimation'):
            if na.get('NullId') != nid:
                continue
            if na.get('Visible') == 'false':
                return None
            fr = AR._frame_at(na.findall('Frame'), t)
            if fr is None or fr.get('Visible') == 'false':
                return None
            return (AR._f(fr, 'XPosition'), AR._f(fr, 'YPosition'), AR._f(fr, 'XScale', 100))
    return None


STATUS_ANM2 = 'gfx/StatusEffects.anm2'      # the game's own name for it (0x68c060)
OVERLAYS = ('Crown', 'Skull')                # champions 22 and 23


def overlay_frames(name, f):
    """the game's Crown or Skull, every frame of its timeline, brought down by f the way
    a body's icon is (mode: majority, no colour made up); answers (frames, pivot, ink)
    - the pivot inside a frame, the ink box (x0, y0, x1, y1) from the pivot over them all"""
    root = ET.parse(AR.find(STATUS_ANM2)).getroot()
    png = AR.find('gfx/' + root.find('.//Spritesheet').get('Path'))
    src = AR.sheet(png)
    anim = next(x for x in root.iter('Animation') if x.get('Name') == name)
    la = next(x for x in anim.iter('LayerAnimation') if x.get('Visible') != 'false' and x.findall('Frame'))
    frames, ink, pivot = [], None, None
    for fr in la.findall('Frame'):
        x, y, w, h = (int(AR._f(fr, k)) for k in ('XCrop', 'YCrop', 'Width', 'Height'))
        px, py = AR._f(fr, 'XPivot'), AR._f(fr, 'YPivot')
        cell = src.crop((x, y, x + w, y + h))
        if f > 1:
            if w % f or h % f:
                sys.exit('%s: a %dx%d cell does not divide by %d' % (name, w, h, f))
            cell = mode(cell, f)
        p = (px / f, py / f)
        if pivot is not None and p != pivot:
            sys.exit('%s: the pivot moves between frames' % name)
        pivot = p
        b = cell.getbbox()
        if b:
            box = (b[0] - p[0], b[1] - p[1], b[2] - p[0], b[3] - p[1])
            ink = box if ink is None else (min(ink[0], box[0]), min(ink[1], box[1]),
                                           max(ink[2], box[2]), max(ink[3], box[3]))
        frames += [cell] * int(AR._f(fr, 'Delay', 1))
    return frames, pivot, ink


def pulse_frames(pic, pivot):
    """Pulsating's sizes baked at 100 %: the icon grown about the entity's origin (the
    sprite's own scale), nearest texel as the game's renderer samples a sprite; one
    frame a whole pixel of growth on the longer side, up to PULSE_FRAMES. Answers
    [(image, dx, dy)], dx dy the frame's top left from the icon's"""
    n = max(1, min(PULSE_FRAMES, int(round(PULSE_MOST * max(pic.width, pic.height)))))
    src = np.asarray(pic)
    px, py = pivot
    out = []
    for k in range(1, n + 1):
        s = 1.0 + PULSE_MOST * k / n
        x0, y0 = int(np.floor(px - px * s)), int(np.floor(py - py * s))
        x1, y1 = int(np.ceil(px + (pic.width - px) * s)), int(np.ceil(py + (pic.height - py) * s))
        u = np.arange(x0, x1) + 0.5
        v = np.arange(y0, y1) + 0.5
        sx = np.floor(px + (u - px) / s).astype(int)
        sy = np.floor(py + (v - py) / s).astype(int)
        okx = (sx >= 0) & (sx < pic.width)
        oky = (sy >= 0) & (sy < pic.height)
        im = np.zeros((len(v), len(u), 4), np.uint8)
        im[np.ix_(oky, okx)] = src[np.ix_(sy[oky], sx[okx])]
        frame = Image.fromarray(im, 'RGBA')
        box = frame.getbbox() or (0, 0, 1, 1)
        out.append((frame.crop(box), x0 + box[0], y0 + box[1]))
    return out


# ------------------------------------------------------------------ sizes
def mode(im, f, cover=0.25):
    """every f x f block becomes its most frequent opaque colour (ties: the darkest),
    transparent when less than `cover` of it is opaque - no colour is made up"""
    w, h = -(-im.width // f) * f, -(-im.height // f) * f
    p = Image.new('RGBA', (w, h))
    p.paste(im, ((w - im.width) // 2, (h - im.height) // 2))
    a = np.asarray(p)
    out = np.zeros((h // f, w // f, 4), np.uint8)
    for y in range(h // f):
        for x in range(w // f):
            b = a[y * f:(y + 1) * f, x * f:(x + 1) * f].reshape(-1, 4)
            op = b[b[:, 3] > 128]
            if len(op) < cover * f * f:
                continue
            c = Counter(map(tuple, op))
            top = max(c.values())
            cand = [k for k, v in c.items() if v == top]
            out[y, x] = min(cand, key=lambda k: int(k[0]) + int(k[1]) + int(k[2]))
    return Image.fromarray(out, 'RGBA')


def solid_box(im):
    return im.getchannel('A').point(lambda v: 255 if v > 64 else 0).getbbox()


def untether_box(im, box):
    """the box with a hanging vessel/thread cut down to a stub (the body is the widest part)"""
    a = np.asarray(im.crop(box).getchannel('A')) > 128
    per = a.sum(axis=1)
    top = int(np.argmax(per))
    while top > 0 and per[top - 1] >= 0.7 * per.max():
        top -= 1
    return (box[0], box[1] + max(0, top - STUB), box[2], box[3])


def shaped(images, name, ms=False):
    """the same crop and the same whole-factor reduction for a set of images of one
    pose (the normal one first: it decides)"""
    n = images[0]
    box = solid_box(n)
    if box is None:
        return [None for _ in images], 1, None
    if name in TETHERED:
        box = untether_box(n, box)
    cut = [im.crop(box) for im in images]
    if ms:
        f = MS_FACTOR
    else:
        f = 1
        while max(-(-cut[0].width // f), -(-cut[0].height // f)) > BAND:
            f += 1
    pad = (0, 0)
    if f > 1:
        w, h = cut[0].width, cut[0].height
        pad = ((-(-w // f) * f - w) // 2, (-(-h // f) * f - h) // 2)     # as mode() centres it
        cut = [mode(im, f) for im in cut]
    b2 = cut[0].getbbox()
    if b2:
        cut = [im.crop(b2) for im in cut]
    b2 = b2 or (0, 0)

    def at(x, y):
        """a point of the canvas the pose was drawn on, in the icon's own pixels"""
        return ((x - box[0] + pad[0]) / f - b2[0], (y - box[1] + pad[1]) / f - b2[1])
    return cut, f, at


# ------------------------------------------------------------------ the Boss Bars icons
def bb_icon_files():
    text = open(BOSSBARS_H, encoding='utf-8').read()
    icons = re.findall(r'"([^"]+)"', text[text.index('BB_ICON[]'):text.index('};', text.index('BB_ICON[]'))])
    body = text[text.index('BB_BOSS[]'):]
    body = body[:body.index('};')]
    out = {}
    for t, v, _style, icon in re.findall(r'\{ (\d+), (\d+), (-?\d+), (-?\d+),', body):
        key, icon = (int(t), int(v)), int(icon)
        if icon >= 0 and key not in out:
            out[key] = icons[icon]
    return out


def icon_frame(anm2_path):
    """The first frame of a mod icon's .anm2 at 100 %: a 32 one as drawn, a larger one its ink."""
    rel = anm2_path[len('gfx/ui/'):] if anm2_path.startswith('gfx/ui/') else anm2_path
    path = os.path.join(BOSSBARS_ICONS, *rel.split('/'))
    if not os.path.exists(path):
        return None
    root = ET.parse(path).getroot()
    sheet = root.find('.//Spritesheet').get('Path')
    fr = root.find('.//LayerAnimation/Frame')
    x, y, w, h = (int(float(fr.get(k))) for k in ('XCrop', 'YCrop', 'Width', 'Height'))
    im = Image.open(os.path.join(os.path.dirname(path), sheet)).convert('RGBA').crop((x, y, x + w, y + h))
    if (w, h) != (32, 32):
        box = im.getbbox()
        im = im.crop(box) if box else im
    return im


# the mod hangs these bars on another part of the same boss, or keeps the
# picture without a row of its own (measured: the run printed them unmatched)
ICON_ALIAS = {
    (45, 0): ('as', (45, 10)),                   # Mom: the mod's row is her stomp, 45.10
    (79, 12): ('as', (79, 2)),                   # the Blighted Ovum's baby: the Ovum's face
    # Dogma: rows only for the TV and phase 2; dogma.png is the mod's dogma_icon.anm2
    # sheet, four 32 frames - its first
    (950, 0): ('file', ('bosshp_icons/final/dogma.png', (0, 0, 32, 32))),
}


def bossbars_icons():
    files = bb_icon_files()
    out = {}
    for key, f in files.items():
        im = icon_frame(f)
        if im is not None:
            out[key] = (im, f)
    for key, (kind, what) in ICON_ALIAS.items():
        if key in out:
            continue
        if kind == 'as' and what in out:
            out[key] = out[what]
        elif kind == 'file':
            path = os.path.join(BOSSBARS_ICONS, *what[0].split('/'))
            if os.path.exists(path):
                out[key] = (Image.open(path).convert('RGBA').crop(what[1]), None)
    return out


def boss_colours():
    """(type, variant) -> the bosscolors.xml suffixes, in order: colour k is subtype k + 1"""
    root = ET.parse(os.path.join(sc.DATA, 'bosscolors.xml')).getroot()
    out = {}
    for b in root.findall('boss'):
        sufs = []
        for c in b.findall('color'):
            s = c.get('suffix')
            if s is None:                        # two give a whole .anm2 instead: its name ends in the colour
                s = re.search(r'_([a-z]+)\.anm2$', c.get('anm2path', '').lower()).group(1)
            sufs.append(s.lstrip('_'))
        out[(int(b.get('id')), int(b.get('variant', 0)))] = sufs
    return out


def colour_icon(base_rel, suffix):
    """the Boss Bars icon of a boss colour: the mod names it <icon>_<suffix>"""
    if not base_rel:
        return None
    rel = re.sub(r'__([^_]+_icon[^/]*)\.anm2$', lambda m: '_%s__%s.anm2' % (suffix, m.group(1)), base_rel)
    if rel == base_rel:
        return None
    return icon_frame(rel)


# ------------------------------------------------------------------ the champion colour table
def champion_table():
    """the 26 entries the static initialiser 0x836314 writes at 0xac28f8 (champ_table.py
    in the mock-ups steps it instruction by instruction)"""
    path = os.path.join(sc.ROOT, 'tools', 'spawner_data', 'champion_colours.json')
    return json.load(open(path, encoding='utf-8'))


# ------------------------------------------------------------------ packing
class Atlas:
    def __init__(self, width=1024):
        self.width, self.x, self.y, self.row = width, 0, 0, 0
        self.items = []

    def add(self, im):
        if self.x + im.width > self.width:
            self.x, self.y, self.row = 0, self.y + self.row + 1, 0
        box = (self.x, self.y, im.width, im.height)
        self.items.append((im, box))
        self.x += im.width + 1
        self.row = max(self.row, im.height)
        return box

    def sheet(self):
        h = self.y + self.row
        out = Image.new('RGBA', (self.width, h), (0, 0, 0, 0))
        for im, (x, y, w, hh) in self.items:
            out.alpha_composite(im, (x, y))
        return out


EMPTY = Image.new('RGBA', (1, 1), (0, 0, 0, 0))


def main():
    strings = sc.stringtable(('Entities', 'Minibosses'))
    B, byid = ordered()
    bb = bossbars_icons()
    bcol = boss_colours()
    table = champion_table()
    ms_index = next(i for i, a in enumerate(B) if a['name'] == '#MOTHERS_SHADOW')

    # every body's .anm2, pose and family
    data = []
    sig_fam = {}
    for i, e in enumerate(B):
        if e['_bestiary'] is not None:
            anm, banim, ov, tr = bestiary_of(e, byid)
            ox, oy = tr[0], tr[1]
        else:
            anm, banim, ov, ox, oy = e['anm2path'], None, None, 0, 0
        a = AR.Anm2(anm)
        drop_shadows(a)
        banim = banim if banim in a.anims else a.default
        data.append(dict(a=a, ov=ov, ox=ox, oy=oy, banim=banim))
        sig_fam.setdefault((signature(a), group(e)), []).append(i)
    pose = {}
    for i, e in enumerate(B):
        d = data[i]
        fam = sig_fam[(signature(d['a']), group(e))]
        lead = fam[0]
        if lead != i and lead in pose and pose[lead][0] in d['a'].anims:
            pose[i] = pose[lead]
        else:
            anim, t = choose(d['a'], d['banim'])
            pose[i] = settle(d['a'], anim, t, d['ov'], d['ox'], d['oy'], d['banim'])

    atlas = Atlas()
    icons, champs, colours = [], [], []
    rows, fallbacks, empty = [], [], []
    stats = Counter()
    for i, e in enumerate(B):
        g = group(e)
        d = data[i]
        key = (e['_type'], e['_var'])
        can_champ = e.get('champion') == '1' and g in (1, 2)
        champ = -1
        if g in (0, 1) and key in bb:
            pic = bb[key][0]
            stats['Boss Bars'] += 1
            icon = len(icons)
            icons.append(atlas.add(pic))
            if can_champ:
                tiny = mode(pic, 2)
                tiny = tiny.crop(tiny.getbbox()) if tiny.getbbox() else EMPTY
                champ = len(champs)
                champs.append(dict(base=atlas.add(pic), mask=None, tbase=atlas.add(tiny), tmask=None,
                                   lum=None, layers=0,
                                   # a face, not a body: grown about its foot, the overlay on its top
                                   pivot=(pic.width / 2.0, float(pic.height)),
                                   crown=(pic.width / 2.0, 0.0), crown_kind=2, pic=pic, shrink=1))
                stats['overlay: Boss Bars face, on its top'] += 1
        else:
            anim, t = pose[i]
            a = d['a']
            normal = frame(a, anim, d['ov'], d['ox'], d['oy'], t)
            images = [normal]
            layered = can_champ and has_champion_layers(a)
            if layered:
                images.append(frame_with(a, opaque_mask_sheet, anim, d['ov'], d['ox'], d['oy'], t))
                images.append(frame_with(a, mask_sheet, anim, d['ov'], d['ox'], d['oy'], t))
                images.append(frame_with(a, weight_sheet, anim, d['ov'], d['ox'], d['oy'], t))
            cut, f, at = shaped(images, e['name'], ms=(i == ms_index))
            pic = cut[0]
            if pic is None:
                empty.append(e['name'])
                pic = EMPTY
            stats['/%d' % f if f > 1 else '1:1'] += 1
            icon = len(icons)
            icons.append(atlas.add(pic))
            if can_champ:
                if layered:
                    C, M = cut[1], cut[2]
                    m = (np.asarray(M)[..., 0] > 128) & (np.asarray(M)[..., 3] > 0)
                    c = np.asarray(C).copy()
                    base = c.copy()
                    base[m] = 0
                    mask = np.zeros_like(c)
                    mask[m] = c[m]
                    base_im, mask_im = Image.fromarray(base, 'RGBA'), Image.fromarray(mask, 'RGBA')
                    tb, tm = mode(base_im, 2), mode(mask_im, 2)
                    lum_im = lum_frame(C, cut[3])
                    stats['champion layers'] += 1
                else:
                    base_im, mask_im, lum_im = pic, None, None
                    tb, tm = mode(pic, 2), None
                tbox = tb.getbbox()
                if tbox and tm is not None and tm.getbbox():
                    mb = tm.getbbox()
                    tbox = (min(tbox[0], mb[0]), min(tbox[1], mb[1]), max(tbox[2], mb[2]), max(tbox[3], mb[3]))
                tb = tb.crop(tbox) if tbox else EMPTY
                tm = tm.crop(tbox) if (tm is not None and tbox) else None
                origin = (CANVAS // 2 + d['ox'], CANVAS // 2 + d['oy'])
                pivot = at(*origin)
                null = overlay_point(a, [(anim, t)] + ([(d['ov'], t)] if d['ov'] else []))
                crown, crown_kind = None, 0
                if null is not None:
                    crown, crown_kind = at(origin[0] + null[0], origin[1] + null[1]), 1
                    stats['overlay null'] += 1
                    if null[2] != 100:
                        stats['overlay null not at 100 %%: %d' % null[2]] += 1
                    if f > 1:
                        stats['overlay on a reduced body (/%d)' % f] += 1
                else:
                    stats['no overlay null (the game draws none)'] += 1
                champ = len(champs)
                champs.append(dict(pivot=pivot, crown=crown, crown_kind=crown_kind, pic=pic, shrink=f,
                                   base=atlas.add(base_im), mask=atlas.add(mask_im) if mask_im is not None else None,
                                   tbase=atlas.add(tb), tmask=atlas.add(tm) if tm is not None else None,
                                   lum=atlas.add(lum_im) if lum_im is not None else None,
                                   layers=1 if mask_im is not None else 0))
        # the boss colours
        cfirst, cn = len(colours), 0
        if g in (0, 1) and key in bcol:
            base_rel = bb.get(key, (None, None))[1]
            for k, suf in enumerate(bcol[key]):
                im = colour_icon(base_rel, suf)
                if im is None:                   # the mod has no picture of this colour: the boss's own
                    im = pic
                    stats['colour without its icon'] += 1
                colours.append(dict(sub=k + 1, name=BOSS_COLOURS.index(suf), box=atlas.add(im)))
                cn += 1
        names = (checked_names(e['_names'], fallbacks, 'name') if '_names' in e
                 else names_of(e, strings, fallbacks))
        rows.append(dict(t=e['_type'], v=e['_var'], s=e['_sub'], g=g, icon=icon, champ=champ,
                         cfirst=cfirst if cn else -1, cn=cn, names=names))

    # the overlay on a body brought down by 2: brought down by 2 as well (the owner, 05.10.2026:
    # "even" - the same size against the body as on the full-size ones), drawn at 100 %
    over_full, over_half, over_anims = [], [], []
    for name in OVERLAYS:
        fr1, p1, ink1 = overlay_frames(name, 1)
        fr2, p2, ink2 = overlay_frames(name, 2)
        boxes, seen = [], {}
        for im in fr2:
            k = im.tobytes()
            if k not in seen:
                seen[k] = atlas.add(im)
            boxes.append(seen[k])
        over_anims.append((name + 'Half', boxes))
        over_full.append((name, len(fr1), (0, 0), ink1))
        over_half.append((name + 'Half', len(fr2), p2, ink2))
    for c in champs:
        if c['crown_kind'] == 1 and c['shrink'] not in (1, 2):
            sys.exit('an overlay on a body brought down by %d: no overlay baked for it' % c['shrink'])
    sheet = atlas.sheet()
    png = 'port_enemy_icons.png'
    sc.save_png(sheet, os.path.join(sc.ASSETS, png))
    champ_frames, lum_frames = [], []
    for c in champs:
        for k in ('base', 'mask', 'tbase', 'tmask'):
            champ_frames.append(c[k] if c[k] is not None else (0, 0, 1, 1))
        lum_frames.append(c['lum'] if c['lum'] is not None else (0, 0, 1, 1))
    sc.write_text(os.path.join(sc.ASSETS, 'port_enemy_icons.anm2'), sc.anm2(png, [
        ('Icons', icons), ('Champ', champ_frames or [(0, 0, 1, 1)]),
        ('Colours', [c['box'] for c in colours] or [(0, 0, 1, 1)]),
        ('Lum', lum_frames or [(0, 0, 1, 1)])] + over_anims, '05/10/2026'))

    # Pulsating's sizes: a sheet of their own, loaded only when one is on the card
    pulse_atlas, pulse_boxes, pulse_at = Atlas(), [], []
    for c in champs:
        c['pulse_at'], c['pulse_n'] = len(pulse_at), 0
        for im, dx, dy in pulse_frames(c['pic'], c['pivot']):
            pulse_boxes.append(pulse_atlas.add(im))
            pulse_at.append((dx, dy))
            c['pulse_n'] += 1
    pulse_png = 'port_enemy_pulse.png'
    pulse_sheet = pulse_atlas.sheet()
    sc.save_png(pulse_sheet, os.path.join(sc.ASSETS, pulse_png))
    sc.write_text(os.path.join(sc.ASSETS, 'port_enemy_pulse.anm2'),
                  sc.anm2(pulse_png, [('Pulse', pulse_boxes or [(0, 0, 1, 1)])], '05/10/2026'))

    champ_names = [checked_names([CHAMP_NAMES[lg][k] for lg in sc.LANGS], fallbacks, 'champion')
                   for k in range(26)]
    colour_names = [checked_names([BOSS_COLOUR_NAMES[lg][k] for lg in sc.LANGS], fallbacks, 'boss colour')
                    for k in range(len(BOSS_COLOURS))]
    paper = Image.open(os.path.join(UI, 'popup.png')).convert('RGBA')
    paper_rgb = Counter(paper.getpixel((x, 12)) for x in range(8, 215)).most_common(1)[0][0][:3]

    counts = [sum(1 for r in rows if r['g'] == g) for g in range(4)]
    def f(v):
        t = '%.6g' % v
        return t + ('' if '.' in t or 'e' in t else '.0') + 'f'
    L = ['/* Written by tools/spawner_enemies.py - do not edit by hand, run the tool. */',
         '#ifndef SPAWNER_ENEMIES_H', '#define SPAWNER_ENEMIES_H', '',
         '/*',
         " * Every monster of the game's bestiary, in the order the list shows them:",
         ' * the training dummy, bosses by the floor that first holds them, then',
         ' * minibosses, then monsters. gfx/port_enemy_icons.anm2, every frame at 100 %,',
         ' * pivot top left, its size here:',
         ' *   "Icons"   icon = frame of the list row (w x h)',
         ' *   "Champ"   champion-able: frames 4c+0 base, 4c+1 mask (the part that takes',
         ' *             the champion colour; a 1 x 1 stand-in when layers is 0), 4c+2 and',
         ' *             4c+3 the same brought down by 2 (TINY)',
         ' *   "Colours" a boss colour\'s Boss Bars icon; subtype = colour + 1',
         " * name[] in the sets' order (en ru de es jp kr zh).",
         ' */',
         '#define SP_GROUP_BOSS 0', '#define SP_GROUP_MINIBOSS 1', '#define SP_GROUP_MONSTER 2',
         '#define SP_GROUP_TRAINING 3', '',
         '#define SP_ENEMY_COLUMN %d' % COL,
         '#define SP_ENEMIES %d      /* bosses %d, minibosses %d, monsters %d, training %d */'
         % (len(rows), counts[0], counts[1], counts[2], counts[3]),
         '#define SP_CHAMPABLE %d' % len(champs),
         '#define SP_BOSS_COLOURS %d' % len(colours),
         '#define SP_PAPER_R %d' % paper_rgb[0], '#define SP_PAPER_G %d' % paper_rgb[1],
         '#define SP_PAPER_B %d' % paper_rgb[2], '',
         'typedef struct { unsigned char w, h; } sp_size_t;', '',
         'static const struct {', '    unsigned short type, variant, subtype;', '    unsigned char group;',
         '    short icon;', '    unsigned char w, h;', '    short champ;              /* SP_CHAMP_ART row, -1 none */',
         '    short colours, colours_n; /* SP_BOSS_COLOUR rows */',
         '    const char *name[7];', '} SP_ENEMY[SP_ENEMIES] = {']
    for r in rows:
        w, h = icons[r['icon']][2], icons[r['icon']][3]
        L.append('    { %d, %d, %d, %s, %d, %d, %d, %d, %d, %d, { %s } },' % (
            r['t'], r['v'], r['s'], GROUP[r['g']], r['icon'], w, h, r['champ'], r['cfirst'], r['cn'],
            ', '.join(sc.c_str(n) for n in r['names'])))
    L += ['};', '',
          '/*',
          ' * per champion-able enemy: does it have champion layers (a mask), the sizes;',
          " * Pulsating's sizes (gfx/port_enemy_pulse.anm2 \"Pulse\", frames pulse_at ..",
          " * pulse_at + pulse_n - 1, growing; their top left in SP_PULSE from the icon's);",
          ' * where the game hangs the crown or the skull (its OverlayEffect null, the',
          " * icon's own pixels from its top left): overlay 1 the body's null, 2 a Boss",
          ' * Bars face (its top middle, a stand-in), 0 none - the game draws none;',
          ' * shrink the whole factor the icon was brought down by (2: the half overlay)',
          ' */',
          'static const struct {',
          '    unsigned char layers; sp_size_t base, tiny;',
          '    short pulse_at; unsigned char pulse_n;',
          '    unsigned char overlay; signed char ox, oy; unsigned char shrink;',
          '} SP_CHAMP_ART[%d] = {' % max(1, len(champs))]
    for c in champs:
        ox, oy = c['crown'] if c['crown'] is not None else (0, 0)
        L.append('    { %d, { %d, %d }, { %d, %d }, %d, %d, %d, %d, %d, %d },' % (
            c['layers'], c['base'][2], c['base'][3], c['tbase'][2], c['tbase'][3],
            c['pulse_at'], c['pulse_n'], c['crown_kind'], int(round(ox)), int(round(oy)), c['shrink']))
    if not champs:
        L.append('    { 0, { 1, 1 }, { 1, 1 }, 0, 0, 0, 0, 0, 1 },')
    L += ['};', '',
          '/*',
          " * The crown (22) and the skull (23): [0] the game's own animation of",
          ' * gfx/StatusEffects.anm2, drawn with its own pivot (px, py nought); [1] the same',
          ' * brought down by 2 for a half-size body, port_enemy_icons.anm2 "<anim>Half",',
          ' * frame t of the timeline, the pivot at (px, py) of the frame. frames: the',
          ' * timeline; x0 y0 x1 y1 the ink from the pivot over every frame (measured).',
          ' */',
          'static const struct { const char *anim; unsigned char frames; signed char px, py, x0, y0, x1, y1; }',
          '    SP_OVERLAY[2][2] = {']
    for k in range(len(OVERLAYS)):
        cells = []
        for name, n, p, ink in (over_full[k], over_half[k]):
            cells.append('{ "%s", %d, %d, %d, %d, %d, %d, %d }' % (name, n, int(p[0]), int(p[1]), *(int(round(v)) for v in ink)))
        L.append('    { %s },' % ', '.join(cells))
    L += ['};', '', '#define SP_PULSES %d' % max(1, len(pulse_at)),
          'static const struct { signed char dx, dy; } SP_PULSE[SP_PULSES] = {']
    L += ['    { %d, %d },' % xy for xy in pulse_at] or ['    { 0, 0 },']
    L += ['};', '',
          'static const struct { unsigned char subtype, name; sp_size_t size; } SP_BOSS_COLOUR[%d] = {'
          % max(1, len(colours))]
    for c in colours:
        L.append('    { %d, %d, { %d, %d } },   /* %s */' % (c['sub'], c['name'], c['box'][2], c['box'][3],
                                                         BOSS_COLOURS[c['name']]))
    L += ['};', '',
          '/*',
          ' * The 26 champion colours, the table 0xac28f8 the game builds at 0x836314:',
          ' * kcolor (+0) the champion colour, then the Color the entity wears (+0x14):',
          ' * tint rgba and offset rgb. anim: how the game changes it every frame.',
          ' */',
          '#define SP_CA_STILL 0', '#define SP_CA_FLICKER 1', '#define SP_CA_CAMO 2', '#define SP_CA_PULSE 3',
          '#define SP_CA_SIZE 4', '#define SP_CA_RAINBOW 5', '#define SP_CHAMP_TINY %d' % CHAMP_TINY, '',
          'static const struct {', '    float kcolor[4], tint[4], offset[3];', '    unsigned char anim;',
          '    const char *name[7];', '} SP_CHAMP_COLOUR[26] = {']
    for k, e in enumerate(table):
        kc = e['kcolor'] or [0, 0, 0, 0]
        col = e['color'] or {'tint': [1, 1, 1, 1], 'offset': [0, 0, 0]}
        L.append('    { { %s }, { %s }, { %s }, %s, { %s } },' % (
            ', '.join(f(v) for v in kc), ', '.join(f(v) for v in col['tint']),
            ', '.join(f(v) for v in col['offset']), CHAMP_ANIM.get(k, 'SP_CA_STILL'),
            ', '.join(sc.c_str(n) for n in champ_names[k])))
    L += ['};', '', 'static const char *const SP_BOSS_COLOUR_NAME[%d][7] = {' % len(BOSS_COLOURS)]
    for names in colour_names:
        L.append('    { %s },' % ', '.join(sc.c_str(n) for n in names))
    L += ['};', '', '#endif', '']
    sc.write_text(HEADER, chr(10).join(L))

    print('enemies %d: bosses %d, minibosses %d, monsters %d, training %d; atlas %dx%d'
          % (len(rows), counts[0], counts[1], counts[2], counts[3], sheet.width, sheet.height))
    print('icons:', dict(stats))
    print('champion-able %d (with champion layers %d); boss colours %d'
          % (len(champs), sum(c['layers'] for c in champs), len(colours)))
    print("Mother's Shadow", icons[rows[ms_index]['icon']][2:])
    print('pulse frames %d, sheet %dx%d' % (len(pulse_at), pulse_sheet.width, pulse_sheet.height))
    if empty:
        print('NO PICTURE: %s' % ', '.join(empty))
    print("English instead of the set's own name: %d" % len(fallbacks))
    for lg, en, why in fallbacks:
        print('  %s  %-28s %s' % (lg, en, why))


if __name__ == '__main__':
    main()
