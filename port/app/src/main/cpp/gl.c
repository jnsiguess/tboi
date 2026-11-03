/*
 * The real drawing surface: EGL instead of EAGL.
 *
 * On iOS the game does not draw into a window but into a colour renderbuffer
 * that a CAEAGLLayer hands to the context; showing a frame is
 * presentRenderbuffer. Android has nothing of the sort: the window *is*
 * framebuffer zero.
 *
 * So the substitution goes like this: renderbufferStorage:fromDrawable:
 * allocates an ordinary renderbuffer of the right size, the game draws into it
 * none the wiser, and presentRenderbuffer blits it into the window and swaps
 * buffers. The game never learns about the swap.
 */
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <pthread.h>
#include <time.h>
#include <dlfcn.h>
#include <string.h>
#include <android/log.h>
#include "diag.h"
#include <android/native_window.h>
#include <android/native_window_jni.h>
#include <EGL/egl.h>
#include <GLES3/gl3.h>

#if ISAAC_DIAG
#define LOG(...) __android_log_print(ANDROID_LOG_INFO, "isaac", __VA_ARGS__)
#else
#define LOG(...) do { } while (0)      /* the build we play on has no log */
#endif
#define ERR(...) __android_log_print(ANDROID_LOG_ERROR, "isaac", __VA_ARGS__)

static ANativeWindow *g_win;
static EGLDisplay g_dpy = EGL_NO_DISPLAY;
static EGLSurface g_surf = EGL_NO_SURFACE;
static EGLContext g_ctx = EGL_NO_CONTEXT;
static EGLConfig g_cfg;
static int g_w, g_h;

/*
 * The window comes and goes at Android's whim: rotation, minimising, the
 * screen going dark. The frame thread is its own and knows nothing of that —
 * so swapping the window and drawing are separated by a lock, and a thread
 * with no window simply waits.
 *
 * Without this, rotation killed the app: Java released the window while the
 * EGL surface still pointed at it.
 */
static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t g_wake = PTHREAD_COND_INITIALIZER;

/* the renderbuffer the game believes is its screen */
static GLuint g_color_rb, g_blit_fbo;
static long g_frames;

/* draw-call tally, kept by the stubs in shim.c */
long g_gl_draws;

static void drop_surface(void) {
    if (g_surf == EGL_NO_SURFACE) return;
    eglMakeCurrent(g_dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    eglDestroySurface(g_dpy, g_surf);
    g_surf = EGL_NO_SURFACE;
}

/*
 * Called from the layout thread. The old surface dies right here, before Java
 * releases the window — otherwise EGL is left holding a pointer to nowhere.
 */
void shim_gl_set_window(void *win) {
    pthread_mutex_lock(&g_lock);
    drop_surface();
    g_win = (ANativeWindow *) win;
    pthread_cond_broadcast(&g_wake);
    pthread_mutex_unlock(&g_lock);
    LOG("drawing window: %p", win);
}

/*
 * The frame rate is NOT declared, and that is deliberate.
 *
 * There used to be an ANativeWindow_setFrameRate(60, FIXED_SOURCE) here, on the
 * guess that a 60 fps producer landing exactly on the two-refresh boundary of a
 * 120 Hz panel was what made the picture drag on weak phones. The guess was
 * never confirmed and it cost more than it could have won: FIXED_SOURCE lets
 * the platform pick a rate it thinks matches, and sixty does not exist on a
 * 90 Hz screen - the nearest divisor there is THIRTY. A tester was locked at 30
 * fps with neither processor nor graphics above half load.
 *
 * So the window says nothing about its rate and takes what vsync gives it.
 */
static int make_surface(void) {
    g_surf = eglCreateWindowSurface(g_dpy, g_cfg, g_win, NULL);
    if (g_surf == EGL_NO_SURFACE) { ERR("surface was not created"); return 0; }
    if (!eglMakeCurrent(g_dpy, g_surf, g_surf, g_ctx)) { ERR("context did not become current"); return 0; }
    /* present on vsync: without it the frame tears down the middle */
    eglSwapInterval(g_dpy, 1);
    int w = 0, h = 0;
    eglQuerySurface(g_dpy, g_surf, EGL_WIDTH, &w);
    eglQuerySurface(g_dpy, g_surf, EGL_HEIGHT, &h);
    if (w != g_w || h != g_h) LOG("window size: %d x %d (was %d x %d)", w, h, g_w, g_h);
    g_w = w;
    g_h = h;
    return 1;
}

/*
 * Start and end of a frame. While there is no window the frame thread waits
 * here: there is nowhere to draw, and going on would mean calling GL with no
 * surface.
 */
void shim_gl_frame_begin(void) {
    pthread_mutex_lock(&g_lock);
    while (!g_win) pthread_cond_wait(&g_wake, &g_lock);
    if (g_ctx != EGL_NO_CONTEXT && g_surf == EGL_NO_SURFACE) make_surface();
    /* the GPU clock starts with the frame, report builds only (gpu.c) */
    if (g_ctx != EGL_NO_CONTEXT && g_surf != EGL_NO_SURFACE) {
        void shim_gpu_frame_open(void);
        shim_gpu_frame_open();
    }
}

void shim_gl_dump_quads(void);
void shim_sticks_overlay_reset(void);
void shim_sticks_between_frames(void);
void shim_reset_between_frames(void);
void shim_pocket_between_frames(void);
void shim_screen_letterbox(void);
void shim_screen_menu_boxes(void);
void shim_keys_deliver(void);

void shim_gl_frame_end(void) { pthread_mutex_unlock(&g_lock); }

/*
 * Frames, counted for the tester's report.
 *
 * A complaint of "it lags" cannot be worked with; a line saying 31 fps at a
 * canvas of 585 x 270 and 58 at 480 x 270 can. So while the report file is open
 * - and it is opened only by a build meant for testing - every five seconds one
 * line goes into it: the rate, the worst single frame of that stretch, and the
 * canvas the game was drawing, which is what says whether the wide screen was
 * on. Nothing here runs at all in a build for playing.
 */
long long g_gl_swap_ns;           /* how long the last swap blocked, for the frame loop's stall lines */
/*
 * The time this frame's present spent in the blit into the window and in
 * eglSwapBuffers, added up; the frame loop takes it and zeroes it (objc.c).
 * Both are where the thread can be held by the panel rather than by work: the
 * next window buffer is dequeued either at the first draw into the window (the
 * blit) or inside the swap, depending on the driver, and with none free it
 * waits for the compositor's next latch; the swap also waits when the driver
 * has too many frames in flight on the GPU. The report's frame work puts it
 * apart from the frame's own work (objc.c shim_vsync_window).
 */
long long g_gl_present_ns;

/*
 * THE PICTURE ON ITS SLOT (hotfix 1.74). "On a strong phone the game speeds up."
 *
 * iOS shows a 60 fps game on a 120 Hz panel one picture per 1/60 s: the
 * display link and the layer present on the 60 Hz cadence. Android shows a
 * buffer on the first vsync after the GPU is done with it. A light frame on
 * 120 Hz is done 6-9 ms after its start - right where the half-way vsync
 * falls - so one picture made that vsync and the next missed it: pictures 8,
 * 25, 8, 25 ms apart, each carrying a full 1/60 s of the game. Every 8 ms one
 * is the game jumping ahead at double speed; on the desk 400-1000 a minute on
 * a 120 Hz panel, the same in 1.666, 1.73 and on a 120.2 Hz S25 Ultra
 * (the research notes of 11.10.2026, speed; speed_model.py). The stronger
 * the phone, the more often its frames land there.
 *
 * So the frame loop names, for every frame, its slot on the 60 Hz grid plus
 * one period less half a vsync (objc.c THE GAME'S OWN CLOCK,
 * g_gl_present_due), and the swap hands that time to the compositor
 * (EGL_ANDROID_presentation_time, what the Android Frame Pacing library
 * does): the picture is shown no earlier than its slot. A picture already
 * late gets no time - the time lies less than a sixteenth of a period ahead -
 * and keeps the automatic timestamp: a timed buffer may be dropped when the
 * one after it is also due, and a late one must not be (Swappy skips the
 * same way). On a 60 Hz panel the time is
 * always behind the swap, so nothing changes there. The time sticks to the
 * window until it is set again (Surface::setBuffersTimestamp), so every swap
 * sets it, AUTO included.
 */
long long g_gl_present_due;       /* this frame's picture no earlier than this, CLOCK_MONOTONIC; 0: at once */
long long g_gl_present_lead;      /* the time is given only while it lies more than this ahead of the swap */
long g_gl_timed, g_gl_untimed;
int g_gl_fps;                         /* FPS COUNTER: pictures presented in the last second (shim_gl_present) */    /* pictures given their slot's time and not, for the report (objc.c) */
#define WINDOW_TIMESTAMP_AUTO (-9223372036854775807LL - 1)   /* system/window.h NATIVE_WINDOW_TIMESTAMP_AUTO */

#if ISAAC_DIAG
/*
 * ONE MINUTE WITH IT, ONE WITHOUT (report builds, 16.10.2026). A weak phone
 * lags in 1.75 and did not in 1.74, and the slot's time is one of 1.75's new
 * things: on some drivers a timed picture is held by the compositor, the
 * window runs out of buffers and the swap blocks. So in a report build every
 * frames window alternates - the slot's time given, then AUTO for every
 * picture - and the frames line says which. One session, both answers.
 */
static int g_present_off;
#endif

/*
 * Close the window NOW, wherever it stands.
 *
 * Asked when the app goes to the background, and it is not a nicety: a window
 * is normally written when a minute passes or when the display mode changes,
 * so a tester who switches the mode, plays half a minute and then leaves the
 * game would lose that half minute entirely - the very half the whole test was
 * about. A window with fewer than sixty frames in it is still dropped: that is
 * noise, not a measurement.
 */
static int g_frames_forced;
static void frames_note(void);
static long long g_ours_ns, g_ours_worst_ns;   /* what our own layer costs a frame */

/* slow frames, split by whether anything was loaded in them; objc.c counts, this prints */
long g_stalls_loading, g_stalls_in_play;

void shim_gl_frames_flush(void) {
    g_frames_forced = 1;
    frames_note();
    g_frames_forced = 0;
}

static void frames_note(void) {
    int shim_report_active(void);
    void report_line(const char *, const char *);
    float shim_screen_width(void);
    float shim_screen_height(void);
    if (!shim_report_active()) return;

    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    double now = (double) ts.tv_sec + (double) ts.tv_nsec / 1e9;

    /*
     * The window's shape, not just its average. A tester's phone that holds
     * sixty on average and drops four frames a second is a different phone
     * from one that holds fifty-eight evenly, and the average alone cannot
     * tell them apart - so the long frames are counted as well.
     */
    /*
     * A MINUTE to a line, and never two display modes in one line.
     *
     * Five seconds was right while a stall was being hunted and wrong for a
     * tester: forty lines a minute of somebody else's play is a wall of
     * numbers, and the interesting comparison - one mode against the other -
     * is buried in it. So a line covers a minute, and the window is closed
     * early whenever the mode or the place changes, which keeps every line
     * about one thing. A window with too few frames in it is dropped: a
     * half-second between two switches says nothing.
     */
    int shim_screen_is_wide(void);
    int shim_sticks_in_game(void);
    int wide = shim_screen_is_wide(), run = shim_sticks_in_game();

    static double began, last, worst, spent;
    static int count, over33, over50, over100;
    static int was_wide = -1, was_run = -1;
    if (last > 0.0) {
        double gap = now - last;
        if (gap > worst) worst = gap;
        spent += gap;
        if (gap >= 0.100) over100++;
        else if (gap >= 0.050) over50++;
        else if (gap >= 0.033) over33++;
    }
    last = now;
    if (began == 0.0) { began = now; was_wide = wide; was_run = run; return; }
    count++;
    int switched = (wide != was_wide || run != was_run) || g_frames_forced;
    if (!switched && now - began < 60.0) return;
    if (switched && count < 60) {              /* too short to mean anything */
        began = now; count = 0; worst = 0.0; spent = 0.0;
        over33 = over50 = over100 = 0;
        was_wide = wide; was_run = run;
        return;
    }

    /*
     * The process's resident memory rides along: a weak phone that drops
     * frames and one that stops drawing rooms are both suspected of running
     * out of it, and the number is what settles that. /proc/self/statm is one
     * short read, once every five seconds.
     */
    long rss_mb = -1;
    {
        FILE *f = fopen("/proc/self/statm", "r");
        if (f) {
            long size = 0, resident = 0;
            if (fscanf(f, "%ld %ld", &size, &resident) == 2)
                rss_mb = resident * (long) sysconf(_SC_PAGESIZE) / (1024L * 1024L);
            fclose(f);
        }
    }
    /*
     * Which display mode this window belongs to is said in words, first thing
     * on the line. The canvas gives it away as well, but a person reading two
     * hundred lines of somebody else's report should not have to know that 585
     * means one mode and 480 the other.
     */
    /*
     * The line is labelled with the mode the WINDOW belonged to, not with the
     * one in force at this instant: when a switch closes the window early, the
     * frames just counted were drawn in the old mode.
     */
    /*
     * How many slow frames there were, and how many of them loaded nothing.
     * Counted on every frame rather than sampled, so this answers the question
     * the paced stall lines cannot: whether play itself hitches.
     */
    extern long g_stalls_loading, g_stalls_in_play;
    long probes = 0, asks = 0;
    char gpu_part[64];
    { void shim_gpu_window(char *, size_t); shim_gpu_window(gpu_part, sizeof(gpu_part)); }
    { void shim_trace_probe_counts(long *, long *); shim_trace_probe_counts(&probes, &asks); }
    char line[2000];                /* the vsync part and the ground truth (objc.c) ride on this line: 689 chars in run175e before them */
    int present_off = 0;
    DIAG(present_off = g_present_off);
    snprintf(line, sizeof(line),
             "FULLSCREEN %s, %s, slot time %s | %.1f a second over %.0f s | avg %.1f ms, worst %.0f ms | "
             "over 33 ms: %d, over 50: %d, over 100: %d | ours %.2f ms avg, %.1f ms worst, "
             "%.0f page asks a frame (%.0f probed) | %s | "
             "slow frames %ld loading, %ld in play | "
             "canvas %.0f x %.0f in a window %d x %d | rss %ld MB",
             was_wide ? "on " : "off",
             was_run ? "in a run  " : "in the menu",
             present_off ? "OFF" : "on",
             (double) count / (now - began), now - began,
             count ? spent * 1000.0 / count : 0.0, worst * 1000.0,
             over33, over50, over100,
             count ? (double) g_ours_ns / count / 1e6 : 0.0, (double) g_ours_worst_ns / 1e6,
             count ? (double) asks / count : 0.0, count ? (double) probes / count : 0.0,
             gpu_part,
             g_stalls_loading, g_stalls_in_play,
             (double) shim_screen_width(), (double) shim_screen_height(), g_w, g_h, rss_mb);
#if ISAAC_DIAG
    {   /* the panel's rate and the game's speed against iOS (objc.c) */
        void shim_vsync_window(char *, size_t, double);
        size_t n = strlen(line);
        shim_vsync_window(line + n, sizeof(line) - n, (double) count / (now - began));
    }
#endif
    report_line("frames", line);
#if ISAAC_DIAG
    /* the same window split into render passes, by road: Filter off / on (gpu.c) */
    { void shim_gpu_passes_window(void); shim_gpu_passes_window(); }
    /* where the frame thread stood over the window, and what it waited in (objc.c) */
    { void shim_prof_window(void); shim_prof_window(); }
    g_present_off = !g_present_off;                 /* the next window the other way */
#endif
    began = now; count = 0; worst = 0.0; spent = 0.0;
    over33 = over50 = over100 = 0;
    g_stalls_loading = g_stalls_in_play = 0;
    g_ours_ns = 0; g_ours_worst_ns = 0;
    was_wide = wide; was_run = run;
}

int shim_gl_ready(void) { return g_ctx != EGL_NO_CONTEXT; }

int shim_gl_width(void) { return g_w; }
int shim_gl_height(void) { return g_h; }

/*
 * Set up once, on the game's first request for a context. We ask for ES 3:
 * that is exactly what the game asks for through initWithAPI.
 */
int shim_gl_start(void) {
    if (g_ctx != EGL_NO_CONTEXT) return 1;
    if (!g_win) { ERR("no window yet"); return 0; }

    g_dpy = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if (g_dpy == EGL_NO_DISPLAY) { ERR("no EGL display"); return 0; }
    if (!eglInitialize(g_dpy, NULL, NULL)) { ERR("eglInitialize failed"); return 0; }

    /*
     * No depth buffer - and that is the iOS behaviour, not a saving.
     *
     * The game keeps GL_DEPTH_TEST enabled from the first frame to the last
     * and never turns it off (measured: glIsEnabled says 1 throughout). On
     * iOS that costs nothing, because the layer it draws into carries no
     * depth attachment at all, and a depth test without a depth buffer always
     * passes. Everything the game draws is flat and ordered by the order of
     * the calls.
     *
     * Ask Android for a depth buffer and that stops being true: sprites of
     * the touch overlay land at one and the same depth, the second one fails
     * the test and simply never appears - the button is there, it takes the
     * finger, but nothing is drawn. That is what the players saw as "the bomb
     * and pause buttons are missing", and it depended on the device only
     * because the frame is composed slightly differently on each.
     */
    const EGLint want[] = {
        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT,
        EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
        EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8, EGL_ALPHA_SIZE, 8,
        EGL_DEPTH_SIZE, 0,
        EGL_NONE
    };
    EGLConfig got[32];
    EGLint n = 0;
    if (!eglChooseConfig(g_dpy, want, got, 32, &n) || n < 1) {
        ERR("no suitable EGL config");
        return 0;
    }
    /*
     * Zero in the request means "no less than zero", so a config with depth
     * still qualifies and may well come first. We pick the one that really
     * has none, and only fall back to the first if the device offers nothing
     * else.
     */
    g_cfg = got[0];
    EGLint depth = -1;
    for (EGLint i = 0; i < n; i++) {
        EGLint d = -1;
        if (eglGetConfigAttrib(g_dpy, got[i], EGL_DEPTH_SIZE, &d) && d == 0) {
            g_cfg = got[i];
            depth = 0;
            break;
        }
    }
    if (depth != 0) eglGetConfigAttrib(g_dpy, g_cfg, EGL_DEPTH_SIZE, &depth);
    LOG("EGL config: depth buffer %d bits of %d configs offered", depth, n);

    const EGLint attrs[] = {EGL_CONTEXT_CLIENT_VERSION, 3, EGL_NONE};
    g_ctx = eglCreateContext(g_dpy, g_cfg, EGL_NO_CONTEXT, attrs);
    if (g_ctx == EGL_NO_CONTEXT) { ERR("context was not created"); return 0; }

    if (!make_surface()) return 0;
    LOG("EGL is up: %d x %d, ES 3, vendor %s", g_w, g_h, glGetString(GL_VENDOR));
    LOG("   renderer: %s", glGetString(GL_RENDERER));
    return 1;
}

int shim_gl_make_current(void) {
    if (g_ctx == EGL_NO_CONTEXT) return 0;
    return eglMakeCurrent(g_dpy, g_surf, g_surf, g_ctx) ? 1 : 0;
}

/*
 * The game has tied its colour renderbuffer to the layer. We have no layer, so
 * we simply allocate storage the size of the window and remember it: it will
 * be needed when the frame is presented.
 */
int shim_gl_bind_drawable(void) {
    if (!shim_gl_start()) return 0;
    GLint rb = 0;
    glGetIntegerv(GL_RENDERBUFFER_BINDING, &rb);
    if (!rb) { ERR("no colour renderbuffer bound — nothing to tie"); return 0; }
    g_color_rb = (GLuint) rb;
    glRenderbufferStorage(GL_RENDERBUFFER, GL_RGBA8, g_w, g_h);
    LOG("colour renderbuffer %u tied to the window: %d x %d", g_color_rb, g_w, g_h);
    return 1;
}

/*
 * Presenting a frame. On iOS the colour renderbuffer *is* the screen; here it
 * is separate, so its contents are blitted into the window's framebuffer and
 * that is what gets swapped out.
 */
int shim_gl_present(void) {
    if (g_ctx == EGL_NO_CONTEXT || g_surf == EGL_NO_SURFACE) return 0;
#if ISAAC_DIAG
    static int told;
    if (!told) {
        told = 1;
        GLint fb = 0, rb = 0;
        glGetIntegerv(GL_FRAMEBUFFER_BINDING, &fb);
        glGetIntegerv(GL_RENDERBUFFER_BINDING, &rb);
        LOG("first present: framebuffer %d, colour buffer %d, GL error %04x",
            fb, rb, glGetError());
    }
#endif
    struct timespec b0, b1;
    clock_gettime(CLOCK_MONOTONIC, &b0);
    if (g_color_rb) {
        if (!g_blit_fbo) glGenFramebuffers(1, &g_blit_fbo);
        GLint prev = 0;
        glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prev);
        /*
         * THE SCREEN'S DEPTH IS NOT KEPT (25.10.2026, reports/2026-10-25/lag:
         * a Moto g84, Adreno 619, its GPU frame mostly at the window's size).
         * The game gives its screen framebuffer a 24-bit depth the size of the
         * window (the iOS view, 0x5fd4) and clears it at the start of every
         * screen pass - "depth loaded 0.00" in every report - but it never
         * says it is done with it: the image imports no glDiscardFramebuffer.
         * On a tiled card an attachment nobody discards is written out to
         * memory when the pass ends, ten megabytes a frame at 2400 x 1080, to
         * be cleared unread. iOS's own driver is told the same through the
         * layer; here it is said once, at the end of the frame, on the screen
         * framebuffer alone (its colour is our renderbuffer). The colour is
         * kept: the blit below reads it.
         */
        if (prev) {
            GLint type = 0, name = 0;
            glGetFramebufferAttachmentParameteriv(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                                                  GL_FRAMEBUFFER_ATTACHMENT_OBJECT_TYPE, &type);
            if (type == GL_RENDERBUFFER)
                glGetFramebufferAttachmentParameteriv(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                                                      GL_FRAMEBUFFER_ATTACHMENT_OBJECT_NAME, &name);
            if (type == GL_RENDERBUFFER && (GLuint) name == g_color_rb) {
                static const GLenum gone[] = { GL_DEPTH_ATTACHMENT, GL_STENCIL_ATTACHMENT };
                glInvalidateFramebuffer(GL_FRAMEBUFFER, 2, gone);
            }
        }
        glBindFramebuffer(GL_READ_FRAMEBUFFER, g_blit_fbo);
        glFramebufferRenderbuffer(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                                  GL_RENDERBUFFER, g_color_rb);
        g_frames++;
        /*
         * A GRID OF PIXELS USED TO BE READ BACK HERE every 120 frames, out of
         * five framebuffers - 49 points each, up to 245 glReadPixels in one
         * frame. Every one of them stops the pipe and waits for the card, and
         * THAT was the lag: at 58 frames a second it came round every 2.1
         * seconds, and over 46 seconds of measurement exactly 22 frames went
         * past 50 ms (14.09.2026).
         *
         * It was there to answer where the finished picture actually lands -
         * settled long ago, and the answer is written just below, where the
         * blit is decided. Gone entirely: a diagnostic that makes the thing it
         * measures stutter is worse than no diagnostic at all.
         */
        /*
         * The blit happens only when the game drew into its own buffer. The
         * measurement said otherwise: its final pass goes into framebuffer
         * zero, and on Android that *is* the window. Blitting an empty buffer
         * over it would wipe the finished frame.
         */
        extern long g_gl_draws_by_fb[8];
        if (g_gl_draws_by_fb[0] == 0) {
            DIAG({ void shim_gpu_pass_blit(int, int); shim_gpu_pass_blit(g_w, g_h); });
            glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
            glBlitFramebuffer(0, 0, g_w, g_h, 0, 0, g_w, g_h,
                              GL_COLOR_BUFFER_BIT, GL_NEAREST);
            /* the blit's pass is stamped while the window is still the target (gpu.c) */
            DIAG({ void shim_gpu_pass_blit_done(void); shim_gpu_pass_blit_done(); });
            DIAG(if (g_frames == 1) LOG("frame blit: error %04x", glGetError()));
        }
        glBindFramebuffer(GL_FRAMEBUFFER, (GLuint) prev);
    }
    clock_gettime(CLOCK_MONOTONIC, &b1);
    g_gl_present_ns += ((long long) b1.tv_sec - b0.tv_sec) * 1000000000LL + (b1.tv_nsec - b0.tv_nsec);
    /* the frame is complete — a good moment to see how it ended up */
    DIAG(shim_gl_dump_quads());
    /* the overlay flag lives for one frame: clear it always, not just in diag */
    shim_sticks_overlay_reset();
    /* a change of stick size is granted here, between frames */
    /*
     * OUR OWN work between frames, timed. The port walks the entity list and
     * the room grid every frame for the panel, and a tester's "it turns into a
     * slideshow" has to be answerable without guessing whose milliseconds
     * those are: the line below carries them beside the frame time.
     */
    struct timespec o0, o1;
    clock_gettime(CLOCK_MONOTONIC, &o0);
    shim_sticks_between_frames();
    shim_reset_between_frames();
    { void shim_rumble_between_frames(void); shim_rumble_between_frames(); }
    shim_pocket_between_frames();
    { void shim_marked_between_frames(void); shim_marked_between_frames(); }
    { void shim_touchedit_between_frames(void); shim_touchedit_between_frames(); }
    { void shim_credits_between_frames(void); shim_credits_between_frames(); }
    { void shim_tweaks_between_frames(void); shim_tweaks_between_frames(); }
    { void shim_spawner_between_frames(void); shim_spawner_between_frames(); }
    { void shim_mods_between_frames(void); shim_mods_between_frames(); }
    { void shim_straps_between_frames(void); shim_straps_between_frames(); }
    shim_screen_letterbox();       /* a cutscene gets its own frame, not the game's */
    { void shim_screen_tell(void); shim_screen_tell(); }
    shim_screen_menu_boxes();      /* the menus' hit boxes follow the widened canvas */
    { void shim_eid_between_frames(void); shim_eid_between_frames(); }
    { void shim_coop_between_frames(void); shim_coop_between_frames(); }
    { void shim_pad_between_frames(void); shim_pad_between_frames(); }
    { void shim_items_between_frames(void); shim_items_between_frames(); }
    clock_gettime(CLOCK_MONOTONIC, &o1);
    {
        long long ns = ((long long) o1.tv_sec - o0.tv_sec) * 1000000000LL
                     + (o1.tv_nsec - o0.tv_nsec);
        g_ours_ns += ns;
        if (ns > g_ours_worst_ns) g_ours_worst_ns = ns;
    }
    shim_keys_deliver();          /* the hardware keyboard, between frames like touches */
    frames_note();                /* one line every five seconds, test builds only */
    { void shim_gpu_frame_close(void); shim_gpu_frame_close(); }   /* and stops before the swap */
    struct timespec a, z;
    clock_gettime(CLOCK_MONOTONIC, &a);
    {   /* the picture's time (THE PICTURE ON ITS SLOT) */
        typedef EGLBoolean (*present_time_fn)(EGLDisplay, EGLSurface, int64_t);
        static present_time_fn present_time;
        static int looked;
        if (!looked) {
            looked = 1;
            const char *ext = eglQueryString(g_dpy, EGL_EXTENSIONS);
            if (ext && strstr(ext, "EGL_ANDROID_presentation_time"))
                present_time = (present_time_fn) eglGetProcAddress("eglPresentationTimeANDROID");
            LOG("presentation time: %s", present_time ? "the pictures keep the 60 Hz cadence" : "not offered");
        }
        if (present_time) {
            long long at = (long long) a.tv_sec * 1000000000LL + a.tv_nsec;
            long long due = g_gl_present_due;
            int timed = due > 0 && due - at > g_gl_present_lead;
            DIAG(if (g_present_off) timed = 0);       /* this window: every picture AUTO */
            present_time(g_dpy, g_surf, timed ? (int64_t) due : (int64_t) WINDOW_TIMESTAMP_AUTO);
            DIAG(if (timed) g_gl_timed++; else g_gl_untimed++);
        }
    }
    int shown = eglSwapBuffers(g_dpy, g_surf) ? 1 : 0;
    clock_gettime(CLOCK_MONOTONIC, &z);
    {   /* FPS COUNTER (Tweaks, 21.10.2026): the pictures handed to the screen in the last whole second */
        static long long since;
        static int n;
        long long t = (long long) z.tv_sec * 1000000000LL + z.tv_nsec;
        if (shown) n++;
        if (!since) since = t;
        if (t - since >= 1000000000LL) {
            g_gl_fps = (int) ((long long) n * 1000000000LL / (t - since));
            n = 0;
            since = t;
        }
    }
    g_gl_swap_ns = ((long long) z.tv_sec - a.tv_sec) * 1000000000LL + (z.tv_nsec - a.tv_nsec);
    g_gl_present_ns += g_gl_swap_ns;
    return shown;
}
