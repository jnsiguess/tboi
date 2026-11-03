/*
 * The game's resources, straight out of the APK, with no unpacking.
 *
 * The .pak chunks sit in the assets uncompressed, so AAsset_getBuffer hands
 * them over as a memory mapping: no copy, no reading from disk up front. We
 * parse each chunk's pointer and keep a "path -> address and length" table.
 * From there our six file-call replacements answer out of that table.
 *
 * That way exactly one copy of the data lives on the phone — the APK itself.
 * Unpacking would copy the same two and a half gigabytes onto the same disk
 * and give nothing back: there is no compression in the APK, and the reads
 * would still come off the same storage.
 *
 * What is NOT from here: saves and settings. Their paths are not in the
 * chunks, the table lookup misses, and the call goes on to the real file
 * system as before. The split happens by itself: resources are only read, our
 * own files are written.
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <fcntl.h>
#include <dirent.h>
#include <sys/syscall.h>
#include <android/log.h>
#include <android/asset_manager.h>
#include <jni.h>
#include <pthread.h>
#include "diag.h"

#if ISAAC_DIAG
#define LOG(...) __android_log_print(ANDROID_LOG_INFO, "isaac", __VA_ARGS__)
#else
#define LOG(...) do { } while (0)      /* the build we play on has no log */
#endif

/*
 * Failures the game cannot drive on after are always written down: to the log
 * and to Downloads/isaac-start.txt. Resources that did not map are
 * exactly the case where all anyone sees from the outside is "it closes
 * immediately".
 */
void report_line(const char *kind, const char *text);
#define FAILURE(...) do { \
    char line_[512]; \
    snprintf(line_, sizeof(line_), __VA_ARGS__); \
    __android_log_print(ANDROID_LOG_ERROR, "isaac", "%s", line_); \
    report_line("FAILURE", line_); \
} while (0)

typedef struct entry {
    const uint8_t *data;
    long length;
    struct entry *next;                /* chain within a bucket */
    long asked;                        /* how often the game has READ it */
    char port;                         /* laid by the port (shim_pack_add), not out of the pack's chunks */
    unsigned spelling;                 /* hash of the path as the game first wrote it */
    char path[1];                      /* variable-length tail */
} entry_t;

/* Plenty of buckets: about thirty-five thousand files, so chains stay at one or two. */
#define BUCKETS 65536
static entry_t *g_table[BUCKETS];
static int g_files;
static char g_root[512];               /* the path names in the chunks are relative to */
static size_t g_root_len;

/*
 * Names are matched WITHOUT regard to case, because that is what the game was
 * built against: an iOS bundle sits on a case-insensitive filesystem, so the
 * engine may ask for a file in whatever case its data files happen to spell
 * it, and the bundle answers. Our chunk keeps the names as they really are.
 *
 * The difference is not academic. Of the 1151 sound samples `sounds.xml` asks
 * for, 767 are spelled in a case the files do not have - "Voiceover/", "V2/",
 * "Feedback/", "Greed SFX/" and a long tail of capitalised file names - and
 * every one of them used to miss here while opening fine on iOS. Among them
 * are the lines Isaac speaks over a pill ("Voiceover/Telepills 4.wav",
 * "I Found Pills 3.wav", "Power Pill_09.wav") and the coin that pays for an
 * item in the shop ("Feedback/coin slot.wav", "V2/CoinInsert.wav") - which is
 * exactly what was reported as missing.
 *
 * Folding cannot make two files collide: all 34824 names in the chunk stay
 * distinct when case is ignored, measured on the packed blob itself.
 */
static unsigned char fold(unsigned char c) {
    return (c >= 'A' && c <= 'Z') ? (unsigned char) (c + ('a' - 'A')) : c;
}

static unsigned hash(const char *s) {
    unsigned h = 2166136261u;
    for (; *s; s++) h = (h ^ fold((unsigned char) *s)) * 16777619u;
    return h & (BUCKETS - 1);
}

static int same_path(const char *a, const char *b) {
    for (; *a && *b; a++, b++)
        if (fold((unsigned char) *a) != fold((unsigned char) *b)) return 0;
    return *a == *b;
}

/*
 * A file the port supplies itself, put into the same table the game's own
 * resources live in. The item descriptions bring their own font this way
 * (eid.c): the game then opens it exactly as it opens its own, with no special
 * case anywhere in its code.
 */
void put(const char *path, const uint8_t *data, long length);

void put(const char *path, const uint8_t *data, long length) {
    size_t n = strlen(path);
    entry_t *e = (entry_t *) malloc(sizeof(entry_t) + n);
    if (!e) return;
    memcpy(e->path, path, n + 1);
    e->data = data;
    e->length = length;
    e->asked = 0;
    e->port = 0;
    unsigned k = hash(path);
    e->next = g_table[k];
    g_table[k] = e;
    g_files++;
}

static const entry_t *find(const char *path) {
    for (entry_t *e = g_table[hash(path)]; e; e = e->next)
        if (same_path(e->path, path)) return e;
    return NULL;
}

/*
 * Is a file in the table under this exact key? by_path strips the resource
 * root first, which is right for a path the GAME asks for and wrong for one
 * we entered ourselves - so the port's own files are looked up directly.
 */
/*
 * The directory every resource path is rooted at. The port's own files have to
 * be asked for the way the game asks for its own - absolute - because the
 * lookup strips this prefix before it searches.
 */
const char *shim_pack_root(void) { return g_root; }

int shim_pack_has(const char *inner) {
    return inner && find(inner) != NULL;
}

/*
 * A file of the pack by its inner path, for the port's own reading (usermods.c:
 * the base of a merge, the game's table a mod's is compared with). Nothing is
 * counted: the account of what the GAME read stays the game's.
 */
const void *shim_pack_peek(const char *inner, long *length) {
    const entry_t *e = inner ? find(inner) : NULL;
    if (!e) return NULL;
    if (length) *length = e->length;
    return e->data;
}

/*
 * The "ask" line (usermods.c, plan 30.09 M11): each call that comes here names
 * itself, with the game's return address - the address that called the file
 * call that called us. Only the report build has it.
 */
#if ISAAC_DIAG
extern volatile int g_usermods_ask_armed;
void shim_usermods_ask_seen(const char *kind, const char *mapped, unsigned long from);
#define ASK_SEEN(kind, path) do { if (g_usermods_ask_armed) \
    shim_usermods_ask_seen(kind, path, (unsigned long) __builtin_return_address(1)); } while (0)
static void packuse_start(void);
#else
#define ASK_SEEN(kind, path) do { } while (0)
#endif

void shim_pack_add(const char *path, const void *data, long length) {
    void put(const char *, const uint8_t *, long);
    if (!path || !data || length <= 0) return;
    put(path, (const uint8_t *) data, length);
    /* the new entry heads its chain: the port's, for the account of reads (packuse) */
    entry_t *e = g_table[hash(path)];
    if (e && same_path(e->path, path)) e->port = 1;
}

/*
 * THE PORT'S OWN FILES, FOR THE EXPORT (plan 30.09, M12). Every asset the port
 * enters into this table itself (eid.c bring_in, the one road for every file
 * of the port's - its fonts, the built-in mods' pictures, the Tweaks and
 * Spawner pages, the credits) is entered under three shapes of one path;
 * here it is kept once, by its canonical one, "<folder>/<leaf>" - the path a
 * player's mod replaces it by (resources/<folder>/<leaf>, usermods.c). The
 * export of the game's files (GameFiles.java) asks for the list, so it carries
 * exactly what the port brought in this run - nothing hand-listed that can go
 * stale (GameFiles.BUILT_IN was that list).
 */
#define PORT_FILES_MAX 1024
typedef struct { char asset[160]; char path[200]; } portfile_t;
static portfile_t g_portfile[PORT_FILES_MAX];
static int g_nportfile;
static pthread_mutex_t g_portfile_lock = PTHREAD_MUTEX_INITIALIZER;

void shim_pack_port_file(const char *asset, const char *path) {
    if (!asset || !path || strlen(asset) >= sizeof(g_portfile[0].asset) || strlen(path) >= sizeof(g_portfile[0].path)) return;
    pthread_mutex_lock(&g_portfile_lock);
    int i = 0;
    while (i < g_nportfile && !same_path(g_portfile[i].path, path)) i++;
    if (i < PORT_FILES_MAX) {
        /* a later bring-in of the same path wins in the table, so it does here */
        snprintf(g_portfile[i].asset, sizeof(g_portfile[0].asset), "%s", asset);
        snprintf(g_portfile[i].path, sizeof(g_portfile[0].path), "%s", path);
        if (i == g_nportfile) g_nportfile++;
    }
    pthread_mutex_unlock(&g_portfile_lock);
}

/* GameFiles.portFiles(): {asset, path, asset, path, ...} - what the port brought in this run */
JNIEXPORT jobjectArray JNICALL Java_ru_isaac_port_GameFiles_portFiles(JNIEnv *env, jclass cls) {
    (void) cls;
    jclass str = (*env)->FindClass(env, "java/lang/String");
    if (!str) return NULL;
    pthread_mutex_lock(&g_portfile_lock);
    int n = g_nportfile;
    jobjectArray out = (*env)->NewObjectArray(env, 2 * n, str, NULL);
    for (int i = 0; out && i < n; i++) {
        jstring a = (*env)->NewStringUTF(env, g_portfile[i].asset), p = (*env)->NewStringUTF(env, g_portfile[i].path);
        if (!a || !p) break;
        (*env)->SetObjectArrayElement(env, out, 2 * i, a);
        (*env)->SetObjectArrayElement(env, out, 2 * i + 1, p);
        (*env)->DeleteLocalRef(env, a);
        (*env)->DeleteLocalRef(env, p);
    }
    pthread_mutex_unlock(&g_portfile_lock);
    return out;
}

/* wide integers in a chunk are in network order — that is how the packer writes them */
static uint32_t get32(const uint8_t *p) {
    return ((uint32_t) p[0] << 24) | ((uint32_t) p[1] << 16) | ((uint32_t) p[2] << 8) | p[3];
}

static uint64_t get64(const uint8_t *p) {
    return ((uint64_t) get32(p) << 32) | get32(p + 4);
}

/*
 * Parsing one chunk. The data is not copied: the table gets pointers into the
 * mapped asset, which lives until we stop.
 */
static void parse_chunk(const char *name, const uint8_t *b, long length) {
    /*
     * Chunks come in two flavours. ISAACPK1 is "path, length, bytes" in a row.
     * ISAACPK2 adds duplicates: half of the game's resources appear three or
     * four times across the DLC sets, byte for byte (measured: 9244 files,
     * 1214 MB, 48.7% of the weight). A duplicate stores -1 as its length,
     * followed by the offset and length of the bytes already written. Data is
     * copied in neither case — the table gets a pointer, so two paths happily
     * look at the same place: resources are only ever read.
     */
    int second = (length >= 12 && !memcmp(b, "ISAACPK2", 8));
    if (length < 12 || (!second && memcmp(b, "ISAACPK1", 8))) {
        LOG("resources: %s — foreign chunk", name);
        return;
    }
    long count = (long) (int32_t) get32(b + 8);
    long p = 12;
    for (long i = 0; i < count; i++) {
        if (p + 4 > length) break;
        long name_len = (long) (int32_t) get32(b + p);
        p += 4;
        if (name_len <= 0 || p + name_len + 8 > length) break;
        char path[1024];
        long m = name_len < (long) sizeof(path) - 1 ? name_len : (long) sizeof(path) - 1;
        memcpy(path, b + p, (size_t) m);
        path[m] = 0;
        p += name_len;
        long size = (long) get64(b + p);
        p += 8;
        if (second && size == -1) {                  /* duplicate: point at someone else's bytes */
            if (p + 16 > length) break;
            long offset = (long) get64(b + p); p += 8;
            long len = (long) get64(b + p); p += 8;
            if (offset < 0 || len < 0 || offset + len > length) break;
            put(path, b + offset, len);
            continue;
        }
        if (size < 0 || p + size > length) break;
        put(path, b + p, size);
        p += size;
    }
}

/*
 * Build the table. Called once, before the game starts opening files. We
 * enumerate the assets ourselves: chunk names match the resource-set names and
 * change together with them.
 */
void shim_pack_open(void *manager, const char *root) {
    AAssetManager *mgr = (AAssetManager *) manager;
    if (!mgr || !root) return;
    snprintf(g_root, sizeof(g_root), "%s/", root);
    g_root_len = strlen(g_root);

    AAssetDir *dir = AAssetManager_openDir(mgr, "");
    if (!dir) { FAILURE("resources: cannot enumerate the assets"); return; }
    const char *name;
    long total = 0;
    while ((name = AAssetDir_getNextFileName(dir))) {
        size_t n = strlen(name);
        if (n < 4 || strcmp(name + n - 4, ".pak")) continue;
        AAsset *asset = AAssetManager_open(mgr, name, AASSET_MODE_BUFFER);
        if (!asset) { FAILURE("resources: %s did not open", name); continue; }
        /*
         * The chunk is uncompressed, so getBuffer returns a mapping rather
         * than a copy. We deliberately never close the asset: closing would
         * drop the mapping, and pointers into it live in the table and are
         * needed until we stop.
         */
        const uint8_t *b = (const uint8_t *) AAsset_getBuffer(asset);
        long length = (long) AAsset_getLength64(asset);
        if (!b) { FAILURE("resources: %s did not map (%ld bytes)", name,
                 (long) AAsset_getLength64(asset)); AAsset_close(asset); continue; }
        parse_chunk(name, b, length);
        total += length;
    }
    AAssetDir_close(dir);
    LOG("resources: %d files taken from the APK, %ld MB, root %s",
        g_files, total >> 20, g_root);
}

/*
 * A path from the game is absolute, inside the app's directory. Names in a
 * chunk are relative to the resource root, so we strip the prefix. Foreign
 * paths (saves, the port's own settings) do not belong to the root and never
 * get here.
 */
static size_t inner_path(const char *path, char *out, size_t capacity);

static const entry_t *by_path(const char *path) {
    if (!g_files || !path) return NULL;
    char inside[1024];
    if (inner_path(path, inside, sizeof(inside)) == (size_t) -1) return NULL;
    return find(inside);
}

/*
 * What the pack was asked for, counted for the tester's report.
 *
 * The stall lines said thousands of minor faults a frame and not one major
 * one - fresh memory being touched, nothing read from flash. The copy in
 * shim_pack_open_fd is the first thing in this layer that could do that at
 * that scale, so it is counted rather than argued about: how many files were
 * served each way and how many bytes were copied.
 */
long g_pack_fopen, g_pack_fd, g_pack_data, g_pack_copied, g_pack_again;
#define AGAIN_KEPT 3
char g_again_name[AGAIN_KEPT][64];
int g_again_at;

/*
 * The spelling as the GAME wrote it, hashed.
 *
 * The pack finds a file whatever the case, and that is what makes the port
 * work at all - but the game's own resource cache is a table of strings, and
 * two spellings of one file are two keys in it. If that is what is happening,
 * every doorway re-reads art the game already holds, and the hash is what
 * catches it: the first spelling is kept, and a later one that differs says so
 * in the report.
 */
static unsigned spelling_of(const char *s) {
    unsigned h = 2166136261u;
    for (; *s; s++) { h ^= (unsigned char) *s; h *= 16777619u; }
    return h ? h : 1u;
}

long g_pack_checks;                    /* existence checks, which read nothing */
unsigned long g_pack_asked_from, g_pack_asked_from2;   /* who asked, two levels (shim.c) */

const void *shim_pack_data(const char *path, long *length) {
    g_pack_data++;
    const entry_t *e = by_path(path);
    if (!e) return NULL;
#if ISAAC_DIAG
    packuse_start();
#endif
    {
        /* the music watch (audio.c): which music and video files are opened */
        int shim_report_active(void);
        static int told;
        size_t n = strlen(path);
        if (told < 80 && n > 4 && (same_path(path + n - 4, ".ogg") || same_path(path + n - 4, ".ogv"))
            && shim_report_active()) {
            told++;
            char line[200];
            const char *slash = strrchr(path, '/');
            snprintf(line, sizeof(line), "opened %s", slash ? slash + 1 : path);
            report_line("music", line);
        }
    }
    {
        unsigned now = spelling_of(path);
        entry_t *w = (entry_t *) e;
        if (!w->spelling) w->spelling = now;
        else if (w->spelling != now) {
            w->spelling = now;
            int shim_report_active(void);
            void report_line(const char *kind, const char *text);
            static int said;
            if (shim_report_active() && said < 12) {
                said++;
                char line[300];
                snprintf(line, sizeof(line), "spelled another way now: \"%s\" (the pack holds \"%s\")",
                         path, e->path);
                report_line("pack", line);
            }
        }
    }
    /*
     * A file asked for twice means the game did not keep what it made of it
     * the first time. On iOS the animations and sheets of a room are held in a
     * cache, and walking back into a room reads nothing at all; if that cache
     * is not working here, every doorway pays the full price of parsing again,
     * which is exactly the shape of the stalls. Counted, not assumed.
     */
    if (((entry_t *) e)->asked++) {
        g_pack_again++;
        /*
         * A file read a SECOND time, and the code that asked for it.
         *
         * Measured: every doorway reads eleven to twenty-four files the game
         * already read - doors, the backdrop, the floor's music - and uploads
         * their art again. Whether that is a cache with too small a budget or
         * simply art nobody kept, the answer starts with the caller: the
         * anm2 loader, the spritesheet loader and the sound loader are
         * different subsystems with different caches, and this says which one
         * came back.
         */
        {
            int shim_report_active(void);
            void report_line(const char *kind, const char *text);
            unsigned long shim_image_base(void);
            int shim_sticks_in_game(void);
            static int said;
            /*
             * Only inside a run. The first sixteen of these were eaten by the
             * fonts of the start-up screens, and the question is about
             * doorways - so the menus keep quiet.
             */
            if (shim_report_active() && said < 16 && shim_sticks_in_game()) {
                said++;
                unsigned long b = shim_image_base();
                unsigned long f1 = g_pack_asked_from, f2 = g_pack_asked_from2;
                const char *slash2 = strrchr(path, '/');
                char line[220];
                snprintf(line, sizeof(line), "read again: %s, asked from %lx, by %lx",
                         slash2 ? slash2 + 1 : path,
                         (b && f1 > b && f1 - b < 0xb20000) ? f1 - b : 0,
                         (b && f2 > b && f2 - b < 0xb20000) ? f2 - b : 0);
                report_line("pack", line);
            }
        }
        /*
         * The path AS THE GAME WROTE IT, not as the pack stores it. The pack
         * matches without regard to case, so it finds a file however the game
         * spells it; the game's own cache compares strings, and would miss.
         * If the spelling differs between two asks for the same file, that is
         * the miss - and it will be visible right here.
         */
        const char *slash = strrchr(path, '/');
        snprintf(g_again_name[g_again_at], sizeof(g_again_name[0]), "%s", slash ? slash + 1 : path);
        g_again_at = (g_again_at + 1) % AGAIN_KEPT;
    }
    if (length) *length = e->length;
    return e->data;
}

/* ---------- answers to the file calls ---------- */

/*
 * Reading through FILE. fmemopen gives a real stream over memory, so the
 * game's fread/fseek/fclose work as usual — no need to replace those.
 */
FILE *shim_pack_fopen(const char *path, const char *mode) {
    ASK_SEEN("fopen", path);
    if (!mode || mode[0] != 'r' || strchr(mode, '+')) return NULL;
    long length = 0;
    const void *data = shim_pack_data(path, &length);
    if (!data) return NULL;
    g_pack_fopen++;
    return fmemopen((void *) (uintptr_t) data, (size_t) length, "rb");
}

/*
 * Reading through a bare descriptor. There is no real file on disk, so we make
 * memory behind a descriptor and fill it. Resources are small — seventy-five
 * kilobytes on average — so a copy here is cheaper than juggling offsets
 * inside the APK, and it makes reading past the end of a file impossible.
 */
int shim_pack_open_fd(const char *path, int flags) {
    ASK_SEEN("open", path);
    if ((flags & O_ACCMODE) != O_RDONLY) return -1;
    long length = 0;
    const void *data = shim_pack_data(path, &length);
    if (!data) return -1;
    int fd = (int) syscall(__NR_memfd_create, "resource", 0u);
    if (fd < 0) return -1;
    const uint8_t *p = (const uint8_t *) data;
    long left = length;
    while (left > 0) {
        ssize_t n = write(fd, p, (size_t) left);
        if (n <= 0) { close(fd); return -1; }
        p += n; left -= n;
    }
    lseek(fd, 0, SEEK_SET);
    g_pack_fd++;
    g_pack_copied += length;
    return fd;
}

/*
 * An existence check is not a read, and counting the two together was hiding
 * the answer: "twenty-two asked, twenty-two of them again" reads like the game
 * re-reading its art, when some of those are the game merely asking whether a
 * file is there. They are counted apart now.
 */
int shim_pack_stat(const char *path, void *st) {
    long length = 0;
    ASK_SEEN("stat", path);
    g_pack_checks++;
    const entry_t *e = by_path(path);
    if (!e) return -1;
    length = e->length;
    struct stat *s = (struct stat *) st;
    memset(s, 0, sizeof(*s));
    s->st_mode = S_IFREG | 0444;
    s->st_size = (off_t) length;
    s->st_nlink = 1;
    s->st_blksize = 4096;
    s->st_blocks = (blkcnt_t) ((length + 511) / 512);
    return 0;
}

int shim_pack_exists(const char *path) {
    ASK_SEEN("access", path);
    g_pack_checks++;
    return by_path(path) != NULL;
}

/* ---------- directory listing ---------- */

/*
 * A chunk has no directories of its own, only file paths. So a listing is
 * assembled on the spot: walk the table and take whatever sits directly under
 * the requested directory, collapsing subdirectories to a name. Directories
 * are opened a handful of times per session, so a full walk is cheaper than a
 * second table that would have to be kept around forever.
 */
#define MAGIC 0x9A0FBEEF

/* Apple's directory entry has its own layout — that is what the game expects */
typedef struct {
    uint64_t d_ino;
    uint64_t d_seekoff;
    uint16_t d_reclen;
    uint16_t d_namlen;
    uint8_t  d_type;
    char     d_name[1024];
} darwin_dirent_t;

#define DT_DIR_APPLE 4
#define DT_REG_APPLE 8

typedef struct {
    unsigned magic;
    int count, current;
    char **names;
    unsigned char *is_dir;             /* same order as the names */
    /*
     * The entry readdir hands back lives in its own walk, as POSIX allows: it
     * stays valid until the next readdir or closedir of THIS directory. One
     * static entry for all of them would be shared by every thread (the game
     * has a sound and a video worker besides the frame) and by two directories
     * walked at once (02.10.2026).
     */
    darwin_dirent_t out;
} walk_t;

static void add_name(walk_t *w, const char *name, size_t n, int is_directory) {
    for (int i = 0; i < w->count; i++)
        if (!strncmp(w->names[i], name, n) && w->names[i][n] == 0) return;
    char **names = (char **) realloc(w->names, sizeof(char *) * (size_t) (w->count + 1));
    unsigned char *flags = (unsigned char *) realloc(w->is_dir, (size_t) (w->count + 1));
    if (!names || !flags) { free(names); free(flags); return; }
    w->names = names; w->is_dir = flags;
    char *copy = (char *) malloc(n + 1);
    if (!copy) return;
    memcpy(copy, name, n); copy[n] = 0;
    w->names[w->count] = copy;
    w->is_dir[w->count] = (unsigned char) is_directory;
    w->count++;
}

/*
 * A directory name from the game arrives as-is: with "./" in the middle and a
 * trailing slash. We bring it to the shape the chunk's names have, otherwise
 * the lookup misses. That is exactly what tripped everything up the first
 * time: the engine was opening "<root>/./", and no such string is in the
 * table, of course.
 */
static size_t inner_path(const char *path, char *out, size_t capacity) {
    if (strncmp(path, g_root, g_root_len)) return (size_t) -1;
    const char *p = path + g_root_len;
    size_t n = 0;
    while (*p && n + 1 < capacity) {
        if (p[0] == '.' && (p[1] == '/' || p[1] == 0)) { p += p[1] ? 2 : 1; continue; }
        if (p[0] == '/' && (n == 0 || out[n - 1] == '/')) { p++; continue; }
        out[n++] = *p++;
    }
    while (n && out[n - 1] == '/') n--;
    out[n] = 0;
    return n;
}

/*
 * A listing is glued together from two sources: resources live in the APK and
 * saves on disk, and the app's root holds both. Handing back only one half
 * would be a lie: the engine resolves letter case by listing a directory, and
 * a missing half turns into a file that cannot be found.
 */
void *shim_pack_opendir(const char *path) {
    ASK_SEEN("opendir", path);
    if (!g_files || !path) return NULL;
    char inside[1024];
    size_t len = inner_path(path, inside, sizeof(inside));
    if (len == (size_t) -1) return NULL;

    walk_t *w = (walk_t *) calloc(1, sizeof(walk_t));
    if (!w) return NULL;
    w->magic = MAGIC;
    for (int k = 0; k < BUCKETS; k++)
        for (entry_t *e = g_table[k]; e; e = e->next) {
            if (len) {
                if (strncmp(e->path, inside, len) || e->path[len] != '/') continue;
            }
            const char *tail = e->path + (len ? len + 1 : 0);
            const char *slash = strchr(tail, '/');
            add_name(w, tail, slash ? (size_t) (slash - tail) : strlen(tail), slash != NULL);
        }

    DIR *d = opendir(path);
    if (d) {
        struct dirent *de;
        while ((de = readdir(d))) {
            if (!strcmp(de->d_name, ".") || !strcmp(de->d_name, "..")) continue;
            add_name(w, de->d_name, strlen(de->d_name), de->d_type == DT_DIR);
        }
        closedir(d);
    }
    if (!w->count) { free(w); return NULL; }        /* no such directory anywhere */
    return w;
}

void *shim_pack_readdir(void *walk) {
    walk_t *w = (walk_t *) walk;
    if (!w || w->magic != MAGIC || w->current >= w->count) return NULL;
    darwin_dirent_t *out = &w->out;
    const char *name = w->names[w->current];
    size_t n = strlen(name);
    if (n > sizeof(out->d_name) - 1) n = sizeof(out->d_name) - 1;
    memset(out, 0, sizeof(*out));
    out->d_ino = (uint64_t) (w->current + 1);
    out->d_seekoff = (uint64_t) (w->current + 1);
    out->d_namlen = (uint16_t) n;
    out->d_type = w->is_dir[w->current] ? DT_DIR_APPLE : DT_REG_APPLE;
    memcpy(out->d_name, name, n);
    out->d_reclen = (uint16_t) (21 + n + 1);
    w->current++;
    return out;
}

int shim_pack_is_our_walk(void *walk) {
    walk_t *w = (walk_t *) walk;
    return w && w->magic == MAGIC;
}

int shim_pack_closedir(void *walk) {
    walk_t *w = (walk_t *) walk;
    if (!w || w->magic != MAGIC) return -1;
    for (int i = 0; i < w->count; i++) free(w->names[i]);
    free(w->names);
    free(w->is_dir);
    w->magic = 0;
    free(w);
    return 0;
}

/* ---------- the account of what the game read (report build) ---------- */
#if ISAAC_DIAG
/*
 * WHAT THE GAME READS OUTSIDE REPENTANCE'S OWN SET (plan 30.09, M12 p.4) - and,
 * since M8 put the port's own files at the game's paths (port_patch/), every
 * file the PORT laid in the table and the game read, Repentance's set or not
 * (entry->port), so the export's self-check covers the port's files too. The
 * export of the game's files ranks the editions, and about 640 of the 832
 * files of the older ones are named only in the older engines' code - the
 * Repentance engine never asks for them. Which ones it does ask for is
 * counted here rather than argued: every file the game READ (the `asked`
 * count, not a mere existence check) whose path is not repentance-resources/,
 * kept in files/packuse.txt as the union of every run of a report build, so a
 * file read once in some corner of the game stays in it. A thread of its own
 * writes it every 30 s when something new was read (never the game's thread),
 * and says per edition how much of it was read, a few times a session.
 * GameFiles.java reads the file after an export: what the game read and the
 * export did not carry (export self-check, p.5).
 */
#include <pthread.h>

typedef struct used { struct used *next; char path[1]; } used_t;
#define USED_BUCKETS 8192
static used_t *g_used[USED_BUCKETS];
static int g_used_n;

static int used_add(const char *path) {
    unsigned k = hash(path) & (USED_BUCKETS - 1);
    for (used_t *u = g_used[k]; u; u = u->next) if (same_path(u->path, path)) return 0;
    size_t n = strlen(path);
    used_t *u = (used_t *) malloc(sizeof(used_t) + n);
    if (!u) return 0;
    memcpy(u->path, path, n + 1);
    u->next = g_used[k];
    g_used[k] = u;
    g_used_n++;
    return 1;
}

static void packuse_file(char *out, size_t cap, const char *tail) {
    snprintf(out, cap, "%s../packuse.txt%s", g_root, tail);
}

static void packuse_load(void) {
    char path[600];
    packuse_file(path, sizeof(path), "");
    FILE *f = fopen(path, "r");
    if (!f) return;
    char line[1100];
    while (fgets(line, sizeof(line), f)) {
        line[strcspn(line, "\r\n")] = 0;
        if (line[0] && line[0] != '#') used_add(line);
    }
    fclose(f);
}

static int packuse_write(void) {
    char path[600], tmp[620];
    packuse_file(path, sizeof(path), "");
    packuse_file(tmp, sizeof(tmp), ".tmp");
    FILE *f = fopen(tmp, "w");
    if (!f) return -1;
    int bad = fputs("# files the game read outside repentance-resources, and the port's own it read, every report run "
                    "together (pack.c)\n", f) < 0;
    for (int k = 0; k < USED_BUCKETS && !bad; k++)
        for (used_t *u = g_used[k]; u && !bad; u = u->next)
            if (fprintf(f, "%s\n", u->path) < 0) bad = 1;
    if (fclose(f)) bad = 1;
    if (bad || rename(tmp, path)) { unlink(tmp); return -1; }
    return 0;
}

/* "<edition>/<set>" of a path, or the port's own files */
static void packuse_group(const char *path, char *out, size_t cap) {
    const char *a = strchr(path, '/');
    const char *b = a ? strchr(a + 1, '/') : NULL;
    if (!a || !b || a - path < 10 || strncmp(a - 10, "-resources", 10)) { snprintf(out, cap, "(the port's own)"); return; }
    snprintf(out, cap, "%.*s", (int) (b - path), path);
}

#define GROUPS 64
static void packuse_summary(int tick) {
    static char name[GROUPS][80];
    static long read_n[GROUPS], all_n[GROUPS];
    int groups = 0;
    memset(read_n, 0, sizeof(read_n));
    memset(all_n, 0, sizeof(all_n));
    for (int k = 0; k < BUCKETS; k++)
        for (entry_t *e = g_table[k]; e; e = e->next) {
            char g[80];
            packuse_group(e->path, g, sizeof(g));
            int i = 0;
            while (i < groups && strcmp(name[i], g)) i++;
            if (i == groups) {
                if (groups == GROUPS) continue;
                snprintf(name[groups++], sizeof(name[0]), "%s", g);
            }
            all_n[i]++;
            if (e->asked) read_n[i]++;
        }
    /* one line an edition: each of its sets, read of held */
    char done[GROUPS] = {0};
    for (int i = 0; i < groups; i++) {
        if (done[i]) continue;
        const char *slash = strchr(name[i], '/');
        size_t ed = slash ? (size_t) (slash - name[i]) : strlen(name[i]);
        char line[600];
        int n = snprintf(line, sizeof(line), "at %d min, %.*s:", tick / 2, (int) ed, name[i]);
        for (int j = i; j < groups && n > 0 && n < (int) sizeof(line) - 60; j++) {
            if (done[j] || strncmp(name[j], name[i], ed) || (name[j][ed] && name[j][ed] != '/')) continue;
            done[j] = 1;
            n += snprintf(line + n, sizeof(line) - (size_t) n, " %s read %ld of %ld;",
                          name[j][ed] ? name[j] + ed + 1 : "-", read_n[j], all_n[j]);
        }
        report_line("packuse", line);
    }
    char line[200];
    snprintf(line, sizeof(line), "%d files outside repentance-resources read, all report runs together (files/packuse.txt)",
             g_used_n);
    report_line("packuse", line);
}

static void *packuse_loop(void *arg) {
    (void) arg;
    packuse_load();
    int kept = g_used_n;
    for (int tick = 1; ; tick++) {
        sleep(30);
        int fresh = 0;
        for (int k = 0; k < BUCKETS; k++)
            for (entry_t *e = g_table[k]; e; e = e->next)
                if (e->asked && (e->port || strncmp(e->path, "repentance-resources/", 21))) fresh += used_add(e->path);
        if ((fresh || g_used_n != kept) && !packuse_write()) kept = g_used_n;
        /* at 1, 5, 15, 30 and 60 minutes */
        if (tick == 2 || tick == 10 || tick == 30 || tick == 60 || tick == 120) packuse_summary(tick);
    }
    return NULL;
}

static void packuse_start(void) {
    int shim_report_active(void);
    static volatile int started;
    if (started || !shim_report_active() || __sync_lock_test_and_set(&started, 1)) return;
    pthread_t t;
    pthread_attr_t a;
    pthread_attr_init(&a);
    pthread_attr_setdetachstate(&a, PTHREAD_CREATE_DETACHED);
    if (pthread_create(&t, &a, packuse_loop, NULL)) started = 0;
    pthread_attr_destroy(&a);
}
#endif
