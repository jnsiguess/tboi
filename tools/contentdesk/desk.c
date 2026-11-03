/* desk: the real content.c (its core: content_build) run over the cases run.py writes from the model's
 * --verify corpus and --test cases. For each case directory named in the list file:
 *   case.txt     fname=F, game=<the game's data dir>, port=port.bin (optional), mod=<id> per mod, strongest first
 *   over/<name>  a game file the model's case gave other bytes (its "_Fixed" game)
 *   mods/<k>/legacy|content|patch   the k-th mod's resources/F, content/F, patch/F
 * writes got.bin (the output), got_lines.txt (the report lines), got_base.txt (the base's id).
 *
 *   desk.exe LIST [refuse]   refuse: answer the legacy refusal hook with "no" for every file (it must
 *                            change nothing: the model's own checks come first)
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../../port/app/src/main/cpp/content.h"

static char *slurp(const char *path, long *len) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *b = (char *) malloc((size_t) n + 1);
    if (b && fread(b, 1, (size_t) n, f) != (size_t) n) { free(b); b = NULL; }
    fclose(f);
    if (b) { b[n] = 0; *len = n; }
    return b;
}

static void spit(const char *path, const char *b, size_t n) {
    FILE *f = fopen(path, "wb");
    if (!f) { fprintf(stderr, "cannot write %s\n", path); exit(2); }
    fwrite(b, 1, n, f);
    fclose(f);
}

typedef struct { char name[128]; char *b; long n; } gfile_t;
typedef struct { char dir[1024], gamedir[1024], otherdir[1024]; gfile_t f[64]; int nf; gfile_t o[64]; int no; } gctx_t;

static const char *game_file(void *ctx, const char *name, long *len) {
    gctx_t *g = (gctx_t *) ctx;
    for (int i = 0; i < g->nf; i++) if (!strcmp(g->f[i].name, name)) { *len = g->f[i].n; return g->f[i].b; }
    char p[2048];
    long n = 0;
    snprintf(p, sizeof(p), "%s/over/%s", g->dir, name);
    char *b = slurp(p, &n);
    if (!b) { snprintf(p, sizeof(p), "%s/%s", g->gamedir, name); b = slurp(p, &n); }
    if (g->nf < 64) { snprintf(g->f[g->nf].name, sizeof(g->f[0].name), "%s", name); g->f[g->nf].b = b; g->f[g->nf].n = n; g->nf++; }
    *len = n;
    return b;
}

/* the other version's table (other=<dir> in case.txt), or NULL */
static const char *other_file(void *ctx, const char *name, long *len) {
    gctx_t *g = (gctx_t *) ctx;
    *len = 0;
    if (!g->otherdir[0]) return NULL;
    for (int i = 0; i < g->no; i++) if (!strcmp(g->o[i].name, name)) { *len = g->o[i].n; return g->o[i].b; }
    char p[2048];
    long n = 0;
    snprintf(p, sizeof(p), "%s/%s", g->otherdir, name);
    char *b = slurp(p, &n);
    if (g->no < 64) { snprintf(g->o[g->no].name, sizeof(g->o[0].name), "%s", name); g->o[g->no].b = b; g->o[g->no].n = n; g->no++; }
    *len = b ? n : 0;
    return b;
}

static int refuse_none(void *ctx, const char *fname, const char *buf, long len, const char *game, long glen, char *why, size_t room) {
    (void) ctx; (void) fname; (void) buf; (void) len; (void) game; (void) glen; (void) why; (void) room;
    return 0;
}

int main(int argc, char **argv) {
    if (argc < 2) { fprintf(stderr, "desk LIST [refuse]\n"); return 2; }
    int with_refuse = argc > 2 && !strcmp(argv[2], "refuse");
    FILE *list = fopen(argv[1], "r");
    if (!list) { fprintf(stderr, "no list %s\n", argv[1]); return 2; }
    char dir[1024];
    int cases = 0;
    while (fgets(dir, sizeof(dir), list)) {
        dir[strcspn(dir, "\r\n")] = 0;
        if (!dir[0]) continue;
        char p[2048];
        long n = 0;
        snprintf(p, sizeof(p), "%s/case.txt", dir);
        char *spec = slurp(p, &n);
        if (!spec) { fprintf(stderr, "no %s\n", p); return 2; }
        static gctx_t g;
        for (int i = 0; i < g.nf; i++) free(g.f[i].b);
        for (int i = 0; i < g.no; i++) free(g.o[i].b);
        memset(&g, 0, sizeof(g));
        snprintf(g.dir, sizeof(g.dir), "%s", dir);
        char fname[128] = "";
        cmodf_t mods[64];
        char ids[64][128];
        int nm = 0;
        char *port = NULL;
        long portn = 0;
        for (char *line = strtok(spec, "\n"); line; line = strtok(NULL, "\n")) {
            line[strcspn(line, "\r")] = 0;
            if (!strncmp(line, "fname=", 6)) snprintf(fname, sizeof(fname), "%s", line + 6);
            else if (!strncmp(line, "game=", 5)) snprintf(g.gamedir, sizeof(g.gamedir), "%s", line + 5);
            else if (!strncmp(line, "other=", 6)) snprintf(g.otherdir, sizeof(g.otherdir), "%s", line + 6);
            else if (!strncmp(line, "port=", 5)) { snprintf(p, sizeof(p), "%s/%s", dir, line + 5); port = slurp(p, &portn); }
            else if (!strncmp(line, "mod=", 4) && nm < 64) {
                cmodf_t *m = &mods[nm];
                memset(m, 0, sizeof(*m));
                snprintf(ids[nm], sizeof(ids[0]), "%s", line + 4);
                m->id = ids[nm];
                snprintf(p, sizeof(p), "%s/mods/%d/legacy", dir, nm);
                m->legacy = slurp(p, &m->legacy_n);
                snprintf(p, sizeof(p), "%s/mods/%d/content", dir, nm);
                m->content = slurp(p, &m->content_n);
                snprintf(p, sizeof(p), "%s/mods/%d/patch", dir, nm);
                m->patch = slurp(p, &m->patch_n);
                nm++;
            }
        }
        cgame_t game = { game_file, &g, with_refuse ? refuse_none : NULL, other_file };
        cbuf_t out = { 0 }, lines = { 0 };
        char base[128];
        int r = content_build(fname, mods, nm, &game, port, portn, &out, &lines, base, sizeof(base));
        snprintf(p, sizeof(p), "%s/got.bin", dir);
        if (r == 0) spit(p, out.s ? out.s : "", out.n); else remove(p);
        snprintf(p, sizeof(p), "%s/got_lines.txt", dir);
        spit(p, lines.s ? lines.s : "", lines.n);
        snprintf(p, sizeof(p), "%s/got_base.txt", dir);
        spit(p, base, strlen(base));
        cbuf_free(&out);
        cbuf_free(&lines);
        for (int i = 0; i < nm; i++) { free((void *) mods[i].legacy); free((void *) mods[i].content); free((void *) mods[i].patch); }
        free(port);
        free(spec);
        cases++;
    }
    fclose(list);
    printf("desk: %d cases built\n", cases);
    return 0;
}
