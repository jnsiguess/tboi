/*
 * A Mach-O image loader for Android.
 *
 * It does exactly what tools/simload.py proved in Python: reserves memory, lays
 * the segments out at their addresses, walks the chained fixups — shifting its
 * own pointers by the base and substituting foreign ones from a table — and
 * finally reads the list of Objective-C classes as a check that everything
 * landed correctly.
 *
 * What is NOT done here: the code is not executed yet. Reconnaissance answers
 * two questions — does the image lay out, and how many foreign names Android
 * supplies by itself.
 */
#include <jni.h>
#include <android/asset_manager_jni.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <signal.h>
#include <ucontext.h>
#include <string.h>
#include <dlfcn.h>
#include <pthread.h>
#include <sys/mman.h>
#include <android/log.h>
#include "diag.h"
#include <android/native_window_jni.h>
#include <link.h>

#define TAG "isaac"
#if ISAAC_DIAG
#define LOG(...) __android_log_print(ANDROID_LOG_INFO, TAG, __VA_ARGS__)
#else
#define LOG(...) do { } while (0)      /* the private build has no log */
#endif
/*
 * Fatal failures are written both to the log and to a file next to the saves.
 *
 * The reason: complaints of "will not start, closes right on the tap" from
 * other people's phones and not a single line of log — there is no way to pull
 * one off somebody else's device. The file `Downloads/isaac-start.txt` is
 * visible to any file manager and can simply be sent over. Java opens it and
 * hands the descriptor down; see shim_set_report_fd below.
 */
void report_line(const char *kind, const char *text);
#define ERR(...) do { \
    char fail_line[512]; \
    snprintf(fail_line, sizeof(fail_line), __VA_ARGS__); \
    __android_log_print(ANDROID_LOG_ERROR, TAG, "%s", fail_line); \
    report_line("FAILURE", fail_line); \
} while (0)

/* ---------- the startup report ---------- */

void report_line(const char *kind, const char *text);

static int g_report_fd = -1;

/*
 * A descriptor, not a path, and that is the whole point.
 *
 * The report has to be readable by a person on somebody else's phone, so Java
 * puts it in Downloads through MediaStore — and a MediaStore file cannot be
 * opened by path from here at all. So Java opens it once and hands the
 * descriptor down. Both sides then write into the same open file: the stages
 * from Java, the failures and the crash line from here, in the order they
 * actually happen.
 *
 * It also means one launch is enough. While the report was a file of ours in a
 * folder no file manager shows, handing it over took two launches: one to
 * crash, another to copy the previous report somewhere visible.
 */
/*
 * WHICH BUILD IS THIS. Written into the report, and into every crash.
 *
 * A tester's crash was chased twice for the same fault, and the second time
 * the fix was already in the shipped file: the offset resolved to the old
 * layout, the register showed the old mask, and the only way to tell whether
 * he had the new APK at all was to disassemble what we happened to have on
 * disk. That is not an answer a report should need.
 *
 * The linker gives every build of the library a note of its own - a hash over
 * the finished file - so it changes whenever anything in it changes and cannot
 * go stale the way a compiled-in date can. It is read out of our own program
 * headers: dl_iterate_phdr walks the loaded objects, ours is the one holding
 * the address of this very function, and its PT_NOTE segment carries
 * NT_GNU_BUILD_ID.
 */
#define NT_GNU_BUILD_ID 3

static char g_build_id[48];

static int find_build_id(struct dl_phdr_info *info, size_t size, void *want) {
    (void) size;
    for (int i = 0; i < info->dlpi_phnum; i++) {
        const ElfW(Phdr) *ph = &info->dlpi_phdr[i];
        unsigned long lo = info->dlpi_addr + ph->p_vaddr;
        if (ph->p_type == PT_LOAD && (unsigned long) want >= lo
            && (unsigned long) want < lo + ph->p_memsz) {
            /* ours: now walk it again for the note */
            for (int j = 0; j < info->dlpi_phnum; j++) {
                const ElfW(Phdr) *n = &info->dlpi_phdr[j];
                if (n->p_type != PT_NOTE) continue;
                const unsigned char *p = (const unsigned char *) (info->dlpi_addr + n->p_vaddr);
                const unsigned char *end = p + n->p_memsz;
                while (p + 12 <= end) {
                    unsigned namesz = *(const unsigned *) p;
                    unsigned descsz = *(const unsigned *) (p + 4);
                    unsigned type = *(const unsigned *) (p + 8);
                    const unsigned char *name = p + 12;
                    const unsigned char *desc = name + ((namesz + 3) & ~3u);
                    if (type == NT_GNU_BUILD_ID && namesz == 4 && !memcmp(name, "GNU", 4)
                        && desc + descsz <= end) {
                        int at = 0;
                        for (unsigned k = 0; k < descsz && at + 2 < (int) sizeof(g_build_id); k++)
                            at += snprintf(g_build_id + at, sizeof(g_build_id) - (size_t) at,
                                           "%02x", desc[k]);
                        return 1;
                    }
                    p = desc + ((descsz + 3) & ~3u);
                }
            }
            return 1;
        }
    }
    return 0;
}

const char *shim_build_id(void) {
    if (!g_build_id[0]) {
        dl_iterate_phdr(find_build_id, (void *) (uintptr_t) &shim_build_id);
        if (!g_build_id[0]) snprintf(g_build_id, sizeof(g_build_id), "(no build id)");
    }
    return g_build_id;
}

/*
 * The same answer for a dying process: whatever was read at startup, and no
 * reading now. dl_iterate_phdr takes a lock, and a lock is the one thing a
 * signal handler must not wait on.
 */
static const char *build_id_already(void) {
    return g_build_id[0] ? g_build_id : "(not read yet)";
}

void shim_set_report_fd(int fd) {
    if (fd < 0) return;
    g_report_fd = fd;
    /*
     * The first line checks ourselves — if it does not show up in the file, the
     * native part cannot write here, and looking for our failures in this file
     * is pointless.
     */
    void shim_catch_crashes(void);
    {
        char line[120];
        snprintf(line, sizeof(line),
                 "report attached, crash interception installed; build %s", shim_build_id());
        report_line("layer", line);
    }
    shim_catch_crashes();
}

/*
 * Is anyone listening? Java opens the report only when it wants one, so this
 * single answer decides whether the layer says anything at all — the startup
 * self-test, the trace that remembers the last file, and every line below.
 * A build for playing opens nothing and pays for nothing.
 */
int shim_report_active(void) { return g_report_fd >= 0; }

void report_line(const char *kind, const char *text) {
    if (g_report_fd < 0) return;
    char line[2100];                /* the frames line with its vsync, ground-truth and core parts runs past 1400 (16.10.2026) */
    int n = snprintf(line, sizeof(line), "%s: %s\n", kind, text);
    if (n > 0) {
        ssize_t ignored = write(g_report_fd, line, (size_t) (n < (int) sizeof(line) ? n : (int) sizeof(line) - 1));
        (void) ignored;
    }
}

/* ---------- a native crash names itself ---------- */

/*
 * Why this is here. Two complaints of "closes right on the tap" from other
 * people's phones, and neither has a crash report. And there is none in two
 * quite different cases: the system killed the app for its appetite for memory
 * — or we died in native code and the firmware did not show the report. From
 * outside these are indistinguishable, and they need different fixes.
 *
 * So we catch the fatal signals ourselves and append a line to the same
 * `isaac-start.txt`. If the file breaks off at a stage and there is no crash
 * line, we were killed from outside. If the line is there, the crash is ours,
 * and the offset shows the place in the image.
 *
 * Inside a signal handler only what is safe to interrupt mid-word may be
 * called: `write` onto a descriptor that is already open. Neither printf nor
 * malloc — so a number is
 * turned into text by hand.
 */
static unsigned long g_report_image_base, g_report_image_size;

static void number_to_text(unsigned long v, char *dst, int base) {
    char tmp[24];
    int n = 0;
    if (!v) tmp[n++] = '0';
    while (v) {
        int digit = (int) (v % (unsigned) base);
        tmp[n++] = (char) (digit < 10 ? '0' + digit : 'a' + digit - 10);
        v /= (unsigned) base;
    }
    while (n) *dst++ = tmp[--n];
    *dst = 0;
}

static void append(int f, const char *text) {
    size_t n = 0;
    while (text[n]) n++;
    ssize_t ignored = write(f, text, n);
    (void) ignored;
}


/*
 * Which mapping an address belongs to, answered from inside a signal handler.
 *
 * A bare address says nothing: every library moves on each launch. dladdr would
 * name it, but dladdr takes locks and allocates, and neither is allowed once a
 * process is already dying. Reading /proc/self/maps is: open, read and close are
 * all safe to interrupt, and the file already holds the answer in plain text.
 */
static void name_of_address(int f, unsigned long pc) {
    char buf[4096];
    int fd = open("/proc/self/maps", O_RDONLY);
    if (fd < 0) return;
    ssize_t got;
    char line[256];
    size_t n = 0;
    while ((got = read(fd, buf, sizeof(buf))) > 0) {
        for (ssize_t i = 0; i < got; i++) {
            if (buf[i] != '\n') {
                if (n + 1 < sizeof(line)) line[n++] = buf[i];
                continue;
            }
            line[n] = 0;
            /* "start-end perms offset dev inode path" */
            unsigned long lo = 0, hi = 0;
            size_t p = 0;
            while (line[p] && line[p] != '-') {
                int d = line[p] >= 'a' ? line[p] - 'a' + 10 : line[p] - '0';
                if (d < 0 || d > 15) break;
                lo = lo * 16 + (unsigned) d; p++;
            }
            unsigned long fileoff = 0;
            if (line[p] == '-') {
                p++;
                while (line[p] && line[p] != ' ') {
                    int d = line[p] >= 'a' ? line[p] - 'a' + 10 : line[p] - '0';
                    if (d < 0 || d > 15) break;
                    hi = hi * 16 + (unsigned) d; p++;
                }
                /*
                 * Past the permissions to the file offset. Without it the number
                 * printed is an offset into one mapping, and a library's code
                 * does not start at the beginning of its file — which is why
                 * looking 0x218e0 up in libc++.so found nothing at all.
                 */
                while (line[p] == ' ') p++;
                while (line[p] && line[p] != ' ') p++;        /* perms */
                while (line[p] == ' ') p++;
                while (line[p] && line[p] != ' ') {
                    int d = line[p] >= 'a' ? line[p] - 'a' + 10 : line[p] - '0';
                    if (d < 0 || d > 15) break;
                    fileoff = fileoff * 16 + (unsigned) d; p++;
                }
            }
            if (pc >= lo && pc < hi) {
                char num[24];
                const char *path = line;
                for (size_t q = 0; line[q]; q++) if (line[q] == '/') { path = line + q; break; }
                append(f, "\n  in ");
                append(f, path);
                append(f, " at ");
                number_to_text(pc - lo + fileoff, num, 16);
                append(f, num);
                close(fd);
                return;
            }
            n = 0;
        }
    }
    close(fd);
}

static void on_signal(int signo, siginfo_t *info, void *ctx) {
    {
        /* the descriptor is already open — a signal handler need not even open() */
        int f = g_report_fd;
        if (f >= 0) {
            char num[24];
            append(f, "build ");
            append(f, build_id_already());
            append(f, "\n");
            append(f, "CRASH: signal ");
            number_to_text((unsigned long) signo, num, 10);
            append(f, num);
            append(f, signo == SIGILL ? " (the processor does not know this instruction)"
                     : signo == SIGSEGV ? " (access at somebody else's address)"
                     : signo == SIGBUS ? " (an invalid memory access)"
                     : signo == SIGFPE ? " (an arithmetic error)"
                     : signo == SIGABRT ? " (the game stopped itself)" : "");
            append(f, ", address 0x");
            number_to_text((unsigned long) (info ? (unsigned long) info->si_addr : 0),
                           num, 16);
            append(f, num);
            unsigned long pc = 0;
            if (ctx) pc = (unsigned long) ((ucontext_t *) ctx)->uc_mcontext.pc;
            append(f, ", instruction at 0x");
            number_to_text(pc, num, 16);
            append(f, num);
            /*
             * Inside the image means BELOW its end as well. Without that second
             * half the test called every library address a game address and
             * printed a fantastic offset for it.
             */
            if (g_report_image_base && pc >= g_report_image_base
                    && pc < g_report_image_base + g_report_image_size) {
                append(f, " (in the game image, offset 0x");
                number_to_text(pc - g_report_image_base, num, 16);
                append(f, num);
                append(f, ")");
            } else {
                append(f, " (outside the image — so in our layer or in the system)");
                name_of_address(f, pc);
            }
            /*
             * What the game was working on. A fault address names the code but
             * not the data, and the difference between "the path was empty" and
             * "the path was fine and the parsing failed" is the whole diagnosis.
             * The text is copied into a fixed buffer by the '=' trace, so
             * reading it here dereferences nothing that could fault twice.
             */
            const char *shim_trace_last_arg(void);
            const char *last = shim_trace_last_arg();
            if (last && last[0]) {
                append(f, ", last path \"");
                append(f, last);
                append(f, "\"");
            } else {
                append(f, ", last path EMPTY");
            }
            /*
             * Which file in order, and how deep the stack had gone by then. The
             * first tells a crash on the very first file from one a thousand
             * files in; the second says whether the stack was anywhere near
             * running out when it happened.
             */
            {
                unsigned long shim_trace_count(void);
                unsigned long shim_stack_left_kb(void);
                append(f, ", file no ");
                number_to_text(shim_trace_count(), num, 10);
                append(f, num);
                append(f, ", stack left ");
                number_to_text(shim_stack_left_kb(), num, 10);
                append(f, num);
                append(f, " KB");
            }
            append(f, "\n");
            /*
             * The way in: the chain of frames.
             *
             * A fault address names one instruction, and when that instruction
             * turns out to sit in somebody else's library the only useful
             * question left is who called it. Every frame here stores the
             * previous frame at its own address and the return address eight
             * bytes on, so walking the chain costs two reads a step and needs
             * nothing that a dying process cannot do. Addresses inside the game
             * image are printed as offsets, which is what the tools take.
             */
            if (ctx) {
                unsigned long fp2 = ((ucontext_t *) ctx)->uc_mcontext.regs[29];
                unsigned long sp2 = ((ucontext_t *) ctx)->uc_mcontext.sp;
                int depth;
                append(f, "  called from");
                for (depth = 0; depth < 12 && fp2 && fp2 >= sp2 && (fp2 & 7) == 0; depth++) {
                    unsigned long ret = *(const unsigned long *) (fp2 + 8);
                    unsigned long next = *(const unsigned long *) fp2;
                    if (!ret) break;
                    append(f, " ");
                    if (g_report_image_base && ret >= g_report_image_base
                            && ret < g_report_image_base + g_report_image_size) {
                        append(f, "game+");
                        number_to_text(ret - g_report_image_base, num, 16);
                    } else {
                        number_to_text(ret, num, 16);
                    }
                    append(f, num);
                    if (next <= fp2 || next - fp2 > 0x100000) break;
                    fp2 = next;
                }
                append(f, "\n");
            }
            /*
             * Registers and the words just under the frame pointer.
             *
             * A fault address names the instruction; these name the data. For
             * the crash we are chasing that is the whole question: the three
             * words of the vector say whether it was never filled at all
             * (begin, end and cap all zero — the parsing produced nothing) or
             * filled and then damaged. No guess distinguishes those two; this
             * does.
             *
             * The frame is read only when it looks like a frame — above the
             * stack pointer and within a megabyte of it. A signal handler that
             * faults a second time reports nothing at all.
             */
            if (ctx) {
                const unsigned long *r = ((ucontext_t *) ctx)->uc_mcontext.regs;
                unsigned long sp_now = ((ucontext_t *) ctx)->uc_mcontext.sp;
                int i;
                for (i = 0; i <= 30; i++) {
                    append(f, (i % 6) ? " x" : "\n  x");
                    number_to_text((unsigned long) i, num, 10);
                    append(f, num);
                    append(f, "=");
                    number_to_text(r[i], num, 16);
                    append(f, num);
                }
                append(f, "\n  sp=");
                number_to_text(sp_now, num, 16);
                append(f, num);
                unsigned long fp = r[29];
                if (fp && sp_now && fp > sp_now && fp - sp_now < 0x100000) {
                    long k;
                    append(f, "\n  frame");
                    for (k = -0x90; k <= -0x68; k += 8) {
                        append(f, " ");
                        number_to_text(*(const unsigned long *) (fp + k), num, 16);
                        append(f, num);
                    }
                }
                append(f, "\n");
            }
        }
    }
    /*
     * The saves the game has already closed are on the writer thread's queue
     * (safefile.c, hotfix 1.72): a moment for them to reach the disk - atomic
     * loads and nanosleep, 1.5 s at most - before the process goes.
     */
    {
        void shim_safe_crash_wait(void);
        shim_safe_crash_wait();
    }
    /* let things go on as they were: the system writes its own report if it can */
    signal(signo, SIG_DFL);
    raise(signo);
}

void shim_catch_crashes(void) {
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_sigaction = on_signal;
    sa.sa_flags = SA_SIGINFO;
    sigemptyset(&sa.sa_mask);
    int signals[] = { SIGSEGV, SIGBUS, SIGILL, SIGFPE, SIGABRT };
    for (unsigned i = 0; i < sizeof(signals) / sizeof(signals[0]); i++)
        sigaction(signals[i], &sa, NULL);
}

/* ---------- Mach-O headers, only the fields we need ---------- */

#define MH_MAGIC_64 0xfeedfacf
#define LC_SEGMENT_64 0x19
#define LC_MAIN 0x80000028
#define LC_DYLD_CHAINED_FIXUPS 0x80000034
#define LC_DYLD_EXPORTS_TRIE 0x80000033

struct mach_header_64 {
    uint32_t magic, cputype, cpusubtype, filetype, ncmds, sizeofcmds, flags, reserved;
};
struct load_command { uint32_t cmd, cmdsize; };
struct segment_command_64 {
    uint32_t cmd, cmdsize;
    char segname[16];
    uint64_t vmaddr, vmsize, fileoff, filesize;
    uint32_t maxprot, initprot, nsects, flags;
};
struct section_64 {
    char sectname[16], segname[16];
    uint64_t addr, size;
    uint32_t offset, align, reloff, nreloc, flags, r1, r2, r3;
};
struct chained_fixups_header {
    uint32_t fixups_version, starts_offset, imports_offset, symbols_offset;
    uint32_t imports_count, imports_format, symbols_format;
};
struct starts_in_image { uint32_t seg_count; uint32_t seg_info_offset[1]; };
struct starts_in_segment {
    uint32_t size;
    uint16_t page_size, pointer_format;
    uint64_t segment_offset;
    uint32_t max_valid_pointer;
    uint16_t page_count, page_start[1];
};

/* ---------- the state of the loaded image ---------- */

typedef struct {
    uint8_t *base;        /* where the image landed */
    uint64_t lo, hi;      /* the original bounds, for translating addresses */
    uint64_t size;
    const uint8_t *raw;   /* the source file */
    uint64_t raw_size;
    uint64_t entry;       /* the offset of the entry point */
    const uint8_t *trie, *trie_end;   /* the export trie: our own names */
    /* the segments, for finding sections */
    const struct section_64 *sects[64];
    int nsects;
} image_t;

static uint8_t *addr_to_mem(image_t *im, uint64_t a) {
    if (a < im->lo || a >= im->hi) return NULL;
    return im->base + (a - im->lo);
}

/* ---------- resolving foreign names ---------- */

/*
 * Three sources, in decreasing order of honesty:
 *   1. Android itself — libc, libm, libc++, GLES: the names match letter for
 *      letter, only without the leading underscore that Apple's linker adds;
 *   2. our own stubs — there are none yet;
 *   3. the swamp — a conspicuous address that shows at once what was missing.
 */
static void *g_libs[8];
static int g_nlibs;

static void open_libs(void) {
    static const char *names[] = {
        "libc.so", "libm.so", "libdl.so", "libc++_shared.so",
        "libGLESv2.so", "libGLESv1_CM.so", "libEGL.so", NULL
    };
    for (int i = 0; names[i]; i++) {
        void *h = dlopen(names[i], RTLD_NOW | RTLD_GLOBAL);
        if (h && g_nlibs < 8) g_libs[g_nlibs++] = h;
    }
}

void *shim_look_up(const char *name);

/*
 * A search through the Android libraries for the rest of the layer. Our own
 * dlopen from somewhere else will not do: some of the libraries here are opened
 * in advance and with RTLD_GLOBAL, and the search must cover exactly those.
 */
void *shim_android_symbol(const char *name) {
    for (int i = 0; i < g_nlibs; i++) {
        void *p = dlsym(g_libs[i], name);
        if (p) return p;
    }
    return dlsym(RTLD_DEFAULT, name);
}

void shim_set_home(const char *path);
long shim_objc_messages(void);
long shim_objc_missed(void);
void shim_objc_set_image(void *base, unsigned long size);
void shim_str_set_cfstrings(void *lo, unsigned long size);
void shim_objc_set_selectors(const void *base, unsigned long size);
void shim_objc_set_classlist(void *base, unsigned long size);
void shim_uikit_install(void);
void shim_set_screen(int w, int h, double scale);
void shim_gl_set_window(void *win);
void shim_set_bundle(const char *path);
void shim_hook(void *at, void *to);
int shim_trace_add(unsigned long off, const char *name);
void shim_trace_seal(void);
void shim_trace_report(void);
void shim_game_log(void);

void shim_sticks_load(const char *dir);
void shim_probe_setup(const char *);
void shim_app_pause(void);
void shim_ime_push(const char *);
void shim_app_resume(void);
void shim_set_saves(const char *);
int shim_sticks_static(void);
void shim_darwin_init(void);
void shim_darwin_streams(void);

/*
 * "The DLC is bought" — yes.
 *
 * The game asks the Apple store about five purchases and picks an edition from
 * the answer: 3 — Repentance, 2 — Afterbirth+, 1 — Afterbirth, 0 — Rebirth. It
 * asks through StoreKit, which does not exist on Android at all, so the answer
 * was always "no" and the game ran as Afterbirth — with its own set of items,
 * floors and even its own control class.
 *
 * The owner bought the game with every add-on, so the honest answer here is
 * "bought". We give it to any request: one place instead of five checks.
 */
static void own_all_dlc(image_t *im) {
    uint32_t *p = (uint32_t *) (im->base + 0x8dab10);
    if (p[0] != 0xA9BD57F6u) { LOG("purchases: the code is not the one expected (%08x)", p[0]); return; }
    p[0] = 0x52800020u;                     /* mov w0, #1 */
    p[1] = 0xD65F03C0u;                     /* ret         */

    /*
     * One answer is not enough: the game remembers the edition level in
     * shell_startup_state.dat and, if the signature matches, does NOT ask the
     * store at all. The file already held a one — Afterbirth — and the patch
     * above simply never reached the decision. We remove the early exit: let it
     * ask every time, and then the remembered value stops deciding anything.
     */
    uint32_t *early = (uint32_t *) (im->base + 0x4cc0);
    if (*early != 0x54000241u) { LOG("purchases: the early exit was not recognised (%08x)", *early); return; }
    *early = 0xD503201Fu;                   /* nop */
    LOG("purchases: the edition is determined afresh on every launch");
    LOG("purchases: 8dab10 now holds %08x %08x, 4cc0 holds %08x", p[0], p[1], *early);
}

/*
 * THE GAME'S OWN LOG, back on - report builds only (23.09.2026).
 *
 * Nicalis cut the writing out: the logger every subsystem calls, 0x8c99c0
 * (level, format, ...), is a bare `ret` in the shipped build. The calls are all
 * still there, over a thousand of them - "Failed to load costume alt color
 * spritesheet %s!" (0x1a3b8c), "[warn] Costume for item %d (%s) has no
 * layers!", "Invalid costume count", and every other thing the engine trips
 * over. Put back, it tells what is wrong about ANY item or character without
 * naming one in advance (Azazel's costumes, the owner's list of 16.09, item 7).
 *
 * One word is all there is at the logger: the next function starts right after
 * it. So the word becomes a branch to a trampoline four words long, and those
 * four are dead ones: the purchases check at 0x8dab10 now answers at once
 * (own_all_dlc: mov w0, #1; ret), and its body from 0x8dab18 on is entered by
 * nobody - tools/into.py counts ten entries into it, all at 0x8dab10. The
 * trampoline is shim_hook's: ldr x16, [pc, #8]; br x16; the address.
 *
 * Variadic arguments sit on the stack in Apple's convention: shim_game_log
 * (objc_asm.S) catches sp and shim_game_log_impl reads them from there.
 */
#define GAME_LOGGER     0x8c99c0
#define LOG_TRAMPOLINE  0x8dab18

static void game_log_on(image_t *im) {
    uint32_t *site = (uint32_t *) (im->base + GAME_LOGGER);
    uint32_t *tramp = (uint32_t *) (im->base + LOG_TRAMPOLINE);
    if (site[0] != 0xD65F03C0u || tramp[0] != 0xA9027BFDu || tramp[1] != 0x910083FDu
        || tramp[2] != 0xAA0003F4u || tramp[3] != 0x90000E08u) {
        LOG("game log: the words are not the ones expected (%08x; %08x %08x %08x %08x)",
            site[0], tramp[0], tramp[1], tramp[2], tramp[3]);
        return;
    }
    shim_hook(tramp, (void *) shim_game_log);
    site[0] = 0x14004456u;                  /* b 0x8dab18 */
    LOG("game log: on, through 0x8dab18");
}

/*
 * The cloud save poll — out of every frame.
 *
 * The measurement: the layer created one string and one date per frame and
 * never released them (our `release` is a blank), so memory grew by 0.9 MB a
 * minute the whole time it ran. A per-class counter named the culprits, and a
 * reconnaissance trace named the place: the string "SaveDataFiles2" came from
 * 0x8bbec0.
 *
 * What happens there. Function 0x8bbec0 is the cloud save check. It creates a
 * CloudKit record name ("SaveDataFiles2", 0x8bbee8), then takes the current
 * time (0x8bbf38) and compares it with the previous check (0x8bbf4c) so as to
 * go to the cloud no more than once every five seconds. That is, the rate
 * limiter itself runs every frame and litters every frame, while the cloud is
 * reached rarely — and even then it runs into our stubs: we answer neither
 * `privateCloudDatabase` nor `performQuery:inZoneWithID:completionHandler:`.
 *
 * The port has no cloud and never will, so we remove the cause rather than the
 * effect: the single call to this check is silenced. The wrapper 0x8e121c does
 * not look at the result — right after the call it puts its own self into x0
 * (0x8e1234) — so replacing the call with a no-op is safe.
 */
static void no_cloud_poll(image_t *im) {
    uint32_t *p = (uint32_t *) (im->base + 0x8e1230);
    if (*p != 0x97FF6B24u) { LOG("cloud: the call was not recognised (%08x)", *p); return; }
    *p = 0xD503201Fu;                       /* nop */
    LOG("cloud: the save poll is out of every frame");
}

/*
 * Saves written plain, the way the desktop build writes them.
 *
 * The iOS writer 0x8e1890 squeezes every save into an LZ4 block before it
 * goes to disk, and the desktop build does not understand that: it looks for
 * the magic at the first byte and finds the block's token instead. Nothing on
 * the reading side needs the compression - the game's own reader (0x8e1720)
 * falls back to the plain bytes when the block does not decode, and a plain
 * save never decodes (work/saves/notes/format.md, section 1).
 *
 * Two words. The compression call becomes "x0 = the plain size" (the next word
 * hands x0 to the write as its length), and the write's buffer becomes the
 * plain one in x24 instead of the compressed one in x26. The bound buffer is
 * still allocated and freed a few words later, untouched; nothing else reads it.
 */
static void plain_saves(image_t *im) {
    uint32_t *squeeze = (uint32_t *) (im->base + 0x8e197c);
    uint32_t *buffer = (uint32_t *) (im->base + 0x8e1990);
    if (squeeze[0] != 0x97FF9A83u || squeeze[1] != 0xAA0003E2u || *buffer != 0xAA1A03E1u) {
        LOG("saves: the writer was not recognised (%08x %08x %08x)", squeeze[0], squeeze[1], *buffer);
        return;
    }
    *squeeze = 0xAA1703E0u;                 /* mov x0, x23 - the plain size    */
    *buffer = 0xAA1803E1u;                  /* mov x1, x24 - the plain bytes   */
    LOG("saves: written plain, as the desktop build writes them");
}

/*
 * Our brightness default, made the game's own (27.09.2026).
 *
 * With no options.ini the reader (Repentance 0x7eb540, called once from the
 * Manager's init) sees Version 0, and the branch for "not version 2" forces
 * Gamma to 0.65 and writes options.ini on the spot (0x7eba38..0x7eba48 ->
 * 0x7ebc90). From then on the file is the player's.
 *
 * Our default used to be put into memory afterwards, frame by frame, until the
 * file appeared - but the file already held 0.65 by then, and the game only
 * writes it again when an option changes. A player who never touched the
 * settings got 0.65 back on the next launch; that is the "65 again" the owner
 * kept seeing after a clean install. Changing the constant of that branch
 * makes the game write 0.90 into the file itself, and no race is left.
 *
 * The same branch sits in all three engine copies (AB 0x244ef4 and AB+
 * 0x4c1188 keep Gamma at +0x40 instead of +0x28). Each is checked by both of
 * its words: mov w8, #0x6666; movk w8, #0x3f26, lsl #16 (0.65f = 0x3f266666)
 * becomes movk #0x3f66 (0.90f = 0x3f666666).
 *
 * THAT BRANCH IS NOT THE ROAD OF A FRESH INSTALL (measured 27.09.2026: the
 * patch in place, 3 of 3, and the new file still said Gamma=0.6500). With no
 * options.ini under any of its three names the reader never reaches Version:
 * at 0x7ebb34 it writes the file at once (0x7ebc90) from what the block
 * holds - the defaults, put in by 0x7eb2b8 before anything is read. There
 * Gamma, Exposure, Brightness and Contrast go in as one 16-byte load from
 * 0x946fe0 = { 0.65, 1, 0, 1 } (ldr q0 at 0x7eb2d4). The constant is read by
 * four places only - 0x3d630, 0x2449a0, 0x4c0a6c, 0x7eb2d4, the defaults
 * function of each engine copy, every one storing it over Gamma - so its first
 * float is the game's default brightness, and it becomes 0.90 too.
 */
#define DEFAULTS_GAMMA 0x946fe0

/*
 * THE DOUBLE-TAP DETECTOR ANSWERS TO THE PORT ALONE (the owner, 27.09.2026:
 * "only our behaviour; the native one off - a chaotic stick sets Mars or
 * Hemoptysis off by itself").
 *
 * The game's detector, per player and per frame (movement 0x516788..0x5168bc
 * for Mars 593, shooting 0x5185a8..0x518a00 for Hemoptysis and the like): for
 * each of the four directions, if the action is triggered this frame, then if
 * it is the same direction as last time and no more than TEN frames have
 * passed (cmp w8, #0xa at 0x5167b0 / 0x5185d0) the item fires; either way
 * "last = this, frames = 0" (0x516894, 0x5189e4). The frame count goes up once
 * a frame after the loop (0x5168bc, 0x518a00). A stick that wobbles through a
 * direction twice inside ten frames is a double tap to it.
 *
 * The window becomes NOUGHT: a second natural press finds the count at one or
 * more and never passes. What passes is a press that the port has sanctioned:
 * preset_detector (sticks.c) writes the direction and a NEGATIVE count, and
 * for as many frames as it takes to climb back to nought the next press in
 * that direction fires. So the gestures are the port's own - two taps to a
 * side on a static stick or D-pad, a tap, then a tap and a flick on a moving
 * one - and nothing else. The Repentance copy only: the other editions' copies
 * never run.
 */
static void double_tap_gate(image_t *im) {
    static const unsigned long at[] = { 0x5167b0, 0x5185d0 };
    int done = 0;
    for (int i = 0; i < 2; i++) {
        uint32_t *p = (uint32_t *) (im->base + at[i]);
        if (*p != 0x7100291Fu) {                /* cmp w8, #0xa */
            LOG("double tap: %lx holds %08x - not the window, left alone", at[i], *p);
            continue;
        }
        *p = 0x7100011Fu;                       /* cmp w8, #0 */
        done++;
    }
    LOG("double tap: the game's window is shut, the port's presets open it (%d of 2)", done);
}

/*
 * THE GAME'S CO-OP JOIN: THE DOOR IS THE PORT'S (the owner, 28.09.2026).
 *
 * Repentance on iOS carries its local co-op whole: the join handler 0x567434
 * runs every frame from the Game's update, and a pad that nobody plays on
 * opens the character choice by holding Start (action 26, bound together with
 * pause). That way in is so odd the owner never met it in years on the
 * iPhone, and the device dispatcher fights it (coop.c). So the handler keeps
 * everything but its own doorbell: the three places where it asks "is action
 * 26 held on this free device" - the instant add with bomb and card
 * (0x56759c), the opening of a join slot (0x56767c) and the "not now" beep
 * (0x5676c8) - answer no for good. The slot is opened by the port instead
 * (coop.c, shim_coop_join_from_pad: Select and Start held together for three
 * seconds), and from there the game's own choice of character, its
 * confirmation and its spawn run untouched. The fourth ask of action 26, at
 * 0x567720, is a dead player's ghost coming back by holding Start - left as
 * it is.
 */
/*
 * THE GAME'S OWN DOOR, OPENED BY OUR GESTURE (the owner, 28.09.2026: "take it
 * off the game's own trigger and put it on ours"). The join handler 0x567434
 * asks "is action 26 held on this free device" (0x8b6600) in three places:
 *   0x56759c  held 26 with 8 and 10 triggered: a player added at once - off;
 *   0x56767c  held 26: the join slot opens, the HUD shows the character
 *             choice, and everything after is the game's own;
 *   0x5676c8  held 26 where no one may join: the game's "not now" sound.
 * The last two now ask shim_coop_join_asked instead (coop.c), which answers
 * yes for one pass of the handler after Select+Start were held three seconds
 * on that device (pad.c). The call goes through four dead words at 0x8dab28
 * (the purchases check's body, entered by nobody - see game_log_on) made a
 * trampoline; a bl keeps x0..x3 and the return, so the handler reads our
 * answer exactly where it read the game's. The ghost's revival (0x567720)
 * still asks the game: holding Start as a ghost is not a join.
 */
#define JOIN_TRAMPOLINE 0x8dab28
int shim_coop_join_asked(unsigned long input, int action, int device, unsigned long flags);
int shim_coop_device_pressed(unsigned long input, int device);
int shim_coop_device_pressed_new(unsigned long input, int device);

/*
 * THE DISPATCHER LEAVES A CHOOSING DEVICE ALONE (the owner, 28.09.2026: "while
 * the character is chosen the pad still drives player 0"). 0x674300 walks the
 * devices and hands player 0 the first one that was pressed and drives no
 * player - and a device choosing its character drives none yet. Its two
 * questions, "anything pressed" (0x8b6d38 at 0x674380) and "anything newly
 * pressed" (0x8b6d10 at 0x67438c), now come to coop.c, which says no for a
 * device with an open join slot and asks the game otherwise. Two more
 * trampolines in the same dead body, 0x8dab38 and 0x8dab48 (into.py: the
 * body's entries are 0x8dab10, 0x8daba0 and 0x8dabb0, nothing between).
 */
static void coop_dispatcher_asks(image_t *im) {
    static const uint32_t dead[8] = { 0xAA0003F3u, 0x90000E08u, 0xF9452500u, 0xAA1403E2u,
                                      0x9400A0F2u, 0xAA1D03FDu, 0x94009807u, 0xAA0003F4u };
    uint32_t *t = (uint32_t *) (im->base + 0x8dab38);
    for (int i = 0; i < 8; i++)
        if (t[i] != dead[i]) {
            LOG("coop: the words at 8dab38 are not the dead ones expected (%d: %08x) - the dispatcher stays as it is",
                i, t[i]);
            return;
        }
    /*
     * And the third, the one that did the stealing in the owner's run: with
     * one player the dispatcher does not go through 0x674300 at all but
     * through 0x6744e0 (0x6735f8), "the first device other than the screen
     * pressed anew" - its question is 0x8b6d10 at 0x674534. Every press
     * question of the dispatcher family (0x6732b4..0x674570) is one of these
     * three, or the screen's own at 0x6744a4, which is left alone.
     */
    uint32_t *any = (uint32_t *) (im->base + 0x674380), *fresh = (uint32_t *) (im->base + 0x67438c);
    uint32_t *first = (uint32_t *) (im->base + 0x674534);
    if (*any != 0x94090A6Eu || *fresh != 0x94090A61u || *first != 0x940909F7u) {
        LOG("coop: the dispatcher's questions are not the expected ones (%08x %08x %08x) - left alone",
            *any, *fresh, *first);
        return;
    }
    shim_hook(t, (void *) shim_coop_device_pressed);
    shim_hook(t + 4, (void *) shim_coop_device_pressed_new);
    *any = 0x940999EEu;                         /* bl 0x8dab38 */
    *fresh = 0x940999EFu;                       /* bl 0x8dab48 */
    *first = 0x94099985u;                       /* bl 0x8dab48 */
    LOG("coop: the device dispatcher asks the port before it hands a choosing device player 0");
}

/*
 * THE CONTINUE'S HAND-OUT OF DEVICES GOES THROUGH THE PORT (the 21st pass,
 * 05.10.2026; coop.c continue_handout). 0x88b9f0 `bl 0x56b0a0` (x0 = the
 * player manager, Game+0x25c40) is the one call of 0x56b0a0, made right
 * after 0x56959c has put every player on his own record's device - the only
 * moment it is known which player was which. There is no free trampoline
 * left, so the join's 0x8dab28 is shared by its first argument: the join
 * asks 0x8b6600 with the input system (0xac5528, 0x56766c/0x5676b8 `mov x0,
 * x21`), this call passes the player manager. Installed only together with
 * the join's own hook, the word checked before it is written.
 */
#define HANDOUT_CALL     0x88b9f0
#define HANDOUT_CALL_WAS 0x97F37DACu        /* bl 0x56b0a0 */
#define HANDOUT_CALL_NOW 0x94013C4Eu        /* bl 0x8dab28 */

static void coop_handout_ours(image_t *im) {
    uint32_t *w = (uint32_t *) (im->base + HANDOUT_CALL);
    if (*w == HANDOUT_CALL_NOW) return;
    if (*w != HANDOUT_CALL_WAS) {
        LOG("coop: %08x at %x is not the Continue's call of 0x56b0a0 - the hand-out stays the game's", *w, HANDOUT_CALL);
        return;
    }
    *w = HANDOUT_CALL_NOW;
    LOG("coop: the Continue's hand-out of devices asks the port (%x -> %x)", HANDOUT_CALL, JOIN_TRAMPOLINE);
}

static void coop_join_ours(image_t *im) {
    uint32_t *off = (uint32_t *) (im->base + 0x56759c);
    uint32_t *tramp = (uint32_t *) (im->base + JOIN_TRAMPOLINE);
    if (*off == 0x940D3C19u) *off = 0x52800000u;     /* bl 0x8b6600 -> mov w0, #0 */
    else LOG("coop: %08x at 56759c is not the ask for action 26 - left alone", *off);
    if (tramp[0] != 0xF944D900u || tramp[1] != 0x9400A091u || tramp[2] != 0xAA1D03FDu || tramp[3] != 0x9400980Eu) {
        LOG("coop: the words at %x are not the dead ones expected - the join stays the game's own (%08x %08x %08x %08x)",
            JOIN_TRAMPOLINE, tramp[0], tramp[1], tramp[2], tramp[3]);
        return;
    }
    static const struct { unsigned long at; uint32_t was, now; } ask[] = {
        { 0x56767c, 0x940D3BE1u, 0x940DCD2Bu },         /* bl 0x8dab28 */
        { 0x5676c8, 0x940D3BCEu, 0x940DCD18u },
    };
    for (int i = 0; i < 2; i++)
        if (*(uint32_t *) (im->base + ask[i].at) != ask[i].was) {
            LOG("coop: %08x at %lx is not the ask for action 26 - the join stays the game's own",
                *(uint32_t *) (im->base + ask[i].at), ask[i].at);
            return;
        }
    shim_hook(tramp, (void *) shim_coop_join_asked);
    for (int i = 0; i < 2; i++) *(uint32_t *) (im->base + ask[i].at) = ask[i].now;
    LOG("coop: the game's join asks the port (through %x)", JOIN_TRAMPOLINE);
    coop_dispatcher_asks(im);
    coop_handout_ours(im);
}

/*
 * THE TWINS OF A CONTINUED RUN: THE FIRST MATCH WINS (the owner, 04.10.2026;
 * the research notes of that day, co-op twins). 0x56959c brings the players
 * back from
 * the save and links the twins anew (+0x2580): for a main player of 19, 29,
 * 38 or 35 it walks the players AFTER him (0x569854..0x5698b0) and links
 * every one of the partner's type (20, 38, 29, 40) that sits on the same
 * device and has no twin yet - and it does not stop at the first. Two pairs
 * of one character saved on one device (after a Continue every player is put
 * on -1 by 0x56b0a0 until he presses something, and the save writes that -1)
 * come back as Jacob 1 <-> Esau 2, Esau 1 -> Jacob 1 one way only, Jacob 2
 * with nobody: the testers' "Jacob & Jacob, Esau & Esau", T.Forgotten's
 * bodies sharing one soul. A twin is made right after its main (0x551310 from
 * 0x506a84), so the first free one after him is his own: once he has one, he
 * stops looking. The word after the link, `ldr w8, [x24]` at 0x5698a4 (the
 * loop's next round reloads the count, 0x5698ac), becomes `b 0x5698b4`, the
 * outer loop's own step, which loads the count itself (0x5698b8). into.py
 * 0x569850 0x5698c4: the entries are 0x569854, 0x5698a8 and 0x5698b4 - nobody
 * enters at 0x5698a4, and x28 (0x2580) is not touched on this road. With
 * different devices nothing changes. coop.c mends a save already spoiled.
 */
#define TWIN_RELINK_NEXT   0x5698a4
#define TWIN_RELINK_WAS    0xB9400308u     /* ldr w8, [x24] */
#define TWIN_RELINK_NOW    0x14000004u     /* b 0x5698b4 */

static void coop_twins_first_match(image_t *im) {
    uint32_t *w = (uint32_t *) (im->base + TWIN_RELINK_NEXT);
    if (*w == TWIN_RELINK_NOW) return;
    if (*w != TWIN_RELINK_WAS) {
        LOG("coop: %08x at %x is not the twin search's next round - the restore links the twins as the game does",
            *w, TWIN_RELINK_NEXT);
        return;
    }
    *w = TWIN_RELINK_NOW;
    LOG("coop: a restored main player takes the first twin he finds (%x)", TWIN_RELINK_NEXT);
}

/*
 * A CO-OP PLAYER'S POCKET STAYS ON THE SCREEN (the owner, 05.10.2026: "let it
 * be half size, but at least in the corner where that player's health is").
 * The HUD draws players 2-4's pocket (cards, pills, the pocket active) at
 * half size under their hearts (0x6eea4c..0x6eea94): at (x - 36, y +
 * max(hearts' height, 32)) from the entry's corner; the height is 12 and 10
 * more a row of three (0x6e6c6c..0x6e6c80). In the lower corners (entries 2
 * and 3, y = H - 33, 0x6ee540/0x6ee578) that is the screen's bottom edge, and
 * with a fourth row of hearts (ten or more) it is below it. So the offset is
 * also held to the room under the corner: min(max(h, 32), H - y). The upper
 * corners have a whole screen of room and do not change; neither does a lower
 * one with up to three rows.
 *
 * The words: -12 and +12 around the height (0x6eea4c..0x6eea58) are a no-op
 * of the compiler's (s0 = s9) and give their four words to reading H and y;
 * the max reads s9 itself; 0x8bea24, the vector's setter, is `stp s0, s1,
 * [x0]; ret`, so `add x0, sp, #0xb0; bl 0x8bea24` becomes one store and frees
 * the word for the min. into.py 0x6eea28 0x6eea80: the only entry is the
 * trinket loop's own 0x6eea28; nobody branches into 0x6eea4c..0x6eea70. y is
 * the entry's corner at sp+0x22c (0x6ee690..0x6ee6cc), H the canvas height at
 * 0xac24dc (0x6ee6c0). Encodings checked with capstone.
 */
#define POCKET_AT 0x6eea4c

static void coop_pocket_on_screen(image_t *im) {
    static const uint32_t was[10] = { 0x1E351000u, 0x1E202920u, 0x1E251001u, 0x1E212800u, 0x0F026441u,
                                      0x1E216801u, 0x52B84208u, 0x1E270100u, 0x9102C3E0u, 0x94073FEDu };
    static const uint32_t now[10] = {
        0x90001EA8u,    /* adrp   x8, 0xac2000 */
        0xBD44DD02u,    /* ldr    s2, [x8, #0x4dc]       H */
        0xBD422FE0u,    /* ldr    s0, [sp, #0x22c]       the corner's y */
        0x1E203842u,    /* fsub   s2, s2, s0             the room under it */
        0x0F026441u,    /* movi   v1.2s, #0x42, lsl #24  32 (as was) */
        0x1E216921u,    /* fmaxnm s1, s9, s1             max(height, 32) */
        0x52B84208u,    /* mov    w8, #-36 (as was) */
        0x1E270100u,    /* fmov   s0, w8 (as was) */
        0x1E227821u,    /* fminnm s1, s1, s2             no lower than the screen */
        0x2D1607E0u,    /* stp    s0, s1, [sp, #0xb0]    what 0x8bea24 did */
    };
    uint32_t *w = (uint32_t *) (im->base + POCKET_AT);
    if (!memcmp(w, now, sizeof(now))) return;
    if (memcmp(w, was, sizeof(was))) {
        LOG("hud: the words at %x are not the co-op pocket's expected ones (%08x %08x) - left as the game draws it",
            POCKET_AT, w[0], w[9]);
        return;
    }
    memcpy(w, now, sizeof(now));
    LOG("hud: a co-op player's pocket is held on the screen (%x)", POCKET_AT);
}

/*
 * A RUN THAT TOOK FROM THE SPAWNER COUNTS FOR NOTHING, through the game's own
 * question (plan item A2; the owner, 30.09.2026: a challenge played with the
 * spawner must not count). 0x672828(manager, what) is the platform's say on
 * whether a run is honest - two words, `mov w0, #0; ret`, on iOS (on PC
 * something else stood there, by the look of it). It is called seven times,
 * all at its start (into.py): with what=0 in "is the run forbidden" 0x88b5b0
 * (0x88b5e0) and in the challenge reward 0x894d14 (0x894d60), with what=1 five
 * times in the daily menu 0x62d5b4. Its first word becomes `b 0x8dab58` - the
 * fifth trampoline in the purchases check's dead body (see game_log_on; the
 * words there are the body's own, entered by nobody) - and spawner.c answers:
 * the marked run for what=0, 0 as before for anything else. A `b`, not a `bl`:
 * the return address stays the caller's, and our w0 is the answer it reads.
 *
 * Also here, report builds only: the unlock watch (spawner.c, plan item A7).
 */
#define RUN_MARK_TRAMPOLINE 0x8dab58
#define PLATFORM_FORBIDS    0x672828
int shim_run_forbidden_asked(unsigned long manager, int what);
void shim_unlocks_watch_install(void);

static void platform_forbids_ours(image_t *im) {
    shim_unlocks_watch_install();
    uint32_t *site = (uint32_t *) (im->base + PLATFORM_FORBIDS);
    uint32_t *tramp = (uint32_t *) (im->base + RUN_MARK_TRAMPOLINE);
    if (site[0] != 0x52800000u || site[1] != 0xD65F03C0u
        || tramp[0] != 0xAA1303E0u || tramp[1] != 0xAA1403E2u || tramp[2] != 0x94009BFCu
        || tramp[3] != 0xAA0003F5u) {
        LOG("run mark: the words are not the ones expected (%08x %08x; %08x %08x %08x %08x) - "
            "the platform's question stays the game's stub", site[0], site[1], tramp[0], tramp[1], tramp[2], tramp[3]);
        return;
    }
    shim_hook(tramp, (void *) shim_run_forbidden_asked);
    site[0] = 0x1409A0CCu;                  /* b 0x8dab58 */
    LOG("run mark: the platform's question 0x672828 is answered by the port (through %x)", RUN_MARK_TRAMPOLINE);
}

static void brightness_default(image_t *im) {
    static const unsigned long at[] = { 0x7eba38, 0x244ef4, 0x4c1188 };
    int done = 0;
    uint32_t *k = (uint32_t *) (im->base + DEFAULTS_GAMMA);
    if (k[0] == 0x3F266666u && k[1] == 0x3F800000u && k[2] == 0 && k[3] == 0x3F800000u) {
        k[0] = 0x3F666666u;                 /* 0.90f */
        LOG("brightness: the defaults' constant at %x now starts with 0.90", DEFAULTS_GAMMA);
    } else {
        LOG("brightness: %x holds %08x %08x %08x %08x - not the defaults, left alone",
            DEFAULTS_GAMMA, k[0], k[1], k[2], k[3]);
    }
    for (int i = 0; i < 3; i++) {
        uint32_t *p = (uint32_t *) (im->base + at[i]);
        if (p[0] != 0x528CCCC8u || p[1] != 0x72A7E4C8u) {
            LOG("brightness: %lx holds %08x %08x - not the default, left alone", at[i], p[0], p[1]);
            continue;
        }
        p[1] = 0x72A7ECC8u;                 /* movk w8, #0x3f66, lsl #16 */
        done++;
    }
    LOG("brightness: a fresh options.ini gets 0.90 (%d of 3 copies)", done);
}

/*
 * AIM LOCK IS OFF ON A FRESH PROFILE (the owner, 30.09.2026) - as the game's
 * own default, the way brightness is: every engine copy's settings constructor
 * stores 1 into its aim lock byte, and that store is made a store of zero. The
 * first options.ini the game writes then carries AimLock=0, and from there the
 * byte has one owner - the Tweaks row and the file the game writes.
 *
 *   0x7eb30c  strb w9, [x0, #0x4d]     -> strb wzr        Repentance, +0x4d
 *   0x3d64c   strb w8, [x19, #0x58]    -> strb wzr        the copy with +0x58
 *   0x2449c4  mov w9, #0x100           -> mov w9, #0      AB: +0x61 (strh at +0x60)
 *   0x4c0aa0  movk w9, #0x100, lsl #16 -> movk w9, #0     AB+: +0x64 (stur at +0x61)
 *   0x7eb9ec  mov w2, #1               -> mov w2, #0      the Repentance reader's
 *                                                         default for a missing key
 *
 * The registers keep their other uses (w8/w9 are stored again right after);
 * only the value that lands in the aim lock byte changes. Every word is
 * checked first and a word that is not the expected one is left alone.
 *
 * What the byte does in the game (0x86077c..0x860854, the stick widget's own
 * update 0x862f04): with it on and a tear weapon the shooting stick's flag
 * +0x1c9 is 0, and a released stick KEEPS its value (0x86314c..0x863188
 * skips the zeroing) - the player goes on firing the last way set. That is
 * what the owner remembers from the iPhone.
 */
static void aimlock_default(image_t *im) {
    static const struct { unsigned long at; uint32_t was, now; } w[] = {
        { 0x7eb30c, 0x39013409u, 0x3901341Fu },
        { 0x3d64c,  0x39016268u, 0x3901627Fu },
        { 0x2449c4, 0x52802009u, 0x52800009u },
        { 0x4c0aa0, 0x72A02009u, 0x72A00009u },
        { 0x7eb9ec, 0x52800022u, 0x52800002u },
    };
    int done = 0;
    char left[160] = "";
    for (unsigned i = 0; i < sizeof(w) / sizeof(w[0]); i++) {
        uint32_t *p = (uint32_t *) (im->base + w[i].at);
        if (*p == w[i].now) { done++; continue; }
        if (*p != w[i].was) {
            size_t n = strlen(left);
            snprintf(left + n, sizeof(left) - n, " %lx=%08x", w[i].at, *p);
            continue;
        }
        *p = w[i].now;
        done++;
    }
    LOG("aim lock: off by default in %d of 5 places%s%s", done, left[0] ? ", NOT patched:" : "", left);
#if ISAAC_DIAG
    if (shim_report_active()) {
        char line[240];
        snprintf(line, sizeof(line), "off by default in %d of 5 places%s%s", done,
                 left[0] ? ", NOT patched:" : "", left);
        report_line("aimlock default", line);
    }
#endif
}

/*
 * Floating sticks — into fixed ones.
 *
 * The class TouchOverlay::Stick has these fields: the anchor the deflection is
 * measured from; the place of the stick itself on the screen; its radius. The
 * floating comes from two places:
 *
 *   in the touch capture — the anchor is put where the finger touched the
 *                          screen;
 *   in the update        — if the finger went further than the radius, the
 *                          anchor and the stick's own place are dragged after
 *                          it.
 *
 * We fix both with one word each: the anchor is taken from the stick's place,
 * and the dragging is skipped. The engine clamps the deflection to one anyway,
 * so past the edge the stick stops rather than crawling away.
 *
 * There are three classes — one each for Rebirth, Afterbirth and Repentance,
 * with identical code and DIFFERENT field offsets. A trace on the capture
 * once found Afterbirth's live - most likely from before the edition was set
 * right (own_all_dlc): the overlay the frame loop draws now is Repentance's,
 * Manager+0x1f6d18, and the report checks it on every run ("sticks: shooting
 * stick at manager+..., the image says ...: same", sticks.c). All three are
 * fixed all the same: which one comes into play depends on the game mode, not
 * on our assumption.
 */
/*
 * Sticks that stay where they are put.
 *
 * Two parts, and they differ in meaning.
 *
 * The first: on a touch the overlay PUTS the stick's place at the finger (two
 * copy calls) and only then hands it the touch. That is the dynamic behaviour —
 * for static sticks both calls are silenced.
 *
 * The second: the capture takes a touch if the finger is within THREE radii of
 * the stick. While the stick jumped under the finger the distance was always
 * near zero and the check meant nothing. For one that stays put it means
 * everything: three radii is almost half the screen, and the right stick
 * answered a poke anywhere on the right.
 *
 * We set FOUR. The stick's deflection radius is 16 units, the drawn base is 64,
 * exactly four times as much. So the capture zone coincides with the circle
 * that is visible on the screen and grows together with the stick's size.
 *
 * Floating after the finger is the third thing, and it lives at the "drag if
 * we went past the radius" fork, 0x86300c. That fork is the ONLY thing in the
 * game that moves a stick's seat once it is grabbed, and taking it out is what
 * anchors a stick — not anything in sticks.c. It is taken out for the static
 * and the dynamic kind, and left alone for the floating one, which is the
 * game's own behaviour untouched.
 */
/*
 * The setting defaults do NOT live in the ini reading.
 *
 * Two patches used to stand here: `aim_lock_off` and `brightness_75`. They
 * changed the third argument of the reading function — the default value — in
 * all four copies of the settings reading, and they applied correctly (the log
 * showed "4 places of 4"). It affected the behaviour in NO way: the game takes
 * the default from the settings block, not from here. The patches are removed
 * so as not to keep a substitution in the image that does nothing. How it
 * really works is in `sticks.c`, with the analysis in the handover notes.
 */

/*
 * Touch Mode shows a word, not a number.
 *
 * The item's branch drew the value as a number: it took an integer from the
 * settings and handed it to the number renderer — hence the "1" on the screen.
 * The neighbouring ExtraHUD item is built differently and shows a word: it
 * picks a layer FRAME by the value. The Touch Mode branch is moved to the same
 * trick.
 *
 * The layer is not the fourth one (that is tied to OptionsMenu.png, and there
 * is no room there) but a new eighteenth — it is defined in optionsmenu.anm2 on
 * the sheet OptionsExtra.png, where the words ANALOGUE and DIGITAL live.
 */
/*
 * The port's layers are not numbers fixed here any more (plan 30.09, M8): the
 * port's rows are a patch over whatever optionsmenu.anm2 / gamemenu.anm2 the
 * game loads (portpatch.c), and the number of a layer or null is read back from
 * that very file. Missing, or different in two language copies: the change
 * that needs it is left out (portpatch.c has said why in the report).
 */
int shim_port_layer(const char *rel, const char *name);
int shim_port_null(const char *rel, const char *name);
int shim_port_frames(const char *rel, const char *anim);
#define OPTIONS_ANM2 "gfx/ui/main menu/optionsmenu.anm2"
#define TITLE_ANM2   "gfx/ui/main menu/gamemenu.anm2"

/* movz wd, #imm - the image's own encoding: 0x52800241 is its mov w1, #18 */
static uint32_t movz_w(unsigned rd, unsigned imm) {
    return 0x52800000u | ((imm & 0xffffu) << 5) | (rd & 31u);
}

/* the layer of optionsmenu.anm2 that shows the port's row values, or -1 */
static int value_layer(void) {
    int layer = shim_port_layer(OPTIONS_ANM2, "Option Value 2");
    return layer >= 0 && layer <= 0xffff ? layer : -1;
}

static void touch_mode_words(image_t *im) {

    uint32_t *c = (uint32_t *) (im->base + 0x6e4020);
    if (c[0] != 0xB8696919u) {
        LOG("Touch Mode: the branch is not the one expected (%08x)", c[0]);
        return;
    }
    int layer = value_layer();
    if (layer < 0) {
        LOG("Touch Mode: optionsmenu.anm2 has no layer Option Value 2 - the value stays a number");
        return;
    }
    c[0] = 0xB8696902u;                     /* ldr w2, [x8, x9] — the value into an argument */
    c[1] = 0xAA1603E0u;                     /* mov x0, x22 — the item's sprite */
    c[2] = movz_w(1, (unsigned) layer);     /* mov w1, #layer — our layer (18 in the port's file) */
    c[3] = 0x9406EAF3u;                     /* bl 0x89ebf8 — set the layer frame */
    c[4] = 0x17FFFFDCu;                     /* b 0x6e3fa0 — the branch's common exit */
    LOG("Touch Mode: the value is shown as a word");
}

/*
 * TOUCH MODE switches on a tap, not on a swipe - and has three values.
 *
 * The item has two states, but it is built as a counter: "add the direction and
 * clamp to 1..2". A tap in our layer arrives as "left", that is, −1, and a
 * value sitting at 1 does not move anywhere — so one has to drag a finger. The
 * game's other two-position items (AIM LOCK, FILTER, POP-UPS) are built as a
 * toggle and work on a tap, so we are not inventing behaviour, we are bringing
 * the item into the same shape: any tap flips 1 <-> 2.
 *
 * The block at 0x6e3680 is twelve words, and there is not a single entry into
 * it, only byte 0xC1 of the tap table (checked with tools/into.py). The first
 * four words (take the field) stay, the fifth and onwards are rewritten; the
 * two-word tail becomes unreachable.
 *
 * THE THIRD VALUE, BUTTONS (the owner's item 19, 23.09.2026): the touch
 * D-pads, sticks.c. So a tap steps 1 -> 2 -> 3 -> 1. The step is taken as an
 * unsigned "at most 2": any other number, garbage included, comes back to 1,
 * so the field repairs itself instead of leading into a layer frame that does
 * not exist. Six of the eight words are used; the last two are unreachable.
 * The game reads the field in one place (0x863084, "is it 2") and the settings
 * reader stores it as it is (0x7eb9d8), so a 3 is kept and read as analogue.
 */
static void touch_mode_tap(image_t *im) {

    uint32_t *c = (uint32_t *) (im->base + 0x6e3690);
    if (c[0] != 0x0B14014Au || c[1] != 0x7100055Fu) {
        LOG("Touch Mode: the counter is not the one expected (%08x %08x)", c[0], c[1]);
        return;
    }
    c[0] = 0x1100054Bu;         /* add  w11, w10, #1            the next value */
    c[1] = 0x7100095Fu;         /* cmp  w10, #2                            */
    c[2] = 0x5280002Au;         /* mov  w10, #1                 or the first */
    c[3] = 0x1A8A916Au;         /* csel w10, w11, w10, ls       0..2 step on */
    c[4] = 0xB829690Au;         /* str  w10, [x8, x9]                      */
    c[5] = 0x17FFFFEEu;         /* b    0x6e365c    the handler's common exit */
    LOG("Touch Mode: switches on a tap, three values");
}

/*
 * Two items of our own in the settings menu: STICK TYPE and STICK SCALE.
 *
 * New branches cannot be appended to the image, and four item numbers stand
 * idle. But a "free branch" here does not mean "free room": the branches share
 * tails, and MapOpacity, for instance, jumps into the middle of the PenOpacity
 * branch. So we put the handlers only where nobody jumps:
 *
 *   0x6e4034 — ten instructions left over from the old number drawing in the
 *              Touch Mode branch; after our patch they are dead. The common
 *              tail goes here.
 *   0x6e3d98 — the Fullscreen branch, seven instructions of its own.
 *   0x6e3f0c — the Rumble branch, eight instructions of its own.
 *
 * The animation names are PenOpacity and Rumble: these are the only two numbers
 * absent both from the main menu list and from the pause list, so their content
 * in optionsmenu.anm2 can be taken over. ChangeController must not be taken: in
 * the pause menu it is a live item, "change the control device", and our
 * caption came out right inside it. The value is read from the settings object:
 * for the type we take the Fullscreen field (0x5eec) — there is one reference
 * to it in the whole image, and that one is in this very branch; for the size
 * we take the unnamed byte 0x5f16 between AimLock and TouchMode, which has no
 * references at all. The Filter field (0x5eed) must NOT be taken, even though
 * it sits right next to it: FILTER is a live item of both lists, and the stick
 * size switched it along the way.
 *
 * The order of the instructions matters: "name the animation" is a CALL, and it
 * clobbers w9. The field number is put in AFTER the call, otherwise the offset
 * arrives as garbage and a random settings byte stands on the screen. That is
 * exactly what "the value does not change" rested on: the toggle worked, but
 * somebody else's value was shown.
 */
#define B_TO(from, dst)  (0x14000000u | ((uint32_t) ((((long) (dst) - (long) (from)) / 4) & 0x03FFFFFFL)))
#define BL_TO(from, dst) (0x94000000u | ((uint32_t) ((((long) (dst) - (long) (from)) / 4) & 0x03FFFFFFL)))

/*
 * AIM LOCK becomes FULLSCREEN.
 *
 * The row is given away on the owner's word: the aim lock does not behave here
 * the way it was meant to, and nobody misses it - while a switch for the wide
 * canvas is wanted, because that is the one thing a weak phone may not be able
 * to afford.
 *
 * Nothing has to be drawn. The desktop game HAS a Fullscreen option, its
 * caption and its animation are in the image and in optionsmenu.anm2, and the
 * row that used to show it is the one WE took for the stick scale. So the art
 * is there for the asking - the name string sits at 0xa58fcc, and the pristine
 * branch at 0x6e3d98 named it exactly so before we overwrote it.
 *
 * And the aim lock's own branch turns out to be built the same way to the
 * word: name the animation, read a byte of the settings block, invert it, set
 * it as the frame of layer 4. So two words change and no code moves:
 *
 *     0x6e3f74  add x1, x1, #0xfc4   "AimLock"  ->  #0xfcc  "Fullscreen"
 *     0x6e3f84  mov w9, #0x5f15      aim lock   ->  #0x5f17  ours
 *
 * The inversion is kept because the real Fullscreen row had it too - it shared
 * this very tail - so one means on and the picture agrees with the value.
 *
 * The byte is 0x55f17, and it is free: nothing in the image builds that offset,
 * checked the same way the stick fields were. The aim lock's own byte keeps
 * its meaning; the row that switches it is on the Tweaks page (tweaks.c), its
 * default is off (aimlock_default) and options.ini stores it as it is - the
 * inversion below is the picture's only.
 *
 * The press goes through the same ready-made toggle as our other two rows -
 * item 18 is index 16 in the tap table - and which row was tapped is worked out
 * from the highlighted line in shim_sticks_menu_sync.
 */
#define FULLSCREEN_FIELD 0x5f17

static void fullscreen_menu_item(image_t *im) {
    uint32_t *name = (uint32_t *) (im->base + 0x6e3f74);
    uint32_t *field = (uint32_t *) (im->base + 0x6e3f84);
    unsigned char *taps = (unsigned char *) (im->base + 0x956dc8);
    /* add x1, x1, #0xfc4 -> #0xfcc ; movz w9, #0x5f15 -> #0x5f17 */
    if ((*name != 0x913F1021u && *name != 0x913F3021u) ||
        (*field != 0x528BE2A9u && *field != 0x528BE2E9u)) {
        LOG("menu: the aim lock row is not what was expected (%08x %08x)", *name, *field);
        return;
    }
    if (taps[13] != 0xA9) {
        LOG("menu: the toggle block is not in place (%02x)", taps[13]);
        return;
    }
    /* the game's own tail sets the frame of layer 4: it must be OnOff in the file loaded */
    if (shim_port_layer(OPTIONS_ANM2, "OnOff") != 4) {
        LOG("menu: OnOff is not layer 4 of optionsmenu.anm2 - the Fullscreen row left out");
        return;
    }
    *name = 0x913F3021u;                /* add x1, x1, #0xfcc   "Fullscreen" */
    *field = 0x528BE2E9u;               /* movz w9, #0x5f17     ours          */
    taps[16] = 0xA9;                    /* item 18 -> flip the byte           */
    LOG("menu: the row AIM LOCK now switches the wide canvas");
}

/*
 * RERUN COULD NOT BE TAPPED (the owner's list, item 21, 23.09.2026).
 *
 * The second main-menu button is CONTINUE or RERUN: its label (0x7f92f0) and
 * the pad's cursor (0x7f8ff4) both look at two bytes of the manager, the saved
 * run 0x4bb40 and the rerun 0x4bb41, and skip the button only when both are
 * zero. The touch handler 0x7f8ce8 looks at one: for hit region 2 it reads
 * 0x4bb40 alone and throws the tap away when it is zero - which is exactly
 * when the button says RERUN. A pad reached it, a finger never did.
 *
 * One word: the LDRB of that byte becomes an LDRH of the pair, so the tap goes
 * through when either is set - the same test the cursor makes. The field is
 * even, so the halfword is aligned. Afterbirth+ has Rerun too (it reads
 * 0x2b588 + 1 all over its menu and saves abp_rerunstate) and the same touch
 * handler with the same one-byte test at 0x4c1e74. Afterbirth has no Rerun -
 * nothing of its reads the byte after 0x374b8 - so its copy is left as it is.
 * The encoding is the image's own: 0x503520 holds ldrh w10, [x8, x9].
 */
static void rerun_tap(image_t *im) {
    static const struct { unsigned long at; const char *what; } SITES[] = {
        {0x7f8d7c, "Repentance"}, {0x4c1e74, "Afterbirth+"},
    };
    for (unsigned i = 0; i < 2; i++) {
        uint32_t *w = (uint32_t *) (im->base + SITES[i].at);
        if (*w != 0x38696908u && *w != 0x78696908u) {      /* ldrb / ldrh w8, [x8, x9] */
            LOG("rerun: %lx holds %08x, not the test expected - %s left alone",
                SITES[i].at, *w, SITES[i].what);
            continue;
        }
        *w = 0x78696908u;
        LOG("rerun: %s - a tap on RERUN goes through", SITES[i].what);
    }
}

/*
 * THE MAIN MENU HAS SIX ITEMS, MODS FOURTH, AS ON THE PC (the owner, 23.09.2026).
 *
 * The markup is tools/main_menu_mods.py: the PC's places for the items, a
 * sixth item layer, six cursor places, six touch areas in list order and the
 * daily run's area moved from 6 to 7. The iOS code counts five, in five
 * places, and each is one word or one byte:
 *
 *   0x7f8d44, 0x7f8d4c  the touch handler: an area above 6 is an error and 6 is
 *                       the daily run -> 7 for both (cmp w0, #7)
 *   0x959fc8            the area -> item table's sixth entry: 10005, never read
 *                       (area 6 was the daily run and left before it) -> 5
 *   0x7f8f6c            the confirm: an item above 4 does nothing -> 5
 *   0x959fac            the confirm's jump table, 00 5a 51 54 57 + a padding
 *                       byte nobody reads -> 00 5a 51 57 54 57: CHALLENGES,
 *                       then MODS, STATS, OPTIONS. MODS goes to the OPTIONS
 *                       block (0x7f90e8, a switch to the settings screen) - no
 *                       room for a block of its own - and hook V tells the two
 *                       apart by the highlighted item (tweaks.c, shim_mods_screen)
 *   0x7f9070            down from the last item wraps to the first: 4 -> 5
 *   0x7f90c4            up from the first wraps to the last: 4 -> 5
 *
 * The cursor's frame is the item number (0x7f92ec) and the markup has six. The
 * CONTINUE/RERUN skip and the label of item 1 do not move. Encodings are the
 * image's own: 0xd9598 cmp w0, #7; 0x4c20 cmp w8, #5; 0xae48 mov w8, #5.
 */
static void main_menu_mods(image_t *im) {
    static const struct { unsigned long at; uint32_t was, now; } WORDS[] = {
        {0x7f8d44, 0x7100181Fu, 0x71001C1Fu},
        {0x7f8d4c, 0x7100181Fu, 0x71001C1Fu},
        {0x959fc8, 0x00002715u, 0x00000005u},
        {0x7f8f6c, 0x7100111Fu, 0x7100151Fu},
        {0x7f9070, 0x7100111Fu, 0x7100151Fu},
        {0x7f90c4, 0x52800088u, 0x528000A8u},
    };
    static const unsigned char JUMP_WAS[6] = {0x00, 0x5a, 0x51, 0x54, 0x57, 0x00};
    static const unsigned char JUMP_NOW[6] = {0x00, 0x5a, 0x51, 0x57, 0x54, 0x57};
    unsigned n = sizeof(WORDS) / sizeof(WORDS[0]);
    for (unsigned i = 0; i < n; i++) {
        uint32_t w = *(const uint32_t *) (im->base + WORDS[i].at);
        if (w != WORDS[i].was && w != WORDS[i].now) {
            LOG("main menu: %lx holds %08x, not %08x - the MODS item left out", WORDS[i].at, w,
                WORDS[i].was);
            return;
        }
    }
    unsigned char *jump = im->base + 0x959fac;
    if (memcmp(jump, JUMP_WAS, 6) != 0 && memcmp(jump, JUMP_NOW, 6) != 0) {
        LOG("main menu: the confirm table is not the one expected - the MODS item left out");
        return;
    }
    /*
     * The words count touch areas by NULL NUMBER: areas 1..6 the items, 7 the
     * daily run, 0 the challenge button - as the port's patch lays them into
     * gamemenu.anm2. Another layout in the file loaded (a mod's): left out.
     */
    {
        static const char *const NULLS[8] = {"ChallengeButton", "Area1", "Area2", "Area3", "Area4", "Area5",
                                             "Area6", "AreaDailyRun"};
        for (int k = 0; k < 8; k++)
            if (shim_port_null(TITLE_ANM2, NULLS[k]) != k) {
                LOG("main menu: null %s of gamemenu.anm2 is not %d - the MODS item left out", NULLS[k], k);
                return;
            }
        if (shim_port_layer(TITLE_ANM2, "Item6") < 0) {
            LOG("main menu: gamemenu.anm2 has no layer Item6 - the MODS item left out");
            return;
        }
    }
    for (unsigned i = 0; i < n; i++) *(uint32_t *) (im->base + WORDS[i].at) = WORDS[i].now;
    memcpy(jump, JUMP_NOW, 6);
    LOG("main menu: six items, MODS fourth");
}

/*
 * THE COMMON TAIL OF THE PORT'S ROWS: a byte of the settings block set as the
 * frame of a layer, both named by the row that jumps here.
 *
 * It stands in the dead words of the Touch Mode branch (0x6e4034, see
 * touch_mode_words), and it used to name the layer itself - 18, the port's
 * Option Value 2 - because only the two stick rows came here. Since hotfix
 * 1.75 the RUMBLE row (the game's FILTER row, rumble_menu_item) comes here too,
 * with the game's own slider layer, Adjustor1 (3). So the caller puts the field
 * in w9 and the layer in w1 AFTER its "name the animation" call (a call: it
 * clobbers both), and the tail is five words that end on the game's own
 * `bl set frame` at 0x6e3f9c, which falls through into the common exit
 * 0x6e3fa0 - exactly what the old `bl` + `b 0x6e3fa0` did:
 *
 *     ldr  x8, [x28]          the settings object
 *     movk w9, #5, lsl #16    the field, 0x5xxxx
 *     ldrb w2, [x8, x9]       its byte: the frame
 *     mov  x0, x22            the row's sprite
 *     b    0x6e3f9c           bl 0x89ebf8(sprite, w1 layer, w2 frame)
 *
 * The two words after it (0x6e4048, 0x6e404c) stay dead; 0x6e4050 is the
 * literal of options_bottom and 0x6e405c is live (Gamma lands there). Nothing
 * from outside lands in 0x6e4034..0x6e405b (tools/into.py). The words are
 * checked first: the game's own or ours, nothing else.
 */
#define ROW_TAIL       0x6e4034
#define ROW_SET_FRAME  0x6e3f9c     /* bl 0x89ebf8, then the common exit 0x6e3fa0 */
#define ROW_NAME_ANIM  0x89eab8

static int row_tail(image_t *im) {
    static const uint32_t was[5] = {0x52A85188u, 0x1E270100u, 0x9101C3E0u, 0x1E2703E1u, 0x94076A78u};
    uint32_t now[5] = {0xF9400388u, 0x72A000A9u, 0x38696902u, 0xAA1603E0u, 0};
    now[4] = B_TO(ROW_TAIL + 16, ROW_SET_FRAME);
    uint32_t *t = (uint32_t *) (im->base + ROW_TAIL);
    if (memcmp(t, was, sizeof(was)) != 0 && memcmp(t, now, sizeof(now)) != 0) {
        LOG("menu: the words at %x are not the dead Touch Mode tail (%08x %08x) - the port's rows left out",
            ROW_TAIL, t[0], t[4]);
        return 0;
    }
    memcpy(t, now, sizeof(now));
    return 1;
}

static void stick_menu_items(image_t *im) {
    enum { BRANCH_TYPE = 0x6e3d98, BRANCH_SCALE = 0x6e3f0c };
    uint32_t *a = (uint32_t *) (im->base + BRANCH_TYPE);      /* STICK TYPE  */
    uint32_t *b = (uint32_t *) (im->base + BRANCH_SCALE);   /* STICK SCALE */
    unsigned char *tab = (unsigned char *) (im->base + 0x956dda);
    if (a[0] != 0xAA1603E0u || b[0] != 0xAA1603E0u) {
        LOG("menu: the free branches were not recognised (%08x %08x)", a[0], b[0]);
        return;
    }
    int layer = value_layer();
    if (layer < 0) {
        LOG("menu: optionsmenu.anm2 has no layer Option Value 2 - STICK TYPE and STICK SCALE left out");
        return;
    }
    if (!row_tail(im)) return;

    a[1] = 0xF0001BA1u;                         /* adrp x1, #0xa5a000                 */
    a[2] = 0x9137F421u;                         /* add  x1, x1, #0xdfd  "PenOpacity"  */
    a[3] = 0x52800002u;                         /* mov  w2, #0                        */
    a[4] = BL_TO(BRANCH_TYPE + 16, ROW_NAME_ANIM);  /* bl   0x89eab8   name the animation     */
    a[5] = 0x528BDD89u;                         /* movz w9, #0x5eec  the type field       */
    a[6] = movz_w(1, (unsigned) layer);         /* mov  w1, #layer   our layer (18)       */
    a[7] = B_TO(BRANCH_TYPE + 28, ROW_TAIL);    /* b    the tail                          */

    b[1] = 0xF0001BA1u;                         /* adrp x1, #0xa5a000                 */
    b[2] = 0x91388821u;                         /* add  x1, x1, #0xe22   "Rumble"     */
    b[3] = 0x52800002u;                         /* mov  w2, #0                        */
    b[4] = BL_TO(BRANCH_SCALE + 16, ROW_NAME_ANIM); /* bl   0x89eab8   name the animation     */
    b[5] = 0x528BE2C9u;                         /* movz w9, #0x5f16  the size field       */
    b[6] = movz_w(1, (unsigned) layer);         /* mov  w1, #layer   our layer (18)       */
    b[7] = B_TO(BRANCH_SCALE + 28, ROW_TAIL);   /* b    the tail                          */

    tab[5] = 0x27;              /* item 5 -> the handler at 0x6e3d98 */
    tab[6] = 0x84;              /* item 6 -> the handler at 0x6e3f0c */

    /*
     * The tap. The dispatch is indexed by the ITEM NUMBER MINUS TWO — that is
     * how the code itself counts (sub w9, w9, #2, then a 0..17 check at
     * 0x6e334c). Our numbers 5 and 6 give indices 3 and 4.
     *
     * There is no room of our own for handlers there — the blocks are three
     * instructions each. Nor is it needed: we point BOTH items at the ready
     * toggle for field 0x5f12 (0x6e3620). That is the Rumble item, which is not
     * in the phone's list, and nobody else reads the field. The block simply
     * flips a byte; which item was tapped and what to do about it is worked out
     * by our shim_sticks_menu_sync from the highlighted line.
     *
     * The previous attempt died here with SIGBUS, and not because of the game:
     * diagnostic traces stood inside the table itself (0x6e3644, 0x6e3464,
     * 0x6e3484), and a trace takes five words where the block length is three.
     * The fifth word landed on the common tail 0x6e364c, which is where this
     * toggle goes — control ended up in the middle of an eight-byte address.
     * The traces are gone.
     */
    unsigned char *tab2 = (unsigned char *) (im->base + 0x956dc8);
    if (tab2[13] != 0xA9) {
        LOG("menu: the toggle for field 0x5f12 is not in place (%02x)", tab2[13]);
        return;
    }
    tab2[3] = 0xA9;             /* item 5 -> flip byte 0x5f12 (0x6e3620) */
    tab2[4] = 0xA9;             /* item 6 -> the same place              */
    LOG("menu: the items STICK TYPE and STICK SCALE are in place");
}

/*
 * FILTER BECOMES RUMBLE (the owner, hotfix 1.75).
 *
 * The owner's word: the game's FILTER option goes for good, and the strength
 * of the rumble - until now a slider on our Tweaks page - takes its row in the
 * game's own Options, lettered by the game. Nothing is drawn: the console
 * versions' Rumble row is still in every optionsmenu.anm2 the IPA carries,
 * label and all, on OptionsMenu.png at (128,368) 64 x 16 - RUMBLE in English,
 * German and Russian, VIBRACIÓN (118,365 72 x 18), ランブル, 진동, 手柄振动 -
 * and the slider is the one SFX and MAP OPACITY use (Adjustor1, crops
 * 192,400..240, eleven frames, 0..10).
 *
 * The animation the row names stays "Filter" (image strings are never renamed:
 * the port's own STICK SCALE row already holds the "Rumble" animation's
 * content). The port's patch of optionsmenu.anm2 fills the Filter animation
 * with the Rumble animation's label and cursor of that language and the SFX
 * slider on layer 3, eleven frames (tools/port_patch.py, the registry's
 * optionsmenu.anm2 entry). Its OnOff layer is left without frames.
 *
 * DRAW, item 7, its own eight words at 0x6e3db8 (tools/into.py: nobody lands
 * inside them). The first five name the animation as before; the last three
 * hand the row to the common tail with the value field and the slider layer:
 *
 *     0x6e3dcc  ldr  x8, [x28]          ->  movz w9, #0x5eee   the rumble byte
 *     0x6e3dd0  movz w9, #0x5eed        ->  movz w1, #3        Adjustor1
 *     0x6e3dd4  b    0x6e3f88           ->  b    0x6e4034      the tail (row_tail)
 *
 * PRESS, tap index 5 (item 7 - 2), the Filter toggle's own eight words at
 * 0x6e3464 (into.py: none from outside). It becomes the slider's step - w20 is
 * the direction the list hands every row, -1 / +1 for left and right on a pad,
 * a keyboard or a sideways swipe, 0 for a confirm - clamped to 0..10 as an
 * unsigned compare, and it leaves through the same tail the SFX and MAP
 * OPACITY steps leave by (0x6e35f8: the menu's sound 0xED with x0 = the
 * manager, then the redraw):
 *
 *     ldr  x0, [x22]                    the manager
 *     add  x8, x0, #0x55, lsl #12
 *     ldrb w10, [x8, #0xeee]            the rumble byte, 0..10
 *     add  w10, w10, w20                one step
 *     cmp  w10, #10
 *     b.hi 0x6e35f8                     past either end: the value stays
 *     strb w10, [x8, #0xeee]
 *     b    0x6e35f8
 *
 * The byte is +0x55eee, options object +0x26: padding between the Filter byte
 * (+0x25) and the gamma float (+0x28). Nothing in the image builds 0x5eee or
 * reaches +0xeee over the manager's 0x55000, the options constructor
 * (0x7eb2b8) never stores there (its strh at +0x24 covers +0x24/+0x25 only),
 * and neither the reader (0x7eb3a0, 0x7eb758) nor the writer (0x7ebc90) of
 * options.ini touches it - checked over the whole __TEXT, 14.10.2026. So the
 * game neither loads nor saves it: sticks.c keeps it equal to rumble.c's
 * strength every frame the list is drawn, and takes a step the game made in
 * it to rumble.c (live) and to port-touch.ini.
 *
 * Encodings are the image's own forms: `ldr x0, [x22]` is 0x6e337c's word,
 * `add w10, w10, w20` the Touch Mode counter's original, `add #0x55, lsl #12`
 * from 0x6e3468, ldrb/strb as 0x6e3470 with rn x8 and the offset /1.
 */
#define RUMBLE_FIELD 0x5eee          /* options object +0x26: the settings block's 0x55eee */

static void rumble_menu_item(image_t *im) {
    enum { DRAW = 0x6e3db8, PRESS = 0x6e3464, SOUND_EXIT = 0x6e35f8 };
    static const uint32_t draw_was[3] = {0xF9400388u, 0x528BDDA9u, 0x1400006Du};
    static const uint32_t press_was[8] = {0xF94002C8u, 0x91415508u, 0x913B2100u, 0x39409408u,
                                          0x7100011Fu, 0x1A9F17E1u, 0x940423E4u, 0x14000077u};
    uint32_t draw_now[3] = {0x528BDDC9u /* movz w9, #0x5eee */, 0, 0};
    uint32_t press_now[8] = {0xF94002C0u, 0x91415408u, 0x397BB90Au, 0x0B14014Au,
                             0x7100295Fu, 0, 0x393BB90Au, 0};
    draw_now[1] = movz_w(1, 3);
    draw_now[2] = B_TO(DRAW + 28, ROW_TAIL);
    press_now[5] = 0x54000008u | ((uint32_t) (((SOUND_EXIT - (PRESS + 20)) / 4) & 0x7FFFF) << 5);  /* b.hi */
    press_now[7] = B_TO(PRESS + 28, SOUND_EXIT);

    uint32_t *head = (uint32_t *) (im->base + DRAW);
    uint32_t *draw = head + 5;
    uint32_t *press = (uint32_t *) (im->base + PRESS);
    const unsigned char *draw_tab = im->base + 0x956dda, *press_tab = im->base + 0x956dc8;

    /* the row is still the game's Filter row: named "Filter", in the tables at its own place */
    if (head[0] != 0xAA1603E0u || head[2] != 0x913D7421u || draw_tab[7] != 0x2F || press_tab[5] != 0x3A) {
        LOG("rumble row: the Filter row is not where it was expected (%08x %08x %02x %02x) - left out",
            head[0], head[2], draw_tab[7], press_tab[5]);
        return;
    }
    if (memcmp(draw, draw_was, sizeof(draw_was)) != 0 && memcmp(draw, draw_now, sizeof(draw_now)) != 0) {
        LOG("rumble row: %x holds %08x %08x %08x - left out", DRAW + 20, draw[0], draw[1], draw[2]);
        return;
    }
    if (memcmp(press, press_was, sizeof(press_was)) != 0 && memcmp(press, press_now, sizeof(press_now)) != 0) {
        LOG("rumble row: the Filter toggle at %x is not the one expected (%08x %08x) - left out",
            PRESS, press[0], press[2]);
        return;
    }
    /* the file the game loads: Adjustor1 is layer 3, and Filter carries the slider's eleven frames */
    if (shim_port_layer(OPTIONS_ANM2, "Adjustor1") != 3) {
        LOG("rumble row: Adjustor1 is not layer 3 of optionsmenu.anm2 - left out");
        return;
    }
    if (shim_port_frames(OPTIONS_ANM2, "Filter") < 11) {
        LOG("rumble row: the Filter animation of optionsmenu.anm2 is not the port's RUMBLE - left out");
        return;
    }
    if (!row_tail(im)) return;
    memcpy(draw, draw_now, sizeof(draw_now));
    memcpy(press, press_now, sizeof(press_now));
    LOG("menu: the FILTER row is the RUMBLE slider (byte +0x5%x)", RUMBLE_FIELD);
}

/*
 * THE FILTER IS GONE FOR GOOD (the owner, hotfix 1.75).
 *
 * The Filter is drawn only while three things hold (0x67183c..0x671858): the
 * config's EnableFilter (+0x55f21), the options byte Filter (+0x55eed) and a
 * flag of the run. The byte has exactly three writers in the Repentance copy,
 * and each is made a store of zero:
 *
 *   0x7eb840  strb w0, [x19, #0x25]   ->  strb wzr   options.ini's Filter= on load
 *   0x7ec40c  strb w1, [x0, #0x25]    ->  strb wzr   the setter; its one caller
 *                                                    was the row's toggle (0x6e347c),
 *                                                    which rumble_menu_item replaces
 *   (0x7eb2cc strh wzr, [x0, #0x24]   the constructor, zero already)
 *
 * so a saved Filter=1 reads as 0, the game writes Filter=0 back the next time
 * it saves options.ini, and nothing can turn it on. The three other engine
 * copies read their own Filter= the same way (0x3d9a0, 0x244d80, 0x4c0fb4,
 * strb w0, [x19, #0x3d]) and are given the same zero, so the change holds in
 * every copy. EnableFilter is left as the game sets it: with the byte at zero
 * nothing reads the surface it makes.
 */
static void filter_gone(image_t *im) {
    static const struct { unsigned long at; uint32_t was, now; } w[] = {
        { 0x7eb840, 0x39009660u, 0x3900967Fu },
        { 0x7ec40c, 0x39009401u, 0x3900941Fu },
        { 0x3d9a0,  0x3900F660u, 0x3900F67Fu },
        { 0x244d80, 0x3900F660u, 0x3900F67Fu },
        { 0x4c0fb4, 0x3900F660u, 0x3900F67Fu },
    };
    int done = 0;
    char left[160] = "";
    for (unsigned i = 0; i < sizeof(w) / sizeof(w[0]); i++) {
        uint32_t *p = (uint32_t *) (im->base + w[i].at);
        if (*p == w[i].now) { done++; continue; }
        if (*p != w[i].was) {
            size_t n = strlen(left);
            snprintf(left + n, sizeof(left) - n, " %lx=%08x", w[i].at, *p);
            continue;
        }
        *p = w[i].now;
        done++;
    }
    LOG("filter: off for good in %d of 5 places%s%s", done, left[0] ? ", NOT patched:" : "", left);
}

/*
 * SHOPIDLE PLAYS, AS IDLE DOES (the owner, 16.10.2026, for the modders: an
 * animated item's ShopIdle froze on its first frame, so animated-item mods lost
 * their animation in a shop).
 *
 * The Repentance copy picks a collectible's picture at 0x71854c: a shop item
 * (+0x3c == 100) gets "ShopIdle" by 0x89eab8 - SetFrame(name, 0), which names
 * the animation and STOPS it (strb wzr, [sprite, #0x54]) - and anything else
 * gets "Idle" by 0x89e82c, Play(name, 0). The PC game does the same (0x6e2b7e:
 * SetAnimation + SetFrame 0), so this is the game's own way and a choice of
 * the owner's to go past it. The call becomes Play, with the same w2 = 0 that
 * Idle's has: an animation already playing is left running, not restarted.
 * One word, the bl at 0x718568; tools/into.py 0x718550 0x718590: entries at
 * 0x718570, 0x718584, 0x71858c only.
 */
static void shop_idle_plays(image_t *im) {
    uint32_t *p = (uint32_t *) (im->base + 0x718568);
    const uint32_t set_frame = 0x94061954u, play = 0x940618B1u;   /* bl 0x89eab8 / bl 0x89e82c */
    if (*p == play) return;
    if (*p != set_frame) { LOG("shop idle: 718568 holds %08x - left alone", *p); return; }
    *p = play;
    LOG("shop idle: ShopIdle plays like Idle");
}

/*
 * A GLITCHED ITEM IN THE HISTORY CRASHED XVII - THE STARS? (the owner,
 * 21.10.2026: challenge #45 DELETE THIS starts with TMTRAINER; the reversed
 * Stars card then took the game down - reports/2026-10-22/tmtrainer). The
 * card looks for the oldest passive item by walking the player's collected
 * history (+0x24d0, entries 0x1c apart) and asks of each entry whether the
 * player still owns it: owned[id] (+0x1ab8). A TMTRAINER item has a NEGATIVE
 * id (-3 here), and the image loads it with ldr w23 - zero-extended - so the
 * read lands 16 GB past the array: SEGV at 0x5473c0. The same words are in the
 * iOS game.
 *
 * The game's own answer for such an id is a few functions away: removing a
 * history entry (0x5394c0) asks owned[] for a plain id and, for a negative one,
 * the player's glitched-item store at +0x2748 (0x5d169c). HasCollectible
 * (0x4eeb74) does both, and with "ignore modifiers" set its plain answer is
 * exactly owned[id] > 0 - this check. So the check calls it. The first try
 * (22.10) passed negative ids over: no crash, but a run of glitched items had
 * nothing the card could take, and it did nothing (the owner: on the PC it
 * throws the oldest item and makes two).
 *
 * There is no room for a call in the loop, so the four words call a stub laid
 * over a dead body - the cloud-save poll 0x8bbec0, whose only call (0x8e1230)
 * no_cloud_poll silences; nothing else branches to it or points at it (a scan
 * of every b/bl and every rebased pointer, 22.10.2026). The stub is laid only
 * when that call is the nop.
 *
 *   5473bc ldr x8, [x27, #0x1ab8]      ->  bl 0x8bbec0
 *   5473c0 ldr w8, [x8, x23, lsl #2]   ->  cbnz w0, 0x549328     (owned: the card takes it)
 *   5473c4 cmp w8, #1                  ->  b 0x5473cc            (not owned: the next entry)
 *   5473c8 b.ge 0x549328               ->  nop
 *   8bbec0: stp x29, x30, [sp, #-16]!; mov x29, sp; mov x0, x27; mov w1, w23; mov w2, #1;
 *           bl 0x4eeb74; ldp x29, x30, [sp], #16; ret
 *
 * The loop keeps its state in x19..x27 and reloads x8..x10 each round, as
 * around its two other calls. tools/into.py 0x547390 0x5473d0: the only entry
 * is 0x5473cc, past the four. Repentance alone has TMTRAINER.
 */
static void glitched_stars(image_t *im) {
    uint32_t *loop = (uint32_t *) (im->base + 0x5473bc);
    uint32_t *stub = (uint32_t *) (im->base + 0x8bbec0);
    const uint32_t *poll = (const uint32_t *) (im->base + 0x8e1230);
    static const uint32_t loop_was[4] = { 0xf94d5f68u, 0xb8777908u, 0x7100051fu, 0x5400fb0au };
    static const uint32_t loop_now[4] = { 0x940dd2c1u, 0x3500fb40u, 0x14000002u, 0xd503201fu };
    static const uint32_t stub_was[8] = { 0xd101c3ffu, 0x6d0323e9u, 0xa90457f6u, 0xa9054ff4u,
                                          0xa9067bfdu, 0x910183fdu, 0xaa0003e2u, 0xf0000ee8u };
    static const uint32_t stub_now[8] = { 0xa9bf7bfdu, 0x910003fdu, 0xaa1b03e0u, 0x2a1703e1u,
                                          0x52800022u, 0x97f0cb28u, 0xa8c17bfdu, 0xd65f03c0u };
    if (!memcmp(loop, loop_now, sizeof(loop_now)) && !memcmp(stub, stub_now, sizeof(stub_now))) return;
    if (*poll != 0xD503201Fu) { LOG("glitched stars: the cloud poll still runs - left alone"); return; }
    if (memcmp(loop, loop_was, sizeof(loop_was)) || memcmp(stub, stub_was, sizeof(stub_was))) {
        LOG("glitched stars: 5473bc holds %08x %08x %08x %08x, 8bbec0 %08x - left alone",
            loop[0], loop[1], loop[2], loop[3], stub[0]);
        return;
    }
    memcpy(stub, stub_now, sizeof(stub_now));
    memcpy(loop, loop_now, sizeof(loop_now));
    LOG("glitched stars: The Stars? asks HasCollectible, glitched items included");
}

/*
 * Switching the stick type on the fly.
 *
 * The same three words that static_sticks sets at startup: the two copies that
 * seat the stick under the finger, and the capture zone multiplier. They are
 * changed right in the game's code, so the menu item takes effect at once,
 * without a restart.
 */
unsigned long shim_image_base(void);

/*
 * Patching the image's code AFTER startup.
 *
 * Until now all our code patched the image at load time, while the segments
 * were still open. protect() closes __TEXT for writing — and those are the real
 * segment permissions, they must not be dropped for good. So the page has to be
 * opened for exactly the time of the write and closed again straight away.
 * Without this the menu item killed the game with SIGSEGV/SEGV_ACCERR on the
 * very first switch.
 *
 * The instruction cache is flushed without fail: otherwise the processor keeps
 * executing the old word for a while and the patch "works every other time".
 *
 * The word is checked before the write, as everywhere: only two values will do
 * — the one that was there and the one we are putting in.
 */
static int patch_word(unsigned long off, uint32_t was_a, uint32_t was_b, uint32_t now) {
    unsigned long base = shim_image_base();
    if (!base) return 0;
    uint32_t *p = (uint32_t *) (base + off);
    if (*p != was_a && *p != was_b) {
        LOG("image: %lx holds %08x — not what was expected, the patch is cancelled", off, *p);
        return 0;
    }
    if (*p == now) return 1;
    /*
     * The page here is the one the kernel counts in, and it is not a constant:
     * Android 15 brought phones with 16 KB pages, where mprotect turns down a
     * merely 4 KB-aligned address outright. Asked, not assumed.
     */
    long ps = sysconf(_SC_PAGESIZE);
    if (ps <= 0) ps = 4096;
    uint8_t *page = (uint8_t *) ((uintptr_t) p & ~(uintptr_t) (ps - 1));
    size_t len = (size_t) ((uint8_t *) (p + 1) - page);
    if (mprotect(page, len, PROT_READ | PROT_WRITE | PROT_EXEC) != 0 &&
        mprotect(page, len, PROT_READ | PROT_WRITE) != 0) {
        LOG("image: the page under %lx did not open for writing", off);
        return 0;
    }
    *p = now;
    __builtin___clear_cache((char *) p, (char *) (p + 1));
    mprotect(page, len, PROT_READ | PROT_EXEC);
    return 1;
}

/* the same single-word patch, for the other files of the layer */
int shim_patch_word(unsigned long off, uint32_t was_a, uint32_t was_b, uint32_t now) {
    return patch_word(off, was_a, was_b, now);
}

/*
 * The four words the stick kind is made of, and what each may already hold.
 *
 * Two writers use this table. At load the image is still open and the words go
 * in directly - that is what static_sticks did, and it matters: patch_word
 * closes the page again when it is done, and the hooks that go in right after
 * (0x862f04, 0x861ea0, 0x8620f0 and the rest sit on those very pages) would
 * then write into read-only memory and take the game down with them. On the fly,
 * from the menu, the image is long since closed and patch_word is the only way.
 */
typedef struct {
    unsigned long at;
    uint32_t may_a, may_b, may_c;      /* what is allowed to be there already */
    uint32_t as_static, as_dynamic, as_floating;
} stick_word_t;

static const stick_word_t STICK_WORDS[] = {
    /* the stick seats itself under the finger on the grab */
    {0x861030, 0x9401778Bu, 0xD503201Fu, 0xD503201Fu, 0xD503201Fu, 0x9401778Bu, 0x9401778Bu},
    {0x86107c, 0x94017778u, 0xD503201Fu, 0xD503201Fu, 0xD503201Fu, 0x94017778u, 0x94017778u},
    /*
     * The capture zone, as a multiple of the travel. Four travels for a stick
     * that stays put - you reach for it where it is - and an eighth of one for
     * a stick that is supposed to come to the finger. The game ships three,
     * which is sixty units, three times the ring's own width: that is what left
     * a dynamic stick standing where it last stood, with the finger out at the
     * rim.
     */
    {0x861b9c, 0x1E211002u, 0x1E221002u, 0x1E281002u, 0x1E221002u, 0x1E281002u, 0x1E281002u},
    /* the fork that drags the seat along once the finger passes the travel */
    {0x86300c, 0x540001ADu, 0x1400000Du, 0x1400000Du, 0x1400000Du, 0x1400000Du, 0x540001ADu},
};
#define STICK_WORDS_N (int) (sizeof(STICK_WORDS) / sizeof(STICK_WORDS[0]))

static uint32_t stick_word_for(const stick_word_t *w, int kind) {
    return kind == 0 ? w->as_static : (kind == 2 ? w->as_floating : w->as_dynamic);
}

/* at load: the image is open, so the words go in as they are */
static void sticks_kind_at_load(image_t *im, int kind) {
    for (int i = 0; i < STICK_WORDS_N; i++) {
        const stick_word_t *w = &STICK_WORDS[i];
        uint32_t *p = (uint32_t *) (im->base + w->at);
        if (*p != w->may_a && *p != w->may_b && *p != w->may_c) {
            LOG("sticks: %lx holds %08x — the kind is not applied", w->at, *p);
            return;
        }
    }
    for (int i = 0; i < STICK_WORDS_N; i++)
        *(uint32_t *) (im->base + STICK_WORDS[i].at) = stick_word_for(&STICK_WORDS[i], kind);
    LOG("sticks: kind %d applied at load", kind);
}

/*
 * The stick kind, on the fly: 0 static, 1 dynamic, 2 floating.
 *
 * Four words, and each one says a separate thing:
 *
 *   0x861030, 0x86107c  the stick seats itself under the finger on the grab —
 *                       silenced for static, kept for the other two;
 *   0x861b9c            the capture zone: by the drawn base for a static stick,
 *                       by the travel for one that comes to the finger;
 *   0x86300c            the fork that drags the seat along once the finger has
 *                       gone past the travel. THIS is the anchor: taking it out
 *                       is what keeps a grabbed stick where it was put, and
 *                       leaving it is what makes a stick float.
 */
void shim_sticks_apply_mode(int kind) {
    /*
     * The capture zone, and why a dynamic stick has almost none.
     *
     * Before a touch is allowed to seat a stick, the overlay asks whether it
     * landed near the one already on screen (0x861b8c: the distance from the
     * ring at +0x1b8 to the touch, against the travel at +0x1c0 times the
     * number below). Inside that circle the stick is taken hold of WHERE IT
     * STANDS - the grab point moves to the finger, the ring does not move at
     * all.
     *
     * For a stick that stays put that is exactly right: you reach for it where
     * it is. For a dynamic one it is the whole complaint - the game shipped
     * three TRAVELS, sixty units, three times the ring's own width, so putting
     * a finger anywhere near where the stick last stood left it standing there
     * with the finger out at the rim. "It appears already pushed to the edge",
     * and always in the same places, because those places are where it stood
     * before.
     *
     * So dynamic and floating get an eighth of a travel - two and a half units,
     * a dozen pixels. Not nought: the seating happens first and the very same
     * test then takes hold of the stick it has just placed, where the distance
     * is zero, so the circle has to exist. It just must not reach past the
     * finger that made it.
     */
                /* an unconditional branch over it  */
    for (int i = 0; i < STICK_WORDS_N; i++) {
        const stick_word_t *w = &STICK_WORDS[i];
        uint32_t want = stick_word_for(w, kind);
        if (!patch_word(w->at, w->may_a, w->may_b, want))
            patch_word(w->at, w->may_c, w->may_c, want);
    }
    LOG("sticks: type -> %s", kind == 0 ? "in place"
        : (kind == 2 ? "under the finger and floating" : "under the finger"));
}


/*
 * THE CAPTURE OF A STATIC D-PAD. The eighth round shrank it to a travel and a
 * quarter for BUTTONS ("pressed far beyond their size"), and the ninth broke
 * the D-pads with it: moving killed shooting, shooting answered only
 * downwards. The port's points stood a quarter travel inside that circle and
 * the game's circle and ours did not agree closely enough; the eighth round's
 * D-pads, on the stick's own four travels, worked. So the capture is the
 * stick's again, always, and what the drawing does not cover inside that
 * circle is refused by sticks.c itself (shim_sticks_dpad, answer 2) - the one
 * place that knows the drawing.
 */
#define CAPTURE_WORD      0x861b9c
#define FMOV_S2_1_25      0x1E2E9002u      /* fmov s2, #1.25 (checked with capstone) */

void shim_sticks_capture_for(int kind, int buttons) {
    unsigned long b = shim_image_base();
    if (!b) return;
    (void) buttons;                        /* the D-pad keeps the stick's capture (above) */
    uint32_t want = stick_word_for(&STICK_WORDS[2], kind);
    uint32_t now = *(const uint32_t *) (b + CAPTURE_WORD);
    if (now == want) return;
    if (now != STICK_WORDS[2].may_a && now != STICK_WORDS[2].may_b && now != STICK_WORDS[2].may_c
        && now != FMOV_S2_1_25) {
        LOG("sticks: %x holds %08x - the capture is not changed", CAPTURE_WORD, now);
        return;
    }
    patch_word(CAPTURE_WORD, now, now, want);
    LOG("sticks: capture %s", want == FMOV_S2_1_25 ? "by the D-pad" : "by the stick");
}

/*
 * The key-binding screen can be left with whatever is at hand.
 *
 * The screen is opened from the Controls list for the device that was picked,
 * and it asks the input manager for "confirm" and "back" from THAT device
 * alone - it is the only screen in the game that does. Every other one asks
 * with -1, meaning any device: that is how a menu stays reachable while a pad
 * is being plugged and unplugged.
 *
 * On a phone the difference is fatal. Pick anything but the pad and the screen
 * waits for a key we never deliver: it draws, it takes no input, and there is
 * no way back. The game is not hung - it is listening to a device that does
 * not exist here.
 *
 * So the way out is asked of any device, exactly as the parent screen asks it
 * (0x597b8c passes -1 for the same action). The confirm is left alone: binding
 * a key belongs to the device being bound, and a screen you can always leave
 * needs nothing more.
 */
/*
 * The Extra HUD row, forward only.
 *
 * The row is not a slider by mistake - it is a real three-state option (off /
 * normal / mini), and its press handler at 0x6e355c cycles the setting modulo
 * three: `add w10, w20, w10`, where w20 is the direction, +1 or -1. Walking
 * backwards is what makes it feel like a slider; our own rows - stick type,
 * stick size - only ever step forward and wrap. One word gives this row the
 * same manners and keeps all three states:
 *
 *     add w10, w20, w10   ->   add w10, w10, #1
 *
 * The encoding is checked against a sample from the image itself: `add w8,
 * w21, #1` at 0x6e36b8 is 0x110006A8, i.e. 0x11000000 | imm12<<10 | Rn<<5 |
 * Rd, so w10,w10,#1 is 0x1100054A.
 *
 * Written plainly, like every other edit made here, and NOT through
 * patch_word: at this point __TEXT is still open for writing, and patch_word
 * closes the page behind itself. The first attempt did exactly that and took
 * the whole page 0x6e3000 read-only - with the options-menu hook at 0x6e3a88
 * still to be installed on it. The game died in shim_trace_add on the write.
 */
static void extra_hud_one_way(image_t *im) {
    uint32_t *step = (uint32_t *) (im->base + 0x6e356c);
    if (*step != 0x0B0A028Au && *step != 0x1100054Au) {
        LOG("menu: %x holds %08x — the Extra HUD row is left alone", 0x6e356c, *step);
        return;
    }
    *step = 0x1100054Au;
    LOG("menu: the Extra HUD row steps forward only");
}

/*
 * MOVZ of the top half of a word: `movz w<rd>, #hi, lsl #16`.
 *
 * Every constant patched below is a float whose low sixteen bits are zero, and
 * the image loads exactly those with a single MOVZ - the form is taken from the
 * image's own words, e.g. 0x52B87E08 at 0x767f34 is movz w8,#0xc3f0,lsl#16 =
 * -480.0. So the encoding is 0x52A00000 | hi << 5 | rd, and a value whose low
 * half is not zero has no such form and must be refused rather than mangled.
 */
static uint32_t movz_hi(unsigned rd, float v) {
    uint32_t bits;
    memcpy(&bits, &v, sizeof(bits));
    if (bits & 0xFFFFu) return 0;
    return 0x52A00000u | ((bits >> 16) << 5) | (rd & 0x1Fu);
}

/*
 * A word that holds a float in a MOVZ, replaced by the same MOVZ with another
 * float. Checked before the write, and content to find the new value already
 * there - the same rule as everywhere else here.
 */
static int movz_word(image_t *im, const char *what, unsigned long off,
                     unsigned rd, float was, float now) {
    uint32_t before = movz_hi(rd, was), after = movz_hi(rd, now);
    if (!before || !after) {
        LOG("%s: %.1f or %.1f is not a single MOVZ - %lx left alone", what,
            (double) was, (double) now, off);
        return 0;
    }
    uint32_t *p = (uint32_t *) (im->base + off);
    if (*p != before && *p != after) {
        LOG("%s: %lx holds %08x, not %08x - left alone", what, off, *p, before);
        return 0;
    }
    *p = after;
    return 1;
}

/*
 * The bottom of the options paper, which does not fit in the frame.
 *
 * This one is ours and it predates the wide canvas. The options screen is a
 * 480x420 tile and the engine reveals its lower part by scrolling, with the
 * travel hard-coded to 160 units in four places: the menu road drives
 * anchorTable[10].y from -1080 down to -1240 (0x769eb8, 0x769f0c), and the
 * pause road drives the options object's own target y from 0 down to -160
 * (0x6e31c4, 0x6e2d98). Vanilla's paper ends at local y=416 - PaperBottom at
 * y=288, pivot 16, height 144 - so it needs 416-270 = 146 units and had 14 to
 * spare. The port added an eighteenth option row and moved PaperBottom to 308:
 * the paper now ends at 436 and needs 166. Six units short, and those six are
 * the strip the owner sees cut off, 32 pixels on this phone.
 *
 * The travel is 166: the paper's foot, the straight lower edge of PaperBottom's
 * shadow, stops exactly on the bottom of the frame (the owner, 23.09.2026 - it
 * used to be 176, and the ten units left bare wall under a cut-off shadow, the
 * same as vanilla's fourteen did). The pause road holds -166 in its MOVZ as it
 * is. The menu road cannot: -1080-166 = -1246 needs the low half of the float
 * too, and the immediate steps by 8 at that exponent (1240/1248). So its two
 * words become LDR w, literal - one instruction for one - and the literal
 * sits in the dead words of the Touch Mode branch (see options_bottom). The
 * paper's own backing, layer 0 of Paper13 in optionsmenu.anm2, is stretched
 * from 4200 to 4500, more than this travel needs.
 *
 * Nothing here has anything to do with the width: every term that reaches the
 * paper's Y is a constant or the canvas HEIGHT, which we never change.
 */
static float g_grid_now;            /* the column step in the image now, 0 if untouched */
static float g_rows_now;            /* and the row step */

#define OPTIONS_ROW   4.0f          /* the options tile sits four rows down */
#define OPTIONS_TRAVEL 166.0f       /* and scrolls this far: its foot on the frame's bottom */

/*
 * The menu road's bottom, a float that one MOVZ cannot hold.
 *
 * 0x6e4050 is the eighth of the ten words at 0x6e4034 that our patch of the
 * Touch Mode branch left dead (stick_menu_items puts its tail in the first
 * seven; the one entry from outside, 0x6e3e24, lands at 0x6e405c, past them).
 * It holds the float, and both words that used to MOVZ it load it instead:
 *
 *     00769eb8  mov w8, #-1240.0   ->  ldr w8, 0x6e4050   (then fmov s0, w8)
 *     00769f0c  mov w9, #-1240.0   ->  ldr w9, 0x6e4050   (then stp w8, w9)
 *
 * LDR (literal) reaches +-1 MB, and these are 0x85e68 and 0x85ebc away. The
 * encoding was checked against the image's own 0x903fbc, ldr w0, +0x3000.
 */
#define OPTIONS_BOTTOM_LIT 0x6e4050
#define OPTIONS_LIT_WAS    0x94076aacu   /* bl 0x8beb00, dead since the Touch Mode patch */

static uint32_t ldr_literal(unsigned rd, unsigned long at, unsigned long lit) {
    long d = ((long) lit - (long) at) / 4;
    return 0x18000000u | (((uint32_t) d & 0x7FFFFu) << 5) | (rd & 0x1Fu);
}

static int g_bottom_lit_ours;       /* the literal has been written by us */

static int options_bottom(image_t *im, float bottom) {
    static const struct { unsigned long at; unsigned rd; } SITES[] = {
        {0x769eb8, 8}, {0x769f0c, 9},
    };
    uint32_t *lit = (uint32_t *) (im->base + OPTIONS_BOTTOM_LIT);
    if (!g_bottom_lit_ours && *lit != OPTIONS_LIT_WAS) {
        LOG("options: %x holds %08x, not the dead call expected - the bottom left alone",
            OPTIONS_BOTTOM_LIT, *lit);
        return 0;
    }
    for (unsigned i = 0; i < 2; i++) {
        uint32_t *w = (uint32_t *) (im->base + SITES[i].at);
        uint32_t vanilla = movz_hi(SITES[i].rd, -1240.0f);
        if (*w != vanilla && *w != ldr_literal(SITES[i].rd, SITES[i].at, OPTIONS_BOTTOM_LIT)) {
            LOG("options: %lx holds %08x - the bottom left alone", SITES[i].at, *w);
            return 0;
        }
    }
    memcpy(lit, &bottom, sizeof(bottom));
    g_bottom_lit_ours = 1;
    for (unsigned i = 0; i < 2; i++)
        *(uint32_t *) (im->base + SITES[i].at) = ldr_literal(SITES[i].rd, SITES[i].at, OPTIONS_BOTTOM_LIT);
    return 2;
}

/*
 * Where the options tile is, and how far its camera may go.
 *
 * Two numbers, and they must agree: the tile's own anchor - four rows down,
 * -1080 while a row is 270 - and the bottom the camera is allowed to reach,
 * which is that anchor less the travel. They live in five words, three of the
 * anchor and two of the bottom, and until now the bottom was written on its own
 * for the eighteenth options row. That was fine while the rows never moved. It
 * stopped being fine the moment a tall canvas widened the row step: the tile
 * went down to four NEW rows and the camera kept its old bottom, so it hung far
 * above the paper - which is exactly what the owner saw at 4:3.
 *
 * So both come from the row step, and the step is 270 until something says
 * otherwise. The travel itself has nothing to do with the grid: it is the paper
 * being taller than the frame, and the port made it 16 units taller still by
 * adding an eighteenth row (see the note in the handover).
 */
static void options_anchor(image_t *im, float step) {
    float was_row = (g_rows_now > 0.0f) ? g_rows_now : 270.0f;
    float top = -OPTIONS_ROW * step, was_top = -OPTIONS_ROW * was_row;
    int done = 0;
    done += movz_word(im, "options", 0x769ea8, 8, was_top, top);
    done += movz_word(im, "options", 0x769ed4, 8, was_top, top);
    done += movz_word(im, "options", 0x769f10, 8, was_top, top);
    float bottom = top - OPTIONS_TRAVEL;
    done += options_bottom(im, bottom);
    LOG("options: the tile sits at %.0f and scrolls to %.0f (%d of 5 words)",
        (double) top, (double) bottom, done);
}

static void options_scroll(image_t *im) {
    int done = 0;
    done += movz_word(im, "options", 0x6e31c4, 8, -160.0f, -OPTIONS_TRAVEL);
    done += movz_word(im, "options", 0x6e2d98, 8, -160.0f, -OPTIONS_TRAVEL);
    options_anchor(im, 270.0f);
    LOG("options: the paper scrolls %.0f units instead of 160 (%d of 2 words)",
        (double) OPTIONS_TRAVEL, done);
}

/*
 * The neighbouring menu screens, showing at the edges of a wider canvas.
 *
 * The whole menu is one grid of tiles - three columns by eight rows - on a step
 * of exactly 480 by 270, held as a table of anchors in the menu object. The
 * camera is anchor[current] + halfExtra, and every screen draws at camera minus
 * its own anchor, so the ACTIVE screen always lands at halfExtra whatever the
 * step is, and a neighbour one column away lands at halfExtra +/- step. At the
 * design width halfExtra is nothing and a neighbour sits exactly off-screen -
 * which is why the renderer draws all fifteen screens unconditionally and never
 * needed a visibility flag. Widen the canvas and halfExtra grows, so a
 * neighbour's edge slides that far into the frame: 52.5 units on this phone,
 * and the owner saw three of them - the daily run beside the main menu, the
 * save files beside Challenges and beside Stats > Movies.
 *
 * The cure is the step itself. The leak stops as soon as halfExtra + step
 * reaches the canvas width, i.e. step >= (W + 480) / 2, and NOTHING else moves:
 * the active screen's place does not depend on the step at all, so every hit
 * test, label and button that works today keeps working. The columns are wider
 * apart and the inactive tiles go further out; that is the whole change.
 *
 * The step is computed from the display, not guessed: the same 270 * ratio the
 * canvas itself uses, rounded up to a multiple of four so it stays a single
 * MOVZ. On this phone that is 540. A canvas of exactly 480 patches nothing.
 *
 * Five words carry the step, one holds a screen's anchor as data, and three
 * more replace a load of the SHARED 480.0 at 0x9585a8 with the step inline -
 * shared, so it cannot simply be edited: the pause menu reads the same word and
 * must keep 480.
 */
static float grid_step(float units) {
    if (units > 900.0f) units = 900.0f;         /* the same ceiling the fit keeps */
    if (units <= 480.0f) return 0.0f;           /* the design canvas: nothing leaks */
    float need = (units + 480.0f) * 0.5f + 4.0f;    /* the bound, and a little over it */
    int step = (((int) need + 4) / 4) * 4;
    return (float) step;
}

/*
 * The step follows the CANVAS, and the canvas is only known once the fit has
 * measured the window. Taking it from the display's own metrics at load time
 * was wrong in principle and wrong in practice the moment the frame stopped
 * being the whole screen: a frame given another shape for testing, a window
 * that is not the display, a phone that reports one size and hands over
 * another. So screen.c calls this again with the width it actually computed,
 * and the words are re-written if that width says a different step.
 *
 * The previous value is remembered rather than re-derived, because a re-write
 * has to recognise what WE put there last time as well as the game's own.
 */
/*
 * The row step, worked out exactly as the column one and completely apart from
 * it: a canvas is either wider than the design or taller, never both, so the
 * two never move together. The leak stops when halfExtra + step reaches the
 * canvas, which for the rows is step >= (H + 270) / 2.
 */
static float row_step(float tall) {
    if (tall > 540.0f) tall = 540.0f;
    if (tall <= 270.0f) return 0.0f;
    float need = (tall + 270.0f) * 0.5f + 4.0f;
    int step = (((int) need + 4) / 4) * 4;
    return (float) step;
}

/*
 * A load of a constant from a data page, replaced by the value itself.
 *
 *     adrp x8, page ; ldr sD, [x8, #off]   ->   movz w8, #hi ; fmov sD, w8
 *
 * Needed because three of the row constants are read from elsewhere as well -
 * -810 has readers in the dead editions and two more live ones, -405 has a
 * second reader - so the DATA must not be edited. The scratch register is safe
 * to clobber: every one of these sites rebuilds x8 from x19 within three
 * instructions.
 */
static int inline_word(image_t *im, const char *what, unsigned long at,
                       uint32_t adrp_was, uint32_t ldr_was, unsigned sd, float now) {
    uint32_t *w = (uint32_t *) (im->base + at);
    uint32_t movz = movz_hi(8, now), fmov = 0x1E270100u | (sd & 31);
    if (!movz) {
        LOG("%s: %.1f is not a single MOVZ - %lx left alone", what, (double) now, at);
        return 0;
    }
    int fresh = (w[0] == adrp_was && w[1] == ldr_was);
    int again = ((w[0] & 0xFF800000u) == 0x52800000u && w[1] == fmov);
    if (!fresh && !again) {
        LOG("%s: %lx holds %08x %08x - left alone", what, at, w[0], w[1]);
        return 0;
    }
    w[0] = movz;
    w[1] = fmov;
    return 1;
}

static void menu_grid_apply(image_t *im, float units) {
    float s = grid_step(units);
    if (s <= 0.0f || s == g_grid_now) return;
    float was = (g_grid_now > 0.0f) ? g_grid_now : 480.0f;

    int done = 0;
    done += movz_word(im, "menu grid", 0x767f34, 8, -was, -s);           /* column -1 */
    done += movz_word(im, "menu grid", 0x767f8c, 8, -2.0f * was, -2.0f * s); /* column -2 */
    done += movz_word(im, "menu grid", 0x767fb4, 8,  was,  s);           /* column +1 */
    done += movz_word(im, "menu grid", 0x769fbc, 8, -was, -s);           /* character page two */
    done += movz_word(im, "menu grid", 0x76a000, 8, -2.0f * was, -2.0f * s); /* screen 6 */

    /*
     * The one anchor kept as data rather than in an instruction. It is 950 and
     * not 960 - ten units short of two full columns, and that is the game's
     * own, whatever the reason - so it is moved by two steps and keeps its
     * oddity instead of being rounded away.
     */
    float *anchor15 = (float *) (im->base + 0x94f4e0);
    float moved = 950.0f + 2.0f * (s - 480.0f);
    float before = 950.0f + 2.0f * (was - 480.0f);
    if (*anchor15 == before || *anchor15 == moved) { *anchor15 = moved; done++; }
    else LOG("menu grid: 94f4e0 holds %.1f, not %.1f - left alone",
             (double) *anchor15, (double) before);

    /*
     * The character screen's second paper takes its column from the SHARED
     * 480.0 at 0x9585a8, which the pause menu reads too. The load is replaced
     * by the step in an immediate, at this one site:
     *
     *     adrp x8, 0x958000 ; add x8, x8, #0x5a8 ; ldr s8, [x8]
     *  -> movz w8, #step    ; fmov s8, w8       ; nop
     */
    uint32_t *trio = (uint32_t *) (im->base + 0x5e8388);
    uint32_t step_word = movz_hi(8, s);
    if (step_word &&
        ((trio[0] == 0x90001B88u && trio[1] == 0x9116A108u && trio[2] == 0xBD400108u) ||
         (trio[1] == 0x1E270108u && trio[2] == 0xD503201Fu))) {
        trio[0] = step_word;
        trio[1] = 0x1E270108u;                  /* fmov s8, w8 */
        trio[2] = 0xD503201Fu;                  /* nop */
        done++;
    } else {
        LOG("menu grid: 5e8388 holds %08x %08x %08x - left alone",
            trio[0], trio[1], trio[2]);
    }

    g_grid_now = s;
    LOG("menu grid: the column step is %.0f instead of 480 (%d of 7 places)",
        (double) s, done);
}

/*
 * The nearest value a single MOVZ can hold. The immediate carries the top
 * sixteen bits of the float, so the low half is rounded away rather than cut -
 * a few units either way on a row anchor, and nothing at all on the round ones.
 */
static float movz_round(float v) {
    uint32_t b;
    memcpy(&b, &v, sizeof(b));
    b = (b + 0x8000u) & 0xFFFF0000u;
    float r;
    memcpy(&r, &b, sizeof(r));
    return r;
}

/*
 * Every Y the grid is built from, scaled by the new row step.
 *
 * Listing them one by one and moving only the ones that looked like whole rows
 * was the mistake that showed at 4:3: the options screen moved and the devices
 * screen under it did not, so the two overlapped and the paper was cut. The
 * grid has anchors that are not whole multiples of 270 - a half row at -405, a
 * pair nudged by a few units at -1917 and -1670 - and each of those belongs to
 * a screen just as much as the round ones do.
 *
 * So the rule is one rule for all of them: every Y is multiplied by
 * step / 270. Relative places are kept, nothing can be forgotten, and the odd
 * offsets ride along.
 *
 * Three of them are read from data pages that other code reads too, so those
 * are turned into immediates in place rather than edited in the data.
 */
typedef struct {
    unsigned long at;       /* the movz, or the adrp of an adrp+ldr pair */
    float was;              /* the value the game shipped */
    unsigned sd;            /* for a pair: the register the ldr filled */
    uint32_t adrp, ldr;     /* for a pair: the two words as shipped */
} row_site_t;

static const row_site_t ROWS[] = {
    {0x767edc,  -270.0f, 0, 0, 0},
    {0x767f08,  -540.0f, 0, 0, 0},
    {0x767fe4, -1080.0f, 0, 0, 0},
    {0x768018,  -290.0f, 0, 0, 0},
    {0x767f60,  -810.0f, 10, 0xF0000EE8u, 0xBD4E590Au},
    {0x767fbc,  -405.0f,  1, 0x90000F48u, 0xBD44D901u},
    {0x76803c, -1350.0f,  1, 0x90000F88u, 0xBD457101u},
    {0x7680ac, -1620.0f,  1, 0xD0000EE8u, 0xBD4E5D01u},
    {0x7680d4, -1917.0f,  1, 0xF0000F28u, 0xBD44DD01u},
    {0x7680fc, -1670.0f,  1, 0x90000F88u, 0xBD457501u},
    {0x76a008,  -810.0f,  1, 0x90000EE8u, 0xBD4E5901u},
};

static void menu_rows_apply(image_t *im, float tall) {
    float s = row_step(tall);
    if (s <= 0.0f || s == g_rows_now) return;
    float now = s / 270.0f;
    float before = ((g_rows_now > 0.0f) ? g_rows_now : 270.0f) / 270.0f;

    int done = 0;
    for (unsigned i = 0; i < sizeof(ROWS) / sizeof(ROWS[0]); i++) {
        const row_site_t *r = &ROWS[i];
        float to = movz_round(r->was * now), from = movz_round(r->was * before);
        if (r->adrp) done += inline_word(im, "menu rows", r->at, r->adrp, r->ldr, r->sd, to);
        else         done += movz_word(im, "menu rows", r->at, 8, from, to);
    }
    options_anchor(im, s);
    g_rows_now = s;
    LOG("menu rows: the row step is %.0f instead of 270 (%d of %d places)",
        (double) s, done, (int) (sizeof(ROWS) / sizeof(ROWS[0])));
}

/*
 * Called by screen.c once the fit knows the canvas. The image is closed for
 * writing by then, so each word goes through patch_word, which opens its page
 * for exactly the length of the write.
 */
void shim_menu_grid(float units, float tall) {
    image_t im;
    memset(&im, 0, sizeof(im));
    im.base = (uint8_t *) (uintptr_t) shim_image_base();
    if (!im.base) return;
    float s = grid_step(units), r = row_step(tall);
    if ((s <= 0.0f || s == g_grid_now) && (r <= 0.0f || r == g_rows_now)) return;
    /*
     * menu_grid_apply writes plainly, and by now protect() has closed the image
     * for writing, so the pages it touches are opened for exactly the length of
     * the work and closed again. Asking patch_word to do the opening does not
     * work: it returns early when the word already holds what is being written,
     * and then nothing is open at all - which is how this crashed the first
     * time, with SEGV_ACCERR on the very first store.
     *
     * The page size is asked for, not assumed: Android 15 phones have 16 KB
     * pages and mprotect refuses a merely 4 KB-aligned address there.
     */
    static const struct { unsigned long at; int code; } touched[] = {
        {0x767edc, 1}, {0x767f08, 1}, {0x767f34, 1}, {0x767f60, 1}, {0x767f8c, 1},
        {0x767fb4, 1}, {0x767fbc, 1}, {0x767fe4, 1}, {0x76803c, 1},
        {0x7680ac, 1}, {0x7680d4, 1}, {0x7680fc, 1}, {0x768018, 1},
        {0x769ea8, 1}, {0x769eb8, 1}, {0x769ed4, 1}, {0x769f0c, 1}, {0x769f10, 1},
        {0x769fbc, 1}, {0x76a000, 1}, {0x76a008, 1}, {0x5e8388, 1}, {0x5e8390, 1},
        {OPTIONS_BOTTOM_LIT, 1},
        {0x94f4e0, 0},
    };
    long ps = sysconf(_SC_PAGESIZE);
    if (ps <= 0) ps = 4096;
    unsigned n = sizeof(touched) / sizeof(touched[0]);
    for (unsigned i = 0; i < n; i++) {
        uint8_t *page = (uint8_t *) ((uintptr_t) (im.base + touched[i].at) & ~(uintptr_t) (ps - 1));
        if (mprotect(page, (size_t) ps, PROT_READ | PROT_WRITE | PROT_EXEC) != 0 &&
            mprotect(page, (size_t) ps, PROT_READ | PROT_WRITE) != 0) {
            LOG("menu grid: the page under %lx did not open — the step stays", touched[i].at);
            return;
        }
    }
    menu_grid_apply(&im, units);
    menu_rows_apply(&im, tall);
    for (unsigned i = 0; i < n; i++) {
        uint8_t *page = (uint8_t *) ((uintptr_t) (im.base + touched[i].at) & ~(uintptr_t) (ps - 1));
        mprotect(page, (size_t) ps, touched[i].code ? (PROT_READ | PROT_EXEC) : PROT_READ);
    }
    __builtin___clear_cache((char *) (im.base + 0x5e8388), (char *) (im.base + 0x76a010));
}

/*
 * Colours brighter than white, which the engine threw away.
 *
 * Items tint a tear by MULTIPLYING its colour: Iron Bar by 0.5 on every
 * channel, Serpent's Kiss by 0.5 on red and blue, Stye by (1.5, 2, 2), Holy
 * Light and Spider Bite by 2. The ones that lower a channel are visible in the
 * game; the ones that only raise them do nothing at all, and that is measured,
 * not guessed: with Stye's branch forced open and its red taken to eight, the
 * highest colour channel reaching the card was still exactly 1.000.
 *
 * The engine clamps. 0x8a33d8 normalises a colour and, for the RGB modes (the
 * word at +0x10 below two), sends each of red, green and blue through the
 * clamp at 0x8ae72c - `fmaxnm` against zero and `fminnm` against one. That
 * clamp is a general-purpose one with 429 callers and must not be touched, but
 * the normaliser has only four, and all four are colour constructors.
 *
 * So the three calls become the plain load they would otherwise return:
 *
 *     mov x0, x19 ; bl clamp01   ->   mov x0, x19 ; ldr s0, [x0]
 *
 * The value passes through untouched, the HSV branch below is left alone, and
 * a multiplier above one survives to the shader - which never clamped it: the
 * game's own coloroffset.vs hands Color0 over as it is and coloroffset.fs
 * multiplies the texture by it, with the vertex attribute a plain float.
 *
 * This is a deliberate DEPARTURE from the original, asked for by the owner: the
 * same clamp runs on iOS, so brightening items do nothing there either. The
 * lower bound goes with it - one word cannot both clamp and load - and nothing
 * in the game asks for a negative multiplier.
 */
static void colours_above_white(image_t *im) {
    static const unsigned long at[] = {0x8a33fc, 0x8a3408, 0x8a3414};
    static const uint32_t was[] = {0x94002CCCu, 0x94002CC9u, 0x94002CC6u};
    for (unsigned i = 0; i < sizeof(at) / sizeof(at[0]); i++) {
        uint32_t *w = (uint32_t *) (im->base + at[i]);
        if (*w != was[i] && *w != 0xBD400000u) {
            LOG("colour: %lx holds %08x — the clamp is left alone", at[i], *w);
            return;
        }
    }
    for (unsigned i = 0; i < sizeof(at) / sizeof(at[0]); i++)
        *(uint32_t *) (im->base + at[i]) = 0xBD400000u;      /* ldr s0, [x0] */
    LOG("colour: a tint brighter than white is kept, not clamped");
}

static void leave_binding_screen(image_t *im) {
    uint32_t *back = (uint32_t *) (im->base + 0x7d3c44);   /* mov x2, x21 */
    if (*back != 0xAA1503E2u && *back != 0x12800002u) {
        LOG("controls: %x holds %08x — the way out is left alone", 0x7d3c44, *back);
        return;
    }
    *back = 0x12800002u;                                   /* mov w2, #-1 — any device */
    LOG("controls: the key-binding screen answers to any device to go back");
}

/*
 * Filling the screen: where it is done.
 *
 * The game lays everything out on a canvas of 480 by 270 units and fits that
 * into the window, centring what is left over - the black bars. That is no
 * longer what happens: the CANVAS itself is widened to 270 * the window's
 * ratio, 585 units on this phone, so a unit stays square and the game lays out
 * on more of them. Nothing is stretched and nothing is scaled.
 *
 * It is done in screen.c, at the fit's own entry (0x670a00, the live copy of
 * four), because the fit reads the canvas object 0xb10d30 with its first
 * instruction - the last moment the width can be set and the only one where
 * the window size is known. The rest of that file is what did not follow by
 * itself: the hit tests that were handed no origin because at 480 the origin
 * is zero. Two things are patched here instead, because they must be in place
 * before the menu is ever built: menu_grid, the tile step, and options_scroll.
 *
 * An earlier attempt at this is worth not repeating: making the FIT take the
 * screen's ratio instead of the canvas's also removes the bars, but a unit
 * becomes 6.5 across and 5.33 down, the picture stretches, and the game hangs
 * after the intro.
 */
int shim_sticks_floating(void);

/*
 * THE TOUCH DEVICE IS LISTED IN CONTROLS.
 *
 * The Controls screen builds its list of devices every frame (0x5979a0) and
 * passes over the touch one on purpose:
 *
 *     00597a34  cmp  w0, #0              is the device connected
 *     00597a38  ccmp w24, w27, #4, ne    ... and is it 0x10DEAD
 *     00597a3c  b.eq #0x597ae8           then skip it
 *
 * With the second word taken out the flags are the connection test alone, so
 * every connected device is listed and the touch one takes a row like the
 * rest. The row is the way into the controls editor (touchedit.c); the game
 * draws, scrolls and confirms it by itself.
 */
static void touch_in_controls(image_t *im) {
    uint32_t *p = (uint32_t *) (im->base + 0x597a38);
    if (*p != 0x7a5b1304u) {
        LOG("controls: %08x at 0x597a38 is not the skip that was expected", *p);
        return;
    }
    *p = 0xd503201fu;                   /* nop */
    LOG("controls: the touch device is listed like the others");
}

static void fixed_sticks(image_t *im) {
    /* the address of "add x1, sp, #8" in the capture, the offset of the stick's
       place, the address of the "drag if we went past the radius" fork */
    int floats = shim_sticks_floating();
    static const struct { unsigned long anchor, guard; unsigned base; const char *who; } sticks[] = {
        {0x06abcc, 0x06c5ac, 0x570, "Rebirth"},
        {0x24e4fc, 0x25004c, 0x568, "Afterbirth"},
        {0x861bc0, 0x86300c, 0x1b8, "Repentance"},
    };
    for (unsigned i = 0; i < sizeof(sticks) / sizeof(sticks[0]); i++) {
        uint32_t *a = (uint32_t *) (im->base + sticks[i].anchor);
        uint32_t *g = (uint32_t *) (im->base + sticks[i].guard);
        if (*a != 0x910023E1u || *g != 0x540001ADu) {
            LOG("sticks (%s): the code is not the one expected (%08x %08x)", sticks[i].who, *a, *g);
            continue;
        }
        /* add x1, x19, #base — instead of the touch point */
        *a = 0x91000000u | ((uint32_t) sticks[i].base << 10) | (19u << 5) | 1u;
        /*
         * The dragging is what makes a stick float. Only the anchored kinds
         * skip it; the floating one keeps the game's own fork.
         */
        if (!floats) *g = 0x1400000Du;      /* an unconditional branch over the dragging */
        LOG("sticks (%s): capture by the stick's own place, dragging %s",
            sticks[i].who, floats ? "left as the game has it" : "taken out");
    }
}

/*
 * THE STICK'S TWO NUMBERS GO THROUGH THE PORT (plan item 4.6, batch 3; the
 * owner's decisions 18 and 20). Two calls in the Repentance stick's update
 * (0x862f04) become calls of ours - one `bl` for another, the call stays a
 * call, the arguments and the s0/s1 answer stay where the game had them:
 *
 *   0x863048  bl 0x8bedbc (deflection / +0x1c4)  -> bl 0x8dab78 -> shim_stick_value:
 *             the dead zone, MOVE SENS and DIGITAL's threshold on the OUTPUT,
 *             instead of holding the finger back (sticks.c, stickcurve.h);
 *   0x863388  bl 0x8beb28 (value x travel)       -> bl 0x8dab68 -> shim_stick_knob:
 *             the knob's target is the finger itself, cut at the travel.
 *
 * The trampolines are shim_hook's four words in the purchases check's dead
 * body (see game_log_on; own_all_dlc has made 0x8dab10 `mov w0, #1; ret` and
 * into.py counts no entry into 0x8dab68..0x8dab87 at all). The slots were
 * given out in advance: 58 the spawner's mark, 68 and 78 the sticks, 88 the
 * sound cache. Each word is checked before it is written, and each branch was
 * checked with capstone: 0x9401DECC is `bl 0x8dab78` at 0x863048, 0x9401DDF8
 * `bl 0x8dab68` at 0x863388. The Rebirth and AB copies keep their sticks in
 * other fields (+0x57c/+0x574) and never run; like STICK_WORDS, this is the
 * Repentance copy only. Not a five-word hook: neither word is in a jump
 * table's block, and 0x863380, the nearest branch target, is left alone.
 */
#define STICK_VALUE_CALL  0x863048
#define STICK_KNOB_CALL   0x863388
#define KNOB_TRAMPOLINE   0x8dab68
#define VALUE_TRAMPOLINE  0x8dab78
#include "stickcurve.h"
stick_v2 shim_stick_value(const float *d, const float *div);      /* sticks.c */
stick_v2 shim_stick_knob(const float *value, const float *travel); /* sticks.c */

static void stick_hooks(image_t *im) {
    const uint32_t *dlc = (const uint32_t *) (im->base + 0x8dab10);
    uint32_t *value = (uint32_t *) (im->base + STICK_VALUE_CALL);
    uint32_t *knob = (uint32_t *) (im->base + STICK_KNOB_CALL);
    uint32_t *kt = (uint32_t *) (im->base + KNOB_TRAMPOLINE);
    uint32_t *vt = (uint32_t *) (im->base + VALUE_TRAMPOLINE);
    /* the body is dead only while the check answers at once */
    if (dlc[0] != 0x52800020u || dlc[1] != 0xD65F03C0u) {
        LOG("sticks: 8dab10 holds %08x %08x, not the answered purchases check - the stick stays the game's",
            dlc[0], dlc[1]);
        DIAG(report_line("sticks", "the stick stays the game's: the purchases check at 8dab10 is not answered"));
        return;
    }
    if (kt[0] == 0xAA1403E0u && kt[1] == 0x940097DFu && kt[2] == 0xAA1303E0u && kt[3] == 0x940097DDu
        && *knob == 0x94016DE8u) {                  /* bl 0x8beb28 */
        shim_hook(kt, (void *) shim_stick_knob);
        *knob = 0x9401DDF8u;                        /* bl 0x8dab68 */
        LOG("sticks: the knob follows the finger (0x863388 -> 8dab68)");
        DIAG(report_line("sticks", "the knob follows the finger (0x863388 -> 8dab68)"));
    } else {
        LOG("sticks: the knob stays the game's (%08x at 863388; %08x %08x %08x %08x at 8dab68)",
            *knob, kt[0], kt[1], kt[2], kt[3]);
#if ISAAC_DIAG
        char line[160];
        snprintf(line, sizeof(line), "the knob stays the game's: %08x at 863388; %08x %08x %08x %08x at 8dab68",
                 *knob, kt[0], kt[1], kt[2], kt[3]);
        report_line("sticks", line);
#endif
    }
    if (vt[0] == 0xAA1503E0u && vt[1] == 0xA9427BFDu && vt[2] == 0xA9414FF4u && vt[3] == 0xA8C357F6u
        && *value == 0x94016F5Du) {                 /* bl 0x8bedbc */
        shim_hook(vt, (void *) shim_stick_value);
        *value = 0x9401DECCu;                       /* bl 0x8dab78 */
        LOG("sticks: the value goes through the port (0x863048 -> 8dab78)");
        DIAG(report_line("sticks", "the value goes through the port (0x863048 -> 8dab78)"));
    } else {
        LOG("sticks: the value stays the game's (%08x at 863048; %08x %08x %08x %08x at 8dab78)",
            *value, vt[0], vt[1], vt[2], vt[3]);
#if ISAAC_DIAG
        char line[160];
        snprintf(line, sizeof(line), "the value stays the game's: %08x at 863048; %08x %08x %08x %08x at 8dab78",
                 *value, vt[0], vt[1], vt[2], vt[3]);
        report_line("sticks", line);
#endif
    }
}

/*
 * THE TOUCH CONTROLS' OPACITY, ONE NUMBER FOR ALL OF THEM (hotfix 1.71, the
 * owner's slider in the touch editor, 05.10.2026).
 *
 * Measured in the image: every piece of the touch overlay is drawn at one
 * opacity, a half, and the half is an immediate in three draws -
 *
 *   0x861f1c  a stick and a D-pad (0x861ea0): fmov s9, #0.5; s9 makes the
 *             scale (0.5 / [0xa9c7c8], at 0x861f20) AND both alphas (0x861f60,
 *             0x861fec: the widget's +0x328 times s9; the ring's own
 *             AlphaTint 100 comes on top, inside the .anm2)
 *   0x863d18  a Button - pause, the twins' and the Forgotten's round ones
 *   0x863d30  (0x863ccc): s8, the alpha of the target it draws
 *   0x864304  an ItemButton - the row's actives, card, pill, bomb
 *   0x86431c  (0x8642c0): the same two places
 *
 * A float like 0.35 has no fmov immediate, so each becomes `ldr sN, literal`
 * of one word the port owns: 0x8dab98, the last two words of the purchases
 * check's dead body (own_all_dlc answers at 0x8dab10, and into.py finds
 * nothing entering 0x8dab98..0x8dabac but the body's own fall-through and its
 * landing pads, dead with it; the trampolines end at 0x8dab97). The literal
 * is in reach of all five (the farthest, 0x861f18, is 0x78c80 away; ldr
 * literal reaches 1 MB).
 *
 *   The stick: s9 must stay a half for the scale, so the four words before it
 *   are re-laid with one to spare - `add x8, x8, #0x7c8; ldr s2, [x8]` is one
 *   `ldr s2, [x8, #0x7c8]` - and s3, dead there (the next write is 0x861f7c),
 *   takes the half for the scale while s9 takes ours.
 *   The Buttons: s0 is also the pressed shade (fcsel s9, s1, s0 at 0x863d28 /
 *   0x864314), so it keeps its half; the pressed alpha came from s1/s0 by the
 *   byte +0x18 / +0x1b0 the draw already branched on being set (0x863cfc,
 *   0x8642e8), so it was always s0 - ours now, through s1.
 *
 * All or nothing: every word is checked before any is written, and the
 * literal goes in before the code that reads it. Repentance's copy only - the
 * touch overlay is this one (stick_hooks above says the same of the sticks).
 */
#define ALPHA_LITERAL 0x8dab98
#define ALPHA_HALF    0x3F000000u

static const struct { unsigned long at; uint32_t was, now; } ALPHA_WORDS[] = {
    { 0x861f10, 0x911F2108u, 0x1E221821u },     /* add x8, x8, #0x7c8   -> fdiv s1, s1, s2 */
    { 0x861f14, 0x1E221821u, 0xBD47C902u },     /* fdiv s1, s1, s2      -> ldr s2, [x8, #0x7c8] */
    { 0x861f18, 0xBD400102u, 0x1C3C6409u },     /* ldr s2, [x8]         -> ldr s9, 0x8dab98 */
    { 0x861f1c, 0x1E2C1009u, 0x1E2C1003u },     /* fmov s9, #0.5        -> fmov s3, #0.5 */
    { 0x861f20, 0x1E221922u, 0x1E221862u },     /* fdiv s2, s9, s2      -> fdiv s2, s3, s2 */
    { 0x863d18, 0x1E2A1001u, 0x1C3B7401u },     /* fmov s1, #0.25       -> ldr s1, 0x8dab98 */
    { 0x863d1c, 0x1E200C28u, 0x1E204028u },     /* fcsel s8, s1, s0, eq -> fmov s8, s1 */
    { 0x863d30, 0x1E2C1008u, 0x1C3B7348u },     /* fmov s8, #0.5        -> ldr s8, 0x8dab98 */
    { 0x864304, 0x1E2A1001u, 0x1C3B44A1u },     /* fmov s1, #0.25       -> ldr s1, 0x8dab98 */
    { 0x864308, 0x1E200C28u, 0x1E204028u },     /* fcsel s8, s1, s0, eq -> fmov s8, s1 */
    { 0x86431c, 0x1E2C1008u, 0x1C3B43E8u },     /* fmov s8, #0.5        -> ldr s8, 0x8dab98 */
};
#define ALPHA_DEAD_WORD 0x14000002u             /* b 0x8daba0, what the literal's word holds */

static int g_alpha_ours;                        /* the five draws read the literal */

float shim_sticks_alpha(void);                  /* sticks.c: the setting, 0.10..1 */

static uint32_t alpha_bits(float a) {
    uint32_t u;
    memcpy(&u, &a, 4);
    return u;
}

static void touch_alpha_words(image_t *im) {
    const uint32_t *dlc = (const uint32_t *) (im->base + 0x8dab10);
    uint32_t *lit = (uint32_t *) (im->base + ALPHA_LITERAL);
    if (dlc[0] != 0x52800020u || dlc[1] != 0xD65F03C0u || *lit != ALPHA_DEAD_WORD) {
        LOG("touch alpha: %08x %08x at 8dab10, %08x at 8dab98 - the game's half stays",
            dlc[0], dlc[1], *lit);
        DIAG(report_line("touch alpha", "the game's half stays: 8dab98 is not the dead word"));
        return;
    }
    for (unsigned i = 0; i < sizeof(ALPHA_WORDS) / sizeof(ALPHA_WORDS[0]); i++) {
        const uint32_t *w = (const uint32_t *) (im->base + ALPHA_WORDS[i].at);
        if (*w != ALPHA_WORDS[i].was) {
            LOG("touch alpha: %lx holds %08x, not %08x - the game's half stays",
                ALPHA_WORDS[i].at, *w, ALPHA_WORDS[i].was);
#if ISAAC_DIAG
            char line[120];
            snprintf(line, sizeof(line), "the game's half stays: %lx holds %08x, not %08x",
                     ALPHA_WORDS[i].at, *w, ALPHA_WORDS[i].was);
            report_line("touch alpha", line);
#endif
            return;
        }
    }
    *lit = alpha_bits(shim_sticks_alpha());
    for (unsigned i = 0; i < sizeof(ALPHA_WORDS) / sizeof(ALPHA_WORDS[0]); i++)
        *(uint32_t *) (im->base + ALPHA_WORDS[i].at) = ALPHA_WORDS[i].now;
    g_alpha_ours = 1;
    LOG("touch alpha: the overlay reads its opacity from 8dab98 (%.2f)", (double) shim_sticks_alpha());
    DIAG(report_line("touch alpha", "the overlay reads its opacity from 8dab98"));
}

/* the setting changed (sticks.c): the five draws see it on their next frame */
int shim_touch_alpha_apply(float a) {
    if (!g_alpha_ours) return 0;
    unsigned long b = shim_image_base();
    if (!b) return 0;
    uint32_t was = *(const uint32_t *) (b + ALPHA_LITERAL);
    return patch_word(ALPHA_LITERAL, was, was, alpha_bits(a));
}

/* what the overlay draws at now: ours once the words are in, the game's half otherwise */
float shim_touch_alpha_live(void) {
    return g_alpha_ours ? shim_sticks_alpha() : 0.5f;
}

/*
 * THE POPUP PAPER ON WHOLE UNITS (06.10.2026; tools/popupedges.py).
 *
 * Every dialog of the game's dialog manager - CONTROLLER_DISCONNECTED and
 * RESELECT_CONTROLLER (0x67422c), and the spawner's card - is drawn by
 * 0x7fa928, whose paper (0x7fac64) is built from gfx/ui/Popup.png: a fill over
 * the rectangle, a 98 x 7 strip of the sheet turned by -90 degrees down each
 * side, strips along the top and the bottom, four corners. Where it stands is
 * 0x7fb3fc's answer, for the paper and for the text on it alike:
 *
 *     centre = canvas / 2 + slide       size = floor(squash * size)
 *
 * The interface is drawn one pixel to the unit, so when the canvas and the
 * size differ in parity a side lands on x.5, exactly on a row of pixel
 * centres. The rasteriser then gives that row to whichever piece its tie rule
 * picks, and when that is a side strip it samples exactly at its last texel's
 * far boundary: the transparent separator row of the sheet, and a one-unit
 * hole the whole length of the side (the owner's screenshot: the spawner's
 * card, 420 wide on a 585 canvas, column 82). Drawn at an even number of
 * pixels to the unit, an x.5 side would fall between pixel centres; at one
 * pixel to the unit it falls on them.
 *
 * Which dialogs and canvases it hits: the card on every canvas of odd width
 * (555, 585, 631), the controller popups on every canvas - their size is their
 * text's, so half of all texts and languages land on a half unit, FULLSCREEN
 * off included - and every dialog for frames of the opening squash, where the
 * size passes through both parities. Measured on the desk, every piece with
 * the image's own float32 steps: tools/popupedges.py.
 *
 * So the answer is made whole where it is made, once for all of them: the
 * size as the game computes it, and the centre moved by under a unit so that
 * the paper's top-left corner is floor(centre - size / 2). Size whole, corner
 * whole - every side, every strip and every corner on whole units, every
 * sample in the middle of a texel. The spawner lays its own content out with
 * the same floor (spawner.c, shim_spawner_draw), so text and paper stay
 * together. The slide is not disturbed: the rasteriser already put the paper
 * on whole pixels each frame, rounding; this floors instead - the same steps,
 * at most one unit apart on a frame, never backwards (popupedges.py --slide).
 *
 * The words replace 0x7fb3fc's tail from +0x50 up to its epilogue - the
 * function is entered only at its start (into.py), and its prologue and the
 * two animation reads (+0x00..+0x4c, x8 = &canvas) stay as they are:
 *
 *     ldp s2,s3,[x8]          canvas
 *     ldp s0,s1,[x21,#0x40]   size; fmul by the squash, frintm  (as before)
 *     stp s0,s1,[x19]         the size out
 *     fmadd s2 = canvas.w * 0.5 + slide.x                      (as before)
 *     left = frintm(s2 - w/2); s2 = left + w/2; the same for y
 *     stp s2,s3,[x20]         the centre out; three nops to the epilogue
 *
 * The same function stands in every engine copy (AB, AB+ and the third,
 * 0x38448, 0x17e630, 0x364924), word for word but for the pc-relative ones;
 * each is checked whole - the plain words as Repentance has them, the
 * relative ones by where they lead - and patched only if all of it matches.
 */
#define POPUP_TAIL   0x50                    /* from the size function's start */
#define POPUP_WORDS  22
#define POPUP_CANVAS 0xa9aff0u

static const unsigned long POPUP_SIZERS[] = { 0x7fb3fc, 0x38448, 0x17e630, 0x364924 };

static const uint32_t POPUP_WAS[POPUP_WORDS] = {
    0xBD400100u, 0x1E2C1001u, 0x1F012000u, 0u /* adrp x8 */, 0x913FD108u, 0xBD400102u,
    0x1F012441u, 0x910023E0u, 0u /* bl 0x8bea24 */, 0x910023E1u, 0xAA1403E0u,
    0u /* bl 0x8bee5c */, 0x2D4806A0u, 0x1E200940u, 0x1E254000u, 0x1E210961u,
    0x1E254021u, 0x910023E0u, 0u /* bl 0x8bea24 */, 0x910023E1u, 0xAA1303E0u,
    0u /* bl 0x8bee5c */,
};

static const uint32_t POPUP_NOW[POPUP_WORDS] = {
    0x2D400D02u,    /* ldp    s2, s3, [x8]            canvas w, h */
    0x2D4806A0u,    /* ldp    s0, s1, [x21, #0x40]    the size */
    0x1E200940u,    /* fmul   s0, s10, s0             squash x */
    0x1E254000u,    /* frintm s0, s0 */
    0x1E210961u,    /* fmul   s1, s11, s1             squash y */
    0x1E254021u,    /* frintm s1, s1 */
    0x2D000660u,    /* stp    s0, s1, [x19]           the size out */
    0x1E2C1004u,    /* fmov   s4, #0.5 */
    0x1F042042u,    /* fmadd  s2, s2, s4, s8          canvas.w / 2 + slide.x */
    0x1F042463u,    /* fmadd  s3, s3, s4, s9          canvas.h / 2 + slide.y */
    0x1E240800u,    /* fmul   s0, s0, s4              w / 2 */
    0x1E240821u,    /* fmul   s1, s1, s4              h / 2 */
    0x1E203842u,    /* fsub   s2, s2, s0              left */
    0x1E254042u,    /* frintm s2, s2 */
    0x1E202842u,    /* fadd   s2, s2, s0              the centre over a whole left */
    0x1E213863u,    /* fsub   s3, s3, s1              top */
    0x1E254063u,    /* frintm s3, s3 */
    0x1E212863u,    /* fadd   s3, s3, s1              the centre under a whole top */
    0x2D000E82u,    /* stp    s2, s3, [x20]           the centre out */
    0xD503201Fu,    /* nop */
    0xD503201Fu,    /* nop */
    0xD503201Fu,    /* nop */
};

/* where a BL at `at` leads, or 0 if it is not one */
static unsigned long popup_bl_to(unsigned long at, uint32_t w) {
    if ((w & 0xFC000000u) != 0x94000000u) return 0;
    long imm = (long) (w & 0x03FFFFFFu);
    if (imm & 0x02000000L) imm -= 0x04000000L;
    return at + (unsigned long) (imm * 4);
}

/* the page an ADRP x8 at `at` makes, or 0 if it is not one */
static unsigned long popup_adrp_x8(unsigned long at, uint32_t w) {
    if ((w & 0x9F00001Fu) != 0x90000008u) return 0;
    long imm = (long) ((((w >> 5) & 0x7FFFFu) << 2) | ((w >> 29) & 3u));
    if (imm & (1L << 20)) imm -= (1L << 21);
    return (at & ~0xFFFUL) + (unsigned long) (imm * 4096);
}

static int popup_sizer_is_the_games(image_t *im, unsigned long fn) {
    const uint32_t *w = (const uint32_t *) (im->base + fn);
    /* +0x48 adrp x8, page of the canvas; +0x4c add x8, x8, #0xff0 */
    if (popup_adrp_x8(fn + 0x48, w[0x48 / 4]) != (POPUP_CANVAS & ~0xFFFu) || w[0x4c / 4] != 0x913FC108u)
        return 0;
    for (int i = 0; i < POPUP_WORDS; i++) {
        unsigned long at = fn + POPUP_TAIL + 4 * (unsigned long) i;
        uint32_t now = w[POPUP_TAIL / 4 + i];
        switch (i) {
        case 3:  if (popup_adrp_x8(at, now) != (POPUP_CANVAS & ~0xFFFu)) return 0; break;
        case 8: case 18:  if (popup_bl_to(at, now) != 0x8bea24) return 0; break;
        case 11: case 21: if (popup_bl_to(at, now) != 0x8bee5c) return 0; break;
        default: if (now != POPUP_WAS[i]) return 0;
        }
    }
    /* and the epilogue right after it, untouched */
    return w[(POPUP_TAIL + 4 * POPUP_WORDS) / 4] == 0xA9457BFDu;
}

static void popup_on_whole_units(image_t *im) {
    int done = 0;
    for (unsigned c = 0; c < sizeof(POPUP_SIZERS) / sizeof(POPUP_SIZERS[0]); c++) {
        unsigned long fn = POPUP_SIZERS[c];
        if (!popup_sizer_is_the_games(im, fn)) {
            LOG("popup paper: %lx is not the size function expected - left as it is", fn);
            continue;
        }
        uint32_t *w = (uint32_t *) (im->base + fn + POPUP_TAIL);
        for (int i = 0; i < POPUP_WORDS; i++) w[i] = POPUP_NOW[i];
        done++;
    }
    LOG("popup paper: on whole units in %d of 4 engine copies", done);
    DIAG(if (done != 4) report_line("popup paper", "not every engine copy matched - see the log"));
    (void) done;                         /* the private build has neither */
}

static void *in_libs(const char *n) {
    for (int i = 0; i < g_nlibs; i++) {
        void *p = dlsym(g_libs[i], n);
        if (p) return p;
    }
    return NULL;
}

static void *look_up(const char *sym) {
    /* our own layer comes first: it holds what Android does not have */
    void *own = shim_look_up(sym);
    if (own) return own;
    const char *n = (sym[0] == '_') ? sym + 1 : sym;   /* strip Apple's underscore */
    /*
     * The game's C++ runtime is ours or it is nothing.
     *
     * All forty-four names it needs are answered above, by cxxstream.c and
     * cxxstr.c, in the layout the image was compiled for. Anything else
     * spelled `std::__1` must NOT be looked for on the phone: the only library
     * there that uses that spelling is /system/lib64/libc++.so, a different
     * version on every release of Android, and its objects are not laid out the
     * way this image expects. That is not a worry, it is the bug that cost two
     * testers a fortnight — a stringstream built half by us and half by the
     * phone reported itself failed, and the game died reading its first .anm2.
     *
     * So an unbound one goes to a named stub instead. Nothing changes today,
     * because none is unbound. What changes is tomorrow: a name added later and
     * forgotten now fails the same way on every phone, loudly and in the list of
     * names with no home, rather than quietly working here and dying somewhere
     * else. A fault that hides on the machine you test on is the expensive kind.
     *
     * Taking our own bundled libc++ instead was tried and measured wrong: it
     * spells everything `std::__ndk1` and, worse, builds strings with the length
     * in the first byte where Apple puts it in the last. Redirecting to it
     * reproduced the testers' crash on a phone that works.
     */
    if (strstr(n, "St3__1")) return NULL;
    return in_libs(n);
}

/* ---------- stubs that name themselves ---------- */

/*
 * A common stub will not do: it shows that something was missing but not what.
 * So every missing name gets a trampoline of its own, twelve bytes — it puts
 * its number into x0 and goes to the common handler. The number stands as the
 * first argument, and that is deliberate: we do not intend to carry on after
 * such a call, and what reconnaissance needs is the name.
 */

#define MAX_STUBS 512
static const char *g_stub_name[MAX_STUBS];
static uint8_t g_stub_seen[MAX_STUBS];
static int g_nstubs;
static uint64_t g_swamp_hits;

static uint64_t stub_hit(uint64_t idx) {
    g_swamp_hits++;
    if (idx < MAX_STUBS && !g_stub_seen[idx]) {
        g_stub_seen[idx] = 1;
        LOG("CALLED %s", g_stub_name[idx] ? g_stub_name[idx] : "?");
    }
    return 0;
}

static uint8_t *g_tramp;          /* the page of trampolines */
static size_t g_tramp_used;

#define TRAMP_SIZE 24

static void *make_stub(const char *name) {
    if (!g_tramp) {
        g_tramp = mmap(NULL, MAX_STUBS * TRAMP_SIZE, PROT_READ | PROT_WRITE,
                       MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        if (g_tramp == MAP_FAILED) { g_tramp = NULL; return (void *) stub_hit; }
    }
    if (g_nstubs >= MAX_STUBS) return (void *) stub_hit;
    int idx = g_nstubs++;
    g_stub_name[idx] = name;
    uint8_t *p = g_tramp + (size_t) idx * TRAMP_SIZE;
    uint32_t *c = (uint32_t *) p;
    c[0] = 0xD2800000u | ((uint32_t) idx << 5);      /* movz x0, #idx        */
    c[1] = 0x58000051u;                              /* ldr  x17, [pc, #8]   */
    c[2] = 0xD61F0220u;                              /* br   x17             */
    *(uint64_t *) (p + 12) = (uint64_t) (uintptr_t) stub_hit;
    return p;
}

static void seal_stubs(void) {
    if (g_tramp) mprotect(g_tramp, MAX_STUBS * TRAMP_SIZE, PROT_READ | PROT_EXEC);
}

/* ---------- looking a name up in the image itself ---------- */

/*
 * Some bindings are marked with library number 253 — "look in all images at
 * once". In this game its own C++ templates are declared that way: Apple's
 * linker moves them outside to merge duplicates, and they are found right here.
 * The addresses lie in the export trie — a compressed trie where the path along
 * the edges spells the name.
 */

static uint64_t uleb(const uint8_t **p, const uint8_t *end) {
    uint64_t v = 0;
    int shift = 0;
    while (*p < end) {
        uint8_t b = *(*p)++;
        v |= (uint64_t) (b & 0x7f) << shift;
        if (!(b & 0x80)) break;
        shift += 7;
    }
    return v;
}

/* returns the offset from the image base, or 0 if there is no such name */
static uint64_t trie_find(const uint8_t *trie, const uint8_t *end, const char *sym) {
    const uint8_t *node = trie;
    const char *s = sym;
    while (node < end) {
        const uint8_t *p = node;
        uint64_t term = uleb(&p, end);
        if (term && *s == 0) {                       /* the name ended — this is a leaf */
            uint64_t flags = uleb(&p, end);
            (void) flags;
            return uleb(&p, end);                    /* the offset from the image base */
        }
        const uint8_t *children = p + term;          /* p already stands past the terminal size */
        if (children >= end) return 0;
        uint8_t nkids = *children++;
        const uint8_t *next = NULL;
        for (uint8_t i = 0; i < nkids && children < end; i++) {
            const char *edge = (const char *) children;
            size_t elen = strlen(edge);
            children += elen + 1;
            uint64_t off = uleb(&children, end);
            if (!next && !strncmp(s, edge, elen)) {
                s += elen;
                next = trie + off;
            }
        }
        if (!next) return 0;
        node = next;
    }
    return 0;
}

/* ---------- the loading itself ---------- */

static int map_image(image_t *im) {
    const struct mach_header_64 *mh = (const struct mach_header_64 *) im->raw;
    if (mh->magic != MH_MAGIC_64) { ERR("not a Mach-O 64: %08x", mh->magic); return 0; }

    uint64_t lo = UINT64_MAX, hi = 0;
    const uint8_t *p = im->raw + sizeof(*mh);
    for (uint32_t i = 0; i < mh->ncmds; i++) {
        const struct load_command *lc = (const struct load_command *) p;
        if (lc->cmd == LC_SEGMENT_64) {
            const struct segment_command_64 *sg = (const void *) p;
            if (strcmp(sg->segname, "__PAGEZERO") != 0) {
                if (sg->vmaddr < lo) lo = sg->vmaddr;
                if (sg->vmaddr + sg->vmsize > hi) hi = sg->vmaddr + sg->vmsize;
            }
        }
        p += lc->cmdsize;
    }
    im->lo = lo; im->hi = hi; im->size = hi - lo;

    /*
     * The memory is taken anonymous rather than mapped from the file:
     * executable pages from a file in the app sandbox are not granted by the
     * system, while anonymous ones are — ordinary just-in-time compilation works
     * on the same pages.
     */
    void *mem = mmap(NULL, im->size, PROT_READ | PROT_WRITE,
                     MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (mem == MAP_FAILED) { ERR("could not reserve memory for the image"); return 0; }
    im->base = mem;
    g_report_image_base = (unsigned long) mem;
    g_report_image_size = (unsigned long) im->size;
    LOG("image: originally %012llx..%012llx (%llu KB), landed at %p",
        (unsigned long long) lo, (unsigned long long) hi,
        (unsigned long long) (im->size / 1024), mem);

    p = im->raw + sizeof(*mh);
    im->nsects = 0;
    for (uint32_t i = 0; i < mh->ncmds; i++) {
        const struct load_command *lc = (const struct load_command *) p;
        if (lc->cmd == LC_SEGMENT_64) {
            const struct segment_command_64 *sg = (const void *) p;
            if (strcmp(sg->segname, "__PAGEZERO") != 0 && sg->filesize) {
                memcpy(im->base + (sg->vmaddr - lo), im->raw + sg->fileoff, sg->filesize);
            }
            const struct section_64 *sc = (const void *) (sg + 1);
            for (uint32_t k = 0; k < sg->nsects && im->nsects < 64; k++)
                im->sects[im->nsects++] = &sc[k];
        } else if (lc->cmd == LC_MAIN) {
            im->entry = *(const uint64_t *) (p + 8);
        } else if (lc->cmd == LC_DYLD_EXPORTS_TRIE) {
            uint32_t off = *(const uint32_t *) (p + 8);
            uint32_t sz = *(const uint32_t *) (p + 12);
            im->trie = im->raw + off;
            im->trie_end = im->raw + off + sz;
        }
        p += lc->cmdsize;
    }
    return 1;
}

/*
 * Memory permissions: the code must become executable, the data must stay
 * writable. Set after the patches — otherwise there would be nothing to patch.
 */
static void protect(image_t *im) {
    const struct mach_header_64 *mh = (const void *) im->raw;
    const uint8_t *p = im->raw + sizeof(*mh);
    for (uint32_t i = 0; i < mh->ncmds; i++) {
        const struct load_command *lc = (const void *) p;
        if (lc->cmd == LC_SEGMENT_64) {
            const struct segment_command_64 *sg = (const void *) p;
            if (strcmp(sg->segname, "__PAGEZERO") && sg->vmsize) {
                int prot = 0;
                if (sg->initprot & 1) prot |= PROT_READ;
                if (sg->initprot & 2) prot |= PROT_WRITE;
                if (sg->initprot & 4) prot |= PROT_EXEC;
                if (mprotect(im->base + (sg->vmaddr - im->lo), sg->vmsize, prot) != 0)
                    ERR("permissions: segment %s was refused (%s). The game will die on the first "
                        "jump into this segment", sg->segname, strerror(errno));
                LOG("permissions: %-12s %s%s%s", sg->segname,
                    (prot & PROT_READ) ? "r" : "-", (prot & PROT_WRITE) ? "w" : "-",
                    (prot & PROT_EXEC) ? "x" : "-");
            }
        }
        p += lc->cmdsize;
    }
}

static const struct section_64 *find_sect(image_t *im, const char *seg, const char *sect) {
    for (int i = 0; i < im->nsects; i++)
        if (!strncmp(im->sects[i]->segname, seg, 16) && !strncmp(im->sects[i]->sectname, sect, 16))
            return im->sects[i];
    return NULL;
}

typedef struct { int total, from_android, from_self, swamped; } bind_stat_t;

static int apply_fixups(image_t *im, bind_stat_t *st, char *missing, size_t missing_cap) {
    const struct mach_header_64 *mh = (const void *) im->raw;
    const struct chained_fixups_header *fx = NULL;
    const uint8_t *p = im->raw + sizeof(*mh);
    for (uint32_t i = 0; i < mh->ncmds; i++) {
        const struct load_command *lc = (const void *) p;
        if (lc->cmd == LC_DYLD_CHAINED_FIXUPS) {
            uint32_t off = *(const uint32_t *) (p + 8);
            fx = (const void *) (im->raw + off);
        }
        p += lc->cmdsize;
    }
    if (!fx) { ERR("there are no chained fixups"); return 0; }

    const uint8_t *fxb = (const uint8_t *) fx;
    const char *symbols = (const char *) (fxb + fx->symbols_offset);
    const uint32_t *imports = (const uint32_t *) (fxb + fx->imports_offset);

    /* an address for every foreign name */
    void **targets = calloc(fx->imports_count, sizeof(void *));
    size_t used = 0;
    /* which library answers for the game's C++ runtime, counted by name */
    char cxx_lib[8][64];
    int cxx_count[8] = { 0 };
    int cxx_libs = 0;
    char cxx_stream[64] = "";
    for (uint32_t i = 0; i < fx->imports_count; i++) {
        uint32_t rec = imports[i];
        int lib = rec & 0xFF;                     /* 253 — look in all images at once */
        const char *name = symbols + (rec >> 9);
        void *a = look_up(name);
        /*
         * Who supplies the game's C++ runtime.
         *
         * The image imports its runtime rather than carrying it: 46 names, all
         * with an ordinary library ordinal, and its own export table holds
         * fourteen names none of which are these. So a stringstream the game
         * builds gets its method table from whatever Android hands over — and
         * dlsym on a handle also walks that library's dependencies, which for
         * libEGL reaches the platform's own C++ runtime. That library is a
         * different version on every Android release, while the game's inline
         * code was compiled against Apple's copy and computes its offsets from
         * that. Which library actually answers is therefore worth recording, on
         * every phone, in the report.
         */
        if (a && strstr(name, "St3__1")) {   /* NK for const members, so match past the N */
            Dl_info di;
            const char *from = (dladdr(a, &di) && di.dli_fname) ? di.dli_fname : "?";
            const char *slash = strrchr(from, '/');
            if (slash) from = slash + 1;
            int k;
            for (k = 0; k < cxx_libs && strcmp(cxx_lib[k], from); k++) { }
            if (k < 8) {
                if (k == cxx_libs) {
                    snprintf(cxx_lib[k], sizeof(cxx_lib[k]), "%s", from);
                    cxx_libs++;
                }
                cxx_count[k]++;
            }
            if (strstr(name, "basic_stringstream") && strstr(name, "ZTV"))
                snprintf(cxx_stream, sizeof(cxx_stream), "%s", from);
            LOG("CXX %-6s %s", from, name);
        }
#if ISAAC_DIAG
        /* how many of the game's own names Android also defines */
        if (lib == 253 && im->trie) {
            uint64_t o2 = trie_find(im->trie, im->trie_end, name);
            if (a && o2 && o2 < im->size) LOG("BOTH: %s", name);
        }
#endif
        if (a) {
            st->from_android++;
        } else if (lib == 253 && im->trie) {
            /*
             * The trie must not be asked about foreign names: on branches that
             * are not there the walk returns garbage and the binding goes
             * nowhere. So only a flat search, and only within the image.
             */
            uint64_t off = trie_find(im->trie, im->trie_end, name);
            if (off && off < im->size) { a = im->base + off; st->from_self++; }
        }
        if (a) { targets[i] = a; }
        else {
            targets[i] = make_stub(name);
            st->swamped++;
            if (used + strlen(name) + 2 < missing_cap) {
                used += snprintf(missing + used, missing_cap - used, "%s ", name);
            }
        }
    }

    const struct starts_in_image *si = (const void *) (fxb + fx->starts_offset);
    int nreb = 0, nbind = 0;
    /* the segments come in the same order as the load commands */
    const struct segment_command_64 *segs[16];
    int nseg = 0;
    p = im->raw + sizeof(*mh);
    for (uint32_t i = 0; i < mh->ncmds; i++) {
        const struct load_command *lc = (const void *) p;
        if (lc->cmd == LC_SEGMENT_64 && nseg < 16) segs[nseg++] = (const void *) p;
        p += lc->cmdsize;
    }

    for (uint32_t i = 0; i < si->seg_count && i < (uint32_t) nseg; i++) {
        if (!si->seg_info_offset[i]) continue;
        const struct starts_in_segment *ss =
                (const void *) ((const uint8_t *) si + si->seg_info_offset[i]);
        for (uint32_t pi = 0; pi < ss->page_count; pi++) {
            uint16_t first = ss->page_start[pi];
            if (first == 0xFFFF) continue;
            uint64_t a = segs[i]->vmaddr + (uint64_t) pi * ss->page_size + first;
            for (;;) {
                uint64_t *slot = (uint64_t *) addr_to_mem(im, a);
                if (!slot) { ERR("the chain ran past the image at %llx", (unsigned long long) a); return 0; }
                uint64_t raw = *slot;
                int is_bind = (raw >> 63) & 1;
                uint32_t next = (raw >> 51) & 0xFFF;
                if (is_bind) {
                    /*
                     * A binding has not only a name but an addend as well —
                     * eight bits right after the number. Missing it is not
                     * obvious at once: most often it is zero. But type
                     * information points at the method table PLUS sixteen, and
                     * without the addend dynamic_cast reads service words
                     * instead of a method. The game died on the very first type
                     * cast — at the start of a run.
                     */
                    uint32_t ord = raw & 0xFFFFFF;
                    uint32_t addend = (uint32_t) ((raw >> 24) & 0xFF);
                    *slot = (uint64_t) (uintptr_t) targets[ord] + addend;
                    nbind++;
                } else {
                    /* the offset is counted from the first mapped segment */
                    uint64_t target = raw & 0xFFFFFFFFFULL;
                    *slot = (uint64_t) (uintptr_t) (im->base + target);
                    nreb++;
                }
                if (!next) break;
                a += (uint64_t) next * 4;
            }
        }
    }
    st->total = nbind;
    free(targets);
    LOG("fixups: ours %d, foreign %d (names %u)", nreb, nbind, fx->imports_count);
    {
        /*
         * Into the report as well, not only the log. On a phone that is not
         * here these counts are the only way to tell a whole image from a
         * half-resolved one, and there is no log to read.
         */
        char sum[160];
        snprintf(sum, sizeof(sum), "fixups ours %d, foreign %d, names %u, into stubs %d",
                 nreb, nbind, fx->imports_count, st->swamped);
        report_line("check", sum);
    }
    /*
     * The list of names that did not resolve is needed AT ONCE, not in a report
     * at exit: with an endless frame loop the exit never comes, and every such
     * stub is a quietly wrong answer somewhere in the middle of the game.
     */
    {
        /* the answer to "whose C++ runtime is this", straight into the report */
        char sum[300];
        int n = snprintf(sum, sizeof(sum), "c++ runtime from");
        for (int k = 0; k < cxx_libs && n < (int) sizeof(sum) - 40; k++)
            n += snprintf(sum + n, sizeof(sum) - n, " %s x%d", cxx_lib[k], cxx_count[k]);
        snprintf(sum + n, sizeof(sum) - n, "; stringstream table from %s",
                 cxx_stream[0] ? cxx_stream : "the image");
        report_line("check", sum);
    }
    if (used) LOG("in the swamp (%d): %s", st->swamped, missing);
    if (used) {
        /* the names that found no home, capped so the report stays readable */
        char sum[420];
        snprintf(sum, sizeof(sum), "no home for %d: %.360s", st->swamped, missing);
        report_line("check", sum);
    }
    return 1;
}

/* ---------- a check: do the class names read ---------- */

/* the flag bits only, never the top of the address - see objc.c for why */
#define FAST_DATA_MASK (~(uint64_t) 7)

static int read_classes(image_t *im, char *out, size_t cap) {
    const struct section_64 *sc = find_sect(im, "__DATA_CONST", "__objc_classlist");
    if (!sc) sc = find_sect(im, "__DATA", "__objc_classlist");
    if (!sc) return 0;
    uint64_t *list = (uint64_t *) addr_to_mem(im, sc->addr);
    if (!list) return 0;
    int n = (int) (sc->size / 8), ok = 0;
    size_t used = 0;
    for (int i = 0; i < n; i++) {
        uint8_t *cls = (uint8_t *) (uintptr_t) list[i];
        if (cls < im->base || cls >= im->base + im->size) continue;
        uint64_t data = (*(uint64_t *) (cls + 0x20)) & FAST_DATA_MASK;
        uint8_t *ro = (uint8_t *) (uintptr_t) data;
        if (ro < im->base || ro >= im->base + im->size) continue;
        const char *name = (const char *) (uintptr_t) (*(uint64_t *) (ro + 0x18));
        if ((const uint8_t *) name < im->base || (const uint8_t *) name >= im->base + im->size)
            continue;
        ok++;
        if (used + strlen(name) + 2 < cap) used += snprintf(out + used, cap - used, "%s ", name);
    }
    return ok;
}

/*
 * A self-test written into the startup report.
 *
 * Two testers' phones die at the same instruction, and the phone here does not.
 * Their report already told us the input was a real path and the parsing still
 * produced nothing, so the question is no longer "which file" but "does the
 * game's own string splitting work on that phone at all". Asking it directly
 * beats another round of guesses: the answer arrives in the same report as the
 * crash, on the first launch, without a debugger anybody has to attach.
 *
 * Everything here runs after the static constructors, because that is when the
 * game's iostreams are ready — the splitting is built on istringstream.
 */
static void report_num(const char *what, long v) {
    char line[160];
    snprintf(line, sizeof(line), "%s %ld", what, v);
    report_line("check", line);
}

/*
 * Call a function that returns a large object. AArch64 hands such a function
 * the address of the result in x8, which C cannot express, so the register is
 * placed by hand.
 */
static void call_with_x8(void *fn, void *a0, unsigned long a1, void *out) {
    register unsigned long r0 asm("x0") = (unsigned long) a0;
    register unsigned long r1 asm("x1") = a1;
    register unsigned long r8 asm("x8") = (unsigned long) out;
    register unsigned long rf asm("x9") = (unsigned long) fn;
    __asm__ volatile("blr %[f]"
                     : "+r"(r0)
                     : [f] "r"(rf), "r"(r1), "r"(r8)
                     : "x2", "x3", "x4", "x5", "x6", "x7", "x10", "x11", "x12",
                       "x13", "x14", "x15", "x16", "x17", "x30",   /* not x18: Android keeps it */
                       "memory", "cc");
}

/*
 * Split a path with the game's own code and say how many parts came out.
 *
 * A libc++ string of this length lives inside the object: the text sits at the
 * front and the last byte holds the length with its top bit clear. That is the
 * layout the game's own code reads back at 0x7f22b4, so a string built this way
 * is the same thing it builds for itself.
 *
 * "gfx/ui/ScoreMenu.anm2" split on '/' is three parts. Zero means the splitting
 * itself is broken on this phone, and the crash we are chasing is a consequence
 * rather than a cause.
 */
static long split_parts(image_t *im) {
    static const char sample[] = "gfx/ui/ScoreMenu.anm2";
    unsigned char str[24];
    unsigned long vec[3] = { 0, 0, 0 };
    size_t len = sizeof(sample) - 1;
    memset(str, 0, sizeof(str));
    memcpy(str, sample, len);
    str[23] = (unsigned char) len;          /* short string: length, top bit clear */
    call_with_x8((void *) (im->base + 0x7f218c), str, '/', vec);
    if (!vec[0] || vec[1] < vec[0]) return -1;
    long n = (long) ((vec[1] - vec[0]) / 24);
    /*
     * The pieces themselves, not just how many. Counting alone once said three
     * while every one of them held rubbish, and the crash that followed was
     * blamed on the caller for an afternoon.
     */
    {
        const char *shim_str_bytes(const void *);
        size_t shim_str_length(const void *);
        char line[300];
        int o = snprintf(line, sizeof(line), "splitting a path: %ld parts:", n);
        for (long i = 0; i < n && i < 6 && o < (int) sizeof(line) - 60; i++) {
            const void *e = (const char *) vec[0] + i * 24;
            o += snprintf(line + o, sizeof(line) - o, " \"%.40s\"(%u)",
                          shim_str_bytes(e), (unsigned) shim_str_length(e));
        }
        report_line("check", line);
    }
    return n;
}

/*
 * The same question, asked from deep in the stack.
 *
 * This is the one measurement that separates the two remaining explanations. If
 * splitting works near the top of the stack and fails far down it, the fault is
 * the stack; if it fails in both places, the fault is the parsing. Each level
 * eats about as much as the game's own deepest function does, and the number of
 * levels is chosen from the stack this thread actually has, so the probe can
 * never be the thing that overruns it.
 */
static long split_deep(image_t *im, int levels) {
    volatile char burn[160 * 1024];
    burn[0] = (char) levels;
    burn[sizeof(burn) - 1] = (char) levels;
    if (levels > 0) return split_deep(im, levels - 1);
    return split_parts(im);
}

#if ISAAC_DIAG
/*
 * setjmp in Apple's layout, measured where it matters (02.10.2026).
 *
 * The game's libpng 1.0.2 keeps error_fn right after a 192-byte jmp_buf, and
 * Android's setjmp wrote its checksum there, so every png_error jumped to the
 * checksum (darwin_jmp.S). Three answers, in order, each only if the one
 * before it held:
 *  1. the round trip itself: registers, stack, return value, and not one byte
 *     written from +0xb8 on (the buffer is 256 bytes of 0xA5);
 *  2. the image's own import slots really hold ours (GOT 0xa84ad0 _setjmp,
 *     0xa848c8 _longjmp — tools/gotname.py);
 *  3. the game's own picture loader 0x8a6150 (this, stream, name) fed a
 *     truncated PNG: the signature passes, IHDR runs short, the read callback
 *     0x8aecb0 calls png_error("Read Error"). On iOS that is "Error loading
 *     png: ..." in the game's log and a false return; with Android's setjmp it
 *     was a jump into the checksum. The object is never touched on that path
 *     (it is only addressed after png_read_info), a zeroed block stands in.
 */
int shim_darwin_setjmp(void *buf);
void shim_darwin_longjmp(void *buf, int val);
int shim_darwin_jmp_probe(void *buf);

#define GOT_SETJMP   0xa84ad0
#define GOT_LONGJMP  0xa848c8
#define PNG_LOADER   0x8a6150

/* a stand-in for KAGE's stream: the loader calls slot +0x30 as fread would be */
typedef struct {
    void *const *vt;
    const unsigned char *data;
    size_t len, at, calls;
} jmp_stream_t;

static size_t jmp_stream_read(jmp_stream_t *s, void *buf, size_t size, size_t count) {
    size_t want = size * count, left = s->len - s->at;
    size_t n = want < left ? want : left;
    memcpy(buf, s->data + s->at, n);
    s->at += n;
    s->calls++;
    return size ? n / size : 0;
}

static void jmp_selftest(image_t *im) {
    char line[200];
    unsigned char buf[256];
    memset(buf, 0xA5, sizeof(buf));
    int bits = shim_darwin_jmp_probe(buf);
    int tail = -1;
    for (int i = 0xb8; i < (int) sizeof(buf); i++)
        if (buf[i] != 0xA5) { tail = i; break; }
    void **got_set = (void **) addr_to_mem(im, im->lo + GOT_SETJMP);
    void **got_long = (void **) addr_to_mem(im, im->lo + GOT_LONGJMP);
    int bound = got_set && got_long && *got_set == (void *) shim_darwin_setjmp
                && *got_long == (void *) shim_darwin_longjmp;
    int ok = !bits && tail < 0 && bound;
    if (tail < 0)
        snprintf(line, sizeof(line), "setjmp: selftest %s: probe bits %d, nothing written from +0xb8, image slots %s",
                 ok ? "ok" : "FAILED", bits, bound ? "ours" : "NOT OURS");
    else
        snprintf(line, sizeof(line), "setjmp: selftest FAILED: probe bits %d, byte +%#x written, image slots %s",
                 bits, tail, bound ? "ours" : "NOT OURS");
    report_line("check", line);
    if (!ok) {
        report_line("check", "png selftest: skipped, setjmp is not sound");
        return;
    }

    static const unsigned char truncated[] = {
        0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A,       /* the signature */
        0x00, 0x00, 0x00, 0x0D, 'I', 'H', 'D', 'R',         /* IHDR, 13 bytes promised */
        0x00, 0x00, 0x00, 0x10,                             /* ... and 4 delivered */
    };
    static void *const vt[8] = { 0, 0, 0, 0, 0, 0, (void *) jmp_stream_read, 0 };
    jmp_stream_t st = { vt, truncated, sizeof(truncated), 0, 0 };
    static unsigned char object[4096];
    memset(object, 0, sizeof(object));
    int (*load)(void *, void *, const char *) =
            (int (*)(void *, void *, const char *)) (im->base + PNG_LOADER);
    report_line("check", "png selftest: a truncated picture through the game's loader");
    int r = load(object, &st, "selftest/truncated.png");
    int clean = 1;
    for (size_t i = 0; i < sizeof(object); i++)
        if (object[i]) { clean = 0; break; }
    snprintf(line, sizeof(line), "png selftest: the loader said %d (0 = refused), %zu of %zu bytes read in %zu calls, object %s",
             r, st.at, st.len, st.calls, clean ? "untouched" : "WRITTEN");
    report_line("check", line);
}
#endif

static void self_test(image_t *im, int inits) {
    char line[512];
    unsigned long stack_base = 0, stack_size = 0;

    /*
     * How much stack the game thread has, and how much of it a frame may use.
     * The game moves the stack pointer 160 KB in one instruction and leans on
     * Apple's stack probe, which our layer turns into a no-op. A thread's stack
     * on Android is a fixed mapping with one guard page, so the size matters.
     */
    {
        pthread_attr_t at;
        void *base = NULL;
        size_t size = 0, guard = 0;
        if (pthread_getattr_np(pthread_self(), &at) == 0) {
            pthread_attr_getstack(&at, &base, &size);
            pthread_attr_getguardsize(&at, &guard);
            stack_base = (unsigned long) base;
            stack_size = (unsigned long) size;
            void shim_set_stack_bounds(unsigned long, unsigned long);
            shim_set_stack_bounds(stack_base, stack_size);
            snprintf(line, sizeof(line), "thread stack %lu KB at %p, guard %lu B, here %p",
                     (unsigned long) (size >> 10), base, (unsigned long) guard,
                     (void *) __builtin_frame_address(0));
            report_line("check", line);
        } else {
            report_line("check", "thread stack: could not be asked");
        }
    }

    /*
     * Where our own library landed. A crash inside the layer reports a bare
     * address, and without this line there is no way to turn it back into a
     * function name — the loader moves the library on every launch.
     */
    {
        Dl_info di;
        if (dladdr((void *) (uintptr_t) &self_test, &di) && di.dli_fbase) {
            snprintf(line, sizeof(line), "layer at %p (%s)", di.dli_fbase,
                     di.dli_fname ? di.dli_fname : "?");
            report_line("check", line);
        }
    }
    snprintf(line, sizeof(line), "page %ld B, image base %p, size %llu KB",
             (long) sysconf(_SC_PAGESIZE), (void *) im->base,
             (unsigned long long) (im->size >> 10));
    report_line("check", line);

    report_num("constructors", inits);
    report_num("stubs touched", (long) g_swamp_hits);

    /* the splitting, near the top of the stack and then far down it */
    report_line("check", "splitting a path: starting");
    report_num("splitting a path: parts", split_parts(im));

    /*
     * The failing function itself, driven by hand with the very arguments the
     * crash reported. Everything it needs comes in ordinary registers, so it
     * can simply be called: a path, the sheet named inside it, a 260-byte
     * buffer and its size. Working here and dying in the game's own hands would
     * mean the function is sound and its caller hands it something we have not
     * seen; dying here would put the fault inside it, in plain view.
     */
    {
        char out[300];
        void (*resolve)(const char *, const char *, char *, int) =
                (void (*)(const char *, const char *, char *, int)) (im->base + 0x7f2278);
        memset(out, 0, sizeof(out));
        report_line("check", "resolving a sheet: starting");
        resolve("gfx/ui/ScoreMenu.anm2", "ScoreMenu.png", out, 260);
        snprintf(line, sizeof(line), "resolving a sheet: \"%.200s\"", out);
        report_line("check", line);
    }
    if (stack_size > 4u << 20) {
        int levels = (int) ((stack_size >> 10) / 160 / 3);   /* a third of the stack */
        if (levels > 12) levels = 12;
        snprintf(line, sizeof(line), "splitting deep: %d levels of 160 KB", levels);
        report_line("check", line);
        report_num("splitting deep: parts", split_deep(im, levels));
    } else {
        report_line("check", "splitting deep: skipped, the stack is too small to probe");
    }

    /*
     * How much memory the process holds by now. A phone that kills the app for
     * its appetite leaves no other trace at all, and the numbers Java wrote at
     * the top say what the phone had, not what we took.
     */
    {
        FILE *f = fopen("/proc/self/statm", "r");
        if (f) {
            unsigned long total = 0, rss = 0;
            if (fscanf(f, "%lu %lu", &total, &rss) == 2) {
                long page = sysconf(_SC_PAGESIZE);
                snprintf(line, sizeof(line), "memory held: %lu MB resident, %lu MB mapped",
                         (unsigned long) ((rss * (unsigned long) page) >> 20),
                         (unsigned long) ((total * (unsigned long) page) >> 20));
                report_line("check", line);
            }
            fclose(f);
        }
    }

#if ISAAC_DIAG
    /* setjmp in Apple's layout, then a broken picture through the game's loader */
    jmp_selftest(im);
#endif
}

/* ---------- the entry from Java ---------- */

JNIEXPORT jstring JNICALL
Java_ru_isaac_port_MainActivity_probe(JNIEnv *env, jobject thiz, jbyteArray blob, jstring home,
                                     jint scr_w, jint scr_h, jfloat scr_density, jstring bundle) {
    shim_set_screen(scr_w, scr_h, scr_density);
    const char *bp = (*env)->GetStringUTFChars(env, bundle, NULL);
    shim_set_bundle(bp);
    (*env)->ReleaseStringUTFChars(env, bundle, bp);
    const char *hp = (*env)->GetStringUTFChars(env, home, NULL);
    shim_set_home(hp);
    (*env)->ReleaseStringUTFChars(env, home, hp);
    static char report[8192];
    static char missing[65536];
    static char names[1024];
    missing[0] = names[0] = 0;

    jsize len = (*env)->GetArrayLength(env, blob);
    jbyte *bytes = (*env)->GetByteArrayElements(env, blob, NULL);

    image_t im;
    memset(&im, 0, sizeof(im));
    im.raw = (const uint8_t *) bytes;
    im.raw_size = (uint64_t) len;

    open_libs();
    shim_darwin_init();          /* the character table is needed before the first string parsing */
    shim_darwin_streams();
    shim_uikit_install();          /* the layer's built-in methods */
    LOG("Android libraries opened: %d", g_nlibs);
    {
        char sum[80];
        snprintf(sum, sizeof(sum), "android libraries opened %d", g_nlibs);
        report_line("check", sum);
    }

    int okmap = map_image(&im);
    if (okmap) shim_objc_set_image(im.base, (unsigned long) im.size);
    bind_stat_t st = {0, 0, 0, 0};
    int okfix = okmap && apply_fixups(&im, &st, missing, sizeof(missing));
    int classes = okfix ? read_classes(&im, names, sizeof(names)) : 0;

    int inits = 0;
    if (okfix) {
        seal_stubs();
        /* the image's method names — the launch selector is looked up among them */
        const struct section_64 *mn = find_sect(&im, "__TEXT", "__objc_methname");
        if (mn) shim_objc_set_selectors(addr_to_mem(&im, mn->addr), (unsigned long) mn->size);
        const struct section_64 *cl = find_sect(&im, "__DATA_CONST", "__objc_classlist");
        if (cl) shim_objc_set_classlist(addr_to_mem(&im, cl->addr), (unsigned long) cl->size);
        /*
         * The image's constant strings: their layout is not ours, the text lies
         * at +16 rather than at +8. Without these bounds our NSString would read
         * their flags field as a pointer to the text — that is how the game died
         * on death.
         */
        const struct section_64 *cf = find_sect(&im, "__DATA_CONST", "__cfstring");
        if (!cf) cf = find_sect(&im, "__DATA", "__cfstring");
        if (cf) shim_str_set_cfstrings(addr_to_mem(&im, cf->addr), (unsigned long) cf->size);
        else LOG("image strings: the __cfstring section was not found");
        own_all_dlc(&im);
        if (shim_report_active()) game_log_on(&im);
        no_cloud_poll(&im);
        plain_saves(&im);
        brightness_default(&im);
        aimlock_default(&im);
        filter_gone(&im);          /* the Filter option can never be on */
        shop_idle_plays(&im);      /* an animated item's ShopIdle runs in a shop too */
        glitched_stars(&im);       /* a TMTRAINER item in the history no longer crashes The Stars? */
        { void shim_sticks_aimlock_rule(unsigned char *); shim_sticks_aimlock_rule(im.base); }  /* charge weapons fire on release (sticks.c) */
        double_tap_gate(&im);
        coop_join_ours(&im);
        coop_twins_first_match(&im);
        coop_pocket_on_screen(&im);
        platform_forbids_ours(&im);
        { void shim_audio_patch(unsigned char *); shim_audio_patch(im.base); }   /* sound pool and cache (audio.c) */
        /*
         * The sticks are kept in place only if the setting says so: the dynamic
         * variant stays available as it was.
         */
        /*
         * Removing the "dragging" is needed by both modes: both a stick that
         * stays put and our dynamic one must follow the finger as far as it
         * goes but never leave their own place.
         */
        touch_mode_words(&im);
        touch_mode_tap(&im);
        stick_menu_items(&im);
        fullscreen_menu_item(&im);
        rumble_menu_item(&im);     /* the game's FILTER row is the RUMBLE slider */
        fixed_sticks(&im);
        stick_hooks(&im);          /* the dead zone on the stick's output, the knob by the finger */
        touch_alpha_words(&im);    /* the touch controls' opacity, the editor's slider */
        popup_on_whole_units(&im); /* every dialog's paper on whole units (0x7fb3fc) */
        touch_in_controls(&im);
        /*
         * Every kind is applied, not just the fixed one.
         *
         * This used to patch the image only when the sticks were fixed and
         * merely say so otherwise - so a player who had chosen `dynamic` in the
         * settings file ran on the game's own words, and the capture zone stayed
         * at the three travels it ships with. That is what kept a dynamic stick
         * standing where it last stood: the next touch inside sixty units was
         * taken as a grab of the old stick rather than as a new one. The menu
         * put it right, because changing the setting there does call this - but
         * only after the setting had been changed by hand.
         */
        sticks_kind_at_load(&im, shim_sticks_static() ? 0
                                 : shim_sticks_floating() ? 2 : 1);
        leave_binding_screen(&im);
        extra_hud_one_way(&im);
        colours_above_white(&im);
        options_scroll(&im);
        rerun_tap(&im);
        main_menu_mods(&im);
        { void shim_rumble_install(unsigned char *); shim_rumble_install(im.base); }
        /* the knife's Update slot: Range Fix acts right after it (rangefix.c) */
        { void shim_rangefix_install(unsigned char *); shim_rangefix_install(im.base); }
        {
            /*
             * A first guess from the display, so the words are right even if
             * the menu were built before the first fit. screen.c corrects it
             * with the canvas the fit actually computes.
             */
            float lo = (float) (scr_w < scr_h ? scr_w : scr_h);
            float hi = (float) (scr_w < scr_h ? scr_h : scr_w);
            float ratio = lo > 0.0f ? hi / lo : 480.0f / 270.0f;
            menu_grid_apply(&im, 270.0f * ratio);
            menu_rows_apply(&im, ratio > 0.0f ? 480.0f / ratio : 270.0f);
        }
        /*
         * The byte road through the font reads a string one byte at a time and
         * so cannot show anything above Latin-1, while every string the game
         * keeps is UTF-8. Sending it down the wide road makes the text readable
         * in every language - see text.c for why this is the game's own defect
         * and not a difference between the platforms.
         */
        { void shim_text_utf8(void); shim_text_utf8(); }
        /*
         * The path whose directory the game takes when an .anm2 names its
         * sheet. Registered only when a report is open: it is silent either
         * way, but a build nobody is reading costs nothing for it. A tester's
         * release build died inside this helper (offset 0x7f2308) and only the
         * name it remembers said which file that was.
         *
         * The mechanism, read out of the code: the helper copies the path,
         * turns '\' into '/', splits it on '/' and pops the last part. The
         * split is getline over an istringstream, and it pushes nothing at all
         * only for an EMPTY string — a string without a slash still yields one
         * part, because the loop tests badbit|failbit and not eofbit. An empty
         * vector then makes pop_back read one byte below null, which is the
         * reported 0xffffffffffffff with the top byte ignored.
         */
        if (shim_report_active()) shim_trace_add(0x7f2278, "=anm2 path");
        /* every track the game starts, to find the greed wave's missing jingle */
        if (shim_report_active()) shim_trace_add(0x822c, "1music track");
        shim_trace_add(0x862f04, "#hold the stick");
        shim_trace_add(0x861ea0, "#show the stick");
        /*
         * The pause button as it draws itself. Behaviour, not observation:
         * the long press that resets a run takes the button's box from the
         * button rather than from a measurement, and without this it would
         * fall back to numbers taken off a screenshot.
         */
        shim_trace_add(0x863ccc, "Ppause button");
        /*
         * The overlay sprites. Needed not for observation: from this the drawing
         * wrapper learns that the overlay has begun and lifts the depth test for
         * that time — otherwise the stick's cap goes under its own base.
         */
        /*
         * The settings menu. Behaviour too, not observation: our two items are
         * inserted into the list from this trace and the taps on them are
         * handled. It has no place inside the diagnostics — in the working build
         * the items simply would not exist.
         *
         * The hook stands at 0x6e3a88 — that is the drawing of the list, with
         * the menu object in x0. It used to stand at 0x6e3990, and that function
         * ends with a ret at 0x6e3a84 and is called only on a redraw after a
         * tap. The values of our items were written once per tap, the game
         * managed to overwrite the fields with its own, and the screen always
         * showed its value rather than ours.
         */
        shim_trace_add(0x6e3a88, "$options menu");
        /* which sound the menu plays, and from where (trace.c, mark @) */
        if (shim_report_active()) shim_trace_add(0x671c8c, "@sound played");

        /*
         * The difficulty on the character screen: a tap picks the row it
         * lands on rather than stepping to the next one. Behaviour, not
         * observation, so it stands outside the diagnostics - see menu.c for
         * why these three places and no others.
         *
         *   0x5e78ac  the screen's input handler, x0 is the screen
         *   0x5e86d4  the widget's drawing point, still in s0/s1
         *   0x5e7dbc  the word after the one store the difficulty passes
         */
        {
            int d = shim_trace_add(0x5e78ac, "Dcharacter screen input");
            int f = shim_trace_add(0x5e86d4, "Fdifficulty widget drawn");
            int k = shim_trace_add(0x5e7dbc, "Kdifficulty chosen");
            /*
             * Two measuring hooks stood here while the difficulty widget was
             * being taken apart: the touch device's held-set (0x8ddc9c) and
             * the shared "is this action pressed" (0x8a8098). They are gone on
             * purpose. The second one is asked several times a frame by every
             * device, and a hook there saves and restores a whole register
             * block each time - in a build that exists to measure frame times
             * that is measuring the measurement. Put them back by hand if the
             * widget ever needs looking at again; the addresses are here.
             */
            if (shim_report_active()) {
                char line[80];
                snprintf(line, sizeof(line), "hooks placed: D %d, F %d, K %d", d, f, k);
                report_line("difficulty", line);
            }
        }

        /*
         * The end of reading a save. The three daily-run trophies are granted
         * there if they are not already - the daily run is off in this port,
         * so they cannot be earned at all, and three items would stay out of
         * the game for good. Behaviour, not observation. See menu.c.
         *
         * It stands on the common exit of the reader (0x70065c), which is the
         * first moment the block holds the save that was chosen.
         */
        shim_trace_add(0x70065c, "Lsave has been read");

        /*
         * The daily run. Not observation but behaviour: a tap in the menu opens
         * the online service readiness check, without which the run never
         * starts. The details are at online_service_ready() in objc.c.
         *
         * It stands HERE, not in the diagnostics: inside ISAAC_DIAG the patch
         * would disappear from the working build and the daily run would stop
         * working again.
         */
        shim_trace_add(0x672358, "&run request B");
        /*
         * The daily run plaque on the main menu is removed with the layer's own
         * flag (menu.c, daily_sign_off). Outside the diagnostics as well: this
         * is the behaviour of the working build, not observation.
         */
        shim_trace_add(0x7f8e34, "~plaque: screen 3 update");
        shim_trace_add(0x7f9da4, "~plaque: screen 3 draw");
        /*
         * The step that serves a request for a run (root+0x1f6b40). Outside the
         * diagnostics as well: a long press on the pause button leaves its
         * request here, and that is behaviour of the working build. See reset.c
         * for what the record holds and why the reset only raises one byte.
         */
        shim_trace_add(0x671348, "+the run request as it is served");

        /*
         * Every widget of the touch overlay, as a finger reaches it and before
         * the widget tests its own box. Behaviour, not observation: the map's
         * zone is offset here to match the HUD offset the game only draws with.
         */
        shim_trace_add(0x8620f0, "Wevery touch widget");

        /*
         * The overlay's next call after the bomb button has been updated.
         * Behaviour, not observation: a golden bomb or Blood Bombs with an empty
         * count leaves the button hidden although the game would still drop
         * bombs, and this is where it is put back (hud.c).
         */
        shim_trace_add(0x85fee8, "Bbomb button");

        /*
         * The touch group's own draw, at its door. Behaviour, not observation:
         * the bomb button's byte is put back here, because the update clears
         * it and then draws before anything else of ours runs (hud.c).
         */
        shim_trace_add(0x85fb98, "Ytouch group draw");

        /*
         * The item button's action, before it decides what to do with it.
         * Behaviour, not observation: a long press on the School Bag's button
         * swaps the two actives without using either (bag.c).
         */
        shim_trace_add(0x864434, "Aitem button action");
        shim_trace_add(0x86448c, "Cwidget action, the other list");

        /*
         * The walk over the pressed widgets, called right after the overlay has
         * published its own actions. Behaviour, not observation: a long press on
         * a card or pill button asks for the drop from here, because an answer
         * written anywhere later is wiped by the overlay's own zero (pocket.c).
         */
        shim_trace_add(0x861d1c, "Ewidgets are walked");
        shim_trace_add(0x860b3c, "Mthe button row");

        /*
         * The fit. Behaviour, not observation: the canvas is widened here, at
         * the last moment before the fit reads it and the only one where the
         * window size is known (screen.c).
         */
        shim_trace_add(0x670a00, "Gthe fit");
        /*
         * The menu being asked to change screens. Behaviour, not observation:
         * choosing the touch device in Controls opens our editor instead of the
         * key bindings, and the camera is sent to an empty place of the menu's
         * world (touchedit.c).
         */
        shim_trace_add(0x769b88, "Vthe menu screen switch");
        /*
         * The Controls screen as it draws. Behaviour, not observation: the row
         * of the touch device is renamed from the engine's "DeviceiOS" to
         * "Touch" (touchedit.c), on the list the screen has just rebuilt.
         */
        shim_trace_add(0x597d34, "Zthe controls screen draws");
        /* who wipes the temporary effects, and from where: objc.c holds them over a minimise */
        shim_trace_add(0x6fdd80, "Rthe effects are wiped");

        /*
         * The menus' box test. Behaviour, not observation: screens that pass no
         * position of their own get the canvas offset put back (screen.c).
         */
        shim_trace_add(0x862674, "Hthe menu box test");
        shim_trace_add(0x86255c, "Jthe finger in units");
        shim_trace_add(0x8de4e4, "Na surface is born");
        /*
         * EntityList::Update, which receives the live list of entities as its
         * first argument. That one pointer is what item descriptions are read
         * from, and it saves walking the process's memory to find them.
         */
        shim_trace_add(0x76436c, "Ithe entity list");
        shim_trace_add(0x861df8, "Othe overlay draw");
        /*
         * A Button reads its middle in two places: its draw (0x863ccc) and its
         * hit test. The draw is hooked already, as "Ppause button", and one
         * address takes one hook - so the twins' button is put right from that
         * handler as well (trace.c), and only the hit test is added here.
         */
        shim_trace_add(0x863b1c, "*a button is touched");
        /*
         * The game's own HUD draw. It holds the minimap (0x578490 upwards) and
         * both readings of the ExtraHUD setting, so it is the HUD and nothing
         * else; the item panel is drawn from here so that the pause paper and
         * the stage transition cover it, as they cover the rest of the HUD.
         */
        shim_trace_add(0x578490, "Xthe hud draw");
        /*
         * The player HUD's corners are laid (hudlayout.c): between
         * the last corner (0x6ee5d0..0x6ee604) and the loop (0x6ee64c). The five
         * words the hook copies are checked first - mov x26, #0; str s0, [sp,
         * #0x118]; str s1, [sp, #0x11c]; add x8, x20, #0xc4, lsl #12; add x22,
         * x8, #0xbf8 - and tools/into.py 0x6ee5ec 0x6ee64c finds no entry into
         * them. A word that is not the expected one: no hook, the game's HUD.
         */
        /*
         * On again (05.10.2026, the owner: "take everything from Rep+ but the
         * online"): the layout is Repentance+ 1.9.7's offline one, checked on
         * the desk by emulation of both games (tools/hudesk/hudcheck.py).
         * 0 leaves the players' HUD wholly the game's.
         */
#define HUD_LAYOUT_HOOK 1
        if (HUD_LAYOUT_HOOK) {
            static const uint32_t corners_laid[5] = { 0xD280001Au, 0xBD011BE0u, 0xBD011FE1u, 0x91431288u, 0x912FE116u };
            const uint32_t *w = (const uint32_t *) (im.base + 0x6ee608);
            if (!memcmp(w, corners_laid, sizeof(corners_laid)))
                shim_trace_add(0x6ee608, "5the HUD's corners are laid");
            else
                LOG("hud: the words at 6ee608 are not the expected ones (%08x) - the players' HUD stays the game's", w[0]);
        }
        /*
         * The stats column's draw (0x6efac4), for the place it is handed: the
         * planetarium line of mods.c goes under it.
         */
        shim_trace_add(0x6efac4, "3the stats column");
        /*
         * THE CAMERA SPLIT of ORIGINAL and POTATO (screen.c): Room::Render's
         * entry, and the laying of the Colormod Surface on the scene after the
         * quad's origin is zeroed. Both checked word for word.
         */
        {
            static const uint32_t room_entry[5] = { 0x6DB733EDu, 0x6D012BEBu, 0x6D0223E9u, 0xA9036FFCu, 0xA90467FAu };
            static const uint32_t room_laid[5] = { 0x900011A8u, 0x91136108u, 0xBD400100u, 0xBC1843A0u, 0x900011A8u };
            static const uint32_t room_slide[5] = { 0xB00020D4u, 0x91136294u, 0xBD400280u, 0xB00020D5u, 0x911372B5u };
            if (!memcmp((const void *) (im.base + 0x5c2630), room_entry, sizeof(room_entry))
                && !memcmp((const void *) (im.base + 0x88e9cc), room_laid, sizeof(room_laid))
                && !memcmp((const void *) (im.base + 0x6a9314), room_slide, sizeof(room_slide))) {
                shim_trace_add(0x5c2630, "{the room is drawn");
                shim_trace_add(0x88e9cc, "}the room is laid");
                shim_trace_add(0x6a9314, "[the rooms slide");
                /* and the menu's (screen.c THE MENU, THE SAME WAY) */
                static const uint32_t menu_draw[5] = { 0xB90003FFu, 0xB819C3BFu, 0xB0001AD7u, 0x911362F7u, 0xBD4002E0u };
                static const uint32_t menu_laid[5] = { 0xBD4002E1u, 0xBD400300u, 0x2D3287A0u, 0x910063E0u, 0x910003E1u };
                if (!memcmp((const void *) (im.base + 0x7691c8), menu_draw, sizeof(menu_draw))
                    && !memcmp((const void *) (im.base + 0x76962c), menu_laid, sizeof(menu_laid))) {
                    shim_trace_add(0x7691c8, "]the menu is drawn");
                    shim_trace_add(0x76962c, ")the menu is laid");
                } else LOG("screen: the menu's draw or laying is not as expected - its camera stays the game's");
            } else LOG("screen: Room::Render or the laying is not as expected - one canvas keeps the game's camera");
        }
        /*
         * A sprite's single-layer render (0x89e550): the pause screen draws its
         * layers one by one through it, and the port's strap goes in just
         * before the paper (straps.c).
         */
        shim_trace_add(0x89e550, "4a layer render");

#if ISAAC_DIAG
        shim_trace_add(0x89e4c0, "%overlay sprite");
        /*
         * THE GROUND TRUTH OF GAME TIME (the research notes of 14.10.2026,
         * frame audit). The frames line DERIVED "updates a second" as
         * presents / 2. These two hooks count the real calls: the game update
         * the manager's tick (0x670fb0) runs on its even ticks (0x88f880, sole
         * caller 0x671270) and the interpolation pass of its odd ticks
         * (0x88f770, sole caller 0x6712c0). Both begin with five plain words
         * (stp, stp, add/stp, mov, movk/mov) - nothing a copy breaks. Report
         * builds only: the marks '6' and '8' are counted in trace.c.
         */
        if (shim_report_active()) {
            shim_trace_add(0x88f880, "6the game update");
            shim_trace_add(0x88f770, "8the interpolation pass");
        }

        /*
         * Traces on the chain we took apart: the engine setup, the XML loader,
         * the mounting, the mode entry and the frame path. Every trace PATCHES
         * the first instructions of a game function, so in the private build
         * there are none at all.
         *
         * And for the same reason there are NO traces inside the switching table
         * here (there were some at 0x6e3644, 0x6e3464, 0x6e3484). A trace takes
         * five words, and the table's blocks are three each; the fifth word
         * lands on the common tail 0x6e364c and wrecks both our item and AIM
         * LOCK. Who is who in that table is now known from reading the code, not
         * from what fired.
         */
        /*
         * The daily run: a short wrapper that leaves by a tail branch into the
         * launch (0x670fb0). Checked with into.py — the only branch aims at the
         * start, nothing falls inside the five words.
         */
        /*
         * The sign ^ means print WHERE the call came from, up to twelve times.
         *
         * It stands on the function that writes the run seed: it fires only at a
         * REAL start of a run. The trace used to stand at 0x670fb0, but a
         * measurement showed that one is called every frame from the shell
         * update (0x676b04) and has nothing to do with the start of a run.
         */
        shim_trace_add(0x88b610, ">run start");
        /* the "start a run" request: they write the flag at root+0x1f6b40 */
        shim_trace_add(0x672284, "Sthe run request");
        shim_trace_add(0x88b144, "Sthe run starts");
        shim_trace_add(0x12f52c, "Sgame start A");
        shim_trace_add(0x12f70c, "Sgame start B");
        shim_trace_add(0x12f9d8, "Sgame start C");
        /*
         * The handler of the deferred work that a tap on DAILY RUN creates. It
         * is right after this that the run should start: for now the menu turns
         * on the loading state, draws "Loading" and waits.
         */
        shim_trace_add(0x62d254, "^run task handler");
        /* the second stage: the first one, having finished, creates this */
        shim_trace_add(0x62d374, "^run task handler, stage 2");
        shim_trace_add(0x769b88, "^screen switcher");
        shim_trace_add(0x4c78, "edition: store poll");
        shim_trace_add(0x06ab4c, "stick: capture, Rebirth");
        shim_trace_add(0x24e47c, "stick: capture, Afterbirth");
        shim_trace_add(0x861b40, "stick: capture, Repentance");
        shim_trace_add(0x885fb4, "engine setup");
        shim_trace_add(0x676a20, "setup wrapper");
        shim_trace_add(0x6754f0, "xml loader");
        /* the three ways a request for a run is filled in: which one a pad reset uses */
        shim_trace_add(0x672438, ">request: fresh, kind 1");
        shim_trace_add(0x67248c, ">request: fresh, kind 2");
        shim_trace_add(0x6724dc, ">request: load a save");
        shim_trace_add(0x88674c, "mounting");
        shim_trace_add(0x475c, "!shell: creation");
        shim_trace_add(0x8c86c8, "frame: frame start");
        shim_trace_add(0x16c250, "frame: drawing the world");
        shim_trace_add(0x286f54, "frame: updating the world");
        shim_trace_add(0x287604, "frame: presenting the world");
        shim_trace_add(0x1f3ea8, ">mode entry");
        shim_trace_add(0x16beb0, ">entry step 1");
        shim_trace_add(0x22b5dc, "  entry: step 2");
        shim_trace_add(0x8b576c, "  entry: step 3");
        shim_trace_add(0x8e1210, "  entry: step 4");
        shim_trace_add(0x16bfdc, ">entry step 5");
        shim_trace_add(0x16c188, ">launch step 1");
        shim_trace_add(0x8b1334, "!shader build");
        /*
         * Opening a file by the game's own means. The shader never reaches the
         * disk: there is neither an open nor an fopen for it. So it breaks off
         * earlier — we look at which path it is called with and what the path
         * parsing does.
         */
        shim_trace_add(0x8b1264, "!reading a whole file");
        shim_trace_add(0x8dfb00, "!file system: open");
        shim_trace_add(0x8ab6d4, "!file system: path parsing");
        /* by the time the shaders are built the table holds one entry: whose is it? */
        shim_trace_add(0x8aafbc, "!entry point added");
#endif
        /*
         * The page holding the hook continuations MUST become executable, and
         * that is outside reconnaissance: three hooks that carry behaviour stand
         * in the private build as well. While the sealing sat inside ISAAC_DIAG,
         * the release died with "trying to execute non-executable memory" on the
         * very first of them.
         */
        shim_trace_seal();
        /* every mark and the hooks that carry it, so a clash cannot hide */
        { void shim_trace_marks(void (*)(const char *, const char *));
          if (shim_report_active()) shim_trace_marks(report_line); }

        protect(&im);
        /*
         * In newer images the constructors are stored not as pointers but as
         * 32-bit offsets from the base: the section __TEXT,__init_offsets. That
         * way they need no fixing up when relocated, which is why the section
         * lives in __TEXT, read-only.
         */
        const struct section_64 *mi = find_sect(&im, "__TEXT", "__init_offsets");
        if (mi) {
            uint32_t *offs = (uint32_t *) addr_to_mem(&im, mi->addr);
            int n = (int) (mi->size / 4);
            LOG("static constructors: %d — running them", n);
            for (int i = 0; i < n; i++) {
                void (*fn)(int, char **, char **, char **) =
                        (void (*)(int, char **, char **, char **)) (im.base + offs[i]);
                LOG("  constructor %d/%d, offset %x", i + 1, n, offs[i]);
                fn(0, NULL, NULL, NULL);
                inits++;
            }
            LOG("constructors done: %d, stubs touched: %llu",
                inits, (unsigned long long) g_swamp_hits);
            if (shim_report_active()) self_test(&im, inits);

            /*
             * The entry point. On an iOS app main calls UIApplicationMain and
             * never returns; ours will return, because the event loop belongs to
             * Android.
             */
            if (im.entry) {
                int (*mainfn)(int, char **, char **, char **) =
                        (int (*)(int, char **, char **, char **)) (im.base + im.entry);
                static char arg0[] = "isaac";
                static char *argv_[] = {arg0, NULL};
                static char *envp_[] = {NULL};
                static char *apple_[] = {NULL};
                LOG("calling the entry point at offset %llx", (unsigned long long) im.entry);
                int rc = mainfn(1, argv_, envp_, apple_);
                LOG("the entry point returned %d, messages %ld (no method %ld), stubs touched %llu",
                    rc, shim_objc_messages(), shim_objc_missed(), (unsigned long long) g_swamp_hits);
            }
        } else {
            LOG("there is no section with constructors");
        }
    }

    snprintf(report, sizeof(report),
             "image: %s\n"
             "entry point: offset %llx\n"
             "fixups: %s\n"
             "foreign names resolved: %d from Android, %d from the image itself, %d into the swamp\n"
             "Objective-C classes read: %d\n"
             "%s\n"
             "--- not found in Android ---\n%s",
             okmap ? "landed" : "DID NOT LAND",
             (unsigned long long) im.entry,
             okfix ? "applied" : "NOT APPLIED",
             st.from_android, st.from_self, st.swamped, classes, names, missing);

    (*env)->ReleaseByteArrayElements(env, blob, bytes, JNI_ABORT);
    LOG("%s", report);
    return (*env)->NewStringUTF(env, report);
}

/* the app went into the background — the sound goes quiet with it */
void shim_audio_pause(int on);

JNIEXPORT void JNICALL
Java_ru_isaac_port_MainActivity_soundPause(JNIEnv *env, jobject thiz, jboolean on) {
    (void) env; (void) thiz;
    shim_audio_pause(on ? 1 : 0);
}

/* the folder holding the stick settings: Java names it */
/*
 * The asset manager from Java. The resource table is built straight from the
 * APK through it, so this must be called BEFORE the game starts opening files.
 */
void shim_pack_open(void *manager, const char *root);

JNIEXPORT void JNICALL
Java_ru_isaac_port_MainActivity_setAssets(JNIEnv *env, jobject thiz,
                                          jobject assets, jstring root) {
    (void) thiz;
    if (!assets || !root) return;
    const char *s = (*env)->GetStringUTFChars(env, root, NULL);
    {
        AAssetManager *mgr = AAssetManager_fromJava(env, assets);
        shim_pack_open(mgr, s);
        /* the port's additions to the game's files, laid over the pack (portpatch.c, M8) */
        { void shim_portpatch_open(void *); shim_portpatch_open(mgr); }
        /* the numbers of the layers the port draws by hand, read from the files just laid */
        { void shim_straps_layers(void); shim_straps_layers(); }
        /* the item descriptions live beside the pack, in the same assets */
        { void shim_eid_open(void *); shim_eid_open(mgr); }
        { void shim_marked_open(void *); shim_marked_open(mgr); }
        { void shim_touchedit_open_assets(void *); shim_touchedit_open_assets(mgr); }
        { void shim_credits_open_assets(void *); shim_credits_open_assets(mgr); }
        { void shim_tweaks_open_assets(void *); shim_tweaks_open_assets(mgr); }
        { void shim_mods_open_assets(void *); shim_mods_open_assets(mgr); }
    }
    (*env)->ReleaseStringUTFChars(env, root, s);
}

/* the state of one gamepad slot from Java: buttons as bits, axes as an array (pad.c) */
void shim_pad_state(int, int, unsigned, const float *, int);

JNIEXPORT void JNICALL
Java_ru_isaac_port_MainActivity_padState(JNIEnv *env, jobject thiz, jint slot, jboolean present,
                                         jint buttons, jfloatArray axes) {
    (void) thiz;
    jfloat buf[16];
    jsize n = axes ? (*env)->GetArrayLength(env, axes) : 0;
    if (n > (jsize) (sizeof(buf) / sizeof(buf[0]))) n = sizeof(buf) / sizeof(buf[0]);
    if (n > 0) (*env)->GetFloatArrayRegion(env, axes, 0, n, buf);
    shim_pad_state((int) slot, present ? 1 : 0, (unsigned) buttons, n > 0 ? buf : NULL, (int) n);
}

/* a slot's family of pad pictures and the vendorName iOS gives such a pad (Java PadIcons, pad.c) */
void shim_pad_family(int slot, int family, const char *ios_name);

JNIEXPORT void JNICALL
Java_ru_isaac_port_MainActivity_padFamily(JNIEnv *env, jobject thiz, jint slot, jint family, jstring name) {
    (void) thiz;
    const char *s = name ? (*env)->GetStringUTFChars(env, name, NULL) : NULL;
    shim_pad_family((int) slot, (int) family, s ? s : "");
    if (s) (*env)->ReleaseStringUTFChars(env, name, s);
}

/*
 * The bridge into Java. Until now the layer only took calls from above; the
 * keyboard, though, needs the way back: the game asks for it from its own
 * thread, and only Android can show it. So we keep the Java machine and a
 * reference to the activity, and attach the thread on a call — the game thread
 * was not created by Java and has no environment of its own.
 */
static JavaVM *g_vm;
static jobject g_activity;
static jmethodID g_show_keyboard;
static jmethodID g_action;          /* MainActivity.portAction(what): Tweaks' actions */
static jmethodID g_action_text;     /* MainActivity.portActionText(what, text): a user mod's */

JNIEXPORT jint JNICALL JNI_OnLoad(JavaVM *vm, void *reserved) {
    (void) reserved;
    g_vm = vm;
    return JNI_VERSION_1_6;
}

JNIEXPORT void JNICALL
Java_ru_isaac_port_MainActivity_setReport(JNIEnv *env, jobject thiz, jint fd) {
    (void) env; (void) thiz;
    void shim_set_report_fd(int);
    shim_set_report_fd((int) fd);
}

JNIEXPORT void JNICALL
Java_ru_isaac_port_MainActivity_imeInit(JNIEnv *env, jobject thiz) {
    if (g_activity) return;
    g_activity = (*env)->NewGlobalRef(env, thiz);
    jclass cls = (*env)->GetObjectClass(env, thiz);
    g_show_keyboard = (*env)->GetMethodID(env, cls, "showKeyboard", "(Z)V");
    g_action = (*env)->GetMethodID(env, cls, "portAction", "(I)V");
    if ((*env)->ExceptionCheck(env)) { (*env)->ExceptionClear(env); g_action = 0; }
    g_action_text = (*env)->GetMethodID(env, cls, "portActionText", "(ILjava/lang/String;)V");
    if ((*env)->ExceptionCheck(env)) { (*env)->ExceptionClear(env); g_action_text = 0; }
}

void shim_ime_show(int show) {
    if (!g_vm || !g_activity || !g_show_keyboard) return;
    JNIEnv *env = NULL;
    int attached = 0;
    if ((*g_vm)->GetEnv(g_vm, (void **) &env, JNI_VERSION_1_6) != JNI_OK) {
        if ((*g_vm)->AttachCurrentThread(g_vm, &env, NULL) != JNI_OK) return;
        attached = 1;
    }
    (*env)->CallVoidMethod(env, g_activity, g_show_keyboard, show ? JNI_TRUE : JNI_FALSE);
    if ((*env)->ExceptionCheck(env)) (*env)->ExceptionClear(env);
    if (attached) (*g_vm)->DetachCurrentThread(g_vm);
}

/* one of the Tweaks page's actions (tweaks.c): 1 the config file, 2 import saves, 3 export */
void shim_port_action(int what) {
    if (!g_vm || !g_activity || !g_action) return;
    JNIEnv *env = NULL;
    int attached = 0;
    if ((*g_vm)->GetEnv(g_vm, (void **) &env, JNI_VERSION_1_6) != JNI_OK) {
        if ((*g_vm)->AttachCurrentThread(g_vm, &env, NULL) != JNI_OK) return;
        attached = 1;
    }
    (*env)->CallVoidMethod(env, g_activity, g_action, (jint) what);
    if ((*env)->ExceptionCheck(env)) (*env)->ExceptionClear(env);
    if (attached) (*g_vm)->DetachCurrentThread(g_vm);
}

/* an action about one of the player's mods (tweaks.c): 5 export it to a folder; `text` is its id */
void shim_port_action_text(int what, const char *text) {
    if (!g_vm || !g_activity || !g_action_text || !text) return;
    JNIEnv *env = NULL;
    int attached = 0;
    if ((*g_vm)->GetEnv(g_vm, (void **) &env, JNI_VERSION_1_6) != JNI_OK) {
        if ((*g_vm)->AttachCurrentThread(g_vm, &env, NULL) != JNI_OK) return;
        attached = 1;
    }
    jstring s = (*env)->NewStringUTF(env, text);
    if (s) {
        (*env)->CallVoidMethod(env, g_activity, g_action_text, (jint) what, s);
        (*env)->DeleteLocalRef(env, s);
    }
    if ((*env)->ExceptionCheck(env)) (*env)->ExceptionClear(env);
    if (attached) (*g_vm)->DetachCurrentThread(g_vm);
}

/* a shake the game asks for goes to Haptics.java through rumble.c's own bridge (batch 3) */

JNIEXPORT void JNICALL
Java_ru_isaac_port_MainActivity_imeText(JNIEnv *env, jobject thiz, jstring text) {
    (void) thiz;
    if (!text) return;
    const char *s = (*env)->GetStringUTFChars(env, text, NULL);
    if (s) shim_ime_push(s);
    (*env)->ReleaseStringUTFChars(env, text, s);
}

/* port-touch.ini was edited in the file manager: read it again (sticks.c) */
JNIEXPORT void JNICALL
Java_ru_isaac_port_MainActivity_configChanged(JNIEnv *env, jobject thiz) {
    (void) env; (void) thiz;
    void shim_sticks_reload_ask(void);
    shim_sticks_reload_ask();
}

JNIEXPORT void JNICALL
Java_ru_isaac_port_MainActivity_imeBackspace(JNIEnv *env, jobject thiz) {
    (void) env; (void) thiz;
    shim_ime_push("");           /* the empty string is the delete: no letter is ever empty */
}

JNIEXPORT void JNICALL
Java_ru_isaac_port_MainActivity_appPause(JNIEnv *env, jobject thiz) {
    (void) env; (void) thiz;
    shim_app_pause();
}

JNIEXPORT void JNICALL
Java_ru_isaac_port_MainActivity_appResume(JNIEnv *env, jobject thiz) {
    (void) env; (void) thiz;
    shim_app_resume();
}

JNIEXPORT void JNICALL
Java_ru_isaac_port_MainActivity_setSavesDir(JNIEnv *env, jobject thiz, jstring path) {
    (void) thiz;
    if (!path) return;
    const char *s = (*env)->GetStringUTFChars(env, path, NULL);
    shim_set_saves(s);
    (*env)->ReleaseStringUTFChars(env, path, s);
}

JNIEXPORT void JNICALL
Java_ru_isaac_port_MainActivity_setConfigDir(JNIEnv *env, jobject thiz, jstring path) {
    (void) thiz;
    if (!path) return;
    const char *s = (*env)->GetStringUTFChars(env, path, NULL);
    shim_sticks_load(s);
    shim_probe_setup(s);            /* field reconnaissance: the commands next to the settings */
    { void shim_usermods_setup(const char *); shim_usermods_setup(s); }   /* the player's resource mods */
    (*env)->ReleaseStringUTFChars(env, path, s);
}

/* touches from Java: put into a queue, the frame thread will drain it */
void shim_touch_push(int id, int phase, float x, float y);

JNIEXPORT void JNICALL
Java_ru_isaac_port_MainActivity_touch(JNIEnv *env, jobject thiz,
                                      jint id, jint phase, jfloat x, jfloat y) {
    (void) env; (void) thiz;
    shim_touch_push(id, phase, x, y);
}

/* the surface from Java: we hold the window while it is alive */
JNIEXPORT void JNICALL
Java_ru_isaac_port_MainActivity_setSurface(JNIEnv *env, jobject thiz, jobject surface) {
    /*
     * The order matters: first the drawing lets go of the old window (the EGL
     * surface dies inside), and only then do we hand it back to the system. The
     * other way round is a crash on a screen rotation.
     */
    static ANativeWindow *win;
    ANativeWindow *fresh = surface ? ANativeWindow_fromSurface(env, surface) : NULL;
    shim_gl_set_window(fresh);
    if (win) ANativeWindow_release(win);
    win = fresh;
    LOG("surface from Java: %p", (void *) win);
}
