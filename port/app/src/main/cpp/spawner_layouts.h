/* Written by tools/spawner_layouts.py - do not edit by hand, run the tool. */
#ifndef SPAWNER_LAYOUTS_H
#define SPAWNER_LAYOUTS_H

/*
 * Bosses that are several entities, as their own boss rooms lay them out
 * (tools/spawner_layouts.py says why). A part is a cell of the room's
 * layout, x 0..12, y 0..6; the game puts cell (x, y) at
 * (80 + 40x, 160 + 40y). Parts in the room's own spawn order.
 */
typedef struct { unsigned short type, variant, subtype; signed char x, y; } sp_part_t;

#define SP_LAYOUTS 14
static const struct { unsigned short type, variant; unsigned char first, count; } SP_LAYOUT[SP_LAYOUTS] = {
    { 19, 0, 0, 4 },   /* 00.special rooms.xml variant 1020 shape 1; group sizes {4: 18, 5: 16, 3: 14, 2: 10, 8: 5} */
    { 19, 1, 4, 5 },   /* 00.special rooms.xml variant 3260 shape 1; group sizes {5: 15, 4: 10, 7: 5, 3: 4, 9: 2} */
    { 19, 2, 9, 6 },   /* 00.special rooms.xml variant 5200 shape 4; group sizes {6: 16} */
    { 19, 3, 15, 5 },   /* 00.special rooms.xml variant 5310 shape 4; group sizes {5: 8} */
    { 918, 0, 20, 5 },   /* 00.special rooms.xml variant 5320 shape 1; group sizes {5: 6} */
    { 28, 0, 25, 3 },   /* 00.special rooms.xml variant 1030 shape 1; group sizes {3: 26, 2: 1} */
    { 28, 1, 28, 3 },   /* 00.special rooms.xml variant 1100 shape 1; group sizes {3: 16} */
    { 28, 2, 31, 3 },   /* 00.special rooms.xml variant 3270 shape 1; group sizes {3: 20} */
    { 45, 0, 34, 5 },   /* 00.special rooms.xml variant 1060 shape 1; group sizes {5: 6} */
    { 69, 1, 39, 2 },   /* 00.special rooms.xml variant 3310 shape 1; group sizes {2: 10, 6: 1} */
    { 237, 1, 41, 2 },   /* 00.special rooms.xml variant 5140 shape 1; group sizes {2: 20, 3: 7, 1: 3, 4: 2, 5: 1} */
    { 237, 2, 43, 2 },   /* 00.special rooms.xml variant 5146 shape 1; group sizes {2: 8, 4: 2} */
    { 82, 0, 45, 2 },   /* 00.special rooms.xml variant 4050 shape 1; group sizes {1: 5, 2: 2} */
    { 915, 0, 47, 5 },   /* 00.special rooms.xml variant 5260 shape 1; group sizes {1: 8} */
};

static const sp_part_t SP_LAYOUT_PART[] = {
    { 19, 0, 0, 6, 2 },
    { 19, 0, 0, 7, 2 },
    { 19, 0, 0, 8, 2 },
    { 19, 0, 0, 9, 2 },
    { 19, 1, 0, 6, 2 },
    { 19, 1, 0, 7, 2 },
    { 19, 1, 0, 8, 2 },
    { 19, 1, 0, 9, 2 },
    { 19, 1, 0, 10, 2 },
    { 19, 2, 0, 3, 4 },
    { 19, 2, 0, 4, 4 },
    { 19, 2, 0, 5, 4 },
    { 19, 2, 0, 6, 4 },
    { 19, 2, 0, 7, 4 },
    { 19, 2, 0, 8, 4 },
    { 19, 3, 0, 5, 4 },
    { 19, 3, 0, 6, 4 },
    { 19, 3, 0, 7, 4 },
    { 19, 3, 0, 8, 4 },
    { 19, 3, 0, 9, 4 },
    { 918, 0, 0, 4, 3 },
    { 918, 0, 0, 5, 3 },
    { 918, 0, 0, 6, 3 },
    { 918, 0, 0, 7, 3 },
    { 918, 0, 0, 8, 3 },
    { 28, 0, 0, 5, 3 },
    { 28, 0, 0, 6, 3 },
    { 28, 0, 0, 7, 3 },
    { 28, 1, 0, 5, 3 },
    { 28, 1, 0, 6, 3 },
    { 28, 1, 0, 7, 3 },
    { 28, 2, 0, 4, 2 },
    { 28, 2, 0, 4, 3 },
    { 28, 2, 0, 4, 4 },
    { 45, 0, 0, 6, 0 },
    { 45, 0, 0, 0, 3 },
    { 45, 0, 0, 6, 3 },
    { 45, 0, 0, 12, 3 },
    { 45, 0, 0, 6, 6 },
    { 69, 1, 0, 5, 3 },
    { 69, 1, 0, 7, 3 },
    { 237, 1, 0, 3, 2 },
    { 237, 1, 0, 9, 4 },
    { 237, 2, 0, 3, 2 },
    { 237, 2, 0, 9, 4 },
    { 82, 0, 0, 4, 2 },
    { 83, 0, 0, 7, 2 },
    { 915, 1, 0, 4, 2 },
    { 915, 1, 0, 9, 2 },
    { 915, 0, 0, 6, 3 },
    { 915, 1, 0, 3, 4 },
    { 915, 1, 0, 8, 4 },
};

#endif
