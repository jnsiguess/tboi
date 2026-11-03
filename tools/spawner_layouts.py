# -*- coding: utf-8 -*-
"""The spawner's ENEMIES tab: bosses that are more than one entity, laid out
the way their own rooms lay them out.

WHY. A boss of the game is sometimes several entities that its room places
together, and its AI only links what the room put there:

  Larry Jr. / The Hollow / Tuff Twin / The Shell (19.0-3), Turdlet (918.0)
      every segment, on its first update, makes itself the child of a
      same-type segment within 42 that has no child yet (0x5f7ad4,
      0x712608) - the chain is the room's row of cells, 40 apart;
  Chub / C.H.A.D. / The Carrion Queen (28.0-2)
      the first to update gathers the free Chubs within 57.6 (a diagonal
      cell) and chains three (0x602e54 -> 0x603534); it grows missing
      segments itself only for Delirium (+0xbf8, written by 0x4e4xxx) or an
      inherited +0x10c0 (0x6036fc) - a lone Chub is a head with no body;
  Mom (45.0)
      five 45.0, four in the cells inside the 1x1 doors and one in the
      centre (0x55d244, see spawner.c "Mom, the way her room makes her").

Nothing in the game spawns these types except their rooms (no Game::Spawn of
19, 28, 45 or 918 anywhere in the image) - and Greed mode's waves, which
spawn a boss room's layout at its own cells in the current room (0x5af884
"Spawning greed mode wave from room variant %d", 0x5afc48). The spawner does
the same: the parts at the layout's own cells.

WHICH LAYOUT. From every boss room (type 5) of repentance-resources: the
connected groups of the boss's own type/variant (4-neighbour cells for the
worms that link within 42, 8-neighbour for Chub that links within 57.6), the
most frequent group size, and the first such group that lies in a 1x1 room's
cells (0..12, 0..6), preferring a 1x1 room - in the room's own spawn order,
which is the order the parts update in and so the order they chain in.
Mom: the five 45.0 of the Mom room. Pairs (Lokii, Gurglings, Turdlings):
all of the type in the room, the most frequent count. Headless Horseman with its
head and Singe with its balls: the body/boss count most frequent, with the
partners of that room (PARTNERS).

    port/app/src/main/cpp/spawner_layouts.h

    py -3 tools/spawner_layouts.py
"""
import collections
import glob
import os
import sys
import xml.etree.ElementTree as ET

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import spawner_common as sc    # noqa: E402

ROOMS = os.path.join(sc.DATA, 'rooms')
HEADER = os.path.join(sc.CPP, 'spawner_layouts.h')

# (type, variant): neighbourhood the AI links within (4 or 8); 0 = the whole room's set
WANTED = {
    (19, 0): 4, (19, 1): 4, (19, 2): 4, (19, 3): 4,
    (918, 0): 4,
    (28, 0): 8, (28, 1): 8, (28, 2): 8,
    (45, 0): 0,
    (69, 1): 0,                      # Lokii: the twins
    (237, 1): 0, (237, 2): 0,        # Gurglings, Turdlings: the pair
    (82, 0): 0,                      # Headless Horseman: the body ...
    (915, 0): 0,                     # Singe ...
}
# ... and the parts of the fight its room always places with it, which its AI does not make
PARTNERS = {
    (82, 0): [(83, 0)],              # ... with its head (in every Horseman room; 82's AI spawns no 83)
    (915, 0): [(915, 1)],            # ... with its four balls (every Singe room; the AI only tops them up
                                     #     to +0xbf0 + 4 over time, 0x6214b0)
}


def rooms():
    for path in sorted(glob.glob(os.path.join(ROOMS, '*.xml'))):
        root = ET.parse(path).getroot()
        for room in root.iter('room'):
            if room.get('type') != '5':
                continue
            cells = []
            for sp in room.iter('spawn'):
                for en in sp.iter('entity'):
                    cells.append((int(en.get('type')), int(en.get('variant')), int(en.get('subtype')),
                                  int(sp.get('x')), int(sp.get('y'))))
            yield os.path.basename(path), room, cells


def groups(cells, near):
    """connected groups in the room's own order"""
    left = list(range(len(cells)))
    out = []
    while left:
        group = [left.pop(0)]
        grew = True
        while grew:
            grew = False
            for i in list(left):
                for j in group:
                    dx = abs(cells[i][3] - cells[j][3])
                    dy = abs(cells[i][4] - cells[j][4])
                    if dx + dy == 1 or (near == 8 and dx == 1 and dy == 1):
                        group.append(i)
                        left.remove(i)
                        grew = True
                        break
        out.append([cells[k] for k in sorted(group)])
    return out


def main():
    found = {k: [] for k in WANTED}
    for name, room, cells in rooms():
        for key, near in WANTED.items():
            own = [c for c in cells if (c[0], c[1]) == key]
            if not own:
                continue
            if near == 0:
                with_parts = [c for c in cells if (c[0], c[1]) == key or (c[0], c[1]) in PARTNERS.get(key, [])]
                if PARTNERS.get(key) and not all(any((c[0], c[1]) == q for c in with_parts) for q in PARTNERS[key]):
                    continue
                found[key].append((room.get('shape'), name, room.get('variant'), with_parts, len(own)))
            else:
                for g in groups(own, near):
                    found[key].append((room.get('shape'), name, room.get('variant'), g, len(g)))
    lines = []
    parts = []
    for key in WANTED:
        sizes = collections.Counter(f[4] for f in found[key])
        size = sizes.most_common(1)[0][0]
        fits = [f for f in found[key] if f[4] == size
                and all(0 <= c[3] <= 12 and 0 <= c[4] <= 6 for c in f[3])]
        fits.sort(key=lambda f: f[0] != '1')
        shape, name, variant, g, _ = fits[0]
        lines.append('    { %d, %d, %d, %d },   /* %s variant %s shape %s; group sizes %s */'
                     % (key[0], key[1], len(parts), len(g), name, variant, shape,
                        dict(sizes.most_common(5))))
        for c in g:
            parts.append('    { %d, %d, %d, %d, %d },' % c)
        print('%d.%d: %d parts from %s variant %s (shape %s), sizes %s'
              % (key[0], key[1], len(g), name, variant, shape, dict(sizes.most_common(5))))
    with open(HEADER, 'w', encoding='utf-8', newline='\n') as f:
        f.write('/* Written by tools/spawner_layouts.py - do not edit by hand, run the tool. */\n')
        f.write('#ifndef SPAWNER_LAYOUTS_H\n#define SPAWNER_LAYOUTS_H\n\n')
        f.write('/*\n * Bosses that are several entities, as their own boss rooms lay them out\n'
                ' * (tools/spawner_layouts.py says why). A part is a cell of the room\'s\n'
                ' * layout, x 0..12, y 0..6; the game puts cell (x, y) at\n'
                ' * (80 + 40x, 160 + 40y). Parts in the room\'s own spawn order.\n */\n')
        f.write('typedef struct { unsigned short type, variant, subtype; signed char x, y; } sp_part_t;\n\n')
        f.write('#define SP_LAYOUTS %d\n' % len(lines))
        f.write('static const struct { unsigned short type, variant; unsigned char first, count; } '
                'SP_LAYOUT[SP_LAYOUTS] = {\n')
        f.write('\n'.join(lines) + '\n};\n\n')
        f.write('static const sp_part_t SP_LAYOUT_PART[] = {\n' + '\n'.join(parts) + '\n};\n\n')
        f.write('#endif\n')
    print('written', HEADER)


if __name__ == '__main__':
    main()
