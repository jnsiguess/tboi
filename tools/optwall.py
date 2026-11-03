# -*- coding: utf-8 -*-
"""The options paper's backing, stretched to match the longer scroll.

Animation Paper13, layer 0 ("Background Wall"): a 10x10 patch stretched by
YScale. 4200 covers local 0..420, which was enough while the travel was 160
(the visible bottom reached 430). The travel is now 176, so the bottom reaches
446 and the wall has to reach at least that: 4500 covers 0..450. XScale is left
alone, and the surplus is simply off-screen at scroll zero.

The kept copy is the one already beside the file (optionsmenu.anm2.orig); this
edits the live file in place, one line, and refuses if it does not find exactly
one YScale="4200" in that animation.
"""
import io
import os
import re
import sys

# M8 (plan 30.09): the game's files in work/app stay vanilla; the port's versions of
# them live in work/port-edit, and tools/port_patch.py makes port_patch/ out of them.
PROJECT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
ROOT = os.path.join(PROJECT, 'work', 'port-edit', 'repentance-resources')
REL = os.path.join('gfx', 'ui', 'main menu', 'optionsmenu.anm2')
WAS, NOW = 'YScale="4200"', 'YScale="4500"'


def main():
    check = '--check' in sys.argv
    for name in sorted(os.listdir(ROOT)):
        path = os.path.join(ROOT, name, REL)
        if not os.path.exists(path):
            continue
        t = io.open(path, encoding='utf-8').read()
        m = re.search(r'<Animation Name="Paper13".*?</Animation>', t, re.S)
        if not m:
            print('   %-10s no Paper13 - left alone' % name)
            continue
        a = m.group(0)
        if a.count(NOW) == 1 and a.count(WAS) == 0:
            print('   %-10s already 4500' % name)
            continue
        if a.count(WAS) != 1:
            print('   %-10s %d copies of %s - left alone' % (name, a.count(WAS), WAS))
            continue
        if check:
            print('   %-10s would go 4200 -> 4500' % name)
            continue
        t = t[:m.start()] + a.replace(WAS, NOW) + t[m.end():]
        io.open(path, 'w', encoding='utf-8', newline='').write(t)
        print('   %-10s the wall reaches 450' % name)


if __name__ == '__main__':
    main()
