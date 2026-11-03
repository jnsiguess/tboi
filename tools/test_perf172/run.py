# -*- coding: utf-8 -*-
"""Desk tests of hotfix 1.72's performance items. Host clang with the MSVC
runtime (vcvars64) and a small POSIX layer (desk/); the port's own files are
compiled as they are. Nothing touches the phone.

    py -3 tools/test_perf172/run.py            everything
    py -3 tools/test_perf172/run.py saves      the save writer only (unit + kill loop)
    py -3 tools/test_perf172/run.py pace|audio|craft|trace
    py -3 tools/test_perf172/run.py frameloop    the frame loop's wait against the desk model
                                                 (research notes of 08.10.2026, frame60_model.py); pace runs it too
    py -3 tools/test_perf172/run.py speed        the game's speed, 1.73 / 1.74 / 1.75 through the cut-out C:
                                                 the matrix, the x1.25 case, the hard cap (hotfix 1.75)

The kill loop: a child writes three files through the queue for ever and is
killed (TerminateProcess) at a random moment, forty times; after every kill
the start-up recovery runs and each file must be one whole version, never
older than the one seen before the kill.
"""
import os
import random
import shutil
import subprocess
import sys
import tempfile
import time

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, '..', '..'))
CPP = os.path.join(ROOT, 'port', 'app', 'src', 'main', 'cpp')
CLANG = shutil.which('clang') or r'C:\Program Files\LLVM\bin\clang.exe'
VCVARS = [r'C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Auxiliary\Build\vcvars64.bat',
          r'C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat']
OUT = os.path.join(tempfile.gettempdir(), 'test_perf172')
_ENV = None


def msvc_env():
    global _ENV
    if _ENV is not None:
        return _ENV
    for bat in VCVARS:
        if not os.path.exists(bat):
            continue
        r = subprocess.run('call "%s" >nul && set' % bat, shell=True, capture_output=True, text=True)
        env = dict(os.environ)
        for line in r.stdout.splitlines():
            k, _, v = line.partition('=')
            if k:
                env[k] = v
        _ENV = env
        return env
    sys.exit('no vcvars64.bat: the MSVC runtime is needed to link')


def build(name, sources, defines=()):
    os.makedirs(OUT, exist_ok=True)
    exe = os.path.join(OUT, name + '.exe')
    desk = os.path.join(HERE, 'desk')
    cmd = [CLANG, '-std=gnu11', '-O1', '-g', '-Wall', '-Wno-unused-function', '-Wno-unused-variable',
           '-Wno-unused-but-set-variable', '-Wno-deprecated-declarations', '-Wno-format',
           '-D_CRT_SECURE_NO_WARNINGS', '-DISAAC_DIAG=0', '-I', desk, '-I', CPP,
           '-I', OUT, '-include', os.path.join(desk, 'desk_posix.h')]
    cmd += ['-D' + d for d in defines]
    cmd += [os.path.join(HERE, s) if not os.path.isabs(s) else s for s in sources]
    cmd += [os.path.join(desk, 'desk_posix.c'), '-o', exe]
    r = subprocess.run(cmd, env=msvc_env(), capture_output=True, text=True)
    if r.returncode:
        print(r.stdout + r.stderr)
        sys.exit('build of %s failed' % name)
    warn = [l for l in (r.stdout + r.stderr).splitlines() if 'warning' in l and 'desk' not in l]
    for w in warn[:20]:
        print('  ' + w)
    return exe


def saves():
    exe = build('test_safefile', ['test_safefile.c'])
    work = os.path.join(OUT, 'saves')
    shutil.rmtree(work, ignore_errors=True)
    os.makedirs(work)
    print('== the save writer: unit')
    if subprocess.run([exe, 'unit', os.path.join(work, 'unit')]).returncode:
        return 1
    bad = 0
    for mode, kills in (('rename', 40), ('inplace', 25)):
        print('== the save writer: killed while writing, %d times (%s)' % (kills, mode))
        kd = os.path.join(work, 'kill-' + mode)
        os.makedirs(kd)
        seen = [0, 0, 0]
        rng = random.Random(1720)
        kills_mid = 0
        for i in range(kills):
            p = subprocess.Popen([exe, 'child', kd, mode], stdout=subprocess.PIPE, text=True)
            p.stdout.readline()                                       # "ready"
            time.sleep(rng.uniform(0.05, 0.9))
            kills_mid += any(n.endswith('.tmp') or n.endswith('.ready') for n in os.listdir(kd))
            p.kill()
            p.wait()
            r = subprocess.run([exe, 'check', kd], capture_output=True, text=True)
            parts = r.stdout.split()
            vers = [int(x) for x in parts[:3]]
            state = parts[3] if len(parts) > 3 else '?'
            left = int(parts[4]) if len(parts) > 4 else -1
            back = [k for k in range(3) if vers[k] >= 0 and vers[k] < seen[k]]
            if r.returncode or state != 'whole' or back or left:
                bad += 1
                print('  kill %d: versions %s (before %s) %s, %d leftovers%s' % (
                    i, vers, seen, state, left, ' WENT BACK' if back else ''))
            seen = [max(a, b) for a, b in zip(seen, vers)]
        print('  %d kills (%d with a .tmp or .ready on the disk just before): files at versions %s, %s' % (
            kills, kills_mid, seen, 'all whole, none went back' if not bad else '%d BAD' % bad))
    return 1 if bad else 0


def cut_pace():
    """objc.c's rule as it is: from PACE_WINDOW_NS to the end of pace_note, into pace_cut.h"""
    src = open(os.path.join(CPP, 'objc.c'), encoding='utf-8').read()
    a = src.index('#define PACE_WINDOW_NS')
    f = src.index('static void pace_note(', a)
    i = src.index('{', f)
    depth = 0
    while True:
        if src[i] == '{':
            depth += 1
        elif src[i] == '}':
            depth -= 1
            if depth == 0:
                break
        i += 1
    os.makedirs(OUT, exist_ok=True)
    with open(os.path.join(HERE, 'pace_cut.h'), 'w', encoding='utf-8') as out:
        out.write('/* cut out of objc.c by run.py - do not edit */\n' + src[a:i + 1] + '\n')


def pace():
    cut_pace()
    exe = build('test_pace', ['test_pace.c'])
    print("== the late start's rule")
    fails = subprocess.run([exe]).returncode
    # the same frames through the C rule and through the desk model's Pace: the same switches
    sys.path.insert(0, os.path.join(ROOT, 'разбор', '2026-10-07'))
    import pacing_model as PM
    rng = random.Random(172)
    mism = 0
    for case in range(20):
        lines, t, p = [], 1_000_000_000, PM.Pace()
        want = []
        level = rng.uniform(6, 22)
        for i in range(60 * 120):
            if i % 600 == 0:
                level = min(30.0, max(4.0, level + rng.uniform(-5, 5)))
            w = int(max(1.0, rng.gauss(level, level * 0.15) + (rng.uniform(50, 400) if rng.random() < 0.005 else 0)) * 1e6)
            t += max(16_666_666, w)
            was = p.wait
            p.note(w, t)
            if was != p.wait:
                want.append('%d %d' % (i, p.wait))
            lines.append('%d %d' % (w, t))
        r = subprocess.run([exe, 'replay'], input='\n'.join(lines) + '\n', capture_output=True, text=True)
        got = r.stdout.split('\n')
        got = [g for g in got if g.strip()]
        if got != want:
            mism += 1
            print('  case %d: C %s, model %s' % (case, got[:6], want[:6]))
    print('  20 random two-minute runs: C and the model switch on the same frames' if not mism
          else '  %d of 20 runs DIFFER' % mism)
    return fails + (1 if mism else 0) + frameloop()


def cut_frameloop(source):
    """objc.c's display link as it is - g_link .. link_wait_due, pace_note included - into
    frameloop_cut.h, with Android's LP64 longs (see lp64)"""
    import re
    src = open(os.path.join(CPP, source), encoding='utf-8').read().replace('\r\n', '\n')
    a = src.index('static AChoreographer *g_link;')
    b = src.index('#if ISAAC_DIAG\n/*\n * For the frames line (gl.c)', a)
    cut = src[a:b].replace('long long', '\x01')
    cut = re.sub(r'\blong\b', 'long long', cut).replace('\x01', 'long long')
    with open(os.path.join(HERE, 'frameloop_cut.h'), 'w', encoding='utf-8') as out:
        out.write('/* cut out of %s by run.py - do not edit */\n' % source + cut + '\n')


def frameloop():
    """The whole wait - objc.c's link_wait_due and pace_note, cut out as they are - over a fake
    Choreographer and buffer queue (test_frameloop.c), against the desk model
    (research notes of 08.10.2026, frame60_model.py) on the same worlds: every frame must start at the same
    nanosecond. Four versions: objc.c.before-175speed (1.74, and the picture time it names),
    objc.c.before-174speed (1.73), objc.c.before-60hz (1.72), objc.c.before-perf172 (1.71). And objc.c
    (1.75, the absolute schedule) on the same worlds - it has no desk model of its own: its hard cap
    must hold (no 1-second window over 61 starts, no 120 frames in under 1990 ms, no start under 8 ms
    after the one before); the most speed it gives up against 1.74 in one world is printed."""
    sys.path.insert(0, os.path.join(ROOT, 'разбор', '2026-10-08'))
    sys.path.insert(0, os.path.join(ROOT, 'разбор', '2026-10-11'))
    import frame60_model as F
    import speed_model as S                     # frame60_model's loops plus 1.74 (the speed cap)
    builds = []
    for ver, source, pace_mode in (('1.71', 'objc.c.before-perf172', 0), ('1.72', 'objc.c.before-60hz', 1),
                                   ('1.73', 'objc.c.before-174speed', 2), ('1.74', 'objc.c.before-175speed', 2),
                                   ('1.75', 'objc.c', 2)):
        if not os.path.exists(os.path.join(CPP, source)):
            print('  (no %s: %s skipped)' % (source, ver))
            continue
        cut_frameloop(source)
        builds.append((ver, build('test_frameloop_' + ver.replace('.', ''), ['test_frameloop.c'],
                                  ('FRAMELOOP_PACE=%d' % pace_mode,
                                   'FRAMELOOP_PRESENT=%d' % (ver >= '1.74'),
                                   'FRAMELOOP_CAP=%d' % (ver >= '1.75')))))
    print('== the frame loop: objc.c against the desk model, the same worlds')
    T = 30 * 1_000_000_000
    rng = random.Random(173)
    panels = [('60', lambda s: F.fixed(60), {}), ('59.94', lambda s: F.fixed(59.94), {}),
              ('50', lambda s: F.fixed(50), {}), ('90', lambda s: F.fixed(90), {}),
              ('120', lambda s: F.fixed(120), {}), ('144', lambda s: F.fixed(144), {}),
              ('60<->120 every 0.5-4 s', lambda s: F.ltpo(s, [60, 120], (0.5, 4.0), T), {}),
              ('60<->120 every 50-300 ms', lambda s: F.ltpo(s, [60, 120], (0.05, 0.3), T), {}),
              ('60/90/120/144', lambda s: F.ltpo(s, [60, 90, 120, 144], (0.5, 4.0), T), {}),
              ('120, app hears 60, told 120', lambda s: F.fixed(120), dict(every=2, told=int(1e9 / 120))),
              ('60, told 120', lambda s: F.fixed(60), dict(told=int(1e9 / 120))),
              ('120, told 60', lambda s: F.fixed(120), dict(told=F.P)),
              ('60, API < 30, old phone', lambda s: F.fixed(60), dict(told=None, lat=(2.0, 1.0), ts_noise=F.MS)),
              ('120.2', lambda s: F.fixed(120.2), {}), ('165', lambda s: F.fixed(165), {}),
              ('LTPO idle to 10/1 Hz', lambda s: S.idle(s, T), {}),
              ('VRR 48-165 per vsync', lambda s: S.vrr(s, 48, 165, T), {}),
              ('120 preempted 2%', lambda s: F.fixed(120), dict(_preempt=0.02))]
    bad = runs = 0
    cap_bad = 0
    worst_gap = 0.0
    for case in range(54):
        name, segs, extra = panels[case % len(panels)]
        c = rng.uniform(3, 22)
        g = rng.uniform(2, 20) if rng.random() < 0.4 else rng.uniform(2, 8)
        kw = dict(extra)
        pre = kw.pop('_preempt', 0)
        kw.setdefault('buffers', rng.choice((2, 3, 3)))
        seed = rng.randrange(1 << 30)
        world = F.World(seed, segs=segs(seed), t_sim=T, work=F.spiky(c, g, c * 0.15, 0.01, (10, 40)), **kw)
        if pre:
            S.preempt(world, seed, pre)
        lines = ['B %d %d %d' % (world.buffers, world.inflight, world.t_sim),
                 'A %d' % len(world.app)] + ['%d %d' % x for x in zip(world.app, world.app_ts)]
        lines += ['L %d' % len(world.latch)] + ['%d' % x for x in world.latch]
        lines += ['E %d' % len(world.events)] + ['%d %d' % x for x in world.events]
        lines += ['T %d' % len(world.lat)] + ['%d' % x for x in world.lat]
        lines += ['W %d' % len(world.C)] + ['%d %d' % x for x in zip(world.C, world.G)]
        feed = '\n'.join(lines) + '\n'
        speed74 = None
        for ver, exe in builds:
            if ver == '1.75':                   # no model: the hard cap and no quick start (THE GAME'S OWN CLOCK:
                st, _ = run_loop(exe, feed, 3)  # no catch-up, so a hitch's lateness is lost - the gap to 1.74 is shown)
                m = speed_measure(st)
                worst_gap = max(worst_gap, (speed74 or 0) - m['speed'])
                if m['c1'] > 61 or m['r120'] > 100.51 or m['quick'] > 0:
                    cap_bad += 1
                    print('  %-28s C %4.1f G %4.1f, 1.75: %.1f%% (1.74 %.1f%%), %d starts in a second, fastest'
                          ' 2 s %.2f%%' % (name, c, g, m['speed'], speed74 or 0, m['c1'], m['r120']))
                continue
            loop = S.Loop(world, ver)
            r = loop.run()
            out = subprocess.run([exe], input=feed, capture_output=True, text=True).stdout.split()
            got = [int(x) for x in out if x.lstrip('-').isdigit()]
            if 'pace' in out:
                got = [int(x) for x in out[:out.index('pace')]]
            pace_c = out[out.index('pace') + 1] if 'pace' in out else None
            if ver == '1.74':                   # start, the picture's time, its lead: all three must agree
                want = [x for t in zip(r['starts'], loop.frame_due, loop.frame_near) for x in t]
            else:
                want = r['starts']
            if ver == '1.74':
                speed74 = speed_measure(r['starts'])['speed']
            runs += 1
            same = got == want and (pace_c is None or int(pace_c) == loop.g_pace_wait)
            if not same:
                bad += 1
                k = next((i for i, (a, b) in enumerate(zip(got, want)) if a != b),
                         min(len(got), len(want)))
                per = 3 if ver == '1.74' else 1
                print('  %-28s C %4.1f G %4.1f, %s: DIFFER at frame %d (C %d frames, model %d)' % (
                    name, c, g, ver, k // per, len(got) // per, len(r['starts'])))
                a0 = max(0, k - k % per - 2 * per)
                print('    C     %s' % got[a0:a0 + 4 * per])
                print('    model %s' % want[a0:a0 + 4 * per])
    if bad:
        print('  %d of %d runs DIFFER' % (bad, runs))
    else:
        print('  %d runs (%d worlds x %d versions, 30 s each, 60/59.94/50/90/120/120.2/144/165 Hz, mode'
              ' switches, LTPO idling to 10 and 1 Hz, VRR, wrong refresh callbacks, an old phone, a preempted'
              ' thread): every frame starts at the same nanosecond in C and in the model (1.74: and names'
              ' the same picture time)' % (runs, runs // max(1, len(builds) - 1), len(builds) - 1))
    print('  1.75 (objc.c) on the same worlds: %s; the most speed below 1.74 in one world: %.1f points' % (
        'the hard cap holds, no quick start' if not cap_bad else '%d BAD' % cap_bad, worst_gap))
    return 1 if bad or cap_bad else 0


# ---------------- the game's speed (hotfix 1.75) ----------------

def world_feed(world):
    """a desk-model world (frame60_model.World) as test_frameloop.c reads it on stdin"""
    lines = ['B %d %d %d' % (world.buffers, world.inflight, world.t_sim),
             'A %d' % len(world.app)] + ['%d %d' % x for x in zip(world.app, world.app_ts)]
    lines += ['L %d' % len(world.latch)] + ['%d' % x for x in world.latch]
    lines += ['E %d' % len(world.events)] + ['%d %d' % x for x in world.events]
    lines += ['T %d' % len(world.lat)] + ['%d' % x for x in world.lat]
    lines += ['W %d' % len(world.C)] + ['%d %d' % x for x in zip(world.C, world.G)]
    return '\n'.join(lines) + '\n'


def run_loop(exe, feed, per):
    """every frame start the cut-out C made (per: numbers a frame prints), and the pictures shown
    (latch, frame) when it was built with FRAMELOOP_PICS"""
    r = subprocess.run([exe], input=feed, capture_output=True, text=True)
    out = r.stdout.split()
    if 'pace' in out:
        out = out[:out.index('pace')]
    starts = [int(x) for x in out][::per]
    pics, held, timed = [], [], []
    for line in r.stderr.splitlines():
        a = line.split()
        if len(a) == 2 and a[0].isdigit():
            pics.append((int(a[1]), int(a[0])))
        elif len(a) == 2 and a[0] == 'h':
            held.append(int(a[1]))
        elif len(a) == 2 and a[0] == 'p':
            timed.append(int(a[1]))
    run_loop.held = held
    run_loop.timed = sum(timed) * 100.0 / len(timed) if timed else 0.0
    return starts, pics


P_NS = 1_000_000_000 // 60


def speed_measure(st, pics=()):
    """speed = frames / 60 over the run; c1 = the most starts in any 1-second window (half-open, as the
    report's pace_rate_note counts them); r120 / r300 = the fastest 120 / 300 frame intervals, start to
    start, as a speed (2 s / 5 s); w2 = the speed in consecutive 2-second windows, w2min the slowest;
    rush and judder of the pictures as speed_model.py counts them"""
    n = len(st)
    m = dict(speed=(n - 1) * P_NS / (st[-1] - st[0]) * 100.0 if n > 1 else 0.0)
    best, j = 0, 0
    for i, t in enumerate(st):
        while st[j] <= t - 1_000_000_000:
            j += 1
        if t - st[0] >= 1_000_000_000:
            best = max(best, i - j + 1)
    m['c1'] = best
    m['quick'] = sum(1 for a, b in zip(st, st[1:]) if b - a < 8_000_000) / max(1e-9, (st[-1] - st[0]) / 60e9)
    late = [t for t in st if t >= st[0] + 3_000_000_000]           # past the first 3 s: a mode found, not looked for
    m['speed3'] = (len(late) - 1) * P_NS / (late[-1] - late[0]) * 100.0 if len(late) > 1 else m['speed']
    for k, key in ((120, 'r120'), (300, 'r300')):
        m[key] = max((k * P_NS / (st[i] - st[i - k]) * 100.0 for i in range(k, n)), default=0.0)
    w, j, t0 = [], 0, st[0]
    while t0 + 2_000_000_000 <= st[-1]:
        k = j
        while k < n and st[k] < t0 + 2_000_000_000:
            k += 1
        w.append((k - j) / 120.0 * 100.0)
        j, t0 = k, t0 + 2_000_000_000
    m['w2'] = w
    m['w2min'] = min(w) if w else m['speed']
    rush = 0
    dev = []
    for (t0, n0), (t1, n1) in zip(pics, pics[1:]):
        dr, dg = t1 - t0, (n1 - n0) * P_NS
        if dg <= 0:
            continue
        dev.append(abs(dr - dg))
        rush += dr <= 0.625 * dg
    secs = (st[-1] - st[0]) / 1e9 if n > 1 else 1.0
    m['rush'] = rush / secs * 60.0
    m['judder'] = sum(dev) / len(dev) / 1e6 if dev else 0.0
    return m


def speed():
    """Hotfix 1.75: the game's speed through the cut-out C of three versions - objc.c.before-174speed
    (1.73), objc.c.before-175speed (1.74), objc.c (1.75) - on one matrix of worlds (desk-model worlds,
    research notes of 08.10.2026, frame60_model.py): panels 60..165 Hz, LTPO, VRR; light, fight and heavy frames;
    callbacks on time, with noisy stamps, with stamps 4 ms ahead of the clock, delivered late (4 and 8 ms),
    carrying the vsync before, and the app hearing every second vsync. And the x1.25 case: a strong phone whose callbacks are late
    for 10 s, then on time for 10 s.
    The checks (the research notes of 13.10.2026, speed in 1.75) must FAIL on 1.74 and PASS on 1.75:
      A  late callbacks cost no speed: in the x1.25 case every 2-second window is 97 % or more
      B  the hard cap: no 1-second window over 61 starts, no 120 frame intervals faster than 100.5 %
      C  never faster than iOS over a run (100.1 %)"""
    sys.path.insert(0, os.path.join(ROOT, 'разбор', '2026-10-08'))
    sys.path.insert(0, os.path.join(ROOT, 'разбор', '2026-10-11'))
    import frame60_model as F
    import speed_model as S
    MS = F.MS
    vers = [('1.73', 'objc.c.before-174speed'), ('1.74', 'objc.c.before-175speed'),
            ('1.75', 'objc.c.before-panelwake'), ('1.76', 'objc.c.before-resetsnap'),   # '1.76' = shipped 1.77
            ('1.78', 'objc.c')]   # THE GRID ON THE PANEL'S BEAT (20.10.2026)
    builds = []
    for ver, source in vers:
        cut_frameloop(source)
        builds.append((ver, build('test_speed_' + ver.replace('.', ''), ['test_frameloop.c'],
                                  ('FRAMELOOP_PACE=%d' % (2 if ver <= '1.74' else 0),   # pace_note gone 14.10
                                   'FRAMELOOP_PRESENT=%d' % (ver >= '1.74'),
                                   'FRAMELOOP_CAP=%d' % (ver >= '1.75'), 'FRAMELOOP_PICS=1'))))
    per = {'1.73': 1, '1.74': 3, '1.75': 3, '1.76': 3, '1.78': 3}
    quick = 'quick' in sys.argv
    T = (10 if quick else 20) * 1_000_000_000

    def late(world, seed, mean_ms, phase_s=0.0):
        """callbacks delivered mean_ms +- mean_ms/2 after their vsync - for ever, or in phases of
        phase_s seconds by turns (the k-th callback taken as k/60 s: one a frame)"""
        rng = random.Random(seed ^ 0x175)
        out = []
        for k, x in enumerate(world.lat):
            on = not phase_s or int(k / 60.0 / phase_s) % 2 == 1
            out.append(x + (int(abs(rng.gauss(mean_ms, mean_ms / 2.0)) * MS) if on else 0))
        world.lat = out
        return world

    panels = [('60', [(0, 60)]), ('59.94', [(0, 59.94)]), ('60.3', [(0, 60.3)]), ('90', [(0, 90)]),
              ('120', [(0, 120)]), ('120.2 S25', [(0, 120.2)]), ('144', [(0, 144)]), ('165', [(0, 165)]),
              ('LTPO 60/120', 'ltpo'), ('VRR 48-165', 'vrr')]
    loads = [('light 4', lambda: F.steady(4.0, 3.0)), ('fight 11.5', lambda: F.spiky(11.5, 6.0, 2.0, 0.01, (12, 28))),
             ('heavy 18', lambda: F.steady(18.0, 6.0))]
    calls = ['on time', 'stamps +-1 ms', 'stamps 4 ms ahead', 'late 4 ms', 'late 8 ms', 'stamp a vsync old',
             'every 2nd vsync']
    throttled = ['every 2nd heard', 'every 2nd, SF too', 'every 3rd heard']
    total = {v: dict(speed=[], c1=0, r120=0.0, r300=0.0, rush=[], judder=[], w2min=200.0) for v, _ in builds}
    rows = []
    seed = 1750
    print('== the game\'s speed through the cut-out C (%d s a world): speed | 1 s max starts | fastest 2 s |'
          ' slowest 2 s | rushed pictures a minute | judder ms' % (T // 10 ** 9))
    print('%-11s %-10s %-17s | %s' % ('panel', 'load', 'callbacks', ' | '.join('%-37s' % v for v, _ in builds)))
    for pname, segs in panels:
        for lname, load in loads:
            for cname in calls:
                seed += 1
                sg = segs
                if segs == 'ltpo':
                    sg = S.ltpo(seed, [60, 120], (0.5, 4.0), T)
                elif segs == 'vrr':
                    sg = S.vrr(seed, 48, 165, T)
                kw = dict(segs=sg)
                if cname == 'stamps +-1 ms':
                    kw.update(ts_noise=MS, jitter=300_000)
                if cname == 'every 2nd vsync':
                    kw.update(every=2)
                w = F.World(seed, t_sim=T, work=load(), **kw)
                if cname.startswith('late'):
                    late(w, seed, float(cname.split()[1]))
                if cname == 'stamp a vsync old':
                    w.app_ts = [w.app_ts[0]] + w.app_ts[:-1]
                if cname == 'stamps 4 ms ahead':
                    w.app_ts = [x + 4 * MS for x in w.app_ts]
                feed = world_feed(w)
                cells = []
                for v, exe in builds:
                    st, pics = run_loop(exe, feed, per[v])
                    m = speed_measure(st, pics)
                    tt = total[v]
                    tt['speed'].append(m['speed'])
                    tt['rush'].append(m['rush'])
                    tt['judder'].append(m['judder'])
                    tt['c1'] = max(tt['c1'], m['c1'])
                    tt['r120'] = max(tt['r120'], m['r120'])
                    tt['r300'] = max(tt['r300'], m['r300'])
                    tt['w2min'] = min(tt['w2min'], m['w2min'])
                    cells.append(m)
                rows.append((pname, lname, cname, cells))
                print('%-11s %-10s %-17s | %s' % (pname, lname, cname, ' | '.join(
                    '%5.1f%% %2d %5.1f%% %5.1f%% %4.0f %4.1f' % (m['speed'], m['c1'], m['r120'], m['w2min'],
                                                                  m['rush'], m['judder']) for m in cells)), flush=True)
    print()
    print('over the matrix (%d worlds):' % len(rows))
    for v, _ in builds:
        tt = total[v]
        print('  %s  mean speed %.2f%%, slowest world %.1f%%, slowest 2 s %.1f%% | most starts in 1 s %d,'
              ' fastest 2 s %.2f%%, fastest 5 s %.2f%%, fastest run %.2f%% | rushed pictures %.0f a minute,'
              ' judder %.2f ms' % (v, sum(tt['speed']) / len(tt['speed']), min(tt['speed']), tt['w2min'], tt['c1'],
                                   tt['r120'], tt['r300'], max(tt['speed']), sum(tt['rush']) / len(tt['rush']),
                                   sum(tt['judder']) / len(tt['judder'])))
    by_calls = {}
    for pname, lname, cname, cells in rows:
        for (v, _), m in zip(builds, cells):
            by_calls.setdefault((cname, v), []).append(m['speed'])
    print('mean speed by how the callbacks come:')
    for cname in calls:
        print('  %-17s %s' % (cname, '  '.join('%s %5.1f%%' % (v, sum(by_calls[(cname, v)]) / len(by_calls[(cname, v)]))
                                              for v, _ in builds)))

    # the system's frame-rate limit (the owner's decision of 13.10.2026: bypass it): the app hears every
    # Nth vsync (refresh callback: the panel's rate); 'SF too': the compositor latches every second vsync as well
    print()
    print('the system\'s frame-rate limit: speed | slowest 2 s | 1 s max starts | fastest 2 s | present held avg / max ms'
          ' | judder ms')
    print('%-6s %-10s %-18s | %s' % ('panel', 'load', 'limit', ' | '.join('%-44s' % v for v, _ in builds)))
    thr = {}
    for hz in (60, 90, 120):
        for lname, load in loads:
            for tname in throttled:
                seed += 1
                every = 3 if '3rd' in tname else 2
                w = F.World(seed, t_sim=T, work=load(), segs=[(0, hz)], every=every)
                if 'SF' in tname:
                    w.latch = w.latch[::2]
                feed = world_feed(w)
                cells = []
                for v, exe in builds:
                    st, pics = run_loop(exe, feed, per[v])
                    held = run_loop.held or [0]
                    m = speed_measure(st, pics)
                    m['held_avg'] = sum(held) / len(held) / 1e6
                    m['held_max'] = max(held) / 1e6
                    thr[(hz, lname, tname, v)] = m
                    tt = total[v]
                    tt['c1'] = max(tt['c1'], m['c1'])
                    tt['r120'] = max(tt['r120'], m['r120'])
                    cells.append(m)
                print('%-6s %-10s %-18s | %s' % (hz, lname, tname, ' | '.join(
                    '%5.1f%% %5.1f%% %2d %5.1f%% %4.1f/%5.1f %4.1f' % (m['speed'], m['w2min'], m['c1'], m['r120'],
                                                                      m['held_avg'], m['held_max'], m['judder'])
                    for m in cells)), flush=True)

    # THE GAME'S OWN CLOCK (the owner's acceptance, 13.10.2026): on any panel of 60 Hz or more, and while it
    # switches, exactly 100 %, no second over 30.5 updates, no quick starts. And the report build as the S25
    # ran it (the 1 ms sampler's signal): the build the owner measured (objc.c.before-175sigprof) and now.
    print()
    print('every rate (the owner\'s acceptance): speed | most updates in 1 s | quick starts a minute (< 8 ms) |'
          ' fastest 2 s | pictures given their time')
    rb = list(builds)
    # '1.75 tested' (objc.c.before-175sigprof) dropped 19.10.2026: under the desk's sampler it no longer
    # finishes a world (an hour of CPU on the first) - it was only the archive of the build measured 14.10
    for ver, source in (('1.76', 'objc.c.before-resetsnap'), ('1.78', 'objc.c')):
        cut_frameloop(source)
        rb.append((ver + ' report', build('test_speed_' + ver.replace('.', '').replace(' ', '') + '_prof',
                                          ['test_frameloop.c'],
                                          ('FRAMELOOP_PACE=%d' % (2 if ver == '1.75 tested' else 0),
                                           'FRAMELOOP_PRESENT=1', 'FRAMELOOP_CAP=1',
                                           'FRAMELOOP_PICS=1', 'FRAMELOOP_SIGPROF=1'))))
    per.update({'1.76 report': 3, '1.78 report': 3})
    rates = [('60', lambda sd: [(0, 60)]), ('75', lambda sd: [(0, 75)]), ('90', lambda sd: [(0, 90)]),
             ('120', lambda sd: [(0, 120)]), ('144', lambda sd: [(0, 144)]), ('165', lambda sd: [(0, 165)]),
             ('S25 adaptive 120.2/60.1', lambda sd: S.ltpo(sd, [120.2, 60.1], (0.5, 4.0), T)),
             ('60/90/120/144 50-300 ms', lambda sd: S.ltpo(sd, [60, 90, 120, 144], (0.05, 0.3), T))]
    print('%-24s %-10s | %s' % ('panel', 'load', ' | '.join('%-30s' % v for v, _ in rb)))
    acc = {}
    for rname, segs in rates:
        for lname, load in loads[:2]:
            seed += 1
            w = F.World(seed, t_sim=T, work=load(), segs=segs(seed))
            feed = world_feed(w)
            cells = []
            for v, exe in rb:
                st, pics = run_loop(exe, feed, per[v])
                m = speed_measure(st, pics)
                m['timed'] = run_loop.timed
                acc[(rname, lname, v)] = m
                cells.append('%5.1f%% %4.1f %4.0f %5.1f%% %3.0f%%' % (m['speed'], m['c1'] / 2.0, m['quick'], m['r120'],
                                                                    m['timed']))
            print('%-24s %-10s | %s' % (rname, lname, ' | '.join('%-30s' % c for c in cells)), flush=True)

    # the x1.25 case: a strong phone (light frames), callbacks late for 10 s, then on time for 10 s, by turns
    print()
    print('the x1.25 case: light frames, callbacks delivered late (8 +- 4 ms on 120.2 Hz, 4 +- 2 on 90, 6 +- 3 on'
          ' 144) for 10 s, then on time for 10 s, by turns (60 s): the speed in every 2 s')
    case = {}
    for hz, lag in ((120.2, 8.0), (90, 4.0), (144, 6.0)):
        w = F.World(1251, t_sim=60 * 1_000_000_000, work=F.steady(4.0, 3.0), segs=[(0, hz)])
        late(w, 1251, lag, 10.0)
        feed = world_feed(w)
        for v, exe in builds:
            st, pics = run_loop(exe, feed, per[v])
            m = speed_measure(st, pics)
            case[(hz, v)] = m
            print('  %5.1f Hz %4.0f ms %s  mean %5.1f%%, 2 s windows %5.1f..%5.1f%% (x%.2f from the slowest to the'
                  ' fastest): %s' % (hz, lag, v, m['speed'], min(m['w2']), max(m['w2']), max(m['w2']) / min(m['w2']),
                                     ' '.join('%3.0f' % x for x in m['w2'])))

    # the checks: A and B must fail on 1.74, A, B and C pass on 1.75
    print()
    verdict = {}
    for v, _ in builds:
        tt = total[v]
        a = all(case[(hz, v)]['w2min'] >= 97.0 for hz in (120.2, 90, 144))
        b = tt['c1'] <= 61 and tt['r120'] <= 100.51
        c = max(tt['speed']) <= 100.1
        # D the system's frame-rate limit: where the phone's own work allows (light and fight), about 100 % once
        #   the limit is found (past the first 3 s: finding it takes two half-second windows of callbacks)
        # (only the callbacks throttled: about 100 %; the compositor throttled too: the swap may pace the loop,
        # but it must never hold the frame thread longer than two of its latches)
        d = (all(thr[(hz, ln, tn, v)]['speed3'] >= 97.0 for hz in (60, 90, 120) for ln in ('light 4', 'fight 11.5')
                 for tn in throttled if 'SF' not in tn)
             and all(m['held_max'] <= 70.0 for k, m in thr.items() if k[3] == v and 'SF' in k[2])
             and max(m['speed'] for k, m in thr.items() if k[3] == v) <= 100.1)
        verdict[v] = (a, b, c, d)
        print('  %s  A late callbacks cost no speed: %s | B the hard cap: %s | C never faster over a run: %s'
              ' | D the system\'s frame-rate limit bypassed: %s' % (
                  v, 'pass' if a else 'FAIL', 'pass' if b else 'FAIL', 'pass' if c else 'FAIL', 'pass' if d else 'fail'))
    # E the owner's acceptance: every rate and the switching, the clean build and the report build alike -
    #   light frames 99.5 % or more, no quick start, no second over 30.5 updates, no 2 s over 100.5 %
    e_tested = None
    for v in ('1.76', '1.76 report', '1.78', '1.78 report'):
        rows = [m for (rn, ln, vv), m in acc.items() if vv == v]
        e = (all(m['speed'] >= 99.5 for (rn, ln, vv), m in acc.items() if vv == v and ln == 'light 4')
             and all(m['quick'] == 0 and m['c1'] <= 61 and m['r120'] <= 100.51 for m in rows))
        print('  %-18s E every rate, 100 %%, no quick start, <= 30.5 updates a second: %s' % (v, 'pass' if e else 'FAIL'))
        if v == '1.76':
            e76 = e
        elif v == '1.76 report':
            e76 = e76 and e
        elif v == '1.78':
            e78 = e
        elif v == '1.78 report':
            e78 = e78 and e
        else:
            e_tested = e
    verdict['1.76'] = verdict['1.76'] + (e76,)
    verdict['1.78'] = verdict['1.78'] + (e78,)
    if e_tested is not None:
        print('  the build the owner measured (report, 1 ms sampler): %s' % ('passes (it should fail)' if e_tested
                                                                             else 'fails, as on his phone'))
    ok75 = all(verdict['1.75'][:4])
    ok76 = all(verdict['1.76'])
    print('  1.78: %s' % ('passes' if all(verdict['1.78']) else 'FAILS'))
    fails74 = not all(verdict['1.74'][:2])
    print('  the explicit test: %s on 1.74, %s on 1.75, %s on 1.76' % (
        'fails' if fails74 else 'PASSES (it should fail)', 'passes' if ok75 else 'FAILS', 'passes' if ok76 else 'FAILS'))
    return 0 if ok75 and ok76 and fails74 else 1


def lp64(name):
    """The port's file with Android's LP64 longs on the desk: Windows keeps long at 32 bits, and the port
    counts nanoseconds in long. Every long becomes long long and every L constant LL; nothing else changes."""
    import re
    src = open(os.path.join(CPP, name), encoding='utf-8').read()
    src = src.replace('long long', '\x01')
    src = re.sub(r'\blong\b', 'long long', src)
    src = src.replace('\x01', 'long long')
    src = re.sub(r'\b(\d+)UL\b', r'\1ULL', src)
    src = re.sub(r'\b(\d+)L\b', r'\1LL', src)
    os.makedirs(OUT, exist_ok=True)
    out = os.path.join(OUT, (name[:-2] if name.endswith('.c') else name) + '_lp64.c')
    open(out, 'w', encoding='utf-8').write('#line 1 "%s"\n' % name + src)
    return out


def cut_function(src, head):
    """the text of a C function from its head to its closing brace"""
    f = src.index(head)
    i = src.index('{', f)
    depth = 0
    while True:
        if src[i] == '{':
            depth += 1
        elif src[i] == '}':
            depth -= 1
            if depth == 0:
                return src[f:i + 1]
        i += 1


def craft():
    src = open(os.path.join(CPP, 'eid_craft.c'), encoding='utf-8').read()
    a = src.index('/* -- the filtered list')
    b = src.index('/* -- end of the filtered list -- */')
    with open(os.path.join(HERE, 'craft_cut.h'), 'w', encoding='utf-8') as out:
        out.write('/* cut out of eid_craft.c by run.py - do not edit */\n')
        out.write(cut_function(src, 'static int bag_used(') + '\n\n' + src[a:b] + '\n')
    return simple('test_craft', ['test_craft.c'])


def trace():
    src = open(os.path.join(CPP, 'trace.c'), encoding='utf-8').read().replace('\r\n', '\n')
    a = src.index('#define PAGES 8192')
    body = cut_function(src, 'static int readable(')
    b = src.index(body) + len(body)
    with open(os.path.join(HERE, 'trace_cut.h'), 'w', encoding='utf-8') as out:
        import re
        cut = src[a:b].replace('long long', '\x01')            # Android's LP64 longs (see lp64)
        cut = re.sub(r'\blong\b', 'long long', cut).replace('\x01', 'long long')
        out.write('/* cut out of trace.c by run.py - do not edit */\n' + cut + '\n')
    return simple('test_trace', ['test_trace.c'])


def pocket():
    import re
    src = open(os.path.join(CPP, 'hudlayout.c'), encoding='utf-8').read().replace('\r\n', '\n')
    a = src.index('typedef struct { unsigned long at; uint32_t v[4]; } word_t;')
    body = cut_function(src, 'static void pa_words(')
    b = src.index(body) + len(body)
    cut = src[a:b].replace('long long', '\x01')                # Android's LP64 longs (see lp64)
    cut = re.sub(r'\blong\b', 'long long', cut).replace('\x01', 'long long')
    with open(os.path.join(HERE, 'pocket_cut.h'), 'w', encoding='utf-8') as out:
        out.write('/* cut out of hudlayout.c by run.py - do not edit */\n' + cut + '\n')
    return simple('test_pocket', ['test_pocket.c'])


def simple(name, sources, defines=()):
    exe = build(name, sources, defines)
    print('== ' + name)
    return subprocess.run([exe]).returncode


def main():
    which = sys.argv[1:] or ['pace', 'audio', 'saves', 'craft', 'trace', 'pocket']
    fails = 0
    for w in which:
        if w == 'saves':
            fails += saves()
        elif w == 'pace':
            fails += pace()
        elif w == 'frameloop':
            fails += frameloop()
        elif w == 'speed':
            fails += speed()
        elif w == 'audio':
            lp64('audio.c')
            fails += simple('test_audio', ['test_audio.c'], ('AUDIO_LP64',))
            if os.path.exists(os.path.join(CPP, 'audio.c.before-perf172')):
                lp64('audio.c.before-perf172')
                simple('test_audio_old', ['test_audio.c'], ('AUDIO_OLD',))
        elif w == 'craft':
            fails += craft()
        elif w == 'trace':
            fails += trace()
        elif w == 'pocket':
            fails += pocket()
    print('ALL PASSED' if not fails else 'FAILED (%d)' % fails)
    sys.exit(1 if fails else 0)


if __name__ == '__main__':
    main()
