/*
 * DESK STAND FOR THE MODS PAGE (tweaks.c), 02.10.2026: the real tweaks.c over
 * the real usermods.c + json.c + content.c (tools/umdesk's harness, its main
 * renamed), one process = one start of the game and a script of the player's
 * moves.
 *
 * The game is played by an "image": a block where tweaks.c finds what it reads
 * at fixed places (the manager and its language, the menu with its anchors and
 * camera, the drawing area) and, at the addresses it calls (the sprite's load,
 * frame and render, the sound), a jump to a function here that writes down
 * what would be drawn where. The menu font is measured from the game's own .fnt
 * files (sum of the advances, as the preview tool measures it).
 *
 *   tweaksdesk.exe <config dir> <canvas w> <canvas h> <language set 0..6> <move>...
 *     mods                 the MODS page opens (the main menu's MODS item)
 *     user <k>             the player's mod k's page, as its arrow opens it
 *     pad <up|down|left|right|confirm|back>
 *     tap <x> <y>          a finger down and up at a canvas point
 *     swipe <x> <y> <dx>   a finger down, sideways dx canvas units in small moves, up
 *     drag <x> <y> <dy>    a finger down, up or down dy canvas units, up
 *     draw                 one frame drawn: every picture and line of text, and every row's box
 *     state                where the page is
 *     guard                the game asks its first file (DESK_ROOT the pack's root): the guard runs, the notes have their facts
 *     notes                every line of the page's notes; a mod's lines over its title (LINK), its details (DETAIL),
 *                          the conflict popup (POPUP, POPLINE, POPBTN)
 *     detail               the open mod's details, as a tap on a line over its title opens them
 *     put <dir> <folder> <keep|->
 *                          ADD MOD while the page is open (hotfix 1.73: no restart after it): the staged mod <dir> is
 *                          renamed into the mods folder as <folder>, <keep> (an update's old folder) marked .remove,
 *                          and the installer's listPut is called, as UserMods does from its own thread
 *     drop <dir> <folder>  (hotfix 1.74) a mod's folder put into mods/ by hand while the game runs
 *     disk                 (hotfix 1.74) the page asks with the disk's look due now: RESTART TO APPLY worked out
 */
#include "compat.h"
#include <math.h>
#include <errno.h>

#include "../../port/app/src/main/cpp/tweaks.c"

#ifndef ISAAC_ROOT                  /* the project root, ending in "/"; the build passes it */
#define ISAAC_ROOT "../../"
#endif

void *desk_low(size_t n);

/* ---------------------------------------------------------------- the image */
#define IMAGE_BYTES 0xac3000
#define MANAGER_BYTES 0x60000
#define MENU_BYTES (0x10fc08 + 32 * 8)
static unsigned char *g_img, *g_mgr, *g_mnu;
static float g_cw = 480.0f, g_ch = 270.0f;
#define PX 4.0f                     /* pixels to a canvas unit */

unsigned long shim_image_base(void) { return (unsigned long) (uintptr_t) g_img; }
float shim_screen_width(void) { return g_cw; }
float shim_screen_height(void) { return g_ch; }

/* what one frame draws */
static int g_drawing;
typedef struct { const void *sprite; const char *anim; int frame; } sprite_at_t;
static sprite_at_t g_spr[64];
static int g_nspr;

static void fake_sound(unsigned long manager, int id, int a, int b, float v, float p) {
    (void) manager; (void) a; (void) b; (void) v; (void) p;
    printf("SOUND %d\n", id);
}
static void fake_sprite_new(void *sprite) { (void) sprite; }
static void fake_sprite_load(void *sprite, const void *path, int x) { (void) path; (void) x; ((unsigned char *) sprite)[SPRITE_LOADED] = 1; }
static void fake_sprite_frame(void *sprite, const char *anim, int frame) {
    int i = 0;
    while (i < g_nspr && g_spr[i].sprite != sprite) i++;
    if (i == g_nspr && g_nspr < 64) g_nspr++;
    if (i < 64) { g_spr[i].sprite = sprite; g_spr[i].anim = anim; g_spr[i].frame = frame; }
}
static void fake_sprite_render(void *sprite, const float *pos, const void *a, const void *b) {
    (void) a; (void) b;
    for (int i = 0; i < g_nspr; i++)
        if (g_spr[i].sprite == sprite && g_drawing) {
            const float *scale = (const float *) ((unsigned char *) sprite + SPRITE_SCALE);
            printf("OP render %s %d %.3f %.3f %.3f %.3f\n", g_spr[i].anim, g_spr[i].frame, pos[0], pos[1], scale[0], scale[1]);
        }
}

/* x64: mov rax, imm64; jmp rax - the call lands in the function above with its arguments as they were */
static void trampoline(unsigned long at, void *to) {
    unsigned char *p = g_img + at;
    p[0] = 0x48; p[1] = 0xB8;
    memcpy(p + 2, &to, 8);
    p[10] = 0xFF; p[11] = 0xE0;
}

static void image_make(int language) {
    /* below 4 GB: tweaks.c keeps addresses in `unsigned long`, which is 32 bits here and 64 on the phone; the
       words stored are written whole (8 bytes), so a read of either width finds the same address */
    g_img = (unsigned char *) desk_low(IMAGE_BYTES);
    g_mgr = (unsigned char *) desk_low(MANAGER_BYTES);
    g_mnu = (unsigned char *) desk_low(MENU_BYTES);
    if (!g_img || !g_mgr || !g_mnu) { fprintf(stderr, "no low memory\n"); exit(3); }
    memset(g_img, 0, IMAGE_BYTES);
    memset(g_mgr, 0, MANAGER_BYTES);
    memset(g_mnu, 0, MENU_BYTES);
    *(unsigned long long *) (g_img + GAME_MANAGER) = (unsigned long long) (uintptr_t) g_mgr;
    *(unsigned long long *) (g_img + MENU_PTR) = (unsigned long long) (uintptr_t) g_mnu;
    *(int *) (g_mgr + LANGUAGE) = language;
    unsigned *area = (unsigned *) (g_img + AREA);
    area[0] = (unsigned) (g_cw * PX); area[1] = (unsigned) (g_ch * PX); area[2] = 0; area[3] = 0;
    *(int *) (g_mnu + MENU_SCREEN) = SCREEN_TITLE;
    *(int *) (g_mnu + MAIN_MENU + MAIN_SELECTED) = ITEM_MODS;
    trampoline(FN_PLAY_SOUND, (void *) fake_sound);
    trampoline(FN_SPRITE_NEW, (void *) fake_sprite_new);
    trampoline(FN_SPRITE_LOAD, (void *) fake_sprite_load);
    trampoline(FN_SPRITE_FRAME, (void *) fake_sprite_frame);
    trampoline(FN_SPRITE_RENDER, (void *) fake_sprite_render);
}

/* the camera there at once: the game's closes a tenth a frame, the desk asks where it comes to rest */
static void settle(void) {
    float *a = anchor_of((unsigned long) (uintptr_t) g_mnu, g_page->screen);
    float *cam = (float *) (g_mnu + MENU_CAMERA);
    cam[0] = a[0] + half_x();
    cam[1] = a[1] + half_y();
}

/* ---------------------------------------------------------------- the menu font, from the game's .fnt */
typedef struct { int adv[0x10000]; int loaded; } font_t;
static font_t g_font[3];
/* the port's own copies of the game's three (tools/menu_fonts.py): what eid.c draws the pages with */
static const char *const FONT_FILE[3] = { "port_menufont16bold", "port_menufont20bold", "port_menufont10" };

static void font_load(int k) {
    font_t *f = &g_font[k];
    f->loaded = 1;
    char p[400];
    snprintf(p, sizeof(p), ISAAC_ROOT "port/app/src/main/assets/%s.fnt", FONT_FILE[k]);
    FILE *fp = fopen(p, "rb");
    if (!fp) { fprintf(stderr, "no font %s\n", p); return; }
    static unsigned char b[1 << 20];
    size_t n = fread(b, 1, sizeof(b), fp);
    fclose(fp);
    for (size_t at = 4; at + 5 <= n;) {
        int kind = b[at];
        unsigned size = (unsigned) b[at + 1] | (unsigned) b[at + 2] << 8 | (unsigned) b[at + 3] << 16 | (unsigned) b[at + 4] << 24;
        size_t body = at + 5;
        if (kind == 4)
            for (size_t i = body; i + 20 <= body + size && i + 20 <= n; i += 20) {
                unsigned id = (unsigned) b[i] | (unsigned) b[i + 1] << 8 | (unsigned) b[i + 2] << 16 | (unsigned) b[i + 3] << 24;
                short adv = (short) (b[i + 16] | b[i + 17] << 8);
                if (id < 0x10000) f->adv[id] = adv;
            }
        at = body + size;
    }
}

static unsigned next_cp(const unsigned char **s) {
    const unsigned char *p = *s;
    unsigned c = *p++;
    if (c >= 0xF0 && p[0] && p[1] && p[2]) { c = (c & 7) << 18 | (p[0] & 63) << 12 | (p[1] & 63) << 6 | (p[2] & 63); p += 3; }
    else if (c >= 0xE0 && p[0] && p[1]) { c = (c & 15) << 12 | (p[0] & 63) << 6 | (p[1] & 63); p += 2; }
    else if (c >= 0xC0 && p[0]) { c = (c & 31) << 6 | (p[0] & 63); p += 1; }
    *s = p;
    return c;
}

float shim_menu_text_width(int which, const char *text) {
    if (which < 0 || which > 2 || !text) return 0.0f;
    if (!g_font[which].loaded) font_load(which);
    int w = 0;
    for (const unsigned char *p = (const unsigned char *) text; *p;) {
        unsigned c = next_cp(&p);
        if (c < 0x10000) w += g_font[which].adv[c];
    }
    return (float) w;
}

void shim_menu_write(int which, const char *text, float x, float y, float r, float g, float b, float a) {
    (void) r; (void) g; (void) b;
    /* a pale or grey line says its alpha on a line of its own before it, not an OP line (hotfix 1.73: THE PALE NAME) */
    if (g_drawing && a < 0.999f) printf("ALPHA %.3f\n", a);
    if (g_drawing) printf("OP write %d %.3f %.3f %.0f %s\n", which, x, y, shim_menu_text_width(which, text), text);
}

/* ---------------------------------------------------------------- the rest of the port, as quiet as it can be */
int shim_sticks_in_game(void) { return 0; }
void shim_menu_page_sound(void) {}
void shim_str_assign(void *s, const char *text) { (void) s; (void) text; }
void shim_str_dtor(void *s) { (void) s; }
void shim_asset_bring_in(void *mgr, const char *name, const char *folder, const char *leaf) { (void) mgr; (void) name; (void) folder; (void) leaf; }
int shim_screen_shot_ceiling_get(void) { return 0; }
void shim_screen_shot_ceiling(int n) { (void) n; }
int shim_screen_shot_rule(void) { return 4; }
float shim_sticks_dead_share(void) { return 0.25f; }
float shim_sticks_sens(void) { return 0.55f; }
float shim_sticks_sens_shoot(void) { return 0.55f; }
void shim_sticks_set_deadzone(float v) { (void) v; }
void shim_sticks_set_sens(float v) { (void) v; }
void shim_sticks_set_sens_shoot(float v) { (void) v; }
void shim_sticks_settings_save(void) {}
const char *shim_eid_language_pinned(void) { return ""; }
void shim_eid_language_pin(const char *code) { (void) code; }
int shim_eid_language_count(void) { return 0; }
const char *shim_eid_language_code(int i) { (void) i; return ""; }
void shim_port_action_text(int what, const char *text) { printf("ACTION %d %s\n", what, text); }
void shim_port_action(int what) { printf("ACTION %d\n", what); }
int shim_rumble_level(void) { return 7; }
void shim_rumble_level_set(int n, int by_pad) { (void) n; (void) by_pad; }
void shim_rumble_probe(int by_pad) { (void) by_pad; }
int shim_modset_int(int id) { (void) id; return 0; }
void shim_modset_step(int id, int comp, int dir) { (void) id; (void) comp; (void) dir; }
void shim_modset_format(int id, int comp, int which, char *out, unsigned long n) { (void) id; (void) comp; (void) which; snprintf(out, n, "0"); }
int shim_eid_glitch_info_get(void) { return 0; }
void shim_eid_glitch_info(int on) { (void) on; }
int shim_gl_height(void) { return (int) (g_ch * PX); }
int shim_spawner_lock_get(void) { return 0; }
int shim_pad_icon_family(void) { return 0; }        /* the guide's pad pictures (1.72): the Xbox family */
void shim_spawner_lock(int on) { (void) on; }

int umdesk_main(int argc, char **argv);
void shim_usermods_setup(const char *config_dir);

/* ---------------------------------------------------------------- the moves */
static void show_state(void) {
    float *a = anchor_of((unsigned long) (uintptr_t) g_mnu, g_page->screen);
    printf("STATE page=%s user=%d cursor=%d cursor_id=%d rows=%d paper_w=%d paper_h=%d paper_x=%d scrolled=%.3f room=%.3f "
           "view_bottom=%.3f restart=%d popup=%d pending=%d links=%d\n", g_page->name, g_user, g_cursor, ID(g_cursor), ROWS(),
           S()->paper_w, S()->paper_h, S()->paper_x, g_base[1] - a[1], scroll_room(), view_bottom(), restart_row(), g_pop,
           g_pop_pending, user_links());
}

static void show_rows(void) {
    float off_x, off_y;
    travel(&off_x, &off_y);
    for (int i = 0; i < ROWS(); i++) {
        float name_x, value_x, x0, y0, x1, y1;
        row_layout(i, &name_x, &value_x);
        to_canvas(0.0f, row_top(i), off_x, off_y, &x0, &y0);
        to_canvas((float) S()->paper_w, row_top(i) + row_h(i), off_x, off_y, &x1, &y1);
        float nx, ny, vx, vy;
        to_canvas(name_x, row_top(i), off_x, off_y, &nx, &ny);
        to_canvas(value_x, row_top(i), off_x, off_y, &vx, &vy);
        printf("ROW %d id=%d box=%.3f,%.3f,%.3f,%.3f name_x=%.3f value_x=%.3f heading=%d uset=%d kind=%d\n", i, ID(i), x0, y0,
               x1, y1, nx, vx, is_heading(i), uset(i), uset_kind(i));
    }
}

static void touch(int id, int phase, float gx, float gy) { shim_tweaks_touch(id, phase, gx * PX, gy * PX); }

int main(int argc, char **argv) {
    setvbuf(stdout, NULL, _IONBF, 0);
    if (argc < 5) { fprintf(stderr, "tweaksdesk.exe <config> <w> <h> <set> moves...\n"); return 2; }
    g_cw = (float) atof(argv[2]);
    g_ch = (float) atof(argv[3]);
    /* the game's language numbers by set (tweaks_layout.h tw_set_for_language): en ru de es jp kr zh */
    static const int LANG_OF_SET[7] = { 0, 10, 5, 4, 2, 11, 13 };
    int set = atoi(argv[4]);
    image_make(LANG_OF_SET[set >= 0 && set < 7 ? set : 0]);
    shim_usermods_setup(argv[1]);
    for (int a = 5; a < argc; a++) {
        const char *m = argv[a];
        printf("== %s\n", m);
        if (!strcmp(m, "mods")) {
            unsigned long regs[4] = { 0, SCREEN_OPTIONS, 0, 0 };
            shim_mods_screen((unsigned long) (uintptr_t) g_mnu, SCREEN_OPTIONS, regs);
        } else if (!strcmp(m, "user") && a + 1 < argc) {
            int k = atoi(argv[++a]);
            g_cursor = MD_OWN + k;
            open_settings(MD_OWN + k);
        } else if (!strcmp(m, "pad") && a + 1 < argc) {
            const char *w = argv[++a];
            int act = !strcmp(w, "up") ? PAGE_UP : !strcmp(w, "down") ? PAGE_DOWN : !strcmp(w, "left") ? PAGE_LEFT
                    : !strcmp(w, "right") ? PAGE_RIGHT : !strcmp(w, "confirm") ? PAGE_CONFIRM : PAGE_BACK;
            shim_tweaks_pad(act);
        } else if (!strcmp(m, "tap") && a + 2 < argc) {
            float x = (float) atof(argv[a + 1]), y = (float) atof(argv[a + 2]);
            a += 2;
            touch(1, 0, x, y);
            touch(1, 3, x, y);
        } else if ((!strcmp(m, "swipe") || !strcmp(m, "drag")) && a + 3 < argc) {
            float x = (float) atof(argv[a + 1]), y = (float) atof(argv[a + 2]), d = (float) atof(argv[a + 3]);
            int side = !strcmp(m, "swipe");
            a += 3;
            touch(2, 0, x, y);
            int steps = (int) (fabsf(d) / 2.0f) + 1;
            for (int s = 1; s <= steps; s++) {
                float t = d * (float) s / (float) steps;
                touch(2, 1, side ? x + t : x, side ? y : y + t);
                settle();
            }
            touch(2, 3, side ? x + d : x, side ? y : y + d);
        } else if (!strcmp(m, "draw")) {
            settle();
            g_drawing = 1;
            shim_tweaks_draw();
            g_drawing = 0;
            show_rows();
        } else if (!strcmp(m, "guard")) {
            /* the game's first file (DESK_ROOT the pack's root): the guard runs, the page's notes have their facts */
            void umdesk_root_from_env(void);
            const char *shim_usermods_map(const char *p);
            umdesk_root_from_env();
            char p[1400];
            const char *r = getenv("DESK_ROOT");
            snprintf(p, sizeof(p), "%s/repentance-resources/data/nothing.xml", r ? r : "");
            shim_usermods_map(p);
        } else if (!strcmp(m, "put") && a + 3 < argc) {
            /* ADD MOD while the page is open: the installer's rename into the mods folder, the old copy marked, listPut */
            void umdesk_list_put(const char *first, const char *keep, char **off, int noff);
            const char *shim_usermods_dir(void);
            const char *dir = argv[a + 1], *folder = argv[a + 2], *keep = argv[a + 3];
            a += 3;
            char to[900];
            snprintf(to, sizeof(to), "%s/%s", shim_usermods_dir(), folder);
            printf("== move %s: %s\n", folder, rename(dir, to) ? strerror(errno) : "ok");
            char *off[1] = { (char *) keep };
            if (strcmp(keep, "-")) {
                char mark[900];
                snprintf(mark, sizeof(mark), "%s/%s/.remove", shim_usermods_dir(), keep);
                FILE *f = fopen(mark, "w");
                if (f) fclose(f);
                umdesk_list_put(folder, keep, off, 1);
            } else {
                umdesk_list_put(folder, NULL, NULL, 0);
            }
        } else if (!strcmp(m, "drop") && a + 2 < argc) {
            /* a mod's folder put into mods/ by hand while the game runs (CONFIG FILE, hotfix 1.74) */
            const char *shim_usermods_dir(void);
            char to[900];
            snprintf(to, sizeof(to), "%s/%s", shim_usermods_dir(), argv[a + 2]);
            printf("== drop %s: %s\n", argv[a + 2], rename(argv[a + 1], to) ? strerror(errno) : "ok");
            a += 2;
        } else if (!strcmp(m, "disk")) {
            /* the page's next ask looks at the disk at once (every 2 s on the phone, THE RESTART OWED) */
            void umdesk_disk_now(void);
            umdesk_disk_now();
            printf("== disk changed=%d\n", shim_usermods_changed());
        } else if (!strcmp(m, "detail")) {
            /* the open mod's details, as a tap on a line over its title opens them */
            open_detail();
        } else if (!strcmp(m, "notes")) {
            /* every line of the page's notes, drawn on screen or scrolled away; a mod's lines over its title; its
               details page's lines; the popup's */
            for (int i = 0; i < g_note_n; i++) printf("NOTE %d %s\n", i, g_note[i]);
            /* hotfix 1.71: every line over the title is red; a mod's name is red when the port does not use all of it */
            for (int i = 0; i < user_links(); i++) printf("LINK %d red %s\n", i, g_link_text[i]);
            for (int k = 0; k < shim_usermods_count(); k++)
                printf("NAME %d %s %s\n", k, mod_red(k) ? "red" : shim_usermods_pending(k) ? "pale" : "ink", shim_usermods_name(k));
            if (g_page == &PAGES[PAGE_DETAIL])
                for (int i = 0; i < g_dl_n; i++)
                    printf("DETAIL %d x=%d y=%d font=%d ink=%s %s\n", i, g_dl[i].x, g_dl[i].y, g_dl[i].font,
                           g_dl[i].ink == 1 ? "red" : g_dl[i].ink == 2 ? "grey" : "ink", g_dl[i].text);
            if (g_pop) {
                printf("POPUP title=%s btn=%d mod=%d\n", g_pop_title, g_pop_btn, g_pop_mod);
                for (int i = 0; i < g_pop_lines; i++) printf("POPLINE %d %s\n", i, g_pop_line[i]);
                for (int k = 0; k < 3; k++) printf("POPBTN %d x=%.3f w=%.3f y=%.3f %s\n", k, g_pop_bx[k], g_pop_bw[k], g_pop_by,
                                                   g_pop_btn_text[k]);
            }
        } else if (!strcmp(m, "state")) {
        } else {
            printf("?? %s\n", m);
        }
        settle();
        if (g_open) show_state();
        else printf("STATE closed\n");
    }
    return 0;
}
