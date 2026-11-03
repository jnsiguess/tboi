/*
 * Desk test of the co-op pocket words (hotfix 1.72, perf item 5): hudlayout.c's
 * PA_WORDS table and pa_words, cut out as they are by run.py (pocket_cut.h),
 * over a fake image. A frame's pocket calls in random modes are run the 1.72
 * way (set the mode, call, put the game's words back - every call) and the new
 * way (switch only when the mode differs, the game's words back once at the
 * frame's end); every call must see the same words both ways, the game must
 * find its own words after the frame, and the switches are counted.
 */
#include <stdio.h>
#include <stdint.h>
#include <string.h>

#define LOG(...) do { } while (0)
#define _SC_PAGESIZE 30
#define PROT_READ 1
#define PROT_WRITE 2
#define PROT_EXEC 4
static long sysconf(int k) { (void) k; return 4096; }
static long g_mprotects, g_flushes;
static int fake_mprotect(void *a, size_t n, int p) { (void) a; (void) n; (void) p; g_mprotects++; return 0; }
#define mprotect fake_mprotect
#define __builtin___clear_cache(a, b) (g_flushes++)
#include "pocket_cut.h"

static int g_fail;
#define CHECK(c, ...) do { if (!(c)) { g_fail++; printf("  FAIL line %d: ", __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

static uint32_t image[0x1000];               /* stands for 0x6e9000..0x6ea000 */
#define BASE ((unsigned long long) (uintptr_t) image - 0x6e9000ULL)

static void words_now(uint32_t *out) {
    for (int i = 0; i < PA_N; i++) out[i] = *(const uint32_t *) (BASE + PA_WORDS[i].at);
}

static unsigned rs = 172;
static unsigned rnd(void) { rs ^= rs << 13; rs ^= rs >> 17; rs ^= rs << 5; return rs; }

int main(void) {
    for (int i = 0; i < PA_N; i++) *(uint32_t *) (BASE + PA_WORDS[i].at) = PA_WORDS[i].v[PA_GAME];
    long old_mp = 0, new_mp = 0, frames = 2000, calls = 0;
    uint32_t seen_old[16][8], seen_new[16][8];
    for (long f = 0; f < frames; f++) {
        int n = 1 + (int) (rnd() % 4), modes[16];
        /* a frame: up to four players' pockets; most frames hold the same pocket as the last */
        for (int k = 0; k < n; k++) modes[k] = rnd() % 5 == 0 ? (int) (rnd() % 4) : (k % 2 ? PA_ACTIVE : PA_BAG);
        /* the 1.72 way */
        long m0 = g_mprotects;
        for (int k = 0; k < n; k++) {
            int pa = modes[k];
            if (pa) pa_words(BASE, pa);
            words_now(seen_old[k]);
            if (pa) pa_words(BASE, PA_GAME);
        }
        old_mp += g_mprotects - m0;
        uint32_t after_old[8];
        words_now(after_old);
        /* the new way */
        m0 = g_mprotects;
        for (int k = 0; k < n; k++) {
            pa_words(BASE, modes[k]);
            words_now(seen_new[k]);
        }
        pa_words(BASE, PA_GAME);
        new_mp += g_mprotects - m0;
        uint32_t after_new[8];
        words_now(after_new);
        for (int k = 0; k < n; k++)
            CHECK(!memcmp(seen_old[k], seen_new[k], sizeof(uint32_t) * PA_N), "frame %ld call %d: other words", f, k);
        CHECK(!memcmp(after_old, after_new, sizeof(uint32_t) * PA_N), "frame %ld: other words after it", f);
        for (int i = 0; i < PA_N; i++)
            CHECK(after_new[i] == PA_WORDS[i].v[PA_GAME], "frame %ld: the game's word %d not back", f, i);
        calls += n;
    }
    printf("%ld frames, %ld pocket calls: the same words on every call and after every frame; "
           "page openings %ld the 1.72 way, %ld now (%.2f a frame instead of %.2f)\n",
           frames, calls, old_mp / 2, new_mp / 2, new_mp / 2.0 / frames, old_mp / 2.0 / frames);
    printf(g_fail ? "FAILED: %d\n" : "all passed\n", g_fail);
    return g_fail ? 1 : 0;
}
