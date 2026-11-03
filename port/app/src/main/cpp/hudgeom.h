/*
 * The players' HUD as Repentance+ 1.9.7 lays it out offline (local co-op), for
 * the iOS image's own renderers (the research notes of 05.10.2026, the PC's
 * co-op HUD).
 *
 * Pure arithmetic - no includes, no game memory - so the same file is built for
 * the phone (hudlayout.c draws by it) and on the desk (tools/hudesk: desk.exe
 * prints the calls, hudemu.py runs the image's renderers with them and pcemu.py
 * runs the PC game's own code for the same line-up; the two are compared).
 *
 * Everything below is the PC code's, address for address (isaac-ng.exe 1.9.7.17):
 *   layout choice 0x9a6570 (+ the entry count 0x9a8430), corners 0x9a8230 and
 *   0x9a6140, the per-player draw 0x9a6fe0. What is not taken: the online HUD
 *   (names, connection bars, OnlineHud's small hearts and bars) and the top
 *   corners' y of 30: kept at the image's 24 (the owner's word, 05.10.2026).
 * Units are the HUD canvas's (0xac24d8 / 0xac24dc).
 */
#ifndef HUDGEOM_H
#define HUDGEOM_H

#define HG_ENTRIES 8
#define HG_OPS 64

/* the layouts of 0x9a6570 */
enum { HG_FULL = 0, HG_TWINS_FULL = 1, HG_COMPACT = 2, HG_TWINS_COMPACT = 3 };

/* what a call draws: the image's renderers */
enum { HG_OP_HEARTS = 0, HG_OP_ACTIVE, HG_OP_TRINKET, HG_OP_POCKET, HG_OP_GRID, HG_OP_SPELLS };

#define HG_TYPE_JACOB      19
#define HG_TYPE_ESAU       20
#define HG_TYPE_T_ISAAC    21
#define HG_TYPE_T_CAIN     23
#define HG_TYPE_T_BLUEBABY 25

typedef struct { float x, y; } hg_pt;

typedef struct {
    float W, H;                 /* the canvas */
    float f;                    /* 1 - HudOffset, as the game keeps it at sp+0x214 */
    int greed;                  /* Greed / Greedier */
    int present[HG_ENTRIES];    /* the entry has a player the HUD is drawn for */
    int type[HG_ENTRIES];       /* the player's type */
    int trinket[HG_ENTRIES][2]; /* trinket slots 0 / 1 hold something (both id and picture) */
} hg_in;

typedef struct {
    int op;                     /* HG_OP_* */
    int entry;                  /* the HUD entry, 0..7 */
    int slot;                   /* actives and trinkets: 0 / 1 */
    float x, y;                 /* what the renderer is handed */
    float s;                    /* its scale (positive) */
    int index;                  /* the entry's number the renderer sees during the call (entry + 0x10) */
    int name_right;             /* pocket: the card's name right of it, else left */
    int more;                   /* pocket: the pockets behind the card in Rep+'s row (0x8473a5), not the image's column */
} hg_op;

typedef struct {
    int n;                      /* entries with a player (0x9a8430) */
    int mine;                   /* 1: the frame is drawn by these ops, else all the game's own */
    int layout[HG_ENTRIES];
    int draw[HG_ENTRIES];       /* drawn by us: the game's loop must skip the entry */
    hg_pt corner[HG_ENTRIES];   /* 0x9a6140's, for the report */
    int nops;
    hg_op op[HG_OPS];
} hg_out;

/*
 * The pockets behind the card (a second card, a pill, a pocket active) in the
 * Rep+ row of every layout but the full one (0x8473a5..0x8475de): pocket i
 * (1..3) at the card + (8 i s - 2, 12 s), half the card's size; a pocket
 * active at a further (-4 s, -8 s), at half the scale, active slot = its id - 1.
 */
#define HG_MORE_STEP 8.0f
#define HG_MORE_DY 12.0f

/* the PC trinket record's centre offset for the compact rows (+0x10/+0x14, half the 32-unit picture) */
#define HG_TRINKET_HALF 16.0f
/* the top corners' y: the image's (PC Rep+ has 30, there for the online name line) */
#define HG_TOP_Y 24.0f

void hg_layout(const hg_in *in, hg_out *out);

#endif
