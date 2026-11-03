# -*- coding: utf-8 -*-
"""The mod's markup tables, turned into a C table the port can read.

Descriptions are not plain text: they carry markers in double braces. Three
kinds, and the counts are from the strings we actually ship (20394 uses):

    {{Damage}} {{Heart}} ...   an icon out of one of the mod's four atlases
    {{ColorGreen}} {{CR}}      the colour of the text that follows
    {{Collectible105}}         the item's own sprite, out of the game's atlas

The first two are decided once, here, from the mod's own Lua: the icon table
names an animation and a frame in an .anm2, and the colour table is a plain
list of RGBA. Both are written out as a C header rather than parsed on the
phone - they never change, and the port has no Lua.

    py -3 tools/eid_icons.py <the mod's source> [the header to write]
"""
import io
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

SHEETS = {                              # the Lua sprite object -> our sheet number
    'EID.InlineIconSprite': 0,
    'EID.IconSprite': 1,
    'EID.CardPillSprite': 2,
    'EID.PlayerSprite': 3,
}
SHEET_FILES = [
    'eid_inline_icons',
    'eid_transform_icons',
    'eid_cardspills',
    'eid_player_icons',
]

# Repentance picks the second spelling of each of these (eid_data.lua, 698-706)
REPENTANCE = ['Damage', 'Speed', 'Tears', 'Range', 'Shotspeed', 'Luck',
              'AngelChance', 'DevilChance', 'Tearsize']

# the mod's own spelling fixes and shortcuts (EID.TextReplacementPairs)
ALIASES = {
    'CR': 'ColorReset',
    'IND': 'Indent',
    'EthernalHeart': 'EternalHeart',
    'MimicChest': 'TrapChest',
    'EternalChest': 'HolyChest',
    'BombChest': 'StoneChest',
    'OldChest': 'DirtyChest',
    'CurseRoom': 'CursedRoom',
    'Crawlspace': 'LadderRoom',
    'GoldHeart': 'GoldenHeart',

    # The mod names three of its colours with a function rather than a value,
    # and the functions answer with these when nothing is configured.
    'ColorError': 'ColorEIDError',
    'ColorTransform': 'ColorEIDTransform',
    'ColorObjName': 'ColorEIDObjName',

    # Mistakes in the mod's own text, found by checking every marker in the
    # strings we ship against the tables: the German translation writes the
    # icon names in German, and two markers are simply mistyped. The mod shows
    # nothing at all in these places; here they show the picture that was meant.
    'Verwirrung': 'Confusion',
    'Brennen': 'Burning',
    'Furcht': 'Fear',
    'Vergiftung': 'Poison',
    'HeiligerMantel': 'HolyMantle',
    'Magnetisieren': 'Magnetize',
    'Blood Donation Machine': 'BloodDonationMachine',
    'Bom': 'Bomb',
}


def block(text, head, stop):
    i = text.index(head)
    return text[i:text.index(stop, i)]


def icons(text):
    """name -> (sheet, animation, frame, width, height, left, top)"""
    out = {}
    for name, body in re.findall(r'\["([^"]+)"\]\s*=\s*\{([^{}]*)\}',
                                 block(text, 'EID.InlineIcons = {', '\nEID.InlineColors')):
        parts = [p.strip() for p in body.split(',')]
        if not parts or not parts[0].startswith('"'):
            continue                            # a function: the controller icons
        anim = parts[0].strip('"')
        def num(i, default=0):
            if i >= len(parts) or not parts[i]:
                return default
            try:
                return int(parts[i])
            except ValueError:
                return default
        sheet = SHEETS.get(parts[6].strip(), 0) if len(parts) > 6 else 0
        out[name] = (sheet, anim, num(1), num(2), num(3), num(4, -1), num(5))
    for name in REPENTANCE:                     # the Repentance spelling wins
        if name + 'REP' in out:
            out[name] = out[name + 'REP']
    return out


def colours(text):
    """name -> (r, g, b, a), the plain ones; the callbacks are left to the port"""
    out = {}
    for name, body in re.findall(r'\["([^"]+)"\]\s*=\s*KColor\(([^)]*)\)',
                                 block(text, 'EID.InlineColors = {', '\nEID.ColorBlindColors')):
        parts = [p.strip() for p in body.split(',')]
        if len(parts) < 4:
            continue
        try:
            out[name] = tuple(float(p) for p in parts[:4])
        except ValueError:
            continue
    return out


KIND = {'passive': 0, 'active': 1, 'familiar': 2, 'trinket': 3}
CHARGE = {'normal': 0, 'timed': 1, 'special': 2}


def items(path, metadata):
    """id -> (picture, kind, quality, charges, charge type), for both kinds.

    {{Collectible105}} is not an icon in the mod's atlas - it is the item's own
    picture, which the mod asks the engine for by id. The engine keeps that in
    its item configuration; the same answers are in the game's own XML, read
    once here rather than reached for at runtime. The mod prints the quality
    beside the name and, for an active, its charge; both come from here too.
    """
    text = io.open(path, encoding='utf-8', errors='replace').read()
    coll, trin = {}, {}
    for tag, body in re.findall(r'<(passive|active|familiar|trinket)\s+([^>]*)>', text):
        a = dict(re.findall(r'(\w+)="([^"]*)"', body))
        if 'id' not in a or 'gfx' not in a:
            continue
        row = [a['gfx'], KIND[tag], 0,
               int(a.get('maxcharges', 0) or 0),
               CHARGE.get(a.get('chargetype', 'normal'), 0)]
        (trin if tag == 'trinket' else coll)[int(a['id'])] = row

    meta = io.open(metadata, encoding='utf-8', errors='replace').read()
    for tag, body in re.findall(r'<(item|trinket)\s+([^>]*)/>', meta):
        a = dict(re.findall(r'(\w+)="([^"]*)"', body))
        if 'id' not in a:
            continue
        table = trin if tag == 'trinket' else coll
        row = table.get(int(a['id']))
        if row:
            row[2] = int(a.get('quality', 0) or 0)
    return coll, trin


# the engine's form order (playerforms.xml) against the mod's icon names, whose
# animations are numbered in the MOD's order - the two lists are not the same
FORM_ICONS = [
    'Guppy',        # 0
    'Beelzebub',    # 1
    'FunGuy',       # 2
    'Seraphim',     # 3
    'Bob',          # 4
    'Spun',         # 5
    'Mom',          # 6
    'Conjoined',    # 7
    'Leviathan',    # 8
    'OhCrap',       # 9
    'Bookworm',     # 10
    'Adult',        # 11
    'SpiderBaby',   # 12
    'Stompy',       # 13
]


def forms(playerforms, metadata, stringtable):
    """(item -> form, form -> names by language column, form -> name key)

    A transformation is counted by the engine from the item's TAGS: each form
    names one tag (playerforms.xml) and each item carries its tags
    (items_metadata.xml). The names come from the game's own string table, in
    the game's own languages, so they read the way the rest of the game does.
    """
    text = io.open(playerforms, encoding='utf-8', errors='replace').read()
    tag_of, key_of = {}, {}
    for body in re.findall(r'<form\s+([^>]*)/>', text):
        a = dict(re.findall(r'(\w+)="([^"]*)"', body))
        i = int(a['id'])
        key_of[i] = a.get('name', '').lstrip('#')
        for tag in a.get('tags', '').split():
            tag_of[tag] = i

    text = io.open(metadata, encoding='utf-8', errors='replace').read()
    item_form = {}
    for body in re.findall(r'<item\s+([^>]*)/>', text):
        a = dict(re.findall(r'(\w+)="([^"]*)"', body))
        for tag in a.get('tags', '').split():
            if tag in tag_of:
                item_form[int(a['id'])] = tag_of[tag]

    text = io.open(stringtable, encoding='utf-8', errors='replace').read()
    names = {}
    for key, body in re.findall(r'<key name="([^"]+)">(.*?)</key>', text, re.S):
        if not key.startswith('TRANSFORMATION_'):
            continue
        names[key] = re.findall(r'<string>(.*?)</string>', body, re.S)
    return item_form, names, key_of


def headers(folder):
    """language -> (dice room header, sacrifice room header)

    Two texts that belong to no item: they head the dice room and sacrifice
    room descriptions. They live in the mod's per-language files rather than in
    its description tables, so they are picked out here.
    """
    out = {}
    for name in os.listdir(folder):
        if not name.endswith('.lua'):
            continue
        text = io.open(os.path.join(folder, name), encoding='utf-8', errors='replace').read()
        dice = re.search(r'diceHeader\s*=\s*"([^"]*)"', text)
        sacr = re.search(r'sacrificeHeader\s*=\s*"([^"]*)"', text)
        pill = re.search(r'unidentifiedPill\s*=\s*"([^"]*)"', text)
        if dice or sacr or pill:
            out[name[:-4]] = (dice.group(1) if dice else '',
                              sacr.group(1) if sacr else '',
                              pill.group(1) if pill else '')
    return out


def form_key(name):
    """a transformation's name as both the game and EID spell it: 'Yes mother?!' ~ 'Yes Mother?'"""
    return re.sub(r'[^a-z]', '', name.lower())


def arabic_names(folder):
    """English transformation name (form_key) -> the Arabic one, from the pair
    tools/eid_arabic.py writes; {} when it has not been made"""
    sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
    from eid_more import table_body, lua_strings
    out = {}
    lists = []
    for code in ('en_us', 'ar'):
        path = os.path.join(folder, code + '.lua')
        if not os.path.exists(path):
            return {}
        body = table_body(io.open(path, encoding='utf-8').read(), 'EID.descriptions[languageCode].transformations')
        lists.append(lua_strings(body or ''))
    for en, ar in zip(*lists):
        if en and ar and ar != en:
            out[form_key(en)] = ar
    return out


ARABIC_PAIR = os.path.join(ROOT, 'work', 'eid-ar', 'descriptions', 'ab+')


def write(path, ico, col, coll, trin, item_form, names, key_of, heads):
    arabic_forms = arabic_names(ARABIC_PAIR)
    lines = []
    lines.append('/*')
    lines.append(' * Generated by tools/eid_icons.py from the mod\'s own Lua - do not edit.')
    lines.append(' *')
    lines.append(' * An icon names an animation and a frame in one of four .anm2 files, which')
    lines.append(' * is exactly what the engine\'s own sprite wants; the width is what the text')
    lines.append(' * layout advances by. A colour is plain RGBA.')
    lines.append(' */')
    lines.append('')
    lines.append('static const char *const EID_SHEETS[] = {')
    for f in SHEET_FILES:
        lines.append('    "gfx/%s.anm2",' % f)
    lines.append('};')
    lines.append('')
    lines.append('typedef struct {')
    lines.append('    const char *name;')
    lines.append('    const char *anim;')
    lines.append('    unsigned char sheet, frame;')
    lines.append('    signed char width, height, left, top;')
    lines.append('} eid_icon_t;')
    lines.append('')
    # The mod plays these three straight from a sprite instead of looking them
    # up (main.lua, "Display ItemType / Charge"), so they are added here to be
    # reachable as markers like everything else.
    ico = dict(ico)
    # the mod moves on by ONE unit after the active's icon: the charge number
    # is drawn almost on top of it (main.lua). The familiar has no charge, so
    # it keeps the full eight.
    ico['active'] = (1, 'active', 0, 1, 8, 0, 2)
    ico['familiar'] = (1, 'familiar', 0, 8, 8, 0, 2)
    ico['ChargeSpecial'] = (0, 'numbers', 13, 6, 7, 0, 0)

    lines.append('static const eid_icon_t EID_ICONS[] = {')
    for name in sorted(ico):
        sheet, anim, frame, w, h, left, top = ico[name]
        lines.append('    { "%s", "%s", %d, %d, %d, %d, %d, %d },'
                     % (name, anim, sheet, frame, w, h, left, top))
    lines.append('};')
    lines.append('')
    lines.append('typedef struct { const char *name; float r, g, b, a; } eid_colour_t;')
    lines.append('')
    lines.append('static const eid_colour_t EID_COLOURS[] = {')
    for name in sorted(col):
        r, g, b, a = col[name]
        lines.append('    { "%s", %.4ff, %.4ff, %.4ff, %.4ff },' % (name, r, g, b, a))
    lines.append('};')
    lines.append('')
    lines.append('/* the mod\'s own shortcuts and spelling fixes */')
    lines.append('typedef struct { const char *from, *to; } eid_alias_t;')
    lines.append('')
    lines.append('static const eid_alias_t EID_ALIASES[] = {')
    for a in sorted(ALIASES):
        lines.append('    { "%s", "%s" },' % (a, ALIASES[a]))
    lines.append('};')
    lines.append('/*')
    lines.append(' * The game\'s own item pictures, from its items.xml. The file names are')
    lines.append(' * relative to gfx/items/collectibles and gfx/items/trinkets.')
    lines.append(' */')
    lines.append('typedef struct {')
    lines.append('    unsigned short id;')
    lines.append('    const char *gfx;')
    lines.append('    unsigned char kind;         /* 0 passive, 1 active, 2 familiar, 3 trinket */')
    lines.append('    unsigned char quality;      /* 0..4, printed beside the name */')
    lines.append('    unsigned char charges;      /* an active\'s charge, 0 for the rest */')
    lines.append('    unsigned char chargetype;   /* 0 normal, 1 timed, 2 special */')
    lines.append('} eid_item_t;')
    lines.append('')
    for name, table in (('EID_COLLECTIBLES', coll), ('EID_TRINKETS', trin)):
        lines.append('static const eid_item_t %s[] = {' % name)
        for i in sorted(table):
            gfx, kind, quality, charges, chargetype = table[i]
            lines.append('    { %d, "%s", %d, %d, %d, %d },'
                         % (i, gfx, kind, quality, min(charges, 255), chargetype))
        lines.append('};')
        lines.append('')
    lines.append('/*')
    lines.append(' * Transformations. Which item counts towards which comes from the game\'s')
    lines.append(' * own tags (playerforms.xml against items_metadata.xml), and the names from')
    lines.append(' * its string table - one column per language, in the order the table lists')
    lines.append(' * them: 0 English, 1 Japanese, 2 Korean, 3 Chinese, 4 Russian, 5 German,')
    lines.append(' * 6 Spanish, 7 French, 8 unused - and 9 Arabic, which the game has not: the')
    lines.append(' * "Arabic EID" mod\'s names (tools/eid_arabic.py), matched to the English.')
    lines.append(' */')
    lines.append('typedef struct { unsigned short id; unsigned char form; } eid_form_item_t;')
    lines.append('')
    lines.append('static const eid_form_item_t EID_FORM_ITEMS[] = {')
    for i in sorted(item_form):
        lines.append('    { %d, %d },' % (i, item_form[i]))
    lines.append('};')
    lines.append('')
    lines.append('#define EID_FORMS 14')
    lines.append('#define EID_FORM_LANGS 10')
    lines.append('')
    lines.append('static const char *const EID_FORM_ICON[EID_FORMS] = {')
    for name in FORM_ICONS:
        lines.append('    "%s",' % name)
    lines.append('};')
    lines.append('')
    lines.append('static const char *const EID_FORM_NAME[EID_FORMS][EID_FORM_LANGS] = {')
    for i in range(len(FORM_ICONS)):
        row = names.get(key_of.get(i, ''), [])
        row = (row + [''] * 9)[:9]
        row.append(arabic_forms.get(form_key(row[0]), ''))
        lines.append('    { %s },' % ', '.join('"%s"' % s.replace('"', '\\"') for s in row))
    lines.append('};')
    lines.append('')
    lines.append('/* texts that belong to a room or to a state rather than to an item */')
    lines.append('typedef struct {')
    lines.append('    const char *lang, *dice, *sacrifice, *unidentified;')
    lines.append('} eid_header_t;')
    lines.append('')
    lines.append('static const eid_header_t EID_HEADERS[] = {')
    for lang in sorted(heads):
        dice, sacr, pill = heads[lang]
        lines.append('    { "%s", "%s", "%s", "%s" },' % (lang, dice, sacr, pill))
    lines.append('};')
    lines.append('')
    io.open(path, 'w', encoding='utf-8', newline='\n').write('\n'.join(lines))
    print('%s: %d icons, %d colours, %d aliases, %d collectibles, %d trinkets, '
          '%d items count towards %d transformations, %d language headers'
          % (path, len(ico), len(col), len(ALIASES), len(coll), len(trin),
             len(item_form), len(set(item_form.values())), len(heads)))


if __name__ == '__main__':
    src = sys.argv[1]
    dst = sys.argv[2] if len(sys.argv) > 2 else os.path.join(ROOT, 'port', 'app', 'src', 'main', 'cpp', 'eid_icons.h')
    text = io.open(os.path.join(src, 'features', 'eid_data.lua'), encoding='utf-8').read()
    game = os.path.join(ROOT, 'work', 'app', 'repentance-resources', 'data', 'items.xml')
    coll, trin = items(game, os.path.join(ROOT, 'work', 'app', 'repentance-resources', 'data', 'items_metadata.xml'))
    root = os.path.join(ROOT, 'work', 'app', 'repentance-resources', 'data', '')
    item_form, names, key_of = forms(root + 'playerforms.xml',
                                     root + 'items_metadata.xml',
                                     root + 'stringtable.sta')
    heads = headers(os.path.join(src, 'descriptions', 'ab+'))
    if os.path.isdir(ARABIC_PAIR):                 # Arabic is not in the mod: its pair is ours
        heads.update((k, v) for k, v in headers(ARABIC_PAIR).items() if k == 'ar')
    write(dst, icons(text), colours(text), coll, trin, item_form, names, key_of, heads)
