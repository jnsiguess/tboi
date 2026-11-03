# -*- coding: utf-8 -*-
"""Build and run the desk test of the Bag of Crafting's swipe and of the held
map's inventory under EID's switch (hotfix 1.72). Host clang with the MSVC
runtime (vcvars64); nothing touches the phone.

    py -3 tools/test_bagswipe/run.py
"""
import os
import shutil
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
CLANG = shutil.which('clang') or r'C:\Program Files\LLVM\bin\clang.exe'
VCVARS = [r'C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Auxiliary\Build\vcvars64.bat',
          r'C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat']


def msvc_env():
    """the C runtime's libraries for the linker: the environment vcvars64 sets"""
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


def main():
    out = os.path.join(tempfile.gettempdir(), 'test_bagswipe.exe')
    cmd = [CLANG, '-std=c11', '-O1', '-Wall', '-Wno-unused-function', '-Wno-unused-variable',
           '-Wno-int-to-pointer-cast', '-D_CRT_SECURE_NO_WARNINGS', '-DISAAC_DIAG=0',
           '-I', os.path.join(HERE, 'stubs'), '-include', os.path.join(HERE, 'prelude.h'),
           os.path.join(HERE, 'test_bagswipe.c'), '-o', out]
    if subprocess.run(cmd, env=msvc_env()).returncode:
        sys.exit('build failed')
    sys.exit(subprocess.run([out]).returncode)


if __name__ == '__main__':
    main()
