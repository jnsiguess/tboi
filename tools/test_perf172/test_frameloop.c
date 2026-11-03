/*
 * Desk test of the frame loop's wait (hotfix 1.73, 60 Hz): objc.c's display
 * link - g_link .. link_wait_due, pace_note included - cut out of objc.c as it
 * is by run.py (frameloop_cut.h), over a fake Choreographer, looper and window
 * buffer queue. The world comes on stdin, drawn by the desk model
 * (research notes of 08.10.2026, frame60_model.py World): the panel's vsyncs and their
 * timestamps, the compositor's latches, the refresh-rate callback's events,
 * the callbacks' latencies, and every frame's CPU and GPU work. The loop is
 * objc.c's (shim_UIApplicationMain) with the game's draw: replaced by its CPU
 * time and shim_gl_present by the buffer queue. Out: every frame's start, and
 * the rule's switches - run.py compares them with the model's.
 *
 *   FRAMELOOP_PACE 0   1.71: no pace_note
 *                  1   1.72: pace_note gets the frame's work, present included
 *                  2   1.73: pace_note gets the work without the present held
 *   FRAMELOOP_PRESENT 1   1.74: every start goes out with the picture's time
 *                         (g_present_due, g_present_near) the loop named for it
 *   FRAMELOOP_CAP 1       1.75 (kept for run.py; the timer's road is 1.74's again)
 *   FRAMELOOP_PICS 1      every picture the compositor shows goes to stderr:
 *                         "frame latch", and every present's hold: "h ns"
 *                         (the speed desk, run.py speed); and every present:
 *                         "p 1" given its time, "p 0" automatic
 *   FRAMELOOP_SIGPROF 1   the report build's sampler (objc.c prof_start): a
 *                         signal every millisecond ends nanosleep and the
 *                         looper's poll early (EINTR, ALOOPER_POLL_WAKE)
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "diag.h"
#undef clock_gettime
#define LOG(...) do { } while (0)

/* ---------- the fake platform ---------- */

typedef struct AChoreographer AChoreographer;
#define ALOOPER_PREPARE_ALLOW_NON_CALLBACKS 1
#define ALOOPER_POLL_WAKE (-1)
#define ALOOPER_POLL_CALLBACK (-2)
#define ALOOPER_POLL_TIMEOUT (-3)
#define ALOOPER_POLL_ERROR (-4)
#define RTLD_DEFAULT ((void *) 0)

#define POST_LEAD 300000LL
#define POLL_NS 100000000LL
#define SWAP_CPU 200000LL
#define QUEUE_LEAD 500000LL

static long long g_now;                       /* the desk's CLOCK_MONOTONIC */
static long long *g_app, *g_app_ts, *g_latch, *g_lat, *g_ev_t, *g_ev_p, *g_C, *g_G;
static int g_napp, g_nlatch, g_nlat, g_nev, g_nframes, g_buffers, g_inflight;
static int g_cb_k, g_ev_k;
static void (*g_cb)(long long, void *);
static void (*g_refresh)(long long, void *);
static AChoreographer *g_fake = (AChoreographer *) 0x1000;

static int sim_clock_gettime(int c, struct timespec *ts) {
    (void) c;
    ts->tv_sec = (time_t) (g_now / 1000000000LL);
    ts->tv_nsec = (long) (g_now % 1000000000LL);
    return 0;
}
#define clock_gettime sim_clock_gettime

/* 1.75 waits out a start held a hair past its vsync (objc.c wait_until): the desk's clock moves */
static int sim_nanosleep(const struct timespec *req, struct timespec *rem) {
    (void) rem;
    long long ns = (long long) req->tv_sec * 1000000000LL + req->tv_nsec;
#if FRAMELOOP_SIGPROF
    if (ns > 1000000LL) { g_now += 1000000LL; return -1; }   /* the sampler's signal: EINTR */
#endif
    g_now += ns;
    return 0;
}
#undef nanosleep
#define nanosleep sim_nanosleep

static int ALooper_prepare(int opts) { (void) opts; return 1; }
static AChoreographer *AChoreographer_getInstance(void) { return g_fake; }
static void AChoreographer_postFrameCallback(AChoreographer *c, void (*cb)(long long, void *), void *d) {
    (void) c; (void) d; g_cb = cb;
}
static void fake_post64(AChoreographer *c, void (*cb)(long long, void *), void *d) {
    (void) c; (void) d; g_cb = cb;
}
static void deliver_events(long long upto) {
    while (g_ev_k < g_nev && g_ev_t[g_ev_k] <= upto) {
        if (g_refresh) g_refresh(g_ev_p[g_ev_k], NULL);
        g_ev_k++;
    }
}
static void fake_register(AChoreographer *c, void (*cb)(long long, void *), void *d) {
    (void) c; (void) d; g_refresh = cb;
    deliver_events(0);                        /* registration tells the mode in force */
}
static void *dlsym(void *h, const char *name) {
    (void) h;
    if (!strcmp(name, "AChoreographer_postFrameCallback64")) return (void *) fake_post64;
    if (!strcmp(name, "AChoreographer_registerRefreshRateCallback")) return (void *) fake_register;
    return NULL;
}
static int first_at_or_after(const long long *a, int n, long long t) {
    int lo = 0, hi = n;
    while (lo < hi) { int m = (lo + hi) / 2; if (a[m] < t) lo = m + 1; else hi = m; }
    return lo;
}
static int ALooper_pollOnce(int ms, int *fd, int *ev, void **data) {
    (void) fd; (void) ev; (void) data;
    int k = first_at_or_after(g_app, g_napp, g_now + POST_LEAD);
    if (k >= g_napp) {                        /* the world's vsyncs are over: the run is over (19.10.2026 - */
        g_now += (long long) ms * 1000000LL;  /* the sampler's branch below returned WAKE here for ever) */
        return ALOOPER_POLL_TIMEOUT;
    }
#if FRAMELOOP_SIGPROF
    if (ms > 1 && (!g_cb || k >= g_napp || (g_now > g_app[k] ? g_now : g_app[k])
                   + g_lat[g_cb_k < g_nlat ? g_cb_k : g_nlat - 1] - g_now > 1000000LL)) {
        g_now += 1000000LL;                   /* the sampler's signal ends the poll early */
        return ALOOPER_POLL_WAKE;
    }
#endif
    if (!g_cb || k >= g_napp || g_app[k] - g_now > (long long) ms * 1000000LL) {
        g_now += (long long) ms * 1000000LL;
        return ALOOPER_POLL_TIMEOUT;
    }
    /* 1.75 waits for its own slot with a short poll: a callback dispatched late does not hold it past its
     * timeout - epoll returns at the timeout and the callback stays pending (the vsync road's 100 ms poll
     * keeps the desk model's way, frame60_model.py) */
    if (ms < 100) {
        long long at = (g_now > g_app[k] ? g_now : g_app[k]) + g_lat[g_cb_k < g_nlat ? g_cb_k : g_nlat - 1];
        if (at - g_now > (long long) ms * 1000000LL) {
            g_now += (long long) ms * 1000000LL;
            return ALOOPER_POLL_TIMEOUT;
        }
    }
    deliver_events(g_app[k]);
    long long v = g_app[k];
    g_now = (g_now > v ? g_now : v) + g_lat[g_cb_k < g_nlat ? g_cb_k : g_nlat - 1];
    g_cb_k++;
    void (*cb)(long long, void *) = g_cb;
    g_cb = NULL;
    cb(g_app_ts[k], NULL);
    return ALOOPER_POLL_CALLBACK;
}
static int shim_report_active(void) { return 0; }
static void report_line(const char *k, const char *t) { (void) k; (void) t; }

#include "frameloop_cut.h"

/* ---------- the window's buffer queue (the model's World/Loop.present) ---------- */

static int q_free, q_n, q_latch_k, q_showing;
static long long q_ready[64], q_due[64], q_gpu_free, *q_gpu_done;
static int q_frame[64];

/*
 * A buffer may carry a presentation time (1.74, gl.c THE PICTURE ON ITS SLOT):
 * it is latched no earlier than the latch at or after it, and of two buffers
 * both due at one latch the older is dropped when it is timed (BufferQueue
 * acquireBuffer) - the model's Loop.latches_upto in speed_model.py.
 */
static void q_pop(void) {
    memmove(q_ready, q_ready + 1, sizeof(q_ready[0]) * (size_t) (q_n - 1));
    memmove(q_due, q_due + 1, sizeof(q_due[0]) * (size_t) (q_n - 1));
    memmove(q_frame, q_frame + 1, sizeof(q_frame[0]) * (size_t) (q_n - 1));
    q_n--;
}

static void latches_upto(long long t) {
    while (q_latch_k < g_nlatch && g_latch[q_latch_k] <= t) {
        long long lt = g_latch[q_latch_k];
#define Q_OK(i) (q_ready[i] <= lt && q_due[i] <= lt)
        while (q_n > 1 && Q_OK(0) && q_due[0] && Q_OK(1)) { q_pop(); q_free++; }
        if (q_n && Q_OK(0)) {
#if FRAMELOOP_PICS
            fprintf(stderr, "%d %lld\n", q_frame[0], lt);
#endif
            q_pop();
            if (q_showing) q_free++;
            q_showing = 1;
        }
#undef Q_OK
        q_latch_k++;
    }
}

static long long present(long long t, int n, long long due, long long lead) {
    latches_upto(t);
    while (q_free == 0) {
        if (q_latch_k >= g_nlatch) break;
        latches_upto(g_latch[q_latch_k]);
        if (t < g_latch[q_latch_k - 1] + 100000) t = g_latch[q_latch_k - 1] + 100000;
    }
    q_free--;
    if (due && due - t <= lead) due = 0;       /* gl.c: a late picture keeps the automatic timestamp */
#if FRAMELOOP_PICS
    fprintf(stderr, "p %d\n", due != 0);
#endif
    t += SWAP_CPU;
    long long start = t > q_gpu_free ? t : q_gpu_free;
    long long done = start + g_G[n];
    q_gpu_free = done;
    q_gpu_done[n] = done;
    q_due[q_n] = due;
    q_frame[q_n] = n;
    q_ready[q_n++] = t + QUEUE_LEAD > done ? t + QUEUE_LEAD : done;
    if (g_inflight && n >= g_inflight && t < q_gpu_done[n - g_inflight]) t = q_gpu_done[n - g_inflight];
    return t;
}

static long long *read_block(const char *tag, int *n, int per) {
    char t[8];
    if (scanf("%7s %d", t, n) != 2 || strcmp(t, tag)) { printf("bad input at %s\n", tag); exit(2); }
    long long *a = (long long *) malloc(sizeof(long long) * (size_t) (*n * per + 1));
    for (int i = 0; i < *n * per; i++) if (scanf("%lld", &a[i]) != 1) { printf("short %s\n", tag); exit(2); }
    return a;
}

int main(void) {
    long long t_sim;
    if (scanf("%*s %d %d %lld", &g_buffers, &g_inflight, &t_sim) != 3) return 2;
    long long *app = read_block("A", &g_napp, 2);
    g_app = (long long *) malloc(sizeof(long long) * (size_t) g_napp);
    g_app_ts = (long long *) malloc(sizeof(long long) * (size_t) g_napp);
    for (int i = 0; i < g_napp; i++) { g_app[i] = app[2 * i]; g_app_ts[i] = app[2 * i + 1]; }
    g_latch = read_block("L", &g_nlatch, 1);
    long long *ev = read_block("E", &g_nev, 2);
    g_ev_t = (long long *) malloc(sizeof(long long) * (size_t) (g_nev + 1));
    g_ev_p = (long long *) malloc(sizeof(long long) * (size_t) (g_nev + 1));
    for (int i = 0; i < g_nev; i++) { g_ev_t[i] = ev[2 * i]; g_ev_p[i] = ev[2 * i + 1]; }
    g_lat = read_block("T", &g_nlat, 1);
    long long *w = read_block("W", &g_nframes, 2);
    g_C = (long long *) malloc(sizeof(long long) * (size_t) g_nframes);
    g_G = (long long *) malloc(sizeof(long long) * (size_t) g_nframes);
    for (int i = 0; i < g_nframes; i++) { g_C[i] = w[2 * i]; g_G[i] = w[2 * i + 1]; }
    q_gpu_done = (long long *) calloc((size_t) g_nframes + 1, sizeof(long long));
    q_free = g_buffers;

    /* objc.c shim_UIApplicationMain's loop, the draw: and the present replaced */
    const long long period = 1000000000LL / 60;
    link_start();
    long long next = g_now, due = 0;
    long late_was = 0;
    (void) late_was;
    long long pd = 0, pn = 0;                    /* gl.c g_gl_present_due / _lead for the frame now starting */
    (void) pn;
    for (int n = 0; g_now < t_sim && n < g_nframes; n++) {
        long long frame_began = g_now;
#if FRAMELOOP_PRESENT
        printf("%lld %lld %lld\n", frame_began, pd, pn);
#else
        printf("%lld\n", frame_began);
#endif
        g_now += g_C[n];
        long long t_present = g_now;
        g_now = present(g_now, n, pd, pn);
        long long took = g_now - frame_began;
        long long held = g_now - t_present;         /* gl.c g_gl_present_ns: the blit and the swap */
#if FRAMELOOP_PICS
        fprintf(stderr, "h %lld\n", held);           /* the present's hold, for the speed desk */
#endif
#if FRAMELOOP_PACE == 1
        pace_note(took, g_now, period);
#elif FRAMELOOP_PACE == 2
        pace_note(took - held, g_now, period);
#endif
        (void) took; (void) held;
        if (link_wait_due(period, &due)) {
            next = g_now;
#if FRAMELOOP_PRESENT
            pd = g_present_due;
            pn = g_present_near;
#endif
            continue;
        }
        pd = 0;
        next += period;
        if (next < g_now) next = g_now;
        else g_now = next;
        due = 0;
    }
#if FRAMELOOP_PACE
    printf("pace %d\n", g_pace_wait);
#endif
    return 0;
}
