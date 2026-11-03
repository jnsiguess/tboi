/* cut out of objc.c by run.py - do not edit */
#define PACE_WINDOW_NS  3000000000LL
#define PACE_MIN_FRAMES 60
#define PACE_KEEP_PCT   70
#define PACE_DROP_PCT   85
#define PACE_HOLD       2
static int g_pace_wait;              /* 1: the late start is off - the frame waits for the vsync */
static long long g_pace_sum, g_pace_t0;
static int g_pace_n, g_pace_over, g_pace_under;
#if ISAAC_DIAG
static long g_pace_switches;
#endif

static void pace_note(long long work, long long now, long long budget) {
    if (budget <= 0) return;
    if (work > 2 * budget) work = 2 * budget;      /* a load weighs as two frames, not as twenty */
    g_pace_sum += work;
    g_pace_n++;
    if (!g_pace_t0) { g_pace_t0 = now; return; }
    if (now - g_pace_t0 < PACE_WINDOW_NS || g_pace_n < PACE_MIN_FRAMES) return;
    long long avg = g_pace_sum / g_pace_n;
    int was = g_pace_wait;
    if (avg * 100 >= budget * PACE_DROP_PCT) {
        g_pace_under = 0;
        if (++g_pace_over >= PACE_HOLD) g_pace_wait = 1;
    } else if (avg * 100 <= budget * PACE_KEEP_PCT) {
        g_pace_over = 0;
        if (++g_pace_under >= PACE_HOLD) g_pace_wait = 0;
    } else {
        g_pace_over = g_pace_under = 0;
    }
#if ISAAC_DIAG
    if (was != g_pace_wait) {
        int shim_report_active(void);
        void report_line(const char *, const char *);
        g_pace_switches++;
        LOG("pacing: the late start %s, work %.1f ms avg", g_pace_wait ? "off" : "on", (double) avg / 1e6);
        if (shim_report_active() && g_pace_switches <= 40) {
            char line[200];
            snprintf(line, sizeof(line), "pacing: the late start %s - the frame's own work averaged %.1f ms "
                     "over %d frames (%lld%% of the %.1f ms budget), the panel at %.1f Hz",
                     g_pace_wait ? "OFF, the frame waits for the vsync" : "ON again",
                     (double) avg / 1e6, g_pace_n, avg * 100 / budget, (double) budget / 1e6,
                     g_vsync_period > 0 ? 1e9 / (double) g_vsync_period : 0.0);
            report_line("vsync", line);
        }
    }
#else
    (void) was;
#endif
    g_pace_sum = 0;
    g_pace_n = 0;
    g_pace_t0 = now;
}
