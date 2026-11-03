# -*- coding: utf-8 -*-
"""Extra tables from the mod's own Lua, appended to eid.bin.

The database of descriptions was packed once (eid_pack.py) and the JSON it was
made from is gone, so this tool does NOT rebuild it: it reads the finished
eid.bin, adds sections of its own to every language, and writes the file back.
What is already in there comes out byte for byte the same, and running the tool
twice changes nothing.

    py -3 tools/eid_more.py <mod dir> [eid.bin]

The mod keeps these tables in its language files as plain Lua, one file per
language, and this reads them with a small parser rather than running Lua:

    EID.descriptions[languageCode].birthright = { {name, "", text}, ... }
    EID.descriptions[languageCode].<phrase>   = "..."
    EID.descriptions[languageCode].goldenTrinketEffects = { [id] = {"..", ..} }

New section kinds, numbered on from the ones eid_pack.py writes:

    8  birthright   id = player type, name = character, text = what it does
    9  phrases      id = PHRASE_*, text = the phrase; name is empty
   10  golden       id = trinket, text = its golden lines, one per JOIN

A language that has no phrase of its own is given the English one, which is
what the mod itself falls back to. The tables live in two of the mod's folders
- the Repentance one and the Afterbirth+ one - and both are read.
"""
import io
import os
import re
import struct
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

# the phrases the port asks for by number; the order is the port's PHRASE_* enum
PHRASES = [
    'goldenTrinket',        # 0  "Effect doubled!"
    'tripledTrinket',       # 1  "Effect tripled!"
    'quadrupledTrinket',    # 2  "Effect quadrupled!"
    'VariableCharge',       # 3  "{1} charge:"
    'ResultsWithX',         # 4  "(Results with {1})"
    'spindownError',        # 5  "Item disappears"
    'BlankCardEffect',      # 6  "Blank Card effect:"
    'SingleUseInfo',        # 7  "SINGLE USE"
    'LuckModifier',         # 8  "{1}% chance at {2} luck"
    'BlackFeatherInformation',  # 9  "{1} evil items: +{2} damage"
    'FalsePHDDamage',       # 10  what False PHD turns a bad pill into
    'FalsePHDHorseDamage',  # 11  the same for a horse pill
    'FalsePHDHeart',        # 12  and when it gives a black heart instead
    'ExperimentalPillPHD',      # 13  the experimental pill with PHD and friends
    'ExperimentalPillFalsePHD', # 14  and with False PHD
    'CraftingBagContent',   # 15  "Bag:"
    'CraftingRoomContent',  # 16  "Room:"
    'CraftingFloorContent', # 17  "Floor:"
    'CraftingMore',         # 18  "...+{1} more"
    'CraftingResults',      # 19  the controls line under the list
]

KIND_BIRTHRIGHT = 8
KIND_PHRASE = 9
KIND_GOLDEN = 10
KIND_GOLDEN_DATA = 11
KIND_CONDITION = 12
KIND_RED_TO = 13                        # "+1 Health" -> "+1 Soul Heart", by heart type
KIND_PLURAL = 14                        # the language's own plural endings
KIND_PRIZE = 17                         # the words Consolation Prize prints
KIND_GLITCH = 18                        # GlitchedItemText: what a TMTRAINER item's parts are called

# GlitchedItemText, keyed the way the port asks (eid_glitch.c): the cache flags
# keep their own numbers 0..16, then these fixed rows, and the entity names get
# 100 + their place in the English table, the key itself kept as the row's name
GLITCH_ROWS = {
    'cacheFlagStart': 20,
    'AddMaxHearts': 21, 'AddHearts': 22, 'AddSoulHearts': 23, 'AddBlackHearts': 24,
    'AddBombs': 25, 'AddCoins': 26, 'AddKeys': 27,
    # the trigger names, 30 + REPENTOGON's order (the game's own enum, 0x5d0b38)
    'active': 30, 'tear_fire': 31, 'enemy_hit': 32, 'enemy_kill': 33, 'damage_taken': 34,
    'room_clear': 35, 'entity_spawned': 36, 'pickup_collected': 37, 'chain': 38,
    # the action names, 40 + the game's action order (the switch at 0x5d0cac)
    'use_active_item': 40, 'add_temporary_effect': 41, 'convert_entities': 42,
    'area_damage': 43, 'spawn_entity': 44, 'fart': 45,
    'grants': 50,
}
GLITCH_ENTITY_FIRST = 100

# the stats it can name, and the pickups, in the mod's own order
PRIZE_STAT_ICONS = ('Speed', 'Tears', 'Damage', 'Range', 'Shotspeed', 'Luck')
PRIZE_PICKUPS = ('5.20', '5.40', '5.30', '5.10.3', '5.10')
MINE = (KIND_BIRTHRIGHT, KIND_PHRASE, KIND_GOLDEN, KIND_GOLDEN_DATA, KIND_CONDITION,
        KIND_RED_TO, KIND_PLURAL, KIND_PRIZE, KIND_GLITCH)

# the heart a character has instead of red ones; 0 is red and needs no rewrite
HEARTS = ('Red', 'Soul', 'Black', 'Bone', 'Coin', 'None')

# what goldenTrinketData says about a trinket, written out for the port to read
GOLDEN_ONLY, GOLDEN_FIND, GOLDEN_APPEND, GOLDEN_FULL = 1, 2, 4, 8

FOLDERS = ('rep', 'ab+')                # the folders the tables above live in
JOIN = chr(31)                          # between the lines of one golden entry
FALLBACK = 'en_us'                      # the language the mod itself falls back to


def lua_strings(text):
    """Every Lua string literal in a piece of source, in order."""
    out = []
    i = 0
    while i < len(text):
        c = text[i]
        if c in '"\'':
            quote = c
            i += 1
            buf = []
            while i < len(text) and text[i] != quote:
                if text[i] == chr(92) and i + 1 < len(text):
                    nxt = text[i + 1]
                    buf.append({'n': '\n', 't': '\t'}.get(nxt, nxt))
                    i += 2
                    continue
                buf.append(text[i])
                i += 1
            i += 1
            out.append(''.join(buf))
            continue
        i += 1
    return out


def strip_comments(text):
    """Lua comments, line and block, with string literals left alone.

    The block ones matter: the mod's own files carry a worked EXAMPLE of a
    golden-trinket table inside --[[ ]], and a parser that cannot see the
    brackets reads that example as if it were data.
    """
    out = []
    i = 0
    while i < len(text):
        c = text[i]
        if c in '"\'':
            quote = c
            out.append(c)
            i += 1
            while i < len(text):
                out.append(text[i])
                if text[i] == chr(92) and i + 1 < len(text):
                    out.append(text[i + 1])
                    i += 2
                    continue
                if text[i] == quote:
                    i += 1
                    break
                i += 1
            continue
        if c == '-' and text[i:i + 2] == '--':
            block = re.match(r'--(\[=*\[)', text[i:])
            if block:                                   # --[[ ... ]] , --[==[ ... ]==]
                close = ']' + '=' * (len(block.group(1)) - 2) + ']'
                end = text.find(close, i + len(block.group(0)))
                i = len(text) if end < 0 else end + len(close)
                continue
            while i < len(text) and text[i] != '\n':
                i += 1
            continue
        out.append(c)
        i += 1
    return ''.join(out)


def table_body(text, header):
    """The text between the braces of `header = {` ... matching `}`.

    The name has to be followed by an `=`: the mod mentions its tables by name
    in other places too, and a plain search for the name lands on those.
    """
    at = -1
    for m in re.finditer(re.escape(header) + r'\s*=\s*\{', text):
        at = m.end() - 1
        break
    if at < 0:
        return None
    depth = 0
    i = at
    while i < len(text):
        if text[i] == '{':
            depth += 1
        elif text[i] == '}':
            depth -= 1
            if depth == 0:
                return text[at + 1:i]
        elif text[i] in '"\'':
            quote = text[i]
            i += 1
            while i < len(text) and text[i] != quote:
                i += 2 if text[i] == chr(92) else 1
        i += 1
    return None


def braced_rows(body):
    """[(text between one pair of outer braces, where it started)] of a table."""
    rows = []
    depth = 0
    start = None
    i = 0
    while i < len(body):
        c = body[i]
        if c in '"\'':
            quote = c
            i += 1
            while i < len(body) and body[i] != quote:
                i += 2 if body[i] == chr(92) else 1
        elif c == '{':
            if depth == 0:
                start = i
            depth += 1
        elif c == '}':
            depth -= 1
            if depth == 0 and start is not None:
                rows.append((body[start:i + 1], start))
                start = None
        i += 1
    return rows


def birthright_of(source):
    """[(player type, name, text)] from the mod's birthright table."""
    body = table_body(source, 'EID.descriptions[languageCode].birthright')
    if body is None:
        return []
    rows = []
    for piece, _ in braced_rows(body):
        parts = lua_strings(piece)
        if len(parts) >= 3:
            rows.append((len(rows), parts[0], parts[2]))
        elif parts:
            rows.append((len(rows), parts[0], ''))
    return rows


def golden_of(source):
    """{trinket: [line, ...]} from the mod's goldenTrinketEffects table."""
    body = table_body(source, 'EID.descriptions[languageCode].goldenTrinketEffects')
    if body is None:
        return {}
    found = {}
    for piece, start in braced_rows(body):
        key = re.search(r'\[\s*(\d+)\s*\]\s*=\s*$', body[:start])
        if not key:
            continue
        lines = lua_strings(piece)
        if lines:
            found[int(key.group(1))] = lines
    return found


def golden_entry(piece):
    """One goldenTrinketData value, written the way the port reads it.

    The mod's value is either a bare number - shorthand for "multiply this one
    number in the description" - or a table of settings. Both come out as a
    line of `key=value` fields:

        t=10,0.4  the numbers to multiply, spelled as the mod spells them
        mult=2    the ceiling for the multiplier
        mults=1.5,2   the multiplier itself, for one doubler and for both
        add=1,2,3     what to add after multiplying
        flags=3       goldenOnly 1, findReplace 2, append 4, fullReplace 8
    """
    piece = piece.strip().rstrip(',')
    fields = []
    if re.match(r'^-?[\d.]+$', piece):
        return 't=' + piece
    body = piece[1:-1] if piece.startswith('{') else piece
    nums = re.search(r'\bt\s*=\s*\{([^}]*)\}', body)
    if nums:
        fields.append('t=' + ','.join(v.strip() for v in nums.group(1).split(',') if v.strip()))
    mult = re.search(r'\bmult\s*=\s*([\d.]+)', body)
    if mult:
        fields.append('mult=' + mult.group(1))
    mults = re.search(r'\bmults\s*=\s*\{([^}]*)\}', body)
    if mults:
        fields.append('mults=' + ','.join(v.strip() for v in mults.group(1).split(',') if v.strip()))
    adds = re.search(r'\badditions\s*=\s*\{([^}]*)\}', body)
    if adds:
        fields.append('add=' + ','.join(v.strip() for v in adds.group(1).split(',') if v.strip()))
    flags = 0
    if re.search(r'\bgoldenOnly\s*=\s*true', body): flags |= GOLDEN_ONLY
    if re.search(r'\bfindReplace\s*=\s*true', body): flags |= GOLDEN_FIND
    if re.search(r'\bappend\s*=\s*true', body): flags |= GOLDEN_APPEND
    if re.search(r'\bfullReplace\s*=\s*true', body): flags |= GOLDEN_FULL
    if flags:
        fields.append('flags=%d' % flags)
    return ';'.join(fields)


def golden_data_of(source, header):
    """{trinket: written-out entry} from a goldenTrinketData-shaped table."""
    body = table_body(source, header)
    if body is None:
        return {}
    found = {}
    for key in re.finditer(r'\[\s*(\d+)\s*\]\s*=\s*', body):
        rest = body[key.end():]
        if rest.startswith('{'):
            piece = braced_rows(rest)
            if not piece or piece[0][1] != 0:
                continue
            value = piece[0][0]
        else:
            value = rest.split(',')[0]
        written = golden_entry(value)
        if written:
            found[int(key.group(1))] = written
    return found


# --------------------------------------------------- the conditional strings
#
# The mod's conditional descriptions - "Overridden by {1}", "No effect for
# {1}", the Car Battery and BFFS! tables - are looked up by a KEY rather than
# by a number: "5.100.395 (Overridden)", "Overridden", or a bare item id. They
# all go into one section, the key hashed so the port can binary-search it, and
# the table's own name is part of the key so the six tables cannot collide.
#
# A value is either one string, which the mod appends, or a list, which it
# treats as a replacement (one entry) or as find-and-replace pairs (two or
# more). The port needs to tell those apart, and needs to know which pieces
# were numbers, so a value is written as:
#
#     S<the string>                       one string
#     T<piece><0x1f><piece>...            a list, each piece n<number> or s<text>
#
LOC_TABLES = ('ConditionalDescs', 'carBattery', 'BFFSSynergies',
              'tarotClothBuffs', 'tarotClothBuffsAB', 'bookOfBelialBuffs',
              'bingeEaterBuffs')


def key_hash(text):
    """FNV-1a over the key's bytes; the port hashes it the same way."""
    h = 2166136261
    for b in text.encode('utf-8'):
        h = ((h ^ b) * 16777619) & 0xffffffff
    return h


def skip_space(text, i):
    while i < len(text):
        if text[i] in ' \t\r\n':
            i += 1
        elif text[i:i + 2] == '--':                 # comments are already gone,
            while i < len(text) and text[i] != '\n':  # but be safe about stray ones
                i += 1
        else:
            break
    return i


def lua_value(text, i):
    """One Lua value at `i`: a number, a string, or a table. (value, next)"""
    i = skip_space(text, i)
    if i >= len(text):
        return None, i
    c = text[i]
    if c in '"\'':
        quote = c
        i += 1
        buf = []
        while i < len(text) and text[i] != quote:
            if text[i] == chr(92) and i + 1 < len(text):
                buf.append({'n': '\n', 't': '\t'}.get(text[i + 1], text[i + 1]))
                i += 2
                continue
            buf.append(text[i])
            i += 1
        return ''.join(buf), i + 1
    if c == '{':
        items = []
        i += 1
        while True:
            i = skip_space(text, i)
            if i >= len(text) or text[i] == '}':
                return items, i + 1
            key = None
            if text[i] == '[':
                key, i = lua_value(text, i + 1)
                i = skip_space(text, i)
                if i < len(text) and text[i] == ']':
                    i += 1
                i = skip_space(text, i)
                if i < len(text) and text[i] == '=':
                    i += 1
            else:
                name = re.match(r'([A-Za-z_]\w*)\s*=(?!=)', text[i:])
                if name:
                    key = name.group(1)
                    i += name.end()
            value, i = lua_value(text, i)
            items.append((key, value))
            i = skip_space(text, i)
            if i < len(text) and text[i] == ',':
                i += 1
    num = re.match(r'-?\d+(?:\.\d+)?', text[i:])
    if num:
        s = num.group(0)
        return (float(s) if '.' in s else int(s)), i + num.end()
    word = re.match(r'[A-Za-z_][\w.:\[\]"\']*', text[i:])
    if word:
        return ('lua', word.group(0)), i + word.end()
    return None, i + 1


def spell(value):
    """A key or a find-piece as the mod spells it."""
    if isinstance(value, float):
        return ('%.14g' % value)
    return str(value)


def plain(value):
    """A parsed table value as a plain string or list, whatever it came as."""
    if isinstance(value, str):
        return value
    return [v if not isinstance(v, tuple) else v[1] for v in value]


def wordless(value):
    """No words at all - only numbers, markers and punctuation.

    Car Battery turning "40" into "80", BFFS! turning "75" into "150": entries
    like that say the same thing in every language, so a language that has not
    got one can be given the English one. That is data, not translation.
    """
    for v in ([value] if isinstance(value, str) else plain(value)):
        if isinstance(v, (int, float)):
            continue
        bare = re.sub(r'\{\{[^}]*\}\}', '', str(v))
        if re.search(r'[A-Za-z]', bare):
            return False
    return True


DESC_KINDS = (1, 2, 3, 4, 5)            # collectibles, trinkets, cards, pills, horse pills


def game_names():
    """The game's own item names, by language: tools/eid_lang/names.json."""
    path = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'eid_lang', 'names.json')
    if not os.path.exists(path):
        return {}
    import json
    return json.load(io.open(path, encoding='utf-8'))


def with_ours(code, sections, english, names, put):
    """The language's own sections, with what we filled in laid over them.

    Two sources, both used ONLY where the language has nothing of its own - an
    entry the mod translated is never touched:

      * names.json, the name out of the GAME's own string table, for entries
        still carrying the English name;
      * <code>-desc.json, a description we translated ourselves, for entries
        the mod's pack is missing or leaves in English (German is short 101
        trinkets, 39 cards and 41 pills, and carries 101 collectibles in
        English word for word).
    """
    mine = our_words(code + '-desc')
    named = names.get(code, {})
    if not mine and not named:
        return sections
    out = []
    for kind, rows in sections:
        if kind not in DESC_KINDS:
            out.append((kind, rows))
            continue
        table = dict((i, [n, x]) for i, n, x in rows)
        for ident, (name_at, text_at) in english.get(kind, {}).items():
            key = '%d|%d' % (kind, ident)
            row = table.get(ident)
            written = mine.get(key)
            if row is None:
                # the mod falls back to English for a description a language
                # has not got; the port used to show nothing at all
                table[ident] = [name_at, text_at]
                row = table[ident]
            if written and row[1] == text_at:
                row[1] = put(written['text'])
            if row is not None and row[0] == name_at and key in named:
                row[0] = put(named[key])
        out.append((kind, [(i, v[0], v[1]) for i, v in sorted(table.items())]))
    return out


def our_phrases(code):
    """Our own wording for the short phrases: tools/eid_lang/<code>-phrases.json.

    The mod's Russian pack has three of them in English and the port adds a
    fourth of its own use (the luck line), so they are written here rather than
    shown in English inside a Russian description.
    """
    path = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'eid_lang',
                        code + '-phrases.json')
    if not os.path.exists(path):
        return {}
    import json
    return dict((int(k), v) for k, v in json.load(io.open(path, encoding='utf-8')).items())


def our_table(code, what):
    """One of our own files beside the mod's: tools/eid_lang/<code>-<what>.json."""
    path = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'eid_lang',
                        '%s-%s.json' % (code, what))
    if not os.path.exists(path):
        return {}
    import json
    return json.load(io.open(path, encoding='utf-8'))


def our_words(code):
    """Our own translations for one language: tools/eid_lang/<code>.json.

    The mod's Russian pack has 69 of the 498 conditional strings and the German
    128, and the mod does not fall back to English for them - so on those two
    languages almost none of this would ever be shown. These files fill the gap
    and sit outside the mod's own folder, so updating the mod does not lose
    them.
    """
    path = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'eid_lang', code + '.json')
    if not os.path.exists(path):
        return {}
    import json
    return json.load(io.open(path, encoding='utf-8'))


def written_value(value):
    """One localization value, in the form the port reads."""
    if isinstance(value, list):
        pieces = []
        for key, item in [(None, v) if not isinstance(v, tuple) else v for v in value]:
            if isinstance(item, (int, float)):
                pieces.append('n' + spell(item))
            elif isinstance(item, str):
                pieces.append('s' + item)
            else:
                return None                 # something we do not understand
        return 'T' + JOIN.join(pieces)
    if isinstance(value, str):
        return 'S' + value
    return None


def loc_tables_of(source):
    """{table name: {key: value}} for the tables the conditionals look in."""
    found = {}
    # the plain ones, written straight into the language
    for name in LOC_TABLES:
        body = table_body(source, 'EID.descriptions[languageCode].' + name)
        if body is None:
            continue
        rows, _ = lua_value('{' + body + '}', 0)
        found.setdefault(name, {})
        for key, value in rows:
            if key is not None:
                found[name][spell(key)] = value
    # and the ones written as a local and merged in afterwards
    for merge in re.finditer(r'EID:updateDescriptionsViaTable\(\s*(\w+)\s*,\s*'
                             r'EID\.descriptions\[languageCode\]\.(\w+)\s*\)', source):
        local, name = merge.group(1), merge.group(2)
        if name not in LOC_TABLES:
            continue
        body = table_body(source, 'local ' + local)
        if body is None:
            continue
        rows, _ = lua_value('{' + body + '}', 0)
        found.setdefault(name, {})
        for key, value in rows:
            if key is not None:
                found[name][spell(key)] = value
    return found


def red_to_of(source):
    """{heart type: [find, replace, ...]} from the mod's RedToX table."""
    body = table_body(source, 'EID.descriptions[languageCode].RedToX')
    if body is None:
        return {}
    found = {}
    for piece, start in braced_rows(body):
        key = re.search(r'\[\s*"Red to (\w+)"\s*\]\s*=\s*$', body[:start])
        if not key or key.group(1) not in HEARTS:
            continue
        found[HEARTS.index(key.group(1))] = lua_strings(piece)
    return found


def plural_of(source):
    """[(marker, singular, plural)] from the language's own PluralizeFunction.

    Every language writes the same shape - a list of ReplaceVariableStr calls
    that put one ending in when there is more than one of a thing and another
    when there is one - so the endings can be read out without running Lua.
    English also turns "Life" into "Lives", which is written here as a marker
    of its own that the port applies as a plain replacement.
    """
    at = source.find('EID.descriptions[languageCode].PluralizeFunction')
    if at < 0:
        return []
    body = source[at:at + 2000]
    found = []
    for m in re.finditer(r'ReplaceVariableStr\(text,\s*"(\w+)",\s*amount\s*>\s*1\s*and\s*'
                         r'"([^"]*)"\s*or\s*"([^"]*)"\)', body):
        found.append((m.group(1), m.group(3), m.group(2)))
    for m in re.finditer(r'text:gsub\("(\w+)",\s*"(\w+)"\)', body):
        found.append(('=' + m.group(1), m.group(1), m.group(2)))
    return found


def stat_name(which, said):
    """One stat name, with the arrow and the icon left to the caller.

    The mod writes the name around a {1} it fills in with the amount, and puts
    the arrow and the stat's icon before the whole thing. Three of its
    languages - Czech, Russian and Ukrainian - wrote the icon into the name
    instead and left out the {1}, so the mod takes its other path and prints
    the arrow and the icon a second time. Their own icon comes out here, and
    the line then reads the way it does everywhere else.
    """
    if '{1}' in said or which >= len(PRIZE_STAT_ICONS):
        return said
    said = said.replace('{{%s}}' % PRIZE_STAT_ICONS[which], ' ')
    said = said.replace('\u2191', ' ')
    return ' '.join(said.split())


def prize_words(code, source, english):
    """The stat names and pickup names Consolation Prize prints.

    Rows 0..5 are the stat names, which the mod keeps as VoidNames because Void
    prints the same list; rows 10..14 are the pickups it can offer instead.

    A language the mod left without one of the two tables gets the English one,
    which is what the mod itself does - and then our own <code>-prize.json is
    laid over those rows, so a Russian panel does not say "3 Coin". A table the
    mod did translate is never touched.
    """
    out = {}
    header = 'EID.descriptions[languageCode].VoidNames'
    own_stats = table_body(source, header)
    body = own_stats or table_body(english, header)
    if body:
        for i, said in enumerate(lua_strings(body)):
            out[i] = stat_name(i, said)
    # and the single rows the Repentance file sets on top, VoidNames[2] = "{1} Fire rate"
    esc = chr(92) * 2
    for m in re.finditer(r'EID\.descriptions\[languageCode\]\.VoidNames\[(\d+)\]\s*=\s*("(?:[^"'
                         + esc + ']|' + esc + '.)*")', source):
        row = int(m.group(1)) - 1
        said = lua_strings(m.group(2))
        if said and 0 <= row < len(PRIZE_STAT_ICONS):
            out[row] = stat_name(row, said[0])
    header = 'EID.descriptions[languageCode].PickupNames'
    own_picks = table_body(source, header)
    body = own_picks or table_body(english, header)
    if body:
        rows, _ = lua_value('{' + body + '}', 0)
        named = dict((spell(k), v) for k, v in rows if isinstance(v, str))
        for i, key in enumerate(PRIZE_PICKUPS):
            if key in named:
                out[10 + i] = named[key]
    for row, said in our_table(code, 'prize').items():
        row = int(row)
        if said and not (own_stats if row < 10 else own_picks):
            out[row] = said
    return out


def glitch_table(source):
    """GlitchedItemText as {key: string}; a positional entry takes Lua's next number."""
    body = table_body(source, 'EID.descriptions[languageCode].GlitchedItemText')
    if not body:
        return {}
    rows, _ = lua_value('{' + body + '}', 0)
    out = {}
    position = 1
    for key, value in rows:
        if key is None:
            key = position
            position += 1
        if isinstance(value, str):
            out[spell(key)] = value
    return out


def glitch_words(source, english_source):
    """[(row, name, text)] of GlitchedItemText for one language.

    A language without its own table is left out - text_of() falls back to the
    English row by row, which is what the mod's getDescriptionEntry does.
    """
    english = glitch_table(english_source)
    entities = sorted(k for k in english if '.' in k)
    own = glitch_table(source)
    out = []
    for key, said in own.items():
        if key.lstrip('-').isdigit() and 0 <= int(key) <= 16:
            out.append((int(key), '', said))
        elif key in GLITCH_ROWS:
            out.append((GLITCH_ROWS[key], '', said))
        elif key in entities:
            out.append((GLITCH_ENTITY_FIRST + entities.index(key), key, said))
    return sorted(out)


def phrases_of(source):
    """{index: phrase} for the plain string assignments the port asks for."""
    found = {}
    for n, key in enumerate(PHRASES):
        esc = chr(92) * 2                   # a backslash, as a regular expression writes one
        m = re.search(r'EID\.descriptions\[languageCode\]\.' + key
                      + r'\s*=\s*("(?:[^"' + esc + ']|' + esc + '.)*")', source)
        if not m:
            continue
        value = lua_strings(m.group(1))
        if value:
            found[n] = value[0]
    return found


def source_of(mod_dir, code):
    """The language's files from every folder that has one, run together."""
    parts = []
    for folder in FOLDERS:
        path = os.path.join(mod_dir, 'descriptions', folder, code + '.lua')
        if os.path.exists(path):
            parts.append(strip_comments(io.open(path, encoding='utf-8').read()))
    return '\n'.join(parts)


# ---------------------------------------------------------------- the binary

def read_bin(path):
    d = io.open(path, 'rb').read()
    magic, version, nlangs, strings_at = struct.unpack_from('<4sIII', d, 0)
    assert magic == b'EIDB', 'not an eid.bin'
    langs = []
    at = 16
    for _ in range(nlangs):
        code = d[at:at + 8].rstrip(b'\0').decode('ascii')
        off = struct.unpack_from('<I', d, at + 8)[0]
        langs.append([code, off])
        at += 12
    blocks = []
    for code, off in langs:
        count = struct.unpack_from('<I', d, off)[0]
        sections = []
        for s in range(count):
            kind, n, where = struct.unpack_from('<III', d, off + 4 + s * 12)
            rows = [struct.unpack_from('<III', d, where + r * 12) for r in range(n)]
            sections.append((kind, rows))
        blocks.append((code, sections))
    return blocks, bytearray(d[strings_at:]), version


def write_bin(path, blocks, pool, version):
    cursor = 16 + len(blocks) * 12
    raw_blocks = []
    offsets = []
    for _, sections in blocks:
        offsets.append(cursor)
        block = bytearray()
        block += struct.pack('<I', len(sections))
        entries_at = cursor + 4 + len(sections) * 12
        rows_bytes = bytearray()
        for kind, rows in sections:
            block += struct.pack('<III', kind, len(rows), entries_at + len(rows_bytes))
            for ident, name, text in rows:
                rows_bytes += struct.pack('<III', ident, name, text)
        block += rows_bytes
        raw_blocks.append(bytes(block))
        cursor += len(block)
    out = bytearray()
    out += struct.pack('<4sIII', b'EIDB', version, len(blocks), cursor)
    for (code, _), off in zip(blocks, offsets):
        out += code.encode('ascii')[:8].ljust(8, b'\0')
        out += struct.pack('<I', off)
    for b in raw_blocks:
        out += b
    out += pool
    io.open(path, 'wb').write(bytes(out))
    return len(out)


def main(mod_dir, target):
    blocks, pool, version = read_bin(target)
    # strings already in the pool can be pointed at again; index what is there
    known = {}
    at = 0
    while at < len(pool):
        end = pool.find(b'\0', at)
        if end < 0:
            break
        known[bytes(pool[at:end])] = at
        at = end + 1

    def put(s):
        b = s.encode('utf-8')
        if b in known:
            return known[b]
        off = len(pool)
        pool.extend(b)
        pool.append(0)
        known[b] = off
        return off

    empty = put('')
    english = phrases_of(source_of(mod_dir, FALLBACK))
    english_loc = loc_tables_of(source_of(mod_dir, FALLBACK))
    english_source = source_of(mod_dir, FALLBACK)
    names = game_names()
    english_rows = {}
    for name, sections in blocks:
        if name == FALLBACK:
            english_rows = dict((kind, dict((i, (n, x)) for i, n, x in rows))
                                for kind, rows in sections)
    base_golden = golden_data_of(
        strip_comments(io.open(os.path.join(mod_dir, 'features', 'eid_data.lua'),
                               encoding='utf-8').read()),
        'EID.GoldenTrinketData')
    added = 0
    for i, (code, sections) in enumerate(blocks):
        source = source_of(mod_dir, code)
        if not source:
            continue
        said = phrases_of(source)
        for n in range(len(PHRASES)):           # the mod falls back to English too
            if n not in said and n in english:
                said[n] = english[n]
        said.update(our_phrases(code))          # and ours over both
        rows = [(pid, put(name), put(text)) for pid, name, text in birthright_of(source)]
        phrases = [(n, empty, put(s)) for n, s in sorted(said.items())]
        golden = [(t, empty, put(JOIN.join(lines)))
                  for t, lines in sorted(golden_of(source).items())]
        # the mod lets a language replace whole entries of the shared table
        data = dict(base_golden)
        data.update(golden_data_of(source, 'EID.descriptions[languageCode].goldenTrinketData'))
        data = [(t, empty, put(written)) for t, written in sorted(data.items())]
        tables = loc_tables_of(source)
        # what has no words in it is the same in every language
        for name, table in english_loc.items():
            for key, value in table.items():
                if key not in tables.setdefault(name, {}) and wordless(value):
                    tables[name][key] = value
        # and our own translations on top of the mod's
        for whole, value in our_words(code).items():
            name, key = whole.split('|', 1)
            tables.setdefault(name, {})[key] = value
        conds = {}
        for name, table in sorted(tables.items()):
            for key, value in table.items():
                written = written_value(value)
                if written is None:
                    continue
                whole = name + '|' + key
                h = key_hash(whole)
                if h in conds and conds[h][0] != whole:
                    raise SystemExit('two keys hash the same: %r and %r' % (conds[h][0], whole))
                conds[h] = (whole, written)
        conds = [(h, empty, put(v[1])) for h, v in sorted(conds.items())]
        hearts = red_to_of(source)
        # ours over the mod's: the mod's own table is written in English, and a
        # find written in English never matches a Russian description
        for kind, lines in our_table(code, 'redto').items():
            hearts[int(kind)] = lines
        red_to = [(kind, empty, put(JOIN.join(lines))) for kind, lines in sorted(hearts.items())]
        prize = [(i, empty, put(said))
                 for i, said in sorted(prize_words(code, source, english_source).items())]
        plural = plural_of(source)
        mine = our_table(code, 'plural')
        if mine:
            plural = [(one['name'], one['one'], one['many']) for one in mine]
        plural = [(0, empty, put(JOIN.join(JOIN.join(one) for one in plural)))] if plural else []
        glitch = [(row, put(name) if name else empty, put(said))
                  for row, name, said in glitch_words(source, english_source)]
        sections = [s for s in sections if s[0] not in MINE]
        for kind, table in ((KIND_BIRTHRIGHT, rows), (KIND_PHRASE, phrases),
                            (KIND_GOLDEN, golden), (KIND_GOLDEN_DATA, data),
                            (KIND_CONDITION, conds), (KIND_RED_TO, red_to),
                            (KIND_PLURAL, plural), (KIND_PRIZE, prize),
                            (KIND_GLITCH, glitch)):
            if table:
                sections.append((kind, table))
        blocks[i] = (code, with_ours(code, sections, english_rows, names, put))
        added += 1
        print('%-6s birthright %3d, phrases %d, golden %2d of %d, conditions %4d'
              % (code, len(rows), len(phrases), len(golden), len(data), len(conds)))
    size = write_bin(target, blocks, pool, version)
    print('%s: %d languages given the new sections, %.2f MB' % (target, added, size / 1048576.0))


if __name__ == '__main__':
    mod = sys.argv[1] if len(sys.argv) > 1 else 'eid-upstream'
    dst = sys.argv[2] if len(sys.argv) > 2 else os.path.join(ROOT, 'port', 'app', 'src', 'main', 'assets', 'eid.bin')
    main(mod, dst)
