/*
 * THE PORT'S STRAPS AS MENU ITEMS.
 *
 * Three straps stick out from under the menus' papers: Port Credits on the
 * title paper, Tweaks on the settings paper, Item Spawn on the pause paper
 * (while the spawner is switched on). A finger taps them (credits.c, tweaks.c,
 * spawner.c); from a pad or a keyboard each is an item of its list, standing in
 * the list's ring between the last item and the first (the owner, 17 and
 * 18.09.2026):
 *
 *   Up on the first item       the strap is chosen - it slides out
 *   Down on the last item      the strap is chosen as well
 *   Down on the strap          on to the first item
 *   Up on the strap            on to the last item
 *   Confirm on the strap       its page or card opens
 *   Back on the strap          what Back does on that screen
 *
 * The lists wrap by themselves (title 0x7f9070, settings 0x6e31a0, pause
 * 0x8a18f4), so leaving the strap is half the list's own move: the press that
 * leaves it the way the list would wrap anyway is handed on, and the press that
 * leaves it back towards the item it was entered from is kept - the list is
 * still standing on that item.
 *
 * THE PICTURES are layers of the menus' own .anm2 files, under the paper, one
 * per whole pixel of the slide (tools/strap_layers.py). The strap slides out
 * and back through them a pixel every STEP_MS (it jumped - the owner,
 * 18.09.2026). Which one shows is a sprite's own per-layer flag, the same one
 * the game turns off for a cursor (layer record 0x89e710(sprite, layer) +0x7c):
 * set every frame, because a reloaded sprite comes back with every layer shown.
 * While a strap is chosen the list's own cursor is hidden the same way - the
 * title's layer 6, the pause overlay's layer 2 - and on the settings list,
 * which writes its cursor flag afresh for every row (0x6e3fa8, cset w25, eq),
 * that one word answers "not this row" instead for as long as the strap is
 * chosen.
 *
 * The pause screen draws its layers by number, 0 to 7 (0x8a23b4), so its strap
 * layers are drawn from the layer render itself (0x89e550, trace mark '4'):
 * just before layer 1, the paper, the strap layer is drawn with the same place.
 *
 * TWO STRAPS ON THE SETTINGS PAPER (25.09.2026): GUIDE hangs under TWEAKS
 * (tools/guide_strap.py) and stands after it in the ring - last item, TWEAKS,
 * GUIDE, first item. Down walks that way, Up back; the list itself is left on
 * the item the ring was entered from, so leaving towards it keeps the press and
 * leaving the other way hands it on for the list to wrap.
 *
 * THE PORT'S PAGES. While the credits page or the Tweaks page is open, every
 * press is the page's: the list under it walked its rows and moved the anchor
 * the Tweaks page borrows, so the camera flew off into the empty world, and
 * Back reached the title under the credits and left it for the save screen
 * (the owner, 18.09.2026). The Tweaks page is walked from here
 * (tweaks.c, shim_tweaks_pad); Back closes either page.
 *
 * THE LISTS, all measured in the image:
 *
 *   title     menu screen 3, object menu+0x81220: +4 the item (0..4, Up wraps),
 *             +0x10 its sprite (gamemenu.anm2)
 *   settings  menu screen 10, list menu+0x10eed8: +0xc 1 inside the pause,
 *             +0x10 the row (Up wraps), +0x30 its sprite (optionsmenu.anm2),
 *             +0x2f8..+0x300 one int per row; the pause's own copy of the list
 *             at pause+0x809b0
 *   pause     Game+0x10dd20: +0x2b8 state (1 the list), +0x2bc the item (0..2,
 *             Up wraps), +0x2e0 its sprite (pausescreen.anm2)
 *
 * THE KEYS AND THE PAD. Choosing is done before the game sees the press: a new
 * pad state is judged here before pad.c stores it, and the pad is muted for the
 * game from the press that the port takes until everything is let go; keyboard
 * presses the port takes are kept from the game (objc.c).
 */
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <android/log.h>
#include "diag.h"
#include "pages.h"

#if ISAAC_DIAG
#define LOG(...) __android_log_print(ANDROID_LOG_INFO, "isaac", __VA_ARGS__)
#else
#define LOG(...) do { } while (0)
#endif

unsigned long shim_image_base(void);
int shim_sticks_in_game(void);
int shim_tweaks_spawner(void);
int shim_tweaks_open(void);
void shim_tweaks_show(int on);
void shim_guide_show(int on);
int shim_credits_open(void);
void shim_credits_show(int on);
int shim_spawner_is_open(void);
void shim_spawner_open(void);
int shim_patch_word(unsigned long off, uint32_t was_a, uint32_t was_b, uint32_t now);
void shim_pad_mute(int on);

#define MENU_PTR          0xac2490
#define GAME_PTR          0xac3b90
#define MENU_SCREEN       0x38
#define SCREEN_TITLE      3
#define SCREEN_SETTINGS   10
#define TITLE_OBJECT      0x81220
#define TITLE_ITEM        0x4
#define TITLE_SPRITE      0x10
#define TITLE_LAST        5                     /* 0x7f9070: past 5 it wraps to 0 - six items with MODS (loader.c) */
#define SETTINGS_LIST     0x10eed8
#define LIST_IN_PAUSE     0xc
#define LIST_ROW          0x10
#define LIST_SPRITE       0x30
#define LIST_TILE         0x1c                  /* where the list is drawn from, less the camera (0x6e3b48) */
#define LIST_KINDS        0x2f8                 /* a vector of ints, one per row (0x6e3190) */
#define LIST_KINDS_END    0x300
#define GAME_PAUSE        0x10dd20
#define PAUSE_STATE       0x2b8
#define PAUSE_ITEM        0x2bc
#define PAUSE_SPRITE      0x2e0
#define PAUSE_LIST        0x809b0
#define PAUSE_LAST        2                     /* 0x8a18f4: past 2 it wraps to 0 */

#define FN_LAYER          0x89e710              /* (sprite, layer) -> the layer record */
#define FN_LAYER_RENDER   0x89e550              /* (sprite, layer, place, clamp, clamp) */
#define LAYER_SHOWN       0x7c
#define SPRITE_LOADED     0x149
#define SPRITE_LAYERS     0xb0
/*
 * The sprite's own copy of its animations (0x89e8b4 finds one by name): a
 * vector of 0x150-byte records, the name a std::string at +0. An animation's
 * layer records are 0x18 bytes, the layer's id at +0 and the .anm2's Visible
 * at +0x14 (the parser, 0x619a88; the layer draw 0x89d5d4 checks it).
 */
#define SPRITE_ANIMS      0x98
#define SPRITE_ANIM_COUNT 0xa0
#define ANIM_BYTES        0x150
#define ANIM_LAYERS       0x18
#define ANIM_LAYER_COUNT  0x20
#define LAYER_ANIM_BYTES  0x18
#define LAYER_ANIM_SHOWN  0x14

/*
 * The strap layers - STEPS of them each, in a row - are the port's patches over
 * the menus' .anm2 (portpatch.c, plan 30.09 M8), and the cursors and the pause
 * paper are the game's own layers. None of the numbers is assumed: each is read
 * by name from the file the game loads (shim_port_layer) once, at start. A strap
 * whose layers are missing or not in a row (a mod's .anm2 the patch could not
 * take) is simply not drawn; portpatch.c has said why in the report.
 * In the port's own files: title strap 12, cursor 6; settings strap 20, guide
 * strap 26; pause strap 8, cursor 2, paper 1.
 */
#define STEPS             6
int shim_port_layer(const char *rel, const char *name);
#define TITLE_ANM2        "gfx/ui/main menu/gamemenu.anm2"
#define OPTIONS_ANM2      "gfx/ui/main menu/optionsmenu.anm2"
#define PAUSE_ANM2        "gfx/ui/pausescreen.anm2"
static int g_layers_known;
static int g_title_strap = -1, g_title_cursor = -1, g_settings_strap = -1, g_guide_strap = -1,
           g_pause_strap = -1, g_pause_cursor = -1, g_pause_paper = -1;
#define TITLE_STRAP       g_title_strap
#define TITLE_CURSOR      g_title_cursor
#define SETTINGS_STRAP    g_settings_strap
#define GUIDE_STRAP       g_guide_strap                    /* after the TWEAKS strap's six */
#define PAUSE_STRAP       g_pause_strap
#define PAUSE_CURSOR      g_pause_cursor
#define PAUSE_PAPER       g_pause_paper

#define STEP_MS           25                    /* one pixel of the slide */

#define MENU_ANCHORS      0x10fc08              /* a Vector2 per screen */


#define ROW_CURSOR_AT     0x6e3fa8
#define ROW_CURSOR_GAME   0x1A9F17F9u           /* cset w25, eq */
#define ROW_CURSOR_NEVER  0x52800019u           /* mov  w25, #0 */

enum { F_NONE, F_TITLE, F_SETTINGS, F_PAUSE, F_GUIDE, F_COUNT };
enum { FROM_TOP, FROM_BOTTOM };

static int g_focus;
static int g_entered;                   /* the end of the list the strap was entered from */
static int g_held;                      /* the pad stays muted until it is let go */
static int g_last_vertical, g_last_horizontal, g_last_a, g_last_b;
static int g_row_patched;
static int g_slide[F_COUNT];            /* whole pixels out, per strap */
static long long g_slide_ms[F_COUNT];   /* when it last moved a pixel */

/* the list a strap belongs to: GUIDE is the settings list's second strap */
static int list_of(int focus) { return focus == F_GUIDE ? F_SETTINGS : focus; }
static int on_settings_strap(void) { return g_focus == F_SETTINGS || g_focus == F_GUIDE; }

static unsigned long menu_now(unsigned long b) { return *(const unsigned long *) (b + MENU_PTR); }
static unsigned long game_now(unsigned long b) { return *(const unsigned long *) (b + GAME_PTR); }

static long long now_ms(void) {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (long long) t.tv_sec * 1000 + t.tv_nsec / 1000000;
}

/* which list is up and could carry a strap, or F_NONE */
static int context(unsigned long b) {
    if (shim_sticks_in_game()) {
        unsigned long game = game_now(b);
        if (game && shim_tweaks_spawner() && !shim_spawner_is_open()
            && *(const int *) (game + GAME_PAUSE + PAUSE_STATE) == 1)
            return F_PAUSE;
        return F_NONE;
    }
    unsigned long menu = menu_now(b);
    if (!menu) return F_NONE;
    int screen = *(const int *) (menu + MENU_SCREEN);
    if (screen == SCREEN_TITLE && !shim_credits_open() && !shim_tweaks_open()) return F_TITLE;
    if (screen == SCREEN_SETTINGS && !shim_tweaks_open()
        && !*(const unsigned char *) (menu + SETTINGS_LIST + LIST_IN_PAUSE))
        return F_SETTINGS;
    return F_NONE;
}

/* the item the list stands on, and its last one */
static int item_now(unsigned long b, int ctx) {
    if (ctx == F_TITLE) return *(const int *) (menu_now(b) + TITLE_OBJECT + TITLE_ITEM);
    if (ctx == F_SETTINGS) return *(const int *) (menu_now(b) + SETTINGS_LIST + LIST_ROW);
    if (ctx == F_PAUSE) return *(const int *) (game_now(b) + GAME_PAUSE + PAUSE_ITEM);
    return -1;
}

static int item_last(unsigned long b, int ctx) {
    if (ctx == F_TITLE) return TITLE_LAST;
    if (ctx == F_PAUSE) return PAUSE_LAST;
    if (ctx == F_SETTINGS) {
        unsigned long list = menu_now(b) + SETTINGS_LIST;
        unsigned long from = *(const unsigned long *) (list + LIST_KINDS);
        unsigned long to = *(const unsigned long *) (list + LIST_KINDS_END);
        return to > from ? (int) ((to - from) / 4) - 1 : -1;
    }
    return -1;
}

static void open_for(int ctx) {
    if (ctx == F_TITLE) shim_credits_show(1);
    else if (ctx == F_SETTINGS) shim_tweaks_show(1);
    else if (ctx == F_GUIDE) shim_guide_show(1);
    else if (ctx == F_PAUSE) shim_spawner_open();
}

void report_line(const char *kind, const char *text);

/* the first of a strap's STEPS layers <prefix>0..5, which must stand in a row; -1 otherwise */
static int strap_run(const char *rel, const char *prefix) {
    int first = -1;
    for (int k = 0; k < STEPS; k++) {
        char name[32];
        snprintf(name, sizeof(name), "%s%d", prefix, k);
        int id = shim_port_layer(rel, name);
        if (id < 0) return -1;
        if (k == 0) first = id;
        else if (id != first + k) {
            char line[200];
            snprintf(line, sizeof(line), "%s: %s%d is layer %d, not %d - the strap is not drawn", rel, prefix, k, id,
                     first + k);
            report_line("port layer", line);
            return -1;
        }
    }
    return first;
}

/* the numbers, read once the pack and the port's patches are in (loader.c setAssets) */
void shim_straps_layers(void) {
    if (g_layers_known) return;
    g_layers_known = 1;
    g_title_strap = strap_run(TITLE_ANM2, "PortStrap");
    g_title_cursor = shim_port_layer(TITLE_ANM2, "Cursor");
    g_settings_strap = strap_run(OPTIONS_ANM2, "PortStrap");
    g_guide_strap = strap_run(OPTIONS_ANM2, "GuideStrap");
    g_pause_strap = strap_run(PAUSE_ANM2, "PortStrap");
    g_pause_cursor = shim_port_layer(PAUSE_ANM2, "Cursor");
    g_pause_paper = shim_port_layer(PAUSE_ANM2, "Paper");
    LOG("straps: layers title %d/%d, settings %d, guide %d, pause %d/%d/%d", g_title_strap, g_title_cursor,
        g_settings_strap, g_guide_strap, g_pause_strap, g_pause_cursor, g_pause_paper);
}

/* the pause paper's layer while the ITEM SPAWN strap is drawn with it, else -1 (spawner.c answers its tap) */
int shim_straps_pause_paper(void) { return g_pause_strap >= 0 ? g_pause_paper : -1; }

/* is the title strap - the PORT CREDITS plaque - drawn at all (credits.c answers its tap) */
int shim_straps_title_drawn(void) { return g_title_strap >= 0; }

/* one layer's own shown flag on one sprite, if the sprite has that layer */
static void show_layer(unsigned long b, unsigned long sprite, int layer, int shown) {
    if (layer < 0) return;
    if (!sprite || !*(const unsigned char *) (sprite + SPRITE_LOADED)) return;
    if (layer >= *(const int *) (sprite + SPRITE_LAYERS)) return;
    unsigned char *record = ((unsigned char *(*)(unsigned long, int)) (b + FN_LAYER))(sprite, layer);
    if (record) record[LAYER_SHOWN] = (unsigned char) (shown != 0);
}

static const char *string_bytes(const unsigned char *s);

/*
 * THE SAME CHOICE IN THE ANIMATION ITSELF. Changing the language reloads the
 * settings sprite, and a reloaded sprite comes back with every layer's own flag
 * set: until the next between-frames step all six steps of each strap were
 * drawn, the slid-out one on top - the straps slid out and back at every
 * change of language (the owner, 25.09.2026). So the .anm2 shows only the
 * step in place (tools/guide_strap.py, at_rest) and the chosen step is turned
 * on here, in the sprite's Paper13: a reload comes up at rest.
 */
static void show_strap_in(unsigned long sprite, const char *animation, int first, int out) {
    if (first < 0) return;
    if (!sprite || !*(const unsigned char *) (sprite + SPRITE_LOADED)) return;
    const unsigned char *anims = *(const unsigned char *const *) (sprite + SPRITE_ANIMS);
    int count = *(const int *) (sprite + SPRITE_ANIM_COUNT);
    if (!anims || count <= 0 || count > 256) return;
    for (int i = 0; i < count; i++) {
        const unsigned char *anim = anims + (unsigned long) i * ANIM_BYTES;
        if (strcmp(string_bytes(anim), animation)) continue;
        unsigned char *layers = *(unsigned char *const *) (anim + ANIM_LAYERS);
        unsigned n = *(const unsigned *) (anim + ANIM_LAYER_COUNT);
        if (!layers || n > 256) return;
        for (unsigned k = 0; k < n; k++) {
            unsigned char *rec = layers + (unsigned long) k * LAYER_ANIM_BYTES;
            int id = *(const int *) rec;
            if (id >= first && id < first + STEPS) rec[LAYER_ANIM_SHOWN] = (unsigned char) (id - first == out);
        }
        return;
    }
}

/* a strap's layers on a sprite: the one `out` pixels along, or none for -1 */
static void show_strap(unsigned long b, unsigned long sprite, int first, int out) {
    if (first < 0) return;
    for (int k = 0; k < STEPS; k++) show_layer(b, sprite, first + k, k == out);
}

/* a pixel every STEP_MS towards where the focus says each strap should be */
static void slide(void) {
    long long now = now_ms();
    for (int c = F_TITLE; c < F_COUNT; c++) {
        int want = g_focus == c ? STEPS - 1 : 0;
        if (g_slide[c] == want) { g_slide_ms[c] = now; continue; }
        while (g_slide[c] != want && now - g_slide_ms[c] >= STEP_MS) {
            g_slide[c] += want > g_slide[c] ? 1 : -1;
            g_slide_ms[c] += STEP_MS;
        }
    }
}

/*
 * PROBE, report builds only: the settings list's layers, once for the main
 * menu's list and once for the pause's. The owner sees the pause's list with
 * the main menu's paper since the strap layers went in (18.09.2026), and the
 * game makes the pause's paper dark by replacing every layer's sheet that is
 * OptionsMenu.png with OptionsMenuDark.png at construction (0x6e2748) and
 * hiding the sketches 6..10 (0x6e26e8). Each layer's sheet path (record +0x10,
 * a std::string) and shown flag say which of that happened.
 */
int shim_report_active(void);
void report_line(const char *kind, const char *text);

static const char *string_bytes(const unsigned char *s) {
    return (s[23] & 0x80) ? *(const char *const *) s : (const char *) s;
}

static void layers_probe(unsigned long b, unsigned long sprite, const char *which) {
    if (!sprite || !*(const unsigned char *) (sprite + SPRITE_LOADED)) return;
    int count = *(const int *) (sprite + SPRITE_LAYERS);
    char line[400];
    int n = snprintf(line, sizeof(line), "%s list, %d layers:", which, count);
    for (int layer = 0; layer < count && layer < 32 && n > 0 && n < (int) sizeof(line); layer++) {
        const unsigned char *record = ((const unsigned char *(*)(unsigned long, int)) (b + FN_LAYER))(sprite, layer);
        if (!record) break;
        const char *path = string_bytes(record + 0x10);
        const char *leaf = strrchr(path, '/');
        n += snprintf(line + n, sizeof(line) - (size_t) n, " %d%s:%.14s", layer,
                      record[LAYER_SHOWN] ? "" : "-", leaf ? leaf + 1 : path);
    }
    report_line("straps", line);
}

static void pictures(unsigned long b) {
    unsigned long menu = menu_now(b), game = game_now(b);
    if (shim_report_active()) {
        static unsigned long said_menu, said_pause;
        unsigned long list = menu ? menu + SETTINGS_LIST + LIST_SPRITE : 0;
        if (list && list != said_menu && *(const unsigned char *) (list + SPRITE_LOADED)) {
            said_menu = list;
            layers_probe(b, list, "the menu's");
        }
        list = game ? game + GAME_PAUSE + PAUSE_LIST + LIST_SPRITE : 0;
        if (list && list != said_pause && *(const unsigned char *) (list + SPRITE_LOADED)) {
            said_pause = list;
            layers_probe(b, list, "the pause's");
        }
    }
    if (menu) {
        unsigned long title = menu + TITLE_OBJECT + TITLE_SPRITE;
        show_strap(b, title, TITLE_STRAP, g_slide[F_TITLE]);
        show_layer(b, title, TITLE_CURSOR, g_focus != F_TITLE);
        show_strap(b, menu + SETTINGS_LIST + LIST_SPRITE, SETTINGS_STRAP, g_slide[F_SETTINGS]);
        show_strap(b, menu + SETTINGS_LIST + LIST_SPRITE, GUIDE_STRAP, g_slide[F_GUIDE]);
        show_strap_in(menu + SETTINGS_LIST + LIST_SPRITE, "Paper13", SETTINGS_STRAP, g_slide[F_SETTINGS]);
        show_strap_in(menu + SETTINGS_LIST + LIST_SPRITE, "Paper13", GUIDE_STRAP, g_slide[F_GUIDE]);
    }
    if (game) {
        /* the settings list inside the pause has no Tweaks page to open */
        show_strap(b, game + GAME_PAUSE + PAUSE_LIST + LIST_SPRITE, SETTINGS_STRAP, -1);
        show_strap(b, game + GAME_PAUSE + PAUSE_LIST + LIST_SPRITE, GUIDE_STRAP, -1);
        show_layer(b, game + GAME_PAUSE + PAUSE_SPRITE, PAUSE_CURSOR, g_focus != F_PAUSE);
        /*
         * The pause's strap layers are OFF for the sprite's own renders - the
         * game draws the whole pause sprite again for its cursor at the chosen
         * item, and the strap went with it, a second strap jumping from item
         * to item under the paper (the owner, 18.09.2026). Only the layer
         * render hook below turns the one chosen layer on, for its own draw.
         */
        show_strap(b, game + GAME_PAUSE + PAUSE_SPRITE, PAUSE_STRAP, -1);
    }
    /*
     * A strap chosen from the bottom of the settings list: the list scrolled
     * down to reach its last row, and the strap hangs off the paper's head, so
     * the anchor goes back to the top and the camera follows it (the owner,
     * 18.09.2026: the cursor went to the strap and the camera stayed below).
     */
    if (on_settings_strap() && menu) {
        /*
         * The top is where the list itself stands, not -1080: on a canvas
         * taller than 16:9 the rows are spread and the settings tile is 4
         * steps down (1280 on 4:3), and the constant threw the paper 200 units
         * down on a tablet (the tap audit, 01.10.2026). The list's own place,
         * list+0x1c, is minus the unscrolled anchor on every canvas.
         */
        float *anchor = (float *) (menu + MENU_ANCHORS + (unsigned long) SCREEN_SETTINGS * 8);
        const float *tile = (const float *) (menu + SETTINGS_LIST + LIST_TILE);
        float top = -tile[1];
        if (anchor[1] != top) {
#if ISAAC_DIAG
            /*
             * M8 of the tap audit: the anchor as it was, as it is put, and the
             * tile in the grid's row steps (screen 8 stands four rows up) - 4
             * steps on every canvas if the tile is what the audit read it to be.
             */
            static int said;
            if (said < 8 && shim_report_active()) {
                said++;
                float rows = -((const float *) (menu + MENU_ANCHORS + 8 * 8))[1] / 4.0f;
                char line[160];
                snprintf(line, sizeof(line), "pad strap: anchor[10] was %.1f, set to %.1f; tile %.1f = %.2f row steps of %.1f",
                         (double) anchor[1], (double) top, (double) tile[1],
                         rows > 0.0f ? (double) (tile[1] / rows) : 0.0, (double) rows);
                report_line("straps", line);
            }
#endif
            anchor[1] = top;
        }
    }
    int want = on_settings_strap();
    if (want != g_row_patched
        && shim_patch_word(ROW_CURSOR_AT, ROW_CURSOR_GAME, ROW_CURSOR_NEVER,
                           want ? ROW_CURSOR_NEVER : ROW_CURSOR_GAME))
        g_row_patched = want;
}

/*
 * One move, from a pad or a keyboard alike. The answer says whether the press
 * is the port's (kept from the game) or the list's (handed on).
 */
static volatile int g_open_asked;           /* a context to open for, frame thread */
static volatile int g_close_asked;
static volatile int g_page_move;            /* a PAGE_ move for the Tweaks page */
static int g_pause_strap_drawn;             /* the pause strap went down this frame already */

static int page_open(void) { return shim_credits_open() || shim_tweaks_open(); }

static int move(unsigned long b, int ctx, int what) {
    if (page_open()) {
        /* the Tweaks and MODS pages play Back themselves: a mod's settings page
           goes back to the MODS list, not out (tweaks.c, back_here) */
        if (shim_tweaks_open()) g_page_move = what;
        else if (what == PAGE_BACK) g_close_asked = 1;
        return 1;                            /* the page's, every one */
    }
    if (!g_focus) {
        if (!ctx) return 0;
        int at = item_now(b, ctx), last = item_last(b, ctx);
        if (what == PAGE_UP && at == 0) {
            g_focus = ctx == F_SETTINGS ? F_GUIDE : ctx;      /* up from the first: the ring's last strap */
            g_entered = FROM_TOP;
            return 1;
        }
        if (what == PAGE_DOWN && last > 0 && at == last) {
            g_focus = ctx;
            g_entered = FROM_BOTTOM;
            return 1;
        }
        return 0;
    }
    switch (what) {
    case PAGE_DOWN:                          /* on to the first item */
        if (g_focus == F_SETTINGS) { g_focus = F_GUIDE; return 1; }     /* TWEAKS -> GUIDE */
        g_focus = F_NONE;
        return g_entered == FROM_TOP;        /* from the bottom the list wraps there itself */
    case PAGE_UP:                            /* on to the last item */
        if (g_focus == F_GUIDE) { g_focus = F_SETTINGS; return 1; }     /* GUIDE -> TWEAKS */
        g_focus = F_NONE;
        return g_entered == FROM_BOTTOM;
    case PAGE_CONFIRM:
        g_open_asked = g_focus;
        g_focus = F_NONE;
        return 1;
    case PAGE_BACK:
        g_focus = F_NONE;                    /* what Back does on that screen */
        return 0;
    }
    return 1;                                /* sideways on a strap is nothing */
}

/*
 * A new pad state, from Java, BEFORE the game can read it (pad.c calls this
 * first and stores the state after): decided here, the press that chooses a
 * strap is muted before the list could see it and wrap. Only flags are set on
 * this thread; opening a page is asked for and done between frames.
 */
void shim_straps_pad(int vertical, int horizontal, int a, int back, int anything) {
    unsigned long b = shim_image_base();
    if (!b) return;
    int ctx = context(b);
    if (g_focus && list_of(g_focus) != ctx) g_focus = F_NONE;

    int what = 0;
    if (vertical == 1 && g_last_vertical != 1) what = PAGE_UP;
    else if (vertical == -1 && g_last_vertical != -1) what = PAGE_DOWN;
    else if (horizontal == 1 && g_last_horizontal != 1) what = PAGE_RIGHT;
    else if (horizontal == -1 && g_last_horizontal != -1) what = PAGE_LEFT;
    else if (a && !g_last_a) what = PAGE_CONFIRM;
    else if (back && !g_last_b) what = PAGE_BACK;
    g_last_vertical = vertical;
    g_last_horizontal = horizontal;
    g_last_a = a;
    g_last_b = back;

    int page = page_open();
    if (what) g_held = move(b, ctx, what);
    if (page) g_held = 1;                    /* and on until it is let go after closing */
    if (g_held && !g_focus && !page && !anything) g_held = 0;
    shim_pad_mute(g_focus != F_NONE || g_held);
}

/* between frames (gl.c): what was asked, the slide and the pictures */
void shim_straps_between_frames(void) {
    unsigned long b = shim_image_base();
    if (!b) return;
    if (g_focus && list_of(g_focus) != context(b)) {
        g_focus = F_NONE;
        shim_pad_mute(g_held);
    }
    /*
     * PROBE, report builds: the settings list as it opens - where its anchor and
     * the camera stand and which row the cursor is on. The owner (27.09.2026):
     * "the camera in the settings menu starts somewhere lower now"; this says
     * whether the list is scrolled (anchor), the camera is elsewhere, or the
     * cursor simply sits on a lower row.
     */
    {
        int shim_report_active(void);
        void report_line(const char *, const char *);
        static int was_settings, said;
        int now = context(b) == F_SETTINGS;
        /*
         * THE LIST KEEPS ITS SCROLL - and that is the game's own design, left
         * alone (the owner, 27.09.2026: "if it was meant so, it stays"). The
         * game moves this anchor in three ways only: the list's init (0x6e1ae4,
         * row 0 -> the top), the cursor walking (0x6e3300 -> 0x769ea0: rows 0-2
         * the top, past 9 the bottom, a step apiece between) and a finger
         * dragging the paper (0x6e299c -> 0x769ef8). Nothing runs on entering
         * the screen, so a list dragged down by a finger is still down on the
         * next visit, the cursor on row 0 all the while - on the iPhone just
         * the same, its paper 420 tall on a 270 screen. A reset here was tried
         * and taken out. The probe below stays, report builds only.
         */
        if (now && !was_settings && said < 8 && shim_report_active()) {
            said++;
            unsigned long menu = menu_now(b);
            const float *anchor = (const float *) (menu + MENU_ANCHORS + (unsigned long) SCREEN_SETTINGS * 8);
            const float *camera = (const float *) (menu + 0x3c);
            /* the unscrolled top is minus the list's own place, as the pad strap puts it back (above) */
            const float *tile = (const float *) (menu + SETTINGS_LIST + LIST_TILE);
            char line[200];
            snprintf(line, sizeof(line),
                     "settings opened: anchor %.0f,%.0f (top is %.0f), camera %.0f,%.0f, row %d of %d, focus %d",
                     (double) anchor[0], (double) anchor[1], (double) -tile[1],
                     (double) camera[0], (double) camera[1],
                     item_now(b, F_SETTINGS), item_last(b, F_SETTINGS), g_focus);
            report_line("straps", line);
        }
        was_settings = now;
    }
    if (g_close_asked) { g_close_asked = 0; shim_credits_show(0); shim_tweaks_show(0); shim_guide_show(0); }
    int step = g_page_move;
    if (step) { g_page_move = 0; shim_tweaks_pad(step); }
    int asked = g_open_asked;
    if (asked) { g_open_asked = 0; open_for(asked); }
    slide();
    pictures(b);
    g_pause_strap_drawn = 0;
}

/*
 * A key from a keyboard, before the game (objc.c): answers 1 when the port
 * takes it. HID usages: 82 up, 81 down, 80 left, 79 right, 40, 0x9e and 88 Return,
 * 44 Space, 41 Escape. While a page of the port's is open every key is its.
 */
static unsigned char g_kept_key[256];

int shim_straps_key(int code, int down) {
    if (code < 0 || code > 255) return 0;
    if (!down) {
        int kept = g_kept_key[code];
        g_kept_key[code] = 0;
        return kept;                     /* the release of a press that was kept */
    }
    unsigned long b = shim_image_base();
    if (!b) return 0;
    int ctx = context(b);
    if (g_focus && list_of(g_focus) != ctx) g_focus = F_NONE;
    int what = code == 82 ? PAGE_UP : code == 81 ? PAGE_DOWN : code == 80 ? PAGE_LEFT
             : code == 79 ? PAGE_RIGHT : (code == 40 || code == 0x9e || code == 88 || code == 44) ? PAGE_CONFIRM
             : code == 41 ? PAGE_BACK : 0;
    int take = what ? move(b, ctx, what) : page_open();
    if (take) g_kept_key[code] = 1;
    return take;
}

#if ISAAC_DIAG
/*
 * M3 of the tap audit (report builds): once a canvas, the pause paper's place
 * as the game hands it to its layers (0x8a2348..0x8a236c: 0.6 W, 0.5 H), the
 * paper layer's frame, where that puts the ITEM SPAWN strap, and the box a
 * finger is judged against (spawner.c). The spawner places its box from the
 * overlay's draw, a frame behind this one, so while the paper still slides in
 * the two differ by a frame's slide: the line waits for them to meet, and
 * gives up after a second with MISMATCH.
 */
int shim_spawner_strap_rect(float box[4], float origin[2]);
#define FN_LAYER_FRAME_NOW 0x89e714     /* (sprite + 0x30, layer) -> the layer's frame now (spawner.c) */
#define STRAP_FROM_PAPER_X 94.0f        /* the strap's top-left from the paper's place (spawner.c) */
#define STRAP_FROM_PAPER_Y -3.0f

static void pause_strap_note(unsigned long b, unsigned long sprite, unsigned long place) {
    static float said_w, said_h;
    static int lines, tries;
    if (lines >= 8 || !place || !shim_report_active() || PAUSE_PAPER < 0) return;
    const float *canvas = (const float *) (b + 0xac24d8);
    float W = canvas[0], H = canvas[1];
    if (W == said_w && H == said_h) return;
    float box[4], origin[2];
    if (!shim_spawner_strap_rect(box, origin)) return;
    const float *p = (const float *) place;
    const float *frame = ((const float *(*)(unsigned long, int)) (b + FN_LAYER_FRAME_NOW))(sprite + 0x30, PAUSE_PAPER);
    float fx = frame ? frame[4] : 0.0f, fy = frame ? frame[5] : 0.0f;
    float drawn_x = p[0] + fx + STRAP_FROM_PAPER_X, drawn_y = p[1] + fy + STRAP_FROM_PAPER_Y;
    float dx = origin[0] - drawn_x, dy = origin[1] - drawn_y;
    int ok = fabsf(dx) <= 1.0f && fabsf(dy) <= 1.0f;
    if (!ok && ++tries < 60) return;
    said_w = W; said_h = H; tries = 0;
    lines++;
    char line[300];
    snprintf(line, sizeof(line),
             "pause strap: canvas %.0fx%.0f place %.1f,%.1f (0.6W,0.5H = %.1f,%.1f) frame %.1f,%.1f; drawn from "
             "%.1f,%.1f; tap box %.1f..%.1f x %.1f..%.1f from %.1f,%.1f; %s %.1f,%.1f",
             (double) W, (double) H, (double) p[0], (double) p[1], (double) (W * 0.6f), (double) (H * 0.5f),
             (double) fx, (double) fy, (double) drawn_x, (double) drawn_y,
             (double) box[0], (double) box[2], (double) box[1], (double) box[3],
             (double) origin[0], (double) origin[1], ok ? "OK" : "MISMATCH", (double) dx, (double) dy);
    report_line("straps", line);
}
#endif

/*
 * The pause screen's layer render (0x89e550, trace mark '4'): the moment its
 * paper is about to be drawn, the strap goes down first, at the same place -
 * its frames copy the paper's, so it slides in and pops with it.
 */
void shim_straps_layer(unsigned long sprite, unsigned long layer, unsigned long place,
                       unsigned long clamp_a, unsigned long clamp_b) {
#if ISAAC_DIAG
    /* the pause list's draws, counted for the report (devices.c, batch 2 U3): layer 3 and the items' layer 6 */
    { void shim_devices_layer_drawn(unsigned long, unsigned long); shim_devices_layer_drawn(sprite, layer); }
#endif
    if (PAUSE_PAPER < 0 || layer != (unsigned long) PAUSE_PAPER) return;
    unsigned long b = shim_image_base();
    if (!b) return;
    unsigned long game = game_now(b);
    if (!game || sprite != game + GAME_PAUSE + PAUSE_SPRITE) return;
    if (!shim_tweaks_spawner() || *(const int *) (game + GAME_PAUSE + PAUSE_STATE) != 1) return;
    /*
     * ONCE A FRAME. The game renders the paper layer a second time each frame
     * at the chosen item's place (the cursor's road), and the strap went down
     * with that call too - a second strap under the paper, jumping from item
     * to item (the owner's screenshot, 19.09.2026). The paper's own call is
     * the first one after the frame's between-frames step.
     */
    if (g_pause_strap_drawn || PAUSE_STRAP < 0) return;
    g_pause_strap_drawn = 1;
    int which = PAUSE_STRAP + g_slide[F_PAUSE];
    show_layer(b, sprite, which, 1);
    ((void (*)(unsigned long, int, unsigned long, unsigned long, unsigned long)) (b + FN_LAYER_RENDER))(
        sprite, which, place, clamp_a, clamp_b);
    show_layer(b, sprite, which, 0);
#if ISAAC_DIAG
    pause_strap_note(b, sprite, place);
#endif
}
