/*
 * STATS+, WITHOUT LUA.
 *
 * "Stats+" (Aleksander Ciesielski, 2.1.3) writes a number of its own beside
 * the stats column: a provider per stat, chosen by a condition, in faint
 * colour, with its changes sliding out beside it. Switched in Tweaks
 * ("stats"); its Mod Config Menu page is the [statsplus] section of
 * port-touch.ini (modset.c), with the mod's own defaults (Config.lua):
 *
 *   speed, range   the D8 multiplier while the map button is held, else nothing
 *   tears          the D8 multiplier while the map is held, else the tear cap
 *   damage         the D8 multiplier while the map is held, else the damage
 *                  multiplier
 *   shot speed, luck   nothing
 *
 * WHAT IS READ, all measured in the image (18.09.2026):
 *
 *   the stats column  Game+0x10d9c8, drawn by 0x6efac4(stats, &place); a slot
 *                     per player, 0xd0 apart: +0x158 the player, +0x160 ten
 *                     entries of 0x14 (+0 the value shown, +4 the last change,
 *                     +0x10 the change's timer). The values are the column's
 *                     own (0x6f1180): speed player+0x194c, tears 30 /
 *                     (player+0x1834 + 1), damage +0x1844, range +0x1854 / 40,
 *                     shot speed +0x1838, luck +0x1950 - so the width of the
 *                     number beside which ours stands is the column's own text,
 *                     "%.2f" of the entry (0x6efd68), in its own font
 *                     (Manager+0xf68b8), at place + (16, 1), a slot 4 right
 *                     and 7 down, a row 12 down (14 with two slots), less 1
 *                     (5 with two), plus the screen shake (Game+0x1af360)
 *   D8                the game keeps its own roll: player+0x1db4 damage,
 *                     +0x1db8 speed, +0x1dbc range, +0x1dc0 tears - set by the
 *                     use of item 406 (0x583994) and multiplied in by
 *                     EvaluateItems (0x4f38cc, 0x4f5354, 0x4f48f8, 0x4f2258).
 *                     The mod could only watch the stat before and after a
 *                     use; the port reads the roll itself.
 *   the map button    0x671c5c(0, 13, player+0x19ec), as items.c reads it; a
 *                     finger on the map (items.c's list) counts too
 *   the drop button   action 11, the same way
 *   items             HasCollectible 0x4eeb74(player, id, 0); the temporary
 *                     effects at player+0x18d8, HasCollectibleEffect 0x6fdc90
 *                     (types passive, active, familiar); GetTrinketMultiplier
 *                     0x506238(player, id); the owned counts at [player+0x1ab8]
 *   time              Game+0x1af34c, the update frame count: it stands still
 *                     in the pause, as the mod's clock does
 *
 *   the tear cap     the owned counts [counting player+0x1ab8] (the player
 *                     GetCollectibleNum 0x4ff70c reads: while [p+0x3c] == 1,
 *                     p = 0x4ee510(p)), the pending stat flags player+0x1958,
 *                     EvaluateItems 0x4f0ee4(player). MaxFireDelay is
 *                     player+0x1834, the number the column turns into tears.
 *                     No longer AddCollectible / RemoveCollectible (01.10.2026,
 *                     plan item A5): see "the tear cap" below.
 */
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <android/log.h>
#include "diag.h"
#include "modset.h"

#if ISAAC_DIAG
#define LOG(...) __android_log_print(ANDROID_LOG_INFO, "isaac", __VA_ARGS__)
#else
#define LOG(...) do { } while (0)
#endif

unsigned long shim_image_base(void);
int shim_report_active(void);
void report_line(const char *, const char *);
int shim_tweaks_mod(int i);
int shim_eid_hud_showing(void);
int shim_eid_seed_on(int id);
int shim_items_open(void);
int shim_sticks_in_game(void);

#define MOD_STATS          5                /* shim_tweaks_mod */

#define MANAGER_PTR        0xac2428
#define GAME_PTR           0xac3b90
#define GAME_STATS         0x10d9c8
#define GAME_FRAME         0x1af34c
#define GAME_SHAKE         0x1af360
#define MGR_FOUND_HUD      0x55f02
#define FONT_LUA_MINI      0xf68b8

#define STATS_SLOT         0xd0
#define STATS_PLAYER       0x158
#define STATS_ENTRIES      0x160
#define ENTRY_BYTES        0x14
#define STATS_SLOT_TWO     0x228

#define PLAYER_TYPE        0x1738
#define PLAYER_CONTROLLER  0x19ec
#define PLAYER_EFFECTS     0x18d8
#define PLAYER_OWNED       0x1ab8
#define PLAYER_D8_DAMAGE   0x1db4
#define PLAYER_D8_SPEED    0x1db8
#define PLAYER_D8_RANGE    0x1dbc
#define PLAYER_D8_TEARS    0x1dc0

#define FN_HAS_COLLECTIBLE 0x4eeb74
#define FN_HAS_EFFECT      0x6fdc90
#define FN_TRINKET_MULT    0x506238
#define FN_ACTION_HELD     0x671c5c
#define FN_KCOLOR          0x8a34f4
#define FN_DRAW_SCALED     0x8be42c
#define FN_TEXT_WIDTH      0x8bdec8

#define ACTION_DROP        11
#define ACTION_MAP         13
#define SEED_NO_HUD        10
#define TYPE_JACOB         19
#define TYPE_ESAU          20

#define SLOTS              2
#define ROWS               6                /* speed, tears, damage, range, shot speed, luck */
enum { ROW_SPEED, ROW_TEARS, ROW_DAMAGE, ROW_RANGE, ROW_SHOTSPEED, ROW_LUCK };
enum { P_D8, P_DAMAGE, P_TEARCAP, P_COUNT };  /* the providers that keep a value */

/* a value's kind: every provider but the tear cap has only numbers */
enum { CAP_UNKNOWN, CAP_NUMBER, CAP_MAX };

/* the mod's timings, in update frames (30 a second, entities/Time.lua) */
#define ACCUMULATE        120.0f            /* changes within this add up (MetricChange) */
#define GAME_START_AT       7.5f
#define GAME_START_FOR      4.5f            /* 150 ms */
#define SWITCH_FOR          4.5f            /* a condition's colour change, 150 ms */
#define DUCK_AFTER        120.0f            /* hidden for 4 s after the stat itself changed */
#define START_SETTLES      45.0f            /* 1.5 s: the run's own start is no change */
#define DUCK_FOR           15.0f            /* then half a second back */
#define CHANGE_HOLD_OFF   150.0f            /* a change waits 5 s after the stat's own */
#define CHANGE_IN          15.0f
#define CHANGE_HOLD       120.0f
#define CHANGE_OUT         22.5f
#define CHANGE_SLIDE       12.0f
#define CHANGE_SLIDE_PX     8.0f
#define CHANGE_ALPHA        0.5f
#define LOOK_BACK         187.5f            /* 4 s + 1 s + 750 ms: the change read then */

#define NEVER            -1.0e9f

/* ------------------------------------------------------------ the numbers */
/* the mod's toFixed: truncated, not rounded, and a hair against 0.29999 */
static void fixed2(char *out, size_t n, const char *prefix, float v) {
    double t = v > 0 ? floor((double) v * 100.0 + 1e-8) / 100.0 : ceil((double) v * 100.0 + 1e-8) / 100.0;
    if (t == 0.0) t = 0.0;                  /* no "-0.00" */
    snprintf(out, n, "%s%.2f", prefix, t);
}

typedef struct {
    int has;                                /* a value has been set at all */
    int kind, base_kind;                    /* CAP_*: the tear cap can also be MAX or unknown */
    float value, base;
    char text[16];
    float change_start;                     /* when the change now building began */
    int change_has;
    float change_value;
    int change_positive;
    char change_text[16];
    float change_last;                      /* when the change last moved */
} metric_t;

typedef struct {
    int active, has_active;
    float last_change;                      /* when the condition last flipped */
    int drop_armed;
} condition_t;

typedef struct {
    metric_t m[P_COUNT];
    condition_t cond;
    float stat_value;
    int stat_known;
    float stat_changed;                     /* when the column's own number last moved */
} row_t;

static row_t g_row[SLOTS][ROWS];
static unsigned long g_player[SLOTS];
static unsigned long g_game;
static float g_mounted;                     /* the frame the rows were last reset */

static int accumulating(const metric_t *m, float now) {
    return m->change_has && ACCUMULATE > now - m->change_last;
}

static void format_value(int provider, int kind, float v, char *out, size_t n) {
    if (provider == P_TEARCAP) {
        if (kind == CAP_MAX) { snprintf(out, n, "MAX"); return; }
        if (kind != CAP_NUMBER) { out[0] = 0; return; }   /* unknown: the mod shows nothing */
        fixed2(out, n, "/", 30.0f / (v + 1.0f));
        return;
    }
    fixed2(out, n, "x", v);
}

/* the change between two values, as the provider words it; 0 when there is none */
static int format_change(int provider, int from_kind, float from, int to_kind, float to,
                         char *out, size_t n, int *positive) {
    if (provider == P_TEARCAP) {
        /* the mod words a change only between two numbers */
        if (from_kind != CAP_NUMBER || to_kind != CAP_NUMBER) return 0;
        float d = 30.0f / (to + 1.0f) - 30.0f / (from + 1.0f);
        if (d == 0.0f) return 0;
        *positive = d > 0.0f;
        fixed2(out, n, d > 0.0f ? "+" : "", d);
        return 1;
    }
    if (from == 0.0f || to == from) return 0;
    float r = to / from;
    *positive = r > 1.0f;
    if (r == 1.0f) return 0;
    fixed2(out, n, "x", r);
    return 1;
}

/* MetricValue.setValue: the text moves only when its words do */
static void metric_set(metric_t *m, int provider, int kind, float v, float now, int silent) {
    char text[16];
    format_value(provider, kind, v, text, sizeof(text));
    if (m->has && !strcmp(text, m->text)) return;
    snprintf(m->text, sizeof(m->text), "%s", text);
    int building = accumulating(m, now);
    if (!building) {
        m->base = m->has ? m->value : v;
        m->base_kind = m->has ? m->kind : kind;
        m->change_start = now;
    }
    if (!silent && m->has) {
        int positive = 0;
        if (format_change(provider, m->base_kind, m->base, kind, v,
                          m->change_text, sizeof(m->change_text), &positive)) {
            m->change_has = 1;
            m->change_value = v;
            m->change_positive = positive;
            m->change_last = now;
        } else {
            m->change_has = 0;
            m->change_last = now - ACCUMULATE;
        }
    }
    m->value = v;
    m->kind = kind;
    m->has = 1;
    if (!building) { m->base = v; m->base_kind = kind; }
}

/* ------------------------------------------------------------ the game */
static unsigned long b_now(void) { return shim_image_base(); }

static int has_item(unsigned long b, unsigned long p, int id) {
    return ((int (*)(unsigned long, int, int)) (b + FN_HAS_COLLECTIBLE))(p, id, 0) & 1;
}

static int has_effect(unsigned long b, unsigned long p, int id) {
    return ((int (*)(unsigned long, int)) (b + FN_HAS_EFFECT))(p + PLAYER_EFFECTS, id) & 1;
}

static int item_count(unsigned long p, int id) {
    const int *owned = *(const int *const *) (p + PLAYER_OWNED);
    return owned ? owned[id] : 0;
}

static int has_trinket(unsigned long b, unsigned long p, int id) {
    return ((int (*)(unsigned long, int)) (b + FN_TRINKET_MULT))(p, id) > 0;
}

/* data/characterMultipliers.lua */
static float character_multiplier(unsigned long b, unsigned long p) {
    switch (*(const int *) (p + PLAYER_TYPE)) {
    case 2: return 1.2f;                    /* Cain */
    case 3: return 1.35f;                   /* Judas */
    case 4: return 1.05f;                   /* ??? */
    case 5: return has_effect(b, p, 122) ? 1.0f : 0.75f;   /* Eve, Whore of Babylon on */
    case 7: return 1.5f;                    /* Azazel */
    case 11: return 1.4f;                   /* Lazarus risen */
    case 12: return 2.0f;                   /* Dark Judas */
    case 14: return 1.2f;                   /* Keeper */
    case 16: return 1.5f;                   /* the Forgotten */
    case 22: return 0.75f;                  /* tainted Magdalene */
    case 23: return 1.2f;                   /* tainted Cain */
    case 26: return 1.2f;                   /* tainted Eve */
    case 28: return 1.5f;                   /* tainted Azazel */
    case 31: return 1.3f;                   /* tainted Lost */
    case 35: return 1.5f;                   /* tainted Forgotten */
    case 38: return 1.5f;                   /* tainted Lazarus, risen */
    }
    return 1.0f;
}

/* data/collectibleMultipliers.lua, in its order: an item held or in effect */
static float item_multiplier(unsigned long b, unsigned long p, int id) {
    switch (id) {
    case 625: return has_effect(b, p, 625) ? 4.0f : 1.0f;                        /* Mega Mush */
    case 4:   return 1.5f;                                                       /* Cricket's Head */
    case 12:  return has_item(b, p, 4) ? 1.0f : 1.5f;                            /* Magic Mushroom */
    case 7:                                                                      /* Blood of the Martyr */
        if (has_item(b, p, 4) || has_effect(b, p, 12)) return 1.0f;
        return has_effect(b, p, 34) ? 1.5f : 1.0f;
    case 169: return 2.0f;                                                       /* Polyphemus */
    case 182: return 2.3f;                                                       /* Sacred Heart */
    case 310: return 2.0f;                                                       /* Eve's Mascara */
    case 561: return 0.33f;                                                      /* Almond Milk */
    case 330: return has_item(b, p, 561) ? 1.0f : 0.2f;                          /* Soy Milk */
    case 415: return has_effect(b, p, 415) ? 2.0f : 1.0f;                        /* Crown of Light */
    case 573: return 1.2f;                                                       /* Immaculate Heart */
    case 120: return 0.9f;                                                       /* Odd Mushroom (thin) */
    case 245: return 0.8f;                                                       /* 20/20 */
    case 118:                                                                    /* Brimstone */
        if (has_item(b, p, 531)) return 1.0f;                                    /* Haemolacria */
        if (item_count(p, 118) == 1) return has_item(b, p, 68) ? 1.5f : 1.0f;   /* with Technology */
        return (has_item(b, p, 68) && has_item(b, p, 395)) ? 1.8f : 1.2f;       /* Tech X too */
    }
    return 1.0f;
}

static const int MULTIPLIED[] = { 625, 4, 12, 7, 169, 182, 310, 561, 330, 415, 573, 120, 245, 118 };

static float damage_multiplier(unsigned long b, unsigned long p) {
    float m = character_multiplier(b, p);
    for (unsigned i = 0; i < sizeof(MULTIPLIED) / sizeof(MULTIPLIED[0]); i++) {
        int id = MULTIPLIED[i];
        if (has_item(b, p, id) || has_effect(b, p, id)) m *= item_multiplier(b, p, id);
    }
    if (has_trinket(b, p, 92)) m *= 1.2f;                                        /* Cracked Crown */
    if (shim_modset_int(MS_STATS_TRACK_D8)) m *= *(const float *) (p + PLAYER_D8_DAMAGE);
    return m;
}

static float d8_of(unsigned long p, int row) {
    switch (row) {
    case ROW_SPEED: return *(const float *) (p + PLAYER_D8_SPEED);
    case ROW_TEARS: return *(const float *) (p + PLAYER_D8_TEARS);
    case ROW_DAMAGE: return *(const float *) (p + PLAYER_D8_DAMAGE);
    case ROW_RANGE: return *(const float *) (p + PLAYER_D8_RANGE);
    }
    return 1.0f;
}

static int action_held(unsigned long b, unsigned long p, int action) {
    return ((int (*)(unsigned long, int, int)) (b + FN_ACTION_HELD))(
        0, action, *(const int *) (p + PLAYER_CONTROLLER)) & 1;
}

/* the provider a loadout choice names, if this stat can show it */
static int provider_of(int show, int row) {
    if (show == MS_SHOW_D8) return row <= ROW_RANGE ? P_D8 : -1;
    if (show == MS_SHOW_DAMAGE) return row == ROW_DAMAGE ? P_DAMAGE : -1;
    if (show == MS_SHOW_TEARCAP) return row == ROW_TEARS ? P_TEARCAP : -1;
    return -1;
}

static int provider_colour(int provider) {
    int id = provider == P_D8 ? MS_STATS_D8_COLOUR : provider == P_DAMAGE ? MS_STATS_DAMAGE_COLOUR
                                                                          : MS_STATS_TEARCAP_COLOUR;
    return shim_modset_int(id);
}

/* Renderer.lua's colours; "none" is white for the first player, pink for the second */
static void rgb_of(int colour, int slot, float *c) {
    static const float TABLE[8][3] = {
        { 1, 1, 1 }, { 1, 1, 1 }, { 1, .568627f, .568627f }, { .568627f, 1, .568627f },
        { .568627f, .819608f, 1 }, { 1, .768627f, .568627f }, { 1, .568627f, .819608f },
        { .568627f, 1, 1 },
    };
    if (colour < 0 || colour > 7) colour = 0;
    c[0] = TABLE[colour][0]; c[1] = TABLE[colour][1]; c[2] = TABLE[colour][2];
    if (colour == MS_COLOUR_NONE && slot > 0) { c[1] = 0.75f; c[2] = 0.75f; }
}

/* ------------------------------------------------------------ the update */
static void reset_rows(float now) {
    memset(g_row, 0, sizeof(g_row));
    for (int s = 0; s < SLOTS; s++)
        for (int r = 0; r < ROWS; r++) {
            g_row[s][r].cond.last_change = NEVER;
            g_row[s][r].stat_changed = NEVER;
            for (int k = 0; k < P_COUNT; k++) {
                g_row[s][r].m[k].change_start = NEVER;
                g_row[s][r].m[k].change_last = NEVER;
            }
        }
    g_mounted = now;
}

/* ------------------------------------------------------------ the tear cap */
/*
 * The mod's own question (createTearCapProvider.lua): take the first tears
 * item the player does NOT hold, count it in again and again until the fire
 * delay stops moving. One step that changed nothing means the cap is already
 * reached ("MAX"); otherwise the delay after the last step IS the cap. Rock
 * Bottom keeps the stats at their best and makes the answer meaningless, so
 * the mod shows nothing at all then - as it does when the player holds every
 * test item.
 *
 * NOT THROUGH THE PICKUP (01.10.2026, plan item A5). The mod adds the item for
 * real and takes it away; here that was the full AddCollectible, up to two
 * hundred times, and a pickup is far more than a stat: it ticks the collection,
 * counts toward "U broke it!", sets and transformations, writes the item
 * history, and EvaluateItems unlocks inside. All of that landed in the save and
 * in the run for items nobody took. So the stat is asked the way the game
 * computes it and nothing else: EvaluateItems reads every count through
 * GetCollectibleNum, so the count itself is raised -
 *
 *   1. the counting player: GetCollectibleNum's own walk, while [p+0x3c] == 1
 *      p = 0x4ee510(p);
 *   2. read-only raised by its byte, data+1, and put back after: SetReadOnly
 *      does nothing else but writes a line into the game's log every time,
 *      which the unlock watch reads (EvaluateItems has six TryUnlocks inside);
 *   3. only with no stat pending ([p+0x1958] == 0): then the fire delay now is
 *      the settled one, and coming back to it exactly proves the probe clean;
 *   4. owned[id] + 1, flags = 2 (the fire delay's bit, 0x4f1480 tbnz w8, #1),
 *      EvaluateItems, read player+0x1834 - until it stops moving;
 *   5. owned[id] back, flags = 2, EvaluateItems once more, flags back.
 *
 * It costs a run of EvaluateItems per step, so the mod recomputes at most once
 * every 3 seconds (90 update frames) and only after the stat cache moved. We
 * cannot hear the cache callback, so the same thing is read from the player:
 * the fire delay itself, whether Rock Bottom appeared, and whether the test
 * item is still free. Anything that moves the cap moves one of the three.
 */
#define PLAYER_FIRE_DELAY     0x1834        /* MaxFireDelay; the column shows 30 / (it + 1) */
#define PLAYER_KIND           0x3c          /* 1: a sub-player, counted by its owner */
#define PLAYER_CACHE_FLAGS    0x1958        /* stats waiting for EvaluateItems; it clears them */
#define CACHE_FIREDELAY       2
#define FN_EVALUATE_ITEMS     0x4f0ee4
#define FN_COUNTING_PLAYER    0x4ee510
#define MGR_PERSISTENT        0x204d0
#define PGD_DIRTY             0
#define PGD_READ_ONLY         1
#define PGD_ACHIEVEMENTS      0x28          /* 638 bytes */
#define PGD_COUNTERS          0x2a8         /* 496 ints */
#define PGD_COLLECTION        0xa68
#define ITEM_ROCK_BOTTOM      562
#define CAP_EPSILON           0.001f
#define CAP_EVERY             90.0f         /* 3000 ms at 30 update frames a second */
#define CAP_ADDS_MAX          200           /* the loop is the game's; keep it bounded */

/* the mod's TEST_COLLECTIBLES, in its order */
static const int CAP_ITEMS[] = { 1, 6, 32, 183, 255, 564, 120 };

typedef struct {
    int known;                              /* a cap has been worked out at least once */
    int kind;                               /* CAP_UNKNOWN, CAP_NUMBER, CAP_MAX */
    float delay;                            /* the capped fire delay, when it is a number */
    float last_run;                         /* the frame it was last worked out */
    float delay_seen;                       /* what moves the cap, as last seen */
    int rock_bottom_seen, item_seen;
} cap_t;

static cap_t g_cap[SLOTS];

static float fire_delay_of(unsigned long p) { return *(const float *) (p + PLAYER_FIRE_DELAY); }

/* the player whose owned counts GetCollectibleNum reads (0x4ff734..0x4ff748) */
static unsigned long counting_player(unsigned long b, unsigned long p) {
    for (int hops = 0; hops < 4 && *(const int *) (p + PLAYER_KIND) == 1; hops++) {
        unsigned long q = ((unsigned long (*)(unsigned long)) (b + FN_COUNTING_PLAYER))(p);
        if (!q || q == p) break;
        p = q;
    }
    return p;
}

static void evaluate_fire_delay(unsigned long b, unsigned long p) {
    *(int *) (p + PLAYER_CACHE_FLAGS) = CACHE_FIREDELAY;
    ((void (*)(unsigned long)) (b + FN_EVALUATE_ITEMS))(p);
}

#if ISAAC_DIAG
/* what the probe could have touched, to say it did not (report builds) */
typedef struct {
    int owned, flags, dirty, read_only, seen;
    float delay;
    unsigned sum_achievements, sum_counters;
} probe_trace_t;

static unsigned sum_of(const unsigned char *p, int n) {
    unsigned s = 0;
    for (int i = 0; i < n; i++) s = s * 31u + p[i];
    return s;
}

static void traces(unsigned long p, const int *owned, int id, const unsigned char *data, probe_trace_t *t) {
    t->owned = owned[id];
    t->flags = *(const int *) (p + PLAYER_CACHE_FLAGS);
    t->delay = fire_delay_of(p);
    t->dirty = data[PGD_DIRTY];
    t->read_only = data[PGD_READ_ONLY];
    t->seen = data[PGD_COLLECTION + id];
    t->sum_achievements = sum_of(data + PGD_ACHIEVEMENTS, 638);
    t->sum_counters = sum_of(data + PGD_COUNTERS, 496 * 4);
}

static void probe_told(int id, int steps, const probe_trace_t *a, const probe_trace_t *z) {
    static int said;
    if (said >= 20 || !shim_report_active()) return;
    said++;
    char line[240];
    int n = 0;
    if (a->owned != z->owned) n += snprintf(line + n, sizeof(line) - (size_t) n, " owned %d->%d", a->owned, z->owned);
    if (a->flags != z->flags) n += snprintf(line + n, sizeof(line) - (size_t) n, " flags %x->%x", a->flags, z->flags);
    if (a->delay != z->delay)
        n += snprintf(line + n, sizeof(line) - (size_t) n, " delay %.4f->%.4f", (double) a->delay, (double) z->delay);
    if (a->dirty != z->dirty) n += snprintf(line + n, sizeof(line) - (size_t) n, " dirty %d->%d", a->dirty, z->dirty);
    if (a->read_only != z->read_only) n += snprintf(line + n, sizeof(line) - (size_t) n, " read-only");
    if (a->seen != z->seen) n += snprintf(line + n, sizeof(line) - (size_t) n, " collection");
    if (a->sum_achievements != z->sum_achievements) n += snprintf(line + n, sizeof(line) - (size_t) n, " achievements");
    if (a->sum_counters != z->sum_counters) n += snprintf(line + n, sizeof(line) - (size_t) n, " counters");
    char out[300];
    if (n) snprintf(out, sizeof(out), "probe left a trace (item %d, %d steps):%s", id, steps, line);
    else snprintf(out, sizeof(out), "probe clean (item %d, %d steps)", id, steps);
    report_line("stats+", out);
}
#endif

/* the first test item the player does not hold, or 0 when there is none to use */
static int cap_item(unsigned long b, unsigned long p) {
    if (has_item(b, p, ITEM_ROCK_BOTTOM)) return 0;
    for (unsigned i = 0; i < sizeof(CAP_ITEMS) / sizeof(CAP_ITEMS[0]); i++)
        if (!has_item(b, p, CAP_ITEMS[i])) return CAP_ITEMS[i];
    return 0;
}

/* -1: not now (a stat is pending), try again next frame */
static int compute_cap(unsigned long b, unsigned long p, float *delay) {
    int id = cap_item(b, p);
    if (!id) return CAP_UNKNOWN;
    unsigned long manager = *(const unsigned long *) (b + MANAGER_PTR);
    unsigned long q = counting_player(b, p);
    int *owned = *(int *const *) (q + PLAYER_OWNED);
    if (!manager || !owned) return CAP_UNKNOWN;
    int flags = *(const int *) (p + PLAYER_CACHE_FLAGS);
    if (flags) return -1;
    unsigned char *data = (unsigned char *) (manager + MGR_PERSISTENT);
#if ISAAC_DIAG
    probe_trace_t before, after;
    traces(p, owned, id, data, &before);
#endif
    unsigned char read_only = data[PGD_READ_ONLY];
    int held = owned[id];
    float current = fire_delay_of(p), previous;
    data[PGD_READ_ONLY] = 1;
    int count = 0;
    do {
        previous = current;
        owned[id]++;
        evaluate_fire_delay(b, p);
        current = fire_delay_of(p);
        count++;
    } while (fabsf(previous - current) > CAP_EPSILON && count < CAP_ADDS_MAX);
    owned[id] = held;
    evaluate_fire_delay(b, p);
    *(int *) (p + PLAYER_CACHE_FLAGS) = flags;
    data[PGD_READ_ONLY] = read_only;
#if ISAAC_DIAG
    traces(p, owned, id, data, &after);
    probe_told(id, count, &before, &after);
#endif
    if (count >= CAP_ADDS_MAX) {
        LOG("stats+: the tear cap did not settle in %d adds of item %d", CAP_ADDS_MAX, id);
        return CAP_UNKNOWN;
    }
    *delay = current;
    return count == 1 ? CAP_MAX : CAP_NUMBER;
}

/* does any part of the tears row ask for the cap at all */
static int cap_wanted(void) {
    int base = MS_STATS_LOADOUT + ROW_TEARS * 3;
    return shim_modset_int(base + 1) == MS_SHOW_TEARCAP || shim_modset_int(base + 2) == MS_SHOW_TEARCAP;
}

/*
 * Between frames (mods.c), where the dance already changes costumes - the
 * player is whole there, and this is the port's stand-in for POST_UPDATE.
 */
void shim_statsplus_between_frames(void) {
    unsigned long b = b_now();
    if (!b || !shim_tweaks_mod(MOD_STATS) || !cap_wanted()) return;
    unsigned long game = *(const unsigned long *) (b + GAME_PTR);
    if (!game || !shim_sticks_in_game()) {
        memset(g_cap, 0, sizeof(g_cap));
        return;
    }
    float now = (float) *(const int *) (game + GAME_FRAME);
    unsigned long stats = game + GAME_STATS;
    for (int s = 0; s < SLOTS; s++) {
        unsigned long p = *(const unsigned long *) (stats + STATS_PLAYER + s * STATS_SLOT);
        cap_t *c = &g_cap[s];
        if (!p) { memset(c, 0, sizeof(*c)); continue; }
        float delay = fire_delay_of(p);
        int rock = has_item(b, p, ITEM_ROCK_BOTTOM);
        int item = cap_item(b, p);
        if (c->known && delay == c->delay_seen && rock == c->rock_bottom_seen && item == c->item_seen)
            continue;                       /* nothing that bears on the cap has moved */
        if (c->known && now - c->last_run < CAP_EVERY) continue;
        float capped = c->delay;
        int kind = compute_cap(b, p, &capped);
        if (kind < 0) continue;             /* a stat is pending: the game settles it first */
        c->kind = kind;
        c->delay = capped;
        c->known = 1;
        c->last_run = now;
        c->delay_seen = fire_delay_of(p);   /* read again: the adds are undone by now */
        c->rock_bottom_seen = rock;
        c->item_seen = item;
        LOG("stats+: the tear cap of player %d is %s %.3f (item %d)", s,
            c->kind == CAP_MAX ? "MAX" : c->kind == CAP_NUMBER ? "delay" : "unknown", c->delay, item);
        if (shim_report_active()) {
            static int said;
            if (said < 20) {
                said++;
                char line[140];
                snprintf(line, sizeof(line), "tear cap of player %d: %s %.3f, probe item %d, Rock Bottom %s, frame %.0f",
                         s, c->kind == CAP_MAX ? "MAX" : c->kind == CAP_NUMBER ? "delay" : "unknown",
                         (double) c->delay, item, rock ? "held" : "no", (double) now);
                report_line("stats+", line);
            }
        }
    }
}

static void update_row(unsigned long b, int slot, int row, unsigned long p, float stat, float now) {
    row_t *R = &g_row[slot][row];
    int silent = !R->stat_known;           /* the first reading after a reset makes no change */

    /*
     * The run's own start is no change (the owner's eighth round, 24.09.2026:
     * "the tear cap first shows ten seconds in"). The game settles a run's stats
     * over its first frames - starting items, the character's own numbers - and
     * every one of those moves ducked the column for four seconds, one after
     * another. The mod's column mounts after the start; ours takes what the
     * first second and a half brings as where the stat begins.
     */
    if (R->stat_known && stat != R->stat_value) {
        if (now - g_mounted < START_SETTLES) {
            R->stat_value = stat;
        } else {
            R->stat_changed = now;
            if (shim_report_active()) {
                static int said;
                if (said < 12) {
                    said++;
                    char line[120];
                    snprintf(line, sizeof(line), "row %d changed %.3f -> %.3f at frame %.0f (%.0f after start): ducked 4 s",
                             row, (double) R->stat_value, (double) stat, (double) now, (double) (now - g_mounted));
                    report_line("stats+", line);
                }
            }
        }
    }
    R->stat_value = stat;
    R->stat_known = 1;

    int base = MS_STATS_LOADOUT + row * 3;
    int when = shim_modset_int(base);
    int active;
    if (when == MS_WHEN_MAP) {
        active = action_held(b, p, ACTION_MAP) || (slot == 0 && shim_items_open());
    } else if (when == MS_WHEN_DROP) {
        int held = action_held(b, p, ACTION_DROP);
        active = R->cond.active;
        if (held && R->cond.drop_armed) { active = !active; R->cond.drop_armed = 0; }
        if (!held) R->cond.drop_armed = 1;
    } else {
        active = 1;
    }
    if (!R->cond.has_active || active != R->cond.active) {
        R->cond.last_change = now;
        R->cond.active = active;
        R->cond.has_active = 1;
    }

    for (int part = 1; part <= 2; part++) {
        int provider = provider_of(shim_modset_int(base + part), row);
        if (provider == P_D8)
            metric_set(&R->m[P_D8], P_D8, CAP_NUMBER, d8_of(p, row), now, silent);
        else if (provider == P_DAMAGE)
            metric_set(&R->m[P_DAMAGE], P_DAMAGE, CAP_NUMBER, damage_multiplier(b, p), now, silent);
        else if (provider == P_TEARCAP && g_cap[slot].known)
            metric_set(&R->m[P_TEARCAP], P_TEARCAP, g_cap[slot].kind, g_cap[slot].delay, now, silent);
    }
}

/* ------------------------------------------------------------ the drawing */
static float clamp01(float v) { return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); }

static float progress(float now, float start, float length) {
    if (start <= NEVER * 0.5f) return 1.0f;
    return clamp01((now - start) / length);
}

static void brackets(const char *text, char *out, size_t n) {
    static const char *const OPEN[] = { "", "[", "(", "{", "<" };
    static const char *const SHUT[] = { "", "]", ")", "}", ">" };
    int k = shim_modset_int(MS_STATS_BRACKETS);
    if (k < 0 || k > 4) k = 0;
    snprintf(out, n, "%s%s%s", OPEN[k], text, SHUT[k]);
}

static void write_text(unsigned long b, unsigned long font, const char *text, float x, float y,
                       const float *rgba) {
    unsigned char colour[32];
    memset(colour, 0, sizeof(colour));
    ((void (*)(void *, float, float, float, float)) (b + FN_KCOLOR))(colour, rgba[0], rgba[1], rgba[2], rgba[3]);
    ((void (*)(unsigned long, const char *, void *, int, int, float, float, float, float))
        (b + FN_DRAW_SCALED))(font, text, colour, 0, 0, x, y, 1.0f, 1.0f);
}

static float width_of(unsigned long b, unsigned long font, const char *text) {
    return (float) ((int (*)(unsigned long, const char *)) (b + FN_TEXT_WIDTH))(font, text);
}

/* lerp two colours by how far the condition's switch has come (getInterpolatedColor) */
static void mix(const float *from, const float *to, float t, float *out) {
    for (int k = 0; k < 4; k++) out[k] = from[k] + (to[k] - from[k]) * t;
}

/* the column's place, as the last draw of it handed it over (mods.c) */
static float g_place_x, g_place_y, g_step, g_lift;
static unsigned g_place_seen, g_draws;

void shim_statsplus_seen(unsigned long stats, unsigned long place) {
    const float *p = (const float *) place;
    int two = *(const unsigned long *) (stats + STATS_SLOT_TWO) != 0;
    g_place_x = p[0];
    g_place_y = p[1];
    g_step = two ? 14.0f : 12.0f;
    g_lift = two ? -5.0f : -1.0f;
    g_place_seen = g_draws;
}

static void draw_row(unsigned long b, unsigned long font, int slot, int row, float x, float y,
                     const char *stat_text, float now) {
    row_t *R = &g_row[slot][row];
    int base = MS_STATS_LOADOUT + row * 3;
    int then_p = provider_of(shim_modset_int(base + 1), row);
    int else_p = provider_of(shim_modset_int(base + 2), row);
    if (shim_modset_int(base) == MS_WHEN_ALWAYS) else_p = -1;
    int active_p = R->cond.active ? then_p : else_p;
    int inactive_p = R->cond.active ? else_p : then_p;
    const metric_t *A = active_p >= 0 ? &R->m[active_p] : NULL;
    const metric_t *I = inactive_p >= 0 ? &R->m[inactive_p] : NULL;
    int a_has = A && A->has && A->text[0];
    int i_has = I && I->has && I->text[0];
    const char *text = a_has ? A->text : (i_has ? I->text : NULL);
    if (!text) return;

    float opacity = shim_modset_float(MS_STATS_OPACITY);
    float start = progress(now, g_mounted + GAME_START_AT, GAME_START_FOR);
    float duck = progress(now, R->stat_changed + DUCK_AFTER, DUCK_FOR);
    float t = progress(now, R->cond.last_change, SWITCH_FOR);
    float from[4], to[4], c[4];
    rgb_of(inactive_p >= 0 ? provider_colour(inactive_p) : 0, slot, from);
    from[3] = i_has ? opacity * start * duck : 0.0f;
    rgb_of(active_p >= 0 ? provider_colour(active_p) : 0, slot, to);
    to[3] = a_has ? opacity * start * duck : 0.0f;
    mix(from, to, t, c);

    char shown[24];
    brackets(text, shown, sizeof(shown));
    float spacing = (float) shim_modset_int(MS_STATS_SPACING);
    float px = x + 1.0f + width_of(b, font, stat_text) + spacing;
    write_text(b, font, shown, px, y, c);

    if (!shim_modset_int(MS_STATS_CHANGES)) return;
    /* the change counts only while it is still building, read LOOK_BACK ago */
    float then = now - LOOK_BACK;
    const metric_t *ac = (A && A->change_has && ACCUMULATE > then - A->change_last) ? A : NULL;
    const metric_t *ic = (I && I->change_has && ACCUMULATE > then - I->change_last) ? I : NULL;
    const metric_t *shown_change = ac ? ac : ic;
    if (!shown_change) return;

    float in_start = R->stat_changed + CHANGE_HOLD_OFF;
    if (A && A->change_has && A->change_last - CHANGE_OUT > in_start) in_start = A->change_last - CHANGE_OUT;
    if (A && A->change_start + CHANGE_IN > in_start) in_start = A->change_start + CHANGE_IN;
    if (in_start <= NEVER * 0.5f) in_start = shown_change->change_last;
    float fade_in = progress(now, in_start, CHANGE_IN);
    float fade_out = 1.0f - progress(now, in_start + CHANGE_IN + CHANGE_HOLD, CHANGE_OUT);
    float slide_t = progress(now, in_start, CHANGE_SLIDE);
    float slide = CHANGE_SLIDE_PX * (1.0f - (1.0f - slide_t) * (1.0f - slide_t) * (1.0f - slide_t));
    float alpha = CHANGE_ALPHA * fade_in * fade_out;
    if (!(alpha > 0.0f)) return;

    float cf[4] = { 0, 0, 0, 0 }, ct[4] = { 0, 0, 0, 0 };
    const metric_t *fc = ic ? ic : ac, *tc = ac ? ac : ic;
    cf[fc->change_positive ? 1 : 0] = 1.0f; cf[3] = ic ? alpha : 0.0f;
    ct[tc->change_positive ? 1 : 0] = 1.0f; ct[3] = ac ? alpha : 0.0f;
    mix(cf, ct, t, c);
    char change[24];
    brackets(shown_change->change_text, change, sizeof(change));
    write_text(b, font, change, px + width_of(b, font, shown) + slide, y, c);
}

/* in the HUD's own pass (mods.c, mark 'X'), after the stats column */
void shim_statsplus_draw(void) {
    unsigned long b = b_now();
    g_draws++;
    if (!b || !shim_tweaks_mod(MOD_STATS)) return;
    unsigned long game = *(const unsigned long *) (b + GAME_PTR);
    unsigned long manager = *(const unsigned long *) (b + MANAGER_PTR);
    if (!game || !manager || !*(const unsigned char *) (manager + MGR_FOUND_HUD)) return;
    if (!shim_eid_hud_showing() || shim_eid_seed_on(SEED_NO_HUD)) return;
    if (g_draws - g_place_seen > 2) return;     /* the column was not drawn lately */

    float now = (float) *(const int *) (game + GAME_FRAME);
    unsigned long stats = game + GAME_STATS;
    unsigned long players[SLOTS];
    for (int s = 0; s < SLOTS; s++) players[s] = *(const unsigned long *) (stats + STATS_PLAYER + s * STATS_SLOT);
    if (game != g_game || memcmp(players, g_player, sizeof(players))) {
        g_game = game;
        memcpy(g_player, players, sizeof(players));
        reset_rows(now);
        LOG("stats+: players %p %p, the numbers start afresh", (void *) players[0], (void *) players[1]);
    }

    unsigned long font = manager + FONT_LUA_MINI;
    const float *shake = (const float *) (game + GAME_SHAKE);
    for (int s = 0; s < SLOTS; s++) {
        unsigned long p = players[s];
        if (!p) continue;
        int type = *(const int *) (p + PLAYER_TYPE);
        for (int r = 0; r < ROWS; r++) {
            /* the column draws no speed for the second of Jacob and Esau (0x6efcb8) */
            if (r == ROW_SPEED && s > 0 && (type == TYPE_JACOB || type == TYPE_ESAU)) continue;
            const float *entry = (const float *) (stats + STATS_ENTRIES + s * STATS_SLOT + r * ENTRY_BYTES);
            update_row(b, s, r, p, entry[0], now);
            char stat_text[16];
            snprintf(stat_text, sizeof(stat_text), "%.2f", (double) entry[0]);
            float x = g_place_x + (float) s * 4.0f + shake[0] + 16.0f;
            float y = g_place_y + (float) r * g_step + g_lift + (float) s * 7.0f + shake[1] + 1.0f;
            draw_row(b, font, s, r, x, y, stat_text, now);
        }
    }
}
