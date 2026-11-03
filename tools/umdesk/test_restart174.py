# -*- coding: utf-8 -*-
"""RESTART TO APPLY worked out, not set (hotfix 1.74, THE RESTART OWED in usermods.c), on the real usermods.c.

The players on 1.73: "RESTART disappeared after installing mods". The row stood on one flag that each road set and
nothing took back, and a flag dies with its process. Now the restart is owed when the mods a start would index -
from the list in memory, and from the disk (mods.txt and the folders it does not name) - are not this run's index.

    bash tools/umdesk/build.sh && bash tools/umdesk/build.sh desk0.exe 0 && py -3 tools/umdesk/test_restart174.py

  A. the page's own changes: owed while they differ, gone when turned back; an off mod moved or deleted owes nothing
  B. ADD MOD, one and a batch: owed, pale
  C. an update: of a mod that is on - owed, pale; of one that is off - nothing
  D. Android ended the game behind the picker, and the installer finished before the new process's start: the start
     indexes the mod - nothing owed, nothing pale (1.73 owed a restart for nothing here)
  E. the same with the start first (what 1.74's installer waits for): owed and pale; the next start takes it
  F. a folder put into mods/ by hand while the game runs (CONFIG FILE): the disk's look owes the restart; a folder
     still marked .adding, an .incoming- one, owe nothing
  G. mods.txt edited by hand while the game runs: owed; put back - nothing
  H. the start while ADD MOD is at work in the same process: its .incoming- and .adding folders are left alone
  I. the list full (64): a new mod is refused with the words UserMods.java knows (LIST_FULL); nothing owed
  J. the ground truth, many random runs: after each step a pale name never stands without the row; at the end the
     row stands exactly when a new start indexes other mods, or in another order, than this run
"""
import os
import random
import re
import shutil
import subprocess
import sys
import tempfile

sys.stdout.reconfigure(encoding='utf-8', errors='replace')
HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
TOP = os.path.join(tempfile.gettempdir(), 'umdesk-restart174')
WAV = b'RIFF....WAVEfmt '
failures = []


def check(cond, what, got=None):
    print(('  ok    ' if cond else '  FAIL  ') + what + ('' if cond or got is None else ' - %r' % (got,)))
    if not cond:
        failures.append(what)


def put(path, data=b'x'):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    open(path, 'wb').write(data if isinstance(data, bytes) else data.encode('utf-8'))


def mod(path, ident):
    put(os.path.join(path, 'mod.json'), '{"id": "%s", "name": "Mod %s"}' % (ident, ident))
    put(os.path.join(path, 'resources', 'sfx', ident + '.wav'), WAV + ident.encode())


def lay_out(name, listed, extra_stage=()):
    """listed: [(folder, on)]; the stage holds the folders ADD MOD will bring"""
    top = os.path.join(TOP, name)
    shutil.rmtree(top, ignore_errors=True)
    cfg, stage = os.path.join(top, 'cfg'), os.path.join(top, 'stage')
    for folder, _ in listed:
        mod(os.path.join(cfg, 'mods', folder), folder.split('-', 1)[1].lower())
    put(os.path.join(cfg, 'mods', 'mods.txt'), "# the player's mods, the strongest first: on|off <folder>\n"
        + ''.join('%s %s\n' % ('on' if on else 'off', f) for f, on in listed))
    for folder in extra_stage:
        mod(os.path.join(stage, folder), folder.split('-', 1)[-1].lower())
    return top, cfg, stage.replace('\\', '/')


def run(desk, cmd, cfg, *args, env=None):
    e = {k: v for k, v in os.environ.items() if not k.startswith('DESK_')}
    e['DESK_ROOT'] = os.path.join(TOP, 'root')
    e['DESK_INNER'] = os.path.join(os.path.dirname(cfg), 'inner')
    e.update(env or {})
    out = subprocess.run([desk, cmd, cfg] + list(args), capture_output=True, text=True, env=e, encoding='utf-8',
                         errors='replace')
    return out.stdout + out.stderr


def script(desk, top, cfg, lines, env=None):
    path = os.path.join(top, 'script-%d.txt' % random.randrange(1 << 30))
    open(path, 'w', encoding='utf-8').write('\n'.join(lines) + '\n')
    return run(desk, 'script', cfg, path, env=env)


def gens(out):
    """each `gen`: (changed, {folder: pending})"""
    got = []
    for m in re.finditer(r'== gen \d+ changed=(\d)\n((?:   PENDING \S+ \d\n)*)', out):
        got.append((int(m.group(1)), {p[0]: int(p[1]) for p in re.findall(r'PENDING (\S+) (\d)', m.group(2))}))
    return got


def disks(out):
    return [tuple(int(x) for x in m) for m in re.findall(r'== disk changed=(\d) list=(\d) disk=(\d)', out)]


def owners(out):
    m = re.findall(r'OWNERS (.*)', out)
    return [o.strip().split(',') if o.strip() else [] for o in m]


def start_line(out):
    return re.search(r'== after the start: .*', out).group(0)


for desk in (os.path.join(HERE, 'desk.exe'), os.path.join(HERE, 'desk0.exe')):
    print('\n== %s' % os.path.basename(desk))

    print('A. the page\'s own changes')
    top, cfg, _ = lay_out('A', [('001-A', True), ('002-B', True), ('003-C', False)])
    out = script(desk, top, cfg, ['gen', 'toggle 2', 'gen', 'toggle 2', 'gen', 'down 0', 'gen', 'up 1', 'gen',
                                  'up 2', 'gen', 'remove 1', 'gen', 'remove 0', 'gen'])
    g = [c for c, _ in gens(out)]
    check(g[0] == 0, 'the start owes nothing', g)
    check(g[1] == 1 and g[2] == 0, 'C switched on: owed; switched off again: gone', g[1:3])
    check(g[3] == 1 and g[4] == 0, 'A under B: owed; back on top: gone', g[3:5])
    check(g[5] == 0, 'C (off) moved above B: the game would see nothing - nothing owed', g[5])
    check(g[6] == 0, 'C (off) deleted: nothing owed', g[6])
    check(g[7] == 1, 'A (on) deleted: owed', g[7])

    print('B. ADD MOD, one and a batch')
    top, cfg, st = lay_out('B', [('001-A', True), ('002-B', True)], ['003-C', '004-D', '005-E', '006-F'])
    out = script(desk, top, cfg, ['move %s/003-C 003-C' % st, 'put 003-C -', 'gen',
                                  'move %s/004-D 004-D' % st, 'put 004-D -', 'move %s/005-E 005-E' % st, 'put 005-E -',
                                  'move %s/006-F 006-F' % st, 'put 006-F -', 'gen'])
    g = gens(out)
    check(g[0][0] == 1 and g[0][1].get('003-C') == 1, 'one mod: owed, pale', g[0])
    check(g[1][0] == 1 and all(g[1][1].get(f) == 1 for f in ('003-C', '004-D', '005-E', '006-F')),
          'a batch of four: owed, all four pale', g[1])

    print('C. an update')
    top, cfg, st = lay_out('C', [('001-A', True), ('002-B', False)], ['003-A', '004-B'])
    out = script(desk, top, cfg, ['move %s/004-B 004-B' % st, 'mark 002-B', 'put 004-B 002-B 002-B', 'gen',
                                  'move %s/003-A 003-A' % st, 'mark 001-A', 'put 003-A 001-A 001-A', 'gen'])
    g = gens(out)
    # hotfix 1.75 (ADD MOD OWES ITS RESTART): 1.74 owed nothing here, and the add window had no RESTART either
    check(g[0][0] == 1 and g[0][1].get('004-B') == 0, 'B (off) updated: off, not pale - ADD MOD owes its restart', g[0])
    check(g[1][0] == 1 and g[1][1].get('003-A') == 1, 'A (on) updated: owed, pale', g[1])

    print('D. the installer finished before the new process\'s start (Android ended the game behind the picker)')
    top, cfg, st = lay_out('D', [('001-A', True)], ['002-N'])
    shutil.move(os.path.join(top, 'stage', '002-N'), os.path.join(cfg, 'mods', '002-N'))
    out = script(desk, top, cfg, ['gen', 'disk', 'owners'], env={'DESK_EARLY_PUT': '002-N'})
    check('== early put 002-N: written' in out, 'listPut with no start yet: written', out[:300])
    g = gens(out)
    # hotfix 1.75: the add was this process's own - its restart is owed (1.73's row; 1.74 had none, nor the window's)
    check(g[0][0] == 1 and g[0][1].get('002-N') == 0, 'the start took it: not pale - ADD MOD owes its restart', g)
    check(disks(out)[0][1:] == (0, 0), 'the list and the disk owe nothing of their own', disks(out))
    check(owners(out)[0] == ['002-N', '001-A'], 'it is in this run\'s index, first', owners(out))

    print('E. the start first, then the installer (what 1.74 waits for)')
    top, cfg, st = lay_out('E', [('001-A', True)], ['002-N'])
    out = script(desk, top, cfg, ['move %s/002-N 002-N' % st, 'put 002-N -', 'gen', 'disk'])
    g = gens(out)
    check(g[0][0] == 1 and g[0][1].get('002-N') == 1, 'owed, pale', g)
    check(disks(out)[0] == (1, 1, 1), 'the list and the disk both say so', disks(out))
    out = script(desk, top, cfg, ['gen', 'owners'])
    check(gens(out)[0][0] == 0 and owners(out)[0] == ['002-N', '001-A'], 'the next start: in the index, nothing owed',
          (gens(out), owners(out)))

    print('F. a folder put in by hand while the game runs')
    top, cfg, st = lay_out('F', [('001-A', True)], ['002-H', '003-M', 'x'])
    os.makedirs(os.path.join(top, 'stage', '003-M'), exist_ok=True)
    put(os.path.join(top, 'stage', '003-M', '.adding'), b'')
    out = script(desk, top, cfg, ['disk', 'drop %s/003-M 003-M' % st, 'disk', 'drop %s/x .incoming-x' % st, 'disk',
                                  'drop %s/002-H 002-H' % st, 'disk'])
    d = disks(out)
    check(d[0][0] == 0, 'nothing yet', d)
    check(d[1][0] == 0, 'a folder still marked .adding: no mod, nothing owed', d[1])
    check(d[2][0] == 0, 'an .incoming- folder: nothing owed', d[2])
    check(d[3] == (1, 0, 1), 'a mod\'s folder: the disk owes the restart (the list in memory does not know it)', d[3])
    out = script(desk, top, cfg, ['owners'])
    check(owners(out)[0] == ['001-A', '002-H'], 'and the next start does take it', owners(out))

    print('G. mods.txt edited by hand while the game runs')
    top, cfg, _ = lay_out('G', [('001-A', True), ('002-B', True)])
    mt = os.path.join(cfg, 'mods', 'mods.txt')
    was = open(mt, encoding='utf-8').read()
    out = script(desk, top, cfg, ['disk'])
    check(disks(out)[0][0] == 0, 'as it was: nothing', disks(out))
    # the hand edit between two of the page's asks: the stand puts a prepared file over mods.txt
    alt = os.path.join(top, 'alt')
    os.makedirs(alt, exist_ok=True)
    put(os.path.join(alt, 'mods.txt'), was.replace('on 002-B', 'off 002-B'))
    put(os.path.join(alt, 'back.txt'), was)
    out = script(desk, top, cfg, ['replace %s/mods.txt mods.txt' % alt.replace('\\', '/'), 'disk',
                                  'replace %s/back.txt mods.txt' % alt.replace('\\', '/'), 'disk'])
    d = disks(out)
    check(len(d) == 2 and d[0] == (1, 0, 1), 'B switched off in the file by hand: owed', d)
    check(len(d) == 2 and d[1][0] == 0, 'the file put back: nothing', d)

    print('H. the start while ADD MOD is at work in this process')
    for busy in (1, 0):
        top, cfg, _ = lay_out('H%d' % busy, [('001-A', True)])
        mods = os.path.join(cfg, 'mods')
        put(os.path.join(mods, '.incoming-002-N', 'resources', 'sfx', 'n.wav'), WAV)
        put(os.path.join(mods, '003-M', 'resources', 'sfx', 'm.wav'), WAV)
        put(os.path.join(mods, '003-M', '.adding'), b'')
        out = run(desk, 'start', cfg, env={'DESK_INSTALLING': '1'} if busy else {})
        kept = os.path.isdir(os.path.join(mods, '.incoming-002-N')), os.path.isdir(os.path.join(mods, '003-M'))
        if busy:
            check(kept == (True, True), 'at work: its .incoming- and .adding folders left alone', kept)
            check('003-M' not in start_line(out) and not re.search(r'\d (on |off) 003-M', out),
                  'the .adding folder is no mod meanwhile', out[:400])
        else:
            check(kept == (False, False), 'no add at work: both swept, as ever', kept)

    print('I. the list full')
    listed = [('%03d-M%d' % (i + 1, i), True) for i in range(256)]       # hotfix 1.75: MODS_MAX 64 -> 256
    top, cfg, st = lay_out('I', listed, ['257-N'])
    out = script(desk, top, cfg, ['move %s/257-N 257-N' % st, 'put 257-N -', 'gen'])
    m = re.search(r'== put 257-N: (.*?), gen', out)
    check(m and m.group(1) == 'the list is full: 256 mods at most', 'refused: ' + (m.group(1) if m else '?'))
    check(gens(out)[0][0] == 0, 'nothing owed (no start would take it)', gens(out)[0][0])
    java = open(os.path.join(ROOT, 'port', 'app', 'src', 'main', 'java', 'ru', 'isaac', 'port', 'UserMods.java'),
                encoding='utf-8').read()
    lf = re.search(r'LIST_FULL = "([^"]+)"', java)
    check(lf and m and m.group(1).startswith(lf.group(1)), 'UserMods.LIST_FULL is the start of that answer',
          lf.group(1) if lf else None)

    print('J. the ground truth, random runs')
    rnd = random.Random(174)
    bad_pale, bad_truth, runs = [], [], 60 if desk.endswith('desk.exe') else 25
    for r in range(runs):
        n0 = rnd.randint(1, 6)
        listed = [('%03d-%s' % (i + 1, chr(65 + i)), rnd.random() < 0.7) for i in range(n0)]
        new = ['%03d-N%d' % (n0 + 1 + k, k) for k in range(6)]
        top, cfg, st = lay_out('J%d' % r, listed, new)
        # the list as the stand will hold it, to pick sensible steps: [(folder, on)]
        cur = list(listed)
        free = list(new)
        lines = []
        for step in range(rnd.randint(1, 8)):
            op = rnd.choice(['toggle', 'up', 'down', 'remove', 'put', 'update', 'drop'] if cur else ['put', 'drop'])
            if op in ('toggle', 'up', 'down', 'remove'):
                i = rnd.randrange(len(cur))
                if op == 'up' and i == 0 or op == 'down' and i == len(cur) - 1:
                    op = 'toggle'
                lines.append('%s %d' % (op, i))
                if op == 'toggle':
                    cur[i] = (cur[i][0], not cur[i][1])
                elif op == 'up':
                    cur[i - 1], cur[i] = cur[i], cur[i - 1]
                elif op == 'down':
                    cur[i + 1], cur[i] = cur[i], cur[i + 1]
                else:
                    cur.pop(i)
            elif free:
                f = free.pop(0)
                if op == 'put':
                    lines += ['move %s/%s %s' % (st, f, f), 'put %s -' % f]
                    cur.insert(0, (f, True))
                elif op == 'update' and cur:
                    i = rnd.randrange(len(cur))
                    old = cur[i][0]
                    lines += ['move %s/%s %s' % (st, f, f), 'mark %s' % old, 'put %s %s %s' % (f, old, old)]
                    cur[i] = (f, cur[i][1])
                else:
                    lines += ['drop %s/%s %s' % (st, f, f)]     # joins at the next start, on, at the end
            lines.append('gen')
        lines += ['disk', 'owners']
        added = any(l.startswith('put ') for l in lines)     # hotfix 1.75: ADD MOD owes its restart till the restart
        out = script(desk, top, cfg, ['owners'] + lines)
        this = owners(out)[0]
        for changed, pend in gens(out):
            if any(pend.values()) and not changed:
                bad_pale.append((r, lines, changed, pend))
        d = disks(out)[-1]
        nxt = owners(script(desk, top, cfg, ['owners']))[0]
        if bool(d[0]) != (nxt != this or added):
            bad_truth.append((r, lines, this, nxt, d))
    check(not bad_pale, '%d runs: a pale name always with RESTART TO APPLY' % runs, bad_pale[:2])
    check(not bad_truth, '%d runs: RESTART TO APPLY exactly when the next start indexes otherwise, or ADD MOD put a mod in'
          % runs, bad_truth[:2])

print('\n%d failures' % len(failures))
sys.exit(1 if failures else 0)
