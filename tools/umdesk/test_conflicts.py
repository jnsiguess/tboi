# -*- coding: utf-8 -*-
"""THE MODS OVER EACH OTHER (task 4 of 04.10.2026, the research notes of 04.10.2026 on mods at run time, section 4): the notes on the
MODS page and in check.txt, on the real usermods.c (desk.exe) and the real tweaks.c (tools/tweaksdesk).

Only two kinds of note, both precise:
  (a) mod A overrides N files of mod B (by kind) - on both pages and in both check.txt;
  (b) a breakage: the .anm2 the game gets is one mod's, a sheet it cuts comes from another mod or the game, and the
      sheet is not the size its frames were cut for (the same picture scaled, or frames falling off its edge).

    bash tools/umdesk/build.sh && bash tools/tweaksdesk/build.sh && py -3 tools/umdesk/test_conflicts.py

The real mods are converted from the workshop folders by tools/modport.py into %TEMP%/umdesk-conflicts/conv (once).
"""
import os
import re
import shutil
import struct
import subprocess
import sys
import tempfile
import zipfile
import zlib

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
TOP = os.path.join(tempfile.gettempdir(), 'umdesk-conflicts')
WORKSHOP = r'Z:/Games/Steam/steamapps/workshop/content/250900'
PACK = os.path.join(ROOT, 'work', 'app', 'repentance-resources', '')
DESK = os.path.join(HERE, 'desk.exe')
TWEAKS = os.path.join(ROOT, 'tools', 'tweaksdesk', 'tweaksdesk.exe')
REAL = {'sammax': '3794486410', 'reflashed': '2934904934', 'cmr': '2603224299'}
failures = []


def check(cond, what, detail=''):
    print(('  ok    ' if cond else '  FAIL  ') + what + ('' if cond or not detail else '\n        ' + str(detail)[:1200]))
    if not cond:
        failures.append(what)


def put(path, data):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    open(path, 'wb').write(data if isinstance(data, bytes) else data.encode('utf-8'))


def png(w, h):
    """a real RGBA picture of w x h"""
    def chunk(t, d):
        return struct.pack('>I', len(d)) + t + d + struct.pack('>I', zlib.crc32(t + d) & 0xffffffff)
    raw = b''.join(b'\0' + b'\x80\x40\x20\xff' * w for _ in range(h))
    return (b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', w, h, 8, 6, 0, 0, 0))
            + chunk(b'IDAT', zlib.compress(raw)) + chunk(b'IEND', b''))


def png_size(path):
    h = open(path, 'rb').read(24)
    return struct.unpack('>II', h[16:24])


def anm2(sheet, crops):
    """one sheet, one layer, a frame per crop (x, y, w, h)"""
    frames = ''.join('<Frame XPosition="0" YPosition="0" XPivot="0" YPivot="0" XCrop="%d" YCrop="%d" Width="%d" '
                     'Height="%d" XScale="100" YScale="100" Delay="1" Visible="true" RedTint="255" GreenTint="255" '
                     'BlueTint="255" AlphaTint="255" RedOffset="0" GreenOffset="0" BlueOffset="0" Rotation="0" '
                     'Interpolated="false"/>' % c for c in crops)
    return ('<AnimatedActor><Info CreatedBy="desk" Fps="30" Version="1"/><Content><Spritesheets>'
            '<Spritesheet Path="%s" Id="0"/></Spritesheets><Layers><Layer Name="L" Id="0" SpritesheetId="0"/></Layers>'
            '<Nulls/><Events/></Content><Animations DefaultAnimation="A"><Animation Name="A" FrameNum="%d" Loop="true">'
            '<RootAnimation><Frame XPosition="0" YPosition="0" Delay="%d" Visible="true" XScale="100" YScale="100" '
            'RedTint="255" GreenTint="255" BlueTint="255" AlphaTint="255" RedOffset="0" GreenOffset="0" BlueOffset="0" '
            'Rotation="0" Interpolated="false"/></RootAnimation><LayerAnimations><LayerAnimation LayerId="0" Visible="true">'
            '%s</LayerAnimation></LayerAnimations><NullAnimations/><Triggers/></Animation></Animations></AnimatedActor>'
            % (sheet, len(crops), len(crops), frames))


def converted(name):
    d = os.path.join(TOP, 'conv', name)
    if not os.path.exists(os.path.join(d, 'mod.json')):
        z = os.path.join(TOP, 'conv', name + '.zip')
        os.makedirs(os.path.dirname(z), exist_ok=True)
        subprocess.run([sys.executable, os.path.join(ROOT, 'tools', 'modport.py'), os.path.join(WORKSHOP, REAL[name]),
                        '--game', PACK.rstrip('/'), '--out', z], capture_output=True)
        with zipfile.ZipFile(z) as zz:
            zz.extractall(d)
    return d


def config(name, mods):
    """mods: [(id, folder to copy | {relative path: bytes})], the first the strongest"""
    cfg = os.path.join(TOP, name)
    shutil.rmtree(cfg, ignore_errors=True)
    for mid, src in mods:
        dest = os.path.join(cfg, 'mods', mid)
        if isinstance(src, str):
            shutil.copytree(src, dest)
        else:
            put(os.path.join(dest, 'mod.json'), '{"id": "%s", "name": "%s"}' % (mid, mid.upper()))
            for rel, data in src.items():
                put(os.path.join(dest, *rel.split('/')), data)
    put(os.path.join(cfg, 'mods', 'mods.txt'), '# order\n' + ''.join('on %s\n' % m for m, _ in mods))
    return cfg


def env():
    e = {k: v for k, v in os.environ.items() if not k.startswith('DESK_')}
    e['DESK_ROOT'] = os.path.join(TOP, 'root')
    e['DESK_REAL_PATCH'] = '1'
    return e


def guard(cfg):
    out = subprocess.run([DESK, 'guard', cfg], capture_output=True, text=True, env=env(), encoding='utf-8',
                         errors='replace').stdout
    over = re.findall(r'   OVER (\S+) (over|under) (\S+) files=(\d+) kinds=(\d+) lang=(\S+)', out)
    broken = re.findall(r'   BROKEN (\S+) anm_of=(\S+) anms=(\d+) first=(.*?) sheet_of=(\S+) sheet=(.*?) '
                        r'served=(\d+x\d+) cut_for=(\d+x\d+)', out)
    return out, over, broken


def check_txt(cfg, mid):
    p = os.path.join(cfg, 'mods', mid + '.check.txt')
    return open(p, encoding='utf-8').read() if os.path.exists(p) else ''


def page(cfg, k, lang=0):
    """mod k's page and its details (tools/tweaksdesk): the lines over the title, the details' lines, the raw output"""
    out = subprocess.run([TWEAKS, cfg, '585', '270', str(lang), 'guard', 'mods', 'user', str(k), 'draw', 'notes', 'detail',
                          'draw', 'notes'], capture_output=True, env=env()).stdout.decode('utf-8', 'replace').replace('\r', '')
    links = re.findall(r'^LINK \d+ (red|grey) (.*)$', out, re.M)
    detail = re.findall(r'^DETAIL \d+ x=-?\d+ y=-?\d+ font=\d ink=(\w+) (.*)$', out, re.M)
    return links, detail, out


def joined(detail):
    return ' '.join(text for _, text in detail)


shutil.rmtree(os.path.join(TOP, 'content-cache'), ignore_errors=True)
os.makedirs(os.path.join(TOP, 'root'), exist_ok=True)

print('1. Sam & Max over Reflashed (both converted by modport)')
cfg = config('sammax-reflashed', [('sammax', converted('sammax')), ('reflashed', converted('reflashed'))])
out, over, broken = guard(cfg)
check(('sammax', 'over', 'reflashed', '22', '1', '-') in over and ('reflashed', 'under', 'sammax', '22', '1', '-') in over,
      'Sam & Max overrides 22 files of Reflashed (pictures), on both pages', over)
menu = [b for b in broken if b[0] == 'sammax' and b[5] == 'gfx/ui/main menu/charactermenu.png']
check(len(menu) == 1 and menu[0][1] == 'reflashed' and menu[0][4] == 'sammax' and menu[0][3] == 'gfx/ui/main menu/charactermenu.anm2'
      and menu[0][6] == '512x1024' and menu[0][7] == '2048x4096',
      'BROKEN: Sam & Max\'s 1x charactermenu.png under Reflashed\'s x4 charactermenu.anm2 (512x1024, cut for 2048x4096)', menu)
mine = sorted((b[5], b[2]) for b in broken if b[0] == 'sammax')
theirs = sorted((b[5], b[2]) for b in broken if b[0] == 'reflashed')
check(mine == theirs and len(mine) == 5, 'the same breakages on both pages (5 sheets)', mine)
check(all(int(b[6].split('x')[0]) * 4 == int(b[7].split('x')[0]) for b in broken),
      'every one is a sheet of Sam & Max at the game\'s size under frames cut for Reflashed\'s x4', broken)
for mid in ('sammax', 'reflashed'):
    t = check_txt(cfg, mid)
    check('BROKEN: gfx/ui/main menu/charactermenu.png of sammax is 512x1024, and 3 .anm2 of reflashed cut it for 2048x4096' in t
          and '    gfx/ui/main menu/charactermenu.anm2\n' in t, 'mods/%s.check.txt: the breakage and its .anm2 listed' % mid,
          t[:600])
    check(('overrides 22 files of reflashed (pictures):' if mid == 'sammax' else '22 files overridden by sammax (pictures):') in t
          and '    gfx/ui/main menu/charactermenu.png\n' in t, 'mods/%s.check.txt: the files overridden, listed' % mid)
check('   sammax: guard completed 0, the game\'s 0,' in out and 'NOT USED' not in check_txt(cfg, 'sammax'),
      'Sam & Max\'s items.xml is content.c\'s: not refused by its raw file\'s bounds, no "the game\'s instead"',
      re.findall(r'   sammax: guard.*', out))
m = re.search(r'mods over each other: .*? (\d+) \.anm2 read, (\d+) ms', out)
check(m is not None, 'the report line of the run: %s' % (m.group(0) if m else '-'))
out2, over2, broken2 = guard(cfg)
m2 = re.search(r'(\d+) \.anm2 read', out2)
check(over2 == over and broken2 == broken and m2 and m2.group(1) == '0',
      'the next start: the same notes, no .anm2 read again (content-cache/guard/sheets.txt)', m2.group(0) if m2 else out2[-300:])

print('2. the page: the lines over the title and the details, in the four languages')
WANT = {0: (('red', 'Conflicts with: Reflashed'), ('grey', 'Check: 2 notes'),
            ['Broken pictures: 5', 'The animations of Reflashed are cut for pictures 4 times larger, and the game gets this '
             'mod\'s:', 'Picture Size Needs Anims', 'charactermenu.png 512x1024 2048x4096 3',
             'What to do: put Reflashed above - then nothing breaks - or turn one of the mods off.',
             'Overrides 22 files of Reflashed: pictures.', 'In full: mods/sammax.check.txt']),
        1: (('red', 'Конфликтует с: Reflashed'), ('grey', 'Проверка: 2 заметки'),
            ['Сломано картинок: 5', 'Анимации мода Reflashed нарезаны под картинки в 4 раза крупнее, а игра берёт картинки '
             'этого мода:', 'Картинка Размер Нужно Анимаций', 'ui_cardspills.png 512x256 2048x1024 44',
             'Перекрывает 22 файла мода Reflashed: картинки.', 'Всё подробно: mods/sammax.check.txt']),
        2: (('red', 'Konflikt mit: Reflashed'), ('grey', 'Prüfung: 2 Hinweise'),
            ['Kaputte Bilder: 5', 'Bild Größe Nötig Animationen', 'Überdeckt 22 Dateien von Reflashed: Bilder.']),
        3: (('red', 'Conflicto con: Reflashed'), ('grey', 'Revisión: 2 notas'),
            ['Imágenes rotas: 5', 'Imagen Tamaño Necesita Animaciones', 'Tapa 22 archivos de Reflashed: imágenes.'])}
for lang, (red, grey, want) in WANT.items():
    links, detail, raw = page(cfg, 0, lang)
    # hotfix 1.71: the grey "Check: N notes" line is gone (the owner: "what does it even mean") - notes in the details
    check(links == [red], 'language set %d: the red line over the title, no grey one' % lang, links)
    text = joined(detail)
    check(all(w in text for w in want), 'language set %d: the details - what breaks, the table, the override' % lang,
          [w for w in want if w not in text] or text[:600])
links, detail, raw = page(cfg, 0, 0)
check(not re.findall(r'^NOTE ', raw, re.M), 'no notes under the rows any more', re.findall(r'^NOTE .*', raw, re.M)[:3])
reds = [text for ink, text in detail if ink == 'red']
check(reds == ['Broken pictures: 5', '512x256', '256x192', '512x64', '512x64', '512x1024'],
      'red: the count, and the sizes the game is handed', reds)
check(('ink', 'Tables: replaced 3') in detail and any('A table from Repentance+' in x for _, x in detail),
      'the check\'s notes on the details, whole sentences, no check.txt path in each', [x for _, x in detail][-6:])
check(sum('check.txt' in x for _, x in detail) == 1, 'the path of check.txt once, at the foot')
links, detail, raw = page(cfg, 1, 0)
text = joined(detail)
check(links and links[0] == ('red', 'Conflicts with: Sam & Max as Jacob & Esau'), 'Reflashed\'s page: its red line', links)
check('The animations of this mod are cut for pictures 4 times larger, and the game gets those of Sam & Max as Jacob & Esau:'
      in text and 'What to do: put this mod above' in text and 'Sam & Max as Jacob & Esau overrides 22 files of this mod: pictures.'
      in text, 'Reflashed\'s details: from its side', text[:700])
# whole units, as test_page checks them: every line on a whole unit, on the page and on the details, scrolled too
def fractions(op):
    nums = [float(v) for v in re.findall(r'-?\d+\.\d+', op)[:2]]
    return tuple(round(v - (v // 1), 3) % 1.0 for v in nums)
for moves, what in ((['user', '0', 'draw', 'drag', '240', '200', '-300', 'draw'], 'the page'),
                    (['user', '0', 'detail', 'draw', 'drag', '240', '200', '-300', 'draw'], 'the details')):
    draw = subprocess.run([TWEAKS, cfg, '585', '270', '0', 'guard', 'mods'] + moves, capture_output=True,
                          env=env()).stdout.decode('utf-8', 'replace')
    blocks = draw.split('== draw')
    d0 = [l[3:] for l in blocks[1].splitlines() if l.startswith('OP ') and not l.startswith('OP render Wall')]
    d1 = [l[3:] for l in blocks[2].splitlines() if l.startswith('OP ') and not l.startswith('OP render Wall')]
    check(d0 and not [o for o in d0 if any(f != 0.0 for f in fractions(o))], '%s: every line on a whole unit' % what,
          [o for o in d0 if any(f != 0.0 for f in fractions(o))][:4])
    fy = set(fractions(o)[1] for o in d1 if len(fractions(o)) > 1)
    check(len(fy) == 1, '%s scrolled: one fraction for every piece' % what, fy)

print('3. Reflashed over Completion Mark Reminder, and back')
cfg = config('reflashed-cmr', [('reflashed', converted('reflashed')), ('cmr', converted('cmr'))])
out, over, broken = guard(cfg)
check(over == [('reflashed', 'over', 'cmr', '2', '1', '-'), ('cmr', 'under', 'reflashed', '2', '1', '-')],
      'Reflashed overrides the 2 widget pictures of Completion Mark Reminder - nothing else', over)
check(not broken, 'no breakage (Reflashed\'s own widget .anm2 over its own x4 pictures)', broken)
t = check_txt(cfg, 'cmr')
check('2 files overridden by reflashed (pictures):\n    gfx/ui/completion_widget.png\n    gfx/ui/completion_widget_pause.png\n' in t,
      'mods/cmr.check.txt names both', t)
cfg = config('cmr-reflashed', [('cmr', converted('cmr')), ('reflashed', converted('reflashed'))])
out, over, broken = guard(cfg)
# hotfix 1.71: Reflashed's own completion_widget.anm2 is the game's now (1.666 too - BY_NUMBER hid it in 1.7), and under
# Completion Mark Reminder's 1x pictures its x4 frames cut wrong - as on the PC: a breakage, named
check(over == [('cmr', 'over', 'reflashed', '2', '1', '-'), ('reflashed', 'under', 'cmr', '2', '1', '-')]
      and any(b[3] == 'gfx/ui/completion_widget.anm2' and b[6] == '256x512' and b[7] == '1024x2048' for b in broken),
      'the other order: Completion Mark Reminder over Reflashed, the same 2 - Reflashed\'s x4 widget frames on its 1x sheet: '
      'a breakage', (over, broken))
for mid in ('reflashed', 'sammax'):
    cfg = config('alone-' + mid, [(mid, converted(mid))])
    out, over, broken = guard(cfg)
    check(not over and not broken, '%s alone: no note' % mid, (over, broken))
    check('Over other mods' not in check_txt(cfg, mid), '%s alone: nothing about other mods in check.txt' % mid)

print('4. the rule on made-up mods: what breaks and what does not')
A = {'resources/gfx/test/t.anm2': anm2('s.png', [(0, 0, 32, 32), (32, 32, 32, 32)]), 'resources/gfx/test/s.png': png(64, 64)}
for size, breaks, why in (((64, 64), False, 'the same size'), ((64, 128), False, 'more rows below, the frames\' places the same'),
                          ((128, 128), True, 'the same picture x2'), ((32, 32), True, 'the same picture at half'),
                          ((64, 48), True, 'smaller: the frames fall off its edge')):
    cfg = config('made-%dx%d' % size, [('b', {'resources/gfx/test/s.png': png(*size)}), ('a', A)])
    out, over, broken = guard(cfg)
    got = [(b[0], b[1], b[4], b[5], b[6], b[7]) for b in broken]
    want = [('a', 'a', 'b', 'gfx/test/s.png', '%dx%d' % size, '64x64'), ('b', 'a', 'b', 'gfx/test/s.png', '%dx%d' % size, '64x64')]
    check(sorted(got) == sorted(want if breaks else []), 'B\'s s.png %dx%d under A\'s frames cut for 64x64: %s - %s'
          % (size + ('BROKEN' if breaks else 'no note', why)), got)
    check(('b', 'over', 'a', '1', '1', '-') in over, '... and B overrides 1 file of A (pictures)', over)
# an .anm2 cutting a sheet nobody measured (no copy of its own, none of the game's): no claim
cfg = config('made-unmeasured', [('b', {'resources/gfx/test/new.png': png(128, 128)}),
                                 ('a', {'resources/gfx/test/t2.anm2': anm2('new.png', [(0, 0, 32, 32)])})])
out, over, broken = guard(cfg)
check(not broken and not over, 'a sheet with no size to be cut for (not A\'s, not the game\'s): no note', (over, broken))
# A's .anm2 for a sheet of the game's, A brings no copy: cut for the game's size
gw, gh = png_size(PACK + 'data/gfx/ui/ui_cardspills.png')
crops = [(0, 0, gw // 2, gh // 2), (gw // 2, gh // 2, gw // 2, gh // 2)]
for k, breaks in ((1, False), (2, True)):
    cfg = config('made-game-%d' % k, [('b', {'resources/gfx/ui/ui_cardspills.png': png(gw * k, gh * k)}),
                                      ('a', {'resources/gfx/ui/t3.anm2': anm2('ui_cardspills.png', crops)})])
    out, over, broken = guard(cfg)
    got = [(b[0], b[1], b[4], b[6], b[7]) for b in broken]
    want = [('a', 'a', 'b', '%dx%d' % (gw * k, gh * k), '%dx%d' % (gw, gh)), ('b', 'a', 'b', '%dx%d' % (gw * k, gh * k), '%dx%d' % (gw, gh))]
    check(sorted(got) == sorted(want if breaks else []), 'A cuts the game\'s ui_cardspills.png (%dx%d), B brings it x%d: %s'
          % (gw, gh, k, 'BROKEN' if breaks else 'no note'), got)
    if breaks:
        check('(the game\'s size: it brings none)' in check_txt(cfg, 'a'), '... check.txt says the size cut for is the game\'s')
    check(not over, '... no override (A brings no copy)', over)
# A's own copy refused by the guard (libpng 1.0.2 stops on it): the game's sheet under frames cut for A's copy
bad = bytearray(png(gw * 4, gh * 4))
bad[24] = 3                                         # bit depth 3 for RGBA: no PNG
cfg = config('made-refused', [('a', {'resources/gfx/ui/t4.anm2': anm2('ui_cardspills.png', [(0, 0, gw * 2, gh * 2),
                                                                                           (gw * 2, gh * 2, gw * 2, gh * 2)]),
                                     'resources/gfx/ui/ui_cardspills.png': bytes(bad)})])
out, over, broken = guard(cfg)
got = [(b[0], b[1], b[4], b[6], b[7]) for b in broken]
check(got == [('a', 'a', '(game)', '%dx%d' % (gw, gh), '%dx%d' % (gw * 4, gh * 4))],
      'A\'s own x4 copy not used (the port\'s check): the game\'s %dx%d under frames cut for %dx%d - BROKEN, the game named'
      % (gw, gh, gw * 4, gh * 4), (got, re.findall(r'VERDICT .*ui_cardspills.*', out)))
check("(the size of its own copy, which the port's check does not use)" in check_txt(cfg, 'a'),
      '... check.txt says the size cut for is its own copy the port\'s check does not use', check_txt(cfg, 'a')[:500])
links, detail, raw = page(cfg, 0, 0)
check(links == [('red', 'Not used: 1 of 2 files - details')] and "Broken: ui_cardspills.png - the game's %dx%d under this mod's animations "
      "(1 file) cut for %dx%d: its own copy is not used" % (gw, gh, gw * 4, gh * 4) in joined(detail),
      '... no other mod in it: no conflict line; the refused copy in red (hotfix 1.71), the details say it', (links, joined(detail)))
# a language folder only
cfg = config('made-lang', [('a', {'resources-dlc3.ru/sfx/x.wav': b'RIFF'}), ('b', {'resources/sfx/x.wav': b'RIFF'})])
out, over, broken = guard(cfg)
check(over == [('a', 'over', 'b', '1', '4', 'ru'), ('b', 'under', 'a', '1', '4', 'ru')], 'A\'s Russian file over B\'s: 1 sound, '
      'only in the ru set', over)
links, detail, raw = page(cfg, 1, 1)
check(links == [] and 'A перекрывает 1 файл этого мода: звуки. Только в наборе ru.' in joined(detail),
      'B\'s page, Russian: no line (hotfix 1.71: notes in the details only); the details - who, how many, which kind, which language', (links, joined(detail)))
cfg = config('made-lang2', [('b', {'resources/sfx/x.wav': b'RIFF'}), ('a', {'resources-dlc3.ru/sfx/x.wav': b'RIFF'})])
out, over, broken = guard(cfg)
check(over == [('b', 'over', 'a', '1', '4', 'ru'), ('a', 'under', 'b', '1', '4', 'ru')],
      'the other order: B\'s file for all sets over A\'s Russian one (the PC: the mods\' order first)', over)

print('\n%d failures' % len(failures))
for f in failures:
    print('  - ' + f)
sys.exit(1 if failures else 0)
