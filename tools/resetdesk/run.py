# -*- coding: utf-8 -*-
"""The reset desk (08.10.2026): the REAL reset.c built for Windows with host clang
and the MSVC runtime, the run restart's one holder - the finger on the pause
button OR the keyboard's restart key (R) - driven against a model of the game's
0x88d988 and its fade (desk.c). Nothing touches the phone.

The only change made to the source on the way in is the width of `long`: the
port is LP64 (Android), Windows is LLP64, so `long` becomes `long long` in the
desk's copy (src_reset.c, written to the temp folder and not kept). Built and
run twice: ISAAC_DIAG=0 (the build we play on) and ISAAC_DIAG=1 (the report's).

    py -3 tools/resetdesk/run.py
    DESK_LOG=1 py -3 tools/resetdesk/run.py      reset.c's log lines too

1.74: each mode also builds the real-table desk (tables_cases.c): objc.c's own
shim_keys_restart_bound over the image's defaults and the fixtures' port and PC
inputconfigs.dat.
    py -3 tools/resetdesk/run.py --objc port/app/src/main/cpp/objc.c.before-174kbd         --uikit port/app/src/main/cpp/uikit.c.before-174kbd      the same cases on 1.73's objc.c and uikit.c
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


IMAGE = os.path.normpath(os.path.join(HERE, '..', '..', 'work', 'isaac.arm64'))
FIXTURES = os.path.join(HERE, 'fixtures')
T_ACTIONS, T_SLOTS = 34, 2          # the keyboard device's rows and slots (the 1.73 report: "actions 34 slots 2")


def keyboard_attrs(path):
    """The <Controller Name="Keyboard"> "a-k" attributes of an inputconfigs.dat, in file order."""
    text = open(path, encoding='utf-8', errors='replace').read()
    m = re.search(r'<Controller\s+Name="Keyboard"([^>]*)', text)
    return [(int(a), int(k), int(v)) for a, k, v in re.findall(r'(\d+)-(\d+)="(-?\d+)"', m.group(1))]


def write_tables(tmp, objc):
    """tables.h and src_objc_keys.c for the real-table desk (1.74)."""
    import struct
    img = open(IMAGE, 'rb').read()
    hid = struct.unpack_from('<256i', img, 0x945ad4)
    kb = struct.unpack_from('<56i', img, 0xa9c6e8)          # the keyboard's defaults, 28 pairs
    pad = struct.unpack_from('<74i', img, 0xa9c5c0)         # [0xabec80]: what the loader fills a record with, 37 pairs

    def pairs(name, v):
        return 'static const int %s[%d][2] = {%s};\n' % (name, len(v) // 2,
                                                       ', '.join('{%d, %d}' % (v[i], v[i + 1]) for i in range(0, len(v), 2)))

    def attrs(name, v):
        return 'static const int %s[%d][3] = {%s};\n' % (name, len(v), ', '.join('{%d, %d, %d}' % t for t in v))
    h = "/* written by run.py: the image's tables and the fixtures */\n"
    h += '#define T_ACTIONS %d\n#define T_SLOTS %d\n' % (T_ACTIONS, T_SLOTS)
    h += 'static const int T_HID_TO_GLFW[256] = {%s};\n' % ', '.join(str(x) for x in hid)
    h += pairs('T_KB_PAIRS', kb) + pairs('T_PAD_PAIRS', pad)
    h += attrs('T_PORT_ATTRS', keyboard_attrs(os.path.join(FIXTURES, 'port-inputconfigs.dat')))
    h += attrs('T_PC_ATTRS', keyboard_attrs(os.path.join(FIXTURES, 'pc-inputconfigs.dat')))
    h += '#define DESK_HAS_TURN %d\n' % ('kb_table_to_ios' in open(objc, encoding='utf-8').read())
    open(os.path.join(tmp, 'tables.h'), 'w', encoding='utf-8').write(h)
    # objc.c from the device's constants to the end of shim_keys_restart_bound
    src = open(objc, encoding='utf-8').read()
    a = src.index('#define KB_INPUT')
    b = src.index('int shim_keys_restart_bound(int hid) {', a)
    b = src.index('\n}\n', b) + 3
    open(os.path.join(tmp, 'src_objc_keys.c'), 'w', encoding='utf-8').write(lp64(src[a:b]))
    # uikit.c's key queue: from its event type to the end of shim_key_next_ex
    ui = sys.argv[sys.argv.index('--uikit') + 1] if '--uikit' in sys.argv else os.path.join(CPP, 'uikit.c')
    src = open(ui, encoding='utf-8').read()
    a = src.index('typedef struct { int code, down, mods, ch, repeat; } kevent_t;')
    b = src.index('int shim_key_next(int *code', a)
    open(os.path.join(tmp, 'src_uikit_keys.c'), 'w', encoding='utf-8').write(lp64(src[a:b]))


def build(diag, env, objc=None):
    tmp = os.path.join(tempfile.gettempdir(), 'resetdesk')
    os.makedirs(tmp, exist_ok=True)
    src = open(os.path.join(CPP, 'reset.c'), encoding='utf-8').read()
    open(os.path.join(tmp, 'src_reset.c'), 'w', encoding='utf-8').write(lp64(src))
    out = os.path.join(tmp, 'resetdesk_diag%d%s.exe' % (diag, '_table' if objc else ''))
    flags = ['-std=c11', '-O1', '-g', '-Wall', '-Wno-unused-function', '-Wno-unused-variable', '-Wno-format',
             '-D_CRT_SECURE_NO_WARNINGS', '-DISAAC_DIAG=%d' % diag,
             '-I', CPP, '-I', os.path.join(HERE, 'fake'), '-I', tmp,
             '-include', os.path.join(HERE, 'prelude.h')]
    if objc:
        write_tables(tmp, objc)
        flags.append('-DDESK_REAL_TABLE')
    cmd = [CLANG] + flags + [os.path.join(HERE, 'desk.c'), '-o', out]
    if subprocess.run(cmd, env=env).returncode:
        sys.exit('build failed (ISAAC_DIAG=%d)' % diag)
    return out


def main():
    env = msvc_env()
    worst = 0
    # --objc FILE: the real-table cases against another objc.c (objc.c.before-174kbd shows 1.73)
    objc = sys.argv[sys.argv.index('--objc') + 1] if '--objc' in sys.argv else os.path.join(CPP, 'objc.c')
    for diag in (0, 1):
        exe = build(diag, env)
        print('ISAAC_DIAG=%d:' % diag)
        worst = max(worst, subprocess.run([exe]).returncode)
        exe = build(diag, env, objc)
        print('ISAAC_DIAG=%d, the real table (%s):' % (diag, os.path.basename(objc)))
        worst = max(worst, subprocess.run([exe]).returncode)
    sys.exit(worst)


if __name__ == '__main__':
    main()
