/*
 * A hook that continues: find out whether one of the game's functions is
 * called, without breaking it.
 *
 * An ordinary stub answers instead of the function. Here we need something
 * else — leave a mark and let the call through, otherwise the game simply will
 * not run. It works like this:
 *
 *   1. the first four instructions of the function are copied to our page;
 *   2. a jump to the original's fifth instruction is written after them;
 *   3. a jump to our handler is written over the start of the original;
 *   4. the handler saves the arguments, writes to the log and goes to the copy.
 *
 * The limitation is real and worth remembering: only instructions that do not
 * depend on their own address may be copied. Prologues such as stp/sub are
 * fine, adrp is not, and a function starting with one cannot be hooked this
 * way.
 */
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <time.h>
#include <sys/mman.h>
#include <android/log.h>
#include "diag.h"

#if ISAAC_DIAG
#define LOG(...) __android_log_print(ANDROID_LOG_INFO, "isaac", __VA_ARGS__)
#else
#define LOG(...) do { } while (0)      /* the build we play on has no log */
#endif

unsigned long shim_image_base(void);

/*
 * Forty-eight places for the thirty-eight the loader registers. With thirty-two
 * the last six were refused - shim_trace_add answers -1 and nobody looked - and
 * the hooks that failed to stand were the ones nobody noticed missing.
 */
/*
 * 96 since 01.10.2026 (the plan, "General rules"): 59 were asked for in the
 * diagnostic build alone, and the first batch adds its own - the unlock watch
 * four (spawner.c, mark '7'), the sound two (audio.c).
 */
#define TRACE_MAX 96
#define SLOT 64                     /* room for the copy, the jump and data */

/* the first letters that pick a handler in shim_trace_hit */
/*
 * The letters ran out at thirty-eight, so the digits carry on from there.
 * Forty-three marks with '7' (01.10.2026: the unlock watch, spawner.c). Taken
 * by a hook today: all but '?', 'Q', 'T', 'U' (their handlers wait for hooks
 * placed elsewhere) and '2'. A new mark is a new character here AND a handler
 * below; a hook whose first letter is on this list goes to that handler.
 * '<' and '|' (01.10.2026): the sound census, a sound object plays / stops
 * its source (audio.c, report builds only).
 */
/* '5' (05.10.2026): the player HUD's corners are laid (hudlayout.c, 0x6ee608) */
/* '6' and '8': the ground truth of game time (14.10.2026) - a mark missing here is a hook that counts nothing */
/* '{' and '}' (17.10.2026): the room drawn on the camera's whole unit, and laid back with its fraction (screen.c) */
static const char MARKS[] = "@#%&$?^~>+=*SPWBACEGHJMDFKLNIOQRTUXYVZ12347<|568{}[])";

typedef struct {
    const char *name;
    void *cont;                     /* where to continue: copy plus jump */
    long hits;
    int args;                       /* show arguments: name starts with '!' */
    char mark;                      /* which handler below is this hook's, 0 for none */
} trace_t;

static trace_t g_tr[TRACE_MAX];
static int g_ntr;
static uint8_t *g_arena;

/*
 * Is the memory at this address readable? The check goes through a pipe: the
 * kernel answers EFAULT instead of taking us down. Otherwise the first junk
 * argument in a log line would cost the whole session.
 */
/*
 * The pages a range covers, kept once they have answered.
 *
 * THIS IS A HOT PATH, and it did not look like one. The panel walks the whole
 * entity list every frame and asks about every entity; the spikes walk the
 * room grid and ask about every cell. Entities are scattered across the heap,
 * so eight remembered ranges held almost nothing - and the cache was thrown
 * away wholesale every 512 answers on top of that. What was left was two
 * syscalls a page, hundreds of times a frame: measured in the testers'
 * reports as heavy drops in rooms full of entities, which is exactly what a
 * room full of Brimstone lasers is.
 *
 * So the pages are kept in a table addressed by the page number itself - a hit
 * is one load and a compare, and thousands of pages fit. A page that has
 * answered stays for HOLD_MS and no longer: the window in which a mapping
 * could go away and still be believed is a bounded half second, which is what
 * the wholesale forgetting was for.
 */
#define PAGES 8192                  /* addressed by the page number; two per entity heap */
#define PAGE_MASK (PAGES - 1)
#define HOLD_MS 500                 /* how long a page may be believed */
static uintptr_t g_page[PAGES];
/*
 * WHEN each page answered (hotfix 1.72, perf item 5): a page is believed for
 * HOLD_MS from its own probe. The table used to be wiped whole every 500 ms -
 * 64 KB cleared, and on the next frame every page in use probed again at once,
 * a hundred and more in a busy room, two system calls each.
 */
static uint32_t g_page_at[PAGES];

/*
 * How many pages had to be ASKED about, for the frames line. A room full of
 * entities used to ask hundreds of times a frame - two syscalls each - and the
 * only honest way to show that the table works is to count what is left.
 */
long g_readable_probes, g_readable_asks;

void shim_trace_probe_counts(long *probes, long *asks) {
    if (probes) *probes = g_readable_probes;
    if (asks) *asks = g_readable_asks;
    g_readable_probes = 0;
    g_readable_asks = 0;
}

static long long now_ms_mono(void) {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (long long) t.tv_sec * 1000 + t.tv_nsec / 1000000;
}

static long page_size(void) {
    static long ps;
    if (!ps) { ps = sysconf(_SC_PAGESIZE); if (ps <= 0) ps = 4096; }
    return ps;
}

/*
 * Is this memory there?
 *
 * The trick is the pipe: the kernel reads the bytes on our behalf and refuses
 * with EFAULT instead of killing us, so an address can be asked about without
 * a signal handler.
 *
 * It is asked about PER PAGE, and that is the whole point of this rewrite.
 * Mapping is page-granular - if one byte of a page answers, the page is there -
 * while the old code walked the range in 64-byte bites. The call sites do not
 * pass sizes but OFFSETS of the field they are about to read: the pause
 * button's is 0x20214, the bomb button's 0x20880. So one question used to cost
 * two thousand pipe writes and reads, a hundred and thirty kilobytes copied
 * through the kernel twice - and both of those questions were asked EVERY
 * FRAME. Eight to nine thousand system calls a frame, half a megabyte of
 * copying: unnoticeable on a fast phone, a whole frame on a weak one, which is
 * exactly where the port was reported to crawl.
 *
 * Thirty-three probes instead of two thousand, and none at all once the answer
 * is remembered.
 */
/*
 * The same test, under a name other files can call. eid.c walks the game's own
 * entity list, and a stale pointer there must cost a skipped entry rather than
 * the session.
 */
int shim_trace_readable(const void *p, unsigned long n);

/*
 * One byte of the page through the pipe: ONE system call (hotfix 1.72). The
 * pipe does not block and is drained only when it is full - once in 65536
 * probes - where every probe used to write a byte and read it back. The
 * kernel's answer is the same: EFAULT for memory that is not there or not
 * readable (a PROT_NONE guard page included, which mincore would miss).
 */
static int probe_byte(int fd[2], int nonblocking, uintptr_t a) {
    for (int tries = 0; tries < 3; tries++) {
        ssize_t k = write(fd[1], (const void *) a, 1);
        if (k == 1) {
            if (!nonblocking) { char waste[1]; if (read(fd[0], waste, 1) != 1) return 0; }   /* the old way */
            return 1;
        }
        if (k < 0 && errno == EINTR) continue;
        if (k < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            char sink[4096];
            while (read(fd[0], sink, sizeof(sink)) > 0) { }
            continue;
        }
        return 0;
    }
    return 0;
}

static int readable(const void *p, unsigned long n) {
    static int fd[2] = { -1, -1 }, nonblocking;
    if (!p || (unsigned long) (uintptr_t) p < 0x1000) return 0;
    if (!n) n = 1;
    uintptr_t lo = (uintptr_t) p, hi = lo + n - 1;
    if (hi < lo) return 0;

    long ps = page_size();
    uintptr_t mask = (uintptr_t) ps - 1;

    /* nothing is believed for long: each page for HOLD_MS from its own answer */
    uint32_t now = (uint32_t) now_ms_mono();

    g_readable_asks++;
    for (uintptr_t a = lo & ~mask;; a += (uintptr_t) ps) {
        size_t i = (size_t) ((a / (uintptr_t) ps) & PAGE_MASK);
        if (g_page[i] != a || (uint32_t) (now - g_page_at[i]) >= HOLD_MS) {
            g_readable_probes++;
            if (fd[0] < 0) {
                if (pipe(fd) != 0) { fd[0] = fd[1] = -1; return 0; }
                nonblocking = fcntl(fd[0], F_SETFL, O_NONBLOCK) == 0 && fcntl(fd[1], F_SETFL, O_NONBLOCK) == 0;
            }
            if (!probe_byte(fd, nonblocking, a)) return 0;
            g_page[i] = a;
            g_page_at[i] = now;
        }
        if (a >= (hi & ~mask)) break;
    }
    return 1;
}

int shim_trace_readable(const void *p, unsigned long n) { return readable(p, n); }

/* prints an argument: as a bare number, and as text if text lives there */
static void tell(const char *name, int k, unsigned long v) {
    const char *s = (const char *) (uintptr_t) v;
    if (readable(s, 16)) {
        int printable = 1, i;
        for (i = 0; i < 16 && s[i]; i++)
            if ((unsigned char) s[i] < 0x20 || (unsigned char) s[i] > 0x7e) printable = 0;
        if (printable && s[0]) { LOG("TRACE:   %s arg %d = %lx  \"%.60s\"", name, k, v, s); return; }
    }
    LOG("TRACE:   %s arg %d = %lx", name, k, v);
}

/*
 * A special reading for the stick: the sprite keeps the name of its current
 * animation at +0x38 (an Apple-layout string), and its position and deflection
 * live next to the object itself. Three numbers explain the picture better
 * than any guess.
 */
const char *shim_str_bytes(const void *);

static void tell_stick(const char *name, unsigned long self) {
    const unsigned char *o = (const unsigned char *) self;
    static int told, seen, moaned;
    /* 0x210 is enough: we never look further into the object */
    if (!readable((const void *) self, 0x210)) {
        if (!moaned) { moaned = 1; LOG("TRACE: %s %lx — unreadable", name, self); }
        return;
    }
    /*
     * Only frames where a finger is on the stick. Idle samples in the menu ate
     * the quota long before a run ever started.
     */
    seen++;
    if (!o[0x168] || told >= 8) return;
    told++;
    const float *base = (const float *) (o + 0x1b8);
    const float *off = (const float *) (o + 0x1a4);
    const void **knob = (const void **) (o + 0x10 + 0x38);
    const void **ring = (const void **) (o + 0x1d0 + 0x38);
    const char *kn = (readable(knob, 8) && readable(*knob, 24)) ? shim_str_bytes(*knob) : "?";
    const char *rn = (readable(ring, 8) && readable(*ring, 24)) ? shim_str_bytes(*ring) : "?";
    const float *anchor = (const float *) (o + 0x180);
    const float *now = (const float *) (o + 0x178);
    const float *rpos = (const float *) (o + 0xec);
    LOG("TRACE: %s %lx held=%d: place %.0f %.0f, anchor %.0f %.0f, finger %.0f %.0f",
        name, self, o[0x168], (double) base[0], (double) base[1],
        (double) anchor[0], (double) anchor[1], (double) now[0], (double) now[1]);
    LOG("TRACE:    deflection %.2f %.2f, sprite shift %.0f %.0f, knob \"%.24s\", ring \"%.24s\"",
        (double) off[0], (double) off[1], (double) rpos[0], (double) rpos[1], kn, rn);
    /*
     * The overlay's own fields. Since everything else matches between the two
     * sticks, the difference has to be here: a sprite keeps its offset, scale
     * and colour right beside itself.
     *
     * A sprite has a byte at +0x90 that swaps the order of its two parts. If
     * the left overlay has a different value from the right one, that is the
     * "knob underneath the ring", and it is fixed by a single byte.
     */
    LOG("TRACE:    order: knob +90=%d +149=%d, ring +90=%d +149=%d",
        o[0x10 + 0x90], o[0x10 + 0x149], o[0x1d0 + 0x90], o[0x1d0 + 0x149]);
    const float *sp = (const float *) (o + 0x10 + 0xc0);
    LOG("TRACE:    knob +c0: %.2f %.2f %.2f %.2f %.2f %.2f %.2f %.2f",
        (double) sp[0], (double) sp[1], (double) sp[2], (double) sp[3],
        (double) sp[4], (double) sp[5], (double) sp[6], (double) sp[7]);
    LOG("TRACE:    knob +e0: %.2f %.2f %.2f %.2f %.2f %.2f %.2f %.2f",
        (double) sp[8], (double) sp[9], (double) sp[10], (double) sp[11],
        (double) sp[12], (double) sp[13], (double) sp[14], (double) sp[15]);
}

/* pinning the sticks lives elsewhere: that is behaviour, not observation */
void shim_sticks_pin(unsigned long self, int (*probe)(const void *, unsigned long));
void shim_sticks_watch_draw(unsigned long sprite);
void shim_sticks_diff(void);
void shim_sticks_scale_hunt(void);
void shim_menu_probe(unsigned long, int (*)(const void *, unsigned long));
void shim_sticks_watch_submit(unsigned long elem, unsigned long idx, unsigned long ctx);

/*
 * What a '=' trace remembers, and why each piece is worth its bytes.
 *
 * The name of the last thing the game worked on says WHICH file a crash
 * happened on; the count says whether it was the first or the thousandth; the
 * lowest stack pointer says how close that work came to the end of the thread's
 * stack. Two phones die where this one does not, and between "the parsing is
 * broken" and "the stack ran out" only the last of these can decide.
 */
static char g_last_arg[160];
static unsigned long g_arg_count;
static unsigned long g_sp_low;
static unsigned long g_stack_base, g_stack_size;

#if ISAAC_DIAG
/*
 * THE GROUND TRUTH OF GAME TIME (14.10.2026). The frames line used to say
 * "N updates a second" as presents / 2 - a derivation, not a count. These
 * two count the real thing, hit by hit: the game update the manager's tick
 * runs on its even ticks (0x88f880, mark '6') and the interpolation pass of
 * its odd ticks (0x88f770, mark '8'). objc.c reads them per window.
 */
long g_truth_updates, g_truth_interp;
#endif

const char *shim_trace_last_arg(void) { return g_last_arg; }
unsigned long shim_trace_count(void) { return g_arg_count; }
unsigned long shim_trace_sp_low(void) { return g_sp_low; }

void shim_set_stack_bounds(unsigned long base, unsigned long size) {
    g_stack_base = base;
    g_stack_size = size;
}

/* how much of the thread's stack is still below us, in KB */
unsigned long shim_stack_left_kb(void) {
    if (!g_stack_base || !g_sp_low || g_sp_low < g_stack_base) return 0;
    return (unsigned long) ((g_sp_low - g_stack_base) >> 10);
}

/* called from the assembly wrapper; returns the address to continue at */
void *shim_trace_hit(long idx, unsigned long a0, unsigned long a1,
                     unsigned long a2, unsigned long a3,
                     unsigned long a4, unsigned long a5, unsigned long *regs) {
    (void) a4;
    if (idx < 0 || idx >= g_ntr) return NULL;
    trace_t *t = &g_tr[idx];
    if (t->hits == 0) {
        LOG("TRACE: %s was called", t->name);
        /* the first hit of every hook, so the report can tell "never placed" from "never reached" */
        int shim_report_active(void);
        void report_line(const char *kind, const char *text);
        if (shim_report_active()) {
            char line[120];
            snprintf(line, sizeof(line), "first hit: %s", t->name);
            report_line("trace", line);
        }
    }
    /*
     * Who called. The assembly wrapper builds a real frame (stp x29, x30), so
     * the caller's return address is one level up. Without it a trace says "I
     * was poked" but not from where, and taking apart the run reset needed
     * exactly that.
     *
     * The '&' mark is behaviour, not observation: open the service gate, but
     * ONLY when the request comes from the daily-run menu (return to
     * 0x62d6b0). It must not stay open all the time: at startup the game files
     * its own request, and with the gate open a run began right after the
     * intro movie.
     */
    /*
     * '@' - WHICH SOUND, AND WHO ASKED FOR IT.
     *
     * The port's own two screens - the credits page and the touch editor - move
     * the menu's camera themselves, so the menu's transition code never runs and
     * they arrived in silence. Fitting them with the sheet of paper the menu
     * plays (credits.c, ids 17 and 18) got a sound out of them, and the owner
     * said at once it was not the one the rest of the menu makes (14.09.2026).
     *
     * So the player itself is watched: 0x671c8c(manager, id, ...), and every
     * call is written down with the id AND the offset it was called from. The
     * id alone would not settle it - the menu plays a dozen - but the caller
     * does: walking the menu and reading the offsets says which line of code
     * belongs to a transition, and that is the sound to copy.
     *
     * Report builds only, and it stops after sixty lines.
     */
    if (t->mark == '@') {
        int shim_report_active(void);
        void report_line(const char *, const char *);
        /* the sound meter counts every ask, past the sixty lines too: the id and s0, the volume (audio.c) */
        void shim_audio_sfx_asked(unsigned long id, float volume);
        float volume = -1.0f;
        if (regs) memcpy(&volume, &regs[10], sizeof(volume));       /* q0's low word */
        shim_audio_sfx_asked(a1 & 0xffff, volume);
        static int told;
        if (told < 60 && shim_report_active()) {
            told++;
            unsigned long back = (unsigned long) __builtin_return_address(1);
            unsigned long base = shim_image_base();
            unsigned long from = (base && back > base) ? back - base : back;
            char line[140];
            snprintf(line, sizeof(line), "sound %lu, asked for at %s%lx",
                     a1 & 0xffff, (base && back > base) ? "" : "outside the image ", from);
            report_line("sfx", line);
        }
    }
    if (t->mark == '&') {
        void online_service_ready(void);
        unsigned long back = (unsigned long) __builtin_return_address(1);
        unsigned long base = shim_image_base();
        unsigned long from = (base && back > base) ? back - base : 0;
        /*
         * Two requests arrive from the menu: the first when the screen OPENS,
         * the second when "Daily Run" is pressed. There is nothing to tell
         * them apart — the measurement showed the same call site, the same
         * arguments (root, 1, 0) and the same caller one level up (768e24):
         * literally the same code runs.
         *
         * So we tell them apart by order: skip the first, open the gate on the
         * second. The counter resets right there, so entering the menu again
         * behaves the same. This works by observation rather than by meaning —
         * worth remembering if the menu ever changes.
         */
        if (from == 0x62d6b0) {
            static int requests;
            if (++requests >= 2) {
                requests = 0;
                LOG("TRACE: %s — pressed in the menu, opening the service gate", t->name + 1);
                online_service_ready();
            } else {
                LOG("TRACE: %s — screen opening, skipping", t->name + 1);
            }
        }
    }
    if (t->mark == '^') {
        static int said[TRACE_MAX];
        if (said[idx] < 12) {
            said[idx]++;
            unsigned long back = (unsigned long) __builtin_return_address(1);
            unsigned long base = shim_image_base();
            unsigned long above = (unsigned long) __builtin_return_address(2);
            LOG("TRACE: %s called from %06lx, and that one from %06lx", t->name + 1,
                base && back > base ? back - base : back,
                base && above > base ? above - base : above);
        }
    }
    /*
     * The '=' mark remembers the first argument as text and says nothing.
     *
     * It exists for the crash the release build cannot explain on its own: the
     * report reaches us, but a fault address alone does not say WHICH file the
     * game was working on. Silent, so it costs nothing in the working build,
     * and bounded, so it cannot itself become the failure.
     */
    if (t->mark == '=') {
        const char *s = (const char *) (uintptr_t) a0;
        if (readable(s, 1)) {
            unsigned i = 0;
            while (i + 1 < sizeof(g_last_arg) && s[i]) { g_last_arg[i] = s[i]; i++; }
            g_last_arg[i] = 0;
        } else {
            g_last_arg[0] = 0;
        }
        g_arg_count++;
        /*
         * How deep the game's stack has gone. This trace sits inside the frame
         * that moves the stack pointer 160 KB in one step, so the value here is
         * the honest deepest point of the whole XML path. A new low is written
         * out at once, because the launch it belongs to may not survive to
         * write anything later — but only while the file stays short.
         */
        {
            void report_line(const char *kind, const char *text);
            unsigned long sp = (unsigned long) __builtin_frame_address(0);
            if (!g_sp_low || sp < g_sp_low) {
                static int said;
                unsigned long was = g_sp_low;
                g_sp_low = sp;
                if (g_stack_base && said < 8 && (!was || was - sp > (64u << 10))) {
                    said++;
                    char line[160];
                    snprintf(line, sizeof(line),
                             "stack: %lu KB used of %lu KB, deepest at file %lu (%.60s)",
                             (unsigned long) ((g_stack_base + g_stack_size - sp) >> 10),
                             (unsigned long) (g_stack_size >> 10),
                             g_arg_count, g_last_arg);
                    report_line("check", line);
                }
            }
        }
    }
    /*
     * The '>' mark: every call, with the whole chain of callers.
     *
     * The ordinary trace speaks once - "was called" - and that is what made
     * the earlier reading of the run reset wrong: the launch functions had all
     * spoken at the first run, so their silence at a reset was taken for proof
     * that the reset goes past them. It proved nothing. This mark repeats, and
     * prints who called, so a second launch is as visible as the first.
     */
    if (t->mark == '>') {
        void shim_probe_who(const char *);
        static int said[TRACE_MAX];
        if (said[idx] < 40) {
            char tag[80];
            snprintf(tag, sizeof(tag), "%s #%ld", t->name + 1, t->hits + 1);
            said[idx]++;
            shim_probe_who(tag);
        }
    }
    /*
     * The '+' mark: the run request, printed while it is still pending.
     *
     * The game does not start a run where the button is pressed. Whoever wants
     * one fills a small record at root+0x1f6b40 and raises its first byte; the
     * per-frame step reads it, acts, and clears it. So the way to learn what a
     * reset asks for is to look at that record from inside the step, before it
     * is served - the fields alone say which of the three kinds of request it
     * is and with what parameters.
     */
    if (t->mark == '+') {
        /*
         * Behaviour first: a long press on the pause button asks for a reset,
         * and the request has to be in the record before this very function
         * reads it. Standing at its entry is the whole point - there is no
         * moment closer to the reading, and nothing else runs in between.
         */
        void shim_reset_serve(unsigned long);
        shim_reset_serve(a0);
        static int said;
        const unsigned char *q = (const unsigned char *) (uintptr_t) (a0 + 0x1f6b40);
        if (said < 24 && readable(q, 0x130) && q[0]) {
            said++;
            const char *seed = "?";
            const void *str = (const void *) (q + 0x10);
            if (readable(str, 24)) seed = shim_str_bytes(str);
            {   /* the seed the "repeat this run" branch copies from */
                const void *held = (const void *) (a0 + 0x49c68);
                const char *t = readable(held, 24) ? shim_str_bytes(held) : "?";
                LOG("request: the run's own seed string is \"%.20s\"", t ? t : "(null)");
            }
            LOG("request: kind %02x %02x %02x, load %02x, fields %d %d %d, seed \"%.20s\"",
                q[0], q[1], q[2], q[0x84],
                *(const int *) (q + 4), *(const int *) (q + 8),
                *(const int *) (q + 0x80), seed ? seed : "(null)");
            for (int row = 0; row < 0x120; row += 32) {
                char line[128];
                int at = 0;
                for (int i = 0; i < 32; i++)
                    at += snprintf(line + at, sizeof(line) - (size_t) at, "%02x%s",
                                   q[row + i], (i % 8) == 7 ? " " : "");
                LOG("request:   +%03x %s", row, line);
            }
        }
    }
    /*
     * The 'S' mark: a call to do with starting a run, in full.
     *
     * Both the request (0x672284) and the launch (0x88b144) take the same
     * shape - (root, who, what, seed text, extra) - and the seed arrives as a
     * string, so it is printed as one. The chain of callers comes with it: that
     * is what tells a reset from the pad apart from ours.
     */
    if (t->mark == 'S') {
        static int said[TRACE_MAX];
        if (said[idx] < 16) {
            said[idx]++;
            const void *str = (const void *) (uintptr_t) a3;
            const char *txt = (readable(str, 24) ? shim_str_bytes(str) : NULL);
            LOG("call %s: root %lx, who %d, what %d, seed \"%.24s\", extra %d",
                t->name + 1, a0, (int) a1, (int) a2, txt ? txt : "(unreadable)", (int) a4);
            shim_probe_who(t->name + 1);
        }
    }
    if (t->mark == '@') tell_stick(t->name + 1, a0);
#if ISAAC_DIAG
    /* the ground truth of game time: real calls, counted (the frames line, objc.c) */
    if (t->mark == '6') g_truth_updates++;
    if (t->mark == '8') g_truth_interp++;
#endif
    if (t->mark == '#') { shim_sticks_pin(a0, readable); DIAG(shim_sticks_scale_hunt()); }
    if (t->mark == 'W') { void shim_hud_zone(unsigned long, int (*)(const void *, unsigned long));
                             void shim_widget_name(unsigned long, int (*)(const void *, unsigned long));
                             shim_hud_zone(a0, readable);
                             /* the twins' drop button: its box, and x2 - the enable (twintrinkets.c) */
                             { void shim_twins_zone(unsigned long, unsigned long *);
                               shim_twins_zone(a0, regs); }
                             DIAG(shim_widget_name(a0, readable)); }
    if (t->mark == 'P') { void shim_reset_pin(unsigned long, int (*)(const void *, unsigned long));
                             void shim_hud_button_place(unsigned long,
                                                        int (*)(const void *, unsigned long));
                             void shim_hud_twin_draw(unsigned long,
                                                     int (*)(const void *, unsigned long));
                             /* a Button's draw: the freeze button shown for a follower too */
                             shim_hud_twin_draw(a0, readable);
                             /* the twins' one and the pause one are put where they were placed */
                             shim_hud_button_place(a0, readable);
                             /*
                              * and only THEN the pause button's box is copied for the
                              * long press (reset.c): copied first, it held the game's
                              * own place and the restart stayed behind a moved button
                              * (hotfix 1.71)
                              */
                             shim_reset_pin(a0, readable); }
    if (t->mark == 'B') { void shim_bomb_button(unsigned long, int (*)(const void *, unsigned long));
                             void shim_pocket_overlay(unsigned long);
                             shim_bomb_button(a0, readable);
                             shim_pocket_overlay(a0); }
    if (t->mark == 'O') { void shim_bomb_tell(unsigned long, int (*)(const void *, unsigned long));
                             void shim_hud_widgets_tell(unsigned long,
                                                        int (*)(const void *, unsigned long));
                             shim_bomb_tell(a0, readable);
                             shim_hud_widgets_tell(a0, readable);
                             { void shim_hud_trinket_tell(unsigned long,
                                                          int (*)(const void *, unsigned long));
                               shim_hud_trinket_tell(a0, readable); } }
    if (t->mark == '*') { void shim_hud_button_place(unsigned long,
                                                     int (*)(const void *, unsigned long));
                          void shim_reset_pin(unsigned long, int (*)(const void *, unsigned long));
                          shim_hud_button_place(a0, readable);     /* the hit test */
                          /* the long press's copy of the box, here too: a pad hides the
                             overlay's draw, not its touches (reset.c) */
                          shim_reset_pin(a0, readable); }
    if (t->mark == 'Y') { void shim_bomb_group_draw(unsigned long,
                                                       int (*)(const void *, unsigned long));
                             void shim_hud_twin_draw(unsigned long,
                                                     int (*)(const void *, unsigned long));
                             shim_bomb_group_draw(a0, readable);
                             /* 0x860e70: after the layout, before 0x8620f0 tests +0xc */
                             shim_hud_twin_draw(a0, readable); }
    if (t->mark == 'A') { void shim_bag_press(unsigned long, unsigned long,
                                                 int (*)(const void *, unsigned long));
                             shim_bag_press(a0, a1, readable); }
    if (t->mark == 'C') { void shim_pocket_action(unsigned long,
                                                     int (*)(const void *, unsigned long));
                             shim_pocket_action(a0, readable); }
    if (t->mark == 'A' || t->mark == 'C') {
        void shim_widget_action(const char *, unsigned long,
                                int (*)(const void *, unsigned long));
        DIAG(shim_widget_action(t->name + 1, a0, readable));
    }
    if (t->mark == 'E') { void shim_pocket_publish(unsigned long, unsigned long, unsigned long);
                             void shim_bag_walk(unsigned long, unsigned long, unsigned long,
                                                int (*)(const void *, unsigned long));
                             shim_pocket_publish(a0, a1, a3);   /* a0: the row being walked */
                             shim_bag_walk(a0, a1, a3, readable);
                             { void shim_marked_publish(unsigned long);
                               shim_marked_publish(a3); } }
    if (t->mark == 'R') { void shim_app_effects_wiped(unsigned long, unsigned long, unsigned long);
                          shim_app_effects_wiped(a0, a1, regs ? regs[9] : 0); }
    if (t->mark == 'Z') { void shim_touchedit_name(unsigned long);
                             shim_touchedit_name(a0); }
    if (t->mark == 'V' && regs) { void shim_touchedit_screen(unsigned long, unsigned long,
                                                                 unsigned long *);
                                     shim_touchedit_screen(a0, a1, regs); }
    if (t->mark == 'V' && regs) { void shim_mods_screen(unsigned long, unsigned long,
                                                      unsigned long *);
                                 shim_mods_screen(a0, a1, regs); }
    if (t->mark == 'M') { void shim_hud_row(unsigned long, unsigned long, unsigned long,
                                               const unsigned long *);
                             shim_hud_row(a0, a1, a2, regs); }
    /*
     * The canvas is widened here and nowhere else: the fit reads the object at
     * its very first instruction, so this is the last moment the width can be
     * set, and the only one where the window size is known (screen.c).
     */
    if (t->mark == 'G') { void shim_screen_fit(unsigned long, unsigned long, unsigned long);
                             shim_screen_fit(a0, a1, a2); }
    /*
     * A menu screen that hands the box test a zero place gets the canvas
     * offset put back into its arguments (screen.c). The stub restores the
     * float registers after us, so writing them here is what the game sees.
     */
    if (t->mark == 'H') { void shim_screen_box_test(unsigned long *);
                             shim_screen_box_test((unsigned long *) regs); }
    if (t->mark == 'J') { void shim_screen_finger(unsigned long *);
                             shim_screen_finger((unsigned long *) regs); }
    /*
     * Every surface is born here, and a canvas-sized one is born larger so the
     * scene can be drawn at a whole number of pixels to the unit (screen.c).
     */
    if (t->mark == 'N') { void shim_screen_surface_born(unsigned long *);
                             shim_screen_surface_born((unsigned long *) regs); }
    /* the entity list, for the item descriptions (eid.c) */
    if (t->mark == 'I') { void shim_eid_entity_list(unsigned long);
                             shim_eid_entity_list(a0); }
    /* the overlay's draw: where the item description is painted (eid.c) */
    /*
     * The item panel is drawn INSIDE the game's own HUD pass ('X'), not over
     * the finished picture ('O').
     *
     * Over the picture it was over everything: the pause paper, the stage
     * transition, the loading screen - all of them are drawn after the HUD,
     * and a panel drawn last sits on top of all three. In the mod it is part
     * of the HUD and nothing else, so it belongs where the HUD is drawn: what
     * comes later covers it, and when the HUD is not drawn at all there is no
     * panel either.
     *
     * The inventory list stays on the overlay: it is ours, it is a whole
     * screen of its own, and it is put away by a tap rather than by a menu
     * opening over it.
     */
    if (t->mark == '5' && regs) { void shim_hud_layout(unsigned long *); shim_hud_layout(regs); }
    if (t->mark == '{' && regs) { void shim_screen_room_render(unsigned long *); shim_screen_room_render(regs); }
    if (t->mark == '}' && regs) { void shim_screen_room_laid(unsigned long *); shim_screen_room_laid(regs); }
    if (t->mark == '[' && regs) { void shim_screen_room_slide(unsigned long *); shim_screen_room_slide(regs); }
    if (t->mark == ']' && regs) { void shim_screen_menu_draw(unsigned long *); shim_screen_menu_draw(regs); }
    if (t->mark == ')' && regs) { void shim_screen_menu_laid(unsigned long *); shim_screen_menu_laid(regs); }
    if (t->mark == 'X') { void shim_twins_draw(void); shim_twins_draw();   /* before 0x6eeb78 runs */
                          void shim_eid_draw(void); shim_eid_draw();
                          void shim_mods_draw(void); shim_mods_draw(); }
    if (t->mark == 'O') { void shim_marked_draw(void); void shim_items_draw(void);
                             void shim_touchedit_draw(void);
                             void shim_spawner_draw(void);
                             void shim_fps_draw(void);
                             shim_marked_draw(); shim_items_draw(); shim_touchedit_draw();
                             shim_spawner_draw(); shim_fps_draw(); }
    /* which font object the game draws its own text with (eid.c) */
    if (t->mark == 'Q') { void shim_eid_saw_font(unsigned long);
                             shim_eid_saw_font(a0); }
    /*
     * The difficulty on the character screen, in three parts (menu.c).
     *
     * 'D' is the screen's input handler at its door, and x0 there is the
     * screen itself - the only place that hands it over plainly, which is why
     * the mark stands on a door rather than beside the value.
     *
     * 'F' is the widget's drawing point, still in s0/s1: the store that puts
     * it into the frame is the first of the five words we copied, so it has
     * not happened yet and the registers are where the numbers are.
     *
     * 'K' is the single store the difficulty ever passes through, taken the
     * word after it, so the value the finger asked for replaces the value the
     * stepping arrived at before the screen saves it.
     */
    if (t->mark == 'D') { void shim_menu_difficulty_screen(unsigned long);
                             shim_menu_difficulty_screen(a0); }
    if (t->mark == 'F' && regs) { void shim_menu_difficulty_drawn(float, float);
                                     float x, y;
                                     memcpy(&x, &regs[10], sizeof(x));
                                     memcpy(&y, &regs[12], sizeof(y));
                                     shim_menu_difficulty_drawn(x, y); }
    if (t->mark == 'K') { void shim_menu_difficulty_chosen(void);
                             shim_menu_difficulty_chosen(); }
    /* the touch device's buttons being pressed and released: who, and which */
    if (t->mark == 'U') { void shim_menu_action_asked(unsigned long, unsigned long);
                             shim_menu_action_asked(a0, a1); }
    if (t->mark == 'T' && regs) { void shim_menu_touch_button(const char *, unsigned long, unsigned long);
                                     shim_menu_touch_button(t->name + 1, a1, regs[27]); }
    /*
     * A save has just been read. The three daily-run trophies are looked at
     * here and nowhere else: this is the one moment the answer can change,
     * and it catches saves that already exist as well as new ones.
     */
    if (t->mark == 'L') { void shim_menu_daily_trophies(void);
                             shim_menu_daily_trophies(); }
    DIAG(if (t->mark == '%') shim_sticks_watch_draw(a0));
    /* the same Sprite::Render: where the menu's two strap sprites are drawn (screen.c, M2 of the tap audit) */
    DIAG(if (t->mark == '%') { void shim_screen_sprite_render(unsigned long, unsigned long);
                               shim_screen_sprite_render(a0, a1); });
    if (t->mark == '$') shim_menu_probe(a0, readable);
    if (t->mark == '?' && regs) {
        /* x9 sits in the saved block at offset 208, then x10, x11, x12 */
        static int said;
        if (said < 12) { said++;
            LOG("%s: x9=%lu x10=%lx x11=%lx x12=%lu", t->name + 1,
                regs[26], regs[27], regs[28], regs[29]); }
    }
    DIAG(if (t->mark == '&') shim_sticks_watch_submit(a0, a2, a5));
    /* The '~' mark is behaviour: clear the daily-run sign's layers (menu.c). */
    if (t->mark == '~') {
        void daily_sign_off(unsigned long);
        daily_sign_off(a0);
        /*
         * The credits page is drawn HERE, at the main menu's own draw, and not
         * with the overlay: drawn there it came out over everything, the
         * vignette included, while it is meant to be a thing lying in the menu
         * (the owner, 13.09.2026). Two hooks wear this mark - the screen's
         * update and its draw - and only the draw is the moment for it.
         */
        if (strstr(t->name, "draw")) { void shim_credits_draw(void); shim_credits_draw();
                                       void shim_tweaks_draw(void); shim_tweaks_draw(); }
    }
    /* a sprite layer is about to be drawn: the pause strap goes under its paper (straps.c) */
    if (t->mark == '4') { void shim_straps_layer(unsigned long, unsigned long, unsigned long,
                                                 unsigned long, unsigned long);
                          shim_straps_layer(a0, a1, a2, a3, a4); }
    /* the stats column is being drawn: where, for the planetarium line (mods.c) */
    if (t->mark == '3') { void shim_mods_stats_seen(unsigned long, unsigned long);
                          shim_mods_stats_seen(a0, a1); }
    if (t->mark == '1') { void shim_audio_track(unsigned long, unsigned long, float);
                          shim_audio_track(a0, a1, 0.0f); }
    /*
     * The sound census (audio.c): '<' is 0x8a2eac, a sound object about to
     * play; '|' is 0x8a30e0, a sound object stopping the source it last used.
     * x0 is the object in both. Placed only while a report is open.
     */
    if (t->mark == '<') { void shim_audio_obj_plays(unsigned long); shim_audio_obj_plays(a0); }
    if (t->mark == '|') { void shim_audio_obj_stops(unsigned long); shim_audio_obj_stops(a0); }
#if ISAAC_DIAG
    /*
     * '7': a writer of the persistent data at its door - who unlocked what, and
     * whether a marked run leaks (spawner.c). The letter after the mark says
     * which writer; the caller is one frame up, as for '@'.
     */
    if (t->mark == '7') { void shim_unlocks_seen(int, unsigned long, unsigned long, long, unsigned long);
                          shim_unlocks_seen(t->name[1], a0, a1, (long) (int) a2,
                                            (unsigned long) __builtin_return_address(1)); }
#endif
    if (t->args && t->hits < 4) {
        unsigned long a[4] = { a0, a1, a2, a3 };
        int k;
        for (k = 0; k < 4; k++) {
            tell(t->name, k, a[k]);
            /* the game's arguments often point at records: show their start too */
            const unsigned long *w = (const unsigned long *) (uintptr_t) a[k];
            if (readable(w, 32)) {
                LOG("TRACE:     at it: %lx %lx %lx %lx", w[0], w[1], w[2], w[3]);
                if (readable((const void *) (uintptr_t) w[0], 16))
                    LOG("TRACE:     and at its first field: \"%.60s\"", (const char *) (uintptr_t) w[0]);
            }
        }
    }
    t->hits++;
    return t->cont;
}

long shim_trace_hits(long idx) { return (idx >= 0 && idx < g_ntr) ? g_tr[idx].hits : -1; }

void shim_trace_report(void) {
    for (int i = 0; i < g_ntr; i++)
        LOG("TRACE: %-28s calls %ld", g_tr[i].name, g_tr[i].hits);
}

/* assembly wrappers: one per site, the index is baked into the code */
extern void shim_trace_entry(void);

/*
 * Installs a hook. Returns the slot index or -1.
 *
 * The adrp check is mandatory: a copied adrp would compute its page from the
 * new location and send the game into foreign memory. Better to refuse than to
 * break it silently.
 */
/*
 * A refusal used to be silent in the build the testers run, and a hook that
 * was never placed looked exactly like a hook that never fired. Now it says so
 * in the report.
 */
/* B.cond, CBZ/CBNZ, TBZ/TBNZ: the branches that fall through when not taken */
static int cond_branch(uint32_t w) {
    return (w & 0xFE000000u) == 0x54000000u                /* B.cond         */
        || (w & 0x7E000000u) == 0x34000000u                /* CBZ, CBNZ      */
        || (w & 0x7E000000u) == 0x36000000u;               /* TBZ, TBNZ      */
}

/* where a conditional branch at this address goes */
static uintptr_t cond_target(const uint32_t *at) {
    uint32_t w = *at;
    int32_t words;
    if ((w & 0x7E000000u) == 0x36000000u) {                /* imm14 */
        words = (int32_t) ((w >> 5) & 0x3FFFu);
        if (words & (1 << 13)) words -= (1 << 14);
    } else {                                               /* imm19 */
        words = (int32_t) ((w >> 5) & 0x7FFFFu);
        if (words & (1 << 18)) words -= (1 << 19);
    }
    return (uintptr_t) at + (intptr_t) words * 4;
}

/* the same branch, pointed `words` instructions from where it will stand */
static uint32_t cond_retarget(uint32_t w, int32_t words) {
    if ((w & 0x7E000000u) == 0x36000000u)
        return (w & ~(0x3FFFu << 5)) | (((uint32_t) words & 0x3FFFu) << 5);
    return (w & ~(0x7FFFFu << 5)) | (((uint32_t) words & 0x7FFFFu) << 5);
}

static int refused(const char *name, const char *why) {
    LOG("TRACE: cannot hook %s - %s", name, why);
    int shim_report_active(void);
    void report_line(const char *kind, const char *text);
    if (shim_report_active()) {
        char line[160];
        snprintf(line, sizeof(line), "cannot hook %s - %s", name, why);
        report_line("trace", line);
    }
    return -1;
}

int shim_trace_add(unsigned long off, const char *name) {
    if (g_ntr >= TRACE_MAX) return refused(name, "the table is full");
    if (!g_arena) {
        g_arena = mmap(NULL, TRACE_MAX * SLOT, PROT_READ | PROT_WRITE,
                       MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        if (g_arena == MAP_FAILED) { g_arena = NULL; return refused(name, "no room for the arena"); }
    }
    uint32_t *orig = (uint32_t *) (shim_image_base() + off);
    /*
     * The insert takes five words: three instructions and an eight-byte
     * address. So five must be copied as well, otherwise the continuation
     * lands in the middle of the address — exactly the mistake that produced
     * SIGILL on the first attempt.
     *
     * Branches at the start cannot be copied: they compute the target from
     * their own address. We refuse honestly instead of breaking silently.
     *
     * ONE CONDITIONAL BRANCH is the exception (01.10.2026): a check of the
     * arguments straight after the prologue, `cmp w1, #n; b.hi out`, as in
     * the collection writer 0x7025a8 and the challenge writer 0x702c10. It is
     * copied pointing at a far jump kept in the slot (slot+40: ldr x16; br
     * x16; the original target), so taken it goes where it always went and
     * not taken it falls through to the next copied word, as before. B, BL,
     * a second conditional branch and literal loads are still refused.
     */
    int conds = 0;
    for (int i = 0; i < 5; i++) {
        uint32_t w = orig[i];
        int branch = (w & 0x7C000000u) == 0x14000000u        /* B, BL          */
                  || (w & 0x3B000000u) == 0x18000000u;       /* LDR (literal)  */
        if (branch) return refused(name, "a branch at the start");
        if (cond_branch(w) && ++conds > 1) return refused(name, "two conditional branches at the start");
    }

    int idx = g_ntr++;
    uint8_t *slot = g_arena + (size_t) idx * SLOT;
    uint32_t *c = (uint32_t *) slot;

    /* the far jump a copied conditional branch is pointed at, and its target */
    uint8_t *lits = slot + 40;
    for (int i = 0; i < 5; i++) {
        uint32_t w = orig[i];
        if (!cond_branch(w)) continue;
        uint32_t *far = (uint32_t *) lits;
        far[0] = 0x58000050u;                              /* ldr x16, [pc, #8] */
        far[1] = 0xD61F0200u;                              /* br  x16           */
        *(uint64_t *) (lits + 8) = (uint64_t) (uintptr_t) cond_target(&orig[i]);
        lits += 16;
    }

    /*
     * adrp computes its page from its own address, so it would answer
     * differently in the new place. We replace it with a load of a ready
     * address from a literal: the value is computed from the ORIGINAL address,
     * and the meaning is preserved.
     */
    int nlit = 0;
    for (int i = 0; i < 5; i++) {
        uint32_t w = orig[i];
        if (cond_branch(w)) { c[i] = cond_retarget(w, (int32_t) ((slot + 40) - (uint8_t *) &c[i]) / 4); continue; }
        if ((w & 0x9F000000u) != 0x90000000u) { c[i] = w; continue; }
        uint32_t rd = w & 31;
        int32_t imm = (int32_t) ((((w >> 5) & 0x7FFFFu) << 2) | ((w >> 29) & 3));
        if (imm & (1 << 20)) imm -= (1 << 21);
        uint64_t page = (((uint64_t) (uintptr_t) &orig[i]) & ~0xFFFULL) + ((int64_t) imm << 12);
        uint8_t *lit = lits + nlit * 8;
        if (lit + 8 > slot + SLOT) return refused(name, "not enough room for addresses");
        *(uint64_t *) lit = page;
        nlit++;
        int32_t delta = (int32_t) (lit - (uint8_t *) &c[i]) / 4;
        c[i] = 0x58000000u | ((uint32_t) (delta & 0x7FFFF) << 5) | rd;   /* ldr xD, literal */
    }

    c[5] = 0x58000050u;                                    /* ldr x16, [pc, #8] */
    c[6] = 0xD61F0200u;                                    /* br  x16           */
    *(uint64_t *) (slot + 28) = (uint64_t) (uintptr_t) (orig + 5);

    g_tr[idx].args = (name[0] == '!');
    g_tr[idx].name = name + g_tr[idx].args;
    /*
     * THE MARK: the first letter of the name, when it is one of these, picks
     * the handler in shim_trace_hit above. Several hooks share a mark on
     * purpose - eight of them answer to '>' - so the mark is not anybody's
     * property; what matters is that it is NOT AN ACCIDENT.
     *
     * It was one. A hook called plainly "Repentance stick: capture" begins with
     * the letter the Controls screen's draw had just taken, and the stick's
     * capture was handed to the screen's handler: the game fell over on the
     * first stick pressed (12.09.2026). Two more names were sitting on marks
     * the same way, "Afterbirth stick: capture" on 'A' and "XML loader" on 'X'.
     * All of them are renamed so their first letter is nobody's mark, and
     * shim_trace_marks() below says out loud, in a report build, which hooks
     * carry which mark - so the next accident is one line away from sight.
     */
    if (name[0] && strchr(MARKS, name[0])) {
        g_tr[idx].mark = name[0];
        g_tr[idx].name = name;                  /* the mark shows in the report */
    }
    g_tr[idx].cont = slot;
    g_tr[idx].hits = 0;

    /* over the original's start: index in x17, jump to the shared handler */
    orig[0] = 0xD2800011u | ((uint32_t) idx << 5);         /* movz x17, #idx    */
    orig[1] = 0x58000050u;                                 /* ldr  x16, [pc, #8]*/
    orig[2] = 0xD61F0200u;                                 /* br   x16          */
    *(uint64_t *) (orig + 3) = (uint64_t) (uintptr_t) shim_trace_entry;
    return idx;
}

/*
 * Every mark, and the hooks that carry it, into the report. A mark with more
 * than one hook is normal; a mark with a hook whose name does not read as
 * "<mark>what it is" is the accident described above.
 */
void shim_trace_marks(void (*say)(const char *, const char *)) {
    if (!say) return;
    for (const char *m = MARKS; *m; m++) {
        char line[200];
        int k = snprintf(line, sizeof(line), "'%c':", *m);
        int found = 0;
        for (int i = 0; i < g_ntr && k > 0 && (size_t) k < sizeof(line); i++) {
            if (g_tr[i].mark != *m) continue;
            found++;
            k += snprintf(line + k, sizeof(line) - (size_t) k, " \"%s\"", g_tr[i].name);
        }
        if (found > 1) say("mark", line);
    }
    /*
     * AND A HOOK THAT ASKED FOR A MARK NOBODY ANSWERS. The mark is the first
     * letter, and a letter missing from MARKS gets no handler at all: the hook
     * is placed, the report shows its name, and the work it was placed for
     * silently never happens. That is exactly what "Rthe effects are wiped"
     * did, after R was moved off the list (13.09.2026).
     */
    for (int i = 0; i < g_ntr; i++) {
        const char *n = g_tr[i].name;
        if (!n || g_tr[i].mark || !(n[0] >= 'A' && n[0] <= 'Z')) continue;
        if (n[1] >= 'a' && n[1] <= 'z' && n[2] >= 'a' && n[2] <= 'z') {
            char line[160];
            snprintf(line, sizeof(line), "\"%s\" begins with '%c', which no handler answers", n, n[0]);
            say("mark", line);
        }
    }
}

void shim_trace_seal(void) {
    if (g_arena) mprotect(g_arena, TRACE_MAX * SLOT, PROT_READ | PROT_EXEC);
}
