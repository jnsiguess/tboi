/*
 * BAG OF CRAFTING: what the pickups around can be crafted into.
 *
 * The EID mod's crafting display (features/eid_bagofcrafting.lua) in its
 * default mode, "Recipe List", with its default settings (eid_config.lua):
 * shown while the bag is held up (the port's default, DisplayBagOfCrafting
 * "hold"; the mod's own is "always"), hidden while the room is fought, seven
 * results a page, the twelve most valuable ingredients combined exhaustively
 * and four hundred random pulls on top, sorted by quality and then by name.
 *
 *   #{{Collectible710}} Bag: 2{{Crafting8}}1{{Crafting12}}#
 *   Floor: 6{{Crafting8}}2{{Crafting15}} (+1{{Crafting21}})#
 *   (Scroll: Hold ... )
 *   #{{Collectible331}} {{ColorLavender}}={{CR}}3{{Crafting8}}...
 *
 * THE RECIPE IS THE GAME'S OWN. The mod has a Lua copy of the crafting
 * routine and uses the game's through REPENTOGON when it can; the port calls
 * the game's routine directly, 0x5551a8(unused, bag[8], 0) - read in full
 * before it was trusted: it seeds its own RNGs on the stack from the run's
 * start seed, reads the item pools without touching them and changes nothing
 * (see eid.c, bag_result). What is in the bag is read out of the player
 * (+0x2690, eight component bytes) and so is what a full bag makes (+0x2698,
 * the game fills it at 0x555140), which is REPENTOGON's path too.
 *
 * What the mod tracks it keeps as the mod does: the pickups of every room seen
 * on this floor, each room's list replaced when it is seen again, all of it
 * forgotten on a new floor; and the cards and pills the players carry.
 *
 * What is left out, and why:
 *   - the Hide and Preview hotkeys (F3, F4) and the search box - keyboard keys
 *     the port does not have; the controls line loses those clauses;
 *   - "Reset Bag" - it clears the mod's own guess of the bag, and the port
 *     reads the real bag;
 *   - blocking the player's controls while scrolling (ControlsCooldown).
 */
#include "modset.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "eid_doors.h"
#include "devices.h"
#include "bagswipe.h"
#include "diag.h"

unsigned long shim_image_base(void);
int shim_eid_readable(const void *p, unsigned long n);
const char *shim_eid_row(unsigned kind, unsigned id);
const char *shim_eid_name_of(unsigned kind, unsigned id);
int shim_eid_card_is_rune(int id);
unsigned long shim_eid_list_now(void);
int shim_eid_players(unsigned long *out, int max);
const char *shim_eid_language_now(void);
int shim_report_active(void);
void report_line(const char *kind, const char *text);

#define GAME_PTR          0xac3b90
#define MANAGER_PTR       0xac2428
#define MGR_ITEM_CONFIG   0x55f30
#define FN_COLLECTIBLE    0x73f7f4            /* (config, id) -> item */
#define FN_HAS_ITEM       0x4eeb74            /* (player, id, 0) */
#define FN_ACTION_HELD    0x671c5c            /* (0, action, controller) */
#define FN_BAG_RESULT     0x5551a8            /* (unused, bag[8], 0) -> item */
#define GAME_ROOM         0x21550             /* the Room object: ONE PER RUN, not per floor (below) */
#define ROOM_DESCRIPTOR   0x08                /* the room being played, one per room on a floor */
#define GAME_ROOM_TABLE   0x18                /* the level's room descriptors: 525 of 0x100 in Game */
#define ROOM_TABLE_BYTES  (525 * 0x100)
#define GAME_SEEDS        0x25d40
#define SEEDS_START       0x04
#define GAME_STAGE        0x0                 /* Level::Init: ldp w9, w8, [game] */
#define GAME_STAGE_TYPE   0x4
#define SEEDS_STAGE       0x18                /* the stage seeds in Seeds, fourteen */
#define STAGE_SEEDS       14
#define GAME_FRAME        0x1af34c            /* the game's frame count (Room::GetFrameCount, 0x5b3520) */
#define ROOM_LOADED       0x00                /* a byte: 0 - no room in the object (GetFrameCount says -1) */
#define ROOM_ENTERED      0x191c              /* the game frame the room was loaded at: Room::Init, 0x5a36e4 */
#define ROOM_FRESH        2                   /* frames: a room this young came with the floor (below) */
#define GAME_CHALLENGE    0x1cf438
#define CHALLENGE_CANTRIPPED 42

#define LIST_ARRAY        0x78                /* the room's entity list */
#define LIST_COUNT        0x84
#define ENT_TYPE          0x38
#define ENT_FLAGS         0x1b8
#define ENT_EXISTS        0x1c2
#define ENT_HP            0x354
#define ENT_SPRITE        0x50
#define SPRITE_ANIMATION  0x38
#define PICKUP_PRICE      0x564
#define FLAG_FRIENDLY     (1ull << 29)

#define PLAYER_TYPE       0x1738
#define PLAYER_CONTROLLER 0x19ec
#define PLAYER_POCKET     0x1c10              /* four { what, kind }: 0 pill, 1 card */
#define PLAYER_BAG        0x2690
#define PLAYER_BAG_MADE   0x2698
#define TYPE_TAINTED_CAIN 23

#define ITEM_BAG          710
#define ITEM_CRAFT_QUALITY 0xcc
#define MAX_ITEM          732

#define KIND_COLLECTIBLE  1
#define KIND_PHRASE       9
#define PHRASE_BAG        15
#define PHRASE_ROOM       16
#define PHRASE_FLOOR      17
#define PHRASE_MORE       18
#define PHRASE_CONTROLS   19

#define ACTION_SHOOT_LEFT  4
#define ACTION_SHOOT_RIGHT 5
#define ACTION_SHOOT_UP    6
#define ACTION_SHOOT_DOWN  7
#define ACTION_MAP         13
#define PLAYER_DEVICE_AT   0x19ec               /* the player's device: 0 the screen and keyboard */

/* the mod's defaults */
#define RESULTS           shim_modset_int(MS_EID_BAG_RESULTS)   /* BagOfCraftingResults, 7; a row */
/*
 * THE PAGE THAT FITS (the owner, 25.09.2026: a long list ran off the bottom of
 * the screen). The setting is the most a page shows; the page shows fewer
 * when the screen it is on has no room for them - measured by the panel's own
 * layout (shim_eid_text_height) against the screen's height below the panel,
 * every time the text is made. Paging steps by what is shown.
 */
static int g_page = 7;
float shim_eid_text_height(const char *markup);
float shim_eid_head_height(void);
float shim_eid_picture_line(void);
float shim_screen_height(void);
void shim_eid_panel_place(float *px, float *py);
#define PAGE_MARGIN       4.0f                /* units kept free under the panel */
#define COMBINATION_MAX   12                  /* BagOfCraftingCombinationMax */
#define RANDOM_RESULTS    400                 /* BagOfCraftingRandomResults */
#define CRUNCH_NS         3000000L            /* the mod yields after 5 ms; a frame here gets 3 */

#define INGREDIENTS       30                  /* component ids 1..29 */
#define MAX_QUERY         (8 * INGREDIENTS)
#define MAX_ROOMS         96
#define MAX_ROOM_ITEMS    48
#define MAX_RECIPES       2048

/* EID.BoC.PickupValues: what each ingredient is worth */
static const unsigned char VALUE[INGREDIENTS + 1] = {
    0, 1, 4, 5, 5, 5, 5, 1, 1, 3, 5, 8, 2, 7, 5, 2, 7, 10, 2, 4, 8, 2, 2, 4, 4, 2, 7, 7, 7, 0, 1,
};

/* EID.BoC.PickupIDLookup, "variant.subtype" -> one or two ingredients */
static const struct { short variant, subtype; unsigned char a, b; } LOOKUP[] = {
    { 10, 1, 1, 0 }, { 10, 2, 1, 0 }, { 10, 3, 2, 0 }, { 10, 4, 4, 0 }, { 10, 5, 1, 1 },
    { 10, 6, 3, 0 }, { 10, 7, 5, 0 }, { 10, 8, 2, 0 }, { 10, 9, 1, 0 }, { 10, 10, 2, 1 },
    { 10, 11, 6, 0 }, { 10, 12, 7, 0 },
    { 20, 1, 8, 0 }, { 20, 2, 9, 0 }, { 20, 3, 10, 0 }, { 20, 4, 8, 8 }, { 20, 5, 11, 0 },
    { 20, 6, 9, 0 }, { 20, 7, 26, 0 },
    { 30, 1, 12, 0 }, { 30, 2, 13, 0 }, { 30, 3, 12, 12 }, { 30, 4, 14, 0 },
    { 40, 1, 15, 0 }, { 40, 2, 15, 15 }, { 40, 4, 16, 0 }, { 40, 7, 17, 0 },
    { 42, 0, 29, 0 }, { 42, 1, 29, 0 },
    { 70, 14, 27, 0 }, { 70, 2062, 27, 0 },
    { 90, 1, 19, 0 }, { 90, 2, 18, 0 }, { 90, 3, 20, 0 }, { 90, 4, 28, 0 },
    { 300, 49, 24, 0 }, { 300, 50, 21, 0 }, { 300, 78, 25, 0 },
};

/* the colour of a result's "=", by its quality */
static const char *const QUALITY_COLOUR[5] = {
    "{{ColorSilver}}", "{{ColorLime}}", "{{ColorPastelBlue}}", "{{ColorLavender}}",
    "{{ColorLightOrange}}",
};

/* ----------------------------------------------------------- small things */
static int readable(unsigned long p, unsigned long n) { return p && shim_eid_readable((const void *) p, n); }

static const char *cxx_string(unsigned long s) {
    if (!readable(s, 24)) return "";
    const unsigned char *raw = (const unsigned char *) s;
    return (raw[23] & 0x80) ? *(const char *const *) raw : (const char *) raw;
}

static void add(char *out, size_t room, const char *s) {
    size_t at = strlen(out);
    if (s && at + 1 < room) snprintf(out + at, room - at, "%s", s);
}

/* EID:getBagOfCraftingID: the ingredients a pickup counts as, 0..2 of them */
static int ingredients_of(int variant, int subtype, unsigned char *out) {
    for (unsigned i = 0; i < sizeof(LOOKUP) / sizeof(LOOKUP[0]); i++) {
        if (LOOKUP[i].variant != variant || LOOKUP[i].subtype != subtype) continue;
        out[0] = LOOKUP[i].a;
        out[1] = LOOKUP[i].b;
        return LOOKUP[i].b ? 2 : 1;
    }
    if (variant == 300) {
        if (subtype == 0) return 0;
        out[0] = shim_eid_card_is_rune(subtype) ? 23 : 21;
        return 1;
    }
    if (variant == 70) {
        if (subtype == 0) return 0;
        out[0] = 22;
        return 1;
    }
    return 0;
}

/* qualitySort: the more valuable first, then the higher id */
static int by_value(const void *pa, const void *pb) {
    unsigned char a = *(const unsigned char *) pa, b = *(const unsigned char *) pb;
    if (VALUE[a] != VALUE[b]) return VALUE[a] > VALUE[b] ? -1 : 1;
    return a > b ? -1 : (a < b ? 1 : 0);
}

static int ascending(const void *pa, const void *pb) {
    return *(const unsigned char *) pa - *(const unsigned char *) pb;
}

static long now_ns(void) {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec * 1000000000L + t.tv_nsec;
}

/* --------------------------------------------------------- the game's side */
static unsigned long item_config(unsigned long b, int id) {
    unsigned long manager = *(const unsigned long *) (b + MANAGER_PTR);
    if (!manager) return 0;
    return ((unsigned long (*)(unsigned long, int)) (b + FN_COLLECTIBLE))(manager + MGR_ITEM_CONFIG, id);
}

/* CraftingItemQualities: the item's craft quality, which the loader sets to
   its quality when items.xml gives none (0x73915c) */
static int craft_quality(unsigned long b, int id) {
    unsigned long item = item_config(b, id);
    if (!readable(item, ITEM_CRAFT_QUALITY + 4)) return -1;
    int q = *(const int *) (item + ITEM_CRAFT_QUALITY);
    return (q < 0 || q > 4) ? -1 : q;
}

/* calculateBagOfCrafting: the game's own answer for eight ingredients */
static int recipe_of(unsigned long b, const unsigned char *eight) {
    unsigned char bag[8];
    memcpy(bag, eight, 8);
    qsort(bag, 8, 1, ascending);             /* the mod sorts before asking too */
    for (int i = 0; i < 8; i++) if (!bag[i] || bag[i] > INGREDIENTS) return 0;
    int made = ((int (*)(void *, const unsigned char *, int)) (b + FN_BAG_RESULT))(NULL, bag, 0);
    return (made > 0 && made <= MAX_ITEM) ? made : 0;
}

static int action_held(unsigned long b, unsigned long player, int action) {
    int controller = *(const int *) (player + PLAYER_CONTROLLER);
    return ((int (*)(unsigned long, int, int)) (b + FN_ACTION_HELD))(0, action, controller) & 1;
}

/* Isaac.CountEnemies() + CountBosses() > 0, read the game's way: a hostile
   enemy is alive and it is one that keeps the doors shut */
static int in_battle(unsigned long list) {
    if (!readable(list + LIST_ARRAY, 16)) return 0;
    const unsigned long *items = *(const unsigned long *const *) (list + LIST_ARRAY);
    unsigned n = *(const unsigned *) (list + LIST_COUNT);
    if (!items || n > 4096 || !readable((unsigned long) items, (unsigned long) n * 8)) return 0;
    for (unsigned i = 0; i < n; i++) {
        unsigned long e = items[i];
        if (!readable(e, ENT_HP + 4) || !*(const unsigned char *) (e + ENT_EXISTS)) continue;
        const int *tvs = (const int *) (e + ENT_TYPE);
        if (tvs[0] < 10 || tvs[0] >= 1000) continue;
        if (*(const unsigned long long *) (e + ENT_FLAGS) & FLAG_FRIENDLY) continue;
        if (!(*(const float *) (e + ENT_HP) > 0.0f)) continue;
        int open = 0;
        for (unsigned k = 0; k < sizeof(EID_OPEN_DOORS) / sizeof(EID_OPEN_DOORS[0]); k++)
            if (EID_OPEN_DOORS[k].type == tvs[0] && EID_OPEN_DOORS[k].variant == tvs[1]) { open = 1; break; }
        if (!open) return 1;
    }
    return 0;
}

/* for the description panel's HideInBattle (eid.c) */
int shim_eid_craft_in_battle(void) {
    unsigned long list = shim_eid_list_now();
    return list && in_battle(list);
}

static int collecting(unsigned long e) {
    unsigned long sprite = e + ENT_SPRITE;
    if (!readable(sprite + SPRITE_ANIMATION, 8)) return 0;
    unsigned long anim = *(const unsigned long *) (sprite + SPRITE_ANIMATION);
    return anim && !strcmp(cxx_string(anim), "Collect");
}

/* ------------------------------------------------------------- the floor */
/*
 * WHICH FLOOR THIS IS (the 02.10.2026 report: on a new floor the bag's screen
 * kept the old floor's pickups and its recipes froze). A new floor used to be
 * told by a new Room object at Game+0x21550 - but there is one Room object per
 * RUN: the image writes that field in three places only, the Game's
 * constructor, 0x6cb9d0 (called from the constructor and from the run's reset
 * 0x88d7ac) and the Game's destruction. So the rooms of every floor piled up
 * until the table was full, and then nothing new was recorded at all.
 *
 * Now the floor is known the way the game itself decides which floor to build.
 * Level::Init (0x6d7d40) reads the stage and its type (Game+0, +4), takes the
 * alternate path for types 4 and 5 (Level::IsAltPath, 0x6d0c70) and seeds the
 * level from Seeds+0x18 + 4 * (stage + alt) - THIS floor's seed (0x6d7ebc..
 * 0x6d7f00). Every way the image re-creates a floor in place (Forget Me Now
 * and its kin, R Key) forgets exactly that seed (Seeds::ForgetStageSeed,
 * 0x5724a4), so the key below changes on each of them too, even when the
 * stage and its type stay. The words are checked once; if they are not what
 * was measured, the key is the start seed, the stage and the type alone.
 *
 * A changed key: every room is forgotten and the recipes are worked out anew -
 * unless the list is locked, which the mod keeps across floors as well; a new
 * start seed (a new run) takes the lock off, as the mod does.
 *
 * THE KEY CAN CHANGE BEFORE THE ROOM DOES (the 03.10.2026 report: after a
 * floor change the recipes were the old floor's). The stage and its type are
 * set by SetStage (0x6daa44), and the level is built by Level::Init later. On
 * the game's own descent the two stand together (0x88b450 -> 0x88b458,
 * 0x88c4a0 -> 0x88c4a8), but the stage transition can set the stage at its
 * start (0x89a23c, 0x89a664), and the port's floor teleport does (SetStage,
 * then 0x88e42c): for the whole transition the key is the new floor's while
 * the Room object still holds the old room with its pickups. Read in that
 * window, the old room was recorded as a room of the new floor - and when the
 * level was built nothing changed the key again, so it stayed, and the "new"
 * recipes were made of the old floor's pickups. The mod cannot be fooled so:
 * it forgets on MC_POST_NEW_LEVEL, after the level is built and its first room
 * entered, and reads no room in its first two frames (GetFrameCount() < 2).
 *
 * So the room's own age decides. Room::GetFrameCount (0x5b3520) is the game's
 * frame (Game+0x1af34c) less the frame the room was loaded at (Room+0x191c,
 * written by Room::Init 0x5a36e4 and nothing else). A room that came with the
 * new key is a frame or two old when the key is first seen - Level::Init and
 * the start room's load run in the same update. A room older than that was
 * loaded before the floor changed: it is the old floor's, and no room is
 * recorded until the Room object loads another one. The watch runs on every
 * call, before anything decides whether the screen is shown, so the age is
 * measured on the frame the key changes and not whenever the bag is next up.
 */
typedef struct {
    unsigned long key;                      /* the room's descriptor */
    int n;
    unsigned serial;                        /* the floor it was seen on (g_floor_serial) */
    int entered;                            /* the game frame the room was loaded at (Room+0x191c) */
    unsigned char items[MAX_ROOM_ITEMS];
} room_t;

typedef struct { unsigned start; int stage, type; unsigned seed; } floor_key_t;

static room_t g_rooms[MAX_ROOMS];
static int g_room_count;
static unsigned g_floor_serial;             /* grows with every floor seen */
static floor_key_t g_floor;                 /* the floor the rooms above belong to */
static int g_floor_known;
static int g_held_back;                     /* the room on screen is the previous floor's */
static int g_held_entered;                  /* ... loaded at this game frame */
static int g_floor_began;                   /* the game frame the floor's first room was loaded at */
#if ISAAC_DIAG
#define FLOOR_LINES       20                  /* floors reported a launch */
static int g_floor_said;
static int g_floor_recompute_said = 1;      /* 0: a new floor waits for its first list */
static int g_held_frame;                    /* the game frame the key changed at, held back */
#endif

/* the Room object: is a room loaded in it, and at which game frame */
static int room_entered(unsigned long game, int *entered) {
    unsigned long room = *(const unsigned long *) (game + GAME_ROOM);
    if (!readable(room + ROOM_ENTERED, 4) || !*(const unsigned char *) (room + ROOM_LOADED)) return 0;
    *entered = *(const int *) (room + ROOM_ENTERED);
    return 1;
}

/* Level::Init's own reading of the floor, word by word (see above) */
static const struct { unsigned at, word; } LEVEL_INIT_WORDS[] = {
    { 0x6d7ebc, 0x29402269u },               /* ldp  w9, w8, [x19]        stage, type */
    { 0x6d7ec0, 0x121f7908u },               /* and  w8, w8, #~1 */
    { 0x6d7ec4, 0x7100111fu },               /* cmp  w8, #4                types 4, 5: the alt path */
    { 0x6d7ec8, 0x1a891539u },               /* cinc w25, w9, eq           index = stage + alt */
    { 0x6d7ef8, 0x528bab09u },               /* mov  w9, #0x5d58 */
    { 0x6d7efc, 0x72a00049u },               /* movk w9, #2, lsl #16      Seeds + 0x18 */
    { 0x6d7f00, 0xb8696915u },               /* ldr  w21, [x8, x9]        this floor's seed */
};

static int stage_seed_words(unsigned long b) {
    static int known = -1;
    if (known >= 0) return known;
    known = 1;
    for (unsigned i = 0; i < sizeof(LEVEL_INIT_WORDS) / sizeof(LEVEL_INIT_WORDS[0]); i++)
        if (*(const unsigned *) (b + LEVEL_INIT_WORDS[i].at) != LEVEL_INIT_WORDS[i].word) known = 0;
#if ISAAC_DIAG
    if (shim_report_active())
        report_line("craft self-check", known
            ? "Level::Init reads the stage seed where expected - the floor key holds it"
            : "Level::Init does not read the stage seed where expected - the floor key is the start seed, stage and type");
#endif
    return known;
}

static void floor_key(unsigned long b, unsigned long game, floor_key_t *k) {
    memset(k, 0, sizeof(*k));
    k->start = *(const unsigned *) (game + GAME_SEEDS + SEEDS_START);
    k->stage = *(const int *) (game + GAME_STAGE);
    k->type = *(const int *) (game + GAME_STAGE_TYPE);
    if (!stage_seed_words(b)) return;
    int i = k->stage + ((k->type & ~1) == 4);
    if (i >= 0 && i < STAGE_SEEDS)
        k->seed = *(const unsigned *) (game + GAME_SEEDS + SEEDS_STAGE + 4 * (unsigned) i);
}

/* EID:BoCGetRoomPickupList, and the room's entry in RoomQueries */
static const room_t *this_room(unsigned long b, unsigned long game, unsigned long list) {
    (void) b;
    int entered;
    if (g_held_back || !room_entered(game, &entered)) return NULL;  /* the previous floor's room */
    unsigned long room = *(const unsigned long *) (game + GAME_ROOM);
    if (!readable(room + ROOM_DESCRIPTOR, 8)) return NULL;
    unsigned long key = *(const unsigned long *) (room + ROOM_DESCRIPTOR);
    room_t *r = NULL;
    for (int i = 0; i < g_room_count; i++) if (g_rooms[i].key == key) r = &g_rooms[i];
    if (!r) {
        if (g_room_count >= MAX_ROOMS) {
#if ISAAC_DIAG
            static unsigned said_full;
            if (shim_report_active() && said_full != g_floor_serial) {
                said_full = g_floor_serial;
                char line[160];
                snprintf(line, sizeof(line), "the room table is FULL (%d) on floor %d/%d - room %lx not recorded",
                         MAX_ROOMS, g_floor.stage, g_floor.type, key);
                report_line("craft", line);
            }
#endif
            return NULL;
        }
        r = &g_rooms[g_room_count++];
        r->key = key;
        r->serial = g_floor_serial;
#if ISAAC_DIAG
        /* the key is a place in the level's room table (Game+0x18, 525 of 0x100) */
        unsigned long off = key - game - GAME_ROOM_TABLE;
        static int said_key;
        if (shim_report_active() && (key < game + GAME_ROOM_TABLE || off % 0x100 || off >= ROOM_TABLE_BYTES)
            && said_key < 5) {
            said_key++;
            char line[160];
            snprintf(line, sizeof(line), "room key %lx outside the level's room table (Game %lx)", key, game);
            report_line("craft self-check", line);
        }
        static int said;
        if (shim_report_active() && said < 30) {
            said++;
            char line[120];
            snprintf(line, sizeof(line), "a room not seen on this floor: key %lx, %d rooms known",
                     key, g_room_count);
            report_line("craft", line);
        }
#endif
    }
    r->n = 0;
    r->entered = entered;
    if (!readable(list + LIST_ARRAY, 16)) return r;
    const unsigned long *items = *(const unsigned long *const *) (list + LIST_ARRAY);
    unsigned n = *(const unsigned *) (list + LIST_COUNT);
    if (!items || n > 4096 || !readable((unsigned long) items, (unsigned long) n * 8)) return r;
    for (unsigned i = 0; i < n; i++) {
        unsigned long e = items[i];
        if (!readable(e, PICKUP_PRICE + 4) || !*(const unsigned char *) (e + ENT_EXISTS)) continue;
        const int *tvs = (const int *) (e + ENT_TYPE);
        if (tvs[0] != 5) continue;
        unsigned char got[2];
        int k = ingredients_of(tvs[1], tvs[2], got);
        if (!k || *(const int *) (e + PICKUP_PRICE) != 0 || collecting(e)) continue;
        for (int j = 0; j < k && r->n < MAX_ROOM_ITEMS; j++) r->items[r->n++] = got[j];
    }
    return r;
}

/* ------------------------------------------------------------ the recipes */
typedef struct { unsigned char ing[8]; short made; } recipe_t;

typedef struct {
    unsigned hash;                          /* of the ingredient query */
    int count;
    recipe_t list[MAX_RECIPES];
    /* the crunch in progress: candidates made, evaluated up to `done` */
    int pending;                            /* candidates not yet evaluated start here */
} results_t;

static results_t g_results;                 /* the query shown now */
static unsigned g_results_gen, g_sorted_gen; /* every change of g_results and g_sorted (filtered_now) */
static results_t g_work;                    /* the query being worked out */
static int g_work_busy;
static unsigned g_locked;                   /* lockedResults: a query's hash, or 0 */
static int g_offset;                        /* bagOfCraftingOffset */
static int g_refreshes;

static unsigned hash_of(const unsigned char *q, int n) {
    unsigned h = 2166136261u;
    for (int i = 0; i < n; i++) { h ^= q[i]; h *= 16777619u; }
    return h ? h : 1;
}

static int have_candidate(const results_t *r, const unsigned char *eight) {
    for (int i = 0; i < r->count; i++) if (!memcmp(r->list[i].ing, eight, 8)) return 1;
    return 0;
}

static void candidate(results_t *r, const unsigned char *eight) {
    if (r->count >= MAX_RECIPES || have_candidate(r, eight)) return;
    memcpy(r->list[r->count].ing, eight, 8);
    r->list[r->count].made = -1;            /* not asked yet */
    r->count++;
}

/* combinations(): every eight of the most valuable, in their order */
static void combine(results_t *r, const unsigned char *from, int n, int start, int left,
                    unsigned char *built) {
    if (!left) { candidate(r, built); return; }
    for (int i = start; i <= n - left; i++) {
        built[8 - left] = from[i];
        combine(r, from, n, i + 1, left - 1, built);
    }
}

static unsigned g_random = 0x77777770u;
static unsigned next_random(void) {
    g_random ^= g_random << 13; g_random ^= g_random >> 17; g_random ^= g_random << 5;
    return g_random;
}

/* RecipeCrunchCoroutine, the part that decides what to ask */
static void start_crunch(const unsigned char *query, int n, unsigned hash, int refresh) {
    if (!refresh || g_work.hash != hash) {
        memset(&g_work, 0, sizeof(g_work));
        g_work.hash = hash;
        if (g_results.hash == hash) {       /* build on the list already known */
            memcpy(&g_work, &g_results, sizeof(g_work));
        }
    }
    unsigned char most[COMBINATION_MAX + 2];
    int m = 0, skip_random = 0;
    if (refresh) {
        g_refreshes++;
        if (n >= COMBINATION_MAX + g_refreshes)
            for (int i = 0; i < COMBINATION_MAX; i++) most[m++] = query[i + g_refreshes];
    } else if (n <= COMBINATION_MAX) {
        memcpy(most, query, (size_t) n);
        m = n;
        skip_random = 1;
    } else {
        memcpy(most, query, COMBINATION_MAX);
        m = COMBINATION_MAX;
    }
    unsigned char built[8];
    if (m >= 8) combine(&g_work, most, m, 0, 8, built);
    if (!skip_random) {
        g_random ^= hash + (unsigned) g_refreshes * 0x9e3779b9u;
        for (int i = 0; i <= RANDOM_RESULTS; i++) {
            unsigned char copy[MAX_QUERY], pick[8];
            int left = n;
            memcpy(copy, query, (size_t) n);
            for (int k = 0; k < 8; k++) {
                int at = (int) (next_random() % (unsigned) left);
                pick[k] = copy[at];
                memmove(copy + at, copy + at + 1, (size_t) (left - at - 1));
                left--;
            }
            qsort(pick, 8, 1, by_value);
            candidate(&g_work, pick);
        }
    }
    g_work_busy = 1;
}

/* and the part that asks, a few milliseconds a frame */
static void crunch(unsigned long b) {
    if (!g_work_busy) return;
    long until = now_ns() + CRUNCH_NS;
    while (g_work.pending < g_work.count) {
        recipe_t *r = &g_work.list[g_work.pending++];
        if (r->made < 0) r->made = (short) recipe_of(b, r->ing);
        if (now_ns() > until) return;
    }
    memcpy(&g_results, &g_work, sizeof(g_results));
    g_results_gen++;                        /* the filtered list is made again (filtered_now) */
    g_work_busy = 0;
}

/* ----------------------------------------------------- the list, in order */
static short g_sorted[MAX_ITEM + 1];        /* sortedIDs */
static signed char g_quality[MAX_ITEM + 1];
static int g_sorted_n;
static char g_sorted_lang[16];

static int g_sort_mode;                    /* BagOfCraftingSortOrder, a row of EID's page */

static int by_quality_then_name(const void *pa, const void *pb) {
    int a = *(const short *) pa, b = *(const short *) pb;
    if (g_sort_mode == MS_SORT_QUALITY && g_quality[a] != g_quality[b])
        return g_quality[a] > g_quality[b] ? -1 : 1;
    const char *na = shim_eid_name_of(KIND_COLLECTIBLE, (unsigned) a);
    const char *nb = shim_eid_name_of(KIND_COLLECTIBLE, (unsigned) b);
    return strcmp(na ? na : "", nb ? nb : "");
}

static void sort_items(unsigned long b) {
    const char *lang = shim_eid_language_now();
    int mode = shim_modset_int(MS_EID_BAG_SORT);
    if (g_sorted_n && !strcmp(g_sorted_lang, lang ? lang : "") && mode == g_sort_mode) return;
    g_sort_mode = mode;
    snprintf(g_sorted_lang, sizeof(g_sorted_lang), "%s", lang ? lang : "");
    g_sorted_n = 0;
    for (int id = 1; id <= MAX_ITEM; id++) {
        g_quality[id] = (signed char) craft_quality(b, id);
        if (g_quality[id] >= 0) g_sorted[g_sorted_n++] = (short) id;
    }
    qsort(g_sorted, (size_t) g_sorted_n, sizeof(short), by_quality_then_name);
    g_sorted_gen++;
}

/* bagContainsCount: how many of the bag's ingredients the recipe uses */
static int bag_used(const unsigned char *recipe, const unsigned char *bag, int bag_n) {
    int left[INGREDIENTS + 1] = { 0 };
    for (int i = 0; i < 8; i++) left[recipe[i]]++;
    int used = 0;
    for (int i = 0; i < bag_n; i++) if (left[bag[i]] > 0) { left[bag[i]]--; used++; }
    return used;
}

/* -- the filtered list (tools/test_perf172 cuts this block out and tests it) -- */
/*
 * filteredRecipesList, MADE ONCE (hotfix 1.72, perf item 4). The list on the
 * screen is the recipes in the order of the sorted items, each item's recipes
 * in the order they were found, and without Tainted Cain only those that use
 * the whole bag. It was worked out on every frame the bag was shown, twice -
 * once to count, once to draw: g_sorted_n (~720) times every recipe (up to
 * 2048), up to 1.5 million comparisons a frame, 2-6 ms on a weak phone. Now it
 * is made when something it depends on changes - the recipes (g_results_gen),
 * the order (g_sorted_gen), the bag, Tainted Cain - and in one pass over the
 * recipes and a sort by each item's place, not a pass per item.
 */
static short g_filtered[MAX_RECIPES];      /* indices into g_results.list, in screen order */
static int g_filtered_n;
static struct {
    int valid;
    unsigned results_gen, sorted_gen;
    unsigned char bag[8];
    int bag_n, tcain;
} g_filtered_key;
static short g_rank[MAX_ITEM + 1];          /* an item's place in g_sorted, -1 when it is not there */
static unsigned g_rank_gen;
static const recipe_t *g_filter_list;       /* for the sort below */

static int by_rank_then_found(const void *pa, const void *pb) {
    int a = *(const short *) pa, b = *(const short *) pb;
    int ra = g_rank[g_filter_list[a].made], rb = g_rank[g_filter_list[b].made];
    if (ra != rb) return ra < rb ? -1 : 1;
    return a < b ? -1 : a > b;
}

static void filtered_build(const results_t *res, const unsigned char *bag, int bag_n, int tcain) {
    if (!g_rank_gen || g_rank_gen != g_sorted_gen) {
        for (int id = 0; id <= MAX_ITEM; id++) g_rank[id] = -1;
        for (int s = 0; s < g_sorted_n; s++) g_rank[g_sorted[s]] = (short) s;
        g_rank_gen = g_sorted_gen ? g_sorted_gen : 1;
    }
    g_filtered_n = 0;
    for (int i = 0; i < res->count && i < MAX_RECIPES; i++) {
        const recipe_t *r = &res->list[i];
        if (r->made < 0 || r->made > MAX_ITEM || g_rank[r->made] < 0) continue;
        if (!tcain && bag_used(r->ing, bag, bag_n) != bag_n) continue;
        g_filtered[g_filtered_n++] = (short) i;
    }
    g_filter_list = res->list;
    qsort(g_filtered, (size_t) g_filtered_n, sizeof(short), by_rank_then_found);
}

/* the list for this frame: made again only when its inputs changed */
static int filtered_now(const results_t *res, const unsigned char *bag, int bag_n, int tcain) {
    if (!g_filtered_key.valid || g_filtered_key.results_gen != g_results_gen
        || g_filtered_key.sorted_gen != g_sorted_gen || g_filtered_key.bag_n != bag_n
        || g_filtered_key.tcain != tcain || memcmp(g_filtered_key.bag, bag, (size_t) bag_n)) {
        filtered_build(res, bag, bag_n, tcain);
        g_filtered_key.valid = 1;
        g_filtered_key.results_gen = g_results_gen;
        g_filtered_key.sorted_gen = g_sorted_gen;
        g_filtered_key.bag_n = bag_n;
        g_filtered_key.tcain = tcain;
        memcpy(g_filtered_key.bag, bag, (size_t) bag_n);
    }
    return g_filtered_n;
}
/* -- end of the filtered list -- */

/* tableToCraftingIconsMerged: "3{{Crafting8}}{{CR}}...", coloured against the bag */
static void icons(char *out, size_t room, const unsigned char *list, int n,
                  const unsigned char *bag, int bag_n, int mark) {
    int count[INGREDIENTS + 1] = { 0 }, in_bag[INGREDIENTS + 1] = { 0 };
    for (int i = 0; i < n; i++) if (list[i] <= INGREDIENTS) count[list[i]]++;
    for (int i = 0; i < bag_n; i++) if (bag[i] <= INGREDIENTS) in_bag[bag[i]]++;
    int first = 1;
    for (int id = 1; id < INGREDIENTS; id++) {
        if (!count[id]) continue;
        const char *colour = "";
        if (mark && in_bag[id]) {
            if (in_bag[id] == count[id]) colour = "{{ColorBagComplete}}";
            else if (in_bag[id] > count[id]) colour = "{{ColorBagOverfill}}";
        }
        char piece[64];
        /* each count with its picture is a word: a long line breaks between them (the
           mod runs them together; the gap after a picture is the same either way) */
        snprintf(piece, sizeof(piece), "%s%s%d{{Crafting%d}}{{CR}}", first ? "" : " ", colour, count[id], id);
        add(out, room, piece);
        first = 0;
    }
}

/*
 * THE LINE FOR A FINGER (the owner's eighth round, 24.09.2026: the line named
 * keyboard keys, and on the touch screen nothing pages the bag - holding the
 * map opens the inventory). A sideways swipe on the bag's screen turns the
 * page - right the next, left the previous, round the ends (07.10.2026) - and a hold
 * locks the list (the owner, 06.10.2026; until then a tap was the next page,
 * round to the first, and there was no way back). items.c takes the finger,
 * bagswipe.h tells the gestures apart; the words are ours, one line per
 * language the game has, English for the rest.
 */
static const struct { const char *lang, *line; } TOUCH_LINE[] = {
    { "en_us", "(Swipe: turn pages, hold: lock)" },
    { "ru",    "(\u0421\u0432\u0430\u0439\u043f: \u043b\u0438\u0441\u0442\u0430\u0442\u044c, "
               "\u0443\u0434\u0435\u0440\u0436\u0430\u043d\u0438\u0435: \u0437\u0430\u043a\u0440\u0435\u043f\u0438\u0442\u044c)" },
    { "de",    "(Wischen: bl\u00e4ttern, halten: fixieren)" },
    { "spa",   "(Desliza: pasar p\u00e1gina, mant\u00e9n: fijar)" },
    { "fr",    "(Glisser : tourner les pages, maintenir : verrouiller)" },
    { "ja_jp", "(\u30b9\u30ef\u30a4\u30d7: \u30da\u30fc\u30b8\u3092\u3081\u304f\u308b\u3001\u9577\u62bc\u3057: \u56fa\u5b9a)" },
    { "ko_kr", "(\uc2a4\uc640\uc774\ud504: \ud398\uc774\uc9c0 \ub118\uae30\uae30, \uae38\uac8c: \uace0\uc815)" },
    { "zh_cn", "(\u6ed1\u52a8: \u7ffb\u9875, \u957f\u6309: \u9501\u5b9a)" },
};

/*
 * THE LINE FOR A PAD (the owner's ninth round, 24.09.2026). The mod's keys hang
 * on holding the map button, and on the phone holding it is the inventory's:
 * two actions on one button, and they exclude each other. So a pad pages the
 * bag with the sticks' clicks, which the game leaves unused - a tap of the
 * right one the next page (held, more recipes), of the left one the previous
 * (held a second, the lock; the owner, 07.10.2026) - and Select stays the
 * inventory's alone. The markers are the mod's own pad pictures.
 */
static const struct { const char *lang, *line; } PAD_LINE[] = {
    { "en_us", "(Pages: {{ButtonLStick}} / {{ButtonRStick}}, lock: hold {{ButtonLStick}}, more: hold {{ButtonRStick}})" },
    { "ru",    "(\u0421\u0442\u0440\u0430\u043d\u0438\u0446\u044b: {{ButtonLStick}} / {{ButtonRStick}}, \u0437\u0430\u043c\u043e\u043a: \u0434\u0435\u0440\u0436\u0430\u0442\u044c {{ButtonLStick}}, \u0435\u0449\u0451: \u0434\u0435\u0440\u0436\u0430\u0442\u044c {{ButtonRStick}})" },
    { "de",    "(Seiten: {{ButtonLStick}} / {{ButtonRStick}}, Sperre: {{ButtonLStick}} halten, mehr: {{ButtonRStick}} halten)" },
    { "spa",   "(P\u00e1ginas: {{ButtonLStick}} / {{ButtonRStick}}, fijar: mant\u00e9n {{ButtonLStick}}, m\u00e1s: mant\u00e9n {{ButtonRStick}})" },
    { "fr",    "(Pages : {{ButtonLStick}} / {{ButtonRStick}}, verrou : maintenir {{ButtonLStick}}, plus : maintenir {{ButtonRStick}})" },
    { "ja_jp", "(\u30da\u30fc\u30b8: {{ButtonLStick}} / {{ButtonRStick}}\u3001\u56fa\u5b9a: {{ButtonLStick}}\u9577\u62bc\u3057\u3001\u8ffd\u52a0: {{ButtonRStick}}\u9577\u62bc\u3057)" },
    { "ko_kr", "(\ud398\uc774\uc9c0: {{ButtonLStick}} / {{ButtonRStick}}, \uace0\uc815: {{ButtonLStick}} \uae38\uac8c, \ub354 \ubcf4\uae30: {{ButtonRStick}} \uae38\uac8c)" },
    { "zh_cn", "(\u7ffb\u9875: {{ButtonLStick}} / {{ButtonRStick}}, \u9501\u5b9a: \u957f\u6309{{ButtonLStick}}, \u66f4\u591a: \u957f\u6309{{ButtonRStick}})" },
};

int shim_input_keys_last(void);
unsigned shim_pad_buttons(void);
unsigned shim_pad_slot_buttons(int slot);
static unsigned long g_bag_player;         /* set by the display below */

static const char *pad_line(void) {
    const char *lang = shim_eid_language_now();
    for (unsigned i = 0; i < sizeof(PAD_LINE) / sizeof(PAD_LINE[0]); i++)
        if (lang && !strcmp(lang, PAD_LINE[i].lang)) return PAD_LINE[i].line;
    return PAD_LINE[0].line;
}

/*
 * The player plays on a pad: his device is of the pad's class (devices.c,
 * batch 2 U4). It used to be "anything but 0", and 0 is the keyboard while
 * the touch screen is 0x10dead: a finger got the pad's line.
 */
static int by_pad(unsigned long player) {
    if (!player || !readable(player + PLAYER_DEVICE_AT, 4)) return 0;
    return shim_device_kind(*(const int *) (player + PLAYER_DEVICE_AT)) == DEVICE_PAD;
}

static const char *touch_line(void) {
    const char *lang = shim_eid_language_now();
    for (unsigned i = 0; i < sizeof(TOUCH_LINE) / sizeof(TOUCH_LINE[0]); i++)
        if (lang && !strcmp(lang, TOUCH_LINE[i].lang)) return TOUCH_LINE[i].line;
    return TOUCH_LINE[0].line;
}

/* the player plays by finger: his device is the touch screen's (devices.c; 0 is the keyboard) */
static int by_finger(unsigned long player) {
    if (!player || !readable(player + PLAYER_DEVICE_AT, 4)) return 0;
    return shim_device_kind(*(const int *) (player + PLAYER_DEVICE_AT)) == DEVICE_SCREEN;
}

/*
 * "Bag:", "Room:", "Floor:" and then the counts: the mod runs them together
 * ("Room:17{{Crafting..}}"); the owner (25.09.2026) wants the space a colon
 * has in a sentence, so it is put in when the language's own word lacks it.
 */
static void add_label(char *out, size_t room, const char *label) {
    if (!label) return;
    add(out, room, label);
    size_t n = strlen(label);
    if (n && label[n - 1] != ' ') add(out, room, " ");
}

/* the mod's controls line, without the clauses for keys the port has not got */
static void controls_line(char *out, size_t room) {
    if (by_finger(g_bag_player)) { add(out, room, touch_line()); return; }
    if (by_pad(g_bag_player)) { add(out, room, pad_line()); return; }
    const char *said = shim_eid_row(KIND_PHRASE, PHRASE_CONTROLS);
    if (!said || !*said) return;
    char text[400], kept[400] = "";
    snprintf(text, sizeof(text), "%s", said);
    /* {{CONFIG_BoC_Toggle}} is the map button's icon: Select on a pad */
    char *cfg = strstr(text, "{{CONFIG_BoC_Toggle}}");
    if (cfg) {
        char rest[400];
        snprintf(rest, sizeof(rest), "%s", cfg + strlen("{{CONFIG_BoC_Toggle}}"));
        snprintf(cfg, sizeof(text) - (size_t) (cfg - text), "{{ButtonSelect}}%s", rest);
    }
    for (char *part = strtok(text, ","); part; part = strtok(NULL, ",")) {
        if (strstr(part, "{1}") || strstr(part, "{{ButtonRB}}")) {
            if (strchr(part, ')') && kept[0]) add(kept, sizeof(kept), ")");
            continue;
        }
        if (kept[0]) add(kept, sizeof(kept), ",");
        add(kept, sizeof(kept), part);
    }
    add(out, room, kept);
}

/* ------------------------------------------------------ the pad's controls */
/*
 * Hold the map button, then: shoot down / up a page, shoot left locks the list
 * where it is, shoot right asks for more recipes (a refresh). The mod's keys,
 * read through the game's own actions, so a pad, a keyboard and the touch
 * sticks all work alike.
 */
static const int PAD_ACTION[4] = { ACTION_SHOOT_DOWN, ACTION_SHOOT_UP, ACTION_SHOOT_LEFT, ACTION_SHOOT_RIGHT };
static int g_was_held[4];
static long g_repeat_at[2];
static int g_refresh_wanted;

/* a finger's asks, from the touch thread (items.c); carried out here, between frames */
static volatile int g_touch_turn, g_touch_lock;
void shim_eid_craft_touch_turn(int direction) { g_touch_turn = direction > 0 ? 1 : direction < 0 ? -1 : 0; }
void shim_eid_craft_touch_lock(void) { g_touch_lock = 1; }

/*
 * A swipe's page, +1 the next and -1 the previous, round the ends as the
 * sticks' clicks (bagswipe.h, bagswipe_round: pages are as long as fit, so the
 * starts walked through are kept to walk back by).
 */
static bagpages_t g_pages;

int shim_items_open(void);

#define PAD_L3            (1u << 8)             /* pad.c: BTN_L3, BTN_R3 */
#define PAD_R3            (1u << 9)
#define STICK_HOLD_NS     500000000L            /* a right click held this long: more recipes */
#define LOCK_HOLD_NS      1000000000L           /* a left click held this long: the lock (the owner, 07.10.2026) */

static void pad(unsigned long b, unsigned long player, int total) {
    int turn = g_touch_turn;
    if (turn) { g_touch_turn = 0; g_offset = bagswipe_round(&g_pages, g_offset, g_page, total, turn); }
    if (g_touch_lock) { g_touch_lock = 0; g_locked = g_locked ? 0 : g_results.hash; }
    /*
     * The sticks' clicks (the owner, 07.10.2026): a tap of R3 the next page and
     * of L3 the previous, both round the ends, as the descriptions' pages
     * (eid.c); R3 held half a second more recipes, as before; L3 held a second
     * the lock - it used to be L3's press, which left no way back.
     */
    if (by_pad(player)) {
        static stickclick_t l3, r3;
        /* the bag player's own pad (co-op: eid_bagofcrafting.lua 875-909 reads bagPlayer.ControllerIndex) */
        int slot = shim_device_pad_slot(*(const int *) (player + PLAYER_DEVICE_AT));
        unsigned now = slot >= 0 ? shim_pad_slot_buttons(slot) : shim_pad_buttons();
        long long t = now_ns();
        int left = stickclick_step(&l3, (now & PAD_L3) != 0, t, LOCK_HOLD_NS);
        int right = stickclick_step(&r3, (now & PAD_R3) != 0, t, STICK_HOLD_NS);
        if (left == CLICK_HOLD) g_locked = g_locked ? 0 : g_results.hash;
        if (right == CLICK_HOLD && !g_locked) g_refresh_wanted = 1;
        if (right == CLICK_TAP) g_offset = bagswipe_round(&g_pages, g_offset, g_page, total, 1);
        if (left == CLICK_TAP) g_offset = bagswipe_round(&g_pages, g_offset, g_page, total, -1);
        return;                                  /* Select is the inventory's (items.c) */
    }
    /* the inventory list covers the bag's screen while it is up: its keys (items.c) */
    if (!action_held(b, player, ACTION_MAP) || shim_items_open()) {
        memset(g_was_held, 0, sizeof(g_was_held));
        return;
    }
    long t = now_ns();
    int held[4], edge[4];
    for (int i = 0; i < 4; i++) {
        held[i] = action_held(b, player, PAD_ACTION[i]);
        edge[i] = held[i] && !g_was_held[i];
        g_was_held[i] = held[i];
    }
    for (int i = 0; i < 2; i++) {
        int step = 0;
        if (edge[i]) { step = 1; g_repeat_at[i] = t + 750000000L; }
        else if (held[i] && t >= g_repeat_at[i]) { step = 1; g_repeat_at[i] = t + 50000000L; }
        if (step) g_offset += i == 0 ? g_page : -g_page;
    }
    if (g_offset < 0) g_offset = 0;
    if (edge[2]) g_locked = g_locked ? 0 : g_results.hash;
    if (edge[3] && !g_locked) g_refresh_wanted = 1;
}

/* ------------------------------------------------------------ the display */
static unsigned long g_bag_player;
static long g_shown_at;                     /* when the bag's screen was last made */

/*
 * Is the bag held up: the body's main animation (Entity +0x50 the sprite, its
 * main state +0x30, the animation record at +8, the name a std::string at +0 -
 * mods.c reads the same for the dance) is one of the PickupWalk ones.
 */
#define ENTITY_SPRITE_AT   0x50
#define SPRITE_MAIN_ANIM   (0x30 + 0x08)

static int bag_held_up(unsigned long player) {
    if (!readable(player + ENTITY_SPRITE_AT + SPRITE_MAIN_ANIM, 8)) return 0;
    const unsigned char *anim = *(const unsigned char *const *) (player + ENTITY_SPRITE_AT + SPRITE_MAIN_ANIM);
    if (!anim || !readable((unsigned long) anim, 24)) return 0;
    const char *name = (anim[23] & 0x80) ? *(const char *const *) anim : (const char *) anim;
    if (!name || !readable((unsigned long) name, 10)) return 0;
    return strncmp(name, "PickupWalk", 10) == 0;
}

/* does a player have the bag - the mod checks every player, and so does this */
static unsigned long bag_player(unsigned long b) {
    unsigned long players[8];
    int n = shim_eid_players(players, 8);
    for (int i = 0; i < n; i++)
        if (((int (*)(unsigned long, int, int)) (b + FN_HAS_ITEM))(players[i], ITEM_BAG, 0) & 1)
            return players[i];
    return 0;
}

/*
 * The floor, watched on every call (above, "which floor this is" and "the key
 * can change before the room does"): a new key forgets the rooms and has the
 * recipes worked out anew, unless locked; a new run also unlocks. If the room
 * on screen was loaded before the key changed it is the previous floor's, and
 * rooms are held back until the Room object loads the next one.
 */
static void floor_watch(unsigned long b, unsigned long game) {
    floor_key_t now;
    floor_key(b, game, &now);
    int entered = 0, loaded = room_entered(game, &entered);
    int frame = *(const int *) (game + GAME_FRAME);
    if (!g_floor_known || memcmp(&now, &g_floor, sizeof(now)) != 0) {
        int first = !g_floor_known;
        int new_run = first || now.start != g_floor.start;
#if ISAAC_DIAG
        if (shim_report_active() && !first && g_floor_said < FLOOR_LINES) {
            int stale = 0, early = 0;
            for (int i = 0; i < g_room_count; i++) {
                stale += g_rooms[i].serial != g_floor_serial;
                early += g_rooms[i].entered < g_floor_began;
            }
            char line[200];
            snprintf(line, sizeof(line), "floor %d/%d, stale rooms %d, rooms from before the floor %d%s, rooms now %d",
                     g_floor.stage, g_floor.type, stale, early, g_held_back ? " (never left the old room)" : "",
                     g_room_count);
            report_line("craft self-check", line);
        }
        int forgotten = g_room_count;
#endif
        g_room_count = 0;
        g_floor_serial++;
        if (new_run) g_locked = 0;
        if (!g_locked) {
            memset(&g_results, 0, sizeof(g_results));
            g_results_gen++;
            memset(&g_work, 0, sizeof(g_work));
            g_work_busy = 0;
            g_refreshes = 0;
            g_refresh_wanted = 0;
            g_offset = 0;
        }
        g_floor = now;
        g_floor_known = 1;
        /* the first key seen has no previous floor to mistake the room for */
        g_held_back = !first && loaded && frame - entered > ROOM_FRESH;
        g_held_entered = entered;
        g_floor_began = g_held_back ? frame : entered;
#if ISAAC_DIAG
        g_held_frame = frame;
        if (shim_report_active() && g_floor_said < FLOOR_LINES) {
            g_floor_said++;
            g_floor_recompute_said = 0;
            char line[300];
            char held[120] = "";
            if (g_held_back)
                snprintf(held, sizeof(held), ", the room on screen is the previous floor's (loaded %d frames before) - held back",
                         frame - entered);
            snprintf(line, sizeof(line), "new floor %d/%d (stage seed %08x)%s, rooms forgotten %d, %s%s",
                     now.stage, now.type, now.seed, new_run ? ", a new run" : "", forgotten,
                     g_locked ? "the list is LOCKED and kept" : "the recipes are worked out anew", held);
            report_line("craft", line);
        }
#endif
        return;
    }
    /* Room::Init is the one writer of the frame: another value is another room */
    if (g_held_back && loaded && entered != g_held_entered) {
        g_held_back = 0;
        g_floor_began = entered;
#if ISAAC_DIAG
        if (shim_report_active() && g_floor_said <= FLOOR_LINES) {
            g_floor_recompute_said = 0;
            char line[200];
            snprintf(line, sizeof(line), "floor %d/%d: its first room is loaded %d frames after the floor changed - rooms are recorded from here",
                     g_floor.stage, g_floor.type, entered - g_held_frame);
            report_line("craft", line);
        }
#endif
    }
}

int shim_eid_craft_text(char *out, unsigned long room, unsigned *stamp) {
    out[0] = 0;
    unsigned long b = shim_image_base();
    if (!b) return 0;
    unsigned long game = *(const unsigned long *) (b + GAME_PTR);
    if (!game) return 0;
    unsigned long player = bag_player(b);
    g_bag_player = player;
    if (!player || !readable(player + PLAYER_BAG_MADE, 4)) return 0;
    floor_watch(b, game);
    if (*(const int *) (game + GAME_CHALLENGE) == CHALLENGE_CANTRIPPED) return 0;
    unsigned long list = shim_eid_list_now();
    if (!list) return 0;
    /* the room is read whether or not the screen is up, so the floor misses no room */
    const room_t *here = this_room(b, game, list);
    if (in_battle(list)) return 0;
    /*
     * DisplayBagOfCrafting, the mod's three (the owner's tenth round, 24.09.2026:
     * "how do I get the room's descriptions back? I see only the bag"): always -
     * the bag's screen takes the panel, as the mod has it by default; hold -
     * only while the bag is held up, the player's body playing PickupWalk*
     * (eid_bagofcrafting.lua), the room's descriptions the rest of the time;
     * never. The port's default is hold (modset.c, the owner's choice).
     */
    {
        int show = shim_modset_int(MS_EID_BAG);
        if (show == MS_BAG_NEVER) return 0;
        if (show == MS_BAG_HOLD && !bag_held_up(player)) return 0;
    }

    /* the bag, as it really is */
    unsigned char bag[8];
    int bag_n = 0;
    const unsigned char *raw = (const unsigned char *) (player + PLAYER_BAG);
    while (bag_n < 8 && raw[bag_n] && raw[bag_n] <= INGREDIENTS) { bag[bag_n] = raw[bag_n]; bag_n++; }

    /* the cards and pills the players carry */
    unsigned char held[64];
    int held_n = 0;
    unsigned long players[8];
    int pn = shim_eid_players(players, 8);
    for (int p = 0; p < pn; p++) {
        if (!readable(players[p] + PLAYER_POCKET, 32)) continue;
        const int *pocket = (const int *) (players[p] + PLAYER_POCKET);
        for (int j = 0; j < 4 && held_n < 60; j++) {
            unsigned char got[2];
            int what = pocket[j * 2], kind = pocket[j * 2 + 1];
            int k = kind == 1 ? ingredients_of(300, what, got) : kind == 0 ? ingredients_of(70, what, got) : 0;
            if (k > 0) held[held_n++] = got[0];     /* the mod counts one per card or pill */
        }
    }

    /* the floor: every room seen, this one as it is now */
    unsigned char floor[MAX_ROOMS * 8];
    int floor_n = 0;
    for (int i = 0; i < g_room_count; i++)
        for (int j = 0; j < g_rooms[i].n && floor_n < (int) sizeof(floor); j++)
            floor[floor_n++] = g_rooms[i].items[j];

    /* itemQuery: floor, held and bag together, no more than eight of a kind */
    unsigned char query[MAX_QUERY];
    int qn = 0, count[INGREDIENTS + 1] = { 0 };
    const unsigned char *parts[3] = { floor, held, bag };
    int part_n[3] = { floor_n, held_n, bag_n };
    for (int t = 0; t < 3; t++)
        for (int i = 0; i < part_n[t] && qn < MAX_QUERY; i++) {
            unsigned char v = parts[t][i];
            if (v > INGREDIENTS || count[v] >= 8) continue;
            count[v]++;
            query[qn++] = v;
        }
    if (qn < 8) return 0;
    qsort(query, (size_t) qn, 1, by_value);
    unsigned hash = hash_of(query, qn);

    sort_items(b);

    /* the recipes: a crunch for a query not seen yet, or when asked to refresh;
       until it ends the last finished list stays up, as in the mod */
    if (!g_locked) {
        int refresh = g_refresh_wanted;
        g_refresh_wanted = 0;
        int known = g_results.hash == hash || (g_work_busy && g_work.hash == hash);
        if (!known || refresh) {
            if (!refresh) g_refreshes = 0;
            start_crunch(query, qn, hash, refresh);
        }
    }
    crunch(b);
    const results_t *list_now = &g_results;
#if ISAAC_DIAG
    /* the first list finished on a new floor: what it was made of */
    if (shim_report_active() && !g_floor_recompute_said && !g_work_busy && g_results.hash == hash) {
        g_floor_recompute_said = 1;
        char line[160];
        snprintf(line, sizeof(line), "floor %d/%d recomputed: query %d ingredients, %d recipes, rooms %d",
                 g_floor.stage, g_floor.type, qn, g_results.count, g_room_count);
        report_line("craft", line);
    }
#endif

    /* filteredRecipesList: without Tainted Cain, only what uses the whole bag */
    int tcain = *(const int *) (player + PLAYER_TYPE) == TYPE_TAINTED_CAIN;
    int total = filtered_now(list_now, bag, bag_n, tcain);
    if (!total) { g_offset = 0; return 0; }
    pad(b, player, total);
    if (g_offset >= total) g_offset = 0;

    /* getFloorItemsString(true, roomItems) */
    if (bag_n) {
        if (bag_n == 8) {
            char made[48];
            snprintf(made, sizeof(made), "{{Collectible%d}} ", *(const int *) (player + PLAYER_BAG_MADE));
            add(out, room, made);
        }
        add_label(out, room, shim_eid_row(KIND_PHRASE, PHRASE_BAG));
        icons(out, room, bag, bag_n, NULL, 0, 0);
        add(out, room, "#");
    }
    if (here && here->n) {
        if (here->n == 8) {
            char made[48];
            snprintf(made, sizeof(made), "{{Collectible%d}} ", recipe_of(b, here->items));
            add(out, room, made);
        }
        add_label(out, room, shim_eid_row(KIND_PHRASE, PHRASE_ROOM));
        icons(out, room, here->items, here->n, NULL, 0, 0);
        add(out, room, "#");
    }
    if (floor_n && (!here || here->n != floor_n)) {
        if (floor_n == 8) {
            char made[48];
            snprintf(made, sizeof(made), "{{Collectible%d}} ", recipe_of(b, floor));
            add(out, room, made);
        }
        add_label(out, room, shim_eid_row(KIND_PHRASE, PHRASE_FLOOR));
        icons(out, room, floor, floor_n, NULL, 0, 0);
    }
    if (held_n) {
        add(out, room, "(+");
        icons(out, room, held, held_n, NULL, 0, 0);
        add(out, room, ")#");
    } else {
        add(out, room, "#");
    }
    controls_line(out, room);

    const char *prefix = g_locked ? "#{{Padlock}} " : "#{{Blank}} ";
    const char *more = shim_eid_row(KIND_PHRASE, PHRASE_MORE);
    char line[160];

    /*
     * The room this screen has for recipes: under the panel's place, less the
     * header, the text above the recipes and the two "more" lines. Each recipe
     * is measured as it is added - a long one breaks onto a second line - and
     * the page ends at the first that would not fit (never before the first).
     */
    float room_left;
    {
        float px = 0.0f, py = 0.0f;
        shim_eid_panel_place(&px, &py);
        room_left = shim_screen_height() - py - shim_eid_head_height() - PAGE_MARGIN
                    - shim_eid_text_height(out) - 2.0f * shim_eid_text_height("#{{Blank}} 0");
    }
    if (g_offset > 0 && more) {
        char n[16];
        snprintf(n, sizeof(n), "%d", g_offset);
        snprintf(line, sizeof(line), "%s", more);
        char *v = strstr(line, "{1}");
        if (v) { char rest[120]; snprintf(rest, sizeof(rest), "%s", v + 3); snprintf(v, sizeof(line) - (size_t) (v - line), "%s%s", n, rest); }
        add(out, room, prefix);
        add(out, room, line);
    }
    int shown = 0;
    float used = 0.0f;
    for (int k = g_offset; k < total && shown < RESULTS; k++) {
        const recipe_t *r = &list_now->list[g_filtered[k]];
        int id = r->made;
        int q = g_quality[id] >= 0 ? g_quality[id] : 0;
        /* "= " - the mod writes the counts against the sign; the owner wants a space */
        char row[320] = "";
        snprintf(line, sizeof(line), "#{{Collectible%d}} %s={{CR}} ", id, QUALITY_COLOUR[q]);
        add(row, sizeof(row), line);
        icons(row, sizeof(row), r->ing, 8, bag, bag_n, 1);
        float h = shim_eid_text_height(row);
        if (shown > 0 && used + h > room_left) break;
        used += h;
        add(out, room, row);
        shown++;
    }
    g_page = shown > 0 ? shown : 1;                     /* the next page starts past these */
    if (g_offset + shown < total && more) {
        char n[16];
        snprintf(n, sizeof(n), "%d", total - shown - g_offset);
        snprintf(line, sizeof(line), "%s", more);
        char *v = strstr(line, "{1}");
        if (v) { char rest[120]; snprintf(rest, sizeof(rest), "%s", v + 3); snprintf(v, sizeof(line) - (size_t) (v - line), "%s%s", n, rest); }
        add(out, room, prefix);
        add(out, room, line);
    } else if (g_locked) {
        add(out, room, prefix);
    }

#if ISAAC_DIAG
    /* PROBE: the text as built, a few times */
    static unsigned said_hash;
    static int said;
    if (shim_report_active() && said < 6) {
        unsigned h0 = 2166136261u;
        for (const char *c = out; *c; c++) { h0 ^= (unsigned char) *c; h0 *= 16777619u; }
        if (h0 != said_hash && !g_work_busy) {
            said_hash = h0;
            said++;
            char line[900];
            snprintf(line, sizeof(line), "query %d ingredients, %d recipes known, %d shown after the bag filter: %.700s",
                     qn, list_now->count, total, out);
            report_line("craft", line);
        }
    }
#endif

    if (stamp) {
        unsigned h = 2166136261u;
        for (const char *c = out; *c; c++) { h ^= (unsigned char) *c; h *= 16777619u; }
        *stamp = h;
    }
    g_shown_at = now_ns();
    return 1;
}

/* is the bag's screen on the panel now (items.c: the map and a shot are its keys) */
int shim_eid_on(void);
int shim_eid_craft_showing(void) {
    return shim_eid_on() && g_shown_at && now_ns() - g_shown_at < 250000000L;
}

/* eid.c: whose bag the screen is - the first player who has it (eid_api.lua 1836, PlayersHaveCollectible) */
unsigned long shim_eid_craft_player(void) { return g_bag_player; }

/* PROBE, report builds: is Room+8 really one per room? */
unsigned long shim_eid_craft_room_key(void) {
    unsigned long b = shim_image_base();
    unsigned long game = b ? *(const unsigned long *) (b + GAME_PTR) : 0;
    unsigned long room = game ? *(const unsigned long *) (game + GAME_ROOM) : 0;
    return readable(room + ROOM_DESCRIPTOR, 8) ? *(const unsigned long *) (room + ROOM_DESCRIPTOR) : 0;
}
