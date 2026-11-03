# -*- coding: utf-8 -*-
"""modport_ref: the game reference the web converter (other stuff/usermods/modport-web) reads.

    py -3 tools/modport_ref.py        writes other stuff/usermods/modport-web/src/gameref.bin,
    py -3 tools/modport_web.py        then builds the single-file page modport.html from src/

Walks the resource tree the pak is built from (work/app/repentance-resources,
with pack.py's own filter, so the port's edited files count and reference
copies do not) and writes one zlib-packed JSON:

    sets   {"resources": "data", "resources-dlc3.ru": "dlc3.ru", ...}
    anm2   {"resources/gfx/001.000_player.anm2":
              {"l": [[id, name], ...], "n": [[id, name], ...], "e": [[id, name], ...],
               "a": ["Pickup", ...], "s": ["005.022_nickel.png", ...]}}     s: its sheets' Path as written
    png    {"resources/gfx/....png": [w, h]}
    xml    {"resources/items.xml": "<items ...>"}        the TEXT_XML files of modport.py
    xmls   ["resources/ambush.xml", ...]                 every xml the game has, names only
    stb    {"resources/rooms/00.special rooms.stb": "5354423100000000"}   the first 8 bytes, hex
    cut    ["resources/gfx/backdrop/controls.png", ...]   pictures some anm2 of the game cuts
    portcut ["resources/gfx/ui/main menu/gamemenu.png", ...]   pictures the port draws at the game's layout (modport.port_drawn)
    portcode ["resources/gfx/items/collectibles/collectibles_394_marked.png", ...]   of those, the ones its C code points a sprite at
    bynumber {"gfx/ui/completion_widget.anm2": ["gfx/ui/completion_widget.png", "gfx/ui/completion_widget_pause.png"], ...}
             the pictures of the files the port reads by layer number, without the set folder (modport.by_number_sheets):
             held to the game's size only when the mod brings no copy of that anm2 of its own (hotfix 1.71)
    portpatch [{"target": "repentance-resources/data/gfx/ui/main menu/gamemenu.anm2", "set": "data", "rel": ...,
               "ops": [...], "adds": [...], "game": {"layers": [[name, id]], "nulls": [...], "anims": [...]}}]
             the port's patched menu files and what its all-or-nothing check needs (modport.port_patch_targets)
    bymenu   {"gfx/ui/main menu/gamemenu.anm2": ["gfx/ui/main menu/gamemenu.png", ...], ...}   the same for the menus
             the port's check sizes under the game's frames (modport.menu_sheets, usermods.c guard_menu_sheets)
    iosonly {"resources/gfx/ui/extratouchwidgets.anm2": "<AnimatedActor ..."}   the iOS-only anm2 (modport.IOS_ONLY_ANM2)
    portown {"resources/gfx/eid_inline_icons.anm2": [size, crc32], ...}   files the port's built-ins bring
    tables {"resources/players.xml": "<players ...", ...}   the other whole data tables of data/

The paths are the mod's paths (resources/..., resources-dlc3.ru/...), lower case.
The same rules as tools/modport.py; the page is that script in JavaScript.
"""
import io
import json
import os
import struct
import sys
import zlib
import xml.etree.ElementTree as ET

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from pack import wanted  # noqa: E402
from modport import TEXT_XML, PORT_FILES, OLDER_EDITIONS, IOS_ONLY_ANM2, port_drawn, port_code, port_own_files, by_number_sheets, menu_sheets, port_patch_targets  # noqa: E402

PROJECT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
ROOT = os.path.join(PROJECT, 'work', 'app', 'repentance-resources')
OUT = os.path.join(PROJECT, 'other stuff', 'usermods', 'modport-web', 'src', 'gameref.bin')


def png_size(path):
    with open(path, 'rb') as f:
        head = f.read(24)
    if head[:8] != b'\x89PNG\r\n\x1a\n' or head[12:16] != b'IHDR':
        return None
    return list(struct.unpack('>II', head[16:24]))


def anm2_shape(path):
    try:
        root = ET.parse(path).getroot()
    except ET.ParseError:
        return None
    c = root.find('Content')
    def pairs(tag, sub):
        if c is None:
            return []
        return [[int(e.get('Id')), e.get('Name', '')] for e in c.findall('%s/%s' % (tag, sub))]
    return {
        'l': pairs('Layers', 'Layer'),
        'n': pairs('Nulls', 'Null'),
        'e': pairs('Events', 'Event'),
        'a': [a.get('Name') for a in root.findall('Animations/Animation')],
        'sheets': [sh.get('Path', '').replace('\\', '/') for sh in root.findall('Content/Spritesheets/Spritesheet')],
    }


def main():
    out = OUT
    if '--out' in sys.argv:
        out = sys.argv[sys.argv.index('--out') + 1]
    ref = {'sets': {}, 'anm2': {}, 'png': {}, 'xml': {}, 'xmls': [], 'stb': {}, 'cut': []}
    cut = set()                                    # pictures a game anm2 cuts (mod paths)
    portcut = set()                                # pictures the port's own menu anm2 cut
    # the Repentance sets, and the Rebirth-set folders the game reads too (the touch controls)
    walks = []
    for set_ in sorted(os.listdir(ROOT)):
        top = os.path.join(ROOT, set_)
        if not os.path.isdir(top):
            continue
        modset = 'resources' if set_ == 'data' else 'resources-' + set_
        ref['sets'][modset] = set_
        walks.append((top, modset, top))
    rebirth = os.path.join(os.path.dirname(ROOT), 'rebirth-resources', 'data')
    # the older editions the engine mounts under Repentance, nearest first; a path already
    # taken stays with the later edition (setdefault below)
    for ed in OLDER_EDITIONS:
        older = os.path.join(os.path.dirname(ROOT), ed, 'data')
        walks.append((older, 'resources', older))
    for start, modset, top in walks:
        for r, _, fs in os.walk(start):
            for f in fs:
                if not wanted(f):
                    continue
                full = os.path.join(r, f)
                rel = (modset + '/' + os.path.relpath(full, top).replace(os.sep, '/')).lower()
                ext = f.rsplit('.', 1)[-1].lower() if '.' in f else ''
                if ext == 'png':
                    s = png_size(full)
                    if s:
                        ref['png'].setdefault(rel, s)
                elif ext == 'anm2':
                    s = anm2_shape(full) if rel not in ref['anm2'] else None     # the later edition's stays
                    if s:
                        ref['anm2'][rel] = s
                        base = rel.rsplit('/', 1)[0]
                        s['s'] = s.pop('sheets')
                        for sheet in s['s']:
                            full = os.path.normpath(os.path.join(base, sheet)).replace(os.sep, '/').lower()
                            cut.add(full)
                            if rel.split('/', 1)[1] in PORT_FILES:
                                portcut.add(full)
                elif ext == 'xml':
                    if rel in ref['xmls']:
                        continue                                            # the later edition's stays
                    ref['xmls'].append(rel)
                    if f.lower() in TEXT_XML:
                        ref['xml'][rel] = open(full, 'rb').read().decode('utf-8', 'replace')
                elif ext == 'stb':
                    ref['stb'].setdefault(rel, open(full, 'rb').read(8).hex())
    # the Rebirth-set pictures the port itself draws (largebomb.png for the touch editor)
    for rel in port_drawn():
        full = os.path.join(rebirth, rel.split('/', 1)[1])
        if rel not in ref['png'] and full.lower().endswith('.png') and os.path.isfile(full):
            s = png_size(full)
            if s:
                ref['png'][rel] = s
    ref['cut'] = sorted(cut)
    ref['portcut'] = sorted(portcut | port_drawn())
    ref['portcode'] = sorted(port_code())
    # the files read by number: their game anm2 by the mod's path, Repentance's first, then the older editions
    def game_read(modpath):
        for ed_dir in [os.path.join(ROOT, 'data')] + [os.path.join(os.path.dirname(ROOT), ed, 'data') for ed in OLDER_EDITIONS]:
            full = os.path.join(ed_dir, *modpath.split('/', 1)[1].split('/'))
            if os.path.isfile(full):
                return open(full, 'rb').read()
        return None
    ref['bynumber'] = by_number_sheets(game_read)
    ref['bymenu'] = menu_sheets(game_read)
    # round 3 (05.10.2026): the port's patched menu files, for the converters' mirror of its check at start
    ref['portpatch'] = port_patch_targets()
    # 04.10.2026: the iOS-only anm2 of the game (their text: the converter puts them into a mod fitted to its
    # pictures), the files the port's built-ins bring ([size, crc32]: a mod's copy is the same or not), and the
    # whole data tables beside the TEXT_XML ones (a whole table against the game's, beyond its text)
    import zlib as _z
    ref['iosonly'] = {}
    for rel in IOS_ONLY_ANM2:
        for ed_dir in [ROOT + os.sep + 'data'] + [os.path.join(os.path.dirname(ROOT), ed, 'data') for ed in OLDER_EDITIONS]:
            full = os.path.join(ed_dir, *rel.split('/', 1)[1].split('/'))
            if os.path.isfile(full):
                ref['iosonly'][rel] = open(full, 'rb').read().decode('utf-8', 'replace')
                break
    ref['portown'] = {}
    for rel, full in sorted(port_own_files().items()):
        b = open(full, 'rb').read()
        ref['portown'][rel] = [len(b), _z.crc32(b) & 0xffffffff]
    ref['tables'] = {}
    data = os.path.join(ROOT, 'data')
    for f in sorted(os.listdir(data)):
        rel = 'resources/' + f.lower()
        if f.lower().endswith('.xml') and wanted(f) and rel not in ref['xml'] and os.path.isfile(os.path.join(data, f)):
            ref['tables'][rel] = open(os.path.join(data, f), 'rb').read().decode('utf-8', 'replace')
    raw = json.dumps(ref, ensure_ascii=False, separators=(',', ':')).encode('utf-8')
    packed = zlib.compress(raw, 9)
    os.makedirs(os.path.dirname(out), exist_ok=True)
    with open(out, 'wb') as f:
        f.write(packed)
    print('anm2 %d  png %d  xml %d  stb %d' % (len(ref['anm2']), len(ref['png']), len(ref['xml']), len(ref['stb'])))
    print('json %d bytes, packed %d -> %s' % (len(raw), len(packed), out))


if __name__ == '__main__':
    main()
