/*
 * The players' HUD as Repentance+ 1.9.7 lays it out offline - see hudgeom.h.
 * Every number is the PC code's; the addresses are isaac-ng.exe 1.9.7.17's.
 */
#include "hudgeom.h"

typedef struct { float x, y; } v2;

static void add(hg_out *o, int op, int entry, int slot, float x, float y, float s, int index, int name_right) {
    if (o->nops >= HG_OPS) return;
    hg_op *p = &o->op[o->nops++];
    p->op = op; p->entry = entry; p->slot = slot; p->x = x; p->y = y; p->s = s;
    p->index = index; p->name_right = name_right;
    p->more = 0;
}

/* a pocket of a layout but the full one: the pockets behind it in Rep+'s row */
static void pocket_row(hg_out *o, int entry, float x, float y, float s, int index, int name_right) {
    add(o, HG_OP_POCKET, entry, 0, x, y, s, index, name_right);
    if (o->nops) o->op[o->nops - 1].more = 1;
}

static int twins(int type) { return type == HG_TYPE_JACOB || type == HG_TYPE_ESAU; }

/* 0x9a6570: the layout of entry k (k and k + 4 share their player's rules) */
static int layout_of(const hg_in *in, int k, int n) {
    int jae = twins(in->type[k]);
    if (k == 0 || k == 4) {
        if (jae) return n > 3 ? HG_TWINS_COMPACT : HG_TWINS_FULL;
        return n >= 3 ? HG_COMPACT : HG_FULL;
    }
    if (jae) return n <= 2 ? HG_TWINS_FULL : HG_TWINS_COMPACT;
    return HG_COMPACT;
}

/*
 * 0x9a8230: the insets and the five corners (f = 1 - HudOffset), negative from
 * the right / bottom edge. Entry 0's and 1's y is the image's 24, not Rep+'s 30.
 */
static void insets(float f, v2 *I) {
    I[0].x = -20 * f; I[0].y = -12 * f;           /* top left */
    I[1].x = 24 * f;  I[1].y = -12 * f;           /* top right */
    I[2].x = -22 * f; I[2].y = 16 * f;            /* bottom left */
    I[3].x = 16 * f;  I[3].y = 6 * f;             /* bottom right */
}

/* 0x9a6140: entry k's corner under layout L */
static v2 corner(const hg_in *in, int k, int L) {
    v2 I[4], C[5], p;
    float f = in->f;
    insets(f, I);
    C[0].x = I[0].x + 68;  C[0].y = I[0].y + HG_TOP_Y;
    C[1].x = I[1].x - 151; C[1].y = I[1].y + HG_TOP_Y;
    C[2].x = I[2].x + 80;  C[2].y = I[3].y - 33;  /* 0x9a8346 / 0x9a8355: y from the bottom-right inset */
    C[3].x = I[3].x - 151; C[3].y = I[3].y - 33;
    C[4].x = I[3].x - 64;  C[4].y = I[3].y - 33;
    if (k < 4) p = C[k];
    else p = L == HG_TWINS_FULL ? C[4] : C[k - 4];             /* a twin: the full layout's own corner, else his owner's */
    if (L == HG_TWINS_COMPACT && (k == 2 || k == 3 || k == 6 || k == 7))
        p.y -= 6 * f + 16;                                       /* the pair at the bottom: room for the twin under */
    if (p.x < 0) p.x += in->W;
    if (p.y < 0) p.y += in->H;
    if (in->greed) p.x += (k & 1) ? -12.0f : 12.0f;
    if (k > 1) {
        int t = in->type[k];
        if (t == HG_TYPE_T_ISAAC || t == HG_TYPE_T_BLUEBABY) p.y -= 22;   /* room for the grid / the spells under */
        else if (t == HG_TYPE_T_CAIN) p.y -= 18;                          /* room for the bag's row */
    }
    return p;
}

/*
 * The actives: 0x8453c0 moves a player's actives 12 to the left in Greed
 * (0x8455d9..0x845607) - the image's renderer does not, so the place it is handed
 * carries it.
 */
static void active(const hg_in *in, hg_out *o, int k, int slot, float x, float y, float s, int index) {
    add(o, HG_OP_ACTIVE, k, slot, x - (in->greed ? 12.0f : 0.0f), y, s, index, 0);
}

/* a compact row's trinket: 0x846ff0 with its flag set takes the place as the picture's centre (0x847025) */
static void trinket_centred(hg_out *o, int k, int slot, float x, float y, float s, int index) {
    add(o, HG_OP_TRINKET, k, slot, x - HG_TRINKET_HALF * s, y - HG_TRINKET_HALF * s, s, index, 0);
}

/* 0x9a6fe0: entry k under layout L */
static void draw(const hg_in *in, hg_out *o, int k, int L) {
    v2 I[4], c = corner(in, k, L);
    insets(in->f, I);
    int t = in->type[k];
    o->corner[k].x = c.x; o->corner[k].y = c.y;
    int twin_half = L == HG_TWINS_COMPACT && k >= 4;
    /* 0x843df0: the second of a compact pair has its hearts 34 lower (0x8440e3..0x844113) */
    add(o, HG_OP_HEARTS, k, 0, c.x, c.y + (twin_half ? 34.0f : 0.0f), 1.0f, L == HG_TWINS_FULL && k >= 4 ? 4 : 0, 0);
    if (L == HG_FULL) {                                          /* 0x9a71f5..0x9a7429 */
        active(in, o, k, 1, c.x - 53, c.y - 12, 1.0f, 0);
        active(in, o, k, 0, c.x - 44, c.y - 12, 1.0f, 0);
        add(o, HG_OP_TRINKET, k, 1, I[2].x + 20, in->H - 52 - 16 + I[2].y, 1.0f, 0, 0);
        add(o, HG_OP_TRINKET, k, 0, I[2].x + 44, in->H - 28 - 16 + I[2].y, 1.0f, 0, 0);
        if (t == HG_TYPE_T_ISAAC) add(o, HG_OP_GRID, k, 0, I[0].x + 58, I[0].y + 45, 1.0f, 0, 0);
        if (t == HG_TYPE_T_BLUEBABY) add(o, HG_OP_SPELLS, k, 0, I[0].x + 58, I[0].y + 49, 1.0f, 0, 0);
        add(o, HG_OP_POCKET, k, 0, in->W - 28 + I[3].x, in->H - 18 + I[3].y, 1.0f, 0, 0);
        return;
    }
    if (L == HG_TWINS_FULL && k >= 4) {                          /* 0x9a744b..0x9a75b2: Esau, bottom right */
        active(in, o, k, 1, c.x + 28, c.y - 16, 1.0f, 4);
        active(in, o, k, 0, c.x + 12, c.y - 12, 1.0f, 4);
        float x = c.x;
        for (int s = 1; s >= 0; s--)
            if (in->trinket[k][s]) { trinket_centred(o, k, s, x, c.y + 20, 0.5f, 4); x -= 16; }
        pocket_row(o, k, x, c.y + 21, 0.5f, 4, 0);
        return;
    }
    if (L == HG_TWINS_COMPACT) {                                 /* 0x9a75c6..0x9a7767 */
        float cy = c.y + (k >= 4 ? 32.0f : 0.0f);
        active(in, o, k, 1, c.x - 34, cy - 8, 0.5f, 0);
        active(in, o, k, 0, c.x - 25, cy - 8, 0.5f, 0);
        float x = c.x - 12;
        for (int s = 1; s >= 0; s--)
            if (in->trinket[k][s]) { trinket_centred(o, k, s, x, cy + 20, 0.5f, 0); x += 16; }
        pocket_row(o, k, x - 2, cy + 21, 0.5f, 0, 1);
        return;
    }
    /* HG_COMPACT, and Jacob of HG_TWINS_FULL: 0x9a776c..0x9a79a1 */
    active(in, o, k, 1, c.x - 45, c.y - 16, 1.0f, 0);
    active(in, o, k, 0, c.x - 44, c.y - 12, 1.0f, 0);
    float x = c.x;
    int any = 0;
    for (int s = 1; s >= 0; s--)
        if (in->trinket[k][s]) { trinket_centred(o, k, s, x, c.y + 22, 0.5f, 0); x += 16; any = 1; }
    if (t == HG_TYPE_T_ISAAC) add(o, HG_OP_GRID, k, 0, c.x + I[0].x, c.y + 33 + I[0].y, 1.0f, 0, 0);
    if (t == HG_TYPE_T_BLUEBABY) add(o, HG_OP_SPELLS, k, 0, c.x + I[0].x, c.y + 39 + I[0].y, 1.0f, 0, 0);
    pocket_row(o, k, x - 3 - (any ? 3.0f : 0.0f), c.y + 23, 0.5f, 0, 1);
}

void hg_layout(const hg_in *in, hg_out *out) {
    *out = (hg_out) { 0 };
    for (int k = 0; k < HG_ENTRIES; k++) out->n += in->present[k] != 0;
    /* one entry: the image's own HUD, untouched */
    if (out->n < 2) return;
    for (int k = 0; k < HG_ENTRIES; k++) {
        if (!in->present[k]) continue;
        int L = layout_of(in, k, out->n);
        out->layout[k] = L;
        /* player 1 at full: the image's own loop draws him as Rep+ does - but in Greed (Rep+ moves the
           corner, 0x9a6277), as T.??? (Rep+'s spells 4 lower, 0x9a73bd: 49 against the image's 45) and as
           T.Cain (Rep+ writes no name under the bag, 0x8484ea) */
        if (k == 0 && L == HG_FULL && !in->greed && in->type[0] != HG_TYPE_T_BLUEBABY && in->type[0] != HG_TYPE_T_CAIN)
            continue;
        out->draw[k] = 1;
        draw(in, out, k, L);
    }
    for (int k = 0; k < HG_ENTRIES; k++) out->mine |= out->draw[k];
}
