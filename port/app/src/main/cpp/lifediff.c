/*
 * WHAT A MINIMISE CHANGES IN THE RUN. Report builds only (the owner's item 5,
 * 23.09.2026).
 *
 * Effects "froze for good" after the game had been minimised, and not for one
 * item: the one road found by name (Gamekid, the save's TriggerNewRoom) is
 * closed, but nobody can try eight hundred items by hand. So the question is
 * asked of the whole run instead: its state is copied four times -
 *
 *   A  going away, before the game hears of it
 *   B  going away, right after its handlers ran (the save among them)
 *   C  coming back, before the game hears of it
 *   D  coming back, right after its handlers ran
 *
 * - and the report lists what differs between A and B (what the minimise
 * itself does), B and C (what happened while we were away, and in how many
 * frames) and C and D (what the return does). The run is the Game object
 * (every byte of it), every player, and the first bytes of every entity in
 * the room, matched by address. Anything an item keeps anywhere in there that
 * a minimise touches shows up by offset, whatever the item.
 *
 * Nothing is written into the run; four copies of about three megabytes are
 * taken on two frames and let go after the report.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

unsigned long shim_image_base(void);
int shim_report_active(void);
void report_line(const char *, const char *);
int shim_trace_readable(const void *p, unsigned long n);
unsigned long shim_eid_list_now(void);
int shim_eid_players(unsigned long *out, int max);

#define GAME_PTR      0xac3b90
#define GAME_MAX      0x300000UL     /* the Game object ends before this; read up to its last page */
#define PLAYER_BYTES  0x2800UL       /* past the twin at +0x2580 */
#define ENTITY_BYTES  0x400UL
#define ENTITIES_MAX  256
#define PLAYERS_MAX   8
#define ENT_TYPE      0x38
#define ENT_VARIANT   0x3c
#define ENT_SUBTYPE   0x40
#define PAGE          0x1000UL
#define LINES_PER     16             /* changed ranges said per kind (game, players, entities) */

typedef struct {
    unsigned long at;
    unsigned long size;
    unsigned char *bytes;
    int type, variant, subtype;
    int player;                      /* its place among the players, -1 if not one */
} piece_t;

typedef struct {
    int taken;
    piece_t game;
    piece_t players[PLAYERS_MAX];
    int nplayers;
    piece_t ents[ENTITIES_MAX];
    int nents;
} snap_t;

static snap_t g_snap[4];
static long g_away_frames;

static unsigned long readable_extent(unsigned long at, unsigned long max) {
    unsigned long n = 0;
    while (n < max) {
        unsigned long step = PAGE - ((at + n) & (PAGE - 1));
        if (step > max - n) step = max - n;
        if (!shim_trace_readable((const void *) (at + n), step)) break;
        n += step;
    }
    return n;
}

static int copy_piece(piece_t *p, unsigned long at, unsigned long max) {
    memset(p, 0, sizeof(*p));
    p->player = -1;
    if (!at) return 0;
    unsigned long n = readable_extent(at, max);
    if (!n) return 0;
    p->bytes = (unsigned char *) malloc(n);
    if (!p->bytes) return 0;
    memcpy(p->bytes, (const void *) at, n);
    p->at = at;
    p->size = n;
    if (n >= ENT_SUBTYPE + 4) {
        p->type = *(const int *) (p->bytes + ENT_TYPE);
        p->variant = *(const int *) (p->bytes + ENT_VARIANT);
        p->subtype = *(const int *) (p->bytes + ENT_SUBTYPE);
    }
    return 1;
}

static void drop(snap_t *s) {
    free(s->game.bytes);
    for (int i = 0; i < s->nplayers; i++) free(s->players[i].bytes);
    for (int i = 0; i < s->nents; i++) free(s->ents[i].bytes);
    memset(s, 0, sizeof(*s));
}

static void take(snap_t *s) {
    drop(s);
    unsigned long b = shim_image_base();
    unsigned long game = b ? *(const unsigned long *) (b + GAME_PTR) : 0;
    if (!game) return;
    s->taken = 1;
    copy_piece(&s->game, game, GAME_MAX);
    unsigned long pl[PLAYERS_MAX];
    int n = shim_eid_players(pl, PLAYERS_MAX);
    for (int i = 0; i < n && s->nplayers < PLAYERS_MAX; i++)
        if (copy_piece(&s->players[s->nplayers], pl[i], PLAYER_BYTES))
            s->players[s->nplayers++].player = i;
    unsigned long list = shim_eid_list_now();
    if (list && shim_trace_readable((const void *) (list + 0x78), 16)) {
        const unsigned long *e = *(const unsigned long *const *) (list + 0x78);
        unsigned count = *(const unsigned *) (list + 0x84);
        if (e && count <= 4096 && shim_trace_readable(e, (unsigned long) count * 8))
            for (unsigned i = 0; i < count && s->nents < ENTITIES_MAX; i++)
                if (copy_piece(&s->ents[s->nents], e[i], ENTITY_BYTES)) s->nents++;
    }
}

/* the changed runs of two copies of one thing, said as offsets; answers how many bytes differ */
static long compare(const char *what, const piece_t *x, const piece_t *y, int *budget) {
    if (!x->bytes || !y->bytes || x->at != y->at) return 0;
    unsigned long n = x->size < y->size ? x->size : y->size;
    long total = 0;
    unsigned long i = 0;
    while (i < n) {
        if (x->bytes[i] == y->bytes[i]) { i++; continue; }
        unsigned long from = i;
        /* a run: differing bytes with gaps of fewer than eight equal ones */
        unsigned long last = i;
        while (i < n && i - last < 8) {
            if (x->bytes[i] != y->bytes[i]) { last = i; total++; }
            i++;
        }
        if (*budget > 0) {
            (*budget)--;
            char line[240], was[40] = "", now[40] = "";
            unsigned long len = last - from + 1, show = len < 8 ? len : 8;
            for (unsigned long k = 0; k < show; k++) {
                snprintf(was + k * 2, 3, "%02x", x->bytes[from + k]);
                snprintf(now + k * 2, 3, "%02x", y->bytes[from + k]);
            }
            snprintf(line, sizeof(line), "  %s +0x%lx, %lu byte(s): %s -> %s%s", what, from, len,
                     was, now, len > show ? " ..." : "");
            report_line("minimise", line);
        }
    }
    return total;
}

static void between(const char *name, const snap_t *x, const snap_t *y) {
    char line[240];
    /* a budget each, so a noisy Game object cannot drown the players and the room */
    int budget = LINES_PER, pbudget = LINES_PER, ebudget = LINES_PER;
    snprintf(line, sizeof(line), "%s:", name);
    report_line("minimise", line);
    long g = compare("game", &x->game, &y->game, &budget);
    long p = 0;
    for (int i = 0; i < x->nplayers; i++)
        for (int k = 0; k < y->nplayers; k++)
            if (x->players[i].at == y->players[k].at) {
                char what[32];
                snprintf(what, sizeof(what), "player %d", x->players[i].player);
                p += compare(what, &x->players[i], &y->players[k], &pbudget);
            }
    long e = 0;
    int gone = 0, born = 0;
    for (int i = 0; i < x->nents; i++) {
        int found = 0;
        for (int k = 0; k < y->nents; k++)
            if (x->ents[i].at == y->ents[k].at) {
                found = 1;
                char what[48];
                snprintf(what, sizeof(what), "entity %d.%d.%d", x->ents[i].type, x->ents[i].variant,
                         x->ents[i].subtype);
                e += compare(what, &x->ents[i], &y->ents[k], &ebudget);
            }
        if (!found) {
            gone++;
            snprintf(line, sizeof(line), "  entity gone: %d.%d.%d", x->ents[i].type, x->ents[i].variant,
                     x->ents[i].subtype);
            report_line("minimise", line);
        }
    }
    for (int k = 0; k < y->nents; k++) {
        int found = 0;
        for (int i = 0; i < x->nents; i++) if (x->ents[i].at == y->ents[k].at) found = 1;
        if (!found) {
            born++;
            snprintf(line, sizeof(line), "  entity new: %d.%d.%d", y->ents[k].type, y->ents[k].variant,
                     y->ents[k].subtype);
            report_line("minimise", line);
        }
    }
    snprintf(line, sizeof(line), "  in all: game %ld bytes (of %lu), players %ld, entities %ld"
             " (%d of them), %d gone, %d new%s", g, x->game.size, p, e, x->nents, gone, born,
             (budget <= 0 || pbudget <= 0 || ebudget <= 0) ? "; more runs than said" : "");
    report_line("minimise", line);
}

/* 0 going away before, 1 after; 2 coming back before, 3 after */
void shim_lifediff_mark(int stage) {
    static int trips;                /* two trips are enough to tell noise from a pattern */
    if (!shim_report_active() || stage < 0 || stage > 3 || trips >= 2) return;
    if (stage == 3) trips++;
    take(&g_snap[stage]);
    if (stage == 1) g_away_frames = 0;
    if (stage != 3) return;
    if (g_snap[0].taken && g_snap[1].taken) between("the minimise itself (A -> B)", &g_snap[0], &g_snap[1]);
    if (g_snap[1].taken && g_snap[2].taken) {
        char line[120];
        snprintf(line, sizeof(line), "while away: %ld frame(s) ran", g_away_frames);
        report_line("minimise", line);
        between("while away (B -> C)", &g_snap[1], &g_snap[2]);
    }
    if (g_snap[2].taken) between("the return itself (C -> D)", &g_snap[2], &g_snap[3]);
    for (int i = 0; i < 4; i++) drop(&g_snap[i]);
}

/* every frame the game runs while we are away */
void shim_lifediff_frame(int away) {
    if (away) g_away_frames++;
}
