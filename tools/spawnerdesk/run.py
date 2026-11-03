# -*- coding: utf-8 -*-
"""The spawner desk (07.10.2026): the REAL spawner.c and spawner_stats.c built
for Windows with host clang and the MSVC runtime, the game's functions answered
by stubs at their very addresses inside a fake image (desk.c), the font
measured with the real advances (tables.inc). Nothing touches the phone.

The only change made to the sources on the way in is the width of `long`:
the port is LP64 (Android), Windows is LLP64, so `long` becomes `long long` in
the desk's copies (src_*.c, written here and not kept).

    py -3 tools/spawnerdesk/run.py
"""
import os
import re
import shutil
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
CPP = os.path.normpath(os.path.join(HERE, '..', '..', 'port', 'app', 'src', 'main', 'cpp'))
CLANG = shutil.which('clang') or r'C:\Program Files\LLVM\bin\clang.exe'
VCVARS = [r'C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Auxiliary\Build\vcvars64.bat',
          r'C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat']


def msvc_env():
    for bat in VCVARS:
        if not os.path.exists(bat):
            continue
        r = subprocess.run('call "%s" >nul && set' % bat, shell=True, capture_output=True, text=True)
        env = dict(os.environ)
        for line in r.stdout.splitlines():
            k, _, v = line.partition('=')
            if k:
                env[k] = v
        return env
    return None


def lp64(text):
    text = re.sub(r'\blong\s+long\b', '\x01', text)
    text = re.sub(r'\blong\b', 'long long', text)
    return text.replace('\x01', 'long long')


def main():
    tmp = os.path.join(tempfile.gettempdir(), 'spawnerdesk')
    os.makedirs(tmp, exist_ok=True)
    for name in ('spawner.c', 'spawner_stats.c'):
        src = open(os.path.join(CPP, name), encoding='utf-8').read()
        open(os.path.join(tmp, 'src_' + name), 'w', encoding='utf-8').write(lp64(src))
    out = os.path.join(tmp, 'spawnerdesk.exe')
    flags = ['-std=c11', '-O0', '-g', '-w', '-D_CRT_SECURE_NO_WARNINGS', '-DISAAC_DIAG=%s' % os.environ.get('DIAG', '1'),
             '-I', CPP, '-I', HERE, '-I', os.path.join(HERE, 'fake'), '-I', tmp, '-include', os.path.join(HERE, 'prelude.h')]
    cmd = [CLANG] + flags + ['-Wl,/errorlimit:0', os.path.join(HERE, 'desk.c'), os.path.join(HERE, 'stats_tu.c'), '-o', out]
    env = msvc_env()
    r = subprocess.run(cmd, env=env)
    if r.returncode:
        sys.exit('build failed')
    sys.exit(subprocess.run([out] + sys.argv[1:]).returncode)


if __name__ == '__main__':
    main()
