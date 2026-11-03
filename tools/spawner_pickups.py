# -*- coding: utf-8 -*-
"""The spawner's ITEMS list beyond the collectibles (the owner, 07.10.2026,
mock-ups v2): keys, bombs, coins, hearts, batteries and the loot - sacks,
chests, troll bombs - with their names in the seven sets and their pictures;
the pills' and the golden trinkets' pictures; the STATS tab's new rows (the
counters and the health) with the HUD's own pictures; and the card's new words.

WHERE IT COMES FROM
  tools/spawner_data/pickups.json      the research table (07.10.2026): type,
      variant, subtype, and each name per set with its source - the game's
      stringtable, the EID mod's PickupNames, and for Russian the words EID's
      Russian descriptions already use (the owner approved the suggested ones)
  tools/spawner_data/pickup_icons/     the pickups' idle frames composited at
      100 % from the game's own .anm2 (chests closed, coins at rest), and the
      HUD's heart and counter frames (gfx/ui/ui_hearts.png, hudpickups.png) -
      cut by other stuff/spawner/research/build_research.py / build_hud.py
  the game's pill sprites (gfx/005.071_pill blue-blue.anm2 and the horse and
      gold ones) and its trinket pictures (gfx/items/trinkets)

THE GOLDEN TRINKETS wear the game's own golden look: shaders/coloroffset_gold.fs
applied here, texel by texel, at the shimmer's resting phase (ColorizeOut.a =
0, TextureSize the trinket's own 32 x 32), then brought down to the list's 24
by LANCZOS - the list draws every picture at 100 %.

THE PICTURES are drawn at 100 %: a pickup larger than the list's column
(32 x 24) is brought down here, LANCZOS (the giga bomb, the mega chest, the key
ring); the rest are the game's own pixels.

OUTPUT
  assets/port_pickup_icons.png/.anm2   "Icons" (SP_PICKUP order), "Pills" (pill,
      horse pill, gold pill, gold horse pill), "Golden" (trinket id - 1),
      "Health" (SP_HEALTH order), "Counters" (coins, bombs, keys)
  cpp/spawner_pickups.h

    py -3 tools/spawner_pickups.py
"""
import json
import math
import os
import sys

from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import spawner_common as sc    # noqa: E402

DATA_DIR = os.path.join(HERE, 'spawner_data')
ICON_DIR = os.path.join(DATA_DIR, 'pickup_icons')
GFX = os.path.join(sc.DATA, 'gfx')
PNG = 'port_pickup_icons.png'
HEADER = os.path.join(sc.CPP, 'spawner_pickups.h')
BOX_W, BOX_H = 32, 24                 # the list's picture column and row
TRINKET = 24                          # a trinket's picture in the list
SHEET_W = 512

# groups, in the list's order; the story chests (Big Chest, Mom's Chest) stay out (the owner)
GROUPS = [('PICKUPS/keys', 'SP_G_KEYS'), ('PICKUPS/bombs', 'SP_G_BOMBS'), ('PICKUPS/coins', 'SP_G_COINS'),
          ('PICKUPS/hearts', 'SP_G_HEARTS'), ('PICKUPS/batteries', 'SP_G_BATTERIES'),
          ('LOOT/sacks', 'SP_G_LOOT'), ('LOOT/chests', 'SP_G_LOOT'), ('LOOT/troll bombs', 'SP_G_LOOT')]

# the STATS rows of the health, in the order they are listed; the picture (pickup_icons) and the name per set
HEALTH = [
    ('CONTAINERS', 'heart_empty.png',
     ['Heart containers', 'Контейнеры сердец', 'Herzcontainer', 'Contenedores', 'ハートのうつわ', '하트 용기', '心之容器']),
    ('RED', 'heart_red_full.png',
     ['Red hearts', 'Красные сердца', 'Rote Herzen', 'Corazones rojos', '赤ハート', '빨간 하트', '红心']),
    ('SOUL', 'heart_soul_full.png',
     ['Soul hearts', 'Синие сердца', 'Seelenherzen', 'Corazones de alma', 'ソウルハート', '소울 하트', '魂心']),
    ('BLACK', 'heart_black_full.png',
     ['Black hearts', 'Чёрные сердца', 'Schwarze Herzen', 'Corazones negros', 'ブラックハート', '블랙 하트', '黑心']),
    ('BONE', 'heart_bone_empty.png',
     ['Bone hearts', 'Костяные сердца', 'Knochenherzen', 'Corazones de hueso', 'ボーンハート', '뼈 하트', '骨心']),
    ('BROKEN', 'heart_broken.png',
     ['Broken hearts', 'Разбитые сердца', 'Gebrochene Herzen', 'Corazones rotos', 'こわれたハート', '부서진 하트', '碎心']),
    ('ETERNAL', 'heart_eternal.png',
     ['Eternal heart', 'Вечное сердце', 'Ewiges Herz', 'Corazón eterno', 'エターナルハート', '이터널 하트', '永恒之心']),
    ('GOLDEN', 'heart_golden.png',
     ['Golden hearts', 'Золотые сердца', 'Goldene Herzen', 'Corazones dorados', 'ゴールデンハート', '황금 하트', '金心']),
    ('ROTTEN', 'heart_rotten.png',
     ['Rotten hearts', 'Гнилые сердца', 'Verrottete Herzen', 'Corazones podridos', 'くさったハート', '썩은 하트', '腐心']),
    ('SOUL_CHARGE', 'charge_soul.png',
     ['Soul charges', 'Заряды души', 'Seelenladungen', 'Cargas de alma', 'ソウルチャージ', '소울 충전', '魂之充能']),
    ('BLOOD_CHARGE', 'charge_blood.png',
     ['Blood charges', 'Заряды крови', 'Blutladungen', 'Cargas de sangre', 'ブラッドチャージ', '피 충전', '血之充能']),
    ('COIN_CONTAINERS', 'heart_coin_empty.png',
     ['Coin containers', 'Монетные контейнеры', 'Münzcontainer', 'Contenedores de moneda', 'コインのうつわ', '동전 용기',
      '硬币容器']),
    ('COIN_HEARTS', 'heart_coin_full.png',
     ['Coin hearts', 'Монетные сердца', 'Münzherzen', 'Corazones de moneda', 'コインハート', '동전 하트', '硬币心']),
    ('COIN_BROKEN', 'heart_coin_broken.png',
     ['Broken hearts', 'Разбитые сердца', 'Gebrochene Herzen', 'Corazones rotos', 'こわれたハート', '부서진 하트', '碎心']),
]
COUNTERS = [('hud_coin.png', ['Coins', 'Монеты', 'Münzen', 'Monedas', 'コイン', '동전', '硬币']),
            ('hud_bomb.png', ['Bombs', 'Бомбы', 'Bomben', 'Bombas', 'ボム', '폭탄', '炸弹']),
            ('hud_key.png', ['Keys', 'Ключи', 'Schlüssel', 'Llaves', 'カギ', '열쇠', '钥匙'])]

# the card's new words, in the order of the sets (en ru de es jp kr zh); ON / OFF are the options' own
WORDS = {
    'SP_GROUND': ['On the ground:', 'На полу:', 'Auf den Boden:', 'Al suelo:', 'ゆかに置く:', '바닥에:', '放在地上:'],
    'SP_ON': ['ON', 'ВКЛ', 'AN', 'SÍ', 'オン', '켜짐', '开'],
    'SP_OFF': ['OFF', 'ВЫКЛ', 'AUS', 'NO', 'オフ', '꺼짐', '关'],
    'SP_HORSE_FMT': ['%s (horse)', '%s (лошадиная)', '%s (Pferd)', '%s (caballo)', '%s (ウマ)', '%s (말)', '%s (马)'],
    'SP_GOLD_FMT': ['%s (gold)', '%s (золото)', '%s (golden)', '%s (dorado)', '%s (ゴールド)', '%s (황금)', '%s (金)'],
    'SP_CHARGE': ['CHARGE ACTIVE ITEMS', 'ЗАРЯДИТЬ АКТИВНЫЕ ПРЕДМЕТЫ', 'AKTIVE ITEMS LADEN', 'CARGAR ACTIVOS',
                  'アクティブをじゅうでん', '액티브 충전', '充满主动道具'],
    'SP_CHARGED': ['Charged', 'Заряжено', 'Geladen', 'Cargado', 'じゅうでんした', '충전됨', '已充满'],
    'SP_NO_ACTIVE': ['No active item', 'Нет активного предмета', 'Kein aktives Item', 'Sin objeto activo',
                     'アクティブがない', '액티브 아이템 없음', '没有主动道具'],
    'SP_SOUL_FMT': ['%s: %s', '%s: %s', '%s: %s', '%s: %s', '%s: %s', '%s: %s', '%s: %s'],
    'SP_LIMIT_FMT': ['the game allows %s', 'игра даёт не больше: %s', 'das Spiel erlaubt %s', 'el juego permite %s',
                     'ゲームの上限: %s', '게임 한도: %s', '游戏上限：%s'],
    # ONLY HELD, the held count's cross, the teleporter's two clears (the owner, 21.10.2026)
    'SP_HELD': ['Only held:', 'Только мои:', 'Nur eigene:', 'Solo míos:', 'もってるだけ:', '보유만:', '仅已拥有:'],
    'SP_CROSS': ['×', '×', '×', '×', '×', '×', '×'],
    'SP_CLEAR_ENEMIES': ['CLEAR ENEMIES', 'УБРАТЬ ВРАГОВ', 'GEGNER WEG', 'QUITAR ENEMIGOS', 'てきをけす', '적 제거',
                         '清除敌人'],
    'SP_CLEAR_PICKUPS': ['CLEAR PICKUPS', 'УБРАТЬ ПРЕДМЕТЫ', 'ITEMS WEG', 'QUITAR OBJETOS', 'アイテムをけす', '아이템 제거',
                         '清除物品'],
    'SP_NOTHING_CLEAR': ['Nothing to clear', 'Убирать нечего', 'Nichts zu entfernen', 'No hay nada que quitar',
                         'けすものがない', '제거할 것이 없음', '没有可清除的'],
}
SETS = sc.LANGS


def icon(name):
    return Image.open(os.path.join(ICON_DIR, name)).convert('RGBA')


def trimmed(im):
    box = im.getchannel('A').getbbox()
    return im.crop(box) if box else im


def fit(im, w=BOX_W, h=BOX_H):
    """Brought down to the box by LANCZOS when larger; the game's own pixels otherwise."""
    if im.width <= w and im.height <= h:
        return im
    k = min(w / im.width, h / im.height)
    return im.resize((max(1, round(im.width * k)), max(1, round(im.height * k))), Image.LANCZOS)


# ------------------------------------------------------------------ the golden look
_LUM = (0.212671, 0.715160, 0.072169)


def _cubic(x, a1, b1, a2, b2, xt):
    return a1 * x ** 3 + b1 * x if x < xt else a2 * (x - 1.0) ** 3 + b2 * (x - 1.0) + 1.0


def golden(im):
    """shaders/coloroffset_gold.fs, texel by texel (Color0 = 1, ColorizeOut.a = 0,
    no pixelation, no colour offset)."""
    src = im.convert('RGBA')
    W, H = src.size
    out = Image.new('RGBA', src.size, (0, 0, 0, 0))
    ps, po = src.load(), out.load()
    for y in range(H):
        for x in range(W):
            r, g, b, a = (c / 255.0 for c in ps[x, y])
            if a == 0.0:
                continue
            u, v = (x + 0.5) / W, (y + 0.5) / H
            t = math.pi * (u * (W / H) + v)
            t = 1.0 - max(0.0, math.sin(t)) ** 20 - math.sin(t) / 4.0
            lum = r * _LUM[0] + g * _LUM[1] + b * _LUM[2]
            add = (-0.2 * t + 0.2) if -lum >= -0.007 else 0.0     # step(-lum, -0.007)
            r = min(1.0, max(0.0, r * (2.0 - t) + add))
            g = min(1.0, max(0.0, g * (1.8 - t) + add))
            b = min(1.0, max(0.0, b * (1.0 - t) + add))
            r = min(1.0, max(0.0, _cubic(r, -6.52644, 3.05795, 1.36096, -0.320444, 0.172549)))
            g = min(1.0, max(0.0, _cubic(g, -1.65228, 2.07291, 1.138, 0.0512927, 0.407843)))
            b = min(1.0, max(0.0, _cubic(b, 4.86766, -3.82087, -44.7824, 9.35043, 0.901961)))
            po[x, y] = (round(r * 255), round(g * 255), round(b * 255), round(a * 255))
    return out


# ------------------------------------------------------------------ the sheet
def pack(groups):
    """[(name, [pictures])] onto one sheet in rows; answers (sheet, {name: [(x,y,w,h)]})."""
    x = y = row_h = 0
    places = {}
    for name, pics in groups:
        boxes = []
        for p in pics:
            if x + p.width > SHEET_W:
                x, y, row_h = 0, y + row_h + 1, 0
            boxes.append((x, y, p.width, p.height))
            x += p.width + 1
            row_h = max(row_h, p.height)
        places[name] = boxes
    H = y + row_h
    sheet = Image.new('RGBA', (SHEET_W, H), (0, 0, 0, 0))
    for name, pics in groups:
        for p, (bx, by, _w, _h) in zip(pics, places[name]):
            sheet.alpha_composite(p, (bx, by))
    return sheet, places


def pill(name):
    import xml.etree.ElementTree as ET
    path = os.path.join(GFX, name)
    root = ET.parse(path).getroot()
    sheet = Image.open(os.path.join(GFX, root.find('Content/Spritesheets/Spritesheet').get('Path'))).convert('RGBA')
    for a in root.iter('Animation'):
        if a.get('Name') != 'Idle':
            continue
        for la in a.iter('LayerAnimation'):
            f = next(la.iter('Frame'), None)
            if f is not None and la.get('Visible', 'true') == 'true':
                x, y, w, h = (int(f.get(k)) for k in ('XCrop', 'YCrop', 'Width', 'Height'))
                return trimmed(sheet.crop((x, y, x + w, y + h)))
    raise SystemExit('no Idle frame in ' + name)


def trinkets():
    import xml.etree.ElementTree as ET
    out = {}
    for e in ET.parse(os.path.join(sc.DATA, 'items.xml')).getroot():
        if e.tag == 'trinket':
            p = os.path.join(GFX, 'items', 'trinkets', e.get('gfx').lower())
            if not os.path.isfile(p):
                p = os.path.join(GFX, 'items', 'trinkets', e.get('gfx'))
            im = Image.open(p).convert('RGBA')
            # a strip of states (Monkey Paw: 128 x 32, a finger fewer each) - the
            # game's trinket frame is the first square, as the plain one shows
            out[int(e.get('id'))] = im.crop((0, 0, im.height, im.height)) if im.width > im.height else im
    return out


def check_words(lg_i, lg, words):
    bad = [(w, sc.desc_missing(lg, w)) for w in words]
    bad = [(w, m) for w, m in bad if m]
    if bad:
        raise SystemExit('the %s description face lacks: %s' % (lg, '; '.join(
            '%s -> %s' % (w, ' '.join('U+%04X' % ord(c) for c in m)) for w, m in bad)))


def main():
    table = json.load(open(os.path.join(DATA_DIR, 'pickups.json'), encoding='utf-8'))
    rows = []
    for group, gname in GROUPS:
        for e in table['entries']:
            if e['group'] != group:
                continue
            names = [e['names'][lg]['text'] for lg in SETS]
            sub = e['spawn_subtype'] if e['spawn_subtype'] is not None else (e['subtype'] or 0)
            key = '%d.%d.%d' % (e['type'], e['variant'], e['subtype'] if e['subtype'] is not None else 1)
            if not os.path.isfile(os.path.join(ICON_DIR, key + '.png')):
                key = '%d.%d.0' % (e['type'], e['variant'])
            rows.append(dict(t=e['type'], v=e['variant'], s=sub, g=gname, names=names,
                             pic=fit(trimmed(icon(key + '.png')))))
    if any(r['t'] == 5 and r['v'] in (340, 390) for r in rows):
        raise SystemExit('a story chest got into the list')

    pills = [fit(pill('005.071_pill blue-blue.anm2')), fit(pill('005.071_horse pill blue-blue.anm2')),
             fit(pill('005.084_pill gold-gold.anm2')), fit(pill('005.084_horse pill gold-gold.anm2'))]
    tr = trinkets()
    top = max(tr)
    blank = Image.new('RGBA', (1, 1), (0, 0, 0, 0))
    gold = [golden(tr[i]).resize((TRINKET, TRINKET), Image.LANCZOS) if i in tr else blank
            for i in range(1, top + 1)]
    health = [trimmed(icon(f)) for _k, f, _n in HEALTH]
    counters = [trimmed(icon(f)) for f, _n in COUNTERS]

    sheet, places = pack([('Icons', [r['pic'] for r in rows]), ('Pills', pills), ('Golden', gold),
                          ('Health', health), ('Counters', counters)])
    sc.save_png(sheet, os.path.join(sc.ASSETS, PNG))
    sc.write_text(os.path.join(sc.ASSETS, 'port_pickup_icons.anm2'),
                  sc.anm2(PNG, [(n, places[n]) for n in ('Icons', 'Pills', 'Golden', 'Health', 'Counters')],
                          '07/10/2026'))

    # every word through the face its set is written in
    for i, lg in enumerate(SETS):
        words = [r['names'][i] for r in rows] + [h[2][i] for h in HEALTH] + [c[1][i] for c in COUNTERS]
        words += [WORDS[k][i].replace('%s', '') for k in WORDS]
        check_words(i, lg, words)

    L = ['/* Written by tools/spawner_pickups.py - do not edit by hand, run the tool. */',
         '#ifndef SPAWNER_PICKUPS_H', '#define SPAWNER_PICKUPS_H', '',
         '/*',
         ' * The spawner ITEMS list past the collectibles (the owner, 07.10.2026): what',
         ' * Game::Spawn makes of each row (type, variant, subtype; a chest closed,',
         ' * subtype 1), its section, and its name in the sets en ru de es jp kr zh.',
         ' * gfx/port_pickup_icons.anm2, every frame at 100 %, pivot top left:',
         ' *   "Icons"    frame k = SP_PICKUP[k]',
         ' *   "Pills"    0 pill, 1 horse pill, 2 gold pill, 3 gold horse pill',
         ' *   "Golden"   frame id - 1 = golden trinket id, the game\'s gold shader baked',
         ' *   "Health"   frame = SP_H_* (the STATS health rows), the HUD\'s own',
         ' *   "Counters" 0 coins, 1 bombs, 2 keys, the HUD\'s own',
         ' */',
         '#define SP_G_KEYS 1', '#define SP_G_BOMBS 2', '#define SP_G_COINS 3', '#define SP_G_HEARTS 4',
         '#define SP_G_BATTERIES 5', '#define SP_G_LOOT 10', '',
         '#define SP_PICKUPS %d' % len(rows),
         'static const struct {',
         '    unsigned short type, variant, subtype;',
         '    unsigned char group;',
         '    unsigned char w, h;                  /* the picture */',
         '    const char *name[7];',
         '} SP_PICKUP[SP_PICKUPS] = {']
    for r, (_x, _y, w, h) in zip(rows, places['Icons']):
        L.append('    { %d, %d, %d, %s, %d, %d, { %s } },' % (r['t'], r['v'], r['s'], r['g'], w, h,
                                                             ', '.join(sc.c_str(n) for n in r['names'])))
    L += ['};', '',
          'static const struct { unsigned char w, h; } SP_PILL_ICON[4] = { %s };' % ', '.join(
              '{ %d, %d }' % (b[2], b[3]) for b in places['Pills']),
          '#define SP_GOLDEN_TOP %d                 /* frames: trinket ids 1..this */' % top,
          '#define SP_TRINKET_PICTURE %d' % TRINKET, '']
    for i, (k, _f, _n) in enumerate(HEALTH):
        L.append('#define SP_H_%s %d' % (k, i))
    L += ['#define SP_HEALTH_ROWS %d' % len(HEALTH), '',
          'static const struct { unsigned char w, h; } SP_HEALTH_ICON[SP_HEALTH_ROWS] = { %s };' % ', '.join(
              '{ %d, %d }' % (b[2], b[3]) for b in places['Health']),
          'static const char *const SP_HEALTH_NAME[7][SP_HEALTH_ROWS] = {']
    for i, lg in enumerate(SETS):
        L.append('    { %s },   /* %s */' % (', '.join(sc.c_str(h[2][i]) for h in HEALTH), lg))
    L += ['};', '',
          'static const struct { unsigned char w, h; } SP_COUNTER_ICON[3] = { %s };' % ', '.join(
              '{ %d, %d }' % (b[2], b[3]) for b in places['Counters']),
          'static const char *const SP_COUNTER_NAME[7][3] = {   /* coins, bombs, keys */']
    for i, lg in enumerate(SETS):
        L.append('    { %s },   /* %s */' % (', '.join(sc.c_str(c[1][i]) for c in COUNTERS), lg))
    L.append('};')
    for k, v in WORDS.items():
        L += ['', 'static const char *const %s[7] = {' % k]
        L += ['    %s,   /* %s */' % (sc.c_str(w), lg) for w, lg in zip(v, SETS)]
        L.append('};')
    # the ids the game really has: the description database also names removed ones (items 43, 61...,
    # trinket 47), and a give of an id the game has no config for is not ours to try
    import xml.etree.ElementTree as ET
    root = ET.parse(os.path.join(sc.DATA, 'items.xml')).getroot()
    for what, tags in (('COLLECTIBLE', ('passive', 'active', 'familiar')), ('TRINKET', ('trinket',))):
        ids = sorted(int(e.get('id')) for e in root if e.tag in tags and e.get('id'))
        bits = [0] * (ids[-1] // 8 + 1)
        for i in ids:
            bits[i // 8] |= 1 << (i % 8)
        L += ['', '#define SP_%s_TOP %d' % (what, ids[-1]),
              'static const unsigned char SP_%s_REAL[%d] = {   /* a bit per id the game has (items.xml) */'
              % (what, len(bits))]
        for k in range(0, len(bits), 16):
            L.append('    ' + ' '.join('0x%02x,' % b for b in bits[k:k + 16]))
        L.append('};')
    L += ['', '#endif', '']
    sc.write_text(HEADER, '\n'.join(L))
    print('pickups %d, sheet %dx%d, golden trinkets 1..%d' % (len(rows), sheet.width, sheet.height, top))


if __name__ == '__main__':
    main()
