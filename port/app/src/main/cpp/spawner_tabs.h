/* Written by tools/spawner_tabs.py - do not edit by hand, run the tool. */
#ifndef SPAWNER_TABS_H
#define SPAWNER_TABS_H

/*
 * gfx/port_spawner_tabs_<suffix>.anm2, one per language set in the order of
 * spawner_title.h (en ru de es jp kr zh). Every frame is drawn at 100 %, pivot at
 * its top left:
 *   "Tabs"   frame k = the word of tab k (0 items, 1 enemies, 2 teleport, 3 stats)
 *   "Stroke" frame k = the stroke under word k; dx, dy = its top left from the
 *            word's top left (centred, 2 below the word's bottom, as the mock-up)
 *   "Heads"  frame g = heading of group g (SP_GROUP_BOSS, _MINIBOSS, _MONSTER,
 *            _TRAINING)
 *   "Rule"   frame n = a stroke SP_RULE_STEP * (n + 1) long: the faint rule after
 *            a heading (the mock-up: alpha 0.45, 8 right of the word, its bottom
 *            one below the word's bottom); SP_RULE[n] is its real size
 */
#define SP_TABS 4
#define SP_HEADS 4
#define SP_RULE_FRAMES 25
#define SP_RULE_STEP 16

static const struct { short w, h; } SP_TAB[7][4] = {
    { { 48, 13 }, { 67, 15 }, { 79, 15 }, { 47, 13 } },   /* en */
    { { 80, 16 }, { 49, 14 }, { 77, 14 }, { 47, 14 } },   /* ru */
    { { 48, 13 }, { 61, 15 }, { 79, 15 }, { 51, 14 } },   /* de */
    { { 72, 13 }, { 83, 15 }, { 86, 14 }, { 47, 13 } },   /* es */
    { { 70, 16 }, { 33, 16 }, { 87, 16 }, { 86, 16 } },   /* jp */
    { { 52, 18 }, { 15, 19 }, { 70, 20 }, { 50, 20 } },   /* kr */
    { { 35, 17 }, { 36, 17 }, { 37, 18 }, { 37, 17 } },   /* zh */
};

static const struct { short w, h, dx, dy; } SP_STROKE[7][4] = {
    { { 54, 5, -3, 15 }, { 73, 5, -3, 17 }, { 85, 6, -3, 17 }, { 53, 5, -3, 15 } },   /* en */
    { { 86, 6, -3, 18 }, { 55, 5, -3, 16 }, { 83, 6, -3, 16 }, { 53, 5, -3, 16 } },   /* ru */
    { { 54, 5, -3, 15 }, { 67, 5, -3, 17 }, { 85, 6, -3, 17 }, { 57, 5, -3, 16 } },   /* de */
    { { 78, 5, -3, 15 }, { 89, 6, -3, 17 }, { 92, 6, -3, 16 }, { 53, 5, -3, 15 } },   /* es */
    { { 76, 5, -3, 18 }, { 39, 5, -3, 18 }, { 93, 6, -3, 18 }, { 92, 6, -3, 18 } },   /* jp */
    { { 58, 5, -3, 20 }, { 21, 5, -3, 21 }, { 76, 5, -3, 22 }, { 56, 5, -3, 22 } },   /* kr */
    { { 41, 5, -3, 19 }, { 42, 5, -3, 19 }, { 43, 5, -3, 20 }, { 43, 5, -3, 19 } },   /* zh */
};

static const struct { short w, h; } SP_HEAD[7][4] = {
    { { 61, 13 }, { 100, 14 }, { 86, 13 }, { 78, 15 } },   /* en */
    { { 50, 13 }, { 98, 13 }, { 75, 14 }, { 101, 14 } },   /* ru */
    { { 90, 13 }, { 142, 15 }, { 76, 14 }, { 78, 15 } },   /* de */
    { { 43, 14 }, { 82, 15 }, { 101, 14 }, { 132, 13 } },   /* es */
    { { 33, 16 }, { 87, 17 }, { 88, 16 }, { 87, 17 } },   /* jp */
    { { 32, 15 }, { 69, 15 }, { 50, 19 }, { 34, 20 } },   /* kr */
    { { 34, 17 }, { 52, 17 }, { 37, 18 }, { 35, 17 } },   /* zh */
};

#define SP_SEC_ITEMS 0
#define SP_SEC_KEYS 1
#define SP_SEC_BOMBS 2
#define SP_SEC_COINS 3
#define SP_SEC_HEARTS 4
#define SP_SEC_BATTERIES 5
#define SP_SEC_CARDS 6
#define SP_SEC_RUNES 7
#define SP_SEC_PILLS 8
#define SP_SEC_TRINKETS 9
#define SP_SEC_LOOT 10
#define SP_SEC_STATS 11
#define SP_SEC_PICKUPS 12
#define SP_SEC_HEALTH 13
#define SP_SECTIONS 14
static const struct { short w, h; } SP_SECTION[7][SP_SECTIONS] = {
    { { 48, 13 }, { 36, 14 }, { 57, 13 }, { 50, 13 }, { 58, 15 }, { 81, 14 }, { 47, 13 }, { 51, 14 }, { 46, 14 }, { 76, 14 }, { 46, 13 }, { 47, 13 }, { 63, 14 }, { 57, 14 } },   /* en */
    { { 80, 16 }, { 53, 14 }, { 57, 14 }, { 66, 14 }, { 56, 15 }, { 87, 15 }, { 49, 14 }, { 40, 15 }, { 63, 14 }, { 69, 14 }, { 27, 14 }, { 47, 14 }, { 104, 16 }, { 82, 15 } },   /* ru */
    { { 48, 13 }, { 88, 16 }, { 67, 14 }, { 66, 16 }, { 61, 14 }, { 83, 15 }, { 59, 13 }, { 53, 13 }, { 56, 15 }, { 127, 16 }, { 46, 13 }, { 51, 14 }, { 63, 14 }, { 97, 14 } },   /* de */
    { { 72, 13 }, { 54, 13 }, { 66, 15 }, { 74, 14 }, { 95, 14 }, { 73, 17 }, { 56, 14 }, { 52, 14 }, { 78, 18 }, { 83, 14 }, { 51, 16 }, { 47, 13 }, { 96, 15 }, { 48, 14 } },   /* es */
    { { 70, 16 }, { 35, 17 }, { 34, 16 }, { 51, 16 }, { 51, 16 }, { 51, 16 }, { 52, 16 }, { 52, 15 }, { 33, 16 }, { 102, 16 }, { 69, 17 }, { 86, 16 }, { 89, 17 }, { 86, 17 } },   /* jp */
    { { 52, 18 }, { 32, 20 }, { 34, 20 }, { 32, 19 }, { 34, 13 }, { 51, 15 }, { 33, 14 }, { 14, 20 }, { 34, 20 }, { 51, 18 }, { 52, 20 }, { 50, 20 }, { 33, 20 }, { 34, 20 } },   /* kr */
    { { 35, 17 }, { 36, 18 }, { 37, 18 }, { 36, 17 }, { 17, 12 }, { 36, 18 }, { 36, 17 }, { 36, 18 }, { 37, 17 }, { 37, 18 }, { 55, 17 }, { 37, 17 }, { 56, 17 }, { 36, 18 } },   /* zh */
};

/* the same in every set: the strokes do not depend on the language */
static const struct { short w, h; } SP_RULE[25] = {
    { 14, 5 }, { 30, 5 }, { 46, 5 }, { 62, 5 }, { 78, 5 },
    { 94, 6 }, { 110, 6 }, { 126, 6 }, { 142, 6 }, { 158, 6 },
    { 174, 6 }, { 190, 6 }, { 206, 6 }, { 222, 8 }, { 238, 8 },
    { 254, 8 }, { 270, 8 }, { 286, 8 }, { 302, 8 }, { 318, 8 },
    { 334, 8 }, { 350, 8 }, { 366, 8 }, { 382, 8 }, { 398, 8 },
};

#endif
