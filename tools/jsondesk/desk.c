/*
 * DESK STAND FOR json.c (02.10.2026): the real file, compiled for Windows.
 *
 *   jsondesk.exe selftest              the same `json: selftest` the report build runs; exit code = failed checks
 *   jsondesk.exe parse <file>          is it JSON: ok with the node count, or line/column/byte and why
 *   jsondesk.exe mod <mod.json> [lang] what the port reads from it: the mod's fields in that language
 *                                      (default en), the settings, every issue as check.txt would get it
 *
 * build: bash build.sh  (LLVM clang + the MSVC runtime, as tools/umdesk)
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "json.h"

static int parse_file(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) { perror(path); return 2; }
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *buf = malloc(n > 0 ? (size_t) n : 1);
    size_t got = buf ? fread(buf, 1, (size_t) n, f) : 0;
    fclose(f);
    json_doc_t d;
    json_error_t e;
    int ok = json_parse(&d, buf, got, &e);
    if (ok) printf("ok: %u values\n", d.count);
    else printf("refused: line %d, column %d (byte %lu): %s\n", e.line, e.col, (unsigned long) e.at, e.what);
    json_free(&d);
    free(buf);
    return ok ? 0 : 1;
}

static const char *show(const char *s) { return s ? s : "(absent)"; }

static int mod_file(const char *path, const char *lang) {
    static modj_t m;
    static const char *const kinds[] = { "switch", "choice", "number" };
    int ok = modj_read_file(&m, path);
    printf("read: %s\n", ok ? "ok" : "BROKEN");
    if (ok) {
        printf("id: %s\nname: %s\nversion: %s\nauthor: %s\ndescription: %s\n", show(m.id), show(modj_text(m.name, lang)),
               show(modj_text(m.version, lang)), show(modj_text(m.author, lang)), show(modj_text(m.description, lang)));
        for (int j = 0; j < m.nsettings; j++) {
            const modj_setting_t *s = &m.settings[j];
            char v[64];
            modj_value_name(s, s->def, v, sizeof(v));
            printf("setting %d %s (%s) \"%s\" default %s restart %d", s->index + 1, s->key, kinds[s->kind],
                   show(modj_text(s->label, lang)), v, s->restart);
            if (s->kind == MODJ_NUMBER) printf(" range %d..%d step %d", s->min, s->max, s->step);
            for (int k = 0; k < s->nvalues; k++) printf("%s%s=\"%s\"", k ? ", " : " values ", s->values[k].value,
                                                        show(modj_text(s->values[k].label, lang)));
            printf("\n");
        }
    }
    for (int j = 0; j < m.nissues; j++)
        printf("%s [%s]: %s\n", modj_level_name(m.issues[j].level), modj_reason_code(m.issues[j].reason), m.issues[j].text);
    if (m.issues_lost) printf("... and %d more issues\n", m.issues_lost);
    modj_free(&m);
    return ok ? 0 : 1;
}

int main(int argc, char **argv) {
    if (argc >= 2 && !strcmp(argv[1], "selftest")) return shim_json_selftest();
    if (argc >= 3 && !strcmp(argv[1], "parse")) return parse_file(argv[2]);
    if (argc >= 3 && !strcmp(argv[1], "mod")) return mod_file(argv[2], argc >= 4 ? argv[3] : "en");
    fprintf(stderr, "usage: jsondesk selftest | parse <file> | mod <mod.json> [lang]\n");
    return 2;
}
