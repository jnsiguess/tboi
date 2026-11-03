# -*- coding: utf-8 -*-
"""What a persistent save says about Greedier - read only, nothing is written.

    py -3 tools/greedcheck.py rep_persistentgamedata1.dat [...]

The three cases it tells apart (plan item 2.8):
  - the greed machine count (counter 115) below 500   -> the progress was lost;
  - achievement 341 (Greedier) up                     -> it is the screen, not the save;
  - 500 or more and 341 down                          -> the load rule 0x700a14 grants
                                                          it on the next start by itself.

Beside it the ordinary donation machine (counter 20: 0x69de60 reads data+0x2f8,
0x69de70 adds one per coin), so the two machines are never mixed up, the
greed tier achievements 246..251, and the traces the old Stats+ probe could have
left (the probe items ticked in the collection, 330 "U broke it!").

The file format is work/saves/notes/isaacsave.py (the reader 0x6ff7f0).
"""
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, '..', 'work', 'saves', 'notes'))
import isaacsave  # noqa: E402

COUNTER_GREED_MACHINE = 115
COUNTER_DONATION_MACHINE = 20
GREEDIER = 341
GREED_665 = 249                       # the next tier; it also opens Greedier on load
STATS_PLUS_PROBES = (1, 6, 32, 183, 255, 564, 120)


def chunk(save, kind):
    return next(c['vals'] for c in save['chunks'] if c['type'] == kind)


def check(path):
    data, packed = isaacsave.load_bytes(path)
    save = isaacsave.parse(data)
    ach, cnt, col = chunk(save, 1), chunk(save, 2), chunk(save, 4)
    greed = cnt[COUNTER_GREED_MACHINE]
    rule = greed % 1000 > 499 or ach[GREED_665] != 0
    print(path, '(packed)' if packed else '(raw)', 'save counter', save['save_counter'])
    print('  greed machine, counter 115    =', greed, '  % 1000 =', greed % 1000)
    print('  donation machine, counter 20  =', cnt[COUNTER_DONATION_MACHINE])
    print('  achievements 246..251         =', [ach[i] for i in range(246, 252)])
    print('  341 Greedier                  =', ach[GREEDIER])
    print('  load rule grants Greedier     =', rule)
    if ach[GREEDIER]:
        verdict = 'Greedier is open in the file: look at the screen (SELECT! over the row), not the save'
    elif rule:
        verdict = 'Greedier is due: the load rule opens it on the next start'
    elif greed >= 500:
        verdict = ('the count passed 500 but the load rule looks at it modulo 1000; the machine itself '
                   'grants Greedier when the count reaches the next 500 of the thousand (the game\'s own way)')
    else:
        verdict = 'the greed machine count is below 500 in this file: the coins never reached it'
    print('  ->', verdict)
    print('  330 U broke it!               =', ach[330])
    print('  Stats+ probe items in the collection', dict((i, col[i]) for i in STATS_PLUS_PROBES))


def main():
    if len(sys.argv) < 2:
        sys.exit(__doc__)
    for path in sys.argv[1:]:
        check(path)


if __name__ == '__main__':
    main()
