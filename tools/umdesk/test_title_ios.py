# -*- coding: utf-8 -*-
"""The title's iOS sheet (plan 30.09, M9): what TitleMenu_iOS.png answers when mods touch the title.

The game asks gfx/ui/main menu/TitleMenu_iOS.png when the last device is touch (0x10dead), TitleMenu.png
otherwise (0x35dffc, 0x59745c). The rule in usermods.c (title_ios_rule): a mod's own iOS sheet wins; else
a mod's TitleMenu.png stands in; else, a mod's TitleMenu.anm2 alone, the game's TitleMenu.png (the same
language set's, else data's); no mod touching the title - the game's own iOS sheet.

    bash tools/umdesk/build.sh && bash tools/umdesk/build.sh desk0.exe 0 && py -3 tools/umdesk/test_title_ios.py
"""
import os
import shutil
import struct
import subprocess
import sys
import zipfile
import zlib

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
GAME = os.path.join(ROOT, 'work', 'app', 'repentance-resources')
REFLASHED = os.path.join(ROOT, 'other stuff', 'usermods', 'ported', 'reflashed.zip')
SCRATCH = sys.argv[1] if len(sys.argv) > 1 else os.path.join(os.environ.get('TEMP', '.'), 'umdesk-title-ios')
MENU = 'gfx/ui/main menu/'
failures = []


def check(cond, what):
    print(('  ok    ' if cond else '  FAIL  ') + what)
    if not cond:
        failures.append(what)


def png(w, h, truncate=0):
    raw = b''.join(b'\x00' + b'\x80' * (w * 4) for _ in range(h))

    def chunk(t, d):
        return struct.pack('>I', len(d)) + t + d + struct.pack('>I', zlib.crc32(t + d) & 0xffffffff)
    data = b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', w, h, 8, 6, 0, 0, 0)) + chunk(b'IDAT', zlib.compress(raw)) + chunk(b'IEND', b'')
    return data[:-truncate] if truncate else data


def ask(desk, cfg, root, cmd, inner):
    e = {k: v for k, v in os.environ.items() if not k.startswith('DESK_')}
    e['DESK_ROOT'] = root
    out = subprocess.run([os.path.join(HERE, desk), cmd, cfg, inner], capture_output=True, text=True, env=e,
                         encoding='utf-8', errors='replace').stdout
    for line in out.splitlines():
        if line.startswith('== map ') or line.startswith('== peek '):
            got = line.split(' -> ', 1)[1].strip().replace('\\', '/')
            return got.rsplit(' (guard ran:', 1)[0] if line.startswith('== peek ') else got
    return out[-300:]


def config(label, files, extra_mods=()):
    cfg = os.path.join(SCRATCH, 'cfg-' + label)
    mods = os.path.join(cfg, 'mods')
    os.makedirs(os.path.join(mods, 'm'))
    open(os.path.join(mods, 'm', 'mod.json'), 'w').write('{"id": "m", "name": "M"}')
    for rel, data in files:
        p = os.path.join(mods, 'm', *rel.split('/'))
        os.makedirs(os.path.dirname(p), exist_ok=True)
        open(p, 'wb').write(data)
    open(os.path.join(mods, 'mods.txt'), 'w').write('# x\non m\n')
    return cfg


def game(rel, s='data'):
    return open(os.path.join(GAME, s, *rel.split('/')), 'rb').read()


shutil.rmtree(SCRATCH, ignore_errors=True)
root = os.path.join(SCRATCH, 'files', 'app')
os.makedirs(root)
IOS = 'repentance-resources/%s/' + MENU + 'TitleMenu_iOS.png'
GAMEPNG = 'repentance-resources/%s/' + MENU + 'titlemenu.png'

with zipfile.ZipFile(REFLASHED) as z:
    ref = [(n, z.read(n)) for n in z.namelist() if n.lower().startswith('resources/' + MENU + 'titlemenu')]
check(any(n.endswith('titlemenu.png') for n, _ in ref) and not any('_ios' in n.lower() for n, _ in ref),
      'Reflashed (ported) brings titlemenu.anm2 and .png, no iOS sheet: %s' % [n.rsplit('/', 1)[1] for n, _ in ref])

cases = {
    'reflashed': ref,
    'anm2-only': [('resources/' + MENU + 'titlemenu.anm2', game(MENU + 'titlemenu.anm2'))],
    'own-ios': [('resources/' + MENU + 'titlemenu.anm2', game(MENU + 'titlemenu.anm2')),
                ('resources/' + MENU + 'titlemenu.png', png(480, 540)),
                ('resources/' + MENU + 'titlemenu_ios.png', png(480, 540))],
    'none': [('resources/gfx/ui/other.png', png(4, 4))],
    'png-refused': [('resources/' + MENU + 'titlemenu.anm2', game(MENU + 'titlemenu.anm2')),
                    ('resources/' + MENU + 'titlemenu.png', png(480, 540, truncate=20))],
    'ru-only': [('resources-dlc3.ru/' + MENU + 'titlemenu.png', png(480, 540))],
}
for desk in ('desk.exe', 'desk0.exe'):
    for label, files in cases.items():
        print('\n[%s] %s' % (desk, label))
        cfg = os.path.join(SCRATCH, 'cfg-' + label)
        if not os.path.isdir(cfg):
            config(label, files)
        shutil.rmtree(os.path.join(SCRATCH, 'files', 'content-cache'), ignore_errors=True)
        got = {s: ask(desk, cfg, root, 'map', IOS % s) for s in ('data', 'dlc3.ru', 'dlc3.jp', 'dlc3.kr')}
        peek = ask(desk, cfg, root, 'peek', IOS % 'data')
        for s, g in got.items():
            print('        %-8s -> %s' % (s, g[-90:]))
        mods = os.path.join(cfg, 'mods', 'm').replace('\\', '/')
        if label == 'reflashed':
            check(all(g.lower().endswith('resources/' + MENU + 'titlemenu.png') and mods in g for g in got.values()),
                  "the mod's TitleMenu.png for the iOS sheet in every set")
            check(peek.lower().endswith('titlemenu.png'), 'peek answers the same: ' + peek[-60:])
        elif label == 'anm2-only':
            for s in got:
                want = GAMEPNG % s if os.path.exists(os.path.join(GAME, s, *(MENU + 'titlemenu.png').split('/'))) else GAMEPNG % 'data'
                check(got[s].lower().endswith(want.lower()), "%s: the game's TitleMenu.png (%s)" % (s, want))
            check(peek == "(the game's own)", 'peek: no mod file (%s)' % peek)
        elif label == 'own-ios':
            check(all(g.lower().endswith('titlemenu_ios.png') and mods in g for g in got.values()), "the mod's own iOS sheet")
        elif label == 'none':
            check(all(g == "(the game's own)" for g in got.values()), "no mod touches the title: the game's own iOS sheet")
        elif label == 'png-refused':
            check(all(g.lower().endswith('titlemenu.png') and mods not in g for g in got.values()),
                  "the mod's TitleMenu.png refused (libpng 1.0.2), its anm2 alone: the game's TitleMenu.png")
        elif label == 'ru-only':
            check(mods in got['dlc3.ru'] and got['dlc3.ru'].lower().endswith('titlemenu.png'), "ru: the mod's ru TitleMenu.png")
            check(got['data'] == "(the game's own)", "data: the game's own iOS sheet (the mod has no title for it)")

# ---- M9, the completion widget: a mod's own anm2 - the reminder leaves it alone unless the mod brings its own
# marked sheets (gfx/port_completion_widget.png and _pause.png); which it is, is in <id>.check.txt
with zipfile.ZipFile(REFLASHED) as z:
    widget = [(n, z.read(n)) for n in z.namelist() if 'completion_widget' in n.lower()]
# the mod of the hotfix (05.10.2026): Cooler Completion Marks [REP+], dropped into mods/ by hand - its own anm2 and
# two 393x500 sheets cut for it (the game's are 256x512); 1.7 handed the game's anm2 and sheet and the mod's pause sheet
COOLER = os.path.join(ROOT, 'reports', '2026-10-05', 'hotfix171', 'cooler_completion_marks_rep+_3361519802', 'cooler_completion_marks_rep+_3361519802')
cooler = []
for f in ('completion_widget.anm2', 'completion_widget.png', 'completion_widget_pause.png'):
    p_ = os.path.join(COOLER, 'resources', 'gfx', 'ui', f)
    if os.path.exists(p_):
        cooler.append(('resources/gfx/ui/' + f, open(p_, 'rb').read()))
for label, files in (('widget-reflashed', widget),
                     ('widget-cooler', cooler),
                     ('widget-own-sheets', widget + [('resources/gfx/port_completion_widget.png', png(4, 4)),
                                                     ('resources/gfx/port_completion_widget_pause.png', png(4, 4))]),
                     ('widget-one-sheet', widget + [('resources/gfx/port_completion_widget.png', png(4, 4))])):
    for desk in ('desk.exe', 'desk0.exe'):
        print('\n[%s] %s' % (desk, label))
        cfg = os.path.join(SCRATCH, 'cfg-' + label)
        if not os.path.isdir(cfg):
            config(label, files)
        note_p = os.path.join(cfg, 'mods', 'm.check.txt')
        if os.path.exists(note_p):
            os.remove(note_p)
        shutil.rmtree(os.path.join(SCRATCH, 'files', 'content-cache'), ignore_errors=True)
        e = {k: v for k, v in os.environ.items() if not k.startswith('DESK_')}
        e['DESK_ROOT'] = root
        subprocess.run([os.path.join(HERE, desk), 'guard', cfg], capture_output=True, env=e)
        note = open(note_p, encoding='utf-8').read() if os.path.exists(note_p) else ''
        said = 'Completion Mark Reminder (TWEAKS) leaves its widget alone' in note
        mapped = ask(desk, cfg, root, 'map', 'gfx/port_completion_widget.png')
        # hotfix 1.71 (05.10.2026): 1.666 had no guard at all - a mod's completion_widget.anm2 was the mod's, and
        # 1.7's BY_NUMBER refusal of it turned Cooler Completion Marks off. The mod's widget is the mod's again,
        # its sheets with it (cut by its own frames, whatever their size); the reminder leaves it alone unless the
        # mod brings its own marked sheets for it (WIDGET_NOTE says which)
        refused = [l for l in note.splitlines() if 'NOT USED' in l and 'completion_widget' in l]
        check(not refused, "the mod's completion_widget.anm2 and its sheets: the mod's (as 1.666): %s" % refused)
        mods = os.path.join(cfg, 'mods', 'm').replace('\\', '/')
        mapped_anm = ask(desk, cfg, root, 'map', 'repentance-resources/data/gfx/ui/completion_widget.anm2')
        mapped_png = ask(desk, cfg, root, 'map', 'repentance-resources/data/gfx/ui/completion_widget.png')
        mapped_pause = ask(desk, cfg, root, 'map', 'repentance-resources/data/gfx/ui/completion_widget_pause.png')
        check(mods in mapped_anm or 'content-cache' in mapped_anm, "the game is handed the mod's widget: " + mapped_anm[-70:])
        check(mods in mapped_png and mods in mapped_pause, "and its two sheets: %s, %s" % (mapped_png[-50:], mapped_pause[-50:]))
        if label == 'widget-own-sheets':
            check(not said, 'its own marked sheets: no "leaves its widget alone" line')
        else:
            check(said, 'no marked sheets of its own: the "leaves its widget alone" line')
        if label == 'widget-own-sheets':
            check(mapped.endswith('resources/gfx/port_completion_widget.png'), "the port's sheet answered by the mod's: " + mapped[-70:])

# the widget's sheets with no anm2 of the mod's to cut them (hotfix 1.71: never refused for their size): 393x500 is
# no whole multiple - the mod's pictures as they are, the game's frames (1.666, and the PC); a whole x2 - the game's
# anm2 handed fitted to it (THE GAME'S FRAMES FITTED)
from PIL import Image as _I
import io as _io
def _png2(w, h):
    b = _io.BytesIO()
    _I.new('RGBA', (w, h), (200, 30, 30, 255)).save(b, 'PNG')
    return b.getvalue()
for label, files in (('widget-sheets-alone', [x for x in cooler if x[0].endswith('.png')]),
                     ('widget-sheets-x2', [('resources/gfx/ui/completion_widget.png', _png2(512, 1024)),
                                           ('resources/gfx/ui/completion_widget_pause.png', _png2(512, 1024))])):
    for desk in ('desk.exe', 'desk0.exe'):
        print('\n[%s] %s' % (desk, label))
        cfg = os.path.join(SCRATCH, 'cfg-' + label)
        if not os.path.isdir(cfg):
            config(label, files)
        shutil.rmtree(os.path.join(SCRATCH, 'files', 'content-cache'), ignore_errors=True)
        a = ask(desk, cfg, root, 'map', 'repentance-resources/data/gfx/ui/completion_widget.png')
        b = ask(desk, cfg, root, 'map', 'repentance-resources/data/gfx/ui/completion_widget_pause.png')
        w = ask(desk, cfg, root, 'map', 'repentance-resources/data/gfx/ui/completion_widget.anm2')
        check('/mods/m/' in a.replace(chr(92), '/') and '/mods/m/' in b.replace(chr(92), '/'),
              label + ": the mod's sheets, never refused for their size (%s, %s)" % (a[-40:], b[-40:]))
        if label == 'widget-sheets-alone':
            check(w == "(the game's own)", "393x500 is no whole multiple: the game's widget as it is (%s)" % w[-50:])
        else:
            text = open(w, encoding='utf-8').read() if os.path.exists(w) else ''
            check('/kfit/' in w.replace(chr(92), '/') and 'XScale="50"' in text, "a whole x2: the game's widget fitted (%s)" % w[-60:])

print('\n%d failures' % len(failures))
for f in failures:
    print('  - ' + f)
sys.exit(1 if failures else 0)
