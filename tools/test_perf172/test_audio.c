/*
 * Desk test of the sound stream's life (hotfix 1.72, perf item 2): audio.c
 * compiled as it is (the build we play on, ISAAC_DIAG=0) over a fake AAudio
 * whose streams can be slow to open, refuse to open, come back DISCONNECTED,
 * underrun on command, and that counts every call made on a closed stream.
 * The frame thread is this program's main loop, calling shim_audio_tick every
 * 16 ms, and every tick is timed.
 *
 *   py -3 tools/test_perf172/run.py audio
 */
#ifdef AUDIO_OLD
#include "audio.c.before-perf172_lp64.c"   /* 1.71/1.72 as released, for the comparison */
#else
#include "audio_lp64.c"          /* audio.c with LP64 longs (run.py lp64) */
#endif
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

/* ---------------------------------------------------------------- the port */
int shim_report_active(void) { return 0; }
void report_line(const char *k, const char *t) { (void) k; (void) t; }
unsigned long long shim_image_base(void) { return 0; }
void shim_hook(void *at, void *to) { (void) at; (void) to; }
int shim_trace_add(unsigned long long off, const char *name) { (void) off; (void) name; return 0; }

/* ------------------------------------------------------------- fake AAudio */
struct AAudioStreamStruct { int state, xr, size, cap, burst, perf, closed, id; };
struct AAudioStreamBuilderStruct { int perf; };
static volatile int f_open_ms = 80, f_close_ms = 30;   /* how long an open and a close take */
static volatile int f_fail_opens;                      /* this many opens fail next */
static volatile int f_route_gone;                      /* streams open (and fall) DISCONNECTED */
static volatile int f_wait_ms;                         /* waitForStateChange takes this long */
static volatile long f_opens, f_closes, f_bad_use;
static int f_size = 192, f_cap = 960, f_burst = 96;
static AAudioStream *f_streams[4096];
static volatile long f_n;

static void use(AAudioStream *s) { if (!s || s->closed) InterlockedIncrement(&f_bad_use); }
aaudio_result_t AAudio_createStreamBuilder(AAudioStreamBuilder **b) { *b = calloc(1, sizeof(**b)); return AAUDIO_OK; }
aaudio_result_t AAudioStreamBuilder_delete(AAudioStreamBuilder *b) { free(b); return AAUDIO_OK; }
void AAudioStreamBuilder_setFormat(AAudioStreamBuilder *b, aaudio_format_t f) { (void) b; (void) f; }
void AAudioStreamBuilder_setChannelCount(AAudioStreamBuilder *b, int32_t n) { (void) b; (void) n; }
void AAudioStreamBuilder_setPerformanceMode(AAudioStreamBuilder *b, aaudio_performance_mode_t m) { b->perf = m; }
void AAudioStreamBuilder_setSharingMode(AAudioStreamBuilder *b, aaudio_sharing_mode_t m) { (void) b; (void) m; }
void AAudioStreamBuilder_setDataCallback(AAudioStreamBuilder *b, AAudioStream_dataCallback cb, void *u) { (void) b; (void) cb; (void) u; }
void AAudioStreamBuilder_setErrorCallback(AAudioStreamBuilder *b, AAudioStream_errorCallback cb, void *u) { (void) b; (void) cb; (void) u; }
aaudio_result_t AAudioStreamBuilder_openStream(AAudioStreamBuilder *b, AAudioStream **s) {
    Sleep((DWORD) f_open_ms);
    InterlockedIncrement(&f_opens);
    if (f_fail_opens > 0) { f_fail_opens--; *s = NULL; return AAUDIO_ERROR_INTERNAL; }
    AAudioStream *n = calloc(1, sizeof(*n));
    n->state = f_route_gone ? AAUDIO_STREAM_STATE_DISCONNECTED : AAUDIO_STREAM_STATE_OPEN;
    n->size = f_size; n->cap = f_cap; n->burst = f_burst; n->perf = b->perf; n->id = (int) f_opens;
    f_streams[f_n++ % 4096] = n;
    *s = n;
    return AAUDIO_OK;
}
aaudio_result_t AAudioStream_close(AAudioStream *s) { use(s); Sleep((DWORD) f_close_ms); s->closed = 1; InterlockedIncrement(&f_closes); return AAUDIO_OK; }  /* never freed: a later use is counted, not a crash */
aaudio_result_t AAudioStream_requestStart(AAudioStream *s) { use(s); if (s->state != AAUDIO_STREAM_STATE_DISCONNECTED) s->state = AAUDIO_STREAM_STATE_STARTED; return AAUDIO_OK; }
aaudio_result_t AAudioStream_requestPause(AAudioStream *s) { use(s); if (s->state != AAUDIO_STREAM_STATE_DISCONNECTED) s->state = AAUDIO_STREAM_STATE_PAUSING; return AAUDIO_OK; }
aaudio_result_t AAudioStream_requestFlush(AAudioStream *s) { use(s); if (s->state == AAUDIO_STREAM_STATE_PAUSED) s->state = AAUDIO_STREAM_STATE_FLUSHING; return AAUDIO_OK; }
aaudio_result_t AAudioStream_waitForStateChange(AAudioStream *s, aaudio_stream_state_t in, aaudio_stream_state_t *next, int64_t t) {
    (void) t;
    use(s);
    Sleep((DWORD) f_wait_ms);
    use(s);                                  /* still alive after the wait? */
    if (in == AAUDIO_STREAM_STATE_PAUSING && s->state == AAUDIO_STREAM_STATE_PAUSING) s->state = AAUDIO_STREAM_STATE_PAUSED;
    if (in == AAUDIO_STREAM_STATE_FLUSHING && s->state == AAUDIO_STREAM_STATE_FLUSHING) s->state = AAUDIO_STREAM_STATE_FLUSHED;
    *next = s->state;
    return AAUDIO_OK;
}
aaudio_stream_state_t AAudioStream_getState(AAudioStream *s) { use(s); return s->state; }
int32_t AAudioStream_getXRunCount(AAudioStream *s) { use(s); return s->xr; }
int32_t AAudioStream_getBufferSizeInFrames(AAudioStream *s) { use(s); return s->size; }
int32_t AAudioStream_setBufferSizeInFrames(AAudioStream *s, int32_t n) { use(s); s->size = n > s->cap ? s->cap : n; return s->size; }
int32_t AAudioStream_getBufferCapacityInFrames(AAudioStream *s) { use(s); return s->cap; }
int32_t AAudioStream_getFramesPerBurst(AAudioStream *s) { use(s); return s->burst; }
int32_t AAudioStream_getSampleRate(AAudioStream *s) { use(s); return 48000; }
aaudio_performance_mode_t AAudioStream_getPerformanceMode(AAudioStream *s) { use(s); return s->perf; }
const char *AAudio_convertResultToText(aaudio_result_t r) { (void) r; return "?"; }
const char *AAudio_convertStreamStateToText(aaudio_stream_state_t s) { (void) s; return "?"; }

/* ------------------------------------------------------------------ helpers */
static int g_fail;
#define CHECK(c, ...) do { if (!(c)) { g_fail++; printf("  FAIL line %d: ", __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)
static long long ms_now(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return (long long) t.tv_sec * 1000 + t.tv_nsec / 1000000; }
static AAudioStream *cur(void) { pthread_mutex_lock(&g_stream_lock); AAudioStream *s = g_stream; pthread_mutex_unlock(&g_stream_lock); return s; }

/* the frame thread for `ms`: a tick every 16 ms; the slowest tick in microseconds */
static long frames(int ms) {
    long worst = 0;
    long long end = ms_now() + ms;
    LARGE_INTEGER f, a, b;
    QueryPerformanceFrequency(&f);
    while (ms_now() < end) {
        QueryPerformanceCounter(&a);
        shim_audio_tick();
        QueryPerformanceCounter(&b);
        long us = (long) ((b.QuadPart - a.QuadPart) * 1000000 / f.QuadPart);
        if (us > worst) worst = us;
        Sleep(16);
    }
    return worst;
}

static DWORD WINAPI resume_thread(LPVOID p) { (void) p; shim_audio_pause(0); return 0; }

#ifdef AUDIO_OLD
int main(void) {
    setvbuf(stdout, NULL, _IONBF, 0);
    printf("the same storm through audio.c as released (1.71/1.72):\n");
    shim_alcOpenDevice(NULL);
    frames(500);
    long o0 = f_opens;
    f_route_gone = 1;
    cur()->state = AAUDIO_STREAM_STATE_DISCONNECTED;
    long worst = frames(12000);
    printf("  %ld opens in 12 s, the slowest tick %ld us\n", f_opens - o0, worst);
    f_route_gone = 0;
    frames(1500);
    shim_audio_pause(1);
    frames(200);
    f_wait_ms = 300;
    HANDLE h = CreateThread(NULL, 0, resume_thread, NULL, 0, NULL);
    Sleep(30);
    worst = frames(1500);
    WaitForSingleObject(h, INFINITE);
    printf("  the return from the background with 300 ms device waits: the slowest tick %ld us\n", worst);
    return 0;
}
#else
int main(void) {
    setvbuf(stdout, NULL, _IONBF, 0);
    printf("the sound stream's life, off the frame thread\n");
    shim_alcOpenDevice(NULL);
    CHECK(cur() && cur()->state == AAUDIO_STREAM_STATE_STARTED, "the game's open starts a stream");
    long worst = frames(500);
    CHECK(f_opens == 1 && worst < 3000, "quiet: one open, ticks at most %ld us", worst);

    printf("1. the route is gone: every new stream comes back DISCONNECTED, for 12 s\n");
    long o0 = f_opens;
    f_route_gone = 1;
    cur()->state = AAUDIO_STREAM_STATE_DISCONNECTED;
    worst = frames(12000);
    long storm = f_opens - o0;
    printf("  %ld opens in 12 s (each 80 ms, plus 30 ms to close); the slowest tick %ld us\n", storm, worst);
    CHECK(storm >= 4 && storm <= 9, "backed off: 0.5, 1, 2, 4, 8 s apart (%ld opens; 1.71 would open on every frame)", storm);
    CHECK(worst < 5000, "the frame thread never opened or closed (worst tick %ld us)", worst);
    f_route_gone = 0;
    worst = frames(9000);                    /* the backoff ends, the next rebuild finds the route */
    AAudioStream *s = cur();
    CHECK(s && s->state == AAUDIO_STREAM_STATE_STARTED, "the route back: a started stream");
    long o1 = f_opens;
    frames(12000);
    CHECK(f_opens == o1, "and it stays (%ld more opens)", f_opens - o1);
    pthread_mutex_lock(&g_stream_lock);
    long long back = g_life_backoff_ns;
    pthread_mutex_unlock(&g_stream_lock);
    printf("  after the storm: the backoff %lld ms\n", (long long) back / 1000000);

    printf("2. opens that fail: retried with the backoff, not every half second for ever\n");
    o0 = f_opens;
    f_fail_opens = 1000;
    pthread_mutex_lock(&g_stream_lock);
    g_stream_lost = 1;
    pthread_mutex_unlock(&g_stream_lock);
    worst = frames(10000);
    storm = f_opens - o0;
    CHECK(storm >= 4 && storm <= 8, "%ld opens in 10 s", storm);
    CHECK(worst < 5000, "worst tick %ld us", worst);
    f_fail_opens = 0;
    frames(9000);
    CHECK(cur() && cur()->state == AAUDIO_STREAM_STATE_STARTED, "the open succeeds once the system lets it");

    printf("3. the return from the background: the waits outside the lock, the frame never waits\n");
    f_wait_ms = 300;
    shim_audio_pause(1);
    frames(200);
    HANDLE h = CreateThread(NULL, 0, resume_thread, NULL, 0, NULL);
    Sleep(50);
    /* meanwhile the system drops the stream: the life thread must not close it under the waiting pause */
    pthread_mutex_lock(&g_stream_lock);
    g_stream_lost = 1;
    pthread_mutex_unlock(&g_stream_lock);
    life_ask();
    worst = frames(1500);
    WaitForSingleObject(h, INFINITE);
    CloseHandle(h);
    frames(1500);
    CHECK(worst < 5000, "worst tick during the 0.6 s of waits %ld us (1.71: the whole wait)", worst);
    CHECK(f_bad_use == 0, "no call on a closed stream (%ld)", f_bad_use);
    CHECK(g_life_deferred > 0, "the rebuild waited for the pause (%ld deferrals)", g_life_deferred);
    CHECK(cur() && cur()->state == AAUDIO_STREAM_STATE_STARTED, "a started stream after it");
    f_wait_ms = 0;

    printf("4. the buffer grows on underruns and shrinks back after quiet\n");
    s = cur();
    int open_size = s->size;
    for (int i = 0; i < 4; i++) { s->xr++; g_desk_skew_ns += 300000000LL; shim_audio_tick(); }
    int grown = s->size;
    CHECK(grown == open_size + 4 * f_burst, "grew to %d from %d", grown, open_size);
    g_desk_skew_ns += 16000000000LL; shim_audio_tick();
    CHECK(s->size == grown - f_burst, "15 s quiet: one burst back (%d)", s->size);
    s->xr++; g_desk_skew_ns += 300000000LL; shim_audio_tick();   /* an underrun right after the shrink */
    CHECK(s->size == grown, "grown again (%d)", s->size);
    g_desk_skew_ns += 16000000000LL; shim_audio_tick();
    CHECK(s->size == grown, "the next shrink waits longer now (%d)", s->size);
    g_desk_skew_ns += 16000000000LL; shim_audio_tick();
    CHECK(s->size == grown - f_burst, "30 s quiet: shrinks (%d)", s->size);
    for (int i = 0; i < 12; i++) { g_desk_skew_ns += 31000000000LL; shim_audio_tick(); }
    CHECK(s->size == open_size, "back to the size it opened with (%d)", s->size);
    CHECK(f_bad_use == 0, "no call on a closed stream (%ld)", f_bad_use);

    printf("5. the ordinary mixer: chosen after underruns at capacity, undone on the return\n");
    for (int k = 0; k < 4; k++) {
        s = cur();
        s->size = s->cap;                       /* nowhere left to grow */
        for (int i = 0; i < 25; i++) { s->xr++; g_desk_skew_ns += 300000000LL; shim_audio_tick(); }
        frames(700);
        s = cur();
        int none = s && s->perf == AAUDIO_PERFORMANCE_MODE_NONE;
        if (k < 4) CHECK(none, "round %d: on the ordinary mixer", k);
        shim_audio_pause(1);
        shim_audio_pause(0);
        frames(700);
        s = cur();
        int fast = s && s->perf == AAUDIO_PERFORMANCE_MODE_LOW_LATENCY;
        if (k < 3) CHECK(fast, "round %d: the return brings the fast mixer back", k);
        else CHECK(!fast, "round %d: three tries spent - the ordinary mixer stays", k);
        g_desk_skew_ns += 20000000000LL;
    }
    CHECK(f_bad_use == 0, "no call on a closed stream (%ld)", f_bad_use);
    printf("  opens %ld, closes %ld\n", f_opens, f_closes);
    printf(g_fail ? "FAILED: %d\n" : "all passed\n", g_fail);
    return g_fail ? 1 : 0;
}
#endif
