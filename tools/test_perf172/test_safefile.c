/*
 * Desk test of the save writer's queue (hotfix 1.72, perf item 3): safefile.c
 * compiled as it is, with saves.c's hooks stubbed and real threads, files and
 * time underneath (desk/desk_posix.c).
 *
 *   test_safefile unit DIR      every rule, one by one
 *   test_safefile child DIR     writes versions of three files through the queue
 *                               for ever - run.py kills it at random moments
 *   test_safefile check DIR     the start-up recovery, then every file must be
 *                               one whole version; prints the versions found
 *
 *   py -3 tools/test_perf172/run.py
 */
#include "../../port/app/src/main/cpp/safefile.c"

/* ---------------------------------------------------------- saves.c, stubbed */
static pthread_mutex_t g_rec_lock = PTHREAD_MUTEX_INITIALIZER;
static char g_seats_now[64] = "seats-0";
static char g_last_seats[64];
static int g_last_seats_n = -9;
static long g_written_calls;
static int g_frozen_test;

int shim_report_active(void) { return 0; }
void report_line(const char *k, const char *t) { (void) k; (void) t; }
const char *shim_saves_dir(void) { return "/nowhere"; }
unsigned long shim_image_base(void) { return 0; }
void shim_saves_written(const char *path, long bytes, long ms, const char *how, const char *who) {
    shim_saves_written_job(path, bytes, ms, how, who, NULL, -2);
}
void shim_saves_written_job(const char *path, long bytes, long ms, const char *how, const char *who,
                            const char *seats, int seats_n) {
    (void) path; (void) bytes; (void) ms; (void) how; (void) who;
    pthread_mutex_lock(&g_rec_lock);
    g_written_calls++;
    g_last_seats_n = seats_n;
    snprintf(g_last_seats, sizeof(g_last_seats), "%.*s", seats_n > 0 ? seats_n : 0, seats ? seats : "");
    pthread_mutex_unlock(&g_rec_lock);
}
int shim_saves_seats_now(const char *path, char *out, int room) {
    if (!strstr(path, "gamestate")) return -1;
    return snprintf(out, (size_t) room, "%s", g_seats_now);
}
int shim_saves_frozen(const char *path) { return g_frozen_test && strstr(path, "persistentgamedata") != NULL; }

/* ------------------------------------------------------------------ helpers */
static int g_fail;
#define CHECK(c, ...) do { if (!(c)) { g_fail++; printf("  FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

static long long ms_now(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return (long long) t.tv_sec * 1000 + t.tv_nsec / 1000000; }

static char *slurp(const char *path, size_t *n) {
    FILE *f = fopen(path, "rb");
    *n = 0;
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long k = ftell(f);
    rewind(f);
    char *b = malloc((size_t) k + 1);
    *n = fread(b, 1, (size_t) k, f);
    b[*n] = 0;
    fclose(f);
    return b;
}

static int exists(const char *p) { return access(p, F_OK) == 0; }

/* a version: "VER <n> LEN <m> SUM <s>\n" then m bytes; the sum covers them */
static unsigned sum_of(const unsigned char *d, size_t n) { unsigned s = 2166136261u; for (size_t i = 0; i < n; i++) s = (s ^ d[i]) * 16777619u; return s; }

static size_t make_version(char *out, size_t room, long ver, size_t len) {
    char head[96];
    unsigned char *body = (unsigned char *) out + 96;
    if (len + 96 > room) len = room - 96;
    for (size_t i = 0; i < len; i++) body[i] = (unsigned char) ('a' + (ver * 31 + (long) i * 7) % 26);
    int h = snprintf(head, sizeof(head), "VER %ld LEN %zu SUM %08x\n", ver, len, sum_of(body, len));
    memmove(out + h, body, len);
    memcpy(out, head, (size_t) h);
    return (size_t) h + len;
}

/* -1 broken, 0 missing, else the version */
static long read_version(const char *path) {
    size_t n;
    char *b = slurp(path, &n);
    if (!b) return 0;
    long ver = -1;
    size_t len = 0;
    unsigned sum = 0;
    int used = 0;
    if (sscanf(b, "VER %ld LEN %zu SUM %x\n%n", &ver, &len, &sum, &used) == 3 && used > 0 && (size_t) used + len == n
        && sum_of((unsigned char *) b + used, len) == sum) { free(b); return ver; }
    free(b);
    return -1;
}

/* the game's way: fopen "wb" through the writer (shim.c game_write_open), fwrite, fclose */
static int game_write(const char *path, const void *data, size_t n) {
    FILE *f = shim_safe_game_open(path, "wb", "the game");
    if (!f) return -1;
    if (n && fwrite(data, 1, n, f) != n) shim_safe_mark_failed(f);
    return shim_safe_fclose(f);
}

/* ------------------------------------------------------------------- tests */
static void t_memstream(void) {
    printf("memstream cookie\n");
    memw_t m = {0};
    CHECK(memw_write(&m, "hello", 5) == 5, "write");
    CHECK(memw_seek(&m, 0, SEEK_SET) == 0, "seek set");
    CHECK(memw_write(&m, "J", 1) == 1, "overwrite");
    CHECK(memw_seek(&m, 3, SEEK_END) == 8, "seek past end");
    CHECK(memw_write(&m, "!", 1) == 1, "write past end");
    CHECK(m.len == 9 && !memcmp(m.d, "Jello\0\0\0!", 9), "content after seek past end");
    CHECK(memw_seek(&m, -2, SEEK_CUR) == 7, "seek cur");
    CHECK(memw_seek(&m, -100, SEEK_CUR) == (fpos_t) -1, "seek before 0 refused");
    char big[100000];
    memset(big, 'x', sizeof(big));
    memw_seek(&m, 0, SEEK_END);
    CHECK(memw_write(&m, big, (int) sizeof(big)) == (int) sizeof(big), "grow");
    CHECK(m.len == 9 + sizeof(big) && m.d[9] == 'x' && m.d[m.len - 1] == 'x', "grown content");
    free(m.d);
}

static void t_basic(const char *dir) {
    printf("basic: stdio into memory, written by the thread\n");
    char p[600];
    snprintf(p, sizeof(p), "%s/rep_gamestate1.dat", dir);
    unlink(p);
    snprintf(g_seats_now, sizeof(g_seats_now), "seats-A");
    long long t0 = ms_now();
    g_desk_fsync_ms = 150;
    FILE *f = shim_safe_game_open(p, "w", "the game");
    CHECK(f != NULL, "open");
    fprintf(f, "Version=%d\r\n", 2);
    fwrite("abc", 1, 3, f);
    fseek(f, 0, SEEK_SET);
    fwrite("W", 1, 1, f);
    fseek(f, 0, SEEK_END);
    fputs("|end", f);
    int r = shim_safe_fclose(f);
    long long took = ms_now() - t0;
    snprintf(g_seats_now, sizeof(g_seats_now), "seats-B");        /* the players change right after the write */
    CHECK(r == 0, "fclose answers 0");
    CHECK(took < 50, "fclose returned at once (%lld ms, the disk takes 150+)", took);
    CHECK(shim_safe_pending() == 1, "one write pending (%d)", shim_safe_pending());
    shim_safe_wait_path(p);
    size_t n;
    char *b = slurp(p, &n);
    CHECK(b && n == strlen("Wersion=2\r\nabc|end") && !memcmp(b, "Wersion=2\r\nabc|end", n), "content [%s]", b ? b : "(none)");
    free(b);
    char t[620];
    snprintf(t, sizeof(t), "%s.tmp", p);
    CHECK(!exists(t), "no .tmp left");
    snprintf(t, sizeof(t), "%s.ready", p);
    CHECK(!exists(t), "no .ready left");
    shim_safe_flush(-1);
    CHECK(g_last_seats_n == 7 && !strcmp(g_last_seats, "seats-A"), "the seats of the write's moment went along (%d [%s])",
          g_last_seats_n, g_last_seats);
    g_desk_fsync_ms = 0;
}

static void t_supersede(const char *dir) {
    printf("supersede: 300 writes of one file, the disk slow - never an older one after a newer\n");
    char p[600];
    snprintf(p, sizeof(p), "%s/rep_persistentgamedata1.dat", dir);
    unlink(p);
    static char buf[300000];
    g_desk_fsync_ms = 20;
    long last_seen = 0;
    int back = 0, broken = 0;
    long q0, s0, w0, d0, q1, s1, w1, d1;
    shim_safe_counts(&q0, &s0, &w0, &d0);
    for (long v = 1; v <= 300; v++) {
        size_t n = make_version(buf, sizeof(buf), v, 1000 + (size_t) (v * 977) % 200000);
        CHECK(game_write(p, buf, n) == 0, "write %ld", v);
        /* a reader in between: whatever is on the disk is whole, and never older than before */
        if (v % 7 == 0) {
            long got = read_version(p);
            if (got < 0) broken++;
            else if (got && got < last_seen) back++;
            if (got > last_seen) last_seen = got;
        }
    }
    shim_safe_flush(-1);
    shim_safe_counts(&q1, &s1, &w1, &d1);
    long fin = read_version(p);
    CHECK(fin == 300, "the last write is on the disk (%ld)", fin);
    CHECK(!back, "%d reads went backwards", back);
    CHECK(!broken, "%d reads saw a broken file", broken);
    CHECK(s1 - s0 > 0, "older queued writes were replaced (%ld)", s1 - s0);
    printf("  300 writes: %ld queued, %ld replaced while waiting, %ld written\n", q1 - q0, s1 - s0, w1 - w0);
    g_desk_fsync_ms = 0;
}

static void t_read_after_write(const char *dir) {
    printf("read after write: a reader gets the queued content, not the old\n");
    char p[600];
    snprintf(p, sizeof(p), "%s/options.ini", dir);
    static char buf[4096];
    size_t n = make_version(buf, sizeof(buf), 1, 100);
    game_write(p, buf, n);
    shim_safe_flush(-1);
    g_desk_fsync_ms = 300;
    n = make_version(buf, sizeof(buf), 2, 120);
    game_write(p, buf, n);
    long long t0 = ms_now();
    shim_safe_wait_path(p);                 /* what shim_fopen / access / NSFileManager do first */
    long got = read_version(p);
    long long waited = ms_now() - t0;
    CHECK(got == 2, "the reader saw version %ld", got);
    CHECK(waited >= 200, "it waited for the write (%lld ms)", waited);
    /* a sync write of the port after a queued game write of the same file: after it, never before */
    n = make_version(buf, sizeof(buf), 3, 50);
    game_write(p, buf, n);
    size_t n4 = make_version(buf, sizeof(buf), 4, 70);
    CHECK(shim_safe_write_file(p, buf, n4) == 0, "sync write");
    g_desk_fsync_ms = 0;
    shim_safe_flush(-1);
    got = read_version(p);
    CHECK(got == 4, "the sync write after the queued one wins (%ld)", got);
    /* the directory: a listing waits for every file inside */
    g_desk_fsync_ms = 200;
    char q[600];
    snprintf(q, sizeof(q), "%s/sub", dir);
    mkdir(q, 0700);
    snprintf(q, sizeof(q), "%s/sub/new.dat", dir);
    unlink(q);
    n = make_version(buf, sizeof(buf), 9, 10);
    game_write(q, buf, n);
    char d[600];
    snprintf(d, sizeof(d), "%s/sub", dir);
    shim_safe_wait_dir(d);
    CHECK(exists(q), "a listing after the wait sees the new file");
    g_desk_fsync_ms = 0;
}

typedef struct { int id; const char *dir; } worker_arg;
static long g_thread_last[4][3];

static void *t_worker(void *a) {
    worker_arg *w = (worker_arg *) a;
    static char bufs[4][70000];
    char *buf = bufs[w->id];
    for (long v = 1; v <= 120; v++) {
        for (int k = 0; k < 3; k++) {
            char p[600];
            snprintf(p, sizeof(p), "%s/t%d_f%d.dat", w->dir, w->id, k);
            size_t n = make_version(buf, sizeof(bufs[0]), v, 500 + (size_t) ((v * 131 + k * 17) % 60000));
            game_write(p, buf, n);
            g_thread_last[w->id][k] = v;
        }
        /* and a file all four write */
        char s[600];
        snprintf(s, sizeof(s), "%s/shared.dat", w->dir);
        size_t n = make_version(buf, sizeof(bufs[0]), w->id * 1000 + v, 300);
        game_write(s, buf, n);
    }
    return NULL;
}

static void t_threads(const char *dir) {
    printf("four threads, 13 files, 1920 writes at once\n");
    g_desk_fsync_ms = 2;
    pthread_t t[4];
    worker_arg a[4];
    for (int i = 0; i < 4; i++) { a[i].id = i; a[i].dir = dir; pthread_create(&t[i], NULL, t_worker, &a[i]); }
    for (int i = 0; i < 4; i++) pthread_join(t[i], NULL);
    shim_safe_flush(-1);
    for (int i = 0; i < 4; i++)
        for (int k = 0; k < 3; k++) {
            char p[600];
            snprintf(p, sizeof(p), "%s/t%d_f%d.dat", dir, i, k);
            long got = read_version(p);
            CHECK(got == 120, "thread %d file %d ends at version %ld", i, k, got);
        }
    char s[600];
    snprintf(s, sizeof(s), "%s/shared.dat", dir);
    long got = read_version(s);
    CHECK(got > 0 && got % 1000 == 120, "the shared file is whole and a last write (%ld)", got);
    CHECK(shim_safe_pending() == 0, "nothing pending");
    g_desk_fsync_ms = 0;
}

static void t_ready(const char *dir) {
    printf(".ready fallback: rename refused - written in place behind the mark\n");
    char p[600];
    snprintf(p, sizeof(p), "%s/inputconfigs.dat", dir);
    static char buf[50000];
    size_t n = make_version(buf, sizeof(buf), 1, 40000);
    game_write(p, buf, n);
    shim_safe_flush(-1);
    snprintf(g_desk_rename_refuse, sizeof(g_desk_rename_refuse), "inputconfigs.dat");
    n = make_version(buf, sizeof(buf), 2, 30000);
    game_write(p, buf, n);
    shim_safe_flush(-1);
    long got = read_version(p);
    CHECK(got == 2, "in place: version %ld", got);
    char t[620];
    snprintf(t, sizeof(t), "%s.tmp", p);
    CHECK(!exists(t), "the .tmp went");
    snprintf(t, sizeof(t), "%s.ready", p);
    CHECK(!exists(t), "the .ready went");
    CHECK(inplace_wanted(p), "the file is written in place from now on");
    n = make_version(buf, sizeof(buf), 3, 10);
    game_write(p, buf, n);
    shim_safe_flush(-1);
    CHECK(read_version(p) == 3, "and the next write in place too");
    g_desk_rename_refuse[0] = 0;

    printf("start-up recovery: .tmp+.ready, .tmp alone, .ready alone\n");
    char rd[600];
    snprintf(rd, sizeof(rd), "%s/rec", dir);
    mkdir(rd, 0700);
    char a[700], b[700];
    /* a whole copy behind its mark: put in place */
    snprintf(a, sizeof(a), "%s/one.dat", rd);
    n = make_version(buf, sizeof(buf), 1, 100);
    FILE *f = fopen(a, "wb"); fwrite(buf, 1, n, f); fclose(f);
    snprintf(b, sizeof(b), "%s/one.dat.tmp", rd);
    n = make_version(buf, sizeof(buf), 2, 200);
    f = fopen(b, "wb"); fwrite(buf, 1, n, f); fclose(f);
    snprintf(b, sizeof(b), "%s/one.dat.ready", rd);
    f = fopen(b, "wb"); fclose(f);
    /* a half-written .tmp: dropped, the file stands */
    snprintf(a, sizeof(a), "%s/two.dat", rd);
    n = make_version(buf, sizeof(buf), 5, 100);
    f = fopen(a, "wb"); fwrite(buf, 1, n, f); fclose(f);
    snprintf(b, sizeof(b), "%s/two.dat.tmp", rd);
    f = fopen(b, "wb"); fwrite("VER 6 LEN 1000", 1, 14, f); fclose(f);
    /* a lone mark: dropped */
    snprintf(b, sizeof(b), "%s/three.dat.ready", rd);
    f = fopen(b, "wb"); fclose(f);
    shim_safe_recover_dir(rd);
    snprintf(a, sizeof(a), "%s/one.dat", rd);
    CHECK(read_version(a) == 2, "one.dat finished from its copy (%ld)", read_version(a));
    snprintf(a, sizeof(a), "%s/two.dat", rd);
    CHECK(read_version(a) == 5, "two.dat kept (%ld)", read_version(a));
    snprintf(b, sizeof(b), "%s/two.dat.tmp", rd);
    CHECK(!exists(b), "the half copy went");
    snprintf(b, sizeof(b), "%s/three.dat.ready", rd);
    CHECK(!exists(b), "the lone mark went");
}

static void t_flush_seal_crash(const char *dir) {
    printf("flush with a timeout, the import's seal, the crash handler's wait\n");
    char p[600], q[600];
    snprintf(p, sizeof(p), "%s/rep_gamestate2.dat", dir);
    snprintf(q, sizeof(q), "%s/rep_persistentgamedata2.dat", dir);
    static char buf[2000];
    g_desk_fsync_ms = 700;
    size_t n = make_version(buf, sizeof(buf), 1, 100);
    game_write(p, buf, n);
    long long t0 = ms_now();
    int ok = shim_safe_flush(200);
    long long took = ms_now() - t0;
    CHECK(!ok && took >= 150 && took < 600, "flush(200) gave up in time (%d, %lld ms)", ok, took);
    t0 = ms_now();
    ok = shim_safe_flush(-1);
    CHECK(ok && shim_safe_pending() == 0, "flush(-1) waited it out (%lld ms)", ms_now() - t0);
    /* the seal: a save file still queued when the import begins is not written */
    unlink(q);
    n = make_version(buf, sizeof(buf), 7, 100);
    game_write(p, buf, n);                  /* on the disk now, slowly */
    n = make_version(buf, sizeof(buf), 8, 100);
    game_write(q, buf, n);                  /* waiting behind it */
    g_frozen_test = 1;
    long d0, d1;
    shim_safe_counts(NULL, NULL, NULL, &d0);
    shim_safe_seal();
    shim_safe_flush(-1);
    shim_safe_counts(NULL, NULL, NULL, &d1);
    CHECK(!exists(q), "the sealed save was not written");
    CHECK(d1 - d0 == 1, "one write dropped (%ld)", d1 - d0);
    CHECK(read_version(p) == 7, "the run file before it was written (%ld)", read_version(p));
    g_wq_sealed = 0;
    g_frozen_test = 0;
    /* the crash handler's wait: a queue that empties within it */
    g_desk_fsync_ms = 300;
    n = make_version(buf, sizeof(buf), 9, 100);
    game_write(p, buf, n);
    t0 = ms_now();
    shim_safe_crash_wait();
    took = ms_now() - t0;
    CHECK(shim_safe_pending() == 0 && took >= 200 && took < 1500, "crash wait saw it through (%lld ms)", took);
    /* and one that does not: 1.5 s at most */
    g_desk_fsync_ms = 3000;
    game_write(p, buf, n);
    t0 = ms_now();
    shim_safe_crash_wait();
    took = ms_now() - t0;
    CHECK(took >= 1400 && took < 2200, "crash wait gives up after 1.5 s (%lld ms)", took);
    g_desk_fsync_ms = 0;
    shim_safe_flush(-1);
    /* NSData: an empty run file on death */
    shim_safe_game_write(p, "", 0, "the game (NSData)");
    shim_safe_flush(-1);
    size_t len = 99;
    char *b = slurp(p, &len);
    CHECK(b && len == 0, "the run file is empty (%zu)", len);
    free(b);
}


static void t_port_files(const char *dir) {
    printf("the port's own files: port-touch.ini and the run marks queued, mods.txt not\n");
    char p[600], q[600], m[600];
    snprintf(p, sizeof(p), "%s/port-touch.ini", dir);
    snprintf(q, sizeof(q), "%s/port-spawner-runs.txt", dir);
    snprintf(m, sizeof(m), "%s/mods.txt", dir);
    unlink(p); unlink(q); unlink(m);
    FILE *f0 = shim_safe_fopen_read(p);
    CHECK(!f0 && errno == ENOENT, "a missing file reads as missing");
    if (f0) fclose(f0);
    g_desk_fsync_ms = 300;
    /* a Tweaks press: written the way sticks.c writes */
    long long t0 = ms_now();
    FILE *f = shim_safe_fopen(p, "w");
    fprintf(f, "spawnerground=%d\n", 1);
    int r = shim_safe_fclose(f);
    long long took = ms_now() - t0;
    CHECK(r == 0 && took < 50, "port-touch.ini: fclose at once (%lld ms, the disk takes 300+)", took);
    CHECK(shim_safe_pending_path(p), "its write is pending");
    /* the second press right after: the strangers read (keep_strangers) sees the first press, without waiting */
    t0 = ms_now();
    FILE *rd = shim_safe_fopen_read(p);
    char line[64] = "";
    if (rd) { fgets(line, sizeof(line), rd); fclose(rd); }
    took = ms_now() - t0;
    CHECK(!strcmp(line, "spawnerground=1\n") && took < 50, "read while queued: [%s] in %lld ms", line, took);
    f = shim_safe_fopen(p, "w");
    fprintf(f, "spawnerground=%d\n", 0);
    shim_safe_fclose(f);
    rd = shim_safe_fopen_read(p);
    line[0] = 0;
    if (rd) { fgets(line, sizeof(line), rd); fclose(rd); }
    CHECK(!strcmp(line, "spawnerground=0\n"), "the newest queued content wins: [%s]", line);
    /* the run marks: queued as well */
    t0 = ms_now();
    f = shim_safe_fopen(q, "w");
    fprintf(f, "# runs\n1 1234 0\n");
    r = shim_safe_fclose(f);
    CHECK(r == 0 && ms_now() - t0 < 50, "port-spawner-runs.txt: fclose at once");
    /* mods.txt: synchronous, read back at once with a plain fopen (usermods.c) */
    t0 = ms_now();
    f = shim_safe_fopen(m, "wb");          /* "w" on the desk is text mode (CRLF); on Android the same */
    fprintf(f, "a=1\n");
    r = shim_safe_fclose(f);
    took = ms_now() - t0;
    CHECK(r == 0 && took >= 250, "mods.txt stays synchronous (%lld ms)", took);
    size_t n;
    char *b = slurp(m, &n);
    CHECK(b && !strcmp(b, "a=1\n"), "mods.txt reads back at once");
    free(b);
    shim_safe_flush(-1);
    CHECK(!shim_safe_pending_path(p), "nothing pending after the flush");
    b = slurp(p, &n);
    CHECK(b && !strcmp(b, "spawnerground=0\n"), "port-touch.ini on the disk: the last press [%s]", b ? b : "");
    free(b);
    b = slurp(q, &n);
    CHECK(b && strstr(b, "1 1234 0"), "the run marks on the disk");
    free(b);
    rd = shim_safe_fopen_read(p);
    line[0] = 0;
    if (rd) { fgets(line, sizeof(line), rd); fclose(rd); }
    CHECK(!strcmp(line, "spawnerground=0\n"), "nothing pending: read from the disk [%s]", line);
    g_desk_fsync_ms = 0;
}

static int unit(const char *dir) {
    mkdir(dir, 0700);
    t_memstream();
    t_basic(dir);
    t_supersede(dir);
    t_read_after_write(dir);
    t_threads(dir);
    t_ready(dir);
    t_flush_seal_crash(dir);
    t_port_files(dir);
    printf(g_fail ? "FAILED: %d\n" : "all passed\n", g_fail);
    return g_fail ? 1 : 0;
}

/* --------------------------------------------- the killed writer and its check */
static int child(const char *dir, int inplace) {
    static char buf[400000];
    /* "inplace": rename onto k1.dat is refused - that file goes the .ready way under the kills */
    if (inplace) snprintf(g_desk_rename_refuse, sizeof(g_desk_rename_refuse), "k1.dat");
    long ver[3] = {0, 0, 0};
    for (int k = 0; k < 3; k++) {
        char p[600];
        snprintf(p, sizeof(p), "%s/k%d.dat", dir, k);
        long got = read_version(p);
        ver[k] = got > 0 ? got : 0;
    }
    srand((unsigned) ms_now());
    printf("ready\n");
    fflush(stdout);
    for (;;) {
        int k = rand() % 3;
        ver[k]++;
        char p[600];
        snprintf(p, sizeof(p), "%s/k%d.dat", dir, k);
        size_t n = make_version(buf, sizeof(buf), ver[k], 1 + (size_t) (rand() * 13 % 380000));
        game_write(p, buf, n);
        if (rand() % 4 == 0) usleep(1000 * (unsigned) (rand() % 5));
    }
    return 0;
}

static int check(const char *dir) {
    shim_safe_recover_dir(dir);
    int bad = 0;
    for (int k = 0; k < 3; k++) {
        char p[600];
        snprintf(p, sizeof(p), "%s/k%d.dat", dir, k);
        long got = read_version(p);
        if (got < 0) bad++;
        printf("%ld ", got);
    }
    char t[700];
    DIR *d = opendir(dir);
    int left = 0;
    struct dirent *e;
    while (d && (e = readdir(d))) if (strstr(e->d_name, ".tmp") || strstr(e->d_name, ".ready")) left++;
    if (d) closedir(d);
    (void) t;
    printf("%s %d\n", bad ? "BROKEN" : "whole", left);
    return bad ? 1 : 0;
}

int main(int argc, char **argv) {
    setvbuf(stdout, NULL, _IONBF, 0);
    if (argc < 3) { printf("usage: unit|child|check DIR\n"); return 2; }
    if (!strcmp(argv[1], "unit")) return unit(argv[2]);
    if (!strcmp(argv[1], "child")) return child(argv[2], argc > 3 && !strcmp(argv[3], "inplace"));
    if (!strcmp(argv[1], "check")) return check(argv[2]);
    return 2;
}
