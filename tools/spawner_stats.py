# -*- coding: utf-8 -*-
"""The spawner's STATS tab pictures and the card's own words in the seven sets.

THE ICONS are the HUD's own, gfx/ui/hudstats2.png, 16 x 16 at 100 %, cut exactly
where the approved mock-up cut them (design.py STAT_ICON): frame 0 speed, 1 tears,
2 damage, 3 range, 4 shot speed, 5 luck.

THE WORDS (spawner_strings.h): the tab names and the enemy-list headings as text
(the pictures are spawner_tabs.py's), the six stat names, the player picker, the
RESET line, the hint while a value is typed and the limit line of each stat, and
every character's name for the player picker. The stat names are the game's own
where it has them (stringtable STAT_UP_*, and the description database's words
for Japanese and Korean, which the game's short "涙アップ" style does not give),
English and Russian as the mock-up has them. The character names are the
stringtable's (Players), by PlayerType - the order of players.xml: the tainted
characters are named by the game exactly as the plain ones.

Every string is looked up in the description face its set is written with; a
character name falls back to English, a UI line that fails stops the tool.

    gfx/port_stat_icons.png/.anm2    "Icons", frames 0..5
    port/app/src/main/cpp/spawner_strings.h

    py -3 tools/spawner_stats.py
"""
import os
import re
import sys

from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import spawner_common as sc    # noqa: E402
import spawner_tabs            # noqa: E402

HEADER = os.path.join(sc.CPP, 'spawner_strings.h')
HUD = os.path.join(sc.DATA, 'gfx', 'ui', 'hudstats2.png')
# speed, tears, damage, range, shot speed, luck - design.py STAT_ICON
ICON_AT = [(16, 0), (32, 0), (0, 0), (0, 16), (16, 16), (32, 16)]
ICON = 16

STAT_NAME = {
    'en': ['Speed', 'Tears', 'Damage', 'Range', 'Shot speed', 'Luck'],
    'ru': ['Скорость', 'Слёзы', 'Урон', 'Дальность', 'Скорость выстрела', 'Удача'],
    'de': ['Tempo', 'Tränen', 'Schaden', 'Reichweite', 'Schusstempo', 'Glück'],
    'es': ['Velocidad', 'Lágrimas', 'Daño', 'Alcance', 'Vel. de lágrimas', 'Suerte'],
    'jp': ['移動速度', '連射速度', '攻撃力', '射程', '弾速', '運'],
    'kr': ['이동속도', '연사', '공격력', '사거리', '탄속', '행운'],
    'zh': ['移速', '射速', '伤害', '射程', '弹速', '幸运'],
}
PLAYER = {'en': 'PLAYER %d', 'ru': 'ИГРОК %d', 'de': 'SPIELER %d', 'es': 'JUGADOR %d',
          'jp': 'プレイヤー%d', 'kr': '플레이어 %d', 'zh': '玩家%d'}
RESET = {'en': 'RESET', 'ru': 'СБРОСИТЬ', 'de': 'ZURÜCKSETZEN', 'es': 'RESTABLECER',
         'jp': 'リセット', 'kr': '초기화', 'zh': '重置'}
# (const char *typed, double factor)
TYPING = {
    'en': 'typing: %s  (items add ×%.2f from now)',
    'ru': 'ввод: %s  (×%.2f к предметам дальше)',
    'de': 'Eingabe: %s  (Items ab jetzt ×%.2f)',
    'es': 'escribiendo: %s  (objetos ×%.2f desde ahora)',
    'jp': '入力: %s  (以後のアイテム ×%.2f)',
    'kr': '입력: %s  (이후 아이템 ×%.2f)',
    'zh': '输入: %s  (之后的道具 ×%.2f)',
}
# per stat, its arguments in HINT_ARGS
HINT = {
    'en': ['speed: min %.2f, max %.2f', 'tears: max %.0f', 'damage: min %.2f', 'range: min %.2f',
           'shot speed: steps of %.1f, min %.2f', ''],
    'ru': ['скорость: мин %.2f, макс %.2f', 'слёзы: макс %.0f', 'урон: мин %.2f', 'дальность: мин %.2f',
           'скорость выстрела: шаг %.1f, мин %.2f', ''],
    'de': ['Tempo: min. %.2f, max. %.2f', 'Tränen: max. %.0f', 'Schaden: min. %.2f', 'Reichweite: min. %.2f',
           'Schusstempo: Schritte von %.1f, min. %.2f', ''],
    'es': ['velocidad: mín. %.2f, máx. %.2f', 'lágrimas: máx. %.0f', 'daño: mín. %.2f', 'alcance: mín. %.2f',
           'vel. de lágrimas: pasos de %.1f, mín. %.2f', ''],
    'jp': ['移動速度: 最小 %.2f、最大 %.2f', '連射速度: 最大 %.0f', '攻撃力: 最小 %.2f', '射程: 最小 %.2f',
           '弾速: %.1f きざみ、最小 %.2f', ''],
    'kr': ['이동속도: 최소 %.2f, 최대 %.2f', '연사: 최대 %.0f', '공격력: 최소 %.2f', '사거리: 최소 %.2f',
           '탄속: %.1f 단위, 최소 %.2f', ''],
    'zh': ['移速: 最小 %.2f, 最大 %.2f', '射速: 最大 %.0f', '伤害: 最小 %.2f', '射程: 最小 %.2f',
           '弹速: 步长 %.1f, 最小 %.2f', ''],
}
HINT_ARGS = ['(double min, double max)', '(double max)', '(double min)', '(double min)',
             '(double step, double min)', '(none: "")']


def icons():
    hud = Image.open(HUD).convert('RGBA')
    pics = [hud.crop((x, y, x + ICON, y + ICON)) for x, y in ICON_AT]
    sheet = Image.new('RGBA', (ICON * len(pics), ICON), (0, 0, 0, 0))
    for i, p in enumerate(pics):
        sheet.alpha_composite(p, (i * ICON, 0))
    png = 'port_stat_icons.png'
    sc.save_png(sheet, os.path.join(sc.ASSETS, png))
    sc.write_text(os.path.join(sc.ASSETS, 'port_stat_icons.anm2'),
                  sc.anm2(png, [('Icons', [(i * ICON, 0, ICON, ICON) for i in range(len(pics))])], '02/10/2026'))


def characters():
    text = open(os.path.join(sc.DATA, 'players.xml'), encoding='utf-8').read()
    keys = {}
    for m in re.finditer(r'<player ([^>]*)>', text):
        a = dict(re.findall(r'(\w+)="([^"]*)"', m.group(1)))
        keys[int(a['id'])] = a['name'].lstrip('#')
    if sorted(keys) != list(range(len(keys))):
        raise SystemExit('players.xml ids are not 0..n-1: %s' % sorted(keys))
    names = sc.stringtable(('Players',))
    rows, fallbacks = [], []
    for pid in range(len(keys)):
        got = names[keys[pid]]
        shown = []
        for lg in sc.LANGS:
            n = got[lg] or got['en']
            miss = sc.desc_missing(lg, n)
            if miss:
                fallbacks.append((lg, n, ' '.join('%s U+%04X' % (c, ord(c)) for c in miss)))
                n = got['en']
            shown.append(n)
        rows.append((pid, keys[pid], shown))
    return rows, fallbacks


def check_ui(lines):
    bad = []
    for lg, s in lines:
        m = sc.desc_missing(lg, s)
        if m:
            bad.append('%s "%s" lacks %s' % (lg, s, ' '.join('U+%04X' % ord(c) for c in m)))
    if bad:
        raise SystemExit('the description face lacks characters:\n  ' + '\n  '.join(bad))


def table(name, rows, comment):
    out = ['/* %s */' % comment, 'static const char *const %s = {' % name]
    for lg, row in zip(sc.LANGS, rows):
        if isinstance(row, list):
            out.append('    { %s },   /* %s */' % (', '.join(sc.c_str(s) for s in row), lg))
        else:
            out.append('    %s,   /* %s */' % (sc.c_str(row), lg))
    return out + ['};', '']


def main():
    icons()
    tabs = [spawner_tabs.TABS[lg] for lg in sc.LANGS]
    heads = [spawner_tabs.HEADS[lg] for lg in sc.LANGS]
    ui = []
    for lg in sc.LANGS:
        ui += [(lg, s) for s in spawner_tabs.TABS[lg] + spawner_tabs.HEADS[lg] + STAT_NAME[lg] + HINT[lg]]
        ui += [(lg, PLAYER[lg]), (lg, RESET[lg]), (lg, TYPING[lg])]
    check_ui(ui)
    chars, fallbacks = characters()

    L = ['/* Written by tools/spawner_stats.py - do not edit by hand, run the tool. */',
         '#ifndef SPAWNER_STRINGS_H', '#define SPAWNER_STRINGS_H', '',
         '/*',
         ' * The spawner card\'s words, one row per language set in the order of',
         ' * spawner_title.h (en ru de es jp kr zh). All are written in the description',
         ' * face and were checked against the face each set is written with. The',
         ' * formats are printf formats; their arguments are given beside each table.',
         ' * The stat icons: gfx/port_stat_icons.anm2 "Icons", frame k = stat k',
         ' * (SP_STAT_ICON square, 100 %, pivot top left).',
         ' */',
         '#define SP_STAT_ICON %d' % ICON,
         '#define SP_STAT_SPEED 0', '#define SP_STAT_TEARS 1', '#define SP_STAT_DAMAGE 2',
         '#define SP_STAT_RANGE 3', '#define SP_STAT_SHOTSPEED 4', '#define SP_STAT_LUCK 5',
         '#define SP_CHARACTERS %d' % len(chars), '']
    L += table('SP_TAB_NAME[7][4]', tabs, 'the tabs as text: items, enemies, teleport, stats')
    L += table('SP_GROUP_NAME[7][3]', heads, 'the enemy-list headings as text: bosses, minibosses, monsters')
    L += table('SP_STAT_NAME[7][6]', [STAT_NAME[lg] for lg in sc.LANGS], 'the stats, SP_STAT_* order')
    L += table('SP_PLAYER[7]', [PLAYER[lg] for lg in sc.LANGS], '(int player, from 1)')
    L += table('SP_PLAYER_FMT[7]', [PLAYER[lg] + ': %s' for lg in sc.LANGS],
               '(int player from 1, const char *character) - the mock-up\'s "< PLAYER 1: Isaac >",'
               ' the arrows are the port\'s')
    L += table('SP_RESET[7]', [RESET[lg] for lg in sc.LANGS], 'the bottom line of the stats tab')
    L += table('SP_TYPING_FMT[7]', [TYPING[lg] for lg in sc.LANGS],
               '(const char *typed, double factor) - while a value is typed')
    L += table('SP_STAT_HINT_FMT[7][6]', [HINT[lg] for lg in sc.LANGS],
               'the limit line of each stat, while it is stepped or typed; arguments: ' +
               ', '.join('%s %s' % (n, a) for n, a in zip(STAT_NAME['en'], HINT_ARGS)))
    L += ['/* every character by PlayerType (players.xml), the game\'s own names; the tainted',
          '   ones (21..40) the game names exactly as the plain ones; English where a set\'s',
          '   face lacks a character */',
          'static const char *const SP_CHARACTER[SP_CHARACTERS][7] = {']
    for pid, key, shown in chars:
        L.append('    { %s },   /* %d %s */' % (', '.join(sc.c_str(s) for s in shown), pid, key))
    L += ['};', '', '#endif', '']
    sc.write_text(HEADER, '\n'.join(L))
    print('stat icons 6, characters %d; English instead of the set\'s own name: %d' % (len(chars), len(fallbacks)))
    for f in fallbacks:
        print('  %s  %s  lacks %s' % f)


if __name__ == '__main__':
    main()
