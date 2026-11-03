# -*- coding: utf-8 -*-
"""A mod's menu .anm2, all or nothing (02.10.2026, the owner's concern for the mods people already made).

The port lays its additions (the MODS row, the TWEAKS/GUIDE straps, its Options rows, the pause strap,
the stick sizes) over gamemenu / optionsmenu / pausescreen / ios_buttons .anm2 by name - a mod's own copy
included. Where the patch cannot place every addition on the mod's file, the game's file with the patch is
served instead, and the reason goes into <id>.check.txt and onto the MODS page (the count of files the
game's copy stands in for). The real usermods.c on the desk with portpatch.c's real pp_apply_whole and the
real port_patch/ of the assets (DESK_REAL_PATCH=1).

    bash tools/umdesk/build.sh && bash tools/umdesk/build.sh desk0.exe 0 && py -3 tools/umdesk/test_menu_whole.py
"""
import os
import re
import shutil
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
GAME = os.path.join(ROOT, 'work', 'app', 'repentance-resources')
PC = os.path.join(ROOT, 'work', 'pc-resources')
SCRATCH = sys.argv[1] if len(sys.argv) > 1 else os.path.join(os.environ.get('TEMP', '.'), 'umdesk-menu-whole')
MENU = 'gfx/ui/main menu/'
failures = []


def check(cond, what):
    print(('  ok    ' if cond else '  FAIL  ') + what)
    if not cond:
        failures.append(what)


def run(desk, cfg, root, *args):
    e = {k: v for k, v in os.environ.items() if not k.startswith('DESK_')}
    e['DESK_ROOT'] = root
    e['DESK_REAL_PATCH'] = '1'
    out = subprocess.run([os.path.join(HERE, desk), 'guard' if not args else 'map', cfg] + list(args),
                         capture_output=True, text=True, env=e, encoding='utf-8', errors='replace')
    return out.stdout + out.stderr


def verdicts(out):
    v = {}
    for line in out.splitlines():
        m = re.match(r'   VERDICT (fixed|refused)\s+(\S+) (\S*\|.*?) \| (.*)$', line)
        if m:
            v[(m.group(2), m.group(3))] = (m.group(1), m.group(4))
    return v


def game(rel, s='data'):
    return open(os.path.join(GAME, s, *rel.split('/')), encoding='utf-8').read()


def put(mods, ident, rel, text, folder='resources'):
    p = os.path.join(mods, ident, folder, *rel.split('/'))
    os.makedirs(os.path.dirname(p), exist_ok=True)
    open(p, 'w', encoding='utf-8', newline='').write(text)


def rename_layer(text, old, new):
    t2 = text.replace('Name="%s"' % old, 'Name="%s"' % new, 1)
    assert t2 != text, old
    return t2


def insert_layer_first(text):
    """a decoration layer put FIRST: every game layer's number moves up by one (the game's code draws by number)"""
    ids = [int(x) for x in re.findall(r'<Layer [^>]*Id="(\d+)"', text)]
    def bump(m):
        return m.group(1) + str(int(m.group(2)) + 1) + '"'
    text = re.sub(r'(<Layer [^>]*?Id=")(\d+)"', bump, text)
    text = re.sub(r'(<LayerAnimation [^>]*?LayerId=")(\d+)"', bump, text)
    i = text.index('<Layer ')
    return text[:i] + '<Layer Name="ModDecor" Id="0" SpritesheetId="0"/>' + text[i:]


def append_layer(text):
    """a decoration layer at the end: nothing of the game's moves"""
    n = max(int(x) for x in re.findall(r'<Layer [^>]*Id="(\d+)"', text)) + 1
    i = text.index('</Layers>')
    return text[:i] + '<Layer Name="ModDecor" Id="%d" SpritesheetId="0"/>' % n + text[i:]


def config(label, mods_spec):
    """mods_spec: [(ident, [(rel, text, folder)])], the strongest first"""
    cfg = os.path.join(SCRATCH, 'cfg-' + label)
    mods = os.path.join(cfg, 'mods')
    os.makedirs(mods)
    for ident, files in mods_spec:
        os.makedirs(os.path.join(mods, ident), exist_ok=True)
        open(os.path.join(mods, ident, 'mod.json'), 'w').write('{"id": "%s", "name": "%s"}' % (ident, ident))
        for rel, text, folder in files:
            put(mods, ident, rel, text, folder)
    open(os.path.join(mods, 'mods.txt'), 'w').write('# x\n' + ''.join('on %s\n' % i for i, _ in mods_spec))
    return cfg, mods


shutil.rmtree(SCRATCH, ignore_errors=True)
root = os.path.join(SCRATCH, 'files', 'app')
os.makedirs(root)

GM, OM, PS = MENU + 'gamemenu.anm2', MENU + 'optionsmenu.anm2', 'gfx/ui/pausescreen.anm2'
cases = [
    ('faithful', [('faithful', [(GM, game(GM), 'resources'), (OM, game(OM), 'resources'), (PS, game(PS), 'resources'),
                                (GM, game(GM, 'dlc3.ru'), 'resources-dlc3.ru')])]),
    ('renamed', [('renamed', [(GM, rename_layer(game(GM), 'Cursor', 'Pointer'), 'resources'),
                              (OM, rename_layer(game(OM), 'OnOff', 'Toggle'), 'resources')])]),
    ('appended', [('appended', [(GM, append_layer(game(GM)), 'resources')])]),
    ('shifted', [('shifted', [(GM, insert_layer_first(game(GM)), 'resources')])]),
    ('pc', [('pc', [(GM, open(os.path.join(PC, 'resources', *GM.split('/')), encoding='utf-8').read(), 'resources'),
                    (OM, open(os.path.join(PC, 'resources', *OM.split('/')), encoding='utf-8').read(), 'resources')])]),
    # a language copy that does not fit, under a faithful copy for all sets: only the language copy is refused
    ('lang', [('lang', [(GM, game(GM), 'resources'),
                        (GM, rename_layer(game(GM, 'dlc3.ru'), 'DailyPaper', 'Paper'), 'resources-dlc3.ru')])]),
    # two mods: the stronger one's file does not fit - the game's is served, not the weaker mod's
    ('two', [('strong', [(GM, rename_layer(game(GM), 'Cursor', 'Pointer'), 'resources')]),
             ('weak', [(GM, game(GM), 'resources')])]),
]

for label, spec in cases:
    print('\n' + label)
    cfg, mods = config(label, spec)
    shutil.rmtree(os.path.join(SCRATCH, 'files', 'content-cache'), ignore_errors=True)
    for desk in ('desk.exe', 'desk0.exe'):
        out = run(desk, cfg, root)
        v = verdicts(out)
        menu = {k: x for k, x in v.items() if x[1].startswith('menu file not used')}
        for (o, k), (kind, why) in sorted(menu.items()):
            print('    %s %-8s %s %s - %s' % (desk, kind, o, k, why[:170]))
        gm_data = run(desk, cfg, root, 'repentance-resources/data/' + GM).split('== map')[1].splitlines()[0]
        gm_ru = run(desk, cfg, root, 'repentance-resources/dlc3.ru/' + GM).split('== map')[1].splitlines()[0]
        om_de = run(desk, cfg, root, 'repentance-resources/dlc3.de/' + OM).split('== map')[1].splitlines()[0]
        note_p = os.path.join(mods, spec[0][0] + '.check.txt')
        note = open(note_p, encoding='utf-8').read() if os.path.exists(note_p) else ''
        tag = '[%s] ' % desk
        if label == 'faithful':
            check(not menu, tag + 'a faithful copy: nothing refused')
            check('port-patched' in gm_data and 'port-patched' in gm_ru and 'port-patched' in om_de,
                  tag + "the mod's file with the port's patch over it, every set: " + gm_data.strip())
        elif label == 'renamed':
            check(len(menu) == 2, tag + 'both renamed files refused: %d' % len(menu))
            check(any('layer Cursor' in w for _, w in menu.values()) and any('layer OnOff' in w for _, w in menu.values()),
                  tag + 'the reasons name the missing layers')
            check('(the game\'s own)' in gm_data and '(the game\'s own)' in om_de, tag + "the game's file served: " + gm_data.strip())
            check('menu file not used' in note and 'Cursor' in note, tag + 'renamed.check.txt says so')
            check(re.search(r'renamed: guard completed \d+, the game\'s 2', out) is not None, tag + "the page's count: the game's 2")
        elif label == 'appended':
            check(not menu and 'port-patched' in gm_data, tag + 'a layer added at the end: the mod\'s file + patch')
        elif label == 'shifted':
            check(len(menu) == 1 and 'number' in list(menu.values())[0][1], tag + 'game layers renumbered: refused (%s)'
                  % (list(menu.values())[0][1][:90] if menu else '-'))
            check('(the game\'s own)' in gm_data, tag + "the game's file served")
        elif label == 'pc':
            # the PC's gamemenu.anm2 has no touch nulls (Area1..6, AreaDailyRun): THE GUARD completes them from the
            # game's file first, and then the patch lands whole - the mod's menu is taken (1.666: always the game's,
            # gamemenu.anm2 was read by number then)
            check(v.get(('pc', '|' + GM), ('',))[0] == 'fixed' and not menu,
                  tag + "the PC's gamemenu.anm2: completed with the game's nulls, then whole: %s" % (v.get(('pc', '|' + GM)),))
            check('port-patched' in gm_data, tag + "and handed out with the port's patch: " + gm_data.strip())
            check('port-patched' in om_de, tag + "the PC's optionsmenu.anm2 fits: the mod's + patch")
        elif label == 'lang':
            check(len(menu) == 1 and list(menu)[0][1].startswith('dlc3.ru|'), tag + 'only the ru copy refused: %s' % list(menu))
            check('port-patched' in gm_data, tag + 'data: the mod\'s copy for all sets')
            check('port-patched' in gm_ru and '/lang/' in gm_ru.replace('\\', '/'),
                  tag + 'ru: the copy for all sets stands in for the refused ru copy: ' + gm_ru.strip())
        elif label == 'two':
            check(('strong', '|' + GM) in menu or any(o == 'strong' for o, _ in menu), tag + "the stronger mod's file refused")
            # the index keeps the strongest mod's file of a path only (as for a refused picture): the game's is next
            check("(the game's own)" in gm_data, tag + "the game's file with the port's patch: " + gm_data.strip())
        # the second start answers from the cache: the same verdicts
        out2 = run(desk, cfg, root)
        check(verdicts(out2) == v, tag + 'the second start: the same verdicts')

# ---- the menu's SHEETS (02.10.2026): a recolour of the game's gamemenu.png gets the port's retouch (the
# strap's place cleared, pixels); Reflashed's 4x gamemenu.png/optionsmenu.png with the game's anm2: the mod's
# pictures (1.666 served them too), and the game's anm2 - with the port's patch - handed FITTED to them (hotfix
# 1.71, THE GAME'S FRAMES FITTED): its frames on those sheets at x4 crops and 25% scale, the port's straps as they are
import io
import zipfile
import numpy as np
from PIL import Image


def rgba(b):
    return np.array(Image.open(io.BytesIO(b)).convert('RGBA')).astype(int)


van = open(os.path.join(GAME, 'data', *(MENU + 'gamemenu.png').split('/')), 'rb').read()
v = rgba(van)
rec = v.copy()
rec[:, :, 0], rec[:, :, 2] = v[:, :, 2], v[:, :, 0]
buf = io.BytesIO()
Image.fromarray(rec.astype(np.uint8), 'RGBA').save(buf, 'PNG')
with zipfile.ZipFile(os.path.join(ROOT, 'other stuff', 'usermods', 'ported', 'reflashed.zip')) as z:
    ref = {n: z.read(n) for n in z.namelist() if n.startswith('resources/' + MENU) and n.endswith('.png')}
over = open(os.path.join(ROOT, 'port', 'app', 'src', 'main', 'assets', 'port_patch', 'repentance-resources', 'data', 'gfx', 'ui', 'main_menu', 'gamemenu.png'), 'rb').read()
mask = (rgba(over) != v).any(axis=2)
for label, files in (('recolour', [('resources/' + MENU + 'gamemenu.png', buf.getvalue())]),
                     ('reflashed-sheets', list(ref.items()))):
    print('\n' + label)
    cfg = os.path.join(SCRATCH, 'cfg-' + label)
    mods = os.path.join(cfg, 'mods')
    os.makedirs(os.path.join(mods, 'm'))
    open(os.path.join(mods, 'm', 'mod.json'), 'w').write('{"id": "m", "name": "M"}')
    for rel, data in files:
        p = os.path.join(mods, 'm', *rel.split('/'))
        os.makedirs(os.path.dirname(p), exist_ok=True)
        open(p, 'wb').write(data)
    open(os.path.join(mods, 'mods.txt'), 'w').write('# x\non m\n')
    for desk in ('desk.exe', 'desk0.exe'):
        shutil.rmtree(os.path.join(SCRATCH, 'files', 'content-cache'), ignore_errors=True)
        tag = '[%s] ' % desk
        got = {s: run(desk, cfg, root, 'repentance-resources/%s/%sgamemenu.png' % (s, MENU)).split('== map')[1].splitlines()[0]
               .split(' -> ', 1)[1].strip() for s in ('data', 'dlc3.de', 'dlc3.ru')}
        if label == 'recolour':
            ok = True
            for s, path in got.items():
                if 'port-patched' not in path:
                    ok = False
                    continue
                g = rgba(open(path, 'rb').read())
                ok &= np.array_equal(g[~mask], rec[~mask]) and np.array_equal(g[mask], rgba(over)[mask])
            check(ok, tag + "a recolour of gamemenu.png: the mod's picture, the strap's place cleared, in data/de/ru")
        else:
            out = run(desk, cfg, root)
            vv = verdicts(out)
            check(all('/mods/m/' in p.replace(chr(92), '/') for p in got.values()),
                  tag + "Reflashed's 4x gamemenu.png: the mod's picture (%s)" % got['data'][-40:])
            check(('m', '|' + MENU + 'gamemenu.png') not in vv and ('m', '|' + MENU + 'optionsmenu.png') not in vv,
                  tag + 'no sheet refused for its size')
            for s in ('data', 'dlc3.ru'):
                a = run(desk, cfg, root, 'repentance-resources/%s/%sgamemenu.anm2' % (s, MENU)).split('== map')[1].splitlines()[0] \
                    .split(' -> ', 1)[1].strip()
                text = open(a, encoding='utf-8').read() if os.path.exists(a) else ''
                check('/kfit/' in a.replace(chr(92), '/') and 'XScale="25"' in text and 'PortStrap' in text,
                      tag + '%s gamemenu.anm2: the game\'s with the port\'s patch, fitted to the x4 sheets (%s)' % (s, a[-60:]))

print('\n%d failures' % len(failures))
for f in failures:
    print('  - ' + f)
sys.exit(1 if failures else 0)
