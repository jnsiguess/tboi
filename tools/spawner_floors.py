# -*- coding: utf-8 -*-
"""The spawner's TELEPORT tab, floors column: every floor of a run, and the
Greed mode's floors apart, with the game's own names in the seven sets.

stage and stage_type are what Level::SetStage takes (LevelStage, StageType: 0
original, 1 Wrath of the Lamb, 2 Afterbirth, 4 and 5 the Repentance alternative
paths - Downpour/Dross, Mines/Ashpit, Mausoleum/Gehenna, Corpse). A normal run
has stages 1..13; Blue Womb is stage 9 and the game names it "???"; Sheol and
Cathedral are 10, Dark Room and Chest 11, the Void 12, Home 13. Greed mode
numbers its own seven floors 1..7 (Basement .. The Shop, Ultra Greed).

The names are the stringtable's, category Stages; the numbered floors get the
game's " I" / " II" (its table at 0xa8e480 adds them in every language).
chapter groups the column - the mock-up leaves a little air between chapters:
1..4 the chapters, 5 Sheol to Chest, 6 the Void and Home; in Greed mode 1..4,
5 Sheol, 6 The Shop, 7 Ultra Greed.

Every name is glyph-checked in the description face of its set; English where
a character is missing.

THE APPROVED v6 (04.10.2026, other stuff/spawner-19-mockups, the notes): the
card is 420 wide in every language, so a few paths are shortened, each the way
its language does it (SHORT: a path's name shortened in I, II and Greed alike),
and every floor carries the icon the game's floor-transition screen gives it -
frame `level` of gfx/ui/stage/progress.anm2 "Levels", picked the way 0x7eeabc
picks it (its tables at 0x959de8 and 0x959df4), read off the code.

    py -3 tools/spawner_floors.py   ->  port/app/src/main/cpp/spawner_floors.h
"""
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import spawner_common as sc    # noqa: E402

HEADER = os.path.join(sc.CPP, 'spawner_floors.h')
SUFFIX = {0: '', 1: ' I', 2: ' II'}

# the shortened path names, approved 04.10.2026 (the notes, v5): language -> full -> shown
SHORT = {
    'en': {'Burning Basement': 'Burning Bsmt'},
    'ru': {'Затопленные Пещеры': 'Затопл. Пещеры'},
    'de': {'Flammen-Souterrain': 'Flammen-Sout.', 'Überflutete Höhlen': 'Überfl. Höhlen',
           'Vernarbter Schoß': 'Vernarb. Schoß'},
    'es': {'Profundidades viscosas': 'Prof. viscosas', 'Sótano en llamas': 'Sót. en llamas',
           'Cuevas inundadas': 'Cuevas inund.'},
}


def level_frame(stage, stype, greed):
    """0x7eeabc (Repentance): the floor-transition screen's icon of a floor"""
    if greed:
        if stype in (4, 5):
            return stype + stage * 2 + 13
        if stage <= 4:
            return 3 * stage - 3 + stype
        if 5 <= stage <= 7:
            return (13, 16, 16)[stage - 5] + stype      # 0x959de8
        return 17 + stype
    if stage == 13:
        return 27
    if stype in (4, 5):
        s = stage if stage - 1 < 0 else stage - 1
        return (s & ~1) + stype + 15
    if stage <= 9:
        s = stage if stage - 1 < 0 else stage - 1
        return (s & ~1) + (s >> 1) + stype
    if 10 <= stage <= 12:
        return (13, 15, 18)[stage - 10] + stype      # 0x959df4
    return 17 + stype


def pair(stage1, t, chapter, key):
    return [(stage1, t, chapter, key, 1), (stage1 + 1, t, chapter, key, 2)]


# (stage, stage type, chapter, name key, numeral)
NORMAL = (pair(1, 0, 1, 'BASEMENT_NAME') + pair(1, 1, 1, 'CELLAR_NAME') + pair(1, 2, 1, 'BURNING_BASEMENT_NAME')
          + pair(1, 4, 1, 'DOWNPOUR_NAME') + pair(1, 5, 1, 'DROSS_NAME')
          + pair(3, 0, 2, 'CAVES_NAME') + pair(3, 1, 2, 'CATACOMBS_NAME') + pair(3, 2, 2, 'FLOODED_CAVES_NAME')
          + pair(3, 4, 2, 'MINES_NAME') + pair(3, 5, 2, 'ASHPIT_NAME')
          + pair(5, 0, 3, 'DEPTHS_NAME') + pair(5, 1, 3, 'NECROPOLIS_NAME') + pair(5, 2, 3, 'DANK_DEPTHS_NAME')
          + pair(5, 4, 3, 'MAUSOLEUM_NAME') + pair(5, 5, 3, 'GEHENNA_NAME')
          + pair(7, 0, 4, 'WOMB_NAME') + pair(7, 1, 4, 'UTERO_NAME') + pair(7, 2, 4, 'SCARRED_WOMB_NAME')
          + pair(7, 4, 4, 'CORPSE_NAME') + [(9, 0, 4, 'BLUE_WOMB_NAME', 0)]
          + [(10, 0, 5, 'SHEOL_NAME', 0), (10, 1, 5, 'CATHEDRAL_NAME', 0),
             (11, 0, 5, 'DARK_ROOM_NAME', 0), (11, 1, 5, 'CHEST_NAME', 0)]
          + [(12, 0, 6, 'THE_VOID_NAME', 0), (13, 0, 6, 'HOME_NAME', 0)])
GREED = [(1, 0, 1, 'BASEMENT_NAME', 0), (1, 1, 1, 'CELLAR_NAME', 0), (1, 2, 1, 'BURNING_BASEMENT_NAME', 0),
         (2, 0, 2, 'CAVES_NAME', 0), (2, 1, 2, 'CATACOMBS_NAME', 0), (2, 2, 2, 'FLOODED_CAVES_NAME', 0),
         (3, 0, 3, 'DEPTHS_NAME', 0), (3, 1, 3, 'NECROPOLIS_NAME', 0), (3, 2, 3, 'DANK_DEPTHS_NAME', 0),
         (4, 0, 4, 'WOMB_NAME', 0), (4, 1, 4, 'UTERO_NAME', 0), (4, 2, 4, 'SCARRED_WOMB_NAME', 0),
         (5, 0, 5, 'SHEOL_NAME', 0), (6, 0, 6, 'THE_SHOP_NAME', 0), (7, 0, 7, 'ULTRA_GREED_NAME', 0)]


def main():
    names = sc.stringtable(('Stages',))
    rows, fallbacks = [], []
    for mode, table in ((0, NORMAL), (1, GREED)):
        for stage, t, chapter, key, num in table:
            if key not in names:
                raise SystemExit('the stringtable has no %s' % key)
            en = names[key]['en'] + SUFFIX[num]
            shown = []
            for lg in sc.LANGS:
                n = names[key][lg]
                if not n:
                    fallbacks.append((lg, en, 'no name in the stringtable'))
                    shown.append(en)
                    continue
                n = SHORT.get(lg, {}).get(n, n) + SUFFIX[num]
                miss = sc.desc_missing(lg, n)
                if miss:
                    fallbacks.append((lg, n, 'lacks ' + ' '.join('%s U+%04X' % (c, ord(c)) for c in miss)))
                    n = en
                shown.append(n)
            rows.append((stage, t, chapter, mode, key, shown, level_frame(stage, t, mode == 1)))
    normal = sum(1 for r in rows if r[3] == 0)
    L = ['/* Written by tools/spawner_floors.py - do not edit by hand, run the tool. */',
         '#ifndef SPAWNER_FLOORS_H', '#define SPAWNER_FLOORS_H', '',
         '/*',
         ' * The teleport tab\'s floors: stage and stage_type as Level::SetStage takes them,',
         ' * chapter groups the column (1..4 the chapters, 5 Sheol..Chest, 6 Void and Home;',
         ' * Greed: 1..4, 5 Sheol, 6 The Shop, 7 Ultra Greed), mode 0 a normal run, 1 Greed',
         ' * (its own stage numbers 1..7). The normal floors come first, SP_FLOORS_NORMAL of',
         ' * them, then the Greed ones. name[] in the sets\' order (en ru de es jp kr zh),',
         ' * the game\'s own (stringtable, Stages) with its " I" / " II", a few paths',
         ' * shortened for the 420 card (approved 04.10.2026).',
         ' */',
         '#define SP_FLOORS %d' % len(rows),
         '#define SP_FLOORS_NORMAL %d' % normal,
         '#define SP_FLOORS_GREED %d' % (len(rows) - normal), '',
         'static const struct {', '    unsigned char stage, stage_type, chapter;',
         '    unsigned char mode;     /* 0 normal, 1 greed */',
         '    unsigned char level;    /* frame of "Stages" in gfx/port_room_icons.anm2 (0x7eeabc) */',
         '    const char *name[7];',
         '} SP_FLOOR[SP_FLOORS] = {']
    for stage, t, chapter, mode, key, shown, level in rows:
        L.append('    { %d, %d, %d, %d, %d, { %s } },' % (stage, t, chapter, mode, level,
                                                     ', '.join(sc.c_str(n) for n in shown)))
    L += ['};', '', '#endif', '']
    sc.write_text(HEADER, '\n'.join(L))
    print('floors %d: normal %d, greed %d' % (len(rows), normal, len(rows) - normal))
    print('English instead of the set\'s own name: %d' % len(fallbacks))
    for f in fallbacks:
        print('  %s  %s  %s' % f)


if __name__ == '__main__':
    main()
