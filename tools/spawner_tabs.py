# -*- coding: utf-8 -*-
"""The spawner card's tab row and the enemy list's section headings, in the
game's own hand, for all seven language sets.

The owner approved the mock-ups (scratchpad spawner/design/design.py): four tabs
- ITEMS / ENEMIES / TELEPORT / STATS - written the way the Tweaks rows are
(tweaks_page.word), the chosen one with the game's stroke under it
(credits_page.rule_of, finished by smoothing.smooth - design.py stroke()); the
others drawn by the port at a lower alpha, so one ink picture per word is all
it takes. The enemy list is cut into BOSSES / MINIBOSSES / MONSTERS by headings
in the same hand, each followed by a faint stroke to the card's right edge.

One sheet and one .anm2 per set, port_spawner_tabs_<suffix>.png/.anm2, in the
assets beside port_itemspawn_title_<suffix> (brought in under gfx/ the same way):

    Tabs    frame k = the word of tab k (0..3)
    Stroke  frame k = the stroke that sits under word k
    Heads   frame g = the heading of group g (0 bosses, 1 minibosses, 2 monsters,
            3 training - the dummy, shown first)
    Rule    frame n = a stroke 16 * (n + 1) long (n = 0..24): the rule after a
            heading; the card's width follows the screen, so the port picks the
            frame that fits, never stretches one

Every frame is a crop drawn at 100 %, pivot at its top left. The sizes are
written to port/app/src/main/cpp/spawner_tabs.h.

THE STROKE LONGER THAN THE GAME'S. The game's stroke is 269 long; rule_of
shortens it by taking a piece out of the middle where the ink on both sides of
the cut runs at the same height and thickness. A rule up to 400 is made the
same way the other way round: a piece of the middle where both ends match is
put in once more, then the length is brought down by rule_of's own cut. Both
ends stay the artist's.

THE WORDS. The game's own word where it has one - the Japanese ENEMIES is 敵,
the stringtable's own (SeedMenu: 敵リスポーン, 敵チャンピオン...), STATS is the
stats page's own ステータス; the rest is ours, as on the Tweaks page.

    py -3 tools/spawner_tabs.py
"""
import os
import sys

from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import credits_page            # noqa: E402
import lang                    # noqa: E402
import smoothing               # noqa: E402
import tweaks_page as tp       # noqa: E402
import spawner_common as sc    # noqa: E402

TABS = {
    'en': ['ITEMS', 'ENEMIES', 'TELEPORT', 'STATS'],
    'ru': ['ПРЕДМЕТЫ', 'ВРАГИ', 'ТЕЛЕПОРТ', 'СТАТЫ'],
    'de': ['ITEMS', 'GEGNER', 'TELEPORT', 'WERTE'],
    'es': ['OBJETOS', 'ENEMIGOS', 'TELEPORTE', 'STATS'],
    'jp': ['アイテム', 'てき', 'テレポート', 'ステータス'],
    'kr': ['아이템', '적', '텔레포트', '능력치'],
    'zh': ['道具', '敌人', '传送', '属性'],
}
# the game says Endgegner, jefe, ボス, 보스, 头目 for a boss (Entities: GURGLING_BOSS)
# and Monster, monstruos, モンスター, 몬스터, 怪物 (Items: MONSTER_MANUAL_NAME); the fourth,
# the training dummy's group (19th pass, 03.10.2026), is ours
HEADS = {
    'en': ['BOSSES', 'MINIBOSSES', 'MONSTERS', 'TRAINING'],
    'ru': ['БОССЫ', 'МИНИБОССЫ', 'МОНСТРЫ', 'ТРЕНИРОВКА'],
    'de': ['ENDGEGNER', 'ZWISCHENGEGNER', 'MONSTER', 'TRAINING'],
    'es': ['JEFES', 'MINIJEFES', 'MONSTRUOS', 'ENTRENAMIENTO'],
    'jp': ['ボス', 'ちゅうボス', 'モンスター', 'れんしゅう'],
    'kr': ['보스', '미니보스', '몬스터', '연습'],
    'zh': ['头目', '小头目', '怪物', '练习'],
}
# the ITEMS list's sections and the STATS tab's (the owner, 07.10.2026, mock-ups v2): items, keys,
# bombs, coins, hearts, batteries, cards, runes, pills, trinkets, loot; then stats, pickups, health.
# ru LOOT is the game's own word (MOMS_KEY_DESCRIPTION); the rest is ours.
SECTIONS = {
    'en': ['ITEMS', 'KEYS', 'BOMBS', 'COINS', 'HEARTS', 'BATTERIES', 'CARDS', 'RUNES', 'PILLS', 'TRINKETS', 'LOOT',
           'STATS', 'PICKUPS', 'HEALTH'],
    'ru': ['ПРЕДМЕТЫ', 'КЛЮЧИ', 'БОМБЫ', 'МОНЕТЫ', 'СЕРДЦА', 'БАТАРЕЙКИ', 'КАРТЫ', 'РУНЫ', 'ПИЛЮЛИ', 'БРЕЛОКИ',
           'ЛУТ', 'СТАТЫ', 'РАСХОДНИКИ', 'ЗДОРОВЬЕ'],
    'de': ['ITEMS', 'SCHLÜSSEL', 'BOMBEN', 'MÜNZEN', 'HERZEN', 'BATTERIEN', 'KARTEN', 'RUNEN', 'PILLEN',
           'SCHMUCKSTÜCKE', 'BEUTE', 'WERTE', 'PICKUPS', 'GESUNDHEIT'],
    'es': ['OBJETOS', 'LLAVES', 'BOMBAS', 'MONEDAS', 'CORAZONES', 'BATERÍAS', 'CARTAS', 'RUNAS', 'PÍLDORAS',
           'BARATIJAS', 'BOTÍN', 'STATS', 'RECOGIBLES', 'SALUD'],
    'jp': ['アイテム', 'カギ', 'ボム', 'コイン', 'ハート', 'でんち', 'カード', 'ルーン', 'ピル', 'トリンケット',
           'おたから', 'ステータス', 'ひろいもの', 'たいりょく'],
    'kr': ['아이템', '열쇠', '폭탄', '동전', '하트', '배터리', '카드', '룬', '알약', '장신구', '전리품', '능력치',
           '픽업', '체력'],
    'zh': ['道具', '钥匙', '炸弹', '硬币', '心', '电池', '卡牌', '符文', '胶囊', '饰品', '战利品', '属性', '拾取物', '生命'],
}
STROKE_PAD = 8                # the stroke under a tab is the word and 8 (design.py)
STROKE_GAP = 2                # below the word's bottom (design.py: y + tall + 2)
RULE_FRAMES, RULE_STEP = 25, 16
HEADER = os.path.join(sc.CPP, 'spawner_tabs.h')


# ------------------------------------------------------------------ the stroke
def _native():
    credits_page.STROKE_THIN = 0
    return credits_page.rule_of(10 ** 4, 1)          # longer than it is: itself, whole


def _cut(strip, width):
    """rule_of's own cut: the length taken out of the middle where the ink on both
    sides runs alike; the margin narrows only for a stroke too short for eight."""
    take = strip.width - width
    if take <= 0:
        return strip
    prof = credits_page.stroke_profile(strip)
    edge = min(8, max(1, (width - 1) // 2))
    best = None
    for a in range(edge, strip.width - take - edge + 1):
        b = a + take
        if b >= strip.width:
            break
        cost = abs(prof[a][0] - prof[b][0]) * 3 + abs(prof[a][1] - prof[b][1])
        if best is None or cost < best[0]:
            best = (cost, a)
    cut = best[1]
    out = Image.new('L', (width, strip.height), 0)
    out.paste(strip.crop((0, 0, cut, strip.height)), (0, 0))
    out.paste(strip.crop((cut + take, 0, strip.width, strip.height)), (cut, 0))
    return out


def _lengthened(strip, width):
    """The middle piece [a, b) put in once more, at a and b where the ink matches:
    strip[:b] + strip[a:b] + strip[b:] joins seamlessly at both seams."""
    need = width - strip.width
    prof = credits_page.stroke_profile(strip)
    best = None
    for a in range(8, strip.width - 8):
        for b in range(a + need, strip.width - 8):
            cost = abs(prof[a][0] - prof[b][0]) * 3 + abs(prof[a][1] - prof[b][1])
            # among equals the shortest piece: the less repeated, the less seen
            key = (cost, b - a)
            if best is None or key < best[0]:
                best = (key, a, b)
    _k, a, b = best
    out = Image.new('L', (strip.width + b - a, strip.height), 0)
    out.paste(strip.crop((0, 0, b, strip.height)), (0, 0))
    out.paste(strip.crop((a, 0, b, strip.height)), (b, 0))
    out.paste(strip.crop((b, 0, strip.width, strip.height)), (2 * b - a, 0))
    return out


def stroke_map(width):
    strip = _native()
    if width > strip.width:
        strip = _lengthened(strip, width)
    if 16 < width <= _native().width:
        credits_page.STROKE_THIN = 0
        return credits_page.rule_of(width, 1)        # exactly what the mock-up drew
    return _cut(strip, width)


def stroke(width):
    """design.py stroke(): the map finished as every stroke of ours is."""
    m = stroke_map(max(8, int(width)))
    r, _ = smoothing.smooth(m, tone=credits_page.LIGHT + (255,), ink=credits_page.INK,
                            silhouette=credits_page.SILHOUETTE)
    return r.crop(r.getchannel('A').getbbox())


# ------------------------------------------------------------------ one set
def build(set_name, suffix):
    sc.require_hand(suffix, TABS[suffix] + HEADS[suffix] + SECTIONS[suffix])
    target = lang.native_share(Image.open(os.path.join(tp.RES, set_name, tp.MENU, 'optionsextra.png'))
                               .convert('RGBA'))
    words = [tp.word(set_name, w, target, False) for w in TABS[suffix]]
    heads = [tp.word(set_name, w, target, False) for w in HEADS[suffix]]
    strokes = [stroke(w.width + STROKE_PAD) for w in words]
    rules = [stroke(RULE_STEP * (n + 1)) for n in range(RULE_FRAMES)]
    sections = [tp.word(set_name, w, target, False) for w in SECTIONS[suffix]]
    sheet, boxes = sc.stack(words + strokes + heads + rules + sections)
    png = 'port_spawner_tabs_%s.png' % suffix
    sc.save_png(sheet, os.path.join(sc.ASSETS, png))
    n1, n2, n3 = len(words), len(words) + len(strokes), len(words) + len(strokes) + len(heads)
    n4 = n3 + len(rules)
    sc.write_text(os.path.join(sc.ASSETS, 'port_spawner_tabs_%s.anm2' % suffix),
                  sc.anm2(png, [('Tabs', boxes[:n1]), ('Stroke', boxes[n1:n2]),
                                ('Heads', boxes[n2:n3]), ('Rule', boxes[n3:n4]), ('Sections', boxes[n4:])],
                         '02/10/2026'))
    tab = [(w.width, w.height) for w in words]
    strk = [(s.width, s.height, (w.width - s.width) // 2, w.height + STROKE_GAP)
            for w, s in zip(words, strokes)]
    head = [(h.width, h.height) for h in heads]
    rule = [(r.width, r.height) for r in rules]
    sect = [(x.width, x.height) for x in sections]
    print('  %-3s tabs %s  heads %s  sheet %dx%d' % (suffix, tab, head, sheet.width, sheet.height))
    return dict(suffix=suffix, tab=tab, stroke=strk, head=head, rule=rule, sections=sect)


def write_header(built):
    L = ['/* Written by tools/spawner_tabs.py - do not edit by hand, run the tool. */',
         '#ifndef SPAWNER_TABS_H', '#define SPAWNER_TABS_H', '',
         '/*',
         ' * gfx/port_spawner_tabs_<suffix>.anm2, one per language set in the order of',
         ' * spawner_title.h (en ru de es jp kr zh). Every frame is drawn at 100 %, pivot at',
         ' * its top left:',
         ' *   "Tabs"   frame k = the word of tab k (0 items, 1 enemies, 2 teleport, 3 stats)',
         ' *   "Stroke" frame k = the stroke under word k; dx, dy = its top left from the',
         ' *            word\'s top left (centred, 2 below the word\'s bottom, as the mock-up)',
         ' *   "Heads"  frame g = heading of group g (SP_GROUP_BOSS, _MINIBOSS, _MONSTER,',
         ' *            _TRAINING)',
         ' *   "Rule"   frame n = a stroke SP_RULE_STEP * (n + 1) long: the faint rule after',
         ' *            a heading (the mock-up: alpha 0.45, 8 right of the word, its bottom',
         ' *            one below the word\'s bottom); SP_RULE[n] is its real size',
         ' */',
         '#define SP_TABS 4', '#define SP_HEADS %d' % len(HEADS['en']),
         '#define SP_RULE_FRAMES %d' % RULE_FRAMES, '#define SP_RULE_STEP %d' % RULE_STEP, '',
         'static const struct { short w, h; } SP_TAB[7][4] = {']
    for b in built:
        L.append('    { %s },   /* %s */' % (', '.join('{ %d, %d }' % t for t in b['tab']), b['suffix']))
    L += ['};', '', 'static const struct { short w, h, dx, dy; } SP_STROKE[7][4] = {']
    for b in built:
        L.append('    { %s },   /* %s */' % (', '.join('{ %d, %d, %d, %d }' % t for t in b['stroke']),
                                            b['suffix']))
    L += ['};', '', 'static const struct { short w, h; } SP_HEAD[7][%d] = {' % len(HEADS['en'])]
    for b in built:
        L.append('    { %s },   /* %s */' % (', '.join('{ %d, %d }' % t for t in b['head']), b['suffix']))
    L += ['};', '']
    for i, w in enumerate(SECTIONS['en']):
        L.append('#define SP_SEC_%s %d' % (w, i))
    L += ['#define SP_SECTIONS %d' % len(SECTIONS['en']),
          'static const struct { short w, h; } SP_SECTION[7][SP_SECTIONS] = {']
    for b in built:
        L.append('    { %s },   /* %s */' % (', '.join('{ %d, %d }' % t for t in b['sections']), b['suffix']))
    L += ['};', '', '/* the same in every set: the strokes do not depend on the language */',
          'static const struct { short w, h; } SP_RULE[%d] = {' % RULE_FRAMES]
    rule = built[0]['rule']
    if any(b['rule'] != rule for b in built):
        raise SystemExit('the rules came out different between the sets')
    for i in range(0, len(rule), 5):
        L.append('    ' + ' '.join('{ %d, %d },' % t for t in rule[i:i + 5]))
    L += ['};', '', '#endif', '']
    sc.write_text(HEADER, '\n'.join(L))


def main():
    built = [build(set_name, suffix) for set_name, suffix in sc.SETS]
    write_header(built)
    print('rules %s' % ' '.join('%dx%d' % r for r in built[0]['rule']))


if __name__ == '__main__':
    main()
