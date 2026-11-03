# -*- coding: utf-8 -*-
"""THE ORDER of the mods' folders, as on the PC (task 3 of 04.10.2026, the research notes of 04.10.2026 on mods at run time, section 3),
on the real usermods.c (desk.exe and the clean desk0.exe).

Measured in isaac-ng.exe (1.7.9b: ModManager 0x870460; Repentance+ 1.9.7: 0x8f5ad0): a resource path goes to the mods
first - the enabled mods in their order, in each resources-dlc3/<path> then resources/<path> - and the game's own
sources (its language sets among them) only when no mod has the file. The port's language folders take the place the
PC's own order gives a language set, inside the mod: resources-dlc3.<lang>, resources-dlc3, resources.<lang>,
resources; the mods' order stands over all of it.

    bash tools/umdesk/build.sh && bash tools/umdesk/build.sh desk0.exe 0 && py -3 tools/umdesk/test_order.py
"""
import os
import re
import shutil
import subprocess
import sys
import tempfile
import zipfile

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
TOP = os.path.join(tempfile.gettempdir(), 'umdesk-order')
WORKSHOP = r'Z:/Games/Steam/steamapps/workshop/content/250900'
failures = []


def check(cond, what, detail=''):
    print(('  ok    ' if cond else '  FAIL  ') + what + ('' if cond or not detail else '\n        ' + str(detail)[:900]))
    if not cond:
        failures.append(what)


def put(path, data=b'x'):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    open(path, 'wb').write(data)


def config(name, mods):
    """mods: [(id, {relative path in the mod: bytes})], the first the strongest"""
    cfg = os.path.join(TOP, name)
    shutil.rmtree(cfg, ignore_errors=True)
    for mid, files in mods:
        put(os.path.join(cfg, 'mods', mid, 'mod.json'), ('{"id": "%s", "name": "%s"}' % (mid, mid)).encode())
        for rel, data in files.items():
            put(os.path.join(cfg, 'mods', mid, *rel.split('/')), data)
    put(os.path.join(cfg, 'mods', 'mods.txt'), ('# order\n' + ''.join('on %s\n' % m for m, _ in mods)).encode())
    return cfg


def ask(desk, cfg, *paths):
    """the game asks for these in turn: what each is answered with (mod/folder, or GAME)"""
    e = {k: v for k, v in os.environ.items() if not k.startswith('DESK_')}
    e['DESK_ROOT'] = os.path.join(TOP, 'root')
    out = subprocess.run([desk, 'map', cfg] + list(paths), capture_output=True, text=True, env=e, encoding='utf-8',
                         errors='replace').stdout
    got = []
    for m in re.finditer(r'== map (.*?) -> (.*)', out):
        a = m.group(2).strip().replace('\\', '/')
        got.append('GAME' if a == "(the game's own)" else a.split('/mods/', 1)[1] if '/mods/' in a else a)
    return got, out


WAV = b'RIFF....WAVEfmt '
for desk in (os.path.join(HERE, 'desk.exe'), os.path.join(HERE, 'desk0.exe')):
    tag = os.path.basename(desk)
    print('\n== %s' % tag)

    print('1. a mod\'s file beats the game\'s language set (the PC: the mods are asked before any source of the game)')
    slime = os.path.join(TOP, 'slime-src')
    if not os.path.exists(os.path.join(slime, 'mod.json')):
        z = os.path.join(TOP, 'slime.zip')
        os.makedirs(TOP, exist_ok=True)
        subprocess.run([sys.executable, os.path.join(ROOT, 'tools', 'modport.py'), os.path.join(WORKSHOP, '3783134747'),
                        '--game', os.path.join(ROOT, 'work', 'app', 'repentance-resources'), '--out', z], capture_output=True)
        with zipfile.ZipFile(z) as zz:
            zz.extractall(slime)
    cfg = os.path.join(TOP, 'slime')
    shutil.rmtree(cfg, ignore_errors=True)
    shutil.copytree(slime, os.path.join(cfg, 'mods', 'slime'))
    put(os.path.join(cfg, 'mods', 'mods.txt'), b'# x\non slime\n')
    have_ru = os.path.exists(os.path.join(ROOT, 'work', 'app', 'repentance-resources', 'dlc3.ru', 'gfx', 'backdrop', 'controls.png'))
    check(have_ru, 'the game has its own Russian controls.png (dlc3.ru)')
    got, out = ask(desk, cfg, 'repentance-resources/dlc3.ru/gfx/backdrop/controls.png',
                   'repentance-resources/data/gfx/backdrop/controls.png')
    check(got == ['slime/resources/gfx/backdrop/controls.png'] * 2,
          'Slime Rancher\'s resources/gfx/backdrop/controls.png answers the Russian set and data alike', got)

    print('2. a stronger mod\'s file beats a weaker mod\'s language folder')
    cfg = config('cross', [('a', {'resources/sfx/a.wav': WAV}), ('b', {'resources-dlc3.ru/sfx/a.wav': WAV})])
    got, _ = ask(desk, cfg, 'repentance-resources/dlc3.ru/sfx/a.wav')
    check(got == ['a/resources/sfx/a.wav'], 'A over B: dlc3.ru asks get A\'s resources/ (before: B\'s language folder)', got)
    cfg = config('cross2', [('b', {'resources-dlc3.ru/sfx/a.wav': WAV}), ('a', {'resources/sfx/a.wav': WAV})])
    got, _ = ask(desk, cfg, 'repentance-resources/dlc3.ru/sfx/a.wav')
    got2, _ = ask(desk, cfg, 'repentance-resources/data/sfx/a.wav')
    check(got + got2 == ['b/resources-dlc3.ru/sfx/a.wav', 'a/resources/sfx/a.wav'],
          'B over A: B\'s Russian file in the Russian set; English (a start with no language set) gets A\'s', got + got2)

    print('3. inside one mod: resources-dlc3.<lang>, resources-dlc3, resources.<lang>, resources')
    cfg = config('inside', [('m', {
        'resources/sfx/b.wav': WAV, 'resources.ru/sfx/b.wav': WAV,
        'resources-dlc3/sfx/c.wav': WAV, 'resources.ru/sfx/c.wav': WAV,
        'resources-dlc3/sfx/d.wav': WAV, 'resources-dlc3.ru/sfx/d.wav': WAV, 'resources/sfx/d.wav': WAV})])
    got, _ = ask(desk, cfg, 'repentance-resources/dlc3.ru/sfx/b.wav', 'repentance-resources/dlc3.ru/sfx/c.wav',
                 'repentance-resources/dlc3.ru/sfx/d.wav')
    check(got == ['m/resources.ru/sfx/b.wav', 'm/resources-dlc3/sfx/c.wav', 'm/resources-dlc3.ru/sfx/d.wav'],
          'b: resources.ru over resources; c: resources-dlc3 over resources.ru; d: resources-dlc3.ru first', got)
    got, _ = ask(desk, cfg, 'repentance-resources/data/sfx/d.wav', 'repentance-resources/data/sfx/b.wav')
    check(got == ['m/resources-dlc3/sfx/d.wav', 'm/resources/sfx/b.wav'],
          'English (no language set mounted): the folders for all sets, resources-dlc3 first', got)

    print('4. the game\'s language from its own sets: a file the game has no Russian copy of')
    got, out = ask(desk, cfg, 'repentance-resources/dlc3.ru', 'repentance-resources/data/sfx/d.wav',
                   'repentance-resources/data/sfx/b.wav')
    check(got[1:] == ['m/resources-dlc3.ru/sfx/d.wav', 'm/resources.ru/sfx/b.wav'],
          'after the engine lists dlc3.ru: data asks get the mod\'s Russian folders', got)
    if tag == 'desk.exe':
        check('order: the game\'s language set is ru' in out, 'the report says which language set it took', out[-400:])
    got, _ = ask(desk, cfg, 'repentance-resources/./', 'repentance-resources/data/sfx/d.wav')
    check(got[1:] == ['m/resources-dlc3/sfx/d.wav'], '"." is no language set', got)

    print('5. the language of a weaker mod does not reach past a stronger mod')
    cfg = config('weak-lang', [('a', {'resources/sfx/e.wav': WAV}), ('b', {'resources-dlc3.ru/sfx/e.wav': WAV})])
    got, _ = ask(desk, cfg, 'repentance-resources/dlc3.ru', 'repentance-resources/data/sfx/e.wav')
    check(got[1:] == ['a/resources/sfx/e.wav'], 'Russian, data asked: A\'s file for all sets', got)

print('\n%d failures' % len(failures))
for f in failures:
    print('  - ' + f)
sys.exit(1 if failures else 0)
