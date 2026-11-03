"""ADD MOD with no restart after it (hotfix 1.73), on the real usermods.c: the game runs on while the installer
puts mods in (desk.exe script), then starts again.

    bash tools/umdesk/build.sh && bash tools/umdesk/build.sh desk0.exe 0 && py -3 tools/umdesk/test_multimod.py

    1. a new mod put in while the game runs: on the list at once, first and on, pending (pale on the page), the
       list's generation moves (the page lays out again), RESTART TO APPLY owed; the game's files untouched;
    2. an update while the game runs: the new folder takes the old one's place and on/off, the old one (marked
       .remove by the installer) leaves the list, its settings go along by key and value name (a value the new
       version has not got: the default), the old section of mod-settings.ini goes; the game still reads the
       copy it started with - never the new one, never a missing file;
    3. an update of a mod that is off: the new one off too, and not pending;
    4. the next start: the old folder gone from the disk, the new ones in the index, nothing pending.
"""
import os
import re
import shutil
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
TOP = os.path.join(tempfile.gettempdir(), 'umdesk-multimod')
WAV = b'RIFF....WAVEfmt '
failures = []


def check(cond, what, got=None):
    print(('  ok    ' if cond else '  FAIL  ') + what + ('' if got is None else ' - %r' % (got,)))
    if not cond:
        failures.append(what)


def put(path, data=b'x'):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    open(path, 'wb').write(data if isinstance(data, bytes) else data.encode('utf-8'))


SETTINGS_V1 = ('{"id": "a", "name": "Mod A", "version": "1", "settings": ['
               '{"key": "blood", "kind": "switch", "label": "Blood", "default": false},'
               '{"key": "style", "kind": "choice", "label": "Style", "values": [{"value": "classic", "label": "Classic"}, {"value": "clean", "label": "Clean"}, {"value": "retro", "label": "Retro"}], "default": "clean"}]}')
SETTINGS_V2 = ('{"id": "a", "name": "Mod A", "version": "2", "settings": ['
               '{"key": "blood", "kind": "switch", "label": "Blood", "default": false},'
               '{"key": "style", "kind": "choice", "label": "Style", "values": [{"value": "classic", "label": "Classic"}, {"value": "clean", "label": "Clean"}], "default": "clean"},'
               '{"key": "glow", "kind": "switch", "label": "Glow", "default": true}]}')


def lay_out(name, a_state='on', style='retro'):
    top = os.path.join(TOP, name)
    shutil.rmtree(top, ignore_errors=True)
    cfg, stage = os.path.join(top, 'cfg'), os.path.join(top, 'stage')
    mods = os.path.join(cfg, 'mods')
    put(os.path.join(mods, '001-A', 'mod.json'), SETTINGS_V1)
    put(os.path.join(mods, '001-A', 'resources', 'sfx', 'a.wav'), WAV + b'A1')
    put(os.path.join(mods, '001-A', 'resources', 'sfx', 'only-v1.wav'), WAV + b'A1')
    put(os.path.join(mods, '001-A.check.txt'), 'a note of the old copy\n')
    put(os.path.join(mods, '002-B', 'mod.json'), '{"id": "b", "name": "Mod B"}')
    put(os.path.join(mods, '002-B', 'resources', 'sfx', 'b.wav'), WAV + b'B')
    put(os.path.join(mods, 'mods.txt'), '# the player\'s mods, the strongest first: on|off <folder>\n%s 001-A\non 002-B\n' % a_state)
    put(os.path.join(mods, 'mod-settings.ini'), '[001-A]\nblood=on\nstyle=%s\n\n[002-B]\nkeep=me\n' % style)
    put(os.path.join(stage, '003-C', 'mod.json'), '{"id": "c", "name": "Mod C"}')
    put(os.path.join(stage, '003-C', 'resources', 'sfx', 'c.wav'), WAV + b'C')
    put(os.path.join(stage, '004-A', 'mod.json'), SETTINGS_V2)
    put(os.path.join(stage, '004-A', 'resources', 'sfx', 'a.wav'), WAV + b'A2')
    put(os.path.join(stage, '004-A', 'resources', 'sfx', 'only-v2.wav'), WAV + b'A2')
    return top, cfg, stage


def run(desk, cmd, cfg, *args):
    e = {k: v for k, v in os.environ.items() if not k.startswith('DESK_')}
    e['DESK_ROOT'] = os.path.join(TOP, 'root')
    e['DESK_INNER'] = os.path.join(os.path.dirname(cfg), 'inner')
    out = subprocess.run([desk, cmd, cfg] + list(args), capture_output=True, text=True, env=e, encoding='utf-8', errors='replace')
    return out.stdout + out.stderr


def block(out, head):
    """the mods under the last block whose head line starts with '== <head>': [(id, on, flags)]"""
    lines = out.splitlines()
    start = max(i for i, l in enumerate(lines) if l.startswith('== ' + head))
    rows = []
    for l in lines[start + 1:]:
        m = re.match(r'\s+\d+ (on |off) (\S+)(.*)', l)
        if not m:
            break
        rows.append((m.group(2), m.group(1).strip() == 'on', m.group(3)))
    return lines[start], rows


def mapped(out):
    got = []
    for m in re.finditer(r'== map (\S+) -> (.*)', out):
        a = m.group(2).strip().replace('\\', '/')
        got.append('GAME' if a == "(the game's own)" else a.split('/mods/', 1)[1] if '/mods/' in a else a)
    return got


for desk in (os.path.join(HERE, 'desk.exe'), os.path.join(HERE, 'desk0.exe')):
    tag = os.path.basename(desk)
    print('\n== %s' % tag)
    top, cfg, stage = lay_out('live')
    script = os.path.join(top, 'script.txt')
    st = stage.replace('\\', '/')
    open(script, 'w').write('\n'.join([
        'gen',
        'map repentance-resources/data/sfx/a.wav',
        '# 1. a new mod while the game runs',
        'move %s/003-C 003-C' % st,
        'put 003-C -',
        'gen',
        'map repentance-resources/data/sfx/c.wav',
        '# 2. an update of A: the installer has put the new copy in its own folder and marked the old one',
        'move %s/004-A 004-A' % st,
        'mark 001-A',
        'put 004-A 001-A 001-A',
        'gen',
        'settings',
        'ini',
        'map repentance-resources/data/sfx/a.wav',
        'map repentance-resources/data/sfx/only-v1.wav',
        'map repentance-resources/data/sfx/only-v2.wav',
    ]) + '\n')
    out = run(desk, 'script', cfg, script)

    print('1. a new mod while the game runs')
    gens = [int(g) for g in re.findall(r'== gen (\d+)', out)]
    head, rows = block(out, 'put 003-C')
    put_c = re.search(r'== put 003-C: (\S+), gen (\d+) -> (\d+)', out)
    check(put_c is not None and put_c.group(1) == 'written' and int(put_c.group(3)) > int(put_c.group(2)),
          'listPut written, the list generation moved (the page lays out again)', put_c.group(0) if put_c else out[-400:])
    _, rows = block(out, 'after put')
    first = block(out[:out.index('== move %s/004-A' % st)] if '== move %s/004-A' % st in out else out, 'after put')[1]
    check([r[0] for r in first] == ['003-C', '001-A', '002-B'] and first[0][1], 'C first and on, the others as they were',
          [(r[0], r[1]) for r in first])
    check('pending' in first[0][2] and 'indexed' not in first[0][2], 'C pending: on, not in this run\'s index', first[0][2])
    check(all('pending' not in r[2] and 'indexed' in r[2] for r in first[1:]), 'A and B indexed, not pending')
    check(re.search(r'== gen \d+ changed=1', out.split('== put 003-C')[1]) is not None, 'RESTART TO APPLY owed (changed=1)')
    m = mapped(out)
    check(m[0] == '001-A/resources/sfx/a.wav', 'before: the game reads A', m[0])
    check(m[1] == 'GAME', 'C\'s file is not the game\'s yet (no index of it this run)', m[1])

    print('2. an update while the game runs')
    head, upd = block(out, 'after put')
    check([r[0] for r in upd] == ['003-C', '004-A', '002-B'], 'the new copy in the old one\'s place; the old one off the list',
          [r[0] for r in upd])
    check(upd[1][1] and 'pending' in upd[1][2], 'on, as the old one was; pending', upd[1])
    sets = dict(re.findall(r'SET 004-A \d+ kind=\d+ label="(\w+)" value=(\d+)', out))
    check(sets == {'Blood': '1', 'Style': '1', 'Glow': '1'},
          'settings carried by key and value name: blood on; style retro is gone in v2 - its default; glow new - its default', sets)
    ini = re.findall(r'   \| (.*)', out.split('== mod-settings.ini')[-1])
    check('[001-A]' not in ini and '[004-A]' in ini and 'blood=on' in ini and 'style=retro' not in ini,
          'mod-settings.ini: the old section gone, the new one with what is not its default', ini)
    check('[002-B]' in ini and 'keep=me' in ini, 'the other sections left as they were')
    check(m[2] == '001-A/resources/sfx/a.wav', 'the game still reads the copy it started with', m[2])
    check(m[3] == '001-A/resources/sfx/only-v1.wav', 'a file only the old copy has: still there for this run', m[3])
    check(m[4] == 'GAME', 'a file only the new copy has: not the game\'s before the restart', m[4])
    check(os.path.isfile(os.path.join(cfg, 'mods', '001-A', 'resources', 'sfx', 'a.wav')), 'the old folder whole on the disk')
    txt = open(os.path.join(cfg, 'mods', 'mods.txt')).read()
    check(txt.splitlines()[1:] == ['on 003-C', 'on 004-A', 'on 002-B'], 'mods.txt as the page shows it', txt.splitlines()[1:])

    print('3. the next start')
    out2 = run(desk, 'map', cfg, 'repentance-resources/data/sfx/a.wav', 'repentance-resources/data/sfx/only-v1.wav',
               'repentance-resources/data/sfx/only-v2.wav', 'repentance-resources/data/sfx/c.wav')
    _, rows2 = block(out2, 'after the start')
    check([(r[0], r[1]) for r in rows2] == [('003-C', True), ('004-A', True), ('002-B', True)], 'the list as left', rows2)
    check(all('indexed' in r[2] and 'pending' not in r[2] for r in rows2), 'all in the index, nothing pending')
    check(not os.path.exists(os.path.join(cfg, 'mods', '001-A')), 'the old folder gone from the disk')
    check(not os.path.exists(os.path.join(cfg, 'mods', '001-A.check.txt')), 'its check.txt gone with it')
    check(mapped(out2) == ['004-A/resources/sfx/a.wav', 'GAME', '004-A/resources/sfx/only-v2.wav', '003-C/resources/sfx/c.wav'],
          'the game reads the new copy and C', mapped(out2))
    check(re.search(r'changed=0', out2.split('== after the start')[1].splitlines()[0]) is not None, 'no restart owed')

    print('4. an update of a mod that is off')
    top, cfg, stage = lay_out('off', a_state='off')
    st = stage.replace('\\', '/')
    script = os.path.join(top, 'script.txt')
    open(script, 'w').write('move %s/004-A 004-A\nmark 001-A\nput 004-A 001-A 001-A\n' % st)
    out = run(desk, 'script', cfg, script)
    _, upd = block(out, 'after put')
    check([(r[0], r[1]) for r in upd] == [('004-A', False), ('002-B', True)] and 'pending' not in upd[0][2],
          'the new copy off, as the old one was, and not pending', upd)

    print('5. an update whose old folder could not be marked: both kept, the old one off (as before)')
    top, cfg, stage = lay_out('nomark')
    st = stage.replace('\\', '/')
    script = os.path.join(top, 'script.txt')
    open(script, 'w').write('move %s/004-A 004-A\nput 004-A 001-A 001-A\n' % st)
    out = run(desk, 'script', cfg, script)
    _, upd = block(out, 'after put')
    check([(r[0], r[1]) for r in upd] == [('004-A', True), ('001-A', False), ('002-B', True)],
          'the new one above the old one, the old one off', [(r[0], r[1]) for r in upd])

    print('6. a choice the new version still has: carried by its name')
    top, cfg, stage = lay_out('classic', style='classic')
    st = stage.replace('\\', '/')
    script = os.path.join(top, 'script.txt')
    open(script, 'w').write('move %s/004-A 004-A\nmark 001-A\nput 004-A 001-A 001-A\nsettings\nini\n' % st)
    out = run(desk, 'script', cfg, script)
    sets = dict(re.findall(r'SET 004-A \d+ kind=\d+ label="(\w+)" value=(\d+)', out))
    ini = re.findall(r'   \| (.*)', out.split('== mod-settings.ini')[-1])
    check(sets == {'Blood': '1', 'Style': '0', 'Glow': '1'} and 'style=classic' in ini and '[001-A]' not in ini,
          'style classic carried (value 0), written under the new folder', (sets, ini))

print('\n%d failures' % len(failures))
sys.exit(1 if failures else 0)
