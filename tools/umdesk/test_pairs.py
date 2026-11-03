# -*- coding: utf-8 -*-
"""THE PAIRS and what the page shows of a mod (the owner, 05.10.2026; the research notes of 05.10.2026 on how mod conflicts look), on the
real usermods.c (desk.exe):

  1. the name without the PC's sort marks ("!!TBOI: Reflashed" -> "Reflashed"), the description without Steam's BBCode;
  2. every two mods of the list, on or off, worked out as if the only two on, the upper over the lower: what breaks
     (an .anm2 of one, a sheet of the other) and what one stands over; a mod that is off answers as if it were on;
  3. the real Sam & Max and Reflashed: the pair says what the run says, in both orders; the next start reads no .anm2.

    bash tools/umdesk/build.sh && py -3 tools/umdesk/test_pairs.py
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
ROOT = os.path.dirname(os.path.dirname(HERE))
TOP = os.path.join(tempfile.gettempdir(), 'umdesk-pairs')
DESK = os.path.join(HERE, 'desk.exe')
CONV = os.path.join(tempfile.gettempdir(), 'umdesk-conflicts', 'conv')       # test_conflicts.py converts them
failures = []


def check(cond, what, detail=''):
    print(('  ok    ' if cond else '  FAIL  ') + what + ('' if cond or not detail else '\n        ' + str(detail)[:1200]))
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


def config(name, mods, states=None):
    """mods: [(id, folder to copy | {relative path: bytes|str})], the first the strongest; states: 'on'/'off' each"""
    cfg = os.path.join(TOP, name)
    shutil.rmtree(cfg, ignore_errors=True)
    for mid, src in mods:
        dest = os.path.join(cfg, 'mods', mid)
        if isinstance(src, str):
            shutil.copytree(src, dest)
        else:
            if 'mod.json' not in src:
                put(os.path.join(dest, 'mod.json'), '{"id": "%s", "name": "%s"}' % (mid, mid.upper()))
            for rel, data in src.items():
                put(os.path.join(dest, *rel.split('/')), data)
    states = states or ['on'] * len(mods)
    put(os.path.join(cfg, 'mods', 'mods.txt'), '# order\n' + ''.join('%s %s\n' % (s, m) for s, (m, _) in zip(states, mods)))
    return cfg


def env():
    e = {k: v for k, v in os.environ.items() if not k.startswith('DESK_')}
    e['DESK_ROOT'] = os.path.join(TOP, 'root')
    return e


def run(cmd, cfg):
    return subprocess.run([DESK, cmd, cfg], capture_output=True, text=True, env=env(), encoding='utf-8',
                          errors='replace').stdout


def pairs(cfg):
    out = run('guard', cfg)
    conflicts = dict((m, (int(n), f)) for m, n, f in re.findall(r'   CONFLICTS (\S+) n=(-?\d+) first=(\S+)', out))
    over = re.findall(r'   PAIROVER (\S+) (\S+) mine_over=(\d) files=(\d+) kinds=(\d+) lang=(\S+)', out)
    broken = re.findall(r'   PAIRBROKEN (\S+) (\S+) anm_mine=(\d) sheet=(.*?) served=(\d+x\d+) cut_for=(\d+x\d+) anms=(\d+) own=(\d)',
                        out)
    return out, conflicts, over, broken


shutil.rmtree(TOP, ignore_errors=True)
os.makedirs(os.path.join(TOP, 'root'), exist_ok=True)

print('1. the name and the description as the page shows them')
NAMES = [('!!TBOI: Reflashed', 'Reflashed'), ('!!~External item descriptions', 'External item descriptions'),
         ('TBOI - Better Items', 'Better Items'), ('TBOI', 'TBOI'), ('~~~', '~~~'), ('TBOIRebirth Plus', 'TBOIRebirth Plus'),
         ('Sam & Max as Jacob & Esau', 'Sam & Max as Jacob & Esau'), ('_Русский мод', 'Русский мод'),
         ('(Hidden) Thing', '(Hidden) Thing')]
mods = []
for k, (raw, want) in enumerate(NAMES):
    mods.append(('m%d' % k, {'mod.json': '{"id": "m%d", "name": %s}' % (k, __import__('json').dumps(raw)),
                             'resources/x%d.txt' % k: 'x'}))
DESC = ('[h1]Big title[/h1]\n[b]Bold[/b] and [i]italic[/i] and [url=https://x.y/z]a link[/url].\n'
        '[img]https://files.catbox.moe/xe0qfs.png[/img]\n[list]\n[*] first\n[*]second\n[/list]\n'
        'Keep [WIP] and [3 items] as they are.\n\n\n\n[hr][/hr]End')
mods.append(('desc', {'mod.json': '{"id": "desc", "name": "Desc", "description": %s}' % __import__('json').dumps(DESC),
                      'resources/d.txt': 'x'}))
cfg = config('names', mods)
out = run('settings', cfg)
got = dict(re.findall(r'   MOD (\S+) name="(.*?)" version=', out))
for k, (raw, want) in enumerate(NAMES):
    check(got.get('m%d' % k) == want, '"%s" shows as "%s"' % (raw, want), got.get('m%d' % k))
desc = re.search(r'   DESC desc (.*?)\n(?:   MOD|   SET|   ISSUES|==)', out, re.S)
desc = desc.group(1) if desc else ''
check('[' not in desc.replace('[WIP]', '').replace('[3 items]', ''), 'no BBCode tag left', desc)
check('Big title\nBold and italic and a link.' in desc, 'the text of the tags stays, a link\'s words too', desc)
check('catbox' not in desc, 'a picture goes whole: its address is no text', desc)
check('• first\n• second' in desc, 'a list item is "• "', desc)
check('Keep [WIP] and [3 items] as they are.' in desc, 'a bracket that is no Steam tag stays', desc)
check('\n\n\n' not in desc and desc.endswith('End'), 'no more than one empty line, none at the end', repr(desc))

print('2. made-up mods: every two, the upper over the lower')
A = {'resources/gfx/test/t.anm2': anm2('s.png', [(0, 0, 32, 32), (32, 32, 32, 32)]), 'resources/gfx/test/s.png': png(64, 64)}
B = {'resources/gfx/test/s.png': png(128, 128)}
cfg = config('b-over-a', [('b', B), ('a', A)])
out, conf, over, broken = pairs(cfg)
check(conf.get('a') == (1, 'b') and conf.get('b') == (1, 'a'), 'B over A: each conflicts with the other', conf)
check(('a', 'b', '1', 'gfx/test/s.png', '128x128', '64x64', '1', '1') in broken
      and ('b', 'a', '0', 'gfx/test/s.png', '128x128', '64x64', '1', '1') in broken,
      'the breakage from both sides: A\'s .anm2 (own copy 64x64) over B\'s 128x128 sheet', broken)
check(('b', 'a', '1', '1', '1', '-') in over and ('a', 'b', '0', '1', '1', '-') in over, 'B stands over 1 picture of A', over)
check('pairs: 2 mods (on and off)' in out and '2 pairs worked out' in out, 'the report line of the pairs',
      re.findall(r'pairs: .*', out))
cfg = config('a-over-b', [('a', A), ('b', B)])
out, conf, over, broken = pairs(cfg)
check(conf.get('a') == (0, '-') and conf.get('b') == (0, '-') and not broken,
      'A over B: A\'s own copy is served, nothing breaks', (conf, broken))
check(('a', 'b', '1', '1', '1', '-') in over, '... and A stands over B\'s picture', over)
cfg = config('b-off', [('b', B), ('a', A)], ['off', 'on'])
out, conf, over, broken = pairs(cfg)
check(conf.get('b') == (1, 'a') and conf.get('a') == (0, '-'),
      'B off: B would break with A (on); A breaks with nothing on', conf)
check(len([b for b in broken if b[0] == 'b']) == 1, 'B off: its pair with A worked out all the same', broken)
cfg = config('both-off', [('b', B), ('a', A)], ['off', 'off'])
out, conf, over, broken = pairs(cfg)
check(conf.get('a') == (0, '-') and conf.get('b') == (0, '-') and len(broken) == 2,
      'both off: no conflict with a mod that is on, the pair known', (conf, broken))
# a sheet of the game's under a mod's own frames (its copy refused) is that mod's business, not the pair's
bad = bytearray(png(64, 64))
bad[24] = 3
C = {'resources/gfx/test/t.anm2': anm2('s.png', [(0, 0, 32, 32)]), 'resources/gfx/test/s.png': bytes(bad)}
D = {'resources/gfx/other/o.png': png(8, 8)}
cfg = config('refused', [('c', C), ('d', D)])
out, conf, over, broken = pairs(cfg)
check(not broken and conf.get('c') == (0, '-'), 'C\'s own copy refused, D not involved: no conflict between C and D',
      (broken, conf))

print('3. Sam & Max and Reflashed')
if os.path.exists(os.path.join(CONV, 'sammax', 'mod.json')) and os.path.exists(os.path.join(CONV, 'reflashed', 'mod.json')):
    cfg = config('sammax-reflashed', [('sammax', os.path.join(CONV, 'sammax')), ('reflashed', os.path.join(CONV, 'reflashed'))])
    out, conf, over, broken = pairs(cfg)
    run_broken = re.findall(r'   BROKEN sammax anm_of=(\S+) anms=(\d+) first=.*? sheet_of=(\S+) sheet=(.*?) served=(\d+x\d+) '
                            r'cut_for=(\d+x\d+)', out)
    mine = sorted((b[3], b[4], b[5], b[6]) for b in broken if b[0] == 'sammax')
    theirs = sorted((r[3], r[4], r[5], r[1]) for r in run_broken)
    check(len(mine) == 5 and mine == theirs, 'the pair says what the run says: the 5 sheets, sizes and .anm2 counts',
          (mine, theirs))
    check(conf.get('sammax') == (1, 'reflashed') and conf.get('reflashed') == (1, 'sammax'), 'each names the other', conf)
    check(('sammax', 'reflashed', '1', '22', '1', '-') in over, 'Sam & Max stands over 22 pictures of Reflashed', over)
    m = re.search(r'pairs: .*?(\d+) \.anm2 read', out)
    out2 = run('guard', cfg)
    m2 = re.search(r'pairs: .*?(\d+) \.anm2 read', out2)
    check(m2 is not None and m2.group(1) == '0', 'the next start: no .anm2 read for the pairs (sheets.txt)',
          (m.group(0) if m else '-', m2.group(0) if m2 else '-'))
    cfg = config('reflashed-off', [('reflashed', os.path.join(CONV, 'reflashed')), ('sammax', os.path.join(CONV, 'sammax'))],
                 ['off', 'on'])
    out, conf, over, broken = pairs(cfg)
    check(conf.get('sammax') == (0, '-') and conf.get('reflashed') == (0, '-'),
          'Reflashed above Sam & Max (off): its own x4 copies are served - nothing breaks', conf)
    check(('reflashed', 'sammax', '1', '22', '1', '-') in over, '... and Reflashed would stand over the 22', over)
else:
    print('  (skipped: run test_conflicts.py once - it converts the two mods)')

print('\n%d failures' % len(failures))
for f in failures:
    print('  - ' + f)
sys.exit(1 if failures else 0)
