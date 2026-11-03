/*
 * Desk test for hotfix 1.72: the Bag of Crafting's swipe (bagswipe.h) and the
 * inventory on a held map going with EID's switch (items.c); a long
 * description's pages under a finger; the sticks' clicks on the recipes
 * (bagswipe.h, stickclick_step and bagswipe_round, as eid_craft.c uses them).
 *
 * items.c is compiled as it is, with the rest of the port stubbed out below:
 * a 480 x 270 canvas, four pixels a unit, the map in its corner and the
 * panel at (70, 45). The clock is turned by hand.
 *
 *   py -3 tools/test_bagswipe/run.py
 */
#include <stdio.h>
#include <string.h>
#include <time.h>
#include "../../port/app/src/main/cpp/bagswipe.h"
#include "../../port/app/src/main/cpp/devices.h"

/* ------------------------------------------------------------ the stubs */
static long g_now_ms = 100000;
int fake_clock_gettime(int clock, struct timespec *ts) {
    (void) clock;
    ts->tv_sec = g_now_ms / 1000;
    ts->tv_nsec = (g_now_ms % 1000) * 1000000L;
    return 0;
}

#define PX 4.0f                         /* pixels a canvas unit */
static int g_eid = 1, g_craft_built = 0, g_desc_pages = 0, g_rings = 0;
static int g_raw, g_turn_next, g_turn_prev, g_lock, g_desc_next, g_desc_prev;

unsigned long shim_image_base(void) { return 0; }
int shim_sticks_in_game(void) { return 1; }
float shim_screen_width(void) { return 480.0f; }
float shim_screen_height(void) { return 270.0f; }
void shim_touch_push_raw(int id, int phase, float x, float y) { (void) id; (void) phase; (void) x; (void) y; g_raw++; }
int shim_hud_map_box(float *cx, float *cy, float *w, float *h) { *cx = 424.5f; *cy = 41.0f; *w = 42.0f; *h = 44.0f; return 1; }
int shim_trace_readable(const void *p, unsigned long n) { (void) p; (void) n; return 0; }
void shim_eid_marker(const char *n, float x, float y) { (void) n; (void) x; (void) y; }
static char g_marker_last[32];
void shim_eid_marker_at(const char *n, float x, float y, float s) {
    (void) x; (void) y; (void) s; snprintf(g_marker_last, sizeof(g_marker_last), "%s", n ? n : "");
}
static int g_writes;
void shim_eid_write(const char *t, float x, float y, float r, float g, float b, float a) {
    (void) t; (void) x; (void) y; (void) r; (void) g; (void) b; (void) a; g_writes++;
}
float shim_eid_line_height(void) { return 11.0f; }
void shim_eid_pin(unsigned kind, unsigned id) { (void) kind; (void) id; }
int shim_eid_pill(int c, unsigned *k, unsigned *i) { (void) c; (void) k; (void) i; return 0; }
const char *shim_eid_name(unsigned k, unsigned i) { (void) k; (void) i; return ""; }
const char *shim_eid_text(unsigned k, unsigned i) { (void) k; (void) i; return ""; }
float shim_eid_show_item(const char *p, const char *n, const char *t, float x, float y) {
    (void) p; (void) n; (void) t; (void) x; (void) y; return 11.0f;
}
int shim_eid_form(int f, const char **i, const char **n, int *h, int *nd) { (void) f; (void) i; (void) n; (void) h; (void) nd; return 0; }
int shim_report_active(void) { return 0; }
void report_line(const char *k, const char *t) { (void) k; (void) t; }
float shim_eid_measure_item(const char *p, const char *n, const char *t) { (void) p; (void) n; (void) t; return 11.0f; }
int shim_eid_hud_showing(void) { return 1; }
void shim_eid_panel_place(float *x, float *y) { *x = 70.0f; *y = 45.0f; }
float shim_eid_panel_width(void) { return 160.0f; }
float shim_eid_panel_height(void) { return 150.0f; }
int shim_screen_finger_units(float x, float y, float *u, float *v) { *u = x / PX; *v = y / PX; return 1; }
int shim_device_kind(int id) { (void) id; return 0; }
/* as eid.c and eid_craft.c have them: the switch, and the screen only while EID builds it */
int shim_eid_on(void) { return g_eid; }
int shim_eid_craft_showing(void) { return shim_eid_on() && g_craft_built; }
void shim_eid_craft_touch_turn(int d) { if (d > 0) g_turn_next++; else g_turn_prev++; }
void shim_eid_craft_touch_lock(void) { g_lock++; }
/* as eid.c has them (PAGES): a long description up, and the turns its zone asks for */
int shim_eid_pages_showing(void) { return shim_eid_on() && g_desc_pages; }
void shim_eid_pages_touch_turn(int d) { if (d > 0) g_desc_next++; else if (d < 0) g_desc_prev++; }
/*
 * eid.c's co-op answers (1.74): the run's players of their own, in order, each
 * on a device of a kind, with the actions he holds as bits; whose panel is up;
 * whose list items.c says is up.
 */
#define ACT(a) (1u << (a))
static unsigned long g_pl[3] = { 0x1000, 0x2000, 0x3000 };
static int g_np = 1;
static int g_pl_kind[3] = { DEVICE_SCREEN, DEVICE_PAD, DEVICE_KEYBOARD };
static unsigned g_pl_held[3];
static unsigned long g_panel_of = 0x1000, g_listed;
int shim_eid_main_players(unsigned long *out, int max) {
    int n = g_np < max ? g_np : max;
    for (int i = 0; i < n; i++) out[i] = g_pl[i];
    return n;
}
static int pl_index(unsigned long p) { for (int i = 0; i < g_np; i++) if (g_pl[i] == p) return i; return -1; }
int shim_eid_player_held(unsigned long p, int a) { int i = pl_index(p); return i >= 0 && ((g_pl_held[i] >> a) & 1); }
int shim_eid_player_device_kind(unsigned long p) { int i = pl_index(p); return i >= 0 ? g_pl_kind[i] : DEVICE_NONE; }
unsigned long shim_eid_panel_player(void) { return g_panel_of; }
static int g_touchable = 1;                 /* eid.c: the panel on screen is player 0's */
int shim_eid_panel_touchable(void) { return g_touchable; }
/* the seats: player i on device i + 1; ESAU, not a player of his own, on the first one's */
#define ESAU 0x4000UL
int shim_eid_player_seat(unsigned long p) { if (p == ESAU) return 1; int i = pl_index(p); return i >= 0 ? i + 1 : -1; }
void shim_eid_list_player(unsigned long p) { g_listed = p; }
int shim_eid_player_type(unsigned long p) { return p == g_pl[1] ? 3 : p == g_pl[2] ? 5 : 0; }
/* sticks.c: a fixed left stick whose ring reaches into the panel's upper half (60..110 x 80..130) */
int shim_sticks_rings(float *o) {
    if (!g_rings) return 0;
    float r[8] = { 60, 80, 110, 130, 380, 150, 430, 200 };
    for (int i = 0; i < 8; i++) o[i] = r[i];
    return 2;
}

#define ISAAC_DIAG 0
#include "../../port/app/src/main/cpp/items.c"

/* ------------------------------------------------------------- the checks */
static int g_fail, g_pass;
#define CHECK(c, ...) do { if (c) g_pass++; else { g_fail++; printf("FAIL %s:%d ", __FILE__, __LINE__); \
                           printf(__VA_ARGS__); printf("\n"); } } while (0)

static void reset_counts(void) { g_raw = g_turn_next = g_turn_prev = g_lock = g_desc_next = g_desc_prev = 0; }

/* a finger in canvas units; returns how many of its events the layer kept from the game */
static int touch(int id, int phase, float ux, float uy) { return shim_items_touch(id, phase, ux * PX, uy * PX); }

/* down at a, moves in `steps` to b over `ms`, up there (phase 3) or cancelled (4) */
static int stroke(int id, float ax, float ay, float bx, float by, int steps, long ms, int end_phase) {
    int kept = touch(id, 0, ax, ay);
    for (int i = 1; i <= steps; i++) {
        g_now_ms += ms / (steps ? steps : 1);
        kept += touch(id, 1, ax + (bx - ax) * i / steps, ay + (by - ay) * i / steps);
    }
    if (!steps) g_now_ms += ms;
    kept += touch(id, end_phase, bx, by);
    return kept;
}

static void test_gesture_pure(void) {
    bagswipe_t s;
    /* a tap */
    bagswipe_down(&s, 100, 100, 0);
    CHECK(bagswipe_up(&s, 101, 100, 120) == SWIPE_NONE, "tap does nothing");
    /* a hold, still */
    bagswipe_down(&s, 100, 100, 0);
    CHECK(bagswipe_up(&s, 100, 100, 600) == SWIPE_LOCK, "hold locks");
    /* a hold with a drift short of a swipe */
    bagswipe_down(&s, 100, 100, 0);
    CHECK(bagswipe_move(&s, 115, 104) == SWIPE_NONE, "15 units is no swipe");
    CHECK(bagswipe_up(&s, 115, 104, 700) == SWIPE_LOCK, "a drifting hold still locks");
    /* swipe RIGHT: next, the moment it crosses (the owner, 07.10.2026) */
    bagswipe_down(&s, 100, 100, 0);
    CHECK(bagswipe_move(&s, 110, 101) == SWIPE_NONE, "10 right: not yet");
    CHECK(bagswipe_move(&s, 124, 103) == SWIPE_NEXT, "24 right: next page");
    CHECK(bagswipe_move(&s, 160, 103) == SWIPE_NONE, "one page a touch");
    CHECK(bagswipe_up(&s, 180, 103, 900) == SWIPE_NONE, "the release adds nothing, not even a lock");
    /* swipe LEFT: previous */
    bagswipe_down(&s, 100, 100, 0);
    CHECK(bagswipe_move(&s, 70, 95) == SWIPE_PREVIOUS, "30 left: previous page");
    /* a fast swipe whose only sample past the threshold is the release */
    bagswipe_down(&s, 100, 100, 0);
    CHECK(bagswipe_up(&s, 140, 98, 80) == SWIPE_NEXT, "decided on release");
    /* vertical drag: nothing, and it stays nothing */
    bagswipe_down(&s, 100, 100, 0);
    CHECK(bagswipe_move(&s, 105, 130) == SWIPE_NONE, "30 down: nothing");
    CHECK(bagswipe_move(&s, 160, 130) == SWIPE_NONE, "then sideways: still nothing");
    CHECK(bagswipe_up(&s, 160, 130, 900) == SWIPE_NONE, "and no lock either");
    /* a diagonal: not mostly sideways */
    bagswipe_down(&s, 100, 100, 0);
    CHECK(bagswipe_move(&s, 70, 70) == SWIPE_NONE, "45 degrees: nothing");
    /* a slow drag: distance decides, not speed */
    bagswipe_down(&s, 100, 100, 0);
    for (int i = 1; i <= 30; i++) {
        int r = bagswipe_move(&s, 100.0f - i, 100);
        if (i < 24) CHECK(r == SWIPE_NONE, "slow drag at %d", i);
        else if (i == 24) CHECK(r == SWIPE_PREVIOUS, "a slow drag left turns back at 24");
        else CHECK(r == SWIPE_NONE, "and only once");
    }
}

static void test_pages(void) {
    bagpages_t p = { { 0 }, 0 };
    /* 20 recipes; the pages fit 7, then 5, then 8 */
    int o = 0;
    o = bagswipe_turn(&p, o, 7, 20, -1); CHECK(o == 0, "back on the first page stays (%d)", o);
    o = bagswipe_turn(&p, o, 7, 20, 1);  CHECK(o == 7, "next -> 7 (%d)", o);
    o = bagswipe_turn(&p, o, 5, 20, 1);  CHECK(o == 12, "next -> 12 (%d)", o);
    o = bagswipe_turn(&p, o, 8, 20, 1);  CHECK(o == 12, "the last page stays (%d)", o);
    o = bagswipe_turn(&p, o, 8, 20, -1); CHECK(o == 7, "back -> 7, not 12 - 8 = 4 (%d)", o);
    o = bagswipe_turn(&p, o, 5, 20, -1); CHECK(o == 0, "back -> 0 (%d)", o);
    o = bagswipe_turn(&p, o, 7, 20, -1); CHECK(o == 0, "and stays (%d)", o);
    /* the keys moved the list behind the history's back */
    o = bagswipe_turn(&p, 0, 7, 40, 1);  CHECK(o == 7, "next -> 7 (%d)", o);
    o = bagswipe_turn(&p, o, 7, 40, 1);  CHECK(o == 14, "next -> 14 (%d)", o);
    o = 9;                                               /* a key stepped back 5 */
    o = bagswipe_turn(&p, o, 7, 40, -1); CHECK(o == 7, "a stale start is dropped (%d)", o);
    p.n = 0;
    o = bagswipe_turn(&p, 20, 6, 40, -1); CHECK(o == 14, "no history: this page's length back (%d)", o);
    o = bagswipe_turn(&p, 3, 6, 40, -1);  CHECK(o == 0, "never below 0 (%d)", o);
    o = bagswipe_turn(&p, 0, 0, 1, 1);    CHECK(o == 0, "one recipe: nowhere to go (%d)", o);
}

/* the sticks' clicks on the recipes (eid_craft.c, 07.10.2026): a tap a page, round; L3 held 1 s the lock */
static void test_clicks(void) {
    const long long MS = 1000000LL, LOCK = 1000 * MS, MORE = 500 * MS;
    stickclick_t c = { 0, 0, 0 };
    long long t = 0;
    /* a tap: nothing on the press, a tap on the release */
    CHECK(stickclick_step(&c, 1, t, LOCK) == CLICK_NONE, "L3 down: nothing yet");
    t += 200 * MS;
    CHECK(stickclick_step(&c, 1, t, LOCK) == CLICK_NONE, "held 0.2 s: nothing");
    CHECK(stickclick_step(&c, 0, t, LOCK) == CLICK_TAP, "let go before 1 s: a tap (a page back)");
    CHECK(stickclick_step(&c, 0, t + MS, LOCK) == CLICK_NONE, "up stays nothing");
    /* a hold: once, at 1 s, and the release after it is nothing */
    t = 10000 * MS;
    stickclick_step(&c, 1, t, LOCK);
    CHECK(stickclick_step(&c, 1, t + 999 * MS, LOCK) == CLICK_NONE, "0.999 s: not yet");
    CHECK(stickclick_step(&c, 1, t + 1000 * MS, LOCK) == CLICK_HOLD, "1 s: the lock");
    CHECK(stickclick_step(&c, 1, t + 3000 * MS, LOCK) == CLICK_NONE, "held on: once only");
    CHECK(stickclick_step(&c, 0, t + 3100 * MS, LOCK) == CLICK_NONE, "the release after the lock turns no page");
    /* R3 with its half second, as before */
    stickclick_t r = { 0, 0, 0 };
    t = 20000 * MS;
    stickclick_step(&r, 1, t, MORE);
    CHECK(stickclick_step(&r, 1, t + 499 * MS, MORE) == CLICK_NONE, "R3 0.499 s: nothing");
    CHECK(stickclick_step(&r, 1, t + 500 * MS, MORE) == CLICK_HOLD, "R3 0.5 s: more recipes");
    CHECK(stickclick_step(&r, 0, t + 600 * MS, MORE) == CLICK_NONE, "and no page on its release");
    stickclick_step(&r, 1, t + 1000 * MS, MORE);
    CHECK(stickclick_step(&r, 0, t + 1100 * MS, MORE) == CLICK_TAP, "R3 tap: a page");

    /* the pages round the ends - the clicks and, since 07.10.2026, the swipes: 20 recipes, pages of 7 */
    bagpages_t p = { { 0 }, 0 };
    int o = 0;
    o = bagswipe_round(&p, o, 7, 20, 1);  CHECK(o == 7, "R3: 7 (%d)", o);
    o = bagswipe_round(&p, o, 7, 20, 1);  CHECK(o == 14, "R3: 14 (%d)", o);
    o = bagswipe_round(&p, o, 6, 20, 1);  CHECK(o == 0 && p.n == 0, "R3 on the last: round to 0, history anew (%d)", o);
    o = bagswipe_round(&p, o, 7, 20, -1); CHECK(o == 13, "L3 on the first: the last 7, from 13 (%d)", o);
    o = bagswipe_round(&p, o, 7, 20, -1); CHECK(o == 6, "L3: back a page (%d)", o);
    o = bagswipe_round(&p, 7, 7, 20, -1); p.n = 0;
    o = bagswipe_round(&p, 0, 7, 20, 1);  o = bagswipe_round(&p, o, 5, 20, 1);
    CHECK(o == 12, "R3 twice: 12 (%d)", o);
    o = bagswipe_round(&p, o, 8, 20, -1); CHECK(o == 7, "L3 walks the history back: 7 (%d)", o);
    o = bagswipe_round(&p, 0, 7, 5, -1);  CHECK(o == 0, "one page only: L3 stays at 0 (%d)", o);
    o = bagswipe_round(&p, 0, 7, 5, 1);   CHECK(o == 0, "and R3 too (%d)", o);
}

static void frames(long ms) {
    for (long t = 0; t < ms; t += 16) { g_now_ms += 16; shim_items_between_frames(); }
}

static void test_bag_zone(void) {
    g_eid = 1;
    g_craft_built = 1;
    /* inside the panel's upper half (70..230 x 45..120 of 45..195): a swipe right */
    reset_counts();
    int kept = stroke(1, 110, 90, 150, 92, 6, 150, 3);
    CHECK(kept == 8, "every event of a finger on the screen is the zone's (%d of 8)", kept);
    CHECK(g_turn_next == 1 && !g_turn_prev && !g_lock, "swipe right: one next page (%d %d %d)", g_turn_next, g_turn_prev, g_lock);
    CHECK(g_raw == 0, "nothing reached the game (%d)", g_raw);
    /* a swipe left */
    reset_counts();
    stroke(1, 160, 90, 110, 88, 5, 150, 3);
    CHECK(g_turn_prev == 1 && !g_turn_next, "swipe left: one previous page");
    /* a tap */
    reset_counts();
    kept = stroke(1, 150, 90, 151, 90, 1, 90, 3);
    CHECK(kept == 3 && !g_turn_next && !g_turn_prev && !g_lock && !g_raw, "a tap: taken, and does nothing");
    /* a hold */
    reset_counts();
    stroke(1, 150, 90, 150, 91, 1, 700, 3);
    CHECK(g_lock == 1 && !g_turn_next, "a hold: the lock");
    /* a vertical drag from the upper half */
    reset_counts();
    stroke(1, 150, 60, 150, 180, 8, 300, 3);
    CHECK(!g_turn_next && !g_turn_prev && !g_lock && !g_raw, "a vertical drag: nothing, and not the game's");
    /* a thumb that lands on the upper half and pushes like a stick: the zone's, whole */
    reset_counts();
    kept = stroke(2, 100, 100, 160, 60, 10, 400, 3);
    CHECK(kept == 12 && g_raw == 0, "a stick drag starting on the upper half never reaches the game (%d, raw %d)", kept, g_raw);
    /* THE LOWER HALF is never the zone's (the owner, 07.10.2026): a swipe there is the game's, whole */
    reset_counts();
    kept = stroke(2, 150, 160, 110, 162, 6, 150, 3);
    CHECK(kept == 0 && !g_turn_next && !g_turn_prev, "a swipe on the lower half: the game's (%d kept)", kept);
    CHECK(touch(2, 0, 150, 121) == 0, "just under the half (120): the game's");
    touch(2, 3, 150, 121);
    CHECK(touch(2, 0, 150, 119) == 1, "just over it: the zone's");
    touch(2, 3, 150, 119);
    /* a cancelled swipe-to-be: no lock, no page beyond what was crossed */
    reset_counts();
    stroke(3, 150, 90, 150, 90, 0, 800, 4);
    CHECK(!g_lock && !g_turn_next, "a cancel locks nothing");
    /* a finger going down outside the screen is the game's */
    reset_counts();
    CHECK(touch(4, 0, 40, 220) == 0, "outside the screen: the game's");
    CHECK(touch(4, 1, 120, 120) == 0, "and stays the game's when it slides over the screen");
    CHECK(touch(4, 3, 120, 120) == 0, "to the end");
    /* while a finger is on the screen, a second finger elsewhere is the game's */
    reset_counts();
    touch(5, 0, 150, 90);
    CHECK(touch(6, 0, 400, 220) == 0, "another finger elsewhere: the game's");
    touch(6, 3, 400, 220);
    touch(5, 3, 150, 90);
    /* a lost release: the next press under the same id is judged afresh */
    reset_counts();
    touch(7, 0, 150, 90);                              /* never released */
    CHECK(touch(7, 0, 40, 220) == 0, "a new press under a lost id, outside: the game's");
    touch(7, 3, 40, 220);
    touch(8, 0, 150, 90);                              /* lost again, and the app went away */
    shim_items_forget();
    CHECK(touch(9, 0, 150, 90) == 1, "after a forget the screen takes a new finger");
    touch(9, 3, 150, 90);
    /* the screen not up: nothing taken */
    g_craft_built = 0;
    CHECK(touch(10, 0, 150, 90) == 0, "no recipes on screen: the game's");
    touch(10, 3, 150, 90);
    /* EID off: the screen is not built, and its zone takes nothing */
    g_craft_built = 1;
    g_eid = 0;
    CHECK(touch(11, 0, 150, 90) == 0, "EID off: the zone takes nothing");
    touch(11, 3, 150, 90);
    g_eid = 1;
}

static void test_map_list(void) {
    g_craft_built = 0;
    /* EID on: a held map opens the list, the finger stays the game's, release closes */
    g_eid = 1;
    CHECK(touch(20, 0, 424, 41) == 0, "the map finger is the game's");
    frames(1300);
    CHECK(shim_items_open() == 1, "EID on: the list is up after 1.2 s");
    g_writes = 0;
    shim_items_draw();
    CHECK(g_writes > 0, "and drawn");
    CHECK(touch(20, 3, 424, 41) == 0, "release: the game's");
    CHECK(shim_items_open() == 0, "and the list goes with it");
    /* EID off: nothing */
    g_eid = 0;
    CHECK(touch(21, 0, 424, 41) == 0, "EID off: the map finger is the game's");
    frames(2000);
    CHECK(shim_items_open() == 0, "EID off: no list on a held map");
    g_writes = 0;
    shim_items_draw();
    CHECK(g_writes == 0, "EID off: nothing drawn");
    touch(21, 3, 424, 41);
    /* switched off while the list is up (the switch is live) */
    g_eid = 1;
    touch(22, 0, 424, 41);
    frames(1300);
    CHECK(shim_items_open() == 1, "up again");
    g_eid = 0;
    g_writes = 0;
    shim_items_draw();
    CHECK(g_writes == 0, "EID off: not drawn on the very next frame");
    frames(16);
    CHECK(shim_items_open() == 0, "EID off: put away between frames");
    touch(22, 3, 424, 41);
    g_eid = 1;
}

/*
 * A LONG DESCRIPTION'S PAGES (hotfix 1.72): the same swipe on the same panel,
 * a zone only while the description has pages and the bag's recipes are not
 * up, never on a fixed stick's ring, a tap and a hold doing nothing.
 */
static void test_desc_zone(void) {
    g_eid = 1;
    g_craft_built = 0;
    g_desc_pages = 1;
    /* a swipe right on the panel's upper half (70..230 x 45..120): the next page, the finger the zone's */
    reset_counts();
    int kept = stroke(1, 110, 90, 150, 92, 6, 150, 3);
    CHECK(kept == 8 && g_desc_next == 1 && !g_desc_prev && !g_raw, "swipe right: next page, all 8 kept (%d, %d, raw %d)", kept, g_desc_next, g_raw);
    CHECK(!g_turn_next && !g_turn_prev && !g_lock, "and the bag heard nothing");
    reset_counts();
    stroke(1, 160, 90, 110, 88, 5, 150, 3);
    CHECK(g_desc_prev == 1 && !g_desc_next, "swipe left: previous page");
    /* a tap and a hold: taken, nothing happens - a description has nothing to lock */
    reset_counts();
    kept = stroke(1, 150, 90, 151, 90, 1, 90, 3);
    CHECK(kept == 3 && !g_desc_next && !g_desc_prev && !g_raw, "a tap: taken, does nothing");
    reset_counts();
    stroke(1, 150, 90, 150, 91, 1, 700, 3);
    CHECK(!g_desc_next && !g_desc_prev && !g_lock && !g_raw, "a hold: nothing (%d %d %d)", g_desc_next, g_desc_prev, g_lock);
    /* a vertical drag */
    reset_counts();
    stroke(1, 150, 60, 150, 180, 8, 300, 3);
    CHECK(!g_desc_next && !g_desc_prev && !g_raw, "a vertical drag: nothing");
    /* down outside the panel: the game's, also when it slides over the panel */
    reset_counts();
    CHECK(touch(2, 0, 40, 220) == 0, "outside the panel: the game's");
    CHECK(touch(2, 1, 150, 90) == 0, "sliding over it: still the game's");
    CHECK(touch(2, 3, 150, 90) == 0 && !g_desc_next, "to the end, and no page");
    /* a second finger elsewhere while one swipes */
    touch(3, 0, 150, 90);
    CHECK(touch(4, 0, 400, 220) == 0, "another finger elsewhere: the game's");
    touch(4, 3, 400, 220);
    touch(3, 3, 150, 90);
    /* the lower half: never the zone's, a stick's thumb lands there (dynamic and floating too) */
    reset_counts();
    kept = stroke(5, 150, 160, 110, 162, 6, 150, 3);
    CHECK(kept == 0 && !g_desc_next && !g_desc_prev, "a swipe on the lower half: the game's (%d kept)", kept);
    /* a fixed stick's ring reaching into the upper half: a thumb there is the stick's */
    g_rings = 1;
    reset_counts();
    CHECK(touch(5, 0, 90, 100) == 0, "on the stick's ring inside the upper half: the game's");
    touch(5, 1, 60, 100);
    CHECK(touch(5, 3, 60, 100) == 0 && !g_desc_next && !g_desc_prev, "and it turns nothing");
    CHECK(touch(6, 0, 150, 90) == 1, "the panel off the ring is still the zone's");
    touch(6, 3, 150, 90);
    g_rings = 0;
    /* one page: no zone */
    g_desc_pages = 0;
    CHECK(touch(7, 0, 150, 90) == 0, "a description that fits: the game's");
    touch(7, 3, 150, 90);
    /* the bag's recipes up: they own the spot, whatever the description says */
    g_desc_pages = 1;
    g_craft_built = 1;
    reset_counts();
    stroke(8, 110, 90, 150, 92, 6, 150, 3);
    CHECK(g_turn_next == 1 && !g_desc_next, "recipes up: the bag turns, not the description (%d %d)", g_turn_next, g_desc_next);
    g_craft_built = 0;
    /* a lost release, and a forget */
    reset_counts();
    touch(9, 0, 150, 90);                              /* never released */
    CHECK(touch(9, 0, 40, 220) == 0, "a new press under a lost id, outside: the game's");
    touch(9, 3, 40, 220);
    touch(10, 0, 150, 90);
    shim_items_forget();
    CHECK(touch(11, 0, 150, 90) == 1, "after a forget the panel takes a new finger");
    touch(11, 3, 150, 90);
    /* EID off */
    g_eid = 0;
    CHECK(touch(12, 0, 150, 90) == 0, "EID off: the zone takes nothing");
    touch(12, 3, 150, 90);
    g_eid = 1;
    /* the list up on a held map: a finger on the panel is not the description's */
    CHECK(touch(13, 0, 424, 41) == 0, "the map finger");
    frames(1300);
    reset_counts();
    stroke(14, 110, 90, 150, 92, 6, 150, 3);
    CHECK(!g_desc_next, "the list up: no page turns under it");
    touch(13, 3, 424, 41);
    g_desc_pages = 0;
}

/*
 * CO-OP (1.74): every player holds the map on his own device and gets his own
 * list (the mod's Item Reminder: holdTabPlayer, main.lua 1358-1368); the one
 * holding first keeps it; a long description's or the bag's hold with a shot is
 * only the panel's own player's; with more than one player the list carries his head.
 */
static void test_map_coop(void) {
    g_eid = 1; g_craft_built = 0; g_desc_pages = 0;
    memset(g_pl_held, 0, sizeof(g_pl_held));
    /* single player on the keyboard: his list, no head */
    g_np = 1; g_pl_kind[0] = DEVICE_KEYBOARD;
    g_pl_held[0] = ACT(ACTION_MAP);
    frames(1300);
    CHECK(shim_items_open() && g_listed == g_pl[0], "one player: Map held, his list");
    g_marker_last[0] = 0; shim_items_draw();
    CHECK(g_head == -1 && g_marker_last[0] == 0, "one player: no head over the list (%d)", g_head);
    g_pl_held[0] = 0; frames(32);
    CHECK(!shim_items_open() && g_listed == 0, "let go: no list, and eid.c told");
    /* two players: the second (a pad) holds Select - his list, his head */
    g_np = 2; g_pl_kind[0] = DEVICE_SCREEN; g_pl_kind[1] = DEVICE_PAD;
    g_pl_held[1] = ACT(ACTION_MAP);
    frames(600);
    CHECK(!shim_items_open(), "the second player's hold, 0.6 s: not yet");
    frames(700);
    CHECK(shim_items_open() && g_listed == g_pl[1], "the second player's hold: HIS list");
    g_marker_last[0] = 0; shim_items_draw();
    CHECK(g_head == 3 && !strcmp(g_marker_last, "Player3"), "two players: his head over it (%d, %s)", g_head, g_marker_last);
    /* the first one presses his map too: the list stays the second one's */
    g_pl_held[0] = ACT(ACTION_MAP);
    frames(200);
    CHECK(shim_items_open() && g_listed == g_pl[1], "the first joins the hold: still the second one's");
    /* the second lets go, the first still holds: the second's list goes, the first's hold begins */
    g_pl_held[1] = 0;
    frames(32);
    CHECK(!shim_items_open(), "the second lets go: his list goes");
    frames(1300);
    CHECK(shim_items_open() && g_listed == g_pl[0], "the first, held on: his own list after his own 1.2 s");
    g_pl_held[0] = 0; frames(32);
    CHECK(!shim_items_open(), "all let go");
    /* the first player's map only: the second one's keys do nothing */
    g_pl_held[1] = ACT(ACTION_SHOOT_DOWN);
    frames(1500);
    CHECK(!shim_items_open(), "the second only shoots: no list");
    g_pl_held[1] = 0;
    /* a long description, the second player's (keyboard): his Map + shot pages it, no list */
    g_np = 2; g_pl_kind[1] = DEVICE_KEYBOARD;
    g_desc_pages = 1; g_panel_of = g_pl[1];
    g_pl_held[1] = ACT(ACTION_MAP);
    frames(200);
    g_pl_held[1] = ACT(ACTION_MAP) | ACT(ACTION_SHOOT_DOWN);
    frames(200);
    g_pl_held[1] = ACT(ACTION_MAP);
    frames(1500);
    CHECK(!shim_items_open(), "his own pages: Map + shot is theirs, no list");
    g_pl_held[1] = 0; frames(32);
    /* the same hold while the description is the FIRST player's: not his pages - his list */
    g_panel_of = g_pl[0];
    g_pl_held[1] = ACT(ACTION_MAP);
    frames(200);
    g_pl_held[1] = ACT(ACTION_MAP) | ACT(ACTION_SHOOT_DOWN);
    frames(200);
    g_pl_held[1] = ACT(ACTION_MAP);
    frames(1000);
    CHECK(shim_items_open() && g_listed == g_pl[1], "another player's pages: his hold opens his list");
    g_pl_held[1] = 0; frames(32);
    /* the Tab trap (the owner, 11.10.2026): his own pages up, Tab held long with NO arrow - still no list */
    g_panel_of = g_pl[1]; g_desc_pages = 1;
    g_pl_held[1] = ACT(ACTION_MAP);
    frames(2500);
    CHECK(!shim_items_open(), "his own pages, Tab held 2.5 s without an arrow: no list");
    g_pl_held[1] = ACT(ACTION_MAP) | ACT(ACTION_SHOOT_DOWN);
    frames(100);
    CHECK(!shim_items_open(), "and the arrow then pages, no list");
    /* he walks away from the thing, still holding: the same hold opens his list (the mod: per frame) */
    g_pl_held[1] = ACT(ACTION_MAP);
    g_desc_pages = 0;
    frames(32);
    CHECK(shim_items_open() && g_listed == g_pl[1], "walked away, still holding: his list");
    g_pl_held[1] = 0; frames(32);
    /* the bag's recipes the same */
    g_craft_built = 1;
    g_pl_held[1] = ACT(ACTION_MAP);
    frames(2500);
    CHECK(!shim_items_open(), "his bag's recipes, Tab held 2.5 s: no list");
    g_pl_held[1] = 0; frames(32);
    g_craft_built = 0;
    /* a pad's Select is the list's: R3/L3 page without it */
    g_pl_kind[1] = DEVICE_PAD; g_desc_pages = 1;
    g_pl_held[1] = ACT(ACTION_MAP);
    frames(1300);
    CHECK(shim_items_open() && g_listed == g_pl[1], "a pad at his own pages: Select held opens his list");
    g_pl_held[1] = 0; frames(32);
    g_pl_kind[1] = DEVICE_KEYBOARD; g_desc_pages = 0;
    /* Jacob and Esau, one player of their own: the bag (or the pages) Esau's, the hold Jacob's - one device */
    g_np = 1; g_pl_kind[0] = DEVICE_KEYBOARD; g_panel_of = ESAU; g_desc_pages = 1;
    g_pl_held[0] = ACT(ACTION_MAP);
    frames(200);
    g_pl_held[0] = ACT(ACTION_MAP) | ACT(ACTION_SHOOT_DOWN);
    frames(200);
    g_pl_held[0] = ACT(ACTION_MAP);
    frames(1500);
    CHECK(!shim_items_open(), "Esau's panel, Jacob's keys (one device): the hold is the pages', no list");
    g_pl_held[0] = 0; frames(32);
    g_np = 2; g_pl_kind[0] = DEVICE_SCREEN;
    /* a pad: his clicks page; his Select is the list's even with a shot (as in single player) */
    g_pl_kind[1] = DEVICE_PAD; g_panel_of = g_pl[1];
    g_pl_held[1] = ACT(ACTION_MAP) | ACT(ACTION_SHOOT_DOWN);
    frames(1300);
    CHECK(shim_items_open() && g_listed == g_pl[1], "a pad: Select + shot opens his list");
    g_pl_held[1] = 0; frames(32);
    /* three players: the third (keyboard) holds Tab */
    g_np = 3; g_desc_pages = 0;
    g_pl_held[2] = ACT(ACTION_MAP);
    frames(1300);
    CHECK(shim_items_open() && g_listed == g_pl[2], "three players: the third's list");
    g_marker_last[0] = 0; shim_items_draw();
    CHECK(!strcmp(g_marker_last, "Player5"), "and his head (%s)", g_marker_last);
    g_pl_held[2] = 0; frames(32);
    CHECK(!shim_items_open(), "let go");
    /* EID off: nobody's hold opens anything */
    g_eid = 0;
    g_pl_held[1] = ACT(ACTION_MAP);
    frames(1500);
    CHECK(!shim_items_open(), "EID off: no list for anyone");
    g_pl_held[1] = 0; frames(32);
    g_eid = 1; g_np = 1; g_panel_of = g_pl[0]; g_pl_kind[0] = DEVICE_SCREEN;
    /* a finger pages only player 0's panel (the owner, 11.10.2026): another's is no zone */
    g_touchable = 0;
    g_desc_pages = 1;
    reset_counts();
    int kept = stroke(30, 110, 90, 150, 92, 6, 150, 3);
    CHECK(kept == 0 && !g_desc_next, "another player's long description: the finger is the game's (%d)", kept);
    g_desc_pages = 0; g_craft_built = 1;
    reset_counts();
    kept = stroke(31, 110, 90, 150, 92, 6, 150, 3);
    CHECK(kept == 0 && !g_turn_next, "another player's recipes: the finger is the game's (%d)", kept);
    g_touchable = 1;
    reset_counts();
    kept = stroke(32, 110, 90, 150, 92, 6, 150, 3);
    CHECK(kept == 8 && g_turn_next == 1, "player 0's recipes: the zone's, a page (%d)", kept);
    g_craft_built = 0;
}

int main(void) {
    test_gesture_pure();
    test_pages();
    test_clicks();
    test_bag_zone();
    test_map_list();
    test_desc_zone();
    test_map_coop();
    printf("%d passed, %d failed\n", g_pass, g_fail);
    return g_fail != 0;
}
