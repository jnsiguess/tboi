/*
 * THE RESET DESK (08.10.2026): the run restart's one holder, "the finger OR the
 * restart key", checked without the phone.
 *
 * The real reset.c in this unit (src_reset.c, `long` widened for Windows), the
 * device's key-down / key-up (0x8ddc9c / 0x8ddcb0) answered by two stubs that
 * write the keyboard device's SET as the game's do (RESET_KEY_FN). Around it a
 * model of the game, from the disassembly (the research notes of 08.10.2026, the R key):
 *
 *   each update: the device's CURRENT := SET; then, with the manager RUNNING
 *   and no dialog up (0x671238 -> 0x6644a4), 0x88d988: all four raw ids held
 *   -> count fade+0x10 up (not while the fade is a reset); past 15 and not
 *   refused (Eden without a token, a daily run) -> fade kind 5, count 0; not
 *   all held -> count 0, and a reset fade turns back (0x88dae4). A reset fade
 *   ends after 30 updates: the run is reset (case 5) and the new one fades in.
 *
 * and a model of objc.c shim_keys_deliver's order for the keyboard: a release
 * goes to reset.c before anything of the port's may keep it; a press reaches it
 * only when the game gets it (the spawner's card takes the keyboard whole).
 *
 *   py -3 tools/resetdesk/run.py          every case; exit 1 on any failure
 *   DESK_LOG=1 py -3 tools/resetdesk/run.py   with reset.c's log lines
 */
#include <stdarg.h>

static unsigned char g_set[0x401];          /* the keyboard device's SET, as 0x8ddc9c writes it */
static unsigned char g_cur[0x401];          /* CURRENT, as the device's update copies it */
static int g_downs, g_ups;                  /* calls of the two, per id 8 */
static void desk_key_down(void *dev, int id) { (void) dev; g_set[id] = 1; if (id == 8) g_downs++; }
static void desk_key_up(void *dev, int id) { (void) dev; g_set[id] = 0; if (id == 8) g_ups++; }
#define RESET_KEY_FN(b, on) ((on) ? desk_key_down : desk_key_up)

#include "src_reset.c"

/* ------------------------------------------------------------------ the clock: 16 ms a frame */
static long long g_ms = 1000000;
int desk_clock_gettime(int clock, struct timespec *ts) {
    (void) clock;
    ts->tv_sec = (time_t) (g_ms / 1000);
    ts->tv_nsec = (long) (g_ms % 1000) * 1000000L;
    return 0;
}

/* ------------------------------------------------------------------ the fake image and game */
static unsigned char *g_img, *g_game, *g_mgr;
#define IMG_SIZE 0xB00000
#define M_STATE 0x204c0
unsigned long long shim_image_base(void) { return (unsigned long long) g_img; }

static int g_in_game_calls;
int shim_sticks_in_game(void) { g_in_game_calls++; return *(int *) (g_mgr + M_STATE) == 2; }
static void set_state(int s) { *(int *) (g_mgr + M_STATE) = s; }
static void set_pause(int on) { *(int *) (g_game + G_PAUSE_STATE) = on; }
static void set_dialogs(int n) { *(unsigned long long *) (g_mgr + M_DIALOGS) = (unsigned long long) n; }
static int kind_now(void) { return *(int *) (g_game + G_FADE_KIND); }

static unsigned char g_bound[256];          /* the live table, action 23's row as HID usages */
static int g_bound_calls;
#ifndef DESK_REAL_TABLE
int shim_keys_restart_bound(int hid) { g_bound_calls++; return hid >= 0 && hid < 256 && g_bound[hid]; }
#else
/*
 * 1.74: the REAL table lookup instead (objc.c, cut out by run.py into
 * src_objc_keys.c: glfw_of, kb_device, kb_bound and shim_keys_restart_bound
 * with whatever helps them), over a keyboard device laid out in the fake image
 * the way the game keeps it. tables_cases.c holds the cases.
 */
int shim_eid_readable(const void *p, unsigned long long n);
#include "src_objc_keys.c"
#include <pthread.h>
#include "src_uikit_keys.c"                 /* uikit.c's key queue, the real code */
#include "tables.h"                         /* the image's tables and the fixtures (run.py) */
#endif

static int g_pushes;                        /* touches handed to the game */
void shim_touch_push_raw(int id, int phase, float x, float y) { (void) id; (void) phase; (void) x; (void) y; g_pushes++; }
const char *shim_str_bytes(const void *s) { (void) s; return ""; }
float shim_screen_width(void) { return 480.0f; }
float shim_screen_height(void) { return 270.0f; }
int shim_screen_finger_units(float x, float y, float *u, float *v) { *u = x; *v = y; return 1; }
void shim_devices_pause_pressed(void) { }
int shim_report_active(void) { return 0; }
void report_line(const char *kind, const char *text) { (void) kind; (void) text; }
int shim_eid_readable(const void *p, unsigned long long n) { (void) p; (void) n; return 1; }

static int g_log;
int __android_log_print(int prio, const char *tag, const char *fmt, ...) {
    (void) prio; (void) tag;
    if (!g_log) return 0;
    va_list a;
    va_start(a, fmt);
    printf("      log: ");
    vprintf(fmt, a);
    printf("\n");
    va_end(a);
    return 0;
}

/* the game's side: 0x88d988 and the fade, one update a frame */
static int g_count, g_fade_t, g_resets, g_refuse, g_fades, g_reverts;
static void game_update(void) {
    memcpy(g_cur, g_set, sizeof(g_cur));
    int *kind = (int *) (g_game + G_FADE_KIND);
    if (*(int *) (g_mgr + M_STATE) == 2 && *(unsigned long long *) (g_mgr + M_DIALOGS) == 0) {
        int all = g_cur[8] && g_cur[0xb] && g_cur[9] && g_cur[0xc];
        if (all) { if (*kind != FADE_RESET) g_count++; }
        else g_count = 0;
        int ripe = !g_refuse && g_count > 15;
        if (all && ripe && *kind != FADE_RESET) { *kind = FADE_RESET; g_count = 0; g_fade_t = 0; g_fades++; }
        else if (!all && *kind == FADE_RESET) { *kind = 0; g_reverts++; }
    }
    if (*kind == FADE_RESET && ++g_fade_t >= 30) { *kind = 0; g_resets++; }
}

/* ------------------------------------------------------------------ the keyboard, in objc.c's order */
static int g_card_open;                     /* the spawner's card takes every press */
#define EV_MAX 16
static struct { int hid, down; } g_ev[EV_MAX];
static int g_evn;
static void key(int hid, int down) { g_ev[g_evn].hid = hid; g_ev[g_evn].down = down; g_evn++; }
static void deliver(void) {
#if defined(DESK_REAL_TABLE) && DESK_HAS_TURN
    if (g_evn) kb_table_to_ios();                   /* objc.c: a frame with keys, before any goes on (1.74) */
#endif
    for (int i = 0; i < g_evn; i++) {
        int hid = g_ev[i].hid, down = g_ev[i].down;
        if (!down) shim_reset_key(hid, 0);          /* before the card or the straps may keep it */
        if (g_card_open && down) continue;          /* the card's */
        if (down) shim_reset_key(hid, 1);           /* the game gets it */
    }
    g_evn = 0;
}

/* one frame: the game's update, then between frames as gl.c orders it (reset, then the keys) */
static void frame(void) {
    game_update();
    shim_reset_between_frames();
    deliver();
    g_ms += 16;
}
static void frames(int n) { while (n-- > 0) frame(); }

/* the finger on the pause button, in screen pixels = units here */
static void finger(int phase) { shim_reset_touch(7, phase, BUTTON_X, BUTTON_Y); }

/* ------------------------------------------------------------------ the cases */
static int g_fail, g_cases;
#define CHECK(c, ...) do { if (!(c)) { g_fail++; printf("  FAIL %s:%d: ", __func__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

#define HID_R 0x15
#define HID_T 0x17
#define HID_F 0x09
#define HID_W 0x1a

static void fresh(void) {
    /* let everything go the way the port would, then wipe the model */
    for (int i = 0; i < 256; i++) shim_reset_key(i, 0);
    shim_reset_forget();
    set_state(2); set_pause(0); set_dialogs(0); g_card_open = 0; g_refuse = 0;
    frames(3);
    memset(g_bound, 0, sizeof(g_bound));
    g_bound[HID_R] = 1;                     /* the defaults: action 23 = R */
    *(int *) (g_game + G_FADE_KIND) = 0;
    g_count = g_fade_t = g_resets = g_fades = g_reverts = 0;
    g_downs = g_ups = g_pushes = g_bound_calls = g_in_game_calls = 0;
    g_evn = 0;
    g_cases++;
}
static int combo(void) { return g_set[8] && g_set[9] && g_set[0xb] && g_set[0xc]; }
static int combo_bits(void) {
    int n = 0;
    for (int i = 0; i < 0x401; i++) if (g_set[i]) n++;
    return n;
}

static void r_alone_to_the_end(void) {
    fresh();
    key(HID_R, 1); frame();
    CHECK(g_rkeys == 1, "R in a run is held for restart");
    frame();
    CHECK(combo() && combo_bits() == 4, "the combination, and nothing else, is held");
    frames(60);
    CHECK(g_fades == 1 && g_resets == 1, "one fade, one reset (fades %d resets %d)", g_fades, g_resets);
    frames(50);
    CHECK(g_resets == 2, "held on, it goes on as the pad's combination does (resets %d)", g_resets);
    key(HID_R, 0); frames(2);
    CHECK(!combo() && combo_bits() == 0, "let go: the combination is let go");
    CHECK(g_downs == 1 && g_ups == 1, "the device touched once each way (downs %d ups %d)", g_downs, g_ups);
    int calls = g_bound_calls, in_game = g_in_game_calls;
    frames(100);
    CHECK(g_bound_calls == calls && g_in_game_calls == in_game,
          "nothing held: nothing asked per frame (bound %d, in-game %d)", g_bound_calls - calls, g_in_game_calls - in_game);
    CHECK(g_pushes == 0, "no touch went to the game");
}

static void r_let_go_early(void) {
    fresh();
    key(HID_R, 1); frames(10);
    key(HID_R, 0); frames(60);
    CHECK(g_fades == 0 && g_resets == 0, "let go before the fade: nothing (fades %d)", g_fades);
    CHECK(!combo() && g_count == 0, "the count is back to nought");
}

static void r_let_go_in_the_fade(void) {
    fresh();
    key(HID_R, 1); frames(25);
    CHECK(kind_now() == FADE_RESET, "25 frames held: the screen darkens");
    key(HID_R, 0); frames(3);
    CHECK(kind_now() != FADE_RESET && g_reverts == 1, "let go in the fade: the game turns it back");
    frames(60);
    CHECK(g_resets == 0, "and nothing is reset");
}

static void finger_alone(void) {
    fresh();
    finger(0); frames(120);
    CHECK(g_resets == 1, "a long press is one reset however long it stays (resets %d)", g_resets);
    CHECK(!combo(), "after its reset the finger holds nothing");
    finger(3); frames(2);
    CHECK(g_pushes == 0, "the long press never reached the game as a pause (pushes %d)", g_pushes);
    fresh();
    finger(0); frames(5); finger(3); frames(2);
    CHECK(g_pushes == 2 && g_resets == 0 && !combo(), "a tap is a pause: press and release handed over");
}

static void both_r_first_let_go(void) {
    /* R first, then the finger; R let go long before the fade: the finger carries on, unbroken */
    fresh();
    key(HID_R, 1); frame();
    finger(0); frames(3);
    key(HID_R, 0); frames(2);
    CHECK(combo() && g_ups == 0, "R let go, the finger still on: the combination stays held");
    CHECK(g_count > 0, "and the count goes on (count %d)", g_count);
    frames(60);
    CHECK(g_fades == 1 && g_resets == 1, "the finger finishes the reset (fades %d resets %d)", g_fades, g_resets);
    finger(3); frames(2);
    CHECK(!combo() && g_downs == 1 && g_ups == 1, "one hold from the first press to the last release");
    CHECK(g_pushes == 0, "no pause");
}

static void both_finger_first_let_go(void) {
    /* the finger first, R joins; the finger lifts mid-fade: R carries on and the reset happens */
    fresh();
    finger(0); frames(2);
    key(HID_R, 1); frames(25);
    CHECK(kind_now() == FADE_RESET, "the screen darkens");
    finger(3); frames(2);
    CHECK(combo() && g_reverts == 0, "the finger lifted, R still held: the fade goes on");
    frames(40);
    CHECK(g_resets == 1, "reset (resets %d)", g_resets);
    CHECK(g_pushes == 0, "the finger was eaten by the fade, not a pause");
    key(HID_R, 0); frames(2);
    CHECK(!combo() && g_downs == 1 && g_ups == 1, "let go once, at the last hand (downs %d ups %d)", g_downs, g_ups);
}

static void r_over_a_spent_finger(void) {
    /* the finger's one reset is spent and it stays on; R pressed now is R's own hold */
    fresh();
    finger(0); frames(60);
    CHECK(g_resets == 1 && !combo(), "the finger's reset is done and it holds nothing");
    key(HID_R, 1); frames(60);
    CHECK(g_resets == 2, "R still restarts while the spent finger stays (resets %d)", g_resets);
    finger(3); frames(2);
    CHECK(combo(), "the finger lifted: R's hold untouched");
    key(HID_R, 0); frames(2);
    CHECK(!combo() && g_pushes == 0, "R let go: released, and no pause");
}

static void both_early_release(void) {
    /* both let go before the fade, in either order: nothing happens */
    fresh();
    key(HID_R, 1); finger(0); frames(4);
    finger(3); frames(2);
    CHECK(combo(), "the finger's tap leaves R's hold alone");
    key(HID_R, 0); frames(40);
    CHECK(g_fades == 0 && g_resets == 0 && !combo(), "both gone early: no fade");
    CHECK(g_pushes == 2, "the short finger was a tap, so a pause (pushes %d)", g_pushes);
    fresh();
    key(HID_R, 1); finger(0); frames(4);
    key(HID_R, 0); frames(2);
    CHECK(combo(), "R's release leaves the finger's hold alone");
    finger(3); frames(40);
    CHECK(g_fades == 0 && g_resets == 0 && !combo(), "both gone early: no fade");
}

static void gating(void) {
    fresh();
    set_state(1);                           /* a menu */
    key(HID_R, 1); frames(30);
    CHECK(g_rkeys == 0 && !combo(), "R in a menu: nothing");
    key(HID_R, 0); frame();
    set_state(2);
    key(HID_R, 1); frame(); key(HID_R, 0); frame();     /* press and release in the run: fine */

    fresh();
    set_pause(1);
    key(HID_R, 1); frames(30);
    CHECK(g_rkeys == 0 && !combo(), "R on the pause screen: nothing");
    key(HID_R, 0); frame();

    fresh();
    key(HID_R, 1); frames(10);
    CHECK(combo(), "held in the run");
    set_pause(1); frames(2);
    CHECK(!combo() && g_rkeys == 0, "the pause screen opens: let go");
    set_pause(0); frames(40);
    CHECK(!combo() && g_resets == 0, "back in the run with R still down: no restart until pressed again");
    key(HID_R, 0); frame();
    key(HID_R, 1); frames(60);
    CHECK(g_resets == 1, "pressed again: restart (resets %d)", g_resets);
    key(HID_R, 0); frame();

    fresh();
    key(HID_R, 1); frames(10);
    set_dialogs(1); frames(2);
    CHECK(!combo() && g_rkeys == 0, "a dialog of the game's: let go");
    set_dialogs(0);
    key(HID_R, 0); frame();

    fresh();
    key(HID_R, 1); frames(10);
    set_state(1); frames(2);
    CHECK(!combo() && g_rkeys == 0, "the run ends: let go");
    set_state(2);
    key(HID_R, 0); frame();

    fresh();
    set_dialogs(1);
    key(HID_R, 1); frames(5);
    CHECK(g_rkeys == 0, "R with a dialog up: nothing");
    key(HID_R, 0); frame();
}

static void taken_keys(void) {
    fresh();
    g_card_open = 1;
    key(HID_R, 1); frames(30);
    CHECK(g_rkeys == 0 && !combo(), "R typed into the spawner's card: nothing");
    key(HID_R, 0); frame();

    fresh();
    key(HID_R, 1); frames(5);
    g_card_open = 1;                         /* the card opens over a held R, and keeps its keys */
    key(HID_R, 0); frames(2);
    CHECK(!combo() && g_rkeys == 0, "R's release reaches the holder even with the card up");

    fresh();
    key(HID_R, 1); frames(5);
    key(HID_R, 0); frames(2);                /* MainActivity.releaseKeys on focus loss / leaving: a release */
    CHECK(!combo(), "a release handed by the app is a release");
}

static void bindings(void) {
    fresh();
    key(HID_F, 1); frames(30);
    CHECK(g_rkeys == 0 && !combo(), "F is not bound to restart: nothing");
    key(HID_F, 0); frame();

    fresh();
    g_bound[HID_R] = 0; g_bound[HID_T] = 1;  /* restart moved to T (inputconfigs.dat) */
    key(HID_R, 1); frames(5);
    CHECK(g_rkeys == 0, "R no longer bound: nothing");
    key(HID_T, 1); frames(60);
    CHECK(g_resets == 1, "T is the restart key now (resets %d)", g_resets);
    key(HID_T, 0); key(HID_R, 0); frame();

    fresh();
    key(HID_R, 1); frames(5);
    g_bound[HID_R] = 0;                      /* the binding changes under the held key */
    frames(3);
    CHECK(combo(), "no key went by yet: nothing asks per frame");
    key(HID_W, 1); frames(2);
    CHECK(!combo() && g_rkeys == 0, "the next key event drops a key that is no longer bound");
    key(HID_W, 0); key(HID_R, 0); frame();

    fresh();
    g_bound[HID_T] = 1;                      /* two keys bound to restart */
    key(HID_R, 1); frames(3);
    key(HID_T, 1); frames(3);
    key(HID_R, 0); frames(3);
    CHECK(combo() && g_ups == 0, "one of two restart keys let go: still held");
    key(HID_T, 0); frames(2);
    CHECK(!combo() && g_ups == 1, "the last let go: released once");
}

static void refused(void) {
    fresh();
    g_refuse = 1;                            /* Eden without a token, a daily run */
    key(HID_R, 1); frames(80);
    CHECK(g_fades == 0 && g_resets == 0, "refused by the game: no fade, as on a pad");
    key(HID_R, 0); frames(2);
    CHECK(!combo(), "and the hold goes with the key");
}

#ifdef DESK_REAL_TABLE
#include "tables_cases.c"
#endif

int main(void) {
    g_log = getenv("DESK_LOG") != NULL;
    g_img = calloc(1, IMG_SIZE);
    g_game = calloc(1, 0x260000);
    g_mgr = calloc(1, 0x210000);
    if (!g_img || !g_game || !g_mgr) { printf("no memory\n"); return 2; }
    *(unsigned long long *) (g_img + GAME_GAME) = (unsigned long long) g_game;
    *(unsigned long long *) (g_img + MANAGER_PTR) = (unsigned long long) g_mgr;
#ifdef DESK_REAL_TABLE
    tables_cases();
    printf("reset desk, the real table: %d cases, %d failures\n", g_cases, g_fail);
    return g_fail ? 1 : 0;
#endif
    r_alone_to_the_end();
    r_let_go_early();
    r_let_go_in_the_fade();
    finger_alone();
    both_r_first_let_go();
    both_finger_first_let_go();
    r_over_a_spent_finger();
    both_early_release();
    gating();
    taken_keys();
    bindings();
    refused();
    printf("reset desk: %d cases, %d failures\n", g_cases, g_fail);
    return g_fail ? 1 : 0;
}
