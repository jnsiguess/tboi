# -*- coding: utf-8 -*-
"""What a card or a pill is, and what it costs a mimic, out of the game's XML.

The mod asks the game for these two things through the modding API, which this
port has not got: `itemConfig:GetCard(id)` for the kind of card it is, and the
config's MimicCharge for how much a Blank Card or a Clear Rune or a Placebo
spends on it. Both come from pocketitems.xml, so they are read out of the
game's own resources once and written as a table.

    py -3 tools/eid_charge.py [work/app/repentance-resources/data/pocketitems.xml]

The kind numbers are the game's own, taken from its parser at 0x73e028: it
compares the type attribute against tarot, suit, rune, special, object and
tarot_reverse in that order and stores 0 to 5.
"""
import io
import os
import re
import sys

TYPES = ['tarot', 'suit', 'rune', 'special', 'object', 'tarot_reverse']
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
XML = os.path.join(ROOT, 'work', 'app', 'repentance-resources', 'data', 'pocketitems.xml')
OUT = os.path.join(ROOT, 'port', 'app', 'src', 'main', 'cpp', 'eid_charge.h')


def actives(path):
    """{collectible: (max charges, charge type)} out of the game's items.xml.

    The mod asks its config the same two things (eid_conditionals_funcs.lua,
    CheckActiveChargeType): how much an active holds, and whether it charges by
    rooms, by time, or not at all. The game keeps both in its own XML, the
    charge type as a word and an absent one meaning the ordinary kind.
    """
    kinds = {'timed': 1, 'special': 2}
    text = io.open(path, encoding='utf-8', errors='replace').read()
    found = {}
    for tag in re.findall(r'<active\b[^>]*>', text):
        ident = re.search(r'\bid="(\d+)"', tag)
        charges = re.search(r'\bmaxcharges="(\d+)"', tag)
        kind = re.search(r'\bchargetype="(\w+)"', tag)
        if ident:
            found[int(ident.group(1))] = (int(charges.group(1)) if charges else 0,
                                          kinds.get(kind.group(1) if kind else '', 0))
    return found


def main(xml, out):
    d = io.open(xml, encoding='utf-8', errors='replace').read()
    cards = {}
    for tag in re.findall(r'<(?:card|rune)\b[^>]*>', d):
        ident = int(re.search(r'\bid="(\d+)"', tag).group(1))
        kind = re.search(r'\btype="([^"]*)"', tag)
        charge = re.search(r'\bmimiccharge="(\d+)"', tag)
        cards[ident] = (TYPES.index(kind.group(1)) if kind and kind.group(1) in TYPES else -1,
                        int(charge.group(1)) if charge else -1)
    pills = {}
    classes = {}
    for tag in re.findall(r'<pilleffect\b[^>]*>', d):
        ident = int(re.search(r'\bid="(\d+)"', tag).group(1))
        charge = re.search(r'\bmimiccharge="(\d+)"', tag)
        pills[ident] = int(charge.group(1)) if charge else -1
        # the class says how big the effect is and whether it is good or bad,
        # which is what False PHD reads to know what it turns the pill into
        said = re.search(r'\bclass="([^"]*)"', tag)
        said = said.group(1) if said else ''
        flags = 0
        for digit, bit in (('1', 1), ('2', 2), ('3', 4)):
            if digit in said:
                flags |= bit
        if '-' in said:
            flags |= 8
        if '+' in said:
            flags |= 16
        classes[ident] = flags

    text = io.StringIO()
    text.write('/*\n * Cards and pills: what kind, and what a mimic pays for them.\n *\n')
    text.write(' * Written by tools/eid_charge.py out of the game\'s own pocketitems.xml -\n')
    text.write(' * do not edit here. The kind is the game\'s own number: 0 tarot, 1 suit,\n')
    text.write(' * 2 rune, 3 special, 4 object, 5 reversed tarot, -1 nothing.\n */\n')
    text.write('#define EID_CARD_MAX %d\n' % max(cards))
    text.write('#define EID_PILL_MAX %d\n\n' % max(pills))
    text.write('static const signed char EID_CARD_KIND[EID_CARD_MAX + 1] = {\n')
    for line in range(0, max(cards) + 1, 16):
        row = ['%2d,' % cards.get(i, (-1, -1))[0] for i in range(line, min(line + 16, max(cards) + 1))]
        text.write('    ' + ' '.join(row) + '\n')
    text.write('};\n\n')
    text.write('static const signed char EID_CARD_CHARGE[EID_CARD_MAX + 1] = {\n')
    for line in range(0, max(cards) + 1, 16):
        row = ['%2d,' % cards.get(i, (-1, -1))[1] for i in range(line, min(line + 16, max(cards) + 1))]
        text.write('    ' + ' '.join(row) + '\n')
    text.write('};\n\n')
    text.write('/* the pill\'s class: 1, 2, 4 how big, 8 bad, 16 good */\n')
    text.write('enum { PILL_SMALL = 1, PILL_MIDDLING = 2, PILL_BIG = 4,\n')
    text.write('       PILL_BAD = 8, PILL_GOOD = 16 };\n')
    text.write('static const signed char EID_PILL_CLASS[EID_PILL_MAX + 1] = {\n')
    for line in range(0, max(pills) + 1, 16):
        row = ['%2d,' % classes.get(i, 0) for i in range(line, min(line + 16, max(pills) + 1))]
        text.write('    ' + ' '.join(row) + '\n')
    text.write('};\n\n')
    text.write('static const signed char EID_PILL_CHARGE[EID_PILL_MAX + 1] = {\n')
    for line in range(0, max(pills) + 1, 16):
        row = ['%2d,' % pills.get(i, -1) for i in range(line, min(line + 16, max(pills) + 1))]
        text.write('    ' + ' '.join(row) + '\n')
    text.write('};\n\n')
    charge = actives(os.path.join(os.path.dirname(xml), 'items.xml'))
    top = max(charge)
    text.write('/* active items: how much they hold, and how they charge */\n')
    text.write('#define EID_ACTIVE_MAX %d\n' % top)
    text.write('enum { CHARGE_NORMAL = 0, CHARGE_TIMED = 1, CHARGE_SPECIAL = 2 };\n\n')
    # an int, not a signed char: a timed active holds its charge in frames - 110,
    # 450, 900, and 100000 for one - which a byte wrapped into nonsense (clang:
    # "changes value from 450 to -62", 25.09.2026)
    text.write('static const int EID_ACTIVE_CHARGES[EID_ACTIVE_MAX + 1] = {\n')
    for line in range(0, top + 1, 16):
        row = ['%2d,' % charge.get(i, (-1, -1))[0] for i in range(line, min(line + 16, top + 1))]
        text.write('    ' + ' '.join(row) + '\n')
    text.write('};\n\n')
    text.write('static const signed char EID_ACTIVE_KIND[EID_ACTIVE_MAX + 1] = {\n')
    for line in range(0, top + 1, 16):
        row = ['%2d,' % charge.get(i, (-1, -1))[1] for i in range(line, min(line + 16, top + 1))]
        text.write('    ' + ' '.join(row) + '\n')
    text.write('};\n')
    # every table still written as signed char must fit one
    for name, values in (('EID_CARD_KIND', [c[0] for c in cards.values()]),
                         ('EID_CARD_CHARGE', [c[1] for c in cards.values()]),
                         ('EID_PILL_CLASS', list(classes.values())),
                         ('EID_PILL_CHARGE', list(pills.values())),
                         ('EID_ACTIVE_KIND', [c[1] for c in charge.values()])):
        bad = [v for v in values if not -128 <= v <= 127]
        if bad:
            sys.exit('%s holds %s - too wide for a signed char' % (name, bad[:5]))
    io.open(out, 'w', encoding='utf-8', newline='\n').write(text.getvalue())
    print('%s: %d cards, %d pills, %d actives' % (out, len(cards), len(pills), len(charge)))


if __name__ == '__main__':
    main(sys.argv[1] if len(sys.argv) > 1 else XML,
         sys.argv[2] if len(sys.argv) > 2 else OUT)
