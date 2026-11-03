/*
 * THE TWEAKS SCREEN, opened from a strap on the settings screen.
 *
 * THE RULE THIS FOLLOWS. Everything here is done the way the settings menu
 * already does it. A first build invented its own way of each thing and the
 * owner took it apart point by point (16.09.2026): a flat ruled band where the
 * paper's texture should be, a font that was not the settings rows' font, a
 * scroll that was raw where the menu's is slow and heavy, sliders that followed
 * the finger where the menu's do not, and a title the size of a row.
 *
 * THE PICTURE is built on the desk by tools/tweaks_page.py out of the settings
 * screen's own pieces and the tools that made the settings rows and the credits
 * headings; the page, the word sheet and tweaks_layout.h all come out of it.
 * The layout header is the one place every row's position is written, so the
 * rectangle a row is drawn in is the rectangle a finger is judged against.
 *
 * THE BEHAVIOUR is the settings list's own, read out of 0x6e2970 (the list's
 * touch update, every frame):
 *
 *   - A finger coming down on a row that is not highlighted highlights it. On
 *     the highlighted row it CONFIRMS it: a switch flips, a choice steps on,
 *     a slider is left as it is. This happens on touch-down, never on lift.
 *   - After ten points of travel (raw touch points, 0x6e2a8c) the gesture is
 *     decided for good: sideways if |dy| <= |dx|, otherwise up and down.
 *   - Sideways, every 30 units of travel is one step of the HIGHLIGHTED row,
 *     wherever the finger is (0x6e2cdc): a slider moves a tenth, a switch flips,
 *     a choice steps forwards or back. The step plays sound 0xED. The finger's
 *     x is never compared with the ticks.
 *   - Up and down, the finger's movement each frame is added to the screen's
 *     ANCHOR, one unit for one unit, and clamped (0x769ef8). There is no
 *     velocity and no inertia: the slow, heavy feel is the menu camera itself,
 *     which closes a tenth of the distance to the anchor every frame (0x768ad0).
 *     So this moves the anchor too, and the camera does the rest.
 *
 * TWO PAGES, ONE HAND (23.09.2026). The ported mods left this page for one of
 * their own, MODS, behind the MODS item the main menu got back (loader.c,
 * main_menu_mods). Both are this file: a page is which screen it hangs from,
 * which rows it has and which picture and layout it draws with (page_t).
 * Everything a row does is keyed by the row's own id, so a switch behaves the
 * same on whichever page it stands.
 *
 * WHERE IT IS. Opening moves the settings screen's anchor away, below the
 * whole menu world, and the camera travels there with the page-turn sound, as
 * the credits page does; closing puts the anchor back. The BACK and SELECT
 * plaques stay, because the game draws them on the glass. Nothing in the image
 * is patched.
 */
#include <math.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <android/log.h>

#include "diag.h"
#include "pages.h"
#include "tweaks_layout.h"
#include "modset.h"
#include "content.h"
#include "guide_layout.h"

#if ISAAC_DIAG
#define LOG(...) __android_log_print(ANDROID_LOG_INFO, "isaac", __VA_ARGS__)
#else
#define LOG(...) do { } while (0)
#endif

unsigned long shim_image_base(void);
int shim_sticks_in_game(void);
float shim_screen_width(void);
float shim_screen_height(void);
void shim_menu_page_sound(void);
int shim_report_active(void);
void report_line(const char *kind, const char *text);
void shim_str_assign(void *s, const char *text);
void shim_str_dtor(void *s);
void shim_asset_bring_in(void *mgr, const char *name, const char *folder, const char *leaf);

int shim_screen_shot_ceiling_get(void);
void shim_screen_shot_ceiling(int n);
int shim_screen_shot_rule(void);
int shim_screen_shot_enhanced(void);         /* screen.c: ENHANCED's multiplier on this screen */
float shim_sticks_dead_share(void);
float shim_sticks_sens(void);
float shim_sticks_sens_shoot(void);
void shim_sticks_set_deadzone(float v);
void shim_sticks_set_sens(float v);
void shim_sticks_set_sens_shoot(float v);
void shim_sticks_settings_save(void);
const char *shim_eid_language_pinned(void);
void shim_eid_language_pin(const char *code);
int shim_eid_language_count(void);
const char *shim_eid_language_code(int i);

/* the player's own mods (usermods.c) and the menu font (eid.c) */
int shim_usermods_count(void);
const char *shim_usermods_id(int i);
const char *shim_usermods_name(int i);
const char *shim_usermods_version(int i);
const char *shim_usermods_author(int i);
const char *shim_usermods_description(int i);
int shim_usermods_on(int i);
int shim_usermods_changed(void);
int shim_usermods_list_gen(void);                         /* grows when ADD MOD changes the list (hotfix 1.73) */
int shim_usermods_pending(int i);                          /* added in this run and on: works after the restart */
void shim_usermods_toggle(int i);
void shim_usermods_move(int i, int dir);
void shim_usermods_remove(int i);
/* what the page must say about the list and a mod (usermods.c, plan 02.10 A5) */
const char *shim_usermods_save_error(void);
int shim_usermods_list_unread(void);
const char *shim_usermods_crash(int i);
int shim_usermods_adapted(int i);
int shim_usermods_guard_fixed(int i);
int shim_usermods_guard_game(int i);
int shim_usermods_files(int i);
int shim_usermods_bare(int i);                             /* 1 nothing the port reads, 2 the same and a script mod */
int shim_usermods_guard_size(int i);
int shim_usermods_guard_bounds(int i, char *file, size_t fn, char *tag, size_t tn, char *id, size_t idn, long *lo, long *hi);
int shim_usermods_content(int i, content_sum_t *sum);
/* two mods as the list stands now (usermods.c, THE PAIRS): the red line, the details, the popup */
int shim_usermods_pairs_gen(void);
int shim_usermods_conflicts(int i, int *first);
int shim_usermods_pair_broken(int i, int j, int k, int *anm_mine, char *sheet, size_t sn, unsigned *sw, unsigned *sh,
                              unsigned *iw, unsigned *ih, int *anms, int *own);
int shim_usermods_pair_over(int i, int j, int *mine_over, int *files, unsigned *kinds, char *lang, size_t ln);
/* the run's own breakages (usermods.c, THE MODS OVER EACH OTHER): a sheet of the game's under a mod's own frames */
int shim_usermods_broken(int i, int k, int *anm_mod, char *anm_id, int *sheet_mod, char *sheet_id, size_t idn,
                         char *sheet, size_t sn, unsigned *sw, unsigned *sh, unsigned *iw, unsigned *ih,
                         int *anms, char *anm, size_t an);
void shim_picture_draw(unsigned char *sprite, const char *anim, int frame, float x, float y, float alpha);
/* a mod's own settings (usermods.c, plan 4.10 part 2) and what json.c said of its mod.json */
void shim_usermods_lang(int set);
int shim_usermods_settings(int i);
int shim_usermods_setting_kind(int i, int k);              /* 0 switch, 1 choice, 2 number */
const char *shim_usermods_setting_label(int i, int k);
int shim_usermods_setting_value(int i, int k);
int shim_usermods_setting_count(int i, int k);
const char *shim_usermods_setting_value_label(int i, int k, int v);
void shim_usermods_setting_range(int i, int k, int *min, int *max, int *step);
void shim_usermods_setting_step(int i, int k, int dir);
const char *shim_usermods_settings_error(void);
int shim_usermods_manifest_note(int i, int *broken, char *reason, size_t room);
void shim_menu_write(int which, const char *text, float x, float y, float r, float g, float b, float a);
float shim_menu_text_width(int which, const char *text);
void shim_port_action_text(int what, const char *text);     /* loader.c: 5 export a mod */

#define MENU_PTR        0xac2490
#define MENU_SCREEN     0x38
#define MENU_ANCHORS    0x10fc08
#define MENU_CAMERA     0x3c
#define SCREEN_OPTIONS  10          /* the settings screen, measured 16.09.2026 */
#define SCREEN_TITLE    3           /* the main menu's own screen (credits.c) */
#define MAIN_MENU       0x81220     /* the main menu object in the menu (0x768e08) */
#define MAIN_SELECTED   4           /* its highlighted item: 3 is MODS now */
#define ITEM_MODS       3
#define AREA            0xabec64
#define GAME_MANAGER    0xac2428
#define OPT_AIMLOCK     0x55f15
#define FN_PLAY_SOUND   0x671c8c    /* (manager, id, w2, w3, volume, pitch) */
#define SOUND_STEP      0xED        /* what the settings sliders play on a step */

#define AWAY_X          0.0f        /* below the whole menu world */
#define AWAY_Y      -2400.0f

/* the settings list's own gesture numbers, 0x6e2a8c and 0x6e2ce8 */
#define DECIDE_POINTS   10.0f       /* raw touch points before a gesture is chosen */
#define STEP_UNITS      30.0f       /* sideways travel for one step */

#define FN_SPRITE_NEW      0x89c16c
#define FN_SPRITE_LOAD     0x89fe24
#define FN_SPRITE_FRAME    0x89eab8
#define FN_SPRITE_RENDER   0x89e4c0
#define ZERO_VECTOR        0x95e7a0
#define SPRITE_BYTES       0x158
#define SPRITE_COLOUR      0xe8
#define SPRITE_SCALE       0xdc
#define SPRITE_LOADED      0x149

/* ------------------------------------------------------------ the strap */
/*
 * The owner's strap (17.09.2026), hanging off the right edge of the settings
 * paper beside its OPTIONS heading. The picture is a layer of the settings
 * .anm2 under the paper (tools/strap_layers.py, straps.c); here is only where
 * a finger finds it.
 *
 * On the owner's mock-up of the settings sheet its top-left is (210, 49) of
 * the paper's head crop, which the settings .anm2 stands at (127, 15) of the
 * screen's own space (143, 31 less the pivot 16) - the space the Tweaks page
 * lays its paper in as well (TW_PAPER_Y, paper_x).
 *
 * The screen's space rides with the menu camera, and the settings list scrolls
 * by moving its anchor between -1240 and -1080 (0x769ef8), -1080 being the top.
 * So the strap is where the unscrolled screen is, moved as far as the camera
 * stands from it.
 */
#define GUIDE_STRAP_DROP  52.0f     /* the GUIDE strap hangs this far under TWEAKS (tools/guide_strap.py) */
#define GUIDE_STRAP_H     47.0f     /* and is this tall: the owner's own strap, guidepaper.png */
#define STRAP_H           55.0f
#define STRAP_FROM_X      (127.0f + 210.0f)
#define STRAP_FROM_Y      (15.0f + 49.0f)
#define OPTIONS_TOP       -1080.0f     /* the settings anchor on a 16:9 canvas only - see strap_box */
#define SETTINGS_LIST     0x10eed8     /* the settings list in the menu (straps.c) */
#define LIST_TILE         0x1c         /* where the list is drawn from, less the camera (0x6e3b48) */

/*
 * Each language's straps as drawn (strap_sizes.h, tools/strap_lang): a word
 * longer than the owner's lengthens its strap, and one that rises with the
 * slope stands taller at the top. The tap takes the same box as the picture.
 */
#include "strap_sizes.h"

static int strap_box(unsigned long menu, int kind, float *x0, float *y0, float *x1, float *y1);

/* ------------------------------------------------------------ the language */
/*
 * The page is made for every one of the game's menu sets (tools/tweaks_page.py)
 * and shown in the set of the game's own language - the same field the
 * description panel reads, Manager+0x564f8. French has no menu set of its own
 * and reads the English one, as the game's menus do.
 */
#define LANGUAGE 0x564f8

static int set_index(void) {
    unsigned long b = shim_image_base();
    unsigned long manager = b ? *(const unsigned long *) (b + GAME_MANAGER) : 0;
    return manager ? tw_set_for_language(*(const int *) (manager + LANGUAGE)) : 0;
}

/*
 * The rows, by id; a page lists a run of them (tools/tweaks_page.py lays them
 * in). A settings row of a mod is R_SET plus its place in SP_ROW: the modset
 * id it steps, its kind and its words are that table's.
 */
enum {
    /* RUMBLE left this page for the game's own Options (hotfix 1.75: the FILTER row, loader.c rumble_menu_item) */
    /* FPS COUNTER under RENDER SCALE (the owner, 21.10.2026): R_LANG moves to 19, still below R_HEADING;
       AIM LOCK below them, over DEAD ZONE - it stood among the picture's rows (the owner, 22.10.2026) */
    R_SPAWNER, R_SPAWNLOCK, R_SCALE, R_FPS, R_AIMLOCK, R_DEAD, R_SENS, R_SENSSHOOT,
    R_CONFIG, R_IMPORT, R_EXPORT,
    R_MOD0, R_MOD1, R_MOD2, R_MOD3, R_MOD4, R_MOD5, R_MOD6, R_MOD7,
    R_LANG,                     /* on no page: EID's settings page steps it (MS_EID_LANG) */
    /* the player's own mods: the heading, ADD MOD and the game's files out on the MODS list; a mod's page */
    R_HEADING = 20, R_ADD, R_DUMP,
    R_U_ON, R_U_UP, R_U_DOWN, R_U_EXPORT, R_U_DEL,
    R_RESTART,                  /* the last row of both, once a change is waiting for a restart */
    R_SHEAD,                    /* a player's mod's page: the heading SETTINGS over its own settings */
    R_U_CONFLICT, R_U_CHECK,    /* a player's mod's page: the red and the grey line over its title */
    R_SET = 100,
    R_USET = 800,               /* a player's mod's page: its setting k, R_USET + k */
    R_USER = 1000,              /* the list's row of the player's mod k: R_USER + k */
};
#define USET_KIND_SWITCH 0
#define USET_KIND_CHOICE 1
#define USET_KIND_NUMBER 2
#define USET_MAX 16             /* json.h MODJ_SETTINGS_MAX */

typedef struct {
    int screen;                 /* the screen whose anchor the page borrows */
    int rows, first;            /* how many rows, the id of the first */
    const tw_set_t *sets;       /* its layout, one per language set */
    int name_frame;             /* the Words frame of its first row's name */
    const char *art;            /* its picture: %s the set's suffix (and %d a page) */
    const char *name;           /* for the log and the report */
} page_t;

#define PAGE_TWEAKS 0
#define PAGE_MODS   1
#define PAGE_SET0   2           /* a mod's settings page: PAGE_SET0 + its SP_PAGE */
#define PAGE_USER   (PAGE_SET0 + SP_PAGES)  /* one of the player's own mods */
#define PAGE_GUIDE  (PAGE_USER + 1)         /* the controls guide, behind the GUIDE strap */
#define PAGE_DETAIL (PAGE_GUIDE + 1)        /* a player's mod's details: what breaks, what the check says */
#define PAGE_COUNT  (PAGE_DETAIL + 1)
#define SP_FIRST(p) (SP_PAGE[p][1])

static const page_t PAGES[PAGE_COUNT] = {
    { SCREEN_OPTIONS, TW_ROW_COUNT, R_SPAWNER, TW_SET, TW_NAME_FRAME,
      "gfx/port_tweaks_menu_%s.anm2", "tweaks" },
    { SCREEN_TITLE, MD_ROW_COUNT, R_MOD0, MD_SET, MD_NAME_FRAME,
      "gfx/port_mods_menu_%s.anm2", "mods" },
    { SCREEN_TITLE, 0, 0, SP_SET[0], 0, "gfx/port_modset_menu_0_%s.anm2", "mod settings" },
    { SCREEN_TITLE, 0, 0, SP_SET[1], 0, "gfx/port_modset_menu_1_%s.anm2", "mod settings" },
    { SCREEN_TITLE, 0, 0, SP_SET[2], 0, "gfx/port_modset_menu_2_%s.anm2", "mod settings" },
    { SCREEN_TITLE, 0, 0, SP_SET[3], 0, "gfx/port_modset_menu_3_%s.anm2", "mod settings" },
    { SCREEN_TITLE, 0, 0, SP_SET[4], 0, "gfx/port_modset_menu_4_%s.anm2", "mod settings" },
    { SCREEN_TITLE, 0, 0, SP_SET[5], 0, "gfx/port_modset_menu_5_%s.anm2", "mod settings" },
    { SCREEN_TITLE, 5, R_U_ON, MD_SET, 0, "gfx/port_mods_menu_%s.anm2", "user mod" },
    { SCREEN_OPTIONS, 0, 0, TW_SET, 0, "gfx/port_guide_%s.anm2", "guide" },
    { SCREEN_TITLE, 0, 0, MD_SET, 0, "gfx/port_mods_menu_%s.anm2", "mod details" },
};
static const page_t *g_page = &PAGES[0];

/*
 * A settings page's rows and first id come from the header, not from the
 * table. The MODS list has the port's own mods, the heading YOUR MODS, ADD MOD,
 * the game's files out - beside ADD MOD, as a modder's first step, not on
 * Tweaks - and then one row for each of the player's (the owner, 25.09.2026).
 */
#define MD_OWN (MD_ROW_COUNT + 3)   /* the list's row of the player's first mod */
/*
 * RESTART TO APPLY: the note under the rows was a button to look at and did
 * nothing (the owner, 25.09.2026), so it is one - the last row of the list and
 * of a mod's page while a change waits, and it starts the game again (the
 * list is saved already, mods.txt).
 */
static int restart_row(void) { return shim_usermods_changed() ? 1 : 0; }

/*
 * A PLAYER'S MOD'S PAGE (the owner's order, 02.10.2026): its title and
 * description, then - when the mod has settings - the heading SETTINGS and a
 * row per setting, then the mod's own rows (ENABLED, MOVE UP, MOVE DOWN,
 * EXPORT, DELETE), then RESTART TO APPLY while a change waits. A mod with no
 * settings keeps the page it had.
 */
static int g_user;                  /* the player's mod whose page is open */
static int user_settings(void) { return shim_usermods_settings(g_user); }
/* THE LINES OVER THE TITLE (below): rows of the page, the first ones */
static int g_links;                 /* 0..2 */
static int g_link_id[2];            /* R_U_CONFLICT, R_U_CHECK */
static int user_rows(void) { int n = user_settings(); return g_links + (n ? 1 + n : 0) + PAGES[PAGE_USER].rows; }
static int user_links(void);

static int page_rows(const page_t *pg) {
    if (pg == &PAGES[PAGE_MODS]) return MD_OWN + shim_usermods_count() + restart_row();
    if (pg == &PAGES[PAGE_USER]) return user_rows() + restart_row();
    int k = (int) (pg - PAGES) - PAGE_SET0;
    return k >= 0 && k < SP_PAGES ? SP_PAGE[k][2] : pg->rows;
}
static int page_first(const page_t *pg) {
    int k = (int) (pg - PAGES) - PAGE_SET0;
    return k >= 0 && k < SP_PAGES ? R_SET + SP_FIRST(k) : pg->first;
}
#define ROWS() page_rows(g_page)

/* the settings page of the mod on a row of the MODS list, or -1 */
static int settings_of(int mod_row) {
    for (int k = 0; k < SP_PAGES; k++)
        if (SP_PAGE[k][0] == mod_row) return k;
    return -1;
}
static int g_list_cursor;           /* the MODS row a settings page was opened from */

/* the menu font's cuts (eid.c, shim_menu_write) */
#define FONT_ROW   0                /* teammeatfont16bold */
#define FONT_TITLE 1                /* teammeatfont20bold */
#define FONT_SMALL 2                /* teammeatfont10 */

static int g_sure;                  /* DELETE pressed once: the next press deletes */

/*
 * The MODS list and a player's mod's page are laid at runtime: their width
 * follows the widest line (a mod's name is known only now), their height the
 * rows. Their layout is the header's, with the paper's size put in (lay_out).
 */
static tw_set_t g_live;
static int g_live_dirty = 1, g_live_set = -1;
static float g_live_w;              /* the canvas width it was laid for: its widest paper is the canvas's (THE WIDEST PAPER) */
static int is_live(const page_t *pg) {
    return pg == &PAGES[PAGE_MODS] || pg == &PAGES[PAGE_USER] || pg == &PAGES[PAGE_DETAIL];
}
static void lay_out(void);
static unsigned notes_sig(void);
static unsigned g_notes_sig;

static const tw_set_t *S(void) {
    if (is_live(g_page)) {
        if (g_live_dirty || g_live_set != set_index() || notes_sig() != g_notes_sig || g_live_w != shim_screen_width())
            lay_out();
        return &g_live;
    }
    return &g_page->sets[set_index()];
}
static int ID(int row) {
    if (g_page == &PAGES[PAGE_MODS]) {
        if (row < MD_ROW_COUNT) return R_MOD0 + row;
        if (row == MD_ROW_COUNT) return R_HEADING;
        if (row == MD_ROW_COUNT + 1) return R_ADD;
        if (row == MD_ROW_COUNT + 2) return R_DUMP;
        if (row - MD_OWN >= shim_usermods_count()) return R_RESTART;
        return R_USER + row - MD_OWN;
    }
    if (g_page == &PAGES[PAGE_USER]) {
        int n = user_settings();
        if (row >= user_rows()) return R_RESTART;
        if (row < g_links) return g_link_id[row];
        row -= g_links;
        if (n) {
            if (row == 0) return R_SHEAD;
            if (row <= n) return R_USET + row - 1;
            row -= 1 + n;
        }
        return R_U_ON + row;
    }
    return page_first(g_page) + row;
}
static const sp_row_t *SP(int row) {
    int id = ID(row);
    return id >= R_SET && id < R_USET ? &SP_ROW[id - R_SET] : 0;
}
/* a setting of the player's mod on its page: its place among the mod's settings, or -1 */
static int uset(int row) { int id = ID(row); return id >= R_USET && id < R_USET + USET_MAX ? id - R_USET : -1; }
static int uset_kind(int row) { int k = uset(row); return k >= 0 ? shim_usermods_setting_kind(g_user, k) : -1; }
/* a heading: YOUR MODS on the list, SETTINGS on a mod's page - the cursor passes them by */
static int is_heading(int row) { int id = ID(row); return id == R_HEADING || id == R_SHEAD; }

/* the language set the menus are shown in, for the spawner's heading */
int shim_tweaks_set_index(void) { return set_index(); }
const char *shim_tweaks_set_suffix(void) { return S()->suffix; }

/*
 * The top of a row, in paper units: YOUR MODS has air above it, a mod's rows
 * stand under its text; on a mod's page the heading SETTINGS has the same air
 * above it as YOUR MODS, and the mod's own rows are apart from its settings.
 * row_air is what is added to the even rows (no S(): lay_out asks it too).
 */
static float g_text_h;              /* a player's mod's page: its version line and description */
static float row_air(int i) {
    float air = 0.0f;
    if (g_page == &PAGES[PAGE_MODS] && i >= MD_ROW_COUNT) air += (float) MD_SECTION_GAP;
    if (g_page == &PAGES[PAGE_USER]) {
        int n = user_settings(), k = i - g_links;
        air += g_text_h;
        if (n) air += (float) MD_SECTION_GAP;                              /* above SETTINGS */
        if (n && k >= 1 + n) air += (float) MD_SECTION_GAP;                /* the mod's own rows, apart */
    }
    if (is_live(g_page) && ID(i) == R_RESTART) air += (float) MD_SECTION_GAP;    /* apart from the rows */
    return air;
}
/*
 * The lines over a mod's title are rows of their own height, one small line
 * each, from LINK_TOP; the rows after them stand where they stood, from row0
 * (which the lines have put down by their height, lay_out).
 */
#define SMALL_LINE 14.0f            /* teammeatfont10's 16, two less: the mock-up's */
#define LINK_TOP 12.0f              /* the first line's top on the paper: where the title's ink began */
static int user_links(void) { return g_page == &PAGES[PAGE_USER] ? g_links : 0; }
static int is_link(int row) { return row >= 0 && row < user_links(); }
static float row_top(int i) {
    if (is_link(i)) return LINK_TOP + (float) i * SMALL_LINE;
    return (float) (S()->row0 + (i - user_links()) * S()->step) + row_air(i);
}
static float row_h(int i) { return is_link(i) ? SMALL_LINE : (float) S()->step; }

/* ------------------------------------------------------------ the state */
static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;
static int g_open;
static unsigned long g_menu;
static float g_anchor_was[2];
static float g_base[2];             /* the anchor with nothing scrolled */

static int g_cursor;                /* the highlighted row */
static int g_pop;                   /* THE POPUP is shown */
static int g_pop_pending = -1;      /* a mod switched on before the pairs were there (THE POPUP) */

static int g_finger = -1;           /* the one finger that scrolls or steps */
static int g_opener = -1;           /* the finger that opened the page, still down */
static float g_start_x, g_start_y;  /* raw points, where it came down */
static float g_last_y;              /* units, where it was last move */
static float g_step_x;              /* units, where the last step was taken */
static int g_gesture;               /* 0 undecided, 1 up and down, 2 sideways */

static unsigned char g_art[PAGE_COUNT][TW_SETS][SPRITE_BYTES] __attribute__((aligned(16)));
static signed char g_art_state[PAGE_COUNT][TW_SETS];
static char g_art_path[PAGE_COUNT][TW_SETS][40];

/*
 * THE GUIDE IN THE PAD'S OWN PICTURES (the owner, 1.72): the slides that carry
 * a pad button are baked once more for every other family of pads
 * (tools/guide_slides.py FAMILIES) into port_guide_<set>_<family>.anm2, a frame
 * a slide (GD_FAMILY_FRAME); the family is the last pressed pad's (pad.c
 * shim_pad_icon_family), read every frame, so another pad's press changes the
 * pictures on the open slide. Family 0 is the main sheet itself, and so is any
 * family whose file does not load.
 */
static const char *const GD_FAMILY[GD_FAMILIES] = GD_FAMILY_NAMES;
static unsigned char g_guide_art[GD_FAMILIES][TW_SETS][SPRITE_BYTES] __attribute__((aligned(16)));
static signed char g_guide_state[GD_FAMILIES][TW_SETS];
static char g_guide_path[GD_FAMILIES][TW_SETS][40];
int shim_pad_icon_family(void);

/*
 * CENTRED IN WHOLE UNITS. Everything on these pages is drawn at 100 % and
 * must stand on a whole unit: at ORIGINAL and POTATO a unit is a pixel of the
 * frame (or two), and a picture half a unit off samples between texels - it
 * jumps as the page scrolls. Centring by (room - width) / 2 gave exactly that
 * half whenever the difference was odd: YOUR MODS on the MODS list (the
 * owner, 30.09.2026), and with it any name, title or line of an odd width,
 * and the whole page on a screen an odd number of units wide. At NORMAL a
 * unit is several pixels and the half fell on a whole one, which is why it
 * showed only there. The taps read the same functions, so they move with it.
 */
static float centred(float room, float width) { return floorf((room - width) * 0.5f); }
static float half_x(void) { return centred(shim_screen_width(), 480.0f); }
static float half_y(void) { return centred(shim_screen_height(), 270.0f); }

static float *anchor_of(unsigned long menu, int screen) {
    return (float *) (menu + MENU_ANCHORS + (unsigned long) screen * 8);
}

static void play(int id) {
    unsigned long b = shim_image_base();
    unsigned long manager = b ? *(const unsigned long *) (b + GAME_MANAGER) : 0;
    if (!manager) return;
    ((void (*)(unsigned long, int, int, int, float, float)) (b + FN_PLAY_SOUND))(
        manager, id, 0, 0, 1.0f, 1.0f);
}

/* --------------------------------------------------------- the settings */
static int g_spawner;
/*
 * EID is a mod like the others on MODS, the last row (the owner, 23.09.2026);
 * it used to follow the game's EXTRA HUD switch, and now has its own.
 */
#define MOD_COUNT 8
/*
 * Specialist Dance starts off (the owner, 25.09.2026), Stats+ too (04.10.2026),
 * and Range Fix (04.10.2026); the rest on. Range Fix stands before EID, which
 * stays the last row: EID moved from 6 to 7 (eid.c MOD_EID) - the file keys
 * are names, so nothing written so far changes meaning.
 */
static int g_mod[MOD_COUNT] = { 0, 1, 1, 1, 1, 0, 0, 1 };
static const char *const MOD_KEY[MOD_COUNT] = { "dance", "planet", "waves", "bars", "marks", "stats",
                                                "rangefix", "eid" };

int shim_tweaks_spawner(void) { return g_spawner; }
void shim_tweaks_set_spawner(int on) { g_spawner = on != 0; }

/* the FPS COUNTER switch (port-touch.ini 'fps'; drawn by mods.c shim_fps_draw) - off by default */
static int g_fps_on;
int shim_tweaks_fps(void) { return g_fps_on; }
void shim_tweaks_set_fps(int on) { g_fps_on = on != 0; }

/*
 * The number itself: the pictures handed to the screen in the last whole
 * second (gl.c g_gl_fps, counted at the swap - what the player sees, not what
 * the game drew), at the top centre of the picture, a few units down. Drawn
 * from the overlay hook (trace.c 'O'), the last of ours, so it stands over the
 * HUD, the menus and the pause paper alike; the description face with its rim
 * reads on any background.
 */
void shim_fps_draw(void) {
    if (!g_fps_on) return;
    extern int g_gl_fps;
    void shim_eid_write_scaled(const char *, float, float, float, float, float, float, float);
    float shim_eid_width_scaled(const char *, float);
    float shim_screen_width(void);
    char text[8];
    snprintf(text, sizeof(text), "%d", g_gl_fps);
    float w = shim_eid_width_scaled(text, 1.0f);
    shim_eid_write_scaled(text, floorf((shim_screen_width() - w) * 0.5f), 4.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f);
}
int shim_tweaks_mod(int i) { return (i >= 0 && i < MOD_COUNT) ? g_mod[i] : 0; }
void shim_tweaks_set_mod(int i, int on) { if (i >= 0 && i < MOD_COUNT) g_mod[i] = on != 0; }

int shim_tweaks_mod_index(const char *name) {
    for (int i = 0; i < MOD_COUNT; i++)
        if (!strcmp(name, MOD_KEY[i])) return i;
    return -1;
}

static unsigned char *settings(void) {
    unsigned long b = shim_image_base();
    return b ? *(unsigned char **) (b + GAME_MANAGER) : 0;
}

static int is_ticks(int row) {
    int id = ID(row);
    return id == R_DEAD || id == R_SENS || id == R_SENSSHOOT;
}
static int uset_kind(int row);
/* what a confirm does not step: a slider, and a mod's number (as the settings list leaves a slider) */
static int confirm_still(int row) { return is_ticks(row) || uset_kind(row) == USET_KIND_NUMBER; }

/*
 * THE ACTIONS: the config file in the system's file manager (item 14), the
 * saves in and out (item 9). They show OPEN and act on a confirm only - a
 * sideways swipe over them must not open a file manager. Java does the work
 * (MainActivity.portAction); loader.c carries the call.
 */
static int is_action(int row) {
    int id = ID(row);
    return id == R_CONFIG || id == R_IMPORT || id == R_EXPORT || id == R_DUMP || id == R_HEADING || id == R_SHEAD || id == R_ADD
        || id == R_U_UP || id == R_U_DOWN || id == R_U_EXPORT || id == R_U_DEL || id == R_RESTART
        || id == R_U_CONFLICT || id == R_U_CHECK;
}
void shim_port_action(int what);            /* loader.c: 1 config, 2 import, 3 export, 4 add a mod,
                                               6 the game's files out, 7 restart */

int shim_modset_int(int id);
void shim_modset_step(int id, int comp, int dir);
void shim_modset_format(int id, int comp, int which, char *out, unsigned long n);

/* a settings row's value and its step: modset.c's, or EID's own two (MS_EID_*) */
static int lang_frame_of(const char *code);
static int lang_available(int frame);
int shim_eid_glitch_info_get(void);
void shim_eid_glitch_info(int on);

static int sp_value(int ms) {
    if (ms == MS_EID_LANG) {
        const char *pin = shim_eid_language_pinned();
        return pin[0] ? lang_frame_of(pin) : 0;
    }
    if (ms == MS_EID_GLITCH) return shim_eid_glitch_info_get();
    return shim_modset_int(ms);
}

static void sp_step(int ms, int comp, int dir) {
    if (ms == MS_EID_LANG) {
        int at = sp_value(ms);
        for (int tries = 0; tries < TW_LANG_WORDS; tries++) {
            at = (at + dir + TW_LANG_WORDS) % TW_LANG_WORDS;
            if (lang_available(at)) break;
        }
        shim_eid_language_pin(at ? TW_LANG_CODE[at] : "auto");
        return;
    }
    if (ms == MS_EID_GLITCH) { shim_eid_glitch_info(!shim_eid_glitch_info_get()); return; }
    shim_modset_step(ms, comp, dir);
}

static int switch_on(int row) {
    if (SP(row)) return sp_value(SP(row)->ms) != 0;
    if (uset(row) >= 0) return shim_usermods_setting_value(g_user, uset(row)) != 0;
    switch (ID(row)) {
    case R_SPAWNER: return g_spawner;
    case R_FPS: return g_fps_on;
    /* CONSOLE TROPHIES, once SPAWNER TROPHIES (the owner, 28.09.2026: "people cannot find spawnerlock"):
       ON lets a run that used the spawner unlock things, that is the lock OFF */
    case R_SPAWNLOCK: { int shim_spawner_lock_get(void); return !shim_spawner_lock_get(); }
    case R_AIMLOCK: { const unsigned char *m = settings(); return m ? m[OPT_AIMLOCK] != 0 : 0; }
    case R_MOD0: case R_MOD1: case R_MOD2: case R_MOD3: case R_MOD4: case R_MOD5: case R_MOD6:
    case R_MOD7:
        return g_mod[ID(row) - R_MOD0];
    case R_U_ON: return shim_usermods_on(g_user);
    }
    if (ID(row) >= R_USER) return shim_usermods_on(ID(row) - R_USER);
    return 0;
}

/*
 * THE TICKS. Eleven frames, 0 to 10 marks, a tenth a step - the settings
 * sliders' own range and step (0x945f3c). Each row maps its value onto them:
 *     DEAD ZONE   0.00 .. 0.50, a twentieth a mark (0.25 by default)
 *     MOVE SENS   0.10 .. 1.00, and AIM SENS the same; nought would be a
 *                 stick that never moves, so the scale starts at a tenth
 */
static int ticks_of(int row) {
    float t;
    switch (ID(row)) {
    case R_DEAD: t = shim_sticks_dead_share() / 0.05f; break;
    case R_SENS: t = (shim_sticks_sens() - 0.1f) / 0.09f; break;
    case R_SENSSHOOT: t = (shim_sticks_sens_shoot() - 0.1f) / 0.09f; break;
    default: return 0;
    }
    int n = (int) lroundf(t);
    return n < 0 ? 0 : (n > 10 ? 10 : n);
}

static void ticks_set(int row, int n) {
    n = n < 0 ? 0 : (n > 10 ? 10 : n);
    switch (ID(row)) {
    case R_DEAD: shim_sticks_set_deadzone(0.05f * (float) n); break;
    case R_SENS: shim_sticks_set_sens(0.1f + 0.09f * (float) n); break;
    case R_SENSSHOOT: shim_sticks_set_sens_shoot(0.1f + 0.09f * (float) n); break;
    }
}

/*
 * RENDER SCALE: THREE STEPS (the owner, 25.10.2026). POTATO - one canvas, the
 * heavy effects off (25.09.2026); ORIGINAL - one canvas, as iOS draws (back on
 * 17.10.2026, the default since 18.10.2026); ENHANCED - the scene enlarged two
 * thirds of the way from one canvas to the picture's own size, the old MEDIUM
 * ("the most balanced; above it nothing much shows"). LOW, HIGH, ULTRA and
 * EXTREME are gone. The multiplier is measured against THIS screen by screen.c
 * (enhanced_for: 1080 tall 3, 1440 tall 4, 720 tall 2), so the name means the
 * same picture on every phone (16.09.2026), and any ceiling above one canvas
 * kept in port-touch.ini - an old HIGH or ULTRA - is ENHANCED.
 */
int shim_gl_height(void);

typedef struct { int mult; int word; } scale_step_t;   /* word: a Words frame */

#define SCALE_POTATO (-1)           /* screen.c's SHOT_POTATO */
#define SCALE_STEPS  3              /* POTATO, ORIGINAL, ENHANCED */

static int scale_steps(scale_step_t *out) {
    int n = 0;
    out[n].mult = SCALE_POTATO;         /* frame 0 is AUTO, 1 POTATO, 2 ORIGINAL, 3 ENHANCED */
    out[n].word = 1;
    n++;
    out[n].mult = 1;                    /* ORIGINAL, back on 17.10.2026: one canvas, as iOS */
    out[n].word = 2;
    n++;
    int e = shim_screen_shot_enhanced();
    if (shim_screen_shot_rule() > 1 && e > 1 && 3 < TW_SCALE_WORDS) {   /* ENHANCED: where the screen has room above one canvas */
        out[n].mult = e;
        out[n].word = 3;
        n++;
    }
    return n;
}

/* where the current ceiling stands among the steps; none set is ORIGINAL (screen.c smooth_x1), any above one canvas ENHANCED */
static int scale_position(const scale_step_t *steps, int n) {
    (void) steps;
    int c = shim_screen_shot_ceiling_get();
    if (c == SCALE_POTATO) return 0;
    if (c > 1 && n > 2) return 2;
    return n > 1 ? 1 : 0;
}

/* the description languages this database actually carries, as word frames */
static int lang_frame_of(const char *code) {
    for (int i = 0; i < TW_LANG_WORDS; i++)
        if (!strcmp(code, TW_LANG_CODE[i])) return i;
    return 0;
}

static int lang_available(int frame) {
    if (frame == 0) return 1;                           /* AUTO */
    int n = shim_eid_language_count();
    for (int i = 0; i < n; i++)
        if (!strcmp(shim_eid_language_code(i), TW_LANG_CODE[frame])) return 1;
    return 0;
}

/*
 * The player's mods' actions. ADD MOD is Java's (one zip or several picked,
 * checked, unpacked; since hotfix 1.73 the game is not started again - the list
 * shows the new mods pale, and RESTART TO APPLY applies them). On a mod's page: UP and DOWN move it
 * in the list - the first is the strongest where two hold one file - with the
 * step's sound and the place shown beside them ("2 / 3": the owner could not
 * tell that anything had happened); EXPORT zips it into a folder the player
 * picks (it was the share sheet, and the owner wanted a folder); DELETE asks
 * once more and then takes it off the list (its folder goes at the next start);
 * RESTART TO APPLY starts the game again.
 */
static void switch_to(const page_t *page, int cursor);
static void open_detail(void);
static void popup_ask(int mod);
static void user_action(int id) {
    int n = shim_usermods_count();
    switch (id) {
    case R_ADD: shim_port_action(4); return;
    case R_U_CONFLICT: case R_U_CHECK: open_detail(); return;
    case R_U_UP:
        if (g_user > 0) { shim_usermods_move(g_user, -1); g_user--; play(SOUND_STEP); }
        break;
    case R_U_DOWN:
        if (g_user + 1 < n) { shim_usermods_move(g_user, 1); g_user++; play(SOUND_STEP); }
        break;
    case R_RESTART: shim_port_action(7); return;
    case R_U_EXPORT: shim_port_action_text(5, shim_usermods_id(g_user)); return;
    case R_U_DEL:
        if (!g_sure) { g_sure = 1; break; }
        g_sure = 0;
        shim_usermods_remove(g_user);
        n = shim_usermods_count();
        /* back on the list, at the mod that took its place - or at ADD MOD */
        switch_to(&PAGES[PAGE_MODS], g_user < n ? MD_OWN + g_user : (n ? MD_OWN + n - 1 : MD_ROW_COUNT + 1));
        break;
    }
    g_live_dirty = 1;
}

/* one step of a row, forwards or back: what a sideways step or a confirm does */
static void step(int row, int dir) {
    if (is_action(row)) {
        int id = ID(row);
        if (dir <= 0) return;
        if (id >= R_CONFIG && id <= R_EXPORT) shim_port_action(id - R_CONFIG + 1);
        else if (id == R_DUMP) shim_port_action(6);
        else user_action(id);
        return;
    }
    if (ID(row) >= R_USER || ID(row) == R_U_ON) {
        int mod = ID(row) == R_U_ON ? g_user : ID(row) - R_USER;
        shim_usermods_toggle(mod);
        g_live_dirty = 1;                               /* the note that a restart applies it */
        if (shim_usermods_on(mod)) popup_ask(mod);      /* switched on: does it break with one that is on (THE POPUP) */
        return;
    }
    if (uset(row) >= 0) {
        /* a switch flips, a choice goes round its values, a number moves by its step and stops at its ends;
           written at once, and RESTART TO APPLY comes and goes with it */
        shim_usermods_setting_step(g_user, uset(row), dir);
        g_live_dirty = 1;
        return;
    }
    if (SP(row)) {
        sp_step(SP(row)->ms, SP(row)->comp, dir);
        shim_sticks_settings_save();
        return;
    }
    switch (ID(row)) {
    case R_SPAWNER: g_spawner = !g_spawner; break;
    case R_FPS: g_fps_on = !g_fps_on; break;
    case R_SPAWNLOCK: { int shim_spawner_lock_get(void); void shim_spawner_lock(int);
                        shim_spawner_lock(!shim_spawner_lock_get()); break; }
    case R_AIMLOCK: { unsigned char *m = settings(); if (m) m[OPT_AIMLOCK] = !m[OPT_AIMLOCK]; break; }
    case R_MOD0: case R_MOD1: case R_MOD2: case R_MOD3: case R_MOD4: case R_MOD5: case R_MOD6:
    case R_MOD7:
        g_mod[ID(row) - R_MOD0] = !g_mod[ID(row) - R_MOD0]; break;
    case R_DEAD: case R_SENS: case R_SENSSHOOT:
        ticks_set(row, ticks_of(row) + dir);
        break;
    case R_SCALE: {
        /* POTATO, ORIGINAL, ENHANCED, and round to POTATO. The first build ran
           it the other way, from the top downwards, and the owner read that as
           backwards (16.09.2026). AUTO is gone (16.10.2026). */
        scale_step_t steps[SCALE_STEPS];
        int n = scale_steps(steps);
        if (n <= 0) break;
        int at = scale_position(steps, n);
        at = (at + dir + n) % n;
        shim_screen_shot_ceiling(steps[at].mult);
        break;
    }
    case R_LANG: {
        const char *pin = shim_eid_language_pinned();
        int at = pin[0] ? lang_frame_of(pin) : 0;
        for (int tries = 0; tries < TW_LANG_WORDS; tries++) {
            at = (at + dir + TW_LANG_WORDS) % TW_LANG_WORDS;
            if (lang_available(at)) break;
        }
        shim_eid_language_pin(at ? TW_LANG_CODE[at] : "auto");
        break;
    }
    }
    shim_sticks_settings_save();
}

/* ------------------------------------------------------------ the layout */
/*
 * How far the page may scroll: until its foot - the straight lower edge of the
 * paper's shadow, the picture's last row - stands on the bottom of the frame.
 * Not a unit further: past it the cut-off shadow and bare wall show (the owner,
 * 23.09.2026; it used to go 8 units on).
 */
/*
 * THE SCREEN'S EDGES IN THE PAGE'S UNITS. The page stands half_y() down from the
 * canvas top, so the canvas's bottom is its height less that - 270 on 16:9,
 * but 315 on a 4:3 tablet, whose canvas is 360 tall. With 270 written in, the
 * page's foot stopped 44 units above the bottom of a tablet (the tap audit,
 * 01.10.2026), and a row was scrolled to as if the screen ended there.
 */
static float view_top(void) { return -half_y(); }
static float view_bottom(void) { return shim_screen_height() - half_y(); }

static float scroll_room(void) {
    /*
     * One unit short of it (the owner, 24.09.2026): with the foot exactly on the
     * frame's bottom its last row - the cut-off shadow - was still on the screen.
     */
    float bottom = (float) (TW_PAPER_Y + S()->paper_h) - 1.0f;
    return bottom > view_bottom() ? bottom - view_bottom() : 0.0f;
}

/*
 * WHERE A ROW'S PIECES STAND.
 *
 * A line is cursor, name, gap and value, and the whole of it is centred on the
 * page - for the row's WIDEST value, never for the one it shows now. That is
 * how the settings rows are laid out (lang.menu), and for a reason the owner
 * spelled out: centred on the current value, a row jumps sideways every time
 * the value changes width (16.09.2026). The cursor counts: left out, it hung
 * off the torn edge on most rows.
 *
 * Every word stands on one baseline, (float) S()->baseline units below the row's top: the
 * words are cut to their own ink, and centring ink of different heights made
 * the rows step up and down.
 */

static int scale_frame(void) {
    scale_step_t steps[SCALE_STEPS];
    int n = scale_steps(steps);
    int at = scale_position(steps, n);
    return n == 0 ? 0 : steps[at].word;
}

/* a setting's widest value on the open mod's page (lay_out): a row is laid out for it, never for the one shown */
static float g_uset_w[USET_MAX];

/* the widest a row's value can ever be - what the row is laid out for */
/* a number as characters of the digit run: its width in units */
static float number_width(const char *text) {
    float w = 0.0f;
    for (const char *c = text; *c; c++) {
        const char *at = strchr(SP_DIGITS, *c);
        if (at) w += (float) S()->word_w[at - SP_DIGITS] + 1.0f;
    }
    return w;
}

static float value_width(int row) {
    if (SP(row)) {
        const sp_row_t *r = SP(row);
        if (r->kind == SP_BOOL) return (float) (S()->on_w > S()->off_w ? S()->on_w : S()->off_w);
        if (r->kind == SP_CHOICE) {
            int widest = 0;
            for (int k = 0; k < r->count; k++)
                if (S()->word_w[r->choice + k] > widest) widest = S()->word_w[r->choice + k];
            return (float) widest;
        }
        char lo[16], hi[16];
        shim_modset_format(r->ms, r->comp, 1, lo, sizeof(lo));
        shim_modset_format(r->ms, r->comp, 2, hi, sizeof(hi));
        float a = number_width(lo), b = number_width(hi);
        return a > b ? a : b;
    }
    /* the MODS list: ON/OFF, then the arrow of a mod that has a page; HEADING and ADD MOD none */
    if (g_page == &PAGES[PAGE_MODS]) {
        if (is_action(row)) return 0.0f;
        return (float) (S()->on_w > S()->off_w ? S()->on_w : S()->off_w) + (float) TW_GAP + (float) MD_ARROW_W;
    }
    if (g_page == &PAGES[PAGE_USER]) {
        if (is_link(row)) return (float) MD_ARROW_W;
        if (uset(row) >= 0) return g_uset_w[uset(row)];
        if (ID(row) == R_U_UP || ID(row) == R_U_DOWN) return shim_menu_text_width(FONT_ROW, "00 / 00");
        return ID(row) == R_U_ON ? (float) (S()->on_w > S()->off_w ? S()->on_w : S()->off_w) : 0.0f;
    }
    if (is_action(row)) return 0.0f;           /* no OPEN beside them any more (24.09.2026) */
    if (is_ticks(row)) return (float) TW_TICKS_W;
    int from = -1, count = 0;
    if (ID(row) == R_SCALE) { from = 0; count = TW_SCALE_WORDS; }
    if (ID(row) == R_LANG) { from = TW_SCALE_WORDS; count = TW_LANG_WORDS; }
    if (from < 0) return (float) (S()->on_w > S()->off_w ? S()->on_w : S()->off_w);
    int widest = 0;
    for (int i = 0; i < count; i++)
        if (S()->word_w[from + i] > widest) widest = S()->word_w[from + i];
    return (float) widest;
}

/* the Words frame of a row's name */
static int name_frame(int row) { return SP(row) ? SP(row)->label : g_page->name_frame + row; }

/* a row of the player's mods named by an Extra frame, or -1 */
static int extra_of(int row) {
    switch (ID(row)) {
    case R_HEADING: return MX_HEADING;
    case R_SHEAD: return MX_SETTINGS;
    case R_ADD: return MX_ADD;
    case R_DUMP: return MX_DUMP;
    case R_U_ON: return MX_ENABLED;
    case R_U_UP: return MX_UP;
    case R_U_DOWN: return MX_DOWN;
    case R_U_EXPORT: return MX_EXPORT;
    case R_RESTART: return MX_RESTART;
    case R_U_DEL: return g_sure ? MX_SURE : MX_DELETE;
    }
    return -1;
}

/*
 * THE MENU FONT FOR A PLAYER'S MOD. Its name is not known on the desk, so it is
 * printed in the game's teammeatfont16bold, in the menus' ink (the owner,
 * 25.09.2026). The baked words stand on the row's baseline by the bottom of
 * their ink; a printed line starts at its top, and a capital's ink ends
 * CAP_BOTTOM below it (measured from the .fnt files: 20 for 16bold, 30 for
 * 20bold, 13 for 10; the capitals' ink begins CAP_TOP below it).
 */
static const float CAP_BOTTOM[3] = { 20.0f, 30.0f, 13.0f };
static const float CAP_TOP[3] = { 2.0f, 2.0f, 1.0f };
#define INK_R (54.0f / 255.0f)
#define INK_G (47.0f / 255.0f)
#define INK_B (45.0f / 255.0f)
/*
 * The red of a conflict: the game's own mark red - completion_widget, coop
 * menu b, baby_select (other stuff/mods-conflict-mockup, 00-palette.png; the
 * owner chose it 05.10.2026). The grey of the check's line is the same ink,
 * lighter: no colour of our own on the game's paper.
 */
#define RED_R (158.0f / 255.0f)
#define RED_G (11.0f / 255.0f)
#define RED_B (15.0f / 255.0f)
#define GREY_A (140.0f / 255.0f)

static char g_link_text[2][160];
static const char *list_label(int k);
static const char *uset_label(int k);
static float name_width(int row) {
    if (is_link(row)) return shim_menu_text_width(FONT_SMALL, g_link_text[row]);
    int x = extra_of(row);
    if (x >= 0) return (float) MX_W[set_index()][x];
    if (ID(row) >= R_USER) return shim_menu_text_width(FONT_ROW, list_label(ID(row) - R_USER));
    if (uset(row) >= 0) return shim_menu_text_width(FONT_ROW, uset_label(uset(row)));
    return (float) S()->word_w[name_frame(row)];
}

static float clear_of_corners(float name_x, float name_w);
static void row_layout(int row, float *name_x, float *value_x) {
    float name_w = name_width(row);
    float value_w = value_width(row);
    float total = (float) TW_CURSOR_W + name_w + (value_w > 0.0f ? (float) TW_GAP + value_w : 0.0f);
    *name_x = centred((float) S()->paper_w, total) + (float) TW_CURSOR_W;
    if (is_live(g_page) && value_w <= 0.0f) *name_x = clear_of_corners(*name_x, name_w);
    *value_x = *name_x + name_w + (float) TW_GAP;
}

/*
 * THE LIVE LAYOUT of the MODS list and a player's mod's page.
 *
 * Width: the widest line, as tools/tweaks_page.py measures the baked pages -
 * cursor, name, gap, value - with a cursor's width more, because the line
 * stands half a cursor right of the middle, and the torn edge's margin on both
 * sides. The paper grows by whole bands, up to the widest the canvas holds
 * (THE WIDEST PAPER: a longer name is cut on the list, its title goes onto
 * more lines). A mod's page is as wide as the list it came from, so the two
 * do not jump between each other.
 *
 * Height: the rows (and the note that a restart applies the changes), in
 * whole pieces, between the head and the foot.
 */
static int g_bands;
static int g_title_font;
#define DESC_LINES 12
static int g_desc_n;
static char g_desc[DESC_LINES][128];
static char g_facts[128];

/*
 * THE PAGE'S NOTES (plan 02.10, A5 and item 1): under the rows of the MODS
 * list, in the small cut - that the list could not be saved (and why) or could
 * not be read. Without them the player saw an OFF that did not stay, and
 * nothing else. The menu font has Latin with its accents and Cyrillic, not the
 * CJK sets: Japanese, Korean and Chinese read the English line, as the mods'
 * own names do.
 *
 * A player's mod's page has none under its rows any more (the owner,
 * 05.10.2026: the wall at its end): what the port's check says of the mod and
 * what it breaks with the mods that are on stand behind two short lines over
 * its title (THE LINES OVER THE TITLE), on its details page (THE DETAILS).
 */
#define NOTE_LINES 24
static int g_note_n;
static char g_note[NOTE_LINES][128];
static float g_note_h;

static int wrap_into(char (*out)[128], int max, const char *text, float width);
static int wrap_font(int font, char (*out)[128], int max, const char *text, float width);

/* the words of a note in the page's language set: en, ru, de, es (jp, kr, zh read en) */
static const char *note_word(int set, const char *const w[4]) { return w[set >= 1 && set <= 3 ? set : 0]; }
static int lang_of(int set) { return set >= 1 && set <= 3 ? set : 0; }

/* Russian's three forms of a count: 1 / 2-4 / 5+ */
static const char *ru_form(int n, const char *one, const char *few, const char *many) {
    int m10 = n % 10, m100 = n % 100;
    return m10 == 1 && m100 != 11 ? one : m10 >= 2 && m10 <= 4 && (m100 < 12 || m100 > 14) ? few : many;
}

static unsigned notes_sig(void) {
    unsigned h = 2166136261u;
    for (const char *s = shim_usermods_save_error(); *s; s++) h = (h ^ (unsigned char) *s) * 16777619u;
    for (const char *s = shim_usermods_settings_error(); *s; s++) h = (h ^ (unsigned char) *s) * 16777619u;
    /* the pairs worked out (usermods.c, THE PAIRS): the red line and the details come with them */
    h = (h ^ (unsigned) shim_usermods_pairs_gen()) * 16777619u;
    /* the list as ADD MOD left it (hotfix 1.73: no restart after it): the new rows, the paper's size, the pale note */
    h = (h ^ (unsigned) shim_usermods_list_gen()) * 16777619u;
    /* RESTART TO APPLY coming or going by itself (hotfix 1.74, THE RESTART OWED: the disk looked at, a list read
       again): the paper's height and the scroll's room count it, so the page lays out again with it */
    h = (h ^ (unsigned) restart_row()) * 16777619u;
    return h ^ (shim_usermods_list_unread() ? 0x9e3779b9u : 0) ^ (unsigned) set_index();
}

/* "N files" in the page's language: en, ru (1 / 2-4 / 5+), de, es */
static void files_word(char *out, size_t room, int set, int n) {
    int lang = set >= 1 && set <= 3 ? set : 0;
    int m10 = n % 10, m100 = n % 100;
    const char *ru = m10 == 1 && m100 != 11 ? "файл" : m10 >= 2 && m10 <= 4 && (m100 < 12 || m100 > 14) ? "файла" : "файлов";
    const char *w = lang == 1 ? ru : lang == 2 ? (n == 1 ? "Datei" : "Dateien") : lang == 3 ? (n == 1 ? "archivo" : "archivos")
                  : (n == 1 ? "file" : "files");
    snprintf(out, room, "%d %s", n, w);
}

/* the kinds of files (usermods.c K_* bits: pictures, animations, ...) in the page's language: "a, b" */
static void kinds_word(char *out, size_t room, int set, unsigned kinds) {
    static const char *const KIND[8][4] = {
        { "pictures", "картинки", "Bilder", "imágenes" }, { "animations", "анимации", "Animationen", "animaciones" },
        { "sounds", "звуки", "Klänge", "sonidos" }, { "music", "музыка", "Musik", "música" },
        { "tables", "таблицы", "Tabellen", "tablas" }, { "fonts", "шрифты", "Schriften", "fuentes" },
        { "videos", "видео", "Videos", "vídeos" }, { "other", "прочее", "Sonstiges", "otros" } };
    size_t at = 0;
    out[0] = 0;
    for (int b = 0; b < 8 && at < room; b++)
        if (kinds & (1u << b)) at += (size_t) snprintf(out + at, room - at, "%s%s", at ? ", " : "", KIND[b][lang_of(set)]);
}

/*
 * THE CHECK'S NOTES on a player's mod - what stood under its rows: its
 * mod.json, a change of its settings that did not stay, the last run closed
 * after a file of it, what the guard completed and refused, pictures of the
 * wrong size, a table the game would write past, the data tables built from
 * it, the converter's report. Whole sentences, without the path of check.txt
 * in each: the details page names it once, at its foot.
 */
#define CHECK_NOTES 14
static int g_cn_n;
static char g_cn[CHECK_NOTES][400];
static void check_note(const char *text) { if (g_cn_n < CHECK_NOTES) snprintf(g_cn[g_cn_n++], sizeof(g_cn[0]), "%s", text); }

static void check_notes(int set) {
    char text[600];
    g_cn_n = 0;
    /* what json.c said of its mod.json (plan 4.10, part 1): the file not read, or settings it turned off */
    int broken = 0;
    char reason[32];
    int refused = shim_usermods_manifest_note(g_user, &broken, reason, sizeof(reason));
    if (broken) {
        static const char *const W[4] = { "mod.json could not be read (%s)",
                                          "mod.json не прочитан (%s)",
                                          "mod.json nicht lesbar (%s)",
                                          "No se pudo leer mod.json (%s)" };
        snprintf(text, sizeof(text), note_word(set, W), reason);
        check_note(text);
    } else if (refused) {
        static const char *const W[4] = { "Settings turned off: %d (%s)",
                                          "Выключено настроек: %d (%s)",
                                          "Abgeschaltete Einstellungen: %d (%s)",
                                          "Ajustes desactivados: %d (%s)" };
        snprintf(text, sizeof(text), note_word(set, W), refused, reason);
        check_note(text);
    }
    /* a change of a setting that did not stay (mod-settings.ini, the safe writer's failure) */
    const char *serr = shim_usermods_settings_error();
    if (serr[0] && user_settings()) {
        static const char *const W[4] = { "The mod's settings were not saved", "Настройки мода не сохранились",
                                          "Die Einstellungen des Mods wurden nicht gespeichert",
                                          "Los ajustes del mod no se guardaron" };
        const char *why = strchr(serr, '(');
        snprintf(text, sizeof(text), "%s %s", note_word(set, W), why ? why : serr);
        check_note(text);
    }
    const char *crash = shim_usermods_crash(g_user);
    if (crash[0]) {
        static const char *const W[4] = { "The game closed right after reading", "Игра закрылась сразу после файла",
                                          "Das Spiel schloss sich gleich nach", "El juego se cerró justo después de" };
        snprintf(text, sizeof(text), "%s %s", note_word(set, W), crash);
        check_note(text);
    }
    int fixed = shim_usermods_guard_fixed(g_user), game = shim_usermods_guard_game(g_user);
    if (fixed || game) {
        static const char *const W[4] = { "Files completed by the port: %d, the game's used instead: %d",
                                          "Порт дополнил файлов: %d, взял игровые вместо: %d",
                                          "Vom Port ergänzt: %d Dateien, durch die des Spiels ersetzt: %d",
                                          "Archivos completados por el port: %d, sustituidos por los del juego: %d" };
        snprintf(text, sizeof(text), note_word(set, W), fixed, game);
        check_note(text);
    }
    /* pictures of the wrong size: the game does not scale them - the converter or the modder does (02.10.2026) */
    int size = shim_usermods_guard_size(g_user);
    if (size) {
        static const char *const W[4] = { "Pictures of the wrong size: %d - run the mod through the converter (modport) or scale them down by hand",
                                          "Картинок не того размера: %d - прогоните мод через конвертер (modport) или уменьшите их вручную",
                                          "Bilder in falscher Größe: %d - den Mod durch den Konverter (modport) schicken oder die Bilder von Hand verkleinern",
                                          "Imágenes de tamaño incorrecto: %d - pasa el mod por el conversor (modport) o redúcelas a mano" };
        snprintf(text, sizeof(text), note_word(set, W), size);
        check_note(text);
    }
    /* a data table the game's loader would write past (M4): not used, and which record and why */
    char file[96], tag[48], id[32];
    long lo = 0, hi = 0;
    int bounds = shim_usermods_guard_bounds(g_user, file, sizeof(file), tag, sizeof(tag), id, sizeof(id), &lo, &hi);
    if (bounds) {
        static const char *const W[4] = { "Not used, the game would write past its table: %s - %s id %s, the table holds %ld..%ld",
                                          "Не взят, игра писала бы за край таблицы: %s - %s id %s, в таблице %ld..%ld",
                                          "Nicht verwendet, das Spiel schriebe über seine Tabelle hinaus: %s - %s id %s, die Tabelle fasst %ld..%ld",
                                          "No se usa, el juego escribiría fuera de su tabla: %s - %s id %s, la tabla admite %ld..%ld" };
        int n = snprintf(text, sizeof(text), note_word(set, W), file, tag, id, lo, hi);
        if (bounds > 1 && n > 0 && (size_t) n < sizeof(text)) snprintf(text + n, sizeof(text) - (size_t) n, " (+%d)", bounds - 1);
        check_note(text);
    }
    /* the data tables the port built from this mod's files (plan 4.9 M3): what it replaced, patched and
       merged, and what stood in the way - a stronger mod's conflict, a legacy file that took a record away */
    content_sum_t cs;
    if (shim_usermods_content(g_user, &cs)) {
        static const char *const HEAD[4] = { "Tables:", "Таблицы:", "Tabellen:", "Tablas:" };
        static const char *const PART[6][4] = {
            { "replaced %d", "заменено %d", "ersetzt %d", "reemplazadas %d" },
            { "patched %d", "с патчем %d", "gepatcht %d", "parcheadas %d" },
            { "merged %d", "слито %d", "zusammengeführt %d", "fusionadas %d" },
            { "not used %d", "не взято %d", "nicht verwendet %d", "no usadas %d" },
            { "conflicts %d", "конфликтов %d", "Konflikte %d", "conflictos %d" },
            { "overridden by legacy %s: %d", "перекрыто легаси %s: %d", "überdeckt von Legacy %s: %d", "tapadas por el legado %s: %d" },
        };
        const int count[6] = { cs.replaced, cs.patched, cs.merged, cs.refused, cs.conflicts, cs.overridden };
        int lang = set >= 1 && set <= 3 ? set : 0;
        size_t n = (size_t) snprintf(text, sizeof(text), "%s", HEAD[lang]);
        for (int k = 0, first = 1; k < 6 && n < sizeof(text); k++) {
            if (!count[k]) continue;
            n += (size_t) snprintf(text + n, sizeof(text) - n, "%s ", first ? "" : (k >= 3 ? ";" : ","));
            if (n < sizeof(text)) {
                if (k == 5) n += (size_t) snprintf(text + n, sizeof(text) - n, PART[k][lang], cs.legacy, count[k]);
                else n += (size_t) snprintf(text + n, sizeof(text) - n, PART[k][lang], count[k]);
            }
            first = 0;
        }
        check_note(text);
        /* a whole table's item lists mended (M1, the owner 02.10.2026): the ids that are no item, left out */
        if (cs.mended) {
            static const char *const W[4] = { "%s: item %s does not exist - left out",
                                              "%s: предмета %s нет - убран",
                                              "%s: Gegenstand %s gibt es nicht - weggelassen",
                                              "%s: el objeto %s no existe - quitado" };
            int m = snprintf(text, sizeof(text), note_word(set, W), cs.mend_who, cs.mend_item);
            if (m > 0 && (size_t) m < sizeof(text) && cs.mended > 1)
                m += snprintf(text + m, sizeof(text) - (size_t) m, " (+%d)", cs.mended - 1);
            check_note(text);
        }
        /* a whole table of another version (Repentance+): its values of that version put back to the game's */
        if (cs.versioned) {
            static const char *const W[4] = {
                "A table from Repentance+: %d of its values are Repentance+'s - the game's kept (%d tables)",
                "Таблица из Repentance+: значений Repentance+ в ней %d - оставлены игровые (таблиц: %d)",
                "Eine Tabelle aus Repentance+: %d ihrer Werte sind von Repentance+ - die des Spiels bleiben (%d Tabellen)",
                "Una tabla de Repentance+: %d de sus valores son de Repentance+ - se quedan los del juego (%d tablas)" };
            snprintf(text, sizeof(text), note_word(set, W), cs.versioned, cs.version_tables);
            check_note(text);
        }
    }
    /* a sheet of the game's under this mod's own frames, cut for its own copy the port's check does not use: no other mod
       takes part, so no conflict - its own business, said here (THE MODS OVER EACH OTHER, the run's record) */
    for (int k = 0; ; k++) {
        int am, sm, anms;
        unsigned sw, sh, iw, ih;
        char aid[80], sid[80], sheet[300], anm[300], files[48];
        if (!shim_usermods_broken(g_user, k, &am, aid, &sm, sid, sizeof(aid), sheet, sizeof(sheet), &sw, &sh, &iw, &ih, &anms,
                                  anm, sizeof(anm))) break;
        if (sid[0] || am != g_user) continue;
        static const char *const W[4] = {
            "Broken: %s - the game's %ux%u under this mod's animations (%s) cut for %ux%u: its own copy is not used",
            "Сломано: %s - игровая %ux%u под анимациями этого мода (%s), нарезанными под %ux%u: его своя копия не взята",
            "Kaputt: %s - die des Spiels, %ux%u, unter den Animationen dieses Mods (%s), geschnitten für %ux%u: seine eigene Kopie wird nicht verwendet",
            "Roto: %s - la del juego, %ux%u, bajo las animaciones de este mod (%s), cortadas para %ux%u: su propia copia no se usa" };
        const char *leaf = strrchr(sheet, '/');
        files_word(files, sizeof(files), set, anms);
        snprintf(text, sizeof(text), note_word(set, W), leaf ? leaf + 1 : sheet, sw, sh, files, iw, ih);
        check_note(text);
    }
    /* the converter's report, for a mod the installer adapted: where it lies */
    if (shim_usermods_adapted(g_user)) {
        static const char *const W[4] = { "Adaptation report: mods/%s/modport-report.txt",
                                          "Отчёт адаптации: mods/%s/modport-report.txt",
                                          "Anpassungsbericht: mods/%s/modport-report.txt",
                                          "Informe de adaptación: mods/%s/modport-report.txt" };
        snprintf(text, sizeof(text), note_word(set, W), shim_usermods_id(g_user));
        check_note(text);
    }
}

static void build_notes(float width) {
    int set = set_index();
    char text[600];
    g_note_n = 0;
    if (g_page == &PAGES[PAGE_MODS]) {
        const char *err = shim_usermods_save_error();
        if (err[0]) {
            static const char *const W[4] = { "The mod list was not saved", "Список модов не сохранился",
                                              "Die Modliste wurde nicht gespeichert", "La lista de mods no se guardó" };
            const char *why = strchr(err, '(');
            snprintf(text, sizeof(text), "%s %s", note_word(set, W), why ? why : err);
            g_note_n += wrap_into(g_note + g_note_n, NOTE_LINES - g_note_n, text, width);
        }
        if (shim_usermods_list_unread()) {
            static const char *const W[4] = { "The mod list could not be read - waiting for it",
                                              "Список модов не прочитан, жду",
                                              "Die Modliste ist nicht lesbar - ich warte",
                                              "No se pudo leer la lista de mods - esperando" };
            g_note_n += wrap_into(g_note + g_note_n, NOTE_LINES - g_note_n, note_word(set, W), width);
        }
    }
    /* mods ADD MOD put in place in this run, switched on: pale until the restart (hotfix 1.73, THE PALE NAME) */
    int pending = 0;
    if (g_page == &PAGES[PAGE_MODS])
        for (int k = 0; k < shim_usermods_count() && !pending; k++) pending = shim_usermods_pending(k);
    else if (g_page == &PAGES[PAGE_USER])
        pending = shim_usermods_pending(g_user);
    if (pending) {
        static const char *const W[4] = { "Pale names: just added, they work after a restart",
                                          "Бледные названия: только что добавлены, заработают после перезапуска",
                                          "Blasse Namen: gerade hinzugefügt, wirken nach einem Neustart",
                                          "Nombres pálidos: recién añadidos, funcionan tras reiniciar" };
        g_note_n += wrap_into(g_note + g_note_n, NOTE_LINES - g_note_n, note_word(set, W), width);
    }
    g_note_h =g_note_n ? 6.0f + (float) g_note_n * SMALL_LINE : 0.0f;
    g_notes_sig = notes_sig();
}

/* text cut to `width` in a font, "…" at its end when it was longer (between letters, never inside one) */
static void fit_text(int font, char *s, size_t room, float width) {
    if (shim_menu_text_width(font, s) <= width) return;
    size_t n = strlen(s);
    while (n > 0) {
        do n--; while (n > 0 && ((unsigned char) s[n] & 0xC0) == 0x80);
        char t[400];
        snprintf(t, sizeof(t), "%.*s\xE2\x80\xA6", (int) n, s);
        if (shim_menu_text_width(font, t) <= width || n == 0) { snprintf(s, room, "%s", t); return; }
    }
}

/*
 * THE WIDEST PAPER AND A LONG NAME (the owner, 06.10.2026: "a long name blows
 * up the Mods screen - nothing is visible and a mod cannot be tapped; cut long
 * names, with an ellipsis").
 *
 * The MODS list and a mod's page are as wide as their widest line (lay_out),
 * and a player's mod's name is one of those lines. Nothing held the paper
 * back, so a mod named by its mod.json or the converter's metadata.xml - a
 * Steam title, "Fiend Folio's Rocks and Scenaries Refreshed ..." - widened it
 * past both edges of the screen. Nothing is scaled: every piece is drawn at
 * 100 %, the paper only grew, centred, and filled the glass edge to edge (it
 * read as zoomed in), its rows ran under BACK and SELECT and off the screen.
 *
 * Now the paper grows by its bands as before, but no wider than the game's
 * design width (480 units, screen.c MIN_W) - or the canvas, were one narrower -
 * with a band of wall on each side. The game's own menus keep their size on a
 * wider screen too; what the bands no longer give, a name gives up: on the
 * list it is cut, by the menu font's own glyph widths (0x8bdec8 sums their
 * advances; the fonts have no kerning), between characters, with the font's
 * own "…" (port_menufont16bold has it: tools/menu_fonts.py MUST). A name
 * that fits is shown whole, as it was. The PC cuts nothing by width: it prints
 * "%d.%s" into 64 bytes (_snprintf, isaac-ng.exe 0x8e43a8) and the paper is
 * what it is.
 *
 * Only the label is cut: the name itself (usermods.c) stays whole everywhere
 * else - on the mod's page its title goes onto as many lines as it takes
 * (title_build), the popup and the details cut their own copies as they did.
 * A mod's setting label on its page is cut the same way, should it be wider
 * than the paper with its widest value beside it; a choice's word wider than
 * half such a line is cut to the half (mod.json allows 24 characters of each,
 * and 24 W's are 456 units).
 *
 * CLEAR OF BACK AND SELECT (the owner on the phone, 06.10.2026: with FULLSCREEN
 * off the paper still went under the corner papers - "you could get a case
 * where a mod's settings cannot be tapped"). The game draws its two corner
 * papers over the page (0x7697f4): BACK at (10, H - 55), SELECT at (W - 80,
 * H - 75) (0x7699a4, 0x769a60, 0x769a70), twenty lower on a narrow low canvas
 * (screen.c shim_screen_papers: 480 x 270, FULLSCREEN off), and the MODS
 * page's screen (3) does not slide them. BackSelectWidget.anm2 (every language
 * the same) cuts BACK 96 x 96 and SELECT 112 x 128 with the pivot at (16, 16),
 * and their ink runs from x 8 and x 3 of the frame: SELECT covers x >= W - 93,
 * BACK x <= 89 - and this page gives every finger left of 95 near the bottom
 * to BACK (on_back). A list scrolls every row past the bottom, so the rule is
 * across: no row's label, ON/OFF or arrow left of 95 or right of W - 93. A row
 * stands centred on the paper, which stands centred on the canvas, so where a
 * row reaches depends on its own width alone, not on the paper's - the line is
 * held to what fits between the two (line_cap), and the paper to the bands
 * that line needs: on FULLSCREEN off (480) a line of 295 and the paper the
 * language's own; on 585 (FULLSCREEN on) the design bound above (424).
 */
#define DESIGN_W        480.0f      /* the game's design width (screen.c MIN_W) */
#define PAPER_AIR       24.0f       /* wall left on each side of the widest paper: a band */
#define BACK_TAP_W      95.0f       /* a finger left of this near the bottom is BACK's (on_back); its ink ends at 89 */
#define SELECT_INK_IN   93.0f       /* SELECT's ink from W - 93: placed at W - 80, pivot 16, ink from x 3 of its frame */
#define LABELS          256         /* usermods.c MODS_MAX (hotfix 1.75: 64 -> 256): every name cut to the paper */
#define TITLE_LINES     4           /* a name is at most 95 bytes: four lines of the narrowest paper hold it */
#define TITLE_STEP      24.0f       /* one title line under the other: 16bold's line, a unit less */

/*
 * The widest LINE (cursor, name, gap, value - a row's own width, as `need` in
 * lay_out counts it, less the centring cursor) a row may have: the widest
 * paper the canvas holds (whole bands, a band of wall on each side) less its
 * margins, and clear of BACK and SELECT. A line of width t starts at the paper's
 * middle less ceil(t / 2) and its label a cursor further on.
 */
static float line_cap(void) {
    float w = shim_screen_width();
    float avail = (w < DESIGN_W ? w : DESIGN_W) - 2.0f * PAPER_AIR;
    int bands = avail > 256.0f ? (int) floorf((avail - 256.0f) / (float) MD_BAND_W) : 0;
    float cap = (float) (256 + bands * MD_BAND_W) - (float) TW_CURSOR_W - 2.0f * (float) MD_MARGIN;
    float mid = half_x() + 240.0f;                              /* the paper's middle: paper_x is 240 less half of it */
    float left = 2.0f * (mid + (float) TW_CURSOR_W - BACK_TAP_W);
    float right = 2.0f * (w - SELECT_INK_IN - mid) + 1.0f;
    if (left < cap) cap = left;
    if (right < cap) cap = right;
    return floorf(cap);
}
static float g_line;                        /* the widest line now, laid out (lay_out): the cut texts' measure */

/*
 * A word of the port's own on the MODS list or a mod's page, wider than line_cap lets a line be: RESTART TO
 * APPLY in Russian (274) on the 480 canvas ran a unit under SELECT's corner, where the list scrolls it at its
 * foot (hotfix 1.74, tweaksdesk/test_restart174.py). Such a word slides left until it is clear of SELECT - never
 * into BACK's band; a word too wide for both stays centred.
 */
static float clear_of_corners(float name_x, float name_w) {
    float x = half_x() + (float) S()->paper_x + name_x;          /* where it starts on the canvas */
    float over = x + name_w - (shim_screen_width() - SELECT_INK_IN);
    if (over > 0.0f && x - over >= BACK_TAP_W) name_x -= ceilf(over);
    return name_x;
}

/* a cut copy of a text, made again when the text or the room changes */
typedef struct { char src[160], text[160]; float room; int made; } cut_t;
static const char *cut_copy(cut_t *c, const char *full, float room) {
    if (!c->made || c->room != room || strcmp(c->src, full)) {
        snprintf(c->src, sizeof(c->src), "%s", full);
        snprintf(c->text, sizeof(c->text), "%s", full);
        if (room > 0.0f) fit_text(FONT_ROW, c->text, sizeof(c->text), room);
        /* cut after a word: "Girls of the apocalypse…", not "apocalypse …" */
        size_t n = strlen(c->text);
        if (n > 3 && !strcmp(c->text + n - 3, "\xE2\x80\xA6")) {
            size_t e = n - 3;
            while (e > 0 && c->text[e - 1] == ' ') e--;
            if (e > 0 && e < n - 3) memmove(c->text + e, c->text + n - 3, 4);
        }
        c->room = room;
        c->made = 1;
    }
    return c->text;
}

static float g_label_room;                  /* the MODS list: the room a player's mod's name has (lay_out) */
static float g_uset_room[USET_MAX];         /* a mod's page: the room each of its settings' labels has */
static cut_t g_label[LABELS], g_uset_cut[USET_MAX], g_uval_cut[USET_MAX];

/* the list's label of the player's mod k: its name, cut to the paper */
static const char *list_label(int k) {
    (void) S();                             /* laid out: the room is the paper's now */
    const char *full = shim_usermods_name(k);
    return k >= 0 && k < LABELS ? cut_copy(&g_label[k], full, g_label_room) : full;
}

/* the open mod's setting k's label, cut to the paper beside its widest value */
static const char *uset_label(int k) {
    (void) S();
    const char *full = shim_usermods_setting_label(g_user, k);
    return k >= 0 && k < USET_MAX ? cut_copy(&g_uset_cut[k], full, g_uset_room[k]) : full;
}

/* the word of the open mod's choice k for value v, cut to the width its row is laid out for (lay_out) */
static const char *uset_value_label(int k, int v) {
    (void) S();
    const char *full = shim_usermods_setting_value_label(g_user, k, v);
    return k >= 0 && k < USET_MAX ? cut_copy(&g_uval_cut[k], full, g_uset_w[k]) : full;
}

/*
 * A MOD'S TITLE on its page and its details: the whole name, in the big cut
 * when it fits the paper, else in the row's cut - on more lines when one is
 * not enough, broken between words (inside a word only when the word alone is
 * wider than the paper). The lines under it go down by the lines it added.
 */
static int g_title_n;
static char g_title[TITLE_LINES][128];

static void title_build(const char *name, float inner) {
    g_title_n = 0;
    if (shim_menu_text_width(FONT_TITLE, name) <= inner || shim_menu_text_width(FONT_ROW, name) <= inner) {
        snprintf(g_title[g_title_n++], sizeof(g_title[0]), "%s", name);
        return;
    }
    const char *p = name;
    while (*p == ' ') p++;
    while (*p && g_title_n < TITLE_LINES) {
        char *line = g_title[g_title_n++];
        size_t room = sizeof(g_title[0]), n = 0;
        const char *best = NULL, *q = p;
        /* as many whole words as fit */
        while (*q) {
            const char *e = q;
            while (*e && *e != ' ') e++;
            size_t take = (size_t) (e - p);
            if (take >= room) break;
            memcpy(line, p, take);
            line[take] = 0;
            if (shim_menu_text_width(FONT_ROW, line) > inner) break;
            best = e;
            q = e;
            while (*q == ' ') q++;
        }
        if (!best) {
            /* one word wider than the paper: as many of its characters as fit, one at the least */
            const char *e = p;
            do e++; while (*e && ((unsigned char) *e & 0xC0) == 0x80);
            best = e;
            while (*best && *best != ' ') {
                e = best;
                do e++; while (*e && ((unsigned char) *e & 0xC0) == 0x80);
                size_t take = (size_t) (e - p);
                if (take >= room) break;
                memcpy(line, p, take);
                line[take] = 0;
                if (shim_menu_text_width(FONT_ROW, line) > inner) break;
                best = e;
            }
        }
        n = (size_t) (best - p);
        if (n >= room) n = room - 1;
        memcpy(line, p, n);
        line[n] = 0;
        p = best;
        while (*p == ' ') p++;
    }
    /* more than the lines hold (not with a name of 95 bytes): the last line says so */
    if (*p) {
        char *last = g_title[g_title_n - 1];
        size_t n = strlen(last);
        snprintf(last + n, sizeof(g_title[0]) - n, " %s", p);
        fit_text(FONT_ROW, last, sizeof(g_title[0]), inner);
    }
    if (!g_title_n) g_title[g_title_n++][0] = 0;
}

/*
 * A MOD IN RED (hotfix 1.71, the owner 05.10.2026: "paint their name red"):
 * its name, in the MODS list and over its page, when the port does not use
 * all of it - some of its files are not used (the guard handed the game's in
 * their place: a file that would crash the game), or nothing in it is a file
 * the port reads at all (a PC script mod dropped into the folder by hand).
 */
static int mod_red(int i) { return shim_usermods_bare(i) || shim_usermods_guard_game(i) > 0; }

/*
 * THE LINES OVER THE TITLE of a player's mod's page (the owner, 05.10.2026):
 * at most two short lines in the small cut, each a row the cursor and a
 * finger reach, with the MODS list's arrow beside it, opening the details
 * (THE DETAILS). All red, plain words, nothing grey (hotfix 1.71: "what does
 * 'Check: 2 notes' even mean"):
 *   - the mod breaks with a mod that is on - an .anm2 of one cuts a sheet of
 *     the other, of another size (usermods.c, THE PAIRS): "Conflicts with:
 *     Reflashed", or with how many;
 *   - the port does not use some of its files: "Not used: 2 of 3 files -
 *     details";
 *   - nothing in it is for the port: "Scripts are not run - nothing for the
 *     port to use".
 * What is only a note (tables merged, files completed, mods over each other)
 * stays on the details page. The title and all under it go down by the lines'
 * height. Until the pairs are worked out there is no conflict line; the page
 * is laid again when they are (notes_sig).
 */
static float g_head_h;              /* how far the lines put the title and all under it down */

static void user_links_build(float paper_w) {
    int set = set_index(), lang = lang_of(set), was = g_links;
    float room = paper_w - 2.0f * (float) MD_MARGIN - 2.0f * (float) TW_CURSOR_W - (float) TW_GAP - (float) MD_ARROW_W;
    if (room > g_line - (float) TW_CURSOR_W - (float) TW_GAP - (float) MD_ARROW_W)     /* clear of BACK and SELECT */
        room = g_line - (float) TW_CURSOR_W - (float) TW_GAP - (float) MD_ARROW_W;
    g_links = 0;
    int first, n = shim_usermods_conflicts(g_user, &first);
    if (n > 0) {
        static const char *const ONE[4] = { "Conflicts with: %s", "Конфликтует с: %s", "Konflikt mit: %s", "Conflicto con: %s" };
        static const char *const MANY[4] = { "Conflicts with %d mods", "Конфликтует с %d модами", "Konflikt mit %d Mods",
                                             "Conflicto con %d mods" };
        if (n == 1) snprintf(g_link_text[g_links], sizeof(g_link_text[0]), ONE[lang], shim_usermods_name(first));
        else snprintf(g_link_text[g_links], sizeof(g_link_text[0]), MANY[lang], n);
        fit_text(FONT_SMALL, g_link_text[g_links], sizeof(g_link_text[0]), room);
        g_link_id[g_links++] = R_U_CONFLICT;
    }
    int bare = shim_usermods_bare(g_user), unused = shim_usermods_guard_game(g_user), of = shim_usermods_files(g_user);
    if (bare && g_links < 2) {
        static const char *const SCRIPT[4] = { "Scripts do not run here - nothing to use",
                                               "Скрипты не запускаются - порту нечего брать",
                                               "Skripte laufen nicht - nichts zu nutzen",
                                               "Los scripts no corren - nada que usar" };
        static const char *const NONE[4] = { "Nothing here for the port to use", "Порту здесь нечего использовать",
                                             "Nichts, was der Port nutzen kann", "Nada que el port pueda usar" };
        snprintf(g_link_text[g_links], sizeof(g_link_text[0]), "%s", (bare == 2 ? SCRIPT : NONE)[lang]);
        fit_text(FONT_SMALL, g_link_text[g_links], sizeof(g_link_text[0]), room);
        g_link_id[g_links++] = R_U_CHECK;
    } else if (unused > 0 && g_links < 2) {
        if (of < unused) of = unused;
        char *t = g_link_text[g_links];
        size_t tn = sizeof(g_link_text[0]);
        if (lang == 1)
            snprintf(t, tn, "%s %d %s из %d - подробнее", unused % 10 == 1 && unused % 100 != 11 ? "Не используется" : "Не используются",
                     unused, ru_form(unused, "файл", "файла", "файлов"), of);
        else if (lang == 2) snprintf(t, tn, "Nicht verwendet: %d von %d %s - Details", unused, of, of == 1 ? "Datei" : "Dateien");
        else if (lang == 3) snprintf(t, tn, "%s %d de %d %s - detalles", unused == 1 ? "No se usa" : "No se usan", unused, of,
                                     of == 1 ? "archivo" : "archivos");
        else snprintf(t, tn, "Not used: %d of %d %s - details", unused, of, of == 1 ? "file" : "files");
        fit_text(FONT_SMALL, t, tn, room);
        g_link_id[g_links++] = R_U_CHECK;
    }
    g_head_h = g_links ? (float) g_links * SMALL_LINE + 4.0f : 0.0f;
    /* the rows the cursor was on stay under it when the lines come or go */
    if (g_page == &PAGES[PAGE_USER] && g_cursor >= was) g_cursor += g_links - was;
    if (g_cursor < 0) g_cursor = 0;
}

/*
 * THE DETAILS of a player's mod (PAGE_DETAIL; the owner, 05.10.2026, variant
 * B of other stuff/mods-conflict-mockup), behind the lines over its title and
 * the popup's DETAILS: its name; then for each mod that is on and has to do
 * with it, that mod's name and what breaks between them as a table - the
 * picture, the size the game is handed, the size the frames were cut for, how
 * many animations cut it - with what to do about it, and how many files one
 * takes from the other; then the port's check's notes, and the path of
 * check.txt once, at the foot. It scrolls as the pages do; BACK goes back to
 * where it was opened from. Laid out once (lay_out) as lines on the paper.
 */
typedef struct { short x, y; signed char font, ink; char text[160]; } dline_t;   /* x < 0: centred; ink 0, 1 red, 2 grey */
#define DETAIL_LINES 200
static dline_t g_dl[DETAIL_LINES];
static int g_dl_n;
static float g_dl_bottom;

static void dl_put(float x, float y, int font, int ink, const char *text) {
    if (g_dl_n >= DETAIL_LINES || !text[0]) return;
    dline_t *d = &g_dl[g_dl_n++];
    d->x = (short) x;
    d->y = (short) y;
    d->font = (signed char) font;
    d->ink = (signed char) ink;
    snprintf(d->text, sizeof(d->text), "%s", text);
}

/* a paragraph, wrapped to `width`: centred, or from `left` - a list item with its bullet, the lines after under its text */
static float dl_para(float y, int font, int ink, const char *text, float left, float width, int centre, int bullet) {
    char lines[24][128];
    static const char BULLET[] = "\xE2\x80\xA2 ";
    float hang = bullet ? shim_menu_text_width(font, BULLET) : 0.0f;
    int n = wrap_font(font, lines, 24, text, width - hang);
    float step = font == FONT_SMALL ? SMALL_LINE : (font == FONT_ROW ? 24.0f : 32.0f);
    for (int i = 0; i < n; i++) {
        if (centre) dl_put(-1.0f, y, font, ink, lines[i]);
        else {
            if (bullet && i == 0) dl_put(left, y, font, ink, "\xE2\x80\xA2");
            dl_put(left + hang, y, font, ink, lines[i]);
        }
        y += step;
    }
    return y;
}

/* "4 times larger", "4 times smaller", "of another size": what the frames were cut for, against what is served */
static void ratio_word(char *out, size_t room, int lang, const unsigned *sw, const unsigned *sh, const unsigned *iw,
                       const unsigned *ih, int n) {
    int up = 0, down = 0;
    for (int k = 0; k < n; k++) {
        int u = sw[k] && iw[k] % sw[k] == 0 ? (int) (iw[k] / sw[k]) : 0, d = iw[k] && sw[k] % iw[k] == 0 ? (int) (sw[k] / iw[k]) : 0;
        if (u < 2 || (unsigned) u * sh[k] != ih[k]) u = 0;
        if (d < 2 || (unsigned) d * ih[k] != sh[k]) d = 0;
        if (k == 0) { up = u; down = d; }
        if (u != up) up = 0;
        if (d != down) down = 0;
    }
    static const char *const UP[4] = { "%d times larger", "в %d %s крупнее", "%d-mal so groß", "%d veces más grandes" };
    static const char *const DOWN[4] = { "%d times smaller", "в %d %s мельче", "%d-mal kleiner", "%d veces más pequeñas" };
    static const char *const OTHER[4] = { "of another size", "другого размера", "einer anderen Größe", "de otro tamaño" };
    int k = up ? up : down;
    if (!k) { snprintf(out, room, "%s", OTHER[lang]); return; }
    if (lang == 1) snprintf(out, room, up ? UP[1] : DOWN[1], k, ru_form(k, "раз", "раза", "раз"));
    else snprintf(out, room, up ? UP[lang] : DOWN[lang], k);
}

/* the breakages between mods i and j, those whose .anm2 are `theirs` (j's) or i's own */
#define PAIR_ROWS 32
typedef struct { char leaf[96]; unsigned sw, sh, iw, ih; int anms, own; } brow_t;
static int pair_rows(int i, int j, int theirs, brow_t *out) {
    int n = 0;
    for (int k = 0; n < PAIR_ROWS; k++) {
        int mine, anms, own;
        unsigned sw, sh, iw, ih;
        char sheet[300];
        if (!shim_usermods_pair_broken(i, j, k, &mine, sheet, sizeof(sheet), &sw, &sh, &iw, &ih, &anms, &own)) break;
        if (mine == theirs) continue;
        const char *leaf = strrchr(sheet, '/');
        snprintf(out[n].leaf, sizeof(out[n].leaf), "%s", leaf ? leaf + 1 : sheet);
        out[n].sw = sw; out[n].sh = sh; out[n].iw = iw; out[n].ih = ih;
        out[n].anms = anms;
        out[n].own = own;
        n++;
    }
    return n;
}

static void detail_build(float inner) {
    int set = set_index(), lang = lang_of(set), count = shim_usermods_count();
    float left = (float) MD_MARGIN;
    g_dl_n = 0;
    static const char *const SUB[4] = { "Conflicts and check", "Конфликты и проверка", "Konflikte und Prüfung",
                                        "Conflictos y revisión" };
    float y = (float) g_live.row0 + 4.0f - CAP_TOP[FONT_SMALL];
    y = dl_para(y, FONT_SMALL, 2, SUB[lang], left, inner, 1, 0) + 6.0f;
    for (int j = 0; j < count; j++) {
        if (j == g_user || !shim_usermods_on(j)) continue;
        int mine_over, files;
        unsigned kinds;
        char lang_only[16], text[600], name[160];
        int over = shim_usermods_pair_over(g_user, j, &mine_over, &files, &kinds, lang_only, sizeof(lang_only));
        brow_t rows[2][PAIR_ROWS];
        int nr[2] = { pair_rows(g_user, j, 1, rows[0]), pair_rows(g_user, j, 0, rows[1]) };
        if (!over && !nr[0] && !nr[1]) continue;
        /* the other mod's name, a heading */
        snprintf(name, sizeof(name), "%s", shim_usermods_name(j));
        fit_text(FONT_ROW, name, sizeof(name), inner);
        y += 8.0f;
        dl_put(-1.0f, y - CAP_TOP[FONT_ROW], FONT_ROW, 0, name);
        y += 28.0f;
        for (int side = 0; side < 2; side++) {
            int n = nr[side];
            if (!n) continue;
            const brow_t *r = rows[side];
            unsigned sw[PAIR_ROWS], sh[PAIR_ROWS], iw[PAIR_ROWS], ih[PAIR_ROWS];
            int all_own = 1;
            for (int k = 0; k < n; k++) { sw[k] = r[k].sw; sh[k] = r[k].sh; iw[k] = r[k].iw; ih[k] = r[k].ih; all_own &= r[k].own == 1; }
            char ratio[64];
            ratio_word(ratio, sizeof(ratio), lang, sw, sh, iw, ih, n);
            static const char *const BROKEN[4] = { "Broken pictures: %d", "Сломано картинок: %d", "Kaputte Bilder: %d",
                                                   "Imágenes rotas: %d" };
            snprintf(text, sizeof(text), BROKEN[lang], n);
            y = dl_para(y, FONT_SMALL, 1, text, left, inner, 1, 0) + 2.0f;
            /* side 0: the other's animations over this mod's pictures; side 1: this mod's over the other's */
            static const char *const THEIRS[4] = {
                "The animations of %s are cut for pictures %s, and the game gets this mod's:",
                "Анимации мода %s нарезаны под картинки %s, а игра берёт картинки этого мода:",
                "Die Animationen von %s sind für Bilder %s geschnitten, das Spiel bekommt die dieses Mods:",
                "Las animaciones de %s están cortadas para imágenes %s, y el juego recibe las de este mod:" };
            static const char *const MINE[4] = {
                "The animations of this mod are cut for pictures %s, and the game gets those of %s:",
                "Анимации этого мода нарезаны под картинки %s, а игра берёт картинки мода %s:",
                "Die Animationen dieses Mods sind für Bilder %s geschnitten, das Spiel bekommt die von %s:",
                "Las animaciones de este mod están cortadas para imágenes %s, y el juego recibe las de %s:" };
            if (side == 0) snprintf(text, sizeof(text), THEIRS[lang], shim_usermods_name(j), ratio);
            else snprintf(text, sizeof(text), MINE[lang], ratio, shim_usermods_name(j));
            y = dl_para(y, FONT_SMALL, 0, text, left, inner, 1, 0) + 4.0f;
            /* the table: picture, the size served, the size cut for, how many .anm2 */
            static const char *const COL[4][4] = { { "Picture", "Size", "Needs", "Anims" },
                                                   { "Картинка", "Размер", "Нужно", "Анимаций" },
                                                   { "Bild", "Größe", "Nötig", "Animationen" },
                                                   { "Imagen", "Tamaño", "Necesita", "Animaciones" } };
            char cell[PAIR_ROWS][4][96];
            float wcol[4];
            for (int c = 0; c < 4; c++) wcol[c] = shim_menu_text_width(FONT_SMALL, COL[lang][c]);
            for (int k = 0; k < n; k++) {
                snprintf(cell[k][0], sizeof(cell[k][0]), "%s", r[k].leaf);
                snprintf(cell[k][1], sizeof(cell[k][1]), "%ux%u", r[k].sw, r[k].sh);
                snprintf(cell[k][2], sizeof(cell[k][2]), "%ux%u", r[k].iw, r[k].ih);
                snprintf(cell[k][3], sizeof(cell[k][3]), "%d", r[k].anms);
                for (int c = 1; c < 4; c++) { float w = shim_menu_text_width(FONT_SMALL, cell[k][c]); if (w > wcol[c]) wcol[c] = w; }
            }
            float gap = 12.0f, rest = wcol[1] + wcol[2] + wcol[3] + 3.0f * gap, name_room = inner - rest;
            for (int k = 0; k < n; k++) {
                fit_text(FONT_SMALL, cell[k][0], sizeof(cell[k][0]), name_room);
                float w = shim_menu_text_width(FONT_SMALL, cell[k][0]);
                if (w > wcol[0]) wcol[0] = w;
            }
            if (wcol[0] > name_room) wcol[0] = name_room;
            float total = wcol[0] + rest, x0 = left + floorf((inner - total) * 0.5f), cx[4];
            cx[0] = x0;
            for (int c = 1; c < 4; c++) cx[c] = cx[c - 1] + wcol[c - 1] + gap;
            for (int c = 0; c < 4; c++) dl_put(cx[c], y, FONT_SMALL, 2, COL[lang][c]);
            y += SMALL_LINE + 2.0f;
            for (int k = 0; k < n; k++) {
                for (int c = 0; c < 4; c++) dl_put(cx[c], y, FONT_SMALL, c == 1 ? 1 : 0, cell[k][c]);
                y += SMALL_LINE;
            }
            y += 4.0f;
            /* what to do: the cutting mod above makes its own copy the one served - when it has one */
            static const char *const ORDER[4] = {
                "What to do: put %s above - then nothing breaks - or turn one of the mods off.",
                "Как быть: поставить %s выше - тогда сломанного не будет - или выключить один из модов.",
                "Was tun: %s höher stellen - dann bricht nichts - oder einen der Mods abschalten.",
                "Qué hacer: poner %s más arriba - así no se rompe nada - o desactivar uno de los mods." };
            static const char *const SELF[4] = { "this mod", "этот мод", "diesen Mod", "este mod" };
            static const char *const OFF[4] = { "What to do: turn one of the mods off.", "Как быть: выключить один из модов.",
                                                "Was tun: einen der Mods abschalten.", "Qué hacer: desactivar uno de los mods." };
            if (all_own) snprintf(text, sizeof(text), ORDER[lang], side == 0 ? shim_usermods_name(j) : SELF[lang]);
            else snprintf(text, sizeof(text), "%s", OFF[lang]);
            y = dl_para(y, FONT_SMALL, 0, text, left, inner, 1, 0) + 6.0f;
        }
        if (over) {
            char fw[48], kw[200];
            files_word(fw, sizeof(fw), set, files);
            kinds_word(kw, sizeof(kw), set, kinds);
            static const char *const OVER[4] = { "Overrides %s of %s: %s.", "Перекрывает %s мода %s: %s.",
                                                 "Überdeckt %s von %s: %s.", "Tapa %s de %s: %s." };
            static const char *const UNDER[4] = { "%s overrides %s of this mod: %s.", "%s перекрывает %s этого мода: %s.",
                                                  "%s überdeckt %s dieses Mods: %s.", "%s tapa %s de este mod: %s." };
            if (mine_over) snprintf(text, sizeof(text), OVER[lang], fw, shim_usermods_name(j), kw);
            else snprintf(text, sizeof(text), UNDER[lang], shim_usermods_name(j), fw, kw);
            if (lang_only[0]) {
                static const char *const ONLY[4] = { " Only in the %s set.", " Только в наборе %s.", " Nur im Satz %s.",
                                                     " Solo en el conjunto %s." };
                size_t at = strlen(text);
                snprintf(text + at, sizeof(text) - at, ONLY[lang], lang_only);
            }
            y = dl_para(y, FONT_SMALL, 0, text, left, inner, 1, 0) + 4.0f;
        }
    }
    /* the port's check */
    check_notes(set);
    if (g_cn_n) {
        static const char *const HEAD[4] = { "Check", "Проверка", "Prüfung", "Revisión" };
        y += 10.0f;
        dl_put(-1.0f, y - CAP_TOP[FONT_ROW], FONT_ROW, 0, HEAD[lang]);
        y += 28.0f;
        for (int k = 0; k < g_cn_n; k++) y = dl_para(y, FONT_SMALL, 0, g_cn[k], left + 8.0f, inner - 8.0f, 0, 1) + 2.0f;
    }
    static const char *const FOOT[4] = { "In full: mods/%s.check.txt", "Всё подробно: mods/%s.check.txt",
                                         "Ausführlich: mods/%s.check.txt", "Completo: mods/%s.check.txt" };
    char text[300];
    snprintf(text, sizeof(text), FOOT[lang], shim_usermods_id(g_user));
    y = dl_para(y + 8.0f, FONT_SMALL, 2, text, left, inner, 1, 0);
    g_dl_bottom = y;
}

/* a mod's description, broken into lines of `width`: at spaces, at its own line breaks */
static void wrap_description(const char *text, float width) {
    g_desc_n = wrap_into(g_desc, DESC_LINES, text, width);
}

/* text broken into at most `max` lines of `width`; how many */
static int wrap_into(char (*out)[128], int max, const char *text, float width) {
    return wrap_font(FONT_SMALL, out, max, text, width);
}

/* the same, in a cut of the menu font */
static int wrap_font(int font, char (*out)[128], int max, const char *text, float width) {
    int count = 0;
    const char *p = text;
    while (*p && count < max) {
        char line[128] = "";
        size_t n = 0;
        const char *best = NULL;              /* the end of the last word that fitted */
        const char *q = p;
        while (*q && *q != '\n') {
            const char *word_end = q;
            while (*word_end && *word_end != ' ' && *word_end != '\n') word_end++;
            size_t take = (size_t) (word_end - p);
            if (take >= sizeof(line)) break;
            memcpy(line, p, take);
            line[take] = 0;
            if (best && shim_menu_text_width(font, line) > width) break;
            best = word_end;
            q = word_end;
            while (*q == ' ') q++;
        }
        if (!best) {                           /* one word longer than the line: cut it */
            best = p;
            while (*best && *best != '\n' && (size_t) (best - p) < sizeof(line) - 1) best++;
        }
        n = (size_t) (best - p);
        memcpy(out[count], p, n);
        out[count][n] = 0;
        count++;
        p = best;
        while (*p == ' ') p++;
        if (*p == '\n') p++;
    }
    return count;
}

/* the widest value setting k of mod i can show: ON/OFF, a choice's words, a number's on its grid */
static float uset_widest(int i, int k, float onoff) {
    int kind = shim_usermods_setting_kind(i, k);
    if (kind == USET_KIND_SWITCH) return onoff;
    float w = 0.0f;
    if (kind == USET_KIND_CHOICE) {
        for (int v = 0; v < shim_usermods_setting_count(i, k); v++) {
            float t = shim_menu_text_width(FONT_ROW, shim_usermods_setting_value_label(i, k, v));
            if (t > w) w = t;
        }
        return w;
    }
    int lo, hi, st;
    shim_usermods_setting_range(i, k, &lo, &hi, &st);
    for (int v = lo; st > 0 && v <= hi; v += st) {
        char text[16];
        snprintf(text, sizeof(text), "%d", v);
        float t = shim_menu_text_width(FONT_ROW, text);
        if (t > w) w = t;
    }
    return w;
}

static void lay_out(void) {
    int set = set_index();
    shim_usermods_lang(set);
    const tw_set_t *base = &MD_SET[set];
    g_live = *base;
    g_live_set = set;
    g_live_dirty = 0;
    g_live_w = shim_screen_width();
    float onoff = (float) (base->on_w > base->off_w ? base->on_w : base->off_w);
    float valued = (float) TW_GAP + onoff + (float) TW_GAP + (float) MD_ARROW_W;
    /*
     * The port's own rows widen the paper to whatever they need (as ever); a line of a mod's own text - a name, a
     * setting, the title on its page - only up to the bands line_cap allows, and is cut to that (THE WIDEST PAPER).
     */
    float widest = 0.0f, texts = 0.0f, cap = line_cap();
    #define WIDER(w) do { float w_ = (w); if (w_ > widest) widest = w_; } while (0)
    #define WIDER_TEXT(w) do { float w_ = (w); if (w_ > texts) texts = w_; } while (0)
    for (int i = 0; i < MD_ROW_COUNT; i++) WIDER(TW_CURSOR_W + base->word_w[MD_NAME_FRAME + i] + valued);
    WIDER(MX_W[set][MX_HEADING]);
    WIDER(TW_CURSOR_W + MX_W[set][MX_ADD]);
    WIDER(TW_CURSOR_W + MX_W[set][MX_DUMP]);
    WIDER(TW_CURSOR_W + MX_W[set][MX_RESTART]);
    int n = shim_usermods_count();
    for (int k = 0; k < n; k++)
        WIDER_TEXT(TW_CURSOR_W + shim_menu_text_width(FONT_ROW, shim_usermods_name(k)) + valued);
    /* a mod's page: its rows, and its title in the smaller cut at the least (on more lines past the widest paper) */
    WIDER(TW_CURSOR_W + MX_W[set][MX_ENABLED] + TW_GAP + onoff);
    WIDER(TW_CURSOR_W + MX_W[set][MX_SURE]);
    if (g_page == &PAGES[PAGE_USER] || g_page == &PAGES[PAGE_DETAIL])
        WIDER_TEXT(shim_menu_text_width(FONT_ROW, shim_usermods_name(g_user)) - TW_CURSOR_W);
    /* the mods' own settings - of every mod, so the list and each mod's page keep one width; the heading SETTINGS */
    for (int k = 0; k < USET_MAX; k++) g_uset_w[k] = 0.0f;
    for (int i = 0; i < n; i++) {
        int ns = shim_usermods_settings(i);
        if (ns) WIDER(MX_W[set][MX_SETTINGS]);
        for (int k = 0; k < ns && k < USET_MAX; k++) {
            float vw = uset_widest(i, k, onoff);
            if (i == g_user) g_uset_w[k] = vw;
            WIDER_TEXT(TW_CURSOR_W + shim_menu_text_width(FONT_ROW, shim_usermods_setting_label(i, k)) + TW_GAP + vw);
        }
    }
    #undef WIDER_TEXT
    #undef WIDER
    /* bands: the port's rows' own, or the texts' up to the most a line of `cap` takes */
    #define BANDS_FOR(line_w) ((line_w) + (float) TW_CURSOR_W + 2.0f * (float) MD_MARGIN - 256.0f > 0.0f         ? (int) ceilf(((line_w) + (float) TW_CURSOR_W + 2.0f * (float) MD_MARGIN - 256.0f) / (float) MD_BAND_W) : 0)
    int own = BANDS_FOR(widest), most = (int) floorf((cap + (float) TW_CURSOR_W + 2.0f * (float) MD_MARGIN - 256.0f)
                                                     / (float) MD_BAND_W);
    int want = BANDS_FOR(texts);
    #undef BANDS_FOR
    if (want > most) want = most;
    g_bands = want > own ? want : own;
    g_live.paper_w = (short) (256 + g_bands * MD_BAND_W);
    g_live.paper_x = (short) (240 - g_live.paper_w / 2);

    float inner = (float) g_live.paper_w - 2.0f * (float) MD_MARGIN;
    /* the widest line: what the paper holds - less its margins and a cursor's width (the line stands half of it right
       of the middle) - and never past line_cap, even on a paper the port's own rows made wider */
    g_line = inner - (float) TW_CURSOR_W;
    if (g_line > cap) g_line = cap;
    /* a name's room: the line less the cursor and what stands after the name */
    g_label_room = g_line - (float) TW_CURSOR_W - valued;
    /* a setting's row: label, gap, its widest value; a choice's word wider than half the line gives up the rest */
    float line = g_line - (float) TW_CURSOR_W, half = floorf((line - (float) TW_GAP) * 0.5f);
    for (int k = 0; k < USET_MAX; k++) {
        if (g_page == &PAGES[PAGE_USER] && k < shim_usermods_settings(g_user) && g_uset_w[k] > half
            && shim_menu_text_width(FONT_ROW, shim_usermods_setting_label(g_user, k)) + (float) TW_GAP + g_uset_w[k] > line)
            g_uset_w[k] = half;
        g_uset_room[k] = line - (float) TW_GAP - g_uset_w[k];
    }
    g_text_h = 0.0f;
    g_head_h = 0.0f;
    float title_more = 0.0f;
    if (g_page == &PAGES[PAGE_USER] || g_page == &PAGES[PAGE_DETAIL]) {
        title_build(shim_usermods_name(g_user), inner);
        g_title_font = g_title_n == 1 && shim_menu_text_width(FONT_TITLE, g_title[0]) <= inner ? FONT_TITLE : FONT_ROW;
        title_more = (float) (g_title_n - 1) * TITLE_STEP;
        if (g_page == &PAGES[PAGE_DETAIL]) g_live.row0 = (short) (base->row0 + (int) title_more);
    }
    /* a mod's page: the lines over its title first - they are rows, and put the title and the rest down */
    if (g_page == &PAGES[PAGE_USER]) {
        user_links_build((float) g_live.paper_w);
        g_live.row0 = (short) (base->row0 + (int) g_head_h + (int) title_more);
    }
    int rows = page_rows(g_page);
    if (g_page == &PAGES[PAGE_DETAIL]) {
        build_notes(inner);
        detail_build(inner);
        int pieces = (int) ceilf((g_dl_bottom + 6.0f - (float) g_live.row0) / (float) MD_PIECE_H);
        if (pieces < 1) pieces = 1;
        g_live.paper_h = (short) (MD_HEAD_H + pieces * MD_PIECE_H + MD_FOOT_H);
        return;
    }
    if (g_page == &PAGES[PAGE_USER]) {
        const char *v = shim_usermods_version(g_user), *a = shim_usermods_author(g_user);
        snprintf(g_facts, sizeof(g_facts), "%s%s%s", v, v[0] && a[0] ? "  -  " : "", a);
        wrap_description(shim_usermods_description(g_user), inner);
        int lines = (g_facts[0] ? 1 : 0) + g_desc_n;
        g_text_h = lines ? 4.0f + (float) lines * SMALL_LINE + 2.0f : 0.0f;
    }
    build_notes(inner);
    /* the rows' bottom with all the air among them (the last row has it all above it), and the notes under it all */
    float bottom = (float) g_live.row0 + (float) (rows - user_links()) * (float) g_live.step + row_air(rows - 1) + g_note_h;
    int pieces = (int) ceilf((bottom - (float) g_live.row0) / (float) MD_PIECE_H);
    if (pieces < 1) pieces = 1;
    g_live.paper_h = (short) (MD_HEAD_H + pieces * MD_PIECE_H + MD_FOOT_H);
}

/* a point in PAPER units -> the canvas, carried by the camera's travel */
static void to_canvas(float px, float py, float off_x, float off_y, float *cx, float *cy) {
    *cx = half_x() + (float) S()->paper_x + px + off_x;
    *cy = half_y() + (float) TW_PAPER_Y + py + off_y;
}

/* ------------------------------------------------------------ the camera */
/* under the lock */
static void open_here(unsigned long menu, const page_t *page) {
    g_page = page;
    g_sure = 0;
    g_live_dirty = 1;
    float *a = anchor_of(menu, g_page->screen);
    g_anchor_was[0] = a[0];
    g_anchor_was[1] = a[1];
    a[0] += AWAY_X;
    a[1] += AWAY_Y;
    g_base[0] = a[0];
    g_base[1] = a[1];
    g_menu = menu;
    g_open = 1;
    g_cursor = 0;
    g_finger = -1;
    g_gesture = 0;
    shim_menu_page_sound();
    LOG("%s: opened", g_page->name);
    if (shim_report_active()) report_line(g_page->name, "opened");
}

/* under the lock */
static void close_here(void) {
    if (!g_open) return;
    if (g_menu) {
        float *a = anchor_of(g_menu, g_page->screen);
        a[0] = g_anchor_was[0];
        a[1] = g_anchor_was[1];
    }
    g_open = 0;
    g_menu = 0;
    g_finger = g_opener = -1;
    g_pop = 0;
    g_pop_pending = -1;
    shim_menu_page_sound();
    LOG("%s: closed", g_page->name);
    if (shim_report_active()) report_line(g_page->name, "closed");
}

/*
 * The MODS list and a mod's settings page are one open page that changes what
 * it shows: the anchor stays away, the page goes back to its top, and BACK on a
 * settings page comes back to the list at the mod it was opened from - not to
 * the main menu (the owner, 23.09.2026).
 */
static void switch_to(const page_t *page, int cursor) {
    g_page = page;
    g_cursor = cursor;
    g_sure = 0;
    g_live_dirty = 1;
    g_finger = -1;
    g_gesture = 0;
    if (g_menu) {
        float *a = anchor_of(g_menu, g_page->screen);
        a[1] = g_base[1];
    }
    shim_menu_page_sound();
    LOG("%s: shown", g_page->name);
}

static void user_links_build(float paper_w);
static void open_settings(int mod_row) {
    if (g_page == &PAGES[PAGE_MODS] && ID(mod_row) >= R_USER) {
        g_user = ID(mod_row) - R_USER;
        g_list_cursor = mod_row;
        g_links = 0;
        switch_to(&PAGES[PAGE_USER], 0);
        user_links_build((float) g_live.paper_w);  /* the cursor starts under the lines over the title */
        if (is_heading(g_cursor)) g_cursor++;       /* the first setting, not the heading over it */
        return;
    }
    int k = settings_of(mod_row);
    if (k < 0) return;
    g_list_cursor = mod_row;
    switch_to(&PAGES[PAGE_SET0 + k], 0);
}

/* the details of the open mod (THE DETAILS): from its page's lines, or from the popup over the list */
static const page_t *g_detail_from;
static int g_detail_cursor;
static void open_detail(void) {
    g_detail_from = g_page;
    g_detail_cursor = g_cursor;
    switch_to(&PAGES[PAGE_DETAIL], 0);
}

/* BACK: from a settings page to the list, from the details where they were opened, from anything else out */
static void back_here(void) {
    if (g_page == &PAGES[PAGE_GUIDE]) close_here();
    else if (g_page == &PAGES[PAGE_DETAIL])
        switch_to(g_detail_from ? g_detail_from : &PAGES[PAGE_USER], g_detail_from ? g_detail_cursor : 0);
    else if (g_page == &PAGES[PAGE_USER]) switch_to(&PAGES[PAGE_MODS], MD_OWN + g_user);
    else if (g_page >= &PAGES[PAGE_SET0]) switch_to(&PAGES[PAGE_MODS], g_list_cursor);
    else close_here();
}


/*
 * THE POPUP (the owner, 05.10.2026: variant A of other stuff/mods-conflict-
 * mockup, with a third button). The moment the player switches on a mod that
 * breaks with a mod that is on (usermods.c, THE PAIRS) - on the MODS list or
 * on the mod's own page - the game's big prompt paper over the shaded screen
 * (tools/mods_popup.py): which two, how many pictures break and why, what to
 * do. OK leaves it so, DETAILS opens the mod's details (THE DETAILS), TURN OFF
 * switches it off again. A pad walks the three and confirm presses, BACK is
 * OK; a finger presses where it comes down. Switched on before the pairs are
 * worked out (the first seconds of a start), it is asked again every frame
 * until they are.
 */
static int g_pop_mod;               /* the mod switched on */
static int g_pop_btn;               /* the pointer: 0 OK, 1 DETAILS, 2 TURN OFF */
static char g_pop_title[64], g_pop_btn_text[3][32];
#define POP_LINES 8
#define POP_ROOM 5                  /* the lines the paper holds between the title and the buttons */
static char g_pop_line[POP_LINES][128];
static unsigned char g_pop_para[POP_LINES];  /* a line that starts a paragraph */
static int g_pop_lines;
static float g_pop_bx[3], g_pop_bw[3], g_pop_by;   /* the buttons on the canvas, as the draw laid them */

static void popup_show(int mod, int other, int more) {
    int set = set_index(), lang = lang_of(set);
    shim_usermods_lang(set);
    /* which way it breaks: the other's animations over this mod's pictures, or this mod's over the other's */
    brow_t rows[2][PAIR_ROWS];
    int nr[2] = { pair_rows(mod, other, 1, rows[0]), pair_rows(mod, other, 0, rows[1]) };
    int side = nr[1] > nr[0] ? 1 : 0, n = nr[side];
    if (!n) return;
    unsigned sw[PAIR_ROWS], sh[PAIR_ROWS], iw[PAIR_ROWS], ih[PAIR_ROWS];
    int all_own = 1;
    for (int k = 0; k < n; k++) {
        const brow_t *r = &rows[side][k];
        sw[k] = r->sw; sh[k] = r->sh; iw[k] = r->iw; ih[k] = r->ih;
        all_own &= r->own == 1;
    }
    char ratio[64], text[600];
    ratio_word(ratio, sizeof(ratio), lang, sw, sh, iw, ih, n);
    const char *me = shim_usermods_name(mod), *them = shim_usermods_name(other);
    const char *cuts = side == 0 ? them : me, *sheets = side == 0 ? me : them;
    static const char *const TITLE[4] = { "These mods conflict", "Моды конфликтуют", "Mods im Konflikt", "Mods en conflicto" };
    static const char *const PAIR[4] = { "\"%s\" and \"%s\"", "\"%s\" и \"%s\"", "\"%s\" und \"%s\"", "\"%s\" y \"%s\"" };
    static const char *const MORE[4] = { " (and %d more)", " (и ещё %d)", " (und %d weitere)", " (y %d más)" };
    static const char *const BODY[4] = {
        "Broken pictures: %d - the animations of %s are cut for pictures %s, and the game gets those of %s.",
        "Сломано картинок: %d - анимации мода %s нарезаны под картинки %s, а игра берёт картинки мода %s.",
        "Kaputte Bilder: %d - die Animationen von %s sind für Bilder %s geschnitten, das Spiel bekommt die von %s.",
        "Imágenes rotas: %d - las animaciones de %s están cortadas para imágenes %s, y el juego recibe las de %s." };
    static const char *const ORDER[4] = { "Put %s above or turn one of the mods off.",
                                          "Поставь %s выше или выключи один из модов.",
                                          "Stelle %s höher oder schalte einen der Mods ab.",
                                          "Pon %s más arriba o desactiva uno de los mods." };
    static const char *const OFF[4] = { "Turn one of the mods off.", "Выключи один из модов.", "Schalte einen der Mods ab.",
                                        "Desactiva uno de los mods." };
    static const char *const BTN[4][3] = { { "OK", "DETAILS", "TURN OFF" }, { "OK", "ПОДРОБНЕЕ", "ВЫКЛЮЧИТЬ" },
                                           { "OK", "DETAILS", "AUS" }, { "OK", "DETALLES", "DESACTIVAR" } };
    snprintf(g_pop_title, sizeof(g_pop_title), "%s", TITLE[lang]);
    for (int k = 0; k < 3; k++) snprintf(g_pop_btn_text[k], sizeof(g_pop_btn_text[0]), "%s", BTN[lang][k]);
    /* the two names on one line between them: each cut to half of it when long */
    char me_fit[160], them_fit[160];
    snprintf(me_fit, sizeof(me_fit), "%s", me);
    snprintf(them_fit, sizeof(them_fit), "%s", them);
    fit_text(FONT_SMALL, me_fit, sizeof(me_fit), 150.0f);
    fit_text(FONT_SMALL, them_fit, sizeof(them_fit), 150.0f);
    g_pop_lines = 0;
    int advice_at = 0;
    for (int para = 0; para < 3; para++) {
        if (para == 2) advice_at = g_pop_lines;
        if (para == 0) {
            int at = snprintf(text, sizeof(text), PAIR[lang], me_fit, them_fit);
            if (more > 0 && at > 0 && (size_t) at < sizeof(text)) snprintf(text + at, sizeof(text) - (size_t) at, MORE[lang], more);
        } else if (para == 1) {
            snprintf(text, sizeof(text), BODY[lang], n, cuts, ratio, sheets);
        } else if (all_own) {
            snprintf(text, sizeof(text), ORDER[lang], cuts);
        } else {
            snprintf(text, sizeof(text), "%s", OFF[lang]);
        }
        int k = wrap_font(FONT_SMALL, g_pop_line + g_pop_lines, POP_LINES - g_pop_lines, text, 340.0f);
        if (k > 0) g_pop_para[g_pop_lines] = 1;
        for (int i = 1; i < k; i++) g_pop_para[g_pop_lines + i] = 0;
        g_pop_lines += k;
    }
    /* the paper holds five lines over the buttons: with long names the advice goes (the details have it) */
    if (g_pop_lines > POP_ROOM) g_pop_lines = advice_at;
    if (g_pop_lines > POP_ROOM) g_pop_lines = POP_ROOM;
    g_pop = 1;
    g_pop_mod = mod;
    g_pop_btn = 0;
    for (int k = 0; k < 3; k++) g_pop_bw[k] = 0.0f;
    g_finger = -1;                      /* the finger that switched it on acts no more */
    g_gesture = 0;
    LOG("mods: popup: %s breaks with %s (%d pictures)", shim_usermods_id(mod), shim_usermods_id(other), n);
    if (shim_report_active()) {
        char line[300];
        snprintf(line, sizeof(line), "popup: %s switched on breaks with %s - %d pictures%s", shim_usermods_id(mod),
                 shim_usermods_id(other), n, more > 0 ? " (and more mods)" : "");
        report_line("mods", line);
    }
}

/* the mod just switched on: does it break with one that is on - asked again later when the pairs are not there yet */
static void popup_ask(int mod) {
    int first, n = shim_usermods_conflicts(mod, &first);
    if (n < 0) { g_pop_pending = mod; return; }
    g_pop_pending = -1;
    if (n > 0 && first >= 0) popup_show(mod, first, n - 1);
}

static void popup_act(int k) {
    static const char *const SAID[3] = { "OK", "details", "turned off" };
    g_pop = 0;
    LOG("mods: popup: %s", SAID[k]);
    (void) SAID;
    if (shim_report_active()) report_line("mods", k == 0 ? "popup: OK" : k == 1 ? "popup: details" : "popup: turned off");
    if (k == 1) {
        g_user = g_pop_mod;
        open_detail();
    } else if (k == 2) {
        if (shim_usermods_on(g_pop_mod)) shim_usermods_toggle(g_pop_mod);
        g_live_dirty = 1;
        play(SOUND_STEP);
    }
}

/*
 * THE CONTROLS GUIDE (the owner's item 22, 25.09.2026): the slides of
 * tools/guide_slides.py on the game's collection screen, behind the GUIDE strap
 * on the settings paper. It is a page of this file for what every page has -
 * the camera away, BACK, the pad and the keys routed here - and has no rows:
 * left and right turn the slides, a swipe or a tap on an arrow does too, and
 * confirm goes on to the next.
 */
static int g_slide;
static int g_swiped;                /* this finger has turned a slide: its lift does not */

static void guide_turn(int dir) {
    int to = g_slide + dir;
    if (to < 0 || to >= GD_SLIDES) return;
    g_slide = to;
    shim_menu_page_sound();
}

/* the arrow of a MODS row, in canvas units: is the point on it */
static void travel(float *off_x, float *off_y);
static void to_canvas(float px, float py, float off_x, float off_y, float *cx, float *cy);
/* a MODS row with a page behind its arrow: a port's mod with settings, or any of the player's */
static int has_arrow(int row) {
    return g_page == &PAGES[PAGE_MODS] && (settings_of(row) >= 0 || ID(row) >= R_USER);
}

static int on_arrow(int row, float gx, float gy) {
    if (!has_arrow(row)) return 0;
    float off_x, off_y, name_x, value_x, cx, cy;
    travel(&off_x, &off_y);
    row_layout(row, &name_x, &value_x);
    float ax = value_x + (float) (S()->on_w > S()->off_w ? S()->on_w : S()->off_w) + (float) TW_GAP;
    to_canvas(ax, row_top(row), off_x, off_y, &cx, &cy);
    /* the arrow is 16 wide; a finger is given a little more on every side */
    return gx >= cx - 8.0f && gx <= cx + (float) MD_ARROW_W + 12.0f && gy >= cy && gy < cy + (float) S()->step;
}

int shim_tweaks_open(void) {
    pthread_mutex_lock(&g_lock);
    int open = g_open;
    pthread_mutex_unlock(&g_lock);
    return open;
}

/* how far the camera still is from the unscrolled page, in units */
static void travel(float *off_x, float *off_y) {
    *off_x = *off_y = 0.0f;
    if (!g_menu) return;
    const float *camera = (const float *) (g_menu + MENU_CAMERA);
    *off_x = camera[0] - (g_base[0] + half_x());
    *off_y = camera[1] - (g_base[1] + half_y());
}

/* kind is STRAP_TWEAKS or STRAP_GUIDE: from the paper's edge to the picture's right end */
static int strap_box(unsigned long menu, int kind, float *x0, float *y0, float *x1, float *y1) {
    if (!menu) return 0;
    /*
     * FROM WHERE THE SETTINGS ARE DRAWN: the camera plus the list's own place,
     * list+0x1c - the vector the render adds (0x6e3b34..0x6e3b60) and the
     * game's own row taps use (0x6e2a14). On a canvas taller than 16:9 the
     * rows are spread (loader.c row_step) and the settings tile stands at 4
     * steps, 1280 on 4:3 rather than 1080: the constant -1080 put the box 200
     * units above the strap on a tablet and 88 on 16:10 (the owner, 01.10.2026;
     * the research notes of 30.09.2026, tap audit). On 16:9 and wider this is
     * the same
     * number as before.
     */
    const float *camera = (const float *) (menu + MENU_CAMERA);
    const float *tile = (const float *) (menu + SETTINGS_LIST + LIST_TILE);
    int set = set_index();
    float top = STRAP_FROM_Y + (kind == STRAP_GUIDE ? GUIDE_STRAP_DROP : 0.0f) + camera[1] + tile[1];
    *x0 = STRAP_FROM_X + camera[0] + tile[0];
    *x1 = *x0 + (float) (STRAP_SIZE[set][kind].w - STRAP_EXTEND);
    *y0 = top - (float) STRAP_SIZE[set][kind].rise;
    *y1 = top + (kind == STRAP_GUIDE ? GUIDE_STRAP_H : STRAP_H);
    return 1;
}

/* ------------------------------------------------------------ the touch */
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
 * The game's BACK plaque, drawn on the glass in the bottom-left corner.
 *
 * It is not always there. 0x7697f4 walks the plaque's slide [menu+0x2c] six
 * units a frame towards a target, and with a pad in use ([menu+0x24] == 0) the
 * target is 90 on every screen - off the bottom. The game's own touch box
 * (0x6732b4, action 0xf) is registered at the slid place, so a plaque that has
 * left cannot be pressed. The box here rides with the same slide, or a finger
 * would close the page through a plaque that is not on the screen.
 */
#define MENU_BACK_SLIDE 0x2c

static int on_back(float gx, float gy) {
    float slide = g_menu ? *(const float *) (g_menu + MENU_BACK_SLIDE) : 0.0f;
    return gx < BACK_TAP_W && gy > shim_screen_height() - 75.0f + slide;
}

/* the row under a canvas point, or -1; rows are the page's width and a row step tall */
static int row_at(float gx, float gy) {
    float off_x, off_y;
    travel(&off_x, &off_y);
    float px = gx - half_x() - (float) S()->paper_x - off_x;
    float py = gy - half_y() - (float) TW_PAPER_Y - off_y;
    if (px < 0.0f || px > (float) S()->paper_w) return -1;
    for (int i = 0; i < ROWS(); i++)
        if (py >= row_top(i) && py < row_top(i) + row_h(i)) return i;
    return -1;
}

/*
 * A touch, before the queue (uikit.c). Closed, it watches the strap on the
 * settings screen. Open, every finger is ours, and the first one down plays
 * the settings list's own gesture (see the top of the file).
 */
int shim_tweaks_touch(int id, int phase, float x, float y) {
    int mine = 0;
    pthread_mutex_lock(&g_lock);
    float gx = 0.0f, gy = 0.0f;
    int placed = to_field(x, y, &gx, &gy);

    if (g_open && g_pop) {
        /* THE POPUP takes every finger: one coming down on a button presses it, anywhere else nothing */
        mine = 1;
        if (id == g_opener) {
            if (phase >= 3) g_opener = -1;
        } else if (placed && phase == 0) {
            for (int k = 0; k < 3; k++)
                if (g_pop_bw[k] > 0.0f && gx >= g_pop_bx[k] - 12.0f && gx <= g_pop_bx[k] + g_pop_bw[k] + 12.0f
                    && gy >= g_pop_by - 8.0f && gy <= g_pop_by + 30.0f) {
                    g_pop_btn = k;
                    popup_act(k);
                    break;
                }
        }
    } else if (g_open && g_page == &PAGES[PAGE_GUIDE]) {
        /* a swipe turns a slide; a tap on an arrow does, on its lift */
        mine = 1;
        if (id == g_opener) {
            if (phase >= 3) g_opener = -1;
        } else if (placed && phase == 0) {
            if (on_back(gx, gy)) back_here();
            else if (g_finger < 0) { g_finger = id; g_step_x = gx; g_swiped = 0; }
        } else if (placed && id == g_finger && phase == 1) {
            if (!g_swiped && fabsf(gx - g_step_x) > STEP_UNITS) {
                guide_turn(gx < g_step_x ? 1 : -1);
                g_swiped = 1;
            }
        } else if (id == g_finger && phase >= 3) {
            if (phase == 3 && placed && !g_swiped) {    /* a cancelled finger (a palm, U7) turns nothing */
                float off_x, off_y;
                travel(&off_x, &off_y);
                float px = gx - half_x() - off_x, py = gy - half_y() - off_y;
                /* the arrows, and a finger's width round them */
                if (py >= GD_ARROW_Y - 16 && py <= GD_ARROW_Y + GD_ARROW_H + 16) {
                    if (px >= GD_NEXT_X - 16 && px <= GD_NEXT_X + GD_ARROW_W + 20) guide_turn(1);
                    else if (px >= GD_BACK_X - 20 && px <= GD_BACK_X + GD_ARROW_W + 16) guide_turn(-1);
                }
            }
            g_finger = -1;
        }
    } else if (g_open) {
        mine = 1;
        if (id == g_opener) {
            if (phase >= 3) g_opener = -1;          /* its lift must not act */
        } else if (placed && phase == 0) {
            if (on_back(gx, gy)) {
                back_here();
            } else {
                int row = row_at(gx, gy);
                if (row >= 0 && on_arrow(row, gx, gy)) {
                    g_cursor = row;
                    open_settings(row);                              /* the arrow opens at once */
                    row = -1;
                } else if (is_link(row)) {
                    g_cursor = row;
                    open_detail();                                   /* a line over the title opens at once */
                    row = -1;
                } else if (row >= 0 && !is_heading(row)) {
                    if (row != g_cursor) { g_cursor = row; g_sure = 0; }    /* highlight */
                    else if (!confirm_still(row)) step(row, +1);     /* confirm */
                }
                if (g_finger < 0) {
                    g_finger = id;
                    g_start_x = x; g_start_y = y;
                    g_last_y = gy;
                    g_step_x = gx;
                    g_gesture = 0;
                }
            }
        } else if (placed && id == g_finger && phase == 1) {
            if (!g_gesture && hypotf(x - g_start_x, y - g_start_y) > DECIDE_POINTS) {
                g_gesture = fabsf(y - g_start_y) <= fabsf(x - g_start_x) ? 2 : 1;
                g_last_y = gy;
                g_step_x = gx;
            }
            if (g_gesture == 1 && g_menu) {
                float *a = anchor_of(g_menu, g_page->screen);
                float want = a[1] + (gy - g_last_y);
                float low = g_base[1] - scroll_room();
                a[1] = want < low ? low : (want > g_base[1] ? g_base[1] : want);
                g_last_y = gy;
            } else if (g_gesture == 2) {
                int dir = gx - g_step_x > STEP_UNITS ? 1 : (gx - g_step_x < -STEP_UNITS ? -1 : 0);
                if (dir && is_action(g_cursor)) dir = 0;
                if (dir) {
                    g_step_x = gx;
                    step(g_cursor, dir);
                    play(SOUND_STEP);
                }
            }
        } else if (id == g_finger && phase >= 3) {
            g_finger = -1;
            g_gesture = 0;
        }
    } else if (phase == 0 && placed && !shim_sticks_in_game()) {
        unsigned long b = shim_image_base();
        unsigned long menu = b ? *(const unsigned long *) (b + MENU_PTR) : 0;
        if (menu && *(const int *) (menu + MENU_SCREEN) == SCREEN_OPTIONS) {
            float x0, y0, x1, y1;
#if ISAAC_DIAG
            /*
             * THE ZONE ON THIS CANVAS (report builds): the box as judged now,
             * from the list's own place, beside where the old constant -1080
             * would have put it - on a canvas taller than 16:9 the two part
             * (the tap audit, 01.10.2026), and the finger shows which one the
             * player meant.
             */
            if (shim_report_active()) {
                static int said;
                if (said < 16 && strap_box(menu, STRAP_TWEAKS, &x0, &y0, &x1, &y1)) {
                    said++;
                    const float *camera = (const float *) (menu + MENU_CAMERA);
                    const float *tile = (const float *) (menu + SETTINGS_LIST + LIST_TILE);
                    float old_y0 = STRAP_FROM_Y + camera[1] - OPTIONS_TOP
                                 - (float) STRAP_SIZE[set_index()][STRAP_TWEAKS].rise;
                    char line[220];
                    snprintf(line, sizeof(line),
                             "strap zone: canvas %.0fx%.0f, list at %.1f,%.1f, camera %.1f,%.1f;"
                             " TWEAKS box %.1f..%.1f x %.1f..%.1f (the old -1080 gave y from %.1f%s);"
                             " finger %.1f,%.1f",
                             (double) shim_screen_width(), (double) shim_screen_height(),
                             (double) tile[0], (double) tile[1], (double) camera[0], (double) camera[1],
                             (double) x0, (double) x1, (double) y0, (double) y1, (double) old_y0,
                             fabsf(old_y0 - y0) > 1.0f ? ", MISMATCH" : "", (double) gx, (double) gy);
                    report_line("tweaks", line);
                }
            }
#endif
            if (strap_box(menu, STRAP_TWEAKS, &x0, &y0, &x1, &y1) && gx >= x0 && gx <= x1 && gy >= y0 && gy <= y1) {
                open_here(menu, &PAGES[0]);
                g_opener = id;
                mine = 1;
            } else if (strap_box(menu, STRAP_GUIDE, &x0, &y0, &x1, &y1) && gx >= x0 && gx <= x1
                       && gy >= y0 && gy <= y1) {
                g_slide = 0;
                open_here(menu, &PAGES[PAGE_GUIDE]);
                g_opener = id;
                mine = 1;
            }
        }
    }
    pthread_mutex_unlock(&g_lock);
    return mine;
}

#if ISAAC_DIAG
/* the tap audit's lines (screen.c, M1 and M4): a strap's box, as the finger above is judged */
int shim_tweaks_strap_rect(int guide, float box[4]) {
    unsigned long b = shim_image_base();
    unsigned long menu = b ? *(const unsigned long *) (b + MENU_PTR) : 0;
    return menu && strap_box(menu, guide ? STRAP_GUIDE : STRAP_TWEAKS, &box[0], &box[1], &box[2], &box[3]);
}

/* and what an open page makes of a point, by the same tests the finger above meets (M4) */
int shim_tweaks_tap_verdict(float gx, float gy, char *out, size_t room) {
    pthread_mutex_lock(&g_lock);
    int open = g_open;
    if (open) {
        if (on_back(gx, gy)) {
            snprintf(out, room, "BACK");
        } else if (g_page == &PAGES[PAGE_GUIDE]) {
            float off_x, off_y;
            travel(&off_x, &off_y);
            float px = gx - half_x() - off_x, py = gy - half_y() - off_y;
            const char *what = "the guide (a swipe turns it)";
            if (py >= GD_ARROW_Y - 16 && py <= GD_ARROW_Y + GD_ARROW_H + 16) {
                if (px >= GD_NEXT_X - 16 && px <= GD_NEXT_X + GD_ARROW_W + 20) what = "the guide's NEXT arrow";
                else if (px >= GD_BACK_X - 20 && px <= GD_BACK_X + GD_ARROW_W + 16) what = "the guide's BACK arrow";
            }
            snprintf(out, room, "%s", what);
        } else {
            int row = row_at(gx, gy);
            if (row < 0) snprintf(out, room, "no row");
            else snprintf(out, room, "row %d%s", row, on_arrow(row, gx, gy) ? ", its arrow" : "");
        }
    }
    pthread_mutex_unlock(&g_lock);
    return open;
}
#endif

/*
 * THE MODS ITEM (hook V on the screen switch 0x769b88, menu in x0, screen in w1).
 *
 * The main menu has six items now and the fourth is MODS (loader.c,
 * main_menu_mods). Its confirm goes through the same block OPTIONS does - a
 * switch to the settings screen - because there is no room for a block of its
 * own; what tells the two apart is the one thing the game's own dispatch went
 * by, the highlighted item. MODS highlighted and the settings asked for from
 * the main menu: the request is turned back into the screen the menu is on
 * (0x769b9c leaves at once when they match) and the MODS page opens instead.
 */
void shim_mods_screen(unsigned long menu, unsigned long screen, unsigned long *regs) {
    if (!menu || !regs) return;
    if ((int) screen != SCREEN_OPTIONS || *(const int *) (menu + MENU_SCREEN) != SCREEN_TITLE) return;
    if (*(const int *) (menu + MAIN_MENU + MAIN_SELECTED) != ITEM_MODS) return;
    regs[1] = SCREEN_TITLE;
    pthread_mutex_lock(&g_lock);
    if (!g_open) {
        open_here(menu, &PAGES[1]);
        g_opener = -1;
    }
    pthread_mutex_unlock(&g_lock);
}

/* from a pad (straps.c): the guide, as the GUIDE strap's tap opens it, or closed */
void shim_guide_show(int on) {
    pthread_mutex_lock(&g_lock);
    unsigned long b = shim_image_base();
    unsigned long menu = b ? *(const unsigned long *) (b + MENU_PTR) : 0;
    if (on && !g_open && menu && *(const int *) (menu + MENU_SCREEN) == SCREEN_OPTIONS) {
        g_slide = 0;
        open_here(menu, &PAGES[PAGE_GUIDE]);
        g_opener = -1;
    } else if (!on && g_open && g_page == &PAGES[PAGE_GUIDE]) {
        close_here();
    }
    pthread_mutex_unlock(&g_lock);
}

/* from a pad (straps.c): open the page as the strap's tap does, or close it */
void shim_tweaks_show(int on) {
    pthread_mutex_lock(&g_lock);
    unsigned long b = shim_image_base();
    unsigned long menu = b ? *(const unsigned long *) (b + MENU_PTR) : 0;
    if (on && !g_open && menu && *(const int *) (menu + MENU_SCREEN) == SCREEN_OPTIONS) {
        open_here(menu, &PAGES[0]);
        g_opener = -1;
    } else if (!on && g_open) {
        close_here();
    }
    pthread_mutex_unlock(&g_lock);
}

/*
 * A pad or a keyboard on the open page (straps.c, between frames). The settings
 * list under the page never hears it: it would walk its own rows and move the
 * anchor this page borrows, and the camera flew off into the empty world (the
 * owner, 18.09.2026). So the page plays the list's moves itself: up and down
 * walk the rows round, sideways steps the row as a sideways swipe does,
 * confirm does what a tap on the highlighted row does.
 *
 * The page scrolls to keep the row in view, through the same anchor a finger
 * moves, and the camera takes it there at its own pace.
 */
#define PAD_MARGIN 20.0f            /* units kept clear above and below the row */

static void keep_in_view(int row) {
    if (!g_menu) return;
    float *a = anchor_of(g_menu, g_page->screen);
    float top = (float) TW_PAPER_Y + row_top(row);
    float least = top + row_h(row) + PAD_MARGIN - view_bottom();
    float most = top - PAD_MARGIN - view_top();
    float scrolled = g_base[1] - a[1];
    if (scrolled < least) scrolled = least;
    if (scrolled > most) scrolled = most;
    /* the first row is the top of the page, title and all: no need to stop short of it */
    if (row == 0) scrolled = 0.0f;
    if (scrolled > scroll_room()) scrolled = scroll_room();
    if (scrolled < 0.0f) scrolled = 0.0f;
    a[1] = g_base[1] - scrolled;
}

void shim_tweaks_pad(int action) {
    pthread_mutex_lock(&g_lock);
    if (g_open && g_pop) {
        if (action == PAGE_LEFT || action == PAGE_UP) g_pop_btn = (g_pop_btn + 2) % 3;
        else if (action == PAGE_RIGHT || action == PAGE_DOWN) g_pop_btn = (g_pop_btn + 1) % 3;
        else if (action == PAGE_CONFIRM) popup_act(g_pop_btn);
        else if (action == PAGE_BACK) popup_act(0);
    } else if (g_open && g_page == &PAGES[PAGE_DETAIL]) {
        /* no rows: up and down scroll a row's step at a time, as far as a finger may */
        if ((action == PAGE_UP || action == PAGE_DOWN) && g_menu) {
            float *a = anchor_of(g_menu, g_page->screen);
            float scrolled = g_base[1] - a[1] + (action == PAGE_DOWN ? (float) S()->step : -(float) S()->step);
            if (scrolled > scroll_room()) scrolled = scroll_room();
            if (scrolled < 0.0f) scrolled = 0.0f;
            a[1] = g_base[1] - scrolled;
        } else if (action == PAGE_BACK) {
            back_here();
        }
    } else if (g_open && g_page == &PAGES[PAGE_GUIDE]) {
        if (action == PAGE_LEFT) guide_turn(-1);
        else if (action == PAGE_RIGHT || action == PAGE_CONFIRM) guide_turn(1);
        else if (action == PAGE_BACK) back_here();
    } else if (g_open) {
        switch (action) {
        case PAGE_UP:
            g_cursor = (g_cursor + ROWS() - 1) % ROWS();
            if (is_heading(g_cursor)) g_cursor = (g_cursor + ROWS() - 1) % ROWS();
            g_sure = 0;
            keep_in_view(g_cursor);
            break;
        case PAGE_DOWN:
            g_cursor = (g_cursor + 1) % ROWS();
            if (is_heading(g_cursor)) g_cursor = (g_cursor + 1) % ROWS();
            g_sure = 0;
            keep_in_view(g_cursor);
            break;
        case PAGE_LEFT:
        case PAGE_RIGHT:
            if (is_action(g_cursor)) break;
            step(g_cursor, action == PAGE_RIGHT ? 1 : -1);
            play(SOUND_STEP);
            break;
        case PAGE_CONFIRM:
            if (has_arrow(g_cursor)) open_settings(g_cursor);
            else if (!confirm_still(g_cursor)) step(g_cursor, +1);
            break;
        case PAGE_BACK:
            back_here();
            break;
        }
    }
    pthread_mutex_unlock(&g_lock);
}

/* once between frames (gl.c): the page belongs to the settings screen alone */
void shim_tweaks_between_frames(void) {
    pthread_mutex_lock(&g_lock);
    if (g_open) {
        unsigned long b = shim_image_base();
        unsigned long menu = b ? *(const unsigned long *) (b + MENU_PTR) : 0;
        if (!menu || menu != g_menu || *(const int *) (menu + MENU_SCREEN) != g_page->screen)
            close_here();
    }
    pthread_mutex_unlock(&g_lock);
}

/* ---------------------------------------------------------- the pictures */
/*
 * One .anm2 into one sprite object. The object is BUILT before it is loaded
 * into - leaving that out crashed the first test build with a signal 11 at
 * 0xa8. `raw` stands in for a std::string and is long long for its alignment.
 */
static int picture_ready(unsigned char *sprite, signed char *state, const char *anm2) {
    if (*state) return *state > 0;
    unsigned long b = shim_image_base();
    if (!b) return 0;
    *state = -1;
    memset(sprite, 0, SPRITE_BYTES);
    ((void (*)(void *)) (b + FN_SPRITE_NEW))(sprite);
    long long raw[3];
    unsigned char *path = (unsigned char *) raw;
    path[23] = 0;
    shim_str_assign(path, anm2);
    ((void (*)(void *, const void *, int)) (b + FN_SPRITE_LOAD))(sprite, path, 1);
    shim_str_dtor(path);
    if (!sprite[SPRITE_LOADED]) {
        if (shim_report_active()) report_line("tweaks", "a picture did NOT load");
        return 0;
    }
    *state = 1;
    return 1;
}

/* SCALE IS A RATIO: 1 is the picture's own size. A percentage here once drew
   three sliders a hundred times too large, over the whole screen. */
static void render(unsigned char *sprite, const char *anim, int frame,
                   float x, float y, float sx, float sy) {
    unsigned long b = shim_image_base();
    ((void (*)(void *, const char *, int)) (b + FN_SPRITE_FRAME))(sprite, anim, frame);
    float *colour = (float *) (sprite + SPRITE_COLOUR);
    colour[0] = colour[1] = colour[2] = colour[3] = 1.0f;
    float *scale = (float *) (sprite + SPRITE_SCALE);
    scale[0] = sx;
    scale[1] = sy;
    float pos[2] = { x, y };
    ((void (*)(void *, const float *, const void *, const void *)) (b + FN_SPRITE_RENDER))(
        sprite, pos, (const void *) (b + ZERO_VECTOR), (const void *) (b + ZERO_VECTOR));
    scale[0] = scale[1] = 1.0f;
}

/* the same two hands for the other files of the layer (mods.c) */
int shim_picture_ready(unsigned char *sprite, signed char *state, const char *anm2) {
    return picture_ready(sprite, state, anm2);
}

void shim_picture_draw(unsigned char *sprite, const char *anim, int frame,
                       float x, float y, float alpha) {
    unsigned long b = shim_image_base();
    if (!b) return;
    ((void (*)(void *, const char *, int)) (b + FN_SPRITE_FRAME))(sprite, anim, frame);
    float *colour = (float *) (sprite + SPRITE_COLOUR);
    colour[0] = colour[1] = colour[2] = 1.0f;
    colour[3] = alpha;
    float pos[2] = { x, y };
    ((void (*)(void *, const float *, const void *, const void *)) (b + FN_SPRITE_RENDER))(
        sprite, pos, (const void *) (b + ZERO_VECTOR), (const void *) (b + ZERO_VECTOR));
    colour[3] = 1.0f;
}

/* ------------------------------------------------------------- the draw */
/*
 * THE LIVE PAPER: the head, a piece a row (Piece1..Piece5 round, as the PC page
 * repeats them), the foot - each its left part, the band as many times as the
 * width asks, and its right part (Pieces frame part * 3 + 0, 1, 2). Then the
 * title: MODS drawn on the desk, or a player's mod's name in the menu font,
 * the big cut when it fits the paper.
 */
static void draw_live_paper(unsigned char *art, float off_x, float off_y) {
    int set = set_index();
    int band_x = MD_BAND_X[set];
    int pieces = (S()->paper_h - MD_HEAD_H - MD_FOOT_H) / MD_PIECE_H;
    float y = 0.0f, cx, cy;
    for (int k = 0; k < pieces + 2; k++) {
        int part = k == 0 ? 0 : (k == pieces + 1 ? 6 : 1 + (k - 1) % 5);
        float ph = (float) (part == 0 ? MD_HEAD_H : (part == 6 ? MD_FOOT_H : MD_PIECE_H));
        to_canvas(0.0f, y, off_x, off_y, &cx, &cy);
        render(art, "Pieces", part * 3, cx, cy, 1.0f, 1.0f);
        for (int b = 0; b < g_bands; b++) {
            to_canvas((float) (band_x + b * MD_BAND_W), y, off_x, off_y, &cx, &cy);
            render(art, "Pieces", part * 3 + 1, cx, cy, 1.0f, 1.0f);
        }
        to_canvas((float) (band_x + g_bands * MD_BAND_W), y, off_x, off_y, &cx, &cy);
        render(art, "Pieces", part * 3 + 2, cx, cy, 1.0f, 1.0f);
        y += ph;
    }
    if (g_page == &PAGES[PAGE_MODS]) {
        to_canvas(centred((float) S()->paper_w, (float) MX_W[set][MX_TITLE]), (float) MD_TITLE_Y,
                  off_x, off_y, &cx, &cy);
        render(art, "Extra", MX_TITLE, cx, cy, 1.0f, 1.0f);
    } else {
        /* a mod's page and its details: its name, whole (on more lines when one is not enough: title_build); on the
           page, under the lines over the title */
        float head = g_page == &PAGES[PAGE_USER] ? g_head_h : 0.0f;
        int red = mod_red(g_user), pale = !red && shim_usermods_pending(g_user);
        for (int i = 0; i < g_title_n; i++) {
            float w = shim_menu_text_width(g_title_font, g_title[i]);
            to_canvas(centred((float) S()->paper_w, w),
                      floorf((float) MD_TITLE_Y + head + (float) i * TITLE_STEP - CAP_TOP[g_title_font]), off_x, off_y, &cx, &cy);
            shim_menu_write(g_title_font, g_title[i], cx, cy, red ? RED_R : INK_R, red ? RED_G : INK_G, red ? RED_B : INK_B,
                            pale ? GREY_A : 1.0f);
        }
    }
}

/* a mod's version and author and its description; and the note, when a restart is owed */
static void draw_live_text(unsigned char *art, float off_x, float off_y) {
    float cx, cy;
    if (g_page == &PAGES[PAGE_USER]) {
        float y = (float) S()->row0 + 4.0f - CAP_TOP[FONT_SMALL];
        const char *lines[DESC_LINES + 1];
        int n = 0;
        if (g_facts[0]) lines[n++] = g_facts;
        for (int i = 0; i < g_desc_n; i++) lines[n++] = g_desc[i];
        for (int i = 0; i < n; i++) {
            float w = shim_menu_text_width(FONT_SMALL, lines[i]);
            to_canvas(centred((float) S()->paper_w, w), floorf(y), off_x, off_y, &cx, &cy);
            shim_menu_write(FONT_SMALL, lines[i], cx, cy, INK_R, INK_G, INK_B, 1.0f);
            y += SMALL_LINE;
        }
    }
    if (g_page == &PAGES[PAGE_DETAIL]) {
        /* THE DETAILS, as detail_build laid them: ink, red or grey */
        float h = shim_screen_height();
        for (int i = 0; i < g_dl_n; i++) {
            const dline_t *d = &g_dl[i];
            float x = d->x >= 0 ? (float) d->x : centred((float) S()->paper_w, shim_menu_text_width(d->font, d->text));
            to_canvas(x, (float) d->y, off_x, off_y, &cx, &cy);
            if (cy < -40.0f || cy > h + 8.0f) continue;
            int red = d->ink == 1;
            shim_menu_write(d->font, d->text, cx, cy, red ? RED_R : INK_R, red ? RED_G : INK_G, red ? RED_B : INK_B,
                            d->ink == 2 ? GREY_A : 1.0f);
        }
    }
    if (g_note_n) {
        int rows = page_rows(g_page);
        float y = (rows ? row_top(rows - 1) + (float) S()->step : (float) S()->row0) + 6.0f - CAP_TOP[FONT_SMALL];
        for (int i = 0; i < g_note_n; i++) {
            float w = shim_menu_text_width(FONT_SMALL, g_note[i]);
            to_canvas(centred((float) S()->paper_w, w), floorf(y), off_x, off_y, &cx, &cy);
            shim_menu_write(FONT_SMALL, g_note[i], cx, cy, INK_R, INK_G, INK_B, 1.0f);
            y += SMALL_LINE;
        }
    }
    (void) art;
}


/* THE POPUP on the glass, over everything: the shade, the paper in the middle, the words, the buttons and the pointer */
static unsigned char g_pop_art[SPRITE_BYTES] __attribute__((aligned(16)));
static signed char g_pop_art_state;

static void popup_draw(float w, float h) {
    if (!picture_ready(g_pop_art, &g_pop_art_state, "gfx/port_mods_popup.anm2")) return;
    render(g_pop_art, "Shade", 0, -20.0f, -20.0f, (w + 40.0f) / 10.0f, (h + 40.0f) / 10.0f);
    float x0 = centred(w, 400.0f), y0 = centred(h, 192.0f);
    render(g_pop_art, "Paper", 0, x0 + 200.0f, y0 + 96.0f, 1.0f, 1.0f);
    shim_menu_write(FONT_ROW, g_pop_title, x0 + centred(400.0f, shim_menu_text_width(FONT_ROW, g_pop_title)),
                    y0 + 18.0f - CAP_TOP[FONT_ROW], RED_R, RED_G, RED_B, 1.0f);
    float y = y0 + 52.0f;
    for (int i = 0; i < g_pop_lines; i++) {
        if (i && g_pop_para[i]) y += 4.0f;
        shim_menu_write(FONT_SMALL, g_pop_line[i], x0 + centred(400.0f, shim_menu_text_width(FONT_SMALL, g_pop_line[i])), y,
                        INK_R, INK_G, INK_B, 1.0f);
        y += SMALL_LINE;
    }
    /* OK, DETAILS, TURN OFF side by side, as the game's YES and NO; the pointer before the chosen one */
    float bw[3], total = 0.0f, gap = 30.0f;
    for (int k = 0; k < 3; k++) { bw[k] = shim_menu_text_width(FONT_ROW, g_pop_btn_text[k]); total += bw[k]; }
    if (total + 2.0f * gap > 350.0f) gap = 20.0f;
    float x = x0 + centred(400.0f, total + 2.0f * gap), by = y0 + 136.0f;
    for (int k = 0; k < 3; k++) {
        g_pop_bx[k] = x;
        g_pop_bw[k] = bw[k];
        shim_menu_write(FONT_ROW, g_pop_btn_text[k], x, by, INK_R, INK_G, INK_B, 1.0f);
        if (k == g_pop_btn) render(g_pop_art, "Pointer", 0, x - 12.0f, by + 11.0f, 1.0f, 1.0f);
        x += bw[k] + gap;
    }
    g_pop_by = by;
}

/*
 * Drawn at the menu's own screen draw, the finished canvas - not at the
 * settings list's draw, which renders its paper into a 256 x 256 texture.
 *
 * The pieces stand where the settings .anm2 stands them, relative to a row:
 * the ticks at (+2, +2) from the value; the cursor, pivot (16,16), at
 * (-6, +14) from the name; ON and OFF, pivot (16,16) turned a quarter
 * clockwise, at (+24, +15) from the value, which puts the word's 40 x 22
 * box on the row.
 */
void shim_tweaks_draw(void) {
    pthread_mutex_lock(&g_lock);
    int open = g_open;
    float off_x = 0.0f, off_y = 0.0f;
    if (open) travel(&off_x, &off_y);
    /* a mod switched on before the pairs were worked out: asked again (THE POPUP) */
    if (open && g_pop_pending >= 0 && shim_usermods_pairs_gen()) {
        int m = g_pop_pending;
        g_pop_pending = -1;
        if (shim_usermods_on(m)) popup_ask(m);
    }
    int cursor = g_cursor;
    pthread_mutex_unlock(&g_lock);

    unsigned long b = shim_image_base();
    if (!b) return;
    float w = shim_screen_width(), h = shim_screen_height();
    if (!(w > 0.0f) || !(h > 0.0f)) return;

    if (!open) return;                   /* the strap is a layer of the settings sheet */
    int set = set_index(), pg = (int) (g_page - PAGES);
    shim_usermods_lang(set);
    if (g_cursor >= ROWS()) g_cursor = cursor = ROWS() - 1;   /* a row went (RESTART TO APPLY, a setting turned back) */
    if (!g_art_path[pg][set][0])
        snprintf(g_art_path[pg][set], sizeof(g_art_path[pg][set]), g_page->art, TW_SET[set].suffix);
    /* the settings pages' art names carry their number in the format itself */
    unsigned char *art = g_art[pg][set];
    if (!picture_ready(art, &g_art_state[pg][set], g_art_path[pg][set])) return;

    /* the wall: the settings screen's ten-pixel swatch, over the whole glass */
    render(art, "Wall", 0, -20.0f, -20.0f, (w + 40.0f) / 10.0f, (h + 40.0f) / 10.0f);
    if (g_page == &PAGES[PAGE_GUIDE]) {
        /* the collection screen's places, in the middle of the glass, carried by the camera */
        float gx0 = half_x() + off_x, gy0 = half_y() + off_y;
        /* a slide with a pad button on it, in the family of the pad in the hand (THE GUIDE IN THE PAD'S OWN PICTURES) */
        unsigned char *slide_art = art;
        int frame = g_slide, fam = shim_pad_icon_family();
        if (fam > 0 && fam < GD_FAMILIES && g_slide >= 0 && g_slide < GD_SLIDES && GD_FAMILY_FRAME[g_slide] >= 0) {
            if (!g_guide_path[fam][set][0])
                snprintf(g_guide_path[fam][set], sizeof(g_guide_path[fam][set]), "gfx/port_guide_%s_%s.anm2",
                         TW_SET[set].suffix, GD_FAMILY[fam]);
            if (picture_ready(g_guide_art[fam][set], &g_guide_state[fam][set], g_guide_path[fam][set])) {
                slide_art = g_guide_art[fam][set];
                frame = GD_FAMILY_FRAME[g_slide];
            }
        }
        render(slide_art, "Paper", frame, gx0 + GD_PAPER_X, gy0 + GD_PAPER_Y, 1.0f, 1.0f);
        render(slide_art, "Note", frame, gx0 + GD_NOTE_X, gy0 + GD_NOTE_Y, 1.0f, 1.0f);
        if (g_slide + 1 < GD_SLIDES) render(art, "Next", 0, gx0 + GD_NEXT_X, gy0 + GD_ARROW_Y, 1.0f, 1.0f);
        if (g_slide > 0) render(art, "Back", 0, gx0 + GD_BACK_X, gy0 + GD_ARROW_Y, 1.0f, 1.0f);
        return;
    }
    render(art, "Sketches", 0, half_x() + off_x, half_y() + off_y, 1.0f, 1.0f);

    float cx, cy;
    int live = is_live(g_page);
    if (live) draw_live_paper(art, off_x, off_y);
    else {
        to_canvas(0.0f, 0.0f, off_x, off_y, &cx, &cy);
        render(art, "Paper", 0, cx, cy, 1.0f, 1.0f);
    }

    for (int i = 0; i < ROWS(); i++) {
        float top = (float) row_top(i), name_x, value_x;
        row_layout(i, &name_x, &value_x);
        to_canvas(0.0f, top, off_x, off_y, &cx, &cy);
        if (cy < -2.0f * S()->step || cy > h + S()->step) continue;

        if (is_link(i)) {
            /* a line over the title: red (hotfix 1.71: nothing grey), the list's arrow beside it, the cursor before it */
            int red = 1;
            float mid = top + 1.0f + floorf((CAP_TOP[FONT_SMALL] + CAP_BOTTOM[FONT_SMALL]) * 0.5f);
            to_canvas(name_x, top, off_x, off_y, &cx, &cy);
            shim_menu_write(FONT_SMALL, g_link_text[i], cx, cy, red ? RED_R : INK_R, red ? RED_G : INK_G, red ? RED_B : INK_B,
                            red ? 1.0f : GREY_A);
            to_canvas(value_x, mid, off_x, off_y, &cx, &cy);
            shim_picture_draw(art, "Arrow", 0, cx, cy, red ? 1.0f : GREY_A);
            if (i == cursor) {
                to_canvas(name_x, mid, off_x, off_y, &cx, &cy);
                render(art, "Cursor", 0, cx - 6.0f, cy + 5.0f, 1.0f, 1.0f);
            }
            continue;
        }
        int set = set_index(), x = extra_of(i), frame = name_frame(i);
        if (is_heading(i)) {
            /* YOUR MODS (or a mod's SETTINGS) and its stroke, in the middle of the row */
            to_canvas(centred((float) S()->paper_w, (float) MX_W[set][x]),
                      top + centred((float) S()->step, (float) MX_H[set][x]) - 2.0f, off_x, off_y, &cx, &cy);
            render(art, "Extra", x, cx, cy, 1.0f, 1.0f);
            continue;
        }
        if (x >= 0) {
            to_canvas(name_x, top + (float) S()->baseline - (float) MX_H[set][x], off_x, off_y, &cx, &cy);
            render(art, "Extra", x, cx, cy, 1.0f, 1.0f);
        } else if (ID(i) >= R_USER) {
            to_canvas(name_x, top + (float) S()->baseline - CAP_BOTTOM[FONT_ROW], off_x, off_y, &cx, &cy);
            int red = mod_red(ID(i) - R_USER);          /* the port does not use all of it (A MOD IN RED) */
            int pale = !red && shim_usermods_pending(ID(i) - R_USER);     /* added now: works after the restart */
            /* its name, cut to the paper when it is longer (THE WIDEST PAPER) */
            shim_menu_write(FONT_ROW, list_label(ID(i) - R_USER), cx, cy, red ? RED_R : INK_R, red ? RED_G : INK_G,
                            red ? RED_B : INK_B, pale ? GREY_A : 1.0f);
        } else if (uset(i) >= 0) {
            /* a mod's setting: its label as the author wrote it, in the menu font (the owner, 02.10.2026) */
            to_canvas(name_x, top + (float) S()->baseline - CAP_BOTTOM[FONT_ROW], off_x, off_y, &cx, &cy);
            shim_menu_write(FONT_ROW, uset_label(uset(i)), cx, cy, INK_R, INK_G, INK_B, 1.0f);
        } else {
            to_canvas(name_x, top + (float) S()->baseline - S()->word_h[frame], off_x, off_y, &cx, &cy);
            render(art, "Words", frame, cx, cy, 1.0f, 1.0f);
        }

        to_canvas(value_x, top, off_x, off_y, &cx, &cy);
        const sp_row_t *sp = SP(i);
        if (ID(i) == R_U_UP || ID(i) == R_U_DOWN) {
            /* where the mod stands in the list, so a press shows it did something */
            char place[24];
            snprintf(place, sizeof(place), "%d / %d", g_user + 1, shim_usermods_count());
            shim_menu_write(FONT_ROW, place, cx, cy + (float) S()->baseline - CAP_BOTTOM[FONT_ROW],
                            INK_R, INK_G, INK_B, 1.0f);
        } else if (uset(i) >= 0 && uset_kind(i) != USET_KIND_SWITCH) {
            /* a choice's word or a number, in the menu font on the row's baseline (a switch is ON/OFF below) */
            char num[16];
            int k = uset(i), v = shim_usermods_setting_value(g_user, k);
            const char *text = num;
            if (uset_kind(i) == USET_KIND_CHOICE) text = uset_value_label(k, v);
            else snprintf(num, sizeof(num), "%d", v);
            shim_menu_write(FONT_ROW, text, cx, cy + (float) S()->baseline - CAP_BOTTOM[FONT_ROW], INK_R, INK_G, INK_B, 1.0f);
        } else if (is_action(i)) {
            /*
             * The actions are their names alone: CONFIG FILE, IMPORT SAVES,
             * EXPORT SAVES act on a tap, and the OPEN that stood beside them
             * said nothing the name did not (the owner, 24.09.2026).
             */
        } else if (sp && sp->kind == SP_CHOICE) {
            frame = sp->choice + sp_value(sp->ms);
            render(art, "Words", frame, cx, cy + (float) S()->baseline - S()->word_h[frame], 1.0f, 1.0f);
        } else if (sp && sp->kind == SP_NUM) {
            char text[16];
            shim_modset_format(sp->ms, sp->comp, 0, text, sizeof(text));
            float x = cx;
            for (const char *c = text; *c; c++) {
                const char *at = strchr(SP_DIGITS, *c);
                if (!at) continue;
                int d = (int) (at - SP_DIGITS);
                render(art, "Words", d, x, cy + (float) S()->baseline - S()->word_h[d], 1.0f, 1.0f);
                x += (float) S()->word_w[d] + 1.0f;
            }
        } else if (is_ticks(i)) {
            render(art, "Slider", ticks_of(i), cx + 2.0f, cy + 2.0f, 1.0f, 1.0f);
        } else if (ID(i) == R_SCALE || ID(i) == R_LANG) {
            if (ID(i) == R_SCALE) frame = scale_frame();
            else {
                const char *pin = shim_eid_language_pinned();
                frame = TW_SCALE_WORDS + (pin[0] ? lang_frame_of(pin) : 0);
            }
            render(art, "Words", frame, cx, cy + (float) S()->baseline - S()->word_h[frame], 1.0f, 1.0f);
        } else {
            /* ON and OFF are this set's own cuts (the Russian OFF is 54 long,
               the English OFF 39). Turned a quarter clockwise about (16,16), a
               word w long starts w - 16 left of where it is put; y 15 is where
               the settings rows put it on their 20-unit step, whose words stand
               on a baseline of 17 - so it is taken from the baseline, and the
               32-unit rows of MODS and the settings pages get it level with
               their names too (the owner, 24.09.2026: ON stood above the name) */
            int on = switch_on(i);
            float len = (float) (on ? S()->on_w : S()->off_w);
            render(art, on ? "On" : "Off", 0, cx + len - 16.0f, cy + (float) S()->baseline - 2.0f,
                   1.0f, 1.0f);
            /* on the MODS list, the PC page's arrow beside a mod with a page */
            if (has_arrow(i)) {
                float ax = cx + (float) (S()->on_w > S()->off_w ? S()->on_w : S()->off_w) + (float) TW_GAP;
                render(art, "Arrow", 0, ax, cy + floorf((float) S()->step * 0.5f), 1.0f, 1.0f);
            }
        }
        if (i == cursor) {
            to_canvas(name_x, top, off_x, off_y, &cx, &cy);
            /* 14 on the settings rows' baseline of 17: the same from any baseline */
            render(art, "Cursor", 0, cx - 6.0f, cy + (float) S()->baseline - 3.0f, 1.0f, 1.0f);
        }
    }
    if (live) draw_live_text(art, off_x, off_y);
    if (g_pop) popup_draw(w, h);
}

/* our own pictures, into the resource table beside the game's */
void shim_tweaks_open_assets(void *manager) {
    if (!manager) return;
    for (int i = 0; i < TW_SETS; i++) {
        char name[48];
        snprintf(name, sizeof(name), "port_tweaks_menu_%s.anm2", TW_SET[i].suffix);
        shim_asset_bring_in(manager, name, "gfx", name);
        snprintf(name, sizeof(name), "port_tweaks_page_%s.png", TW_SET[i].suffix);
        shim_asset_bring_in(manager, name, "gfx", name);
        snprintf(name, sizeof(name), "port_tweaks_words_%s.png", TW_SET[i].suffix);
        shim_asset_bring_in(manager, name, "gfx", name);
        snprintf(name, sizeof(name), "port_mods_menu_%s.anm2", TW_SET[i].suffix);
        shim_asset_bring_in(manager, name, "gfx", name);
        snprintf(name, sizeof(name), "port_mods_page_%s.png", TW_SET[i].suffix);
        shim_asset_bring_in(manager, name, "gfx", name);
        snprintf(name, sizeof(name), "port_mods_words_%s.png", TW_SET[i].suffix);
        shim_asset_bring_in(manager, name, "gfx", name);
        snprintf(name, sizeof(name), "port_mods_sheet_%s.png", TW_SET[i].suffix);
        shim_asset_bring_in(manager, name, "gfx", name);
        snprintf(name, sizeof(name), "port_mods_paper_%s.png", TW_SET[i].suffix);
        shim_asset_bring_in(manager, name, "gfx", name);
        /* the mods' settings pages: one picture a page, one word sheet for all four */
        snprintf(name, sizeof(name), "port_modset_words_%s.png", TW_SET[i].suffix);
        shim_asset_bring_in(manager, name, "gfx", name);
        for (int k = 0; k < SP_PAGES; k++) {
            snprintf(name, sizeof(name), "port_modset_menu_%d_%s.anm2", k, TW_SET[i].suffix);
            shim_asset_bring_in(manager, name, "gfx", name);
            snprintf(name, sizeof(name), "port_modset_page_%d_%s.png", k, TW_SET[i].suffix);
            shim_asset_bring_in(manager, name, "gfx", name);
        }
        /* the controls guide, in every set's own words (tools/guide_slides.py) */
        snprintf(name, sizeof(name), "port_guide_%s.anm2", TW_SET[i].suffix);
        shim_asset_bring_in(manager, name, "gfx", name);
        snprintf(name, sizeof(name), "port_guide_%s.png", TW_SET[i].suffix);
        shim_asset_bring_in(manager, name, "gfx", name);
        /* and its slides with a pad button in the other families' pictures (guide_slides.py write_family) */
        for (int f = 1; f < GD_FAMILIES; f++) {
            snprintf(name, sizeof(name), "port_guide_%s_%s.anm2", TW_SET[i].suffix, GD_FAMILY[f]);
            shim_asset_bring_in(manager, name, "gfx", name);
            snprintf(name, sizeof(name), "port_guide_%s_%s.png", TW_SET[i].suffix, GD_FAMILY[f]);
            shim_asset_bring_in(manager, name, "gfx", name);
        }
        /* the spawner card's heading, the same hand (tools/itemspawn_title.py) */
        snprintf(name, sizeof(name), "port_itemspawn_title_%s.anm2", TW_SET[i].suffix);
        shim_asset_bring_in(manager, name, "gfx", name);
        snprintf(name, sizeof(name), "port_itemspawn_title_%s.png", TW_SET[i].suffix);
        shim_asset_bring_in(manager, name, "gfx", name);
        /* and its tabs and headings (tools/spawner_tabs.py) */
        snprintf(name, sizeof(name), "port_spawner_tabs_%s.anm2", TW_SET[i].suffix);
        shim_asset_bring_in(manager, name, "gfx", name);
        snprintf(name, sizeof(name), "port_spawner_tabs_%s.png", TW_SET[i].suffix);
        shim_asset_bring_in(manager, name, "gfx", name);
    }
    /* the spawner's pictures for every language: enemies, rooms, stats */
    static const char *const SPAWNER_ART[] = {
        "port_enemy_icons.anm2", "port_enemy_icons.png", "port_room_icons.anm2", "port_room_icons.png",
        "port_stat_icons.anm2", "port_stat_icons.png",
        "port_pickup_icons.anm2", "port_pickup_icons.png",      /* ITEMS' pickups and loot, STATS' health (spawner_pickups.py) */
        "port_enemy_pulse.anm2", "port_enemy_pulse.png",        /* Pulsating's sizes (spawner_enemies.py) */
        "port_mods_popup.anm2", "port_mods_popup.png",          /* the conflict popup (tools/mods_popup.py) */
    };
    for (unsigned i = 0; i < sizeof(SPAWNER_ART) / sizeof(SPAWNER_ART[0]); i++)
        shim_asset_bring_in(manager, SPAWNER_ART[i], "gfx", SPAWNER_ART[i]);
    LOG("tweaks: the pictures of %d language sets are in the resource table", TW_SETS);
}
