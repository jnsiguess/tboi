/*
 * THE PLAYERS' HUD AS REPENTANCE+ LAYS IT OUT OFFLINE (the owner's word,
 * 05.10.2026: "take everything from Rep+ but the online"; the research notes
 * of that day, the PC's co-op HUD). The places themselves live in hudgeom.c,
 * every one the PC
 * code's: the layout choice by the number of HUD entries, the corners
 * (W - 151 on the right, the top row kept at the image's 24), hearts six a row
 * for everyone, the compact rows of half-size trinkets and card under the
 * hearts, Esau of every co-op pair drawn under his Jacob, T.Isaac's grid and
 * T.???'s spells for every player, the bottom corners lifted for a pair (16),
 * T.Isaac / T.??? (22) and T.Cain (18), Greed's 12 inwards.
 *
 * One entry (a single player who is not Jacob) is never touched; neither is
 * player 1 at the full layout (two entries, not Greed, not T.???): the image
 * draws him exactly as Rep+ does.
 *
 * HOW, WITHOUT REWRITING THE GAME'S LOOP AND WITHOUT A WORD OF ITS CODE. The
 * player HUD's draw 0x6ed3b0 lays the eight entries' corners on its stack
 * (0x6ee4e0..0x6ee5d0, at x29 - 0xe0 + 8k) and its loop (0x6ee64c) skips an
 * entry whose corner is the (0, 0) of [0x95e7a0] (0x6ee674) - the way entries
 * 5..7 are always skipped. The hook at 0x6ee608, right after the last corner
 * and before the loop, gives the entries we draw that corner and draws them
 * with the image's own renderers - hearts 0x6e5f24, actives 0x6e6df8, trinkets
 * 0x6e82f0, the pocket 0x6e8bc0, the grid 0x6ea554, the spells 0x6eaa30 -
 * every one of which takes the place and the scale.
 *
 *   0x6ee608 sits right after `bl 0x8beb00`: x9..x17 are dead there, and the
 *   five words the hook copies (mov x26, #0; str s0, [sp, #0x118]; str s1,
 *   [sp, #0x11c]; add x8, x20, #0xc4, lsl #12; add x22, x8, #0xbf8) neither
 *   branch nor depend on their address; tools/into.py 0x6ee5ec 0x6ee64c: no
 *   entry from anywhere. loader.c checks all five before the hook goes in.
 *
 * THE ENTRY'S NUMBER. The renderers ask the entry's own number (entry + 0x10)
 * in six places: hearts six a row only for 0 and 4+ (0x6e5fbc..0x6e5fd0), the
 * hearts leftwards and their half-heart flipped for 4+ (0x6e60d4, 0x6e5fe4,
 * 0x6e69f0), the lives' "x%d" for 0 and 4+ (0x6e6a98), the charge bar left of
 * the active for 4+ (0x6e7b2c / 0x6e7b48), the trinket's and the card name's
 * side by the scale's sign and, for 1+, the pair (0x6e868c, 0x6e8c24). Rep+
 * asks none of that by the number: six a row, rightwards, the bar right and the
 * lives' text for everyone; leftwards with the bar on the left only for Esau of
 * the full J&E layout (0x844113 / 0x8467a0: entry 4+ AND layout 1). So for the
 * call the entry is handed the number whose answers are Rep+'s - 0, or 4 for
 * that Esau, who is entry 4 anyway - and its own number goes back at once.
 * Data, the frame's own thread, nothing of the image's code changes.
 *
 * VERIFIED ON THE DESK BY EMULATION, not by reading: tools/hudesk/hudcheck.py
 * runs desk.exe (this hudgeom.c), feeds every call through the image's own
 * renderers under unicorn (hudemu.py) and the PC game's own layout choice and
 * per-player draw (pcemu.py: 0x9a6570, 0x9a6fe0) on the same line-up, and
 * compares every sprite, picture and text by place and scale: 34 cases, all
 * matched but for two accepted differences (the 1-unit divider between a
 * compact pair, which the image has no primitive for; Esau's card name 2.5
 * units further left in the full J&E layout).
 *
 * THE REPORT (report builds): "hud blocks: ..." names the frame when it
 * changes, then one "hud piece: ..." line per call - the very words desk.exe
 * prints as "op ...", so a phone pass is compared number for number - with
 * where the renderer puts it on the screen (rules measured by emulation), and
 * "hud: SELF-CHECK hud blocks: OK|FAIL ...".
 */
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/mman.h>
#include <android/log.h>
#include "diag.h"
#include "hudgeom.h"

#if ISAAC_DIAG
#define LOG(...) __android_log_print(ANDROID_LOG_INFO, "isaac", __VA_ARGS__)
#else
#define LOG(...) do { } while (0)
#endif

unsigned long shim_image_base(void);
int shim_eid_readable(const void *p, unsigned long n);
int shim_report_active(void);
void report_line(const char *kind, const char *text);
int shim_twins_button_up(void);                /* twintrinkets.c */

#define GAME_PTR        0xac3b90
#define CANVAS_W_AT     0xac24d8
#define CANVAS_H_AT     0xac24dc
#define GAME_DIFFICULTY 0x24f9d8               /* Game +: 0 normal, 1 hard, 2 Greed, 3 Greedier (eid.c) */
#define HUD_ENTRIES     0x47fe0                /* Game +: the HUD, its eight entries from +0 */
#define HUD_ENTRY       0x918
#define ENTRY_INDEX     0x10                   /* short: the entry's number, as the renderers read it */
#define ENTRY_TRINKET   0x318                  /* + slot * 0x18: the id, +8 the picture (0x6e831c / 0x6e8338) */
#define HUD_HEARTS_OBJ  0xc4bf8                /* HUD +: what 0x6e5f24 takes as x1 (0x6ee614) */
#define ENTRY_FADE      0x900                  /* float: the twins' fade (0x6ee714) */
#define JOIN_SLOTS      (0x25c40 + 0x5c)       /* Game +: four of 0x14, +8 the choice (-1: none) */
#define JOIN_SLOT_SIZE  0x14

#define P_TYPE          0x1738
#define P_TWIN          0x2580
#define P_ORDER         0x19f0
#define P_GONE          0x3c                   /* not nought: the loop stops after the hearts (0x6ee6e4) */
#define P_DIMMED        0x27e0                 /* byte: actives at 0.2 (0x6ee72c) */

#define P_POCKETS       0x1c10                 /* four of 8: the id, then the kind (0 card, 1 pill, 2 pocket active) */
#define ENTRY_CARDS     0x350                  /* + i * 0x160: the card sprite of pocket i (0x6e944c, 0x6e8cf4) */
#define CARD_STEP       0x160
#define SPRITE_SCALE    0xdc                   /* anm2: the scale vector (0x6e8cc0 sets it, 0x6e9738.. puts (1, 1) back) */
#define ZERO_VEC        0x95e7a0               /* the (0, 0) the renderers hand Render as both crops */
#define FN_SET_FRAME    0x89eab8               /* anm2: (sprite, name, frame) */
#define FN_RENDER       0x89e4c0               /* anm2: (sprite, &pos, &crop, &crop) */

#define FN_BAG_PREVIEW  0x6e9934               /* T.Cain's bag: (entry, &pos, scale), 0x6e932c */
#define ITEM_BAG        0x2c6                  /* Bag of Crafting */
#define P_ACTIVE_ID     0x1964                 /* + slot * 0x1c: the active's id (0x6e6e74) */

#define FN_HEARTS       0x6e5f24
#define FN_ACTIVE       0x6e6df8
#define FN_TRINKET      0x6e82f0
#define FN_POCKET       0x6e8bc0
#define FN_GRID         0x6ea554
#define FN_SPELLS       0x6eaa30

/* the frame of 0x6ed3b0, as the trace entry leaves it (trace.c, objc_asm.S) */
#define REGS_X29        208
#define REGS_SP         224
#define SP_THIS         0x20                   /* str x0, [sp, #0x20] (0x6ed3f8): the HUD */
#define SP_INSET        0x214                  /* 1 - HudOffset (0x6ed51c) */
#define X29_CORNERS     0xe0                   /* corners at x29 - 0xe0 + 8k */

typedef struct { float x, y; } v2;
typedef v2 (*hearts_fn)(unsigned long entry, unsigned long obj, const float *pos, float scale);
typedef void (*active_fn)(unsigned long entry, int slot, const float *pos, float scale, float alpha, int w3);
typedef void (*trinket_fn)(unsigned long entry, int slot, const float *pos, float scale);
typedef void (*pocket_fn)(unsigned long entry, const float *pos, float scale, float alpha);
typedef void (*extra_fn)(unsigned long entry, const float *pos, float scale);
typedef int (*set_frame_fn)(unsigned long sprite, const char *name, int frame);
typedef void (*bag_fn)(unsigned long entry, const float *pos, float scale);
typedef void (*render_fn)(unsigned long sprite, const float *pos, const float *crop0, const float *crop1);

/*
 * Rep+'s row of the pockets behind the card (hudgeom.h, HG_MORE_*): the image's
 * renderer has drawn the card alone (their ids hidden for the call), setting the
 * alpha of all four card sprites (their scale it puts back to 1); the cards and pills are drawn
 * here as it draws them - SetFrame "HUDSmall" and Render on pocket i's own
 * sprite (0x6e8d5c..0x6e8d9c) - in Rep+'s places, a pocket active by the
 * actives' renderer as the image does (0x6e8da8..0x6e8e20).
 */
static void pockets_behind(unsigned long b, unsigned long entry, unsigned long p, const hg_op *op, float a_act) {
    const int *slots = (const int *) (p + P_POCKETS);
    float s = op->s;
    for (int i = 1; i < 4; i++) {
        int id = slots[i * 2], kind = slots[i * 2 + 1];
        if (!id) continue;
        float pos[2] = { op->x + HG_MORE_STEP * (float) i * s - 2.0f, op->y + HG_MORE_DY * s };
        if (kind < 2) {
            unsigned long spr = entry + ENTRY_CARDS + (unsigned long) i * CARD_STEP;
            float *scale = (float *) (spr + SPRITE_SCALE);
            ((set_frame_fn) (b + FN_SET_FRAME))(spr, "HUDSmall", 0);
            scale[0] = s; scale[1] = s;                    /* the renderer's own, put back to (1, 1) when it ends */
            ((render_fn) (b + FN_RENDER))(spr, pos, (const float *) (b + ZERO_VEC), (const float *) (b + ZERO_VEC));
            scale[0] = 1.0f; scale[1] = 1.0f;
        } else if (kind == 2) {
            pos[0] -= 4.0f * s;
            pos[1] -= 8.0f * s;
            ((active_fn) (b + FN_ACTIVE))(entry, id - 1, pos, 0.5f * s, a_act, 0);
        }
    }
}

static unsigned long player_of(unsigned long hud, int k) {
    return *(const unsigned long *) (hud + (unsigned long) k * HUD_ENTRY);
}

static int readable_player(unsigned long p) {
    return p && shim_eid_readable((const void *) p, P_DIMMED + 8);
}

/* the first of his pair, or alone: what 0x6e8c30..0x6e8c58 compares */
static int main_one(unsigned long p) {
    unsigned long twin = *(const unsigned long *) (p + P_TWIN);
    if (!twin || twin == p || !shim_eid_readable((const void *) (twin + P_ORDER), 4)) return 1;
    return !(*(const int *) (twin + P_ORDER) < *(const int *) (p + P_ORDER));
}

/*
 * The card's name (0x6e9200..0x6e96c4): left of the card, or right when
 * flipped; flipped is (scale < 0) XOR (number >= 1 and the first of his pair).
 * The renderer draws at |scale|, so the side is the scale's sign alone.
 */
static float card_scale(int index, unsigned long p, float s, int name_right) {
    int flip_if_positive = index >= 1 && main_one(p);
    return flip_if_positive == name_right ? s : -s;
}

/* the alphas the loop gives (0x6ee6ec..0x6ee734): the card's, the actives' */
static void alphas(unsigned long entry, unsigned long p, float *card, float *act) {
    int type = *(const int *) (p + P_TYPE);
    float s0 = 1.0f, s8 = 1.0f;
    if ((type == HG_TYPE_JACOB || type == HG_TYPE_ESAU) && *(const unsigned long *) (p + P_TWIN)) {
        float fade = *(const float *) (entry + ENTRY_FADE);
        s0 = 1.0f + fade * -0.7f;
        s8 = 0.3f + fade * 0.7f;
    }
    *card = s8;
    *act = *(const unsigned char *) (p + P_DIMMED) ? 0.2f : s0;
}

/*
 * A POCKET ACTIVE IN FRONT (the Bag of Crafting, Dark Arts, Flip, Hold...) in
 * every layout but the full one: Rep+ puts the active at the card's place
 * + (-9, -8) (0x84830d..0x84831d), its name from + 19 s (13 + 6) at y - 8 s
 * (0x848325, 0x84738f), and T.Cain's bag preview at + (69 s, 27 s)
 * (0x84838e..0x8483ff). The image's renderer has the full layout's numbers in
 * six words of its own - the active at + (-24, -18) (0x6e9280 / 0x6e9284), the
 * name 4 further out (0x6e9260) at y - 10 s (0x6e920c / 0x6e9224), the bag's
 * preview from its own place and a name under it (0x6e92cc; Rep+ writes none,
 * 0x848404) - and nothing else there to hand them in. So for exactly that one
 * call those words hold Rep+'s, the bag's preview is drawn here from Rep+'s
 * place, and the words go back. (Its eight ingredient slots the image's
 * preview spaces 12 apart at any scale, Rep+'s 12 x scale: left as the image
 * draws them.) At the full layout everything is Rep+'s already but the bag's
 * name, which Rep+ never writes: one word skips it (PA_BAG_FULL).
 * Player 1 alone (one entry) is the game's: there the image writes the name. Each is
 * checked for one of its two values first; a stranger: they are left alone for
 * good and the active stays where the image puts it.
 */
typedef struct { unsigned long at; uint32_t v[4]; } word_t;   /* v[mode] */
enum { PA_GAME = 0, PA_ACTIVE = 1, PA_BAG = 2, PA_BAG_FULL = 3 };
static const word_t PA_WORDS[] = {
    /*            game         active       bag (a row)  bag (full) */
    { 0x6e920c, { 0x1E349001u, 0x1E341001u, 0x1E341001u, 0x1E349001u } },  /* fmov s1, #-10 -> #-8: name's y (left) */
    { 0x6e9224, { 0x1E349001u, 0x1E341001u, 0x1E341001u, 0x1E349001u } },  /* fmov s1, #-10 -> #-8: name's y (right) */
    { 0x6e9260, { 0x1E321001u, 0x1E231001u, 0x1E231001u, 0x1E321001u } },  /* fmov s1, #-4 -> #6: the name's x */
    { 0x6e9280, { 0x1E371000u, 0x1E345000u, 0x1E345000u, 0x1E371000u } },  /* fmov s0, #-24 -> #-9: the active's x */
    { 0x6e9284, { 0x1E365001u, 0x1E341001u, 0x1E341001u, 0x1E365001u } },  /* fmov s1, #-18 -> #-8: the active's y */
    /* the bag in a row: b.ne 0x6e951c -> b 0x6e9738, past its own preview and its name */
    { 0x6e92cc, { 0x54001281u, 0x54001281u, 0x1400011Bu, 0x54001281u } },
    /* the bag at full: its preview is Rep+'s (0x84849b: the same (-34, -3) s), its name is not - b 0x6e951c ->
       b 0x6e9738 after the preview (Rep+ empties the name, 0x8484ea) */
    { 0x6e9330, { 0x1400007Bu, 0x1400007Bu, 0x1400007Bu, 0x14000102u } },
};
#define PA_N (int) (sizeof(PA_WORDS) / sizeof(PA_WORDS[0]))
static int g_pa_on, g_pa_broken;

static uint32_t pa_word(const word_t *w, int mode) { return w->v[mode]; }

static void pa_words(unsigned long b, int on) {
    if (g_pa_broken || on == g_pa_on) return;
    for (int i = 0; i < PA_N; i++) {
        uint32_t now = *(const uint32_t *) (b + PA_WORDS[i].at);
        int known = 0;
        for (int m = 0; m < 4; m++) known |= now == PA_WORDS[i].v[m];
        if (!known) {
            g_pa_broken = 1;
            LOG("hud: the word at %lx is %08x, not the pocket renderer's - pocket actives stay where the image puts them",
                PA_WORDS[i].at, now);
            return;
        }
    }
    long page = sysconf(_SC_PAGESIZE);
    if (page <= 0) page = 4096;
    unsigned long lo = (b + PA_WORDS[0].at) & ~((unsigned long) page - 1);
    unsigned long hi = ((b + PA_WORDS[PA_N - 1].at + 4) + (unsigned long) page - 1) & ~((unsigned long) page - 1);
    if (mprotect((void *) lo, hi - lo, PROT_READ | PROT_WRITE | PROT_EXEC) != 0) {
        g_pa_broken = 1;
        LOG("hud: cannot open the pocket renderer's page - pocket actives stay where the image puts them");
        return;
    }
    for (int i = 0; i < PA_N; i++)
        *(uint32_t *) (b + PA_WORDS[i].at) = pa_word(&PA_WORDS[i], on);
    mprotect((void *) lo, hi - lo, PROT_READ | PROT_EXEC);
    __builtin___clear_cache((char *) lo, (char *) hi);
    g_pa_on = on;
}

/* one call, the entry handed the number Rep+'s answers come from (see THE ENTRY'S NUMBER) */
static void run_op(unsigned long b, unsigned long hud, const hg_op *op) {
    unsigned long entry = hud + (unsigned long) op->entry * HUD_ENTRY;
    unsigned long p = player_of(hud, op->entry);
    if (!readable_player(p)) return;
    if (op->op != HG_OP_HEARTS && *(const int *) (p + P_GONE)) return;      /* as the loop does (0x6ee6e4) */
    float pos[2] = { op->x, op->y };
    float a_card, a_act;
    alphas(entry, p, &a_card, &a_act);
    short *number = (short *) (entry + ENTRY_INDEX);
    short own = *number;
    *number = (short) op->index;
    switch (op->op) {
    case HG_OP_HEARTS:
        ((hearts_fn) (b + FN_HEARTS))(entry, hud + HUD_HEARTS_OBJ, pos, op->s);
        break;
    case HG_OP_ACTIVE:
        ((active_fn) (b + FN_ACTIVE))(entry, op->slot, pos, op->s, a_act, 0);
        break;
    case HG_OP_TRINKET:
        ((trinket_fn) (b + FN_TRINKET))(entry, op->slot, pos, op->s);
        break;
    case HG_OP_POCKET: {
        int *slots = (int *) (p + P_POCKETS), hidden[4] = { 0 };
        if (op->more)
            for (int i = 1; i < 4; i++) { hidden[i] = slots[i * 2]; slots[i * 2] = 0; }
        /* a pocket active in front, Rep+'s row: its places (see A POCKET ACTIVE IN FRONT) */
        int front = slots[1] == 2 && slots[0] >= 1 && slots[0] <= 4;
        int bag = front && *(const int *) (p + P_ACTIVE_ID + (unsigned long) (slots[0] - 1) * 0x1c) == ITEM_BAG;
        int pa = op->more ? (bag ? PA_BAG : (front ? PA_ACTIVE : PA_GAME)) : (bag ? PA_BAG_FULL : PA_GAME);
        /*
         * The words are switched only when this call needs others than the
         * last one left (hotfix 1.72): they used to be set and put back around
         * every pocket call - four mprotect and two cache flushes a player a
         * frame. Nothing but these calls runs the pocket renderer while the
         * frame's pieces are drawn, and the frame puts the game's words back
         * once at its end (shim_hud_layout), before the game's own loop draws
         * anything: every call sees exactly the words it saw before.
         */
        pa_words(b, pa);
        ((pocket_fn) (b + FN_POCKET))(entry, pos, card_scale(op->index, p, op->s, op->name_right), a_card);
        if (pa && !g_pa_broken && pa == PA_BAG) {
            float bag[2] = { op->x + 69.0f * op->s, op->y + 27.0f * op->s };
            ((bag_fn) (b + FN_BAG_PREVIEW))(entry, bag, op->s);   /* its own function: the words do not touch it */
        }
        if (op->more) {
            for (int i = 1; i < 4; i++) slots[i * 2] = hidden[i];
            pockets_behind(b, entry, p, op, a_act);
        }
        break;
    }
    case HG_OP_GRID:
        ((extra_fn) (b + FN_GRID))(entry, pos, op->s);
        break;
    case HG_OP_SPELLS:
        ((extra_fn) (b + FN_SPELLS))(entry, pos, op->s);
        break;
    }
    *number = own;
}

#if ISAAC_DIAG
static const char *OP_NAME[] = { "hearts", "active", "trinket", "pocket", "grid", "spells" };

/*
 * Where the renderer puts the piece on the screen, by the rules measured by
 * emulation (tools/hudesk/hudemu.py): the first heart's centre at the place,
 * rows of six, 12 by 10 times the scale; an active's picture from the place,
 * 32 x scale square, its bar at +(34, 17) x scale (+(0, 17) for number 4+;
 * the School Bag's second at half that size, its bar at +(-2, 17)); a
 * trinket's picture from the place, 32 x scale; the card centred on the place,
 * its name at y - 10 x scale, from x + 13 x scale rightwards or ending at
 * x - 16 x scale leftwards; the grid's slots from +(8, 8), 12 apart; the
 * spells' poops from +(8, 8), 12 apart.
 */
static void say_op(const hg_op *op, const hg_out *o) {
    char line[260];
    int n = snprintf(line, sizeof(line), "op %s entry %d slot %d at %.2f,%.2f scale %.2f index %d name %s more %d",
                     OP_NAME[op->op], op->entry, op->slot, (double) op->x, (double) op->y, (double) op->s, op->index,
                     op->name_right ? "right" : "left", op->more);
    float s = op->s;
    if (op->op == HG_OP_ACTIVE && n < (int) sizeof(line)) {
        /* the School Bag's second at half size, its bar at +(-2, 17) (+(38, 17) for number 4+) */
        float h = op->slot ? 0.5f * s : s;
        float bx = op->slot ? (op->index >= 4 ? 38.0f : -2.0f) : (op->index >= 4 ? 0.0f : 34.0f);
        snprintf(line + n, sizeof(line) - (size_t) n, "; picture %.1f,%.1f..%.1f,%.1f, bar %.1f,%.1f", (double) op->x,
                 (double) op->y, (double) (op->x + 32 * h), (double) (op->y + 32 * h), (double) (op->x + bx * h),
                 (double) (op->y + 17 * h));
    }
    else if (op->op == HG_OP_TRINKET && n < (int) sizeof(line))
        snprintf(line + n, sizeof(line) - (size_t) n, "; picture %.1f,%.1f..%.1f,%.1f", (double) op->x, (double) op->y,
                 (double) (op->x + 32 * s), (double) (op->y + 32 * s));
    else if (op->op == HG_OP_POCKET && n < (int) sizeof(line))
        snprintf(line + n, sizeof(line) - (size_t) n, "; card %.1f,%.1f..%.1f,%.1f, name %s %.1f at y %.1f",
                 (double) (op->x - 8 * s), (double) (op->y - 12 * s), (double) (op->x + 8 * s), (double) (op->y + 12 * s),
                 op->name_right ? "from" : "ending at", (double) (op->x + (op->name_right ? 13.0f : -16.0f) * s),
                 (double) (op->y - 10 * s));
    else if (op->op == HG_OP_HEARTS && n < (int) sizeof(line))
        snprintf(line + n, sizeof(line) - (size_t) n, "; first heart centre %.1f,%.1f, six a row %s",
                 (double) op->x, (double) op->y, op->index >= 4 ? "leftwards" : "rightwards");
    report_line("hud piece", line);
    (void) o;
}

/* the frame, every call and the check of them, when the frame changes */
static void tell(const hg_in *in, const hg_out *o, int twins_button) {
    static char was[400];
    static int said;
    if (said >= 12 || !shim_report_active()) return;
    char line[400];
    int at = snprintf(line, sizeof(line), "entries %d, canvas %.0fx%.0f, HudOffset %.2f%s%s, layouts", o->n,
                      (double) in->W, (double) in->H, (double) (1.0f - in->f), in->greed ? ", Greed" : "",
                      twins_button ? ", twins' trinkets on their button" : "");
    for (int k = 0; k < HG_ENTRIES && at < (int) sizeof(line) - 40; k++)
        if (in->present[k])
            at += snprintf(line + at, sizeof(line) - (size_t) at, " %d:%d type %d%s%s", k, o->layout[k], in->type[k],
                           o->draw[k] ? "" : " (game's)", in->trinket[k][0] || in->trinket[k][1] ? " T" : "");
    if (!strcmp(line, was)) return;
    strcpy(was, line);
    said++;
    report_line("hud blocks", line);
    for (int i = 0; i < o->nops; i++) say_op(&o->op[i], o);
    /* the check: every place a number inside the canvas give or take a picture, and a layout for every entry */
    int bad = 0;
    char chk[300];
    int c = snprintf(chk, sizeof(chk), "SELF-CHECK hud blocks: ");
    for (int i = 0; i < o->nops; i++) {
        const hg_op *op = &o->op[i];
        if (!(op->x > -40.0f && op->x < in->W + 40.0f && op->y > -40.0f && op->y < in->H + 40.0f && op->s > 0.0f)) {
            if (bad++ < 4 && c < (int) sizeof(chk) - 60)
                c += snprintf(chk + c, sizeof(chk) - (size_t) c, "%s entry %d at %.1f,%.1f outside; ", OP_NAME[op->op],
                              op->entry, (double) op->x, (double) op->y);
        }
    }
    for (int k = 0; k < HG_ENTRIES; k++)
        if (o->draw[k] && !in->present[k]) bad++;
    snprintf(chk + c, sizeof(chk) - (size_t) c, "%s (%d calls)", bad ? "FAIL" : "OK", o->nops);
    report_line("hud", chk);
}
#endif

/*
 * The hook at 0x6ee608 (trace.c, mark '5'): every frame the player HUD is drawn,
 * between the corners and the loop.
 */
void shim_hud_layout(unsigned long *regs) {
    unsigned long b = shim_image_base();
    if (!b || !regs) return;
    unsigned long x29 = *(const unsigned long *) ((const char *) regs + REGS_X29);
    unsigned long sp = (unsigned long) regs + REGS_SP;
    unsigned long G = *(const unsigned long *) (b + GAME_PTR);
    if (!G || !x29) return;
    unsigned long hud = G + HUD_ENTRIES;
    /* the frame must be 0x6ed3b0's own and its HUD the game's: else hands off */
    if (x29 - sp != 0x360 || *(const unsigned long *) (sp + SP_THIS) != hud) {
        DIAG(static int said; if (!said) { said = 1;
             LOG("hud: the corners' hook sees a frame it does not know (x29 - sp %lx, this %lx, HUD %lx) - left alone",
                 x29 - sp, *(const unsigned long *) (sp + SP_THIS), hud); });
        return;
    }
    if (!shim_eid_readable((const void *) hud, HG_ENTRIES * HUD_ENTRY)) return;

    hg_in in;
    memset(&in, 0, sizeof(in));
    in.W = *(const float *) (b + CANVAS_W_AT);
    in.H = *(const float *) (b + CANVAS_H_AT);
    in.f = *(const float *) (sp + SP_INSET);
    if (!(in.W > 0.0f) || !(in.H > 0.0f) || !(in.f >= -0.01f) || !(in.f <= 1.01f)) return;
    if (shim_eid_readable((const void *) (G + GAME_DIFFICULTY), 4))
        in.greed = *(const int *) (G + GAME_DIFFICULTY) >= 2;
    int choosing[4] = { 0 };
    if (shim_eid_readable((const void *) (G + JOIN_SLOTS), 4 * JOIN_SLOT_SIZE))
        for (int k = 0; k < 4; k++)
            choosing[k] = *(const int *) (G + JOIN_SLOTS + (unsigned long) k * JOIN_SLOT_SIZE + 8) >= 0;
    for (int k = 0; k < HG_ENTRIES; k++) {
        unsigned long p = player_of(hud, k);
        if (!readable_player(p) || choosing[k & 3]) continue;        /* a slot choosing a character: the game's */
        in.present[k] = 1;
        in.type[k] = *(const int *) (p + P_TYPE);
        unsigned long e = hud + (unsigned long) k * HUD_ENTRY;
        for (int s = 0; s < 2; s++)          /* 0x6e831c / 0x6e8338: an id and a picture, or the renderer draws nothing */
            in.trinket[k][s] = *(const int *) (e + ENTRY_TRINKET + (unsigned long) s * 0x18) != 0
                            && *(const unsigned long *) (e + ENTRY_TRINKET + (unsigned long) s * 0x18 + 8) != 0;
    }
    /*
     * Player 1's twins with their trinkets on the touch overlay's cross button
     * (twintrinkets.c): the pictures are there, and the row under the hearts
     * keeps their places empty - the cards stand where Rep+ puts them.
     */
    int twins_button = shim_twins_button_up();

    /* the layout is worked out when what it depends on changes, not every frame */
    static hg_in was;
    static hg_out o;
    static int have;
    if (!have || memcmp(&was, &in, sizeof(in))) {
        hg_layout(&in, &o);
        was = in;
        have = 1;
    }
    if (!o.mine) return;

    float *corners = (float *) (x29 - X29_CORNERS);
    for (int k = 0; k < HG_ENTRIES; k++)
        if (o.draw[k]) { corners[k * 2] = 0.0f; corners[k * 2 + 1] = 0.0f; }   /* the loop's skip (0x6ee674) */
    for (int i = 0; i < o.nops; i++) {
        const hg_op *op = &o.op[i];
        if (twins_button && op->op == HG_OP_TRINKET && (op->entry == 0 || op->entry == 4)) continue;
        run_op(b, hud, op);
    }
    pa_words(b, PA_GAME);                   /* the game's own words again, for everything after (see run_op) */
#if ISAAC_DIAG
    tell(&in, &o, twins_button);
#endif
}
