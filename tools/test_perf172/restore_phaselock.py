"""Re-applies THE GRID ON THE PANEL'S BEAT to objc.c (from objc.c.before-phaselock) - 21.10.2026, after objc.c
was found holding the 1.76 clock (an interrupted A/B build copied objc.c.before-panelwake over it)."""
import os
ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
P = os.path.join(ROOT, 'port', 'app', 'src', 'main', 'cpp', 'objc.c')
s = open(P, encoding='utf-8', newline='').read()
def rep(o, n):
    global s
    assert s.count(o) == 1, o[:70]
    s = s.replace(o, n)

rep("""static int link_wait_due(long long period, long long *due) {
    if (!g_link) return 0;""", """/*
 * THE GRID ON THE PANEL'S BEAT (20.10.2026, 1.77). The grid of 1.75 was anchored
 * on a vsync once and then kept its own time; after every frame late past a
 * quarter period - every room's loading - it restarted at "now", wherever that
 * fell between two vsyncs. The picture is timed half a vsync after its slot and
 * shown on the first vsync not before that time, so a slot right after a vsync
 * left the frame a whole vsync to be done and shown, and a slot half a vsync
 * before one left it HALF. A phone whose frame and its card's work take 8-10 ms
 * (a Snapdragon 8s Gen 3 on its middle cores at a third of their clock,
 * reports/2026-10-19/8sgen3) then missed its vsync on many frames: two pictures
 * on one vsync, one dropped, the vsync before shows the old one again. The
 * report counted 59 frames a second and the player saw 45-50, "out of nowhere"
 * - from the next room on, until the one after. Up to 1.74 every frame started
 * on a vsync, the phase could not be lost, and the frame had the whole vsync.
 *
 * So where the game's period is a whole number of the panel's vsyncs (60, 120,
 * 240 Hz, within 1/800) every slot is drawn towards its nearest vsync by at
 * most PHASE_PULL a frame: a half-vsync phase is found again within a quarter
 * second of the frame that lost it, the panel's own drift and a change of its
 * mode are followed, and no two frames come closer than a period less the
 * pull. A late frame's slot is NOT pushed onto the next vsync: on the desk
 * (run.py speed) that cost heavy frames a tenth of the game's speed (90 -> 80 %
 * in the slowest world), and a panel at 60.3 Hz, followed exactly, ran the game
 * at 100.5 %. On 75, 90, 144, 165 Hz the slots fall between vsyncs by the
 * arithmetic of the rates, as before.
 */
#define PHASE_PULL(period) ((period) / 32)

/* the panel's vsync period if the game's period is a whole number of them, else 0 */
static long long beat_of(long long period) {
    long long pp = g_vsync_period;
    if (pp <= 0 || !g_vsync_ns) return 0;
    long long k = (period + pp / 2) / pp;
    if (k < 1) return 0;
    long long d = period - k * pp;
    if (d < 0) d = -d;
    return d <= period / 800 ? pp : 0;
}

/* how far the time t lies from the panel's nearest vsync, in (-pp/2, pp/2] */
static long long off_beat(long long t, long long pp) {
    long long e = (t - g_vsync_ns) % pp;
    if (e < 0) e += pp;
    if (e > pp / 2) e -= pp;
    return e;
}

static int link_wait_due(long long period, long long *due) {
    if (!g_link) return 0;""")

rep("""    if (now - *due > period / 4) {             /* late past the thread's own wake-up: the clock moves to this */
        *due = now + period;                   /* start - the lateness is lost, nothing is caught up */
        DIAG(g_cap_held++);
    } else {
        *due += period;                        /* the absolute grid: 1/60 s a frame */
    }
    return 1;
}""", """    long long beat = beat_of(period);          /* THE GRID ON THE PANEL'S BEAT */
    if (now - *due > period / 4) {             /* late past the thread's own wake-up: the clock moves to this */
        *due = now + period;                   /* start - the lateness is lost, nothing is caught up */
        DIAG(g_cap_held++);
    } else {
        *due += period;                        /* the absolute grid: 1/60 s a frame */
    }
    if (beat) {                                /* drawn towards its vsync, a little a frame */
        long long e = off_beat(*due, beat), pull = PHASE_PULL(period);
        *due -= e > pull ? pull : e < -pull ? -pull : e;
    }
    return 1;
}""")
open(P, 'w', encoding='utf-8', newline='').write(s)
print('ok')
