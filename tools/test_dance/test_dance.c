/*
 * Desk test of the Specialist Dance on every way out of a run (1.75: "exit
 * during the dance and it crashes"). mods.c's dance section is cut out as it
 * is by run.py (dance_cut.h, Android's LP64 longs) and run against a fake
 * image: the game functions it calls are thunks into this file, and the
 * players are deleted the game's way (0x5671e8: out of Game+0x25c40, then
 * deleted) on every road the image takes:
 *
 *   menu      pause -> Exit, the game over, Android's back -> pause -> Exit:
 *             0x88d540 asks 0x672684(M, mode, 1); the manager's next update
 *             0x6716a0 ends the run (0x6716dc -> 0x672830 -> 0x88d7ac ->
 *             0x5671e8) and writes state 3 (0x6716f8).
 *   savequit  the same with the save (the costume ids go into it), then
 *             Continue: RestoreGameState puts our costume back on.
 *   restart   R, the pad's combination, the held pause: 0x672568 asks for a
 *             run; 0x671348 ends the old one (0x671394 -> 0x672830) and
 *             writes state 2 again (0x6713e4) before the new players come -
 *             the frame code never sees a state other than 2. New players
 *             at new addresses, or at the very addresses just deleted.
 *   minimise  the app sent away: the run is saved, nobody is deleted; the
 *             costume vectors are rebuilt (moved) to be sure nobody caches them.
 *   coopleave one co-op dancer deleted mid-dance, the other dances on.
 *   floor     a trapdoor mid-dance: state 5, then a new floor's room.
 *   fuzz N    random roads, N steps, every invariant after every pass.
 *
 * A deleted player's memory is decommitted (its costume vector too), so any
 * read of it faults; the fault is caught and named. A game function called
 * on a deleted player is named too. Exit 3: a use after free; 1: a failed
 * check; 0: clean.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "modset.h"

#ifndef ISAAC_DIAG
#define ISAAC_DIAG 0
#endif
#define LOG(...) do { } while (0)
#define strcasecmp _stricmp
#define gettid() ((int) GetCurrentThreadId())

typedef unsigned long long u64;

/* ------------------------------------------------ what mods.c asks around it */
typedef struct AAssetManager AAssetManager;
typedef struct AAssetDir AAssetDir;
static AAssetDir *AAssetManager_openDir(AAssetManager *m, const char *d) { (void) m; (void) d; return NULL; }
static const char *AAssetDir_getNextFileName(AAssetDir *d) { (void) d; return NULL; }
static void AAssetDir_close(AAssetDir *d) { (void) d; }
void shim_asset_bring_in(void *mgr, const char *name, const char *folder, const char *leaf) {
    (void) mgr; (void) name; (void) folder; (void) leaf;
}

static int g_verbose, g_mod_on = 1, g_fail;
#define CHECK(c, ...) do { if (!(c)) { g_fail++; printf("  FAIL line %d: ", __LINE__); printf(__VA_ARGS__); \
                                       printf("\n"); fflush(stdout); } } while (0)

int shim_report_active(void) { return 1; }
void report_line(const char *kind, const char *text) { if (g_verbose) printf("    [%s] %s\n", kind, text); }
int shim_tweaks_mod(int i) { (void) i; return g_mod_on; }
int shim_modset_int(int id) {
    switch (id) {
    case MS_DANCE_QUALITY: return 4;
    case MS_DANCE_MUSIC: return 1;
    default: return 0;
    }
}
int shim_modset_listed(int id, int value) { (void) id; (void) value; return 0; }
void shim_bossbars_between_frames(void) { }
void shim_statsplus_between_frames(void) { }
static void marks_between_frames(u64 b) { (void) b; }
static void unlocks_probe(u64 b) { (void) b; }

static u64 g_base;
static u64 shim_image_base(void) { return g_base; }

static u64 g_manager;
#define STATE (*(int *) (g_manager + 0x204c0))
int shim_sticks_in_game(void) { return STATE == 2; }

#include "dance_cut.h"

/* ------------------------------------------------------------ the fake game */
#define PLAYER_BYTES   0x4000
#define COSTUME_BUF    0x2000               /* 0x178 records: room for 21 */
#define P_ANIM         0x2900               /* our costume's HeadDown record, inside the player */
#define P_OWNED_AT     0x2a00               /* the owned-items counts, inside the player */
#define MAXP           64

typedef struct { u64 at; u64 buf; int alive; } block_t;
static block_t g_blocks[MAXP * 64];
static int g_nblocks;
static u64 g_game, g_room;
static u64 g_plist[16];
static int g_np;
static u64 g_null_items[256];
static unsigned char g_null_rec[256][0x100];
static unsigned char g_collectible[8][0x100];
static int g_dead_calls;

static void sync_players(void) {
    u64 *list = (u64 *) (g_game + GAME_PLAYER_LIST);
    list[2] = (u64) g_plist;
    list[3] = (u64) (g_plist + g_np);
}

static block_t *block_of(u64 p) {
    for (int i = 0; i < g_nblocks; i++) if (g_blocks[i].at == p) return &g_blocks[i];
    return NULL;
}

static int alive(u64 p) { block_t *k = block_of(p); return k && k->alive; }

/* a sprite inside some live player's costume vector? */
static int sprite_alive(u64 s) {
    for (int i = 0; i < g_nblocks; i++)
        if (g_blocks[i].alive && s >= g_blocks[i].buf && s < g_blocks[i].buf + COSTUME_BUF) return 1;
    return 0;
}

static u64 new_player(int type, u64 reuse) {
    u64 p;
    if (reuse) {
        p = (u64) VirtualAlloc((void *) reuse, PLAYER_BYTES, MEM_COMMIT, PAGE_READWRITE);
        CHECK(p == reuse, "the deleted player's address was not given again");
    } else {
        p = (u64) VirtualAlloc(NULL, PLAYER_BYTES, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    }
    u64 buf = (u64) VirtualAlloc(NULL, COSTUME_BUF, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    memset((void *) p, 0, PLAYER_BYTES);
    *(int *) (p + PLAYER_TYPE) = type;
    *(u64 *) (p + PLAYER_OWNED) = p + P_OWNED_AT;
    *(u64 *) (p + PLAYER_COSTUMES) = buf;
    *(u64 *) (p + PLAYER_COSTUMES + 8) = buf;
    unsigned char *anim = (unsigned char *) (p + P_ANIM);
    strcpy((char *) anim, "HeadDown");
    anim[ANIMATION_LOOP] = 1;
    block_t *k = block_of(p);
    if (!k) k = &g_blocks[g_nblocks++];
    k->at = p; k->buf = buf; k->alive = 1;
    return p;
}

/* the player's destructor: its costume vector freed, then the player */
static void delete_player(u64 p) {
    block_t *k = block_of(p);
    CHECK(k && k->alive, "deleting a player twice");
    VirtualFree((void *) k->buf, 0, MEM_RELEASE);
    VirtualFree((void *) p, PLAYER_BYTES, MEM_DECOMMIT);    /* reserved still: a restart may get it back */
    k->alive = 0;
}

/* 0x5671e8: every player out of the vector, then deleted */
static void players_clear(void) {
    for (int i = 0; i < g_np; i++) delete_player(g_plist[i]);
    g_np = 0;
    sync_players();
}

static void player_add(u64 p) { g_plist[g_np++] = p; sync_players(); }

static void player_remove(int i) {
    u64 p = g_plist[i];
    memmove(&g_plist[i], &g_plist[i + 1], (size_t) (g_np - i - 1) * 8);
    g_np--;
    sync_players();
    delete_player(p);
}

static int is_ours(u64 item) {
    for (int k = 0; k < C_COUNT; k++) if (g_null_items[132 + k] == item) return 1;
    return 0;
}

static int costumes_of(u64 p, u64 *out, int max) {
    u64 b = *(u64 *) (p + PLAYER_COSTUMES), e = *(u64 *) (p + PLAYER_COSTUMES + 8);
    int n = 0;
    for (u64 c = b; c < e; c += COSTUME_RECORD) if (n < max) out[n++] = *(u64 *) (c + COSTUME_ITEM);
    return n;
}

static int ours_worn(u64 p) {
    u64 items[32];
    int n = costumes_of(p, items, 32), k = 0;
    for (int i = 0; i < n; i++) k += is_ours(items[i]);
    return k;
}

/* the game's functions, as thunks in the fake image */
static int fake_load_costumes(u64 cfg, const char *path, void *mod) {
    (void) path; (void) mod;
    u64 *v = (u64 *) (cfg + CONFIG_NULLS);
    v[1] += C_COUNT * 8;
    return 1;
}
static int fake_load_music(u64 music, const char *path, int m) {
    (void) path; (void) m;
    u64 *v = (u64 *) (music + MUSIC_TRACKS);
    v[1] += T_COUNT * 0x70;
    return 1;
}
static u64 fake_null_item(u64 cfg, int id) { (void) cfg; return id >= 0 && id < 174 ? g_null_items[id] : 0; }
static u64 fake_collectible(u64 cfg, int id) { (void) cfg; return id == 1 ? (u64) g_collectible[1] : (u64) g_collectible[0]; }
static void fake_add_costume(u64 p, u64 item, int force) {
    (void) force;
    if (!alive(p)) { g_dead_calls++; CHECK(0, "AddCostume on a deleted player %llx", p); return; }
    u64 *v = (u64 *) (p + PLAYER_COSTUMES);
    CHECK(v[1] + COSTUME_RECORD <= block_of(p)->buf + COSTUME_BUF, "costume vector full");
    unsigned char *c = (unsigned char *) v[1];
    memset(c, 0, COSTUME_RECORD);
    *(u64 *) (c + COSTUME_ITEM) = item;
    *(u64 *) (c + STATE_MAIN + STATE_ANIMATION) = p + P_ANIM;
    v[1] += COSTUME_RECORD;
}
static void fake_remove_costume(u64 p, u64 item) {
    if (!alive(p)) { g_dead_calls++; CHECK(0, "RemoveCostume on a deleted player %llx", p); return; }
    u64 *v = (u64 *) (p + PLAYER_COSTUMES);
    for (u64 c = v[0]; c < v[1]; c += COSTUME_RECORD)
        if (*(u64 *) (c + COSTUME_ITEM) == item) {
            memmove((void *) c, (void *) (c + COSTUME_RECORD), (size_t) (v[1] - c - COSTUME_RECORD));
            v[1] -= COSTUME_RECORD;
            return;
        }
}
static void fake_play_music(u64 music, int id) { *(int *) (music + MUSIC_NOW) = id; }
#define ROOM_TRACK 0x20                     /* the fake room's own music */
static int g_room_music_calls;
static void fake_room_music(u64 room) {
    g_room_music_calls++;
    *(int *) (g_manager + MGR_MUSIC + MUSIC_NOW) = *(int *) (room + ROOM_TRACK);
}
static unsigned fake_curses(u64 game) { (void) game; return 0; }
static int g_plays;
static void fake_sprite_play(u64 sprite, const char *name, int force) {
    (void) name; (void) force;
    if (!sprite_alive(sprite)) { g_dead_calls++; CHECK(0, "Play on a deleted player's sprite %llx", sprite); return; }
    *(unsigned char *) (sprite + STATE_MAIN + STATE_PLAYING) = 1;
    g_plays++;
}
static void fake_sprite_play_over(u64 sprite, const char *name, int force) {
    (void) name; (void) force;
    if (!sprite_alive(sprite)) { g_dead_calls++; CHECK(0, "Play (overlay) on a deleted sprite %llx", sprite); return; }
    *(unsigned char *) (sprite + STATE_OVERLAY + STATE_PLAYING) = 1;
}

static void thunk(u64 at, void *fn) {
    unsigned char *t = (unsigned char *) (g_base + at);
    t[0] = 0x48; t[1] = 0xB8;               /* movabs rax, fn */
    memcpy(t + 2, &fn, 8);
    t[10] = 0xFF; t[11] = 0xE0;             /* jmp rax */
}

static LONG CALLBACK on_fault(EXCEPTION_POINTERS *e) {
    if (e->ExceptionRecord->ExceptionCode != EXCEPTION_ACCESS_VIOLATION) return EXCEPTION_CONTINUE_SEARCH;
    u64 at = (u64) e->ExceptionRecord->ExceptionInformation[1];
    const char *what = "unknown memory";
    for (int i = 0; i < g_nblocks; i++) {
        if (at >= g_blocks[i].at && at < g_blocks[i].at + PLAYER_BYTES) what = "a DELETED player";
        if (at >= g_blocks[i].buf && at < g_blocks[i].buf + COSTUME_BUF) what = "a deleted player's costume vector";
    }
    printf("  USE AFTER FREE: the dance read %llx, %s\n", at, what);
    fflush(stdout);
    TerminateProcess(GetCurrentProcess(), 3);
    return EXCEPTION_CONTINUE_SEARCH;
}

/* a room: a pedestal holding a quality-4 item (id 1), or nothing */
static u64 new_room(int with_item, int track) {
    u64 room = (u64) calloc(1, 0x2000);
    *(int *) (room + ROOM_TRACK) = track;
    u64 *ents = calloc(4, 8);
    unsigned n = 0;
    if (with_item) {
        u64 e = (u64) calloc(1, 0x600);
        int *tvs = (int *) (e + ENTITY_TYPE);
        tvs[0] = 5; tvs[1] = 100; tvs[2] = 1;
        *(unsigned char *) (e + ENTITY_ALIVE) = 1;
        ents[n++] = e;
    }
    *(u64 *) (room + ROOM_ENTITIES + LIST_ARRAY) = (u64) ents;
    *(unsigned *) (room + ROOM_ENTITIES + LIST_COUNT) = n;
    return room;
}

static void enter_room(int with_item, int track) {
    g_room = new_room(with_item, track);
    *(u64 *) (g_game + GAME_ROOM) = g_room;
    fake_room_music(g_room);
    g_room_music_calls--;                   /* the game's own, not ours */
}

static void take_item(void) {
    unsigned n = *(unsigned *) (g_room + ROOM_ENTITIES + LIST_COUNT);
    u64 *ents = *(u64 **) (g_room + ROOM_ENTITIES + LIST_ARRAY);
    for (unsigned i = 0; i < n; i++) *(unsigned char *) (ents[i] + ENTITY_ALIVE) = 0;
}

static int music_now(void) { return *(int *) (g_manager + MGR_MUSIC + MUSIC_NOW); }
static int music_ours(void) { return music_now() >= g_track_first && music_now() < g_track_first + T_COUNT; }

/* the game's frame: every costume's head animation set again, stopped (0x2fcdd0, no HeadUp) */
static void game_frame(void) {
    for (int i = 0; i < g_np; i++) {
        u64 p = g_plist[i];
        u64 b = *(u64 *) (p + PLAYER_COSTUMES), e = *(u64 *) (p + PLAYER_COSTUMES + 8);
        for (u64 c = b; c < e; c += COSTUME_RECORD) *(unsigned char *) (c + STATE_MAIN + STATE_PLAYING) = 0;
    }
}

static void pass(void) { shim_mods_between_frames(); }
static void frames(int n) { for (int i = 0; i < n; i++) { game_frame(); pass(); } }

/* everything that must hold after a pass in a run */
static void invariants(const char *when) {
    if (STATE != 2) return;
    for (int i = 0; i < g_np; i++) {
        u64 p = g_plist[i];
        int dancer = 0;
        for (int k = 0; k < DANCERS; k++) if (g_dancing && g_dancer[k] == p) dancer = 1;
        int worn = ours_worn(p);
        if (g_mod_on) CHECK(dancer ? worn >= 1 : worn == 0, "%s: player %d wears %d of ours, dancer %d",
                            when, i, worn, dancer);
    }
    for (int k = 0; k < DANCERS; k++)
        if (g_dancing && g_dancer[k]) CHECK(alive(g_dancer[k]), "%s: a dancer is a deleted player", when);
    CHECK(!music_ours() || g_dancing || g_music_ours, "%s: our track %d plays and nobody will put the room's back",
          when, music_now());
    CHECK(g_dead_calls == 0, "%s: %d game calls on deleted players", when, g_dead_calls);
}

static void setup(int players) {
    AddVectoredExceptionHandler(1, on_fault);
    g_base = (u64) VirtualAlloc(NULL, 0xB00000, MEM_RESERVE | MEM_COMMIT, PAGE_EXECUTE_READWRITE);
    g_manager = (u64) calloc(1, 0x60000);
    g_game = (u64) calloc(1, 0x26000);
    *(u64 *) (g_base + MANAGER_PTR) = g_manager;
    *(u64 *) (g_base + GAME_PTR) = g_game;
    thunk(FN_LOAD_COSTUMES, (void *) fake_load_costumes);
    thunk(FN_LOAD_MUSIC, (void *) fake_load_music);
    thunk(FN_NULL_ITEM, (void *) fake_null_item);
    thunk(FN_COLLECTIBLE, (void *) fake_collectible);
    thunk(FN_ADD_COSTUME, (void *) fake_add_costume);
    thunk(FN_REMOVE_COSTUME, (void *) fake_remove_costume);
    thunk(FN_PLAY_MUSIC, (void *) fake_play_music);
    thunk(FN_ROOM_MUSIC, (void *) fake_room_music);
    thunk(FN_CURSES, (void *) fake_curses);
    thunk(FN_SPRITE_PLAY, (void *) fake_sprite_play);
    thunk(FN_SPRITE_PLAY_OVER, (void *) fake_sprite_play_over);
    FlushInstructionCache(GetCurrentProcess(), (void *) g_base, 0xB00000);
    for (int i = 0; i < 256; i++) {
        *(int *) (g_null_rec[i] + ITEM_ID) = i;
        g_null_items[i] = (u64) g_null_rec[i];
    }
    *(int *) (g_collectible[1] + ITEM_QUALITY) = 4;
    u64 *nulls = (u64 *) (g_manager + MGR_ITEM_CONFIG + CONFIG_NULLS);
    nulls[0] = (u64) g_null_items; nulls[1] = nulls[0] + 132 * 8;
    static unsigned char tracks[0x70 * 130];
    u64 *tv = (u64 *) (g_manager + MGR_MUSIC + MUSIC_TRACKS);
    tv[0] = (u64) tracks; tv[1] = tv[0] + 119 * 0x70;
    STATE = 3;                              /* the menu */
    shim_mods_path_asked("resources/repentance-resources/gfx/ui/bossportraits.xml");
    pass();
    CHECK(g_dance_state == 1, "the dance did not load");
    sync_players();
    /* a run: the players, a room with a good item */
    for (int i = 0; i < players; i++) player_add(new_player(i == 0 ? 0 : 3, 0));
    STATE = 2;
    enter_room(1, 3);
    frames(3);
    CHECK(g_dancing && music_ours(), "the dance did not start (dancing %d, track %d)", g_dancing, music_now());
    for (int i = 0; i < g_np; i++) CHECK(ours_worn(g_plist[i]) == 1, "player %d not dressed", i);
    frames(5);
    CHECK(g_plays > 0, "the costume's loop was never played");
    invariants("set up");
}

/* ------------------------------------------------------------------- roads */
static void road_menu(void) {
    setup(1);
    /* the manager's update: 0x6716dc -> ... -> 0x5671e8, then state 3 */
    players_clear();
    STATE = 3;
    *(int *) (g_manager + MGR_MUSIC + MUSIC_NOW) = 50;     /* the title's music */
    frames(30);
    CHECK(!g_dancing && !g_music_ours, "the dance outlived the run");
    /* a new run from the menu: Isaac in a room without a good item */
    player_add(new_player(0, 0));
    STATE = 2;
    enter_room(0, 2);
    frames(30);
    invariants("a new run after the menu");
    CHECK(g_room_music_calls == 0, "the room's music was put back %d times with nothing to put back",
          g_room_music_calls);
}

static void road_savequit(void) {
    setup(1);
    players_clear();
    STATE = 3;
    *(int *) (g_manager + MGR_MUSIC + MUSIC_NOW) = 50;
    frames(30);
    /* Continue: RestoreGameState puts the saved costumes back, ours among them (0x52554c..0x5255e4) */
    u64 p = new_player(0, 0);
    player_add(p);
    fake_add_costume(p, g_null_items[132 + C_ISAAC], 0);
    STATE = 2;
    enter_room(1, 3);                       /* the room saved with its good item */
    frames(3);
    invariants("continued in the dancing room");
    CHECK(g_dancing && ours_worn(p) == 1, "continued: dancing %d, ours worn %d", g_dancing, ours_worn(p));
    take_item();
    frames(3);
    invariants("the item taken after Continue");
    CHECK(!g_dancing && music_now() == 3, "the item taken: dancing %d, track %d", g_dancing, music_now());
}

static void road_restart(int reuse, int good_room) {
    setup(1);
    u64 old = g_plist[0];
    int calls = g_room_music_calls;
    /* one update: 0x671348 -> 0x672830 -> 0x5671e8, state 2 again, the new run */
    players_clear();
    STATE = 2;
    player_add(new_player(0, reuse ? old : 0));
    if (good_room) enter_room(1, 1);
    else {
        /* the same Room object, the new run's start room in it, its music */
        *(unsigned *) (g_room + ROOM_ENTITIES + LIST_COUNT) = 0;
        *(int *) (g_room + ROOM_TRACK) = 1;
        *(int *) (g_manager + MGR_MUSIC + MUSIC_NOW) = 1;
    }
    frames(30);
    invariants("after the restart");
    if (good_room) {
        CHECK(g_dancing && ours_worn(g_plist[0]) == 1, "the new run's dance: dancing %d", g_dancing);
        take_item();
        frames(3);
        invariants("the item taken in the new run");
        CHECK(music_now() == 1, "the room's music not back: %d", music_now());
    } else {
        CHECK(!g_dancing && music_now() == 1, "restart: dancing %d, track %d", g_dancing, music_now());
        CHECK(g_room_music_calls == calls, "the new run's music was played over by ours");
    }
}

static void road_minimise(void) {
    setup(1);
    /* the save on the way out: nobody deleted; the costume vectors moved, the old buffers freed */
    for (int i = 0; i < g_np; i++) {
        u64 p = g_plist[i];
        block_t *k = block_of(p);
        u64 nb = (u64) VirtualAlloc(NULL, COSTUME_BUF, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
        u64 *v = (u64 *) (p + PLAYER_COSTUMES);
        memcpy((void *) nb, (void *) v[0], (size_t) (v[1] - v[0]));
        v[1] = nb + (v[1] - v[0]);
        v[0] = nb;
        VirtualFree((void *) k->buf, 0, MEM_RELEASE);
        k->buf = nb;
    }
    frames(30);
    invariants("back from the background");
    CHECK(g_dancing, "the dance stopped over a minimise");
    /* and out to the menu after all */
    players_clear();
    STATE = 3;
    frames(10);
    CHECK(!g_dancing, "the dance outlived the run");
}

static void road_coopleave(void) {
    setup(2);
    player_remove(1);                       /* the second player leaves: deleted */
    frames(30);
    invariants("a co-op dancer left");
    CHECK(g_dancing && ours_worn(g_plist[0]) == 1, "the first player stopped dancing");
    take_item();
    frames(3);
    invariants("the item taken");
    CHECK(!g_dancing && ours_worn(g_plist[0]) == 0 && music_now() == 3, "the end: dancing %d, worn %d, track %d",
          g_dancing, ours_worn(g_plist[0]), music_now());
}

static void road_floor(void) {
    setup(1);
    STATE = 5;
    frames(20);
    STATE = 2;
    enter_room(0, 4);
    frames(10);
    invariants("the next floor");
    CHECK(!g_dancing && ours_worn(g_plist[0]) == 0, "the next floor: dancing %d, worn %d", g_dancing,
          ours_worn(g_plist[0]));
}

static unsigned g_rs = 175;
static unsigned rnd(unsigned n) { g_rs ^= g_rs << 13; g_rs ^= g_rs >> 17; g_rs ^= g_rs << 5; return g_rs % n; }

static void road_fuzz(int steps) {
    setup(1);
    int counts[12] = {0};
    long dancing_passes = 0;
    for (int s = 0; s < steps; s++) {
        int e = (int) rnd(12);
        if (STATE == 3) e = rnd(2) ? 10 : 11;   /* from the menu: a new run, or Continue */
        counts[e]++;
        switch (e) {
        case 0: enter_room(1, 3); break;
        case 1: enter_room(0, 2); break;
        case 2: take_item(); break;
        case 3: if (g_np < 4) player_add(new_player((int) rnd(41), 0)); break;
        case 4: if (g_np > 1) player_remove(1 + (int) rnd((unsigned) g_np - 1)); break;
        case 5: {                               /* restart */
            u64 was[16]; int n = g_np;
            memcpy(was, g_plist, sizeof(was));
            players_clear();
            for (int i = 0; i < n; i++) player_add(new_player(0, rnd(2) ? was[i] : 0));
            if (rnd(2)) enter_room(0, 1);
            else { *(unsigned *) (g_room + ROOM_ENTITIES + LIST_COUNT) = 0; *(int *) (g_room + ROOM_TRACK) = 1;
                   *(int *) (g_manager + MGR_MUSIC + MUSIC_NOW) = 1; }
            break;
        }
        case 6: case 7:                         /* exit to the menu, save & quit, the game over */
            players_clear(); STATE = 3; *(int *) (g_manager + MGR_MUSIC + MUSIC_NOW) = 50; break;
        case 8: {                               /* minimise: costume vectors moved */
            for (int i = 0; i < g_np; i++) {
                u64 p = g_plist[i]; block_t *k = block_of(p);
                u64 nb = (u64) VirtualAlloc(NULL, COSTUME_BUF, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
                u64 *v = (u64 *) (p + PLAYER_COSTUMES);
                memcpy((void *) nb, (void *) v[0], (size_t) (v[1] - v[0]));
                v[1] = nb + (v[1] - v[0]); v[0] = nb;
                VirtualFree((void *) k->buf, 0, MEM_RELEASE); k->buf = nb;
            }
            break;
        }
        case 9:                                 /* a trapdoor */
            STATE = 5; frames(1 + (int) rnd(5)); STATE = 2; enter_room((int) rnd(2), 4); break;
        case 10: case 11: {                     /* a new run or Continue, from the menu */
            if (STATE != 3) { g_mod_on = !g_mod_on; break; }   /* in a run: the switch in MODS */
            int n = 1 + (int) rnd(2);
            for (int i = 0; i < n; i++) {
                u64 p = new_player((int) rnd(41), 0);
                if (e == 11 && rnd(2)) fake_add_costume(p, g_null_items[132 + (int) rnd(C_COUNT)], 0);
                player_add(p);
            }
            STATE = 2;
            enter_room((int) rnd(2), 3);
            break;
        }
        }
        int k = 1 + (int) rnd(4);
        for (int i = 0; i < k; i++) { game_frame(); pass(); invariants("fuzz"); dancing_passes += g_dancing; }
        if (g_fail) { printf("  stopped at step %d (event %d)\n", s, e); return; }
    }
    printf("  %d steps: rooms %d/%d, items taken %d, joins %d, leaves %d, restarts %d, exits %d, minimises %d, "
           "floors %d, runs/continues/switches %d/%d; %ld passes danced, %d Play calls\n", steps, counts[0], counts[1], counts[2], counts[3],
           counts[4], counts[5], counts[6] + counts[7], counts[8], counts[9], counts[10], counts[11],
           dancing_passes, g_plays);
}

int main(int argc, char **argv) {
    setvbuf(stdout, NULL, _IONBF, 0);
    const char *road = argc > 1 ? argv[1] : "menu";
    g_verbose = getenv("DANCE_VERBOSE") != NULL;
    if (!strcmp(road, "menu")) road_menu();
    else if (!strcmp(road, "savequit")) road_savequit();
    else if (!strcmp(road, "restart")) road_restart(0, 0);
    else if (!strcmp(road, "restart-same-address")) road_restart(1, 0);
    else if (!strcmp(road, "restart-into-dance")) road_restart(1, 1);
    else if (!strcmp(road, "minimise")) road_minimise();
    else if (!strcmp(road, "coopleave")) road_coopleave();
    else if (!strcmp(road, "floor")) road_floor();
    else if (!strcmp(road, "fuzz")) {
        g_rs = argc > 3 ? (unsigned) atoi(argv[3]) : 175;
        road_fuzz(argc > 2 ? atoi(argv[2]) : 2000);
    } else { printf("no road %s\n", road); return 2; }
    return g_fail ? 1 : 0;
}
