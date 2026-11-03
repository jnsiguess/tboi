/* cut out of objc.c by run.py - do not edit */
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
static long long g_late_frames, g_work_n;

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
static long long g_cpu_frames[CPU_SEEN];
static long long g_cpu_khz_sum[CPU_SEEN];
static long long g_cpu_khz_n[CPU_SEEN];
static long long g_cpu_top_khz[CPU_SEEN];        /* cpuinfo_max_freq, read once; -1 unreadable */

int sched_getcpu(void);                      /* bionic declares it for _GNU_SOURCE only */

static long long cpu_sysfs_khz(int cpu, const char *leaf) {
    char path[96];
    snprintf(path, sizeof(path), "/sys/devices/system/cpu/cpu%d/cpufreq/%s", cpu, leaf);
    FILE *f = fopen(path, "r");
    if (!f) return -1;
    long long v = -1;
    if (fscanf(f, "%ld", &v) != 1) v = -1;
    fclose(f);
    return v;
}

static void cpu_note(void) {
    static long long frames;
    int c = sched_getcpu();
    if (c < 0 || c >= CPU_SEEN) return;
    g_cpu_frames[c]++;
    if (frames++ % 15) return;
    long long khz = cpu_sysfs_khz(c, "scaling_cur_freq");
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
unsigned long long shim_image_base(void);
static long long g_cam_frames, g_cam_frac, g_cam_moved, g_cam_shook;
static float g_cam_fmin = 1.0f, g_cam_fmax = 0.0f, g_cam_step, g_cam_last[2], g_cam_xmin = 1e9f, g_cam_xmax = -1e9f,
             g_cam_ymin = 1e9f, g_cam_ymax = -1e9f;
static int g_cam_have;

static float frac_of(float v) { float f = v - floorf(v); return f > 0.5f ? 1.0f - f : f; }  /* distance to a whole unit */

static void cam_note(void) {
    unsigned long long b = shim_image_base();
    if (!b) return;
    unsigned long long G = *(const unsigned long long *) (b + 0xac3b90);
    if (!G) return;
    unsigned long long room = *(const unsigned long long *) (G + 0x21550);
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
        void shim_screen_split_window(char *, unsigned long long);
        if (at > 0 && (size_t) at < room) shim_screen_split_window(out + at, room - (size_t) at);
        return;
    }
    int at = snprintf(out, room, " | camera over %ld frames: x %.2f..%.2f y %.2f..%.2f, moved in %ld, off a whole unit in %ld"
             " (by %.3f..%.3f), largest step %.3f; shaking in %ld",
             g_cam_frames, (double) g_cam_xmin, (double) g_cam_xmax, (double) g_cam_ymin, (double) g_cam_ymax,
             g_cam_moved, g_cam_frac, (double) (g_cam_frac ? g_cam_fmin : 0.0f), (double) g_cam_fmax,
             (double) g_cam_step, g_cam_shook);
    { void shim_screen_split_window(char *, unsigned long long);
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
static void on_vsync32(long long when, void *data) { (void) data; g_vsync_ns = when; g_vsync_came = 1; g_vsync_asked = 0; }

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
 * 60 Hz by itself mid-game (the research notes of 11.10.2026 on speed, reports/2026-10-13/
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
static long long g_cap_held;              /* times the clock moved to a late frame's start (no catch-up), for the report */
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
        struct timespec w = { (time_t) ((until - now) / 1000000000LL), (long long) ((until - now) % 1000000000LL) };
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


