/*
 * The one writer for every file that holds the player's state (P1, 30.09.2026),
 * and since hotfix 1.72 its queue: the game's own writes leave the frame thread.
 *
 * The first half is shim.c's writer moved here as it was (.tmp, fsync, rename,
 * the in-place copy behind a .ready mark, the start-up recovery and the storage
 * probe) - unchanged, so that it can be compiled and tested on the desk alone
 * (tools/test_perf172). The second half is new: THE WRITER THREAD.
 */
#define _GNU_SOURCE
#define _BSD_SOURCE                         /* funopen */
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
#ifndef DESK_TEST
#include <android/log.h>
#include <jni.h>
#endif
#include "diag.h"

#if ISAAC_DIAG && !defined(DESK_TEST)
#define LOG(...) __android_log_print(ANDROID_LOG_INFO, "isaac", __VA_ARGS__)
#else
#define LOG(...) do { } while (0)      /* the private build has no log */
#endif

unsigned long shim_image_base(void);
static int game_close(FILE *f, int *ret);
static void written_hook(const char *final, long bytes, long ms, const char *how, const char *who);
void shim_safe_wait_path(const char *path);

/*
 * ONE RULE FOR EVERY FILE THAT HOLDS THE PLAYER'S STATE (P1, 30.09.2026).
 *
 * It began with the saves (the owner's item 27, 23.09.2026): the game writes
 * them in place - fopen "wb" (0x8af3f4) empties the file, then fwrite and
 * fclose, and the result of the fwrite is never looked at (0x8e1890). A write
 * that stops half way leaves a short file, and the next launch meets a save
 * that does not read and writes an empty one over it (0x6fec18: the checksum
 * fails, 0x6fe8cc resets, 0x6fecbc writes).
 *
 * The same holds for options.ini (0x7ebc90, fopen "w" and no checks: an empty
 * file reads as Version=0 and the game fills in its defaults), for our own
 * port-touch.ini and mods.txt. So there is one writer for all of them, for C
 * and - in MediaFs.java - for Java, and it goes like this:
 *
 *   1. the new content goes to name.tmp, every write checked, then fflush
 *      and fsync;
 *   2. rename(name.tmp, name) - one step, the old file or the new one, never
 *      half of either;
 *   3. where rename is refused (the media folder answers through MediaProvider,
 *      and it did refuse on 28.09) the copy is written over the old file IN
 *      PLACE, but only after an empty name.ready says "the .tmp is whole": a
 *      copy cut short can then be finished from the .tmp at the next start;
 *   4. .tmp and .ready are removed.
 *
 * After the first refused rename a file is written in place for the rest of
 * the session, and a folder the start-up probe (shim_storage_probe) found
 * refusing is written in place from the start. A .tmp that failed to be
 * written whole is dropped: the old file is still there.
 *
 * At start, before anybody reads (shim_safe_recover_dir):
 *   .tmp + .ready   the copy is whole - it is put in place;
 *   .tmp alone      the write broke off - the .tmp goes, the old file stands;
 *   .ready alone    the step after the rename broke off - the mark goes.
 *
 * The API, for anyone in the layer (usermods.c writes mods.txt through it):
 *
 *   FILE *shim_safe_fopen(const char *path, const char *mode);
 *         "w", "wb", "wt": a stream into path.tmp; any other mode is a plain
 *         fopen. NULL when neither the .tmp nor the file itself would open.
 *   int shim_safe_fclose(FILE *f);
 *         0 when the new content is in place whole, EOF (-1) with errno when
 *         it is not - and
 *         then the old file is untouched (or, if an in-place copy broke off,
 *         finished from the .tmp at the next start). Any other FILE is simply
 *         closed.
 *   int shim_safe_write_file(const char *path, const void *data, size_t n);
 *         the two above in one call; 0 on success.
 */
int shim_report_active(void);
void report_line(const char *, const char *);
const char *shim_saves_dir(void);

#if ISAAC_DIAG
#define SAY(kind, ...) do { if (shim_report_active()) { char say_[480];                    \
        snprintf(say_, sizeof(say_), __VA_ARGS__); report_line(kind, say_); } } while (0)
#else
#define SAY(kind, ...) do { } while (0)
#endif

FILE *shim_safe_fopen(const char *path, const char *mode);
int shim_safe_fclose(FILE *f);
int shim_safe_write_file(const char *path, const void *data, size_t n);
void shim_safe_recover_dir(const char *dir);
int shim_storage_probe(const char *dir);

/* the save guard and the settings guard (saves.c) */
void shim_saves_before_write(const char *path);
void shim_saves_written(const char *path, long bytes, long ms, const char *how, const char *who);
int shim_saves_frozen(const char *path);

__attribute__((unused)) static const char *leaf_of(const char *p) {
    const char *s = p ? strrchr(p, '/') : NULL;
    return s ? s + 1 : (p ? p : "?");
}

static long long safe_now_ms(void) {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (long long) t.tv_sec * 1000 + t.tv_nsec / 1000000;
}

#define SAFE_MAX 16
static struct {
    FILE *f;
    char final[512], tmp[520];
    int failed;
    long long t0;
    char who[96];                   /* who opened it, for the settings guard (report builds) */
} g_safe[SAFE_MAX];
static pthread_mutex_t g_safe_lock = PTHREAD_MUTEX_INITIALIZER;

/*
 * Where rename is not tried any more: a file whose rename was refused this
 * session, or a folder (kept with a trailing slash) the start-up probe found
 * refusing.
 */
#define INPLACE_MAX 32
static char g_inplace[INPLACE_MAX][520];
static int g_inplace_n;

static int inplace_wanted(const char *final) {
    int yes = 0;
    pthread_mutex_lock(&g_safe_lock);
    for (int i = 0; i < g_inplace_n && !yes; i++) {
        size_t n = strlen(g_inplace[i]);
        if (n && g_inplace[i][n - 1] == '/') {
            const char *slash = strrchr(final, '/');
            yes = slash && (size_t) (slash - final + 1) == n && !strncmp(final, g_inplace[i], n);
        } else {
            yes = !strcmp(final, g_inplace[i]);
        }
    }
    pthread_mutex_unlock(&g_safe_lock);
    return yes;
}

static void inplace_mark(const char *what) {
    pthread_mutex_lock(&g_safe_lock);
    int have = 0;
    for (int i = 0; i < g_inplace_n && !have; i++) have = !strcmp(g_inplace[i], what);
    if (!have && g_inplace_n < INPLACE_MAX)
        snprintf(g_inplace[g_inplace_n++], sizeof(g_inplace[0]), "%s", what);
    pthread_mutex_unlock(&g_safe_lock);
}

static int write_all_fd(int fd, const void *p, size_t n) {
    const char *c = (const char *) p;
    while (n) {
        ssize_t k = write(fd, c, n);
        if (k < 0 && errno == EINTR) continue;
        if (k <= 0) return -1;
        c += k;
        n -= (size_t) k;
    }
    return 0;
}

/* the whole of `from` over `to`, in place, flushed to the disk; 0 when every byte is there */
static int copy_in_place(const char *from, const char *to) {
    int in = open(from, O_RDONLY | O_CLOEXEC);
    if (in < 0) return -1;
    int out = open(to, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0660);
    if (out < 0) { close(in); return -1; }
    char buf[16384];
    ssize_t k;
    int bad = 0;
    while ((k = read(in, buf, sizeof(buf))) != 0) {
        if (k < 0) { if (errno == EINTR) continue; bad = 1; break; }
        if (write_all_fd(out, buf, (size_t) k)) { bad = 1; break; }
    }
    if (!bad && fsync(out) != 0) bad = 1;
    if (close(out) != 0) bad = 1;
    close(in);
    return bad ? -1 : 0;
}

static int touch_ready(const char *ready) {
    int fd = open(ready, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0660);
    if (fd < 0) return -1;
    int bad = fsync(fd) != 0;
    if (close(fd) != 0) bad = 1;
    return bad ? -1 : 0;
}

/*
 * The whole, flushed .tmp into final's place. 0 done (`how` says which way),
 * 1 not done and the old file untouched, 2 not done and the copy left for the
 * next start to finish (.tmp and .ready both there).
 */
static int commit(const char *tmp, const char *final, const char **how) {
    char ready[530];
    snprintf(ready, sizeof(ready), "%s.ready", final);
    if (!inplace_wanted(final)) {
        if (rename(tmp, final) == 0) { *how = "rename"; return 0; }
        int e = errno;
        inplace_mark(final);
        SAY("storage", "rename %s FAILED, errno %d - written in place from now on", leaf_of(final), e);
        (void) e;
    }
    if (touch_ready(ready) != 0) return 1;       /* without the mark a copy cut short could not be told */
    if (copy_in_place(tmp, final) != 0) return 2;
    unlink(tmp);
    unlink(ready);
    *how = "in-place";
    return 0;
}

static FILE *safe_open_at(const char *path, const char *mode, const char *who) {
    if (!path || !mode || mode[0] != 'w' || strchr(mode, '+')) return fopen(path, mode);
    char tmp[520], ready[530];
    if ((size_t) snprintf(tmp, sizeof(tmp), "%s.tmp", path) >= sizeof(tmp)) return fopen(path, mode);
    snprintf(ready, sizeof(ready), "%s.ready", path);
    unlink(ready);                          /* a mark from before belongs to no copy now */
    FILE *f = fopen(tmp, mode);
    if (!f) {
        /* no room for a copy beside it: in place, as it always was - better than nothing */
        SAY("storage", "%s.tmp did not open (errno %d) - written in place", leaf_of(path), errno);
        return fopen(path, mode);
    }
    pthread_mutex_lock(&g_safe_lock);
    int slot = -1;
    for (int i = 0; i < SAFE_MAX; i++) if (!g_safe[i].f) { slot = i; break; }
    if (slot < 0) {
        pthread_mutex_unlock(&g_safe_lock);
        fclose(f);
        unlink(tmp);
        return fopen(path, mode);
    }
    g_safe[slot].f = f;
    snprintf(g_safe[slot].final, sizeof(g_safe[slot].final), "%s", path);
    snprintf(g_safe[slot].tmp, sizeof(g_safe[slot].tmp), "%s", tmp);
    snprintf(g_safe[slot].who, sizeof(g_safe[slot].who), "%s", who ? who : "");
    g_safe[slot].failed = 0;
    g_safe[slot].t0 = safe_now_ms();
    pthread_mutex_unlock(&g_safe_lock);
    return f;
}

static int port_file_queued(const char *path);
FILE *shim_safe_game_open(const char *path, const char *mode, const char *who);

FILE *shim_safe_fopen(const char *path, const char *mode) {
    /* the port's own state files go onto the writer's queue as the game's do (THE PORT'S OWN FILES) */
    if (port_file_queued(path) && mode && mode[0] == 'w' && !strchr(mode, '+'))
        return shim_safe_game_open(path, mode, "the port");
    shim_safe_wait_path(path);              /* after the game's own queued write of it, never before */
    return safe_open_at(path, mode, "the port");
}

int shim_safe_fclose(FILE *f) {
    if (!f) return EOF;
    { int r; if (game_close(f, &r)) return r; }      /* a game stream: onto the writer's queue */
    int slot = -1;
    char final[512] = "", tmp[520] = "", who[96] = "";
    int failed = 0;
    long long t0 = 0;
    pthread_mutex_lock(&g_safe_lock);
    for (int i = 0; i < SAFE_MAX; i++) if (g_safe[i].f == f) { slot = i; break; }
    if (slot >= 0) {
        snprintf(final, sizeof(final), "%s", g_safe[slot].final);
        snprintf(tmp, sizeof(tmp), "%s", g_safe[slot].tmp);
        snprintf(who, sizeof(who), "%s", g_safe[slot].who);
        failed = g_safe[slot].failed;
        t0 = g_safe[slot].t0;
        g_safe[slot].f = NULL;
    }
    pthread_mutex_unlock(&g_safe_lock);
    if (slot < 0) return fclose(f);

    long bytes = -1;
    if (fflush(f) != 0 || ferror(f)) failed = 1;
    if (!failed) {
        struct stat st;
        if (fstat(fileno(f), &st) == 0) bytes = (long) st.st_size;
    }
    if (!failed && fsync(fileno(f)) != 0) failed = 1;
    if (fclose(f) != 0) failed = 1;
    const char *how = "";
    int pending = 0;
    if (!failed) {
        int r = commit(tmp, final, &how);
        if (r) { failed = 1; pending = r == 2; }
    }
    int err = failed ? (errno ? errno : EIO) : 0;       /* the caller is promised the cause in errno */
    if (failed && !pending) unlink(tmp);
    long ms = (long) (safe_now_ms() - t0);
    {
        static int said;
        if (failed) {
            SAY("saves", "NOT written, %s: %s", pending ? "finished from the copy at the next start"
                                                       : "the old one kept", leaf_of(final));
        } else if (said < 12 || ms > 100) {
            said++;
            SAY("saves", "%s %ld bytes written in %ld ms (%s)", leaf_of(final), bytes, ms, how);
        }
        (void) said;
    }
    if (!failed) written_hook(final, bytes, ms, how, who);
    if (failed) errno = err;
    return failed ? EOF : 0;
}

int shim_safe_write_file(const char *path, const void *data, size_t n) {
    FILE *f = shim_safe_fopen(path, "wb");
    if (!f) return -1;
    if (n && fwrite(data, 1, n, f) != n) {
        pthread_mutex_lock(&g_safe_lock);
        for (int i = 0; i < SAFE_MAX; i++) if (g_safe[i].f == f) g_safe[i].failed = 1;
        pthread_mutex_unlock(&g_safe_lock);
    }
    return shim_safe_fclose(f) == 0 ? 0 : -1;
}

/*
 * What a start finds of writes that broke off, put right before anybody
 * reads. Only this folder, not below it.
 */
static int ends_with(const char *s, const char *tail) {
    size_t a = strlen(s), b = strlen(tail);
    return a > b && !strcmp(s + a - b, tail);
}

void shim_safe_recover_dir(const char *dir) {
    if (!dir || !*dir) return;
    DIR *d = opendir(dir);
    if (!d) return;
    enum { NAMES = 64 };
    char (*names)[256] = calloc(NAMES, 256);
    int n = 0;
    struct dirent *e;
    while (names && (e = readdir(d)) != NULL && n < NAMES) {
        if (ends_with(e->d_name, ".tmp") || ends_with(e->d_name, ".ready")
            || !strncmp(e->d_name, ".port-selftest", 14))
            snprintf(names[n++], 256, "%s", e->d_name);
    }
    closedir(d);
    for (int i = 0; i < n; i++) {
        char path[800], final[800], other[820];
        snprintf(path, sizeof(path), "%s/%s", dir, names[i]);
        if (!strncmp(names[i], ".port-selftest", 14)) { unlink(path); continue; }
        struct stat st;
        if (stat(path, &st) != 0 || !S_ISREG(st.st_mode)) continue;
        if (ends_with(names[i], ".tmp")) {
            snprintf(final, sizeof(final), "%.*s", (int) (strlen(path) - 4), path);
            snprintf(other, sizeof(other), "%s.ready", final);
            if (access(other, F_OK) == 0) {
                /* the copy is whole: rename, or the same copy in place when rename is refused */
                int ok = rename(path, final) == 0;
                if (!ok) ok = copy_in_place(path, final) == 0 && unlink(path) == 0;
                if (ok) unlink(other);
                SAY("saves", "%s: a whole copy from a broken-off write %s", leaf_of(final),
                    ok ? "put in place" : "could NOT be put in place, kept");
            } else {
                unlink(path);
                SAY("saves", "%s: a half-written copy dropped, the file itself stands", leaf_of(final));
            }
        } else {                                        /* name.ready */
            snprintf(final, sizeof(final), "%.*s", (int) (strlen(path) - 6), path);
            snprintf(other, sizeof(other), "%s.tmp", final);
            if (access(other, F_OK) != 0) unlink(path);
        }
    }
    free(names);
}

/*
 * HOW THIS FOLDER TAKES A WRITE (P4): a file written and flushed, renamed over
 * an existing one and read back, renamed to a new name, and written in place.
 * The answer is only the starting choice for the folder - rename or in place
 * behind a .ready mark; the fallback is on every single write anyway, since
 * the refusal of 28.09 was tied to one path the probe would not repeat. Report
 * builds run it ten times over: MediaProvider refusals come and go.
 */
static int put_file(const char *path, const char *text) {
    int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0660);
    if (fd < 0) return -1;
    int bad = write_all_fd(fd, text, strlen(text)) != 0 || fsync(fd) != 0;
    if (close(fd) != 0) bad = 1;
    return bad ? -1 : 0;
}

static int holds(const char *path, const char *text) {
    char buf[64];
    int fd = open(path, O_RDONLY | O_CLOEXEC);
    if (fd < 0) return 0;
    ssize_t k = read(fd, buf, sizeof(buf) - 1);
    close(fd);
    if (k < 0) return 0;
    buf[k] = 0;
    return !strcmp(buf, text);
}

int shim_storage_probe(const char *dir) {
    if (!dir || !*dir) return -1;
    int rounds = 1;
#if ISAAC_DIAG
    if (shim_report_active()) rounds = 10;
#endif
    char a[560], t[560], m[560];
    snprintf(a, sizeof(a), "%s/.port-selftest", dir);
    snprintf(t, sizeof(t), "%s/.port-selftest.new", dir);
    snprintf(m, sizeof(m), "%s/.port-selftest.moved", dir);
    int over_bad = 0, new_bad = 0, place_bad = 0, over_err = 0, new_err = 0, place_err = 0;
    long long t0 = safe_now_ms();
    for (int r = 0; r < rounds; r++) {
        char want[32];
        snprintf(want, sizeof(want), "new %d", r);
        if (put_file(a, "old") != 0 || put_file(t, want) != 0) { place_bad++; place_err = errno; }
        else if (rename(t, a) != 0) { over_bad++; over_err = errno; }
        else if (!holds(a, want)) { over_bad++; over_err = -1; }
        if (access(a, F_OK) == 0 && rename(a, m) != 0) { new_bad++; new_err = errno; }
        const char *at = access(m, F_OK) == 0 ? m : a;
        snprintf(want, sizeof(want), "in place %d", r);
        if (put_file(at, want) != 0 || !holds(at, want)) { place_bad++; place_err = errno; }
        unlink(a); unlink(t); unlink(m);
    }
    if (over_bad) {
        char slot[520];
        snprintf(slot, sizeof(slot), "%s/", dir);
        inplace_mark(slot);
    }
    SAY("storage", "%s: rename over an existing file %s (errno %d, %d of %d); to a new name %s (errno %d); "
        "in place %s (errno %d); strategy %s; %lld ms",
        dir, over_bad ? "FAILED" : "ok", over_err, over_bad, rounds, new_bad ? "FAILED" : "ok", new_err,
        place_bad ? "FAILED" : "ok", place_err, over_bad ? "in-place+ready" : "rename",
        safe_now_ms() - t0);
    (void) new_err; (void) place_err; (void) over_err; (void) t0; (void) new_bad; (void) place_bad;
    return over_bad ? 1 : 0;
}

/* ======================================================================= */
/*  THE WRITER THREAD (hotfix 1.72)                                        */
/* ======================================================================= */

/*
 * THE GAME'S SAVES LEAVE THE FRAME THREAD (hotfix 1.72, the owner's item 3 of
 * 07.10.2026). Since 1.7 one save is .tmp + fsync + rename on the media folder
 * (FUSE and MediaProvider), a read back, the backup inside the app with
 * another fsync, and the seats beside a run with a third - all of it on the
 * frame thread, between two frames: 3-18 ms on the owner's phone, tens to
 * hundreds of milliseconds on eMMC (the research notes of 07.10.2026,
 * performance, section 3).
 *
 * Now the game writes into memory. Its fopen "w"/"wb" of a file the writer
 * keeps (shim.c game_write_open) gets a stream over a growing buffer; fwrite,
 * fprintf and fseek land there; at fclose the whole content goes into a queue
 * and fclose returns at once. NSData's writeToFile: (the run file wiped on
 * death, uikit.c) hands over a copy the same way. One thread of its own writes
 * the queue out exactly as the frame thread used to: safe_open_at, every byte,
 * shim_safe_fclose - .tmp, fsync, rename, the .ready fallback - then the read
 * back, the backup and the seats (saves.c). The seats are coop.c's lines as
 * they are AT THE GAME'S WRITE, taken on the frame thread and carried along.
 *
 * The rules:
 *   - one writer thread, jobs in the order of the game's fclose: one file's
 *     writes reach the disk in the order the game made them;
 *   - a newer write of a file whose older write is still waiting replaces it
 *     and goes to the end of the queue: the older one is never written after
 *     the newer, nor at all;
 *   - whoever reads, lists, tests or removes a file with a write queued or on
 *     the disk right now waits for it first (shim_safe_wait_path, _wait_dir):
 *     the game's fopen, access, opendir and NSFileManager, our own readers -
 *     nobody reads a save older than the game's last write;
 *   - the queue is waited for when the app goes away (shim_app_pause, Java's
 *     onPause/onStop), before our own restarts, before an export or an
 *     import, and - briefly, by atomic loads only - by the crash handler;
 *   - an import seals the queue (savesFlush with seal): what is still waiting
 *     for a save file then is not written over the imported one.
 * Where a queue cannot be had - no memory for the job, no thread - the write
 * is done on the spot, the old way: never lost.
 */
typedef struct job {
    struct job *next;
    char path[512];
    char who[96];
    uint8_t *data;
    size_t len;
    char *seats;                    /* coop.c's lines beside a run, taken at the game's write */
    int seats_n;                    /* -1: not a run file, 0: no players to keep */
    long long queued_ms;
} job_t;

static pthread_mutex_t g_wq_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t g_wq_more = PTHREAD_COND_INITIALIZER;     /* a job for the writer */
static pthread_cond_t g_wq_done = PTHREAD_COND_INITIALIZER;     /* a job finished: the waiting look again */
static job_t *g_wq_head, *g_wq_tail;
static job_t *g_wq_busy;            /* the job on the disk right now */
static int g_wq_up;
static pthread_t g_wq_thread;
static int g_wq_pending;            /* queued + on the disk; read without the lock by the crash handler */
static long g_wq_queued, g_wq_superseded, g_wq_written, g_wq_dropped;
static volatile int g_wq_sealed;    /* an import is writing: the game's save files are not written any more */
static __thread const job_t *t_job; /* the job this thread is writing (written_hook) */

/* saves.c */
void shim_saves_written(const char *path, long bytes, long ms, const char *how, const char *who);
void shim_saves_written_job(const char *path, long bytes, long ms, const char *how, const char *who,
                            const char *seats, int seats_n);
int shim_saves_seats_now(const char *path, char *out, int room);
int shim_saves_frozen(const char *path);

static void written_hook(const char *final, long bytes, long ms, const char *how, const char *who) {
    if (t_job) shim_saves_written_job(final, bytes, ms, how, who, t_job->seats, t_job->seats_n);
    else shim_saves_written(final, bytes, ms, how, who);
}

static int on_writer(void) {
    return g_wq_up && pthread_equal(pthread_self(), g_wq_thread);
}

/* the old way, on whatever thread calls it: the .tmp, every byte, shim_safe_fclose */
static int write_now(const char *path, const char *who, const uint8_t *data, size_t len) {
    FILE *f = safe_open_at(path, "wb", who);
    if (!f) return -1;
    if (len && fwrite(data, 1, len, f) != len) {
        pthread_mutex_lock(&g_safe_lock);
        for (int i = 0; i < SAFE_MAX; i++) if (g_safe[i].f == f) g_safe[i].failed = 1;
        pthread_mutex_unlock(&g_safe_lock);
    }
    return shim_safe_fclose(f) == 0 ? 0 : -1;
}

static void job_free(job_t *j) {
    if (!j) return;
    free(j->data);
    free(j->seats);
    free(j);
}

static void job_run(const job_t *j) {
    if (g_wq_sealed && shim_saves_frozen(j->path)) {
        g_wq_dropped++;
        SAY("saves", "%s: a write still queued when the import began - not written over it", leaf_of(j->path));
        return;
    }
    long long waited = safe_now_ms() - j->queued_ms;
    t_job = j;
    int r = write_now(j->path, j->who, j->data, j->len);
    t_job = NULL;
    {
        static int said;
        if (r != 0 || said < 12 || waited > 250) {
            said++;
            SAY("saves", "%s: the writer thread took the game's write %lld ms after its fclose (%zu bytes)%s",
                leaf_of(j->path), waited, j->len, r ? " - NOT written" : "");
        }
        (void) said;
    }
    (void) waited; (void) r;
}

static void *writer_main(void *arg) {
    (void) arg;
#ifndef DESK_TEST
    pthread_setname_np(pthread_self(), "isaac-saves");
#endif
    pthread_mutex_lock(&g_wq_lock);
    for (;;) {
        while (!g_wq_head) pthread_cond_wait(&g_wq_more, &g_wq_lock);
        job_t *j = g_wq_head;
        g_wq_head = j->next;
        if (!g_wq_head) g_wq_tail = NULL;
        j->next = NULL;
        g_wq_busy = j;
        pthread_mutex_unlock(&g_wq_lock);
        job_run(j);
        pthread_mutex_lock(&g_wq_lock);
        g_wq_busy = NULL;
        g_wq_written++;
        __atomic_sub_fetch(&g_wq_pending, 1, __ATOMIC_RELEASE);
        pthread_cond_broadcast(&g_wq_done);
        job_free(j);
    }
    return NULL;
}

/* is a write of `path` (or, with `dir`, of anything right inside or below it) queued or on the disk? */
static int busy_with(const char *path, int dir) {
    size_t n = strlen(path);
    for (job_t *j = g_wq_busy; j; j = NULL) {
        if (dir ? !strncmp(j->path, path, n) && j->path[n] == '/' : !strcmp(j->path, path)) return 1;
    }
    for (job_t *j = g_wq_head; j; j = j->next)
        if (dir ? !strncmp(j->path, path, n) && j->path[n] == '/' : !strcmp(j->path, path)) return 1;
    return 0;
}

/*
 * The game's write, in memory, onto the queue. Takes `data` (malloc'd) over.
 * 0 when it is queued or written; -1 when it could not be written at all.
 */
static int queue_put(const char *path, const char *who, uint8_t *data, size_t len) {
    job_t *j = calloc(1, sizeof(*j));
    if (!j || strlen(path) >= sizeof(j->path)) {
        /* no room for a job: on the spot, the old way, after whatever of this file is still queued */
        free(j);
        shim_safe_wait_path(path);
        int r = write_now(path, who, data, len);
        free(data);
        return r;
    }
    snprintf(j->path, sizeof(j->path), "%s", path);
    snprintf(j->who, sizeof(j->who), "%s", who ? who : "");
    j->data = data;
    j->len = len;
    j->queued_ms = safe_now_ms();
    /* the seats beside a run: what coop.c says NOW, at the game's write (saves.c) */
    {
        char seats[640];
        j->seats_n = shim_saves_seats_now(path, seats, (int) sizeof(seats));
        if (j->seats_n > 0) {
            j->seats = malloc((size_t) j->seats_n);
            if (j->seats) memcpy(j->seats, seats, (size_t) j->seats_n);
            else j->seats_n = -1;
        }
    }
    pthread_mutex_lock(&g_wq_lock);
    /* an older write of this file still waiting: it is replaced, never written after this one */
    job_t *prev = NULL;
    for (job_t *k = g_wq_head; k; ) {
        job_t *next = k->next;
        if (!strcmp(k->path, j->path)) {
            if (prev) prev->next = next; else g_wq_head = next;
            if (g_wq_tail == k) g_wq_tail = prev;
            job_free(k);
            g_wq_superseded++;
            __atomic_sub_fetch(&g_wq_pending, 1, __ATOMIC_RELEASE);
        } else {
            prev = k;
        }
        k = next;
    }
    if (!g_wq_up) {
        pthread_attr_t a;
        pthread_attr_init(&a);
        pthread_attr_setdetachstate(&a, PTHREAD_CREATE_DETACHED);
        g_wq_up = pthread_create(&g_wq_thread, &a, writer_main, NULL) == 0;
        pthread_attr_destroy(&a);
        if (!g_wq_up) {
            /* no thread: this write and every later one on the spot, as before 1.72 */
            pthread_mutex_unlock(&g_wq_lock);
            LOG("saves: the writer thread did not start - written on the frame thread");
            int r = write_now(j->path, j->who, j->data, j->len);
            job_free(j);
            return r;
        }
    }
    if (g_wq_tail) g_wq_tail->next = j; else g_wq_head = j;
    g_wq_tail = j;
    g_wq_queued++;
    __atomic_add_fetch(&g_wq_pending, 1, __ATOMIC_RELEASE);
    pthread_cond_signal(&g_wq_more);
    pthread_mutex_unlock(&g_wq_lock);
    return 0;
}

/* ---------------------------------------------------------- the game's streams */

/*
 * A write stream over memory. funopen, as saves.c's mended reads: every stdio
 * call the game makes (fwrite, fprintf, fseek, ftell) works on it as on a file;
 * a seek past the end and a write there leaves zeroes between, as a file does.
 */
typedef struct {
    uint8_t *d;
    size_t len, cap, at;
    int bad;
} memw_t;

static int memw_write(void *cookie, const char *buf, int n) {
    memw_t *m = (memw_t *) cookie;
    if (n < 0) { errno = EINVAL; return -1; }
    size_t end = m->at + (size_t) n;
    if (end > m->cap) {
        size_t cap = m->cap ? m->cap : 16384;
        while (cap < end) cap *= 2;
        uint8_t *d = (uint8_t *) realloc(m->d, cap);
        if (!d) { m->bad = 1; errno = ENOMEM; return -1; }
        memset(d + m->cap, 0, cap - m->cap);
        m->d = d;
        m->cap = cap;
    }
    if (n) memcpy(m->d + m->at, buf, (size_t) n);
    m->at = end;
    if (end > m->len) m->len = end;
    return n;
}

static fpos_t memw_seek(void *cookie, fpos_t off, int whence) {
    memw_t *m = (memw_t *) cookie;
    long long base = whence == SEEK_SET ? 0 : whence == SEEK_CUR ? (long long) m->at
                   : whence == SEEK_END ? (long long) m->len : -1;
    if (base < 0 || base + (long long) off < 0) { errno = EINVAL; return (fpos_t) -1; }
    m->at = (size_t) (base + (long long) off);
    return (fpos_t) m->at;
}

static int memw_close(void *cookie) { (void) cookie; return 0; }   /* the bytes are taken by game_close */

#define GAME_MAX 16
static struct {
    FILE *f;
    memw_t *m;
    char path[512];
    char who[96];
    int failed;
} g_game[GAME_MAX];                 /* under g_safe_lock */

/*
 * The game opens a file of the writer's for writing: a stream into memory, or
 * - with no memory or no slot - the old synchronous stream (safe_open_at),
 * after whatever of this file is still queued.
 */
FILE *shim_safe_game_open(const char *path, const char *mode, const char *who) {
    if (!path || !mode || mode[0] != 'w' || strchr(mode, '+') || strlen(path) >= sizeof(g_game[0].path))
        return safe_open_at(path, mode, who);
    memw_t *m = (memw_t *) calloc(1, sizeof(*m));
    FILE *f = NULL;
    if (m) {
#ifdef DESK_TEST
        f = tmpfile();                  /* the desk has no funopen: the same bytes through a file */
#else
        f = funopen(m, NULL, memw_write, memw_seek, memw_close);
#endif
    }
    int slot = -1;
    if (f) {
        pthread_mutex_lock(&g_safe_lock);
        for (int i = 0; i < GAME_MAX; i++) if (!g_game[i].f) { slot = i; break; }
        if (slot >= 0) {
            g_game[slot].f = f;
            g_game[slot].m = m;
            snprintf(g_game[slot].path, sizeof(g_game[slot].path), "%s", path);
            snprintf(g_game[slot].who, sizeof(g_game[slot].who), "%s", who ? who : "");
            g_game[slot].failed = 0;
        }
        pthread_mutex_unlock(&g_safe_lock);
    }
    if (slot >= 0) return f;
    if (f) fclose(f);
    free(m);
    shim_safe_wait_path(path);
    return safe_open_at(path, mode, who);
}

/* a game stream's close: 1 and *ret when the stream was one of ours */
static int game_close(FILE *f, int *ret) {
    int slot = -1;
    memw_t *m = NULL;
    char path[512], who[96];
    int failed = 0;
    pthread_mutex_lock(&g_safe_lock);
    for (int i = 0; i < GAME_MAX; i++) if (g_game[i].f == f) { slot = i; break; }
    if (slot >= 0) {
        m = g_game[slot].m;
        snprintf(path, sizeof(path), "%s", g_game[slot].path);
        snprintf(who, sizeof(who), "%s", g_game[slot].who);
        failed = g_game[slot].failed;
        g_game[slot].f = NULL;
        g_game[slot].m = NULL;
    }
    pthread_mutex_unlock(&g_safe_lock);
    if (slot < 0) return 0;
    if (fflush(f) != 0 || ferror(f)) failed = 1;
    uint8_t *data = NULL;
    size_t len = 0;
#ifdef DESK_TEST
    if (!failed && fseek(f, 0, SEEK_END) == 0) {
        long n = ftell(f);
        if (n >= 0) {
            data = (uint8_t *) malloc((size_t) n + 1);
            rewind(f);
            if (data && fread(data, 1, (size_t) n, f) == (size_t) n) len = (size_t) n;
            else failed = 1;
        } else {
            failed = 1;
        }
    }
    fclose(f);
#else
    fclose(f);
    if (m) {
        failed |= m->bad;
        data = m->d;
        len = m->len;
        m->d = NULL;
    }
#endif
    free(m);
    if (!failed && !data) data = (uint8_t *) malloc(1);     /* an empty file is a write as well */
    if (failed || !data) {
        free(data);
        SAY("saves", "NOT written, the game's write broke off in memory: %s - the old one kept", leaf_of(path));
        errno = EIO;
        *ret = EOF;
        return 1;
    }
    *ret = queue_put(path, who, data, len) == 0 ? 0 : EOF;
    return 1;
}

/*
 * THE PORT'S OWN FILES (hotfix 1.72, the owner's report of 07.10: a 64 ms
 * frame when SPAWNER GROUND was flipped in Tweaks - port-touch.ini's .tmp,
 * fsync, rename and the report's settings guard, all on the frame thread).
 * The port's state files whose readers either read them once at start or ask
 * through shim_safe_fopen_read / shim_safe_pending_path go onto the same queue
 * as the game's writes, with the same rules: port-touch.ini (sticks.c: Tweaks,
 * the touch editor, the spawner's toggles), the spawner's run marks and its
 * stat edits. mods.txt stays synchronous: usermods.c reads it back at once
 * with a plain fopen and tells the player when it did not stay.
 */
static int port_file_queued(const char *path) {
    static const char *const LEAVES[] = { "port-touch.ini", "port-spawner-runs.txt", "port-stat-edits.txt" };
    const char *leaf = path ? strrchr(path, '/') : NULL;
    leaf = leaf ? leaf + 1 : path;
    if (!leaf) return 0;
    for (unsigned i = 0; i < sizeof(LEAVES) / sizeof(LEAVES[0]); i++) if (!strcmp(leaf, LEAVES[i])) return 1;
    return 0;
}

/* NSData's writeToFile: of a file the writer keeps - a copy onto the queue */
int shim_safe_game_write(const char *path, const void *data, size_t n, const char *who) {
    if (!path) return -1;
    uint8_t *copy = (uint8_t *) malloc(n ? n : 1);
    if (!copy) {
        shim_safe_wait_path(path);
        return write_now(path, who, (const uint8_t *) (n ? data : ""), n);
    }
    if (n) memcpy(copy, data, n);
    return queue_put(path, who, copy, n);
}

/* a short write on a stream of either kind: the file is not written */
void shim_safe_mark_failed(FILE *f) {
    pthread_mutex_lock(&g_safe_lock);
    for (int i = 0; i < SAFE_MAX; i++) if (g_safe[i].f == f) g_safe[i].failed = 1;
    for (int i = 0; i < GAME_MAX; i++) if (g_game[i].f == f) g_game[i].failed = 1;
    pthread_mutex_unlock(&g_safe_lock);
}

/* ----------------------------------------------------------------- waiting */

static void wait_for(const char *path, int dir) {
    if (!path || !__atomic_load_n(&g_wq_pending, __ATOMIC_ACQUIRE) || on_writer()) return;
    long long t0 = 0;
    int waited = 0;
    pthread_mutex_lock(&g_wq_lock);
    while (busy_with(path, dir)) {
        if (!waited) { waited = 1; t0 = safe_now_ms(); }
        pthread_cond_wait(&g_wq_done, &g_wq_lock);
    }
    pthread_mutex_unlock(&g_wq_lock);
    if (waited) {
        static int said;
        if (said < 20) {
            said++;
            SAY("saves", "%s %s while its write was queued: waited %lld ms for it", leaf_of(path),
                dir ? "listed" : "asked for", safe_now_ms() - t0);
        }
        (void) said; (void) t0;
    }
}

/* is a write of `path` queued or on the disk right now? Never waits */
int shim_safe_pending_path(const char *path) {
    if (!path || !__atomic_load_n(&g_wq_pending, __ATOMIC_ACQUIRE)) return 0;
    pthread_mutex_lock(&g_wq_lock);
    int busy = busy_with(path, 0);
    pthread_mutex_unlock(&g_wq_lock);
    return busy;
}

/*
 * A reader that does not wait: a stream over the newest content written for
 * `path` - still queued, or on the disk right now - or, with nothing pending,
 * the file itself (fopen "r"). The port's readers of its own queued files
 * (sticks.c) read through this, so a second press in Tweaks right after the
 * first neither waits for the disk nor reads the file before the first press.
 */
typedef struct { uint8_t *d; size_t len, at; } memr_t;

static int memr_read(void *cookie, char *buf, int n) {
    memr_t *m = (memr_t *) cookie;
    if (n <= 0 || m->at >= m->len) return 0;
    size_t k = m->len - m->at < (size_t) n ? m->len - m->at : (size_t) n;
    memcpy(buf, m->d + m->at, k);
    m->at += k;
    return (int) k;
}

static fpos_t memr_seek(void *cookie, fpos_t off, int whence) {
    memr_t *m = (memr_t *) cookie;
    long long base = whence == SEEK_SET ? 0 : whence == SEEK_CUR ? (long long) m->at
                   : whence == SEEK_END ? (long long) m->len : -1;
    if (base < 0 || base + (long long) off < 0) { errno = EINVAL; return (fpos_t) -1; }
    m->at = (size_t) (base + (long long) off);
    return (fpos_t) m->at;
}

static int memr_close(void *cookie) {
    memr_t *m = (memr_t *) cookie;
    free(m->d);
    free(m);
    return 0;
}

FILE *shim_safe_fopen_read(const char *path) {
    if (!path) { errno = EINVAL; return NULL; }
    uint8_t *copy = NULL;
    size_t len = 0;
    int have = 0;
    if (__atomic_load_n(&g_wq_pending, __ATOMIC_ACQUIRE)) {
        pthread_mutex_lock(&g_wq_lock);
        const job_t *newest = NULL;
        if (g_wq_busy && !strcmp(g_wq_busy->path, path)) newest = g_wq_busy;
        for (const job_t *j = g_wq_head; j; j = j->next) if (!strcmp(j->path, path)) newest = j;   /* queued: newer */
        if (newest) {
            copy = (uint8_t *) malloc(newest->len ? newest->len : 1);
            if (copy) { memcpy(copy, newest->data, newest->len); len = newest->len; have = 1; }
        }
        pthread_mutex_unlock(&g_wq_lock);
        if (newest && !have) { shim_safe_wait_path(path); }       /* no memory for a copy: wait for the disk */
    }
    if (!have) return fopen(path, "r");
#ifdef DESK_TEST
    FILE *f = tmpfile();
    if (f) { if (len) fwrite(copy, 1, len, f); rewind(f); }
    free(copy);
    return f;
#else
    memr_t *m = (memr_t *) calloc(1, sizeof(*m));
    if (!m) { free(copy); shim_safe_wait_path(path); return fopen(path, "r"); }
    m->d = copy;
    m->len = len;
    FILE *f = funopen(m, memr_read, NULL, memr_seek, memr_close);
    if (!f) { free(copy); free(m); shim_safe_wait_path(path); return fopen(path, "r"); }
    return f;
#endif
}

/* the reader's side: returns once no write of `path` is queued or on the disk */
void shim_safe_wait_path(const char *path) { wait_for(path, 0); }

/* the same for every file inside `dir` (a listing of it, a removal of it) */
void shim_safe_wait_dir(const char *dir) { wait_for(dir, 1); }

/*
 * Every queued write on the disk: 1 when it is so, 0 when `timeout_ms` ran
 * out first (a negative timeout waits for as long as it takes).
 */
int shim_safe_flush(int timeout_ms) {
    if (!__atomic_load_n(&g_wq_pending, __ATOMIC_ACQUIRE)) return 1;
    if (on_writer()) return 0;
    struct timespec until;
    if (timeout_ms >= 0) {
        clock_gettime(CLOCK_REALTIME, &until);
        until.tv_sec += timeout_ms / 1000;
        until.tv_nsec += (long) (timeout_ms % 1000) * 1000000L;
        if (until.tv_nsec >= 1000000000L) { until.tv_sec++; until.tv_nsec -= 1000000000L; }
    }
    pthread_mutex_lock(&g_wq_lock);
    while (g_wq_head || g_wq_busy) {
        if (timeout_ms < 0) pthread_cond_wait(&g_wq_done, &g_wq_lock);
        else if (pthread_cond_timedwait(&g_wq_done, &g_wq_lock, &until) == ETIMEDOUT) break;
    }
    int ok = !g_wq_head && !g_wq_busy;
    pthread_mutex_unlock(&g_wq_lock);
    if (!ok) SAY("saves", "the writer's queue was NOT emptied in %d ms (%d still to write)", timeout_ms,
                 __atomic_load_n(&g_wq_pending, __ATOMIC_ACQUIRE));
    return ok;
}

/* how many writes are queued or on the disk: no lock, for the crash handler and the tests */
int shim_safe_pending(void) { return __atomic_load_n(&g_wq_pending, __ATOMIC_ACQUIRE); }

/* the writes queued so far, ever (saves.c: "is options.ini on its way") */
long shim_safe_queued_total(void) {
    pthread_mutex_lock(&g_wq_lock);
    long n = g_wq_queued;
    pthread_mutex_unlock(&g_wq_lock);
    return n;
}

/* the import: from here on a queued write of a save file is dropped, not written over the import */
void shim_safe_seal(void) { g_wq_sealed = 1; }

/*
 * The crash handler's wait (loader.c on_signal): a moment for the writer to
 * finish what the game has already closed. Atomic loads and nanosleep only -
 * a signal handler may do nothing else - and 1.5 s at most.
 */
void shim_safe_crash_wait(void) {
    if (on_writer()) return;
    struct timespec t0, t;
    clock_gettime(CLOCK_MONOTONIC, &t0);            /* async-signal-safe, as nanosleep is */
    while (__atomic_load_n(&g_wq_pending, __ATOMIC_ACQUIRE) > 0) {
        clock_gettime(CLOCK_MONOTONIC, &t);
        if ((t.tv_sec - t0.tv_sec) * 1000L + (t.tv_nsec - t0.tv_nsec) / 1000000L >= 1500) break;
        struct timespec ts = { 0, 10000000L };
        nanosleep(&ts, NULL);
    }
}

/* the counters, for the report and the desk */
void shim_safe_counts(long *queued, long *superseded, long *written, long *dropped) {
    pthread_mutex_lock(&g_wq_lock);
    if (queued) *queued = g_wq_queued;
    if (superseded) *superseded = g_wq_superseded;
    if (written) *written = g_wq_written;
    if (dropped) *dropped = g_wq_dropped;
    pthread_mutex_unlock(&g_wq_lock);
}

/*
 * The game's own exit() (the image imports it; its shutdown writes the
 * settings, 0x8865cc): whatever it queued reaches the disk first - 3 s at
 * most - and then the process ends as it would have.
 */
void shim_exit(int code) {
    shim_safe_flush(3000);
    exit(code);
}

#ifndef DESK_TEST
/*
 * Java's side: onPause, onStop, a restart, an export, an import, a failure.
 * `seal` for the import, after the wait (shim_safe_seal).
 */
JNIEXPORT jboolean JNICALL
Java_ru_isaac_port_MainActivity_savesFlush(JNIEnv *env, jobject thiz, jint ms, jboolean seal) {
    (void) env; (void) thiz;
    int ok = shim_safe_flush((int) ms);
    if (seal) shim_safe_seal();
    return ok ? JNI_TRUE : JNI_FALSE;
}
#endif
