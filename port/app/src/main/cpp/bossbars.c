/*
 * ENHANCED BOSS BARS, WITHOUT LUA.
 *
 * The mod replaces the game's single boss bar with one bar per boss: the
 * boss's own icon at the bar's start, a design of its own for the big fights,
 * the bar flashing on a hit, greyed while the boss cannot be hurt, and a row of
 * status icons (burn, poison, charm...) beside it. Switched on and off in Tweaks
 * ("bars"); everything the mod lets one set is the [bossbars] section of
 * port-touch.ini (modset.c), with the mod's own defaults.
 *
 * THE DATA. tools/mod_bossbars.py reads the mod's bossbar_data.lua into
 * bossbars_data.h - 35 bar styles, 160 boss definitions, the icon conditions,
 * the champion colour variants - and makes an .anm2 for every sheet a bar, an
 * overlay, a notch row or an icon can wear, beside the picture. The mod swaps a
 * sprite's sheet at run time; here loading the sprite already chooses it, so no
 * sheet swap is ever called and every picture is its own texture. The ignore
 * rules (Chub's segments, Mom's doors, Mega Satan's hands...) are the mod's
 * list, written out below.
 *
 * THE GAME'S OWN BAR. EntityList::Update (0x76436c) sums the bosses into a fill
 * and hands it to the HUD; the HUD draws it (0x6eef84..). While the mod is on,
 * the one branch that sends every boss but Gideon to the normal bar is turned
 * to the skip path (0x6ef020, b.ne 0x6ef174 -> b.ne 0x6eefbc). Gideon keeps the
 * game's own bar, because only that one carries his wave counter; the mod does
 * the same on the PC and draws its bar under it.
 *
 * WHAT IS READ, all measured in the image:
 *
 *   room [Game+0x21550], its entities at room+0x1950: array +0x78, count +0x84;
 *   the room's boss id +0x21e0 (55 Mega Satan)
 *   entity  +0x38 type  +0x3c variant  +0x40 subtype  +0x50 sprite (embedded)
 *           +0x1b8 flags (u64)  +0x1c2 exists  +0x354 HP  +0x358 max HP
 *           +0x390 parent  +0x398 child  +0x3a8 spawner
 *   an NPC is type 10..999 (0x82bac8); a boss is byte +0xcc9 of an NPC
 *   NPC     +0xb70 state  +0xbf0 I1  +0xbf4 I2
 *   sprite  +0x38 the current animation's data, its name a std::string at +0,
 *           its frame count +0x44; +0xdc scale, +0xe4 rotation, +0xe8 tint,
 *           +0xf8 colorize, +0x108 offset (the constructor 0x89c16c copies them
 *           in that order, the order of the API's Sprite fields)
 *   0x82dca8(entity, NULL)  can it be hurt (the mod's IsVulnerableEnemy)
 *   0x89e4c0(sprite, position, top-left clamp, bottom-right clamp)
 *   Game+0x1af34c  the update frame count (the mod's GetFrameCount)
 */
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <android/asset_manager.h>
#include <android/log.h>
#include "diag.h"
#include "modset.h"
#include "bossbars_data.h"

#if ISAAC_DIAG
#define LOG(...) __android_log_print(ANDROID_LOG_INFO, "isaac", __VA_ARGS__)
#else
#define LOG(...) do { } while (0)
#endif

unsigned long shim_image_base(void);
int shim_report_active(void);
void report_line(const char *kind, const char *text);
int shim_tweaks_mod(int i);
int shim_eid_hud_showing(void);
int shim_eid_seed_on(int id);
int shim_picture_ready(unsigned char *sprite, signed char *state, const char *anm2);
void shim_asset_bring_in(void *mgr, const char *name, const char *folder, const char *leaf);
int shim_patch_word(unsigned long off, uint32_t was_a, uint32_t was_b, uint32_t now);

#define MOD_BARS          3
#define SEED_NO_HUD       10

#define MANAGER_PTR       0xac2428
#define GAME_PTR          0xac3b90
#define DESIGN_CANVAS     0xac24d8
#define MGR_HUD_OFFSET    0x55f0c
#define GAME_ROOM         0x21550
#define GAME_FRAME        0x1af34c
#define GAME_STAGE        0x0
#define GAME_STAGE_TYPE   0x4
#define GAME_PLAYER_LIST  0x25c40
#define FN_HAS_COLLECTIBLE 0x4eeb74           /* (player, id, ignore modifiers) */
#define ROOM_ENTITIES     0x1950
#define ROOM_BOSS_ID      0x21e0
#define LIST_ARRAY        0x78
#define LIST_COUNT        0x84
#define BOSS_MEGA_SATAN   55
#define ITEM_SPIDER_MOD   403

#define E_INDEX           0x30
#define E_TYPE            0x38
#define E_VARIANT         0x3c
#define E_SUBTYPE         0x40
#define E_SPRITE          0x50
#define E_FLAGS           0x1b8
#define E_EXISTS          0x1c2
#define E_HP              0x354
#define E_MAX_HP          0x358
#define E_PARENT          0x390
#define E_CHILD           0x398
#define E_SPAWNER         0x3a8
#define NPC_BOSS          0xcc9
#define NPC_STATE         0xb70
#define NPC_I1            0xbf0
#define NPC_I2            0xbf4
#define SPRITE_ANIMATION  0x38
#define ANIMATION_FRAMES  0x44

#define FN_VULNERABLE     0x82dca8
#define FN_SPRITE_FRAME   0x89eab8
#define FN_SPRITE_RENDER  0x89e4c0
#define FN_KCOLOR         0x8a34f4
#define FN_DRAW_SCALED    0x8be42c
#define FN_TEXT_WIDTH     0x8bdec8
#define ZERO_VECTOR       0x95e7a0
#define SPRITE_BYTES      0x158
#define SPRITE_ROTATION   0xe4
#define SPRITE_TINT       0xe8                  /* R G B A */
#define SPRITE_OFFSET     0x108                 /* RO GO BO */

/* the game's boss bar: its one branch to the normal draw, sent to the skip */
#define BAR_BRANCH        0x6ef020
#define BAR_BRANCH_GAME   0x54000AA1u           /* b.ne 0x6ef174 */
#define BAR_BRANCH_SKIP   0x54FFFCE1u           /* b.ne 0x6eefbc */

#define FLAG_FRIENDLY          29
#define FLAG_DONT_COUNT_BOSS_HP 31

#define BOSSES_MAX        32
#define ROW_STEP          20.0f

/* ----------------------------------------------------------------- assets */
void shim_bossbars_open_assets(void *manager) {
    AAssetManager *mgr = (AAssetManager *) manager;
    if (!mgr) return;
    int n = 0;
    for (int i = 0; i < BB_DIR_COUNT; i++) {
        char asset_dir[96], folder[96];
        snprintf(asset_dir, sizeof(asset_dir), "port_bossbars/%s", BB_DIRS[i]);
        snprintf(folder, sizeof(folder), "gfx/ui/%s", BB_DIRS[i]);
        AAssetDir *d = AAssetManager_openDir(mgr, asset_dir);
        if (!d) continue;
        const char *leaf;
        while ((leaf = AAssetDir_getNextFileName(d)) != NULL) {
            char name[200];
            snprintf(name, sizeof(name), "%s/%s", asset_dir, leaf);
            shim_asset_bring_in(mgr, name, folder, leaf);
            n++;
        }
        AAssetDir_close(d);
    }
    LOG("bossbars: %d files are in the resource table", n);
}

/*
 * The styles the STYLE row offers, for the [bossbars] style key (modset.c).
 *
 * Only the designs meant for every bar (the owner, 27.09.2026). The mod lists
 * its twelve boss designs - Beast, Dogma, Mother and the rest - in the same
 * list, so picking one dressed EVERY boss as that boss; the bosses wear them
 * through BOSS DESIGNS, which is its own switch. A style is a boss design when
 * a boss of BB_BOSS wears it: 23 of the 35 are left. tools/modset_pages.py
 * draws the row's words by the same rule, so the numbers agree. An old
 * port-touch.ini naming a boss design no longer matches a word and the style
 * falls back to Default.
 */
static short g_general[BB_STYLES];
static int g_general_n = -1;

static void general_styles(void) {
    if (g_general_n >= 0) return;
    unsigned char worn[BB_STYLES] = {0};
    for (int k = 0; k < BB_BOSSES; k++)
        if (BB_BOSS[k].style >= 0 && BB_BOSS[k].style < BB_STYLES) worn[BB_BOSS[k].style] = 1;
    int n = 0;
    for (int k = 0; k < BB_STYLES; k++)
        if (!worn[k]) g_general[n++] = (short) k;
    g_general_n = n;
}

/* the STYLE row's choice, as an index into BB_STYLE */
static int general_style(int choice) {
    general_styles();
    return (choice >= 0 && choice < g_general_n) ? g_general[choice] : g_general[0];
}

int shim_bossbars_style_count(void) { general_styles(); return g_general_n; }
const char *shim_bossbars_style_name(int i) { return BB_STYLE[general_style(i)].name; }

/* ---------------------------------------------------------------- entities */
static int is_npc(unsigned long e) {
    return (unsigned) (*(const int *) (e + E_TYPE) - 10) < 990;
}

static int flag(unsigned long e, int bit) {
    return (*(const unsigned long *) (e + E_FLAGS) >> bit) & 1;
}

static unsigned long parent_of(unsigned long e) { return *(const unsigned long *) (e + E_PARENT); }
static unsigned long child_of(unsigned long e) { return *(const unsigned long *) (e + E_CHILD); }

/* the sprite's current animation name, or "" */
static const char *animation_of(unsigned long e) {
    const unsigned char *data = *(const unsigned char *const *) (e + E_SPRITE + SPRITE_ANIMATION);
    if (!data) return "";
    if ((signed char) data[0x17] < 0) {
        const char *p = *(const char *const *) data;
        return p ? p : "";
    }
    return (const char *) data;
}

static int npc_int(unsigned long e, unsigned long at) {
    return is_npc(e) ? *(const int *) (e + at) : 0;
}

/*
 * A boss's colour variant (Lua GetBossColorIdx): int npc+0xd24, -1 for the
 * plain boss, set by Init from the entity's bosscolors list (0x6823e4).
 * A champion (Lua GetChampionColorIdx >= 0): byte npc+0xd0f, set by
 * MakeChampion 0x6825bc. Either paints the entity's own Color, the 0x2c bytes
 * of its embedded sprite at entity+0x138 (UpdateColor 0x689540).
 */
#define NPC_BOSS_COLOUR   0xd24
#define NPC_CHAMPION      0xd0f
#define E_COLOUR          0x138                 /* entity+0x50+0xe8: tint, colorize, offset */
#define COLOUR_BYTES      0x2c

static int boss_colour_index(unsigned long e) {
    return is_npc(e) ? *(const int *) (e + NPC_BOSS_COLOUR) : -1;
}

static int is_champion(unsigned long e) {
    return is_npc(e) && *(const unsigned char *) (e + NPC_CHAMPION);
}

/*
 * The mod's evaluateEntityIgnore: friends and bosses that do not count, all of
 * Delirium's brood, and its list of parts that are not bosses of their own -
 * with the three fights the settings can leave without bars.
 */
static int ignored(unsigned long e) {
    if (flag(e, FLAG_FRIENDLY) || flag(e, FLAG_DONT_COUNT_BOSS_HP)) return 1;
    unsigned long p = parent_of(e);
    for (int depth = 0; p && depth < 32; depth++, p = parent_of(p))
        if (*(const int *) (p + E_TYPE) == 412) return 1;

    int type = *(const int *) (e + E_TYPE), variant = *(const int *) (e + E_VARIANT);
    const char *anim = animation_of(e);
    int mega_satan = shim_modset_int(MS_BARS_MEGA_SATAN), mother = shim_modset_int(MS_BARS_MOTHER);
    int beast = shim_modset_int(MS_BARS_BEAST);
    switch (type * 1000 + variant) {
    case 28000: case 28001: case 28002:              /* Chub, Chad, Carrion Queen segments */
    case 62000: case 62001: case 62002: case 62003:  /* Pin, Scolex, Frail, Wormwood segments */
    case 65001:                                      /* Conquest */
    case 412000:                                     /* what Delirium spawns */
    case 918000:                                     /* Turdlet segments */
        return parent_of(e) != 0;
    case 45000:                                      /* Mom's doors */
    case 266001: case 266002:                        /* Mama Gurdy's hands */
    case 294000:                                     /* Ultra Greed's doors */
    case 411001:                                     /* Big Horn's hand */
    case 866000:                                     /* Dark Esau */
    case 867000:                                     /* Mother's shadow */
    case 906001:                                     /* Hornfel's decoy */
    case 912030: case 912100:                        /* Mother's attacks */
    case 919001:                                     /* Raglich's arm */
    case 950000:                                     /* Dogma's invincible baby */
    case 951100: case 951101: case 951102: case 951103: case 951104:   /* the Beast's scenery */
    case 964000:                                     /* the dummy */
        return 1;
    case 84000: return npc_int(e, NPC_I1) <= 1;     /* Satan before his last phase */
    case 84010: return child_of(e) == 0;            /* Satan's second leg */
    case 101000: case 101001: return npc_int(e, NPC_I1) == 1;   /* the leg stomps */
    case 274000: case 274001: case 274002:           /* Mega Satan asleep */
        return npc_int(e, NPC_STATE) == 2 || !mega_satan;
    case 275000: return !mega_satan;                 /* Mega Satan's second phase */
    case 406000: case 406001: return !strcmp(anim, "Final") || !strcmp(anim, "Death");
    case 903001: return strstr(anim, "Stun") != NULL;             /* the Mask */
    case 905000: return *(const unsigned long *) (e + E_SPAWNER) != 0;   /* the Heretic's dummy */
    case 912000: return parent_of(e) != 0 || !strcmp(anim, "Transition") || !mother;
    case 912010: return !mother;
    case 950001: return strstr(anim, "Destroyed") != NULL;        /* Dogma's TV */
    case 951000: return (!strcmp(anim, "Idle") && npc_int(e, NPC_STATE) == 16) || !beast;
    case 951010: case 951020: case 951030: case 951040: return !beast;   /* the horsemen */
    }
    return 0;
}

static const bb_boss_t *definition_of(int type, int variant) {
    int lo = 0, hi = BB_BOSSES - 1;
    while (lo <= hi) {
        int mid = (lo + hi) / 2;
        const bb_boss_t *d = &BB_BOSS[mid];
        int cmp = d->type != type ? (d->type < type ? -1 : 1)
                : d->variant != variant ? (d->variant < variant ? -1 : 1) : 0;
        if (!cmp) return d;
        if (cmp < 0) lo = mid + 1; else hi = mid - 1;
    }
    return NULL;
}

/* ------------------------------------------------------------------ bosses */
typedef struct {
    unsigned long e;
    int type, variant, subtype;
    float hp, max_hp;
    int change_frame;
    signed char hit;                        /* 1 damage, 2 heal */
} boss_t;

/* a bar: the boss it stands for and the health it shows, summed over what it covers */
typedef struct {
    int boss;
    float hp, max_hp;
    int change_frame;
    signed char hit;
    signed char ignore_invincible;
    signed char merged;                     /* the one bar of "vanilla" over several bosses */
} bar_t;

static boss_t g_boss[BOSSES_MAX];
static int g_bosses;
static bar_t g_bar_list[BOSSES_MAX];
static int g_bars;
static int g_last_frame = -1;
static unsigned long g_last_room;

static int find_boss(unsigned long e) {
    for (int i = 0; i < g_bosses; i++) if (g_boss[i].e == e) return i;
    return -1;
}

static void add_bar(int boss) {
    if (g_bars >= BOSSES_MAX) return;
    const boss_t *b = &g_boss[boss];
    const bb_boss_t *d = definition_of(b->type, b->variant);
    bar_t *bar = &g_bar_list[g_bars++];
    bar->boss = boss;
    bar->hp = b->hp;
    bar->max_hp = b->max_hp;
    bar->change_frame = b->change_frame;
    bar->hit = b->hit;
    bar->ignore_invincible = d && d->ignore_invincible;
    bar->merged = 0;
}

static void fold_into(bar_t *bar, const boss_t *b) {
    bar->hp += b->hp;
    bar->max_hp += b->max_hp;
    if (b->change_frame > bar->change_frame) { bar->change_frame = b->change_frame; bar->hit = b->hit; }
}

static void track(unsigned long room, int frame) {
    if (room != g_last_room) { g_last_room = room; g_bosses = 0; }
    unsigned long list = room + ROOM_ENTITIES;
    const unsigned long *items = *(const unsigned long *const *) (list + LIST_ARRAY);
    unsigned n = *(const unsigned *) (list + LIST_COUNT);

    boss_t fresh[BOSSES_MAX];
    int count = 0;
    for (unsigned k = 0; items && k < n && count < BOSSES_MAX; k++) {
        unsigned long e = items[k];
        if (!e || !*(const unsigned char *) (e + E_EXISTS)) continue;
        int at = find_boss(e);
        int boss = is_npc(e) && *(const unsigned char *) (e + NPC_BOSS);
        if (at < 0 && !(boss && !ignored(e))) continue;
        boss_t b;
        if (at >= 0) {
            b = g_boss[at];
            float hp = *(const float *) (e + E_HP);
            if (hp < b.hp) { b.change_frame = frame; b.hit = 1; }
            else if (hp > b.hp) { b.change_frame = frame; b.hit = 2; }
            b.hp = hp;
            b.max_hp = *(const float *) (e + E_MAX_HP);
            /* a bar goes when its boss changes kind, dies or becomes ignorable */
            if (b.type != *(const int *) (e + E_TYPE) || b.variant != *(const int *) (e + E_VARIANT)
                || b.subtype != *(const int *) (e + E_SUBTYPE) || hp <= 0.0f || ignored(e))
                continue;
        } else {
            memset(&b, 0, sizeof(b));
            b.e = e;
            b.type = *(const int *) (e + E_TYPE);
            b.variant = *(const int *) (e + E_VARIANT);
            b.subtype = *(const int *) (e + E_SUBTYPE);
            b.hp = *(const float *) (e + E_HP);
            b.max_hp = *(const float *) (e + E_MAX_HP);
            if (b.hp <= 0.0f) continue;
        }
        fresh[count++] = b;
    }
    /* the mod sorts by Entity.Index: int entity+0x30, given by Room::AddEntity 0x5ac308 */
    for (int i = 1; i < count; i++)
        for (int j = i; j > 0 && *(const int *) (fresh[j - 1].e + E_INDEX) > *(const int *) (fresh[j].e + E_INDEX); j--) {
            boss_t t = fresh[j]; fresh[j] = fresh[j - 1]; fresh[j - 1] = t;
        }
    memcpy(g_boss, fresh, (size_t) count * sizeof(boss_t));
    g_bosses = count;
    g_bars = 0;
    if (!g_bosses) return;

    int sorting = shim_modset_int(MS_BARS_SORTING);
    if (sorting == MS_SORTING_VANILLA) {
        /* one bar for all, standing for the first boss */
        add_bar(0);
        bar_t *bar = &g_bar_list[0];
        bar->merged = g_bosses > 1;
        for (int i = 1; i < g_bosses; i++) {
            const bb_boss_t *d = definition_of(g_boss[i].type, g_boss[i].variant);
            fold_into(bar, &g_boss[i]);
            if (d && d->ignore_invincible) bar->ignore_invincible = 1;
        }
        return;
    }

    /* every head, then the parts of its chain after it - or folded into it */
    int visited[BOSSES_MAX] = { 0 };
    for (int i = 0; i < g_bosses; i++) {
        if (visited[i] || parent_of(g_boss[i].e)) continue;
        visited[i] = 1;
        const bb_boss_t *d = definition_of(g_boss[i].type, g_boss[i].variant);
        int apart = sorting == MS_SORTING_SEGMENTS || (d && d->segmented);
        bar_t head;
        memset(&head, 0, sizeof(head));
        head.hp = g_boss[i].hp;
        head.max_hp = g_boss[i].max_hp;
        head.change_frame = g_boss[i].change_frame;
        head.hit = g_boss[i].hit;
        for (unsigned long c = child_of(g_boss[i].e); c; c = child_of(c)) {
            int at = find_boss(c);
            if (at < 0 || visited[at]) break;
            visited[at] = 1;
            if (apart) add_bar(at);
            else fold_into(&head, &g_boss[at]);
        }
        add_bar(i);
        bar_t *mine = &g_bar_list[g_bars - 1];
        mine->hp = head.hp;
        mine->max_hp = head.max_hp;
        mine->change_frame = head.change_frame;
        mine->hit = head.hit;
    }
    for (int i = 0; i < g_bosses; i++) if (!visited[i]) add_bar(i);

    /* Lokii's twins: taken out, the one with I1 == 2 first, and put back at the end */
    bar_t lokii[BOSSES_MAX];
    int nl = 0, kept = 0;
    for (int k = 0; k < g_bars; k++) {
        const boss_t *b = &g_boss[g_bar_list[k].boss];
        const bb_boss_t *d = definition_of(b->type, b->variant);
        if (d && d->lokii) lokii[nl++] = g_bar_list[k];
        else g_bar_list[kept++] = g_bar_list[k];
    }
    for (int pass = 0; pass < 2; pass++)
        for (int k = 0; k < nl; k++) {
            int first = npc_int(g_boss[lokii[k].boss].e, NPC_I1) == 2;
            if (first == (pass == 0)) g_bar_list[kept++] = lokii[k];
        }
    /* and the whole list turned round, as the mod finishes */
    for (int a = 0, z = g_bars - 1; a < z; a++, z--) {
        bar_t t = g_bar_list[a]; g_bar_list[a] = g_bar_list[z]; g_bar_list[z] = t;
    }
}

/* ---------------------------------------------------------------- sprites */
static unsigned char g_icon[BB_ICONS][SPRITE_BYTES] __attribute__((aligned(16)));
static signed char g_icon_state[BB_ICONS];
static unsigned char g_plain[SPRITE_BYTES] __attribute__((aligned(16)));
static signed char g_plain_state;
#define ORIENTATIONS 2
static unsigned char g_bar[ORIENTATIONS][BB_STYLES][SPRITE_BYTES] __attribute__((aligned(16)));
static signed char g_bar_state[ORIENTATIONS][BB_STYLES];
static unsigned char g_overlay[ORIENTATIONS][BB_STYLES][SPRITE_BYTES] __attribute__((aligned(16)));
static signed char g_overlay_state[ORIENTATIONS][BB_STYLES];
static unsigned char g_notch[BB_STYLES][SPRITE_BYTES] __attribute__((aligned(16)));
static signed char g_notch_state[BB_STYLES];
static unsigned char g_status[SPRITE_BYTES] __attribute__((aligned(16)));
static signed char g_status_state;

static void colour(unsigned char *sprite, const float *c) {
    float *tint = (float *) (sprite + SPRITE_TINT);
    float *offset = (float *) (sprite + SPRITE_OFFSET);
    tint[0] = c[0]; tint[1] = c[1]; tint[2] = c[2]; tint[3] = c[3];
    offset[0] = c[4]; offset[1] = c[5]; offset[2] = c[6];
}

static const float WHITE[7] = { 1, 1, 1, 1, 0, 0, 0 };
static const float GIDEON[7] = { 0.2f, 0.2f, 0.2f, 1, 0, 0, 0 };

/*
 * The frame of an animation: the HP's for a sprite that follows the health, the
 * clock's for one that plays by itself (the mod lets such a sprite run with
 * Update() once a game frame; the game frame count gives the same frame).
 */
static void frame_of(unsigned char *sprite, const char *anim, int animated, int hp_frame, int clock) {
    unsigned long b = shim_image_base();
    void (*set)(void *, const char *, int) = (void (*)(void *, const char *, int)) (b + FN_SPRITE_FRAME);
    if (!animated) { set(sprite, anim, hp_frame); return; }
    set(sprite, anim, 0);
    const unsigned char *data = *(const unsigned char *const *) (sprite + SPRITE_ANIMATION);
    int frames = data ? *(const int *) (data + ANIMATION_FRAMES) : 1;
    if (frames > 1) set(sprite, anim, clock % frames);
}

static void render(unsigned char *sprite, float x, float y, float clamp_right) {
    unsigned long b = shim_image_base();
    float pos[2] = { x, y };
    float right[2] = { clamp_right, 0.0f };
    ((void (*)(void *, const float *, const void *, const void *)) (b + FN_SPRITE_RENDER))(
        sprite, pos, (const void *) (b + ZERO_VECTOR),
        clamp_right > 0.0f ? (const void *) right : (const void *) (b + ZERO_VECTOR));
}

/* ------------------------------------------------------------------ icons */
static int condition_holds(const bb_cond_t *c, const boss_t *boss) {
    unsigned long e = boss->e;
    int has_parent = parent_of(e) != 0, has_child = child_of(e) != 0;
    const char *anim = animation_of(e);
    size_t al = strlen(anim), tl = c->text ? strlen(c->text) : 0;
    unsigned long b = shim_image_base();
    unsigned long game = *(const unsigned long *) (b + GAME_PTR);
    switch (c->kind) {
    case BB_HEAD:   return !has_parent && has_child;
    case BB_MIDDLE: return has_parent && has_child;
    case BB_TAIL:   return has_parent && !has_child;
    case BB_CHILD:  return has_parent;
    case BB_I1_EQUAL: return npc_int(e, NPC_I1) == (int) c->number;
    case BB_I2_EQUAL: return npc_int(e, NPC_I2) == (int) c->number;
    case BB_HP_BELOW: return boss->hp < c->number;
    case BB_HP_PERCENT_BELOW: return boss->max_hp > 0 && boss->hp / boss->max_hp * 100.0f < c->number;
    case BB_ANIM_STARTS:   return c->text && !strncmp(anim, c->text, tl);
    case BB_ANIM_ENDS:     return c->text && al >= tl && !strcmp(anim + al - tl, c->text);
    case BB_ANIM_CONTAINS: return c->text && strstr(anim, c->text) != NULL;
    case BB_ANIM_EQUAL:    return c->text && !strcmp(anim, c->text);
    case BB_STAGE_TYPE: return game && *(const int *) (game + GAME_STAGE_TYPE) == (int) c->number;
    case BB_STAGE:      return game && *(const int *) (game + GAME_STAGE) == (int) c->number;
    case BB_HEAD_I2:   return !has_parent && has_child && npc_int(e, NPC_I2) == (int) c->number;
    case BB_MIDDLE_I2: return has_parent && has_child && npc_int(e, NPC_I2) == (int) c->number;
    case BB_TAIL_I2:   return has_parent && !has_child && npc_int(e, NPC_I2) == (int) c->number;
    }
    return 0;
}

/* which icon a boss wears: its conditions, its own picture, its style's default - then its colour */
static int icon_of(const bb_boss_t *d, const boss_t *boss, int style) {
    int icon = BB_STYLE[style].icon;
    if (d) {
        int found = -1;
        for (int k = 0; k < d->cond_count && found < 0; k++) {
            const bb_cond_t *c = &BB_COND[d->cond_first + k];
            if (c->icon >= 0 && condition_holds(c, boss)) found = c->icon;
        }
        if (found < 0) found = d->icon;
        if (found >= 0) icon = found;
        int colour_index = boss_colour_index(boss->e);
        if (colour_index >= 0 && colour_index < d->colour_count) {
            const char *suffix = BB_COLOUR[d->colour_first + colour_index];
            for (int v = 0; v < BB_VARIANTS; v++)
                if (BB_VARIANT[v].icon == icon && !strcmp(BB_VARIANT[v].suffix, suffix)) {
                    icon = BB_VARIANT[v].coloured;
                    break;
                }
        }
    }
    return icon;
}

/*
 * The status icons, in the order of the mod's table. The bits are the game's
 * EntityFlag numbers; the low ones are confirmed in the image, the rest are
 * the documented values (BURN 12, SHRINK 25, BLEED_OUT 34, ICE_FROZEN 49,
 * ICE 50, MAGNETIZED 51, BAITED 52, WEAKNESS 54, BRIMSTONE_MARKED 56).
 */
#define CHAINED -1                               /* not a flag: an Anima Sola chain holds it */

static const struct { int bit, bit2; int frame; } STATUS[] = {
    { 12, -1, 0 },  { 8, -1, 1 },  { 9, -1, 2 },  { 11, -1, 3 },  { 5, 10, 4 },
    { 6, -1, 5 },   { 7, -1, 6 },  { 29, -1, 7 }, { 25, -1, 8 },  { 34, -1, 9 },
    { 52, -1, 10 }, { CHAINED, -1, 11 }, { 49, 50, 12 }, { 51, -1, 13 }, { 56, -1, 14 },
    { 54, -1, 15 },
};

/*
 * Is the boss held by Anima Sola's chain: an effect of type 1000, variant 193
 * ("Anima Chain", entities2.xml) in the room, whose Target (+0x3a0, written by
 * SetTarget 0x82bbb4 when the item spawns it, 0x58e364) is this boss.
 */
#define EFFECT_TYPE     1000
#define ANIMA_CHAIN     193
#define E_TARGET        0x3a0

static int chained(unsigned long boss) {
    unsigned long b = shim_image_base();
    unsigned long game = b ? *(const unsigned long *) (b + GAME_PTR) : 0;
    unsigned long room = game ? *(const unsigned long *) (game + GAME_ROOM) : 0;
    if (!room) return 0;
    const unsigned long *items = *(const unsigned long *const *) (room + ROOM_ENTITIES + LIST_ARRAY);
    unsigned n = *(const unsigned *) (room + ROOM_ENTITIES + LIST_COUNT);
    for (unsigned k = 0; items && k < n; k++) {
        unsigned long e = items[k];
        if (!e || !*(const unsigned char *) (e + E_EXISTS)) continue;
        if (*(const int *) (e + E_TYPE) != EFFECT_TYPE || *(const int *) (e + E_VARIANT) != ANIMA_CHAIN) continue;
        if (*(const unsigned long *) (e + E_TARGET) == boss) return 1;
    }
    return 0;
}

/* ------------------------------------------------------------------- text */
/*
 * The bar's text: its health as a percentage or what is left, drawn the way
 * Isaac.RenderScaledText draws it - white, at the settings' size and opacity.
 */
#define TEXT_FONT 0x967c8                       /* Manager+: PFTempestaSevenCondensed, the Lua API's (I) */

static void bar_text(const bar_t *bar, float x, float y, int size, int vertical) {
    int what = shim_modset_int(MS_BARS_TEXT);
    if (what == MS_TEXT_NONE) return;
    unsigned long b = shim_image_base();
    unsigned long manager = *(const unsigned long *) (b + MANAGER_PTR);
    if (!manager || bar->max_hp <= 0) return;
    char text[24];
    if (what == MS_TEXT_PERCENT) {
        snprintf(text, sizeof(text), "%d%%", (int) ceilf(bar->hp / bar->max_hp * 100.0f));
    } else {
        float left = ceilf(bar->hp * 10.0f) / 10.0f;
        if (left == floorf(left)) snprintf(text, sizeof(text), "%d", (int) left);
        else snprintf(text, sizeof(text), "%.1f", (double) left);
    }
    unsigned long font = manager + TEXT_FONT;
    float scale = shim_modset_float(MS_BARS_TEXT_SIZE);
    float width = (float) ((int (*)(unsigned long, const char *)) (b + FN_TEXT_WIDTH))(font, text);
    float tx = vertical ? x - width * 0.5f * scale : x + (float) size * 0.5f - width * 0.5f * scale;
    float ty = vertical ? y - (float) size * 0.5f - 6.0f * scale : y - 6.0f * scale;
    unsigned char colour_buf[32];
    memset(colour_buf, 0, sizeof(colour_buf));
    ((void (*)(void *, float, float, float, float)) (b + FN_KCOLOR))(
        colour_buf, 1.0f, 1.0f, 1.0f, shim_modset_float(MS_BARS_TEXT_ALPHA));
    ((void (*)(unsigned long, const char *, void *, int, int, float, float, float, float))
        (b + FN_DRAW_SCALED))(font, text, colour_buf, 0, 0, tx, ty, scale, scale);
}

/* ------------------------------------------------------------------- draw */
static const int SIZES_ACROSS[10] = { 120, 120, 60, 60, 30, 30, 30, 15, 15, 15 };
static const int SIZES_DOWN[10] = { 120, 60, 60, 30, 30, 15, 15, 15, 15, 15 };

static void draw_bar(const bar_t *bar, float x, float y, int size, int clock, int vertical,
                     int gideon) {
    const boss_t *boss = &g_boss[bar->boss];
    const bb_boss_t *d = definition_of(boss->type, boss->variant);
    int style = general_style(shim_modset_int(MS_BARS_STYLE));
    if (!bar->merged && shim_modset_int(MS_BARS_BOSS_DESIGNS) && d && d->style >= 0) style = d->style;
    const bb_style_t *s = &BB_STYLE[style];
    int o = vertical ? 1 : 0;
    float rotation = vertical ? -90.0f : 0.0f;
    int percent = 100 * size / 120;
    int fill = 1;
    if (!gideon) {
        int pct = bar->max_hp > 0 ? (int) ceilf(bar->hp / bar->max_hp * 100.0f) : 0;
        if (pct < 0) pct = 0;
        fill = 100 - (pct < 99 ? pct : 99);
    }
    char anim[16];

    const char *bar_anm2 = vertical ? s->bar_vertical : s->bar;
    unsigned char *sprite = g_bar[o][style];
    if (bar_anm2 && !shim_picture_ready(sprite, &g_bar_state[o][style], bar_anm2)) {
        static int said;                    /* PROBE: the owner saw only the game's bars */
        if (said < 6 && shim_report_active()) {
            said++;
            char line[200];
            snprintf(line, sizeof(line), "the bar %s did not load", bar_anm2);
            report_line("bossbars", line);
        }
    }
    if (bar_anm2 && g_bar_state[o][style] > 0) {
        *(float *) (sprite + SPRITE_ROTATION) = rotation;
        colour(sprite, WHITE);
        snprintf(anim, sizeof(anim), "bg%d", percent);
        frame_of(sprite, anim, s->bar_animated, fill, clock);
        render(sprite, x, y, 0.0f);

        const float *c = s->colour[0];
        int frame_now = clock;
        if (gideon) {
            c = GIDEON;
        } else if (shim_modset_int(MS_BARS_FLASHING) && bar->hit
                   && bar->change_frame + 7 >= frame_now && (frame_now & 1)) {
            c = s->colour[bar->hit == 1 ? 1 : 2];
        } else if (shim_modset_int(MS_BARS_INVINCIBLE) && !bar->ignore_invincible && is_npc(boss->e)
                   && !((int (*)(unsigned long, unsigned long)) (shim_image_base() + FN_VULNERABLE))(boss->e, 0)) {
            c = s->colour[3];
        }
        colour(sprite, c);
        snprintf(anim, sizeof(anim), "charge%d", percent);
        frame_of(sprite, anim, s->bar_animated, fill, clock);
        float progress = (float) size / 100.0f * (float) fill;
        render(sprite, x, y, 1.0f + progress);

        frame_of(sprite, "endDeco", s->bar_animated, fill, clock);
        if (vertical) render(sprite, x, y - ((float) size - 1.0f - progress), 0.0f);
        else render(sprite, x + (float) size - 1.0f - progress, y, 0.0f);
        colour(sprite, WHITE);
        *(float *) (sprite + SPRITE_ROTATION) = 0.0f;
    }
    if (shim_modset_int(MS_BARS_NOTCHES) && s->notch
        && shim_picture_ready(g_notch[style], &g_notch_state[style], s->notch)) {
        *(float *) (g_notch[style] + SPRITE_ROTATION) = rotation;
        snprintf(anim, sizeof(anim), "overlay%d", percent);
        frame_of(g_notch[style], anim, s->notch_animated, fill, clock);
        render(g_notch[style], x, y, 0.0f);
        *(float *) (g_notch[style] + SPRITE_ROTATION) = 0.0f;
    }
    const char *overlay = vertical ? s->overlay_vertical : s->overlay;
    if (overlay && shim_picture_ready(g_overlay[o][style], &g_overlay_state[o][style], overlay)) {
        unsigned char *ov = g_overlay[o][style];
        *(float *) (ov + SPRITE_ROTATION) = rotation;
        snprintf(anim, sizeof(anim), "overlay%d", percent);
        frame_of(ov, anim, s->overlay_animated, fill, clock);
        render(ov, x, y, 0.0f);
        *(float *) (ov + SPRITE_ROTATION) = 0.0f;
    }

    if (shim_modset_int(MS_BARS_ICONS)) {
        unsigned char *icon_sprite = NULL;
        int animated = 0;
        float ox = -4.0f, oy = 0.0f;
        if (shim_modset_int(MS_BARS_BOSS_ICONS)) {
            int ic = icon_of(bar->merged ? NULL : d, boss, style);
            if (ic >= 0 && ic < BB_ICONS && shim_picture_ready(g_icon[ic], &g_icon_state[ic], BB_ICON[ic]))
                icon_sprite = g_icon[ic];
            if (d && !bar->merged) { ox = d->off_x; oy = d->off_y; animated = d->icon_animated; }
        } else if (shim_picture_ready(g_plain, &g_plain_state, BB_PLAIN_ICON)) {
            icon_sprite = g_plain;
        }
        if (icon_sprite) {
            /*
             * A champion's icon in the champion's colours - the entity's own
             * Color, but not while it is the half-grey hit flash (the mod's
             * filter: no channel at 0.5, and a red offset in [0.5, 0.9)).
             */
            int tinted = 0;
            if (!bar->merged && shim_modset_int(MS_BARS_CHAMPION_COLOURS)
                && (is_champion(boss->e) || boss_colour_index(boss->e) >= 0)) {
                const float *c = (const float *) (boss->e + E_COLOUR);
                if (c[0] != 0.5f && c[1] != 0.5f && c[2] != 0.5f && c[8] >= 0.5f && c[8] < 0.9f) {
                    memcpy(icon_sprite + SPRITE_TINT, c, COLOUR_BYTES);
                    tinted = 1;
                }
            }
            frame_of(icon_sprite, "idle", animated, fill, clock);
            if (vertical) render(icon_sprite, x - oy, y - ox, 0.0f);
            else render(icon_sprite, x + ox - 3.0f, y + oy, 0.0f);
            if (tinted) {
                memset(icon_sprite + SPRITE_TINT, 0, COLOUR_BYTES);
                colour(icon_sprite, WHITE);
            }
        }
    }
    if (gideon) return;
    /*
     * The icon is a frame of "idle" by number: a statuseffect_icon.anm2 shorter
     * than the one the port brings (a user mod's) shows only what it has (M8).
     */
    static int status_length = -2;
    if (status_length == -2) {
        int shim_port_length(const char *rel, const char *anim);
        status_length = shim_port_length("gfx/ui/bosshp_icons/statuseffect_icon.anm2", "idle");
    }
    if (shim_modset_int(MS_BARS_STATUS) && status_length > 0
        && shim_picture_ready(g_status, &g_status_state, "gfx/ui/bosshp_icons/statuseffect_icon.anm2")) {
        int shown = 0;
        for (unsigned k = 0; k < sizeof(STATUS) / sizeof(STATUS[0]); k++) {
            if (STATUS[k].frame >= status_length) continue;
            int on = STATUS[k].bit == CHAINED ? chained(boss->e)
                   : flag(boss->e, STATUS[k].bit) || (STATUS[k].bit2 >= 0 && flag(boss->e, STATUS[k].bit2));
            if (!on) continue;
            shown++;
            ((void (*)(void *, const char *, int)) (shim_image_base() + FN_SPRITE_FRAME))(
                g_status, "idle", STATUS[k].frame);
            if (vertical) render(g_status, x - 6.0f, y - (float) shown * 10.0f, 0.0f);
            else render(g_status, x + (float) shown * 10.0f, y - 6.0f, 0.0f);
        }
    }
    bar_text(bar, x, y, size, vertical);
}

static int spider_mod_carried(unsigned long game) {
    const unsigned long *list = (const unsigned long *) (game + GAME_PLAYER_LIST);
    if (!list[2] || list[3] < list[2]) return 0;
    for (unsigned long *p = (unsigned long *) list[2]; (unsigned long) p < list[3]; p++) {
        if (!*p) continue;
        if (((int (*)(unsigned long, int, int)) (shim_image_base() + FN_HAS_COLLECTIBLE))(*p, ITEM_SPIDER_MOD, 0))
            return 1;
    }
    return 0;
}

void shim_bossbars_draw(void) {
    unsigned long b = shim_image_base();
    if (!b || !shim_tweaks_mod(MOD_BARS) || !shim_eid_hud_showing() || shim_eid_seed_on(SEED_NO_HUD))
        return;
    unsigned long game = *(const unsigned long *) (b + GAME_PTR);
    unsigned long manager = *(const unsigned long *) (b + MANAGER_PTR);
    if (!game || !manager) return;
    unsigned long room = *(const unsigned long *) (game + GAME_ROOM);
    if (!room) return;
    int frame = *(const int *) (game + GAME_FRAME);
    if (frame != g_last_frame || room != g_last_room) {
        g_last_frame = frame;
        track(room, frame);
    }
    if (!g_bars) return;
    if (!shim_modset_int(MS_BARS_MEGA_SATAN) && *(const int *) (room + ROOM_BOSS_ID) == BOSS_MEGA_SATAN)
        return;
    if (!shim_modset_int(MS_BARS_SPIDER_MOD) && spider_mod_carried(game)) return;

    const float *canvas = (const float *) (b + DESIGN_CANVAS);
    float w = canvas[0], h = canvas[1];

    /* Gideon: only his bar, under the game's own, which keeps the wave counter */
    for (int k = 0; k < g_bars; k++) {
        if (g_boss[g_bar_list[k].boss].type != 907) continue;
        float off = *(const float *) (manager + MGR_HUD_OFFSET);
        draw_bar(&g_bar_list[k], w * 0.5f - 22.0f, h - 12.0f * off - 13.5f, 60, frame, 0, 1);
        return;
    }

    int position = shim_modset_int(MS_BARS_POSITION);
    int vertical = position == MS_POSITION_LEFT || position == MS_POSITION_RIGHT;
    const int *sizes = vertical ? SIZES_DOWN : SIZES_ACROSS;
    float padding = shim_modset_float(MS_BARS_PADDING);
    float edge = shim_modset_float(MS_BARS_SCREEN_PADDING);
    int per_row = shim_modset_int(MS_BARS_PER_ROW) - (vertical ? 1 : 0);
    if (per_row < 1) per_row = 1;
    float row_x = position == MS_POSITION_LEFT ? -ROW_STEP : position == MS_POSITION_RIGHT ? ROW_STEP : 0.0f;
    float row_y = position == MS_POSITION_TOP ? -ROW_STEP : position == MS_POSITION_BOTTOM ? ROW_STEP : 0.0f;

    int rows = (g_bars + per_row - 1) / per_row;
    for (int row = 0; row < rows; row++) {
        int in_row = g_bars - row * per_row;
        if (in_row > per_row) in_row = per_row;
        int size = sizes[(in_row < 10 ? in_row : 10) - 1];
        float x0, y0;
        switch (position) {
        case MS_POSITION_TOP:
            x0 = w * 0.5f - (float) (in_row * size) * 0.5f - (float) (in_row - 1) * padding;
            y0 = edge;
            break;
        case MS_POSITION_LEFT:
            x0 = edge * 2.0f;
            y0 = h * 0.5f + (float) (in_row * size) * 0.5f + (float) (in_row - 1) * padding;
            break;
        case MS_POSITION_RIGHT:
            x0 = w - edge * 2.0f;
            y0 = h * 0.5f + (float) (in_row * size) * 0.5f + (float) (in_row - 1) * padding;
            break;
        default:
            x0 = w * 0.5f - (float) (in_row * size) * 0.5f - (float) (in_row - 1) * padding;
            y0 = h - edge;
        }
        x0 += (float) (row - (rows - 1)) * row_x;
        y0 += (float) (row - (rows - 1)) * row_y;
        for (int slot = 0; slot < in_row; slot++) {
            float step = (float) slot * ((float) size + padding * 2.0f);
            float x = vertical ? x0 : x0 + step;
            float y = vertical ? y0 - step : y0;
            draw_bar(&g_bar_list[row * per_row + slot], x, y, size, frame, vertical, 0);
        }
    }
}

/* ---------------------------------------------------- the game's own bar */
static int g_patched;

/* between frames (mods.c): the game's bar is skipped exactly while the mod is on */
void shim_bossbars_between_frames(void) {
    int want = shim_tweaks_mod(MOD_BARS) != 0;
    if (want == g_patched) return;
    if (shim_patch_word(BAR_BRANCH, BAR_BRANCH_GAME, BAR_BRANCH_SKIP,
                        want ? BAR_BRANCH_SKIP : BAR_BRANCH_GAME)) {
        g_patched = want;
        LOG("bossbars: the game's boss bar is %s", want ? "skipped" : "back");
        if (shim_report_active()) report_line("bossbars", want ? "the game's bar is skipped" : "the game's bar is back");
    } else {
        g_patched = want;                   /* not the word we know: do not try again */
        LOG("bossbars: the game's boss bar branch was not recognised");
        if (shim_report_active()) report_line("bossbars", "the game's bar branch was NOT recognised");
    }
}
