# -*- coding: utf-8 -*-
"""The entity names EID prints in a glitched item's description.

"Spawn a {1}", "Convert all {1} in the room to {2}", "When a {T1} is spawned":
the mod fills these from its own table EID.XMLEntityNames (features/
eid_xmldata.lua, built from entities2.xml), keyed "type.variant" or
"type.variant.subtype" - EID:GetEntityXMLName tries the first, then the
second. The names are English there, as they are in the mod. GlitchedItemText
can override a few in each language (eid.bin section 18); eid_glitch.c asks
that first, as the mod does.

And the entities that do NOT keep a room's doors shut (shutdoors="false" in
the game's own entities2.xml): the Bag of Crafting display hides while the room
is fought (BagOfCraftingHideInBattle), and a fight is a hostile enemy that
shuts the doors - the game's own rule for a room that is not clear.

    py -3 tools/eid_entities.py <EID mod dir>
"""
import io
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(HERE, '..', 'port', 'app', 'src', 'main', 'cpp', 'eid_entities.h')
DOORS = os.path.join(HERE, '..', 'port', 'app', 'src', 'main', 'cpp', 'eid_doors.h')
ENTITIES2 = os.path.join(HERE, '..', 'work', 'app', 'repentance-resources', 'data', 'entities2.xml')


def open_doors():
    """(type, variant) of every entity entities2.xml says does not shut the doors."""
    src = io.open(ENTITIES2, encoding='utf-8', errors='replace').read()
    out = set()
    for e in re.findall(r'<entity\s[^>]*>', src):
        if not re.search(r'\sshutdoors="false"', e):
            continue
        t = re.search(r'\sid="(\d+)"', e)
        v = re.search(r'\svariant="(\d+)"', e)
        out.add((int(t.group(1)), int(v.group(1)) if v else 0))
    return sorted(out)


def entity_names(mod_dir):
    src = io.open(os.path.join(mod_dir, 'features', 'eid_xmldata.lua'), encoding='utf-8').read()
    m = re.search(r'EID\.XMLEntityNames\s*=\s*\{', src)
    if not m:
        sys.exit('EID.XMLEntityNames is not in eid_xmldata.lua')
    names = {}
    for key, name in re.findall(r'\["([-\d.]+)"\]\s*=\s*"((?:[^"\\]|\\.)*)"', src[m.end():src.index('}', m.end())]):
        names[key] = name.replace('\\"', '"')
    return names


def main(mod_dir):
    names = entity_names(mod_dir)
    rows = []
    for key, name in names.items():
        parts = [int(p) for p in key.split('.')]
        if len(parts) == 2:
            parts.append(-1)                # "type.variant": any subtype
        rows.append((parts[0], parts[1], parts[2], name))
    rows.sort()
    with io.open(OUT, 'w', encoding='utf-8', newline='\n') as f:
        f.write('/* Written by tools/eid_entities.py from the EID mod\'s EID.XMLEntityNames - do not edit. */\n')
        f.write('#ifndef EID_ENTITIES_H\n#define EID_ENTITIES_H\n\n')
        f.write('/* sorted by type, variant, subtype; subtype -1 is a "type.variant" key */\n')
        f.write('static const struct { short type, variant, subtype; const char *name; } EID_ENTITY[] = {\n')
        for t, v, s, n in rows:
            f.write('    { %d, %d, %d, "%s" },\n' % (t, v, s, n.replace('\\', '\\\\').replace('"', '\\"')))
        f.write('};\n\n#endif\n')
    doors = open_doors()
    with io.open(DOORS, 'w', encoding='utf-8', newline='\n') as f:
        f.write('/* Written by tools/eid_entities.py from entities2.xml - do not edit. */\n')
        f.write('#ifndef EID_DOORS_H\n#define EID_DOORS_H\n\n')
        f.write('/* shutdoors="false": these never keep a room from being clear */\n')
        f.write('static const struct { short type, variant; } EID_OPEN_DOORS[] = {\n')
        for t, v in doors:
            f.write('    { %d, %d },\n' % (t, v))
        f.write('};\n\n#endif\n')
    print('%d entity names -> %s' % (len(rows), os.path.normpath(OUT)))
    print('%d that leave the doors open -> %s' % (len(doors), os.path.normpath(DOORS)))


if __name__ == '__main__':
    main(sys.argv[1])
