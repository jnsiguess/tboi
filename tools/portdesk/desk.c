/*
 * portdesk: portpatch.c's own functions on the desk, driven by run.py.
 *   portdesk apply BASE PATCH OUT       the result to OUT, report lines to stdout
 *   portdesk variant BASE PCT OUT
 *   portdesk number FILE layer|null NAME
 *   portdesk whole MINE BASE PATCH [OUT] a mod's file under the patch, all or nothing (pp_apply_whole) against
 *                                       BASE + PATCH: "0" (OUT written), "1 <why>" or "-1 <why>"
 *   portdesk pixels PNG PATCH OUT       the port's retouch as pixels over a sheet (pp_pixels_apply): "0|1|-1 mode"
 *                                       (mode 2 whole, 1 the strap's footprint only)
 *   portdesk png IN OUT                 the PNG reader alone: IN back out as 8-bit RGBA
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int pp_apply(const char *base, long bn, const char *patch, long pn, char **out, long *on,
             void (*sayf)(void *, const char *), void *ctx);
int pp_variant(const char *base, long bn, int pct, char **out, long *on);
int pp_number(const char *b, long n, int is_null, const char *name);
int pp_apply_whole(const char *mine, long mn, const char *game, long gn, const char *patch, long pn,
                   char **out, long *on, char *why, size_t room);
int pp_pixels_apply(const unsigned char *png, long n, const unsigned char *patch, long pn, unsigned char **out, long *on,
                    int *mode);
int pp_png_read(const unsigned char *b, long n, unsigned char **rgba, int *w, int *h);
int pp_png_write(const unsigned char *rgba, int w, int h, unsigned char **png, long *n);

static char *slurp(const char *p, long *n) {
    FILE *f = fopen(p, "rb");
    if (!f) { fprintf(stderr, "cannot open %s\n", p); exit(2); }
    fseek(f, 0, SEEK_END); *n = ftell(f); fseek(f, 0, SEEK_SET);
    char *b = (char *) malloc((size_t) *n + 1);
    if (fread(b, 1, (size_t) *n, f) != (size_t) *n) exit(2);
    fclose(f);
    return b;
}

static void spit(const char *p, const char *b, long n) {
    FILE *f = fopen(p, "wb");
    if (!f || fwrite(b, 1, (size_t) n, f) != (size_t) n) exit(2);
    fclose(f);
}

static void say(void *ctx, const char *line) { (void) ctx; printf("%s\n", line); }

int main(int argc, char **argv) {
    if (argc >= 5 && !strcmp(argv[1], "apply")) {
        long bn, pn, on; char *out;
        char *b = slurp(argv[2], &bn), *p = slurp(argv[3], &pn);
        if (pp_apply(b, bn, p, pn, &out, &on, say, NULL)) { printf("FAILED\n"); return 1; }
        spit(argv[4], out, on);
        return 0;
    }
    if (argc >= 5 && !strcmp(argv[1], "variant")) {
        long bn, on; char *out;
        char *b = slurp(argv[2], &bn);
        if (pp_variant(b, bn, atoi(argv[3]), &out, &on)) { printf("FAILED\n"); return 1; }
        spit(argv[4], out, on);
        return 0;
    }
    if (argc >= 5 && !strcmp(argv[1], "number")) {
        long n;
        char *b = slurp(argv[2], &n);
        printf("%d\n", pp_number(b, n, !strcmp(argv[3], "null"), argv[4]));
        return 0;
    }
    if (argc >= 5 && !strcmp(argv[1], "whole")) {
        long mn, bn, pn, gn, on; char *game, *out, why[400];
        char *m = slurp(argv[2], &mn), *b = slurp(argv[3], &bn), *p = slurp(argv[4], &pn);
        if (pp_apply(b, bn, p, pn, &game, &gn, NULL, NULL)) { printf("BASE FAILED\n"); return 1; }
        int r = pp_apply_whole(m, mn, game, gn, p, pn, &out, &on, why, sizeof(why));
        printf("%d%s%s\n", r, why[0] ? " " : "", why);
        if (!r && argc >= 6) spit(argv[5], out, on);
        return 0;
    }
    if (argc >= 5 && !strcmp(argv[1], "pixels")) {
        long n, pn, on; unsigned char *out;
        char *b = slurp(argv[2], &n), *p = slurp(argv[3], &pn);
        int mode = 0;
        int r = pp_pixels_apply((const unsigned char *) b, n, (const unsigned char *) p, pn, &out, &on, &mode);
        printf("%d %d\n", r, mode);
        if (!r) spit(argv[4], (const char *) out, on);
        return 0;
    }
    if (argc >= 4 && !strcmp(argv[1], "png")) {
        /* read a PNG and write it back as 8-bit RGBA: the reader on its own */
        long n, on; unsigned char *rgba, *out; int w, h;
        char *b = slurp(argv[2], &n);
        if (pp_png_read((const unsigned char *) b, n, &rgba, &w, &h)) { printf("-1\n"); return 0; }
        pp_png_write(rgba, w, h, &out, &on);
        spit(argv[3], (const char *) out, on);
        printf("0 %dx%d\n", w, h);
        return 0;
    }
    fprintf(stderr, "usage: portdesk apply|variant|number|whole|pixels|png ...\n");
    return 2;
}
