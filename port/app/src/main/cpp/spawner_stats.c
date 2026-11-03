/*
 * THE STATS TAB OF THE SPAWNER - an exact value for a stat (the owner,
 * 02.10.2026: "as the PC console does"; the value typed is the value wanted,
 * it holds for the rest of the run as if it had always been so, and in co-op
 * the player is chosen).
 *
 * NO HOOKS (the owner's choice, 02.10.2026). EvaluateItems (0x4f0ee4) builds
 * every stat afresh from the player's items each time, so a write to a stat
 * itself is lost at the next pickup. What the game keeps instead, and saves
 * with the run (the research notes of 02.10.2026, stats editor):
 *
 *   speed, tears, damage, range   D8's own multipliers player+0x1db8 / 0x1dc0 /
 *                                 0x1db4 / 0x1dbc, applied to every character
 *                                 every time (saved, gsp+0x3bc..0x3c8)
 *   luck                          the Eden luck roll player+0x1944, ADDED to
 *                                 every character's luck (saved, gsp+0x78)
 *   shot speed                    no fraction of its own: the integer counter
 *                                 player+0x1918 (0.2 a count before the item
 *                                 multipliers), so the value is the nearest
 *                                 one the counter can make
 *
 * The multiplier is SOLVED BY THE GAME rather than modelled: a first guess
 * m * T / V, then the stat's cache flag, EvaluateItems, the stat read back, and
 * a secant on what came out - Cracked Crown, the co-op baby's halving, the
 * twins' shared speed and Dogma are all inside the game's own answer. Items
 * taken later go on changing the stat from the new value, multiplied as D8's
 * own are. D8 rerolls the four multipliers, as it does in the game.
 *
 * Everything runs on the frame thread between frames (the card's own turn,
 * spawner.c), the run marked first - EvaluateItems unlocks inside.
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "diag.h"

unsigned long shim_image_base(void);
int shim_report_active(void);
void report_line(const char *kind, const char *text);
int shim_eid_players(unsigned long *out, int max);
void shim_spawner_mark_run(void);
int shim_spawner_run_key(int *slot, unsigned *seed, int *challenge);
const char *shim_saves_dir(void);
FILE *shim_safe_fopen(const char *path, const char *mode);
int shim_safe_fclose(FILE *f);

#define FN_EVALUATE_ITEMS  0x4f0ee4
#define FN_HAS_COLLECTIBLE 0x4eeb74         /* (player, id, ignore modifiers) */
#define PLAYER_VARIANT     0x3c             /* 1: the co-op baby, never saved (0x569b6c) */
#define PLAYER_PARENT      0x390            /* a body that copies its parent's speed */
#define PLAYER_TWIN        0x2580           /* Jacob and Esau: the speed is their average */
#define PLAYER_TYPE        0x1738
#define PLAYER_CACHE_FLAGS 0x1958
#define PLAYER_LUCK_ADD    0x1944           /* float, added to everyone's luck (0x4fa514) */
#define PLAYER_SHOT_COUNT  0x1918           /* int, 0.2 shot speed a count (0x4f3cc4) */
#define ITEM_ROCK_BOTTOM   562
#define PLAYERS_MAX        8

#define STAT_SPEED 0
#define STAT_TEARS 1
#define STAT_DAMAGE 2
#define STAT_RANGE 3
#define STAT_SHOT 4
#define STAT_LUCK 5
#define STATS 6

/* in the order the tab lists them (the HUD's own) */
static const struct {
    unsigned d8;                            /* D8's multiplier, 0 for none */
    unsigned value;                         /* the stat as EvaluateItems leaves it */
    unsigned flag;                          /* its bit in the cache flags */
    unsigned rock;                          /* Rock Bottom's kept highest */
    float least, most;                      /* what the game lets the stat be */
    const char *name;
} STAT[STATS] = {
    { 0x1db8, 0x194c, 0x10,  0x25d8, 0.10f, 2.00f,   "speed" },
    { 0x1dc0, 0x1834, 0x02,  0x25dc, 0.01f, 120.0f,  "tears" },
    { 0x1db4, 0x1844, 0x01,  0x25e0, 0.50f, 1.0e6f,  "damage" },
    { 0x1dbc, 0x1854, 0x08,  0x25e4, 1.00f, 1.0e6f,  "range" },
    { 0,      0x1838, 0x04,  0x25e8, 0.60f, 1.0e6f,  "shot speed" },
    { 0,      0x1950, 0x400, 0x25ec, -1.0e6f, 1.0e6f, "luck" },
};

#define D8_LEAST   0.01f
#define D8_MOST    100.0f
#define HIT        0.005f                   /* close enough: the HUD shows two places */
#define SOLVE_MAX  5                        /* evaluations after the first guess */

/* ------------------------------------------------------------ the players */
/*
 * The players the tab offers: the run's list (Game+0x25c40) without the co-op
 * babies, which are never saved and are counted by their owner - the same
 * players the run save writes (0x569b6c).
 */
int shim_stats_players(unsigned long *out, int max) {
    unsigned long all[PLAYERS_MAX];
    int n = shim_eid_players(all, PLAYERS_MAX), k = 0;
    for (int i = 0; i < n && k < max; i++)
        if (all[i] && *(const int *) (all[i] + PLAYER_VARIANT) == 0) out[k++] = all[i];
    return k;
}

int shim_stats_player_type(unsigned long p) { return p ? *(const int *) (p + PLAYER_TYPE) : -1; }

/*
 * WHOSE BODY IT IS (the owner, 20th pass, 05.10.2026: "player one - such a
 * character, then such a one"). One player can stand in the list as more than
 * one body holding things of its own: Esau beside Jacob, T.Forgotten's soul,
 * T.Lazarus' other form once Birthright is taken - each a twin (+0x2580) made
 * right after its main or added later at the end (the co-op twins notes) - and a
 * body with a parent (+0x390), Strawman's Keeper. Such a body counts with its
 * owner: a twin with the one before it, a child with its parent. The number is
 * the owner's place among the owners, in the list's order - the join order.
 */
static int offered_at(const unsigned long *l, int n, unsigned long p) {
    for (int i = 0; p && i < n; i++)
        if (l[i] == p) return i;
    return -1;
}

static int owner_of(const unsigned long *l, int n, int i) {
    for (int guard = 0; guard < n; guard++) {
        int j = offered_at(l, n, *(const unsigned long *) (l[i] + PLAYER_PARENT));
        if (j >= 0 && j != i) { i = j; continue; }
        j = offered_at(l, n, *(const unsigned long *) (l[i] + PLAYER_TWIN));
        if (j >= 0 && j < i) { i = j; continue; }
        break;
    }
    return i;
}

/* 1 for the first player and every body of his, 2 for the next; 0 when p is not offered */
int shim_stats_player_number(unsigned long p) {
    unsigned long l[PLAYERS_MAX];
    int n = shim_stats_players(l, PLAYERS_MAX), i = offered_at(l, n, p);
    if (i < 0) return 0;
    int owner = owner_of(l, n, i), number = 1;
    for (int k = 0; k < owner; k++)
        if (owner_of(l, n, k) == k) number++;
    return number;
}

static int in_run(unsigned long p) {
    unsigned long all[PLAYERS_MAX];
    int n = shim_eid_players(all, PLAYERS_MAX);
    for (int i = 0; i < n; i++)
        if (all[i] == p) return 1;
    return 0;
}

/* the stat as the HUD shows it */
float shim_stats_value(unsigned long p, int s) {
    if (!p || s < 0 || s >= STATS) return 0.0f;
    float v = *(const float *) (p + STAT[s].value);
    if (s == STAT_TEARS) return v > -0.999f ? 30.0f / (v + 1.0f) : 0.0f;
    if (s == STAT_RANGE) return v / 40.0f;
    return v;
}

void shim_stats_limits(int s, float *least, float *most) {
    if (s < 0 || s >= STATS) return;
    if (least) *least = STAT[s].least;
    if (most) *most = STAT[s].most;
}

static void evaluate(unsigned long b, unsigned long p, unsigned flag) {
    *(unsigned *) (p + PLAYER_CACHE_FLAGS) |= flag;
    ((void (*)(unsigned long)) (b + FN_EVALUATE_ITEMS))(p);
}

/* ------------------------------------------------------------ what was there before */
/*
 * RESET puts back the fields as they were before the tab first touched them,
 * per player of this run; kept beside the saves (port-stat-edits.txt, the safe
 * writer) so a Continue can still reset. A run is its slot, seed and challenge,
 * as the spawner's run mark keys it.
 */
#define KEPT_MAX 16

typedef struct {
    int slot, challenge, player;
    unsigned seed;
    float d8[4];                            /* speed, tears, damage, range */
    float luck;
    int shot;
} kept_t;

static kept_t g_kept[KEPT_MAX];
static int g_kept_n, g_kept_loaded;

static int kept_path(char *out, size_t n) {
    const char *saves = shim_saves_dir();
    if (!saves || !*saves) return 0;
    snprintf(out, n, "%s/port-stat-edits.txt", saves);
    return 1;
}

static void kept_load(void) {
    char path[600];
    if (g_kept_loaded || !kept_path(path, sizeof(path))) return;
    g_kept_loaded = 1;
    FILE *f = fopen(path, "r");
    if (!f) return;
    char line[256];
    while (g_kept_n < KEPT_MAX && fgets(line, sizeof(line), f)) {
        kept_t k;
        if (line[0] == '#') continue;
        if (sscanf(line, "%d %u %d %d %f %f %f %f %f %d", &k.slot, &k.seed, &k.challenge, &k.player,
                   &k.d8[0], &k.d8[1], &k.d8[2], &k.d8[3], &k.luck, &k.shot) == 10)
            g_kept[g_kept_n++] = k;
    }
    fclose(f);
}

static void kept_save(void) {
    char path[600];
    if (!kept_path(path, sizeof(path))) return;
    FILE *f = shim_safe_fopen(path, "w");
    if (!f) return;
    fprintf(f, "# the stats tab: fields before the first edit - slot seed challenge player "
               "d8(speed tears damage range) luck shotcount\n");
    for (int i = 0; i < g_kept_n; i++)
        fprintf(f, "%d %u %d %d %.6f %.6f %.6f %.6f %.6f %d\n", g_kept[i].slot, g_kept[i].seed,
                g_kept[i].challenge, g_kept[i].player, g_kept[i].d8[0], g_kept[i].d8[1], g_kept[i].d8[2],
                g_kept[i].d8[3], g_kept[i].luck, g_kept[i].shot);
    if (shim_safe_fclose(f) != 0 && shim_report_active())
        report_line("stats", "port-stat-edits.txt NOT written - RESET holds for this session only");
}

static int player_index(unsigned long p) {
    unsigned long list[PLAYERS_MAX];
    int n = shim_stats_players(list, PLAYERS_MAX);
    for (int i = 0; i < n; i++)
        if (list[i] == p) return i;
    return -1;
}

static kept_t *kept_find(unsigned long p, int make) {
    int slot, challenge;
    unsigned seed;
    int who = player_index(p);
    if (who < 0 || !shim_spawner_run_key(&slot, &seed, &challenge)) return NULL;
    kept_load();
    for (int i = 0; i < g_kept_n; i++)
        if (g_kept[i].slot == slot && g_kept[i].seed == seed && g_kept[i].challenge == challenge
            && g_kept[i].player == who)
            return &g_kept[i];
    if (!make) return NULL;
    if (g_kept_n == KEPT_MAX) {                 /* the oldest goes */
        memmove(&g_kept[0], &g_kept[1], sizeof(kept_t) * (KEPT_MAX - 1));
        g_kept_n--;
    }
    kept_t *k = &g_kept[g_kept_n++];
    k->slot = slot; k->seed = seed; k->challenge = challenge; k->player = who;
    k->d8[0] = *(const float *) (p + STAT[STAT_SPEED].d8);
    k->d8[1] = *(const float *) (p + STAT[STAT_TEARS].d8);
    k->d8[2] = *(const float *) (p + STAT[STAT_DAMAGE].d8);
    k->d8[3] = *(const float *) (p + STAT[STAT_RANGE].d8);
    k->luck = *(const float *) (p + PLAYER_LUCK_ADD);
    k->shot = *(const int *) (p + PLAYER_SHOT_COUNT);
    kept_save();
    return k;
}

int shim_stats_can_reset(unsigned long p) { return p && kept_find(p, 0) != NULL; }

/* ------------------------------------------------------------ the solve */
/* Rock Bottom keeps a stat at its highest: to go lower, its highest goes first */
static void below_rock_bottom(unsigned long b, unsigned long p, int s, float target, float now) {
    if (target >= now) return;
    if (!((int (*)(unsigned long, int, int)) (b + FN_HAS_COLLECTIBLE))(p, ITEM_ROCK_BOTTOM, 0)) return;
    float least = s == STAT_LUCK ? -1.0e6f : 0.0f;
    *(float *) (p + STAT[s].rock) = least;
}

/*
 * The players a multiplier is written to. Speed is not each player's own when
 * it is shared: a body with a parent copies the parent's (0x4fb2ac), and the
 * twins take their average (0x4fb14c) - so the parent is edited, and both
 * twins by the same factor.
 */
static int speed_group(unsigned long p, unsigned long out[2]) {
    unsigned long parent = *(const unsigned long *) (p + PLAYER_PARENT);
    if (parent && in_run(parent)) p = parent;
    out[0] = p;
    unsigned long twin = *(const unsigned long *) (p + PLAYER_TWIN);
    if (twin && twin != p && in_run(twin)) { out[1] = twin; return 2; }
    return 1;
}

static float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }

typedef struct { unsigned long who[2]; int n; } group_t;

static void write_d8(const group_t *g, int s, float m, const float *base, float base_m) {
    for (int i = 0; i < g->n; i++) {
        /* a twin keeps its own ratio to the edited one */
        float v = base_m > 0.0f ? base[i] * (m / base_m) : m;
        *(float *) (g->who[i] + STAT[s].d8) = clampf(v, D8_LEAST, D8_MOST);
    }
}

static void evaluate_group(unsigned long b, const group_t *g, unsigned flag) {
    for (int i = 0; i < g->n; i++) evaluate(b, g->who[i], flag);
}

/* the multipliers: a guess, then the secant on what the game made of it */
static int solve_d8(unsigned long b, unsigned long p, int s, float target, float *got) {
    group_t g = { { p, 0 }, 1 };
    if (s == STAT_SPEED) g.n = speed_group(p, g.who);
    float base[2];
    for (int i = 0; i < g.n; i++) base[i] = *(const float *) (g.who[i] + STAT[s].d8);
    float m0 = base[0], v0 = shim_stats_value(p, s);
    float m1 = (v0 > 0.01f && m0 > D8_LEAST) ? m0 * target / v0 : 1.0f;
    m1 = clampf(m1, D8_LEAST, D8_MOST);
    write_d8(&g, s, m1, base, base[0]);
    evaluate_group(b, &g, STAT[s].flag);
    float v1 = shim_stats_value(p, s);
    int runs = 1;
    for (; runs <= SOLVE_MAX && fabsf(v1 - target) > HIT; runs++) {
        float dv = v1 - v0;
        float m2;
        if (fabsf(dv) < 1.0e-6f) {
            /* the stat did not move: a limit, or a start at the floor - step outright */
            m2 = target > v1 ? m1 * 2.0f : m1 * 0.5f;
        } else {
            m2 = m1 + (target - v1) * (m1 - m0) / dv;
        }
        m2 = clampf(m2, D8_LEAST, D8_MOST);
        if (m2 == m1) break;
        m0 = m1; v0 = v1; m1 = m2;
        write_d8(&g, s, m1, base, base[0]);
        evaluate_group(b, &g, STAT[s].flag);
        v1 = shim_stats_value(p, s);
    }
    if (got) *got = v1;
    return runs;
}

static int solve_luck(unsigned long b, unsigned long p, float target, float *got) {
    int runs = 0;
    float v = shim_stats_value(p, STAT_LUCK);
    for (; runs < 2 && fabsf(v - target) > HIT; runs++) {
        *(float *) (p + PLAYER_LUCK_ADD) += target - v;
        evaluate(b, p, STAT[STAT_LUCK].flag);
        v = shim_stats_value(p, STAT_LUCK);
    }
    if (got) *got = v;
    return runs;
}

/* the counter: one count's worth measured by a trial, then the nearest count */
static int solve_shot(unsigned long b, unsigned long p, float target, float *got) {
    int n = *(const int *) (p + PLAYER_SHOT_COUNT);
    float v0 = shim_stats_value(p, STAT_SHOT);
    *(int *) (p + PLAYER_SHOT_COUNT) = n + 1;
    evaluate(b, p, STAT[STAT_SHOT].flag);
    float slope = shim_stats_value(p, STAT_SHOT) - v0;
    int want = n;
    if (slope > 1.0e-4f) {
        want = n + (int) lroundf((target - v0) / slope);
        /* never below the game's floor by our own hand: it would only stick there */
        while (want < n && v0 + (float) (want - n) * slope < STAT[STAT_SHOT].least - HIT) want++;
    }
    *(int *) (p + PLAYER_SHOT_COUNT) = want;
    evaluate(b, p, STAT[STAT_SHOT].flag);
    if (got) *got = shim_stats_value(p, STAT_SHOT);
    return 2;
}

/*
 * Set stat s of player p to the target. Answers the value the game made of it
 * (the target, or the nearest the game allows), or NAN when nothing was done.
 */
float shim_stats_set(unsigned long p, int s, float target) {
    unsigned long b = shim_image_base();
    if (!b || !p || s < 0 || s >= STATS || !in_run(p) || !isfinite(target)) return NAN;
    target = clampf(target, STAT[s].least, STAT[s].most);
    float before = shim_stats_value(p, s);
    shim_spawner_mark_run();                     /* first: EvaluateItems unlocks inside */
    if (!kept_find(p, 1) && shim_report_active())
        report_line("stats", "the fields before the edit were not kept: RESET will not reach this player");
    if (s == STAT_SPEED) {                       /* the twin's multiplier moves too */
        unsigned long g[2];
        for (int i = 0, n = speed_group(p, g); i < n; i++) kept_find(g[i], 1);
    }
    below_rock_bottom(b, p, s, target, before);
    float got = before;
    int runs;
    if (STAT[s].d8) runs = solve_d8(b, p, s, target, &got);
    else if (s == STAT_LUCK) runs = solve_luck(b, p, target, &got);
    else runs = solve_shot(b, p, target, &got);
#if ISAAC_DIAG
    if (shim_report_active()) {
        char line[200];
        snprintf(line, sizeof(line), "stat edit: player %d type %d %s %.3f -> want %.3f, got %.3f "
                 "after %d evaluation(s)%s", player_index(p), shim_stats_player_type(p), STAT[s].name,
                 before, target, got, runs, STAT[s].d8 ? "" : " (no multiplier)");
        report_line("stats", line);
        if (STAT[s].d8) {
            snprintf(line, sizeof(line), "stat edit: D8 %s now %.4f", STAT[s].name,
                     *(const float *) (p + STAT[s].d8));
            report_line("stats", line);
        }
    }
#else
    (void) runs;
#endif
    return got;
}

/* shot speed by whole counts, the arrows' step: answers the new value */
float shim_stats_shot_step(unsigned long p, int dir) {
    unsigned long b = shim_image_base();
    if (!b || !p || !in_run(p) || !dir) return NAN;
    shim_spawner_mark_run();
    kept_find(p, 1);
    int n = *(const int *) (p + PLAYER_SHOT_COUNT);
    float before = shim_stats_value(p, STAT_SHOT);
    *(int *) (p + PLAYER_SHOT_COUNT) = n + (dir > 0 ? 1 : -1);
    evaluate(b, p, STAT[STAT_SHOT].flag);
    float now = shim_stats_value(p, STAT_SHOT);
    if (dir < 0 && now <= STAT[STAT_SHOT].least + HIT && before <= STAT[STAT_SHOT].least + HIT) {
        /* already at the floor: a count more down would only hide below it */
        *(int *) (p + PLAYER_SHOT_COUNT) = n;
        evaluate(b, p, STAT[STAT_SHOT].flag);
        now = shim_stats_value(p, STAT_SHOT);
    }
    return now;
}

/* every field back as before the first edit; answers 1 when there was something to put back */
int shim_stats_reset(unsigned long p) {
    unsigned long b = shim_image_base();
    kept_t *k = p && b && in_run(p) ? kept_find(p, 0) : NULL;
    if (!k) return 0;
    shim_spawner_mark_run();
    *(float *) (p + STAT[STAT_SPEED].d8) = k->d8[0];
    *(float *) (p + STAT[STAT_TEARS].d8) = k->d8[1];
    *(float *) (p + STAT[STAT_DAMAGE].d8) = k->d8[2];
    *(float *) (p + STAT[STAT_RANGE].d8) = k->d8[3];
    *(float *) (p + PLAYER_LUCK_ADD) = k->luck;
    *(int *) (p + PLAYER_SHOT_COUNT) = k->shot;
    unsigned all = 0;
    for (int s = 0; s < STATS; s++) all |= STAT[s].flag;
    evaluate(b, p, all);
    /* a speed edit moved the parent's or the twin's multiplier as well: theirs back too */
    unsigned long g[2];
    for (int i = 0, n = speed_group(p, g); i < n; i++) {
        if (g[i] == p) continue;
        kept_t *o = kept_find(g[i], 0);
        if (o) *(float *) (g[i] + STAT[STAT_SPEED].d8) = o->d8[0];
        evaluate(b, g[i], STAT[STAT_SPEED].flag);
    }
    if (shim_report_active()) report_line("stats", "reset: the fields as before the first edit");
    return 1;
}

/* ------------------------------------------------------------ the counters and the health */
/*
 * THE STATS TAB'S NEW ROWS (the owner, 07.10.2026, mock-ups v2): coins, bombs
 * and keys, and the health as the character can hold it - every change made
 * as a DELTA through the game's own adder, so the game's limits, its
 * per-character redirects (Bethany's soul charges, The Forgotten's Soul, the
 * Keeper's coins) and its own bookkeeping stay the game's
 * (other stuff/spawner/research/hearts.md; every address read in the image):
 *
 *   AddCoins 0x4f0a14 +0x16e0, AddBombs 0x4f0ca0 +0x16dc, AddKeys 0x4f0d20 +0x16d4
 *   GetHealthType 0x4eeae4 -> 0 red, 1 soul, 2 none (the Lost), 3 coin, 4 bone
 *   AddMaxHearts 0x4ee8a0(p, halves, ignoreKeeper)       max      +0x16b8 (halves)
 *   AddHearts 0x4f0338(p, halves, flag)                  red      +0x16bc (halves)
 *   AddSoulHearts 0x4ef2d0(p, halves)                    soul     +0x16c4 (halves, black inside)
 *   AddBlackHearts 0x4ef078(p, halves)                   black    +0x16c8 (a bit per whole soul heart)
 *   AddBoneHearts 0x4ef5c4(p, wholes)                    bone     +0x2450
 *   AddBrokenHearts 0x5059ac(p, wholes)                  broken   +0x2470
 *   AddEternalHearts 0x4f0590(p, n)                      eternal  +0x16c0
 *   AddGoldenHearts 0x552ab8(p, n)                       golden   +0x1dc4
 *   AddRottenHearts 0x554794(p, halves, flag)            rotten   +0x2474 (wholes, each one red heart)
 *   AddSoulCharge 0x4f08d0 / AddBloodCharge 0x4f04c4     +0x2478 / +0x247c (0..99)
 *
 * The flag 1 of AddHearts / AddRottenHearts is the game's "as is": no
 * T.Magdalene doubling, no "Have a heart" refusal - the value asked is the
 * value meant. A loss of soul or black hearts goes through AddSoulHearts with
 * a negative count, which takes the LAST halves whatever their colour, as a hit
 * does: the rows show what the game made of it.
 */
#define FN_HEALTH_TYPE     0x4eeae4
#define FN_ADD_MAX         0x4ee8a0
#define FN_ADD_RED         0x4f0338
#define FN_ADD_SOUL        0x4ef2d0
#define FN_ADD_BLACK       0x4ef078
#define FN_ADD_BONE        0x4ef5c4
#define FN_ADD_BROKEN      0x5059ac
#define FN_ADD_ETERNAL     0x4f0590
#define FN_ADD_GOLDEN      0x552ab8
#define FN_ADD_ROTTEN      0x554794
#define FN_ADD_SOUL_CHARGE 0x4f08d0
#define FN_ADD_BLOOD       0x4f04c4
#define FN_ADD_COINS       0x4f0a14
#define FN_ADD_BOMBS       0x4f0ca0
#define FN_ADD_KEYS        0x4f0d20
#define P_MAX              0x16b8
#define P_RED              0x16bc
#define P_ETERNAL          0x16c0
#define P_SOUL             0x16c4
#define P_BLACK_MASK       0x16c8
#define P_BONE             0x2450
#define P_OTHER_BODY       0x2460           /* The Forgotten <-> The Soul */
#define P_BROKEN           0x2470
#define P_ROTTEN           0x2474
#define P_SOUL_CHARGE      0x2478
#define P_BLOOD_CHARGE     0x247c
#define P_GOLDEN           0x1dc4
#define P_KEYS             0x16d4
#define P_BOMBS            0x16dc
#define P_COINS            0x16e0
#define TYPE_FORGOTTEN     16
#define TYPE_THE_SOUL      17
#define TYPE_BETHANY       18
#define TYPE_T_BETHANY     36
#define HT_RED  0
#define HT_SOUL 1
#define HT_NONE 2
#define HT_COIN 3
#define HT_BONE 4

/* the rows: the same numbers as SP_H_* of spawner_pickups.h (tools/spawner_pickups.py) */
enum { H_CONTAINERS, H_RED, H_SOUL, H_BLACK, H_BONE, H_BROKEN, H_ETERNAL, H_GOLDEN, H_ROTTEN,
       H_SOUL_CHARGE, H_BLOOD_CHARGE, H_COIN_CONTAINERS, H_COIN_HEARTS, H_COIN_BROKEN, H_ROWS };

int shim_health_type(unsigned long p) {
    unsigned long b = shim_image_base();
    return b && p ? ((int (*)(unsigned long)) (b + FN_HEALTH_TYPE))(p) : HT_NONE;
}

/* the other body of The Forgotten / The Soul, when it is there */
static unsigned long other_body(unsigned long p) {
    int t = shim_stats_player_type(p);
    if (t != TYPE_FORGOTTEN && t != TYPE_THE_SOUL) return 0;
    return *(const unsigned long *) (p + P_OTHER_BODY);
}

/*
 * The health rows this body shows, in order; other[i] is 1 when the row is the
 * other body's (The Soul's soul and black hearts under The Forgotten, and the
 * other way round). Only what the character can HOLD by the game's adders
 * (hearts.md, "Which character can hold what"); the Lost holds nothing.
 */
int shim_health_rows(unsigned long p, int *rows, int *other, int max) {
    int n = 0;
    if (!p) return 0;
    int type = shim_stats_player_type(p), ht = shim_health_type(p);
#define ROW(r, o) do { if (n < max) { rows[n] = (r); other[n] = (o); n++; } } while (0)
    if (type == TYPE_FORGOTTEN) {
        ROW(H_BONE, 0); ROW(H_RED, 0); ROW(H_BROKEN, 0); ROW(H_ETERNAL, 0); ROW(H_GOLDEN, 0); ROW(H_ROTTEN, 0);
        if (other_body(p)) { ROW(H_SOUL, 1); ROW(H_BLACK, 1); }
    } else if (type == TYPE_THE_SOUL) {
        ROW(H_SOUL, 0); ROW(H_BLACK, 0); ROW(H_BROKEN, 0); ROW(H_GOLDEN, 0);
        if (other_body(p)) { ROW(H_BONE, 1); ROW(H_RED, 1); ROW(H_ROTTEN, 1); }
    } else if (ht == HT_COIN) {
        ROW(H_COIN_CONTAINERS, 0); ROW(H_COIN_HEARTS, 0); ROW(H_GOLDEN, 0); ROW(H_COIN_BROKEN, 0);
    } else if (ht == HT_SOUL) {
        ROW(H_SOUL, 0); ROW(H_BLACK, 0); ROW(H_BONE, 0); ROW(H_BROKEN, 0); ROW(H_GOLDEN, 0);
        if (type == TYPE_T_BETHANY) ROW(H_BLOOD_CHARGE, 0);
    } else if (ht == HT_RED || ht == HT_BONE) {
        ROW(H_CONTAINERS, 0); ROW(H_RED, 0);
        if (type != TYPE_BETHANY) { ROW(H_SOUL, 0); ROW(H_BLACK, 0); }
        ROW(H_BONE, 0); ROW(H_BROKEN, 0); ROW(H_ETERNAL, 0); ROW(H_GOLDEN, 0); ROW(H_ROTTEN, 0);
        if (type == TYPE_BETHANY) ROW(H_SOUL_CHARGE, 0);
    }
#undef ROW
    return n;
}

/* whether a row counts halves (shown with a half) */
int shim_health_halves(int row) {
    return row == H_CONTAINERS || row == H_RED || row == H_SOUL || row == H_BLACK;
}

/* the black halves among the soul hearts: a mask bit per whole soul heart, the last one maybe half */
static int black_halves(unsigned long q) {
    int soul = *(const int *) (q + P_SOUL);
    unsigned mask = *(const unsigned *) (q + P_BLACK_MASK);
    int hearts = (soul + 1) / 2, n = 0;
    for (int i = 0; i < hearts && i < 32; i++)
        if (mask & (1u << i)) n += (i == hearts - 1 && (soul & 1)) ? 1 : 2;
    return n;
}

/* the row's value: halves for shim_health_halves rows, wholes otherwise */
int shim_health_get(unsigned long p, int row, int other) {
    unsigned long q = other ? other_body(p) : p;
    if (!q) return 0;
    switch (row) {
    case H_CONTAINERS:      return *(const int *) (q + P_MAX);
    case H_RED:             return *(const int *) (q + P_RED);
    case H_SOUL:            return *(const int *) (q + P_SOUL) - black_halves(q);
    case H_BLACK:           return black_halves(q);
    case H_BONE:            return *(const int *) (q + P_BONE);
    case H_BROKEN:
    case H_COIN_BROKEN:     return *(const int *) (q + P_BROKEN);
    case H_ETERNAL:         return *(const int *) (q + P_ETERNAL);
    case H_GOLDEN:          return *(const int *) (q + P_GOLDEN);
    case H_ROTTEN:          return *(const int *) (q + P_ROTTEN);
    case H_SOUL_CHARGE:     return *(const int *) (q + P_SOUL_CHARGE);
    case H_BLOOD_CHARGE:    return *(const int *) (q + P_BLOOD_CHARGE);
    case H_COIN_CONTAINERS: return *(const int *) (q + P_MAX) / 2;
    case H_COIN_HEARTS:     return *(const int *) (q + P_RED) / 2;
    default:                return 0;
    }
}

/*
 * Change a row by `delta` of its own units through the game's adder; answers
 * the value read back. The adders are called on the body shown: the game
 * itself sends The Forgotten's soul hearts to The Soul and The Soul's red and
 * bone hearts to the body (0x4ef2d0, 0x4f0338, 0x4ef5c4).
 */
int shim_health_add(unsigned long p, int row, int other, int delta) {
    unsigned long b = shim_image_base();
    if (!b || !p || !delta || !in_run(p)) return shim_health_get(p, row, other);
    if (other && !other_body(p)) return 0;
    shim_spawner_mark_run();
    switch (row) {
    case H_CONTAINERS:      ((void (*)(unsigned long, int, int)) (b + FN_ADD_MAX))(p, delta, 0); break;
    case H_COIN_CONTAINERS: ((void (*)(unsigned long, int, int)) (b + FN_ADD_MAX))(p, delta * 2, 0); break;
    case H_RED:             ((void (*)(unsigned long, int, int)) (b + FN_ADD_RED))(p, delta, 1); break;
    case H_COIN_HEARTS:     ((void (*)(unsigned long, int, int)) (b + FN_ADD_RED))(p, delta * 2, 1); break;
    case H_ROTTEN:          ((void (*)(unsigned long, int, int)) (b + FN_ADD_ROTTEN))(p, delta * 2, 1); break;
    case H_SOUL:            ((void (*)(unsigned long, int)) (b + FN_ADD_SOUL))(p, delta); break;
    case H_BLACK:           ((void (*)(unsigned long, int)) (b + (delta > 0 ? FN_ADD_BLACK : FN_ADD_SOUL)))(p, delta); break;
    case H_BONE:            ((void (*)(unsigned long, int)) (b + FN_ADD_BONE))(p, delta); break;
    case H_BROKEN:
    case H_COIN_BROKEN:     ((void (*)(unsigned long, int)) (b + FN_ADD_BROKEN))(p, delta); break;
    case H_ETERNAL:         ((void (*)(unsigned long, int)) (b + FN_ADD_ETERNAL))(p, delta); break;
    case H_GOLDEN:          ((void (*)(unsigned long, int)) (b + FN_ADD_GOLDEN))(p, delta); break;
    case H_SOUL_CHARGE:     ((void (*)(unsigned long, int)) (b + FN_ADD_SOUL_CHARGE))(p, delta); break;
    case H_BLOOD_CHARGE:    ((void (*)(unsigned long, int)) (b + FN_ADD_BLOOD))(p, delta); break;
    default:                break;
    }
    int now = shim_health_get(p, row, other);
#if ISAAC_DIAG
    if (shim_report_active()) {
        char line[160];
        snprintf(line, sizeof(line), "health edit: player %d type %d health type %d row %d%s delta %d -> %d",
                 player_index(p), shim_stats_player_type(p), shim_health_type(p), row, other ? " (other body)" : "",
                 delta, now);
        report_line("stats", line);
    }
#endif
    return now;
}

/* coins, bombs, keys */
int shim_counter_get(unsigned long p, int c) {
    static const unsigned AT[3] = { P_COINS, P_BOMBS, P_KEYS };
    return p && c >= 0 && c < 3 ? *(const int *) (p + AT[c]) : 0;
}

int shim_counter_add(unsigned long p, int c, int delta) {
    static const unsigned long FN[3] = { FN_ADD_COINS, FN_ADD_BOMBS, FN_ADD_KEYS };
    unsigned long b = shim_image_base();
    if (!b || !p || c < 0 || c > 2 || !delta || !in_run(p)) return shim_counter_get(p, c);
    shim_spawner_mark_run();
    ((void (*)(unsigned long, int)) (b + FN[c]))(p, delta);
    int now = shim_counter_get(p, c);
#if ISAAC_DIAG
    if (shim_report_active()) {
        char line[120];
        snprintf(line, sizeof(line), "counter edit: player %d counter %d delta %d -> %d", player_index(p), c, delta,
                 now);
        report_line("stats", line);
    }
#endif
    return now;
}

/*
 * CHARGE ACTIVE ITEMS (the owner, 07.10.2026): the primary, the Schoolbag's and
 * the pocket active - slots 0, 1, 2 of the four at +0x1964 (0x1c bytes each,
 * the item id first) - each by the game's FullCharge 0x504f64(p, slot, force 0),
 * which overcharges only with The Battery, as a pickup does (AddActiveCharge
 * 0x5113cc). Answers how many slots held an item.
 */
#define FN_FULL_CHARGE   0x504f64
#define PLAYER_ACTIVES   0x1964
#define ACTIVE_BYTES     0x1c

int shim_charge_actives(unsigned long p) {
    unsigned long b = shim_image_base();
    if (!b || !p || !in_run(p)) return 0;
    int n = 0;
    for (int slot = 0; slot < 3; slot++)
        if (*(const int *) (p + PLAYER_ACTIVES + (unsigned long) slot * ACTIVE_BYTES) > 0) n++;
    if (!n) return 0;
    shim_spawner_mark_run();
    for (int slot = 0; slot < 3; slot++) {
        unsigned long at = p + PLAYER_ACTIVES + (unsigned long) slot * ACTIVE_BYTES;
        if (*(const int *) at <= 0) continue;
        ((void (*)(unsigned long, int, int)) (b + FN_FULL_CHARGE))(p, slot, 0);
#if ISAAC_DIAG
        if (shim_report_active()) {
            char line[120];
            snprintf(line, sizeof(line), "charge: player %d slot %d item %d -> charge %d battery %d", player_index(p),
                     slot, *(const int *) at, *(const int *) (at + 4), *(const int *) (at + 8));
            report_line("stats", line);
        }
#endif
    }
    return n;
}
