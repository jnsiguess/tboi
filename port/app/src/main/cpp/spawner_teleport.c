/*
 * THE TELEPORT TAB OF THE SPAWNER (the owner, 30.09.2026): every room type in
 * one column, every floor in the other; a room goes on one tap, a floor on the
 * second. Full lists - some items reach anywhere, even in a challenge - so a
 * room the floor does not have is made, the way the game makes its own rooms
 * off the map. All of it read in the image: the research notes of 02.10.2026,
 * teleport.
 *
 * A JUMP NEVER STARTS UNDER THE PAUSE. Game::Update (0x88dbf0) runs room and
 * stage transitions (0x88e03c, 0x88e05c) before it asks about the pause
 * (0x88e0bc), so a jump begun with the pause up would play beneath it. The tab
 * only asks; between frames here:
 *
 *   1. the card is already closing (spawner.c) and the run marked;
 *   2. the pause goes as the pad's Back takes it away: 0x8a1f08(Game+0x10dd20),
 *      then 0x670ca8(manager) (the tail at 0x8a18a0);
 *   3. wait for: the card's dialog gone, the pause state [Game+0x10dfd8] at 0,
 *      no room transition 0x6a6658(Game+0x25810), no stage transition
 *      0x89a0b4(Game+0x25c00);
 *   4. the jump, the game's own way for each kind of room:
 *
 *   on the floor   [Game+0x2156c] = -1; ChangeRoom 0x88f8cc(Game, desc+4, -1,
 *                  3, player0, -1) - as the Emperor card does (0x544a1c); the
 *                  room list Game+0x18 + i*0x100, i < [Game+0x21510], the
 *                  room's config [desc+0x10] (type +8, subtype +0x10), its
 *                  dimension desc+0xc against [Game+0x21560]
 *   start room     ChangeRoom([Game+0x21514], .., dim 0) - the Fool
 *   devil, angel   0x6dc660(Game, angel, devil), then ChangeRoom(-1, .., 0) -
 *                  the Joker; once a floor has one, the other is not made
 *   error, crawl-  the game's own slots off the map, -2 (only when 0x6d47a0
 *   space, boss    says the floor may have it), -4, -5, -6, each only when
 *   rush, black    the game filled it
 *   market
 *   not on the     the debug room -3: GetRandomRoom 0x752b00 from the special
 *   floor          rooms (stage 0), then 0x6d2fe0(Game, cfg) - the game's own
 *                  way into a room by its config (0x88c4f4). Leaving it: the
 *                  room's one door leads to the room you came from
 *                  (0x6d2014, 0x6d192c). Never -3 straight into -3: the room
 *                  left is saved into its own descriptor first (0x6d30f4) -
 *                  so from -3 the way is back to the previous room, and then on
 *
 *   a floor        SetStage 0x6daa44(Game, stage, type); the same floor again
 *                  takes a new seed instead, 0x5724a4(Game+0x25d40, stage +
 *                  IsAltStage 0x6d0c70(Game)) as Forget Me Now does; then
 *                  0x88e42c(Game, 1, 0, NULL)
 *
 * Bosses come from the floor's own list only: the special rooms hold every
 * arena, the final ones too, and one made at random could start an ending.
 */
#include <stdio.h>
#include <string.h>
#include "diag.h"

unsigned long shim_image_base(void);
int shim_report_active(void);
void report_line(const char *kind, const char *text);
int shim_spawner_card_gone(void);

#include "spawner_floors.h"

#define GAME_PTR            0xac3b90
#define MANAGER_PTR         0xac2428
#define GAME_STAGE          0x0
#define GAME_STAGE_TYPE     0x4
#define GAME_ROOMS          0x18
#define DESC_BYTES          0x100
#define GAME_ROOM_COUNT     0x21510
#define GAME_START_INDEX    0x21514
#define GAME_ROOM           0x21550         /* the Room; its [+8] is the descriptor */
#define GAME_ROOM_INDEX     0x21558
#define GAME_LAST_INDEX     0x2155c
#define GAME_DIMENSION      0x21560
#define GAME_LEAVE_DOOR     0x2156c
#define GAME_ROOM_CONFIGS   0x21b78
#define GAME_SEED_OBJECT    0x25d40
#define GAME_PLAYERS        0x25c40
#define GAME_ROOM_TRANS     0x25810
#define GAME_STAGE_TRANS    0x25c00
#define GAME_PAUSE          0x10dd20
#define GAME_PAUSE_STATE    0x10dfd8
#define GAME_DIFFICULTY     0x24f9d8
#define MGR_RESUMED         0x27bcc9
#define DESC_SAFE_INDEX     0x4
#define DESC_DIMENSION      0xc
#define DESC_CONFIG         0x10
#define DESC_DOORS          0x20
#define CFG_TYPE            0x8
#define CFG_SUBTYPE         0x10

#define FN_PLAYER_AT        0x4da574
#define FN_CHANGE_ROOM      0x88f8cc
#define FN_DEVIL_ANGEL      0x6dc660
#define FN_ERROR_ALLOWED    0x6d47a0
#define FN_RANDOM_ROOM      0x752b00
#define FN_DEBUG_ROOM       0x6d2fe0
#define FN_RANDOM           0x7f1cc0
#define FN_SET_STAGE        0x6daa44
#define FN_RESEED           0x5724a4
#define FN_IS_ALT           0x6d0c70
#define FN_STAGE_TRANSITION 0x88e42c
#define FN_PAUSE_CLOSE      0x8a1f08
#define FN_APP_ACTIVE       0x670ca8
#define FN_ROOM_TRANS_BUSY  0x6a6658
#define FN_STAGE_TRANS_BUSY 0x89a0b4

#define T_DEFAULT  1
#define T_ERROR    3
#define T_BOSS     5
#define T_CHALLENGE 11
#define T_DEVIL    14
#define T_ANGEL    15
#define T_DUNGEON  16
#define T_BOSSRUSH 17
#define T_BLACK_MARKET 22
#define T_SECRET_EXIT 27
#define SHAPE_ANY  13
#define ANIM_TELEPORT 3
#define WAIT_MOST  300                      /* frames: ten seconds, then the jump is given up */

static unsigned long game_now(unsigned long b) { return b ? *(const unsigned long *) (b + GAME_PTR) : 0; }

static unsigned long desc_at(unsigned long game, int idx) {
    /* the list, and the rooms off the map below it: -k at list index 0x1fa + k (GetRoomByIdx) */
    int list = idx >= 0 ? idx : 0x1fa - idx;
    return game + GAME_ROOMS + (unsigned long) list * DESC_BYTES;
}

static unsigned long config_of(unsigned long desc) { return *(const unsigned long *) (desc + DESC_CONFIG); }

static unsigned long desc_now(unsigned long game) {
    unsigned long room = *(const unsigned long *) (game + GAME_ROOM);
    return room ? *(const unsigned long *) (room + 8) : 0;
}

/*
 * Where a room of this type is: answers 2 you are in it, 1 it is there (idx,
 * dim), 0 it can be made, -1 not on this floor nor to be made.
 */
static int room_way(unsigned long b, unsigned long game, int type, int sub, int *idx, int *dim) {
    unsigned long here = desc_now(game);
    int curdim = *(const int *) (game + GAME_DIMENSION);
    *dim = -1;
    if (type == T_DEFAULT) {
        *idx = *(const int *) (game + GAME_START_INDEX);
        *dim = 0;
        return desc_at(game, *idx) == here && curdim == 0 ? 2 : 1;
    }
    int slot = 0;
    switch (type) {
    case T_ERROR: slot = -2; break;
    case T_DEVIL: case T_ANGEL: slot = -1; break;
    case T_DUNGEON: slot = -4; break;
    case T_BOSSRUSH: slot = -5; break;
    case T_BLACK_MARKET: slot = -6; break;
    }
    if (slot) {
        unsigned long d = desc_at(game, slot), cfg = config_of(d);
        *idx = slot;
        if (slot != -2) *dim = 0;
        if (slot == -2)                         /* only where the game itself would allow it */
            return cfg && ((int (*)(unsigned long)) (b + FN_ERROR_ALLOWED))(game) ? (d == here ? 2 : 1) : -1;
        if (cfg && *(const int *) (cfg + CFG_TYPE) == type) return d == here ? 2 : 1;
        if (slot == -1) return cfg ? -1 : 0;    /* the other one was made this floor */
        if (slot != -5) return -1;              /* the boss rush alone may be made */
    }
    int n = *(const int *) (game + GAME_ROOM_COUNT);
    if (n < 0 || n > 507) n = 0;
    for (int i = 0; i < n; i++) {
        unsigned long d = desc_at(game, i), cfg = config_of(d);
        if (!cfg || *(const int *) (cfg + CFG_TYPE) != type) continue;
        if (type == T_CHALLENGE && *(const int *) (cfg + CFG_SUBTYPE) != sub) continue;
        if (*(const int *) (d + DESC_DIMENSION) != curdim) continue;
        *idx = *(const int *) (d + DESC_SAFE_INDEX);
        return d == here ? 2 : 1;
    }
    if (type == T_BOSS || type == T_SECRET_EXIT) return -1;
    *idx = -3;
    return 0;
}

int shim_teleport_room_state(int type, int subtype) {
    unsigned long b = shim_image_base(), game = game_now(b);
    if (!game) return -1;
    int idx, dim;
    return room_way(b, game, type, subtype, &idx, &dim);
}

int shim_teleport_floor_here(void) {
    unsigned long b = shim_image_base(), game = game_now(b);
    if (!game) return -1;
    int stage = *(const int *) (game + GAME_STAGE), type = *(const int *) (game + GAME_STAGE_TYPE);
    int d = *(const int *) (game + GAME_DIFFICULTY), greed = d == 2 || d == 3;
    for (int i = 0; i < SP_FLOORS; i++)
        if (SP_FLOOR[i].stage == stage && SP_FLOOR[i].stage_type == type && (SP_FLOOR[i].mode == 1) == greed)
            return i;
    return -1;
}

/* ------------------------------------------------------------ the jump */
#define ASK_NONE  0
#define ASK_ROOM  1
#define ASK_FLOOR 2

static int g_ask;
static int g_type, g_sub, g_stage, g_stage_type;
static int g_waited;
static int g_back_first;                     /* in -3 asked for -3: the previous room first */
static int g_after;                          /* frames to watch after the jump, for the report */

void shim_teleport_ask_room(int type, int subtype) {
    g_ask = ASK_ROOM; g_type = type; g_sub = subtype; g_waited = 0; g_back_first = 0;
}

void shim_teleport_ask_floor(int stage, int stage_type) {
    g_ask = ASK_FLOOR; g_stage = stage; g_stage_type = stage_type; g_waited = 0; g_back_first = 0;
}

/* the report's lines, in the report build only */
#if ISAAC_DIAG
#define SAY(...) do { if (shim_report_active()) { char said_[200]; snprintf(said_, sizeof(said_), __VA_ARGS__);                                                  report_line("teleport", said_); } } while (0)
#else
#define SAY(...) do { } while (0)
#endif

static int settled(unsigned long b, unsigned long game) {
    return shim_spawner_card_gone()
           && *(const int *) (game + GAME_PAUSE_STATE) == 0
           && !((int (*)(unsigned long)) (b + FN_ROOM_TRANS_BUSY))(game + GAME_ROOM_TRANS)
           && !((int (*)(unsigned long)) (b + FN_STAGE_TRANS_BUSY))(game + GAME_STAGE_TRANS);
}

static void change_room(unsigned long b, unsigned long game, int idx, int dim) {
    unsigned long player = ((unsigned long (*)(unsigned long, int)) (b + FN_PLAYER_AT))(game + GAME_PLAYERS, 0);
    *(int *) (game + GAME_LEAVE_DOOR) = -1;
    ((void (*)(unsigned long, int, int, int, unsigned long, int)) (b + FN_CHANGE_ROOM))(
        game, idx, -1, ANIM_TELEPORT, player, dim);
}

/*
 * GetRandomRoom takes five arguments on the stack, laid out the Apple way -
 * each by its own size: [sp] minDifficulty, [sp+4] maxDifficulty, [sp+8] the
 * doors' byte mask pointer, [sp+0x10] subtype, [sp+0x14] mode. Here every
 * stack argument takes eight bytes, so they go as three words packed to that
 * same layout.
 */
static unsigned long random_room(unsigned long b, unsigned long game, int type, int sub) {
    static unsigned char doors_any;              /* 0: any doors */
    typedef unsigned long (*grr_t)(unsigned long, int, int, int, int, int, int, int,
                                   unsigned long, unsigned long, unsigned long);
    int seed = ((int (*)(void)) (b + FN_RANDOM))();
    unsigned long diff = 1ul | (10ul << 32);
    unsigned long tail = (unsigned long) (unsigned) sub | ((unsigned long) 0xffffffffu << 32);   /* mode -1 */
    return ((grr_t) (b + FN_RANDOM_ROOM))(game + GAME_ROOM_CONFIGS, seed, 0, 0, type, SHAPE_ANY, 0, -1,
                                          diff, (unsigned long) &doors_any, tail);
}

static void jump_room(unsigned long b, unsigned long game) {
    int idx = 0, dim = -1;
    int way = room_way(b, game, g_type, g_sub, &idx, &dim);
    int cur = *(const int *) (game + GAME_ROOM_INDEX);
    if (way == 2 || way < 0) {
        SAY("room type %d.%d: %s", g_type, g_sub, way == 2 ? "already there" : "not to be had");
        return;
    }
    if (way == 1) {
        change_room(b, game, idx, dim);
        SAY("room type %d.%d: to %d dim %d from %d", g_type, g_sub, idx, dim, cur);
        return;
    }
    /* to be made */
    if (g_type == T_DEVIL || g_type == T_ANGEL) {
        ((void (*)(unsigned long, int, int)) (b + FN_DEVIL_ANGEL))(game, g_type == T_ANGEL, g_type == T_DEVIL);
        unsigned long cfg = config_of(desc_at(game, -1));
        int made = cfg ? *(const int *) (cfg + CFG_TYPE) : -1;
        SAY("room type %d: the game made %d in -1", g_type, made);
        if (made == g_type) change_room(b, game, -1, 0);
        return;
    }
    if (cur == -3 && !g_back_first) {           /* never -3 into -3: back first, then on */
        int last = *(const int *) (game + GAME_LAST_INDEX);
        g_back_first = 1;
        g_ask = ASK_ROOM;
        g_waited = 0;
        change_room(b, game, last, -1);
        SAY("room type %d: in the made room - first back to %d", g_type, last);
        return;
    }
    unsigned long cfg = random_room(b, game, g_type, g_type == T_CHALLENGE ? g_sub : -1);
    if (!cfg) {
        SAY("room type %d.%d: the special rooms have none", g_type, g_sub);
        return;
    }
    *(int *) (game + GAME_LEAVE_DOOR) = -1;
    ((void (*)(unsigned long, unsigned long)) (b + FN_DEBUG_ROOM))(game, cfg);
    SAY("room type %d.%d: made from the special rooms (type %d subtype %d) from %d",
             g_type, g_sub, *(const int *) (cfg + CFG_TYPE), *(const int *) (cfg + CFG_SUBTYPE), cur);
    g_after = 60;
}

static void jump_floor(unsigned long b, unsigned long game) {
    int stage = *(const int *) (game + GAME_STAGE), type = *(const int *) (game + GAME_STAGE_TYPE);
    if (stage == g_stage && type == g_stage_type) {
        int alt = ((int (*)(unsigned long)) (b + FN_IS_ALT))(game);
        ((void (*)(unsigned long, int)) (b + FN_RESEED))(game + GAME_SEED_OBJECT, stage + alt);
    } else {
        ((void (*)(unsigned long, int, int)) (b + FN_SET_STAGE))(game, g_stage, g_stage_type);
    }
    ((void (*)(unsigned long, int, int, unsigned long)) (b + FN_STAGE_TRANSITION))(game, 1, 0, 0);
    SAY("floor %d.%d asked from %d.%d, ascent %d", g_stage, g_stage_type, stage, type,
             *(const unsigned char *) (game + 0x1cf406) & 1);
    g_after = 90;
}

void shim_teleport_between_frames(void) {
    unsigned long b = shim_image_base(), game = game_now(b);
    if (!game) { g_ask = ASK_NONE; return; }
#if ISAAC_DIAG
    if (g_after > 0 && --g_after == 0 && shim_report_active()) {
        char line[200];
        unsigned long d = desc_now(game);
        snprintf(line, sizeof(line), "after: stage %d.%d room %d last %d dim %d doors 0x%x",
                 *(const int *) (game + GAME_STAGE), *(const int *) (game + GAME_STAGE_TYPE),
                 *(const int *) (game + GAME_ROOM_INDEX), *(const int *) (game + GAME_LAST_INDEX),
                 *(const int *) (game + GAME_DIMENSION), d ? *(const unsigned *) (d + DESC_DOORS) : 0);
        report_line("teleport", line);
    }
#endif
    if (g_ask == ASK_NONE) return;
    unsigned long manager = *(const unsigned long *) (b + MANAGER_PTR);
    if (g_waited == 0 && *(const int *) (game + GAME_PAUSE_STATE) != 0) {
        /* the pause goes the way a pad's Back takes it (0x8a1868 .. 0x8a18a0) */
#if ISAAC_DIAG
        int was = manager ? *(const unsigned char *) (manager + MGR_RESUMED) : -1;
#endif
        ((void (*)(unsigned long)) (b + FN_PAUSE_CLOSE))(game + GAME_PAUSE);
        if (manager) ((void (*)(unsigned long)) (b + FN_APP_ACTIVE))(manager);
        SAY("resume: byte %d -> %d, pause state %d",
                 was, manager ? *(const unsigned char *) (manager + MGR_RESUMED) : -1,
                 *(const int *) (game + GAME_PAUSE_STATE));
    }
    g_waited++;
    if (!settled(b, game)) {
        if (g_waited > WAIT_MOST) {
            SAY("given up after %d frames: pause state %d, card gone %d",
                     g_waited, *(const int *) (game + GAME_PAUSE_STATE), shim_spawner_card_gone());
            g_ask = ASK_NONE;
        }
        return;
    }
    int ask = g_ask;
    g_ask = ASK_NONE;
    if (ask == ASK_ROOM) jump_room(b, game);
    else jump_floor(b, game);
}
