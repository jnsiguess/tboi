# -*- coding: utf-8 -*-
"""The mod's conditional descriptions, turned into a table the port can walk.

EID does not only describe an item: it describes what the item does FOR THIS
run. Brimstone in the room and a pedestal that Brimstone overrides, Keeper
looking at a heart item, Tarot Cloth in the bag and a card on the floor - each
adds or rewrites a line. The mod registers a couple of hundred such conditions
in features/eid_conditionals.lua and evaluates them in
features/eid_conditionals_funcs.lua (applyConditionals).

This reads those registrations - it does not run Lua - and writes them out as a
C table, one row per condition. The words the rows point at are not here: they
live in every language of the mod and go into eid.bin through
tools/eid_more.py, section 12.

    py -3 tools/eid_rules.py <mod dir> [eid_rules.h]

Only conditions the port can actually answer are carried over:

  * the player holds a collectible, trinket, card or pill
  * the player IS a character (and, for some, a tainted one)
  * what the RUN is: Greed Mode, hard mode, which floor
  * what the shown item is: an active with such a charge, and how much
  * the ones the mod itself answers with a constant - without REPENTOGON its
    achievement checks always say yes, and a second player never exists here

Everything else - what familiars are in the room, what the player is holding as
an active, whether anyone has a golden bomb - is left out and listed at the end
of the run, so what is missing is known rather than quietly dropped.
"""
import io
import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from eid_more import strip_comments, table_body, lua_value, skip_space, spell

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT = os.path.join(ROOT, 'port', 'app', 'src', 'main', 'cpp', 'eid_rules.h')

# the pickup variants the port shows, and its own numbering for them
VARIANTS = {'100': 1, '350': 2, '300': 3, '70': 4}
KIND_NAMES = {1: 'collectible', 2: 'trinket', 3: 'card', 4: 'pill'}

# the icon marker for one item, by variant
MARKERS = {'100': 'Collectible', '350': 'Trinket', '300': 'Card', '70': 'Pill'}

VAR_NONE, VAR_ITEM, VAR_PLAYER, VAR_TEXT = 0, 1, 2, 3

# the conditions that ask about the run itself. The mod writes each as a
# function of its own; the port answers them from the game's own memory
# (difficulty at Game + 0x24f9d8, the floor at Game + 0) or from a constant.
STATE = {
    'EID.IsGreedMode':          ('GREED', 0, 0),
    'EID.IsHardMode':           ('HARD', 0, 0),
    'EID.InStageVoid':          ('STAGE_EQ', 12, 0),
    'EID.InStageNoTreasureRoom': ('STAGE_GT', 6, 0),
    'EID.InStageTheShop':       ('SHOP', 0, 0),
    'EID.PlayersHaveRestock':   ('RESTOCK', 0, 0),
    'EID.CheckForTaintedPlayer': ('TAINTED', 0, 0),
    'EID.CheckForNoRedHealthPlayer': ('NO_RED', 0, 0),
}

# and the ones that walk a list of items and hand back the first one the player
# has. The mod puts that answer where the modifier text would be (useResult),
# so the line it prints is the one written for THAT item.
LISTS = {
    'EID.CheckActivesForCarBattery':   'CarBatteryPedestalWhitelist',
    'EID.CheckActivesForNoCarBattery': 'CarBatteryNoSynergy',
    'EID.CheckFamiliarsForBFFS':       'BFFSPedestalWhitelist',
    'EID.CheckFamiliarsForNoBFFS':     'BFFSNoSynergy',
    'EID.CheckFamiliarsForHiveMind':   'BFFSPedestalWhitelist&HiveMindFamiliars',
}

# Schoolbag, or a character who carries a pocket active
MULTI = {
    'EID.CheckForMultipleActives': 0,
    'EID.CheckForMultipleChargeableActives': 1,
}

# CheckPlayersForActiveChargeType(maxCharge, chargeType, checkPockets)
HELD = re.compile(r'EID:CheckPlayersForActiveChargeType\(\s*(nil|\d+)\s*,\s*(nil|\d+)\s*'
                  r'(?:,\s*(true|false)\s*)?\)')

# and the inline ones, matched on what they call. CheckActiveChargeType asks
# about the item on the PEDESTAL - its charge is in the game's own items.xml,
# so no run state is needed; CheckPlayersForActiveChargeType asks about what
# the player is holding, which is not read yet.
INLINE = re.compile(r'EID:CheckActiveChargeType\(\s*descObj\.ObjSubType\s*,\s*'
                    r'(nil|\d+)\s*,\s*(nil|\d+)\s*,\s*(\d+)\s*\)')
ACHIEVEMENT = re.compile(r'^(not\s+)?EID:HaveNotUnlockedAchievement')


class Skip(Exception):
    pass


# ------------------------------------------------------------ reading the Lua

def keep_block(condition):
    """Whether an `if ... then` block belongs in this port."""
    condition = condition.strip()
    if condition == 'EID.isRepentance':
        return True
    if condition == 'not EID.isRepentance':
        return False
    return False                    # Repentance+, REPENTOGON, anything unknown


def flatten(source, tables):
    """The file with its dead branches removed and its loops unrolled.

    The mod writes one file for every version of the game it supports, so half
    of it is inside `if EID.isRepentance then`, `if not EID.isRepentance then`,
    `if REPENTOGON then` and their `else` branches. This keeps the branches
    that are true for the game this port runs and drops the rest.
    """
    out = []
    stack = []                              # one entry per open block

    def live():
        return all(s['active'] for s in stack)

    for line in source.split('\n'):
        bare = line.strip()
        loop = re.match(r'for\s+(\w+)\s*,\s*\w+\s+in\s+pairs\(EID\.(\w+)\)\s+do\s+(.*?)\s*end\s*$', bare)
        if loop:
            if live():
                for key in tables.get(loop.group(2), {}):
                    out.append(re.sub(r'\b%s\b' % loop.group(1), spell(key), loop.group(3)))
            continue
        one = re.match(r'if\s+(.*?)\s+then\s+(.+?)\s+end\s*$', bare)
        if one:
            if live() and keep_block(one.group(1)):
                out.append(one.group(2))
            continue
        opened = re.match(r'if\s+(.*?)\s+then\s*$', bare)
        if opened:
            taken = keep_block(opened.group(1))
            stack.append({'taken': taken, 'active': taken})
            continue
        if re.match(r'for\s+.*\s+do\s*$', bare):
            stack.append({'taken': False, 'active': False})   # unrolled by hand above
            continue
        closing = re.match(r'else\s+(.+?)\s+end\s*$', bare)
        if closing and stack:
            block = stack.pop()
            if live() and not block['taken']:
                out.append(closing.group(1))
            continue
        again = re.match(r'elseif\s+(.*?)\s+then\s*$', bare)
        if again and stack:
            taken = (not stack[-1]['taken']) and keep_block(again.group(1))
            stack[-1] = {'taken': stack[-1]['taken'] or taken, 'active': taken}
            continue
        if bare == 'else' and stack:
            stack[-1] = {'taken': True, 'active': not stack[-1]['taken']}
            continue
        if bare == 'end' or bare == 'end)':
            if stack:
                stack.pop()
            continue
        if live():
            out.append(line)
    return '\n'.join(out)


def calls_in(source, name):
    """[(arguments as text, where)] of every `EID:<name>(...)` in the source."""
    found = []
    for m in re.finditer(r'EID:' + name + r'\s*\(', source):
        depth = 1
        i = m.end()
        while i < len(source) and depth:
            c = source[i]
            if c in '"\'':
                quote = c
                i += 1
                while i < len(source) and source[i] != quote:
                    i += 2 if source[i] == chr(92) else 1
            elif c == '(':
                depth += 1
            elif c == ')':
                depth -= 1
            i += 1
        found.append(source[m.end():i - 1])
    return found


def split_args(text):
    """The top-level arguments of a call."""
    parts = []
    depth = 0
    at = 0
    i = 0
    while i < len(text):
        c = text[i]
        if c in '"\'':
            quote = c
            i += 1
            while i < len(text) and text[i] != quote:
                i += 2 if text[i] == chr(92) else 1
        elif c in '({[':
            depth += 1
        elif c in ')}]':
            depth -= 1
        elif c == ',' and depth == 0:
            parts.append(text[at:i])
            at = i + 1
        i += 1
    parts.append(text[at:])
    return [p.strip() for p in parts]


def value_of(text, tables, locals_):
    """One argument, as a Python value. ('lua', name) for anything unresolved."""
    text = text.strip()
    if not text or text == 'nil':
        return None
    if text == 'true':
        return True
    if text == 'false':
        return False
    if text in locals_:
        return locals_[text]
    if '..' in text and not text.startswith('{'):
        # "5.350." .. 45 - the unrolled loops build their keys this way
        return ''.join(spell(value_of(piece, tables, locals_))
                       for piece in text.split('..'))
    named = re.match(r'^EID\.(\w+)\["(\w+)"\]$', text)
    if named and named.group(1) in tables:
        return list(tables[named.group(1)].get(named.group(2), []))
    named = re.match(r'^EID\.(\w+)$', text)
    if named and named.group(1) in tables:
        return list(tables[named.group(1)])
    value, _ = lua_value(text, 0)
    if isinstance(value, list):             # a table literal, keyed or not
        if all(k is None for k, _ in value):
            return [v for _, v in value]
        return dict((k, v) for k, v in value)
    return value


def data_tables(mod_dir):
    """The tables from eid_data.lua that the conditionals refer to by name."""
    source = strip_comments(io.open(os.path.join(mod_dir, 'features', 'eid_data.lua'),
                                    encoding='utf-8').read())
    wanted = ('SpecialHeartPlayers', 'BloodUpData', 'BlackFeatherItems',
              'BlackFeatherTrinkets', 'TaintedToRegularID',
              'HealthUpData', 'HealingItemData',
              'CarBatteryPedestalWhitelist', 'CarBatteryNoSynergy',
              'BFFSPedestalWhitelist', 'BFFSNoSynergy', 'HiveMindFamiliars',
              'PocketActivePlayerIDs')
    out = {}
    for name in wanted:
        body = table_body(source, 'EID.' + name)
        if body is None:
            continue
        rows, _ = lua_value('{' + body + '}', 0)
        if all(k is None for k, _ in rows):
            out[name] = [v for _, v in rows]
        else:
            out[name] = dict((spell(k), v) for k, v in rows)
        # and the entries the mod replaces further down - SpecialHeartPlayers is
        # written once for every version of the game and then overridden for
        # Repentance, and reading only the first one loses half the characters
        for over in re.finditer(r'EID\.' + name + r'\[\s*"(\w+)"\s*\]\s*=\s*\{', source):
            piece = table_body(source[over.start():], 'EID.%s["%s"]' % (name, over.group(1)))
            if piece is None:
                continue
            more, _ = lua_value('{' + piece + '}', 0)
            if isinstance(out.get(name), dict):
                out[name][over.group(1)] = [v for _, v in more]
    # SpecialHeartPlayers holds lists of player types, one per heart colour
    for name, table in list(out.items()):
        if isinstance(table, dict):
            for key, value in list(table.items()):
                if isinstance(value, list) and all(isinstance(v, tuple) for v in value):
                    table[key] = [v for _, v in value]
    return out


# ------------------------------------------------------------- one condition

def as_list(value):
    if value is None:
        return []
    return value if isinstance(value, list) else [value]


def target_of(ident):
    """(kind, id) for one of the mod's item keys; id -1 means the whole kind."""
    text = spell(ident)
    if re.match(r'^\d+$', text):
        return VARIANTS['100'], int(text)   # a bare number is a collectible
    parts = text.split('.')
    if len(parts) < 2 or parts[0] != '5' or parts[1] not in VARIANTS:
        raise Skip('target ' + text)
    kind = VARIANTS[parts[1]]
    return kind, (int(parts[2]) if len(parts) > 2 else -1)


def marker_of(ident):
    """The icon marker for an item key, ready to print."""
    text = spell(ident)
    if re.match(r'^\d+$', text):
        return '{{Collectible%s}}' % text
    parts = text.split('.')
    if len(parts) == 3 and parts[1] in MARKERS:
        return '{{%s%s}}' % (MARKERS[parts[1]], parts[2])
    return None


def item_number(ident):
    """The collectible number of an item key, or None if it is not one."""
    text = spell(ident)
    if re.match(r'^\d+$', text):
        return int(text)
    parts = text.split('.')
    if len(parts) == 3 and parts[1] == '100':
        return int(parts[2])
    return None


class Rules(object):
    def __init__(self):
        self.rows = []
        self.uniques = {}
        self.skipped = {}
        self.lists = {}

    def skip(self, why):
        self.skipped[why] = self.skipped.get(why, 0) + 1

    def add(self, ids, test, own_kind, value, tainted, modifier, extra, bullet, variable,
            arg1=0, arg2=0):
        extra = extra or {}
        for ident in as_list(ids):
            try:
                kind, number = target_of(ident)
            except Skip as why:
                self.skip(str(why))
                continue
            if extra.get('noTable'):
                # its words are a plain description entry, not one of the
                # tables tools/eid_more.py carries; one condition does this
                self.skip('words outside the tables (' + str(modifier) + ')')
                continue
            unique = extra.get('uniqueID')
            if unique is not None and unique not in self.uniques:
                self.uniques[unique] = len(self.uniques) + 1
            row = {
                'kind': kind, 'id': number,
                'test': test, 'own_kind': own_kind, 'value': value,
                'arg1': arg1, 'arg2': arg2,
                'tainted': 1 if tainted else 0,
                'layer': int(extra.get('layer') or 0),
                'check_layers': 1 if extra.get('checkLayers') else 0,
                'use_result': 1 if extra.get('useResult') else 0,
                'no_table': 1 if extra.get('noTable') else 0,
                'loc': extra.get('locTable') or 'ConditionalDescs',
                'modifier': modifier,
                'bullet': extra.get('bulletpoint') and '{{%s}}' % extra['bulletpoint'] or bullet,
                'colour': extra.get('lineColor'),
                'replace': extra.get('replaceColor'),
                'unique': self.uniques.get(unique, 0),
                'fallback': 0 if extra.get('noFallback', True) else 1,
            }
            row.update(self.variable(extra, variable, ident))
            self.rows.append(row)

    def variable(self, extra, variable, target):
        """What {1} in the text becomes."""
        if extra.get('usePedestalName'):
            try:
                kind, number = target_of(target)
            except Skip:
                kind, number = 0, -1
            if number >= 0:
                return {'var': VAR_ITEM, 'var_kind': kind, 'var_id': number, 'var_text': None}
        given = extra.get('variableText', variable)
        if given is None:
            return {'var': VAR_NONE, 'var_kind': 0, 'var_id': 0, 'var_text': None}
        if isinstance(given, (int, float)):
            return {'var': VAR_TEXT, 'var_kind': 0, 'var_id': 0, 'var_text': spell(given)}
        text = spell(given)
        name = re.match(r'^\{\{NameOnlyC(\d+)\}\}$', text)
        if name:
            return {'var': VAR_ITEM, 'var_kind': 1, 'var_id': int(name.group(1)), 'var_text': None}
        name = re.match(r'^\{\{NameOnly5\.(\d+)\.(\d+)\}\}$', text)
        if name and name.group(1) in VARIANTS:
            return {'var': VAR_ITEM, 'var_kind': VARIANTS[name.group(1)],
                    'var_id': int(name.group(2)), 'var_text': None}
        name = re.match(r'^\{\{NameOnlyI(\d+)\}\}$', text)
        if name:
            return {'var': VAR_PLAYER, 'var_kind': 0, 'var_id': int(name.group(1)), 'var_text': None}
        if text.startswith('{{NameOnly'):
            return {'var': VAR_NONE, 'var_kind': 0, 'var_id': 0, 'var_text': None}
        return {'var': VAR_TEXT, 'var_kind': 0, 'var_id': 0, 'var_text': text}

    # -------------------------------------------------- the mod's own helpers
    def item(self, ids, owned, modifier, extra):
        for one in as_list(owned):
            try:
                kind, number = target_of(one)
            except Skip as why:
                self.skip('holding ' + str(why))
                continue
            if number < 0:
                self.skip('holding any ' + KIND_NAMES.get(kind, '?'))
                continue
            variant = dict((v, k) for k, v in VARIANTS.items())[kind]
            self.add(ids, 'OWNS', kind, number, 0, modifier, extra,
                     marker_of(one), '{{NameOnly5.%s.%d}}' % (variant, number))

    def list_rule(self, ids, which, modifier, extra):
        """A condition that walks one of the mod's lists and returns what it found."""
        if which not in self.lists:
            self.lists[which] = len(self.lists)
        self.add(ids, 'LIST', 0, 0, 0, modifier, extra, None, None,
                 self.lists[which], 0)

    def state(self, ids, test, arg1, arg2, modifier, extra, owned=0):
        """A condition about the run itself, or about the item on the floor."""
        self.add(ids, test, 0, owned, 0, modifier, extra, None, None, arg1, arg2)

    def player(self, ids, chars, modifier, extra, tainted):
        for one in as_list(chars):
            if not isinstance(one, int):
                self.skip('character ' + spell(one))
                continue
            self.add(ids, 'CHAR', 0, one, tainted, modifier, extra,
                     '{{Player%d}}' % one, '{{NameOnlyI%d}}' % one)


def collect(source, tables):
    rules = Rules()
    locals_ = {}
    for m in re.finditer(r'^local\s+(\w+)\s*=\s*(.+)$', source, re.M):
        name, text = m.group(1), m.group(2).strip()
        joined = re.match(r'EID:ConcatTables\((.*)\)\s*$', text)
        if joined:
            whole = []
            for piece in split_args(joined.group(1)):
                whole += as_list(value_of(piece, tables, locals_))
            locals_[name] = whole
        else:
            locals_[name] = value_of(text, tables, locals_)

    def args(text, count):
        parts = split_args(text)
        parts += [''] * (count - len(parts))
        return [value_of(p, tables, locals_) for p in parts[:count]]

    def text_of(value):
        if isinstance(value, tuple) or value is None:
            return None                     # a function, or nothing
        return spell(value)

    for call in calls_in(source, 'AddItemConditional'):
        ids, owned, modifier, extra, _ = args(call, 5)
        rules.item(ids, owned, text_of(modifier), extra)

    for call in calls_in(source, 'AddSynergyConditional'):
        ids, owned, first, second, extra1, extra2, _ = args(call, 7)
        rules.item(ids, owned, text_of(first), extra1)
        rules.item(owned, ids, text_of(second if second is not None else first),
                   extra2 if extra2 is not None else extra1)

    for call in calls_in(source, 'AddOneSidedSynergyConditional'):
        ids, owned, modifier, extra, _ = args(call, 5)
        theirs = dict(extra or {})
        theirs['usePedestalName'] = True
        rules.item(ids, owned, text_of(modifier), theirs)
        rules.item(owned, ids, text_of(modifier), extra)

    for call in calls_in(source, 'AddSelfConditional'):
        owned, modifier, extra, diplopia = args(call, 4)
        for one in as_list(owned):
            twins = [one] if diplopia is False else [one, 347, 485]
            rules.item(one, twins, text_of(modifier), extra)

    for name in ('AddPlayerConditional', 'AddClosestPlayerConditional'):
        # with one player in the room the closest player IS the player, and the
        # mod's ClosestCharCheck then simply hands back the modifier text
        for call in calls_in(source, name):
            ids, chars, modifier, extra, tainted = args(call, 5)
            rules.player(ids, chars, text_of(modifier), extra, tainted is not False)

    for call in calls_in(source, 'AddConditional'):
        parts = split_args(call)
        parts += [''] * (4 - len(parts))
        ids = value_of(parts[0], tables, locals_)
        func = parts[1].strip()
        modifier, extra = [value_of(p, tables, locals_) for p in parts[2:4]]
        modifier = text_of(modifier)
        named = re.match(r'^(EID\.\w+)$', func)
        if named and named.group(1) in STATE:
            test, arg1, arg2 = STATE[named.group(1)]
            rules.state(ids, test, arg1, arg2, modifier, extra)
            continue
        if named and named.group(1) in LISTS:
            rules.list_rule(ids, LISTS[named.group(1)], modifier, extra)
            continue
        if named and named.group(1) in MULTI:
            rules.state(ids, 'MULTI_ACTIVE', MULTI[named.group(1)], 0, modifier, extra)
            continue
        body = re.match(r'^function\s*\([^)]*\)\s*return\s+(.*?)\s*end$', func, re.S)
        inner = body.group(1) if body else ''
        held = HELD.search(inner)
        if held:
            most = -1 if held.group(1) == 'nil' else int(held.group(1))
            kind = -1 if held.group(2) == 'nil' else int(held.group(2))
            pockets = 0 if held.group(3) == 'false' else 1
            rules.state(ids, 'HELD_CHARGE', most, kind, modifier, extra, pockets)
            continue
        charge = INLINE.search(inner)
        if charge:
            most = -1 if charge.group(1) == 'nil' else int(charge.group(1))
            kind = -1 if charge.group(2) == 'nil' else int(charge.group(2))
            rules.state(ids, 'CHARGE', most, kind, modifier, extra, int(charge.group(3)))
            continue
        achievement = ACHIEVEMENT.match(inner)
        if achievement:
            # without REPENTOGON the mod's own check always says yes, so a
            # plain one always fires and a negated one never does
            if not achievement.group(1):
                rules.state(ids, 'ALWAYS', 0, 0, modifier, extra)
            else:
                rules.skip('a condition that never fires without REPENTOGON')
            continue
        named = re.match(r'^EID\.(\w+)$', func)
        rules.skip('condition ' + (named.group(0) if named else 'written in place'))

    return rules


# ------------------------------------------------------------- writing it out

def quoted(text):
    if text is None:
        return 'NULL'
    out = text.replace(chr(92), chr(92) * 2).replace('"', chr(92) + '"')
    return '"%s"' % out


def write(rules, out, loc_names, tainted, no_red, tables):
    order = dict((name, i) for i, name in enumerate(loc_names))
    # specific rules before generic ones, and a higher layer before a lower one,
    # which is the order applyConditionals walks them in
    rows = sorted(rules.rows, key=lambda r: (r['kind'], r['id'] < 0, -r['layer']))
    text = io.StringIO()
    text.write('/*\n * The mod\'s conditional descriptions, one row per condition.\n *\n')
    text.write(' * Written by tools/eid_rules.py out of the mod\'s own\n')
    text.write(' * features/eid_conditionals.lua - do not edit here.\n */\n')
    text.write('enum { RULE_OWNS = 0, RULE_CHAR = 1, RULE_GREED = 2, RULE_HARD = 3,\n')
    text.write('       RULE_STAGE_EQ = 4, RULE_STAGE_GT = 5, RULE_SHOP = 6, RULE_RESTOCK = 7,\n')
    text.write('       RULE_TAINTED = 8, RULE_NO_RED = 9, RULE_ALWAYS = 10, RULE_CHARGE = 11,\n')
    text.write('       RULE_HELD_CHARGE = 12, RULE_LIST = 13, RULE_MULTI_ACTIVE = 14 };\n')
    text.write('enum { RULE_CHECK_LAYERS = 1, RULE_NO_TABLE = 2, RULE_USE_RESULT = 4 };\n')
    text.write('enum { RULE_VAR_NONE = 0, RULE_VAR_ITEM = 1, RULE_VAR_PLAYER = 2,'
               ' RULE_VAR_TEXT = 3 };\n\n')
    text.write('static const char *const EID_LOC[] = {\n')
    for name in loc_names:
        text.write('    "%s",\n' % name)
    text.write('};\n\n')
    text.write('/* the character a tainted one counts as, for the mod\'s own char checks */\n')
    text.write('#define EID_PLAYER_KINDS %d\n' % (max(tainted) + 1))
    text.write('static const signed char EID_REGULAR_OF[EID_PLAYER_KINDS] = {\n')
    for line in range(0, max(tainted) + 1, 16):
        row = ['%2d,' % tainted.get(i, -1) for i in range(line, min(line + 16, max(tainted) + 1))]
        text.write('    ' + ' '.join(row) + '\n')
    text.write('};\n\n')
    text.write("/* the characters whose hearts are not red - the mod's own list */\n")
    text.write('#define EID_NO_RED_HEALTH_COUNT %d\n' % len(no_red))
    text.write('static const short EID_NO_RED_HEALTH[EID_NO_RED_HEALTH_COUNT] = {\n    ')
    text.write(' '.join('%d,' % v for v in no_red))
    text.write('\n};\n\n')
    walked = sorted(rules.lists.items(), key=lambda kv: kv[1])
    text.write('/* the item lists the mod walks, in the order the rows point at them */\n')
    rows_out = []
    for name, _ in walked:
        parts = name.split('&')
        items = set(int(k) for k in tables[parts[0]])
        for more in parts[1:]:
            items &= set(int(k) for k in tables[more])
        rows_out.append(sorted(items))
    text.write('#define EID_LISTS %d\n' % len(rows_out))
    for i, items in enumerate(rows_out):
        text.write('static const short EID_LIST_%d[] = { %s -1 };\n'
                   % (i, ' '.join('%d,' % v for v in items)))
    text.write('static const short *const EID_LIST[EID_LISTS] = { %s };\n\n'
               % ' '.join('EID_LIST_%d,' % i for i in range(len(rows_out))))
    pockets = dict((int(k), int(v)) for k, v in tables['PocketActivePlayerIDs'].items())
    text.write('/* characters who carry a pocket active: 0 ordinary, 1 timed, 2 special */\n')
    text.write('#define EID_POCKET_ACTIVE_KINDS %d\n' % (max(pockets) + 1))
    text.write('static const signed char EID_POCKET_ACTIVE[EID_POCKET_ACTIVE_KINDS] = {\n    ')
    text.write(' '.join('%d,' % pockets.get(i, -1) for i in range(max(pockets) + 1)))
    text.write('\n};\n\n')
    hearts = ('Red', 'Soul', 'Black', 'Bone', 'Coin', 'None')
    kind_of = {}
    for name, group in tables['SpecialHeartPlayers'].items():
        if name in hearts:
            for one in group:
                kind_of[int(one)] = hearts.index(name)
    text.write('/* the heart a character has instead of red ones */\n')
    text.write('#define EID_HEART_KINDS %d\n' % (max(kind_of) + 1))
    text.write('static const signed char EID_HEART_KIND[EID_HEART_KINDS] = {\n    ')
    text.write(' '.join('%d,' % kind_of.get(i, 0) for i in range(max(kind_of) + 1)))
    text.write('\n};\n\n')
    up = []
    for key, value in tables['HealthUpData'].items():
        parts = str(key).split('.')
        if len(parts) == 3 and parts[0] == '5' and parts[1] in VARIANTS:
            up.append((VARIANTS[parts[1]], int(parts[2]), int(value)))
    text.write('/* how many hearts an item gives, where the mod knows */\n')
    text.write('typedef struct { unsigned char kind; short id; signed char hearts; } eid_health_t;\n')
    text.write('#define EID_HEALTH_UPS %d\n' % len(up))
    text.write('static const eid_health_t EID_HEALTH_UP[EID_HEALTH_UPS] = {\n')
    for kind, ident, hearts in sorted(up):
        text.write('    { %d, %d, %d },\n' % (kind, ident, hearts))
    text.write('};\n\n')
    for name, label in (('BlackFeatherItems', 'ITEMS'), ('BlackFeatherTrinkets', 'TRINKETS')):
        ids = sorted(int(k) for k in tables[name])
        text.write('/* the "evil" %s Black Feather counts */\n' % label.lower())
        text.write('#define EID_FEATHER_%s_COUNT %d\n' % (label, len(ids)))
        text.write('static const short EID_FEATHER_%s[EID_FEATHER_%s_COUNT] = { %s };\n\n'
                   % (label, label, ' '.join('%d,' % v for v in ids)))
    text.write('typedef struct {\n')
    text.write('    unsigned char kind;      /* the pickup this is about */\n')
    text.write('    short id;                /* -1: every pickup of that kind */\n')
    text.write('    unsigned char test;      /* holding a thing, or being a character */\n')
    text.write('    unsigned char own_kind;  /* what sort of thing is held */\n')
    text.write('    short value;             /* which thing, or which character */\n')
    text.write('    short arg1;              /* the floor, or the charge asked for */\n')
    text.write('    short arg2;              /* the kind of charge asked for */\n')
    text.write('    unsigned char tainted;   /* the character\'s tainted twin counts too */\n')
    text.write('    short layer;             /* the order the mod checks them in */\n')
    text.write('    unsigned char flags;\n')
    text.write('    unsigned char loc;       /* which table the words are in */\n')
    text.write('    unsigned char var;       /* what {1} becomes */\n')
    text.write('    unsigned char var_kind;\n')
    text.write('    short var_id;\n')
    text.write('    unsigned char unique;    /* only one row of a group prints */\n')
    text.write('    unsigned char fallback;  /* English will do if this language has none */\n')
    text.write('    const char *modifier;\n')
    text.write('    const char *bullet;\n')
    text.write('    const char *var_text;\n')
    text.write('    const char *colour;\n')
    text.write('    const char *replace;\n')
    text.write('} eid_rule_t;\n\n')
    text.write('#define EID_RULES %d\n\n' % len(rows))
    text.write('static const eid_rule_t EID_RULE[EID_RULES] = {\n')
    for r in rows:
        flags = ((1 if r['check_layers'] else 0) | (2 if r['no_table'] else 0)
                 | (4 if r['use_result'] else 0))
        text.write('    { %d, %d, RULE_%s, %d, %d, %d, %d, %d, %d, %d, %d, %d, %d, %d, %d, %d,'
                   ' %s, %s, %s, %s, %s },\n'
                   % (r['kind'], r['id'], r['test'], r['own_kind'], r['value'],
                      r['arg1'], r['arg2'], r['tainted'],
                      r['layer'], flags, order[r['loc']], r['var'], r['var_kind'], r['var_id'],
                      r['unique'], r['fallback'],
                      quoted(r['modifier']), quoted(r['bullet']), quoted(r['var_text']),
                      quoted(r['colour']), quoted(r['replace'])))
    text.write('};\n\n')
    # The panel lays itself out again when one of these changes hands: the mod
    # re-checks its conditions every frame, and picking an item up while
    # standing on a pedestal has to change the description then and there.
    watched = sorted(set(r['value'] for r in rows
                         if r['test'] == 'OWNS' and r['own_kind'] == 1))
    text.write('#define EID_WATCHED_ITEMS %d\n' % len(watched))
    text.write('static const short EID_WATCHED[EID_WATCHED_ITEMS] = {\n')
    for line in range(0, len(watched), 16):
        text.write('    ' + ' '.join('%d,' % v for v in watched[line:line + 16]) + '\n')
    text.write('};\n')
    io.open(out, 'w', encoding='utf-8', newline='\n').write(text.getvalue())


def main(mod_dir, out):
    tables = data_tables(mod_dir)
    source = strip_comments(io.open(os.path.join(mod_dir, 'features', 'eid_conditionals.lua'),
                                    encoding='utf-8').read())
    rules = collect(flatten(source, tables), tables)
    from eid_more import LOC_TABLES
    unknown = set(r['loc'] for r in rules.rows) - set(LOC_TABLES)
    if unknown:
        raise SystemExit('conditions point at tables eid_more.py does not carry: %s' % unknown)
    tainted = dict((int(k), int(v)) for k, v in tables['TaintedToRegularID'].items())
    no_red = sorted(set(int(v) for group in tables['SpecialHeartPlayers'].values()
                        for v in group))
    write(rules, out, LOC_TABLES, tainted, no_red, tables)
    print('%s: %d conditions' % (out, len(rules.rows)))
    for kind in sorted(set(r['kind'] for r in rules.rows)):
        n = sum(1 for r in rules.rows if r['kind'] == kind)
        print('    %-12s %d' % (KIND_NAMES.get(kind, '?'), n))
    print('  left out:')
    for why, n in sorted(rules.skipped.items(), key=lambda kv: -kv[1]):
        print('    %-40s %d' % (why, n))


if __name__ == '__main__':
    main(sys.argv[1] if len(sys.argv) > 1 else 'eid-upstream',
         sys.argv[2] if len(sys.argv) > 2 else OUT)
