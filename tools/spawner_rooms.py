# -*- coding: utf-8 -*-
"""The spawner's TELEPORT tab, rooms column: every room type the game has, with
the minimap's own icon and a name in the seven language sets.

THE LIST is the game's RoomType, all of it that a player can be sent to - the
owner wants every room, also the ones the floor does not have: the default
(starting) room, treasure, planetarium, boss, shop, miniboss, the three secret
rooms, curse, sacrifice, library, arcade, challenge and boss challenge (one
RoomType, 11, subtype 0 and 1 - 00.special rooms.xml), dice, vault (CHEST),
devil, angel, black market, crawlspace (DUNGEON), boss rush, Isaac's and the
barren bedroom, the secret exit (the door to the alternative path), the blue
room, I AM ERROR. Left out: GREED_EXIT and the two TELEPORTER types - their
rooms exist only in 18.greed special.xml, so outside Greed mode there is
nowhere for them to lead.

THE ICON is the minimap's: the IconXxx animation of gfx/ui/minimap_icons.anm2
(16 x 16 cells), its ink cropped, centred in 12 x 12 and doubled NEAREST - the
mock-up's room_icon(); baked here, so the port draws it at 100 %. The starting
room is drawn as the mock-up drew it, the minimap's own room cell (minimap1.png).
A room the minimap has no icon for gets none (-1), not an invented one - I AM
ERROR among them: the mock-up meant the minimap's question mark for it, but that
picture is 16 x 20 of ink (minimap_questionmark.png), it does not fit a 12 x 12
cell doubled, and it never reached the mock-up's screen (the column ended above
it), so it is left out rather than drawn at another scale than its neighbours. The crawlspace takes
IconLadder: the description database marks crawlspaces with exactly that
picture ({{LadderRoom}}).

THE NAMES: the game has no room names in its stringtable, so they are the
description database's own words for each language (eid.bin - "Teleports Isaac
to the Boss Room", ボス部屋, 보스방, 头目房...), the Russian and English as the
approved mock-up has them; where the database never names a room (the bedrooms,
the vault, the black market in some sets) the word is ours. Glyph-checked in the
face that set's rows are written with; English where it lacks one.

THE APPROVED v6 (04.10.2026, other stuff/spawner-19-mockups, the notes):
  - a room the minimap has no icon for (black market, boss rush, secret exit,
    blue room, I AM ERROR) takes the plain room cell the starting room has
    (minimap1.png RoomVisited, doubled) - the frame 0 already there;
  - the card is 420 wide in every language, so a few names are shortened,
    each the way its language shortens it (SHORT, the table of the note's v5);
  - "Stages": the floor icons, gfx/ui/stage/progress.anm2 "Levels", the 28 frames
    the floor-transition screen picks from (0x7eeabc), their ink 26 x 18 at 100 %
    (spawner_floors.py names the frame of every floor).

Also in the .anm2: "Here" - the minimap's RoomCurrent cell (minimap1.png, 9 x 8),
kept for the old builds (the mark is now the description face's own triangle),
and "Seam" - a 1 x 8 piece of the paper's grey the port repeats down between
the two columns.

    gfx/port_room_icons.png/.anm2        "Icons", "Here", "Seam", "Stages"
    port/app/src/main/cpp/spawner_rooms.h

    py -3 tools/spawner_rooms.py
"""
import os
import sys
import xml.etree.ElementTree as ET

from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import spawner_common as sc    # noqa: E402

UI = os.path.join(sc.DATA, 'gfx', 'ui')
CELL = 24                      # 12 x 12, doubled
HEADER = os.path.join(sc.CPP, 'spawner_rooms.h')
SEAM = (196, 184, 188, 255)

# RoomType numbers (the game's enum, Repentance)
RT = dict(DEFAULT=1, SHOP=2, ERROR=3, TREASURE=4, BOSS=5, MINIBOSS=6, SECRET=7, SUPERSECRET=8,
          ARCADE=9, CURSE=10, CHALLENGE=11, LIBRARY=12, SACRIFICE=13, DEVIL=14, ANGEL=15,
          DUNGEON=16, BOSSRUSH=17, ISAACS=18, BARREN=19, CHEST=20, DICE=21, BLACK_MARKET=22,
          GREED_EXIT=23, PLANETARIUM=24, TELEPORTER=25, TELEPORTER_EXIT=26, SECRET_EXIT=27,
          BLUE=28, ULTRASECRET=29)

# (RoomType, subtype, icon, names en ru de es jp kr zh) - the order of the column
ROOMS = [
    ('DEFAULT', 0, 'Start',
     ['Starting Room', 'Стартовая', 'Startraum', 'Sala inicial', '開始時の部屋', '시작방', '初始房间']),
    ('TREASURE', 0, 'IconTreasureRoom',
     ['Treasure Room', 'Сокровищница', 'Schatzraum', 'Sala del tesoro', 'トレジャールーム', '보물방', '宝箱房']),
    ('PLANETARIUM', 0, 'IconPlanetarium',
     ['Planetarium', 'Планетарий', 'Planetarium', 'Planetario', 'プラネタリウム', '천체관', '星象房']),
    ('BOSS', 0, 'IconBoss',
     ['Boss Room', 'Босс', 'Bossraum', 'Sala del jefe', 'ボス部屋', '보스방', '头目房']),
    ('SHOP', 0, 'IconShop',
     ['Shop', 'Магазин', 'Laden', 'Tienda', 'お店', '상점', '商店']),
    ('MINIBOSS', 0, 'IconMiniboss',
     ['Miniboss Room', 'Мини-босс', 'Miniboss-Raum', 'Sala de minijefe', '中ボス部屋', '미니보스방', '小头目房']),
    ('SECRET', 0, 'IconSecretRoom',
     ['Secret Room', 'Секретная', 'Geheimraum', 'Sala secreta', '隠し部屋', '비밀방', '隐藏房']),
    ('SUPERSECRET', 0, 'IconSuperSecretRoom',
     ['Super Secret Room', 'Суперсекретная', 'Superheimlicher Raum', 'Sala súper secreta',
      'スーパーシークレットルーム', '일급 비밀방', '超级隐藏房']),
    ('ULTRASECRET', 0, 'IconUltraSecretRoom',
     ['Ultra Secret Room', 'Ультрасекретная', 'Ultra-Geheimraum', 'Sala ultrasecreta', '離れ隠し部屋',
      '특급 비밀방', '究极隐藏房']),
    ('CURSE', 0, 'IconCurseRoom',
     ['Curse Room', 'Проклятая', 'Fluchraum', 'Sala maldita', '呪いの部屋', '저주방', '诅咒房']),
    ('SACRIFICE', 0, 'IconSacrificeRoom',
     ['Sacrifice Room', 'Жертвенная', 'Opferraum', 'Sala del sacrificio', '生贄部屋', '희생방', '献祭房']),
    ('LIBRARY', 0, 'IconLibrary',
     ['Library', 'Библиотека', 'Bibliothek', 'Biblioteca', '図書館', '책방', '图书馆']),
    ('ARCADE', 0, 'IconArcade',
     ['Arcade', 'Аркада', 'Arkadenraum', 'Arcade', 'アーケード', '오락실', '赌博房']),
    ('CHALLENGE', 0, 'IconAmbushRoom',
     ['Challenge Room', 'Испытание', 'Challenge-Raum', 'Sala de desafío', 'チャレンジルーム', '도전방', '挑战房']),
    ('CHALLENGE', 1, 'IconBossAmbushRoom',
     ['Boss Challenge Room', 'Испытание боссов', 'Boss-Challenge-Raum', 'Desafío de jefes',
      'ボスチャレンジルーム', '보스 도전방', '头目挑战房']),
    ('DICE', 0, 'IconDiceRoom',
     ['Dice Room', 'Игральная кость', 'Würfelraum', 'Sala de dados', 'ダイス部屋', '주사위방', '骰子房']),
    ('CHEST', 0, 'IconChestRoom',
     ['Vault', 'Хранилище', 'Tresorraum', 'Bóveda', '宝物庫', '금고방', '宝库']),
    ('DEVIL', 0, 'IconDevilRoom',
     ['Devil Room', 'Дьявол', 'Teufelsraum', 'Sala del Diablo', '悪魔部屋', '악마방', '恶魔房']),
    ('ANGEL', 0, 'IconAngelRoom',
     ['Angel Room', 'Ангел', 'Engelsraum', 'Sala del Ángel', '天使部屋', '천사방', '天使房']),
    ('BLACK_MARKET', 0, None,
     ['Black Market', 'Чёрный рынок', 'Schwarzmarkt', 'Mercado negro', 'ブラックマーケット', '암시장', '黑市']),
    ('DUNGEON', 0, 'IconLadder',
     ['Crawlspace', 'Ретро-сокровищница', 'Kriechkeller', 'Mazmorra', 'クロールスペース', '사다리방', '夹层']),
    ('BOSSRUSH', 0, None,
     ['Boss Rush', 'Босс-раш', 'Boss-Ansturm', 'Boss Rush', 'ボスラッシュ', '보스러시', '头目车轮战']),
    ('ISAACS', 0, 'IconIsaacsRoom',
     ["Isaac's Bedroom", 'Спальня Айзека', 'Isaacs Schlafzimmer', 'Dormitorio de Isaac', 'アイザックの寝室',
      '아이작의 침실', '以撒的卧室']),
    ('BARREN', 0, 'IconBarrenRoom',
     ['Barren Bedroom', 'Пустая спальня', 'Kahles Schlafzimmer', 'Dormitorio vacío', '殺風景な寝室',
      '텅 빈 침실', '简陋的卧室']),
    ('SECRET_EXIT', 0, None,
     ['Secret Exit', 'Тайный выход', 'Geheimer Ausgang', 'Salida secreta', '秘密の出口', '비밀 출구', '秘密出口']),
    ('BLUE', 0, None,
     ['Blue Room', 'Синяя комната', 'Blauer Raum', 'Sala azul', '青い部屋', '파란 방', '蓝色房间']),
    ('ERROR', 0, None,
     ['I AM ERROR', 'I AM ERROR', 'I AM ERROR', 'I AM ERROR', 'I AM ERROR', '오류방', '错误房']),
]


# the shortened names, approved 04.10.2026 (the notes, v5): language -> full -> shown
SHORT = {
    'en': {'Boss Challenge Room': 'Boss Challenge'},
    'ru': {'Ретро-сокровищница': 'Ретро-сокр.'},
    'de': {'Superheimlicher Raum': 'Super-Geheimraum', 'Kahles Schlafzimmer': 'Kahles Schlafz.',
           'Isaacs Schlafzimmer': 'Isaacs Schlafz.', 'Boss-Challenge-Raum': 'Boss-Challenge'},
    'es': {'Dormitorio de Isaac': 'Dorm. de Isaac', 'Sala del sacrificio': 'Sala del sacrif.',
           'Sala súper secreta': 'Sala supersecreta'},
    'jp': {'スーパーシークレットルーム': 'スーパー隠し部屋', 'ボスチャレンジルーム': 'ボスチャレンジ'},
}
STAGE_INK = (3, 13, 29, 31)    # every "Levels" frame's ink, 26 x 18


def stage_icons():
    """progress.anm2 "Levels": its frames in order, each cut to its ink"""
    path = os.path.join(UI, 'stage', 'progress.anm2')
    t = ET.parse(path).getroot()
    sheet = Image.open(os.path.join(UI, 'stage', t.find('Content/Spritesheets/Spritesheet').get('Path'))).convert('RGBA')
    for a in t.iter('Animation'):
        if a.get('Name') != 'Levels':
            continue
        for la in a.iter('LayerAnimation'):
            fs = list(la.iter('Frame'))
            if fs:
                out = []
                for f in fs:
                    x, y = int(f.get('XCrop')), int(f.get('YCrop'))
                    out.append(sheet.crop((x + STAGE_INK[0], y + STAGE_INK[1], x + STAGE_INK[2], y + STAGE_INK[3])))
                return out
    raise SystemExit('progress.anm2 has no Levels')


def minimap_icons():
    """IconXxx -> its crop, read from the game's own minimap_icons.anm2."""
    t = ET.parse(os.path.join(UI, 'minimap_icons.anm2')).getroot()
    out = {}
    for a in t.find('Animations').findall('Animation'):
        f = a.find('LayerAnimations').find('LayerAnimation').find('Frame')
        out[a.get('Name')] = tuple(int(f.get(k)) for k in ('XCrop', 'YCrop', 'Width', 'Height'))
    return out


def icon_1x(name, crops):
    if name == 'Start':
        m1 = Image.open(os.path.join(UI, 'minimap1.png')).convert('RGBA')
        c = Image.new('RGBA', (16, 16))
        c.alpha_composite(m1.crop((27, 160, 36, 168)), (4, 4))
        return c
    x, y, w, h = crops[name]
    return Image.open(os.path.join(UI, 'minimap_icons.png')).convert('RGBA').crop((x, y, x + w, y + h))


def icon(name, crops):
    """design.py room_icon(): the ink centred in 12 x 12, doubled NEAREST."""
    im = icon_1x(name, crops)
    g = im.crop(im.getchannel('A').getbbox())
    if g.width > CELL // 2 or g.height > CELL // 2:
        raise SystemExit('%s is %dx%d, larger than the 12 x 12 the cell doubles' % (name, g.width, g.height))
    c = Image.new('RGBA', (CELL // 2, CELL // 2), (0, 0, 0, 0))
    c.alpha_composite(g, ((CELL // 2 - g.width) // 2, (CELL // 2 - g.height) // 2))
    return c.resize((CELL, CELL), Image.NEAREST)


def main():
    crops = minimap_icons()
    pictures, index, fallbacks = [], {}, []
    for _t, _s, name, _n in ROOMS:
        if name and name not in index:
            index[name] = len(pictures)
            pictures.append(icon(name, crops))
    here = Image.open(os.path.join(UI, 'minimap1.png')).convert('RGBA').crop((27, 192, 36, 200))
    seam = Image.new('RGBA', (1, 8), SEAM)
    stages = stage_icons()
    sw, sh = STAGE_INK[2] - STAGE_INK[0], STAGE_INK[3] - STAGE_INK[1]
    # the icons in a row of cells, the two small pieces under them, the stage icons under those
    cols = 8
    rows = (len(pictures) + cols - 1) // cols
    scols = (cols * CELL + 1) // (sw + 1)
    srows = (len(stages) + scols - 1) // scols
    hy = rows * CELL + 1
    sy0 = hy + here.height + 1
    sheet = Image.new('RGBA', (cols * CELL, sy0 + srows * (sh + 1)), (0, 0, 0, 0))
    frames = []
    for i, p in enumerate(pictures):
        x, y = (i % cols) * CELL, (i // cols) * CELL
        sheet.alpha_composite(p, (x, y))
        frames.append((x, y, CELL, CELL))
    sheet.alpha_composite(here, (0, hy))
    sheet.alpha_composite(seam, (here.width + 1, hy))
    sframes = []
    for i, p in enumerate(stages):
        x, y = (i % scols) * (sw + 1), sy0 + (i // scols) * (sh + 1)
        sheet.alpha_composite(p, (x, y))
        sframes.append((x, y, sw, sh))
    png = 'port_room_icons.png'
    sc.save_png(sheet, os.path.join(sc.ASSETS, png))
    sc.write_text(os.path.join(sc.ASSETS, 'port_room_icons.anm2'), sc.anm2(png, [
        ('Icons', frames), ('Here', [(0, hy, here.width, here.height)]),
        ('Seam', [(here.width + 1, hy, 1, 8)]), ('Stages', sframes)], '04/10/2026'))

    rows_out = []
    for t, s, name, names in ROOMS:
        shown = []
        for lg, n in zip(sc.LANGS, names):
            n = SHORT.get(lg, {}).get(n, n)
            miss = sc.desc_missing(lg, n)
            if miss:
                fallbacks.append((lg, n, ' '.join('%s U+%04X' % (c, ord(c)) for c in miss)))
                n = names[0]
            shown.append(n)
        rows_out.append((t, s, index.get(name, index['Start']) if name else index['Start'], shown))

    L = ['/* Written by tools/spawner_rooms.py - do not edit by hand, run the tool. */',
         '#ifndef SPAWNER_ROOMS_H', '#define SPAWNER_ROOMS_H', '',
         '/*',
         ' * The teleport tab\'s rooms, in the order of the column: type is the game\'s',
         ' * RoomType, subtype tells the boss challenge (RoomType 11, subtype 1) from the',
         ' * plain one (0) and is 0 elsewhere. icon = frame of "Icons" in',
         ' * gfx/port_room_icons.anm2 (SP_ROOM_ICON square, 100 %, pivot top left); a room',
         ' * the minimap has no icon for wears the plain room cell (frame 0, as the start).',
         ' * name[] in the sets\' order (en ru de es jp kr zh), shortened where the 420',
         ' * card asks (approved 04.10.2026); English where the face lacks a character.',
         ' * "Stages": the floor icons (progress.anm2 "Levels"), SP_STAGE_W x SP_STAGE_H.',
         ' * "Here" (frame 0) is the minimap\'s RoomCurrent cell, SP_HERE_W x SP_HERE_H;',
         ' * "Seam" (frame 0) is 1 x 8 of the paper\'s grey, repeated down between the columns.',
         ' */',
         '#define SP_ROOM_ICON %d' % CELL,
         '#define SP_HERE_W %d' % here.width, '#define SP_HERE_H %d' % here.height,
         '#define SP_SEAM_H 8',
         '#define SP_STAGE_W %d' % sw, '#define SP_STAGE_H %d' % sh, '#define SP_STAGES %d' % len(stages), '',
         '#define SP_ROOMS %d' % len(rows_out), '',
         'static const struct {', '    unsigned char type, subtype;', '    short icon;',
         '    const char *name[7];', '} SP_ROOM[SP_ROOMS] = {']
    for t, s, ic, names in rows_out:
        L.append('    { %d, %d, %d, { %s } },   /* %s */' % (RT[t], s, ic, ', '.join(sc.c_str(n) for n in names),
                                                       t + (' boss' if s else '')))
    L += ['};', '', '#endif', '']
    sc.write_text(HEADER, '\n'.join(L))
    print('rooms %d, icons %d, sheet %dx%d' % (len(rows_out), len(pictures), sheet.width, sheet.height))
    print('English instead of the set\'s own name: %d' % len(fallbacks))
    for lg, n, why in fallbacks:
        print('  %s  %s  lacks %s' % (lg, n, why))


if __name__ == '__main__':
    main()
