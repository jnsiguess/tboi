/*
 * Audio: an OpenAL layer on top of AAudio.
 *
 * The game asks the system for exactly twenty-four names — device, context,
 * buffers, sources, listener and the error code. Android has neither OpenAL
 * nor anything like it, so those twenty-four are written here.
 *
 * Decoding audio files is not our job: Vorbis is built into the game itself
 * (the image contains both "OggS" and "Xiph"), and what reaches us is finished
 * PCM through alBufferData.
 *
 * The design is simple and matches exactly what the game does:
 *   - short sounds play from a single buffer,
 *   - music runs as a queue of buffers the game refills and takes back,
 *   - pitch is the read step, volume is a multiplier.
 *
 * And where the game can tell, the answers are OpenAL's own, because the
 * game's streaming code was written against them (11.09.2026, "the sound
 * drops out and then piles on itself"): a stop makes every queued buffer
 * processed, a play that is not a resume starts from the head of the whole
 * queue, a buffer still attached to a source can be neither deleted nor
 * refilled, a name that is not a live one is refused, a buffer can be empty,
 * and the queue has no small limit. See "THE ANSWERS ARE OPENAL'S".
 *
 * Two defects of the game itself are put right here too (the owner,
 * 30.09.2026: "fix the original's pool and cache"), see "ONE OWNER PER SOURCE
 * AND PER BUFFER" below: the effects' pool of 64 sources, and the sound cache
 * of 500 buffers whose keys stopped matching what the buffers held.
 *
 * Mixing runs on the AAudio thread while the calls arrive from the game
 * thread, so everything shared sits under one lock. The lock is taken in short
 * stretches: holding it for a whole frame of mixing would make the sound
 * stutter.
 */
#include <stdint.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <pthread.h>
#include <android/log.h>
#include "diag.h"
#include <time.h>
#include <aaudio/AAudio.h>
#include <unistd.h>

#if ISAAC_DIAG
#define LOG(...) __android_log_print(ANDROID_LOG_INFO, "isaac", __VA_ARGS__)
#else
#define LOG(...) do { } while (0)      /* the build we play on has no log */
#endif

int shim_report_active(void);
void report_line(const char *kind, const char *text);
unsigned long shim_image_base(void);
void shim_hook(void *at, void *to);
int shim_trace_add(unsigned long off, const char *name);

/* ---------- the OpenAL names that appear in the game's code ---------- */

#define AL_NONE                 0
#define AL_FALSE                0
#define AL_TRUE                 1
#define AL_SOURCE_RELATIVE      0x0202
#define AL_PITCH                0x1003
#define AL_POSITION             0x1004
#define AL_DIRECTION            0x1005
#define AL_VELOCITY             0x1006
#define AL_LOOPING              0x1007
#define AL_BUFFER               0x1009
#define AL_GAIN                 0x100A
#define AL_MIN_GAIN             0x100D
#define AL_MAX_GAIN             0x100E
#define AL_SOURCE_STATE         0x1010
#define AL_INITIAL              0x1011
#define AL_PLAYING              0x1012
#define AL_PAUSED               0x1013
#define AL_STOPPED              0x1014
#define AL_BUFFERS_QUEUED       0x1015
#define AL_BUFFERS_PROCESSED    0x1016
#define AL_REFERENCE_DISTANCE   0x1020
#define AL_ROLLOFF_FACTOR       0x1021
#define AL_MAX_DISTANCE         0x1023
#define AL_SEC_OFFSET           0x1024
#define AL_SAMPLE_OFFSET        0x1025
#define AL_BYTE_OFFSET          0x1026

#define AL_FORMAT_MONO8         0x1100
#define AL_FORMAT_MONO16        0x1101
#define AL_FORMAT_STEREO8       0x1102
#define AL_FORMAT_STEREO16      0x1103

#define AL_NO_ERROR             0
#define AL_INVALID_NAME         0xA001
#define AL_INVALID_VALUE        0xA003
#define AL_INVALID_OPERATION    0xA004

/* ---------- the game's places this file works with ---------- */

#define SND_MANAGER     0xac5490    /* the sound manager: +0x18/+0x20 its objects, +0x88 cache, +0x90 pool */
#define GAME_MANAGER    0xac2428    /* [it] is the game; +0x204c0 its state, +0x55ea0 the effects' manager */
#define POOL_SIZE_WORD  0x8c91d4    /* movz w1, #0x40: the pool's size, in the manager's setup 0x8c9188 */
#define CACHE_BRANCH    0x8c95cc    /* b 0x8d09dc: the manager's "buffer for this sound" */
#define CACHE_TRAMPOLINE 0x8dab88   /* four dead words of the purchases check's body */
#define PURCHASES_CHECK 0x8dab10    /* own_all_dlc makes it mov w0, #1; ret - its body is dead then */
#define GAME_CACHE_GET  0x8d09dc    /* (cache, data, size, format, rate) -> buffer name */
#define FN_POOL_NEW     0x8c6684    /* (pool, size) */
#define FN_POOL_FREE    0x8c6708
#define FN_POOL_GET     0x8c67d4    /* (pool, sound object) -> source name or -1 */
#define FN_CACHE_NEW    0x8d088c    /* (cache, size) */
#define FN_CACHE_FREE   0x8d0910
#define FN_SND_PLAY     0x8a2eac    /* a sound object plays: stop checked, cache, pool, AL_BUFFER, play */
#define FN_SND_STOP     0x8a30e0    /* a sound object stops the source it last used */
#define FN_SND_PLAYING  0x8a3084    /* ... and asks whether that source plays */
#define FN_SFX_PLAY     0x7f06cc    /* the effects' manager plays a sound id (Repentance) */
#define FN_SFX_STOP_ALL 0x7f0d34
#define AT_PLAY_CALL    0x8a2f90    /* the return address of 0x8a2eac's alSourcePlay */
#define AT_RING_ASK     0x8c691c    /* ... of the pool ring's alGetSourcei(AL_SOURCE_STATE) */
#define AT_POUR_OVER    0x8d0b4c    /* ... of the game cache's alBufferData when it is full */
#define MENU_STATE      3           /* [game+0x204c0]: 3 menu, 1 loading a room, 2 a run */

/*
 * The pools grow on demand: real OpenAL has no limit, and we used to — 1024
 * buffers and 64 sources. Measured against sounds.xml: 1151 samples plus 97
 * music tracks at four buffers per stream. While sounds load lazily the limit
 * is not hit at once, but it is hit silently: alGenBuffers returns zero, the
 * music gets an empty buffer and stops. We also count what was handed out and
 * what was refused here, so "the sound disappeared" can be measured instead of
 * guessed at.
 */
#define BUF_START 1024
#define SRC_START 64
#define QUEUE_MAX 256              /* OpenAL has no limit; the video's audio queues by the hundred */

typedef struct {
    int used;
    int16_t *pcm;          /* always 16 bit: that is what the game's decoder gives */
    int frames, channels, rate;
    long freed_ns;         /* when the name was deleted; it rests before it is given again */
    const void *origin;    /* the address the sound was poured from (the report's WRONG SOUND) */
} albuf_t;

/*
 * A queue lives apart from its source. Effects never queue, and since every
 * effect has a source of its own (see ONE OWNER PER SOURCE) there are over a
 * thousand of them: two queues of 256 inside each would be four megabytes the
 * mixer walks past on every call.
 */
typedef struct {
    int queue[QUEUE_MAX];          /* a queue means music */
    int done[QUEUE_MAX];           /* played out: the game takes these back by name */
} srcq_t;

typedef struct {
    int used;
    int state;
    int looping;
    float gain, pitch;
    int buffer;                    /* a single buffer means a short sound */
    srcq_t *q;                     /* made on the first queueing; qn/dn count what is in it */
    int qn, dn;
    double pos;                    /* read position in frames of the current buffer */
    int starved;                   /* the queue ran dry: watch whether the game restarts it */
    long queued_ns;                /* when the game last queued a chunk (the report's dry watch) */
    int held_ms;                   /* how much sound the queue held right after that */
    long starved_ns;               /* when it ran dry */
    int feeder_tid;                /* the thread that last queued a chunk (the music watch) */
    float told_gain;               /* the gain the music watch last wrote down */
    long freed_ns;                 /* when the name was deleted; it rests before it is given again */
    long played_ns, paused_ns;     /* the census: when it was last played / paused */
    unsigned long play_from;       /* ... and from where in the game (image offset) */
    int census_told;               /* the census wrote this source down already */
    int unbound;                   /* the sound cache took its buffer while it stood stopped */
    unsigned long owner;           /* report: the sound object that last played in it */
#if ISAAC_DIAG
    /* report: the fate of the effect play now in it (see THE FATE OF A PLAY) */
    int m_live;                    /* an effect play whose end is not written down yet */
    int m_sounded;                 /* the mixer has read at least one frame of it */
    long m_play_ns;
    unsigned long m_from, m_from2; /* where the game played it from, and that caller's caller */
#endif
} alsrc_t;

static const char *state_name(int st);   /* defined with the report, used by the probes */

static albuf_t *g_buf; static int g_buf_cap;   /* name zero means "nothing" */
static alsrc_t *g_src; static int g_src_cap;

/* counters for the report: handed out, in use, and where we refused */
static long g_buf_used, g_buf_peak, g_buf_given, g_buf_fail, g_buf_bytes;
static long g_src_used, g_src_peak, g_src_given, g_src_fail;
static long g_dev_open, g_dev_close, g_ctx_new, g_ctx_del;
static long g_total_starves, g_total_lost_buffers, g_queue_overflows, g_stream_losses;
static long g_delete_refused;                 /* attached buffers the game tried to delete */
static long g_data_refused;                   /* attached buffers the game tried to refill */
static long g_dev_open_ns;                    /* when the device was first opened */
static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;

/*
 * A NAME IS CHECKED WITHOUT A SIGN (30.09.2026).
 *
 * When the game's pool had no free source it handed out -1 (0x8c6934) and the
 * game went on to play on it anyway. Real OpenAL answers AL_INVALID_NAME to
 * such a name and does nothing. We compared the name as a signed number: -1
 * was "below the size of the list", and the layer read - and in the deletes
 * wrote - nine terabytes past the start of the array. That was a crash, in
 * the build we play on as well. Every entry now asks these two.
 */
static int src_name_ok(unsigned id) { return id != 0 && id < (unsigned) g_src_cap; }
static int buf_name_ok(unsigned id) { return id != 0 && id < (unsigned) g_buf_cap; }
/* under the lock */
static int buf_live(int id) { return buf_name_ok((unsigned) id) && g_buf[id].used; }

static unsigned long img_off(unsigned long a) {
    unsigned long b = shim_image_base();
    return (b && a > b) ? a - b : 0;
}

/*
 * THE ERROR CODE IS THE CALLING THREAD'S OWN (28.09.2026), AND THE FIRST ONE
 * HOLDS (30.09.2026).
 *
 * The game checks alGetError right after a call and acts on it: opening a
 * stream (0x8aa870) makes its source (0x8c9528) and its four buffers
 * (0x8c9578), and gives the stream up if the error read after either is not
 * zero. The music is fed from the game's own audio thread (0x8c931c, every
 * 20 ms) while the frame's thread plays effects and opens streams, and with
 * ONE code for everybody an error one thread raised was read by the other:
 * a refused unqueue on the audio thread made the frame's thread believe its
 * fresh source had failed, and that track or long sound never started. Each
 * thread now reads only what its own calls raised, which is what "call, then
 * check" means.
 *
 * That part departs from Apple, whose code is one for everybody (oalImp.cpp,
 * gCurrentError). What Apple does keep is the FIRST error until it is read
 * (oalImp.cpp:267, "set it only if it is empty") - and so do we now. Whether
 * the per-thread code should go back to Apple's shared one is decided by the
 * report: the diagnostic build keeps Apple's model alongside, in the shadow,
 * and writes down every read where the two would differ.
 */
static __thread int g_error = AL_NO_ERROR;

#if ISAAC_DIAG
static void shadow_set(int e, unsigned long from);
static void shadow_read(int mine, unsigned long from);
#endif

static void set_error_at(int e, unsigned long from) {
    if (g_error == AL_NO_ERROR) g_error = e;
#if ISAAC_DIAG
    shadow_set(e, from);
#else
    (void) from;
#endif
}
/* inside an al* entry: the address is the game's call */
#define SET_ERROR(e) set_error_at((e), (unsigned long) __builtin_return_address(0))

static AAudioStream *g_stream;
static int g_rate = 48000;

/*
 * THE STREAM'S LIFE, and the two ways it used to end in silence for good
 * (the testers, 28.09.2026: "sounds drop out, one kind after another, and
 * never come back"):
 *   - the system drops the stream (a route change, the audio server
 *     restarting) and the stream opened in its place FAILS to open - the
 *     server is still coming back - and nobody ever tried again: g_stream
 *     stayed empty and every call returned early;
 *   - the stream stays in its started state and simply stops asking for data
 *     (no error callback, no reopen): the sources sit in PLAYING for ever, the
 *     game's pool of sources hands out nothing new, and every later sound is
 *     dropped while the pool says its sources are busy.
 * So the open is retried every half second while there is no stream, the
 * mixer stamps every call, and a started stream that has not called for two
 * seconds while sounds are playing is rebuilt. The lifecycle sits under its
 * own lock: the pause comes from the UI thread, the tick from the frame's,
 * and the two used to race on the same pointer.
 *
 * Every rebuild by the watchdog is written into the report as an ALARM, not as
 * a cure: it means AAudio fell silent without saying so, and why is the
 * question.
 */
static pthread_mutex_t g_stream_lock = PTHREAD_MUTEX_INITIALIZER;
static int g_paused;                    /* asked to be quiet: background, a call */
static volatile long g_mix_ns;          /* when the mixer was last called */
static long g_open_fail, g_open_retry, g_watchdog_rebuilds, g_last_open_try_ns;
static long g_buffer_grown, g_silent_disconnects;   /* see THE BUFFER GROWS ON XRUNS, A DISCONNECT NOBODY TOLD */
#define OPEN_RETRY_NS   (500L * 1000L * 1000L)
#define WATCHDOG_NS     (2000L * 1000L * 1000L)

/*
 * THE STREAM'S LIFE OFF THE FRAME THREAD (hotfix 1.72).
 *
 * Closing a stream and opening another is tens to hundreds of milliseconds
 * inside AAudio, and it used to happen on the frame thread, under the
 * stream's lock, in the middle of a frame: every lost stream was a hitch. Now
 * the frame thread only notices (shim_audio_tick, which takes the lock with a
 * trylock and skips the frame's look when somebody else holds it) and asks;
 * a thread of its own (life_main) closes and opens, under the same lock as
 * before - g_stream changes hands only there and in alcOpenDevice, the
 * game's first open. Back-to-back rebuilds and failed opens back off: half a
 * second, then twice as long each time, eight seconds at most; a stream that
 * lived ten seconds resets it.
 *
 * A stream somebody uses OUTSIDE the lock is pinned (g_stream_pins, under the
 * lock): shim_audio_pause waits for the device's state changes - up to 0.6 s -
 * without holding the lock, and the life thread does not close a pinned
 * stream; it looks again 50 ms later.
 */
static int g_stream_pins;                    /* under g_stream_lock */
static long g_disc_ns;                       /* under g_stream_lock: since when the stream has said DISCONNECTED */
static long g_opened_ns;                     /* under g_stream_lock: when the current stream opened */
static pthread_mutex_t g_life_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t g_life_cond = PTHREAD_COND_INITIALIZER;
static int g_life_asked, g_life_up;          /* under g_life_lock */
static volatile long g_life_next_ns;         /* not before this: the backoff */
static long g_life_backoff_ns;               /* the life thread's own */
static long g_life_rebuilds, g_life_deferred;
#define LIFE_BACKOFF_MIN_NS (500L * 1000L * 1000L)
#define LIFE_BACKOFF_MAX_NS (8000L * 1000L * 1000L)
#define LIFE_LIVED_NS       (10000L * 1000L * 1000L)
static void life_ask(void);

/* ---------- the report's sound lines: one budget of 200 for all of them ---------- */

#if ISAAC_DIAG
#define SND_BUDGET  200
#define SND_RESERVE 30                  /* kept back for the self-test and the summaries */
static int g_snd_lines;

static void snd_emit(int reserved, const char *kind, const char *fmt, va_list ap) {
    if (!shim_report_active()) return;
    if (g_snd_lines >= (reserved ? SND_BUDGET : SND_BUDGET - SND_RESERVE)) return;
    g_snd_lines++;
    char text[520];
    vsnprintf(text, sizeof(text), fmt, ap);
    report_line(kind, text);
}

__attribute__((format(printf, 1, 2)))
static void snd_line(const char *fmt, ...) {
    va_list ap; va_start(ap, fmt); snd_emit(0, "audio", fmt, ap); va_end(ap);
}

__attribute__((format(printf, 1, 2)))
static void snd_summary(const char *fmt, ...) {
    va_list ap; va_start(ap, fmt); snd_emit(1, "audio", fmt, ap); va_end(ap);
}

__attribute__((format(printf, 1, 2)))
static void st_line(const char *fmt, ...) {
    va_list ap; va_start(ap, fmt); snd_emit(1, "audio selftest", fmt, ap); va_end(ap);
}

/* the stream's events into the report */
#define AUDIO_SAY(...) snd_line(__VA_ARGS__)
#else
#define AUDIO_SAY(...) do { } while (0)
#endif

/*
 * THE SOUND METER (report builds, 05.10.2026).
 *
 * Players lose ALL sound at once - music and effects - and get it back only
 * when something new starts (the owner's 23rd pass: from Dogma's death to
 * Mother's). The report said what the game asked for, but not whether
 * anything reached the speaker. So the mixer measures what it actually wrote
 * - the peak and the RMS of every second of output, how many voices, how loud
 * their data was before and after their gain - and the frame's thread puts
 * that next to the stream's state, the sources' gains, the game's own volume
 * settings and what the game asked for in that second. A line goes out when
 * something in that picture changes, and every second while the output is
 * silent.
 *
 * The signature that matters is SILENCE: the output stays below -80 dBFS for
 * a second or more while sources play or the game asks for sounds. Its line
 * names the broken link - no stream, a mixer nobody calls, no source playing,
 * every gain at zero, sources whose data is silent - with the playing sources
 * and the last calls that turned something down, paused or stopped it (a ring
 * of 64, with the game's call site and its caller's, as image offsets).
 *
 * The effective gain of a source here is its own gain and nothing else: the
 * layer applies no listener or master volume (the game imports no
 * alListenerf, and an AL_GAIN on the listener would be written down as 'L').
 *
 * Nothing here changes what is heard; the build we play on has none of it.
 */
#if ISAAC_DIAG
#define METER_SILENT    1e-4f          /* -80 dBFS: three steps of sixteen bits */
#define METER_GAIN_EPS  0.001f         /* -60 dB: a gain nobody hears */
#define METER_RING      64

typedef struct {
    long ns;
    char kind;              /* g gain to ~0, G gain up from ~0, p pause, s stop and d delete of a sounding
                               source, P a long sound played, y a queue ran dry, L listener gain,
                               A/a the app paused/resumed the stream, R the stream rebuilt */
    char music;
    int src, tid;
    float was, now;
    unsigned long from, from2;          /* image offsets: the game's call, and its caller */
} mev_t;

static mev_t g_mring[METER_RING];
static long g_mring_n;                  /* under g_lock */
static float g_listener_gain = 1.0f;
static long g_m_plays, g_m_eplays;      /* since the last window, under g_lock */

static long now_ns(void);
static int is_music(const alsrc_t *s);
static int census_off(void);
static void meter_mix(int32_t frames, float peak, double sq, float raw, float out, float gmax, int voices,
                      long gap, long wait, long spent);
static void meter_tick(void);

/* under g_lock */
static void meter_event(char kind, int src, float was, float now, unsigned long from, unsigned long from2) {
    if (!shim_report_active() || census_off()) return;
    mev_t *e = &g_mring[g_mring_n++ % METER_RING];
    e->ns = now_ns();
    e->kind = kind;
    e->src = src;
    e->tid = gettid();
    e->was = was;
    e->now = now;
    e->music = (src_name_ok((unsigned) src) && g_src[src].used && is_music(&g_src[src])) ? 1 : 0;
    e->from = img_off(from);
    e->from2 = img_off(from2);
}

/*
 * Inside an al* entry: the game's call, and the call of the game's function
 * one level up. Our frame record holds the game's frame pointer, and Apple's
 * ABI keeps the game's frame record valid; it is checked to lie just above
 * ours on the same stack before it is read.
 */
#define METER_FROM(f0, f1) do { \
    (f0) = (unsigned long) __builtin_return_address(0); \
    const unsigned long *fp_ = (const unsigned long *) __builtin_frame_address(0); \
    const unsigned long *up_ = fp_ ? (const unsigned long *) fp_[0] : NULL; \
    (f1) = (up_ > fp_ && (unsigned long) up_ - (unsigned long) fp_ < (1UL << 20) \
            && !((unsigned long) up_ & 7)) ? up_[1] : 0; \
} while (0)

/*
 * THE FATE OF A PLAY (report builds, 05.10.2026).
 *
 * Players say single sounds drop out in places - on their phones, never on the
 * owner's, and we cannot read their reports. A sound that "drops out" can only
 * have ended in one of these ways, so every effect play is followed to its end
 * and the end is written down:
 *   - played to the end of its sound (or looped until stopped);
 *   - CUT SHORT: the game stopped, replayed or deleted it while it sounded -
 *     the call site and its caller are kept, so the report says which game
 *     code cuts sounds and how often;
 *   - UNHEARD: the game stopped or replayed it before the mixer had read a
 *     single frame of it. The mixer reads in bursts whose size is the phone's
 *     (2 ms on the owner's, tens of ms on a Bluetooth or a non-fast route), so
 *     the same game code that is heard on one phone is not on another. The
 *     line carries the burst at that moment;
 *   - LOST: the layer itself stopped it because its buffer was gone - this
 *     must stay zero.
 * Music and long sounds have their own watch and are not counted here.
 */
#define FATE_UNHEARD_RING 16
#define FATE_CUTTERS      12

typedef struct { int src, buffer, len_ms, how; long play_ns, end_ns; unsigned long from, from2, pfrom; } fate_ev_t;

static long g_fate_plays, g_fate_end, g_fate_cut, g_fate_unheard, g_fate_lost;   /* under g_lock */
static long g_fate_w_plays, g_fate_w_cut, g_fate_w_unheard;                     /* since the last meter second */
static fate_ev_t g_fate_ring[FATE_UNHEARD_RING];
static long g_fate_ring_n, g_fate_ring_told;
static struct { unsigned long from, from2; long n; int told; } g_cutters[FATE_CUTTERS];
static volatile int32_t g_burst_now;    /* the last callback's frames, for the UNHEARD lines */
static volatile int g_fate_quiet;       /* the self-test's live stress plays: not the players' sounds */

/* under g_lock: the play in s ends by the game's hand ('s' stop, 'r' replay, 'd' delete) */
static void fate_close(alsrc_t *s, char how, unsigned long from, unsigned long from2) {
    if (!s->m_live) return;
    s->m_live = 0;
    if (s->state != AL_PLAYING && s->state != AL_PAUSED) return;   /* it had ended already */
    from = img_off(from); from2 = img_off(from2);
    if (!s->m_sounded) {
        g_fate_unheard++; g_fate_w_unheard++;
        fate_ev_t *e = &g_fate_ring[g_fate_ring_n++ % FATE_UNHEARD_RING];
        e->src = (int) (s - g_src);
        e->buffer = s->buffer;
        albuf_t *b = buf_live(s->buffer) ? &g_buf[s->buffer] : NULL;
        e->len_ms = (b && b->rate > 0) ? (int) ((long) b->frames * 1000L / b->rate) : -1;
        e->how = how;
        e->play_ns = s->m_play_ns;
        e->end_ns = now_ns();
        e->from = from; e->from2 = from2;
        e->pfrom = s->m_from;
        return;
    }
    g_fate_cut++; g_fate_w_cut++;
    for (int i = 0; i < FATE_CUTTERS; i++) {
        if (g_cutters[i].n && (g_cutters[i].from != from || g_cutters[i].from2 != from2)) continue;
        g_cutters[i].from = from; g_cutters[i].from2 = from2;
        g_cutters[i].n++;
        break;
    }
}

/* under g_lock: a new effect play begins in s (not a resume) */
static void fate_open(alsrc_t *s, unsigned long from, unsigned long from2) {
    s->m_live = !is_music(s) && !census_off() && !g_fate_quiet;
    s->m_sounded = 0;
    s->m_play_ns = now_ns();
    s->m_from = img_off(from);
    s->m_from2 = img_off(from2);
    if (s->m_live) { g_fate_plays++; g_fate_w_plays++; }
}

/* under g_lock, the mixer: the sound ended by itself */
static void fate_end(alsrc_t *s) { if (s->m_live) { s->m_live = 0; g_fate_end++; } }

/* under g_lock, the mixer: the layer stopped it - its buffer was gone, or it was played with none */
static long g_fate_empty;
static void fate_lost(alsrc_t *s) {
    if (!s->m_live) return;
    s->m_live = 0;
    if (!s->buffer && !s->qn) g_fate_empty++;
    else g_fate_lost++;
}
#endif

/* ---------- mixing ---------- */

/* which buffer the source is reading right now */
static albuf_t *current_of(alsrc_t *s) {
    int id = s->qn > 0 ? s->q->queue[0] : s->buffer;
    return buf_live(id) ? &g_buf[id] : NULL;   /* pcm may be NULL: an empty buffer, played through in no time */
}

/*
 * The current buffer is finished: the queue moves on, a single buffer either
 * loops or stops.
 *
 * The position carries over with its REMAINDER instead of resetting to zero.
 * Resetting lost the fractional part at every seam between buffers, and at any
 * pitch other than one that is audible as a click: the sound jumps in phase.
 * At a pitch of exactly one the remainder is zero anyway, so nothing gets
 * worse anywhere.
 */
static long g_seams, g_starves, g_buffer_switches;   /* measured: seams, dry queue, buffer swaps */

#if ISAAC_DIAG
static int g_starve_lines;
static int g_peak_voices;                     /* the census: most voices mixed at once */
static long g_mix_worst_ns, g_mix_budget_ns, g_clipped_total;
#endif

/*
 * MEASUREMENT, for the report: a music queue that ran dry.
 *
 * A tester (11.09.2026): in a cutscene - one started from the menu as well -
 * the sound drops out for about a second and then the sounds pile on each
 * other. A queue the game did not refill in time does exactly that, and so the
 * report is told, for each dry queue, how much sound the queue held at the
 * last refill, how long ago that refill was, and how long the game took to
 * start the source again. Recorded on the audio thread into a small ring,
 * written from the game thread (shim_audio_tick). Nothing here changes what
 * is heard.
 */
#define DRY_MAX 16
static struct { int src, held_ms; long since_refill_ms, at_ns, restart_ms; } g_dry[DRY_MAX];
static int g_dry_n;
static long g_dry_total;

static long now_ns(void);

static int held_ms_of(alsrc_t *s) {
    double ms = 0.0;
    for (int i = 0; i < s->qn; i++) {
        int id = s->q->queue[i];
        if (!buf_live(id) || g_buf[id].rate <= 0) continue;
        double frames = (double) g_buf[id].frames - (i == 0 ? s->pos : 0.0);
        if (frames > 0.0) ms += frames * 1000.0 / (double) g_buf[id].rate;
    }
    return (int) (ms / (s->pitch > 0.0f ? (double) s->pitch : 1.0));
}

/* under the lock */
static void note_dry(alsrc_t *s) {
    long now = now_ns();
    s->starved_ns = now;
    g_dry_total++;
    DIAG(meter_event('y', (int) (s - g_src), s->gain, 0.0f, 0, 0));
    if (g_dry_n >= DRY_MAX) return;
    g_dry[g_dry_n].src = (int) (s - g_src);
    g_dry[g_dry_n].held_ms = s->held_ms;
    g_dry[g_dry_n].since_refill_ms = s->queued_ns ? (now - s->queued_ns) / 1000000L : -1;
    g_dry[g_dry_n].at_ns = now;
    g_dry[g_dry_n].restart_ms = -1;
    g_dry_n++;
}

static void advance(alsrc_t *s, long frames_done) {
    double leftover = s->pos - (double) frames_done;
    if (leftover < 0.0) leftover = 0.0;
    if (leftover > 0.001) g_seams++;
    g_buffer_switches++;
    if (s->qn > 0) {
        srcq_t *q = s->q;
        if (s->dn < QUEUE_MAX) q->done[s->dn++] = q->queue[0];
        for (int i = 1; i < s->qn; i++) q->queue[i - 1] = q->queue[i];
        s->qn--;
        s->pos = leftover;
        /* the queue ran dry: the game did not queue the next chunk in time */
        if (s->qn == 0) {
            s->state = AL_STOPPED; g_starves++; g_total_starves++; s->starved = 1;
            note_dry(s);
#if ISAAC_DIAG
            if (g_starve_lines++ < 100)
                LOG("audio: source %ld ran out of queue — stopped (pitch %.3f, played out %d)",
                    (long) (s - g_src), (double) s->pitch, s->dn);
#endif
        }
        return;
    }
    if (s->looping && frames_done > 0) { s->pos = leftover; return; }
    s->pos = 0;
    s->state = AL_STOPPED;
    DIAG(fate_end(s));
}

/*
 * MEASUREMENT: where the clicking comes from.
 *
 * A click has two flavours and they are cured differently:
 *   - the sum of the voices went past one and the limiter below clipped it;
 *   - the mixer was late and the audio subsystem repeated the old data.
 * So we count both: how many voices were sounding, what the peak was before
 * clipping, how many samples were clipped. Written once every two seconds of
 * audio time so as not to flood the log from the audio thread.
 */
static long now_ns(void) {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (long) t.tv_sec * 1000000000L + t.tv_nsec;
}

#if ISAAC_DIAG
static void watch_output(int voices, float peak, long clipped, int32_t frames,
                         long spent, long gap, float jump, long jumps,
                         int32_t at_sample, int voices_before) {
    static int32_t worst_at; static int voices_then, voices_after_then;
    static long accumulated, total_clipped, max_voices;
    static long longest_mix, longest_gap, late_calls, total_jumps;
    static float max_peak, max_jump;
    total_jumps += jumps;
    if (jump > max_jump) {
        max_jump = jump; worst_at = at_sample;
        voices_then = voices_before; voices_after_then = voices;
    }
    long budget = (long) frames * 1000000000L / (g_rate > 0 ? g_rate : 48000);
    accumulated += frames;
    total_clipped += clipped;
    if (peak > max_peak) max_peak = peak;
    if (voices > max_voices) max_voices = voices;
    if (spent > longest_mix) longest_mix = spent;
    if (gap > longest_gap) longest_gap = gap;
    if (gap > budget * 3 / 2) late_calls++;
    /* the census keeps its own maxima for the whole session */
    g_clipped_total += clipped;
    if (voices > g_peak_voices) g_peak_voices = voices;
    if (spent > g_mix_worst_ns) { g_mix_worst_ns = spent; g_mix_budget_ns = budget; }
    if (accumulated < 96000) return;
    /*
     * We stay quiet while all is well. A click is a discontinuity at the
     * output, so that is what we watch for, along with clipping, a late mixer
     * and a dry queue. The 0.25 threshold comes from the measurement: a real
     * click jumped by 0.38..0.87, clean sound by 0.08..0.13.
     */
    int bad = total_jumps || total_clipped || late_calls || g_starves;
    if (!bad) {
        accumulated = 0; total_clipped = 0; max_peak = 0.0f; max_voices = 0;
        longest_mix = 0; longest_gap = 0; late_calls = 0;
        g_seams = 0; g_starves = 0; g_buffer_switches = 0;
        max_jump = 0.0f; total_jumps = 0;
        return;
    }
    LOG("audio: up to %ld voices, peak %.2f, clipped %ld; mixing up to %ld us against a %ld us budget, "
        "longest gap %ld us, late %ld; seams with remainder %ld, dry queues %ld; "
        "buffer swaps %ld; largest jump %.3f at sample %d of %d (voices were %d, became %d), "
        "jumps above 0.25: %ld",
        max_voices, (double) max_peak, total_clipped,
        longest_mix / 1000, budget / 1000, longest_gap / 1000, late_calls,
        g_seams, g_starves, g_buffer_switches, (double) max_jump, worst_at, frames,
        voices_then, voices_after_then, total_jumps);
    g_seams = 0; g_starves = 0; g_buffer_switches = 0;
    max_jump = 0.0f; total_jumps = 0;
    accumulated = 0; total_clipped = 0; max_peak = 0.0f; max_voices = 0;
    longest_mix = 0; longest_gap = 0; late_calls = 0;
}
#endif

static aaudio_data_callback_result_t mix(AAudioStream *st, void *user, void *out, int32_t frames) {
    (void) st; (void) user;
    float *dst = (float *) out;
    memset(dst, 0, sizeof(float) * 2 * (size_t) frames);
    g_mix_ns = now_ns();                 /* the watchdog in shim_audio_tick */

#if ISAAC_DIAG
    static long prev_entry;
    static AAudioStream *prev_stream;
    long entered = now_ns();
    if (st != prev_stream) { prev_stream = st; prev_entry = 0; }   /* a new stream: no gap to the old one */
    long gap = prev_entry ? entered - prev_entry : 0;
    prev_entry = entered;
    g_burst_now = frames;

    static int voices_prev;
    int voices_before = voices_prev;
#endif
    int voices = 0;
#if ISAAC_DIAG
    float m_raw = 0.0f, m_out = 0.0f, m_gain = 0.0f;   /* the meter: the loudest voice before / after its gain */
    long m_wait_from = now_ns();                       /* the meter: how long the lock kept the mixer waiting */
#endif
    pthread_mutex_lock(&g_lock);
#if ISAAC_DIAG
    long m_wait = now_ns() - m_wait_from;
#endif
    for (int i = 1; i < g_src_cap; i++) {
        alsrc_t *s = &g_src[i];
        if (!s->used || s->state != AL_PLAYING) continue;
        voices++;
        float gain = s->gain;
        double step = (double) s->pitch;
#if ISAAC_DIAG
        float vpk = 0.0f;
#endif
        int32_t f = 0;
        for (; f < frames; f++) {
            albuf_t *b = current_of(s);
            if (!b) { s->state = AL_STOPPED; g_starves++; g_total_lost_buffers++; DIAG(fate_lost(s)); break; }
            /*
             * Finished buffers are wound forward RIGHT HERE, without spending
             * an output sample on it. There used to be a continue here: at
             * every seam between buffers a voice skipped one sample and fell
             * to zero. The measurement showed exactly that — twelve buffer
             * swaps in two seconds, four to six of them with an output jump of
             * up to 0.87. That was the click.
             */
            while (!b->pcm || (long) s->pos >= b->frames) {
                advance(s, b->frames);
                if (s->state != AL_PLAYING) break;
                b = current_of(s);
                if (!b) { s->state = AL_STOPPED; g_starves++; g_total_lost_buffers++; DIAG(fate_lost(s)); break; }
            }
            if (s->state != AL_PLAYING || !b) break;
            double rate_step = step * (double) b->rate / (double) g_rate;
            long idx = (long) s->pos;
            /*
             * Between samples we interpolate linearly: without it the pitch is
             * audible as steps, and the game changes pitch constantly.
             */
            double frac = s->pos - (double) idx;
            long nxt = (idx + 1 < b->frames) ? idx + 1 : idx;
            float l, r;
            if (b->channels == 2) {
                float l0 = b->pcm[idx * 2], l1 = b->pcm[nxt * 2];
                float r0 = b->pcm[idx * 2 + 1], r1 = b->pcm[nxt * 2 + 1];
                l = (float) (l0 + (l1 - l0) * frac);
                r = (float) (r0 + (r1 - r0) * frac);
            } else {
                float v0 = b->pcm[idx], v1 = b->pcm[nxt];
                l = r = (float) (v0 + (v1 - v0) * frac);
            }
            dst[f * 2] += l * gain / 32768.0f;
            dst[f * 2 + 1] += r * gain / 32768.0f;
            s->pos += rate_step;
#if ISAAC_DIAG
            float al = l < 0.0f ? -l : l, ar = r < 0.0f ? -r : r;
            if (al > vpk) vpk = al;
            if (ar > vpk) vpk = ar;
#endif
        }
#if ISAAC_DIAG
        if (vpk > m_raw) m_raw = vpk;
        if (vpk * gain > m_out) m_out = vpk * gain;
        if (gain > m_gain) m_gain = gain;
        if (f > 0) s->m_sounded = 1;           /* at least one frame of this play reached the output */
#else
        (void) f;
#endif
    }
    pthread_mutex_unlock(&g_lock);

    /* the sum of several sources runs past the edges — clip at the edge */
    float peak = 0.0f;
    long clipped = 0;
#if ISAAC_DIAG
    double sq = 0.0;                     /* the meter's RMS, of what is written (before the clip) */
#endif
    for (int32_t f = 0; f < frames * 2; f++) {
        float v = dst[f] < 0.0f ? -dst[f] : dst[f];
        if (v > peak) peak = v;
#if ISAAC_DIAG
        sq += (double) v * (double) v;
#endif
        if (dst[f] > 1.0f) { dst[f] = 1.0f; clipped++; }
        else if (dst[f] < -1.0f) { dst[f] = -1.0f; clipped++; }
    }

    /*
     * A click IS a discontinuity: a jump between neighbouring samples. We
     * measure it right at the output, across the call boundary too. It is a
     * per-sample pass, so the build we play on does not have it at all —
     * there is nothing left to watch for there.
     */
#if ISAAC_DIAG
    static float prev_left, prev_right;
    float jump = 0.0f;
    long jumps = 0;
    int32_t at_sample = -1;
    for (int32_t f = 0; f < frames; f++) {
        float dl = dst[f * 2] - prev_left, dr = dst[f * 2 + 1] - prev_right;
        if (dl < 0.0f) dl = -dl;
        if (dr < 0.0f) dr = -dr;
        float d = dl > dr ? dl : dr;
        if (d > jump) { jump = d; at_sample = f; }
        if (d > 0.25f) jumps++;
        prev_left = dst[f * 2];
        prev_right = dst[f * 2 + 1];
    }
    DIAG(voices_prev = voices);
    watch_output(voices, peak, clipped, frames, now_ns() - entered, gap, jump, jumps,
                 at_sample, voices_before);
    meter_mix(frames, peak, sq, m_raw / 32768.0f, m_out / 32768.0f, m_gain, voices, gap, m_wait,
              now_ns() - entered);
#else
    (void) voices; (void) peak; (void) clipped;
#endif
    return AAUDIO_CALLBACK_RESULT_CONTINUE;
}

/*
 * The system disconnects the stream on its own — when the audio route changes,
 * when the audio server restarts. Without a handler that is silence forever:
 * the stream is stopped, g_stream is not empty, and nobody reopens anything.
 * Closing the stream from inside its own callback is not allowed, so there is
 * only a flag here, and the reopen happens on the game thread once a frame
 * (shim_audio_tick).
 */
static volatile int g_stream_lost;

static void on_stream_error(AAudioStream *st, void *user, aaudio_result_t error) {
    (void) st; (void) user;
    LOG("audio: the system disconnected the AAudio stream (%d: %s)",
        (int) error, AAudio_convertResultToText(error));
    g_stream_lost = 1;
}

/*
 * THE BUFFER GROWS ON XRUNS (05.10.2026).
 *
 * How much sound the stream holds ahead of the speaker is the phone's choice
 * at open, and it differs from phone to phone: half the capacity on an MMAP
 * stream (AOSP AudioStreamInternal: "Default buffer size to match Q"), the
 * AudioTrack's own size on the older path. When the mixer is late by more than
 * that - a busy CPU, a slow core, the thermal governor, a lock - the device
 * plays a gap: an underrun, which AAudio counts. Android's own advice is to
 * watch that count and raise the buffer by a burst each time it grows
 * (developer.android.com/games/sdk/oboe/low-latency-audio; Oboe's
 * LatencyTuner does exactly this). A phone that never underruns - the owner's
 * reports show zero - keeps the latency it had; one that does trades a few
 * milliseconds for a gapless output. Checked four times a second from the
 * frame's thread, under the stream's lock.
 */
static int32_t g_xr_seen;
static long g_xr_check_ns;
#define XRUN_CHECK_NS (250L * 1000L * 1000L)

/*
 * AND SHRINKS BACK (hotfix 1.72). Grown buffer used to stay grown for the
 * whole process: a phone that underran during one heavy room kept the extra
 * latency for the rest of the session - the sound late behind the picture.
 * After fifteen seconds without an underrun the buffer gives back one burst,
 * and again after each further fifteen quiet seconds, down to the size the
 * stream opened with. An underrun that comes within fifteen seconds of a
 * shrink grows it again as before and doubles the quiet time asked before the
 * next shrink (two minutes at most): a phone that really needs the room keeps
 * it, a passing load does not cost latency for good.
 */
static int32_t g_size_open;                     /* the buffer the stream opened with */
static long g_xr_quiet_ns;                      /* since the last underrun or the last shrink */
static long g_shrunk_ns;                        /* the last shrink */
static long g_shrink_wait_ns;
static long g_buffer_shrunk;
#define SHRINK_QUIET_NS (15000L * 1000L * 1000L)
#define SHRINK_MAX_NS   (120000L * 1000L * 1000L)

/*
 * THE LAST RESORT: THE ORDINARY MIXER (05.10.2026, the owner: "as a fallback
 * for the extreme cases, if it is clean").
 *
 * A fast-path stream whose buffer is already as large as the device allows
 * and still underruns has nowhere left to grow: the phone cannot feed the
 * fast mixer in time, whatever we do. Android's ordinary mixer
 * (PERFORMANCE_MODE_NONE) sits behind a far larger buffer and wakes far less
 * often - tens of milliseconds more latency, which this game does not feel,
 * against a gapless output. So when the fast stream has underrun at its
 * capacity for five seconds, ten underruns or more, with no quiet gap of ten
 * seconds in between, it is rebuilt ONCE in this session with the ordinary
 * mixer and everything else the same; the sources do not notice. A phone that
 * never fills its buffer - the owner's - never gets here; a stream that did
 * not get the fast path in the first place is not touched; and if the
 * ordinary stream will not open, the open goes back to the fast one.
 */
static aaudio_performance_mode_t g_perf = AAUDIO_PERFORMANCE_MODE_LOW_LATENCY;
static int g_perf_fallback;                     /* in force: the ordinary mixer was chosen for this phone */
/*
 * AND IS UNDONE (hotfix 1.72). The ordinary mixer used to stay for the rest
 * of the process, with its 40-100 ms of latency. The next process starts with
 * the fast mixer anyway (g_perf's initial value); within one, the fast mixer
 * is tried again when the app comes back from the background (shim_audio_pause):
 * the sound is held there, so the rebuild is not heard, and the load that
 * made it underrun - heat, a busy phone - has often passed. Three times a
 * session at most; a phone that falls back each time stays on the ordinary
 * mixer.
 */
static int g_perf_reverts;
#define PERF_REVERTS_MAX 3
static long g_cap_ns, g_cap_last_ns, g_cap_xr;  /* underruns with no room left to grow */
#define CAP_WINDOW_NS  (5000L * 1000L * 1000L)
#define CAP_QUIET_NS   (10000L * 1000L * 1000L)
#define CAP_XRUNS      10

static void xrun_watch(long now) {
    if (now - g_xr_check_ns < XRUN_CHECK_NS) return;
    g_xr_check_ns = now;
    int32_t xr = AAudioStream_getXRunCount(g_stream);
    if (xr < 0) return;                         /* this stream cannot count them */
    if (xr > g_xr_seen) {
        /* an underrun soon after a shrink: the room was needed - wait longer before the next one */
        if (g_shrunk_ns && now - g_shrunk_ns < SHRINK_QUIET_NS) {
            g_shrink_wait_ns *= 2;
            if (g_shrink_wait_ns > SHRINK_MAX_NS) g_shrink_wait_ns = SHRINK_MAX_NS;
        }
        g_shrunk_ns = 0;
        g_xr_quiet_ns = now;
        int32_t size = AAudioStream_getBufferSizeInFrames(g_stream);
        int32_t cap = AAudioStream_getBufferCapacityInFrames(g_stream);
        int32_t burst = AAudioStream_getFramesPerBurst(g_stream);
        int32_t got = size;
        if (size > 0 && burst > 0 && cap > 0 && size + burst <= cap) {
            got = AAudioStream_setBufferSizeInFrames(g_stream, size + burst);
            if (got > size) g_buffer_grown++;
            AUDIO_SAY("xruns %d (+%d): the buffer grows from %d to %d frames (%.1f ms, capacity %d)",
                      (int) xr, (int) (xr - g_xr_seen), (int) size, (int) got,
                      got > 0 ? got * 1000.0 / (g_rate > 0 ? g_rate : 48000) : -1.0, (int) cap);
        }
        if (got <= size) {
            /* no room left: count it towards the last resort */
            if (!g_cap_ns) { g_cap_ns = now; g_cap_xr = 0; }
            g_cap_xr += xr - g_xr_seen;
            g_cap_last_ns = now;
        }
    }
    else if (g_size_open > 0 && g_shrink_wait_ns > 0 && now - g_xr_quiet_ns >= g_shrink_wait_ns) {
        int32_t size = AAudioStream_getBufferSizeInFrames(g_stream);
        int32_t burst = AAudioStream_getFramesPerBurst(g_stream);
        if (size > 0 && burst > 0 && size - burst >= g_size_open) {
            int32_t got = AAudioStream_setBufferSizeInFrames(g_stream, size - burst);
            if (got > 0 && got < size) {
                g_buffer_shrunk++;
                g_shrunk_ns = now;
                AUDIO_SAY("no underrun for %ld s: the buffer shrinks back from %d to %d frames (%.1f ms, opened at %d)",
                          g_shrink_wait_ns / 1000000000L, (int) size, (int) got,
                          got * 1000.0 / (g_rate > 0 ? g_rate : 48000), (int) g_size_open);
            }
        }
        g_xr_quiet_ns = now;
    }
    g_xr_seen = xr;
    if (g_cap_ns && now - g_cap_last_ns > CAP_QUIET_NS) g_cap_ns = 0;      /* it settled */
    if (g_cap_ns && !g_perf_fallback && now - g_cap_ns >= CAP_WINDOW_NS && g_cap_xr >= CAP_XRUNS
        && AAudioStream_getPerformanceMode(g_stream) == AAUDIO_PERFORMANCE_MODE_LOW_LATENCY) {
        g_perf_fallback = 1;
        g_perf = AAUDIO_PERFORMANCE_MODE_NONE;
        g_stream_lost = 1;                      /* shim_audio_tick rebuilds it right after this */
        LOG("audio: %ld underruns in %ld ms with the buffer at capacity: reopened with the ordinary mixer",
            g_cap_xr, (now - g_cap_ns) / 1000000L);
        AUDIO_SAY("FALLBACK: %ld underruns in %ld ms with the buffer at its capacity - the stream is reopened "
                  "once with the ordinary mixer (PERFORMANCE_MODE_NONE)", g_cap_xr, (now - g_cap_ns) / 1000000L);
    }
}

#if ISAAC_DIAG
/*
 * What the phone actually gave us, at every open: the rate, the format, the
 * path (MMAP or the older AudioTrack one - AAudio's own private probe, the same
 * one Oboe reads), the burst, the buffer and its capacity, the route's device.
 * Players' phones differ exactly here, and the report should say where.
 */
#include <dlfcn.h>
#include <sys/system_properties.h>

static const char *perf_text(int m) {
    switch (m) {
    case AAUDIO_PERFORMANCE_MODE_NONE:          return "NONE";
    case AAUDIO_PERFORMANCE_MODE_POWER_SAVING:  return "POWER_SAVING";
    case AAUDIO_PERFORMANCE_MODE_LOW_LATENCY:   return "LOW_LATENCY";
    default:                                    return "?";
    }
}

static void stream_told(AAudioStream *st) {
    static int told;
    if (!shim_report_active() || told >= 12) return;
    told++;
    static _Bool (*is_mmap)(AAudioStream *);
    static int looked;
    if (!looked) {
        looked = 1;
        void *lib = dlopen("libaaudio.so", RTLD_NOW | RTLD_NOLOAD);
        if (lib) is_mmap = (_Bool (*)(AAudioStream *)) dlsym(lib, "AAudioStream_isMMapUsed");
    }
    char make[PROP_VALUE_MAX] = "", model[PROP_VALUE_MAX] = "", soc[PROP_VALUE_MAX] = "", sdk[PROP_VALUE_MAX] = "";
    __system_property_get("ro.product.manufacturer", make);
    __system_property_get("ro.product.model", model);
    if (__system_property_get("ro.soc.model", soc) <= 0) __system_property_get("ro.board.platform", soc);
    __system_property_get("ro.build.version.sdk", sdk);
    int rate = AAudioStream_getSampleRate(st), burst = AAudioStream_getFramesPerBurst(st);
    int size = AAudioStream_getBufferSizeInFrames(st), cap = AAudioStream_getBufferCapacityInFrames(st);
    int fmt = AAudioStream_getFormat(st), ch = AAudioStream_getChannelCount(st);
    double ms = rate > 0 ? 1000.0 / rate : 0.0;
    snd_summary("stream opened (#%d): %d Hz, %d channel(s), %s%s, %s (asked %s), %s, %s; burst %d (%.1f ms), "
                "buffer %d (%.1f ms) of capacity %d, frames per callback %d, device %d; %s %s, SoC %s, API %s",
                told, rate, ch, fmt == AAUDIO_FORMAT_PCM_FLOAT ? "float" : fmt == AAUDIO_FORMAT_PCM_I16 ? "i16" : "?",
                (fmt != AAUDIO_FORMAT_PCM_FLOAT || ch != 2) ? " - NOT WHAT THE MIXER WRITES" : "",
                perf_text(AAudioStream_getPerformanceMode(st)), perf_text(g_perf),
                AAudioStream_getSharingMode(st) == AAUDIO_SHARING_MODE_EXCLUSIVE ? "exclusive" : "shared",
                is_mmap ? (is_mmap(st) ? "MMAP" : "legacy AudioTrack path") : "path unknown",
                burst, burst * ms, size, size * ms, cap, (int) AAudioStream_getFramesPerDataCallback(st),
                (int) AAudioStream_getDeviceId(st), make, model, soc, sdk);
}
#endif

static void audio_start(void) {
    if (g_stream) return;
    g_last_open_try_ns = now_ns();
    AAudioStreamBuilder *b = NULL;
    if (AAudio_createStreamBuilder(&b) != AAUDIO_OK || !b) {
        g_open_fail++;
        LOG("audio: AAudio did not start");
        AUDIO_SAY("the stream builder could not be made; the open will be tried again");
        return;
    }
    AAudioStreamBuilder_setFormat(b, AAUDIO_FORMAT_PCM_FLOAT);
    AAudioStreamBuilder_setChannelCount(b, 2);
    AAudioStreamBuilder_setPerformanceMode(b, g_perf);
    AAudioStreamBuilder_setSharingMode(b, AAUDIO_SHARING_MODE_SHARED);
    AAudioStreamBuilder_setDataCallback(b, mix, NULL);
    AAudioStreamBuilder_setErrorCallback(b, on_stream_error, NULL);
    aaudio_result_t r = AAudioStreamBuilder_openStream(b, &g_stream);
    AAudioStreamBuilder_delete(b);
    if (r != AAUDIO_OK || !g_stream) {
        g_open_fail++;
        LOG("audio: stream did not open (%d)", (int) r);
        AUDIO_SAY("the stream did not open (%s), failure %ld; the open will be tried again%s",
                  AAudio_convertResultToText(r), g_open_fail,
                  g_perf != AAUDIO_PERFORMANCE_MODE_LOW_LATENCY ? " - with the fast mixer, as before" : "");
        g_stream = NULL;
        g_perf = AAUDIO_PERFORMANCE_MODE_LOW_LATENCY;   /* the ordinary mixer would not open: back to what worked */
        return;
    }
    g_rate = AAudioStream_getSampleRate(g_stream);
    g_xr_seen = 0;                       /* a fresh stream counts its underruns from zero */
    g_cap_ns = 0;
    /* and its own clocks: the disconnect is timed from this stream, the shrink from its own size */
    g_disc_ns = 0;
    g_opened_ns = now_ns();
    g_size_open = AAudioStream_getBufferSizeInFrames(g_stream);
    g_xr_quiet_ns = g_opened_ns;
    g_shrunk_ns = 0;
    g_shrink_wait_ns = SHRINK_QUIET_NS;
    AAudioStream_requestStart(g_stream);
    g_mix_ns = now_ns();                 /* a fresh stream gets its two seconds before the watchdog looks */
    LOG("audio: AAudio %d Hz, two channels", g_rate);
    DIAG(stream_told(g_stream));
    if (g_open_fail || g_stream_losses || g_watchdog_rebuilds)
        AUDIO_SAY("the stream is open again at %d Hz (losses %ld, failed opens %ld, retries %ld, watchdog rebuilds %ld)",
                  g_rate, g_stream_losses, g_open_fail, g_open_retry, g_watchdog_rebuilds);
}

/* ---------- device and context: the game only needs a non-zero ---------- */

static int g_device = 0xA1D0, g_context = 0xA1C0;

/*
 * We count opens and closes: if the game rebuilds its audio when the language
 * changes, that will show up in the report, and along with it what it leaves
 * behind in the pools.
 */
void *shim_alcOpenDevice(const char *name) {
    (void) name;
    g_dev_open++;
    if (!g_dev_open_ns) g_dev_open_ns = now_ns();
    LOG("audio: device opened (%ld)", g_dev_open);
    pthread_mutex_lock(&g_stream_lock);
    audio_start();
    pthread_mutex_unlock(&g_stream_lock);
    return &g_device;
}
int shim_alcCloseDevice(void *dev) { (void) dev; g_dev_close++; LOG("audio: device closed (%ld)", g_dev_close); return AL_TRUE; }
void *shim_alcCreateContext(void *dev, const int *attrs) { (void) dev; (void) attrs; g_ctx_new++; return &g_context; }
int shim_alcMakeContextCurrent(void *ctx) { (void) ctx; return AL_TRUE; }
void shim_alcProcessContext(void *ctx) { (void) ctx; }
void shim_alcDestroyContext(void *ctx) {
    (void) ctx;
    g_ctx_del++;
    LOG("audio: context destroyed (%ld); buffers in use %ld, sources %ld", g_ctx_del, g_buf_used, g_src_used);
}

/*
 * MEASUREMENT, for the report: the lives of the long sounds.
 *
 * The owner (11.09.2026): in the main menu the music plays, and a couple of
 * seconds later a second music starts over it; a tester hears the same in
 * cutscenes. No queue ran dry there and no frame stalled, so this watches
 * what is done to the long sources - a queued one, or one whose single buffer
 * holds more than eight seconds: every play, pause, stop, delete and buffer
 * change, the gain as it moves, and the thread each call comes from (the
 * frame's thread is marked with a star). Once a second, when more than one of
 * them can be heard, which ones. Nothing here changes what is heard.
 */
static int g_frame_tid;                 /* the thread that draws the frames (shim_audio_tick) */
static int g_watch_lines;

static const char *watch_state(int st) {
    switch (st) {
    case AL_INITIAL: return "initial";
    case AL_PLAYING: return "playing";
    case AL_PAUSED:  return "paused";
    case AL_STOPPED: return "stopped";
    default:         return "?";
    }
}

/* under the lock */
static int is_music(const alsrc_t *s) {
    if (s->queued_ns) return 1;
    int id = s->buffer;
    return buf_live(id) && g_buf[id].rate > 0 && g_buf[id].frames > g_buf[id].rate * 8;
}

static void watch_line(const char *text) {
    if (g_watch_lines >= 240 || !shim_report_active()) return;
    g_watch_lines++;
    report_line("music", text);
}

static void watch_event(unsigned id, const char *what) {
    if (!shim_report_active()) return;
    int tid = gettid();
    char line[240];
    pthread_mutex_lock(&g_lock);
    alsrc_t *s = (src_name_ok(id) && g_src[id].used) ? &g_src[id] : NULL;
    if (!s || !is_music(s)) { pthread_mutex_unlock(&g_lock); return; }
    snprintf(line, sizeof(line),
             "+%ld ms: source %u %s, tid %d%s; it was %s, gain %.2f, queued %d, played %d, "
             "fed by tid %d%s, buffer %d",
             now_ns() / 1000000L, id, what, tid, tid == g_frame_tid ? "*" : "",
             watch_state(s->state), (double) s->gain, s->qn, s->dn,
             s->feeder_tid, s->feeder_tid && s->feeder_tid == g_frame_tid ? "*" : "", s->buffer);
    pthread_mutex_unlock(&g_lock);
    watch_line(line);
}

static void watch_gain(unsigned id, float value) {
    if (!shim_report_active()) return;
    char line[160];
    pthread_mutex_lock(&g_lock);
    alsrc_t *s = (src_name_ok(id) && g_src[id].used) ? &g_src[id] : NULL;
    if (!s || !is_music(s)) { pthread_mutex_unlock(&g_lock); return; }
    float was = s->told_gain;
    int crossed = (was <= 0.02f) != (value <= 0.02f);
    if (!crossed && fabs((double) (value - was)) < 0.25) { pthread_mutex_unlock(&g_lock); return; }
    s->told_gain = value;
    int tid = gettid();
    snprintf(line, sizeof(line), "+%ld ms: source %u gain %.2f -> %.2f, tid %d%s, %s",
             now_ns() / 1000000L, id, (double) was, (double) value, tid,
             tid == g_frame_tid ? "*" : "", watch_state(s->state));
    pthread_mutex_unlock(&g_lock);
    watch_line(line);
}

/* once a frame, from the frame's thread */
static void watch_audible(void) {
    if (!shim_report_active()) return;
    g_frame_tid = gettid();
    static long last_ns;
    static char was_key[64];
    long now = now_ns();
    if (now - last_ns < 1000000000L) return;
    last_ns = now;
    char list[300], key[64];
    int n = 0, k = 0, count = 0;
    list[0] = key[0] = 0;
    pthread_mutex_lock(&g_lock);
    for (int i = 1; i < g_src_cap; i++) {
        alsrc_t *s = &g_src[i];
        if (!s->used || s->state != AL_PLAYING || s->gain <= 0.02f || !is_music(s)) continue;
        count++;
        if (n < (int) sizeof(list) - 1)
            n += snprintf(list + n, sizeof(list) - (size_t) n, "%ssource %d gain %.2f fed by tid %d%s",
                          count > 1 ? ", " : "", i, (double) s->gain, s->feeder_tid,
                          s->feeder_tid && s->feeder_tid == g_frame_tid ? "*" : "");
        if (k < (int) sizeof(key) - 1)
            k += snprintf(key + k, sizeof(key) - (size_t) k, "%d,", i);
    }
    pthread_mutex_unlock(&g_lock);
    {
        /* the two counters that would mean the game and the layer disagree about a queue */
        static long told_over, told_refused;
        if (g_queue_overflows != told_over || g_delete_refused != told_refused) {
            told_over = g_queue_overflows; told_refused = g_delete_refused;
            char line[120];
            snprintf(line, sizeof(line), "+%ld ms: queue overflows %ld, deletes refused %ld",
                     now / 1000000L, told_over, told_refused);
            watch_line(line);
        }
    }
    if (count < 2) { was_key[0] = 0; return; }
    if (!strcmp(key, was_key)) return;
    snprintf(was_key, sizeof(was_key), "%s", key);
    char line[400];
    snprintf(line, sizeof(line), "+%ld ms: %d heard at once (frame tid %d): %s",
             now / 1000000L, count, g_frame_tid, list);
    watch_line(line);
}

/*
 * A LATE CALL ON A DELETED NAME, for the report.
 *
 * Measured on the owner's phone (11.09.2026): skipping the intro, the frame's
 * thread fades the voice-over's source out, stops it and deletes it - and the
 * last one to feed that source was ANOTHER thread, the game's music stream.
 * A call that thread makes a moment later names a source that no longer
 * exists. What such a call hits is the question, and this says whether they
 * happen at all: which call, on which name, from which thread, how long after
 * the deletion. Counted always, written only into the report.
 */
static long g_late_calls;

static void note_late(const char *call, const char *kind, unsigned id, long ago_ms) {
    g_late_calls++;
    if (!shim_report_active()) return;
    int tid = gettid();
    char line[200];
    snprintf(line, sizeof(line), "+%ld ms: %s on %s %u, deleted %ld ms ago, tid %d%s (%ld late calls so far)",
             now_ns() / 1000000L, call, kind, id, ago_ms, tid, tid == g_frame_tid ? "*" : "", g_late_calls);
    watch_line(line);
}

static void late_source(unsigned id, const char *call) {
    if (!id || !shim_report_active()) return;
    long ago = -1;
    pthread_mutex_lock(&g_lock);
    if (src_name_ok(id) && !g_src[id].used && g_src[id].freed_ns)
        ago = (now_ns() - g_src[id].freed_ns) / 1000000L;
    pthread_mutex_unlock(&g_lock);
    if (ago >= 0) note_late(call, "source", id, ago);
}

/*
 * A DELETED NAME RESTS BEFORE IT IS GIVEN AGAIN.
 *
 * The pools used to hand out the lowest free name, so a name deleted this very
 * moment was the first to be given to the next caller. With the game feeding
 * its music from a thread of its own, that is a trap: the frame's thread
 * deletes a stream's source, the feeding thread is one step behind and calls
 * on the same name - and by then the name may belong to a sound just made for
 * something else, which that call then queues into, starts, or stops. Real
 * OpenAL answers a call on a deleted name with AL_INVALID_NAME and does
 * nothing, and it can only do that while the name is not somebody else's.
 *
 * So a deleted name is not given out again for ten seconds, and the search for
 * a free one goes round from where the last one was given rather than from
 * the bottom. A late call then finds no source behind its name and is refused
 * with AL_INVALID_NAME. If nothing has rested long enough, the pool grows, as
 * it always could.
 */
#define NAME_REST_NS (10L * 1000L * 1000L * 1000L)
static int g_buf_cursor, g_src_cursor;

static int name_is_free(int used, long freed_ns, long now) {
    return !used && (!freed_ns || now - freed_ns > NAME_REST_NS);
}

/*
 * THE ANSWERS ARE OPENAL'S.
 *
 * Found by reading the game (11.09.2026), looking for how a sound could play
 * twice. The video player's audio (theoraplayer's OpenAL interface, 0x8a4bac)
 * seeks like this - 0x8a4f3c, and doSeek (0x8fccf0) runs it on the worker
 * thread:
 *
 *     alSourceStop(source)
 *     while (queue not empty) { alSourceUnqueueBuffers(1); alDeleteBuffers(1) }
 *
 * On OpenAL a stop makes every queued buffer processed, so the drain takes
 * them all. Here a stop left the queue as it was, the unqueue handed back only
 * the ones that had played, the rest stayed in the queue under names the game
 * then deleted - and when a name was given out again to a fresh chunk, that
 * chunk was heard twice: once from where the stale entry sat, once from where
 * it was queued. The same shape - a queue the game thinks empty and the layer
 * does not - came from three more answers that differed from OpenAL's, and all
 * of them are put right here:
 *
 *   - alSourceStop: every queued buffer becomes processed (all_processed);
 *   - alSourcePlay, unless it resumes a pause, starts from the head of the
 *     whole queue, processed buffers included (restart_from_head) - which is
 *     what OpenAL does and what the game's own collect-refill-play order
 *     relies on;
 *   - alDeleteBuffers refuses a buffer still attached to a source
 *     (AL_INVALID_OPERATION), so a queue can never hold a dead name;
 *   - alSourcei(AL_BUFFER) is refused on a playing or paused source, and a
 *     name that is not a live buffer is refused with AL_INVALID_VALUE (the
 *     source keeps what it had);
 *   - alBufferData refuses a buffer attached to ANY source, a stopped one
 *     included (AL_INVALID_OPERATION) - Apple's OALBuffer::AddAudioData checks
 *     attachedCount > 0 and so does OpenAL Soft. We used to pour a new sound
 *     into a buffer while a source was reading it;
 *   - a name that is not a live source or buffer gets AL_INVALID_NAME, and an
 *     alGet* on it leaves the answer untouched;
 *   - alBufferData with no bytes makes an empty buffer rather than leaving the
 *     old sound in it; AL_SEC_OFFSET counts the processed buffers still
 *     attached, as OpenAL does; and the queue is long enough for the video's
 *     hundred-millisecond chunks (QUEUE_MAX).
 */

/* under the lock: every queued buffer becomes processed, as a stop makes it */
static void all_processed(alsrc_t *s) {
    if (!s->q) { s->qn = 0; return; }
    for (int i = 0; i < s->qn; i++)
        if (s->dn < QUEUE_MAX) s->q->done[s->dn++] = s->q->queue[i];
    s->qn = 0;
}

/* under the lock: the processed buffers go back in front, as a fresh play replays them */
static void restart_from_head(alsrc_t *s) {
    if (s->dn == 0 || !s->q) return;
    int tmp[QUEUE_MAX], k = 0;
    for (int i = 0; i < s->dn && k < QUEUE_MAX; i++) tmp[k++] = s->q->done[i];
    for (int i = 0; i < s->qn && k < QUEUE_MAX; i++) tmp[k++] = s->q->queue[i];
    memcpy(s->q->queue, tmp, sizeof(int) * (size_t) k);
    s->qn = k;
    s->dn = 0;
}

/*
 * Under the lock: the source a buffer is attached to, or 0. A walk over the
 * truth, not a count kept aside: a count drifts wherever a queue is cut short
 * (QUEUE_MAX, AL_BUFFER over a queue), and a buffer wrongly believed attached
 * could never be refilled again - a wrong sound for good, the very thing this
 * file puts right. At over a thousand sources the walk is a few thousand
 * compares, and it runs only when a buffer is filled.
 */
static int attached_to(int id) {
    for (int i = 1; i < g_src_cap; i++) {
        const alsrc_t *s = &g_src[i];
        if (!s->used) continue;
        if (s->buffer == id) return i;
        if (!s->q) continue;
        for (int k = 0; k < s->qn; k++) if (s->q->queue[k] == id) return i;
        for (int k = 0; k < s->dn; k++) if (s->q->done[k] == id) return i;
    }
    return 0;
}

/* ---------- buffers ---------- */

/* under the lock: double the pool, keeping the names */
static int grow_buffers(void) {
    int grown = g_buf_cap ? g_buf_cap * 2 : BUF_START;
    albuf_t *fresh = (albuf_t *) realloc(g_buf, sizeof(albuf_t) * (size_t) grown);
    if (!fresh) return 0;
    memset(fresh + g_buf_cap, 0, sizeof(albuf_t) * (size_t) (grown - g_buf_cap));
    g_buf = fresh; g_buf_cap = grown;
    return 1;
}

static int grow_sources(void) {
    int grown = g_src_cap ? g_src_cap * 2 : SRC_START;
    alsrc_t *fresh = (alsrc_t *) realloc(g_src, sizeof(alsrc_t) * (size_t) grown);
    if (!fresh) return 0;
    memset(fresh + g_src_cap, 0, sizeof(alsrc_t) * (size_t) (grown - g_src_cap));
    g_src = fresh; g_src_cap = grown;
    return 1;
}

/* under the lock: a fresh buffer name, or 0 */
static int gen_buffer_locked(void) {
    int got = 0;
    long now = now_ns();
    for (;;) {
        for (int step = 0; step < g_buf_cap - 1; step++) {
            int i = 1 + (g_buf_cursor + step) % (g_buf_cap - 1);
            if (name_is_free(g_buf[i].used, g_buf[i].freed_ns, now)) {
                g_buf[i].used = 1;
                g_buf[i].freed_ns = 0;
                g_buf_cursor = i % (g_buf_cap - 1);
                got = i;
                break;
            }
        }
        if (got || !grow_buffers()) break;
    }
    if (got) { g_buf_given++; if (++g_buf_used > g_buf_peak) g_buf_peak = g_buf_used; }
    else { g_buf_fail++; LOG("audio: NO BUFFER — refusal %ld (%ld in use)", g_buf_fail, g_buf_used); }
    return got;
}

void shim_alGenBuffers(int n, unsigned *out) {
    pthread_mutex_lock(&g_lock);
    for (int k = 0; k < n; k++) out[k] = (unsigned) gen_buffer_locked();
    pthread_mutex_unlock(&g_lock);
}

/*
 * THE MIXER NEVER WAITS FOR free() (05.10.2026).
 *
 * The mixer takes g_lock on the audio thread, and every thread that holds it
 * makes the mixer wait. Android's audio thread runs at real-time priority, the
 * game's threads do not, and bionic's mutex has no priority inheritance: a
 * game thread preempted while holding the lock stalls the speaker
 * (source.android.com, "Avoiding priority inversion"). So nothing slow is
 * done under it. Freeing a sound's samples was: a large block goes back to the
 * kernel with munmap, which waits for the process's memory-map lock - and that
 * lock is busy exactly while the game loads a room and faults its textures in.
 * The samples are now handed out of the lock and freed after it.
 */

/* under the lock: the name goes, and rests; the caller frees the samples returned, after the lock */
static int16_t *buffer_delete_locked(int i) {
    int16_t *pcm = g_buf[i].pcm;
    if (g_buf[i].used) g_buf_used--;
    if (pcm) g_buf_bytes -= (long) g_buf[i].frames * g_buf[i].channels * 2;
    memset(&g_buf[i], 0, sizeof(g_buf[i]));
    g_buf[i].freed_ns = now_ns();
    return pcm;
}

void shim_alDeleteBuffers(int n, const unsigned *ids) {
    /* the samples to free once the lock is let go; without room for the list, freed in place as before */
    int16_t **gone = n > 0 ? (int16_t **) calloc((size_t) n, sizeof(int16_t *)) : NULL;
    pthread_mutex_lock(&g_lock);
    for (int k = 0; k < n; k++) {
        unsigned i = ids[k];
        if (i == 0) continue;                           /* the null buffer: nothing to delete, as OpenAL */
        if (!buf_live((int) i)) { SET_ERROR(AL_INVALID_NAME); continue; }
        int holder = attached_to((int) i);
        if (holder) {
            /* still attached: OpenAL refuses, and the buffer stays */
            SET_ERROR(AL_INVALID_OPERATION);
            g_delete_refused++;
            if (g_delete_refused <= 12) {
                char line[160];
                snprintf(line, sizeof(line), "+%ld ms: buffer %u is still attached to source %d, not deleted (%ld so far)",
                         now_ns() / 1000000L, i, holder, g_delete_refused);
                watch_line(line);
            }
            continue;
        }
        int16_t *pcm = buffer_delete_locked((int) i);
        if (gone) gone[k] = pcm;
        else free(pcm);
    }
    pthread_mutex_unlock(&g_lock);
    if (gone) {
        for (int k = 0; k < n; k++) free(gone[k]);
        free(gone);
    }
}

/* the game's PCM as ours: always sixteen bits */
static int pcm_make(int format, const void *data, int size, int16_t **out, int *frames_out, int *channels_out) {
    *out = NULL;
    if (size < 0 || (size > 0 && !data)) return AL_INVALID_VALUE;
    int channels = (format == AL_FORMAT_STEREO8 || format == AL_FORMAT_STEREO16) ? 2 : 1;
    int wide = (format == AL_FORMAT_MONO16 || format == AL_FORMAT_STEREO16);
    int frames = wide ? size / (2 * channels) : size / channels;
    /* no bytes is a legal, empty buffer: it plays through in no time */
    int16_t *pcm = frames > 0 ? (int16_t *) malloc(sizeof(int16_t) * (size_t) frames * (size_t) channels) : NULL;
    if (frames > 0 && !pcm) return AL_INVALID_VALUE;
    if (frames == 0) {
        /* nothing to copy */
    } else if (wide) {
        memcpy(pcm, data, sizeof(int16_t) * (size_t) frames * (size_t) channels);
    } else {
        /* eight-bit audio in OpenAL is unsigned, centred on 128 */
        const uint8_t *src = (const uint8_t *) data;
        for (long i = 0; i < (long) frames * channels; i++)
            pcm[i] = (int16_t) ((src[i] - 128) << 8);
    }
    *out = pcm; *frames_out = frames; *channels_out = channels;
    return AL_NO_ERROR;
}

/* under the lock: the buffer takes the new sound; the old samples are returned, to be freed after the lock */
static int16_t *buf_commit_locked(int id, int16_t *pcm, int frames, int channels, int freq, const void *origin) {
    int16_t *old = g_buf[id].pcm;
    if (old) g_buf_bytes -= (long) g_buf[id].frames * g_buf[id].channels * 2;
    g_buf_bytes += (long) frames * channels * 2;
    g_buf[id].pcm = pcm;
    g_buf[id].frames = frames;
    g_buf[id].channels = channels;
    g_buf[id].rate = freq > 0 ? freq : 44100;
    g_buf[id].origin = origin;
    return old;
}

#if ISAAC_DIAG
static void census_data_refused(unsigned id, int holder, int state, unsigned long from);
static void census_pour_over(unsigned long from);
#endif

void shim_alBufferData(unsigned id, int format, const void *data, int size, int freq) {
    unsigned long from = (unsigned long) __builtin_return_address(0);
    pthread_mutex_lock(&g_lock);
    int named = buf_name_ok(id), live = named && g_buf[id].used;
    int holder = live ? attached_to((int) id) : 0;
    int holder_state = holder ? g_src[holder].state : 0;
    long ago = (named && !live && g_buf[id].freed_ns) ? (now_ns() - g_buf[id].freed_ns) / 1000000L : -1;
    pthread_mutex_unlock(&g_lock);
    if (!live) {
        /* a name that was never given, or was deleted: OpenAL refuses it, and so do we */
        SET_ERROR(AL_INVALID_NAME);
        if (named) note_late("alBufferData", "buffer", id, ago);
        return;
    }
    if (holder) {
        /* attached to a source - playing or not: refused, and the old sound stays (Apple, OpenAL Soft) */
        SET_ERROR(AL_INVALID_OPERATION);
        g_data_refused++;
        DIAG(census_data_refused(id, holder, holder_state, from));
        return;
    }
    int16_t *pcm = NULL;
    int frames = 0, channels = 1;
    int err = pcm_make(format, data, size, &pcm, &frames, &channels);
    if (err) { SET_ERROR(err); return; }
    static int told;
    if (told < 6) { told++; LOG("audio: buffer %u — %d frames, %d channel(s), %d Hz", id, frames, channels, freq); }
    pthread_mutex_lock(&g_lock);
    /* another thread may have deleted or attached it while the sound was being made */
    if (!buf_live((int) id)) { pthread_mutex_unlock(&g_lock); free(pcm); SET_ERROR(AL_INVALID_NAME); return; }
    holder = attached_to((int) id);
    if (holder) {
        holder_state = g_src[holder].state;
        pthread_mutex_unlock(&g_lock);
        free(pcm);
        SET_ERROR(AL_INVALID_OPERATION);
        g_data_refused++;
        DIAG(census_data_refused(id, holder, holder_state, from));
        return;
    }
    int16_t *old = buf_commit_locked((int) id, pcm, frames, channels, freq, data);
    pthread_mutex_unlock(&g_lock);
    free(old);
    DIAG(census_pour_over(from));
    (void) from; (void) holder_state;
    if (frames > (freq > 0 ? freq : 44100) * 8) {
        char line[120];
        snprintf(line, sizeof(line), "+%ld ms: buffer %u is filled with %d s of sound, tid %d%s",
                 now_ns() / 1000000L, id, frames / (freq > 0 ? freq : 44100), gettid(),
                 gettid() == g_frame_tid ? "*" : "");
        watch_line(line);
    }
}

/* ---------- sources ---------- */

void shim_alGenSources(int n, unsigned *out) {
    pthread_mutex_lock(&g_lock);
    for (int k = 0; k < n; k++) {
        out[k] = 0;
        long now = now_ns();
        for (;;) {
            for (int step = 0; step < g_src_cap - 1; step++) {
                int i = 1 + (g_src_cursor + step) % (g_src_cap - 1);
                if (name_is_free(g_src[i].used, g_src[i].freed_ns, now)) {
                    free(g_src[i].q);
                    memset(&g_src[i], 0, sizeof(g_src[i]));
                    g_src[i].used = 1;
                    g_src[i].state = AL_INITIAL;
                    g_src[i].gain = 1.0f;
                    g_src[i].pitch = 1.0f;
                    g_src_cursor = i % (g_src_cap - 1);
                    out[k] = (unsigned) i;
                    break;
                }
            }
            if (out[k] || !grow_sources()) break;
        }
        if (out[k]) { g_src_given++; if (++g_src_used > g_src_peak) g_src_peak = g_src_used; }
        else { g_src_fail++; LOG("audio: NO SOURCE — refusal %ld (%ld in use)", g_src_fail, g_src_used); }
    }
    pthread_mutex_unlock(&g_lock);
}

void shim_alDeleteSources(int n, const unsigned *ids) {
#if ISAAC_DIAG
    unsigned long from, from2;
    METER_FROM(from, from2);
#endif
    for (int k = 0; k < n; k++) watch_event(ids[k], "is deleted");
    pthread_mutex_lock(&g_lock);
    for (int k = 0; k < n; k++) {
        unsigned i = ids[k];
        if (!src_name_ok(i) || !g_src[i].used) { SET_ERROR(AL_INVALID_NAME); continue; }
        DIAG(if (g_src[i].state == AL_PLAYING && g_src[i].gain > METER_GAIN_EPS)
                 meter_event('d', (int) i, g_src[i].gain, 0.0f, from, from2));
        DIAG(fate_close(&g_src[i], 'd', from, from2));
        g_src_used--;
        free(g_src[i].q);
        memset(&g_src[i], 0, sizeof(alsrc_t));
        g_src[i].freed_ns = now_ns();
    }
    pthread_mutex_unlock(&g_lock);
}

/* under the lock */
static alsrc_t *src_of(unsigned id) {
    if (!src_name_ok(id) || !g_src[id].used) return NULL;
    return &g_src[id];
}

#if ISAAC_DIAG
static void census_minus_one(unsigned long from);
static void census_buffer_refused(unsigned id, int state, int had, int wanted, unsigned long from);
static void census_pitch(float value, unsigned long from);
static void census_position(float x);
static void census_play(unsigned id, unsigned long from);
static void census_play_refused(unsigned id, unsigned long from, unsigned long from2);
static void census_ring(unsigned long from);
#endif

void shim_alSourcei(unsigned id, int param, int value) {
    unsigned long from = (unsigned long) __builtin_return_address(0);
    DIAG(if (id == 0xffffffffu) census_minus_one(from));
    late_source(id, "alSourcei");
    if (param == AL_BUFFER) watch_event(id, value ? "is given a buffer" : "has its buffer cleared");
    int err = AL_NO_ERROR, state = 0, had = 0;
    pthread_mutex_lock(&g_lock);
    alsrc_t *s = src_of(id);
    if (!s) err = AL_INVALID_NAME;
    else if (param == AL_BUFFER) {
        if (value != 0 && !buf_live(value)) err = AL_INVALID_VALUE;       /* not a live buffer */
        /* OpenAL refuses to change the buffer of a source that is playing or paused */
        else if (s->state == AL_PLAYING || s->state == AL_PAUSED) {
            err = AL_INVALID_OPERATION; state = s->state; had = s->buffer;
        }
        else { s->buffer = value; s->pos = 0; s->qn = 0; s->dn = 0; s->unbound = 0; }
    }
    else if (param == AL_LOOPING) s->looping = value;
    pthread_mutex_unlock(&g_lock);
    if (err) SET_ERROR(err);
    DIAG(if (err == AL_INVALID_OPERATION) census_buffer_refused(id, state, had, value, from));
    (void) from; (void) state; (void) had;
}

void shim_alSourcef(unsigned id, int param, float value) {
#if ISAAC_DIAG
    unsigned long from, from2;
    METER_FROM(from, from2);
#endif
    late_source(id, "alSourcef");
    int err = AL_NO_ERROR;
    pthread_mutex_lock(&g_lock);
    alsrc_t *s = src_of(id);
    if (!s) err = AL_INVALID_NAME;
    else {
        /*
         * Out-of-range values are REFUSED, the way OpenAL refuses them, and the
         * source keeps what it had. Real OpenAL answers a pitch of zero or less
         * with AL_INVALID_VALUE and changes nothing; the game expects exactly
         * that and even logs it ("SetPitch() encountered OpenAL error"). We
         * used to accept it and clamp to 0.01: the source then read its buffers
         * a hundred times slower and the music died for good.
         *
         * Volume is not ours to reason about either. The game works out every
         * music gain in 0x75fb38 as
         *
         *     [0xac2488] * player[0x1cc] * channel[0x20] * channel[0x24]
         *
         * - the run-is-going flag, the crossfade between the two tracks the
         * manager keeps, the track's own "mul", and the layer - and hands us
         * the result. Measured on the device end to end: a music channel that
         * falls silent falls silent because the game asked for it. Unicorn
         * Stump is the loudest example and it is authored: while its effect
         * lasts, 0x52bed8 re-rolls the music pitch to a random 0.5 .. 2.0 and,
         * one roll in three, calls SetVolume(0, 0.08) and fades the music out
         * altogether; 0x53aa4c puts both back when the effect ends.
         *
         * A value that is not a number is refused too, as OpenAL refuses it:
         * a NaN gain would put NaN into the output for as long as the source
         * plays, and a NaN pitch would leave the read position NaN - a source
         * that never ends and never sounds, PLAYING for ever in the game's pool.
         */
        if (param == AL_GAIN) {
            if (!(value >= 0.0f) || !isfinite(value)) err = AL_INVALID_VALUE;
            else {
                /* the meter's ring: a sounding source turned down to nothing, or back up */
                DIAG(if ((s->gain > METER_GAIN_EPS) != (value > METER_GAIN_EPS) && s->state == AL_PLAYING)
                         meter_event(value > METER_GAIN_EPS ? 'G' : 'g', (int) id, s->gain, value, from, from2));
                s->gain = value;
            }
        } else if (param == AL_PITCH) {
            if (!(value > 0.0f) || !isfinite(value)) err = AL_INVALID_VALUE;
            else s->pitch = value;
        }
    }
    pthread_mutex_unlock(&g_lock);
    if (err) SET_ERROR(err);
    DIAG(if (!err && param == AL_PITCH) census_pitch(value, from));
    if (!err && param == AL_GAIN) watch_gain(id, value);
}

/*
 * The game does set a source's position in space (0x8a320c), and on iOS its
 * effects are heard left and right. That is a real difference, but not this
 * complaint's: the panning comes back in a later round, after the iOS 3D
 * mixer's default algorithm is known. Until then the name is checked as
 * everywhere else, and the report writes down which positions are asked for.
 */
void shim_alSource3f(unsigned id, int param, float x, float y, float z) {
    (void) y; (void) z;
    pthread_mutex_lock(&g_lock);
    int ok = src_of(id) != NULL;
    pthread_mutex_unlock(&g_lock);
    if (!ok) { SET_ERROR(AL_INVALID_NAME); return; }
    DIAG(if (param == AL_POSITION) census_position(x));
    (void) param; (void) x;
}

void shim_alListener3f(int param, float x, float y, float z) {
    (void) param; (void) x; (void) y; (void) z;
}

void shim_alListenerfv(int param, const float *v) {
    (void) param; (void) v;
#if ISAAC_DIAG
    /* never applied, as it never was; the meter only writes down whether the game asks */
    if (param == AL_GAIN && v) {
        unsigned long from, from2;
        METER_FROM(from, from2);
        pthread_mutex_lock(&g_lock);
        meter_event('L', 0, g_listener_gain, v[0], from, from2);
        g_listener_gain = v[0];
        pthread_mutex_unlock(&g_lock);
    }
#endif
}

void shim_alSourcePlay(unsigned id) {
    DIAG(static int told;
         if (told < 300) { told++;
             pthread_mutex_lock(&g_lock);
             alsrc_t *t = src_of(id);
             LOG("audio: source %u started (buffer %d, queued %d, gain %.3f)", id,
                 t ? t->buffer : -1, t ? t->qn : -1, t ? (double) t->gain : -1.0);
             pthread_mutex_unlock(&g_lock); });
    watch_event(id, "is played");
    late_source(id, "alSourcePlay");
    unsigned long from = (unsigned long) __builtin_return_address(0);
    DIAG(census_play(id, from));
#if ISAAC_DIAG
    unsigned long p_at, p_up;
    METER_FROM(p_at, p_up);
#endif
    pthread_mutex_lock(&g_lock);
    alsrc_t *s = src_of(id);
    if (s) {
#if ISAAC_DIAG
        /* a play that is not a resume ends the play before it, and begins one of its own */
        if (s->state != AL_PAUSED) {
            if (s->state == AL_PLAYING) fate_close(s, 'r', p_at, p_up);
            fate_open(s, p_at, p_up);
        }
#endif
        s->played_ns = now_ns();
        s->play_from = img_off(from);
        s->census_told = 0;
#if ISAAC_DIAG
        if (!census_off()) {
            g_m_plays++;
            if (img_off(from) == AT_PLAY_CALL) g_m_eplays++;
            if (is_music(s)) {
                unsigned long at, up;
                METER_FROM(at, up);
                meter_event('P', (int) id, s->gain, s->gain, at, up);
            }
        }
#endif
        if (s->starved) {
#if ISAAC_DIAG
            static int said;
            if (said++ < 12)
                LOG("audio: the game restarted source %u after a dry queue (%d queued)", id, s->qn);
#endif
            s->starved = 0;
            for (int k = g_dry_n - 1; k >= 0; k--)
                if (g_dry[k].src == (int) id && g_dry[k].restart_ms < 0) {
                    g_dry[k].restart_ms = (now_ns() - s->starved_ns) / 1000000L;
                    break;
                }
        }
        if (s->state != AL_PAUSED) { restart_from_head(s); s->pos = 0; }
        s->state = AL_PLAYING;
    }
    pthread_mutex_unlock(&g_lock);
    if (!s) SET_ERROR(AL_INVALID_NAME);
    DIAG(if (!s) census_play_refused(id, p_at, p_up));
}

void shim_alSourcePause(unsigned id) {
#if ISAAC_DIAG
    unsigned long from, from2;
    METER_FROM(from, from2);
#endif
    watch_event(id, "is paused");
    late_source(id, "alSourcePause");
    pthread_mutex_lock(&g_lock);
    alsrc_t *s = src_of(id);
    if (s && s->state == AL_PLAYING) {
        DIAG(meter_event('p', (int) id, s->gain, s->gain, from, from2));
        s->state = AL_PAUSED; s->paused_ns = now_ns(); s->census_told = 0;
    }
    pthread_mutex_unlock(&g_lock);
    if (!s) SET_ERROR(AL_INVALID_NAME);
}

void shim_alSourceStop(unsigned id) {
#if ISAAC_DIAG
    unsigned long from, from2;
    METER_FROM(from, from2);
#endif
    watch_event(id, "is stopped");
    late_source(id, "alSourceStop");
    pthread_mutex_lock(&g_lock);
    alsrc_t *s = src_of(id);
    DIAG(if (s && (s->state == AL_PLAYING || s->state == AL_PAUSED) && s->gain > METER_GAIN_EPS)
             meter_event('s', (int) id, s->gain, 0.0f, from, from2));
    DIAG(if (s) fate_close(s, 's', from, from2));
    if (s) { s->state = AL_STOPPED; s->pos = 0; all_processed(s); }
    pthread_mutex_unlock(&g_lock);
    if (!s) SET_ERROR(AL_INVALID_NAME);
}

/* the thread that last refilled a queue, for the report (shim_audio_refilled_here) */
static volatile int g_queue_tid;

void shim_alSourceQueueBuffers(unsigned id, int n, const unsigned *ids) {
    late_source(id, "alSourceQueueBuffers");
    int err = AL_NO_ERROR;
    pthread_mutex_lock(&g_lock);
    alsrc_t *s = src_of(id);
    if (!s) err = AL_INVALID_NAME;
    else {
        /* every name first, as OpenAL does: a bad one and nothing is queued */
        for (int k = 0; k < n; k++)
            if (ids[k] != 0 && !buf_live((int) ids[k])) { err = AL_INVALID_NAME; break; }
        if (!err && !s->q && !(s->q = (srcq_t *) calloc(1, sizeof(srcq_t)))) err = AL_INVALID_VALUE;
    }
    int first_fed = 0;
    if (!err) {
        for (int k = 0; k < n; k++) {
            if (s->qn >= QUEUE_MAX) { g_queue_overflows++; break; }
            s->q->queue[s->qn++] = (int) ids[k];
        }
        first_fed = !s->queued_ns;
        s->queued_ns = now_ns(); s->held_ms = held_ms_of(s);
        g_queue_tid = gettid(); s->feeder_tid = g_queue_tid;
    }
    pthread_mutex_unlock(&g_lock);
    if (err) SET_ERROR(err);
    if (first_fed) watch_event(id, "is fed for the first time");
}

/*
 * The game takes back the buffers that have played out, pours the next chunk
 * of music into them and returns them to the queue. We must hand back their
 * NAMES and exactly as many as have played: an empty name goes straight back
 * in, and the music loops on a single fragment.
 */
void shim_alSourceUnqueueBuffers(unsigned id, int n, unsigned *out) {
    late_source(id, "alSourceUnqueueBuffers");
    pthread_mutex_lock(&g_lock);
    alsrc_t *s = src_of(id);
    int given = 0;
    if (s && s->q)
        while (given < n && s->dn > 0) {
            out[given++] = (unsigned) s->q->done[0];
            for (int i = 1; i < s->dn; i++) s->q->done[i - 1] = s->q->done[i];
            s->dn--;
        }
    pthread_mutex_unlock(&g_lock);
    if (!s) SET_ERROR(AL_INVALID_NAME);
    else if (given < n) SET_ERROR(AL_INVALID_VALUE);
}

void shim_alGetSourcei(unsigned id, int param, int *out) {
    if (!out) return;
    DIAG(census_ring((unsigned long) __builtin_return_address(0)));
    late_source(id, "alGetSourcei");
    pthread_mutex_lock(&g_lock);
    alsrc_t *s = src_of(id);
    if (s) {
        if (param == AL_SOURCE_STATE) *out = s->state;
        else if (param == AL_BUFFERS_QUEUED) *out = s->qn + s->dn;
        else if (param == AL_BUFFERS_PROCESSED) *out = s->dn;
        else if (param == AL_BUFFER) *out = s->buffer;
        else if (param == AL_LOOPING) *out = s->looping;
        else *out = 0;
    }
    pthread_mutex_unlock(&g_lock);
    if (!s) SET_ERROR(AL_INVALID_NAME);          /* and the answer is left as it was */
}

void shim_alGetSourcef(unsigned id, int param, float *out) {
    if (!out) return;
    pthread_mutex_lock(&g_lock);
    alsrc_t *s = src_of(id);
    if (s) {
        if (param == AL_GAIN) *out = s->gain;
        else if (param == AL_PITCH) *out = s->pitch;
        else if (param == AL_SEC_OFFSET) {
            /* from the head of the attached queue: the processed buffers count, as in OpenAL */
            double sec = 0.0;
            for (int i = 0; s->q && i < s->dn; i++) {
                int b = s->q->done[i];
                if (buf_live(b) && g_buf[b].rate > 0)
                    sec += (double) g_buf[b].frames / (double) g_buf[b].rate;
            }
            albuf_t *b = current_of(s);
            if (b && b->rate) sec += s->pos / b->rate;
            *out = (float) sec;
        }
        else *out = 0.0f;
    }
    pthread_mutex_unlock(&g_lock);
    if (!s) SET_ERROR(AL_INVALID_NAME);
}

int shim_alGetError(void) {
    int e = g_error;
    g_error = AL_NO_ERROR;
#if ISAAC_DIAG
    shadow_read(e, (unsigned long) __builtin_return_address(0));
#endif
    return e;
}

/*
 * ONE OWNER PER SOURCE AND PER BUFFER (30.09.2026; the owner: "fix the
 * original's pool and cache, as with the bomb button").
 *
 * "Single sounds die during play, different ones, and never come back." Both
 * causes are the game's own and live on iOS too, where Apple's platform
 * happened to cover them:
 *
 * THE POOL (0x8c67d4). Every effect asks the manager's pool for a source,
 * keyed by the sound object. The first 64 objects each get a source of their
 * own, for good; past 64 an object is handed ANY source that is not playing
 * (the ring, 0x8c68b8..0x8c6938) - somebody else's own source included - and
 * that object keeps the source's name in [obj+0x58] as its own. Every play
 * begins by stopping [obj+0x58] (0x8a9fc8 -> 0x8a30e0) without asking whose
 * sound is in it now, and volume is written there just as blindly: effects cut
 * each other off and turn each other down, long and looping ones first. 64 is
 * no design of the game: it is the default number of buses of Apple's mixer
 * ("default mixer bus count of 64", oalImp.cpp:962), a limit of the iOS
 * platform. Our mixer has no such limit, so the pool's size word becomes 8192:
 * every sound object takes the game's own first branch, a source of its own,
 * and the ring and the -1 never come (sounds.xml holds about 1151 samples).
 *
 * THE CACHE (0x8d09dc). 500 buffers keyed by the address of the sound's PCM.
 * When all 500 are taken the game pours the new sound into the oldest buffer
 * and neither drops the old key nor adds the new one: from then on the old
 * sound plays whatever was poured over it - a scrap, a click, silence - until
 * the process ends, and every new sound spoils one more. Apple refused the
 * pour on a buffer attached to a source (see alBufferData above), which only
 * moves the wrong sound to the new one. So the manager's "buffer for this
 * sound" branch (0x8c95cc) goes to shim_sound_cache_get: the key is the
 * address, the size, the format and the rate, the order is least recently
 * used, the capacity is the game's own 500 read from the object, and a box
 * whose buffer a playing or paused source holds is never taken. A stopped
 * source holding the box's buffer lets go of it (buffer 0): the game sets
 * AL_BUFFER again before every play of an effect (0x8a2f38), and the only
 * play that does not - "resume", vtable +0x70, 0x8a3024 - is reached from the
 * music's handles alone (0x8ae404 <- 0x75f700/0x75f72c and the older copies),
 * whose sounds are streamed (0x8a3bcc loads them as streams) and never come
 * from this cache. The census writes down any play on a source with no
 * buffer from anywhere but 0x8a2f90 all the same.
 *
 * THE TWO GO TOGETHER OR NOT AT ALL. With a source per sound, each stopped
 * source keeps its last buffer; without letting go nearly every box would be
 * held and nothing could be evicted.
 */
#define CACHE_STATES 4                  /* the live cache and the self-test's */
#define CACHE_HASH   2048

typedef struct {
    const void *data; int size, format, freq;
    uint64_t print[3];                  /* words of the sound itself: the address may be reused */
    int buf;
    int keyed, listed;
    int newer, older;                   /* the order of use: newest at the head */
    int chain;                          /* the next box in the same bucket */
} cbox_t;

typedef struct {
    const void *game;                   /* the game's cache object this serves */
    const void *names;                  /* its [+8] when first seen: another object at the same address differs */
    int cap;
    cbox_t *box; int n, room;
    int bucket[CACHE_HASH];
    int newest, oldest;
    long hits, misses, evicted, unbound, over, stale, failed;
    int said;
} scache_t;

static scache_t g_caches[CACHE_STATES];
static pthread_mutex_t g_cache_lock = PTHREAD_MUTEX_INITIALIZER;   /* taken before g_lock, never after */
static unsigned char *g_busy; static int g_busy_cap;
static int g_patched;                   /* the pool's size and the cache are ours */

static unsigned cache_hash(const void *data, int size, int format, int freq) {
    uint64_t h = (uint64_t) (uintptr_t) data;
    h ^= (uint64_t) (unsigned) size * 0x9E3779B97F4A7C15ull;
    h ^= ((uint64_t) (unsigned) format << 17) ^ ((uint64_t) (unsigned) freq << 31);
    h ^= h >> 29; h *= 0xBF58476D1CE4E5B9ull; h ^= h >> 32;
    return (unsigned) h & (CACHE_HASH - 1);
}

static void print_of(const void *data, int size, uint64_t out[3]) {
    out[0] = out[1] = out[2] = 0;
    if (!data || size <= 0) return;
    const unsigned char *p = (const unsigned char *) data;
    size_t n = (size_t) size, take = n < 8 ? n : 8;
    memcpy(&out[0], p, take);
    memcpy(&out[1], p + (n - take) / 2, take);
    memcpy(&out[2], p + n - take, take);
}

static void lru_unlink(scache_t *st, int i) {
    cbox_t *b = &st->box[i];
    if (!b->listed) return;
    if (b->newer >= 0) st->box[b->newer].older = b->older; else st->newest = b->older;
    if (b->older >= 0) st->box[b->older].newer = b->newer; else st->oldest = b->newer;
    b->newer = b->older = -1;
    b->listed = 0;
}

static void lru_push_newest(scache_t *st, int i) {
    cbox_t *b = &st->box[i];
    lru_unlink(st, i);
    b->newer = -1; b->older = st->newest;
    if (st->newest >= 0) st->box[st->newest].newer = i; else st->oldest = i;
    st->newest = i;
    b->listed = 1;
}

static void lru_push_oldest(scache_t *st, int i) {
    cbox_t *b = &st->box[i];
    lru_unlink(st, i);
    b->older = -1; b->newer = st->oldest;
    if (st->oldest >= 0) st->box[st->oldest].older = i; else st->newest = i;
    st->oldest = i;
    b->listed = 1;
}

static void key_drop(scache_t *st, int i) {
    cbox_t *b = &st->box[i];
    if (!b->keyed) return;
    int *link = &st->bucket[cache_hash(b->data, b->size, b->format, b->freq)];
    while (*link >= 0 && *link != i) link = &st->box[*link].chain;
    if (*link == i) *link = b->chain;
    b->chain = -1;
    b->keyed = 0;
}

static void key_set(scache_t *st, int i, const void *data, int size, int format, int freq) {
    cbox_t *b = &st->box[i];
    key_drop(st, i);
    b->data = data; b->size = size; b->format = format; b->freq = freq;
    print_of(data, size, b->print);
    unsigned h = cache_hash(data, size, format, freq);
    b->chain = st->bucket[h];
    st->bucket[h] = i;
    b->keyed = 1;
}

/* its buffers go, as far as nothing holds them any more */
static void cache_release(scache_t *st) {
    int16_t **gone = st->n > 0 ? (int16_t **) calloc((size_t) st->n, sizeof(int16_t *)) : NULL;
    pthread_mutex_lock(&g_lock);
    for (int i = 0; i < st->n; i++) {
        int id = st->box[i].buf;
        if (!buf_live(id) || attached_to(id)) continue;
        int16_t *pcm = buffer_delete_locked(id);
        if (gone) gone[i] = pcm;
        else free(pcm);
    }
    pthread_mutex_unlock(&g_lock);
    if (gone) {
        for (int i = 0; i < st->n; i++) free(gone[i]);
        free(gone);
    }
    free(st->box);
    memset(st, 0, sizeof(*st));
}

/* under g_cache_lock */
static scache_t *cache_state(const void *game) {
    const void *names = *(const void *const *) ((const char *) game + 8);
    scache_t *spare = NULL;
    for (int i = 0; i < CACHE_STATES; i++) {
        scache_t *st = &g_caches[i];
        if (st->game == game) {
            if (st->names == names) return st;
            cache_release(st);                  /* a new object where an old one was */
            spare = st;
            break;
        }
        if (!st->game && !spare) spare = st;
    }
    if (!spare) return NULL;
    int cap = *(const int *) game;
    spare->game = game;
    spare->names = names;
    spare->cap = cap > 0 ? cap : 500;
    spare->newest = spare->oldest = -1;
    for (int h = 0; h < CACHE_HASH; h++) spare->bucket[h] = -1;
    return spare;
}

/* under both locks: a new box with a fresh buffer name, not yet in the order */
static int box_new(scache_t *st) {
    if (st->n == st->room) {
        int room = st->room ? st->room * 2 : 64;
        cbox_t *grown = (cbox_t *) realloc(st->box, sizeof(cbox_t) * (size_t) room);
        if (!grown) return -1;
        st->box = grown;
        st->room = room;
    }
    int id = gen_buffer_locked();
    if (!id) return -1;
    int i = st->n++;
    cbox_t *b = &st->box[i];
    memset(b, 0, sizeof(*b));
    b->buf = id;
    b->newer = b->older = b->chain = -1;
    return i;
}

/*
 * Under g_lock: which buffers may not be taken. 1 - a playing or paused source
 * holds it, or it sits in a queue; 2 - only stopped sources hold it, and they
 * can let go; 0 - nobody.
 */
static int busy_map_locked(void) {
    if (g_busy_cap < g_buf_cap) {
        unsigned char *m = (unsigned char *) realloc(g_busy, (size_t) g_buf_cap);
        if (!m) return 0;
        g_busy = m;
        g_busy_cap = g_buf_cap;
    }
    memset(g_busy, 0, (size_t) g_buf_cap);
    for (int i = 1; i < g_src_cap; i++) {
        const alsrc_t *s = &g_src[i];
        if (!s->used) continue;
        int held = s->state == AL_PLAYING || s->state == AL_PAUSED;
        if (buf_live(s->buffer)) {
            if (held) g_busy[s->buffer] = 1;
            else if (!g_busy[s->buffer]) g_busy[s->buffer] = 2;
        }
        if (!s->q) continue;
        for (int k = 0; k < s->qn; k++) if (buf_live(s->q->queue[k])) g_busy[s->q->queue[k]] = 1;
        for (int k = 0; k < s->dn; k++) if (buf_live(s->q->done[k])) g_busy[s->q->done[k]] = 1;
    }
    return 1;
}

/* under g_lock: the stopped sources holding this buffer let go of it */
static int unbind_locked(int id) {
    int n = 0;
    for (int i = 1; i < g_src_cap; i++) {
        alsrc_t *s = &g_src[i];
        if (!s->used || s->buffer != id || s->state == AL_PLAYING || s->state == AL_PAUSED) continue;
        s->buffer = 0;
        s->pos = 0;
        s->unbound = 1;
        n++;
    }
    return n;
}

#if ISAAC_DIAG
static void census_cache(scache_t *st, int how, int unbound);
#endif

/*
 * The manager's "buffer for this sound", in place of 0x8d09dc. Reached from the
 * game through the trampoline at 0x8dab88 with the game's own arguments:
 * x0 the cache object, x1 the PCM, w2 its size, w3 its format, w4 its rate.
 * Returns the buffer name, as the game's does.
 */
unsigned shim_sound_cache_get(void *game, const void *data, int size, int format, int freq) {
    pthread_mutex_lock(&g_cache_lock);
    scache_t *st = cache_state(game);
    if (!st) {
        /* no room for another state (never with the game's single cache): its own code answers */
        pthread_mutex_unlock(&g_cache_lock);
        return ((unsigned (*)(void *, const void *, int, int, int))
                (shim_image_base() + GAME_CACHE_GET))(game, data, size, format, freq);
    }
    unsigned h = cache_hash(data, size, format, freq);
    for (int i = st->bucket[h]; i >= 0; i = st->box[i].chain) {
        cbox_t *b = &st->box[i];
        if (b->data != data || b->size != size || b->format != format || b->freq != freq) continue;
        uint64_t now[3];
        print_of(data, size, now);
        pthread_mutex_lock(&g_lock);
        int alive = buf_live(b->buf);
        pthread_mutex_unlock(&g_lock);
        if (alive && !memcmp(now, b->print, sizeof(now))) {
            st->hits++;
            lru_push_newest(st, i);
            unsigned id = (unsigned) b->buf;
            pthread_mutex_unlock(&g_cache_lock);
            return id;
        }
        /* the address holds another sound now (or the buffer is gone): the key is wrong */
        key_drop(st, i);
        lru_push_oldest(st, i);
        st->stale++;
        DIAG(census_cache(st, 3, 0));
        break;
    }
    st->misses++;
    /* the sound is made ready before anything is taken */
    int16_t *pcm = NULL;
    int frames = 0, channels = 1;
    if (pcm_make(format, data, size, &pcm, &frames, &channels) != AL_NO_ERROR) {
        st->failed++;
        pthread_mutex_unlock(&g_cache_lock);
        return 0;
    }
    int i = -1, how = 0, unbound = 0;
    pthread_mutex_lock(&g_lock);
    if (st->n < st->cap) {
        i = box_new(st);                                  /* room left: a new box */
    } else if (busy_map_locked()) {
        /* full: the least recently used box that no playing or paused source holds */
        for (int k = st->oldest; k >= 0; k = st->box[k].newer) {
            int id = st->box[k].buf;
            if (!buf_live(id)) {
                /* its buffer was deleted under it: a fresh name for the box */
                int fresh = gen_buffer_locked();
                if (!fresh) continue;
                st->box[k].buf = fresh;
                i = k; how = 1;
                break;
            }
            if (g_busy[id] == 1) continue;
            if (g_busy[id] == 2) unbound = unbind_locked(id);
            i = k; how = 1;
            break;
        }
    }
    if (i < 0) { i = box_new(st); how = 2; }              /* every box sounds: one over the capacity */
    if (i < 0) {
        pthread_mutex_unlock(&g_lock);
        free(pcm);
        st->failed++;
        pthread_mutex_unlock(&g_cache_lock);
        return 0;
    }
    int16_t *old = buf_commit_locked(st->box[i].buf, pcm, frames, channels, freq, data);
    pthread_mutex_unlock(&g_lock);
    free(old);
    key_set(st, i, data, size, format, freq);
    lru_push_newest(st, i);
    if (how == 1) { st->evicted++; st->unbound += unbound; }
    else if (how == 2) st->over++;
    DIAG(census_cache(st, how, unbound));
    unsigned id = (unsigned) st->box[i].buf;
    pthread_mutex_unlock(&g_cache_lock);
    return id;
}

/*
 * The pool's size and the cache's branch, together or not at all. Every word
 * is checked before anything is written; one that is not the expected one and
 * the game keeps its own pool and cache. Called by the loader right after the
 * other image patches, before any of the game's code has run - the pool is
 * made later, by the manager's setup 0x8c9188.
 */
#if ISAAC_DIAG
static void census_hooks(void);
#endif

void shim_audio_patch(unsigned char *base) {
    uint32_t *size_word = (uint32_t *) (base + POOL_SIZE_WORD);
    uint32_t *branch = (uint32_t *) (base + CACHE_BRANCH);
    uint32_t *check = (uint32_t *) (base + PURCHASES_CHECK);
    uint32_t *tramp = (uint32_t *) (base + CACHE_TRAMPOLINE);
    const char *why = NULL;
    if (*size_word != 0x52800801u) why = "the pool's size word";                     /* movz w1, #0x40 */
    else if (*branch != 0x14001D04u) why = "the cache's branch";                     /* b 0x8d09dc */
    /* the trampoline sits in the purchases check's body: dead only once the check answers at once */
    else if (check[0] != 0x52800020u || check[1] != 0xD65F03C0u) why = "the purchases check (its body is alive)";
    /* ret; mov x21, x0 (the body's landing pad, dead with it); mov x0, x20; bl 0x900ae8 */
    else if (tramp[0] != 0xD65F03C0u || tramp[1] != 0xAA0003F5u || tramp[2] != 0xAA1403E0u
             || tramp[3] != 0x940097D5u) why = "the dead words at 8dab88";
    if (why) {
        LOG("sound: %s is not the one expected - the game keeps its own pool and cache "
            "(%08x %08x; %08x %08x; %08x %08x %08x %08x)", why, *size_word, *branch, check[0], check[1],
            tramp[0], tramp[1], tramp[2], tramp[3]);
        DIAG(snd_line("the pool and the cache stay the game's: %s is not the one expected", why));
    } else {
        shim_hook(tramp, (void *) shim_sound_cache_get);
        *branch = 0x1400456Fu;                  /* b 0x8dab88 */
        *size_word = 0x52840001u;               /* movz w1, #0x2000 */
        __builtin___clear_cache((char *) tramp, (char *) (tramp + 4));
        __builtin___clear_cache((char *) branch, (char *) (branch + 1));
        __builtin___clear_cache((char *) size_word, (char *) (size_word + 1));
        g_patched = 1;
        LOG("sound: a source for every sound (pool of 8192), and the cache is ours (through 8dab88)");
        DIAG(snd_line("the pool is 8192 and the sound cache is ours (through 8dab88)"));
    }
#if ISAAC_DIAG
    census_hooks();
#endif
}

/*
 * Quiet while the app is away, and loud again when it comes back.
 *
 * AAudio has a state machine and it does not forgive: a stream that was PAUSED
 * cannot be started again. The documented road back is PAUSED -> requestFlush
 * -> FLUSHED -> requestStart, and requestPause only ASKS - the stream sits in
 * PAUSING until the device thread has stopped, so a start issued straight
 * after the pause is refused and the stream stays silent for good. That is the
 * "sound disappears after minimising" the Pixel tester reported.
 *
 * So the road is walked properly, with a wait at each turn; and if anything
 * still refuses, the stream is marked lost and shim_audio_tick builds a new
 * one on the next frame rather than leaving the game mute.
 */
#define AUDIO_WAIT_NS (300L * 1000L * 1000L)     /* long enough for a device thread to stop */

#if ISAAC_DIAG
static void census_summary(const char *why);
#endif

void shim_audio_pause(int on) {
#if ISAAC_DIAG
    if (on) census_summary("going to the background");
    pthread_mutex_lock(&g_lock);
    meter_event(on ? 'A' : 'a', 0, 0.0f, 0.0f, 0, 0);
    pthread_mutex_unlock(&g_lock);
#endif
    pthread_mutex_lock(&g_stream_lock);
    g_paused = on;
    if (!on) {
        g_mix_ns = now_ns();                    /* the mixer gets its two seconds to come back */
        g_last_open_try_ns = 0;
        g_life_next_ns = 0;                     /* no stream? the life thread opens one at once */
    }
    if (!g_stream) {
        pthread_mutex_unlock(&g_stream_lock);
        if (!on) life_ask();
        return;
    }
    if (on) {
        AAudioStream_requestPause(g_stream);
        pthread_mutex_unlock(&g_stream_lock);
        return;
    }
    /* back from the background on the ordinary mixer: the fast one is tried again (see g_perf_reverts) */
    if (g_perf_fallback && g_perf == AAUDIO_PERFORMANCE_MODE_NONE && g_perf_reverts < PERF_REVERTS_MAX) {
        g_perf_reverts++;
        g_perf_fallback = 0;
        g_perf = AAUDIO_PERFORMANCE_MODE_LOW_LATENCY;
        g_cap_ns = 0;
        g_stream_lost = 1;
        LOG("audio: back from the background - the fast mixer is tried again (%d of %d)",
            g_perf_reverts, PERF_REVERTS_MAX);
        AUDIO_SAY("back from the background: the stream is rebuilt with the fast mixer again (try %d of %d)",
                  g_perf_reverts, PERF_REVERTS_MAX);
        pthread_mutex_unlock(&g_stream_lock);
        life_ask();
        return;
    }
    /*
     * The waits for the device are made OUTSIDE the lock, on a pinned stream:
     * the frame thread's tick never waits for them (it only tries the lock),
     * and the life thread does not close a pinned stream.
     */
    AAudioStream *st = g_stream;
    g_stream_pins++;
    pthread_mutex_unlock(&g_stream_lock);
    aaudio_stream_state_t now = AAudioStream_getState(st), next = now;
    if (now == AAUDIO_STREAM_STATE_PAUSING) {
        AAudioStream_waitForStateChange(st, AAUDIO_STREAM_STATE_PAUSING,
                                        &next, AUDIO_WAIT_NS);
        now = next;
    }
    if (now == AAUDIO_STREAM_STATE_PAUSED) {
        AAudioStream_requestFlush(st);
        AAudioStream_waitForStateChange(st, AAUDIO_STREAM_STATE_FLUSHING,
                                        &next, AUDIO_WAIT_NS);
        now = next;
    }
    pthread_mutex_lock(&g_stream_lock);
    g_stream_pins--;
    if (g_stream == st && !g_paused && now != AAUDIO_STREAM_STATE_STARTED && now != AAUDIO_STREAM_STATE_STARTING
        && AAudioStream_requestStart(st) != AAUDIO_OK) {
        LOG("audio: the stream refused to start from %s - a new one will be built",
            AAudio_convertStreamStateToText(AAudioStream_getState(st)));
        AUDIO_SAY("the stream refused to start again from %s: a new one is built",
                  AAudio_convertStreamStateToText(AAudioStream_getState(st)));
        g_stream_lost = 1;          /* the life thread opens a fresh one */
    }
    int ask = g_stream_lost;
    pthread_mutex_unlock(&g_stream_lock);
    if (ask) life_ask();
}

/* the next wait after an open that failed or a stream that did not live: twice the last, 8 s at most */
static void life_back_off(int badly) {
    if (badly) {
        g_life_backoff_ns = g_life_backoff_ns ? g_life_backoff_ns * 2 : LIFE_BACKOFF_MIN_NS;
        if (g_life_backoff_ns > LIFE_BACKOFF_MAX_NS) g_life_backoff_ns = LIFE_BACKOFF_MAX_NS;
    } else {
        g_life_backoff_ns = LIFE_BACKOFF_MIN_NS;
    }
    g_life_next_ns = now_ns() + g_life_backoff_ns;
}

/*
 * The life thread: closes a lost stream and opens its successor, or opens one
 * where the last open failed - asked by the frame's tick, the pause, the
 * error callback's flag. See THE STREAM'S LIFE OFF THE FRAME THREAD.
 */
static void life_work(void) {
    long now = now_ns();
    pthread_mutex_lock(&g_stream_lock);
    if (g_stream_pins) {
        /* the pause is waiting on this stream outside the lock: not now */
        g_life_deferred++;
        pthread_mutex_unlock(&g_stream_lock);
        g_life_next_ns = now + 50L * 1000L * 1000L;
        life_ask();
        return;
    }
    if (g_stream_lost) {
        g_stream_lost = 0;
        /* a stream that did not live ten seconds: the next rebuild waits longer */
        int short_lived = g_opened_ns && now - g_opened_ns < LIFE_LIVED_NS;
        if (!short_lived) g_life_backoff_ns = 0;    /* it lived: the backoff starts again from the bottom */
        AAudioStream *old = g_stream;
        g_stream = NULL;
        if (old) AAudioStream_close(old);
        g_stream_losses++;
        g_life_rebuilds++;
        AUDIO_SAY("the stream was lost: a new one is opened");
        DIAG(pthread_mutex_lock(&g_lock); meter_event('R', 0, 0.0f, 0.0f, 0, 0); pthread_mutex_unlock(&g_lock));
        audio_start();
        LOG("audio: stream reopened (losses %ld), %s", g_stream_losses, g_stream ? "running" : "DID NOT OPEN");
        life_back_off(!g_stream || short_lived);
    } else if (!g_stream && !g_paused && g_dev_open) {
        /* no stream at all - the last open failed - and the game is playing: try again */
        g_open_retry++;
        audio_start();
        life_back_off(!g_stream);
    }
    pthread_mutex_unlock(&g_stream_lock);
}

static void *life_main(void *arg) {
    (void) arg;
    pthread_setname_np(pthread_self(), "isaac-audio");
    pthread_mutex_lock(&g_life_lock);
    for (;;) {
        while (!g_life_asked) pthread_cond_wait(&g_life_cond, &g_life_lock);
        long wait = g_life_next_ns - now_ns();
        if (wait > 0) {
            /* the backoff: asked again too soon - sleep it out, a little at a time */
            pthread_mutex_unlock(&g_life_lock);
            struct timespec ts = { 0, wait > 100000000L ? 100000000L : wait };
            nanosleep(&ts, NULL);
            pthread_mutex_lock(&g_life_lock);
            continue;
        }
        g_life_asked = 0;
        pthread_mutex_unlock(&g_life_lock);
        life_work();
        pthread_mutex_lock(&g_life_lock);
    }
    return NULL;
}

/* never blocks on the stream: the frame thread calls it */
static void life_ask(void) {
    pthread_mutex_lock(&g_life_lock);
    if (!g_life_up) {
        pthread_t t;
        pthread_attr_t a;
        pthread_attr_init(&a);
        pthread_attr_setdetachstate(&a, PTHREAD_CREATE_DETACHED);
        g_life_up = pthread_create(&t, &a, life_main, NULL) == 0;
        pthread_attr_destroy(&a);
        if (!g_life_up) LOG("audio: the stream's life thread did not start");
    }
    g_life_asked = 1;
    pthread_cond_signal(&g_life_cond);
    pthread_mutex_unlock(&g_life_lock);
}

/* once a frame from the game thread: reopen the stream if the system dropped it */
static void tell_dry(void) {
    if (!g_dry_n || !shim_report_active()) return;
    static int told;
    long now = now_ns();
    pthread_mutex_lock(&g_lock);
    int k = 0;
    while (k < g_dry_n) {
        /* written once the game restarted the source, or gave up on it for two seconds */
        if (g_dry[k].restart_ms < 0 && now - g_dry[k].at_ns < 2000000000L) { k++; continue; }
        if (told < 60) {
            told++;
            char line[200];
            snprintf(line, sizeof(line),
                     "source %d ran dry (%ld so far): the last refill held %d ms of sound and came %ld ms "
                     "before; %s %ld ms",
                     g_dry[k].src, g_dry_total, g_dry[k].held_ms, g_dry[k].since_refill_ms,
                     g_dry[k].restart_ms < 0 ? "not restarted within" : "restarted by the game after",
                     g_dry[k].restart_ms < 0 ? 2000L : g_dry[k].restart_ms);
            report_line("audio", line);
        }
        for (int j = k + 1; j < g_dry_n; j++) g_dry[j - 1] = g_dry[j];
        g_dry_n--;
    }
    pthread_mutex_unlock(&g_lock);
}

/*
 * MEASUREMENT, for the report: the headroom. The least sound any playing music
 * queue holds right now - a frame longer than that leaves it dry - and the
 * stream's own underrun count, which is the other way sound drops out: the
 * system asked the mixer for data and the mixer was late.
 */
void shim_audio_headroom(int *held_ms, int *queues, int *xruns) {
    int least = -1, n = 0;
    pthread_mutex_lock(&g_lock);
    for (int i = 1; i < g_src_cap; i++) {
        alsrc_t *s = &g_src[i];
        if (!s->used || s->state != AL_PLAYING || !s->queued_ns) continue;
        int h = held_ms_of(s);
        n++;
        if (least < 0 || h < least) least = h;
    }
    pthread_mutex_unlock(&g_lock);
    if (held_ms) *held_ms = least;
    if (queues) *queues = n;
    if (xruns) {
        *xruns = -1;
        if (pthread_mutex_trylock(&g_stream_lock) == 0) {
            if (g_stream) *xruns = (int) AAudioStream_getXRunCount(g_stream);
            pthread_mutex_unlock(&g_stream_lock);
        }
    }
}

/*
 * MEASUREMENT: is the music refilled by the thread that draws the frames? If it
 * is, a long frame can starve it; if the refill comes from a thread of its own,
 * a long frame cannot, and the dropout has another cause. 1 yes, 0 no, -1 no
 * refill seen yet.
 */
int shim_audio_refilled_here(void) {
    int tid = g_queue_tid;
    return tid ? tid == gettid() : -1;
}

#if ISAAC_DIAG
static void census_tick(void);
static void selftest_tick(void);
#endif

void shim_audio_tick(void) {
    tell_dry();
    watch_audible();
    /*
     * The frame thread never waits for the stream: while the life thread
     * opens one or the pause waits for the device, this frame simply does not
     * look (THE STREAM'S LIFE OFF THE FRAME THREAD).
     */
    if (pthread_mutex_trylock(&g_stream_lock) != 0) goto done;
    long now = now_ns();
    /*
     * A DISCONNECT NOBODY TOLD (05.10.2026). On some Android 9 and early 10
     * builds the disconnect never reaches an MMAP stream's error callback
     * (Oboe issue #252, its tech note "Disconnected Streams"), and on 11 it
     * comes as a timeout instead (issuetracker 173928197). The stream then sits
     * DISCONNECTED and the mixer is never called again - and the watchdog
     * below looks only at a stream that says STARTED. A stream that has said
     * DISCONNECTED for half a second with no word from its callback (which
     * normally follows the state within milliseconds) is rebuilt all the same.
     * The half second is timed from the moment THIS stream first said so
     * (g_disc_ns starts again with every open, audio_start): it used to carry
     * over, and a fresh stream that came back DISCONNECTED - the route still
     * gone - was rebuilt on every single frame (hotfix 1.72).
     */
    if (!g_stream_lost && g_stream && !g_paused
        && AAudioStream_getState(g_stream) == AAUDIO_STREAM_STATE_DISCONNECTED) {
        if (!g_disc_ns) g_disc_ns = now;
        else if (now - g_disc_ns > OPEN_RETRY_NS) {
            g_silent_disconnects++;
            g_stream_lost = 1;
            g_disc_ns = 0;
            AUDIO_SAY("the stream is DISCONNECTED and its error callback never came (%ld so far): rebuilt",
                      g_silent_disconnects);
        }
    } else {
        g_disc_ns = 0;
    }
    if (!g_stream_lost && g_stream && !g_paused) xrun_watch(now);
    if (!g_stream_lost && g_stream && !g_paused) {
        /*
         * THE WATCHDOG: a stream that says it is started and has not asked the
         * mixer for data in two seconds, while sources are playing, is dead
         * without having said so. It is rebuilt like a dropped one.
         */
        long last = g_mix_ns;
        if (last && now - last > WATCHDOG_NS
            && AAudioStream_getState(g_stream) == AAUDIO_STREAM_STATE_STARTED) {
            int playing = 0;
            pthread_mutex_lock(&g_lock);
            for (int i = 1; i < g_src_cap; i++)
                if (g_src[i].used && g_src[i].state == AL_PLAYING) { playing = 1; break; }
            pthread_mutex_unlock(&g_lock);
            if (playing) {
                g_watchdog_rebuilds++;
                g_stream_lost = 1;
                LOG("audio: the stream sat silent for %ld ms with sounds playing: rebuilt (%ld so far)",
                    (now - last) / 1000000L, g_watchdog_rebuilds);
                AUDIO_SAY("ALARM: the stream sat silent for %ld ms with sounds playing - AAudio went quiet "
                          "without an error; rebuilt (%ld so far)", (now - last) / 1000000L, g_watchdog_rebuilds);
            } else {
                g_mix_ns = now;        /* nothing to play: nothing to expect */
            }
        }
    }
    /* a lost stream, or none at all while the game plays: the life thread's work, not this frame's */
    int ask = g_stream_lost || (!g_stream && !g_paused && g_dev_open);
    pthread_mutex_unlock(&g_stream_lock);
    if (ask && now >= g_life_next_ns) life_ask();
done:
#if ISAAC_DIAG
    census_tick();
    selftest_tick();
    meter_tick();
#endif
    return;
}

/* ======================================================================= */
/*  The report: the census, the error model's shadow, the self-test.       */
/*  All of it is the diagnostic build's alone.                              */
/* ======================================================================= */

/*
 * The two hooks' handlers (trace.c, marks '<' and '|'). They exist in every
 * build so the trace table links; the build we play on places no hooks and
 * they do nothing there.
 */
void shim_audio_obj_plays(unsigned long obj);
void shim_audio_obj_stops(unsigned long obj);
void shim_audio_sfx_asked(unsigned long id, float volume);

#if !ISAAC_DIAG
void shim_audio_obj_plays(unsigned long obj) { (void) obj; }
void shim_audio_obj_stops(unsigned long obj) { (void) obj; }
void shim_audio_sfx_asked(unsigned long id, float volume) { (void) id; (void) volume; }
void shim_audio_stress_allow(int on) { (void) on; }      /* no self-test in this build */
#else

static int g_selftest_tid;              /* while the self-test runs: its calls stay out of the census */
static int census_off(void) { return g_selftest_tid && gettid() == g_selftest_tid; }

static int edition(void) {
    unsigned long b = shim_image_base();
    return b ? *(const int *) (b + 0xabec34) : -1;
}

static unsigned long game_root(void) {
    unsigned long b = shim_image_base();
    return b ? *(const unsigned long *) (b + GAME_MANAGER) : 0;
}

static int in_menu(void) {
    unsigned long g = game_root();
    return g && *(const int *) (g + 0x204c0) == MENU_STATE;
}

/* ---- the error model: Apple's one shared code, kept in the shadow ---- */

static pthread_mutex_t g_sh_lock = PTHREAD_MUTEX_INITIALIZER;
static int g_sh_err, g_sh_tid;
static unsigned long g_sh_from;
static long g_sh_reads, g_sh_diverged;

static void shadow_set(int e, unsigned long from) {
    if (census_off()) return;
    pthread_mutex_lock(&g_sh_lock);
    if (g_sh_err == AL_NO_ERROR) { g_sh_err = e; g_sh_from = img_off(from); g_sh_tid = gettid(); }
    pthread_mutex_unlock(&g_sh_lock);
}

static void shadow_read(int mine, unsigned long from) {
    if (census_off()) return;
    int tid = gettid();
    pthread_mutex_lock(&g_sh_lock);
    int shared = g_sh_err, set_tid = g_sh_tid;
    unsigned long set_from = g_sh_from;
    g_sh_err = AL_NO_ERROR;
    g_sh_reads++;
    int differ = shared != mine;
    long n = differ ? ++g_sh_diverged : 0;
    pthread_mutex_unlock(&g_sh_lock);
    if (differ && n <= 12)
        snd_line("error model: tid %d%s at +%lx reads %#x; Apple's one shared code would give %#x, "
                 "set by tid %d%s at +%lx (%ld so far)", tid, tid == g_frame_tid ? "*" : "", img_off(from),
                 mine, shared, set_tid, set_tid && set_tid == g_frame_tid ? "*" : "", set_from, n);
}

/* ---- who a sound object is: its id in the effects' manager (Repentance) ---- */

static void sound_name(unsigned long obj, char *out, size_t n) {
    snprintf(out, n, "object %lx", obj);
    unsigned long g = game_root();
    if (!obj || !g || edition() != 3) return;
    unsigned long mgr = g + 0x55ea0;
    unsigned long begin = *(const unsigned long *) mgr, end = *(const unsigned long *) (mgr + 8);
    if (!begin || end <= begin) return;
    long count = (long) ((end - begin) / 0x1f0);
    for (long i = 0; i < count; i++) {
        unsigned long rec = begin + (unsigned long) i * 0x1f0;
        int variants = *(const int *) (rec + 0x1c8);
        for (int k = 0; k < variants && k < 7; k++)
            if (*(const unsigned long *) (rec + 0x20 + 0x40 * (unsigned long) k) == obj) {
                snprintf(out, n, "#%ld", i);
                return;
            }
    }
}

/* the sound object whose PCM sits at this address, among the manager's */
static unsigned long object_with_pcm(const void *pcm) {
    unsigned long b = shim_image_base();
    if (!b || !pcm) return 0;
    unsigned long begin = *(const unsigned long *) (b + SND_MANAGER + 0x18);
    unsigned long end = *(const unsigned long *) (b + SND_MANAGER + 0x20);
    for (unsigned long p = begin; p && p < end; p += 8) {
        unsigned long o = *(const unsigned long *) p;
        if (o && *(const void *const *) (o + 0x48) == pcm) return o;
    }
    return 0;
}

/* ---- the census's counters ---- */

typedef struct { unsigned long off; long n; } caller_t;

static long caller_add(caller_t *t, int n, unsigned long off) {
    for (int i = 0; i < n; i++) {
        if (t[i].n && t[i].off == off) return ++t[i].n;
        if (!t[i].n) { t[i].off = off; t[i].n = 1; return 1; }
    }
    return -1;
}

static long g_cross, g_wrong, g_plays_game, g_play_unbound, g_ring, g_minus_one, g_pour;
static long g_buffer_refused, g_data_refused_census;
static caller_t g_buffer_refusers[8], g_data_refusers[8], g_pitch_callers[20];
static float g_pitch_lo, g_pitch_hi, g_x_lo, g_x_hi;
static long g_pitch_n, g_pitch_out, g_pos_n, g_pos_off;
static __thread unsigned long t_playing_obj;   /* set at 0x8a2eac's door, read at its alSourcePlay */

static void census_minus_one(unsigned long from) {
    if (census_off()) return;
    if (++g_minus_one == 1)
        snd_line("the game's pool had no source: alSourcei on the name -1 from +%lx (answered AL_INVALID_NAME)",
                 img_off(from));
}

static void census_buffer_refused(unsigned id, int state, int had, int wanted, unsigned long from) {
    if (census_off()) return;
    g_buffer_refused++;
    long n = caller_add(g_buffer_refusers, 8, img_off(from));
    if (n == 1)
        snd_line("AL_BUFFER refused: source %u is %s with buffer %d, buffer %d asked for from +%lx "
                 "(%ld refusals so far)", id, watch_state(state), had, wanted, img_off(from), g_buffer_refused);
}

static void census_data_refused(unsigned id, int holder, int state, unsigned long from) {
    if (census_off()) return;
    g_data_refused_census++;
    long n = caller_add(g_data_refusers, 8, img_off(from));
    if (n == 1 || n == 10 || n == 100)
        snd_line("alBufferData refused (#%ld from +%lx): buffer %u is attached to source %d, which is %s "
                 "(%ld refusals so far)", n, img_off(from), id, holder, watch_state(state), g_data_refused_census);
}

static void census_pour_over(unsigned long from) {
    if (census_off() || img_off(from) != AT_POUR_OVER) return;
    long n = ++g_pour;
    if (n == 1 || n == 10 || n == 100)
        snd_line("sound cache: the game's own cache is full and poured sound #%ld over its oldest buffer", n);
}

static void census_pitch(float value, unsigned long from) {
    if (census_off()) return;
    if (!g_pitch_n || value < g_pitch_lo) g_pitch_lo = value;
    if (!g_pitch_n || value > g_pitch_hi) g_pitch_hi = value;
    g_pitch_n++;
    if (value >= 0.5f && value <= 2.0f) return;
    g_pitch_out++;
    if (caller_add(g_pitch_callers, 20, img_off(from)) == 1)
        snd_line("pitch %.3f asked for from +%lx, outside 0.5..2.0 (%ld such so far)",
                 (double) value, img_off(from), g_pitch_out);
}

static void census_position(float x) {
    if (census_off()) return;
    if (!g_pos_n || x < g_x_lo) g_x_lo = x;
    if (!g_pos_n || x > g_x_hi) g_x_hi = x;
    g_pos_n++;
    if (x != 0.0f) g_pos_off++;
}

static void census_ring(unsigned long from) {
    if (!census_off() && img_off(from) == AT_RING_ASK) g_ring++;
}

/* alSourcePlay on a name that is not a live source: OpenAL refuses it, and the sound is not heard */
static long g_play_refused;
static caller_t g_play_refusers[8];

static void census_play_refused(unsigned id, unsigned long from, unsigned long from2) {
    if (census_off()) return;
    g_play_refused++;
    if (caller_add(g_play_refusers, 8, img_off(from2)) == 1)
        snd_line("PLAY REFUSED: alSourcePlay on %d, not a live source, from +%lx<+%lx - that sound is not heard "
                 "(%ld so far)", (int) id, img_off(from), img_off(from2), g_play_refused);
}

/*
 * The fates (THE FATE OF A PLAY), once a frame: the UNHEARD plays one by one,
 * the first twenty, and every new place in the game that cuts sounds short.
 */
static void fate_tick(void) {
    fate_ev_t ev[FATE_UNHEARD_RING];
    int n = 0;
    struct { unsigned long from, from2; long n; } cut[FATE_CUTTERS];
    int nc = 0;
    long total_unheard, total_cut, plays;
    pthread_mutex_lock(&g_lock);
    if (g_fate_ring_told < g_fate_ring_n - FATE_UNHEARD_RING) g_fate_ring_told = g_fate_ring_n - FATE_UNHEARD_RING;
    while (g_fate_ring_told < g_fate_ring_n) ev[n++] = g_fate_ring[g_fate_ring_told++ % FATE_UNHEARD_RING];
    for (int i = 0; i < FATE_CUTTERS; i++)
        if (g_cutters[i].n && !g_cutters[i].told) {
            g_cutters[i].told = 1;
            cut[nc].from = g_cutters[i].from; cut[nc].from2 = g_cutters[i].from2; cut[nc].n = g_cutters[i].n;
            nc++;
        }
    total_unheard = g_fate_unheard; total_cut = g_fate_cut; plays = g_fate_plays;
    pthread_mutex_unlock(&g_lock);
    static int told_unheard;
    int burst = g_burst_now, rate = g_rate > 0 ? g_rate : 48000;
    for (int i = 0; i < n && told_unheard < 20; i++, told_unheard++) {
        const fate_ev_t *e = &ev[i];
        snd_line("UNHEARD: effect source %d (buffer %d, %d ms of sound) played from +%lx was %s %ld us later from "
                 "+%lx<+%lx, before the mixer read a frame of it; the mixer reads %d frames (%.1f ms) a call; "
                 "%ld unheard of %ld effect plays", e->src, e->buffer, e->len_ms, e->pfrom,
                 e->how == 'r' ? "replayed" : e->how == 'd' ? "deleted" : "stopped",
                 (e->end_ns - e->play_ns) / 1000L, e->from, e->from2, burst, burst * 1000.0 / rate,
                 total_unheard, plays);
    }
    for (int i = 0; i < nc; i++)
        snd_line("CUT SHORT: the game cut a sounding effect from +%lx<+%lx (first seen; %ld cut there so far, "
                 "%ld cuts of %ld effect plays in all)", cut[i].from, cut[i].from2, cut[i].n, total_cut, plays);
}

/* alSourcePlay: whose sound goes into the source, and is it that sound's */
static void census_play(unsigned id, unsigned long from) {
    if (census_off() || !shim_report_active()) return;
    unsigned long off = img_off(from);
    unsigned long obj = 0;
    if (off == AT_PLAY_CALL) { obj = t_playing_obj; t_playing_obj = 0; g_plays_game++; }
    int buffer = 0, queued = 0, unbound = 0, found = 0;
    const void *origin = NULL;
    pthread_mutex_lock(&g_lock);
    alsrc_t *s = src_of(id);
    if (s) {
        found = 1;
        if (obj) s->owner = obj;
        buffer = s->buffer;
        queued = s->qn + s->dn;
        unbound = s->unbound;
        if (buf_live(buffer)) origin = g_buf[buffer].origin;
    }
    pthread_mutex_unlock(&g_lock);
    if (!found) return;
    if (!buffer && !queued && off != AT_PLAY_CALL) {
        long n = ++g_play_unbound;
        if (n <= 10)
            snd_line("alSourcePlay on source %u with no buffer from +%lx%s (%ld so far)", id, off,
                     unbound ? " - the sound cache had taken its buffer" : "", n);
    }
    if (!obj || !origin) return;
    const void *want = *(const void *const *) (obj + 0x48);
    if (origin == want) return;
    long n = ++g_wrong;
    if (n > 20) return;
    char who[40], other[40];
    sound_name(obj, who, sizeof(who));
    unsigned long holder = object_with_pcm(origin);
    if (holder) sound_name(holder, other, sizeof(other));
    else snprintf(other, sizeof(other), "poured from %p", origin);
    snd_line("WRONG SOUND: sound %s got buffer %d holding sound %s (%ld so far)", who, buffer, other, n);
}

/* '<' at 0x8a2eac: which sound object is about to play */
void shim_audio_obj_plays(unsigned long obj) {
    if (census_off()) return;
    t_playing_obj = obj;
}

/* '|' at 0x8a30e0: a sound object stops the source it last used - is the sound in it still its own? */
void shim_audio_obj_stops(unsigned long obj) {
    if (census_off() || !obj) return;
    unsigned src = *(const unsigned *) (obj + 0x58);
    unsigned long other = 0;
    int state = 0;
    pthread_mutex_lock(&g_lock);
    alsrc_t *s = src_of(src);
    if (s && (s->state == AL_PLAYING || s->state == AL_PAUSED) && s->owner && s->owner != obj) {
        other = s->owner;
        state = s->state;
    }
    pthread_mutex_unlock(&g_lock);
    if (!other) return;
    long n = ++g_cross;
    if (n > 20) return;
    char who[40], whose[40];
    sound_name(obj, who, sizeof(who));
    sound_name(other, whose, sizeof(whose));
    snd_line("CROSS-TALK: sound %s stopped source %u %s sound %s (%ld so far)", who, src,
             state == AL_PAUSED ? "holding the paused" : "playing", whose, n);
}

static void census_hooks(void) {
    if (!shim_report_active()) return;
    shim_trace_add(FN_SND_PLAY, "<a sound object plays");
    shim_trace_add(FN_SND_STOP, "|a sound object stops its source");
}

static const void *live_cache(void) {
    unsigned long b = shim_image_base();
    return b ? *(const void *const *) (b + SND_MANAGER + 0x88) : NULL;
}

/* the live cache's thresholds and events (under g_cache_lock) */
static void census_cache(scache_t *st, int how, int unbound) {
    if (st->game != live_cache() || census_off()) return;
    if (st->n >= st->cap * 9 / 10 && !(st->said & 1)) {
        st->said |= 1;
        snd_line("sound cache: %d of %d boxes filled (%ld hits, %ld misses)", st->n, st->cap, st->hits, st->misses);
    }
    if (st->n >= st->cap && !(st->said & 2)) {
        st->said |= 2;
        snd_line("sound cache: full, %d of %d boxes (%ld hits, %ld misses)", st->n, st->cap, st->hits, st->misses);
    }
    long e = st->evicted;
    if (how == 1 && (e == 1 || e == 10 || e == 100 || e == 1000))
        snd_line("sound cache: eviction #%ld - the least recently used box no playing sound holds took "
                 "a new sound; %d stopped source(s) let go of its buffer", e, unbound);
    if (how == 2 && (st->over == 1 || st->over == 10 || st->over == 100))
        snd_line("sound cache: every box is held by a playing sound - box %d made over the capacity (#%ld)",
                 st->n, st->over);
    if (how == 3 && (st->stale == 1 || st->stale == 10 || st->stale == 100))
        snd_line("sound cache: an address holds another sound than its key was made from - refilled (#%ld)",
                 st->stale);
}

/* ---- the summary: on every trip to the background and every ten minutes ---- */

static void census_summary(const char *why) {
    if (!shim_report_active()) return;
    unsigned long b = shim_image_base();
    if (!b) return;
    const void *cache = live_cache();
    const int *pool = *(const int *const *) (b + SND_MANAGER + 0x90);
    unsigned long begin = *(const unsigned long *) (b + SND_MANAGER + 0x18);
    unsigned long end = *(const unsigned long *) (b + SND_MANAGER + 0x20);
    int objects = (begin && end > begin) ? (int) ((end - begin) / 8) : 0;
    int filled = -1, cap = -1;
    long hits = 0, misses = 0, evicted = 0, unbound = 0, over = 0, stale = 0;
    pthread_mutex_lock(&g_cache_lock);
    for (int i = 0; i < CACHE_STATES; i++)
        if (cache && g_caches[i].game == cache) {
            scache_t *st = &g_caches[i];
            filled = st->n; cap = st->cap;
            hits = st->hits; misses = st->misses; evicted = st->evicted;
            unbound = st->unbound; over = st->over; stale = st->stale;
        }
    pthread_mutex_unlock(&g_cache_lock);
    if (filled < 0 && cache) { filled = ((const int *) cache)[1]; cap = ((const int *) cache)[0]; }
    snd_summary("sound census (%s): cache %d/%d (%s), hits %ld, misses %ld, evictions %ld, let go %ld, "
                "over capacity %ld, stale keys %ld, game pour-overs %ld; pool %d/%d, ring hand-outs %ld, "
                "name -1 %ld, sound objects %d", why, filled, cap, g_patched ? "ours" : "the game's",
                hits, misses, evicted, unbound, over, stale, g_pour,
                pool ? pool[1] : -1, pool ? pool[0] : -1, g_ring, g_minus_one, objects);
    long mix_worst_us = g_mix_worst_ns / 1000, mix_budget_us = g_mix_budget_ns / 1000;
    snd_summary("sound census (%s): CROSS-TALK %ld, WRONG SOUND %ld, effect plays %ld, played with no buffer %ld; "
                "AL_BUFFER refused %ld, alBufferData refused %ld, deletes refused %ld; peak voices %d, "
                "worst mix %ld us of %ld, clipped %ld; pitch %.3f..%.3f (%ld of %ld outside 0.5..2.0); "
                "position x %.2f..%.2f (%ld of %ld off centre); error model %ld of %ld reads differ; "
                "watchdog %ld, stream losses %ld, failed opens %ld, paused %d",
                why, g_cross, g_wrong, g_plays_game, g_play_unbound, g_buffer_refused, g_data_refused_census,
                g_delete_refused, g_peak_voices, mix_worst_us, mix_budget_us, g_clipped_total,
                (double) g_pitch_lo, (double) g_pitch_hi, g_pitch_out, g_pitch_n,
                (double) g_x_lo, (double) g_x_hi, g_pos_off, g_pos_n, g_sh_diverged, g_sh_reads,
                g_watchdog_rebuilds, g_stream_losses, g_open_fail, g_paused);
    long fp, fe, fc, fu, fl, fz;
    pthread_mutex_lock(&g_lock);
    fp = g_fate_plays; fe = g_fate_end; fc = g_fate_cut; fu = g_fate_unheard; fl = g_fate_lost; fz = g_fate_empty;
    pthread_mutex_unlock(&g_lock);
    int xr = -1, size = -1, bcap = -1, burst = -1;
    pthread_mutex_lock(&g_stream_lock);
    if (g_stream) {
        xr = (int) AAudioStream_getXRunCount(g_stream);
        size = (int) AAudioStream_getBufferSizeInFrames(g_stream);
        bcap = (int) AAudioStream_getBufferCapacityInFrames(g_stream);
        burst = (int) AAudioStream_getFramesPerBurst(g_stream);
    }
    pthread_mutex_unlock(&g_stream_lock);
    long failed = 0;
    pthread_mutex_lock(&g_cache_lock);
    for (int i = 0; i < CACHE_STATES; i++) if (cache && g_caches[i].game == cache) failed = g_caches[i].failed;
    pthread_mutex_unlock(&g_cache_lock);
    snd_summary("sound census (%s): effect plays %ld - to the end %ld, CUT SHORT by the game %ld, UNHEARD %ld, "
                "LOST by the layer %ld, played with no buffer %ld, still sounding %ld; plays refused %ld, cache refusals %ld, "
                "lost buffers %ld; "
                "stream %d Hz, burst %d, buffer %d of %d, xruns %d, grown %ld times, silent disconnects %ld, "
                "ordinary-mixer fallback %s",
                why, fp, fe, fc, fu, fl, fz, fp - fe - fc - fu - fl - fz, g_play_refused, failed, g_total_lost_buffers,
                g_rate, burst, size, bcap, xr, g_buffer_grown, g_silent_disconnects,
                g_perf_fallback ? (g_perf == AAUDIO_PERFORMANCE_MODE_NONE ? "ON" : "tried, did not open") : "no");
}

/*
 * Once a frame: the game's own cache's thresholds (when it is still the
 * game's), keys of dead objects piling up in the pool, sources that play on
 * past their sound - the next suspects if the census shows no wrong sound and
 * no cross-talk - and the summary every ten minutes.
 */
static void census_tick(void) {
    if (!shim_report_active()) return;
    fate_tick();
    unsigned long b = shim_image_base();
    if (!b) return;
    long now = now_ns();
    static long last_ns, summary_ns;
    static int said_game_cache, said_dead_keys, stuck_lines;
    if (!summary_ns) summary_ns = now;
    if (now - summary_ns > 600L * 1000000000L) { summary_ns = now; census_summary("ten minutes"); }
    if (now - last_ns < 5000000000L) return;
    last_ns = now;
    const int *cache = (const int *) live_cache();
    if (cache && !g_patched) {
        if (cache[1] >= cache[0] * 9 / 10 && !(said_game_cache & 1)) {
            said_game_cache |= 1;
            snd_line("sound cache: the game's own cache holds %d of %d", cache[1], cache[0]);
        }
        if (cache[1] >= cache[0] && !(said_game_cache & 2)) {
            said_game_cache |= 2;
            snd_line("sound cache: the game's own cache is full, %d of %d", cache[1], cache[0]);
        }
    }
    const int *pool = *(const int *const *) (b + SND_MANAGER + 0x90);
    unsigned long begin = *(const unsigned long *) (b + SND_MANAGER + 0x18);
    unsigned long end = *(const unsigned long *) (b + SND_MANAGER + 0x20);
    int objects = (begin && end > begin) ? (int) ((end - begin) / 8) : 0;
    if (pool && objects > 0 && pool[1] * 2 > objects * 3 && !said_dead_keys) {
        said_dead_keys = 1;
        snd_line("sound census: the pool holds %d sources for %d live sound objects - keys of dead objects "
                 "pile up", pool[1], objects);
    }
    if (stuck_lines >= 8) return;
    char stuck[4][220];
    int k = 0;
    pthread_mutex_lock(&g_lock);
    for (int i = 1; i < g_src_cap && k < 4; i++) {
        alsrc_t *s = &g_src[i];
        if (!s->used || s->queued_ns || s->census_told) continue;
        if (s->state != AL_PLAYING && s->state != AL_PAUSED) continue;
        albuf_t *cur = current_of(s);
        double len_s = (cur && cur->rate > 0 && s->pitch > 0.0f) ? (double) cur->frames / cur->rate / s->pitch : 0.0;
        double age_s = s->played_ns ? (now - s->played_ns) / 1e9 : -1.0;
        double held_s = s->paused_ns ? (now - s->paused_ns) / 1e9 : -1.0;
        const char *why = NULL;
        if (s->state == AL_PLAYING && !s->looping && age_s > len_s + 2.0) why = "PLAYING past its end";
        else if (s->state == AL_PAUSED && held_s > 10.0) why = "PAUSED for over ten seconds";
        if (!why) continue;
        s->census_told = 1;
        snprintf(stuck[k++], sizeof(stuck[0]),
                 "source %d %s: buffer %d (%.2f s of sound, %s), gain %.2f, pitch %.3f, played %.1f s ago from +%lx",
                 i, why, s->buffer, len_s, cur ? (cur->pcm ? "filled" : "EMPTY") : "NO BUFFER",
                 (double) s->gain, (double) s->pitch, age_s, s->play_from);
    }
    pthread_mutex_unlock(&g_lock);
    for (int j = 0; j < k && stuck_lines < 8; j++, stuck_lines++) snd_line("%s", stuck[j]);
}

/*
 * THE SELF-TEST (report builds; the owner has nothing to do for it).
 *
 * Once a run, in the menu, with the mixer alive and not paused, eight seconds
 * after the sound opened. It proves the MECHANISM on this phone and this
 * layer - the cause of the complaint is proved only by the census in ordinary
 * play (a full cache, WRONG SOUND or CROSS-TALK above zero).
 *
 *   S0  every al* entry on the names -1, 0x7fffffff, the size of the list and
 *       one past it: no crash, AL_INVALID_NAME, alGet* answers untouched;
 *       alSourcei(AL_BUFFER) with a dead buffer: AL_INVALID_VALUE; a refill of
 *       an attached buffer refused; the first error holds.
 *   S1  "as on iOS": a pool of 64 and a cache of 500 built by the game's own
 *       constructors, the game's own pool and cache code. This is also how the
 *       answer "no" to the owner's question would have sounded.
 *   S2  "ours": the pool at the patched size, our cache.
 *
 * Each plays 610 made-up sound objects exactly as 0x8a9fc8/0x8a2eac does -
 * the real stop (0x8a30e0) and isPlaying (0x8a3084), the real pool, the cache,
 * alSourcei/alSourcef/alSourcePlay - 64 of them first (their own sources),
 * 10 looping ones, the effects' volume pass, 536 more (the cache overflows),
 * then 100 of the first again. Every object's PCM is 48 frames carrying its
 * own number; its gain, 1e-6 x (number + 1), cannot be heard. Checked: the
 * buffer in the object's source holds its own number; the loops still play,
 * with their own gain; no one-shot source plays two seconds past its end.
 * Never more than 32 plays a frame, so the 1 ms sounds end between frames.
 * The pool is taken apart before the cache, so the cache's buffers are free.
 *
 * Then, when the pool and the cache are ours, the live stress: every loaded
 * sound id of the effects' manager (0x7f06cc) three times over at volume 0,
 * in the menu only, and StopAll (0x7f0d34) at the end - the whole real chain
 * with its variants and handles.
 */
#define ST_OBJS      610
#define ST_PERSONAL  64
#define ST_LOOPS     10
#define ST_FRAMES    48
#define ST_RATE      48000
#define ST_PER_FRAME 32
#define ST_OBJ_SIZE  0x100
#define ST_REPEAT    100

enum { ST_WAIT, ST_SETUP, ST_PERSONAL_PHASE, ST_LOOP_PHASE, ST_VOLUME, ST_ONESHOTS, ST_REPEAT1,
       ST_REPEAT2, ST_SETTLE, ST_CLEANUP, ST_STRESS, ST_DONE };

typedef struct {
    long plays, wrong, rplays, rwrong, loops_killed, gain_over, no_source, refused, stuck;
    int pool_size, ran;
} st_result_t;

static struct {
    int phase, scenario, next, pass;
    long settle_ns, refused_at;
    unsigned char *objs;
    int16_t *pcm;
    unsigned long vt[0x20];
    void *pool, *cache;
    st_result_t r[3];
    /* the live stress */
    long c_plays, c_evicted, c_over, c_unbound, c_cross, c_wrong, c_unbound_play, c_refused;
    int ids, skipped;
} g_st;

static unsigned char *st_obj(int k) { return g_st.objs + (size_t) k * ST_OBJ_SIZE; }
static const int16_t *st_data(int k) { return g_st.pcm + (size_t) k * ST_FRAMES; }
static float st_gain(int k) { return 1e-6f * (float) (k + 1); }
static int st_is_loop(int k) { return k >= ST_PERSONAL && k < ST_PERSONAL + ST_LOOPS; }
static unsigned st_src(int k) { return *(const unsigned *) (st_obj(k) + 0x58); }

static int st_marker(unsigned src) {
    int m = -1;
    pthread_mutex_lock(&g_lock);
    alsrc_t *s = src_of(src);
    if (s) m = (buf_live(s->buffer) && g_buf[s->buffer].pcm) ? g_buf[s->buffer].pcm[0] : 0;
    pthread_mutex_unlock(&g_lock);
    return m;
}

/* 0x8a9fc8 and 0x8a2eac, word for word, on a made-up object */
static void st_play(int k, int repeat) {
    unsigned long b = shim_image_base();
    unsigned char *o = st_obj(k);
    st_result_t *r = &g_st.r[g_st.scenario];
    ((void (*)(void *)) (b + FN_SND_STOP))(o);
    if (((int (*)(void *)) (b + FN_SND_PLAYING))(o) & 1) return;
    unsigned buf = g_st.scenario == 1
        ? ((unsigned (*)(void *, const void *, int, int, int)) (b + GAME_CACHE_GET))(
              g_st.cache, st_data(k), ST_FRAMES * 2, AL_FORMAT_MONO16, ST_RATE)
        : shim_sound_cache_get(g_st.cache, st_data(k), ST_FRAMES * 2, AL_FORMAT_MONO16, ST_RATE);
    *(unsigned *) (o + 0x5c) = buf;
    unsigned src = ((unsigned (*)(void *, void *)) (b + FN_POOL_GET))(g_st.pool, o);
    *(unsigned *) (o + 0x58) = src;
    shim_alSourcei(src, AL_BUFFER, (int) buf);
    shim_alSourcei(src, AL_LOOPING, o[0xd]);
    shim_alSourcef(src, AL_GAIN, st_gain(k));
    shim_alSourcePlay(src);
    shim_alGetError();                          /* the game reads it right there (0x8a2f90) */
    if (src == 0xffffffffu) { r->no_source++; return; }
    r->plays++;
    int wrong = st_marker(src) != k + 1;
    r->wrong += wrong;
    if (repeat) { r->rplays++; r->rwrong += wrong; }
}

static int st_expect(int want, const char *what, long *calls, long *bad, char *first, size_t n) {
    int got = shim_alGetError();
    (*calls)++;
    if (got == want) return 1;
    if (!(*bad)++) snprintf(first, n, "%s gave %#x, not %#x", what, got, want);
    return 0;
}

/* S0: the names, the refused refill, the first error */
static void st_names(void) {
    long calls = 0, bad = 0;
    char first[120] = "";
    int16_t pcm[ST_FRAMES];
    for (int i = 0; i < ST_FRAMES; i++) pcm[i] = 1;
    shim_alGetError();
    unsigned good_buf = 0, good_src = 0, other;
    shim_alGenBuffers(1, &good_buf);
    shim_alGenSources(1, &good_src);
    shim_alBufferData(good_buf, AL_FORMAT_MONO16, pcm, sizeof(pcm), ST_RATE);
    pthread_mutex_lock(&g_lock);
    unsigned scap = (unsigned) g_src_cap, bcap = (unsigned) g_buf_cap;
    pthread_mutex_unlock(&g_lock);
    const unsigned srcs[4] = { 0xffffffffu, 0x7fffffffu, scap, scap + 1 };
    const unsigned bufs[4] = { 0xffffffffu, 0x7fffffffu, bcap, bcap + 1 };
    shim_alGetError();
    for (int i = 0; i < 4; i++) {
        unsigned n = srcs[i];
        int iv = 0x5a5a; float fv = -7.0f;
        shim_alSourcei(n, AL_BUFFER, 0);        st_expect(AL_INVALID_NAME, "alSourcei(AL_BUFFER)", &calls, &bad, first, sizeof(first));
        shim_alSourcei(n, AL_LOOPING, 1);       st_expect(AL_INVALID_NAME, "alSourcei(AL_LOOPING)", &calls, &bad, first, sizeof(first));
        shim_alSourcef(n, AL_GAIN, 0.5f);       st_expect(AL_INVALID_NAME, "alSourcef(AL_GAIN)", &calls, &bad, first, sizeof(first));
        shim_alSourcef(n, AL_PITCH, 1.0f);      st_expect(AL_INVALID_NAME, "alSourcef(AL_PITCH)", &calls, &bad, first, sizeof(first));
        shim_alSource3f(n, AL_POSITION, 0, 0, 0); st_expect(AL_INVALID_NAME, "alSource3f", &calls, &bad, first, sizeof(first));
        shim_alSourcePlay(n);                   st_expect(AL_INVALID_NAME, "alSourcePlay", &calls, &bad, first, sizeof(first));
        shim_alSourcePause(n);                  st_expect(AL_INVALID_NAME, "alSourcePause", &calls, &bad, first, sizeof(first));
        shim_alSourceStop(n);                   st_expect(AL_INVALID_NAME, "alSourceStop", &calls, &bad, first, sizeof(first));
        shim_alSourceQueueBuffers(n, 1, &good_buf); st_expect(AL_INVALID_NAME, "alSourceQueueBuffers", &calls, &bad, first, sizeof(first));
        shim_alSourceUnqueueBuffers(n, 1, &other); st_expect(AL_INVALID_NAME, "alSourceUnqueueBuffers", &calls, &bad, first, sizeof(first));
        shim_alGetSourcei(n, AL_SOURCE_STATE, &iv);
        if (st_expect(AL_INVALID_NAME, "alGetSourcei", &calls, &bad, first, sizeof(first)) && iv != 0x5a5a && !bad++)
            snprintf(first, sizeof(first), "alGetSourcei wrote its answer on a bad name");
        shim_alGetSourcef(n, AL_GAIN, &fv);
        if (st_expect(AL_INVALID_NAME, "alGetSourcef", &calls, &bad, first, sizeof(first)) && fv != -7.0f && !bad++)
            snprintf(first, sizeof(first), "alGetSourcef wrote its answer on a bad name");
        shim_alDeleteSources(1, &n);            st_expect(AL_INVALID_NAME, "alDeleteSources", &calls, &bad, first, sizeof(first));
    }
    for (int i = 0; i < 4; i++) {
        unsigned n = bufs[i];
        shim_alBufferData(n, AL_FORMAT_MONO16, pcm, sizeof(pcm), ST_RATE);
        st_expect(AL_INVALID_NAME, "alBufferData", &calls, &bad, first, sizeof(first));
        shim_alDeleteBuffers(1, &n);            st_expect(AL_INVALID_NAME, "alDeleteBuffers", &calls, &bad, first, sizeof(first));
        shim_alSourcei(good_src, AL_BUFFER, (int) n);
        st_expect(AL_INVALID_VALUE, "alSourcei(AL_BUFFER, a dead buffer)", &calls, &bad, first, sizeof(first));
        shim_alSourceQueueBuffers(good_src, 1, &n);
        st_expect(AL_INVALID_NAME, "alSourceQueueBuffers(a dead buffer)", &calls, &bad, first, sizeof(first));
    }
    /* a buffer attached to a stopped source is not refilled; let go, and it is */
    int16_t other_pcm[ST_FRAMES];
    for (int i = 0; i < ST_FRAMES; i++) other_pcm[i] = 2;
    long c1_calls = 0, c1_bad = 0;
    char c1_first[120] = "";
    shim_alSourcei(good_src, AL_BUFFER, (int) good_buf);
    shim_alGetError();
    shim_alBufferData(good_buf, AL_FORMAT_MONO16, other_pcm, sizeof(other_pcm), ST_RATE);
    st_expect(AL_INVALID_OPERATION, "a refill of an attached buffer", &c1_calls, &c1_bad, c1_first, sizeof(c1_first));
    if (st_marker(good_src) != 1 && !c1_bad++) snprintf(c1_first, sizeof(c1_first), "the refused refill changed the sound");
    shim_alSourcei(good_src, AL_BUFFER, 0);
    shim_alSourceQueueBuffers(good_src, 1, &good_buf);
    shim_alSourceStop(good_src);
    shim_alGetError();
    shim_alBufferData(good_buf, AL_FORMAT_MONO16, other_pcm, sizeof(other_pcm), ST_RATE);
    st_expect(AL_INVALID_OPERATION, "a refill of a processed queued buffer", &c1_calls, &c1_bad, c1_first, sizeof(c1_first));
    shim_alSourceUnqueueBuffers(good_src, 1, &other);
    shim_alGetError();
    shim_alBufferData(good_buf, AL_FORMAT_MONO16, other_pcm, sizeof(other_pcm), ST_RATE);
    st_expect(AL_NO_ERROR, "a refill of a free buffer", &c1_calls, &c1_bad, c1_first, sizeof(c1_first));
    /* the first error holds */
    shim_alSourcei(0xffffffffu, AL_LOOPING, 0);
    shim_alSourcei(good_src, AL_BUFFER, 0x7fffffff);
    int held = shim_alGetError() == AL_INVALID_NAME && shim_alGetError() == AL_NO_ERROR;
    shim_alDeleteSources(1, &good_src);
    shim_alDeleteBuffers(1, &good_buf);
    shim_alGetError();
    st_line("names: %ld calls on -1, 0x7fffffff, the list's size and one past it, %ld wrong answers%s%s; "
            "attached refill: %s%s; the first error holds: %s",
            calls, bad, bad ? " - first: " : "", first, c1_bad ? "WRONG - " : "refused as OpenAL",
            c1_first, held ? "yes" : "NO");
}

static void st_setup(void) {
    unsigned long b = shim_image_base();
    st_result_t *r = &g_st.r[g_st.scenario];
    memset(r, 0, sizeof(*r));
    if (!g_st.objs) g_st.objs = (unsigned char *) calloc(ST_OBJS, ST_OBJ_SIZE);
    if (!g_st.pcm) g_st.pcm = (int16_t *) calloc(ST_OBJS * ST_FRAMES, sizeof(int16_t));
    if (!g_st.objs || !g_st.pcm) { st_line("no memory for the test's objects - not run"); g_st.phase = ST_DONE; return; }
    memset(g_st.objs, 0, (size_t) ST_OBJS * ST_OBJ_SIZE);
    memset(g_st.vt, 0, sizeof(g_st.vt));
    g_st.vt[0x78 / 8] = b + FN_SND_PLAYING;
    g_st.vt[0x80 / 8] = b + FN_SND_STOP;
    for (int k = 0; k < ST_OBJS; k++) {
        int16_t *p = g_st.pcm + (size_t) k * ST_FRAMES;
        for (int f = 0; f < ST_FRAMES; f++) p[f] = (int16_t) (k + 1);
        unsigned char *o = st_obj(k);
        *(unsigned long *) o = (unsigned long) g_st.vt;
        o[0xd] = (unsigned char) st_is_loop(k);
        *(const void **) (o + 0x48) = st_data(k);
        *(int *) (o + 0x50) = ST_FRAMES * 2;
        *(int *) (o + 0x54) = AL_FORMAT_MONO16;
    }
    uint32_t word = *(const uint32_t *) (b + POOL_SIZE_WORD);
    r->pool_size = g_st.scenario == 1 ? 64 : (int) ((word >> 5) & 0xffff);
    g_st.pool = calloc(1, 0x40);
    g_st.cache = calloc(1, 0x40);
    if (!g_st.pool || !g_st.cache) { st_line("no memory for the test's pool - not run"); g_st.phase = ST_DONE; return; }
    ((void *(*)(void *, int)) (b + FN_POOL_NEW))(g_st.pool, r->pool_size);
    ((void *(*)(void *, int)) (b + FN_CACHE_NEW))(g_st.cache, 500);
    g_st.refused_at = g_data_refused;
    r->ran = 1;
    g_st.next = 0;
    g_st.phase = ST_PERSONAL_PHASE;
}

/* the next up-to-32 objects of a range; 1 when the range is done */
static int st_run(int from, int to, int repeat) {
    int played = 0;
    while (g_st.next < to - from && played < ST_PER_FRAME) {
        int k = from + g_st.next++;
        if (repeat && st_is_loop(k)) continue;      /* a loop played again restarts itself: not the test */
        st_play(k, repeat);
        played++;
    }
    if (g_st.next < to - from) return 0;
    g_st.next = 0;
    return 1;
}

static void st_settle(void) {
    st_result_t *r = &g_st.r[g_st.scenario];
    for (int k = ST_PERSONAL; k < ST_PERSONAL + ST_LOOPS; k++) {
        /* killed: stopped by somebody, or its source now plays another sound (the gain is counted apart) */
        unsigned src = st_src(k);
        pthread_mutex_lock(&g_lock);
        alsrc_t *s = src_of(src);
        int alive = s && s->state == AL_PLAYING;
        pthread_mutex_unlock(&g_lock);
        if (!alive || st_marker(src) != k + 1) r->loops_killed++;
    }
    pthread_mutex_lock(&g_lock);
    unsigned char *seen = (unsigned char *) calloc((size_t) g_src_cap, 1);
    for (int k = 0; seen && k < ST_OBJS; k++) {
        unsigned src = st_src(k);
        alsrc_t *s = src_of(src);
        if (!s || seen[src]) continue;
        seen[src] = 1;
        if (s->state == AL_PLAYING && !s->looping) r->stuck++;
    }
    free(seen);
    pthread_mutex_unlock(&g_lock);
}

static void st_cleanup(void) {
    unsigned long b = shim_image_base();
    st_result_t *r = &g_st.r[g_st.scenario];
    for (int k = 0; k < ST_OBJS; k++) {
        unsigned src = st_src(k);
        if (src && src != 0xffffffffu) shim_alSourceStop(src);
    }
    shim_alGetError();
    /* the pool first: its sources go, and with them every hold on the cache's buffers */
    ((void *(*)(void *)) (b + FN_POOL_FREE))(g_st.pool);
    if (g_st.scenario == 2) {
        pthread_mutex_lock(&g_cache_lock);
        for (int i = 0; i < CACHE_STATES; i++)
            if (g_caches[i].game == g_st.cache) cache_release(&g_caches[i]);
        pthread_mutex_unlock(&g_cache_lock);
    }
    ((void *(*)(void *)) (b + FN_CACHE_FREE))(g_st.cache);   /* the game's own: deletes what it made */
    shim_alGetError();
    free(g_st.pool); free(g_st.cache);
    g_st.pool = g_st.cache = NULL;
    r->refused = g_data_refused - g_st.refused_at;
    st_line("%s: wrong %ld/%ld (on the repeat %ld/%ld), loops killed %ld/%d, gain overwritten %ld, "
            "no-source %ld, refills refused %ld, playing past the end %ld (pool %d, cache 500)",
            g_st.scenario == 1 ? "ios-like" : "ours", r->wrong, r->plays, r->rwrong, r->rplays,
            r->loops_killed, ST_LOOPS, r->gain_over, r->no_source, r->refused, r->stuck, r->pool_size);
}

static void st_summary(void) {
    st_result_t *a = &g_st.r[1], *o = &g_st.r[2];
    if (o->ran)
        st_line("ios-like: wrong %ld/%ld, loops killed %ld/%d, gain overwritten %ld, no-source %ld; "
                "ours: %ld/%ld, %ld/%d, %ld, %ld%s", a->rwrong, a->rplays, a->loops_killed, ST_LOOPS,
                a->gain_over, a->no_source, o->rwrong, o->rplays, o->loops_killed, ST_LOOPS, o->gain_over,
                o->no_source,
                (o->rwrong || o->wrong || o->loops_killed || o->gain_over || o->no_source || o->stuck)
                    ? " - OURS IS NOT CLEAN" : " - ours is clean");
    else
        st_line("ios-like: wrong %ld/%ld, loops killed %ld/%d, gain overwritten %ld, no-source %ld; "
                "ours: not installed (the words did not match)", a->rwrong, a->rplays, a->loops_killed,
                ST_LOOPS, a->gain_over, a->no_source);
}

/* ---- the live stress: the effects' manager itself, at volume 0 ---- */

/*
 * OFF UNLESS ASKED FOR (16.10.2026). On a weak phone the stress is not a test
 * of the sound but a load of its own: a Galaxy A15 5G ran 75..139 voices, the
 * mixer took 13-20 ms over a 5.3 ms callback, held the sound's lock, and the
 * frame thread waited 2.3 s in the intro - a tester's "lag when I skipped the
 * intro" that the release build never had. A report build handed to a player
 * measures his phone, so the stress runs only with audiostress=1 in
 * port-touch.ini (sticks.c), for the desk's own phone.
 */
static int g_stress_allowed;
void shim_audio_stress_allow(int on) { g_stress_allowed = on != 0; }

static unsigned long sfx_manager(void) {
    unsigned long g = game_root();
    return g ? g + 0x55ea0 : 0;
}

static scache_t *live_state(void) {
    const void *c = live_cache();
    for (int i = 0; c && i < CACHE_STATES; i++) if (g_caches[i].game == c) return &g_caches[i];
    return NULL;
}

static void stress_begin(void) {
    scache_t *st = live_state();
    g_st.c_plays = g_plays_game;
    g_st.c_evicted = st ? st->evicted : 0;
    g_st.c_over = st ? st->over : 0;
    g_st.c_unbound = st ? st->unbound : 0;
    g_st.c_cross = g_cross;
    g_st.c_wrong = g_wrong;
    g_st.c_unbound_play = g_play_unbound;
    g_st.c_refused = g_data_refused;
    g_st.next = 0;
    g_st.pass = 0;
    g_st.ids = g_st.skipped = 0;
    g_st.phase = ST_STRESS;
    g_fate_quiet = 1;
    st_line("live stress begins: every loaded sound id of the effects' manager, three passes at volume 0");
}

static void stress_end(const char *how) {
    unsigned long b = shim_image_base();
    unsigned long mgr = sfx_manager();
    if (mgr) ((void (*)(unsigned long)) (b + FN_SFX_STOP_ALL))(mgr);
    g_fate_quiet = 0;
    scache_t *st = live_state();
    const int *pool = *(const int *const *) (b + SND_MANAGER + 0x90);
    st_line("live stress %s: %d sound ids x %d passes at volume 0 (%d skipped: not loaded, or streamed); "
            "%ld plays reached a source, cache evictions +%ld, over capacity +%ld, let go +%ld; CROSS-TALK +%ld, "
            "WRONG SOUND +%ld, played with no buffer +%ld, alBufferData refused +%ld; pool %d/%d, cache %d/%d, "
            "peak voices %d; StopAll done",
            how, g_st.ids, g_st.pass, g_st.skipped, g_plays_game - g_st.c_plays,
            st ? st->evicted - g_st.c_evicted : -1, st ? st->over - g_st.c_over : -1,
            st ? st->unbound - g_st.c_unbound : -1, g_cross - g_st.c_cross, g_wrong - g_st.c_wrong,
            g_play_unbound - g_st.c_unbound_play, g_data_refused - g_st.c_refused,
            pool ? pool[1] : -1, pool ? pool[0] : -1, st ? st->n : -1, st ? st->cap : -1, g_peak_voices);
    g_st.phase = ST_DONE;
}

static void stress_step(void) {
    unsigned long b = shim_image_base();
    unsigned long mgr = sfx_manager();
    if (!mgr) { stress_end("not run (no manager)"); return; }
    unsigned long begin = *(const unsigned long *) mgr, end = *(const unsigned long *) (mgr + 8);
    int count = (begin && end > begin) ? (int) ((end - begin) / 0x1f0) : 0;
    int played = 0;
    while (played < ST_PER_FRAME) {
        if (g_st.next >= count) {
            g_st.pass++;
            g_st.next = 0;
            if (g_st.pass >= 3) { stress_end("done"); return; }
            return;                                 /* a new pass starts next frame */
        }
        int i = g_st.next++;
        unsigned long rec = begin + (unsigned long) i * 0x1f0;
        int variants = *(const int *) (rec + 0x1c8);
        int loaded = *(const unsigned char *) (rec + 0x1d0);
        int streamed = 0;
        for (int k = 0; k < variants && k < 7; k++)
            if (*(const unsigned char *) (rec + 0x40 + 0x40 * (unsigned long) k)) streamed = 1;
        if (variants <= 0 || !loaded || streamed) { if (!g_st.pass) g_st.skipped++; continue; }
        if (!g_st.pass) g_st.ids++;
        /* (manager, id, delay, loop; volume, pitch, pan) - x0 w1 w2 w3 s0 s1 s2, as 0x671c8c passes them */
        ((void (*)(unsigned long, int, int, int, float, float, float)) (b + FN_SFX_PLAY))(
            mgr, i, 0, 0, 0.0f, 1.0f, 0.0f);
        played++;
    }
}

static int st_can_run(void) {
    long now = now_ns();
    return g_stream && !g_paused && g_mix_ns && now - g_mix_ns < 200000000L && in_menu();
}

static void selftest_tick(void) {
    if (!shim_report_active() || g_st.phase == ST_DONE) return;
    long now = now_ns();
    if (g_st.phase == ST_STRESS) {
        if (!st_can_run()) { stress_end("interrupted (left the menu, or the mixer stopped)"); return; }
        stress_step();
        return;
    }
    if (!st_can_run()) return;                      /* wait: menu, mixer, not paused */
    if (g_st.phase == ST_WAIT) {
        if (!g_dev_open_ns || now - g_dev_open_ns < 8000000000L) return;
        st_line("started %ld s after the sound opened, in the menu, the mixer alive; pool and cache %s",
                (now - g_dev_open_ns) / 1000000000L, g_patched ? "ours" : "the game's");
        g_selftest_tid = gettid();
        st_names();
        g_selftest_tid = 0;
        g_st.scenario = 1;
        g_st.phase = ST_SETUP;
        return;
    }
    g_selftest_tid = gettid();
    switch (g_st.phase) {
    case ST_SETUP:
        st_setup();
        break;
    case ST_PERSONAL_PHASE:
        if (st_run(0, ST_PERSONAL, 0)) g_st.phase = ST_LOOP_PHASE;
        break;
    case ST_LOOP_PHASE:
        if (st_run(ST_PERSONAL, ST_PERSONAL + ST_LOOPS, 0)) g_st.phase = ST_VOLUME;
        break;
    case ST_VOLUME: {
        /* the effects' volume pass: a sound that says it plays gets its volume, wherever it is */
        unsigned long b = shim_image_base();
        for (int k = 0; k < ST_PERSONAL; k++)
            if (((int (*)(void *)) (b + FN_SND_PLAYING))(st_obj(k)) & 1)
                shim_alSourcef(st_src(k), AL_GAIN, st_gain(k));
        shim_alGetError();
        for (int k = ST_PERSONAL; k < ST_PERSONAL + ST_LOOPS; k++) {
            pthread_mutex_lock(&g_lock);
            alsrc_t *s = src_of(st_src(k));
            if (s && s->gain != st_gain(k)) g_st.r[g_st.scenario].gain_over++;
            pthread_mutex_unlock(&g_lock);
        }
        g_st.phase = ST_ONESHOTS;
        break;
    }
    case ST_ONESHOTS:
        if (st_run(ST_PERSONAL + ST_LOOPS, ST_OBJS, 0)) g_st.phase = ST_REPEAT1;
        break;
    case ST_REPEAT1:
        /* 100 one-shots from the start again: 0..63, then past the loops */
        if (st_run(0, ST_REPEAT + ST_LOOPS, 1)) g_st.phase = ST_REPEAT2;
        break;
    case ST_REPEAT2:
        if (st_run(0, ST_PERSONAL, 1)) { g_st.phase = ST_SETTLE; g_st.settle_ns = now; }
        break;
    case ST_SETTLE:
        if (now - g_st.settle_ns < 2200000000L) break;
        st_settle();
        g_st.phase = ST_CLEANUP;
        break;
    case ST_CLEANUP:
        st_cleanup();
        if (g_st.scenario == 1 && g_patched) { g_st.scenario = 2; g_st.phase = ST_SETUP; break; }
        st_summary();
        free(g_st.objs); free(g_st.pcm);
        g_st.objs = NULL; g_st.pcm = NULL;
        g_selftest_tid = 0;
        if (g_patched && edition() == 3 && g_stress_allowed) stress_begin();
        else {
            if (g_patched && edition() == 3) st_line("live stress not run: audiostress=1 in port-touch.ini asks for it");
            g_st.phase = ST_DONE;
        }
        break;
    default:
        break;
    }
    g_selftest_tid = 0;
}

/* ---- the sound meter: what reached the stream, second by second ---- */

#define METER_LINES   400               /* the per-second lines: a budget of their own */
#define SILENCE_LINES 150               /* the silence episodes' lines */
#define ASK_IDS       8

typedef struct {
    long frames, calls;
    double sq;
    float peak, raw, out, gmax;         /* written; the loudest voice before and after its gain; the top gain */
    int voices_lo, voices_hi;
    long started_ns, ended_ns;
    long longest, run_end;              /* silent frames in a row: the longest, and the run at the end */
    long gap_max, wait_max, spent_max;  /* the callbacks: longest gap between two, lock wait, time inside */
    int fpc_lo, fpc_hi;                 /* frames a callback asked for */
} meter_win_t;

static pthread_mutex_t g_meter_lock = PTHREAD_MUTEX_INITIALIZER;
static meter_win_t g_meter_pub;         /* the last finished second, from the mixer */
static long g_meter_seq;
static int g_meter_lines, g_silence_lines;

/* what the game asked of its effects' player (0x671c8c, the trace's '@'): x1 the id, s0 the volume */
static long g_m_asks;
static float g_m_ask_lo = -1.0f, g_m_ask_hi = -1.0f;
static int g_m_ask_ids[ASK_IDS], g_m_ask_n;

void shim_audio_sfx_asked(unsigned long id, float volume) {
    if (!shim_report_active()) return;
    g_m_asks++;
    if (g_m_ask_lo < 0.0f || volume < g_m_ask_lo) g_m_ask_lo = volume;
    if (volume > g_m_ask_hi) g_m_ask_hi = volume;
    int n = g_m_ask_n;
    if (n < ASK_IDS) { g_m_ask_ids[n] = (int) id; g_m_ask_n = n + 1; }
}

/* the mixer's thread: a few compares a call, and one hand-over a second */
static void meter_mix(int32_t frames, float peak, double sq, float raw, float out, float gmax, int voices,
                      long gap, long wait, long spent) {
    static meter_win_t acc;
    static long run;                    /* silent frames in a row, across the seconds */
    if (!shim_report_active()) return;
    long now = now_ns();
    if (!acc.calls) { acc.started_ns = now; acc.voices_lo = voices; acc.fpc_lo = frames; }
    if (gap > acc.gap_max) acc.gap_max = gap;
    if (wait > acc.wait_max) acc.wait_max = wait;
    if (spent > acc.spent_max) acc.spent_max = spent;
    if (frames < acc.fpc_lo) acc.fpc_lo = frames;
    if (frames > acc.fpc_hi) acc.fpc_hi = frames;
    acc.frames += frames;
    acc.calls++;
    acc.sq += sq;
    if (peak > acc.peak) acc.peak = peak;
    if (raw > acc.raw) acc.raw = raw;
    if (out > acc.out) acc.out = out;
    if (gmax > acc.gmax) acc.gmax = gmax;
    if (voices < acc.voices_lo) acc.voices_lo = voices;
    if (voices > acc.voices_hi) acc.voices_hi = voices;
    run = peak < METER_SILENT ? run + frames : 0;
    if (run > acc.longest) acc.longest = run;
    if (acc.frames < (g_rate > 0 ? g_rate : 48000)) return;
    acc.ended_ns = now;
    acc.run_end = run;
    pthread_mutex_lock(&g_meter_lock);
    g_meter_pub = acc;
    g_meter_seq++;
    pthread_mutex_unlock(&g_meter_lock);
    memset(&acc, 0, sizeof(acc));
}

static const char *stream_state_text(int st) {
    if (st < 0) return "NONE";
    const char *t = AAudio_convertStreamStateToText((aaudio_stream_state_t) st);
    if (!t) return "?";
    return strncmp(t, "AAUDIO_STREAM_STATE_", 20) ? t : t + 20;
}

/* the game's volume settings, where options.ini is read into (saves.c, OPT_KEYS) */
static void game_volumes(float *sfx, float *music, int *enabled) {
    unsigned long g = game_root();
    if (!g) return;
    const unsigned char *blk = (const unsigned char *) (g + 0x55ec8);
    *music = *(const float *) (blk + 0x18);
    *sfx = *(const float *) (blk + 0x1c);
    *enabled = blk[-0x78];
}

static void meter_say(int silence, const char *text) {
    if (silence) { if (g_silence_lines >= SILENCE_LINES) return; g_silence_lines++; }
    else { if (g_meter_lines >= METER_LINES) return; g_meter_lines++; }
    report_line("sound meter", text);
}

/* a list into lines of the report's width: at most three of them */
typedef struct { char line[540]; int len, lines, silence; const char *head; } meter_list_t;

static void list_open(meter_list_t *l, int silence, const char *head) {
    l->silence = silence;
    l->head = head;
    l->lines = 0;
    l->len = snprintf(l->line, sizeof(l->line), "%s", head);
}

static void list_add(meter_list_t *l, const char *item) {
    if (l->lines >= 3) return;
    size_t need = strlen(item) + 2;
    if ((size_t) l->len + need >= sizeof(l->line)) {
        meter_say(l->silence, l->line);
        if (++l->lines >= 3) return;
        l->len = snprintf(l->line, sizeof(l->line), "%s (more)", l->head);
    }
    l->len += snprintf(l->line + l->len, sizeof(l->line) - (size_t) l->len, "; %s", item);
}

static void list_close(meter_list_t *l) {
    if (l->lines < 3) meter_say(l->silence, l->line);
}

static const char *event_name(char k) {
    switch (k) {
    case 'g': return "gain to ~0";
    case 'G': return "gain up";
    case 'p': return "pause";
    case 's': return "stop";
    case 'd': return "delete";
    case 'P': return "play";
    case 'y': return "ran dry";
    case 'L': return "LISTENER gain";
    case 'A': return "app paused the stream";
    case 'a': return "app resumed the stream";
    case 'R': return "stream rebuilt";
    default:  return "?";
    }
}

/* the ring's calls since a moment, oldest first, times against now */
static void meter_calls(const char *head, long since_ns, long now) {
    mev_t copy[METER_RING];
    int n = 0;
    pthread_mutex_lock(&g_lock);
    long total = g_mring_n;
    for (long k = total > METER_RING ? total - METER_RING : 0; k < total; k++) {
        const mev_t *e = &g_mring[k % METER_RING];
        if (e->ns >= since_ns) copy[n++] = *e;
    }
    pthread_mutex_unlock(&g_lock);
    meter_list_t l;
    char h[120];
    int skip = n > 15 ? n - 15 : 0;             /* three lines hold about fifteen: the newest ones */
    snprintf(h, sizeof(h), "%s: %d call(s)%s%s", head, n, skip ? ", the last 15" : "",
             n == METER_RING ? ", the ring is full - older ones are lost" : "");
    list_open(&l, 1, h);
    for (int i = skip; i < n; i++) {
        const mev_t *e = &copy[i];
        char one[170];
        long ms = (e->ns - now) / 1000000L;
        if (!e->src)
            snprintf(one, sizeof(one), "%+ld ms %s, tid %d%s", ms, event_name(e->kind), e->tid,
                     e->tid == g_frame_tid ? "*" : "");
        else
            snprintf(one, sizeof(one), "%+ld ms %s %s %d %.2f->%.2f at +%lx<+%lx, tid %d%s", ms,
                     event_name(e->kind), e->music ? "music" : "effect", e->src, (double) e->was,
                     (double) e->now, e->from, e->from2, e->tid, e->tid == g_frame_tid ? "*" : "");
        list_add(&l, one);
    }
    list_close(&l);
}

/* every sounding or paused source: its gain, and how loud its data is for the next half second */
static void meter_sources(long now) {
    char items[12][150];
    int k = 0, total = 0;
    pthread_mutex_lock(&g_lock);
    for (int i = 1; i < g_src_cap; i++) {
        alsrc_t *s = &g_src[i];
        if (!s->used || (s->state != AL_PLAYING && s->state != AL_PAUSED)) continue;
        total++;
        if (k >= 12) continue;
        albuf_t *b = current_of(s);
        float ahead = -1.0f;
        if (b && b->pcm && b->frames > 0) {
            long from = (long) s->pos, to = from + (b->rate > 0 ? b->rate : 48000) / 2;
            if (from < 0) from = 0;
            if (to > b->frames) to = b->frames;
            int pk = 0;
            for (long f = from * b->channels; f < to * b->channels; f++) {
                int v = b->pcm[f] < 0 ? -b->pcm[f] : b->pcm[f];
                if (v > pk) pk = v;
            }
            ahead = (float) pk / 32768.0f;
        }
        snprintf(items[k++], sizeof(items[0]),
                 "%d %s%s gain %.2f pitch %.2f, %s %d%s, data ahead %.4f, played %ld ms ago from +%lx", i,
                 s->state == AL_PAUSED ? "PAUSED " : "", is_music(s) ? "music" : "effect", (double) s->gain,
                 (double) s->pitch, s->qn ? "queue of" : "buffer", s->qn ? s->qn : s->buffer,
                 b ? (b->pcm ? "" : " EMPTY") : " NONE", (double) ahead,
                 s->played_ns ? (now - s->played_ns) / 1000000L : -1L, s->play_from);
    }
    pthread_mutex_unlock(&g_lock);
    meter_list_t l;
    char h[80];
    snprintf(h, sizeof(h), "SILENCE sources: %d playing or paused%s", total, total > k ? ", the first 12" : "");
    list_open(&l, 1, h);
    for (int i = 0; i < k; i++) list_add(&l, items[i]);
    list_close(&l);
}

/* which link of the chain is broken, by the second that showed the silence */
static const char *silence_cause(char *buf, size_t n, const meter_win_t *w, int st, int playing,
                                 long plays, long asks) {
    if (st < 0) return "there is no stream at all";
    if (st != AAUDIO_STREAM_STATE_STARTED) {
        snprintf(buf, n, "the stream is %s, not started", stream_state_text(st));
        return buf;
    }
    if (!w->calls) return "the stream says started but the mixer was not called";
    if (!w->voices_hi && !playing) {
        if (plays) return "sources were played but none sounded through the second (empty buffers?)";
        if (asks) return "nothing plays at our layer: the game asked for effects and none reached alSourcePlay";
        return "nothing plays at our layer";
    }
    if (w->gmax <= METER_GAIN_EPS) return "every source that played had gain ~0: the game's own volume";
    if (w->raw < METER_SILENT) return "the sources with gain read silent data";
    if (w->out < METER_SILENT) return "the sources' data and gains are both tiny";
    return "the sources sounded but the output did not: the mix lost it";
}

/* once a frame, from the frame's thread */
static void meter_tick(void) {
    if (!shim_report_active()) return;
    static long seen, last_ns, run_ms, sil_ns, read_was = -1;
    static const AAudioStream *read_of;
    static int xr_was = -1, key_was[12], have_key, in_sil, sil_windows, after, hist_k;
    static char hist[3][580];
    static int hist_said[3];
    long now = now_ns();
    meter_win_t w;
    int fresh = 0;
    pthread_mutex_lock(&g_meter_lock);
    if (g_meter_seq != seen) { seen = g_meter_seq; w = g_meter_pub; fresh = 1; }
    pthread_mutex_unlock(&g_meter_lock);
    if (!last_ns) last_ns = now;
    if (!fresh) {
        /* no finished second from the mixer: it is late, or nobody calls it */
        if (now - last_ns < 1500000000L) return;
        memset(&w, 0, sizeof(w));
        w.started_ns = last_ns;
        w.ended_ns = now;
    }
    last_ns = now;
    long span_ms = (w.ended_ns - w.started_ns) / 1000000L;
    int rate = g_rate > 0 ? g_rate : 48000;
    if (fresh) run_ms = w.run_end * 1000L / rate;
    else run_ms += span_ms;
    long longest_ms = fresh ? w.longest * 1000L / rate : run_ms;

    /* the sources now, and the plays counted since the last second */
    int playing = 0, audible = 0, paused = 0, music = 0, music_aud = 0, loud = 0, loud_music = 0;
    float gsum = 0.0f, gmax = 0.0f;
    char mlist[100];
    int ml = 0;
    mlist[0] = 0;
    pthread_mutex_lock(&g_lock);
    for (int i = 1; i < g_src_cap; i++) {
        alsrc_t *s = &g_src[i];
        if (!s->used) continue;
        if (s->state == AL_PAUSED) paused++;
        if (s->state != AL_PLAYING) continue;
        playing++;
        gsum += s->gain;
        if (!loud || s->gain > gmax) { gmax = s->gain; loud = i; }
        if (s->gain > METER_GAIN_EPS) audible++;
        if (!is_music(s)) continue;
        music++;
        if (s->gain > METER_GAIN_EPS) music_aud++;
        if (ml < (int) sizeof(mlist) - 16)
            ml += snprintf(mlist + ml, sizeof(mlist) - (size_t) ml, "%s%d:%.2f", ml ? " " : "", i, (double) s->gain);
    }
    long plays = g_m_plays, eplays = g_m_eplays;
    g_m_plays = g_m_eplays = 0;
    long f_plays = g_fate_w_plays, f_cut = g_fate_w_cut, f_unheard = g_fate_w_unheard;
    g_fate_w_plays = g_fate_w_cut = g_fate_w_unheard = 0;
    float listener = g_listener_gain;
    unsigned long loud_from = loud ? g_src[loud].play_from : 0;
    long loud_ago = (loud && g_src[loud].played_ns) ? (now - g_src[loud].played_ns) / 1000000L : -1L;
    if (loud) loud_music = is_music(&g_src[loud]);
    pthread_mutex_unlock(&g_lock);
    long asks = g_m_asks;
    float alo = g_m_ask_lo, ahi = g_m_ask_hi;
    char ids[64];
    int il = 0;
    ids[0] = 0;
    for (int i = 0; i < g_m_ask_n && il < (int) sizeof(ids) - 8; i++)
        il += snprintf(ids + il, sizeof(ids) - (size_t) il, "%s%d", i ? "," : "", g_m_ask_ids[i]);
    g_m_asks = 0;
    g_m_ask_lo = g_m_ask_hi = -1.0f;
    g_m_ask_n = 0;

    /* the stream */
    int st = -1, xr = -1, paused_app, lost, bsize = -1, bburst = -1;
    long read_d = -1;
    pthread_mutex_lock(&g_stream_lock);
    if (g_stream) {
        st = (int) AAudioStream_getState(g_stream);
        xr = (int) AAudioStream_getXRunCount(g_stream);
        bsize = (int) AAudioStream_getBufferSizeInFrames(g_stream);
        bburst = (int) AAudioStream_getFramesPerBurst(g_stream);
        long rd = (long) AAudioStream_getFramesRead(g_stream);
        if (read_of == g_stream && read_was >= 0) read_d = rd - read_was;
        read_of = g_stream;
        read_was = rd;
    } else {
        read_of = NULL;
        read_was = -1;
    }
    paused_app = g_paused;
    lost = g_stream_lost;
    pthread_mutex_unlock(&g_stream_lock);
    int xr_new = (xr_was >= 0 && xr > xr_was) ? xr - xr_was : 0;
    xr_was = xr;

    float sfx = -1.0f, mus = -1.0f;
    int on = -1;
    game_volumes(&sfx, &mus, &on);

    int silent = !w.calls || w.peak < METER_SILENT;
    int wanted = playing || w.voices_hi || plays || asks;
    int busy = g_st.phase != ST_WAIT && g_st.phase != ST_DONE;    /* the self-test plays at volume ~0 */
    int quiet_ok = paused_app || busy;

    char line[580];
    double rms = w.frames ? sqrt(w.sq / (2.0 * (double) w.frames)) : 0.0;
    char xrs[24] = "";
    if (xr_new) snprintf(xrs, sizeof(xrs), " (+%d)", xr_new);
    snprintf(line, sizeof(line),
             "+%ld ms, %ld ms: out peak %.5f rms %.5f, silent %ld ms (longest %ld); wrote %ld frames in %ld calls, "
             "device read %ld; stream %s%s%s, xruns %d%s; voices %d..%d, playing %d (gain>0.001 %d, paused %d), "
             "gain sum %.2f max %.2f (src %d); top voice %.4f raw %.5f gained; music %d [%s]; asked %ld [%s] "
             "vol %.2f..%.2f, plays %ld (effects %ld); options sfx %.2f music %.2f on %d; listener %.2f",
             now / 1000000L, span_ms, (double) w.peak, rms, run_ms, longest_ms, w.frames, w.calls, read_d,
             stream_state_text(st), paused_app ? " PAUSED by the app" : "", lost ? " LOST" : "", xr, xrs,
             w.voices_lo, w.voices_hi, playing, audible, paused, (double) gsum, (double) gmax, loud,
             (double) w.raw, (double) w.out, music, mlist, asks, ids, (double) alo, (double) ahi, plays, eplays,
             (double) sfx, (double) mus, on, (double) listener);

    int key[12] = { silent, wanted, st, paused_app, lost, xr_new > 0, music_aud,
                    (int) lroundf(sfx * 100.0f), (int) lroundf(mus * 100.0f), on,
                    (int) lroundf(listener * 100.0f), audible > 0 };
    int emit = !have_key || memcmp(key, key_was, sizeof(key)) != 0;
    memcpy(key_was, key, sizeof(key));
    have_key = 1;

    /*
     * A second is SUSPECT when its output stayed silent for a second or more
     * while something was wanted, and the silence BEGINS only when the next
     * second is silent too. The asks and the plays are counted on the frame's
     * thread up to the moment the mixer's second is read, so a sound asked
     * just after the mixer closed its second sounds only in the next one:
     * without the second look that would be a silence that never was.
     */
    static int suspect, s_playing;
    static long s_asks, s_plays;
    static meter_win_t sw;
    static char s_ids[64];
    int began = 0, ended = 0;
    const char *why_end = NULL;
    if (!in_sil && suspect && silent && !quiet_ok) {
        began = 1;
        in_sil = 1;
        sil_windows = 0;
        sil_ns = now - run_ms * 1000000L;
        suspect = 0;
    } else if (!in_sil && !quiet_ok && wanted && silent && run_ms >= 1000) {
        suspect = 1;
        sw = w; s_asks = asks; s_plays = plays; s_playing = playing;
        snprintf(s_ids, sizeof(s_ids), "%s", ids);
    } else if (!in_sil) {
        suspect = 0;
    } else if (!silent || quiet_ok) {
        ended = 1;
        in_sil = 0;
        after = 2;
        why_end = !silent ? "sound came back" : paused_app ? "the app paused the stream" : "the self-test began";
    }
    if (in_sil && !began) { sil_windows++; if (sil_windows <= 10 || sil_windows % 5 == 0) emit = 1; }
    if (!in_sil && !ended && after > 0) { after--; emit = 1; }
    if (began || ended) emit = 1;

    int slot = hist_k;
    hist_k = (hist_k + 1) % 3;
    snprintf(hist[slot], sizeof(hist[slot]), "%s", line);
    hist_said[slot] = 0;
    if (began)
        for (int j = 2; j >= 1; j--) {          /* the seconds before it, if they were not written */
            int h = (slot + 3 - j) % 3;
            if (!hist_said[h] && hist[h][0]) { meter_say(0, hist[h]); hist_said[h] = 1; }
        }
    if (emit) { meter_say(0, line); hist_said[slot] = 1; }

    /*
     * THE TIMING LINE: written only when something was lost in this second -
     * the device underran, an effect went unheard, or the system called the
     * mixer later than the buffer could cover. It names the cause: the lock
     * (a game thread held the layer while the mixer waited), the mix itself
     * (the CPU), or the call came late (the system's scheduling, the thermal
     * governor, the route).
     */
    double ms_per = 1000.0 / rate;
    double buf_ms = bsize > 0 ? bsize * ms_per : -1.0;
    double gap_ms = w.gap_max / 1e6, wait_ms = w.wait_max / 1e6, spent_ms = w.spent_max / 1e6;
    if (fresh && (xr_new > 0 || f_unheard > 0 || (buf_ms > 0.0 && gap_ms > buf_ms))) {
        const char *why;
        if (buf_ms <= 0.0) why = "no stream to measure against";
        else if (xr_new <= 0 && gap_ms <= buf_ms) why = "no underrun: the game stopped them before the next callback";
        else if (wait_ms > buf_ms * 0.5) why = "the mixer WAITED for the layer's lock (a game thread held it)";
        else if (spent_ms > buf_ms * 0.5) why = "the MIX itself took too long (CPU)";
        else if (gap_ms > buf_ms) why = "the system CALLED the mixer late (scheduling, thermal, route)";
        else why = "the callbacks were on time: the loss is past the mixer (device side)";
        char tl[400];
        snprintf(tl, sizeof(tl),
                 "+%ld ms timing: xruns +%d, effects %ld played / %ld cut / %ld UNHEARD; callback %d..%d frames "
                 "(burst %d), buffer %d frames = %.1f ms; longest gap %.1f ms, lock wait %.2f ms, mix %.2f ms; "
                 "voices %d..%d - %s",
                 now / 1000000L, xr_new, f_plays, f_cut, f_unheard, w.fpc_lo, w.fpc_hi, bburst, bsize, buf_ms,
                 gap_ms, wait_ms, spent_ms, w.voices_lo, w.voices_hi, why);
        meter_say(0, tl);
    }

    if (began) {
        /* the two silent seconds together: the suspect one and this one */
        meter_win_t both = w;
        if (sw.voices_hi > both.voices_hi) both.voices_hi = sw.voices_hi;
        if (sw.gmax > both.gmax) both.gmax = sw.gmax;
        if (sw.raw > both.raw) both.raw = sw.raw;
        if (sw.out > both.out) both.out = sw.out;
        if (sw.peak > both.peak) both.peak = sw.peak;
        char cause_buf[80], text[580];
        const char *cause = silence_cause(cause_buf, sizeof(cause_buf), &both, st,
                                          playing > s_playing ? playing : s_playing, plays + s_plays, asks + s_asks);
        snprintf(text, sizeof(text),
                 "SILENCE began +%ld ms: the output stayed below -80 dBFS for %ld ms (peak %.5f) - %s; playing %d, "
                 "then %d (gain>0.001 %d, top gain %.2f, top voice %.4f raw), music %d; the game asked for "
                 "%ld [%s], then %ld [%s] effects, alSourcePlay %ld, then %ld; stream %s, xruns %d, app pause %d; "
                 "options sfx %.2f music %.2f on %d; listener %.2f",
                 sil_ns / 1000000L, run_ms, (double) both.peak, cause, s_playing, playing, audible,
                 (double) both.gmax, (double) both.raw, music, s_asks, s_ids, asks, ids, s_plays, plays,
                 stream_state_text(st), xr, paused_app, (double) sfx, (double) mus, on, (double) listener);
        meter_say(1, text);
        meter_sources(now);
        meter_calls("SILENCE calls before it", sil_ns - 5000000000L, now);
    }
    if (ended) {
        char text[400];
        snprintf(text, sizeof(text),
                 "SILENCE ended +%ld ms after %ld ms: %s; out peak %.5f; the loudest source now %d (%s) gain %.2f, "
                 "played %ld ms ago from +%lx",
                 now / 1000000L, (now - sil_ns) / 1000000L, why_end, (double) w.peak, loud,
                 loud ? (loud_music ? "music" : "effect") : "-", (double) gmax, loud_ago, loud_from);
        meter_say(1, text);
        meter_calls("SILENCE calls during it", sil_ns, now);
    }
}

#endif /* ISAAC_DIAG: the census and the self-test */

#if ISAAC_DIAG
static const char *state_name(int st) {
    switch (st) {
    case AL_INITIAL: return "initial";
    case AL_PLAYING: return "playing";
    case AL_PAUSED:  return "paused";
    case AL_STOPPED: return "stopped";
    default:         return "?";
    }
}

/* the probe's "audio" command: the stream, the pools and every live source */
void shim_audio_report(void) {
    pthread_mutex_lock(&g_stream_lock);
    int st = g_stream ? (int) AAudioStream_getState(g_stream) : -1;
    int xr = g_stream ? (int) AAudioStream_getXRunCount(g_stream) : -1;
    pthread_mutex_unlock(&g_stream_lock);
    LOG("audio: stream %s, state %d, xruns %d, stream losses %ld, failed opens %ld, retries %ld, "
        "watchdog rebuilds %ld, paused %d, last mix %ld ms ago, %d Hz",
        g_stream ? "present" : "NONE", st, xr, g_stream_losses, g_open_fail, g_open_retry,
        g_watchdog_rebuilds, g_paused, g_mix_ns ? (now_ns() - g_mix_ns) / 1000000L : -1L, g_rate);
    LOG("audio: buffers in use %ld (peak %ld, given %ld, refusals %ld, capacity %d, data %.1f MB); "
        "sources in use %ld (peak %ld, given %ld, refusals %ld, capacity %d)",
        g_buf_used, g_buf_peak, g_buf_given, g_buf_fail, g_buf_cap, g_buf_bytes / 1048576.0,
        g_src_used, g_src_peak, g_src_given, g_src_fail, g_src_cap);
    LOG("audio: device opened %ld / closed %ld, context created %ld / destroyed %ld; "
        "dry queues %ld, lost buffers %ld, queue overflows %ld; name -1 %ld, refills refused %ld, "
        "pool and cache %s",
        g_dev_open, g_dev_close, g_ctx_new, g_ctx_del,
        g_total_starves, g_total_lost_buffers, g_queue_overflows, g_minus_one, g_data_refused,
        g_patched ? "ours" : "the game's");
    pthread_mutex_lock(&g_lock);
    int shown = 0;
    for (int i = 1; i < g_src_cap; i++) {
        alsrc_t *s = &g_src[i];
        if (!s->used) continue;
        if (shown++ >= 40) { LOG("audio:   … and more, the rest is not shown"); break; }
        LOG("audio:   source %d: %s, gain %.2f, pitch %.2f, buffer %d, queued %d, "
            "played %d, position %.0f, looping %d%s",
            i, state_name(s->state), (double) s->gain, (double) s->pitch, s->buffer,
            s->qn, s->dn, s->pos, s->looping, s->starved ? ", STARVED" : "");
    }
    pthread_mutex_unlock(&g_lock);
    if (!shown) LOG("audio:   no live sources");
}
#endif

/* ------------------------------------------------------- what music is asked for */
/*
 * A LOG OF EVERY TRACK THE GAME STARTS, to find out why the greed mode's third
 * wave ends without its victory jingle (the owner, 14.09.2026).
 *
 * The music manager's play is 0x822c: x0 is the manager, w1 the track number,
 * s0 the volume it is asked at. The entry above it (0x8210) is a wrapper that
 * only fetches a default volume, so the hook sits on the real head. The number
 * is checked against 0x66 there, which is why the table in music.xml runs to
 * 102 entries.
 *
 * What to look for: the greed wave should ask for 92 "Challenge Room Outro
 * (jingle)" - or 40 "Boss Rush (jingle)", both of them the same
 * `Ambush Jinle outro V3_04.ogg` - before whatever music follows. If the number
 * never appears, the game is not asking; if it appears and the next track comes
 * in the same breath, the music is cutting the jingle off.
 *
 * REPORT ONLY.
 */
void shim_audio_track(unsigned long manager, unsigned long id, float volume) {
    (void) manager;
    if (!shim_report_active()) return;
    static unsigned long was = ~0ul;
    static int said;
    if (id == was || said > 200) return;         /* a held note asks again every frame */
    was = id;
    said++;
    char line[80];
    snprintf(line, sizeof(line), "track %lu asked for (volume %.2f)", id, (double) volume);
    report_line("music", line);
    LOG("music: track %lu", id);
}
