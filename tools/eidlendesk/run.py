# -*- coding: utf-8 -*-
"""The EID length desk (07.10.2026): the REAL eid.c built for Windows with host
clang and the MSVC runtime, the game's functions answered by stubs at their own
addresses inside a fake image (desk.c), text measured with the real advances of
the description faces (the .fnt files in the APK's assets, as the engine's
0x8bdec8 sums them). It walks the player up to every entry of eid.bin in every
language and records how tall the walk-up panel is. Nothing touches the phone.

The only change made to the source on the way in is the width of `long`: the
port is LP64 (Android), Windows is LLP64, so `long` becomes `long long` in the
desk's copy (src_eid.c, written to the temp folder and not kept).

    py -3 tools/eidlendesk/run.py [desk arguments]
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


# desk-only experiments on the copy, never on the source:
#   DESK_BOX=180  the text box (the mod's TextboxWidth) wider than 130
#   DESK_PC=1     the mod's own vertical metrics: every line 11, no breath (LineHeight 11)
EXPERIMENTS = {
    'DESK_BOX': lambda v: [('#define M_BOX           130.0f', '#define M_BOX           %s.0f' % v)],
    'DESK_PC': lambda v: [('#define M_LINE          12.0f', '#define M_LINE          11.0f'),
                          ('#define M_PIC_LINE      20.0f', '#define M_PIC_LINE      11.0f'),
                          ('#define M_SECTION       16.0f', '#define M_SECTION       11.0f'),
                          ('#define M_FORM_AIR      3.0f', '#define M_FORM_AIR      0.0f')],
}


def build():
    tmp = os.path.join(tempfile.gettempdir(), 'eidlendesk')
    os.makedirs(tmp, exist_ok=True)
    src = open(os.path.join(CPP, 'eid.c'), encoding='utf-8').read()
    tag = ''
    for key, subs in EXPERIMENTS.items():
        if os.environ.get(key):
            tag += '_%s%s' % (key[5:].lower(), os.environ[key])
            for old, new in subs(os.environ[key]):
                if old not in src:
                    sys.exit('experiment %s: "%s" is not in eid.c any more' % (key, old))
                src = src.replace(old, new)
    open(os.path.join(tmp, 'src_eid.c'), 'w', encoding='utf-8').write(lp64(src))
    out = os.path.join(tmp, 'eidlendesk%s.exe' % tag)
    root = os.path.normpath(os.path.join(HERE, '..', '..')).replace(os.sep, '/')
    flags = ['-std=c11', '-O2', '-g', '-w', '-D_CRT_SECURE_NO_WARNINGS', '-DISAAC_DIAG=1',   # LOG on: the desk catches the description it carries
             '-DISAAC_ROOT="%s/"' % root, '-I', CPP, '-I', HERE, '-I', os.path.join(HERE, 'fake'), '-I', tmp,
             '-include', os.path.join(HERE, 'prelude.h')]
    cmd = [CLANG] + flags + ['-Wl,/errorlimit:0', os.path.join(HERE, 'desk.c'), '-o', out]
    if subprocess.run(cmd, env=msvc_env()).returncode:
        sys.exit('build failed')
    return out


def main():
    out = build()
    sys.exit(subprocess.run([out] + sys.argv[1:]).returncode)


if __name__ == '__main__':
    main()
