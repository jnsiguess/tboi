# -*- coding: utf-8 -*-
"""Hotfix 1.75, THE GAME'S OWN CLOCK: the owner's acceptance on the desk, in one short run.

On any panel of 60 Hz or more - 60, 75, 90, 120, 144, 165 - and while it switches (the S25 Ultra's
"Adaptive" 120.2 <-> 60.1, LTPO 60/90/120/144 every 50-300 ms) the game runs at 100 %, never more than
30.5 updates in a second, and no frame starts under 8 ms after the one before. The cut-out C of objc.c
(test_frameloop.c, run.py's builds), light (4 ms) and fight (11.5 ms, 1 % of frames 12-28 ms longer)
frames, 20 s a world; and the report build as it runs on the phone (FRAMELOOP_SIGPROF: the 1 ms sampler
signal), for 1.75 now and for the build the owner measured (objc.c.before-175sigprof, run175c.txt).

    py -3 tools/test_perf172/rates175.py
"""
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, '..', '..'))
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.join(ROOT, 'разбор', '2026-10-08'))
sys.path.insert(0, os.path.join(ROOT, 'разбор', '2026-10-11'))
import run as R                 # noqa: E402
import frame60_model as F       # noqa: E402
import speed_model as S         # noqa: E402

T = 20 * 1_000_000_000
VERS = [('1.74', 'objc.c.before-175speed', 0), ('1.75 as measured, report', 'objc.c.before-175sigprof', 1),
        ('1.75', 'objc.c', 0), ('1.75 report', 'objc.c', 1)]
RATES = [('60', lambda sd: [(0, 60)]), ('75', lambda sd: [(0, 75)]), ('90', lambda sd: [(0, 90)]),
         ('120', lambda sd: [(0, 120)]), ('144', lambda sd: [(0, 144)]), ('165', lambda sd: [(0, 165)]),
         ('S25 adaptive 120.2/60.1', lambda sd: S.ltpo(sd, [120.2, 60.1], (0.5, 4.0), T)),
         ('LTPO 60/90/120/144 50-300 ms', lambda sd: S.ltpo(sd, [60, 90, 120, 144], (0.05, 0.3), T))]
LOADS = [('light 4', lambda: F.steady(4.0, 3.0)), ('fight 11.5', lambda: F.spiky(11.5, 6.0, 2.0, 0.01, (12, 28)))]


def main():
    builds = []
    for ver, source, prof in VERS:
        R.cut_frameloop(source)
        builds.append((ver, R.build('rates_' + ''.join(c for c in ver if c.isalnum()), ['test_frameloop.c'],
                                    ('FRAMELOOP_PACE=2', 'FRAMELOOP_PRESENT=1', 'FRAMELOOP_CAP=%d' % (ver >= '1.75'),
                                     'FRAMELOOP_PICS=1', 'FRAMELOOP_SIGPROF=%d' % prof))))
    print('cell: speed | most updates in 1 s | quick starts a minute (< 8 ms) | fastest 2 s')
    print('%-29s %-10s | %s' % ('panel', 'frames', ' | '.join('%-27s' % v for v, _ in builds)))
    bad = []
    seed = 1755
    for rname, segs in RATES:
        for lname, load in LOADS:
            seed += 1
            w = F.World(seed, t_sim=T, work=load(), segs=segs(seed))
            feed = R.world_feed(w)
            cells = []
            for ver, exe in builds:
                st, _ = R.run_loop(exe, feed, 3)
                m = R.speed_measure(st)
                cells.append('%5.1f%% %4.1f %4.0f %5.1f%%' % (m['speed'], m['c1'] / 2.0, m['quick'], m['r120']))
                if ver in ('1.75', '1.75 report'):
                    if m['c1'] > 61 or m['quick'] > 0 or (lname == 'light 4' and m['speed'] < 99.5):
                        bad.append('%s %s %s' % (rname, lname, ver))
            print('%-29s %-10s | %s' % (rname, lname, ' | '.join('%-27s' % c for c in cells)), flush=True)
    print('1.75: %s' % ('every rate 100 %, <= 30.5 updates a second, no quick start - PASSED' if not bad
                        else 'FAILED: ' + ', '.join(bad)))
    return 1 if bad else 0


if __name__ == '__main__':
    sys.exit(main())
