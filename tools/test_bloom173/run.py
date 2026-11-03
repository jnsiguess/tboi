# -*- coding: utf-8 -*-
"""Desk tests of the bloom redirect and the pass clock (08.10.2026). Host clang
with the MSVC runtime, a fake GL (fakegl.c) and test_perf172's small POSIX
layer; the port's own bloom.c and gpu.c are compiled as they are. Nothing
touches the phone.

    py -3 tools/test_bloom173/run.py          everything
    py -3 tools/test_bloom173/run.py bloom    bloom.c, the play build and the report build
    py -3 tools/test_bloom173/run.py passes   gpu.c's pass clock, new and (shown, not judged) old

The condition model over every screen is apart: the research notes of 08.10.2026, bloom_model.py.
"""
import os
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, '..', '..'))
CPP = os.path.join(ROOT, 'port', 'app', 'src', 'main', 'cpp')
sys.path.insert(0, os.path.join(ROOT, 'tools', 'test_perf172'))
import run as perf  # noqa: E402  (its clang and MSVC environment)

DESK = os.path.join(ROOT, 'tools', 'test_perf172', 'desk')
OUT = os.path.join(perf.OUT, '..', 'test_bloom173')


def build(name, sources, defines):
    os.makedirs(OUT, exist_ok=True)
    exe = os.path.join(OUT, name + '.exe')
    cmd = [perf.CLANG, '-std=gnu11', '-O1', '-g', '-Wall', '-Wno-unused-function', '-Wno-unused-variable',
           '-Wno-unused-but-set-variable', '-Wno-format', '-D_CRT_SECURE_NO_WARNINGS',
           '-I', os.path.join(HERE, 'fakegl'), '-I', HERE, '-I', CPP, '-I', DESK,
           '-include', os.path.join(DESK, 'desk_posix.h')]
    cmd += ['-D' + d for d in defines]
    cmd += [os.path.join(HERE, s) for s in sources]
    cmd += [os.path.join(DESK, 'desk_posix.c'), '-o', exe]
    r = subprocess.run(cmd, env=perf.msvc_env(), capture_output=True, text=True)
    if r.returncode:
        print(r.stdout + r.stderr)
        sys.exit('build of %s failed' % name)
    for w in [l for l in (r.stdout + r.stderr).splitlines() if 'warning' in l and 'desk' not in l][:20]:
        print('  ' + w)
    return exe


def run(exe, judged=True):
    r = subprocess.run([exe])
    if judged and r.returncode:
        sys.exit('%s failed' % os.path.basename(exe))


def bloom():
    run(build('test_bloom_play', ['test_bloom.c', 'fakegl.c'], ['ISAAC_DIAG=0']))
    run(build('test_bloom_report', ['test_bloom.c', 'fakegl.c'], ['ISAAC_DIAG=1']))


def passes():
    run(build('test_passes', ['test_passes.c', 'fakegl.c'], ['ISAAC_DIAG=1']))
    run(build('test_passes_old', ['test_passes.c', 'fakegl.c'], ['ISAAC_DIAG=1', 'OLD']), judged=False)


if __name__ == '__main__':
    what = sys.argv[1] if len(sys.argv) > 1 else 'all'
    if what in ('all', 'bloom'):
        bloom()
    if what in ('all', 'passes'):
        passes()
    print('done')
