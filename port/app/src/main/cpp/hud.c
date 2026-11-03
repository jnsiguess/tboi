/*
 * The HUD offset, and the touch zones the game forgets to move with it.
 *
 * "HUD Offset" in the options pushes the corner elements away from the middle
 * of the screen. The game applies it while DRAWING and nowhere else: in the
 * whole image the setting (Manager+0x55f0c) is read five times, three of them
 * in the options menu itself, and the two that remain both build a vector for
 * a draw position - 0x5784e0 and 0x538060. Not one hit test asks about it.
 *
 * So with the offset turned up the minimap is drawn in the corner while the
 * zone that answers a tap stays where the map used to be. Measured on the
 * device: taps at 412..419 across worked, taps at 451..457 - on the map as
 * drawn - did not. The zone itself is a TouchOverlay::ItemButton with no
 * picture of its own, box 403.5..445.5 by 19..63 (centre 424.5, 41, size
 * 42 x 44), and it never moves.
 *
 * This is the game's own shortcoming, not a difference between the platforms:
 * the same code runs on iOS. Moving the zone is therefore OUR addition, made
 * at the owner's word, and it is kept as small as an addition can be - the
 * zone is offset by exactly the vector the game offsets the drawing by,
 *
 *     (24, -14) * (1 - HudOffset)
 *
 * taken from 0x5784e0 instruction for instruction, and written in the one
 * moment it matters: 0x8620f0, the road along which the overlay hands a finger
 * to each of its widgets, just before the widget's own hit test reads the box.
 *
 * Nothing else is touched. If the game ever moves the zone itself, the base is
 * taken afresh from it: we compare what is there against what we wrote last
 * time, and a difference means the value is the game's, not ours.
 */
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <android/log.h>
#include "diag.h"

#if ISAAC_DIAG
#define LOG(...) __android_log_print(ANDROID_LOG_INFO, "isaac", __VA_ARGS__)
#else
#define LOG(...) do { } while (0)
#endif

unsigned long shim_image_base(void);

#define GAME_MANAGER   0xac2428     /* Manager *, a global */
#define HUD_OFFSET     0x55f0c      /* float: 1 is flush, 0 is pushed fully into the corner */
#define VT_ITEMBUTTON  0xa90c98     /* IsaacRepentance::TouchOverlay::ItemButton */

#define ITEM_CENTRE    0x1b4        /* Vector2, as ItemButton::HitTest (0x864034) reads it */
#define ITEM_EXTENT    0x1bc        /* Vector2: the whole width and height */

#define SHIFT_X  24.0f              /* the vector 0x578510 builds */
#define SHIFT_Y (-14.0f)

#define FIELD_H (shim_screen_height())

/*
 * The canvas is no longer a constant: it is as wide as the screen (screen.c),
 * so every place that used to say 480 asks the game what it is laying out
 * against right now.
 */
float shim_screen_width(void);
float shim_screen_height(void);
#define FIELD_W (shim_screen_width())

static unsigned long g_zone;        /* the zone we saw last, to notice a rebuilt overlay */
static float g_base[2];             /* where the game put it */
static float g_ours[2];             /* what we wrote there last */
static int g_have_base;

/* the offset the game would draw with, 0 when there is nothing to ask */
static int hud_shift(float *dx, float *dy) {
    unsigned long b = shim_image_base();
    if (!b) return 0;
    unsigned long M = *(const unsigned long *) (b + GAME_MANAGER);
    if (!M) return 0;
    float offset = *(const float *) (M + HUD_OFFSET);
    if (!isfinite(offset)) return 0;
    float k = 1.0f - offset;
    if (k < 0.0f) k = 0.0f;
    if (k > 1.0f) k = 1.0f;
    *dx = SHIFT_X * k;
    *dy = SHIFT_Y * k;
    return 1;
}

/*
 * Is this the map's zone? The overlay carries one ItemButton whose box sits in
 * the top right of the field with a real size to it - the others are either
 * empty, or in the corners the map is not in. Asking the shape rather than
 * remembering a number keeps this true after we have moved the box: a zone
 * pushed further into its own corner is still in that corner.
 */
/*
 * WHERE THE MAP'S ZONE IS, AND WHY IT IS NOT REMEMBERED.
 *
 * This used to latch the first ItemButton whose box looked right - big enough,
 * small enough, in the top-right quarter - and hold that pointer for the life
 * of the process. Two things were wrong with that, and together they are the
 * fault the owner reported on 16.09.2026: the long press on the map stops
 * opening the item list, and the SAME long press starts working on the active
 * item button instead.
 *
 * The touch overlay is built anew whenever the canvas changes - the FULLSCREEN
 * switch, and entering or leaving a run. reset.c carries the same note about
 * the pause button, and was fixed for it; this was not. After a rebuild the
 * pointer is dead. What the box is then read out of is whatever now lives at
 * that address, and the row's item buttons have the SAME layout at +0x1b4 and
 * +0x1bc and boxes in the same 8..160 range - so they pass every check this
 * made, and the "map box" quietly becomes some other button's box.
 *
 * And the shape test could never tell them apart anyway: it accepted any
 * ItemButton in the top-right quarter, running for every widget as the finger
 * is handed round, with nothing to say the layout had happened yet.
 *
 * So nothing is remembered. The zone is the widget at a KNOWN place inside the
 * overlay - +0x43610, from the roll call below - and the overlay itself is
 * pinned afresh every frame (pocket.c, hook B). Identity by address inside a
 * live object, not by the look of a box.
 */
#define ZONE_MAP 0x43610            /* the map's hit zone inside the overlay */

unsigned long shim_pocket_overlay_at(void);

static unsigned long map_zone(void) {
    unsigned long overlay = shim_pocket_overlay_at();
    return overlay ? overlay + ZONE_MAP : 0;
}

void shim_hud_zone(unsigned long self, int (*readable)(const void *, unsigned long)) {
    if (!self || !readable((const void *) self, ITEM_EXTENT + 8)) return;

    unsigned long b = shim_image_base();
    if (!b || *(const unsigned long *) self != b + VT_ITEMBUTTON) return;

    /*
     * MEASUREMENT (item 25, report build): which of the overlay's other zones
     * a finger lands on, where its box is, and with what in hand - the two
     * "drop" zones that are the trinket's button and the nameless one that may
     * be Bag of Crafting's. They are to be made movable in the editor, and not
     * before their boxes are known.
     */
    {
        int shim_report_active(void);
        void report_line(const char *, const char *);
        unsigned long overlay = shim_pocket_overlay_at();
        static int said;
        if (overlay && said < 16 && shim_report_active()
            && (self == overlay + 0x437d8 || self == overlay + 0x439a0 || self == overlay + 0x43b68)
            && *(const unsigned char *) (self + 0x168)) {
            said++;
            const float *zc = (const float *) (self + ITEM_CENTRE);
            const float *zs = (const float *) (self + ITEM_EXTENT);
            char line[160];
            snprintf(line, sizeof(line), "zone +0x%lx held: centre %.1f,%.1f size %.1f x %.1f (canvas %.0f x %.0f)",
                     self - overlay, (double) zc[0], (double) zc[1], (double) zs[0], (double) zs[1],
                     (double) FIELD_W, (double) FIELD_H);
            report_line("zones", line);
        }
    }

    unsigned long zone = map_zone();
    if (!zone || self != zone) return;

    float *c = (float *) (self + ITEM_CENTRE);
    const float *s = (const float *) (self + ITEM_EXTENT);

    /*
     * A rebuilt overlay brings a fresh widget holding the game's own centre,
     * and our memory of "what we last wrote" belongs to the dead one. Keeping
     * it would make the next base re-take read our stale value as the game's.
     */
    if (zone != g_zone) {
        g_zone = zone;
        g_have_base = 0;
        {
            int shim_report_active(void);
            void report_line(const char *, const char *);
            if (shim_report_active()) {
                char line[120];
                snprintf(line, sizeof(line), "map zone centre %.1f,%.1f size %.1f x %.1f",
                         (double) c[0], (double) c[1], (double) s[0], (double) s[1]);
                report_line("hud", line);
            }
        }
    }

    float dx, dy;
    if (!hud_shift(&dx, &dy)) return;

    /* a value we did not write is the game's: take it as the new base */
    if (!g_have_base || c[0] != g_ours[0] || c[1] != g_ours[1]) {
        g_base[0] = c[0];
        g_base[1] = c[1];
        g_have_base = 1;
    }

    float wx = g_base[0] + dx, wy = g_base[1] + dy;
    if (c[0] == wx && c[1] == wy) return;
    c[0] = wx; c[1] = wy;
    g_ours[0] = wx; g_ours[1] = wy;

    DIAG(static float said_x, said_y;
         if (wx != said_x || wy != said_y) { said_x = wx; said_y = wy;
             LOG("hud: the map's zone follows the offset — base %.1f,%.1f + %.1f,%.1f "
                 "-> box %.1f..%.1f by %.1f..%.1f",
                 (double) g_base[0], (double) g_base[1], (double) dx, (double) dy,
                 (double) (wx - s[0] * 0.5f), (double) (wx + s[0] * 0.5f),
                 (double) (wy - s[1] * 0.5f), (double) (wy + s[1] * 0.5f)); });
}

/*
 * Where the map's zone is right now, for the port's own long press on it
 * (items.c). The box is the widget's own, kept current by the code above, so
 * whatever the HUD offset is, this is where the finger has to land.
 */
int shim_hud_map_box(float *cx, float *cy, float *w, float *h) {
    unsigned long zone = map_zone();
    if (!zone || !cx || !cy || !w || !h) return 0;
    const float *c = (const float *) (zone + ITEM_CENTRE);
    const float *s = (const float *) (zone + ITEM_EXTENT);
    if (!isfinite(c[0]) || !isfinite(c[1]) || !isfinite(s[0]) || !isfinite(s[1])) return 0;
    if (s[0] < 8.0f || s[1] < 8.0f) return 0;
    *cx = c[0]; *cy = c[1]; *w = s[0]; *h = s[1];
    return 1;
}

/*
 * The bomb button and the golden bomb.
 *
 * The overlay hides its bomb button whenever the player has no bombs left:
 * 0x85f8bc reads Entity_Player+0x16dc, and at 0x85f9cc anything below one
 * clears the byte at overlay+0x20880 — the byte the drawing (0x8603e0) and the
 * layout of the button column both read.
 *
 * The golden bomb is not a count but a flag, Entity_Player+0x16d9, and the
 * game honours it everywhere else. Dropping a bomb goes through 0x51d76c:
 * it takes (bombs > 0), ORs in that flag and lets the bomb through — so with
 * a golden bomb and an empty count the game will still drop bombs happily,
 * and the only thing missing is the button to ask with. The HUD knows the
 * flag too and draws the golden icon by it (0x6ed8ac). Only the touch button
 * asks the count alone.
 *
 * That is the original's own oversight, on iOS as much as here, and mending it
 * is OUR addition, made at the owner's word. It is kept as small as one: we
 * run right after the bomb button's update, on the overlay's very next call
 * (0x85fee8, which has that single caller), and put the byte back when the
 * player holds a golden bomb and nothing else would show it. The number stays
 * hidden, which is what an endless supply should look like.
 *
 * Tainted ??? is left alone: he carries poops, counted in +0x269c, and the
 * update already treats him apart (player type 25 at +0x1738).
 *
 * BLOOD BOMBS (collectible 614) is the same case with another key. The same
 * drop road, for a player with no bombs, asks HasCollectible(614) at 0x51d7a4
 * (0x4eeb74, the player, the id, zero) and with it pays for the bomb and lets
 * it through:
 *
 *     0051d79c  mov  w1, #0x266                  614, Blood Bombs
 *     0051d7a4  bl   #0x4eeb74                   HasCollectible
 *     0051d7b0  ldr  w8, [x19, #0x1738]          Tainted Bethany (36) with a
 *     0051d7bc  ldr  w8, [x19, #0x247c]          blood charge spends one
 *     0051d7d0  bl   #0x4f04c4                   (AddBloodCharge -1),
 *     0051d818  blr  x8                          everybody else takes the damage
 *
 * The button asks the count alone for this one too, so the item could not be
 * used as it was meant to on touch. The byte is put back for it the same way,
 * and the question asked is the game's own call.
 *
 * AND THE TWIN CARRIES HIS OWN ITEMS. Jacob and Esau share the bomb count, but
 * not what they picked up: an item goes to whichever of them touched it. The
 * drop road above runs for EACH of them, off the same device and the same
 * press, and asks its questions of the one it is running for - so Esau alone
 * with Blood Bombs still drops one. (Two are not dropped for one press: each
 * looks at his twin's bomb cooldown at +0x19e4, fresh from this very frame,
 * and stands down - 0x51d694..0x51d6fc for the twin at +0x2580, 0x51d704 for
 * the linked one at +0x390 whose +0x38 is 1.) So the question here is asked of
 * both, exactly as the game asks it.
 */
#define OVERLAY_LIVE  0x684         /* the overlay is up: 0x85f8d4 asks this first */
#define BOMB_SHOWN    0x20880       /* byte: the bomb button takes part at all */
unsigned long shim_pocket_overlay_at(void);

#define TOUCH_PLAYER  0x85fa3c      /* the player whose device is the screen, or none */

#define PLAYER_TYPE   0x1738
#define PLAYER_BOMBS  0x16dc
#define PLAYER_GOLDEN 0x16d9
#define PLAYER_POOPS  0x269c
#define TAINTED_BLUE  25

#define FN_HAS_COLLECTIBLE 0x4eeb74 /* (player, id, ignore modifiers) */
#define BLOOD_BOMBS   614

#define PLAYER_TWIN   0x2580        /* Esau beside Jacob (0x51d694) */
#define PLAYER_LINKED 0x390         /* and the linked one, when its +0x38 is 1 (0x51d704) */
#define ENTITY_KIND   0x38
#define ENTITY_PLAYER 1

int g_bomb_ours;

/* the player and whoever drops a bomb off the same press: at most three */
static int endless_bombs(unsigned long b, unsigned long p,
                         int (*readable)(const void *, unsigned long)) {
    if (!p || !readable((const void *) p, PLAYER_POOPS + 4)) return 0;
    if (*(const unsigned char *) (p + PLAYER_GOLDEN)) return 1;
    return ((int (*)(unsigned long, int, int)) (b + FN_HAS_COLLECTIBLE))(p, BLOOD_BOMBS, 0) != 0;
}

void shim_bomb_button(unsigned long self, int (*readable)(const void *, unsigned long)) {
    if (!self || !readable((const void *) self, BOMB_SHOWN + 1)) return;
    unsigned long b = shim_image_base();
    if (!b) return;
    if (!*(const unsigned char *) (self + OVERLAY_LIVE)) return;

    unsigned char *shown = (unsigned char *) (self + BOMB_SHOWN);

    unsigned long p = ((unsigned long (*)(void)) (b + TOUCH_PLAYER))();
    if (!p || !readable((const void *) p, PLAYER_POOPS + 4)) return;
    if (*(const int *) (p + PLAYER_TYPE) == TAINTED_BLUE) return;

    if (*shown) return;                     /* the game shows it by itself */
    if (*(const int *) (p + PLAYER_BOMBS) > 0) return;

    int endless = endless_bombs(b, p, readable);
    if (!endless && readable((const void *) p, PLAYER_TWIN + 8))
        endless = endless_bombs(b, *(const unsigned long *) (p + PLAYER_TWIN), readable);
    if (!endless && readable((const void *) p, PLAYER_LINKED + 8)) {
        unsigned long linked = *(const unsigned long *) (p + PLAYER_LINKED);
        if (linked && readable((const void *) linked, ENTITY_KIND + 4)
            && *(const int *) (linked + ENTITY_KIND) == ENTITY_PLAYER)
            endless = endless_bombs(b, linked, readable);
    }
    if (!endless) return;

    *shown = 1;
    DIAG(static int said;
         if (!said) { said = 1;
             LOG("hud: the bomb button stays — a golden bomb or Blood Bombs with an empty count"); });
    g_bomb_ours = 1;
}

/*
 * AND THE SAME BYTE, PUT BACK BEFORE THE DRAWING READS IT.
 *
 * Measured, after the button answered a finger and stayed invisible: the
 * update itself draws. 0x85f8bc clears overlay+0x20880 for an empty count at
 * 0x85fa04 and then calls the group's draw at 0x85fa18, and the draw reads
 * that very byte (0x85fc3c, as group+0x201f0+0x18) before it renders the bomb
 * sprite at group+0x20228. Our restore sat at 0x85fee8 - the overlay's next
 * call, AFTER the draw - so the layout and the hit test saw the byte and the
 * painting never did. Hence: pressable, and not there.
 *
 * So it is put back here as well, at the door of the draw itself. The draw is
 * one function for four groups (0x860e60/70/80 draw the other players'), so
 * only the local player's is touched: the one at overlay+0x678, the overlay
 * being the one the port already meets every frame.
 */
#define GROUP_IN_DRAW 0x678         /* the local player's group, as 0x85fa18 hands it over */

void shim_bomb_group_draw(unsigned long group, int (*readable)(const void *, unsigned long)) {
    unsigned long overlay = shim_pocket_overlay_at();
    if (!overlay || !group || group != overlay + GROUP_IN_DRAW) return;
    shim_bomb_button(overlay, readable);
}

/*
 * PROBE, diagnostic builds only: what the bomb button looks like to the code
 * that DRAWS it, at the moment it draws.
 *
 * The button answers a finger and is not painted, so the byte we put back is
 * believed by one road and not by the other. These are every byte either road
 * reads - overlay+0x684 (the button takes part at all), overlay+0x20880 (it
 * takes part this frame), the picture it was last told to show, and the
 * player's own count and flag - written whenever any of them changes.
 */
void shim_bomb_tell(unsigned long self, int (*readable)(const void *, unsigned long)) {
    int shim_report_active(void);
    void report_line(const char *, const char *);
    static int said;
    if (said >= 24 || !shim_report_active()) return;
    if (!self || !readable((const void *) self, 0x20FB0)) return;
    unsigned long b = shim_image_base();
    if (!b) return;
    const unsigned char *o = (const unsigned char *) self;
    unsigned long p = ((unsigned long (*)(void)) (b + TOUCH_PLAYER))();
    int bombs = -1, golden = -1, type = -1;
    if (p && readable((const void *) p, PLAYER_POOPS + 4)) {
        bombs = *(const int *) (p + PLAYER_BOMBS);
        golden = *(const unsigned char *) (p + PLAYER_GOLDEN);
        type = *(const int *) (p + PLAYER_TYPE);
    }
    static int was[6] = { -2, -2, -2, -2, -2, -2 };
    int now[6] = { o[OVERLAY_LIVE], o[BOMB_SHOWN], o[0x678 + 0x0c],
                   bombs, golden, g_bomb_ours };
    int same = 1;
    for (int i = 0; i < 6; i++) if (now[i] != was[i]) same = 0;
    if (same) return;
    for (int i = 0; i < 6; i++) was[i] = now[i];
    said++;
    char line[200];
    snprintf(line, sizeof(line),
             "+0x684=%d +0x20880=%d +0x684(again)=%d | bombs %d golden %d type %d | ours %d",
             now[0], now[1], now[2], bombs, golden, type, g_bomb_ours);
    report_line("bomb", line);
}

/*
 * HUNTING THE DROP: the widget behind an action.
 *
 *   0x864434 and 0x86448c are the two handlers a touch widget's action goes
 *   through. They are given a record of 0x18 bytes: the widget, the slot
 *   number, the item, and the two actions the group carries. The widget knows
 *   its own name - the sprite sits at +0x10 and its animation is a std::string
 *   at +0x38 - so the log says "PillCard Button" rather than a pointer.
 *
 */
#define SPRITE_IN_WIDGET 0x10
#define ANIM_IN_SPRITE   0x38

const char *shim_str_bytes(const void *);



void shim_widget_action(const char *where, unsigned long entry,
                        int (*readable)(const void *, unsigned long)) {
    if (!entry || !readable((const void *) entry, 0x18)) return;
    unsigned long w = *(const unsigned long *) entry;
    const char *name = "?";
    if (w && readable((const void *) w, SPRITE_IN_WIDGET + ANIM_IN_SPRITE + 8)) {
        const void *const *anim = (const void *const *) (w + SPRITE_IN_WIDGET + ANIM_IN_SPRITE);
        if (readable(anim, 8) && readable(*anim, 24)) name = shim_str_bytes(*anim);
    }
    static int said;
    if (said >= 40) return;
    said++;
    unsigned long b = shim_image_base();
    unsigned long vt = (w && readable((const void *) w, 8)) ? *(const unsigned long *) w : 0;
    LOG("widget %s at %s: slot %d, item %d, actions %d and %d",
        name, where, *(const int *) (entry + 8), *(const int *) (entry + 0xc),
        *(const int *) (entry + 0x10), *(const int *) (entry + 0x14));
    if (!w || !readable((const void *) w, 0x1c8)) return;
    const float *c = (const float *) (w + 0x1b4);
    const float *s = (const float *) (w + 0x1bc);
    LOG("   its widget %lx, class %lx, kind %d, held %d; +0x1b4 says %.1f,%.1f size %.1f x %.1f",
        w, (b && vt > b) ? vt - b : vt, *(const int *) (w + 8),
        *(const unsigned char *) (w + 0x168),
        (double) c[0], (double) c[1], (double) s[0], (double) s[1]);
}

/*
 * The touch overlay's row of buttons, named once each.
 *
 * The drop does not go through the action handlers at all - it is published
 * straight from two bytes of two widgets (0x86122c..0x86125c), so the only way
 * to say WHICH buttons those are is to catch every widget as a finger reaches
 * it and ask it its own name. The sprite sits at widget+0x10 and its animation
 * is a std::string at sprite+0x38 - the same road sticks.c walks to tell the
 * two sticks apart.
 *
 * The offset inside the overlay is printed with it: that is what ties a name
 * to the addresses the publishing code uses. The overlay is the object the
 * sticks live at the start of, and its first widget is at +0x10, so the offset
 * is taken from the stick we have already met.
 */
void shim_widget_name(unsigned long self, int (*readable)(const void *, unsigned long)) {
    if (!self || !readable((const void *) self, SPRITE_IN_WIDGET + ANIM_IN_SPRITE + 8)) return;
    const void *const *anim = (const void *const *) (self + SPRITE_IN_WIDGET + ANIM_IN_SPRITE);
    if (!readable(anim, 8) || !readable(*anim, 24)) return;
    const char *name = shim_str_bytes(*anim);
    if (!name || !*name) return;

    static unsigned long said[24];
    static int n;
    for (int i = 0; i < n; i++) if (said[i] == self) return;
    if (n >= (int) (sizeof(said) / sizeof(said[0]))) return;
    said[n++] = self;
    const float *c = (const float *) (self + ITEM_CENTRE);
    const float *s = (const float *) (self + ITEM_EXTENT);
    LOG("touch button \"%s\" at %lx, kind %d, centre %.1f,%.1f size %.1f x %.1f",
        name, self, *(const int *) (self + 8),
        (double) c[0], (double) c[1], (double) s[0], (double) s[1]);
}

/*
 * The row of touch buttons, moved over to the shooting stick.
 *
 * 0x860b3c(x0 = the widget group, x1 = the start place, x2 = the size every
 * button in the row has, s1 = the gap between them) walks three lists of
 * widgets - [x0+0xe50..0xe58], [+0xe68..0xe70], [+0xe80..0xe88], entries of
 * 0x18 - and writes each widget's box centre (+0x1b4) and size (+0x1bc),
 * advancing the place by size.x + gap each time. The whole block is therefore
 * placed from ONE number, and the caller works that number out from the canvas
 * width at 0x860440: the row is centred along the bottom.
 *
 * Centred is where it was on a 16:9 phone with the sticks at the corners, and
 * it is a long reach from the right thumb on a 21:9 one. The owner asked for
 * the block to sit beside the shooting stick with an even gap. So the row is
 * given a RIGHT edge instead of a middle: its last button ends GAP units before
 * the stick's ring begins.
 *
 * Two things this deliberately does not do.
 *
 *   It does not touch a single button. The buttons come and go with what the
 *   player carries - a bomb button that appears, a second active from the bag,
 *   four pocket slots that fill and empty - and the game re-lays the row every
 *   frame from that count. We change the number it starts from, before it lays
 *   anything out, so the row keeps its own order, its own spacing and its own
 *   membership, whatever they are this frame.
 *
 *   It does not measure from the stick's centre. The ring is 32 units across at
 *   the small size and 48 at the big one, so a gap taken from the centre would
 *   be a different gap at each of the three sizes. Taken from the ring's edge -
 *   which is what sticks.c answers - it is the same gap at all three.
 */
/*
 * Between the last button and the ring, in canvas units - a 270th of the
 * screen's height, so the same share of every screen. The owner asked for
 * 24 once the ring was measured where it is really drawn.
 */
#define ROW_GAP 24.0f

#define ROW_COUNT      0x860ab8     /* the game's own count of the row */
#define GROUP_IN_OVERLAY 0x20fb0    /* the local player's group inside the overlay */
#define GROUP_STRIDE   0xe98        /* the next player's group: the second row (0x8604c0) */
#define BOMB_ALLOWED   0x684        /* byte: the bomb button may take part at all */
#define BOMB_PRESENT   0x20880      /* byte: and it does, this frame */

int shim_sticks_shoot_edge(float *left);
int shim_sticks_row_anchor(float *right, float *middle);        /* sticks.c */
int shim_marked_wanted(void);                                   /* marked.c */
void shim_marked_place(int wanted, float cx, float cy, float w, float h);

/*
 * How many buttons the row will hold - asked of the game, not counted here.
 *
 * Counting them by hand was wrong, and wrong in a way that hid itself. The
 * group keeps three lists of widgets at +0xe50, +0xe68 and +0x0e80, and walking
 * those is what the layout's own loop does - but it is NOT the whole row. The
 * caller adds one more at 0x86040c: `add w8, w0, w26`, where w26 is the bomb
 * button, which lives outside the lists entirely (overlay+0x20880, allowed by
 * overlay+0x684). A run carrying an active item has something in the lists and
 * the hand count agreed; a run carrying only bombs has all three lists empty,
 * the hand count came out zero, and the row was left where it was - which is
 * exactly what the owner saw, a day after it appeared to work.
 *
 * So the count comes from 0x860ab8, the same function the caller asks, plus the
 * same bomb term the caller adds. It reads and computes and touches nothing.
 */
static int row_count(unsigned long group) {
    unsigned long b = shim_image_base();
    if (!b || !group) return 0;
    int (*count)(unsigned long) = (int (*)(unsigned long)) (b + ROW_COUNT);
    int n = count(group);
    if (n < 0) return 0;
    const unsigned char *overlay = (const unsigned char *) (group - GROUP_IN_OVERLAY);
    if (overlay[BOMB_ALLOWED] && overlay[BOMB_PRESENT]) n++;
    return n;
}

/*
 * THE OVERLAY'S WIDGETS, ONCE, INTO THE REPORT.
 *
 * The overlay builds them all in its own constructor (0x85ea74..0x85eb24), each
 * at a fixed place inside it, and every Button and ItemButton keeps its middle
 * at +0x1b4 and its whole size at +0x1bc - the fields the drawing (0x8642c0)
 * and the hit test (0x864034) both read. The twins' round button is one of
 * these: its picture is set to "Analog Stick Twin Freeze" at 0x85f584, on the
 * sprite of the widget at +0x43d30, and a second one at +0x64668.
 *
 * Which of the two is the one on the screen, and where the game puts it, is
 * not worth guessing - so this says so out loud, once, in a report build.
 */
int shim_report_active(void);
void report_line(const char *kind, const char *text);

#define WIDGET_CENTRE  0x1b4
#define WIDGET_EXTENT  0x1bc

void shim_hud_widgets_tell(unsigned long overlay, int (*readable)(const void *, unsigned long)) {
    /*
     * Said again whenever the row of buttons has moved - the editor moves it -
     * up to four times: the active item's touch zone after the panel was moved
     * (the owner's item 7, 23.09.2026) is read against where the row now is.
     */
    static int said;
    static float row_was[2] = {-1.0f, -1.0f};
    if (said >= 4 || !overlay || !shim_report_active()) return;
    unsigned long b = shim_image_base();
    if (!b) return;
    if (!readable((const void *) overlay, OVERLAY_LIVE + 1)) return;
    if (!*(const unsigned char *) (overlay + OVERLAY_LIVE)) return;   /* not up yet */
    /*
     * And not until the widgets have been laid out: in the menu the overlay is
     * already up and every box is still zero, which is what the first roll call
     * printed (12.09.2026).
     */
    unsigned long map = overlay + 0x43610;
    if (!readable((const void *) map, WIDGET_EXTENT + 8)) return;
    if (!(*(const float *) (map + WIDGET_EXTENT) > 0.0f)) return;
    {
        unsigned long row = overlay + 0x20fb0;
        if (!readable((const void *) row, WIDGET_EXTENT + 8)) return;
        const float *rc = (const float *) (row + WIDGET_CENTRE);
        if (rc[0] == row_was[0] && rc[1] == row_was[1]) return;
        row_was[0] = rc[0];
        row_was[1] = rc[1];
    }
    said++;

    static const struct { unsigned long at; const char *what; } WIDGETS[] = {
        { 0x10,     "stick, the first" },   { 0x340,    "stick, the second" },
        { 0x678,    "button" },             { 0x20fb0,  "the row, player one" },
        { 0x21e48,  "the row, player two" },{ 0x22ce0,  "pause" },
        { 0x43610,  "zone: the map" },      { 0x437d8,  "zone: drop" },
        { 0x439a0,  "zone: drop" },         { 0x43b68,  "zone" },
        /*
         * The two round buttons, told apart by the masks 0x860318 hands their
         * live bytes (+0xc) at 0x860a5c and 0x860a70, a bit to a player type:
         *   +0x43d30  0x000000A000180000 - 19, 20, 37, 39: Jacob and Esau,
         *             tainted and not, and only below type 40
         *   +0x64668  0x0000010800030000 - 16, 17, 35, 40: the Forgotten, the
         *             Soul, and BOTH of their tainted halves
         * Both were called "twin freeze" here, which sent the search the wrong
         * way once (14.09.2026).
         */
        { 0x43d30,  "the twins' button" }, { 0x64668,  "the Forgotten's swap" },
    };
    for (unsigned i = 0; i < sizeof(WIDGETS) / sizeof(WIDGETS[0]); i++) {
        unsigned long w = overlay + WIDGETS[i].at;
        if (!readable((const void *) w, WIDGET_EXTENT + 8)) continue;
        const float *c = (const float *) (w + WIDGET_CENTRE);
        const float *s = (const float *) (w + WIDGET_EXTENT);
        unsigned long vt = *(const unsigned long *) w;
        /* a Button keeps its box far further in than an ItemButton: +0x2020c */
        double bx = 0, by = 0, bw = 0, bh = 0;
        if (readable((const void *) w, 0x20214 + 8)) {
            const float *bc = (const float *) (w + 0x2020c);
            const float *bs = (const float *) (w + 0x20214);
            bx = bc[0]; by = bc[1]; bw = bs[0]; bh = bs[1];
        }
        char line[220];
        snprintf(line, sizeof(line),
                 "+0x%05lx %-24s vtable +0x%lx  item %.1f,%.1f %.1f x %.1f"
                 "  button %.1f,%.1f %.1f x %.1f",
                 WIDGETS[i].at, WIDGETS[i].what, vt > b ? vt - b : 0,
                 (double) c[0], (double) c[1], (double) s[0], (double) s[1],
                 bx, by, bw, bh);
        report_line("widget", line);
    }
}

/*
 * THE TWINS' ROUND BUTTON, WHERE THE PLAYER PUT IT.
 *
 * Jacob and Esau get a button beside the pause one that freezes the twin. It is
 * a TouchOverlay::Button, and a Button is not laid out like the ItemButtons
 * above: its middle is at +0x2020c and its whole size at +0x20214 (the hit test
 * 0x863b1c builds its box from exactly those two, and the draw 0x863ccc takes
 * the same +0x2020c for the position). One field, so moving it moves the
 * picture and the finger together.
 *
 * There are two of them, at overlay+0x43d30 and +0x64668, and the game puts
 * BOTH in the same place (0x8606ac and 0x8606f0), so both are moved.
 *
 * Where the game itself puts it, read out of 0x8604ac..0x8606b8: the slot is
 * (72 f, 64 f) and the gap 3 f, as for the row; the pause button's middle is
 *
 *      ( width - 60 - 72 f ,  64 f )
 *
 * and the twins' button stands one slot and one gap to its left,
 *
 *      ( width - 60 - 147 f ,  64 f ).
 *
 * That is the editor's starting point as well (touchedit.c), so it can show the
 * button in the menu, where the overlay has not laid anything out.
 *
 * WHEN it is written matters. The overlay lays its widgets out INSIDE its own
 * draw (0x860318, called at 0x860e50), so a value written at the start of that
 * draw - where the port's other overlay work sits - is computed over a moment
 * later. Measured: our place did reach the field, and the button was still
 * drawn where the game had put it (12.09.2026).
 *
 * So the place is written at the moment it is USED, in the Button's own two
 * readers: its draw (0x863ccc) and its hit test (0x863b1c), both of which take
 * the middle from +0x2020c. Both belong to every Button, the pause one
 * included, so the object is checked against the two the twins own.
 */
#define TWIN_BUTTON_A  0x43d30
#define TWIN_BUTTON_B  0x64668
#define BUTTON_CENTRE  0x2020c
#define BUTTON_EXTENT  0x20214

int shim_sticks_twin_anchor(float *x, float *y);                /* sticks.c */
int shim_hud_twin_home(float *x, float *y, float *w, float *h);  /* below */

/*
 * THE PICTURE ON THE BUTTON, BY WHO IS PLAYING (the owner's item 11).
 *
 * The game shows Jacob holding Esau on both round buttons - the Forgotten's
 * swap too. tools/twin_buttons.py made one picture per pair out of the drawing
 * on their starting room's floor, as the game's own was made; here the button's
 * sprite is given the one for the player standing there, at the moment it is
 * drawn or touched. A type this does not know keeps the game's picture.
 */
#define BUTTON_SPRITE  0x10          /* a widget's sprite; its animation name at +0x38 */
#define SPRITE_ANIM    0x38
#define FN_SPRITE_ANIM 0x89eab8      /* (sprite, name, frame): name the animation */
#define GAME_GAME      0xac3b90
#define G_PLAYERS      0x25c40
#define FN_PLAYER_AT   0x4da574
#define PLAYER_TYPE    0x1738

static const char *twin_picture(int type) {
    switch (type) {
    case 16: case 17: return "Twin Swap";           /* the Forgotten, the Soul */
    case 35: case 40: return "Twin Soul";           /* Tainted Forgotten and his soul */
    /*
     * Tainted Jacob (37, 39) had "Twin Dark Esau" here; his button is now only
     * the follower rule's, with the freeze picture (twin_live, 13.10.2026). The
     * picture stays in the pack, unused.
     */
    }
    return "Analog Stick Twin Freeze";              /* Jacob and Esau, and anyone else */
}

/*
 * A SECOND BODY THAT CAN BE LEFT BEHIND (the owner's item 17, 23.09.2026).
 *
 * Holding drop leaves a follower where it stands, and the game does it for
 * more than the twins: the movement road 0x515294 zeroes the walk of any
 * player whose Parent (+0x390) is a player - Strawman's Keeper, the Soul of
 * Jacob and Esau's and of the Forgotten's temporary body, the Found Soul - and
 * of Tainted Lazarus' second form (a twin at +0x2580); the clots of Sumptorium
 * and Lil Clot stop with it (0x81b8bc), and so do the familiars under RC
 * Remote (0x7ff7e4). On the touch screen all of it reads action 16, the one
 * the twins' round button publishes (a toggle: overlay+0x64660, published every
 * frame at 0x8612xx). Only the button is missing: the layout shows it for
 * Jacob and Esau and Tainted Jacob alone (0x860a40, a mask of player types) -
 * and for Tainted Jacob it holds nothing, so he goes by this rule too
 * (twin_live, 13.10.2026).
 *
 * So the button is shown whenever such a body is there, with the game's own
 * picture, and taken away - its latch cleared, or the next one would start
 * out frozen - when there is none. The rest is the game's.
 */
#define PLAYER_PARENT   0x390
#define PLAYER_TWIN     0x2580
#define PLAYER_CONTROL  0x19ec
#define ENT_TYPE        0x38
#define ENT_VARIANT     0x3c
#define ENT_EXISTS      0x1c2
#define FAMILIAR_OWNER  0x400
#define TOUCH_DEVICE    0x10DEAD
#define FN_HAS_TRINKET  0x4fc598     /* (player, trinket, 0) */
#define TRINKET_RC      179
#define TYPE_ENTITY_PLAYER 1
#define FAMILIAR_CLOT   238
#define TWIN_LATCH      0x64660      /* overlay: the freeze toggle the button flips */
#define BUTTON_LIVE     0x0c

unsigned long shim_eid_list_now(void);

static int g_twin_forced;           /* the freeze button is up by our word, not the layout's */

static int detachable_now(int (*readable)(const void *, unsigned long)) {
    unsigned long b = shim_image_base();
    unsigned long G = b ? *(const unsigned long *) (b + GAME_GAME) : 0;
    if (!G) return 0;
    unsigned long list = G + G_PLAYERS;
    unsigned long first = *(const unsigned long *) (list + 0x10), past = *(const unsigned long *) (list + 0x18);
    int count = (first && past > first) ? (int) ((past - first) / 8) : 0;
    if (count > 8) count = 8;
    for (int i = 0; i < count; i++) {
        unsigned long p = ((unsigned long (*)(unsigned long, int)) (b + FN_PLAYER_AT))(list, i);
        if (!p || !readable((const void *) p, PLAYER_TWIN + 8)) continue;
        unsigned long parent = *(const unsigned long *) (p + PLAYER_PARENT);
        if (parent && readable((const void *) (parent + ENT_TYPE), 4)
            && *(const int *) (parent + ENT_TYPE) == TYPE_ENTITY_PLAYER) return 1;
        int type = *(const int *) (p + PLAYER_TYPE);
        unsigned long twin = *(const unsigned long *) (p + PLAYER_TWIN);
        if ((type == 29 || type == 38) && twin && twin != p) return 1;
        if (*(const int *) (p + PLAYER_CONTROL) == TOUCH_DEVICE
            && ((int (*)(unsigned long, int, int)) (b + FN_HAS_TRINKET))(p, TRINKET_RC, 0)) return 1;
    }
    unsigned long ents = shim_eid_list_now();
    if (ents && readable((const void *) (ents + 0x78), 16)) {
        const unsigned long *e = *(const unsigned long *const *) (ents + 0x78);
        unsigned n = *(const unsigned *) (ents + 0x84);
        if (e && n <= 4096 && readable(e, (unsigned long) n * 8)) {
            for (unsigned i = 0; i < n; i++) {
                unsigned long x = e[i];
                if (!x || !readable((const void *) x, FAMILIAR_OWNER + 8)) continue;
                if (*(const int *) (x + ENT_TYPE) != 3 || *(const int *) (x + ENT_VARIANT) != FAMILIAR_CLOT) continue;
                if (!*(const unsigned char *) (x + ENT_EXISTS)) continue;
                unsigned long owner = *(const unsigned long *) (x + FAMILIAR_OWNER);
                if (owner && readable((const void *) (owner + PLAYER_CONTROL), 4)
                    && *(const int *) (owner + PLAYER_CONTROL) == TOUCH_DEVICE) return 1;
            }
        }
    }
    return 0;
}

/* at the freeze button's draw, right after the layout wrote its live byte */
/*
 * THE FREEZE BUTTON FOR A FOLLOWER ANSWERS A FINGER (the owner's eighth round,
 * 24.09.2026: the button was there and did nothing).
 *
 * The overlay's update (0x860d68) lays its widgets out first - 0x860318 writes
 * this button's byte +0xc back to 0 for anyone but the twins - and hands the
 * fingers out after (0x860f34 -> 0x863bf0 -> 0x8620f0, which drops a finger on
 * a widget whose +0xc is 0 and clears its pressed byte +0x18d). The switch at
 * 0x86127c reads +0x18d. Our byte was written only in the button's draw, in the
 * render pass, after the fingers: drawn, and never pressed. So it is written as
 * well at 0x85fb98, which the update calls for this button at 0x860e70 - after
 * the layout, before the fingers (trace.c 'Y', as the bomb button is).
 *
 * With the byte written twice a frame it no longer tells whose it is, so the
 * game's own case is decided by the layout's own rule: 0x860a48..0x860a6c, the
 * player types 19, 20, 37 and 39 (0xA000180000) - here only 19 and 20, see
 * below.
 *
 * TAINTED JACOB IS LEFT TO THE FOLLOWER RULE (the owner's decision, 13.10.2026,
 * the research notes of that day, Tainted Jacob's button). The layout shows
 * the button to types 37
 * and 39 for the whole run, yet nothing of theirs reads it: action 16 is read at
 * 0x5154e0 (the walk road, which sends 37/39 to the plain walk at 0x5152d8
 * before asking), 0x800244 (RC Remote) and 0x81bd50 (clots), and Dark Esau
 * (866) reads no input at all. So for 37/39 the button is the follower rule's:
 * up, with the freeze picture, while something can be held, and otherwise its
 * byte +0xc is put back to 0 - in the draw and before the fingers, where
 * 0x8620f0 then drops a finger on it and clears its pressed byte - and the latch
 * is cleared, so nothing pressed in vain freezes the next follower.
 */
#define FREEZE_TYPES_GAME   0x180000ULL      /* 19, 20: Jacob and Esau */
#define FREEZE_TYPES_LAYOUT 0xA000180000ULL  /* and 37, 39: Tainted Jacob */

/* Jacob and Esau as player 0: the game's own case, as it always was here */
static int game_shows_freeze(void) {
    unsigned long b = shim_image_base();
    unsigned long G = b ? *(const unsigned long *) (b + GAME_GAME) : 0;
    if (!G) return 0;
    const unsigned char *p = ((const unsigned char *(*)(unsigned long, int)) (b + FN_PLAYER_AT))(G + G_PLAYERS, 0);
    if (!p) return 0;
    unsigned t = *(const unsigned *) (p + PLAYER_TYPE);
    return t < 40 && ((FREEZE_TYPES_GAME >> t) & 1);
}

/*
 * The layout put the button up for Tainted Jacob, and it is ours to take down.
 * Asked of the player on the screen, the one the layout asks (0x860a40).
 */
static int layout_only_freeze(void) {
    unsigned long b = shim_image_base();
    if (!b) return 0;
    const unsigned char *p = ((const unsigned char *(*)(void)) (b + TOUCH_PLAYER))();
    if (!p) return 0;
    unsigned t = *(const unsigned *) (p + PLAYER_TYPE);
    return t < 40 && (((FREEZE_TYPES_LAYOUT & ~FREEZE_TYPES_GAME) >> t) & 1);
}

static void twin_live(unsigned long button, unsigned long overlay,
                      int (*readable)(const void *, unsigned long)) {
    unsigned char *live = (unsigned char *) (button + BUTTON_LIVE);
    int ours_to_hide = layout_only_freeze();
    if (!ours_to_hide && game_shows_freeze()) { g_twin_forced = 0; return; }  /* the game's own case */
    if (!*(const unsigned char *) (overlay + OVERLAY_LIVE)) {       /* paused: all stays */
        if (ours_to_hide && !g_twin_forced) *live = 0;
        return;
    }
    if (detachable_now(readable)) {
        *live = 1;
        if (!g_twin_forced && shim_report_active()) report_line("hud", "freeze button shown for a follower");
        g_twin_forced = 1;
    } else if (ours_to_hide) {
        static int said;
        if (!said && *live && shim_report_active()) {
            said = 1;
            report_line("hud", "freeze button hidden for Tainted Jacob: nothing to hold");
        }
        *live = 0;
        *(unsigned char *) (overlay + TWIN_LATCH) = 0;     /* nothing to hold */
        g_twin_forced = 0;
    } else if (g_twin_forced) {
        *(unsigned char *) (overlay + TWIN_LATCH) = 0;     /* nothing left to hold */
        g_twin_forced = 0;
    }
}

static void twin_picture_set(unsigned long button, int (*readable)(const void *, unsigned long)) {
    unsigned long b = shim_image_base();
    unsigned long G = b ? *(const unsigned long *) (b + GAME_GAME) : 0;
    if (!G) return;
    const unsigned char *player = ((const unsigned char *(*)(unsigned long, int)) (b + FN_PLAYER_AT))(
        G + G_PLAYERS, 0);
    if (!player || !readable(player + PLAYER_TYPE, 4)) return;
    const char *want = twin_picture(*(const int *) (player + PLAYER_TYPE));
    unsigned long overlay = shim_pocket_overlay_at();
    if (g_twin_forced && overlay && button == overlay + TWIN_BUTTON_A) want = "Analog Stick Twin Freeze";
    unsigned long sprite = button + BUTTON_SPRITE;
    const void *const *anim = (const void *const *) (sprite + SPRITE_ANIM);
    if (readable(anim, 8) && *anim && readable(*anim, 24)) {
        const char *now = shim_str_bytes(*anim);
        if (now && !strcmp(now, want)) return;
    }
    ((void (*)(unsigned long, const char *, int)) (b + FN_SPRITE_ANIM))(sprite, want, 0);
}

/* the Button's DRAW (trace mark P) only: its live byte is the layout's there */
void shim_hud_twin_draw(unsigned long button, int (*readable)(const void *, unsigned long)) {
    unsigned long overlay = shim_pocket_overlay_at();
    if (!overlay || button != overlay + TWIN_BUTTON_A) return;
    twin_live(button, overlay, readable);
}

/*
 * THE PAUSE BUTTON, WHERE THE PLAYER PUT IT (hotfix 1.71, the touch editor).
 *
 * The same kind of Button as the twins' (overlay+0x22ce0), laid out by the game
 * every update at (width - 60 - 72 f, 64 f) - 0x860538..0x86056c, the HUD
 * offset is not read there - with a box of 72 f x 64 f, so it is moved the
 * same way: its middle +0x2020c is written in both of its readers, the draw
 * (trace mark P) and the hit test (mark *). The tap stays the game's own: the
 * hit test reads the box we wrote. The long press (reset.c) copies the box at
 * the draw, after this has written it (trace.c puts the place first), so the
 * restart goes with the button too.
 */
#define PAUSE_BUTTON   0x22ce0

int shim_sticks_pause_anchor(float *x, float *y);               /* sticks.c */

/* the game's own place for it, and its box: what the editor starts from */
int shim_hud_pause_home(float *x, float *y, float *w, float *h) {
    unsigned long b = shim_image_base();
    if (!b) return 0;
    float px = *(const float *) (b + 0xa9c7c8);
    if (!(px > 0.0f)) return 0;
    float f = 0.5f / px, width = FIELD_W;
    if (!(width > 0.0f)) return 0;
    if (x) *x = width - 60.0f - 72.0f * f;
    if (y) *y = 64.0f * f;
    if (w) *w = 72.0f * f;
    if (h) *h = 64.0f * f;
    return 1;
}

/*
 * Where the pause button stands when the player has placed it: the place, pushed
 * clear of a stick that has grown since, as the twins' button is. 0 when it is
 * the game's own - then the game's numbers in the Button are the truth.
 */
int shim_hud_pause_placed(float *x, float *y) {
    float wx = 0.0f, wy = 0.0f, hw = 0.0f, hh = 0.0f;
    if (!shim_sticks_pause_anchor(&wx, &wy)) return 0;
    if (shim_hud_pause_home(0, 0, &hw, &hh) && hw > 0.0f && hh > 0.0f) {
        float box[4] = { wx - hw * 0.5f, wy - hh * 0.5f, wx + hw * 0.5f, wy + hh * 0.5f };
        int shim_sticks_clear_of_sticks(float *box);
        if (shim_sticks_clear_of_sticks(box)) {
            wx = (box[0] + box[2]) * 0.5f;
            wy = (box[1] + box[3]) * 0.5f;
        }
    }
    if (x) *x = wx;
    if (y) *y = wy;
    return 1;
}

static void pause_place(unsigned long button, unsigned long overlay,
                        int (*readable)(const void *, unsigned long)) {
    float wx = 0.0f, wy = 0.0f;
    if (!shim_hud_pause_placed(&wx, &wy)) return;
    unsigned long c = button + BUTTON_CENTRE;
    if (!readable((const void *) c, 8)) return;
    float *p = (float *) c;
    if (shim_report_active()) {
        static int said;
        if (said < 3) {
            said++;
            char line[170];
            snprintf(line, sizeof(line),
                     "pause button +0x%lx: the game says %.1f,%.1f, we say %.1f,%.1f (canvas %.0f x %.0f)",
                     button - overlay, (double) p[0], (double) p[1], (double) wx, (double) wy,
                     (double) FIELD_W, (double) FIELD_H);
            report_line("hud", line);
        }
    }
    p[0] = wx;
    p[1] = wy;
}

void shim_hud_button_place(unsigned long button, int (*readable)(const void *, unsigned long)) {
    if (!button) return;
    unsigned long overlay = shim_pocket_overlay_at();
    if (!overlay) return;
    if (button == overlay + PAUSE_BUTTON) { pause_place(button, overlay, readable); return; }
    if (button != overlay + TWIN_BUTTON_A && button != overlay + TWIN_BUTTON_B) return;
    twin_picture_set(button, readable);
    float wx = 0.0f, wy = 0.0f;
    if (!shim_sticks_twin_anchor(&wx, &wy)) return;
    /* and it steps aside for a stick that has grown, the same way the row does */
    {
        float hw = 0.0f, hh = 0.0f;
        if (shim_hud_twin_home(0, 0, &hw, &hh) && hw > 0.0f && hh > 0.0f) {
            float box[4] = { wx - hw * 0.5f, wy - hh * 0.5f, wx + hw * 0.5f, wy + hh * 0.5f };
            int shim_sticks_clear_of_sticks(float *box);
            if (shim_sticks_clear_of_sticks(box)) {
                wx = (box[0] + box[2]) * 0.5f;
                wy = (box[1] + box[3]) * 0.5f;
            }
        }
    }
    /* both round buttons up (the Forgotten with a Strawman...): the freeze one a slot left */
    if (button == overlay + TWIN_BUTTON_A && *(const unsigned char *) (overlay + TWIN_BUTTON_B + BUTTON_LIVE)) {
        float hw = 0.0f, hh = 0.0f;
        if (shim_hud_twin_home(0, 0, &hw, &hh) && hw > 0.0f) wx -= hw + 3.0f;
    }
    unsigned long c = button + BUTTON_CENTRE;
    if (!readable((const void *) c, 8)) return;
    float *p = (float *) c;
    if (shim_report_active()) {
        static int said;
        if (said < 3) {
            said++;
            char line[170];
            snprintf(line, sizeof(line),
                     "twins' button +0x%lx: the game says %.1f,%.1f, we say %.1f,%.1f"
                     " (canvas %.0f x %.0f)",
                     button - overlay, (double) p[0], (double) p[1], (double) wx, (double) wy,
                     (double) FIELD_W, (double) FIELD_H);
            report_line("hud", line);
        }
    }
    p[0] = wx;
    p[1] = wy;
    DIAG(static float said_x, said_y;
         if (wx != said_x || wy != said_y) { said_x = wx; said_y = wy;
             LOG("hud: the twins' button stands where it was placed — %.1f,%.1f",
                 (double) wx, (double) wy); });
}

/* the game's own place for it, which the editor starts from */
int shim_hud_twin_home(float *x, float *y, float *w, float *h) {
    unsigned long b = shim_image_base();
    if (!b) return 0;
    float px = *(const float *) (b + 0xa9c7c8);
    if (!(px > 0.0f)) return 0;
    float f = 0.5f / px, width = FIELD_W;
    if (!(width > 0.0f)) return 0;
    if (x) *x = width - 60.0f - 147.0f * f;
    if (y) *y = 64.0f * f;
    if (w) *w = 72.0f * f;
    if (h) *h = 64.0f * f;
    return 1;
}

void shim_hud_row(unsigned long group, unsigned long start, unsigned long size,
                  const unsigned long *regs) {
    if (!group || !start || !size || !regs) return;
    /*
     * Only the local player's row. The bomb term is read at a fixed offset from
     * the OVERLAY, so the group has to be the one that sits inside it; another
     * player's group would send that read somewhere meaningless.
     */
    unsigned long overlay = shim_pocket_overlay_at();
    if (!overlay) return;
    /*
     * BOTH rows. A second player on the same device - Esau beside Jacob - has
     * a group of his own (overlay+0x21e48) and the game lays it out with the
     * same function, one button's height below the first. It used to be turned
     * away here, so his row stayed where the game centres it while the first
     * row moved to the stick: the owner saw exactly that (12.09.2026). Our own
     * Marked button belongs to the touch player, so it joins the first row only.
     */
    int second;
    if (group == overlay + GROUP_IN_OVERLAY) second = 0;
    else if (group == overlay + GROUP_IN_OVERLAY + GROUP_STRIDE) second = 1;
    else return;
    int n = row_count(group);
    /*
     * The Marked button (marked.c) stands at the row's left end, in front of
     * the first button, so the right end, the bomb and the gap to the stick are
     * exactly what they were. It is told where every frame the row is laid out,
     * and told it is not there whenever the row cannot say.
     */
    int marked = !second && shim_marked_wanted();
    if (n <= 0 && !marked) { shim_marked_place(0, 0.0f, 0.0f, 0.0f, 0.0f); return; }

    float *place = (float *) start;
    const float *s = (const float *) size;
    float w = s[0], gap = *(const float *) &regs[12];      /* q1 low = s1 */
    if (!(w > 0.0f) || !(gap >= 0.0f)) { shim_marked_place(0, 0.0f, 0.0f, 0.0f, 0.0f); return; }

    /*
     * Where the shooting stick's ring begins. The answer was TAKEN WITHOUT
     * ASKING WHETHER IT EXISTS: when the sticks cannot say (the canvas has
     * just changed and their places are not settled yet), `edge` was whatever
     * the stack held, and the row was moved by a number out of nowhere. That
     * is the gap that grew after switching the screen mode.
     */
    /*
     * Where the row's right end goes. Placed in the editor it is the card's own
     * corner; otherwise it is a fixed gap before the shooting stick's ring AS
     * THE PORT WOULD SEAT IT - the stick's home, not where it has been dragged.
     * The card was sewn to the stick and followed it about; the owner asked for
     * that thread cut (13.09.2026). The row still fills from the right, which is
     * the game's own doing and untouched.
     */
    float want = 0.0f, middle = 0.0f, edge = 0.0f;
    int pinned = shim_sticks_row_anchor(&want, &middle);
    if (!pinned) {
        if (!shim_sticks_shoot_edge(&edge)) { shim_marked_place(0, 0.0f, 0.0f, 0.0f, 0.0f); return; }
        /* the stick lives in the overlay's space, the row in the picture's (screen.c) */
        float shim_screen_overlay_to_area_x(float);
        edge = shim_screen_overlay_to_area_x(edge);
        want = edge - ROW_GAP;
    } else {
        /*
         * A PLACED ROW STEPS ASIDE FOR A STICK THAT HAS GROWN. The place was
         * chosen in the editor, where nothing may lie over a stick - but the
         * stick's size is a setting of its own and can be turned up afterwards.
         * So the row's box is pushed clear here as well, just far enough not to
         * overlap; wedged with nowhere to go, it stays where it was put
         * (sticks.c). Two rows tall, as the editor reserves it, and as wide as
         * this row actually is - our own button at the left end included.
         */
        float span_now = (float) (n > 0 ? n : 0) * w
                       + (float) (n > 1 ? n - 1 : 0) * gap
                       + (marked ? w + gap : 0.0f);
        float box[4];
        box[0] = want - span_now;
        box[2] = want;
        box[1] = middle - s[1] * 0.5f;
        box[3] = middle + s[1] * 1.5f;
        int shim_sticks_clear_of_sticks(float *box);
        if (span_now > 0.0f && shim_sticks_clear_of_sticks(box)) {
            want = box[2];
            middle = box[1] + s[1] * 0.5f;
        }
        /*
         * Both rows go with the card. The game works out each row's height on
         * its own - the first one lifted by a whole button when there are two
         * players (0x860420) - so pinning only the first would tear them apart.
         * The card's place IS the first row's middle, and the second row sits
         * one button below it, exactly as the game stacks them.
         *
         * But only while there IS a second row. The game lifts the first row
         * only when the second player's group is live - the byte at the head
         * of that group, overlay+0x21e48, tested at 0x860420 - and otherwise
         * the one row sits where the bottom row would. A placed card kept its
         * first row up regardless, and a lone row floated a button above an
         * empty band (the owner, 16.09.2026). So a lone row takes the card's
         * bottom place, as the game's own does.
         */
        int two_rows = *(const unsigned char *) (overlay + GROUP_IN_OVERLAY + GROUP_STRIDE) != 0;
        place[1] = (second || !two_rows) ? middle + s[1] : middle;
    }

    /* no button of the game's in the row: ours alone ends where the row would */
    if (n <= 0) { shim_marked_place(1, want - w * 0.5f, place[1], w, s[1]); return; }

    /* the place is the FIRST button's centre, so the row's own width is this */
    float span = (float) n * w + (float) (n - 1) * gap;
    float right = place[0] - w * 0.5f + span;
    float shift = want - right;
    int shim_report_active(void);
    void report_line(const char *kind, const char *text);
    if (shim_report_active()) {
        static int said;
        static int last_n;
        static float last_w;
        if (said < 24 && (said < 10 || n != last_n || FIELD_W != last_w)) {
            said++; last_n = n; last_w = FIELD_W;
            /* and where the game itself keeps the shooting stick, in its own numbers */
            int shim_sticks_shoot_home(float *, float *, float *, float *);
            float hx = 0, hy = 0, ax = 0, ay = 0;
            int home = shim_sticks_shoot_home(&hx, &hy, &ax, &ay);
            char line[300];
            snprintf(line, sizeof(line),
                     "row: canvas %.0f x %.0f, %d buttons of %.1f, gap %.1f,"
                     " first at %.1f, right %.1f, stick %.1f, want %.1f, shift %.1f;"
                     " the game's stick: home %.1f,%.1f anchor %.1f,%.1f (%s);"
                     " row %d, pinned %d",
                     (double) FIELD_W, (double) FIELD_H,
                     n, (double) w, (double) gap, (double) place[0],
                     (double) right, (double) edge, (double) want, (double) shift,
                     (double) hx, (double) hy, (double) ax, (double) ay,
                     home ? "read" : "not seen yet", second, pinned);
            report_line("hud", line);
        }
    }
    /* never past the left edge of the canvas: a very long row keeps its place */
    float front = marked ? w + gap : 0.0f;
    int moves = shift != 0.0f && place[0] - w * 0.5f - front + shift >= 0.0f;
    if (moves) place[0] += shift;
    shim_marked_place(marked, place[0] - (w + gap), place[1], w, s[1]);
    if (!moves) return;

    DIAG(static float said; if (shift != said) { said = shift;
        LOG("hud: the button row ends at %.1f, %.1f before the stick's ring "
            "(%d buttons of %.0f, gap %.1f, moved %.1f)",
            (double) want, (double) ROW_GAP, n, (double) w, (double) gap,
            (double) shift); });
}

/*
 * MEASUREMENT, report builds only: the drop zone against the trinket under it.
 *
 * The drop zone (overlay+0x437d8) has no picture: what a player takes for the
 * button's icon is the HUD's own trinket, drawn by RenderTrinket (0x6e82f0) as
 * a 32x32 quad from its TOP-LEFT corner. For player 0 (HUD entry 0, 0x6eeaa0)
 * slot 0 goes to (44, H-44) and slot 1 to (20, H-68); for the twin's entry
 * (4 and up, 0x6ee740) slot 0 to (W-48, H-65) and slot 1 to (W-76, H-65); all
 * of them plus the HUD-offset inset - (-22, 16) for player 0 and (16, 6) for
 * the twin, times 1 - HudOffset (0x6ed520..0x6ed5a8) - plus the screen shake
 * at Game+0x1af360. The report the zone came from put it at centre (45, 246),
 * 45 x 32 - which reaches x 67.5, while the quad above starts at 44. Before a
 * picture of both twins' trinkets is put in that zone, both boxes are read off
 * the live game here, side by side, every time what the twins carry changes.
 *
 * The trinket rectangles are COMPUTED by the decoded formula from live inputs
 * (canvas, option, shake), not caught in the draw - the trace table has no free
 * slot for 0x6e82f0. The owner's eye on where the trinket really sits closes
 * the loop.
 */
#define CANVAS_W_AT    0xac24d8     /* float: the HUD's canvas width */
#define CANVAS_H_AT    0xac24dc     /* float: and height */
#define GAME_SHAKE     0x1af360     /* Vector2 added to every HUD trinket */
#define ZONE_DROP      0x437d8
#define ZONE_ENABLED   0x1b0        /* byte: takes fingers at all (0x8640fc) */
#define PLAYER_TRINKET 0x1ab0       /* two ints, bit 15 golden */

void shim_hud_trinket_tell(unsigned long overlay, int (*readable)(const void *, unsigned long)) {
    static int said;
    static int was[4] = {-1, -1, -1, -1};
    if (said >= 12 || !overlay || !shim_report_active()) return;
    unsigned long b = shim_image_base();
    if (!b) return;
    unsigned long G = *(const unsigned long *) (b + GAME_GAME);
    unsigned long mgr = *(const unsigned long *) (b + GAME_MANAGER);
    if (!G || !mgr) return;
    unsigned long list = G + G_PLAYERS;
    unsigned long first = *(const unsigned long *) (list + 0x10), past = *(const unsigned long *) (list + 0x18);
    if (!first || past <= first) return;
    unsigned long p0 = ((unsigned long (*)(unsigned long, int)) (b + FN_PLAYER_AT))(list, 0);
    if (!p0 || !readable((const void *) p0, PLAYER_TWIN + 8)) return;
    unsigned long twin = *(const unsigned long *) (p0 + PLAYER_TWIN);
    if (twin == p0 || (twin && !readable((const void *) twin, PLAYER_TRINKET + 8))) twin = 0;
    int now[4];
    now[0] = *(const int *) (p0 + PLAYER_TRINKET);
    now[1] = *(const int *) (p0 + PLAYER_TRINKET + 4);
    now[2] = twin ? *(const int *) (twin + PLAYER_TRINKET) : 0;
    now[3] = twin ? *(const int *) (twin + PLAYER_TRINKET + 4) : 0;
    if (!memcmp(now, was, sizeof(now))) return;
    memcpy(was, now, sizeof(now));
    unsigned long z = overlay + ZONE_DROP;
    if (!readable((const void *) z, ITEM_EXTENT + 8)) return;
    said++;
    const float *zc = (const float *) (z + ITEM_CENTRE);
    const float *zs = (const float *) (z + ITEM_EXTENT);
    float W = *(const float *) (b + CANVAS_W_AT), H = *(const float *) (b + CANVAS_H_AT);
    float off = 1.0f - *(const float *) (mgr + HUD_OFFSET);
    const float *shake = (const float *) (G + GAME_SHAKE);
    float x0 = 44.0f - 22.0f * off + shake[0], y0 = H - 44.0f + 16.0f * off + shake[1];
    float x1 = 20.0f - 22.0f * off + shake[0], y1 = H - 68.0f + 16.0f * off + shake[1];
    float ex = W - 48.0f + 16.0f * off + shake[0], ey = H - 65.0f + 6.0f * off + shake[1];
    char line[300];
    snprintf(line, sizeof(line),
             "canvas %.0f x %.0f, HudOffset %.2f; drop zone %.1f..%.1f x %.1f..%.1f %s;"
             " p0 trinkets %d/%d at %.0f,%.0f and %.0f,%.0f (32x32 from top-left);"
             " twin %s trinkets %d/%d, slot 0 at %.0f,%.0f",
             (double) W, (double) H, (double) (1.0f - off),
             (double) (zc[0] - zs[0] / 2), (double) (zc[0] + zs[0] / 2),
             (double) (zc[1] - zs[1] / 2), (double) (zc[1] + zs[1] / 2),
             *(const unsigned char *) (z + ZONE_ENABLED) ? "takes fingers" : "off",
             now[0] & 0x7fff, now[1] & 0x7fff, (double) x0, (double) y0, (double) x1, (double) y1,
             twin ? "yes" : "none", now[2] & 0x7fff, now[3] & 0x7fff, (double) ex, (double) ey);
    report_line("trinkets", line);
}
