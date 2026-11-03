/*
 * USER MODS: THE RESOURCE MODS A PLAYER ADDS HIMSELF (the owner, 23-25.09.2026).
 *
 * What a mod is here: a PC mod's resources and nothing else. The iOS engine
 * has no Lua and no mod API (checked: not one of lua_pcall, RegisterMod,
 * AddCallback in the image), so a mod can change pictures, animations,
 * sounds, music and fonts - files - and no behaviour. The port's own format
 * says so by what it holds:
 *
 *   <mods>/<id>/mod.json                 name, version, author, description
 *   <mods>/<id>/resources/...            the PC mod's own layout: gfx/..., sfx/...
 *   <mods>/<id>/resources-dlc3/...       Repentance's layer, as on the PC
 *   <mods>/<id>/resources-dlc3.<lang>/   a language's own files (dlc3.ru, ...)
 *
 * <mods> is "mods" beside port-touch.ini, where the player's file manager
 * reaches it (PortFilesProvider). Which mods are on, and in which order, is
 * <mods>/mods.txt: a line per mod, "<id> on" or "<id> off", the first line the
 * strongest - when two mods hold the same file, the earlier one's is used.
 * THIS FILE IS ITS ONE WRITER (30.09.2026): the MODS page's changes, the start
 * and the installer (UserMods.java, through listPut below) all come here, and
 * every write goes through the port's safe writer with its failure kept.
 *
 * HOW A FILE IS SWAPPED. The game asks for a resource as
 * "<root>/<edition>-resources/<set>/<path>": repentance-resources/data/gfx/...,
 * rebirth-resources/data.jp/gfx/..., repentance-resources/dlc3.ru/gfx/... The
 * <path> is exactly the path inside a PC mod's resources folder (measured
 * 25.09.2026: the same formats, png anm2 wav ogg xml fnt stb sta, the same tree).
 * Every file call of the game goes through shim_map_path (uikit.c), and it
 * asks here first: the edition and the set are cut off, and if an enabled mod
 * holds <path> the call is sent to the mod's file on disk instead - the
 * strongest mod's, and in a mod its resources-dlc3.<lang>, resources-dlc3,
 * resources.<lang>, resources (THE ORDER, at ufile_t: the PC's, measured). The pack never hears of it; the game's code is not touched.
 * Case is ignored, as the pack ignores it: a mod made on Windows spells as
 * it likes. The base engine's own set (kage-resources: its shaders) is not a
 * mod's to answer.
 *
 * THE BUILT-IN MODS' FILES TOO (the owner, 28.09.2026). The port's own art -
 * the Specialist Dance, the boss bars, the Boss Rush wave bar, the completion
 * widget, the item descriptions' fonts and icons - is entered into the
 * resource table by the port (shim_asset_bring_in) under a plain path, with
 * no edition in front: "gfx/port_specialist/...", "data/gfx/...",
 * "resources/data/gfx/..." - every shape the resource system could ask in.
 * Those are looked up here as well, with "resources/" and "data/" taken off,
 * in the mod's shared folders; so a player's mod replaces them the way it
 * replaces the game's files, at the path the export gives them.
 *
 * The index is built once, at start: the game keeps what it has loaded, so
 * turning a mod on or off takes a restart, and the MODS page says so.
 *
 * A CRASH TIED TO A MOD (plan 30.09, M10). When the game dies right after a
 * thread of it read a mod's file, the file and the mod are written to
 * <mods>/last-crash.txt, in the clean build too; the next start hands that to
 * the MODS page (shim_usermods_crash). Nothing is repaired on a guess: the
 * repair is for the class of file that measurably brought the game down.
 *
 * WHAT CHANGED ON 02.10.2026 (batch of that day, items 1, 2, 5, 7):
 *   - a mods.txt that is there but cannot be read is "not read", never "empty":
 *     nothing is written in its name, the port's own copy of the list inside
 *     the app stands in, and it is read again every few seconds (read_list);
 *   - every write is read back, and the copy inside the app follows it;
 *   - the lines are "on|off <folder>" again, the order the build of 1.666
 *     reads, under the '#' line it skips - a step back does not turn every
 *     mod on;
 *   - a folder marked .remove or .adding is no mod, and the mark goes last;
 *   - an update that broke off puts the old copy back (shim_usermods_setup);
 *   - the files the game is handed from the mods are checked first, the
 *     stacked set of all that are on (THE GUARD below): what the engine's
 *     loader cannot take is answered by the game's file, what is incomplete
 *     is completed from the game's file into content-cache/;
 *   - the crash note keeps the last sixteen mod files of every thread, with
 *     the signal and pc/lr in the image.
 */
#include <ctype.h>
#include <stdarg.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <jni.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#include <dlfcn.h>
#include <zlib.h>
#include <android/log.h>
#if defined(__ANDROID__)
#include <sys/prctl.h>
#endif
#include "diag.h"
#include "content.h"
#include "json.h"

#if ISAAC_DIAG
#define LOG(...) __android_log_print(ANDROID_LOG_INFO, "isaac", __VA_ARGS__)
/* a line of the report build's report, formatted; nothing at all in the clean build */
#define NOTE(...) do { if (shim_report_active()) { char note_[900]; snprintf(note_, sizeof(note_), __VA_ARGS__); \
                       report_line("usermods", note_); } } while (0)
#else
#define LOG(...) do { } while (0)
#define NOTE(...) do { } while (0)
#endif

int shim_report_active(void);
void report_line(const char *kind, const char *text);
const char *shim_pack_root(void);
const void *shim_pack_peek(const char *inner, long *length);
int shim_pack_has(const char *inner);
unsigned long shim_image_base(void);
/*
 * The port's one rule of writing a file that keeps a player's state (shim.c,
 * plan 30.09 P1.1): a copy beside it, checked and synced, then put in place -
 * by rename, or written in place where rename fails. shim_safe_fclose answers
 * 0 when the file stands whole in its place, and -1 with errno set when not.
 */
FILE *shim_safe_fopen(const char *path, const char *mode);
int shim_safe_fclose(FILE *f);

/*
 * THE LIST'S SIZE (hotfix 1.75, the owner: 256 - 64 was the port's own number, the game has none). Each mod costs
 * its umod_t twice (g_mods and list_take's copy, about 4 KB each: 2 MB of zero pages at 256, touched only as far as
 * the list goes) and its owner number in every index entry - owner_t, two bytes: 0..MODS_MAX-1 a mod, OWNER_CONTENT
 * the merged content/ file. content.c (CM_MAX) and tweaks.c (LABELS) follow this number.
 */
#define MODS_MAX      256
#define ID_MAX        64
typedef unsigned short owner_t;             /* a mod's place in g_owner[], or OWNER_CONTENT */
#define BUCKETS       4096

#define TEXT_LANGS    4                     /* a mod's texts in the page's sets: en ru de es (jp kr zh read en) */
#define LAYERS_SIG    700

typedef struct {
    char id[ID_MAX];                        /* the folder's name */
    int on;
    int reach;                              /* 0, or the errno its folder answered with: kept in the list, not indexed */
    int indexed;                            /* its files are in this run's index (on at the start) */
    char name[TEXT_LANGS][96], version[TEXT_LANGS][32], author[TEXT_LANGS][64];
    char description[TEXT_LANGS][512];
    int files;                              /* how many of its files are in the index */
    int content;                            /* it brings data-table files content.c builds from (content/, patch/, whole tables) */
    char crash[200];                        /* the last run closed right after reading this file of it */
    int adapted;                            /* the installer adapted it from the PC: its report lies in its folder */
    int port_dirs;                          /* it has a folder the port reads (resources*, content, patch, options) */
    int data_dirs;                          /* of them content/, patch/ or options/ (read past the index: content.c, layers) */
    int script;                             /* it brings a Lua script (main.lua at its top) - the port runs none */
    int seen;                               /* its files the index met, a stronger mod's over them or not */
    int added;                              /* put in place by ADD MOD in this run: its files wait for a restart */
    /* mod.json (json.c) and the mod's settings (plan 4.10, part 2) */
    int manifest;                           /* 0 none (the PC's layout), 1 read, -1 not read (broken) */
    int refused;                            /* json.c's refusals: the file, or settings turned off */
    char reason[24];                        /* the first refusal's code, for the page */
    char *issues;                           /* its check.txt lines ("level [code]: text"), NULL none */
    modj_t *mj;                             /* a mod with settings: kept, never freed while the game runs */
    int val[MODJ_SETTINGS_MAX];             /* the settings' values now (mod-settings.ini) */
    int start[MODJ_SETTINGS_MAX];           /* and at the start: what this run's index was built with */
    char layers[LAYERS_SIG];                /* the layers in use, "key/folder,..." ("" none): the caches' key */
} umod_t;

typedef struct ufile {
    struct ufile *next;
    char *path;                             /* what the game is handed: the mod's file, or the port's completed copy */
    char *src;                              /* the mod's own file on disk */
    long long size, mtime;                  /* of src, at the index: the guard's cache key */
    long gamelen, fixlen;                   /* the game's file it was checked against (-1 none); the completed copy */
    char *why;                              /* the guard's reason, when it refused or completed the file */
    owner_t owner;                          /* g_owner[] - the mod it came from, for the crash note */
    unsigned char guard;                    /* GUARD_*: what the check made of it */
    unsigned char rank;                     /* RANK_*: its folder's place in the mod's own order (THE ORDER below) */
    signed char kx;                         /* a picture: k times the game's (THE GAME'S FRAMES FITTED); 0 not asked, -1 not whole */
    char key[1];                            /* "set|path" for a language's own, "|path" for all */
} ufile_t;

/*
 * THE ORDER OF A MOD'S FOLDERS AND OF THE MODS (the owner, 04.10.2026: "do it as
 * on the PC"; measured in isaac-ng.exe 1.7.9b, the research notes of
 * 04.10.2026, mods at run time,
 * section 3). On the PC a resource path goes to ModManager first (0x870460 in
 * 1.7.9b, 0x8f5ad0 in Repentance+ 1.9.7): the enabled mods in their order, and
 * in each <mod>/resources-dlc3/<path>, then <mod>/resources/<path>; the first
 * that exists is the answer, an absolute path, and the game's own sources -
 * its language sets among them - are never asked. Only when no mod has the
 * file does the game's lookup (0x963120 -> 0x964aa0) walk its sources from the
 * last mounted: resources-dlc3.<lang>, resources-dlc3, resources.<lang>,
 * resources. So a mod's file beats the game's language copy (a mod's English
 * controls.png is what a Russian player sees on the PC too), and a stronger
 * mod's file beats every folder of a weaker one.
 *
 * A PC mod has no language folders - the PC never reads them. The port's
 * format has them (the export gives resources-dlc3.<lang>/, a modder edits
 * the language's picture there); they take the place the PC's own order gives
 * a language set, inside the mod: resources-dlc3.<lang>, resources-dlc3,
 * resources.<lang>, resources - and the mods' order stands over all of it.
 */
enum { RANK_DATA, RANK_DATA_LANG, RANK_DLC3, RANK_DLC3_LANG };

/* what the guard made of a file (THE GUARD below) */
enum { GUARD_NEW, GUARD_OK, GUARD_FIXED, GUARD_REFUSED, GUARD_PNG_LATER };

#define OWNER_CONTENT 0xFFFF                /* the merged content/ file, of every mod that brings one (past MODS_MAX) */

static char g_dir[512];                     /* <config>/mods */
static umod_t g_mods[MODS_MAX];
static int g_count;
static ufile_t *g_index[BUCKETS];
static int g_indexed;
static long g_swapped;                      /* how many calls were sent to a mod's file */
/* a restart is owed (THE RESTART OWED, hotfix 1.74): worked out from what this run indexed - and, since 1.75, owed by
   ADD MOD itself from the mod it puts in until the restart (ADD MOD OWES ITS RESTART) */
static int g_setup_done;                    /* shim_usermods_setup has built this run's index (g_owner) */
static volatile int g_owed_list;            /* the list in memory would index other mods, or in another order */
static volatile int g_owed_disk;            /* so would the list the next start reads from the disk */
static volatile int g_owed_add;             /* ADD MOD put a mod into the list in this process (list_put) */
static volatile int g_installing;           /* ADD MOD is at work in this process (UserMods.installAll) */
static volatile int g_disk_busy;            /* a look at the disk is under way (disk_due) */
static long long g_disk_at;                 /* when the last one started; 0 none since the start */
static __thread int t_quiet;                /* the disk's look at the next start: no report lines for what it passes */
static void owed_update(void);
static volatile int g_list_gen;             /* grows when the list changes off the page (ADD MOD): the page lays out again */
static int g_list_dirty;                    /* what was read is not what mods.txt says: write it at the start */
static char g_save_error[200];              /* why the last write of mods.txt failed, "" when it did not */
static char g_owner[MODS_MAX][ID_MAX];      /* the indexed mods' ids, fixed for the run (a signal reads them) */
static int g_owners;
static int g_content_files;                 /* the data-table files content.c took from the mods of this run */
static pthread_mutex_t g_list_lock = PTHREAD_MUTEX_INITIALIZER;   /* the page (frame thread) and the installer (Java) */

/*
 * A LIST THAT COULD NOT BE READ (plan 02.10, A1). mods.txt lies in shared
 * storage, which on some phones fails for a moment; in 1.666 such a moment
 * turned into "every mod on", written at once and for good. Now a failed
 * read is "not read": the port's own copy of the list (inside the app, out of
 * the media store's reach) stands in, or - with no copy - the folders join
 * off; nothing is written in the file's name, and it is read again every
 * RETRY_MS while the game runs. What the player changes himself is written,
 * whole, as before.
 */
#define RETRY_MS 5000
static char g_inner[512];                   /* <files>/state: the copy of mods.txt inside the app (UserMods.innerDir) */
static int g_list_unread;                   /* mods.txt is there and could not be read */
static int g_list_errno;                    /* what the read answered */
static long long g_unread_at, g_retry_at;   /* when it was found unreadable; the next try (read or save) */
static int g_touched;                       /* the player changed the list in this run */
enum { LIST_FILE, LIST_INNER, LIST_NONE };
static int g_list_from;                     /* where this run's list came from */

static int remove_tree(const char *path, int depth);
static void reading_note(const char *p, const ufile_t *hit);
static void crash_read(void);
static long long now_ms(void);
#define REMOVE_MARK ".remove"               /* a mod deleted on the page: its folder goes at the next start */
#define ADDING_MARK ".adding"               /* a copy the installer is still putting in place: no mod yet */

#if ISAAC_DIAG
static void ask_note(const char *p, const ufile_t *hit);
static void anyset_add(const char *key);
static void legacy_note(const char *leaf, const ufile_t *hit, const char *inner);
static void dance_note(const char *p);
static void media_selftest(void);
static long long g_t0;                      /* when the mods were set up, for the dance lines */
#endif

/* ---- a growing text */
typedef struct { char *s; size_t n, cap; } sb_t;

static void sb_put(sb_t *b, const char *p, size_t n) {
    if (b->n + n + 1 > b->cap) {
        size_t cap = b->cap ? b->cap * 2 : 1024;
        while (cap < b->n + n + 1) cap *= 2;
        char *s = (char *) realloc(b->s, cap);
        if (!s) return;
        b->s = s;
        b->cap = cap;
    }
    memcpy(b->s + b->n, p, n);
    b->n += n;
    b->s[b->n] = 0;
}
static void sb_puts(sb_t *b, const char *s) { sb_put(b, s, strlen(s)); }
static void sb_printf(sb_t *b, const char *fmt, ...) {
    char tmp[1024];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(tmp, sizeof(tmp), fmt, ap);
    va_end(ap);
    if (n > 0) sb_put(b, tmp, (size_t) n < sizeof(tmp) ? (size_t) n : sizeof(tmp) - 1);
}

static long long now_ms(void) {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (long long) t.tv_sec * 1000 + t.tv_nsec / 1000000;
}

/* ------------------------------------------------------------ the list */

static void lower(char *s) { for (; *s; s++) *s = (char) tolower((unsigned char) *s); }

static unsigned hash_of(const char *s) {
    unsigned h = 2166136261u;
    for (; *s; s++) { h ^= (unsigned char) tolower((unsigned char) *s); h *= 16777619u; }
    return h;
}

/*
 * mod.json (plan 30.09, 4.10 part 1; json.c). Read whole and strictly: the
 * mod's name, version, author and description, each a text or {language:
 * text}, and its settings. Shown in the menu's language, else English, else
 * the first the mod has; the menu font has no CJK, so the Japanese, Korean and
 * Chinese menus read English, as they do the mods' notes. What json.c says of
 * the file goes to <id>.check.txt ("level [code]: text") and the report; the
 * page tells the player a refusal in his own words. A mod with no mod.json is
 * the PC's layout (M4): named by its folder, as before.
 */
static const char *const TEXT_LANG[TEXT_LANGS] = { "en", "ru", "de", "es" };
static int g_text_lang;                     /* the page's: TEXT_LANG index */

/* the issues of a mod's mod.json, and the port's own notes on it, as check.txt lines */
static void issue_line(sb_t *b, const char *level, const char *code, const char *text) {
    sb_printf(b, "%s [%s]: %s\n", level, code, text);
}

/*
 * WHAT THE PAGE SHOWS OF A MOD'S NAME AND DESCRIPTION (the owner, 05.10.2026).
 *
 * A PC modder puts his mod where he wants it in the PC's list by the first
 * characters of its name: "!!TBOI: Reflashed", "!!~External item descriptions".
 * The port has an order of its own (MODS), so on the page that is noise: the
 * leading sort marks go, and then a leading "TBOI" tag when a real name follows
 * it ("!!TBOI: Reflashed" -> "Reflashed"). Only what is shown: mod.json, the
 * folders and the converter's output are left as they are.
 *
 * A Steam description is written in Steam's BBCode: [h1], [b], [url=...]...
 * [/url], [list][*]... The page shows text, so the tags go and the text stays;
 * a list item is "• ", a heading or a list starts a line, and a picture or a
 * video ([img], [previewyoutube]) goes whole - its inside is an address, not
 * text. A bracket that is no tag of Steam's ("[WIP]") stays as it is.
 */
static int sort_mark(unsigned char c) { return c && strchr(" !~_.-+=#*@$%^&|:;,`\t", c) != NULL; }

static void display_name(char *s) {
    const char *p = s;
    while (sort_mark((unsigned char) *p)) p++;
    if (!strncasecmp(p, "TBOI", 4) && (p[4] == ':' || p[4] == '-' || p[4] == ' ' || p[4] == '_')) {
        const char *q = p + 4;
        while (sort_mark((unsigned char) *q)) q++;
        if (*q) p = q;                              /* a name follows the tag */
    }
    if (!*p) return;                                /* marks alone: left as they are */
    if (p != s) memmove(s, p, strlen(p) + 1);
    size_t n = strlen(s);
    while (n && s[n - 1] == ' ') s[--n] = 0;
}

static const char *const BB_TAGS[] = {
    "h1", "h2", "h3", "h4", "h5", "h6", "b", "i", "u", "s", "strike", "spoiler", "noparse", "code", "quote", "url",
    "list", "olist", "ul", "ol", "li", "*", "hr", "img", "previewyoutube", "table", "tr", "td", "th", "p", "center",
    "size", "color", "font", "sub", "sup", "emoticon",
};

/* a tag of Steam's at p ('['): its name (lower case), whether it closes, and where it ends; 0 when it is no tag */
static int bb_tag(const char *p, const char **end, char *name, size_t room, int *closing) {
    const char *q = p + 1;
    *closing = *q == '/';
    if (*closing) q++;
    size_t k = 0;
    while ((isalnum((unsigned char) *q) || *q == '*') && k + 1 < room) name[k++] = (char) tolower((unsigned char) *q++);
    name[k] = 0;
    if (!k) return 0;
    if (*q != ']' && *q != '=' && *q != ' ') return 0;
    const char *r = q;
    while (*r && *r != ']' && *r != '[' && *r != '\n' && r - p < 400) r++;
    if (*r != ']') return 0;
    for (size_t i = 0; i < sizeof(BB_TAGS) / sizeof(BB_TAGS[0]); i++)
        if (!strcmp(name, BB_TAGS[i])) { *end = r + 1; return 1; }
    return 0;
}

static void bb_line(sb_t *b) { if (b->n && b->s[b->n - 1] != '\n') sb_puts(b, "\n"); }

static void bb_strip(const char *in, sb_t *out) {
    sb_puts(out, "");
    for (const char *p = in; *p; ) {
        const char *end;
        char name[24];
        int closing;
        if (*p == '\r') { p++; continue; }
        if (*p != '[' || !bb_tag(p, &end, name, sizeof(name), &closing)) { sb_put(out, p++, 1); continue; }
        p = end;
        if (!closing && (!strcmp(name, "img") || !strcmp(name, "previewyoutube") || !strcmp(name, "emoticon"))) {
            /* its inside is an address: up to its own closing tag, or nothing more when it has none */
            char close[40];
            snprintf(close, sizeof(close), "[/%s]", name);
            size_t cn = strlen(close);
            const char *c = p;
            while (*c && strncasecmp(c, close, cn)) c++;
            if (*c) p = c + cn;
            if (*p == '\r') p++;
            if (*p == '\n' && (!out->n || out->s[out->n - 1] == '\n')) p++;    /* a line it stood alone on goes too */
            continue;
        }
        if (!strcmp(name, "*") || !strcmp(name, "li")) {
            if (closing) continue;
            bb_line(out);
            sb_puts(out, "\xE2\x80\xA2 ");
            while (*p == ' ' || *p == '\t') p++;
        } else if (closing && strcmp(name, "td") && strcmp(name, "th")) {
            /* a block's end: its own line break follows it in the text */
        } else if (name[0] == 'h' && name[1] >= '1' && name[1] <= '6' && !name[2]) {
            bb_line(out);
        } else if (!strcmp(name, "list") || !strcmp(name, "olist") || !strcmp(name, "ul") || !strcmp(name, "ol")
                   || !strcmp(name, "hr") || !strcmp(name, "p") || !strcmp(name, "table") || !strcmp(name, "tr")
                   || !strcmp(name, "quote") || !strcmp(name, "code") || !strcmp(name, "center")) {
            bb_line(out);
        } else if ((!strcmp(name, "td") || !strcmp(name, "th")) && closing) {
            sb_puts(out, " ");
        }
    }
    /* no more than one empty line in a row, none at the ends; no spaces before a line's end */
    char *s = out->s;
    size_t w = 0, nl = 0;
    for (size_t r = 0; r < out->n; r++) {
        char c = s[r];
        if (c == '\n') {
            while (w && s[w - 1] == ' ') w--;
            if (!w || ++nl > 2) continue;
        } else {
            nl = 0;
        }
        s[w++] = c;
    }
    while (w && (s[w - 1] == '\n' || s[w - 1] == ' ')) w--;
    s[w] = 0;
    out->n = w;
}

static void read_manifest(umod_t *m) {
    char path[768];
    for (int l = 0; l < TEXT_LANGS; l++) {
        snprintf(m->name[l], sizeof(m->name[l]), "%s", m->id);
        m->version[l][0] = m->author[l][0] = m->description[l][0] = 0;
    }
    struct stat st;
    snprintf(path, sizeof(path), "%s/%s/content/entities2.xml", g_dir, m->id);
    m->content = !stat(path, &st) && S_ISREG(st.st_mode);
    /* UserMods.ADAPTED_REPORT: the converter's report, written beside the mod's files at install */
    snprintf(path, sizeof(path), "%s/%s/modport-report.txt", g_dir, m->id);
    m->adapted = !stat(path, &st) && S_ISREG(st.st_mode);
    /* a mod with nothing the port reads - a PC script mod dropped in by hand (ADD MOD refuses those): its name goes red */
    snprintf(path, sizeof(path), "%s/%s/main.lua", g_dir, m->id);
    m->script = !stat(path, &st) && S_ISREG(st.st_mode);
    m->port_dirs = m->data_dirs = 0;
    snprintf(path, sizeof(path), "%s/%s", g_dir, m->id);
    DIR *dd = opendir(path);
    if (dd) {
        struct dirent *de;
        while ((de = readdir(dd))) {
            const char *n = de->d_name;
            if (strcasecmp(n, "resources") && strncasecmp(n, "resources-dlc3", 14) && strncasecmp(n, "resources.", 10)
                && strcasecmp(n, "content") && strcasecmp(n, "patch") && strcasecmp(n, "options")) continue;
            char sub[900];
            snprintf(sub, sizeof(sub), "%s/%s", path, n);
            int dir = !stat(sub, &st) && S_ISDIR(st.st_mode);
            m->port_dirs |= dir;
            if (dir && strncasecmp(n, "resources", 9)) m->data_dirs = 1;
        }
        closedir(dd);
    } else m->port_dirs = 1;                /* the folder did not answer: not called bare for that */
    /* what an earlier read left is let go, never freed: the page may be reading it on the frame thread */
    m->mj = NULL;
    m->issues = NULL;
    m->manifest = 0;
    m->refused = 0;
    m->reason[0] = 0;
    snprintf(path, sizeof(path), "%s/%s/mod.json", g_dir, m->id);
    modj_t *mj = (modj_t *) calloc(1, sizeof(modj_t));
    if (!mj) return;
    int ok = modj_read_file(mj, path);
    if (!ok && mj->open_errno == ENOENT) { modj_free(mj); free(mj); return; }   /* the PC's layout: no mod.json */
    m->manifest = ok ? 1 : -1;
    /* not readable even leniently: the name json.c found in the text still names it (hotfix 1.71) */
    for (int l = 0; (ok || mj->name.node) && l < TEXT_LANGS; l++) {
        const char *t = modj_text(mj->name, TEXT_LANG[l]);
        if (t && t[0]) { modj_copy(m->name[l], sizeof(m->name[l]), t); display_name(m->name[l]); }
        if ((t = modj_text(mj->version, TEXT_LANG[l]))) modj_copy(m->version[l], sizeof(m->version[l]), t);
        if ((t = modj_text(mj->author, TEXT_LANG[l]))) modj_copy(m->author[l], sizeof(m->author[l]), t);
        if ((t = modj_text(mj->description, TEXT_LANG[l]))) {
            sb_t d = {0};
            bb_strip(t, &d);
            modj_copy(m->description[l], sizeof(m->description[l]), d.s ? d.s : "");
            free(d.s);
        }
    }
    sb_t b = {0};
    for (int k = 0; k < mj->nissues; k++) {
        const modj_issue_t *is = &mj->issues[k];
        issue_line(&b, modj_level_name(is->level), modj_reason_code(is->reason), is->text);
        if (is->level != MODJ_NOTE) {
            m->refused++;
            if (!m->reason[0]) snprintf(m->reason, sizeof(m->reason), "%s", modj_reason_code(is->reason));
        }
    }
    if (mj->issues_lost) sb_printf(&b, "... and %d more\n", mj->issues_lost);
    /*
     * "restart": false is the author's promise that a layer is taken up without a
     * restart. Not taken on trust (the schema, part 5): only files the game is
     * measured to read again would make it hold, and nothing is measured yet.
     */
    for (int k = 0; ok && k < mj->nsettings; k++)
        if (!mj->settings[k].restart) {
            char t[200];
            snprintf(t, sizeof(t), "setting %d \"%s\": \"restart\": false is not taken yet - the port has not measured "
                     "which files the game reads again, so a change still asks for a restart",
                     mj->settings[k].index + 1, mj->settings[k].key);
            issue_line(&b, "note", "restart_ignored", t);
        }
    m->issues = b.s;                        /* NULL when there is nothing to say */
    if (ok && mj->nsettings) m->mj = mj;
    else { modj_free(mj); free(mj); }
    for (int k = 0; k < MODJ_SETTINGS_MAX; k++) m->val[k] = m->start[k] = m->mj ? m->mj->settings[k].def : 0;
}

/* ------------------------------------------------------------ the mods' settings (plan 4.10, part 2) */
/*
 * WHAT A SETTING DOES. The port runs no script: a mod is files. So a setting
 * chooses which of the mod's files the game sees - its value turns on a layer,
 * options/<key>/<value>/, laid over the mod's own files with the same layout
 * (resources*, content/, patch/): a switch's on/ or off/, a choice's value, a
 * number's folder with the largest number not above it. No folder: the value
 * changes nothing. A setting declared later stands over one declared earlier;
 * between mods the order of MODS decides as before.
 *
 * WHERE THE VALUES LIVE. <mods>/mod-settings.ini, a section per mod (its
 * folder), a line per setting that is NOT its default - written through the
 * port's safe writer when the player changes one on the page, the file read
 * again first so that nothing else in it is lost (lines the port does not
 * know, sections of mods that are gone). A value the mod no longer has (it was
 * updated) reads as the default, a line in the report says so, and the file is
 * left as it is until the player changes a setting of that mod himself.
 * DELETE on the mod's page takes its section away.
 *
 * WHEN. The index is built at the start, as before: a changed setting waits
 * for a restart and the page says so (RESTART TO APPLY) - compared with the
 * values this run started with, so turning it back takes the note away.
 */
#define SETTINGS_FILE "mod-settings.ini"
static char g_settings_error[200];          /* why the last write of mod-settings.ini failed, "" when it did not */

/* mod-settings.ini as it is now: 0 and the text (NULL: there is none), or the errno of the read */
static int ini_read(char **text, size_t *len) {
    *text = NULL;
    *len = 0;
    char path[640];
    snprintf(path, sizeof(path), "%s/" SETTINGS_FILE, g_dir);
    errno = 0;
    FILE *f = fopen(path, "rb");
    if (!f) return errno == ENOENT ? 0 : (errno ? errno : EIO);
    sb_t b = {0};
    char chunk[4096];
    size_t got;
    while ((got = fread(chunk, 1, sizeof(chunk), f)) > 0) sb_put(&b, chunk, got);
    int bad = ferror(f);
    fclose(f);
    if (bad) { free(b.s); return EIO; }
    *text = b.s;
    *len = b.n;
    return 0;
}

/* one line of the file: a section's name (1), key=value (2), or anything else (0); spaces around are not part of it */
static int ini_line(const char *line, size_t n, char *a, size_t an, char *v, size_t vn) {
    while (n && (line[n - 1] == '\r' || line[n - 1] == ' ' || line[n - 1] == '\t')) n--;
    while (n && (*line == ' ' || *line == '\t')) { line++; n--; }
    if (n >= 2 && line[0] == '[' && line[n - 1] == ']') {
        snprintf(a, an, "%.*s", (int) (n - 2), line + 1);
        return 1;
    }
    const char *eq = memchr(line, '=', n);
    if (!eq || line[0] == ';' || line[0] == '#') return 0;
    size_t kn = (size_t) (eq - line), vs = kn + 1;
    while (kn && (line[kn - 1] == ' ' || line[kn - 1] == '\t')) kn--;
    while (vs < n && (line[vs] == ' ' || line[vs] == '\t')) vs++;
    snprintf(a, an, "%.*s", (int) kn, line);
    snprintf(v, vn, "%.*s", (int) (n - vs), line + vs);
    return 2;
}

/* the mod's values from the file's text: each setting's last line in its section, or its default */
static void settings_take(umod_t *m, const char *text, size_t len) {
    if (!m->mj) return;
    for (int k = 0; k < m->mj->nsettings; k++) m->val[k] = m->mj->settings[k].def;
    int in = 0;
    for (size_t at = 0; text && at < len;) {
        const char *nl = memchr(text + at, '\n', len - at);
        size_t n = nl ? (size_t) (nl - (text + at)) : len - at;
        char a[200], v[200];
        int kind = ini_line(text + at, n, a, sizeof(a), v, sizeof(v));
        if (kind == 1) in = !strcmp(a, m->id);
        else if (kind == 2 && in) {
            const modj_setting_t *s = modj_find(m->mj, a);
            int value;
            if (s && modj_parse_value(s, v, &value)) m->val[s - m->mj->settings] = value;
            else if (s) {
                m->val[s - m->mj->settings] = s->def;
                LOG("usermods: %s: mod-settings.ini %s=%s is no value of this setting now - the default", m->id, a, v);
                NOTE("%s: mod-settings.ini \"%s=%s\" is not a value of this setting now - the default is used; the "
                     "file is left as it is", m->id, a, v);
            }
        }
        at += n + (nl ? 1 : 0);
    }
    for (int k = 0; k < m->mj->nsettings; k++) m->start[k] = m->val[k];
}

/*
 * The file written again with this mod's section as its values are now (gone,
 * when `drop`): what else is in it - other sections, lines the port does not
 * know - stays as it is. 0, or -1 with g_settings_error saying why.
 */
static int settings_write(const umod_t *m, int drop, const char *who) {
    char *text = NULL;
    size_t len = 0;
    int err = ini_read(&text, &len);
    if (err) {
        snprintf(g_settings_error, sizeof(g_settings_error), SETTINGS_FILE " was not saved (read first: %s)", strerror(err));
        __android_log_print(ANDROID_LOG_ERROR, "isaac", "usermods: %s, asked by %s", g_settings_error, who);
        NOTE("%s, asked by %s", g_settings_error, who);
        return -1;
    }
    sb_t ours = {0};
    for (int k = 0; !drop && m->mj && k < m->mj->nsettings; k++) {
        const modj_setting_t *s = &m->mj->settings[k];
        char name[64];
        if (m->val[k] != s->def && modj_value_name(s, m->val[k], name, sizeof(name))) sb_printf(&ours, "%s=%s\n", s->key, name);
    }
    sb_t out = {0}, kept = {0};
    int in = 0, seen = 0;
    #define LEAVE() do { \
        int body_ = 0; \
        for (size_t i_ = 0; i_ < kept.n && !body_; i_++) body_ = kept.s[i_] != '\n' && kept.s[i_] != '\r' && kept.s[i_] != ' '; \
        if (ours.n || body_) { sb_printf(&out, "[%s]\n", m->id); if (ours.n) sb_put(&out, ours.s, ours.n); } \
        if (kept.n) sb_put(&out, kept.s, kept.n); \
        kept.n = 0; ours.n = 0; in = 0; \
    } while (0)
    for (size_t at = 0; text && at < len;) {
        const char *nl = memchr(text + at, '\n', len - at);
        size_t n = nl ? (size_t) (nl - (text + at)) : len - at;
        char a[200], v[200];
        int kind = ini_line(text + at, n, a, sizeof(a), v, sizeof(v));
        if (kind == 1) {
            if (in) LEAVE();
            if (!strcmp(a, m->id)) { in = 1; seen = 1; at += n + (nl ? 1 : 0); continue; }
        }
        int mine = in && (drop || (kind == 2 && m->mj && modj_find(m->mj, a)));
        if (!mine) {
            sb_t *to = in ? &kept : &out;
            sb_put(to, text + at, n);
            sb_puts(to, "\n");
        }
        at += n + (nl ? 1 : 0);
    }
    if (in) LEAVE();
    #undef LEAVE
    if (!seen && ours.n) {
        if (out.n && out.s[out.n - 1] != '\n') sb_puts(&out, "\n");
        if (out.n) sb_puts(&out, "\n");
        sb_printf(&out, "[%s]\n", m->id);
        sb_put(&out, ours.s, ours.n);
    }
    free(text);
    free(ours.s);
    free(kept.s);
    char path[640];
    snprintf(path, sizeof(path), "%s/" SETTINGS_FILE, g_dir);
    const char *step = NULL;
    errno = 0;
    FILE *f = shim_safe_fopen(path, "w");
    err = 0;
    if (!f) { step = "open"; err = errno; }
    else {
        if (out.n && fwrite(out.s, 1, out.n, f) != out.n) { step = "write"; err = errno; }
        errno = 0;
        if (shim_safe_fclose(f) != 0 && !step) { step = "close"; err = errno; }
    }
    free(out.s);
    if (step) {
        snprintf(g_settings_error, sizeof(g_settings_error), SETTINGS_FILE " was not saved (%s: %s)", step,
                 err ? strerror(err) : "unknown");
        __android_log_print(ANDROID_LOG_ERROR, "isaac", "usermods: %s, asked by %s", g_settings_error, who);
        NOTE("%s, asked by %s", g_settings_error, who);
        return -1;
    }
    g_settings_error[0] = 0;
    NOTE(SETTINGS_FILE " written (%s %s)", who, m->id);
    return 0;
}

/* a folder in dir named `want` without regard to case (the shared storage does not keep case apart either) */
static int child_dir(const char *dir, const char *want, char *out, size_t room) {
    DIR *d = opendir(dir);
    if (!d) return 0;
    struct dirent *e;
    int found = 0;
    while (!found && (e = readdir(d))) {
        if (e->d_name[0] == '.' || strcasecmp(e->d_name, want)) continue;
        struct stat st;
        snprintf(out, room, "%s/%s", dir, e->d_name);
        found = !stat(out, &st) && S_ISDIR(st.st_mode);
    }
    closedir(d);
    return found;
}

/* a number's layer: of the folders named by a whole number, the largest not above the value */
static int number_dir(const char *dir, int value, char *out, size_t room, char *name, size_t nn) {
    DIR *d = opendir(dir);
    if (!d) return 0;
    struct dirent *e;
    long best = 0;
    int found = 0;
    while ((e = readdir(d))) {
        const char *c = e->d_name;
        size_t n = strlen(c), i = c[0] == '-' ? 1 : 0;
        if (n <= i || n - i > 7) continue;
        for (size_t j = i; j < n && c; j++) if (c[j] < '0' || c[j] > '9') c = NULL;
        if (!c) continue;
        long v = strtol(e->d_name, NULL, 10);
        if (v > value || (found && v <= best)) continue;
        char full[1000];
        struct stat st;
        snprintf(full, sizeof(full), "%s/%s", dir, e->d_name);
        if (stat(full, &st) || !S_ISDIR(st.st_mode)) continue;
        best = v;
        found = 1;
        snprintf(out, room, "%s", full);
        snprintf(name, nn, "%s", e->d_name);
    }
    closedir(d);
    return found;
}

/*
 * The layers of a mod's settings as they stood at the start, in the order of
 * declaration (the index walks them back to front: the later the stronger);
 * m->layers says which, "key/folder,..." - the guard's and content.c's caches
 * are keyed by it.
 */
#define LAYERS_MAX MODJ_SETTINGS_MAX
static int mod_layers(umod_t *m, char (*out)[900]) {
    m->layers[0] = 0;
    if (!m->mj) return 0;
    char base[800], options[900];
    snprintf(base, sizeof(base), "%s/%s", g_dir, m->id);
    if (!child_dir(base, "options", options, sizeof(options))) return 0;
    int n = 0;
    size_t sl = 0;
    for (int k = 0; k < m->mj->nsettings && n < LAYERS_MAX; k++) {
        const modj_setting_t *s = &m->mj->settings[k];
        char kdir[900], name[64] = "";
        if (!child_dir(options, s->key, kdir, sizeof(kdir))) continue;
        int found;
        if (s->kind == MODJ_NUMBER) found = number_dir(kdir, m->start[k], out[n], sizeof(out[n]), name, sizeof(name));
        else found = modj_value_name(s, m->start[k], name, sizeof(name)) && child_dir(kdir, name, out[n], sizeof(out[n]));
        if (!found) continue;
        int w = snprintf(m->layers + sl, sizeof(m->layers) - sl, "%s%s/%s", sl ? "," : "", s->key, name);
        if (w > 0 && sl + (size_t) w < sizeof(m->layers)) sl += (size_t) w;
        n++;
    }
    return n;
}

/* mod.json, and the values mod-settings.ini holds for its settings */
static void read_mod(umod_t *m) {
    read_manifest(m);
    if (!m->mj) return;
    char *text = NULL;
    size_t len = 0;
    if (!ini_read(&text, &len)) settings_take(m, text, len);
    free(text);
}

static int is_state(const char *w) { return !strcmp(w, "on") || !strcmp(w, "off"); }

static int find_mod(const char *id) {
    for (int i = 0; i < g_count; i++) if (!strcmp(g_mods[i].id, id)) return i;
    return -1;
}

static int has_mark(const char *dir, const char *mark) {
    char p[900];
    struct stat st;
    snprintf(p, sizeof(p), "%s/%s", dir, mark);
    return !stat(p, &st);
}

/*
 * What a name in the mods folder is: 1 a mod's folder, 0 no mod (gone, not a
 * folder, or marked - deleted on the page, or still being put in place by the
 * installer), or -errno when the folder did not answer (plan 02.10, A2: a
 * moment's failure of the storage is not "the mod is gone").
 */
static int folder_state(const char *id) {
    char dir[768];
    struct stat st;
    snprintf(dir, sizeof(dir), "%s/%s", g_dir, id);
    errno = 0;
    if (stat(dir, &st)) return errno == ENOENT || errno == ENOTDIR ? 0 : -(errno ? errno : EIO);
    if (!S_ISDIR(st.st_mode)) return 0;
    if (has_mark(dir, REMOVE_MARK) || has_mark(dir, ADDING_MARK)) {
        if (!t_quiet) NOTE("%s has %s - skipped", id, has_mark(dir, REMOVE_MARK) ? REMOVE_MARK : ADDING_MARK);
        return 0;
    }
    return 1;
}

typedef struct { char id[ID_MAX]; int on, reach; } lrow_t;

static int row_find(const lrow_t *rows, int n, const char *id) {
    for (int i = 0; i < n; i++) if (!strcmp(rows[i].id, id)) return i;
    return -1;
}

/*
 * A list file's lines into rows. The lines are "on|off <folder>" - the order
 * of 1.666, which reads "%7s %63s", the state first, and skips a line whose
 * folder is not there (our '#' line) - and that order is written again since
 * 02.10.2026 (A6). The order of 30.09-01.10, "<folder> on|off" under a '#'
 * line saying so, is read too: a line is taken by where its on/off stands,
 * and only a line where both words could be the state needs the header. 0, or
 * the errno of a read that broke off (ferror): such a file is not read.
 * *dirty is set when the file is not what the port would write.
 */
static int list_parse(FILE *f, lrow_t *rows, int *n, int *dirty, int *lines) {
    int header = 0, id_first = 0;
    char line[256];
    *n = 0;
    if (lines) *lines = 0;
    errno = 0;
    while (fgets(line, sizeof(line), f)) {
        if (line[0] == '#') {
            header = 1;
            id_first = strstr(line, "<folder> on|off") != NULL;
            if (id_first) *dirty = 1;
            continue;
        }
        char a[ID_MAX], b[ID_MAX];
        if (sscanf(line, "%63s %63s", a, b) != 2) continue;
        if (lines) (*lines)++;
        int sa = is_state(a), sb = is_state(b);
        const char *id, *state;
        if (sa && (!sb || !id_first)) { state = a; id = b; }
        else if (sb) { id = a; state = b; *dirty = 1; }     /* the order of 30.09: written anew */
        else continue;
        if (row_find(rows, *n, id) >= 0 || *n >= MODS_MAX) { *dirty = 1; continue; }
        int fs = folder_state(id);
        if (!fs) { *dirty = 1; continue; }                    /* removed by hand, or marked */
        lrow_t *r = &rows[(*n)++];
        snprintf(r->id, sizeof(r->id), "%s", id);
        r->on = !strcmp(state, "on");
        r->reach = fs < 0 ? -fs : 0;
        if (r->reach && !t_quiet) NOTE("%s not reachable (%s) - kept in the list as %s", id, strerror(r->reach), r->on ? "on" : "off");
    }
    if (ferror(f)) return errno ? errno : EIO;
    if (!header) *dirty = 1;
    return 0;
}

/* the text the port writes: the same for mods.txt and for its copy inside the app */
static const char *list_text(size_t *len) {
    static char text[128 + MODS_MAX * (ID_MAX + 8)];
    size_t n = (size_t) snprintf(text, sizeof(text), "# the player's mods, the strongest first: on|off <folder>\n");
    for (int i = 0; i < g_count && n < sizeof(text); i++)
        n += (size_t) snprintf(text + n, sizeof(text) - n, "%s %s\n", g_mods[i].on ? "on" : "off", g_mods[i].id);
    if (n >= sizeof(text)) n = sizeof(text) - 1;
    *len = n;
    return text;
}

static void inner_path(char *out, size_t room) { snprintf(out, room, "%s/mods.txt", g_inner); }

/* the copy inside the app: the newest state of the list the port knows (A4) */
static void inner_write(const char *text, size_t len) {
    if (!g_inner[0]) return;
    mkdir(g_inner, 0700);
    char path[600];
    inner_path(path, sizeof(path));
    FILE *f = shim_safe_fopen(path, "w");
    int bad = !f;
    if (f) {
        if (fwrite(text, 1, len, f) != len) bad = 1;
        if (shim_safe_fclose(f) != 0) bad = 1;
    }
    if (bad) __android_log_print(ANDROID_LOG_ERROR, "isaac", "usermods: the copy of mods.txt inside the app was not written (%s)",
                                 strerror(errno));
}

static int inner_read(lrow_t *rows, int *n) {
    if (!g_inner[0]) return 0;
    char path[600];
    inner_path(path, sizeof(path));
    FILE *f = fopen(path, "r");
    if (!f) return 0;
    int dirty = 0;
    int err = list_parse(f, rows, n, &dirty, NULL);
    fclose(f);
    if (err) *n = 0;
    return !err;
}

/* the rows into g_mods, each mod's runtime state kept where it was there before */
static void list_take(const lrow_t *rows, int n) {
    static umod_t old[MODS_MAX];
    int oldn = g_count;
    memcpy(old, g_mods, sizeof(umod_t) * (size_t) oldn);
    g_count = 0;
    for (int i = 0; i < n && g_count < MODS_MAX; i++) {
        umod_t *m = &g_mods[g_count++];
        int k = -1;
        for (int j = 0; j < oldn; j++) if (!strcmp(old[j].id, rows[i].id)) { k = j; break; }
        if (k >= 0) *m = old[k];
        else { memset(m, 0, sizeof(*m)); snprintf(m->id, sizeof(m->id), "%s", rows[i].id); }
        m->on = rows[i].on;
        m->reach = rows[i].reach;
    }
}

/*
 * The list at the start. mods.txt is read; when it is there but cannot be
 * read, the copy inside the app stands in (or, with none, the folders join
 * off) and nothing is written. A folder the list does not name joins at the
 * end, on - one dropped in by hand.
 */
static void read_list(void) {
    g_count = 0;
    g_list_dirty = 0;
    g_list_unread = 0;
    static lrow_t rows[MODS_MAX];
    int n = 0, dirty = 0, lines = 0;
    char path[640];
    snprintf(path, sizeof(path), "%s/mods.txt", g_dir);
    errno = 0;
    FILE *f = fopen(path, "r");
    int err = f ? 0 : (errno ? errno : EIO);
    if (f) {
        err = list_parse(f, rows, &n, &dirty, &lines);
        fclose(f);
    }
    if (!err) {
        g_list_from = LIST_FILE;
    } else {
        n = 0;
        dirty = 0;
        int had = inner_read(rows, &n);
        g_list_from = had ? LIST_INNER : LIST_NONE;
        if (err == ENOENT) {
            dirty = 1;                          /* no file: one is written */
            NOTE("mods.txt is not there - the list %s", had ? "from the copy inside the app" : "from the folders, on");
        } else {
            g_list_unread = 1;
            g_list_errno = err;
            g_unread_at = now_ms();
            g_retry_at = g_unread_at + RETRY_MS;
            __android_log_print(ANDROID_LOG_ERROR, "isaac", "usermods: mods.txt could not be read (%s)", strerror(err));
            NOTE("mods.txt NOT READ (errno %d, %s) - left alone; the list %s", err, strerror(err),
                 had ? "from the copy inside the app" : "from the folders, all off");
        }
    }
    list_take(rows, n);
    int folders = 0;
    DIR *d = opendir(g_dir);
    if (d) {
        struct dirent *e;
        while ((e = readdir(d)) && g_count < MODS_MAX) {
            if (e->d_name[0] == '.' || strlen(e->d_name) >= ID_MAX) continue;
            if (find_mod(e->d_name) >= 0) { folders++; continue; }
            if (folder_state(e->d_name) != 1) continue;
            folders++;
            umod_t *m = &g_mods[g_count++];
            memset(m, 0, sizeof(*m));
            snprintf(m->id, sizeof(m->id), "%s", e->d_name);
            /* a mod nobody's list names: on - unless the list could not be read and had no copy */
            m->on = !(g_list_unread && g_list_from == LIST_NONE);
            dirty = 1;
        }
        closedir(d);
    }
    g_list_dirty = dirty;
    for (int i = 0; i < g_count; i++) read_manifest(&g_mods[i]);
    /* the settings' values: one read of mod-settings.ini for all */
    char *ini = NULL;
    size_t ini_n = 0;
    int ini_err = ini_read(&ini, &ini_n);
    if (ini_err) {
        __android_log_print(ANDROID_LOG_ERROR, "isaac", "usermods: " SETTINGS_FILE " could not be read (%s)", strerror(ini_err));
        NOTE(SETTINGS_FILE " NOT READ (errno %d, %s) - every setting at its default this run", ini_err, strerror(ini_err));
    }
    for (int i = 0; i < g_count; i++) settings_take(&g_mods[i], ini, ini_n);
    free(ini);
    if (g_list_from == LIST_FILE)
        NOTE("mods.txt read: %d lines, %d mods, %d folders%s", lines, n, folders, dirty ? " - it will be written anew" : "");
    (void) folders;
    (void) lines;
    /* the media copy read and as the port writes it: the copy inside the app follows it (A4) */
    if (g_list_from == LIST_FILE && !dirty && g_inner[0]) {
        size_t len;
        const char *text = list_text(&len);
        char ip[600];
        static char have[128 + MODS_MAX * (ID_MAX + 8) + 1];   /* list_text's room and a byte: a longer copy differs */
        inner_path(ip, sizeof(ip));
        FILE *c = fopen(ip, "rb");
        size_t got = c ? fread(have, 1, sizeof(have), c) : 0;
        if (c) fclose(c);
        if (got != len || memcmp(have, text, len)) {
            if (c) NOTE("media list differs from the copy inside the app - taken from media");
            inner_write(text, len);
        }
    }
}

/*
 * THE ONE WRITER OF mods.txt. Through the safe writer, every step checked, and
 * the file read back (A4): what is not there as written is a failure too. A
 * failure is kept in g_save_error (the MODS page shows it, the installer says
 * it) and tried again every RETRY_MS. The copy inside the app is written first
 * - it is the newest state the port knows, whatever the media store does.
 * Called with g_list_lock held.
 */
static int list_write(const char *who) {
    if (!g_dir[0]) return -1;
    size_t len;
    const char *text = list_text(&len);
    inner_write(text, len);
    char path[640];
    snprintf(path, sizeof(path), "%s/mods.txt", g_dir);
    const char *step = NULL;
    int err = 0;
    errno = 0;
    FILE *f = shim_safe_fopen(path, "w");
    if (!f) { step = "open"; err = errno; }
    if (f) {
        if (fwrite(text, 1, len, f) != len) { step = "write"; err = errno; }
        errno = 0;
        if (shim_safe_fclose(f) != 0 && !step) { step = "close"; err = errno; }
    }
    if (!step) {
        /* read back: the file holds what was written, or the write did not stay */
        static char back[sizeof(g_mods) / sizeof(g_mods[0]) * (ID_MAX + 8) + 256];
        errno = 0;
        FILE *r = fopen(path, "rb");
        size_t got = r ? fread(back, 1, sizeof(back), r) : 0;
        if (!r) { step = "read back"; err = errno; }
        else {
            if (ferror(r)) { step = "read back"; err = errno ? errno : EIO; }
            fclose(r);
            if (!step && (got != len || memcmp(back, text, len))) { step = "read back"; err = 0; }
        }
        if (!step) NOTE("mods.txt read back ok");
        else NOTE("mods.txt read back DIFFERS (%d of %d bytes)", (int) got, (int) len);
    }
    if (step) {
        char was[sizeof(g_save_error)];
        snprintf(was, sizeof(was), "%s", g_save_error);
        if (!strcmp(step, "read back") && !err)
            snprintf(g_save_error, sizeof(g_save_error), "mods.txt was not saved (read back: it holds something else)");
        else
            snprintf(g_save_error, sizeof(g_save_error), "mods.txt was not saved (%s: %s)", step, err ? strerror(err) : "unknown");
        g_retry_at = now_ms() + RETRY_MS;
        if (strcmp(was, g_save_error)) {            /* said once, not at every retry */
            __android_log_print(ANDROID_LOG_ERROR, "isaac", "usermods: %s, asked by %s", g_save_error, who);
            if (shim_report_active()) {
                char line[300];
                snprintf(line, sizeof(line), "%s, asked by %s", g_save_error, who);
                report_line("usermods", line);
            }
        }
        return -1;
    }
    if (g_list_unread) NOTE("mods.txt written while it could not be read (%s) - the player's list stands now", who);
    g_save_error[0] = 0;
    g_list_unread = 0;                              /* what the file holds now is the port's own */
#if ISAAC_DIAG
    /* the settings watch (plan 30.09, P1.6): every write of mods.txt, who asked and what it holds now */
    if (shim_report_active()) {
        static int said;
        if (said < 30) {
            said++;
            char line[560];
            int n = snprintf(line, sizeof(line), "mods.txt written (%s):", who);
            for (int i = 0; i < g_count && n > 0 && n < (int) sizeof(line) - 80; i++)
                n += snprintf(line + n, sizeof(line) - (size_t) n, " %s %s%s", g_mods[i].on ? "on" : "off",
                              g_mods[i].id, i + 1 < g_count ? "," : "");
            report_line("usermods", line);
        }
    }
#endif
    return 0;
}

/*
 * mods.txt read again, the player having changed nothing: its order and states
 * are taken. The index of this run was built from the copy or with the mods
 * off, so a difference waits for a restart (RESTART TO APPLY).
 */
static void list_reread(void) {
    char path[640];
    snprintf(path, sizeof(path), "%s/mods.txt", g_dir);
    errno = 0;
    FILE *f = fopen(path, "r");
    if (!f) {
        if (errno == ENOENT) list_write("retry");   /* it is gone: the port's list goes there */
        return;
    }
    static lrow_t rows[MODS_MAX];
    int n = 0, dirty = 0;
    int err = list_parse(f, rows, &n, &dirty, NULL);
    fclose(f);
    if (err) return;
    /* the mods the file does not name keep their state, at the end */
    for (int i = 0; i < g_count && n < MODS_MAX; i++)
        if (row_find(rows, n, g_mods[i].id) < 0 && folder_state(g_mods[i].id) == 1) {
            snprintf(rows[n].id, sizeof(rows[n].id), "%s", g_mods[i].id);
            rows[n].on = g_mods[i].on;
            rows[n].reach = 0;
            n++;
            dirty = 1;
        }
    list_take(rows, n);
    g_list_gen++;
    for (int i = 0; i < g_count; i++)
        if (!g_mods[i].name[0][0]) read_mod(&g_mods[i]);
    owed_update();
    NOTE("mods.txt read on retry after %lld s%s", (now_ms() - g_unread_at) / 1000,
         g_owed_list ? " - it differs from this run's mods: a restart applies it" : "");
    g_list_unread = 0;
    if (dirty) list_write("retry");
}

/* between the game's files and the page's frames: a list not read, or not saved, is tried again */
static void list_tick(void) {
    if (!g_list_unread && !g_save_error[0]) return;
    long long t = now_ms();
    if (t < g_retry_at) return;
    if (pthread_mutex_trylock(&g_list_lock)) return;
    if (t >= g_retry_at) {
        g_retry_at = t + RETRY_MS;
        if (g_list_unread && !g_touched) list_reread();
        else list_write("retry");
    }
    pthread_mutex_unlock(&g_list_lock);
}

void shim_usermods_save(void) {
    pthread_mutex_lock(&g_list_lock);
    list_write("save");
    pthread_mutex_unlock(&g_list_lock);
}

/* ------------------------------------------------------------ the index */

/*
 * A weaker mod's file under a key a stronger mod holds (add_file): not in the
 * index - the game never gets it - but kept for the conflict notes (THE MODS
 * OVER EACH OTHER below): who stands over whom, and the size of the sheet a
 * mod's own .anm2 was cut for.
 */
typedef struct shadow {
    struct shadow *next;
    char *src;
    owner_t owner;
    unsigned char rank;
    char key[1];
} shadow_t;
static shadow_t *g_shadows;

typedef struct { struct mfile *v; int n, cap; int *slot; int nslot; } collect_t;
static collect_t *g_collect;                /* THE PAIRS: add_file lists one mod's files into it, not the index */
static void collect_add(collect_t *c, const char *key, const char *path, int rank);

static void add_file(const char *key, const char *path, umod_t *m, int owner, int rank, const struct stat *st) {
    if (g_collect) { collect_add(g_collect, key, path, rank); return; }
    m->seen++;                                      /* its file, over or under another mod's (shim_usermods_bare) */
    unsigned k = hash_of(key) % BUCKETS;
    for (ufile_t *e = g_index[k]; e; e = e->next)
        if (!strcasecmp(e->key, key)) {                 /* a stronger mod has it already */
            if (e->owner != owner && owner < MODS_MAX) {
                size_t kn = strlen(key);
                shadow_t *s = (shadow_t *) malloc(sizeof(shadow_t) + kn);
                if (s) {
                    memcpy(s->key, key, kn + 1);
                    lower(s->key);
                    s->src = strdup(path);
                    s->owner = (owner_t) owner;
                    s->rank = (unsigned char) rank;
                    s->next = g_shadows;
                    g_shadows = s;
                }
            }
            return;
        }
    size_t kn = strlen(key);
    ufile_t *e = (ufile_t *) malloc(sizeof(ufile_t) + kn);
    if (!e) return;
    memcpy(e->key, key, kn + 1);
    lower(e->key);
    e->path = e->src = strdup(path);
    e->size = st ? (long long) st->st_size : -1;
    e->mtime = st ? (long long) st->st_mtime : -1;
    e->guard = GUARD_NEW;
    e->gamelen = e->fixlen = -1;
    e->why = NULL;
    e->owner = (owner_t) owner;
    e->rank = (unsigned char) rank;
    e->kx = 0;                                     /* malloc'd: the k of THE GAME'S FRAMES FITTED not asked yet */
    e->next = g_index[k];
    g_index[k] = e;
    g_indexed++;
    m->files++;
#if ISAAC_DIAG
    anyset_add(strchr(e->key, '|') + 1);
#endif
}

/* every file under dir, keyed "<set>|<path below the folder>" */
static void walk(const char *dir, const char *tail, const char *set, umod_t *m, int owner, int rank, int depth) {
    if (depth > 12) return;
    char here[1024];
    snprintf(here, sizeof(here), "%s%s%s", dir, tail[0] ? "/" : "", tail);
    DIR *d = opendir(here);
    if (!d) return;
    struct dirent *e;
    while ((e = readdir(d))) {
        if (e->d_name[0] == '.') continue;
        char sub[1024], full[1280];
        snprintf(sub, sizeof(sub), "%s%s%s", tail, tail[0] ? "/" : "", e->d_name);
        snprintf(full, sizeof(full), "%s/%s", dir, sub);
        struct stat st;
        if (stat(full, &st)) continue;
        if (S_ISDIR(st.st_mode)) { walk(dir, sub, set, m, owner, rank, depth + 1); continue; }
        char key[1280];
        snprintf(key, sizeof(key), "%s|%s", set, sub);
        add_file(key, full, m, owner, rank, &st);
    }
    closedir(d);
}

/* one root of a mod (the mod's folder, or a layer of its settings): its language folders (each under its own key),
   then resources-dlc3, then resources under the key for all sets - the first met of a key is the mod's; which of a
   mod's keys answers a set is THE ORDER's (usermods_find), by the rank each file carries */
static void index_root(const char *base, umod_t *m, int owner) {
    DIR *d = opendir(base);
    if (!d) return;
    struct dirent *e;
    while ((e = readdir(d))) {
        /* resources-dlc3.ru -> the set dlc3.ru; resources.jp -> data.jp */
        char set[64] = "";
        int rank = RANK_DATA_LANG;
        if (!strncasecmp(e->d_name, "resources-dlc3.", 15)) { snprintf(set, sizeof(set), "dlc3.%s", e->d_name + 15); rank = RANK_DLC3_LANG; }
        else if (!strncasecmp(e->d_name, "resources.", 10)) snprintf(set, sizeof(set), "data.%s", e->d_name + 10);
        if (!set[0] || !set[5]) continue;
        lower(set);
        char dir[1024];
        snprintf(dir, sizeof(dir), "%s/%s", base, e->d_name);
        walk(dir, "", set, m, owner, rank, 0);
    }
    closedir(d);
    char dir[1024];
    snprintf(dir, sizeof(dir), "%s/resources-dlc3", base);
    walk(dir, "", "", m, owner, RANK_DLC3, 0);
    snprintf(dir, sizeof(dir), "%s/resources", base);
    walk(dir, "", "", m, owner, RANK_DATA, 0);
}

/* the owners' settings' layers and mod.json lines, fixed for the run (the guard's thread reads them) */
static const char *g_owner_layers[MODS_MAX];
static const char *g_owner_issues[MODS_MAX];

/*
 * One mod: the layers of its settings first, the later-declared the stronger,
 * then its own folders - an index key is the first one met's.
 */
static void index_mod(umod_t *m) {
    if (g_owners >= MODS_MAX || m->reach) return;  /* a folder that did not answer: on the list, out of this run */
    m->indexed = 1;
    int owner = g_owners++;
    snprintf(g_owner[owner], sizeof(g_owner[0]), "%s", m->id);
    char base[768];
    snprintf(base, sizeof(base), "%s/%s", g_dir, m->id);
    static char layer[LAYERS_MAX][900];
    int nl = mod_layers(m, layer);
    g_owner_layers[owner] = strdup(m->layers);
    g_owner_issues[owner] = m->issues;
    for (int k = nl - 1; k >= 0; k--) index_root(layer[k], m, owner);
    index_root(base, m, owner);
    /* its data tables (resources/, content/, patch/): content.c builds them - a mod of patch/ alone is a mod; a
       layer's whole table and content/ stand over the mod's, its patch is applied over the mod's own */
    static const struct { const char *dir; char kind; } KINDS[] = {
        { "resources-dlc3", 'l' }, { "resources", 'l' }, { "content", 'c' }, { "patch", 'p' },
    };
    for (int k = nl - 1; k >= 0; k--)
        for (size_t i = 0; i < sizeof(KINDS) / sizeof(KINDS[0]); i++) {
            char dir[1100];
            snprintf(dir, sizeof(dir), "%s/%s", layer[k], KINDS[i].dir);
            content_scan_layer(m->id, dir, KINDS[i].kind);
        }
    content_scan_mod(g_dir, m->id);
    content_mod_layers(m->id, m->layers);
    int tables = content_mod_files(m->id);
    if (tables) m->content = 1;
    g_content_files += tables;
    if (nl) NOTE("%s: settings' layers in use: %s", m->id, m->layers);
}

/* the mod's file under this key, refused by the guard or not */
static ufile_t *find_key(const char *set, const char *path) {
    char key[1280];
    snprintf(key, sizeof(key), "%s|%s", set, path);
    for (ufile_t *e = g_index[hash_of(key) % BUCKETS]; e; e = e->next)
        if (!strcasecmp(e->key, key)) return e;
    return NULL;
}

/* the mod's file under this key; one the guard refused is no answer (the game's own file is) */
static const ufile_t *find_entry(const char *set, const char *path) {
    const ufile_t *e = find_key(set, path);
    return e && e->guard == GUARD_REFUSED ? NULL : e;
}

/* of two mods' files for one path, the one THE ORDER puts first: the stronger mod, then the mod's own folder order */
static int order_before(int owner_a, int rank_a, int owner_b, int rank_b) {
    return owner_a != owner_b ? owner_a < owner_b : rank_a > rank_b;
}

/*
 * THE ORDER's answer for a path in a language (THE ORDER, at ufile_t): of the
 * mods' files for it - the language's dlc3 and data folders and the folder for
 * all sets - the stronger mod's, and in one mod the higher rank. lang "" has the
 * folders for all sets alone. A refused file is no answer, and the next one is.
 */
static const ufile_t *order_pick(const char *lang, const char *path) {
    const ufile_t *c[3] = { NULL, NULL, NULL };
    if (lang[0]) {
        char set[40];
        snprintf(set, sizeof(set), "dlc3.%s", lang);
        c[0] = find_entry(set, path);
        snprintf(set, sizeof(set), "data.%s", lang);
        c[1] = find_entry(set, path);
    }
    c[2] = find_entry("", path);
    const ufile_t *best = NULL;
    for (int i = 0; i < 3; i++)
        if (c[i] && (!best || order_before(c[i]->owner, c[i]->rank, best->owner, best->rank))) best = c[i];
    return best;
}

/*
 * THE GAME'S LANGUAGE, as its own lookup shows it. The engine mounts a
 * language's sets only for a language other than English (the PC's mount,
 * 0x90c562 in 1.7.9b: the "<name>.<lang>" source only when the language is not
 * 0; the port's journal shows the same engine opening repentance-resources/
 * dlc3.fr/ for French), so a set with a language in its name, asked for under
 * an edition, names the language the game plays in. It is kept for the paths
 * the game asks under a set for all languages (the game has no copy of that file
 * in its language), where a mod's own language folder still comes first.
 */
static char g_game_lang[16];
static pthread_mutex_t g_lang_lock = PTHREAD_MUTEX_INITIALIZER;

static void game_lang_seen(const char *set, size_t n) {
    const char *dot = memchr(set, '.', n);
    if (!dot || dot == set) return;
    size_t k = n - (size_t) (dot + 1 - set);
    if (!k || k >= sizeof(g_game_lang)) return;
    for (size_t i = 0; i < k; i++) if (!isalnum((unsigned char) dot[1 + i])) return;
    if (strlen(g_game_lang) == k && !strncasecmp(g_game_lang, dot + 1, k)) return;
    pthread_mutex_lock(&g_lang_lock);
    memcpy(g_game_lang, dot + 1, k);
    g_game_lang[k] = 0;
    lower(g_game_lang);
    pthread_mutex_unlock(&g_lang_lock);
    NOTE("order: the game's language set is %s (from %.*s)", g_game_lang, (int) n, set);
}

/* ------------------------------------------------------------ reading XML as the game does */
/*
 * The game's parser is rapidxml (0x9461e2). What matters for reading a mod's
 * file the way the game will: a '>' inside a quoted value does not end a tag,
 * <!-- comments -->, <?xml ?> and <![CDATA[ ]]> are no elements, and an
 * attribute is asked for by its exact name, the first of that name. The first
 * merge scanned with strchr(s, '>') and saw commented-out entries as real ones
 * (a commented entity of the base read as "the game has it already").
 */
typedef struct { const char *s, *e; } span_t;

typedef struct {
    const char *start;                      /* '<' of the start tag */
    const char *tag_end;                    /* its '>' */
    const char *inner_end;                  /* '<' of the end tag; stop for a self-closing one */
    const char *stop;                       /* one past the element */
    span_t name;
    span_t attrs;                           /* after the name, up to '>' or "/>" */
    int depth, parent;
} xel_t;

typedef struct { xel_t *v; int n, cap; } xdoc_t;

static int xspace(char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; }

/* the '>' that ends the tag starting at p, outside quotes */
static const char *xml_tag_end(const char *p, const char *lim) {
    char q = 0;
    for (p++; p < lim; p++) {
        if (q) { if (*p == q) q = 0; continue; }
        if (*p == '"' || *p == '\'') q = *p;
        else if (*p == '>') return p;
    }
    return NULL;
}

static const char *xml_find(const char *p, const char *lim, const char *what) {
    size_t n = strlen(what);
    return lim > p ? (const char *) memmem(p, (size_t) (lim - p), what, n) : NULL;
}

/* every element of [p, lim) in document order; 0, or -1 when the file breaks off */
static int xml_parse(const char *p, const char *lim, xdoc_t *d) {
    int stack[64], sp = 0;
    d->n = 0;
    while (p < lim) {
        const char *lt = memchr(p, '<', (size_t) (lim - p)), *e;
        if (!lt) break;
        size_t left = (size_t) (lim - lt);
        if (left >= 4 && !memcmp(lt, "<!--", 4)) {
            if (!(e = xml_find(lt + 4, lim, "-->"))) return -1;
            p = e + 3; continue;
        }
        if (left >= 9 && !memcmp(lt, "<![CDATA[", 9)) {
            if (!(e = xml_find(lt + 9, lim, "]]>"))) return -1;
            p = e + 3; continue;
        }
        if (left >= 2 && lt[1] == '?') {
            if (!(e = xml_find(lt + 2, lim, "?>"))) return -1;
            p = e + 2; continue;
        }
        if (left >= 2 && lt[1] == '!') {
            if (!(e = xml_tag_end(lt, lim))) return -1;
            p = e + 1; continue;
        }
        if (left >= 2 && lt[1] == '/') {
            if (!(e = memchr(lt, '>', left))) return -1;
            if (sp) { xel_t *x = &d->v[stack[--sp]]; x->inner_end = lt; x->stop = e + 1; }
            p = e + 1; continue;
        }
        const char *q = lt + 1;
        while (q < lim && !xspace(*q) && *q != '/' && *q != '>') q++;
        if (q == lt + 1) { p = lt + 1; continue; }         /* a bare '<' in text */
        const char *gt = xml_tag_end(lt, lim);
        if (!gt) return -1;
        if (d->n == d->cap) {
            int cap = d->cap ? d->cap * 2 : 1024;
            xel_t *v = (xel_t *) realloc(d->v, sizeof(xel_t) * (size_t) cap);
            if (!v) return -1;
            d->v = v; d->cap = cap;
        }
        int idx = d->n++;
        xel_t *x = &d->v[idx];
        int self = gt[-1] == '/' && gt - 1 >= q;
        x->start = lt;
        x->tag_end = gt;
        x->name.s = lt + 1; x->name.e = q;
        x->attrs.s = q; x->attrs.e = self ? gt - 1 : gt;
        x->depth = sp;
        x->parent = sp ? stack[sp - 1] : -1;
        if (self) { x->inner_end = gt + 1; x->stop = gt + 1; }
        else {
            if (sp == 64) return -1;
            stack[sp++] = idx;
            x->inner_end = x->stop = NULL;
        }
        p = gt + 1;
    }
    return sp ? -1 : 0;
}

/* the next attribute of a start tag: name and value; the place after it, or NULL at the end */
static const char *xml_attr(const char *p, const char *end, span_t *name, span_t *val) {
    while (p < end && xspace(*p)) p++;
    if (p >= end) return NULL;
    name->s = p;
    while (p < end && *p != '=' && !xspace(*p)) p++;
    name->e = p;
    while (p < end && xspace(*p)) p++;
    if (p >= end || *p != '=') return NULL;
    p++;
    while (p < end && xspace(*p)) p++;
    if (p >= end || (*p != '"' && *p != '\'')) return NULL;
    char q = *p++;
    val->s = p;
    while (p < end && *p != q) p++;
    if (p >= end) return NULL;
    val->e = p;
    return p + 1;
}

static int span_is(span_t s, const char *text) {
    size_t n = strlen(text);
    return (size_t) (s.e - s.s) == n && !memcmp(s.s, text, n);
}

static int xml_get(const xel_t *x, const char *attr, span_t *val) {
    span_t n, v;
    for (const char *p = x->attrs.s; (p = xml_attr(p, x->attrs.e, &n, &v)); )
        if (span_is(n, attr)) { *val = v; return 1; }
    return 0;
}

static long xml_num(const xel_t *x, const char *attr, long def) {
    span_t v;
    if (!xml_get(x, attr, &v)) return def;
    char tmp[32];
    size_t n = (size_t) (v.e - v.s);
    if (n >= sizeof(tmp)) n = sizeof(tmp) - 1;
    memcpy(tmp, v.s, n);
    tmp[n] = 0;
    return strtol(tmp, NULL, 10);
}

/* ------------------------------------------------------------ the data tables: content.c */
/*
 * A MOD'S DATA TABLES ARE BUILT BY content.c (plan 30.09, 4.9 M3 and 4.10 part
 * 3, 02.10.2026): repentance-resources/data/<table>.xml and stringtable.sta
 * come from the game's file, the strongest mod's whole file (resources/, as on
 * the PC), every mod's content/ and patch/ - its spec is tools/content_sim.py.
 * Here: each indexed mod is handed to it (index_mod), it builds at the guard's
 * run, and the game's request of such a file is answered with what it built
 * (shim_usermods_map). The first merge of content/entities2.xml (29.09) lived
 * here; content.c's step 3 is that merge, byte for byte.
 */
#define CONTENT_SLOTS 48
static ufile_t g_content_slot[CONTENT_SLOTS];  /* the built files as "mod files", for the crash note */
static int g_content_slots;

static const ufile_t *content_slot(const char *path) {
    for (int i = 0; i < g_content_slots; i++) if (!strcmp(g_content_slot[i].path, path)) return &g_content_slot[i];
    if (g_content_slots >= CONTENT_SLOTS) return NULL;
    ufile_t *e = &g_content_slot[g_content_slots++];
    e->path = e->src = strdup(path);
    e->owner = OWNER_CONTENT;
    e->guard = GUARD_OK;
    e->gamelen = e->fixlen = -1;
    return e->path ? e : NULL;
}

static char *slurp(const char *path, long *len) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *buf = n >= 0 ? (char *) malloc((size_t) n + 1) : NULL;
    if (buf && fread(buf, 1, (size_t) n, f) != (size_t) n) { free(buf); buf = NULL; }
    fclose(f);
    if (buf) { buf[n] = 0; *len = n; }
    return buf;
}

#if ISAAC_DIAG
/* a file of the pack, copied, by the path the game asked for it (absolute) or by its inner path */
static char *pack_copy(const char *path, long *len) {
    const char *root = shim_pack_root();
    size_t rn = root ? strlen(root) : 0;
    if (rn && !strncmp(path, root, rn)) path += rn;
    while (*path == '/') path++;
    long n = 0;
    const void *d = shim_pack_peek(path, &n);
    char *buf = d && n > 0 ? (char *) malloc((size_t) n + 1) : NULL;
    if (!buf) return NULL;
    memcpy(buf, d, (size_t) n);
    buf[n] = 0;
    *len = n;
    return buf;
}
#endif

/* ------------------------------------------------------------ THE GUARD (plan 02.10, item 1) */
/*
 * WHAT A MOD HANDS THE ENGINE IS CHECKED FIRST (the owner, 02.10.2026: "complete
 * it ourselves - we need a fallback, whether the mod meant it or not"). A PC
 * mod works on the PC engine of the same version (1.7.9b), so what breaks here
 * is a difference of the platform, and the classes that measurably reach it
 * (round 1 and 2 of the Mines crash, the research notes of 02.10.2026, the desk
 * scripts in modcheck/) are guarded by class, not by a mod's name:
 *
 *   anm2 past the engine loader's limits (0x618b84.., the Repentance copy): its
 *        tracks of an animation sit in a stack array of 64 and the 65th is
 *        written past it (0x619a28; nulls 0x61a690), and so are 5000 frames or
 *        more of a track (0x619b4c) - memory damage, proved in the code. The
 *        game's own file is handed instead (or none, when the game has none).
 *   anm2 the port's own code reads by layer number (its menus, the HUD stats,
 *        the score card, the completion widget, the pad's glyphs, the touch
 *        buttons): never a mod's - the game's file.
 *   anm2 incomplete against the game's same file: animations, Content layers,
 *        nulls, spritesheets, the layer and null tracks the game's same
 *        animation has. Completed into content-cache/ (THE GAME's file is the
 *        model): a missing animation is the mod's default one renamed, with
 *        the game's length, loop and triggers; a missing layer track is an
 *        invisible one frame as long as the animation; a missing null track is
 *        the game's own (its positions are the game's units); a missing layer,
 *        null or sheet is the game's entry. The engine itself would survive
 *        most of these (it looks a track up and checks for NULL, round 1 p.3),
 *        but the PC's code that waits for a frame, an event or a null does not
 *        have to.
 *   anm2 sheet paths "../../resources/..." - on the PC the climb lands in the
 *        mod's own resources/ folder by its name; here the set is "data", and
 *        the picture went missing. Pointed at the set's root, as the PC finds it.
 *   whole data tables (resources/items.xml, entities2.xml, ...) of an older
 *        version, missing records the game has: the game's records are added.
 *   whole data tables with a record the game's loader would write past its
 *        fixed array (players.xml id 41 and up, items.xml collectible 733,
 *        challenges.xml 46, ...: THE LOADERS' FIXED TABLES below) - memory
 *        damage, proved in the code: the game's file is handed instead (M4).
 *   png that libpng 1.0.2 (the image's own) would stop on with png_error: the
 *        game's picture instead. The structure is checked for every picture at
 *        once; the CRC and the compressed stream, which mean reading and
 *        unpacking the whole file, at the first time the game asks for it.
 *
 * The check is of the STACKED set: the index holds one file per path, the
 * strongest enabled mod's, and that is what is checked - what the engine will
 * really get. It runs once, at the first file the game asks for (the pack, the
 * model of every check, is open only from setAssets on - after the index is
 * built in setConfigDir), and every answer is kept in content-cache/guard/
 * <mod>.txt by the file's size and time and the game's file's length, so the
 * next start reads no file of the mod again. Each mod's findings go into
 * <mods>/<id>.check.txt and onto its MODS page.
 */
#define GUARD_RULES 7                       /* the rules' version: a cache of other rules is not used (4: the tables' bounds,
                                               5: a menu .anm2 the port's patch does not land on whole is the game's,
                                               6: a table content.c builds is not refused by its raw file's bounds,
                                               7: hotfix 1.71 - no file of a mod refused for its size; a mod anm2 of the 5 files taken) */
#define ANM_TRACKS_MAX 64
#define ANM_FRAMES_MAX 5000

/*
 * (Until hotfix 1.71 a list here, BY_NUMBER, handed the game's completion_widget,
 * scoremenu, hudstats2, buttons and statuseffect_icon .anm2 whatever a mod
 * brought, and refused the mod's sheets of them of another size. 1.666 had no
 * such rule - every file of a mod was the mod's - and the port's readers take
 * their numbers from the file the game loads and step aside on their own
 * (mods.c, eid.c, bossbars.c). A picture of another size is never refused now:
 * THE GAME'S FRAMES FITTED below.)
 */

/* ---- a growing text: sb_t, at the top of the file */
static void sb_span(sb_t *b, span_t v) { sb_put(b, v.s, (size_t) (v.e - v.s)); }

/* ---- edits of a text: [at, end) replaced by text, applied in one pass */
typedef struct { const char *at, *end; char *text; size_t tn; int seq; } edit_t;
typedef struct { edit_t *v; int n, cap; } edits_t;

static void ed_add(edits_t *ed, const char *at, const char *end, const char *text, size_t tn) {
    if (ed->n == ed->cap) {
        int cap = ed->cap ? ed->cap * 2 : 32;
        edit_t *v = (edit_t *) realloc(ed->v, sizeof(edit_t) * (size_t) cap);
        if (!v) return;
        ed->v = v;
        ed->cap = cap;
    }
    edit_t *e = &ed->v[ed->n];
    e->text = (char *) malloc(tn + 1);
    if (!e->text) return;
    memcpy(e->text, text, tn);
    e->text[tn] = 0;
    e->tn = tn;
    e->at = at;
    e->end = end;
    e->seq = ed->n++;
}

static int ed_cmp(const void *a, const void *b) {
    const edit_t *x = (const edit_t *) a, *y = (const edit_t *) b;
    if (x->at != y->at) return x->at < y->at ? -1 : 1;
    return x->seq - y->seq;
}

static char *ed_apply(edits_t *ed, const char *buf, long len, long *outn) {
    qsort(ed->v, (size_t) ed->n, sizeof(edit_t), ed_cmp);
    sb_t o = {0};
    const char *at = buf;
    for (int i = 0; i < ed->n; i++) {
        edit_t *e = &ed->v[i];
        if (e->at < at) continue;                       /* overlapping: never made, but never applied twice */
        sb_put(&o, at, (size_t) (e->at - at));
        sb_put(&o, e->text, e->tn);
        at = e->end;
    }
    sb_put(&o, at, (size_t) (buf + len - at));
    *outn = (long) o.n;
    return o.s;
}

static void ed_free(edits_t *ed) {
    for (int i = 0; i < ed->n; i++) free(ed->v[i].text);
    free(ed->v);
    ed->v = NULL;
    ed->n = ed->cap = 0;
}

/* ---- walking an element's children */
static int x_next(const xdoc_t *d, int parent, const char *name, int after) {
    int depth = parent >= 0 ? d->v[parent].depth : -1;
    for (int i = after >= 0 ? after + 1 : parent + 1; i < d->n && d->v[i].depth > depth; i++)
        if (d->v[i].parent == parent && (!name || span_is(d->v[i].name, name))) return i;
    return -1;
}
static int x_child(const xdoc_t *d, int parent, const char *name) { return x_next(d, parent, name, -1); }
static int x_count(const xdoc_t *d, int parent, const char *name) {
    int n = 0;
    for (int i = x_child(d, parent, name); i >= 0; i = x_next(d, parent, name, i)) n++;
    return n;
}
static span_t x_attr(const xel_t *x, const char *attr) {
    span_t v = { NULL, NULL };
    if (!xml_get(x, attr, &v)) v.s = v.e = NULL;
    return v;
}
static int span_eq(span_t a, span_t b) {
    if (!a.s || !b.s) return a.s == b.s;
    return a.e - a.s == b.e - b.s && !memcmp(a.s, b.s, (size_t) (a.e - a.s));
}
static int x_self_closing(const xel_t *x) { return x->inner_end == x->stop; }

/*
 * New children into a container (Spritesheets, Layers, LayerAnimations...): put
 * before its end tag; a self-closing one is opened; a missing one is made at
 * the end of its parent.
 */
static void put_children(edits_t *ed, const xdoc_t *d, int parent, int cont, const char *name, const sb_t *text) {
    if (!text->n) return;
    sb_t b = {0};
    if (cont >= 0 && !x_self_closing(&d->v[cont])) {
        ed_add(ed, d->v[cont].inner_end, d->v[cont].inner_end, text->s, text->n);
    } else if (cont >= 0) {
        const xel_t *x = &d->v[cont];
        sb_printf(&b, "<%s", name);
        sb_span(&b, x->attrs);
        sb_puts(&b, ">");
        sb_put(&b, text->s, text->n);
        sb_printf(&b, "</%s>", name);
        ed_add(ed, x->start, x->stop, b.s, b.n);
    } else if (parent >= 0 && !x_self_closing(&d->v[parent])) {
        sb_printf(&b, "<%s>", name);
        sb_put(&b, text->s, text->n);
        sb_printf(&b, "</%s>", name);
        ed_add(ed, d->v[parent].inner_end, d->v[parent].inner_end, b.s, b.n);
    }
    free(b.s);
}

/* ---- an anm2's parts */
typedef struct {
    xdoc_t d;
    int root, content, sheets, layers, nulls, events, anims;
} anm_t;

static int anm_open(anm_t *a, const char *buf, long len) {
    memset(a, 0, sizeof(*a));
    if (xml_parse(buf, buf + len, &a->d)) return -1;
    a->root = x_child(&a->d, -1, "AnimatedActor");
    if (a->root < 0) return -1;
    a->content = x_child(&a->d, a->root, "Content");
    a->sheets = a->content >= 0 ? x_child(&a->d, a->content, "Spritesheets") : -1;
    a->layers = a->content >= 0 ? x_child(&a->d, a->content, "Layers") : -1;
    a->nulls = a->content >= 0 ? x_child(&a->d, a->content, "Nulls") : -1;
    a->events = a->content >= 0 ? x_child(&a->d, a->content, "Events") : -1;
    a->anims = x_child(&a->d, a->root, "Animations");
    return 0;
}

/* an entry of a Content list by its Id, or -1 */
static int anm_entry(const anm_t *a, int list, const char *tag, long id) {
    if (list < 0) return -1;
    for (int i = x_child(&a->d, list, tag); i >= 0; i = x_next(&a->d, list, tag, i))
        if (xml_num(&a->d.v[i], "Id", -9999) == id) return i;
    return -1;
}

static int anm_anim(const anm_t *a, span_t name) {
    if (a->anims < 0) return -1;
    for (int i = x_child(&a->d, a->anims, "Animation"); i >= 0; i = x_next(&a->d, a->anims, "Animation", i))
        if (span_eq(x_attr(&a->d.v[i], "Name"), name)) return i;
    return -1;
}

/* a track of an animation by its layer (or null) id, or -1 */
static int anm_track(const anm_t *a, int anim, int null, long id) {
    int list = x_child(&a->d, anim, null ? "NullAnimations" : "LayerAnimations");
    if (list < 0) return -1;
    const char *tag = null ? "NullAnimation" : "LayerAnimation", *key = null ? "NullId" : "LayerId";
    for (int i = x_child(&a->d, list, tag); i >= 0; i = x_next(&a->d, list, tag, i))
        if (xml_num(&a->d.v[i], key, -9999) == id) return i;
    return -1;
}

/* the engine loader's limits: a reason, or NULL */
static int anm_limits(const anm_t *a, char *why, size_t room) {
    if (a->anims < 0) return 0;
    for (int k = x_child(&a->d, a->anims, "Animation"); k >= 0; k = x_next(&a->d, a->anims, "Animation", k)) {
        span_t nm = x_attr(&a->d.v[k], "Name");
        int nmn = nm.s ? (int) (nm.e - nm.s) : 0;
        const char *nms = nm.s ? nm.s : "";
        for (int null = 0; null < 2; null++) {
            int list = x_child(&a->d, k, null ? "NullAnimations" : "LayerAnimations");
            int n = list >= 0 ? x_count(&a->d, list, null ? "NullAnimation" : "LayerAnimation") : 0;
            if (n > ANM_TRACKS_MAX) {
                snprintf(why, room, "past the engine loader's limits: animation \"%.*s\" has %d %s tracks (%d at most)",
                         nmn, nms, n, null ? "null" : "layer", ANM_TRACKS_MAX);
                return 1;
            }
            for (int t = list >= 0 ? x_child(&a->d, list, NULL) : -1; t >= 0; t = x_next(&a->d, list, NULL, t)) {
                int f = x_count(&a->d, t, "Frame");
                if (f >= ANM_FRAMES_MAX) {
                    snprintf(why, room, "past the engine loader's limits: animation \"%.*s\" has a track of %d frames (%d at most)",
                             nmn, nms, f, ANM_FRAMES_MAX - 1);
                    return 1;
                }
            }
        }
        int root = x_child(&a->d, k, "RootAnimation");
        int f = root >= 0 ? x_count(&a->d, root, "Frame") : 0;
        if (f >= ANM_FRAMES_MAX) {
            snprintf(why, room, "past the engine loader's limits: animation \"%.*s\" has %d root frames (%d at most)",
                     nmn, nms, f, ANM_FRAMES_MAX - 1);
            return 1;
        }
    }
    return 0;
}

/*
 * A sheet path that climbs out of the set ("../../resources/gfx/x.png" from
 * gfx/) pointed at the set's root, as the climb lands on the PC: the mod's own
 * resources/ folder. 1 when changed.
 */
static int sheet_path_fix(const char *anm_path, span_t path, char *out, size_t room) {
    char segs[48][96];
    int n = 0, climbed = 0, dirn = 0;
    /* the anm2's folder */
    const char *p = anm_path, *slash;
    while ((slash = strchr(p, '/')) && n < 48) {
        size_t k = (size_t) (slash - p);
        if (k && k < 96) { memcpy(segs[n], p, k); segs[n][k] = 0; n++; }
        p = slash + 1;
    }
    dirn = n;
    char dir[48][96];
    memcpy(dir, segs, sizeof(segs[0]) * (size_t) dirn);
    /* the sheet's path from there */
    const char *s = path.s;
    int any_up = 0;
    while (s < path.e) {
        const char *e = s;
        while (e < path.e && *e != '/' && *e != '\\') e++;
        size_t k = (size_t) (e - s);
        if (k == 2 && s[0] == '.' && s[1] == '.') {
            any_up = 1;
            if (n) n--;
            else climbed++;
        } else if (k && !(k == 1 && s[0] == '.') && k < 96 && n < 48) {
            memcpy(segs[n], s, k);
            segs[n][k] = 0;
            n++;
        }
        s = e < path.e ? e + 1 : e;
    }
    if (!any_up || !climbed || !n) return 0;
    /* the PC's folder the climb lands in: resources, resources-dlc3, resources-dlc3.<lang>, resources.<lang> */
    if (strcasecmp(segs[0], "resources") && strncasecmp(segs[0], "resources-dlc3", 14) && strncasecmp(segs[0], "resources.", 10))
        return 0;
    /* from the anm2's folder to segs[1..n) */
    int common = 0;
    while (common < dirn && common + 1 < n && !strcasecmp(dir[common], segs[common + 1])) common++;
    size_t at = 0;
    out[0] = 0;
    for (int i = common; i < dirn && at + 4 < room; i++) at += (size_t) snprintf(out + at, room - at, "../");
    for (int i = common + 1; i < n && at < room; i++)
        at += (size_t) snprintf(out + at, room - at, "%s%s", segs[i], i + 1 < n ? "/" : "");
    return at < room;
}

/* the invisible layer track, as long as its animation */
static void anm_blank_track(sb_t *b, long layer, long frames) {
    sb_printf(b, "<LayerAnimation LayerId=\"%ld\" Visible=\"false\"><Frame XPosition=\"0\" YPosition=\"0\" XPivot=\"0\" "
              "YPivot=\"0\" XCrop=\"0\" YCrop=\"0\" Width=\"0\" Height=\"0\" XScale=\"100\" YScale=\"100\" Delay=\"%ld\" "
              "Visible=\"false\" RedTint=\"255\" GreenTint=\"255\" BlueTint=\"255\" AlphaTint=\"255\" RedOffset=\"0\" "
              "GreenOffset=\"0\" BlueOffset=\"0\" Rotation=\"0\" Interpolated=\"false\"/></LayerAnimation>",
              layer, frames > 0 ? frames : 1);
}

static void sb_element(sb_t *b, const xel_t *x) { sb_put(b, x->start, (size_t) (x->stop - x->start)); }
static void sb_inner(sb_t *b, const xel_t *x) {
    if (!x_self_closing(x)) sb_put(b, x->tag_end + 1, (size_t) (x->inner_end - (x->tag_end + 1)));
}

/* a null of the mod's under this id carries the game's name (or will: it is added) */
static int null_same(const anm_t *m, const anm_t *g, long id, const sb_t *added_nulls_ids) {
    int mi = anm_entry(m, m->nulls, "Null", id), gi = anm_entry(g, g->nulls, "Null", id);
    if (gi < 0) return 0;
    if (mi < 0) {
        char tag[32];
        snprintf(tag, sizeof(tag), ",%ld,", id);
        return added_nulls_ids->s && strstr(added_nulls_ids->s, tag) != NULL;
    }
    return span_eq(x_attr(&m->d.v[mi], "Name"), x_attr(&g->d.v[gi], "Name"));
}

/*
 * The anm2 rules. 0 as it is, 1 completed (*out), -1 refused (the game's file
 * goes, or none); *why says it.
 */
static int guard_anm2(const char *path, const char *mbuf, long mlen, const char *gbuf, long glen,
                      char **out, long *outn, char *why, size_t room) {
    *out = NULL;
    anm_t m, g;
    int gok = 0;
    if (anm_open(&m, mbuf, mlen)) {
        free(m.d.v);
        snprintf(why, room, "it does not read as an animation file (broken XML) - %s", gbuf ? "the game's file is used" : "left out");
        return -1;
    }
    if (anm_limits(&m, why, room)) {
        free(m.d.v);
        size_t k = strlen(why);
        snprintf(why + k, room - k, " - %s", gbuf ? "the game's file is used" : "left out");
        return -1;
    }
    if (gbuf) gok = !anm_open(&g, gbuf, glen);
    edits_t ed = {0};
    int sheets_added = 0, layers_added = 0, nulls_added = 0, anims_added = 0, tracks_added = 0, ntracks_added = 0,
        paths_fixed = 0, events_added = 0;
    sb_t add_sheets = {0}, add_layers = {0}, add_nulls = {0}, add_events = {0}, add_anims = {0}, null_ids = {0};
    sb_puts(&null_ids, ",");
    /* the sheet paths that climb out of the set */
    for (int i = m.sheets >= 0 ? x_child(&m.d, m.sheets, "Spritesheet") : -1; i >= 0; i = x_next(&m.d, m.sheets, "Spritesheet", i)) {
        span_t pv = x_attr(&m.d.v[i], "Path");
        char fixed[600];
        if (pv.s && sheet_path_fix(path, pv, fixed, sizeof(fixed))) {
            ed_add(&ed, pv.s, pv.e, fixed, strlen(fixed));
            paths_fixed++;
        }
    }
    long first_sheet = -1;
    if (m.sheets >= 0) {
        int i = x_child(&m.d, m.sheets, "Spritesheet");
        if (i >= 0) first_sheet = xml_num(&m.d.v[i], "Id", -1);
    }
    if (gok) {
        /* spritesheets the game's file has by id (code swaps a sheet by its number) */
        for (int i = g.sheets >= 0 ? x_child(&g.d, g.sheets, "Spritesheet") : -1; i >= 0; i = x_next(&g.d, g.sheets, "Spritesheet", i)) {
            long id = xml_num(&g.d.v[i], "Id", -1);
            if (id < 0 || anm_entry(&m, m.sheets, "Spritesheet", id) >= 0) continue;
            sb_puts(&add_sheets, "\n");
            sb_element(&add_sheets, &g.d.v[i]);
            if (first_sheet < 0) first_sheet = id;
            sheets_added++;
        }
        /* layers */
        for (int i = g.layers >= 0 ? x_child(&g.d, g.layers, "Layer") : -1; i >= 0; i = x_next(&g.d, g.layers, "Layer", i)) {
            long id = xml_num(&g.d.v[i], "Id", -1);
            if (id < 0 || anm_entry(&m, m.layers, "Layer", id) >= 0) continue;
            long sid = xml_num(&g.d.v[i], "SpritesheetId", -1);
            if (anm_entry(&m, m.sheets, "Spritesheet", sid) < 0 && anm_entry(&g, g.sheets, "Spritesheet", sid) < 0) sid = first_sheet;
            sb_puts(&add_layers, "\n<Layer Name=\"");
            sb_span(&add_layers, x_attr(&g.d.v[i], "Name"));
            sb_printf(&add_layers, "\" Id=\"%ld\" SpritesheetId=\"%ld\"/>", id, sid < 0 ? 0 : sid);
            layers_added++;
        }
        /* nulls */
        for (int i = g.nulls >= 0 ? x_child(&g.d, g.nulls, "Null") : -1; i >= 0; i = x_next(&g.d, g.nulls, "Null", i)) {
            long id = xml_num(&g.d.v[i], "Id", -1);
            if (id < 0 || anm_entry(&m, m.nulls, "Null", id) >= 0) continue;
            sb_puts(&add_nulls, "\n");
            sb_element(&add_nulls, &g.d.v[i]);
            sb_printf(&null_ids, "%ld,", id);
            nulls_added++;
        }
        /* animations */
        long next_event = 0;
        for (int i = m.events >= 0 ? x_child(&m.d, m.events, "Event") : -1; i >= 0; i = x_next(&m.d, m.events, "Event", i)) {
            long id = xml_num(&m.d.v[i], "Id", -1);
            if (id >= next_event) next_event = id + 1;
        }
        span_t defname = m.anims >= 0 ? x_attr(&m.d.v[m.anims], "DefaultAnimation") : (span_t) { NULL, NULL };
        int mdef = defname.s ? anm_anim(&m, defname) : -1;
        if (mdef < 0 && m.anims >= 0) mdef = x_child(&m.d, m.anims, "Animation");
        /* the events a trigger of a new animation needs: by name, the mod's id or a new one */
        typedef struct { span_t name; long id; } ev_t;
        ev_t evs[64];
        int nev = 0;
        for (int ga = g.anims >= 0 ? x_child(&g.d, g.anims, "Animation") : -1; ga >= 0; ga = x_next(&g.d, g.anims, "Animation", ga)) {
            span_t name = x_attr(&g.d.v[ga], "Name");
            int ma = anm_anim(&m, name);
            if (ma >= 0) {
                /* an animation both have: the tracks the game's has */
                long frames = xml_num(&m.d.v[ma], "FrameNum", 1);
                sb_t la = {0}, na = {0};
                int glist = x_child(&g.d, ga, "LayerAnimations");
                for (int t = glist >= 0 ? x_child(&g.d, glist, "LayerAnimation") : -1; t >= 0; t = x_next(&g.d, glist, "LayerAnimation", t)) {
                    long lid = xml_num(&g.d.v[t], "LayerId", -1);
                    if (lid < 0 || anm_track(&m, ma, 0, lid) >= 0) continue;
                    anm_blank_track(&la, lid, frames);
                    tracks_added++;
                }
                glist = x_child(&g.d, ga, "NullAnimations");
                for (int t = glist >= 0 ? x_child(&g.d, glist, "NullAnimation") : -1; t >= 0; t = x_next(&g.d, glist, "NullAnimation", t)) {
                    long nid = xml_num(&g.d.v[t], "NullId", -1);
                    if (nid < 0 || anm_track(&m, ma, 1, nid) >= 0 || !null_same(&m, &g, nid, &null_ids)) continue;
                    sb_element(&na, &g.d.v[t]);
                    ntracks_added++;
                }
                put_children(&ed, &m.d, ma, x_child(&m.d, ma, "LayerAnimations"), "LayerAnimations", &la);
                put_children(&ed, &m.d, ma, x_child(&m.d, ma, "NullAnimations"), "NullAnimations", &na);
                free(la.s);
                free(na.s);
                continue;
            }
            /* an animation the mod lacks: the mod's default one, with the game's length, loop, nulls and triggers */
            long frames = xml_num(&g.d.v[ga], "FrameNum", 1);
            span_t loop = x_attr(&g.d.v[ga], "Loop");
            sb_t b = {0};
            sb_puts(&b, "\n<Animation Name=\"");
            sb_span(&b, name);
            sb_printf(&b, "\" FrameNum=\"%ld\" Loop=\"", frames);
            if (loop.s) sb_span(&b, loop); else sb_puts(&b, "false");
            sb_puts(&b, "\">");
            int mroot = mdef >= 0 ? x_child(&m.d, mdef, "RootAnimation") : -1;
            int groot = x_child(&g.d, ga, "RootAnimation");
            if (mroot >= 0) sb_element(&b, &m.d.v[mroot]);
            else if (groot >= 0) sb_element(&b, &g.d.v[groot]);
            sb_puts(&b, "<LayerAnimations>");
            int mlist = mdef >= 0 ? x_child(&m.d, mdef, "LayerAnimations") : -1;
            if (mlist >= 0) sb_inner(&b, &m.d.v[mlist]);
            int glist = x_child(&g.d, ga, "LayerAnimations");
            for (int t = glist >= 0 ? x_child(&g.d, glist, "LayerAnimation") : -1; t >= 0; t = x_next(&g.d, glist, "LayerAnimation", t)) {
                long lid = xml_num(&g.d.v[t], "LayerId", -1);
                if (lid < 0 || (mdef >= 0 && anm_track(&m, mdef, 0, lid) >= 0)) continue;
                anm_blank_track(&b, lid, frames);
            }
            sb_puts(&b, "</LayerAnimations><NullAnimations>");
            glist = x_child(&g.d, ga, "NullAnimations");
            for (int t = glist >= 0 ? x_child(&g.d, glist, "NullAnimation") : -1; t >= 0; t = x_next(&g.d, glist, "NullAnimation", t)) {
                long nid = xml_num(&g.d.v[t], "NullId", -1);
                if (nid >= 0 && null_same(&m, &g, nid, &null_ids)) sb_element(&b, &g.d.v[t]);
            }
            sb_puts(&b, "</NullAnimations><Triggers>");
            int gtr = x_child(&g.d, ga, "Triggers");
            for (int t = gtr >= 0 ? x_child(&g.d, gtr, "Trigger") : -1; t >= 0; t = x_next(&g.d, gtr, "Trigger", t)) {
                long gev = xml_num(&g.d.v[t], "EventId", -1);
                int ge = anm_entry(&g, g.events, "Event", gev);
                if (ge < 0) continue;
                span_t evname = x_attr(&g.d.v[ge], "Name");
                long mid = -1;
                for (int i = m.events >= 0 ? x_child(&m.d, m.events, "Event") : -1; i >= 0 && mid < 0; i = x_next(&m.d, m.events, "Event", i))
                    if (span_eq(x_attr(&m.d.v[i], "Name"), evname)) mid = xml_num(&m.d.v[i], "Id", -1);
                for (int i = 0; i < nev && mid < 0; i++) if (span_eq(evs[i].name, evname)) mid = evs[i].id;
                if (mid < 0 && nev < 64) {
                    mid = next_event++;
                    evs[nev].name = evname;
                    evs[nev].id = mid;
                    nev++;
                    sb_printf(&add_events, "\n<Event Id=\"%ld\" Name=\"", mid);
                    sb_span(&add_events, evname);
                    sb_puts(&add_events, "\"/>");
                    events_added++;
                }
                if (mid < 0) continue;
                sb_printf(&b, "<Trigger EventId=\"%ld\" AtFrame=\"%ld\"/>", mid, xml_num(&g.d.v[t], "AtFrame", 0));
            }
            sb_puts(&b, "</Triggers></Animation>");
            sb_put(&add_anims, b.s, b.n);
            free(b.s);
            anims_added++;
        }
    }
    /* a layer pointing at a sheet the file does not list: the first sheet's picture under that id */
    for (int i = m.layers >= 0 ? x_child(&m.d, m.layers, "Layer") : -1; i >= 0; i = x_next(&m.d, m.layers, "Layer", i)) {
        long sid = xml_num(&m.d.v[i], "SpritesheetId", -1);
        if (sid < 0 || anm_entry(&m, m.sheets, "Spritesheet", sid) >= 0) continue;
        char tag[40];
        snprintf(tag, sizeof(tag), "Id=\"%ld\"", sid);
        if (add_sheets.s && strstr(add_sheets.s, tag)) continue;
        int f0 = m.sheets >= 0 ? x_child(&m.d, m.sheets, "Spritesheet") : -1;
        if (f0 < 0) continue;
        sb_puts(&add_sheets, "\n<Spritesheet Path=\"");
        sb_span(&add_sheets, x_attr(&m.d.v[f0], "Path"));
        sb_printf(&add_sheets, "\" Id=\"%ld\"/>", sid);
        sheets_added++;
    }
    put_children(&ed, &m.d, m.content, m.sheets, "Spritesheets", &add_sheets);
    put_children(&ed, &m.d, m.content, m.layers, "Layers", &add_layers);
    put_children(&ed, &m.d, m.content, m.nulls, "Nulls", &add_nulls);
    put_children(&ed, &m.d, m.content, m.events, "Events", &add_events);
    put_children(&ed, &m.d, m.root, m.anims, "Animations", &add_anims);
    int changed = ed.n > 0;
    if (changed) {
        *out = ed_apply(&ed, mbuf, mlen, outn);
        sb_t w = {0};
        if (anims_added) sb_printf(&w, ", %d animations", anims_added);
        if (layers_added) sb_printf(&w, ", %d layers", layers_added);
        if (nulls_added) sb_printf(&w, ", %d nulls", nulls_added);
        if (sheets_added) sb_printf(&w, ", %d spritesheets", sheets_added);
        if (tracks_added) sb_printf(&w, ", %d layer tracks", tracks_added);
        if (ntracks_added) sb_printf(&w, ", %d null tracks", ntracks_added);
        if (events_added) sb_printf(&w, ", %d events", events_added);
        size_t k = 0;
        why[0] = 0;
        if (w.n) k = (size_t) snprintf(why, room, "completed from the game's file: %s", w.s + 2);
        if (paths_fixed && k < room)
            snprintf(why + k, room - k, "%s%d sheet paths out of the set (../../resources/) pointed at its root",
                     k ? "; " : "", paths_fixed);
        free(w.s);
    }
    ed_free(&ed);
    free(add_sheets.s); free(add_layers.s); free(add_nulls.s); free(add_events.s); free(add_anims.s); free(null_ids.s);
    free(m.d.v);
    if (gok || gbuf) free(g.d.v);
    if (changed && !*out) { snprintf(why, room, "out of memory while completing it"); return 0; }
    return changed ? 1 : 0;
}

static void span_copy(span_t v, char *out, size_t cap) {
    size_t n = (size_t) (v.e - v.s);
    if (n >= cap) n = cap - 1;
    memcpy(out, v.s, n);
    out[n] = 0;
}

/* the class a tag counts in: items.xml keeps active, passive and familiar in one id space */
static void tag_class_of(const xel_t *x, char *out, size_t cap) {
    size_t n = (size_t) (x->name.e - x->name.s);
    if (n >= cap) n = cap - 1;
    memcpy(out, x->name.s, n);
    out[n] = 0;
    if (!strcmp(out, "active") || !strcmp(out, "passive") || !strcmp(out, "familiar")) snprintf(out, cap, "item");
}

/* a record's key in a whole table: entities by type.variant.subtype, the rest by id or name; "" for none */
static void rec_key(const xel_t *x, char *out, size_t room) {
    char cls[48];
    tag_class_of(x, cls, sizeof(cls));
    span_t v;
    out[0] = 0;
    if (!strcmp(cls, "entity"))
        snprintf(out, room, "entity[%ld.%ld.%ld]", xml_num(x, "id", -1), xml_num(x, "variant", 0), xml_num(x, "subtype", 0));
    else if (xml_get(x, "id", &v) || xml_get(x, "Id", &v) || xml_get(x, "ID", &v) || xml_get(x, "name", &v) || xml_get(x, "Name", &v))
        snprintf(out, room, "%s[%.*s]", cls, (int) (v.e - v.s), v.s);
}

/*
 * A whole data table: the game's records the mod's file lacks, added before its
 * root's end. 0 as it is, 1 completed, -1 refused (it does not parse).
 */
static int guard_table(const char *mbuf, long mlen, const char *gbuf, long glen, char **out, long *outn, char *why, size_t room) {
    *out = NULL;
    xdoc_t md = {0}, gd = {0};
    if (xml_parse(mbuf, mbuf + mlen, &md)) {
        free(md.v);
        snprintf(why, room, "it does not read as XML (an element is not closed) - the game's file is used");
        return -1;
    }
    if (xml_parse(gbuf, gbuf + glen, &gd)) { free(md.v); free(gd.v); return 0; }
    edits_t ed = {0};
    int added = 0;
    for (int gr = x_child(&gd, -1, NULL); gr >= 0; gr = x_next(&gd, -1, NULL, gr)) {
        /* the mod's root of the same name */
        int mr = -1;
        for (int i = x_child(&md, -1, NULL); i >= 0 && mr < 0; i = x_next(&md, -1, NULL, i))
            if (span_eq(md.v[i].name, gd.v[gr].name)) mr = i;
        if (mr < 0 || x_self_closing(&md.v[mr])) continue;
        /* the mod's keys, with their hashes for a quick no */
        int cap = 0, nk = 0;
        for (int i = x_child(&md, mr, NULL); i >= 0; i = x_next(&md, mr, NULL, i)) cap++;
        char (*keys)[128] = cap ? (char (*)[128]) malloc(128 * (size_t) cap) : NULL;
        unsigned *hk = cap ? (unsigned *) malloc(sizeof(unsigned) * (size_t) cap) : NULL;
        if (cap && (!keys || !hk)) { free(keys); free(hk); continue; }
        for (int i = x_child(&md, mr, NULL); i >= 0; i = x_next(&md, mr, NULL, i)) {
            rec_key(&md.v[i], keys[nk], 128);
            hk[nk] = hash_of(keys[nk]);
            nk++;
        }
        sb_t b = {0};
        for (int i = x_child(&gd, gr, NULL); i >= 0; i = x_next(&gd, gr, NULL, i)) {
            char k[128];
            rec_key(&gd.v[i], k, sizeof(k));
            if (!k[0]) continue;
            unsigned h = hash_of(k);
            int have = 0;
            for (int j = 0; j < nk && !have; j++) have = hk[j] == h && !strcmp(keys[j], k);
            if (have) continue;
            sb_puts(&b, "\n");
            sb_element(&b, &gd.v[i]);
            added++;
        }
        if (b.n) {
            sb_puts(&b, "\n");
            ed_add(&ed, md.v[mr].inner_end, md.v[mr].inner_end, b.s, b.n);
        }
        free(b.s);
        free(keys);
        free(hk);
    }
    int changed = ed.n > 0;
    if (changed) {
        *out = ed_apply(&ed, mbuf, mlen, outn);
        snprintf(why, room, "an older table: %d records of the game's were missing - added", added);
    }
    ed_free(&ed);
    free(md.v);
    free(gd.v);
    return changed ? (*out ? 1 : 0) : 0;
}

/*
 * THE LOADERS' FIXED TABLES (plan 30.09, 2.3 and M4; batch 5). A whole data
 * table of a mod replaces the game's as on the PC - except one that makes a
 * Repentance loader (the load list at 0x66eaa0) write past an array it sized
 * itself, or through a pointer it read past one, or that hangs it: memory
 * damage or a dead game, proved in the code. Such a file is not handed to the
 * engine at all; the game's own file is. Each rule is the loader's own, read
 * in the image (the research notes of 02.10.2026, mod bugs, "Batch 5: M4"):
 *
 *   players.xml      resize(41) 0x7855dc; the record is copied to [id] at
 *                    0x7862c8 (0x178 a record) - below 0 is skipped
 *                    (0x7862b4), above 40 nothing checks.
 *   challenges.xml   resize(46) 0x66f9a0; [id] at 0x67069c (umaddl, 0xf0 a
 *                    record, no check). The id lives in a stack slot only an
 *                    id attribute writes (0x66ff54): a record without one
 *                    takes the last record's, the first one the stack's.
 *   items.xml        pointers stored with no check, the id signed (ldrsw):
 *                    collectibles (passive, active, familiar) resize(733)
 *                    0x738680, [id] at 0x739200; trinkets resize(190)
 *                    0x73868c, [id] at 0x7392dc; null items - <null> and any
 *                    tag it does not know (it logs "unknown item type" and
 *                    keeps the type 0 the new item was made with) - in the
 *                    132 the item config made (0x737efc), [id] at 0x7392dc.
 *   items_metadata.xml  "Collectible/Trinket ID out of bounds" is only told:
 *                    the pointer is still read at [id] (0x73b4f4, sxtw) and
 *                    16 bytes written through it (0x73b4fc..0x73b50c). A
 *                    <trinket> goes to the 190, any other tag to the 733.
 *   pocketitems.xml  cards and runes: resize(98) 0x73dcac, [id] at 0x73e3dc;
 *                    pill effects: resize(50) 0x73dcb8, [id] at 0x73e438 -
 *                    below 0 the loader refuses itself (0x73e24c, 0x73e33c).
 *   sounds.xml       resize(833) 0x7eff74; 833 and up is told and skipped by
 *                    a SIGNED compare (0x7f01a0..0x7f01c4), so a negative id
 *                    passes, zero-extended into madd at 0x7f0220 (0x1f0 a
 *                    record): a write terabytes off. (music.xml is safe: an
 *                    unsigned check 1..118 at 0x7608d0.)
 *   wisps.xml        <wisp id="sN">: the special wisps, resize(19) at
 *                    config+0x1d8 (0x738098); the old one at [N] is read and
 *                    freed (0x73b89c) and [N] cleared (0x73b8b4), sxtw. Any
 *                    other id is a collectible: told when past the 733
 *                    (0x73bc50) and read at [id] anyway (0x73bc70), its
 *                    fields written. The id is first_attribute("id").
 *   costumes2.xml    type="passive|active|familiar": told past the 733
 *                    (0x73ae18), read at [id] (0x73ae3c) and written through
 *                    (0x73ae44); "trinket" the same on the 190 (0x73af74,
 *                    0x73af98); "none": the null item at [id] of the 132
 *                    read with no bound (0x73af0c) and written through with
 *                    no NULL check (0x73af10) - every slot of the 132 holds
 *                    an item from the start (the config fills them,
 *                    0x737fb8..0x738000), so the bound is what matters.
 *   bossportraits.xml  the boss itself is kept only for 1..103 (0x7879b0,
 *                    unsigned), but each <alt> of it is pushed into the
 *                    record at [id] of the 104 (0x787698) with no bound
 *                    (0x787b5c..0x787b78, the id zero-extended). The id slot
 *                    is written only by an id attribute (0x7877e0): a boss
 *                    without one takes the last one's, the first the stack's.
 *   locusts.xml      as wisps: told past the 733 (0x73c9e8), read at [id]
 *                    anyway (0x73ca08) and written through.
 *   cutscenes.xml    a fixed array of 27 x 0x60: madd id*0x60 at 0x7560f4,
 *                    no check.
 *   stages.xml       a fixed array of 37 x 0x100: base + id<<8 at 0x751ec8,
 *                    no check.
 *   seedmenu.xml     the seed's id indexes SaveState+0xe94 bytes (0x7036cc)
 *                    and manager+0x23640 (0x5d3228) with no check; the
 *                    tables' length is not pinned - 79, the game's highest
 *                    id, is the bound (conservative).
 *   recipes.xml      an input byte of 0x80 and up is read signed (0x73d9b4)
 *                    and indexes a table backwards: input is ASCII or the
 *                    file is not handed out (recipe_ascii below).
 *   (batch 5, M3: the whole danger column of the table in
 *   the research notes of 02.10.2026, batch 5 content spec, part 4; content.c
 *   asks the
 *   same rules about every table it builds - the mods' whole files first,
 *   then the finished result: shim_usermods_table_refusal.)
 *   entities2.xml    a std::map, no array - but the bestiary's parent chain
 *                    is walked with no end (0x782324: look up, follow
 *                    +0x198/+0x19c while the parent type is not 0): a chain
 *                    that comes back to itself hangs the game whenever it is
 *                    walked. See guard_parents.
 *
 * How a record is found, as the loaders do: the root by first_node(name),
 * exact; players, challenges and sounds take first_node("player"/...) and
 * then every next sibling whatever its name (rapidxml's m_next_sibling,
 * +0x58); the others take every child and go by its tag (strcmp, exact) or,
 * costumes2, by its type attribute. The id is atoi of the value after
 * rapidxml's entities, every attribute walked and the last "id" kept (the
 * attribute loop, 0x738c90) - wisps.xml asks first_attribute("id").
 */
enum { NOID_SKIP, NOID_ZERO, NOID_INHERIT };    /* a record without an id: not written (or unknown) / id 0 / the last one's */

typedef struct {
    const char *file, *root;
    const char *first;                      /* the first record by name, then every sibling; NULL: every child */
    const char *by;                         /* NULL: a record's kind is its tag; else the value of this attribute */
    const char *kinds;                      /* "a|b": the kinds the bound is of; "!a|b": every other; NULL: all */
    char prefix;                            /* 's': only ids written "s<N>", N the id; '-': only the others; 0 any */
    char first_id;                          /* the loader takes the first "id", not the last */
    char noid;                              /* NOID_* */
    const char *child;                      /* written only for a record with this child (bossportraits' alt) */
    long lo, hi;                            /* the ids the loader's table holds */
    char below_skipped, above_skipped;      /* the loader itself skips an id below lo / above hi */
    const char *proof;                      /* where the loader goes through the id */
} tbound_t;

static const tbound_t TABLE_BOUNDS[] = {
    { "players.xml",        "players",     "player",    NULL,   NULL,                               0,   0, NOID_SKIP,    NULL,    0, 40,  1, 0, "0x7855dc resize(41), 0x7862c8" },
    { "challenges.xml",     "challenges",  "challenge", NULL,   NULL,                               0,   0, NOID_INHERIT, NULL,    0, 45,  0, 0, "0x66f9a0 resize(46), 0x67069c" },
    { "items.xml",          "items",       NULL,        NULL,   "passive|active|familiar",          0,   0, NOID_ZERO,    NULL,    0, 732, 0, 0, "0x738680 resize(733), 0x739200" },
    { "items.xml",          "items",       NULL,        NULL,   "trinket",                          0,   0, NOID_ZERO,    NULL,    0, 189, 0, 0, "0x73868c resize(190), 0x7392dc" },
    { "items.xml",          "items",       NULL,        NULL,   "!passive|active|familiar|trinket", 0,   0, NOID_ZERO,    NULL,    0, 131, 0, 0, "0x737efc resize(132), 0x7392dc" },
    { "items_metadata.xml", "items",       NULL,        NULL,   "trinket",                          0,   0, NOID_ZERO,    NULL,    0, 189, 0, 0, "0x73b4c4, 0x73b4f4 read, 0x73b4fc written" },
    { "items_metadata.xml", "items",       NULL,        NULL,   "!trinket",                         0,   0, NOID_ZERO,    NULL,    0, 732, 0, 0, "0x73b494, 0x73b4f4 read, 0x73b4fc written" },
    { "pocketitems.xml",    "pocketitems", NULL,        NULL,   "card|rune",                        0,   0, NOID_ZERO,    NULL,    0, 97,  1, 0, "0x73dcac resize(98), 0x73e3dc" },
    { "pocketitems.xml",    "pocketitems", NULL,        NULL,   "pilleffect",                       0,   0, NOID_ZERO,    NULL,    0, 49,  1, 0, "0x73dcb8 resize(50), 0x73e438" },
    { "sounds.xml",         "sounds",      "sound",     NULL,   NULL,                               0,   0, NOID_SKIP,    NULL,    0, 832, 0, 1, "0x7eff74 resize(833), 0x7f0220" },
    { "wisps.xml",          "wisps",       NULL,        NULL,   "wisp",                             's', 1, NOID_SKIP,    NULL,    0, 18,  0, 0, "0x738098 resize(19), 0x73b89c read, 0x73b8b4" },
    { "wisps.xml",          "wisps",       NULL,        NULL,   "wisp",                             '-', 1, NOID_SKIP,    NULL,    0, 732, 0, 0, "0x73bc50, 0x73bc70 read and written through" },
    { "costumes2.xml",      "costumes",    NULL,        "type", "passive|active|familiar",          0,   0, NOID_ZERO,    NULL,    0, 732, 0, 0, "0x73ae18, 0x73ae3c read, 0x73ae44 written" },
    { "costumes2.xml",      "costumes",    NULL,        "type", "trinket",                          0,   0, NOID_ZERO,    NULL,    0, 189, 0, 0, "0x73af74, 0x73af98 read, 0x73afa0 written" },
    { "costumes2.xml",      "costumes",    NULL,        "type", "none",                             0,   0, NOID_ZERO,    NULL,    0, 131, 0, 0, "0x73af0c read, 0x73af10 written with no NULL check" },
    { "bossportraits.xml",  "bosses",      NULL,        NULL,   NULL,                               0,   0, NOID_INHERIT, "alt",  0, 103, 0, 0, "0x787698 resize(104), 0x787b6c" },
    { "locusts.xml",        "locusts",     NULL,        NULL,   "locust",                           0,   0, NOID_SKIP,    NULL,    0, 732, 0, 0, "0x73c9e8, 0x73ca08 read and written through" },
    { "cutscenes.xml",      "cutscenes",   NULL,        NULL,   "cutscene",                         0,   0, NOID_SKIP,    NULL,    0, 26,  0, 0, "27 x 0x60, madd 0x7560f4" },
    { "stages.xml",         "stages",      NULL,        NULL,   "stage",                            0,   0, NOID_SKIP,    NULL,    0, 36,  0, 0, "37 x 0x100, 0x751ec8" },
    { "seedmenu.xml",       "seedmenu",    NULL,        NULL,   "seed",                             0,   0, NOID_SKIP,    NULL,    0, 79,  0, 0, "SaveState+0xe94 0x7036cc, manager+0x23640 0x5d3228" },
};
#define TABLE_BOUNDS_N ((int) (sizeof(TABLE_BOUNDS) / sizeof(TABLE_BOUNDS[0])))
#define BOUNDS_MARK " is not in the game's table "      /* in a refusal's reason: what the page and the counts look for */

static int kind_in(const char *s, size_t n, const char *kinds) {
    int not = kinds[0] == '!', hit = 0;
    for (const char *t = kinds + not; *t && !hit; ) {
        const char *bar = strchr(t, '|');
        size_t k = bar ? (size_t) (bar - t) : strlen(t);
        hit = n == k && !memcmp(s, t, k);
        t += k + (bar ? 1 : 0);
    }
    return not ? !hit : hit;
}

/* an attribute's value as rapidxml hands it: its entities translated; the first or the last of the name; 0 none */
static int attr_value(const xel_t *x, const char *attr, int first, char *out, size_t cap) {
    span_t n, v, got = { NULL, NULL };
    for (const char *p = x->attrs.s; (p = xml_attr(p, x->attrs.e, &n, &v)); )
        if (span_is(n, attr)) { got = v; if (first) break; }
    if (!got.s) return 0;
    static const char *const NAMED[5][2] = { {"lt", "<"}, {"gt", ">"}, {"amp", "&"}, {"apos", "'"}, {"quot", "\""} };
    size_t k = 0;
    for (const char *s = got.s; s < got.e && k < cap - 1; ) {
        const char *semi = *s == '&' ? memchr(s, ';', (size_t) (got.e - s)) : NULL;
        if (semi && s[1] == '#') {
            long c = s[2] == 'x' ? strtol(s + 3, NULL, 16) : strtol(s + 2, NULL, 10);
            out[k++] = c > 0 && c < 128 ? (char) c : '?';
            s = semi + 1;
            continue;
        }
        int done = 0;
        for (int j = 0; semi && j < 5 && !done; j++)
            if ((size_t) (semi - s - 1) == strlen(NAMED[j][0]) && !memcmp(s + 1, NAMED[j][0], (size_t) (semi - s - 1))) {
                out[k++] = NAMED[j][1][0];
                s = semi + 1;
                done = 1;
            }
        if (!done) out[k++] = *s++;
    }
    out[k] = 0;
    return 1;
}

/* bionic's atoi: strtol into a 64-bit long, cut to an int */
static long loader_atoi(const char *s) { return (long) (int) strtoll(s, NULL, 10); }

/*
 * A whole table against its loader's fixed arrays: 1 (and why) when a record
 * would be written past one - the file is not handed out. 0 otherwise, and
 * for a file that is no XML (the table rule refuses that on its own).
 */
/* recipes.xml: an input with a byte of 0x80 and up (0x73d9b4 reads it signed and indexes backwards) */
static int recipe_ascii(const xdoc_t *d, char *why, size_t room) {
    int root = x_child(d, -1, "recipes"), count = 0;
    char first[64] = "";
    for (int i = root >= 0 ? x_child(d, root, "recipe") : -1; i >= 0; i = x_next(d, root, "recipe", i)) {
        span_t n, v, got = { NULL, NULL };
        for (const char *p = d->v[i].attrs.s; (p = xml_attr(p, d->v[i].attrs.e, &n, &v)); ) if (span_is(n, "input")) got = v;
        int high = 0;
        for (const char *c = got.s; got.s && c < got.e && !high; c++) high = (unsigned char) *c >= 0x80;
        if (high && !count++) span_copy(got, first, sizeof(first));
    }
    if (!count) return 0;
    size_t k = (size_t) snprintf(why, room, "recipe input \"%s\" has a byte past ASCII", first);
    if (count > 1 && k < room) k += (size_t) snprintf(why + k, room - k, " (%d such recipes)", count);
    if (k < room) snprintf(why + k, room - k, " - the game's loader reads it signed and indexes before its table (0x73d9b4); "
                           "the game's file is used");
    return 1;
}

static int guard_bounds(const char *leaf, const char *buf, long len, char *why, size_t room) {
    int rules = !strcasecmp(leaf, "recipes.xml");
    for (int r = 0; r < TABLE_BOUNDS_N; r++) rules |= !strcasecmp(leaf, TABLE_BOUNDS[r].file);
    if (!rules) return 0;
    xdoc_t d = {0};
    if (xml_parse(buf, buf + len, &d)) { free(d.v); return 0; }
    if (!strcasecmp(leaf, "recipes.xml")) { int bad = recipe_ascii(&d, why, room); free(d.v); return bad; }
    int bad = 0;
    for (int r = 0; r < TABLE_BOUNDS_N && !bad; r++) {
        const tbound_t *b = &TABLE_BOUNDS[r];
        if (strcasecmp(leaf, b->file)) continue;
        int root = x_child(&d, -1, b->root);
        if (root < 0) continue;                         /* the loader stops at its missing root: nothing written */
        int count = 0, have_last = 0;
        long last = 0;
        char tag[48] = "", what[32] = "";
        for (int i = x_child(&d, root, b->first); i >= 0; i = x_next(&d, root, NULL, i)) {
            const xel_t *x = &d.v[i];
            char v[64];
            int has = attr_value(x, "id", b->first_id, v, sizeof(v)), known = 1;
            long id = 0;
            if (has) {
                const char *p = v;
                if (b->prefix == 's' && *p++ != 's') continue;
                if (b->prefix == '-' && *p == 's') continue;
                id = loader_atoi(p);
                last = id;
                have_last = 1;
            } else if (b->noid == NOID_SKIP) continue;
            else if (b->noid == NOID_INHERIT) { id = last; known = have_last; }
            if (b->kinds) {
                char kv[64];
                if (b->by) { if (!attr_value(x, b->by, 0, kv, sizeof(kv))) kv[0] = 0; }
                else span_copy(x->name, kv, sizeof(kv));
                if (!kind_in(kv, strlen(kv), b->kinds)) continue;
            }
            if (b->child && x_child(&d, i, b->child) < 0) continue;     /* nothing of it is written */
            int out = !known || (id > b->hi && !b->above_skipped) || (id < b->lo && !b->below_skipped);
            if (!out) continue;
            if (!count++) {
                span_copy(x->name, tag, sizeof(tag));
                if (known) snprintf(what, sizeof(what), "%ld", id); else snprintf(what, sizeof(what), "(none)");
            }
        }
        if (!count) continue;
        bad = 1;
        size_t n = (size_t) snprintf(why, room, "%s id %s" BOUNDS_MARK "%ld..%ld", tag, what, b->lo, b->hi);
        if (count > 1 && n < room) n += (size_t) snprintf(why + n, room - n, " (%d such records)", count);
        if (n < room)
            snprintf(why + n, room - n, " - the game's loader would go past its table (%s)%s; the game's file is used",
                     b->proof, strcmp(what, "(none)") ? "" : ": the first record has no id, and the index it goes "
                     "through is whatever the stack held");
    }
    free(d.v);
    return bad;
}

/*
 * entities2.xml: the bestiary's parent chain (<entity><bestiary parent="T.V">).
 * The walker 0x782324 looks the entity up (0x7822ac: the key id<<20 |
 * variant<<8 | subtype, 32 bits; not found - subtype 0, then variant 0 too)
 * and follows the record's parent (+0x198 type, +0x19c variant; "T.V" split
 * at '.', atoi each, 0x783928..0x78398c) for as long as the parent type is not
 * 0 - with no count. A chain that comes back to itself never ends, and the
 * game hangs the first time anything walks it (56 callers). The engine's set
 * is the mod's records and the game's ones it lacks (the guard puts them
 * back); a key twice is the last one's (the record is assigned over the
 * map's node, 0x78a048 then 0x784b04 - operator[], not insert).
 */
typedef struct { unsigned key; int order; long pt, pv; } eparent_t;

static int eparent_cmp(const void *a, const void *b) {
    unsigned x = ((const eparent_t *) a)->key, y = ((const eparent_t *) b)->key;
    return x < y ? -1 : x > y;
}

/* by key, and of one key in reading order: the mod's before the game's, each in file order */
static int eparent_order(const void *a, const void *b) {
    int k = eparent_cmp(a, b);
    return k ? k : ((const eparent_t *) a)->order - ((const eparent_t *) b)->order;
}

static unsigned ekey(long t, long v, long s) { return (unsigned) t << 20 | (unsigned) v << 8 | (unsigned) s; }

static const eparent_t *elookup(const eparent_t *e, int n, long t, long v) {
    long s = 0;
    for (;;) {
        eparent_t k = { ekey(t, v, s), 0, 0, 0 };
        const eparent_t *f = (const eparent_t *) bsearch(&k, e, (size_t) n, sizeof(eparent_t), eparent_cmp);
        if (f) return f;
        if (s) s = 0;
        else if (v) v = 0;
        else return NULL;
    }
}

static int entities_of(const char *buf, long len, eparent_t **out, int *n, int *cap) {
    xdoc_t d = {0};
    if (xml_parse(buf, buf + len, &d)) { free(d.v); return -1; }
    int root = x_child(&d, -1, "entities");
    for (int i = root >= 0 ? x_child(&d, root, "entity") : -1; i >= 0; i = x_next(&d, root, NULL, i)) {
        const xel_t *x = &d.v[i];
        char v[64];
        long id = attr_value(x, "id", 0, v, sizeof(v)) ? loader_atoi(v) : 0;
        long var = attr_value(x, "variant", 0, v, sizeof(v)) ? loader_atoi(v) : 0;
        long sub = attr_value(x, "subtype", 0, v, sizeof(v)) ? loader_atoi(v) : 0;
        long pt = 0, pv = 0;
        int best = x_child(&d, i, "bestiary");
        if (best >= 0 && attr_value(&d.v[best], "parent", 0, v, sizeof(v))) {
            pt = loader_atoi(v);
            const char *dot = strchr(v, '.');
            pv = dot ? loader_atoi(dot + 1) : 0;
        }
        if (*n == *cap) {
            int c = *cap ? *cap * 2 : 1024;
            eparent_t *p = (eparent_t *) realloc(*out, sizeof(eparent_t) * (size_t) c);
            if (!p) break;
            *out = p;
            *cap = c;
        }
        (*out)[*n] = (eparent_t) { ekey(id, var, sub), *n, pt, pv };
        (*n)++;
    }
    free(d.v);
    return 0;
}

static int guard_parents(const char *mbuf, long mlen, const char *gbuf, long glen, char *why, size_t room) {
    eparent_t *e = NULL;
    int n = 0, cap = 0;
    if (entities_of(mbuf, mlen, &e, &n, &cap)) { free(e); return 0; }
    int mine = n;
    if (gbuf) entities_of(gbuf, glen, &e, &n, &cap);
    if (n) qsort(e, (size_t) n, sizeof(eparent_t), eparent_order);
    /* of a key: the mod's last record; the game's only for a key the mod has not (the guard adds only those) */
    int u = 0;
    for (int i = 0; i < n; ) {
        int j = i, pick = i;
        for (; j < n && e[j].key == e[i].key; j++) if (e[j].order < mine || e[pick].order >= mine) pick = j;
        e[u++] = e[pick];
        i = j;
    }
    int bad = 0;
    for (int i = 0; i < u && !bad; i++) {
        if (!e[i].pt) continue;
        const eparent_t *at = &e[i];
        int steps = 0;
        while (at && at->pt && steps <= u) { at = elookup(e, u, at->pt, at->pv); steps++; }
        if (at && at->pt) {
            bad = 1;
            snprintf(why, room, "entity %u.%u.%u: its bestiary parent chain comes back to itself - the game's walker "
                     "(0x782324) never ends it and the game hangs; the game's file is used",
                     e[i].key >> 20, e[i].key >> 8 & 0xfff, e[i].key & 0xff);
        }
    }
    free(e);
    return bad;
}

/*
 * content.c's question about a whole table (a mod's file before it is a base,
 * or what the build made of the game's and the mods' files): 1 and why when the
 * game's loader would go past its table or hang on it. The reason without the
 * guard's own ending ("the game's file is used"): content.c says what it does.
 */
int shim_usermods_table_refusal(const char *fname, const char *buf, long len, const char *game, long glen,
                                char *why, size_t room) {
    int bad = guard_bounds(fname, buf, len, why, room)
           || (!strcasecmp(fname, "entities2.xml") && guard_parents(buf, len, game, game ? glen : 0, why, room));
    if (!bad) return 0;
    static const char *const TAIL[] = { "; the game's file is used", " - the game's file is used" };
    for (int i = 0; i < 2; i++) {
        size_t n = strlen(why), k = strlen(TAIL[i]);
        if (n > k && !strcmp(why + n - k, TAIL[i])) why[n - k] = 0;
    }
    return 1;
}

/* ---- PNG as libpng 1.0.2 reads it (modcheck/png102.py, pngrutil.c/pngread.c of the 1.0.x line) */
typedef struct { int ihdr, plte, idat; unsigned w, h; int bd, ct, il; } pngst_t;

static int png_channels(int ct) { return ct == 0 ? 1 : ct == 2 ? 3 : ct == 3 ? 1 : ct == 4 ? 2 : ct == 6 ? 4 : 0; }

static int be32(const unsigned char *p) { return (int) ((unsigned) p[0] << 24 | (unsigned) p[1] << 16 | (unsigned) p[2] << 8 | p[3]); }

/* one chunk against the reader's rules: NULL, or the png_error it would end in */
static const char *png_rule(pngst_t *s, const unsigned char *type, unsigned len, const unsigned char *ihdr13) {
    static char msg[64];
    int crit = !(type[0] & 0x20);
    if (!memcmp(type, "IHDR", 4)) {
        if (s->ihdr) return "Out of place IHDR";
        if (len != 13 || !ihdr13) return "Invalid IHDR chunk";
        s->w = (unsigned) be32(ihdr13);
        s->h = (unsigned) be32(ihdr13 + 4);
        s->bd = ihdr13[8];
        s->ct = ihdr13[9];
        s->il = ihdr13[12];
        if (!s->w || !s->h || s->w > 0x7fffffffu || s->h > 0x7fffffffu) return "Invalid image size";
        if (s->bd != 1 && s->bd != 2 && s->bd != 4 && s->bd != 8 && s->bd != 16) return "Invalid bit depth";
        if (!png_channels(s->ct) && s->ct != 0) return "Invalid color type";
        if ((s->ct == 3 && s->bd > 8) || ((s->ct == 2 || s->ct == 4 || s->ct == 6) && s->bd < 8))
            return "Invalid color type/bit depth combination";
        if (s->il > 1) return "Unknown interlace method";
        if (ihdr13[10]) return "Unknown compression method";
        if (ihdr13[11]) return "Unknown filter method";
        s->ihdr = 1;
        return NULL;
    }
    if (!s->ihdr) { snprintf(msg, sizeof(msg), "Missing IHDR before %.4s", (const char *) type); return msg; }
    if (!memcmp(type, "PLTE", 4)) {
        if (s->plte) return "Duplicate PLTE chunk";
        if (s->idat) return "Invalid PLTE after IDAT";
        if (len % 3 && s->ct == 3) return "Invalid palette chunk";
        s->plte = 1;
        return NULL;
    }
    if (!memcmp(type, "IDAT", 4)) {
        if (s->ct == 3 && !s->plte) return "Missing PLTE before IDAT";
        if (s->idat == 2 && len) return "Too many IDAT's found";
        if (!s->idat) s->idat = 1;
        return NULL;
    }
    if (s->idat == 1) s->idat = 2;
    if (!memcmp(type, "IEND", 4)) return NULL;
    if (crit) { snprintf(msg, sizeof(msg), "unknown critical chunk %.4s", (const char *) type); return msg; }
    return NULL;
}

/* the structure, by the chunks' headers alone: 0 ok (or not a PNG: the game refuses that itself), 1 png_error */
static int png_structure(const char *path, long long size, char *why, size_t room) {
    FILE *f = fopen(path, "rb");
    if (!f) return 0;                               /* not readable now: the game will see the same, on its own path */
    unsigned char h[13];
    int bad = 0;
    pngst_t s;
    memset(&s, 0, sizeof(s));
    if (fread(h, 1, 8, f) != 8 || memcmp(h, "\x89PNG\r\n\x1a\n", 8)) { fclose(f); return 0; }
    long long at = 8;
    for (;;) {
        unsigned char hd[8];
        if (at + 8 > size || fseek(f, (long) at, SEEK_SET) || fread(hd, 1, 8, f) != 8) {
            snprintf(why, room, "Read Error (the file ends before IEND)");
            bad = 1;
            break;
        }
        unsigned len = (unsigned) be32(hd);
        if (len > 0x7fffffffu || at + 12 + (long long) len > size) {
            snprintf(why, room, "Read Error (the file ends inside the %.4s chunk)", (const char *) hd + 4);
            bad = 1;
            break;
        }
        int is_ihdr = !memcmp(hd + 4, "IHDR", 4);
        const unsigned char *ih = NULL;
        if (is_ihdr && len == 13 && fread(h, 1, 13, f) == 13) ih = h;
        const char *e = png_rule(&s, hd + 4, len, ih);
        if (e) { snprintf(why, room, "%s", e); bad = 1; break; }
        at += 12 + (long long) len;
        if (!memcmp(hd + 4, "IEND", 4)) break;
    }
    if (!bad && !s.idat) { snprintf(why, room, "no IDAT"); bad = 1; }
    fclose(f);
    return bad;
}

/* zlib, from the system (libz.so is part of every Android): only to read a mod's picture through once */
typedef int (*z_init_t)(z_streamp, const char *, int);
typedef int (*z_run_t)(z_streamp, int);
typedef int (*z_end_t)(z_streamp);
static struct { int tried; z_init_t init; z_run_t run; z_end_t end; } g_z;

static int z_ready(void) {
    if (!g_z.tried) {
        g_z.tried = 1;
        void *h = dlopen("libz.so", RTLD_NOW);
        if (h) {
            g_z.init = (z_init_t) dlsym(h, "inflateInit_");
            g_z.run = (z_run_t) dlsym(h, "inflate");
            g_z.end = (z_end_t) dlsym(h, "inflateEnd");
        }
    }
    return g_z.init && g_z.run && g_z.end;
}

static unsigned g_crc_table[256];
static unsigned crc32_of(unsigned crc, const unsigned char *p, size_t n) {
    if (!g_crc_table[1])
        for (unsigned i = 0; i < 256; i++) {
            unsigned c = i;
            for (int k = 0; k < 8; k++) c = c & 1 ? 0xedb88320u ^ (c >> 1) : c >> 1;
            g_crc_table[i] = c;
        }
    crc = ~crc;
    while (n--) crc = g_crc_table[(crc ^ *p++) & 0xff] ^ (crc >> 8);
    return ~crc;
}

/* the whole file as the reader goes through it: CRCs of the critical chunks, and the image data unpacked */
static int png_full(const char *path, char *why, size_t room) {
    long n = 0;
    char *buf = slurp(path, &n);
    if (!buf) return 0;
    const unsigned char *b = (const unsigned char *) buf;
    int bad = 0;
    pngst_t s;
    memset(&s, 0, sizeof(s));
    if (n < 8 || memcmp(b, "\x89PNG\r\n\x1a\n", 8)) { free(buf); return 0; }
    sb_t idat = {0};
    long at = 8;
    for (;;) {
        if (at + 8 > n) { snprintf(why, room, "Read Error (the file ends before IEND)"); bad = 1; break; }
        unsigned len = (unsigned) be32(b + at);
        if (len > 0x7fffffffu || at + 12 + (long) len > n) {
            snprintf(why, room, "Read Error (the file ends inside the %.4s chunk)", (const char *) b + at + 4);
            bad = 1;
            break;
        }
        const unsigned char *type = b + at + 4, *data = b + at + 8;
        if (!(type[0] & 0x20) && crc32_of(0, type, 4 + (size_t) len) != (unsigned) be32(data + len)) {
            snprintf(why, room, "CRC error in the critical chunk %.4s", (const char *) type);
            bad = 1;
            break;
        }
        const char *e = png_rule(&s, type, len, len == 13 ? data : NULL);
        if (e) { snprintf(why, room, "%s", e); bad = 1; break; }
        if (!memcmp(type, "IDAT", 4)) sb_put(&idat, (const char *) data, len);
        at += 12 + (long) len;
        if (!memcmp(type, "IEND", 4)) break;
    }
    if (!bad && !s.idat) { snprintf(why, room, "no IDAT"); bad = 1; }
    if (!bad && z_ready()) {
        /* the bytes the rows need: a filter byte and the row, per row (per pass when interlaced) */
        long long bpp = (long long) png_channels(s.ct) * s.bd, need = 0;
        #define ROWB(width) (((long long) (width) * bpp + 7) / 8)
        if (s.il) {
            static const int P[7][4] = { {0,0,8,8}, {4,0,8,8}, {0,4,4,8}, {2,0,4,4}, {0,2,2,4}, {1,0,2,2}, {0,1,1,2} };
            for (int k = 0; k < 7; k++) {
                long long pw = s.w > (unsigned) P[k][0] ? (s.w - P[k][0] + P[k][2] - 1) / P[k][2] : 0;
                long long ph = s.h > (unsigned) P[k][1] ? (s.h - P[k][1] + P[k][3] - 1) / P[k][3] : 0;
                if (pw && ph) need += ph * (1 + ROWB(pw));
            }
        } else {
            need = (long long) s.h * (1 + ROWB(s.w));
        }
        #undef ROWB
        z_stream z;
        memset(&z, 0, sizeof(z));
        z.next_in = (Bytef *) (idat.s ? idat.s : "");
        z.avail_in = (uInt) idat.n;
        static unsigned char sink[1 << 16];
        long long got = 0;
        int ret = g_z.init(&z, ZLIB_VERSION, (int) sizeof(z_stream));
        if (ret == Z_OK) {
            for (;;) {
                z.next_out = sink;
                z.avail_out = sizeof(sink);
                ret = g_z.run(&z, Z_NO_FLUSH);
                got += (long long) (sizeof(sink) - z.avail_out);
                if (ret != Z_OK || got > need) break;
            }
            size_t left = z.avail_in;
            g_z.end(&z);
            if (ret == Z_DATA_ERROR || ret == Z_STREAM_ERROR || ret == Z_NEED_DICT || ret == Z_MEM_ERROR)
                { snprintf(why, room, "Decompression error"); bad = 1; }
            else if (got > need) { snprintf(why, room, "Extra compressed data (more pixels than the image)"); bad = 1; }
            else if (got < need) { snprintf(why, room, "Not enough image data (%lld of %lld bytes)", got, need); bad = 1; }
            else if (ret != Z_STREAM_END) { snprintf(why, room, "Not enough image data (the compressed stream does not end)"); bad = 1; }
            else if (left) { snprintf(why, room, "Extra compressed data (%u bytes after the stream)", (unsigned) left); bad = 1; }
        }
    }
    free(idat.s);
    free(buf);
    return bad;
}

/* ---- the cache of answers, and the run */
typedef struct gcache {
    struct gcache *next;
    char verdict;                           /* o ok, p png whose full check waits, r refused, f completed */
    long long size, mtime;
    long gamelen, fixlen;
    char *why;
    char key[1];                            /* "<mod>\x01<index key>" */
} gcache_t;
static gcache_t *g_gcache[BUCKETS];
static char g_guard_dir[700];               /* <files>/content-cache/guard */
static pthread_once_t g_guard_once = PTHREAD_ONCE_INIT;
static pthread_mutex_t g_guard_lock = PTHREAD_MUTEX_INITIALIZER;
static volatile int g_guard_ran;
static int g_guard_fixed[MODS_MAX], g_guard_game[MODS_MAX];
static int g_guard_size[MODS_MAX];          /* of the game's: pictures of the wrong size (the converter's work) */
static int g_guard_bounds[MODS_MAX];        /* of the game's: tables the loader would write past (M4) */
static const ufile_t *g_guard_bound_file[MODS_MAX];   /* the first of them, for the page */
/*
 * What the modder does about a picture of the wrong size (the owner, 02.10.2026):
 * the game does not scale it at run time - the converter (tools/modport.py, the
 * web converter) bakes it down, or the modder does by hand; the guide says how.
 */
#define SIZE_HINT "Run the mod through the converter (modport) or scale the picture down by hand"

static void gcache_put(const char *owner, const char *key, char verdict, long long size, long long mtime,
                       long gamelen, long fixlen, const char *why) {
    char full[1400];
    snprintf(full, sizeof(full), "%s\x01%s", owner, key);
    unsigned k = hash_of(full) % BUCKETS;
    gcache_t *c = NULL;
    for (gcache_t *e = g_gcache[k]; e; e = e->next) if (!strcmp(e->key, full)) { c = e; break; }
    if (!c) {
        size_t n = strlen(full);
        c = (gcache_t *) calloc(1, sizeof(gcache_t) + n);
        if (!c) return;
        memcpy(c->key, full, n + 1);
        c->next = g_gcache[k];
        g_gcache[k] = c;
    }
    free(c->why);
    c->why = why && why[0] ? strdup(why) : NULL;
    c->verdict = verdict;
    c->size = size;
    c->mtime = mtime;
    c->gamelen = gamelen;
    c->fixlen = fixlen;
}

static const gcache_t *gcache_get(const char *owner, const char *key) {
    char full[1400];
    snprintf(full, sizeof(full), "%s\x01%s", owner, key);
    for (gcache_t *e = g_gcache[hash_of(full) % BUCKETS]; e; e = e->next) if (!strcmp(e->key, full)) return e;
    return NULL;
}

static void gcache_load(const char *owner) {
    char path[900];
    snprintf(path, sizeof(path), "%s/%s.txt", g_guard_dir, owner);
    FILE *f = fopen(path, "r");
    if (!f) return;
    char line[2600];
    int rules = 0;
    /* "guard <rules> <the settings' layers in use, - none>": a cache made with other layers is not used */
    char layers[LAYERS_SIG] = "-";
    int o = 0;
    while (o < g_owners && strcmp(g_owner[o], owner)) o++;
    const char *now = o < g_owners && g_owner_layers[o] && g_owner_layers[o][0] ? g_owner_layers[o] : "-";
    if (fgets(line, sizeof(line), f)) sscanf(line, "guard %d %699s", &rules, layers);
    if (strcmp(layers, now)) rules = 0;
    while (rules == GUARD_RULES && fgets(line, sizeof(line), f)) {
        line[strcspn(line, "\r\n")] = 0;
        /* v size mtime gamelen fixlen key why - the last field takes the rest */
        char *t[7];
        int n = 0;
        char *p = line;
        while (n < 7) {
            t[n++] = p;
            char *tab = n < 7 ? strchr(p, '\t') : NULL;
            if (!tab) break;
            *tab = 0;
            p = tab + 1;
        }
        if (n < 6) continue;
        gcache_put(owner, t[5], t[0][0], atoll(t[1]), atoll(t[2]), atol(t[3]), atol(t[4]), n > 6 ? t[6] : "");
    }
    fclose(f);
}

static void gcache_line(FILE *f, const ufile_t *e) {
    char v = e->guard == GUARD_OK ? 'o' : e->guard == GUARD_PNG_LATER ? 'p' : e->guard == GUARD_REFUSED ? 'r'
           : e->guard == GUARD_FIXED ? 'f' : 0;
    if (!v) return;
    fprintf(f, "%c\t%lld\t%lld\t%ld\t%ld\t%s\t%s\n", v, e->size, e->mtime, e->gamelen, e->fixlen, e->key, e->why ? e->why : "");
}

static void mkdirs(const char *path) {
    char p[1200];
    snprintf(p, sizeof(p), "%s", path);
    for (char *s = p + 1; *s; s++) if (*s == '/') { *s = 0; mkdir(p, 0700); *s = '/'; }
    mkdir(p, 0700);
}

/* the game's own file a key stands for: repentance-resources/<set or data>/<path> */
static const void *game_file(const char *key, long *len) {
    const char *bar = strchr(key, '|');
    if (!bar) return NULL;
    char inner[1400];
    const void *d = NULL;
    if (bar > key) {
        snprintf(inner, sizeof(inner), "repentance-resources/%.*s/%s", (int) (bar - key), key, bar + 1);
        d = shim_pack_peek(inner, len);
    }
    if (!d) {
        snprintf(inner, sizeof(inner), "repentance-resources/data/%s", bar + 1);
        d = shim_pack_peek(inner, len);
    }
    return d;
}

static const char *owner_id(const ufile_t *e) { return e->owner < MODS_MAX ? g_owner[e->owner] : "?"; }

/* where a completed copy lives: content-cache/guard/<mod>/<set, or _ for all>/<path> */
static void fixed_path(const ufile_t *e, char *out, size_t room) {
    const char *bar = strchr(e->key, '|');
    snprintf(out, room, "%s/%s/%.*s%s/%s", g_guard_dir, owner_id(e), (int) (bar - e->key), e->key,
             bar > e->key ? "" : "_", bar + 1);
}

static void set_why(ufile_t *e, const char *why) {
    free(e->why);
    e->why = why && why[0] ? strdup(why) : NULL;
}

static int content_owns(const ufile_t *e);

/* one file of the index: checked, or its answer taken from the cache */
static int guard_file(ufile_t *e, int *from_cache) {
    const char *path = strchr(e->key, '|') + 1;
    size_t n = strlen(path);
    int anm = n > 5 && !strcmp(path + n - 5, ".anm2"), png = n > 4 && !strcmp(path + n - 4, ".png");
    int table = n > 4 && !strcmp(path + n - 4, ".xml") && !strchr(path, '/');
    /*
     * A ROOMS FILE (.stb) WITHOUT ITS MARK (hotfix 1.71, measured by the converter's round 2):
     * Repentance's loader 0x750cf0 reads the first four bytes and compares them with "STB1"
     * (0x750eb0: 0x31425453); anything else and it loads no room of the file, the lookup 0x7529f4
     * answers NULL and its caller at 0x6cec78 reads [x0,#0x5c] unchecked - the game falls. A file
     * of the older games' format (no mark) is such a file. Only the mark is checked: another
     * count of rooms is the mod's choice.
     */
    if (n > 4 && !strcmp(path + n - 4, ".stb")) {
        unsigned char mark[4] = { 0 };
        FILE *f = fopen(e->src, "rb");
        size_t got = f ? fread(mark, 1, sizeof(mark), f) : 0;
        if (f) fclose(f);
        if (got == 4 && !memcmp(mark, "STB1", 4)) { e->guard = GUARD_OK; return 0; }
        if (!f) { e->guard = GUARD_OK; return 0; }        /* not readable now: nothing to decide on */
        long glen = -1;
        const void *g = game_file(e->key, &glen);
        e->guard = GUARD_REFUSED;
        set_why(e, g ? "a rooms file without the STB1 mark (an older game's format): Repentance's loader reads no room "
                       "from it and the game falls - the game's file is used"
                     : "a rooms file without the STB1 mark (an older game's format): Repentance's loader reads no room "
                       "from it and the game falls - left out");
        return 1;
    }
    if (!anm && !png && !table) { e->guard = GUARD_OK; return 0; }
    long glen = -1;
    const void *g = game_file(e->key, &glen);
    if (!g) glen = -1;
    e->gamelen = glen;
    const gcache_t *c = gcache_get(owner_id(e), e->key);
    if (c && c->size == e->size && c->mtime == e->mtime && c->gamelen == glen) {
        int ok = 1;
        if (c->verdict == 'f') {
            char fixed[1400];
            struct stat st;
            fixed_path(e, fixed, sizeof(fixed));
            ok = !stat(fixed, &st) && (long) st.st_size == c->fixlen;
            if (ok) { e->path = strdup(fixed); e->fixlen = c->fixlen; }
        }
        if (ok) {
            e->guard = c->verdict == 'o' ? GUARD_OK : c->verdict == 'p' ? GUARD_PNG_LATER
                     : c->verdict == 'r' ? GUARD_REFUSED : GUARD_FIXED;
            set_why(e, c->why);
            *from_cache = 1;
            return 1;
        }
    }
    char why[600] = "";
    if (png) {
        e->guard = png_structure(e->src, e->size, why, sizeof(why)) ? GUARD_REFUSED : GUARD_PNG_LATER;
        if (e->guard == GUARD_REFUSED) {
            size_t k = strlen(why);
            snprintf(why + k, sizeof(why) - k, " - libpng 1.0.2 would stop on it; %s", g ? "the game's picture is used" : "left out");
        }
        set_why(e, why);
        return 1;
    }
    long mlen = 0;
    char *mbuf = slurp(e->src, &mlen);
    if (!mbuf) { e->guard = GUARD_OK; return 1; }     /* not readable now: nothing to decide on */
    char *out = NULL;
    long outn = 0;
    /* a table the loader would go past, or hang on (M4): refused before anything is completed in it - not a table
       content.c builds: what the game gets of it is the merged table, and content.c checks the bounds of that */
    int r = table && !content_owns(e) && guard_bounds(path, mbuf, mlen, why, sizeof(why)) ? -1
          : table && !strcasecmp(path, "entities2.xml") && guard_parents(mbuf, mlen, (const char *) g, glen, why, sizeof(why)) ? -1
          : anm ? guard_anm2(path, mbuf, mlen, (const char *) g, glen, &out, &outn, why, sizeof(why))
          : g ? guard_table(mbuf, mlen, (const char *) g, glen, &out, &outn, why, sizeof(why)) : 0;
    free(mbuf);
    if (r < 0) e->guard = GUARD_REFUSED;
    else if (r > 0 && out) {
        char fixed[1400];
        fixed_path(e, fixed, sizeof(fixed));
        char *slash = strrchr(fixed, '/');
        *slash = 0;
        mkdirs(fixed);
        *slash = '/';
        FILE *f = fopen(fixed, "wb");
        int ok = f && fwrite(out, 1, (size_t) outn, f) == (size_t) outn;
        if (f && fclose(f)) ok = 0;
        if (ok) { e->path = strdup(fixed); e->fixlen = outn; e->guard = GUARD_FIXED; }
        else { e->guard = GUARD_OK; snprintf(why, sizeof(why), "a completed copy could not be written (%s) - the mod's file as it is", strerror(errno)); }
    } else e->guard = GUARD_OK;
    free(out);
    set_why(e, e->guard == GUARD_OK ? NULL : why);
    return 1;
}

/* a whole table of repentance-resources/data/: content.c answers for it - what it completes, its section tells */
static int content_owns(const ufile_t *e) {
    if (e->key[0] != '|' || strchr(e->key + 1, '/')) return 0;
    return content_known(e->key + 1);
}

static void check_note_path(char *out, size_t room, const char *id) { snprintf(out, room, "%s/%s.check.txt", g_dir, id); }

/* <mods>/<id>.check.txt: what the guard did to the mod's files, written when it changes */
static void check_write(int owner, const sb_t *text) {
    char path[800];
    check_note_path(path, sizeof(path), g_owner[owner]);
    if (!text->n) { unlink(path); return; }
    long have_n = 0;
    char *have = slurp(path, &have_n);
    int same = have && have_n == (long) text->n && !memcmp(have, text->s, text->n);
    free(have);
    if (same) return;
    FILE *f = fopen(path, "w");
    if (!f) return;
    fwrite(text->s, 1, text->n, f);
    fclose(f);
}

/* what the port read of the mod's mod.json, the head of its check.txt */
static void manifest_section(sb_t *b, const char *issues, int off) {
    if (!issues || !issues[0]) return;
    sb_puts(b, "mod.json - what the port read of it (each line: broken, refused or note, [the reason's code], what):\n");
    sb_puts(b, issues);
    if (off) sb_puts(b, "(the mod is off: its files were not checked in this run)\n");
    sb_puts(b, "\n");
}

static void check_line(sb_t *b, const ufile_t *e) {
    const char *bar = strchr(e->key, '|');
    if (!b->n)
        sb_puts(b, "The port's check of this mod's files (what the engine here cannot take is not handed to it as it is).\n"
                   "Each line: the file, then what the port does with it and why.\n\n");
    if (bar > e->key) sb_printf(b, "[%.*s] ", (int) (bar - e->key), e->key);
    sb_printf(b, "%s: %s - %s\n", bar + 1, e->guard == GUARD_FIXED ? "COMPLETED" : "NOT USED", e->why ? e->why : "");
}

/* a PNG's width and height from its header (the IHDR is always first), or 0 */
static int png_size(const unsigned char *h, size_t n, unsigned *w, unsigned *hh) {
    if (n < 24 || memcmp(h, "\x89PNG\r\n\x1a\n", 8) || memcmp(h + 12, "IHDR", 4)) return 0;
    *w = (unsigned) be32(h + 16);
    *hh = (unsigned) be32(h + 20);
    return 1;
}

/*
 * A MOD'S MENU FILE, ALL OR NOTHING (02.10.2026, the owner's concern for the
 * mods people already made). The port lays its additions - the MODS row, the
 * TWEAKS and GUIDE straps, its Options rows, the pause strap, the stick sizes -
 * over gamemenu, optionsmenu, pausescreen and ios_buttons .anm2 by name, a
 * mod's own copy included (port_patched below). Where the patch cannot place
 * every one of them on the mod's file (a layer, null or animation it hangs on
 * is missing or renamed, or the game's own numbers moved), the port used to
 * leave that addition out - and the player lost a piece of the port's menu to
 * a mod that only meant to recolour it. Now such a file is not used at all:
 * the game's own, with the port's patch, is served in its place (as when the
 * mod brings none), and the reason goes into <id>.check.txt and onto the MODS
 * page with the other files the game's copy stands in for.
 */
int shim_port_patch_file(const char *inner, const void *in, long n, void **out, long *outn, char *why, size_t room)
    __attribute__((weak));
int shim_port_pixels_file(const char *inner, const void *in, long n, void **out, long *outn, char *why, size_t room)
    __attribute__((weak));
const char *shim_port_patch_target(int i) __attribute__((weak));
static const ufile_t *usermods_find(const char *p, int quiet);

static void guard_port_patched(void) {
    if (!shim_port_patch_file || !shim_port_patch_target) return;
    const char *root = shim_pack_root();
    for (int i = 0; ; i++) {
        const char *inner = shim_port_patch_target(i);
        if (!inner) break;
        char full[1200];
        snprintf(full, sizeof(full), "%s%s", root, inner);       /* as portpatch.c builds it: the root ends in '/' */
        /* the answer for this path, again after each refusal: a mod's language copy, its copy for all sets, another mod's */
        for (int k = 0; k < 2 * MODS_MAX + 2; k++) {
            ufile_t *e = (ufile_t *) usermods_find(full, 1);
            if (!e || e->owner >= MODS_MAX) break;
            long len = 0;
            char *buf = slurp(e->path, &len);
            if (!buf) break;
            void *out = NULL;
            long outn = 0;
            char why[400] = "";
            int r = shim_port_patch_file(inner, buf, len, &out, &outn, why, sizeof(why));
            free(buf);
            free(out);
            if (r >= 0) break;
            char text[700];
            snprintf(text, sizeof(text), "menu file not used: %s - the game's file with the port's menu is used instead",
                     r == -1 ? "it does not read as an .anm2" : why);
            e->guard = GUARD_REFUSED;
            set_why(e, text);
            __android_log_print(ANDROID_LOG_WARN, "isaac", "usermods: %s %s: %s", owner_id(e), e->key, text);
            NOTE("guard %s: %s - %s", owner_id(e), inner, text);
        }
    }
}

/*
 * THE COMPLETION WIDGET OF A MOD (plan 30.09, M9). The port's Completion Mark
 * Reminder (TWEAKS, mods.c) swaps the widget's two sheets for its own
 * gfx/port_completion_widget(_pause).png, drawn for the GAME's widget. A mod
 * whose completion_widget.anm2 wins cuts another layout: the reminder then
 * leaves the widget alone - unless the mod brings its own copies of those two
 * port sheets, cut for its anm2 (a mod may stand in for any file of the
 * port's, usermods_find). Which it is, is told in <id>.check.txt and asked by
 * the MODS page (shim_usermods_widget_alone).
 */
#define WIDGET_NOTE "gfx/ui/completion_widget.anm2: the mod's own - Completion Mark Reminder (TWEAKS) leaves its widget " \
                    "alone: the reminder's marked sheets are cut for the game's widget. To have it, the mod brings " \
                    "resources/gfx/port_completion_widget.png and port_completion_widget_pause.png cut for its anm2."
static int g_widget_alone[MODS_MAX];

/* mod o's completion_widget.anm2 is the one the game loads, and o has not brought the port's two sheets for it */
static int widget_alone(int o) {
    const ufile_t *w = find_entry("", "gfx/ui/completion_widget.anm2");
    if (!w || w->owner != o) return 0;
    const ufile_t *a = find_entry("", "gfx/port_completion_widget.png");
    const ufile_t *b = find_entry("", "gfx/port_completion_widget_pause.png");
    return !(a && a->owner == o && b && b->owner == o);
}

/*
 * THE MODS OVER EACH OTHER (the owner, 04.10.2026: "only precise, never
 * vague"). Two notes, nothing else, on both mods' pages and in both check.txt:
 *
 *   (a) what is so: mod A stands over N files of mod B - THE ORDER gives the
 *       game A's file where both bring one (by kind: pictures, animations...);
 *   (b) what breaks: the .anm2 the game gets is mod A's, and a sheet it cuts
 *       comes from mod B or from the game, of a size the frames were not cut
 *       for - A's own copy of that sheet (or the game's, when A brings none) is
 *       another size, and the frames land elsewhere on the one served: the
 *       same picture scaled (Sam & Max's 1x charactermenu.png under Reflashed's
 *       x4 charactermenu.anm2), or frames falling off its edge further than
 *       they did on the sheet they were cut for. A sheet that is only bigger
 *       with the frames' places the same (more rows below) is no breakage.
 *
 * Both are worked out once, after the guard, over every mod file of the
 * stacked set: the index (the stronger mod's file per key) and the shadows
 * (the weaker ones'), for the folders for all sets and for each language a
 * mod brings a folder of (THE ORDER is per language). The data tables content.c
 * builds are its own (merged, with its own conflict lines) and are not counted.
 */
#define OVER_MAX   4096
#define BROKEN_MAX 64

enum { K_PIC = 1, K_ANM = 2, K_SOUND = 4, K_MUSIC = 8, K_TABLE = 16, K_FONT = 32, K_VIDEO = 64, K_OTHER = 128 };

typedef struct mfile {
    const char *key, *path, *src;           /* path: after the '|' */
    const char *served;                     /* what the game is handed for it: the guard's completed copy, or src;
                                               NULL: a shadow of this run's stack - never served */
    int refused;                            /* the guard refused it: no answer, the next one is */
    int owner, rank;
    char lang[16];                          /* the key's language, "" for all sets */
} mfile_t;

typedef struct { owner_t over, under; char lang[16]; const char *path; int win; } over_rec_t;   /* win: v[] */
typedef struct {
    owner_t over, under;                    /* owners */
    int files, lang_only, refused;          /* refused: files where the stronger one's copy is not used either */
    unsigned kinds;
    char lang[16];                          /* the language of the language-only ones (the first) */
} over_t;
typedef struct {
    owner_t anm_owner;
    int sheet_owner;                       /* an owner, or -1: the game's sheet */
    char anm[200], sheet[200], lang[16];    /* the first .anm2; lang: "" when so in all sets */
    unsigned iw, ih, sw, sh;                /* the size the frames were cut for; the size served */
    int anms, own;                          /* how many .anm2 of the mod cut it; the size cut for: 0 the game's, 1 its own
                                               copy a stronger mod's stands over, 2 its own copy the guard refused */
    sb_t files;                             /* those .anm2, for check.txt */
} broken_t;

static over_t g_over[OVER_MAX / 16];
static int g_nover;
static broken_t g_breaks[BROKEN_MAX];
static int g_nbreaks;
static sb_t g_conflict_text[MODS_MAX];      /* each mod's section of its check.txt */

static unsigned kind_of(const char *path) {
    size_t n = strlen(path);
    const char *dot = strrchr(path, '.');
    if (!dot || strchr(dot, '/')) return K_OTHER;
    if (!strcasecmp(dot, ".png")) return K_PIC;
    if (!strcasecmp(dot, ".anm2")) return K_ANM;
    if (!strcasecmp(dot, ".wav") || !strcasecmp(dot, ".ogg")) return !strncasecmp(path, "music/", 6) ? K_MUSIC : K_SOUND;
    if (!strcasecmp(dot, ".xml")) return K_TABLE;
    if (!strcasecmp(dot, ".fnt")) return K_FONT;
    if (!strcasecmp(dot, ".ogv")) return K_VIDEO;
    (void) n;
    return K_OTHER;
}

static int mfile_cmp(const void *a, const void *b) {
    const mfile_t *x = (const mfile_t *) a, *y = (const mfile_t *) b;
    int c = strcmp(x->path, y->path);
    if (c) return c;
    if (x->owner != y->owner) return x->owner - y->owner;
    return y->rank - x->rank;
}

static int over_rec_cmp(const void *a, const void *b) {
    const over_rec_t *x = (const over_rec_t *) a, *y = (const over_rec_t *) b;
    if (x->over != y->over) return x->over - y->over;
    if (x->under != y->under) return x->under - y->under;
    int c = strcmp(x->path, y->path);
    if (c) return c;
    return strcmp(x->lang, y->lang);                /* "" (all sets) first */
}

/* does a file of this key's language take part in THE ORDER for lang ("" - the folders for all sets alone) */
static int in_lang(const mfile_t *m, const char *lang) { return !m->lang[0] || (lang[0] && !strcmp(m->lang, lang)); }

/* the first of [lo, hi) in THE ORDER for lang: served - a file the game can get (no shadow, not refused) */
static const mfile_t *group_first(const mfile_t *v, int lo, int hi, const char *lang, int served) {
    const mfile_t *best = NULL;
    for (int i = lo; i < hi; i++) {
        const mfile_t *m = &v[i];
        if (!in_lang(m, lang) || (served && (!m->served || m->refused))) continue;
        if (!best || order_before(m->owner, m->rank, best->owner, best->rank)) best = m;
    }
    return best;
}

/* a picture's size from its header on disk, or the game's for this language's key; 0 when unknown */
static int pic_size_file(const char *src, unsigned *w, unsigned *h) {
    unsigned char hd[24];
    FILE *f = fopen(src, "rb");
    size_t n = f ? fread(hd, 1, sizeof(hd), f) : 0;
    if (f) fclose(f);
    return png_size(hd, n, w, h);
}
static int pic_size_game(const char *lang, const char *rel, unsigned *w, unsigned *h) {
    char key[700];
    snprintf(key, sizeof(key), "%s%s|%s", lang[0] ? "dlc3." : "", lang, rel);
    long n = 0;
    const unsigned char *g = (const unsigned char *) game_file(key, &n);
    return g && png_size(g, (size_t) n, w, h);
}

/* "<dir of anm>/<sheet path>" with "." and ".." taken out, '\' as '/', lower case; 0 when it climbs out */
static int sheet_rel(const char *anm, span_t p, char *out, size_t room) {
    char segs[48][96];
    int n = 0;
    const char *s = anm, *slash;
    while ((slash = strchr(s, '/')) && n < 48) {
        size_t k = (size_t) (slash - s);
        if (k && k < 96) { memcpy(segs[n], s, k); segs[n][k] = 0; n++; }
        s = slash + 1;
    }
    for (const char *a = p.s; a < p.e; ) {
        const char *e = a;
        while (e < p.e && *e != '/' && *e != '\\') e++;
        size_t k = (size_t) (e - a);
        if (k == 2 && a[0] == '.' && a[1] == '.') { if (!n) return 0; n--; }
        else if (k && !(k == 1 && a[0] == '.')) {
            if (k >= 96 || n >= 48) return 0;
            memcpy(segs[n], a, k);
            segs[n][k] = 0;
            n++;
        }
        a = e < p.e ? e + 1 : e;
    }
    size_t at = 0;
    out[0] = 0;
    for (int i = 0; i < n && at < room; i++) at += (size_t) snprintf(out + at, room - at, "%s%s", segs[i], i + 1 < n ? "/" : "");
    if (!n || at >= room) return 0;
    lower(out);
    return 1;
}

/* the reach of an .anm2's visible frames on one sheet: the right and bottom edges of its crops; 0 when none */
static int anm_reach(const anm_t *a, long sheet_id, long *rx, long *ry) {
    *rx = *ry = 0;
    int any = 0;
    if (a->layers < 0 || a->anims < 0) return 0;
    long ids[256];
    int nl = 0;
    for (int l = x_child(&a->d, a->layers, "Layer"); l >= 0 && nl < 256; l = x_next(&a->d, a->layers, "Layer", l))
        if (xml_num(&a->d.v[l], "SpritesheetId", -9999) == sheet_id) ids[nl++] = xml_num(&a->d.v[l], "Id", -9999);
    if (!nl) return 0;
    for (int k = x_child(&a->d, a->anims, "Animation"); k >= 0; k = x_next(&a->d, a->anims, "Animation", k)) {
        int list = x_child(&a->d, k, "LayerAnimations");
        for (int t = list >= 0 ? x_child(&a->d, list, "LayerAnimation") : -1; t >= 0;
             t = x_next(&a->d, list, "LayerAnimation", t)) {
            long lid = xml_num(&a->d.v[t], "LayerId", -9999);
            int mine = 0;
            for (int i = 0; i < nl && !mine; i++) mine = ids[i] == lid;
            span_t vis = x_attr(&a->d.v[t], "Visible");
            if (!mine || (vis.s && span_is(vis, "false"))) continue;
            for (int f = x_child(&a->d, t, "Frame"); f >= 0; f = x_next(&a->d, t, "Frame", f)) {
                const xel_t *x = &a->d.v[f];
                span_t fv = x_attr(x, "Visible");
                if (fv.s && span_is(fv, "false")) continue;
                long w = xml_num(x, "Width", 0), h = xml_num(x, "Height", 0);
                if (w <= 0 || h <= 0) continue;
                long r = xml_num(x, "XCrop", 0) + w, b = xml_num(x, "YCrop", 0) + h;
                if (r > *rx) *rx = r;
                if (b > *ry) *ry = b;
                any = 1;
            }
        }
    }
    return any;
}

/* the frames cut for iw x ih land elsewhere on sw x sh: the same picture scaled, or they fall further off its edge */
static int frames_break(unsigned iw, unsigned ih, unsigned sw, unsigned sh, long rx, long ry) {
    if (iw == sw && ih == sh) return 0;
    if ((unsigned long long) sw * ih == (unsigned long long) sh * iw) return 1;
    long ox_i = rx > (long) iw ? rx - (long) iw : 0, ox_s = rx > (long) sw ? rx - (long) sw : 0;
    long oy_i = ry > (long) ih ? ry - (long) ih : 0, oy_s = ry > (long) sh ? ry - (long) sh : 0;
    return ox_s > ox_i || oy_s > oy_i;
}

static const char *const KIND_EN[8] = { "pictures", "animations", "sounds", "music", "tables", "fonts", "videos", "other" };

static void kinds_text(sb_t *b, unsigned kinds) {
    for (int k = 0, first = 1; k < 8; k++)
        if (kinds & (1u << k)) { sb_printf(b, "%s%s", first ? "" : ", ", KIND_EN[k]); first = 0; }
}

/*
 * The sheets an .anm2 cuts and how far its visible frames reach on each:
 * "<reach x>\t<reach y>\t<sheet path>\n" a sheet. Kept in content-cache/guard/
 * sheets.txt by the file's size and time, as the guard keeps its answers: a
 * start reads again only the .anm2 that changed (Reflashed has 900 of them).
 */
typedef struct reach {
    struct reach *next;
    long long size, mtime;
    int used;                               /* asked for this run: kept in sheets.txt (and its file looked at) */
    char *text;
    char file[1];
} reach_t;
static reach_t *g_reach[BUCKETS];
static int g_reach_dirty;

static reach_t *reach_put(const char *file, long long size, long long mtime, const char *text) {
    unsigned k = hash_of(file) % BUCKETS;
    reach_t *r = NULL;
    for (reach_t *e = g_reach[k]; e && !r; e = e->next) if (!strcmp(e->file, file)) r = e;
    if (!r) {
        size_t n = strlen(file);
        r = (reach_t *) calloc(1, sizeof(reach_t) + n);
        if (!r) return NULL;
        memcpy(r->file, file, n + 1);
        r->next = g_reach[k];
        g_reach[k] = r;
    }
    free(r->text);
    r->text = strdup(text);
    r->size = size;
    r->mtime = mtime;
    return r;
}

static void reach_path(char *out, size_t room) { snprintf(out, room, "%s/sheets.txt", g_guard_dir); }

/* "sheets 1", then per file "F\t<size>\t<mtime>\t<file>" and its sheets' lines with a tab before each */
static void reach_load(void) {
    char p[800];
    reach_path(p, sizeof(p));
    FILE *f = fopen(p, "r");
    if (!f) return;
    char line[1600];
    if (!fgets(line, sizeof(line), f) || strncmp(line, "sheets 1", 8)) { fclose(f); return; }
    char file[1400] = "";
    long long size = -1, mtime = -1;
    sb_t text = {0};
    int have = 0;
    for (;;) {
        int more = fgets(line, sizeof(line), f) != NULL;
        if (more) line[strcspn(line, "\r\n")] = 0;
        if (!more || line[0] == 'F') {
            if (have) reach_put(file, size, mtime, text.s ? text.s : "");
            text.n = 0;
            if (text.s) text.s[0] = 0;
            have = 0;
            if (!more) break;
            char *t1 = strchr(line, '\t'), *t2 = t1 ? strchr(t1 + 1, '\t') : NULL, *t3 = t2 ? strchr(t2 + 1, '\t') : NULL;
            if (!t3) continue;
            size = atoll(t1 + 1);
            mtime = atoll(t2 + 1);
            snprintf(file, sizeof(file), "%s", t3 + 1);
            have = 1;
        } else if (line[0] == '\t' && have) {
            sb_puts(&text, line + 1);
            sb_puts(&text, "\n");
        }
    }
    free(text.s);
    fclose(f);
}

/* the sheets of the .anm2 `file` the game gets for `path`; NULL when it does not read */
static const char *reach_of(const char *file, const char *path, int *parsed) {
    reach_t *r = NULL;
    for (reach_t *e = g_reach[hash_of(file) % BUCKETS]; e && !r; e = e->next) if (!strcmp(e->file, file)) r = e;
    /* looked at once a run: the run's stack and THE PAIRS ask for one file many times, and the files do not change */
    if (r && r->used) return r->text;
    struct stat st;
    if (stat(file, &st)) return NULL;
    if (r && r->size == (long long) st.st_size && r->mtime == (long long) st.st_mtime) { r->used = 1; return r->text; }
    long len = 0;
    char *buf = slurp(file, &len);
    if (!buf) return NULL;
    (*parsed)++;
    anm_t a;
    sb_t text = {0};
    sb_puts(&text, "");
    if (!anm_open(&a, buf, len) && a.sheets >= 0)
        for (int s = x_child(&a.d, a.sheets, "Spritesheet"); s >= 0; s = x_next(&a.d, a.sheets, "Spritesheet", s)) {
            span_t sp = x_attr(&a.d.v[s], "Path");
            char rel[400];
            long rx, ry;
            if (!sp.s || !sheet_rel(path, sp, rel, sizeof(rel)) || strchr(rel, '\t')) continue;
            if (anm_reach(&a, xml_num(&a.d.v[s], "Id", -9999), &rx, &ry)) sb_printf(&text, "%ld\t%ld\t%s\n", rx, ry, rel);
        }
    free(a.d.v);
    free(buf);
    r = reach_put(file, (long long) st.st_size, (long long) st.st_mtime, text.s ? text.s : "");
    free(text.s);
    if (!r) return NULL;
    r->used = 1;
    g_reach_dirty = 1;
    return r->text;
}

/* written again when a file was read anew or one kept is gone from the stack */
static void reach_save(void) {
    int stale = 0;
    for (int b = 0; b < BUCKETS && !stale; b++)
        for (reach_t *e = g_reach[b]; e && !stale; e = e->next) stale = !e->used;
    if (!g_reach_dirty && !stale) return;
    char p[800];
    reach_path(p, sizeof(p));
    FILE *f = fopen(p, "w");
    if (!f) return;
    fputs("sheets 1\n", f);
    for (int b = 0; b < BUCKETS; b++)
        for (reach_t *e = g_reach[b]; e; e = e->next) {
            if (!e->used) continue;
            fprintf(f, "F\t%lld\t%lld\t%s\n", e->size, e->mtime, e->file);
            for (const char *t = e->text; *t; ) {
                const char *eol = strchr(t, '\n');
                size_t k = eol ? (size_t) (eol - t) : strlen(t);
                fprintf(f, "\t%.*s\n", (int) k, t);
                t += k + (eol ? 1 : 0);
            }
        }
    fclose(f);
}

/* the languages the files' keys name (each file's lang filled), "" not among them; at most 16 */
static int files_langs(mfile_t *v, int n, char (*langs)[16]) {
    int nlangs = 0;
    for (int i = 0; i < n; i++) {
        const char *bar = strchr(v[i].key, '|'), *dot = memchr(v[i].key, '.', (size_t) (bar - v[i].key));
        if (dot) snprintf(v[i].lang, sizeof(v[i].lang), "%.*s", (int) (bar - dot - 1), dot + 1);
        int seen = !v[i].lang[0];
        for (int k = 0; k < nlangs && !seen; k++) seen = !strcmp(langs[k], v[i].lang);
        if (!seen && nlangs < 16) snprintf(langs[nlangs++], sizeof(langs[0]), "%s", v[i].lang);
    }
    return nlangs;
}

/*
 * (b) WHAT BREAKS, over files sorted by path (mfile_cmp): the .anm2 the game
 * gets is one owner's, a sheet of it comes from another owner or from the
 * game, of a size its frames were not cut for. A record per (the .anm2's
 * owner, the sheet's owner, the sheet, the sizes) into out[], the .anm2 under
 * it listed in its `files`; how many. The run's stack (conflicts_run) and two
 * mods alone (THE PAIRS) are this one walk.
 */
static int breaks_find(const mfile_t *v, int n, char (*langs)[16], int nlangs, broken_t *out, int max, int *parsed) {
    int nb = 0;
    for (int lo = 0, hi; lo < n; lo = hi) {
        for (hi = lo + 1; hi < n && !strcmp(v[hi].path, v[lo].path); hi++) {}
        const char *path = v[lo].path;
        size_t pn = strlen(path);
        if (pn < 6 || strcmp(path + pn - 5, ".anm2")) continue;
        char said[16][200];                         /* the sheets of this .anm2 told already (another language) */
        int nsaid = 0;
        for (int L = -1; L < nlangs; L++) {
            const char *lang = L < 0 ? "" : langs[L];
            const mfile_t *w = group_first(v, lo, hi, lang, 1);
            if (!w) continue;
            /* "<reach x>\t<reach y>\t<sheet>\n" per sheet its visible frames cut */
            const char *sheets = reach_of(w->served, path, parsed);
            for (const char *ln = sheets; ln && *ln; ) {
                const char *eol = strchr(ln, '\n');
                if (!eol) break;
                long rx = 0, ry = 0;
                char rel[400];
                int got = sscanf(ln, "%ld\t%ld\t", &rx, &ry);
                const char *tab = strchr(ln, '\t');
                tab = tab && tab < eol ? strchr(tab + 1, '\t') : NULL;
                snprintf(rel, sizeof(rel), "%.*s", tab && tab < eol ? (int) (eol - tab - 1) : 0, tab && tab < eol ? tab + 1 : "");
                ln = eol + 1;
                if (got != 2 || !rel[0]) continue;
                /* the sheet's files: binary search of the sorted paths */
                int slo = 0, shi = n;
                while (slo < shi) { int mid = (slo + shi) / 2; if (strcmp(v[mid].path, rel) < 0) slo = mid + 1; else shi = mid; }
                int send = slo;
                while (send < n && !strcmp(v[send].path, rel)) send++;
                const mfile_t *served = group_first(v, slo, send, lang, 1);
                if (served && served->owner == w->owner) continue;      /* its own: the mod's own business */
                /* what the frames were cut for: the mod's own copy (served or not), else the game's */
                const mfile_t *own = NULL;
                for (int i = slo; i < send; i++)
                    if (v[i].owner == w->owner && in_lang(&v[i], lang) && (!own || v[i].rank > own->rank)) own = &v[i];
                if (!own && !served) continue;                          /* the game's under frames cut for the game's */
                unsigned iw, ih, sw, sh;
                if (!(own ? pic_size_file(own->src, &iw, &ih) : pic_size_game(lang, rel, &iw, &ih))) continue;
                if (!(served ? pic_size_file(served->served, &sw, &sh) : pic_size_game(lang, rel, &sw, &sh))) continue;
                if (!frames_break(iw, ih, sw, sh, rx, ry)) continue;
                int dup = 0;
                for (int k = 0; k < nsaid && !dup; k++) dup = !strcmp(said[k], rel);
                if (dup) continue;
                if (nsaid < 16) snprintf(said[nsaid++], sizeof(said[0]), "%s", rel);
                /* one record per (the .anm2's mod, the sheet's mod, the sheet, the sizes): the .anm2 files under it listed */
                int sheet_owner = served ? served->owner : -1;
                broken_t *bk = NULL;
                for (int k = 0; k < nb && !bk; k++)
                    if (out[k].anm_owner == w->owner && out[k].sheet_owner == sheet_owner && !strcmp(out[k].sheet, rel)
                        && out[k].iw == iw && out[k].ih == ih && out[k].sw == sw && out[k].sh == sh)
                        bk = &out[k];
                if (!bk) {
                    if (nb >= max) continue;
                    bk = &out[nb++];
                    memset(bk, 0, sizeof(*bk));
                    bk->anm_owner = (owner_t) w->owner;
                    bk->sheet_owner = sheet_owner;
                    bk->own = !own ? 0 : own->refused ? 2 : 1;
                    snprintf(bk->anm, sizeof(bk->anm), "%s", path);
                    snprintf(bk->sheet, sizeof(bk->sheet), "%s", rel);
                    snprintf(bk->lang, sizeof(bk->lang), "%s", lang);
                    bk->iw = iw; bk->ih = ih; bk->sw = sw; bk->sh = sh;
                }
                if (bk->lang[0] && !lang[0]) bk->lang[0] = 0;          /* in all sets after all */
                bk->anms++;
                sb_printf(&bk->files, "    %s%s%s%s\n", path, lang[0] ? " [" : "", lang, lang[0] ? " only]" : "");
            }
        }
    }
    return nb;
}

static void conflicts_run(void) {
    long long t0 = now_ms();
    /* every mod file of the stacked set */
    int cap = g_indexed + 64, n = 0;
    for (shadow_t *s = g_shadows; s; s = s->next) cap++;
    mfile_t *v = (mfile_t *) calloc((size_t) cap, sizeof(mfile_t));
    if (!v) return;
    char langs[16][16];
    int nlangs = 0;
    for (int b = 0; b <= BUCKETS; b++) {
        if (b < BUCKETS) {
            for (ufile_t *e = g_index[b]; e && n < cap; e = e->next) {
                if (e->owner >= MODS_MAX) continue;
                mfile_t *m = &v[n++];
                m->key = e->key; m->path = strchr(e->key, '|') + 1; m->src = e->src;
                m->served = e->path; m->refused = e->guard == GUARD_REFUSED;
                m->owner = e->owner; m->rank = e->rank;
            }
        } else {
            for (shadow_t *s = g_shadows; s && n < cap; s = s->next) {
                mfile_t *m = &v[n++];
                m->key = s->key; m->path = strchr(s->key, '|') + 1; m->src = s->src; m->served = NULL;
                m->owner = s->owner; m->rank = s->rank;
            }
        }
    }
    nlangs = files_langs(v, n, langs);
    qsort(v, (size_t) n, sizeof(mfile_t), mfile_cmp);

    /* (a) who stands over whom: per path, per language, the first in THE ORDER over every other mod's file */
    over_rec_t *rec = (over_rec_t *) malloc(sizeof(over_rec_t) * OVER_MAX);
    int nrec = 0;
    for (int lo = 0, hi; rec && lo < n; lo = hi) {
        for (hi = lo + 1; hi < n && !strcmp(v[hi].path, v[lo].path); hi++) {}
        if (hi - lo < 2) continue;
        const char *path = v[lo].path;
        if (!strchr(path, '/') && strlen(path) > 4 && !strcmp(path + strlen(path) - 4, ".xml") && content_known(path))
            continue;                               /* content.c's: merged, with its own conflict lines */
        for (int L = -1; L < nlangs; L++) {
            const char *lang = L < 0 ? "" : langs[L];
            const mfile_t *w = group_first(v, lo, hi, lang, 0);
            if (!w) continue;
            for (int i = lo; i < hi && nrec < OVER_MAX; i++) {
                if (v[i].owner == w->owner || !in_lang(&v[i], lang)) continue;
                over_rec_t *r = &rec[nrec++];
                r->over = (owner_t) w->owner;
                r->under = (owner_t) v[i].owner;
                snprintf(r->lang, sizeof(r->lang), "%s", lang);
                r->path = path;
                r->win = (int) (w - v);
            }
        }
    }
    if (rec) qsort(rec, (size_t) nrec, sizeof(over_rec_t), over_rec_cmp);
    g_nover = 0;
    sb_t *ct = g_conflict_text;
    for (int i = 0; rec && i < nrec; ) {
        int j = i;
        while (j < nrec && rec[j].over == rec[i].over && rec[j].under == rec[i].under) j++;
        if (g_nover < (int) (sizeof(g_over) / sizeof(g_over[0]))) {
            over_t *o = &g_over[g_nover++];
            memset(o, 0, sizeof(*o));
            o->over = rec[i].over;
            o->under = rec[i].under;
            sb_t list = {0};
            for (int k = i; k < j; k++) {
                if (k > i && !strcmp(rec[k].path, rec[k - 1].path)) continue;   /* in all sets already, or a 2nd language */
                o->files++;
                o->kinds |= kind_of(rec[k].path);
                if (rec[k].lang[0]) { if (!o->lang_only++) snprintf(o->lang, sizeof(o->lang), "%s", rec[k].lang); }
                /* the stronger one's copy refused by the guard: the game's file is what the game gets */
                const mfile_t *wm = &v[rec[k].win];
                int refused = !wm->served || wm->refused;
                o->refused += refused;
                sb_printf(&list, "    %s%s%s%s%s\n", rec[k].path, rec[k].lang[0] ? " [" : "", rec[k].lang,
                          rec[k].lang[0] ? " only]" : "",
                          refused ? " - the stronger mod's copy is not used either (the port's check): the game's file" : "");
            }
            for (int side = 0; side < 2; side++) {
                int me = side ? o->under : o->over, other = side ? o->over : o->under;
                if (!ct[me].n) sb_puts(&ct[me], "Over other mods (the stronger mod's file is the one the game gets, as on the "
                                                 "PC - the order of the MODS page):\n");
                sb_printf(&ct[me], side ? "  %d files overridden by %s (" : "  overrides %d files of %s (", o->files,
                          g_owner[other]);
                kinds_text(&ct[me], o->kinds);
                sb_puts(&ct[me], "):\n");
                sb_put(&ct[me], list.s, list.n);
            }
            free(list.s);
        }
        i = j;
    }
    free(rec);

    /* (b) what breaks */
    int parsed = 0;
    reach_load();
    g_nbreaks = breaks_find(v, n, langs, nlangs, g_breaks, BROKEN_MAX, &parsed);
    for (int k = 0; k < g_nbreaks; k++) {
        broken_t *bk = &g_breaks[k];
        char head[1400];
        snprintf(head, sizeof(head), "  BROKEN: %s of %s is %ux%u, and %d .anm2 of %s cut it for %ux%u (%s) - their frames "
                 "land elsewhere on it:\n", bk->sheet, bk->sheet_owner >= 0 ? g_owner[bk->sheet_owner] : "the game", bk->sw, bk->sh,
                 bk->anms, g_owner[bk->anm_owner], bk->iw, bk->ih,
                 bk->own == 1 ? "the size of its own copy, which a stronger mod's stands over"
                 : bk->own == 2 ? "the size of its own copy, which the port's check does not use" : "the game's size: it brings none");
        int ends[2] = { bk->anm_owner, bk->sheet_owner };
        for (int side = 0; side < 2; side++) {
            int me = ends[side];
            if (me < 0) continue;
            if (!ct[me].n) sb_puts(&ct[me], "Over other mods (the stronger mod's file is the one the game gets, as on the "
                                             "PC - the order of the MODS page):\n");
            sb_puts(&ct[me], head);
            sb_put(&ct[me], bk->files.s, bk->files.n);
        }
        __android_log_print(ANDROID_LOG_WARN, "isaac", "usermods: %.*s", (int) strlen(head + 2) - 2, head + 2);
    }
    for (int o = 0; o < g_owners; o++) if (g_conflict_text[o].n) sb_puts(&g_conflict_text[o], "\n");
    NOTE("mods over each other: %d mod files (%d under a stronger one), %d languages; %d pairs, %d broken; %d .anm2 read, %lld ms",
         n, n - g_indexed, nlangs, g_nover, g_nbreaks, parsed, now_ms() - t0);
    (void) t0;
    free(v);
}


/* ------------------------------------------------------------ THE PAIRS */
/*
 * THE PAIRS (the owner, 05.10.2026: a popup the moment a mod is switched on
 * that breaks with one that is on; one red line at the top of a mod's page,
 * its table behind it). The run's record above is exact for the stack this
 * run started with, and a mod switched on now was not in it. So every two
 * mods of the list, on or off, are worked out as if they were the only two on
 * - the upper one in the list over the lower, as THE ORDER has it - by the
 * same walk (breaks_find) over each mod's files. A third mod between them can
 * change what is served; the stack's own record is the run's (check.txt).
 *
 * Only a breakage between the two is one: an .anm2 of one of them and a sheet
 * of the other. A sheet of the game's under a mod's own frames (its copy
 * refused by the guard) is that mod's business, not the pair's.
 *
 * A mod's files are listed once (THE ORDER's index for one mod: its layers
 * and its folders; a mod of this run keeps the guard's word on each - the
 * completed copy, refused), and the .anm2's sheets come from the guard's own
 * cache (reach_of, sheets.txt): after the first start nothing is parsed again,
 * and a file is looked at on the disk once a run. Both orders of every two
 * are worked out on a thread of its own after the guard, so a mod moved on
 * the page has its answer ready; a mod the list did not have then (one added
 * while the game runs) is worked out when it is asked for. Until the thread is
 * done the page's answer is "not yet" (-1), and it asks again.
 */
typedef struct plist {
    struct plist *next;                     /* in its bucket of g_plists */
    char id[ID_MAX];
    int n;
    mfile_t *v;                             /* sorted by path (mfile_cmp) */
} plist_t;

/*
 * THE PAIRS AT 256 MODS (hotfix 1.75, MODS_MAX 64 -> 256): 256 x 255 = 65280 of them, sixteen times 64's. Kept in
 * one list, each pair asked walked all the ones before it - the desk's run with 256 small mods took 4 minutes; by
 * bucket the thread takes 135 ms and the run 6 s. And most pairs share nothing: a full record each (two ids and the rest, ~180 bytes) was
 * 12 MB kept for the run; such a pair is now a key of three words (pnone_t, 1.5 MB at the most) and answers with
 * the one empty record. A pair is keyed by its two plists, each listed once a run.
 */
typedef struct pair {
    struct pair *next;
    const plist_t *upper, *lower;
    int nbroken;
    broken_t *broken;                       /* owner 0 the upper, 1 the lower; the sheet always the other one's */
    int over_files, over_lang_only;         /* the upper one's files over the lower one's */
    unsigned over_kinds;
    char over_lang[16];
} pair_t;

#define PAIR_BROKEN 32
static pthread_mutex_t g_pair_lock = PTHREAD_MUTEX_INITIALIZER;
#define PLIST_BUCKETS 512
#define PAIR_BUCKETS 8192
typedef struct pnone { struct pnone *next; const plist_t *upper, *lower; } pnone_t;   /* a pair with nothing */
static plist_t *g_plists[PLIST_BUCKETS];
static pair_t *g_pairs[PAIR_BUCKETS];
static pnone_t *g_pnone[PAIR_BUCKETS];
static const pair_t k_pair_none;            /* what a pair with nothing answers */
static unsigned pair_bucket(const plist_t *upper, const plist_t *lower) {
    unsigned long long a = (unsigned long long) (size_t) upper >> 4, b = (unsigned long long) (size_t) lower >> 4;
    return (unsigned) ((a * 1000003u) ^ (b * 31u) ^ (a >> 13)) % PAIR_BUCKETS;
}
static volatile int g_pairs_gen;            /* 0 until the thread is done; then grows when a pair is added */
static int g_pairs_parsed;

static void collect_add(collect_t *c, const char *key, const char *path, int rank) {
    /* a key once: the first met is the mod's, as in the index (a layer's file over the mod's own) */
    if (c->n * 2 >= c->nslot) {
        int ns = c->nslot ? c->nslot * 2 : 1024;
        int *s = (int *) malloc(sizeof(int) * (size_t) ns);
        if (!s) return;
        for (int i = 0; i < ns; i++) s[i] = -1;
        for (int i = 0; i < c->n; i++) {
            unsigned h = hash_of(c->v[i].key) & (unsigned) (ns - 1);
            while (s[h] >= 0) h = (h + 1) & (unsigned) (ns - 1);
            s[h] = i;
        }
        free(c->slot);
        c->slot = s;
        c->nslot = ns;
    }
    unsigned h = hash_of(key) & (unsigned) (c->nslot - 1);
    for (; c->slot[h] >= 0; h = (h + 1) & (unsigned) (c->nslot - 1))
        if (!strcasecmp(c->v[c->slot[h]].key, key)) return;
    if (c->n >= c->cap) {
        int cap = c->cap ? c->cap * 2 : 256;
        mfile_t *v = (mfile_t *) realloc(c->v, sizeof(mfile_t) * (size_t) cap);
        if (!v) return;
        c->v = v;
        c->cap = cap;
    }
    char *k = strdup(key), *src = strdup(path);
    if (!k || !src) { free(k); free(src); return; }
    lower(k);
    mfile_t *m = &c->v[c->n];
    memset(m, 0, sizeof(*m));
    m->key = k;
    m->path = strchr(k, '|') + 1;
    m->src = m->served = src;
    m->rank = rank;
    c->slot[h] = c->n++;
}

/* a mod's files, listed once (under g_pair_lock); NULL for a folder that did not answer */
static const plist_t *plist_of(const umod_t *m) {
    unsigned at = hash_of(m->id) % PLIST_BUCKETS;
    for (plist_t *p = g_plists[at]; p; p = p->next) if (!strcmp(p->id, m->id)) return p;
    if (m->reach || !g_dir[0]) return NULL;
    plist_t *p = (plist_t *) calloc(1, sizeof(plist_t));
    umod_t *tmp = (umod_t *) malloc(sizeof(umod_t));
    if (!p || !tmp) { free(p); free(tmp); return NULL; }
    snprintf(p->id, sizeof(p->id), "%s", m->id);
    *tmp = *m;                                      /* mod_layers writes the layers' names into it */
    collect_t c = {0};
    static char layer[LAYERS_MAX][900];
    g_collect = &c;
    int nl = mod_layers(tmp, layer);
    for (int k = nl - 1; k >= 0; k--) index_root(layer[k], tmp, 0);
    char base[768];
    snprintf(base, sizeof(base), "%s/%s", g_dir, m->id);
    index_root(base, tmp, 0);
    g_collect = NULL;
    free(tmp);
    free(c.slot);
    /* a mod of this run: what the game is handed for a file of it is the guard's word on it */
    int o = -1;
    for (int k = 0; k < g_owners && o < 0; k++) if (!strcmp(g_owner[k], m->id)) o = k;
    char lang_none[16][16];
    files_langs(c.v, c.n, lang_none);
    for (int i = 0; o >= 0 && i < c.n; i++) {
        const char *key = c.v[i].key;
        for (ufile_t *e = g_index[hash_of(key) % BUCKETS]; e; e = e->next)
            if (!strcasecmp(e->key, key)) {
                if (e->owner == o) { c.v[i].served = e->path; c.v[i].refused = e->guard == GUARD_REFUSED; }
                break;
            }
    }
    qsort(c.v, (size_t) c.n, sizeof(mfile_t), mfile_cmp);
    p->v = c.v;
    p->n = c.n;
    p->next = g_plists[at];
    g_plists[at] = p;
    return p;
}

/* the upper mod's files over the lower one's, and what breaks between them (under g_pair_lock) */
static pair_t *pair_make(const plist_t *a, const plist_t *b) {
    pair_t *p = (pair_t *) calloc(1, sizeof(pair_t));
    if (!p) return NULL;
    p->upper = a;
    p->lower = b;
    int n = a->n + b->n;
    mfile_t *v = (mfile_t *) malloc(sizeof(mfile_t) * (size_t) (n ? n : 1));
    if (!v) { free(p); return NULL; }
    if (a->n) memcpy(v, a->v, sizeof(mfile_t) * (size_t) a->n);
    if (b->n) memcpy(v + a->n, b->v, sizeof(mfile_t) * (size_t) b->n);
    for (int i = 0; i < n; i++) v[i].owner = i < a->n ? 0 : 1;
    qsort(v, (size_t) n, sizeof(mfile_t), mfile_cmp);
    char langs[16][16];
    int nlangs = 0;
    for (int i = 0; i < n; i++) {
        int seen = !v[i].lang[0];
        for (int k = 0; k < nlangs && !seen; k++) seen = !strcmp(langs[k], v[i].lang);
        if (!seen && nlangs < 16) snprintf(langs[nlangs++], sizeof(langs[0]), "%s", v[i].lang);
    }
    /* (a) a path both bring, in a set both answer: the upper one's is the game's */
    for (int lo = 0, hi; lo < n; lo = hi) {
        for (hi = lo + 1; hi < n && !strcmp(v[hi].path, v[lo].path); hi++) {}
        if (hi - lo < 2) continue;
        const char *path = v[lo].path;
        if (!strchr(path, '/') && strlen(path) > 4 && !strcmp(path + strlen(path) - 4, ".xml") && content_known(path))
            continue;                               /* content.c's: merged, with its own conflict lines */
        int both = 0, all_sets = 0;
        const char *lang = NULL;
        for (int x = lo; x < hi; x++)
            for (int y = lo; y < hi; y++) {
                if (v[x].owner != 0 || v[y].owner != 1) continue;
                if (v[x].lang[0] && v[y].lang[0] && strcmp(v[x].lang, v[y].lang)) continue;
                both = 1;
                if (!v[x].lang[0] && !v[y].lang[0]) all_sets = 1;
                else if (!lang) lang = v[x].lang[0] ? v[x].lang : v[y].lang;
            }
        if (!both) continue;
        p->over_files++;
        p->over_kinds |= kind_of(path);
        if (!all_sets && lang && !p->over_lang_only++) snprintf(p->over_lang, sizeof(p->over_lang), "%s", lang);
    }
    /* (b) what breaks: an .anm2 of one and a sheet of the other */
    static broken_t found[PAIR_BROKEN];
    int nf = breaks_find(v, n, langs, nlangs, found, PAIR_BROKEN, &g_pairs_parsed), kept = 0;
    for (int k = 0; k < nf; k++) kept += found[k].sheet_owner >= 0;
    p->broken = kept ? (broken_t *) malloc(sizeof(broken_t) * (size_t) kept) : NULL;
    for (int k = 0; k < nf; k++) {
        if (found[k].sheet_owner >= 0 && p->broken) {
            p->broken[p->nbroken] = found[k];
            memset(&p->broken[p->nbroken].files, 0, sizeof(sb_t));
            p->nbroken++;
        }
        free(found[k].files.s);
    }
    free(v);
    return p;
}

/* upper over lower, worked out once (under g_pair_lock) */
static const pair_t *pair_of(const umod_t *upper, const umod_t *lower) {
    const plist_t *a = plist_of(upper), *b = plist_of(lower);
    if (!a || !b) return NULL;
    unsigned h = pair_bucket(a, b);
    for (pair_t *p = g_pairs[h]; p; p = p->next) if (p->upper == a && p->lower == b) return p;
    for (pnone_t *z = g_pnone[h]; z; z = z->next) if (z->upper == a && z->lower == b) return &k_pair_none;
    pair_t *p = pair_make(a, b);
    if (!p) return NULL;
    if (!p->over_files && !p->nbroken) {
        /* nothing between them: a key of three words instead of the record */
        free(p->broken);
        free(p);
        pnone_t *z = (pnone_t *) malloc(sizeof(pnone_t));
        if (!z) return &k_pair_none;
        z->upper = a;
        z->lower = b;
        z->next = g_pnone[h];
        g_pnone[h] = z;
        return &k_pair_none;
    }
    p->next = g_pairs[h];
    g_pairs[h] = p;
    return p;
}

static void *pairs_warm(void *arg) {
    (void) arg;
    long long t0 = now_ms();
    pthread_mutex_lock(&g_list_lock);
    int n = g_count;
    umod_t *snap = n ? (umod_t *) malloc(sizeof(umod_t) * (size_t) n) : NULL;
    if (snap) memcpy(snap, g_mods, sizeof(umod_t) * (size_t) n);
    pthread_mutex_unlock(&g_list_lock);
    pthread_mutex_lock(&g_pair_lock);
    int files = 0, pairs = 0, breaking = 0;
    for (int i = 0; snap && i < n; i++) { const plist_t *l = plist_of(&snap[i]); if (l) files += l->n; }
    for (int i = 0; snap && i < n; i++)
        for (int j = 0; j < n; j++) {
            if (i == j) continue;
            const pair_t *p = pair_of(&snap[i], &snap[j]);
            if (p) { pairs++; breaking += p->nbroken > 0; }
        }
    reach_save();
    g_pairs_gen++;
    pthread_mutex_unlock(&g_pair_lock);
    NOTE("pairs: %d mods (on and off), %d files listed, %d pairs worked out (both orders), %d breaking; %d .anm2 read, "
         "%lld ms", n, files, pairs, breaking, g_pairs_parsed, now_ms() - t0);
    (void) t0; (void) files; (void) pairs; (void) breaking;
    free(snap);
    return NULL;
}

/* after the guard: the pairs on a thread of their own (the game's loading is not held for them) */
static void pairs_start(void) {
    pthread_t th;
    if (pthread_create(&th, NULL, pairs_warm, NULL) == 0) pthread_detach(th);
    else pairs_warm(NULL);
}

/*
 * THE SHEETS OF A MOD'S .anm2 THE PORT DOES NOT USE (hotfix 1.71, round 3). When
 * a mod's .anm2 is refused (its menu file all or nothing, broken XML, past the
 * loader's limits), the game's .anm2 cuts the mod's sheets in its place. A sheet
 * of the game's size is fine; a whole k of it gets the game's frames fitted
 * (THE GAME'S FRAMES FITTED). Any other size - Reflashed's optionsextra.png,
 * 1008x1150 for the game's 256x256 - nothing can cut right: the game's picture
 * stands in, and the reason says it in red on the mod's page. Not when another
 * .anm2 of the mod that is used cuts the same picture (its own frames want it).
 */
static void kfit_norm(char *p);
#define KFIT_K_MAX 16                     /* the largest k THE GAME'S FRAMES FITTED takes */

static int sheet_wanted_by_own(int owner, const char *rel) {
    const char *slash = strrchr(rel, '/');
    size_t dn = slash ? (size_t) (slash - rel + 1) : 0;
    const char *leaf = rel + dn;
    char want[300];
    snprintf(want, sizeof(want), "path=\"%s\"", leaf);
    lower(want);
    for (int b = 0; b < BUCKETS; b++)
        for (ufile_t *e = g_index[b]; e; e = e->next) {
            if (e->owner != owner || e->guard == GUARD_REFUSED) continue;
            const char *p = strchr(e->key, '|') + 1;
            size_t pn = strlen(p);
            if (pn < 6 || strcmp(p + pn - 5, ".anm2") || strncmp(p, rel, dn) || strchr(p + dn, '/')) continue;
            long n = 0;
            char *t = slurp(e->path, &n);
            if (!t) continue;
            lower(t);
            int hit = strstr(t, want) != NULL;
            free(t);
            if (hit) return 1;
        }
    return 0;
}

static void guard_orphan_sheets(void) {
    for (int b = 0; b < BUCKETS; b++)
        for (ufile_t *a = g_index[b]; a; a = a->next) {
            const char *ap = strchr(a->key, '|') + 1;
            size_t an = strlen(ap);
            if (a->owner >= MODS_MAX || a->guard != GUARD_REFUSED || an < 6 || strcmp(ap + an - 5, ".anm2")) continue;
            long glen = 0;
            const char *g = (const char *) game_file(a->key, &glen);
            anm_t ga;
            if (!g || anm_open(&ga, g, glen)) { if (g) free(ga.d.v); continue; }
            const char *slash = strrchr(ap, '/');
            int dirn = slash ? (int) (slash - ap + 1) : 0;
            const char *aleaf = ap + dirn;
            for (int s = ga.sheets >= 0 ? x_child(&ga.d, ga.sheets, "Spritesheet") : -1; s >= 0;
                 s = x_next(&ga.d, ga.sheets, "Spritesheet", s)) {
                span_t pv = x_attr(&ga.d.v[s], "Path");
                if (!pv.s) continue;
                char rel[600];
                snprintf(rel, sizeof(rel), "%.*s%.*s", dirn, ap, (int) (pv.e - pv.s), pv.s);
                kfit_norm(rel);
                lower(rel);
                for (int c = 0; c < BUCKETS; c++)
                    for (ufile_t *e = g_index[c]; e; e = e->next) {
                        if (e->owner >= MODS_MAX || e->guard == GUARD_REFUSED || strcmp(strchr(e->key, '|') + 1, rel)) continue;
                        unsigned char h[24];
                        FILE *f = fopen(e->path, "rb");
                        size_t n = f ? fread(h, 1, sizeof(h), f) : 0;
                        if (f) fclose(f);
                        unsigned mw, mh, gw, gh;
                        long pl = 0;
                        const unsigned char *gp = (const unsigned char *) game_file(e->key, &pl);
                        if (!png_size(h, n, &mw, &mh) || !gp || !png_size(gp, (size_t) pl, &gw, &gh)) continue;
                        if (mw == gw && mh == gh) continue;
                        unsigned k = gw && gh && !(mw % gw) && !(mh % gh) ? mw / gw : 0;
                        if (k >= 2 && k <= KFIT_K_MAX && mh / gh == k) continue;       /* fitted */
                        if (sheet_wanted_by_own(e->owner, rel)) continue;
                        char why[500];
                        snprintf(why, sizeof(why), "a picture of the mod's %s, which the port does not use (see that file): "
                                 "%ux%u is neither the game's %ux%u nor a whole multiple of it, so the game's frames "
                                 "cannot cut it - the game's picture is used", aleaf, mw, mh, gw, gh);
                        e->guard = GUARD_REFUSED;
                        set_why(e, why);
                        NOTE("guard %s: %s - %s", owner_id(e), e->key, why);
                    }
            }
            free(ga.d.v);
        }
}

static void guard_run(void) {
    long long t0 = now_ms();
    snprintf(g_guard_dir, sizeof(g_guard_dir), "%s/../content-cache/guard", shim_pack_root());
    mkdirs(g_guard_dir);
    for (int o = 0; o < g_owners; o++) gcache_load(g_owner[o]);
    int checked[MODS_MAX] = {0}, cached[MODS_MAX] = {0}, later[MODS_MAX] = {0};
    for (int b = 0; b < BUCKETS; b++)
        for (ufile_t *e = g_index[b]; e; e = e->next) {
            if (e->owner >= MODS_MAX) continue;
            int from_cache = 0;
            if (guard_file(e, &from_cache)) { if (from_cache) cached[e->owner]++; else checked[e->owner]++; }
        }
    guard_port_patched();
    guard_orphan_sheets();
    /* the data tables: built (or taken from content-cache/data/) before the game reads the first of them */
    char cache[800];
    snprintf(cache, sizeof(cache), "%s/../content-cache", shim_pack_root());
    content_prepare(cache);
    /* the mods over each other: who stands over whom, and what breaks of it; then every two of the list (THE PAIRS) */
    conflicts_run();
    pairs_start();
    for (int b = 0; b < BUCKETS; b++)
        for (ufile_t *e = g_index[b]; e; e = e->next) {
            if (e->owner >= MODS_MAX) continue;
            if (e->guard == GUARD_FIXED) g_guard_fixed[e->owner]++;
            if (e->guard == GUARD_REFUSED) g_guard_game[e->owner]++;
            if (e->guard == GUARD_REFUSED && e->why && strstr(e->why, SIZE_HINT)) g_guard_size[e->owner]++;
            if (e->guard == GUARD_REFUSED && e->why && strstr(e->why, BOUNDS_MARK)) {
                if (!g_guard_bounds[e->owner]++) g_guard_bound_file[e->owner] = e;
            }
            if (e->guard == GUARD_PNG_LATER) later[e->owner]++;
        }
    /* the caches and the notes, a mod at a time */
    for (int o = 0; o < g_owners; o++) {
        char path[900];
        snprintf(path, sizeof(path), "%s/%s.txt", g_guard_dir, g_owner[o]);
        FILE *f = checked[o] || !cached[o] ? fopen(path, "w") : NULL;
        if (f) fprintf(f, "guard %d %s\n", GUARD_RULES, g_owner_layers[o] && g_owner_layers[o][0] ? g_owner_layers[o] : "-");
        sb_t note = {0};
        for (int b = 0; b < BUCKETS; b++)
            for (ufile_t *e = g_index[b]; e; e = e->next) {
                if (e->owner != o) continue;
                if (f) gcache_line(f, e);
                if (e->guard == GUARD_REFUSED || (e->guard == GUARD_FIXED && !content_owns(e))) check_line(&note, e);
            }
        if (f) fclose(f);
        /* the completion widget's anm2 of its own (M9): the reminder's marked sheets are the game's layout */
        g_widget_alone[o] = widget_alone(o);
        if (g_widget_alone[o]) {
            if (!note.n) sb_puts(&note, "The port's check of this mod's files (what the engine here cannot take is not handed to it as it is).\n");
            sb_puts(&note, WIDGET_NOTE "\n");
        }
        /* the mods over each other (THE MODS OVER EACH OTHER) */
        if (g_conflict_text[o].n) {
            if (note.n) sb_puts(&note, "\n");
            sb_put(&note, g_conflict_text[o].s, g_conflict_text[o].n);
        }
        /* what content.c made of its tables: merged, replaced, refused, conflicts, "overridden by legacy" */
        const char *tables = content_check_text(g_owner[o]);
        if (tables[0]) {
            if (!note.n) sb_puts(&note, "The port's check of this mod's files (what the engine here cannot take is not handed to it as it is).\n");
            sb_puts(&note, tables);
        }
        /* what the port read of its mod.json, at the head */
        if (g_owner_issues[o] && g_owner_issues[o][0]) {
            sb_t whole = {0};
            manifest_section(&whole, g_owner_issues[o], 0);
            if (note.n) sb_put(&whole, note.s, note.n);
            free(note.s);
            note = whole;
        }
        check_write(o, &note);
        free(note.s);
        if (g_guard_fixed[o] || g_guard_game[o])
            LOG("usermods: guard %s: %d completed, %d the game's", g_owner[o], g_guard_fixed[o], g_guard_game[o]);
        NOTE("guard %s: %d files checked, %d answered from the cache; %d completed, %d the game's file instead, "
             "%d pictures checked whole at their first use", g_owner[o], checked[o], cached[o], g_guard_fixed[o],
             g_guard_game[o], later[o]);
#if ISAAC_DIAG
        int said = 0;
        for (int b = 0; b < BUCKETS && said < 40; b++)
            for (ufile_t *e = g_index[b]; e && said < 40; e = e->next)
                if (e->owner == o && (e->guard == GUARD_FIXED || e->guard == GUARD_REFUSED)) {
                    NOTE("guard %s: %s %s - %s", g_owner[o], e->guard == GUARD_FIXED ? "completed" : "the game's file for",
                         e->key, e->why ? e->why : "");
                    said++;
                }
#endif
    }
    NOTE("guard: done in %lld ms", now_ms() - t0);
    (void) t0;
    g_guard_ran = 1;
}

static void guard_start(void) {
    const char *root = shim_pack_root();
    if (!root || !*root) return;                    /* the pack is not open yet: the game's files are the model */
    pthread_once(&g_guard_once, guard_run);
}

/* a picture whose whole check waits for its first use: read through once, as libpng 1.0.2 would */
static const ufile_t *guard_png(ufile_t *e) {
    pthread_mutex_lock(&g_guard_lock);
    if (e->guard == GUARD_PNG_LATER) {
        char why[300] = "";
        int bad = png_full(e->src, why, sizeof(why));
        if (bad) {
            size_t k = strlen(why);
            snprintf(why + k, sizeof(why) - k, " - libpng 1.0.2 would stop on it; %s",
                     e->gamelen >= 0 ? "the game's picture is used" : "left out");
            set_why(e, why);
            g_guard_game[e->owner]++;
            __android_log_print(ANDROID_LOG_WARN, "isaac", "usermods: %s %s: %s", owner_id(e), e->key, why);
            NOTE("guard %s: the game's file for %s - %s", owner_id(e), e->key, why);
        }
        e->guard = bad ? GUARD_REFUSED : GUARD_OK;
        /* the answer kept: a line at the end of the mod's cache (the last line of a key wins) */
        char path[900];
        snprintf(path, sizeof(path), "%s/%s.txt", g_guard_dir, owner_id(e));
        FILE *f = fopen(path, "a");
        if (f) { gcache_line(f, e); fclose(f); }
        if (bad) {
            sb_t note = {0};
            char cp[800];
            check_note_path(cp, sizeof(cp), owner_id(e));
            long n = 0;
            char *have = slurp(cp, &n);
            if (have) sb_put(&note, have, (size_t) n);
            free(have);
            check_line(&note, e);
            check_write(e->owner, &note);
            free(note.s);
        }
    }
    pthread_mutex_unlock(&g_guard_lock);
    return e;
}

/* ------------------------------------------------------------ the answer */

/* the mod's file for a path under the pack's root, or NULL; quiet: no report lines (shim_usermods_peek) */
static const ufile_t *usermods_find(const char *p, int quiet) {
    const char *root = shim_pack_root();
    size_t rn = root ? strlen(root) : 0;
    if (!rn || strncmp(p, root, rn)) return NULL;
    const char *q = p + rn;
    while (*q == '/') q++;
    /* <edition>-resources/<set>/<path> */
    const char *slash = strchr(q, '/');
    if (!slash || slash - q < 10 || strncmp(slash - 10, "-resources", 10)) {
        /* the port's own files: [resources/][data/]<path> (see the note above) */
        const char *path = q;
        if (!strncasecmp(path, "resources/", 10)) path += 10;
        if (!strncasecmp(path, "data/", 5)) path += 5;
        return *path ? find_entry("", path) : NULL;
    }
    /* the base engine's own set (its shaders) is no mod's: "shaders" is not a set of a mod */
    if (slash - q == 14 && !strncasecmp(q, "kage-resources", 14)) return NULL;
    const char *set = slash + 1;
    const char *slash2 = strchr(set, '/');
    /* the set's own language (the engine lists its language sets when it mounts them: a path ending at the set) */
    game_lang_seen(set, slash2 ? (size_t) (slash2 - set) : strlen(set));
    if (!slash2) return NULL;
    char set_name[64];
    size_t sn = (size_t) (slash2 - set);
    if (sn >= sizeof(set_name)) return NULL;
    memcpy(set_name, set, sn);
    set_name[sn] = 0;
    lower(set_name);
    const char *path = slash2 + 1;
    while (*path == '/') path++;
    /* THE ORDER: the language is the asked set's, or - asked under a set for all languages - the game's */
    char lang[16];
    const char *dot = strchr(set_name, '.');
    if (dot) snprintf(lang, sizeof(lang), "%s", dot + 1);
    else {
        pthread_mutex_lock(&g_lang_lock);
        snprintf(lang, sizeof(lang), "%s", g_game_lang);
        pthread_mutex_unlock(&g_lang_lock);
    }
    const ufile_t *hit = order_pick(lang, path);
#if ISAAC_DIAG
    /* a data table the mod replaces whole: what it changes, in the report (M4) */
    if (hit && !quiet && !strchr(path, '/')) {
        size_t n = strlen(path);
        if (n > 4 && !strcasecmp(path + n - 4, ".xml")) legacy_note(path, hit, q);
    }
#endif
    (void) quiet;
    return hit;
}
static const ufile_t *usermods_lookup(const char *p) { return usermods_find(p, 0); }

/* <root>/repentance-resources/data/<leaf>, a table content.c answers for: the leaf; NULL otherwise */
static const char *data_leaf(const char *p) {
    const char *root = shim_pack_root();
    size_t rn = root ? strlen(root) : 0;
    if (!rn || strncmp(p, root, rn)) return NULL;
    const char *q = p + rn;
    while (*q == '/') q++;
    if (strncmp(q, "repentance-resources/data/", 26)) return NULL;
    q += 26;
    if (strchr(q, '/') || strlen(q) >= 48) return NULL;
    char low[48];
    snprintf(low, sizeof(low), "%s", q);
    lower(low);
    return content_known(low) ? q : NULL;
}

/*
 * THE PORT'S ADDITIONS OVER A MOD'S OWN (plan 30.09, M8). The port lays its
 * rows, straps and buttons over a few of the game's .anm2 by name (portpatch.c);
 * a mod that brings its own copy of one gets the same laid over it, so the mod's
 * menu keeps the port's straps instead of the port's menu being the game's
 * alone. The patch is per language set (the target's inner path), so a mod's
 * file for all sets is patched once for each set it is asked in; the patched
 * copy lives in content-cache/guard/<mod>/port-patched/<inner path>. Whether
 * the patch lands whole was decided before (guard_port_patched: all or
 * nothing). The same road takes a mod's SHEET whose game sheet the port
 * retouches (gamemenu.png: the PORT CREDITS strap's place cleared,
 * shim_port_pixels_file) - a sheet the retouch does not fit stays the mod's.
 */

#define PATCHED_MAX 64
typedef struct { const ufile_t *hit; char inner[320]; ufile_t *copy; int verdict; } patched_t;
static patched_t g_patched[PATCHED_MAX];
static int g_npatched;

/* the mod's .anm2 for this inner path with the port's patch over it: the copy, the mod's own (no patch for this
   path), or NULL - the mod's file does not read as an .anm2 and the game's (the port's) is used */
static const ufile_t *port_patched(const char *p, const ufile_t *hit) {
    size_t n = strlen(hit->key);
    int anm = n > 5 && !strcasecmp(hit->key + n - 5, ".anm2"), png = n > 4 && !strcasecmp(hit->key + n - 4, ".png");
    if ((anm && !shim_port_patch_file) || (png && !shim_port_pixels_file) || (!anm && !png)) return hit;
    const char *root = shim_pack_root();
    size_t rn = root ? strlen(root) : 0;
    if (!rn || strncmp(p, root, rn)) return hit;
    const char *inner = p + rn;
    while (*inner == '/') inner++;
    if (strlen(inner) >= sizeof(g_patched[0].inner)) return hit;
    pthread_mutex_lock(&g_guard_lock);
    patched_t *pt = NULL;
    for (int i = 0; i < g_npatched && !pt; i++)
        if (g_patched[i].hit == hit && !strcasecmp(g_patched[i].inner, inner)) pt = &g_patched[i];
    if (!pt && g_npatched < PATCHED_MAX) {
        pt = &g_patched[g_npatched];
        memset(pt, 0, sizeof(*pt));
        pt->hit = hit;
        snprintf(pt->inner, sizeof(pt->inner), "%s", inner);
        long len = 0;
        char *buf = slurp(hit->path, &len);
        void *out = NULL;
        long outn = 0;
        char why[400] = "";
        /* a sheet for all sets is drawn like data's: data's retouch over it (dlc3.de's plaque is smaller -
           its retouch would leave part of a data-drawn plaque standing) */
        char pin[400];
        snprintf(pin, sizeof(pin), "%s", inner);
        if (png && hit->key[0] == '|' && !strncmp(inner, "repentance-resources/", 21)) {
            const char *rest = strchr(inner + 21, '/');
            if (rest) snprintf(pin, sizeof(pin), "repentance-resources/data%s", rest);
        }
        int r = !buf ? 0 : anm ? shim_port_patch_file(inner, buf, len, &out, &outn, why, sizeof(why))
                         : shim_port_pixels_file(pin, buf, len, &out, &outn, why, sizeof(why));
        /* a sheet the retouch does not fit is the mod's as it is - never the game's instead */
        if (png && r < 0) {
            NOTE("guard %s: %s - %s", owner_id(hit), inner, why);
            r = 0;
        }
        free(buf);
        pt->verdict = r;
        if (r == 1) {
            char path[1400];
            snprintf(path, sizeof(path), "%s/%s/port-patched/%s", g_guard_dir, owner_id(hit), inner);
            char *slash = strrchr(path, '/');
            *slash = 0;
            mkdirs(path);
            *slash = '/';
            long have_n = -1;
            char *have = slurp(path, &have_n);
            int same = have && have_n == outn && !memcmp(have, out, (size_t) outn);
            free(have);
            FILE *f = same ? NULL : fopen(path, "wb");
            int ok = same || (f && fwrite(out, 1, (size_t) outn, f) == (size_t) outn);
            if (f && fclose(f)) ok = 0;
            ufile_t *c = ok ? (ufile_t *) calloc(1, sizeof(ufile_t) + strlen(hit->key)) : NULL;
            if (c) {
                *c = *hit;
                memcpy(c->key, hit->key, strlen(hit->key) + 1);
                c->path = strdup(path);
                c->next = NULL;
                pt->copy = c;
            } else pt->verdict = 0;     /* not written: the mod's file as it is */
            NOTE("guard %s: %s - the port's additions laid over the mod's file%s%s%s", owner_id(hit), inner,
                 c ? "" : " - could NOT be written, the mod's file as it is", why[0] ? ": " : "", why);
        } else if (r < 0) {
            __android_log_print(ANDROID_LOG_WARN, "isaac", "usermods: %s %s: no .anm2 to lay the port's additions over - "
                                "the game's file is used", owner_id(hit), inner);
            NOTE("guard %s: %s does not read as an .anm2 for the port's additions - the game's file is used",
                 owner_id(hit), inner);
        }
        free(out);
        g_npatched++;
    }
    const ufile_t *answer = !pt ? hit : pt->verdict == 1 ? pt->copy : pt->verdict < 0 ? NULL : hit;
    pthread_mutex_unlock(&g_guard_lock);
    return answer;
}

/*
 * THE GAME'S FRAMES FITTED TO A MOD'S k-TIMES PICTURE (hotfix 1.71; the owner,
 * 05.10.2026: what worked in 1.666 works the same or better, and a mod is
 * refused a file only when that file would crash the game). A mod - a PC one
 * dropped into mods/ by hand above all - brings a picture at k times the game's
 * size (Reflashed: x4) without the .anm2 that cuts it. The game's frames cut
 * its top-left 1/k: 1.666 and the PC show that. 1.7 handed the game's picture
 * instead for the files the port draws by number and for its menus - and the
 * mod's OWN .anm2 cutting the same picture at x4 (Reflashed's challengemenu)
 * then cut a 1x picture. Now no picture is refused for its size. The .anm2 the
 * game asks for, when no mod brings it, is handed FITTED: its frames on that
 * sheet with crops, sizes and pivots times k and scale over k - the same on the
 * screen, the mod's art whole; the mod's own .anm2 get the very same picture.
 * The converter does the same thing by baking the picture down (ADD MOD); here
 * the picture stays as the mod made it. The port's patch goes on first (the
 * menus), so its rows and straps stay where they are. A k that is not whole on
 * both sides is left as it is (the PC's way). Worked out at the first ask of
 * each .anm2 and kept in content-cache/guard/<mod>/kfit/.
 */
#define KFIT_BUCKETS 1024
typedef struct kfit { struct kfit *next; ufile_t *copy; char inner[1]; } kfit_t;
static kfit_t *g_kfit[KFIT_BUCKETS];
static int g_kfit_made;

/* a png's size from its first 24 bytes (memory) */
static int kfit_png_mem(const void *b, long n, unsigned *w, unsigned *h) {
    return b && n >= 24 && png_size((const unsigned char *) b, 24, w, h);
}

/* the k a mod's picture is of the game's, both sides whole (2..KFIT_K_MAX), else 0; kept on the entry */
static int kfit_k_of(ufile_t *e, const char *sheet_inner) {
    if (e->kx) return e->kx > 0 ? e->kx : 0;
    e->kx = -1;
    unsigned char h[24];
    FILE *f = fopen(e->path, "rb");
    size_t n = f ? fread(h, 1, sizeof(h), f) : 0;
    if (f) fclose(f);
    unsigned mw, mh, gw, gh;
    if (!png_size(h, n, &mw, &mh)) return 0;
    long gn = 0;
    const void *g = shim_pack_peek(sheet_inner, &gn);
    if (!kfit_png_mem(g, gn, &gw, &gh)) {
        /* the language set has none of its own: the game's data copy */
        const char *slash = strncmp(sheet_inner, "repentance-resources/", 21) ? NULL : strchr(sheet_inner + 21, '/');
        if (!slash) return 0;
        char data[600];
        snprintf(data, sizeof(data), "repentance-resources/data%s", slash);
        g = shim_pack_peek(data, &gn);
        if (!kfit_png_mem(g, gn, &gw, &gh)) return 0;
    }
    if (!gw || !gh || mw % gw || mh % gh) return 0;
    unsigned k = mw / gw;
    if (k < 2 || k > KFIT_K_MAX || mh / gh != k) return 0;
    e->kx = (signed char) k;
    return (int) k;
}

/* "a/b/../c.png" -> "a/c.png", in place (at most 600 bytes) */
static void kfit_norm(char *p) {
    char buf[600];
    snprintf(buf, sizeof(buf), "%s", p);
    for (char *c = buf; *c; c++) if (*c == 92) *c = '/';
    const char *seg[64];
    size_t len[64];
    int n = 0;
    for (const char *c = buf; *c && n < 64; ) {
        const char *e = strchr(c, '/');
        size_t l = e ? (size_t) (e - c) : strlen(c);
        if (l == 2 && c[0] == '.' && c[1] == '.') { if (n) n--; }
        else if (l && !(l == 1 && c[0] == '.')) { seg[n] = c; len[n] = l; n++; }
        c += l;
        if (*c == '/') c++;
    }
    size_t at = 0;
    p[0] = 0;
    for (int i = 0; i < n && at < 590; i++) at += (size_t) snprintf(p + at, 600 - at, "%s%.*s", i ? "/" : "", (int) len[i], seg[i]);
}

/* the mod's whole-k picture standing for this sheet path of the .anm2 at inner (dir ends in '/'), or NULL */
static ufile_t *kfit_sheet(const char *dir, const char *path, size_t pathn, int *k) {
    const char *root = shim_pack_root();
    char rel[600];
    snprintf(rel, sizeof(rel), "%s%.*s", dir, (int) pathn, path);
    kfit_norm(rel);
    char full[1200];
    snprintf(full, sizeof(full), "%s%s%s", root, root[0] && root[strlen(root) - 1] != '/' ? "/" : "", rel);
    ufile_t *e = (ufile_t *) usermods_find(full, 1);
    if (!e || e->owner >= MODS_MAX || e->guard == GUARD_REFUSED) return NULL;
    size_t pl = strlen(e->path);
    if (pl < 4 || strcasecmp(e->path + pl - 4, ".png")) return NULL;
    *k = kfit_k_of(e, rel);
    return *k ? e : NULL;
}

/* a number attribute's new text: times m */
static void kfit_num(edits_t *ed, const xel_t *x, const char *attr, double m, double def) {
    span_t v = x_attr(x, attr);
    double was = def;
    if (v.s) {
        char t[48];
        size_t k = (size_t) (v.e - v.s) < sizeof(t) - 1 ? (size_t) (v.e - v.s) : sizeof(t) - 1;
        memcpy(t, v.s, k);
        t[k] = 0;
        was = strtod(t, NULL);
    }
    char o[64];
    snprintf(o, sizeof(o), "%.6g", was * m);
    if (v.s) ed_add(ed, v.s, v.e, o, strlen(o));
    else {
        char a[96];
        int n = snprintf(a, sizeof(a), " %s=\"%s\"", attr, o);
        ed_add(ed, x->attrs.e, x->attrs.e, a, (size_t) n);
    }
}

/*
 * The fitted copy of the .anm2 at inner (no mod brings it), or NULL - none of
 * its sheets is a mod's whole-k picture. Called with g_guard_lock held.
 */
static ufile_t *kfit_make(const char *inner) {
    long gn = 0;
    const char *g = (const char *) shim_pack_peek(inner, &gn);
    if (!g || gn <= 0) return NULL;
    char dir[400];
    snprintf(dir, sizeof(dir), "%s", inner);
    char *slash = strrchr(dir, '/');
    if (slash) slash[1] = 0; else dir[0] = 0;
    /* a quick look first, on the text: is any sheet of it a mod's whole-k picture at all (most are not) */
    int any = 0, k0 = 0;
    for (const char *c = g, *end = g + gn; !any && c < end; ) {
        const char *q = memchr(c, 'P', (size_t) (end - c));
        if (!q || end - q < 7) break;
        c = q + 1;
        if (memcmp(q, "Path=\"", 6)) continue;
        const char *v = q + 6, *ve = memchr(v, '"', (size_t) (end - v));
        if (!ve) break;
        if (kfit_sheet(dir, v, (size_t) (ve - v), &k0)) any = 1;
    }
    if (!any) return NULL;
    anm_t a;
    if (anm_open(&a, g, gn)) { free(a.d.v); return NULL; }
    long fit_id[16];
    int fit_k[16], nfit = 0, owner = -1;
    for (int s = a.sheets >= 0 ? x_child(&a.d, a.sheets, "Spritesheet") : -1; s >= 0 && nfit < 16;
         s = x_next(&a.d, a.sheets, "Spritesheet", s)) {
        span_t pv = x_attr(&a.d.v[s], "Path");
        int k = 0;
        ufile_t *e = pv.s ? kfit_sheet(dir, pv.s, (size_t) (pv.e - pv.s), &k) : NULL;
        if (!e) continue;
        fit_id[nfit] = xml_num(&a.d.v[s], "Id", -1);
        fit_k[nfit++] = k;
        if (owner < 0) owner = e->owner;
    }
    if (!nfit) { free(a.d.v); return NULL; }
    /* the port's patch first (its menus), then the frames on those sheets fitted */
    const char *base = g;
    long bn = gn;
    void *patched = NULL;
    long pn = 0;
    if (shim_port_patch_file) {
        char why[300] = "";
        if (shim_port_patch_file(inner, g, gn, &patched, &pn, why, sizeof(why)) == 1 && patched) {
            base = (const char *) patched;
            bn = pn;
        }
    }
    free(a.d.v);
    if (anm_open(&a, base, bn)) { free(a.d.v); free(patched); return NULL; }
    edits_t ed = {0};
    int frames = 0;
    for (int an = a.anims >= 0 ? x_child(&a.d, a.anims, "Animation") : -1; an >= 0; an = x_next(&a.d, a.anims, "Animation", an)) {
        int las = x_child(&a.d, an, "LayerAnimations");
        for (int la = las >= 0 ? x_child(&a.d, las, "LayerAnimation") : -1; la >= 0; la = x_next(&a.d, las, "LayerAnimation", la)) {
            long lid = xml_num(&a.d.v[la], "LayerId", -1);
            int li = anm_entry(&a, a.layers, "Layer", lid);
            if (li < 0) continue;
            long sid = xml_num(&a.d.v[li], "SpritesheetId", -1);
            int k = 0;
            for (int i = 0; i < nfit; i++) if (fit_id[i] == sid) k = fit_k[i];
            if (!k) continue;
            for (int f = x_child(&a.d, la, "Frame"); f >= 0; f = x_next(&a.d, la, "Frame", f)) {
                const xel_t *x = &a.d.v[f];
                kfit_num(&ed, x, "XCrop", k, 0);
                kfit_num(&ed, x, "YCrop", k, 0);
                kfit_num(&ed, x, "Width", k, 0);
                kfit_num(&ed, x, "Height", k, 0);
                kfit_num(&ed, x, "XPivot", k, 0);
                kfit_num(&ed, x, "YPivot", k, 0);
                kfit_num(&ed, x, "XScale", 1.0 / k, 100);
                kfit_num(&ed, x, "YScale", 1.0 / k, 100);
                frames++;
            }
        }
    }
    long outn = 0;
    char *out = frames ? ed_apply(&ed, base, bn, &outn) : NULL;
    ed_free(&ed);
    free(a.d.v);
    free(patched);
    if (!out) return NULL;
    if (!g_guard_dir[0]) snprintf(g_guard_dir, sizeof(g_guard_dir), "%s/../content-cache/guard", shim_pack_root());
    char path[1400];
    snprintf(path, sizeof(path), "%s/%s/kfit/%s", g_guard_dir, g_owner[owner], inner);
    char *ps = strrchr(path, '/');
    *ps = 0;
    mkdirs(path);
    *ps = '/';
    long have_n = -1;
    char *have = slurp(path, &have_n);
    int same = have && have_n == outn && !memcmp(have, out, (size_t) outn);
    free(have);
    FILE *f = same ? NULL : fopen(path, "wb");
    int ok = same || (f && fwrite(out, 1, (size_t) outn, f) == (size_t) outn);
    if (f && fclose(f)) ok = 0;
    free(out);
    if (!ok) {
        NOTE("kfit %s: %s could NOT be written - the game's frames as they are", g_owner[owner], inner);
        return NULL;
    }
    const char *rel = strchr(inner, '/');
    rel = rel && !strncmp(inner, "repentance-resources/", 21) ? strchr(rel + 1, '/') : rel;
    rel = rel ? rel + 1 : inner;
    ufile_t *c = (ufile_t *) calloc(1, sizeof(ufile_t) + strlen(rel) + 1);
    if (!c) return NULL;
    c->path = strdup(path);
    c->src = c->path;
    c->owner = (owner_t) owner;
    c->guard = GUARD_FIXED;
    c->gamelen = gn;
    c->key[0] = '|';
    memcpy(c->key + 1, rel, strlen(rel) + 1);
    g_kfit_made++;
    NOTE("kfit %s: %s - the game's frames fitted to the mod's picture (x%d, %d frames)", g_owner[owner], inner, fit_k[0], frames);
    return c;
}

/* the fitted .anm2 for this absolute path when no mod brings it (THE GAME'S FRAMES FITTED), or NULL */
static const ufile_t *kfit_answer(const char *p) {
    size_t pl = strlen(p);
    if (pl < 6 || strcasecmp(p + pl - 5, ".anm2") || !g_indexed || !g_count) return NULL;
    const char *root = shim_pack_root();
    size_t rn = root ? strlen(root) : 0;
    if (!rn || strncmp(p, root, rn)) return NULL;
    const char *inner = p + rn;
    while (*inner == '/') inner++;
    unsigned b = hash_of(inner) % KFIT_BUCKETS;
    pthread_mutex_lock(&g_guard_lock);
    kfit_t *k = g_kfit[b];
    while (k && strcmp(k->inner, inner)) k = k->next;
    if (!k) {
        size_t n = strlen(inner);
        k = (kfit_t *) calloc(1, sizeof(kfit_t) + n);
        if (k) {
            memcpy(k->inner, inner, n + 1);
            k->copy = kfit_make(inner);
            k->next = g_kfit[b];
            g_kfit[b] = k;
        }
    }
    const ufile_t *answer = k ? k->copy : NULL;
    pthread_mutex_unlock(&g_guard_lock);
    return answer;
}

/*
 * THE TITLE SCREEN'S iOS SHEET (plan 30.09, M9). The title asks for
 * TitleMenu_iOS.png when the last input device is the touch pseudo-device
 * 0x10dead, TitleMenu.png otherwise (measured in the image: Repentance's
 * 0x35dffc..0x35e050 and 0x59745c..0x5974dc compare [0xac0bb0]+0x34 with
 * 0x10dead and replace the sheet by one or the other - the latter sheets 0
 * and 1 both). A PC mod brings TitleMenu.png, never the iOS one, so on a
 * phone played by touch its title came out as the game's 1x iOS sheet cut by
 * the mod's anm2 (Reflashed's is 4x): shreds. The rule:
 *   - a mod's own TitleMenu_iOS.png wins as any file does;
 *   - else a mod's TitleMenu.png stands in for it (what the same mod shows
 *     with a pad - the converter, tools/modport.py, puts the very same copy
 *     under the iOS name);
 *   - else, when a mod brings the title's anm2 alone, the game's TitleMenu.png
 *     (of the same language set, else data's): the mod's frames are cut for
 *     the PC's sheet, which the iOS one is not;
 *   - no mod touching the title: the game's own iOS sheet, as ever.
 * The iOS sheet differs from the other only in its line of text (10..309 x
 * 499..531 of 480x540: the touch prompt), which a mod's title then lacks.
 */
#define TITLE_DIR "gfx/ui/main menu/"
static pthread_mutex_t g_title_lock = PTHREAD_MUTEX_INITIALIZER;

/* p with its leaf (after the last '/') replaced: into out; 0 when it does not fit */
static int with_leaf(const char *p, const char *leaf, char *out, size_t room) {
    const char *slash = strrchr(p, '/');
    if (!slash) return 0;
    int n = snprintf(out, room, "%.*s/%s", (int) (slash - p), p, leaf);
    return n > 0 && (size_t) n < room;
}

/* is p <root><edition>-resources/<set>/gfx/ui/main menu/titlemenu_ios.png */
static int title_ios_path(const char *p) {
    static const char LEAF[] = "/" TITLE_DIR "titlemenu_ios.png";
    size_t n = strlen(p), k = sizeof(LEAF) - 1;
    const char *root = shim_pack_root();
    size_t rn = root ? strlen(root) : 0;
    return rn && n > k + rn && !strncmp(p, root, rn) && !strcasecmp(p + n - k, LEAF);
}

/* the game's TitleMenu.png for this iOS path: the same set's, else the edition's data/; a path kept for good */
static const char *title_game_png(const char *p) {
    static char *kept[24];
    static int nkept;
    const char *root = shim_pack_root();
    size_t rn = strlen(root);
    char inner[700], full[1400];
    if (!with_leaf(p + rn, "titlemenu.png", inner, sizeof(inner))) return NULL;
    if (!shim_pack_has(inner)) {
        /* <edition>/<set>/... -> <edition>/data/... */
        const char *s1 = strchr(inner, '/'), *s2 = s1 ? strchr(s1 + 1, '/') : NULL;
        if (!s2) return NULL;
        char data[700];
        snprintf(data, sizeof(data), "%.*s/data%s", (int) (s1 - inner), inner, s2);
        if (!shim_pack_has(data)) return NULL;
        snprintf(inner, sizeof(inner), "%s", data);
    }
    snprintf(full, sizeof(full), "%s%s", root, inner);
    pthread_mutex_lock(&g_title_lock);
    const char *got = NULL;
    for (int i = 0; i < nkept && !got; i++) if (!strcmp(kept[i], full)) got = kept[i];
    if (!got && nkept < (int) (sizeof(kept) / sizeof(kept[0]))) got = kept[nkept++] = strdup(full);
    pthread_mutex_unlock(&g_title_lock);
    return got;
}

/*
 * The answer for a TitleMenu_iOS.png no mod brings: 1 and *hit = a mod's
 * TitleMenu.png; 2 and *game = the game's TitleMenu.png (a mod's title anm2
 * alone); 0 - no mod touches the title. quiet: no guard work (peek).
 */
static int title_ios_rule(const char *p, const ufile_t **hit, const char **game, int quiet) {
    char png[1400], anm[1400];
    *hit = NULL; *game = NULL;
    if (!title_ios_path(p) || !with_leaf(p, "titlemenu.png", png, sizeof(png))
        || !with_leaf(p, "titlemenu.anm2", anm, sizeof(anm))) return 0;
    const ufile_t *h = usermods_find(png, 1);
    for (int k = 0; !quiet && k < 2 && h && h->guard == GUARD_PNG_LATER; k++) {
        guard_png((ufile_t *) h);
        if (h->guard == GUARD_REFUSED) h = usermods_find(png, 1);
    }
    if (h) { *hit = h; return 1; }
    if (usermods_find(anm, 1)) { *game = title_game_png(p); return *game ? 2 : 0; }
    return 0;
}

/*
 * shim_map_path (uikit.c) asks this about every absolute path. A resource of
 * the game's, held by an enabled mod, comes back as the mod's file; anything
 * else as NULL. stringtable.sta is content.c's with no mod on too: the port's
 * own keys are filled into the game's file there (M8: the pack holds the game's).
 */
const char *shim_usermods_map(const char *p) {
    if (!p) return NULL;
#if ISAAC_DIAG
    dance_note(p);
#endif
    list_tick();
    const ufile_t *hit = NULL;
    /* the stacked set checked before the first file of it is handed out (THE GUARD); the tables built */
    if (!g_guard_ran) guard_start();
    /* a data table some mod touches (or stringtable.sta): what content.c built - or the game's own file */
    int game_own = 0;
    const char *leaf = data_leaf(p);
    if (leaf) {
        const char *built = content_map(leaf, &game_own);
        /* the base mod's own file, untouched: its index entry (the crash note names it); else the built copy */
        const ufile_t *own = built && g_indexed ? find_entry("", leaf) : NULL;
        if (built) hit = own && !strcmp(own->src, built) ? own : content_slot(built);
#if ISAAC_DIAG
        const ufile_t *whole = (built || game_own) && g_indexed ? find_entry("", leaf) : NULL;
        if (whole) legacy_note(leaf, whole, "repentance-resources/data/");
#endif
    }
    if (!hit && !game_own && g_indexed) hit = usermods_lookup(p);
    /* a picture read through whole at its first use; refused, the next answer (or the game's) */
    for (int k = 0; k < 2 && hit && hit->guard == GUARD_PNG_LATER; k++) {
        guard_png((ufile_t *) hit);
        if (hit->guard == GUARD_REFUSED) hit = usermods_lookup(p);
    }
    /* the title's iOS sheet no mod brings: the mod's TitleMenu.png, or the game's when it brings the anm2 alone (M9) */
    if (!hit && !leaf && g_indexed) {
        const char *game = NULL;
        int t = title_ios_rule(p, &hit, &game, 0);
        if (t == 2) {
            static int said;
            if (!said++) NOTE("title: %s - a mod's TitleMenu.anm2 without its sheets: the game's TitleMenu.png", p);
            reading_note(p, NULL);
            return game;
        }
        if (t == 1) {
            static int said;
            if (!said++) NOTE("title: %s - the mod's TitleMenu.png stands in for the iOS sheet (%s)", p, hit->path);
        }
    }
    if (hit && hit->owner < MODS_MAX) hit = port_patched(p, hit);
    /* an .anm2 no mod brings, cutting a mod's picture of k times the game's: its frames fitted (hotfix 1.71) */
    if (!hit && !leaf && !game_own) hit = kfit_answer(p);
    if (hit) g_swapped++;
    reading_note(p, hit);
#if ISAAC_DIAG
    ask_note(p, hit);
#endif
    return hit ? hit->path : NULL;
}

/*
 * The same answer with nothing done (portpatch.c, M8): which mod file would win
 * for this path, or NULL - no guard run, no counts, no notes. Before the guard
 * has run a file is the mod's own (nothing is refused or completed yet).
 */
const char *shim_usermods_peek(const char *p) {
    if (!p) return NULL;
    int game_own = 0;
    const char *leaf = data_leaf(p);
    if (leaf) {
        const char *built = content_map(leaf, &game_own);
        if (built) return built;
        if (game_own) return NULL;
    }
    if (!g_indexed) return NULL;
    const ufile_t *hit = usermods_find(p, 1);
    /* the title's iOS sheet (M9): a mod's TitleMenu.png stands in; the game's own is no mod's file */
    if (!hit && !leaf) {
        const char *game = NULL;
        if (title_ios_rule(p, &hit, &game, 1) != 1) hit = NULL;
    }
    if (!hit && !leaf) hit = kfit_answer(p);
    return hit ? hit->path : NULL;
}

/* ------------------------------------------------------------ a crash right after a mod's file (M10) */
/*
 * Every thread's last sixteen mod files (plan 02.10, item 1: the measurement
 * that needs no new pass by the owner): a ring per thread, sixteen threads,
 * written on each file call while mods are on, read by the signal handler of
 * the thread that died. The handler writes <mods>/last-crash.txt in any build:
 * the signal, pc and lr as offsets into the image (lr = image+0x8ca2ac is
 * libpng's png_error calling through a clobbered error_fn - round 2), the
 * thread's name, its ring and the newest file of every other thread.
 *
 * A mod is BLAMED - its MODS page gets the line - only as before: the dying
 * thread read its file in the last three seconds and no file of the game's
 * since (a file is parsed as soon as it is read; a crash minutes later is not
 * "while reading" it). The handler goes in at the first mod file handed out -
 * after the report's own (loader.c), which it passes on to - so a player with
 * no mods runs with nothing of this. Nothing in the handler allocates.
 */
#define READERS 16
#define RING 16
typedef struct {
    volatile int tid;
    volatile unsigned n;                    /* files put in, ever: the newest is ring[(n - 1) % RING] */
    volatile long long pack_at;             /* the last file of the game's (not a mod's) this thread asked for */
    struct { const ufile_t *volatile f; volatile long long at; } ring[RING];
} reader_t;
static reader_t g_reader[READERS];
#define CRASH_WINDOW_MS 3000                /* a file is parsed right after it is read: older is not "while reading" */
static volatile int g_reading_on;           /* some mod can answer: files are watched */
static volatile int g_crash_armed;
static char g_crash_file[640];              /* <mods>/last-crash.txt */
static const int CRASH_SIGNALS[] = { SIGSEGV, SIGBUS, SIGILL, SIGFPE, SIGABRT };
static const char *const CRASH_NAMES[] = { "SIGSEGV", "SIGBUS", "SIGILL", "SIGFPE", "SIGABRT" };
#define CRASH_NSIG ((int) (sizeof(CRASH_SIGNALS) / sizeof(CRASH_SIGNALS[0])))
static struct sigaction g_crash_prev[CRASH_NSIG];
static char g_crash_text[8192];

static size_t put_text(char *dst, size_t at, size_t cap, const char *s) {
    while (s && *s && at + 1 < cap) dst[at++] = *s++;
    dst[at] = 0;
    return at;
}

static size_t put_num(char *dst, size_t at, size_t cap, long long v) {
    char tmp[24];
    int k = 0, neg = v < 0;
    unsigned long long u = neg ? (unsigned long long) -v : (unsigned long long) v;
    do { tmp[k++] = (char) ('0' + u % 10); u /= 10; } while (u && k < 22);
    if (neg) tmp[k++] = '-';
    while (k && at + 1 < cap) dst[at++] = tmp[--k];
    dst[at] = 0;
    return at;
}

static size_t put_hex(char *dst, size_t at, size_t cap, unsigned long v) {
    char tmp[20];
    int k = 0;
    do { tmp[k++] = "0123456789abcdef"[v & 15]; v >>= 4; } while (v && k < 18);
    at = put_text(dst, at, cap, "0x");
    while (k && at + 1 < cap) dst[at++] = tmp[--k];
    dst[at] = 0;
    return at;
}

/* an address as an offset into the image, the way every report of the port names one */
static size_t put_addr(char *dst, size_t at, size_t cap, unsigned long a) {
    unsigned long b = shim_image_base();
    if (b && a >= b && a - b < 0xb20000) {
        at = put_text(dst, at, cap, "+");
        return put_hex(dst, at, cap, a - b);
    }
    at = put_hex(dst, at, cap, a);
    return put_text(dst, at, cap, " (outside the image)");
}

static const char *ring_owner(const ufile_t *f) {
    return f->owner == OWNER_CONTENT ? "*content*" : f->owner < MODS_MAX ? g_owner[f->owner] : "?";
}

static size_t put_ring_entry(char *dst, size_t at, size_t cap, const ufile_t *f, long long at_ms, long long now) {
    at = put_text(dst, at, cap, "  -");
    at = put_num(dst, at, cap, now - at_ms);
    at = put_text(dst, at, cap, " ms  ");
    at = put_text(dst, at, cap, ring_owner(f));
    at = put_text(dst, at, cap, "  ");
    at = put_text(dst, at, cap, f->src);
    return put_text(dst, at, cap, f->path != f->src ? "  (the port's completed copy)\n" : "\n");
}

static void crash_write(int signo, siginfo_t *info, void *ctx) {
    int tid = gettid();
    long long now = now_ms();
    reader_t *me = NULL;
    for (int i = 0; i < READERS; i++) if (g_reader[i].tid == tid) me = &g_reader[i];
    const ufile_t *last = NULL;
    long long last_at = 0;
    if (me && me->n) {
        unsigned k = (me->n - 1) % RING;
        last = me->ring[k].f;
        last_at = me->ring[k].at;
    }
    int blamed = last && last->src && now - last_at <= CRASH_WINDOW_MS && last_at >= me->pack_at;
    size_t n = 0, cap = sizeof(g_crash_text);
    char *t = g_crash_text;
    if (blamed) {
        /* the two lines the next start reads back for the mod's MODS page (crash_read) */
        n = put_text(t, n, cap, "mod ");
        n = put_text(t, n, cap, ring_owner(last));
        n = put_text(t, n, cap, "\nfile ");
        n = put_text(t, n, cap, last->src);
        n = put_text(t, n, cap, "\nThe game closed right after reading this file of the mod.\n");
    } else {
        n = put_text(t, n, cap, "The game closed; the mods' files below are the last ones read before it "
                                "(none of them right before the crash on the thread that died).\n");
    }
    int k = 0;
    while (k < CRASH_NSIG && CRASH_SIGNALS[k] != signo) k++;
    n = put_text(t, n, cap, "signal ");
    n = put_num(t, n, cap, signo);
    n = put_text(t, n, cap, k < CRASH_NSIG ? " (" : "");
    n = put_text(t, n, cap, k < CRASH_NSIG ? CRASH_NAMES[k] : "");
    n = put_text(t, n, cap, k < CRASH_NSIG ? ")" : "");
    if (info) {
        n = put_text(t, n, cap, " code ");
        n = put_num(t, n, cap, info->si_code);
        n = put_text(t, n, cap, " at ");
        n = put_hex(t, n, cap, (unsigned long) info->si_addr);
    }
    n = put_text(t, n, cap, "\n");
#if defined(__aarch64__) && defined(__ANDROID__)
    if (ctx) {
        const ucontext_t *uc = (const ucontext_t *) ctx;
        n = put_text(t, n, cap, "pc ");
        n = put_addr(t, n, cap, (unsigned long) uc->uc_mcontext.pc);
        n = put_text(t, n, cap, "\nlr ");
        n = put_addr(t, n, cap, (unsigned long) uc->uc_mcontext.regs[30]);
        n = put_text(t, n, cap, "\n");
    }
#else
    (void) ctx;
#endif
    n = put_text(t, n, cap, "image base ");
    n = put_hex(t, n, cap, shim_image_base());
    n = put_text(t, n, cap, "\nthread ");
    n = put_num(t, n, cap, tid);
#if defined(__ANDROID__)
    char name[20] = "";
    if (!prctl(PR_GET_NAME, (unsigned long) name, 0, 0, 0)) {
        name[16] = 0;
        n = put_text(t, n, cap, " \"");
        n = put_text(t, n, cap, name);
        n = put_text(t, n, cap, "\"");
    }
#endif
    n = put_text(t, n, cap, "\nthis thread's last mod files, the newest first");
    if (me && me->pack_at) {
        n = put_text(t, n, cap, " (its last file of the game's: -");
        n = put_num(t, n, cap, now - me->pack_at);
        n = put_text(t, n, cap, " ms)");
    }
    n = put_text(t, n, cap, ":\n");
    if (me)
        for (unsigned i = 0; i < RING && i < me->n; i++) {
            unsigned k2 = (me->n - 1 - i) % RING;
            const ufile_t *f = me->ring[k2].f;
            if (f && f->src) n = put_ring_entry(t, n, cap, f, me->ring[k2].at, now);
        }
    n = put_text(t, n, cap, "the other threads' newest mod file:\n");
    for (int i = 0; i < READERS; i++) {
        reader_t *r = &g_reader[i];
        if (!r->tid || r == me || !r->n) continue;
        unsigned k2 = (r->n - 1) % RING;
        const ufile_t *f = r->ring[k2].f;
        if (!f || !f->src) continue;
        n = put_text(t, n, cap, "  thread ");
        n = put_num(t, n, cap, r->tid);
        n = put_text(t, n, cap, ":");
        n = put_ring_entry(t, n, cap, f, r->ring[k2].at, now);
    }
    int fd = open(g_crash_file, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0600);
    if (fd >= 0) {
        ssize_t ignored = write(fd, g_crash_text, n);
        (void) ignored;
        close(fd);
    }
#if ISAAC_DIAG
    if (shim_report_active()) {
        /* the report's writer formats only "%s: %s": no allocation on the way */
        report_line("crash", g_crash_text);
    }
#endif
}

static void crash_on_signal(int signo, siginfo_t *info, void *ctx) {
    static volatile int once;
    if (!__sync_lock_test_and_set(&once, 1)) crash_write(signo, info, ctx);
    int k = 0;
    while (k < CRASH_NSIG && CRASH_SIGNALS[k] != signo) k++;
    if (k == CRASH_NSIG) { signal(signo, SIG_DFL); raise(signo); return; }
    /* the handler that was there before: the report's (loader.c), or the system's */
    struct sigaction prev = g_crash_prev[k];
    sigaction(signo, &prev, NULL);
    if ((prev.sa_flags & SA_SIGINFO) && prev.sa_sigaction) { prev.sa_sigaction(signo, info, ctx); return; }
    if (prev.sa_handler != SIG_DFL && prev.sa_handler != SIG_IGN) { prev.sa_handler(signo); return; }
    signal(signo, SIG_DFL);
    raise(signo);
}

static void crash_install(void) {
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_sigaction = crash_on_signal;
    sa.sa_flags = SA_SIGINFO;
    sigemptyset(&sa.sa_mask);
    for (int k = 0; k < CRASH_NSIG; k++) sigaction(CRASH_SIGNALS[k], &sa, &g_crash_prev[k]);
    g_crash_armed = 1;
}

static void reading_note(const char *p, const ufile_t *hit) {
    if (!g_reading_on) return;
    if (!hit) {
        /* only a resource counts: a save or a setting read in between says nothing */
        const char *root = shim_pack_root();
        size_t rn = root ? strlen(root) : 0;
        if (!rn || strncmp(p, root, rn)) return;
    }
    int tid = gettid();
    reader_t *me = NULL;
    for (int i = 0; i < READERS; i++) if (g_reader[i].tid == tid) { me = &g_reader[i]; break; }
    if (!me && hit)
        for (int i = 0; i < READERS; i++)
            if (!g_reader[i].tid && __sync_bool_compare_and_swap(&g_reader[i].tid, 0, tid)) { me = &g_reader[i]; break; }
    if (me) {
        long long t = now_ms();
        if (hit) {
            unsigned k = me->n % RING;
            me->ring[k].f = NULL;                   /* the slot is being written: the handler skips it */
            me->ring[k].at = t;
            me->ring[k].f = hit;
            me->n = me->n + 1;
        } else {
            me->pack_at = t;
        }
    }
    if (hit && !g_crash_armed) {
        static pthread_once_t once = PTHREAD_ONCE_INIT;
        pthread_once(&once, crash_install);
    }
}

/* the last run's marker: to its mod's note on the MODS page, then put aside */
static void crash_read(void) {
    FILE *f = fopen(g_crash_file, "r");
    if (!f) return;
    char line[900], id[ID_MAX] = "", file[800] = "";
    while (fgets(line, sizeof(line), f)) {
        line[strcspn(line, "\r\n")] = 0;
        if (!strncmp(line, "mod ", 4)) snprintf(id, sizeof(id), "%s", line + 4);
        else if (!strncmp(line, "file ", 5)) snprintf(file, sizeof(file), "%s", line + 5);
    }
    fclose(f);
    int tied = 0;                           /* said in the report build */
    for (int i = 0; i < g_count && file[0]; i++) {
        umod_t *m = &g_mods[i];
        /* a built table (content-cache/data/<leaf>): every mod that brings a file of that table */
        const char *built = strrchr(file, '/') ? strrchr(file, '/') + 1 : file;
        if (strcmp(m->id, id) && !(!strcmp(id, "*content*") && content_mod_has(m->id, built))) continue;
        char mine[700];
        snprintf(mine, sizeof(mine), "%s/%s/", g_dir, m->id);
        if (!strcmp(id, "*content*")) snprintf(m->crash, sizeof(m->crash), "%s (built by the port from the mods)", built);
        else snprintf(m->crash, sizeof(m->crash), "%s", !strncmp(file, mine, strlen(mine)) ? file + strlen(mine) : file);
        tied++;
    }
#if ISAAC_DIAG
    if (shim_report_active()) {
        char text[1000];
        if (file[0])
            snprintf(text, sizeof(text), "the last run closed right after reading %s from mod %s%s",
                     file, id, tied ? " - on its MODS page now" : " - the mod is gone");
        else
            snprintf(text, sizeof(text), "the last run closed with mods on, not right after a mod's file - mods/last-crash.old.txt");
        report_line("crash", text);
    }
#else
    (void) tied;
#endif
    char old[660];
    snprintf(old, sizeof(old), "%s/last-crash.old.txt", g_dir);
    unlink(old);
    if (rename(g_crash_file, old)) unlink(g_crash_file);
}

/* ------------------------------------------------------------ the report's measurements */
#if ISAAC_DIAG
/*
 * THE "ask" LINE (plan 30.09, M11): where the game asked for a file, how, and
 * who answered. It replaces the two probes by name (titlemenu, ios_buttons)
 * that did their work on 28.09. The question behind it is how the engine
 * chooses the edition and the set: by stat/access per set, or by listing a
 * folder - a listing (shim_pack_opendir) is the pack and the disk, never the
 * mods' index, so a mod's file under a set the game has no such file in
 * would be invisible. A line is written for
 *   - a path some mod holds under some set (whoever answers it),
 *   - a resource path nobody has (the engine probing the sets), and
 *   - every opendir under *-resources,
 * once per (call, path), within budgets. The call is named where the pack
 * sees it (pack.c, with the game's return address); a path mapped and never
 * brought to the pack went through NSFileManager (uikit.c: exists, is-dir,
 * a listing) and is written as "fm", with no address.
 */
volatile int g_usermods_ask_armed;          /* pack.c reads it before it tells us the call */
static volatile int g_ask_checked;
enum { ASK_NONE, ASK_INDEX, ASK_MISS, ASK_OTHER };
enum { BUDGET_INDEX, BUDGET_MISS, BUDGET_DIR };
static const int ASK_MAX[3] = { 280, 80, 40 };
static const char *const ASK_WHAT[3] = { "a mod could answer", "nobody has", "opendir" };
static int g_ask_n[3];
static unsigned g_ask_seen[2048];
static pthread_mutex_t g_ask_lock = PTHREAD_MUTEX_INITIALIZER;

typedef struct {
    const char *mapped;                     /* what shim_map_path handed on: the pack sees this pointer */
    int cls;
    char path[400];
    char answer[72];
} ask_t;
static __thread ask_t t_ask;

/* every path some enabled mod holds, under any set */
typedef struct anykey { struct anykey *next; char key[1]; } anykey_t;
static anykey_t *g_anyset[BUCKETS];

static void anyset_add(const char *key) {
    unsigned k = hash_of(key) % BUCKETS;
    for (anykey_t *e = g_anyset[k]; e; e = e->next) if (!strcasecmp(e->key, key)) return;
    size_t n = strlen(key);
    anykey_t *e = (anykey_t *) malloc(sizeof(anykey_t) + n);
    if (!e) return;
    memcpy(e->key, key, n + 1);
    e->next = g_anyset[k];
    g_anyset[k] = e;
}

static int anyset_has(const char *key) {
    for (anykey_t *e = g_anyset[hash_of(key) % BUCKETS]; e; e = e->next) if (!strcasecmp(e->key, key)) return 1;
    return 0;
}

static void ask_emit(const char *kind, const ask_t *a, int budget, unsigned long from) {
    unsigned h = hash_of(a->path) ^ (hash_of(kind) * 31u);
    if (!h) h = 1;
    int last = 0;
    pthread_mutex_lock(&g_ask_lock);
    unsigned slot = h % 2048u;
    int seen = 0;
    for (unsigned k = 0; k < 2048u; k++, slot = (slot + 1) % 2048u) {
        if (g_ask_seen[slot] == h) { seen = 1; break; }
        if (!g_ask_seen[slot]) { g_ask_seen[slot] = h; break; }
    }
    if (seen || g_ask_n[budget] > ASK_MAX[budget]) { pthread_mutex_unlock(&g_ask_lock); return; }
    last = ++g_ask_n[budget] > ASK_MAX[budget];
    pthread_mutex_unlock(&g_ask_lock);
    char line[600];
    if (last) {
        snprintf(line, sizeof(line), "ask: the lines for \"%s\" end here (%d)", ASK_WHAT[budget], ASK_MAX[budget]);
        report_line("usermods", line);
        return;
    }
    char where[40];
    unsigned long b = shim_image_base();
    if (!from) snprintf(where, sizeof(where), "-");
    else if (b && from > b && from - b < 0xb20000) snprintf(where, sizeof(where), "+0x%lx", from - b);
    else snprintf(where, sizeof(where), "outside the image");
    snprintf(line, sizeof(line), "ask %s %s -> %s from %s", kind, a->path, a->answer, where);
    report_line("usermods", line);
}

static void ask_flush(void) {
    ask_t *a = &t_ask;
    if (a->cls == ASK_INDEX || a->cls == ASK_MISS) ask_emit("fm", a, a->cls == ASK_INDEX ? BUDGET_INDEX : BUDGET_MISS, 0);
    a->cls = ASK_NONE;
}

static void ask_note(const char *p, const ufile_t *hit) {
    if (!g_ask_checked) { g_usermods_ask_armed = shim_report_active(); g_ask_checked = 1; }
    if (!g_usermods_ask_armed) return;
    ask_flush();
    const char *root = shim_pack_root();
    size_t rn = root ? strlen(root) : 0;
    if (!rn || strncmp(p, root, rn)) return;
    const char *q = p + rn;
    while (*q == '/') q++;
    /* the part a mod's index is keyed by */
    const char *below = NULL;
    int edition = 0;
    const char *slash = strchr(q, '/');
    if (slash && slash - q >= 10 && !strncmp(slash - 10, "-resources", 10)) {
        edition = 1;
        const char *s2 = strchr(slash + 1, '/');
        below = s2 ? s2 + 1 : NULL;
    } else {
        below = q;
        if (!strncasecmp(below, "resources/", 10)) below += 10;
        if (!strncasecmp(below, "data/", 5)) below += 5;
    }
    ask_t *a = &t_ask;
    a->mapped = hit ? hit->path : p;
    if (hit) {
        a->cls = ASK_INDEX;
        snprintf(a->answer, sizeof(a->answer), "%s", hit->owner == OWNER_CONTENT ? "content-cache"
                 : hit->owner < MODS_MAX ? g_owner[hit->owner] : "?");
    } else {
        int in_pack = shim_pack_peek(q, NULL) != NULL;
        if (below && *below && anyset_has(below)) a->cls = ASK_INDEX;
        else if (edition && !in_pack) a->cls = ASK_MISS;
        else if (edition) a->cls = ASK_OTHER;
        else return;
        snprintf(a->answer, sizeof(a->answer), "%s", in_pack ? "pack" : "none");
    }
    snprintf(a->path, sizeof(a->path), "%s", q);
}

/* pack.c: the call the last mapped path came to, and the game's return address */
void shim_usermods_ask_seen(const char *kind, const char *mapped, unsigned long from) {
    ask_t *a = &t_ask;
    if (a->cls == ASK_NONE || mapped != a->mapped) return;
    int cls = a->cls;
    a->cls = ASK_NONE;
    int dir = !strcmp(kind, "opendir");
    if (dir) {
        if (strcmp(a->answer, "none")) snprintf(a->answer, sizeof(a->answer), "listing");
        ask_emit(kind, a, BUDGET_DIR, from);
    } else if (cls != ASK_OTHER) {
        ask_emit(kind, a, cls == ASK_INDEX ? BUDGET_INDEX : BUDGET_MISS, from);
    }
}

/*
 * THE LEGACY DATA FILES: WHAT A WHOLE REPLACEMENT CHANGES (plan 30.09, M4).
 * A mod that brings a whole data table (resources/players.xml, ...) takes the
 * game's place entirely, as on the PC - and nobody saw that the portraits mod
 * took Cambion Conception away from Lilith. The first time the game is handed
 * such a file, it is compared with the game's own by record: the gameplay
 * values that differ get a line each, the cosmetic ones (portraits, names,
 * pictures) a count, and so do records added and taken away. The mod's own
 * file is compared (src), not the guard's completed copy: the copy has the
 * game's missing records put back, and the records the mod took away would
 * not show. A table past the loaders' fixed arrays never comes here: the
 * guard refuses it (THE LOADERS' FIXED TABLES), and its own line says so.
 */
#define LEGACY_FILE_LINES 40
#define LEGACY_LINES 160
static int g_legacy_lines;
static char g_legacy_done[32][48];
static int g_legacy_ndone;

static const char *const COSMETIC[] = {
    "name", "description", "portrait", "bigportrait", "extraportrait", "skin", "skincolor", "costume",
    "costumeSuffix", "gfx", "anm2path", "nameimage", "gfxroot", "anm2root", "root", "image", "sprite",
    "portraitimage", "bossname", "text", NULL
};

static int cosmetic(span_t n) {
    for (int i = 0; COSMETIC[i]; i++) {
        size_t k = strlen(COSMETIC[i]);
        if ((size_t) (n.e - n.s) == k && !strncasecmp(n.s, COSMETIC[i], k)) return 1;
    }
    return 0;
}

/* a record's key: its parent's, then tag[key value]; the key value by what the loaders look up */
static char **legacy_keys(const xdoc_t *d) {
    char **keys = (char **) calloc((size_t) d->n + 1, sizeof(char *));
    if (!keys) return NULL;
    for (int i = 0; i < d->n; i++) {
        const xel_t *x = &d->v[i];
        if (!x->depth) continue;
        char cls[48], val[96] = "";
        tag_class_of(x, cls, sizeof(cls));
        span_t v;
        if (!strcmp(cls, "entity"))                         /* entities2: type, variant, subtype */
            snprintf(val, sizeof(val), "%ld.%ld.%ld", xml_num(x, "id", -1), xml_num(x, "variant", 0), xml_num(x, "subtype", 0));
        else if (xml_get(x, "id", &v) || xml_get(x, "Id", &v) || xml_get(x, "ID", &v)
                 || xml_get(x, "name", &v) || xml_get(x, "Name", &v)) span_copy(v, val, sizeof(val));
        else {
            int k = 0;
            for (int j = i - 1; j > x->parent && j >= 0; j--) {
                const xel_t *y = &d->v[j];
                char c2[48];
                if (y->parent != x->parent) continue;
                tag_class_of(y, c2, sizeof(c2));
                if (!strcmp(c2, cls)) k++;
            }
            snprintf(val, sizeof(val), "#%d", k);
        }
        const char *up = x->parent > 0 && keys[x->parent] ? keys[x->parent] : NULL;
        size_t need = (up ? strlen(up) + 1 : 0) + strlen(cls) + strlen(val) + 16;
        keys[i] = (char *) malloc(need);
        if (!keys[i]) continue;
        snprintf(keys[i], need, "%s%s%s[%s]", up ? up : "", up ? "/" : "", cls, val);
        /* the same key twice under one parent (an entity's <preload id="1000"> lines): told apart by their order */
        int twin = 0;
        size_t kl = strlen(keys[i]);
        for (int j = i - 1; j > x->parent && j >= 0; j--)
            if (d->v[j].parent == x->parent && keys[j] && !strncmp(keys[j], keys[i], kl)
                    && (keys[j][kl] == 0 || keys[j][kl] == '~')) twin++;
        if (twin) snprintf(keys[i] + kl, need - kl, "~%d", twin);
    }
    return keys;
}

/* 0 a difference (budgeted), 2 a file's summary (always: one a file) */
static void legacy_say(const char *text, int what) {
    if (what == 0 && g_legacy_lines++ >= LEGACY_LINES) return;
    report_line("content", text);
}

/*
 * entities2.xml, what is told and not refused (batch 5, M4): a gibFlags word the
 * loader's table does not have (0x7829b8..0x782df8: blood bone gut eye large
 * poop worm rock rock_small sound_baby sound_bone chain dust huge) is looked up,
 * not found, and the int 0x38 past the map's end node is taken as its bit
 * (0x782ea4: the static at 0xb125d8, a Vector2 of gameplay code) - 14 or more
 * and the bitset throws (0x782eac -> 0x784408), less and some flag is set; and
 * an id, variant or subtype past 12, 12 or 8 bits whose key (id<<20 |
 * variant<<8 | subtype, 0x7822ac) is another record's of the file - the later
 * one takes the earlier's place. (The game's own file is past the bits too -
 * 9001, the pills' subtypes 2049.. - with keys of their own: not told.) The
 * words are split as getline at ' ' does (0x7f218c): "a  b" has an empty word.
 */
static void entities_warn(const char *id, const xdoc_t *d) {
    static const char *const GIBS = "blood|bone|gut|eye|large|poop|worm|rock|rock_small|sound_baby|sound_bone|chain|dust|huge";
    char line[400];
    int root = x_child(d, -1, "entities");
    long (*tvs)[3] = (long (*)[3]) calloc((size_t) d->n + 1, sizeof(long[3]));
    if (!tvs) return;
    int nr = 0;
    for (int i = root >= 0 ? x_child(d, root, "entity") : -1; i >= 0; i = x_next(d, root, NULL, i)) {
        const xel_t *x = &d->v[i];
        char v[256];
        long t = attr_value(x, "id", 0, v, sizeof(v)) ? loader_atoi(v) : 0;
        long var = attr_value(x, "variant", 0, v, sizeof(v)) ? loader_atoi(v) : 0;
        long sub = attr_value(x, "subtype", 0, v, sizeof(v)) ? loader_atoi(v) : 0;
        tvs[nr][0] = t; tvs[nr][1] = var; tvs[nr][2] = sub;
        nr++;
        if (t < 0 || t > 0xfff || var < 0 || var > 0xfff || sub < 0 || sub > 0xff)
            for (int j = 0; j < nr - 1; j++)
                if (ekey(tvs[j][0], tvs[j][1], tvs[j][2]) == ekey(t, var, sub)
                        && (tvs[j][0] != t || tvs[j][1] != var || tvs[j][2] != sub)) {
                    snprintf(line, sizeof(line), "legacy entities2.xml from %s: entity %ld.%ld.%ld is past the key's 12/12/8 "
                             "bits - its key is entity %ld.%ld.%ld's, and it takes that one's place (0x7822ac)", id, t, var,
                             sub, tvs[j][0], tvs[j][1], tvs[j][2]);
                    legacy_say(line, 0);
                    break;
                }
        span_t n, val;
        for (const char *p = x->attrs.s; (p = xml_attr(p, x->attrs.e, &n, &val)); ) {
            if (!span_is(n, "gibFlags")) continue;
            char w[256];
            span_copy(val, w, sizeof(w));
            size_t wn = strlen(w);
            for (size_t a = 0; a < wn; ) {                      /* getline: a word up to each ' ', none after the last */
                size_t b = a;
                while (b < wn && w[b] != ' ') b++;
                if (!kind_in(w + a, b - a, GIBS)) {
                    snprintf(line, sizeof(line), "legacy entities2.xml from %s: entity %ld.%ld.%ld gibFlags \"%.*s\" is not a "
                             "word the loader knows - it reads a stray int for its bit (0x782ea4), 14 or more throws", id, t,
                             var, sub, (int) (b - a), w + a);
                    legacy_say(line, 0);
                }
                a = b + 1;
            }
        }
    }
    free(tvs);
}

static void legacy_note(const char *leaf, const ufile_t *hit, const char *inner) {
    if (!shim_report_active() || hit->owner == OWNER_CONTENT) return;
    for (int i = 0; i < g_legacy_ndone; i++) if (!strcasecmp(g_legacy_done[i], leaf)) return;
    if (g_legacy_ndone < 32) snprintf(g_legacy_done[g_legacy_ndone++], sizeof(g_legacy_done[0]), "%s", leaf);
    const char *id = hit->owner < MODS_MAX ? g_owner[hit->owner] : "?";
    long long t0 = now_ms();
    char line[600];
    long mlen = 0, glen = 0;
    char *mod = slurp(hit->src, &mlen);         /* the mod's own file, not the guard's completed copy */
    char game_path[400];
    snprintf(game_path, sizeof(game_path), "repentance-resources/data/%s", leaf);
    char *game = pack_copy(inner, &glen);
    if (!game) game = pack_copy(game_path, &glen);
    if (!mod || !game) {
        snprintf(line, sizeof(line), "legacy %s from %s: %s", leaf, id,
                 !mod ? "the mod's file could not be read" : "the game has no such file - a new file, not a replacement");
        legacy_say(line, 2);
        free(mod); free(game);
        return;
    }
    xdoc_t md = {0}, gd = {0};
    int bad_m = xml_parse(mod, mod + mlen, &md), bad_g = xml_parse(game, game + glen, &gd);
    char **mk = bad_m ? NULL : legacy_keys(&md), **gk = bad_g ? NULL : legacy_keys(&gd);
    if (!mk || !gk) {
        snprintf(line, sizeof(line), "legacy %s from %s: %s - not compared", leaf, id,
                 bad_m ? "the mod's file breaks off (an unclosed element)" : "a file could not be read as XML");
        legacy_say(line, 2);
        goto done;
    }
    if (!strcasecmp(leaf, "entities2.xml")) entities_warn(id, &md);
    {
        /* the game's records by key */
        int cap = 1;
        while (cap < gd.n * 2) cap <<= 1;
        int *table = (int *) malloc(sizeof(int) * (size_t) cap);
        unsigned char *matched = (unsigned char *) calloc((size_t) gd.n + 1, 1);
        unsigned char *mod_new = (unsigned char *) calloc((size_t) md.n + 1, 1);
        if (!table || !matched || !mod_new) { free(table); free(matched); free(mod_new); goto done; }
        for (int i = 0; i < cap; i++) table[i] = -1;
        for (int i = 0; i < gd.n; i++) {
            if (!gk[i]) continue;
            unsigned s = hash_of(gk[i]) & (unsigned) (cap - 1);
            while (table[s] >= 0 && strcmp(gk[table[s]], gk[i])) s = (s + 1) & (unsigned) (cap - 1);
            if (table[s] < 0) table[s] = i;
        }
        int records = 0, gameplay = 0, cosm = 0, added = 0, removed = 0, file_lines = 0;
        for (int i = 0; i < md.n; i++) {
            if (!mk[i]) continue;
            records++;
            unsigned s = hash_of(mk[i]) & (unsigned) (cap - 1);
            while (table[s] >= 0 && strcmp(gk[table[s]], mk[i])) s = (s + 1) & (unsigned) (cap - 1);
            int g = table[s];
            if (g < 0) {
                mod_new[i] = 1;
                if (md.v[i].parent > 0 && mod_new[md.v[i].parent]) continue;   /* inside a record already counted */
                added++;
                if (file_lines++ < LEGACY_FILE_LINES) {
                    snprintf(line, sizeof(line), "legacy %s from %s: + %s (the game has no such record)", leaf, id, mk[i]);
                    legacy_say(line, 0);
                }
                continue;
            }
            matched[g] = 1;
            const xel_t *mx = &md.v[i], *gx = &gd.v[g];
            span_t n, v, gv;
            /* the mod's values against the game's, then what only the game's record has */
            for (int pass = 0; pass < 2; pass++) {
                const xel_t *from = pass ? gx : mx, *other = pass ? mx : gx;
                for (const char *p = from->attrs.s; (p = xml_attr(p, from->attrs.e, &n, &v)); ) {
                    char an[48];
                    span_copy(n, an, sizeof(an));
                    int has = xml_get(other, an, &gv);
                    if (pass && has) continue;                       /* compared on the first pass */
                    if (has && gv.e - gv.s == v.e - v.s && !memcmp(gv.s, v.s, (size_t) (v.e - v.s))) continue;
                    if (cosmetic(n)) { cosm++; continue; }
                    gameplay++;
                    if (file_lines++ >= LEGACY_FILE_LINES) continue;
                    char was[80] = "(none)", now[80] = "(none)";
                    if (!pass) { if (has) span_copy(gv, was, sizeof(was)); span_copy(v, now, sizeof(now)); }
                    else span_copy(v, was, sizeof(was));
                    snprintf(line, sizeof(line), "legacy %s from %s: %s %s \"%s\" -> \"%s\"", leaf, id, mk[i], an, was, now);
                    legacy_say(line, 0);
                }
            }
        }
        for (int g = 0; g < gd.n; g++) {
            if (!gk[g] || matched[g]) continue;
            int up = gd.v[g].parent;
            if (up > 0 && !matched[up] && gk[up]) continue;                  /* its record is gone as a whole */
            removed++;
            if (file_lines++ < LEGACY_FILE_LINES) {
                snprintf(line, sizeof(line), "legacy %s from %s: - %s (the mod's file does not have it)", leaf, id, gk[g]);
                legacy_say(line, 0);
            }
        }
        snprintf(line, sizeof(line), "legacy %s from %s: %d records; %d gameplay values differ, %d cosmetic (portraits, "
                 "names, pictures); %d records added, %d taken away%s; %lld ms", leaf, id, records, gameplay, cosm,
                 added, removed, file_lines > LEGACY_FILE_LINES ? " (lines cut)" : "", now_ms() - t0);
        legacy_say(line, 2);
        free(table); free(matched); free(mod_new);
    }
done:
    if (mk) for (int i = 0; i < md.n; i++) free(mk[i]);
    if (gk) for (int i = 0; i < gd.n; i++) free(gk[i]);
    free(mk); free(gk);
    free(md.v); free(gd.v);
    free(mod); free(game);
}

/*
 * THE SPECIALIST DANCE: WHEN THE GAME'S TABLES ARE DONE (plan 30.09, 4.10 A,
 * measurement only). The dance loads today when the costume table stands at
 * exactly 132 and the music table at 119 (mods.c) - a number that means both
 * "the game has read its files" and "our ids start here", and a mod that
 * changes either table switches the dance off. The fix waits for the file the
 * Repentance loader (0x66eaa0) reads last, bossportraits.xml. Before it goes
 * in, this says what the tables hold as music.xml, costumes2.xml and
 * bossportraits.xml are asked for, and at the first file after the marker -
 * the thread and the time of each. Frames are not known here: the time is from
 * the mods' setup. Addresses as mods.c has them: the manager [0xac2428], the
 * null costumes at +0x55f30+0x30 (8 a record), the tracks at +0x55a60+0x3d8
 * (0x70 a record).
 */
static int g_dance_lines, g_dance_after;

static void dance_tables(long *nulls, long *tracks) {
    *nulls = *tracks = -1;
    unsigned long b = shim_image_base();
    if (!b) return;
    unsigned long m = *(const unsigned long *) (b + 0xac2428);
    if (!m) return;
    const unsigned long *v = (const unsigned long *) (m + 0x55f30 + 0x30);
    *nulls = v[1] >= v[0] ? (long) ((v[1] - v[0]) / 8) : 0;
    v = (const unsigned long *) (m + 0x55a60 + 0x3d8);
    *tracks = v[1] >= v[0] ? (long) ((v[1] - v[0]) / 0x70) : 0;
}

static void dance_note(const char *p) {
    if (g_dance_lines >= 16 || !shim_report_active()) return;
    const char *leaf = strrchr(p, '/');
    leaf = leaf ? leaf + 1 : p;
    int which = !strcasecmp(leaf, "music.xml") ? 0 : !strcasecmp(leaf, "costumes2.xml") ? 1
              : !strcasecmp(leaf, "bossportraits.xml") ? 2 : -1;
    long nulls, tracks;
    char line[300];
    if (which < 0) {
        if (!g_dance_after) return;
        g_dance_after = 0;
        dance_tables(&nulls, &tracks);
        snprintf(line, sizeof(line), "the first file after the marker (%s) at +%lld ms, thread %d: nulls=%ld tracks=%ld",
                 leaf, now_ms() - g_t0, gettid(), nulls, tracks);
        g_dance_lines++;
        report_line("dance", line);
        return;
    }
    const char *ed = strstr(p, "-resources/");
    if (!ed) return;
    const char *eds = ed;
    while (eds > p && eds[-1] != '/') eds--;
    dance_tables(&nulls, &tracks);
    snprintf(line, sizeof(line), "asked %s (%.*s) at +%lld ms, thread %d (main %d): nulls=%ld tracks=%ld",
             leaf, (int) (ed - eds), eds, now_ms() - g_t0, gettid(), getpid(), nulls, tracks);
    g_dance_lines++;
    report_line("dance", line);
    if (which == 2 && !strncmp(eds, "repentance", 10)) {
        snprintf(line, sizeof(line), "marker seen at +%lld ms, thread %d (mods: %d)", now_ms() - g_t0, gettid(), g_count);
        g_dance_lines++;
        report_line("dance", line);
        g_dance_after = 1;
    }
}

/*
 * THE MEDIA SELF-TEST (plan 30.09, M7 p.5): what the installer does to a
 * mod's folder, done once to a folder of our own in the mods folder - make it
 * with a file inside, rename it away, rename it back onto the path this same
 * run freed, remove it all. Honestly: files the app made itself do not
 * reproduce 28.09 (the index rows of files made past the media store); this
 * says only that the ordinary road works on this phone.
 */
static void media_selftest(void) {
    if (!shim_report_active() || !g_dir[0]) return;
    char a[700], b[720], sub[760], file[800];
    snprintf(a, sizeof(a), "%s/.selftest-%ld", g_dir, (long) time(NULL));
    snprintf(b, sizeof(b), "%s-moved", a);
    const char *step = NULL;
    int err = 0;
#define STEP(name, call) do { if (!step && (call) != 0) { step = name; err = errno; } } while (0)
    STEP("mkdir", mkdir(a, 0700));
    snprintf(sub, sizeof(sub), "%s/a", a);
    STEP("mkdir inside", mkdir(sub, 0700));
    snprintf(file, sizeof(file), "%s/b.txt", sub);
    if (!step) {
        FILE *f = fopen(file, "w");
        if (!f) { step = "create a file"; err = errno; }
        else {
            if (fputs("self-test\n", f) < 0) { step = "write"; err = errno; }
            if (fclose(f) && !step) { step = "close"; err = errno; }
        }
    }
    STEP("rename away", rename(a, b));
    STEP("rename back onto the freed path", rename(b, a));
    struct stat st;
    STEP("the file after both", stat(file, &st));
#undef STEP
    remove_tree(a, 0);
    remove_tree(b, 0);
    if (!step && (!stat(a, &st) || !stat(b, &st))) { step = "remove"; err = ENOTEMPTY; }
    char line[900];
    if (step) snprintf(line, sizeof(line), "media self-test failed at \"%s\": %s (%s)", step, strerror(err), g_dir);
    else snprintf(line, sizeof(line), "media self-test ok: made, renamed away, renamed back onto the freed path, removed (%s)", g_dir);
    report_line("usermods", line);
}
#endif  /* ISAAC_DIAG */

/* ------------------------------------------------------------ the start */

/* a folder of the mods folder removed at the start; what will not go is said (the case of 28.09) */
static void start_remove(const char *name) {
    char dir[768];
    struct stat st;
    snprintf(dir, sizeof(dir), "%s/%s", g_dir, name);
    int why = remove_tree(dir, 0);
    int left = !lstat(dir, &st);
    LOG("usermods: removed %s%s", name, left ? " - NOT all of it" : "");
    /* a mod's folder gone whole (DELETE, or an update's old copy - hotfix 1.73, a new folder each update): its
       check.txt beside it goes too, or one would be left behind for every update */
    if (!left && name[0] != '.') {
        char note[800];
        check_note_path(note, sizeof(note), name);
        if (!unlink(note)) NOTE("%s.check.txt removed with its folder", name);
    }
    if (left && shim_report_active()) {
        char line[900];
        snprintf(line, sizeof(line), "%s could not be removed whole (%s)", dir, strerror(why ? why : ENOTEMPTY));
        report_line("usermods", line);
    }
}

/* ".old-<id>-<yyyyMMdd>-<HHmmss>" -> <id>, or 0 */
static int old_copy_of(const char *name, char *id, size_t room) {
    size_t n = strlen(name);
    if (strncmp(name, ".old-", 5) || n < 5 + 1 + 16) return 0;
    const char *stamp = name + n - 16;                  /* -yyyyMMdd-HHmmss */
    if (stamp[0] != '-' || stamp[9] != '-') return 0;
    for (int i = 1; i < 16; i++) if (i != 9 && !isdigit((unsigned char) stamp[i])) return 0;
    size_t k = (size_t) (stamp - (name + 5));
    if (!k || k >= room) return 0;
    memcpy(id, name + 5, k);
    id[k] = 0;
    return 1;
}

/* called with the folder port-touch.ini lives in (loader.c, setConfigDir) */
void shim_usermods_setup(const char *config_dir) {
    if (!config_dir || !config_dir[0]) return;
#if ISAAC_DIAG
    g_t0 = now_ms();
#endif
    pthread_mutex_lock(&g_list_lock);
    snprintf(g_dir, sizeof(g_dir), "%s/mods", config_dir);
    snprintf(g_crash_file, sizeof(g_crash_file), "%s/last-crash.txt", g_dir);
    mkdir(g_dir, 0700);
    /*
     * What the last run left to be removed goes first: a mod deleted on the
     * page (marked .remove: its files were still in that run's index), a
     * half-unpacked .incoming-<id>, a copy the installer had not finished
     * putting in place (marked .adding), an old copy the installer moved
     * aside and marked once the new one stood in its place, and the media
     * self-test's folder. Each mark goes last (remove_tree), so a folder that
     * will not go whole stays marked and no mod.
     *
     * Not while ADD MOD is at work in this very process (hotfix 1.74): a game
     * Android ended behind the file picker is started again with the picked
     * archives, and the installer may be unpacking into .incoming-<id>, or
     * moving a copy marked .adding, or have set an old copy aside, as the
     * start comes. Those are its own, alive; what an add of an earlier process
     * left goes at the next start (a marked folder is no mod meanwhile).
     */
    int live_add = g_installing;
    if (live_add) {
        __android_log_print(ANDROID_LOG_INFO, "isaac", "usermods: ADD MOD is at work - its folders are left alone at this start");
        NOTE("ADD MOD is at work in this process - .incoming-, .adding and .old- folders left alone at this start");
    }
    DIR *d = opendir(g_dir);
    if (d) {
        struct dirent *e;
        while ((e = readdir(d))) {
            if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;
            char dir[768];
            snprintf(dir, sizeof(dir), "%s/%s", g_dir, e->d_name);
            if (live_add && (!strncmp(e->d_name, ".incoming-", 10) || has_mark(dir, ADDING_MARK))) continue;
            if (!strncmp(e->d_name, ".incoming-", 10) || !strncmp(e->d_name, ".selftest-", 10)
                    || has_mark(dir, REMOVE_MARK) || has_mark(dir, ADDING_MARK))
                start_remove(e->d_name);
        }
        closedir(d);
    }
    /*
     * An old copy with no mark is an update that broke off (plan 02.10, B3):
     * the installer marks the old copy only when the new one stands at <id>.
     * With <id> not there - never placed, or removed just above as unfinished
     * - the old copy goes back to <id>: a killed update leaves the old mod or
     * the new one, never neither. With <id> there, the old copy goes.
     */
    d = live_add ? NULL : opendir(g_dir);
    if (d) {
        struct dirent *e;
        char names[MODS_MAX][ID_MAX + 24];
        int n = 0;
        while ((e = readdir(d)) && n < MODS_MAX)
            if (!strncmp(e->d_name, ".old-", 5) && strlen(e->d_name) < sizeof(names[0]))
                snprintf(names[n++], sizeof(names[0]), "%s", e->d_name);
        closedir(d);
        for (int i = 0; i < n; i++) {
            char id[ID_MAX], from[768], to[768];
            struct stat st;
            snprintf(from, sizeof(from), "%s/%s", g_dir, names[i]);
            if (!old_copy_of(names[i], id, sizeof(id))) { start_remove(names[i]); continue; }
            snprintf(to, sizeof(to), "%s/%s", g_dir, id);
            if (lstat(to, &st) && errno == ENOENT) {
                int ok = !rename(from, to);
                __android_log_print(ok ? ANDROID_LOG_INFO : ANDROID_LOG_ERROR, "isaac", "usermods: %s %s from %s (an update broke off)%s%s",
                                    id, ok ? "restored" : "NOT restored", names[i], ok ? "" : ": ", ok ? "" : strerror(errno));
                NOTE("%s %s from %s (an update broke off)%s%s", id, ok ? "restored" : "NOT restored", names[i],
                     ok ? "" : ": ", ok ? "" : strerror(errno));
            } else {
                start_remove(names[i]);
            }
        }
    }
#if ISAAC_DIAG
    media_selftest();
    shim_json_selftest();
#endif
    read_list();
    for (int i = 0; i < g_count; i++) if (g_mods[i].on) index_mod(&g_mods[i]);
    /* this run's index is built: the restart owed is worked out against it from now on (THE RESTART OWED) */
    g_setup_done = 1;
    g_owed_disk = 0;
    g_disk_at = 0;
    owed_update();
    /* a mod that is off says what is wrong with its mod.json all the same: the page points at its check.txt */
    for (int i = 0; i < g_count; i++)
        if (!g_mods[i].indexed && g_mods[i].issues) {
            sb_t note = {0};
            manifest_section(&note, g_mods[i].issues, 1);
            char path[800];
            check_note_path(path, sizeof(path), g_mods[i].id);
            long have_n = 0;
            char *have = slurp(path, &have_n);
            int same = have && have_n == (long) note.n && !memcmp(have, note.s, note.n);
            free(have);
            FILE *f = same ? NULL : fopen(path, "w");
            if (f) { fwrite(note.s, 1, note.n, f); fclose(f); }
            free(note.s);
        }
    for (int i = 0; i < g_count; i++) if (g_mods[i].on && g_mods[i].content) g_reading_on = 1;
    if (g_indexed) g_reading_on = 1;
    crash_read();
    LOG("usermods: %d mods, %d files in the index", g_count, g_indexed);
    if (shim_report_active()) {
        char line[900];
        snprintf(line, sizeof(line), "%d mods in %s, %d files from the ones on", g_count, g_dir, g_indexed);
        report_line("usermods", line);
        for (int i = 0; i < g_count; i++) {
            snprintf(line, sizeof(line), "  %s %s \"%s\" %s: %d files%s%s%s%s", g_mods[i].on ? "on " : "off",
                     g_mods[i].id, g_mods[i].name[0], g_mods[i].version[0], g_mods[i].files,
                     g_mods[i].reach ? " (its folder did not answer)" : "",
                     g_mods[i].manifest < 0 ? ", mod.json NOT READ" : g_mods[i].refused ? ", settings refused" : "",
                     g_mods[i].layers[0] ? ", layers " : "", g_mods[i].layers);
            report_line("usermods", line);
            if (g_mods[i].mj) {
                int n = snprintf(line, sizeof(line), "    settings:");
                for (int k = 0; k < g_mods[i].mj->nsettings && n > 0 && n < (int) sizeof(line) - 80; k++) {
                    char v[64] = "?";
                    modj_value_name(&g_mods[i].mj->settings[k], g_mods[i].val[k], v, sizeof(v));
                    n += snprintf(line + n, sizeof(line) - (size_t) n, " %s=%s%s", g_mods[i].mj->settings[k].key, v,
                                  g_mods[i].val[k] == g_mods[i].mj->settings[k].def ? "" : "*");
                }
                report_line("usermods", line);
            }
        }
    }
    /* folders found by hand join the list; an older format goes - never in the name of a list not read */
    if (g_list_dirty && !g_list_unread) list_write("start");
    pthread_mutex_unlock(&g_list_lock);
}

/* ------------------------------------------------------------ for the MODS page */

/* the page's language set (tweaks.c, TW_SET order: en ru de es jp kr zh): a mod's texts in it, English for CJK */
void shim_usermods_lang(int set) { g_text_lang = set >= 1 && set <= 3 ? set : 0; }

static int settings_differ(void);
int shim_usermods_count(void) { return g_count; }
const char *shim_usermods_dir(void) { return g_dir; }
const char *shim_usermods_id(int i) { return i >= 0 && i < g_count ? g_mods[i].id : ""; }
const char *shim_usermods_name(int i) { return i >= 0 && i < g_count ? g_mods[i].name[g_text_lang] : ""; }
const char *shim_usermods_version(int i) { return i >= 0 && i < g_count ? g_mods[i].version[g_text_lang] : ""; }
const char *shim_usermods_author(int i) { return i >= 0 && i < g_count ? g_mods[i].author[g_text_lang] : ""; }
const char *shim_usermods_description(int i) { return i >= 0 && i < g_count ? g_mods[i].description[g_text_lang] : ""; }
int shim_usermods_on(int i) { return i >= 0 && i < g_count && g_mods[i].on; }
int shim_usermods_files(int i) { return i >= 0 && i < g_count ? g_mods[i].files : 0; }
/*
 * The MODS page's red (hotfix 1.71, the owner 05.10.2026): 0 the port reads the
 * mod; 1 there is nothing in it the port reads; 2 the same, and it is a script
 * mod (main.lua) - the port runs no scripts. Judged by its folders (resources*,
 * content, patch, options), and a mod that is on with only resources* folders
 * by its files the index met (empty folders).
 */
int shim_usermods_bare(int i) {
    if (i < 0 || i >= g_count) return 0;
    const umod_t *m = &g_mods[i];
    int bare = !m->port_dirs || (m->indexed && m->seen == 0 && !m->data_dirs);
    return bare ? (m->script ? 2 : 1) : 0;
}
/*
 * THE RESTART OWED (hotfix 1.74; the players on 1.73: "RESTART disappeared
 * after installing mods - nothing to restart the game with"). RESTART TO
 * APPLY stood on one flag, set by each road that changed the list - the
 * page's own, ADD MOD's listPut - and never taken back. A flag is lost with
 * its process: Android ends a game left behind its own file picker on a
 * phone short of memory, the picked archives come to a new process, and
 * there the installer ran side by side with the start (setConfigDir) - its
 * mods could land before the index (then the flag owed a restart for
 * nothing) or after it. And a road that does not pass the flag (a folder put
 * into mods/ by hand through CONFIG FILE, mods.txt edited there) owed none.
 *
 * Now the debt is worked out, never set: this run's index is the mods
 * index_mod took at the start, in their order (g_owner). A restart is owed
 * when the mods a start would index now - on, their folder answering, in the
 * list's order - are not those, or not in that order; or when a setting of an
 * indexed mod is not what the run started with (settings_differ). Asked of
 * the list in memory after every change (owed_update: the page, ADD MOD, the
 * list read again), and of the list the next start reads from the disk -
 * mods.txt and the folders it does not name, exactly as read_list takes them
 * - on a thread of its own, at most every DISK_EVERY_MS while the page asks
 * (disk_due). Turning a change back takes the row away; a change the game
 * would not see (an off mod moved or deleted on the page) owes nothing. A pale
 * name (shim_usermods_pending: on, added in this run, not indexed) always
 * comes with the row: such a mod is in the list now and not in g_owner.
 * ADD MOD is the exception since 1.75: it owes its restart from the mod it
 * puts in until the game starts again (ADD MOD OWES ITS RESTART, below).
 */
#define DISK_EVERY_MS 2000

/* the ids a start would index, in their order: n of them - against this run's index */
static int differs_from_index(char (*ids)[ID_MAX], int n) {
    if (n > MODS_MAX) n = MODS_MAX;                 /* index_mod takes MODS_MAX owners at most */
    if (n != g_owners) return 1;
    for (int i = 0; i < n; i++) if (strcmp(ids[i], g_owner[i])) return 1;
    return 0;
}

/* the list in memory, after a change to it; with g_list_lock held (or before anyone else can reach the list) */
static void owed_update(void) {
    if (!g_setup_done) return;                      /* the start sets it, when its index is built */
    static char ids[MODS_MAX][ID_MAX];
    int n = 0;
    for (int i = 0; i < g_count && n < MODS_MAX; i++)
        if (g_mods[i].on && !g_mods[i].reach) snprintf(ids[n++], ID_MAX, "%s", g_mods[i].id);
    g_owed_list = differs_from_index(ids, n);
}

/*
 * The list the next start reads, from the disk (read_list's way: mods.txt's
 * rows whose folder is a mod, then the folders it does not name, on): 1 it
 * would index other mods than this run, 0 the same, -1 no word - mods.txt is
 * not there or not readable (the start would take the copy inside the app),
 * or a folder did not answer (a moment's failure of the storage is no change).
 * With g_list_lock held.
 */
static int disk_differs(void) {
    static lrow_t rows[MODS_MAX];
    static char ids[MODS_MAX][ID_MAX];
    int n = 0, dirty = 0, verdict = -1;
    char path[640];
    snprintf(path, sizeof(path), "%s/mods.txt", g_dir);
    t_quiet = 1;
    FILE *f = fopen(path, "r");
    int err = f ? list_parse(f, rows, &n, &dirty, NULL) : -1;
    if (f) fclose(f);
    DIR *d = err ? NULL : opendir(g_dir);
    if (d) {
        struct dirent *e;
        while ((e = readdir(d)) && n < MODS_MAX) {
            if (e->d_name[0] == '.' || strlen(e->d_name) >= ID_MAX || row_find(rows, n, e->d_name) >= 0) continue;
            if (folder_state(e->d_name) != 1) continue;
            snprintf(rows[n].id, sizeof(rows[n].id), "%s", e->d_name);
            rows[n].on = 1;                         /* a folder nobody's list names joins on */
            rows[n].reach = 0;
            n++;
        }
        closedir(d);
        int k = 0, reach = 0;
        for (int i = 0; i < n; i++) {
            if (rows[i].reach) reach = 1;
            else if (rows[i].on) snprintf(ids[k++], ID_MAX, "%s", rows[i].id);
        }
        if (!reach) verdict = differs_from_index(ids, k);
    }
    t_quiet = 0;
    return verdict;
}

static void *disk_look(void *arg) {
    (void) arg;
    pthread_mutex_lock(&g_list_lock);
    int v = disk_differs();
    if (v >= 0 && v != g_owed_disk) {
        g_owed_disk = v;
        NOTE("the list the next start reads from the disk %s this run's index", v ? "differs from" : "is again");
    }
    pthread_mutex_unlock(&g_list_lock);
    g_disk_busy = 0;
    return NULL;
}

/* the page asks: the disk is looked at again when DISK_EVERY_MS have gone by, never on the frame's own thread */
static void disk_due(void) {
    if (g_disk_busy || !g_dir[0]) return;
    long long t = now_ms();
    if (g_disk_at && t - g_disk_at < DISK_EVERY_MS) return;
    g_disk_at = t;
    g_disk_busy = 1;
    pthread_t th;
    if (pthread_create(&th, NULL, disk_look, NULL) == 0) pthread_detach(th);
    else g_disk_busy = 0;
}

/*
 * ADD MOD OWES ITS RESTART (hotfix 1.75; the owner on 1.74: "the restart button when adding mods - you said you had
 * fixed it, and now it is gone COMPLETELY"). 1.74 tied both RESTART TO APPLYs - the page's row and the add window's
 * button - to the debt worked out above alone, and that debt is nothing whenever what a start would load equals what
 * this run loaded - also right after ADD MOD put mods in: an update of a mod that is off (the replaced copy was not
 * loaded either), and archives that reached a new process whose start had not come when the installer's wait for it
 * ran out (UserMods.waitForGame, 20 s: the start then took them in, and the window had asked a layer with no index
 * yet - "nothing owed"). 1.73 had the row on every one of those roads (its flag), so 1.74 cut what worked
 * (tools/umdesk/test_restart175.py, the phone's own order of calls over jnidesk.c). Now the road owes its restart as it did in 1.73 - from the mod it puts in until the game starts again,
 * whatever else is undone meanwhile - and the worked-out debt keeps every other road it found (a folder put in by
 * hand, mods.txt edited, the page's changes coming and going, the settings). Before the start's index there is no
 * list to work anything out from, but this debt is known all the same: the add window asks then too.
 */
int shim_usermods_changed(void) {
    if (!g_setup_done) return g_owed_add;
    disk_due();
    return g_owed_add || g_owed_list || g_owed_disk || settings_differ();
}

/*
 * ADD MOD WITHOUT A RESTART (hotfix 1.73, the players' request the owner
 * approved, 08.10.2026). The installer used to end the process after every
 * mod; nothing needed it but the mod's files, which the index of this run
 * does not hold - the list itself was put right by list_put all along. Now a
 * batch of mods goes in one after another and the player restarts once, with
 * RESTART TO APPLY. A mod added in this run is on the page at once (the page
 * lays out again when list_gen moves) and is marked as waiting: switched on,
 * its files reach the game only after the restart.
 */
int shim_usermods_list_gen(void) { return g_list_gen; }
int shim_usermods_pending(int i) {
    return i >= 0 && i < g_count && g_mods[i].added && g_mods[i].on && !g_mods[i].indexed;
}

/*
 * A MOD'S SETTINGS ON ITS PAGE (plan 4.10, part 2; the owner's order of
 * 02.10.2026: the title and the description, the SETTINGS block, then the
 * mod's own rows). Only the settings json.c took; k is a place among them.
 */
static const modj_setting_t *setting_of(int i, int k) {
    const modj_t *mj = i >= 0 && i < g_count ? g_mods[i].mj : NULL;
    return mj && k >= 0 && k < mj->nsettings ? &mj->settings[k] : NULL;
}
int shim_usermods_settings(int i) { return i >= 0 && i < g_count && g_mods[i].mj ? g_mods[i].mj->nsettings : 0; }
/* MODJ_SWITCH 0, MODJ_CHOICE 1, MODJ_NUMBER 2; -1 none */
int shim_usermods_setting_kind(int i, int k) { const modj_setting_t *s = setting_of(i, k); return s ? (int) s->kind : -1; }
const char *shim_usermods_setting_label(int i, int k) {
    const modj_setting_t *s = setting_of(i, k);
    const char *t = s ? modj_text(s->label, TEXT_LANG[g_text_lang]) : NULL;
    return t ? t : (s ? s->key : "");
}
int shim_usermods_setting_value(int i, int k) { return setting_of(i, k) ? g_mods[i].val[k] : 0; }
int shim_usermods_setting_count(int i, int k) { const modj_setting_t *s = setting_of(i, k); return s ? s->nvalues : 0; }
/* a choice's value v as the page shows it */
const char *shim_usermods_setting_value_label(int i, int k, int v) {
    const modj_setting_t *s = setting_of(i, k);
    if (!s || s->kind != MODJ_CHOICE || v < 0 || v >= s->nvalues) return "";
    const char *t = modj_text(s->values[v].label, TEXT_LANG[g_text_lang]);
    return t ? t : s->values[v].value;
}
void shim_usermods_setting_range(int i, int k, int *min, int *max, int *step) {
    const modj_setting_t *s = setting_of(i, k);
    *min = s ? s->min : 0;
    *max = s ? s->max : 0;
    *step = s ? s->step : 1;
}

/*
 * One step of a setting, as a Tweaks row steps (0x6e2970): a switch flips, a
 * choice goes on or back round its values, a number moves by its step and
 * stops at its ends. Written at once (mod-settings.ini); the run's files stay
 * as they started, and RESTART TO APPLY says a restart applies it.
 */
void shim_usermods_setting_step(int i, int k, int dir) {
    pthread_mutex_lock(&g_list_lock);
    const modj_setting_t *s = setting_of(i, k);
    if (s && dir) {
        umod_t *m = &g_mods[i];
        int v = m->val[k];
        if (s->kind == MODJ_SWITCH) v = !v;
        else if (s->kind == MODJ_CHOICE) v = ((v + (dir > 0 ? 1 : -1)) % s->nvalues + s->nvalues) % s->nvalues;
        else {
            v += dir > 0 ? s->step : -s->step;
            if (v < s->min) v = s->min;
            if (v > s->max) v = s->max;
        }
        if (v != m->val[k]) {
            m->val[k] = v;
            g_touched = 1;
            settings_write(m, 0, "page");
        }
    }
    pthread_mutex_unlock(&g_list_lock);
}

static int settings_differ(void) {
    for (int i = 0; i < g_count; i++) {
        const umod_t *m = &g_mods[i];
        if (!m->indexed || !m->mj) continue;
        for (int k = 0; k < m->mj->nsettings; k++)
            if (m->val[k] != m->start[k]) return 1;     /* each asks for one: "restart": false is not taken yet */
    }
    return 0;
}

/* why the last write of mod-settings.ini failed ("" when it did not): the page says the change did not stay */
const char *shim_usermods_settings_error(void) { return g_settings_error; }

/* what json.c said of the mod's mod.json, for its page: how many refusals (the file, or settings turned off),
   whether the file was not read at all, and the first refusal's code */
int shim_usermods_manifest_note(int i, int *broken, char *reason, size_t room) {
    *broken = 0;
    if (room) reason[0] = 0;
    if (i < 0 || i >= g_count) return 0;
    *broken = g_mods[i].manifest < 0;
    snprintf(reason, room, "%s", g_mods[i].reason);
    return g_mods[i].refused;
}

/* the file of this mod the last run closed right after reading ("" when none): a line for its page */
const char *shim_usermods_crash(int i) { return i >= 0 && i < g_count ? g_mods[i].crash : ""; }
int shim_usermods_adapted(int i) { return i >= 0 && i < g_count ? g_mods[i].adapted : 0; }
/* why mods.txt could not be saved the last time ("" when it was): the page's changes did not stay */
const char *shim_usermods_save_error(void) { list_tick(); return g_save_error; }
/* 1 while mods.txt is there and could not be read (the page says the list waits); asking tries again */
int shim_usermods_list_unread(void) { list_tick(); return g_list_unread; }
/* what the guard did to this mod's files: completed, and answered by the game's file instead */
static int owner_of(int i) {
    for (int o = 0; i >= 0 && i < g_count && o < g_owners; o++) if (!strcmp(g_owner[o], g_mods[i].id)) return o;
    return -1;
}
int shim_usermods_guard_fixed(int i) { int o = owner_of(i); return o >= 0 ? g_guard_fixed[o] : 0; }
int shim_usermods_guard_game(int i) { int o = owner_of(i); return o >= 0 ? g_guard_game[o] : 0; }
/* the mod's completion widget is its own and the reminder leaves it alone (M9; the MODS page's line, WIDGET_NOTE) */
int shim_usermods_widget_alone(int i) { int o = owner_of(i); return o >= 0 ? g_widget_alone[o] : 0; }
/* of those, the pictures that are not the game's size: the page tells the modder to convert them */
int shim_usermods_guard_size(int i) { int o = owner_of(i); return o >= 0 ? g_guard_size[o] : 0; }

/* the page's place of an owner of this run, or -1 (the list has no such mod now) */
static int page_of(int o) {
    for (int i = 0; o >= 0 && o < g_owners && i < g_count; i++) if (!strcmp(g_mods[i].id, g_owner[o])) return i;
    return -1;
}

/*
 * THE MODS OVER EACH OTHER, for the page (the lines in its language are
 * tweaks.c's). The k-th pair mod i is in: the other mod (its place on the page,
 * or -1 and its id in `other_id`), whether mod i is the stronger one, how many
 * files, their kinds (K_* bits: pictures, animations, sounds, music, tables,
 * fonts, videos, other) and, when every one of them is so in one language
 * only, that language. 0 past the last pair.
 */
int shim_usermods_over(int i, int k, int *other, char *other_id, size_t idn, int *stronger, int *files, unsigned *kinds,
                       char *lang, size_t ln) {
    int o = owner_of(i);
    for (int p = 0; o >= 0 && p < g_nover; p++) {
        const over_t *v = &g_over[p];
        if (v->over != o && v->under != o) continue;
        if (k--) continue;
        int x = v->over == o ? v->under : v->over;
        *other = page_of(x);
        snprintf(other_id, idn, "%s", g_owner[x]);
        *stronger = v->over == o;
        *files = v->files;
        *kinds = v->kinds;
        snprintf(lang, ln, "%s", v->lang_only == v->files ? v->lang : "");
        return 1;
    }
    return 0;
}

/*
 * The k-th breakage mod i is part of: the .anm2's mod and the sheet's mod (a
 * page place, -1 for the game's sheet; their ids beside), the sheet's path, the
 * size served and the size the frames were cut for, how many .anm2 of the mod
 * cut it and the first of them. 0 past the last.
 */
int shim_usermods_broken(int i, int k, int *anm_mod, char *anm_id, int *sheet_mod, char *sheet_id, size_t idn,
                         char *sheet, size_t sn, unsigned *sw, unsigned *sh, unsigned *iw, unsigned *ih,
                         int *anms, char *anm, size_t an) {
    int o = owner_of(i);
    for (int p = 0; o >= 0 && p < g_nbreaks; p++) {
        const broken_t *b = &g_breaks[p];
        if (b->anm_owner != o && b->sheet_owner != o) continue;
        if (k--) continue;
        *anm_mod = page_of(b->anm_owner);
        snprintf(anm_id, idn, "%s", g_owner[b->anm_owner]);
        *sheet_mod = b->sheet_owner >= 0 ? page_of(b->sheet_owner) : -1;
        snprintf(sheet_id, idn, "%s", b->sheet_owner >= 0 ? g_owner[b->sheet_owner] : "");
        snprintf(sheet, sn, "%s", b->sheet);
        *sw = b->sw; *sh = b->sh; *iw = b->iw; *ih = b->ih;
        *anms = b->anms;
        snprintf(anm, an, "%s", b->anm);
        return 1;
    }
    return 0;
}
/*
 * THE PAIRS, for the page (tweaks.c): mod i and mod j as the list stands now -
 * the upper one over the lower. The page asks on the frame thread and never
 * waits: while the pairs' thread works, or before it has run, the answer is
 * "not yet" (-1, or 0 records) and shim_usermods_pairs_gen() tells when to ask
 * again.
 */
int shim_usermods_pairs_gen(void) { return g_pairs_gen; }

static const pair_t *pair_now(int i, int j) {
    return i < j ? pair_of(&g_mods[i], &g_mods[j]) : pair_of(&g_mods[j], &g_mods[i]);
}

/* the mods on now (but i) whose files break with mod i's, were i on too: how many, the first one's place; -1 not yet */
int shim_usermods_conflicts(int i, int *first) {
    *first = -1;
    if (!g_pairs_gen || i < 0 || i >= g_count || pthread_mutex_trylock(&g_pair_lock)) return -1;
    int n = 0;
    for (int j = 0; j < g_count; j++) {
        if (j == i || !g_mods[j].on) continue;
        const pair_t *p = pair_now(i, j);
        if (p && p->nbroken && !n++) *first = j;
    }
    pthread_mutex_unlock(&g_pair_lock);
    return n;
}

/*
 * The k-th breakage between mods i and j: whose .anm2 cut the sheet (1 mod i's,
 * 0 mod j's - the sheet is the other one's), the sheet's path, the size served
 * and the size the frames were cut for, how many .anm2 cut it, and what the
 * size cut for is (THE MODS OVER EACH OTHER, broken_t.own: 0 the game's, 1 the
 * cutting mod's own copy). 0 past the last, or not yet.
 */
int shim_usermods_pair_broken(int i, int j, int k, int *anm_mine, char *sheet, size_t sn, unsigned *sw, unsigned *sh,
                              unsigned *iw, unsigned *ih, int *anms, int *own) {
    if (!g_pairs_gen || i < 0 || j < 0 || i >= g_count || j >= g_count || i == j || pthread_mutex_trylock(&g_pair_lock))
        return 0;
    const pair_t *p = pair_now(i, j);
    int r = 0;
    if (p && k >= 0 && k < p->nbroken) {
        const broken_t *b = &p->broken[k];
        *anm_mine = (b->anm_owner == 0) == (i < j);
        snprintf(sheet, sn, "%s", b->sheet);
        *sw = b->sw; *sh = b->sh; *iw = b->iw; *ih = b->ih;
        *anms = b->anms;
        *own = b->own;
        r = 1;
    }
    pthread_mutex_unlock(&g_pair_lock);
    return r;
}

/* the files mod i and mod j both bring: whether i's are the game's (i the upper), how many, their kinds (K_*) and,
   when every one is so in one language only, that language; 0 none, or not yet */
int shim_usermods_pair_over(int i, int j, int *mine_over, int *files, unsigned *kinds, char *lang, size_t ln) {
    if (!g_pairs_gen || i < 0 || j < 0 || i >= g_count || j >= g_count || i == j || pthread_mutex_trylock(&g_pair_lock))
        return 0;
    const pair_t *p = pair_now(i, j);
    int r = 0;
    if (p && p->over_files) {
        *mine_over = i < j;
        *files = p->over_files;
        *kinds = p->over_kinds;
        snprintf(lang, ln, "%s", p->over_lang_only == p->over_files ? p->over_lang : "");
        r = 1;
    }
    pthread_mutex_unlock(&g_pair_lock);
    return r;
}

/*
 * Of those, the data tables the game's loader would write past (M4): how many,
 * and the first one's file, record tag, id ("(none)" for a missing one) and
 * the ids the game's table holds - for the page's line in its own language.
 */
/* what content.c made of the mod's data tables (the MODS page's line): 0 when nothing to tell */
int shim_usermods_content(int i, content_sum_t *sum) {
    memset(sum, 0, sizeof(*sum));
    return i >= 0 && i < g_count && g_mods[i].indexed ? content_summary(g_mods[i].id, sum) : 0;
}

int shim_usermods_guard_bounds(int i, char *file, size_t fn, char *tag, size_t tn, char *id, size_t idn, long *lo, long *hi) {
    int o = owner_of(i);
    if (o < 0 || !g_guard_bounds[o] || !g_guard_bound_file[o]) return 0;
    const ufile_t *e = g_guard_bound_file[o];
    char t[48] = "", v[32] = "";
    *lo = *hi = 0;
    if (!e->why || sscanf(e->why, "%47s id %31s is not in the game's table %ld..%ld", t, v, lo, hi) != 4) return 0;
    snprintf(file, fn, "%s", strchr(e->key, '|') + 1);
    snprintf(tag, tn, "%s", t);
    snprintf(id, idn, "%s", v);
    return g_guard_bounds[o];
}

/* the page's changes: kept in mods.txt at once, used from the next start */
void shim_usermods_toggle(int i) {
    pthread_mutex_lock(&g_list_lock);
    if (i >= 0 && i < g_count) {
        g_mods[i].on = !g_mods[i].on;
        g_touched = 1;
        owed_update();
        list_write("toggle");
    }
    pthread_mutex_unlock(&g_list_lock);
}

void shim_usermods_move(int i, int dir) {
    pthread_mutex_lock(&g_list_lock);
    int j = i + dir;
    if (i >= 0 && i < g_count && j >= 0 && j < g_count) {
        umod_t t = g_mods[i];
        g_mods[i] = g_mods[j];
        g_mods[j] = t;
        g_touched = 1;
        owed_update();
        list_write("move");
    }
    pthread_mutex_unlock(&g_list_lock);
}

/*
 * A folder and all in it; 0, or the first errno on the way. The marks
 * (.remove, .adding) go last, and only when all else went (plan 02.10, A3): a
 * folder that will not go whole stays marked - no mod - and is tried again at
 * the next start. In 1.666 the mark could go first and the rest come back as a
 * mod, on.
 */
static int remove_tree(const char *path, int depth) {
    struct stat st;
    if (lstat(path, &st)) return errno == ENOENT ? 0 : errno;
    if (depth > 16) return ELOOP;
    int first = 0;
    if (S_ISDIR(st.st_mode)) {
        int marks = 0;
        DIR *d = opendir(path);
        if (d) {
            struct dirent *e;
            while ((e = readdir(d))) {
                if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;
                if (!strcmp(e->d_name, REMOVE_MARK) || !strcmp(e->d_name, ADDING_MARK)) { marks = 1; continue; }
                char sub[1280];
                snprintf(sub, sizeof(sub), "%s/%s", path, e->d_name);
                int r = remove_tree(sub, depth + 1);
                if (r && !first) first = r;
            }
            closedir(d);
        } else first = errno;
        if (marks) {
            if (first) return first;                    /* the marks stay with what stays */
            const char *const mk[2] = { REMOVE_MARK, ADDING_MARK };
            for (int k = 0; k < 2; k++) {
                char sub[1280];
                snprintf(sub, sizeof(sub), "%s/%s", path, mk[k]);
                if (unlink(sub) && errno != ENOENT && !first) first = errno;
            }
        }
        if (rmdir(path) && !first) first = errno;
    } else if (unlink(path)) {
        first = errno;
    }
    return first;
}

/*
 * DELETE on a mod's page. The list forgets the mod at once; its folder is only
 * marked, and goes at the next start (shim_usermods_setup). Taken away now, the
 * index of this run would still send the game to its files, and a picture the
 * game asks for later would not be there.
 */
void shim_usermods_remove(int i) {
    pthread_mutex_lock(&g_list_lock);
    if (i >= 0 && i < g_count) {
        char mark[800];
        snprintf(mark, sizeof(mark), "%s/%s/" REMOVE_MARK, g_dir, g_mods[i].id);
        FILE *f = fopen(mark, "w");
        g_touched = 1;
        if (f && !fclose(f)) {
            if (g_mods[i].mj) settings_write(&g_mods[i], 1, "remove");   /* its settings go with it */
            for (int k = i; k + 1 < g_count; k++) g_mods[k] = g_mods[k + 1];
            g_count--;
        } else {
            /* no mark, no removal: forgotten by the list, the folder would come back at the next start, on */
            __android_log_print(ANDROID_LOG_ERROR, "isaac", "usermods: remove %s: the mark could not be made (%s) - kept, off",
                                g_mods[i].id, strerror(errno));
            if (shim_report_active()) {
                char line[900];
                snprintf(line, sizeof(line), "remove %s: the mark could not be made (%s) - the folder stays, the mod off",
                         g_mods[i].id, strerror(errno));
                report_line("usermods", line);
            }
            g_mods[i].on = 0;
        }
        owed_update();
        list_write("remove");
    }
    pthread_mutex_unlock(&g_list_lock);
}

/* ------------------------------------------------------------ the installer's side (UserMods.java) */

static char *jstr(JNIEnv *env, jstring s) {
    if (!s) return NULL;
    const char *c = (*env)->GetStringUTFChars(env, s, NULL);
    char *out = c ? strdup(c) : NULL;
    if (c) (*env)->ReleaseStringUTFChars(env, s, c);
    return out;
}

/*
 * A mod put in place by ADD MOD. A NEW mod goes first in the list and on. An
 * UPDATE (`keep_from`: the folder of the copy it replaces - its own folder, or
 * the one it had to go beside) keeps that copy's place in the order and its
 * on/off (the owner, 02.10.2026: the player's load order is not broken by an
 * update). The folders in `off` - the same mod's earlier copies the installer
 * could not take away - are switched off, so their files do not stand in for
 * the new ones. Called with g_list_lock held; "" or why mods.txt was not
 * written, in why.
 */
/* an update's settings: the replaced copy's values, by key and by the value's name, where the new copy has them; how
   many of them are not the new copy's default (its section in mod-settings.ini is written then) */
static int settings_carry(umod_t *to, const umod_t *from) {
    if (!to->mj || !from->mj) return 0;
    int n = 0;
    for (int k = 0; k < to->mj->nsettings; k++) {
        const modj_setting_t *s = &to->mj->settings[k];
        const modj_setting_t *was = modj_find(from->mj, s->key);
        char name[64];
        int v;
        if (!was || !modj_value_name(was, from->val[was - from->mj->settings], name, sizeof(name))) continue;
        if (!modj_parse_value(s, name, &v)) {
            NOTE("%s: setting \"%s\" was \"%s\" in %s, no value of it now - the default", to->id, s->key, name, from->id);
            continue;
        }
        to->val[k] = to->start[k] = v;
        if (v != s->def) n++;
    }
    return n;
}

static void list_put(const char *first, const char *keep_from, char **off, int noff, char *why, size_t room) {
    int at = find_mod(first);
    int keep = keep_from && keep_from[0] ? find_mod(keep_from) : -1;
    if (at < 0 && keep < 0 && g_count >= MODS_MAX) { snprintf(why, room, "the list is full: %d mods at most", MODS_MAX); return; }
    umod_t m;
    if (at >= 0) m = g_mods[at];
    else { memset(&m, 0, sizeof(m)); snprintf(m.id, sizeof(m.id), "%s", first); }
    read_mod(&m);
    m.added = 1;
    /*
     * AN UPDATE GOES INTO A FOLDER OF ITS OWN (hotfix 1.73): with no restart after ADD MOD the running game
     * still reads the replaced copy - its index and the guard's word point into that folder, the pairs and
     * content.c's caches are keyed by its name - so the installer leaves it whole, marked to go at the next
     * start, and the new copy takes its place in the list: its on/off, and the values of its settings by
     * key and by the value's name where the new version still has them. The replaced copy leaves the list
     * now, as DELETE takes a mod away.
     */
    if (keep >= 0 && at < 0 && strcmp(keep_from, first) && folder_state(keep_from) == 0) {
        umod_t *old = &g_mods[keep];
        m.on = old->on;
        int carried = settings_carry(&m, old);
        if (old->mj) settings_write(old, 1, "update");
        if (carried) settings_write(&m, 0, "update");
        NOTE("add %s: an update of %s, which goes at the next start - in its place, %d of %d, %s, %d settings carried",
             first, keep_from, keep + 1, g_count, m.on ? "on" : "off", carried);
        g_mods[keep] = m;
    } else if (keep >= 0) {
        /* the update: the replaced copy's place and state */
        m.on = g_mods[keep].on;
        if (at >= 0 && at != keep) {                    /* the new folder listed already (found at the start): out first */
            for (int k = at; k + 1 < g_count; k++) g_mods[k] = g_mods[k + 1];
            g_count--;
            if (at < keep) keep--;
            at = -1;
        }
        if (at >= 0) g_mods[at] = m;                    /* updated in its own folder: where it was */
        else if (g_count < MODS_MAX) {                  /* beside the old folder: just above it */
            for (int k = g_count; k > keep; k--) g_mods[k] = g_mods[k - 1];
            g_mods[keep] = m;
            g_count++;
        }
        NOTE("add %s: an update of %s - kept at %d of %d, %s", first, keep_from, keep + 1, g_count, m.on ? "on" : "off");
    } else {
        m.on = 1;
        int from = at >= 0 ? at : g_count++;
        for (int k = from; k > 0; k--) g_mods[k] = g_mods[k - 1];
        g_mods[0] = m;
    }
    for (int k = 0; k < noff; k++) {
        const char *id = off[k];
        if (!id || !id[0] || strlen(id) >= ID_MAX || !strcmp(id, first)) continue;
        int j = find_mod(id);
        if (j < 0 && g_count < MODS_MAX && folder_state(id) == 1) {
            j = g_count++;
            memset(&g_mods[j], 0, sizeof(g_mods[j]));
            snprintf(g_mods[j].id, sizeof(g_mods[j].id), "%s", id);
            read_mod(&g_mods[j]);
        }
        if (j >= 0) g_mods[j].on = 0;
    }
    g_touched = 1;
    g_owed_add = 1;                                 /* ADD MOD OWES ITS RESTART (hotfix 1.75) */
    owed_update();
    g_list_gen++;
    if (list_write("add")) snprintf(why, room, "%s", g_save_error);
}

JNIEXPORT jstring JNICALL
Java_ru_isaac_port_UserMods_listPut(JNIEnv *env, jclass cls, jstring jdir, jstring jfirst, jobjectArray joff, jstring jkeep) {
    (void) cls;
    char *dir = jstr(env, jdir), *first = jstr(env, jfirst), *keep = jstr(env, jkeep);
    char why[400] = "";
    int n = joff ? (*env)->GetArrayLength(env, joff) : 0;
    char **off = n > 0 ? (char **) calloc((size_t) n, sizeof(char *)) : NULL;
    for (int k = 0; k < n && off; k++) {
        jstring js = (jstring) (*env)->GetObjectArrayElement(env, joff, k);
        off[k] = jstr(env, js);
        if (js) (*env)->DeleteLocalRef(env, js);
    }
    pthread_mutex_lock(&g_list_lock);
    if (!dir || !first || !first[0] || strlen(first) >= ID_MAX) snprintf(why, sizeof(why), "a bad folder name");
    else if (!g_dir[0]) { snprintf(g_dir, sizeof(g_dir), "%s", dir); read_list(); }
    else if (strcmp(g_dir, dir)) snprintf(why, sizeof(why), "the game reads mods from %s, not %s", g_dir, dir);
    if (!why[0]) list_put(first, keep, off, off ? n : 0, why, sizeof(why));
    pthread_mutex_unlock(&g_list_lock);
    for (int k = 0; k < n && off; k++) free(off[k]);
    free(off);
    free(dir);
    free(first);
    free(keep);
    return why[0] ? (*env)->NewStringUTF(env, why) : NULL;
}

/*
 * ADD MOD AND THE START (hotfix 1.74). The picked archives can reach a process
 * that is only starting: Android ended the game behind its file picker, and
 * the answer comes to a new one, before its surface - and setConfigDir - are
 * there. The installer waits for the start's index (gameReady), so what it puts
 * in is this run's change, owed a restart and pale, never half of it indexed;
 * and while it works (installing) the start, should it come all the same, leaves
 * its folders alone. The lock: a start under way finishes first.
 */
JNIEXPORT jboolean JNICALL
Java_ru_isaac_port_UserMods_gameReady(JNIEnv *env, jclass cls) {
    (void) env; (void) cls;
    return g_setup_done ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT void JNICALL
Java_ru_isaac_port_UserMods_installing(JNIEnv *env, jclass cls, jboolean on) {
    (void) env; (void) cls;
    pthread_mutex_lock(&g_list_lock);
    g_installing = on ? 1 : 0;
    pthread_mutex_unlock(&g_list_lock);
}

/* the add window's own RESTART TO APPLY comes when the page's row would (THE RESTART OWED); the report says why */
JNIEXPORT jboolean JNICALL
Java_ru_isaac_port_UserMods_restartOwed(JNIEnv *env, jclass cls) {
    (void) env; (void) cls;
    int owed = shim_usermods_changed();
    NOTE("add window: RESTART TO APPLY %s - ADD MOD %d, the list %d, the disk %d, settings %d, the start's index %s",
         owed ? "shown" : "NOT shown", g_owed_add, g_owed_list, g_owed_disk, g_setup_done ? settings_differ() : 0,
         g_setup_done ? "built" : "NOT built yet");
    return owed ? JNI_TRUE : JNI_FALSE;
}

/*
 * The app's own folder (getFilesDir), named before setConfigDir: the copy of
 * mods.txt inside the app lives in <files>/state (plan 02.10, A4), out of the
 * media store's reach.
 */
JNIEXPORT void JNICALL
Java_ru_isaac_port_UserMods_innerDir(JNIEnv *env, jclass cls, jstring jdir) {
    (void) cls;
    char *dir = jstr(env, jdir);
    if (dir && dir[0]) snprintf(g_inner, sizeof(g_inner), "%s/state", dir);
    free(dir);
}

/* a line into the tester's report from the Java side (nothing when no report is open) */
JNIEXPORT void JNICALL
Java_ru_isaac_port_UserMods_nativeReport(JNIEnv *env, jclass cls, jstring jkind, jstring jtext) {
    (void) cls;
    if (!shim_report_active()) return;
    char *kind = jstr(env, jkind), *text = jstr(env, jtext);
    if (kind && text) report_line(kind, text);
    free(kind);
    free(text);
}

JNIEXPORT jboolean JNICALL
Java_ru_isaac_port_UserMods_reportActive(JNIEnv *env, jclass cls) {
    (void) env; (void) cls;
    return shim_report_active() ? JNI_TRUE : JNI_FALSE;
}
