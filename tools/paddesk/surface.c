/*
 * surface: pad.c (the real file) on the desk, against the real image's words
 * and tables, with the game's input device and its double-tap detector
 * modelled from the image (0x8a9988, 0x8a79e4, 0x8a7e88, 0x516788).
 *   bash tools/paddesk/surface.sh
 *
 * What it asserts:
 *   1. drop: the word at 0x52ad84 is "bl 0x671c5c" in the image and becomes
 *      "bl 0x671c6c", nothing else written; the drop road (0x52acd8) over
 *      the modelled device, R2 held 130 updates: one swap, and the card still
 *      dropped at the 121st update of the hold (0x52aea0 still asks "held");
 *   2. a held R2 is one edge for the device, however long it is held;
 *   3. the double tap on a pad fires exactly where the game's own window (10)
 *      would fire it on iOS, for the d-pad and for the stick, at the same
 *      update - checked on fixed cases and on random tapping against a model
 *      of the ungated game, with one hook an update, two, or none between
 *      updates run back to back.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

long long g_desk_ms;

/* ---------- the image ---------- */
static unsigned char *g_img;
static long g_img_n;
unsigned long shim_image_base(void) { return (unsigned long) g_img; }

static int g_patches;
int shim_patch_word(unsigned long off, uint32_t was_a, uint32_t was_b, uint32_t now) {
    uint32_t *p = (uint32_t *) (g_img + off);
    if (*p != was_a && *p != was_b) return 0;
    if (*p != now) g_patches++;
    *p = now;
    return 1;
}

/* ---------- the layer around pad.c ---------- */
int shim_report_active(void) { return 1; }
static int g_quiet;
void report_line(const char *kind, const char *text) { if (!g_quiet) printf("    report %s: %s\n", kind, text); }
int shim_coop_join_from_pad(void *controller) { (void) controller; return 0; }
void shim_straps_pad(int v, int h, int a, int back, int any) { (void) v; (void) h; (void) a; (void) back; (void) any; }
void *shim_objc_alloc(void *cls) { (void) cls; return calloc(1, 64); }
void *shim_objc_make_class(const char *name) { (void) name; return (void *) 1; }
void *shim_str_make(const char *text) { (void) text; return (void *) 1; }
static void *g_list[8];
void *shim_arr_make_from(void **items, long count) {
    memset(g_list, 0, sizeof(g_list));
    for (long i = 0; i < count && i < 8; i++) g_list[i] = items[i];
    return g_list;
}
int shim_eid_readable(const void *p, unsigned long n) { (void) n; return p != NULL; }

/* the game's side: one player on the pad's device, the device object itself */
#define DEV_ID 2
static unsigned char g_player[0x3000];
static unsigned char g_dev[0x100];
static int g_codes[0x40][2];
static unsigned long g_bind_table[0x40];
static unsigned char g_player_kb[0x3000];   /* a second player, on the keyboard (device 0) */
static int g_kb_player_on;
int shim_eid_players(unsigned long *out, int max) {
    int n = 0;
    if (n < max) out[n++] = (unsigned long) g_player;
    if (g_kb_player_on && n < max) out[n++] = (unsigned long) g_player_kb;
    return n;
}
int shim_device_of_pad_slot(int slot) { return slot == 0 ? DEV_ID : -1; }
unsigned long shim_device_object(int id) { return id == DEV_ID ? (unsigned long) g_dev : 0; }

/* pad.c's own */
void shim_pad_state(int slot, int connected, unsigned buttons, const float *axes, int axis_count);
void shim_pad_between_frames(void);
void *shim_pad_controllers(void *cls, const char *sel);
void *shim_pad_controller_object(int slot);
void *shim_pad_gamepad(void *s, const char *l);
void *shim_pad_dpad(void *s, const char *l);
void *shim_pad_up(void *s, const char *l);
void *shim_pad_down(void *s, const char *l);
void *shim_pad_left(void *s, const char *l);
void *shim_pad_right(void *s, const char *l);
void *shim_pad_buttonA(void *s, const char *l);
void *shim_pad_buttonB(void *s, const char *l);
void *shim_pad_buttonX(void *s, const char *l);
void *shim_pad_buttonY(void *s, const char *l);
void *shim_pad_L1(void *s, const char *l);
void *shim_pad_R1(void *s, const char *l);
void *shim_pad_L2(void *s, const char *l);
void *shim_pad_R2(void *s, const char *l);
void *shim_pad_menu(void *s, const char *l);
void *shim_pad_options(void *s, const char *l);
void *shim_pad_left_stick(void *s, const char *l);
void *shim_pad_right_stick(void *s, const char *l);
void *shim_pad_x(void *s, const char *l);
void *shim_pad_y(void *s, const char *l);
long shim_pad_is_pressed(void *self, const char *sel);
float shim_pad_axis_value(void *self, const char *sel);

static int checks, fails;
static void check(int ok, const char *what) {
    checks++;
    if (!ok) { fails++; printf("  FAIL %s\n", what); }
}

static uint32_t rd32(unsigned long off) { uint32_t v; memcpy(&v, g_img + off, 4); return v; }
static unsigned long bl_target(unsigned long at) {
    uint32_t w = rd32(at);
    if ((w >> 26) != 0x25) return 0;
    long imm = (long) (w & 0x3ffffff);
    if (imm & 0x2000000) imm -= 0x4000000;
    return at + (unsigned long) (imm * 4);
}

/* ---------- the "Java" side: the state of slot 0 ---------- */
enum { BTN_A = 0, BTN_B, BTN_X, BTN_Y, BTN_L1, BTN_R1, BTN_MENU, BTN_OPTIONS };
enum { AX_LEFT_X = 0, AX_LEFT_Y, AX_RIGHT_X, AX_RIGHT_Y, AX_DPAD_X, AX_DPAD_Y, AX_L2, AX_R2, AX_N };
static unsigned g_btn;
static float g_ax[AX_N];
static void java_push(void) { shim_pad_state(0, 1, g_btn, g_ax, AX_N); }

/* ---------- the game's device (0x8a9988 + 0x8a79e4), over the surface ---------- */
static void *g_gp;
static uint64_t g_cur, g_prev;           /* +0x30 / +0x38 with the stick halves in 16.. as one word */

/*
 * The device's own words while it reads us, as 0x8a79e4 keeps them: +0x38 and
 * +0x48 take last update's buttons and stick halves, +0x30 and +0x40 are
 * zeroed, the buttons are asked and stored at the end (0x8a9cdc), each stick
 * is asked and its halves ORed in after it (0x8a7a94..0x8a7b2c).
 */
static uint64_t device_poll(void) {
    uint64_t *now_b = (uint64_t *) (g_dev + 0x30), *was_b = (uint64_t *) (g_dev + 0x38);
    uint64_t *now_s = (uint64_t *) (g_dev + 0x40), *was_s = (uint64_t *) (g_dev + 0x48);
    *was_b = *now_b; *was_s = *now_s; *now_b = 0; *now_s = 0;
    void *d = shim_pad_dpad(g_gp, 0);
    uint64_t m = 0;
    if (shim_pad_is_pressed(shim_pad_left(d, 0), 0)) m |= 1u << 0;
    if (shim_pad_is_pressed(shim_pad_right(d, 0), 0)) m |= 1u << 1;
    if (shim_pad_is_pressed(shim_pad_up(d, 0), 0)) m |= 1u << 2;
    if (shim_pad_is_pressed(shim_pad_down(d, 0), 0)) m |= 1u << 3;
    if (shim_pad_is_pressed(shim_pad_buttonA(g_gp, 0), 0)) m |= 1u << 4;
    if (shim_pad_is_pressed(shim_pad_buttonB(g_gp, 0), 0)) m |= 1u << 5;
    if (shim_pad_is_pressed(shim_pad_buttonX(g_gp, 0), 0)) m |= 1u << 6;
    if (shim_pad_is_pressed(shim_pad_buttonY(g_gp, 0), 0)) m |= 1u << 7;
    if (shim_pad_is_pressed(shim_pad_L1(g_gp, 0), 0)) m |= 1u << 8;
    if (shim_pad_is_pressed(shim_pad_L2(g_gp, 0), 0)) m |= 1u << 9;
    if (shim_pad_is_pressed(shim_pad_R1(g_gp, 0), 0)) m |= 1u << 11;
    if (shim_pad_is_pressed(shim_pad_R2(g_gp, 0), 0)) m |= 1u << 12;
    if (shim_pad_is_pressed(shim_pad_options(g_gp, 0), 0)) m |= 1u << 14;
    if (shim_pad_is_pressed(shim_pad_menu(g_gp, 0), 0)) m |= 1u << 15;
    *now_b = m;
    void *sticks[2] = { shim_pad_left_stick(g_gp, 0), shim_pad_right_stick(g_gp, 0) };
    for (int i = 0; i < 2; i++) {                       /* 0x8a7a68..0x8a7b2c, threshold +0xe4 */
        float x = shim_pad_axis_value(shim_pad_x(sticks[i], 0), 0);
        float y = shim_pad_axis_value(shim_pad_y(sticks[i], 0), 0);
        uint64_t h = 0;
        if (x < -0.5f) h |= 1u;
        else if (x > 0.5f) h |= 2u;
        if (y < -0.5f) h |= 4u;
        else if (y > 0.5f) h |= 8u;
        *now_s |= h << (4 * i);
    }
    return m | (*now_s << 16);
}

static uint64_t code_mask(int code) {
    if (code >= 0 && code < 16) return 1ull << code;
    if ((unsigned) code >= 0xABAB0000u && (unsigned) code < 0xABAB0008u) return 1ull << (16 + ((unsigned) code - 0xABAB0000u));
    return 0;
}
static int triggered(uint64_t cur, uint64_t prev, int action) {
    for (int k = 0; k < 2; k++) {
        uint64_t m = code_mask(g_codes[action][k]);
        if ((cur & m) && !(prev & m)) return 1;
    }
    return 0;
}

/* ---------- the detector (0x516788): gate 0 with the port, 10 without ---------- */
typedef struct { int last, ago, cool; } detector_t;
static int detector_step(detector_t *d, uint64_t cur, uint64_t prev, int gate) {
    static const int TABLE[4] = { 0, 2, 1, 3 };         /* 0x952c10 */
    int dashed = 0;
    for (int i = 0; i < 4; i++) {
        if (!triggered(cur, prev, TABLE[i])) continue;
        if (d->last == i && d->ago <= gate && d->cool <= 0) { dashed = 1; d->cool = 90; }
        d->last = i;
        d->ago = 0;
    }
    if (d->ago < 9999) d->ago++;
    if (d->cool >= 0) d->cool--;                        /* 0x530c14: one down an update, to -1 */
    return dashed;
}

#define MOVE_COOL 0x25b8
#define MOVE_LAST 0x25bc
#define MOVE_AGO  0x25c0
static int *pl_last(void) { return (int *) (g_player + MOVE_LAST); }
static int *pl_ago(void) { return (int *) (g_player + MOVE_AGO); }

/*
 * One run: a script of state changes, each at (update, before or after the
 * poll). Returns the updates where the port's game dashed and where the
 * ungated game, reading the true state at its poll, dashed.
 */
typedef struct { int update, after; unsigned btn; float ax[AX_N]; } event_t;

static int g_hook_mode;                 /* 0: one hook an update; 1: one or two; 2: also none */
static unsigned g_hook_seed = 777;

static int run(const event_t *ev, int nev, int updates, int *ours, int *native, int verbose) {
    int no = 0, nn = 0;
    g_btn = 0; memset(g_ax, 0, sizeof(g_ax)); java_push();
    for (int k = 0; k < 4; k++) shim_pad_between_frames();
    g_cur = g_prev = 0;
    detector_t mine = { -1, 9999, -1 }, theirs = { -1, 9999, -1 };
    *pl_last() = -1; *pl_ago() = 9999; *(int *) (g_player + MOVE_COOL) = -1;
    uint64_t t_cur = 0, t_prev = 0;
    int e = 0;
    for (int u = 0; u < updates; u++) {
        while (e < nev && ev[e].update == u && !ev[e].after) {
            g_btn = ev[e].btn; memcpy(g_ax, ev[e].ax, sizeof(g_ax)); java_push(); e++;
        }
        /* the update: the device, then the player (the port's game: the detector is the player's fields) */
        g_prev = g_cur; g_cur = device_poll();
        mine.last = *pl_last(); mine.ago = *pl_ago(); mine.cool = *(int *) (g_player + MOVE_COOL);
        if (detector_step(&mine, g_cur, g_prev, 0)) { ours[no++] = u; if (verbose) printf("    update %d: dash (port)\n", u); }
        *pl_last() = mine.last; *pl_ago() = mine.ago; *(int *) (g_player + MOVE_COOL) = mine.cool;
        /* the same update on iOS: the true state at the poll, the window ten */
        uint64_t truth = 0;
        {
            unsigned b = g_btn;
            if (g_ax[AX_DPAD_X] < -0.1f) truth |= 1u << 0;
            if (g_ax[AX_DPAD_X] > 0.1f) truth |= 1u << 1;
            if (g_ax[AX_DPAD_Y] > 0.1f) truth |= 1u << 2;
            if (g_ax[AX_DPAD_Y] < -0.1f) truth |= 1u << 3;
            if (b & (1u << BTN_A)) truth |= 1u << 4;
            if (g_ax[AX_LEFT_X] < -0.5f) truth |= 1u << 16;
            else if (g_ax[AX_LEFT_X] > 0.5f) truth |= 1u << 17;
            if (g_ax[AX_LEFT_Y] < -0.5f) truth |= 1u << 18;
            else if (g_ax[AX_LEFT_Y] > 0.5f) truth |= 1u << 19;
        }
        t_prev = t_cur; t_cur = truth;
        if (detector_step(&theirs, t_cur, t_prev, 10)) { native[nn++] = u; if (verbose) printf("    update %d: dash (iOS)\n", u); }
        if (verbose > 1)
            printf("      u%3d dev %06llx truth %06llx | port last %d ago %d cool %d | iOS last %d ago %d cool %d\n", u,
                   (unsigned long long) g_cur, (unsigned long long) t_cur, mine.last, *pl_ago(), mine.cool,
                   theirs.last, theirs.ago, theirs.cool);
        while (e < nev && ev[e].update == u && ev[e].after) {
            g_btn = ev[e].btn; memcpy(g_ax, ev[e].ax, sizeof(g_ax)); java_push(); e++;
        }
        g_desk_ms += 33;
        /* the render's end: the port's hook - once, twice (60 frames to 30 updates) or, catching up, not at all */
        int hooks = 1;
        if (g_hook_mode) {
            g_hook_seed = g_hook_seed * 1103515245u + 12345u;
            int r = (int) ((g_hook_seed >> 16) % 6);
            hooks = r < 3 ? 1 : r < 5 ? 2 : (g_hook_mode > 1 ? 0 : 1);
        }
        for (int h = 0; h < hooks; h++) shim_pad_between_frames();
    }
    return no * 1000 + nn;
}

static event_t ev_dpad(int update, int after, float dx) {
    event_t e; memset(&e, 0, sizeof(e));
    e.update = update; e.after = after; e.ax[AX_DPAD_X] = dx;
    return e;
}
static event_t ev_dir(int stick, int vert, int update, int after, float v) {
    event_t e; memset(&e, 0, sizeof(e));
    e.update = update; e.after = after;
    e.ax[stick ? (vert ? AX_LEFT_Y : AX_LEFT_X) : (vert ? AX_DPAD_Y : AX_DPAD_X)] = v;
    return e;
}
static event_t ev_stick(int update, int after, float lx) {
    event_t e; memset(&e, 0, sizeof(e));
    e.update = update; e.after = after; e.ax[AX_LEFT_X] = lx;
    return e;
}

static int same_dashes(const int *a, int na, const int *b, int nb) {
    if (na != nb) return 0;
    for (int i = 0; i < na; i++) if (a[i] != b[i]) return 0;
    return 1;
}

static void scripted(const char *name, const event_t *ev, int n, int updates, int want) {
    int ours[64], native[64];
    printf("  %s\n", name);
    int r = run(ev, n, updates, ours, native, 1);
    int no = r / 1000, nn = r % 1000;
    char what[200];
    snprintf(what, sizeof(what), "%s: iOS dashes %d, want %d", name, nn, want);
    check(nn == want, what);
    snprintf(what, sizeof(what), "%s: the port dashes %d times, at the same updates as iOS (%d)", name, no, nn);
    check(same_dashes(ours, no, native, nn), what);
}

int main(int argc, char **argv) {
    const char *img = argc > 1 ? argv[1] : "../../work/isaac.arm64";   /* surface.sh passes it */
    FILE *f = fopen(img, "rb");
    if (!f) { printf("no image %s\n", img); return 2; }
    fseek(f, 0, SEEK_END); g_img_n = ftell(f); fseek(f, 0, SEEK_SET);
    g_img = (unsigned char *) calloc(1, (size_t) g_img_n + 0x200000);
    if (fread(g_img, 1, (size_t) g_img_n, f) != (size_t) g_img_n) return 2;
    fclose(f);

    /* the game's state as the run has it: the manager made, two codes an action, the Repentance bindings */
    *(unsigned long *) (g_img + 0xac2428) = 1;
    *(uint32_t *) (g_img + 0xabec7c) = 2;
    for (int a = 0; a < 0x40; a++) g_codes[a][0] = g_codes[a][1] = -1;
    for (int i = 0; i < 37; i++) {                      /* 0xa9c5c0: (action, code) pairs of the pad */
        int a = (int) rd32(0xa9c5c0 + i * 8), c = (int) rd32(0xa9c5c0 + i * 8 + 4);
        if (a < 0 || a >= 0x40) continue;
        if (g_codes[a][0] == -1) g_codes[a][0] = c; else g_codes[a][1] = c;
    }
    for (int a = 0; a < 0x40; a++) g_bind_table[a] = (unsigned long) g_codes[a];
    *(uint32_t *) (g_dev + 0x1c) = 0x25;
    *(unsigned long *) (g_dev + 0x20) = (unsigned long) g_bind_table;
    *(float *) (g_dev + 0xe4) = 0.5f;
    *(int *) (g_player + 0x19ec) = DEV_ID;
    {
        char what[120];
        snprintf(what, sizeof(what), "walk left is d-pad 0 and stick 0xABAB0000 (have %x %x)", g_codes[0][0], g_codes[0][1]);
        check(g_codes[0][0] == 0 && (unsigned) g_codes[0][1] == 0xABAB0000u, what);
        check(g_codes[11][0] == 12, "drop (11) is R2 (code 12)");
        check(rd32(0x952c10) == 0 && rd32(0x952c14) == 2 && rd32(0x952c18) == 1 && rd32(0x952c1c) == 3,
              "the movement detector's table is 0 2 1 3");
        check(rd32(0x5167b0) == 0x7100291Fu, "the image's window is cmp w8, #0xa at 0x5167b0");
    }

    /* the pad connects and the game takes its controller */
    java_push();
    shim_pad_controllers(0, 0);
    g_gp = shim_pad_gamepad(shim_pad_controller_object(0), 0);
    check(g_gp != NULL, "the pad has a button set");

    printf("1. drop\n");
    check(bl_target(0x52ad84) == 0x671c5c, "0x52ad84 calls 0x671c5c (held) in the image");
    check(bl_target(0x52aea0) == 0x671c5c, "0x52aea0 (the 121-update counter) calls 0x671c5c");
    check(bl_target(0x1bda1c) == 0x8b6600 && bl_target(0xb8d64) == 0x8b6600,
          "Afterbirth+ and Rebirth ask 'went down' (0x8b6600) at the same road");
    g_patches = 0;
    shim_pad_between_frames();
    check(bl_target(0x52ad84) == 0x671c6c, "0x52ad84 now calls 0x671c6c (went down this update)");
    check(bl_target(0x52aea0) == 0x671c5c, "0x52aea0 still asks held");
    check(g_patches == 1, "one word written");
    shim_pad_between_frames();
    check(g_patches == 1, "and only once");

    printf("2. R2 held\n");
    {
        int edges = 0, steady = 1;
        g_cur = g_prev = 0;
        for (int u = 0; u < 120; u++) {
            g_ax[AX_R2] = (u >= 5 && u < 100) ? 1.0f : 0.0f;
            java_push();
            g_prev = g_cur; g_cur = device_poll();
            if ((g_cur & (1u << 12)) && !(g_prev & (1u << 12))) edges++;
            if (u >= 5 && u < 100 && !(g_cur & (1u << 12))) steady = 0;
            shim_pad_between_frames();
        }
        check(edges == 1, "a held R2 is one edge for the device over 95 updates");
        check(steady, "and pressed on every one of them");
        g_ax[AX_R2] = 0; java_push();
    }

    printf("2b. the drop road (0x52acd8) over R2 held 130 updates\n");
    {
        int ask_trig = bl_target(0x52ad84) == 0x671c6c, count_held = bl_target(0x52aea0) == 0x671c5c;
        int swaps = 0, swaps_ios = 0, drop_at = -1, counter = 0;
        g_cur = g_prev = 0;
        memset(g_ax, 0, sizeof(g_ax)); g_btn = 0;
        for (int u = 0; u < 160; u++) {
            g_ax[AX_R2] = (u >= 10 && u < 140) ? 1.0f : 0.0f;
            java_push();
            g_prev = g_cur; g_cur = device_poll();
            int held = 0, went = triggered(g_cur, g_prev, 11);
            for (int k = 0; k < 2; k++) if (g_cur & code_mask(g_codes[11][k])) held = 1;
            if (ask_trig ? went : held) swaps++;
            if (held) swaps_ios++;
            if (count_held ? held : went) {                  /* 0x52aea0..0x52aebc: +1, at 0x79 the drop */
                if (++counter == 0x79 && drop_at < 0) drop_at = u - 10 + 1;
            } else counter = 0;
            shim_pad_between_frames();
        }
        printf("    swaps %d (the iOS road: %d), the card dropped at the hold's update %d\n", swaps, swaps_ios, drop_at);
        check(swaps == 1, "a press of R2 held 130 updates is one swap");
        check(drop_at == 121, "and the long press still drops, at the 121st update");
        g_ax[AX_R2] = 0; java_push();
    }

    printf("3. the double tap\n");
    {
        event_t e[] = { ev_dpad(2, 0, -1), ev_dpad(4, 0, 0), ev_dpad(7, 0, -1), ev_dpad(9, 0, 0) };
        scripted("d-pad left twice, five updates apart, both before the poll", e, 4, 30, 1);
    }
    {
        event_t e[] = { ev_dpad(2, 1, -1), ev_dpad(4, 1, 0), ev_dpad(7, 1, -1), ev_dpad(9, 1, 0) };
        scripted("d-pad left twice, both after the poll", e, 4, 30, 1);
    }
    {
        event_t e[] = { ev_stick(2, 0, -0.9f), ev_stick(4, 0, 0), ev_stick(8, 0, -0.9f), ev_stick(10, 0, 0) };
        scripted("stick left flicked twice", e, 4, 30, 1);
    }
    {
        event_t e[] = { ev_dpad(2, 0, -1), ev_dpad(4, 0, 0), ev_dpad(20, 0, -1), ev_dpad(22, 0, 0) };
        scripted("d-pad left twice, eighteen updates apart", e, 4, 40, 0);
    }
    {
        event_t e[] = { ev_dpad(2, 0, -1), ev_dpad(4, 0, 0), ev_dpad(7, 0, 1), ev_dpad(9, 0, 0) };
        scripted("left then right", e, 4, 30, 0);
    }
    {
        /* the edge of the window: the second press judged at a count of exactly 10, then 11 */
        event_t a[] = { ev_dpad(2, 0, -1), ev_dpad(4, 0, 0), ev_dpad(12, 0, -1), ev_dpad(14, 0, 0) };
        scripted("second press at a count of 10, before the poll", a, 4, 30, 1);
        event_t b2[] = { ev_dpad(2, 0, -1), ev_dpad(4, 0, 0), ev_dpad(11, 1, -1), ev_dpad(14, 0, 0) };
        scripted("second press at a count of 10, after the poll before", b2, 4, 30, 1);
        event_t c[] = { ev_dpad(2, 0, -1), ev_dpad(4, 0, 0), ev_dpad(13, 0, -1), ev_dpad(15, 0, 0) };
        scripted("second press at a count of 11", c, 4, 30, 0);
    }
    {
        /* a single press is not held back: the device sees it at the update it arrived before */
        g_btn = 0; memset(g_ax, 0, sizeof(g_ax)); java_push();
        for (int k = 0; k < 3; k++) shim_pad_between_frames();
        *pl_last() = -1; *pl_ago() = 9999;
        g_ax[AX_DPAD_X] = -1; java_push();
        check((device_poll() & 1u) != 0, "a first press reaches the poll with no delay");
        g_ax[AX_DPAD_X] = 0; java_push();
    }
    {
        /* random tapping on the d-pad and the stick against the ungated game */
      for (g_hook_mode = 0; g_hook_mode < 3; g_hook_mode++) {
        unsigned seed = 12345;
        int runs = 3000, bad = 0, total = 0;
        g_quiet = 1;
        for (int r = 0; r < runs; r++) {
            event_t ev[64];
            int n = 0, u = 1;
            int stick = r & 1;
            while (n < 60 && u < 150) {
                seed = seed * 1103515245u + 12345u;
                int gap = 1 + (int) ((seed >> 16) % 9);
                int after = (int) ((seed >> 8) & 1);
                float dir = ((seed >> 12) % 5 == 0) ? 1.0f : -1.0f;
                float v = stick ? dir * 0.9f : dir;
                int vert = ((seed >> 24) % 4) == 0;
                ev[n++] = ev_dir(stick, vert, u, after, v);
                u += 1 + (int) ((seed >> 20) % 3);
                ev[n++] = ev_dir(stick, vert, u, (int) ((seed >> 4) & 1), 0);
                u += gap;
            }
            int ours[200], native[200];
            int res = run(ev, n, 170, ours, native, 0);
            int no = res / 1000, nn = res % 1000;
            total += nn;
            if (!same_dashes(ours, no, native, nn)) {
                bad++;
                if (bad == 2) { g_quiet = 0; run(ev, n, 170, ours, native, 2); g_quiet = 1; }
                if (bad <= 3) {
                    printf("    run %d (%s): port", r, stick ? "stick" : "d-pad");
                    for (int i = 0; i < no; i++) printf(" %d", ours[i]);
                    printf(" | iOS");
                    for (int i = 0; i < nn; i++) printf(" %d", native[i]);
                    printf("\n");
                }
            }
        }
        g_quiet = 0;
        char what[160];
        snprintf(what, sizeof(what), "random tapping (%s), %d runs, %d iOS dashes: the port differs in %d",
                 g_hook_mode == 0 ? "a hook an update" : g_hook_mode == 1 ? "one or two hooks an update"
                                                       : "and updates back to back", runs, total, bad);
        printf("  %s\n", what);
        check(bad == 0, what);
        check(total > 100, "the random runs do dash");
      }
    }

    printf("4. the double tap from the keyboard\n");
    {
        /* the keyboard's device: its bindings (0xa9c6e8: 0x1c pairs), SET / CURRENT / PREVIOUS as 0x8ddc40 has them */
        static int kcodes[0x40][2];
        static unsigned long ktable[0x40];
        static unsigned char kdev[0x100];
        static unsigned char kset[0x401], kcur[0x401], kprev[0x401];
        for (int a = 0; a < 0x40; a++) kcodes[a][0] = kcodes[a][1] = -1;
        for (int i = 0; i < 0x1c; i++) {
            int a = (int) rd32(0xa9c6e8 + i * 8), c = (int) rd32(0xa9c6e8 + i * 8 + 4);
            if (a < 0 || a >= 0x40) continue;
            if (kcodes[a][0] == -1) kcodes[a][0] = c; else kcodes[a][1] = c;
        }
        for (int a = 0; a < 0x40; a++) ktable[a] = (unsigned long) kcodes[a];
        *(uint32_t *) (kdev + 0x1c) = 0x25;
        *(unsigned long *) (kdev + 0x20) = (unsigned long) ktable;
        kdev[0xd] = 1;
        check(kcodes[0][0] == 0x41 && kcodes[1][0] == 0x44 && kcodes[2][0] == 0x57,
              "the keyboard walks on A, D, W (0xa9c6e8)");
        int shim_double_tap_keys(int dev_id, unsigned long dev, const unsigned char *set, const unsigned char *cur, int keys);
        g_kb_player_on = 1;
        *(int *) (g_player_kb + 0x19ec) = 0;
        static const int TABLE[4] = { 0, 2, 1, 3 };
        for (g_hook_mode = 0; g_hook_mode < 3; g_hook_mode++) {
            unsigned seed = 4242;
            int runs = 3000, bad = 0, total = 0, ours_total = 0;
            for (int r = 0; r < runs; r++) {
                memset(kset, 0, sizeof(kset)); memset(kcur, 0, sizeof(kcur)); memset(kprev, 0, sizeof(kprev));
                detector_t mine = { -1, 9999, -1 }, theirs = { -1, 9999, -1 };
                *(int *) (g_player_kb + 0x25bc) = -1; *(int *) (g_player_kb + 0x25c0) = 9999;
                int no = 0, nn = 0, ours[200], native[200];
                /* a script of key changes, delivered at the hooks: (hook number, key, down) */
                int key_at[80], key_code[80], key_down[80], ne = 0, h = 1;
                while (ne < 76 && h < 300) {
                    seed = seed * 1103515245u + 12345u;
                    int key = ((seed >> 12) % 5 == 0) ? 0x44 : (((seed >> 24) % 4) == 0 ? 0x57 : 0x41);
                    key_at[ne] = h; key_code[ne] = key; key_down[ne] = 1; ne++;
                    h += 1 + (int) ((seed >> 20) % 5);
                    key_at[ne] = h; key_code[ne] = key; key_down[ne] = 0; ne++;
                    h += 1 + (int) ((seed >> 16) % 18);
                }
                int e = 0, hook = 0;
                int later_code[80], later_n = 0;                  /* releases waiting for the device (kb_deliver) */
                unsigned hs = 777u + (unsigned) r;
                for (int u = 0; u < 200; u++) {
                    /* the update: the keyboard device first (0x8ddc40), then the player */
                    memcpy(kprev, kcur, sizeof(kcur)); memcpy(kcur, kset, sizeof(kset));
                    uint64_t cur = 0, prev = 0;                    /* the walking keys as bits 0..3 of the detector */
                    for (int a = 0; a < 4; a++) {
                        int k = kcodes[a][0];
                        if (kcur[k]) cur |= 1ull << a;
                        if (kprev[k]) prev |= 1ull << a;
                    }
                    for (int pass = 0; pass < 2; pass++) {
                        detector_t *d = pass ? &theirs : &mine;
                        if (!pass) { d->last = *(int *) (g_player_kb + 0x25bc); d->ago = *(int *) (g_player_kb + 0x25c0); }
                        int dashed = 0;
                        for (int i = 0; i < 4; i++) {
                            int a = TABLE[i];
                            if (!((cur >> a) & 1) || ((prev >> a) & 1)) continue;
                            if (d->last == i && d->ago <= (pass ? 10 : 0) && d->cool <= 0) { dashed = 1; d->cool = 90; }
                            d->last = i; d->ago = 0;
                        }
                        if (d->ago < 9999) d->ago++;
                        if (d->cool >= 0) d->cool--;
                        if (!pass) { *(int *) (g_player_kb + 0x25bc) = d->last; *(int *) (g_player_kb + 0x25c0) = d->ago; }
                        if (dashed) { if (pass) native[nn++] = u; else ours[no++] = u; }
                    }
                    /* the frame's end: the hooks, each delivering the keys due (shim_keys_deliver) */
                    int hooks = 1;
                    if (g_hook_mode) {
                        hs = hs * 1103515245u + 12345u;
                        int q = (int) ((hs >> 16) % 6);
                        hooks = q < 3 ? 1 : q < 5 ? 2 : (g_hook_mode > 1 ? 0 : 1);
                    }
                    for (int k = 0; k < hooks; k++) {
                        hook++;
                        int still = 0;
                        for (int i = 0; i < later_n; i++) {            /* a release waits until CURRENT has the press */
                            int c = later_code[i];
                            if (kset[c] && !kcur[c]) later_code[still++] = c; else kset[c] = 0;
                        }
                        later_n = still;
                        while (e < ne && key_at[e] <= hook) {
                            int c = key_code[e];
                            if (key_down[e]) kset[c] = 1;
                            else if (kset[c] && !kcur[c]) later_code[later_n++] = c;
                            else kset[c] = 0;
                            e++;
                        }
                        shim_double_tap_keys(0, (unsigned long) kdev, kset, kcur, 0x401);
                    }
                }
                total += nn; ours_total += no;
                if (!same_dashes(ours, no, native, nn)) {
                    bad++;
                    if (bad <= 3) {
                        printf("    run %d: port", r);
                        for (int i = 0; i < no; i++) printf(" %d", ours[i]);
                        printf(" | iOS");
                        for (int i = 0; i < nn; i++) printf(" %d", native[i]);
                        printf("\n");
                    }
                }
            }
            char what[200];
            snprintf(what, sizeof(what), "keyboard random tapping (%s), %d runs, %d iOS dashes, %d the port's: differs in %d",
                     g_hook_mode == 0 ? "a hook an update" : g_hook_mode == 1 ? "one or two hooks an update"
                                                           : "and updates back to back", runs, total, ours_total, bad);
            printf("  %s\n", what);
            check(bad == 0 && total > 100, what);
        }
        g_kb_player_on = 0;
    }

    printf("%d checks, %d failed\n", checks, fails);
    return fails ? 1 : 0;
}
