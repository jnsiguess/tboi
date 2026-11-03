/*
 * Desk test of the late start's rule (hotfix 1.72, perf item 1): objc.c's
 * pace_note and its constants, cut out of objc.c as they are by run.py
 * (pace_cut.h), fed with frame works.
 *
 *   test_pace              the rule's cases, asserted
 *   test_pace replay       "work_ns now_ns" lines on stdin -> the frames where
 *                          the mode changed; run.py compares them with the desk
 *                          model's Pace (research notes of 07.10.2026, pacing_model.py)
 */
#include <stdio.h>
#include <string.h>
#define LOG(...) do { } while (0)
static long long g_vsync_period = 11111111;
#include "pace_cut.h"

static int g_fail;
#define CHECK(c, ...) do { if (!(c)) { g_fail++; printf("  FAIL line %d: ", __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)
#define BUDGET 16666666LL

static void reset(int wait) {
    g_pace_wait = wait;
    g_pace_sum = g_pace_t0 = 0;
    g_pace_n = g_pace_over = g_pace_under = 0;
}

/* frames of `work` ms at 60 a second for `secs`; returns the second at which the mode last changed, -1 none */
static double run(double secs, double (*work)(long i), long long *clock) {
    double changed = -1;
    long frames = (long) (secs * 60);
    for (long i = 0; i < frames; i++) {
        int was = g_pace_wait;
        long long w = (long long) (work(i) * 1e6);
        *clock += 16666666LL > w ? 16666666LL : w;
        pace_note(w, *clock, BUDGET);
        if (was != g_pace_wait) changed = (double) i / 60.0;
    }
    return changed;
}

static double w10(long i) { (void) i; return 10.0; }
static double w11_5(long i) { (void) i; return 11.5; }
static double w13(long i) { (void) i; return 13.0; }
static double w15(long i) { (void) i; return 15.0; }
static double w22(long i) { (void) i; return 22.0; }
static double loads(long i) { return i % 100 == 0 ? 400.0 : 10.0; }       /* a 400 ms load every 100 frames */
static double flip(long i) { return (i / 180) % 2 ? 15.0 : 10.0; }       /* 3 s heavy, 3 s light, ... */
static double long_loads(long i) { return (i % 600) < 6 ? 300.0 : 9.0; } /* six 300 ms frames every 10 s */

int main(int argc, char **argv) {
    if (argc > 1 && !strcmp(argv[1], "replay")) {
        long long w, t;
        long i = 0;
        reset(0);
        while (scanf("%lld %lld", &w, &t) == 2) {
            int was = g_pace_wait;
            pace_note(w, t, BUDGET);
            if (was != g_pace_wait) printf("%ld %d\n", i, g_pace_wait);
            i++;
        }
        return 0;
    }
    long long clock = 1000000000LL;
    printf("the late start's rule (objc.c pace_note): KEEP %d%% DROP %d%% HOLD %d, window %lld ms\n",
           PACE_KEEP_PCT, PACE_DROP_PCT, PACE_HOLD, PACE_WINDOW_NS / 1000000);
    reset(0);
    CHECK(run(60, w10, &clock) < 0 && !g_pace_wait, "10 ms: the late start stays");
    reset(0);
    CHECK(run(60, w11_5, &clock) < 0 && !g_pace_wait, "11.5 ms (69%%): stays");
    reset(0);
    double at = run(60, w15, &clock);
    CHECK(g_pace_wait && at >= 5.5 && at <= 9.0, "15 ms (90%%): off after two windows (%.2f s)", at);
    at = run(60, w15, &clock);
    CHECK(g_pace_wait && at < 0, "and stays off");
    at = run(60, w13, &clock);
    CHECK(g_pace_wait && at < 0, "13 ms (78%%) after off: stays off - the hysteresis");
    at = run(60, w11_5, &clock);
    CHECK(!g_pace_wait && at >= 5.5 && at <= 9.0, "11.5 ms: back on after two windows (%.2f s)", at);
    at = run(60, w13, &clock);
    CHECK(!g_pace_wait && at < 0, "13 ms after on: stays on");
    reset(0);
    CHECK(run(120, w22, &clock) >= 0 && g_pace_wait, "22 ms: off");
    reset(0);
    CHECK(run(120, loads, &clock) < 0 && !g_pace_wait, "10 ms with a 400 ms load every 100 frames: stays on (a load counts as two frames)");
    reset(0);
    CHECK(run(120, long_loads, &clock) < 0 && !g_pace_wait, "9 ms with six 300 ms frames every 10 s: stays on");
    reset(0);
    CHECK(run(120, flip, &clock) < 0 && !g_pace_wait, "3 s heavy / 3 s light by turns: never two windows in a row - no switch");
    /* a window needs 60 frames: a stall of 3 s in 10 frames decides nothing */
    reset(0);
    for (int i = 0; i < 10; i++) { clock += 300000000LL; pace_note(300000000LL, clock, BUDGET); }
    CHECK(!g_pace_wait && g_pace_over == 0, "ten 300 ms frames: no verdict");
    /* a budget of zero (no fps yet) is ignored */
    reset(0);
    pace_note(50000000LL, clock, 0);
    CHECK(g_pace_n == 0, "no budget: not counted");
    printf(g_fail ? "FAILED: %d\n" : "all passed\n", g_fail);
    return g_fail ? 1 : 0;
}
