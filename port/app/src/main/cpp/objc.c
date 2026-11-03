/*
 * The Objective-C runtime, stage one: the talking one.
 *
 * The job of this stage is not to execute messages but to hear them.
 * objc_msgSend names the selector and returns zero; ownership bookkeeping is
 * reduced to identity. That way the entry point enumerates for us what the
 * game wants from iOS — the same trick that already found six missing
 * functions instead of the hundred and sixty we had assumed.
 *
 * The class layout here is the real one, from the Objective-C binary
 * interface: an honest method lookup will walk it later, with nothing to redo.
 */
#include <stdint.h>
#include <dlfcn.h>
#include <sys/resource.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <android/log.h>
#include <android/choreographer.h>
#include <android/looper.h>

#include "diag.h"
#include <unistd.h>
#include <time.h>

#if ISAAC_DIAG
#define LOG(...) __android_log_print(ANDROID_LOG_INFO, "isaac", __VA_ARGS__)
#else
#define LOG(...) do { } while (0)      /* the build we play on has no log */
#endif

/*
 * The display link.
 *
 * The Choreographer hands a callback to the looper of the thread that asked,
 * once per vsync, with the vsync's timestamp. The frame thread is a plain
 * worker with no looper of its own, so one is prepared for it here, and the
 * wait is a poll on that looper until the callback has run. A poll that times
 * out means no vsync is coming - the screen is off - and the caller falls
 * back to the timer for that frame.
 */
/*
 * Page faults, for the stall lines.
 *
 * The resources are served straight out of the mapped APK (pack.c), so the
 * first touch of a sprite's page is a read from flash - a MAJOR fault. If the
 * stalls are the game walking into a room and meeting art it has not read
 * before, this is what will show it, and no guess is needed. Minor faults come
 * with fresh memory, which points at allocation instead.
 *
 * /proc/self/stat, one short read a frame, and only while a report is open.
 * The comm field can hold spaces inside its brackets, so counting starts after
 * the closing one: state is the first field there and majflt the tenth.
 */
/*
 * Page faults, asked of the kernel directly.
 *
 * This used to open /proc/self/stat, read it and parse past eight fields -
 * every frame, in a build whose whole purpose is to measure how fast the
 * frames are. An open, a read and a close is tens of microseconds and it lands
 * inside the very number being measured. getrusage answers the same two counts
 * in one call with no file behind it.
 */
static void faults(long *major, long *minor) {
    struct rusage ru;
    *major = *minor = 0;
    if (getrusage(RUSAGE_SELF, &ru) != 0) return;
    *minor = (long) ru.ru_minflt;
    *major = (long) ru.ru_majflt;
}

/*
 * THE FRAME'S BUDGET, TOLD TO THE SYSTEM (19.10.2026, with WOKEN BY THE PANEL).
 * Android 13 and up takes a render thread's target and its real work per frame
 * (APerformanceHint, ADPF) and sets the cores' clocks from them, instead of
 * guessing from a thread that sleeps most of every frame. The session is made
 * on the frame thread at its first frame with the game's period as the target;
 * every frame reports its own work - the present's hold left out, a wait is
 * not work. Looked up by name: below Android 13 it is simply not there.
 *
 * THE TARGET IS THE CPU'S DEADLINE, NOT THE PERIOD (21.10.2026, the renderer
 * audit). Told 16.67 ms against 5-8 ms of work, ADPF read "40 % used" and let
 * the cores sit at a third of their clock (the 8s Gen 3 report). The picture
 * is asked for half a vsync before the next slot and the card still has its
 * part to do, so the frame's CPU has about half the period: that is the target
 * now, and the work reported is the whole of it - the present's blit and swap
 * are the drivers' CPU too. A frame that began by waiting for a window (back
 * from the background) is not reported, and a new period updates the target.
 */
typedef struct APerformanceHintManager APerformanceHintManager;
typedef struct APerformanceHintSession APerformanceHintSession;
static APerformanceHintSession *g_hint;
static int (*g_hint_report)(APerformanceHintSession *, int64_t);
static int (*g_hint_target)(APerformanceHintSession *, int64_t);
static long long g_hint_period;

static void frame_hint(long long work_ns, long long period) {
    static int tried;
    if (!tried) {
        tried = 1;
        APerformanceHintManager *(*get)(void) =
            (APerformanceHintManager *(*)(void)) dlsym(RTLD_DEFAULT, "APerformanceHint_getManager");
        APerformanceHintSession *(*make)(APerformanceHintManager *, const int32_t *, size_t, int64_t) =
            (APerformanceHintSession *(*)(APerformanceHintManager *, const int32_t *, size_t, int64_t))
                dlsym(RTLD_DEFAULT, "APerformanceHint_createSession");
        g_hint_report = (int (*)(APerformanceHintSession *, int64_t))
            dlsym(RTLD_DEFAULT, "APerformanceHint_reportActualWorkDuration");
        g_hint_target = (int (*)(APerformanceHintSession *, int64_t))
            dlsym(RTLD_DEFAULT, "APerformanceHint_updateTargetWorkDuration");
        APerformanceHintManager *m = get ? get() : NULL;
        int32_t tid = (int32_t) gettid();
        g_hint_period = period;
        if (m && make && g_hint_report) g_hint = make(m, &tid, 1, (int64_t) (period / 2));
        LOG("frame hint: %s", g_hint ? "the frame thread's budget is told to the system"
                                     : "not offered (below Android 13)");
    }
    if (!g_hint) return;
    if (period != g_hint_period && g_hint_target) {
        g_hint_period = period;
        g_hint_target(g_hint, (int64_t) (period / 2));
    }
    if (work_ns > 0 && work_ns < 10 * period) g_hint_report(g_hint, (int64_t) work_ns);   /* not a wait for a window */
}

static AChoreographer *g_link;
static long long g_vsync_ns;        /* the timestamp of the vsync that just fired */
static int g_vsync_came;
/*
 * One vsync interval of the panel; 0 while not told. The system tells it on
 * the refresh-rate callback (on_refresh, Android 11+), which the looper hands
 * over only while it is polled - while the grid is anchored on a vsync
 * (link_wait_due): once at the start, and again after the timer's frames. In
 * play the looper is not polled, so a change of the display mode mid-game is
 * not heard, and nothing needs it: the frames run on the game's own clock.
 * It sets how early a picture may be timed (half a vsync, link_wait_due).
 */
static long long g_vsync_period;

#if ISAAC_DIAG
/* the report's view of the frame thread, per frames window (shim_vsync_window) */
static long long g_work_sum, g_held_sum;
static long g_late_frames, g_work_n;

/*
 * WHICH CORE THE FRAMES RAN ON, AND HOW FAST IT WAS (16.10.2026, a Moto G50
 * 5G that "lags badly" on 1.75 even at POTATO and was fine on 1.74). The frame
 * thread sleeps to its slot now (THE GAME'S OWN CLOCK); a thread that sleeps a
 * lot may be read by the kernel as idle - its core clocked down, or the
 * thread moved to a little core - and a frame that fitted stops fitting. So
 * every frame says which core it ran on, and every 15th frame reads that
 * core's clock (cpufreq, a short sysfs read); the window prints, per core,
 * the frames, the average clock seen and the core's top clock.
 */
#define CPU_SEEN 16
static long g_cpu_frames[CPU_SEEN];
static long long g_cpu_khz_sum[CPU_SEEN];
static long g_cpu_khz_n[CPU_SEEN];
static long g_cpu_top_khz[CPU_SEEN];        /* cpuinfo_max_freq, read once; -1 unreadable */

int sched_getcpu(void);                      /* bionic declares it for _GNU_SOURCE only */

static long cpu_sysfs_khz(int cpu, const char *leaf) {
    char path[96];
    snprintf(path, sizeof(path), "/sys/devices/system/cpu/cpu%d/cpufreq/%s", cpu, leaf);
    FILE *f = fopen(path, "r");
    if (!f) return -1;
    long v = -1;
    if (fscanf(f, "%ld", &v) != 1) v = -1;
    fclose(f);
    return v;
}

static void cpu_note(void) {
    static long frames;
    int c = sched_getcpu();
    if (c < 0 || c >= CPU_SEEN) return;
    g_cpu_frames[c]++;
    if (frames++ % 15) return;
    long khz = cpu_sysfs_khz(c, "scaling_cur_freq");
    if (khz > 0) { g_cpu_khz_sum[c] += khz; g_cpu_khz_n[c]++; }
}

/*
 * THE CAMERA, MEASURED (17.10.2026). One canvas without the camera's stepping
 * wants the camera split: the room drawn with its whole part, the fraction
 * added where the room is laid on the enlarged scene. Before that is built, the
 * camera itself is measured: the room (game+0x21550) keeps its render scroll
 * offset at +0x1938 - added by every render path that places a sprite of the
 * room (0x5c14d0, 0x7e82e0, 0x7e8414, 0x85c3fc...) - and the screen shake at
 * game+0x1af360 is added to every sprite (0x89e2d0). Per window: how many frames
 * had a camera off the whole unit, the fraction's range, the largest step from
 * one frame to the next, and how many frames shook.
 */
unsigned long shim_image_base(void);
static long g_cam_frames, g_cam_frac, g_cam_moved, g_cam_shook;
static float g_cam_fmin = 1.0f, g_cam_fmax = 0.0f, g_cam_step, g_cam_last[2], g_cam_xmin = 1e9f, g_cam_xmax = -1e9f,
             g_cam_ymin = 1e9f, g_cam_ymax = -1e9f;
static int g_cam_have;

static float frac_of(float v) { float f = v - floorf(v); return f > 0.5f ? 1.0f - f : f; }  /* distance to a whole unit */

static void cam_note(void) {
    unsigned long b = shim_image_base();
    if (!b) return;
    unsigned long G = *(const unsigned long *) (b + 0xac3b90);
    if (!G) return;
    unsigned long room = *(const unsigned long *) (G + 0x21550);
    if (!room) return;
    const float *c = (const float *) (room + 0x1938);
    const float *sh = (const float *) (G + 0x1af360);
    if (!isfinite(c[0]) || !isfinite(c[1])) return;
    g_cam_frames++;
    float fx = frac_of(c[0]), fy = frac_of(c[1]);
    float f = fx > fy ? fx : fy;
    if (f > 0.001f) {
        g_cam_frac++;
        if (f < g_cam_fmin) g_cam_fmin = f;
        if (f > g_cam_fmax) g_cam_fmax = f;
    }
    if (c[0] < g_cam_xmin) g_cam_xmin = c[0];
    if (c[0] > g_cam_xmax) g_cam_xmax = c[0];
    if (c[1] < g_cam_ymin) g_cam_ymin = c[1];
    if (c[1] > g_cam_ymax) g_cam_ymax = c[1];
    if (g_cam_have) {
        float dx = c[0] - g_cam_last[0], dy = c[1] - g_cam_last[1];
        float d = sqrtf(dx * dx + dy * dy);
        if (d > 0.0001f) g_cam_moved++;
        if (d > g_cam_step && d < 100.0f) g_cam_step = d;
    }
    g_cam_last[0] = c[0]; g_cam_last[1] = c[1]; g_cam_have = 1;
    if (isfinite(sh[0]) && (fabsf(sh[0]) > 0.001f || fabsf(sh[1]) > 0.001f)) g_cam_shook++;
}

static void cam_window(char *out, size_t room) {
    if (!room) return;
    if (!g_cam_frames) {
        int at = snprintf(out, room, " | camera: no room");
        void shim_screen_split_window(char *, unsigned long);
        if (at > 0 && (size_t) at < room) shim_screen_split_window(out + at, room - (size_t) at);
        return;
    }
    int at = snprintf(out, room, " | camera over %ld frames: x %.2f..%.2f y %.2f..%.2f, moved in %ld, off a whole unit in %ld"
             " (by %.3f..%.3f), largest step %.3f; shaking in %ld",
             g_cam_frames, (double) g_cam_xmin, (double) g_cam_xmax, (double) g_cam_ymin, (double) g_cam_ymax,
             g_cam_moved, g_cam_frac, (double) (g_cam_frac ? g_cam_fmin : 0.0f), (double) g_cam_fmax,
             (double) g_cam_step, g_cam_shook);
    { void shim_screen_split_window(char *, unsigned long);
      if (at > 0 && (size_t) at < room) shim_screen_split_window(out + at, room - (size_t) at); }
    g_cam_frames = g_cam_frac = g_cam_moved = g_cam_shook = 0;
    g_cam_fmin = 1.0f; g_cam_fmax = 0.0f; g_cam_step = 0.0f;
    g_cam_xmin = g_cam_ymin = 1e9f; g_cam_xmax = g_cam_ymax = -1e9f;
}

/* " | frames by core: cpu6 812 at 1.65 of 2.20 GHz, cpu2 40 at 1.10 of 2.00 GHz" */
static void cpu_window(char *out, size_t room) {
    if (!room) return;
    int at = snprintf(out, room, " | frames by core:");
    int any = 0;
    for (int c = 0; c < CPU_SEEN && at > 0 && (size_t) at < room; c++) {
        if (!g_cpu_frames[c]) continue;
        if (!g_cpu_top_khz[c]) g_cpu_top_khz[c] = cpu_sysfs_khz(c, "cpuinfo_max_freq");
        double seen = g_cpu_khz_n[c] ? (double) g_cpu_khz_sum[c] / (double) g_cpu_khz_n[c] / 1e6 : -1.0;
        double top = g_cpu_top_khz[c] > 0 ? (double) g_cpu_top_khz[c] / 1e6 : -1.0;
        if (seen > 0.0 && top > 0.0)
            at += snprintf(out + at, room - (size_t) at, " cpu%d %ld at %.2f of %.2f GHz", c, g_cpu_frames[c], seen, top);
        else
            at += snprintf(out + at, room - (size_t) at, " cpu%d %ld (clock not readable)", c, g_cpu_frames[c]);
        any = 1;
    }
    if (!any) snprintf(out, room, " | frames by core: not told");
    memset(g_cpu_frames, 0, sizeof(g_cpu_frames));
    memset(g_cpu_khz_sum, 0, sizeof(g_cpu_khz_sum));
    memset(g_cpu_khz_n, 0, sizeof(g_cpu_khz_n));
}
#endif

static int g_vsync_asked;           /* a frame callback is posted and has not come yet */
static void on_vsync64(int64_t when, void *data) { (void) data; g_vsync_ns = when; g_vsync_came = 1; g_vsync_asked = 0; }
static void on_vsync32(long when, void *data) { (void) data; g_vsync_ns = when; g_vsync_came = 1; g_vsync_asked = 0; }

/* the display's refresh period: on registration, and a change of mode at the next anchor */
static void on_refresh(int64_t period, void *data) {
    (void) data;
    if (period <= 0) return;
    g_vsync_period = period;
#if ISAAC_DIAG
    LOG("display mode: %.2f Hz", 1e9 / (double) period);
    {
        int shim_report_active(void);
        void report_line(const char *, const char *);
        static int told;
        if (shim_report_active() && told < 40) {
            char line[96];
            told++;
            snprintf(line, sizeof(line), "the display mode is %.2f Hz (a vsync every %.2f ms)",
                     1e9 / (double) period, (double) period / 1e6);
            report_line("vsync", line);
        }
    }
#endif
}

static void link_start(void) {
    if (!ALooper_prepare(ALOOPER_PREPARE_ALLOW_NON_CALLBACKS)) { LOG("display link: no looper"); return; }
    g_link = AChoreographer_getInstance();
    LOG("display link: %s", g_link ? "the panel's vsync drives the frames" : "not available, the timer does");
    {   /* and into the tester's report, where the frame lines will be read against it */
        int shim_report_active(void);
        void report_line(const char *, const char *);
        if (shim_report_active())
            report_line("display link", g_link ? "the panel's vsync drives the frames"
                                               : "not available - the timer paces the frames");
    }
    /* Android 11 and up names the panel's period and every change of it, on this thread's looper */
    if (g_link) {
        void (*reg)(AChoreographer *, void (*)(int64_t, void *), void *) =
            (void (*)(AChoreographer *, void (*)(int64_t, void *), void *))
                dlsym(RTLD_DEFAULT, "AChoreographer_registerRefreshRateCallback");
        if (reg) reg(g_link, on_refresh, NULL);
    }
}

/* ask for the next vsync's callback */
static void link_post(void) {
    /* the 64-bit callback exists from Android 10; below that the old one, whose time wraps but is never compared across the wrap here */
    static void (*post64)(AChoreographer *, void (*)(int64_t, void *), void *);
    static int looked;
    if (!looked) { looked = 1; post64 = (void (*)(AChoreographer *, void (*)(int64_t, void *), void *)) dlsym(RTLD_DEFAULT, "AChoreographer_postFrameCallback64"); }
    if (post64) post64(g_link, on_vsync64, NULL);
    else AChoreographer_postFrameCallback(g_link, on_vsync32, NULL);
    g_vsync_asked = 1;
}

/* one vsync; 0 if none came within a tenth of a second */
static int link_wait_vsync(void) {
    if (!g_link) return 0;
    g_vsync_came = 0;
    if (!g_vsync_asked) link_post();
    while (!g_vsync_came) {
        int r = ALooper_pollOnce(100, NULL, NULL, NULL);
        if (r == ALOOPER_POLL_TIMEOUT || r == ALOOPER_POLL_ERROR) return 0;
    }
    return 1;
}

/*
 * THE GAME'S OWN CLOCK (1.75, the owner's rule of 13.10.2026: "the game's
 * speed must not depend on the refresh rate at all - 100 % on 60, 75, 90,
 * 120, 144, 165 and while the panel switches between them").
 *
 * THE GAME'S SPEED IS THE NUMBER OF FRAMES. The engine has no delta time: the
 * manager's update (0x670fb0) counts every draw: and runs Game::Update
 * (0x88f880) on every second one - thirty updates for sixty frames, as iOS's
 * display link at sixty gives them. Fewer frames is a slower game, more a
 * faster one.
 *
 * So the frames run on one absolute clock, not on the vsync: the next frame
 * is due one period after the last one's due time, and it never starts before
 * it - a plain wait on CLOCK_MONOTONIC (wait_until, which sleeps again when a
 * signal wakes it early). A frame that could not finish in time runs late,
 * once, and the clock moves to its start: the next one is due a full period
 * after it - what was lost is lost, nothing is caught up, no two frames come
 * close together. Lateness under a quarter period (the thread's own wake-up)
 * keeps the grid, so it does not drift. The grid is anchored once, on a vsync;
 * a change of the display mode changes nothing.
 *
 * Every way of letting the vsync start the frames (up to 1.74) went wrong on
 * some real phone: a callback a few ms late moved the grid, a slot half-way
 * between two vsyncs was taken early or late, a panel dropped from 120 to
 * 60 Hz by itself mid-game (the research notes of 11.10.2026, speed;
 * reports/2026-10-13/
 * run175c.txt). With this clock the owner's phone measured Game::Update at
 * 29.1-30.0 a second (reports/2026-10-14/truth*.txt).
 *
 * The vsync and the presentation time only place the picture: a picture done
 * a millisecond before its slot + period - half a vsync is given that time
 * (gl.c, THE PICTURE ON ITS SLOT), and the compositor shows it on the first
 * vsync not before it, whatever rate the panel runs at.
 */
/*
 * WOKEN BY THE PANEL AGAIN (19.10.2026, 1.76). From 1.75 the frame thread slept
 * to its slot on a plain timer and asked the Choreographer for nothing in play;
 * players whose 1.72-1.74 ran well (a Galaxy S23 among them) found 1.75 and 1.76
 * dragging at every RENDER SCALE, POTATO too - not the card's work. Up to 1.74
 * the thread had slept in the looper and woken on the panel's vsync callback:
 * the platform saw a thread that asks for every vsync and draws on it - the
 * shape its boosts are built around (a reading of the code, not a measure: no
 * report from those phones). So the wait is that again - the looper,
 * a frame callback asked for every vsync - and the start stays THE GAME'S OWN
 * CLOCK to the nanosecond: the looper is polled up to the last whole
 * millisecond before the slot, the rest is slept out (wait_until). Nothing in
 * the timing moves: on the desk (run.py speed) the variants that started on
 * the vsync itself lost 0.3 points of speed to the callbacks' own delivery,
 * and starts half a vsync early ran 120 frames at 100.7 %.
 *
 * The grid, the no-catch-up rule and the picture's time are 1.75's unchanged.
 */
static long long g_present_due;     /* the frame now starting: its picture no earlier than this; 0: at once */
static long long g_present_near;    /* gl.c gives the time only while it lies more than this ahead */
#if ISAAC_DIAG
static long g_cap_held;              /* times the clock moved to a late frame's start (no catch-up), for the report */
#endif

/*
 * Sleep to `until` on the frame clock; returns the time it woke - never
 * before `until`. A signal ends nanosleep early and it is never restarted
 * (SA_RESTART does not cover it): the report build's sampler (prof_start)
 * fires on this thread every millisecond. So it sleeps again until the clock
 * says so.
 */
static long long wait_until(long long until) {
    for (;;) {
        struct timespec tn;
        clock_gettime(CLOCK_MONOTONIC, &tn);
        long long now = (long long) tn.tv_sec * 1000000000LL + tn.tv_nsec;
        if (now >= until) return now;
        struct timespec w = { (time_t) ((until - now) / 1000000000LL), (long) ((until - now) % 1000000000LL) };
        nanosleep(&w, NULL);
    }
}

/* sleep in the looper, a vsync callback asked for, up to the slot `due`; returns the time it woke - the slot */
static long long wait_on_panel(long long due) {
    for (;;) {
        struct timespec tn;
        clock_gettime(CLOCK_MONOTONIC, &tn);
        long long now = (long long) tn.tv_sec * 1000000000LL + tn.tv_nsec;
        int ms = (int) ((due - now) / 1000000LL);      /* whole milliseconds: the poll never runs past the slot */
        if (ms <= 0) return wait_until(due);
        if (!g_vsync_asked) link_post();
        if (ALooper_pollOnce(ms, NULL, NULL, NULL) == ALOOPER_POLL_ERROR) return wait_until(due);
    }
}

/*
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
    if (!g_link) return 0;
    long long pp = g_vsync_period > 0 ? g_vsync_period : period;
    long long near = (pp < period ? pp : period) / 2;
    long long now;
    if (*due == 0) {                           /* the grid anchored on a vsync, once (and after the timer) */
        if (!link_wait_vsync()) return 0;
        struct timespec tn;
        clock_gettime(CLOCK_MONOTONIC, &tn);
        now = (long long) tn.tv_sec * 1000000000LL + tn.tv_nsec;
        *due = g_vsync_ns;
    } else {
        struct timespec tn;
        clock_gettime(CLOCK_MONOTONIC, &tn);
        now = (long long) tn.tv_sec * 1000000000LL + tn.tv_nsec;
        if (now < *due) now = wait_on_panel(*due);   /* never before its due time, woken by the panel (WOKEN BY THE PANEL) */
        else if (now > *due + period / 8) DIAG(g_late_frames++);   /* the own work ran past the slot */
    }
    g_present_due = *due + period - near;      /* the picture: on the first vsync not before this */
    g_present_near = period / 16;              /* done a millisecond ahead of its time: the time is given */
    long long beat = beat_of(period);          /* THE GRID ON THE PANEL'S BEAT */
    if (now - *due > period / 4) {             /* late past the thread's own wake-up: the clock moves to this */
        long long late = now - *due;
        *due = now + period;                   /* start - the lateness is lost, nothing is caught up */
        /*
         * And after a STALL - a room's loading, longer than two periods - on the
         * panel's beat at once (21.10.2026): forward to its next vsync, never
         * back. The frames right after a load were the ones the pull alone still
         * lost (the renderer audit; phase.py: 10-13 pictures a minute never
         * shown -> 3-6). Not after every late frame: a phone whose every frame
         * runs 18 ms on 60 Hz then started on every second vsync, 90 -> 81 % of
         * the game's speed (run.py speed).
         */
        if (beat && late > 2 * period) {
            long long e = off_beat(*due, beat);
            *due += e > 0 ? beat - e : -e;
        }
        DIAG(g_cap_held++);
    } else {
        *due += period;                        /* the absolute grid: 1/60 s a frame */
    }
    if (beat) {                                /* drawn towards its vsync, a little a frame */
        long long e = off_beat(*due, beat), pull = PHASE_PULL(period);
        *due -= e > pull ? pull : e < -pull ? -pull : e;
    }
    return 1;
}

#if ISAAC_DIAG
/*
 * For the frames line (gl.c): the frame thread over the window and the game's
 * speed against iOS - so a "the game runs slow on my 90 Hz phone" can be read
 * straight off a report. The speed is the frames against the sixty the game
 * asks for, since every frame is one tick of the manager and every second one
 * a game update.
 */
/*
 * The speed in every second of unbroken play - the window's average hides a
 * burst and a sag that cancel out. Every frame start goes into a ring; the
 * frames within the second before it are counted, min and max kept over the
 * frames line's window. A gap over 250 ms (a load, the app in the background)
 * starts a new run, so the min is play, not loading. And starts under 8 ms
 * after the one before: the catch-up bursts 1.73 ran after every hitch. The
 * game's own clock starts no frame sooner than three quarters of a period
 * after the last (link_wait_due), so any here is news.
 */
#define RATE_RING 128
static long long g_rate_ring[RATE_RING], g_rate_since;
static int g_rate_head, g_rate_n, g_rate_max, g_rate_min = -1;
static long g_rate_quick;
static long long g_rate_span2;       /* the shortest span of 120 frame intervals in the window (1.75); 0: none */

static void rate_note(long long t) {
    if (g_rate_n) {
        long long last = g_rate_ring[(g_rate_head + RATE_RING - 1) % RATE_RING];
        if (t < last || t - last > 250000000LL) g_rate_n = 0;
        else if (t - last < 8000000LL) g_rate_quick++;
    }
    if (!g_rate_n) g_rate_since = t;
    g_rate_ring[g_rate_head] = t;
    g_rate_head = (g_rate_head + 1) % RATE_RING;
    if (g_rate_n < RATE_RING) g_rate_n++;
    if (g_rate_n > 120) {                /* two seconds' frames, start to start: the hard cap's own measure */
        long long span = t - g_rate_ring[(g_rate_head - 121 + RATE_RING) % RATE_RING];
        if (!g_rate_span2 || span < g_rate_span2) g_rate_span2 = span;
    }
    if (t - g_rate_since < 1000000000LL) return;
    int c = 0;
    for (int i = 1; i <= g_rate_n; i++) {
        if (g_rate_ring[(g_rate_head - i + RATE_RING) % RATE_RING] <= t - 1000000000LL) break;
        c++;
    }
    if (c > g_rate_max) g_rate_max = c;
    if (g_rate_min < 0 || c < g_rate_min) g_rate_min = c;
}

/*
 * THE GROUND TRUTH OF GAME TIME, SAMPLED EVERY FRAME (the research notes of
 * 14.10.2026, frame audit). "updates a second" on the frames line is
 * presents / 2 - a derivation. These are counts: the real calls of the game
 * update (0x88f880, the manager's even ticks) and of the interpolation pass
 * (0x88f770, its odd ticks) by trace hooks (loader.c, marks '6' and '8' in
 * trace.c); the manager's tick counter (mgr+0x56578, one per draw:); the
 * game's own frame counter (game+0x1af34c); and the presents (one a frame).
 *
 * Each frame's deltas go to the LIVE sums only when the engine itself says
 * the game is running: a Game, and 0x88f6a4(game) == 0 - the very check the
 * interpolation pass and the room code make: no pause, no fade, no stage or
 * room transition, manager state 2 (in a run). A frame's span counts as live
 * time capped at a quarter second (a load is one frame, not its wait). The
 * game's frame counter is zeroed when a run starts (0x88c89c): a negative
 * step is not counted. The window line reads these out (shim_vsync_window).
 * iOS at sixty callbacks: 30 / 30 / 60 / 30 / 60.
 */
static long long g_tr_window_t0, g_tr_live_ns, g_tr_prev_began;
static long g_tr_upd, g_tr_int, g_tr_tick, g_tr_gf, g_tr_pres, g_tr_tick_all, g_tr_gf_all;
static long g_tr_upd_last, g_tr_int_last;
static int g_tr_tick_last, g_tr_gf_last, g_tr_primed, g_tr_interp = -1, g_tr_active = -1;

static void truth_frame_note(long long began) {
    extern long g_truth_updates, g_truth_interp;
    unsigned long shim_image_base(void);
    unsigned long base = shim_image_base();
    unsigned char *mgr = base ? *(unsigned char **) (base + 0xac2428) : NULL;    /* the Repentance manager */
    unsigned char *game = base ? *(unsigned char **) (base + 0xac3b90) : NULL;   /* the Game (GAME_PTR below) */
    int tick = mgr ? *(const int *) (mgr + 0x56578) : 0;
    int gf = game ? *(const int *) (game + 0x1af34c) : 0;
    int live = mgr && game && ((int (*)(unsigned long)) (base + 0x88f6a4))((unsigned long) game) == 0;
    if (g_tr_primed) {
        long dt_tick = tick - g_tr_tick_last;
        long dt_gf = gf - g_tr_gf_last;
        if (dt_gf < 0) dt_gf = 0;                       /* a run started: the counter was zeroed */
        g_tr_tick_all += dt_tick;
        g_tr_gf_all += dt_gf;
        if (live && began > g_tr_prev_began) {
            long long dt = began - g_tr_prev_began;
            if (dt > 250000000LL) dt = 250000000LL;
            g_tr_live_ns += dt;
            g_tr_upd += g_truth_updates - g_tr_upd_last;
            g_tr_int += g_truth_interp - g_tr_int_last;
            g_tr_tick += dt_tick;
            g_tr_gf += dt_gf;
            g_tr_pres++;
        }
    }
    g_tr_interp = mgr ? mgr[0x55f1c] : -1;
    g_tr_active = mgr ? mgr[0x27bcc9] : -1;
    g_tr_primed = 1;
    g_tr_prev_began = began;
    g_tr_upd_last = g_truth_updates;
    g_tr_int_last = g_truth_interp;
    g_tr_tick_last = tick;
    g_tr_gf_last = gf;
}

void shim_vsync_window(char *out, size_t room, double frames_a_second) {
    int shim_want_fps(void);
    if (!room) return;
    int fps = shim_want_fps();
    if (fps <= 0) fps = 60;
    /* the frame's work, from its start to the wait, and the part of it the present was held by the panel */
    double work = g_work_n ? (double) g_work_sum / (double) g_work_n / 1e6 : 0.0;
    double held = g_work_n ? (double) g_held_sum / (double) g_work_n / 1e6 : 0.0;
    /* the frames whose own work ran past their slot (link_wait_due), and the thread's working share */
    double late_share = g_work_n ? (double) g_late_frames * 100.0 / (double) g_work_n : 0.0;
    double busy = (work - held) * frames_a_second / 10.0;
    /*
     * The speed in every second (rate_note) and how many pictures went to the
     * panel with their slot's time (gl.c THE PICTURE ON ITS SLOT). Over 61
     * frames in one second, or a window over 100.5 %, is said in capitals:
     * that is what the game's own clock is there to rule out. And the fastest
     * two seconds, start to start (any 120 frame intervals) - over 100.5 % is
     * said in capitals too - and how many times the clock moved to a late
     * frame's start, skipping its slot (THE GAME'S OWN CLOCK).
     */
    char cap[380];
    {
        extern long g_gl_timed, g_gl_untimed;
        long pics = g_gl_timed + g_gl_untimed;
        double fast2 = g_rate_span2 > 0 ? 120.0 * 1e9 / (double) fps / (double) g_rate_span2 * 100.0 : 0.0;
        int over = g_rate_max > 61 || fast2 > 100.5 || frames_a_second * 100.0 / fps > 100.5;
        char two[48] = "";
        if (fast2 > 0.0) snprintf(two, sizeof(two), ", fastest 2 s %.1f%%", fast2);
        if (g_rate_min >= 0)
            snprintf(cap, sizeof(cap), " (every 1 s: %.1f..%.1f updates%s, %ld quick starts, %ld slots skipped, "
                     "pictures on their slot %.0f%%)%s", g_rate_min / 2.0, g_rate_max / 2.0, two, g_rate_quick,
                     g_cap_held, pics ? (double) g_gl_timed * 100.0 / (double) pics : 0.0,
                     over ? " FASTER THAN iOS" : "");
        else
            snprintf(cap, sizeof(cap), " (every 1 s: not a whole second of play, %ld quick starts)%s", g_rate_quick,
                     over ? " FASTER THAN iOS" : "");
        g_gl_timed = g_gl_untimed = 0;
        g_rate_span2 = 0;
        g_cap_held = 0;
        g_rate_max = 0;
        g_rate_min = -1;
        g_rate_quick = 0;
    }
    /*
     * THE GROUND TRUTH OF GAME TIME (the research notes of 14.10.2026, frame audit).
     * "updates a second" above is presents / 2 - a derivation, not a count,
     * and it stood at 30.0 while the owner saw the game run fast. So here are
     * the counts, per real second of this window: the real calls of the game
     * update (0x88f880, the manager's even ticks) and of the interpolation
     * pass (0x88f770, its odd ticks), both counted by trace hooks (loader.c,
     * trace.c); the manager's tick counter (mgr+0x56578, one per draw:); the
     * game's own frame counter (game+0x1af34c - it is zeroed when a run
     * starts, so a window with a new run in it reads low once); and the
     * presents. With the two switches the gate depends on: EnableInterpolation
     * (mgr+0x55f1c, options.ini, default 1) and "the app is active"
     * (mgr+0x27bcc9, OnDidBecomeActive). iOS: 30 / 30 / 60 / 30 / 60.
     */
    /*
     * SECOND RUN (truth.txt, 14.10.2026): the whole-window rates read "game
     * frames 11..28/s" with ticks at 60/s - the windows held pauses, the EID
     * panel and room fades, during which the game's frame counter stands
     * still while the manager keeps ticking (its state-2 case skips the
     * update: 0x88f6a4 says "blocked"). So every frame is now sampled
     * (truth_frame_note) and sorted by the engine's own answer: in a run
     * and not blocked (0x88f6a4(game) == 0: no pause, no fade, no stage or
     * room transition, manager state 2). The rates below are over those
     * live seconds only; the whole window's ticks and game frames follow
     * for comparison.
     */
    char truth[360] = "";
    {
        struct timespec ts;
        clock_gettime(CLOCK_MONOTONIC, &ts);
        long long now = (long long) ts.tv_sec * 1000000000LL + ts.tv_nsec;
        if (g_tr_window_t0 && now > g_tr_window_t0) {
            double all = (double) (now - g_tr_window_t0) / 1e9;
            double live = (double) g_tr_live_ns / 1e9;
            if (live > 0.5)
                snprintf(truth, sizeof(truth), " | ground truth over %.1f s in a run and unpaused (of %.0f s): "
                         "Game::Update %.1f/s, interpolation passes %.1f/s, manager ticks %.1f/s, game frames %.1f/s, "
                         "presents %.1f/s (interpolation %d, active %d); the whole window: ticks %.1f/s, game frames %.1f/s",
                         live, all, (double) g_tr_upd / live, (double) g_tr_int / live, (double) g_tr_tick / live,
                         (double) g_tr_gf / live, (double) g_tr_pres / live, g_tr_interp, g_tr_active,
                         (double) g_tr_tick_all / all, (double) g_tr_gf_all / all);
            else
                snprintf(truth, sizeof(truth), " | ground truth: %.1f s in a run and unpaused of %.0f s - too little to rate; "
                         "the whole window: ticks %.1f/s, game frames %.1f/s (interpolation %d, active %d)",
                         live, all, (double) g_tr_tick_all / all, (double) g_tr_gf_all / all, g_tr_interp, g_tr_active);
        }
        g_tr_window_t0 = now;
        g_tr_live_ns = 0;
        g_tr_upd = g_tr_int = g_tr_tick = g_tr_gf = g_tr_pres = g_tr_tick_all = g_tr_gf_all = 0;
    }
    char cores[300];
    cpu_window(cores, sizeof(cores));
    char camera[360];
    cam_window(camera, sizeof(camera));
    char panel[32] = "vsync not told";
    if (g_vsync_period > 0) snprintf(panel, sizeof(panel), "vsync %.1f Hz", 1e9 / (double) g_vsync_period);
    snprintf(out, room, " | %s, %ld frames past their slot (%.0f%%), frame work %.1f ms avg (own %.1f + present "
             "held %.1f), thread busy %.0f%%%s%s | game speed %.1f%% of iOS, %.1f updates a second%s%s",
             panel, g_late_frames, late_share, work, work - held, held, busy, cores, camera,
             frames_a_second * 100.0 / fps, frames_a_second / 2.0, cap, truth);
    g_late_frames = 0;
    g_work_sum = 0;
    g_held_sum = 0;
    g_work_n = 0;
}
#endif


/* ---------- class layout ---------- */

typedef struct objc_class objc_class_t;

struct objc_class {
    objc_class_t *isa;
    objc_class_t *superclass;
    void *cache_buckets;          /* a two-field cache: we just hold the space */
    uintptr_t cache_mask_occupied;
    uintptr_t bits;               /* pointer to class_ro_t with flags in the low bits */
};

/*
 * THE POINTER IS NOT MASKED TO 47 BITS. This crashed a Pixel 7 Pro.
 *
 * A class keeps its class_ro_t in `bits` with three flag bits at the bottom,
 * and Apple's runtime digs it out with FAST_DATA_MASK, 0x00007FFFFFFFFFF8 -
 * bits 3 to 46. That is sound on iOS, where nothing user-space ever lives
 * above 2^47, and it quietly throws away the top of an address anywhere else.
 *
 * Measured, from a tester's crash report: the library had been mapped at
 * 0xd5379065a000 and the value read was 0xd53782f69b00; the mask made it
 * 0x553782f69b00 - bit 47 gone - and the very next load faulted at
 * 0x553782f69b18 (objc.c, class_name). Android hands out addresses that high
 * on newer phones, and GrapheneOS's hardened allocator makes it the rule
 * rather than the exception; the tester's own workaround, "exploit protection
 * compatibility mode", is exactly the switch that moves allocations back down.
 *
 * Only the three flag bits are ours to remove, so only they are removed. On
 * iOS this is the same answer the full mask gives - the discarded bits are
 * zero there - so nothing is lost by being right.
 */
#define FAST_DATA_MASK (~(uintptr_t) 7)

typedef struct {
    uint32_t flags, instanceStart, instanceSize, reserved;
    const uint8_t *ivarLayout;
    const char *name;
    void *baseMethods, *baseProtocols, *ivars;
    const uint8_t *weakIvarLayout;
    void *baseProperties;
} class_ro_t;

static class_ro_t *ro_of(objc_class_t *c) {
    return (class_ro_t *) (uintptr_t) (c->bits & FAST_DATA_MASK);
}

static const char *class_name(objc_class_t *c) {
    if (!c) return "nil";
    class_ro_t *ro = ro_of(c);
    return (ro && ro->name) ? ro->name : "?";
}

/* an object: its first field is its class */
typedef struct { objc_class_t *isa; } objc_object_t;

/* ---------- the empty cache ---------- */

/*
 * Every class in the image refers to _objc_empty_cache. The real runtime keeps
 * a shared empty cache there; a block of zeroes of the right size is enough
 * for us, as long as the pointer is valid.
 */
uint64_t shim_objc_empty_cache[8];

/* ---------- boundaries: where valid classes live ---------- */

/*
 * Walking the superclass chain blindly is not allowed: for the game's classes
 * the superclass is a binding such as _OBJC_CLASS_$_UIResponder, and until we
 * have substituted it there is a stub address sitting there. So only something
 * that lives either in the game's image or in our own class storage counts as
 * a class.
 */
static uintptr_t g_img_lo, g_img_hi;

unsigned long shim_image_base(void) { return (unsigned long) g_img_lo; }

void shim_objc_set_image(void *base, unsigned long size) {
    g_img_lo = (uintptr_t) base;
    g_img_hi = g_img_lo + size;
}

#define OURS_MAX 128
static objc_class_t g_ours[OURS_MAX];
static class_ro_t g_ours_ro[OURS_MAX];
static char g_ours_name[OURS_MAX][64];
static int g_nours;

static int is_class(objc_class_t *c) {
    uintptr_t a = (uintptr_t) c;
    if (!a) return 0;
    if (a >= g_img_lo && a < g_img_hi) return 1;
    return a >= (uintptr_t) g_ours && a < (uintptr_t) (g_ours + OURS_MAX);
}

/*
 * The iOS classes are created on demand, straight from a binding's name: there
 * are forty-seven of them and writing each one by hand is pointless — the name
 * says everything we need from it so far.
 */
void *shim_objc_make_class(const char *bind_name) {
    const char *n = bind_name;
    if (!strncmp(n, "_OBJC_CLASS_$_", 14)) n += 14;
    else if (!strncmp(n, "_OBJC_METACLASS_$_", 18)) n += 18;
    else return NULL;
    for (int i = 0; i < g_nours; i++)
        if (!strcmp(g_ours_name[i], bind_name)) return &g_ours[i];
    if (g_nours >= OURS_MAX) return NULL;
    int i = g_nours++;
    strncpy(g_ours_name[i], bind_name, sizeof(g_ours_name[i]) - 1);
    g_ours_ro[i].name = g_ours_name[i] + (n - bind_name);
    g_ours_ro[i].instanceSize = 128;          /* we have no fields of our own, but need slack */
    g_ours[i].bits = (uintptr_t) &g_ours_ro[i];
    g_ours[i].cache_buckets = shim_objc_empty_cache;
    return &g_ours[i];
}

/* ---------- method lists ---------- */

/*
 * The new, relative form: offsets from the field itself instead of pointers.
 * That lets the list sit in __TEXT and need no fixups when relocated. The
 * marker is the top bit of the entry-size field.
 */
typedef struct { uint32_t entsizeAndFlags, count; } method_list_t;
typedef struct { int32_t name, types, imp; } method_small_t;
typedef struct { const char *name; const char *types; void *imp; } method_big_t;

#define METHOD_LIST_SMALL 0x80000000u

static void *find_in_list(method_list_t *ml, const char *sel) {
    if (!ml || !sel) return NULL;
    uint32_t ent = ml->entsizeAndFlags & 0xFFFCu;
    int small = (ml->entsizeAndFlags & METHOD_LIST_SMALL) != 0;
    const uint8_t *p = (const uint8_t *) (ml + 1);
    for (uint32_t i = 0; i < ml->count; i++, p += ent) {
        const char *name;
        void *imp;
        if (small) {
            const method_small_t *m = (const method_small_t *) p;
            /* the name field points at a selector reference, not at the string */
            const char **ref = (const char **) ((const uint8_t *) &m->name + m->name);
            name = *ref;
            imp = (void *) ((const uint8_t *) &m->imp + m->imp);
        } else {
            const method_big_t *m = (const method_big_t *) p;
            name = m->name;
            imp = m->imp;
        }
        if (name == sel || (name && !strcmp(name, sel))) return imp;
    }
    return NULL;
}

/* ---------- message dispatch ---------- */

static long g_msg_count, g_msg_missed;
static void *g_shell;          /* the game's shell, caught as it was created */
static void *g_input_view;     /* AAPLOpenGLView: typed text goes to it */
static void *g_leaderboard_info;  /* LeaderboardInfo: the daily menu waits on it */
#define SEEN_MAX 512
static const char *g_seen[SEEN_MAX];
static int g_nseen;

static void note_missed(objc_class_t *cls, const char *sel) {
    for (int i = 0; i < g_nseen; i++)
        if (g_seen[i] == sel) return;                 /* selectors are unique by address */
    if (g_nseen < SEEN_MAX) g_seen[g_nseen++] = sel;
    DIAG(LOG("NO METHOD [%s %s]", class_name(cls), sel ? sel : "?"));
    /*
     * The report as well, in the build the testers run: a method nobody
     * answers is the usual reason for "nothing happened", and the log that
     * used to hold it is off in that build.
     */
    int shim_report_active(void);
    void report_line(const char *kind, const char *text);
    if (shim_report_active() && g_nseen <= 40) {
        char line[160];
        snprintf(line, sizeof(line), "NO METHOD [%s %s]", class_name(cls), sel ? sel : "?");
        report_line("objc", line);
    }
}

/* same as the misses, but for successful calls: without it the game's flow is invisible */
static const char *g_hit_sel[SEEN_MAX];
static objc_class_t *g_hit_cls[SEEN_MAX];
static int g_nhit;

/* the filter is by pair: the same selector shows up on different classes */
static void note_hit(objc_class_t *cls, const char *sel) {
#if !ISAAC_DIAG
    (void) cls; (void) sel;
#else
    for (int i = 0; i < g_nhit; i++)
        if (g_hit_sel[i] == sel && g_hit_cls[i] == cls) return;
    if (g_nhit < SEEN_MAX) { g_hit_sel[g_nhit] = sel; g_hit_cls[g_nhit] = cls; g_nhit++; }
    LOG("CALL [%s %s]", class_name(cls), sel ? sel : "?");
#endif
}

static void *lookup_from(objc_class_t *start, const char *sel) {
    for (objc_class_t *c = start; is_class(c); c = c->superclass) {
        class_ro_t *ro = ro_of(c);

        if (!ro) break;
        void *imp = find_in_list((method_list_t *) ro->baseMethods, sel);
        if (imp) return imp;
    }
    return NULL;
}

/* ---------- the layer's built-in methods ---------- */

/*
 * Our iOS classes have no method lists yet, so their behaviour lives in a
 * separate table: class name, selector, body. An empty class name means "fits
 * any receiver" — that is how NSObject is described, whose methods everything
 * inherits, including the game's own classes.
 */
typedef struct { const char *cls; const char *sel; void *imp; } builtin_t;
static const builtin_t *g_builtins;

void shim_objc_set_builtins(const void *table) { g_builtins = (const builtin_t *) table; }

/*
 * The stub lookup walks THE ANCESTRY, as Objective-C is supposed to.
 *
 * It used to compare only the receiver's own class name, so stubs written
 * against UIView reached an object whose name was exactly "UIView" and nothing
 * else. The moment the controller was handed the game's real view,
 * AAPLOpenGLView, it could find neither layer, nor bounds, nor
 * setContentScaleFactor:, and touches fell apart.
 *
 * A class's own methods are looked up first (lookup_from), so inheritance
 * overrides nothing: a stub only reaches whoever has none of their own.
 */
static void *builtin_imp(objc_class_t *recv_cls, const char *sel) {
    if (!g_builtins || !sel) return NULL;
    for (objc_class_t *c = recv_cls; ; c = c->superclass) {
        const char *cn = c ? class_name(c) : NULL;
        for (int i = 0; g_builtins[i].sel; i++) {
            if (strcmp(g_builtins[i].sel, sel)) continue;
            if (g_builtins[i].cls && (!cn || strcmp(g_builtins[i].cls, cn))) continue;
            return g_builtins[i].imp;
        }
        if (!c || !c->superclass || c->superclass == c) break;
    }
    return NULL;
}

void *shim_objc_class_named(const char *);   /* defined below, called earlier */

/* Diagnostics: a class's ancestry. Without it there is no telling whether it reaches UIView. */
static void show_ancestry(const char *name) {
    objc_class_t *c = (objc_class_t *) shim_objc_class_named(name);
    if (!c) { LOG("ancestry of %s: no such class", name); return; }
    char line[240];
    int k = 0;
    for (int step = 0; c && step < 8; step++) {
        k += snprintf(line + k, sizeof(line) - (size_t) k, "%s%s",
                      step ? " -> " : "", class_name(c));
        if (!c->superclass || c->superclass == c) break;
        c = c->superclass;
    }
    LOG("ancestry of %s: %s", name, line);
}

/* called from objc_asm.S: return the method's address or zero */
void *shim_objc_lookup(void *self, const char *sel) {
    g_msg_count++;
    objc_object_t *o = (objc_object_t *) self;
    if (!o) { g_msg_missed++; return NULL; }

    /*
     * A receiver comes in two kinds: an ordinary object and a class itself.
     * For an object we search its class, for a class the class itself: our
     * fakes have no metaclasses, and for the image's classes isa already
     * points at the metaclass.
     */
    objc_class_t *start = is_class(o->isa) ? o->isa
                        : (is_class((objc_class_t *) o) ? (objc_class_t *) o : NULL);

    void *imp = start ? lookup_from(start, sel) : NULL;
    if (imp) note_hit(start, sel);
    /*
     * The game's shell is never handed out anywhere, and we need it. We catch
     * it as it is being created: the receiver of this message is the shell.
     */
    if (imp && !g_shell && start && !strcmp(class_name(start), "IsaacShell")) g_shell = self;
    /*
     * The game's view is caught the same way. Seed entry is implemented on
     * AAPLOpenGLView (insertText:, deleteBackward, hasText), while focus is
     * asked of a plain UIView — a stub of our layer. So typed text has to go
     * not to whoever asked for the keyboard but to this view: it is the real
     * recipient.
     */
    if (imp && !g_input_view && start && !strcmp(class_name(start), "AAPLOpenGLView"))
        g_input_view = self;
    /*
     * Leaderboard information. The daily-run menu keeps waiting until it
     * considers the data ready, and that marker lives somewhere in the object.
     * We catch it so the memory inspector can look inside.
     */
    if (imp && !g_leaderboard_info && start && !strcmp(class_name(start), "LeaderboardInfo")) {
        g_leaderboard_info = self;
        LOG("leaderboard info: object %p", self);
    }
    if (!imp) imp = builtin_imp(start, sel);
    if (!imp) { g_msg_missed++; note_missed(start, sel); }
    return imp;
}

/* for super the first argument is a {receiver, class} pair; search from the superclass */
typedef struct { void *receiver; objc_class_t *cls; } objc_super2_t;

void *shim_objc_lookup_super(void *pair, const char *sel) {
    g_msg_count++;
    objc_super2_t *s = (objc_super2_t *) pair;
    if (!s || !is_class(s->cls)) { g_msg_missed++; return NULL; }
    void *imp = lookup_from(s->cls->superclass, sel);
    if (!imp) imp = builtin_imp(s->cls->superclass, sel);
    if (!imp) { g_msg_missed++; note_missed(s->cls, sel); }
    return imp;
}

/* ---------- ownership bookkeeping ---------- */

/*
 * There is no reference counting yet: retain is identity, release does
 * nothing. That is enough for the investigation, and we never hand memory back
 * anyway. The variants with a register in the name (objc_retain_x19 and kin)
 * degenerate into "return" under this behaviour, and all of them point at the
 * shared stub in objc_asm.S.
 */
void *shim_objc_retain(void *o) { return o; }
void shim_objc_release(void *o) { (void) o; }
void *shim_objc_autoreleaseReturnValue(void *o) { return o; }
void *shim_objc_retainAutorelease(void *o) { return o; }
void *shim_objc_retainAutoreleaseReturnValue(void *o) { return o; }
void *shim_objc_retainAutoreleasedReturnValue(void *o) { return o; }
void *shim_objc_claimAutoreleasedReturnValue(void *o) { return o; }
void *shim_objc_unsafeClaimAutoreleasedReturnValue(void *o) { return o; }
void shim_objc_storeStrong(void **loc, void *o) { if (loc) *loc = o; }
void shim_objc_enumerationMutation(void *o) { (void) o; }

/* ---------- creating objects ---------- */

/*
 * Here it is for real: the size comes from the class description, the memory
 * is zeroed, the class is set. Otherwise the very first access to a field
 * would take the game down and the investigation would end early.
 */
long g_objects, g_object_bytes;      /* measured: how many were made and how large */

#if ISAAC_DIAG
/*
 * Who exactly gets created. We never free objects, so a per-class tally is
 * literally a list of leaks sorted by weight. Class names live as long as the
 * image, so we compare pointers rather than strings.
 */
#define CLASSES_COUNTED 24
static struct { const char *name; long count; } g_class_count[CLASSES_COUNTED];

static void count_class(const char *name) {
    if (!name) return;
    for (int i = 0; i < CLASSES_COUNTED; i++) {
        if (!g_class_count[i].name) { g_class_count[i].name = name; g_class_count[i].count = 1; return; }
        if (g_class_count[i].name == name || !strcmp(g_class_count[i].name, name)) {
            g_class_count[i].count++;
            return;
        }
    }
}

void show_class_counts(long frames) {
    /*
     * We print by descending weight and no more than eight: a log line is not
     * infinite, and the first version of this report silently broke off at the
     * eleventh entry — half the objects went unshown, and I first concluded
     * they had no name.
     */
    int order[CLASSES_COUNTED], n = 0;
    long total_by_class = 0;
    for (int i = 0; i < CLASSES_COUNTED && g_class_count[i].name; i++) {
        order[n++] = i;
        total_by_class += g_class_count[i].count;
    }
    for (int i = 0; i < n; i++)
        for (int j = i + 1; j < n; j++)
            if (g_class_count[order[j]].count > g_class_count[order[i]].count) {
                int t = order[i]; order[i] = order[j]; order[j] = t;
            }
    char line[600];
    int k = 0;
    for (int i = 0; i < n && i < 8; i++) {
        int idx = order[i];
        if (k > (int) sizeof(line) - 70) break;
        k += snprintf(line + k, sizeof(line) - (size_t) k, "%s%s %ld (%.2f/frame)",
                      i ? ", " : "", g_class_count[idx].name, g_class_count[idx].count,
                      (double) g_class_count[idx].count / (double) (frames ? frames : 1));
    }
    LOG("   by class (%d distinct, %ld of %ld accounted for): %s",
        n, total_by_class, g_objects, line);
}
#endif

void *shim_objc_alloc(objc_class_t *cls) {
    if (!cls) return NULL;
    class_ro_t *ro = ro_of(cls);
    size_t size = (ro && ro->instanceSize >= sizeof(void *)) ? ro->instanceSize : 64;
    objc_object_t *o = (objc_object_t *) calloc(1, size);
    if (o) {
        o->isa = cls;
        g_objects++;
        g_object_bytes += (long) size;
        DIAG(count_class(class_name(cls)));
    }
    return o;
}

/*
 * Make the object and let it initialise itself.
 *
 * "alloc, then init" is one message on Apple and the second half was skipped
 * here: a class with an init of its own got an object full of zeros instead of
 * its own starting values. Ours mostly have none - the call then finds no
 * method and gives the object back unchanged, exactly as before - but a class
 * that does have one is now asked properly.
 */
static const char *find_sel(const char *want);          /* both are defined below */
void *shim_objc_msgSend(void *self, const char *sel, ...);

void *shim_objc_alloc_init(objc_class_t *cls) {
    void *o = shim_objc_alloc(cls);
    if (!o) return NULL;
    const char *init = find_sel("init");
    if (init) {
        void *r = shim_objc_msgSend(o, init);
        if (r) return r;                    /* init may hand back another object */
    }
    return o;
}

/* the metaclass marker is the low bit of the flags in the description */
#define RO_META 0x1

static int is_meta(objc_class_t *c) {
    class_ro_t *ro = c ? ro_of(c) : NULL;
    return ro && (ro->flags & RO_META);
}

/*
 * [object class] gives its class, while [class class] gives the class itself
 * rather than the metaclass. They can only be told apart by the flag: for a
 * class isa points at the metaclass, and blindly returning isa handed the game
 * a metaclass instead of a class.
 */
void *shim_objc_opt_class(objc_object_t *o) {
    if (!o) return NULL;
    /*
     * The receiver can be a class itself. For the image's classes that shows
     * in the metaclass flag inside isa, but our fakes have no metaclass at
     * all: isa is empty, and a check through it returned ZERO instead of the
     * class.
     *
     * The game reads that zero as "no such class". That is how the gamepad
     * disappeared: before polling GameController the engine calls
     * objc_opt_class(GCController) and, given a zero, quietly skips all pad
     * handling — with the entire surface sitting right there. So we ask
     * directly first: is this a class from the image or from our storage?
     */
    if (is_class((objc_class_t *) o)) return o;
    objc_class_t *isa = o->isa;
    if (is_meta(isa)) return o;          /* the receiver is a class itself */
    return isa;
}

int shim_objc_opt_isKindOfClass(objc_object_t *o, objc_class_t *cls) {
    if (!o || !cls) return 0;
    for (objc_class_t *c = o->isa; c; c = c->superclass)
        if (c == cls) return 1;
    return 0;
}

/* properties: access by offset, with no reference counting */
void *shim_objc_getProperty(void *self, const char *sel, ptrdiff_t off, int atomic) {
    (void) sel; (void) atomic;
    return self ? *(void **) ((char *) self + off) : NULL;
}

void shim_objc_setProperty_atomic(void *self, const char *sel, void *v, ptrdiff_t off) {
    (void) sel;
    if (self) *(void **) ((char *) self + off) = v;
}

void shim_objc_setProperty_nonatomic_copy(void *self, const char *sel, void *v, ptrdiff_t off) {
    (void) sel;
    if (self) *(void **) ((char *) self + off) = v;
}

/* ---------- entering the application ---------- */

/*
 * On iOS UIApplicationMain builds the application, starts the event loop and
 * never returns. Here the loop belongs to Android, so this is only a marker:
 * what matters is that we got this far.
 */
void *shim_objc_msgSend(void *self, const char *sel, ...);

/*
 * The class name arrives here through NSStringFromClass. We have no real
 * NSString yet, so that call returns the class itself and we accept it as
 * such. The substitution is temporary and rests only on the fact that nothing
 * else in the game consumes that string.
 */
void *shim_str_make(const char *text);
const char *shim_str_text(void *o);

void *shim_NSStringFromClass(void *cls) {
    return shim_str_make(is_class((objc_class_t *) cls) ? class_name((objc_class_t *) cls) : "");
}

/* ---------- finding a class by name ---------- */

/*
 * There will be no storyboards in the port: on iOS the view controller is
 * created by the Main file, here it is created by this code. So we have to be
 * able to find a class by name — which is exactly what the image's class list
 * section is for.
 */
static void **g_classlist;
static unsigned long g_nclasses;

void shim_objc_set_classlist(void *base, unsigned long size) {
    g_classlist = (void **) base;
    g_nclasses = size / sizeof(void *);
}

void *shim_objc_class_named(const char *want) {
    for (unsigned long i = 0; i < g_nclasses; i++) {
        objc_class_t *c = (objc_class_t *) g_classlist[i];
        if (is_class(c) && !strcmp(class_name(c), want)) return c;
    }
    return NULL;
}

/* find a selector among the image's method names: they lie in a row, NUL separated */
static const char *g_selbase, *g_selend;

void shim_objc_set_selectors(const void *base, unsigned long size) {
    g_selbase = (const char *) base;
    g_selend = g_selbase + size;
}

/*
 * The application lifecycle.
 *
 * On iOS the system is obliged to warn the game before it goes into the
 * background — the phone may kill it at any moment, which is why the game
 * saves the run there. We only ever sent OnDidBecomeActive, and only once, so
 * the game never learned it had been minimised: the run was not saved, and
 * quitting let a bad room be replayed.
 *
 * The save is not free. It runs the players through
 * Entity_Player::TriggerNewRoom (0x52652c), which calls
 * TemporaryEffects::RemoveAll (0x6fdd80) on player+0x18d8 and keeps only the
 * effects whose item config carries `persistent` (0x6fddd4). An effect from a
 * held item is re-derived right after, but a card's or a pill's one-off effect
 * is simply gone. Leaving the application costs them — that is the game's own
 * price for not losing the run.
 *
 * They must be sent from the game thread: the engine is not built for foreign
 * threads. And on minimising we must wait for them to be handled, otherwise
 * Android freezes the process before the game has written its file.
 */
static void *g_vc;
static volatile int g_lifecycle_event;          /* 1 minimised, 2 restored */

static const char *find_sel(const char *want);   /* declared below, called earlier */

void shim_touch_cancel_all(void);

void shim_app_pause(void) {
    /* the measurement window is closed before we go away, or it is lost */
    { void shim_gl_frames_flush(void); shim_gl_frames_flush(); }
    if (!g_vc) return;
    shim_touch_cancel_all();        /* nothing is held while we are away */
    g_lifecycle_event = 1;
    for (int i = 0; i < 200 && g_lifecycle_event; i++) usleep(2000);   /* up to 0.4 s */
    /*
     * And the saves it made: since hotfix 1.72 the game's writes go onto the
     * writer thread's queue (safefile.c), and Android may freeze or end the
     * process soon after this returns. So this waits on - 2.5 s more at most,
     * well inside the system's patience with onPause - until the minimise is
     * delivered AND the queue is on the disk.
     */
    {
        int shim_safe_pending(void);
        for (int i = 0; i < 1250 && (g_lifecycle_event || shim_safe_pending()); i++) usleep(2000);
    }
}

void shim_app_resume(void) {
    if (!g_vc) return;
    g_lifecycle_event = 2;
}

/*
 * Text typed on the keyboard. Android sends it from its own thread and the
 * engine does not tolerate foreign threads — so we collect it here and hand it
 * over at the start of a frame, the same way as the minimise notifications.
 */
/*
 * Where a long frame actually spends itself.
 *
 * Counters name what was done - so many textures, so many files - but not who
 * was doing it, and the loading frame is 384 milliseconds of which the layer's
 * own counters explain a third. So the frame is SAMPLED: a profiling timer
 * fires every millisecond, and the handler writes down the program counter it
 * interrupted. When a frame turns out to have been a long one, the addresses
 * collected during it are counted up and the top of the list goes into the
 * report - as image offsets for the game's own code, and by library and symbol
 * for everything else.
 *
 * The handler does nothing but store a number, which is all it may do; the
 * counting and the naming happen afterwards, on the frame thread.
 *
 * ITIMER_PROF counts processor time and is delivered to whichever thread is
 * burning it, which during a load is the game thread and no other.
 */
#include <signal.h>
#include <sys/time.h>
#include <sys/syscall.h>
#include <ucontext.h>
#include <pthread.h>
#include <dlfcn.h>

/*
 * The sampler below is a TOOL, and it is off in the build the testers run.
 *
 * It fires a signal on the frame thread every millisecond and writes down the
 * program counter, which is exactly what a frame-rate measurement must not
 * carry: sixty interrupts a frame on a weak phone measure the sampler as much
 * as the game. Set this to one when a long frame needs taking apart again -
 * that is how the loading stall was named - and back to nought afterwards.
 */
/*
 * The frame profiler, ON while a report is open.
 *
 * It was written for exactly the question now standing and then left switched
 * off: of the stall time measured on the owner's phone, texture upload
 * explains nine per cent and nothing accounts for the rest. This samples the
 * frame thread every millisecond of wall time and, for every frame reported
 * as a stall, names the twelve places it was found in - an offset in the
 * image, or a symbol in a system library.
 */
#define PROFILE_FRAMES 1

#define PROF_MAX 4096               /* four seconds of samples at a millisecond */
static volatile unsigned g_prof_n;
static unsigned long g_prof_pc[PROF_MAX];
/*
 * THE CALLER AS WELL AS THE PLACE.
 *
 * The place alone said __ioctl and stopped there, which names the door but not
 * who walked through it. x30 holds the return address, so one more word a
 * sample says which library made the call - and if that is the GL driver, which
 * of its entry points is the one that blocks (the owner, 14.09.2026).
 *
 * A leaf that has not pushed x30 yet gives its true caller; one that has gives
 * whatever x30 was last set to, which is still nearly always the caller. Good
 * enough to name a library and an entry point, which is all that is wanted.
 */
static unsigned long g_prof_lr[PROF_MAX];

/*
 * THE WHOLE WAY UP, NOT ONE STEP (25.10.2026). On a pill the Mali phone's frame
 * thread stood 40-60 ms in futex, and x30 said only "libc +754c0" - libc's own
 * lock, called from libc. Who took the lock stayed hidden one frame further
 * up. The frame records chain through x29: [fp] is the caller's record,
 * [fp + 8] its return address. Every record is checked to lie inside this
 * thread's stack and above the last one before it is read, so a library built
 * without frame records ends the chain early instead of reading wild memory.
 */
#define PROF_UP 6
static unsigned long g_prof_up[PROF_MAX][PROF_UP];
static unsigned long g_stack_lo, g_stack_hi;

static void prof_sample(int sig, siginfo_t *si, void *uc) {
    (void) sig; (void) si;
    if (!uc) return;
    const ucontext_t *c = (const ucontext_t *) uc;
    unsigned n = g_prof_n;
    g_prof_pc[n & (PROF_MAX - 1)] = (unsigned long) c->uc_mcontext.pc;
    g_prof_lr[n & (PROF_MAX - 1)] = (unsigned long) c->uc_mcontext.regs[30];
    unsigned long *up = g_prof_up[n & (PROF_MAX - 1)];
    unsigned long fp = (unsigned long) c->uc_mcontext.regs[29];
    for (int k = 0; k < PROF_UP; k++) {
        unsigned long ret = 0;
        if (g_stack_hi && fp >= g_stack_lo && fp + 16 <= g_stack_hi && !(fp & 7)) {
            ret = ((const unsigned long *) fp)[1] & 0x00FFFFFFFFFFFFFFUL;   /* a tagged or signed address, bare */
            unsigned long next = ((const unsigned long *) fp)[0];
            fp = next > fp ? next : 0;
        } else {
            fp = 0;
        }
        up[k] = ret;
    }
    g_prof_n = n + 1;
}

/*
 * WALL time, and this thread only.
 *
 * The first version counted processor time (ITIMER_PROF) and answered a
 * question nobody asked: a 208 millisecond frame gathered fifty samples, which
 * says the thread was NOT computing for three quarters of it - it was waiting,
 * and waiting leaves no processor time to sample. A monotonic timer bound to
 * the frame thread fires whatever the thread is doing, interrupts the wait,
 * and the handler writes down the place where it was waiting.
 */
static timer_t g_prof_timer;

static void prof_start(void) {
    pthread_attr_t attr;
    if (pthread_getattr_np(pthread_self(), &attr) == 0) {
        void *lo = NULL;
        size_t size = 0;
        if (pthread_attr_getstack(&attr, &lo, &size) == 0 && lo && size) {
            g_stack_lo = (unsigned long) lo;
            g_stack_hi = g_stack_lo + size;
        }
        pthread_attr_destroy(&attr);
    }
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_sigaction = prof_sample;
    sa.sa_flags = SA_SIGINFO | SA_RESTART;
    sigemptyset(&sa.sa_mask);
    if (sigaction(SIGPROF, &sa, NULL) != 0) return;
    struct sigevent sev;
    memset(&sev, 0, sizeof(sev));
    sev.sigev_notify = SIGEV_THREAD_ID;
    sev.sigev_signo = SIGPROF;
    sev.sigev_notify_thread_id = (int) syscall(__NR_gettid);
    if (timer_create(CLOCK_MONOTONIC, &sev, &g_prof_timer) != 0) return;
    struct itimerspec its;
    its.it_interval.tv_sec = 0; its.it_interval.tv_nsec = 1000000;   /* a millisecond */
    its.it_value = its.it_interval;
    timer_settime(g_prof_timer, 0, &its, NULL);
}

/*
 * WHICH SYSCALL, AND ON WHAT.
 *
 * The sampler says the frame stands in __ioctl and the return register says
 * libc's own ioctl wrapper - one level short of the answer, and a stack walk
 * inside a signal handler is not a thing to do. The kernel will tell us
 * instead: /proc/self/task/<tid>/syscall holds the syscall the thread is in
 * right now, with its arguments, and it can be read from ANOTHER thread with
 * no signals and no unwinding at all.
 *
 * The first argument of ioctl is a descriptor, and a descriptor has a name in
 * /proc/self/fd - so the answer comes out as "ioctl on /dev/kgsl-3d0" or
 * "ioctl on /dev/binder", which is the thing worth knowing (14.09.2026).
 *
 * A read every two milliseconds off a thread of its own. Report builds only.
 */
#include <fcntl.h>
#include <pthread.h>

#define SYS_RING 2048
static volatile unsigned g_sys_n;
static struct { int nr; unsigned long a0, a1; } g_sys[SYS_RING];
static volatile int g_sys_tid;

static void *sys_watch(void *unused) {
    (void) unused;
    char path[64];
    snprintf(path, sizeof(path), "/proc/self/task/%d/syscall", g_sys_tid);
    int fd = open(path, O_RDONLY);
    if (fd < 0) return NULL;
    for (;;) {
        char buf[160];
        ssize_t n = pread(fd, buf, sizeof(buf) - 1, 0);
        if (n > 0) {
            buf[n] = 0;
            int nr = -1;
            unsigned long a0 = 0, a1 = 0;
            if (buf[0] == 'r') {                 /* "running": on a processor, not in a call */
                nr = -1;
            } else {
                nr = (int) strtol(buf, NULL, 10);
                const char *s = strchr(buf, ' ');
                if (s) { a0 = strtoul(s + 1, NULL, 0); s = strchr(s + 1, ' '); }
                if (s) a1 = strtoul(s + 1, NULL, 0);   /* for an ioctl this is the request */
            }
            unsigned k = g_sys_n;
            g_sys[k & (SYS_RING - 1)].nr = nr;
            g_sys[k & (SYS_RING - 1)].a0 = a0;
            g_sys[k & (SYS_RING - 1)].a1 = a1;
            g_sys_n = k + 1;
        }
        struct timespec t = { 0, 2000000 };      /* two milliseconds */
        nanosleep(&t, NULL);
    }
    return NULL;
}

static void sys_watch_start(void) {
    g_sys_tid = (int) syscall(__NR_gettid);
    pthread_t th;
    if (pthread_create(&th, NULL, sys_watch, NULL) == 0) pthread_detach(th);
}

/* the name behind a descriptor, for the line below */
static void fd_name(unsigned long fd, char *out, size_t room) {
    char path[64], link[160];
    snprintf(path, sizeof(path), "/proc/self/fd/%lu", fd);
    ssize_t n = readlink(path, link, sizeof(link) - 1);
    if (n <= 0) { snprintf(out, room, "fd %lu", fd); return; }
    link[n] = 0;
    snprintf(out, room, "%s", link);
}

/* what the frame thread was doing, by the kernel's own account */
static void sys_report(int took_ms) {
    if (!g_sys_tid || took_ms < 1) return;
    unsigned to = g_sys_n;
    unsigned want = (unsigned) (took_ms / 2) + 1;
    if (want > SYS_RING) want = SYS_RING;
    if (to < want) return;
    struct { int nr; unsigned long a0, a1; int n; } seen[12];
    int kinds = 0, total = 0;
    for (unsigned i = to - want; i != to; i++) {
        int nr = g_sys[i & (SYS_RING - 1)].nr;
        unsigned long a0 = g_sys[i & (SYS_RING - 1)].a0;
        unsigned long a1 = g_sys[i & (SYS_RING - 1)].a1;
        if (nr != 29) a0 = a1 = 0;               /* the descriptor and the request are ioctl's */
        int k;
        for (k = 0; k < kinds; k++) if (seen[k].nr == nr && seen[k].a0 == a0 && seen[k].a1 == a1) break;
        if (k == kinds && kinds < 12) {
            seen[kinds].nr = nr; seen[kinds].a0 = a0; seen[kinds].a1 = a1; seen[kinds].n = 0; kinds++;
        }
        if (k < 12) seen[k].n++;
        total++;
    }
    if (!total) return;
    for (int shown = 0; shown < 5; shown++) {
        int best = -1;
        for (int k = 0; k < kinds; k++) if (best < 0 || seen[k].n > seen[best].n) best = k;
        if (best < 0 || seen[best].n <= 0) break;
        char what[200], line[260];
        if (seen[best].nr == -1) snprintf(what, sizeof(what), "on a processor");
        else if (seen[best].nr == 29) {
            char name[160];
            fd_name(seen[best].a0, name, sizeof(name));
            /*
             * The request tells the operation apart: the low byte is the
             * driver's own number, the top bits the direction and the size of
             * the argument. Written raw - decoding it belongs on this side.
             */
            snprintf(what, sizeof(what), "ioctl %#lx on %s", seen[best].a1, name);
        } else snprintf(what, sizeof(what), "syscall %d", seen[best].nr);
        snprintf(line, sizeof(line), "%3d%% (%d of %d reads) %s",
                 seen[best].n * 100 / total, seen[best].n, total, what);
        report_line("doing", line);
        seen[best].n = 0;
    }
}

/*
 * NAMING AN ADDRESS COSTS MORE THAN THE STALL IT DESCRIBES, unless it is
 * remembered.
 *
 * dladdr takes the linker's lock and walks every loaded object. It was called
 * afresh for each of the two dozen addresses of every stall line - and the
 * addresses hardly ever change, the same libc offset coming up in stall after
 * stall. Counted on the owner's phone (14.09.2026): of 478 ms of stalls in
 * play, 148 were inside linker64, called from linker64. A THIRD OF THE TROUBLE
 * WAS THE REPORT DESCRIBING IT - the same lesson as the readback grid in gl.c
 * and, before that, the GPU clock. A measurement has to be cheaper than what it
 * measures, or it measures itself.
 *
 * So each answer is kept. The table never empties: the places a frame stalls in
 * are a handful, and the first stall pays for all the rest.
 */
#define NAMED_MAX 192
static struct { unsigned long pc; char text[96]; } g_named[NAMED_MAX];
static int g_named_n;

/* one address, named: the game by its offset, a library by its symbol */
static void name_place(unsigned long pc, unsigned long base, char *out, size_t room) {
    if (base && pc > base && pc - base < 0xb20000) {
        snprintf(out, room, "the game at %lx", pc - base);
        return;
    }
    for (int i = 0; i < g_named_n; i++)
        if (g_named[i].pc == pc) { snprintf(out, room, "%s", g_named[i].text); return; }
    Dl_info info;
    memset(&info, 0, sizeof(info));
    const char *lib = "?", *sym = "";
    unsigned long off = 0;
    if (dladdr((const void *) pc, &info) && info.dli_fname) {
        const char *slash = strrchr(info.dli_fname, '/');
        lib = slash ? slash + 1 : info.dli_fname;
        if (info.dli_sname) sym = info.dli_sname;
        off = pc - (unsigned long) info.dli_fbase;
    }
    snprintf(out, room, "%s %s+%lx", lib, sym, off);
    if (g_named_n < NAMED_MAX) {
        g_named[g_named_n].pc = pc;
        snprintf(g_named[g_named_n].text, sizeof(g_named[g_named_n].text), "%s", out);
        g_named_n++;
    }
}

/* who CALLED the place the frame was waiting in, gathered the same way */
static void prof_callers(unsigned from, unsigned to) {
    if (to - from < 4 || to - from > PROF_MAX) return;
    unsigned long base = shim_image_base();
    unsigned long where[24];
    int count[24];
    int kinds = 0, total = 0;
    for (unsigned i = from; i != to; i++) {
        unsigned long lr = g_prof_lr[i & (PROF_MAX - 1)];
        if (!lr) continue;
        unsigned long key = lr & ~0x3FUL;
        int seen = 0;
        for (int k = 0; k < kinds; k++)
            if (where[k] == key) { count[k]++; seen = 1; break; }
        if (!seen && kinds < 24) { where[kinds] = key; count[kinds] = 1; kinds++; }
        total++;
    }
    if (!total) return;
    for (int shown = 0; shown < 6; shown++) {
        int best = -1;
        for (int k = 0; k < kinds; k++) if (best < 0 || count[k] > count[best]) best = k;
        if (best < 0 || count[best] <= 0) break;
        char place[200], line[240];
        name_place(where[best], base, place, sizeof(place));
        snprintf(line, sizeof(line), "%3d%% (%d of %d ms) called from %s",
                 count[best] * 100 / total, count[best], total, place);
        report_line("who", line);
        count[best] = 0;
    }
}

/* the chains of callers above the wait, the same samples, the busiest three */
static void prof_chains(unsigned from, unsigned to) {
    if (to - from < 4 || to - from > PROF_MAX) return;
    unsigned long base = shim_image_base();
    int first[16], count[16];
    int kinds = 0, total = 0;
    for (unsigned i = from; i != to; i++) {
        const unsigned long *up = g_prof_up[i & (PROF_MAX - 1)];
        if (!up[0]) continue;
        int k;
        for (k = 0; k < kinds; k++)
            if (!memcmp(g_prof_up[(unsigned) first[k] & (PROF_MAX - 1)], up, sizeof(g_prof_up[0]))) break;
        if (k == kinds && kinds < 16) { first[kinds] = (int) i; count[kinds] = 0; kinds++; }
        if (k < 16) count[k]++;
        total++;
    }
    if (!total) return;
    for (int shown = 0; shown < 3; shown++) {
        int best = -1;
        for (int k = 0; k < kinds; k++) if (best < 0 || count[k] > count[best]) best = k;
        if (best < 0 || count[best] * 10 < total) break;
        const unsigned long *up = g_prof_up[(unsigned) first[best] & (PROF_MAX - 1)];
        char line[900];
        int at = snprintf(line, sizeof(line), "%3d%% (%d of %d ms) up from the wait:",
                          count[best] * 100 / total, count[best], total);
        for (int k = 0; k < PROF_UP && up[k] && at > 0 && at < (int) sizeof(line) - 1; k++) {
            char place[200];
            name_place(up[k], base, place, sizeof(place));
            at += snprintf(line + at, sizeof(line) - (size_t) at, "%s %s", k ? " <" : "", place);
        }
        report_line("chain", line);
        count[best] = 0;
    }
}

/* the samples of one frame, counted and named, into the report */
static void prof_report(unsigned from, unsigned to) {
    if (to - from < 4 || to - from > PROF_MAX) return;
    unsigned long base = shim_image_base();
    unsigned long where[PROF_MAX / 8];
    int count[PROF_MAX / 8];
    int kinds = 0, total = 0;
    for (unsigned i = from; i != to; i++) {
        unsigned long pc = g_prof_pc[i & (PROF_MAX - 1)];
        if (!pc) continue;
        /* a sixteen-word bucket: near enough to name a place, coarse enough to gather */
        unsigned long key = pc & ~0x3FUL;
        int seen = 0;
        for (int k = 0; k < kinds; k++)
            if (where[k] == key) { count[k]++; seen = 1; break; }
        if (!seen && kinds < (int) (sizeof(count) / sizeof(count[0]))) {
            where[kinds] = key; count[kinds] = 1; kinds++;
        }
        total++;
    }
    if (!total) return;
    for (int shown = 0; shown < 12; shown++) {
        int best = -1;
        for (int k = 0; k < kinds; k++) if (best < 0 || count[k] > count[best]) best = k;
        if (best < 0 || count[best] <= 0) break;
        unsigned long pc = where[best];
        char line[220];
        if (base && pc > base && pc - base < 0xb20000) {
            snprintf(line, sizeof(line), "%3d%% (%d of %d ms) in the game at %lx",
                     count[best] * 100 / total, count[best], total, pc - base);
        } else {
            char place[200];
            name_place(pc, 0, place, sizeof(place));      /* through the same kept table */
            snprintf(line, sizeof(line), "%3d%% (%d of %d ms) in %s",
                     count[best] * 100 / total, count[best], total, place);
        }
        report_line("where", line);
        count[best] = 0;
    }
}

#if ISAAC_DIAG
/*
 * THE WHOLE MINUTE, NOT ITS STALLS (16.10.2026). A weak phone that "lags
 * badly" all the time has no stalls to name: every frame is a little late, and
 * the stall lines above stay silent. So the same samples - where the frame
 * thread stood, every millisecond of wall time, and what the kernel says it
 * was in - are also gathered over the whole frames window and printed beside
 * the frames line: the places, the libraries they lie in, and the calls the
 * thread waited in. One window answers whether the minute went to the game's
 * own code, the GL driver, a wait for the card, a wait for the compositor or
 * our own sleep to the slot.
 */
#define WIN_PLACES 384
static unsigned long g_win_key[WIN_PLACES];
static long g_win_cnt[WIN_PLACES];
static int g_win_kinds;
static long g_win_total, g_win_lost;
static unsigned g_win_at, g_win_sys_at;
static struct { int nr; unsigned long a0, a1; long n; } g_win_sys[24];
static int g_win_sys_kinds;
static long g_win_sys_total;

/* once a frame: the samples since the last call, into the window's counts */
static void prof_window_take(void) {
    unsigned to = g_prof_n;
    if (to - g_win_at > PROF_MAX) g_win_at = to - PROF_MAX;     /* overrun: the oldest are gone */
    for (unsigned i = g_win_at; i != to; i++) {
        unsigned long pc = g_prof_pc[i & (PROF_MAX - 1)];
        if (!pc) continue;
        unsigned long key = pc & ~0x3FUL;
        int k;
        for (k = 0; k < g_win_kinds; k++) if (g_win_key[k] == key) break;
        if (k == g_win_kinds) {
            if (g_win_kinds == WIN_PLACES) { g_win_lost++; g_win_total++; continue; }
            g_win_key[k] = key; g_win_cnt[k] = 0; g_win_kinds++;
        }
        g_win_cnt[k]++;
        g_win_total++;
    }
    g_win_at = to;
    unsigned sto = g_sys_n;
    if (sto - g_win_sys_at > SYS_RING) g_win_sys_at = sto - SYS_RING;
    for (unsigned i = g_win_sys_at; i != sto; i++) {
        int nr = g_sys[i & (SYS_RING - 1)].nr;
        unsigned long a0 = g_sys[i & (SYS_RING - 1)].a0, a1 = g_sys[i & (SYS_RING - 1)].a1;
        if (nr != 29) a0 = a1 = 0;
        int k;
        for (k = 0; k < g_win_sys_kinds; k++)
            if (g_win_sys[k].nr == nr && g_win_sys[k].a0 == a0 && g_win_sys[k].a1 == a1) break;
        if (k == g_win_sys_kinds) {
            if (k == 24) { g_win_sys_total++; continue; }
            g_win_sys[k].nr = nr; g_win_sys[k].a0 = a0; g_win_sys[k].a1 = a1; g_win_sys[k].n = 0;
            g_win_sys_kinds++;
        }
        g_win_sys[k].n++;
        g_win_sys_total++;
    }
    g_win_sys_at = sto;
}

/* gl.c, right after the frames line: the window's places, libraries and waits */
void shim_prof_window(void) {
    if (!g_win_total) return;
    unsigned long base = shim_image_base();
    char line[1400];
    /* by library first: the game, our layer, the GL driver, libc... */
    struct { char lib[48]; long n; } libs[16];
    int nl = 0;
    for (int k = 0; k < g_win_kinds; k++) {
        char place[200], lib[48];
        if (base && g_win_key[k] > base && g_win_key[k] - base < 0xb20000) snprintf(lib, sizeof(lib), "the game");
        else {
            name_place(g_win_key[k], 0, place, sizeof(place));
            const char *sp = strchr(place, ' ');
            size_t len = sp ? (size_t) (sp - place) : strlen(place);
            if (len >= sizeof(lib)) len = sizeof(lib) - 1;
            memcpy(lib, place, len);
            lib[len] = 0;
        }
        int j;
        for (j = 0; j < nl; j++) if (!strcmp(libs[j].lib, lib)) break;
        if (j == nl) { if (nl == 16) continue; snprintf(libs[j].lib, sizeof(libs[j].lib), "%s", lib); libs[j].n = 0; nl++; }
        libs[j].n += g_win_cnt[k];
    }
    int at = snprintf(line, sizeof(line), "the frame thread over the window, %ld ms sampled, by library:", g_win_total);
    for (int shown = 0; shown < 8 && at < (int) sizeof(line) - 60; shown++) {
        int best = -1;
        for (int j = 0; j < nl; j++) if (libs[j].n > 0 && (best < 0 || libs[j].n > libs[best].n)) best = j;
        if (best < 0) break;
        at += snprintf(line + at, sizeof(line) - (size_t) at, " %s %.1f%%,", libs[best].lib,
                       (double) libs[best].n * 100.0 / (double) g_win_total);
        libs[best].n = 0;
    }
    if (g_win_lost) snprintf(line + at, sizeof(line) - (size_t) at, " (%ld ms in places past the table)", g_win_lost);
    report_line("minute", line);

    /* the places themselves, sixteen of them */
    at = snprintf(line, sizeof(line), "the frame thread's places:");
    for (int shown = 0; shown < 16 && at < (int) sizeof(line) - 120; shown++) {
        int best = -1;
        for (int k = 0; k < g_win_kinds; k++) if (g_win_cnt[k] > 0 && (best < 0 || g_win_cnt[k] > g_win_cnt[best])) best = k;
        if (best < 0) break;
        char place[200];
        if (base && g_win_key[best] > base && g_win_key[best] - base < 0xb20000)
            snprintf(place, sizeof(place), "the game at %lx", g_win_key[best] - base);
        else name_place(g_win_key[best], 0, place, sizeof(place));
        at += snprintf(line + at, sizeof(line) - (size_t) at, " %.1f%% %s;",
                       (double) g_win_cnt[best] * 100.0 / (double) g_win_total, place);
        g_win_cnt[best] = 0;
    }
    report_line("minute", line);

    /* and what the kernel said the thread was in */
    if (g_win_sys_total) {
        at = snprintf(line, sizeof(line), "the frame thread was in (%ld reads):", g_win_sys_total);
        for (int shown = 0; shown < 8 && at < (int) sizeof(line) - 200; shown++) {
            int best = -1;
            for (int k = 0; k < g_win_sys_kinds; k++)
                if (g_win_sys[k].n > 0 && (best < 0 || g_win_sys[k].n > g_win_sys[best].n)) best = k;
            if (best < 0) break;
            char what[200];
            if (g_win_sys[best].nr == -1) snprintf(what, sizeof(what), "on a processor");
            else if (g_win_sys[best].nr == 29) {
                char name[160];
                fd_name(g_win_sys[best].a0, name, sizeof(name));
                snprintf(what, sizeof(what), "ioctl %#lx on %s", g_win_sys[best].a1, name);
            } else if (g_win_sys[best].nr == 115) snprintf(what, sizeof(what), "clock_nanosleep");
            else if (g_win_sys[best].nr == 98) snprintf(what, sizeof(what), "futex");
            else if (g_win_sys[best].nr == 22) snprintf(what, sizeof(what), "epoll_pwait");
            else if (g_win_sys[best].nr == 101) snprintf(what, sizeof(what), "nanosleep");
            else snprintf(what, sizeof(what), "syscall %d", g_win_sys[best].nr);
            at += snprintf(line + at, sizeof(line) - (size_t) at, " %.1f%% %s;",
                           (double) g_win_sys[best].n * 100.0 / (double) g_win_sys_total, what);
            g_win_sys[best].n = 0;
        }
        report_line("minute", line);
    }
    g_win_kinds = 0;
    g_win_total = g_win_lost = 0;
    g_win_sys_kinds = 0;
    g_win_sys_total = 0;
}
#endif

#define INPUT_QUEUE 64
void *shim_ime_target(void);

void *shim_str_make(const char *);

static char g_input[INPUT_QUEUE][8];
static volatile int g_input_head, g_input_tail;

/* the last text the IME pushed, for the difficulty screen's report (menu.c) */
#if ISAAC_DIAG
static char g_ime_last[2][8];
static volatile int g_ime_last_n;
#endif

void shim_ime_recent(char *out, unsigned long room) {
    if (!out || !room) return;
    out[0] = 0;
#if ISAAC_DIAG
    int n = g_ime_last_n;
    if (!n) { snprintf(out, room, "none"); return; }
    const char *a = g_ime_last[(n - 1) & 1];
    snprintf(out, room, "%s", !strcmp(a, "\n") ? "\\n" : (a[0] ? a : "(delete)"));
#endif
}

void shim_ime_push(const char *text) {
#if ISAAC_DIAG
    if (text) { snprintf(g_ime_last[g_ime_last_n & 1], sizeof(g_ime_last[0]), "%s", text); g_ime_last_n++; }
#endif
    {
        int shim_report_active(void);
        void report_line(const char *kind, const char *text);
        if (shim_report_active()) {
            static int said;
            if (said < 10) {
                said++;
                char line[80];
                snprintf(line, sizeof(line), "pushed \"%s\"", text ? text : "(null)");
                report_line("ime", line);
            }
        }
    }
    int next_slot = (g_input_tail + 1) % INPUT_QUEUE;
    if (next_slot == g_input_head) return;      /* overflow: we lose a letter but do not crash */
    snprintf(g_input[g_input_tail], sizeof(g_input[0]), "%s", text);
    g_input_tail = next_slot;
}

/*
 * Bind the input object to the game's shell.
 *
 * insertText: on AAPLOpenGLView first asks itself for _isaacShell and puts the
 * letter there. We create our own object empty, and that field is zero in it —
 * so the method ran to no effect. The game itself gives the view its shell
 * with setIsaacShellInstance:; we do exactly the same.
 */
/*
 * The hardware keyboard, delivered the way UIKit delivers it.
 *
 * On iOS a key reaches the game as a press: the view's pressesBegan: hands
 * every press to the shell with onKeyDown:, and the shell reads the code and
 * the modifiers off it. We call the same method with a press of our own
 * (uikit.c), so everything below - the table that turns a HID code into the
 * game's own, the bindings, the menus - is the game's own code, untouched.
 *
 * Delivered between frames, like touches: the game is not built for two
 * threads, and a key arriving mid-frame would be read from under it.
 */
void *shim_key_press(long code, long mods);
int shim_key_next_ex(int *code, int *down, int *mods, int *ch, int *repeat);
int shim_report_active(void);
void report_line(const char *kind, const char *text);
int shim_eid_readable(const void *p, unsigned long n);
int shim_sticks_in_game(void);
int shim_gpu_probe_recent(void);           /* gpu.c: the report's own slow frame */

/*
 * THE GAME'S KEYBOARD DEVICE, as the image has it (batch 2, K5/K6, 01.10.2026).
 *
 * The shell's onKeyDown: (0x51cc) turns the HID code into the game's own GLFW
 * number through the table at 0x945ad4 and hands it to 0x676bd0, which sets a
 * byte in the device's SET (0x8ca88c -> 0x8ddc9c); onKeyUp: clears it
 * (0x8ddcb0). Once a frame the device's update 0x8ddc40 copies CURRENT into
 * PREVIOUS and SET into CURRENT, and everything the game asks - held, pressed
 * this frame, "is any key down", the Controls capture 0x8ddb34 - reads CURRENT
 * and PREVIOUS only. So a press and its release that both land between two
 * updates are never seen at all: the device keeps no queue, only a state.
 */
#define KB_INPUT        0xac5528    /* the input system */
#define KB_DEVICE       0xa8        /* input + 0xa8: the keyboard device (0x8ca88c) */
#define KB_VTABLE       0xa94958
#define KB_ENABLED      0xd         /* byte: the device is switched on */
#define KB_ACTIONS      0x1c        /* u32: actions with bindings */
#define KB_BINDINGS     0x20        /* int *[action]: the GLFW keys bound to each */
#define KB_PER_ACTION   0xabec7c    /* u32: keys per action, the same for every device */
#define KB_CURRENT      0xac59d8    /* a byte per GLFW key: the state the game reads */
#define KB_PREVIOUS     0xac61da    /* the same a frame before */
#define KB_SET          0xac5dd9    /* the same as the presses left it since the update */
#define KB_KEYS         0x401
#define KB_HID_TO_GLFW  0x945ad4    /* int per HID usage, -1 for a key the game does not know */
#define KB_GAME         0xac3b90
#define KB_PAUSE_MENU   (0x10dd20 + 0x2b8)   /* Game: non-zero while the pause menu is up */
#define ACTION_PAUSE    12
#define GLFW_ESCAPE     256
#define HID_ESCAPE      41
#define HID_P           19

/* the GLFW number the game makes of a HID usage, -1 when it knows none */
static int glfw_of(int hid) {
    unsigned long b = shim_image_base();
    if (!b || hid < 0 || hid > 255) return -1;
    return ((const int *) (b + KB_HID_TO_GLFW))[hid];
}

/* the HID usage the game reads as this GLFW key (the table turned round), -1 none */
static int hid_of_glfw(int glfw) {
    static short rev[KB_KEYS];
    static int built;
    unsigned long b = shim_image_base();
    if (!b || glfw < 0 || glfw >= KB_KEYS) return -1;
    if (!built) {
        for (int k = 0; k < KB_KEYS; k++) rev[k] = -1;
        const int *t = (const int *) (b + KB_HID_TO_GLFW);
        for (int h = 0; h < 256; h++)               /* the lowest usage wins: 0x39 CapsLock, not 0x82 */
            if (t[h] >= 0 && t[h] < KB_KEYS && rev[t[h]] < 0) rev[t[h]] = (short) h;
        built = 1;
    }
    return rev[glfw];
}

/* the game's keyboard device, or nought while there is none to read */
static unsigned long kb_device(void) {
    unsigned long b = shim_image_base();
    if (!b) return 0;
    unsigned long dev = *(const unsigned long *) (b + KB_INPUT + KB_DEVICE);
    if (!dev || !shim_eid_readable((const void *) dev, KB_BINDINGS + 8)) return 0;
    if (*(const unsigned long *) dev != b + KB_VTABLE) return 0;
    return dev;
}

/*
 * The keys bound to an action in the device's LIVE table - what Controls and
 * inputconfigs.dat made of it, not the defaults. Answers how many were written
 * (-1 where a key slot is empty), or -1 when there is no table to read.
 */
static int kb_bound(unsigned long dev, int action, int *out, int room) {
    unsigned long b = shim_image_base();
    if (!b || !dev) return -1;
    int per = *(const int *) (b + KB_PER_ACTION);
    unsigned acts = *(const unsigned *) (dev + KB_ACTIONS);
    const int *const *map = *(const int *const **) (dev + KB_BINDINGS);
    if (per <= 0 || per > 16 || (unsigned) action >= acts || acts > 256 || !map) return -1;
    if (!shim_eid_readable(map + action, sizeof(void *))) return -1;
    const int *row = map[action];
    if (!row || !shim_eid_readable(row, (unsigned long) per * sizeof(int))) return -1;
    int n = per < room ? per : room;
    for (int i = 0; i < n; i++) out[i] = row[i];
    return n;
}

/*
 * IS THIS KEY THE RESTART KEY (reset.c, the keyboard's hand): a HID usage
 * whose GLFW key stands in action 23's row of the device's live table - R in
 * the defaults, or what inputconfigs.dat binds. Asked per key event, never per
 * frame; frame thread (the delivery), where the game writes the table.
 */
#define ACTION_RESTART  23

/*
 * A PC TABLE, TURNED INTO THE iOS NUMBERING (1.74, the owner's decision;
 * the research notes of 11.10.2026, keyboard, §4 and §9).
 *
 * The iOS edition put seven touch actions at 16..22 and moved everything from
 * restart on up by seven: R at 23, F 24, M 25, Enter 26, the menu's arrows
 * 27..30, Q/E/Tab 31..33. The PC's inputconfigs.dat has the same <Controller
 * Name="Keyboard"> and the same "action-slot" attributes in the PC's
 * numbering: R at 16 ... the menu's arrows at 20..23, Tab at 26 (the owner's PC
 * file, 13.09.2026: exactly the iOS defaults moved down by seven). The game
 * reads it index for index (0x866374: a record of 35 rows, filled first from
 * the pad's defaults 0xa9c5c0 - both slots - and then by the attributes;
 * 0x865fac hands all 35 rows to the device), so with such a file the keyboard
 * had the Down arrow at restart (23) and the pad's codes - no key at all - at
 * the menu's arrows and Q/E/Tab (27..33).
 *
 * A table in the iOS numbering never has a key in 16..22 (the defaults leave
 * them empty, Controls has no row for them). A key there is the PC's
 * numbering, and the device's LIVE table is turned round once: PC rows 16..26
 * go to 23..33 (a row with no key gets the iOS default), 16..22 are emptied.
 * The file is not touched. The game writes it from the live table only when
 * the keyboard's own bindings are saved (0x866a50: that device's record
 * refreshed by 0x866034, then every record written) - then in the iOS
 * numbering, which reads back as it is. Until then the PC file stays, and is
 * turned round again at the next launch; a turned table has nothing in
 * 16..22, so nothing is ever turned twice. A record applied again later
 * (0x8660f8) is caught at the next key. Pads are not touched: only the
 * keyboard device (kb_device) is.
 *
 * Asked once a frame that has keys, before any of them reaches the game or the
 * restart key's check.
 */
#define ACTION_TOUCH_FIRST  16
#define ACTION_TOUCH_LAST   22
#define PC_MOVED_FIRST      16          /* PC 16..26 are iOS 23..33 */
#define PC_MOVED_LAST       26
#define PC_SHIFT            7
#define KB_DEFAULTS         0xa9b1e0    /* (action, GLFW key) pairs, the keyboard's defaults */
#define KB_DEFAULTS_N       0xa9b1e8
#define GLFW_KEY_FIRST      32          /* GLFW_KEY_SPACE */
#define GLFW_KEY_LAST       348         /* GLFW_KEY_MENU */

static int glfw_key(int v) { return v >= GLFW_KEY_FIRST && v <= GLFW_KEY_LAST; }

static int g_kb_turned;                 /* the table was turned round (times, the report) */

/* a key in the touch actions' rows: the PC's numbering */
static int kb_pc_numbered(unsigned long dev) {
    int keys[16];
    for (int a = ACTION_TOUCH_FIRST; a <= ACTION_TOUCH_LAST; a++) {
        int n = kb_bound(dev, a, keys, 16);
        for (int i = 0; i < n; i++) if (glfw_key(keys[i])) return 1;
    }
    return 0;
}

/* the device's own store (0x8a8a58, its vtable +0x110), with its own bounds */
static void kb_set(unsigned long dev, int action, int slot, int key) {
    unsigned long b = shim_image_base();
    unsigned acts = *(const unsigned *) (dev + KB_ACTIONS);
    unsigned per = *(const unsigned *) (b + KB_PER_ACTION);
    if ((unsigned) action >= acts || (unsigned) slot >= per) return;
    int *const *map = *(int *const **) (dev + KB_BINDINGS);
    map[action][slot] = key;
}

static void kb_table_to_ios(void) {
    unsigned long b = shim_image_base(), dev = kb_device();
    if (!b || !dev || !kb_pc_numbered(dev)) return;
    unsigned acts = *(const unsigned *) (dev + KB_ACTIONS);
    if (acts < PC_MOVED_LAST + PC_SHIFT + 1) return;
    /* every source row first: 23..26 are read before they are written */
    int rows[PC_MOVED_LAST - PC_MOVED_FIRST + 1][16], n[PC_MOVED_LAST - PC_MOVED_FIRST + 1];
    for (int a = PC_MOVED_FIRST; a <= PC_MOVED_LAST; a++) {
        n[a - PC_MOVED_FIRST] = kb_bound(dev, a, rows[a - PC_MOVED_FIRST], 16);
        if (n[a - PC_MOVED_FIRST] <= 0) return;                 /* unreadable: nothing half done */
    }
    const int *defs = *(const int *const *) (b + KB_DEFAULTS);
    int ndefs = *(const int *) (b + KB_DEFAULTS_N);
    if (!defs || ndefs <= 0 || ndefs > 64 || !shim_eid_readable(defs, (unsigned long) ndefs * 8)) defs = NULL;
    for (int a = PC_MOVED_FIRST; a <= PC_MOVED_LAST; a++) {
        const int *row = rows[a - PC_MOVED_FIRST];
        int per = n[a - PC_MOVED_FIRST], to = a + PC_SHIFT, has = 0;
        for (int k = 0; k < per; k++) if (glfw_key(row[k])) has = 1;
        if (has) {
            for (int k = 0; k < per; k++) kb_set(dev, to, k, glfw_key(row[k]) ? row[k] : -1);
            continue;
        }
        int k = 0;                                              /* no key in the file's row: the iOS default */
        for (int i = 0; defs && i < ndefs && k < per; i++) if (defs[i * 2] == to) kb_set(dev, to, k++, defs[i * 2 + 1]);
        for (; k < per; k++) kb_set(dev, to, k, -1);
    }
    for (int a = ACTION_TOUCH_FIRST; a <= ACTION_TOUCH_LAST; a++)
        for (int k = 0; k < n[0]; k++) kb_set(dev, a, k, -1);
    g_kb_turned++;
    DIAG({ static int said; if (said < 4) { said++;
        LOG("keyboard: the bindings came in the PC's numbering - turned into the iOS one"
            " (actions 16..26 -> 23..33, 16..22 emptied)"); } });
}

int shim_keys_restart_bound(int hid) {
    int glfw = glfw_of(hid);
    if (glfw < 0) return 0;
    int keys[16];
    int n = kb_bound(kb_device(), ACTION_RESTART, keys, 16);
    for (int i = 0; i < n; i++) if (keys[i] == glfw) return 1;
    return 0;
}

#if ISAAC_DIAG
static int g_keys_said;                 /* the keyboard's report lines, within their budget */
#define KEYS_LINES_MAX 120
static void keys_say(const char *line) {
    if (!shim_report_active() || g_keys_said >= KEYS_LINES_MAX) return;
    g_keys_said++;
    report_line("keys", line);
}
#endif

/*
 * ESC PAUSES A RUN, as on the PC (the owner's eighth round, 24.09.2026) - by
 * way of THE KEY THE PLAYER BOUND TO PAUSE (K6). The device's own bindings put
 * pause on P and Esc on "back", and back does nothing in a run that is not
 * paused. So in a run with the pause screen down Esc goes in as whatever key
 * pause is bound to right now in the live table, read on every press: rebinding
 * pause in Controls moves Esc with it. Pause bound to Esc itself - nothing to
 * change; pause bound to nothing - Esc stays Esc, and the report says so.
 * Paused or in a menu it stays Esc and backs out, as the PC does with it there.
 */
static int esc_target(void) {
    unsigned long b = shim_image_base();
    unsigned long game = b ? *(const unsigned long *) (b + KB_GAME) : 0;
    int paused = game ? *(const int *) (game + KB_PAUSE_MENU) != 0 : 1;
    if (!shim_sticks_in_game() || paused) return HID_ESCAPE;
    int keys[16];
    int n = kb_bound(kb_device(), ACTION_PAUSE, keys, 16);
    if (n < 0) {
        DIAG({ static int said; if (!said) { said = 1;
            keys_say("esc: the keyboard's bindings could not be read, P as the default"); } });
        return HID_P;
    }
    int first = -1;
    for (int i = 0; i < n; i++) {
        if (keys[i] == GLFW_ESCAPE) return HID_ESCAPE;      /* Esc pauses by itself */
        if (keys[i] >= 0 && first < 0) first = keys[i];
    }
    if (first < 0) {
        DIAG({ static int said; if (said < 3) { said++; keys_say("esc: pause has no key, Esc left as Esc"); } });
        return HID_ESCAPE;
    }
    int hid = hid_of_glfw(first);
    return hid >= 0 ? hid : HID_ESCAPE;
}

/*
 * One key into the game's shell. Answers the GLFW key the game made of it (-1
 * for one it does not know): the delivery below watches that key's byte.
 */
static int key_to_shell(int code, int down, int mods) {
    static int esc_as = HID_ESCAPE;            /* what Esc's press went in as; its release follows */
    if (code == HID_ESCAPE) {
        if (down) esc_as = esc_target();
        code = esc_as;
        if (!down) esc_as = HID_ESCAPE;
    }
    const char *sel = find_sel(down ? "onKeyDown:" : "onKeyUp:");
    if (!sel) {
        static int moaned;
        if (!moaned) { moaned = 1; LOG("keyboard: the game has no onKeyDown:/onKeyUp:"); }
        return -1;
    }
    shim_objc_msgSend(g_shell, sel, shim_key_press(code, mods));
    DIAG(static int said; if (said < 30) { said++;
        LOG("keyboard: key %d %s, modifiers %04x", code, down ? "down" : "up", mods); });
    return glfw_of(code);
}

/*
 * EVERY PRESS REACHES AT LEAST ONE FRAME (K5, 01.10.2026).
 *
 * Keys come in between frames, the whole queue at once, and the device only
 * keeps a state (above) - so a key tapped faster than a frame, a key tapped
 * during a long frame, or the co-op gesture's lone Tab let in as "down and up
 * now" went down and up again before the device ever looked: no pause, no
 * map, and Controls could not bind a quick key. The pad solved the same thing
 * by holding a release until the game had read the press (pad.c,
 * the_game_read); this is the same contract for the keyboard, read from the
 * device's own memory, no hook: a release of a key whose press sits in SET but
 * not yet in CURRENT waits, and so does every later event of that key, in
 * order, until the device's update has taken the press in. A press that never
 * reached SET (a key the game does not know) is not waited for.
 *
 * The logic works on a pair of arrays and a sender, so the start-up self-test
 * can run it against a device of its own (keys_selftest_batching).
 */
typedef struct {
    const unsigned char *set, *cur;          /* the device's arrays, or the self-test's */
    int (*send)(int code, int down, int mods);
} kb_env_t;

#define KB_LATER_MAX 32
#define KB_WAIT_MAX  600                    /* ten seconds of frames: past that the release goes anyway */

typedef struct {
    struct { int code, down, mods; unsigned since; } later[KB_LATER_MAX];
    int n;
    short press_glfw[256];                   /* the GLFW key each HID code's last press set, -1 none */
    unsigned batch;
    /* the counters of the report */
    unsigned presses, unknown, deferred, waited1, waited2, waited_more, forced, overflow;
} kb_state_t;

static void kb_reset(kb_state_t *s) {
    memset(s, 0, sizeof(*s));
    for (int i = 0; i < 256; i++) s->press_glfw[i] = -1;
}

static int kb_waits(const kb_state_t *s, const kb_env_t *e, int code) {
    int g = s->press_glfw[code & 0xff];
    return g >= 0 && g < KB_KEYS && e->set[g] && !e->cur[g];
}

static void kb_send(kb_state_t *s, const kb_env_t *e, int code, int down, int mods) {
    int g = e->send(code, down, mods);
    if (!down) return;
    s->press_glfw[code & 0xff] = (short) (g >= 0 && g < KB_KEYS ? g : -1);
    s->presses++;
    if (g < 0) s->unknown++;
}

static int kb_queued(const kb_state_t *s, int code) {
    for (int i = 0; i < s->n; i++) if (s->later[i].code == code) return 1;
    return 0;
}

static void kb_deliver(kb_state_t *s, const kb_env_t *e, int code, int down, int mods) {
    if (code >= 0 && code <= 255 && (kb_queued(s, code) || (!down && kb_waits(s, e, code)))) {
        if (s->n < KB_LATER_MAX) {
            s->later[s->n].code = code;
            s->later[s->n].down = down;
            s->later[s->n].mods = mods;
            s->later[s->n].since = s->batch;
            s->n++;
            if (!down) s->deferred++;
            return;
        }
        s->overflow++;                       /* nowhere to wait: in it goes, as before */
    }
    kb_send(s, e, code, down, mods);
}

/* the start of a batch: what waited goes first, in its order, if the device took the press */
static void kb_batch_begins(kb_state_t *s, const kb_env_t *e) {
    s->batch++;
    unsigned char blocked[256];
    memset(blocked, 0, sizeof(blocked));
    int kept = 0;
    for (int i = 0; i < s->n; i++) {
        int code = s->later[i].code, down = s->later[i].down, mods = s->later[i].mods;
        unsigned since = s->later[i].since;
        int hold = blocked[code & 0xff];
        if (!hold && !down && kb_waits(s, e, code)) {
            if (s->batch - since < KB_WAIT_MAX) hold = 1;
            else s->forced++;
        }
        if (hold) {
            blocked[code & 0xff] = 1;
            if (kept != i) s->later[kept] = s->later[i];
            kept++;
            continue;
        }
        if (!down) {
            unsigned waited = s->batch - since;
            if (waited <= 1) s->waited1++;
            else if (waited == 2) s->waited2++;
            else s->waited_more++;
        }
        kb_send(s, e, code, down, mods);
    }
    s->n = kept;
}

static kb_state_t g_kb;
static kb_env_t g_kb_env;
static int g_kb_ready;
static unsigned g_repeats_dropped, g_repeats_spawner;
static int g_keys_real;                 /* a key of the player's reached the delivery this launch */

/* the frame clock of the keys: one tick per frame the game draws (gl.c) */
unsigned shim_keys_batch(void) { return g_kb.batch; }

static void kb_ready(void) {
    if (g_kb_ready) return;
    unsigned long b = shim_image_base();
    if (!b) return;
    unsigned n = g_kb.batch;
    kb_reset(&g_kb);
    g_kb.batch = n;
    g_kb_env.set = (const unsigned char *) (b + KB_SET);
    g_kb_env.cur = (const unsigned char *) (b + KB_CURRENT);
    g_kb_env.send = key_to_shell;
    g_kb_ready = 1;
}

/* one key into the delivery; before the image is there, straight in */
static void kb_key(int code, int down, int mods) {
    if (g_kb_ready) kb_deliver(&g_kb, &g_kb_env, code, down, mods);
    else key_to_shell(code, down, mods);
}

/*
 * THE CO-OP GESTURE ON THE KEYBOARD: Tab and Esc held together 1.5 seconds
 * (the owner, 28.09.2026), as Select and Start on a pad (pad.c), and by the
 * same rule: held TOGETHER, neither does its own thing - no map, no pause; a
 * lone key goes in without a delay anyone could feel. The keys arrive here
 * once a frame, so a press of either is held back to the next frame: if the
 * other has come by then, both are the gesture's and stay out of the game
 * until they are let go; if not, it goes in one frame late, as it is. A key
 * that comes up before its frame is over goes in as a press and a release -
 * through the delivery, so the release waits for the device to have seen the
 * press (K5): a quick Tab is a map, a quick Esc a pause. The join itself is
 * the game's own, for the game's own keyboard device (coop.c
 * shim_coop_join_keyboard) - the keyboard is not a pad.
 */
enum { KG_TAB = 43, KG_ESC = 41, KG_HOLD_MS = 1500 };
/*
 * A LONE KEY WAITS THREE FRAMES (the 19th round, 28.09.2026: with two pads in,
 * Tab and Esc paused and opened the map instead of joining). The two keys of
 * a hand arrive some 30 ms apart, and "the end of this frame" was often the
 * very next millisecond: the first key had gone in before the second came.
 * Three frames are 50 ms, which no finger can tell on a map or a pause; the
 * pad waits two reads of the game for the same reason (pad.c PAIR_READS).
 */
#define KG_WAIT_FRAMES 3
static struct { int held, pending, swallowed, mods; } g_kg[2];     /* 0 Tab, 1 Esc; pending counts frames */
static long long g_kg_both_at;
static int g_kg_fired;

static long long kg_now_ms(void) {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (long long) t.tv_sec * 1000 + t.tv_nsec / 1000000;
}

static int kg_index(int code) { return code == KG_TAB ? 0 : (code == KG_ESC ? 1 : -1); }
static int kg_code(int i) { return i == 0 ? KG_TAB : KG_ESC; }

/* a key of the pair as it arrives: 1 when it is the gesture's to keep */
static int key_gesture(int code, int down, int mods) {
    int i = kg_index(code);
    if (i < 0) return 0;
    if (down) {
        if (g_kg[i].held) return 1;                     /* already judged */
        g_kg[i].held = 1;
        g_kg[i].mods = mods;
        if (g_kg[!i].held && (g_kg[!i].pending || g_kg[!i].swallowed)) {
            g_kg[i].swallowed = g_kg[!i].swallowed = 1;  /* both: the gesture's */
            g_kg[i].pending = g_kg[!i].pending = 0;
            g_kg_both_at = kg_now_ms();
            g_kg_fired = 0;
        } else {
            g_kg[i].pending = KG_WAIT_FRAMES;           /* the other may follow within these frames */
        }
        return 1;
    }
    g_kg[i].held = 0;
    if (g_kg[i].pending) {                              /* never let in: a press now, its release when seen */
        g_kg[i].pending = 0;
        kb_key(code, 1, g_kg[i].mods);
        kb_key(code, 0, mods);
        return 1;
    }
    if (g_kg[i].swallowed) {
        g_kg[i].swallowed = 0;
        g_kg_both_at = 0;
        return 1;
    }
    return 0;                                           /* it went in: its release follows */
}

/* the end of a frame's keys: a key still alone goes in; the pair's clock */
static void key_gesture_frame(void) {
    for (int i = 0; i < 2; i++)
        if (g_kg[i].pending && --g_kg[i].pending == 0)
            kb_key(kg_code(i), 1, g_kg[i].mods);
    if (g_kg_both_at && !g_kg_fired && kg_now_ms() - g_kg_both_at >= KG_HOLD_MS) {
        g_kg_fired = 1;
        int shim_coop_join_keyboard(void);
        shim_coop_join_keyboard();
    }
}

/*
 * THE LAST KEYS, for the difficulty screen's report (menu.c): what was
 * delivered, and on which frame. Written on the frame thread only.
 */
#if ISAAC_DIAG
static struct { short code; unsigned char down, rep; unsigned batch; } g_recent[4];
static int g_recent_n;

static void keys_remember(int code, int down, int rep) {
    int at = g_recent_n++ & 3;
    g_recent[at].code = (short) code;
    g_recent[at].down = (unsigned char) down;
    g_recent[at].rep = (unsigned char) rep;
    g_recent[at].batch = g_kb.batch;
}
#endif

void shim_keys_recent(char *out, unsigned long room) {
    if (!out || !room) return;
    out[0] = 0;
#if ISAAC_DIAG
    unsigned long n = 0;
    int have = g_recent_n < 4 ? g_recent_n : 4;
    for (int k = have; k > 0 && n < room; k--) {
        int at = (g_recent_n - k) & 3;
        n += (unsigned long) snprintf(out + n, room - n, "%s%#x %s%s@%u", n ? " " : "",
                                      g_recent[at].code, g_recent[at].down ? "down" : "up",
                                      g_recent[at].rep ? "(repeat)" : "", g_recent[at].batch);
    }
    if (!have) snprintf(out, room, "none");
#endif
}

/* ---------- the report's measurements and self-tests (report builds only) ---------- */
#if ISAAC_DIAG
#include <sys/stat.h>
int shim_eid_players(unsigned long *out, int max);
const char *shim_saves_dir(void);
int shim_touch_any_down(void);

/*
 * The scan-code table Java translates keys by (KeyMap.java, tools/keymap.py),
 * handed down once so the start-up self-test can put every entry to the
 * game's own table.
 */
static short g_scan_table[256];
static volatile int g_scan_table_n = -1;

/* K5's logic on a device of its own: the release of a quick key waits for an update */
static unsigned char g_st_set[KB_KEYS], g_st_cur[KB_KEYS];
static int g_st_log[16], g_st_logn;
static int st_send(int code, int down, int mods) {
    (void) mods;
    if (g_st_logn < 16) g_st_log[g_st_logn++] = down ? code : -code;
    if (code == 7) return -1;                           /* a key the "game" does not know */
    g_st_set[code] = (unsigned char) down;
    return code;
}
static void st_update(void) { memcpy(g_st_cur, g_st_set, sizeof(g_st_cur)); }

static const char *keys_selftest_batching(void) {
    kb_state_t s;
    kb_env_t e = { g_st_set, g_st_cur, st_send };
    memset(g_st_set, 0, sizeof(g_st_set)); memset(g_st_cur, 0, sizeof(g_st_cur));
    kb_reset(&s); g_st_logn = 0;
    /* 1: down and up in one batch - the up waits for an update */
    kb_batch_begins(&s, &e);
    kb_deliver(&s, &e, 66, 1, 0); kb_deliver(&s, &e, 66, 0, 0);
    if (g_st_logn != 1 || g_st_log[0] != 66 || s.n != 1) return "a quick key's release went in with its press";
    kb_batch_begins(&s, &e);                             /* 2: no update in between - it still waits */
    if (g_st_logn != 1) return "the release went before the device had taken the press";
    st_update();
    kb_batch_begins(&s, &e);
    if (g_st_logn != 2 || g_st_log[1] != -66 || s.n != 0) return "the release did not follow the update";
    /* 3: a key held for frames - its release goes at once */
    kb_deliver(&s, &e, 67, 1, 0); st_update(); kb_batch_begins(&s, &e);
    kb_deliver(&s, &e, 67, 0, 0);
    if (g_st_logn != 4 || g_st_log[3] != -67) return "a held key's release was held back";
    /* 4: a key the game does not know is not waited for */
    kb_deliver(&s, &e, 7, 1, 0); kb_deliver(&s, &e, 7, 0, 0);
    if (g_st_logn != 6 || g_st_log[5] != -7) return "a key the game does not know was waited for";
    /* 5: what follows a waiting release of the same key waits behind it, in order */
    kb_deliver(&s, &e, 68, 1, 0); kb_deliver(&s, &e, 68, 0, 0); kb_deliver(&s, &e, 68, 1, 0);
    if (g_st_logn != 7 || s.n != 2) return "a later press overtook a waiting release";
    st_update(); kb_batch_begins(&s, &e);
    if (g_st_logn != 9 || g_st_log[7] != -68 || g_st_log[8] != 68) return "the waiting events came out of order";
    return NULL;
}

static void keys_selftest_table(void) {
    int n = g_scan_table_n;
    if (n < 0) return;
    g_scan_table_n = -2;                                 /* once */
    int keys = 0, known = 0, told = 0;
    char unknown[200];
    unknown[0] = 0;
    for (int s = 0; s < 256; s++) {
        int hid = g_scan_table[s];
        if (hid <= 0) continue;
        keys++;
        if (glfw_of(hid) >= 0) { known++; continue; }
        if (told < 12) {
            size_t k = strlen(unknown);
            snprintf(unknown + k, sizeof(unknown) - k, " scan %d->%#x", s, hid);
            told++;
        }
    }
    char line[300];
    snprintf(line, sizeof(line), "keys selftest: scan table %d keys, the game knows %d%s%s; Enter scan 28 -> %#x -> GLFW %d",
             keys, known, unknown[0] ? ", unknown:" : "", unknown, g_scan_table[28], glfw_of(g_scan_table[28]));
    keys_say(line);
}

static const char *glfw_name(int g, char *buf, size_t room) {
    if (g >= 32 && g < 97 && g != 32) snprintf(buf, room, "'%c'", g);
    else if (g == 32) snprintf(buf, room, "Space");
    else if (g == 256) snprintf(buf, room, "Esc");
    else if (g == 257) snprintf(buf, room, "Enter");
    else if (g == 258) snprintf(buf, room, "Tab");
    else if (g >= 262 && g <= 265) snprintf(buf, room, "%s", (const char *[]) {"Right", "Left", "Down", "Up"}[g - 262]);
    else snprintf(buf, room, "%d", g);
    return buf;
}

/* the bindings as the device has them now, against the defaults; and the file behind them */
static void keys_selftest_bindings(void) {
    unsigned long b = shim_image_base(), dev = kb_device();
    if (!b || !dev) return;
    char line[300];
    const char *saves = shim_saves_dir();
    int found = 0;
    for (int k = 0; k < 2 && saves; k++) {
        char path[640];
        snprintf(path, sizeof(path), k ? "%s/inputconfigs.dat" : "%s/Repentance/inputconfigs.dat", saves);
        struct stat st;
        if (stat(path, &st) != 0) continue;
        char t[32];
        struct tm tmv;
        time_t mt = st.st_mtime;
        localtime_r(&mt, &tmv);
        strftime(t, sizeof(t), "%d.%m %H:%M", &tmv);
        snprintf(line, sizeof(line), "keyboard: bindings from %s, %lld bytes, written %s",
                 path, (long long) st.st_size, t);
        keys_say(line);
        found = 1;
    }
    if (!found) keys_say("keyboard: no inputconfigs.dat - the device runs on its defaults");
    /* the defaults: (action, key) pairs the edition's table holds (0x7f1be4 puts it at 0xa9b1e0) */
    const int *defs = *(const int *const *) (b + 0xa9b1e0);
    int ndefs = *(const int *) (b + 0xa9b1e8);
    if (!defs || ndefs <= 0 || ndefs > 64 || !shim_eid_readable(defs, (unsigned long) ndefs * 8)) defs = NULL;
    unsigned acts = *(const unsigned *) (dev + KB_ACTIONS);
    int differ = 0;
    for (unsigned a = 0; a < acts && a < 64; a++) {
        int live[16];
        int n = kb_bound(dev, (int) a, live, 16);
        if (n < 0) break;
        int want[16], nw = 0;
        for (int i = 0; defs && i < ndefs && nw < 16; i++) if (defs[i * 2] == (int) a) want[nw++] = defs[i * 2 + 1];
        int same = 1;
        for (int i = 0; i < nw && same; i++) {
            int has = 0;
            for (int j = 0; j < n; j++) if (live[j] == want[i]) has = 1;
            same = has;
        }
        for (int j = 0; j < n && same; j++) {
            if (live[j] < 0) continue;
            int has = 0;
            for (int i = 0; i < nw; i++) if (want[i] == live[j]) has = 1;
            same = has;
        }
        if (same) continue;
        differ++;
        if (differ > 12) continue;
        int k = snprintf(line, sizeof(line), "keyboard: action %u bound to", a);
        char nb[16];
        for (int j = 0; j < n && k < (int) sizeof(line) - 20; j++)
            if (live[j] >= 0) k += snprintf(line + k, sizeof(line) - (size_t) k, " %s", glfw_name(live[j], nb, sizeof(nb)));
        k += snprintf(line + k, sizeof(line) - (size_t) k, ", the default");
        for (int i = 0; i < nw && k < (int) sizeof(line) - 20; i++)
            k += snprintf(line + k, sizeof(line) - (size_t) k, " %s", glfw_name(want[i], nb, sizeof(nb)));
        keys_say(line);
    }
    snprintf(line, sizeof(line), "keyboard: %u actions, %d differ from the defaults%s", acts, differ,
             defs ? "" : " (the defaults table was not readable)");
    keys_say(line);
    /* a table in the PC's numbering is turned round before any key reaches the game (kb_table_to_ios) */
    if (g_kb_turned) {
        snprintf(line, sizeof(line), "keyboard: the bindings came in the PC's numbering - turned into the iOS one"
                 " %d time(s) (actions 16..26 -> 23..33)", g_kb_turned);
        keys_say(line);
    }
    /* Esc in a run: which key it goes in as */
    int keys[16], n = kb_bound(dev, ACTION_PAUSE, keys, 16), first = -1, esc = 0;
    for (int i = 0; i < n; i++) { if (keys[i] == GLFW_ESCAPE) esc = 1; if (keys[i] >= 0 && first < 0) first = keys[i]; }
    char nb[16];
    if (esc) snprintf(line, sizeof(line), "esc -> pause: Esc is bound to pause itself, nothing to change");
    else if (first < 0) snprintf(line, sizeof(line), "esc -> pause has no key: Esc stays Esc in a run");
    else snprintf(line, sizeof(line), "esc -> pause key GLFW %d (%s), HID %#x", first,
                  glfw_name(first, nb, sizeof(nb)), hid_of_glfw(first));
    keys_say(line);
}

/* the co-op join slots the moment a Game appears, before anyone could have joined (K7) */
static void keys_join_slots(unsigned long b) {
    static unsigned long seen;
    static int told;
    unsigned long game = *(const unsigned long *) (b + KB_GAME);
    if (!game || game == seen || told >= 6) return;
    seen = game;
    told++;
    const unsigned char *slot = (const unsigned char *) (game + 0x25c40 + 0x5c);
    if (!shim_eid_readable(slot, 4 * 0x14)) return;
    char line[300];
    int k = snprintf(line, sizeof(line), "join slots at a new Game %lx:", game);
    for (int i = 0; i < 4; i++) {
        const unsigned char *s = slot + i * 0x14;
        float f;
        memcpy(&f, s + 0xc, sizeof(f));
        k += snprintf(line + k, sizeof(line) - (size_t) k, " [dev %#x char %d idx %d %.2f %u]",
                      *(const unsigned *) s, *(const int *) (s + 4), *(const int *) (s + 8), (double) f, s[0x10]);
    }
    keys_say(line);
}

/* the live counters, told as they grow */
static void keys_counters(void) {
    static unsigned told_at;
    unsigned p = g_kb.presses;
    if (!p || p < told_at * 2 + 20) return;
    told_at = p;
    char line[300];
    snprintf(line, sizeof(line),
             "keys: delivered %u presses (%u the game does not know), deferred ups %u (after 1 frame %u, 2 frames %u,"
             " more %u, forced %u, no room %u); repeats: %u dropped, %u to the spawner",
             p, g_kb.unknown, g_kb.deferred, g_kb.waited1, g_kb.waited2, g_kb.waited_more, g_kb.forced,
             g_kb.overflow, g_repeats_dropped, g_repeats_spawner);
    keys_say(line);
}

/*
 * DOES THE GAME GIVE PLAYER 0 BACK TO THE KEYBOARD BY ITSELF (K7)? Once a
 * launch, in a run, when player 0 sits on the touch screen, no finger is down
 * and the player has already used a key this launch: a key NOTHING is bound
 * to in the live table goes into the device's set for three frames, and the
 * seat of player 0 is watched for a second. The key does nothing in the game,
 * our own writer (coop.c key_wants_player0) never hears of it, and player 0 is
 * not put back afterwards: what the finger does next is written down, not
 * undone.
 */
#define TOUCH_DEVICE_ID 0x10dead
static struct { int state, glfw; unsigned at, back_at; int finger_took, finger_at_start; } g_rt;

static unsigned rt_player0_device(void) {
    unsigned long p[8];
    int n = shim_eid_players(p, 8);
    if (n < 1 || !p[0] || !shim_eid_readable((const void *) (p[0] + 0x19ec), 4)) return 0xffffffffu;
    return *(const unsigned *) (p[0] + 0x19ec);
}

static int rt_is_keyboard(unsigned id) {
    unsigned long b = shim_image_base();
    if (!b || id == 0xffffffffu || id == TOUCH_DEVICE_ID) return 0;
    unsigned long dev = ((unsigned long (*)(unsigned long, int)) (b + 0x8b5d18))(b + KB_INPUT, (int) id);
    return dev && shim_eid_readable((const void *) dev, 8) && *(const unsigned long *) dev == b + KB_VTABLE;
}

static void keys_return_selftest(void) {
    unsigned long b = shim_image_base(), dev = kb_device();
    if (!b || !dev || g_rt.state == 3) return;
    unsigned char *set = (unsigned char *) (b + KB_SET);
    const unsigned char *cur = (const unsigned char *) (b + KB_CURRENT);
    unsigned now = g_kb.batch;
    if (g_rt.state == 0) {
        if (!g_keys_real || !shim_sticks_in_game() || g_kb.n) return;
        unsigned long game = *(const unsigned long *) (b + KB_GAME);
        if (!game || *(const int *) (game + KB_PAUSE_MENU) != 0) return;
        if (rt_player0_device() != TOUCH_DEVICE_ID || shim_touch_any_down()) return;
        if (!*(const unsigned char *) (dev + KB_ENABLED)) return;
        static const int CANDIDATES[] = { 298, 299, 300, 301, 281, 284, 302, 303, 304, 305 };
        unsigned acts = *(const unsigned *) (dev + KB_ACTIONS);
        for (unsigned c = 0; c < sizeof(CANDIDATES) / sizeof(CANDIDATES[0]) && g_rt.glfw <= 0; c++) {
            int g = CANDIDATES[c], bound = 0;
            for (unsigned a = 0; a < acts && a < 64 && !bound; a++) {
                int keys[16], n = kb_bound(dev, (int) a, keys, 16);
                for (int i = 0; i < n; i++) if (keys[i] == g) bound = 1;
            }
            if (!bound && !set[g] && !cur[g]) g_rt.glfw = g;
        }
        if (g_rt.glfw <= 0) { g_rt.state = 3; keys_say("seat selftest: every candidate key is bound, not run"); return; }
        set[g_rt.glfw] = 1;
        g_rt.at = now;
        g_rt.state = 1;
        return;
    }
    unsigned seat = rt_player0_device();
    if (g_rt.state == 1 && now - g_rt.at >= 3) {
        if (set[g_rt.glfw]) set[g_rt.glfw] = 0;
        g_rt.state = 2;
    }
    if (!g_rt.back_at && rt_is_keyboard(seat)) g_rt.back_at = now;
    if (g_rt.back_at && seat == TOUCH_DEVICE_ID) g_rt.finger_took = 1;
    if (now - g_rt.at < 60) return;
    char line[240];
    if (g_rt.back_at)
        snprintf(line, sizeof(line), "seat selftest: the game returned player 0 to the keyboard in %u frames"
                 " (key GLFW %d, nothing bound); a finger took it back after: %s",
                 g_rt.back_at - g_rt.at, g_rt.glfw, g_rt.finger_took ? "yes" : "no");
    else {
        /* the device's own "is any key down" (+0xd8, 0x8ddbbc): what the dispatcher asks */
        const unsigned long *vt = *(const unsigned long *const *) dev;
        int held = ((int (*)(unsigned long)) vt[0xd8 / 8])(dev);
        snprintf(line, sizeof(line), "seat selftest: NOT returned in 60 frames (key GLFW %d): player 0 on %#x,"
                 " keyboard on %d, any key held %d, a finger down now %d",
                 g_rt.glfw, seat, *(const unsigned char *) (dev + KB_ENABLED), held, shim_touch_any_down());
    }
    keys_say(line);
    g_rt.state = 3;
}

static int g_bindings_told;

static void keys_report_frame(void) {
    unsigned long b = shim_image_base();
    if (!b || !shim_report_active()) return;
    /* the bindings once they are loaded: five seconds into the frames, or at the first key */
    if (!g_bindings_told && g_kb.batch >= 300 && kb_device()) { g_bindings_told = 1; keys_selftest_bindings(); }
    static int once;
    if (!once) {
        once = 1;
        const char *fail = keys_selftest_batching();
        char line[160];
        if (fail) snprintf(line, sizeof(line), "keys selftest: batching FAILED - %s", fail);
        else snprintf(line, sizeof(line), "keys selftest: batching ok (a quick key's release waits for the device, in order)");
        keys_say(line);
    }
    keys_selftest_table();
    keys_join_slots(b);
    keys_counters();
    keys_return_selftest();
}
#endif

/*
 * THE DOUBLE TAP FROM KEYS (19th round: the class closed for every device, not
 * for the pad alone). The game's double-tap detectors (Mars, Hemoptysis and the
 * like) sit behind the port's touch gate (loader.c double_tap_gate), so a
 * player on the keyboard never dashed. The pad got the game's own window back
 * (pad.c shim_double_tap_open); the keyboard gets the same, judged the same
 * way. Once the queue is in, SET holds exactly what the device's update
 * (0x8ddc40, the next input update, before any player's) takes for CURRENT, and
 * a release never overtakes its press (kb_deliver) - so a key down in SET and
 * up in CURRENT is a press the detector will see at that update, and the judge
 * opens the detector of every keyboard player whose last press was that way,
 * within the game's ten. The keys are the device's live bindings (Controls).
 */
int shim_double_tap_keys(int dev_id, unsigned long dev, const unsigned char *set, const unsigned char *cur, int keys);
int shim_device_keyboard(void);

static void keys_double_tap(void) {
    if (!g_kb_ready) return;
    unsigned long dev = kb_device();
    if (!dev || !*(const unsigned char *) (dev + KB_ENABLED)) return;
    int id = shim_device_keyboard();
    if (id != -1) shim_double_tap_keys(id, dev, g_kb_env.set, g_kb_env.cur, KB_KEYS);
}

void shim_keys_deliver(void) {
    if (!g_shell) return;
    /*
     * MEASUREMENT (item 30, the keyboard): onKeyDown: (0x51cc) turns the HID
     * code into the game's own and calls the function the shell keeps at
     * +0x98. Nothing in the image stores it there by a plain offset, so where
     * it leads is read off the live shell once.
     */
    DIAG({ static int said; if (!said) { said = 1;
        unsigned long base = shim_image_base(), cb = *(const unsigned long *) ((char *) g_shell + 0x98);
        int shim_report_active(void); void report_line(const char *, const char *);
        char line[96];
        snprintf(line, sizeof(line), "shell key callback %lx (image +%lx)", cb, cb >= base ? cb - base : 0);
        LOG("keyboard: %s", line);
        if (shim_report_active()) report_line("keyboard", line); } });
    kb_ready();
    if (g_kb_ready) kb_batch_begins(&g_kb, &g_kb_env);
    else g_kb.batch++;
    DIAG(keys_report_frame());
    int code, down, mods, ch, rep, table_seen = 0;
    while (shim_key_next_ex(&code, &down, &mods, &ch, &rep)) {
        /* a frame with keys: a PC-numbered table is turned round before any of them goes on (1.74) */
        if (!table_seen) { table_seen = 1; kb_table_to_ios(); }
        DIAG(keys_remember(code, down, rep));
        /*
         * A release lets the restart key go before anything below may keep it
         * (reset.c): held for a restart, it must not stay held because the
         * spawner's card or a page opened over it.
         */
        { void shim_reset_key(int, int); if (!down) shim_reset_key(code, 0); }
        /* the spawner's card, when it is up, takes the keyboard whole - its search the repeats too */
        { int shim_spawner_key(int, int, int, int, int);
          if (shim_spawner_key(code, down, mods, ch, rep)) { if (rep) g_repeats_spawner++; continue; } }
        /*
         * ONE PRESS, ONE EVENT (K4), as iOS gives it: pressesBegan: comes once
         * for a key however long it is held. Android repeats a held key every
         * ~50 ms, and every repeat was a fresh press to the straps, the
         * spawner's list and the Esc rule. Only the spawner's search takes
         * them (above), as a text field does.
         */
        if (rep) { g_repeats_dropped++; continue; }
        /* the port's straps: a menu's top item, one step above its first (straps.c) */
        { int shim_straps_key(int, int); if (shim_straps_key(code, down)) continue; }
        if (key_gesture(code, down, mods)) continue;
        if (down) {
            g_keys_real = 1;
            void shim_coop_key_edge(void); shim_coop_key_edge();   /* player 0 back from the screen (coop.c) */
            DIAG(if (!g_bindings_told) { g_bindings_told = 1; keys_selftest_bindings(); });
            /* a press the game gets: is it the restart key, held in a run (reset.c) */
            void shim_reset_key(int, int); shim_reset_key(code, 1);
        }
        kb_key(code, down, mods);
    }
    key_gesture_frame();
    keys_double_tap();              /* after every key of this frame is in SET */
}

/* ---------- the keyboard's JNI (MainActivity) ---------- */
#include <jni.h>

/*
 * A key from a hardware keyboard: the HID usage of its PLACE (MainActivity
 * reads the scan code, K2), the modifiers as UIKey has them, the character the
 * layout makes of it (for the spawner's search alone - the game reads places,
 * as iOS does) and whether it is the system's auto-repeat.
 */
JNIEXPORT void JNICALL
Java_ru_isaac_port_MainActivity_keyEvent(JNIEnv *env, jobject thiz, jint code, jboolean down, jint mods,
                                         jint ch, jboolean repeat) {
    (void) env; (void) thiz;
    void shim_key_push_ex(int, int, int, int, int);
    shim_key_push_ex((int) code, down ? 1 : 0, (int) mods, (int) ch, repeat ? 1 : 0);
}

/* is the spawner's card up: then the hardware keyboard is taken before the IME (K1) */
JNIEXPORT jboolean JNICALL
Java_ru_isaac_port_MainActivity_keysForSpawner(JNIEnv *env, jobject thiz) {
    (void) env; (void) thiz;
    int shim_spawner_is_open(void);
    return shim_spawner_is_open() ? JNI_TRUE : JNI_FALSE;
}

/* report builds: Java's scan-code table, for the start-up self-test against 0x945ad4 */
JNIEXPORT void JNICALL
Java_ru_isaac_port_MainActivity_keysScanTable(JNIEnv *env, jobject thiz, jshortArray table) {
    (void) thiz;
#if ISAAC_DIAG
    if (!table || g_scan_table_n != -1) return;
    jsize n = (*env)->GetArrayLength(env, table);
    if (n > 256) n = 256;
    (*env)->GetShortArrayRegion(env, table, 0, n, g_scan_table);
    g_scan_table_n = (int) n;
#else
    (void) env; (void) table;
#endif
}

void shim_ime_bind(void *view) {
    if (!view || !g_shell) return;
    const char *s = find_sel("setIsaacShellInstance:");
    if (!s) { LOG("input: no setIsaacShellInstance: selector"); return; }
    shim_objc_msgSend(view, s, g_shell);
    LOG("input: object bound to shell %p", g_shell);
}

static void deliver_input(void) {
    while (g_input_head != g_input_tail) {
        char t[8];
        snprintf(t, sizeof(t), "%s", g_input[g_input_head]);
        g_input_head = (g_input_head + 1) % INPUT_QUEUE;
        /* while the item spawner's card is up, what is typed is its search */
        { int shim_spawner_text(const char *); if (shim_spawner_text(t)) continue; }
        /* the recipient is whoever the game asked focus for: that is its view */
        void *target = shim_ime_target();
        if (!target) target = g_input_view;          /* fallback */
        {
            int shim_report_active(void);
            void report_line(const char *kind, const char *text);
            if (shim_report_active()) {
                static int said;
                if (said < 10) {
                    said++;
                    char line[110];
                    snprintf(line, sizeof(line), "delivering \"%s\" to %p", t, target);
                    report_line("ime", line);
                }
            }
        }
        if (!target) continue;                       /* focus released — the letter has nowhere to go */
        if (!strcmp(t, "")) {
            const char *s = find_sel("deleteBackward");
            int shim_report_active(void);
            void report_line(const char *kind, const char *text);
            if (shim_report_active()) {
                static int said;
                if (said < 8) {
                    said++;
                    char line[120];
                    snprintf(line, sizeof(line), "erase: target %p, selector %s",
                             target, s ? s : "MISSING");
                    report_line("ime", line);
                }
            }
            if (s) shim_objc_msgSend(target, s);
        } else {
            const char *s = find_sel("insertText:");
            if (s) shim_objc_msgSend(target, s, shim_str_make(t));
        }
    }
}

/*
 * Readiness of the online service.
 *
 * The daily run would not start, and the investigation led to the cause:
 * before filing a run request the game checks two fields of the object at
 * image+0xac2490 — the byte at +1 and the float at +4, which must be at least
 * one. The same trick gates two more places, all three about capabilities tied
 * to the service.
 *
 * For us both fields are always zero: there is no service and there will not
 * be one. The check is not "not passed yet" but unsatisfiable, so we set the
 * fields ourselves. This is not bypassing protection: a single-player run with
 * a given seed needs no network at all.
 *
 * NOT CALLED on its own. This must not be set every frame: at startup the game
 * files its own run request (from 0x886084), and with the gate open it starts
 * loading a run right after the intro movie — the menu becomes unreachable.
 * The check may only be opened for the duration of a press in the daily-run
 * menu, never permanently.
 */
unsigned long shim_image_base(void);

void online_service_ready(void) {
    unsigned long base = shim_image_base();
    if (!base) return;
    unsigned char *o = *(unsigned char **) (base + 0xac2490);
    if (!o) return;
    if (o[1] == 0) o[1] = 1;
    if (*(float *) (o + 4) < 1.0f) *(float *) (o + 4) = 1.0f;
}

/*
 * MINIMISING MUST NOT TOUCH THE RUN.
 *
 * Going into the background makes the game save, and the save walks every
 * player through Entity_Player::TriggerNewRoom (0x52652c), which calls
 * TemporaryEffects::RemoveAll (0x6fdd80) on the player's own effects at
 * +0x18d8. What survives that is only what carries the persistent byte at
 * +0xb8 - an effect an item in hand gives back at once. A Holy Mantle taken
 * from a card or a pill simply goes, and not only from the picture: the shield
 * is really gone.
 *
 * This was looked at once before (07.09.2026) and left alone as the game's own
 * doing - the same price a save-and-continue costs on a PC. The owner has since
 * settled it the other way, twice over and in as many words: minimising is not
 * to affect the run at all. So it does not, and this is a DELIBERATE DIVERGENCE
 * from the original.
 *
 * HOW IT IS HELD (23.09.2026, the owner's item 5). The first hold raised the
 * byte at TemporaryEffects+0x21 on every player, which RemoveAll answers to -
 * and so does every other method of the effects object (0x6fd76c ... 0x6fe124):
 * with it raised, "has effect" says no to everything. The save goes on through
 * TriggerNewRoom regardless, and that walks the active slots asking
 * 0x505e60(player, id) whether an item's costume still has its effect
 * (HasCollectibleEffect 0x6fdc90): blind, it said no, and Gamekid's Pac-Man head
 * came off while the effect itself went on - the drop the owner saw. The
 * Forgotten's soul (+0x2588) was never held at all.
 *
 * The game has a switch for exactly this kind of save, one that must leave the
 * run as it is: the byte game+0x24fa0a. Its own rewind snapshot raises it
 * around its save (0x8959bc: set at 0x895b08, save at 0x895b20, cleared at
 * 0x895b8c), and the store of the players (0x569afc) skips TriggerNewRoom for
 * every player and the sub-player when it is up (0x569b80 cbnz). The store
 * still writes every effect into the file. So the minimise raises that byte
 * for the whole trip and puts it back when we are back - nothing in the run is
 * cleaned, and nothing is blinded.
 *
 * FOR THE WHOLE TRIP, not for the delivery: frames go on running in the
 * background and the save comes on one of them (13.09.2026). And only ONCE:
 * a second "minimised" before the return must not take the raised byte for the
 * value to put back, or it would stay up and silence the rewind and every real
 * room change.
 */
#define GAME_PTR        0xac3b90
#define GAME_QUIET_SAVE 0x24fa0a    /* byte: a save that leaves the run alone (0x569b80) */
#define QUIET_READER    0x569b54    /* mov w25, #0xfa0a - the store's own reading of it */
#define QUIET_WORD      0x529f4159u

static int g_away;                  /* between the minimise and the return */
static unsigned char *g_quiet;      /* the byte raised, while it is */
static unsigned char g_quiet_was;

/* raise the game's own quiet-save byte; answers 1 when it is up */
static int effects_hold(void) {
    unsigned long b = shim_image_base();
    if (!b || g_quiet) return g_quiet != 0;
    if (*(const uint32_t *) (b + QUIET_READER) != QUIET_WORD) {
        LOG("app: 0x569b54 is not the store's reading of the quiet byte - nothing held");
        return 0;
    }
    unsigned long game = *(const unsigned long *) (b + GAME_PTR);
    if (!game) return 0;
    g_quiet = (unsigned char *) (game + GAME_QUIET_SAVE);
    g_quiet_was = *g_quiet;
    *g_quiet = 1;
    return 1;
}

static void effects_release(void) {
    if (!g_quiet) return;
    unsigned long b = shim_image_base();
    unsigned long game = b ? *(const unsigned long *) (b + 0xac3b90) : 0;   /* Game */
    /* the game object can be another one by now; the old one is left alone then */
    if (game && g_quiet == (unsigned char *) (game + GAME_QUIET_SAVE)) *g_quiet = g_quiet_was;
    g_quiet = 0;
}

/*
 * WHO WIPES THE EFFECTS, AND FROM WHERE. Report builds only.
 *
 * "It is the game's own behaviour" was a conclusion, not a measurement, and the
 * owner said so (13.09.2026). So RemoveAll says out loud every time it is
 * called: whose effects, with which flag, and the return address of whoever
 * called it, as an offset in the image. A minimise that wipes them will name
 * its own road.
 */
void shim_app_effects_wiped(unsigned long self, unsigned long flags, unsigned long from) {
    int shim_report_active(void);
    void report_line(const char *, const char *);
    static int said;
    if (said >= 24 || !shim_report_active()) return;
    said++;
    unsigned long b = shim_image_base();
    /*
     * HOW MANY effects it was about to take, and WHETHER WE WERE AWAY. The
     * first asking showed a wipe right after the minimise, with the hold
     * already let go, and that does not square with the effects surviving -
     * so the question is asked properly: the list is counted before the call
     * (RemoveAll reads its own vector at this+8 and this+0x10, sixteen bytes to
     * an entry), and "away" spans the whole trip, not just the delivery.
     */
    long n = -1;
    {
        const unsigned long *v = (const unsigned long *) (self + 8);
        if (v[0] && v[1] >= v[0] && v[1] - v[0] < 0x10000) n = (long) ((v[1] - v[0]) / 16);
    }
    char line[200];
    snprintf(line, sizeof(line), "effects wiped: this %lx, flags %lu, %ld in the list,"
             " called from +0x%lx%s%s",
             self, flags, n, (b && from > b) ? from - b : from,
             g_away ? "  (WE ARE AWAY)" : "", g_quiet ? "  (QUIET SAVE)" : "");
    report_line("app", line);
}

static void deliver_lifecycle(void) {
    int e = g_lifecycle_event;
    { void shim_lifediff_frame(int); shim_lifediff_frame(g_away && !e); }
    if (!e || !g_vc) return;
    /*
     * We only send what the view controller has: it forwards to the shell
     * itself (OnWillResignActive -> IsaacShell OnResignActive and so on).
     * Sending the shell's names directly as well would deliver the event
     * twice.
     */
    static const char *minimised[] = {"OnWillResignActive", "OnDidEnterBackground", NULL};
    static const char *restored[] = {"OnWillEnterForeground", "OnDidBecomeActive", NULL};
    const char **list = (e == 1) ? minimised : restored;
    /* what the trip changes in the run, whatever the item (lifediff.c, report builds) */
    void shim_lifediff_mark(int stage);
    shim_lifediff_mark(e == 1 ? 0 : 2);
    /* on the way out only: the run keeps its effects over the save (see above) */
    g_away = (e == 1);
    /* raised on the way out, put back only when we are back: see the note above */
    int held = (e == 1) ? effects_hold() : 0;
#if ISAAC_DIAG
    /*
     * DOES THE MINIMISE WRITE THE PROGRESS (plan item A6, report builds). The
     * game's own way out saves the run and, at its end, the persistent data
     * (0x8864d0 / 0x886538 -> 0x670b28 -> 0x670b78, which ends in `b 0x700cf8`,
     * SaveIfDirty). That save returns early when the manager is not in a run
     * ([Manager+0x204c0] != 2) or in a daily ([Game+0x1cf4c0] != 0), and
     * SaveIfDirty itself writes nothing while the data are not loaded (+0xf50).
     * So the dirty byte (data+0) before and after the handlers, with those
     * three: "dirty after" names the branch that left the progress unwritten.
     */
    unsigned char *pgd = NULL;
    int dirty_before = -1;
    {
        unsigned long b = shim_image_base();
        unsigned long manager = b ? *(const unsigned long *) (b + 0xac2428) : 0;
        if (manager) { pgd = (unsigned char *) (manager + 0x204d0); dirty_before = pgd[0]; }
    }
#endif
    for (int i = 0; list[i]; i++) {
        const char *s = find_sel(list[i]);
        if (s) shim_objc_msgSend(g_vc, s);
    }
#if ISAAC_DIAG
    {
        int shim_report_active(void);
        void report_line(const char *, const char *);
        static int said;
        if (pgd && said < 16 && shim_report_active()) {
            said++;
            unsigned long b = shim_image_base();
            unsigned long manager = (unsigned long) pgd - 0x204d0;
            unsigned long game = b ? *(const unsigned long *) (b + GAME_PTR) : 0;
            char line[200];
            snprintf(line, sizeof(line), "%s: persistent dirty before the game's handlers %d, after %d "
                     "(loaded %d, manager state %d, daily %d)",
                     e == 1 ? "minimised" : "restored", dirty_before, pgd[0], pgd[0xf50],
                     *(const int *) (manager + 0x204c0), game ? *(const int *) (game + 0x1cf4c0) : -1);
            report_line("app", line);
        }
    }
#endif
    if (e != 1) effects_release();
    shim_lifediff_mark(e == 1 ? 1 : 3);
    if (e == 1) {
        if (held) {
            int shim_report_active(void);
            void report_line(const char *, const char *);
            if (shim_report_active()) {
                char line[100];
                snprintf(line, sizeof(line), "minimised: the save is a quiet one (game+0x24fa0a)");
                report_line("app", line);
            }
            LOG("app: minimised — the save leaves the run alone (game+0x24fa0a)");
        }
    }
    /*
     * Clear only what we have just delivered.
     *
     * A plain zero here swallowed a newer event: minimising can take longer
     * than the 0.4 s shim_app_pause waits for, and by the time the delivery
     * finished onResume had already put a 2 in - "we are back". Zeroing it
     * left the game believing it was still in the background until the next
     * trip there and out again.
     */
    __sync_bool_compare_and_swap(&g_lifecycle_event, e, 0);
}

static const char *find_sel(const char *want) {
    for (const char *p = g_selbase; p && p < g_selend; p += strlen(p) + 1)
        if (!strcmp(p, want)) return p;
    return NULL;
}

int shim_UIApplicationMain(int argc, char **argv, void *principal, void *delegate) {
    (void) argc; (void) argv; (void) principal;
    /* the delegate's name arrives as a real string — that is how we find the class */
    const char *want = shim_str_text(delegate);
    LOG("UIApplicationMain: delegate \"%s\"", want);
    objc_class_t *dcls = (objc_class_t *) shim_objc_class_named(want);
    if (!dcls) { LOG("delegate class not found"); return 0; }
    delegate = dcls;

    void *app = shim_objc_alloc((objc_class_t *) delegate);
    if (!app) return 0;

    /*
     * From here on it is like iOS: the delegate is told the application has
     * launched. That is where all of its work starts — window, context, first
     * frame.
     */
    const char *sel = find_sel("application:didFinishLaunchingWithOptions:");
    if (!sel) { LOG("launch selector not found in the image"); return 0; }
    LOG("sending the delegate its launch message");
    shim_objc_msgSend(app, sel, NULL, NULL);
    LOG("delegate done: messages %ld, without a method %ld", g_msg_count, g_msg_missed);

    /*
     * On iOS the storyboard would create the view controller next. We do it
     * ourselves: find the class by name, create it and tell it the view has
     * loaded. From there come the context request and the first frame.
     */
    show_ancestry("AAPLOpenGLView");
    show_ancestry("AAPLOpenGLViewController");
    objc_class_t *vc_cls = (objc_class_t *) shim_objc_class_named("AAPLOpenGLViewController");
    if (!vc_cls) { LOG("view controller not found in the image"); return 0; }
    void *vc = shim_objc_alloc(vc_cls);
    g_vc = vc;                          /* lifecycle notifications go to it */
    LOG("view controller created: %p", vc);

    const char *s_load = find_sel("viewDidLoad");
    if (s_load) { LOG("sending viewDidLoad"); shim_objc_msgSend(vc, s_load); }

    /*
     * We do NOT send prepareView from here: viewDidLoad calls it itself.
     *
     * A second call created a second set of buffers — new framebuffer and
     * colour buffer names — and the controller switched to them. By then the
     * game's shell had already been given the name of the FIRST framebuffer
     * and was drawing into it. That is where the black screen with a live draw
     * counter came from: the game was writing into a buffer nobody presented
     * any more.
     *
     * Next in the iOS lifecycle comes viewWillAppear: — the system sends it
     * before the view is shown. We used to skip it, and it is in the
     * controller's method list, so the game counts on it.
     */
    const char *s_appear = find_sel("viewWillAppear:");
    if (s_appear) { LOG("sending viewWillAppear:"); shim_objc_msgSend(vc, s_appear, (long) 0); }

    const char *s_layout = find_sel("viewDidLayoutSubviews");
    if (s_layout) { LOG("sending viewDidLayoutSubviews"); shim_objc_msgSend(vc, s_layout); }

    LOG("controller done: messages %ld, without a method %ld", g_msg_count, g_msg_missed);

    /*
     * The frame clock. On iOS CADisplayLink runs it, poking draw: at the
     * screen's rate. Our thread is our own, so we drive the frames ourselves —
     * which is more honest anyway: the loop belongs to the port, not to a
     * foreign runtime.
     *
     * On iOS, moving to the foreground is announced by UIApplication
     * notifications. We have none, so we say it outright: without this the
     * game's shell considers itself minimised and draws no frames even though
     * the clock is running.
     */
    const char *s_active = find_sel("OnDidBecomeActive");
    if (s_active) { LOG("sending OnDidBecomeActive"); shim_objc_msgSend(vc, s_active); }

    /*
     * The shell has SwitchToGame: — by its name it looks like the call that
     * starts the game proper, while draw: spins an empty frame until then.
     * Let us check. The shell comes from the controller: it lives in its
     * _isaacShell field.
     */
    void *shell = g_shell;
    LOG("game shell: %p", shell);

    if (shell) {
        const char *s_switch = find_sel("SwitchToGame:");
        /*
         * The argument is the edition level, not a one.
         *
         * The game works it out itself: it asks the store about five purchases
         * and sets 3 (Repentance), 2 (Afterbirth+), 1 (Afterbirth) or 0. But
         * that count is kicked off by the cloud-sync branch, which we never
         * reach — and all that time we were sending a one, so the game
         * honestly ran as Afterbirth, with its own set of items, floors and
         * even its own control class.
         *
         * So we make the game's own decision ourselves: its function, its
         * buffer. Our store already answers "purchased", so the result is 3.
         */
        void (*decide_edition)(void *) = (void (*)(void *)) (g_img_lo + 0x4c78);
        void *state = (void *) (g_img_lo + 0xabec30);
        decide_edition(state);
        int level = *(int *) (g_img_lo + 0xabec34);
        if (level < 0 || level > 3) level = 0;
        LOG("edition: level %d — sending SwitchToGame:%d", level, level);
        if (s_switch) shim_objc_msgSend(shell, s_switch, (long) level);
        /*
         * Drawing inside draw: does not happen directly but through the
         * shell's fields: two readiness markers and four pointers to the
         * frame's work. We look at their values — they say whether the game
         * proper has been reached or the shell is still empty.
         */
        {
            unsigned char *b = (unsigned char *) shell;
            unsigned long *w = (unsigned long *) shell;
            LOG("shell: markers %02x %02x %02x, mode %d",
                b[0x40], b[0x41], b[0x42], *(int *) (b + 0xc8));
            /*
             * The shell hands these two pairs of numbers over together with
             * touches: the engine uses them to decide whether a touch landed
             * in the frame. So touch coordinates have to be given in the same
             * units.
             */
            double *d = (double *) (b + 0x20);
            LOG("   touch fields: +20 = %.1f %.1f, +30 = %.1f %.1f", d[0], d[1], d[2], d[3]);
            static const int off[] = {0x50, 0x58, 0x60, 0x68, 0x70, 0x78, 0xb0, 0xb8, 0xc0};
            for (unsigned i = 0; i < sizeof(off) / sizeof(off[0]); i++) {
                unsigned long v = w[off[i] / 8];
                if (v > (unsigned long) g_img_lo && v < (unsigned long) g_img_lo + 0xb00000)
                    LOG("   field +%02x -> %06lx in the image", off[i], v - (unsigned long) g_img_lo);
                else
                    LOG("   field +%02x = %016lx", off[i], v);
            }
        }
    }

    const char *s_draw = find_sel("draw:");
    if (!s_draw) { LOG("no drawing method in the image"); return 0; }
    void shim_trace_report(void);
    shim_trace_report();
    LOG("frames are running");
    /*
     * The frames are no longer counted out in advance: the game reaches its
     * own menu now, and cutting it off at six hundred frames only gets in the
     * way — one simply has to look at the screen.
     */
    void shim_gl_frame_begin(void);
    void shim_gl_frame_end(void);
    void shim_touch_deliver(void *);
    /*
     * The frame clock: THE GAME'S OWN CLOCK (link_wait_due) - every frame is
     * due one period after the last one's due time on CLOCK_MONOTONIC, the
     * grid anchored once on a vsync, and the vsync only places the picture.
     * The timer below stays only as the road for a panel that sends no vsync,
     * which is a screen that is off; the grid is anchored again when it
     * returns.
     */
    int shim_want_fps(void);
    void shim_probe_tick(void);
    void shim_audio_tick(void);
    int shim_report_active(void);
    void report_line(const char *, const char *);
    extern long long g_gl_swap_ns;   /* how long the last present's swap took */
    extern long long g_gl_present_ns;   /* this frame's present: blit and swap, added up (gl.c) */
    link_start();
    struct timespec t0;
    clock_gettime(CLOCK_MONOTONIC, &t0);
    long long next = (long long) t0.tv_sec * 1000000000LL + t0.tv_nsec;
    long long due = 0;               /* on the vsync grid: when the next frame is owed */
    int stalls_told = 0;                 /* counted for the summary, no longer a cap */
    long was_major = 0, was_minor = 0, was_objects = 0, was_draws = 0;
    long was_pack_fopen = 0, was_pack_fd = 0, was_pack_data = 0, was_pack_copied = 0;
    long was_pack_again = 0, was_pack_checks = 0, was_tex = 0, was_texpx = 0, was_texdel = 0;
    long was_big = 0, was_bigpx = 0, was_surf = 0, was_surfpx = 0;
    long long was_texns = 0;
    long was_fread = 0, was_freadb = 0;
    if (PROFILE_FRAMES && shim_report_active()) { prof_start(); sys_watch_start(); }
    for (long frame = 0;; frame++) {
        struct timespec tf;
        clock_gettime(CLOCK_MONOTONIC, &tf);
        long long frame_began = (long long) tf.tv_sec * 1000000000LL + tf.tv_nsec;
        unsigned prof_at = g_prof_n;
        deliver_lifecycle();            /* minimise and restore — on the game thread */
        deliver_input();                /* and the typed text from there as well */
        shim_gl_frame_begin();
        shim_touch_deliver(vc);              /* fingers are handled where the frames are */
        /* the report's headroom, taken going into the frame (audio.c) */
        int head_ms = -1, head_q = 0, head_xr = -1;
        if (shim_report_active()) {
            void shim_audio_headroom(int *, int *, int *);
            shim_audio_headroom(&head_ms, &head_q, &head_xr);
        }
        shim_objc_msgSend(vc, s_draw, NULL);
        shim_probe_tick();          /* field hunting; empty in the build we play on */
        shim_audio_tick();          /* the audio stream: reopen it if the system dropped it */
        shim_gl_frame_end();
        /*
         * The frame's work, for the report: from its start to here, with the
         * time the present spent held by the panel apart (gl.c
         * g_gl_present_ns: the blit, where the next window buffer may be
         * dequeued, and the swap). A frame held there is waiting, not working.
         */
        clock_gettime(CLOCK_MONOTONIC, &tf);
        long long took = (long long) tf.tv_sec * 1000000000LL + tf.tv_nsec - frame_began;
        long long held = g_gl_present_ns;
        g_gl_present_ns = 0;
        if (held < 0 || held > took) held = held < 0 ? 0 : took;
        {   /* the frame's own work, to the system (THE FRAME'S BUDGET) */
            int hfps = shim_want_fps();
            frame_hint(took, 1000000000LL / (hfps > 0 ? hfps : 60));
        }
        /*
         * A frame that took far longer than its period goes into the tester's
         * report with its swap split out: the swap is the wait for the panel
         * and a buffer, everything else is the game and this layer. Thirty
         * lines a launch, then silence - a report is not a log.
         */
        if (shim_report_active()) {
            DIAG(g_work_sum += took; g_held_sum += held; g_work_n++);   /* the frame's work, for the vsync part */
            DIAG(cpu_note());                    /* the core it ran on, and its clock */
            DIAG(if (shim_sticks_in_game()) cam_note());   /* the room's camera, in a run */
            DIAG(if (PROFILE_FRAMES) prof_window_take());   /* and where it stood, for the minute */
            DIAG(rate_note(frame_began));        /* frames started in every second, for the speed line */
            DIAG(truth_frame_note(frame_began)); /* the ground truth of game time, live frames only */
            long major = 0, minor = 0;
            faults(&major, &minor);
            /*
             * A stall is worth a line, but a tester plays for an hour: thirty
             * lines and then silence would cover the first minute and lose the
             * rest. So the lines are PACED instead of capped - at most one
             * every three seconds, for as long as the session lasts, which
             * keeps a long report readable and still catches a bad patch an
             * hour in.
             */
            long long now_ns = (long long) tf.tv_sec * 1000000000LL + tf.tv_nsec;
            static long long told_at, told_play;
            /*
             * IS THIS A LOADING FRAME? The owner asked a question the pacing
             * above could not answer: are there stalls DURING PLAY, or only on
             * the way into a room? One line every three seconds is enough to
             * follow a bad patch, but a loading stall takes the slot and a
             * stall in play right after it is never written down - so the
             * first report showed 90 loading stalls and one that was not, and
             * that one was the app coming back from the background.
             *
             * So the two are counted apart (every frame, no sampling at all -
             * gl.c prints the tally on the frames line), and a stall that
             * carried NO pack read and NO texture upload gets a pace of its
             * own: they are supposed to be rare, and if they are not, that is
             * the answer.
             */
            int loading;
            {
                extern long g_pack_data, g_gl_teximages;
                loading = (g_pack_data - was_pack_data) > 0 || (g_gl_teximages - was_tex) > 0;
            }
            if (took > 33000000LL) {
                extern long g_stalls_loading, g_stalls_in_play;
                if (loading) g_stalls_loading++; else g_stalls_in_play++;
            }
            /*
             * A LOAD IS A RUN OF LONG FRAMES, and the pacing hid all but the
             * first: three seconds of silence after a line, while the frames
             * that make the loading animation stand come one after another. The
             * owner asked why the animation freezes when a run starts, and the
             * answer was being thrown away before it was written (13.09.2026).
             * So the first sixteen loading frames are all written, whatever the
             * pacing says; everything else is paced as before.
             */
            /*
             * IN THE MENU A SMALLER HITCH SHOWS. The owner reports odd lags while
             * the camera slides across the menu's canvas (14.09.2026), and forty
             * milliseconds is too coarse a sieve for those: a slide that drops
             * from sixty to forty frames a second never trips it. So while no run
             * is going, the bar is twenty-two milliseconds - a frame and a third -
             * and the pacing below is left alone, since a slide is over in a
             * second and every frame of it is wanted.
             */
            int shim_sticks_in_game(void);
            long long bar = shim_sticks_in_game() ? 40000000LL : 22000000LL;
            static int menu_told;
            if (!shim_sticks_in_game() && took > bar && menu_told < 120) {
                menu_told++;
                told_at = 0;                 /* never paced out while there is room */
            }
            static int loading_told;
            int worth_a_line = took > bar
                && (now_ns - told_at > 3000000000LL
                    || (!loading && now_ns - told_play > 1000000000LL)
                    || (loading && loading_told < 16));
            if (worth_a_line && loading) loading_told++;
            if (!loading) told_play = now_ns;
            /*
             * Under a cutscene every long frame is written, paced or not: the
             * page changes come a few seconds apart, the pacing above would
             * drop most of them, and a tester's sound drops out exactly there
             * (audio.c, the dry watch).
             */
            {
                int shim_screen_cutscene(void);
                static int cut_told;
                static int was_cut;
                int cut = shim_screen_cutscene(), first = cut && !was_cut;
                was_cut = cut;
                if (cut && (took > 40000000LL || first) && cut_told < 80) {
                    extern long g_pack_data, g_gl_teximages, g_gl_texpixels;
                    extern long long g_gl_tex_ns;
                extern long long g_gl_ns_draw, g_gl_ns_clear, g_gl_ns_bindfb;
                extern long g_gl_compiles, g_gl_links;
                extern long long g_gl_ns_shader;
                /* whichever wrapped GL call took the longest this frame */
                extern const char *const g_glw_name[];
                extern long long g_glw_ns[];
                extern long g_glw_n[];
                char worst_gl[120] = "none";
                {
                    int best = -1;
                    for (int k = 0; k < 14; k++)
                        if (g_glw_ns[k] > 0 && (best < 0 || g_glw_ns[k] > g_glw_ns[best])) best = k;
                    if (best >= 0)
                        snprintf(worst_gl, sizeof(worst_gl), "%s %lld ms over %ld calls",
                                 g_glw_name[best], g_glw_ns[best] / 1000000LL, g_glw_n[best]);
                }
                    extern long g_fread_bytes;
                    char cut[300];
                    int after_xr = -1;
                    void shim_audio_headroom(int *, int *, int *);
                    int shim_audio_refilled_here(void);
                    shim_audio_headroom(NULL, NULL, &after_xr);
                    cut_told++;
                    snprintf(cut, sizeof(cut),
                             "%sa frame of %lld ms, swap %lld ms; textures +%ld of %ld Kpx in %lld ms; "
                             "pack %ld read, fread %ld KB; music queues %d, the least held %d ms going in; "
                             "underruns %d -> %d; refilled on the frame's thread %d",
                             first ? "begins: " : "",
                             took / 1000000LL, g_gl_swap_ns / 1000000LL,
                             g_gl_teximages - was_tex, (g_gl_texpixels - was_texpx) / 1024,
                             (g_gl_tex_ns - was_texns) / 1000000LL,
                             g_pack_data - was_pack_data, (g_fread_bytes - was_freadb) / 1024,
                             head_q, head_ms, head_xr, after_xr, shim_audio_refilled_here());
                    report_line("cutscene", cut);
                }
            }
            if (worth_a_line) {
                int shim_sticks_in_game(void);
                extern long g_gl_draws;
                extern long g_pack_fopen, g_pack_fd, g_pack_data, g_pack_copied, g_pack_again;
                extern long g_pack_checks;
                extern long g_gl_teximages, g_gl_texpixels, g_gl_texdeletes;
                extern long g_gl_bigimages, g_gl_bigpixels, g_gl_surfaces, g_gl_surfpixels;
                extern int g_gl_big_w, g_gl_big_h;
                extern long long g_gl_tex_ns;
                extern long long g_gl_ns_draw, g_gl_ns_clear, g_gl_ns_bindfb;
                extern long g_gl_compiles, g_gl_links;
                extern long long g_gl_ns_shader;
                /* whichever wrapped GL call took the longest this frame */
                extern const char *const g_glw_name[];
                extern long long g_glw_ns[];
                extern long g_glw_n[];
                char worst_gl[120] = "none";
                {
                    int best = -1;
                    for (int k = 0; k < 14; k++)
                        if (g_glw_ns[k] > 0 && (best < 0 || g_glw_ns[k] > g_glw_ns[best])) best = k;
                    if (best >= 0)
                        snprintf(worst_gl, sizeof(worst_gl), "%s %lld ms over %ld calls",
                                 g_glw_name[best], g_glw_ns[best] / 1000000LL, g_glw_n[best]);
                }
                extern long g_fread_calls, g_fread_bytes;
                extern char g_again_name[3][64];
                char line[640];
                snprintf(line, sizeof(line),
                         "%lld ms, swap %lld ms; faults %ld major %ld minor; objects %ld; "
                         "draws %ld; textures +%ld of %ld Kpx in %lld ms, -%ld; "
                         "big +%ld of %ld Kpx (surfaces %ld of %ld Kpx, largest %d x %d); "
                         "GL draws %lld ms, clears %lld ms, framebuffer binds %lld ms; "
                         "shaders %ld compiled and %ld linked in %lld ms; the slowest GL call: %s; "
                         "pack %ld read (%ld of them again: %s %s %s), %ld checks, %ld streams; fread %ld calls of %ld KB; %s%s",
                         took / 1000000LL, g_gl_swap_ns / 1000000LL,
                         major - was_major, minor - was_minor,
                         g_objects - was_objects, g_gl_draws - was_draws,
                         g_gl_teximages - was_tex, (g_gl_texpixels - was_texpx) / 1024,
                         (g_gl_tex_ns - was_texns) / 1000000LL,
                         g_gl_texdeletes - was_texdel,
                         g_gl_bigimages - was_big, (g_gl_bigpixels - was_bigpx) / 1024,
                         g_gl_surfaces - was_surf, (g_gl_surfpixels - was_surfpx) / 1024,
                         g_gl_big_w, g_gl_big_h,
                         g_gl_ns_draw / 1000000LL, g_gl_ns_clear / 1000000LL,
                         g_gl_ns_bindfb / 1000000LL,
                         g_gl_compiles, g_gl_links, g_gl_ns_shader / 1000000LL, worst_gl,
                         g_pack_data - was_pack_data, g_pack_again - was_pack_again,
                         g_again_name[0], g_again_name[1], g_again_name[2],
                         g_pack_checks - was_pack_checks,
                         g_pack_fopen - was_pack_fopen,
                         g_fread_calls - was_fread, (g_fread_bytes - was_freadb) / 1024,
                         shim_sticks_in_game()
                             ? (loading ? "in a run" : "IN PLAY, nothing loaded")
                             : "in the menu",
                         shim_gpu_probe_recent() ? "; THE REPORT'S PROBE FRAME (glFinish at every pass, slow on purpose)" : "");
                report_line("stall", line);
                if (PROFILE_FRAMES) { prof_report(prof_at, g_prof_n);
                                      prof_callers(prof_at, g_prof_n);
                                      prof_chains(prof_at, g_prof_n);
                                      sys_report((int) (took / 1000000LL)); }
                stalls_told++;
                told_at = now_ns;
                /* who asked for the surfaces in this frame, by the caller's address */
                int shim_screen_births(char *, size_t);
                char births[400];
                if (shim_screen_births(births, sizeof(births))) report_line("births", births);
            } else {
                int shim_screen_births(char *, size_t);
                char births[400];
                shim_screen_births(births, sizeof(births));      /* a quiet frame: forget them */
            }
            { extern long long g_gl_ns_draw, g_gl_ns_clear, g_gl_ns_bindfb, g_gl_ns_shader;
              extern long g_gl_compiles, g_gl_links;
              g_gl_ns_draw = g_gl_ns_clear = g_gl_ns_bindfb = g_gl_ns_shader = 0;
              g_gl_compiles = g_gl_links = 0; }
            { extern long long g_glw_ns[]; extern long g_glw_n[];
              for (int k = 0; k < 14; k++) { g_glw_ns[k] = 0; g_glw_n[k] = 0; } }
            was_major = major; was_minor = minor;
            was_objects = g_objects;
            { extern long g_gl_draws; was_draws = g_gl_draws; }
            { extern long g_pack_fopen, g_pack_fd, g_pack_data, g_pack_copied, g_pack_again;
                extern char g_again_name[3][64];
              was_pack_fopen = g_pack_fopen; was_pack_fd = g_pack_fd;
              was_pack_data = g_pack_data; was_pack_copied = g_pack_copied;
              was_pack_again = g_pack_again;
              { extern long g_pack_checks; was_pack_checks = g_pack_checks; } }
            { extern long g_gl_teximages, g_gl_texpixels, g_gl_texdeletes;
                extern long long g_gl_tex_ns;
                extern long long g_gl_ns_draw, g_gl_ns_clear, g_gl_ns_bindfb;
                extern long g_gl_compiles, g_gl_links;
                extern long long g_gl_ns_shader;
                /* whichever wrapped GL call took the longest this frame */
                extern const char *const g_glw_name[];
                extern long long g_glw_ns[];
                extern long g_glw_n[];
                char worst_gl[120] = "none";
                {
                    int best = -1;
                    for (int k = 0; k < 14; k++)
                        if (g_glw_ns[k] > 0 && (best < 0 || g_glw_ns[k] > g_glw_ns[best])) best = k;
                    if (best >= 0)
                        snprintf(worst_gl, sizeof(worst_gl), "%s %lld ms over %ld calls",
                                 g_glw_name[best], g_glw_ns[best] / 1000000LL, g_glw_n[best]);
                }
                extern long g_fread_calls, g_fread_bytes;
              was_tex = g_gl_teximages; was_texpx = g_gl_texpixels;
              was_texdel = g_gl_texdeletes;
              { extern long g_gl_bigimages, g_gl_bigpixels, g_gl_surfaces, g_gl_surfpixels;
                was_big = g_gl_bigimages; was_bigpx = g_gl_bigpixels;
                was_surf = g_gl_surfaces; was_surfpx = g_gl_surfpixels; }
              { extern long long g_gl_tex_ns; was_texns = g_gl_tex_ns; } }
            { extern long g_fread_calls, g_fread_bytes;
              was_fread = g_fread_calls; was_freadb = g_fread_bytes; }
        }
#if ISAAC_DIAG
        if (frame == 0 || frame == 59 || (frame % 600) == 599) {
            LOG("frame %ld drawn: messages %ld, without a method %ld",
                frame + 1, g_msg_count, g_msg_missed);
            /*
             * We never free Objective-C objects at all, so their count is a
             * direct measure of the leak: divided by the number of frames it
             * says how much of it falls on each frame.
             */
            LOG("   objects created %ld for %ld bytes (%.1f per frame)",
                g_objects, g_object_bytes, (double) g_object_bytes / (double) (frame + 1));
            { void show_class_counts(long); show_class_counts(frame + 1); }
            /*
             * The shell takes its "there is something to show" marker from two
             * of the game's globals. While nothing is being drawn those are
             * exactly what to look at: they say whether the game has reached a
             * working state.
             */
            void *game = *(void **) (g_img_lo + 0xabfb38);
            void *world = *(void **) (g_img_lo + 0xac0ab8);
            extern long g_gl_draws, g_gl_clears, g_gl_programs, g_gl_viewports;
            LOG("   game %p (ready %d), world %p (state %d)",
                game, game ? *(int *) ((char *) game + 0x38) : -1,
                world, world ? *(int *) ((char *) world + 0x21018) : -1);
            extern long g_gl_draws_by_fb[8];
            LOG("   GL: clears %ld, programs %ld, viewports %ld, draws %ld",
                g_gl_clears, g_gl_programs, g_gl_viewports, g_gl_draws);
            LOG("   draws per framebuffer: 0:%ld 1:%ld 2:%ld 3:%ld 4:%ld",
                g_gl_draws_by_fb[0], g_gl_draws_by_fb[1], g_gl_draws_by_fb[2],
                g_gl_draws_by_fb[3], g_gl_draws_by_fb[4]);
            /* edition: 1 Afterbirth, 2 Afterbirth+, 3 Repentance */
            LOG("   edition: signature %08x, level %d",
                *(unsigned *) (g_img_lo + 0xabec30), *(int *) (g_img_lo + 0xabec34));
            unsigned *vp = (unsigned *) (g_img_lo + 0xabec64);
            LOG("   touch area: %u x %u, origin %u %u", vp[0], vp[1], vp[2], vp[3]);
            unsigned char *f = (unsigned char *) (g_img_lo + 0xabec74);
            LOG("   image markers: mode %d, +24 %02x +25 %02x +26 %02x",
                *(int *) f, f[0x24], f[0x25], f[0x26]);
            if (g_shell) {
                unsigned char *b = (unsigned char *) g_shell;
                unsigned long *w = (unsigned long *) g_shell;
                LOG("   shell: markers %02x %02x %02x, update +50 -> %06lx, present +58 -> %06lx",
                    b[0x40], b[0x41], b[0x42],
                    w[0x50 / 8] - (unsigned long) g_img_lo,
                    w[0x58 / 8] - (unsigned long) g_img_lo);
            }
        }
#endif
        int fps = shim_want_fps();
        long long period = 1000000000LL / (fps > 0 ? fps : 60);
        extern long long g_gl_present_due, g_gl_present_lead;   /* the next frame's picture time (gl.c) */
        if (link_wait_due(period, &due)) {
            struct timespec now;                 /* keep the timer's grid in step, for the day it is needed */
            clock_gettime(CLOCK_MONOTONIC, &now);
            next = (long long) now.tv_sec * 1000000000LL + now.tv_nsec;
            g_gl_present_due = g_present_due;    /* its slot plus a period (THE GAME'S OWN CLOCK) */
            g_gl_present_lead = g_present_near;
            continue;
        }
        g_gl_present_due = 0;                    /* the timer's frame: its picture at once */
        /* no vsync came: the timer's grid, as before */
        struct timespec now;
        clock_gettime(CLOCK_MONOTONIC, &now);
        long long ns = (long long) now.tv_sec * 1000000000LL + now.tv_nsec;
        next += period;
        if (next < ns) next = ns;            /* we fell behind — start counting afresh */
        else wait_until(next);               /* the whole wait, a signal or not */
        due = 0;                              /* the vsync grid is re-anchored when it returns */
    }
}

long shim_objc_messages(void) { return g_msg_count; }
long shim_objc_missed(void) { return g_msg_missed; }
