# -*- coding: utf-8 -*-
"""Desk cases of hotfix 1.71 (05.10.2026, the owner: what worked in 1.666 works the same or better; a mod's file is
refused only when it would crash the game, and then in red). On the real usermods.c (desk.exe, desk0.exe) and the
MODS page (tweaksdesk.exe):

  1. a rooms file (.stb) without the STB1 mark: refused (Repentance's loader 0x750cf0 reads no room, 0x6cec78 falls),
     the game's file handed; one with the mark (another count of rooms) is the mod's; red on the page;
  2. a refused menu .anm2 (all or nothing) and its sheet of no whole multiple: the game's picture, the reason named;
     a sheet a used .anm2 of the mod cuts too is left the mod's;
  3. mod.json slips: read, named; a script-only folder: its name red with its line.

    bash tools/umdesk/build.sh && bash tools/umdesk/build.sh desk0.exe 0 && bash tools/tweaksdesk/build.sh
    py -3 tools/umdesk/test_hotfix171.py [scratch]
"""
import os
import re
import shutil
import subprocess
import sys
import io

HERE = os.path.dirname(os.path.abspath(__file__))
TWEAKS = os.path.join(HERE, '..', 'tweaksdesk', 'tweaksdesk.exe')
ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
GAME = os.path.join(ROOT, 'work', 'app', 'repentance-resources', 'data')
SCRATCH = sys.argv[1] if len(sys.argv) > 1 else os.path.join(os.environ.get('TEMP', '.'), 'umdesk-hotfix171')
failures = []


def check(cond, what, detail=''):
    print(('  ok    ' if cond else '  FAIL  ') + what + ('' if cond or not detail else '\n        ' + str(detail)[:600]))
    if not cond:
        failures.append(what)


def png(w, h):
    from PIL import Image
    b = io.BytesIO()
    Image.new('RGBA', (w, h), (10, 200, 30, 255)).save(b, 'PNG')
    return b.getvalue()


def config(label, mods):
    cfg = os.path.join(SCRATCH, label)
    shutil.rmtree(cfg, ignore_errors=True)
    for mid, files in mods:
        for rel, data in files.items():
            p = os.path.join(cfg, 'mods', mid, *rel.split('/'))
            os.makedirs(os.path.dirname(p), exist_ok=True)
            open(p, 'wb').write(data if isinstance(data, bytes) else data.encode('utf-8'))
    open(os.path.join(cfg, 'mods', 'mods.txt'), 'w').write(''.join('on %s\n' % m for m, _ in mods))
    return cfg


def env(cfg):
    e = {k: v for k, v in os.environ.items() if not k.startswith('DESK_')}
    e['DESK_ROOT'] = os.path.join(cfg, 'files', 'app')
    e['DESK_REAL_PATCH'] = '1'
    os.makedirs(e['DESK_ROOT'], exist_ok=True)
    return e


def desk(exe, cfg, *paths):
    out = subprocess.run([os.path.join(HERE, exe), 'map' if paths else 'guard', cfg] + list(paths), capture_output=True,
                         text=True, env=env(cfg), encoding='utf-8', errors='replace').stdout
    maps = {m.group(1): m.group(2).strip() for m in re.finditer(r'^== map (.*?) -> (.*)$', out, re.M)}
    return out, maps


def verdicts(out):
    return {m.group(2): (m.group(1), m.group(3)) for m in re.finditer(r'   VERDICT (fixed|refused)\s+\S+ (\S*\|.*?) \| (.*)$', out, re.M)}


def page(cfg, k, lang=0):
    out = subprocess.run([TWEAKS, cfg, '480', '270', str(lang), 'guard', 'mods', 'user', str(k), 'notes'], capture_output=True,
                         text=True, env=env(cfg), encoding='utf-8', errors='replace').stdout
    return re.findall(r'^LINK \d+ red (.*)$', out, re.M), dict((int(a), b) for a, b in re.findall(r'^NAME (\d+) (red|ink) ', out, re.M))


ROOMS = 'rooms/01.basement.stb'
good = open(os.path.join(GAME, *ROOMS.split('/')), 'rb').read()
print('1. a rooms file without the STB1 mark')
cfg = config('stb', [('old', {'mod.json': '{"name": "Old rooms"}', 'resources/' + ROOMS: b'\x00\x00\x00\x05' + good[4:]}),
                     ('new', {'mod.json': '{"name": "New rooms"}', 'resources/rooms/02.cellar.stb': good[:4] + b'\x01\x00\x00\x00' + good[8:]})])
for exe in ('desk.exe', 'desk0.exe'):
    out, maps = desk(exe, cfg, 'repentance-resources/data/' + ROOMS, 'repentance-resources/data/rooms/02.cellar.stb')
    v = verdicts(desk(exe, cfg)[0])
    check(maps.get('repentance-resources/data/' + ROOMS) == "(the game's own)", '[%s] no STB1 mark: the game\'s file' % exe, maps)
    check('/mods/new/' in maps.get('repentance-resources/data/rooms/02.cellar.stb', '').replace(chr(92), '/'),
          '[%s] with the mark, another room count: the mod\'s' % exe, maps)
    why = v.get('|' + ROOMS, ('', ''))
    check(why[0] == 'refused' and 'STB1' in why[1] and 'falls' in why[1], '[%s] the reason: %s' % (exe, why[1][:120]))
links, names = page(cfg, 0)
check(links == ['Not used: 1 of 1 file - details'] and names.get(0) == 'red' and names.get(1) == 'ink',
      'the page: the old rooms mod in red, "Not used: 1 of 1 file"', (links, names))

print('2. the sheets of a refused menu .anm2')
menu = 'gfx/ui/main menu/'
REFLASHED_OPTIONS = r'Z:/Games/Steam/steamapps/workshop/content/250900/2934904934/resources/gfx/ui/main menu/optionsmenu.anm2'
odd = png(1008, 1150)
cfg = config('orphan', [('m', {'mod.json': '{"name": "Menus"}',
                               # Reflashed's own (made for Repentance+: its layers renumbered, Paper13 missing)
                               'resources/' + menu + 'optionsmenu.anm2': open(REFLASHED_OPTIONS, 'rb').read(),
                               'resources/' + menu + 'optionsextra.png': odd})])
for exe in ('desk.exe', 'desk0.exe'):
    out, maps = desk(exe, cfg, 'repentance-resources/data/' + menu + 'optionsextra.png')
    v = verdicts(desk(exe, cfg)[0])
    check(v.get('|' + menu + 'optionsmenu.anm2', ('',))[0] == 'refused', '[%s] the menu file: all or nothing, refused' % exe)
    sheet = v.get('|' + menu + 'optionsextra.png', ('', ''))
    check(sheet[0] == 'refused' and '1008x1150' in sheet[1] and 'optionsmenu.anm2' in sheet[1],
          '[%s] its 1008x1150 sheet: the game\'s picture - %s' % (exe, sheet[1][:110]))
    check(maps.get('repentance-resources/data/' + menu + 'optionsextra.png') == "(the game's own)", '[%s] handed: the game\'s' % exe)
# the same sheet cut by another .anm2 of the mod that is used: the mod's
cfg2 = config('orphan-wanted', [('m', {'mod.json': '{"name": "Menus"}',
                                       'resources/' + menu + 'optionsmenu.anm2': open(os.path.join(cfg, 'mods', 'm', 'resources', *(menu + 'optionsmenu.anm2').split('/')), 'rb').read(),
                                       'resources/' + menu + 'mine.anm2': '<AnimatedActor><Content><Spritesheets>'
                                       '<Spritesheet Id="0" Path="optionsextra.png"/></Spritesheets><Layers/><Nulls/><Events/>'
                                       '</Content><Animations DefaultAnimation="A"><Animation Name="A" FrameNum="1" Loop="false">'
                                       '<RootAnimation><Frame Delay="1"/></RootAnimation><LayerAnimations/></Animation></Animations>'
                                       '</AnimatedActor>',
                                       'resources/' + menu + 'optionsextra.png': odd})])
v = verdicts(desk('desk.exe', cfg2)[0])
check(('|' + menu + 'optionsextra.png') not in v or v['|' + menu + 'optionsextra.png'][0] != 'refused',
      'a sheet another .anm2 of the mod cuts: left the mod\'s', v.get('|' + menu + 'optionsextra.png'))

print('3. mod.json slips, a script-only folder')
cfg = config('cards', [('slips', {'mod.json': "{'name': 'Slips', // hand-made\n 'version': 1.0,}", 'resources/gfx/x.png': png(4, 4)}),
                       ('lua', {'metadata.xml': '<metadata><name>Lua</name></metadata>', 'main.lua': 'x'})])
links, names = page(cfg, 1)
out = subprocess.run([os.path.join(HERE, 'desk.exe'), 'settings', cfg], capture_output=True, text=True, env=env(cfg),
                     encoding='utf-8', errors='replace').stdout
check('name="Slips"' in out and 'broken=0' in [l for l in out.splitlines() if 'MOD slips' in l][0], 'slips: read and named')
check(names.get(0) == 'ink' and names.get(1) == 'red' and links == ['Scripts do not run here - nothing to use'],
      'a script-only folder: red, with its line', (links, names))

print('\n%d failures' % len(failures))
for f in failures:
    print('  - ' + f)
sys.exit(1 if failures else 0)
