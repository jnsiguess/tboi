# -*- coding: utf-8 -*-
"""Book of Virtues and Abyss: the blocks they add, built ahead of time.

Both items describe what they would make out of the item on the floor - a wisp
with such health and such damage, a locust of such a size and speed - and the
mod builds those blocks at runtime out of numbers from the game's XML and words
from the language pack (eid_modifiers.lua, BookOfVirtuesWispDescriptionBuilder
and AbyssCallback).

Nothing in that depends on the run except ONE number: a wisp's damage grows
with the floor. So the blocks are built here instead, once per language, with
the damage left as {1}; the port fills it in from three numbers per item and
the floor it already reads.

    py -3 tools/eid_wisps.py [mod dir] [eid.bin] [eid_wisps.h]

Sections written into eid.bin:

   15  wisps    id = collectible, text = the block, {1} for the damage
   16  locusts  id = collectible, text = the block; id 0 is the default one,
                which is what Abyss says about an item its XML does not name
"""
import io
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from eid_more import (strip_comments, table_body, lua_value, read_bin, write_bin,
                      source_of, FALLBACK, JOIN, our_table, plural_of)

MOD = os.path.join(os.path.dirname(HERE), 'eid-mod')
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
BIN = os.path.join(ROOT, 'port', 'app', 'src', 'main', 'assets', 'eid.bin')
OUT = os.path.join(ROOT, 'port', 'app', 'src', 'main', 'cpp', 'eid_wisps.h')

KIND_WISP = 15
KIND_LOCUST = 16

RING = {-1: '{{StationaryWisp}}', 0: '{{InnerWisp}}', 1: '{{MiddleWisp}}', 2: '{{OuterWisp}}'}
WISP_COLOUR = '{{ColorPastelBlue}}'
LOCUST_COLOUR = '{{ColorRed}}'
LOCUST_DEFAULT = [1, 1, 1, [-1], [-1], [-1], [-1], [-1], [-1], 1, 1, 1, 1, 1]


def plain(value):
    """A parsed Lua value with the keys thrown away; a bare word is nothing."""
    if isinstance(value, list):
        return [plain(v) for _, v in value]
    if isinstance(value, tuple):
        # the parser hands back bare words as they stand: true, false, nil
        if value[1] == 'true':
            return True
        if value[1] == 'false':
            return False
        return None
    return value


def numbered(source, header):
    """{number: value} of a table whose keys are numbers."""
    body = table_body(source, header)
    if body is None:
        return {}
    rows, _ = lua_value('{' + body + '}', 0)
    out = {}
    for key, value in rows:
        if isinstance(key, (int, float)):
            out[int(key)] = plain(value)
    return out


def named(source, header):
    """{name: value} of a table whose keys are words."""
    body = table_body(source, header)
    if body is None:
        return {}
    rows, _ = lua_value('{' + body + '}', 0)
    return dict((k, plain(v)) for k, v in rows if isinstance(k, str))


def trimmed(value, digits):
    """A number the way Lua's string.format leaves it, without trailing zeros."""
    text = ('%.*f' % (digits, value)).rstrip('0').rstrip('.')
    return text if text else '0'


def fill(text, name, value):
    return text.replace('{%s}' % name, str(value))


def one_variable(text, value):
    return text.replace('{1}', str(value))


class Words(object):
    """One language's words, falling back to English the way the mod does."""

    def __init__(self, mod_dir, code):
        mine = source_of(mod_dir, code)
        theirs = source_of(mod_dir, FALLBACK)
        self.wisp = named(mine, 'EID.descriptions[languageCode].BookOfVirtuesWispTexts') \
            or named(theirs, 'EID.descriptions[languageCode].BookOfVirtuesWispTexts')
        self.abyss = named(mine, 'EID.descriptions[languageCode].AbyssTexts') \
            or named(theirs, 'EID.descriptions[languageCode].AbyssTexts')
        self.locust_effects = numbered(mine, 'EID.descriptions[languageCode].AbyssLocustEffects') \
            or numbered(theirs, 'EID.descriptions[languageCode].AbyssLocustEffects')
        self.tear_flags = numbered(mine, 'EID.descriptions[languageCode].TearFlagNames') \
            or numbered(theirs, 'EID.descriptions[languageCode].TearFlagNames')
        # and our own words over the mod's, for a language it never wrote them in
        ours = our_table(code, 'wisptexts')
        self.wisp.update(ours.get('wisp', {}))
        self.abyss.update(ours.get('abyss', {}))
        self.extra_wisp = numbered(mine, 'EID.descriptions[languageCode].bookOfVirtuesWisps')
        self.extra_abyss = numbered(mine, 'EID.descriptions[languageCode].abyssSynergies')
        self.plural = our_table(code, 'plural')
        if self.plural:
            self.plural = [(o['name'], o['one'], o['many']) for o in self.plural]
        else:
            self.plural = plural_of(mine)

    def pluralize(self, text, amount):
        for name, one, many in self.plural:
            ending = many if amount > 1 else one
            if name.startswith('='):
                if amount > 1:
                    text = text.replace(name[1:], many)
            else:
                text = text.replace('{%s}' % name, ending)
        return text

    def chance(self, which, first, second, third):
        """The mod's GetChanceString, one for one."""
        chance = None
        if which == 3 and third is not None and third < 1:
            chance = third
        elif which == 2 and second is not None and second < 1:
            chance = second
        if chance is None:
            chance = first if first is not None else 1
        if chance == 1:
            return ''
        said = self.abyss.get('Chance', '')
        return one_variable(said, trimmed(chance * 100, 2))

    def flags(self, which, table, colour, flags, first, second, third=None):
        """The mod's GetFlagString - and, as in Repentance, only the first flag."""
        text = ''
        for flag in flags or []:
            said = table.get(int(flag))
            if said:
                text += '#' + colour + said + self.chance(which, first, second, third)
            return text                     # the engine only triggers the first
        return text


def wisp_block(words, ident, data, wisp_data):
    """What Book of Virtues says about this item, with {1} left for the damage."""
    extra = words.extra_wisp.get(ident)
    head = '#{{VirtuesCollectible%d}} ' % ident
    if data is None:
        return (head + WISP_COLOUR + extra) if extra else None
    hp, layer, _dmg, _stage, mult2, shot_speed, fire_delay, proc, can_shoot, amount = data[:10]
    tears, tears2 = data[10], data[11]
    can_shoot = bool(can_shoot) and fire_delay != -1
    if ident in wisp_data['NoWisp']:
        text = head + WISP_COLOUR + words.wisp.get('NoWisps', '')
    else:
        stat = words.wisp.get('StatDescription', '')
        stat = fill(stat, 'ringIcon', WISP_COLOUR + RING.get(int(layer), RING[1]))
        stat = fill(stat, 'amount', amount if amount != 0 else '')
        stat = fill(stat, 'health', hp)
        stat = fill(stat, 'damage', '{1}' if can_shoot else '-')
        text = head + WISP_COLOUR + stat
        if ident in wisp_data['SingleRoom']:
            text += '#{{Warning}} ' + WISP_COLOUR + words.wisp.get('SingleRoom', '')
        if can_shoot:
            if shot_speed != 1:
                said = one_variable(words.wisp.get('Shotspeed', ''), trimmed(shot_speed * 100, 2))
                text += '#{{Shotspeed}} ' + WISP_COLOUR + said
            if mult2 != 1:
                said = one_variable(words.wisp.get('Damage', ''), trimmed(mult2 * 100, 2))
                text += '#{{Damage}} ' + WISP_COLOUR + said + words.chance(2, proc, proc, None)
            colour = '{{Shotspeed}} ' + WISP_COLOUR
            text += words.flags(1, words.tear_flags, colour, tears, proc, proc)
            text += words.flags(2, words.tear_flags, colour, tears2, proc, proc)
    if extra:
        for line in extra.split('#'):
            if line:
                text += '#' + WISP_COLOUR + line
    return words.pluralize(text, amount)


def locust_block(words, ident, data):
    """What Abyss says about this item."""
    extra = words.extra_abyss.get(ident)
    if extra:
        return '#{{Collectible706}} ' + LOCUST_COLOUR + extra
    amount, scale, speed = data[0], data[1], data[2]
    flags1, flags2, flags3 = data[3], data[4], data[5]
    tears1, tears2, tears3 = data[6], data[7], data[8]
    proc1, proc2, proc3 = data[9], data[10], data[11]
    mult1, mult2 = data[12], data[13]
    damage = one_variable(words.abyss.get('DamageMult', ''), trimmed(1 * mult1, 2))
    size = ''
    if scale < 1:
        size = words.abyss.get('SizeSmall', '')
    elif scale > 1:
        size = words.abyss.get('SizeBig', '')
    quick = ''
    if speed < 1:
        quick = words.abyss.get('SpeedSlow', '')
    elif speed >= 6:
        quick = words.abyss.get('SpeedDash', '')
    elif speed > 1:
        quick = words.abyss.get('SpeedFast', '')
    info = (words.abyss.get('InfoTextPlural') if amount > 1 else None) or words.abyss.get('InfoText', '')
    text = '#{{Collectible706}} ' + LOCUST_COLOUR + info
    text = fill(fill(fill(fill(text, 'amount', amount), 'size', size), 'speed', quick), 'dmg', damage)
    if mult2 != 1:
        said = one_variable(words.abyss.get('DamageMult', ''), mult2)
        text += '#' + LOCUST_COLOUR + said + words.chance(1, proc1, proc2, proc3)
    for which, group in ((1, flags1), (2, flags2), (3, flags3)):
        text += words.flags(which, words.locust_effects, LOCUST_COLOUR, group, proc1, proc2, proc3)
    for which, group in ((1, tears1), (2, tears2), (3, tears3)):
        text += words.flags(which, words.tear_flags, LOCUST_COLOUR, group, proc1, proc2, proc3)
    return words.pluralize(text, amount)


def main(mod_dir, binary, header):
    data = strip_comments(io.open(os.path.join(mod_dir, 'features', 'eid_data.lua'),
                                  encoding='utf-8').read())
    xml = strip_comments(io.open(os.path.join(mod_dir, 'features', 'eid_xmldata.lua'),
                                 encoding='utf-8').read())
    wisps = numbered(xml, 'EID.XMLWisps')
    locusts = numbered(xml, 'EID.XMLLocusts')
    wisp_data = named(data, 'EID.WispData')
    wisp_data = {'NoWisp': set(int(k) for k in numbered_keys(data, 'NoWisp')),
                 'SingleRoom': set(int(k) for k in numbered_keys(data, 'SingleRoom'))}

    blocks, pool, version = read_bin(binary)
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
    for i, (code, sections) in enumerate(blocks):
        source = source_of(mod_dir, code)
        if not source:
            continue
        words = Words(mod_dir, code)
        wisp_rows = []
        for ident in sorted(set(wisps) | set(words.extra_wisp)):
            block = wisp_block(words, ident, wisps.get(ident), wisp_data)
            if block:
                wisp_rows.append((ident, empty, put(block)))
        locust_rows = [(0, empty, put(locust_block(words, 0, LOCUST_DEFAULT)))]
        for ident in sorted(set(locusts) | set(words.extra_abyss)):
            block = locust_block(words, ident, locusts.get(ident, LOCUST_DEFAULT))
            if block:
                locust_rows.append((ident, empty, put(block)))
        sections = [s for s in sections if s[0] not in (KIND_WISP, KIND_LOCUST)]
        sections.append((KIND_WISP, wisp_rows))
        sections.append((KIND_LOCUST, locust_rows))
        blocks[i] = (code, sections)
        print('%-6s wisps %3d, locusts %3d' % (code, len(wisp_rows), len(locust_rows)))
    size = write_bin(binary, blocks, pool, version)
    print('%s: %.2f MB' % (binary, size / 1048576.0))

    # and the three numbers a wisp's damage needs, which depend on the floor
    text = io.StringIO()
    text.write('/*\n * A wisp\'s damage: (damage + stage damage * (floor - 1)) * (30 / fire\n')
    text.write(' * delay), which is the mod\'s own sum. Written by tools/eid_wisps.py out\n')
    text.write(' * of the mod\'s XMLWisps - do not edit here.\n */\n')
    text.write('typedef struct { short id; float damage, stage, delay; } eid_wisp_t;\n')
    rows = [(i, d) for i, d in sorted(wisps.items()) if d[8] and d[6] != -1]
    text.write('#define EID_WISPS %d\n' % len(rows))
    text.write('static const eid_wisp_t EID_WISP[EID_WISPS] = {\n')
    def number(value):
        said = '%g' % value
        return said + ('f' if ('.' in said or 'e' in said) else '.0f')

    for ident, d in rows:
        text.write('    { %d, %s, %s, %s },\n'
                   % (ident, number(d[2]), number(d[3]), number(d[6])))
    text.write('};\n')
    io.open(header, 'w', encoding='utf-8', newline='\n').write(text.getvalue())
    print('%s: %d wisps that shoot' % (header, len(rows)))


def numbered_keys(source, inner):
    """The keys of one of WispData's inner tables."""
    body = table_body(source, 'EID.WispData')
    if body is None:
        return []
    at = body.find(inner)
    if at < 0:
        return []
    piece = table_body(body[at:], inner)
    return [int(k) for k in re.findall(r'\[\s*(\d+)\s*\]\s*=\s*true', piece or '')]


if __name__ == '__main__':
    main(sys.argv[1] if len(sys.argv) > 1 else MOD,
         sys.argv[2] if len(sys.argv) > 2 else BIN,
         sys.argv[3] if len(sys.argv) > 3 else OUT)
