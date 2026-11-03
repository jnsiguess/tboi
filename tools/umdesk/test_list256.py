# -*- coding: utf-8 -*-
"""THE LIST OF 256 (hotfix 1.75, the owner: MODS_MAX 64 -> 256; the game itself has no such limit), on the real
usermods.c (desk.exe, desk0.exe: the clean build's).

  A. the start with 256 mods: all 256 in the list and in the index, in mods.txt's order; its time
  B. owner 255 - the number OWNER_CONTENT had while owners were one byte: the game asks a file only that mod has and
     is handed that mod's file, the conflict notes name it (not "*content*", not "?")
  C. mods over each other among the last ones (owners 250..255 share a file): the strongest wins, the notes say it
  D. 255 listed + ADD MOD: the new mod goes first, on, owes the restart, written to mods.txt
  E. 256 listed + ADD MOD: refused, "the list is full: 256 mods at most" (UserMods.LIST_FULL, red in the window);
     nothing owed
  F. the guard and the pairs (every two of the list, both orders) over 256 mods: their time

    bash tools/umdesk/build.sh && bash tools/umdesk/build.sh desk0.exe 0 && py -3 tools/umdesk/test_list256.py
"""
import os
import re
import shutil
import subprocess
import sys
import tempfile
import time

sys.stdout.reconfigure(encoding='utf-8', errors='replace')
HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
TOP = os.path.join(tempfile.gettempdir(), 'umdesk-list256')
WAV = b'RIFF....WAVEfmt '
failures = []


def check(cond, what, got=None):
    print(('  ok    ' if cond else '  FAIL  ') + what + ('' if cond or got is None else ' - %r' % (got,)))
    if not cond:
        failures.append(what)


def put(path, data):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    open(path, 'wb').write(data if isinstance(data, bytes) else data.encode('utf-8'))


def folder(i):
    return '%03d-M%d' % (i + 1, i)


def mod(path, i):
    put(os.path.join(path, 'mod.json'), '{"id": "m%d", "name": "Mod number %d with a name of some length"}' % (i, i))
    put(os.path.join(path, 'resources', 'sfx', 'm%d.wav' % i), WAV + b'm%d' % i)
    if i >= 250:                                    # C: the last six bring one file each
        put(os.path.join(path, 'resources', 'sfx', 'shared.wav'), WAV + b'shared by m%d' % i)


def lay_out(name, n, stage=0):
    top = os.path.join(TOP, name)
    shutil.rmtree(top, ignore_errors=True)
    cfg = os.path.join(top, 'cfg')
    for i in range(n):
        mod(os.path.join(cfg, 'mods', folder(i)), i)
    put(os.path.join(cfg, 'mods', 'mods.txt'), "# the player's mods, the strongest first: on|off <folder>\n"
        + ''.join('on %s\n' % folder(i) for i in range(n)))
    for k in range(stage):
        mod(os.path.join(top, 'stage', '%03d-N%d' % (n + 1 + k, k)), 1000 + k)
    root = os.path.join(TOP, 'root')
    put(os.path.join(root, 'repentance-resources', 'data', 'nothing.xml'), '<x/>')
    return top, cfg, os.path.join(top, 'stage').replace('\\', '/')


def run(desk, cmd, cfg, *args):
    e = {k: v for k, v in os.environ.items() if not k.startswith('DESK_')}
    e['DESK_ROOT'] = os.path.join(TOP, 'root')
    e['DESK_INNER'] = os.path.join(os.path.dirname(cfg), 'inner')
    t0 = time.time()
    out = subprocess.run([desk, cmd, cfg] + list(args), capture_output=True, text=True, env=e, encoding='utf-8',
                         errors='replace')
    return out.stdout + out.stderr, time.time() - t0


def script(desk, top, cfg, lines):
    path = os.path.join(top, 'script.txt')
    open(path, 'w', encoding='utf-8').write('\n'.join(lines) + '\n')
    return run(desk, 'script', cfg, path)


for desk in (os.path.join(HERE, 'desk.exe'), os.path.join(HERE, 'desk0.exe')):
    print('\n== %s' % os.path.basename(desk))

    print('A. the start with 256 mods')
    top, cfg, _ = lay_out('A', 256)
    out, secs = script(desk, top, cfg, ['owners', 'gen'])
    own = re.findall(r'OWNERS (.*)', out)
    own = own[0].strip().split(',') if own else []
    check(len(own) == 256 and own == [folder(i) for i in range(256)], 'all 256 in this run\'s index, in order',
          (len(own), own[-2:]))
    check(re.search(r'== gen \d+ changed=0', out) is not None, 'nothing owed after the start')
    print('        the start and its script: %.2f s (the desk process, 256 mods, 262 files)' % secs)

    print('B. owner 255 (OWNER_CONTENT when owners were one byte)')
    out, _ = run(desk, 'map', cfg, 'repentance-resources/data/sfx/m255.wav')
    line = [l for l in out.splitlines() if 'm255.wav' in l and '->' in l]
    check(any(folder(255) in l for l in line), 'm255.wav is handed from %s' % folder(255), line[-2:])
    out, _ = run(desk, 'map', cfg, 'repentance-resources/data/sfx/m0.wav')
    line = [l for l in out.splitlines() if 'm0.wav' in l and '->' in l]
    check(any(folder(0) in l for l in line), 'm0.wav is handed from %s' % folder(0), line[-2:])

    print('C. the last six over each other')
    out, _ = run(desk, 'map', cfg, 'repentance-resources/data/sfx/shared.wav')
    line = [l for l in out.splitlines() if 'shared.wav' in l and '->' in l]
    check(any(folder(250) in l for l in line), 'shared.wav: the strongest of them (%s) wins' % folder(250), line[-2:])
    out, secs = run(desk, 'guard', cfg)
    over = re.findall(r'OVER (\S+) (over|under) (\S+) files=(\d+)', out)
    check(('%s' % folder(250), 'over', folder(255), '1') in over and (folder(255), 'under', folder(250), '1') in over,
          'the notes: %s over %s, one file' % (folder(250), folder(255)), [o for o in over if folder(255) in o[0]][:3])
    check('*content*' not in out and not re.search(r'OVER \S+ \S+ \? ', out), 'no owner read as the content file or nobody')
    pairs = re.findall(r'PAIROVER (\S+) (\S+) mine_over=(\d)', out)
    check((folder(250), folder(255), '1') in pairs and (folder(255), folder(250), '0') in pairs,
          'the pairs: %s over %s, both ways' % (folder(250), folder(255)), [p for p in pairs if folder(255) in p][:3])
    pm = re.search(r'pairs: 256 mods .*?(\d+) pairs worked out.*?, (\d+) ms', out)
    print('        the desk run, its own 65280 pair questions too: %.2f s%s' % (
        secs, ('; the pairs thread: %s pairs in %s ms' % pm.groups()) if pm else ''))

    print('D. 255 listed + ADD MOD')
    top, cfg, st = lay_out('D', 255, stage=1)
    out, _ = script(desk, top, cfg, ['move %s/256-N0 256-N0' % st, 'put 256-N0 -', 'gen'])
    check('== put 256-N0: written' in out, 'the 256th is written', re.findall(r'== put .*', out))
    check(re.search(r'== gen \d+ changed=1\n   PENDING 256-N0 1', out) is not None, 'first, pale, the restart owed',
          out[out.find('== gen'):][:200])
    txt = open(os.path.join(cfg, 'mods', 'mods.txt'), encoding='utf-8').read().splitlines()
    check(txt[1] == 'on 256-N0' and len(txt) == 257, 'mods.txt: first, on, 256 rows', (txt[1], len(txt)))

    print('E. 256 listed + ADD MOD')
    top, cfg, st = lay_out('E', 256, stage=1)
    out, _ = script(desk, top, cfg, ['move %s/257-N0 257-N0' % st, 'put 257-N0 -', 'gen'])
    m = re.search(r'== put 257-N0: (.*?), gen', out)
    check(m and m.group(1) == 'the list is full: 256 mods at most', 'refused: ' + (m.group(1) if m else '?'))
    check(re.search(r'== gen \d+ changed=0', out) is not None, 'nothing owed')
    java = open(os.path.join(ROOT, 'port', 'app', 'src', 'main', 'java', 'ru', 'isaac', 'port', 'UserMods.java'),
                encoding='utf-8').read()
    lf = re.search(r'LIST_FULL = "([^"]+)"', java)
    check(lf and m and m.group(1).startswith(lf.group(1)), 'UserMods.LIST_FULL starts that answer: refused in red')

print('\n%d failures' % len(failures))
sys.exit(1 if failures else 0)
