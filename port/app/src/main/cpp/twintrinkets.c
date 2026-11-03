/*
 * Jacob and Esau: one drop button, both twins' trinkets on it (04.10.2026).
 *
 * WHAT THE GAME DOES, AND WHY IT FALLS SHORT FOR THE TWINS.
 *
 *   Dropping is action 11 held for 121 updates (0x52ae90), and it is asked of
 *   the DEVICE: Jacob and Esau sit on the same one, so each runs his own count
 *   off the same press and both drop everything - on PC exactly the same. On
 *   the touch screen the press comes from an invisible zone over the HUD's
 *   trinket, overlay+0x437d8, and the zone takes fingers only while PLAYER 0
 *   has a droppable trinket (0x85fee8: +0x1b0 from 0x54a490 / 0x508d24 of
 *   player index 0; the ItemButton's touch entry 0x8640fc hands +0x1b0 to
 *   0x8620f0 as w2, and a nought there lets the finger go). Esau's trinket is
 *   drawn in his own HUD block, bottom right (entry + 4, 0x6ee740), and no zone
 *   lies over it - with a trinket on Esau alone nothing could be dropped.
 *
 * WHAT THE OWNER CHOSE (04.10.2026): option A plus a picture.
 *
 *   The zone takes fingers when EITHER twin has a droppable trinket; a hold
 *   drops both, as on PC. And the button shows the trinkets of both: a fixed
 *   56 x 56 square at the game zone's corner, the trinkets at 100% in a cross -
 *   Jacob's slot 0 left, Esau's slot 0 right, Jacob's slot 1 on top, Esau's
 *   slot 1 below; the group centred by its arms (one in the middle, two a bar,
 *   three a T, four the cross); every icon centred by its ART (trinket_art.h),
 *   on the longest arm that keeps all the art inside the square. Mockups:
 *   other stuff/twin-trinkets-mockup, cross-*.png.
 *
 *   A single character keeps the game's zone and picture untouched. So does a
 *   twin played without the touch overlay: there is no button to dress then.
 *
 * HOW, WITHOUT A NEW HOOK.
 *
 *   The picture. Player 0's trinkets are drawn by one call in a loop over his
 *   two slots - 0x6eeb78, `bl 0x6e82f0` (RenderTrinket), reached only for HUD
 *   entry 0. While the twins' button is up that one word is a nop, and we draw
 *   in its place with the very same RenderTrinket - so the shadow, the golden
 *   shimmer, Monkey Paw's frames and the screen shake are the game's. We draw
 *   from hook X (0x578490, inside the minimap's draw 0x578464), which the
 *   player HUD's own draw 0x6ed3b0 calls at 0x6ed69c - before its trinket loop,
 *   on the same thread, in the same frame and the same HUD transform. So the
 *   word is set right before the only place that runs it. Esau's block,
 *   bottom right, is left to the game.
 *
 *   The zone. Hook W (0x8620f0) is every widget's touch entry, called after
 *   the layout (0x860318 lays the zones out anew every frame, from 0x860e50)
 *   and before the widget's own hit test reads its box. There the drop zone's
 *   box (+0x1b4 centre, +0x1bc size) becomes our square and the enable, still
 *   in x2, becomes ours. The game rebuilds its box the next frame, so leaving
 *   the twins needs no undoing.
 *
 * SCREENS. The square stands on the game zone's own bottom-left corner, read
 * live (it follows the canvas height), moved by the same HUD-offset inset the
 * game gives player 0's trinket - (-22, 16) * (1 - HudOffset), 0x6ed520 - and
 * kept inside the canvas. Picture and finger are in one space: the game turns
 * the finger into the picture's units itself (0x86255c, FULLSCREEN on or off).
 *
 * MOVED AND SEEN THROUGH, AS THE OTHER TOUCH CONTROLS (1.75, the owner,
 * 14.10.2026). The touch editor places the square (sticks.c trinketpos, its
 * middle; pushed clear of the sticks as the pause button is) and shows it with
 * four trinkets, the cross at its fullest.
 *
 * And not for the twins alone: "the whole button, not only our add-on for the
 * twins" (the owner, the same day). A single character's trinket button is the
 * game's zone over his HUD trinket, so his trinkets are drawn here too, by the
 * same nop of 0x6eeb78 and the same RenderTrinket: where nobody has moved the
 * button, at the very places the HUD puts them - slot 0 at (44, H - 44), slot 1
 * at (20, H - 68), plus the bottom-left inset (-22, 16) * (1 - HudOffset), the
 * rule hudgeom.c took from the image and checked by emulation - and the zone is
 * left the game's; once moved, on the square, slot 1 above and left of slot 0
 * as the HUD has it, and the zone is the square, taking fingers as the game
 * says. Only when player 0 is the HUD's one entry: in co-op hudlayout.c draws
 * the entries and the image's loop never gets to 0x6eeb78.
 *
 * Everything here is drawn at the touch controls' opacity, the editor's slider: RenderTrinket has no alpha of its
 * own to be given, so while it draws for us its two constant alphas are ours -
 * the picture's `mov w8, #1.0` (0x6e84d4, stored into the KColor's tint alpha
 * at [sp+0x44]) and the shadow's 0.3, loaded at 0x6e83dc/0x6e83e0 by
 * adrp + ldr, which becomes `mov w8, #a*0.3` + `fmov s3, w8`. A float whose low
 * half is nought is one `movz ..., lsl #16`; the upper half alone is within
 * 0.4% of any opacity, under a step of the 255 the screen has. The words go
 * back the moment our draw is done: the same function draws every other
 * trinket of the HUD, Esau's corner and every single character's.
 */
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <android/log.h>
#include "diag.h"
#include "trinket_art.h"

#if ISAAC_DIAG
#define LOG(...) __android_log_print(ANDROID_LOG_INFO, "isaac", __VA_ARGS__)
#else
#define LOG(...) do { } while (0)
#endif

unsigned long shim_image_base(void);
unsigned long shim_pocket_overlay_at(void);
int shim_eid_readable(const void *p, unsigned long n);
int shim_eid_seed_on(int id);
int shim_patch_word(unsigned long off, uint32_t was_a, uint32_t was_b, uint32_t now);
int shim_report_active(void);
void report_line(const char *kind, const char *text);
int shim_sticks_in_game(void);
int shim_sticks_trinket_anchor(float *x, float *y);       /* sticks.c */
int shim_sticks_clear_of_sticks(float *box);
int shim_sticks_right_of_move(float *box);
float shim_touch_alpha_live(void);                        /* loader.c */

#define GAME_PTR        0xac3b90
#define MANAGER_PTR     0xac2428
#define HUD_OFFSET      0x55f0c     /* float in the manager: 1 flush, 0 hard into the corner */
#define CANVAS_W_AT     0xac24d8    /* floats: the canvas the HUD lays out against */
#define CANVAS_H_AT     0xac24dc
#define SEED_NO_HUD     10

#define HUD_ENTRIES     0x47fe0     /* the HUD's eight player entries, inside the game */
#define HUD_ENTRY       0x918
#define HUD_ENTRY_N     8

#define PLAYER_TYPE     0x1738
#define PLAYER_TWIN     0x2580
#define PLAYER_EXISTS   0x1c2
#define TYPE_JACOB      19
#define TYPE_ESAU       20

#define FN_TRINKET_SLOTS 0x508b9c   /* (player) -> how many trinket slots */
#define FN_GET_TRINKET  0x54a490    /* (player, slot) -> id, bit 15 golden */
#define FN_CAN_DROP     0x508d24    /* (player, id) -> may be dropped (0x85ff8c) */
#define FN_RENDER       0x6e82f0    /* RenderTrinket(entry, slot, Vector2 *top-left, scale) */
#define CALL_P0_TRINKET 0x6eeb78    /* bl FN_RENDER, HUD entry 0 only */
#define NOP             0xd503201fu

#define ZONE_DROP       0x437d8     /* the overlay's drop zone, an ItemButton */
#define ITEM_CENTRE     0x1b4
#define ITEM_EXTENT     0x1bc
#define OVERLAY_LIVE    0x684       /* byte: the touch overlay is up */
#define VT_ITEMBUTTON   0xa90c98
#define UNIT_PIXELS     0xa9c7c8    /* the overlay's f is 0.5 over this (0x860354) */

/*
 * The drop zone as the overlay's layout makes it (0x8605dc..0x86062c), for the
 * editor, where no run lays it out: 72 f by 64 f as a Vector2, then the centre
 * (1.25 x, H - 0.75 y) and the size (1.25 x, y). Measured in a run, f 0.5 on a
 * canvas 270 tall: centre 45,246, size 45 x 32.
 */
#define ZONE_X          1.25f
#define ZONE_UP         0.75f

/* RenderTrinket's two alphas (see MOVED AND SEEN THROUGH above) */
#define AT_SHADOW_PAGE  0x6e83dc    /* adrp x8, 0x946000 */
#define AT_SHADOW_LOAD  0x6e83e0    /* ldr s3, [x8, #0x378] */
#define AT_ICON_ALPHA   0x6e84d4    /* mov w8, #0x3f800000 */
#define SHADOW_ALPHA    0x946378    /* the float the shadow is drawn at, 0.3 */
#define W_SHADOW_PAGE   0xd00012e8u
#define W_SHADOW_LOAD   0xbd437903u
#define W_ICON_ALPHA    0x52a7f008u
#define W_FMOV_S3_W8    0x1e270103u /* as 0x6e8398's fmov s0, w22 is 0x1e2702c0 */

#define BUTTON          56          /* the owner's square, canvas units */
#define ARM_LONGEST     24
#define ARM_SHORTEST    4
#define INSET_X         (-22.0f)    /* player 0's trinket inset, times 1 - HudOffset */
#define INSET_Y         16.0f

typedef struct {
    unsigned long entry;            /* the HUD entry RenderTrinket draws from */
    int slot;
    int id;                         /* bit 15 golden */
    int ax, ay;                     /* the arm: -1, 0, 1 */
    int x, y;                       /* top-left of the 32x32 picture, canvas units */
    int droppable;
} icon_t;

static int g_patched;               /* 0x6eeb78 holds the nop */
static int g_live;                  /* the twins' button is up this frame */
static int g_single;                /* a single character's trinkets are ours this frame */
static int g_moved;                 /* ...and stand on the square the player put somewhere */
static int g_enable;                /* the zone takes fingers */
static float g_box[4];              /* our square: x0, y0, x1, y1 */
static float g_base[4];             /* the game's own zone box, centre and size */
static float g_ours[4];             /* what we wrote last, to tell it from the game's */
static int g_have_base;
static uint32_t g_dimmed[3];        /* the three words while they hold our alpha; 0 when the game's */

static uint32_t bl_word(unsigned long from, unsigned long to) {
    return 0x94000000u | ((uint32_t) (((long) to - (long) from) >> 2) & 0x03ffffffu);
}

static void call_game_draw(int on) {
    uint32_t bl = bl_word(CALL_P0_TRINKET, FN_RENDER);
    int want_nop = !on;
    if (want_nop == g_patched) return;
    if (shim_patch_word(CALL_P0_TRINKET, bl, NOP, want_nop ? NOP : bl)) {
        g_patched = want_nop;
        LOG("twins: player 0's trinket draw is %s", want_nop ? "ours" : "the game's again");
    }
}

static void art_of(int id, int *x0, int *y0, int *x1, int *y1) {
    int t = id & 0x7fff;
    if (t > 0 && t < TRINKET_ART_MAX) {
        *x0 = TRINKET_ART[t][0]; *y0 = TRINKET_ART[t][1];
        *x1 = TRINKET_ART[t][2]; *y1 = TRINKET_ART[t][3];
    } else {                        /* a modded trinket: its box, as the game draws it */
        *x0 = 0; *y0 = 0; *x1 = 32; *y1 = 32;
    }
}

static unsigned long entry_of(unsigned long G, unsigned long player) {
    for (int k = 0; k < HUD_ENTRY_N; k++) {
        unsigned long e = G + HUD_ENTRIES + (unsigned long) k * HUD_ENTRY;
        if (*(const unsigned long *) e == player) return e;
    }
    return 0;
}

/* a twin's trinkets, onto the arms: slot 0 sideways, slot 1 up or down */
static int gather(unsigned long b, unsigned long G, unsigned long p, int side, icon_t *out, int n) {
    unsigned long e = entry_of(G, p);
    int slots = ((int (*)(unsigned long)) (b + FN_TRINKET_SLOTS))(p);
    if (slots > 2) slots = 2;
    for (int s = 0; s < slots; s++) {
        int id = ((int (*)(unsigned long, int)) (b + FN_GET_TRINKET))(p, s);
        if (!(id & 0x7fff)) continue;
        icon_t *ic = &out[n++];
        ic->entry = e;
        ic->slot = s;
        ic->id = id;
        ic->ax = s == 0 ? side : 0;
        ic->ay = s == 0 ? 0 : side;
        ic->droppable = ((int (*)(unsigned long, int)) (b + FN_CAN_DROP))(p, id) != 0;
    }
    return n;
}

/* the longest arm that keeps every icon's art inside the square */
static int place(icon_t *ic, int n, const float *box) {
    int mnx = 1, mxx = -1, mny = 1, mxy = -1;
    for (int i = 0; i < n; i++) {
        if (ic[i].ax < mnx) mnx = ic[i].ax;
        if (ic[i].ax > mxx) mxx = ic[i].ax;
        if (ic[i].ay < mny) mny = ic[i].ay;
        if (ic[i].ay > mxy) mxy = ic[i].ay;
    }
    float mx = (mnx + mxx) * 0.5f, my = (mny + mxy) * 0.5f;
    float cx = (box[0] + box[2]) * 0.5f, cy = (box[1] + box[3]) * 0.5f;
    int arm;
    for (arm = ARM_LONGEST; arm >= ARM_SHORTEST; arm--) {
        int fits = 1;
        for (int i = 0; i < n; i++) {
            int x0, y0, x1, y1;
            art_of(ic[i].id, &x0, &y0, &x1, &y1);
            float px = cx + (ic[i].ax - mx) * (float) arm;
            float py = cy + (ic[i].ay - my) * (float) arm;
            ic[i].x = (int) lroundf(px - (x0 + x1) * 0.5f);
            ic[i].y = (int) lroundf(py - (y0 + y1) * 0.5f);
            if (ic[i].x + x0 < box[0] || ic[i].y + y0 < box[1]
                || ic[i].x + x1 > box[2] || ic[i].y + y1 > box[3]) fits = 0;
        }
        if (fits || arm == ARM_SHORTEST) break;
    }
    return arm;
}

/*
 * The game's zone box as the layout left it this frame - or, if what is there
 * is what we wrote, the base we took from the game before.
 */
static int game_zone(unsigned long overlay, float *base) {
    unsigned long z = overlay + ZONE_DROP;
    if (!shim_eid_readable((const void *) z, ITEM_EXTENT + 8)) return 0;
    unsigned long b = shim_image_base();
    if (*(const unsigned long *) z != b + VT_ITEMBUTTON) return 0;
    const float *c = (const float *) (z + ITEM_CENTRE);
    const float *s = (const float *) (z + ITEM_EXTENT);
    if (!g_have_base || c[0] != g_ours[0] || c[1] != g_ours[1] || s[0] != g_ours[2] || s[1] != g_ours[3]) {
        g_base[0] = c[0]; g_base[1] = c[1]; g_base[2] = s[0]; g_base[3] = s[1];
        g_have_base = 1;
    }
    if (!(g_base[2] > 0.0f) || !(g_base[3] > 0.0f)) return 0;
    memcpy(base, g_base, sizeof(g_base));
    return 1;
}

static void tell(const icon_t *ic, int n, int arm, float hud) {
    static int said;
    static char was[300];
    if (said >= 16 || !shim_report_active()) return;
    char line[300];
    int k = snprintf(line, sizeof(line), "button %.0f..%.0f x %.0f..%.0f (game zone %.1f,%.1f %.0fx%.0f, "
                     "HudOffset %.2f), arm %d, takes fingers %d:",
                     (double) g_box[0], (double) g_box[2], (double) g_box[1], (double) g_box[3],
                     (double) g_base[0], (double) g_base[1], (double) g_base[2], (double) g_base[3],
                     (double) hud, arm, g_enable);
    for (int i = 0; i < n && k < (int) sizeof(line) - 40; i++) {
        int x0, y0, x1, y1;
        art_of(ic[i].id, &x0, &y0, &x1, &y1);
        k += snprintf(line + k, sizeof(line) - (size_t) k, " %s%d #%d art %d..%d x %d..%d%s",
                      ic[i].ax < 0 || ic[i].ay < 0 ? "J" : "E", ic[i].slot, ic[i].id & 0x7fff,
                      ic[i].x + x0, ic[i].x + x1, ic[i].y + y0, ic[i].y + y1,
                      ic[i].droppable ? "" : " (stays)");
    }
    if (!strcmp(line, was)) return;
    strcpy(was, line);
    said++;
    report_line("twins", line);
}

/* `mov w8, #v`, v rounded to its upper half - the encoding 0x6e84d4 itself is in */
static uint32_t movz_w8(float v) {
    uint32_t u;
    memcpy(&u, &v, sizeof(u));
    u = (u + 0x8000u) >> 16;
    return 0x52a00000u | (u << 5) | 8u;
}

static const unsigned long ALPHA_AT[3] = { AT_SHADOW_PAGE, AT_SHADOW_LOAD, AT_ICON_ALPHA };
static const uint32_t ALPHA_WAS[3] = { W_SHADOW_PAGE, W_SHADOW_LOAD, W_ICON_ALPHA };

/* RenderTrinket's alphas back to the game's */
static void alpha_back(void) {
    for (int i = 0; i < 3; i++) {
        if (!g_dimmed[i]) continue;
        if (shim_patch_word(ALPHA_AT[i], ALPHA_WAS[i], g_dimmed[i], ALPHA_WAS[i])) g_dimmed[i] = 0;
    }
}

/* RenderTrinket's alphas made ours; 0, and the game's left in place, when a word is not what it should be */
static int alpha_ours(unsigned long b, float a) {
    if (!(a < 1.0f)) return 1;                                  /* the game's own: nothing to change */
    float shadow = *(const float *) (b + SHADOW_ALPHA);
    uint32_t want[3] = { movz_w8(shadow * a), W_FMOV_S3_W8, movz_w8(a) };
    for (int i = 0; i < 3; i++) {
        if (!shim_patch_word(ALPHA_AT[i], ALPHA_WAS[i], want[i], want[i])) { alpha_back(); return 0; }
        g_dimmed[i] = want[i];
    }
    return 1;
}

/* the zone by the layout's own rule, the menu having none laid out */
static int zone_by_rule(unsigned long b, float *base) {
    float px = *(const float *) (b + UNIT_PIXELS);
    float H = *(const float *) (b + CANVAS_H_AT);
    if (!(px > 0.0f) || !(H > 0.0f)) return 0;
    float f = 0.5f / px;
    base[0] = 72.0f * f * ZONE_X;
    base[1] = H - 64.0f * f * ZONE_UP;
    base[2] = 72.0f * f * ZONE_X;
    base[3] = 64.0f * f;
    return 1;
}

/*
 * The square: where the player put it, pushed clear of the sticks; else on the
 * zone's bottom-left corner, inset with the HUD - and, where that corner lies
 * under the movement stick, just right of its ring (sticks.c
 * shim_sticks_right_of_move, 25.10.2026). Whole units, inside the canvas.
 * *own is 1 when the square is not the HUD's corner: then a single character's
 * trinkets stand on it and the zone is the square, as for a placed button.
 */
static int square_of(unsigned long b, const float *base, float *box, int *own) {
    unsigned long mgr = *(const unsigned long *) (b + MANAGER_PTR);
    float W = *(const float *) (b + CANVAS_W_AT), H = *(const float *) (b + CANVAS_H_AT);
    if (!(W > 0.0f) || !(H > 0.0f)) return 0;
    float left, bottom;
    float x = 0.0f, y = 0.0f;
    if (own) *own = 0;
    if (shim_sticks_trinket_anchor(&x, &y)) {
        if (own) *own = 1;
        float sq[4] = { x - BUTTON * 0.5f, y - BUTTON * 0.5f, x + BUTTON * 0.5f, y + BUTTON * 0.5f };
        shim_sticks_clear_of_sticks(sq);
        left = roundf(sq[0]);
        bottom = roundf(sq[3]);
    } else {
        float hud = mgr ? *(const float *) (mgr + HUD_OFFSET) : 1.0f;
        left = floorf(base[0] - base[2] * 0.5f) + roundf(INSET_X * (1.0f - hud));
        bottom = floorf(base[1] + base[3] * 0.5f) + roundf(INSET_Y * (1.0f - hud));
        float sq[4] = { left, bottom - BUTTON, left + BUTTON, bottom };
        if (shim_sticks_right_of_move(sq)) {
            if (own) *own = 1;
            left = roundf(sq[0]);
            bottom = roundf(sq[3]);
        }
    }
    if (left < 0.0f) left = 0.0f;
    if (left + BUTTON > W) left = W - BUTTON;
    if (bottom > H) bottom = H;
    if (bottom - BUTTON < 0.0f) bottom = BUTTON;
    box[0] = left; box[1] = bottom - BUTTON; box[2] = left + BUTTON; box[3] = bottom;
    return 1;
}

/*
 * touchedit.c: the square as the run will stand it, and four trinkets on it -
 * the cross at its fullest - as their 32 x 32 pictures' top-left corners.
 * The trinkets are the editor's to pick; their order is the run's: Jacob's
 * slot 0, his slot 1, Esau's slot 0, his slot 1.
 */
int shim_twins_button_preview(const int *ids, float *box, int *xy) {
    unsigned long b = shim_image_base();
    float base[4];
    if (!b || !zone_by_rule(b, base) || !square_of(b, base, box, 0)) return 0;
    icon_t ic[4];
    memset(ic, 0, sizeof(ic));
    for (int i = 0; i < 4; i++) {
        int side = i < 2 ? -1 : 1, slot = i & 1;
        ic[i].id = ids[i];
        ic[i].ax = slot == 0 ? side : 0;
        ic[i].ay = slot == 0 ? 0 : side;
    }
    place(ic, 4, box);
    for (int i = 0; i < 4; i++) { xy[i * 2] = ic[i].x; xy[i * 2 + 1] = ic[i].y; }
    return 1;
}

/* player 0 is the HUD's only entry: the image's loop draws him, through 0x6eeb78 */
static int alone(unsigned long G) {
    for (int k = 1; k < HUD_ENTRY_N; k++) {
        unsigned long p = *(const unsigned long *) (G + HUD_ENTRIES + (unsigned long) k * HUD_ENTRY);
        if (p && shim_eid_readable((const void *) p, PLAYER_TWIN + 8)
            && *(const unsigned char *) (p + PLAYER_EXISTS)) return 0;
    }
    return 1;
}

/* a single character's trinkets: where the HUD has them, or on the square once it is moved */
static void single_draw(unsigned long b, unsigned long G, unsigned long mgr, unsigned long p0,
                        const float *base) {
    icon_t ic[2];
    int n = gather(b, G, p0, -1, ic, 0);
    for (int i = 0; i < n; i++) {                               /* slot 1 above and left of slot 0 */
        ic[i].ax = ic[i].slot == 0 ? 1 : 0;
        ic[i].ay = ic[i].slot == 0 ? 0 : -1;
    }
    float at[2][2];
    float hud = *(const float *) (mgr + HUD_OFFSET);
    int own = 0;
    if (square_of(b, base, g_box, &own) && own) {
        g_moved = 1;
        if (n) place(ic, n, g_box);
        for (int i = 0; i < n; i++) { at[i][0] = (float) ic[i].x; at[i][1] = (float) ic[i].y; }
    } else {
        float H = *(const float *) (b + CANVAS_H_AT), f = 1.0f - hud;
        for (int i = 0; i < n; i++) {
            at[i][0] = (ic[i].slot ? 20.0f : 44.0f) + INSET_X * f;
            at[i][1] = H - (ic[i].slot ? 68.0f : 44.0f) + INSET_Y * f;
        }
    }
    if (shim_report_active()) {
        static int said;
        static char was[200];
        char line[200];
        int k = snprintf(line, sizeof(line), "single character: %s, HudOffset %.2f:",
                         g_moved ? "on the square the player put" : "where the HUD has them", (double) hud);
        for (int i = 0; i < n && k < (int) sizeof(line) - 30; i++)
            k += snprintf(line + k, sizeof(line) - (size_t) k, " slot %d #%d at %.1f,%.1f",
                          ic[i].slot, ic[i].id & 0x7fff, (double) at[i][0], (double) at[i][1]);
        if (said < 16 && strcmp(line, was)) { strcpy(was, line); said++; report_line("trinket", line); }
    }
    if (!n || shim_eid_seed_on(SEED_NO_HUD)) return;
    void (*render)(unsigned long, int, const float *, float) =
        (void (*)(unsigned long, int, const float *, float)) (b + FN_RENDER);
    alpha_ours(b, shim_touch_alpha_live());
    for (int i = n - 1; i >= 0; i--)                            /* slot 1 first, as the HUD's loop */
        if (ic[i].entry) render(ic[i].entry, ic[i].slot, at[i], 1.0f);
    alpha_back();
}

/*
 * Hook X: inside the player HUD's draw, before its trinket loop. Decides
 * whether the twins' button is up, sets 0x6eeb78 to match, and draws.
 */
void shim_twins_draw(void) {
    unsigned long b = shim_image_base();
    if (!b) return;
    unsigned long G = *(const unsigned long *) (b + GAME_PTR);
    unsigned long mgr = *(const unsigned long *) (b + MANAGER_PTR);
    unsigned long overlay = shim_pocket_overlay_at();
    int live = 0, single = 0;
    unsigned long p0 = 0, twin = 0;
    float base[4];
    if (G && mgr && overlay && shim_sticks_in_game()
        && shim_eid_readable((const void *) (overlay + OVERLAY_LIVE), 1)
        && *(const unsigned char *) (overlay + OVERLAY_LIVE)) {
        p0 = *(const unsigned long *) (G + HUD_ENTRIES);         /* the player of entry 0 */
        if (p0 && shim_eid_readable((const void *) p0, PLAYER_TWIN + 8) && game_zone(overlay, base)) {
            int type = *(const int *) (p0 + PLAYER_TYPE);
            twin = *(const unsigned long *) (p0 + PLAYER_TWIN);
            live = (type == TYPE_JACOB || type == TYPE_ESAU) && twin && twin != p0
                && shim_eid_readable((const void *) twin, PLAYER_TWIN + 8)
                && *(const unsigned char *) (twin + PLAYER_EXISTS);
            single = !live && alone(G);
        }
    }
    call_game_draw(!(live || single));
    g_live = live;
    g_single = single;
    g_moved = 0;
    g_enable = 0;
    if (single) { single_draw(b, G, mgr, p0, base); return; }
    if (!live) return;

    /* the square: the player's place, or the game zone's bottom-left corner inset with the HUD */
    float hud = *(const float *) (mgr + HUD_OFFSET);
    if (!square_of(b, base, g_box, 0)) { g_live = 0; call_game_draw(1); return; }

    icon_t ic[4];
    int n = gather(b, G, p0, -1, ic, 0);                        /* Jacob: left and top */
    n = gather(b, G, twin, 1, ic, n);                           /* Esau: right and below */
    for (int i = 0; i < n; i++) g_enable |= ic[i].droppable;
    if (!n) { tell(ic, 0, 0, hud); return; }
    int arm = place(ic, n, g_box);
    tell(ic, n, arm, hud);
    if (shim_eid_seed_on(SEED_NO_HUD)) return;                  /* the HUD is skipped after us */

    /* back to front: the first gathered lies on top, as cards lie */
    void (*render)(unsigned long, int, const float *, float) =
        (void (*)(unsigned long, int, const float *, float)) (b + FN_RENDER);
    alpha_ours(b, shim_touch_alpha_live());                     /* at the touch controls' opacity */
    for (int i = n - 1; i >= 0; i--) {
        if (!ic[i].entry) continue;
        float at[2] = { (float) ic[i].x, (float) ic[i].y };
        render(ic[i].entry, ic[i].slot, at, 1.0f);
    }
    alpha_back();
}

/*
 * Hook W: every widget's touch entry. The drop zone becomes the square, and
 * takes fingers when either twin has something to drop. regs[2] is w2, the
 * enable 0x8640fc read from +0x1b0 on its way in.
 */
/* hudlayout.c: the twins' button is up this frame - player 1's trinkets are on it */
int shim_twins_button_up(void) { return g_live; }

void shim_twins_zone(unsigned long widget, unsigned long *regs) {
    if (!(g_live || g_moved) || !regs) return;
    if (!shim_sticks_in_game()) { g_live = 0; g_moved = 0; return; }   /* out of the run: the game's zone again */
    unsigned long overlay = shim_pocket_overlay_at();
    if (!overlay || widget != overlay + ZONE_DROP) return;
    float *c = (float *) (widget + ITEM_CENTRE);
    float *s = (float *) (widget + ITEM_EXTENT);
    c[0] = (g_box[0] + g_box[2]) * 0.5f;
    c[1] = (g_box[1] + g_box[3]) * 0.5f;
    s[0] = g_box[2] - g_box[0];
    s[1] = g_box[3] - g_box[1];
    g_ours[0] = c[0]; g_ours[1] = c[1]; g_ours[2] = s[0]; g_ours[3] = s[1];
    if (g_live) regs[2] = (unsigned long) (g_enable ? 1 : 0);   /* a single character's: as the game says */
    {
        static int said;
        if (said < 2 && shim_report_active()) {
            said++;
            char line[160];
            snprintf(line, sizeof(line), "drop zone answers as %.0f..%.0f x %.0f..%.0f, fingers %s",
                     (double) g_box[0], (double) g_box[2], (double) g_box[1], (double) g_box[3],
                     g_enable ? "taken" : "let go");
            report_line("twins", line);
        }
    }
}
