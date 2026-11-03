/*
 * THE TOUCH CONTROLS EDITOR: the way in, and the empty place it opens on.
 *
 * The owner asked (12.09.2026) for the sticks and the button row to be placed
 * by hand, in the game, rather than by numbers in port-touch.ini. The way in is
 * the game's own Controls screen, and the place is an empty corner of the main
 * menu's world.
 *
 * HOW THE WAY IN WORKS. The Controls screen (screen 11, update 0x5979a0)
 * rebuilds its list of devices every frame and passes over the touch device on
 * purpose: 0x597a38 compares the device with 0x10DEAD and skips it. With that
 * one word taken out (loader.c) the touch device is listed like any other, and
 * the game draws, scrolls and confirms the row itself. On a confirmation the
 * screen remembers the device (0x769d3c, menu+0x34) and asks the menu to move
 * to the key-binding screen 12 (0x769b88). That request is where we stand: if
 * the device it just chose is the touch one, the screen stays where it is and
 * the editor opens instead.
 *
 * WHERE IT OPENS. The main menu is one world; every screen has its anchor in
 * the menu object (menu+0x10fc08, a pair of floats per screen, read at
 * 0x768ba4) and the camera slides to the anchor of the current screen. So the
 * editor does not move the camera itself: it writes a far-away anchor for the
 * Controls screen, and the game's own camera carries it there, at its own
 * speed, exactly as it does between screens. On the way out the anchor is put
 * back and the camera comes home the same way.
 *
 * While it is open every finger belongs to the editor, so nothing behind it is
 * touched, and it is left by its own button alone - a tap anywhere used to
 * close it, which took the owner out by accident (12.09.2026).
 *
 * THE GAME'S OWN PLAQUES. The menu draws two of them, BACK and SELECT, from
 * gfx/ui/BackSelectWidget.anm2 into two sprites of its own - menu+0x1a0 and
 * menu+0x2f8 (0x768260 and 0x7682a8). Neither has anything to do here: the
 * editor is left by its tick alone, and the sticks stand over the corners where
 * the plaques lie, so they would be hard to hit anyway. While the editor is up
 * both are drawn clear - the alpha of their KColor - and put back on the way
 * out, when the Controls screen needs them again.
 *
 * THE OPACITY SLIDER AND THE PAUSE BUTTON (hotfix 1.71, the owner's mockup of
 * 05.10.2026, other stuff/touch-editor-mockup). Under the four buttons a slider
 * sets how solid every touch control is drawn - the game's one half, made ours
 * in loader.c touch_alpha_words - live, while the thumb moves; and everything
 * the editor shows is drawn at that opacity, the way the run draws it, so what
 * is seen here is what the run shows. The pause button is a piece like the
 * others; hud.c puts it where it is dropped, its tap and its long press with it.
 */
#include <math.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <android/log.h>
#include "diag.h"
#include "touchedit_tools.h"

#if ISAAC_DIAG
#define LOG(...) __android_log_print(ANDROID_LOG_INFO, "isaac", __VA_ARGS__)
#else
#define LOG(...) do { } while (0)
#endif

unsigned long shim_image_base(void);
int shim_report_active(void);
void report_line(const char *kind, const char *text);
void shim_asset_bring_in(void *mgr, const char *name, const char *folder, const char *leaf);

#define MENU_PTR        0xac2490    /* the main menu object, a pointer in the image */
#define MENU_SCREEN     0x38        /* the screen it is on (0x768dcc picks by it) */
#define MENU_DEVICE     0x34        /* the device the Controls screen chose (0x769d3c) */
#define MENU_ANCHORS    0x10fc08    /* a Vector2 per screen, read at 0x768ba4 */
#define SCREEN_CONTROLS 11
#define SCREEN_KEYS     12
#define TOUCH_DEVICE    0x10DEAD

/*
 * Where the empty place is. The screens stand in a row a step apart and the
 * step grows with the canvas (loader.c, menu_grid_apply), so a place BELOW the
 * world is the one that stays empty at every width. Two thousand units is
 * about seven screens down.
 */
#define AWAY_Y          2000.0f

/*
 * WHERE THE CAMERA IS, so that nothing is drawn before it has arrived.
 *
 * The menu keeps the camera at menu+0x3c - right after the chosen device and
 * the current screen - and slides it towards that screen's anchor plus the half
 * of the extra: 0x768bd8 takes the distance between the two and steps while it
 * is over one. The editor's things used to be drawn from the first frame, over
 * the menu the camera was still leaving (the owner, 13.09.2026).
 *
 * The camera has exactly AWAY_Y to travel, so how far it has come IS the
 * measure: nothing is drawn for the first nine tenths of it, and the last tenth
 * brings everything up from nothing, so there is no pop at the end either.
 *
 * MEASURED, after a miss: menu+0x10fce0 is not the camera but the anchor of
 * another screen - the anchors start at 0x10fc08 and run eight bytes apart, so
 * that address is simply the twenty-seventh of them. It sat still at 163 while
 * the camera crossed two thousand, and the editor never showed itself.
 */
#define MENU_CAMERA     0x3c
#define EDITOR_SHOWS    0.90f       /* the share of the way after which it appears */


static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;
static int g_open;
static int g_ours = -1;                 /* the finger that came after the editor opened */
static unsigned long g_menu;            /* the menu whose anchor we moved */
static float g_anchor_was[2];
/*
 * THE EDITOR'S OWN BUTTONS, at the top of the empty field where nothing else
 * stands. No words on them, at the owner's word: a grid, a turning arrow, a
 * cross and a tick, from gfx/port_touch_tools.png, one cell of 32 each and in
 * that order.
 *
 * The grid turns the squares on, the arrow puts the places back to the port's
 * own, the tick keeps what was moved and the cross puts back what the places
 * were when the editor opened.
 */
/*
 * WHAT A FINGER CAN MOVE, and the rules it moves by (the owner, 12.09.2026):
 * the movement stick keeps to the left half of the field, the shooting stick to
 * the right half, the card goes anywhere but may not lie over either stick, and
 * nothing may leave the field. With the grid on, a place settles on its lines.
 *
 * The finger holds the thing where it took it: what is remembered is the gap
 * between the finger and the thing's own anchor, so the picture does not jump
 * under the thumb.
 */
enum { GRAB_NONE = -1, GRAB_MOVE, GRAB_SHOOT, GRAB_CARD, GRAB_TWIN, GRAB_PAUSE, GRAB_TRINKET, GRAB_COUNT };

enum { TOOL_GRID, TOOL_RESET, TOOL_CANCEL, TOOL_DONE, TOOL_COUNT };
static const char *const TOOL_ANIMATION[TOOL_COUNT] = { "Grid", "Reset", "Cancel", "Done" };

#define PLACES 16                       /* eight places, two shares each (sticks.c) */

static int g_grid;                      /* the grid is drawn and places snap to it */
static int g_held = -1;                 /* the tool button under the finger, or none */
static int g_grab = GRAB_NONE;          /* the thing the finger is carrying */
static int g_slider;                    /* the finger is on the opacity slider */
static float g_grab_dx, g_grab_dy;      /* the finger, less that thing's anchor */

/*
 * THE PLAQUES RIDE OUT AND BACK, by the game's own hand.
 *
 * They used to be turned off outright - alpha to zero - and the owner asked for
 * the slide instead. The game has one already (screen.c holds the whole table):
 * 0x7697f4 picks a case by the screen from a table of bytes at 0x95858c and
 * walks both plaques six units a frame towards that case's target. Entry 8 is
 * screen 1's case, and it walks BOTH to ninety - clean off the bottom.
 *
 * So the editor writes entry 8 into the Controls screen's slot while it is open
 * and puts the zero back on the way out. Nothing is animated here: the game
 * does the walking, in its own step, and the way back comes free.
 */
int shim_patch_word(unsigned long off, unsigned was_a, unsigned was_b, unsigned now);
void shim_menu_page_sound(void);                /* credits.c */

#define SLIDE_TABLE_11  0x958594    /* screens 9..12: bytes 185, 0, 0, 0 */
#define SLIDE_HOME      0x000000b9u
#define SLIDE_AWAY      0x000800b9u /* the third byte is screen 11's */

static void sprites_try_again(void);

static void plaques_away(int away) {
    shim_patch_word(SLIDE_TABLE_11, SLIDE_HOME, SLIDE_AWAY, away ? SLIDE_AWAY : SLIDE_HOME);
}

int shim_patch_word(unsigned long off, unsigned was_a, unsigned was_b, unsigned now);
void shim_sticks_reset_places(void);              /* sticks.c */
void shim_sticks_places_get(float *six);
void shim_sticks_places_set(const float *six);
void shim_sticks_place_at(int which, float x, float y);
int shim_sticks_places_save(void);
float shim_sticks_alpha(void);
void shim_sticks_alpha_set(float a);
float shim_touch_alpha_live(void);                                 /* loader.c */
int shim_hud_pause_home(float *x, float *y, float *w, float *h);  /* hud.c */
int shim_hud_pause_placed(float *x, float *y);
int shim_sticks_twin_anchor(float *x, float *y);
int shim_hud_twin_home(float *x, float *y, float *w, float *h);  /* hud.c */
int shim_twins_button_preview(const int *ids, float *box, int *xy);  /* twintrinkets.c */


static float g_places_were[PLACES];     /* what they were when the editor opened */
static float g_alpha_were = 0.5f;       /* and the opacity */
static float g_camera_was;
static float g_target_y, g_travel;      /* the mark the camera makes for, and the way to it */
static float g_fade = 1.0f;            /* how much of the way it has come, the last tenth */

static float *anchor_of(unsigned long menu, int screen) {
    return (float *) (menu + MENU_ANCHORS + (unsigned long) screen * 8);
}

/* under the lock */
static void open_here(unsigned long menu) {
    float *a = anchor_of(menu, SCREEN_CONTROLS);
    g_anchor_was[0] = a[0];
    g_anchor_was[1] = a[1];
    a[1] += AWAY_Y;
    plaques_away(1);                    /* they ride out the way the game rides them */
    sprites_try_again();                /* a picture that failed last time is tried afresh */
    shim_sticks_places_get(g_places_were);
    g_alpha_were = shim_sticks_alpha();
    g_camera_was = ((const float *) (menu + MENU_CAMERA))[1];
    g_target_y = a[1];                  /* where the camera is going, in the menu's world */
    g_travel = fabsf(g_target_y - g_camera_was);   /* and how far that is from here */
    g_fade = 0.0f;
    g_menu = menu;
    g_open = 1;
    g_held = -1;
    shim_menu_page_sound();                   /* the same page the menu turns (credits.c) */
    LOG("editor: opened, the camera goes to %.0f,%.0f", (double) a[0], (double) a[1]);
    if (shim_report_active()) {
        char line[140];
        snprintf(line, sizeof(line), "opened: the Controls anchor %.0f,%.0f becomes %.0f,%.0f",
                 (double) g_anchor_was[0], (double) g_anchor_was[1], (double) a[0], (double) a[1]);
        report_line("editor", line);
    }
}

/* under the lock */
static void close_here(void) {
    if (!g_open) return;
    g_slider = 0;
    if (g_menu) {
        float *a = anchor_of(g_menu, SCREEN_CONTROLS);
        a[0] = g_anchor_was[0];
        a[1] = g_anchor_was[1];
        plaques_away(0);
    }
    g_open = 0;
    g_menu = 0;
    g_held = -1;
    shim_menu_page_sound();
    LOG("editor: closed");
    if (shim_report_active()) report_line("editor", "closed: the anchor is back");
}

int shim_touchedit_open(void) {
    pthread_mutex_lock(&g_lock);
    int open = g_open;
    pthread_mutex_unlock(&g_lock);
    return open;
}

/*
 * The menu is asked to change screens (hook V on 0x769b88, menu in x0 and the
 * screen in w1). The request to open the key bindings for the TOUCH device is
 * turned into the editor: w1 goes back to the screen we are already on, so the
 * menu stays, and the anchor takes the camera away.
 */
void shim_touchedit_screen(unsigned long menu, unsigned long screen, unsigned long *regs) {
    if (!menu || !regs) return;
    pthread_mutex_lock(&g_lock);
    if (!g_open && (int) screen == SCREEN_KEYS
        && *(const int *) (menu + MENU_DEVICE) == TOUCH_DEVICE) {
        regs[1] = SCREEN_CONTROLS;
        open_here(menu);
    } else if (g_open) {
        close_here();                   /* any other change of screen closes it */
    }
    pthread_mutex_unlock(&g_lock);
}

/*
 * A touch, before the queue (uikit.c): while the editor is up every finger is
 * its own.
 *
 * Only a finger that ARRIVED after the opening can close it. The finger that
 * chose the row in the list is still down when the editor opens, and its lift
 * was taken for the way out - the camera left and came straight back, which is
 * what the owner saw (12.09.2026).
 */
static int to_field(float x, float y, float *gx, float *gy);   /* with the drawing, below */
static int tool_at(float gx, float gy);
static int slider_at(float gx, float gy);
static void slider_to(float gx);
static int grab_at(float gx, float gy, float *dx, float *dy);
static void grab_move(int what, float gx, float gy, int grid);

int shim_touchedit_touch(int id, int phase, float x, float y) {
    pthread_mutex_lock(&g_lock);
    int mine = g_open;
    if (mine) {
        float gx = 0.0f, gy = 0.0f;
        int on = to_field(x, y, &gx, &gy) ? tool_at(gx, gy) : -1;
        if (phase == 0) {
            g_ours = id;                         /* this one is ours from the start */
            /*
             * A piece before the slider: one placed where the slider now stands, by
             * an older build or by hand, must still come out from under it - and
             * nothing can be dropped there any more (obstacles, controls_rect).
             */
            if (on >= 0) g_held = on;
            else if ((g_grab = grab_at(gx, gy, &g_grab_dx, &g_grab_dy)) != GRAB_NONE) { }
            else if (slider_at(gx, gy)) { g_slider = 1; slider_to(gx); }   /* the knob jumps to the thumb */
        } else if (id == g_ours) {
            if (g_grab != GRAB_NONE && phase < 3) grab_move(g_grab, gx, gy, g_grid);
            if (g_slider && phase < 3) slider_to(gx);   /* live: the run's controls follow at once */
            if (phase >= 3) {
                int hit = g_held;
                g_ours = -1;
                g_held = -1;
                g_grab = GRAB_NONE;
                g_slider = 0;
                if (phase == 3 && hit >= 0 && on == hit) {     /* lifted where it was put down; cancelled acts not (U7) */
                    if (hit == TOOL_GRID) g_grid = !g_grid;
                    else if (hit == TOOL_RESET) {
                        shim_sticks_reset_places();
                        shim_sticks_alpha_set(0.5f);             /* the game's own half */
                    }
                    else if (hit == TOOL_DONE) { shim_sticks_places_save(); close_here(); }
                    else if (hit == TOOL_CANCEL) {
                        shim_sticks_places_set(g_places_were);   /* leave it as it was found */
                        shim_sticks_alpha_set(g_alpha_were);
                        close_here();
                    }
                }
            } else if (g_held >= 0 && on != g_held) {
                g_held = -1;                     /* the thumb slid off the button */
            }
        }
    }
    pthread_mutex_unlock(&g_lock);
    return mine;
}

/*
 * THE ROW IS CALLED "Touch".
 *
 * The screen keeps its list itself: sixteen bytes an entry from screen+0x168 to
 * +0x170, the name in a buffer of its own at +0 and the device at +8 (the list
 * is built at 0x597a40..0x597ad8, the name through snprintf "%s" into sixty-four
 * bytes). The game's own name for the touch device is "DeviceiOS", which says
 * nothing to a player, so the buffer is written afresh on every draw - the list
 * is rebuilt every frame, and the draw is the moment after it.
 */
void shim_touchedit_name(unsigned long screen) {
    if (!screen) return;
    char **begin = *(char ***) (screen + 0x168);
    char **end = *(char ***) (screen + 0x170);
    if (!begin || end <= begin) return;
    for (char **e = begin; e + 2 <= end; e += 2) {
        char *name = e[0];
        int device = *(const int *) ((const char *) e + 8);
        if (device == TOUCH_DEVICE && name) {
            static int told;
            if (strcmp(name, "Touch")) {
                snprintf(name, 0x40, "Touch");
                if (!told && shim_report_active()) { told = 1; report_line("editor", "the touch row is named Touch"); }
            }
        }
    }
}

/* once between frames (gl.c): the editor belongs to the main menu and nothing else */
void shim_touchedit_between_frames(void) {
    pthread_mutex_lock(&g_lock);
    if (g_open) {
        unsigned long b = shim_image_base();
        unsigned long menu = b ? *(const unsigned long *) (b + MENU_PTR) : 0;
        if (!menu || menu != g_menu || *(const int *) (menu + MENU_SCREEN) != SCREEN_CONTROLS)
            close_here();
    }
    pthread_mutex_unlock(&g_lock);
}

/* ------------------------------------------------------------- the picture */

/*
 * Everything is drawn with the game's own sprite code, in canvas units, from
 * the overlay's draw (hook O). The overlay is drawn over the menu and in the
 * screen's own coordinates, not the world's - which is why the camera's trip
 * to the empty place changes nothing here.
 *
 *   the sticks   the game's own gfx/ui/touch/iOS_Buttons.anm2 (the size the
 *                player chose is substituted into the path by sticks.c), the
 *                animations "Analog Bounds" and "Analog Stick Move"/"Shoot",
 *                drawn at a quarter, which is how the overlay draws them
 *   the icons    our port_touch_editor.anm2 with the game's picture put into
 *                its layer: the die, a card, the large bomb
 *   the lines    the same file's single pixel, stretched by the scale
 */
#define FN_SPRITE_NEW      0x89c16c
#define FN_SPRITE_LOAD     0x89fe24
#define FN_SPRITE_FRAME    0x89eab8
#define FN_SPRITE_RENDER   0x89e4c0
#define FN_SPRITE_REPLACE  0x8a0228
#define FN_SPRITE_GRAPHICS 0x8a0284
#define ZERO_VECTOR        0x95e7a0
#define SPRITE_BYTES       0x158
#define SPRITE_COLOUR      0xe8
#define SPRITE_SCALE       0xdc
#define SPRITE_LOADED      0x149

#define UNIT_PIXELS        0xa9c7c8    /* the row's f is 0.5 over this (0x860354) */
#define ROW_ABOVE_BOTTOM   0x947124    /* the share of the height the row sits above (0x8603c4) */
#define ROW_GAP            24.0f       /* hud.c: the gap between the row and the ring */
#define CARD_SLOTS         7           /* the widest row the game can build */

#define STICK_ANM2         "gfx/ui/touch/ios_buttons_100.anm2"   /* the name is built by size */
#define EDITOR_ANM2        "gfx/port_touch_editor.anm2"
#define DIE_PICTURE        "gfx/items/collectibles/collectibles_105_dice.png"
#define BIBLE_PICTURE      "gfx/items/collectibles/collectibles_033_thebible.png"
#define MARKED_PICTURE     "gfx/items/collectibles/collectibles_394_marked.png"
#define BAG_PICTURE        "gfx/items/collectibles/collectibles_710_bagofcrafting.png"
#define PILL_PICTURE       "gfx/ui/touch/ui_cardspills_2x.png"
#define BAR_PICTURE        "gfx/ui/touch/ui_chargebar.png"
#define WINDOW_PIXELS      0xa9aff8    /* the window, as the stick's draw reads it (0x861ed8) */
#define AREA               0xabec64    /* the fitted picture: width, height, x, y in pixels */
#define CARD_PICTURE       "gfx/ui/touch/ui_cardfronts_2x.png"
#define BOMB_PICTURE       "gfx/ui/largebomb.png"
#define TOOLS_PICTURE      "gfx/port_touch_tools.png"

/*
 * THE TWINS' TRINKET BUTTON, AT ITS FULLEST (1.75, the owner, 14.10.2026):
 * four trinkets in the cross, two of Jacob's and two of Esau's, in the order
 * the run gathers them - Jacob's slot 0 left, his slot 1 on top, Esau's slot 0
 * right, his slot 1 below. Any four would do; these read at a glance. Each is
 * its whole 32 x 32 file, drawn as the HUD draws a trinket: at one texel a
 * unit, its shadow first - black at 0.3 of the picture's alpha, two units
 * down and right (RenderTrinket 0x6e840c..0x6e848c).
 */
#define TRINKET_SHOWN      4
#define TRINKET_SHADOW     0.3f        /* [0x946378] */
#define TRINKET_SHADOW_AT  2.0f
static const int TRINKET_ID[TRINKET_SHOWN] = { 1, 19, 2, 3 };
#define TRINKET_PICTURE_0  "gfx/items/trinkets/trinket_001_swallowedpenny.png"
#define TRINKET_PICTURE_1  "gfx/items/trinkets/trinket_019_paperclip.png"
#define TRINKET_PICTURE_2  "gfx/items/trinkets/trinket_002_petrifiedpoop.png"
#define TRINKET_PICTURE_3  "gfx/items/trinkets/trinket_003_aaabattery.png"

/*
 * The buttons and the grid, in TEXELS: everything here is drawn at 2 f, the
 * same as an item in the row, so a texel is a unit of the layout.
 */
/*
 * The step from one button's middle to the next is the language's own
 * (touchedit_tools.h, from tools/editor_tools_sheet.py): a button is an icon
 * over a word, and where the words are long the buttons stand further apart
 * instead of the words being cut (the owner's item 4, 23.09.2026). A button is
 * touched anywhere in its step bar a sliver, the height of its cell.
 */
#define TOOL_TOP           26.0f     /* the middle's height below the top edge */
#define TOOL_SLIVER         4.0f     /* left untouchable between two buttons */
#define GRID_PARTS         16        /* the cell the grid AIMS for: a sixteenth of the height */
#define ROWS_SHOWN         2         /* the card is two rows tall: a second player has one */

float shim_screen_width(void);
float shim_screen_height(void);
int shim_sticks_seat_at(int is_move, float *x, float *y);
int shim_sticks_seat_home(int is_move, float *x, float *y);
int shim_sticks_clear_of_sticks(float *box);
int shim_sticks_static(void);
float shim_sticks_ring_units(void);
float shim_sticks_ring_outer(void);
int shim_sticks_row_anchor(float *right, float *middle);
void *shim_str_assign(void *self, const char *s);
void shim_str_dtor(void *self);

enum { SP_STICKS, SP_LINE, SP_DIE, SP_BIBLE, SP_CARD, SP_BOMB, SP_MARKED, SP_BAG,
       SP_PILL, SP_BAR, SP_TOOLS, SP_TRINKET0, SP_TRINKET1, SP_TRINKET2, SP_TRINKET3, SP_COUNT };

/*
 * A picture that means something in the game is drawn THROUGH THE GAME'S OWN
 * .anm2 (plan 30.09, item 13): the bomb button is ui_touchbomb.anm2 "Default",
 * the charge bar ui_chargebar.anm2 "BarFull" - the frames the game itself draws
 * them with. A mod that redraws largebomb.png or ui_chargebar.png with another
 * layout brings its .anm2 along, and the editor shows what the game shows; a
 * crop of the sheet by our own numbers would cut its picture up. (Both frames
 * are the very crops and pivots port_touch_editor.anm2 had: nothing moves.)
 * The pill and the card keep their own frames for now: the game's put them 4
 * texels higher (Pills YPivot 28, CardFronts 20 against our 32 and 24) - the
 * owner's call. An item is its whole 32x32 file: nothing to cut there.
 */
#define BOMB_ANM2          "gfx/ui/touch/ui_touchbomb.anm2"
#define BAR_ANM2           "gfx/ui/touch/ui_chargebar.anm2"

static const char *const SPRITE_FILE[SP_COUNT] = {
    STICK_ANM2, EDITOR_ANM2, EDITOR_ANM2, EDITOR_ANM2, EDITOR_ANM2, BOMB_ANM2,
    EDITOR_ANM2, EDITOR_ANM2, EDITOR_ANM2, BAR_ANM2, EDITOR_ANM2,
    EDITOR_ANM2, EDITOR_ANM2, EDITOR_ANM2, EDITOR_ANM2,
};
static const char *const SPRITE_SHEET[SP_COUNT] = {
    0, 0, DIE_PICTURE, BIBLE_PICTURE, CARD_PICTURE, 0,
    MARKED_PICTURE, BAG_PICTURE, PILL_PICTURE, 0, TOOLS_PICTURE,
    TRINKET_PICTURE_0, TRINKET_PICTURE_1, TRINKET_PICTURE_2, TRINKET_PICTURE_3,
};
/* the game's animation for a sprite drawn through the game's .anm2 (0: the editor's own name) */
static const char *const SPRITE_ANIM[SP_COUNT] = {
    0, 0, 0, 0, 0, "Default", 0, 0, 0, "BarFull", 0,
    0, 0, 0, 0,
};

static unsigned char g_sprite[SP_COUNT][SPRITE_BYTES] __attribute__((aligned(16)));
static signed char g_sprite_state[SP_COUNT];

/*
 * THE STICK PICTURE FOLLOWS THE SIZE SETTING.
 *
 * ASKED FOR BY THE VARIANT'S OWN NAME. The resource store keeps what it parsed
 * under the name it was GIVEN, so asking again for "iOS_Buttons.anm2" hands
 * back the frames of the size that was read first, however the file behind
 * that name has been substituted since - sticks.c learnt this the hard way and
 * reloads the game's own stick sprites from "ios_buttons_<pct>.anm2" for the
 * same reason. The editor asks the same way: a different name is a different
 * key, and the store has to read it.
 *
 * The size is remembered beside the sprite, and when it changes the picture is
 * loaded afresh - the sprite itself is built only once, Load replaces its
 * contents. Without this the editor went on showing whatever size was set when
 * it first opened (the owner, 13.09.2026).
 */
static signed char g_sprite_made[SP_COUNT];
static int g_stick_pct;

int shim_sticks_size_pct(void);                  /* sticks.c */

/* the editor is opening: forget the loads that failed, keep the ones that took */
static void sprites_try_again(void) {
    for (int i = 0; i < SP_COUNT; i++)
        if (g_sprite_state[i] < 0) g_sprite_state[i] = 0;
}

static int sprite_ready(int which) {
    if (which == SP_STICKS) {
        int pct = shim_sticks_size_pct();
        if (pct != g_stick_pct) {
            g_stick_pct = pct;
            g_sprite_state[which] = 0;           /* load it again, at the new size */
        }
    }
    if (g_sprite_state[which]) return g_sprite_state[which] > 0;
    unsigned long b = shim_image_base();
    if (!b) return 0;
    /*
     * ONE ATTEMPT PER OPENING, not one attempt ever.
     *
     * This used to be written down as a refusal for good, and the owner found
     * what that costs: open the editor before the resource is ready and that
     * picture never loads again until the game is restarted - and the faster he
     * tapped, the more of them were lost, one per opening (14.09.2026). The
     * mark still keeps a failed load from being retried every frame, which is
     * what it was for; open_here clears the failures so the next opening tries
     * afresh.
     */
    g_sprite_state[which] = -1;
    unsigned char *s = g_sprite[which];
    if (!g_sprite_made[which]) {
        memset(s, 0, SPRITE_BYTES);
        ((void (*)(void *)) (b + FN_SPRITE_NEW))(s);
        g_sprite_made[which] = 1;
    }

    char want[160];
    if (which == SP_STICKS)
        snprintf(want, sizeof(want), "gfx/ui/touch/ios_buttons_%d.anm2", g_stick_pct);
    else
        snprintf(want, sizeof(want), "%.159s", SPRITE_FILE[which]);

    long long raw[3];
    unsigned char *path = (unsigned char *) raw;
    path[23] = 0;
    shim_str_assign(path, want);
    ((void (*)(void *, const void *, int)) (b + FN_SPRITE_LOAD))(s, path, 1);
    shim_str_dtor(path);
    if (!s[SPRITE_LOADED]) {
        if (shim_report_active()) {
            char line[160];
            snprintf(line, sizeof(line), "%s did NOT load", want);
            report_line("editor", line);
        }
        return 0;
    }
    if (which == SP_STICKS && shim_report_active()) {
        char line[160];
        snprintf(line, sizeof(line), "the sticks are drawn from %s", want);
        report_line("editor", line);
    }
    if (SPRITE_SHEET[which]) {
        path[23] = 0;
        shim_str_assign(path, SPRITE_SHEET[which]);
        ((void (*)(void *, int, const void *)) (b + FN_SPRITE_REPLACE))(s, 0, path);
        shim_str_dtor(path);
        ((void (*)(void *, int)) (b + FN_SPRITE_GRAPHICS))(s, 0);
    }
    g_sprite_state[which] = 1;
    return 1;
}

/*
 * A picture, and the same picture as a WHITE silhouette.
 *
 * The colour of a sprite is the engine's KColor: four floats of tint at +0xe8
 * and three of offset after them. The game paints the bomb's outline with
 * exactly this - tint (0,0,0,1) and offset (1,1,1), four times, one pixel to
 * each side (0x85fcd0..0x85fdf8) - and the charged actives in the row wear the
 * same outline, so the editor draws it the same way.
 */
enum { PAINT_PICTURE, PAINT_WHITE, PAINT_BLACK };

static void draw_tinted(int which, const char *animation, float x, float y,
                        float sx, float sy, float alpha, int paint) {
    unsigned long b = shim_image_base();
    if (!b || !sprite_ready(which)) return;
    unsigned char *s = g_sprite[which];
    if (SPRITE_ANIM[which]) animation = SPRITE_ANIM[which];
    ((void (*)(void *, const char *, int)) (b + FN_SPRITE_FRAME))(s, animation, 0);
    float *colour = (float *) (s + SPRITE_COLOUR);
    colour[0] = colour[1] = colour[2] = paint == PAINT_PICTURE ? 1.0f : 0.0f;
    colour[3] = alpha * g_fade;         /* nothing until the camera is there */
    colour[4] = colour[5] = colour[6] = paint == PAINT_WHITE ? 1.0f : 0.0f;
    float *scale = (float *) (s + SPRITE_SCALE);
    scale[0] = sx;
    scale[1] = sy;
    float pos[2] = { x, y };
    ((void (*)(void *, const float *, const void *, const void *)) (b + FN_SPRITE_RENDER))(
        s, pos, (const void *) (b + ZERO_VECTOR), (const void *) (b + ZERO_VECTOR));
}

static void draw_sprite(int which, const char *animation, float x, float y,
                        float sx, float sy, float alpha) {
    draw_tinted(which, animation, x, y, sx, sy, alpha, PAINT_PICTURE);
}

/* ------------------------------------------------------------ the measures */

/* the layout's own measure: the slot is 72 f, so a texel drawn at 2 f is a unit */
static float measure(void) {
    unsigned long b = shim_image_base();
    if (!b) return 0.0f;
    float px = *(const float *) (b + UNIT_PIXELS);
    return px > 0.0f ? 0.5f / px : 0.0f;
}

/*
 * TWO SPACES ON ONE PAGE (screen.c, "one finger, two spaces").
 *
 * The editor is drawn from hook O, inside the overlay's widget pass, and the
 * viewport there is the PICTURE: the card, the twins' button, the tool buttons
 * and the grid are all laid out in the picture's units, and so is a finger
 * here. A stick, though, lives in the WINDOW's units - it is seated and drawn
 * after the widgets, over the whole window (0x8a7208, 0x861e78), and sticks.c
 * keeps its seat in those units. With FULLSCREEN on the two are one; with it
 * off the picture is letterboxed inside the window, and a seat drawn here as it
 * stands stood beside where the run puts the stick - and a stick dragged here
 * landed beside where it was dropped. So a seat is carried into the picture to
 * be shown, touched and kept clear of, and back into the window to be stored.
 */
int shim_screen_finger_units(float x, float y, float *u, float *v);
float shim_screen_overlay_to_area_x(float x);
float shim_screen_overlay_to_area_y(float y);
float shim_screen_area_to_overlay_x(float x);
float shim_screen_area_to_overlay_y(float y);

/* a touch, from screen pixels into the picture's units everything here is laid out in */
static int to_field(float x, float y, float *gx, float *gy) {
    return shim_screen_finger_units(x, y, gx, gy);
}

/* where a stick stands, in the picture's units the editor works in */
static int seat_shown(int is_move, float *x, float *y) {
    if (!shim_sticks_seat_at(is_move, x, y)) return 0;
    *x = shim_screen_overlay_to_area_x(*x);
    *y = shim_screen_overlay_to_area_y(*y);
    return 1;
}

/* a stick put down at a point of the picture: stored in the window's units it lives in */
static void seat_store(int which, float x, float y) {
    shim_sticks_place_at(which, shim_screen_area_to_overlay_x(x), shim_screen_area_to_overlay_y(y));
}

int shim_tweaks_set_index(void);

/* the language set the buttons are drawn for: the tweaks page's own choice */
static int tool_set(void) {
    int set = shim_tweaks_set_index();
    return (set >= 0 && set < TOOL_SETS) ? set : 0;
}

/* one tool button's middle, the size a finger finds it in, and the drawing's scale, in units */
static int tool_box(int i, float *cx, float *cy, float *bw, float *bh, float *scale) {
    float f = measure(), w = shim_screen_width();
    if (!(f > 0.0f) || !(w > 0.0f)) return 0;
    float step = TOOL_STEP_OF[tool_set()];
    *scale = 2.0f * f;
    *bw = (step - TOOL_SLIVER) * *scale;
    *bh = TOOL_HIGH * *scale;
    *cx = w * 0.5f + ((float) i - (float) (TOOL_COUNT - 1) * 0.5f) * (step * *scale);
    *cy = TOOL_TOP * *scale;
    return 1;
}

/* ------------------------------------------------------------ the boxes */

static void card_now(unsigned long b, float *right, float *middle,
                     float *slot_w, float *slot_h, float *gap);

struct box { float x0, y0, x1, y1; };

static int boxes_meet(const struct box *a, const struct box *b) {
    return a->x0 < b->x1 && b->x0 < a->x1 && a->y0 < b->y1 && b->y0 < a->y1;
}

/*
 * The shortest way out. A ring is taken as the square around it: the sticks and
 * the card are both wide things and a square keeps the pushing predictable -
 * a thing slides along an edge instead of creeping round a curve.
 */
static void push_out(const struct box *a, const struct box *b, float *dx, float *dy) {
    float left = b->x0 - a->x1, right = b->x1 - a->x0;
    float up = b->y0 - a->y1, down = b->y1 - a->y0;
    float best = left, other = 0.0f;
    if (fabsf(right) < fabsf(best)) best = right;
    if (fabsf(up) < fabsf(best)) { other = up; }
    if (fabsf(down) < fabsf(best) && fabsf(down) < fabsf(up)) { other = down; }
    if (other != 0.0f) { *dx = 0.0f; *dy = other; return; }
    *dx = best; *dy = 0.0f;
}

/* the box a stick takes up, and takes a finger in: the OUTER circle of it */
static int stick_box(int is_move, struct box *box, float *cx, float *cy, float *r) {
    /*
     * Only a stick that STANDS somewhere has a box. In the dynamic and floating
     * modes it comes to the finger and is nowhere until then, so there is
     * nothing to draw, nothing to take hold of and nothing to keep clear of -
     * the editor then places the card and the twins' button alone, in a layout
     * of their own (sticks.c).
     */
    if (!shim_sticks_static()) return 0;
    float x = 0.0f, y = 0.0f, rr = shim_sticks_ring_outer();
    if (!(rr > 0.0f) || !seat_shown(is_move, &x, &y)) return 0;
    if (box) { box->x0 = x - rr; box->y0 = y - rr; box->x1 = x + rr; box->y1 = y + rr; }
    if (cx) *cx = x;
    if (cy) *cy = y;
    if (r) *r = rr;
    return 1;
}

/* the card as it stands: its box, its anchor, and the slot it is built from */
static int card_rect(struct box *box, float *right, float *middle,
                     float *sw, float *sh, float *gap) {
    unsigned long b = shim_image_base();
    if (!b) return 0;
    float r = 0.0f, m = 0.0f, w = 0.0f, h = 0.0f, g = 0.0f;
    card_now(b, &r, &m, &w, &h, &g);
    if (!(w > 0.0f)) return 0;
    if (box) {
        float width = (float) CARD_SLOTS * w + (float) (CARD_SLOTS - 1) * g;
        box->x0 = r - width;
        box->x1 = r;
        box->y0 = m - h * 0.5f;
        box->y1 = m + h * ((float) ROWS_SHOWN - 0.5f);
    }
    if (right) *right = r;
    if (middle) *middle = m;
    if (sw) *sw = w;
    if (sh) *sh = h;
    if (gap) *gap = g;
    return 1;
}

/* ----------------------------------------------------------- moving things */

/*
 * The twins' round button. Where it stands when nobody has moved it is the
 * game's own place, worked out by hud.c from the same numbers the overlay uses,
 * so the editor can show it in the menu where nothing has been laid out.
 */
static int twin_rect(struct box *box, float *cx, float *cy) {
    float x = 0.0f, y = 0.0f, w = 0.0f, h = 0.0f;
    if (!shim_hud_twin_home(&x, &y, &w, &h)) return 0;
    float px = 0.0f, py = 0.0f;
    if (shim_sticks_twin_anchor(&px, &py)) {
        float box[4] = { px - w * 0.5f, py - h * 0.5f, px + w * 0.5f, py + h * 0.5f };
        if (shim_sticks_clear_of_sticks(box)) {
            px = (box[0] + box[2]) * 0.5f;
            py = (box[1] + box[3]) * 0.5f;
        }
        x = px;
        y = py;
    }
    if (box) { box->x0 = x - w * 0.5f; box->y0 = y - h * 0.5f;
               box->x1 = x + w * 0.5f; box->y1 = y + h * 0.5f; }
    if (cx) *cx = x;
    if (cy) *cy = y;
    return 1;
}

/*
 * The pause button: the game's own place (hud.c, the numbers the overlay lays it
 * out by), or the player's, pushed clear of the sticks as the run pushes it.
 */
static int pause_rect(struct box *box, float *cx, float *cy) {
    float x = 0.0f, y = 0.0f, w = 0.0f, h = 0.0f;
    if (!shim_hud_pause_home(&x, &y, &w, &h)) return 0;
    shim_hud_pause_placed(&x, &y);
    if (box) { box->x0 = x - w * 0.5f; box->y0 = y - h * 0.5f;
               box->x1 = x + w * 0.5f; box->y1 = y + h * 0.5f; }
    if (cx) *cx = x;
    if (cy) *cy = y;
    return 1;
}

/*
 * The twins' trinket button: its square as the run stands it (twintrinkets.c,
 * by the overlay layout's own rule here, where no run lays the zone out), and
 * the four trinkets' top-left corners on it.
 */
static int trinket_rect(struct box *box, float *cx, float *cy, int *xy) {
    float sq[4];
    int at[TRINKET_SHOWN * 2];
    if (!shim_twins_button_preview(TRINKET_ID, sq, at)) return 0;
    if (box) { box->x0 = sq[0]; box->y0 = sq[1]; box->x1 = sq[2]; box->y1 = sq[3]; }
    if (cx) *cx = (sq[0] + sq[2]) * 0.5f;
    if (cy) *cy = (sq[1] + sq[3]) * 0.5f;
    if (xy) memcpy(xy, at, sizeof(at));
    return 1;
}

static int controls_rect(struct box *box);      /* the editor's own buttons and slider, below */

/* everything the thing being moved has to keep out of */
static int obstacles(int me, struct box *out) {
    int n = 0;
    for (int i = 0; i < GRAB_COUNT; i++) {
        if (i == me) continue;
        struct box b;
        int have = 0;
        if (i == GRAB_MOVE) have = stick_box(1, &b, 0, 0, 0);
        else if (i == GRAB_SHOOT) have = stick_box(0, &b, 0, 0, 0);
        else if (i == GRAB_CARD) have = card_rect(&b, 0, 0, 0, 0, 0);
        else if (i == GRAB_TWIN) have = twin_rect(&b, 0, 0);
        else if (i == GRAB_PAUSE) have = pause_rect(&b, 0, 0);
        else if (i == GRAB_TRINKET) have = trinket_rect(&b, 0, 0, 0);
        if (have) out[n++] = b;
    }
    /*
     * And the editor's own controls: a piece dropped under the buttons or the
     * slider could not be taken hold of again - the finger goes to them first.
     */
    struct box c;
    if (controls_rect(&c)) out[n++] = c;
    return n;
}

static void keep_inside(struct box *b, float lo, float hi, float top, float bottom) {
    float w = b->x1 - b->x0, h = b->y1 - b->y0;
    if (w > hi - lo || h > bottom - top) return;
    if (b->x0 < lo) { b->x0 = lo; b->x1 = lo + w; }
    if (b->x1 > hi) { b->x1 = hi; b->x0 = hi - w; }
    if (b->y0 < top) { b->y0 = top; b->y1 = top + h; }
    if (b->y1 > bottom) { b->y1 = bottom; b->y0 = bottom - h; }
}

/*
 * Push the box out of everything it lies over and back inside its walls, twice
 * over - a push away from one thing can be a push into another - and answer
 * whether it came to rest. It does not, when there is nowhere to go; then the
 * move is simply not made and the thing stays where it was.
 */
static int settle(struct box *me, int who, float lo, float hi, float top, float bottom) {
    struct box other[GRAB_COUNT + 1];
    int n = obstacles(who, other);
    keep_inside(me, lo, hi, top, bottom);
    for (int pass = 0; pass < 2; pass++) {
        for (int i = 0; i < n; i++) {
            if (!boxes_meet(me, &other[i])) continue;
            float dx = 0.0f, dy = 0.0f;
            push_out(me, &other[i], &dx, &dy);
            me->x0 += dx; me->x1 += dx; me->y0 += dy; me->y1 += dy;
        }
        keep_inside(me, lo, hi, top, bottom);
    }
    for (int i = 0; i < n; i++) if (boxes_meet(me, &other[i])) return 0;
    return 1;
}

static float snapped(float v, float step) {
    return step > 0.0f ? floorf(v / step + 0.5f) * step : v;
}

/*
 * THE GRID'S STEP, WORKED OUT FROM THE CANVAS ITSELF.
 *
 * A flat sixteenth of the height does not divide the width: at 585 x 270 it is
 * 16.875 and the last column comes out half wide, so a stick snapped k cells
 * from the left edge and another k cells from the right do NOT stand
 * symmetrically - which is exactly what the owner hit (13.09.2026).
 *
 * So the height's sixteenth is only what the cell AIMS for. The width is then
 * divided into a whole number of cells, and an EVEN number of them, so that the
 * middle of the field - where the two halves meet - is a line of the grid and
 * not the inside of a cell. The height is divided afresh by that width, again
 * whole. Both edges land on lines, the grid mirrors about the middle, and the
 * cell stays square to within a couple of per cent (17.21 x 16.88 at 585 x 270).
 */
static int grid_steps(int grid, float *sx, float *sy) {
    float w = shim_screen_width(), h = shim_screen_height();
    if (!grid || !(w > 0.0f) || !(h > 0.0f)) return 0;
    float want = h / (float) GRID_PARTS;
    int nx = (int) floorf(w / (want * 2.0f) + 0.5f) * 2;
    if (nx < 2) nx = 2;
    float stepx = w / (float) nx;
    int ny = (int) floorf(h / stepx + 0.5f);
    if (ny < 1) ny = 1;
    if (sx) *sx = stepx;
    if (sy) *sy = h / (float) ny;
    return 1;
}

static void move_stick(int is_move, float wx, float wy, int grid) {
    float w = shim_screen_width(), h = shim_screen_height(), r = shim_sticks_ring_outer();
    if (!(w > 0.0f) || !(h > 0.0f) || !(r > 0.0f)) return;
    float sx = 0.0f, sy = 0.0f;
    grid_steps(grid, &sx, &sy);
    float cx = snapped(wx, sx), cy = snapped(wy, sy);
    struct box me = { cx - r, cy - r, cx + r, cy + r };
    /* each stick keeps to its own half, ring and all */
    float lo = is_move ? 0.0f : w * 0.5f;
    float hi = is_move ? w * 0.5f : w;
    if (!settle(&me, is_move ? GRAB_MOVE : GRAB_SHOOT, lo, hi, 0.0f, h)) return;
    seat_store(is_move ? 0 : 1, (me.x0 + me.x1) * 0.5f, (me.y0 + me.y1) * 0.5f);
}

static void move_card(float want_right, float want_middle, int grid) {
    float w = shim_screen_width(), h = shim_screen_height();
    struct box me;
    float sw = 0.0f, sh = 0.0f, gap = 0.0f;
    if (!(w > 0.0f) || !(h > 0.0f) || !card_rect(&me, 0, 0, &sw, &sh, &gap)) return;
    float width = (float) CARD_SLOTS * sw + (float) (CARD_SLOTS - 1) * gap;
    float tall = sh * (float) ROWS_SHOWN;
    float sx = 0.0f, sy = 0.0f;
    grid_steps(grid, &sx, &sy);
    float left = snapped(want_right - width, sx);
    float top = snapped(want_middle - sh * 0.5f, sy);
    me.x0 = left; me.x1 = left + width;
    me.y0 = top; me.y1 = top + tall;
    if (!settle(&me, GRAB_CARD, 0.0f, w, 0.0f, h)) return;
    shim_sticks_place_at(2, me.x1, me.y0 + sh * 0.5f);
}

static void move_twin(float wx, float wy, int grid) {
    float w = shim_screen_width(), h = shim_screen_height();
    struct box me;
    if (!(w > 0.0f) || !(h > 0.0f) || !twin_rect(&me, 0, 0)) return;
    float side_w = me.x1 - me.x0, side_h = me.y1 - me.y0;
    float sx = 0.0f, sy = 0.0f;
    grid_steps(grid, &sx, &sy);
    float cx = snapped(wx, sx), cy = snapped(wy, sy);
    me.x0 = cx - side_w * 0.5f; me.x1 = cx + side_w * 0.5f;
    me.y0 = cy - side_h * 0.5f; me.y1 = cy + side_h * 0.5f;
    if (!settle(&me, GRAB_TWIN, 0.0f, w, 0.0f, h)) return;
    shim_sticks_place_at(3, (me.x0 + me.x1) * 0.5f, (me.y0 + me.y1) * 0.5f);
}

static void move_pause(float wx, float wy, int grid) {
    float w = shim_screen_width(), h = shim_screen_height();
    struct box me;
    if (!(w > 0.0f) || !(h > 0.0f) || !pause_rect(&me, 0, 0)) return;
    float side_w = me.x1 - me.x0, side_h = me.y1 - me.y0;
    float sx = 0.0f, sy = 0.0f;
    grid_steps(grid, &sx, &sy);
    float cx = snapped(wx, sx), cy = snapped(wy, sy);
    me.x0 = cx - side_w * 0.5f; me.x1 = cx + side_w * 0.5f;
    me.y0 = cy - side_h * 0.5f; me.y1 = cy + side_h * 0.5f;
    if (!settle(&me, GRAB_PAUSE, 0.0f, w, 0.0f, h)) return;
    shim_sticks_place_at(4, (me.x0 + me.x1) * 0.5f, (me.y0 + me.y1) * 0.5f);
}

static void move_trinket(float wx, float wy, int grid) {
    float w = shim_screen_width(), h = shim_screen_height();
    struct box me;
    if (!(w > 0.0f) || !(h > 0.0f) || !trinket_rect(&me, 0, 0, 0)) return;
    float side_w = me.x1 - me.x0, side_h = me.y1 - me.y0;
    float sx = 0.0f, sy = 0.0f;
    grid_steps(grid, &sx, &sy);
    float cx = snapped(wx, sx), cy = snapped(wy, sy);
    me.x0 = cx - side_w * 0.5f; me.x1 = cx + side_w * 0.5f;
    me.y0 = cy - side_h * 0.5f; me.y1 = cy + side_h * 0.5f;
    if (!settle(&me, GRAB_TRINKET, 0.0f, w, 0.0f, h)) return;
    shim_sticks_place_at(5, (me.x0 + me.x1) * 0.5f, (me.y0 + me.y1) * 0.5f);
}

/* what a finger has taken hold of, and how far it is from that thing's anchor */
static int grab_at(float gx, float gy, float *dx, float *dy) {
    for (int s = 0; s < 2; s++) {
        float cx = 0.0f, cy = 0.0f, r = 0.0f;
        if (!stick_box(s == 0, 0, &cx, &cy, &r)) continue;
        if ((gx - cx) * (gx - cx) + (gy - cy) * (gy - cy) <= r * r) {
            *dx = gx - cx;
            *dy = gy - cy;
            return s == 0 ? GRAB_MOVE : GRAB_SHOOT;
        }
    }
    struct box pause;
    float px = 0.0f, py = 0.0f;
    if (pause_rect(&pause, &px, &py)
        && gx >= pause.x0 && gx <= pause.x1 && gy >= pause.y0 && gy <= pause.y1) {
        *dx = gx - px;
        *dy = gy - py;
        return GRAB_PAUSE;
    }
    struct box trinket;
    float kx = 0.0f, ky = 0.0f;
    if (trinket_rect(&trinket, &kx, &ky, 0)
        && gx >= trinket.x0 && gx <= trinket.x1 && gy >= trinket.y0 && gy <= trinket.y1) {
        *dx = gx - kx;
        *dy = gy - ky;
        return GRAB_TRINKET;
    }
    struct box twin;
    float tx = 0.0f, ty = 0.0f;
    if (twin_rect(&twin, &tx, &ty)
        && gx >= twin.x0 && gx <= twin.x1 && gy >= twin.y0 && gy <= twin.y1) {
        *dx = gx - tx;
        *dy = gy - ty;
        return GRAB_TWIN;
    }
    struct box card;
    float right = 0.0f, middle = 0.0f;
    if (card_rect(&card, &right, &middle, 0, 0, 0)
        && gx >= card.x0 && gx <= card.x1 && gy >= card.y0 && gy <= card.y1) {
        *dx = gx - right;
        *dy = gy - middle;
        return GRAB_CARD;
    }
    return GRAB_NONE;
}

static void grab_move(int what, float gx, float gy, int grid) {
    if (what == GRAB_MOVE) move_stick(1, gx - g_grab_dx, gy - g_grab_dy, grid);
    else if (what == GRAB_SHOOT) move_stick(0, gx - g_grab_dx, gy - g_grab_dy, grid);
    else if (what == GRAB_CARD) move_card(gx - g_grab_dx, gy - g_grab_dy, grid);
    else if (what == GRAB_TWIN) move_twin(gx - g_grab_dx, gy - g_grab_dy, grid);
    else if (what == GRAB_PAUSE) move_pause(gx - g_grab_dx, gy - g_grab_dy, grid);
    else if (what == GRAB_TRINKET) move_trinket(gx - g_grab_dx, gy - g_grab_dy, grid);
}

/* which button a finger is on, or none */
static int tool_at(float gx, float gy) {
    for (int i = 0; i < TOOL_COUNT; i++) {
        float cx, cy, bw, bh, scale;
        if (!tool_box(i, &cx, &cy, &bw, &bh, &scale)) return -1;
        if (fabsf(gx - cx) <= bw * 0.5f && fabsf(gy - cy) <= bh * 0.5f) return i;
    }
    return -1;
}

/*
 * THE OPACITY SLIDER, in texels at the tool buttons' scale (2 f: a texel a unit),
 * as the mockup has it: the track's middle SLIDER_TOP below the top edge, right
 * under the buttons (their cells end at TOOL_TOP + 17), the two end marks
 * SLIDER_END_AT from the track's ends, the word and the value under it. The
 * finger takes the whole row - track, ends and words - and a little air round it.
 */
#define SLIDER_TOP     54.0f
#define SLIDER_END_AT  12.0f      /* an end mark's middle, out from the track's end */
#define SLIDER_LABEL   17.0f      /* the words' middle line, below the track's */
#define SLIDER_WORD_GAP 4.0f      /* between the word and the value */
#define SLIDER_AIR     6.0f       /* round the row, for the finger */

/* the track's left end, its middle line and the scale; 0 before the layout is known */
static int slider_frame(float *x0, float *cy, float *scale) {
    float f = measure(), w = shim_screen_width();
    if (!(f > 0.0f) || !(w > 0.0f)) return 0;
    *scale = 2.0f * f;
    *x0 = w * 0.5f - SLIDER_TRACK * 0.5f * *scale;
    *cy = SLIDER_TOP * *scale;
    return 1;
}

static int slider_box(struct box *b) {
    float x0, cy, k;
    if (!slider_frame(&x0, &cy, &k)) return 0;
    float reach = (SLIDER_END_AT + SLIDER_END_R + SLIDER_AIR) * k;
    b->x0 = x0 - reach;
    b->x1 = x0 + SLIDER_TRACK * k + reach;
    b->y0 = cy - (SLIDER_KNOB_H * 0.5f + SLIDER_AIR * 0.5f) * k;
    b->y1 = cy + (SLIDER_LABEL + SLIDER_LABEL_H * 0.5f + SLIDER_AIR * 0.5f) * k;
    return 1;
}

static int slider_at(float gx, float gy) {
    struct box b;
    return slider_box(&b) && gx >= b.x0 && gx <= b.x1 && gy >= b.y0 && gy <= b.y1;
}

/* the tool buttons and the slider together: what a piece may not be dropped under */
static int controls_rect(struct box *box) {
    struct box s;
    float cx, cy, bw, bh, scale;
    if (!slider_box(&s) || !tool_box(0, &cx, &cy, &bw, &bh, &scale)) return 0;
    float left = cx - bw * 0.5f, top = cy - bh * 0.5f;
    if (!tool_box(TOOL_COUNT - 1, &cx, &cy, &bw, &bh, &scale)) return 0;
    float right = cx + bw * 0.5f;
    box->x0 = left < s.x0 ? left : s.x0;
    box->x1 = right > s.x1 ? right : s.x1;
    box->y0 = top;
    box->y1 = s.y1;
    return 1;
}

/* the value under the thumb: the slider's steps, the ends past the track included */
static void slider_to(float gx) {
    float x0, cy, k;
    if (!slider_frame(&x0, &cy, &k)) return;
    float t = (gx - x0) / (SLIDER_TRACK * k);
    if (t < 0.0f) t = 0.0f;
    if (t > 1.0f) t = 1.0f;
    float pct = (float) SLIDER_PCT_LOW + t * (float) (SLIDER_PCT_HIGH - SLIDER_PCT_LOW);
    shim_sticks_alpha_set(pct / 100.0f);          /* onto its steps, and into the image (sticks.c) */
}

/* a filled rectangle, from the single pixel: the scale IS its size in units */
static void draw_rect_paint(float x, float y, float w, float h, float alpha, int paint) {
    if (w <= 0.0f || h <= 0.0f) return;
    draw_tinted(SP_LINE, "Pixel", x, y, w, h, alpha, paint);
}

static void draw_rect(float x, float y, float w, float h, float alpha) {
    draw_rect_paint(x, y, w, h, alpha, PAINT_PICTURE);
}

/*
 * The card: the widest row the game can build, where its right edge is and
 * where the middle of its first row sits. Placed by hand it is the settings'
 * own corner; otherwise it is where the row hangs today - a fixed gap before
 * the shooting stick's ring, at the height the game itself works out
 * (0x860410: the canvas height, less its own share, plus three quarters of
 * half a button).
 */
static void card_now(unsigned long b, float *right, float *middle,
                     float *slot_w, float *slot_h, float *gap) {
    float px = *(const float *) (b + UNIT_PIXELS);
    float f = px > 0.0f ? 0.5f / px : 0.0f;
    *slot_w = 72.0f * f;
    *slot_h = 64.0f * f;
    *gap = 3.0f * f;
    if (shim_sticks_row_anchor(right, middle)) {
        /*
         * Placed, and pushed clear of a stick that has grown since - the same
         * step aside the run makes (hud.c), so what the editor shows is what
         * the run will do.
         */
        float width = (float) CARD_SLOTS * *slot_w + (float) (CARD_SLOTS - 1) * *gap;
        float box[4] = { *right - width, *middle - *slot_h * 0.5f, *right,
                         *middle + *slot_h * ((float) ROWS_SHOWN - 0.5f) };
        if (shim_sticks_clear_of_sticks(box)) {
            *right = box[2];
            *middle = box[1] + *slot_h * 0.5f;
        }
        return;
    }
    /* the stick's HOME, so the card does not follow one that has been dragged */
    float sx = 0.0f, sy = 0.0f;
    shim_sticks_seat_home(0, &sx, &sy);
    /* the ring's edge is a point of the window's; the row is the picture's - as the run does it (hud.c) */
    *right = shim_screen_overlay_to_area_x(sx - shim_sticks_ring_units()) - ROW_GAP;
    float part = *(const float *) (b + ROW_ABOVE_BOTTOM);
    float h = shim_screen_height();
    /*
     * One row higher than the game s own bottom row. The card is two rows tall -
     * a second player gets one of his own - and the game makes room for it the
     * same way, by lifting the first row when there are two (0x860420). Without
     * the lift the editor drew the second row off the bottom edge.
     */
    *middle = h - h * part + (*slot_h * 0.5f) * 0.75f - *slot_h;
}

/*
 * ONE SLOT OF THE ROW, at the size the game itself draws it.
 *
 * A button's picture is a 72 x 64 RENDER TARGET (made at 0x863fd8, filled at
 * 0x86410c) which the button then draws to fill its box of 72 f x 64 f
 * (0x8642c0) - so one pixel of the target is f, and the size of a thing on the
 * screen is the size of its picture in the target, times f.
 *
 * Into that target the HUD draws:
 *
 *   an item    the collectible's own 32 x 32 picture at 36,32 SCALE 2, so 64
 *              target pixels - and 2 f to the texel
 *   a card     gfx/ui/touch/ui_cardfronts_2x.png, cells of 32 x 48, at 1
 *   a pill     gfx/ui/touch/ui_cardspills_2x.png, cells of 64 x 64, at 1
 *   the bomb   gfx/ui/largebomb.png, frames of 64 x 64 (ui_touchbomb.anm2), at 1
 *   the bar    gfx/ui/touch/ui_chargebar.png, cells of 32 x 64, at 1
 *
 * The touch pictures are already drawn at twice the HUD's size, which is why
 * they take a scale of one where an item takes two. Checked against the owner's
 * screenshot of a run (12.09.2026): the row's pitch is 200 screen pixels for
 * 75 f, our Marked button (27 texels at 2 f) measures 142, and a card measures
 * 73 x 92 for its 28 x 36 texels at f. A charged active wears a white outline
 * and a charge bar beside it.
 */
struct slot { int sprite; const char *animation; float scale; int charged; const char *ring; };

#define ITEM_SCALE  2.0f                        /* an item is drawn at twice its texels */
#define TOUCH_SCALE 1.0f                        /* the touch pictures are twice already */

static const struct slot ROW_FIRST[] = {        /* from the RIGHT, as the game fills it */
    { SP_BOMB,   "Icon64", TOUCH_SCALE, 0, 0 },
    { SP_PILL,   "Icon64", TOUCH_SCALE, 0, 0 },
    { SP_CARD,   "Card",   TOUCH_SCALE, 0, 0 },
    { SP_BAG,    "Icon32", ITEM_SCALE,  1, "Ring2" },
    { SP_BIBLE,  "Icon32", ITEM_SCALE,  1, "Ring1" },
    { SP_DIE,    "Icon32", ITEM_SCALE,  1, "Ring0" },
    { SP_MARKED, "Icon32", ITEM_SCALE,  0, 0 },
};

/*
 * A second player - Esau beside Jacob - has a row of his own, and it is two
 * slots shorter: the bomb is shared and drawn once, and the Marked button is
 * ours and belongs to the player the screen controls (marked.c: 0x85fa3c), who
 * is Jacob. So the first row is the widest the game can build, and the card is
 * built around it.
 */
static const struct slot ROW_SECOND[] = {
    { SP_PILL,  "Icon64", TOUCH_SCALE, 0, 0 },
    { SP_CARD,  "Card",   TOUCH_SCALE, 0, 0 },
    { SP_BAG,   "Icon32", ITEM_SCALE,  1, "Ring2" },
    { SP_BIBLE, "Icon32", ITEM_SCALE,  1, "Ring1" },
    { SP_DIE,   "Icon32", ITEM_SCALE,  1, "Ring0" },
};

/*
 * AT THE RUN'S OPACITY. The run draws a slot into its 72 x 64 target whole and
 * then the target at the touch controls' opacity, once; the editor draws the
 * pieces one by one, so a piece over another would show what lies under it -
 * the four white silhouettes of the charged outline, under the item, turned
 * the whole item pale. So the outline is drawn as its ring alone, outside the
 * picture (RingN, tools/editor_tools_sheet.py from the same three items), and
 * nothing in the slot lies over anything else but the bar's left edge over the
 * item's right one, a few texels.
 */
static void draw_row(const struct slot *row, int n, float right, float y,
                     float sw, float gap, float f, float alpha) {
    for (int i = 0; i < n; i++) {
        float cx = right - sw * 0.5f - (float) i * (sw + gap);
        float s = f * row[i].scale;
        if (row[i].charged) draw_sprite(SP_TOOLS, row[i].ring, cx, y, f, f, alpha);
        draw_sprite(row[i].sprite, row[i].animation, cx, y, s, s, alpha);
        if (row[i].charged) draw_sprite(SP_BAR, "Bar", cx, y, f, f, alpha);
    }
}

/* the grid: whole cells across the canvas, the step places snap to */
static void draw_grid(float w, float h) {
    float sx = 0.0f, sy = 0.0f;
    if (!grid_steps(1, &sx, &sy) || !(sx > 0.0f) || !(sy > 0.0f)) return;
    int nx = (int) floorf(w / sx + 0.5f), ny = (int) floorf(h / sy + 0.5f);
    for (int i = 1; i < ny; i++)
        draw_rect_paint(0.0f, (float) i * sy, w, 0.5f, 0.16f, PAINT_BLACK);
    for (int i = 1; i < nx; i++)
        draw_rect_paint((float) i * sx, 0.0f, 0.5f, h, 0.16f, PAINT_BLACK);
}

/*
 * The editor's own buttons: an icon over its word, light with a black edge in
 * the manner of the EID font, on nothing at all (the plate went at the owner's
 * word, 13.09.2026; the icons and the words came with item 4, 23.09.2026).
 * Each language set has its own four animations, Grid0..Done6.
 *
 * What the drawing does not say by itself:
 *   the grid       stands full while it is on and faded while it is off
 *   a pressed one  fades and steps a texel down, the way a key goes in
 */
#define TOOL_FADED   0.45f
#define TOOL_PRESSED 0.55f

static void draw_tools(int held, int grid) {
    int set = tool_set();
    for (int i = 0; i < TOOL_COUNT; i++) {
        float cx = 0.0f, cy = 0.0f, bw = 0.0f, bh = 0.0f, scale = 0.0f;
        if (!tool_box(i, &cx, &cy, &bw, &bh, &scale)) return;
        float alpha = 1.0f;
        if (i == TOOL_GRID && !grid) alpha = TOOL_FADED;
        if (i == held) { alpha = TOOL_PRESSED; cy += scale; }
        static char name[TOOL_COUNT][16];                 /* lasting, like the literals it replaced */
        snprintf(name[i], sizeof(name[i]), "%s%d", TOOL_ANIMATION[i], set);
        draw_sprite(SP_TOOLS, name[i], cx, cy, scale, scale, alpha);
    }
}

/*
 * The slider: the track, its thick part up to the knob, the two end marks, the
 * knob, and under it the word and the value - each a picture of the tool sheet
 * at the buttons' scale, so a texel is a unit and nothing is drawn between
 * texels. The knob stands on whole texels of the track; held, it fades and
 * steps a texel down, as a pressed button does.
 */
static void draw_slider(int held) {
    float x0 = 0.0f, cy = 0.0f, k = 0.0f;
    if (!slider_frame(&x0, &cy, &k)) return;
    int set = tool_set();
    int pct = (int) lroundf(shim_sticks_alpha() * 100.0f);
    if (pct < SLIDER_PCT_LOW) pct = SLIDER_PCT_LOW;
    if (pct > SLIDER_PCT_HIGH) pct = SLIDER_PCT_HIGH;
    pct = SLIDER_PCT_LOW + ((pct - SLIDER_PCT_LOW + SLIDER_PCT_STEP / 2) / SLIDER_PCT_STEP) * SLIDER_PCT_STEP;
    float t = (float) (pct - SLIDER_PCT_LOW) / (float) (SLIDER_PCT_HIGH - SLIDER_PCT_LOW);
    float fill = floorf(t * SLIDER_TRACK + 0.5f);              /* whole texels */
    float x1 = x0 + SLIDER_TRACK * k;
    draw_sprite(SP_TOOLS, "SliderLow", x0 - SLIDER_END_AT * k, cy, k, k, 1.0f);
    draw_sprite(SP_TOOLS, "SliderHigh", x1 + SLIDER_END_AT * k, cy, k, k, 1.0f);
    draw_sprite(SP_TOOLS, "SliderTrack", x0, cy, k, k, 1.0f);
    if (fill > 0.0f) {
        draw_sprite(SP_TOOLS, "SliderFillCap", x0, cy, k, k, 1.0f);
        draw_sprite(SP_TOOLS, "SliderFill", x0, cy, fill * k, k, 1.0f);
    }
    draw_sprite(SP_TOOLS, "SliderKnob", x0 + fill * k, cy + (held ? k : 0.0f), k, k,
                held ? TOOL_PRESSED : 1.0f);

    int v = (pct - SLIDER_PCT_LOW) / SLIDER_PCT_STEP;
    float wide = OPACITY_W[set] + SLIDER_WORD_GAP + PCT_W[v];
    float lx = floorf(x0 + SLIDER_TRACK * 0.5f * k - wide * 0.5f * k + 0.5f);
    float ly = cy + SLIDER_LABEL * k;
    static char word[TOOL_SETS][12], value[(SLIDER_PCT_HIGH - SLIDER_PCT_LOW) / SLIDER_PCT_STEP + 1][8];
    snprintf(word[set], sizeof(word[set]), "Opacity%d", set);  /* lasting, as the tools' names are */
    snprintf(value[v], sizeof(value[v]), "Pct%d", pct);
    draw_sprite(SP_TOOLS, word[set], lx, ly, k, k, 1.0f);
    draw_sprite(SP_TOOLS, value[v], lx + (OPACITY_W[set] + SLIDER_WORD_GAP) * k, ly, k, k, 1.0f);
}

/* the overlay's draw (hook O), every frame while the editor is up */
void shim_touchedit_draw(void) {
    pthread_mutex_lock(&g_lock);
    int open = g_open, held = g_held, grid = g_grid, sliding = g_slider;
    pthread_mutex_unlock(&g_lock);
    if (!open) return;
    /* everything the run draws at the touch controls' opacity, the editor draws so too */
    float a = shim_touch_alpha_live();
    unsigned long b = shim_image_base();
    if (!b) return;

    float w = shim_screen_width(), h = shim_screen_height();
    if (!(w > 0.0f) || !(h > 0.0f)) return;

    /*
     * How far the camera has come of its own way here. WHICH WAY IT GOES IS NOT
     * ASSUMED: the anchor is moved down and the camera arrives from above, so
     * the travel was negative and the editor never showed itself at all
     * (13.09.2026). What matters is the distance covered, not its sign.
     */
    if (g_menu) {
        /*
         * AGAINST THE DISTANCE THERE ACTUALLY IS, not against AWAY_Y.
         *
         * Dividing by the full 2000 assumes the camera starts at home. Open the
         * editor while it is still on its way back from the last time and it
         * has less than that to go: the share tops out below one, and the fade
         * - which only begins at nine tenths and is stretched over the last
         * tenth - sticks halfway. The editor then stands half see-through for
         * good, and the faster the owner tapped the fainter it was (14.09.2026).
         *
         * So: how far is LEFT, against how far there was at the opening. The
         * camera settles at the anchor plus halfExtra, and halfExtra is nought
         * on a canvas 270 units tall, so the anchor is the mark to measure to.
         */
        float now = ((const float *) (g_menu + MENU_CAMERA))[1];
        float left = fabsf(g_target_y - now);
        float part = g_travel > 1.0f ? 1.0f - left / g_travel : 1.0f;
        if (part < 0.0f) part = 0.0f;
        float came = fabsf(now - g_camera_was);
        g_fade = (part - EDITOR_SHOWS) / (1.0f - EDITOR_SHOWS);
        if (g_fade < 0.0f) g_fade = 0.0f;
        if (g_fade > 1.0f) g_fade = 1.0f;
        if (shim_report_active()) {
            static int said;
            if (said < 6) {
                said++;
                char line[160];
                snprintf(line, sizeof(line), "camera %.0f -> %.0f of %.0f, shown %.2f",
                         (double) g_camera_was,
                         (double) ((const float *) (g_menu + MENU_CAMERA))[1],
                         (double) AWAY_Y, (double) g_fade);
                report_line("editor", line);
            }
        }
    }
    if (!(g_fade > 0.0f)) return;

    if (grid) draw_grid(w, h);

    /* the field is halved: each stick keeps to its own side */
    draw_rect(w * 0.5f - 0.5f, 0.0f, 1.0f, h, 0.35f);

    float right = 0.0f, middle = 0.0f, sw = 0.0f, sh = 0.0f, gap = 0.0f;
    card_now(b, &right, &middle, &sw, &sh, &gap);
    if (!(sw > 0.0f)) return;
    float f = sw / 72.0f;

    draw_row(ROW_FIRST, (int) (sizeof(ROW_FIRST) / sizeof(ROW_FIRST[0])), right, middle, sw, gap, f, a);
    draw_row(ROW_SECOND, (int) (sizeof(ROW_SECOND) / sizeof(ROW_SECOND[0])), right, middle + sh,
             sw, gap, f, a);

    /*
     * The sticks, by the game's own measure: its draw takes the picture at
     * 0.5 / [0xa9c7c8] - f - narrowed on each axis by how much of the window
     * the picture fills (0x861ec8..0x861f30). That narrowing belongs to the
     * WINDOW's viewport the run draws them in, where a unit is the window over
     * the canvas: it gives a stick the same pixels a unit of f has in the
     * picture. Here the viewport is the picture already, so the stick is drawn
     * at f itself, and it comes out the size the run shows - with FULLSCREEN on
     * the two numbers are one; with it off, the narrowing drew them small.
     * (Half of f was what once made them look tiny.)
     */
    float stick = f;

    float mx = 0.0f, my = 0.0f, tx = 0.0f, ty = 0.0f;
    if (shim_sticks_static()) {
        seat_shown(1, &mx, &my);
        seat_shown(0, &tx, &ty);
        /* the D-pads, when TOUCH MODE is BUTTONS: what the run shows is what is placed */
        int shim_sticks_buttons(void);
        /* the ring keeps its own AlphaTint under ours, as in the run (0x861f60) */
        if (shim_sticks_buttons()) {
            draw_sprite(SP_STICKS, "Analog DPad Move", mx, my, stick, stick, a);
            draw_sprite(SP_STICKS, "Analog DPad Shoot", tx, ty, stick, stick, a);
        } else {
            draw_sprite(SP_STICKS, "Analog Bounds", mx, my, stick, stick, a);
            draw_sprite(SP_STICKS, "Analog Stick Move", mx, my, stick, stick, a);
            draw_sprite(SP_STICKS, "Analog Bounds", tx, ty, stick, stick, a);
            draw_sprite(SP_STICKS, "Analog Stick Shoot", tx, ty, stick, stick, a);
        }
    }

    /*
     * The twins' round button, the same picture the overlay puts in it: the
     * Button draws its 72 x 64 target into its box, and the picture goes in at
     * a scale of one, so a texel of it is f on the screen.
     */
    float tx2 = 0.0f, ty2 = 0.0f;
    if (twin_rect(0, &tx2, &ty2))
        draw_sprite(SP_STICKS, "Analog Stick Twin Freeze", tx2, ty2, f, f, a);

    /* the pause button, the same way: its 50-texel picture into the target at one, at f */
    float px2 = 0.0f, py2 = 0.0f;
    if (pause_rect(0, &px2, &py2))
        draw_sprite(SP_STICKS, "Pause Button", px2, py2, f, f, a);

    /* the twins' trinket button, four trinkets on it, back to front as the run lays them */
    int at[TRINKET_SHOWN * 2];
    if (trinket_rect(0, 0, 0, at))
        for (int i = TRINKET_SHOWN - 1; i >= 0; i--) {
            float ix = (float) at[i * 2] + 16.0f, iy = (float) at[i * 2 + 1] + 16.0f;   /* Icon32: pivot 16,16 */
            draw_tinted(SP_TRINKET0 + i, "Icon32", ix + TRINKET_SHADOW_AT, iy + TRINKET_SHADOW_AT,
                        1.0f, 1.0f, TRINKET_SHADOW * a, PAINT_BLACK);
            draw_sprite(SP_TRINKET0 + i, "Icon32", ix, iy, 1.0f, 1.0f, a);
        }

    draw_tools(held, grid);
    draw_slider(sliding);

    static int told;
    if (!told && shim_report_active()) {
        told = 1;
        char line[220];
        snprintf(line, sizeof(line),
                 "drawn: canvas %.0f x %.0f, slot %.1f x %.1f gap %.1f, f %.3f, stick scale %.3f,"
                 " card right %.1f middle %.1f, sticks %.0f,%.0f and %.0f,%.0f, pause %.0f,%.0f,"
                 " opacity %.2f",
                 (double) w, (double) h, (double) sw, (double) sh, (double) gap, (double) f,
                 (double) stick, (double) right, (double) middle,
                 (double) mx, (double) my, (double) tx, (double) ty, (double) px2, (double) py2,
                 (double) a);
        report_line("editor", line);
    }
}

/* the editor's own pictures, into the resource table beside the game's */
void shim_touchedit_open_assets(void *manager) {
    if (!manager) return;
    shim_asset_bring_in(manager, "port_touch_editor.anm2", "gfx", "port_touch_editor.anm2");
    shim_asset_bring_in(manager, "port_white.png", "gfx", "port_white.png");
    shim_asset_bring_in(manager, "port_touch_tools.png", "gfx", "port_touch_tools.png");
    LOG("editor: the pictures are in the resource table");
}
