/*
 * Desk test of readable() (hotfix 1.72, perf item 5): trace.c's page table
 * and probe, cut out as they are by run.py (trace_cut.h), over a fake pipe
 * whose write answers EFAULT for "unmapped" pages and EAGAIN when 64 KB are
 * in it, as the kernel's does. The monotonic clock is moved by hand.
 */
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <errno.h>

#define LOG(...) do { } while (0)
#define _SC_PAGESIZE 30
#define F_SETFL 4
#define O_NONBLOCK 04000

static long sysconf(int k) { (void) k; return 4096; }

static uintptr_t g_bad_lo, g_bad_hi;           /* the "unmapped" range */
static long g_in_pipe, g_writes, g_reads, g_drains;
static int fake_pipe(int fd[2]) { fd[0] = 7; fd[1] = 8; return 0; }
static int fake_fcntl(int fd, int cmd, int v) { (void) fd; (void) cmd; (void) v; return 0; }
static long long fake_write(int fd, const void *p, size_t n) {
    (void) fd; (void) n;
    g_writes++;
    if (g_in_pipe >= 65536) { errno = EAGAIN; return -1; }
    uintptr_t a = (uintptr_t) p;
    if (a >= g_bad_lo && a < g_bad_hi) { errno = EFAULT; return -1; }
    g_in_pipe++;
    return 1;
}
static long long fake_read(int fd, void *p, size_t n) {
    (void) fd; (void) p;
    g_reads++;
    if (!g_in_pipe) { errno = EAGAIN; return -1; }
    long k = (long) n < g_in_pipe ? (long) n : g_in_pipe;
    g_in_pipe -= k;
    if (n > 1) g_drains++;
    return k;
}
#define pipe fake_pipe
#define fcntl fake_fcntl
#define write fake_write
#define read fake_read
#include "trace_cut.h"

static int g_fail;
#define CHECK(c, ...) do { if (!(c)) { g_fail++; printf("  FAIL line %d: ", __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

int main(void) {
    const uintptr_t base = 0x7000000000ull;
    g_bad_lo = base + 0x100000; g_bad_hi = base + 0x200000;
    CHECK(readable((void *) (base + 0x10), 64), "a mapped page");
    CHECK(!readable((void *) (g_bad_lo + 8), 4), "an unmapped page");
    CHECK(!readable((void *) (g_bad_lo - 16), 64), "a range running into an unmapped page");
    long w = g_writes, r = g_reads;
    for (int i = 0; i < 1000; i++) readable((void *) (base + 0x10), 64);
    CHECK(g_writes == w && g_reads == r, "remembered: no probe for 1000 asks");
    /* one system call a probe, not two */
    w = g_writes; r = g_reads;
    for (int i = 0; i < 200; i++) readable((void *) (base + 0x400000 + (uintptr_t) i * 4096), 8);
    CHECK(g_writes - w == 200 && g_reads == r, "200 new pages: %ld writes, %ld reads", g_writes - w, g_reads - r);
    /* the pipe fills: drained once, the answer still right */
    g_in_pipe = 65536;
    CHECK(readable((void *) (base + 0x900000), 8), "a full pipe is drained and the page probed");
    CHECK(g_drains >= 1 && g_in_pipe == 1, "drained (%ld) and one byte in it (%ld)", g_drains, g_in_pipe);
    /* each page is believed for 500 ms from its own probe - no wholesale wipe */
    g_desk_skew_ns += 300000000LL;
    w = g_writes;
    readable((void *) (base + 0xa00000), 8);                     /* a page first asked 300 ms later */
    g_desk_skew_ns += 250000000LL;                               /* the first pages 550 ms old, this one 250 */
    long before = g_writes;
    readable((void *) (base + 0x10), 8);
    CHECK(g_writes == before + 1, "an old page is asked again");
    before = g_writes;
    readable((void *) (base + 0xa00000), 8);
    CHECK(g_writes == before, "a young page is not");
    /* a page that went away is noticed within HOLD_MS */
    g_bad_lo = base; g_bad_hi = base + 0x1000;
    CHECK(readable((void *) (base + 0x10), 8), "within 500 ms the page is still believed");
    g_desk_skew_ns += 600000000LL;
    CHECK(!readable((void *) (base + 0x10), 8), "after 500 ms it is asked and found gone");
    printf(g_fail ? "FAILED: %d\n" : "all passed\n", g_fail);
    return g_fail ? 1 : 0;
}
