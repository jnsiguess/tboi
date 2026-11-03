# -*- coding: utf-8 -*-
"""What the real mods we keep get, file by file, after pack 5 (M8/M9, all-or-nothing menus, the strap's pixels).

The real mods in the project: other stuff/usermods/ported/reflashed.zip, other stuff/usermods/example-mod.zip,
green-isaac.zip, red-isaac.zip. The PT-BR translation and onlyQ4 itempools mod were deleted by the owner -
their classes are played synthetically: a whole-file translation (the game's own data files brought whole:
stringtable.sta, menu .anm2 and sheets, fonts' pages, tables) and a whole itempools.xml replacement.

For each: THE GUARD's verdicts and the answers for the paths pack 5 touched (the title's iOS sheet, the
menu files and sheets, the completion widget, the tables), with what 1.666 gave - every difference named.
1.666 (tboi-1.666.apk, 01.10.2026) had no guard at all: its libisaacport.so has none of the guard's texts,
its map is "%s/%s -> a mod's file" - every file of an enabled mod as it is (measured 05.10.2026, hotfix 1.71).
The BY_NUMBER rule came on 02.10 with the guard; the hotfix took its refusal of a mod's own anm2 back.

    bash tools/umdesk/build.sh && bash tools/umdesk/build.sh desk0.exe 0 && py -3 tools/umdesk/test_real_mods.py
"""
import os
import re
import shutil
import subprocess
import sys
import zipfile

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
GAME = os.path.join(ROOT, 'work', 'app', 'repentance-resources', 'data')
MODS = os.path.join(ROOT, 'other stuff', 'usermods')
SCRATCH = sys.argv[1] if len(sys.argv) > 1 else os.path.join(os.environ.get('TEMP', '.'), 'umdesk-real-mods')
MENU = 'gfx/ui/main menu/'
failures = []


def check(cond, what):
    print(('  ok    ' if cond else '  FAIL  ') + what)
    if not cond:
        failures.append(what)


def run(cfg, root, *args):
    e = {k: v for k, v in os.environ.items() if not k.startswith('DESK_')}
    e['DESK_ROOT'] = root
    e['DESK_REAL_PATCH'] = '1'
    out = subprocess.run([os.path.join(HERE, 'desk.exe'), 'guard' if not args else 'map', cfg] + list(args),
                         capture_output=True, text=True, env=e, encoding='utf-8', errors='replace')
    return out.stdout + out.stderr


def answer(cfg, root, inner):
    o = run(cfg, root, inner)
    m = re.search(r'== map .*? -> (.*)', o)
    a = m.group(1).strip().replace('\\', '/') if m else '?'
    if 'port-patched' in a:
        return 'MOD+PORT ' + a.split('/port-patched/')[0].rsplit('/', 1)[-1]
    if '/mods/' in a:
        return 'MOD ' + a.split('/mods/', 1)[1]
    if '/content-cache/' in a:
        return 'BUILT ' + a.split('/content-cache/', 1)[1]
    if a == "(the game's own)":
        return 'GAME'
    if '/files/app/' in a:
        return 'GAME(redirect) ' + a.split('/files/app/', 1)[1]
    return a


def verdicts(out):
    v = {}
    for line in out.splitlines():
        m = re.match(r'   VERDICT (fixed|refused)\s+(\S+) (\S*\|.*?) \| (.*)$', line)
        if m:
            v[m.group(3)] = (m.group(1), m.group(4))
    return v


def unpack(zpath, dest):
    with zipfile.ZipFile(zpath) as z:
        for n in z.namelist():
            if n.endswith('/'):
                continue
            out = os.path.join(dest, *n.split('/'))
            os.makedirs(os.path.dirname(out), exist_ok=True)
            open(out, 'wb').write(z.read(n))


def mod_root(folder):
    """the folder holding mod.json / resources/ (a zip may wrap the mod in one folder)"""
    for dp, dn, fn in os.walk(folder):
        if 'mod.json' in fn or 'metadata.xml' in fn or 'resources' in dn:
            return dp
    return folder


shutil.rmtree(SCRATCH, ignore_errors=True)
root = os.path.join(SCRATCH, 'files', 'app')
os.makedirs(root)

PATHS = [
    ('title iOS sheet', 'repentance-resources/data/' + MENU + 'TitleMenu_iOS.png'),
    ('title iOS sheet (ru)', 'repentance-resources/dlc3.ru/' + MENU + 'TitleMenu_iOS.png'),
    ('title sheet', 'repentance-resources/data/' + MENU + 'TitleMenu.png'),
    ('title anm2', 'repentance-resources/data/' + MENU + 'titlemenu.anm2'),
    ('main menu anm2', 'repentance-resources/data/' + MENU + 'gamemenu.anm2'),
    ('main menu sheet', 'repentance-resources/data/' + MENU + 'gamemenu.png'),
    ('options anm2', 'repentance-resources/data/' + MENU + 'optionsmenu.anm2'),
    ('options sheet', 'repentance-resources/data/' + MENU + 'optionsmenu.png'),
    ('pause anm2', 'repentance-resources/data/gfx/ui/pausescreen.anm2'),
    ('widget anm2', 'repentance-resources/data/gfx/ui/completion_widget.anm2'),
    ('widget sheet', 'repentance-resources/data/gfx/ui/completion_widget.png'),
    ('itempools.xml', 'repentance-resources/data/itempools.xml'),
    ('stringtable.sta', 'repentance-resources/data/stringtable.sta'),
]

# what 1.666 gave, by its rules, where pack 5 changes it (filled per mod below)
real = [('reflashed', os.path.join(MODS, 'ported', 'reflashed.zip')),
        ('example', os.path.join(MODS, 'example-mod.zip')),
        ('green', os.path.join(MODS, 'green-isaac.zip')),
        ('red', os.path.join(MODS, 'red-isaac.zip'))]

# synthetic classes of the deleted mods
synth = os.path.join(SCRATCH, 'synth')
tr = os.path.join(synth, 'translation', 'resources')
for rel in ('stringtable.sta', MENU + 'gamemenu.anm2', MENU + 'gamemenu.png', MENU + 'optionsmenu.anm2',
            MENU + 'optionsmenu.png', 'gfx/ui/pausescreen.anm2', MENU + 'titlemenu.png', 'items.xml', 'players.xml',
            'font/teammeatfont10_0.png'):
    src = os.path.join(GAME, *rel.split('/'))
    if os.path.exists(src):
        os.makedirs(os.path.dirname(os.path.join(tr, *rel.split('/'))), exist_ok=True)
        shutil.copyfile(src, os.path.join(tr, *rel.split('/')))
open(os.path.join(synth, 'translation', 'mod.json'), 'w').write('{"id": "ptbr-like", "name": "PT-BR-like translation"}')
q4 = os.path.join(synth, 'onlyq4', 'resources')
os.makedirs(q4)
pools = open(os.path.join(GAME, 'itempools.xml'), encoding='utf-8').read()
pools = re.sub(r'(<Pool Name="[^"]*"[^>]*>).*?(</Pool>)', r'\1<Item Id="1" Weight="1"/><Item Id="2" Weight="1"/>\2', pools, flags=re.S)
open(os.path.join(q4, 'itempools.xml'), 'w', encoding='utf-8').write(pools)
open(os.path.join(synth, 'onlyq4', 'mod.json'), 'w').write('{"id": "onlyq4-like", "name": "onlyQ4-like"}')

table = {}
for label, src in real + [('ptbr-like', os.path.join(synth, 'translation')), ('onlyq4-like', os.path.join(synth, 'onlyq4'))]:
    print('\n' + label)
    if not os.path.exists(src):
        print('  (not available: %s)' % src)
        continue
    cfg = os.path.join(SCRATCH, 'cfg-' + label)
    mods = os.path.join(cfg, 'mods')
    os.makedirs(mods)
    dest = os.path.join(mods, label)
    if src.endswith('.zip'):
        tmp = os.path.join(SCRATCH, 'unz-' + label)
        unpack(src, tmp)
        shutil.copytree(mod_root(tmp), dest)
    else:
        shutil.copytree(src, dest)
    if not os.path.exists(os.path.join(dest, 'mod.json')):
        open(os.path.join(dest, 'mod.json'), 'w').write('{"id": "%s", "name": "%s"}' % (label, label))
    open(os.path.join(mods, 'mods.txt'), 'w').write('# x\non %s\n' % label)
    shutil.rmtree(os.path.join(SCRATCH, 'files', 'content-cache'), ignore_errors=True)
    out = run(cfg, root)
    v = verdicts(out)
    for k, (kind, why) in sorted(v.items()):
        print('    %-7s %s - %s' % (kind, k, why[:140]))
    got = {name: answer(cfg, root, inner) for name, inner in PATHS}
    for name, a in got.items():
        print('    %-22s %s' % (name, a))
    table[label] = (got, v)

# ---- the differences against 1.666, each one named and expected
print('\nagainst 1.666')
if 'reflashed' in table:
    got, v = table['reflashed']
    check(got['title iOS sheet'].startswith('MOD ') and got['title iOS sheet'].lower().endswith('titlemenu.png'),
          "Reflashed, title by touch: the mod's TitleMenu.png (1.666: the game's 1x iOS sheet under the mod's 4x anm2 - shreds) [M9 fix]")
    check(got['main menu sheet'].startswith('MOD ') and got['options sheet'].startswith('MOD ')
          and got['main menu anm2'].startswith('BUILT ') and '/kfit/' in got['main menu anm2'],
          "Reflashed's 4x menu sheets: the mod's, the game's anm2 fitted to them (1.666: the mod's under 1x frames) [hotfix 1.71]")
    check(got['widget anm2'].startswith('MOD ') and got['widget sheet'].startswith('MOD '),
          "Reflashed's completion widget: its own anm2 and its 4x sheet (1.666: the same - no guard) [hotfix 1.71]")
if 'ptbr-like' in table:
    got, v = table['ptbr-like']
    check(got['main menu anm2'].startswith('MOD+PORT') and got['options anm2'].startswith('MOD+PORT') and got['pause anm2'].startswith('MOD+PORT'),
          "a translation bringing the game's menu .anm2 whole: its own + the port's patch (1.666: the game's - BY_NUMBER) [M8, by design]")
    check(got['main menu sheet'].startswith('MOD+PORT'),
          "its gamemenu.png (the game's size): the port's strap place cleared over it (1.666: the mod's under the port's anm2 - the plaque showed under the strap... in 1.666 the pack's own sheet had it cleared, the mod's did not) [strap fix]")
    check(got['title iOS sheet'].startswith('MOD '), "its TitleMenu.png stands in for the iOS sheet [M9]")
    check(got['stringtable.sta'].startswith('BUILT') or got['stringtable.sta'].startswith('MOD'),
          "its stringtable.sta: content.c's (the port's strings filled) [unchanged since pack 1]")
    check(not any(k for k, (kind, why) in v.items() if why.startswith('menu file not used')), 'no menu file refused')
if 'onlyq4-like' in table:
    got, v = table['onlyq4-like']
    check(got['itempools.xml'].startswith('MOD') or got['itempools.xml'].startswith('BUILT'),
          "onlyQ4-like: its whole itempools.xml is what the game reads (a replacement, as 1.666): " + got['itempools.xml'])
for label in ('example', 'green', 'red'):
    if label in table:
        got, v = table[label]
        menu = [k for k, (kind, why) in v.items() if 'menu' in why]
        check(not menu, '%s: no menu file or sheet touched by pack 5 (%s)' % (label, menu or '-'))

print('\n%d failures' % len(failures))
for f in failures:
    print('  - ' + f)
sys.exit(1 if failures else 0)
