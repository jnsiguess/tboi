# -*- coding: utf-8 -*-
"""The settings pages of the ported mods: one page a mod, behind its arrow on
the MODS page (the owner, 23.09.2026).

Each page is the PC's own mods page (ModsMenu.png of the language), its title
the mod's name as the MODS list writes it, and a row per setting of the mod's
Mod Config Menu on the PC: the LABELS are the mods' own MCM words, read out of
their Lua (work/../mods/*/mcm.lua, main.lua, the Stats+ bundle) - the mods are
English and so are their pages. What a row changes is modset.c's table: the
same setting the port-touch.ini section holds.

What is not a row: Specialist's item lists (always, never), which want ids
typed in - they stay in the file.

The words of all four pages share one sheet per language set: the labels, the
choice words, and the digits a number is put together from.

Called by tweaks_page.py, which writes the one header for every page.
"""
import os

from PIL import Image

import lang
import tweaks_page as tp

# kinds, as tweaks.c reads them
BOOL, NUM, CHOICE = 0, 1, 2

POSITIONS = ['BOTTOM', 'TOP', 'LEFT', 'RIGHT']
SORTINGS = ['SEGMENTS', 'BOSSES', 'VANILLA']
TEXTS = ['NONE', 'PERCENT', 'HPLEFT']
PRESETS = ['DEFAULT', 'FOUND HUD', 'BOSS BAR']
BRACKETS = ['NONE', 'SQUARE', 'ROUND', 'CURLY', 'ANGLE']
COLOURS = ['NONE', 'GREY', 'RED', 'GREEN', 'BLUE', 'ORANGE', 'MAGENTA', 'CYAN']
# Stats+'s own condition names are 'Map button is held' and 'Toggled via the drop
# button'; at their length a loadout row came out 448 units wide, wider than the
# screen, so they are said shorter
WHENS = ['ALWAYS', 'MAP BUTTON HELD', 'DROP BUTTON TOGGLE']
SHOWS = ['NONE', 'D8 MULTIPLIER', 'DAMAGE MULTIPLIER', 'TEAR CAP']
# Specialist's DefaultMusicNames, in modset.h MS_TRACK_* order
DANCE_TRACKS = ['CLASSIC', 'TAINTED', 'SAD (T. CAIN)', 'GLITCHED (T. EDEN)']


def styles():
    """Enhanced Boss Bars' styles for every bar, in bossbars_data.h's order - the
    order the STYLE row counts in.

    The mod lists its twelve boss designs (Beast, Dogma, Mother...) among them;
    those are left out, as bossbars.c leaves them out (the owner, 27.09.2026):
    the bosses wear them through BOSS DESIGNS. A style is a boss design when a
    boss of BB_BOSS wears it - the same rule as general_styles() in the C."""
    import re
    path = os.path.join(tp.ROOT, 'port', 'app', 'src', 'main', 'cpp', 'bossbars_data.h')
    text = open(path, encoding='utf-8').read()
    names = re.findall(r'\{ "([^"]*)"', text[text.index('BB_STYLE['):text.index('#define BB_STYLES')])
    bosses = text[text.index('BB_BOSS[] = {'):text.index('#define BB_BOSSES')]
    worn = {int(m) for m in re.findall(r'\{ \d+, \d+, (-?\d+),', bosses)}
    return [n.upper() for i, n in enumerate(names) if i not in worn]


# modset.h's ids, by name (the header is the authority; read, not copied)
def modset_ids():
    import re
    path = os.path.join(tp.ROOT, 'port', 'app', 'src', 'main', 'cpp', 'modset.h')
    text = open(path, encoding='utf-8').read()
    body = text[text.index('enum {'):text.index('MS_COUNT')]
    names = re.findall(r'\b(MS_[A-Z0-9_]+),', body)
    return {n: i for i, n in enumerate(names)}


# EID's rows are not modset.c's: its language and its glitch switch keep their
# own top-level keys in port-touch.ini (eidlang, eidglitch). Their ids stand
# above the modset table and tweaks.c hands them to eid.c (modset.h).
EID_IDS = {'MS_EID_LANG': 900, 'MS_EID_GLITCH': 901}

# a page: which mod row of the MODS list it belongs to, and its rows:
# (modset id name, component, kind, label, choice words)
PAGES = [
    (0, [  # Specialist for Good Items
        ('MS_DANCE_QUALITY', 0, NUM, 'QUALITY CHECK', None),
        ('MS_DANCE_IGNORE_QUALITY', 0, BOOL, 'IGNORE QUALITY', None),
        ('MS_DANCE_MUSIC', 0, BOOL, 'ENABLE MUSIC', None),
        # the mod's 1.5.1 (04.10.2026): its MCM's words, its order (music, then
        # flashing last); the track names are its DefaultMusicNames
        ('MS_DANCE_DEFAULT_MUSIC', 0, CHOICE, 'DEFAULT MUSIC', DANCE_TRACKS),
        ('MS_DANCE_ALWAYS_DEFAULT', 0, BOOL, 'ALWAYS PLAY DEFAULT MUSIC', None),
        ('MS_DANCE_BLIND', 0, BOOL, 'BYPASS CURSE OF THE BLIND', None),
        ('MS_DANCE_REDUCE_FLASHING', 0, BOOL, 'REDUCE FLASHING', None),
    ]),
    (2, [  # Boss Rush Wave Counter
        ('MS_WAVES_PRESET', 0, CHOICE, 'PRESET', PRESETS),
        ('MS_WAVES_COLOUR', 0, NUM, 'COUNTER R', None),
        ('MS_WAVES_COLOUR', 1, NUM, 'COUNTER G', None),
        ('MS_WAVES_COLOUR', 2, NUM, 'COUNTER B', None),
        ('MS_WAVES_BAR_SCALE', 0, NUM, 'BAR SCALE', None),
        ('MS_WAVES_TEXT_SCALE', 0, NUM, 'FONT SCALE', None),
        ('MS_WAVES_TEXT_NUDGE', 0, NUM, 'VERTICAL ADJUSTMENT', None),
        ('MS_WAVES_X', 0, NUM, 'MOVE X', None),
        ('MS_WAVES_Y', 0, NUM, 'MOVE Y', None),
    ]),
    (3, [  # Enhanced Boss Bars, in its MCM's order
        ('MS_BARS_STYLE', 0, CHOICE, 'STYLE', 'styles'),
        ('MS_BARS_POSITION', 0, CHOICE, 'POSITION', POSITIONS),
        ('MS_BARS_SORTING', 0, CHOICE, 'SORTING', SORTINGS),
        ('MS_BARS_NOTCHES', 0, BOOL, 'SHOW NOTCHES', None),
        ('MS_BARS_PADDING', 0, NUM, 'BAR PADDING', None),
        ('MS_BARS_SCREEN_PADDING', 0, NUM, 'SCREEN PADDING', None),
        ('MS_BARS_PER_ROW', 0, NUM, 'BARS PER ROW', None),
        ('MS_BARS_FLASHING', 0, BOOL, 'ENABLE FLASHING', None),
        ('MS_BARS_INVINCIBLE', 0, BOOL, 'INVINCIBILITY COLORING', None),
        ('MS_BARS_SPIDER_MOD', 0, BOOL, 'SHOW WITH SPIDERMOD', None),
        ('MS_BARS_TEXT', 0, CHOICE, 'TEXT DISPLAY', TEXTS),
        ('MS_BARS_TEXT_ALPHA', 0, NUM, 'TEXT TRANSPARENCY', None),
        ('MS_BARS_TEXT_SIZE', 0, NUM, 'TEXT SIZE', None),
        ('MS_BARS_ICONS', 0, BOOL, 'SHOW ICONS', None),
        ('MS_BARS_BOSS_ICONS', 0, BOOL, 'SHOW CUSTOM ICONS', None),
        ('MS_BARS_STATUS', 0, BOOL, 'SHOW STATUS EFFECTS', None),
        ('MS_BARS_CHAMPION_COLOURS', 0, BOOL, 'USE CHAMPION COLORING', None),
        ('MS_BARS_BEAST', 0, BOOL, 'ENABLE IN BEAST FIGHT', None),
        ('MS_BARS_MOTHER', 0, BOOL, 'ENABLE IN MOTHER FIGHT', None),
        ('MS_BARS_MEGA_SATAN', 0, BOOL, 'ENABLE MEGA SATAN', None),
        ('MS_BARS_BOSS_DESIGNS', 0, BOOL, 'BOSS SPECIFIC BARS', None),
    ]),
    (5, [  # Stats+
        ('MS_STATS_OPACITY', 0, NUM, 'TEXT OPACITY', None),
        ('MS_STATS_BRACKETS', 0, CHOICE, 'BRACKET STYLE', BRACKETS),
        ('MS_STATS_SPACING', 0, NUM, 'SPACING', None),
        ('MS_STATS_CHANGES', 0, BOOL, 'SHOW PROVIDER CHANGES', None),
        ('MS_STATS_TRACK_D8', 0, BOOL, 'D8 SUPPORT', None),
        ('MS_STATS_D8_COLOUR', 0, CHOICE, 'D8 MULTIPLIER COLOR', COLOURS),
        ('MS_STATS_DAMAGE_COLOUR', 0, CHOICE, 'DAMAGE MULTIPLIER COLOR', COLOURS),
        ('MS_STATS_TEARCAP_COLOUR', 0, CHOICE, 'TEAR CAP COLOR', COLOURS),
    ] + [(('MS_STATS_%s_%s' % (stat, part)), 0, CHOICE, '%s: %s' % (name, part),
          WHENS if part == 'WHEN' else SHOWS)
         for stat, name in (('SPEED', 'SPEED'), ('TEARS', 'TEARS'), ('DAMAGE', 'DAMAGE'),
                            ('RANGE', 'RANGE'), ('SHOTSPEED', 'SHOT SPEED'), ('LUCK', 'LUCK'))
         for part in ('WHEN', 'THEN', 'ELSE')]),
    (6, [  # Range Fix: the Bones & Sword mod's MCM, its words (rangefix.c, 04.10.2026)
        ('MS_RANGE_ENABLED', 0, BOOL, 'ENABLED', None),
        ('MS_RANGE_FACTOR', 0, NUM, 'RANGE FACTOR', None),
        ('MS_RANGE_BOC_ENABLED', 0, BOOL, 'ENABLED FOR BOC', None),
        ('MS_RANGE_BOC_FACTOR', 0, NUM, 'RANGE FACTOR FOR BOC', None),
    ]),
    (7, [  # EID, in its MCM's order: General, Display, Visuals, Crafting (24.09.2026)
        ('MS_EID_LANG', 0, CHOICE, 'LANGUAGE', ['AUTO'] + tp.LANG_SHOWN),
        ('MS_EID_HIDE_BATTLE', 0, BOOL, 'HIDE IN BATTLE', None),
        ('MS_EID_DISTANCE', 0, NUM, 'MAX DISTANCE', None),
        ('MS_EID_SHOW_BLIND', 0, BOOL, 'SHOW ON CURSE OF THE BLIND', None),
        ('MS_EID_SHOP_CARDS', 0, BOOL, 'PURCHASEABLE CARD INFO', None),
        ('MS_EID_SHOP_PILLS', 0, BOOL, 'PURCHASEABLE PILL INFO', None),
        ('MS_EID_SHOP_SOULS', 0, BOOL, 'PURCHASEABLE SOULSTONE INFO', None),
        ('MS_EID_UNREACHABLE', 0, BOOL, 'UNREACHABLE CARD/PILL INFO', None),
        ('MS_EID_FLIGHT', 0, BOOL, 'ALWAYS SHOW WITH FLIGHT', None),
        ('MS_EID_UNIDENTIFIED', 0, BOOL, 'UNIDENTIFIED PILL EFFECTS', None),
        ('MS_EID_GLITCH', 0, BOOL, 'GLITCHED ITEM DETAILS', None),
        ('MS_EID_HEALTHUP', 0, BOOL, 'DYNAMIC HEALTH UP TEXT', None),
        ('MS_EID_SPINDOWN', 0, NUM, 'SPINDOWN PREVIEW', None),
        ('MS_EID_OPACITY', 0, NUM, 'OPACITY', None),
        ('MS_EID_X', 0, NUM, 'MOVE X', None),
        ('MS_EID_Y', 0, NUM, 'MOVE Y', None),
        ('MS_EID_STAT_ICONS', 0, BOOL, 'STAT CHANGE ICONS', None),
        ('MS_EID_QUALITY', 0, BOOL, 'DISPLAY QUALITY', None),
        ('MS_EID_TRANSFORM', 0, BOOL, 'TRANSFORMATION INFO', None),
        ('MS_EID_BAG', 0, CHOICE, 'BAG OF CRAFTING DISPLAY', ['ALWAYS', 'HOLD', 'NEVER']),
        ('MS_EID_BAG_RESULTS', 0, NUM, 'RECIPES TO DISPLAY', None),
        ('MS_EID_BAG_SORT', 0, CHOICE, 'SORT RECIPES BY', ['QUALITY', 'NAME']),
    ]),
]

DIGITS = '0123456789-.'


def digit_word(face, ch, target):
    """A character of the number run. The page puts every frame on the digits'
    baseline (tweaks.c, SP_NUM), so a frame cropped to its own drawing sat the
    minus on the line like an underscore (the owner, 24.09.2026: "-15" read as
    "_15"). The minus and the point keep the room below them down to the
    bottom of a 0 set the same way, and stand where the font puts them."""
    if ch not in '-.':
        return tp.word(face, ch, target, False)
    s = dict(lang.SETTINGS[face])
    im, _ = lang.compose_like_native('0' + ch + '0', s['height'], s, target, bold=False)
    alpha = im.getchannel('A')
    box = alpha.getbbox()
    cols = [any(alpha.getpixel((x, y)) for y in range(im.size[1])) for x in range(im.size[0])]
    runs, x = [], 0
    while x < len(cols):                     # the three marks, left to right
        if cols[x]:
            x0 = x
            while x < len(cols) and cols[x]:
                x += 1
            runs.append((x0, x))
        else:
            x += 1
    if len(runs) != 3:
        raise SystemExit('"0%s0" of %s is not three marks: %s' % (ch, face, runs))
    x0, x1 = runs[1]
    mark = alpha.crop((x0, 0, x1, im.size[1])).getbbox()
    return im.crop((x0, mark[1], x1, box[3]))  # from its top down to the 0s' bottom


def build(set_name, suffix, mod_names, show):
    """Every settings page of one set. Answers the rows (the same for every set),
    the words' frames, the pages and one layout record per page."""
    ids = modset_ids()
    ids.update(EID_IDS)
    # THE WORDS ARE ENGLISH IN EVERY SET, and so they are set in the English face:
    # set in the Japanese or Chinese cut, English came out twice as wide and a
    # page wider than the screen. The set's own are the paper, the title - the
    # mod's name in the set's language - and ON and OFF.
    face = 'data'
    target = lang.native_share(Image.open(os.path.join(tp.RES, face, tp.MENU, 'optionsextra.png'))
                               .convert('RGBA'))
    sheet = Image.open(os.path.join(tp.PC, tp.PC_SET[set_name], tp.MENU, 'modsmenu.png')).convert('RGBA')

    words = [digit_word(face, ch, target) for ch in DIGITS]        # frames 0..11
    on, off = tp.onoff_of(set_name)
    onoff_w = max(on[3], off[3])
    digit_w = max(im.width for im in words)
    rows, pages, runs = [], [], {}
    style_words = styles()
    for mod_row, spec in PAGES:
        first = len(rows)
        for name, comp, kind, label, choices in spec:
            if choices == 'styles':
                choices = style_words
            lf = len(words)
            words.append(tp.word(face, label, target, True))
            cf, cn = -1, 0
            if kind == CHOICE:                   # a choice's words are always a run
                key = tuple(choices)
                if key not in runs:              # the same list is laid in once
                    runs[key] = len(words)
                    words += [tp.word(face, c, target, False) for c in choices]
                cf, cn = runs[key], len(choices)
            rows.append(dict(ms=ids[name], comp=comp, kind=kind, label=lf, choice=cf, count=cn))
        pages.append((mod_row, first, len(spec)))

    def value_w(r):
        if r['kind'] == BOOL:
            return onoff_w
        if r['kind'] == CHOICE:
            return max(words[r['choice'] + k].width for k in range(r['count']))
        return digit_w * 5                       # a number is at most five characters

    sheet_w = max(im.width for im in words)
    out = Image.new('RGBA', (sheet_w, sum(im.height for im in words)), (0, 0, 0, 0))
    frames, y = [], 0
    for im in words:
        out.alpha_composite(im, (0, y))
        frames.append((0, y, im.width, im.height))
        y += im.height
    out.save(os.path.join(tp.ASSETS, 'port_modset_words_%s.png' % suffix))

    records = []
    for p, (mod_row, first, count) in enumerate(pages):
        page_rows = rows[first:first + count]
        widest = max(tp.CURSOR_W + words[r['label']].width + tp.GAP + value_w(r) for r in page_rows)
        extra = max(0, widest + tp.MODS_MARGIN * 2 - 256)
        extra += extra % 2
        paper = tp.mods_paper(sheet, count, extra,
                              title=tp.title_art(set_name, mod_names[mod_row], gap=tp.TITLE_GAP_MODS))
        page_png = 'port_modset_page_%d_%s.png' % (p, suffix)
        paper.save(os.path.join(tp.ASSETS, page_png))
        tp.write_mods_anm2(suffix, paper.size, frames, on, off, page_png=page_png,
                           words_png='port_modset_words_%s.png' % suffix,
                           out_name='port_modset_menu_%d_%s.anm2' % (p, suffix))
        records.append(dict(suffix=suffix, x=240 - paper.width // 2, w=paper.width, h=paper.height,
                            step=tp.MODS_STEP, baseline=tp.MODS_STEP // 2 + 8,
                            row0=tp.MODS_ROW0, on_w=on[3], off_w=off[3]))
        print('  %-8s settings page %d: %d rows, paper %d x %d, widest line %d'
              % (set_name, p, count, paper.width, paper.height, widest))
    return dict(rows=rows, pages=pages, frames=frames, records=records)
