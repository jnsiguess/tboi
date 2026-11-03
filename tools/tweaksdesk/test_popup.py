# -*- coding: utf-8 -*-
"""Desk cases for a mod's page without the wall of notes, its details and the conflict popup (the owner's choices of
05.10.2026, the research notes of 05.10.2026 on how mod conflicts look), on the real tweaks.c + usermods.c (tools/tweaksdesk/build.sh).

    bash tools/tweaksdesk/build.sh && py -3 tools/tweaksdesk/test_popup.py

  1. the popup: a mod switched on that breaks with one that is on (the list's sideways step, the page's ENABLED), in
     the four languages; OK, DETAILS, TURN OFF by the pad and by a finger; nothing for a mod that breaks with nothing;
     switched on before the pairs are worked out, it comes when they are;
  2. the lines over the title: the cursor starts under them, the pad reaches them, a tap and a confirm open the
     details, BACK comes back where it was;
  3. the details: they scroll as far as the paper, by the pad and by a finger;
  4. whole units: the popup and the lines over the title on whole units.
"""
import os
import re
import shutil
import struct
import subprocess
import sys
import tempfile
import zlib

HERE = os.path.dirname(os.path.abspath(__file__))
DESK = os.path.join(HERE, 'tweaksdesk.exe')
TOP = os.path.join(tempfile.gettempdir(), 'tweaksdesk-popup')
failures = []


def check(cond, what, detail=''):
    print(('  ok    ' if cond else '  FAIL  ') + what + ('' if cond or not detail else '\n        ' + str(detail)[:900]))
    if not cond:
        failures.append(what)


def put(path, data):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    open(path, 'wb').write(data if isinstance(data, bytes) else data.encode('utf-8'))


def png(w, h):
    def chunk(t, d):
        return struct.pack('>I', len(d)) + t + d + struct.pack('>I', zlib.crc32(t + d) & 0xffffffff)
    raw = b''.join(b'\0' + b'\x80\x40\x20\xff' * w for _ in range(h))
    return (b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', w, h, 8, 6, 0, 0, 0))
            + chunk(b'IDAT', zlib.compress(raw)) + chunk(b'IEND', b''))


def anm2(sheet, crops):
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


A = {'resources/gfx/test/t.anm2': anm2('s.png', [(0, 0, 32, 32), (32, 32, 32, 32)]), 'resources/gfx/test/s.png': png(64, 64)}
B = {'resources/gfx/test/s.png': png(128, 128)}
C = {'resources/sfx/c.wav': b'RIFF'}


def config(name, mods, states):
    cfg = os.path.join(TOP, name)
    shutil.rmtree(cfg, ignore_errors=True)
    for (mid, files), state in zip(mods, states):
        put(os.path.join(cfg, 'mods', mid, 'mod.json'), '{"id": "%s", "name": "!!%s"}' % (mid, mid.upper()))
        for rel, data in files.items():
            put(os.path.join(cfg, 'mods', mid, *rel.split('/')), data)
    put(os.path.join(cfg, 'mods', 'mods.txt'), '# order\n' + ''.join('%s %s\n' % (s, m) for (m, _), s in zip(mods, states)))
    return cfg


def env():
    e = {k: v for k, v in os.environ.items() if not k.startswith('DESK_')}
    e['DESK_ROOT'] = os.path.join(TOP, 'root')
    return e


class Run(object):
    def __init__(self, cfg, moves, lang=0, w=585, h=270):
        p = subprocess.run([DESK, cfg, str(w), str(h), str(lang)] + [str(m) for m in moves], capture_output=True, env=env())
        self.text = p.stdout.decode('utf-8', 'replace').replace('\r', '')
        self.blocks = []
        for part in self.text.split('\n== ')[1:]:
            head, _, body = part.partition('\n')
            st = re.search(r'^STATE (.*)$', body, re.M)
            state = dict(kv.split('=', 1) for kv in st.group(1).split() if '=' in kv) if st else {}
            self.blocks.append(dict(move=head, body=body, state=state))

    def last(self, move=None):
        bl = [b for b in self.blocks if move is None or b['move'] == move]
        return bl[-1] if bl else dict(move='', body='', state={})


def popup(block):
    title = re.search(r'^POPUP title=(.*?) btn=(\d) mod=(-?\d+)$', block['body'], re.M)
    lines = re.findall(r'^POPLINE \d+ (.*)$', block['body'], re.M)
    btns = re.findall(r'^POPBTN (\d) x=([-\d.]+) w=([-\d.]+) y=([-\d.]+) (.*)$', block['body'], re.M)
    return title, lines, btns


def mods_txt(cfg):
    return open(os.path.join(cfg, 'mods', 'mods.txt')).read()


TO_B = ['pad', 'down'] * 10                 # the MODS list's row of the player's first mod (YOUR MODS passed by)
os.makedirs(os.path.join(TOP, 'root'), exist_ok=True)

print('1. the popup')
WANT = {0: ('These mods conflict', '"B" and "A"', 'Broken pictures: 1 - the animations of A are cut for pictures 2 times smaller',
            'Put A above or turn one of the mods off.', ['OK', 'DETAILS', 'TURN OFF']),
        1: ('Моды конфликтуют', '"B" и "A"', 'Сломано картинок: 1 - анимации мода A нарезаны под картинки в 2 раза мельче',
            'Поставь A выше или выключи один из модов.', ['OK', 'ПОДРОБНЕЕ', 'ВЫКЛЮЧИТЬ']),
        2: ('Mods im Konflikt', '"B" und "A"', 'Kaputte Bilder: 1 - die Animationen von A sind für Bilder 2-mal kleiner',
            'Stelle A höher oder schalte einen der Mods ab.', ['OK', 'DETAILS', 'AUS']),
        3: ('Mods en conflicto', '"B" y "A"', 'Imágenes rotas: 1 - las animaciones de A están cortadas para imágenes 2 veces más pequeñas',
            'Pon A más arriba o desactiva uno de los mods.', ['OK', 'DETALLES', 'DESACTIVAR'])}
for lang, (title, pair, body, advice, btns) in WANT.items():
    cfg = config('pop-%d' % lang, [('b', B), ('a', A)], ['off', 'on'])
    r = Run(cfg, ['guard', 'mods'] + TO_B + ['pad', 'right', 'draw', 'notes'], lang)
    t, lines, b = popup(r.last('notes'))
    text = ' '.join(lines)
    check(t and t.group(1) == title and t.group(3) == '0' and lines and lines[0] == pair and body in text and advice in text
          and [x[4] for x in b] == btns, 'language set %d: switched on, B breaks with A: the popup says which, what, what to do'
          % lang, (t and t.group(0), lines, b))
    check('on b' in mods_txt(cfg), '... and B is on (the switch was made)', mods_txt(cfg))
cfg = config('pop-btn', [('b', B), ('a', A)], ['off', 'on'])
r = Run(cfg, ['guard', 'mods'] + TO_B + ['pad', 'right', 'draw', 'pad', 'right', 'notes', 'pad', 'confirm', 'notes'])
check(popup(r.blocks[-3])[0].group(2) == '1', 'the pad moves the pointer to DETAILS', popup(r.blocks[-3])[0])
check(r.last('pad')['state'].get('page') == 'mod' and r.last('pad')['state'].get('popup') == '0'
      and re.search(r'^DETAIL .*Broken pictures: 1', r.last('notes')['body'], re.M),
      'DETAILS: the popup goes, B\'s details open', r.last('pad')['state'])
cfg = config('pop-back', [('b', B), ('a', A)], ['off', 'on'])
r = Run(cfg, ['guard', 'mods'] + TO_B + ['pad', 'right', 'pad', 'right', 'pad', 'confirm', 'pad', 'back', 'state'])
check(r.last('state')['state'].get('page') == 'mods' and r.last('state')['state'].get('cursor') == '11',
      'BACK from the details: the list, at B\'s row', r.last('state')['state'])
cfg = config('pop-off', [('b', B), ('a', A)], ['off', 'on'])
r = Run(cfg, ['guard', 'mods'] + TO_B + ['pad', 'right', 'pad', 'left', 'pad', 'confirm', 'state'])
check(r.last('state')['state'].get('popup') == '0' and 'off b' in mods_txt(cfg),
      'TURN OFF (the pointer round to the left): B off again, the popup gone', mods_txt(cfg))
cfg = config('pop-ok', [('b', B), ('a', A)], ['off', 'on'])
r = Run(cfg, ['guard', 'mods'] + TO_B + ['pad', 'right', 'pad', 'back', 'state'])
check(r.last('state')['state'].get('popup') == '0' and r.last('state')['state'].get('page') == 'mods' and 'on b' in mods_txt(cfg),
      'BACK is OK: the popup goes, B stays on, the list stays', (r.last('state')['state'], mods_txt(cfg)))
# a finger on a button
cfg = config('pop-tap', [('b', B), ('a', A)], ['off', 'on'])
r = Run(cfg, ['guard', 'mods'] + TO_B + ['pad', 'right', 'draw', 'notes'])
t, lines, b = popup(r.last('notes'))
x, w, y = float(b[2][1]), float(b[2][2]), float(b[2][3])
r = Run(cfg, ['guard', 'mods'] + TO_B + ['pad', 'right', 'draw', 'tap', x + w / 2, y + 10, 'state'])
check(r.last('state')['state'].get('popup') == '0' and 'off b' in mods_txt(cfg), 'a finger on TURN OFF: B off again', mods_txt(cfg))
cfg = config('pop-tap2', [('b', B), ('a', A)], ['off', 'on'])
r = Run(cfg, ['guard', 'mods'] + TO_B + ['pad', 'right', 'draw', 'tap', 300, 30, 'state'])
check(r.last('state')['state'].get('popup') == '1', 'a finger beside the buttons: nothing, the popup stays', r.last('state')['state'])
# the page's ENABLED
cfg = config('pop-page', [('b', B), ('a', A)], ['off', 'on'])
r = Run(cfg, ['guard', 'mods', 'user', 0, 'pad', 'right', 'draw', 'notes'])
check(r.last('notes')['state'].get('popup') == '1' and r.last('notes')['state'].get('page') == 'user',
      'ENABLED on B\'s own page: the popup there too', r.last('notes')['state'])
# a mod that breaks with nothing; a mod switched off
cfg = config('pop-none', [('c', C), ('a', A)], ['off', 'on'])
r = Run(cfg, ['guard', 'mods'] + TO_B + ['pad', 'right', 'state'])
check(r.last('state')['state'].get('popup') == '0', 'C switched on breaks with nothing: no popup', r.last('state')['state'])
cfg = config('pop-offnow', [('b', B), ('a', A)], ['on', 'on'])
r = Run(cfg, ['guard', 'mods'] + TO_B + ['pad', 'right', 'state'])
check(r.last('state')['state'].get('popup') == '0', 'B switched OFF: no popup', r.last('state')['state'])
# before the pairs: asked again when they are there
cfg = config('pop-wait', [('b', B), ('a', A)], ['off', 'on'])
r = Run(cfg, ['mods'] + TO_B + ['pad', 'right', 'state', 'draw', 'guard', 'draw', 'state'])
first = [b for b in r.blocks if b['move'] == 'state'][0]['state']
check(first.get('pending') == '0' and first.get('popup') == '0', 'switched on before the pairs: pending, no popup yet', first)
check(r.last('state')['state'].get('popup') == '1' and r.last('state')['state'].get('pending') == '-1',
      'the pairs worked out: the next frame shows it', r.last('state')['state'])

print('2. the lines over the title')
cfg = config('lines', [('b', B), ('a', A)], ['on', 'on'])
r = Run(cfg, ['guard', 'mods', 'user', 1, 'draw', 'notes'])
st = r.last('notes')['state']
links = re.findall(r'^LINK (\d) (red|grey) (.*)$', r.last('notes')['body'], re.M)
check([l[1:] for l in links] == [('red', 'Conflicts with: B')],
      'A\'s page: "Conflicts with: B" (the name without its "!!"); B over its picture is behind it, no grey line', links)
check(st.get('cursor') == '1' and st.get('cursor_id') == '23', 'the cursor starts on ENABLED, under the line', st)
rows = re.findall(r'^ROW (\d+) id=(\d+) box=([-\d.]+),([-\d.]+),([-\d.]+),([-\d.]+)', r.last('draw')['body'], re.M)
title = re.search(r'^OP write 1 [-\d.]+ ([-\d.]+) \d+ A$', r.last('draw')['body'], re.M)
check(rows[0][1] == '30' and float(rows[0][5]) <= float(title.group(1)) + 2, 'the line stands over the title',
      (rows[:2], title and title.group(0)))
r = Run(cfg, ['guard', 'mods', 'user', 1, 'pad', 'up', 'state', 'pad', 'confirm', 'state', 'pad', 'back', 'state'])
s = [b['state'] for b in r.blocks if b['move'] == 'state']
check(s[0].get('cursor_id') == '30', 'the pad goes up to the red line', s[0])
check(s[1].get('page') == 'mod', 'its confirm opens the details', s[1])
check(s[2].get('page') == 'user' and s[2].get('cursor_id') == '30', 'BACK: the page, the cursor where it was', s[2])
box = [float(v) for v in rows[0][2:]]
r = Run(cfg, ['guard', 'mods', 'user', 1, 'tap', (box[0] + box[2]) / 2, (box[1] + box[3]) / 2, 'state'])
check(r.last('state')['state'].get('page') == 'mod', 'a tap on the red line opens the details at once', r.last('state')['state'])
cfg = config('lines-grey', [('c', C), ('d', {'resources/sfx/c.wav': b'RIFF'})], ['on', 'on'])
r = Run(cfg, ['guard', 'mods', 'user', 1, 'notes', 'detail', 'notes'], lang=1)
links = re.findall(r'^LINK (\d) (red|grey) (.*)$', [b for b in r.blocks if b['move'] == 'notes'][0]['body'], re.M)
detail = ' '.join(re.findall(r'^DETAIL \d+ .*? ink=\w+ (.*)$', r.last('notes')['body'], re.M))
# hotfix 1.71: no grey line any more (the owner: "what does 'Check: 1 note' mean") - the details alone say it; D's
# name stays in ink: its file is under C's by the order, which is no file the port leaves unused
names = re.findall(r'^NAME (\d+) (red|ink) (.*)$', [b for b in r.blocks if b['move'] == 'notes'][0]['body'], re.M)
check(links == [] and 'C перекрывает 1 файл этого мода: звуки.' in detail and all(n[1] == 'ink' for n in names),
      'D under C, nothing broken: no line over the title, the names in ink, the details say who stands over what',
      (links, names, detail))
cfg = config('lines-alone', [('a', A)], ['on'])
r = Run(cfg, ['guard', 'mods', 'user', 0, 'notes'])
check(r.last('notes')['state'].get('links') == '0' and r.last('notes')['state'].get('cursor_id') == '23',
      'a mod with nothing to say: no lines, the page as it was', r.last('notes')['state'])

print('3. the details scroll')
cfg = config('scroll', [('b', B), ('a', A)], ['on', 'on'])
r = Run(cfg, ['guard', 'mods', 'user', 1, 'detail', 'state'] + ['pad', 'down'] * 30 + ['state', 'drag', 240, 100, 400, 'state'],
        w=480, h=270)
s = [b['state'] for b in r.blocks if b['move'] == 'state']
room = float(s[0].get('room', 0))
check(float(s[1].get('scrolled', -1)) == room, 'the pad scrolls down to the foot and stops there (%s)' % room, s[1])
check(float(s[2].get('scrolled', -1)) == 0.0, 'a finger brings it back to the top', s[2])

print('4. whole units')
def fractions(op):
    nums = [float(v) for v in re.findall(r'-?\d+\.\d+', op)[:2]]
    return [round(v - (v // 1), 3) % 1.0 for v in nums]
for w, h in ((585, 270), (480, 270), (480, 360), (601, 270)):
    cfg = config('units', [('b', B), ('a', A)], ['off', 'on'])
    r = Run(cfg, ['guard', 'mods'] + TO_B + ['pad', 'right', 'draw'], w=w, h=h)
    ops = [l[3:] for l in r.last('draw')['body'].splitlines() if l.startswith('OP ') and ('Paper' in l or 'Pointer' in l
                                                                                         or 'write' in l)]
    pop = [o for o in ops if 'Paper' in o or 'Pointer' in o or 'OK' in o or 'TURN OFF' in o]
    check(len(pop) >= 4 and not [o for o in pop if any(f != 0.0 for f in fractions(o))],
          '%dx%d: the popup on whole units' % (w, h), pop)
    cfg = config('units2', [('b', B), ('a', A)], ['on', 'on'])
    r = Run(cfg, ['guard', 'mods', 'user', 1, 'draw'], w=w, h=h)
    ops = [l[3:] for l in r.last('draw')['body'].splitlines() if l.startswith('OP ') and not l.startswith('OP render Wall')]
    check(not [o for o in ops if any(f != 0.0 for f in fractions(o))], '%dx%d: the page with its lines on whole units' % (w, h),
          [o for o in ops if any(f != 0.0 for f in fractions(o))][:4])

print('\n%d failures' % len(failures))
for f in failures:
    print('  - ' + f)
sys.exit(1 if failures else 0)
