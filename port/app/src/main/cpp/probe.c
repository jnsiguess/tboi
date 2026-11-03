/*
 * Memory inspector: finding the game's fields inside the live process.
 *
 * A measurement showed there is nothing to rebuild a Lua binding from — the
 * image holds neither class names nor method names. So fields have to be
 * hunted exactly the way a debugger hunts them on a PC: take a number the game
 * shows on screen, find every occurrence of it in memory, change it in the
 * game and keep the addresses where it changed too. Same thing here, only from
 * inside the process.
 *
 * Lives only in the diagnostic build: with ISAAC_DIAG off nothing of this file
 * remains.
 *
 * Commands go into probe.txt next to port-touch.ini, one per line. The file is
 * re-read whenever its size or modification time changes.
 *
 *   scan f 3.5        find float 3.5 across the whole heap
 *   next f 4.0        keep only the addresses that now hold 4.0
 *   dump ADDR LEN     show memory: as words, as numbers and as floats
 *   base              remind me of the image base and the root object
 *   near ADDR         whose object is this: look for pointers to it
 *   snap ADDR LEN     take a snapshot of a region (length: 0x… or decimal)
 *   cmp               print the words that changed since the snapshot, refresh it
 *   audio             audio state: stream, pools, every live source
 *
 * Answers go to the log (`adb logcat -s isaac:*`).
 */
#include "diag.h"

#if ISAAC_DIAG

#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <android/log.h>

#define LOG(...) __android_log_print(ANDROID_LOG_INFO, "isaac", __VA_ARGS__)

unsigned long shim_image_base(void);

#define MAX_HITS 400000
/*
 * A process has thousands of regions: the measurement gave 6589 in total and
 * 2384 writable ones, 3.6 GB together. The first version took 512 and covered
 * a fifth of memory — and quietly returned an incomplete answer, which is
 * worse than no answer at all.
 */
#define MAX_REGIONS 4096

static char g_path[512];
static long g_size = -1;
static long g_mtime = -1;

static unsigned long *g_hits;
static long g_hit_count;

typedef struct {
    unsigned long from, to;
} region_t;

/* Readable and writable regions of our memory: the heap and whatever the game made. */
static int regions(region_t *list, int max) {
    FILE *f = fopen("/proc/self/maps", "r");
    if (!f) return 0;
    char line[512];
    int n = 0;
    while (n < max && fgets(line, sizeof(line), f)) {
        unsigned long from, to;
        char rights[8];
        if (sscanf(line, "%lx-%lx %7s", &from, &to, rights) != 3) continue;
        if (rights[0] != 'r' || rights[1] != 'w') continue;     /* read-write only */
        if (strstr(line, "[stack]") || strstr(line, "[vvar]")) continue;
        if (to - from > (256UL << 20)) continue;                /* certainly not ours */
        list[n].from = from;
        list[n].to = to;
        n++;
    }
    fclose(f);
    return n;
}

static int close_enough(float a, float b) {
    float d = a - b;
    if (d < 0) d = -d;
    return d < 0.0005f;
}

static region_t g_regions[MAX_REGIONS];      /* 64 KB: too much for the stack */

static void search(float target) {
    region_t *list = g_regions;
    int n = regions(list, MAX_REGIONS);
    if (!g_hits) g_hits = (unsigned long *) malloc(sizeof(unsigned long) * MAX_HITS);
    if (!g_hits) { LOG("probe: not enough memory for the list"); return; }
    g_hit_count = 0;
    unsigned long bytes = 0;
    for (int i = 0; i < n && g_hit_count < MAX_HITS; i++) {
        bytes += list[i].to - list[i].from;
        for (unsigned long a = list[i].from; a + 4 <= list[i].to; a += 4) {
            float v = *(const float *) a;
            if (close_enough(v, target)) {
                g_hits[g_hit_count++] = a;
                if (g_hit_count >= MAX_HITS) break;
            }
        }
    }
    LOG("probe: searched %.4f across %d regions (%.1f MB) — found %ld%s",
        (double) target, n, bytes / 1048576.0, g_hit_count,
        n >= MAX_REGIONS ? "  WARNING: the region list hit its limit" : "");
    for (long i = 0; i < g_hit_count && i < 8; i++)
        LOG("   %ld: 0x%lx", i, g_hits[i]);
}

static void narrow(float target) {
    if (!g_hits || g_hit_count <= 0) { LOG("probe: run scan first"); return; }
    long was = g_hit_count, kept = 0;
    for (long i = 0; i < was; i++) {
        float v = *(const float *) g_hits[i];
        if (close_enough(v, target)) g_hits[kept++] = g_hits[i];
    }
    g_hit_count = kept;
    LOG("probe: narrowed by %.4f — was %ld, now %ld", (double) target, was, g_hit_count);
    for (long i = 0; i < g_hit_count && i < 16; i++) {
        unsigned long base = shim_image_base();
        unsigned char *root = base ? *(unsigned char **) (base + 0xac2428) : NULL;
        if (root && g_hits[i] > (unsigned long) root &&
            g_hits[i] - (unsigned long) root < 0x200000)
            LOG("   %ld: 0x%lx  = root + 0x%lx", i, g_hits[i],
                g_hits[i] - (unsigned long) root);
        else
            LOG("   %ld: 0x%lx", i, g_hits[i]);
    }
}

/*
 * Keep the hits that have another wanted number lying next to them. That finds
 * a whole structure rather than a field: a player's damage, fire delay and
 * range sit close together, and a triple lands where a single one lies.
 */
static void with_neighbour(float neighbour, long radius) {
    if (!g_hits || g_hit_count <= 0) { LOG("probe: run scan first"); return; }
    if (radius <= 0 || radius > 65536) radius = 512;
    long was = g_hit_count, kept = 0;
    for (long i = 0; i < was; i++) {
        unsigned long a = g_hits[i];
        int found = 0;
        for (long o = -radius; o <= radius && !found; o += 4) {
            unsigned long t = a + o;
            if (t < 0x1000) continue;
            if (close_enough(*(const float *) t, neighbour)) found = 1;
        }
        if (found) g_hits[kept++] = a;
    }
    g_hit_count = kept;
    LOG("probe: with neighbour %.4f within ±%ld — was %ld, now %ld",
        (double) neighbour, radius, was, g_hit_count);
    unsigned long base = shim_image_base();
    unsigned char *root = base ? *(unsigned char **) (base + 0xac2428) : NULL;
    for (long i = 0; i < g_hit_count && i < 20; i++) {
        if (root && g_hits[i] > (unsigned long) root &&
            g_hits[i] - (unsigned long) root < 0x200000)
            LOG("   %ld: 0x%lx = root + 0x%lx", i, g_hits[i],
                g_hits[i] - (unsigned long) root);
        else
            LOG("   %ld: 0x%lx", i, g_hits[i]);
    }
}

/*
 * Can we read this address at all? Without the check the inspector takes the
 * game down: addresses from a previous run are not mapped in the new one,
 * while the command file stays on disk and is read on the very first frame.
 * That is exactly how the game crashed once.
 */
static int accessible(unsigned long addr, long len) {
    /*
     * We read the map ourselves rather than through the shared enumeration:
     * that one filters out regions larger than 256 MB (the search needs that
     * to save time), and Android's heap is exactly such a region — a live
     * object was being declared unreachable. Here only one thing matters: is
     * the region mapped and readable.
     *
     * We also strip the pointer tag. On Android the top byte of a heap address
     * is a tag (0xb4… under scudo); the CPU ignores it, and /proc/self/maps
     * lists regions without it. A numeric comparison with the tag on never
     * matched, and a live object was declared unreachable.
     */
    addr &= 0x00FFFFFFFFFFFFFFUL;
    FILE *f = fopen("/proc/self/maps", "r");
    if (!f) return 0;
    char line[512];
    int ok = 0;
    while (!ok && fgets(line, sizeof(line), f)) {
        unsigned long from, to;
        char rights[8];
        if (sscanf(line, "%lx-%lx %7s", &from, &to, rights) != 3) continue;
        if (rights[0] != 'r') continue;
        if (addr >= from && addr + (unsigned long) len <= to) ok = 1;
    }
    fclose(f);
    return ok;
}

static void show(unsigned long addr, long len) {
    if (len <= 0 || len > 512) len = 64;
    if (!accessible(addr, len)) {
        LOG("probe: address 0x%lx (%ld bytes) is not mapped — cannot read it. "
            "Looks like it is left over from a previous run: every run has its own root.",
            addr, len);
        return;
    }
    for (long o = 0; o + 16 <= len; o += 16) {
        const unsigned char *p = (const unsigned char *) (addr + o);
        LOG("  +0x%03lx  %08x %08x %08x %08x   f: %.3f %.3f %.3f %.3f",
            o, *(const unsigned *) p, *(const unsigned *) (p + 4),
            *(const unsigned *) (p + 8), *(const unsigned *) (p + 12),
            (double) *(const float *) p, (double) *(const float *) (p + 4),
            (double) *(const float *) (p + 8), (double) *(const float *) (p + 12));
    }
}

/* Who holds a pointer to this object — that is how its owner is found. */
static void near_pointers(unsigned long target) {
    /* the target itself can be anything: we search THROUGH memory, not read at it */
    region_t *list = g_regions;
    int n = regions(list, MAX_REGIONS);
    int found = 0;
    for (int i = 0; i < n && found < 12; i++) {
        for (unsigned long a = list[i].from; a + 8 <= list[i].to; a += 8) {
            if (*(const unsigned long *) a == target) {
                LOG("probe: a pointer to 0x%lx sits at 0x%lx", target, a);
                if (++found >= 12) break;
            }
        }
    }
    if (!found) LOG("probe: no pointers to 0x%lx were found", target);
}

static void root_and_base(void) {
    unsigned long base = shim_image_base();
    unsigned char *root = base ? *(unsigned char **) (base + 0xac2428) : NULL;
    LOG("probe: image base 0x%lx, root 0x%lx, state %d",
        base, (unsigned long) root,
        root ? *(const int *) (root + 0x204c0) : -1);
}

/*
 * A snapshot of a region and a printout of what changed in it. That is how the
 * field that switches screens is found: snapshot on the main menu, "cmp" on
 * the same menu — that is the noise (frame counters, timers), then "cmp" after
 * the transition. The field we want is the one that changed in the second
 * comparison and stayed still in the first. Every "cmp" refreshes the snapshot
 * so the next one measures from the current point.
 */
static unsigned char *g_snapshot;
static unsigned long g_snapshot_addr;
static long g_snapshot_len;

static void take_snapshot(unsigned long addr, long len) {
    if (len <= 0 || !accessible(addr, len)) {
        LOG("probe: snapshot 0x%lx (%ld bytes): region is not mapped", addr, len);
        return;
    }
    free(g_snapshot);
    g_snapshot = malloc((size_t) len);
    if (!g_snapshot) { LOG("probe: snapshot: out of memory"); return; }
    memcpy(g_snapshot, (const void *) addr, (size_t) len);
    g_snapshot_addr = addr;
    g_snapshot_len = len;
    LOG("probe: snapshot 0x%lx +%ld taken", addr, len);
}

static void compare(void) {
    if (!g_snapshot) { LOG("probe: no snapshot"); return; }
    if (!accessible(g_snapshot_addr, g_snapshot_len)) {
        LOG("probe: the snapshot's region is no longer mapped");
        return;
    }
    const unsigned char *now = (const unsigned char *) g_snapshot_addr;
    long changed = 0;
    for (long i = 0; i + 4 <= g_snapshot_len; i += 4) {
        unsigned was = *(const unsigned *) (g_snapshot + i);
        unsigned is = *(const unsigned *) (now + i);
        if (was == is) continue;
        if (changed < 600) LOG("probe: +0x%lx: %08x -> %08x", i, was, is);
        changed++;
    }
    LOG("probe: words changed %ld (at most 600 shown)", changed);
    memcpy(g_snapshot, now, (size_t) g_snapshot_len);
}

static void execute(char *line) {
    while (*line == ' ') line++;
    char *end = line + strlen(line);
    while (end > line && (end[-1] == '\n' || end[-1] == '\r' || end[-1] == ' '))
        *--end = 0;
    if (!*line || *line == '#') return;
    LOG("probe: command \"%s\"", line);
    if (!strncmp(line, "scan f ", 7)) search((float) atof(line + 7));
    else if (!strncmp(line, "next f ", 7)) narrow((float) atof(line + 7));
    else if (!strncmp(line, "withf ", 6)) {
        float v = 0; long r = 512;
        sscanf(line + 6, "%f %ld", &v, &r);
        with_neighbour(v, r);
    }
    else if (!strncmp(line, "dump ", 5)) {
        unsigned long a = 0; long n = 64;
        sscanf(line + 5, "%lx %ld", &a, &n);
        if (a) show(a, n); else LOG("probe: could not parse the address");
    } else if (!strncmp(line, "near ", 5)) {
        unsigned long a = 0;
        sscanf(line + 5, "%lx", &a);
        if (a) near_pointers(a);
    } else if (!strncmp(line, "snap ", 5)) {
        unsigned long a = 0; long n = 0;
        sscanf(line + 5, "%lx %li", &a, &n);
        if (a) take_snapshot(a, n); else LOG("probe: could not parse the address");
    } else if (!strcmp(line, "cmp")) {
        compare();
    } else if (!strcmp(line, "audio")) {
        void shim_audio_report(void);
        shim_audio_report();
    } else if (!strcmp(line, "base")) root_and_base();
    else LOG("probe: no such command");
}

/*
 * Who called us: the whole chain, not just the caller.
 *
 * shim_fopen already prints one return address, and for a call that arrives
 * through a table of virtual functions that address is a shared file helper -
 * the same one for every file in the game. What matters is who is above it.
 *
 * The chain is walkable because arm64 keeps a frame pointer: x29 points at a
 * pair {previous x29, return address}. Apple's ABI requires it, so every frame
 * of the image is there. Nothing is dereferenced without checking first: the
 * frames must climb, stay aligned, and stay inside this stack - a wrong guess
 * here costs a crash instead of an answer.
 *
 * Addresses inside the image are printed as offsets, ready for tools/dis.py;
 * everything else (our own layer, libc) is printed as a dot.
 */
void shim_probe_who(const char *tag) {
    unsigned long base = shim_image_base();
    unsigned long *fp = (unsigned long *) __builtin_frame_address(0);
    unsigned long floor = (unsigned long) fp;
    char line[512];
    int at = 0;
    for (int level = 0; level < 16; level++) {
        unsigned long here = (unsigned long) fp;
        if (here & 15) break;                                  /* not a frame */
        if (here < floor || here - floor > (1UL << 20)) break; /* not this stack */
        unsigned long back = fp[1];
        if (base && back > base && back - base < 0xc00000UL)
            at += snprintf(line + at, sizeof(line) - (size_t) at, " %lx", back - base);
        else
            at += snprintf(line + at, sizeof(line) - (size_t) at, " .");
        if (at > (int) sizeof(line) - 16) break;
        unsigned long next = fp[0];
        if (next <= here) break;
        fp = (unsigned long *) next;
    }
    LOG("who %s:%s", tag, line);
}

void shim_probe_setup(const char *dir) {
    if (!dir || !*dir) return;
    snprintf(g_path, sizeof(g_path), "%s/probe.txt", dir);
    LOG("probe: waiting for commands in %s", g_path);
}

void shim_probe_tick(void) {
    static int tick;
    if (!g_path[0] || (++tick % 30)) return;         /* twice a second is plenty */
    struct stat st;
    if (stat(g_path, &st) != 0) return;
    if ((long) st.st_size == g_size && (long) st.st_mtime == g_mtime) return;
    g_size = (long) st.st_size;
    g_mtime = (long) st.st_mtime;
    FILE *f = fopen(g_path, "r");
    if (!f) return;
    char line[256];
    while (fgets(line, sizeof(line), f)) execute(line);
    fclose(f);
}

#else
void shim_probe_setup(const char *dir) { (void) dir; }
void shim_probe_tick(void) { }
void shim_probe_who(const char *tag) { (void) tag; }
#endif
