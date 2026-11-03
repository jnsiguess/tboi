/*
 * A save file brought from somewhere else.
 *
 * The game builds its save names from a format string and its own prefix -
 * "%spersistentgamedata%d.dat" for Rebirth, "%sab_" for Afterbirth, "%sabp_"
 * for Afterbirth+, "%srep_" for Repentance, and the same four for
 * "gamestate%d.dat", the run in progress. We run as Repentance, so the game
 * asks for `rep_persistentgamedata1.dat` and nothing else.
 *
 * The desktop build names the same files differently, and a player who brings
 * one over finds the game ignoring it - not refusing it, simply never looking.
 * So: when the game opens a save FOR READING and its own name is not there, we
 * look in the same folder for a file with the same tail and open that instead.
 *
 *     rep_persistentgamedata1.dat   asked for, missing
 *     20260905.persistentgamedata1.dat   found, opened in its place
 *
 * Three rules, and the middle one is the whole point of the exercise:
 *
 *   - The game's own name always wins. We are only ever asked when it is not
 *     there, so a folder holding both kinds uses the `rep_` one and the other
 *     is never looked at. That is the safety the owner asked for.
 *   - Only reading is redirected. Writing goes to the game's own name, so the
 *     first save after a foreign file is read puts the progress where the game
 *     expects it, and from then on the question does not arise.
 *   - If several foreign files match, the newest is taken - a player who has
 *     just dropped one in means that one - and every candidate is written to
 *     the log, so a surprise can be explained rather than guessed at.
 */
#define _BSD_SOURCE                         /* funopen, for the mended stream */
#include <dirent.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <android/log.h>
#include "diag.h"

#if ISAAC_DIAG
#define LOG(...) __android_log_print(ANDROID_LOG_INFO, "isaac", __VA_ARGS__)
#else
#define LOG(...) do { } while (0)
#endif

int shim_report_active(void);
void report_line(const char *kind, const char *text);

/*
 * Report lines of this file are for the report build only, and compiled out
 * of the clean one, so build.py can check they are gone. What they describe
 * works in both.
 */
#if ISAAC_DIAG
#define SAY(kind, ...) do { if (shim_report_active()) { char say_[500];                    \
        snprintf(say_, sizeof(say_), __VA_ARGS__); report_line(kind, say_); } } while (0)
#else
#define SAY(kind, ...) do { } while (0)
#endif

/* the two families of save file, by the part of the name they end with */
static const char *const KINDS[] = { "persistentgamedata", "gamestate" };

/*
 * The tail of a save name: everything from the family word to the end, so
 * `rep_persistentgamedata1.dat` gives `persistentgamedata1.dat`. A name with
 * no family word in it is not a save and answers nothing.
 */
static const char *save_tail(const char *name) {
    for (unsigned i = 0; i < sizeof(KINDS) / sizeof(KINDS[0]); i++) {
        const char *at = strstr(name, KINDS[i]);
        if (at) return at;
    }
    return NULL;
}

static int reading_only(const char *mode) {
    return mode && mode[0] == 'r' && !strchr(mode, '+');
}

/*
 * Answers the path to open instead, or nothing at all. The answer lives in a
 * static buffer: it is used immediately by the caller and never kept.
 */
const char *shim_save_alias(const char *path, const char *mode) {
    static char answer[1024];
    if (!path || !reading_only(mode)) return NULL;

    const char *slash = strrchr(path, '/');
    const char *name = slash ? slash + 1 : path;
    const char *tail = save_tail(name);
    if (!tail) return NULL;
    size_t tail_len = strlen(tail);
    /*
     * ONLY THE PC'S NAMES, and only for Repentance's own question (P6.5). The
     * game also asks for its older editions' files - ab_, abp_ and the bare
     * Rebirth name - to carry them over, and a request for abp_... used to be
     * answered with rep_...: a Repentance save handed to the Afterbirth+
     * reader. So the question must be Repentance's, and the answer may not be
     * any edition's own file.
     */
    if (strncmp(name, "rep_", 4) != 0) return NULL;

    size_t dir_len = slash ? (size_t) (slash - path) : 1;
    char dir[1024];
    if (dir_len + 1 >= sizeof(dir)) return NULL;
    if (slash) { memcpy(dir, path, dir_len); dir[dir_len] = 0; }
    else { strcpy(dir, "."); dir_len = 1; }

    DIR *d = opendir(dir);
    if (!d) return NULL;

    char best[256] = {0};
    long best_time = -1;
    int seen = 0;
    struct dirent *e;
    while ((e = readdir(d)) != NULL) {
        size_t len = strlen(e->d_name);
        if (len < tail_len || strcmp(e->d_name + len - tail_len, tail) != 0) continue;
        if (strcmp(e->d_name, name) == 0) continue;      /* the one that is not there */
        if (!strncmp(e->d_name, "rep_", 4) || !strncmp(e->d_name, "ab_", 3)
            || !strncmp(e->d_name, "abp_", 4)) continue;  /* an edition's own file, not the PC's */
        char full[1024];
        if ((size_t) snprintf(full, sizeof(full), "%s/%s", dir, e->d_name) >= sizeof(full)) continue;
        struct stat st;
        if (stat(full, &st) != 0 || !S_ISREG(st.st_mode)) continue;
        seen++;
        LOG("saves: '%s' would do for '%s'", e->d_name, name);
        if ((long) st.st_mtime > best_time && len < sizeof(best)) {
            best_time = (long) st.st_mtime;
            strcpy(best, e->d_name);
        }
    }
    closedir(d);
    if (!seen || !best[0]) return NULL;

    if ((size_t) snprintf(answer, sizeof(answer), "%s/%s", dir, best) >= sizeof(answer))
        return NULL;
    LOG("saves: '%s' is not there — reading '%s' instead (%d of that kind found)",
        name, best, seen);
    return answer;
}

/*
 * A save handed to the game as the game itself reads it.
 *
 * The measure is the game's own reader (0x6ff7f0), not the writer's layout
 * (the research notes of 02.10.2026, save crash 2; work/saves/notes/format.md,
 * section 4).
 * What it does - game_read below follows it word for word:
 *
 *   - the header must be "ISAACNGSAVE09R  " and the checksum of [0x10, len-4)
 *     must match the last four bytes; otherwise it answers 0, and the loader
 *     (0x6fec18) resets the slot and writes the empty file over it at once;
 *   - chunks are taken as they come, by their type: type, size, and for a
 *     non-zero size the count; then min(count, its own count) elements, and
 *     no more than the size field allows (a byte per unit of size, a word per
 *     four). NOTHING IS SKIPPED: what a count leaves unread is read as the
 *     next type, and an unknown type is passed over without a byte consumed;
 *   - a read past the end of the file takes nothing and leaves the variable
 *     as it was; the loop ends at the end of the file, at a negative type or
 *     at a type of 11 or more, and then the save counter is read;
 *   - the bestiary's list count and list lengths are bounded by nothing.
 *
 * So the game takes much more than a strict reading of the layout would: a
 * count with a stray top byte (the save sent on 02.10.2026), a count past the
 * end of the file under a sane size field, a file cut short - it loads what is
 * there and keeps the rest at zero. Such a file goes to the game as it is: a
 * file the game reads is never refused and never set aside.
 *
 * On top of that the port does two things, both to keep progress:
 *
 *   - a file in the writer's own layout, read to the byte (every chunk where
 *     its count puts it, the bestiary to the byte, eight bytes after it), is
 *     rebuilt at this build's counts and its checksum put right: a chunk
 *     longer than ours (Repentance+ online's 642 achievements) would slip the
 *     game's stream and lose everything after it, and a wrong checksum would
 *     reset the slot (layout_mend);
 *   - the one file the game cannot get through: a bestiary list, or the list
 *     count, running past the end of the file. Past the end every turn of
 *     those loops inserts the same pair again, ~120 instructions and a map
 *     lookup each, up to 2^29 per list and 2^32 lists - the frame thread
 *     frozen for seconds or for good. Such a file gets the backup, or with no
 *     sound backup the state the game would have when the loops were done,
 *     rebuilt (MEND_HANGS).
 *
 * The file on disk is never touched here; the game's next save writes what it
 * read under its own name.
 */
#define SAVE_CAP (2u << 20)                 /* the game's own read buffer */
static const char SAVE_MAGIC[] = "ISAACNGSAVE09R";
static const char SAVE_HEADER[16] = "ISAACNGSAVE09R  ";

/* chunks 1..10: element size, the count this build reads, where the reader puts them */
static const struct { uint32_t elem, count, at; } CHUNKS[10] = {
    {1, 638, 0x28}, {4, 496, 0x2a8}, {4, 14, 0xd48}, {1, 733, 0xa68}, {1, 7, 0xd80},
    {1, 104, 0xd87}, {1, 46, 0xdef}, {4, 27, 0xe20}, {4, 2, 0xe8c}, {1, 80, 0xe94},
};
#define BLOCK_LINK    0xf48                 /* the run save's link, offset 0x10 of the file */
#define BLOCK_COUNTER 0xf4c                 /* the save counter, read last */
#define BLOCK_BYTES   0xf58

static uint32_t rd32(const uint8_t *p) {
    return (uint32_t) p[0] | (uint32_t) p[1] << 8 | (uint32_t) p[2] << 16 | (uint32_t) p[3] << 24;
}

static void wr32(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t) v; p[1] = (uint8_t) (v >> 8); p[2] = (uint8_t) (v >> 16); p[3] = (uint8_t) (v >> 24);
}

/*
 * The game's checksum (0x7f3724): CRC-32 with the reflected polynomial, but
 * its table is built with an ARITHMETIC shift, so a set top bit stays set.
 * The register starts from 0xFEDCBA76 inverted and is inverted at the end.
 */
static uint32_t save_crc(const uint8_t *p, size_t n) {
    static uint32_t table[256];
    static int built;
    /* the writer thread checks saves too (safefile.c): the table is published whole, never half-built */
    if (!__atomic_load_n(&built, __ATOMIC_ACQUIRE)) {
        for (uint32_t i = 0; i < 256; i++) {
            uint32_t c = i;
            for (int k = 0; k < 8; k++)
                c = (uint32_t) ((int32_t) c >> 1) ^ ((c & 1) ? 0xEDB88320u : 0);
            table[i] = c;
        }
        __atomic_store_n(&built, 1, __ATOMIC_RELEASE);
    }
    uint32_t c = ~0xFEDCBA76u;
    for (size_t i = 0; i < n; i++) c = table[(c ^ p[i]) & 0xff] ^ (c >> 8);
    return ~c;
}

/*
 * An LZ4 block, strictly: any step out of bounds is a refusal, like the
 * library's safe decoder. A plain save always refuses - its first token asks
 * for a match 18254 bytes back from a four-byte output.
 */
static uint8_t *lz4_unblock(const uint8_t *src, size_t n, size_t *out_len) {
    uint8_t *out = malloc(SAVE_CAP);
    if (!out) return NULL;
    size_t i = 0, o = 0;
    for (;;) {
        if (i >= n) goto refuse;
        uint8_t token = src[i++];
        size_t lit = token >> 4;
        if (lit == 15) for (;;) {
            if (i >= n) goto refuse;
            uint8_t b = src[i++]; lit += b;
            if (b != 255) break;
        }
        if (lit > n - i || lit > SAVE_CAP - o) goto refuse;
        memcpy(out + o, src + i, lit); i += lit; o += lit;
        if (i == n) break;                          /* the last run is literals only */
        if (n - i < 2) goto refuse;
        size_t back = (size_t) src[i] | (size_t) src[i + 1] << 8; i += 2;
        if (back == 0 || back > o) goto refuse;
        size_t len = token & 15;
        if (len == 15) for (;;) {
            if (i >= n) goto refuse;
            uint8_t b = src[i++]; len += b;
            if (b != 255) break;
        }
        len += 4;
        if (len > SAVE_CAP - o) goto refuse;
        for (size_t k = 0; k < len; k++, o++) out[o] = out[o - back];
    }
    *out_len = o;
    return out;
refuse:
    free(out);
    return NULL;
}

/* ------------------------------------------------------ the writer's layout */

/*
 * The file walked as its writer laid it out: 1 when every chunk 1..10 sits
 * where the counts before it put it, the bestiary's lists end exactly eight
 * bytes (counter, checksum) before the end of the file. Nothing is changed.
 */
static int layout_clean(const uint8_t *d, size_t n) {
    if (n < 0x20 || memcmp(d, SAVE_HEADER, sizeof(SAVE_HEADER)) != 0) return 0;
    size_t in = 0x14;
    for (uint32_t t = 0; t < 10; t++) {
        if (n - in < 12 || rd32(d + in) != t + 1) return 0;
        uint64_t bytes = (uint64_t) rd32(d + in + 8) * CHUNKS[t].elem;
        if (bytes > n - in - 12) return 0;
        in += 12 + (size_t) bytes;
    }
    if (n - in < 12 || rd32(d + in) != 11) return 0;
    uint32_t lists = rd32(d + in + 8);
    in += 12;
    for (uint32_t k = 0; k < lists; k++) {
        if (n - in < 8) return 0;
        uint32_t sub_size = rd32(d + in + 4);
        if (sub_size % 4 || (uint64_t) sub_size * 2 > n - in - 8) return 0;
        in += 8 + (size_t) sub_size * 2;
    }
    return n - in == 8;
}

/*
 * A file that layout_clean passed, rewritten in place at this build's counts
 * (a longer chunk loses its surplus, the size fields follow the writer's
 * convention: the achievements store the count, the rest count*4) and with
 * the checksum computed anew. Answers 1 if the bytes changed, 0 if not.
 */
static int layout_mend(uint8_t *d, size_t *len, const char *name) {
    (void) name;
    size_t n = *len, in = 0x14, out = 0x14;
    int changed = 0;
    for (uint32_t t = 0; t < 10; t++) {
        uint32_t count = rd32(d + in + 8);
        uint32_t keep = count < CHUNKS[t].count ? count : CHUNKS[t].count;
        if (keep != count) {
            LOG("saves: %s - chunk %u holds %u elements, this build reads %u; the rest is dropped",
                name, t + 1, count, CHUNKS[t].count);
            changed = 1;
        }
        uint32_t size = t == 0 ? keep : keep * 4;
        if (rd32(d + in + 4) != size) changed = 1;
        wr32(d + out, t + 1);
        wr32(d + out + 4, size);
        wr32(d + out + 8, keep);
        memmove(d + out + 12, d + in + 12, (size_t) keep * CHUNKS[t].elem);
        in += 12 + (size_t) count * CHUNKS[t].elem;
        out += 12 + (size_t) keep * CHUNKS[t].elem;
    }
    size_t tail = n - in;                       /* bestiary, counter, checksum */
    memmove(d + out, d + in, tail);
    out += tail;
    uint32_t stored = rd32(d + out - 4), sum = save_crc(d + 0x10, out - 0x14);
    if (stored != sum) {
        LOG("saves: %s - checksum %08x where %08x is due", name, stored, sum);
        wr32(d + out - 4, sum);
        changed = 1;
    }
    *len = out;
    return changed;
}

/* ------------------------------------------------------- the game's reader */

typedef struct { uint32_t key, value, seq; } pair_t;

typedef struct {
    uint8_t blk[BLOCK_BYTES];               /* the PersistentGameData block as the reader leaves it */
    pair_t *list[4];                        /* the bestiary's four maps, by list id 1..4 */
    size_t n[4], cap[4];
    uint32_t seq;
    int oom;                                /* a map could not grow: the pairs are not all there */
    int layout;                             /* save_judge: the file was in the writer's layout */
    const char *why;                        /* GAME_REJECTS: what the reader stopped at */
    uint32_t odd_type, odd_count, odd_read; /* the first chunk whose count is not this build's */
    size_t stop, end;                       /* where the chunk loop ended, and the file's length */
    uint64_t past_end;                      /* bestiary turns the reader makes with the file used up */
} game_read_t;

enum { GAME_REJECTS, GAME_READS, GAME_HANGS };

/*
 * Past the end of the file the bestiary loops go on, each turn the same: at
 * ~120 instructions and a map lookup a turn, 2^24 turns hold the frame thread
 * for seconds, more for longer. Anything a real file can make it do with data
 * in hand stays below 2^19 (the 2 MB buffer, 4 bytes a turn at the least).
 */
#define HANG_TURNS (1u << 24)

typedef struct { const uint8_t *d; size_t n, at; } game_stream_t;

/* the game's stream (0x8d7ea4): what is left, at most k bytes; the rest of `to` stays as it was */
static void game_take(game_stream_t *s, uint8_t *to, size_t k) {
    size_t take = s->n - s->at < k ? s->n - s->at : k;
    memcpy(to, s->d + s->at, take);
    s->at += take;
}

/* the map's emplace (0x495d60) and the store after it: a key met again takes the newer value */
static void pair_put(game_read_t *g, uint32_t id, uint32_t key, uint32_t value) {
    size_t i = id - 1;
    if (g->n[i] == g->cap[i]) {
        size_t cap = g->cap[i] ? g->cap[i] * 2 : 64;
        pair_t *more = realloc(g->list[i], cap * sizeof(pair_t));
        if (!more) { g->oom = 1; return; }
        g->list[i] = more;
        g->cap[i] = cap;
    }
    g->list[i][g->n[i]++] = (pair_t) { key, value, g->seq++ };
}

static int pair_order(const void *a, const void *b) {
    const pair_t *x = a, *y = b;
    if (x->key != y->key) return (int32_t) x->key < (int32_t) y->key ? -1 : 1;
    return x->seq < y->seq ? -1 : x->seq > y->seq;
}

/* the maps as the game holds them: one value per key (the last put), keys in order */
static void pairs_settle(game_read_t *g) {
    for (int i = 0; i < 4; i++) {
        if (!g->n[i]) continue;
        qsort(g->list[i], g->n[i], sizeof(pair_t), pair_order);
        size_t w = 0;
        for (size_t k = 0; k < g->n[i]; k++)
            if (k + 1 == g->n[i] || g->list[i][k + 1].key != g->list[i][k].key) g->list[i][w++] = g->list[i][k];
        g->n[i] = w;
    }
}

static void game_read_free(game_read_t *g) {
    for (int i = 0; i < 4; i++) { free(g->list[i]); g->list[i] = NULL; g->n[i] = g->cap[i] = 0; }
}

/*
 * The bestiary (0x700294). The list count and every list length are taken as
 * they stand; the stack variables keep what they last held when a read finds
 * nothing. Once the file is used up every turn reads nothing and puts the
 * same pair again - so the rest is counted, not run, and the pair put once.
 */
static void game_bestiary(game_stream_t *s, game_read_t *g, uint8_t *lists_at, uint8_t *id_at,
                          uint8_t *size_at, uint8_t *key_at, uint8_t *value_at) {
    for (int i = 0; i < 4; i++) g->n[i] = 0;
    game_take(s, lists_at, 4);
    uint32_t lists = rd32(lists_at);
    for (uint32_t j = 0; j < lists; j++) {
        if (s->at >= s->n) {
            uint32_t id = rd32(id_at);
            int32_t size = (int32_t) rd32(size_at);
            int fills = id - 1 < 4 && size >= 4;
            g->past_end += (uint64_t) (lists - j) * (1 + (fills ? (uint32_t) size >> 2 : 0));
            if (fills) pair_put(g, id, rd32(key_at), rd32(value_at));
            return;
        }
        game_take(s, id_at, 4);
        uint32_t id = rd32(id_at);
        if (id - 1 >= 4) continue;                       /* not a list: passed over, nothing more read */
        game_take(s, size_at, 4);
        int32_t size = (int32_t) rd32(size_at);
        if (size < 4) continue;
        uint32_t entries = (uint32_t) size >> 2;
        for (uint32_t e = 0; e < entries; e++) {
            if (s->at >= s->n) {
                g->past_end += entries - e;
                pair_put(g, id, rd32(key_at), rd32(value_at));
                break;
            }
            game_take(s, key_at, 4);
            game_take(s, value_at, 4);
            pair_put(g, id, rd32(key_at), rd32(value_at));
        }
    }
}

/*
 * The game's reader (0x6ff7f0) over a plain file: what it leaves in the block
 * and the four maps. GAME_REJECTS when it answers 0 (g->why), GAME_HANGS when
 * the bestiary would hold it past HANG_TURNS (the state is still the one it
 * would end with), GAME_READS otherwise. The stack variables the game does
 * not clear start from zero here; only a file whose very first element lies
 * past its end could tell.
 */
static int game_read(const uint8_t *d, size_t n, game_read_t *g) {
    memset(g, 0, sizeof(*g));
    if (n > SAVE_CAP) n = SAVE_CAP;                      /* the game's buffer takes no more */
    g->end = n;
    if (n < sizeof(SAVE_HEADER) || memcmp(d, SAVE_HEADER, sizeof(SAVE_HEADER)) != 0) {
        g->why = n >= 11 && !memcmp(d, "ISAACNGSAVE", 11) ? "another edition's save" : "not a save";
        return GAME_REJECTS;
    }
    if (n < 0x14) { g->why = "cut short"; return GAME_REJECTS; }
    if (rd32(d + n - 4) != save_crc(d + 0x10, n - 0x14)) { g->why = "the checksum does not match"; return GAME_REJECTS; }
    game_stream_t s = { d, n, 0x10 };
    uint8_t type[4] = {0}, size[4], count[4];
    uint8_t v58[4] = {0}, v54[4] = {0}, v50[4] = {0}, v4c[4] = {0}, v48[4] = {0};  /* [sp, #0x58] .. */
    game_take(&s, g->blk + BLOCK_LINK, 4);
    while (s.at < s.n) {
        game_take(&s, type, 4);                          /* not cleared before: a short read keeps the old type */
        memset(size, 0, sizeof(size));
        game_take(&s, size, 4);
        uint32_t t = rd32(type), sz = rd32(size);
        if (sz && t - 1 < 10) {
            const uint32_t elem = CHUNKS[t - 1].elem, want = CHUNKS[t - 1].count, at = CHUNKS[t - 1].at;
            memset(count, 0, sizeof(count));
            game_take(&s, count, 4);
            uint32_t c = rd32(count), m = c < want ? c : want;
            uint64_t k = 0;
            if (m) {
                if (elem == 1) {
                    do {                                 /* (int8) byte > 0, while k < count and k < size */
                        game_take(&s, v58, 1);
                        g->blk[at + k] = (int8_t) v58[0] > 0;
                        k++;
                    } while (k < m && k < sz);
                } else {
                    do {                                 /* a word, while k < count and 4k < size */
                        game_take(&s, v58, 4);
                        memcpy(g->blk + at + 4 * k, v58, 4);
                        k++;
                    } while (k < m && 4 * k < sz);
                }
            }
            if (c != want && !g->odd_type) { g->odd_type = t; g->odd_count = c; g->odd_read = (uint32_t) k; }
        } else if (sz && t == 11) {
            game_bestiary(&s, g, v58, v54, v50, v4c, v48);
        }
        if (s.at >= s.n || (int32_t) t < 0 || (int32_t) t >= 11) break;
    }
    g->stop = s.at;
    game_take(&s, g->blk + BLOCK_COUNTER, 4);
    pairs_settle(g);
    return g->past_end > HANG_TURNS ? GAME_HANGS : GAME_READS;
}

/*
 * What the reader left, written out as the game writes it (0x700d14): every
 * chunk at this build's count, the four lists in the order 4, 2, 3, 1, the
 * counter and the checksum. Reads back into the very same state.
 */
static uint8_t *state_build(const game_read_t *g, size_t *out_len) {
    size_t pairs = g->n[0] + g->n[1] + g->n[2] + g->n[3];
    size_t n = 0x14 + 12 + 4 * 8 + 8 * pairs + 8;
    for (int t = 0; t < 10; t++) n += 12 + (size_t) CHUNKS[t].count * CHUNKS[t].elem;
    uint8_t *d = malloc(n);
    if (!d) return NULL;
    memcpy(d, SAVE_HEADER, sizeof(SAVE_HEADER));
    memcpy(d + 0x10, g->blk + BLOCK_LINK, 4);
    size_t o = 0x14;
    for (uint32_t t = 0; t < 10; t++) {
        uint32_t c = CHUNKS[t].count, bytes = c * CHUNKS[t].elem;
        wr32(d + o, t + 1);
        wr32(d + o + 4, t == 0 ? c : c * 4);
        wr32(d + o + 8, c);
        memcpy(d + o + 12, g->blk + CHUNKS[t].at, bytes);
        o += 12 + bytes;
    }
    wr32(d + o, 11);
    wr32(d + o + 4, (uint32_t) (4 * pairs));
    wr32(d + o + 8, 4);
    o += 12;
    static const int ORDER[4] = {4, 2, 3, 1};
    for (int k = 0; k < 4; k++) {
        int i = ORDER[k] - 1;
        wr32(d + o, (uint32_t) ORDER[k]);
        wr32(d + o + 4, (uint32_t) (4 * g->n[i]));
        o += 8;
        for (size_t p = 0; p < g->n[i]; p++, o += 8) {
            wr32(d + o, g->list[i][p].key);
            wr32(d + o + 4, g->list[i][p].value);
        }
    }
    memcpy(d + o, g->blk + BLOCK_COUNTER, 4);
    o += 4;
    wr32(d + o, save_crc(d + 0x10, o - 0x10));
    *out_len = o + 4;
    return d;
}

/*
 * A plain save judged: mended in place when it is in the writer's layout,
 * then read the way the game reads it (into g, freed by the caller with
 * game_read_free). Answers 1 if the bytes were mended, 0 if they go to the
 * game as they are, MEND_REFUSED if the game does not read them (g->why),
 * MEND_HANGS if its bestiary would hold the game past the end of the file.
 */
#define MEND_REFUSED (-1)
#define MEND_HANGS   (-2)

static const char HANGS_WHY[] = "the bestiary runs the game's reader past the end of the file";

static int save_judge(uint8_t *d, size_t *len, const char *name, game_read_t *g) {
    if (*len > SAVE_CAP) *len = SAVE_CAP;               /* all the game's buffer takes */
    int layout = layout_clean(d, *len);
    int changed = layout ? layout_mend(d, len, name) : 0;
    int r = game_read(d, *len, g);
    g->layout = layout;
    if (r == GAME_REJECTS) return MEND_REFUSED;
    if (r == GAME_HANGS) return MEND_HANGS;
    return changed;
}

/* a stream over the mended bytes, which frees them when the game closes it */
typedef struct { uint8_t *data; size_t len, at; } mem_file_t;

static int mem_read(void *cookie, char *buf, int n) {
    mem_file_t *m = cookie;
    size_t left = m->len - m->at, take = n < 0 ? 0 : (size_t) n;
    if (take > left) take = left;
    memcpy(buf, m->data + m->at, take);
    m->at += take;
    return (int) take;
}

static fpos_t mem_seek(void *cookie, fpos_t off, int whence) {
    mem_file_t *m = cookie;
    long long to = whence == SEEK_SET ? (long long) off
                 : whence == SEEK_CUR ? (long long) m->at + off
                 : (long long) m->len + off;
    if (to < 0 || to > (long long) m->len) return -1;
    m->at = (size_t) to;
    return (fpos_t) to;
}

static int mem_close(void *cookie) {
    mem_file_t *m = cookie;
    free(m->data);
    free(m);
    return 0;
}

/* ====================================================================== */
/*
 * THE PORT'S OWN BACKUP OF THE SAVES (P6, the owner, 30.09.2026: "one live
 * copy in Android/media, and a silent backup inside the app").
 *
 * The live files are the game's, in the port's folder in Android/media. Every
 * time one of them is written whole AND reads back sound, a copy goes into the
 * app's internal storage - files/save-backup/<name>/1.bin, 2.bin, 3.bin, the
 * newest first - where no file manager, cleaner or MediaProvider refusal can
 * reach it. When a live file is missing, empty or unreadable, the newest sound
 * copy is what the game gets:
 *
 *   - at start, before the game reads anything (shim_saves_startup), it is
 *     written back into place;
 *   - while the game runs, a progress file that does not read (or is not
 *     there) is handed over from the backup in memory (shim_save_mended,
 *     shim_save_missing), and the game's next save puts it on the disk.
 *
 * Guarded: rep_persistentgamedata1..3.dat (read with the game's own layout),
 * options.ini (it must end with Version=2), inputconfigs.dat (not empty) and
 * the spawner's run marks, saves/port-spawner-runs.txt (spawner.c): a mark
 * lost would let a run that took from the spawner unlock things.
 * The run in progress (rep_gamestateN) is NOT restored: the game empties it on
 * purpose when a run ends, and bringing it back would bring a dead run back.
 *
 * Two more rules, because a reset that goes through silently is the worst
 * loss there is:
 *   - if the game writes a slot that did not read this launch and nothing was
 *     handed over in its place (the game has reset it), the file on the disk
 *     is kept first, as keep-before-reset-<time>.bin;
 *   - if a slot's newest sound copy had more achievements than the one now
 *     written, the older one is kept aside the same way before it is rotated
 *     out. Deleting a slot in the game's menu lands there too - harmlessly.
 *
 * Kept copies: at most six per file, the oldest go first.
 */
#include <jni.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <time.h>
#include <unistd.h>

const char *shim_saves_dir(void);
unsigned long shim_image_base(void);
int shim_safe_write_file(const char *path, const void *data, size_t n);

static char g_backup[512];                  /* <files>/save-backup */
static char g_guard[512];                   /* <files>/port-guard (report builds) */
static char g_files[512];                   /* <files>: the seats beside a run (coop.c) */
static pthread_mutex_t g_bk_lock = PTHREAD_MUTEX_INITIALIZER;

enum { K_NONE, K_SAVE, K_OPTIONS, K_INPUT, K_MARKS };

static const char *leaf_name(const char *p) {
    const char *s = p ? strrchr(p, '/') : NULL;
    return s ? s + 1 : (p ? p : "");
}

/* rep_persistentgamedataN.dat -> N, anything else 0 */
static int rep_slot(const char *leaf) {
    static const char head[] = "rep_persistentgamedata";
    if (strncmp(leaf, head, sizeof(head) - 1)) return 0;
    const char *d = leaf + sizeof(head) - 1;
    if (d[0] < '1' || d[0] > '3' || strcmp(d + 1, ".dat")) return 0;
    return d[0] - '0';
}

static int kind_of(const char *leaf) {
    if (rep_slot(leaf)) return K_SAVE;
    if (!strcmp(leaf, "options.ini")) return K_OPTIONS;
    if (!strcmp(leaf, "inputconfigs.dat")) return K_INPUT;
    if (!strcmp(leaf, "port-spawner-runs.txt")) return K_MARKS;     /* spawner.c, the run marks */
    return K_NONE;
}

static int in_saves_dir(const char *p) {
    const char *s = shim_saves_dir();
    size_t n = s ? strlen(s) : 0;
    return p && n && !strncmp(p, s, n) && p[n] == '/';
}

/* the whole of a file into memory; NULL with errno, and 0 bytes for an empty file */
static uint8_t *read_whole(const char *path, size_t *n) {
    *n = 0;
    int fd = open(path, O_RDONLY | O_CLOEXEC);
    if (fd < 0) return NULL;
    size_t room = 1 << 16, have = 0;
    uint8_t *buf = malloc(room);
    while (buf) {
        if (have == room) {
            if (room >= SAVE_CAP * 2) { free(buf); buf = NULL; errno = EFBIG; break; }
            uint8_t *more = realloc(buf, room * 2);
            if (!more) { free(buf); buf = NULL; break; }
            buf = more;
            room *= 2;
        }
        ssize_t k = read(fd, buf + have, room - have);
        if (k < 0) { if (errno == EINTR) continue; int e = errno; free(buf); buf = NULL; errno = e; break; }
        if (k == 0) break;
        have += (size_t) k;
    }
    int e = errno;
    close(fd);
    errno = e;
    if (buf) *n = have;
    return buf;
}

/* inside the app: tmp, flushed, renamed - no MediaProvider here, rename is sure */
static int write_internal(const char *path, const void *data, size_t n) {
    char tmp[600];
    snprintf(tmp, sizeof(tmp), "%s.tmp", path);
    int fd = open(tmp, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0600);
    if (fd < 0) return -1;
    const uint8_t *c = data;
    size_t left = n;
    int bad = 0;
    while (left && !bad) {
        ssize_t k = write(fd, c, left);
        if (k < 0 && errno == EINTR) continue;
        if (k <= 0) bad = 1; else { c += k; left -= (size_t) k; }
    }
    if (!bad && fsync(fd) != 0) bad = 1;
    if (close(fd) != 0) bad = 1;
    if (!bad && rename(tmp, path) != 0) bad = 1;
    if (bad) unlink(tmp);
    return bad ? -1 : 0;
}

static int find_bytes(const uint8_t *hay, size_t n, const char *needle) {
    size_t k = strlen(needle);
    for (size_t i = 0; k && i + k <= n; i++) if (!memcmp(hay + i, needle, k)) return 1;
    return 0;
}

/*
 * What is wrong with this content, or NULL when it is sound. For a save the
 * achievements it holds come back too (-1 when unknown), and three of them
 * by number for the report's rollback watch.
 */
static int g_ach_watch[3];

/*
 * A save is sound when the game reads it (save_judge): as it is, or mended.
 * The achievements are counted in what the game's reader leaves, as the game
 * would see them.
 */
static const char *content_problem(int kind, const uint8_t *d, size_t n, int *achievements) {
    if (achievements) *achievements = -1;
    if (n == 0) return "empty";
    if (kind == K_OPTIONS) return find_bytes(d, n, "Version=2") ? NULL : "no Version=2 line";
    if (kind == K_INPUT) return NULL;
    if (kind == K_MARKS) return d[0] == '#' || (d[0] >= '0' && d[0] <= '9') ? NULL : "not the run marks";
    if (kind != K_SAVE) return NULL;
    if (n < 0x14) return "cut short";
    uint8_t *plain;
    size_t len = n;
    if (memcmp(d, SAVE_MAGIC, sizeof(SAVE_MAGIC) - 1) == 0) {
        plain = malloc(n);
        if (!plain) return NULL;                        /* no memory is no verdict */
        memcpy(plain, d, n);
    } else {
        plain = lz4_unblock(d, n, &len);
        if (!plain) return !memcmp(d, "ISAACNGSAVE", 11) ? "another edition's save" : "not a save";
    }
    game_read_t g;
    int verdict = save_judge(plain, &len, "check", &g);
    const char *why = verdict == MEND_REFUSED ? g.why : verdict == MEND_HANGS ? HANGS_WHY : NULL;
    if (!why && achievements) {
        const uint8_t *ach = g.blk + CHUNKS[0].at;
        int got = 0;
        for (uint32_t i = 1; i < CHUNKS[0].count; i++) got += ach[i] != 0;
        *achievements = got;
        static const int watch[3] = {115, 249, 341};
        for (int w = 0; w < 3; w++) g_ach_watch[w] = ach[watch[w]];
    }
    game_read_free(&g);
    free(plain);
    return why;
}

static void stamp(char *out, size_t room, time_t t) {
    struct tm tm;
    localtime_r(&t, &tm);
    strftime(out, room, "%Y%m%d-%H%M%S", &tm);
}

static void when(char *out, size_t room, time_t t) {
    struct tm tm;
    localtime_r(&t, &tm);
    strftime(out, room, "%d.%m %H:%M:%S", &tm);
}

static int leaf_dir(const char *leaf, char *out, size_t room) {
    if (!g_backup[0]) return -1;
    mkdir(g_backup, 0700);
    snprintf(out, room, "%s/%s", g_backup, leaf);
    mkdir(out, 0700);
    return 0;
}

/* a copy set aside for good (well, the six newest): keep-<tag>-<time>.bin */
static void keep_copy(const char *leaf, const char *tag, const uint8_t *d, size_t n) {
    char dir[600], path[700], st[32];
    if (!n || leaf_dir(leaf, dir, sizeof(dir))) return;
    stamp(st, sizeof(st), time(NULL));
    snprintf(path, sizeof(path), "%s/keep-%s-%s.bin", dir, tag, st);
    write_internal(path, d, n);
    /* six at most: the oldest by time goes */
    for (;;) {
        DIR *dd = opendir(dir);
        if (!dd) break;
        int count = 0;
        time_t oldest = 0;
        char victim[300] = "";
        struct dirent *e;
        while ((e = readdir(dd)) != NULL) {
            if (strncmp(e->d_name, "keep-", 5)) continue;
            char full[700];
            struct stat s;
            snprintf(full, sizeof(full), "%s/%s", dir, e->d_name);
            if (stat(full, &s) != 0) continue;
            count++;
            if (!victim[0] || s.st_mtime < oldest) { oldest = s.st_mtime; snprintf(victim, sizeof(victim), "%s", e->d_name); }
        }
        closedir(dd);
        if (count <= 6 || !victim[0]) break;
        snprintf(path, sizeof(path), "%s/%s", dir, victim);
        unlink(path);
    }
    SAY("saves", "%s: a copy kept aside in the backup as keep-%s-%s (%zu bytes)", leaf, tag, st, n);
}

/*
 * The newest sound copy of a file: its bytes, their time and which of the
 * three it was (1 newest). NULL when none of them reads.
 */
static uint8_t *backup_newest(const char *leaf, int kind, size_t *n, time_t *t, int *gen) {
    char dir[600];
    if (leaf_dir(leaf, dir, sizeof(dir))) return NULL;
    for (int g = 1; g <= 3; g++) {
        char path[700];
        struct stat s;
        snprintf(path, sizeof(path), "%s/%d.bin", dir, g);
        if (stat(path, &s) != 0) continue;
        size_t len;
        uint8_t *d = read_whole(path, &len);
        if (!d) continue;
        if (!content_problem(kind, d, len, NULL)) {
            *n = len; *t = s.st_mtime; *gen = g;
            return d;
        }
        free(d);
    }
    return NULL;
}

/* a sound copy in as the newest; the same bytes twice are not a new generation */
static void backup_push(const char *leaf, int kind, const uint8_t *d, size_t n) {
    char dir[600], g1[700], g2[700], g3[700];
    pthread_mutex_lock(&g_bk_lock);
    if (leaf_dir(leaf, dir, sizeof(dir))) { pthread_mutex_unlock(&g_bk_lock); return; }
    snprintf(g1, sizeof(g1), "%s/1.bin", dir);
    snprintf(g2, sizeof(g2), "%s/2.bin", dir);
    snprintf(g3, sizeof(g3), "%s/3.bin", dir);
    size_t on = 0;
    uint8_t *old = read_whole(g1, &on);
    if (old && on == n && !memcmp(old, d, n)) { free(old); pthread_mutex_unlock(&g_bk_lock); return; }
    if (old && kind == K_SAVE) {
        int was = -1, now = -1;
        if (!content_problem(K_SAVE, old, on, &was) && !content_problem(K_SAVE, d, n, &now)
            && was > 0 && now >= 0 && now < was) {
            keep_copy(leaf, "before-reset", old, on);
            SAY("saves", "%s: the achievements went DOWN, %d -> %d; the earlier save is kept aside", leaf, was, now);
        }
    }
    free(old);
    rename(g2, g3);
    rename(g1, g2);
    if (write_internal(g1, d, n) != 0)
        SAY("saves", "%s: the backup could NOT be written (errno %d)", leaf, errno);
    pthread_mutex_unlock(&g_bk_lock);
}

/* ---------------------------------------------------------------- the guard */
/*
 * THE SETTINGS GUARD (P1.6, report builds only). It mends nothing - it names
 * whoever is left writing the settings. Every write of options.ini,
 * port-touch.ini, mods.txt and inputconfigs.dat gives a line: who wrote it and
 * which keys changed. A copy of what was written is kept in files/port-guard,
 * and a file that differs from its copy at the next start was changed by
 * somebody else: `CHANGED OUTSIDE the game`.
 */
#if ISAAC_DIAG
static int guarded_leaf(const char *leaf) {
    return !strcmp(leaf, "options.ini") || !strcmp(leaf, "port-touch.ini")
        || !strcmp(leaf, "mods.txt") || !strcmp(leaf, "inputconfigs.dat");
}

/* the value of `key` in ini text (sections as "[s]key"), into out; 0 when absent */
static int ini_value(const char *text, size_t n, const char *key, char *out, size_t room) {
    char section[64] = "";
    size_t i = 0;
    while (i < n) {
        size_t e = i;
        while (e < n && text[e] != '\n') e++;
        size_t a = i, b = e;
        while (a < b && (text[a] == ' ' || text[a] == '\t')) a++;
        while (b > a && (text[b - 1] == '\r' || text[b - 1] == ' ')) b--;
        if (a < b && text[a] == '[') {
            size_t c = a + 1;
            while (c < b && text[c] != ']') c++;
            snprintf(section, sizeof(section), "%.*s", (int) (c - a - 1), text + a + 1);
        } else if (a < b && text[a] != '#') {
            size_t q = a;
            while (q < b && text[q] != '=') q++;
            if (q < b) {
                char k[128];
                size_t kl = q;
                while (kl > a && (text[kl - 1] == ' ' || text[kl - 1] == '\t')) kl--;
                snprintf(k, sizeof(k), "%s%s%s%.*s", section[0] ? "[" : "", section, section[0] ? "]" : "",
                         (int) (kl - a), text + a);
                if (!strcmp(k, key)) {
                    snprintf(out, room, "%.*s", (int) (b - q - 1), text + q + 1);
                    return 1;
                }
            }
        }
        i = e + 1;
    }
    return 0;
}

/* every key of a text, one after another (for the diff) */
typedef void (*key_fn)(const char *key, void *ctx);
static void ini_keys(const char *text, size_t n, key_fn fn, void *ctx) {
    char section[64] = "";
    size_t i = 0;
    while (i < n) {
        size_t e = i;
        while (e < n && text[e] != '\n') e++;
        size_t a = i, b = e;
        while (a < b && (text[a] == ' ' || text[a] == '\t')) a++;
        while (b > a && (text[b - 1] == '\r' || text[b - 1] == ' ')) b--;
        if (a < b && text[a] == '[') {
            size_t c = a + 1;
            while (c < b && text[c] != ']') c++;
            snprintf(section, sizeof(section), "%.*s", (int) (c - a - 1), text + a + 1);
        } else if (a < b && text[a] != '#') {
            size_t q = a;
            while (q < b && text[q] != '=') q++;
            if (q < b) {
                char k[128];
                size_t kl = q;
                while (kl > a && (text[kl - 1] == ' ' || text[kl - 1] == '\t')) kl--;
                snprintf(k, sizeof(k), "%s%s%s%.*s", section[0] ? "[" : "", section, section[0] ? "]" : "",
                         (int) (kl - a), text + a);
                fn(k, ctx);
            } else {
                char k[128];                                /* a bare line (mods.txt) is its own key */
                snprintf(k, sizeof(k), "%.*s", (int) (b - a), text + a);
                fn(k, ctx);
            }
        }
        i = e + 1;
    }
}

typedef struct {
    const char *a; size_t an;              /* before */
    const char *b; size_t bn;              /* after */
    char *out; size_t room, used;
    int count, both;                       /* both: the key is looked for in the other text too */
} diff_t;

static void diff_key(const char *key, void *ctx) {
    diff_t *d = ctx;
    char va[96] = "", vb[96] = "";
    int ha = ini_value(d->a, d->an, key, va, sizeof(va));
    int hb = ini_value(d->b, d->bn, key, vb, sizeof(vb));
    if (d->both && ha) return;                         /* the second pass: only what the first had not */
    if (ha == hb && !strcmp(va, vb)) return;
    d->count++;
    if (d->used + 40 >= d->room) return;
    d->used += (size_t) snprintf(d->out + d->used, d->room - d->used, " %s %s->%s", key,
                                 ha ? va : "(none)", hb ? vb : "(none)");
}

/* the keys that differ between two texts, into out */
static int text_diff(const uint8_t *a, size_t an, const uint8_t *b, size_t bn, char *out, size_t room) {
    diff_t d = { (const char *) a, an, (const char *) b, bn, out, room, 0, 0, 0 };
    out[0] = 0;
    ini_keys((const char *) b, bn, diff_key, &d);       /* what the new one holds */
    d.both = 1;
    d.a = (const char *) b; d.an = bn; d.b = (const char *) a; d.bn = an;
    ini_keys((const char *) a, an, diff_key, &d);      /* and what it dropped */
    return d.count;
}

static void guard_path(const char *leaf, char *out, size_t room) {
    mkdir(g_guard, 0700);
    snprintf(out, room, "%s/%s", g_guard, leaf);
}

/* a write went through: who, and what changed against the last copy */
static void guard_written(const char *path, const char *who) {
    const char *leaf = leaf_name(path);
    if (!g_guard[0] || !guarded_leaf(leaf) || !shim_report_active()) return;
    char gp[600];
    guard_path(leaf, gp, sizeof(gp));
    size_t on = 0, nn = 0;
    uint8_t *old = read_whole(gp, &on);
    uint8_t *now = read_whole(path, &nn);
    if (!now) { free(old); return; }
    static int said;
    if (said < 80) {
        said++;
        char keys[360];
        if (!strcmp(leaf, "inputconfigs.dat"))
            snprintf(keys, sizeof(keys), " %zu -> %zu bytes%s", on, nn,
                     old && on == nn && !memcmp(old, now, nn) ? ", the same" : "");
        else if (!old) snprintf(keys, sizeof(keys), " (no earlier copy)");
        else if (!text_diff(old, on, now, nn, keys, sizeof(keys))) snprintf(keys, sizeof(keys), " nothing changed");
        /* the game's own callers of the options writer 0x7ebc90, by the return address after the call */
        static const struct { const char *at, *what; } KNOWN[] = {
            {"6e33c8", "BACK, the same language"}, {"6e2e94", "BACK, the language changed"},
            {"7ebb3c", "the reader: no file"}, {"7eba4c", "the reader: Version!=2"},
            {"8865cc", "the game shutting down"},
        };
        const char *named = "";
        for (unsigned k = 0; k < sizeof(KNOWN) / sizeof(KNOWN[0]); k++)
            if (who && strstr(who, KNOWN[k].at)) { named = KNOWN[k].what; break; }
        SAY("settings guard", "%s written by %s%s%s%s:%s", leaf, who && *who ? who : "?",
            named[0] ? " (" : "", named, named[0] ? ")" : "", keys);
    }
    write_internal(gp, now, nn);
    free(old);
    free(now);
}

/* at start: does the file still hold what was last written? */
void shim_guard_check(const char *path) {
    const char *leaf = leaf_name(path);
    if (!g_guard[0] || !guarded_leaf(leaf) || !shim_report_active()) return;
    char gp[600];
    guard_path(leaf, gp, sizeof(gp));
    struct stat gs;
    if (stat(gp, &gs) != 0) { SAY("settings guard", "%s: no copy of the last write yet", leaf); return; }
    size_t on = 0, nn = 0;
    uint8_t *old = read_whole(gp, &on);
    uint8_t *now = read_whole(path, &nn);
    if (!now) {
        SAY("settings guard", "%s: NOT READ at start, errno %d", leaf, errno);
    } else if (old && (on != nn || memcmp(old, now, nn))) {
        char keys[360], t[32];
        when(t, sizeof(t), gs.st_mtime);
        if (!strcmp(leaf, "inputconfigs.dat")) snprintf(keys, sizeof(keys), " %zu -> %zu bytes", on, nn);
        else text_diff(old, on, now, nn, keys, sizeof(keys));
        SAY("settings guard", "%s CHANGED OUTSIDE the game since %s:%s", leaf, t, keys);
        write_internal(gp, now, nn);
    }
    free(old);
    free(now);
}
#else
void shim_guard_check(const char *path) { (void) path; }
#endif

/* ------------------------------------------------------- the save's reading */

/* per slot, what its last read this launch came to */
enum { READ_NONE, READ_OK, READ_MENDED, READ_FROM_BACKUP, READ_UNREADABLE };
static int g_slot_read[4];

static FILE *mem_stream(uint8_t *data, size_t len) {
    mem_file_t *m = malloc(sizeof(*m));
    if (!m) { free(data); return NULL; }
    m->data = data; m->len = len; m->at = 0;
    FILE *f = funopen(m, mem_read, NULL, mem_seek, mem_close);
    if (!f) { free(data); free(m); }
    return f;
}

/* the newest sound backup of a progress file, as a stream: mended, or as it is when the game reads it so */
static FILE *backup_stream(const char *leaf, const char *why) {
    (void) why;
    size_t n;
    time_t t;
    int gen;
    /* the writer thread turns the copies over under this lock (backup_push): never half-way through */
    pthread_mutex_lock(&g_bk_lock);
    uint8_t *d = backup_newest(leaf, K_SAVE, &n, &t, &gen);
    pthread_mutex_unlock(&g_bk_lock);
    if (!d) return NULL;
    uint8_t *plain = d;
    size_t len = n;
    if (memcmp(d, SAVE_MAGIC, sizeof(SAVE_MAGIC) - 1)) {
        plain = lz4_unblock(d, n, &len);
        free(d);
        if (!plain) return NULL;
    }
    game_read_t g;
    int verdict = save_judge(plain, &len, leaf, &g);
    game_read_free(&g);
    if (verdict < 0) { free(plain); return NULL; }
    char tw[32];
    when(tw, sizeof(tw), t);
    SAY("saves", "read %s: %s - the backup of %s handed instead", leaf, why, tw);
    (void) tw;
    return mem_stream(plain, len);
}

/*
 * Given the stream the game has just opened, answers another one to use
 * instead - the file mended, or the backup when the game would not read the
 * file - or nothing, and then the original is rewound and the game reads it
 * as it is. A file the game reads by its own rules is always that last case.
 *
 * The one exception to "as it is" among the files the game reads: a bestiary
 * that would hold the game's reader past the end of the file (MEND_HANGS).
 * The backup goes in, or with no sound backup the state the game would have
 * reached once its loops were done, rebuilt (state_build) - the same progress,
 * without the wait.
 */
FILE *shim_save_mended(FILE *f, const char *path, const char *mode) {
    if (!f || !path || !reading_only(mode)) return NULL;
    const char *name = leaf_name(path);
    if (!strstr(name, "persistentgamedata")) return NULL;
    int slot = rep_slot(name);

    uint8_t *raw = malloc(SAVE_CAP);
    if (!raw) return NULL;
    size_t n = fread(raw, 1, SAVE_CAP, f);
    rewind(f);

    uint8_t *plain = raw;
    size_t len = n;
    int packed = n < sizeof(SAVE_MAGIC) || memcmp(raw, SAVE_MAGIC, sizeof(SAVE_MAGIC) - 1) != 0;
    if (packed) {
        plain = n ? lz4_unblock(raw, n, &len) : NULL;
        if (!plain && slot) {
            /* not ours and not a packed one of ours: another edition's save, a cut file, or rubbish */
            const char *why = n == 0 ? "UNREADABLE (empty)"
                            : (n >= 11 && !memcmp(raw, "ISAACNGSAVE", 11)) ? "UNREADABLE (another edition's save)"
                            : "UNREADABLE (not a save)";
            free(raw);
            FILE *b = backup_stream(name, why);
            g_slot_read[slot] = b ? READ_FROM_BACKUP : READ_UNREADABLE;
            if (!b) SAY("saves", "read %s: %s - no sound backup, handed as it is", name, why);
            return b;
        }
        if (!plain) { free(raw); return NULL; }
    }
    if (plain != raw) { free(raw); raw = NULL; }
    game_read_t g;
    int verdict = save_judge(plain, &len, name, &g);

    if (verdict == MEND_REFUSED) {
        /* the game answers 0 and resets the slot: the backup, if there is one */
        char why[120];
        snprintf(why, sizeof(why), "UNREADABLE (%s)", g.why);
        game_read_free(&g);
        free(plain);
        if (!slot) return NULL;
        FILE *b = backup_stream(name, why);
        g_slot_read[slot] = b ? READ_FROM_BACKUP : READ_UNREADABLE;
        if (!b) SAY("saves", "read %s: %s - no sound backup, handed as it is", name, why);
        return b;
    }

    if (verdict == MEND_HANGS) {
        unsigned long long turns = (unsigned long long) g.past_end;
        (void) turns;
        FILE *b = slot ? backup_stream(name, "the bestiary would hold the game past the end of the file") : NULL;
        if (b) {
            g_slot_read[slot] = READ_FROM_BACKUP;
        } else {
            size_t bn = 0;
            uint8_t *built = g.oom ? NULL : state_build(&g, &bn);
            b = built ? mem_stream(built, bn) : NULL;
            if (slot) g_slot_read[slot] = b ? READ_MENDED : READ_UNREADABLE;
            SAY("saves", "read %s: the bestiary would hold the game %llu turns past the end of the file - "
                "no sound backup, %s", name, turns,
                b ? "handed what the game reads up to the end of the file, rebuilt" : "handed as it is");
        }
        game_read_free(&g);
        free(plain);
        return b;
    }

    if (slot) {
        g_slot_read[slot] = verdict ? READ_MENDED : READ_OK;
        static int said;
        if (said < 12) {
            said++;
            if (g.layout) SAY("saves", "read %s: %s", name, verdict ? "mended" : "ok");
            else if (g.odd_type)
                SAY("saves", "read %s: by the game's own rules, not the writer's layout (chunk %u count 0x%08x, "
                    "%u read; the chunks end at %zu of %zu bytes) - handed as it is", name, g.odd_type,
                    g.odd_count, g.odd_read, g.stop, g.end);
            else
                SAY("saves", "read %s: by the game's own rules, not the writer's layout (the chunks end at %zu "
                    "of %zu bytes) - handed as it is", name, g.stop, g.end);
        }
    }
    game_read_free(&g);
    if (verdict == 0) {
        free(plain);
        return NULL;
    }
    FILE *mended = mem_stream(plain, len);
    if (mended) LOG("saves: %s is read mended (%zu bytes%s)", name, len, packed ? ", was LZ4" : "");
    return mended;
}

/* a progress file the game asks for that is not there: the backup, if it holds one */
FILE *shim_save_missing(const char *path, const char *mode) {
    if (!path || !reading_only(mode) || !in_saves_dir(path)) return NULL;
    const char *name = leaf_name(path);
    int slot = rep_slot(name);
    if (!slot) return NULL;
    FILE *b = backup_stream(name, "MISSING");
    if (b) g_slot_read[slot] = READ_FROM_BACKUP;
    return b;
}

/* ------------------------------------------------------- the save's writing */

static volatile int g_frozen;
static const char *volatile g_who_override;
static int g_options_writes;                /* options.ini written whole, this launch (the writer thread adds) */
static volatile int g_opts_read_seen;       /* the game has read (or first written) options.ini */
static long long g_opts_read_ms;

static long long mono_ms(void) {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (long long) t.tv_sec * 1000 + t.tv_nsec / 1000000;
}

/* an import is being written: the game's own saves go nowhere until the restart */
int shim_saves_frozen(const char *path) {
    return g_frozen && path && (strstr(path, "persistentgamedata") || strstr(path, "gamestate"));
}

/* the game is about to write a slot it could not read and got nothing for: keep what is on the disk */
void shim_saves_before_write(const char *path) {
    const char *leaf = leaf_name(path);
    int slot = rep_slot(leaf);
    if (!slot || g_slot_read[slot] != READ_UNREADABLE) return;
    g_slot_read[slot] = READ_NONE;
    size_t n = 0;
    void shim_safe_wait_path(const char *);
    shim_safe_wait_path(path);
    uint8_t *d = read_whole(path, &n);
    if (d && n) {
        pthread_mutex_lock(&g_bk_lock);
        keep_copy(leaf, "before-reset", d, n);
        pthread_mutex_unlock(&g_bk_lock);
        SAY("saves", "%s did not read and the game writes it anew - the old file is kept in the backup", leaf);
    }
    free(d);
}

/*
 * THE SEATS BESIDE A RUN (coop.c, the owner 05.10.2026: on a Continue every
 * player gets back what he played on). The run's file holds each player's
 * device as the game's number, which means nothing in another process; when
 * the game has written rep_gamestateN.dat whole, coop.c's lines - each
 * player's type and what his device IS (the screen, the keyboard, a pad by
 * its Android descriptor) - go beside it into the app's own folder,
 * port-seats-<leaf>.txt; when the game reads that run, they are handed back.
 * Not in the saves folder: nobody edits them, and a lost file only means the
 * game's own hand-out.
 */
int shim_coop_seats_text(const char *leaf, char *out, int room);
void shim_coop_seats_read(const char *leaf, const char *text, long len);

static int seats_path(const char *leaf, char *out, size_t room) {
    if (!g_files[0] || !strstr(leaf, "gamestate")) return 0;
    snprintf(out, room, "%s/port-seats-%s.txt", g_files, leaf);
    return 1;
}

/*
 * The seats as coop.c has them NOW, for a run file the game is writing: taken
 * on the frame thread at the game's write and carried with it to the writer
 * thread (safefile.c), which writes them beside the run. -1 when `path` is not
 * a run file in the saves folder (or there is no folder for the seats).
 */
int shim_saves_seats_now(const char *path, char *out, int room) {
    char at[700];
    if (!path || !in_saves_dir(path) || !seats_path(leaf_name(path), at, sizeof(at))) return -1;
    int n = shim_coop_seats_text(leaf_name(path), out, room);
    return n > 0 ? n : 0;
}

/* `text`/`n` taken at the game's write (n >= 0), or n < 0: asked of coop.c here and now */
static void seats_written(const char *path, const char *given, int given_n) {
    char at[700], text[600];
    if (!in_saves_dir(path) || !seats_path(leaf_name(path), at, sizeof(at))) return;
    int n;
    const char *put = text;
    if (given_n >= 0) { n = given_n; put = given; }
    else n = shim_coop_seats_text(leaf_name(path), text, sizeof(text));
    int ok = n > 0 && put && write_internal(at, put, (size_t) n) == 0;
    SAY("seats", "beside %s: %s (%d bytes)", leaf_name(path), n <= 0 ? "no players to keep" : ok ? "kept" : "NOT written", n);
    (void) ok;
}

static void seats_read(const char *path) {
    char at[700];
    if (!in_saves_dir(path) || !seats_path(leaf_name(path), at, sizeof(at))) return;
    size_t n = 0;
    uint8_t *d = read_whole(at, &n);
    shim_coop_seats_read(leaf_name(path), (const char *) d, d ? (long) n : 0);
    SAY("seats", "beside %s: %s", leaf_name(path), d ? "read back" : "none kept");
    free(d);
}

/*
 * Who writes, as the guard should name it: the port's own call of the game's
 * writer names itself (options_write_now). Asked at the game's fopen, on the
 * frame thread - the write itself reaches the disk later, on the writer's.
 */
const char *shim_saves_who(const char *who) {
    const char *o = g_who_override;
    return o ? o : who;
}

/*
 * A file went onto the disk whole (safefile.c): back it up if it is sound, and
 * tell the guard. The writer thread calls it for the game's writes, with the
 * seats taken at the game's write (seats_n >= 0; -1: not a run file); every
 * other writer through shim_saves_written, the seats asked here.
 */
void shim_saves_written_job(const char *path, long bytes, long ms, const char *how, const char *who,
                            const char *seats, int seats_n) {
    (void) bytes; (void) ms; (void) how;
    const char *leaf = leaf_name(path);
    int kind = kind_of(leaf);
    if (g_who_override) who = g_who_override;
    if (kind == K_OPTIONS && in_saves_dir(path)) {
        __atomic_add_fetch(&g_options_writes, 1, __ATOMIC_SEQ_CST);
        g_opts_read_seen = 1;
    }
    if (seats_n >= 0) seats_written(path, seats, seats_n);
    else if (seats_n == -2) seats_written(path, NULL, -1);
    if (kind != K_NONE && in_saves_dir(path) && g_backup[0]) {
        size_t n = 0;
        uint8_t *d = read_whole(path, &n);
        if (d) {
            const char *why = content_problem(kind, d, n, NULL);
            if (!why) backup_push(leaf, kind, d, n);
            else SAY("saves", "%s written, but it does NOT read back (%s) - not backed up", leaf, why);
            free(d);
        } else {
            SAY("saves", "%s written, but it does NOT read back (errno %d)", leaf, errno);
        }
    }
#if ISAAC_DIAG
    guard_written(path, who);
#else
    (void) who;
#endif
}

void shim_saves_written(const char *path, long bytes, long ms, const char *how, const char *who) {
    shim_saves_written_job(path, bytes, ms, how, who, NULL, -2);
}

/* every file the game opens in the saves folder, once per name and mode (report builds) */
void shim_saves_opened(const char *path, const char *mode, int ok) {
    const char *leaf = leaf_name(path);
    if (ok && mode && mode[0] == 'r') seats_read(path);
    if (!strcmp(leaf, "options.ini") && ok && mode && mode[0] == 'r' && !g_opts_read_seen) {
        g_opts_read_seen = 1;
        g_opts_read_ms = mono_ms();
    }
#if ISAAC_DIAG
    static char seen[24][80];
    static int n;
    char key[80];
    snprintf(key, sizeof(key), "%s %s", leaf, mode ? mode : "?");
    for (int i = 0; i < n; i++) if (!strcmp(seen[i], key)) return;
    if (n < 24) snprintf(seen[n++], sizeof(seen[0]), "%s", key);
    SAY("saves", "opened %s \"%s\"%s", leaf, mode ? mode : "?", ok ? "" : " - NOT there");
#else
    (void) mode;
#endif
}

/* ------------------------------------------------------------ the start */

/*
 * AIM LOCK: THE ONE-TIME CLEAN-UP (P2). Its default is off now (loader.c,
 * aimlock_default), but an options.ini written before that carries
 * AimLock=1 - the game's old default, written at the first launch, while the
 * port zeroed the byte in memory on every start since 09.09 and so nobody
 * could have played with it on. That line is set to 0 once, before the game
 * reads the file, and a mark beside it says it is done; from then on the row
 * in Tweaks is the byte's only owner.
 */
static void aimlock_once(const char *rep) {
    char mark[600], opts[600];
    snprintf(mark, sizeof(mark), "%s/.port-aimlock-v1", rep);
    if (access(mark, F_OK) == 0) return;
    snprintf(opts, sizeof(opts), "%s/options.ini", rep);
    size_t n = 0;
    uint8_t *d = read_whole(opts, &n);
    int changed = 0;
    if (d) {
        for (size_t i = 0; i + 9 <= n; i++) {
            if ((i == 0 || d[i - 1] == '\n') && !memcmp(d + i, "AimLock=1", 9)) { d[i + 8] = '0'; changed = 1; }
        }
        if (changed && shim_safe_write_file(opts, d, n) != 0) {
            SAY("aimlock", "the one-time clean-up could NOT write options.ini - tried again next start");
            free(d);
            return;
        }
        free(d);
    } else if (errno != ENOENT) {
        return;                                         /* not read: not decided, tried again next start */
    }
    int fd = open(mark, O_WRONLY | O_CREAT | O_CLOEXEC, 0660);
    if (fd >= 0) close(fd);
    SAY("aimlock", "one-time clean-up: %s", changed ? "AimLock=1 in options.ini set to 0"
                                                    : "nothing to change");
}

/*
 * The saves as this start finds them, before the game reads any: missing,
 * empty or unreadable ones come back from the backup, sound ones refresh it.
 * The report build writes a line per file - the self-check that replaces the
 * stress test on the desk (the owner does not want the phone driven).
 */
void shim_saves_startup(const char *saves) {
    if (!saves || !*saves) return;
    char rep[560];
    snprintf(rep, sizeof(rep), "%s/Repentance", saves);
    static const char *const LEAVES[] = {
        "rep_persistentgamedata1.dat", "rep_persistentgamedata2.dat", "rep_persistentgamedata3.dat",
        "options.ini", "inputconfigs.dat", "port-spawner-runs.txt",
    };
    if (!g_backup[0]) SAY("saves", "no backup folder was given - nothing can be restored");
    for (unsigned i = 0; i < sizeof(LEAVES) / sizeof(LEAVES[0]); i++) {
        const char *leaf = LEAVES[i];
        int kind = kind_of(leaf);
        char path[700];
        /* the run marks lie in the saves folder itself, the game's files in Repentance/ */
        snprintf(path, sizeof(path), "%s/%s", kind == K_MARKS ? saves : rep, leaf);
#if ISAAC_DIAG
        if (kind != K_SAVE) shim_guard_check(path);
#endif
        struct stat st;
        int there = stat(path, &st) == 0;
        size_t n = 0;
        errno = 0;
        uint8_t *d = there ? read_whole(path, &n) : NULL;
        int err = errno;
        int ach = -1;
        const char *why = !there ? "missing" : !d ? "not read" : content_problem(kind, d, n, &ach);
        char state[80];
        if (!why) snprintf(state, sizeof(state), "sound");
        else if (!d && there) snprintf(state, sizeof(state), "NOT READ (errno %d)", err);
        else snprintf(state, sizeof(state), "%s%s", there ? "UNREADABLE " : "", why);
        if (!why) {
            backup_push(leaf, kind, d, n);
        } else {
            size_t bn;
            time_t bt;
            int gen;
            uint8_t *b = backup_newest(leaf, kind, &bn, &bt, &gen);
            if (b) {
                if (d && n) {
                    pthread_mutex_lock(&g_bk_lock);
                    keep_copy(leaf, "unreadable", d, n);
                    pthread_mutex_unlock(&g_bk_lock);
                }
                int ok = shim_safe_write_file(path, b, bn) == 0;
                char tw[32];
                when(tw, sizeof(tw), bt);
                SAY("saves", "%s %s - %s from the backup of %s (copy %d)", leaf, state,
                    ok ? "RESTORED" : "could NOT be restored", tw, gen);
                (void) ok;
                free(b);
            } else if (there) {
                SAY("saves", "%s %s - the backup holds no sound copy%s", leaf, state,
                    why == HANGS_WHY ? "; the game will be handed what it reads up to the end of the file" : "");
            }
        }
#if ISAAC_DIAG
        if (shim_report_active()) {
            size_t bn = 0;
            time_t bt = 0;
            int gen = 0;
            uint8_t *b = backup_newest(leaf, kind, &bn, &bt, &gen);
            char tw[32] = "-";
            if (b) when(tw, sizeof(tw), bt);
            char extra[80] = "";
            if (kind == K_SAVE && ach >= 0)
                snprintf(extra, sizeof(extra), ", %d achievements (115:%d 249:%d 341:%d)", ach,
                         g_ach_watch[0], g_ach_watch[1], g_ach_watch[2]);
            if (kind != K_INPUT && there && d)
                snprintf(extra + strlen(extra), sizeof(extra) - strlen(extra), ", uid %s, %s",
                         st.st_uid == getuid() ? "ours" : "FOREIGN",
                         access(path, R_OK | W_OK) == 0 ? "rw" : "NOT rw");
            SAY("saves", "self-check %s: live %s, %zu bytes%s; backup copy %d of %s%s", leaf, state, n, extra,
                gen, tw, b ? (bn == n && d && !memcmp(b, d, n) ? ", the same" : ", DIFFERS") : ", none");
            if (kind == K_OPTIONS && d && n) {
                int lf = 0;
                for (size_t k = 0; k < n; k++) if (d[k] == '\n' && (k == 0 || d[k - 1] != '\r')) lf++;
                if ((n >= 3 && d[0] == 0xEF && d[1] == 0xBB && d[2] == 0xBF) || lf)
                    SAY("saves", "options.ini was edited outside: %s%s",
                        n >= 3 && d[0] == 0xEF ? "a BOM at the start " : "", lf ? "lines ending in LF alone" : "");
            }
            free(b);
        }
#endif
        free(d);
    }
    aimlock_once(rep);
}

/* ---------------------------------------------------- the frame thread's work */

/*
 * OPTIONS.INI WRITTEN THE GAME'S WAY BEFORE OUR OWN RESTARTS (P1.5). RESTART
 * TO APPLY and IMPORT end the process outright (OK after ADD MOD did too, until
 * hotfix 1.73) - and the
 * game writes options.ini only on BACK from its settings (0x6e33c4, 0x6e2e90),
 * so whatever was changed since lived in memory and was lost. Java asks
 * (requestOptionsSave) and waits on a thread of its own; the frame thread,
 * between frames, calls the game's own writer exactly as BACK does:
 * 0x7ebc90(Manager + 0x55ec8). The word there is checked first
 * (sub sp, sp, #0x150), and nothing is called before the game has read the
 * file this launch - a restart in the middle of loading writes nothing.
 */
#define OPTIONS_WRITER      0x7ebc90
#define OPTIONS_WRITER_WORD 0xD10543FFu
#define MANAGER_AT          0xac2428
#define SETTINGS_BLOCK      0x55ec8

static volatile int g_opts_asked, g_opts_done, g_opts_result;

int shim_safe_flush(int timeout_ms);
long shim_safe_queued_total(void);

/*
 * 1 written (or, without `wait`, handed to the writer), 2 not. The game's
 * writer puts the file onto the writer thread's queue (safefile.c): before our
 * own restart the queue is waited for - Java ends the process right after -
 * and only a file whole on the disk counts as written.
 */
static int options_write_now(const char *who, int wait) {
    unsigned long b = shim_image_base();
    if (!b) return 2;
    if (*(const uint32_t *) (b + OPTIONS_WRITER) != OPTIONS_WRITER_WORD) {
        SAY("options", "the writer at %x is not what was expected (%08x) - not called", OPTIONS_WRITER,
            *(const uint32_t *) (b + OPTIONS_WRITER));
        return 2;
    }
    unsigned long m = *(const unsigned long *) (b + MANAGER_AT);
    if (!m || !g_opts_read_seen) return 2;
    int before = __atomic_load_n(&g_options_writes, __ATOMIC_SEQ_CST);
    long queued = shim_safe_queued_total();
    g_who_override = who;
    ((void (*)(void *)) (b + OPTIONS_WRITER))((void *) (m + SETTINGS_BLOCK));
    g_who_override = NULL;
    if (!wait) return shim_safe_queued_total() != queued || g_options_writes != before ? 1 : 2;
    shim_safe_flush(1200);
    return __atomic_load_n(&g_options_writes, __ATOMIC_SEQ_CST) != before ? 1 : 2;
}

JNIEXPORT jint JNICALL
Java_ru_isaac_port_MainActivity_requestOptionsSave(JNIEnv *env, jobject thiz) {
    (void) env; (void) thiz;
    return __atomic_add_fetch(&g_opts_asked, 1, __ATOMIC_SEQ_CST);
}

/* 0 still waiting, 1 written, 2 not written (nothing to write yet, or it failed) */
JNIEXPORT jint JNICALL
Java_ru_isaac_port_MainActivity_optionsSaveState(JNIEnv *env, jobject thiz, jint token) {
    (void) env; (void) thiz;
    return __atomic_load_n(&g_opts_done, __ATOMIC_SEQ_CST) >= token ? g_opts_result : 0;
}

/* the import is about to write: the game's own saves go nowhere from here on */
JNIEXPORT void JNICALL
Java_ru_isaac_port_MainActivity_savesFreeze(JNIEnv *env, jobject thiz) {
    (void) env; (void) thiz;
    g_frozen = 1;
}

/* the internal folder: files/ of the app; the backup and the guard live under it */
JNIEXPORT void JNICALL
Java_ru_isaac_port_MainActivity_setBackupDir(JNIEnv *env, jobject thiz, jstring path) {
    (void) thiz;
    if (!path) return;
    const char *s = (*env)->GetStringUTFChars(env, path, NULL);
    if (s) {
        snprintf(g_backup, sizeof(g_backup), "%s/save-backup", s);
        snprintf(g_guard, sizeof(g_guard), "%s/port-guard", s);
        snprintf(g_files, sizeof(g_files), "%s", s);
        mkdir(g_backup, 0700);
    }
    (*env)->ReleaseStringUTFChars(env, path, s);
}

/* the import's two calls: a file kept aside under a tag, and a fresh file taken in as the newest */
JNIEXPORT void JNICALL
Java_ru_isaac_port_MainActivity_savesKeep(JNIEnv *env, jobject thiz, jstring path, jstring tag) {
    (void) thiz;
    if (!path || !tag) return;
    const char *p = (*env)->GetStringUTFChars(env, path, NULL);
    const char *t = (*env)->GetStringUTFChars(env, tag, NULL);
    if (p && t) {
        size_t n = 0;
        uint8_t *d = read_whole(p, &n);
        if (d && n) {
            pthread_mutex_lock(&g_bk_lock);
            keep_copy(leaf_name(p), t, d, n);
            pthread_mutex_unlock(&g_bk_lock);
        }
        free(d);
    }
    if (t) (*env)->ReleaseStringUTFChars(env, tag, t);
    if (p) (*env)->ReleaseStringUTFChars(env, path, p);
}

JNIEXPORT void JNICALL
Java_ru_isaac_port_MainActivity_savesBackup(JNIEnv *env, jobject thiz, jstring path) {
    (void) thiz;
    if (!path) return;
    const char *p = (*env)->GetStringUTFChars(env, path, NULL);
    if (p) {
        const char *leaf = leaf_name(p);
        int kind = kind_of(leaf);
        size_t n = 0;
        uint8_t *d = kind != K_NONE ? read_whole(p, &n) : NULL;
        if (d && !content_problem(kind, d, n, NULL)) backup_push(leaf, kind, d, n);
        free(d);
        (*env)->ReleaseStringUTFChars(env, path, p);
    }
}

/*
 * THE MEMORY AGAINST THE FILE (P1.6, report builds): one and ten seconds after
 * the game read options.ini, every key the writer puts down (0x7ebc90, read
 * off its code: the offset in the settings block and the format) is compared
 * with the file. A difference means somebody wrote the block over what was
 * read. Our own fields are marked: +0x24 is the stick kind (FIELD_KIND, the
 * game calls it Fullscreen), +0x4a is RumbleEnabled, given back from the
 * port's rumble setting on purpose, and +0x25 is Filter, read as zero whatever
 * the file says (loader.c filter_gone, hotfix 1.75) until the game writes it.
 */
#if ISAAC_DIAG
static const struct { const char *key; int off; char type; int ours; } OPT_KEYS[] = {
    {"MusicVolume", 0x18, 'f', 0}, {"MusicEnabled", -0x78, 'b', 0}, {"SFXVolume", 0x1c, 'f', 0},
    {"MapOpacity", 0x20, 'f', 0}, {"Fullscreen", 0x24, 'b', 1}, {"Filter", 0x25, 'b', 1},
    {"Exposure", 0x2c, 'f', 0}, {"Gamma", 0x28, 'f', 0}, {"ControllerHotplug", 0x38, 'b', 0},
    {"PopUps", 0x39, 'b', 0}, {"CameraStyle", 0x3c, 'i', 0}, {"ShowRecentItems", 0x40, 'i', 0},
    {"HudOffset", 0x44, 'f', 0}, {"TryImportSave", 0x48, 'b', 0}, {"FoundHUD", 0x3a, 'b', 0},
    {"EnableMods", 0x49, 'b', 0}, {"RumbleEnabled", 0x4a, 'b', 1}, {"ChargeBars", 0x4b, 'b', 0},
    {"BulletVisibility", 0x4c, 'b', 0}, {"TouchMode", 0x50, 'i', 0}, {"AimLock", 0x4d, 'b', 0},
};

static void options_vs_memory(const char *when_said) {
    const char *saves = shim_saves_dir();
    unsigned long b = shim_image_base();
    unsigned long m = b ? *(const unsigned long *) (b + MANAGER_AT) : 0;
    if (!saves || !m) return;
    char path[600];
    snprintf(path, sizeof(path), "%s/Repentance/options.ini", saves);
    size_t n = 0;
    void shim_safe_wait_path(const char *);
    shim_safe_wait_path(path);
    uint8_t *d = read_whole(path, &n);
    if (!d) { SAY("options", "%s: options.ini NOT READ (errno %d)", when_said, errno); return; }
    const unsigned char *blk = (const unsigned char *) (m + SETTINGS_BLOCK);
    int differ = 0;
    for (unsigned i = 0; i < sizeof(OPT_KEYS) / sizeof(OPT_KEYS[0]); i++) {
        char v[64];
        if (!ini_value((const char *) d, n, OPT_KEYS[i].key, v, sizeof(v))) continue;
        double file = atof(v), mem;
        if (OPT_KEYS[i].type == 'f') mem = *(const float *) (blk + OPT_KEYS[i].off);
        else if (OPT_KEYS[i].type == 'i') mem = *(const int *) (blk + OPT_KEYS[i].off);
        else mem = blk[OPT_KEYS[i].off];
        double gap = file - mem;
        if (gap < 0) gap = -gap;
        if (gap <= (OPT_KEYS[i].type == 'f' ? 0.0001 : 0.0)) continue;
        differ++;
        SAY("options", "%s: memory differs from the file: %s file %s memory %g%s", when_said, OPT_KEYS[i].key,
            v, mem, OPT_KEYS[i].ours ? " (ours, deliberately)" : "");
    }
    if (!differ) SAY("options", "%s: memory agrees with the file, %zu bytes", when_said, n);
    free(d);
}
#endif

/*
 * Between frames, on the thread the game runs on (sticks.c calls it).
 *
 * THE AIM LOCK ROW OF TWEAKS IS SAVED WHEN IT IS PRESSED. The Tweaks page is
 * not the game's settings screen, so no BACK of the game's ever wrote the
 * byte it flips: the choice lived until the process ended. So when the byte
 * changes - after the file is read and settled - options.ini is written the
 * game's way, as BACK would.
 */
void shim_saves_between_frames(void) {
    int asked = __atomic_load_n(&g_opts_asked, __ATOMIC_SEQ_CST);
    if (asked > g_opts_done) {
        int r = options_write_now("the port, before its own restart", 1);
        g_opts_result = r;
        __atomic_store_n(&g_opts_done, asked, __ATOMIC_SEQ_CST);
        SAY("options", "%s before the restart", r == 1 ? "written the game's way" : "NOT written");
    }
    unsigned long b = shim_image_base();
    unsigned long m = b ? *(const unsigned long *) (b + MANAGER_AT) : 0;
    if (!m) return;
    if (!g_opts_read_seen) {
        /*
         * The read is seen at fopen (shim_saves_opened). Should the game ever
         * read the file some other way, ten seconds of frames with the
         * settings in place are taken as "read" - the reader runs at start.
         */
        static int frames;
        if (++frames < 600) return;
        g_opts_read_seen = 1;
        SAY("options", "its read was never seen at fopen - taken as read after 600 frames");
    }
    static int settle, aim = -1;
    if (settle < 120) { settle++; return; }
    int now = ((const unsigned char *) (m + SETTINGS_BLOCK))[0x4d];
    if (aim < 0) aim = now;
    else if (now != aim) {
        aim = now;
        int r = options_write_now("AIM LOCK on the Tweaks page", 0);
        SAY("aimlock", "the row set %d; options.ini %s", now, r == 1 ? "written" : "NOT written");
        (void) r;
    }
#if ISAAC_DIAG
    static int checked;
    long long since = mono_ms() - g_opts_read_ms;
    if (g_opts_read_ms && checked == 0 && since >= 1000) { checked = 1; options_vs_memory("1 s after the read"); }
    if (g_opts_read_ms && checked == 1 && since >= 10000) { checked = 2; options_vs_memory("10 s after the read"); }
#endif
}
