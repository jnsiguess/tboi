# -*- coding: utf-8 -*-
"""RESTART TO APPLY after ADD MOD in the phone's own order of calls (hotfix 1.75; the owner on 1.74: "the restart
button when adding mods - you said you fixed it, and now it is gone COMPLETELY").

The 1.74 stands called usermods.c's list_put from C and ran UserMods.java with no layer behind it, so the road the
phone takes - UserMods.installAll -> listPut -> the add window's restartIsOwed, the page asking every frame, the disk's
look on a thread of its own, the start (setConfigDir) before or after the picked archives - was never played whole.
jnidesk.c + JniDesk.java play it: the real UserMods.java over the real usermods.c (the clean build's, ISAAC_DIAG=0),
real threads and real locks. Each case is one game process.

  new         new mods into a list of 2 / 20 / 60 (and 63: one fits, one does not)
  update-on   each an update of a listed mod that is on ("update")
  update-off  the same of a mod that is off
  beside      the same mod again, "beside it"
  again       the same archives twice in one run
  death       Android ended the game behind the picker: the archives reach a new process, whose start comes 0 / 0.8 /
              4 s later - and 21 s later, past the installer's 20 s wait for it

What must hold after every add that put a mod in: the add window has RESTART TO APPLY, the MODS page's row is there
at once and stays after the disk's look, and a pale name never stands without the row. A full list (64) adds nothing:
no restart is offered (the window says why, in red).

The same cases over 1.74's usermods.c (usermods.c.before-175restart): update-off and death past the wait have neither
the button nor the row - the cut this hotfix puts back.

    py -3 tools/umdesk/test_restart175.py
"""
import os
import re
import subprocess
import sys
import tempfile

sys.stdout.reconfigure(encoding='utf-8', errors='replace')
HERE = os.path.dirname(os.path.abspath(__file__))
T = tempfile.gettempdir()
JAVA = r'C:\Program Files\Android\Android Studio\jbr\bin\java.exe'
SDK = (os.environ.get('ANDROID_HOME') or os.environ.get('ANDROID_SDK_ROOT')
       or os.path.join(os.environ.get('LOCALAPPDATA', ''), 'Android', 'Sdk'))
AJ = os.path.join(SDK, 'platforms', 'android-35', 'android.jar')
BASH = r'C:\Program Files\Git\bin\bash.exe'
ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
SRC = os.path.join(ROOT, 'port', 'app', 'src', 'main', 'cpp', '').replace(os.sep, '/')   # it ends up in a C #include
failures = []

CASES = [('new', 2, 1), ('new', 20, 3), ('new', 60, 3), ('new', 63, 2), ('new', 64, 2),
         ('update-on', 20, 3), ('update-off', 2, 1), ('update-off', 20, 3), ('beside', 20, 2), ('again', 20, 2),
         ('death', 20, 3, 0), ('death', 20, 3, 800), ('death', 20, 3, 4000), ('death', 20, 3, 21000)]


def build(out, usermods_c=None):
    env = dict(os.environ, DESK_OUT=out, DESK_BUILD_ONLY='1')
    if usermods_c:
        env['DESK_USERMODS_C'] = usermods_c
    p = subprocess.run([BASH, os.path.join(HERE, 'run_jni.sh')], env=env, capture_output=True)
    if p.returncode:
        sys.exit('build failed:\n' + p.stdout.decode('utf-8', 'replace')[-3000:] + p.stderr.decode('utf-8', 'replace')[-3000:])


def run(out, case):
    work = os.path.join(T, 'jd175-' + os.path.basename(out))
    p = subprocess.run([JAVA, '-Dstdout.encoding=UTF-8', '-Dumdesk.jni=' + os.path.join(out, 'umjni.dll'), '-cp',
                        out + ';' + AJ, 'ru.isaac.port.JniDesk', work] + [str(x) for x in case], capture_output=True)
    text = p.stdout.decode('utf-8', 'replace') + p.stderr.decode('utf-8', 'replace')
    m = re.search(r'RESULT (.*)', text)
    if not m:
        return None, text[-2000:]
    got = dict(kv.split('=', 1) for kv in m.group(1).split() if '=' in kv and not kv.startswith(('index', 'on=')))
    return got, m.group(0)


def check(cond, what, detail=''):
    print(('  ok    ' if cond else '  FAIL  ') + what + ('' if cond or not detail else '\n        ' + str(detail)[:600]))
    if not cond:
        failures.append(what)


def owed_everywhere(r):
    return r['button'] == '1' and r['row_now'] == '1' and r['row_later'] == '1' and r['frame_row'] == '1'


now = os.path.join(T, 'umdesk-jni175')
was = os.path.join(T, 'umdesk-jni174')
build(now)
build(was, SRC + 'usermods.c.before-175restart')

print('== this usermods.c')
for case in CASES:
    what = ' '.join(str(x) for x in case)
    r, line = run(now, case)
    if r is None:
        check(False, what + ': the run gave its line', line)
        continue
    added = int(r['added'].split('/')[0])
    if added:
        check(owed_everywhere(r), '%s: %s added - the window\'s button, the row at once, after the disk\'s look, '
              'and on the frames' % (what, r['added']), line)
    else:
        check(r['button'] == '0' and r['row_later'] == '0', '%s: nothing added (the list is full) - no restart offered'
              % what, line)
    check(int(r['pale']) == 0 or r['row_later'] == '1', '%s: no pale name without the row' % what, line)

print('== 1.74\'s usermods.c (usermods.c.before-175restart): the roads it cut')
cut = 0
for case in CASES:
    what = ' '.join(str(x) for x in case)
    r, line = run(was, case)
    if r is None:
        check(False, what + ': the run gave its line (1.74)', line)
        continue
    added = int(r['added'].split('/')[0])
    lost = added and not owed_everywhere(r)
    if lost:
        cut += 1
        print('  1.74  %s: %s added, button=%s row=%s - RESTART TO APPLY nowhere' % (what, r['added'], r['button'],
                                                                                 r['row_later']))
check(cut >= 3, '1.74 loses the restart after a successful add on the roads above (update-off x2, death past the '
      'wait): %d cases' % cut)

print('\n%d failures' % len(failures))
sys.exit(1 if failures else 0)
