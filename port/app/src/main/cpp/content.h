/*
 * content.c - a data table of the game, built from the game's file and the
 * enabled mods (plan 30.09, 4.9 M3 and 4.10 part 3). The spec is
 * tools/content_sim.py; tools/contentdesk/ checks this code against it byte
 * for byte.
 */
#ifndef CONTENT_H
#define CONTENT_H
#include <stddef.h>

typedef struct { char *s; size_t n, cap; } cbuf_t;
void cbuf_free(cbuf_t *b);

/* one mod's files for the file being built (any may be NULL); the mods go strongest first */
typedef struct {
    const char *id;
    const char *legacy;  long legacy_n;     /* resources/<F>: a whole file, as on the PC */
    const char *content; long content_n;    /* content/<F>: new records (entities2 only) */
    const char *patch;   long patch_n;      /* patch/<F>: changes to records */
} cmodf_t;

typedef struct cgame {
    /* the game's own data/<name>, or NULL; the bytes stay valid for the whole build */
    const char *(*file)(void *ctx, const char *name, long *len);
    void *ctx;
    /* the port's own, loader-exact refusal of a whole table (usermods.c: the loaders' fixed
       tables and the bestiary's parent chain), asked after the model's checks; NULL: none.
       game is NULL when buf is a finished build (nothing of the game's is added to it). */
    int (*refuse)(void *ctx, const char *fname, const char *buf, long len, const char *game, long glen,
                  char *why, size_t room);
    /* the other version's (Repentance+'s) table of this name, or NULL (04.10.2026: a mod's whole table made
       for that version is merged three ways - content_sim.py _version); NULL: none */
    const char *(*other)(void *ctx, const char *name, long *len);
} cgame_t;

/*
 * F's bytes for this set of mods into out, the report lines into lines (each
 * "<F>: ...\n", without the "content: " the report adds), the base's id into
 * base ("game" or a mod's id). 0, or -1 when the game has no such file (one
 * line says so, out stays empty). port: the port's fill patch (stringtable.sta)
 * or NULL.
 */
int content_build(const char *fname, const cmodf_t *mods, int n, const cgame_t *game,
                  const char *port, long portn, cbuf_t *out, cbuf_t *lines, char *base, size_t base_room);

/* a file content_build answers for: a data table of the key table, or stringtable.sta */
int content_known(const char *fname);
/* the port's stringtable fill (step 5) */
const char *content_port_stringtable(long *len);

/* ---- the device side (usermods.c calls these) */

/* a mod of this run, in strength order (the strongest first): its top-level data files are noted */
void content_scan_mod(const char *mods_dir, const char *id);
/* every file a mod touches built (or taken from content-cache/); the per-mod notes made */
void content_prepare(const char *cache_dir);
/* repentance-resources/data/<leaf>: the built file's path; NULL with *game=1 when the answer is the
   game's own file; NULL with *game=0 when no mod touches it (the usual lookup goes on) */
const char *content_map(const char *leaf, int *game);
/* a folder of a mod read as files of one kind ('l' whole files, 'c' content/, 'p' patch/) - the hook for
   options/: a chosen option's folders go in BEFORE content_scan_mod (the first whole file and content/ of a name
   wins; patches stack - each layer's patch/ is applied over the mod's own, the first scanned the strongest) */
int content_scan_layer(const char *id, const char *dir, char kind);
/* the settings' layers a mod uses in this run ("" none): part of the key of every table it touches */
void content_mod_layers(const char *id, const char *layers);
/* how many table files of the mod content.c took; whether it brings a file of this table */
int content_mod_files(const char *id);
int content_mod_has(const char *id, const char *leaf);
/* a mod's data-table lines for its <id>.check.txt ("" none), and its counts for the MODS page */
const char *content_check_text(const char *id);
typedef struct {
    int replaced, patched, merged, refused, conflicts, overridden;
    char legacy[64];
    int mended;                             /* ids left out of a whole table's item lists (M1) */
    char mend_who[48], mend_item[24];       /* the first of them: the record ("#ISAAC_NAME", "player[3]") and the id */
    int versioned;                          /* values and records of another version's (Repentance+'s) put back */
    int version_tables;                     /* in how many of the mod's whole tables */
} content_sum_t;
int content_summary(const char *id, content_sum_t *s);

#endif
