/*
 * DESK STAND FOR usermods.c (02.10.2026): the real file, compiled for Windows
 * with stubs around it, one process = one start of the game.
 *
 *   desk.exe <command> <config dir> [args]
 *     start             the start (shim_usermods_setup) and the list after it
 *     toggle <i>        the start, then ON/OFF of mod i on the MODS page
 *     remove <i>        the start, then DELETE of mod i
 *     tick              the start, then a later frame: the retry of a list not read / not saved
 *     guard             the start, then the first file the game asks for: the guard over the stacked set
 *     map <inner path>  the start, then the game asks for this resource (repentance-resources/...)
 *     peek <inner path> the start, then portpatch.c asks which mod file would win (no guard, no notes)
 *     settings          the start, then each mod's settings as the MODS page reads them
 *     step <i> <k> <d>  the start, then the page steps setting k of mod i (d = 1 / -1) - mod-settings.ini after it
 *     put <folder> ...  the installer's listPut
 *     script <file>     the start, then the lines of the file in turn, the game running all along (hotfix 1.73: ADD MOD
 *                       with no restart after it): move <staged dir> <mods folder> (the installer's rename into the
 *                       mods folder), mark <mods folder> (.remove), put <folder> <keep|-> [off...], map <inner path>,
 *                       show, settings, ini, gen (the page's list generation and each mod's pending mark)
 *                       (hotfix 1.74, THE RESTART OWED:) toggle <i>, up <i>, down <i>, remove <i> (the page's own
 *                       changes), disk (the page asks with the disk's look due now: the restart owed), owners (this
 *                       run's index, in order), drop <staged dir> <mods folder> (a folder put in by hand)
 *
 * The phone's failures, put in by the environment (a moment's failure of the media store):
 *   DESK_FAIL_OPEN=<path end>:<errno>    fopen for reading answers errno
 *   DESK_FAIL_OPENW=<path end>:<errno>   fopen for writing answers errno (a plain fopen, not the safe writer)
 *   DESK_FAIL_READ=<path end>:<lines>    the read breaks off after so many lines (ferror)
 *   DESK_FAIL_STAT=<path end>:<errno>    stat answers errno
 *   DESK_FAIL_UNLINK=<path end>          unlink and rmdir refuse (EACCES)
 *   DESK_FAIL_WRITE=<path end>           the safe writer cannot open the file (EACCES)
 *   DESK_HEAL=1                          the failures stop before `tick` runs its frame
 *   DESK_INNER=<dir>                     the app's own folder (UserMods.innerDir)
 *   DESK_ROOT=<dir>                      the pack's root; content-cache is beside it
 *   DESK_PORT_PATCH=<inner path end>     the port has a patch for that path (portpatch.c, M8): a mod's .anm2 for it
 *                                        comes back with "<!-- port patch -->" put after its first line; one that
 *                                        is no <AnimatedActor> answers -1
 *   DESK_REAL_PATCH=1                    portpatch.c's real all-or-nothing patch from the assets' port_patch/ (M8,
 *                                        02.10.2026): a mod's menu .anm2 the patch does not land on whole is refused
 *   DESK_PACK=<dir/>                     the pack's own files (default the project's work/app/), ending in /
 *   DESK_LANG=<set>                      the page's language set (0 en, 1 ru, 2 de, 3 es, 4 jp, 5 kr, 6 zh)
 *   DESK_EARLY_PUT=<folder>              (hotfix 1.74) the installer's listPut BEFORE the start, as a game Android
 *                                        ended behind the picker meets the picked archives: g_dir unset, the list
 *                                        read by listPut itself, then setConfigDir's start
 *   DESK_INSTALLING=1                    (hotfix 1.74) ADD MOD is at work in this process when the start comes
 */
#include "compat.h"
#include <errno.h>

static int desk_healed;

static int ends_with(const char *s, const char *tail) {
    size_t n = strlen(s), k = strlen(tail);
    return n >= k && !_stricmp(s + n - k, tail);
}

/* "<path end>:<number>" from the environment, when it names this path */
static int desk_rule(const char *var, const char *path, int *num) {
    if (desk_healed) return 0;
    const char *v = getenv(var);
    if (!v || !*v) return 0;
    char tail[600];
    snprintf(tail, sizeof(tail), "%s", v);
    char *colon = strrchr(tail, ':');
    *num = 0;
    if (colon && colon > tail + 1) { *num = atoi(colon + 1); *colon = 0; }
    for (char *p = tail; *p; p++) if (*p == '\\') *p = '/';
    char p2[1200];
    snprintf(p2, sizeof(p2), "%s", path);
    for (char *p = p2; *p; p++) if (*p == '\\') *p = '/';
    return ends_with(p2, tail);
}

static FILE *g_broken;          /* the file whose read breaks off */
static int g_broken_left, g_broken_failed;

static FILE *desk_fopen(const char *p, const char *m) {
    int e;
    if (m[0] == 'r' && desk_rule("DESK_FAIL_OPEN", p, &e)) { errno = e ? e : EIO; return NULL; }
    if (m[0] == 'w' && desk_rule("DESK_FAIL_OPENW", p, &e)) { errno = e ? e : EIO; return NULL; }
    FILE *f = fopen(p, m);
    int lines;
    if (f && m[0] == 'r' && desk_rule("DESK_FAIL_READ", p, &lines)) { g_broken = f; g_broken_left = lines; g_broken_failed = 0; }
    return f;
}

static char *desk_fgets(char *s, int n, FILE *f) {
    if (f == g_broken) {
        if (g_broken_left-- <= 0) { g_broken_failed = 1; errno = EIO; return NULL; }
    }
    return fgets(s, n, f);
}

static int desk_ferror(FILE *f) { return (f == g_broken && g_broken_failed) || ferror(f); }

static int desk_fclose(FILE *f) {
    if (f == g_broken) g_broken = NULL;
    return fclose(f);
}

static int desk_stat(const char *p, struct stat *st) {
    int e;
    if (desk_rule("DESK_FAIL_STAT", p, &e)) { errno = e ? e : EIO; return -1; }
    return stat(p, st);
}

static int desk_unlink(const char *p) {
    int e;
    if (desk_rule("DESK_FAIL_UNLINK", p, &e)) { errno = EACCES; return -1; }
    return _unlink(p);
}

static int desk_rmdir(const char *p) {
    int e;
    char sub[1200];
    snprintf(sub, sizeof(sub), "%s/x", p);
    (void) sub;
    if (desk_rule("DESK_FAIL_UNLINK", p, &e)) { errno = EACCES; return -1; }
    return _rmdir(p);
}

#define fopen desk_fopen
#define fgets desk_fgets
#define ferror desk_ferror
#define fclose desk_fclose
#define stat(p, st) desk_stat(p, st)
#define unlink desk_unlink
#define rmdir desk_rmdir

/* another usermods.c to compare with (hotfix 1.75: run_jni.sh DESK_USERMODS_C, an earlier one by its .before- copy) */
#ifndef UMDESK_USERMODS
#define UMDESK_USERMODS "../../port/app/src/main/cpp/usermods.c"
#endif
#ifndef ISAAC_ROOT                  /* the project root, ending in "/"; the build passes it */
#define ISAAC_ROOT "../../"
#endif
#include UMDESK_USERMODS

#undef fopen
#undef fclose

static char g_root_s[600];
/* tweaksdesk (its "guard" move): the pack's root from DESK_ROOT, as main sets it here */
void umdesk_root_from_env(void) { const char *r = getenv("DESK_ROOT"); snprintf(g_root_s, sizeof(g_root_s), "%s", r && *r ? r : ""); }
static const char *g_packdir = ISAAC_ROOT "work/app/";

int shim_report_active(void) { return 1; }
void report_line(const char *k, const char *t) { printf("  [report] %s: %s\n", k, t); }
const char *shim_pack_root(void) { return g_root_s; }
unsigned long shim_image_base(void) { return 0; }
/* the phone has no text mode: "w" is bytes as they are; DESK_FAIL_WRITE=<path end> refuses the open */
FILE *shim_safe_fopen(const char *p, const char *m) {
    int e;
    if (desk_rule("DESK_FAIL_WRITE", p, &e)) { errno = EACCES; return NULL; }
    return fopen(p, m[0] == 'w' ? "wb" : m);
}
int shim_safe_fclose(FILE *f) { return fclose(f); }

/*
 * portpatch.c's hand for a mod's own .anm2 (M8), as the desk plays it.
 *   DESK_PORT_PATCH=<path end>: the patched path; the mod's file comes back with "<!-- port patch -->" after its
 *     first line, one that is no <AnimatedActor> answers -1 (test_guard.py's case);
 *   DESK_REAL_PATCH=1: the real thing - portpatch.c's own pp_apply_whole (built with PORTPATCH_DESK) with the
 *     patch of port_patch/index.txt in the assets, over the game's file the pack holds patched (the pack's file
 *     + the patch, as shim_portpatch_open lays it): all or nothing, -2 and the reason when it does not land whole.
 */
int pp_apply(const char *base, long bn, const char *patch, long pn, char **out, long *on,
             void (*sayf)(void *, const char *), void *ctx);
int pp_apply_whole(const char *mine, long mn, const char *game, long gn, const char *patch, long pn,
                   char **out, long *on, char *why, size_t room);
static const char *ASSETS = ISAAC_ROOT "port/app/src/main/assets/";
static char *desk_slurp(const char *p, long *n) {
    FILE *f = fopen(p, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    *n = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *b = (char *) malloc((size_t) *n + 1);
    if (b && fread(b, 1, (size_t) *n, f) != (size_t) *n) { free(b); b = NULL; }
    fclose(f);
    return b;
}
/* line k of the index's patch lines: its target, asset and base; 0 past the end */
static int desk_patch_line(int k, char *target, char *asset, char *base) {
    char p[700];
    snprintf(p, sizeof(p), "%sport_patch/index.txt", ASSETS);
    FILE *f = fopen(p, "rb");
    if (!f) return 0;
    char line[1400];
    int i = 0, found = 0;
    while (!found && fgets(line, sizeof(line), f)) {
        line[strcspn(line, "\r\n")] = 0;
        if (strncmp(line, "patch\t", 6)) continue;
        if (i++ != k) continue;
        char *t = line + 6, *a = strchr(t, '\t');
        if (!a) continue;
        *a++ = 0;
        char *b = strchr(a, '\t');
        if (b) *b++ = 0;
        strcpy(target, t); strcpy(asset, a); strcpy(base, b && *b ? b : t);
        found = 1;
    }
    fclose(f);
    return found;
}
/* the port's retouch as pixels over a mod's sheet (DESK_REAL_PATCH): portpatch.c's own pp_pixels_apply */
int pp_pixels_apply(const unsigned char *png, long n, const unsigned char *patch, long pn, unsigned char **out, long *on,
                    int *mode);
int shim_port_pixels_file(const char *inner, const void *in, long n, void **out, long *outn, char *why, size_t room) {
    if (why && room) why[0] = 0;
    if (!getenv("DESK_REAL_PATCH")) return 0;
    char p[700];
    snprintf(p, sizeof(p), "%sport_patch/index.txt", ASSETS);
    FILE *f = fopen(p, "rb");
    if (!f) return 0;
    char line[1400], asset[700] = "";
    while (!asset[0] && fgets(line, sizeof(line), f)) {
        line[strcspn(line, "\r\n")] = 0;
        if (strncmp(line, "pixels\t", 7)) continue;
        char *t = line + 7, *a = strchr(t, '\t');
        if (!a) continue;
        *a++ = 0;
        if (!_stricmp(t, inner)) snprintf(asset, sizeof(asset), "%s", a);
    }
    fclose(f);
    if (!asset[0]) return 0;
    snprintf(p, sizeof(p), "%s%s", ASSETS, asset);
    long pn = 0;
    char *patch = desk_slurp(p, &pn);
    if (!patch) return 0;
    unsigned char *o; long on;
    int mode = 0;
    int r = pp_pixels_apply((const unsigned char *) in, n, (const unsigned char *) patch, pn, &o, &on, &mode);
    free(patch);
    if (r < 0) { if (why) snprintf(why, room, "the picture does not read here"); return -1; }
    if (r > 0) { if (why) snprintf(why, room, "not the game's size"); return -2; }
    *out = o; *outn = on;
    return 1;
}
const char *shim_port_patch_target(int i) {
    static char t[64][400];
    char a[400], b[400];
    if (!getenv("DESK_REAL_PATCH") || i >= 64 || !desk_patch_line(i, t[i], a, b)) return NULL;
    return t[i];
}
int shim_port_patch_file(const char *inner, const void *in, long n, void **out, long *outn, char *why, size_t room) {
    int e;
    if (why && room) why[0] = 0;
    if (getenv("DESK_REAL_PATCH")) {
        char t[400], a[400], b[400];
        for (int k = 0; desk_patch_line(k, t, a, b); k++) {
            if (_stricmp(t, inner)) continue;
            char p[900];
            long pn = 0, bn = 0, gn = 0, on = 0;
            snprintf(p, sizeof(p), "%s%s", ASSETS, a);
            char *patch = desk_slurp(p, &pn);
            snprintf(p, sizeof(p), "%s%s", g_packdir, b);
            char *base = desk_slurp(p, &bn), *game = NULL, *o = NULL;
            if (!patch || !base || pp_apply(base, bn, patch, pn, &game, &gn, NULL, NULL)) return 0;
            int r = pp_apply_whole((const char *) in, n, game, gn, patch, pn, &o, &on, why, room);
            free(patch); free(base); free(game);
            if (r < 0) return -1;
            if (r > 0) return -2;
            *out = o; *outn = on;
            return 1;
        }
        return 0;
    }
    if (!desk_rule("DESK_PORT_PATCH", inner, &e)) return 0;
    const char *b = (const char *) in;
    if (n < 14 || !strstr(b, "<AnimatedActor")) return -1;
    const char *nl = memchr(b, '\n', (size_t) n);
    long at = nl ? (long) (nl - b) + 1 : n;
    static const char MARK[] = "<!-- port patch -->\n";
    char *o = (char *) malloc((size_t) n + sizeof(MARK));
    memcpy(o, b, (size_t) at);
    memcpy(o + at, MARK, sizeof(MARK) - 1);
    memcpy(o + at + sizeof(MARK) - 1, b + at, (size_t) (n - at));
    *out = o;
    *outn = n + (long) sizeof(MARK) - 1;
    return 1;
}

/* the phone's pack answers from memory; the desk reads the file - its time is counted apart */
static long long g_peek_ms;
static const void *peek_file(const char *inner, long *length);
const void *shim_pack_peek(const char *inner, long *length) {
    long long t = now_ms();
    const void *r = peek_file(inner, length);
    g_peek_ms += now_ms() - t;
    return r;
}

/* the pack has the file (the desk: on the disk, case-blind as Windows is) */
int shim_pack_has(const char *inner) {
    char p[1400];
    struct _stat64 st;
    snprintf(p, sizeof(p), "%s%s", g_packdir, inner);
    return _stat64(p, &st) == 0;
}

static const void *peek_file(const char *inner, long *length) {
    char p[1400];
    snprintf(p, sizeof(p), "%s%s", g_packdir, inner);
    /* the port's own files laid into the pack (portpatch.c): the other version's tables (content.c 1a) */
    if (!strncmp(inner, "port-version-ref/", 17)) snprintf(p, sizeof(p), "%sport_patch/%s", ASSETS, inner);
    FILE *f = fopen(p, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *b = (char *) malloc((size_t) n + 1);          /* leaked: a desk run */
    if (b && fread(b, 1, (size_t) n, f) != (size_t) n) { free(b); b = NULL; }
    fclose(f);
    if (b && length) *length = n;
    return b;
}

static void show(const char *what) {
    printf("== %s: %d mods, unread=%d (errno %d), from=%s, changed=%d, save_error=\"%s\"\n", what, g_count, g_list_unread,
           g_list_errno, g_list_from == LIST_FILE ? "file" : g_list_from == LIST_INNER ? "inner copy" : "folders", shim_usermods_changed(),
           g_save_error);
    for (int i = 0; i < g_count; i++)
        printf("   %d %s %s%s files=%d%s%s\n", i, g_mods[i].on ? "on " : "off", g_mods[i].id,
               g_mods[i].reach ? " (unreachable)" : "", g_mods[i].files, g_mods[i].indexed ? " indexed" : "",
               shim_usermods_pending(i) ? " pending" : "");
}

/* each mod as the MODS page reads it: its texts, what json.c said, its settings and the layers in use */
static void show_settings(void) {
    printf("== settings: changed=%d settings_error=\"%s\"\n", shim_usermods_changed(), shim_usermods_settings_error());
    for (int i = 0; i < g_count; i++) {
        int broken;
        char reason[32];
        int refused = shim_usermods_manifest_note(i, &broken, reason, sizeof(reason));
        printf("   MOD %s name=\"%s\" version=\"%s\" author=\"%s\" refused=%d broken=%d reason=%s layers=%s\n",
               g_mods[i].id, shim_usermods_name(i), shim_usermods_version(i), shim_usermods_author(i), refused, broken,
               reason[0] ? reason : "-", g_mods[i].layers[0] ? g_mods[i].layers : "-");
        printf("   DESC %s %s\n", g_mods[i].id, shim_usermods_description(i));
        for (int k = 0; k < shim_usermods_settings(i); k++) {
            int kind = shim_usermods_setting_kind(i, k), v = shim_usermods_setting_value(i, k), lo, hi, st;
            shim_usermods_setting_range(i, k, &lo, &hi, &st);
            printf("   SET %s %d kind=%d label=\"%s\" value=%d start=%d", g_mods[i].id, k, kind, shim_usermods_setting_label(i, k),
                   v, g_mods[i].start[k]);
            if (kind == 1) {
                printf(" shows=\"%s\" values=", shim_usermods_setting_value_label(i, k, v));
                for (int c = 0; c < shim_usermods_setting_count(i, k); c++)
                    printf("%s%s", c ? "|" : "", shim_usermods_setting_value_label(i, k, c));
            }
            if (kind == 2) printf(" range=%d..%d/%d", lo, hi, st);
            printf("\n");
        }
        if (g_mods[i].issues) printf("   ISSUES %s\n%s", g_mods[i].id, g_mods[i].issues);
    }
}

static void show_ini(const char *cfg) {
    char p[700];
    snprintf(p, sizeof(p), "%s/mods/mod-settings.ini", cfg);
    FILE *f = fopen(p, "rb");
    printf("== mod-settings.ini%s\n", f ? ":" : " is not there");
    if (!f) return;
    char line[600];
    while (fgets(line, sizeof(line), f)) printf("   | %s", line);
    fclose(f);
}

/* the installer's listPut from another stand (tools/tweaksdesk: ADD MOD while the MODS page is open, hotfix 1.73) */
void umdesk_list_put(const char *first, const char *keep, char **off, int noff) {
    char why[400] = "";
    pthread_mutex_lock(&g_list_lock);
    list_put(first, keep, off, noff, why, sizeof(why));
    pthread_mutex_unlock(&g_list_lock);
    printf("== put %s: %s, gen %d\n", first, why[0] ? why : "written", shim_usermods_list_gen());
}

/* the page's next ask looks at the disk at once (hotfix 1.74, THE RESTART OWED: every DISK_EVERY_MS on the phone) */
void umdesk_disk_now(void) { g_disk_at = 0; }

int main(int argc, char **argv) {
    if (argc < 3) { fprintf(stderr, "desk.exe start|toggle|remove|tick|guard|map <config> [arg]\n"); return 2; }
    setvbuf(stdout, NULL, _IONBF, 0);
    const char *cmd = argv[1], *cfg = argv[2];
    const char *inner = getenv("DESK_INNER");
    if (inner && *inner) snprintf(g_inner, sizeof(g_inner), "%s/state", inner);
    const char *pack = getenv("DESK_PACK");          /* the pack's files: work/app by default */
    if (pack && *pack) g_packdir = pack;
    const char *root = getenv("DESK_ROOT");
    snprintf(g_root_s, sizeof(g_root_s), "%s", root && *root ? root : "");
    const char *lang = getenv("DESK_LANG");
    if (lang && *lang) shim_usermods_lang(atoi(lang));
    const char *early = getenv("DESK_EARLY_PUT");
    if (early && *early) {
        /* Java_ru_isaac_port_UserMods_listPut's road with no start yet: its own read of the list, then list_put */
        char why[400] = "";
        pthread_mutex_lock(&g_list_lock);
        snprintf(g_dir, sizeof(g_dir), "%s/mods", cfg);
        read_list();
        list_put(early, NULL, NULL, 0, why, sizeof(why));
        pthread_mutex_unlock(&g_list_lock);
        printf("== early put %s: %s\n", early, why[0] ? why : "written");
        g_dir[0] = 0;                               /* setConfigDir names it again */
    }
    const char *busy = getenv("DESK_INSTALLING");
    if (busy && *busy == '1') g_installing = 1;
    shim_usermods_setup(cfg);
    show("after the start");
    if (!strcmp(cmd, "toggle") && argc > 3) { shim_usermods_toggle(atoi(argv[3])); show("after toggle"); }
    else if (!strcmp(cmd, "remove") && argc > 3) { shim_usermods_remove(atoi(argv[3])); show("after remove"); show_ini(cfg); }
    else if (!strcmp(cmd, "settings")) { show_settings(); show_ini(cfg); }
    else if (!strcmp(cmd, "step") && argc > 5) {
        for (int a = 3; a + 2 < argc; a += 3) shim_usermods_setting_step(atoi(argv[a]), atoi(argv[a + 1]), atoi(argv[a + 2]));
        show_settings();
        show_ini(cfg);
    } else if (!strcmp(cmd, "peek") && argc > 3) {
        char p[1400];
        snprintf(p, sizeof(p), "%s/%s", g_root_s, argv[3]);
        const char *got = shim_usermods_peek(p);
        printf("== peek %s -> %s (guard ran: %d)\n", argv[3], got ? got : "(the game's own)", g_guard_ran);
    }
    else if (!strcmp(cmd, "put") && argc > 4) {
        /* put <folder> <the folder an update replaces, or -> [off folders...]: UserMods.listPut */
        char why[400] = "";
        pthread_mutex_lock(&g_list_lock);
        list_put(argv[3], strcmp(argv[4], "-") ? argv[4] : NULL, argv + 5, argc - 5, why, sizeof(why));
        pthread_mutex_unlock(&g_list_lock);
        printf("== put: %s\n", why[0] ? why : "written");
        show("after put");
    }
    else if (!strcmp(cmd, "script") && argc > 3) {
        /* the game runs on while the installer works (hotfix 1.73): its steps in turn, one process */
        FILE *sf = fopen(argv[3], "r");
        char line[1400];
        while (sf && fgets(line, sizeof(line), sf)) {
            char *w[16];
            int nw = 0;
            for (char *t = strtok(line, " \t\r\n"); t && nw < 16; t = strtok(NULL, " \t\r\n")) w[nw++] = t;
            if (!nw || w[0][0] == '#') continue;
            if (!strcmp(w[0], "move") && nw > 2) {
                char to[900];
                snprintf(to, sizeof(to), "%s/%s", g_dir, w[2]);
                printf("== move %s -> %s: %s\n", w[1], w[2], rename(w[1], to) ? strerror(errno) : "ok");
            } else if (!strcmp(w[0], "mark") && nw > 1) {
                char m[900];
                snprintf(m, sizeof(m), "%s/%s/" REMOVE_MARK, g_dir, w[1]);
                FILE *f = fopen(m, "w");
                printf("== mark %s: %s\n", w[1], f ? "ok" : strerror(errno));
                if (f) fclose(f);
            } else if (!strcmp(w[0], "put") && nw > 2) {
                char why[400] = "";
                int g0 = shim_usermods_list_gen();
                pthread_mutex_lock(&g_list_lock);
                list_put(w[1], strcmp(w[2], "-") ? w[2] : NULL, w + 3, nw - 3, why, sizeof(why));
                pthread_mutex_unlock(&g_list_lock);
                printf("== put %s: %s, gen %d -> %d\n", w[1], why[0] ? why : "written", g0, shim_usermods_list_gen());
                show("after put");
            } else if (!strcmp(w[0], "map") && nw > 1) {
                char p[1400];
                snprintf(p, sizeof(p), "%s/%s", g_root_s, w[1]);
                const char *got = shim_usermods_map(p);
                printf("== map %s -> %s\n", w[1], got ? got : "(the game's own)");
            } else if (!strcmp(w[0], "show")) show("now");
            else if (!strcmp(w[0], "settings")) show_settings();
            else if (!strcmp(w[0], "ini")) show_ini(cfg);
            else if (!strcmp(w[0], "toggle") && nw > 1) { shim_usermods_toggle(atoi(w[1])); show("after toggle"); }
            else if (!strcmp(w[0], "up") && nw > 1) { shim_usermods_move(atoi(w[1]), -1); show("after up"); }
            else if (!strcmp(w[0], "down") && nw > 1) { shim_usermods_move(atoi(w[1]), 1); show("after down"); }
            else if (!strcmp(w[0], "remove") && nw > 1) { shim_usermods_remove(atoi(w[1])); show("after remove"); }
            else if (!strcmp(w[0], "drop") && nw > 2) {
                char to[900];
                snprintf(to, sizeof(to), "%s/%s", g_dir, w[2]);
                printf("== drop %s -> %s: %s\n", w[1], w[2], rename(w[1], to) ? strerror(errno) : "ok");
            } else if (!strcmp(w[0], "replace") && nw > 2) {
                /* a file of the mods folder written over by hand (mods.txt edited in a file manager) */
                char to[900];
                snprintf(to, sizeof(to), "%s/%s", g_dir, w[2]);
                remove(to);
                printf("== replace %s -> %s: %s\n", w[1], w[2], rename(w[1], to) ? strerror(errno) : "ok");
            } else if (!strcmp(w[0], "disk")) {
                g_disk_at = 0;                          /* the look is due: the fake thread runs it at once */
                int c = shim_usermods_changed();
                printf("== disk changed=%d list=%d disk=%d\n", c, g_owed_list, g_owed_disk);
            } else if (!strcmp(w[0], "owners")) {
                printf("OWNERS ");
                for (int i = 0; i < g_owners; i++) printf("%s%s", i ? "," : "", g_owner[i]);
                printf("\n");
            }
            else if (!strcmp(w[0], "gen")) {
                printf("== gen %d changed=%d\n", shim_usermods_list_gen(), shim_usermods_changed());
                for (int i = 0; i < g_count; i++) printf("   PENDING %s %d\n", g_mods[i].id, shim_usermods_pending(i));
            } else printf("?? %s\n", w[0]);
        }
        if (sf) fclose(sf);
        else printf("== script %s: %s\n", argv[3], strerror(errno));
    }
    else if (!strcmp(cmd, "tick")) {
        if (getenv("DESK_HEAL")) desk_healed = 1;
        g_retry_at = 0;
        int unread = shim_usermods_list_unread();
        printf("== the page asked: unread=%d\n", unread);
        show("after a later frame");
    } else if (!strcmp(cmd, "guard") || !strcmp(cmd, "map")) {
        /* map <path> [<path>...]: the game asks for each in turn (a bare set "repentance-resources/dlc3.ru" is the
           engine listing its language set at the mount) */
        for (int a = 3; a == 3 || a < argc; a++) {
            char p[1400];
            snprintf(p, sizeof(p), "%s/%s", g_root_s, argc > a ? argv[a] : "repentance-resources/data/nothing.xml");
            const char *got = shim_usermods_map(p);
            printf("== map %s -> %s\n", argc > a ? argv[a] : "(the first file)", got ? got : "(the game's own)");
        }
        for (int i = 0; i < g_count; i++)
            printf("   %s: guard completed %d, the game's %d, of the wrong size %d\n", g_mods[i].id, shim_usermods_guard_fixed(i),
                   shim_usermods_guard_game(i), shim_usermods_guard_size(i));
        for (int i = 0; i < g_count; i++) {
            /* the MODS page's line of a table past the loader's array (M4), as tweaks.c gets it */
            char file[96], tag[48], id[32];
            long lo = 0, hi = 0;
            int n = shim_usermods_guard_bounds(i, file, sizeof(file), tag, sizeof(tag), id, sizeof(id), &lo, &hi);
            if (n) printf("   BOUNDS %s %d %s %s %s %ld..%ld\n", g_mods[i].id, n, file, tag, id, lo, hi);
        }
        for (int i = 0; i < g_count; i++) {
            /* the mods over each other, as tweaks.c gets them (THE MODS OVER EACH OTHER) */
            int other, stronger, files, am, sm;
            unsigned kinds, sw, sh, iw, ih;
            char oid[80], aid[80], sid[80], lang[16], sheet[300];
            for (int k = 0; shim_usermods_over(i, k, &other, oid, sizeof(oid), &stronger, &files, &kinds, lang, sizeof(lang)); k++)
                printf("   OVER %s %s %s files=%d kinds=%u lang=%s page=%d\n", g_mods[i].id, stronger ? "over" : "under", oid,
                       files, kinds, lang[0] ? lang : "-", other);
            int anms;
            char first[300];
            for (int k = 0; shim_usermods_broken(i, k, &am, aid, &sm, sid, sizeof(aid), sheet, sizeof(sheet), &sw, &sh, &iw, &ih,
                                                 &anms, first, sizeof(first)); k++)
                printf("   BROKEN %s anm_of=%s anms=%d first=%s sheet_of=%s sheet=%s served=%ux%u cut_for=%ux%u pages=%d,%d\n",
                       g_mods[i].id, aid, anms, first, sid[0] ? sid : "(game)", sheet, sw, sh, iw, ih, am, sm);
        }
        /* THE PAIRS, as the page asks them: every two of the list, the upper over the lower */
        printf("   PAIRS gen=%d\n", shim_usermods_pairs_gen());
        for (int i = 0; i < g_count; i++) {
            int first, n = shim_usermods_conflicts(i, &first);
            printf("   CONFLICTS %s n=%d first=%s\n", g_mods[i].id, n, first >= 0 ? g_mods[first].id : "-");
            for (int j = 0; j < g_count; j++) {
                if (j == i) continue;
                int mine, files, anms, own;
                unsigned kinds, sw, sh, iw, ih;
                char lang[16], sheet[300];
                if (shim_usermods_pair_over(i, j, &mine, &files, &kinds, lang, sizeof(lang)))
                    printf("   PAIROVER %s %s mine_over=%d files=%d kinds=%u lang=%s\n", g_mods[i].id, g_mods[j].id, mine, files,
                           kinds, lang[0] ? lang : "-");
                for (int k = 0; shim_usermods_pair_broken(i, j, k, &mine, sheet, sizeof(sheet), &sw, &sh, &iw, &ih, &anms, &own); k++)
                    printf("   PAIRBROKEN %s %s anm_mine=%d sheet=%s served=%ux%u cut_for=%ux%u anms=%d own=%d\n", g_mods[i].id,
                           g_mods[j].id, mine, sheet, sw, sh, iw, ih, anms, own);
            }
        }
        if (!strcmp(cmd, "guard"))
            for (int b = 0; b < BUCKETS; b++)
                for (ufile_t *e = g_index[b]; e; e = e->next)
                    if (e->guard == GUARD_FIXED || e->guard == GUARD_REFUSED)
                        printf("   VERDICT %s %s %s | %s\n", e->guard == GUARD_FIXED ? "fixed  " : "refused", owner_id(e), e->key,
                               e->why ? e->why : "");
    }
    printf("== the desk's own reading of the game's files (memory on the phone): %lld ms\n", g_peek_ms);
    return 0;
}
