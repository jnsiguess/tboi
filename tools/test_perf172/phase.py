"""THE GRID ON THE PANEL'S BEAT (20.10.2026): what reaches the screen, not what is drawn.
A Snapdragon 8s Gen 3 counted 59 frames a second in its report and its player saw 45-50
(reports/2026-10-19/8sgen3). The worlds here are that phone: 60 Hz (and LTPO 60/120), a frame's
CPU 7 ms and card 2.5 ms (its cores at a third of their clock), and a room's loading of 60-130 ms
every few seconds. Per build: frames drawn, pictures shown a second, frames never shown, vsyncs
that showed the old picture again while new ones were being drawn.
    py -3 tools/test_perf172/phase.py"""
import os, sys, random
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import run as R
sys.path.insert(0, os.path.join(R.ROOT, 'разбор', '2026-10-08'))
sys.path.insert(0, os.path.join(R.ROOT, 'разбор', '2026-10-11'))
import frame60_model as F
import speed_model as S

T = 60 * 1_000_000_000


def phone(c_ms, g_ms, every_s, stall_ms):
    def f(rng, k):
        c = rng.gauss(c_ms, c_ms * 0.15)
        if k and k % int(every_s * 60) == 0:
            c += rng.uniform(*stall_ms)
        return c, rng.gauss(g_ms, g_ms * 0.10)
    return f


def shown(pics, n_frames, t_end):
    """pictures shown a second, frames never shown, and the fewest new pictures in any second of play
    (seconds holding a room's loading left out: there nothing is drawn)"""
    by_latch = sorted(pics)
    frames = set(f for _, f in by_latch)
    secs = t_end / 1e9
    t0 = by_latch[0][0]
    per = {}
    for lt, f in by_latch:
        per[int((lt - t0) // 1_000_000_000)] = per.get(int((lt - t0) // 1_000_000_000), 0) + 1
    play = [v for k, v in per.items() if 0 < k < max(per) and v >= 50]
    return len(frames) / secs, n_frames - len(frames), (min(play) if play else 0)


def main():
    builds = []
    for ver, source in (('1.77', 'objc.c.before-resetsnap'), ('snap', 'objc.c')):
        R.cut_frameloop(source)
        builds.append((ver, R.build('test_phase_' + ver.replace('.', ''), ['test_frameloop.c'],
                                    ('FRAMELOOP_PACE=0', 'FRAMELOOP_PRESENT=1', 'FRAMELOOP_CAP=1',
                                     'FRAMELOOP_PICS=1'))))
    worlds = [('60 Hz, 7+2.5 ms, a room every 4 s', [(0, 60)], phone(7.0, 2.5, 4, (55, 130))),
              ('60 Hz, 9+3 ms, a room every 4 s', [(0, 60)], phone(9.0, 3.0, 4, (55, 130))),
              ('60 Hz, 4+2 ms, a room every 4 s', [(0, 60)], phone(4.0, 2.0, 4, (55, 130))),
              ('59.94 Hz, 7+2.5 ms', [(0, 59.94)], phone(7.0, 2.5, 4, (55, 130))),
              ('LTPO 60/120, 7+2.5 ms', 'ltpo', phone(7.0, 2.5, 4, (55, 130))),
              ('90 Hz, 7+2.5 ms', [(0, 90)], phone(7.0, 2.5, 4, (55, 130))),
              ('120 Hz, 7+2.5 ms', [(0, 120)], phone(7.0, 2.5, 4, (55, 130)))]
    print('%-38s | %s' % ('world', ' | '.join('%-44s' % v for v, _ in builds)))
    print('%-38s | %s' % ('', ' | '.join('%-44s' % 'drawn/s shown/s never shown  speed' for _ in builds)))
    worst = {}
    for i, (name, segs, work) in enumerate(worlds):
        cells = []
        for seed in (11, 12, 13):
            sg = S.ltpo(seed, [60, 120], (0.5, 4.0), T) if segs == 'ltpo' else segs
            w = F.World(seed + 100 * i, t_sim=T, work=work, segs=sg)
            feed = R.world_feed(w)
            row = []
            for v, exe in builds:
                st, pics = R.run_loop(exe, feed, 3)
                t_end = st[-1] - st[0]
                sps, never, low = shown(pics, len(st), t_end)
                m = R.speed_measure(st, pics)
                row.append((len(st) / (t_end / 1e9), sps, never, m['speed'], low))
            cells.append(row)
        avg = [tuple(sum(c[j][k] for c in cells) / len(cells) for k in range(4)) + (min(c[j][4] for c in cells),)
               for j in range(len(builds))]
        print('%-38s | %s' % (name, ' | '.join('%5.1f   %5.1f   %6.0f       %5.1f%%   worst s %2d' % a
                                                 for a in avg)), flush=True)


if __name__ == '__main__':
    main()
