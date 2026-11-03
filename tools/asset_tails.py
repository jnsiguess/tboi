# -*- coding: utf-8 -*-
"""THE APK'S ASSETS CARRY NO REFERENCE COPIES (plan 30.09, M12 p.6).

The pack is guarded against the tails of reference copies (pack.py drops
`.before-`), the assets folder was not: port_touch_editor.anm2.bak rode in the
APK. A reference copy is kept beside what it guards (CLAUDE.md, "keep a
reference"), and that is exactly why it must never ship: it is a second file
at a path the port never asks for, and the first thing a player exporting the
port's files would take for the real one.

    py -3 tools/asset_tails.py            # list them; exit 1 if there are any
    import asset_tails; asset_tails.check(folder)   # the same for build.py

A tail is: .bak, .orig, .tmp, a `.before-` anywhere in the name, `REFERENCE`
anywhere in the name (any case). The check refuses; moving the file out is
left to whoever put it there - never deleted here, it may be the only copy.
"""
import os
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
ASSETS = os.path.join(ROOT, 'port', 'app', 'src', 'main', 'assets')
ENDINGS = ('.bak', '.orig', '.tmp')


def is_tail(name):
    low = name.lower()
    return low.endswith(ENDINGS) or '.before-' in low or 'reference' in low


def check(folder=ASSETS):
    """Every reference copy under folder, as paths relative to it."""
    found = []
    for here, _dirs, files in os.walk(folder):
        for f in files:
            if is_tail(f):
                found.append(os.path.relpath(os.path.join(here, f), folder))
    return sorted(found)


def main():
    found = check()
    if not found:
        print('assets: no reference copies')
        return 0
    print('assets: reference copies would ride in the APK - move them out of %s:' % ASSETS)
    for f in found:
        print('  ' + f)
    return 1


if __name__ == '__main__':
    sys.exit(main())
