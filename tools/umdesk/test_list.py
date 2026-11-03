"""Desk cases for mods.txt and the start's cleanup (plan 02.10, A1-A6, B3), on the real usermods.c.

    bash tools/umdesk/build.sh && py -3 tools/umdesk/test_list.py
"""
import os
import shutil
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
DESK = os.path.join(HERE, 'desk.exe')
sys.path.insert(0, HERE)
from mods666 import read_666  # noqa: E402

failures = []


def check(cond, what):
    print(('  ok    ' if cond else '  FAIL  ') + what)
    if not cond:
        failures.append(what)


def make_mod(mods, ident, extra=True):
    d = os.path.join(mods, ident)
    os.makedirs(os.path.join(d, 'resources', 'gfx'), exist_ok=True)
    open(os.path.join(d, 'mod.json'), 'w').write('{"id": "%s", "name": "Mod %s", "version": "1"}' % (ident, ident))
    if extra:
        open(os.path.join(d, 'resources', 'gfx', 'x.txt'), 'w').write('x')
    return d


def run(cmd, cfg, *args, env=None, inner=None):
    e = dict(os.environ)
    for k in list(e):
        if k.startswith('DESK_'):
            del e[k]
    if inner:
        e['DESK_INNER'] = inner
    e.update(env or {})
    out = subprocess.run([DESK, cmd, cfg] + list(args), capture_output=True, text=True, env=e, encoding='utf-8', errors='replace')
    return out.stdout + out.stderr


def state(out, block='after the start'):
    """{id: on} and the head line of a block of desk output"""
    lines = out.splitlines()
    for i, l in enumerate(lines):
        if l.startswith('== ' + block):
            res = {}
            order = []
            for m in lines[i + 1:]:
                if not m.startswith('   ') or m.startswith('   ['):
                    if m.startswith('  [report]'):
                        continue
                    break
                w = m.split()
                if len(w) >= 3 and w[0].isdigit():
                    res[w[2]] = w[1] == 'on'
                    order.append(w[2])
            return res, order, l
    return {}, [], ''


def text(path):
    try:
        return open(path, 'rb').read()
    except OSError:
        return None


def fresh():
    root = tempfile.mkdtemp(prefix='umdesk-')
    cfg = os.path.join(root, 'cfg')
    mods = os.path.join(cfg, 'mods')
    os.makedirs(mods)
    inner = os.path.join(root, 'files')
    os.makedirs(inner)
    return root, cfg, mods, inner


def case(name):
    print('\n' + name)


# 1. OFF survives a restart, and the list is written in the order 1.666 reads
case('1. OFF -> restart -> OFF; the file in the order "on|off <folder>"')
root, cfg, mods, inner = fresh()
make_mod(mods, 'a'); make_mod(mods, 'b')
o = run('start', cfg, inner=inner)
s, order, _ = state(o)
check(s == {'a': True, 'b': True}, 'two folders join on: %s' % s)
o = run('toggle', cfg, '0', inner=inner)
body = text(os.path.join(mods, 'mods.txt')).decode()
check(body.startswith('# ') and 'off a\n' in body and 'on b\n' in body, 'mods.txt after OFF: %r' % body)
check('read back ok' in o, 'the write was read back')
check(text(os.path.join(inner, 'state', 'mods.txt')) == body.encode(), 'the copy inside the app is the same text')
o = run('start', cfg, inner=inner)
s, order, _ = state(o)
check(s == {'a': False, 'b': True}, 'after the restart: %s' % s)
check('mods.txt written' not in o, 'the restart wrote nothing')
r666 = read_666(mods)
check([(i, on) for i, on in r666] == [(i, s[i]) for i in order], '1.666 reads the same list in the same order: %s' % r666)

# 2. mods.txt there but not readable: the copy stands in, nothing written
case('2. mods.txt answers EACCES: not read; the list from the copy inside the app; the file left alone')
before = text(os.path.join(mods, 'mods.txt'))
o = run('start', cfg, inner=inner, env={'DESK_FAIL_OPEN': 'mods/mods.txt:13'})
s, order, head = state(o)
check('unread=1' in head and 'inner copy' in head, head)
check(s == {'a': False, 'b': True}, 'states from the copy: %s' % s)
check(text(os.path.join(mods, 'mods.txt')) == before, 'mods.txt not touched')
check('NOT READ' in o, 'said in the report')

case('3. the same with EIO and no copy inside the app: the mods join OFF')
o = run('start', cfg, inner=os.path.join(root, 'nofiles'), env={'DESK_FAIL_OPEN': 'mods/mods.txt:5'})
s, order, head = state(o)
check(s == {'a': False, 'b': False}, 'all off: %s' % s)
check(text(os.path.join(mods, 'mods.txt')) == before, 'mods.txt not touched')

case('4. the read breaks off in the middle (ferror): the same as not read')
o = run('start', cfg, inner=inner, env={'DESK_FAIL_READ': 'mods/mods.txt:1'})
s, order, head = state(o)
check('unread=1' in head, head)
check(s == {'a': False, 'b': True}, 'states from the copy: %s' % s)
check(text(os.path.join(mods, 'mods.txt')) == before, 'mods.txt not touched')

case('5. stat of a mod\'s folder answers EIO: its line is kept, the mod is left out of this run')
o = run('start', cfg, inner=inner, env={'DESK_FAIL_STAT': 'mods/b:5'})
s, order, head = state(o)
check(s.get('b') is True and '(unreachable)' in o, 'b kept as on: %s' % s)
check(text(os.path.join(mods, 'mods.txt')) == before, 'mods.txt not touched')
check('not reachable' in o, 'said in the report')

case('6. a folder marked .remove that will not go whole: stays marked, never a mod')
make_mod(mods, 'c')
open(os.path.join(mods, 'c', '.remove'), 'w').close()
o = run('start', cfg, inner=inner, env={'DESK_FAIL_UNLINK': 'mods/c/resources/gfx/x.txt'})
s, order, head = state(o)
check('c' not in s, 'c not in the list: %s' % s)
check(os.path.exists(os.path.join(mods, 'c', '.remove')), 'the mark is still there')
o = run('start', cfg, inner=inner)
check(not os.path.exists(os.path.join(mods, 'c')), 'gone at the next start, when it can go')
s, order, head = state(o)
check('c' not in s, 'and never came back: %s' % s)

case('7. a file of 1.666 (no header): read, written once with the header, not again')
open(os.path.join(mods, 'mods.txt'), 'w').write('off b\non a\n')
o = run('start', cfg, inner=inner)
s, order, head = state(o)
check(order == ['b', 'a'] and s == {'a': True, 'b': False}, 'read: %s %s' % (order, s))
check('mods.txt written (start)' in o, 'written once')
body = text(os.path.join(mods, 'mods.txt')).decode()
check(body.splitlines()[1:] == ['off b', 'on a'], 'the order kept: %r' % body)
o = run('start', cfg, inner=inner)
check('mods.txt written' not in o, 'not written again')

case('8. the file of 30.09 ("<folder> on|off" under its header): read, written in the order 1.666 reads')
open(os.path.join(mods, 'mods.txt'), 'w').write("# the player's mods, the strongest first: <folder> on|off\na on\nb off\n")
o = run('start', cfg, inner=inner)
s, order, head = state(o)
check(order == ['a', 'b'] and s == {'a': True, 'b': False}, 'read: %s %s' % (order, s))
body = text(os.path.join(mods, 'mods.txt')).decode()
check(body.splitlines()[1:] == ['on a', 'off b'], 'written anew: %r' % body)
r666 = read_666(mods)
check(r666 == [('a', True), ('b', False)], '1.666 reads it: %s' % r666)

case('9. a mod named "on": the header decides')
make_mod(mods, 'on')
open(os.path.join(mods, 'mods.txt'), 'w').write("# the player's mods, the strongest first: on|off <folder>\noff on\non a\noff b\n")
o = run('start', cfg, inner=inner)
s, order, head = state(o)
check(s == {'on': False, 'a': True, 'b': False} and order == ['on', 'a', 'b'], 'read: %s %s' % (order, s))
shutil.rmtree(os.path.join(mods, 'on'))

case('10. an update that broke off (B3)')
root, cfg, mods, inner = fresh()
make_mod(mods, 'x')
os.rename(os.path.join(mods, 'x'), os.path.join(mods, '.old-x-20261002-120000'))
o = run('start', cfg, inner=inner)
check(os.path.isdir(os.path.join(mods, 'x')) and not os.path.exists(os.path.join(mods, '.old-x-20261002-120000')),
      'killed after the old copy went aside: it is back as x')
check('restored from .old-x-20261002-120000' in o, 'said in the report')
make_mod(mods, '.old-x-20261002-130000')
make_mod(mods, '.incoming-x')
o = run('start', cfg, inner=inner)
check(os.path.isdir(os.path.join(mods, 'x')) and not os.path.exists(os.path.join(mods, '.old-x-20261002-130000')),
      'killed after the new copy stood whole: the old copy goes')
check(not os.path.exists(os.path.join(mods, '.incoming-x')), 'the unfinished .incoming goes')
# the new copy put in place file by file and cut short: marked .adding
os.rename(os.path.join(mods, 'x'), os.path.join(mods, '.old-x-20261002-140000'))
make_mod(mods, 'x')
open(os.path.join(mods, 'x', 'NEW'), 'w').close()
open(os.path.join(mods, 'x', '.adding'), 'w').close()
o = run('start', cfg, inner=inner)
check(os.path.isdir(os.path.join(mods, 'x')) and not os.path.exists(os.path.join(mods, 'x', 'NEW')),
      'killed in the middle of a file-by-file move: the half copy goes, the old one is back')
make_mod(mods, '.old-x-20261002-150000')
open(os.path.join(mods, '.old-x-20261002-150000', '.remove'), 'w').close()
o = run('start', cfg, inner=inner)
check(not os.path.exists(os.path.join(mods, '.old-x-20261002-150000')), 'a marked old copy goes')
s, order, head = state(o)
check(s == {'x': True}, 'the list: %s' % s)

case('11. a list not read at the start, read later between frames')
root, cfg, mods, inner = fresh()
make_mod(mods, 'a'); make_mod(mods, 'b')
open(os.path.join(mods, 'mods.txt'), 'w').write("# the player's mods, the strongest first: on|off <folder>\non a\noff b\n")
o = run('tick', cfg, inner=inner, env={'DESK_FAIL_OPEN': 'mods/mods.txt:5', 'DESK_HEAL': '1'})
s, order, head = state(o)
check(s == {'a': False, 'b': False}, 'at the start: all off (no copy): %s' % s)
s, order, head = state(o, 'after a later frame')
check('unread=0' in head and s == {'a': True, 'b': False} and 'changed=1' in head, 'read later: %s %s' % (head, s))
check('read on retry' in o, 'said in the report')

case('12. mods.txt that cannot be written: the error kept for the page, the copy inside the app follows the player')
root, cfg, mods, inner = fresh()
make_mod(mods, 'a')
run('start', cfg, inner=inner)
o = run('toggle', cfg, '0', inner=inner, env={'DESK_FAIL_WRITE': 'mods/mods.txt'})
s, order, head = state(o, 'after toggle')
check('was not saved (open' in head, head)
check(b'off a' in (text(os.path.join(inner, 'state', 'mods.txt')) or b''), 'the copy inside the app has the OFF')

case('13. mods.txt gone (ENOENT) with a copy inside the app: the copy, written back')
root, cfg, mods, inner = fresh()
make_mod(mods, 'a'); make_mod(mods, 'b')
run('toggle', cfg, '1', inner=inner)
os.remove(os.path.join(mods, 'mods.txt'))
o = run('start', cfg, inner=inner)
s, order, head = state(o)
check(s == {'a': True, 'b': False} and 'inner copy' in head, 'from the copy: %s %s' % (head, s))
check(b'off b' in (text(os.path.join(mods, 'mods.txt')) or b''), 'mods.txt written back')

case('14. DELETE whose mark cannot be made: kept, off - not back on at the next start')
root, cfg, mods, inner = fresh()
make_mod(mods, 'a'); make_mod(mods, 'b')
run('start', cfg, inner=inner)
o = run('remove', cfg, '0', inner=inner, env={'DESK_FAIL_OPENW': 'mods/a/.remove:13'})
s, order, head = state(o, 'after remove')
check(s == {'a': False, 'b': True}, 'the mark refused: a kept in the list, off: %s' % s)
o = run('start', cfg, inner=inner)
s, order, head = state(o)
check(s == {'a': False, 'b': True}, 'and still off at the next start: %s' % s)
o = run('remove', cfg, '0', inner=inner)
s, order, head = state(o, 'after remove')
check(s == {'b': True}, 'the mark made: gone from the list: %s' % s)
o = run('start', cfg, inner=inner)
s, order, head = state(o)
check(s == {'b': True} and not os.path.exists(os.path.join(mods, 'a')), 'and its folder gone at the next start: %s' % s)

case('15. ADD MOD of a new mod: first and on')
root, cfg, mods, inner = fresh()
for m in ('001-A', '002-B', '003-C'):
    make_mod(mods, m)
open(os.path.join(mods, 'mods.txt'), 'w').write("# x\non 001-A\noff 002-B\non 003-C\n")
make_mod(mods, '004-D')
o = run('put', cfg, '004-D', '-', inner=inner)
s, order, head = state(o, 'after put')
check(order == ['004-D', '001-A', '002-B', '003-C'] and s['004-D'], 'new: first, on: %s %s' % (order, s))

case('16. an UPDATE in its own folder keeps its place and its OFF (the owner, 02.10)')
open(os.path.join(mods, 'mods.txt'), 'w').write("# x\non 001-A\noff 002-B\non 003-C\n")
shutil.rmtree(os.path.join(mods, '004-D'))
o = run('put', cfg, '002-B', '002-B', inner=inner)
s, order, head = state(o, 'after put')
check(order == ['001-A', '002-B', '003-C'] and s == {'001-A': True, '002-B': False, '003-C': True}, 'kept: %s %s' % (order, s))
body = text(os.path.join(mods, 'mods.txt')).decode()
check(body.splitlines()[1:] == ['on 001-A', 'off 002-B', 'on 003-C'], 'mods.txt: %r' % body)

case('17. an UPDATE that had to go beside its folder takes the old one\'s place and state; the old one off')
make_mod(mods, '003-C-20261002-120000')
o = run('put', cfg, '003-C-20261002-120000', '003-C', '003-C', inner=inner)
s, order, head = state(o, 'after put')
check(order == ['001-A', '002-B', '003-C-20261002-120000', '003-C'] and s['003-C-20261002-120000'] and not s['003-C'],
      'beside: %s %s' % (order, s))

print('\n%d failures' % len(failures))
for f in failures:
    print('  ' + f)
sys.exit(1 if failures else 0)
