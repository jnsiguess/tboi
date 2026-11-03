/*
 * THE PORT'S CREDITS: the plaque on the main menu, and the page it opens.
 *
 * The plaque is the title strap: layers PortStrap0..5 the port's patch lays into
 * gamemenu.anm2 (portpatch.c, plan 30.09 M8; it used to be painted into the
 * sheet). The title sprite draws it in its own grid, one unit to one pixel, at
 * the place the patch gives it. That gives a picture and nothing else - no
 * widget, no hit area - so the finger is answered here; and when the strap is
 * not drawn (a mod's gamemenu.anm2 the patch could not take, straps.c), the
 * finger is not answered either.
 *
 * WHERE IT OPENS. The same road the touch controls editor rides, and for the
 * same reason it was chosen (touchedit.c): the main menu is one world, every
 * screen has its anchor in the menu object at menu+0x10fc08, and the camera
 * slides to the anchor of the screen it is on. Write a far-away anchor for the
 * screen we are on and the game's own camera carries us to empty space, at its
 * own speed. The page is then drawn in the screen's own coordinates, over the
 * emptiness, and on the way out the anchor is put back and the camera comes
 * home by itself. Nothing is added to the menu and nothing is taken from it.
 *
 * THE ANCHORS ARE NOT SHARED between this and the editor: that one moves the
 * Controls screen's, this one the main menu's own, and the two are never open
 * at the same time - the editor is reached from a screen the credits cannot be
 * reached from.
 *
 * WHILE IT IS OPEN every finger belongs to it, so nothing behind is touched.
 */
#include <math.h>
#include <pthread.h>
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
void shim_asset_bring_in(void *mgr, const char *name, const char *folder, const char *leaf);
float shim_screen_width(void);
float shim_screen_height(void);
void *shim_str_assign(void *self, const char *s);
void shim_str_dtor(void *self);
int shim_sticks_in_game(void);

/*
 * THE PAGE TURN, HEARD AS ON EVERY OTHER SCREEN.
 *
 * The port's own two screens - this page and the touch editor - move the menu's
 * camera themselves, so the menu's own change of screen never runs and they
 * arrived in silence.
 *
 * Which sound to play was GUESSED FIRST AND MEASURED SECOND, and the guess was
 * wrong. sounds.xml has ids 17 and 18 as V2/paper_in.wav and paper_out.wav, the
 * menu plays them at 0x8a1dd4 and 0x8a1f58, and they sounded like a sheet of
 * paper - but the owner said at once it was not what the rest of the menu makes
 * (14.09.2026). So the player itself was watched (trace.c, mark '@'), and six
 * transitions through the native menu gave one answer every time:
 *
 *     sfx: sound 8, asked for at 769bd8    x6
 *
 * Sound 8 is Feedback/book page turn.wav, and 0x769bc8 plays it - the same
 * sound in both directions, there being only the one call site. This is that
 * call to the register:
 *
 *     x0 = [0xac2428]   the game manager
 *     w1 = 8            the page turn
 *     w2 = 0, w3 = 0
 *     s0 = 1.0          the volume, and s1 = 1.0 the pitch
 *
 * Nothing here is chosen: every number is the menu's own, so our screens sound
 * exactly like its screens, and the player's SFX setting is applied downstream
 * by the code that applies it to everything else.
 */
#define FN_PLAY_SOUND   0x671c8c    /* (manager, id, w2, w3, volume, pitch) */
#define GAME_MANAGER    0xac2428
#define SOUND_PAGE_TURN 8

void shim_menu_page_sound(void) {
    unsigned long b = shim_image_base();
    if (!b) return;
    unsigned long manager = *(const unsigned long *) (b + GAME_MANAGER);
    if (!manager) return;
    ((void (*)(unsigned long, int, int, int, float, float)) (b + FN_PLAY_SOUND))(
        manager, SOUND_PAGE_TURN, 0, 0, 1.0f, 1.0f);
}

#define MENU_PTR        0xac2490    /* the main menu object, a pointer in the image */
#define MENU_SCREEN     0x38        /* the screen it is on (0x768dcc picks by it) */
#define MENU_ANCHORS    0x10fc08    /* a Vector2 per screen, read at 0x768ba4 */
#define MENU_CAMERA     0x3c        /* where the camera stands (0x768bd8) */
#define SCREEN_GAME     3           /* the main menu's own screen */

/*
 * WHICH WAY IT GOES, and HOW THE PAGE RIDES WITH IT. Both measured, both wrong
 * the first two times.
 *
 * The report writes the whole anchor table at startup ("credits map"). There
 * are seventeen screens; past that the numbers are somebody else's:
 *
 *      0,1 (0,0)      2 (0,-270)      3 (0,-540) US    4 (-540,-540)
 *      5 (0,-810)     6 (-1080,-810)  7 (540,-216)     8 (0,-1080)
 *      9 (540,-1350) 10 (-540,-1080) 11 (-540,-1620)  12 (-540,-1890)
 *     13 (-540,-270) 14 (540,-1670)  15 (1070,-216)   16 (1440,-290)
 *
 * A SCREEN IS PAINTED AT (middle + camera - its anchor), which is the opposite
 * of what was assumed here at first. The proof is a picture the owner took: the
 * camera stood at x 800 and the CHALLENGES sheet showed on the RIGHT - that is
 * screen 7 at x 540, and 800 - 540 is +260, to the right. Two things follow.
 *
 *   Where we may stop. A screen shows while it is within (canvasW + 480) / 2
 *   across - 532.5 on this canvas - and, for the tall sheets, some 405 down.
 *   The road LEFT (the first try, -2000) goes over screen 4, the daily run we
 *   hid; the road RIGHT to 800 STOPS on screen 7. The owner saw both. Sweeping
 *   every ending within reach against the table, the emptiest is (3200,-1200):
 *   the nearest screen is 1228 past the edge of showing, and no ending nearer
 *   than that is as clear. The camera is a lerp of a tenth a frame with a floor
 *   of one, so the distance costs almost nothing in time: 800 takes some 52
 *   frames and 3200 some 65.
 *
 *   Which way the page comes in. It has to ride the same way as everything
 *   else, so its offset is (camera - where it stands) - the same sense as a
 *   screen's. Drawn the other way round it slid AGAINST the menu, and the owner
 *   said so twice.
 */
#define AWAY_X          3200.0f
#define AWAY_Y          (-660.0f)
/*
 * ONE TO ONE. The page is not scaled: it was GROWN by repeating its middle
 * (tools/credits_paper.py), the way a nine-slice grows, so its torn edges are
 * exactly as they were drawn and the grain does not smear. Scaling it to twice
 * the size was the wrong way round and looked it.
 */
#define PAPER_SCALE     1.0f

/*
 * THE PLAQUE, in the DESIGN grid of the sheet - and the sheet is not where the
 * finger is.
 *
 * Menu screens are drawn at the camera, and the camera carries halfExtra:
 * ((canvasW - 480) / 2, (canvasH - 270) / 2), which is (52.5, 0) on a canvas
 * 585 wide. So the plaque painted at x 335 of the sheet is at 387.5 under the
 * thumb, and a box taken straight from the sheet is missed by half a hundred
 * units - which is exactly what happened (13.09.2026). The same term is what
 * screen.c adds to the menus' own hit tables, for the same reason.
 *
 * Measured by the paper's own tone (236,209,196) in the finished sheet: it
 * covered x 335..395 and y 178..225 when first drawn; the owner then moved the
 * plaque up (14.09.2026) and it covers x 331..388 and y 143..188, the words
 * inside it at x 333..384, y 150..181. The box is the paper three units wider
 * on every side, so a thumb need not be exact. The words moved, the box did
 * not, and the plaque answered nowhere near where it was drawn - which is why
 * the box is now written down as paper-plus-three rather than as numbers.
 */
#define PLAQUE_X0       328.0f
#define PLAQUE_X1       391.0f
#define PLAQUE_Y0       140.0f
#define PLAQUE_Y1       191.0f
/*
 * A language whose word lengthened the strap (strap_sizes.h, tools/strap_lang)
 * has it reach further right and rise higher at the top: the box grows by as
 * much as the picture did over the owner's.
 */
#include "strap_sizes.h"
int shim_tweaks_set_index(void);
static float plaque_x1(void) {
    int set = shim_tweaks_set_index();
    return PLAQUE_X1 + (float) (STRAP_SIZE[set][STRAP_CREDITS].w - STRAP_SIZE[0][STRAP_CREDITS].w);
}
static float plaque_y0(void) {
    return PLAQUE_Y0 - (float) STRAP_SIZE[shim_tweaks_set_index()][STRAP_CREDITS].rise;
}

/*
 * WHERE THE TITLE IS DRAWN, from the same numbers the game draws it with: the
 * menu's camera plus the title object's own place, [menu+0x81228] (0x7f9dd4..
 * 0x7f9dec: camera [menu+0x3c] + [title+8], title = menu+0x81220). The plaque's
 * numbers are in that sprite's own grid, so a tap is judged against them from
 * this origin - on every canvas, and while the camera is still moving. The old
 * origin, halfExtra, equals it only once the camera has settled, and missed it
 * by half a unit on canvases an odd number of units wide (the tap audit,
 * 01.10.2026: the research notes of 30.09.2026).
 */
#define TITLE_PLACE     0x81228
static void title_origin(unsigned long menu, float *ox, float *oy) {
    const float *camera = (const float *) (menu + MENU_CAMERA);
    const float *place = (const float *) (menu + TITLE_PLACE);
    *ox = camera[0] + place[0];
    *oy = camera[1] + place[1];
}

#define AREA            0xabec64    /* the fitted picture: width, height, x, y in pixels */

#define FN_SPRITE_NEW      0x89c16c
#define FN_SPRITE_LOAD     0x89fe24
#define FN_SPRITE_FRAME    0x89eab8
#define FN_SPRITE_RENDER   0x89e4c0
#define ZERO_VECTOR        0x95e7a0
#define SPRITE_BYTES       0x158
#define SPRITE_COLOUR      0xe8
#define SPRITE_SCALE       0xdc
#define SPRITE_LOADED      0x149

/*
 * THE PAGE IN EVERY LANGUAGE (the owner, 30.09.2026; tools/credits_lang.py):
 * English is port_credits.anm2 as it always was, every other set its own
 * port_credits_<suffix>.anm2 beside it. The page is picked by the language
 * the game runs in, and each is loaded once, the first time it is shown.
 */
#define PAPER_ANM2      "gfx/port_credits.anm2"
#define PAPER_SETS      7            /* tweaks_layout.h's TW_SET */
static const char *const PAPER_SUFFIX[PAPER_SETS] = { "", "ru", "de", "es", "jp", "kr", "zh" };
#define PAPER_ANIM      "Paper"
#define PAPER_W         440.0f      /* the picture's own size, as grown */
#define PAPER_H         250.0f

static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;
static int g_open;
static int g_ours = -1;             /* the finger that arrived after it opened */
static unsigned long g_menu;        /* the menu whose anchor we moved */
static float g_anchor_was[2];
static float g_target[2];           /* where the camera is going, in the menu's world */

static unsigned char g_papers[PAPER_SETS][SPRITE_BYTES] __attribute__((aligned(16)));
static signed char g_paper_states[PAPER_SETS];
static unsigned char *g_paper;       /* the page of the language shown now */

/* the term the camera carries, and every menu screen with it */
/* whole units, as on the Tweaks pages (tweaks.c, centred): a 100 % picture half
   a unit off samples between texels at ORIGINAL and POTATO */
static float half_x(void) { return floorf((shim_screen_width() - 480.0f) * 0.5f); }
static float half_y(void) { return floorf((shim_screen_height() - 270.0f) * 0.5f); }

static float *anchor_of(unsigned long menu, int screen) {
    return (float *) (menu + MENU_ANCHORS + (unsigned long) screen * 8);
}

/* under the lock */
static void open_here(unsigned long menu) {
    float *a = anchor_of(menu, SCREEN_GAME);
    g_anchor_was[0] = a[0];
    g_anchor_was[1] = a[1];
    a[0] += AWAY_X;
    a[1] += AWAY_Y;
    /*
     * WHERE THE PAGE STANDS IN THE WORLD. It is not pinned to the screen: the
     * owner asked for it to be THERE already, so that the camera arrives at it
     * rather than it appearing once the camera has stopped. The menu's camera
     * settles at the screen's anchor plus halfExtra (0x768bd8 takes the
     * distance between the two and steps while it is over one), so that is
     * where the page is put, and every frame it is drawn as far from the middle
     * of the screen as the camera still is from it.
     */
    g_target[0] = a[0] + half_x();
    g_target[1] = a[1] + half_y();
    g_menu = menu;
    g_open = 1;
    shim_menu_page_sound();                   /* the page turns, as on the menu's own screens */
    LOG("credits: opened, the camera goes to %.0f,%.0f", (double) a[0], (double) a[1]);
    if (shim_report_active()) {
        char line[140];
        snprintf(line, sizeof(line), "opened: the menu anchor %.0f,%.0f becomes %.0f,%.0f",
                 (double) g_anchor_was[0], (double) g_anchor_was[1], (double) a[0], (double) a[1]);
        report_line("credits", line);
    }
}

/* under the lock */
static void close_here(void) {
    if (!g_open) return;
    if (g_menu) {
        float *a = anchor_of(g_menu, SCREEN_GAME);
        a[0] = g_anchor_was[0];
        a[1] = g_anchor_was[1];
    }
    g_open = 0;
    g_menu = 0;
    g_ours = -1;
    shim_menu_page_sound();
    LOG("credits: closed");
    if (shim_report_active()) report_line("credits", "closed: the anchor is back");
}

int shim_credits_open(void) {
    pthread_mutex_lock(&g_lock);
    int open = g_open;
    pthread_mutex_unlock(&g_lock);
    return open;
}

/* a touch, from screen pixels into the units the menu is laid out in */
static int to_field(float x, float y, float *gx, float *gy) {
    unsigned long b = shim_image_base();
    if (!b) return 0;
    const unsigned *area = (const unsigned *) (b + AREA);
    if (!area[0] || !area[1]) return 0;
    *gx = (x - (float) area[2]) * shim_screen_width() / (float) area[0];
    *gy = (y - (float) area[3]) * shim_screen_height() / (float) area[1];
    return 1;
}

/*
 * A touch, before the queue (uikit.c).
 *
 * Closed, it only watches for a tap on the plaque, and only while the main menu
 * is the screen being looked at - the menu draws every one of its screens each
 * frame, so a plaque in view is not the same as a plaque that can be pressed.
 * Open, every finger is its own, and the one that ARRIVED after the opening
 * closes it: the finger that pressed the plaque is still down at that moment,
 * and its lift would otherwise take us straight back out.
 */
int shim_credits_touch(int id, int phase, float x, float y) {
    int mine = 0;
    pthread_mutex_lock(&g_lock);
    if (g_open) {
        mine = 1;
        if (phase == 0) g_ours = id;
        else if (id == g_ours && phase == 3) close_here();
        else if (id == g_ours && phase == 4) g_ours = -1;     /* cancelled (a palm): no close (U7) */
    } else if (phase == 0 && !shim_sticks_in_game()) {
        unsigned long b = shim_image_base();
        unsigned long menu = b ? *(const unsigned long *) (b + MENU_PTR) : 0;
        float gx = 0.0f, gy = 0.0f, ox = 0.0f, oy = 0.0f;
        if (menu) title_origin(menu, &ox, &oy);
        if (menu && shim_report_active() && to_field(x, y, &gx, &gy)) {
            static int said;
            if (said < 12) {
                said++;
                char line[180];
                snprintf(line, sizeof(line),
                         "a finger at %.1f,%.1f; the plaque is %.1f..%.1f by %.1f..%.1f;"
                         " screen %d", (double) gx, (double) gy,
                         (double) (PLAQUE_X0 + ox), (double) (plaque_x1() + ox),
                         (double) (plaque_y0() + oy), (double) (PLAQUE_Y1 + oy),
                         *(const int *) (menu + MENU_SCREEN));
                report_line("credits", line);
            }
        }
        /*
         * Not while the MODS page is up: it is drawn on this very screen, and
         * the arrows of its middle rows stand where the plaque is - a tap on
         * them opened the credits under the page and took the camera away
         * (the owner, 24.09.2026).
         */
        int shim_tweaks_open(void);
        int shim_straps_title_drawn(void);
        if (menu && *(const int *) (menu + MENU_SCREEN) == SCREEN_GAME && !shim_tweaks_open()
            && shim_straps_title_drawn()
            && to_field(x, y, &gx, &gy)
            && gx >= PLAQUE_X0 + ox && gx <= plaque_x1() + ox
            && gy >= plaque_y0() + oy && gy <= PLAQUE_Y1 + oy) {
            open_here(menu);
            mine = 1;
        }
    }
    pthread_mutex_unlock(&g_lock);
    return mine;
}

#if ISAAC_DIAG
/* the tap audit's lines (screen.c, M1 and M4): the plaque's box, as a finger above is judged */
int shim_credits_plaque_rect(float box[4]) {
    unsigned long b = shim_image_base();
    unsigned long menu = b ? *(const unsigned long *) (b + MENU_PTR) : 0;
    if (!menu) return 0;
    float ox, oy;
    title_origin(menu, &ox, &oy);
    box[0] = PLAQUE_X0 + ox;
    box[1] = plaque_y0() + oy;
    box[2] = plaque_x1() + ox;
    box[3] = PLAQUE_Y1 + oy;
    return 1;
}
#endif

/* from a pad (straps.c): open the page as the plaque's tap does, or close it */
void shim_credits_show(int on) {
    pthread_mutex_lock(&g_lock);
    unsigned long b = shim_image_base();
    unsigned long menu = b ? *(const unsigned long *) (b + MENU_PTR) : 0;
    int shim_tweaks_open(void);
    if (on && !g_open && menu && *(const int *) (menu + MENU_SCREEN) == SCREEN_GAME && !shim_tweaks_open()) {
        open_here(menu);
        g_ours = -1;
    } else if (!on && g_open) {
        close_here();
    }
    pthread_mutex_unlock(&g_lock);
}

/* once between frames (gl.c): the page belongs to the main menu and nothing else */
void shim_credits_between_frames(void) {
    pthread_mutex_lock(&g_lock);
    if (shim_report_active()) {                 /* where every screen of the menu stands */
        static int mapped;
        unsigned long b = shim_image_base();
        unsigned long menu = b ? *(const unsigned long *) (b + MENU_PTR) : 0;
        if (!mapped && menu) {
            mapped = 1;
            for (int s = 0; s < 17; s++) {   /* seventeen, counted */
                const float *p = anchor_of(menu, s);
                char line[80];
                snprintf(line, sizeof(line), "screen %2d stands at %.0f,%.0f",
                         s, (double) p[0], (double) p[1]);
                report_line("credits map", line);
            }
        }
    }
    if (g_open) {
        unsigned long b = shim_image_base();
        unsigned long menu = b ? *(const unsigned long *) (b + MENU_PTR) : 0;
        if (!menu || menu != g_menu || *(const int *) (menu + MENU_SCREEN) != SCREEN_GAME)
            close_here();
    }
    pthread_mutex_unlock(&g_lock);
}

/* ------------------------------------------------------------- the picture */

static int paper_ready(void) {
    int set = shim_tweaks_set_index();
    if (set < 0 || set >= PAPER_SETS) set = 0;
    g_paper = g_papers[set];
    signed char *state = &g_paper_states[set];
    if (*state) return *state > 0;
    unsigned long b = shim_image_base();
    if (!b) return 0;
    *state = -1;                                /* one attempt, whatever happens */
    char anm2[48];
    if (set == 0) snprintf(anm2, sizeof(anm2), "%s", PAPER_ANM2);
    else snprintf(anm2, sizeof(anm2), "gfx/port_credits_%s.anm2", PAPER_SUFFIX[set]);
    memset(g_paper, 0, SPRITE_BYTES);
    ((void (*)(void *)) (b + FN_SPRITE_NEW))(g_paper);
    long long raw[3];
    unsigned char *path = (unsigned char *) raw;
    path[23] = 0;
    shim_str_assign(path, anm2);
    ((void (*)(void *, const void *, int)) (b + FN_SPRITE_LOAD))(g_paper, path, 1);
    shim_str_dtor(path);
    if (!g_paper[SPRITE_LOADED]) {
        if (shim_report_active()) {
            char line[96];
            snprintf(line, sizeof(line), "%s did NOT load", anm2);
            report_line("credits", line);
        }
        return 0;
    }
    *state = 1;
    return 1;
}

/* the overlay's draw (hook O), every frame while the page is up */
void shim_credits_draw(void) {
    pthread_mutex_lock(&g_lock);
    int open = g_open;
    unsigned long menu = g_menu;
    pthread_mutex_unlock(&g_lock);
    if (!open || !menu) return;
    unsigned long b = shim_image_base();
    if (!b || !paper_ready()) return;

    float w = shim_screen_width(), h = shim_screen_height();
    if (!(w > 0.0f) || !(h > 0.0f)) return;

    /* as far from the middle as the camera still is from where it is going */
    const float *camera = (const float *) (menu + MENU_CAMERA);
    float off_x = camera[0] - g_target[0];
    float off_y = camera[1] - g_target[1];

    ((void (*)(void *, const char *, int)) (b + FN_SPRITE_FRAME))(g_paper, PAPER_ANIM, 0);
    float *colour = (float *) (g_paper + SPRITE_COLOUR);
    colour[0] = colour[1] = colour[2] = 1.0f;
    colour[3] = 1.0f;
    float *scale = (float *) (g_paper + SPRITE_SCALE);
    scale[0] = scale[1] = PAPER_SCALE;
    /* on whole units: a 100 % page half a unit off samples between texels (tweaks.c, centred) */
    float pos[2] = { floorf(w * 0.5f) + off_x, floorf(h * 0.5f) + off_y };
    ((void (*)(void *, const float *, const void *, const void *)) (b + FN_SPRITE_RENDER))(
        g_paper, pos, (const void *) (b + ZERO_VECTOR), (const void *) (b + ZERO_VECTOR));

    static int told;
    if (!told && shim_report_active()) {
        told = 1;
        char line[160];
        snprintf(line, sizeof(line),
                 "drawn: canvas %.0f x %.0f, paper %.0f x %.0f at x%.1f",
                 (double) w, (double) h, (double) (PAPER_W * PAPER_SCALE),
                 (double) (PAPER_H * PAPER_SCALE), (double) PAPER_SCALE);
        report_line("credits", line);
    }
}

/* our own picture, into the resource table beside the game's */
void shim_credits_open_assets(void *manager) {
    if (!manager) return;
    shim_asset_bring_in(manager, "port_credits.anm2", "gfx", "port_credits.anm2");
    shim_asset_bring_in(manager, "port_credits.png", "gfx", "port_credits.png");
    for (int set = 1; set < PAPER_SETS; set++) {
        char anm2[40], png[40];
        snprintf(anm2, sizeof(anm2), "port_credits_%s.anm2", PAPER_SUFFIX[set]);
        snprintf(png, sizeof(png), "port_credits_%s.png", PAPER_SUFFIX[set]);
        shim_asset_bring_in(manager, anm2, "gfx", anm2);
        shim_asset_bring_in(manager, png, "gfx", png);
    }
    LOG("credits: the picture is in the resource table");
}
