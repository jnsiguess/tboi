/*
 * THE ITEM SPAWNER - the search.
 *
 * What the owner asked for (16.09.2026): from the pause menu, when switched on
 * in Tweaks, a card like the game's controller popup with a search line. Type
 * a name, and the nearest matching items, trinkets and cards show as you type;
 * pick one and it is given. Pills are listed by EFFECT (the owner, 07.10.2026):
 * a colour means nothing across runs, so the give asks the game for the colour
 * that carries the effect in this run, as the PC console does (give_thing).
 *
 * This file is the part that does not depend on where or how the card is
 * drawn: the query and the ranked list of what it matches.
 *
 * WHERE THE NAMES COME FROM. The description database already names every
 * item, trinket and card in twenty languages (eid.c). A player may type the
 * name as their game shows it or the English one they know from the wiki, so
 * both are searched, and the better of the two scores.
 *
 * HOW A NAME IS COMPARED. Both sides are brought to lower case first - ASCII,
 * Latin-1, Greek and Cyrillic, which covers every language the database has
 * whose letters have case (U+0401 with U+0415, since nobody types the dots). Then, best
 * first:
 *
 *     the whole name equals the query
 *     the name starts with the query
 *     a word inside the name starts with it       (Mom's KNIFE for "kni")
 *     the query appears anywhere in the name
 *
 * and inside one rank a shorter name comes first, so "Onion" puts The Sad
 * Onion above longer names that merely contain the word. A query that is a
 * number also finds the thing with that id, which is how the wiki numbers them.
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <time.h>
#include <unistd.h>
#include "diag.h"
#include "spawner_enemies.h"
#include "spawner_title.h"
#include "spawner_pickups.h"
#include "eid_charge.h"                      /* EID_CARD_KIND: which cards the game calls runes */

#define KIND_ITEM    1
#define KIND_TRINKET 2
#define KIND_CARD    3
#define KIND_ENEMY   4                      /* an index into SP_ENEMY, not an id */
/*
 * THE ONE LIST (the owner, 07.10.2026, mock-ups v2): ITEMS holds everything a
 * run can be given - collectibles, keys, bombs, coins, hearts, batteries,
 * cards, runes, pills by effect (horse and gold too), trinkets (golden ones
 * beside them), and the loot: sacks, chests, troll bombs.
 */
#define KIND_PILL    5                      /* id: the pill effect 0..49 or PILL_GOLD, | PILL_HORSE */
#define KIND_PICKUP  6                      /* an index into SP_PICKUP (spawner_pickups.h) */
#define KIND_HEAD    7                      /* a section heading of the whole list: SP_SEC_* */
#define TRINKET_GOLD 0x8000                 /* the game's golden trinket: its flag in the subtype */
#define PILL_HORSE   0x100
#define PILL_GOLD    99                     /* the gold pill, colour 14 */
#define EID_PILLS    4                      /* the description database's pills: effect + 1, 9999 the gold one */
#define EID_GOLD_PILL 9999

/*
 * THE ENEMIES (the owner, 30.09.2026: a tab of their own, every creature with
 * its picture). The list is the game's bestiary - entities2.xml entries with a
 * <bestiary> - with the names the game itself gives them, in its order of
 * bosses by floor, minibosses, monsters (tools/spawner_enemies.py ->
 * spawner_enemies.h). Spawning one: 0x88fcbc(game, type, variant, pos,
 * velocity, spawner 0, subtype, seed, 0) at 0x59de50(room, player pos, 120, 0,
 * 0), a free spot 120 units from the player.
 */
#define TW_SETS_MAX  ((int) (sizeof(SP_TITLE) / sizeof(SP_TITLE[0])))

#define QUERY_MAX 48                        /* code points */
/* 64 until 25.10.2026: a broad query ("a", "the") was cut at 64 rows and what
   fell past them looked lost from the list (the owner: "part of the items went
   missing"). Now eight times as many; the empty query lists everything anyway. */
#define HITS_MAX  512

typedef struct {
    unsigned char kind;
    unsigned short id;
    int score;                              /* higher is better */
} hit_t;

int shim_eid_each_name(unsigned kind,
                       void (*fn)(unsigned id, const char *name, const char *english, void *ctx),
                       void *ctx);
int shim_tweaks_set_index(void);

/* an id the game has: the description database names removed ones too (items 43, 61..., trinket 47) */
static int real_id(unsigned char kind, unsigned id) {
    if (kind == KIND_ITEM) return id <= SP_COLLECTIBLE_TOP && (SP_COLLECTIBLE_REAL[id / 8] >> (id % 8) & 1);
    if (kind == KIND_TRINKET) return id <= SP_TRINKET_TOP && (SP_TRINKET_REAL[id / 8] >> (id % 8) & 1);
    return 1;
}

/* ------------------------------------------------------------ folding */
static unsigned next_cp(const char **p) {
    const unsigned char *s = (const unsigned char *) *p;
    unsigned c = s[0];
    if (c < 0x80) { *p += 1; return c; }
    if ((c & 0xe0) == 0xc0 && s[1]) { *p += 2; return ((c & 0x1f) << 6) | (s[1] & 0x3f); }
    if ((c & 0xf0) == 0xe0 && s[1] && s[2]) {
        *p += 3;
        return ((c & 0x0f) << 12) | ((s[1] & 0x3f) << 6) | (s[2] & 0x3f);
    }
    if ((c & 0xf8) == 0xf0 && s[1] && s[2] && s[3]) {
        *p += 4;
        return ((c & 0x07) << 18) | ((s[1] & 0x3f) << 12) | ((s[2] & 0x3f) << 6) | (s[3] & 0x3f);
    }
    *p += 1;
    return 0xfffd;
}

static unsigned fold(unsigned c) {
    if (c >= 'A' && c <= 'Z') return c + 32;
    if (c >= 0xc0 && c <= 0xde && c != 0xd7) return c + 32;          /* Latin-1 */
    if (c >= 0x391 && c <= 0x3ab && c != 0x3a2) return c + 32;       /* Greek */
    if (c >= 0x410 && c <= 0x42f) return c + 32;                     /* Cyrillic */
    if (c >= 0x400 && c <= 0x40f) return c + 80;                     /* U+0400..U+040F */
    if (c == 0x451) return 0x435;                                    /* U+0451 as U+0435 */
    if (c == 0x2019 || c == 0x2018) return '\'';                     /* curly apostrophes */
    return c;
}

/* a string as folded code points; answers how many */
static int folded(const char *s, unsigned *out, int room) {
    int n = 0;
    while (s && *s && n < room) {
        unsigned c = fold(next_cp(&s));
        if (c == 0x451) c = 0x435;
        out[n++] = c;
    }
    return n;
}

static int is_space(unsigned c) { return c == ' ' || c == '-' || c == '\'' || c == '.'; }

/* how well a name matches; 0 for not at all */
static int score_of(const unsigned *q, int qn, const char *name) {
    if (!name || !qn) return 0;
    unsigned buf[160];
    int n = folded(name, buf, 160);
    if (n < qn) return 0;
    int best = 0;
    for (int at = 0; at + qn <= n; at++) {
        if (memcmp(buf + at, q, (size_t) qn * sizeof(unsigned))) continue;
        int rank;
        if (at == 0 && n == qn) rank = 4;
        else if (at == 0) rank = 3;
        else if (is_space(buf[at - 1])) rank = 2;
        else rank = 1;
        if (rank > best) best = rank;
    }
    /* rank first; inside a rank, the shorter name */
    return best ? best * 1000 - (n > 999 ? 999 : n) : 0;
}

/* ------------------------------------------------------------ search */
typedef struct {
    const unsigned *q;
    int qn;
    long number;                            /* the query as an id, or -1 */
    unsigned char kind;
    hit_t *hits;
    int count;
} search_t;

static void keep(search_t *s, unsigned id, int score) {
    if (score <= 0) return;
    int at = s->count;
    if (at == HITS_MAX) {
        if (score <= s->hits[HITS_MAX - 1].score) return;
        at = HITS_MAX - 1;
    } else {
        s->count++;
    }
    while (at > 0 && s->hits[at - 1].score < score) {
        s->hits[at] = s->hits[at - 1];
        at--;
    }
    s->hits[at].kind = s->kind;
    s->hits[at].id = (unsigned short) id;
    s->hits[at].score = score;
}

/* ONLY HELD (below): the search keeps the player's collectibles alone while it is on */
static int g_only_held;
static int held_of(unsigned id);

static void each(unsigned id, const char *name, const char *english, void *ctx) {
    search_t *s = (search_t *) ctx;
    if (g_only_held && (s->kind != KIND_ITEM || !held_of(id))) return;
    int a = score_of(s->q, s->qn, name), b = score_of(s->q, s->qn, english);
    int score = a > b ? a : b;
    if (s->kind == KIND_PILL) {               /* a pill and its horse pill, side by side */
        if (id != EID_GOLD_PILL && (id < 1 || id > 50)) return;
        unsigned pill = id == EID_GOLD_PILL ? PILL_GOLD : id - 1;
        keep(s, pill, score);
        keep(s, pill | PILL_HORSE, score);
        return;
    }
    if ((s->kind == KIND_ITEM || s->kind == KIND_TRINKET) && !real_id(s->kind, id)) return;
    if (s->number >= 0 && (long) id == s->number) score = 5000;       /* the id itself */
    keep(s, id, score);
    if (s->kind == KIND_TRINKET) keep(s, id | TRINKET_GOLD, score);  /* its golden one beside it */
}

/*
 * The best matches for a query, most likely first; answers how many were
 * written. Items come before trinkets and trinkets before cards when they
 * score the same, because the kinds are searched in that order and a later
 * equal score never pushes an earlier one down.
 */
int shim_spawner_search(const char *query, hit_t *out, int room) {
    unsigned q[QUERY_MAX];
    int qn = folded(query, q, QUERY_MAX);
    while (qn && q[qn - 1] == ' ') qn--;                              /* a trailing space */
    int lead = 0;
    while (lead < qn && q[lead] == ' ') lead++;
    if (lead) { memmove(q, q + lead, (size_t) (qn - lead) * sizeof(unsigned)); qn -= lead; }
    if (!qn || !out || room <= 0) return 0;

    long number = 0;
    for (int i = 0; i < qn && number >= 0; i++)
        number = (q[i] >= '0' && q[i] <= '9') ? number * 10 + (long) (q[i] - '0') : -1;

    hit_t hits[HITS_MAX];
    search_t s = { q, qn, number, 0, hits, 0 };
    static const unsigned char KINDS[] = { KIND_ITEM, KIND_TRINKET, KIND_CARD };
    for (unsigned k = 0; k < sizeof(KINDS); k++) {
        s.kind = KINDS[k];
        shim_eid_each_name(KINDS[k], each, &s);
    }
    s.kind = KIND_PILL;
    shim_eid_each_name(EID_PILLS, each, &s);
    /* the pickups and the loot: their names in the menus' language and the English ones */
    int set = shim_tweaks_set_index();
    if (set < 0 || set > 6) set = 0;
    s.kind = KIND_PICKUP;
    for (unsigned i = 0; i < SP_PICKUPS && !g_only_held; i++) {
        int a = score_of(q, qn, SP_PICKUP[i].name[set]), b = set ? score_of(q, qn, SP_PICKUP[i].name[0]) : 0;
        keep(&s, i, a > b ? a : b);
    }
    int n = s.count < room ? s.count : room;
    memcpy(out, hits, (size_t) n * sizeof(hit_t));
    return n;
}

/*
 * The enemies the same way: by the name in the game's language and by the
 * English one, the better scoring. A number is never an enemy - the wiki
 * numbers items, and type numbers mean nothing to a player.
 */

int shim_spawner_enemy_search(const char *query, hit_t *out, int room) {
    unsigned q[QUERY_MAX];
    int qn = folded(query, q, QUERY_MAX);
    while (qn && q[qn - 1] == ' ') qn--;
    int lead = 0;
    while (lead < qn && q[lead] == ' ') lead++;
    if (lead) { memmove(q, q + lead, (size_t) (qn - lead) * sizeof(unsigned)); qn -= lead; }
    if (!qn || !out || room <= 0) return 0;
    int set = shim_tweaks_set_index();
    if (set < 0 || set > 6) set = 0;
    hit_t hits[HITS_MAX];
    search_t s = { q, qn, -1, KIND_ENEMY, hits, 0 };
    for (unsigned i = 0; i < SP_ENEMIES; i++) {
        int a = score_of(q, qn, SP_ENEMY[i].name[set]), b = set ? score_of(q, qn, SP_ENEMY[i].name[0]) : 0;
        keep(&s, i, a > b ? a : b);
    }
    int n = s.count < room ? s.count : room;
    memcpy(out, hits, (size_t) n * sizeof(hit_t));
    return n;
}

/* ------------------------------------------------------------ the line */
/*
 * The search line and what it matches, kept here and read by whatever draws
 * the card. Letters arrive between frames on the frame thread (objc.c hands the
 * soft keyboard's text here instead of to the game while the card is up, and
 * the hardware keyboard's keys the same way), so nothing here needs a lock.
 */
/*
 * FOUR TABS (the owner, 30.09 and 02.10.2026; the mock-ups approved 02.10):
 * ITEMS as it always was, ENEMIES, TELEPORT and STATS. Each searching tab has
 * a line of its own, kept while the card is up: leaving a tab and coming back
 * finds what was typed still there. The tab itself is kept between openings.
 */
#define TAB_ITEMS    0
#define TAB_ENEMIES  1
#define TAB_TELEPORT 2
#define TAB_STATS    3
#define TABS         4

#define SHOWN_MAX HITS_MAX

static int g_open;
static int g_tab;
static char g_queries[2][160];               /* ITEMS, ENEMIES */
static hit_t g_hits[SHOWN_MAX];
static int g_hit_count;

/*
 * ITEMS' rows: with nothing typed the whole list under its headings (the
 * owner, 07.10.2026: everything at once, scrolled from the top); with a query
 * the best matches alone, no headings.
 */
#define ITEM_ROWS_MAX 1600
static hit_t g_rows[ITEM_ROWS_MAX];
static int g_rows_n;

/*
 * HOW MANY (the owner, 07.10.2026): every row but a collectible carries its own
 * count, 1..99, kept while the game runs. A slot per thing: 0 is one.
 */
#define COUNT_SLOTS 700
#define COUNT_MOST  99
static unsigned char g_count[COUNT_SLOTS];

static int count_slot(const hit_t *h) {
    switch (h->kind) {
    case KIND_PICKUP:  return h->id < SP_PICKUPS ? (int) h->id : -1;                       /* 0..46 */
    case KIND_CARD:    return h->id <= 120 ? 64 + (int) h->id : -1;                        /* 64..184 */
    case KIND_PILL: {
        int e = h->id & 0xff;
        if (e == PILL_GOLD) e = 50;
        return e <= 50 ? 190 + e + ((h->id & PILL_HORSE) ? 51 : 0) : -1;                   /* 190..291 */
    }
    case KIND_TRINKET: {
        int t = h->id & 0x7fff;
        return t < 200 ? 300 + t + ((h->id & TRINKET_GOLD) ? 200 : 0) : -1;                /* 300..699 */
    }
    default:           return -1;
    }
}

/* the row's count, or 0 for a row without one (a collectible, a heading) */
static int count_of(const hit_t *h) {
    int slot = count_slot(h);
    return slot < 0 ? 0 : g_count[slot] + 1;
}

/*
 * ON THE GROUND (the owner, 07.10.2026): ON puts a collectible on a pedestal
 * beside the player and pickups on the floor near him; OFF gives into the
 * player as a pickup would. Loot is always on the floor. Remembered between
 * launches: port-touch.ini `spawnerground` (sticks.c writes and reads it).
 */
static int g_ground;
static int g_line_focus;                     /* ITEMS: a pad's or a keyboard's cursor is on the line */
#define FOCUS_WHO   0                         /* STATS: the player's switch */
#define FOCUS_LIST  1                         /* the list */
#define FOCUS_FOOT  2                         /* the foot's buttons */
static int g_stat_focus = FOCUS_LIST;

int shim_spawner_ground_get(void) { return g_ground; }
void shim_spawner_ground_set(int on) { g_ground = on != 0; }
void shim_sticks_settings_save(void);

static void ground_flip(void) {
    g_ground = !g_ground;
    shim_sticks_settings_save();
}

/*
 * ONLY HELD (the owner, 21.10.2026): left of ON THE GROUND and made the same
 * way; ON shows the collectibles the player shown holds and nothing else.
 * Remembered between launches: port-touch.ini `spawnerheld`. The line's
 * cursor (g_line_focus) is 1 on ON THE GROUND, 2 on ONLY HELD.
 */
int shim_spawner_held_get(void) { return g_only_held; }
void shim_spawner_held_set(int on) { g_only_held = on != 0; }
static void held_flip(void);                 /* the list is searched again: below */

void shim_ime_show(int show);
static void rows_rebuilt(int tab);           /* the card's lists, below */
static void stats_text(const char *text);
static void tab_confirm(void);

static char *query_now(void) { return g_queries[g_tab == TAB_ENEMIES]; }

static void research(void) {
    if (g_tab == TAB_ITEMS) g_hit_count = shim_spawner_search(g_queries[0], g_hits, SHOWN_MAX);
    rows_rebuilt(g_tab);
}

static void held_flip(void) {
    g_only_held = !g_only_held;
    shim_sticks_settings_save();
    research();
}

int shim_spawner_is_open(void) { return g_open; }
const char *shim_spawner_query(void) { return query_now(); }

/*
 * Opening and closing are only ASKED for here: the card is a dialog of the
 * game's own, and the dialog list may be touched on the frame thread alone
 * (shim_spawner_between_frames does it). A tap on the pause screen arrives on
 * the touch thread, Escape between frames - both just ask.
 */
static volatile int g_want_open, g_want_close;

void shim_spawner_open(void) { g_want_open = 1; }
void shim_spawner_close(void) { if (g_open) g_want_close = 1; }

/* drop the last whole character, not the last byte of it */
static void rub_out(char *q) {
    size_t n = strlen(q);
    while (n > 0) {
        n--;
        if (((unsigned char) q[n] & 0xc0) != 0x80) break;
    }
    q[n] = 0;
}

/*
 * Text from the soft keyboard: a letter, "" for delete, "\n" for Return. Answers
 * 1 when the card took it, so objc.c does not hand it to the game as well.
 * Return is the tab's confirm (give, spawn, go); STATS reads numbers; TELEPORT
 * has no line and takes Return alone.
 */
int shim_spawner_text(const char *text) {
    if (!g_open || !text) return 0;
    if (g_tab == TAB_STATS) { stats_text(text); return 1; }
    if (!strcmp(text, "\n")) { tab_confirm(); return 1; }
    if (g_tab == TAB_TELEPORT) return 1;
    char *q = query_now();
    if (!*text) rub_out(q);
    else if (strlen(q) + strlen(text) < sizeof(g_queries[0])) strcat(q, text);
    research();
    return 1;
}

/*
 * THE SEARCH TYPES WHAT THE LAYOUT TYPES (batch 2, K2, 01.10.2026).
 *
 * A key arrives as the HID usage of its PLACE (the game reads places, as iOS
 * does) and, beside it, the character the keyboard's layout makes of it. The
 * search is a text field, so it takes the character: capitals and signs as
 * typed (the Shift bit was read as 0x2, where the modifiers carry it in bit 17
 * as UIKey does - capitals never came). research() matches the names of the
 * language the game is in AND the English ones (shim_spawner_search, `each`),
 * so a Cyrillic letter is kept when the game's names are Cyrillic; with any
 * other language a Cyrillic letter could match nothing, and it is taken for
 * the Latin letter on the same key - the Russian layout is still a QWERTY
 * keyboard underneath, and typing the Russian letters on A, S, D means "asd" there.
 */
static int g_cyr_names = -1;            /* the language's names hold Cyrillic: -1 not yet known */
static const char *g_cyr_for;           /* for which language */

static void cyr_look(unsigned id, const char *name, const char *english, void *ctx) {
    (void) id; (void) english;
    int *found = (int *) ctx;
    for (const char *p = name; p && *p && !*found; ) {
        unsigned c = next_cp(&p);
        if (c >= 0x400 && c <= 0x4ff) *found = 1;
    }
}

static int names_are_cyrillic(void) {
    const char *shim_eid_language_now(void);
    const char *now = shim_eid_language_now();
    if (g_cyr_names < 0 || now != g_cyr_for) {
        int found = 0;
        shim_eid_each_name(KIND_ITEM, cyr_look, &found);
        g_cyr_names = found;
        g_cyr_for = now;
    }
    return g_cyr_names;
}

/* the Latin sign on the same key of a US board, for a HID usage; 0 for none */
static unsigned latin_at(int code) {
    if (code >= 4 && code <= 29) return 'a' + (unsigned) (code - 4);
    switch (code) {
        case 0x2d: return '-';  case 0x2e: return '=';  case 0x2f: return '[';  case 0x30: return ']';
        case 0x31: return '\\'; case 0x33: return ';';  case 0x34: return '\''; case 0x35: return '`';
        case 0x36: return ',';  case 0x37: return '.';  case 0x38: return '/';
        default: return 0;
    }
}

static int utf8_put(unsigned c, char *out) {
    if (c < 0x80) { out[0] = (char) c; out[1] = 0; return 1; }
    if (c < 0x800) { out[0] = (char) (0xc0 | (c >> 6)); out[1] = (char) (0x80 | (c & 0x3f)); out[2] = 0; return 2; }
    if (c < 0x10000) {
        out[0] = (char) (0xe0 | (c >> 12)); out[1] = (char) (0x80 | ((c >> 6) & 0x3f));
        out[2] = (char) (0x80 | (c & 0x3f)); out[3] = 0; return 3;
    }
    return 0;
}

/*
 * A hardware key: the HID usage of its place, the modifiers, the character the
 * layout made of it (0 when there is none), and whether it is the system's
 * auto-repeat. Backspace, Return, Escape and the arrows are the card's by
 * their place; any other key types its character. Everything is swallowed
 * while the card is up, so a key meant for the search never moves Isaac
 * behind it - and the release of a key is taken only when its press was:
 * a key held when the card opened is let go in the game, not left held there.
 * A repeat is the text field's: Backspace rubs out, an arrow walks, a letter
 * types again; Return and Escape do not repeat. Answers 1 when taken.
 */
static unsigned char g_took[256];
static void card_walk(int dx, int dy);        /* the card's arrows, below */
static void tab_step(int dir);
static int g_kb_hold;                         /* a value held: -1 / 1 while Left / Right is held, else 0 */
static void value_hold_press(int dir);
static int hold_applies(void);

int shim_spawner_key(int code, int down, int mods, int ch, int repeat) {
    int slot = code & 0xff;
    if (!down) {
        if ((code == 80 && g_kb_hold < 0) || (code == 79 && g_kb_hold > 0)) g_kb_hold = 0;
        if (!g_took[slot]) return 0;
        g_took[slot] = 0;
        return 1;
    }
    if (!g_open) return 0;
    g_took[slot] = 1;
    if (code == 42) { shim_spawner_text(""); return 1; }
    if (code == 40 || code == 0x9e || code == 88) { if (!repeat) shim_spawner_text("\n"); return 1; }
    if (code == 41) { if (!repeat) shim_spawner_close(); return 1; }
    /* Tab and Shift+Tab walk the tabs; asked before the co-op gesture (objc.c) */
    if (code == 43) { if (!repeat) tab_step((mods >> 17) & 1 ? -1 : 1); return 1; }
    if (code == 81) { card_walk(0, 1); return 1; }                   /* down */
    if (code == 82) { card_walk(0, -1); return 1; }                  /* up */
    /* a value steps while the key is held (value_hold, below): its own repeat, not the system's -
       STATS, and the count of an ITEMS row */
    if ((code == 80 || code == 79) && hold_applies()) {
        if (!repeat) { g_kb_hold = code == 80 ? -1 : 1; value_hold_press(g_kb_hold); }
        return 1;
    }
    if (code == 80) { card_walk(-1, 0); return 1; }                  /* left */
    if (code == 79) { card_walk(1, 0); return 1; }                   /* right */
    if (g_tab == TAB_TELEPORT) return 1;                             /* no line to type into */
    unsigned c = (unsigned) ch;
    if (c >= 0x400 && c <= 0x4ff && !names_are_cyrillic()) {
        unsigned l = latin_at(code);
        int upper = (c >= 0x400 && c <= 0x42f);                          /* U+0400..U+042F */
        c = (l >= 'a' && l <= 'z' && upper) ? l - 32 : l;
    }
    if (!c && ch == 0) {
        /* no character came with the key (a synthetic one): the place, as a US board has it */
        int shift = (mods >> 17) & 1;
        if (code >= 4 && code <= 29) c = (unsigned) ((shift ? 'A' : 'a') + (code - 4));
        else if (code >= 30 && code <= 38) c = (unsigned) ('1' + (code - 30));
        else if (code == 39) c = '0';
        else if (code == 44) c = ' ';
    }
    char t[5];
    if (c >= 0x20 && c != 0x7f && utf8_put(c, t)) shim_spawner_text(t);
    return 1;
}

/* ------------------------------------------------------------ the give */
/*
 * Giving, always on the frame thread between frames: a tap arrives on the
 * touch thread and only asks (g_tap_list, g_tap_row); Return from a keyboard is already
 * delivered between frames. The thing goes to the body chosen on the card's
 * switch (whom it is for, below) - the first player while there is no other;
 * an enemy still comes beside the first player.
 *
 *   item     AddCollectible 0x500588(player, id, charge, firstTime, slot, varData)
 *            - firstTime, so a health up or a pickup on pickup does what it does
 *   trinket  AddTrinket 0x5039f8(player, id, firstTime) when a slot is free;
 *            with every slot taken it is SPAWNED on the floor beside the player
 *            (Game::Spawn 0x88fcbc, type 5 variant 350) and the game's own
 *            pickup swaps it with the one held - DropTrinket (0x508d90) carries
 *            special cases of its own and is not ours to imitate
 *   card     AddCard 0x515170(player, card)
 */
#define GAME_PTR           0xac3b90
#define GAME_PLAYERS       0x25c40
#define GAME_ROOM          0x21550
#define FN_PLAYER_AT       0x4da574
#define FN_ADD_COLLECTIBLE 0x500588
#define FN_ADD_TRINKET     0x5039f8
#define FN_ADD_CARD        0x515170
#define FN_TRINKET_SLOTS   0x508b9c
#define FN_SPAWN           0x88fcbc
#define FN_FREE_SPOT       0x59de50
#define FN_RANDOM          0x7f1cc0
#define FN_MAKE_CHAMPION   0x6825bc         /* (npc, seed, colour 0..25, again): +0xd0f 1, +0xd10 colour */
#define NPC_CHAMPION       0xd0f
#define NPC_CHAMPION_COLOUR 0xd10
#define NPC_HP             0x354
#define NPC_MAX_HP         0x358

/* what the next enemy give is: a champion colour, or a boss colour's subtype; -1 neither */
static int g_give_champion = -1, g_give_subtype = -1;
#define ZERO_VECTOR        0x95e7a0
#define PLAYER_TRINKETS    0x1ab0
#define PLAYER_POSITION    0x310

unsigned long shim_image_base(void);
int shim_report_active(void);
void report_line(const char *kind, const char *text);

typedef struct { float x, y; } vec2_t;

/*
 * A RUN THAT USED THE SPAWNER UNLOCKS NOTHING (the owner, 23.09.2026) - the way
 * a run on a special seed unlocks nothing, with the crossed trophy on the HUD.
 * And a challenge played with the spawner does not count either (the owner,
 * 30.09.2026, plan item A2).
 *
 * The game decides "no achievements for this run" in one place, 0x88b5b0(game)
 * (read in full, see the handoff notes, the read-only section): the seed's fixed
 * byte, a blocking special seed, the platform's own say 0x672828(manager, 0),
 * or the Rerun byte Game+0x1cf43c. The spawner used to raise the Rerun byte -
 * a byte with an owner of its own. Now it answers the platform's question
 * instead (loader.c platform_forbids_ours: 0x672828 is `b 0x8dab58`, the
 * trampoline comes here), which on iOS is a stub saying "no" and which the
 * game asks for exactly this. Everything that refuses a forbidden run then
 * refuses a marked one by itself - the fourteen callers of 0x88b5b0:
 *
 *   0x88b2b8 new run, 0x88bd94 continue, 0x88c064 rerun, 0x88c390 the start
 *            without a name: the persistent data read-only (a continued marked
 *            run is not refused, only its unlocks are)
 *   0x6edca8 the HUD's crossed trophy
 *   0x672b78 / 0x672ce0  the win and the loss streak
 *   0x5a0bf8 / 0x5a0c50 / 0x5a0ca8  the victory lap achievements 321, 360, 337
 *   0x69bac0 / 0x69bbf8  the greed donation machine takes itself away
 *   0x5b49e4 (0x5b49c4, called from 0x5b45a4)  a timed in-run bonus, skipped
 *   0x82f758 (0x82f5c4)  an entity count toward achievement 387, skipped
 *
 * and the challenge reward 0x894d14 asks 0x672828 directly (0x894d60) before
 * it lifts read-only to write the challenge done - a marked challenge is not
 * written. The daily run's end 0x88d310 lifts read-only without asking anybody,
 * but the daily is unreachable in the port (menu.c daily_sign_off).
 *
 * WHICH RUN IS MARKED. A run is its save slot (Manager+0x204c8, written by the
 * slot load 0x672990), its start seed (Game+0x25d44, the seed object at
 * +0x25d40 with the seed at +4) and its challenge (Game+0x1cf438). A new random
 * run gets a new seed and falls out of the list by itself; a continued one
 * keeps its seed and is found again. The list lives in memory and in
 * <saves>/port-spawner-runs.txt, a few runs a slot, written through the safe
 * writer (shim.c) the moment a run is marked. A refused write keeps the mark in
 * memory for the session.
 *
 * A VICTORY LAP carries the mark on. The game's own forbidden runs do: a lap
 * (0x88c604) counts Game+0x1cf700 up and draws a new seed (0x572410 writes the
 * seed only, +4), while the seed's fixed byte and the Rerun byte stay. So a lap
 * that follows a marked run in the same slot and challenge is marked too.
 *
 * FROM THE FIRST SPAWN, AND NOTHING BACK (plan item A3): what was unlocked before
 * the first thing was taken is kept; the persistent data are made read-only
 * right away as well (0x7027a0), as the run start would have done.
 *
 * `spawnerlock=0` in port-touch.ini (CONSOLE TROPHIES on) lifts the rule; a run
 * marked stays marked.
 */
#define MANAGER_PTR_SP     0xac2428
#define MGR_PERSISTENT     0x204d0
#define MGR_SAVE_SLOT      0x204c8          /* int: the slot the save came from (0x672990) */
#define GAME_SEED          0x25d44          /* the run's start seed */
#define GAME_CHALLENGE     0x1cf438
#define GAME_LAPS          0x1cf700         /* victory laps, counted up by 0x88c604 */
#define GAME_FRAME_SP      0x1af34c
#define FN_SET_READ_ONLY   0x7027a0         /* PersistentGameData::SetReadOnly(data, on) */
#define PGD_READ_ONLY      1
#define PGD_ACHIEVEMENTS   0x28
#define PGD_COLLECTION     0xa68
#define ACHIEVEMENTS_N     638
#define COLLECTION_TOP     0x2dc            /* the collection writer's own bound, 0x7025b4 */
#define CHALLENGE_TOP      0x2d             /* 0x702c1c */

const char *shim_saves_dir(void);
FILE *shim_safe_fopen(const char *path, const char *mode);
int shim_safe_fclose(FILE *f);

static int g_lock = 1;
static int g_lock_from;                      /* 0 the default, 1 port-touch.ini, 2 Tweaks */
static int g_frames_seen;                    /* set on the first frame: later calls are Tweaks */
void shim_spawner_lock(int on) {
    g_lock = on != 0;
    g_lock_from = g_frames_seen ? 2 : 1;     /* before the first frame only the ini is read */
#if ISAAC_DIAG
    if (g_frames_seen && shim_report_active()) {
        char line[80];
        snprintf(line, sizeof(line), "lock %d (changed in Tweaks)", g_lock);
        report_line("spawner", line);
    }
#endif
}
int shim_spawner_lock_get(void) { return g_lock; }

/* ------------------------------------------------------------ the run mark */
#define MARK_SLOTS     4                     /* slots 1..3, and 0 for anything else */
#define MARKS_PER_SLOT 8

typedef struct {
    unsigned seed;
    int challenge;
    unsigned char origin;                    /* 0 read from the file, 1 marked now, 2 a victory lap */
} run_mark_t;

typedef struct {
    run_mark_t m[MARK_SLOTS][MARKS_PER_SLOT];    /* oldest first */
    int n[MARK_SLOTS];
} run_marks_t;

static run_marks_t g_runs;
static int g_runs_loaded;                    /* the file was read (or found missing) */
static int g_runs_unwritten;                 /* a mark that only memory holds */

static int slot_index(int slot) { return slot > 0 && slot < MARK_SLOTS ? slot : 0; }

static int marks_find(const run_marks_t *t, int slot, unsigned seed, int challenge) {
    int s = slot_index(slot);
    for (int i = 0; i < t->n[s]; i++)
        if (t->m[s][i].seed == seed && t->m[s][i].challenge == challenge) return i;
    return -1;
}

/* answers 1 when the run was not in the list yet */
static int marks_add(run_marks_t *t, int slot, unsigned seed, int challenge, int origin) {
    if (marks_find(t, slot, seed, challenge) >= 0) return 0;
    int s = slot_index(slot);
    if (t->n[s] == MARKS_PER_SLOT) {
        memmove(&t->m[s][0], &t->m[s][1], sizeof(run_mark_t) * (MARKS_PER_SLOT - 1));
        t->n[s]--;
    }
    run_mark_t *m = &t->m[s][t->n[s]++];
    m->seed = seed;
    m->challenge = challenge;
    m->origin = (unsigned char) origin;
    return 1;
}

static int marks_write(const run_marks_t *t, const char *path) {
    FILE *f = shim_safe_fopen(path, "w");
    if (!f) return -1;
    fprintf(f, "# runs that took something from the item spawner: slot seed challenge\n");
    for (int s = 0; s < MARK_SLOTS; s++)
        for (int i = 0; i < t->n[s]; i++)
            fprintf(f, "%d %u %d\n", s, t->m[s][i].seed, t->m[s][i].challenge);
    return shim_safe_fclose(f) == 0 ? 0 : -1;
}

/* answers how many runs were read, -1 when there is no file */
static int marks_read(run_marks_t *t, const char *path) {
    FILE *f = fopen(path, "r");
    if (!f) return -1;
    char line[96];
    int n = 0;
    while (fgets(line, sizeof(line), f)) {
        int slot, challenge;
        unsigned seed;
        if (line[0] == '#' || sscanf(line, "%d %u %d", &slot, &seed, &challenge) != 3) continue;
        if (marks_add(t, slot, seed, challenge, 0)) n++;
    }
    fclose(f);
    return n;
}

static int runs_path(char *out, size_t n, const char *leaf) {
    const char *saves = shim_saves_dir();
    if (!saves || !*saves) return 0;
    snprintf(out, n, "%s/%s", saves, leaf);
    return 1;
}

/* the file, once, as soon as the saves folder is known */
static void runs_load(void) {
    char path[600];
    if (g_runs_loaded || !runs_path(path, sizeof(path), "port-spawner-runs.txt")) return;
    g_runs_loaded = 1;
    int n = marks_read(&g_runs, path);
#if ISAAC_DIAG
    if (shim_report_active()) {
        char line[120];
        if (n < 0) snprintf(line, sizeof(line), "run mark: no port-spawner-runs.txt yet");
        else snprintf(line, sizeof(line), "run mark: %d marked run(s) read from port-spawner-runs.txt", n);
        report_line("spawner", line);
    }
#else
    (void) n;
#endif
}

static void runs_save(void) {
    char path[600];
    int ok = runs_path(path, sizeof(path), "port-spawner-runs.txt") && marks_write(&g_runs, path) == 0;
    g_runs_unwritten = !ok;
#if ISAAC_DIAG
    if (!ok && shim_report_active())
        report_line("spawner", "run mark: port-spawner-runs.txt NOT written - the mark is kept in memory "
                               "for this session");
#endif
}

typedef struct { int ok; int slot; unsigned seed; int challenge; int laps; } run_key_t;

static run_key_t run_key(unsigned long b, unsigned long manager) {
    run_key_t k = { 0, 0, 0, 0, 0 };
    unsigned long game = *(const unsigned long *) (b + GAME_PTR);
    if (!manager) manager = *(const unsigned long *) (b + MANAGER_PTR_SP);
    if (!game || !manager) return k;
    k.ok = 1;
    k.slot = *(const int *) (manager + MGR_SAVE_SLOT);
    k.seed = *(const unsigned *) (game + GAME_SEED);
    k.challenge = *(const int *) (game + GAME_CHALLENGE);
    k.laps = *(const int *) (game + GAME_LAPS);
    return k;
}

/* the run last asked about, for the victory lap and for the report */
static run_key_t g_seen;
static int g_seen_marked = -1;

#if ISAAC_DIAG
static void mark_said(const run_key_t *k, int marked, int origin) {
    static int said;
    if (said >= 24 || !shim_report_active()) return;
    said++;
    static const char *const FROM[] = { "from file", "now", "a victory lap of a marked run" };
    char line[160];
    if (marked)
        snprintf(line, sizeof(line), "run mark: slot %d seed %u challenge %d lap %d -> marked (%s)",
                 k->slot, k->seed, k->challenge, k->laps, FROM[origin < 3 ? origin : 0]);
    else
        snprintf(line, sizeof(line), "run mark: slot %d seed %u challenge %d lap %d -> not marked",
                 k->slot, k->seed, k->challenge, k->laps);
    report_line("spawner", line);
}
#endif

/* is this run marked; the one place a victory lap inherits its run's mark */
static int run_marked(unsigned long b, unsigned long manager) {
    run_key_t k = run_key(b, manager);
    if (!k.ok) return 0;
    runs_load();
    int s = slot_index(k.slot);
    int i = marks_find(&g_runs, k.slot, k.seed, k.challenge);
    if (i < 0 && g_seen_marked == 1 && g_seen.ok && g_seen.slot == k.slot && g_seen.challenge == k.challenge
        && g_seen.seed != k.seed && k.laps == g_seen.laps + 1) {
        marks_add(&g_runs, k.slot, k.seed, k.challenge, 2);
        runs_save();
        i = marks_find(&g_runs, k.slot, k.seed, k.challenge);
    }
    int marked = i >= 0;
#if ISAAC_DIAG
    if (marked != g_seen_marked || k.seed != g_seen.seed || k.slot != g_seen.slot)
        if (marked || g_seen_marked == 1) mark_said(&k, marked, marked ? g_runs.m[s][i].origin : 0);
#else
    (void) s;
#endif
    g_seen = k;
    g_seen_marked = marked;
    return marked;
}

/*
 * THE GAME ASKS HERE (0x672828 -> 0x8dab58 -> this): "does the platform count
 * this run out". what == 0 is the run question; 1, asked by the daily menu, is
 * something else and keeps the stub's answer. Every frame through the HUD, so
 * it stays a lookup in a short list.
 */
int shim_run_forbidden_asked(unsigned long manager, int what) {
    if (what != 0) return 0;
    unsigned long b = shim_image_base();
    return b && run_marked(b, manager) ? 1 : 0;
}

int shim_run_marked_now(void) {
    unsigned long b = shim_image_base();
    return b && run_marked(b, 0);
}

static void lock_the_run(unsigned long b, unsigned long game) {
    (void) game;
    if (!g_lock) return;
    unsigned long manager = *(const unsigned long *) (b + MANAGER_PTR_SP);
    run_key_t k = run_key(b, manager);
    if (!k.ok) return;
    runs_load();
    int fresh = marks_add(&g_runs, k.slot, k.seed, k.challenge, 1);
    if (fresh || g_runs_unwritten) runs_save();
    unsigned char *data = (unsigned char *) (manager + MGR_PERSISTENT);
    if (!data[PGD_READ_ONLY]) ((void (*)(unsigned long, int)) (b + FN_SET_READ_ONLY))(manager + MGR_PERSISTENT, 1);
    if (fresh && shim_report_active()) report_line("spawner", "the run is marked: no unlocks from here on");
}

/* for the STATS and TELEPORT tabs (spawner_stats.c, spawner_teleport.c): the same mark, first */
void shim_spawner_mark_run(void) {
    unsigned long b = shim_image_base();
    unsigned long game = b ? *(const unsigned long *) (b + GAME_PTR) : 0;
    if (game) lock_the_run(b, game);
}

/* the run as the mark keys it: slot, start seed, challenge */
int shim_spawner_run_key(int *slot, unsigned *seed, int *challenge) {
    unsigned long b = shim_image_base();
    run_key_t k = b ? run_key(b, 0) : (run_key_t) { 0, 0, 0, 0, 0 };
    if (!k.ok) return 0;
    if (slot) *slot = k.slot;
    if (seed) *seed = k.seed;
    if (challenge) *challenge = k.challenge;
    return 1;
}

/* ------------------------------------------------------------ the final bosses */
/*
 * A FINAL BOSS FROM THE SPAWNER ENDS CLEANLY (the owner, 19th pass, 03.10.2026:
 * The Beast killed outside its arena left the screen white until a teleport).
 * Measured in the image (the research notes of 03.10.2026, spawner 19):
 *
 * The ending itself is never the boss's decision. Game::End 0x88d310 is called
 * by the player's death, by the room's clear 0x59e6fc (the Beast's ending 14
 * only when the room's config is type 16 on stage 35 - its arena) and by the
 * big chest and the trophy, which only that same clear spawns. Mom, Mom's
 * Heart, It Lives, Satan, Isaac, ???, the Lamb, Mega Satan, Hush, Delirium,
 * Mother and Ultra Greed killed in an ordinary room get an ordinary clear.
 *
 * What a boss DOES start by itself is the boss overlay, Game+0x27390, the
 * player of data/bossoverlays.xml: +0 playing, +4 the entry; 0x76b83c(ov, id)
 * starts one, its frame 0x76b6ac ends an entry that is not "persist" when its
 * animation is over, and a room change resets it (0x88d904 -> 0x76b670) -
 * which is why a teleport helped. Three places in the whole image start it:
 *
 *   The Beast's death (0x745040, state 0x12, "Shoot")  entry 4 Whiteout2,
 *       persist: white for good, the arena's clear ends the run under it
 *   Dogma's death (0x628f20, "Sound")                  entry 2, the static
 *   Dogma's death (0x628f8c, "Shoot")                  entry 3 "Shutdown", after
 *       0x56a560(players, 0) takes every player's controls (+0x400); the
 *       entry's "End" event shows cutscene 25 and moves to room -10, the
 *       Beast's fight (0x76b71c..0x76b768)
 *
 * The persist flag and the "End" check are read by the entry's number on every
 * frame. So for a Beast or a Dogma this spawner made, and only while it lies in
 * its death state, the entry it has just started is relabelled 1 - the game's
 * own Whiteout, not persistent and with no "End" of its own: the animation
 * already playing (the white, the TV going off) plays out and the overlay goes
 * away by itself. Dogma's players get their controls back by the game's own
 * call, 0x56a560(players, 1), once the overlay is gone. A Beast or a Dogma of
 * the floor itself is not ours and keeps its ending.
 */
#define GAME_BOSS_OVERLAY  0x27390
#define OVERLAY_PLAYING    0
#define OVERLAY_ENTRY      4
#define ENTRY_WHITEOUT     1                /* "Fade", not persistent, no End */
#define ENTRY_SHUTDOWN     3                /* Dogma's TV: End -> cutscene 25, room -10 */
#define ENTRY_WHITEOUT_HELD 4               /* the Beast's: persistent */
#define FN_CONTROLS        0x56a560         /* (players, on): every player's +0x400 */
#define MGR_SFX            0x55ea0          /* the manager's sound effects (0x5af81c plays through it) */
#define MGR_MUSIC          0x55a60          /* and its music (0x628f30) */
#define SFX_LEVEL          0x1c
#define SFX_SPEED          0x20
#define FN_SFX_LEVEL_BACK  0x7f11ec         /* (sfx): level 1.0, step 0.005 */
#define ENT_TYPE_SP        0x38
#define ENT_VARIANT_SP     0x3c
#define ENT_EXISTS_SP      0x1c2
#define ENT_STATE_SP       0xb70
#define STATE_DEATH        0x12
#define EL_ARRAY           0x78             /* the room's entity list (eid_craft.c) */
#define EL_COUNT           0x84
#define TYPE_DOGMA         950
#define TYPE_BEAST         951
#define FINALS             8

unsigned long shim_eid_list_now(void);
int shim_trace_readable(const void *p, unsigned long n);

static struct { unsigned long e; int type; } g_finals[FINALS];
static int g_controls_owed;                 /* our Dogma took the players' controls */

static void finals_remember(unsigned long e, int type, int variant) {
    if (!e || !(type == TYPE_DOGMA || (type == TYPE_BEAST && variant == 0))) return;
    int at = 0;
    for (int i = 0; i < FINALS; i++) if (!g_finals[i].e) { at = i; break; }
    if (g_finals[at].e) {                   /* full: the oldest goes */
        memmove(&g_finals[0], &g_finals[1], sizeof(g_finals[0]) * (FINALS - 1));
        at = FINALS - 1;
    }
    g_finals[at].e = e;
    g_finals[at].type = type;
}

/* one of ours in the room's list right now, lying in its death state */
static int finals_dying(int type) {
    unsigned long list = shim_eid_list_now();
    if (!list || !shim_trace_readable((const void *) (list + EL_ARRAY), 16)) return 0;
    const unsigned long *items = *(const unsigned long *const *) (list + EL_ARRAY);
    unsigned n = *(const unsigned *) (list + EL_COUNT);
    if (!items || n > 4096 || !shim_trace_readable(items, (unsigned long) n * 8)) return 0;
    int dying = 0;
    for (int f = 0; f < FINALS; f++) {
        unsigned long e = g_finals[f].e;
        if (!e) continue;
        int here = 0;
        for (unsigned i = 0; i < n && !here; i++) here = items[i] == e;
        if (!here || !shim_trace_readable((const void *) e, ENT_STATE_SP + 4)
            || *(const int *) (e + ENT_TYPE_SP) != g_finals[f].type) {
            g_finals[f].e = 0;              /* gone (a list of another room is not ours either) */
            continue;
        }
        if (g_finals[f].type == type && *(const int *) (e + ENT_STATE_SP) == STATE_DEATH) dying = 1;
    }
    return dying;
}

static void finals_between_frames(void) {
    int any = g_controls_owed;
    for (int f = 0; f < FINALS && !any; f++) any = g_finals[f].e != 0;
    if (!any) return;
    unsigned long b = shim_image_base();
    unsigned long game = b ? *(const unsigned long *) (b + GAME_PTR) : 0;
    if (!game) { memset(g_finals, 0, sizeof(g_finals)); g_controls_owed = 0; return; }
    int *ov = (int *) (game + GAME_BOSS_OVERLAY);
    if (ov[OVERLAY_PLAYING / 4]) {
        int entry = ov[OVERLAY_ENTRY / 4];
        int type = entry == ENTRY_WHITEOUT_HELD ? TYPE_BEAST : entry == ENTRY_SHUTDOWN ? TYPE_DOGMA : 0;
        if (type && finals_dying(type)) {
            ov[OVERLAY_ENTRY / 4] = ENTRY_WHITEOUT;
            if (type == TYPE_DOGMA) g_controls_owed = 1;
#if ISAAC_DIAG
            if (shim_report_active())
                report_line("spawner", type == TYPE_BEAST
                    ? "final boss: our Beast died - its held whiteout (overlay 4) plays as the passing one (1)"
                    : "final boss: our Dogma died - its TV shutdown (overlay 3) plays out without the move "
                      "to the Beast and cutscene 25");
#endif
        }
    } else {
        if (g_controls_owed) {
            g_controls_owed = 0;
            ((void (*)(unsigned long, int)) (b + FN_CONTROLS))(game + GAME_PLAYERS, 1);
            /*
             * The sound too. Dogma's "Shoot" (0x628fc4 -> 0x7f11a8) sends the sound
             * effects' level (SFX manager +0x1c) to -10 at 0.005 a frame - the TV
             * going silent; the game brings it back only in entry 3's "End"
             * (0x76b744 -> 0x7f11ec: +0x1c = 1.0, +0x20 = 0.005), with cutscene 25 and
             * the move to -10, or when a room change resets the overlay (0x76b670 ->
             * 0x7f11ec). Our Dogma's entry ends without that End, so the same call
             * of the game is made here (the 23rd pass: all sound gone after Dogma).
             */
            unsigned long manager = *(const unsigned long *) (b + MANAGER_PTR_SP);
            if (manager) {
#if ISAAC_DIAG
                const float *sfx = (const float *) (manager + MGR_SFX);
                float level_was = sfx[SFX_LEVEL / 4], speed_was = sfx[SFX_SPEED / 4];
#endif
                ((void (*)(unsigned long)) (b + FN_SFX_LEVEL_BACK))(manager + MGR_SFX);
#if ISAAC_DIAG
                if (shim_report_active()) {
                    char line[140];
                    snprintf(line, sizeof(line), "final boss: sound effects' level %.2f (step %.3f) -> %.2f (step %.3f)",
                             (double) level_was, (double) speed_was, (double) sfx[SFX_LEVEL / 4],
                             (double) sfx[SFX_SPEED / 4]);
                    report_line("spawner", line);
                }
#endif
            }
#if ISAAC_DIAG
            if (shim_report_active()) report_line("spawner", "final boss: the overlay is over - controls back");
#endif
        }
        finals_dying(0);                    /* only prunes the gone */
    }
}

/* ------------------------------------------------------------ whom it is for */
/*
 * THE PLAYER SWITCH OF ITEMS AND STATS (the owner, 20th pass, 05.10.2026: in
 * co-op the items went to player 1 alone; "the same switch as in stats", shown
 * only when there really is more than one). The bodies offered are the STATS
 * tab's (spawner_stats.c): every player of the run's list but the co-op babies
 * - so Esau, T.Forgotten's soul and T.Lazarus' other form with Birthright are
 * bodies of their own, each holding its own things, and are named
 * "PLAYER 1: Jacob", "PLAYER 1: Esau" (shim_stats_player_number). One choice
 * serves both tabs. What the game does with a give is the game's: AddCollectible
 * itself hands a soul's passive item to T.Forgotten (0x500608..0x500624: type
 * 40 with a twin, the item not an active one -> the twin), as when the soul
 * picks it up. The Forgotten's own soul and T.Lazarus' form without Birthright
 * (+0x2588) are not in the list and not bodies one plays at the same time:
 * the one in the list takes it, as a pickup would.
 */
#define PLAYERS_SHOWN 8
#define PLAYER_TWIN_SP  0x2580               /* the report only: whether the twin got it */
#define FN_HAS_ITEM_SP  0x4eeb74             /* (player, id, ignore modifiers) */

int shim_stats_players(unsigned long *out, int max);
int shim_stats_player_type(unsigned long p);
int shim_stats_player_number(unsigned long p);

static int g_who;                            /* which of the bodies offered, in the switch's order */
static unsigned long g_who_p;                /* the body itself, kept through a change of the line-up */

/* the bodies in the switch's order: by the owner's number, then as the list has them */
static int who_list(unsigned long *p) {
    int n = shim_stats_players(p, PLAYERS_SHOWN), num[PLAYERS_SHOWN];
    for (int i = 0; i < n; i++) num[i] = shim_stats_player_number(p[i]);
    for (int i = 1; i < n; i++)
        for (int k = i; k > 0 && num[k - 1] > num[k]; k--) {
            unsigned long t = p[k]; p[k] = p[k - 1]; p[k - 1] = t;
            int u = num[k]; num[k] = num[k - 1]; num[k - 1] = u;
        }
    return n;
}

static unsigned long who_player(void) {
    unsigned long p[PLAYERS_SHOWN];
    int n = who_list(p);
    if (!n) { g_who_p = 0; return 0; }
    for (int i = 0; i < n; i++)
        if (g_who_p && p[i] == g_who_p) { g_who = i; return p[i]; }
    if (g_who >= n) g_who = n - 1;
    if (g_who < 0) g_who = 0;
    g_who_p = p[g_who];
    return g_who_p;
}

/*
 * WHAT THE PLAYER HOLDS (the owner, 21.10.2026): every collectible row the
 * player shown holds carries the count on its right and a cross that takes
 * every copy away. Read between frames, once a frame while the card shows
 * ITEMS: the game's own count, GetCollectibleNum(player, id, true) (0x4ff70c) -
 * the owned array at +0x1ab8 for a passive, the active slots for an active -
 * asked only for the ids the array or a slot names.
 */
#define FN_COLLECTIBLE_NUM    0x4ff70c       /* (player, id, only true) */
#define FN_REMOVE_COLLECTIBLE 0x5051e0       /* (player, id, ignore modifiers, active slot, from the form) */
#define PLAYER_OWNED_SP       0x1ab8         /* int per id: begin, end at +0x1ac0 */
#define PLAYER_ACTIVE_SP      0x1964         /* the active slots' ids, 0x1c apart */
#define ACTIVE_STEP_SP        0x1c
#define ACTIVE_SLOTS_SP       4
#define HELD_MAX              4096

static unsigned short g_held[HELD_MAX];
static unsigned g_held_sig;

static int held_of(unsigned id) { return id < HELD_MAX ? g_held[id] : 0; }

static int held_num(unsigned long b, unsigned long p, unsigned id) {
    return ((int (*)(unsigned long, int, int)) (b + FN_COLLECTIBLE_NUM))(p, (int) id, 1);
}

/* the counts for the player shown; answers whether they changed */
static int held_refresh(void) {
    unsigned long b = shim_image_base();
    unsigned long p = b ? who_player() : 0;
    unsigned sig = 2166136261u;
    memset(g_held, 0, sizeof(g_held));
    if (p && shim_trace_readable((const void *) (p + PLAYER_OWNED_SP), 16)) {
        const int *owned = *(const int *const *) (p + PLAYER_OWNED_SP);
        const int *end = *(const int *const *) (p + PLAYER_OWNED_SP + 8);
        long n = owned && end > owned ? (long) (end - owned) : 0;
        if (n > HELD_MAX) n = HELD_MAX;
        if (n > 0 && !shim_trace_readable(owned, (unsigned long) n * 4)) n = 0;
        for (long id = 1; id < n; id++) {
            if (owned[id] <= 0) continue;
            int k = held_num(b, p, (unsigned) id);
            g_held[id] = (unsigned short) (k > 0 ? (k < 65535 ? k : 65535) : 0);
        }
        for (int k = 0; k < ACTIVE_SLOTS_SP; k++) {
            int id = *(const int *) (p + PLAYER_ACTIVE_SP + k * ACTIVE_STEP_SP);
            if (id <= 0 || id >= HELD_MAX || g_held[id]) continue;
            int c = held_num(b, p, (unsigned) id);
            g_held[id] = (unsigned short) (c > 0 ? (c < 65535 ? c : 65535) : 0);
        }
        for (int id = 1; id < HELD_MAX; id++)
            if (g_held[id]) sig = (sig ^ (unsigned) id ^ ((unsigned) g_held[id] << 16)) * 16777619u;
    }
    int moved = sig != g_held_sig;
    g_held_sig = sig;
    return moved;
}

/* every copy of a collectible out of the player shown, as the game takes one (one a call) */
static int held_take(unsigned id) {
    unsigned long b = shim_image_base();
    unsigned long p = b ? who_player() : 0;
    if (!p || !id || id >= HELD_MAX) return 0;
    int left = held_num(b, p, id);
    if (left <= 0) return 0;
    shim_spawner_mark_run();                 /* EvaluateItems runs inside: the run is marked first */
    for (int i = 0; i < 64 && left > 0; i++) {
        /* an active one goes from its own slot: RemoveCollectible skips an active item asked with slot -1 */
        int slot = -1;
        for (int k = 0; k < ACTIVE_SLOTS_SP; k++)
            if (*(const int *) (p + PLAYER_ACTIVE_SP + k * ACTIVE_STEP_SP) == (int) id) { slot = k; break; }
        ((void (*)(unsigned long, int, int, int, int)) (b + FN_REMOVE_COLLECTIBLE))(p, (int) id, 0, slot, 1);
        int now = held_num(b, p, id);
        if (now >= left) break;              /* the game would not take it: no endless loop */
        left = now;
    }
    if (shim_report_active()) {
        char line[96];
        snprintf(line, sizeof(line), "spawner: collectible %u taken from the player, %d left", id, left);
        report_line("spawner", line);
    }
    return 1;
}

/* the switch is there only with more than one body to choose from */
static int who_shown(void) {
    unsigned long p[PLAYERS_SHOWN];
    return shim_stats_players(p, PLAYERS_SHOWN) > 1;
}

static void who_step(int dir) {
    unsigned long p[PLAYERS_SHOWN];
    int n = who_list(p);
    if (n < 2 || !dir) return;
    who_player();
    g_who = (g_who + n + (dir > 0 ? 1 : -1)) % n;
    g_who_p = p[g_who];
}

/* ------------------------------------------------------------ bosses as their rooms lay them out */
/*
 * A BOSS OF SEVERAL ENTITIES COMES WHOLE (the owner, 20th pass and his answers
 * of 05.10.2026: Mom was only her foot; Larry Jr. came as a head that died at
 * once; Chub and its kin were a floating head with no animation). Measured in
 * the image (the research notes of 05.10.2026, spawner bosses and rooms):
 *
 * These bosses are several entities that their ROOM places, and their AI only
 * links what the room put there - nothing in the game spawns 19, 28, 45 or 918
 * except a room layout:
 *   Larry Jr. & co. (19.x), Turdlet (918)  each segment, on its first update,
 *       becomes the child of a same-type segment within 42 that has no child
 *       (0x5f7ad4, 0x712608): the chain is the room's row of cells;
 *   Chub & co. (28.x)  the first to update chains three free Chubs within 57.6
 *       (0x603534); it grows missing ones only for Delirium or an inherited
 *       +0x10c0 (0x6036fc) - alone it is a head without a body;
 *   Mom (45.0)  five 45.0 - see below.
 * And the fights a boss room always lays out with more than one entity (a scan
 * of every boss and miniboss over all its boss rooms, the same notes):
 *   Lokii (69.1) the twins, Gurglings (237.1) and Turdlings (237.2) the pair;
 *   Headless Horseman (82.0) with its head 83.0 (every Horseman room; 82's AI
 *       spawns no head); Singe (915.0) with its four balls 915.1 (every Singe
 *       room; its AI only tops them up to +0xbf0 + 4 over time, 0x6214b0).
 * Partners the AI makes itself are left to it (Mega Satan's hands, Mask of
 * Infamy's heart, Ultra Greed's doors); a part listed on its own (the head, the
 * heart, Ultra Pride's baby) comes alone, as its own entry.
 * The game's own way of bringing such a boss into a room that is not its own is
 * Greed mode's wave: the boss room's layout spawned at its own cells in the
 * current room (0x5af884 -> 0x5afc48). The spawner does the same, with the
 * layout tools/spawner_layouts.py takes from the boss rooms (spawner_layouts.h),
 * all parts in one go between frames and in the room's own order - Game::Spawn
 * puts an entity into the room's main list at once (0x763f0c -> +0x70, the list
 * FindByType reads), so the parts find each other on their first update exactly
 * as in their room. A layout cell outside this room's floor (a narrow or L room)
 * moves the whole group to the room's centre on the 40 grid; if it still does
 * not fit, one entity is spawned as before.
 *
 * MOM. Every Mom room (00.special rooms 1060..1064, 6030, all 1x1) places five
 * 45.0: in the cells inside the four doors and in the centre. Her AI 0x55d244
 * sorts them on its first update by a door table built for a 1x1 room
 * (0xb0fe78, filled by 0x55e848): (40,280) (320,120) (600,280) (320,440); within
 * 50 of one (squared 2500, 0x9460c8) is a door Mom - it turns to that door
 * (rotation k*90 - 90, +0x134) and stands in it with its own door picture. The
 * one near no door and without a parent becomes the foot 45.10 and adopts the
 * rest (FindByType, SetParent, +0xbf0). The PC game has the same table and code
 * (0xc5c698, 0x4d1510). In a room that is not 1x1 she can stand only in the
 * doors that ARE her table's points: a door elsewhere is no door to her code - a
 * 45.0 there gets no turn, no snap into the doorway, and the foot's collision
 * class (+0x1e0 = 5, 0x55db78) - and without a parent it would be a second
 * foot. So a door part is kept only where this room's door slot k (room +0xe70,
 * the grid index 0x5a2db8 gives the shape) stands exactly on her point k; in a
 * 1x1 room that is all four, as in her room.
 */
#include "spawner_layouts.h"

#define TYPE_MOM           45
#define ROOM_DESCRIPTOR    8
#define DESC_CONFIG        0x10
#define CONFIG_SHAPE       0x5c             /* RoomShape (0x59e1e0 reads it) */
#define ROOM_WIDTH         0x14             /* grid width (0x59d1d4) */
#define ROOM_DOOR_INDEX    0xe70            /* int[8]: the grid index of each door slot, -1 none (0x5a38c0) */
#define FN_ROOM_CLAMP      0x5c6148         /* (room, pos, 4 margins) -> the position kept inside the floor */
#define FN_ROOM_CENTRE     0x59e2ac
#define MOM_NEAR_SQ        2500.0f          /* 0x9460c8 */

static const float MOM_DOOR[4][2] = { { 40.0f, 280.0f }, { 320.0f, 120.0f }, { 600.0f, 280.0f }, { 320.0f, 440.0f } };

#if ISAAC_DIAG
static int room_shape(unsigned long room) {
    if (!room || !shim_trace_readable((const void *) (room + ROOM_DESCRIPTOR), 8)) return -1;
    unsigned long desc = *(const unsigned long *) (room + ROOM_DESCRIPTOR);
    if (!desc || !shim_trace_readable((const void *) (desc + DESC_CONFIG), 8)) return -1;
    unsigned long config = *(const unsigned long *) (desc + DESC_CONFIG);
    if (!config || !shim_trace_readable((const void *) (config + CONFIG_SHAPE), 4)) return -1;
    return *(const int *) (config + CONFIG_SHAPE);
}
#endif

/* the Mom door a cell belongs to by her own test, -1 the centre */
static int mom_door_of(const float *at) {
    for (int k = 0; k < 4; k++) {
        float dx = at[0] - MOM_DOOR[k][0], dy = at[1] - MOM_DOOR[k][1];
        if (dx * dx + dy * dy < MOM_NEAR_SQ) return k;
    }
    return -1;
}

/* does this room have door slot k exactly on her point k */
static int mom_door_here(unsigned long room, int k) {
    int w = *(const int *) (room + ROOM_WIDTH);
    int at = *(const int *) (room + ROOM_DOOR_INDEX + (unsigned long) k * 4);
    if (w <= 0 || at < 0) return 0;
    return 40.0f + 40.0f * (float) (at % w) == MOM_DOOR[k][0] && 120.0f + 40.0f * (float) (at / w) == MOM_DOOR[k][1];
}

static int room_holds_by(unsigned long b, unsigned long room, const float *at, float margin) {
    vec2_t in = ((vec2_t (*)(unsigned long, const float *, float, float, float, float)) (b + FN_ROOM_CLAMP))(
        room, at, margin, margin, margin, margin);
    return fabsf(in.x - at[0]) < 0.5f && fabsf(in.y - at[1]) < 0.5f;
}

static int room_holds(unsigned long b, unsigned long room, const float *at) {
    return room_holds_by(b, room, at, 0.0f);
}

/*
 * MOM MOVES THE PLAYERS. Whichever 45.0 becomes the foot - one of five or a
 * lone one - puts every player at (320,370), the next ones at a random 20 from
 * it (0x55daa0 -> 0x53b920), with no clamp: 0x53b920 writes the position as it
 * is (0x53baa4) and frees a spot only for some familiars (0x53bb94). Measured
 * where that point lies: on the floor of 1x1, IV, 1x2, IIV, 2x1, 2x2 and the
 * L rooms that keep the top-left quarter; in IH and IIH (floor rows 240..320,
 * the bottom wall's cells centred at y 360 - the rooms' own spawn rows, xml y
 * 2..4) it is 10 past the centre of the bottom wall, and outside that wall the
 * builder 0x5a8038 puts nothing (walls are a ring), so the wall pushes the
 * player out of the room; in LTL it is the missing quarter itself. The game
 * never has Mom there, and a lone 45.0 moves the players the same way.
 *
 * The owner's choice (05.10.2026, "we only patch the really open holes"): Mom
 * is spawned there as anywhere else, and in such a room - only there - every
 * player her start left off the floor is put back on it, the way the game
 * frees a spot after its own teleport (0x53bb94): the point the room's own
 * clamp keeps 20 inside the floor (0x5c6148), the free spot from it (0x59de50
 * with radius 0, as there), and the player moved there by the same teleport
 * her code used (0x53b920, familiars follow). Watched between frames for a
 * short while after the spawn, so it catches her start whenever it runs.
 */
#define MOM_PLAYERS_X      320.0f           /* 0x55daa0: 0x43a00000, 0x43b90000 */
#define MOM_PLAYERS_Y      370.0f
#define MOM_PLAYERS_ROOM   20.0f
#define MOM_WATCH_FRAMES   30
#define GAME_PLAYER_LIST   0x25c50          /* begin, end of the players (0x55dac4) */
#define FN_PLAYER_TELEPORT 0x53b920         /* (player, pos, 0, 0) */

static int mom_keeps_players_in(unsigned long b, unsigned long room) {
    const float at[2] = { MOM_PLAYERS_X, MOM_PLAYERS_Y };
    return room_holds_by(b, room, at, MOM_PLAYERS_ROOM);
}

static struct { int frames; unsigned long desc; } g_mom_watch;

static void mom_watch_players(unsigned long room) {
    g_mom_watch.frames = MOM_WATCH_FRAMES;
    g_mom_watch.desc = *(const unsigned long *) (room + ROOM_DESCRIPTOR);
}

static void mom_players_between_frames(void) {
    if (g_mom_watch.frames <= 0) return;
    g_mom_watch.frames--;
    unsigned long b = shim_image_base();
    unsigned long game = b ? *(const unsigned long *) (b + GAME_PTR) : 0;
    unsigned long room = game ? *(const unsigned long *) (game + GAME_ROOM) : 0;
    if (!room || *(const unsigned long *) (room + ROOM_DESCRIPTOR) != g_mom_watch.desc) {
        g_mom_watch.frames = 0;              /* another room: nothing of hers here */
        return;
    }
    const unsigned long *list = (const unsigned long *) (game + GAME_PLAYER_LIST);
    if (!list[0] || list[1] < list[0]) return;
    unsigned n = (unsigned) ((list[1] - list[0]) / 8);
    for (unsigned i = 0; i < n && i < 8; i++) {
        unsigned long player = ((unsigned long (*)(unsigned long, int)) (b + FN_PLAYER_AT))(game + GAME_PLAYERS,
                                                                                         (int) i);
        if (!player) continue;
        const float *pos = (const float *) (player + PLAYER_POSITION);
        if (room_holds(b, room, pos)) continue;
        vec2_t in = ((vec2_t (*)(unsigned long, const float *, float, float, float, float)) (b + FN_ROOM_CLAMP))(
            room, pos, MOM_PLAYERS_ROOM, MOM_PLAYERS_ROOM, MOM_PLAYERS_ROOM, MOM_PLAYERS_ROOM);
        float inside[2] = { in.x, in.y };
        vec2_t spot = ((vec2_t (*)(unsigned long, const float *, float, int, int)) (b + FN_FREE_SPOT))(
            room, inside, 0.0f, 1, 0);
        float to[2] = { spot.x, spot.y };
#if ISAAC_DIAG
        if (shim_report_active()) {
            char line[160];
            snprintf(line, sizeof(line), "Mom put player %u at (%.0f,%.0f), off this room's floor - "
                     "moved back to (%.0f,%.0f)", i, (double) pos[0], (double) pos[1], (double) to[0], (double) to[1]);
            report_line("spawner", line);
        }
#endif
        ((void (*)(unsigned long, const float *, int, int)) (b + FN_PLAYER_TELEPORT))(player, to, 0, 0);
    }
}

static unsigned long g_spawned[16];          /* what the last enemy give spawned */
static int g_spawned_n;

/*
 * Spawn entry id as its room lays it out; 0 when it is not such a boss or the
 * layout does not fit this room (the caller then spawns one entity). *main is
 * the part the rest hangs on: Mom's centre, else the first part.
 */
static int layout_spawn(unsigned long b, unsigned long game, unsigned long room, unsigned id, int subtype,
                        int seed, unsigned long *main) {
    int L = -1;
    for (int i = 0; i < SP_LAYOUTS; i++)
        if (SP_LAYOUT[i].type == SP_ENEMY[id].type && SP_LAYOUT[i].variant == SP_ENEMY[id].variant) L = i;
    if (L < 0) return 0;
    int mom = SP_ENEMY[id].type == TYPE_MOM;
    float at[16][2];
    int keep[16], n = SP_LAYOUT[L].count, kept = 0, doors = 0;
    if (n > 16) return 0;
    for (int k = 0; k < n; k++) {
        const sp_part_t *p = &SP_LAYOUT_PART[SP_LAYOUT[L].first + k];
        at[k][0] = 80.0f + 40.0f * (float) p->x;
        at[k][1] = 160.0f + 40.0f * (float) p->y;
        int door = mom ? mom_door_of(at[k]) : -1;
        keep[k] = door < 0 || mom_door_here(room, door);
        doors += door >= 0 && keep[k];
    }
    int fits = 1;
    for (int k = 0; k < n; k++) if (keep[k] && !room_holds(b, room, at[k])) fits = 0;
    if (!fits && !mom) {                     /* the whole group to the room's centre, on the grid */
        vec2_t c = ((vec2_t (*)(unsigned long)) (b + FN_ROOM_CENTRE))(room);
        float dx = 40.0f * roundf((c.x - 320.0f) / 40.0f), dy = 40.0f * roundf((c.y - 280.0f) / 40.0f);
        fits = 1;
        for (int k = 0; k < n; k++) {
            at[k][0] += dx;
            at[k][1] += dy;
            if (!room_holds(b, room, at[k])) fits = 0;
        }
    }
#if ISAAC_DIAG
    if (shim_report_active()) {
        char line[180];
        if (mom)
            snprintf(line, sizeof(line), "layout: Mom in room shape %d - %d door Moms her door table knows here, "
                     "centre %s", room_shape(room), doors, fits ? "fits" : "outside the floor - one 45.0 instead");
        else
            snprintf(line, sizeof(line), "layout: %d.%d as its room lays it out - %d parts, room shape %d, %s",
                     SP_ENEMY[id].type, SP_ENEMY[id].variant, n, room_shape(room),
                     fits ? "fits" : "does not fit - one entity instead");
        report_line("spawner", line);
    }
#endif
    (void) doors;
    if (!fits) return 0;
    unsigned long (*spawn)(unsigned long, int, int, const float *, const float *, unsigned long, int, int, int) =
        (unsigned long (*)(unsigned long, int, int, const float *, const float *, unsigned long, int, int, int))
            (b + FN_SPAWN);
    *main = 0;
    for (int k = 0; k < n; k++) {
        if (!keep[k]) continue;
        const sp_part_t *p = &SP_LAYOUT_PART[SP_LAYOUT[L].first + k];
        int own = p->type == SP_ENEMY[id].type && p->variant == SP_ENEMY[id].variant;
        int part_seed = kept == 0 ? seed : ((int (*)(void)) (b + FN_RANDOM))();
        unsigned long e = spawn(game, p->type, p->variant, at[k], (const float *) (b + ZERO_VECTOR), 0,
                                own ? subtype : p->subtype, part_seed, 0);
        if (e && own && (!*main || (mom && mom_door_of(at[k]) < 0))) *main = e;   /* not a partner */
        if (e && g_spawned_n < 16) g_spawned[g_spawned_n++] = e;
        kept++;
    }
    return kept;
}

/* ------------------------------------------------------------ Dogma's and Mother's arena, put back */
/*
 * THE ROOM COMES BACK (the owner, 05.10.2026: "why not just put the room back as
 * it was"). Both build their arena out of the room they appear in, and the game
 * never returns to that room (Dogma leaves through cutscene 25 into room -10,
 * Mother's room is the last of Corpse II). Measured:
 *
 *   Dogma 950.0 from the spawner has no spawner (+0x44 = 0), so its state 0
 *   takes the arena path 0x62c378: the TV 950.1 at the room's centre (its
 *   spawner Dogma, +0x44 = 950), all 8 doors replaced by walls (RemoveDoor
 *   0x5a918c), descriptor flags &~1 | 0x8004 (+0x50: the static backdrop and
 *   no walls), then the backdrop rebuilt (0x5a2110 room, 0x70de24 room+0x2200).
 *   The other arena path (state 2 -> 7, 0x62bc0c), which also swaps the room's
 *   config for "type 1 variant 1000" of the stage - NULL where the stage has
 *   none - is taken only when Dogma HAS a spawner (0x62aed4); the spawner passes
 *   none, and state 7 is written in one place only (0x62a924, from state 2).
 *   So the config of a room of ours is never swapped.
 *   Mother 912.0 (subtype 0, state 0, 0x4d5074): to the centre, all 8 doors
 *   replaced by walls, the descriptor's door mask cleared (+0x20, 0x7301c),
 *   players moved, two Corpse wall effects 1000.140 (no spawner), and parts of
 *   her own, 912.0 subtype 1, that are no enemies (+0xcc8 = 0, 0x4d4c1c) and
 *   outlive her (the 21st pass: the room stayed broken because they did - the
 *   fight is over when no part is an active enemy by the game's own test,
 *   vtable +0x90, the one the room counts for its clear). Her room gets its own
 *   music back by the game's 0x59a070, as Dogma's arena does.
 *
 * The spawner notes the room before the boss's first update: descriptor, its
 * config and grid index, the stage, flags, door mask, and the 1000.140 effects
 * already there. Then, between frames:
 *   - the player has left (TELEPORT, death...): the descriptor gets its flags
 *     and door mask back, if it is still the same room (same stage, same config,
 *     same grid index - a new floor reuses the descriptors); the next visit
 *     builds doors and backdrop from them, as every visit does;
 *   - in the room, once no Dogma part (but the TV) or Mother part is left and
 *     Dogma's overlay has ended: flags and door mask back, the backdrop rebuilt
 *     the way Dogma rebuilt it (0x5a2110, 0x70de24), the doors made again by the
 *     game's own door builder 0x5b080c (for each slot of the mask: the door
 *     0x59d370, its target from the descriptor +0x28, its look 0x726448 - the
 *     same routine the game runs to reopen a live room's doors, 0x5b3644), the
 *     TV and the new 1000.140 effects removed (Remove, vtable +0x58, as Dogma
 *     removes the living room's props).
 */
#define TYPE_MOTHER        912
#define VARIANT_TV         1
#define GAME_STAGE         0
#define DESC_GRID          0
#define DESC_DOORS         0x20             /* u64, a bit per door slot (0x5b0834) */
#define DESC_FLAGS         0x50
#define DOGMA_FLAGS        0x8004
#define FLAG_CLEAR         1
#define ROOM_DOOR          0xe30            /* GridEntityDoor *[8] */
#define ROOM_BACKDROP_OBJ  0x2200
#define FN_BACKDROP_TYPE   0x5a2110
#define FN_BACKDROP_INIT   0x70de24
#define FN_ROOM_DOORS      0x5b080c
#define ENT_SPAWNER_TYPE   0x44
#define VT_REMOVE          0x58
#define TYPE_EFFECT        1000
#define EFFECT_CORPSE_WALL 140
#define ARENA_HAD          32
#define ROOM_ENEMIES_A     0x1a58           /* the room's active enemy counts (0x5b1630) */
#define ROOM_ENEMIES_B     0x1a5c
#define ROOM_CLEAR_DELAY   0x1918           /* 10 while busy, counted down; clear at 1..8 (0x59e3f8) */
#define ENT_DEAD           0x1c3
#define ENT_SUBTYPE        0x40
#define VT_ACTIVE_ENEMY    0x90             /* NPC: 0x69aae4 */
#define FN_ROOM_MUSIC      0x59a070         /* (room): the room's own music, as Dogma's arena calls it (0x62c1c4) */

static struct {
    int on, dogma, mother;
    unsigned long game, desc, config;
    int stage, grid;
    unsigned flags;
    unsigned long long doors;
    int had_n, last_key;
    unsigned long main;
    unsigned long had[ARENA_HAD];
} g_arena;

static int arena_list(const unsigned long **items, unsigned *n) {
    unsigned long list = shim_eid_list_now();
    if (!list || !shim_trace_readable((const void *) (list + EL_ARRAY), 16)) return 0;
    *items = *(const unsigned long *const *) (list + EL_ARRAY);
    *n = *(const unsigned *) (list + EL_COUNT);
    return *items && *n <= 4096 && shim_trace_readable(*items, (unsigned long) *n * 8);
}

static void arena_remember(unsigned long game, unsigned long room, unsigned long e, int type, int variant) {
    if (!(type == TYPE_DOGMA && variant == 0) && !(type == TYPE_MOTHER && variant == 0)) return;
    if (!g_arena.on) {
        unsigned long desc = *(const unsigned long *) (room + ROOM_DESCRIPTOR);
        if (!desc || !shim_trace_readable((const void *) desc, DESC_FLAGS + 4)) return;
        g_arena.game = game;
        g_arena.desc = desc;
        g_arena.config = *(const unsigned long *) (desc + DESC_CONFIG);
        g_arena.grid = *(const int *) (desc + DESC_GRID);
        g_arena.stage = *(const int *) (game + GAME_STAGE);
        g_arena.flags = *(const unsigned *) (desc + DESC_FLAGS);
        g_arena.doors = *(const unsigned long long *) (desc + DESC_DOORS);
        g_arena.had_n = 0;
        const unsigned long *items;
        unsigned n;
        if (arena_list(&items, &n))
            for (unsigned i = 0; i < n && g_arena.had_n < ARENA_HAD; i++)
                if (shim_trace_readable((const void *) items[i], ENT_VARIANT_SP + 4)
                    && *(const int *) (items[i] + ENT_TYPE_SP) == TYPE_EFFECT
                    && *(const int *) (items[i] + ENT_VARIANT_SP) == EFFECT_CORPSE_WALL)
                    g_arena.had[g_arena.had_n++] = items[i];
        g_arena.on = 1;
#if ISAAC_DIAG
        if (shim_report_active()) {
            char line[140];
            snprintf(line, sizeof(line), "arena: room noted before %d.0 builds its arena - flags %#x, door mask %#llx",
                     type, g_arena.flags, g_arena.doors & 0xffULL);
            report_line("spawner", line);
        }
#endif
    }
    if (type == TYPE_DOGMA) g_arena.dogma = 1;
    else g_arena.mother = 1;
    g_arena.main = e;
    g_arena.last_key = -2;
}

/* the descriptor as it was: Dogma's flag bits, a CLEAR it took away, Mother's door mask */
static void arena_descriptor_back(unsigned long desc) {
    unsigned *flags = (unsigned *) (desc + DESC_FLAGS);
    *flags = (*flags & ~(unsigned) DOGMA_FLAGS) | (g_arena.flags & DOGMA_FLAGS) | (g_arena.flags & FLAG_CLEAR);
    unsigned long long *doors = (unsigned long long *) (desc + DESC_DOORS);
    *doors = (*doors & ~0xffULL) | (g_arena.doors & 0xffULL);
}

static void arena_between_frames(void) {
    if (!g_arena.on) return;
    unsigned long b = shim_image_base();
    unsigned long game = b ? *(const unsigned long *) (b + GAME_PTR) : 0;
    if (game != g_arena.game) {              /* another run: its rooms are not ours to touch */
        memset(&g_arena, 0, sizeof(g_arena));
        return;
    }
    unsigned long room = *(const unsigned long *) (game + GAME_ROOM);
    if (!room) return;
    unsigned long desc = *(const unsigned long *) (room + ROOM_DESCRIPTOR);
    if (desc != g_arena.desc) {              /* left the room */
        int same = shim_trace_readable((const void *) g_arena.desc, DESC_FLAGS + 4)
                   && *(const int *) (game + GAME_STAGE) == g_arena.stage
                   && *(const unsigned long *) (g_arena.desc + DESC_CONFIG) == g_arena.config
                   && *(const int *) (g_arena.desc + DESC_GRID) == g_arena.grid;
        if (same) arena_descriptor_back(g_arena.desc);
#if ISAAC_DIAG
        if (shim_report_active())
            report_line("spawner", same ? "arena: left the room - its descriptor (flags, door mask) put back"
                                        : "arena: left the room for another floor - nothing to put back");
#endif
        memset(&g_arena, 0, sizeof(g_arena));
        return;
    }
    const int *ov = (const int *) (game + GAME_BOSS_OVERLAY);
    if (ov[OVERLAY_PLAYING / 4]) return;     /* Dogma's TV is still going off */
    const unsigned long *items;
    unsigned n;
    if (!arena_list(&items, &n)) return;
    /*
     * The fight is over when the game itself says so: the room counts its
     * active enemies (+0x1a58, +0x1a5c - what Room::Update tests, 0x5b1630) and
     * holds its clear delay (+0x1918: 10 while anything counts, then counted
     * down; the clear comes at 1..8, 0x59e3f8) - the same moment the game
     * would open the doors of a cleared room. A boss's death sequence keeps
     * the room busy until the game is done with it (the 22nd pass: Mother
     * "evaporated" because a part-by-part test ended her fight at her last HP,
     * the phase change 0x4d49d8 -> Morph 912.10, and removed her).
     */
#if ISAAC_DIAG
    if (shim_report_active() && g_arena.main && shim_trace_readable((const void *) g_arena.main, ENT_STATE_SP + 4)) {
        unsigned long m = g_arena.main;
        int here = 0;
        for (unsigned i = 0; i < n && !here; i++) here = items[i] == m;
        int act = here && ((int (*)(unsigned long)) (*(const unsigned long *) (*(const unsigned long *) m
                                                                               + VT_ACTIVE_ENEMY)))(m);
        int key = here ? (*(const int *) (m + ENT_VARIANT_SP) * 4096 + *(const int *) (m + ENT_STATE_SP) * 8
                          + *(const unsigned char *) (m + ENT_DEAD) * 2 + act) : -1;
        if (key != g_arena.last_key) {
            g_arena.last_key = key;
            char line[200];
            if (here)
                snprintf(line, sizeof(line), "arena: our %d.%d now state %d, hp %.1f, dead %d, active enemy %d | "
                         "room counts %d+%d, clear delay %d", *(const int *) (m + ENT_TYPE_SP),
                         *(const int *) (m + ENT_VARIANT_SP), *(const int *) (m + ENT_STATE_SP),
                         (double) *(const float *) (m + NPC_HP), *(const unsigned char *) (m + ENT_DEAD), act,
                         *(const int *) (room + ROOM_ENEMIES_A), *(const int *) (room + ROOM_ENEMIES_B),
                         *(const int *) (room + ROOM_CLEAR_DELAY));
            else
                snprintf(line, sizeof(line), "arena: our boss left the room's list | room counts %d+%d, clear delay %d",
                         *(const int *) (room + ROOM_ENEMIES_A), *(const int *) (room + ROOM_ENEMIES_B),
                         *(const int *) (room + ROOM_CLEAR_DELAY));
            report_line("spawner", line);
        }
    }
#endif
    if (*(const int *) (room + ROOM_ENEMIES_A) + *(const int *) (room + ROOM_ENEMIES_B) > 0
        || *(const int *) (room + ROOM_CLEAR_DELAY) > 0)
        return;
    /* the boss is gone: the room as it was */
    unsigned flags_were = *(const unsigned *) (desc + DESC_FLAGS);
    arena_descriptor_back(desc);
    unsigned flags_now = *(const unsigned *) (desc + DESC_FLAGS);
    int backdrop = (flags_were & DOGMA_FLAGS) != (flags_now & DOGMA_FLAGS);
    if (backdrop) {
        ((void (*)(unsigned long)) (b + FN_BACKDROP_TYPE))(room);
        ((void (*)(unsigned long)) (b + FN_BACKDROP_INIT))(room + ROOM_BACKDROP_OBJ);
    }
    int missing = 0;
    for (int k = 0; k < 8; k++)
        if ((g_arena.doors >> k & 1) && !*(const unsigned long *) (room + ROOM_DOOR + (unsigned long) k * 8)) missing++;
    if (missing) ((void (*)(unsigned long)) (b + FN_ROOM_DOORS))(room);
#if ISAAC_DIAG
    unsigned long mgr = *(const unsigned long *) (b + MANAGER_PTR_SP);
    char audio_was[96] = "";
    if (mgr) {
        const float *sfx = (const float *) (mgr + MGR_SFX);
        int slot = *(const int *) (mgr + MGR_MUSIC);
        snprintf(audio_was, sizeof(audio_was), "sfx level %.2f, music slot %d layers %#llx",
                 (double) sfx[SFX_LEVEL / 4], slot, slot >= 0 && slot < 8
                     ? *(const unsigned long long *) (mgr + MGR_MUSIC + (unsigned long) slot * 0x1e0 + 0x1c8) : 0ULL);
    }
#endif
    /* the room's own music, as Dogma's arena turns it on (0x62c1c4); Dogma's death left its jingle,
       its layers off (0x761820) and nothing after (the music of -10 never comes) */
    ((void (*)(unsigned long)) (b + FN_ROOM_MUSIC))(room);
#if ISAAC_DIAG
    if (shim_report_active() && mgr) {
        const float *sfx = (const float *) (mgr + MGR_SFX);
        int slot = *(const int *) (mgr + MGR_MUSIC);
        char line[240];
        snprintf(line, sizeof(line), "arena: audio %s -> sfx level %.2f, music slot %d layers %#llx", audio_was,
                 (double) sfx[SFX_LEVEL / 4], slot, slot >= 0 && slot < 8
                     ? *(const unsigned long long *) (mgr + MGR_MUSIC + (unsigned long) slot * 0x1e0 + 0x1c8) : 0ULL);
        report_line("spawner", line);
    }
#endif
    int removed = 0;
    for (unsigned i = 0; i < n; i++) {
        unsigned long e = items[i];
        if (!shim_trace_readable((const void *) e, ENT_SPAWNER_TYPE + 4)) continue;
        int t = *(const int *) (e + ENT_TYPE_SP), v = *(const int *) (e + ENT_VARIANT_SP);
        int ours = 0;
        if (g_arena.dogma && t == TYPE_DOGMA && v == VARIANT_TV && *(const int *) (e + ENT_SPAWNER_TYPE) == TYPE_DOGMA)
            ours = 1;
        if (g_arena.mother && t == TYPE_MOTHER && !*(const unsigned char *) (e + ENT_DEAD)
            && *(const int *) (e + ENT_STATE_SP) != STATE_DEATH
            && !((int (*)(unsigned long)) (*(const unsigned long *) (*(const unsigned long *) e + VT_ACTIVE_ENEMY)))(e))
            ours = 1;                        /* her leftovers: parts that are no enemies (0x4d4c1c), after the clear */
        if (g_arena.mother && t == TYPE_EFFECT && v == EFFECT_CORPSE_WALL) {
            ours = 1;
            for (int h = 0; h < g_arena.had_n; h++) if (g_arena.had[h] == e) ours = 0;
        }
        if (ours) {
            ((void (*)(unsigned long)) (*(const unsigned long *) (*(const unsigned long *) e + VT_REMOVE)))(e);
            removed++;
        }
    }
#if ISAAC_DIAG
    if (shim_report_active()) {
        char line[200];
        snprintf(line, sizeof(line), "arena: %s gone - room put back: flags %#x -> %#x, %d doors made again, "
                 "backdrop %s, %d arena entities removed",
                 g_arena.dogma ? (g_arena.mother ? "Dogma and Mother" : "Dogma") : "Mother", flags_were, flags_now,
                 missing, backdrop ? "rebuilt" : "untouched", removed);
        report_line("spawner", line);
    }
#endif
    (void) removed;
    memset(&g_arena, 0, sizeof(g_arena));
}

/*
 * A PLAIN BOSS STAYS PLAIN, A COLOURED ONE STAYS COLOURED (the owner, 21st
 * pass: "if I pressed the plain one, the plain one must come"). Measured: a
 * boss with subtype 0 gets one of its colours at every config load (vtable
 * +0xc8 0x681f2c: from Entity::Init 0x82bcc4 inside Game::Spawn, and from
 * every Morph 0x689070 -> 0x68914c) when a RNG seeded with the ROOM's seed
 * (descriptor +0x68, read at 0x68221c, shift 5) rolls under the chance (0.1;
 * 0.3 with achievement 0x21; 0.6 in Greedier; 1.0 on a victory lap or
 * challenge 34; doubled per Purple Heart) - one answer for every boss of the
 * room. A subtype other than 0 skips the roll (0x682130). So a plain boss can
 * still turn coloured later, at a transform that keeps subtype 0 or in a part
 * it spawns with subtype 0 - the bosses of the list whose transforms land on a
 * config that has colours (bosscolors.xml): Mom 45.0 -> 45.10 (her first
 * update), War 65.0 -> 65.10 (under half health, 0x5eb6f4), Death 66.0 ->
 * 66.30 (0x5ec7fc) and the parts Death and the splitters spawn. (Dogma ->
 * 950.2, Mother -> 912.10, Mega Satan -> 275, Ultra Greed -> 406.1, Brownie,
 * the Matriarch: the game forces subtype 0 or 1000 and the new config has no
 * colours - nothing to roll.)
 *
 * The room's seed is read by a dozen other places too (a devil door's roll at
 * the clear 0x5a99a0, items...), so it is not touched. Only the colour roll's
 * own read is turned, while a plain boss of ours lives: the word at 0x68221c,
 * "ldr w1, [x8, #0x68]", becomes "mov w1, #0x56a0000" - a seed whose first
 * roll by the game's RNG (0x76f8a8, 0x76f9cc, emulated on the desk) is 0.9995,
 * which no chance short of 1.0 reaches - and goes back when none of those
 * entities is left in the room or the room changes. Every config load in that
 * time rolls "plain": the boss, its transforms, its parts. A coloured row is a
 * subtype, never rolls, and keeps its colour through a transform the game's
 * way (the subtype goes along, 0x55d8f0, 0x5eb6e4, 0x5ec7ec).
 */
#define COLOUR_SEED_READ   0x68221c
#define COLOUR_SEED_GAME   0xb9406901u      /* ldr w1, [x8, #0x68] */
#define COLOUR_SEED_PLAIN  0x52a0ad41u      /* mov w1, #0x56a0000 */
#define PLAIN_KEPT         32

int shim_patch_word(unsigned long off, uint32_t was_a, uint32_t was_b, uint32_t now);

static struct { int on; unsigned long desc; int n; unsigned long e[PLAIN_KEPT]; } g_plain;
static void plain_guard(int on) {
    if (g_plain.on == on) return;
    if (!shim_patch_word(COLOUR_SEED_READ, COLOUR_SEED_GAME, COLOUR_SEED_PLAIN,
                         on ? COLOUR_SEED_PLAIN : COLOUR_SEED_GAME)) return;
    g_plain.on = on;
    if (!on) g_plain.n = 0;
#if ISAAC_DIAG
    if (shim_report_active())
        report_line("spawner", on ? "plain boss: the colour roll reads the plain seed (0x68221c) while it lives"
                                  : "plain boss: gone - the colour roll reads the room's seed again");
#endif
}

static void plain_keep(unsigned long room, unsigned long e) {
    if (!e) return;
    g_plain.desc = *(const unsigned long *) (room + ROOM_DESCRIPTOR);
    for (int i = 0; i < g_plain.n; i++) if (g_plain.e[i] == e) return;
    if (g_plain.n < PLAIN_KEPT) g_plain.e[g_plain.n++] = e;
}

static void plain_between_frames(void) {
    if (!g_plain.on) return;
    unsigned long b = shim_image_base();
    unsigned long game = b ? *(const unsigned long *) (b + GAME_PTR) : 0;
    unsigned long room = game ? *(const unsigned long *) (game + GAME_ROOM) : 0;
    if (!room || *(const unsigned long *) (room + ROOM_DESCRIPTOR) != g_plain.desc) {
        plain_guard(0);
        return;
    }
    const unsigned long *items;
    unsigned n;
    if (!arena_list(&items, &n)) return;
    int kept = 0;
    for (int k = 0; k < g_plain.n; k++) {
        int here = 0;
        for (unsigned i = 0; i < n && !here; i++) here = items[i] == g_plain.e[k];
        if (here) g_plain.e[kept++] = g_plain.e[k];
    }
    g_plain.n = kept;
    if (!kept) plain_guard(0);
}

/*
 * The run is closed BEFORE the give, not after (plan item A1): AddCollectible
 * unlocks by itself, inside - the collection (0x500714 -> 0x7025a8), the sets
 * (Mom's Contact and the rest), "U broke it!", EvaluateItems' own unlocks - and
 * every one of those writers checks read-only first. So everything that can
 * still call the give off is asked first, then the run is marked, then the
 * thing is given. A give called off marks nothing. What this costs, on purpose:
 * a thing from the spawner is no longer ticked in the collection.
 */
/*
 * THE NEW KINDS (the owner, 07.10.2026; other stuff/spawner/research/spawn.md,
 * every address read in the image):
 *
 *   on a pedestal  as the game puts one beside the player (0x546454..0x5464e4):
 *                  Spawn(5, 100, FindFreePickupSpawnPosition(room, player, 40),
 *                  zero, none, id, seed), then the poof Spawn(1000, 15) there
 *   on the floor   the same free spot, 40 from the player, one per count; a
 *                  spot just taken counts as taken for the next (pickups within
 *                  20 block a spot, 0x59de50, and Spawn puts them in the room's
 *                  list at once)
 *   into the player  a pickup is spawned there and handed to the game's own
 *                  pickup collision, Entity_Pickup::HandleCollision 0x71e310
 *                  (pickup, player, 0): every per-kind rule is the game's - a
 *                  golden key, a giga bomb, a charged key, a coin's worth, a
 *                  heart the character cannot take (it stays on the floor, as a
 *                  heart at full health does), a battery's charge. A taken
 *                  pickup plays "Collect" and is marked dead (+0x1c3,
 *                  0x71ed1c..0x71ed34): the game takes it away itself.
 *   a chest        closed, as the game resolves subtype 0 at its spawn
 *                  (0x715178) but without its swaps (Left Hand, Gilded Key):
 *                  1, the eternal chest 2 (0x715a84), the mega chest
 *                  1 + rand 4 + rand 4 (0x7158e0)
 *   a troll bomb   the game's own Spawn(4, 3 | 4 | 18, pos, zero, none, 0, seed)
 *                  (0x719a10..0x719b64)
 *   a pill         by effect, as the PC console's `giveitem p<N>`: the colour
 *                  ItemPool::ForceAddPillEffect 0x87b440(Game+0x242c0, effect)
 *                  answers (it takes a colour of the run for the effect when no
 *                  colour carries it yet), | 0x800 a horse pill; the gold pill is
 *                  colour 14 (GetPill 0x879b20). AddPill 0x515178(player, colour)
 *   a card, a rune AddCard 0x515170, as before; on the floor Spawn(5, 300, id)
 *   a trinket      as before, | 0x8000 golden (the console's `giveitem T`); on the
 *                  floor Spawn(5, 350, id)
 */
#define FN_PICKUP_COLLIDE  0x71e310         /* Entity_Pickup::HandleCollision(pickup, other, low) */
#define FN_FORCE_PILL      0x87b440         /* ItemPool::ForceAddPillEffect(pool, effect) -> colour 1..13 */
#define FN_ADD_PILL        0x515178         /* AddPill(player, colour) */
#define GAME_ITEM_POOL     0x242c0
#define PILL_COLOUR_GOLD   14
#define PILL_COLOUR_HORSE  0x800
#define FREE_STEP          40.0f            /* the game's own, beside the player (0x546468) */
#define TYPE_PICKUP        5
#define VAR_COLLECTIBLE    100
#define VAR_PILL           70
#define VAR_CARD           300
#define VAR_TRINKET        350
#define VAR_ETERNAL_CHEST  53
#define VAR_MEGA_CHEST     57
#define TYPE_EFFECT_POOF   1000
#define VAR_POOF           15
#define ENT_DEAD_SP        0x1c3            /* a taken pickup: set by the collision (0x71ed34) */

static int g_give_times = 1;                 /* how many of it, the row's count */

static unsigned long spawn_beside(unsigned long b, unsigned long game, unsigned long room, unsigned long player,
                                  int type, int variant, int subtype, unsigned long spawner, float *where) {
    vec2_t at = ((vec2_t (*)(unsigned long, const float *, float, int, int)) (b + FN_FREE_SPOT))(
        room, (const float *) (player + PLAYER_POSITION), FREE_STEP, 0, 0);
    float pos[2] = { at.x, at.y };
    if (where) { where[0] = at.x; where[1] = at.y; }
    int seed = ((int (*)(void)) (b + FN_RANDOM))();
    return ((unsigned long (*)(unsigned long, int, int, const float *, const float *, unsigned long, int, int,
                               int)) (b + FN_SPAWN))(
        game, type, variant, pos, (const float *) (b + ZERO_VECTOR), spawner, subtype, seed, 0);
}

/* a chest closed the way the game closes a fresh one; any other pickup as it is */
static int closed_chest(unsigned long b, int variant, int subtype) {
    if (variant == VAR_ETERNAL_CHEST) return 2;
    if (variant == VAR_MEGA_CHEST) {
        unsigned r1 = (unsigned) ((int (*)(void)) (b + FN_RANDOM))();
        unsigned r2 = (unsigned) ((int (*)(void)) (b + FN_RANDOM))();
        return 1 + (int) (r1 % 4u) + (int) (r2 % 4u);
    }
    return subtype;
}

static int trinket_slot_free(unsigned long b, unsigned long player) {
    int slots = ((int (*)(unsigned long)) (b + FN_TRINKET_SLOTS))(player);
    for (int i = 0; i < slots && i < 2; i++)
        if (!*(const int *) (player + PLAYER_TRINKETS + (unsigned long) i * 4)) return 1;
    return 0;
}

/* anything but an enemy, `times` of it; the run is already closed */
static void give_thing(unsigned long b, unsigned long game, unsigned long room, unsigned long player,
                       unsigned char kind, unsigned id, int times) {
    if (times < 1) times = 1;
    switch (kind) {
    case KIND_ITEM:
        if (g_ground && room) {
            float at[2];
            spawn_beside(b, game, room, player, TYPE_PICKUP, VAR_COLLECTIBLE, (int) id, 0, at);
            ((unsigned long (*)(unsigned long, int, int, const float *, const float *, unsigned long, int, int,
                                int)) (b + FN_SPAWN))(
                game, TYPE_EFFECT_POOF, VAR_POOF, at, (const float *) (b + ZERO_VECTOR), 0, 0,
                ((int (*)(void)) (b + FN_RANDOM))(), 0);
        } else {
            ((void (*)(unsigned long, int, int, int, int, int)) (b + FN_ADD_COLLECTIBLE))(player, (int) id, 0, 1, 0, 0);
        }
        break;
    case KIND_PICKUP: {
        if (id >= SP_PICKUPS || !room) break;
        int loot = SP_PICKUP[id].group == SP_G_LOOT;
        int taken = 0, left = 0;
        for (int t = 0; t < times; t++) {
            int sub = SP_PICKUP[id].type == TYPE_PICKUP
                      ? closed_chest(b, SP_PICKUP[id].variant, SP_PICKUP[id].subtype) : SP_PICKUP[id].subtype;
            unsigned long e = spawn_beside(b, game, room, player, SP_PICKUP[id].type, SP_PICKUP[id].variant, sub, 0,
                                           NULL);
            if (!e || loot || g_ground) continue;
            ((int (*)(unsigned long, unsigned long, int)) (b + FN_PICKUP_COLLIDE))(e, player, 0);
            if (*(const unsigned char *) (e + ENT_DEAD_SP)) taken++;
            else left++;
        }
#if ISAAC_DIAG
        if (shim_report_active() && !loot && !g_ground) {
            char line[120];
            snprintf(line, sizeof(line), "pickup %d.%d.%d into the player: %d taken, %d left on the floor",
                     SP_PICKUP[id].type, SP_PICKUP[id].variant, SP_PICKUP[id].subtype, taken, left);
            report_line("spawner", line);
        }
#else
        (void) taken;
        (void) left;
#endif
        break;
    }
    case KIND_CARD:
        for (int t = 0; t < times; t++) {
            if (g_ground && room) spawn_beside(b, game, room, player, TYPE_PICKUP, VAR_CARD, (int) id, 0, NULL);
            else ((void (*)(unsigned long, int)) (b + FN_ADD_CARD))(player, (int) id);
        }
        break;
    case KIND_PILL: {
        int effect = (int) (id & 0xff), colour;
        if (effect == PILL_GOLD) colour = PILL_COLOUR_GOLD;
        else colour = ((int (*)(unsigned long, int)) (b + FN_FORCE_PILL))(game + GAME_ITEM_POOL, effect);
        if (colour <= 0) break;
        if (id & PILL_HORSE) colour |= PILL_COLOUR_HORSE;
        for (int t = 0; t < times; t++) {
            if (g_ground && room) spawn_beside(b, game, room, player, TYPE_PICKUP, VAR_PILL, colour, 0, NULL);
            else ((void (*)(unsigned long, int)) (b + FN_ADD_PILL))(player, colour);
        }
#if ISAAC_DIAG
        if (shim_report_active()) {
            char line[100];
            snprintf(line, sizeof(line), "pill effect %d%s as colour %d", effect, (id & PILL_HORSE) ? " (horse)" : "",
                     colour);
            report_line("spawner", line);
        }
#endif
        break;
    }
    case KIND_TRINKET:
        for (int t = 0; t < times; t++) {
            if (!g_ground && trinket_slot_free(b, player))
                ((void (*)(unsigned long, int, int)) (b + FN_ADD_TRINKET))(player, (int) id, 1);
            else if (room)
                /* on the floor; with every slot taken the game's own pickup swaps it with the one held -
                   DropTrinket (0x508d90) carries special cases of its own */
                spawn_beside(b, game, room, player, TYPE_PICKUP, VAR_TRINKET, (int) id, g_ground ? 0 : player, NULL);
        }
        break;
    default:
        break;
    }
}

static void give_now(unsigned char kind, unsigned id) {
    unsigned long b = shim_image_base();
    if (!b) return;
    unsigned long game = *(const unsigned long *) (b + GAME_PTR);
    if (!game) return;
    unsigned long player = ((unsigned long (*)(unsigned long, int)) (b + FN_PLAYER_AT))(
        game + GAME_PLAYERS, 0);
    if (!player) return;
    if (kind != KIND_ENEMY) {                /* a thing goes to the body on the switch */
        unsigned long to = who_player();
        if (to) player = to;
    }

    /* 1. everything that can call the give off */
    unsigned long room = *(const unsigned long *) (game + GAME_ROOM);
    int free_slot = 0;
    if (kind == KIND_ENEMY && (!room || id >= SP_ENEMIES)) return;
    if (kind == KIND_TRINKET) {
        free_slot = trinket_slot_free(b, player);
        if (!free_slot && !room) return;
    }
    if (kind == KIND_PICKUP && (!room || id >= SP_PICKUPS)) return;
    if (kind != KIND_ENEMY && kind != KIND_ITEM && kind != KIND_CARD && kind != KIND_TRINKET && kind != KIND_PILL
        && kind != KIND_PICKUP) return;

    /* 2. the run is closed */
    lock_the_run(b, game);
#if ISAAC_DIAG
    if (shim_report_active()) {
        static int said;
        if (said < 20) {
            said++;
            unsigned long manager = *(const unsigned long *) (b + MANAGER_PTR_SP);
            int ro = manager ? *(const unsigned char *) (manager + MGR_PERSISTENT + PGD_READ_ONLY) : -1;
            report_line("spawner", !g_lock ? "closed before the give: not asked (CONSOLE TROPHIES on)"
                                           : ro == 1 ? "closed before the give: yes"
                                                     : "closed before the give: NO");
        }
    }
#endif

    /* 3. the give */
    if (kind == KIND_ENEMY) {
        vec2_t at = ((vec2_t (*)(unsigned long, const float *, float, int, int)) (b + FN_FREE_SPOT))(
            room, (const float *) (player + PLAYER_POSITION), 120.0f, 0, 0);
        int seed = ((int (*)(void)) (b + FN_RANDOM))();
        float pos[2] = { at.x, at.y };
        int subtype = g_give_subtype >= 0 ? g_give_subtype : SP_ENEMY[id].subtype;   /* a boss colour: n + 1 */
        unsigned long e = 0;
        int plain = subtype == 0 && SP_ENEMY[id].group != SP_GROUP_MONSTER && SP_ENEMY[id].group != SP_GROUP_TRAINING;
        if (plain) plain_guard(1);           /* before the spawn: Init rolls the colour inside it */
        g_spawned_n = 0;
        if (!layout_spawn(b, game, room, id, subtype, seed, &e)) {
            e = ((unsigned long (*)(unsigned long, int, int, const float *, const float *, unsigned long, int, int,
                                    int)) (b + FN_SPAWN))(
                game, SP_ENEMY[id].type, SP_ENEMY[id].variant, pos, (const float *) (b + ZERO_VECTOR), 0,
                subtype, seed, 0);
            if (e) g_spawned[g_spawned_n++] = e;
        }
        if (plain) {
            for (int k = 0; k < g_spawned_n; k++) plain_keep(room, g_spawned[k]);
            if (!g_plain.n) plain_guard(0);
#if ISAAC_DIAG
            if (shim_report_active()) {
                char line[120];
                snprintf(line, sizeof(line), "plain boss: %d entities kept plain, subtype now %d", g_spawned_n,
                         e ? *(const int *) (e + ENT_SUBTYPE) : -1);
                report_line("spawner", line);
            }
#endif
        }
        if (e) arena_remember(game, room, e, SP_ENEMY[id].type, SP_ENEMY[id].variant);
        if (e && SP_ENEMY[id].type == TYPE_MOM && SP_ENEMY[id].variant == 0 && !mom_keeps_players_in(b, room)) {
            mom_watch_players(room);
#if ISAAC_DIAG
            if (shim_report_active())
                report_line("spawner", "Mom in a room whose floor does not hold (320,370) - her players are watched");
#endif
        }
        finals_remember(e, SP_ENEMY[id].type, SP_ENEMY[id].variant);
        /*
         * A champion: spawned as it is, then made one the way the game makes a
         * fresh champion - MakeChampion(npc, seed, colour, 0) right after the
         * Spawn, as the split child does (0x5e3d84) and the room's champion
         * roll (0x5afebc). The last argument 1 is only for re-making one that
         * already is a champion (0x6824c0, after a morph): with it MakeChampion
         * skips the health refill (+0x354 = the doubled +0x358, 0x682c48) and
         * the Eternal Fly 96.0 the white and rainbow colours get (0x682a64,
         * 0x682bd8). With 0 it does nothing to an NPC that already is a
         * champion (0x6825dc) - the game's rule, kept.
         */
        if (e && g_give_champion >= 0 && g_give_champion < 26)
            ((void (*)(unsigned long, int, int, int)) (b + FN_MAKE_CHAMPION))(
                e, ((int (*)(void)) (b + FN_RANDOM))(), g_give_champion, 0);
#if ISAAC_DIAG
        if (shim_report_active() && (g_give_champion >= 0 || g_give_subtype >= 0)) {
            char line[200];
            snprintf(line, sizeof(line), "spawned %d.%d.%d %s %d: entity %s, champion byte %d colour %d, "
                     "hp %.1f of %.1f",
                     SP_ENEMY[id].type, SP_ENEMY[id].variant, subtype,
                     g_give_champion >= 0 ? "as champion" : "in boss colour",
                     g_give_champion >= 0 ? g_give_champion : g_give_subtype - 1, e ? "made" : "NOT made",
                     e ? *(const unsigned char *) (e + NPC_CHAMPION) : -1,
                     e ? *(const int *) (e + NPC_CHAMPION_COLOUR) : -1,
                     e ? (double) *(const float *) (e + NPC_HP) : -1.0,
                     e ? (double) *(const float *) (e + NPC_MAX_HP) : -1.0);
            report_line("spawner", line);
        }
#endif
    } else {
        give_thing(b, game, room, player, kind, id, g_give_times);
    }
    if (shim_report_active()) {
        char line[80];
        snprintf(line, sizeof(line), "gave kind %u id %u x%d%s", (unsigned) kind, id,
                 kind == KIND_ENEMY || kind == KIND_ITEM ? 1 : g_give_times, g_ground ? " on the ground" : "");
        report_line("spawner", line);
    }
#if ISAAC_DIAG
    if (shim_report_active() && kind != KIND_ENEMY) {
        unsigned long all[PLAYERS_SHOWN];
        int n = shim_stats_players(all, PLAYERS_SHOWN), at = -1;
        for (int i = 0; i < n; i++) if (all[i] == player) at = i;
        /* an item: who holds it now - the body, and its twin (a soul's passive goes to T.Forgotten) */
        char held[64] = "";
        if (kind == KIND_ITEM) {
            int (*has)(unsigned long, int, int) = (int (*)(unsigned long, int, int)) (b + FN_HAS_ITEM_SP);
            unsigned long twin = *(const unsigned long *) (player + PLAYER_TWIN_SP);
            snprintf(held, sizeof(held), ", held: body %d twin %d", has(player, (int) id, 1) ? 1 : 0,
                     twin ? (has(twin, (int) id, 1) ? 1 : 0) : -1);
        }
        char line[200];
        snprintf(line, sizeof(line), "give target: body %d of %d, player %d type %d%s%s", at, n,
                 shim_stats_player_number(player), shim_stats_player_type(player), held,
                 kind == KIND_TRINKET && !free_slot ? " (trinket on the floor beside it)" : "");
        report_line("spawner", line);
    }
#endif
}

/* ITEMS' row k: the thing, as many as its count */
void shim_spawner_give(int index) {
    if (index < 0 || index >= g_rows_n || g_rows[index].kind == KIND_HEAD) return;
    int n = count_of(&g_rows[index]);
    g_give_times = n > 0 ? n : 1;
    give_now(g_rows[index].kind, g_rows[index].id);
    g_give_times = 1;
}

/* ------------------------------------------------------------ who unlocked what */
/*
 * ONE MEASUREMENT FOR THE WHOLE CLASS (plan item A7, report builds only). Every
 * writer of progress checks read-only (data+1) first, so eight doors are
 * watched (trace.c, mark '7'; the letter after it says which). The one that
 * does not - the cutscene count 0x703644 behind the endings menu - is left the
 * game's on purpose: a run on a typed seed counts its ending too, and a marked
 * run is to behave exactly like one (the owner, 03.10.2026):
 *
 *   0x702644 TryUnlock(data, id)                      'a' achievement
 *   0x701e28 (data, counter, delta): the counters and  'c' counter
 *            the completion marks, Mom's kill among them
 *   0x7025a8 (data, item): the collection               's' seen
 *   0x702c10 (data, challenge): a challenge done        'w' won
 *   0x703288 the bestiary's encounters (data+0xee8)     'e'
 *   0x703054 its kills (+0xf00)                         'k'
 *   0x702d10 its hits (+0xf18)                          'h'
 *   0x702e54 its deaths (+0xf30)                        'd'
 *
 * "unlock:" is an achievement or a challenge about to be written, with who
 * asked; "LEAK" is any write going through while a marked run is played (not
 * in the main menu). After A1, A2 and A5 there must be none. Eighty lines at
 * most.
 */
#if ISAAC_DIAG
#define GAME_RERUN_BYTE 0x1cf43c             /* the game's own Rerun mark, shown only */
#define MGR_STATE_SP    0x204c0              /* 1: the main menu (0x67266c) */
#define STATE_MENU      1
#define UNLOCK_LINES    80

int shim_trace_add(unsigned long off, const char *name);

void shim_unlocks_watch_install(void) {
    if (!shim_report_active()) return;
    static const struct { unsigned long at; uint32_t w[5]; const char *name; } DOOR[] = {
        { 0x702644, { 0xA9BA6FFCu, 0xA90167FAu, 0xA9025FF8u, 0xA90357F6u, 0xA9044FF4u },
          "7achievement (TryUnlock 0x702644)" },
        { 0x701e28, { 0xA9BA6FFCu, 0xA90167FAu, 0xA9025FF8u, 0xA90357F6u, 0xA9044FF4u },
          "7counter (0x701e28)" },
        /* the fifth word is a b.hi / b.gt: trace.c carries one conditional branch over */
        { 0x7025a8, { 0xA9BE4FF4u, 0xA9017BFDu, 0x910043FDu, 0x710B703Fu, 0x54000408u },
          "7seen in the collection (0x7025a8)" },
        { 0x702c10, { 0xA9BE4FF4u, 0xA9017BFDu, 0x910043FDu, 0x7100B43Fu, 0x540003ACu },
          "7won challenge (0x702c10)" },
        /* the bestiary's four maps, each writer behind its read-only check (03.10.2026) */
        { 0x703288, { 0xD10103FFu, 0xA9024FF4u, 0xA9037BFDu, 0x9100C3FDu, 0x39400414u },
          "7encounter in the bestiary (0x703288)" },
        { 0x703054, { 0xD10103FFu, 0xA9024FF4u, 0xA9037BFDu, 0x9100C3FDu, 0x39400413u },
          "7kill in the bestiary (0x703054)" },
        { 0x702d10, { 0xD10103FFu, 0xA9024FF4u, 0xA9037BFDu, 0x9100C3FDu, 0x39400413u },
          "7hit in the bestiary (0x702d10)" },
        { 0x702e54, { 0xD10103FFu, 0xA9024FF4u, 0xA9037BFDu, 0x9100C3FDu, 0x39400413u },
          "7death in the bestiary (0x702e54)" },
    };
    unsigned long b = shim_image_base();
    if (!b) return;
    for (unsigned i = 0; i < sizeof(DOOR) / sizeof(DOOR[0]); i++) {
        const uint32_t *at = (const uint32_t *) (b + DOOR[i].at);
        if (memcmp(at, DOOR[i].w, sizeof(DOOR[i].w))) {
            char line[120];
            snprintf(line, sizeof(line), "watch: %lx is not the code expected (%08x) - not watched",
                     DOOR[i].at, at[0]);
            report_line("unlock", line);
            continue;
        }
        if (shim_trace_add(DOOR[i].at, DOOR[i].name) < 0) continue;   /* trace.c says why */
    }
}

void shim_unlocks_seen(int kind, unsigned long data, unsigned long id, long delta, unsigned long from) {
    static int said;
    if (said >= UNLOCK_LINES) return;
    unsigned long b = shim_image_base();
    if (!b) return;
    unsigned long manager = *(const unsigned long *) (b + MANAGER_PTR_SP);
    if (!manager || data != manager + MGR_PERSISTENT) return;       /* Repentance's own data only */
    const unsigned char *d = (const unsigned char *) data;
    if (d[PGD_READ_ONLY]) return;                                    /* the writer refuses by itself */
    unsigned n = (unsigned) id;
    unsigned long game = *(const unsigned long *) (b + GAME_PTR);
    int frame = game ? *(const int *) (game + GAME_FRAME_SP) : -1;
    int rerun = game ? *(const unsigned char *) (game + GAME_RERUN_BYTE) : -1;
    /*
     * Marked means a marked run being PLAYED: in the main menu the Game object
     * still holds the last run's seed, and the slot load's own derivation
     * (0x700968, counter 0 by 0 from +0x7009a4) read as three false LEAKs on
     * 03.10.2026. A counter moved by nothing is no write either.
     */
    int state = *(const int *) (manager + MGR_STATE_SP);
    int marked = state != STATE_MENU && shim_run_marked_now();
    unsigned long at = from > b ? from - b : from;
    const char *what = NULL;
    char line[200];
    switch (kind) {
    case 'a':
        if (!n || n >= ACHIEVEMENTS_N || d[PGD_ACHIEVEMENTS + n]) return;
        what = "achievement";
        break;
    case 'w':
        if (n > CHALLENGE_TOP) return;
        what = "challenge done";
        break;
    case 'c':
        if (!marked || !delta) return;                               /* counters move all the time */
        snprintf(line, sizeof(line), "LEAK: counter %u %+ld while the run is marked, frame %d, from +0x%lx",
                 n, delta, frame, at);
        said++;
        report_line("unlock", line);
        return;
    case 's':
        if (!marked || !n || n > COLLECTION_TOP || d[PGD_COLLECTION + n]) return;
        snprintf(line, sizeof(line), "LEAK: item %u ticked in the collection while the run is marked, "
                 "frame %d, from +0x%lx", n, frame, at);
        said++;
        report_line("unlock", line);
        return;
    case 'e': case 'k': case 'h': case 'd':
        if (!marked) return;                                         /* the bestiary fills all the time */
        snprintf(line, sizeof(line), "LEAK: bestiary %s of %u while the run is marked, frame %d, from +0x%lx",
                 kind == 'e' ? "encounter" : kind == 'k' ? "kill" : kind == 'h' ? "hit" : "death", n, frame, at);
        said++;
        report_line("unlock", line);
        return;
    default:
        return;
    }
    snprintf(line, sizeof(line), "id %u (%s), frame %d, run marked %d, spawner lock %d, rerun %d, from +0x%lx",
             n, what, frame, marked, g_lock, rerun, at);
    said++;
    report_line("unlock", line);
    if (marked) {
        snprintf(line, sizeof(line), "LEAK: %s %u in a marked run, from +0x%lx", what, n, at);
        said++;
        report_line("unlock", line);
    }
}
#else
void shim_unlocks_watch_install(void) { }
#endif

/* ------------------------------------------------------------ the first frame */
/*
 * Once, on the first frame: where the lock came from (plan item A4), and in a
 * report build the self-test of the run mark - the question wired through the
 * trampoline, the other question still answered 0, the list, and a write and a
 * read of the file beside the real one.
 */
#define PLATFORM_FORBIDS 0x672828
#define RUN_MARK_TRAMP   0x8dab58

#if ISAAC_DIAG
static const char *self_test(unsigned long b) {
    const uint32_t *site = (const uint32_t *) (b + PLATFORM_FORBIDS);
    const uint32_t *tramp = (const uint32_t *) (b + RUN_MARK_TRAMP);
    uint64_t to;
    memcpy(&to, tramp + 2, sizeof(to));
    if (site[0] != 0x1409A0CCu || tramp[0] != 0x58000050u || tramp[1] != 0xD61F0200u
        || to != (uint64_t) (uintptr_t) shim_run_forbidden_asked)
        return "FAIL: 0x672828 is not wired to 0x8dab58 (the loader left the game's stub; see the log)";
    unsigned long manager = *(const unsigned long *) (b + MANAGER_PTR_SP);
    if (((int (*)(unsigned long, int)) (b + PLATFORM_FORBIDS))(manager, 1) != 0)
        return "FAIL: the question with 1 does not answer 0";

    static run_marks_t t, back;
    memset(&t, 0, sizeof(t));
    memset(&back, 0, sizeof(back));
    marks_add(&t, 1, 0x1111u, 0, 1);
    marks_add(&t, 1, 0x2222u, 7, 1);
    for (unsigned i = 0; i < 10; i++) marks_add(&t, 2, 100u + i, 0, 1);    /* slot 2 runs over */
    if (marks_add(&t, 1, 0x1111u, 0, 1) || marks_find(&t, 1, 0x1111u, 0) < 0
        || marks_find(&t, 1, 0x2222u, 7) < 0 || marks_find(&t, 1, 0x2222u, 0) >= 0
        || marks_find(&t, 2, 100u, 0) >= 0 || marks_find(&t, 2, 109u, 0) < 0 || t.n[2] != MARKS_PER_SLOT
        || marks_find(&t, 3, 0x1111u, 0) >= 0)
        return "FAIL: the list";

    char path[600];
    if (!runs_path(path, sizeof(path), "port-spawner-runs.selftest"))
        return "ok, the file part not run: the saves folder is not known yet";
    if (marks_write(&t, path) != 0) { unlink(path); return "FAIL: the test file was not written"; }
    int n = marks_read(&back, path);
    unlink(path);
    if (n != t.n[1] + t.n[2] || memcmp(back.n, t.n, sizeof(t.n)))
        return "FAIL: the test file did not read back";
    for (int s = 0; s < MARK_SLOTS; s++)
        for (int i = 0; i < t.n[s]; i++)
            if (back.m[s][i].seed != t.m[s][i].seed || back.m[s][i].challenge != t.m[s][i].challenge)
                return "FAIL: the test file read back different";
    return "ok";
}
#endif

static void first_frame(unsigned long b) {
    g_frames_seen = 1;
#if ISAAC_DIAG
    if (!shim_report_active()) return;
    char line[200];
    snprintf(line, sizeof(line), "lock %d (%s)", g_lock, g_lock_from ? "from ini" : "default");
    report_line("spawner", line);
    snprintf(line, sizeof(line), "run mark: self-test %s", self_test(b));
    report_line("spawner", line);
#else
    (void) b;
#endif
}

/* ------------------------------------------------------------ the card */
/*
 * The card is the game's own dialog - the one that asks to reconnect a
 * controller - with nothing written in it, only its paper at a size we choose.
 * What it brings with it is exactly what a card over the pause screen needs:
 *
 *   - the paper, its shadow and the darkened screen behind it, sliding in and
 *     out the way every popup of the game does;
 *   - the world and the menu under it stand still while it is up (the run
 *     update and the menu update are both skipped with a dialog in the list);
 *   - Back on a pad closes it, once its back answer is not -1.
 *
 * The line, the results and their pictures are ours, drawn over the paper in
 * the description font, so every language the descriptions have is shown in
 * the face that can draw it. The paper's live size and place are read from the
 * dialog itself each frame, so nothing here depends on how the game lays it out.
 *
 * Calls and fields (all measured in the image, see the handoff notes):
 *
 *   0x7628d4(desc, font, closeCallback, float size[2])   fill a description
 *   0x6644b4(manager, desc) -> id                         put a copy up
 *   0x67456c(desc)                                        free our description
 *   0x66457c(manager, &id)                                start closing it
 *   the list: the manager itself is its head, [+8] the first node, a node's
 *   dialog at [+0x10] and the next node at [+8]; a dialog's id is its first int
 *   dialog +4 state (0 opening, 1 open, 2 closing), +0x40 width and height,
 *   +0x90 the slide offset from the canvas middle
 */
#include "spawner_tabs.h"
#include "spawner_rooms.h"
#include "spawner_floors.h"
#include "spawner_strings.h"

#define MANAGER_PTR      0xac2428
#define MGR_DIALOG_FONT  0x196a48
#define FN_DIALOG_FILL   0x7628d4
#define FN_DIALOG_ADD    0x6644b4
#define FN_DIALOG_FREE   0x67456c
#define FN_DIALOG_CLOSE  0x66457c
#define DESC_BYTES       0x70
#define DESC_BACK        0x68
#define DIALOG_STATE     4
#define DIALOG_SIZE      0x40
#define DIALOG_SLIDE     0x90
#define STATE_OPEN       1
#define CANVAS_SIZE      0xa9aff0
#define AREA             0xabec64       /* the drawn area: width, height, x, y in pixels */
/*
 * The menu's own input: an action asked for this frame (the dialog reads it
 * too). Repentance's menu actions are the PC's moved up by seven (confirm 14
 * and back 15 stay): left 27, right 28, up 29, down 30, the shoulders 31 and
 * 32 - the options menu at 0x667e84 asks 31, 32, 27, 28, 29, 30 in a row, as
 * the PC's asks 24, 25, 20 .. 23.
 */
#define MENU_INPUT       0xac5528
#define FN_MENU_ACTION   0x8b6600
#define FN_MENU_HELD     0x8b64f8       /* the same, held: vtable +0x48 beside +0x50 */
#define ACTION_CONFIRM   0x0e
#define ACTION_LEFT      0x1b
#define ACTION_RIGHT     0x1c
#define ACTION_UP        0x1d
#define ACTION_DOWN      0x1e
#define ACTION_LT        0x1f
#define ACTION_RT        0x20
#define GAME_DIFFICULTY  0x24f9d8       /* 2 and 3: Greed and Greedier */

/*
 * THE CARD'S LOOK (the owner, 18.09.2026, after the second pass; the tabs
 * from the mock-ups approved 02.10.2026):
 *
 *   - the heading is the game's own hand with the stroke under it, made on
 *     the desk exactly as the Tweaks title is (tools/itemspawn_title.py), one
 *     picture per language set of the menus - the card is called ITEM SPAWN,
 *     as the strap says;
 *   - under it the four tabs, the words in the same hand
 *     (tools/spawner_tabs.py): the tab shown in full ink with the stroke under
 *     it, the others pale;
 *   - the names are the description face, rim and all, half as large again,
 *     in white, as the panel writes them; the rim is what makes them readable
 *     on the paper;
 *   - a picture is 24 units (the panel's item frame), a row the taller of the
 *     two with some air;
 *   - the rows SCROLL, in units rather than rows: a finger drags them, lets
 *     go and they run on and slow down, and a row crossing the top or bottom
 *     of the list fades out on its way rather than popping;
 *   - a thing given flashes its row and plays the item jingle.
 *
 * The card is 420 wide in every language (approved 04.10.2026), never wider
 * than the screen less 60; 236 tall.
 */
#define CARD_W_LEAST 320.0f
#define CARD_W_MOST  420.0f
#define CARD_H     236.0f
#define CARD_PAD   16.0f
#define TEXT_SCALE 1.5f                 /* twice was huge (the owner, 19.09.2026) */
#define PICTURE    24.0f                /* the item frame, 32 at 75% (eid_inline_icons.anm2) */
#define PICTURE_GAP 8.0f                /* between the picture and the name */
#define ROW_AIR    4.0f
#define CARD_SCALE 1.5f                 /* a card's atlas frame is 16 x 20 at 75%: 18 x 22 */
#define PICTURES   10                   /* item pictures of our own - more than the card shows */
#define FLASH_FRAMES 14
#define SOUND_POWERUP 128               /* Feedback/power up1.wav: the item jingle (sounds.xml) */
#define FN_PLAY_SOUND 0x671c8c          /* (manager, id, w2, w3, volume, pitch) */
#define PALE       0.4f                 /* a tab not shown, a room you cannot go to now */
#define HEAD_AIR   7.0f                 /* under a heading of the enemies */
#define RULE_ALPHA 0.45f
#define ROOM_GAP   5.0f                 /* the room's icon to its name */
#define COL_GAP    14.0f                /* between the teleport's columns */
/*
 * "You are here" (approved 04.10.2026, variant A): the description face's own
 * triangles, white with its rim and at the text's 1.5 - before the floor, after
 * the room. The floor's column: the triangle and 2 of air, the game's stage
 * icon (progress.anm2 "Levels", 26 x 18 at 100 %), 5, the name.
 */
#define HERE_FLOOR "\xe2\x96\xb6"       /* U+25B6 */
#define HERE_ROOM  "\xe2\x97\x80"       /* U+25C0 */
#define HERE_AIR   4.0f                 /* a room's name to its triangle */
#define ARROW_AIR  2.0f
#define STAGE_GAP  5.0f
#define text_mark_w(s) shim_eid_width_scaled((s), TEXT_SCALE)
#define CHAPTER_AIR 6.0f

/* the scroll: a finger's own units, then the run-on */
#define DRAG_UNITS 6.0f                 /* travel before a finger is a drag, not a tap */
#define FRICTION   0.92f
#define STILL      0.05f
#define FOLLOW     0.35f                /* how much of the way to a pad's row per frame */
#define FADE_UNITS 10.0f                /* a row fades over this much of the edge */

float shim_screen_width(void);
float shim_screen_height(void);
void shim_eid_write_scaled(const char *text, float x, float y, float scale,
                           float r, float g, float b, float a);
float shim_eid_width_scaled(const char *text, float scale);
float shim_eid_line_height(void);
void shim_eid_marker_faded(const char *name, float x, float y, float scale, float alpha);
int shim_eid_item_picture(unsigned char *s, signed char *built, int id, int trinket);
void shim_eid_item_picture_draw(unsigned char *s, float x, float y, float alpha);
const char *shim_eid_name_of(unsigned kind, unsigned id);
int shim_picture_ready(unsigned char *sprite, signed char *state, const char *anm2);
void shim_picture_draw(unsigned char *sprite, const char *anim, int frame, float x, float y, float alpha);
const char *shim_tweaks_set_suffix(void);

/* the teleport (spawner_teleport.c) */
int shim_teleport_room_state(int type, int subtype);   /* 0 not on this floor, 1 there, 2 you are in it, -1 never */
int shim_teleport_floor_here(void);         /* the SP_FLOOR row of the floor now, or -1 */
void shim_teleport_ask_room(int type, int subtype);
void shim_teleport_ask_floor(int stage, int stage_type);
void shim_teleport_between_frames(void);

/* the stats (spawner_stats.c) */
int shim_stats_players(unsigned long *out, int max);
int shim_stats_player_type(unsigned long p);
float shim_stats_value(unsigned long p, int s);
void shim_stats_limits(int s, float *least, float *most);
float shim_stats_set(unsigned long p, int s, float target);
float shim_stats_shot_step(unsigned long p, int dir);
int shim_stats_reset(unsigned long p);
int shim_stats_can_reset(unsigned long p);

#define ENEMY_ROWS ((int) SP_ENEMIES + SP_HEADS)

static int g_dialog = -1;                    /* the id of the card's dialog */
static volatile int g_want_keyboard;

/* where the card was drawn, for the finger: written by the draw, read by touch */
static float g_card_x, g_card_y, g_card_w, g_card_h;
static float g_card_want_w = CARD_W_LEAST;   /* the width asked for at card_up */
static float g_tabs_top, g_tabs_bottom, g_query_bottom, g_split;

static int g_flash_row = -1;
static int g_flash_left;
static int g_tab_flash;                      /* frames left of a tab's flash */

/* the heading pictures, one per language set */
#define SPRITE_BYTES 0x158
static unsigned char g_title[TW_SETS_MAX][SPRITE_BYTES] __attribute__((aligned(16)));
static signed char g_title_state[TW_SETS_MAX];

static int set_now(void) {
    int set = shim_tweaks_set_index();
    return set < 0 || set >= TW_SETS_MAX ? 0 : set;
}

/* ------------------------------------------------------------ the lists */
/*
 * A list of rows that scrolls: ITEMS' results, ENEMIES (with their headings),
 * the teleport's rooms and its floors. Each row's top inside the list is
 * worked out once, when the rows are built, so rows may differ in height (a
 * heading, the air between chapters) and every place is on a whole unit -
 * a row half a unit off would shimmer as it scrolls.
 */
typedef struct {
    int count;                               /* rows */
    int chosen;
    float scroll, velocity, target;          /* target < 0: none */
    float top, bottom;                       /* its window on the canvas, as last drawn */
    float *pos;                              /* each row's top in the list; pos[count] the end */
    int room;
} list_t;

#define L_ITEMS   0
#define L_ENEMIES 1
#define L_ROOMS   2
#define L_FLOORS  3
#define L_STATS   4
#define LISTS     5

#define STAT_ROWS_MAX 48

static float g_pos_items[ITEM_ROWS_MAX + 1];
static float g_pos_enemies[600 + 1];
static float g_pos_rooms[64 + 1];
static float g_pos_floors[96 + 1];
static float g_pos_stats[STAT_ROWS_MAX + 1];
static list_t g_list[LISTS] = {
    { 0, 0, 0, 0, -1, 0, 0, g_pos_items, ITEM_ROWS_MAX },
    { 0, 0, 0, 0, -1, 0, 0, g_pos_enemies, 600 },
    { 0, 0, 0, 0, -1, 0, 0, g_pos_rooms, 64 },
    { 0, 0, 0, 0, -1, 0, 0, g_pos_floors, 96 },
    { 0, 0, 0, 0, -1, 0, 0, g_pos_stats, STAT_ROWS_MAX },
};

/*
 * STATS' rows (the owner, 07.10.2026, mock-ups v2): the six stats, the
 * counters, the health as the character can hold it, each section under its
 * heading; one list that scrolls, as the other tabs do.
 */
#define SR_HEAD    0                         /* id: SP_SEC_* */
#define SR_STAT    1                         /* id: 0..5, the HUD's order */
#define SR_COUNTER 2                         /* id: 0 coins, 1 bombs, 2 keys */
#define SR_HEALTH  3                         /* id: SP_H_*, other: the other body's (The Soul / The Forgotten) */
typedef struct { unsigned char kind, id, other; } srow_t;
static srow_t g_srow[STAT_ROWS_MAX];

static short g_enemy_row[600];               /* >= 0 an SP_ENEMY, < 0 the heading -1 - group */
static short g_enemy_sub[600];               /* -1 the enemy itself; else its sub-row (see the champions) */
static float g_enemy_real[600];              /* a row's own top in the list (pos[] is clipped while opening) */
static float g_enemy_h[600];                 /* and its own height */
static unsigned char g_floor_row[96];        /* an SP_FLOOR */
static int g_col;                            /* the teleport's column: 0 rooms, 1 floors, 2 the foot */
static int g_tp_button, g_tp_from;           /* the foot's button under the cursor, and the column it came from */
static float g_tp_foot, g_tp_split;          /* the foot's top and where its two buttons meet, as drawn */
static int g_armed = -1;                     /* a floor tapped once, waiting for the second */

static float row_h(const list_t *l, int k) { return l->pos[k + 1] - l->pos[k]; }

static float line_h(void) { return shim_eid_line_height() * TEXT_SCALE; }

static float item_step(void) {
    float lh = line_h();
    return floorf((lh > PICTURE ? lh : PICTURE) + ROW_AIR + 0.5f);
}

/*
 * An enemy's picture (approved 04.10.2026, v6): at 100 % and its own size - a
 * Boss Bars icon as drawn, a body in its resting pose, brought down only by a
 * whole factor when over 48 (tools/spawner_enemies.py). The icons stand
 * centred in a column 64 wide (Mega Satan's is 63) and every row grows to its
 * own icon.
 */
#define ENEMY_COLUMN ((float) SP_ENEMY_COLUMN)
#define SUB_INDENT   18.0f                  /* a champion's row under its monster */

static float enemy_row_step(float icon_h) {
    float lh = line_h();
    return floorf((lh > icon_h ? lh : icon_h) + ROW_AIR + 0.5f);
}

static float room_step(void) {
    float lh = line_h(), icon = (float) SP_ROOM_ICON;
    return floorf((lh > icon ? lh : icon) + 2.0f + 0.5f);
}

static float floor_step(void) { return floorf(line_h() + 4.0f + 0.5f); }

static int row_choosable(int l, int k) {
    if (k < 0 || k >= g_list[l].count) return 0;
    if (l == L_ITEMS) return g_rows[k].kind != KIND_HEAD;
    if (l == L_STATS) return g_srow[k].kind != SR_HEAD;
    if (l != L_ENEMIES) return 1;
    return g_enemy_row[k] >= 0 && g_list[l].pos[k + 1] > g_list[l].pos[k];   /* a sub-row still closed: no */
}

static void list_reset(list_t *l) {
    l->scroll = 0.0f;
    l->velocity = 0.0f;
    l->target = -1.0f;
}

/* the most the list can scroll: its length less the window */
static float list_most(const list_t *l) {
    float most = l->pos[l->count] - (l->bottom - l->top);
    return most > 0.0f ? most : 0.0f;
}

/* a pad's choice: the window follows the chosen row, at its own pace */
static void list_follow(list_t *l) {
    if (l->chosen < 0 || l->chosen >= l->count || l->bottom <= l->top) return;
    float top = l->pos[l->chosen], bottom = l->pos[l->chosen + 1];
    float window = l->bottom - l->top;
    /* walking up into a section brings its heading along (the ITEMS list, STATS) */
    int li = (int) (l - g_list);
    if (l->chosen > 0 && li >= 0 && li < LISTS && (li == L_ITEMS || li == L_STATS) && !row_choosable(li, l->chosen - 1))
        top = l->pos[l->chosen - 1];
    if (top < l->scroll) l->target = top;
    else if (bottom > l->scroll + window) l->target = bottom - window;
    l->velocity = 0.0f;
}

/* the first choosable row from k on, the way dir goes */
static int list_settle(int l, int k, int dir) {
    while (k >= 0 && k < g_list[l].count && !row_choosable(l, k)) k += dir;
    return row_choosable(l, k) ? k : -1;
}

static void list_walk(int l, int dir) {
    list_t *t = &g_list[l];
    int k = list_settle(l, t->chosen + dir, dir);
    if (k >= 0) { t->chosen = k; list_follow(t); }
}

/* the row under a canvas y, or -1 */
static int list_at(const list_t *l, float gy) {
    if (gy < l->top || gy >= l->bottom) return -1;
    float y = gy - l->top + l->scroll;
    for (int k = 0; k < l->count; k++)
        if (y >= l->pos[k] && y < l->pos[k + 1]) return k;
    return -1;
}

/* the run-on after a finger, the close on a pad's row, the bounds */
static void list_run(list_t *l) {
    if (l->target >= 0.0f) {
        l->scroll += (l->target - l->scroll) * FOLLOW;
        if (fabsf(l->target - l->scroll) < 0.5f) { l->scroll = l->target; l->target = -1.0f; }
    } else if (l->velocity != 0.0f) {
        l->scroll += l->velocity;
        l->velocity *= FRICTION;
        if (fabsf(l->velocity) < STILL) l->velocity = 0.0f;
    }
    float most = list_most(l);
    if (l->scroll < 0.0f) { l->scroll = 0.0f; l->velocity = 0.0f; }
    if (l->scroll > most) { l->scroll = most; l->velocity = 0.0f; }
}

/* a row on its way over the top or the bottom fades with what is left of it inside */
static float row_alpha(const list_t *l, float y, float h) {
    float a = 1.0f;
    if (y < l->top) a = 1.0f - (l->top - y) / FADE_UNITS;
    else if (y + h > l->bottom) a = 1.0f - (y + h - l->bottom) / FADE_UNITS;
    return a < 0.0f ? 0.0f : (a > 1.0f ? 1.0f : a);
}

/* ------------------------------------------------------------ the rows of each tab */
static void row_put(unsigned char kind, unsigned id) {
    if (g_rows_n >= ITEM_ROWS_MAX) return;
    g_rows[g_rows_n].kind = kind;
    g_rows[g_rows_n].id = (unsigned short) id;
    g_rows[g_rows_n].score = 0;
    g_rows_n++;
}

static void all_of(unsigned id, const char *name, const char *english, void *ctx) {
    (void) name; (void) english;
    int how = *(const int *) ctx;
    if ((how == KIND_ITEM || how == KIND_TRINKET) && !real_id((unsigned char) how, id)) return;
    switch (how) {
    case KIND_ITEM:    row_put(KIND_ITEM, id); break;
    case KIND_TRINKET: row_put(KIND_TRINKET, id); row_put(KIND_TRINKET, id | TRINKET_GOLD); break;
    case KIND_PILL:
        if (id >= 1 && id <= 50) { row_put(KIND_PILL, id - 1); row_put(KIND_PILL, (id - 1) | PILL_HORSE); }
        break;
    default: {                                /* cards: -1 the cards, -2 the runes (the game's own word for each) */
        int rune = id <= EID_CARD_MAX && EID_CARD_KIND[id] == 2;
        if (rune == (how == -2)) row_put(KIND_CARD, id);
        break;
    }
    }
}

static void pickups_of(int group) {
    for (unsigned i = 0; i < SP_PICKUPS; i++)
        if (SP_PICKUP[i].group == group) row_put(KIND_PICKUP, i);
}

/* everything, in the order of the mock-up v2, every section under its heading */
static void the_whole_list(void) {
    static const struct { int section, group; } PICKUP_SECTIONS[] = {
        { SP_SEC_KEYS, SP_G_KEYS }, { SP_SEC_BOMBS, SP_G_BOMBS }, { SP_SEC_COINS, SP_G_COINS },
        { SP_SEC_HEARTS, SP_G_HEARTS }, { SP_SEC_BATTERIES, SP_G_BATTERIES },
    };
    int how;
    g_rows_n = 0;
    row_put(KIND_HEAD, SP_SEC_ITEMS);
    how = KIND_ITEM;
    shim_eid_each_name(KIND_ITEM, all_of, &how);
    for (unsigned i = 0; i < sizeof(PICKUP_SECTIONS) / sizeof(PICKUP_SECTIONS[0]); i++) {
        row_put(KIND_HEAD, (unsigned) PICKUP_SECTIONS[i].section);
        pickups_of(PICKUP_SECTIONS[i].group);
    }
    row_put(KIND_HEAD, SP_SEC_CARDS);
    how = -1;
    shim_eid_each_name(KIND_CARD, all_of, &how);
    row_put(KIND_HEAD, SP_SEC_RUNES);
    how = -2;
    shim_eid_each_name(KIND_CARD, all_of, &how);
    row_put(KIND_HEAD, SP_SEC_PILLS);
    how = KIND_PILL;
    shim_eid_each_name(EID_PILLS, all_of, &how);
    row_put(KIND_PILL, PILL_GOLD);
    row_put(KIND_PILL, PILL_GOLD | PILL_HORSE);
    row_put(KIND_HEAD, SP_SEC_TRINKETS);
    how = KIND_TRINKET;
    shim_eid_each_name(KIND_TRINKET, all_of, &how);
    row_put(KIND_HEAD, SP_SEC_LOOT);
    pickups_of(SP_G_LOOT);
}

static float section_h(int set, int section) { return (float) SP_SECTION[set][section].h + HEAD_AIR; }

static void items_rows(void) {
    list_t *l = &g_list[L_ITEMS];
    const char *q = g_queries[0];
    while (*q == ' ') q++;
    held_refresh();
    if (*q) {
        g_rows_n = 0;
        for (int i = 0; i < g_hit_count; i++) row_put(g_hits[i].kind, g_hits[i].id);
    } else {
        the_whole_list();
        if (g_only_held) {                   /* ONLY HELD: the ITEMS heading and the collectibles held */
            int w = 0;
            for (int i = 0; i < g_rows_n; i++) {
                const hit_t *r = &g_rows[i];
                if ((r->kind == KIND_HEAD && r->id == SP_SEC_ITEMS) || (r->kind == KIND_ITEM && held_of(r->id)))
                    g_rows[w++] = *r;
            }
            g_rows_n = w;
        }
    }
    int set = set_now();
    float step = item_step(), at = 0.0f;
    l->count = g_rows_n < l->room ? g_rows_n : l->room;
    for (int k = 0; k < l->count; k++) {
        l->pos[k] = at;
        at += g_rows[k].kind == KIND_HEAD ? section_h(set, g_rows[k].id) : step;
    }
    l->pos[l->count] = at;
    l->chosen = list_settle(L_ITEMS, 0, 1);
    list_reset(l);
    g_line_focus = 0;
}

/*
 * ENEMIES: with nothing typed, the whole bestiary under its headings (the
 * training dummy first, then bosses, minibosses, monsters); with a query, the
 * best matches alone, most likely first.
 *
 * CHAMPIONS (the owner, approved 04.10.2026): the first tap on a monster that
 * can be a champion (entities2.xml champion="1") slides out under it all 26
 * champion colours; on a boss with colours of its own (bosscolors.xml), those.
 * The second tap on the monster spawns it as before; a tap on a sub-row spawns
 * that champion or that colour. One monster is open at a time.
 *
 * The slide: the sub-list's height is shown up to its progress (ease-out over
 * OPEN_FRAMES), the rows below ride on it, and a sub-row is cut at the opening
 * edge and fades in as the edge passes it. pos[] keeps the list's own
 * geometry for the scroll and the finger (a row not yet uncovered has no
 * height there); g_enemy_real[] is where a row really stands.
 */
#define OPEN_FRAMES 9

static int g_open_enemy = -1;                /* the SP_ENEMY whose sub-list is out, -1 none */
static int g_open_tick;                      /* frames into the slide */
static short g_base_row[600];                /* the list without sub-rows: enemy, or the heading -1 - group */
static int g_base_count;

static int subs_of(int e) {
    if (e < 0 || e >= (int) SP_ENEMIES) return 0;
    return (SP_ENEMY[e].champ >= 0 ? 26 : 0) + SP_ENEMY[e].colours_n;
}

/* a sub-row: a champion colour (0..25) or, after them, a boss colour */
static int sub_is_champion(int e, int j) { return SP_ENEMY[e].champ >= 0 && j < 26; }
static int sub_colour(int e, int j) { return SP_ENEMY[e].colours + (j - (SP_ENEMY[e].champ >= 0 ? 26 : 0)); }

/*
 * THE CROWN AND THE SKULL (the owner, 20th pass: "Crown looks like the plain
 * enemy"). Neither colours the body (KColor nought, Color plain): the game
 * hangs an overlay on it. Every frame (0x68bec0) a crowned champion (22) plays
 * "Crown", a skulled one (23) "Skull" of gfx/StatusEffects.anm2 on its status
 * sprite (+0xd88, 0x690d94) - unless a status effect of its own (charm, fear...)
 * takes the place - and the render (0x69828c) draws it after the body, at the
 * body's OverlayEffect null (the raw keyframe of the body's animation; the
 * tool finds it in the icon's pose), plain white, not grown with the body. A
 * body without that null gets none. Both animations loop: the crown glints
 * (18 frames), the skull bobs (12). Their ink from the pivot (SP_OVERLAY,
 * measured by the tool in statuseffects.png over every frame) - the row grows
 * to hold it, as rows grow to their icon.
 *
 * A body brought down by 2 wears the overlay brought down by 2 as well (the
 * owner, 05.10.2026: "make it even" - the same size against the body as on
 * the full-size ones): the tool bakes it the way it brought the body down,
 * port_enemy_icons "CrownHalf" / "SkullHalf", drawn at 100 %.
 */
/*
 * LARGE (the owner, 21st pass: "make it a little bigger"; "the game draws a
 * clean 1.5, so can we"). MakeChampion (0x6827b8) sets the body's scale
 * +0xcc4: 0.5 for TINY (18), 1.5 for LARGE (19), 1.15 for the rest, and the
 * render grows the sprite by it on the GPU (SpriteScale +0x12c, the sprite's
 * own +0xdc). So the card does the same: the LARGE row draws the body - and its
 * mask and luminance passes - through the sprite's own scale of 1.5, no baked
 * copy. At 6 pixels a unit a texel is then exactly 9 pixels; at an odd count
 * it is as uneven as the game's own LARGE champions in a run. The row grows to
 * the whole grown body; the body is scaled about its frame's top left, which
 * the layout puts on whole units (the row's own top is floored while the list
 * scrolls), so the picture never sits between scene pixels and does not
 * shimmer. Alone in its cell, where the game's origin would put it is only a
 * shift of the whole picture, and the row centres it as every row does.
 */
#define CHAMP_LARGE     19
#define LARGE_SCALE     1.5f                 /* 0x6827f4 */
#define CHAMP_PULSATING 21
#define CHAMP_CROWN     22
#define CHAMP_SKULL     23

/* the overlay a champion row wears, or -1 */
static int sub_overlay(int e, int j) {
    if (!sub_is_champion(e, j) || !SP_CHAMP_ART[SP_ENEMY[e].champ].overlay) return -1;
    return j == CHAMP_CROWN ? 0 : j == CHAMP_SKULL ? 1 : -1;
}

/* which of SP_OVERLAY[o]: the game's own, or the half one for a half-size body */
static int overlay_half(int c) { return SP_CHAMP_ART[c].shrink == 2; }

/* a sub-row's picture: its size, and the icon's top left inside it (the overlay may stand out) */
static void sub_box(int e, int j, float *w, float *h, float *dx, float *dy) {
    *dx = *dy = 0.0f;
    if (sub_is_champion(e, j)) {
        int c = SP_ENEMY[e].champ, tiny = j == SP_CHAMP_TINY;
        *w = tiny ? SP_CHAMP_ART[c].tiny.w : SP_CHAMP_ART[c].base.w;
        *h = tiny ? SP_CHAMP_ART[c].tiny.h : SP_CHAMP_ART[c].base.h;
        if (j == CHAMP_LARGE) {                   /* the body drawn at 1.5: its row holds all of it */
            *w = ceilf(*w * LARGE_SCALE);
            *h = ceilf(*h * LARGE_SCALE);
        }
        int o = sub_overlay(e, j);
        if (o >= 0) {
            float ox = (float) SP_CHAMP_ART[c].ox, oy = (float) SP_CHAMP_ART[c].oy;
            int half = overlay_half(c);
            float x0 = fminf(0.0f, ox + (float) SP_OVERLAY[o][half].x0);
            float y0 = fminf(0.0f, oy + (float) SP_OVERLAY[o][half].y0);
            float x1 = fmaxf(*w, ox + (float) SP_OVERLAY[o][half].x1);
            float y1 = fmaxf(*h, oy + (float) SP_OVERLAY[o][half].y1);
            *dx = -x0; *dy = -y0;
            *w = x1 - x0; *h = y1 - y0;
        }
    } else {
        int c = sub_colour(e, j);
        *w = SP_BOSS_COLOUR[c].size.w;
        *h = SP_BOSS_COLOUR[c].size.h;
    }
}

static void sub_size(int e, int j, float *w, float *h) {
    float dx, dy;
    sub_box(e, j, w, h, &dx, &dy);
}

static float ease_out(float t) {
    t = t < 0.0f ? 0.0f : (t > 1.0f ? 1.0f : t);
    return 1.0f - (1.0f - t) * (1.0f - t) * (1.0f - t);
}

/* the rows and their places from g_base_row and the open monster, as far as it is open */
static void enemies_layout(void) {
    list_t *l = &g_list[L_ENEMIES];
    int set = set_now(), n = 0;
    float at = 0.0f;
    float progress = ease_out((float) g_open_tick / OPEN_FRAMES);
    for (int b = 0; b < g_base_count && n < l->room; b++) {
        int e = g_base_row[b];
        float h = e >= 0 ? enemy_row_step((float) SP_ENEMY[e].h) : (float) SP_HEAD[set][-1 - e].h + HEAD_AIR;
        g_enemy_row[n] = (short) e;
        g_enemy_sub[n] = -1;
        g_enemy_real[n] = at;
        g_enemy_h[n] = h;
        l->pos[n] = at;
        n++;
        at += h;
        if (e < 0 || e != g_open_enemy) continue;
        int subs = subs_of(e);
        float full = 0.0f;
        for (int j = 0; j < subs; j++) {
            float w, sh;
            sub_size(e, j, &w, &sh);
            full += enemy_row_step(sh);
        }
        float shown = floorf(full * progress + 0.5f), top = at, y = at;
        for (int j = 0; j < subs && n < l->room - 1; j++) {
            float w, sh;
            sub_size(e, j, &w, &sh);
            float rh = enemy_row_step(sh);
            g_enemy_row[n] = (short) e;
            g_enemy_sub[n] = (short) j;
            g_enemy_real[n] = y;
            g_enemy_h[n] = rh;
            l->pos[n] = y < top + shown ? y : top + shown;
            n++;
            y += rh;
        }
        at = top + shown;
    }
    l->pos[n] = at;
    l->count = n;
}

static void enemies_rows(void) {
    list_t *l = &g_list[L_ENEMIES];
    int n = 0;
    const char *q = g_queries[1];
    while (*q == ' ') q++;
    if (*q) {
        hit_t hits[HITS_MAX];
        int found = shim_spawner_enemy_search(g_queries[1], hits, HITS_MAX);
        for (int i = 0; i < found && n < l->room; i++) g_base_row[n++] = (short) hits[i].id;
    } else {
        int group = -1;
        for (unsigned i = 0; i < SP_ENEMIES && n < l->room - 1; i++) {
            if (SP_ENEMY[i].group != group) {
                group = SP_ENEMY[i].group;
                g_base_row[n++] = (short) (-1 - group);
            }
            g_base_row[n++] = (short) i;
        }
    }
    g_base_count = n;
    g_open_enemy = -1;
    g_open_tick = 0;
    enemies_layout();
    l->chosen = list_settle(L_ENEMIES, 0, 1);
    list_reset(l);
}

/* open a monster's sub-list (closing another), the chosen row kept */
static void enemy_open(int e) {
    list_t *l = &g_list[L_ENEMIES];
    int chosen_e = l->chosen >= 0 ? g_enemy_row[l->chosen] : -1;
    int chosen_sub = l->chosen >= 0 ? g_enemy_sub[l->chosen] : -1;
    g_open_enemy = subs_of(e) ? e : -1;
    g_open_tick = 0;
    enemies_layout();
    for (int k = 0; k < l->count; k++)
        if (g_enemy_row[k] == chosen_e && g_enemy_sub[k] == chosen_sub) { l->chosen = k; break; }
}

/* once a frame: the slide goes on */
static void enemies_slide(void) {
    if (g_open_enemy < 0 || g_open_tick >= OPEN_FRAMES) return;
    g_open_tick++;
    enemies_layout();
}

static int greed_now(void) {
    unsigned long b = shim_image_base();
    unsigned long game = b ? *(const unsigned long *) (b + GAME_PTR) : 0;
    int d = game ? *(const int *) (game + GAME_DIFFICULTY) : 0;
    return d == 2 || d == 3;
}

/*
 * TELEPORT: every room type in one column (the owner, 30.09.2026: full lists of
 * rooms and floors - some items reach anywhere, even in a challenge), and
 * every floor of the mode played in the other, chapters a little apart. The
 * floors open on the row above the floor you are on.
 */
static void teleport_rows(void) {
    list_t *r = &g_list[L_ROOMS], *f = &g_list[L_FLOORS];
    float rstep = room_step(), fstep = floor_step();
    r->count = SP_ROOMS < r->room ? SP_ROOMS : r->room;
    for (int k = 0; k <= r->count; k++) r->pos[k] = (float) k * rstep;
    if (r->chosen < 0 || r->chosen >= r->count) r->chosen = 0;
    list_reset(r);

    int greed = greed_now(), n = 0, chapter = -1;
    float at = 0.0f;
    for (int i = 0; i < SP_FLOORS && n < f->room; i++) {
        if ((SP_FLOOR[i].mode == 1) != greed) continue;
        if (chapter >= 0 && SP_FLOOR[i].chapter != chapter) at += CHAPTER_AIR;
        chapter = SP_FLOOR[i].chapter;
        f->pos[n] = at;
        g_floor_row[n++] = (unsigned char) i;
        at += fstep;
    }
    f->pos[n] = at;
    f->count = n;
    list_reset(f);
    int here = shim_teleport_floor_here(), row = -1;
    for (int k = 0; k < n && row < 0; k++)
        if (g_floor_row[k] == here) row = k;
    f->chosen = row >= 0 ? row : 0;
    if (row > 0) f->scroll = f->pos[row - 1];
    g_armed = -1;
}

static void rows_rebuilt(int tab) {
    if (tab == TAB_ITEMS) items_rows();
    else if (tab == TAB_ENEMIES) enemies_rows();
    else if (tab == TAB_TELEPORT) teleport_rows();
}

/* ------------------------------------------------------------ the card up and down */
static unsigned long manager(void) {
    unsigned long b = shim_image_base();
    return b ? *(const unsigned long *) (b + MANAGER_PTR) : 0;
}

static unsigned long dialog_of(int id) {
    unsigned long m = manager();
    if (!m || id < 0) return 0;
    for (unsigned long node = *(const unsigned long *) (m + 8); node && node != m;
         node = *(const unsigned long *) (node + 8)) {
        unsigned long d = *(const unsigned long *) (node + 0x10);
        if (d && *(const int *) d == id) return d;
    }
    return 0;
}

/* the width the teleport's two columns ask for in this language */
static float g_rooms_w;

static void card_measure(void) {
    int set = set_now();
    float rooms = 0.0f;
    for (int i = 0; i < SP_ROOMS; i++) {
        float w = shim_eid_width_scaled(SP_ROOM[i].name[set], TEXT_SCALE);
        if (w > rooms) rooms = w;
    }
    /*
     * The approved v6 (04.10.2026): ONE width, 420, in every language - the
     * widest set's - and every mark inside its own column (the room's after
     * its name, the floor's before its icon, the second tap's " ?" after the
     * name), the few names that would not fit shortened by the tools
     * (spawner_rooms.py, spawner_floors.py; checked on the longest names of
     * all seven sets, the least to spare 0.5). Never wider than the screen
     * less 60.
     */
    rooms += (float) SP_ROOM_ICON + ROOM_GAP + HERE_AIR + text_mark_w(HERE_ROOM);
    float want = CARD_W_MOST;
    float most = shim_screen_width() - 60.0f;
    if (want > most) want = most;
    if (want < CARD_W_LEAST) want = CARD_W_LEAST;
    g_card_want_w = floorf(want);
    g_rooms_w = floorf(rooms);
}

static void ime_for_tab(void);

static void card_up(void) {
    unsigned long b = shim_image_base(), m = manager();
    if (!b || !m || g_dialog >= 0) return;
    card_measure();
    float h = shim_screen_height() - 24.0f;
    unsigned char desc[DESC_BYTES] __attribute__((aligned(16)));
    memset(desc, 0, sizeof(desc));
    /*
     * Any size will do on any canvas: the paper of every dialog is put on
     * whole units where the game places it (loader.c, THE POPUP PAPER ON WHOLE
     * UNITS), and shim_spawner_draw lays the content out with the same floor.
     */
    float size[2] = { g_card_want_w, h < CARD_H ? floorf(h) : CARD_H };
    ((void (*)(void *, unsigned long, void *, float *)) (b + FN_DIALOG_FILL))(
        desc, m + MGR_DIALOG_FONT, NULL, size);
    *(int *) (desc + DESC_BACK) = 0;         /* Back closes it */
    g_dialog = ((int (*)(unsigned long, void *)) (b + FN_DIALOG_ADD))(m, desc);
    ((void (*)(void *)) (b + FN_DIALOG_FREE))(desc);

    g_open = 1;
    g_queries[0][0] = g_queries[1][0] = 0;
    g_hit_count = 0;
    for (int i = 0; i < LISTS; i++) { g_list[i].count = 0; g_list[i].chosen = -1; list_reset(&g_list[i]); }
    g_stat_focus = FOCUS_LIST;               /* STATS' rows are made for the body shown at its first draw */
    g_line_focus = 0;
    g_col = 0;
    g_list[L_ROOMS].chosen = 0;
    for (int t = 0; t < TABS; t++) rows_rebuilt(t);
    g_flash_row = -1;
    g_tab_flash = 0;
    ime_for_tab();
}

static void stats_stop_typing(void);
static void pend_drop(void);                 /* STATS: a value the arrows set, not yet applied */

/* the card's dialog as it slides away: the teleport waits for it to be gone */
static int g_closing = -1;

int shim_spawner_card_gone(void) {
    if (g_closing >= 0 && !dialog_of(g_closing)) g_closing = -1;
    return g_closing < 0;
}

static void card_down(void) {
    unsigned long b = shim_image_base(), m = manager();
    int id = g_dialog;
    g_closing = id;
    if (b && m && dialog_of(id))
        ((void (*)(unsigned long, int *)) (b + FN_DIALOG_CLOSE))(m, &id);
    g_dialog = -1;
    g_open = 0;
    g_armed = -1;
    stats_stop_typing();
    pend_drop();
    shim_ime_show(0);
}

static int menu_action(int action) {
    unsigned long b = shim_image_base();
    return b && ((int (*)(unsigned long, int, int, void *)) (b + FN_MENU_ACTION))(
        b + MENU_INPUT, action, -1, NULL);
}

/* the same menu action, held rather than pressed this frame (0x8b64f8, see value_hold) */
static int menu_held(int action) {
    unsigned long b = shim_image_base();
    return b && (((int (*)(unsigned long, int, int, void *)) (b + FN_MENU_HELD))(
        b + MENU_INPUT, action, -1, NULL) & 1);
}

static void play_jingle(void) {
    unsigned long b = shim_image_base(), m = manager();
    if (b && m)
        ((void (*)(unsigned long, int, int, int, float, float)) (b + FN_PLAY_SOUND))(
            m, SOUND_POWERUP, 0, 0, 1.0f, 1.0f);
}

/* the thing given: its row flashes and the item jingle plays */
static void given(int row) {
    g_flash_row = row;
    g_flash_left = FLASH_FRAMES;
    play_jingle();
}

/* ------------------------------------------------------------ the tabs */
/* the soft keyboard where there is a line to type into: ITEMS, ENEMIES, a stat being typed */
static int g_typing;                          /* STATS: a number is being typed */

static void ime_for_tab(void) {
    shim_ime_show(g_tab == TAB_ITEMS || g_tab == TAB_ENEMIES || (g_tab == TAB_STATS && g_typing));
}

static void tab_show(int tab) {
    if (tab < 0 || tab >= TABS || tab == g_tab) return;
    stats_stop_typing();
    pend_drop();
    g_tab = tab;
    g_armed = -1;
    g_flash_row = -1;
    g_tab_flash = 6;
    if (tab == TAB_TELEPORT) teleport_rows();       /* the floor and its rooms may have changed */
    ime_for_tab();
}

static void tab_step(int dir) { tab_show((g_tab + TABS + dir) % TABS); }

/* ------------------------------------------------------------ the stats tab */
/*
 * STATS (the owner, 02.10.2026; the research notes of that day, stats editor,
 * and the
 * mock-ups v2 approved 07.10.2026): the player, then ONE list that scrolls -
 * the six stats as the HUD lists them, the counters (coins, bombs, keys), the
 * health as the character can hold it - and the foot: CHARGE ACTIVE ITEMS and
 * RESET. The value typed is the value wanted; the arrows step it. Shot speed
 * has no fraction the game keeps, so its arrows step the game's own count, 0.2
 * or so a step. The counters and the health move by the game's own adders as
 * deltas (spawner_stats.c), so whatever the game refuses is refused here too.
 */
#define STAT_SHOT   4
#define STAT_LUCK   5
#define NOTE_FRAMES 120
#define FOOT_CHARGE 0
#define FOOT_RESET  1
#define SECTION_AIR 6.0f                      /* under a heading of STATS */
#define HEALTH_ROWS_MAX 16
#define TYPE_THE_FORGOTTEN 16
#define TYPE_FORGOTTEN_SOUL 17

int shim_health_rows(unsigned long p, int *rows, int *other, int max);
int shim_health_halves(int row);
int shim_health_get(unsigned long p, int row, int other);
int shim_health_add(unsigned long p, int row, int other, int delta);
int shim_counter_get(unsigned long p, int c);
int shim_counter_add(unsigned long p, int c, int delta);
int shim_charge_actives(unsigned long p);

static const float STAT_STEP[6] = { 0.1f, 0.5f, 0.5f, 0.5f, 0.0f, 1.0f };   /* speed tears damage range - luck */

static int g_foot_button;                     /* FOOT_CHARGE or FOOT_RESET */
static char g_typed[16];
static char g_note[128];
static int g_note_left;
static float g_stat_foot, g_foot_split;       /* the draw's layout, for the finger */
static float g_arrow_l0, g_arrow_l1, g_arrow_r0, g_arrow_r1;      /* the chosen row's arrows */
static unsigned long g_srows_for;             /* the body the rows were made for */
static int g_srows_type = -1, g_srows_sig = -1;

static unsigned long stat_player(void) { return who_player(); }

static void stats_stop_typing(void) {
    if (!g_typing) return;
    g_typing = 0;
    g_typed[0] = 0;
    if (g_open) ime_for_tab();
}

static void note(const char *text) {
    snprintf(g_note, sizeof(g_note), "%s", text);
    g_note_left = NOTE_FRAMES;
}

static float stat_row_h(void) {
    float lh = line_h();
    return floorf((lh > 16.0f ? lh : 16.0f) + 3.5f);
}

/*
 * The rows for the body shown, made again when the body, its character or what
 * it can hold changes (a switch, Lazarus rising, The Forgotten turning into
 * The Soul); the chosen row is kept by what it is.
 */
static void stats_rows(void) {
    unsigned long p = stat_player();
    int hrows[HEALTH_ROWS_MAX], hother[HEALTH_ROWS_MAX];
    int hn = p ? shim_health_rows(p, hrows, hother, HEALTH_ROWS_MAX) : 0;
    int type = p ? shim_stats_player_type(p) : -1, sig = hn;
    for (int i = 0; i < hn; i++) sig = sig * 31 + hrows[i] * 2 + hother[i];
    list_t *l = &g_list[L_STATS];
    if (p == g_srows_for && type == g_srows_type && sig == g_srows_sig && l->count) return;
    srow_t was = { SR_STAT, 2, 0 };            /* damage, as the first mock-up opened */
    if (l->chosen >= 0 && l->chosen < l->count) was = g_srow[l->chosen];
    int fresh = p != g_srows_for || !l->count;
    g_srows_for = p;
    g_srows_type = type;
    g_srows_sig = sig;
    int n = 0;
#define SROW(k, i, o) do { if (n < STAT_ROWS_MAX) { g_srow[n].kind = (k); g_srow[n].id = (unsigned char) (i); \
                               g_srow[n].other = (unsigned char) (o); n++; } } while (0)
    SROW(SR_HEAD, SP_SEC_STATS, 0);
    for (int s = 0; s < 6; s++) SROW(SR_STAT, s, 0);
    SROW(SR_HEAD, SP_SEC_PICKUPS, 0);
    for (int c = 0; c < 3; c++) SROW(SR_COUNTER, c, 0);
    if (hn) {
        SROW(SR_HEAD, SP_SEC_HEALTH, 0);
        for (int i = 0; i < hn; i++) SROW(SR_HEALTH, hrows[i], hother[i]);
    }
#undef SROW
    int set = set_now();
    float step = stat_row_h(), at = 0.0f;
    l->count = n;
    for (int k = 0; k < n; k++) {
        l->pos[k] = at;
        at += g_srow[k].kind == SR_HEAD ? (float) SP_SECTION[set][g_srow[k].id].h + SECTION_AIR : step;
    }
    l->pos[n] = at;
    int chosen = -1;
    for (int k = 0; k < n && chosen < 0; k++)
        if (g_srow[k].kind == was.kind && g_srow[k].id == was.id && g_srow[k].other == was.other) chosen = k;
    l->chosen = chosen >= 0 ? chosen : list_settle(L_STATS, 0, 1);
    if (fresh) list_reset(l);
}

static const srow_t *stat_chosen(void) {
    list_t *l = &g_list[L_STATS];
    return g_stat_focus == FOCUS_LIST && l->chosen >= 0 && l->chosen < l->count
           && g_srow[l->chosen].kind != SR_HEAD ? &g_srow[l->chosen] : NULL;
}

/* a stat's own rule, from the game: its limits, and shot speed's step */
static void stat_hint(int s, char *out, size_t n) {
    float least, most;
    shim_stats_limits(s, &least, &most);
    const char *fmt = SP_STAT_HINT_FMT[set_now()][s];
    switch (s) {
    case 0:  snprintf(out, n, fmt, (double) least, (double) most); break;
    case 1:  snprintf(out, n, fmt, (double) most); break;
    case STAT_SHOT: snprintf(out, n, fmt, 0.2, (double) least); break;
    case STAT_LUCK: snprintf(out, n, "%s", fmt); break;
    default: snprintf(out, n, fmt, (double) least); break;
    }
}

/* the game's answer, said when it is not what was asked: a limit, or the nearest it could make;
   answers 1 when the answer was the game's limit (what it allows) */
static int stat_answer(int s, float asked, float got) {
    if (isnan(got)) return 1;
    char line[128];
    if (fabsf(got - asked) <= 0.006f) return 0;
    float least, most;
    int limit = 0;
    shim_stats_limits(s, &least, &most);
    if (asked < least - 0.005f || asked > most + 0.005f) { stat_hint(s, line, sizeof(line)); limit = 1; }
    else snprintf(line, sizeof(line), "~ %.2f", (double) got);
    if (line[0]) note(line);
    return limit;
}

/*
 * TAKEN (the owner, 07.10.2026, after the report build): a change STATS made
 * says so as the ITEMS give does - the item jingle (sound 128) and the row's
 * flash. A held arrow plays it on the press and then at most every 150 ms
 * (the steps themselves come up to every 33 ms); the flash follows every step.
 * A change the game refused - the value did not move, the note says what it
 * allows - plays nothing.
 */
#define CONFIRM_EVERY_MS 150
static long long now_ms(void);
static long long g_confirmed_at;

static void stat_confirm(int flash_row) {
    g_flash_row = flash_row;
    g_flash_left = FLASH_FRAMES;
    long long now = now_ms();
    if (now - g_confirmed_at >= CONFIRM_EVERY_MS || now < g_confirmed_at) {
        g_confirmed_at = now;
        play_jingle();
    }
}

static void row_confirm(void) { stat_confirm(2000 + g_list[L_STATS].chosen); }

/* a counter's or a heart's value as shown: halves as "2½" */
static void units_shown(int value, int halves, char *out, size_t n) {
    if (!halves) { snprintf(out, n, "%d", value); return; }
    int whole = value / 2, half = value % 2;
    if (value < 0) { snprintf(out, n, "%d", value); return; }
    if (!half) snprintf(out, n, "%d", whole);
    else if (!whole) snprintf(out, n, "\xc2\xbd");
    else snprintf(out, n, "%d\xc2\xbd", whole);
}

static int srow_value(unsigned long p, const srow_t *r) {
    if (r->kind == SR_COUNTER) return shim_counter_get(p, r->id);
    return shim_health_get(p, r->id, r->other);
}

/* the game would not go where it was asked: say what it allows */
static void units_refused(const srow_t *r, int now) {
    char v[24], line[128];
    units_shown(now, r->kind == SR_HEALTH && shim_health_halves(r->id), v, sizeof(v));
    snprintf(line, sizeof(line), SP_LIMIT_FMT[set_now()], v);
    note(line);
}

/* a counter or a heart row by `delta` of its units, through the game: taken, or refused and said */
static void units_add(const srow_t *r, int delta) {
    unsigned long p = stat_player();
    if (!p || !delta) return;
    int before = srow_value(p, r);
    int now = r->kind == SR_COUNTER ? shim_counter_add(p, r->id, delta) : shim_health_add(p, r->id, r->other, delta);
    if (now == before) units_refused(r, now);
    else row_confirm();
}

/*
 * SET, THEN APPLY (the owner, 07.10.2026, after the report build: an applied
 * step each time was "hell"). The arrows - a finger's, a pad's, a keyboard's,
 * a held one's - only move a PENDING value: nothing reaches the game and
 * nothing sounds. The row then shows that value with " ?" after it, blinking,
 * the way the teleport asks with " ?" before its second tap; the foot says how
 * to apply it. A tap on the row (not on its arrows), the pad's confirm or Enter
 * applies it, once, with the confirmation - or the game's limit note and no
 * confirmation. Another row, another tab, another body or the card closing
 * drops it: the row shows what the game has again. A number typed applies at
 * once, as before.
 */
#define PEND_STEPS_MOST 40                    /* shot speed: whole counts, as its arrows always stepped */
#define UNITS_MOST      999

static const char *const SP_APPLY_HINT[7] = {
    "apply: tap the row, A or ENTER",
    "\xd0\xbf\xd1\x80\xd0\xb8\xd0\xbc\xd0\xb5\xd0\xbd\xd0\xb8\xd1\x82\xd1\x8c: \xd1\x82\xd0\xb0\xd0\xbf \xd0\xbf\xd0\xbe "
    "\xd1\x81\xd1\x82\xd1\x80\xd0\xbe\xd0\xba\xd0\xb5, A \xd0\xb8\xd0\xbb\xd0\xb8 ENTER",
    "\xc3\xbc" "bernehmen: Zeile antippen, A oder ENTER",
    "aplicar: toca la fila, A o ENTER",
    "\xe6\xb1\xba\xe5\xae\x9a\xef\xbc\x9a\xe8\xa1\x8c\xe3\x82\x92\xe3\x82\xbf\xe3\x83\x83\xe3\x83\x97\xe3\x80\x81"
    "A\xe3\x81\x8b" "ENTER",
    "\xec\xa0\x81\xec\x9a\xa9: \xec\xa4\x84\xec\x9d\x84 \xed\x83\xad, A \xeb\x98\x90\xeb\x8a\x94 ENTER",
    "\xe5\xba\x94\xe7\x94\xa8\xef\xbc\x9a\xe7\x82\xb9\xe8\xaf\xa5\xe8\xa1\x8c\xe3\x80\x81" "A\xe6\x88\x96" "ENTER",
};

static struct {
    int on;
    unsigned long p;                          /* the body it was set for */
    int row;                                  /* the list row */
    srow_t r;
    float value;                              /* a stat's value */
    int steps;                                /* shot speed: whole counts */
    int units;                                /* a counter's or a heart row's value */
} g_pend;

static void pend_drop(void) { g_pend.on = 0; }

/* the pending value of the chosen row, still meant: the same body, row and focus; else dropped */
static int pend_here(void) {
    if (!g_pend.on) return 0;
    list_t *l = &g_list[L_STATS];
    int k = l->chosen;
    if (g_pend.p == stat_player() && g_stat_focus == FOCUS_LIST && k == g_pend.row && k >= 0 && k < l->count
        && g_srow[k].kind == g_pend.r.kind && g_srow[k].id == g_pend.r.id && g_srow[k].other == g_pend.r.other)
        return 1;
    pend_drop();
    return 0;
}

/* the chosen row stepped: the pending value moves, the game is not asked */
static void srow_step(int dir, int times) {
    const srow_t *r = stat_chosen();
    unsigned long p = stat_player();
    if (!r || !dir || !p) return;
    if (times < 1) times = 1;
    stats_stop_typing();
    g_note_left = 0;
    if (!pend_here()) {
        g_pend.on = 1;
        g_pend.p = p;
        g_pend.row = g_list[L_STATS].chosen;
        g_pend.r = *r;
        g_pend.value = r->kind == SR_STAT ? shim_stats_value(p, r->id) : 0.0f;
        g_pend.steps = 0;
        g_pend.units = r->kind == SR_STAT ? 0 : srow_value(p, r);
    }
    if (r->kind == SR_STAT && r->id == STAT_SHOT) {
        g_pend.steps += dir * times;
        if (g_pend.steps > PEND_STEPS_MOST) g_pend.steps = PEND_STEPS_MOST;
        if (g_pend.steps < -PEND_STEPS_MOST) g_pend.steps = -PEND_STEPS_MOST;
        if (!g_pend.steps) pend_drop();
        return;
    }
    if (r->kind == SR_STAT) {
        int s = r->id;
        float step = STAT_STEP[s], v = g_pend.value, least, most;
        float want = roundf(v / step) * step + (float) dir * step * (float) times;
        if (dir > 0 && want <= v + 0.001f) want += step;
        if (dir < 0 && want >= v - 0.001f) want -= step;
        shim_stats_limits(s, &least, &most);
        if (want > most) want = most;               /* the arrows stop at what the game allows */
        if (want < least) want = least;
        g_pend.value = want;
        if (fabsf(want - shim_stats_value(p, s)) < 0.001f) pend_drop();
        return;
    }
    int unit = r->kind == SR_HEALTH && r->id == SP_H_CONTAINERS ? 2 : 1;   /* a whole container */
    g_pend.units += dir * times * unit;
    if (g_pend.units < 0) g_pend.units = 0;
    if (g_pend.units > UNITS_MOST) g_pend.units = UNITS_MOST;
    if (g_pend.units == srow_value(p, r)) pend_drop();
}

/* the pending value applied: once, with the confirmation, or the game's note */
static void pend_apply(void) {
    if (!pend_here()) return;
    unsigned long p = g_pend.p;
    srow_t r = g_pend.r;
    pend_drop();
    if (r.kind == SR_STAT && r.id == STAT_SHOT) {
        float before = shim_stats_value(p, r.id), got = before;
        int dir = g_pend.steps > 0 ? 1 : -1;
        for (int i = 0; i < abs(g_pend.steps) && !isnan(got); i++) got = shim_stats_shot_step(p, dir);
        if (!isnan(got) && fabsf(got - before) >= 0.001f) row_confirm();
        else if (!isnan(got)) {
            char line[128];
            stat_hint(r.id, line, sizeof(line));
            note(line);
        }
        return;
    }
    if (r.kind == SR_STAT) {
        float before = shim_stats_value(p, r.id);
        float got = shim_stats_set(p, r.id, g_pend.value);
        if (!stat_answer(r.id, g_pend.value, got) && fabsf(got - before) >= 0.001f) row_confirm();
        return;
    }
    units_add(&r, g_pend.units - srow_value(p, &r));
}

/* what the chosen row shows while a value waits: as the row shows its own, " ?" after it */
static void pend_shown(unsigned long p, char *out, size_t n) {
    char v[24];
    if (g_pend.r.kind == SR_STAT && g_pend.r.id == STAT_SHOT) {
        snprintf(v, sizeof(v), "%.2f", (double) (shim_stats_value(p, STAT_SHOT) + 0.2f * (float) g_pend.steps));
    } else if (g_pend.r.kind == SR_STAT) {
        float x = g_pend.value;
        if (g_pend.r.id == STAT_LUCK && fabsf(x - roundf(x)) < 0.005f) snprintf(v, sizeof(v), "%.0f", (double) x);
        else snprintf(v, sizeof(v), "%.2f", (double) x);
    } else {
        units_shown(g_pend.units, g_pend.r.kind == SR_HEALTH && shim_health_halves(g_pend.r.id), v, sizeof(v));
    }
    snprintf(out, n, "%s ?", v);
}

static void stat_typed_apply(void) {
    pend_drop();
    const srow_t *r = stat_chosen();
    char text[sizeof(g_typed)];
    snprintf(text, sizeof(text), "%s", g_typed);
    for (char *c = text; *c; c++) if (*c == ',') *c = '.';
    char *end = NULL;
    float want = strtof(text, &end);
    stats_stop_typing();
    if (!end || end == text || !r) return;
    unsigned long p = stat_player();
    if (!p) return;
    if (r->kind == SR_STAT) {
        float before = shim_stats_value(p, r->id);
        float got = shim_stats_set(p, r->id, want);
        if (!stat_answer(r->id, want, got) && fabsf(got - before) >= 0.001f) row_confirm();
        return;
    }
    int halves = r->kind == SR_HEALTH && shim_health_halves(r->id);
    int target = (int) lroundf(halves ? want * 2.0f : want);
    units_add(r, target - srow_value(p, r));
}

/* text from either keyboard while STATS is shown */
static void stats_text(const char *text) {
    if (!strcmp(text, "\n")) {
        if (g_typing) stat_typed_apply();
        else tab_confirm();
        return;
    }
    if (!*text) { if (g_typing) rub_out(g_typed); return; }
    const srow_t *r = stat_chosen();
    if (!r) return;
    for (const char *c = text; *c; c++) {
        int ok = (*c >= '0' && *c <= '9') || *c == '.' || *c == ','
                 || (*c == '-' && r->kind == SR_STAT && r->id == STAT_LUCK);
        if (!ok) continue;
        if (!g_typing) { g_typing = 1; g_typed[0] = 0; pend_drop(); ime_for_tab(); }
        size_t n = strlen(g_typed);
        if (n + 1 < sizeof(g_typed)) { g_typed[n] = *c; g_typed[n + 1] = 0; }
    }
}

static void stats_walk(int dx, int dy) {
    list_t *l = &g_list[L_STATS];
    if (dy) {
        stats_stop_typing();
        pend_drop();                          /* another row: the value waiting is dropped */
        g_note_left = 0;
        if (g_stat_focus == FOCUS_WHO) {
            if (dy > 0) g_stat_focus = FOCUS_LIST;
        } else if (g_stat_focus == FOCUS_FOOT) {
            if (dy < 0) g_stat_focus = FOCUS_LIST;
        } else {
            int k = list_settle(L_STATS, l->chosen + dy, dy);
            if (k >= 0) { l->chosen = k; list_follow(l); }
            else if (dy < 0 && who_shown()) g_stat_focus = FOCUS_WHO;
            else if (dy > 0) g_stat_focus = FOCUS_FOOT;
        }
    }
    if (dx) {
        if (g_stat_focus == FOCUS_WHO) who_step(dx);
        else if (g_stat_focus == FOCUS_FOOT) g_foot_button = dx > 0 ? FOOT_RESET : FOOT_CHARGE;
        else srow_step(dx, 1);
    }
}

/* ------------------------------------------------------------ the counts of ITEMS */
static const hit_t *item_chosen(void) {
    list_t *l = &g_list[L_ITEMS];
    return !g_line_focus && l->chosen >= 0 && l->chosen < l->count && g_rows[l->chosen].kind != KIND_HEAD
           ? &g_rows[l->chosen] : NULL;
}

/* the chosen row's count by `delta`, held within 1..99 */
static void count_step(int delta) {
    const hit_t *h = item_chosen();
    int slot = h ? count_slot(h) : -1;
    if (slot < 0 || !delta) return;
    int n = g_count[slot] + 1 + delta;
    if (n < 1) n = 1;
    if (n > COUNT_MOST) n = COUNT_MOST;
    g_count[slot] = (unsigned char) (n - 1);
}

/*
 * HOLD TO STEP (the owner, 19th pass, 03.10.2026: a handheld with no touch must
 * get from 3.50 to 500 damage without hundreds of presses). The game's own
 * menus repeat a held direction: the main menu (0x7f8f98, 0x7f9018) asks the
 * held state 0x8b64f8(input, action, -1, 0) - vtable +0x48, beside the
 * "pressed now" of 0x8b6600 (+0x50) - counts the updates it is held
 * (0x7f93c0), acts on the press, then once 50 updates have gone, on every
 * 10th: at the game's 30 updates a second, 1.67 s and then 0.33 s. The value
 * starts exactly so and then speeds up - shorter waits, then bigger steps - so
 * a long hold ends at the value wanted in seconds. A key of a keyboard is the
 * same hold (shim_spawner_key keeps it; the system's own repeat is not used).
 * The same hold steps an ITEMS row's count (07.10.2026). The player row and
 * the foot step once per press, as a list.
 */
#define HOLD_FIRST_MS    1667                /* 50 of the game's updates */

static const struct { int from; int every_ms; int times; } HOLD_PACE[] = {
    {   0, 333,   1 },                       /* the game's own: every 10th update */
    {   4, 167,   1 },
    {  10,  67,   1 },
    {  20,  33,   1 },
    {  35,  33,   2 },
    {  50,  33,   5 },
    {  65,  33,  10 },
    {  80,  33,  25 },
    { 100,  33, 100 },
};

static int g_hold_dir;                        /* the direction held, 0 none */
static long long g_hold_next;                 /* when the next step is due, ms */
static int g_hold_reps;

static long long now_ms(void) {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (long long) t.tv_sec * 1000 + t.tv_nsec / 1000000;
}

static int menu_held(int action);

/* left and right step a value here, held: a STATS row, or an ITEMS row's count */
static int hold_applies(void) {
    if (g_tab == TAB_STATS) return 1;
    if (g_tab == TAB_ITEMS) { const hit_t *h = item_chosen(); return h && count_of(h) > 0; }
    return 0;
}

/* a press: the step at once, as the game's menus act, and the hold's clock started */
static void value_hold_press(int dir) {
    g_hold_dir = dir;
    g_hold_reps = 0;
    g_hold_next = now_ms() + HOLD_FIRST_MS;
    if (!dir) return;
    if (g_tab == TAB_STATS) stats_walk(dir, 0);
    else count_step(dir);
}

/* once a frame while a value is under the cursor: dir is what is held now, pad or keyboard */
static void value_hold(int dir) {
    long long now = now_ms();
    if (dir != g_hold_dir) { value_hold_press(dir); return; }
    if (!dir) return;
    if (g_tab == TAB_STATS && !stat_chosen()) return;
    if (g_hold_next < now - 200) g_hold_next = now;   /* a long frame does not fire a burst */
    while (now >= g_hold_next) {
        int k = 0;
        while (k + 1 < (int) (sizeof(HOLD_PACE) / sizeof(HOLD_PACE[0])) && g_hold_reps >= HOLD_PACE[k + 1].from) k++;
        if (g_tab == TAB_STATS) srow_step(dir, HOLD_PACE[k].times);
        else count_step(dir * HOLD_PACE[k].times);
        g_hold_reps++;
        g_hold_next += HOLD_PACE[k].every_ms;
    }
}

/* the foot's buttons: every active item charged, or the stats put back */
static void foot_press(int button) {
    unsigned long p = stat_player();
    stats_stop_typing();
    if (button == FOOT_CHARGE) {
        int n = p ? shim_charge_actives(p) : 0;
        note(n ? SP_CHARGED[set_now()] : SP_NO_ACTIVE[set_now()]);
        if (n) stat_confirm(3000 + FOOT_CHARGE);
    } else if (shim_stats_reset(p)) {
        stat_confirm(3000 + FOOT_RESET);
    }
}

static void stats_confirm(void) {
    if (g_stat_focus == FOCUS_FOOT) {
        foot_press(g_foot_button);
    } else if (g_stat_focus == FOCUS_WHO) {
        stats_walk(1, 0);
    } else if (stat_chosen()) {
        if (g_typing) stat_typed_apply();
        else if (pend_here()) pend_apply();   /* A / Enter applies what the arrows set */
        else { g_typing = 1; g_typed[0] = 0; ime_for_tab(); }
    }
}

/* a finger's tap on STATS outside the list: the player's line, the foot's buttons */
static void stats_tap(float gx, float gy) {
    if (gy < g_list[L_STATS].top) {               /* the player line: its halves step back and on */
        if (!who_shown()) return;
        stats_stop_typing();
        g_stat_focus = FOCUS_WHO;
        stats_walk(gx < g_card_x + g_card_w * 0.5f ? -1 : 1, 0);
        return;
    }
    if (gy >= g_stat_foot && !g_note_left && !g_typing) {
        g_stat_focus = FOCUS_FOOT;
        g_foot_button = gx < g_foot_split ? FOOT_CHARGE : FOOT_RESET;
        foot_press(g_foot_button);
    }
}

/* a STATS row tapped: the first tap picks it, then its arrows step it and the rest types */
static void stats_row_tapped(int k, float gx) {
    list_t *l = &g_list[L_STATS];
    if (!row_choosable(L_STATS, k)) return;
    if (k != l->chosen || g_stat_focus != FOCUS_LIST) {
        stats_stop_typing();
        pend_drop();
        g_stat_focus = FOCUS_LIST;
        l->chosen = k;
        g_note_left = 0;
        return;
    }
    if (gx >= g_arrow_l0 - 6.0f && gx <= g_arrow_l1 + 4.0f) srow_step(-1, 1);
    else if (gx >= g_arrow_r0 - 4.0f && gx <= g_arrow_r1 + 6.0f) srow_step(1, 1);
    else if (pend_here()) pend_apply();       /* the row itself applies what its arrows set */
    else if (!g_typing) { g_typing = 1; g_typed[0] = 0; ime_for_tab(); }
}

/* ------------------------------------------------------------ what each tab does */
static void teleport_go_room(int k) {
    if (k < 0 || k >= g_list[L_ROOMS].count) return;
    int type = SP_ROOM[k].type, subtype = SP_ROOM[k].subtype;
    if (shim_teleport_room_state(type, subtype) < 0) return;
    shim_spawner_mark_run();
    shim_teleport_ask_room(type, subtype);
    play_jingle();                            /* taken: the same jingle as a floor and a give */
    card_down();
}

/* a floor goes on the second tap: the first flashes it and asks with a "?" */
static void teleport_floor_tap(int k) {
    if (k < 0 || k >= g_list[L_FLOORS].count) return;
    g_list[L_FLOORS].chosen = k;
    if (g_armed != k) {
        g_armed = k;
        g_flash_row = 1000 + k;
        g_flash_left = FLASH_FRAMES;
        return;
    }
    const int i = g_floor_row[k];
    shim_spawner_mark_run();
    shim_teleport_ask_floor(SP_FLOOR[i].stage, SP_FLOOR[i].stage_type);
    play_jingle();                            /* the second tap is taken: the give's own jingle (19th pass) */
    card_down();
}

/* a row of ENEMIES acted on: the monster itself, one of its champions or one of its colours */
static void enemy_spawn(int k) {
    if (!row_choosable(L_ENEMIES, k)) return;
    int e = g_enemy_row[k], j = g_enemy_sub[k];
    g_give_champion = -1;
    g_give_subtype = -1;
    if (j >= 0 && sub_is_champion(e, j)) g_give_champion = j;
    else if (j >= 0) g_give_subtype = SP_BOSS_COLOUR[sub_colour(e, j)].subtype;
    give_now(KIND_ENEMY, (unsigned) e);
    g_give_champion = g_give_subtype = -1;
    given(k);
}

/* a monster's own row confirmed: its sub-list first, when it has one and it is not out */
static void enemy_confirm(int k) {
    if (!row_choosable(L_ENEMIES, k)) return;
    int e = g_enemy_row[k];
    if (g_enemy_sub[k] < 0 && subs_of(e) && g_open_enemy != e) { enemy_open(e); return; }
    enemy_spawn(k);
}

/* a row tapped: the first tap picks it, a tap on the picked one acts (a room acts at once) */
static float g_cnt_l0 = -1.0f, g_cnt_l1, g_cnt_r0, g_cnt_r1;     /* the chosen ITEMS row's count arrows, as drawn */
static float g_cross_x = -1.0f;              /* where the held rows' crosses start, as drawn; -1 none */
static int g_cross_focus;                    /* a pad's cursor is on the chosen row's cross */

/* the cross acted on: every copy goes, the row flashes */
static void held_cross(int k) {
    if (k < 0 || k >= g_rows_n || g_rows[k].kind != KIND_ITEM) return;
    if (!held_take(g_rows[k].id)) return;
    g_cross_focus = 0;
    given(k);
    if (held_refresh() && g_only_held) research();
}

static void row_tapped(int l, int k, float tx) {
    list_t *t = &g_list[l];
    if (k < 0 || k >= t->count) return;
    if (l == L_FLOORS) { g_col = 1; teleport_floor_tap(k); return; }
    if (l == L_ROOMS) { g_col = 0; g_armed = -1; t->chosen = k; teleport_go_room(k); return; }
    if (!row_choosable(l, k)) return;
    if (l == L_ENEMIES) {
        /* a champion's row spawns at once; a monster's row opens its champions, then spawns */
        if (g_enemy_sub[k] >= 0) { t->chosen = k; enemy_spawn(k); return; }
        if (k != t->chosen) {
            t->chosen = k;
            int e = g_enemy_row[k];
            if (subs_of(e) && g_open_enemy != e) enemy_open(e);
            else if (g_open_enemy >= 0 && g_open_enemy != e) enemy_open(-1);
            return;
        }
        enemy_confirm(k);
        return;
    }
    if (l == L_STATS) { stats_row_tapped(k, tx); return; }
    /* ITEMS: the first tap picks, then the count's arrows step it and the rest of the row gives */
    g_line_focus = 0;
    /* a held collectible's cross takes it away at once, picked or not (a button, the owner's) */
    if (g_rows[k].kind == KIND_ITEM && held_of(g_rows[k].id) && g_cross_x >= 0.0f && tx >= g_cross_x - 6.0f) {
        t->chosen = k;
        held_cross(k);
        return;
    }
    if (k != t->chosen) { t->chosen = k; return; }
    if (count_of(&g_rows[k]) && g_cnt_l0 >= 0.0f) {
        if (tx >= g_cnt_l0 - 6.0f && tx <= g_cnt_l1 + 4.0f) { count_step(-1); return; }
        if (tx >= g_cnt_r0 - 4.0f && tx <= g_cnt_r1 + 6.0f) { count_step(1); return; }
    }
    shim_spawner_give(k);
    given(k);
}

static void tab_confirm(void) {
    if (g_tab == TAB_ITEMS) {
        int k = g_list[L_ITEMS].chosen;
        if (g_line_focus == 1) ground_flip();
        else if (g_line_focus == 2) held_flip();
        else if (g_cross_focus && k >= 0 && k < g_rows_n && g_rows[k].kind == KIND_ITEM && held_of(g_rows[k].id))
            held_cross(k);
        else if (row_choosable(L_ITEMS, k)) { shim_spawner_give(k); given(k); }
    } else if (g_tab == TAB_ENEMIES) {
        enemy_confirm(g_list[L_ENEMIES].chosen);
    } else if (g_tab == TAB_TELEPORT) {
        void teleport_clear(int which);
        if (g_col == 2) teleport_clear(g_tp_button);
        else if (g_col == 0) teleport_go_room(g_list[L_ROOMS].chosen);
        else teleport_floor_tap(g_list[L_FLOORS].chosen);
    } else {
        stats_confirm();
    }
}

/* arrows, from a pad or a keyboard */
static void card_walk(int dx, int dy) {
    if (g_tab == TAB_STATS) { stats_walk(dx, dy); return; }
    if (g_tab == TAB_TELEPORT) {
        g_armed = -1;
        /* CLEAR ENEMIES / CLEAR PICKUPS (21.10.2026): the foot, below the last row of either column */
        if (g_col == 2) {
            if (dx) g_tp_button = dx > 0 ? 1 : 0;
            if (dy < 0) g_col = g_tp_from;
            return;
        }
        if (dx) g_col = dx > 0 ? 1 : 0;
        if (dy > 0) {
            list_t *t = &g_list[g_col ? L_FLOORS : L_ROOMS];
            if (t->chosen >= t->count - 1) { g_tp_from = g_col; g_col = 2; return; }
        }
        if (dy) list_walk(g_col ? L_FLOORS : L_ROOMS, dy);
        return;
    }
    if (g_tab == TAB_ITEMS) {
        /*
         * ITEMS (07.10.2026): up from the first row reaches the line, where left,
         * right and confirm turn "On the ground"; down leaves it. On a row left
         * and right step its count; a collectible has none, and there they are
         * the player switch's, as before.
         *
         * 21.10.2026: the line holds two switches, ONLY HELD on the left and ON
         * THE GROUND on the right - left and right move between them, confirm
         * turns the one the cursor is on. A held collectible's row has its cross
         * on the right: right reaches it, left leaves it, confirm takes the item.
         */
        list_t *t = &g_list[L_ITEMS];
        if (dy) g_cross_focus = 0;
        if (dy < 0 && !g_line_focus && list_settle(L_ITEMS, t->chosen - 1, -1) < 0) { g_line_focus = 1; return; }
        if (dy > 0 && g_line_focus) {
            g_line_focus = 0;
            if (!row_choosable(L_ITEMS, t->chosen)) t->chosen = list_settle(L_ITEMS, 0, 1);
            list_follow(t);
            return;
        }
        if (dx) {
            const hit_t *h = item_chosen();
            if (g_line_focus) g_line_focus = dx < 0 ? 2 : 1;
            else if (h && count_of(h)) count_step(dx);
            else if (h && h->kind == KIND_ITEM && held_of(h->id) && (dx > 0 || g_cross_focus)) g_cross_focus = dx > 0;
            else who_step(dx);
        }
        if (dy && !g_line_focus) list_walk(L_ITEMS, dy);
        return;
    }
    if (dy) list_walk(L_ENEMIES, dy);
}

/* ------------------------------------------------------------ between frames */
/* touches, kept by the touch thread for the frame */
static volatile int g_tap_list = -1, g_tap_row = -1;
static volatile float g_tap_x;                /* where on the row it was tapped */
static volatile int g_want_ground;            /* ITEMS: a tap on "On the ground" */
static volatile int g_want_held;              /* ITEMS: a tap on "Only held" */
static volatile int g_want_clear;             /* TELEPORT: a tap on CLEAR ENEMIES (1) or CLEAR PICKUPS (2) */
static volatile int g_want_tab = -1;
static volatile int g_stat_tap;
static volatile float g_stat_tap_x, g_stat_tap_y;
static volatile int g_want_who;               /* ITEMS: a tap on the switch's half, -1 or 1 */
static volatile int g_drag_started;

void shim_spawner_between_frames(void) {
    if (!g_frames_seen) { unsigned long b0 = shim_image_base(); if (b0) first_frame(b0); }
    shim_teleport_between_frames();          /* a jump asked for runs with the card already gone */
    finals_between_frames();                 /* a final boss of ours ends without an ending */
    arena_between_frames();                  /* Dogma's or Mother's arena turns back into the room */
    plain_between_frames();                  /* a plain boss's colour roll back to the room's once it is gone */
    mom_players_between_frames();            /* nobody stays behind a wall after Mom's start */
    if (g_want_open) { g_want_open = 0; if (!g_open) card_up(); }
    if (!g_open) return;
    if (g_want_close) { g_want_close = 0; card_down(); return; }

    /* the dialog went by itself: Back on a pad */
    unsigned long d = dialog_of(g_dialog);
    if (!d || *(const int *) (d + DIALOG_STATE) > STATE_OPEN) { card_down(); return; }

    if (g_want_keyboard) { g_want_keyboard = 0; shim_ime_show(1); }

    int tab = g_want_tab;
    if (tab >= 0) { g_want_tab = -1; tab_show(tab); }
    if (g_drag_started) { g_drag_started = 0; g_armed = -1; }

    if (g_tab == TAB_STATS) stats_rows();    /* the body shown may have changed: its rows with it */
    int l = g_tap_list, k = g_tap_row;
    if (l >= 0) { g_tap_list = -1; g_tap_row = -1; row_tapped(l, k, g_tap_x); if (!g_open) return; }
    if (g_stat_tap) { g_stat_tap = 0; stats_tap(g_stat_tap_x, g_stat_tap_y); }
    int who_dir = g_want_who;
    if (who_dir) { g_want_who = 0; if (g_tab == TAB_ITEMS) who_step(who_dir); }
    if (g_want_ground) { g_want_ground = 0; if (g_tab == TAB_ITEMS) ground_flip(); }
    if (g_want_held) { g_want_held = 0; if (g_tab == TAB_ITEMS) held_flip(); }
    if (g_want_clear) {
        void teleport_clear(int which);
        int w = g_want_clear;
        g_want_clear = 0;
        if (g_tab == TAB_TELEPORT) teleport_clear(w - 1);
    }
    /* what the player holds, once a frame: a change shows at once, and ONLY HELD's list follows it */
    if (g_tab == TAB_ITEMS && held_refresh() && g_only_held) research();
    /* the switch went away (a second player left): its row with it */
    if (g_stat_focus == FOCUS_WHO && !who_shown()) g_stat_focus = FOCUS_LIST;

    /* a pad walks the card once it is at rest; its shoulders walk the tabs */
    if (*(const int *) (d + DIALOG_STATE) == STATE_OPEN) {
        if (menu_action(ACTION_LT)) tab_step(-1);
        if (menu_action(ACTION_RT)) tab_step(1);
        if (menu_action(ACTION_UP)) card_walk(0, -1);
        if (menu_action(ACTION_DOWN)) card_walk(0, 1);
        if (hold_applies()) {
            /* left and right are held, not pressed: value_hold steps and repeats */
            int pressed = menu_action(ACTION_RIGHT) - menu_action(ACTION_LEFT);
            int held = menu_held(ACTION_RIGHT) - menu_held(ACTION_LEFT);
            if (pressed && pressed != g_hold_dir) value_hold_press(pressed);   /* a tap shorter than a frame */
            value_hold(g_kb_hold ? g_kb_hold : held);
        } else {
            g_hold_dir = 0;
            if (menu_action(ACTION_LEFT)) card_walk(-1, 0);
            if (menu_action(ACTION_RIGHT)) card_walk(1, 0);
        }
        if (menu_action(ACTION_CONFIRM)) tab_confirm();
        if (!g_open) return;
    } else {
        g_hold_dir = 0;
    }

    enemies_slide();                         /* a champion list on its way out */
    for (int i = 0; i < LISTS; i++) list_run(&g_list[i]);
    if (g_flash_left > 0 && --g_flash_left == 0) g_flash_row = -1;
    if (g_tab_flash > 0) g_tab_flash--;
    if (g_note_left > 0) g_note_left--;
}

static const float CHOSEN[3] = { 1.0f, 0.86f, 0.55f };
static const float FLASH[3] = { 0.55f, 1.0f, 0.55f };
static const float GREY[3] = { 150 / 255.0f, 140 / 255.0f, 144 / 255.0f };

/* ------------------------------------------------------------ the strap */
/*
 * The way in: a strap on the right edge of the pause paper, shown only when
 * the spawner is switched on in Tweaks.
 *
 * The pause screen is part of the Game object (Game+0x10dd20) and its state
 * at +0x2b8 is 1 while the main list is up - through its slide in and out as
 * well. The paper is drawn at (0.6 W, 0.5 H) of the design canvas, and while
 * it slides the Paper layer (1) of its sprite (+0x2e0) carries the offset:
 * 0x89e714(sprite+0x30, 1) answers the layer's current frame, its offset at
 * +0x10 - the very source the game copies to the stats box so it slides along
 * (0x8a23f0). The strap takes the same offset and so rides with the paper.
 *
 * The owner's strap (17.09.2026) sits where the mock-up of the pause sheet puts
 * it: its paper's top-left at (214, 85) of the sheet, whose Paper layer crop
 * starts at (0, 0) with its pivot at (120, 88) - so (+94, -3) from the anchor.
 */
#define GAME_PAUSE_STATE   0x10dfd8
#define GAME_PAUSE_SPRITE  0x10e000     /* Game+0x10dd20 + 0x2e0 */
#define PAUSE_MAIN         1
#define FN_LAYER_FRAME     0x89e714
/*
 * The Paper layer is read by name from the pausescreen.anm2 the game loads
 * (straps.c, portpatch.c - 1 in the game's own file); -1 when the strap is not
 * drawn there, and then there is nothing for the finger either.
 */
int shim_straps_pause_paper(void);
#define FRAME_OFFSET       0x10
#define DESIGN_CANVAS      0xac24d8
#define PAPER_AT_X         0.6f
#define STRAP_FROM_X       94.0f        /* the strap's paper top-left from the paper anchor */
#define STRAP_FROM_Y       -3.0f
/*
 * The strap's size per language (strap_sizes.h, tools/strap_lang): today every
 * set's pause strap is the owner's 78 x 55, but the box follows the picture so
 * a lengthened one would answer along its whole length.
 */
#include "strap_sizes.h"

int shim_tweaks_spawner(void);
int shim_sticks_in_game(void);

static float g_strap_x, g_strap_y;          /* the strap's top-left, as last drawn */
static volatile int g_strap_shown;

static int pause_main(unsigned long b) {
    unsigned long game = *(const unsigned long *) (b + GAME_PTR);
    return game && *(const int *) (game + GAME_PAUSE_STATE) == PAUSE_MAIN;
}

/* where the strap is, for the finger - the picture itself is a layer of the pause sheet (straps.c) */
static void strap_place(void) {
    unsigned long b = shim_image_base();
    if (!b || g_open || !shim_tweaks_spawner() || !shim_sticks_in_game() || !pause_main(b)) {
        g_strap_shown = 0;
        return;
    }
    int paper = shim_straps_pause_paper();
    if (paper < 0) {
        g_strap_shown = 0;
        return;
    }
    unsigned long game = *(const unsigned long *) (b + GAME_PTR);
    const float *canvas = (const float *) (b + DESIGN_CANVAS);
    float x = canvas[0] * PAPER_AT_X + STRAP_FROM_X, y = canvas[1] * 0.5f + STRAP_FROM_Y;
    const float *frame = ((const float *(*)(unsigned long, int)) (b + FN_LAYER_FRAME))(
        game + GAME_PAUSE_SPRITE + 0x30, paper);
    if (frame) { x += frame[FRAME_OFFSET / 4]; y += frame[FRAME_OFFSET / 4 + 1]; }
    g_strap_x = x;
    g_strap_y = y;
    g_strap_shown = 1;
}

#if ISAAC_DIAG
/* the tap audit's lines (screen.c M4, straps.c M3): the strap's box, as the finger is judged below */
int shim_spawner_strap_rect(float box[4], float origin[2]) {
    if (!g_strap_shown) return 0;
    if (origin) { origin[0] = g_strap_x; origin[1] = g_strap_y; }
    int set = shim_tweaks_set_index();
    if (set < 0 || set > 6) set = 0;
    float rise = (float) STRAP_SIZE[set][STRAP_SPAWNER].rise;
    box[0] = g_strap_x;
    box[1] = g_strap_y - rise;
    box[2] = g_strap_x + (float) (STRAP_SIZE[set][STRAP_SPAWNER].w - STRAP_EXTEND);
    box[3] = g_strap_y + (float) STRAP_SIZE[set][STRAP_SPAWNER].h - rise;
    return 1;
}
#endif

/* ------------------------------------------------------------ the pictures */
/*
 * The card's own item pictures, a sprite each, handed to the rows in view this
 * frame; a row keeps its sprite for as long as it stays in view, so a file is
 * read only when a new item scrolls in.
 */
static unsigned char g_pic[PICTURES][SPRITE_BYTES] __attribute__((aligned(16)));
static signed char g_pic_built[PICTURES];
static int g_pic_key[PICTURES];             /* +id an item, -id a trinket, 0 none */
static unsigned g_pic_used[PICTURES];       /* the frame that last drew it */
static unsigned g_pic_frame;

static void pictures_frame(void) { g_pic_frame++; }

static unsigned char *picture_for(unsigned char kind, unsigned id) {
    int key = kind == KIND_TRINKET ? -(int) id : (int) id;
    for (int i = 0; i < PICTURES; i++)
        if (g_pic_key[i] == key && g_pic_built[i] > 0) { g_pic_used[i] = g_pic_frame; return g_pic[i]; }
    int take = -1;
    for (int i = 0; i < PICTURES && take < 0; i++)
        if (g_pic_used[i] != g_pic_frame && g_pic_built[i] >= 0) take = i;
    if (take < 0) return NULL;
    g_pic_key[take] = 0;
    if (!shim_eid_item_picture(g_pic[take], &g_pic_built[take], (int) id, kind == KIND_TRINKET))
        return NULL;
    g_pic_key[take] = key;
    g_pic_used[take] = g_pic_frame;
    return g_pic[take];
}

/*
 * The name of a row, in the language the names are shown in: the description
 * database's names for items, trinkets, cards and pills (the panel's language,
 * English where it has none), the pickups' own table for the rest; a horse
 * pill and a golden trinket carry their mark after the name.
 */
static const char *row_name(const hit_t *h, char *buf, size_t n) {
    int set = set_now();
    const char *name = NULL;
    switch (h->kind) {
    case KIND_ENEMY:  return h->id < SP_ENEMIES ? SP_ENEMY[h->id].name[set] : "?";
    case KIND_PICKUP: return h->id < SP_PICKUPS ? SP_PICKUP[h->id].name[set] : "?";
    case KIND_PILL: {
        unsigned e = h->id & 0xff;
        name = shim_eid_name_of(EID_PILLS, e == PILL_GOLD ? EID_GOLD_PILL : e + 1);
        if (!name) name = "?";
        if (!(h->id & PILL_HORSE)) return name;
        snprintf(buf, n, SP_HORSE_FMT[set], name);
        return buf;
    }
    case KIND_TRINKET:
        name = shim_eid_name_of(KIND_TRINKET, h->id & 0x7fff);
        if (!name) name = "?";
        if (!(h->id & TRINKET_GOLD)) return name;
        snprintf(buf, n, SP_GOLD_FMT[set], name);
        return buf;
    default:
        name = shim_eid_name_of(h->kind, h->id);
        return name ? name : "?";
    }
}

/* ------------------------------------------------------------ the draw */
/* the tabs' words and headings, the enemies', rooms' and stats' pictures: a sprite each */
static unsigned char g_tabs_art[TW_SETS_MAX][SPRITE_BYTES] __attribute__((aligned(16)));
static signed char g_tabs_state[TW_SETS_MAX];
static unsigned char g_enemy_art[SPRITE_BYTES] __attribute__((aligned(16)));
static signed char g_enemy_state;
static unsigned char g_room_art[SPRITE_BYTES] __attribute__((aligned(16)));
static signed char g_room_state;
static unsigned char g_stat_art[SPRITE_BYTES] __attribute__((aligned(16)));
static signed char g_stat_state;
static unsigned char g_pickup_art[SPRITE_BYTES] __attribute__((aligned(16)));   /* port_pickup_icons */
static signed char g_pickup_state;

static int tabs_art(int set) {
    char anm2[64];
    snprintf(anm2, sizeof(anm2), "gfx/port_spawner_tabs_%s.anm2", shim_tweaks_set_suffix());
    return shim_picture_ready(g_tabs_art[set], &g_tabs_state[set], anm2);
}

static void write_text(const char *text, float x, float y, const float *c, float alpha) {
    shim_eid_write_scaled(text, x, y, TEXT_SCALE, c ? c[0] : 1.0f, c ? c[1] : 1.0f, c ? c[2] : 1.0f, alpha);
}

static float text_w(const char *text) { return shim_eid_width_scaled(text, TEXT_SCALE); }

/* the four tabs under the heading; answers the y under them */
static float draw_tabs(int set, float left, float width, float y) {
    int art = tabs_art(set);
    float tall = 0.0f;
    for (int k = 0; k < TABS; k++)
        if (SP_TAB[set][k].h > tall) tall = (float) SP_TAB[set][k].h;
    if (!art) tall = line_h();
    float cell = (width - 2.0f * CARD_PAD) / (float) TABS;
    g_tabs_top = y;
    for (int k = 0; k < TABS; k++) {
        float cx = left + CARD_PAD + cell * (float) k + cell * 0.5f;
        float alpha = k == g_tab ? (g_tab_flash ? 0.7f : 1.0f) : PALE;
        if (art) {
            float wx = floorf(cx - (float) SP_TAB[set][k].w * 0.5f);
            float wy = floorf(y + tall - (float) SP_TAB[set][k].h);
            shim_picture_draw(g_tabs_art[set], "Tabs", k, wx, wy, alpha);
            if (k == g_tab)
                shim_picture_draw(g_tabs_art[set], "Stroke", k, wx + (float) SP_STROKE[set][k].dx,
                                  wy + (float) SP_STROKE[set][k].dy, 1.0f);
        } else {
            const char *word = SP_TAB_NAME[set][k];
            write_text(word, floorf(cx - text_w(word) * 0.5f), y, NULL, alpha);
        }
    }
    g_tabs_bottom = y + tall + 9.0f;
    return g_tabs_bottom;
}

/*
 * The line typed into and its list window. ITEMS carries its toggle on the
 * right of it (the owner, 07.10.2026): "On the ground: ON / OFF", the line
 * shortened to make room; a line longer than its field shows its end.
 */
static float g_toggle_x;                     /* where the toggle starts, for the finger; 0 none */
static float g_held_x;                       /* where ONLY HELD starts, for the finger; 0 none */
static float g_query_cut;                    /* units taken from the air under the line (shim_spawner_draw) */

static float draw_query(float x, float y, float right, int toggle) {
    int set = set_now();
    float field = right - x;
    g_toggle_x = 0.0f;
    g_held_x = 0.0f;
    if (toggle) {
        const char *label = SP_GROUND[set], *on = SP_ON[set], *off = SP_OFF[set];
        float lw = text_w(label) + text_w(" ");
        float vw = text_w(on) > text_w(off) ? text_w(on) : text_w(off);
        float tx = floorf(right - lw - vw);
        write_text(label, tx, y, g_line_focus == 1 ? CHOSEN : NULL, 1.0f);
        write_text(g_ground ? on : off, floorf(tx + lw), y, g_ground ? FLASH : GREY, 1.0f);
        g_toggle_x = tx;
        /* ONLY HELD, left of it, made the same way */
        const char *held = SP_HELD[set];
        float hw = text_w(held) + text_w(" ");
        float hx = floorf(tx - 14.0f - hw - vw);
        write_text(held, hx, y, g_line_focus == 2 ? CHOSEN : NULL, 1.0f);
        write_text(g_only_held ? on : off, floorf(hx + hw), y, g_only_held ? FLASH : GREY, 1.0f);
        g_held_x = hx;
        field = hx - 12.0f - x;
    }
    const char *q = query_now();
    char line[sizeof(g_queries[0]) + 8];
    for (;;) {
        snprintf(line, sizeof(line), "> %s_", q);
        if (!*q || text_w(line) <= field) break;
        next_cp(&q);                            /* the first character goes: the end stays in sight */
    }
    write_text(line, x, y, NULL, 1.0f);
    y += floorf(line_h() + 6.0f - g_query_cut);
    g_query_bottom = y;
    return y;
}

static const float *row_colour(int row, int chosen) {
    if (row == g_flash_row) return FLASH;
    return chosen ? CHOSEN : NULL;
}

/* the name as it fits `room`: whole, or cut at a character with "…" after it */
static const char *name_cut(const char *name, float room, char *buf, size_t n) {
    if (text_w(name) <= room || n < 8) return name;
    const char *p = name, *fits = name;
    char try[200];
    while (*p) {
        const char *q = p;
        next_cp(&q);
        int len = (int) (q - name);
        if (len + 4 >= (int) sizeof(try) || len + 4 >= (int) n) break;
        snprintf(try, sizeof(try), "%.*s\xe2\x80\xa6", len, name);
        if (text_w(try) > room) break;
        fits = q;
        p = q;
    }
    while (fits > name && fits[-1] == ' ') fits--;          /* no space before the dots */
    snprintf(buf, n, "%.*s\xe2\x80\xa6", (int) (fits - name), name);
    return buf;
}

/* a heading of the hand with its faint stroke after it, as ENEMIES heads its groups */
static void draw_heading(int set, const char *anim, int frame, float hw, float hh, float x, float row, float right,
                         float alpha) {
    shim_picture_draw(g_tabs_art[set], anim, frame, x, row + 2.0f, alpha);
    int rule = (int) ((right - x - hw - 8.0f) / (float) SP_RULE_STEP) - 1;
    if (rule >= SP_RULE_FRAMES) rule = SP_RULE_FRAMES - 1;
    if (rule >= 0)
        shim_picture_draw(g_tabs_art[set], "Rule", rule, x + hw + 8.0f,
                          row + 2.0f + hh - (float) SP_RULE[rule].h + 1.0f, RULE_ALPHA * alpha);
}

/*
 * ITEMS' rows (mock-ups v2, 07.10.2026): one picture column 32 wide for every
 * kind - a chest is 30 - the picture centred in it, the name after it, and on a
 * row with a count the count at the right edge: "<  N  >" on the chosen row,
 * "×N" on another when it is not one.
 */
#define ITEM_COLUMN 32.0f

static void draw_items(int set, float x, float y, float right, float bottom) {
    list_t *l = &g_list[L_ITEMS];
    l->top = y; l->bottom = bottom;
    float lh = line_h();
    int art = tabs_art(set);
    int icons = shim_picture_ready(g_pickup_art, &g_pickup_state, "gfx/port_pickup_icons.anm2");
    g_cnt_l0 = -1.0f;
    g_cross_x = -1.0f;
    for (int k = 0; k < l->count; k++) {
        float h = row_h(l, k), row = floorf(y + l->pos[k] - l->scroll);
        if (row >= bottom) break;
        if (row + h <= y - FADE_UNITS) continue;
        float alpha = row_alpha(l, row, h);
        if (alpha <= 0.0f) continue;
        const hit_t *r = &g_rows[k];
        if (r->kind == KIND_HEAD) {
            if (art && r->id < SP_SECTIONS)
                draw_heading(set, "Sections", r->id, (float) SP_SECTION[set][r->id].w, (float) SP_SECTION[set][r->id].h,
                             x, row, right, alpha);
            continue;
        }
        float cx = x + ITEM_COLUMN * 0.5f;
        switch (r->kind) {
        case KIND_CARD: {
            char marker[24];
            snprintf(marker, sizeof(marker), "Card%u", (unsigned) r->id);
            shim_eid_marker_faded(marker, floorf(cx - 16.0f * 0.75f * CARD_SCALE * 0.5f),
                                  floorf(row + (h - 20.0f * 0.75f * CARD_SCALE) * 0.5f), CARD_SCALE, alpha);
            break;
        }
        case KIND_PICKUP:
            if (icons && r->id < SP_PICKUPS)
                shim_picture_draw(g_pickup_art, "Icons", r->id, floorf(cx - (float) SP_PICKUP[r->id].w * 0.5f),
                                  floorf(row + (h - (float) SP_PICKUP[r->id].h) * 0.5f), alpha);
            break;
        case KIND_PILL: {
            int f = ((r->id & 0xff) == PILL_GOLD ? 2 : 0) + ((r->id & PILL_HORSE) ? 1 : 0);
            if (icons)
                shim_picture_draw(g_pickup_art, "Pills", f, floorf(cx - (float) SP_PILL_ICON[f].w * 0.5f),
                                  floorf(row + (h - (float) SP_PILL_ICON[f].h) * 0.5f), alpha);
            break;
        }
        case KIND_TRINKET:
            if (r->id & TRINKET_GOLD) {
                int t = r->id & 0x7fff;
                if (icons && t >= 1 && t <= SP_GOLDEN_TOP)
                    shim_picture_draw(g_pickup_art, "Golden", t - 1, floorf(cx - SP_TRINKET_PICTURE * 0.5f),
                                      floorf(row + (h - SP_TRINKET_PICTURE) * 0.5f), alpha);
                break;
            }
            /* fall through: a plain trinket's picture is the panel's, as an item's */
        default: {
            unsigned char *pic = picture_for(r->kind, r->id);
            if (pic) shim_eid_item_picture_draw(pic, floorf(cx - PICTURE * 0.5f), floorf(row + (h - PICTURE) * 0.5f),
                                                alpha);
            break;
        }
        }
        int chosen = k == l->chosen && !g_line_focus;
        char name[160], cut[160];
        const float *c = row_colour(k, chosen);
        float ty = floorf(row + (h - lh) * 0.5f), nx = x + ITEM_COLUMN + PICTURE_GAP;
        const char *shown_name = row_name(r, name, sizeof(name));
        int n = count_of(r);
        int held = r->kind == KIND_ITEM ? held_of(r->id) : 0;
        if (held) {
            /* WHAT THE PLAYER HOLDS: the count, no arrows, and the cross that takes every copy */
            char shown[16];
            snprintf(shown, sizeof(shown), "%d", held);
            const char *cross = SP_CROSS[set];
            float xw = text_w(cross), sw = text_w(shown);
            float xx = floorf(right - xw - 2.0f), sx = floorf(xx - 10.0f - sw);
            static const float RED[3] = { 1.0f, 0.42f, 0.42f };
            write_text(name_cut(shown_name, sx - 6.0f - nx, cut, sizeof(cut)), nx, ty, c, alpha);
            write_text(shown, sx, ty, chosen ? c : GREY, alpha);
            write_text(cross, xx, ty, chosen && g_cross_focus ? CHOSEN : RED, alpha);
            g_cross_x = xx;
            continue;
        }
        if (!n || (!chosen && n == 1)) {
            write_text(shown_name, nx, ty, c, alpha);
            continue;
        }
        char shown[24];
        if (chosen) snprintf(shown, sizeof(shown), "<  %d  >", n);
        else snprintf(shown, sizeof(shown), "\xc3\x97%d", n);
        float sw = text_w(shown);
        /* a name too long for the room the count leaves is cut with the face's own "…" (as tweaks.c cuts a mod's) */
        write_text(name_cut(shown_name, right - sw - 6.0f - nx, cut, sizeof(cut)), nx, ty, c, alpha);
        write_text(shown, floorf(right - sw), ty, chosen ? c : GREY, alpha);
        if (chosen) {
            g_cnt_l0 = right - sw; g_cnt_l1 = g_cnt_l0 + text_w("<");
            g_cnt_r1 = right; g_cnt_r0 = right - text_w(">");
        }
    }
}

/*
 * A CHAMPION'S LOOK, as the game paints it (read in the image, 04.10.2026;
 * the research notes of 03.10.2026, spawner 19, the 04.10 section). Entry k of the table at
 * 0xac28f8 carries a KColor - the champion colour - and the Color the entity
 * wears (tint, offset). A monster with champion layers (a <sheet>_champion.png)
 * wears the entry's Color and lays the KColor over the layers' mask; one
 * without them wears Color(tint = KColor) * the entry's Color. Every frame
 * (0x68bd68, f the room's frame count, 30 a second):
 *
 *   pulsing green, grey, red  the entity's colour = lerp(entry, plain,
 *                             clamp(sin 0.3f + 0.5)); the KColor's alpha =
 *                             1 - clamp(sin 0.1f + 0.5)
 *   rainbow                   the KColor = HSV((f mod 90) * 4 deg, 1, 1)
 *   black (flickering)        the entity's alpha goes to 0 by 0.05 a frame and
 *                             back, staying 60..89 frames each way (0x68c898)
 *   camouflage                tint 0, offset the colour under it * 0.9: here
 *                             the card's own paper (0x68d118)
 *   pulsating                 a scale of 1 + clamp(sin 0.2f) * 0.15 (render,
 *                             0x6978c4) - a size, baked (pulse_frame, below)
 *   crown, skull              an overlay on the body (sub_box, below)
 *
 * HOW THE KCOLOR LANDS (20th pass, 05.10.2026: Light White was far too pale).
 * Layers with a champion sheet are drawn with coloroffset_champion.fs (shader
 * 10, layer flag bit 4, 0x89d980; ChampionColorIn = entity+0x164): a texel of
 * alpha >= 128 is a mask texel of weight (1 - a) * 2.00788 (128 the whole
 * colour, 255 none; _build_champion.luax) and becomes
 *     mix(rgb, KColor.rgb * dot(rgb, lum), KColor.a * weight)
 * with lum (0.212671, 0.715160, 0.072169) - the texel's LUMINANCE times the
 * colour, not its colour times the colour - and only then the entity's
 * Color. So Light White (KColor 0.8, offset 0.196) takes the pink out of a
 * Gaper's skin: (183,183,183) where rgb * KColor kept (216,175,174).
 *
 * The icon comes in parts baked from the champion sheets (the tool): the base
 * and the mask, both drawn with the entity's colour - the sprite as it is -
 * and "Lum", the mask's luminance with the weight in its alpha, drawn over
 * them with the entity's colour times the KColor at the KColor's alpha: the
 * shader's mix, pixel for pixel, and it can follow the colour every frame.
 */
#define SPRITE_TINT_AT    0xe8               /* the sprite's Color: tint rgba, colorize, offset rgb */
#define SPRITE_OFFSET_AT  0x108
#define SPRITE_SCALE_SP   0xdc               /* the sprite's own scale, two floats (eid.c, credits.c) */
#define CAMO_SHADE        0.9f               /* 0x9460b0 */

typedef struct { float tint[4], offset[3], k[4]; } champ_paint_t;

static float clamp01(float v) { return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); }

static void hsv_rgb(float h, float *rgb) {           /* 0x7f44a8 with s = v = 1 */
    float hh = fmodf(h, 360.0f) / 60.0f;
    int i = (int) hh;
    float x = 1.0f - fabsf(fmodf(hh, 2.0f) - 1.0f);
    float r = 0, g = 0, b = 0;
    switch (i) {
    case 0: r = 1; g = x; break;
    case 1: r = x; g = 1; break;
    case 2: g = 1; b = x; break;
    case 3: g = x; b = 1; break;
    case 4: r = x; b = 1; break;
    default: r = 1; b = x; break;
    }
    rgb[0] = r; rgb[1] = g; rgb[2] = b;
}

static void champion_paint(int k, int layers, long long f, champ_paint_t *p) {
    const float *kc = SP_CHAMP_COLOUR[k].kcolor;
    for (int i = 0; i < 4; i++) { p->tint[i] = SP_CHAMP_COLOUR[k].tint[i]; p->k[i] = kc[i]; }
    for (int i = 0; i < 3; i++) p->offset[i] = SP_CHAMP_COLOUR[k].offset[i];
    int anim = SP_CHAMP_COLOUR[k].anim;
    if (anim == SP_CA_PULSE) {
        float p1 = clamp01(sinf(0.1f * (float) f) + 0.5f), p2 = clamp01(sinf(0.3f * (float) f) + 0.5f);
        for (int i = 0; i < 4; i++) p->tint[i] = p->tint[i] * (1.0f - p2) + p2;
        for (int i = 0; i < 3; i++) p->offset[i] *= 1.0f - p2;
        p->k[3] = 1.0f - p1;
    } else if (anim == SP_CA_RAINBOW) {
        hsv_rgb((float) (f % 90) * 4.0f, p->k);
        p->k[3] = 1.0f;
        if (!layers) p->k[3] = 0.0f;              /* without layers the game's rainbow lands nowhere */
    } else if (anim == SP_CA_FLICKER) {
        int m = (int) (f % 190);                  /* 75 seen, 20 fading, 75 gone, 20 coming back */
        p->tint[3] *= m < 75 ? 1.0f : m < 95 ? 1.0f - (float) (m - 75) * 0.05f
                    : m < 170 ? 0.0f : (float) (m - 170) * 0.05f;
    } else if (anim == SP_CA_CAMO) {
        p->tint[0] = p->tint[1] = p->tint[2] = 0.0f;
        p->offset[0] = SP_PAPER_R / 255.0f * CAMO_SHADE;
        p->offset[1] = SP_PAPER_G / 255.0f * CAMO_SHADE;
        p->offset[2] = SP_PAPER_B / 255.0f * CAMO_SHADE;
    }
    if (!layers && anim != SP_CA_PULSE && kc[3] > 0.0f) {   /* Color(tint = KColor) * the entry's */
        for (int i = 0; i < 4; i++) p->tint[i] *= kc[i];
        p->k[3] = 0.0f;
    }
    if (!layers) p->k[3] = 0.0f;
}

/* scale: the sprite's own (+0xdc), as the game grows an entity - about the frame's pivot, here its top left */
static void picture_coloured(unsigned char *sprite, const char *anim, int frame, float x, float y,
                             const float *tint, const float *offset, float alpha, float scale) {
    unsigned long b = shim_image_base();
    if (!b) return;
    float *t = (float *) (sprite + SPRITE_TINT_AT), *o = (float *) (sprite + SPRITE_OFFSET_AT);
    float *sc = (float *) (sprite + SPRITE_SCALE_SP), was0 = sc[0], was1 = sc[1];
    ((void (*)(void *, const char *, int)) (b + 0x89eab8))(sprite, anim, frame);   /* SetFrame, as tweaks.c */
    t[0] = tint[0]; t[1] = tint[1]; t[2] = tint[2]; t[3] = tint[3] * alpha;
    o[0] = offset[0]; o[1] = offset[1]; o[2] = offset[2];
    sc[0] = sc[1] = scale;
    float pos[2] = { x, y };
    ((void (*)(void *, const float *, const void *, const void *)) (b + 0x89e4c0))(
        sprite, pos, (const void *) (b + ZERO_VECTOR), (const void *) (b + ZERO_VECTOR));
    t[0] = t[1] = t[2] = t[3] = 1.0f;
    o[0] = o[1] = o[2] = 0.0f;
    sc[0] = was0; sc[1] = was1;
}

/*
 * PULSATING (the owner, 20th pass: it did not pulse). The render (0x6978c4)
 * multiplies the sprite's own scale by 1 + clamp(sin 0.2f, 0, 1) * 0.15 for
 * the draw, f the frames since the room began (0x5b3520): half of each 31.4
 * frames at its size, half swelling to 15 % more and back. Drawn at 100 % as
 * everything here: the tool bakes the grown sizes about the entity's origin
 * (the sprite's scale centre), a whole pixel of growth a frame, and the card
 * shows the one nearest the game's scale. Answers the "Pulse" frame, or -1 for
 * the plain size.
 */
#define PULSE_ANM2  "gfx/port_enemy_pulse.anm2"
#define STATUS_ANM2 "gfx/StatusEffects.anm2"     /* the game's own name for it (0x68c060) */
static unsigned char g_pulse_art[SPRITE_BYTES] __attribute__((aligned(16)));
static signed char g_pulse_state;
static unsigned char g_status_art[SPRITE_BYTES] __attribute__((aligned(16)));
static signed char g_status_state;

static int pulse_frame(int c, long long f) {
    int n = SP_CHAMP_ART[c].pulse_n;
    float grow = clamp01(sinf(0.2f * (float) f));
    int k = (int) lroundf(grow * (float) n);
    if (k <= 0 || n <= 0 || !shim_picture_ready(g_pulse_art, &g_pulse_state, PULSE_ANM2)) return -1;
    return SP_CHAMP_ART[c].pulse_at + k - 1;
}

/* the open monster's sub-rows, cut at the opening edge, fading in as it passes them */
static void draw_enemy_subs(int set, float x, float y, float bottom, int icons) {
    list_t *l = &g_list[L_ENEMIES];
    float lh = line_h();
    int first = -1, last = -1;
    for (int k = 0; k < l->count; k++)
        if (g_enemy_sub[k] >= 0) { if (first < 0) first = k; last = k; }
    if (first < 0) return;
    float sub_top = g_enemy_real[first], edge = l->pos[last + 1];       /* the list's own units */
    long long f = now_ms() * 30 / 1000;
    int room_art = shim_picture_ready(g_room_art, &g_room_state, "gfx/port_room_icons.anm2");
    /* the guide down the opened sub-list, the paper's grey */
    for (float sy = sub_top + 2.0f; room_art && sy + 8.0f <= edge - 2.0f; sy += 8.0f) {
        float gy = floorf(y + sy - l->scroll);
        if (gy < y || gy + 8.0f > bottom) continue;
        shim_picture_draw(g_room_art, "Seam", 0, x + 6.0f, gy, 1.0f);
    }
    for (int k = first; k <= last; k++) {
        float top = g_enemy_real[k], h = g_enemy_h[k];
        if (top >= edge) break;
        float vis = clamp01((edge - top) / h);
        float row = floorf(y + top - l->scroll);
        if (row >= bottom) break;
        if (row + h <= y - FADE_UNITS) continue;
        float alpha = row_alpha(l, row, h) * (0.35f + 0.65f * vis) * vis;
        if (alpha <= 0.0f) continue;
        int e = g_enemy_row[k], j = g_enemy_sub[k];
        float iw, ih, idx, idy;
        sub_box(e, j, &iw, &ih, &idx, &idy);
        float ix = x + SUB_INDENT + floorf((ENEMY_COLUMN - iw) * 0.5f), iy = floorf(row + (h - ih) * 0.5f);
        ix += idx; iy += idy;                     /* the icon inside the box its overlay widens */
        const char *name;
        if (sub_is_champion(e, j)) {
            int c = SP_ENEMY[e].champ, tiny = j == SP_CHAMP_TINY, layers = SP_CHAMP_ART[c].layers;
            champ_paint_t p;
            champion_paint(j, layers, f, &p);
            /* Pulsating: the size the game gives it this frame, baked (see pulse_frame) */
            int grown = j == CHAMP_PULSATING ? pulse_frame(c, f) : -1;
            if (icons && grown >= 0) {
                picture_coloured(g_pulse_art, "Pulse", grown, ix + (float) SP_PULSE[grown].dx,
                                 iy + (float) SP_PULSE[grown].dy, p.tint, p.offset, alpha, 1.0f);
            } else if (icons) {
                int base = 4 * c + (tiny ? 2 : 0);
                float grow = j == CHAMP_LARGE ? LARGE_SCALE : 1.0f;
                picture_coloured(g_enemy_art, "Champ", base, ix, iy, p.tint, p.offset, alpha, grow);
                if (layers) {
                    picture_coloured(g_enemy_art, "Champ", base + 1, ix, iy, p.tint, p.offset, alpha, grow);
                    if (p.k[3] > 0.0f && !tiny) {   /* TINY's KColor is nought: no luminance of its own */
                        float kt[4] = { p.tint[0] * p.k[0], p.tint[1] * p.k[1], p.tint[2] * p.k[2], p.tint[3] };
                        picture_coloured(g_enemy_art, "Lum", c, ix, iy, kt, p.offset, alpha * p.k[3], grow);
                    }
                }
            }
            int o = sub_overlay(e, j);
            if (o >= 0 && icons) {
                int half = overlay_half(c);
                float px = ix + (float) SP_CHAMP_ART[c].ox, py = iy + (float) SP_CHAMP_ART[c].oy;
                int frame = (int) (f % SP_OVERLAY[o][half].frames);
                if (half)                         /* ours: pivot at the frame's top left */
                    shim_picture_draw(g_enemy_art, SP_OVERLAY[o][1].anim, frame,
                                      px - (float) SP_OVERLAY[o][1].px, py - (float) SP_OVERLAY[o][1].py, alpha);
                else if (shim_picture_ready(g_status_art, &g_status_state, STATUS_ANM2))
                    shim_picture_draw(g_status_art, SP_OVERLAY[o][0].anim, frame, px, py, alpha);
            }
            name = SP_CHAMP_COLOUR[j].name[set];
        } else {
            int c = sub_colour(e, j);
            if (icons) shim_picture_draw(g_enemy_art, "Colours", c, ix, iy, alpha);
            name = SP_BOSS_COLOUR_NAME[SP_BOSS_COLOUR[c].name][set];
        }
        write_text(name, x + SUB_INDENT + ENEMY_COLUMN + PICTURE_GAP, floorf(row + (h - lh) * 0.5f),
                   row_colour(k, k == l->chosen), alpha);
    }
}

static void draw_enemies(int set, float x, float y, float right, float bottom) {
    list_t *l = &g_list[L_ENEMIES];
    l->top = y; l->bottom = bottom;
    float lh = line_h();
    int art = tabs_art(set);
    int icons = shim_picture_ready(g_enemy_art, &g_enemy_state, "gfx/port_enemy_icons.anm2");
    for (int k = 0; k < l->count; k++) {
        float h = row_h(l, k), row = floorf(y + l->pos[k] - l->scroll);
        if (row >= bottom) break;
        if (row + h <= y - FADE_UNITS) continue;
        float alpha = row_alpha(l, row, h);
        if (alpha <= 0.0f) continue;
        int e = g_enemy_row[k];
        if (e < 0) {                              /* a heading, in the game's hand, the stroke after it */
            int g = -1 - e;
            if (!art) continue;
            float hw = (float) SP_HEAD[set][g].w, hh = (float) SP_HEAD[set][g].h;
            shim_picture_draw(g_tabs_art[set], "Heads", g, x, row + 2.0f, alpha);
            int frame = (int) ((right - x - hw - 8.0f) / (float) SP_RULE_STEP) - 1;
            if (frame >= SP_RULE_FRAMES) frame = SP_RULE_FRAMES - 1;
            if (frame >= 0)
                shim_picture_draw(g_tabs_art[set], "Rule", frame, x + hw + 8.0f,
                                  row + 2.0f + hh - (float) SP_RULE[frame].h + 1.0f, RULE_ALPHA * alpha);
            continue;
        }
        int j = g_enemy_sub[k];
        if (j >= 0) continue;                     /* the sub-rows: below, over everything */
        if (icons && SP_ENEMY[e].icon >= 0)
            shim_picture_draw(g_enemy_art, "Icons", SP_ENEMY[e].icon,
                              x + floorf((ENEMY_COLUMN - (float) SP_ENEMY[e].w) * 0.5f),
                              floorf(row + (h - (float) SP_ENEMY[e].h) * 0.5f), alpha);
        write_text(SP_ENEMY[e].name[set], x + ENEMY_COLUMN + PICTURE_GAP, floorf(row + (h - lh) * 0.5f),
                   row_colour(k, k == l->chosen), alpha);
    }
    if (g_open_enemy >= 0) draw_enemy_subs(set, x, y, bottom, icons);
}

static void draw_teleport_foot(int set, float x, float right, float foot);

static void draw_teleport(int set, float left, float x, float y, float right, float bottom) {
    list_t *r = &g_list[L_ROOMS], *f = &g_list[L_FLOORS];
    float lh = line_h();
    float split = floorf(x + g_rooms_w + COL_GAP * 0.5f);
    g_split = split;
    r->top = f->top = y;
    /* CLEAR ENEMIES and CLEAR PICKUPS stand at the foot, as STATS' two buttons do */
    float foot = floorf(bottom - lh);
    g_tp_foot = foot;
    bottom = foot - 3.0f;
    r->bottom = f->bottom = bottom;
    draw_teleport_foot(set, x, right, foot);
    int icons = shim_picture_ready(g_room_art, &g_room_state, "gfx/port_room_icons.anm2");
    (void) left;

    /* the seam between the columns, the paper's own grey */
    for (float sy = y + 2.0f; icons && sy + 8.0f <= bottom - 2.0f; sy += 8.0f)
        shim_picture_draw(g_room_art, "Seam", 0, split, sy, 1.0f);

    for (int k = 0; k < r->count; k++) {
        float h = row_h(r, k), row = floorf(y + r->pos[k] - r->scroll);
        if (row >= bottom) break;
        if (row + h <= y - FADE_UNITS) continue;
        float alpha = row_alpha(r, row, h);
        if (alpha <= 0.0f) continue;
        int state = shim_teleport_room_state(SP_ROOM[k].type, SP_ROOM[k].subtype);
        float dim = state == 1 ? 1.0f : PALE;
        if (icons)                                     /* no icon of its own: the plain room cell (the tool) */
            shim_picture_draw(g_room_art, "Icons", SP_ROOM[k].icon >= 0 ? SP_ROOM[k].icon : 0, x,
                              floorf(row + (h - (float) SP_ROOM_ICON) * 0.5f), alpha * dim);
        const char *name = SP_ROOM[k].name[set];
        float nx = x + (float) SP_ROOM_ICON + ROOM_GAP;
        write_text(name, nx, floorf(row + (h - lh) * 0.5f), row_colour(k, g_col == 0 && k == r->chosen),
                   alpha * dim);
        if (state == 2)                                /* you are here: the face's triangle after the name */
            write_text(HERE_ROOM, floorf(nx + text_w(name) + HERE_AIR), floorf(row + (h - lh) * 0.5f), NULL, alpha);
    }

    float fx = split + floorf(COL_GAP * 0.5f);
    float arrow_col = floorf(text_mark_w(HERE_FLOOR) + 0.5f) + ARROW_AIR;
    int here = shim_teleport_floor_here();
    for (int k = 0; k < f->count; k++) {
        float h = row_h(f, k), row = floorf(y + f->pos[k] - f->scroll);
        if (row >= bottom) break;
        if (row + h <= y - FADE_UNITS) continue;
        float alpha = row_alpha(f, row, h);
        if (alpha <= 0.0f) continue;
        int i = g_floor_row[k];
        float ty = floorf(row + (h - lh) * 0.5f);
        if (i == here)                                 /* you are here: the triangle before the icon */
            write_text(HERE_FLOOR, fx, ty, NULL, alpha);
        float ix = fx + arrow_col;
        if (icons)
            shim_picture_draw(g_room_art, "Stages", SP_FLOOR[i].level, ix,
                              floorf(row + (h - (float) SP_STAGE_H) * 0.5f), alpha);
        char name[96];
        snprintf(name, sizeof(name), "%s%s", SP_FLOOR[i].name[set], k == g_armed ? " ?" : "");
        const float *c = g_flash_row == 1000 + k ? FLASH
                         : ((g_col == 1 && k == f->chosen) || k == g_armed ? CHOSEN : NULL);
        write_text(name, ix + (float) SP_STAGE_W + STAGE_GAP, ty, c, alpha);
    }
    (void) right;
}

/*
 * THE TELEPORTER'S CLEARS (the owner, 21.10.2026): CLEAR ENEMIES removes every
 * enemy of the room - an NPC (type 10..999) that is an active enemy and not a
 * friend; CLEAR PICKUPS removes every pickup - type 5, pedestals and their
 * items, consumables, chests, shop goods, all of them. Remove, not kill (vtable
 * +0x58, as the arena code does): no deaths, no drops. The room then counts its
 * enemies itself once the pause is over, opens its doors and gives its reward.
 */
#define FLAGS_SP        0x1b8
#define FRIENDLY_SP     (1ull << 29)
#define TYPE_PICKUP_SP  5

void teleport_clear(int which) {
    unsigned long list = shim_eid_list_now();
    int gone = 0;
    if (list && shim_trace_readable((const void *) (list + EL_ARRAY), 16)) {
        const unsigned long *items = *(const unsigned long *const *) (list + EL_ARRAY);
        unsigned n = *(const unsigned *) (list + EL_COUNT);
        if (items && n <= 4096 && shim_trace_readable(items, (unsigned long) n * 8)) {
            int marked = 0;
            for (unsigned i = 0; i < n; i++) {
                unsigned long e = items[i];
                if (!e || !shim_trace_readable((const void *) e, FLAGS_SP + 8)
                    || !shim_trace_readable((const void *) (e + ENT_EXISTS_SP), 2)) continue;
                if (!*(const unsigned char *) (e + ENT_EXISTS_SP) || *(const unsigned char *) (e + ENT_DEAD)) continue;
                int t = *(const int *) (e + ENT_TYPE_SP), take = 0;
                if (which == 0)
                    take = t >= 10 && t < 1000 && !(*(const unsigned long long *) (e + FLAGS_SP) & FRIENDLY_SP)
                           && ((int (*)(unsigned long)) (*(const unsigned long *) (*(const unsigned long *) e
                                                                                   + VT_ACTIVE_ENEMY)))(e);
                else
                    take = t == TYPE_PICKUP_SP;
                if (!take) continue;
                if (!marked) { shim_spawner_mark_run(); marked = 1; }
                ((void (*)(unsigned long)) (*(const unsigned long *) (*(const unsigned long *) e + VT_REMOVE)))(e);
                gone++;
            }
        }
    }
    if (gone) stat_confirm(4000 + which);
    else note(SP_NOTHING_CLEAR[set_now()]);
    if (shim_report_active()) {
        char line[96];
        snprintf(line, sizeof(line), "spawner: %s - %d removed", which ? "clear pickups" : "clear enemies", gone);
        report_line("spawner", line);
    }
}

static void draw_teleport_foot(int set, float x, float right, float foot) {
    float width = right - x;
    if (g_note_left) {
        write_text(g_note, floorf(x + (width - text_w(g_note)) * 0.5f), foot, GREY, 1.0f);
        g_tp_split = x + width * 0.5f;
        return;
    }
    const char *a = SP_CLEAR_ENEMIES[set], *p = SP_CLEAR_PICKUPS[set];
    float gap = 24.0f, wa = text_w(a), wp = text_w(p);
    float fx = floorf(x + (width - wa - gap - wp) * 0.5f);
    write_text(a, fx, foot, g_flash_row == 4000 ? FLASH : (g_col == 2 && g_tp_button == 0 ? CHOSEN : GREY), 1.0f);
    write_text(p, floorf(fx + wa + gap), foot,
               g_flash_row == 4001 ? FLASH : (g_col == 2 && g_tp_button == 1 ? CHOSEN : GREY), 1.0f);
    g_tp_split = fx + wa + gap * 0.5f;
}

static void stat_shown(unsigned long p, int s, char *out, size_t n) {
    float v = p ? shim_stats_value(p, s) : 0.0f;
    if (s == STAT_LUCK && fabsf(v - roundf(v)) < 0.005f) snprintf(out, n, "%.0f", v);
    else snprintf(out, n, "%.2f", v);
}

/*
 * The switch, centred: < PLAYER 1: Isaac >, the number the owner's and the
 * name the body's own (a twin: PLAYER 1: Esau). Drawn only with more than one
 * body to choose from; nothing is kept for it otherwise. Answers the y under it.
 */
static float g_who_top, g_who_bottom;         /* where it was drawn, for the finger */

static const char *who_name(unsigned long p, int set) {
    int type = shim_stats_player_type(p);
    return type >= 0 && type < SP_CHARACTERS ? SP_CHARACTER[type][set] : "?";
}

/*
 * A body's name on the switch. Two bodies of one player can carry the same
 * name - the game calls both forms of T.Lazarus "Lazarus" (29 and 38) - so
 * the later one in the switch's order is told apart by its place (the owner,
 * 05.10.2026: "Lazarus (2)", the game's name and " (2)" in every language).
 */
static void who_label(unsigned long p, int set, char *out, size_t n) {
    unsigned long l[PLAYERS_SHOWN];
    int count = who_list(l), number = shim_stats_player_number(p), same = 0;
    const char *name = who_name(p, set);
    for (int i = 0; i < count && l[i] != p; i++)
        if (shim_stats_player_number(l[i]) == number && !strcmp(who_name(l[i], set), name)) same++;
    char named[64];
    if (same) snprintf(named, sizeof(named), "%s (%d)", name, same + 1);
    else snprintf(named, sizeof(named), "%s", name);
    snprintf(out, n, SP_PLAYER_FMT[set], number, named);
}

static float draw_who(int set, float left, float width, float y, const float *colour) {
    g_who_top = g_who_bottom = y;
    if (!who_shown()) return y;
    unsigned long p = who_player();
    char who[96], line[128];
    who_label(p, set, who, sizeof(who));
    snprintf(line, sizeof(line), "< %s >", who);
    write_text(line, floorf(left + (width - text_w(line)) * 0.5f), y - 2.0f, colour, 1.0f);
    y += floorf(line_h() + 1.0f);
    g_who_bottom = y;
    return y;
}

/*
 * STATS drawn (mock-ups v2, 07.10.2026): the player's switch, the list - each
 * section under its heading in the hand, a row the HUD's own picture, its name
 * and its value at the right edge, the chosen one between arrows - and the
 * foot: CHARGE ACTIVE ITEMS and RESET side by side, or a note, a hint or what
 * is being typed in their place.
 */
static void draw_stats(int set, float left, float width, float y, float bottom) {
    float lh = line_h();
    unsigned long p = stat_player();
    char line[160];
    stats_rows();

    y = draw_who(set, left, width, y, g_stat_focus == FOCUS_WHO ? CHOSEN : NULL);
    float x = left + CARD_PAD, vx = left + width - CARD_PAD;
    float foot = floorf(bottom - lh);
    g_stat_foot = foot;
    list_t *l = &g_list[L_STATS];
    l->top = y; l->bottom = foot - 3.0f;
    int art = tabs_art(set);
    int icons = shim_picture_ready(g_stat_art, &g_stat_state, "gfx/port_stat_icons.anm2");
    int pickups = shim_picture_ready(g_pickup_art, &g_pickup_state, "gfx/port_pickup_icons.anm2");
    int type = p ? shim_stats_player_type(p) : -1;
    for (int k = 0; k < l->count; k++) {
        float h = row_h(l, k), row = floorf(y + l->pos[k] - l->scroll);
        if (row >= l->bottom) break;
        if (row + h <= y - FADE_UNITS) continue;
        float alpha = row_alpha(l, row, h);
        if (alpha <= 0.0f) continue;
        const srow_t *r = &g_srow[k];
        if (r->kind == SR_HEAD) {
            if (art)
                draw_heading(set, "Sections", r->id, (float) SP_SECTION[set][r->id].w, (float) SP_SECTION[set][r->id].h,
                             x, row, vx, alpha);
            continue;
        }
        int chosen = g_stat_focus == FOCUS_LIST && k == l->chosen;
        const float *c = g_flash_row == 2000 + k ? FLASH : (chosen ? CHOSEN : NULL);
        float iy = floorf(row + (h - 16.0f) * 0.5f);
        const char *name = "?";
        char v[24], named[96];
        if (r->kind == SR_STAT) {
            if (icons) shim_picture_draw(g_stat_art, "Icons", r->id, x + 2.0f, iy, alpha);
            name = SP_STAT_NAME[set][r->id];
            if (chosen && g_typing) snprintf(v, sizeof(v), "%s_", g_typed);
            else if (chosen && pend_here()) pend_shown(p, v, sizeof(v));
            else stat_shown(p, r->id, v, sizeof(v));
        } else {
            int halves = r->kind == SR_HEALTH && shim_health_halves(r->id);
            if (pickups && r->kind == SR_COUNTER)
                shim_picture_draw(g_pickup_art, "Counters", r->id,
                                  x + 2.0f + floorf((16.0f - (float) SP_COUNTER_ICON[r->id].w) * 0.5f),
                                  floorf(row + (h - (float) SP_COUNTER_ICON[r->id].h) * 0.5f), alpha);
            if (pickups && r->kind == SR_HEALTH && r->id < SP_HEALTH_ROWS)
                shim_picture_draw(g_pickup_art, "Health", r->id,
                                  x + 2.0f + floorf((16.0f - (float) SP_HEALTH_ICON[r->id].w) * 0.5f),
                                  floorf(row + (h - (float) SP_HEALTH_ICON[r->id].h) * 0.5f), alpha);
            if (r->kind == SR_COUNTER) {
                name = SP_COUNTER_NAME[set][r->id];
            } else if (r->id < SP_HEALTH_ROWS) {
                name = SP_HEALTH_NAME[set][r->id];
                if (r->other) {                     /* the other body's: "The Soul: Soul hearts" */
                    int whose = type == TYPE_THE_FORGOTTEN ? TYPE_FORGOTTEN_SOUL : TYPE_THE_FORGOTTEN;
                    snprintf(named, sizeof(named), SP_SOUL_FMT[set], SP_CHARACTER[whose][set], name);
                    name = named;
                }
            }
            if (chosen && g_typing) snprintf(v, sizeof(v), "%s_", g_typed);
            else if (chosen && pend_here()) pend_shown(p, v, sizeof(v));
            else units_shown(p ? srow_value(p, r) : 0, halves, v, sizeof(v));
        }
        float ty = floorf(row + (h - lh) * 0.5f);
        write_text(name, x + 24.0f, ty, c, alpha);
        char shown[40];
        if (chosen) snprintf(shown, sizeof(shown), "<  %s  >", v);
        else snprintf(shown, sizeof(shown), "%s", v);
        float sw = text_w(shown);
        float blink = 1.0f;
        if (chosen && !g_typing && pend_here())   /* waiting: the value breathes, 1 -> 0.45 -> 1 in 0.8 s */
            blink = 0.725f + 0.275f * cosf((float) (now_ms() % 800) * (6.2831853f / 800.0f));
        write_text(shown, floorf(vx - sw), ty, c, alpha * blink);
        if (chosen) {
            g_arrow_l0 = vx - sw; g_arrow_l1 = g_arrow_l0 + text_w("<");
            g_arrow_r1 = vx; g_arrow_r0 = vx - text_w(">");
        }
    }

    /* the foot: what is typed, a note, a stat's own rule, or the two buttons */
    const srow_t *r = stat_chosen();
    const char *foot_text = NULL;
    if (!g_typing && r && pend_here()) {
        foot_text = SP_APPLY_HINT[set];
    } else if (g_typing && r && r->kind == SR_STAT && r->id < 4) {
        float typed = strtof(g_typed, NULL), now = p ? shim_stats_value(p, r->id) : 0.0f;
        snprintf(line, sizeof(line), SP_TYPING_FMT[set], g_typed[0] ? g_typed : "0",
                 (double) (now > 0.001f ? typed / now : 1.0f));
        foot_text = line;
    } else if (g_note_left) {
        foot_text = g_note;
    } else if (r && r->kind == SR_STAT && (g_typing || r->id == STAT_SHOT)) {
        stat_hint(r->id, line, sizeof(line));
        if (line[0]) foot_text = line;
    }
    if (foot_text) {
        /* "Charged" stands where the button was: it flashes in its place */
        int flashing = g_flash_row == 3000 + FOOT_CHARGE || g_flash_row == 3000 + FOOT_RESET;
        write_text(foot_text, floorf(left + (width - text_w(foot_text)) * 0.5f), foot, flashing ? FLASH : GREY, 1.0f);
        g_foot_split = left + width * 0.5f;
        return;
    }
    const char *charge = SP_CHARGE[set], *reset = SP_RESET[set];
    float gap = 24.0f, wc = text_w(charge), wr = text_w(reset);
    float fx = floorf(left + (width - wc - gap - wr) * 0.5f);
    write_text(charge, fx, foot, g_flash_row == 3000 + FOOT_CHARGE ? FLASH
                                 : (g_stat_focus == FOCUS_FOOT && g_foot_button == FOOT_CHARGE ? CHOSEN : GREY), 1.0f);
    write_text(reset, floorf(fx + wc + gap), foot, g_flash_row == 3000 + FOOT_RESET ? FLASH
               : (g_stat_focus == FOCUS_FOOT && g_foot_button == FOOT_RESET ? CHOSEN : GREY), 1.0f);
    g_foot_split = fx + wc + gap * 0.5f;
}

/*
 * FIVE ROWS (the owner, 07.10.2026: the list shows five rows again). A set
 * whose heading or tabs stand taller than the English ones would cut the
 * fifth row by a unit or two (Russian: the title 29 against 27, the tabs 16
 * against 15) - so the air under the title and under the search line gives
 * back what the five rows need, up to what still keeps them apart.
 */
#define TITLE_AIR      4.0f
#define TITLE_AIR_LEAST 2.0f
#define QUERY_AIR_GIVE 4.0f

static float g_title_cut;

static void five_rows(int set, float top, float bottom) {
    float tall = 0.0f;
    for (int k = 0; k < TABS; k++)
        if (SP_TAB[set][k].h > tall) tall = (float) SP_TAB[set][k].h;
    float list_top = floorf(top + CARD_PAD * 0.6f) + (float) SP_TITLE[set].h + TITLE_AIR + tall + 9.0f
                     + floorf(line_h() + 6.0f);
    float need = 5.0f * item_step() - (bottom - list_top);
    g_query_cut = 0.0f;
    g_title_cut = 0.0f;
    if (need <= 0.0f) return;
    g_query_cut = need < QUERY_AIR_GIVE ? need : QUERY_AIR_GIVE;
    need -= g_query_cut;
    float title_give = TITLE_AIR - TITLE_AIR_LEAST;
    g_title_cut = need < title_give ? need : title_give;
}

void shim_spawner_draw(void) {
    strap_place();
    if (!g_open) return;
    unsigned long b = shim_image_base();
    unsigned long d = dialog_of(g_dialog);
    if (!b || !d || *(const int *) (d + DIALOG_STATE) != STATE_OPEN) return;

    const float *canvas = (const float *) (b + CANVAS_SIZE);
    const float *size = (const float *) (d + DIALOG_SIZE);
    const float *slide = (const float *) (d + DIALOG_SLIDE);
    float left = floorf(canvas[0] * 0.5f + slide[0] - size[0] * 0.5f);
    float top = floorf(canvas[1] * 0.5f + slide[1] - size[1] * 0.5f);
    float x = left + CARD_PAD, y = floorf(top + CARD_PAD * 0.6f);
    float bottom = floorf(top + size[1] - CARD_PAD * 0.6f);
    g_card_x = left; g_card_y = top; g_card_w = size[0]; g_card_h = size[1];

    /* the heading, centred, in the game's hand */
    int set = set_now();
    char anm2[64];
    snprintf(anm2, sizeof(anm2), "gfx/port_itemspawn_title_%s.anm2", shim_tweaks_set_suffix());
    if (shim_picture_ready(g_title[set], &g_title_state[set], anm2))
        /* on whole units: a 100 % picture half a unit off samples between texels
           and jumps at ORIGINAL and POTATO (tweaks.c, centred) */
        shim_picture_draw(g_title[set], "Idle", 0, floorf(left + (size[0] - (float) SP_TITLE[set].w) * 0.5f),
                          y, 1.0f);
    five_rows(set, top, bottom);
    y += (float) SP_TITLE[set].h + TITLE_AIR - g_title_cut;
    y = draw_tabs(set, left, size[0], y);
    pictures_frame();

    float right = left + size[0] - CARD_PAD;
    switch (g_tab) {
    case TAB_ITEMS:
        draw_items(set, x, draw_query(x, draw_who(set, left, size[0], y, NULL), right, 1), right, bottom);
        break;
    case TAB_ENEMIES:  draw_enemies(set, x, draw_query(x, y, right, 0), right, bottom); break;
    case TAB_TELEPORT: draw_teleport(set, left, x, y, left + size[0] - CARD_PAD, bottom); break;
    default:           draw_stats(set, left, size[0], y, bottom); break;
    }
}

/*
 * The finger, while the card is up, is the card's alone. On the rows a finger
 * that travels drags the list and, let go of, hands it its speed; one that
 * lifts where it landed is a tap: it picks the row, and picks it again to
 * act. A tap on a tab shows it, on the line brings the keyboard back if it
 * was put away, and beside the paper closes the card.
 */
static volatile int g_strap_finger = -1;    /* the finger that opened the card, still down */
static int g_row_finger = -1;
static int g_row_list = -1;
static float g_row_from;                    /* units, where it came down */
static float g_row_from_x;
static float g_scroll_from;
static float g_last_gy, g_last_move;        /* the finger's last place, and its move */
static int g_dragged;

int shim_spawner_touch(int id, int phase, float x, float y) {
    unsigned long b = shim_image_base();
    if (!b) return 0;
    if (id == g_strap_finger) {                 /* its move and lift are ours too */
        if (phase >= 3) g_strap_finger = -1;
        return 1;
    }
    const unsigned *area = (const unsigned *) (b + AREA);
    if (!area[0] || !area[1]) return g_open;
    float gx = (x - (float) area[2]) * shim_screen_width() / (float) area[0];
    float gy = (y - (float) area[3]) * shim_screen_height() / (float) area[1];

    if (!g_open) {
        if (phase != 0 || !g_strap_shown || !shim_tweaks_spawner() || !shim_sticks_in_game()
            || !pause_main(b))
            return 0;
        int set = set_now();
        float sw = (float) (STRAP_SIZE[set][STRAP_SPAWNER].w - STRAP_EXTEND);
        float rise = (float) STRAP_SIZE[set][STRAP_SPAWNER].rise;
        float sh = (float) STRAP_SIZE[set][STRAP_SPAWNER].h - rise;
        if (gx < g_strap_x || gx > g_strap_x + sw || gy < g_strap_y - rise || gy > g_strap_y + sh) return 0;
        g_strap_finger = id;
        shim_spawner_open();
        return 1;
    }

    if (id == g_row_finger) {
        list_t *l = &g_list[g_row_list];
        if (phase == 1 || phase == 2) {
            float dy = gy - g_row_from;
            if (!g_dragged && (dy > DRAG_UNITS || dy < -DRAG_UNITS)) { g_dragged = 1; g_drag_started = 1; }
            if (g_dragged) {
                float most = list_most(l);
                float want = g_scroll_from - dy;
                l->scroll = want < 0.0f ? 0.0f : (want > most ? most : want);
                g_last_move = g_last_gy - gy;
                l->target = -1.0f;
            }
            g_last_gy = gy;
        } else if (phase >= 3) {
            if (phase == 4) {
                /* cancelled (a palm, the system - U7): no tap on a row, no fling */
            } else if (!g_dragged) {
                int k = list_at(l, g_row_from);
                if (k >= 0) { g_tap_x = g_row_from_x; g_tap_row = k; g_tap_list = g_row_list; }
            } else {
                l->velocity = g_last_move;      /* the last move a frame, carried on */
            }
            g_row_finger = -1;
        }
        return 1;
    }
    if (phase != 0) return 1;

    if (gx < g_card_x || gx > g_card_x + g_card_w || gy < g_card_y || gy > g_card_y + g_card_h) {
        g_want_close = 1;
        return 1;
    }
    if (gy >= g_tabs_top && gy < g_tabs_bottom) {
        float cell = (g_card_w - 2.0f * CARD_PAD) / (float) TABS;
        int k = (int) ((gx - g_card_x - CARD_PAD) / cell);
        if (k >= 0 && k < TABS) g_want_tab = k;
        return 1;
    }
    if (gy < g_tabs_bottom) return 1;
    if (g_tab == TAB_ITEMS && gy >= g_who_top && gy < g_who_bottom) {      /* the switch: its halves step back and on */
        g_want_who = gx < g_card_x + g_card_w * 0.5f ? -1 : 1;
        return 1;
    }
    if (g_tab == TAB_STATS) {
        /* the list's rows are a list's (a drag scrolls them); the player's line and the foot are taps */
        list_t *sl = &g_list[L_STATS];
        if (gy < sl->top || gy >= g_stat_foot) {
            g_stat_tap_x = gx; g_stat_tap_y = gy; g_stat_tap = 1;
            return 1;
        }
    }
    if (g_tab == TAB_TELEPORT && g_tp_foot > 0.0f && gy >= g_tp_foot) {   /* the foot's two buttons */
        if (!g_note_left) g_want_clear = gx < g_tp_split ? 1 : 2;
        return 1;
    }
    int l = -1;
    if (g_tab == TAB_TELEPORT) l = gx < g_split ? L_ROOMS : L_FLOORS;
    else if (g_tab == TAB_STATS) l = L_STATS;
    else if (gy < g_query_bottom) {
        if (g_tab == TAB_ITEMS && g_toggle_x > 0.0f && gx >= g_toggle_x - 4.0f) g_want_ground = 1;   /* the toggle */
        else if (g_tab == TAB_ITEMS && g_held_x > 0.0f && gx >= g_held_x - 4.0f) g_want_held = 1;
        else g_want_keyboard = 1;
    }
    else l = g_tab == TAB_ITEMS ? L_ITEMS : L_ENEMIES;
    if (l >= 0 && gy >= g_list[l].top && g_row_finger < 0) {
        g_row_finger = id;
        g_row_list = l;
        g_row_from = gy;
        g_row_from_x = gx;
        g_last_gy = gy;
        g_last_move = 0.0f;
        g_scroll_from = g_list[l].scroll;
        g_list[l].velocity = 0.0f;
        g_dragged = 0;
    }
    return 1;
}
