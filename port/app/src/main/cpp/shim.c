/*
 * A stand-in for iOS: everything Android does not provide by itself.
 *
 * Filled in on demand, never in advance. Reconnaissance shows what the game
 * really calls, and exactly that lands here — a stub that gets called even
 * once becomes real.
 */
#include <stdint.h>
#include <time.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdarg.h>
#include <sys/stat.h>
#include <dirent.h>
#include <pthread.h>
#include <errno.h>
#include <stdlib.h>
#include <android/log.h>
#include "diag.h"

#if ISAAC_DIAG
#define LOG(...) __android_log_print(ANDROID_LOG_INFO, "isaac", __VA_ARGS__)
#else
#define LOG(...) do { } while (0)      /* the private build has no log */
#endif

/* ---------- where the game keeps its home ---------- */

static char g_home[512] = "/data/local/tmp";

/*
 * The same path in the shape the game expects.
 *
 * While building a path the engine checks whether it starts with /private or
 * /Users/ — on the iPhone the sandbox lives under /private/var, on the Mac
 * under /Users. If it is neither, the path counts as incomplete and the
 * resource folder gets glued onto it. Our /data/user/0/... fits nothing, so
 * the root was substituted twice and saves went to an address that does not
 * exist.
 *
 * So outwards the sandbox is shown with a /private prefix, and at the border
 * of the system calls that prefix is stripped. This is not a fix for one
 * call: every other check of the kind we have not met yet works the same way.
 */
static char g_home_shown[560];

/*
 * Whether the profile was clean at startup.
 *
 * Looking at the settings file later is no good: the game creates it itself,
 * before the menu is even drawn. The "if the file is missing" test in our
 * defaults therefore NEVER fired — by then the file already existed. So the
 * answer is remembered once, here, before the game starts.
 */
static int g_profile_clean = -1;

const char *shim_saves_dir(void);

/*
 * Has the player already settled the settings?
 *
 * This used to look under the home for Documents/Repentance/options.ini - the
 * place the file has on an iPhone. Here the saves are redirected, so nothing
 * was ever found there and the answer was always "clean": every launch we put
 * our own brightness back and threw away whatever had been chosen. The file is
 * where the game actually writes it, next to the save data.
 *
 * The answer is worked out on the first question rather than when the home is
 * set: by then the saves folder is known. While it is not, the safe answer is
 * "settled" - a launch with no defaults applied costs nothing, a launch that
 * overwrites the player's brightness costs their setting.
 */
/*
 * Has the player's own settings file appeared yet? Asked afresh every time.
 *
 * shim_profile_clean() answers once and keeps the answer, because the question
 * it settles - "was there a profile before the game ran" - has one answer for
 * the whole launch. This one is the opposite: the game writes options.ini
 * during startup, and our defaults are only ours until it does.
 */
int shim_profile_file_now(void) {
    const char *saves = shim_saves_dir();
    if (!saves || !*saves) return 0;
    char opts[640];
    void shim_safe_wait_path(const char *);
    snprintf(opts, sizeof(opts), "%s/Repentance/options.ini", saves);
    shim_safe_wait_path(opts);              /* the game's first write of it may still be on the writer's queue */
    if (access(opts, F_OK) == 0) return 1;
    snprintf(opts, sizeof(opts), "%s/options.ini", saves);
    shim_safe_wait_path(opts);
    return access(opts, F_OK) == 0;
}

int shim_profile_clean(void) {
    if (g_profile_clean < 0) {
        const char *saves = shim_saves_dir();
        if (!saves || !*saves) return 0;
        char opts[640];
        snprintf(opts, sizeof(opts), "%s/Repentance/options.ini", saves);
        int missing = (access(opts, F_OK) != 0);
        if (missing) {
            snprintf(opts, sizeof(opts), "%s/options.ini", saves);
            missing = (access(opts, F_OK) != 0);
        }
        g_profile_clean = missing;
        LOG("profile: %s", g_profile_clean ? "clean, our defaults apply"
                                           : "settings already there, defaults left alone");
    }
    return g_profile_clean > 0;
}

void shim_set_home(const char *path) {
    if (path) { strncpy(g_home, path, sizeof(g_home) - 1); g_home[sizeof(g_home) - 1] = 0; }
    snprintf(g_home_shown, sizeof(g_home_shown), "/private%s", g_home);
    LOG("game home: %s (shown to the game as %s)", g_home, g_home_shown);
}

/* ---------- Darwin quirks ---------- */

/*
 * On Apple the address of errno comes from __error, on Android from __errno.
 * The names differ, and without this line the game got nothing back and died
 * on the very first write to errno — right before opening its first file.
 */
int *shim_error(void) { return &errno; }

/*
 * bzero is not exported by bionic — it is inlined there. The game calls it as
 * an ordinary function, and without it object fields stay full of garbage.
 */
void shim_bzero(void *p, size_t n) { memset(p, 0, n); }

/* ---------- a tiny CoreFoundation ---------- */

/*
 * A real CFURL is not needed: the game takes the home folder and turns it into
 * a path at once. So an "object" here is just a string with a tag, and
 * CFRelease lets it go.
 */
typedef struct { uint32_t tag; char path[512]; } cf_url_t;
#define CF_URL_TAG 0x43465552u      /* 'CFUR' */

/*
 * The application bundle. On iOS resources live inside the .app and the game
 * gets their path through CFBundle. Ours are unpacked next to the app, so the
 * bundle is only a tag and the path is the whole point.
 */
static char g_bundle[512];
static const uint32_t CF_BUNDLE_TAG = 0x43464244u;      /* 'CFBD' */
static uint32_t g_bundle_obj = 0x43464244u;

const char *shim_home_dir(void) { return g_home; }

/*
 * The save folder that was opened. Empty means none was set and everything
 * goes the old way, into the game home: the port must work without it.
 */
static char g_saves[512];

void shim_set_saves(const char *path) {
    if (!path || !*path) return;
    strncpy(g_saves, path, sizeof(g_saves) - 1);
    g_saves[sizeof(g_saves) - 1] = 0;
    LOG("saves live in %s", g_saves);
    /*
     * Ask now, while the answer still means something.
     *
     * The question is "has the player settled the settings", and the only
     * honest moment to ask it is before the game runs: the game writes
     * options.ini of its own accord at startup, so anyone asking later - our
     * defaults did, from the first frame of the options menu - is told "yes,
     * settled" on a brand new install, and the defaults quietly stop applying.
     * That is exactly how they were lost: the fix for "brightness comes back
     * as ours every launch" corrected WHERE we look, and the lateness of the
     * question then turned the defaults off altogether.
     */
    /*
     * Before the game reads a byte (P1, P4, P6): writes that broke off are
     * finished or dropped, the folder is asked how it takes a rename, and the
     * saves and options.ini are checked against the port's own backup -
     * missing, empty or unreadable ones come back from it. All of that has to
     * be settled before the question below, or a restored options.ini would
     * still count as "clean".
     */
    {
        void shim_safe_recover_dir(const char *);
        int shim_storage_probe(const char *);
        void shim_saves_startup(const char *);
        char rep[560];
        snprintf(rep, sizeof(rep), "%s/Repentance", g_saves);
        mkdir(rep, 0770);
        shim_safe_recover_dir(g_saves);
        shim_safe_recover_dir(rep);
        shim_storage_probe(rep);
        shim_saves_startup(g_saves);
    }
    shim_profile_clean();
}

const char *shim_saves_dir(void) { return g_saves[0] ? g_saves : NULL; }
const char *shim_home_shown(void) { return g_home_shown; }

void shim_set_bundle(const char *path) {
    if (!path) return;
    strncpy(g_bundle, path, sizeof(g_bundle) - 1);
    g_bundle[sizeof(g_bundle) - 1] = 0;
    /*
     * Move into that folder as well: the game builds part of its paths from
     * the current directory rather than from the bundle, and this way they
     * line up by themselves.
     */
    if (chdir(g_bundle) != 0) LOG("could not change into %s", g_bundle);
    LOG("game resources: %s", g_bundle);
}

void *shim_CFBundleGetMainBundle(void) { return &g_bundle_obj; }

void *shim_CFCopyHomeDirectoryURL(void) {
    cf_url_t *u = (cf_url_t *) calloc(1, sizeof(cf_url_t));
    if (!u) return NULL;
    u->tag = CF_URL_TAG;
    strncpy(u->path, g_home_shown, sizeof(u->path) - 1);
    DIAG(static int told; if (told < 4) { told++; LOG("game asked for the home folder -> %s", u->path); });
    return u;
}

int shim_CFURLGetFileSystemRepresentation(void *url, int resolve, uint8_t *buf, long len) {
    (void) resolve;
    cf_url_t *u = (cf_url_t *) url;
    if (!u || u->tag != CF_URL_TAG || !buf || len <= 0) return 0;
    size_t n = strlen(u->path);
    if ((long) n >= len) return 0;
    memcpy(buf, u->path, n + 1);
    DIAG(static int told; if (told < 4) { told++;
         LOG("path handed to the game: %s (buffer %p, length %ld, written %zu)", u->path, (void *) buf, len, n + 1); });
    return 1;
}

void *shim_CFBundleCopyResourcesDirectoryURL(void *bundle) {
    (void) bundle;
    cf_url_t *u = (cf_url_t *) calloc(1, sizeof(cf_url_t));
    if (!u) return NULL;
    u->tag = CF_URL_TAG;
    strncpy(u->path, g_bundle[0] ? g_bundle : ".", sizeof(u->path) - 1);
    DIAG(static int told; if (told < 4) { told++; LOG("game asked for the resource folder -> %s", u->path); });
    return u;
}

void shim_CFRelease(void *o) {
    cf_url_t *u = (cf_url_t *) o;
    if (u && u->tag == CF_URL_TAG) { u->tag = 0; free(u); }
    /* other objects are not ours yet — nothing to release */
}

/* ---------- autorelease pool ---------- */

/*
 * A real pool holds objects until the end of the turn and then releases them.
 * We own nothing yet, so the pool is a pair of empty braces. Once there is an
 * Objective-C runtime here, a counter and a list will appear.
 */
void *shim_objc_autoreleasePoolPush(void) { return NULL; }
void shim_objc_autoreleasePoolPop(void *pool) { (void) pool; }

/* ---------- blocks ---------- */

/*
 * A block is an object: the first field is isa, the third is the address of
 * its body. Real block classes are not needed, only a usable pointer, because
 * the game merely compares them and passes them on.
 */
void *shim_NSConcreteGlobalBlock[8];
void *shim_NSConcreteStackBlock[8];

void shim_Block_object_assign(void *dst, const void *src, int flags) {
    (void) dst; (void) src; (void) flags;
}
void shim_Block_object_dispose(const void *o, int flags) { (void) o; (void) flags; }

typedef struct { void *isa; int flags, reserved; void (*invoke)(void *); } block_t;

static void run_block(void *b) {
    block_t *blk = (block_t *) b;
    if (blk && blk->invoke) blk->invoke(blk);
}

/* ---------- queues ---------- */

/*
 * Queues are serial and immediate for now: the work runs right here, on the
 * same thread. For reconnaissance that is more honest than threading — the
 * order of events stays the one the engine intended, and crashes do not drift.
 */
void *shim_dispatch_main_q[16];

void shim_dispatch_once(long *pred, void *block) {
    if (!pred || *pred) return;
    *pred = 1;
    run_block(block);
}

void shim_dispatch_async(void *queue, void *block) {
    (void) queue;
    run_block(block);
}

void *shim_dispatch_queue_create(const char *label, void *attr) {
    (void) attr;
    LOG("queue: %s (serial and immediate for now)", label ? label : "?");
    static long tag;
    return (void *) (uintptr_t) (0xD1500000u + (unsigned) (++tag));
}

void shim_NSLog(void *fmt, ...) { (void) fmt; LOG("NSLog from the game"); }

/*
 * The game's own assertion. Its text is the most valuable thing we get at such
 * a moment: it names both the condition and the place in the Nicalis sources.
 */
void shim_assert_rtn(const char *func, const char *file, int line, const char *expr) {
    LOG("GAME ASSERTION FAILED: %s", expr ? expr : "?");
    LOG("   in %s, file %s, line %d", func ? func : "?", file ? file : "?", line);
    abort();
}

/* ---------- listening in on file access ---------- */

/*
 * The point is not to replace the file work but to hear it: which paths the
 * engine really tries. So there is only a log line here and a hand-off to the
 * real work — the same trick that has already found three missing pieces.
 */
static int g_told_paths;
#define PATHS_MAX 40

static void tell_path(const char *what, const char *path, int ok) {
    if (g_told_paths >= PATHS_MAX || !path) return;
    g_told_paths++;
    LOG("%s %s -> %s", what, path, ok ? "yes" : "no");
    if (g_told_paths == PATHS_MAX) LOG("(paths no longer logged)");
}

const char *shim_map_path(const char *p);

/*
 * Resources live inside the APK and are never unpacked to disk, so every file
 * call asks them first. A miss means the path is not a resource one (our own
 * saves, the port settings), and everything goes on to the real file system,
 * as before.
 */
FILE *shim_pack_fopen(const char *, const char *);
int shim_pack_open_fd(const char *, int);
int shim_pack_stat(const char *, void *);
int shim_pack_exists(const char *);
void *shim_pack_opendir(const char *);
void *shim_pack_readdir(void *);
int shim_pack_is_our_walk(void *);
int shim_pack_closedir(void *);

/*
 * open, with the flags translated.
 *
 * The numbers differ: Apple has O_CREAT 0x200, O_TRUNC 0x400, O_APPEND 0x008;
 * bionic has 0x40, 0x200, 0x400. Passing them straight through turned "create"
 * into "truncate" and "truncate" into "append" - the image does not import
 * open today, so it never fired, but the trap was set.
 */
static int apple_open_flags(int f) {
    int out = f & 3;                       /* the access mode is the same in both */
    if (f & 0x000008) out |= O_APPEND;
    if (f & 0x000004) out |= O_NONBLOCK;
    if (f & 0x000200) out |= O_CREAT;
    if (f & 0x000400) out |= O_TRUNC;
    if (f & 0x000800) out |= O_EXCL;
    if (f & 0x100000) out |= O_DIRECTORY;
    if (f & 0x1000000) out |= O_CLOEXEC;
    return out;
}

int shim_open(const char *path, int flags, int mode) {
    const char *p = shim_map_path(path);
    { extern unsigned long g_pack_asked_from, g_pack_asked_from2;
      g_pack_asked_from = (unsigned long) __builtin_return_address(0);
      g_pack_asked_from2 = (unsigned long) __builtin_return_address(1); }
    int fd = shim_pack_open_fd(p, flags & 3);
    if (fd >= 0) return fd;
    void shim_safe_wait_path(const char *);
    shim_safe_wait_path(p);
    fd = open(p, apple_open_flags(flags), mode);
    tell_path("open", p, fd >= 0);
    return fd;
}

unsigned long shim_image_base(void);
void shim_probe_who(const char *tag);

const char *shim_save_alias(const char *path, const char *mode);
FILE *shim_save_mended(FILE *f, const char *path, const char *mode);

/*
 * Reading a stream, counted.
 *
 * The PNG decoder is linked into the image and pulls its bytes through fread
 * from the stream our pack hands it. If it asks in small pieces, every one of
 * them costs a lock and a call, and thousands of them are how an unpack turns
 * into eighty milliseconds. Counted rather than guessed: calls and bytes,
 * side by side in the stall line.
 */
long g_fread_calls, g_fread_bytes;

size_t shim_fread(void *ptr, size_t size, size_t n, FILE *f) {
    g_fread_calls++;
    size_t got = fread(ptr, size, n, f);
    g_fread_bytes += got * size;
    return got;
}

/*
 * ONE RULE FOR EVERY FILE THAT HOLDS THE PLAYER'S STATE (P1, 30.09.2026): the
 * writer - .tmp, fsync, rename, the .ready fallback, the start-up recovery -
 * lives in safefile.c, and since hotfix 1.72 with a queue and a thread of its
 * own for the game's writes (THE WRITER THREAD there).
 */
int shim_report_active(void);
void report_line(const char *, const char *);
const char *shim_saves_dir(void);

#if ISAAC_DIAG
#define SAY(kind, ...) do { if (shim_report_active()) { char say_[480];                            snprintf(say_, sizeof(say_), __VA_ARGS__); report_line(kind, say_); } } while (0)
#else
#define SAY(kind, ...) do { } while (0)
#endif

FILE *shim_safe_fopen(const char *path, const char *mode);
int shim_safe_fclose(FILE *f);
FILE *shim_safe_game_open(const char *path, const char *mode, const char *who);
void shim_safe_mark_failed(FILE *f);
void shim_safe_wait_path(const char *path);
void shim_safe_wait_dir(const char *dir);

/* the save guard and the settings guard (saves.c) */
void shim_saves_before_write(const char *path);
int shim_saves_frozen(const char *path);

__attribute__((unused)) static const char *leaf_of(const char *p) {
    const char *s = p ? strrchr(p, '/') : NULL;
    return s ? s + 1 : (p ? p : "?");
}

static int ends_with(const char *s, const char *tail) {
    size_t a = strlen(s), b = strlen(tail);
    return a > b && !strcmp(s + a - b, tail);
}

/* ---------- the game's own writes ---------- */

static int is_save_file(const char *p) {
    return p && (strstr(p, "persistentgamedata") || strstr(p, "gamestate"));
}

static int in_saves(const char *p) {
    const char *s = shim_saves_dir();
    size_t n = s ? strlen(s) : 0;
    return p && n && !strncmp(p, s, n) && p[n] == '/';
}

/*
 * WHO WRITES IT, for the settings guard (report builds): the return addresses
 * up the game's frame chain, as image offsets. Bounded by the thread's own
 * stack, so a broken chain ends the walk rather than the process.
 */
#if ISAAC_DIAG
static void chain_of(char *out, size_t room) {
    out[0] = 0;
    unsigned long base = shim_image_base();
    pthread_attr_t attr;
    void *lo = NULL;
    size_t size = 0;
    if (!base || pthread_getattr_np(pthread_self(), &attr) != 0) return;
    pthread_attr_getstack(&attr, &lo, &size);
    pthread_attr_destroy(&attr);
    unsigned long top = (unsigned long) lo + size;
    unsigned long fp = (unsigned long) __builtin_frame_address(0);
    size_t n = 0;
    int got = 0;
    for (int i = 0; i < 16 && got < 5; i++) {
        if (fp < (unsigned long) lo || fp + 16 > top || (fp & 7)) break;
        unsigned long next = ((const unsigned long *) fp)[0], lr = ((const unsigned long *) fp)[1];
        if (lr > base && lr - base < 0x1000000 && n < room) {
            n += (size_t) snprintf(out + n, room - n, "%s%lx", got ? "<" : "", lr - base);
            got++;
        }
        if (next <= fp) break;
        fp = next;
    }
}
#endif

/*
 * Everything the game opens for writing inside the saves folder goes through
 * the one writer: the saves, the run in progress, options.ini and
 * inputconfigs.dat (the keyboard's bindings). Only plain "w"/"wb" - anything
 * that reads as well passes by untouched.
 */
static FILE *game_write_open(const char *p, const char *mode) {
    if (!p || !mode || mode[0] != 'w' || strchr(mode, '+')) return NULL;
    if (!in_saves(p) && !is_save_file(p)) return NULL;
    if (ends_with(p, ".tmp") || ends_with(p, ".ready")) return NULL;
    if (shim_saves_frozen(p)) {
        SAY("saves", "%s: the game's write is let go to nowhere - an import is on its way in", leaf_of(p));
        return fopen("/dev/null", mode);
    }
    shim_saves_before_write(p);             /* waits for the queue itself, only when it reads the disk */
    char who[96] = "the game";
#if ISAAC_DIAG
    if (shim_report_active() && !is_save_file(p)) chain_of(who, sizeof(who));
#endif
    {   /* the port's own call of the game's writer names itself (saves.c options_write_now) */
        const char *shim_saves_who(const char *);
        const char *o = shim_saves_who(NULL);
        if (o) snprintf(who, sizeof(who), "%s", o);
    }
    /* into memory, and onto the writer's queue at fclose (safefile.c, THE WRITER THREAD) */
    return shim_safe_game_open(p, mode, who);
}

size_t shim_fwrite(const void *ptr, size_t size, size_t n, FILE *f) {
    size_t put = fwrite(ptr, size, n, f);
    if (put < n) shim_safe_mark_failed(f);
    return put;
}

int shim_fclose(FILE *f) {
    return shim_safe_fclose(f);
}

/*
 * WHICH CHARACTER AND COSTUME PICTURES OPEN, AND WHICH DO NOT. Report builds
 * only (the owner's list of 16.09, item 7: no item changes Azazel's look).
 * A costume that shows on nobody, or only on some characters, is either a
 * file that is asked for and not found or one that is never asked for at all;
 * this tells the two apart, whatever the costume.
 */
static void tell_character_file(const char *p, int found, unsigned long from) {
    int shim_report_active(void);
    void report_line(const char *, const char *);
    static int opened, missed, azazel;
    if (!p || !strstr(p, "gfx/characters") || !shim_report_active()) return;
    /* Azazel's own files have their own count: the menu's characters spent
       the common one before any run began (the eighth round's report) */
    if (strcasestr(p, "azazel")) {
        if (azazel >= 40) return;
        azazel++;
    } else {
        if (found ? opened >= 60 : missed >= 60) return;
        if (found) opened++; else missed++;
    }
    const char *leaf = strstr(p, "gfx/characters");
    char line[240];
    snprintf(line, sizeof(line), "%s %s (from +0x%lx)", found ? "opened" : "NOT FOUND", leaf, from);
    report_line("costume", line);
}

FILE *shim_fopen(const char *path, const char *mode) {
    const char *p = shim_map_path(path);
    /*
     * Whoever called US is the game, and that is the address worth keeping:
     * inside the pack the only caller visible is this function. The pack uses
     * it to name the code that reads a file it has already read.
     */
    /*
     * TWO levels, because one was not enough: the address just above us turned
     * out to be a single generic helper - "open this file, mode rb" - shared
     * by every subsystem, so it names nobody. The level above THAT is the
     * subsystem itself.
     */
    { extern unsigned long g_pack_asked_from, g_pack_asked_from2;
      g_pack_asked_from = (unsigned long) __builtin_return_address(0);
      g_pack_asked_from2 = (unsigned long) __builtin_return_address(1); }
    FILE *from_apk = shim_pack_fopen(p, mode);
    {
        unsigned long base0 = shim_image_base(), up = (unsigned long) __builtin_return_address(1);
        if (from_apk) tell_character_file(p, 1, base0 && up > base0 ? up - base0 : 0);
    }
    if (from_apk) return from_apk;
    FILE *whole = game_write_open(p, mode);
    if (whole) {
        void shim_saves_opened(const char *, const char *, int);
        if (in_saves(p)) shim_saves_opened(p, mode, 1);
        return whole;
    }
    shim_safe_wait_path(p);                 /* a write of it still queued: the game reads what it wrote */
    FILE *f = fopen(p, mode);
    /*
     * A save the player brought from the desktop build carries a different
     * name, and the game never looks for it. If its own is not there, saves.c
     * answers with one that will do; if its own IS there, we are never asked.
     */
    if (!f) {
        const char *instead = shim_save_alias(p, mode);
        if (instead) f = fopen(instead, mode);
    }
    /*
     * A save whose checksum or chunk sizes would make the game wipe it is
     * handed over mended, from memory (saves.c). A sound one is untouched.
     */
    FILE *mended = shim_save_mended(f, p, mode);
    if (mended) { fclose(f); f = mended; }
    /*
     * A progress file that is not there at all while the port's own backup
     * holds a sound copy of it (P6): the backup is handed over instead - the
     * game never deletes a save, so a missing one was lost, not thrown away.
     */
    if (!f) {
        FILE *shim_save_missing(const char *, const char *);
        f = shim_save_missing(p, mode);
    }
    {
        void shim_saves_opened(const char *, const char *, int);
        if (in_saves(p)) shim_saves_opened(p, mode, f != NULL);
    }
    /*
     * Who exactly opens it matters more than the path itself: the call goes
     * through a table of virtual functions, and plain reading will not find
     * it. The return address is turned into an image offset, which points at
     * the place in the game's code.
     */
    unsigned long back = (unsigned long) __builtin_return_address(0);
    unsigned long base = shim_image_base();
    /*
     * The cap here is not decoration: the game opens thousands of files, and
     * without it the log drowns in them before the first frame. FAILURES are
     * always written — the log exists for their sake.
     */
    if (!f) tell_character_file(p, 0, base && back > base ? back - base : back);
    static int told;
    if (!f || told < 60) {
        if (f) told++;
        LOG("fopen '%s' (%s) called from %06lx", p, f ? "opened" : "no",
            base && back > base ? back - base : back);
    }
    return f;
}

/*
 * stat is deliberately NOT offered.
 *
 * It used to be, and it filled the caller's buffer in Android's layout, while
 * a caller from this image expects Apple's - different order, different sizes,
 * a different place for every field. The image imports neither _stat nor
 * _lstat, so it never fired; but had it, the game would have read rubbish and
 * believed it.
 *
 * Answering nothing is better than answering wrongly: an import that is not
 * served lands in a named stub and says so in the log, which is a report, not
 * a silent lie. If it is ever needed, write it against Apple's struct stat -
 * st_dev int32, st_mode uint16, st_nlink uint16, st_ino uint64, then the ids,
 * st_rdev, four timespecs, and only then st_size.
 */

int shim_access(const char *path, int mode) {
    const char *p = shim_map_path(path);
    if (!(mode & W_OK) && shim_pack_exists(p)) return 0;
    shim_safe_wait_path(p);                 /* a first write still queued: the file is there for the game */
    int r = access(p, mode);
    tell_path("access", p, r == 0);
    return r;
}

/* ---------- Apple strings: the layout differs from Android ---------- */

void *shim_str_copy_ctor(void *, const void *);
void *shim_str_sub_ctor(void *, const void *, size_t, size_t, const void *);
void shim_str_dtor(void *);
void *shim_str_assign_str(void *, const void *);
void *shim_str_append(void *, const char *);
void *shim_str_append_n(void *, const char *, size_t);
void *shim_str_assign(void *, const char *);
void shim_str_push_back(void *, char);
void *shim_str_insert(void *, size_t, const char *);
void *shim_str_insert_n(void *, size_t, const char *, size_t);
void *shim_str_insert_fill(void *, size_t, size_t, char);
void *shim_str_erase(void *, size_t, size_t);
void shim_str_resize(void *, size_t, char);
void *shim_str_replace(void *, size_t, size_t, const char *);
void *shim_str_replace_n(void *, size_t, size_t, const char *, size_t);
void shim_str_to_string_abi(void);
void shim_str_concat_cs_abi(void);
void shim_strbuf_str(void *, const void *);
void shim_strbuf_str_get_abi(void);
int shim_str_compare_c(const void *, const char *);
int shim_str_compare_sub_c(const void *, size_t, size_t, const char *);
void *shim_strbuf_vtable(void);

/* the streams, laid out the Apple way — cxxstream.c */
void shim_ios_init(void *, void *);
void shim_ios_clear(void *, unsigned);
void shim_ios_badbit(void *);
void shim_istream_sentry(void *, void *, int);
void shim_ostream_sentry_ctor(void *, void *);
void shim_ostream_sentry_dtor(void *);
void *shim_ostream_put(void *, char);
void *shim_ostream_flush(void *);
void *shim_ostream_put_long(void *, long);
void shim_iostream_dtor(void *);
void shim_basic_ios_dtor(void *);
void shim_streambuf_ctor(void *);
void shim_streambuf_dtor(void *);
void shim_locale_dtor(void *);
void *shim_ctype_id(void);
void shim_ios_getloc_abi(void);
void *shim_use_facet(const void *, const void *);
void *shim_stringbuf_table(void);
void *shim_stringstream_table(void);
void *shim_stringstream_vtt(void);
void *shim_rs_get(void);
void shim_rs_dtor(void *);
unsigned shim_rs_call(void *);

#define STRK "__ZNKSt3__112basic_stringIcNS_11char_traitsIcEENS_9allocatorIcEEE"
#define SBUF "__ZNKSt3__115basic_stringbufIcNS_11char_traitsIcEENS_9allocatorIcEEE"

#define STR "__ZNSt3__112basic_stringIcNS_11char_traitsIcEENS_9allocatorIcEEE"

/*
 * Naming a thread. On Apple this function takes ONE argument and names the
 * current thread, on Android two, the thread and the name. The game calls it
 * the Apple way, and Android takes a random register for the name: the crash
 * lands inside strlen, far from the actual mistake.
 */
int shim_pthread_setname_np(const char *name) {
    return pthread_setname_np(pthread_self(), name ? name : "");
}

/* ---------- counting draw calls ---------- */

/*
 * Needed for exactly one question: does the game draw at all. The stubs count
 * and then call the real ones — those resolve through the ordinary Android
 * lookup.
 */
extern long g_gl_draws;
/*
 * HOW LONG EACH KIND OF GL CALL BLOCKS, per frame.
 *
 * The sampling profiler said the frame stands in __ioctl and stopped there: the
 * return register holds libc's own ioctl wrapper, and who called THAT is a
 * stack walk, which is not a thing to do inside a signal handler. But the layer
 * already stands in front of the calls that can block, so they can simply be
 * timed - and whichever of them holds the ioctl will show it (14.09.2026).
 *
 * A clock_gettime a call is cheap: it comes out of the vDSO without a syscall,
 * and a heavy frame makes a few hundred of these calls, not a few hundred
 * thousand.
 */
long long g_gl_ns_draw, g_gl_ns_clear, g_gl_ns_bindfb, g_gl_ns_tex;
/*
 * THE SHADER WORK OF A FRAME, counted and timed.
 *
 * The hitch does not follow the number of pixels - halving them changed nothing
 * - and it is a wait on the card while not one draw blocks. A shader being
 * compiled or linked fits all of that: it costs the same whatever the scene is
 * sized at, it happens when something NEW appears on the screen, which is
 * exactly when the owner sees it (many projectiles, a menu opening), and the
 * driver will put a barrier in the pipe for it.
 */
long g_gl_compiles, g_gl_links;
long long g_gl_ns_shader;

/*
 * The clock is a DIAGNOSTIC one. The wrappers stay in the build meant for
 * playing - taking them out of the table would change which code the game
 * calls, and that is not a thing to differ between builds - but they must not
 * pay for a measurement nobody is taking: glBindTexture alone is called
 * hundreds of times a frame. Without the diagnostics a wrapper is one extra
 * indirect call and nothing else.
 */
#if ISAAC_DIAG
/* the pass probe, gpu.c - report builds only */
void shim_gpu_pass_bind(unsigned target, unsigned fb);
void shim_gpu_pass_attach(unsigned target, unsigned att, unsigned tex);
void shim_gpu_pass_rb(unsigned target, unsigned att, unsigned rb);
void shim_gpu_pass_clear(unsigned mask);
void shim_gpu_pass_draw(int count);
void shim_gpu_pass_leave_bind(unsigned target, unsigned fb);
void shim_gpu_pass_leave_attach(unsigned target, unsigned att, unsigned tex);
void shim_gpu_pass_status(void);
void shim_gpu_pass_glw(int which);
void shim_gpu_pass_teximage(unsigned target, int level);
void shim_gpu_pass_deleted(int n, const unsigned *names);
#endif

#if ISAAC_DIAG
static long long gl_now(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (long long) ts.tv_sec * 1000000000LL + ts.tv_nsec;
}
#else
#define gl_now() 0LL
#endif

static void (*p_draw_arrays)(unsigned, int, int);
static void (*p_draw_elements)(unsigned, int, unsigned, const void *);
void *shim_android_symbol(const char *);

void shim_gl_note_arrays(int first, int count);

void shim_glDrawArrays(unsigned mode, int first, int count) {
    if (!p_draw_arrays) p_draw_arrays = (void (*)(unsigned, int, int)) shim_android_symbol("glDrawArrays");
    g_gl_draws++;
    { extern int g_gpu_on; void shim_gpu_draw(int); if (g_gpu_on) shim_gpu_draw(count); }
    DIAG(shim_gpu_pass_draw(count));         /* the pass probe (gpu.c) */
    DIAG(shim_gl_note_arrays(first, count));
    { long long a = gl_now(); p_draw_arrays(mode, first, count);
      g_gl_ns_draw += gl_now() - a; }
}

long g_gl_draws_by_fb[8];
extern long g_gl_last_fb;

int shim_sticks_in_game(void);
int shim_sticks_overlay_started(void);

void shim_sticks_overlay_reset(void);
void shim_gl_note_quads(int count, unsigned type, const void *idx);

void shim_glDrawElements(unsigned mode, int count, unsigned type, const void *idx) {
    if (!p_draw_elements)
        p_draw_elements = (void (*)(unsigned, int, unsigned, const void *)) shim_android_symbol("glDrawElements");
    g_gl_draws++;
    if (g_gl_last_fb >= 0 && g_gl_last_fb < 8) g_gl_draws_by_fb[g_gl_last_fb]++;
    { extern int g_gpu_on; void shim_gpu_draw(int); if (g_gpu_on) shim_gpu_draw(count); }
    /*
     * The bloom composite is drawn at the canvas's own size and laid over the
     * enlarged scene (bloom.c) - the one draw of the game's that is replaced.
     * The pass probe is told after it: a redirected draw runs in bloom.c's own
     * pass, and bloom.c tells the probe about that pass itself.
     */
    {
        int shim_bloom_draw(void (*)(unsigned, int, unsigned, const void *),
                            unsigned, int, unsigned, const void *);
        if (shim_bloom_draw(p_draw_elements, mode, count, type, idx)) return;
    }
    DIAG(shim_gpu_pass_draw(count));         /* the pass probe (gpu.c) */
    DIAG(shim_gl_note_quads(count, type, idx));
    /* where the quads land on the pixel grid of their target (texel.c) */
    DIAG({ void shim_texel_probe(unsigned, int, unsigned, const void *);
           shim_texel_probe(mode, count, type, idx); });
    /*
     * Nothing is done to the depth test here any more, and that is the point.
     *
     * There used to be a switch-off around the overlay calls: the game keeps
     * GL_DEPTH_TEST on the whole time, its overlay sprites all sit at one
     * depth, and the second of a pair was being thrown away. It was treated
     * where it showed - and only from the first stick sprite of a frame
     * onwards, because that is when we recognised the overlay. Everything the
     * overlay drew before that stayed at the mercy of the test, and on some
     * devices that was the bomb, the pause and the item buttons: present,
     * taking the finger, drawn nowhere.
     *
     * The cause was a step earlier. iOS gives the game no depth buffer at
     * all, so its always-on depth test never rejects anything; we were asking
     * EGL for one. Now we do not (see shim_gl_start), the test is as harmless
     * here as it is there, and this place has nothing left to fix.
     *
     * The old switch-back was harmful on its own: it turned the test ON
     * regardless of what it had been, leaking that state into every later
     * draw.
     */
    /* the picture onto the window: read LINEAR for its sharp-bilinear shader (stretch.c) */
    { void shim_stretch_begin(void); shim_stretch_begin(); }
    { long long a = gl_now(); p_draw_elements(mode, count, type, idx);
      g_gl_ns_draw += gl_now() - a; }
    { void shim_stretch_end(void); shim_stretch_end(); }
    /*
     * A refused draw will not show itself: the GL error is taken by whoever
     * asks first, and the game asks — and then says nothing. So we ask right
     * after the call and name the first refusals out loud.
     */
#if ISAAC_DIAG
    /*
     * THE LOOK-UP IS DONE ONCE, not on every draw. `told` only moves when a
     * draw is actually refused, so while everything is well this block runs for
     * EVERY glDrawElements - and it used to ask the linker for two symbols each
     * time. dlsym walks every loaded object under the linker's lock: at three
     * hundred draws a frame that is six hundred walks. Counted on the owner's
     * phone (14.09.2026): 118 of 351 ms of stalls in play were inside
     * linker64's do_dlsym - a third of the trouble, and all of it ours. The
     * third such find in one day, after the readback grid in gl.c and the
     * naming of addresses in objc.c: a measurement has to be cheaper than what
     * it measures.
     */
    static int told;
    static unsigned (*geterr)(void);
    static void (*getiv)(unsigned, int *);
    static int looked;
    if (told < 12) {
        if (!looked) {
            looked = 1;
            geterr = (unsigned (*)(void)) shim_android_symbol("glGetError");
            getiv = (void (*)(unsigned, int *)) shim_android_symbol("glGetIntegerv");
        }
        unsigned e = geterr ? geterr() : 0;
        if (e) {
            int prog = 0, vao = 0, buf = 0;
            if (getiv) {
                getiv(0x8B8D /* CURRENT_PROGRAM */, &prog);
                getiv(0x85B5 /* VERTEX_ARRAY_BINDING */, &vao);
                getiv(0x8895 /* ELEMENT_ARRAY_BUFFER_BINDING */, &buf);
            }
            told++;
            LOG("DRAW REFUSED %04x: mode %04x, points %d, type %04x, program %d, vao %d, index buffer %d",
                e, mode, count, type, prog, vao, buf);
        }
    }
    /*
     * A snapshot of the state on the first calls. A draw with no error and no
     * pixels can only be explained by state: scissor, depth, blending, face
     * culling or an empty vertex source. Guessing here is pointless.
     */
    static int shots;
    if (shots < 3) {
        shots++;
        void (*getiv)(unsigned, int *) = (void (*)(unsigned, int *)) shim_android_symbol("glGetIntegerv");
        unsigned char (*isen)(unsigned) = (unsigned char (*)(unsigned)) shim_android_symbol("glIsEnabled");
        void (*getva)(unsigned, unsigned, int *) =
            (void (*)(unsigned, unsigned, int *)) shim_android_symbol("glGetVertexAttribiv");
        int vp[4] = {0}, sc[4] = {0}, prog = 0, vao = 0, ab = 0, eb = 0, df = 0, bs = 0, bd = 0, cf = 0;
        if (getiv) {
            getiv(0x0BA2 /* VIEWPORT */, vp);
            getiv(0x0C10 /* SCISSOR_BOX */, sc);
            getiv(0x8B8D, &prog); getiv(0x85B5, &vao);
            getiv(0x8894 /* ARRAY_BUFFER_BINDING */, &ab);
            getiv(0x8895, &eb);
            getiv(0x0B74 /* DEPTH_FUNC */, &df);
            getiv(0x80C9 /* BLEND_SRC_RGB */, &bs);
            getiv(0x80C8 /* BLEND_DST_RGB */, &bd);
            getiv(0x0B45 /* CULL_FACE_MODE */, &cf);
        }
        LOG("draw call %d: points %d, mode %04x, indices %p, program %d, vao %d, vertex buffer %d, index buffer %d",
            shots, count, mode, idx, prog, vao, ab, eb);
        LOG("   viewport %d %d %d x %d, scissor %d %d %d x %d",
            vp[0], vp[1], vp[2], vp[3], sc[0], sc[1], sc[2], sc[3]);
        LOG("   depth %s (%04x), scissor %s, blending %s (%04x/%04x), faces %s (%04x)",
            isen && isen(0x0B71) ? "on" : "off", df,
            isen && isen(0x0C11) ? "on" : "off",
            isen && isen(0x0BE2) ? "on" : "off", bs, bd,
            isen && isen(0x0B44) ? "on" : "off", cf);
        void (*getvap)(unsigned, unsigned, void **) =
            (void (*)(unsigned, unsigned, void **)) shim_android_symbol("glGetVertexAttribPointerv");
        void (*getbp)(unsigned, unsigned, int *) =
            (void (*)(unsigned, unsigned, int *)) shim_android_symbol("glGetBufferParameteriv");
        if (getva)
            for (unsigned a = 0; a < 4; a++) {
                int on = 0, sz = 0, bb = 0, ty = 0, stride = 0;
                void *ptr = NULL;
                getva(a, 0x8622 /* ENABLED */, &on);
                getva(a, 0x8623 /* SIZE */, &sz);
                getva(a, 0x8625 /* TYPE */, &ty);
                getva(a, 0x8624 /* STRIDE */, &stride);
                getva(a, 0x889F /* BUFFER_BINDING */, &bb);
                if (getvap) getvap(a, 0x8645 /* ARRAY_POINTER */, &ptr);
                LOG("   stream %u: %s, fields %d, type %04x, stride %d, source %d, offset %p",
                    a, on ? "on" : "off", sz, ty, stride, bb, ptr);
            }
        /* whether the bound buffers have any storage — that is the whole question */
        if (getbp) {
            int vsz = -1, isz = -1;
            getbp(0x8892 /* ARRAY_BUFFER */, 0x8764 /* BUFFER_SIZE */, &vsz);
            getbp(0x8893 /* ELEMENT_ARRAY_BUFFER */, 0x8764, &isz);
            LOG("   vertex buffer size %d, index buffer size %d", vsz, isz);
        }
    }
#endif
}

/*
 * A few more places along the frame path. Zero draws by itself does not say
 * where the path broke off: before clearing the screen, before choosing a
 * program, or already after that. So these are counted too — three numbers
 * show how far things get.
 */
long g_gl_clears, g_gl_programs, g_gl_viewports;
static void (*p_clear)(unsigned);
static void (*p_use_program)(unsigned);
static void (*p_viewport)(int, int, int, int);

void shim_glClear(unsigned mask) {
    if (!p_clear) p_clear = (void (*)(unsigned)) shim_android_symbol("glClear");
    g_gl_clears++;
    DIAG(shim_gpu_pass_clear(mask));         /* the pass probe (gpu.c) */
{ long long a = gl_now(); p_clear(mask);
      g_gl_ns_clear += gl_now() - a; }
}

void shim_glUseProgram(unsigned prog) {
    if (!p_use_program) p_use_program = (void (*)(unsigned)) shim_android_symbol("glUseProgram");
    g_gl_programs++;
    { void shim_gpu_use(unsigned); shim_gpu_use(prog); }     /* which shader the draws use (gpu.c) */
    p_use_program(prog);
}

/*
 * The viewport is NOT touched, and that is the finding of the whole exercise.
 *
 * 0x8a6dc4 - the road every surface takes to become the drawing target - asks
 * the surface itself how large it is (its vtable at +0x50 and +0x58) and sets
 * the viewport to exactly that, right after attaching its texture; 0x8a6bb4, a
 * few instructions earlier, brings the depth buffer to the same size. So a
 * surface created larger carries both along with it. Growing the viewport by
 * hand, or the texture by hand, only made the halves disagree: the framebuffer
 * went incomplete and the room stopped being drawn at all, while the HUD, which
 * travels another road, stayed on screen.
 */
int shim_screen_scene_scale(int *cw, int *ch);

/*
 * glGetError, ours to ask: the driver keeps a QUEUE of errors and hands back
 * one per call, so a single ask can leave others behind. Draining is a loop.
 */
static unsigned (*p_gl_error)(void);

static void gl_error_drain(void) {
    int shim_report_active(void);
    if (!shim_report_active()) return;
    if (!p_gl_error) p_gl_error = (unsigned (*)(void)) shim_android_symbol("glGetError");
    if (!p_gl_error) return;
    for (int i = 0; i < 16 && p_gl_error(); i++) { }
}

static unsigned gl_error_after(void) {
    if (!p_gl_error) p_gl_error = (unsigned (*)(void)) shim_android_symbol("glGetError");
    return p_gl_error ? p_gl_error() : 0;
}

void shim_glViewport(int x, int y, int w, int h) {
    if (!p_viewport) p_viewport = (void (*)(int, int, int, int)) shim_android_symbol("glViewport");
    { void shim_gpu_viewport(int, int); shim_gpu_viewport(w, h); }
    DIAG(static int told; if (told < 8) { told++; LOG("GL: viewport %d %d %d x %d", x, y, w, h); });
    /*
     * Into the tester's report: which viewports the game actually sets. The
     * scene has to be told apart from the shadow passes and the window itself
     * before its viewport can be grown, and two attempts at guessing that were
     * two attempts too many. Each distinct rectangle is named once.
     */
    {
        int shim_report_active(void);
        void report_line(const char *, const char *);
        if (shim_report_active()) {
            static struct { int x, y, w, h; } seen[16];
            static int n;
            int known = 0;
            for (int i = 0; i < n; i++)
                if (seen[i].x == x && seen[i].y == y && seen[i].w == w && seen[i].h == h) { known = 1; break; }
            if (!known && n < 16) {
                seen[n].x = x; seen[n].y = y; seen[n].w = w; seen[n].h = h; n++;
                char line[120];
                snprintf(line, sizeof(line), "%d,%d %d x %d", x, y, w, h);
                report_line("viewport", line);
            }
        }
    }
    /*
     * PROBE: which framebuffer each viewport belongs to. A viewport alone does
     * not say whether it matches the surface it is drawing into - the pair
     * does, and a scene drawn small into a large surface is exactly a pair
     * that disagrees.
     */
    {
        int shim_report_active(void);
        void report_line(const char *, const char *);
        extern long g_gl_last_fb;
        if (shim_report_active()) {
            static struct { int w, h; long fb; } seen[24];
            static int n;
            int known = 0;
            for (int i = 0; i < n; i++)
                if (seen[i].w == w && seen[i].h == h && seen[i].fb == g_gl_last_fb) { known = 1; break; }
            if (!known && n < 24) {
                seen[n].w = w; seen[n].h = h; seen[n].fb = g_gl_last_fb; n++;
                char line[120];
                snprintf(line, sizeof(line), "%d x %d into framebuffer %ld", w, h, g_gl_last_fb);
                report_line("draws", line);
            }
        }
    }
    g_gl_viewports++;
    p_viewport(x, y, w, h);
}

/*
 * Where the game actually draws. Until that has been seen with our own eyes,
 * a black screen with a live draw counter has no explanation: the buffer may
 * not be the one we present afterwards.
 */
static void (*p_bind_fb)(unsigned, unsigned);
static void (*p_fb_rb)(unsigned, unsigned, unsigned, unsigned);
long g_gl_last_fb = -1;

void shim_glBindFramebuffer(unsigned target, unsigned fb) {
    if (!p_bind_fb) p_bind_fb = (void (*)(unsigned, unsigned)) shim_android_symbol("glBindFramebuffer");
    static int told;
    if (told < 12) { told++; LOG("GL: framebuffer %u selected (target %04x)", fb, target); }
    g_gl_last_fb = (long) fb;
    DIAG(shim_gpu_pass_leave_bind(target, fb));   /* the pass that ends is stamped while it is the target */
{ long long a = gl_now(); p_bind_fb(target, fb);
      g_gl_ns_bindfb += gl_now() - a; }
    DIAG(shim_gpu_pass_bind(target, fb));    /* after the bind: it may ask about the target */
}

/*
 * Compiling and linking programs. The game asks GL itself whether everything
 * worked, but its own log line is cut out of the build, and the failure goes
 * silent. So we ask ourselves and say it out loud: drawing without a working
 * program is exactly "the calls go out and the screen stays empty".
 */
static void (*p_compile)(unsigned);
static void (*p_link)(unsigned);

void shim_glCompileShader(unsigned sh) {
    if (!p_compile) p_compile = (void (*)(unsigned)) shim_android_symbol("glCompileShader");
    { long long a = gl_now(); p_compile(sh); g_gl_ns_shader += gl_now() - a; }
    g_gl_compiles++;
    void (*getiv)(unsigned, unsigned, int *) =
        (void (*)(unsigned, unsigned, int *)) shim_android_symbol("glGetShaderiv");
    void (*getlog)(unsigned, int, int *, char *) =
        (void (*)(unsigned, int, int *, char *)) shim_android_symbol("glGetShaderInfoLog");
    int ok = 1;
    if (getiv) getiv(sh, 0x8B81 /* GL_COMPILE_STATUS */, &ok);
    if (!ok && getlog) {
        char log[1024];
        int n = 0;
        getlog(sh, (int) sizeof(log), &n, log);
        LOG("SHADER %u DID NOT COMPILE: %.900s", sh, log);
    }
}

void shim_glLinkProgram(unsigned pr) {
    if (!p_link) p_link = (void (*)(unsigned)) shim_android_symbol("glLinkProgram");
    { long long a = gl_now(); p_link(pr); g_gl_ns_shader += gl_now() - a; }
    g_gl_links++;
    void (*getiv)(unsigned, unsigned, int *) =
        (void (*)(unsigned, unsigned, int *)) shim_android_symbol("glGetProgramiv");
    void (*getlog)(unsigned, int, int *, char *) =
        (void (*)(unsigned, int, int *, char *)) shim_android_symbol("glGetProgramInfoLog");
    int ok = 1;
    if (getiv) getiv(pr, 0x8B82 /* GL_LINK_STATUS */, &ok);
    LOG("program %u %s", pr, ok ? "linked" : "DID NOT LINK");
    if (!ok && getlog) {
        char log[1024];
        int n = 0;
        getlog(pr, (int) sizeof(log), &n, log);
        LOG("   %.900s", log);
    }
}

/*
 * The shader's text and the program it goes into: passed through untouched,
 * and shown to gpu.c on the way, which names the shader by a line only its
 * file contains. Report builds only - gpu.c returns at once otherwise.
 */
static void (*p_shader_source)(unsigned, int, const char *const *, const int *);
static void (*p_attach_shader)(unsigned, unsigned);

void shim_glShaderSource(unsigned sh, int count, const char *const *str, const int *len) {
    if (!p_shader_source)
        p_shader_source = (void (*)(unsigned, int, const char *const *, const int *))
                          shim_android_symbol("glShaderSource");
    p_shader_source(sh, count, str, len);
    { void shim_gpu_shader_source(unsigned, int, const char *const *, const int *);
      shim_gpu_shader_source(sh, count, str, len); }
}

void shim_glAttachShader(unsigned prog, unsigned sh) {
    if (!p_attach_shader)
        p_attach_shader = (void (*)(unsigned, unsigned)) shim_android_symbol("glAttachShader");
    p_attach_shader(prog, sh);
    { void shim_gpu_attach(unsigned, unsigned); shim_gpu_attach(prog, sh); }
}

/*
 * What the game puts into textures. If drawing goes without refusals and the
 * screen is still empty, the first suspect is empty images: the game parses
 * PNG on its own, and it could have broken on anything of ours.
 */
/*
 * Counted for the tester's report: a stall that carries texture uploads is a
 * room reloading its art, and the megapixels say how much of it.
 */
long g_gl_teximages, g_gl_texpixels, g_gl_texdeletes;
long g_gl_bigimages, g_gl_bigpixels, g_gl_surfaces, g_gl_surfpixels, g_gl_biggest;
int g_gl_big_w, g_gl_big_h;

/*
 * NO FILTER IS SET HERE ANY MORE (13.10.2026). The scene surface used to be
 * switched to LINEAR at its upload, when the upload was exactly canvas x shot
 * - and the engine never uploads that: it rounds a surface's texture up to a
 * power of two (3510 x 1620 goes up as 4096 x 2048), so from 09.09 to 1.74 the
 * test never matched and the scene was point-sampled onto the window. The
 * last stretch is now read sharp-bilinear by its own shader, with the filter
 * turned LINEAR for that one draw and put back after it (stretch.c), in
 * every mode; every upload keeps exactly what the game set.
 */
/* the canvas and the enlargement both come from screen.c, which decides them */
long long g_gl_tex_ns;              /* time spent inside the uploads themselves */

/*
 * Textures being DELETED is the other half of the question: a room that
 * reloads its art either never kept it, or threw it away on the way out. The
 * count says which.
 */
void shim_glDeleteTextures(int n, const unsigned *names) {
    static void (*real)(int, const unsigned *);
    if (!real) real = (void (*)(int, const unsigned *)) shim_android_symbol("glDeleteTextures");
    if (n > 0) g_gl_texdeletes += n;
    DIAG(shim_gpu_pass_deleted(n, names));
    real(n, names);
}

void shim_glTexImage2D(unsigned target, int level, int internal, int w, int h,
                       int border, unsigned fmt, unsigned type, const void *data) {
    static void (*real)(unsigned, int, int, int, int, int, unsigned, unsigned, const void *);
    if (!real) real = (void (*)(unsigned, int, int, int, int, int, unsigned, unsigned, const void *))
                          shim_android_symbol("glTexImage2D");
    g_gl_teximages++;
    g_gl_texpixels += (long) w * h;
    /*
     * A big upload with NO pixels behind it is a render surface being made:
     * the game hands the driver a size and nothing else. Counting those apart
     * says whether a stall is the port's own enlarged scene or the game's
     * artwork, and the two want opposite cures.
     */
    if ((long) w * h >= 250000) {
        g_gl_bigimages++;
        g_gl_bigpixels += (long) w * h;
        if (!data) { g_gl_surfaces++; g_gl_surfpixels += (long) w * h; }
        if ((long) w * h > g_gl_biggest) { g_gl_biggest = (long) w * h;
                                           g_gl_big_w = w; g_gl_big_h = h; }
    }
#if ISAAC_DIAG
    static int told;
    if (told < 10 && level == 0 && w > 4 && h > 4) {
        told++;
        int lit = 0;
        if (data) {
            const unsigned char *p = (const unsigned char *) data;
            long n = (long) w * h * 4;
            if (n > 400000) n = 400000;
            for (long i = 0; i < n; i++) if (p[i]) { lit++; if (lit > 8) break; }
        }
        LOG("texture %d x %d, format %04x/%04x, data %s, non-zero bytes %s",
            w, h, fmt, type, data ? "yes" : "NO", lit > 8 ? "many" : (lit ? "few" : "NO"));
    }
#endif
    /*
     * The upload is timed apart from everything else. A stall is either the
     * driver taking the pixels or the game making them - reading the file and
     * unpacking the PNG - and only these two numbers side by side say which.
     */
    /*
     * Whatever was pending is collected and thrown away first: only then does
     * an error found afterwards belong to THIS upload. Only while a report is
     * open - the play build pays nothing.
     */
    if (data && level == 0) gl_error_drain();
    DIAG(shim_gpu_pass_teximage(target, level));   /* re-specification, for the pass probe */

    struct timespec ta, tb;
    clock_gettime(CLOCK_MONOTONIC, &ta);
    real(target, level, internal, w, h, border, fmt, type, data);
    clock_gettime(CLOCK_MONOTONIC, &tb);
    g_gl_tex_ns += ((long long) tb.tv_sec - ta.tv_sec) * 1000000000LL + (tb.tv_nsec - ta.tv_nsec);

    /*
     * A refused upload names itself - and names WHICH error, and is asked in a
     * way that cannot lie.
     *
     * The first version asked glGetError only after the upload, and glGetError
     * hands back the oldest error nobody has collected yet: an error raised by
     * some earlier call, anywhere in the frame, was reported against the next
     * texture. That is why a tester's report said "refused" for perfectly
     * ordinary 512 x 512 RGBA uploads. The queue is drained BEFORE the upload
     * now, so whatever comes out after it belongs to it, and the code is
     * printed rather than sorted into two buckets.
     */
    if (data && level == 0) {
        int shim_report_active(void);
        void report_line(const char *, const char *);
        if (shim_report_active()) {
            unsigned e = gl_error_after();
            if (e) {
                static int told;
                static unsigned counted[8], seen_n;
                static long refusals;
                refusals++;
                int known = 0;
                for (unsigned i = 0; i < seen_n; i++) if (counted[i] == e) known = 1;
                if (!known && seen_n < 8) counted[seen_n++] = e;
                if (told < 20) {
                    told++;
                    char line[200];
                    snprintf(line, sizeof(line),
                             "%s (%04x) uploading %d x %d (format %04x/%04x), refusal no %ld",
                             e == 0x0505 ? "OUT OF MEMORY" :
                             e == 0x0501 ? "BAD VALUE" :
                             e == 0x0502 ? "BAD OPERATION" :
                             e == 0x0500 ? "BAD ENUM" : "refused",
                             e, w, h, fmt, type, refusals);
                    report_line("texture", line);
                }
            }
        }
    }
}

/*
 * The game keeps vertices in its own memory, not in GL buffers.
 *
 * Two measurements show it at once: the pointers it hands to
 * glVertexAttribPointer and glDrawElements are real heap addresses, and the
 * whole image holds not a single call to glBufferData, glBufferSubData or a
 * buffer mapping. So it has nothing to fill a buffer with, and the buffer has
 * no storage.
 *
 * And yet it binds them. A bound buffer changes what the pointer means: GL
 * starts reading not from the game's memory but from the buffer at an offset
 * — from an empty one. That is where the black screen came from with sound
 * programs, live textures and drawing without a single error: the points were
 * read out of nothing.
 *
 * So the binding is kept at zero. The vertex array object is kept at zero
 * too: in ES 3 reading from client memory is only allowed with the default
 * one.
 */
void shim_glBindBuffer(unsigned target, unsigned name) {
    static void (*real)(unsigned, unsigned);
    if (!real) real = (void (*)(unsigned, unsigned)) shim_android_symbol("glBindBuffer");
    (void) name;
    real(target, 0);
}

void shim_glBindVertexArray(unsigned name) {
    static void (*real)(unsigned);
    if (!real) real = (void (*)(unsigned)) shim_android_symbol("glBindVertexArray");
    (void) name;
    real(0);
}

/*
 * The order of quads inside a frame.
 *
 * All the earlier measurements agreed: the two sticks have the same fields,
 * the same feed, the same material and the same number of draws. So if the
 * order does flip, it happens later — in the assembled batch. We force the
 * buffers to zero, so both vertices and indices live in application memory:
 * they can be read right here and show the real sequence instead of a deduced
 * one.
 *
 * The last records are kept in a ring — the overlay is drawn at the end of
 * the frame.
 */

struct attr_t { const void *ptr; int size, stride; unsigned type; };
static struct attr_t g_attr[8];

    /*
     * TEMPORARY: the colour actually handed to the card.
     *
     * Attribute 2 is four floats with a stride of 88 - the sprite vertex's
     * Color, which the vertex shader passes through untouched and the fragment
     * shader multiplies the texture by. Neither shader clamps it. So if a
     * multiplier above one never shows, either the game clamps before the
     * vertex or it never gets that far, and reading the vertex says which. The
     * red-times-eight test is in place, so an eight here means the game passed
     * it and the loss is later; a one means the game clamped it.
     */


#define QRING 64
#define QATTR 4
/* the field layout is remembered TOGETHER with the quad: it changes between calls */
static float g_qv[QRING][QATTR][4];     /* first vertex */
static float g_qw[QRING][QATTR][4];     /* opposite corner */
static int g_qsz[QRING][QATTR];
static int g_qdraw[QRING], g_qn, g_draw_no;

void shim_glVertexAttribPointer(unsigned idx, int size, unsigned type, unsigned char norm,
                                int stride, const void *ptr) {
    static void (*real)(unsigned, int, unsigned, unsigned char, int, const void *);
    if (!real) real = (void (*)(unsigned, int, unsigned, unsigned char, int, const void *))
                          shim_android_symbol("glVertexAttribPointer");
    if (idx < 8) {
        /*
         * Sometimes the game passes an offset inside a buffer here instead of
         * an address. We force the buffers to zero, so such an "offset" points
         * nowhere — reading through it is not allowed. Tell them apart by
         * magnitude.
         */
        g_attr[idx].ptr = ((uintptr_t) ptr >= 0x100000u) ? ptr : 0;
        g_attr[idx].size = size;
        g_attr[idx].type = type;
        g_attr[idx].stride = stride ? stride : size * 4;
    }
    real(idx, size, type, norm, stride, ptr);
}

#if ISAAC_DIAG
/* the client-memory stream an attribute was last given, for the texel probe (texel.c) */
int shim_gl_attr_get(unsigned idx, const void **ptr, int *size, int *stride, unsigned *type) {
    if (idx >= 8 || !g_attr[idx].ptr) return 0;
    if (ptr) *ptr = g_attr[idx].ptr;
    if (size) *size = g_attr[idx].size;
    if (stride) *stride = g_attr[idx].stride;
    if (type) *type = g_attr[idx].type;
    return 1;
}
#endif

/* record the first vertex of every quad in the batch that was issued */
void shim_gl_note_quads(int count, unsigned type, const void *idx) {
    if (!idx || !g_attr[0].ptr || !shim_sticks_overlay_started()) return;
    g_draw_no++;
    /* indices come in three widths; I used to take only the short ones and lost the overlay */
    unsigned short buf[6];
    (void) buf;
    #define IDX(k) (type == 0x1403 ? ((const unsigned short *) idx)[k]                   : type == 0x1405 ? (unsigned) ((const unsigned int *) idx)[k]                   : (unsigned) ((const unsigned char *) idx)[k])
    const unsigned short *ix = (const unsigned short *) idx;
    (void) ix;
    const char *vp = (const char *) g_attr[0].ptr;
    int st = g_attr[0].stride;
    (void) vp; (void) st;
    for (int q = 0; q + 6 <= count; q += 6) {
        /*
         * Take the stick quads only. Their place is known: the game field is
         * 480 by 270 and the sticks sit at the edges around height 200. Among
         * the vertex fields find the one holding a position with depth, and
         * use it to filter out the rest — otherwise the ring fills up with a
         * whole frame.
         */
        int s = g_qn % QRING;
        for (int a = 0; a < QATTR; a++) {
            g_qsz[s][a] = 0;
            if (!g_attr[a].ptr || g_attr[a].size < 1 || g_attr[a].size > 4) continue;
            g_qsz[s][a] = g_attr[a].size;
            const char *b = (const char *) g_attr[a].ptr;
            int w = g_attr[a].stride;
            const float *v0 = (const float *) (b + (size_t) IDX(q) * (size_t) w);
            const float *v2 = (const float *) (b + (size_t) IDX(q + 2) * (size_t) w);
            for (int k = 0; k < g_attr[a].size; k++) { g_qv[s][a][k] = v0[k]; g_qw[s][a][k] = v2[k]; }
        }
        g_qdraw[s] = g_draw_no;
        g_qn++;
    }
}


/* the same thing for drawing without indices: the vertices follow one another */
void shim_gl_note_arrays(int first, int count) {
    if (!g_attr[0].ptr || !shim_sticks_overlay_started()) return;
    g_draw_no++;
    for (int q = 0; q + 4 <= count; q += 4) {
        int s = g_qn % QRING;
        for (int a = 0; a < QATTR; a++) {
            g_qsz[s][a] = 0;
            if (!g_attr[a].ptr || g_attr[a].size < 1 || g_attr[a].size > 4) continue;
            g_qsz[s][a] = g_attr[a].size;
            const char *b = (const char *) g_attr[a].ptr;
            int w = g_attr[a].stride;
            const float *v0 = (const float *) (b + (size_t) (first + q) * (size_t) w);
            const float *v2 = (const float *) (b + (size_t) (first + q + 2) * (size_t) w);
            for (int k = 0; k < g_attr[a].size; k++) { g_qv[s][a][k] = v0[k]; g_qw[s][a][k] = v2[k]; }
        }
        g_qdraw[s] = g_draw_no;
        g_qn++;
    }
}

/* print the tail of the frame: the last quads in the order they were issued */
void shim_gl_dump_quads(void) {
    static int times;
    if (times >= 1 || !shim_sticks_in_game() || g_qn < 1) {
        g_qn = 0; g_draw_no = 0; return;
    }
    times++;
    int have = g_qn < QRING ? g_qn : QRING;
    int first = g_qn - have;
    LOG("frame tail: %d quads in all, showing %d (fields: sizes %d %d %d %d)",
        g_qn, have, g_attr[0].size, g_attr[1].size, g_attr[2].size, g_attr[3].size);
    /* few quads — print every field and both corners of each */
    for (int i = 0; i < have; i++) {
        int s = (first + i) % QRING;
        char line[300];
        int n = snprintf(line, sizeof(line), "overlay %d: call %d", i, g_qdraw[s]);
        for (int a = 0; a < QATTR; a++) {
            if (!g_qsz[s][a]) continue;
            n += snprintf(line + n, sizeof(line) - (size_t) n, " | f%d", a);
            for (int k = 0; k < g_qsz[s][a]; k++)
                n += snprintf(line + n, sizeof(line) - (size_t) n, " %.3f", (double) g_qv[s][a][k]);
            n += snprintf(line + n, sizeof(line) - (size_t) n, " ->");
            for (int k = 0; k < g_qsz[s][a]; k++)
                n += snprintf(line + n, sizeof(line) - (size_t) n, " %.3f", (double) g_qw[s][a][k]);
        }
        LOG("%s", line);
    }
    g_qn = 0;
    g_draw_no = 0;
}

void shim_glFramebufferRenderbuffer(unsigned target, unsigned att, unsigned rbtarget, unsigned rb) {
    if (!p_fb_rb)
        p_fb_rb = (void (*)(unsigned, unsigned, unsigned, unsigned)) shim_android_symbol("glFramebufferRenderbuffer");
    DIAG(LOG("GL: colour buffer %u attached to the framebuffer (slot %04x)", rb, att));
    p_fb_rb(target, att, rbtarget, rb);
    DIAG(shim_gpu_pass_rb(target, att, rb)); /* the screen framebuffer, for the pass probe */
}

/* ---------- stable sort ---------- */

/*
 * Our own qsort, and not out of whim.
 *
 * The standard does not promise that elements with equal keys keep their
 * relative order, and Apple and Android shuffle them differently. The game
 * leans on that order: it accumulates a frame and sorts it by a numeric key,
 * and the cap and the base of a stick have the SAME key — zero. So which one
 * ends up on top is decided purely by the stability of the sort. On iOS the
 * feed order survived, here it did not, and the movement stick's cap went
 * under its own base.
 *
 * So this is a merge: with equal keys the left run goes first, which means
 * the feed order is preserved exactly. It also fixes any other place in the
 * game that leans on the same thing — and there would have been no way to
 * find those otherwise.
 */
static void merge_run(char *dst, char *a, size_t na, char *b, size_t nb,
                      size_t w, int (*cmp)(const void *, const void *)) {
    size_t i = 0, j = 0;
    while (i < na && j < nb) {
        /* "not greater" means take the left one on a tie: that is the whole point */
        if (cmp(a + i * w, b + j * w) <= 0) { memcpy(dst, a + i * w, w); i++; }
        else { memcpy(dst, b + j * w, w); j++; }
        dst += w;
    }
    if (i < na) { memcpy(dst, a + i * w, (na - i) * w); dst += (na - i) * w; }
    if (j < nb) memcpy(dst, b + j * w, (nb - j) * w);
}

/*
 * The type of a mutex, translated between the two systems.
 *
 * Apple numbers them NORMAL 0, ERRORCHECK 1, RECURSIVE 2; bionic numbers the
 * last two the other way round. The game asks for 2 meaning recursive and,
 * without this, got a checking mutex - one that answers with an error on the
 * second lock instead of letting the same thread through.
 */
int shim_mutexattr_settype(pthread_mutexattr_t *attr, int apple_type) {
    int type = apple_type;
    if (apple_type == 1) type = PTHREAD_MUTEX_ERRORCHECK;
    else if (apple_type == 2) type = PTHREAD_MUTEX_RECURSIVE;
    return pthread_mutexattr_settype(attr, type);
}

void shim_qsort(void *base, size_t n, size_t w, int (*cmp)(const void *, const void *)) {
    if (!base || !cmp || w == 0 || n < 2) return;
    char *src = (char *) base;
    char *tmp = (char *) malloc(n * w);
    if (!tmp) {
        /* out of memory — insertion sort: slow, but just as stable */
        for (size_t i = 1; i < n; i++)
            for (size_t j = i; j > 0 && cmp(src + (j - 1) * w, src + j * w) > 0; j--)
                for (size_t b = 0; b < w; b++) {
                    char t = src[(j - 1) * w + b];
                    src[(j - 1) * w + b] = src[j * w + b];
                    src[j * w + b] = t;
                }
        return;
    }
    char *from = src, *to = tmp;
    for (size_t run = 1; run < n; run *= 2) {
        for (size_t i = 0; i < n; i += 2 * run) {
            size_t na = (i + run < n) ? run : (n - i);
            size_t nb = (i + 2 * run < n) ? run : ((i + run < n) ? n - i - run : 0);
            merge_run(to + i * w, from + i * w, na, from + (i + run) * w, nb, w, cmp);
        }
        char *swap = from; from = to; to = swap;
    }
    if (from != src) memcpy(src, from, n * w);
    free(tmp);
}

/* ---------- walking folders: Darwin lays the directory entry out differently ---------- */

/*
 * A trap of the same kind as the variadic arguments, and just as silent: file
 * names inside a directory entry sit at DIFFERENT offsets.
 *
 *   Apple:   ino 0, seekoff 8, reclen 16, namlen 18, type 20, name from 21
 *   Android: ino 0, off     8, reclen 16, type   18,          name from 19
 *
 * The game is built for Apple and reads the name from byte 21, that is, it
 * loses the first two characters of every file and takes the second letter of
 * the name for the type. The directory is walked "successfully" all the while
 * — every name is simply wrong. So the entry is repacked into the Apple
 * layout here.
 */
typedef struct {
    uint64_t d_ino;
    uint64_t d_seekoff;
    uint16_t d_reclen;
    uint16_t d_namlen;
    uint8_t  d_type;
    char     d_name[1024];
} darwin_dirent_t;

void *shim_opendir(const char *path) {
    const char *p = shim_map_path(path);
    void *from_apk = shim_pack_opendir(p);
    if (from_apk) return from_apk;
    shim_safe_wait_dir(p);                  /* every queued write inside it is there before the listing */
    void *d = opendir(p);
    tell_path("opendir", p, d != NULL);
    return d;
}

/*
 * One record per open directory, not one for all of them.
 *
 * readdir answers with a pointer to a record the caller reads before asking
 * again, and a single static record served every directory at once: two walks
 * running together overwrote each other's answer. Eight places is more than
 * the game ever opens at a time; were they ever to run out, the last is shared
 * again, which is no worse than what was here before.
 */
static void *g_dir_who[8];
static darwin_dirent_t g_dir_slot[8];

static darwin_dirent_t *dir_entry(void *dirp) {
    for (unsigned i = 0; i < 8; i++) if (g_dir_who[i] == dirp) return &g_dir_slot[i];
    for (unsigned i = 0; i < 8; i++)
        if (!g_dir_who[i]) { g_dir_who[i] = dirp; return &g_dir_slot[i]; }
    return &g_dir_slot[7];
}

static void dir_forget(void *dirp) {
    for (unsigned i = 0; i < 8; i++) if (g_dir_who[i] == dirp) g_dir_who[i] = NULL;
}

void *shim_readdir(void *dirp) {
    if (shim_pack_is_our_walk(dirp)) return shim_pack_readdir(dirp);
    struct dirent *e = readdir((DIR *) dirp);
    if (!e) return NULL;
    darwin_dirent_t *out = dir_entry(dirp);
    size_t n = strlen(e->d_name);
    if (n > sizeof(out->d_name) - 1) n = sizeof(out->d_name) - 1;
    out->d_ino = e->d_ino;
    out->d_seekoff = (uint64_t) e->d_off;
    out->d_namlen = (uint16_t) n;
    out->d_type = e->d_type;
    memcpy(out->d_name, e->d_name, n);
    out->d_name[n] = 0;
    out->d_reclen = (uint16_t) (21 + n + 1);
    return out;
}

int shim_closedir(void *dirp) {
    if (shim_pack_is_our_walk(dirp)) return shim_pack_closedir(dirp);
    dir_forget(dirp);
    return closedir((DIR *) dirp);
}

/*
 * strlen with a check. Somewhere the game passes nothing in here; instead of
 * crashing we name the place in its code and answer zero — that way both the
 * cause and what happens next stay visible.
 */
size_t shim_strlen(const char *s) {
    if (!s) {
        unsigned long back = (unsigned long) __builtin_return_address(0);
        unsigned long base = shim_image_base();
        LOG("strlen(null) called from %06lx", base && back > base ? back - base : back);
        return 0;
    }
    return strlen(s);
}

/* ---------- intercepting the game's log ---------- */

/*
 * The engine has a log call of its own, and on iOS it goes to the system log.
 * We put a branch on it right in the image's memory, before the code becomes
 * read-only. This is not a patch to the game: the image lives in our memory
 * and is never changed on disk.
 */
int shim_game_log_impl(int level, const char *fmt, void *args);

/* ---------- Apple-style variadic arguments ---------- */

/*
 * An argument list in AAPCS64 is not a pointer but a description: where the
 * registers ran out and where the stack part lies. On Apple it is simply a
 * pointer to the stack. Converting one into the other: say that no register
 * arguments are left (offsets at zero) and that the stack part starts where
 * the game left it.
 */
typedef struct { void *stack, *gr_top, *vr_top; int gr_offs, vr_offs; } aapcs_va_t;

static void fill_va(aapcs_va_t *v, void *args) {
    v->stack = args;
    v->gr_top = args;
    v->vr_top = args;
    v->gr_offs = 0;
    v->vr_offs = 0;
}

int shim_sprintf_impl(char *dst, const char *fmt, void *args) {
    aapcs_va_t v;
    fill_va(&v, args);
    return vsprintf(dst, fmt, *(va_list *) &v);
}

int shim_snprintf_impl(char *dst, size_t n, const char *fmt, void *args) {
    aapcs_va_t v;
    fill_va(&v, args);
    return vsnprintf(dst, n, fmt, *(va_list *) &v);
}

/*
 * The game's own output. On iOS it goes to the device log, here it would go
 * nowhere — and it is the most valuable diagnostic there is: the engine
 * itself tells what it is doing and what it trips over.
 */
int shim_game_log_impl(int level, const char *fmt, void *args) {
#if !ISAAC_DIAG
    /* in the private build the game's output goes nowhere — as on iOS without a console */
    (void) level; (void) fmt; (void) args;
    return 0;
#else
    /*
     * Into the report as well: every distinct message, its first three times,
     * no more than 150 lines. Counted by the format's address before anything
     * is formatted, so a message the engine repeats every frame costs a lookup.
     */
    enum { SEEN = 512, EACH = 3, LINES = 150 };
    static const char *seen[SEEN];
    static unsigned char times[SEEN];
    static int lines;
    int slot = -1;
    for (int i = 0; i < SEEN; i++) {
        if (seen[i] == fmt) { slot = i; break; }
        if (!seen[i]) { seen[i] = fmt; slot = i; break; }
    }
    if (slot >= 0 && times[slot] >= EACH) return 0;
    if (slot >= 0) times[slot]++;
    aapcs_va_t v;
    fill_va(&v, args);
    char line[2048];
    int n = vsnprintf(line, sizeof(line), fmt ? fmt : "(no format)", *(va_list *) &v);
    if (n > 0) {
        while (n > 0 && (line[n-1] == 10 || line[n-1] == 13)) line[--n] = 0;
        if (n > 0) __android_log_print(ANDROID_LOG_INFO, "isaac-game", "[%d] %s", level, line);
        int shim_report_active(void);
        void report_line(const char *, const char *);
        if (n > 0 && lines < LINES && shim_report_active()) {
            lines++;
            char shortened[220];
            snprintf(shortened, sizeof(shortened), "[%d] %s", level, line);
            report_line("game", shortened);
        }
    }
    return n;
#endif
}

/* place a branch at an address inside the image */
void shim_hook(void *at, void *to) {
    unsigned int *c = (unsigned int *) at;
    c[0] = 0x58000050u;                 /* ldr x16, [pc, #8] */
    c[1] = 0xD61F0200u;                 /* br  x16           */
    *(unsigned long *) (c + 2) = (unsigned long) to;
}

int shim_fprintf_impl(FILE *f, const char *fmt, void *args) {
    /*
     * It only reaches the log during reconnaissance. Writing to the REAL file
     * always stays: that is not observation, that is the game working.
     */
    int n = 0;
#if ISAAC_DIAG
    aapcs_va_t v;
    fill_va(&v, args);
    char line[2048];
    n = vsnprintf(line, sizeof(line), fmt, *(va_list *) &v);
    if (n > 0) {
        while (n > 0 && (line[n-1] == 10 || line[n-1] == 13)) line[--n] = 0;
        if (n > 0) __android_log_print(ANDROID_LOG_INFO, "isaac-game", "%s", line);
    }
#endif
    if (f && f != stdout && f != stderr) {
        aapcs_va_t v2;
        fill_va(&v2, args);
        return vfprintf(f, fmt, *(va_list *) &v2);
    }
    return n;
}

int shim_sscanf_impl(const char *src, const char *fmt, void *args) {
    aapcs_va_t v;
    fill_va(&v, args);
    return vsscanf(src, fmt, *(va_list *) &v);
}

/*
 * These two get the argument list ready-made — and on Apple that is simply a
 * pointer to the stack, not a description. So the conversion is the same, only
 * the arguments arrive in a parameter rather than from sp.
 */
int shim_vsprintf(char *dst, const char *fmt, void *apple_va) {
    aapcs_va_t v;
    fill_va(&v, apple_va);
    return vsprintf(dst, fmt, *(va_list *) &v);
}

int shim_vsnprintf(char *dst, size_t n, const char *fmt, void *apple_va) {
    aapcs_va_t v;
    fill_va(&v, apple_va);
    return vsnprintf(dst, n, fmt, *(va_list *) &v);
}

/* ---------- table: name from the image -> our work ---------- */

typedef struct { const char *name; void *fn; } shim_entry_t;

/* Objective-C runtime — declarations from objc.c and objc_asm.S */
void *shim_objc_msgSend(void *, const char *, ...);
void *shim_objc_msgSendSuper2(void *, const char *, ...);
void *shim_objc_retain(void *);
void shim_objc_release(void *);
void *shim_objc_autoreleaseReturnValue(void *);
void *shim_objc_retainAutorelease(void *);
void *shim_objc_retainAutoreleaseReturnValue(void *);
void *shim_objc_retainAutoreleasedReturnValue(void *);
void *shim_objc_claimAutoreleasedReturnValue(void *);
void *shim_objc_unsafeClaimAutoreleasedReturnValue(void *);
void shim_objc_storeStrong(void **, void *);
void shim_objc_enumerationMutation(void *);
void *shim_objc_alloc(void *);
void *shim_objc_alloc_init(void *);
void *shim_objc_opt_class(void *);
int shim_objc_opt_isKindOfClass(void *, void *);
void *shim_objc_getProperty(void *, const char *, long, int);
void shim_objc_setProperty_atomic(void *, const char *, void *, long);
void shim_objc_setProperty_nonatomic_copy(void *, const char *, void *, long);
int shim_UIApplicationMain(int, char **, void *, void *);
void *shim_NSStringFromClass(void *);
void shim_objc_ret_noop(void);
void shim_objc_retain_x1(void);
void shim_objc_retain_x2(void);
void shim_objc_retain_x8(void);
void shim_objc_retain_x19(void);
void shim_objc_retain_x20(void);
void shim_objc_retain_x21(void);
void shim_objc_retain_x23(void);
void shim_objc_retain_x24(void);

extern unsigned long long shim_objc_empty_cache[8];

void shim_dispatch_once(long *, void *);
void shim_dispatch_async(void *, void *);
void *shim_dispatch_queue_create(const char *, void *);
void shim_Block_object_assign(void *, const void *, int);
void shim_Block_object_dispose(const void *, int);
void shim_NSLog(void *, ...);
int *shim_error(void);
size_t shim_strlen(const char *);
void shim_sprintf(void);
void shim_snprintf(void);
void shim_fprintf(void);
void shim_sscanf(void);
int shim_vsprintf(char *, const char *, void *);
int shim_vsnprintf(char *, size_t, const char *, void *);
int shim_open(const char *, int, int);
FILE *shim_fopen(const char *, const char *);
size_t shim_fwrite(const void *, size_t, size_t, FILE *);
int shim_fclose(FILE *);
size_t shim_fread(void *, size_t, size_t, FILE *);
int shim_access(const char *, int);
void shim_exit(int);
void shim_assert_rtn(const char *, const char *, int, const char *);
extern void *shim_NSConcreteGlobalBlock[8];
extern void *shim_NSConcreteStackBlock[8];
extern void *shim_dispatch_main_q[16];

/* ---------- sound: our own OpenAL layer on top of AAudio ---------- */

void *shim_alcOpenDevice(const char *);
int shim_alcCloseDevice(void *);
void *shim_alcCreateContext(void *, const int *);
int shim_alcMakeContextCurrent(void *);
void shim_alcProcessContext(void *);
void shim_alcDestroyContext(void *);
void shim_alGenBuffers(int, unsigned *);
void shim_alDeleteBuffers(int, const unsigned *);
void shim_alBufferData(unsigned, int, const void *, int, int);
void shim_alGenSources(int, unsigned *);
void shim_alDeleteSources(int, const unsigned *);
void shim_alSourcei(unsigned, int, int);
void shim_alSourcef(unsigned, int, float);
void shim_alSource3f(unsigned, int, float, float, float);
void shim_alListener3f(int, float, float, float);
void shim_alListenerfv(int, const float *);
void shim_alSourcePlay(unsigned);
void shim_alSourcePause(unsigned);
void shim_alSourceStop(unsigned);
void shim_alSourceQueueBuffers(unsigned, int, const unsigned *);
void shim_alSourceUnqueueBuffers(unsigned, int, unsigned *);
void shim_alGetSourcei(unsigned, int, int *);
void shim_alGetSourcef(unsigned, int, float *);
int shim_alGetError(void);

/* ---------- the things only Apple has ---------- */

typedef struct { double s, c; } sincos_d_t;
typedef struct { float s, c; } sincos_f_t;
sincos_d_t shim_sincos_stret(double);
sincos_f_t shim_sincosf_stret(float);
double shim_exp10(double);
void shim_memset_pattern16(void *, const void *, size_t);
int shim_maskrune(int, unsigned long);
int shim_tolower_rune(int);
int shim_toupper_rune(int);
void *shim_rune_table(void);
extern FILE *shim_stderrp;
void shim_darwin_init(void);
void shim_darwin_streams(void);
/* darwin_jmp.S: Apple's 192-byte jmp_buf — Android's setjmp writes past it */
int shim_darwin_setjmp(void *buf);
void shim_darwin_longjmp(void *buf, int val);

static int shim_pthread_cancel(void *t) { (void) t; return 0; }

/* ------------------------------------------------ every GL call that can block
 *
 * The frame waits on IOCTL_KGSL_DEVICE_WAITTIMESTAMP_CTXTID - the kernel's own
 * word, read out of /proc/self/task/<tid>/syscall and decoded from the request
 * 0x400c0907: direction _IOW, twelve bytes of argument, type 0x09 which is
 * KGSL, number 7. That call is the processor standing still until the card
 * reaches a mark, and the driver only makes it when something forces it: a
 * readback, a finish, or a touch of a resource the card has not finished with.
 *
 * Draws, clears, framebuffer binds and the swap were timed already and take
 * nothing. So the rest of the suspects are wrapped here, each with a clock, and
 * the frame reports whichever of them took the longest. Sixteen calls were
 * wrapped before this; these are the ones that were going past us (14.09.2026).
 */
enum { GLW_BUFFERDATA, GLW_BUFFERSUB, GLW_TEXSUB, GLW_FINISH, GLW_FLUSH,
       GLW_READPIXELS, GLW_BINDTEX, GLW_MIPMAP, GLW_FBTEX, GLW_FBSTATUS,
       GLW_DELBUF, GLW_DELFB, GLW_MAP, GLW_UNMAP, GLW_COUNT };
const char *const g_glw_name[GLW_COUNT] = {
    "glBufferData", "glBufferSubData", "glTexSubImage2D", "glFinish", "glFlush",
    "glReadPixels", "glBindTexture", "glGenerateMipmap", "glFramebufferTexture2D",
    "glCheckFramebufferStatus", "glDeleteBuffers", "glDeleteFramebuffers",
    "glMapBufferRange", "glUnmapBuffer",
};
long long g_glw_ns[GLW_COUNT];
long g_glw_n[GLW_COUNT];

static void *glw_p[GLW_COUNT];

static void *glw_get(int which, const char *name) {
    if (!glw_p[which]) glw_p[which] = shim_android_symbol(name);
    return glw_p[which];
}

/* one wrapper each: a function-pointer type cannot be passed through a macro */
#define GLW_VOID(fn, which, name, params, call, args)                            void fn params {                                                                     static void (*p) args;                                                           if (!p) p = (void (*) args) glw_get(which, name);                                if (!p) return;                                                                  long long a = gl_now();                                                          p call;                                                                          g_glw_ns[which] += gl_now() - a;                                                 g_glw_n[which]++;   DIAG(shim_gpu_pass_glw(which));                          }

GLW_VOID(shim_glBufferData, GLW_BUFFERDATA, "glBufferData",
         (unsigned target, long size, const void *data, unsigned usage),
         (target, size, data, usage), (unsigned, long, const void *, unsigned))
GLW_VOID(shim_glBufferSubData, GLW_BUFFERSUB, "glBufferSubData",
         (unsigned target, long offset, long size, const void *data),
         (target, offset, size, data), (unsigned, long, long, const void *))
GLW_VOID(shim_glTexSubImage2D, GLW_TEXSUB, "glTexSubImage2D",
         (unsigned target, int level, int x, int y, int w, int h, unsigned fmt,
          unsigned type, const void *px),
         (target, level, x, y, w, h, fmt, type, px),
         (unsigned, int, int, int, int, int, unsigned, unsigned, const void *))
GLW_VOID(shim_glFinish, GLW_FINISH, "glFinish", (void), (), (void))
GLW_VOID(shim_glFlush, GLW_FLUSH, "glFlush", (void), (), (void))
/*
 * WHO READS THE SCREEN BACK.
 *
 * Measured on the owner's phone (14.09.2026): every hitch in play holds exactly
 * ONE glReadPixels of 9 to 18 ms, while the swap takes none and the card's own
 * clock says the frame cost ten. A read of the framebuffer drains the pipe and
 * waits for the card - a quarter of the samples sat in ioctl 0x400c0907 on
 * /dev/kgsl-3d0, the kernel's wait-for-timestamp - so the wait IS the read.
 *
 * Our layer does not call it; the game does. This says from where, and what it
 * reads, so the call can be found in the image and understood. The report build
 * alone: it costs a return address and a handful of lines.
 */
#if ISAAC_DIAG
static void read_pixels_seen(int x, int y, int w, int h, unsigned fmt, unsigned type,
                             unsigned long c0, unsigned long c1, unsigned long c2, double ms) {
    static int said;
    int shim_report_active(void);
    void report_line(const char *, const char *);
    if (said >= 16 || !shim_report_active()) return;
    said++;
    /*
     * WHETHER A CACHE COULD ANSWER THIS. The read costs what it costs because
     * the driver must finish everything queued for that framebuffer first. If
     * NOTHING is drawn into it between reads, the picture is standing still and
     * our own copy would give the same bytes without the wait. So the target is
     * named, and with it the draws that have gone into it since the last read.
     */
    extern long g_gl_last_fb, g_gl_draws_by_fb[8];
    static long drawn_before[8];
    long fb = g_gl_last_fb, since = -1;
    if (fb >= 0 && fb < 8) {
        since = g_gl_draws_by_fb[fb] - drawn_before[fb];
        drawn_before[fb] = g_gl_draws_by_fb[fb];
    }
    unsigned long base = shim_image_base(), at[3] = {c0, c1, c2};
    char who[90];
    int n = 0;
    for (int i = 0; i < 3; i++) {
        int ours = base && at[i] > base && at[i] - base < 0x1000000;
        n += snprintf(who + n, sizeof(who) - (size_t) n, "%s%s0x%lx",
                      i ? " <- " : "", ours ? "" : "?", ours ? at[i] - base : at[i]);
        if (n >= (int) sizeof(who) - 24) break;
    }
    char line[240];
    snprintf(line, sizeof(line),
             "glReadPixels %d,%d %dx%d fmt 0x%x type 0x%x took %.1f ms, target %ld,"
             " drawn into since the last read %ld, from %s",
             x, y, w, h, fmt, type, ms, fb, since, who);
    report_line("gl", line);
}
#endif


void shim_glReadPixels(int x, int y, int w, int h, unsigned fmt, unsigned type, void *px) {
    static void (*p)(int, int, int, int, unsigned, unsigned, void *);
    if (!p) p = (void (*)(int, int, int, int, unsigned, unsigned, void *))
                glw_get(GLW_READPIXELS, "glReadPixels");
    if (!p) return;
    long long a = gl_now();
    p(x, y, w, h, fmt, type, px);
    long long spent = gl_now() - a;
    g_glw_ns[GLW_READPIXELS] += spent;
    g_glw_n[GLW_READPIXELS]++;
    DIAG(shim_gpu_pass_glw(GLW_READPIXELS));
#if ISAAC_DIAG
    read_pixels_seen(x, y, w, h, fmt, type,
                     (unsigned long) __builtin_return_address(0),
                     (unsigned long) __builtin_return_address(1),
                     (unsigned long) __builtin_return_address(2), (double) spent / 1e6);
#endif
}
GLW_VOID(shim_glBindTexture, GLW_BINDTEX, "glBindTexture",
         (unsigned target, unsigned tex), (target, tex), (unsigned, unsigned))
GLW_VOID(shim_glGenerateMipmap, GLW_MIPMAP, "glGenerateMipmap",
         (unsigned target), (target), (unsigned))
#if ISAAC_DIAG
/* the same wrapper as the macro gives, and the pass probe told what was attached */
void shim_glFramebufferTexture2D(unsigned target, unsigned att, unsigned textarget, unsigned tex,
                                 int level) {
    static void (*p)(unsigned, unsigned, unsigned, unsigned, int);
    if (!p) p = (void (*)(unsigned, unsigned, unsigned, unsigned, int))
                glw_get(GLW_FBTEX, "glFramebufferTexture2D");
    if (!p) return;
    shim_gpu_pass_leave_attach(target, att, tex);  /* stamped while the old texture is the target */
    long long a = gl_now();
    p(target, att, textarget, tex, level);
    g_glw_ns[GLW_FBTEX] += gl_now() - a;
    g_glw_n[GLW_FBTEX]++;
    shim_gpu_pass_attach(target, att, tex);
}
#else
GLW_VOID(shim_glFramebufferTexture2D, GLW_FBTEX, "glFramebufferTexture2D",
         (unsigned target, unsigned att, unsigned textarget, unsigned tex, int level),
         (target, att, textarget, tex, level), (unsigned, unsigned, unsigned, unsigned, int))
#endif
GLW_VOID(shim_glDeleteBuffers, GLW_DELBUF, "glDeleteBuffers",
         (int n, const unsigned *b), (n, b), (int, const unsigned *))
GLW_VOID(shim_glDeleteFramebuffers, GLW_DELFB, "glDeleteFramebuffers",
         (int n, const unsigned *f), (n, f), (int, const unsigned *))

unsigned shim_glCheckFramebufferStatus(unsigned target) {
    unsigned (*f)(unsigned) = (unsigned (*)(unsigned))
        glw_get(GLW_FBSTATUS, "glCheckFramebufferStatus");
    if (!f) return 0;
    long long a = gl_now();
    unsigned r = f(target);
    g_glw_ns[GLW_FBSTATUS] += gl_now() - a;
    g_glw_n[GLW_FBSTATUS]++;
    DIAG(shim_gpu_pass_status());
    return r;
}
void *shim_glMapBufferRange(unsigned target, long offset, long length, unsigned access) {
    void *(*f)(unsigned, long, long, unsigned) = (void *(*)(unsigned, long, long, unsigned))
        glw_get(GLW_MAP, "glMapBufferRange");
    if (!f) return 0;
    long long a = gl_now();
    void *r = f(target, offset, length, access);
    g_glw_ns[GLW_MAP] += gl_now() - a;
    g_glw_n[GLW_MAP]++;
    return r;
}
unsigned char shim_glUnmapBuffer(unsigned target) {
    unsigned char (*f)(unsigned) = (unsigned char (*)(unsigned)) glw_get(GLW_UNMAP, "glUnmapBuffer");
    if (!f) return 0;
    long long a = gl_now();
    unsigned char r = f(target);
    g_glw_ns[GLW_UNMAP] += gl_now() - a;
    g_glw_n[GLW_UNMAP]++;
    return r;
}



static const shim_entry_t g_shims[] = {
    {"_dispatch_once", (void *) shim_dispatch_once},
    {"_dispatch_async", (void *) shim_dispatch_async},
    {"_dispatch_queue_create", (void *) shim_dispatch_queue_create},
    {"__dispatch_main_q", (void *) shim_dispatch_main_q},
    {"__Block_object_assign", (void *) shim_Block_object_assign},
    {"__Block_object_dispose", (void *) shim_Block_object_dispose},
    {"__NSConcreteGlobalBlock", (void *) shim_NSConcreteGlobalBlock},
    {"__NSConcreteStackBlock", (void *) shim_NSConcreteStackBlock},
    /*
     * Apple's stack probe. Doing nothing here is right, but NOTHING exactly:
     * the generic stub puts its own number into x0 and wipes the argument, and
     * this function is called in the middle of somebody else's arithmetic.
     *
     * What doing nothing costs, and why the game thread now names its stack
     * size. The probe exists to walk a large frame down page by page, so that a
     * stack which is too small faults on the guard page at once. Only the main
     * thread grows on demand on Android; a thread's stack is a fixed mapping
     * with a single guard page, and the game moves the stack pointer 160 KB in
     * one instruction (0x618758), clearing that guard in a single bound.
     * Without the probe there is no fault — the writes land in whatever is
     * mapped below and quietly damage it. MainActivity therefore asks for a
     * stack far larger than any frame here, which is what makes the silence
     * safe.
     */
    {"___chkstk_darwin", (void *) shim_objc_ret_noop},
    {"_strlen", (void *) shim_strlen},
    {"___error", (void *) shim_error},
    {"_sprintf", (void *) shim_sprintf},
    {"_snprintf", (void *) shim_snprintf},
    {"_fprintf", (void *) shim_fprintf},
    {"_sscanf", (void *) shim_sscanf},
    {"_vsprintf", (void *) shim_vsprintf},
    {"_vsnprintf", (void *) shim_vsnprintf},
    {"_open", (void *) shim_open},
    {"_fopen", (void *) shim_fopen},
    {"_fread", (void *) shim_fread},
    {"_fwrite", (void *) shim_fwrite},
    {"_fclose", (void *) shim_fclose},
    {"_access", (void *) shim_access},
    /* the game's exit(): the save writer's queue first (safefile.c, hotfix 1.72) */
    {"_exit", (void *) shim_exit},
    {"_pthread_setname_np", (void *) shim_pthread_setname_np},
    {"_qsort", (void *) shim_qsort},
    {"___sincos_stret", (void *) shim_sincos_stret},
    {"___sincosf_stret", (void *) shim_sincosf_stret},
    {"___exp10", (void *) shim_exp10},
    {"_memset_pattern16", (void *) shim_memset_pattern16},
    {"___maskrune", (void *) shim_maskrune},
    {"___tolower", (void *) shim_tolower_rune},
    {"___toupper", (void *) shim_toupper_rune},
    {"___stderrp", (void *) &shim_stderrp},
    /*
     * setjmp/longjmp in Apple's layout (02.10.2026). The game's libpng 1.0.2
     * keeps error_fn right after a 192-byte jmp_buf (png_struct +0xc0), and
     * Android's setjmp puts its checksum exactly there: every png_error then
     * jumped to the checksum. These are the only two such names the image
     * imports (no __setjmp, no _sigsetjmp); the reasons are in darwin_jmp.S.
     */
    {"_setjmp", (void *) shim_darwin_setjmp},
    {"_longjmp", (void *) shim_darwin_longjmp},
    {"_pthread_cancel", (void *) shim_pthread_cancel},
    {"_alcOpenDevice", (void *) shim_alcOpenDevice},
    {"_alcCloseDevice", (void *) shim_alcCloseDevice},
    {"_alcCreateContext", (void *) shim_alcCreateContext},
    {"_alcMakeContextCurrent", (void *) shim_alcMakeContextCurrent},
    {"_alcProcessContext", (void *) shim_alcProcessContext},
    {"_alcDestroyContext", (void *) shim_alcDestroyContext},
    {"_alGenBuffers", (void *) shim_alGenBuffers},
    {"_alDeleteBuffers", (void *) shim_alDeleteBuffers},
    {"_alBufferData", (void *) shim_alBufferData},
    {"_alGenSources", (void *) shim_alGenSources},
    {"_alDeleteSources", (void *) shim_alDeleteSources},
    {"_alSourcei", (void *) shim_alSourcei},
    {"_alSourcef", (void *) shim_alSourcef},
    {"_alSource3f", (void *) shim_alSource3f},
    {"_alListener3f", (void *) shim_alListener3f},
    {"_alListenerfv", (void *) shim_alListenerfv},
    {"_alSourcePlay", (void *) shim_alSourcePlay},
    {"_alSourcePause", (void *) shim_alSourcePause},
    {"_alSourceStop", (void *) shim_alSourceStop},
    {"_alSourceQueueBuffers", (void *) shim_alSourceQueueBuffers},
    {"_alSourceUnqueueBuffers", (void *) shim_alSourceUnqueueBuffers},
    {"_alGetSourcei", (void *) shim_alGetSourcei},
    {"_alGetSourcef", (void *) shim_alGetSourcef},
    {"_alGetError", (void *) shim_alGetError},
    {"_glDrawArrays", (void *) shim_glDrawArrays},
    {"_glDrawElements", (void *) shim_glDrawElements},
    {"_glClear", (void *) shim_glClear},
    {"_glUseProgram", (void *) shim_glUseProgram},
    {"_glViewport", (void *) shim_glViewport},
    {"_glBindFramebuffer", (void *) shim_glBindFramebuffer},
    {"_glFramebufferRenderbuffer", (void *) shim_glFramebufferRenderbuffer},
    {"_glTexImage2D", (void *) shim_glTexImage2D},
    {"_glDeleteTextures", (void *) shim_glDeleteTextures},
    {"_glBindBuffer", (void *) shim_glBindBuffer},
    {"_glBindVertexArray", (void *) shim_glBindVertexArray},
    {"_glVertexAttribPointer", (void *) shim_glVertexAttribPointer},
    {"_glCompileShader", (void *) shim_glCompileShader},
    {"_glLinkProgram", (void *) shim_glLinkProgram},
    {"_glShaderSource", (void *) shim_glShaderSource},
    {"_glAttachShader", (void *) shim_glAttachShader},
    {"_glBufferData", (void *) shim_glBufferData},
    {"_glBufferSubData", (void *) shim_glBufferSubData},
    {"_glTexSubImage2D", (void *) shim_glTexSubImage2D},
    {"_glFinish", (void *) shim_glFinish},
    {"_glFlush", (void *) shim_glFlush},
    {"_glReadPixels", (void *) shim_glReadPixels},
    {"_glBindTexture", (void *) shim_glBindTexture},
    {"_glGenerateMipmap", (void *) shim_glGenerateMipmap},
    {"_glFramebufferTexture2D", (void *) shim_glFramebufferTexture2D},
    {"_glCheckFramebufferStatus", (void *) shim_glCheckFramebufferStatus},
    {"_glDeleteBuffers", (void *) shim_glDeleteBuffers},
    {"_glDeleteFramebuffers", (void *) shim_glDeleteFramebuffers},
    {"_glMapBufferRange", (void *) shim_glMapBufferRange},
    {"_glUnmapBuffer", (void *) shim_glUnmapBuffer},
    /*
     * The streams. Every one of these used to come from the phone's own libc++,
     * whose field offsets are not the ones this image was compiled against.
     * See cxxstream.c for what that cost.
     */
    {"__ZNSt3__18ios_base4initEPv", (void *) shim_ios_init},
    {"__ZNSt3__18ios_base5clearEj", (void *) shim_ios_clear},
    {"__ZNSt3__18ios_base33__set_badbit_and_consider_rethrowEv", (void *) shim_ios_badbit},
    {"__ZNSt3__113basic_istreamIcNS_11char_traitsIcEEE6sentryC1ERS3_b", (void *) shim_istream_sentry},
    {"__ZNSt3__113basic_ostreamIcNS_11char_traitsIcEEE6sentryC1ERS3_", (void *) shim_ostream_sentry_ctor},
    {"__ZNSt3__113basic_ostreamIcNS_11char_traitsIcEEE6sentryD1Ev", (void *) shim_ostream_sentry_dtor},
    {"__ZNSt3__113basic_ostreamIcNS_11char_traitsIcEEE3putEc", (void *) shim_ostream_put},
    {"__ZNSt3__113basic_ostreamIcNS_11char_traitsIcEEE5flushEv", (void *) shim_ostream_flush},
    {"__ZNSt3__113basic_ostreamIcNS_11char_traitsIcEEElsEl", (void *) shim_ostream_put_long},
    {"__ZNSt3__114basic_iostreamIcNS_11char_traitsIcEEED2Ev", (void *) shim_iostream_dtor},
    {"__ZNSt3__19basic_iosIcNS_11char_traitsIcEEED2Ev", (void *) shim_basic_ios_dtor},
    {"__ZNSt3__115basic_streambufIcNS_11char_traitsIcEEEC2Ev", (void *) shim_streambuf_ctor},
    {"__ZNSt3__115basic_streambufIcNS_11char_traitsIcEEED2Ev", (void *) shim_streambuf_dtor},
    {"__ZNSt3__16localeD1Ev", (void *) shim_locale_dtor},
    /*
     * The locale, and the facet a stream asks it for. basic_ios::fill() goes
     * through both to work out a space character the first time anything is
     * written, so leaving them to the phone left half a stream foreign.
     */
    {"__ZNKSt3__18ios_base6getlocEv", (void *) shim_ios_getloc_abi},
    {"__ZNKSt3__16locale9use_facetERNS0_2idE", (void *) shim_use_facet},
    {"__ZNSt3__112__rs_defaultD1Ev", (void *) shim_rs_dtor},
    {"__ZNSt3__112__rs_defaultclEv", (void *) shim_rs_call},
    {"__ZNSt3__18__rs_getEv", (void *) shim_rs_get},
    {STR "C1ERKS5_", (void *) shim_str_copy_ctor},
    {STR "C1ERKS5_mmRKS4_", (void *) shim_str_sub_ctor},
    {STR "D1Ev", (void *) shim_str_dtor},
    {STR "aSERKS5_", (void *) shim_str_assign_str},
    {STR "6appendEPKc", (void *) shim_str_append},
    {STR "6appendEPKcm", (void *) shim_str_append_n},
    {STR "6assignEPKc", (void *) shim_str_assign},
    {STR "9push_backEc", (void *) shim_str_push_back},
    {STR "6insertEmPKc", (void *) shim_str_insert},
    {STR "6insertEmPKcm", (void *) shim_str_insert_n},
    {STR "6insertEmmc", (void *) shim_str_insert_fill},
    {STR "5eraseEmm", (void *) shim_str_erase},
    {STR "6resizeEmc", (void *) shim_str_resize},
    {STR "7replaceEmmPKc", (void *) shim_str_replace},
    {STR "7replaceEmmPKcm", (void *) shim_str_replace_n},
    {"__ZNSt3__115basic_stringbufIcNS_11char_traitsIcEENS_9allocatorIcEEE3strERKNS_12basic_stringIcS2_S4_EE", (void *) shim_strbuf_str},
    {STRK "7compareEPKc", (void *) shim_str_compare_c},
    {STRK "7compareEmmPKc", (void *) shim_str_compare_sub_c},
    {SBUF "3strEv", (void *) shim_strbuf_str_get_abi},
    {"__ZNSt3__19to_stringEx", (void *) shim_str_to_string_abi},
    {"__ZNSt3__1plIcNS_11char_traitsIcEENS_9allocatorIcEEEENS_12basic_stringIT_T0_T1_EEPKS6_RKS9_",
     (void *) shim_str_concat_cs_abi},
    {"_pthread_mutexattr_settype", (void *) shim_mutexattr_settype},
    {"_opendir", (void *) shim_opendir},
    {"_readdir", (void *) shim_readdir},
    {"_closedir", (void *) shim_closedir},
    {"_NSLog", (void *) shim_NSLog},
    {"___assert_rtn", (void *) shim_assert_rtn},
    {"_objc_msgSend", (void *) shim_objc_msgSend},
    {"_objc_msgSendSuper2", (void *) shim_objc_msgSendSuper2},
    {"_objc_retain", (void *) shim_objc_retain},
    {"_objc_release", (void *) shim_objc_release},
    {"_objc_autoreleaseReturnValue", (void *) shim_objc_autoreleaseReturnValue},
    {"_objc_retainAutorelease", (void *) shim_objc_retainAutorelease},
    {"_objc_retainAutoreleaseReturnValue", (void *) shim_objc_retainAutoreleaseReturnValue},
    {"_objc_retainAutoreleasedReturnValue", (void *) shim_objc_retainAutoreleasedReturnValue},
    {"_objc_claimAutoreleasedReturnValue", (void *) shim_objc_claimAutoreleasedReturnValue},
    {"_objc_unsafeClaimAutoreleasedReturnValue", (void *) shim_objc_unsafeClaimAutoreleasedReturnValue},
    {"_objc_storeStrong", (void *) shim_objc_storeStrong},
    {"_objc_enumerationMutation", (void *) shim_objc_enumerationMutation},
    {"_objc_alloc", (void *) shim_objc_alloc},
    {"_objc_alloc_init", (void *) shim_objc_alloc_init},
    {"_objc_opt_class", (void *) shim_objc_opt_class},
    {"_objc_opt_isKindOfClass", (void *) shim_objc_opt_isKindOfClass},
    {"_objc_getProperty", (void *) shim_objc_getProperty},
    {"_objc_setProperty_atomic", (void *) shim_objc_setProperty_atomic},
    {"_objc_setProperty_nonatomic_copy", (void *) shim_objc_setProperty_nonatomic_copy},
    {"_UIApplicationMain", (void *) shim_UIApplicationMain},
    {"_NSStringFromClass", (void *) shim_NSStringFromClass},
    {"__objc_empty_cache", (void *) shim_objc_empty_cache},
    /* the variants carrying a register number: with identity this is just a return */
    {"_objc_retain_x1", (void *) shim_objc_retain_x1},
    {"_objc_retain_x2", (void *) shim_objc_retain_x2},
    {"_objc_retain_x8", (void *) shim_objc_retain_x8},
    {"_objc_retain_x19", (void *) shim_objc_retain_x19},
    {"_objc_retain_x20", (void *) shim_objc_retain_x20},
    {"_objc_retain_x21", (void *) shim_objc_retain_x21},
    {"_objc_retain_x23", (void *) shim_objc_retain_x23},
    {"_objc_retain_x24", (void *) shim_objc_retain_x24},
    {"_objc_release_x1", (void *) shim_objc_ret_noop},
    {"_objc_release_x8", (void *) shim_objc_ret_noop},
    {"_objc_release_x19", (void *) shim_objc_ret_noop},
    {"_objc_release_x20", (void *) shim_objc_ret_noop},
    {"_objc_release_x21", (void *) shim_objc_ret_noop},
    {"_objc_release_x22", (void *) shim_objc_ret_noop},
    {"_objc_release_x23", (void *) shim_objc_ret_noop},
    {"_bzero", (void *) shim_bzero},
    {"_CFCopyHomeDirectoryURL", (void *) shim_CFCopyHomeDirectoryURL},
    {"_CFBundleGetMainBundle", (void *) shim_CFBundleGetMainBundle},
    {"_CFBundleCopyResourcesDirectoryURL", (void *) shim_CFBundleCopyResourcesDirectoryURL},
    {"_CFURLGetFileSystemRepresentation", (void *) shim_CFURLGetFileSystemRepresentation},
    {"_CFRelease", (void *) shim_CFRelease},
    {"_objc_autoreleasePoolPush", (void *) shim_objc_autoreleasePoolPush},
    {"_objc_autoreleasePoolPop", (void *) shim_objc_autoreleasePoolPop},
    {NULL, NULL}
};

void *shim_objc_make_class(const char *bind_name);

void *shim_look_up(const char *name) {
    for (int i = 0; g_shims[i].name; i++)
        if (!strcmp(g_shims[i].name, name)) return g_shims[i].fn;
    /*
     * The string buffer's method table is our own: two of its slots walk the
     * string, and our string layout is the Apple one. The address is not a
     * constant, hence a separate line rather than the common list.
     */
    if (!strcmp(name, "__ZTVNSt3__115basic_stringbufIcNS_11char_traitsIcEENS_9allocatorIcEEEE"))
        return shim_stringbuf_table();
    /*
     * The stringstream's method table and its VTT. Built by us, from the shape
     * the game's own constructor demands, so that no part of a stream comes
     * from a libc++ that lays its objects out differently.
     */
    if (!strcmp(name, "__ZTVNSt3__118basic_stringstreamIcNS_11char_traitsIcEENS_9allocatorIcEEEE"))
        return shim_stringstream_table();
    if (!strcmp(name, "__ZTTNSt3__118basic_stringstreamIcNS_11char_traitsIcEENS_9allocatorIcEEEE"))
        return shim_stringstream_vtt();
    /* a facet's identity is a data word, not a function */
    if (!strcmp(name, "__ZNSt3__15ctypeIcE2idE")) return shim_ctype_id();
    /* the character class table — its address is not a constant either */
    if (!strcmp(name, "__DefaultRuneLocale")) return shim_rune_table();
    /* iOS classes and metaclasses — on demand, straight from the binding name */
    void *cls = shim_objc_make_class(name);
    if (cls) return cls;
    return NULL;
}
