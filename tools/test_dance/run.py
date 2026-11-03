# -*- coding: utf-8 -*-
"""Desk test of the Specialist Dance on every way out of a run (1.75). mods.c's
dance section is cut out as it is (Android's LP64 longs) and run against the
fake game of test_dance.c, which deletes the players the game's way on each
road. Host clang with the MSVC runtime, as tools/test_perf172. Nothing touches
the phone.

    py -3 tools/test_dance/run.py              mods.c, ISAAC_DIAG 0 and 1, every road
    py -3 tools/test_dance/run.py --old        mods.c.before-175dance as well: the crash, shown
    py -3 tools/test_dance/run.py --fuzz N     N random steps per seed (default 3000)
"""
import os
import re
import shutil
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, '..', '..'))
CPP = os.path.join(ROOT, 'port', 'app', 'src', 'main', 'cpp')
CLANG = shutil.which('clang') or r'C:\Program Files\LLVM\bin\clang.exe'
VCVARS = [r'C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Auxiliary\Build\vcvars64.bat',
          r'C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat']
OUT = os.path.join(tempfile.gettempdir(), 'test_dance')
ROADS = ['menu', 'savequit', 'restart', 'restart-same-address', 'restart-into-dance', 'minimise',
         'coopleave', 'floor']
SEEDS = [175, 1013, 4242, 77, 9001]


def msvc_env():
    for bat in VCVARS:
        if os.path.exists(bat):
            r = subprocess.run('call "%s" >nul && set' % bat, shell=True, capture_output=True, text=True)
            env = dict(os.environ)
            for line in r.stdout.splitlines():
                k, _, v = line.partition('=')
                if k:
                    env[k] = v
            return env
    sys.exit('no vcvars64.bat: the MSVC runtime is needed to link')


def cut_function(src, head):
    """the text of a C function from its head to its closing brace"""
    f = src.index(head)
    i = src.index('{', f)
    depth = 0
    while True:
        depth += {'{': 1, '}': -1}.get(src[i], 0)
        if depth == 0:
            return src[f:i + 1]
        i += 1


def cut(source, tag):
    """mods.c's dance, from MOD_DANCE to the draw, every long made long long (Windows keeps long at 32 bits)"""
    src = open(os.path.join(CPP, source), encoding='utf-8').read().replace('\r\n', '\n')
    a = src.index('#define MOD_DANCE        0')
    b = src.index('/* ------------------------------------------------------------- the draw */')
    # what the section takes from the file's head: the two globals, the room, their readers
    head = [m.group(0) for m in re.finditer(r'^#define (MANAGER_PTR|GAME_PTR|GAME_ROOM)\b.*$', src[:a], re.M)]
    head += [cut_function(src, 'static unsigned long game_now('), cut_function(src, 'static unsigned long manager_now(')]
    text = '\n'.join(head) + '\n#line %d "%s"\n' % (src[:a].count('\n') + 1, source) + src[a:b]
    text = text.replace('long long', '\x01')
    text = re.sub(r'\blong\b', 'long long', text).replace('\x01', 'long long')
    d = os.path.join(OUT, tag)
    os.makedirs(d, exist_ok=True)
    with open(os.path.join(d, 'dance_cut.h'), 'w', encoding='utf-8') as f:
        f.write('/* cut out of %s by run.py - do not edit */\n' % source + text)
    return d


def build(env, source, tag, diag):
    d = cut(source, tag)
    exe = os.path.join(d, 'test_dance_diag%d.exe' % diag)
    cmd = [CLANG, '-std=gnu11', '-O1', '-g', '-Wall', '-Wno-unused-function', '-Wno-unused-variable',
           '-Wno-format', '-Wno-unused-but-set-variable', '-D_CRT_SECURE_NO_WARNINGS', '-DISAAC_DIAG=%d' % diag,
           '-I', d, '-I', CPP, os.path.join(HERE, 'test_dance.c'), '-o', exe]
    r = subprocess.run(cmd, env=env, capture_output=True, text=True)
    if r.returncode:
        print(r.stdout + r.stderr)
        sys.exit('build of %s (diag %d) failed' % (source, diag))
    for w in [l for l in (r.stdout + r.stderr).splitlines() if 'warning' in l][:10]:
        print('  ' + w)
    return exe


def run(exe, args):
    r = subprocess.run([exe] + args, capture_output=True, text=True, timeout=600)
    return r.returncode, (r.stdout + r.stderr).strip()


def main():
    steps = int(sys.argv[sys.argv.index('--fuzz') + 1]) if '--fuzz' in sys.argv else 3000
    env = msvc_env()
    bad = 0
    for diag in (0, 1):
        exe = build(env, 'mods.c', 'new', diag)
        print('== mods.c, ISAAC_DIAG=%d' % diag)
        for road in ROADS:
            code, text = run(exe, [road])
            print('  %-22s %s' % (road, 'ok' if code == 0 else 'FAILED (%d)' % code))
            if code:
                bad += 1
                print('    ' + text.replace('\n', '\n    '))
        for seed in SEEDS:
            code, text = run(exe, ['fuzz', str(steps), str(seed)])
            print('  fuzz seed %-12d %s' % (seed, 'ok' if code == 0 else 'FAILED (%d)' % code))
            print('    ' + text.replace('\n', '\n    '))
            bad += code != 0
    if '--old' in sys.argv and os.path.exists(os.path.join(CPP, 'mods.c.before-175dance')):
        exe = build(env, 'mods.c.before-175dance', 'old', 0)
        print('== mods.c.before-175dance (1.74), ISAAC_DIAG=0 - expected to fail')
        for road in ROADS:
            code, text = run(exe, [road])
            first = text.splitlines()[0] if text else ''
            print('  %-22s %s  %s' % (road, 'ok' if code == 0 else 'FAILED (%d)' % code, first))
        code, text = run(exe, ['fuzz', str(steps), str(SEEDS[0])])
        print('  fuzz seed %-12d %s  %s' % (SEEDS[0], 'ok' if code == 0 else 'FAILED (%d)' % code,
                                            text.splitlines()[0] if text else ''))
    print('ALL CLEAN' if not bad else '%d FAILED' % bad)
    return 1 if bad else 0


if __name__ == '__main__':
    sys.exit(main())
