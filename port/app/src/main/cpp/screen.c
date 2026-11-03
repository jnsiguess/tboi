/*
 * The screen: the canvas the game lays out on, and what follows from its size.
 *
 * Everything the game places is anchored to a handful of globals that the fit
 * at 0x670a00 exports from the canvas object 0xb10d30:
 *
 *   0xac24d8/dc  design size in units, float   - what the HUD, the menus, the
 *                                                 room centring and the touch
 *                                                 roads read (~150 sites)
 *   0xac24e0/e4  design size in units, u32     - render targets, one quad a frame
 *   0xa9aff0/f4  the projection's logical size - what a unit is on the screen
 *   0xa9aff8/fc  the window in pixels
 *   0xa9c7c8/d0  the scale pair
 *   0xabec64..70 the drawn area in pixels: w, h, origin x, origin y
 *
 * The game is built for 480 by 270 units and fits that into the window,
 * centring what is left over - the black bars on a 21:9 phone. This file
 * sizes the CANVAS from the window instead: the window divided by a whole
 * number of pixels per unit, so a unit is square and always the same number
 * of screen pixels, and the game lays out on however many units that leaves
 * (624 x 288 at 5 px on a 3120 x 1440 phone). Nothing is stretched and no
 * pixel is ever five wide here and six there; there is simply more of the
 * picture, and it is a little smaller than the fractional fit would make it.
 *
 * That much the engine does by itself once the object is widened. What does
 * not follow by itself is everything written when 480 was a certainty, and the
 * rest of this file is those places: two hit tests that were handed no origin
 * because at the design width the origin is zero, and the tables of boxes that
 * were built from absolute constants for the same reason.
 *
 * Two roads write the globals: the plain wrapper 0x6709a0 (init, resizes,
 * cutscene end) and the cutscene wrapper 0x670888 -> 0x6708f8, which re-runs
 * the fit and then OVERWRITES the float pairs with the current cutscene part's
 * own size - intro.anm2 declares 427x240, the size of its background art. That
 * is why a cutscene is stretched and why it is left that way; the note further
 * down says what was tried.
 */
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <android/log.h>
#include "diag.h"

#if ISAAC_DIAG
#define LOG(...) __android_log_print(ANDROID_LOG_INFO, "isaac", __VA_ARGS__)
#else
#define LOG(...) do { } while (0)
#endif

unsigned long shim_image_base(void);
int shim_report_active(void);
void report_line(const char *kind, const char *text);
extern long g_gl_draws;

/* the canvas rule and what is in force: declared here, set up further down */
#define HIRES 1                     /* the surfaces are created larger; shot_for says by how much */
static void canvas_for(unsigned long w, unsigned long h, float *cw, float *ch, int *k);
static int shot_for(unsigned long h, unsigned long win_w, unsigned long win_h, float cw, float ch);
static int potato_due(void);
static void potato_apply(unsigned char *m);
static float g_units_w, g_units_h;
static int g_scene_shot;
static int g_born_x1;                   /* worth_enlarging: the rule the room surfaces were born under */
static int smooth_x1(void);
static int g_want_shot;

#define GAME_MANAGER 0xac2428
#define M_STATE      0x204c0
#define CANVAS       0xb10d30
#define UNITS_F      0xac24d8       /* float w, h */
#define UNITS_U      0xac24e0       /* u32 w, h */
#define LOGICAL      0xa9aff0       /* float w, h */
#define WINDOW       0xa9aff8       /* u32 w, h */
#define SCALE        0xa9c7c8       /* float sx; sy at +8 */
#define AREA         0xabec64       /* u32 w, h, ox, oy */

typedef struct {
    float units[2], logical[2], scale[2], canvas[10];
    unsigned units_u[2], window[2], area[4];
    int state;
} shot_t;

static void take(shot_t *s) {
    unsigned long b = shim_image_base();
    memcpy(s->units, (void *) (b + UNITS_F), 8);
    memcpy(s->units_u, (void *) (b + UNITS_U), 8);
    memcpy(s->logical, (void *) (b + LOGICAL), 8);
    memcpy(s->window, (void *) (b + WINDOW), 8);
    s->scale[0] = *(float *) (b + SCALE);
    s->scale[1] = *(float *) (b + SCALE + 8);
    memcpy(s->area, (void *) (b + AREA), 16);
    memcpy(s->canvas, (void *) (b + CANVAS), 40);
    unsigned char *M = *(unsigned char **) (b + GAME_MANAGER);
    s->state = M ? *(int *) (M + M_STATE) : -1;
}



/* ---------------------------------------------------------------- widening */

/*
 * Filling the screen without stretching anything.
 *
 * The engine fits a canvas of 480 x 270 units into the window and centres the
 * leftover - the black bars. The desktop build has no such fixed canvas: it
 * derives the logical screen from the window, so a wider window simply shows
 * more units. That is what this does.
 *
 * WHERE. The canvas object at 0xb10d30 is read by exactly one function, the
 * fit at 0x670a00, and it is read at its first instruction. So the width is
 * written here, at the fit's door, from the window size the fit is about to be
 * given. Nothing in the image is patched, the value follows the real screen,
 * and a rotation or a resize recomputes it by itself.
 *
 * WHAT. Two pairs, not one, and this is what the earlier attempt got wrong:
 *
 *     obj+0  -> 0xac24d8 (float) and 0xa9aff0 : the size the HUD, the menus,
 *                                               the room centring and both
 *                                               touch roads lay out against
 *     obj+8  -> 0xac24e0 (u32)               : the size of every off-screen
 *                                               render target and of the
 *                                               full-canvas quad drawn each frame
 *
 * Widening only the first leaves the interface at 585 while the surfaces it
 * draws into stay 480 wide. Both are written, always together.
 *
 * The height is left at 270. The width is 270 * window aspect, so a unit stays
 * square - 3120x1440 gives 585 x 270 and 5.333 px per unit both ways, exactly
 * the scale the game already had vertically. That is why nothing stretches:
 * the fit then finds the canvas ratio equal to the window ratio and the
 * leftover it centres is zero.
 */
#define UNITS_H 270.0f              /* the design height */
#define MAX_H   540.0f              /* 480 by 540 is 8:9 - past that it is a portrait */
#define MIN_W 480.0f                /* never narrower than the game was designed for */
#define MAX_W 900.0f                /* a sanity bound; 32:9 would be 720 */

static int g_wide = 1;                          /* `wide` in port-touch.ini */
/*
 * What we last asked for. The height starts at zero on purpose: it is what the
 * "this canvas is ours" test leans on, and until the first fit there is nothing
 * of ours for a cutscene's own size to be mistaken for.
 */
static float g_units_w = 480.0f, g_units_h;

void shim_screen_wide(int on) { g_wide = on != 0; }
int shim_screen_is_wide(void) { return g_wide; }

/*
 * BACK and SELECT, lower - in the default display only.
 *
 * The menu manager puts both papers in the canvas corners: BACK at
 * (10, H - 55) and SELECT at (W - 80, H - 75), with the paper's own slide-in
 * offset added (0x7699a4 and 0x769a70). The same constant is read again a few
 * instructions later for the paper's touch box, so moving it moves the tap
 * zone with it - the picture and the press cannot come apart.
 *
 * How far down was MEASURED on the owner's screenshot, in canvas units, not
 * modelled: the picture there is 2560 x 1440 for a 480 x 270 canvas, 5.333
 * pixels to the unit, and against that grid the word "Greedier" of the
 * difficulty widget runs to x 440 with its feet on y 200, while the paper's
 * top edge and its pin cross that same place at y 185. Sixteen units of
 * overlap; twenty are given, and the word SELECT! itself still stands clear of
 * the bottom edge (its ink ends at y 255 of 270).
 *
 * An earlier attempt moved them twelve, worked out from the artwork alone, and
 * the pin still sat on the word - which is why this one is taken off the
 * screen the owner actually sees.
 *
 * On the widened canvas the corner is 144 units further out, there is nothing
 * to clear, and the game's own constants stand. Which canvas counts is decided
 * below, by the geometry (shim_screen_papers).
 */
int shim_patch_word(unsigned long off, unsigned was_a, unsigned was_b, unsigned now);

#define MOVZ_W8(top) (0x52A00000u | ((unsigned) (top) << 5) | 8u)
#define PAPER_BACK_Y   0x7699a4      /* BACK:   H - 55, and -35 when lowered */
#define PAPER_SELECT_Y 0x769a70      /* SELECT: H - 75, and -55 when lowered */

/*
 * THE SLIDE THE CORNER PAPERS DO BY THEMSELVES, and how it is switched off.
 *
 * 0x7697f4 both moves and draws the two plaques. It reads the screen from
 * [menu+0x38], goes through a table of bytes at 0x95858c - one per screen, the
 * index being screen minus one - and jumps to a case that walks two floats six
 * at a time towards a target of its own: [menu+0x2c] for BACK and [menu+0x30]
 * for SELECT. The plaque is then drawn at (canvasH - 55 + that float).
 *
 * There are four cases in all:
 *
 *     entry  0 (0x769854)  both towards 0    screens 2,3,5,6,7,10,11,12
 *     entry  8 (0x769874)  both towards 90   screen 1 - right off the bottom
 *     entry 27 (0x7698c0)  BACK to 25, SELECT to 90   screens 4,8,13,14
 *     entry 185 (0x769b38) a case of its own  screen 9
 *
 * Screen 14 is the BESTIARY (its object is menu+0x10f640, built right after the
 * game logs "Menu Bestiary Init"), and on it the slide is ruinous here: in the
 * narrow canvas our own papers already stand twenty lower, so BACK lands at
 * canvasH - 10 and SELECT at canvasH + 35 - half under the screen and wholly
 * off it. In the wide canvas there is nothing to clear in the first place. So
 * the four screens that slide are given entry 0, and the plaques stay put.
 *
 * Screens 4 (the daily run, which we hide), 8 and 13 share that entry and are
 * turned off with it.
 */
#define SLIDE_TABLE_4   0x95858c    /* screens 1..4 : bytes 8, 0, 0, 27 */
#define SLIDE_TABLE_8   0x958590    /* screens 5..8 : bytes 0, 0, 0, 27 */
#define SLIDE_TABLE_14  0x958598    /* screens 13..16: bytes 27, 27, 31, 0 */

void shim_screen_no_slide(void) {
    shim_patch_word(SLIDE_TABLE_4, 0x1b000008u, 0x00000008u, 0x00000008u);
    shim_patch_word(SLIDE_TABLE_8, 0x1b000000u, 0x00000000u, 0x00000000u);
    shim_patch_word(SLIDE_TABLE_14, 0x001f1b1bu, 0x001f0000u, 0x001f0000u);
    LOG("screen: the corner papers no longer slide away on the bestiary");
}

/* asked once a frame, between frames (sticks.c) */
/*
 * WHEN THEY GO LOWER - by where they stand, not by the FULLSCREEN switch
 * (01.10.2026). The first rule lowered them whenever FULLSCREEN was off, which
 * is the owner's 20:9 phone with a 480-wide picture; but with FULLSCREEN on, a
 * 16:9 or 18:9 phone has a canvas of 480..540 too, and there the papers stood
 * on the Greedier row as before. What decides is the geometry:
 *
 *   SELECT's paper begins at x W - 72 (drawn at W - 56, half a paper left);
 *   the difficulty widget hangs off the character's paper, which follows half
 *   the widening, so the Greedier row ends at 437 + (W - 480) / 2.
 *
 * The two part at W = 538; below 560 the papers are lowered, with a margin.
 * A canvas taller than 300 is a tablet's (480 x 360 on 4:3): there the corner
 * is 90 units further down, as on the iPad, and there is nothing to clear.
 */
float shim_screen_width(void);
float shim_screen_height(void);
static int papers_lowered;
int shim_screen_papers_lowered(void) { return papers_lowered; }

void shim_screen_papers(void) {
    static int was = -1;
    float cw = shim_screen_width(), ch = shim_screen_height();
    int lower = cw > 0.0f && cw < 560.0f && ch < 300.0f;
    papers_lowered = lower;
    if (lower == was || !shim_image_base()) return;
    was = lower;
    shim_patch_word(PAPER_BACK_Y, MOVZ_W8(0xC25C), MOVZ_W8(0xC20C),
                    lower ? MOVZ_W8(0xC20C) : MOVZ_W8(0xC25C));
    shim_patch_word(PAPER_SELECT_Y, MOVZ_W8(0xC296), MOVZ_W8(0xC25C),
                    lower ? MOVZ_W8(0xC25C) : MOVZ_W8(0xC296));
    LOG("screen: the corner papers are %s (canvas %.0f x %.0f)", lower ? "twenty units lower" : "where the game puts them",
        (double) cw, (double) ch);
}


/*
 * Change the canvas under a running game.
 *
 * Two things have to happen, and the order matters.
 *
 * The numbers come from the fit at 0x670a00, which the game runs when the
 * window changes - and a window never changes while somebody stands in the
 * options menu, so it is called here by hand with the window the game already
 * has.
 *
 * But the numbers alone give a soft, stretched picture, and that is worth
 * writing down: every off-screen surface the game draws through - shadow,
 * light, shockwave, water, the HQX4 pair - is made ONCE, each guarded by "if my
 * pointer is still empty". Change the canvas under them and the scene is drawn
 * into a surface of the old size and blown up into the new one. Sharp before
 * the switch, soft after it, whichever way it was flipped.
 *
 * So the surfaces are given back first, and the game's own teardown does it:
 * 0x599d44 takes no argument, finds the manager at 0xac2428 itself and hands
 * every one of them back through 0x66da88. Nothing is orphaned and nothing
 * leaks - which is exactly why this is the game's function and not our own
 * zeroing of its pointers. Each surface is then made again, lazily, at the size
 * the canvas has by then.
 *
 * Called between frames. Handing back a surface in the middle of drawing into
 * it is asking for trouble.
 */
void shim_screen_refit(void) {
    unsigned long b = shim_image_base();
    if (!b) return;
    int shim_gl_width(void);
    int shim_gl_height(void);
    int w = shim_gl_width(), h = shim_gl_height();
    if (w <= 0 || h <= 0) return;

    /*
     * NOTHING TO REFIT IS THE COMMON CASE NOW, and it is worth a whole load.
     *
     * This is asked for on every step between the menus and a run, from the
     * days when the two had canvases of their own - 624 x 288 against 585 x 270
     * - and every step really did have to remake every canvas-sized surface.
     * They share one canvas since 12.09.2026 and one multiplier since today, so
     * that walk usually gives back exactly what it takes away.
     *
     * It is not free. The engine rounds a surface up to a power of two and
     * fills the whole of it by hand - the byte-swapping loop at 0x8aea00 - so
     * the three scene surfaces are 33 MB apiece, and making them again was
     * measured at about 150 ms of the floor-loading frame (207 ms without the
     * enlargement against 359 with it, the difference exactly three surfaces).
     * That is the stall the owner sees the loading animation take.
     *
     * So the canvas the rule wants is worked out first, and when it matches
     * what is in force to the pixel, nothing is touched at all.
     */
    {
        /*
         * AND THE MODE DECIDES WHICH CANVAS THE RULE WANTS. canvas_for answers
         * for the WIDE one and knows nothing of the switch; asking it while the
         * switch was off compared the design canvas against the wide one, and
         * after an on-off the numbers happened to match what was in force - so
         * this guard skipped the very refit that should have put the design
         * canvas back. The switch then said "off" while the game went on
         * laying out 585 x 270: the owner saw both modes behave like fullscreen
         * after a toggle, though a fresh start with it off was right
         * (13.09.2026).
         */
        float cw = 0.0f, ch = 0.0f;
        int k = 0;
        if (!g_wide) {
            cw = MIN_W;
            ch = UNITS_H;
            float per_w = (float) w / MIN_W, per_h = (float) h / UNITS_H;
            float per = per_w < per_h ? per_w : per_h;
            unsigned long fitted = (unsigned long) (UNITS_H * per + 0.5f);
            g_want_shot = (HIRES && fitted)
                ? shot_for(fitted, (unsigned long) w, (unsigned long) h, MIN_W, UNITS_H) : 1;
        } else {
            canvas_for((unsigned long) w, (unsigned long) h, &cw, &ch, &k);
        }
        if (cw == g_units_w && ch == g_units_h && g_want_shot == g_scene_shot
            && g_units_w > 0.0f && !potato_due() && (g_born_x1 < 0 || g_born_x1 == smooth_x1())) {
            if (shim_report_active()) {
                static int said;
                if (said < 6) {
                    said++;
                    report_line("screen", "the surfaces are left alone: the canvas has not moved");
                }
            }
            return;
        }
    }

    if (shim_report_active()) {
        static int said;
        if (said < 8) { said++; report_line("screen", "the surfaces are asked for afresh"); }
    }

    /*
     * Two lists, and the second one is why the first attempt still came out
     * soft. 0x599d44 hands back a long row of surfaces - the whole run from
     * 0xac2290 to 0xac23d0 - but three more are made somewhere else entirely,
     * by the lazy creator at 0x66da98, each behind its own "if my pointer is
     * still empty". The image names them itself:
     *
     *     0xac2430  HQX4 Surface (Manager)
     *     0xac2450  Render Surface (Manager)     <- the scene is drawn in here
     *     0xac2460  Color Correction Surface
     *
     * All three are sized from the canvas pair at 0xac24e0/e4. Leave the render
     * surface at the old size and the scene is drawn small and blown up into
     * the new one: sharp before the switch, coarse after it, whichever way it
     * was flipped. That was the soft picture.
     *
     * Handing them back is not enough either, and that cost a crash: the frame
     * that followed walked into 0x8a6dc4, which binds a surface as the render
     * target and reads its texture name at +0xb8, with a null in its hands.
     * A released surface must be remade before anything can draw again.
     *
     * So this follows the game's own rebuild at 0x6717d4 exactly - the same
     * three slots, the same release at 0x8de7ec (first argument 0xac65e0, the
     * second the ADDRESS of the pointer slot), and then straight into the
     * creator at 0x66da98, which takes the settings object. No frame ever sees
     * an empty slot. 0xac2440 is left alone on purpose: the game lets it go in
     * that rebuild, but the creator ends at 0x66dccc without remaking it, so it
     * comes from somewhere else and is none of our business.
     *
     * The fit runs in between, because the creator reads the canvas pair to
     * decide how large to make them.
     */
    unsigned long owner = *(unsigned long *) (b + 0xac2428);
    if (!owner) return;
    /* POTATO's effects, before anything is made again: the creators read them */
    potato_apply((unsigned char *) owner);

    /*
     * Everything goes back, then everything is made again, in one call between
     * frames - the same order a fresh start builds them in. Both halves of that
     * were learned the hard way.
     *
     * Letting a surface go without making it again crashes: the next frame
     * walks into 0x8a6dc4, which binds a surface as the render target and reads
     * its texture name at +0xb8, holding a null. That is the effect row, and it
     * is not lazily rebuilt on the way into a run - it is simply bound.
     *
     * Making one half and leaving the other to catch up is worse than not
     * touching either: asking the frame to rebuild its own three (the byte at
     * +0x27bcc8) puts them a frame behind, and for that frame the effect row is
     * cut for the new canvas while the scene is still drawn for the old one.
     *
     * So both rows are handed back, the canvas is fitted, and both are made
     * again, here, before the next frame starts:
     *
     *   0x599d44  hands back 0xac2290..0xac23d0 - shadow, light, water
     *   0x8de7ec  hands back one slot, for the two sets 0x599d44 misses
     *   0x6709a0  the game's resize road: the fit and the three steps that
     *             follow it (the note further down says which)
     *   0x5988f4  makes the effect row again, sized from 0xac24d8/dc
     *   0x66da98  makes 0xac2430/50/60, sized from 0xac24e0/e4
     *   0x889d18  makes 0xac3b98..0xac3be8, likewise
     *   0x767af0  makes 0xac2498, the menu's colormod surface, likewise
     *
     * There are THREE sets, not two, and missing the third is what kept the
     * picture soft after everything else was right. It was found by walking the
     * image for every call to the surface constructor at 0x8de4e4 and reading
     * the name each one is given: 0xac3b98 is the Pixelation Surface, which is
     * what keeps the pixels square. Left at the old size it was resampled, and
     * only walking into a run - which rebuilds that set on its own - put it
     * right. That is why a run "fixed" the picture and the menu never did.
     *
     * Both creators are lazy - every slot behind "if my pointer is still empty"
     * - so neither can double up on anything that survived.
     */
    static const unsigned long SURFACES[] = {
        /* HQX4, Render Surface, Colour Correction - made by 0x66da98 */
        0xac2430, 0xac2450, 0xac2460,
        /* Pixelation, Bloom, Old TV, Hallucination, its snapshot, Colormod
         * - made by 0x889d18. This set was the one being missed. */
        0xac3b98, 0xac3ba8, 0xac3bb8, 0xac3bc8, 0xac3bd8, 0xac3be8,
        /* Menu Colormod Surface - made by 0x767af0 from the menu's own draw.
         * Found by walking every call of the surface constructor 0x8de4e4:
         * canvas-sized like the rest, and in no rebuild but this one. */
        0xac2498,
    };
    void (*let_go)(unsigned long, unsigned long) =
        (void (*)(unsigned long, unsigned long)) (b + 0x8de7ec);

    /*
     * The effect row is NOT handed back here, and that is a correction.
     *
     * 0x599d44 gives back every surface from 0xac2290 to 0xac23d0 - the
     * shadows, the light overlay, the water, the shockwave. The game's own
     * rebuild (0x6717d4), which is what a resolution change runs on the
     * desktop, gives back four surfaces and none of those. Ours went further,
     * and the price showed on the phone: after a switch of the display mode
     * the room lost its lighting until the player walked through a door. What
     * the room had drawn into those surfaces was thrown away, and nothing puts
     * it back until the room is entered again - which is exactly when the game
     * fills them.
     *
     * So we now do what the game does. Those surfaces keep the size the old
     * canvas gave them until the next room, where they are made afresh at the
     * new one; the scene itself, which is the part the eye measures sharpness
     * by, is remade here with the three below.
     */
    /* ((void (*)(void)) (b + 0x599d44))();  the effect row - the game keeps it */
    for (unsigned i = 0; i < sizeof(SURFACES) / sizeof(SURFACES[0]); i++)
        let_go(b + 0xac65e0, b + SURFACES[i]);

    /*
     * The game's OWN resize road, not the fit alone - and calling only the fit
     * was the whole of the broken geometry.
     *
     * 0x6709a0 is what the engine runs when the window changes, and it does
     * four things, in this order:
     *
     *   0x670a00(0xb10d30, w, h)     the fit - all we used to call
     *   0x8a7134(0xac55d8, -1, -1)   the renderer's remembered width and
     *                                height (+0x304, +0x308) set to -1, then
     *                                0x8a6bb4, which on seeing -1 asks the
     *                                object itself for both (its vtable at
     *                                +0xb8 and +0xc0) and rebinds the
     *                                renderbuffer to what it answers
     *   0x88a4f0(*0xac3b90)          +0x1cf3e4 = 1, another "recompute me"
     *   *(manager + 0x27bcc8) = 1    the frame rebuilds its four surfaces
     *
     * The second one is why the tiles walked. 0xac55d8 is a global that
     * outlives menus and rooms, so a canvas changed without it left the whole
     * engine drawing to the old grid - and it stayed wrong through leaving the
     * run, switching in the main menu and walking back in, which is exactly
     * what the owner saw. Hiding the row from the pause menu could never have
     * helped, and it has been put back.
     *
     * Our own rebuilding below stays: the frame's flag covers 0xac2430..0x2460
     * and nothing else, while the effect row, the post pass and the menu's
     * colormod are ours to remake.
     */
    ((void (*)(unsigned long, unsigned long, unsigned long)) (b + 0x6709a0))
        (owner, (unsigned long) w, (unsigned long) h);

    ((void (*)(void)) (b + 0x5988f4))();                  /* the effect row again */
    ((void (*)(unsigned long)) (b + 0x66da98))(owner);    /* the three */
    ((void (*)(void)) (b + 0x889d18))();                  /* the post pass */
    ((void (*)(void)) (b + 0x767af0))();                  /* and the menu's colormod */

    LOG("screen: refitted for a %d x %d window, every canvas-sized surface remade", w, h);
    /* the tester's report, when one is open: a switch is worth a line of its own */
    {
        int shim_report_active(void);
        void report_line(const char *, const char *);
        if (shim_report_active()) {
            char line[160];
            snprintf(line, sizeof(line), "FULLSCREEN switched %s: canvas %.0f x %.0f in a window %d x %d",
                     g_wide ? "on" : "off", (double) g_units_w, (double) g_units_h, w, h);
            report_line("screen", line);
        }
    }
}


/*
 * A whole number of pixels per unit.
 *
 * The frame is drawn on the canvas grid, one pixel to the unit - the Render
 * Surface at 0xac2450 is created at exactly the u32 pair 0xac24e0/e4 - and
 * blown up to the window in one quad, sampled NEAREST (the only filter the
 * image ever sets: 0x8db53c, 0x8db54c, 0x8db708). At 5.333 pixels to the unit
 * that makes a cell five pixels wide twice and six the third time, and a sprite
 * creeping by one unit lands on a different phase of that pattern each step:
 * one row of it moves by five screen pixels, the next by six. That is the logo
 * torn in half at the bottom of its bob, and the rocks and walls jittering as
 * the room scrolls. It cannot be filtered away without going soft.
 *
 * So the canvas is the window divided by a whole number, and the remainder is
 * given to the picture: on 3120 x 1440 that is 5 px to the unit and a canvas
 * of 624 x 288, on 2712 x 1220 it is 4 px and 678 x 305. More of the room is
 * seen and everything is a little smaller - 6 and 11.5 per cent there - which
 * is the trade the owner chose on 09.09.2026, over black bars, after seeing it
 * on a tester's 2712 x 1220 phone: that screenshot measures 4.0 px per unit and
 * a canvas of 678 x 305, with the title's knife, which the design itself cuts
 * at -4 units, standing whole in the room above it. FULLSCREEN OFF keeps the
 * design canvas and the original fractional fit, unchanged.
 *
 * Exactness is kept to the pixel, which the first attempt did not do: the
 * units are whole numbers (the window divided down, not up), and the drawn
 * area is written as exactly units * k by the letterbox pass - the fit's own
 * rule would give 5.003 on a window that is not a multiple of k, and a
 * duplicated column somewhere in the middle. What does not divide is at most
 * k - 1 pixels a side and goes to the bars: four pixels at most on a 1440
 * screen, which no one can see.
 *
 * WHICH whole number, and this is the part that took a second pass.
 *
 * Taking the largest k that still shows the whole design height (270 units)
 * makes the picture as SMALL as a whole number can: on 3120 x 1440 that is 5,
 * and the playing field comes out 6.7 per cent smaller than the fractional fit
 * would draw it - measured on the owner's screen, the character 109 px tall
 * against 117. He was right to call that the wrong way round: a fullscreen
 * mode that shows the room smaller than the letterboxed one has no point.
 *
 * The room's backdrop stretches to whatever canvas it is given - more of it is
 * simply seen, and it mirrors itself at the edges - but the FIELD the player
 * walks in is fixed in units, so its size on screen is nothing but units times
 * k. Bigger k, bigger field. So k is taken as large as it can be while the
 * canvas still holds what has to be seen: the field is 182 units tall and the
 * walls and the HUD sit around it, which FIELD_UNITS_H fixes at 240.
 *
 * Raising k to 6 was tried on the owner's screen (canvas 520 x 240) and taken
 * back the same hour: 240 units cut too much off - the room, the HUD and the
 * menus all share this one canvas, so the menu lost its edges as well. There
 * is no middle setting to be had: on a 1440-tall screen the whole numbers are
 * 5 (288 units) and 6 (240), and nothing lies between them. The threshold is
 * therefore the design height itself, which is the same as taking the largest
 * k that still shows all 270 units:
 *
 *     3120 x 1440   k = 5, canvas 624 x 288
 *     2712 x 1220   k = 4, canvas 678 x 305
 *     2400 x 1080   k = 4, canvas 600 x 270
 */
#define FIELD_UNITS_H 270.0f        /* the least the canvas may be: the design height */

/*
 * Drawing the scene larger than the canvas, so the picture can be as big as
 * the fractional fit makes it without the fractional fit's crawling.
 *
 * The engine keeps TWO sizes in the canvas object, and until now we wrote the
 * same numbers into both:
 *
 *     obj+0 -> 0xac24d8 (float), 0xa9aff0   the size the HUD, the menus and
 *                                           the room are laid out against
 *     obj+8 -> 0xac24e0 (u32)               the size of every off-screen
 *                                           surface, and of the quad that
 *                                           puts the finished scene on screen
 *
 * Written apart, they do exactly what the owner asked for: the layout stays at
 * 270 units tall, so nothing is cut off the menus or the HUD, while the
 * surfaces are made shot times bigger, so the projection draws every sprite at
 * a whole shot of pixels to the unit. Whole is what matters: a sprite moving one
 * unit moves five surface pixels exactly, and nothing dances.
 *
 * The finished surface then goes to the screen almost one to one - 2925 x 1350
 * onto 3120 x 1440, a stretch of seven per cent - which is a world away from
 * stretching a 585-wide picture five times over. What that stretch costs is a
 * touch of softness, and it is bought with a linear filter on that one
 * surface (gl.c): with NEAREST the odd row would double and the crawl would be
 * back, in miniature.
 *
 * The cost is real and belongs here: the surfaces are that many times squared
 * larger in memory, and the scene covers that many more pixels each frame.
 *
 * Two attempts went wrong before this one, and both for the same reason: the
 * u32 pair at 0xac24e0/e4 is read by TWO different things - the size every
 * off-screen surface is created at, and the quad that puts the finished scene
 * on the screen. Enlarging the pair enlarged both, and the picture ended up in
 * a corner. Measured, not guessed: the report now lists every viewport the
 * game sets, and in a run the scene's is "0,0 585 x 270" - the canvas exactly.
 *
 * Three attempts went wrong before the mechanism was read rather than guessed
 * at, and the reading settles it. 0x8a6dc4, the road a surface takes to become
 * the drawing target, ASKS THE SURFACE how large it is - vtable +0x50 and
 * +0x58 - and sets the viewport from the answer; 0x8a6bb4, a few instructions
 * earlier, brings the depth buffer to the same size. So a surface created
 * larger carries its viewport and its depth buffer with it, and nothing else
 * has to be touched. Enlarging the texture by hand, or the viewport by hand,
 * made the halves disagree instead: the framebuffer went incomplete and the
 * room vanished while the HUD, on another road, stayed.
 *
 * And the enlargement itself belongs at the ONE door every surface passes
 * through: the constructor at 0x8de4e4, which takes the width and height in
 * x1 and x2. Writing the u32 pair instead was not enough - measured, the
 * report showed both "viewport 0,0 2925 x 1350" and "viewport 0,0 585 x 270"
 * in the same run, because the effect row is created from the FLOAT pair (the
 * layout one, which must stay at the canvas) while the render surfaces are
 * created from the u32 one. Half the passes grew and half did not, and the
 * picture fell between them.
 *
 * At the constructor there is no such split: whatever a creator read, it hands
 * the same two numbers over, and a request for exactly the canvas is answered
 * with a surface that many times larger. Everything downstream follows by itself -
 * the viewport from the surface's own size, the depth buffer with it.
 *
 * Only the RUN is drawn this way. The menus keep the whole-number canvas they
 * were given, where the sheet of paper sits at a comfortable distance and
 * nothing is cut off; and a menu does not move, so no seam can crawl in it.
 */
#define HIRES 1                     /* the surfaces are created larger; shot_for says by how much */
/*
 * How many pixels to the unit the scene is drawn at - a WHOLE number, which is
 * what keeps the seams still, chosen so the finished scene comes out about the
 * size of the window. Then it reaches the screen at very nearly one to one,
 * and the little that is left is smoothed by the linear filter.
 *
 * A fixed number cannot suit every screen: at three, this phone stretches the
 * scene by 1.78 and a 4K tablet by 2.96, and the softness grows with it. Taken
 * from the window instead, every screen lands within a few per cent of one to
 * one, and each pays for its own sharpness - a 720p phone draws a scene of
 * 1440 x 810, a tablet draws its own size, and nobody carries anyone else's
 * cost.
 *
 *     window 1440 tall -> 5   (2925 x 1350, stretched 1.07)
 *     window 1600      -> 6   (3510 x 1620, 0.99 - a shade under one to one)
 *     window 1080      -> 4   (2340 x 1080, exactly one to one)
 *     window  720      -> 3   (1755 x  810, 0.89)
 */
#define SHOT_MAX 8                  /* a sanity bound, not a policy */
/*
 * POTATO (the owner, 25.09.2026): for a weak phone, what iOS itself does for
 * one - the heavy effects off. The ceiling -1 (shotmax=-1 in port-touch.ini)
 * turns off the effects config_ios.ini can turn off; the scene it is drawn at
 * is one canvas, as iOS draws it (the owner, 17.10.2026; from 14.10 to 17.10
 * it was two or three canvases, in a run and in the menus).
 *
 * Half the canvas came first and was "unreadable mush" (the owner, the same
 * day): the art is pixel art at one canvas, and there is nothing below it.
 *
 * config_ios.ini is read at 0x7eb364 into the options object at M+0x55ec8,
 * every key defaulting to 1; the port ships no such file, so every effect is
 * on. Each byte is read where it is used - the effect draws check it every
 * frame (lighting 0x5c1698, bloom 0x88e6c8, water 0x5c1f7c...) - and the
 * surfaces behind them are made only while it is set, each lazily, "if my
 * pointer is still empty": the effect row by 0x5988f4 (light, shockwave,
 * caustics, water), the colour correction surface by 0x66da98, the bloom row
 * by 0x889d18. So the bytes change only inside the refit, between frames,
 * after the surfaces are handed back and before they are made again:
 *   - off: the colour correction and bloom surfaces are not made again, and
 *     nothing draws with them - walked for every reference to the two slots:
 *     the frame loop checks the byte (0x67188c, 0x671958; bloom 0x88e6c8,
 *     0x88ebec), 0x6ec0f8 checks the pointer (0x6ec124), the rest only hand
 *     them back;
 *   - back on: they are made again before the next frame. The effect row is
 *     never handed back (see the refit), and it was made at startup with
 *     every effect on, so its surfaces are there whichever way it goes.
 *
 * Off: shockwave, caustics, bloom, water surface and its copy (+0x5d, set
 * from +0x5c at 0x7eb500). Kept: lighting (below), colour correction (see the table
 * below - without it the scene skips the canvas altogether), interpolation
 * (motion, not cost), the config's Filter (its option is off for good -
 * loader.c filter_gone) and Pixelation (Pixelation is an item's effect, part
 * of play) and the colour modifier (rooms and mods tint with it).
 */
#define SHOT_POTATO (-1)            /* the ceiling that asks for it */

/*
 * COLOUR CORRECTION STAYS ON (the owner, 28.09.2026, with two screenshots:
 * POTATO came out SHARPER than ORIGINAL, at the screen's own resolution). The
 * frame loop binds the screen first (0x8a705c at 0x671838) and the colour
 * correction surface only while its byte is set (0x671884..0x6718c4: the
 * byte, then 0x7ec414, then 0x8a6dc4 on 0xac2460); with the byte off the scene
 * is drawn STRAIGHT ONTO THE SCREEN, every sprite at the screen's pixels, and
 * the canvas-sized picture ORIGINAL is meant to give never exists. So that
 * byte is left alone: the scene goes through the surface at one canvas, as
 * ORIGINAL's does, and only the effects that cost a pass of their own go.
 */
/*
 * LIGHTING STAYS ON (the owner, 26.10.2026): Curse of Darkness is drawn by the
 * lighting pass - its heavy vignette, and the dark rooms with it - and with
 * EnableLighting off the floor was simply lit: "a cheat". Darkness is play,
 * not decoration, so POTATO keeps the game's byte.
 */
static const unsigned POTATO_OFF[] = {
    0x55f1f,                        /* EnableShockwave */
    0x55f20,                        /* EnableCaustics */
    0x55f23,                        /* EnableBloom */
    0x55f24,                        /* EnableWaterSurface */
    0x55f25,                        /* its copy, read by the water draws */
};
#define POTATO_BYTES ((int) (sizeof(POTATO_OFF) / sizeof(POTATO_OFF[0])))
static unsigned char g_potato_kept[POTATO_BYTES];
static int g_potato_off;            /* the bytes are ours, the kept ones hold the game's */

/*
 * The owner's ceiling from port-touch.ini, nought when he has not set one.
 * It only ever lowers what the rule below works out - see the note by the key
 * itself in sticks.c.
 */
static int g_shot_ceiling;

/* the effects are not yet as the ceiling wants them: a refit is owed */
static int potato_due(void) {
    return (g_shot_ceiling == SHOT_POTATO) != g_potato_off;
}

/*
 * Only ever from the refit, between frames, before the surfaces are made
 * again - never on its own: an effect turned on with its surface not yet made
 * is drawn into a null (0x8a6dc4 reads +0xb8 of it). The game's bytes are kept
 * and put back, so leaving POTATO is the config it started with.
 */
static void potato_apply(unsigned char *m) {
    int want = g_shot_ceiling == SHOT_POTATO;
    if (!m || want == g_potato_off) return;
    for (int i = 0; i < POTATO_BYTES; i++) {
        if (want) {
            g_potato_kept[i] = m[POTATO_OFF[i]];
            m[POTATO_OFF[i]] = 0;
        } else {
            m[POTATO_OFF[i]] = g_potato_kept[i];
        }
    }
    g_potato_off = want;
    LOG("screen: POTATO - the heavy effects are %s", want ? "off" : "back as they were");
}

/*
 * How many times larger than the canvas the scene is drawn - a CEILING, so the
 * finished picture is REDUCED into the screen and never stretched onto it.
 *
 * Measured, and the measurement is what makes this free. The engine rounds a
 * surface up to a power of two: at five the scene is 2925 x 1350 and the
 * texture behind it is 4096 x 2048; at six it is 3510 x 1620 and the texture
 * is the same 4096 x 2048. Same memory, same 33 megabytes filled and uploaded
 * whenever the surface is made - the floor load does not grow by a
 * millisecond. What changes is the last step: five stretches the scene onto
 * 3120 x 1440 and a stretch of seven per cent smears every pixel edge across
 * two, which is the coarse picture the owner saw; six brings it down instead,
 * and several drawn pixels average into one.
 *
 * The only real cost is the fill: the scene covers 44 per cent more pixels a
 * frame, against three to four milliseconds a frame in a run.
 *
 * The width is bounded as well - a canvas of 585 at six is 3510, and a canvas
 * as wide as MAX_W would be past 4096, where the texture would jump to the
 * next power of two and stop being free.
 *
 * READ AGAIN 13.10.2026: every picture compared here was POINT-SAMPLED onto
 * the window - the linear filter never reached the scene's texture (see
 * the note above shim_screen_surface_born) - so "five is coarse" was NEAREST doubling
 * every fifteenth row, and six dropped every ninth instead. With the filter
 * in place the two measure alike for walking (the research notes of
 * 13.10.2026, walls_model.py: a stripe's width wobbles 0.06 px at five, 0.12 at six,
 * against a whole pixel point-sampled; a line one scene pixel thin keeps its
 * light to within 6 per cent reduced and 2 enlarged, where point sampling
 * loses it altogether in one frame and doubles it in another). A reduction
 * below 2:1 is safe with LINEAR, so the rule stays as the owner chose it.
 */
#define SURF_MAX 4096.0f

static unsigned pot(float v) {
    unsigned want = (unsigned) (v + 0.5f), p = 1;
    while (p < want && p < 8192u) p <<= 1;
    return p;
}

/* for the settings writer (sticks.c): 0 when the rule is left to decide */
int shim_screen_shot_ceiling_get(void) { return g_shot_ceiling; }

/*
 * A new ceiling is a refit, asked for at once. The multiplier is worked out
 * by the fit, and the fit only ran on its own at the next load - so a render
 * scale chosen in Tweaks showed only after entering a run (the owner,
 * 18.09.2026). sticks.c refits between frames when this says so, and the
 * refit leaves everything alone when the multiplier comes out the same.
 */
static int g_ceiling_moved;

void shim_screen_shot_ceiling(int n) {
    int was = g_shot_ceiling;
    g_shot_ceiling = (n == SHOT_POTATO || (n > 0 && n <= SHOT_MAX)) ? n : 0;
    if (g_shot_ceiling != was) g_ceiling_moved = 1;
    LOG("screen: the scene may be drawn at most %d times the canvas%s",
        g_shot_ceiling,
        g_shot_ceiling ? " (asked for in port-touch.ini)" : " - none set: ORIGINAL");
}

static int shot_by_rule(unsigned long h, float cw, float ch);

/*
 * THE GAME'S OWN UPSCALER IS GONE (the owner, hotfix 1.75).
 *
 * The Filter option (options.ini `Filter=`, the byte at M+0x55eed) drew the
 * scene at one canvas into the "HQX4 Surface (Manager)" and laid that on the
 * screen through shaders/hq4x.fs, and while it was on our enlargement stood
 * aside for it (one canvas, so as not to feed the filter a decimated 6x
 * render). The byte is now a store of zero wherever the game writes it
 * (loader.c filter_gone) and its row is the RUMBLE slider (rumble_menu_item),
 * so the scene always takes our multiplier and nothing here asks about it.
 */

/*
 * What the rule alone would pick on this screen, remembered as it is worked
 * out. The Tweaks page needs it to know which steps of Render Scale are real
 * here: the ceiling can only LOWER, so a step above the rule does nothing and
 * showing it would be a lie.
 */
static int g_shot_rule = 4;

int shim_screen_shot_rule(void) { return g_shot_rule; }

/*
 * ENHANCED (the owner, 25.10.2026: "three modes - POTATO, ORIGINAL and
 * ENHANCED; ENHANCED is today's MEDIUM, the most balanced - above it nothing
 * much shows"). LOW, HIGH, ULTRA and EXTREME are gone, and any ceiling above
 * one canvas is ENHANCED: two thirds of the way from one canvas to the step at
 * which the scene is the picture's size (its height over the canvas's,
 * rounded), as tweaks.c named MEDIUM - 1080 tall 3, 1440 tall 4, 720 tall 2 -
 * never below two canvases and never above the rule. An old HIGH or ULTRA kept
 * in port-touch.ini is read as ENHANCED, as the Tweaks row shows it.
 */
static int g_shot_enhanced = 2;

int shim_screen_shot_enhanced(void) { return g_shot_enhanced; }

static int enhanced_for(unsigned long h, float ch) {
    if (!(ch > 0.0f)) ch = UNITS_H;
    int high = (int) lroundf((float) h / ch);
    if (high < 1) high = 1;
    int m = (int) lroundf(1.0f + (float) (high - 1) * 2.0f / 3.0f);
    return m < 2 ? 2 : m;
}

/*
 * POTATO'S SCENE IN A RUN IS DRAWN INSIDE AT TWO OR MORE (the owner,
 * 14.10.2026; the research notes of that day, texels).
 *
 * At one canvas the game can only move a sprite by a whole canvas pixel: the
 * Colormod Surface is the canvas, ImageBase::Render rounds every corner to it
 * (worth_enlarging), and a slow camera steps 0, 1, 1, 0 units - five screen
 * pixels at a time on a 1440-tall screen. Laid at a whole k with bars round
 * it, the units were even and the stepping still showed (the owner, the same
 * day): that is what one canvas IS, and no last stretch can hide it. From
 * 14.10 to 17.10 x1 was taken out of the run for that (POTATO drawn at two or
 * three canvases, ORIGINAL gone); the owner has them back as iOS draws them
 * (17.10.2026): the stepping is the original's own, and these two modes are
 * for those who want the original or a weak phone. Every other mode keeps the
 * enlarged scene. The effect bytes go by the ceiling alone (potato_due).
 */
/*
 * ONE CANVAS, AND THE CAMERA SPLIT (the owner, 17.10.2026: "make x1 properly").
 *
 * ORIGINAL and POTATO draw the room as iOS does - the Colormod Surface, where
 * every tile, rock, door, wall and entity of the room is drawn (worth_enlarging),
 * at one canvas - and that is the whole of their cost saved. What one canvas
 * costs in looks is the camera: the game rounds every sprite to the pixel of
 * the surface it draws into (ImageBase::Render 0x8b43e4), so at one canvas a
 * camera crawling through a big room moves the whole room by whole canvas
 * pixels - 5.33 screen pixels a step on a 1440-tall screen. Measured
 * (reports/2026-10-17/camera): in an ordinary room the camera stands still on
 * whole units; in a big room it was off a whole unit in 505 of 979 frames.
 *
 * So the camera is split. The room's render scroll offset, room+0x1938 (the
 * room at game+0x21550; every render path that places a room sprite adds it -
 * 0x5c14d0, 0x7e82e0, 0x85c3fc and the rest; the five writers 0x61c8b8..0x61e5b8
 * are the camera's own motion, which never sees what is done here), is put on
 * its WHOLE unit while Room::Render (0x5c2630, called from Game::Render at
 * 0x88e838) draws into the Colormod Surface: the room lands there on the canvas
 * grid as one picture, nothing in it can step apart from the rest. The true
 * value goes back where the game lays that surface on the scene (0x88e9cc, the
 * quad's origin at [sp+0x50]/[sp+0x78], zeroed by the two stores before it),
 * and the origin takes the FRACTION, negated: the room is drawn f units too far
 * along (its sprites stood at world - (c - f)) and the quad moves it back. The
 * HUD and everything drawn after the laying see the true camera.
 *
 * THE SCENE AT ONE CANVAS TOO (the owner, 21.10.2026). From 17.10 the scene
 * these two modes lay the room on was enlarged (x2, x3 on a 1440-tall screen)
 * so the fraction landed to half a unit and the room glided. The renderer
 * audit of 21.10 put the price on it: four times the scene pass and a 2048 x
 * 1024 target instead of 1024 x 512 - ORIGINAL and POTATO dearer than 1.74's
 * POTATO on exactly the weak cards they are for. Now the scene is one canvas
 * as well, as iOS draws it: the laying rounds the fraction to a whole canvas
 * pixel, so in a big room and in a transition the room moves as ONE piece in
 * whole canvas pixels - the iPhone's own camera - and nothing in it steps
 * apart from the rest, which is what the split is for. The smooth glide is
 * LOW and up, where the room itself is enlarged.
 *
 * WHOLE, NOT SPLIT, AT ONE CANVAS (the owner, 22.10.2026: "in big rooms the
 * picture breaks into four parts that move apart - keep an honest x1 and mend
 * the glitches"). The split laid the room's fraction back as the laying quad's
 * origin. On an enlarged scene that was a shift of whole scene pixels; at one
 * canvas it is a canvas-sized picture point-sampled a fraction of a texel off,
 * and near half a texel the two triangles of the quad and the rows of the
 * rasteriser pick their texels by rounding noise - the picture tore into
 * pieces. A x2 menu in its place (22.10, an hour) was no answer either: the
 * owner wants one canvas, honest, everywhere.
 *
 * So at one canvas nothing fractional is laid at all. The room, a transition's
 * two rooms and the menu are drawn on the camera ROUNDED to its nearest unit,
 * and laid where they are. Everything the game draws around them with the
 * true camera - the effects, the shadows, the HUD's world markers - it rounds
 * itself, sprite by sprite, to that same nearest unit for anything that stands
 * on a whole unit of the world: round(p + c) = p + round(c). The whole picture
 * steps by one canvas pixel at once, as one piece - the iPhone's camera without
 * the iPhone's shimmer. On an enlarged scene (a mode with smooth_x1 and a scene
 * above one canvas, none today) the split would lay the fraction as before.
 */

/* ORIGINAL by default (the owner, 18.10.2026): a ceiling never set is ORIGINAL */
static int smooth_x1(void) {
    return g_shot_ceiling == SHOT_POTATO || g_shot_ceiling == 1 || g_shot_ceiling == 0;
}

/* one canvas: the camera is put on its nearest unit and nothing fractional is laid (WHOLE, NOT SPLIT) */
static int whole_camera(void) { return g_scene_shot <= 1; }


#define ROOM_PTR_IN_GAME   0x21550     /* game -> the room */
#define ROOM_SCROLL        0x1938      /* room: the render scroll offset, a Vector2 */
#define ROOM_RENDER_RET    0x88e83c    /* Room::Render's return into Game::Render */
#define COLORMOD_ON        0x55f26     /* manager: EnableColorModifier */
#define TRACE_REGS_LR      9           /* trace.c's saved array: x30 at index 9 */
#define TRACE_REGS_SP      224         /* the hooked frame's sp, past the saved registers */

static float g_cam_true[2];
static float *g_cam_at;                 /* non-null while the room is drawn on the whole camera */
static float g_lay[2];                  /* where the laying moves the Colormod Surface, in units */
static int g_lay_due;                   /* a split was made this frame: the laying owes its shift */

/*
 * THE MEASURE OF IT (report builds): per frames window, how many frames the
 * room was split and how many a transition was, the largest fraction carried,
 * and how many frames were laid with a shift. Printed beside the camera's line
 * (objc.c cam_window).
 */
static long g_split_room, g_split_trans, g_split_laid, g_split_menu;
static unsigned long g_colormod_w, g_colormod_h;   /* the size the Colormod Surface was last made at */
static unsigned long g_menu_colormod_w, g_menu_colormod_h;
static float g_split_max;
static void split_note(float fx, float fy) {
    float f = fabsf(fx) > fabsf(fy) ? fabsf(fx) : fabsf(fy);
    if (f > g_split_max) g_split_max = f;
}
void shim_screen_split_window(char *out, unsigned long room) {
    if (!room) return;
    snprintf(out, room, "; split: room %ld, transition %ld, menu %ld frames, laid %ld, largest fraction %.3f;"
             " Colormod Surface %lu x %lu, Menu Colormod Surface %lu x %lu, scene x%d",
             g_split_room, g_split_trans, g_split_menu, g_split_laid, (double) g_split_max,
             g_colormod_w, g_colormod_h, g_menu_colormod_w, g_menu_colormod_h, g_scene_shot);
    g_split_room = g_split_trans = g_split_laid = g_split_menu = 0;
    g_split_max = 0.0f;
}

/* hook '{' at Room::Render's entry */
void shim_screen_room_render(unsigned long *regs) {
    if (!regs || !smooth_x1()) return;
    unsigned long b = shim_image_base();
    if (!b || regs[TRACE_REGS_LR] != b + ROOM_RENDER_RET) return;      /* Game::Render's own call only */
    unsigned long m = *(const unsigned long *) (b + GAME_MANAGER);
    if (!m || !*(const unsigned char *) (m + COLORMOD_ON)) return;     /* the room is not drawn apart */
    unsigned long G = *(const unsigned long *) (b + 0xac3b90);
    unsigned long room = G ? *(const unsigned long *) (G + ROOM_PTR_IN_GAME) : 0;
    if (!room) return;
    float *c = (float *) (room + ROOM_SCROLL);
    if (!isfinite(c[0]) || !isfinite(c[1])) return;
    g_cam_true[0] = c[0]; g_cam_true[1] = c[1];
    if (whole_camera()) {                  /* WHOLE, NOT SPLIT: the nearest unit, laid as it is */
        c[0] = roundf(c[0]); c[1] = roundf(c[1]);
        g_cam_at = c;
        g_lay_due = 0;
        g_split_room++;
        return;
    }
    float wx = floorf(c[0]), wy = floorf(c[1]);
    /*
     * A sprite stands at its render position PLUS the camera (Entity::Render
     * 0x7e82c0..0x7e82ec: 0x7f1fa0, then + room+0x1938), so on the whole camera
     * the room is drawn f units short, and the laying adds f back. (The first
     * build took -f: the room ran up to two units off and snapped back - the
     * owner's "the camera shakes now", 18.10.2026.)
     */
    g_lay[0] = c[0] - wx; g_lay[1] = c[1] - wy;
    g_lay_due = 1;
    c[0] = wx; c[1] = wy;
    g_cam_at = c;
    g_split_room++;
    split_note(g_lay[0], g_lay[1]);
}

/*
 * A ROOM TRANSITION, THE SAME WAY. The slide (0x6a8dd0, Game::Render's call at
 * 0x88e82c instead of Room::Render) draws the old room's and the new room's
 * surfaces into the Colormod Surface at offsets it works out from the slide's
 * progress - [sp+0x58] and [sp+0x60] of its frame, written by 0x6a92f8/0x6a9300
 * and read from 0x6a9334 on - fractions of a unit every frame, which at one
 * canvas are rounded to whole ones. Hook '[' at 0x6a9314, after both are
 * written: both lose the new room's fraction - they keep their own
 * distance - and the laying adds it back.
 */
#define TRANS_OLD 0x58
#define TRANS_NEW 0x60

void shim_screen_room_slide(unsigned long *regs) {
    if (!regs || !smooth_x1()) return;
    unsigned long b = shim_image_base();
    if (!b) return;
    unsigned long m = *(const unsigned long *) (b + GAME_MANAGER);
    if (!m || !*(const unsigned char *) (m + COLORMOD_ON)) return;
    unsigned long sp = (unsigned long) regs + TRACE_REGS_SP;
    float *o = (float *) (sp + TRANS_OLD), *n = (float *) (sp + TRANS_NEW);
    if (!isfinite(n[0]) || !isfinite(n[1]) || !isfinite(o[0]) || !isfinite(o[1])) return;
    if (whole_camera()) {                  /* WHOLE, NOT SPLIT: both on the new room's nearest unit */
        float rx = n[0] - roundf(n[0]), ry = n[1] - roundf(n[1]);
        o[0] -= rx; o[1] -= ry;
        n[0] -= rx; n[1] -= ry;
        g_lay_due = 0;
        g_split_trans++;
        return;
    }
    float fx = n[0] - floorf(n[0]), fy = n[1] - floorf(n[1]);
    o[0] -= fx; o[1] -= fy;
    n[0] -= fx; n[1] -= fy;
    /* drawn f units short of where they belong: the laying takes +f */
    g_lay[0] = fx; g_lay[1] = fy;
    g_lay_due = 1;
    g_split_trans++;
    split_note(fx, fy);
}

/* hook '}' where the Colormod Surface is laid on the scene: the camera back, the fraction into the quad */
void shim_screen_room_laid(unsigned long *regs) {
    if (g_cam_at) {
        g_cam_at[0] = g_cam_true[0]; g_cam_at[1] = g_cam_true[1];
        g_cam_at = NULL;
    }
    if (!g_lay_due) return;
    g_lay_due = 0;
    if (!regs) return;
    unsigned long sp = (unsigned long) regs + TRACE_REGS_SP;
    *(float *) (sp + 0x50) = g_lay[0];
    *(float *) (sp + 0x78) = g_lay[1];
    g_split_laid++;
}

/*
 * THE MENU, THE SAME WAY (the owner, 18.10.2026). The main menu draws its world
 * - every screen of it, side by side - into the Menu Colormod Surface (made at
 * 0x767b64, its pointer at 0xac2498; pushed at 0x7691a0, laid on the scene at
 * 0x7695d4 with the quad's origin at [sp] and [x29-0x64], zeroed at
 * 0x769624/0x769628), and its camera, menu+0x3c, slides between the screens. A
 * menu sprite stands at its place LESS the camera - the sprite that rides with
 * the camera is drawn at camera + offset (0x7692ec) - so on the whole camera
 * the world is drawn f units further along, and the laying takes -f.
 * Hook ']' at 0x7691c8, after the push, while the menu draws: the camera on its
 * whole unit; hook ')' at 0x76962c, after the origin is zeroed: the camera back,
 * the origin -f. The camera's own sliding (0x768bd8) is the menu's update, not
 * this draw, and never sees the whole value.
 */
#define MENU_PTR_AT      0xac2490
#define MENU_CAMERA_AT   0x3c
#define TRACE_REGS_X29   208           /* trace.c's saved x29, the hooked frame's own */

static float g_menu_true[2], g_menu_lay[2];
static float *g_menu_cam;

void shim_screen_menu_draw(unsigned long *regs) {
    (void) regs;
    if (!smooth_x1() || g_menu_cam) return;
    unsigned long b = shim_image_base();
    if (!b) return;
    unsigned long m = *(const unsigned long *) (b + GAME_MANAGER);
    if (!m || !*(const unsigned char *) (m + COLORMOD_ON)) return;
    unsigned long menu = *(const unsigned long *) (b + MENU_PTR_AT);
    if (!menu) return;
    float *c = (float *) (menu + MENU_CAMERA_AT);
    if (!isfinite(c[0]) || !isfinite(c[1])) return;
    g_menu_true[0] = c[0]; g_menu_true[1] = c[1];
    float wx = floorf(c[0]), wy = floorf(c[1]);
    g_menu_lay[0] = -(c[0] - wx); g_menu_lay[1] = -(c[1] - wy);
    if (whole_camera()) {                  /* WHOLE, NOT SPLIT: the nearest unit, laid as it is */
        wx = roundf(c[0]); wy = roundf(c[1]);
        g_menu_lay[0] = g_menu_lay[1] = 0.0f;
    }
    c[0] = wx; c[1] = wy;
    g_menu_cam = c;
    g_split_menu++;
    split_note(g_menu_lay[0], g_menu_lay[1]);
}

void shim_screen_menu_laid(unsigned long *regs) {
    if (!g_menu_cam) return;
    g_menu_cam[0] = g_menu_true[0]; g_menu_cam[1] = g_menu_true[1];
    g_menu_cam = NULL;
    if (!regs) return;
    unsigned long sp = (unsigned long) regs + TRACE_REGS_SP;
    if (g_menu_lay[0] == 0.0f && g_menu_lay[1] == 0.0f) return;   /* WHOLE: the game's own zero stands */
    unsigned long x29 = *(const unsigned long *) ((const char *) regs + TRACE_REGS_X29);
    *(float *) sp = g_menu_lay[0];
    if (x29) *(float *) (x29 - 0x64) = g_menu_lay[1];
    g_split_laid++;
}

/*
 * NO AUTO ANY MORE (the owner, 16.10.2026; the default was LOW until 18.10.2026, ORIGINAL
 * since - smooth_x1). The rule's
 * own multiplier - the screen's resolution and above - was the default for
 * whoever never opened Tweaks, and since the room itself is enlarged (1.75)
 * it is a real price on a weak card. A ceiling never set (0) is ORIGINAL; any
 * ceiling above one canvas is ENHANCED (25.10.2026, enhanced_for).
 */
static int shot_for(unsigned long h, unsigned long win_w, unsigned long win_h, float cw, float ch) {
    int n = shot_by_rule(h, cw, ch);
    g_shot_rule = n;
    /*
     * POTATO AND ORIGINAL ARE ONE CANVAS AGAIN (the owner, 17.10.2026): both as
     * iOS draws them - the canvas fitted to the window by the game's own rule,
     * one pixel of it to the unit - POTATO with its heavy effects off as well.
     * The x2/x3 POTATO of 14.10 (potato_shot) is gone with it.
     */
    (void) win_w; (void) win_h;
    g_shot_enhanced = enhanced_for(h, ch);
    if (smooth_x1()) return 1;          /* the scene at one canvas too (21.10.2026), menus and run alike (22.10) */
    if (n > g_shot_enhanced) n = g_shot_enhanced;   /* ENHANCED, whatever the multiplier kept (25.10.2026) */
    return n;
}

static int shot_by_rule(unsigned long h, float cw, float ch) {
    if (!(ch > 0.0f)) ch = UNITS_H;
    float per = (float) h / ch;                          /* pixels to the unit */
    int up = (int) per;
    if ((float) up < per) up++;                          /* the ceiling: never stretched */
    int down = (int) (per + 0.5f);                       /* the rounding: as it was */
    if (up < 1) up = 1;
    if (down < 1) down = 1;
    if (up > SHOT_MAX) up = SHOT_MAX;
    if (down > SHOT_MAX) down = SHOT_MAX;
    while (up > 1 && (cw * (float) up > SURF_MAX || ch * (float) up > SURF_MAX)) up--;
    if (up <= down) return down;
    /*
     * THE NEAREST MULTIPLIER, NOT THE CEILING - and this was got wrong once.
     *
     * The ceiling used to be taken whenever it was "free", free meaning the same
     * texture: on a 1440-tall screen both 2925 x 1350 and 3510 x 1620 round up
     * to 4096 x 2048, the same 33 megabytes, uploaded the same number of times.
     * That is true of MEMORY and says nothing about FILL. The card shades the
     * VIEWPORT, not the whole texture, and the viewport is the scene itself:
     *
     *     multiplier 6   3510 x 1620 = 5.69 Mpx   27 per cent ABOVE the screen
     *     multiplier 5   2925 x 1350 = 3.95 Mpx   12 per cent below it
     *     the screen     3120 x 1440 = 4.49 Mpx
     *
     * Forty-four per cent more pixels a frame, over a dozen full-screen
     * surfaces - render, colour correction, HQX4, shadow, light, shockwave,
     * water, the two room surfaces. Measured on the owner's phone with a room
     * full of projectiles: a hundred frames of 46 to 90 ms, and 80 per cent of
     * their samples inside __ioctl, which on an Adreno is the frame waiting for
     * the card (14.09.2026). Nothing was being loaded in those frames at all.
     *
     * AND IT MADE NO DIFFERENCE, which is the useful half of the measurement.
     * With the multiplier at five the hitches kept their depth exactly - the
     * median stayed at sixty milliseconds and __ioctl kept three quarters of
     * the samples - while the picture lost a little sharpness. Forty-four per
     * cent fewer pixels changing nothing says the card is NOT short of fill,
     * and the wait is somewhere else. The owner asked for the sharpness back.
     *
     * So the ceiling stands, and the rule for it is the one it always was: it
     * is taken only when the surface lands in the same power-of-two texture -
     * on a 1440-tall screen 2925 x 1350 and 3510 x 1620 both live in
     * 4096 x 2048, the same 33 megabytes and the same number of uploads. On a
     * 900-tall screen they would land in 2048 x 1024 and 2048 x 2048, twice
     * the memory, and there the rounding stands.
     */
    if ((unsigned long) pot(cw * up) * pot(ch * up) !=
        (unsigned long) pot(cw * down) * pot(ch * down)) return down;
    return up;
}

static int g_pixels;                /* px per unit when whole; 0 = the game's own fit */
static int g_scene_shot = 1;        /* how much larger than the canvas the scene is drawn */
static int g_want_shot = 1;         /* what the canvas rule just asked for */
static int g_shot_changed;          /* the multiplier moved: every surface has to be made again */
static int g_scene_w, g_scene_h;    /* the canvas, as the game's own viewport will name it */

/*
 * The surface constructor, at its door: a request for a canvas-sized surface
 * becomes a request for a larger one. x1 and x2 are the width and
 * the height, and the trace stub restores the register block after us, so
 * writing them here is what the constructor sees.
 *
 * Only exactly the canvas is matched. The shadow atlases, the minimap, the
 * fonts and every other size in the game go past untouched.
 */
/*
 * Who asks for the large surfaces, for the stall report. The floor load
 * uploads two hundred megapixels in one frame, and a size alone does not say
 * which of the game's surfaces those are: the caller's address does. x30 sits
 * at word 27 of the saved block (the wrapper's own frame comes first).
 */
#define BIRTH_MAX 24
#define BIRTH_BIG 65536             /* 256 x 256 and up; the fonts and icons stay out */
static struct { unsigned long from; int w, h, n; } g_birth[BIRTH_MAX];
static int g_births;

static void note_birth(unsigned long from, int w, int h) {
    if ((long) w * h < BIRTH_BIG) return;
    for (int i = 0; i < g_births; i++)
        if (g_birth[i].from == from && g_birth[i].w == w && g_birth[i].h == h) { g_birth[i].n++; return; }
    if (g_births >= BIRTH_MAX) return;
    g_birth[g_births].from = from; g_birth[g_births].w = w; g_birth[g_births].h = h;
    g_birth[g_births].n = 1;
    g_births++;
}

/* the births since the last asking, as "caller wxh xN, ..."; answers 0 when none */
int shim_screen_births(char *out, size_t room) {
    int n = 0, any = 0;
    if (!out || room < 2) return 0;
    out[0] = 0;
    for (int i = 0; i < g_births; i++) {
        if (!g_birth[i].n) continue;
        any = 1;
        if ((size_t) n < room - 1)
            n += snprintf(out + n, room - (size_t) n, "%s%lx %dx%d x%d", n ? ", " : "",
                          g_birth[i].from, g_birth[i].w, g_birth[i].h, g_birth[i].n);
        g_birth[i].n = 0;
    }
    return any;
}

/*
 * Which canvas-sized surfaces are worth enlarging: the ones the finished
 * picture passes through. The engine names every surface at its birth (x3),
 * and the report listed them: the scene is drawn into "Render Surface
 * (Manager)" and graded into "Color Correction Surface"; the other twenty at
 * canvas size - shadows, light and dust overlays, water, shockwave, heat wave,
 * dizzy, mirror, bloom, pixelation, the two room-slide surfaces - are effects
 * blended over it, and the engine clears and re-uploads them on every room.
 *
 * Measured with all of them enlarged: a floor load of 957 ms and a room change
 * of 815 ms, 24 uploads of 8 megapixels each (2400 x 1350 rounded up to
 * 4096 x 2048 by the engine), 850 MB of pages touched for the buffers behind
 * them. Twenty-two of those surfaces gain nothing from the size - they are
 * smooth overlays, filtered on the way in anyway - so they keep the canvas
 * size, and the two that carry the picture keep the enlargement.
 */
/*
 * WHICH SURFACES ARE DRAWN LARGE. Named, not guessed: the engine gives every
 * surface a name of its own as it makes it, and the report prints them all
 * (see below).
 *
 * "Menu Colormod Surface" is the menus' own, and it was the answer to a whole
 * afternoon: the enlargement was turned on for the menu, the surfaces really
 * were made afresh, and the picture still crawled - because the two named here
 * are the RUN's, and the menu draws into a third. It was born at the canvas's
 * own 585 x 270, blown up to 3120 across, 5.33 pixels to the unit, and the logo
 * shimmered exactly as it had (the owner, 13.09.2026).
 */
/*
 * "Colormod Surface" IS THE ROOM, and it was left at the canvas (14.10.2026).
 *
 * Game::Render 0x88e7c4..0x88e7f4: with EnableColorModifier on - the byte at
 * manager+0x55f26, set to 1 by the options' defaults (0x7eb380) and read from
 * options.ini at 0x7eb4e4 - the frame makes the Colormod Surface (0xac3be8)
 * the drawing target before the room is drawn (Room::Render 0x5c2630, or the
 * transition's 0x6a8dd0), and only at 0x88e964..0x88ea78 lays it over the
 * scene with the colormod shader. The predicates at 0x6a8b98/0x6a8c88 skip it
 * only inside one transition state. So in a run every rock, tile, door, wall
 * and entity is drawn into this surface, not into the Render Surface above.
 *
 * The game keeps its point-sampled sprites on the pixel grid of whatever it
 * draws into: ImageBase::Render 0x8b44a0..0x8b4528 rounds each corner of a
 * square quad as round(v * s) / s, s being the target's pixels to the unit
 * (0x8b54a8: the target's size over the canvas at 0xa9aff0/f4), taken at the
 * start of every batch (0x8b4ad4 -> 0x8b4bb4, image+0xa8). With this surface at
 * 585 x 270, s was 1: the room was rounded to WHOLE UNITS and moved by whole
 * units, and the enlarged scene only ever received a blown-up copy of it -
 * every texel edge at k x 5.333 screen pixels on a 1440-tall screen, the
 * camera in jumps of a unit. Enlarged, s becomes the multiplier: the same
 * rounding puts every corner on a whole pixel of the scene, the camera moves
 * in steps of 1/N unit, and the composite back is one to one.
 *
 * The surface has no other reader: its slot is touched by its creator
 * (0x88a0b0..0x88a150), the release (0x88a4d0, 0x89a004), the push and the
 * composite above - and our refit already remakes it (SURFACES below).
 */
static int smooth_x1(void);

/*
 * Whether the room surfaces were born under ORIGINAL/POTATO's rule (-1: not yet
 * born). The refit's "nothing moved" test compares it: before 21.10.2026 a
 * switch ORIGINAL <-> LOW on a screen where both gave the same multiplier left
 * the room at the size of the mode before, until a restart (the renderer audit).
 */
static int g_born_x1 = -1;

static int worth_enlarging(const char *name) {
    if (!name) return 0;
    if (!strcmp(name, "Colormod Surface")) g_born_x1 = smooth_x1();
    if (smooth_x1() && (!strcmp(name, "Colormod Surface") || !strcmp(name, "Menu Colormod Surface")))
        return 0;                       /* the room and the menu at one canvas (THE CAMERA SPLIT) */
    return !strcmp(name, "Render Surface (Manager)") ||
           !strcmp(name, "Color Correction Surface") ||
           !strcmp(name, "Colormod Surface") ||
           !strcmp(name, "Menu Colormod Surface");
}

/*
 * THE ENLARGED SCENE IS NOT SMOOTHED AT ITS BIRTH (13.10.2026). The LINEAR
 * filter meant for it (09.09) was tested in shim.c against an upload of
 * exactly canvas x shot, and the engine never makes one: the constructor
 * 0x8ae8d0 keeps the asked size at +0xc0/+0xc4 and rounds the TEXTURE up to a
 * power of two at +0xc8/+0xcc (the byte at 0xac5489 that would allow any size
 * has no writer in the image), and 0x8db4bc uploads the rounded pair after
 * setting NEAREST (0x8db53c/0x8db54c) - 3510 x 1620 goes up as 4096 x 2048. The
 * last stretch onto the window is now handled where it happens, in every mode
 * and at every multiplier: stretch.c and the sharp-bilinear shaders.
 */
void shim_screen_surface_born(unsigned long *regs) {
    if (!regs) return;
    if (shim_report_active()) {
        unsigned long b = shim_image_base(), from = regs[27];
        note_birth((b && from > b) ? from - b : from, (int) regs[1], (int) regs[2]);
    }
    /*
     * Births, by name and size. ONLY AFTER THE CANVAS IS KNOWN: the engine
     * makes some seventy surfaces at startup, before the rule has ever run, and
     * they ate the whole allowance the first time this was asked (13.09.2026).
     * What matters is what is made AFTERWARDS - that is what the remaking does
     * or fails to do.
     */
    if (shim_report_active() && g_scene_w) {
        static int said;
        if (said < 40) {
            said++;
            const char *name = (const char *) regs[3];
            char line[200];
            snprintf(line, sizeof(line), "%lu x %lu \"%.80s\" (canvas %d x %d, shot %d)%s",
                     regs[1], regs[2], name ? name : "?", g_scene_w, g_scene_h, g_scene_shot,
                     (g_scene_shot > 1 && (int) regs[1] == g_scene_w && (int) regs[2] == g_scene_h
                      && worth_enlarging(name)) ? " -> ENLARGED" : "");
            report_line("born", line);
        }
    }
    int colormod = regs[3] && !strcmp((const char *) regs[3], "Colormod Surface");
    int menu_colormod = regs[3] && !strcmp((const char *) regs[3], "Menu Colormod Surface");
    if (colormod) { g_colormod_w = regs[1]; g_colormod_h = regs[2]; }
    if (menu_colormod) { g_menu_colormod_w = regs[1]; g_menu_colormod_h = regs[2]; }
    if (!g_scene_w) return;
    if ((int) regs[1] != g_scene_w || (int) regs[2] != g_scene_h) return;
    if (g_scene_shot <= 1) return;
    if (!worth_enlarging((const char *) regs[3])) return;
    regs[1] = (unsigned long) (g_scene_w * g_scene_shot);
    regs[2] = (unsigned long) (g_scene_h * g_scene_shot);
    if (colormod) { g_colormod_w = regs[1]; g_colormod_h = regs[2]; }
    if (menu_colormod) { g_menu_colormod_w = regs[1]; g_menu_colormod_h = regs[2]; }
    DIAG(static int said; if (said < 6) { said++;
             LOG("screen: a %d x %d surface is born %lu x %lu",
                 g_scene_w, g_scene_h, regs[1], regs[2]); });
}

/* sticks.c asks between frames: does everything have to be made again */
int shim_screen_shot_changed(void) {
    /*
     * PROBE, report builds (the testers, 28.09.2026: "POTATO jumps between a
     * sharp and a coarse picture and looks more detailed than ORIGINAL"): the
     * multiplier the scene is drawn at, the ceiling asked for, and the effect
     * bytes as the game holds them, a line whenever any of it changes.
     */
    if (shim_report_active()) {
        static int lines, was_shot = -1, was_ceiling = -99, was_off = -1;
        static unsigned char was_bytes[POTATO_BYTES];
        unsigned long b = shim_image_base();
        unsigned long m = b ? *(const unsigned long *) (b + 0xac2428) : 0;
        unsigned char bytes[POTATO_BYTES];
        for (int i = 0; i < POTATO_BYTES; i++) bytes[i] = m ? ((const unsigned char *) m)[POTATO_OFF[i]] : 0xff;
        if (lines < 40 && (g_scene_shot != was_shot || g_shot_ceiling != was_ceiling
                           || g_potato_off != was_off || memcmp(bytes, was_bytes, sizeof(bytes)))) {
            lines++;
            was_shot = g_scene_shot; was_ceiling = g_shot_ceiling; was_off = g_potato_off;
            memcpy(was_bytes, bytes, sizeof(bytes));
            char line[200];
            snprintf(line, sizeof(line),
                     "scene shot x%d, ceiling %d, effects off %d, bytes light %u shock %u caust %u bloom %u water %u/%u",
                     g_scene_shot, g_shot_ceiling, g_potato_off,
                     m ? ((const unsigned char *) m)[0x55f1e] : 0xff,   /* lighting: never POTATO's since 26.10.2026 */
                     bytes[0], bytes[1], bytes[2], bytes[3], bytes[4]);
            report_line("screen", line);
        }
    }
    /*
     * POTATO's bytes wait for a refit that gets as far as the Manager: one
     * asked for while the image was still loading (the ceiling comes from
     * port-touch.ini that early) stopped short of it, so this asks again until
     * they are in.
     */
    int was = g_shot_changed || g_ceiling_moved || potato_due();
    g_shot_changed = 0;
    g_ceiling_moved = 0;
    return was;
}

/* shim.c asks, to know which viewport belongs to the scene */
/* stretch.c (LIGHTER): whether the scene lands on a viewport this size a whole number of pixels to a texel */
int shim_screen_stretch_whole(int vw, int vh) {
    float sw = g_units_w * (float) g_scene_shot, sh = g_units_h * (float) g_scene_shot;
    if (!(sw > 0.0f) || !(sh > 0.0f) || vw <= 0 || vh <= 0) return 0;
    float kx = (float) vw / sw, ky = (float) vh / sh;
    return kx >= 1.0f - 1e-4f && fabsf(kx - roundf(kx)) < 1e-4f && fabsf(ky - roundf(ky)) < 1e-4f
        && roundf(kx) == roundf(ky);
}

int shim_screen_scene_scale(int *cw, int *ch) {
    if (cw) *cw = g_scene_w;
    if (ch) *ch = g_scene_h;
    return g_scene_shot;
}

/*
 * TWO canvases, and which one is in force depends on where the player is.
 *
 * The whole-number rule below makes a steady picture and a SMALLER one: on a
 * 1440-tall screen it gives 5 pixels to the unit where the game's own fit
 * would give 5.333, so the field comes out 6.7 per cent smaller than in the
 * letterboxed 16:9 - measured, the character 109 pixels tall against 117. In
 * the menus that is exactly what the owner wanted: the sheet of paper sits
 * further away and nothing is cut off. In a RUN it is the wrong way round -
 * a fullscreen mode has no business shrinking the room.
 *
 * So the run gets the game's own scale, which is the largest there is: 270
 * units tall, and as many units wide as the window's shape asks for. Nothing
 * is scaled down and nothing is cut; the extra width is simply room the HUD
 * can spread into, which is the whole point of the wide canvas. The menus keep
 * the whole-number canvas.
 *
 *     in a run, 3120 x 1440   585 x 270 units, 5.333 px per unit
 *     in a menu               624 x 288 units, 5 px per unit
 *
 * The canvas is swapped where the state changes, through the same rebuild the
 * FULLSCREEN row uses (sticks.c). It costs what that rebuild costs - about a
 * tenth of a second - and it happens on entering and leaving a run, where a
 * pause of that size passes unnoticed.
 */
int shim_sticks_in_game(void);
int shim_sticks_run_canvas(void);   /* a run OR the transition between its floors */

/*
 * A CUTSCENE'S picture is the cutscene's own, and the canvas is not swapped
 * under it.
 *
 * The manager is in state 3 while one plays - the game's own window-resize
 * road keys on exactly that (0x885f48: in state 3, ask the cutscene for its
 * size, 0x756c3c, and size the window for it, 0x670888). Every scene of a
 * cutscene sizes the window the same way as it starts (0x756e78 -> 0x670888
 * -> 0x6708f8): the fit is run for the game's canvas, and then the scene's own
 * size - 427 x 240 for the intro and the endings' backgrounds - is written
 * over the logical pair, and shim_screen_letterbox fits the drawn area to it.
 *
 * Measured, on a tester's 2712 x 1220 phone and in the owner's screenshot:
 * the Mom cutscene came out in the top-left, 63 per cent of the width. That
 * is 427 / 678 - the scene's width over the MENU canvas's - and this is how
 * it got there. State 3 is not a run, so the canvas trigger in sticks.c saw
 * the run's picture give way to "not a run" and asked for the refit that
 * swaps the run's canvas for the menus'. The refit runs the game's resize
 * road, whose fit publishes the canvas pair over the logical one - over the
 * scene's 427 x 240, which the cutscene had written a moment before and does
 * not write again. The scene then drew its 427 units into a projection of
 * 678. The stage transition had the same fault a day earlier, for the same
 * reason (state 5), and it has the same cure: the canvas follows the picture,
 * and while the picture is a cutscene's there is nothing to follow.
 *
 * So two things hold while the state is 3. The trigger in sticks.c does not
 * compare states. And the fit's door below leaves the canvas object as it is:
 * the cutscene runs the fit itself at every scene and once more at its end
 * (0x756d74, the same resize road), and each of those would otherwise be a
 * chance to swap the pair to the shape the state asks for, without the
 * surfaces that go with it. Whatever picture comes after the cutscene is
 * compared against the pair the object still holds, and refitted then if it
 * differs: a run that carries on keeps its canvas and its surfaces untouched,
 * a return to the menu rebuilds for the menu.
 */
#define STATE_CUTSCENE 3

int shim_screen_cutscene(void) {
    unsigned long b = shim_image_base();
    if (!b) return 0;
    unsigned long m = *(const unsigned long *) (b + GAME_MANAGER);
    return m && *(const int *) (m + M_STATE) == STATE_CUTSCENE;
}

static void canvas_for(unsigned long w, unsigned long h, float *cw, float *ch, int *k) {
    int in_run = shim_sticks_run_canvas();
    {
        /*
         * The game's own fit: the design height, the window's own shape. k is
         * left at zero, which tells the letterbox pass to use the fit's own
         * rule rather than a whole multiple.
         */
        *cw = MIN_W;
        *ch = UNITS_H;
        *k = 0;                     /* the fit's own rule, not a whole multiple */
        if (w && h) {
            float ratio = (float) w / (float) h;
            if (ratio >= MIN_W / UNITS_H) {
                float units = UNITS_H * ratio;
                if (units > MAX_W) units = MAX_W;
                if (units < MIN_W) units = MIN_W;
                *cw = (float) (int) (units + 0.5f);
            } else {
                float units = MIN_W / ratio;
                if (units > MAX_H) units = MAX_H;
                if (units < UNITS_H) units = UNITS_H;
                *ch = (float) (int) (units + 0.5f);
            }
        }
        /* asked with the canvas it has just chosen: the ceiling is against it */
        g_want_shot = HIRES ? shot_for(h, w, h, *cw, *ch) : 1;
        if (in_run) return;
    }

    /*
     * ONE CANVAS FOR EVERYTHING. The menus used to have a rule of their own -
     * a whole number of pixels to the unit - and it gave them a canvas of their
     * own too: 624 x 288 where a run had 585 x 270. The touch controls editor
     * shows the sticks and the button row in the menu and the player uses them
     * in a run, so the two have to be the same size, and the owner asked for
     * the run's canvas everywhere (12.09.2026). The old rule is kept, in full,
     * in Z:/Isaac/backup/menu-whole-pixel-canvas.txt.
     *
     * AND THE SCENE IS DRAWN LARGE IN THE MENU TOO, which it was not before.
     *
     * The menu's old rule made a unit a WHOLE number of screen pixels, and that
     * alone kept its tiles still: nothing is resampled when the blow-up is a
     * whole multiple. Taking the rule away took that with it - 585 units across
     * 3120 pixels is 5.33 to the unit - and the logo and the plaques began to
     * crawl again. The owner asked why the downsampling did not save them, and
     * the answer was that there WAS none here: the enlargement was asked for
     * only in a run (13.09.2026).
     *
     * So it is asked for everywhere. The scene is drawn at six times the canvas
     * and sampled down by the linear filter (shim.c), which is exactly how the
     * same crawl is answered in a run. It costs nothing in memory over a run:
     * the surface rounds up to the same 4096 x 2048 either way, and the menu
     * has no room surfaces beside it.
     */
    if (!HIRES) g_want_shot = 1;
}

/*
 * At the fit's door, before it reads a thing. x0 is the canvas object, w1/w2
 * the window in pixels.
 */
void shim_screen_fit(unsigned long obj, unsigned long w, unsigned long h) {
    if (!obj) return;
    if (!g_wide) {
        /*
         * Off means the design canvas, and it has to be WRITTEN: the object may
         * be holding the wide pair we put there a moment ago, and the fit would
         * otherwise letterbox that instead of 480 x 270. This is what makes the
         * switch work without leaving the menu.
         */
        float *back = (float *) obj;
        back[0] = back[2] = MIN_W;
        back[1] = back[3] = UNITS_H;
        g_units_w = MIN_W;
        g_units_h = UNITS_H;
        g_pixels = 0;
        /*
         * THE SCENE IS DRAWN LARGE HERE TOO. This branch used to leave before
         * the canvas rule ran, so with the switch off there was no enlargement
         * at all: the design 480 x 270 went to the window whole, 5.33 pixels to
         * the unit, and it was visibly softer than the switch on - which is
         * what the owner asked about (13.09.2026). The picture here is
         * letterboxed, not smaller, and it needs the enlargement no less.
         *
         * The multiplier is asked for against the axis that BINDS: the canvas
         * is fitted by whichever of the two runs out first, and that decides
         * how many pixels a unit gets. Measured with the switch off: logical
         * 480 x 270, picture 2560 x 1440 at 280,0, scene x6 - the bars are
         * where they belong and the scene is drawn at 2880 x 1620.
         */
        {
            float per_w = (float) w / MIN_W, per_h = (float) h / UNITS_H;
            float per = per_w < per_h ? per_w : per_h;
            unsigned long fitted = (unsigned long) (UNITS_H * per + 0.5f);
            g_scene_w = (int) MIN_W;
            g_scene_h = (int) UNITS_H;
            g_want_shot = (HIRES && fitted)
                ? shot_for(fitted, (unsigned long) w, (unsigned long) h, MIN_W, UNITS_H) : 1;
            if (g_scene_shot != g_want_shot) g_shot_changed = 1;
            g_scene_shot = g_want_shot;
        }
        /*
         * The switch is off, so the game keeps the design canvas and the fit
         * puts black bars round it, exactly as before any of this work. The
         * menu grid still has to be put back by hand: the first guess at load
         * time widens it from the DISPLAY, and it runs before the settings are
         * read, so by now it is sized for a canvas the game is not using. The
         * grid must match the canvas the game actually lays out on, and here
         * that is the design one. A no-op after the first call.
         */
        { void shim_menu_grid(float, float); shim_menu_grid(MIN_W, UNITS_H); }
        return;
    }
    /* a cutscene's own fits, at its scenes and at its end: the canvas stays as it is */
    if (shim_screen_cutscene()) return;
    float cw, ch;
    int k;
    canvas_for(w, h, &cw, &ch, &k);
    float *o = (float *) obj;
    /*
     * Only the game's own canvas is touched. A cutscene hands the fit its own
     * size - the intro's is 427 by 240 - and that one is left exactly as it is.
     * It is recognised by not being either the design pair or the pair we wrote
     * last time; the height alone used to be the test, which stops working the
     * moment the height is ours to change.
     */
    /*
     * Ours, or the design's, or a cutscene's own - and the two pairs may now
     * legitimately differ, so the test looks at the layout pair and at what we
     * last put in the other one.
     */
    int design = (o[1] == UNITS_H && o[3] == UNITS_H);
    int ours = (o[1] == g_units_h && o[3] == g_units_h && g_units_h > 0.0f);
    if (!design && !ours) return;
    if (o[0] == cw && o[2] == cw && o[1] == ch && o[3] == ch &&
        g_scene_shot == g_want_shot) return;
    o[0] = cw;                       /* -> 0xac24d8 float, 0xa9aff0: the layout */
    o[1] = ch;                       /* -> 0xac24dc float, 0xa9aff4 */
    /*
     * A SURFACE IS MADE ONCE, so a change of the multiplier has to ask for it
     * to be made again. shim_screen_surface_born enlarges a surface AS IT IS
     * BORN, and the first ones are born before this rule has ever run: asking
     * the menu to draw large did nothing by itself, because the menu's surface
     * was already there at the canvas's own size and nobody remade it. Leaving
     * a run remade it, which is why the menu was steady after a game and
     * crawled before one (the owner, 13.09.2026).
     */
    if (g_scene_shot != g_want_shot) g_shot_changed = 1;
    g_scene_shot = g_want_shot;
    o[2] = cw;                       /* -> 0xac24e0 u32: left at the canvas */
    o[3] = ch;                       /* -> 0xac24e4 u32 */
    g_scene_w = (int) cw;
    g_scene_h = (int) ch;
    g_units_w = cw;
    g_units_h = ch;
    g_pixels = k;
    /*
     * The menu's tile grid is sized from the same numbers, and it has to be
     * right before the menu is built. Asked here rather than at load time: this
     * is the first moment the canvas is known, and the display's own metrics
     * are not the same thing as the frame the game was given.
     */
    { void shim_menu_grid(float, float); shim_menu_grid(cw, ch); }
    LOG("screen: the canvas is %d x %d units for a %lu x %lu window",
        (int) cw, (int) ch, w, h);
    /*
     * Into the tester's report as well, once per fit: on somebody else's
     * screen the whole number k is a different one, and a canvas that came out
     * with a fractional scale is what makes the tiles crawl. The bars say how
     * many pixels did not divide - up to k - 1 a side, and no more.
     */
    {
        int shim_report_active(void);
        void report_line(const char *, const char *);
        if (shim_report_active()) {
            char line[220];
            if (g_scene_shot > 1)
                snprintf(line, sizeof(line),
                         "run: layout %d x %d units, scene %d x %d px at %d per unit, "
                         "onto a %lu x %lu window (%.3f)",
                         (int) cw, (int) ch, (int) cw * g_scene_shot, (int) ch * g_scene_shot,
                         g_scene_shot, w, h, (double) w / ((double) cw * g_scene_shot));
            else if (k > 0)
                snprintf(line, sizeof(line),
                         "menu canvas %d x %d, %d px per unit (whole), window %lu x %lu, "
                         "bars %ld x %ld",
                         (int) cw, (int) ch, k, w, h,
                         (long) w - (long) cw * k, (long) h - (long) ch * k);
            else
                snprintf(line, sizeof(line),
                         "run canvas %d x %d, %.3f px per unit (the game's own), window %lu x %lu",
                         (int) cw, (int) ch, (double) h / (double) ch, w, h);
            report_line("scale", line);
        }
    }
}

/*
 * PROBE, diagnostic builds only: everything about the picture's geometry, in
 * one line, whenever any of it changes. A stage transition that lands in a
 * corner is either a canvas that moved under it or a viewport that did not
 * follow the surface, and these six numbers tell the two apart.
 */
void shim_screen_tell(void) {
    int shim_report_active(void);
    void report_line(const char *, const char *);
    static int said;
    if (said >= 40 || !shim_report_active()) return;
    unsigned long b = shim_image_base();
    if (!b) return;
    const float *logical = (const float *) (b + LOGICAL);
    const float *units = (const float *) (b + UNITS_F);
    const unsigned *area = (const unsigned *) (b + AREA);
    static float was[4];
    static unsigned was_area[4];
    static int was_shot = -1;
    if (logical[0] == was[0] && logical[1] == was[1]
        && units[0] == was[2] && units[1] == was[3]
        && area[0] == was_area[0] && area[1] == was_area[1]
        && area[2] == was_area[2] && area[3] == was_area[3]
        && g_scene_shot == was_shot) return;
    was[0] = logical[0]; was[1] = logical[1];
    was[2] = units[0]; was[3] = units[1];
    was_area[0] = area[0]; was_area[1] = area[1];
    was_area[2] = area[2]; was_area[3] = area[3];
    was_shot = g_scene_shot;
    said++;
    char line[220];
    snprintf(line, sizeof(line),
             "logical %.0fx%.0f, layout %.0fx%.0f, area %ux%u at %u,%u, scene x%d (%dx%d), "
             "in game %d",
             (double) logical[0], (double) logical[1], (double) units[0], (double) units[1],
             area[0], area[1], area[2], area[3], g_scene_shot, g_scene_w, g_scene_h,
             shim_sticks_in_game());
    report_line("canvas", line);
}

/* what the game is laying out against right now, for our own layer */
/*
 * What the game is laying out against RIGHT NOW - not what we asked for.
 *
 * The two are not the same, and the difference cost a wrong answer already: a
 * cutscene declares its own size (the intro is 427 x 240) and the engine
 * honours it, so the live width is legitimately below the design 480 while one
 * plays. A guard that floored anything under 480 back to 480 turned that into a
 * lie, and the sticks were seated against a canvas that did not exist.
 *
 * So the bounds here only reject nonsense - an uninitialised or absurd value -
 * and pass everything the engine can genuinely be using.
 */
#define SANE_MIN 64.0f
#define SANE_MAX 4000.0f

/*
 * ONE FINGER, TWO SPACES - one per viewport the game draws through.
 *
 * The frame's interface is drawn in two passes with two viewports (0x671788):
 * everything up to and including the overlay's widgets (0x861df8, called at
 * 0x671c08) goes into the fitted area 0xabec64 - the HUD, the menus, the
 * Buttons and ItemButtons of the touch overlay, and every picture of ours hung
 * on hook O. Then 0x8a7208 sets the viewport to the WHOLE WINDOW (0, 0,
 * 0xa9aff8/fc), the two sticks are drawn (0x861e78), and 0x8a71d4 puts the
 * area back. The canvas's units are spread over whichever viewport is set, so
 * with the picture letterboxed (FULLSCREEN off) a unit of the sticks is not a
 * unit of the rest.
 *
 * The game maps a finger into each of them its own way, read in the image
 * (01.10.2026; the tap audit had taken the first for the second):
 *
 *   PICTURE  the touch device (DeviceiOS, 0x8b382c) keeps every finger
 *            RELATIVE TO THE AREA - its place less area origin / scale
 *            (0x8b38bc..0x8b38e8, 0x8b39bc..0x8b39d4, stored at record+0x10) -
 *            and its view size (+0xb18, the vtable's +0xd0 answer) IS the area
 *            / scale (0x8b38ec..0x8b3928). So 0x86255c, units = record+0x10 *
 *            canvas / view, is (finger - area origin) * canvas / area: the
 *            picture. The Buttons' and ItemButtons' hit tests (0x863b1c,
 *            0x864034) reach it through their vtable +0x10 (0x863b14, 0x86402c:
 *            `mov x0, x1; b 0x86255c`), and so do the menus' own tests.
 *   WINDOW   a stick maps its fingers itself (0x861a20, the Stick vtable's
 *            +0x10): it ADDS the area origin back (0x8beb00 is an add) and
 *            divides by the window - plain proportion against the window, which
 *            is the viewport it is drawn in. That is why the game's sticks were
 *            right with FULLSCREEN off and ours (sticks.c) had to follow them.
 *
 * With FULLSCREEN on the area is the window and the two are one. Everything of
 * ours maps its fingers through the space its drawing is in, with these - the
 * pause and pocket long presses and the map's (reset.c, pocket.c, items.c)
 * through the picture, because the game's own Button and ItemButton judge them
 * there; Marked, the inventory list and the editor's row through the picture,
 * because they are drawn in the widget pass; the D-pads (sticks.c) and the
 * editor's sticks through the window.
 */
float shim_screen_width(void);
float shim_screen_height(void);

/* pixels -> units in the PICTURE: the game's own 0x86255c, to the instruction */
int shim_screen_finger_units(float x, float y, float *u, float *v) {
    unsigned long b = shim_image_base();
    if (!b || !u || !v) return 0;
    const unsigned *area = (const unsigned *) (b + AREA);
    if (!area[0] || !area[1]) return 0;
    *u = (x - (float) area[2]) * shim_screen_width() / (float) area[0];
    *v = (y - (float) area[3]) * shim_screen_height() / (float) area[1];
    return 1;
}

/* pixels -> units over the WINDOW: a stick's 0x861a20; the area before the first fit */
int shim_screen_finger_window(float x, float y, float *u, float *v) {
    unsigned long b = shim_image_base();
    if (!b || !u || !v) return 0;
    const unsigned *win = (const unsigned *) (b + WINDOW);
    if (!win[0] || !win[1]) return shim_screen_finger_units(x, y, u, v);
    *u = x * shim_screen_width() / (float) win[0];
    *v = y * shim_screen_height() / (float) win[1];
    return 1;
}

/*
 * A point of the window's units, in the picture's units - and back. In
 * fullscreen the area IS the window and both are the identity. Measured on the
 * owner's screenshots before it was read: switching FULLSCREEN off left both
 * sticks where they were on the screen while the whole HUD, the button row with
 * it, moved in with the picture.
 */
static float window_to_area(float at, unsigned win, unsigned size, unsigned origin, float units) {
    if (!win || !size || units <= 0.0f) return at;
    if (size == win && origin == 0) return at;
    float px = at * (float) win / units;
    return (px - (float) origin) * units / (float) size;
}

static float area_to_window(float at, unsigned win, unsigned size, unsigned origin, float units) {
    if (!win || !size || units <= 0.0f) return at;
    if (size == win && origin == 0) return at;
    float px = at * (float) size / units + (float) origin;
    return px * units / (float) win;
}

float shim_screen_overlay_to_area_x(float x) {
    unsigned long b = shim_image_base();
    if (!b) return x;
    const unsigned *win = (const unsigned *) (b + WINDOW);
    const unsigned *area = (const unsigned *) (b + AREA);
    return window_to_area(x, win[0], area[0], area[2], shim_screen_width());
}

float shim_screen_overlay_to_area_y(float y) {
    unsigned long b = shim_image_base();
    if (!b) return y;
    const unsigned *win = (const unsigned *) (b + WINDOW);
    const unsigned *area = (const unsigned *) (b + AREA);
    return window_to_area(y, win[1], area[1], area[3], shim_screen_height());
}

float shim_screen_area_to_overlay_x(float x) {
    unsigned long b = shim_image_base();
    if (!b) return x;
    const unsigned *win = (const unsigned *) (b + WINDOW);
    const unsigned *area = (const unsigned *) (b + AREA);
    return area_to_window(x, win[0], area[0], area[2], shim_screen_width());
}

float shim_screen_area_to_overlay_y(float y) {
    unsigned long b = shim_image_base();
    if (!b) return y;
    const unsigned *win = (const unsigned *) (b + WINDOW);
    const unsigned *area = (const unsigned *) (b + AREA);
    return area_to_window(y, win[1], area[1], area[3], shim_screen_height());
}

float shim_screen_width(void) {
    unsigned long b = shim_image_base();
    if (!b) return MIN_W;
    float w = *(const float *) (b + UNITS_F);
    return (w >= SANE_MIN && w <= SANE_MAX) ? w : MIN_W;
}

float shim_screen_height(void) {
    unsigned long b = shim_image_base();
    if (!b) return UNITS_H;
    float h = *(const float *) (b + UNITS_F + 4);
    return (h >= SANE_MIN && h <= SANE_MAX) ? h : UNITS_H;
}


/*
 * The cutscenes, fitted rather than stretched.
 *
 * The fit at 0x670a00 does two separate things: it publishes the LOGICAL size
 * the projection uses (0xa9aff0/f4) and it works out which part of the window is
 * drawn into (0xabec64: width, height, then the origin), by taking the SMALLER
 * of the two ratios so a unit stays square and centring what is left over.
 * Those two always agree - until a cutscene.
 *
 * A cutscene declares its own size and 0x6708f8 writes it over the logical pair
 * AFTER the fit has already sized the drawn area for the game's canvas. The
 * intro declares 427x240, the exact size of its background art, intro_bg.png.
 * On a 16:9 phone that is the window's own ratio and nothing shows; on anything
 * else the projection maps 427x240 onto the whole window and the picture is
 * stretched - 22 per cent too wide at 2.17:1, and squashed the other way at
 * 4:3.
 *
 * So the area is recomputed here by the fit's own rule, from whatever the
 * logical size currently is. When the two agree - every frame that is not a
 * cutscene - the numbers come out identical and nothing is written at all. The
 * cutscene gets its own frame, square units, and the whole picture; the room
 * left over goes to bars, which is the only honest answer when the background
 * is exactly the declared size and there is nothing to reveal beside it.
 *
 * Filling the frame instead was tried and rejected: it costs about nine per
 * cent of the height at 2.17:1 and far more at wider ratios, and the sheet of
 * paper the intro is drawn on very nearly fills the art already.
 */
void shim_screen_letterbox(void) {
    unsigned long b = shim_image_base();
    if (!b) return;
    const float *logical = (const float *) (b + LOGICAL);
    const unsigned *win = (const unsigned *) (b + WINDOW);
    unsigned *area = (unsigned *) (b + AREA);
    if (!win[0] || !win[1]) return;
    float lw = logical[0], lh = logical[1];
    if (!(lw >= SANE_MIN && lw <= SANE_MAX)) return;
    if (!(lh >= SANE_MIN && lh <= SANE_MAX)) return;

    unsigned w, h;
    if (g_pixels > 0 && lw == g_units_w && lh == g_units_h) {
        /*
         * Our own canvas, at a whole number of pixels to the unit: the area is
         * exactly units * k, and what the window has over that is the bars.
         * The fit's rule below would round the height up to the window and
         * leave a scale of 5.003 - one duplicated column somewhere.
         */
        w = (unsigned) lw * (unsigned) g_pixels;
        h = (unsigned) lh * (unsigned) g_pixels;
    } else {
        float s = (float) win[0] / lw;
        float sy = (float) win[1] / lh;
        if (sy < s) s = sy;
        w = (unsigned) (lw * s + 0.5f);
        h = (unsigned) (lh * s + 0.5f);
    }
    if (w > win[0]) w = win[0];
    if (h > win[1]) h = win[1];
    unsigned ox = (win[0] - w) / 2, oy = (win[1] - h) / 2;
    if (area[0] == w && area[1] == h && area[2] == ox && area[3] == oy) return;

    DIAG(LOG("frame: %ux%u at %u,%u for a logical %.0fx%.0f (was %ux%u at %u,%u)",
             w, h, ox, oy, (double) lw, (double) lh,
             area[0], area[1], area[2], area[3]));
    if (shim_report_active()) {
        static int said;
        if (said < 12) {
            said++;
            char line[190];
            snprintf(line, sizeof(line),
                     "picture %ux%u at %u,%u; window %ux%u; logical %.0fx%.0f;"
                     " ours %.0fx%.0f, whole %d, scene x%d",
                     w, h, ox, oy, win[0], win[1], (double) lw, (double) lh,
                     (double) g_units_w, (double) g_units_h, g_pixels, g_scene_shot);
            report_line("letterbox", line);
        }
    }
    area[0] = w; area[1] = h; area[2] = ox; area[3] = oy;
}

/* -------------------------------------------------- the menu's hit tables */

/*
 * Taps in the menus, after the canvas grew.
 *
 * Menu screens are DRAWN at camera + tilePos, and the camera carries the term
 * 0x767d6c returns - halfExtra = ((canvasW - 480)/2, (canvasH - 270)/2), which
 * is (52.5, 0) on this phone. Most hit tests carry the same term. Two do not:
 * the character select and the save select are checked against STATIC TABLES
 * read by 0x8633a8, and that road adds no position at all - the boxes are
 * absolute numbers from the 480 grid. So the item is drawn 52.5 units to the
 * right of the box that answers for it.
 *
 * And that also explains the second symptom, the boxes reading as too HIGH.
 * The rows are TILTED: each entry carries an angle (8 degrees for both these
 * tables) and 0x863450 rotates the finger's offset by it before comparing. A
 * horizontal error of 52.5 units therefore needs 52.5 * tan(8) = 7.4 units of
 * vertical error to cancel - so the finger has to sit 7.4 units above the row's
 * middle to be counted, and since the rows are 30 units apart and overlap, the
 * miss lands squarely on the neighbour.
 *
 * The tables are plain writable data and the game rebuilds them from constants,
 * so we do not patch anything: every frame the centres are written as
 * pristine + halfExtra. It is absolute, not incremental, so it is idempotent;
 * at 480 halfExtra is zero and the write is bit-for-bit what the game built.
 * Only the centres move. The half-extents and the angle stay - halfExtra is a
 * translation and belongs before the rotation, which is exactly where it lands.
 *
 * The pause paper's own table is here too, and it needs a DIFFERENT bias. It
 * is not a menu screen: it is drawn absolutely, at (0.6 * canvasW, 0.5 * H) -
 * 0x8a234c reads 0xac24d8 and multiplies it by the 0.6 at 0x946244, so the
 * paper legitimately follows the width and sits 63 units further right on this
 * phone. Its three boxes, built by 0x8a2d10 at 240,164 / 228,190 / 237,215,
 * are constants and do not follow. The gap is the paper's own drift and nothing
 * else, so the boxes get exactly that: 0.6 * (canvasW - 480). The offset
 * between the paper and its rows - which was never zero, the paper sits at 288
 * and the rows at 228..240 - comes out unchanged, which is the point.
 *
 * THE YES/NO PROMPT, 0xb14780. This said the table was "the game's own" and
 * needed nothing, and it was wrong: players could not answer YES to the Victory
 * Lap prompt (16.09.2026). The prompt is drawn at the CENTRE OF THE CANVAS -
 * 0x82872c reads 0xac24d8 and halves it - while its two boxes, YES at 175,155
 * and NO at 251,155, both 40 x 30 half, are constants for a 480-wide screen.
 * On a 585 canvas the drawing stands 52.5 to the right of its boxes, so the
 * YES a thumb sees sits inside the NO box. The drift is (W - 480) / 2 and
 * (H - 270) / 2, the same as halfExtra, so the menu bias is exactly right. The
 * same prompt, table and test serve the daily run's connection failure, the
 * save menu and the pause menu's quit prompt, so they are all mended by this.
 *
 * 0xb10c58's screen has still not been identified - it is only measured here.
 */
#define ENTRY 0x18                  /* {u32 id; float2 centre; float2 half; float angle} */
#define CENTRE 0x04

enum { BIAS_NONE, BIAS_MENU, BIAS_PAPER };

typedef struct {
    unsigned long at;
    int n;
    const char *what;
    int bias;                       /* how it moves, or whether we only watch */
} table_t;

static const table_t TABLES[] = {
    {0xb109f8, 3, "character select", BIAS_MENU},
    {0xb10928, 1, "save select",      BIAS_MENU},
    {0xb16408, 3, "pause paper",      BIAS_PAPER},
    {0xb14780, 2, "yes/no prompt",    BIAS_MENU},
    {0xb10c58, 9, "unidentified",     BIAS_NONE},
};
#define TABLES_N (int) (sizeof(TABLES) / sizeof(TABLES[0]))
#define ROWS_MAX 9

static float g_pristine[TABLES_N][ROWS_MAX][2];
static float g_wrote[TABLES_N][ROWS_MAX][2];
static unsigned char g_have[TABLES_N][ROWS_MAX];

#if ISAAC_DIAG
static void tables_note(unsigned long b);
static void zones_note(unsigned long b);
static void box_test_note(unsigned long *regs, float dx, float dy);
#endif

/* what 0x767d6c returns, to the instruction */
static void half_extra(float *dx, float *dy) {
    *dx = (shim_screen_width() - 480.0f) * 0.5f;
    *dy = (shim_screen_height() - 270.0f) * 0.5f;
}

/* how far the pause paper itself has moved: 0x8a234c, 0.6 * canvasW */
static void paper_drift(float *dx, float *dy) {
    *dx = (shim_screen_width() - 480.0f) * 0.6f;
    *dy = (shim_screen_height() - 270.0f) * 0.5f;   /* 0x8a236c: 0.5 * canvas height */
}

void shim_screen_menu_boxes(void) {
    unsigned long b = shim_image_base();
    if (!b) return;
    float dx, dy;
    half_extra(&dx, &dy);

    for (int t = 0; t < TABLES_N; t++) {
        float bx = dx, by = dy;
        if (TABLES[t].bias == BIAS_PAPER) paper_drift(&bx, &by);
        for (int i = 0; i < TABLES[t].n && i < ROWS_MAX; i++) {
            float *c = (float *) (b + TABLES[t].at + (unsigned long) i * ENTRY + CENTRE);
            /*
             * A value we did not write is the game's: that is how the pristine
             * numbers are learnt, and how a rebuild is noticed. Zero is not a
             * built table - the space is cleared long before the builder runs.
             */
            const float *was = TABLES[t].bias != BIAS_NONE ? g_wrote[t][i] : g_pristine[t][i];
            if (!g_have[t][i] || c[0] != was[0] || c[1] != was[1]) {
                if (c[0] == 0.0f && c[1] == 0.0f) continue;
                g_pristine[t][i][0] = c[0];
                g_pristine[t][i][1] = c[1];
                g_have[t][i] = 1;
                DIAG(LOG("menu boxes: %s row %d is the game's own at %.1f,%.1f",
                         TABLES[t].what, i, (double) c[0], (double) c[1]));
            }
            if (TABLES[t].bias == BIAS_NONE) continue;
            float wx = g_pristine[t][i][0] + bx, wy = g_pristine[t][i][1] + by;
            if (c[0] == wx && c[1] == wy) continue;
            c[0] = wx; c[1] = wy;
            g_wrote[t][i][0] = wx; g_wrote[t][i][1] = wy;
        }
    }
#if ISAAC_DIAG
    tables_note(b);
    zones_note(b);
#endif
}

/* ------------------------------------------- the menus' box test, per screen */

/*
 * Menu screens that hand the box test no position at all.
 *
 * Taps on a menu screen are answered by 0x862674 (reached directly, and through
 * the enabled-gate 0x862750, which tail-branches into it so the caller is
 * unchanged). It is given the finger, the box, and a BIAS in s4/s5 - the
 * screen's own position, negated. Screens that are laid out correctly compute
 * that bias as -(menuCamera + tilePos), which carries halfExtra; several
 * screens instead load the shared zero float2 at 0x95e7a0 and pass it, so their
 * boxes answer at the raw 480-grid coordinates while the items are drawn 52.5
 * units to the right.
 *
 * Neither the pool word nor the addressing is touched: the pool has readers all
 * over the image, and repointing three adrp pairs would need a page of our own
 * within reach of an adrp. Instead the ARGUMENT is corrected, in the register
 * block the trace stub saves and restores around us: q4 sits at byte 144 of it
 * and q5 at byte 160, and their low words are s4 and s5.
 *
 * The rule identifies itself: a bias of exactly zero means the screen never
 * passed its position - no menu screen legitimately sits at the canvas origin
 * once the canvas is wider than the design - so halfExtra is subtracted, which
 * is what the correct screens' own negation produces. At 480 halfExtra is zero
 * and the correction is nothing at all.
 */
#define Q4_IN_REGS 18               /* byte 144 of the saved block */
#define Q5_IN_REGS 20               /* byte 160 */

void shim_screen_box_test(unsigned long *regs) {
    if (!regs) return;
    float *s4 = (float *) &regs[Q4_IN_REGS];
    float *s5 = (float *) &regs[Q5_IN_REGS];
    float dx, dy;
    half_extra(&dx, &dy);

    if (*s4 != 0.0f || *s5 != 0.0f) return;      /* the screen gave its place */
    if (dx == 0.0f && dy == 0.0f) return;
    *s4 -= dx;
    *s5 -= dy;
#if ISAAC_DIAG
    box_test_note(regs, dx, dy);
#endif
}

/*
 * The main menu's taps, after the canvas grew.
 *
 * The main menu answers taps through a third road, not the two already known.
 * 0x862768 copies each fresh touch onto its own stack, turns it into canvas
 * units through 0x86255c, and compares it against the sprite's own touch areas
 * - the nulls Area1..Area5, AreaDailyRun and ChallengeButton of gamemenu.anm2.
 * Measured, they sit at 236,63 / 238,94 / 246,128 / 232,164 / 244,198 and two
 * more on the right: the raw 480-wide grid, exactly where the anm2 puts them.
 *
 * There is no origin in that road at all. It does not need one at the design
 * width: the main menu is screen zero, its camera is anchor + halfExtra, its
 * anchor is nothing, and halfExtra is nothing while the canvas is 480 wide. So
 * a null's own position IS its place on the screen, and the road was written
 * that way. Widen the canvas and the drawing moves by halfExtra with the
 * camera while the nulls stay - the items sit 52.5 units right of the boxes,
 * which is what the owner sees.
 *
 * The missing term is therefore the camera, and it equals halfExtra. It is put
 * back on the finger rather than on the nulls: the nulls are the sprite's own,
 * and the sprite draws its item labels from them, so moving them would move the
 * text with them. The finger, by then, is a COPY - 0x862768 stores each touch
 * on its stack at sp+0x10 before the conversion and the caller keeps reading
 * its own table afterwards - so a correction there reaches this road and no
 * other.
 *
 * 0x86255c is plain proportion, with no offset in it:
 *
 *     units = record[+0x10] * canvasUnits / viewPixels
 *
 * so halfExtra in units is subtracted as halfExtra * viewPixels / canvasUnits
 * in pixels. The view is the touch device's own (+0xb18), and that is the
 * drawn AREA, not the window: the record is already relative to the area's
 * origin (0x8b39bc..0x8b39d4) and the view is the area's size (0x8b38ec..
 * 0x8b3928; see "one finger, two spaces" above). It used to be taken as the
 * window, 0xa9aff8 - the same number whenever this road is taken at all, since
 * halfExtra is only there with FULLSCREEN on, where the area is the window to
 * a pixel; the area is the one the game divides by.
 */
#define MENU_ROAD 0x8627ec          /* the return address inside 0x862768 */
#define TOUCH_POINT 0x10            /* the touch record's position, in pixels */

#if ISAAC_DIAG
static void finger_probe(unsigned long b, unsigned long *regs);
#endif

void shim_screen_finger(unsigned long *regs) {
    if (!regs) return;
    unsigned long b = shim_image_base();
    if (!b) return;
#if ISAAC_DIAG
    finger_probe(b, regs);
#endif
    unsigned long from = regs[27];
    unsigned long off = (b && from > b) ? from - b : from;


    if (off != MENU_ROAD) return;

    float dx, dy;
    half_extra(&dx, &dy);
    if (dx == 0.0f && dy == 0.0f) return;

    const unsigned *area = (const unsigned *) (b + AREA);
    float uw = shim_screen_width(), uh = shim_screen_height();
    if (!area[0] || !area[1] || uw <= 0.0f || uh <= 0.0f) return;

    float *p = (float *) (regs[0] + TOUCH_POINT);
    float px = dx * (float) area[0] / uw;
    float py = dy * (float) area[1] / uh;
    p[0] -= px;
    p[1] -= py;
}

#if ISAAC_DIAG
/*
 * MEASUREMENT M7 and the finger's space (report builds only; the tap audit,
 * the research notes of 30.09.2026). At the door of 0x86255c, the game's
 * own finger-to-units, on the frame thread:
 *
 *   "finger space:" once per canvas - the window, the area, and the touch
 *   device's view size as the game itself asks for it (0x8b6c48, the call
 *   0x86255c makes next), with the record's place against the raw one: the
 *   reading above holds when the view IS the area and the record is the raw
 *   place less the area's origin.
 *
 *   "overlay finger:" while the picture is not the window (FULLSCREEN off), in
 *   a run, one line per finger going down, up to forty: the finger in the
 *   window's units, in the picture's, what the game makes of it, and which of
 *   the overlay's widgets each space puts it on. The game's widgets answer in
 *   the game's units; ours (reset.c, pocket.c, items.c, marked.c) in the
 *   picture's - the line shows the two agree.
 */
#define INPUT_SYSTEM   0xac5528     /* the object itself */
#define FN_VIEW_SIZE   0x8b6c48     /* (input, record id, preferred device) -> float2 */
#define M_TOUCH_PREF   0x1f6d18     /* the manager's device word 0x86255c passes */
#define REC_ID         0x20
#define REC_FLAGS      0x24         /* bit 0: the finger has just come down */
#define REC_RAW        0x34         /* the place as the view reported it */
#define OVL_WIDGETS    0x84f98      /* the overlay's widget vector (sticks.c) */
#define VT_BUTTON      0xa90c40
#define VT_ITEMBUTTON  0xa90c98

int shim_trace_readable(const void *p, unsigned long n);
unsigned long shim_pocket_overlay_at(void);
int shim_sticks_in_game(void);
const char *shim_str_bytes(const void *s);

typedef struct { float w, h; } view_t;

static const char *road_name(unsigned long off) {
    switch (off) {
    case 0x863b40: return "Button hit test";
    case 0x864058: return "ItemButton hit test";
    case 0x862150: return "widget touch";
    case 0x8627ec: return "main menu road";
    case 0x8626dc: return "menu box test";
    default: return NULL;
    }
}

/* which of the overlay's widgets a point in units is on, by the widget's own box */
static void widget_at(unsigned long b, float u, float v, char *out, size_t room) {
    snprintf(out, room, "none");
    unsigned long overlay = shim_pocket_overlay_at();
    if (!overlay || !shim_trace_readable((const void *) (overlay + OVL_WIDGETS), 16)) return;
    const unsigned long *list = (const unsigned long *) (overlay + OVL_WIDGETS);
    unsigned long first = list[0], past = list[1];
    if (!first || past < first || past - first > 64 * 8) return;
    for (unsigned long at = first; at < past; at += 8) {
        unsigned long w = *(const unsigned long *) at;
        if (!w || !shim_trace_readable((const void *) w, 0x1c8)) continue;
        unsigned long vt = *(const unsigned long *) w - b;
        const float *c, *sz;
        float moved[2];
        if (vt == VT_BUTTON) {
            if (!shim_trace_readable((const void *) (w + 0x2020c), 16)) continue;
            c = (const float *) (w + 0x2020c);
            sz = (const float *) (w + 0x20214);
            /* the pause button the player moved: the game's layout writes its own place
               every update and ours goes in only at the draw and the hit test (hud.c) */
            int shim_hud_pause_placed(float *x, float *y);
            if (w == overlay + 0x22ce0 && shim_hud_pause_placed(&moved[0], &moved[1])) c = moved;
        } else if (vt == VT_ITEMBUTTON) {
            if (!*(const unsigned char *) (w + 0x1b0)) continue;
            c = (const float *) (w + 0x1b4);
            sz = (const float *) (w + 0x1bc);
        } else {
            continue;
        }
        if (!*(const unsigned char *) (w + 0xc)) continue;
        if (!(sz[0] > 0.0f) || !(sz[1] > 0.0f)) continue;
        if (u < c[0] - sz[0] * 0.5f || u > c[0] + sz[0] * 0.5f) continue;
        if (v < c[1] - sz[1] * 0.5f || v > c[1] + sz[1] * 0.5f) continue;
        const char *name = NULL;
        if (vt == VT_BUTTON) {
            const void *const *anim = (const void *const *) (w + 0x10 + 0x38);
            if (shim_trace_readable(anim, 8) && *anim && shim_trace_readable(*anim, 24))
                name = shim_str_bytes(*anim);
        }
        if (name) snprintf(out, room, "\"%s\" at %.1f,%.1f", name, (double) c[0], (double) c[1]);
        else snprintf(out, room, "%s +0x%lx at %.1f,%.1f", vt == VT_BUTTON ? "Button" : "ItemButton",
                      w - overlay, (double) c[0], (double) c[1]);
        return;
    }
}

static void finger_probe(unsigned long b, unsigned long *regs) {
    if (!shim_report_active()) return;
    unsigned long rec = regs[0];
    if (!rec || !shim_trace_readable((const void *) rec, 0x48)) return;
    const unsigned *win = (const unsigned *) (b + WINDOW);
    const unsigned *area = (const unsigned *) (b + AREA);
    if (!win[0] || !win[1] || !area[0] || !area[1]) return;
    float W = shim_screen_width(), H = shim_screen_height();
    unsigned long m = *(const unsigned long *) (b + GAME_MANAGER);
    if (!m) return;
    const float *rel = (const float *) (rec + TOUCH_POINT);
    const float *raw = (const float *) (rec + REC_RAW);
    int letterboxed = !(area[0] == win[0] && area[1] == win[1] && area[2] == 0 && area[3] == 0);

    static float told_w, told_h;
    static int spaces_said;
    static int fingers_said;
    static unsigned long last_rec;
    static float last_raw[2];
    int space_due = (W != told_w || H != told_h) && spaces_said < 12;
    int finger_due = letterboxed && fingers_said < 40 && (*(const unsigned char *) (rec + REC_FLAGS) & 1)
                     && shim_sticks_in_game()
                     && !(rec == last_rec && raw[0] == last_raw[0] && raw[1] == last_raw[1]);
    if (!space_due && !finger_due) return;

    view_t view = ((view_t (*)(unsigned long, unsigned, unsigned)) (b + FN_VIEW_SIZE))(
        b + INPUT_SYSTEM, *(const unsigned *) (rec + REC_ID), *(const unsigned *) (m + M_TOUCH_PREF));
    if (!(view.w > 0.0f) || !(view.h > 0.0f)) return;
    float gu = rel[0] * W / view.w, gv = rel[1] * H / view.h;       /* 0x86255c itself */
    float pu, pv, wu, wv;
    shim_screen_finger_units(raw[0], raw[1], &pu, &pv);
    shim_screen_finger_window(raw[0], raw[1], &wu, &wv);
    char line[400];

    if (space_due) {
        told_w = W; told_h = H;
        spaces_said++;
        int view_is_area = fabsf(view.w - (float) area[0]) < 1.0f && fabsf(view.h - (float) area[1]) < 1.0f;
        int rel_ok = fabsf(rel[0] - (raw[0] - (float) area[2])) < 0.5f
                  && fabsf(rel[1] - (raw[1] - (float) area[3])) < 0.5f;
        snprintf(line, sizeof(line),
                 "finger space: canvas %.0fx%.0f, window %ux%u, area %ux%u@%u,%u; the touch device's view "
                 "%.1fx%.1f (%s); record %.1f,%.1f for raw %.1f,%.1f (%s); game units %.2f,%.2f, "
                 "picture %.2f,%.2f, window %.2f,%.2f: the game maps through the %s",
                 (double) W, (double) H, win[0], win[1], area[0], area[1], area[2], area[3],
                 (double) view.w, (double) view.h, view_is_area ? "= area" : "MISMATCH: not the area",
                 (double) rel[0], (double) rel[1], (double) raw[0], (double) raw[1],
                 rel_ok ? "raw less the area origin" : "MISMATCH: not raw less the area origin",
                 (double) gu, (double) gv, (double) pu, (double) pv, (double) wu, (double) wv,
                 fabsf(gu - pu) < 0.05f && fabsf(gv - pv) < 0.05f ? "picture"
                 : (fabsf(gu - wu) < 0.05f && fabsf(gv - wv) < 0.05f ? "WINDOW" : "NEITHER"));
        report_line("screen", line);
    }
    if (finger_due) {
        fingers_said++;
        last_rec = rec;
        last_raw[0] = raw[0]; last_raw[1] = raw[1];
        unsigned long from = regs[27];
        unsigned long off = from > b ? from - b : from;
        const char *road = road_name(off);
        char by_game[96], by_window[96], road_buf[24];
        widget_at(b, gu, gv, by_game, sizeof(by_game));
        widget_at(b, wu, wv, by_window, sizeof(by_window));
        if (!road) { snprintf(road_buf, sizeof(road_buf), "0x%lx", off); road = road_buf; }
        snprintf(line, sizeof(line),
                 "overlay finger: raw %.0f,%.0f px; window %.1f,%.1f; picture %.1f,%.1f; game %.1f,%.1f "
                 "(%s; asked by %s); widget by the game's units: %s; by the window's: %s",
                 (double) raw[0], (double) raw[1], (double) wu, (double) wv, (double) pu, (double) pv,
                 (double) gu, (double) gv,
                 fabsf(gu - pu) < 0.05f && fabsf(gv - pv) < 0.05f ? "game = picture" : "GAME != PICTURE",
                 road, by_game, by_window);
        report_line("screen", line);
    }
}
#endif

/*
 * PROBE, report builds only: the easter-egg list (menu screen 6, object
 * menu+0x103728, update 0x5d3810). The owner has to tap to the right of a
 * row's words for it to answer (18.09.2026), and this list's hit test is not
 * one of the tables above. Every finger going down and coming up on that
 * screen writes where it was, in canvas units, beside what the list thinks:
 * its scroll (+0x30, +0x34), the chosen row (+0x3c) and the first rows' own
 * places (+0x1d8, 0x28 a row, the place at +4) - enough to read the offset
 * off one report instead of guessing it.
 */
#define SEEDS_SCREEN   6
#define SEEDS_OBJECT   0x103728
#define MENU_OBJECT    0xac2490

#if ISAAC_DIAG
static void tap_probe(int phase, float x, float y);
#endif

void shim_screen_seeds_probe(int id, int phase, float x, float y) {
    int shim_report_active(void);
    void report_line(const char *, const char *);
#if ISAAC_DIAG
    tap_probe(phase, x, y);         /* M4: every finger going down, before anybody takes it */
#endif
    static int said;
    if ((phase != 0 && phase < 3) || said >= 40 || !shim_report_active()) return;
    unsigned long b = shim_image_base();
    unsigned long menu = b ? *(const unsigned long *) (b + MENU_OBJECT) : 0;
    if (!menu || *(const int *) (menu + 0x38) != SEEDS_SCREEN) return;
    float gx, gy;
    if (!shim_screen_finger_units(x, y, &gx, &gy)) return;
    unsigned long o = menu + SEEDS_OBJECT;
    const float *scroll = (const float *) (o + 0x30);
    int chosen = *(const int *) (o + 0x3c);
    unsigned long rows = *(const unsigned long *) (o + 0x1d8);
    char line[240];
    int n = snprintf(line, sizeof(line), "%s finger %d at %.1f,%.1f; scroll %.1f,%.1f; chosen %d;",
                     phase == 0 ? "down" : "up", id, (double) gx, (double) gy,
                     (double) scroll[0], (double) scroll[1], chosen);
    for (int r = 0; rows && r < 4 && n > 0 && n < (int) sizeof(line); r++) {
        const float *at = (const float *) (rows + (unsigned long) r * 0x28 + 4);
        n += snprintf(line + n, sizeof(line) - (size_t) n, " row%d %.1f,%.1f", r,
                      (double) at[0], (double) at[1]);
    }
    const float *camera = (const float *) (menu + 0x3c);
    if (n > 0 && n < (int) sizeof(line))
        snprintf(line + n, sizeof(line) - (size_t) n, "; camera %.1f,%.1f", (double) camera[0],
                 (double) camera[1]);
    said++;
    report_line("seeds", line);
}

#if ISAAC_DIAG
/* ------------------------------------------ the tap audit's measurements */

/*
 * MEASUREMENTS M1, M2, M4, M5, M6 (report builds only; the research notes of
 * 30.09.2026, tap audit). Nothing here changes what a finger does: each line puts
 * a zone as a finger is judged against it beside where the thing is drawn, on
 * whatever canvas the phone gives, so a promise "it matches on every format"
 * is read off a report rather than modelled on a desk.
 */
int shim_tweaks_strap_rect(int guide, float box[4]);
int shim_tweaks_tap_verdict(float gx, float gy, char *out, size_t room);
int shim_tweaks_open(void);
int shim_credits_plaque_rect(float box[4]);
int shim_credits_open(void);
int shim_spawner_strap_rect(float box[4], float origin[2]);
int shim_spawner_is_open(void);

#define Z_MENU_SCREEN   0x38
#define Z_MENU_CAMERA   0x3c
#define Z_MENU_ANCHORS  0x10fc08     /* a Vector2 per screen */
#define Z_SETTINGS_LIST 0x10eed8
#define Z_LIST_TILE     0x1c         /* the list is drawn at camera + this (0x6e3b48) */
#define Z_LIST_SPRITE   0x30
#define Z_TITLE_PLACE   0x81228      /* the title is drawn at camera + this (0x7f9de0) */
#define Z_TITLE_SPRITE  0x81230
#define Z_SPRITE_SCALE  0xdc
#define Z_GAME_PTR      0xac3b90
#define Z_PAUSE_STATE   (0x10dd20 + 0x2b8)
#define Z_PAUSE_PAPER   0xb16408     /* the paper's three rows, as screen.c keeps them */
#define Z_SCREEN_TITLE  3
#define Z_SCREEN_SETTINGS 10

static unsigned long menu_now(unsigned long b) {
    return b ? *(const unsigned long *) (b + MENU_OBJECT) : 0;
}

static const float *anchor_at(unsigned long menu, int screen) {
    return (const float *) (menu + Z_MENU_ANCHORS + (unsigned long) screen * 8);
}

/* the menu grid's steps as the game holds them: screen 8 stands 4 rows up, screen 4 a column left */
static void grid_now(unsigned long menu, float *rows, float *cols) {
    *rows = -anchor_at(menu, 8)[1] / 4.0f;
    *cols = -anchor_at(menu, 4)[0];
}

/*
 * M2: where the settings list's sprite and the title's sprite are really drawn
 * - the place Sprite::Render (0x89e4c0) is handed, on the hook the report build
 * already keeps there ('%', trace.c). The first four of each after a canvas
 * change go into the report beside the vector the port's zones start from.
 */
static float g_render_at[2][2];
static float g_render_w[2], g_render_h[2];      /* the canvas they were seen on */

void shim_screen_sprite_render(unsigned long sprite, unsigned long pos) {
    unsigned long b = shim_image_base();
    unsigned long menu = menu_now(b);
    if (!menu || !sprite || !pos) return;
    int which = sprite == menu + Z_SETTINGS_LIST + Z_LIST_SPRITE ? 0
              : (sprite == menu + Z_TITLE_SPRITE ? 1 : -1);
    if (which < 0 || !shim_trace_readable((const void *) pos, 8)) return;
    const float *p = (const float *) pos;
    float W = shim_screen_width(), H = shim_screen_height();
    static int said[2];
    static float said_w[2], said_h[2];
    g_render_at[which][0] = p[0];
    g_render_at[which][1] = p[1];
    g_render_w[which] = W;
    g_render_h[which] = H;
    if (said_w[which] != W || said_h[which] != H) { said_w[which] = W; said_h[which] = H; said[which] = 0; }
    if (said[which] >= 4 || !shim_report_active()) return;
    said[which]++;
    const float *camera = (const float *) (menu + Z_MENU_CAMERA);
    const float *tile = (const float *) (menu + (which ? Z_TITLE_PLACE : Z_SETTINGS_LIST + Z_LIST_TILE));
    const float *scale = (const float *) (sprite + Z_SPRITE_SCALE);
    float ox = camera[0] + tile[0], oy = camera[1] + tile[1];
    float dx = p[0] - ox, dy = p[1] - oy;
    char line[200];
    snprintf(line, sizeof(line),
             "render: %s sprite at %.1f,%.1f; camera+tile %.1f,%.1f; scale %.2f,%.2f; canvas %.0fx%.0f; %s %.1f,%.1f",
             which ? "title" : "settings", (double) p[0], (double) p[1], (double) ox, (double) oy,
             (double) scale[0], (double) scale[1], (double) W, (double) H,
             fabsf(dx) <= 1.0f && fabsf(dy) <= 1.0f ? "OK" : "MISMATCH", (double) dx, (double) dy);
    report_line("zones", line);
}

/* one zone into a line: the tap box, the origin it hangs from, and where that origin is drawn */
static int zone_piece(char *out, size_t room, const char *name, const float box[4],
                      float tap_x, float tap_y, float drawn_x, float drawn_y, const char *drawn_by) {
    float dx = drawn_x - tap_x, dy = drawn_y - tap_y;
    return snprintf(out, room,
                    " | %s tap %.1f..%.1f x %.1f..%.1f from %.1f,%.1f, drawn from %.1f,%.1f (%s): %s %.1f,%.1f",
                    name, (double) box[0], (double) box[2], (double) box[1], (double) box[3],
                    (double) tap_x, (double) tap_y, (double) drawn_x, (double) drawn_y, drawn_by,
                    fabsf(dx) <= 1.0f && fabsf(dy) <= 1.0f ? "OK" : "MISMATCH", (double) dx, (double) dy);
}

/*
 * M1: on screens 3 and 10, once the camera has settled on the screen (it goes
 * to anchor + halfExtra, 0x768bb4), once per canvas and per entering: the
 * canvas, the grid's steps, the screen's tile against its anchor (G1 of the
 * audit: the tile is minus the unscrolled anchor), the sprite's scale (G2),
 * and every strap of the screen - the box a finger is judged against beside
 * where the drawing's vector puts it (M2's render place, or the settled model
 * halfExtra + tile + anchor before the render has been seen on this canvas).
 */
static void zones_note(unsigned long b) {
    static int lines;
    static float key_w, key_h;
    static int said_mask, was_screen = -1;
    if (lines >= 24 || !shim_report_active() || shim_sticks_in_game()) return;
    unsigned long menu = menu_now(b);
    if (!menu) return;
    int screen = *(const int *) (menu + Z_MENU_SCREEN);
    float W = shim_screen_width(), H = shim_screen_height();
    if (W != key_w || H != key_h) { key_w = W; key_h = H; said_mask = 0; }
    if (screen != was_screen) {                         /* entering a screen asks again */
        if (was_screen == Z_SCREEN_TITLE) said_mask &= ~1;
        if (was_screen == Z_SCREEN_SETTINGS) said_mask &= ~2;
        was_screen = screen;
    }
    int bit = screen == Z_SCREEN_TITLE ? 1 : (screen == Z_SCREEN_SETTINGS ? 2 : 0);
    if (!bit || (said_mask & bit) || shim_tweaks_open() || shim_credits_open()) return;
    float hx, hy;
    half_extra(&hx, &hy);
    const float *camera = (const float *) (menu + Z_MENU_CAMERA);
    const float *anchor = anchor_at(menu, screen);
    if (fabsf(camera[0] - (anchor[0] + hx)) >= 1.0f || fabsf(camera[1] - (anchor[1] + hy)) >= 1.0f) return;
    said_mask |= bit;
    lines++;

    int which = screen == Z_SCREEN_TITLE;
    const float *tile = (const float *) (menu + (which ? Z_TITLE_PLACE : Z_SETTINGS_LIST + Z_LIST_TILE));
    const float *scale = (const float *) (menu + (which ? Z_TITLE_SPRITE : Z_SETTINGS_LIST + Z_LIST_SPRITE)
                                          + Z_SPRITE_SCALE);
    const unsigned *area = (const unsigned *) (b + AREA);
    float rows, cols;
    grid_now(menu, &rows, &cols);
    float tap_x = camera[0] + tile[0], tap_y = camera[1] + tile[1];
    int rendered = g_render_w[which] == W && g_render_h[which] == H;
    float drawn_x = rendered ? g_render_at[which][0] : hx + tile[0] + anchor[0];
    float drawn_y = rendered ? g_render_at[which][1] : hy + tile[1] + anchor[1];
    const char *by = rendered ? "render" : "settled model";

    char line[900];
    int n = snprintf(line, sizeof(line),
                     "zones: screen %d canvas %.0fx%.0f area %ux%u@%u,%u rows %.0f cols %.0f halfExtra %.1f,%.1f"
                     " | tile %.1f,%.1f anchor %.1f,%.1f (tile+anchor %.1f,%.1f) camera %.1f,%.1f scale %.2f,%.2f",
                     screen, (double) W, (double) H, area[0], area[1], area[2], area[3],
                     (double) rows, (double) cols, (double) hx, (double) hy,
                     (double) tile[0], (double) tile[1], (double) anchor[0], (double) anchor[1],
                     (double) (tile[0] + anchor[0]), (double) (tile[1] + anchor[1]),
                     (double) camera[0], (double) camera[1], (double) scale[0], (double) scale[1]);
    float box[4];
    if (which) {
        if (n > 0 && n < (int) sizeof(line) && shim_credits_plaque_rect(box))
            n += zone_piece(line + n, sizeof(line) - (size_t) n, "CREDITS", box, tap_x, tap_y,
                            drawn_x, drawn_y, by);
    } else {
        if (n > 0 && n < (int) sizeof(line) && shim_tweaks_strap_rect(0, box))
            n += zone_piece(line + n, sizeof(line) - (size_t) n, "TWEAKS", box, tap_x, tap_y,
                            drawn_x, drawn_y, by);
        if (n > 0 && n < (int) sizeof(line) && shim_tweaks_strap_rect(1, box))
            n += zone_piece(line + n, sizeof(line) - (size_t) n, "GUIDE", box, tap_x, tap_y,
                            drawn_x, drawn_y, by);
    }
    report_line("zones", line);
}

/*
 * M6: every table screen.c keeps, once per canvas, as soon as its first row is
 * known: the game's own centres, what stands there now, and the bias.
 */
static void tables_note(unsigned long b) {
    static float said_w[TABLES_N], said_h[TABLES_N];
    static int lines;
    if (lines >= 30 || !shim_report_active()) return;
    float W = shim_screen_width(), H = shim_screen_height();
    for (int t = 0; t < TABLES_N; t++) {
        if (!g_have[t][0] || (said_w[t] == W && said_h[t] == H)) continue;
        said_w[t] = W;
        said_h[t] = H;
        lines++;
        float bx, by;
        half_extra(&bx, &by);
        if (TABLES[t].bias == BIAS_PAPER) paper_drift(&bx, &by);
        if (TABLES[t].bias == BIAS_NONE) bx = by = 0.0f;
        char line[400];
        int n = snprintf(line, sizeof(line), "table %s: canvas %.0fx%.0f, bias %s %.1f,%.1f; pristine",
                         TABLES[t].what, (double) W, (double) H,
                         TABLES[t].bias == BIAS_MENU ? "menu" : (TABLES[t].bias == BIAS_PAPER ? "paper" : "none"),
                         (double) bx, (double) by);
        for (int i = 0; i < TABLES[t].n && i < ROWS_MAX && n > 0 && n < (int) sizeof(line); i++)
            n += snprintf(line + n, sizeof(line) - (size_t) n, " %.1f,%.1f",
                          (double) g_pristine[t][i][0], (double) g_pristine[t][i][1]);
        if (n > 0 && n < (int) sizeof(line))
            n += snprintf(line + n, sizeof(line) - (size_t) n, "; written");
        for (int i = 0; i < TABLES[t].n && i < ROWS_MAX && n > 0 && n < (int) sizeof(line); i++) {
            const float *c = (const float *) (b + TABLES[t].at + (unsigned long) i * ENTRY + CENTRE);
            n += snprintf(line + n, sizeof(line) - (size_t) n, " %.1f,%.1f", (double) c[0], (double) c[1]);
        }
        report_line("zones", line);
    }
}

/*
 * M5: the zero-bias rule of the box test at work (G3) - which screens hand it
 * no place, and from where, once per screen and caller on each canvas.
 */
static void box_test_note(unsigned long *regs, float dx, float dy) {
    static struct { int screen; unsigned long from; float w, h; } seen[16];
    static int n_seen, lines;
    if (lines >= 24 || !shim_report_active()) return;
    unsigned long b = shim_image_base();
    unsigned long menu = menu_now(b);
    int screen = menu ? *(const int *) (menu + Z_MENU_SCREEN) : -1;
    unsigned long from = regs[27] > b ? regs[27] - b : regs[27];
    float W = shim_screen_width(), H = shim_screen_height();
    for (int i = 0; i < n_seen; i++)
        if (seen[i].screen == screen && seen[i].from == from && seen[i].w == W && seen[i].h == H) return;
    if (n_seen < 16) {
        seen[n_seen].screen = screen; seen[n_seen].from = from;
        seen[n_seen].w = W; seen[n_seen].h = H;
        n_seen++;
    }
    lines++;
    char line[180];
    snprintf(line, sizeof(line),
             "box test: screen %d passed zero (asked from 0x%lx%s), moved by %.1f,%.1f on %.0fx%.0f",
             screen, from, shim_sticks_in_game() ? ", in a run" : "", (double) -dx, (double) -dy,
             (double) W, (double) H);
    report_line("zones", line);
}

/* how far a point is outside a box, per axis: nought inside */
static float outside(float at, float lo, float hi) {
    return at < lo ? at - lo : (at > hi ? at - hi : 0.0f);
}

/*
 * M4: every finger going down on the title (3), the settings (10), the pause,
 * and on the port's own pages, up to sixty: the finger in the picture's units -
 * the space all of these are drawn in and judged in - and the zone it lands
 * on, or the nearest one and by how much it was missed.
 */
static void tap_probe(int phase, float x, float y) {
    static int said;
    if (phase != 0 || said >= 60 || !shim_report_active()) return;
    unsigned long b = shim_image_base();
    if (!b) return;
    float u, v;
    if (!shim_screen_finger_units(x, y, &u, &v)) return;
    int in_run = shim_sticks_in_game();
    char line[240], verdict[96];

    if (!in_run && shim_tweaks_tap_verdict(u, v, verdict, sizeof(verdict))) {
        said++;
        snprintf(line, sizeof(line), "tap: Tweaks page finger %.1f,%.1f -> %s", (double) u, (double) v, verdict);
        report_line("zones", line);
        return;
    }
    if (!in_run && shim_credits_open()) {
        said++;
        snprintf(line, sizeof(line), "tap: credits page finger %.1f,%.1f -> closes it", (double) u, (double) v);
        report_line("zones", line);
        return;
    }
    if (in_run && shim_spawner_is_open()) {
        said++;
        snprintf(line, sizeof(line), "tap: spawner card finger %.1f,%.1f", (double) u, (double) v);
        report_line("zones", line);
        return;
    }

    struct { char name[16]; float box[4]; } zone[6];
    int n = 0;
    int screen = -1;
    if (in_run) {
        unsigned long game = *(const unsigned long *) (b + Z_GAME_PTR);
        if (!game || *(const int *) (game + Z_PAUSE_STATE) != 1) return;
        if (shim_spawner_strap_rect(zone[n].box, NULL)) snprintf(zone[n++].name, 16, "ITEM SPAWN");
        for (int i = 0; i < 3; i++) {           /* the paper's own rows, as the game judges them */
            const float *c = (const float *) (b + Z_PAUSE_PAPER + (unsigned long) i * ENTRY + CENTRE);
            if (c[0] == 0.0f && c[1] == 0.0f) continue;
            zone[n].box[0] = c[0] - c[2]; zone[n].box[1] = c[1] - c[3];
            zone[n].box[2] = c[0] + c[2]; zone[n].box[3] = c[1] + c[3];
            snprintf(zone[n++].name, 16, "pause row %d", i);
        }
    } else {
        unsigned long menu = menu_now(b);
        if (!menu) return;
        screen = *(const int *) (menu + Z_MENU_SCREEN);
        if (screen == Z_SCREEN_TITLE && shim_credits_plaque_rect(zone[n].box))
            snprintf(zone[n++].name, 16, "CREDITS");
        if (screen == Z_SCREEN_SETTINGS) {
            if (shim_tweaks_strap_rect(0, zone[n].box)) snprintf(zone[n++].name, 16, "TWEAKS");
            if (shim_tweaks_strap_rect(1, zone[n].box)) snprintf(zone[n++].name, 16, "GUIDE");
        }
    }
    if (!n) return;
    said++;
    char where[16];
    if (in_run) snprintf(where, sizeof(where), "pause");
    else snprintf(where, sizeof(where), "screen %d", screen);
    int best = 0;
    float best_d = 1e9f, best_dx = 0.0f, best_dy = 0.0f;
    for (int i = 0; i < n; i++) {
        float dx = outside(u, zone[i].box[0], zone[i].box[2]);
        float dy = outside(v, zone[i].box[1], zone[i].box[3]);
        if (dx == 0.0f && dy == 0.0f) {
            snprintf(line, sizeof(line), "tap: %s finger %.1f,%.1f -> %s", where, (double) u, (double) v,
                     zone[i].name);
            report_line("zones", line);
            return;
        }
        float d = hypotf(dx, dy);
        if (d < best_d) { best_d = d; best = i; best_dx = dx; best_dy = dy; }
    }
    snprintf(line, sizeof(line), "miss: %s finger %.1f,%.1f; nearest %s by %+.1f,%+.1f", where,
             (double) u, (double) v, zone[best].name, (double) best_dx, (double) best_dy);
    report_line("zones", line);
}
#endif
