/* cut out of trace.c by run.py - do not edit */
#define PAGES 8192                  /* addressed by the page number; two per entity heap */
#define PAGE_MASK (PAGES - 1)
#define HOLD_MS 500                 /* how long long a page may be believed */
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
long long g_readable_probes, g_readable_asks;

void shim_trace_probe_counts(long long *probes, long long *asks) {
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

static long long page_size(void) {
    static long long ps;
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
int shim_trace_readable(const void *p, unsigned long long n);

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

static int readable(const void *p, unsigned long long n) {
    static int fd[2] = { -1, -1 }, nonblocking;
    if (!p || (unsigned long long) (uintptr_t) p < 0x1000) return 0;
    if (!n) n = 1;
    uintptr_t lo = (uintptr_t) p, hi = lo + n - 1;
    if (hi < lo) return 0;

    long long ps = page_size();
    uintptr_t mask = (uintptr_t) ps - 1;

    /* nothing is believed for long long: each page for HOLD_MS from its own answer */
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
