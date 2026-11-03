# -*- coding: utf-8 -*-
"""The export desk's inputs (run_export.sh): a pack (ISAACPK1, tools/pack.py's format) of a slice of work/app -
the menus of every Repentance set, its shaders, an older edition's touch folder - and the bring_in registry the
C code makes (asset TAB <folder>/<leaf>), as the built-in mods and the item descriptions bring their files in."""
import os
import struct
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..'))
import pack

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
APP = os.path.join(ROOT, 'work', 'app')
ASSETS = os.path.join(ROOT, 'port', 'app', 'src', 'main', 'assets')
work = sys.argv[1]
SLICE = ['repentance-resources/%s/gfx/ui/main menu' % s for s in
         ('data', 'dlc3.de', 'dlc3.es', 'dlc3.jp', 'dlc3.kr', 'dlc3.ru', 'dlc3.zh')]
SLICE += ['repentance-resources/data/shaders', 'rebirth-resources/data/gfx/ui/touch', 'rebirth-resources/data.jp/gfx/ui/touch']
files = []
for d in SLICE:
    full = os.path.join(APP, *d.split('/'))
    if not os.path.isdir(full):
        continue
    for f in sorted(os.listdir(full)):
        p = os.path.join(full, f)
        if os.path.isfile(p) and pack.wanted(f):
            files.append((d + '/' + f, p))
with open(os.path.join(work, 'slice.pak'), 'wb') as out:
    out.write(b'ISAACPK1' + struct.pack('>i', len(files)))
    for name, p in files:
        b = open(p, 'rb').read()
        n = name.encode('utf-8')
        out.write(struct.pack('>i', len(n)) + n + struct.pack('>q', len(b)) + b)
# the registry: what bring_in enters (eid.c, mods.c, bossbars.c, credits.c, touchedit.c, marked.c, tweaks.c)
reg = []
def walk(asset, game):
    p = os.path.join(ASSETS, *asset.split('/'))
    if os.path.isdir(p):
        for f in sorted(os.listdir(p)):
            walk(asset + '/' + f, game + '/' + f)
    elif os.path.isfile(p):
        reg.append((asset, game))
for a, g in [('port_specialist', 'gfx/port_specialist'), ('port_bossbars/bosshp_bars', 'gfx/ui/bosshp_bars'),
             ('port_bossbars/bosshp_icons', 'gfx/ui/bosshp_icons')]:
    walk(a, g)
for f in sorted(os.listdir(ASSETS)):
    p = os.path.join(ASSETS, f)
    if not os.path.isfile(p):
        continue
    if f.startswith('port_') and f.endswith(('.png', '.anm2')):
        reg.append((f, 'gfx/' + f))
    elif f.startswith('eid_') and f.endswith(('.png', '.anm2')) and ('_0' in f or '_1' in f) and not f.endswith('.anm2'):
        reg.append((f, 'font/' + f))
    elif f.startswith('eid_') and f.endswith('.fnt'):
        reg.append((f, 'font/' + f))
    elif f.startswith('eid_') and f.endswith(('.png', '.anm2')):
        reg.append((f, 'gfx/' + f))
with open(os.path.join(work, 'registry.txt'), 'w', encoding='utf-8') as out:
    for a, g in reg:
        out.write('%s\t%s\n' % (a, g))
print('slice.pak: %d files; registry: %d port assets' % (len(files), len(reg)))
