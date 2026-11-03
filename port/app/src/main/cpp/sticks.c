/*
 * The sticks: where they sit, whether they are visible, and whether we hold
 * them in place at all.
 *
 * Everything is decided by one text file next to the game, so the placement
 * can be changed without a rebuild — and so there is something to edit later
 * if we ever get to tuning it on a grid inside the game itself.
 *
 * The unit of length is the game's own system: a field of 480 by 270 with the
 * origin in the top-left corner of the black frame. Verified by measurement:
 * one unit is 5.33 pixels, with a horizontal offset of 280.
 */
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <time.h>
#include <stdarg.h>
#include <sys/stat.h>
#include <android/log.h>
#include <strings.h>
#include <errno.h>
#include "diag.h"
#include "devices.h"
#include "stickcurve.h"

#if ISAAC_DIAG
#define LOG(...) __android_log_print(ANDROID_LOG_INFO, "isaac", __VA_ARGS__)
#else
#define LOG(...) do { } while (0)      /* the build we play on has no log */
#endif

const char *shim_str_bytes(const void *);
unsigned long shim_image_base(void);
int shim_sticks_in_game(void);
void *shim_touch_object(int id);                                 /* uikit.c */
int shim_eid_players(unsigned long *out, int max);               /* eid.c */
int shim_eid_readable(const void *p, unsigned long n);           /* eid.c */
float shim_screen_overlay_to_area_x(float x);                    /* screen.c */
float shim_screen_overlay_to_area_y(float y);
extern long g_gl_draws;   /* tally of real GL draw calls */

#define FIELD_H (shim_screen_height())

/*
 * The canvas is no longer a constant: it is as wide as the screen (screen.c),
 * so every place that used to say 480 asks the game what it is laying out
 * against right now.
 */
float shim_screen_width(void);
float shim_screen_height(void);
void shim_screen_wide(int on);
int shim_screen_is_wide(void);
#define FIELD_W (shim_screen_width())

/* what the settings writer asks the other owners for - see OUR_KEYS below */
int shim_screen_shot_ceiling_get(void);
int shim_eid_font_choice(void);
const char *shim_eid_language_pinned(void);
int shim_eid_conditions_english_get(void);
int shim_eid_glitch_info_get(void);
int shim_spawner_lock_get(void);
int shim_spawner_ground_get(void);
int shim_spawner_held_get(void);          /* spawner.c: ONLY HELD */
int shim_rumble_get(void);
int shim_rumble_power_get(void);            /* rumble.c: rumblepower, 1..10 */
const char *shim_rumble_tier_get(void);     /* rumble.c: rumbletier */
int shim_tweaks_spawner(void);
int shim_tweaks_fps(void);                  /* tweaks.c: FPS COUNTER */
int shim_tweaks_mod(int i);

/*
 * How the sticks behave. Three kinds, and the numbers are the frames of our
 * value sprite as well as the byte the menu row shows (see FIELD_KIND):
 *
 *   static    stand where they were put and never move
 *   dynamic   appear under the finger and stay on the spot they were grabbed
 *   floating  appear under the finger and follow it - the game's own way,
 *             with our freeze taken off
 *
 * The image is patched for one of them only: `static` nails the seat and the
 * capture zone (shim_sticks_apply_mode), and the other two share the game's
 * untouched code. What separates dynamic from floating is ours alone, and it
 * lives in shim_sticks_pin.
 */
enum { KIND_STATIC = 0, KIND_DYNAMIC = 1, KIND_FLOATING = 2, KIND_COUNT = 3 };


/*
 * The stick keeps two places, not one: +0x1b8 is the ring as it is drawn, and
 * +0x180 is the point the pull is measured from. The game moves them together.
 */
#define STICK_GRAB 0x180

static int g_kind = KIND_STATIC;         /* keep the sticks where they are */

static const char *kind_name(int k) {
    return k == KIND_STATIC ? "static" : (k == KIND_DYNAMIC ? "dynamic" : "floating");
}
static int g_visible = 1;                /* show them at all times */
static float g_move[2] = {75.0f, 200.0f};
static float g_shoot[2] = {405.0f, 200.0f};

/*
 * Stick size, given as a percentage. Ready-made ios_buttons_<percent>.anm2
 * files sit next to ios_buttons.anm2, so the steps can be picked in the
 * settings file without rebuilding the port. 100 is the game's original look.
 */
static int g_pct = 125;                  /* small 100, medium 125, big 150, very big 175 */
#define PCT_MEDIUM   125
#define PCT_BIG      150
#define PCT_VERY_BIG 175                 /* the owner, 23.09.2026: for a large phone held far */

/*
 * Where the scale lives inside a sprite is not settled: two triples of fields
 * both look like ones. The choice is left to the settings file so it can be
 * found in a single pass.
 */
static unsigned g_scale_off = 0xe8;

/* a separate tweak for the knob's travel, so it reaches exactly the ring's edge */
static float g_reach = 1.0f;
/*
 * Sensitivity: at what fraction of the travel full speed is reached.
 *
 * The game computes speed as the deflection divided by the field at +0x1c4,
 * capped at one (0x8bedbc divides the vector by that scalar). The native
 * numbers are a travel of 16 and a divisor of 8, that is full speed at HALF
 * the circle. We stretched the travel to match the stick's size while the
 * divisor stayed native, and the fraction fell to 0.4 — hence "the character
 * is uncontrollable".
 *
 * Now the divisor is derived from the travel: 1.0 means full speed right at
 * the edge, 0.5 is the native behaviour. Below 0.4 makes no sense: that is
 * twitchier than it was before the fix.
 *
 * The two sticks want DIFFERENT fractions. Movement needs smoothness — at 0.4
 * the character bolted and was uncontrollable. Shooting does not need it:
 * there, setting a direction quickly is what matters, and the old 0.4 suited
 * the owner.
 */
/*
 * THE DEAD ZONE, ON THE STICK'S OUTPUT (batch 3, plan item 4.6).
 *
 * Read out of the stick's own update (0x862fd4..0x863048): the deflection is
 * the finger less the point the stick was taken at, kept at +0x180, and the
 * value is that vector divided by +0x1c4 and capped at one. ANALOGUE has no
 * middle of its own - the smallest tremor of a thumb resting on the glass is
 * already a direction, and the owner asked for something to hold it still
 * (13.09.2026). The game's only thresholds are DIGITAL's (0x8630a8..0x8630d0:
 * under 85% of the output is nought) and the axis zone +0x1cc it uses there.
 *
 * The zone used to be made by holding the FINGER back: inside it the stick was
 * told the finger had gone, outside it the finger was pressed afresh. Every
 * crossing of the edge was then a lift to the game - its spring mid-gesture, a
 * moving stick that blinked out and sat down again, a middle that was not the
 * game's. Now the game owns the finger whole, and the zone works on the one
 * number that comes out: the division at 0x863048 goes through
 * shim_stick_value (below; the curve itself is stickcurve.h). Inside the zone
 * that number is an exact +0.0f.
 *
 * It is a share of the TRAVEL, not a number of units: the travel follows the
 * stick's size, and a zone that did not would be wide on the small stick and
 * narrow on the big one. In DIGITAL it is where the stick starts to run (the
 * owner's variant B); MOVE SENS is ANALOGUE's alone.
 *
 * THE MOVEMENT STICK ONLY. Shooting wants the opposite - a direction set as
 * soon as the thumb moves - and a zone there only gets in the way, which the
 * owner said plainly once he had both (13.09.2026, decision 20).
 */
static float g_dead = 0.25f;         /* share of the travel, the MOVEMENT stick only */

static float g_sens = 0.8f;          /* movement stick */
static float g_sens_shoot = 0.4f;    /* shooting stick: as it was before the fix */

static float size_factor(void) { return (float) g_pct / 100.0f; }

/*
 * THE GAME'S OWN FADE, 0..1 (the owner's ninth round, 24.09.2026: under the
 * reset's darkening the D-pads stayed bright on top). The fade lives at
 * Game+0x1cf440: the level at +4, its speed at +8, its kind at +0xc - 0x88b128
 * adds the speed to the level each step and writes both, 0x894d2c asks whether
 * the level is above nought. The overlay is drawn after the fade, and a fixed
 * stick's opacity is ours (below), so it goes down as the picture does.
 */
#define GAME_FADE_LEVEL 0x1cf444

static float fade_level(void) {
    unsigned long b = shim_image_base();
    unsigned long G = b ? *(const unsigned long *) (b + 0xac3b90) : 0;
    if (!G) return 0.0f;
    float v = *(const float *) (G + GAME_FADE_LEVEL);
    if (!(v > 0.0f)) return 0.0f;
    return v > 1.0f ? 1.0f : v;
}
static char g_path[512];
/*
 * When port-touch.ini was last written, by us or by anyone: an edit from a file
 * manager while the game runs is picked up within two seconds (the watch in
 * shim_sticks_between_frames), not only after a visit through the port's own
 * document provider. The folder is in Android/media now, where any manager can
 * write (MainActivity.portDir).
 */
/*
 * The whole fingerprint, not the second (P1.3): an editor that empties the
 * file and fills it within one second used to leave the same st_mtime twice,
 * so the edit was never read and the next press in Tweaks wrote the file back
 * from memory over it. Nanoseconds and the size together tell every write
 * apart.
 */
static struct timespec g_ini_time;
static long long g_ini_size = -1;
/*
 * OUR OWN WRITES GO ONTO THE WRITER'S QUEUE (hotfix 1.72, safefile.c THE
 * PORT'S OWN FILES): the file reaches the disk a few milliseconds after
 * shim_safe_fclose answers. Its time is taken once it is there - the two-
 * second look below does not take our own write for an edit from outside -
 * and every read of the file here goes through shim_safe_fopen_read, which
 * answers with the newest content written even while it is still queued.
 */
FILE *shim_safe_fopen_read(const char *path);
int shim_safe_pending_path(const char *path);
static int g_ini_time_owed;                 /* our write is still on its way: its time is taken when it lands */
static void note_ini_time(void) {
    struct stat st;
    if (g_path[0] && shim_safe_pending_path(g_path)) { g_ini_time_owed = 1; return; }
    g_ini_time_owed = 0;
    if (g_path[0] && stat(g_path, &st) == 0) { g_ini_time = st.st_mtim; g_ini_size = (long long) st.st_size; }
    else { g_ini_time.tv_sec = 0; g_ini_time.tv_nsec = 0; g_ini_size = -1; }
}

static int ini_changed(const struct stat *st) {
    return st->st_mtim.tv_sec != g_ini_time.tv_sec || st->st_mtim.tv_nsec != g_ini_time.tv_nsec
        || (long long) st->st_size != g_ini_size;
}

/*
 * THE FILE IS WRITTEN ONLY ONCE IT HAS BEEN READ (P1.2). The loader used to
 * write the defaults over port-touch.ini on ANY failure to open it - a moment's
 * refusal of the shared storage at start, and the player's file was replaced
 * by the defaults for good. Now the defaults go down only where there is no
 * file (ENOENT); any other failure leaves the file alone, says so in the
 * report and is read again two seconds later, and until it has been read
 * nothing is written into it at all.
 */
static int g_loaded;

FILE *shim_safe_fopen(const char *path, const char *mode);
int shim_safe_fclose(FILE *f);
int shim_report_active(void);
void report_line(const char *kind, const char *text);

#if ISAAC_DIAG
#define CSAY(...) do { if (shim_report_active()) { char say_[400];                          \
        snprintf(say_, sizeof(say_), __VA_ARGS__); report_line("config", say_); } } while (0)
#else
#define CSAY(...) do { } while (0)
#endif

/*
 * Inset from the edge of the SCREEN, in percent.
 *
 * A stick's place is stored in units of the game's field (480 by 270), but the
 * field is fitted to the screen by height, leaving bars at the sides. So the
 * same point of the field gives a different inset on different screens, and
 * the sticks have to be anchored to the screen. We measure to the EDGE of the
 * ring rather than to its centre: otherwise a large stick runs off screen.
 */
/*
 * PLACED BY HAND, and the editor to come: each stick's centre and the button
 * card's corner as SHARES of the canvas, 0..1.
 *
 * Shares rather than units because the canvas is not one size: the menu lays
 * out on 624 x 288 where a run has 585 x 270 (screen.c), and the editor shows
 * the layout in the menu while the game uses it in a run. A share means the
 * same place in both. A value below zero means "not set", and then everything
 * below works exactly as it did: the insets seat the sticks and the row hangs
 * off the shooting stick (hud.c).
 */
static float g_move_at[2] = {-1.0f, -1.0f};    /* the movement stick's centre */
static float g_shoot_at[2] = {-1.0f, -1.0f};   /* the shooting stick's centre */
static float g_row_at[2] = {-1.0f, -1.0f};     /* the card's RIGHT edge, and the first row's centre */
static float g_twin_at[2] = {-1.0f, -1.0f};    /* the twins' round button, its middle (hud.c) */

/*
 * TWO LAYOUTS, AND WHY.
 *
 * A static stick stands in one place and everything else is arranged around it.
 * A dynamic or floating one has no place at all until a thumb lands: it appears
 * under the finger, anywhere in its own half. So the same card position is
 * right in one mode and wrong in the other - beside a stick that is always
 * there, or out in the open where a stick may yet appear.
 *
 * The card and the twins' button therefore keep a place for each: the pair
 * above belongs to the static mode, the pair below to the other two. The sticks
 * themselves are placed only in the static mode - there is nothing to place
 * otherwise - so they have one pair and no second.
 */
static float g_row_at_loose[2] = {-1.0f, -1.0f};
static float g_twin_at_loose[2] = {-1.0f, -1.0f};

/*
 * THE PAUSE BUTTON, WHERE THE PLAYER PUT IT (hotfix 1.71). One place for every
 * mode: it stands nowhere near a stick that comes to the finger, so nothing
 * about the sticks' kind makes it right or wrong. hud.c writes it into the
 * game's own Button; reset.c's long press reads the box back from there.
 */
static float g_pause_at[2] = {-1.0f, -1.0f};

/*
 * THE TWINS' TRINKET BUTTON, WHERE THE PLAYER PUT IT (1.75, the owner,
 * 14.10.2026). One place for every mode, as the pause button has: it is the
 * HUD's trinket corner by default, nowhere near a stick that comes to the
 * finger. twintrinkets.c stands its square there.
 */
static float g_trinket_at[2] = {-1.0f, -1.0f};

/*
 * THE TOUCH CONTROLS' OPACITY (hotfix 1.71): the editor's slider. The game
 * draws every piece of its overlay at a half (loader.c touch_alpha_words), and
 * that half is the default. A twentieth a step, never below a tenth - a
 * control nobody can see is still under the thumb.
 */
#define ALPHA_DEFAULT 0.5f
#define ALPHA_MIN     0.1f
#define ALPHA_STEP    0.05f
static float g_alpha = ALPHA_DEFAULT;

int shim_touch_alpha_apply(float a);           /* loader.c */

float shim_sticks_alpha(void) { return g_alpha; }

/* a value from anywhere - the slider, the file - onto the slider's own steps, and into the image */
void shim_sticks_alpha_set(float a) {
    if (!isfinite(a)) a = ALPHA_DEFAULT;
    a = roundf(a / ALPHA_STEP) * ALPHA_STEP;
    if (a < ALPHA_MIN) a = ALPHA_MIN;
    if (a > 1.0f) a = 1.0f;
    g_alpha = a;
    shim_touch_alpha_apply(a);
}

/* the sticks come to the finger: dynamic or floating */
static int loose_sticks(void) { return g_kind != KIND_STATIC; }

static float *row_place(void) { return loose_sticks() ? g_row_at_loose : g_row_at; }
static float *twin_place(void) { return loose_sticks() ? g_twin_at_loose : g_twin_at; }

static int placed(const float *p) {
    return p[0] >= 0.0f && p[0] <= 1.0f && p[1] >= 0.0f && p[1] <= 1.0f;
}

/* the editor's Reset (touchedit.c): back to the places the port lays out itself */
/* only the layout being edited: the other one is nobody's business here */
void shim_sticks_reset_places(void) {
    g_move_at[0] = g_move_at[1] = -1.0f;
    g_shoot_at[0] = g_shoot_at[1] = -1.0f;
    float *r = row_place(), *t = twin_place();
    r[0] = r[1] = -1.0f;
    t[0] = t[1] = -1.0f;
    g_pause_at[0] = g_pause_at[1] = -1.0f;
    g_trinket_at[0] = g_trinket_at[1] = -1.0f;
}

/*
 * All the places at once, for the editor to remember what they were when it
 * opened and put them back if it is left by the cross. Eight pairs of shares:
 * the movement stick, the shooting stick, the card, the twins' button, the
 * card and the twins' button of the moving sticks, the pause button, the
 * twins' trinket button.
 */
void shim_sticks_places_get(float *all) {
    if (!all) return;
    all[0]  = g_move_at[0];        all[1]  = g_move_at[1];
    all[2]  = g_shoot_at[0];       all[3]  = g_shoot_at[1];
    all[4]  = g_row_at[0];         all[5]  = g_row_at[1];
    all[6]  = g_twin_at[0];        all[7]  = g_twin_at[1];
    all[8]  = g_row_at_loose[0];   all[9]  = g_row_at_loose[1];
    all[10] = g_twin_at_loose[0];  all[11] = g_twin_at_loose[1];
    all[12] = g_pause_at[0];       all[13] = g_pause_at[1];
    all[14] = g_trinket_at[0];     all[15] = g_trinket_at[1];
}

/* the editor moves one thing: 0 the movement stick, 1 the shooting one, 2 the card,
   3 the twins' button, 4 the pause button, 5 the twins' trinket button */
void shim_sticks_place_at(int which, float x, float y) {
    float w = FIELD_W, h = FIELD_H;
    if (!(w > 0.0f) || !(h > 0.0f)) return;
    float *p = which == 0 ? g_move_at
             : (which == 1 ? g_shoot_at : (which == 2 ? row_place()
             : (which == 3 ? twin_place() : (which == 4 ? g_pause_at : g_trinket_at))));
    p[0] = x / w;
    p[1] = y / h;
}

void shim_sticks_places_set(const float *all) {
    if (!all) return;
    g_move_at[0] = all[0];        g_move_at[1] = all[1];
    g_shoot_at[0] = all[2];       g_shoot_at[1] = all[3];
    g_row_at[0] = all[4];         g_row_at[1] = all[5];
    g_twin_at[0] = all[6];        g_twin_at[1] = all[7];
    g_row_at_loose[0] = all[8];   g_row_at_loose[1] = all[9];
    g_twin_at_loose[0] = all[10]; g_twin_at_loose[1] = all[11];
    g_pause_at[0] = all[12];      g_pause_at[1] = all[13];
    g_trinket_at[0] = all[14];    g_trinket_at[1] = all[15];
}

/*
 * A PLACED THING IS PUSHED CLEAR OF THE STICKS, WHEREVER IT IS USED.
 *
 * The button card and the twins' button keep places of their own, and the
 * editor already refuses to drop one over a stick. But the stick's SIZE is a
 * setting of its own and can be changed afterwards: a ring that grows would
 * swallow what was placed beside it. The owner asked for the thing to step
 * aside by itself then - just far enough not to overlap, not to keep a fixed
 * distance the way the card used to (13.09.2026).
 *
 * The push is the shortest way out of the ring's own square, twice over,
 * because a step away from one stick can be a step into the other. If the box
 * still has nowhere to stand - wedged between two rings with less room than it
 * needs - it is left exactly where the player put it. An overlap the player can
 * see and mend in the editor is better than a thing that jumps somewhere of its
 * own accord.
 */
static float ring_radius(void);                  /* all four below are defined further on */
static float ring_drawn_radius(void);
static void place_by_inset(float *move, float *shoot);
static void seat_for(int is_move, float *out);

static int boxes_meet(const float *a, const float *b) {
    return a[0] < b[2] && b[0] < a[2] && a[1] < b[3] && b[1] < a[3];
}

/* the shortest way for `a` out of `b`, one axis only */
static void push_out(const float *a, const float *b, float *dx, float *dy) {
    float left = b[0] - a[2], right = b[2] - a[0];
    float up = b[1] - a[3], down = b[3] - a[1];
    float best = fabsf(right) < fabsf(left) ? right : left;
    float other = 0.0f;
    if (fabsf(up) < fabsf(best)) other = up;
    if (fabsf(down) < fabsf(best) && fabsf(down) < fabsf(up)) other = down;
    if (other != 0.0f) { *dx = 0.0f; *dy = other; return; }
    *dx = best;
    *dy = 0.0f;
}

static void move_box(float *a, float dx, float dy) {
    a[0] += dx; a[2] += dx; a[1] += dy; a[3] += dy;
}

static void keep_on_canvas(float *a) {
    float w = FIELD_W, h = FIELD_H;
    if (!(w > 0.0f) || !(h > 0.0f)) return;
    if (a[2] - a[0] > w || a[3] - a[1] > h) return;
    if (a[0] < 0.0f) move_box(a, -a[0], 0.0f);
    else if (a[2] > w) move_box(a, w - a[2], 0.0f);
    if (a[1] < 0.0f) move_box(a, 0.0f, -a[1]);
    else if (a[3] > h) move_box(a, 0.0f, h - a[3]);
}

/*
 * box is {x0, y0, x1, y1} in the PICTURE's units - the card and the twins'
 * button are the overlay's widgets, drawn in the widget pass and judged by the
 * game through 0x86255c there (screen.c, "ONE FINGER, TWO SPACES"). A stick's
 * seat is a point of the WINDOW's units (the stick pass, 0x861a20), so it is
 * put into the picture before the two are compared; the ring's radius needs no
 * such step, the game draws it at the picture's scale (0x861ec8..0x861f30:
 * area / window per axis). With FULLSCREEN on the two spaces are one. Answers
 * 1 when the box came to rest free.
 */
int shim_sticks_clear_of_sticks(float *box) {
    if (!box) return 0;
    /* a stick that comes to the finger stands nowhere: there is nothing to avoid */
    if (loose_sticks()) return 1;
    float r = ring_radius();            /* the outer circle, not the knob */
    if (!(r > 0.0f)) return 1;
    place_by_inset(g_move, g_shoot);
    float ring[2][4];
    for (int i = 0; i < 2; i++) {
        float seat[2];
        seat_for(i == 0, seat);
        float cx = shim_screen_overlay_to_area_x(seat[0]), cy = shim_screen_overlay_to_area_y(seat[1]);
        ring[i][0] = cx - r; ring[i][1] = cy - r;
        ring[i][2] = cx + r; ring[i][3] = cy + r;
    }
    float was[4] = { box[0], box[1], box[2], box[3] };
    for (int pass = 0; pass < 2; pass++) {
        for (int i = 0; i < 2; i++) {
            if (!boxes_meet(box, ring[i])) continue;
            float dx = 0.0f, dy = 0.0f;
            push_out(box, ring[i], &dx, &dy);
            move_box(box, dx, dy);
        }
        keep_on_canvas(box);
    }
    for (int i = 0; i < 2; i++) {
        if (!boxes_meet(box, ring[i])) continue;
        box[0] = was[0]; box[1] = was[1]; box[2] = was[2]; box[3] = was[3];
        return 0;                       /* nowhere to go: leave it as it was placed */
    }
    return 1;
}

/*
 * hudlayout.c: the two sticks' rings, {x0, y0, x1, y1} each in the picture's
 * units, the same squares shim_sticks_clear_of_sticks pushes boxes out of.
 * Answers 2, or 0 when the sticks come to the finger and stand nowhere.
 */
int shim_sticks_rings(float *out8) {
    if (!out8 || loose_sticks()) return 0;
    float r = ring_radius();
    if (!(r > 0.0f)) return 0;
    place_by_inset(g_move, g_shoot);
    for (int i = 0; i < 2; i++) {
        float seat[2];
        seat_for(i == 0, seat);
        float cx = shim_screen_overlay_to_area_x(seat[0]), cy = shim_screen_overlay_to_area_y(seat[1]);
        out8[i * 4] = cx - r; out8[i * 4 + 1] = cy - r;
        out8[i * 4 + 2] = cx + r; out8[i * 4 + 3] = cy + r;
    }
    return 2;
}

/*
 * twintrinkets.c: the trinket button where nobody has placed it. The HUD's
 * trinket corner lies under the movement stick's ring, and a thumb on the stick
 * dropped trinkets; by default the button now stands just right of the ring,
 * its left edge on the ring's right one, at the corner's own height (the owner,
 * 25.10.2026). Answers 1 when it was moved; a box that would land on the
 * shooting ring, or sticks that come to the finger, leave it as it was.
 */
int shim_sticks_right_of_move(float *box) {
    float ring[8];
    if (!box || shim_sticks_rings(ring) != 2 || !boxes_meet(box, ring)) return 0;
    float was[4] = { box[0], box[1], box[2], box[3] };
    move_box(box, ring[2] - box[0], 0.0f);
    keep_on_canvas(box);
    if (boxes_meet(box, ring) || boxes_meet(box, ring + 4)) {
        box[0] = was[0]; box[1] = was[1]; box[2] = was[2]; box[3] = was[3];
        return 0;
    }
    return 1;
}

/* hud.c: where the twins' button is pinned, in canvas units; 0 when nobody has placed it */
int shim_sticks_twin_anchor(float *x, float *y) {
    const float *p = twin_place();
    if (!placed(p)) return 0;
    if (x) *x = p[0] * FIELD_W;
    if (y) *y = p[1] * FIELD_H;
    return 1;
}

/* hud.c: where the pause button is pinned, in canvas units; 0 when nobody has placed it */
int shim_sticks_pause_anchor(float *x, float *y) {
    if (!placed(g_pause_at)) return 0;
    if (x) *x = g_pause_at[0] * FIELD_W;
    if (y) *y = g_pause_at[1] * FIELD_H;
    return 1;
}

/* twintrinkets.c: where the twins' trinket button is pinned, in canvas units; 0 when nobody has placed it */
int shim_sticks_trinket_anchor(float *x, float *y) {
    if (!placed(g_trinket_at)) return 0;
    if (x) *x = g_trinket_at[0] * FIELD_W;
    if (y) *y = g_trinket_at[1] * FIELD_H;
    return 1;
}

/* hud.c: where the button card is pinned, in canvas units; 0 when nobody has placed it */
int shim_sticks_row_anchor(float *right, float *middle) {
    const float *p = row_place();
    if (!placed(p)) return 0;
    if (right) *right = p[0] * FIELD_W;
    if (middle) *middle = p[1] * FIELD_H;
    return 1;
}

static int g_inset_x = 5;       /* from the side edges, canvas share; the ring radius
                                   is added on top, so 5 puts the centre near 12% */
static int g_inset_y = 10;      /* from the bottom: at 5% the sticks sat too low */

int shim_gl_width(void);
int shim_gl_height(void);

/* the memory reader comes from the hook; we keep it to read names safely */
static int (*g_readable)(const void *, unsigned long);
static int probe_readable(const void *p, unsigned long n) {
    return g_readable ? g_readable(p, n) : 0;
}

/*
 * A temporary key for chasing down a cause: a full comparison of the two
 * sticks turned up just two suspicious differences — field +0x08 (7 against 6)
 * and flag +0x1c9 (1 against 0). We make the movement stick match the shooting
 * one by one of them and watch whether the overlay bug goes away. Switched
 * from the file, with no rebuild.
 */
static int g_probe;                      /* 0 none, 1 field +08, 2 flag +1c9 */

static void clamp_place(float *p) {
    if (p[0] < 0) p[0] = 0;
    if (p[1] < 0) p[1] = 0;
    if (p[0] > FIELD_W) p[0] = FIELD_W;
    if (p[1] > FIELD_H) p[1] = FIELD_H;
}

/*
 * EVERY KEY THIS FILE HOLDS, and why the list has to exist.
 *
 * The writer below puts the whole file down afresh. It used to write six keys
 * out of twelve, which threw away the rest; that was fixed by teaching it the
 * twelve. It then went on throwing away the keys that belong to OTHER modules
 * - shotmax, eidfont, eidlang, eidall - because it had never been told they
 * existed. The owner hit it from the other side: shotmax had to be typed in by
 * hand, and the next change made from the menu deleted it again (16.09.2026).
 *
 * Two halves to the answer, and both are needed.
 *
 * Keys we OWN are asked of their owner and written every time, so the list can
 * never go stale against what the game is actually running.
 *
 * Keys we do not know are COPIED THROUGH from the file as it stands. `aspect`
 * is read in MainActivity before any of this is alive, `probe` and
 * `scalefield` are hand-set diagnostics - nothing here can ask for their
 * values, and a writer that cannot ask must not delete. This also means a key
 * added later survives a build that predates it.
 *
 * The legacy spellings are listed as known so they are CONSUMED rather than
 * copied: kept lines go at the end of the file, and a stale `inset=` sitting
 * after our `insetx=` would quietly win on the next load.
 */
static const char *const OUR_KEYS[] = {
    "mode", "wide", "visible", "sizepct", "insetx", "insety",
    "reach", "sens", "sensshoot", "deadzone",
    "move", "shoot", "movepos", "shootpos",
    "rowpos", "twinpos", "rowposloose", "twinposloose", "pausepos", "trinketpos", "touchalpha",
    "shotmax", "eidfont", "eidlang", "eidall", "eidglitch", "spawner", "spawnerlock", "spawnerground", "spawnerheld", "fps", "rumble",
    "rumblepower", "rumbletier",
    "moddance", "modplanet", "modwaves", "modbars", "modmarks", "modstats", "modrangefix", "modeid",
    "size", "inset",                      /* older spellings, parsed, not written */
};

static int ours(const char *key, size_t n) {
    for (unsigned i = 0; i < sizeof(OUR_KEYS) / sizeof(OUR_KEYS[0]); i++)
        if (strlen(OUR_KEYS[i]) == n && !strncmp(key, OUR_KEYS[i], n)) return 1;
    return 0;
}

/*
 * A section header, "[name]": the name into out, and 1. The mods' settings live
 * in sections (modset.c); everything above the first header is ours.
 */
int shim_modset_section(const char *section);
int shim_modset_parse(const char *section, const char *key, const char *value);
void shim_modset_write(FILE *f);

static int g_mod_sections_seen;             /* the file already carries the mods' sections */

static int section_header(const char *line, char *out, size_t room) {
    while (*line == ' ' || *line == '\t') line++;
    if (*line != '[') return 0;
    const char *end = strchr(line, ']');
    if (!end) return 0;
    size_t n = (size_t) (end - line - 1);
    if (n >= room) n = room - 1;
    memcpy(out, line + 1, n);
    out[n] = 0;
    return 1;
}

/*
 * The strangers are kept WHOLE, however many and however long (P1.2): they
 * used to go into buffers of 2 and 4 KB and lines of 256 bytes, and whatever
 * did not fit was quietly dropped - a mod's section past the limit was lost
 * for good on the next press in Tweaks.
 */
typedef struct { char *p; size_t n, room; int lost; } grow_t;

static void append_line(grow_t *g, const char *s) {
    size_t take = strlen(s);
    if (g->n + take + 2 > g->room) {
        size_t r = g->room ? g->room : 4096;
        while (r < g->n + take + 2) r *= 2;
        char *q = realloc(g->p, r);
        if (!q) { g->lost++; return; }
        g->p = q;
        g->room = r;
    }
    memcpy(g->p + g->n, s, take);
    g->n += take;
    if (!g->n || g->p[g->n - 1] != '\n') g->p[g->n++] = '\n';
    g->p[g->n] = 0;
}

/*
 * The lines of the current file that name a key we cannot ask anyone for: at
 * the top level into `out`, and whole sections nobody here owns into `sections`
 * (a section modset.c owns is written from its own values instead).
 */
/* 0 read (no file counts: nothing to keep), -1 the file is there and did not read */
static int keep_strangers(grow_t *out, grow_t *sections) {
    FILE *f = shim_safe_fopen_read(g_path);
    if (!f) return errno == ENOENT ? 0 : -1;
    char *line = NULL, section[64] = "";
    size_t cap = 0;
    while (getline(&line, &cap, f) > 0) {
        char name[64];
        if (section_header(line, name, sizeof(name))) {
            snprintf(section, sizeof(section), "%s", name);
            if (!shim_modset_section(section)) append_line(sections, line);
            continue;
        }
        const char *p = line;
        while (*p == ' ' || *p == '\t') p++;
        if (section[0]) {
            if (!shim_modset_section(section) && *p != '#' && *p != '\n' && *p != '\r' && *p)
                append_line(sections, p);
            continue;
        }
        if (*p == '#' || *p == '\n' || *p == '\r' || !*p) continue;
        const char *eq = strchr(p, '=');
        if (!eq) continue;
        size_t len = (size_t) (eq - p);
        while (len && (p[len - 1] == ' ' || p[len - 1] == '\t')) len--;
        if (ours(p, len)) continue;
        append_line(out, p);
    }
    int bad = ferror(f);
    free(line);
    fclose(f);
    return bad ? -1 : 0;
}

static void write_default(void) {
    if (!g_path[0]) return;
    if (!g_loaded) {
        CSAY("port-touch.ini NOT written: it has not been read yet this launch");
        return;
    }
    grow_t strangers_g = {0}, sections_g = {0};
    if (keep_strangers(&strangers_g, &sections_g) != 0) {
        /* what is in the file could not be read, so it cannot be kept: nothing is written over it */
        CSAY("port-touch.ini NOT written: it did not read back (errno %d)", errno);
        free(strangers_g.p);
        free(sections_g.p);
        return;
    }
    const char *strangers = strangers_g.p ? strangers_g.p : "";
    const char *strange_sections = sections_g.p ? sections_g.p : "";
    size_t kept = strangers_g.n + sections_g.n;
    FILE *f = shim_safe_fopen(g_path, "w");
    if (!f) {
        CSAY("port-touch.ini NOT written: it did not open (errno %d)", errno);
        free(strangers_g.p);
        free(sections_g.p);
        return;
    }
    fprintf(f,
        "# Port sticks. Game field: 480 x 270, origin at the top-left corner\n"
        "# of the black frame. Change the numbers and restart the game.\n"
        "#\n"
        "# mode      = static | dynamic | floating\n"
        "#             (static - fixed in place, dynamic - appear under the\n"
        "#              finger and stay put, floating - and follow it)\n"
        "# visible   = 1 | 0             (show them even when untouched)\n"
        "# sizepct   = 100 | 125 | 150 | 175   (size; also changed from the menu)\n"
        "# insetx    = percent of the CANVAS width, edge to the ring\n"
        "# insety    = percent           (gap from the bottom edge)\n"
        "# reach     = travel multiplier (how far the knob is pulled)\n"
        "# sens      = fraction of travel (where the movement stick reaches\n"
        "#                                full speed: 0.5 as in the original,\n"
        "#                                lower is twitchier, higher smoother)\n"
        "# sensshoot = the same for shooting\n"
        "# deadzone  = how much of the travel counts as the middle, where the\n"
        "#             stick gives nothing at all. A share, 0 to turn it off.\n"
        "#             0.25 by default. The game itself has none.\n"
        "#\n"
        "# move and shoot set the places by hand and only work when\n"
        "# insetx and insety are zero.\n"
        "#\n"
        "# movepos, shootpos = a stick's centre as a share of the canvas,\n"
        "#             \"x,y\" from 0 to 1. Set, they override the insets and\n"
        "#             the places above. Below zero means not set.\n"
        "# rowpos    = the button card: its RIGHT edge and the middle of its\n"
        "#             first row, the same way. Set, the row hangs there\n"
        "#             instead of beside the shooting stick.\n"
        "# twinpos   = the twins' round button (Jacob and Esau), its middle,\n"
        "#             the same way. Set, it stands there instead of beside\n"
        "#             the pause button.\n"
        "#\n"
        "# rowposloose, twinposloose = the same two, for the dynamic and\n"
        "#             floating modes. A stick that comes to the finger has\n"
        "#             no place of its own, so what stands well beside a\n"
        "#             static stick stands wrong there; each mode keeps its\n"
        "#             own. movepos and shootpos belong to the static mode\n"
        "#             alone.\n"
        "# pausepos  = the pause button, its middle, the same way; one for\n"
        "#             every mode. Set, it stands there instead of the top\n"
        "#             right. Its long press (restart) goes with it.\n"
        "# trinketpos = the twins' trinket button (Jacob and Esau), its\n"
        "#             middle, the same way; one for every mode. Set, it\n"
        "#             stands there instead of the HUD's trinket corner.\n"
        "# touchalpha = how solid every touch control is drawn, 0.10 to 1\n"
        "#             in steps of 0.05; 0.50 is the game's own. The OPACITY\n"
        "#             slider of the touch editor.\n"
        "#\n"
        "# aspect    = W:H to run in a frame of that shape, centred, with\n"
        "#             the rest of the screen black - for trying the\n"
        "#             layout at 16:9, 21:9, 16:10 or 4:3 on one phone.\n"
        "#             WxH instead of W:H asks for that many PIXELS,\n"
        "#             which reproduces another phone's window exactly\n"
        "#             - the same units AND the same pixels per unit.\n"
        "#             Absent means the whole screen.\n"
        "#\n"
        "# eidfont   = 0..3: which font the item descriptions use\n"
        "#             (absent means the mod's own pixel font)\n"
        "#\n"
        "# eidlang   = the language of the item descriptions: a code\n"
        "#             the mod knows (ru, en_us, de, fr, spa, pl, ar...).\n"
        "#             Absent follows the game's own Language setting.\n"
        "#\n"
        "# eidall    = 1 to show the lines about THIS run (synergies,\n"
        "#             the character, what overrides what) in English\n"
        "#             where the chosen language has not translated\n"
        "#             them. Absent shows only what the language has,\n"
        "#             which is what the mod itself does.\n"
        "#\n"
        "# eidglitch = 1 to describe a glitched (TMTRAINER) item in full:\n"
        "#             what it grants, its stats and its effects. Absent\n"
        "#             shows its own garbled text - the mod's default,\n"
        "#             since the full list gives the item away.\n"
        "#\n"
        "# wide      = 0 to switch the widened canvas off and run on\n"
        "#             the design 480 x 270 with black bars, the way\n"
        "#             the game ran before. 1 or absent is normal.\n"
        "#\n"
        "# spawner   = 1 shows the CONSOLE (the item spawner) in the pause menu.\n"
        "# fps       = 1 shows the frames a second at the top of the\n"
        "#             screen (Tweaks: FPS COUNTER).\n"
        "# spawnerlock = 1: a run that got anything from the console unlocks\n"
        "#             nothing, as a run on a special seed (crossed trophy on\n"
        "#             the HUD). 0 lifts that. Also on the Tweaks page, the\n"
        "#             other way round: CONSOLE TROPHIES ON is spawnerlock=0.\n"
        "# spawnerground = 1: the console's \"On the ground\" is ON - things\n"
        "#             land beside the player instead of going into him.\n"
        "# rumble    = 1 shakes the phone (or the pad) where the game rumbles:\n"
        "#             explosions, slams, hits. 0 turns it off.\n"
        "# rumblepower = 1..10, how strong (7 by default). Both are the\n"
        "#             RUMBLE slider in Options (the game's own row): 0 there\n"
        "#             is rumble=0.\n"
        "# rumbletier = auto | primitives | clicks | waveform | onoff: the\n"
        "#             kind of effects played; auto picks the best this\n"
        "#             phone has, the others force a simpler one.\n"
        "# moddance, modplanet, modwaves, modbars, modmarks, modstats,\n"
        "# modrangefix, modeid =\n"
        "#             the ported mods and the item descriptions, 1 on, 0 off;\n"
        "#             all on at first but the dance, Stats+ and Range Fix.\n"
        "#             All of these are switched on the\n"
        "#             MODS page of the main menu, their settings behind the arrows.\n"
        "#\n"
        "# shotmax   = Render Scale in Tweaks: -1 POTATO (one canvas, the\n"
        "#             heavy effects off - light, bloom, shockwaves,\n"
        "#             caustics, water - for a weak phone), 1 or 0\n"
        "#             ORIGINAL (one canvas, as iOS draws it), and any\n"
        "#             number above 1 ENHANCED (the scene enlarged by\n"
        "#             the screen's own measure: 3 on a 1080-tall\n"
        "#             phone, 4 on 1440).\n"
        "\n"
        "mode=%s\n"
        "wide=%d\n"
        "visible=%d\n"
        "sizepct=%d\n"
        "insetx=%d\n"
        "insety=%d\n"
        "reach=%.2f\n"
        "sens=%.2f\n"
        "sensshoot=%.2f\n"
        "deadzone=%.2f\n"
        "move=%.0f,%.0f\n"
        "shoot=%.0f,%.0f\n"
        "movepos=%.3f,%.3f\n"
        "shootpos=%.3f,%.3f\n"
        "rowpos=%.3f,%.3f\n"
        "twinpos=%.3f,%.3f\n"
        "rowposloose=%.3f,%.3f\n"
        "twinposloose=%.3f,%.3f\n"
        "pausepos=%.3f,%.3f\n"
        "trinketpos=%.3f,%.3f\n"
        "touchalpha=%.2f\n"
        "spawner=%d\n"
        "spawnerlock=%d\n"
        "spawnerground=%d\n"
        "spawnerheld=%d\n"
        "fps=%d\n"
        "rumble=%d\n"
        "rumblepower=%d\n"
        "rumbletier=%s\n"
        "moddance=%d\n"
        "modplanet=%d\n"
        "modwaves=%d\n"
        "modbars=%d\n"
        "modmarks=%d\n"
        "modstats=%d\n"
        "modrangefix=%d\n"
        "modeid=%d\n"
        "shotmax=%d\n"
        "eidfont=%d\n"
        "eidlang=%s\n"
        "eidall=%d\n"
        "eidglitch=%d\n",
        kind_name(g_kind), shim_screen_is_wide(), g_visible, g_pct, g_inset_x, g_inset_y,
        (double) g_reach, (double) g_sens, (double) g_sens_shoot, (double) g_dead,
        (double) g_move[0], (double) g_move[1],
        (double) g_shoot[0], (double) g_shoot[1],
        (double) g_move_at[0], (double) g_move_at[1],
        (double) g_shoot_at[0], (double) g_shoot_at[1],
        (double) g_row_at[0], (double) g_row_at[1],
        (double) g_twin_at[0], (double) g_twin_at[1],
        (double) g_row_at_loose[0], (double) g_row_at_loose[1],
        (double) g_twin_at_loose[0], (double) g_twin_at_loose[1],
        (double) g_pause_at[0], (double) g_pause_at[1],
        (double) g_trinket_at[0], (double) g_trinket_at[1],
        (double) g_alpha,
        shim_tweaks_spawner(),
        shim_spawner_lock_get(),
        shim_spawner_ground_get(),
        shim_spawner_held_get(),
        shim_tweaks_fps(),
        shim_rumble_get(),
        shim_rumble_power_get(),
        shim_rumble_tier_get(),
        shim_tweaks_mod(0), shim_tweaks_mod(1), shim_tweaks_mod(2), shim_tweaks_mod(3), shim_tweaks_mod(4),
        shim_tweaks_mod(5), shim_tweaks_mod(6), shim_tweaks_mod(7),
        shim_screen_shot_ceiling_get(),
        shim_eid_font_choice(),
        shim_eid_language_pinned()[0] ? shim_eid_language_pinned() : "auto",
        shim_eid_conditions_english_get(),
        shim_eid_glitch_info_get());
    if (strangers[0]) {
        fputs("\n# keys this build does not know, kept as they were\n", f);
        fputs(strangers, f);
    }
    /* the ported mods' settings, a section each, and last any section we do not know */
    fputs("\n# ---------------------------------------------------------------------\n"
          "# The ported mods. Whether a mod is on at all is the Tweaks page (and the\n"
          "# mod* keys above); what it does when it is on is its section below.\n", f);
    shim_modset_write(f);
    if (strange_sections[0]) {
        fputs("\n", f);
        fputs(strange_sections, f);
    }
    int ok = shim_safe_fclose(f) == 0;
    if (!ok) CSAY("port-touch.ini NOT written whole - the old file stands");
    if (strangers_g.lost || sections_g.lost)
        CSAY("%d lines of strangers dropped (no memory)", strangers_g.lost + sections_g.lost);
    free(strangers_g.p);
    free(sections_g.p);
    note_ini_time();
    LOG("sticks: settings written — %s%s%s", g_path, kept ? " (strangers kept)" : "", ok ? "" : " - FAILED");
    (void) kept;
}

/*
 * THE EDITOR'S PLACES AND THE OPACITY INTO THE FILE, AND NOTHING ELSE TOUCHED.
 *
 * write_default() above writes the whole file from what this one module holds,
 * so every key that belongs to somebody else - aspect, eidfont, eidlang,
 * eidall, wide, scalefield - would be dropped by it. The editor saves often
 * and must not do that, so the file is read back, the three lines are taken
 * out wherever they stand, and fresh ones are put at the end.
 *
 * A file too large for the buffer is left alone rather than cut short.
 */
static int line_is_place(const char *line, size_t len) {
    static const char *const KEYS[] = { "movepos", "shootpos", "rowpos", "twinpos",
                                        "rowposloose", "twinposloose", "pausepos", "trinketpos",
                                        "touchalpha" };
    size_t i = 0;
    while (i < len && (line[i] == ' ' || line[i] == '\t')) i++;
    for (unsigned k = 0; k < sizeof(KEYS) / sizeof(KEYS[0]); k++) {
        size_t n = strlen(KEYS[k]);
        if (len - i < n || strncmp(line + i, KEYS[k], n)) continue;
        size_t j = i + n;
        while (j < len && (line[j] == ' ' || line[j] == '\t')) j++;
        if (j < len && line[j] == '=') return 1;
    }
    return 0;
}

int shim_sticks_places_save(void) {
    if (!g_path[0] || !g_loaded) return 0;
    static char text[32768], out[33280];
    size_t n = 0;
    FILE *f = shim_safe_fopen_read(g_path);
    if (f) {
        n = fread(text, 1, sizeof(text) - 1, f);
        int more = fgetc(f) != EOF;
        int bad = ferror(f);
        fclose(f);
        if (more) { LOG("sticks: %s is too large to rewrite, the places are not saved", g_path); return 0; }
        if (bad) { CSAY("the places NOT saved: port-touch.ini did not read back"); return 0; }
    } else if (errno != ENOENT) {
        CSAY("the places NOT saved: port-touch.ini did not open (errno %d)", errno);
        return 0;
    }
    text[n] = 0;

    /*
     * The places belong to the top level, so they go in before the first
     * section header: put at the very end they would land inside the mods'
     * last section and be read as its keys.
     */
    size_t k = 0;
    const char *sections = NULL;
    for (const char *p = text; *p; ) {
        const char *nl = strchr(p, '\n');
        size_t len = nl ? (size_t) (nl - p) + 1 : strlen(p);
        char name[64];
        if (section_header(p, name, sizeof(name))) { sections = p; break; }
        if (!line_is_place(p, len) && k + len < sizeof(out)) { memcpy(out + k, p, len); k += len; }
        p += len;
    }
    if (k && out[k - 1] != '\n' && k + 1 < sizeof(out)) out[k++] = '\n';
    int wrote = snprintf(out + k, sizeof(out) - k,
                         "movepos=%.4f,%.4f\n"
                         "shootpos=%.4f,%.4f\n"
                         "rowpos=%.4f,%.4f\n"
                         "twinpos=%.4f,%.4f\n"
                         "rowposloose=%.4f,%.4f\n"
                         "twinposloose=%.4f,%.4f\n"
                         "pausepos=%.4f,%.4f\n"
                         "trinketpos=%.4f,%.4f\n"
                         "touchalpha=%.2f\n",
                         (double) g_move_at[0], (double) g_move_at[1],
                         (double) g_shoot_at[0], (double) g_shoot_at[1],
                         (double) g_row_at[0], (double) g_row_at[1],
                         (double) g_twin_at[0], (double) g_twin_at[1],
                         (double) g_row_at_loose[0], (double) g_row_at_loose[1],
                         (double) g_twin_at_loose[0], (double) g_twin_at_loose[1],
                         (double) g_pause_at[0], (double) g_pause_at[1],
                         (double) g_trinket_at[0], (double) g_trinket_at[1],
                         (double) g_alpha);
    if (wrote < 0 || (size_t) wrote >= sizeof(out) - k) return 0;
    k += (size_t) wrote;
    if (sections) {                         /* the sections after, untouched */
        size_t rest = strlen(sections);
        if (k + rest + 1 >= sizeof(out)) return 0;
        out[k++] = '\n';
        memcpy(out + k, sections, rest);
        k += rest;
    }

    f = shim_safe_fopen(g_path, "w");
    if (!f) return 0;
    size_t put = fwrite(out, 1, k, f);
    int ok = shim_safe_fclose(f) == 0 && put == k;
    note_ini_time();
    LOG("sticks: the places are saved — %s%s", g_path, ok ? "" : " - FAILED");
    if (!ok) CSAY("the places NOT written whole - the old file stands");
    return ok;
}

/*
 * The parser is deliberately simple: "key=value" per line, a hash starts a
 * comment. The file may be missing — then we write it with the defaults so the
 * owner has something to edit.
 */
#if ISAAC_DIAG
static void sliders_read_note(void);                 /* the report's "sliders:", further down */
#endif

void shim_sticks_load(const char *dir) {
    if (!dir || !*dir) return;
    snprintf(g_path, sizeof(g_path), "%s/port-touch.ini", dir);
    {
        /*
         * The first time: the port's folder is put right before anything reads
         * it - writes that broke off are finished or dropped (shim.c), the
         * folder is asked how it takes a rename, and the report's guard looks
         * whether the files changed since our last write.
         */
        static int first = 1;
        if (first) {
            first = 0;
            void shim_safe_recover_dir(const char *);
            int shim_storage_probe(const char *);
            void shim_guard_check(const char *);
            char mods[600];
            snprintf(mods, sizeof(mods), "%s/mods", dir);
            shim_safe_recover_dir(dir);
            shim_safe_recover_dir(mods);
            shim_storage_probe(dir);
            shim_guard_check(g_path);
            snprintf(mods, sizeof(mods), "%s/mods/mods.txt", dir);
            shim_guard_check(mods);
        }
    }
    FILE *f = shim_safe_fopen_read(g_path);
    if (!f) {
        if (errno == ENOENT) {
            g_loaded = 1;
            CSAY("port-touch.ini is not there - the defaults written");
            write_default();
        } else {
            /* there, and not read: left as it is, read again in two seconds (between frames) */
            g_loaded = 0;
            CSAY("port-touch.ini NOT READ, errno %d - left alone, read again shortly", errno);
        }
        return;
    }
    char *line = NULL, section[64] = "";
    size_t line_cap = 0;
    while (getline(&line, &line_cap, f) > 0) {
        char name[64];
        if (section_header(line, name, sizeof(name))) {
            snprintf(section, sizeof(section), "%s", name);
            if (shim_modset_section(section)) g_mod_sections_seen = 1;
            continue;
        }
        char *h = strchr(line, '#');
        if (h) *h = 0;
        char *eq = strchr(line, '=');
        if (!eq) continue;
        *eq = 0;
        char *key = line, *val = eq + 1;
        while (*key == ' ' || *key == '\t') key++;
        for (char *e = key + strlen(key); e > key && (e[-1] == ' ' || e[-1] == '\t'); e--) e[-1] = 0;
        /* inside a section: a mod's setting (modset.c), never a key of the top level */
        if (section[0]) { shim_modset_parse(section, key, val); continue; }
        if (!strcmp(key, "mode"))
            g_kind = strstr(val, "floating") ? KIND_FLOATING
                   : (strstr(val, "dynamic") ? KIND_DYNAMIC : KIND_STATIC);
        else if (!strcmp(key, "visible")) g_visible = atoi(val) != 0;
        else if (!strcmp(key, "move")) sscanf(val, "%f,%f", &g_move[0], &g_move[1]);
        else if (!strcmp(key, "shoot")) sscanf(val, "%f,%f", &g_shoot[0], &g_shoot[1]);
        else if (!strcmp(key, "reach")) g_reach = (float) atof(val);
        else if (!strcmp(key, "sens")) g_sens = (float) atof(val);
        else if (!strcmp(key, "sensshoot")) g_sens_shoot = (float) atof(val);
        else if (!strcmp(key, "deadzone")) g_dead = (float) atof(val);
        else if (!strcmp(key, "scalefield")) g_scale_off = (unsigned) strtoul(val, 0, 16);
        else if (!strcmp(key, "size"))
            g_pct = strstr(val, "small") ? 100 : (strstr(val, "verybig") ? PCT_VERY_BIG
                                                : (strstr(val, "big") ? PCT_BIG : PCT_MEDIUM));
        else if (!strcmp(key, "sizepct")) g_pct = atoi(val);
        else if (!strcmp(key, "inset")) { g_inset_x = g_inset_y = atoi(val); }
        else if (!strcmp(key, "movepos")) sscanf(val, "%f,%f", &g_move_at[0], &g_move_at[1]);
        else if (!strcmp(key, "shootpos")) sscanf(val, "%f,%f", &g_shoot_at[0], &g_shoot_at[1]);
        else if (!strcmp(key, "rowpos")) sscanf(val, "%f,%f", &g_row_at[0], &g_row_at[1]);
        else if (!strcmp(key, "twinpos")) sscanf(val, "%f,%f", &g_twin_at[0], &g_twin_at[1]);
        else if (!strcmp(key, "rowposloose"))
            sscanf(val, "%f,%f", &g_row_at_loose[0], &g_row_at_loose[1]);
        else if (!strcmp(key, "twinposloose"))
            sscanf(val, "%f,%f", &g_twin_at_loose[0], &g_twin_at_loose[1]);
        else if (!strcmp(key, "pausepos")) sscanf(val, "%f,%f", &g_pause_at[0], &g_pause_at[1]);
        else if (!strcmp(key, "trinketpos")) sscanf(val, "%f,%f", &g_trinket_at[0], &g_trinket_at[1]);
        else if (!strcmp(key, "touchalpha")) g_alpha = (float) atof(val);
        else if (!strcmp(key, "insetx")) g_inset_x = atoi(val);
        else if (!strcmp(key, "insety")) g_inset_y = atoi(val);
        /*
         * The one switch that turns the widened canvas off, so a phone
         * that misbehaves can be asked the only question worth asking
         * first: is it the widening at all? With it off the game gets
         * the design 480 x 270 and black bars, exactly as it did before
         * any of this work - a fault that survives that is not ours.
         */
        else if (!strcmp(key, "wide")) shim_screen_wide(atoi(val) != 0);
        /*
         * HOW MUCH LARGER THAN THE CANVAS THE SCENE MAY BE DRAWN - a ceiling on
         * the rule, not a replacement for it.
         *
         * The rule already follows the screen: a 1080-tall phone gets 4, which
         * is EXACTLY one scene pixel to one screen pixel, and a 1440-tall one
         * gets 6. So nothing is being wasted on a budget phone - what costs
         * there is the screen's own resolution meeting a slow card. Measured on
         * two testers' phones (14.09.2026): the card's own clock shows 110 ms
         * of work in a frame holding 400 sprite draws, and the frame then sits
         * in eglSwapBuffers waiting for it.
         *
         * Lowering the ceiling therefore does not remove waste; it draws the
         * scene BELOW the screen and lets it be stretched up - cheaper by the
         * square of the step, softer by the same measure. On 1080: 3 gives
         * 1755 x 810 and 44 per cent fewer points, 2 gives a quarter of them,
         * 1 is what iOS did. Zero or absent leaves the rule alone.
         */
        else if (!strcmp(key, "rumble")) { void shim_rumble_set(int);
                                           shim_rumble_set(atoi(val) != 0); }
        else if (!strcmp(key, "rumblepower")) { void shim_rumble_power_set(int);
                                                shim_rumble_power_set(atoi(val)); }
        else if (!strcmp(key, "rumbletier")) { void shim_rumble_tier_set(const char *);
                                               shim_rumble_tier_set(val); }
        else if (!strcmp(key, "spawnerlock")) { void shim_spawner_lock(int);
                                                shim_spawner_lock(atoi(val) != 0); }
        else if (!strcmp(key, "spawnerground")) { void shim_spawner_ground_set(int);
                                                  shim_spawner_ground_set(atoi(val) != 0); }
        else if (!strcmp(key, "spawnerheld")) { void shim_spawner_held_set(int);
                                                shim_spawner_held_set(atoi(val) != 0); }
        else if (!strcmp(key, "spawner")) { void shim_tweaks_set_spawner(int);
            shim_tweaks_set_spawner(atoi(val) != 0); }
        else if (!strcmp(key, "fps")) { void shim_tweaks_set_fps(int); shim_tweaks_set_fps(atoi(val) != 0); }
        else if (!strncmp(key, "mod", 3)) { int shim_tweaks_mod_index(const char *);
            void shim_tweaks_set_mod(int, int);
            int i = shim_tweaks_mod_index(key + 3);
            if (i >= 0) shim_tweaks_set_mod(i, atoi(val) != 0); }
        else if (!strcmp(key, "shotmax")) { void shim_screen_shot_ceiling(int);
                                            shim_screen_shot_ceiling(atoi(val)); }
        /* which font the item descriptions use, while the right one is settled */
        else if (!strcmp(key, "eidfont")) { void shim_eid_font(int); shim_eid_font(atoi(val)); }
        else if (!strcmp(key, "eidlang")) { void shim_eid_language_pin(const char *);
                                            shim_eid_language_pin(val); }
        else if (!strcmp(key, "eidall")) { void shim_eid_conditions_english(int);
                                           shim_eid_conditions_english(atoi(val) != 0); }
        else if (!strcmp(key, "eidglitch")) { void shim_eid_glitch_info(int);
                                              shim_eid_glitch_info(atoi(val) != 0); }
        else if (!strcmp(key, "audiostress")) { void shim_audio_stress_allow(int);   /* audio.c, report builds */
                                                shim_audio_stress_allow(atoi(val) != 0); }
        else if (!strcmp(key, "probe"))
            g_probe = strstr(val, "kind") ? 1 : (strstr(val, "flag") ? 2 :
                       (strstr(val, "hide") ? 3 : (strstr(val, "mark") ? 4 : 0)));
    }
    int read_bad = ferror(f);
    free(line);
    fclose(f);
    if (read_bad) {
        g_loaded = 0;
        CSAY("port-touch.ini NOT READ to its end - left alone, read again shortly");
        return;
    }
    g_loaded = 1;
    {
        static int said;
        struct stat st;
        if (said < 4 && stat(g_path, &st) == 0) {
            said++;
            CSAY("read %s, %lld bytes, %s", g_path, (long long) st.st_size,
                 st.st_uid == getuid() ? "ours" : "a FOREIGN owner");
        }
        (void) said;
    }
    {
        /*
         * The three numbers of the stick's curve into the ranges the Tweaks page
         * has (tweaks.c): a zone typed as 1 by hand would leave the movement
         * stick nothing but its middle.
         */
        void shim_sticks_set_deadzone(float), shim_sticks_set_sens(float), shim_sticks_set_sens_shoot(float);
        shim_sticks_set_deadzone(isfinite(g_dead) ? g_dead : 0.25f);
        shim_sticks_set_sens(isfinite(g_sens) ? g_sens : 0.8f);
        shim_sticks_set_sens_shoot(isfinite(g_sens_shoot) ? g_sens_shoot : 0.4f);
        /* the opacity onto the slider's steps, and into the image if it is there already */
        shim_sticks_alpha_set(g_alpha);
    }
#if ISAAC_DIAG
    sliders_read_note();
#endif
    clamp_place(g_move);
    clamp_place(g_shoot);
    LOG("sticks: size %d%%", g_pct);
    LOG("sticks: %s, visible %d, movement %.0f,%.0f, shooting %.0f,%.0f",
        kind_name(g_kind), g_visible,
        (double) g_move[0], (double) g_move[1], (double) g_shoot[0], (double) g_shoot[1]);
    /*
     * A file from a build before the mods' sections gets them written in once,
     * with the defaults, so they are there to be edited. Everything else is
     * written back as it was just read.
     */
    if (!g_mod_sections_seen) write_default();
    note_ini_time();
}

/*
 * THE RING AS IT IS SEEN: the outer circle, and the boundary everything else
 * keeps out of.
 *
 * "Analog Bounds" is a 128-pixel frame and the picture in it is an annulus -
 * empty in the middle, brightest at the rim (the alpha rises from nothing at a
 * quarter of the radius to its fullest at the frame's edge). Drawn at a half,
 * with the variant file's own scale on top, its rim is 32 * pct/100 out from
 * the middle. Measured on the owner's screenshots, run and editor laid over
 * each other at 125%: the glow reaches about 37 units and the frame's edge is
 * 40 (13.09.2026).
 */
static float ring_radius(void) { return 32.0f * (float) g_pct / 100.0f; }

/*
 * THE KNOB, which is what the button row is measured from. The dark disc under
 * the thumb - "Analog Stick" - comes out 16 * pct/100 of radius; measured at
 * 125%: about 16-17 units against this 20, the picture not quite filling its
 * frame. The row hangs a gap from THIS, not from the outer circle, because that
 * is where the owner wanted it once he saw both.
 */
static float ring_drawn_radius(void) { return 16.0f * (float) g_pct / 100.0f; }

/*
 * A seat, against the canvas the game is actually laying out on.
 *
 * The two places in the settings are written in canvas units, and they were
 * written when the canvas was always 480 wide (SEAT_REF). The canvas is now as
 * wide as the screen (screen.c), and an absolute number no longer means the
 * same thing: the shooting stick sat 75 units in from the RIGHT edge, and on a
 * 585-unit canvas the same 405 leaves it 180 units in - adrift towards the
 * middle, which is exactly what the owner saw.
 *
 * So each stick keeps its distance from its OWN edge: the movement stick from
 * the left, the shooting stick from the right. On a 480 canvas this is an
 * identity (480 - 480 + x = x), so nothing changes on a 16:9 screen.
 */
#define SEAT_REF 480.0f            /* the canvas the settings were written for */

/*
 * A stick's seat. With `home` the hand-placed position is passed over and the
 * answer is where the port would seat the stick by itself - which is what the
 * button card's own default is measured from, so that dragging a stick in the
 * editor no longer drags the card behind it (the owner, 13.09.2026).
 */
static void seat_pick(int is_move, int home, float *out) {
    const float *at = is_move ? g_move_at : g_shoot_at;
    if (!home && placed(at)) {
        /* a place of its own: a share of the canvas, and nothing else applies */
        out[0] = at[0] * FIELD_W;
        out[1] = at[1] * FIELD_H;
        return;
    }
    const float *src = is_move ? g_move : g_shoot;
    out[0] = src[0];
    out[1] = src[1];
    /*
     * Only the MANUAL places need re-anchoring. When an inset is set,
     * place_by_inset has already measured from the screen's own edges and its
     * answer is in these very fields - re-anchoring on top of that moved the
     * shooting stick a second time, right off the screen.
     */
    if (is_move || g_inset_x > 0 || g_inset_y > 0) return;
    float w = FIELD_W;
    if (w > SEAT_REF) out[0] = w - (SEAT_REF - src[0]);
}

static void seat_for(int is_move, float *out) { seat_pick(is_move, 0, out); }

/* stick places from the screen inset; with inset<=0 the manual places are used */
/*
 * The inset is a share of the CANVAS, not of the window. The distinction did
 * not exist while the canvas was always 480 units fitted into the middle of the
 * screen - the two agreed to the pixel. Now the canvas is as wide as the screen
 * (screen.c), and measuring from the window left the sticks at the same PIXELS
 * while everything drawn around them moved out to the true edges: the same 12%
 * that used to sit on the black bar now sits well inside the picture. Measured
 * from the canvas, a percentage means the same place relative to what is drawn,
 * on any screen and at any aspect.
 */
static void place_by_inset(float *move, float *shoot) {
    if (g_inset_x <= 0 && g_inset_y <= 0) return;
    float R = ring_radius();
    float gx = (float) g_inset_x / 100.0f, gy = (float) g_inset_y / 100.0f;
    move[0]  = gx * FIELD_W + R;
    shoot[0] = (1.0f - gx) * FIELD_W - R;
    move[1] = shoot[1] = (1.0f - gy) * FIELD_H - R;
    clamp_place(move);
    clamp_place(shoot);
}

/*
 * Where the shooting stick's ring ENDS, on the left, in canvas units.
 *
 * The row of touch buttons - bomb, card and pill, the actives - is anchored to
 * this (hud.c), so the block sits beside the stick instead of in the middle of
 * the screen. The ring's radius is the whole point of asking: it is 32 units at
 * the small size and half again at the big one, so an anchor taken from the
 * stick's CENTRE would leave a different gap at each of the three sizes, and an
 * anchor taken from its edge leaves the same one.
 *
 * The seat is worked out exactly as the drawing works it out - the inset first,
 * then the re-anchoring to the canvas - so the two cannot drift apart. For the
 * dynamic and floating sticks, which travel under the finger, this is their
 * resting place; there is no other sensible thing for the buttons to follow.
 */
/*
 * Where the shooting stick's ring begins IF NOBODY HAD MOVED IT. Only the card's
 * default asks this, and it must not follow the stick around: the card has a
 * place of its own the moment the editor gives it one.
 */
int shim_sticks_shoot_edge(float *left) {
    if (!left) return 0;
    place_by_inset(g_move, g_shoot);
    float seat[2];
    seat_pick(0, 1, seat);
    float e = seat[0] - ring_drawn_radius();
    if (!(e > 0.0f) || e > FIELD_W) return 0;
    *left = e;
    return 1;
}

/* the editor (touchedit.c): where the port itself would seat a stick */
int shim_sticks_seat_home(int is_move, float *x, float *y) {
    place_by_inset(g_move, g_shoot);
    float seat[2];
    seat_pick(is_move, 1, seat);
    if (x) *x = seat[0];
    if (y) *y = seat[1];
    return 1;
}

/* the editor (touchedit.c): where a stick sits now, in canvas units, and how big its ring is */
int shim_sticks_seat_at(int is_move, float *x, float *y) {
    place_by_inset(g_move, g_shoot);
    float seat[2];
    seat_for(is_move, seat);
    if (x) *x = seat[0];
    if (y) *y = seat[1];
    return 1;
}

/*
 * THE STICK AS THE GAME KEEPS IT: the fields its own grab (0x861b40), update
 * (0x862f04) and draw (0x861ea0) use.
 */
int shim_report_active(void);
void report_line(const char *kind, const char *text);

/* the two sticks, met by their own update; the array itself is further down */
static unsigned long g_seen[2];

#define STICK_LIVE   0x00c          /* byte: the stick takes touches in this state (0x861b5c) */
#define OVERLAY_LIVE 0x684          /* byte of the overlay: it is up at all (0x8603d8, hud.c) */
#define STICK_HELD   0x168          /* byte: this stick has a finger (0x862fcc) */
#define STICK_TOUCH  0x170          /* the UITouch it holds: the touch record's first word (0x861bb4;
                                       DeviceiOS puts the object there, 0x8b3a8c) */
#define STICK_FINGER 0x178          /* Vector2: the finger now, in the window's units (0x862fe8) */
#define STICK_TAKEN  0x180          /* Vector2: where it was taken (0x861bc4) - the seat, by fixed_sticks */
#define STICK_VALUE  0x194          /* Vector2: what the stick gives, after the game's own rules */
#define STICK_KNOB_OFF 0x1a4        /* Vector2: the knob as drawn is the seat plus this (0x8620ac) */
#define STICK_KNOB_T 0x1b4          /* float: the knob's animation, +0.1 an update up to one (0x863188) */
#define STICK_SEAT   0x1b8
#define STICK_TRAVEL 0x1c0
#define STICK_DIV    0x1c4          /* travel x MOVE SENS (shim_sticks_pin) */
#define STICK_RING_SCALE 0x2ac      /* the ring sprite's scale, two floats (0x861f44) */
#define OVERLAY_IN_MANAGER 0x1f6d18
#define STICK_MOVE   0x10
#define STICK_SHOOT  0x340
#define TOUCH_MODE_FIELD 0x55f18
#define PLAYER_DEVICE     0x19ec
#define PLAYER_MOVE_LAST  0x25bc
#define PLAYER_MOVE_AGO   0x25c0
#define PLAYER_MARS_COOL  0x25b8
#define PLAYER_SHOOT_LAST 0x26e8
#define PLAYER_SHOOT_AGO  0x26ec
#define PLAYERS_MAX       8

static int buttons_mode(void);
static int in_game(void);

/* frames, counted between them: tells an update that goes on from a new hold */
static unsigned g_frame = 1;

static unsigned long manager_now(void) {
    unsigned long b = shim_image_base();
    return b ? *(const unsigned long *) (b + 0xac2428) : 0;
}

/* 0 the movement stick, 1 the shooting one, -1 anything else - by where the overlay keeps them */
static int stick_index(unsigned long stick) {
    unsigned long m = manager_now();
    if (!m) return -1;
    unsigned long overlay = m + OVERLAY_IN_MANAGER;
    return stick == overlay + STICK_MOVE ? 0 : (stick == overlay + STICK_SHOOT ? 1 : -1);
}

static unsigned long stick_at(int i) {
    unsigned long m = manager_now();
    return m ? m + OVERLAY_IN_MANAGER + (i ? STICK_SHOOT : STICK_MOVE) : 0;
}

static int touch_mode(void) {
    unsigned long m = manager_now();
    return m ? *(const int *) (m + TOUCH_MODE_FIELD) : 0;
}

/*
 * THE PLAYER THE TOUCH SCREEN DRIVES: the first whose device (+0x19ec) is the
 * screen's, by its class (devices.c). Alone, that is player 0; in co-op it is
 * whoever plays on the glass, and a pad's player never opens the touch sticks'
 * double tap. The game's thread only.
 */
static unsigned long touch_player(void) {
    unsigned long p[PLAYERS_MAX];
    int n = shim_eid_players(p, PLAYERS_MAX);
    for (int i = 0; i < n; i++) {
        if (!p[i] || !shim_eid_readable((const void *) (p[i] + PLAYER_DEVICE),
                                        PLAYER_SHOOT_AGO + 4 - PLAYER_DEVICE)) continue;
        if (shim_device_kind(*(const int *) (p[i] + PLAYER_DEVICE)) == DEVICE_SCREEN) return p[i];
    }
    return 0;
}

/* the port's double tap is open for the touch player's movement (preset_detector, below) */
static int move_sanctioned(void) {
    unsigned long p = touch_player();
    return p && *(const int *) (p + PLAYER_MOVE_AGO) < 0;
}

static long long now_ms(void) {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (long long) t.tv_sec * 1000 + t.tv_nsec / 1000000;
}

static struct {
    unsigned value_frame;           /* the last update that asked, in g_frame */
    int on;                         /* DIGITAL's switch (stickcurve.h) */
    long long off_ms;               /* when it last went off */
    int ons, bounces;               /* DIGITAL in this hold: switched on, and on within 100 ms of off */
    float m;                        /* the last pull, a share of the travel */
    unsigned long value_calls, knob_calls;
#if ISAAC_DIAG
    unsigned knob_frame, since_press;
    int have_knob;
    float knob[2];                  /* what the knob was last told */
    unsigned long knob_full, knob_apart, knob_past, knob_spring;
#endif
} g_st[2];

/*
 * THE STICK'S OUTPUT (plan item 4.6, 5.1b): the division at 0x863048,
 * `bl 0x8bedbc` -> `bl 0x8dab78` (loader.c, stick_hooks). d is the deflection
 * the update has just worked out (its sp+0x10: finger less grab), div the
 * stick's +0x1c4; the answer goes back in s0/s1, where 0x8bedbc left its
 * quotient, and the game's cap, DIGITAL rule and axis zone follow as ever.
 * One curve for all three kinds of stick: the middle is the grab the game
 * made - the seat of a static stick (fixed_sticks), the landing of a dynamic
 * one, the point a floating one drags after the thumb (0x86300c).
 *
 * The zone is lifted while the port's double tap is open for the touch player
 * (+0x25c0 < 0): the owner, 23.09.2026, "the dead zone must not reach Mars at
 * all". The pull is then what it is with the zone at 0 - the game's division in
 * ANALOGUE, any pull in DIGITAL - for the half second the sanction lasts.
 *
 * The game's thread, inside the stick's update.
 */
stick_v2 shim_stick_value(const float *d, const float *div) {
    unsigned long stick = (unsigned long) div - STICK_DIV;
    int i = stick_index(stick);
    if (i < 0) return stick_native(d, *div);
    if (g_st[i].value_frame != g_frame && g_st[i].value_frame + 1 != g_frame) {
        g_st[i].on = 0;                         /* a new hold: DIGITAL starts off */
        g_st[i].off_ms = 0;
    }
    g_st[i].value_frame = g_frame;
    g_st[i].value_calls++;
    int mode = touch_mode();
    float travel = *(const float *) (stick + STICK_TRAVEL);
    float dz = g_dead;
    if (i == 0 && dz > 0.0f && mode != STICK_BUTTONS && move_sanctioned()) dz = 0.0f;
    int was = g_st[i].on;
    stick_v2 r = stick_output(d, *div, travel, dz, mode, i == 0, &g_st[i].on);
    if (g_st[i].on != was) {
        long long t = now_ms();
        if (g_st[i].on) {
            g_st[i].ons++;
            if (g_st[i].off_ms && t - g_st[i].off_ms < 100) g_st[i].bounces++;
        } else {
            g_st[i].off_ms = t;
        }
    }
    g_st[i].m = travel > 0.0f ? sqrtf(d[0] * d[0] + d[1] * d[1]) / travel : 0.0f;
    return r;
}

/*
 * THE KNOB FOLLOWS THE FINGER (plan item 4.6, 5.1v; the owner's decision 18,
 * the 13.09 principle carried to the end): 0x863388, `bl 0x8beb28` ->
 * `bl 0x8dab68` (loader.c, stick_hooks). The game gives its knob the target
 * value x travel (+0x1ac) and its own animation and spring (0x863188..
 * 0x86326c) take the picture there. The target is now the finger itself, less
 * the grab and cut at the travel: one owner for the knob in every kind and
 * mode, the zone shown by the knob moving while the character stands, and the
 * spring left to real lifts only - there are no false ones any more.
 *
 * It was "beautiful" at DEAD ZONE 0 and MOVE SENS 0.55 because the game drew
 * the knob then, 1 / 0.55 = 1.8 times faster than the thumb; tools/stickmodel.py
 * draws that against this. BUTTONS keeps the game's own: the knob is hidden
 * there (dpad_dress). Both sticks alike - the shooting stick's knob comes to
 * the finger on its own spring at the press (+0x1c9 = 0, 0x861be8), which is
 * the game's look and stays.
 *
 * Arguments as the call site has them: x0 = +0x194 (the value), x1 = +0x1c0
 * (the travel) on every road into 0x863380 (x20, x21 from 0x863050, 0x86307c).
 */
stick_v2 shim_stick_knob(const float *value, const float *travel) {
    float t = *travel;
    stick_v2 r = { value[0] * t, t * value[1] };         /* 0x8beb28, to the bit */
    unsigned long stick = (unsigned long) travel - STICK_TRAVEL;
    int i = stick_index(stick);
    if (i < 0) return r;
    g_st[i].knob_calls++;
    if (buttons_mode() || !(t > 0.0f)) return r;
    const float *finger = (const float *) (stick + STICK_FINGER);
    const float *grab = (const float *) (stick + STICK_TAKEN);
    float dx = finger[0] - grab[0], dy = finger[1] - grab[1];
    if (!isfinite(dx) || !isfinite(dy)) return r;
    float len = sqrtf(dx * dx + dy * dy);
    if (len > t) { float k = t / len; dx *= k; dy *= k; }
    /*
     * THE KNOB STOPS AT THE RING, on every screen and in every mode (the
     * owner, 01.10.2026). The travel is in the stick pass's units, the
     * window's, while the ring and the knob are drawn at the sprite's own
     * scale per axis, area / window with FULLSCREEN off (0x861f44). With the
     * two equal - FULLSCREEN on - a full pull puts the knob's edge exactly on
     * the ring: the ring is 64 texels, the knob's disc 32, both times the
     * stick's size (capture_watch: "Analog Bounds", "Analog Stick"), so the
     * room between them is 32 texels x size x scale. With FULLSCREEN off the
     * scale shrinks on one axis and the knob went past the ring by 24 px
     * across on a 20:9 phone (28 px down on 4:3). So the reach on each axis is
     * that room at that axis's scale, never more than the travel: the knob is
     * held inside an ellipse that is the ring's own, whatever the size, the
     * `reach` key or the screen. Only the picture: what the stick GIVES is
     * shim_stick_value's business.
     */
    {
        const float *scale = (const float *) (stick + STICK_RING_SCALE);
        float room = 32.0f * (float) g_pct / 100.0f;
        float ax = room * scale[0], ay = room * scale[1];
        if (ax > 0.05f && ay > 0.05f && isfinite(ax) && isfinite(ay)) {
            if (ax > t) ax = t;
            if (ay > t) ay = t;
            float e = (dx * dx) / (ax * ax) + (dy * dy) / (ay * ay);
            if (e > 1.0f) { float k = 1.0f / sqrtf(e); dx *= k; dy *= k; }
        }
    }
#if ISAAC_DIAG
    {
        /*
         * The knob's three counters (the line "knob:"). What is drawn now was
         * worked out in the last update, from what we told it then - so at
         * t = 1 it must be that, and never past the travel; a t under one with
         * no press in the last ten updates is a spring in the middle of a hold,
         * which only a lift the finger did not make could start.
         */
        int fresh = g_st[i].knob_frame != g_frame && g_st[i].knob_frame + 1 != g_frame;
        g_st[i].knob_frame = g_frame;
        if (fresh) { g_st[i].since_press = 0; g_st[i].have_knob = 0; }
        else g_st[i].since_press++;
        float tt = *(const float *) (stick + STICK_KNOB_T);
        const float *off = (const float *) (stick + STICK_KNOB_OFF);
        if (tt >= 1.0f) {
            g_st[i].knob_full++;
            if (g_st[i].have_knob && hypotf(off[0] - g_st[i].knob[0], off[1] - g_st[i].knob[1]) > 0.05f)
                g_st[i].knob_apart++;
            if (hypotf(off[0], off[1]) > t + 0.01f) g_st[i].knob_past++;
        } else if (g_st[i].since_press > 10) {
            g_st[i].knob_spring++;
        }
        g_st[i].knob[0] = dx;
        g_st[i].knob[1] = dy;
        g_st[i].have_knob = 1;
    }
#endif
    r.x = dx;
    r.y = dy;
    return r;
}

/*
 * THE PORT'S DOUBLE TAP (the owner, 23.09 and 27.09.2026: "only our
 * behaviour; the native one off"). The game's detectors (0x516740 for moving,
 * 0x5185a8 for shooting) fire only on a press the port has sanctioned
 * (double_tap_gate, loader.c): preset_detector below writes the direction and
 * a negative count, and the next press that way within half a second is the
 * double tap. The gestures that sanction it: two taps to a side of a static
 * stick or D-pad (static_tap), a tap, then a tap and a flick on a moving one
 * (shim_sticks_tap_watch). Nothing is pushed on anybody's behalf.
 *
 * The finger is never held back any more (the dead zone is on the stick's
 * output, shim_stick_value), so there are no synthetic presses, no pulses of
 * our own and no "way the stick last walked": a tap inside the zone is a tap
 * the game sees, and the zone is lifted while the sanction is open. Everything
 * behaves as with the zone at 0, as the owner asked for Mars.
 */
#define TAP_MS          300
#define PAIR_MS         450

static const char *const DIR_NAME[4] = {"right", "down", "left", "up"};

static int cardinal(float dx, float dy) {
    if (fabsf(dx) >= fabsf(dy)) return dx >= 0.0f ? 0 : 2;
    return dy >= 0.0f ? 1 : 3;
}

static void dtap_say(const char *line) {
    if (!shim_report_active()) return;
    static int said;
    if (said >= 60) return;
    said++;
    report_line("doubletap", line);
}

/*
 * THE FINGERS' SPACE FOR THE STICKS AND D-PADS IS THE WINDOW (the owner,
 * 28.09.2026: "with FULLSCREEN off the D-pads' touch is cut off at the
 * picture"). A stick maps its finger itself, by plain proportion against the
 * WINDOW (0x861a20, the Stick vtable's +0x10: the area origin added back, then
 * divided by the window), and the sticks are drawn with the viewport on the
 * whole window (0x8a7208 before 0x861e78). The game's Buttons and ItemButtons
 * are the other space: 0x86255c maps against the PICTURE, the area 0xabec64
 * (screen.c, "ONE FINGER, TWO SPACES"). So every finger here that is a stick's
 * or a D-pad's is mapped against the window: its size and a zero origin, in the
 * shape the area had (w, h, x, y); anything compared with the game's widgets
 * is put into the picture first (on_overlay_widget). With FULLSCREEN on the
 * two are one and nothing changes.
 */
static const unsigned *finger_space(unsigned long b, unsigned out[4]) {
    const unsigned *win = (const unsigned *) (b + 0xa9aff8);       /* u32 w, h (screen.c WINDOW) */
    const unsigned *area = (const unsigned *) (b + 0xabec64);
    out[0] = win[0] ? win[0] : area[0];
    out[1] = win[1] ? win[1] : area[1];
    out[2] = win[0] ? 0u : area[2];
    out[3] = win[1] ? 0u : area[3];
    return out;
}

/*
 * THE STICKS' OWN WORD ON WHETHER THEY TAKE TOUCHES AT ALL (the owner's
 * eighth and eleventh rounds, 24-25.09.2026: scrolling the settings stiff, the
 * options menu's BACK not pressed). "In a run" is the manager's state, and it
 * stays RUNNING under the pause menu and its options; the game's stick takes
 * nothing unless the overlay and the stick are live (0x8603d8, 0x861b5c) - the
 * overlay's +0x684 measured 1 in the run and 0 under the options, the stick's
 * +0xc is what its own reach test asks. The D-pads ask the same (shim_sticks_dpad).
 */
static int stick_taking(unsigned long s) {
    if (!shim_sticks_in_game()) return 0;
    unsigned long shim_pocket_overlay_at(void);
    unsigned long overlay = shim_pocket_overlay_at();
    return (!overlay || *(const unsigned char *) (overlay + OVERLAY_LIVE))
        && *(const unsigned char *) (s + STICK_LIVE);
}

/*
 * THE MOVING STICKS' DOUBLE TAP: A TAP, THEN A FLICK (the owner's eighth round,
 * 24.09.2026). The detectors ask, every player update, whether a direction's
 * action was pressed this update (0x671c6c), in the order left, up, right,
 * down (tables 0x952c10: 0 2 1 3, and 0x952c20: 4 6 5 7). A press in the same
 * direction as the last one (player +0x25bc, shooting +0x26e8) while the count
 * (+0x25c0, +0x26ec) is still below nought is the double tap: Mars dashes (and
 * sets its cooldown, +0x25b8, to 90), the shooting items do theirs.
 *
 * On a moving stick the gesture is the owner's: a plain tap first, then a
 * second touch that flicks the way it means. The first tap arms the stick for
 * half a second. When the flick of the second has gone a little way - before
 * the stick's own pull reaches the game's half - the port writes into the
 * detector "the last press was this way, just now". The flick then reaches the
 * game as the real press it is.
 *
 * Whose finger it is, is the game's own answer: the stick keeps the touch it
 * took at +0x170 (0x861bb4), the very object uikit.c hands the game for that
 * finger (shim_touch_object; DeviceiOS stores it as the record's first word,
 * 0x8b3a8c). The landing used to be compared with the grab instead, within a
 * unit - which missed with FULLSCREEN off, where the two are mapped through
 * different spaces, and the dead zone had to be asked about its own fingers.
 *
 * All of it runs as the event is handed to the game (uikit.c,
 * shim_touch_deliver): the game's thread, the object already bound to the
 * finger, and the detector written BEFORE the game sees the flick - the
 * player's fields are written from the game's thread only.
 */
#define WATCHED 10
#define ARM_MS            500
#define SANCTION_FRAMES   30            /* half a second for the sanctioned press to come */

static struct { int used, id, stick, armed, preset; long long t; float land[2], far; } g_watch[WATCHED];
static long long g_armed_at[2];             /* when each stick's arming tap came up */
static unsigned long g_dash_player;         /* measurement: did the game dash after a preset */
static int g_dash_frames;

/* the detector's own index for a cardinal direction (right, down, left, up) */
static int detector_index(int dir) {
    static const int IDX[4] = {2, 3, 0, 1};          /* left 0, up 1, right 2, down 3 */
    return IDX[dir & 3];
}

static void preset_detector(int stick, int dir) {
    unsigned long pl = touch_player();
    if (!pl) return;
    int *last = (int *) (pl + (stick ? PLAYER_SHOOT_LAST : PLAYER_MOVE_LAST));
    int *ago = (int *) (pl + (stick ? PLAYER_SHOOT_AGO : PLAYER_MOVE_AGO));
    *last = detector_index(dir);
    /*
     * A NEGATIVE count is the sanction (double_tap_gate, loader.c): the game's
     * window is shut at nought, and the count climbs one a frame, so the next
     * press this way fires for the next SANCTION_FRAMES frames and no other
     * press ever does.
     */
    *ago = -SANCTION_FRAMES;
    if (!stick) { g_dash_player = pl; g_dash_frames = 90; }
    char line[120];
    snprintf(line, sizeof(line), "tap then flick %s on the %s stick: the detector is told",
             DIR_NAME[dir & 3], stick ? "shooting" : "movement");
    dtap_say(line);
}

/* between frames: whether Mars answered (its cooldown goes up when it dashes) */
static void dash_watch(void) {
    if (!g_dash_frames || !g_dash_player) return;
    g_dash_frames--;
    if (!shim_eid_readable((const void *) (g_dash_player + PLAYER_MARS_COOL), 4)) { g_dash_frames = 0; return; }
    if (*(const int *) (g_dash_player + PLAYER_MARS_COOL) > 0) {
        dtap_say("the game dashed");
        g_dash_frames = 0;
    } else if (!g_dash_frames) {
        dtap_say("no dash within 90 frames (no Mars, or the flick never made a press)");
    }
}

/*
 * A TAP WITHOUT A DIRECTION LETS GO OF THE AIM LOCK (the owner, 05.10.2026:
 * "we need a way to switch the aim lock off somehow - it is very awkward
 * otherwise"; the research notes of 05.10.2026, aim lock release).
 *
 * The lock is the game's and lives on the touch shooting stick alone: with
 * the overlay's flag +0x1c9 at 0 a lifted finger leaves the value +0x194 and
 * the knob's target +0x1ac as they were (0x863140 -> 0x863188), and the
 * player goes on firing. The byte is read nowhere else (0x86077c); a pad and
 * the keyboard are devices of their own and fire only while held.
 *
 * The game already lets go when a tap carries no direction: the grab zeroes
 * the value (0x861bec), the update sets it by the finger, and the player
 * drops a firing input under 0.5 (0x516c1c). A D-pad's middle is the centre
 * itself, so there it is exact; on a stick the "no fire" circle is half the
 * pull, a fifth of the travel at SENS SHOOT 0.40 - a hair's breadth, and the
 * knob stays off the centre. So the port gives the middle a size a thumb can
 * find (shoot_middle: the whole DEAD ZONE, never under 48 dp across) and lets
 * go the game's own way: a short touch that never left the middle - around
 * the centre on a static stick or D-pad, around where it landed on a dynamic
 * or floating one - and came up while the lock held. A D-pad is judged by the
 * finger itself, not by the point it puts the finger on (dpad_raw_far).
 *
 * A double tap to a side is NOT used: it is the port's way into the game's
 * shooting double tap (preset_detector) for Hemoptysis, GB Bug and Divine
 * Intervention (0x5185a8: items 726, 405, 568), and a side tap re-aims.
 * Nothing those gestures need is in the middle. Mars is the movement stick's.
 *
 * Asked at the lift (shim_sticks_tap_watch, the game's thread, before the
 * game has the lift), done between frames once the game has taken the finger
 * off the stick (+0x168 down): exactly what the update does for a free stick
 * at a lift (0x863158..0x863184) - the knob springs home from where it was,
 * the value and the target go to nought. A finger that took the stick again
 * first keeps it; a weapon that is free (+0x1c9 = 1) was let go by the game.
 */
#define STICK_KNOB_FROM   0x19c          /* Vector2: the knob's spring starts here (0x86315c) */
#define STICK_KNOB_TO     0x1ac          /* Vector2: and ends here, the target */
#define STICK_FREE_FLAG   0x1c9          /* byte: 0 is the lock (0x860854) */
#define LETGO_FRAMES      4              /* frames to wait for the game to take the lift */

static struct { int frames; const void *touch; const char *how; } g_letgo;

/*
 * A static or dynamic D-pad hands the game - and the watch below - its finger
 * already put on one of its nine points, so a thumb a little off the middle
 * reaches the watch as a full push on an arm. Where the finger really was is
 * kept here as the D-pad maps it (shim_sticks_dpad, the input thread; read on
 * the game's thread after that finger's lift has come through the queue).
 * A floating D-pad's finger reaches the watch raw and needs none of it.
 */
static volatile int g_dpad_raw_id = -1;
static volatile float g_dpad_raw_far;

/* the shooting D-pad's finger `id`, farthest from its middle in field units; 0 if not its */
static int dpad_raw_far(int id, float *far) {
    if (g_dpad_raw_id != id || !buttons_mode()) return 0;
    *far = g_dpad_raw_far;
    return 1;
}

/*
 * THE MIDDLE, IN FIELD UNITS (the owner, 05.10.2026: "the dead zone counts
 * too, or the player will not find where that centre is"). The whole DEAD
 * ZONE is the middle, and never less than a thumb can find: a circle of
 * MIDDLE_DP across - Android's own smallest touch target, 48 dp, about 7.6 mm
 * on any screen - turned into field units through the window (a point is a
 * pixel here, uikit.c). A DEAD ZONE of 0 still has that circle. It stops at
 * one travel, so a tap farther out always aims: the stick gives its full pull
 * at SENS SHOOT x travel, and the capture reaches three travels (0x861b98).
 * On the owner's phone (3120 px across 585 units, density about 3.5, size 125)
 * the zone alone was 5 units, 1.4 mm; the floor makes it about 16 of 20.
 */
#define MIDDLE_DP        48.0f
#define MIDDLE_DENSITY   2.75f           /* dp scale if Java has not said (a common phone) */

float shim_screen_density(void);         /* uikit.c */

static float shoot_middle(float travel) {
    float share = g_dead > 0.0f ? g_dead : 0.0f;
    float r = share * travel;
    unsigned long b = shim_image_base();
    unsigned area_buf[4];
    const unsigned *area = b ? finger_space(b, area_buf) : NULL;
    float fw = FIELD_W;
    if (area && area[0] && fw > 0.0f) {
        float dp = shim_screen_density();
        if (!(dp > 0.3f)) dp = MIDDLE_DENSITY;
        float floor_r = 0.5f * MIDDLE_DP * dp * fw / (float) area[0];
        if (r < floor_r) r = floor_r;
    } else if (r < 0.5f * g_sens_shoot * travel) {
        r = 0.5f * g_sens_shoot * travel;           /* no window yet: the game's own half pull */
    }
    if (r > travel) r = travel;
    return r;
}

/* a lift of a tap without a direction: the lock lets go if this finger was the shooting stick's */
static void letgo_ask(int id, const char *how) {
    unsigned long s = stick_at(1);
    if (!s || !g_readable || !g_readable((const void *) s, STICK_FREE_FLAG + 1)) return;
    const unsigned char *o = (const unsigned char *) s;
    const void *touch = shim_touch_object(id);
    if (!touch || !o[STICK_HELD] || *(const void *const *) (s + STICK_TOUCH) != touch) return;
    if (o[STICK_FREE_FLAG]) return;                     /* no lock: the game lets go by itself */
    g_letgo.frames = LETGO_FRAMES;
    g_letgo.touch = touch;
    g_letgo.how = how;
}

#if ISAAC_DIAG
static void letgo_say(const char *line) {
    static int said;
    if (said >= 40 || !shim_report_active()) return;
    said++;
    report_line("aimlock", line);
}
#endif

/* between frames, the game's thread */
static void letgo_frame(void) {
    if (!g_letgo.frames) return;
    unsigned long s = stick_at(1);
    if (!s || !g_readable || !g_readable((const void *) s, STICK_FREE_FLAG + 1)) { g_letgo.frames = 0; return; }
    unsigned char *o = (unsigned char *) s;
    if (o[STICK_HELD]) {
        if (*(const void *const *) (s + STICK_TOUCH) == g_letgo.touch && --g_letgo.frames) return;
        g_letgo.frames = 0;
        DIAG(letgo_say("a tap without a direction: the stick was taken again first - the lock holds"));
        return;
    }
    g_letgo.frames = 0;
    float *value = (float *) (s + STICK_VALUE);
    float *from = (float *) (s + STICK_KNOB_FROM);
    float *to = (float *) (s + STICK_KNOB_TO);
    if (o[STICK_FREE_FLAG] || (value[0] == 0.0f && value[1] == 0.0f)) return;
#if ISAAC_DIAG
    char line[200];
    snprintf(line, sizeof(line), "a tap without a direction (%s) let go of the fire: value %.2f,%.2f -> 0",
             g_letgo.how, (double) value[0], (double) value[1]);
#endif
    from[0] = to[0]; from[1] = to[1];                   /* 0x863158..0x863160 */
    *(float *) (s + STICK_KNOB_T) = 0.0f;               /* 0x863164 */
    value[0] = value[1] = 0.0f;                         /* 0x863168..0x863178 */
    to[0] = to[1] = 0.0f;                               /* 0x86317c..0x863184 */
#if ISAAC_DIAG
    letgo_say(line);
#endif
}

/*
 * TWO TAPS TO A SIDE OF A STATIC STICK OR D-PAD - the port's own reading, now
 * that the game's detector fires only on a sanctioned press (double_tap_gate).
 * A tap is a touch shorter than TAP_MS that stays put; its side is where it
 * landed against the seat, at least a third of a travel out (a tap in the
 * middle is no side). The stick is the one whose seat is within the game's
 * own capture, three travels (0x861b98), the nearer if both. The second tap
 * of a pair, same side within PAIR_MS of the first coming up, is sanctioned
 * as it goes DOWN - before the game gets the press it brings - and that press
 * is the one that fires. A cancelled finger (phase 4: a palm, the app leaving)
 * is no tap and breaks the pair.
 */
static struct { long long down; float land[2]; int stick, dir, mid; } g_stap[WATCHED];
static int g_stap_id[WATCHED];
static struct { long long up; int dir; } g_stap_last[2] = { { 0, -1 }, { 0, -1 } };

static void static_tap(int id, int phase, float ux, float uy, long long t) {
    static int ready;
    if (!ready) { ready = 1; for (int i = 0; i < WATCHED; i++) g_stap_id[i] = -1; }
    float travel = 16.0f * size_factor() * g_reach;
    int k = -1;
    for (int i = 0; i < WATCHED; i++) if (g_stap_id[i] == id) { k = i; break; }
    if (phase == 0) {
        if (k < 0) for (int i = 0; i < WATCHED; i++) if (g_stap_id[i] < 0) { k = i; break; }
        if (k < 0) return;
        int stick = -1;
        float best = 3.0f * travel;
        for (int s2 = 0; s2 < 2; s2++) {
            unsigned long st = g_seen[s2];
            if (!st || !g_readable((const void *) st, STICK_SEAT + 8)) continue;
            const float *seat = (const float *) (st + STICK_SEAT);
            float d = hypotf(ux - seat[0], uy - seat[1]);
            if (d <= best) { best = d; stick = s2; }
        }
        g_stap_id[k] = id;
        g_stap[k].down = t;
        g_stap[k].land[0] = ux; g_stap[k].land[1] = uy;
        g_stap[k].stick = stick;
        g_stap[k].dir = -1;
        g_stap[k].mid = 0;
        if (stick < 0) return;
        const float *seat = (const float *) (g_seen[stick] + STICK_SEAT);
        float sx = ux - seat[0], sy = uy - seat[1];
        /* the shooting stick's middle: a tap that stays in it lets go of the aim lock (letgo_ask) */
        g_stap[k].mid = stick == 1 && hypotf(sx, sy) <= shoot_middle(travel);
        if (hypotf(sx, sy) < travel / 3.0f) return;           /* the middle is no side */
        g_stap[k].dir = cardinal(sx, sy);
        if (g_stap_last[stick].dir == g_stap[k].dir && t - g_stap_last[stick].up <= PAIR_MS) {
            preset_detector(stick, g_stap[k].dir);
            g_stap_last[stick].dir = -1;
            char line[120];
            snprintf(line, sizeof(line), "two taps %s on the static %s: the second is sanctioned",
                     DIR_NAME[g_stap[k].dir & 3], stick ? "shooting stick" : "movement stick");
            dtap_say(line);
        }
        return;
    }
    if (k < 0) return;
    if (phase == 1 || phase == 2) {
        /* a finger that walks off is a push, not a tap */
        if (hypotf(ux - g_stap[k].land[0], uy - g_stap[k].land[1]) > travel * 0.5f) g_stap[k].dir = -1;
        if (g_stap[k].mid && g_stap[k].stick >= 0 && g_readable((const void *) g_seen[g_stap[k].stick], STICK_SEAT + 8)) {
            const float *seat = (const float *) (g_seen[g_stap[k].stick] + STICK_SEAT);
            if (hypotf(ux - seat[0], uy - seat[1]) > shoot_middle(travel)) g_stap[k].mid = 0;
        }
        return;
    }
    /* up, or cancelled */
    int stick = g_stap[k].stick, dir = g_stap[k].dir;
    if (phase == 3 && t - g_stap[k].down <= TAP_MS) {
        float far;
        if (dpad_raw_far(id, &far)) {
            if (far <= shoot_middle(travel)) letgo_ask(id, "static D-pad");
        } else if (g_stap[k].mid) {
            const float *seat = g_readable((const void *) g_seen[1], STICK_SEAT + 8)
                              ? (const float *) (g_seen[1] + STICK_SEAT) : NULL;
            if (seat && hypotf(ux - seat[0], uy - seat[1]) <= shoot_middle(travel)) letgo_ask(id, "static");
        }
    }
    if (phase == 3 && stick >= 0 && dir >= 0 && t - g_stap[k].down <= TAP_MS) {
        g_stap_last[stick].up = t;
        g_stap_last[stick].dir = dir;
    } else if (stick >= 0) {
        g_stap_last[stick].dir = -1;                          /* a hold, a push or a cancel breaks the pair */
    }
    g_stap_id[k] = -1;
}

/*
 * Every finger, as it is handed to the game (uikit.c, shim_touch_deliver): the
 * game's thread, before the game sees the event.
 *
 * The moving D-pads too (the owner, 27.09.2026: "Mars does not react at all" -
 * his run was on a floating D-pad). A static D-pad's tap on an arm is a full
 * push at once; a moving one is seated under the finger with no direction,
 * exactly like a moving stick, and needs the same tap, then tap and flick. A
 * floating D-pad's finger is still raw here (shim_sticks_dpad_float puts it on
 * its point right after); the other D-pads' fingers come already on their
 * points, which lie the same way from the middle.
 */
void shim_sticks_tap_watch(int id, int phase, float x, float y) {
    if (!shim_sticks_in_game()) return;
    unsigned long b = shim_image_base();
    if (!b || !g_readable) return;
    unsigned area_buf[4]; const unsigned *area = finger_space(b, area_buf);
    float fw = FIELD_W, fh = FIELD_H;
    if (!area[0] || !area[1] || !(fw > 0.0f) || !(fh > 0.0f)) return;
    float ux = (x - (float) area[2]) * fw / (float) area[0];
    float uy = (y - (float) area[3]) * fh / (float) area[1];
    long long t = now_ms();
    if (g_kind == KIND_STATIC) { static_tap(id, phase, ux, uy, t); return; }
    int k = -1;
    for (int i = 0; i < WATCHED; i++) if (g_watch[i].used && g_watch[i].id == id) { k = i; break; }
    if (phase == 0) {
        if (k < 0) for (int i = 0; i < WATCHED; i++) if (!g_watch[i].used) { k = i; break; }
        if (k < 0) return;
        g_watch[k].used = 1;
        g_watch[k].id = id;
        g_watch[k].t = t;
        g_watch[k].stick = -1;
        g_watch[k].armed = 0;
        g_watch[k].preset = 0;
        g_watch[k].land[0] = ux; g_watch[k].land[1] = uy;
        g_watch[k].far = 0.0f;
        return;
    }
    if (k < 0) return;
    if (phase == 4) { g_watch[k].used = 0; return; }        /* a palm or a cancel: no tap, no flick */
    /* whose it is: the stick that holds this very finger's object */
    if (g_watch[k].stick < 0) {
        const void *touch = shim_touch_object(id);
        for (int s2 = 0; touch && s2 < 2; s2++) {
            unsigned long st = stick_at(s2);
            if (!st || !g_readable((const void *) st, STICK_TOUCH + 8)) continue;
            if (*(const unsigned char *) (st + STICK_HELD) && *(const void *const *) (st + STICK_TOUCH) == touch) {
                g_watch[k].stick = s2;
                break;
            }
        }
        if (g_watch[k].stick >= 0) {
            int s2 = g_watch[k].stick;
            g_watch[k].armed = g_armed_at[s2] && g_watch[k].t - g_armed_at[s2] <= ARM_MS;
        }
    }
    int stick = g_watch[k].stick;
    float dx = ux - g_watch[k].land[0], dy = uy - g_watch[k].land[1];
    float travel = 16.0f * size_factor() * g_reach;
    float sens = stick == 1 ? g_sens_shoot : g_sens;
    float edge = 0.5f * travel * sens;                   /* where the game's pull reaches its half */
    float early = 0.4f * edge < 1.0f ? 1.0f : 0.4f * edge;
    {   /* how far it went from where it landed: a tap that stays in the middle has no direction */
        float far = sqrtf(dx * dx + dy * dy);
        if (far > g_watch[k].far) g_watch[k].far = far;
    }
    if (phase == 1 || phase == 2) {
        if (stick >= 0 && g_watch[k].armed && !g_watch[k].preset && dx * dx + dy * dy >= early * early) {
            g_watch[k].preset = 1;
            g_armed_at[stick] = 0;
            preset_detector(stick, cardinal(dx, dy));
        }
        return;
    }
    /* up: a short plain tap arms its stick for the flick that follows */
    if (stick >= 0 && !g_watch[k].preset && t - g_watch[k].t <= TAP_MS && dx * dx + dy * dy < edge * edge)
        g_armed_at[stick] = t;
    /* and on the shooting stick, one that never left the middle lets go of the aim lock */
    if (stick == 1 && phase == 3 && t - g_watch[k].t <= TAP_MS) {
        float far = g_watch[k].far;
        int dpad = dpad_raw_far(id, &far) || buttons_mode();
        if (far <= shoot_middle(travel))
            letgo_ask(id, g_kind == KIND_FLOATING ? (dpad ? "floating D-pad" : "floating")
                                                  : (dpad ? "dynamic D-pad" : "dynamic"));
    }
    g_watch[k].used = 0;
}

#if ISAAC_DIAG
/*
 * THE REPORT'S LINES FOR THE STICKS (batch 3; report builds only). Each has
 * its own budget and says what "right" looks like in the notes
 * (the research notes of 30.09.2026, batch 3 touch):
 *
 *   stickcurve:  the curve's self-test, once;
 *   sens:        a second of hold at a time - mode, zone, MOVE SENS, pull,
 *                what the stick gives after the game's rules, the actions it
 *                feeds, the player's step, and how often the two hooks ran;
 *   zero-hold:   a run of frames with the movement stick held at an exact zero
 *                - its actions 0..3 and 27..30, the movement input 0x515294
 *                makes of them, and how far the player moved: the 13.09 fear
 *                of "walking on the spot" answered by the game itself;
 *   digital:     each DIGITAL hold that switched: how often, and how many
 *                came back on within 100 ms of going off (flicker);
 *   knob:        the knob's three counters, at the end of a run and every
 *                minute of holding;
 *   capture:     the stick's capture circle against its ring as drawn, in
 *                pixels, on every change of window, picture or canvas.
 */
#define FN_ACTION_VALUE   0x671c7c     /* (-, action, device, -) -> float, as 0x515294 calls it */
#define FN_MOVEMENT_INPUT 0x515294     /* (player) -> the movement vector */
#define PLAYER_POSITION   0x310

static void st_say(const char *kind, int *budget, int max, const char *fmt, ...) __attribute__((format(printf, 4, 5)));
static void st_say(const char *kind, int *budget, int max, const char *fmt, ...) {
    if (*budget >= max || !shim_report_active()) return;
    (*budget)++;
    char line[400];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(line, sizeof(line), fmt, ap);
    va_end(ap);
    report_line(kind, line);
}

static const char *mode_name(int mode) {
    return mode == STICK_DIGITAL ? "DIGITAL" : (mode == STICK_BUTTONS ? "BUTTONS" : "ANALOGUE");
}

static void curve_selftest_once(void) {
    static int done;
    if (done || !shim_report_active()) return;
    done = 1;
    char why[200];
    int fails = stick_curve_selftest(why, sizeof(why));
    char line[280];
    if (fails) snprintf(line, sizeof(line), "selftest FAIL, %d checks: %s", fails, why);
    else snprintf(line, sizeof(line), "selftest ok (zone, MOVE SENS, DIGITAL gap %.2f)", (double) STICK_DIGITAL_GAP);
    report_line("stickcurve", line);
}

static struct {
    int was_held;
    unsigned frames;                   /* of this window of hold */
    float act_max, out_max, step_max;
    unsigned long value_calls, knob_calls;
    unsigned zero;                     /* a run of exact zeros (movement) */
    float zero_act, zero_look, zero_move, zero_moved;
    unsigned held_frames;              /* for the minute's knob line */
} g_sw[2];

static int g_budget_sens, g_budget_zero, g_budget_digital, g_budget_knob, g_budget_capture;

static void knob_say(const char *when) {
    st_say("knob", &g_budget_knob, 24,
           "%s: movement %lu updates at t=1, %lu apart from the finger, %lu past the rim, %lu springs "
           "in mid-hold; shooting %lu, %lu, %lu, %lu (all but the first should be 0)",
           when, g_st[0].knob_full, g_st[0].knob_apart, g_st[0].knob_past, g_st[0].knob_spring,
           g_st[1].knob_full, g_st[1].knob_apart, g_st[1].knob_past, g_st[1].knob_spring);
}

static void zero_say(void) {
    if (g_sw[0].zero < 2) { g_sw[0].zero = 0; return; }
    st_say("zero-hold", &g_budget_zero, 40,
           "%u frames at an exact zero (%s, zone %.2f): actions 0..3 max %.6f, 27..30 max %.6f, "
           "movement input max %.6f, the player moved %.2f units",
           g_sw[0].zero, mode_name(touch_mode()), (double) g_dead, (double) g_sw[0].zero_act,
           (double) g_sw[0].zero_look, (double) g_sw[0].zero_move, (double) g_sw[0].zero_moved);
    g_sw[0].zero = 0;
    g_sw[0].zero_act = g_sw[0].zero_look = g_sw[0].zero_move = g_sw[0].zero_moved = 0.0f;
}

/* the capture circle against the drawing, in pixels (plan item 4.6, the 01.10 addition) */
static void capture_watch(unsigned long b) {
    unsigned long st = stick_at(0);
    if (!st || !g_readable((const void *) st, STICK_RING_SCALE + 8)) return;
    const unsigned *win = (const unsigned *) (b + 0xa9aff8), *area = (const unsigned *) (b + 0xabec64);
    float cw = FIELD_W, ch = FIELD_H;
    const float *scale = (const float *) (st + STICK_RING_SCALE);
    float travel = *(const float *) (st + STICK_TRAVEL);
    if (!win[0] || !win[1] || !area[0] || !area[1] || !(cw > 0.0f) || !(ch > 0.0f) || !(scale[0] > 0.0f)
        || !(travel > 0.0f)) return;
    unsigned word = *(const unsigned *) (b + 0x861b9c);
    float mult = word == 0x1E211002u ? 3.0f : word == 0x1E221002u ? 4.0f : word == 0x1E281002u ? 0.125f
               : word == 0x1E2E9002u ? 1.25f : -1.0f;
    static char was[160];
    char key[160];
    snprintf(key, sizeof(key), "%u %u %u %u %u %u %.1f %.1f %.4f %.4f %d %.3f %08x", win[0], win[1], area[0], area[1],
             area[2], area[3], (double) cw, (double) ch, (double) scale[0], (double) scale[1], g_pct, (double) travel, word);
    if (!strcmp(key, was)) return;
    snprintf(was, sizeof(was), "%s", key);
    float px = (float) win[0] / cw, py = (float) win[1] / ch;              /* the stick pass: the window */
    float rim = 64.0f * (float) g_pct / 100.0f;                           /* "Analog Bounds": 128 texels */
    float knob = 32.0f * (float) g_pct / 100.0f;                          /* "Analog Stick": its disc */
    float cap_x = mult * travel * px, cap_y = mult * travel * py;
    float ring_x = rim * scale[0] * px, ring_y = rim * scale[1] * py;
    float k = *(const float *) (b + 0xa9c7c8);
    st_say("capture", &g_budget_capture, 8,
           "FULLSCREEN %s: window %ux%u, picture %ux%u at %u,%u, canvas %.0fx%.0f, K %.3f; capture %.3g travels "
           "= %.0f x %.0f px, the ring drawn %.0f x %.0f px (scale %.3f, %.3f): capture / ring %.2f x %.2f "
           "(FULLSCREEN on gives the same both ways); the knob's edge at full pull %.0f x %.0f px",
           shim_screen_is_wide() ? "on" : "off", win[0], win[1], area[0], area[1], area[2], area[3],
           (double) cw, (double) ch, (double) k, (double) mult, (double) cap_x, (double) cap_y,
           (double) ring_x, (double) ring_y, (double) scale[0], (double) scale[1],
           (double) (cap_x / ring_x), (double) (cap_y / ring_y),
           (double) (travel * px + knob * scale[0] * px), (double) (travel * py + knob * scale[1] * py));
}

/* between frames, in a run */
static void stick_watch(unsigned long b) {
    curve_selftest_once();
    if (!shim_report_active() || !g_readable) return;
    static int was_run;
    int run = in_game();
    if (was_run && !run) knob_say("the run is over");
    was_run = run;
    if (!run) return;
    capture_watch(b);
    unsigned long pl = touch_player();
    int dev = pl ? *(const int *) (pl + PLAYER_DEVICE) : -1;
    float (*action)(unsigned long, int, int, unsigned long) =
        (float (*)(unsigned long, int, int, unsigned long)) (b + FN_ACTION_VALUE);
    stick_v2 (*movement)(unsigned long) = (stick_v2 (*)(unsigned long)) (b + FN_MOVEMENT_INPUT);
    static float was_pos[2];
    static unsigned long was_pl;
    float step = 0.0f;
    if (pl && shim_eid_readable((const void *) (pl + PLAYER_POSITION), 8)) {
        const float *pos = (const float *) (pl + PLAYER_POSITION);
        if (was_pl == pl) step = hypotf(pos[0] - was_pos[0], pos[1] - was_pos[1]);
        was_pos[0] = pos[0]; was_pos[1] = pos[1];
        was_pl = pl;
    } else {
        was_pl = 0;
    }
    for (int i = 0; i < 2; i++) {
        unsigned long st = stick_at(i);
        if (!st || !g_readable((const void *) st, STICK_DIV + 4)) continue;
        int held = *(const unsigned char *) (st + STICK_HELD) != 0;
        if (!held) {
            if (g_sw[i].was_held) {
                if (i == 0) zero_say();
                if (g_st[i].ons)
                    st_say("digital", &g_budget_digital, 40, "%s stick: %d switch-ons in the hold, %d within "
                           "100 ms of a switch-off (zone %.2f, gap %.2f)", i ? "shooting" : "movement",
                           g_st[i].ons, g_st[i].bounces, (double) g_dead, (double) STICK_DIGITAL_GAP);
                g_st[i].ons = g_st[i].bounces = 0;
            }
            g_sw[i].was_held = 0;
            continue;
        }
        g_sw[i].was_held = 1;
        const float *v = (const float *) (st + STICK_VALUE);
        float out = hypotf(v[0], v[1]);
        float act = 0.0f;
        if (pl && dev != -1)
            for (int a = i ? 4 : 0; a < (i ? 8 : 4); a++) { float x = action(0, a, dev, 0); if (x > act) act = x; }
        g_sw[i].frames++;
        if (act > g_sw[i].act_max) g_sw[i].act_max = act;
        if (out > g_sw[i].out_max) g_sw[i].out_max = out;
        if (i == 0 && step > g_sw[i].step_max) g_sw[i].step_max = step;
        if (i == 0) {
            if (v[0] == 0.0f && v[1] == 0.0f && pl && dev != -1) {
                float look = 0.0f;
                for (int a = 27; a <= 30; a++) { float x = action(0, a, dev, 0); if (x > look) look = x; }
                stick_v2 mv = movement(pl);
                float mm = hypotf(mv.x, mv.y);
                g_sw[0].zero++;
                if (act > g_sw[0].zero_act) g_sw[0].zero_act = act;
                if (look > g_sw[0].zero_look) g_sw[0].zero_look = look;
                if (mm > g_sw[0].zero_move) g_sw[0].zero_move = mm;
                g_sw[0].zero_moved += step;
            } else if (g_sw[0].zero) {
                zero_say();
            }
            if (++g_sw[0].held_frames >= 3600) { g_sw[0].held_frames = 0; knob_say("a minute of holding"); }
        }
        if (g_sw[i].frames >= 60) {
            float travel = *(const float *) (st + STICK_TRAVEL), div = *(const float *) (st + STICK_DIV);
            st_say("sens", &g_budget_sens, 60,
                   "%s stick, %s: zone %.2f, sensitivity %.2f, pull %.3f, gives %.3f (max %.3f), actions %s max %.3f, "
                   "player step max %.2f units/frame; the port's value asked %lu times, knob %lu (this second)",
                   i ? "shooting" : "movement", mode_name(touch_mode()), i ? 0.0 : (double) g_dead,
                   travel > 0.0f ? (double) (div / travel) : 0.0, (double) g_st[i].m, (double) out,
                   (double) g_sw[i].out_max, i ? "4..7" : "0..3", (double) g_sw[i].act_max,
                   (double) g_sw[i].step_max, g_st[i].value_calls - g_sw[i].value_calls,
                   g_st[i].knob_calls - g_sw[i].knob_calls);
            g_sw[i].frames = 0;
            g_sw[i].act_max = g_sw[i].out_max = g_sw[i].step_max = 0.0f;
            g_sw[i].value_calls = g_st[i].value_calls;
            g_sw[i].knob_calls = g_st[i].knob_calls;
        }
    }
}

/*
 * THE SLIDERS, READ AND WRITTEN (plan item 4.6, 5.1a). At every read of the file
 * and at every press in Tweaks: what the three numbers are, and - after a
 * press - what the file says once written, read back from the disk itself.
 * "Changed by somebody else between launches" is the settings guard's line
 * (saves.c, shim_guard_check: "port-touch.ini CHANGED OUTSIDE the game").
 */
static float g_said_dead = -1.0f, g_said_sens = -1.0f, g_said_shoot = -1.0f;
static int g_budget_sliders;

static int ini_reads_back(float *dead, float *sens, float *shoot) {
    FILE *f = shim_safe_fopen_read(g_path);
    if (!f) return 0;
    char line[256];
    int got = 0;
    while (fgets(line, sizeof(line), f)) {
        if (line[0] == '[') break;                          /* the mods' sections: not ours */
        float v;
        if (sscanf(line, " deadzone = %f", &v) == 1) { *dead = v; got |= 1; }
        else if (sscanf(line, " sensshoot = %f", &v) == 1) { *shoot = v; got |= 4; }
        else if (sscanf(line, " sens = %f", &v) == 1) { *sens = v; got |= 2; }
    }
    fclose(f);
    return got == 7;
}

static void sliders_read_note(void) {
    if (g_dead == g_said_dead && g_sens == g_said_sens && g_sens_shoot == g_said_shoot) return;
    st_say("sliders", &g_budget_sliders, 40, "read: DEAD ZONE %.2f, MOVE SENS %.2f, AIM SENS %.2f from %s",
           (double) g_dead, (double) g_sens, (double) g_sens_shoot, g_path);
    g_said_dead = g_dead; g_said_sens = g_sens; g_said_shoot = g_sens_shoot;
}

static void sliders_written_note(float was_dead, float was_sens, float was_shoot) {
    if (g_dead == was_dead && g_sens == was_sens && g_sens_shoot == was_shoot) return;
    float fd = -1.0f, fs = -1.0f, fa = -1.0f;
    int back = ini_reads_back(&fd, &fs, &fa);
    int same = back && fabsf(fd - g_dead) < 0.006f && fabsf(fs - g_sens) < 0.006f && fabsf(fa - g_sens_shoot) < 0.006f;
    char what[120] = "";
    size_t n = 0;
    if (g_dead != was_dead)
        n += (size_t) snprintf(what + n, sizeof(what) - n, "DEAD ZONE %.2f -> %.2f ", (double) was_dead, (double) g_dead);
    if (g_sens != was_sens && n < sizeof(what))
        n += (size_t) snprintf(what + n, sizeof(what) - n, "MOVE SENS %.2f -> %.2f ", (double) was_sens, (double) g_sens);
    if (g_sens_shoot != was_shoot && n < sizeof(what))
        snprintf(what + n, sizeof(what) - n, "AIM SENS %.2f -> %.2f ", (double) was_shoot, (double) g_sens_shoot);
    if (same)
        st_say("sliders", &g_budget_sliders, 40, "%s- written ok, the file reads %.2f / %.2f / %.2f", what,
               (double) fd, (double) fs, (double) fa);
    else if (back)
        st_say("sliders", &g_budget_sliders, 40, "%s- NOT WRITTEN: the file reads %.2f / %.2f / %.2f", what,
               (double) fd, (double) fs, (double) fa);
    else
        st_say("sliders", &g_budget_sliders, 40, "%s- NOT WRITTEN: the file does not read back (errno %d)", what, errno);
    g_said_dead = g_dead; g_said_sens = g_sens; g_said_shoot = g_sens_shoot;
}
#endif

float shim_sticks_ring_units(void) { return ring_drawn_radius(); }

/* the editor: the OUTER circle, which is what a thing has to keep clear of */
float shim_sticks_ring_outer(void) { return ring_radius(); }

int shim_sticks_static(void) { return g_kind == KIND_STATIC; }
int shim_sticks_floating(void) { return g_kind == KIND_FLOATING; }

/*
 * Which stick animation file to substitute.
 *
 * A sprite's scale is set by the XScale field of a frame, and the frames live
 * in someone else's memory behind a pair of pointers. Instead of hunting for
 * them we keep two ready files next to it, enlarged by 35 and 70 percent, and
 * swap the path. The game asks for the name with capitals
 * (iOS_Buttons.anm2) while on disk it is lower case — hence the case-
 * insensitive comparison.
 */
static char g_anm2[1024];

const char *shim_sticks_anm2(const char *p) {
    if (g_pct == 100 || !p) return 0;
    static const char tail[] = "ios_buttons.anm2";
    size_t n = strlen(p), m = sizeof(tail) - 1;
    if (n < m || strcasecmp(p + n - m, tail) != 0) return 0;
    snprintf(g_anm2, sizeof(g_anm2), "%.*sios_buttons_%d.anm2", (int) (n - m), p, g_pct);
    return g_anm2;
}

/*
 * Is a run in progress?
 *
 * The marker is the game state kept in its control object. The values were
 * taken from the live game, not deduced from the code:
 *
 *   3 — menu,  1 — loading a room,  2 — a run is going.
 *
 * I used to take a pointer to the game object and its readiness field here —
 * and got it wrong: inside a run that pointer is empty, and it is alive
 * precisely during loading. That produced the exact opposite: the sticks hung
 * around in the menu and vanished in a run.
 */
#define STATE_RUNNING 2

static int in_game(void) {
    unsigned long b = shim_image_base();
    if (!b) return 0;
    const void *holder = *(const void *const *) (b + 0xac2428);
    if (!holder) return 0;
    return *(const int *) ((const char *) holder + 0x204c0) == STATE_RUNNING;
}

int shim_sticks_in_game(void) { return in_game(); }

/*
 * Is a RUN on screen - not "is the player playing", but "is the picture the
 * run's picture"? The two differ for the length of a stage transition, and
 * that difference was throwing the transition into a corner.
 *
 * The canvas has two shapes (screen.c): the run's, at the game's own scale,
 * and the menus', at a whole number of pixels to the unit. They are chosen by
 * the manager's state, and the state leaves RUNNING while a floor is being
 * handed over - measured, +187 s state 2, then state 5 for the transition,
 * then state 2 again. So the canvas swapped to the menus' shape underneath the
 * transition and back, and the transition was laid out against one shape and
 * projected through the other: the picture came out at the ratio between the
 * two, anchored at the origin.
 *
 * How big that error is, per device, is exactly why it looked like a mystery:
 *
 *     samsung A17  2340x1080   both canvases 585x270      no error at all
 *     the owner's  3120x1440   585x270 against 624x288    0.94 - barely seen
 *     Infinix      1600x 720   600x270 against 800x360    0.75 - glaring
 *
 * A transition is part of the run, so the run's canvas stands through it and
 * nothing is rebuilt in the middle of it - which also spares the rebuild that
 * used to land inside the transition's own animation.
 */
#define STATE_TRANSITION 5          /* a floor being handed over */

int shim_sticks_run_canvas(void) {
    unsigned long b = shim_image_base();
    if (!b) return 0;
    const void *holder = *(const void *const *) (b + 0xac2428);
    if (!holder) return 0;
    int state = *(const int *) ((const char *) holder + 0x204c0);
    return state == STATE_RUNNING || state == STATE_TRANSITION;
}



/*
 * The values of our menu rows.
 *
 * The branches read them out of the settings object, from fields the image
 * itself never touches: Fullscreen (+0x55eec) for the kind and an unnamed byte
 * +0x55f16 for the size. We write there whatever the port's settings say, and
 * then the menu shows the right word.
 *
 * The neighbouring +0x55eed was never taken over: it is the Filter field, which
 * the image reads in the frame loop (changing the stick size once toggled it).
 * Since hotfix 1.75 the Filter is off for good (loader.c filter_gone) and its
 * row is the RUMBLE slider, whose byte is the padding after it, +0x55eee.
 */
#define FIELD_KIND  0x55eec      /* Fullscreen: no references in the image except our branch */
#define FIELD_SIZE  0x55f16      /* an unnamed byte: no references in the image at all       */
#define FIELD_CLICK 0x55f12      /* Rumble: the byte the game toggles on a press             */
#define FIELD_RUMBLE 0x55eee     /* the RUMBLE row's slider, 0..10 (loader.c rumble_menu_item) */
#define ROW_KIND 5                  /* the NUMBER of the row, as the list carries it */
#define ROW_SIZE 6

static void save_settings(void);
void shim_sticks_size_changed(void);

void shim_sticks_apply_mode(int kind);
void shim_screen_wide(int on);
int shim_screen_is_wide(void);
void shim_screen_refit(void);
int shim_screen_cutscene(void);     /* the picture is a cutscene's own (screen.c) */

/* set when the canvas has to be rebuilt; done between frames, never mid-draw */
static int g_refit_wanted;

/*
 * The row that used to be the aim lock, and the byte it shows.
 *
 * 18 is its number in the game's own list, unchanged - only what the row draws
 * and what a press does are ours. The byte is free: nothing in the image builds
 * the offset 0x55f17.
 */
/*
 * Rows are known by their NUMBER, looked up through the list.
 *
 * The highlighted line is kept as an index INTO the array of rows at +0x2f8,
 * and for a while the rows were told apart by that index: the aim lock stood
 * second, ours fourth and fifth. That stopped being true the day the pause
 * menu's list lost the FULLSCREEN row (menu.c: switching the canvas under a
 * running room lays the room out wrong, so the row exists only in the main
 * menu) - in that list everything after it stands one place earlier. So the
 * index is turned into the row's number through the list itself, and the
 * numbers are what is compared: 5 and 6 for the sticks, 18 for the canvas.
 *
 * The TAP table is indexed by the number minus two, so item 18 is entry 16
 * there. Two different countings, one row.
 */
#define ROW_FULLSCREEN 18
#define FIELD_FULLSCREEN 0x55f17
const char *shim_home_dir(void);
int shim_profile_clean(void);

/*
 * Our defaults, on a profile that has never been played.
 *
 * Applied once a launch, as early as the settings block exists - which is a
 * frame, not a menu. They used to be applied from the options menu, so a player
 * who never opened it played with the game's defaults instead of ours.
 *
 * "Never been played" is decided before the game starts (shim_set_saves asks),
 * because the game writes options.ini itself at startup and any later question
 * answers "already settled" even on a fresh install.
 */
static void apply_our_defaults(unsigned char *settings) {
    static int done;
    static int frames;
    if (done || !settings) return;
    int clean = shim_profile_clean();
    DIAG(LOG("defaults: settings file %s; aim lock +0x55f15 = %d, "
             "gamma +0x55ef0 = %.4f, field +0x55f0c = %.4f",
             clean ? "missing — installing ours" : "present — left alone",
             settings[0x55f15], (double) *(const float *) (settings + 0x55ef0),
             (double) *(const float *) (settings + 0x55f0c)));
    {
        int shim_report_active(void);
        void report_line(const char *, const char *);
        const char *shim_saves_dir(void);
        static int said;
        if (said < 3 && shim_report_active()) {
            said++;
            char line[300];
            const char *saves = shim_saves_dir();
            snprintf(line, sizeof(line),
                     "saves \"%s\"; profile %s; gamma now %.4f",
                     saves ? saves : "(none yet)",
                     clean ? "clean, ours go in" : "already settled, ours stay out",
                     (double) *(const float *) (settings + 0x55ef0));
            report_line("defaults", line);
        }
    }
    /*
     * AIM LOCK IS NOT TOUCHED HERE ANY MORE (P2, 30.09.2026). From 09.09 this
     * line zeroed the byte +0x55f15 on every launch - a second owner of the
     * byte the Tweaks row flips, so the row's choice never outlived the
     * process. Its default is now the game's own (loader.c, aimlock_default),
     * an old AimLock=1 is cleaned up once (saves.c, aimlock_once), and the row
     * is saved when pressed (saves.c, shim_saves_between_frames).
     */
    if (!clean) { done = 1; return; }
    /*
     * ON A CLEAN PROFILE OUR VALUES ARE PUT BACK UNTIL THE GAME KEEPS THEM.
     *
     * Writing them once, on the first frame the settings block exists, was too
     * early: with no options.ini to read the game fills the block with its own
     * defaults DURING startup, after our write. (Brightness has since left this
     * loop altogether - see the end of the function.)
     *
     * So on a clean profile they go in on every frame, and the moment the game
     * writes the player's own options.ini - which it does at startup, once the
     * block is settled - they stop for good and the file is the player's. The
     * file is asked for about once a second, not every frame.
     */
    if (++frames >= 60) {
        frames = 0;
        int shim_profile_file_now(void);
        if (shim_profile_file_now()) {
            done = 1;
            LOG("defaults: the game has written options.ini - ours stand, we stop");
        }
    }
    settings[FIELD_FULLSCREEN] = 1;     /* the wide canvas, as the port has always been */
    /*
     * Brightness is not written here any more (27.09.2026). The game had already
     * put its 0.65 into the new options.ini before our first frame, so a value
     * set in memory lived only until the next launch. Our 0.90 is now the
     * game's own default: brightness_default() in loader.c.
     */
}

/*
 * THE CLICK BYTE IS ALSO RUMBLE. Our two rows take their clicks from the byte
 * the game's Rumble toggle flips (+0x55f12), and that byte is the game's
 * RumbleEnabled as well - which rumble.c gives back. So the byte is written in
 * one place only, here, and every write moves the remembered value with it:
 * a byte changed behind the sync's back would read as a click next frame.
 */
static int g_click_was = -1;

void shim_sticks_click_byte(int value) {
    unsigned long b = shim_image_base();
    unsigned char *settings = b ? *(unsigned char **) (b + 0xac2428) : 0;
    if (!settings) return;
    if (settings[FIELD_CLICK] == (unsigned char) value && g_click_was == value) return;
    settings[FIELD_CLICK] = (unsigned char) value;
    g_click_was = value;
}

void shim_sticks_menu_sync(unsigned long menu) {
    unsigned long b = shim_image_base();
    if (!b) return;
    unsigned char *settings = *(unsigned char **) (b + 0xac2428);
    if (!settings) return;

    /*
     * The game turns a press on our row into a toggle of a single byte — both
     * rows were given the same switch. We catch that byte changing and look at
     * which row is highlighted: the third is the kind, the fourth the size.
     * That gives the size a third state, which the switch itself does not have.
     *
     * The highlighted row lives at +0x10 — the index INTO the array of rows,
     * the very one the game uses to pick a branch (ldrsw x9, [x19, #0x10] at
     * 0x6e334c). This used to read +0x2f4, which is something else entirely:
     * the language the screen was opened with (0x6e33b0 compares it with
     * 0x66de14() and, when they differ, leaves through the language change,
     * 0x6e2e3c - P1, the skeptic's check of 30.09).
     *
     * Defaults on a clean profile: see apply_our_defaults. The aim lock byte
     * +0x55f15 is 1 for "on" (0x860784 reads it), and options.ini stores it AS
     * IT IS (ldrb w8, [x19, #0x4d] at 0x7ec290 -> AimLock=%d); the `eor #1` at
     * 0x6e3f90 belongs to the menu row's picture only.
     */
    apply_our_defaults(settings);

    /*
     * The row draws itself from the byte, so the byte has to agree with what the
     * port is actually doing - and the truth comes from port-touch.ini, read
     * long before any settings block exists. Written once a launch, and only
     * when they differ, so a press is never overwritten by this.
     */
    {
        static int told;
        int on = shim_screen_is_wide();
        if (!told && settings[FIELD_FULLSCREEN] != (unsigned char) on) {
            settings[FIELD_FULLSCREEN] = (unsigned char) on;
            told = 1;
        }
    }

    /*
     * One press is one change.
     *
     * The row answers while the finger LIES on it, not when it is put down: the
     * byte a press flips is flipped again on every frame the touch lasts, so a
     * tap of a tenth of a second arrives as six to ten changes and a held finger
     * as a blur. The stick rows had this too - a ring of three hid it, because
     * ten steps round three still land somewhere sensible - and on a switch of
     * two it is plain to see.
     *
     * The answer is the press itself, not a pause after it: the touch layer
     * counts fingers as they are put down, and one act is allowed per finger. A
     * held finger does nothing more, because nothing more was pressed. When the
     * flip did not come from a finger at all - a pad, a keyboard - there is no
     * counter to lean on and the game's own repeat is left to decide.
     */
    unsigned shim_touch_downs(void);
    int shim_touch_any_down(void);
    static unsigned acted_on;

    static int ours = 0;
#define was g_click_was
    int click = settings[FIELD_CLICK];
    /*
     * One press, one flip. The count of presses is the whole test: a press that
     * has already been acted on can never be acted on again, however long the
     * frame that follows it takes. Asking "is a finger still down" as well was
     * wrong - a slow frame gives the finger time to lift, and the second flip
     * the game sends for the same press then walks straight through. A flip
     * that came from a pad or a keyboard moves no counter at all, so those are
     * recognised by there having been no presses yet and left to the game.
     */
    unsigned presses = shim_touch_downs();
    int fresh = presses == 0 || presses != acted_on;
    if (ours && click != was && menu && fresh) {
        acted_on = presses;
        /* the highlighted place, turned into the number of the row standing there */
        int at = *(const int *) (menu + 0x10);
        const int *const *rows = (const int *const *) (menu + 0x2f8);
        const int *const *rows_end = (const int *const *) (menu + 0x300);
        long count = (*rows && *rows_end > *rows) ? (long) (*rows_end - *rows) : 0;
        int row = (at >= 0 && at < count) ? (*rows)[at] : -1;
        if (row == ROW_KIND) {
            g_kind = (g_kind + 1) % KIND_COUNT;
            shim_sticks_apply_mode(g_kind);
            LOG("menu: stick kind -> %s", kind_name(g_kind));
            save_settings();
        } else if (row == ROW_FULLSCREEN) {
            /*
             * The wide canvas, from the row that used to be the aim lock. The
             * value is the byte the row draws itself from, so the picture and
             * the setting cannot disagree.
             *
             * It takes effect where it is pressed: the canvas is asked for
             * again between frames, and the off-screen surfaces are handed back
             * first so the game makes them anew at the new size. See
             * shim_screen_refit for why both halves are needed - the numbers
             * alone leave the picture soft.
             */
            settings[FIELD_FULLSCREEN] = settings[FIELD_FULLSCREEN] ? 0 : 1;
            shim_screen_wide(settings[FIELD_FULLSCREEN]);
            g_refit_wanted = 1;
            LOG("menu: the wide canvas -> %s", settings[FIELD_FULLSCREEN] ? "on" : "off");
            save_settings();
        } else if (row == ROW_SIZE) {
            g_pct = (g_pct <= 100) ? PCT_MEDIUM : (g_pct <= PCT_MEDIUM ? PCT_BIG
                                                  : (g_pct <= PCT_BIG ? PCT_VERY_BIG : 100));
            LOG("menu: stick size -> %d%%", g_pct);
            shim_sticks_size_changed();
            save_settings();
        } else {
            LOG("menu: a click on row %d — not ours", row);
        }
    }
    was = click;
    ours = 1;
    /*
     * The byte is also the game's RumbleEnabled, and the setting is ours (the
     * Tweaks page): put it back at once, and remember the value put back, so
     * the restoring is never taken for a click.
     */
    settings[FIELD_CLICK] = (unsigned char) shim_rumble_get();
    was = settings[FIELD_CLICK];
#undef was

    /*
     * THE RUMBLE ROW (hotfix 1.75): the game's FILTER row, now the slider of
     * the rumble's strength (loader.c rumble_menu_item). Its byte, +0x55eee, is
     * padding the game neither loads nor saves, so it is ours to keep: the
     * strength rumble.c holds is put into it every frame the list is drawn, and
     * a step the game made in it since the last frame - left or right on a
     * pad, a keyboard or a sideways swipe, as on SFX - goes to rumble.c, which
     * feels it at once (one IMPACT at the new strength, the slider's sample),
     * and into port-touch.ini. 0 is off, as the Tweaks slider had it.
     *
     * The sample goes where the step came from: a finger on the glass is the
     * phone, anything else a pad or a keyboard (rumble.c, routeOf).
     */
    {
        void shim_rumble_level_set(int n, int by_pad);
        int shim_rumble_level(void);
        static int shown = -1;
        int now = settings[FIELD_RUMBLE];
        if (shown >= 0 && now != shown && now <= 10) {
            shim_rumble_level_set(now, !shim_touch_any_down());
            LOG("menu: rumble -> %d", now);
            save_settings();
        }
        settings[FIELD_RUMBLE] = (unsigned char) (shown = shim_rumble_level());
    }

    /*
     * We write right before the row is drawn — the hook sits at its start. If
     * by the next frame the value is not ours, then somebody else holds the
     * field, and that is worth knowing rather than guessing. We complain three
     * times and fall silent.
     */
    static unsigned char wrote_kind, wrote_size, wrote, complained;
    if (wrote && complained < 3 &&
        (settings[FIELD_KIND] != wrote_kind || settings[FIELD_SIZE] != wrote_size)) {
        complained++;
        LOG("menu: fields overwritten from outside — kind %d (ours %d), size %d (ours %d)",
            settings[FIELD_KIND], wrote_kind, settings[FIELD_SIZE], wrote_size);
    }
    settings[FIELD_KIND] = wrote_kind = (unsigned char) g_kind;
    /* the value is the frame of the Rumble animation: SMALL, MEDIUM, BIG, VERY BIG */
    settings[FIELD_SIZE] = wrote_size = (unsigned char) (g_pct <= 100 ? 0 : (g_pct <= PCT_MEDIUM ? 1
                                                         : (g_pct <= PCT_BIG ? 2 : 3)));
    wrote = 1;
}

/* remember the choice so it survives a restart */
/*
 * The whole file, not a part of it.
 *
 * This used to write six keys out of twelve, and the file is rewritten from
 * scratch: touching the size in the menu silently threw away the sensitivity
 * of both sticks, the hand-set places and the comments that explain the rest.
 * The owner's file lost sens=0.80 and sensshoot=0.40 exactly that way. There
 * is one writer now, and it puts down everything it knows.
 */
static void save_settings(void) {
    if (!g_path[0]) return;
    write_default();
}

/*
 * What the Tweaks page reads and writes. The numbers live here, so the page
 * asks rather than keeping a second copy of them - the same reason the ini
 * writer asks every other owner for its key.
 */
float shim_sticks_dead_share(void) { return g_dead; }
float shim_sticks_sens(void) { return g_sens; }
float shim_sticks_sens_shoot(void) { return g_sens_shoot; }

void shim_sticks_set_deadzone(float v) { g_dead = v < 0.0f ? 0.0f : (v > 0.9f ? 0.9f : v); }
void shim_sticks_set_sens(float v) { g_sens = v < 0.1f ? 0.1f : (v > 1.0f ? 1.0f : v); }
void shim_sticks_set_sens_shoot(float v) { g_sens_shoot = v < 0.1f ? 0.1f : (v > 1.0f ? 1.0f : v); }

void shim_sticks_settings_save(void) {
#if ISAAC_DIAG
    float was_dead = g_said_dead, was_sens = g_said_sens, was_shoot = g_said_shoot;   /* the file as last known */
#endif
    save_settings();
#if ISAAC_DIAG
    /* what the press changed, and what the file says once written (the report's "sliders:") */
    sliders_written_note(was_dead, was_sens, was_shoot);
#endif
}

/*
 * A mark that the overlay has started.
 *
 * This is NOT observation but part of a fix: the drawing wrapper uses it to
 * turn the depth test off for the duration of the overlay. So it lives outside
 * the diagnostics and must not go dark with them.
 */
static int g_overlay;
int shim_sticks_overlay_started(void) { return g_overlay; }
void shim_sticks_overlay_reset(void) { g_overlay = 0; }

/* remember both sticks: that is how their sprites are recognised in the renderer */
static unsigned long g_seen[2];

/*
 * WHERE the sticks are, read out of the image so they can be reached when the
 * game has stopped handing them to us.
 *
 * This is the third return of "the sticks stay lying over the menu", and the
 * reason it kept coming back: the opacity we raise is lowered in
 * shim_sticks_pin, and shim_sticks_pin runs only from the widget's own update
 * (the '#' hooks). After a run those updates stop coming while the overlay is
 * still DRAWN - the draw at 0x861df8 is called from the frame loop
 * unconditionally (0x671c08) and gated on nothing but overlay+4 - so the line
 * that lowers the opacity was never reached, and the last value written
 * stood: ours, a one. Both earlier fixes changed the condition inside that
 * line, which could not help: the line was not being run.
 *
 * So the lowering is done from the frame loop, by addresses that cannot go
 * stale, all three taken from the image:
 *
 *   the overlay is EMBEDDED in the Manager at +0x1f6d18 - built there once per
 *   process by 0x85ed74 from the engine setup (0x66e990: manager+0x1f6d18 is
 *   handed to the constructor), and the frame loop reaches it the same way
 *   (0x671bfc) before drawing it;
 *
 *   the two sticks are the widgets at overlay+0x10 and overlay+0x340 - the
 *   draw compares every widget of its list against exactly those two
 *   (0x861e30..0x861e40: `add x22, x19, #0x10; add x19, x19, #0x340; cmp;
 *   ccmp`) and treats them apart; their sprites are the ones loaded into
 *   overlay+0x20 and overlay+0x350 by the constructor (0x85f4d0);
 *
 *   the opacity is the float at widget+0x328, which is what that draw reads
 *   before rendering (0x861f5c, 0x861fe8).
 *
 * Nothing of this is remembered from an earlier frame, so a rebuilt or a
 * freed object is not a case that can arise: the Manager lives as long as the
 * game does.
 */
#define OVERLAY_IN_MANAGER 0x1f6d18
#define STICK_MOVE   0x10
#define STICK_SHOOT  0x340
#define STICK_ALPHA  0x328

/*
 * The shooting stick as the GAME keeps it: its home (+0x1b8) and anchor
 * (+0x180), read off the widget the overlay built. For the report: the row is
 * placed from our own arithmetic, and this is what to compare it with.
 */
int shim_sticks_shoot_home(float *hx, float *hy, float *ax, float *ay) {
    if (!g_seen[1] || !g_readable || !g_readable((const void *) g_seen[1], 0x1c0)) return 0;
    const float *home = (const float *) (g_seen[1] + 0x1b8);
    const float *anchor = (const float *) (g_seen[1] + 0x180);
    if (hx) *hx = home[0];
    if (hy) *hy = home[1];
    if (ax) *ax = anchor[0];
    if (ay) *ay = anchor[1];
    return 1;
}


/*
 * The size changes with no restart.
 *
 * The scale of the picture lives in the animation frame, that is, in the file,
 * and is chosen when the file is opened - which used to happen once per launch,
 * while the overlay was built. Hence the old behaviour: the menu moved the
 * sticks, because the radius is read live, and left their picture alone.
 *
 * Two things had to be found for this to work at all.
 *
 * FIRST, the right loader. The game keeps three editions of the touch sticks
 * and each has its own copy of the sprite's load; the one in use here is
 * 0x89fe24, the very call the overlay builder makes. Its twins - 0x9cfcc and
 * 0x3edd88 - take the path as a plain char pointer instead of a string, and
 * calling them killed the process several times before the difference showed.
 *
 * SECOND, the name. Animations are not owned by the sprites: every .anm2 goes
 * into one store, keyed by the path it was asked for (measured - two dozen
 * different files in a row all handed to the same object). Asking again for
 * "iOS_Buttons.anm2" brings back what the store parsed at startup, whatever
 * file that name reaches now. So the variant is asked for by its own name.
 *
 *   0x89fe24  load(sprite, std::string const *path, bool)
 *   0x89e82c  play(sprite, char const *animation, bool)
 *
 * The sprites are the knob at +0x10 and the ring at +0x1d0 of each stick - the
 * two the renderer hands us - and the animation being played is put back after
 * the load, which leaves a sprite on the file's first one.
 */
#define GAME_SPRITE_LOAD 0x89fe24   /* Sprite::Load(path, force) - the builder's own */
#define GAME_SPRITE_PLAY 0x89e82c   /* Sprite::Play(name, force) */
#define STICK_ANM2 "gfx/ui/touch/iOS_Buttons.anm2"

void *shim_str_assign(void *self, const char *s);
void shim_str_dtor(void *self);

static int g_resize_wanted;

void shim_sticks_size_changed(void) { g_resize_wanted = 1; }

/* the editor (touchedit.c): which of the three sizes is set right now */
int shim_sticks_size_pct(void) { return g_pct; }

/*
 * THE FILE WAS EDITED OUTSIDE (the Tweaks page's CONFIG FILE, item 14): Java
 * notices on the way back from the file manager and asks; the file is read
 * again here, between frames, on the thread that reads the settings.
 */
static volatile int g_reload_asked;
void shim_sticks_reload_ask(void) { g_reload_asked = 1; }

static int pad_blocks_touch(void);

/*
 * AIM LOCK LETS GO OF THE WEAPONS THAT NEED A RELEASE (the owner's 19th round,
 * 03.10.2026: "T.Forgotten with aim lock charges forever and cannot strike").
 *
 * The game has the rule itself. The overlay's update (0x8607d4..0x860854)
 * takes player 0's weapon type (player +0x1760, +0x38) and leaves the shooting
 * stick FREE - no lock, no padlock on the knob, a lifted finger stops the fire
 * - for the weapons that fire on release: `1 << type & 0x1d4`, Brimstone (2),
 * the knife (4), Epic Fetus (6), Monstro's Lung (7), Ludovico (8), and tears
 * with Cursed Eye or Chocolate Milk. The table was written for Rebirth's eight
 * types: `cmp w9, #8; b.hi` sends every later one under the lock, Tech X and
 * the Forgotten's bone among them - held forever, they charge forever. The
 * image is the iPhone's, so the iPhone had the same fault.
 *
 * The types, measured on the image (the factory table 0xa9c490 + 8 * type that
 * Weapon::Create, 0x7d33a4, reads; the classes by their RTTI): 9 Weapon_TechX,
 * 10 and 11 Weapon_Bone (the Forgotten's club, the Notched Axe), 12
 * Weapon_UrnOfSouls, 13 Weapon_SpiritSword, 14 Weapon_Fetus, 15
 * Weapon_UmbilicalWhip. The ones that charge and fire on release - 9, 10, 11,
 * 13 - join the game's own free set; 12, 14 and 15 fire while held, like
 * Technology, and stay locked as before. Two words, in place:
 *
 *   0x8607e0  cmp w9, #8          -> cmp w9, #15          (7100213f -> 71003d3f)
 *   0x8607f0  mov w11, #0x1d4     -> mov w11, #0x2fd4     (52803a8b -> 5285fa8b)
 *
 * Nothing jumps into 0x8607d4..0x860804 (tools/into.py: the entries in use are
 * 0x860808, 0x860838, 0x860848, 0x860854), and each word stays the same
 * instruction on the same registers - only the bound and the mask change. The other
 * engine copies of the overlay (0x6a1e0, 0x24db1c, 0x43cd20) test a player's
 * array of weapon flags instead and never run - the overlay drawn is
 * Repentance's (the report's "sticks: shooting stick at manager+..." line).
 *
 * Called from loader.c with the image still writable, every word checked
 * first; a word that is not the expected one leaves the game's rule alone.
 */
#define AIMLOCK_BOUND_AT   0x8607e0
#define AIMLOCK_MASK_AT    0x8607f0
#define AIMLOCK_FREE_TYPES 0x2fd4      /* 0x1d4 | Tech X 9 | Bone 10 | Notched Axe 11 | Spirit Sword 13 */

void shim_sticks_aimlock_rule(unsigned char *base) {
    unsigned int *bound = (unsigned int *) (base + AIMLOCK_BOUND_AT);
    unsigned int *mask = (unsigned int *) (base + AIMLOCK_MASK_AT);
    const unsigned int bound_was = 0x7100213Fu, bound_now = 0x71003D3Fu;   /* cmp w9, #8 / #15 */
    const unsigned int mask_was = 0x52803A8Bu, mask_now = 0x5285FA8Bu;     /* mov w11, #0x1d4 / #0x2fd4 */
    const char *how;
    if (*bound == bound_now && *mask == mask_now) {
        how = "already";
    } else if (*bound == bound_was && *mask == mask_was) {
        *bound = bound_now;
        *mask = mask_now;
        how = "patched";
    } else {
        how = NULL;
    }
#if ISAAC_DIAG
    char line[160];
    if (how)
        snprintf(line, sizeof(line), "Tech X, the bone, the axe and the spirit sword fire on release (%s, mask %#x)",
                 how, AIMLOCK_FREE_TYPES);
    else
        snprintf(line, sizeof(line), "the game's rule left alone: %08x at %x, %08x at %x",
                 *bound, AIMLOCK_BOUND_AT, *mask, AIMLOCK_MASK_AT);
    LOG("aim lock: %s", line);
    if (shim_report_active()) report_line("aimlock rule", line);
#else
    (void) how;
#endif
}

/*
 * AIM LOCK, WATCHED (P2, report builds only). What the byte does, read off the
 * image: the overlay's update (0x86077c..0x860854) picks the shooting knob's
 * animation from it and the weapon, and writes the shooting stick's flag
 * +0x1c9 (the overlay's +0x509): 0 is "locked" - aim lock on and a tear weapon
 * (type 1) without Cursed Eye (316) or Chocolate Milk (69), or a weapon of
 * type 0, 3, 5, 12, 14 or 15 (with shim_sticks_aimlock_rule; before it, every
 * type above 8) - and then "Analog Stick Shoot Lock" is played. The stick's
 * own update (0x862f04) reads the flag when the finger comes up
 * (0x86314c..0x863188): with 0 it does NOT zero its value, so the player goes
 * on firing the last way set - the iPhone's behaviour the owner describes.
 *
 * So the report says, on every change: the byte, the weapon's type, the flag,
 * the knob's animation and the one the image would pick; and on every lift of
 * the shooting finger in a run, whether the value stayed (the fire goes on) or
 * went to nought. If the byte is 1, the weapon is tears and the value still
 * drops, something of ours stands in the way - and this names the moment.
 */
#if ISAAC_DIAG
static void aimlock_watch(unsigned long b) {
    if (!shim_report_active()) return;
    static int tick, said, was_byte = -1, was_flag = -1, was_type = -2, was_held = -1;
    static char was_anim[48];
    if (++tick < 15) return;
    tick = 0;
    unsigned long m = *(const unsigned long *) (b + 0xac2428);
    if (!m || !g_readable) return;
    const unsigned char *ov = (const unsigned char *) (m + OVERLAY_IN_MANAGER);
    const unsigned char *shoot = ov + STICK_SHOOT;
    if (!g_readable(ov, 0x520) || !g_readable(shoot, 0x200)) return;
    int byte = ((const unsigned char *) m)[0x55f15];
    int flag = ov[0x509];
    int held = shoot[STICK_HELD];
    const float *val = (const float *) (shoot + 0x194);
    char anim[48] = "?";
    const void *const *ap = (const void *const *) (ov + 0x350 + 0x38);
    if (g_readable(ap, 8) && *ap && g_readable(*ap, 24)) {
        const char *a = shim_str_bytes(*ap);
        if (a) snprintf(anim, sizeof(anim), "%.47s", a);
    }
    /* the first player's weapon, as 0x8607d4..0x8607dc reaches it: player +0x1760, then +0x38 */
    int type = -1;
    unsigned long G = *(const unsigned long *) (b + 0xac3b90);
    const unsigned long *list = G ? (const unsigned long *) (G + 0x25c50) : NULL;
    if (list && g_readable(list, 16) && list[0] && list[1] > list[0] && g_readable((const void *) list[0], 8)) {
        unsigned long player = *(const unsigned long *) list[0];
        if (player && g_readable((const void *) (player + 0x1760), 8)) {
            unsigned long weapon = *(const unsigned long *) (player + 0x1760);
            if (weapon && g_readable((const void *) (weapon + 0x38), 4)) type = *(const int *) (weapon + 0x38);
        }
    }
    const char *expect = !byte ? "Analog Stick Shoot"
                       : (type >= 0 && type <= 15 && ((1 << type) & AIMLOCK_FREE_TYPES)) ? "Analog Stick Shoot"
                       : type == 1 ? "Analog Stick Shoot Lock (unless Cursed Eye or Chocolate Milk)"
                       : type >= 0 ? "Analog Stick Shoot Lock" : "?";
    char line[300];
    if (said < 40 && (byte != was_byte || flag != was_flag || type != was_type || strcmp(anim, was_anim))) {
        said++;
        snprintf(line, sizeof(line), "byte %d, weapon type %d, shooting flag +0x1c9 %d (%s), knob \"%s\", "
                 "the image picks \"%s\"", byte, type, flag, flag ? "free" : "LOCKED", anim, expect);
        report_line("aimlock", line);
    }
    if (said < 40 && was_held == 1 && !held && in_game()) {
        said++;
        int on = val[0] != 0.0f || val[1] != 0.0f;
        snprintf(line, sizeof(line), "the shooting finger came up: value %.2f,%.2f - %s (byte %d, flag %d)",
                 (double) val[0], (double) val[1], on ? "the fire goes on" : "the fire stops", byte, flag);
        report_line("aimlock", line);
    }
    was_byte = byte; was_flag = flag; was_type = type; was_held = held;
    snprintf(was_anim, sizeof(was_anim), "%s", anim);
}
#endif

void shim_sticks_between_frames(void) {
    g_frame++;                      /* the stick hooks tell a hold that goes on from a new one by this */
    dash_watch();
    letgo_frame();                  /* a tap without a direction lets go of the aim lock */
    {   /* the game's capture circle follows the stick kind and TOUCH MODE (loader.c) */
        void shim_sticks_capture_for(int kind, int buttons);
        static int was = -1;
        int now = g_kind * 2 + (buttons_mode() ? 1 : 0);
        if (now != was && shim_image_base()) { was = now; shim_sticks_capture_for(g_kind, now & 1); }
    }
    unsigned long b = shim_image_base();
    if (!b) return;
    /* options.ini before our restarts, the AIM LOCK row saved when pressed (saves.c) */
    { void shim_saves_between_frames(void); shim_saves_between_frames(); }
#if ISAAC_DIAG
    aimlock_watch(b);
    stick_watch(b);                 /* stickcurve:, sens:, zero-hold:, digital:, knob:, capture: */
#endif
    {   /* the file changed under us: read it again (once in two seconds is often enough) */
        static int tick;
        if (++tick >= 120) {
            tick = 0;
            struct stat st;
            if (g_path[0] && shim_safe_pending_path(g_path)) {
                /* our own write on its way to the disk: not an edit from outside, looked at next time */
            } else if (g_ini_time_owed) {
                note_ini_time();                /* it has landed: its time is ours */
            } else if (g_path[0] && stat(g_path, &st) == 0 && ini_changed(&st)) g_reload_asked = 1;
            if (g_path[0] && !g_loaded) g_reload_asked = 1;     /* not read yet: try again */
        }
    }
    if (g_reload_asked && g_path[0]) {
        g_reload_asked = 0;
        char dir[sizeof(g_path)];
        snprintf(dir, sizeof(dir), "%s", g_path);
        char *slash = strrchr(dir, '/');
        if (slash) {
            *slash = 0;
            int pct = g_pct;
            shim_sticks_load(dir);
            shim_sticks_apply_mode(g_kind);
            if (g_pct != pct) shim_sticks_size_changed();
            LOG("sticks: port-touch.ini read again after an edit outside");
        }
    }

    /*
     * The canvas differs between a run and the menus (screen.c), so a change
     * of state asks for the same rebuild the FULLSCREEN row asks for. Only a
     * real change, and only once: the state is read every frame anyway.
     */
    {
        /*
         * The canvas follows the PICTURE, not the play: a stage transition
         * keeps the run's canvas, so nothing is rebuilt under it.
         */
        int shim_sticks_run_canvas(void);
        static int was = -1;
        /*
         * And a cutscene's picture is its own: there is no state to compare
         * while one plays, and nothing to swap under it (screen.c). What the
         * picture was before the cutscene stays remembered, so whatever comes
         * after it is compared with the canvas the object still holds.
         */
        if (!shim_screen_cutscene()) {
            int now = shim_sticks_run_canvas();
            if (was >= 0 && now != was) g_refit_wanted = 1;
            was = now;
        }
    }
    /* the corner papers follow the display mode (screen.c); a no-op once set */
    { void shim_screen_papers(void); shim_screen_papers(); }
    {   /* once, as soon as the image is there: the bestiary's own slide is off */
        void shim_screen_no_slide(void);
        static int slid;
        if (!slid && shim_image_base()) { slid = 1; shim_screen_no_slide(); }
    }
    {
        /* the scene's multiplier changed: every canvas-sized surface is made afresh */
        int shim_screen_shot_changed(void);
        if (shim_screen_shot_changed()) g_refit_wanted = 1;
    }
    if (g_refit_wanted && !shim_screen_cutscene()) { g_refit_wanted = 0; shim_screen_refit(); }


    /*
     * WHAT WE RAISED, WE LOWER - from here, every frame, whether or not the
     * game is still updating the widgets.
     *
     * The opacity of a fixed stick is ours: the game never asks for it, and
     * nothing else takes it back. Lowering it inside the widget's own update
     * was not enough, because at the end of a run the game stops updating the
     * sticks while still drawing them - and that is exactly the moment they
     * were left lying over the menu. Here there is no such window.
     */
    {
        unsigned long m = *(const unsigned long *) (b + 0xac2428);
        unsigned long overlay = m ? m + OVERLAY_IN_MANAGER : 0;
        static const unsigned at[2] = { STICK_MOVE, STICK_SHOOT };
        int run = in_game();
        float alpha[2] = { -1.0f, -1.0f };
        int put[2] = { 0, 0 };
        for (int i = 0; overlay && g_readable && i < 2; i++) {
            unsigned long w = overlay + at[i];
            if (!g_readable((const void *) w, STICK_ALPHA + 4)) continue;
            float *a = (float *) (w + STICK_ALPHA);
            alpha[i] = *a;
            if (!run && *a != 0.0f) { *a = 0.0f; alpha[i] = 0.0f; put[i] = 1; }
        }
        /*
         * PROBE, diagnostic builds only. Three questions, and the report has to
         * answer all three or it answers none:
         *
         *   is this the right widget      - the line written when it is first met
         *   did we lower it               - "put down" below
         *   and is it DOWN yet drawn      - the opacity itself, whenever it moves
         *
         * Without the third, a report showing "put down" beside a screenshot of
         * sticks over the menu would leave the opacity unproven, and the next
         * guess would be as blind as the two before it.
         */
        int shim_report_active(void);
        void report_line(const char *, const char *);
        static int said;
        static float was[2] = { -2.0f, -2.0f };
        static int was_run = -1, was_gate = -1;
        int gate = (overlay && g_readable && g_readable((const void *) overlay, 5))
                 ? *(const unsigned char *) (overlay + 4) : -1;
        if (said < 16 && shim_report_active()
            && (alpha[0] != was[0] || alpha[1] != was[1] || run != was_run || gate != was_gate)) {
            said++;
            was[0] = alpha[0]; was[1] = alpha[1]; was_run = run; was_gate = gate;
            char line[180];
            snprintf(line, sizeof(line),
                     "opacity %.2f / %.2f, overlay drawn %d, in a run %d%s%s",
                     (double) alpha[0], (double) alpha[1], gate, run,
                     put[0] ? " | movement put down" : "",
                     put[1] ? " | shooting put down" : "");
            report_line("sticks", line);
        }
        /*
         * PROBE, diagnostic builds only: a pad at the start of a run. The owner
         * (17.09.2026): with a pad connected a new run walks in stutters until
         * the on-screen stick is touched once, and a pad pressed before the
         * stick has faded leaves it drawn over every menu. What decides which
         * device drives the player and whether a stick is drawn is logged here
         * on every change, so the two are read off the device, not guessed.
         */
        DIAG({
            int shim_pad_is_connected(void);
            unsigned shim_pad_buttons(void);
            float shim_pad_left_x(void);
            float shim_pad_left_y(void);
            static char was_line[256];
            static int lines;
            if (overlay && g_readable && lines < 400 && run
                && g_readable((const void *) overlay, 0x690)) {
                unsigned long tp = ((unsigned long (*)(void)) (b + 0x85fa3c))();
                unsigned long chosen = *(const unsigned long *) (b + 0xac2490);
                if (chosen && !g_readable((const void *) chosen, 0x40)) chosen = 0;
                int ctl = tp ? *(const int *) (tp + 0x19ec) : -1;
                char line[256];
                int n = snprintf(line, sizeof(line),
                    "padprobe run %d pad %d btn %x lx %d ly %d | overlay mode %d live %d dev %#x"
                    " | chosen %d dev %#x | touch player %d ctl %#x | blocks %d",
                    run, shim_pad_is_connected(), shim_pad_buttons(),
                    (int) roundf(shim_pad_left_x()), (int) roundf(shim_pad_left_y()),
                    *(const int *) (overlay + 8), *(const unsigned char *) (overlay + OVERLAY_LIVE),
                    *(const unsigned *) overlay,
                    /* the dispatcher's object (0xac2490): its +0x38 and the device at +0x34 that
                       0x860880 compares with the overlay's own (0x8608c0's first branch) */
                    chosen ? *(const int *) (chosen + 0x38) : -99, chosen ? *(const unsigned *) (chosen + 0x34) : 0xdeadu,
                    tp != 0, (unsigned) ctl, pad_blocks_touch());
                for (int i = 0; i < 2 && n > 0 && n < (int) sizeof(line); i++) {
                    unsigned long w = overlay + at[i];
                    if (!g_readable((const void *) w, STICK_ALPHA + 4)) continue;
                    n += snprintf(line + n, sizeof(line) - (size_t) n,
                                  " | %s flags %x live %d held %d alpha %.2f",
                                  i ? "shoot" : "move", *(const unsigned *) (w + 8),
                                  *(const unsigned char *) (w + STICK_LIVE),
                                  *(const unsigned char *) (w + STICK_HELD),
                                  (double) *(const float *) (w + STICK_ALPHA));
                }
                /* the fade alone must not spend the budget: the key leaves the alphas out */
                char key[256];
                snprintf(key, sizeof(key), "%d %d %d %d %d %d %d %d %d %d", run, shim_pad_is_connected(),
                         *(const int *) (overlay + 8), *(const unsigned char *) (overlay + OVERLAY_LIVE),
                         tp != 0, ctl, pad_blocks_touch(),
                         g_readable((const void *) (overlay + at[0]), 0x10) ? *(const unsigned char *) (overlay + at[0] + STICK_LIVE) : -1,
                         g_readable((const void *) (overlay + at[1]), 0x10) ? *(const unsigned char *) (overlay + at[1] + STICK_LIVE) : -1,
                         chosen != 0);
                if (strcmp(key, was_line)) {
                    strcpy(was_line, key);
                    lines++;
                    LOG("%s", line);
                    if (lines <= 200 && shim_report_active()) report_line("pad", line);
                }
            }
        });
    }

    /*
     * The defaults are put in here rather than in the menu: this runs every
     * frame from the first one, so they apply whether or not the player ever
     * opens the settings.
     */
    apply_our_defaults(*(unsigned char **) (b + 0xac2428));

    if (!g_resize_wanted) return;
    g_resize_wanted = 0;

    /*
     * The sprite's own load, the one the overlay builder itself uses. It asks
     * the store for the file - and asks with force, so the store reads it
     * again rather than handing back what it parsed at startup, which is what
     * makes the new size appear. The animation being played is put back after:
     * loading leaves the sprite on the file's first one.
     */
    void (*load)(unsigned long, const void *, int) =
        (void (*)(unsigned long, const void *, int)) (b + GAME_SPRITE_LOAD);
    void (*play)(unsigned long, const char *, int) =
        (void (*)(unsigned long, const char *, int)) (b + GAME_SPRITE_PLAY);

    static const unsigned where[2] = {0x10, 0x1d0};      /* the knob and the ring */
    int n = 0;
    for (int i = 0; i < 2; i++) {
        if (!g_seen[i]) continue;
        for (int part = 0; part < 2; part++) {
            unsigned long sprite = g_seen[i] + where[part];
            char name[64];
            name[0] = 0;
            const void **anim = (const void **) (sprite + 0x38);
            if (!g_readable || (g_readable(anim, 8) && *anim && g_readable(*anim, 24))) {
                const char *now = shim_str_bytes(*anim);
                if (now && *now) snprintf(name, sizeof(name), "%.63s", now);
            }
            /*
             * The variant is asked for by its own name, not through the one
             * the game uses. The store keeps what it parsed under the name it
             * was given, so asking again for "iOS_Buttons.anm2" brings back
             * the frames of the size that was chosen at startup, however the
             * file behind that name has changed since. A different name is a
             * different key, and the store has to read it.
             *
             * That is why 100% has a file of its own too - ios_buttons_100.anm2,
             * a copy of the original. It used to be asked for under the game's
             * own name, and that is exactly the name the store already holds:
             * switching to SMALL left the sticks at whatever size the launch
             * had loaded, which is why SMALL and MEDIUM looked the same.
             */
            char want[160];
            snprintf(want, sizeof(want), "gfx/ui/touch/ios_buttons_%d.anm2", g_pct);
            long long raw[3];
            unsigned char *path = (unsigned char *) raw;
            path[23] = 0;
            shim_str_assign(path, want);
            load(sprite, path, 1);
            shim_str_dtor(path);
            if (name[0]) play(sprite, name, 1);
            n++;
        }
    }
    LOG("sticks: size %d%% applied to %d sprites", g_pct, n);
}


/*
 * The size and why it still needs a restart. Everything below was measured on
 * the device; it is written down so the next attempt starts from here.
 *
 * WHERE THE SIZE LIVES. Not in the objects. The stick and both of its sprites
 * were dumped word by word at 100% and at 150% - two runs, 256 words each -
 * and the only fields that differ are the ones we write ourselves (the place
 * at +0x1b8, the travel at +0x1c0/1c4) and pointers that simply land at other
 * addresses. The same at one level deeper, in what the sprite points at from
 * +0x30 and +0x60: pointers only, not a single float. The scale is in the
 * parsed animation, further in than a couple of dereferences.
 *
 * WHAT DOES NOT WORK. Writing the sprite's own scale: the triples of ones at
 * +0xe8 and +0x114 are not it - written 1.5, the picture did not grow, checked
 * by measuring the ring in the frame.
 *
 * THE LOADER IS FOUND, THE CALL IS NOT. The game keeps three editions of the
 * touch sticks, each with its own loader, and the Repentance one is the one in
 * use. Its constructor at 0x43e938:
 *
 *   sprite  = stick + 0x10          (its file name is the string at +0x00)
 *   0x3edd88  load(sprite, std::string const &path, bool)
 *   0x89e82c  play(stick, char const *animation, bool)
 *
 * The other editions load through 0x9cfcc and 0x28c7d4, and 0x9cfcc takes a
 * char pointer rather than a string - calling it cost several crashes before
 * the difference was noticed.
 *
 * Calling 0x3edd88 on a live sprite - with the name emptied first, so the
 * loader takes the same branch it takes for a fresh one, and from the swap
 * between frames rather than mid-draw - still dies inside the game, at
 * 0x45c3d8: `ldp x23, x8, [x0, #0x60]` with x0 = 0x329d0, a number where a
 * pointer belongs. That code is a utility called from 119 places, so what is
 * missing is the state around the load, not the call itself: the constructor
 * does more than load - it builds a render target for the button right after
 * (0x8de4e4, "Touch Button Render Target").
 *
 * SO THE NEXT STEP is to follow the constructor to the end and repeat what it
 * does around the load, not just the load; or to find who fills the object the
 * utility at 0x45c3bc reads, and see what it is bound to.
 *
 * Until then the size is chosen when the file is opened - once per launch - so
 * a change in the menu is seen after restarting the game.
 */

/*
 * THE D-PADS: TOUCH MODE's third value, BUTTONS (the owner's item 19, 23.09.2026).
 *
 * The native row has ANALOGUE (1) and DIGITAL (2); the game reads it in one
 * place, the stick's update (0x863084), and all DIGITAL does there is drop a
 * pull shorter than 0.85 and make any other a full one. BUTTONS (3) is ours:
 * the menu's tap now steps 1 -> 2 -> 3 -> 1 (loader.c, touch_mode_tap), the
 * settings reader keeps the number as it is (0x7eb9d8, no clamp), and to the
 * game's stick a 3 is simply "not digital".
 *
 * Nothing new takes the fingers. A D-pad IS the stick: the same widget, the
 * same seat, the same capture, the same travel. Two things change.
 *
 * WHAT THE STICK IS TOLD. A finger of a D-pad is moved, before the game sees
 * it, onto one of eight points: the middle's own if it is within the D-pad's
 * centre square, otherwise a full travel out in the nearest of the eight
 * directions, 45 degrees each. The game then reads an exact direction at full
 * pull - eight ways, like a pad's D-pad, for moving and for shooting alike. A
 * static D-pad takes a finger by the game's own rule (four travels from the
 * seat) and its middle is the seat, so a tap on an arm is a push at once; a
 * moving one is seated under the finger by the game, and it is claimed once
 * the game has taken the finger (held, +0x168, grabbed where it landed,
 * +0x180), its middle being that landing.
 *
 * WHAT IS DRAWN. The ring sprite (+0x1d0) plays "Analog DPad Move/Shoot" and
 * the frame of the direction held (0 still, 1..8 from up round clockwise); the
 * knob (+0x10) plays an animation with every layer hidden. Both are set at
 * the door of the stick's draw (the '#' hook at 0x861ea0), after the overlay's
 * update - which plays the shooting knob's own animation afresh every frame
 * (0x860860) - and before anything is drawn. The pictures are the native
 * DPad's, in ios_buttons.png and every size variant (tools/dpad_art.py).
 */
#define TOUCH_MODE_FIELD   0x55f18     /* Manager: ANALOGUE 1, DIGITAL 2, BUTTONS 3 */
#define TOUCH_MODE_BUTTONS 3
#define STICK_HELD         0x168
#define STICK_GRAB         0x180
#define STICK_RING         0x1d0
#define STICK_KNOB         0x10
/*
 * THE MIDDLE IS A POINT, NOT A SQUARE (the owner's eighth round, 24.09.2026:
 * "far too big a dead zone - a D-pad needs none"). The whole 18-texel centre
 * square used to be "still", and a thumb resting a little off the middle did
 * nothing. Now only a thumb within 5 texels of the middle stands; anywhere else
 * the direction is read at once.
 *
 * And a static D-pad takes only what is drawn: its plus, the corners' arrows
 * - the picture, measured in texels at 100% (tools/dpad_art.py). The capture of
 * four travels the sticks have reached well past the drawing, and the owner
 * found the pads "pressed far beyond their size".
 */
#define DPAD_CENTRE        5.0f        /* texels at 100%: the still middle */
#define DPAD_ARM_LONG      58.0f       /* an arm's reach from the middle */
#define DPAD_ARM_HALF      21.0f       /* an arm's half width */
#define DPAD_CORNER        37.0f       /* the diagonal arrows reach this far on both axes (measured
                                          on the atlas: every layer of "Analog DPad Move", 24.09) */
#define FN_SPRITE_FRAME    0x89eab8    /* Sprite::SetFrame(name, frame) */
#define DPAD_PENDING       10

int shim_pad_is_connected(void);
int shim_coop_touch_player(void);              /* coop.c: a player is on the touch screen */

/*
 * A pad puts the touch controls aside only while nobody plays on the screen.
 * With the game's co-op (coop.c) a pad can be the second player's while the
 * first stays on the screen, and the screen's sticks and D-pads must stay too.
 */
int shim_coop_players(void);                   /* coop.c: how many players the run has now */

/*
 * THE TOUCH CONTROLS STEP ASIDE WHEN NOBODY PLAYS ON THE SCREEN - whatever
 * took the player (the owner, 28.09.2026: "with the keyboard our sticks and
 * D-pads do not hide - look at how WE did it for the pad"). The game moves
 * player 0 to whichever device was pressed last (its dispatcher, 0x6733d8):
 * a pad, and just as well the keyboard, which is a device of its own (device
 * 0; the touch screen is 0x10dead). Once nobody is on the screen, the port's
 * D-pads and the static sticks' forced opacity must go, and come back when a
 * finger brings player 0 back. This used to ask "is a pad connected" first,
 * which the keyboard never is.
 *
 * A pad connected still counts on its own when the player list is empty for
 * a moment (a boss room's way in empties it), as before; an empty list with
 * no pad blocks nothing.
 */
static int pad_blocks_touch(void) {
    if (shim_coop_touch_player()) return 0;
    return shim_pad_is_connected() || shim_coop_players() > 0;
}

static int buttons_mode(void) {
    unsigned long b = shim_image_base();
    unsigned long m = b ? *(const unsigned long *) (b + 0xac2428) : 0;
    return m && *(const int *) (m + TOUCH_MODE_FIELD) == TOUCH_MODE_BUTTONS;
}

int shim_sticks_buttons(void) { return buttons_mode(); }

static int g_dpad_id[2] = {-1, -1};    /* the finger each D-pad holds */
static int g_dpad_dir[2];              /* the frame to show: 0 still, 1..8 the direction */
static float g_dpad_mid[2][2];         /* a moving D-pad's middle: where its finger landed */
static volatile int g_float_id[2] = {-1, -1};   /* a floating D-pad's finger, put on its point
                                                    where the frames are (shim_sticks_dpad_float) */
static int g_float_axis[2];            /* the axis a floating D-pad's diagonal leans to: 1 x, 2 y */
static struct { int used, id; float x, y; } g_dpad_wait[DPAD_PENDING];   /* moving: not claimed yet */

/* the frame for an angle: 1 up, 2 up-right, ... 8 up-left (y grows downwards) */
static int dpad_frame(float dx, float dy) {
    static const int by_octant[8] = {3, 4, 5, 6, 7, 8, 1, 2};   /* from "right", clockwise */
    float a = atan2f(dy, dx);
    int k = (int) floorf(a / (float) (M_PI / 4.0) + 0.5f);
    return by_octant[(k % 8 + 8) % 8];
}

static void dpad_point(int frame, float *dx, float *dy) {
    static const float ux[9] = {0, 0, 1, 1, 1, 0, -1, -1, -1};
    static const float uy[9] = {0, -1, -1, 0, 1, 1, 1, 0, -1};
    float n = (ux[frame] != 0.0f && uy[frame] != 0.0f) ? 0.70710678f : 1.0f;
    *dx = ux[frame] * n;
    *dy = uy[frame] * n;
}

static void dpad_say(const char *what, int i, int id, int frame) {
    if (!shim_report_active()) return;
    static int said;
    if (said >= 120) return;
    said++;
    char line[120];
    snprintf(line, sizeof(line), "%s: %s D-pad, finger %d, frame %d", what, i ? "shooting" : "movement",
             id, frame);
    report_line("dpad", line);
}

/*
 * WHAT ELSE THE OVERLAY HAS UNDER A POINT: its Buttons (+0x2020c middle,
 * +0x20214 size) and ItemButton zones (+0x1b4, +0x1bc, enabled +0x1b0), live
 * (+0xc) - the list the game walks (overlay+0x84f98, 0x860f24). A press the
 * D-pad refuses must not be taken from one of them.
 *
 * The point comes in the WINDOW's units, as a D-pad's finger is mapped
 * (finger_space), and the game judges these widgets in the PICTURE's: their
 * hit tests go through 0x86255c (0x863b14, 0x86402c; screen.c, "ONE FINGER,
 * TWO SPACES"). So the point is put into the picture first. With FULLSCREEN
 * off the two differ by up to 26 units on 20:9 and 4:3 and 37 on 21:9
 * (the batch 2 touch notes): a finger on a row button beside a static D-pad went to
 * the D-pad, or was refused and eaten, instead of reaching the button.
 */
#define OVERLAY_WIDGETS    0x84f98
#define VT_BUTTON          0xa90c40
#define VT_ITEMBUTTON      0xa90c98

static int on_overlay_widget(unsigned long b, unsigned long overlay, float wx, float wy) {
    float ux = shim_screen_overlay_to_area_x(wx), uy = shim_screen_overlay_to_area_y(wy);
    const unsigned long *list = (const unsigned long *) (overlay + OVERLAY_WIDGETS);
    if (!g_readable((const void *) list, 16)) return 0;
    unsigned long first = list[0], past = list[1];
    if (!first || past < first || past - first > 64 * 8) return 0;
    for (unsigned long at = first; at < past; at += 8) {
        unsigned long w = *(const unsigned long *) at;
        if (!w || !g_readable((const void *) w, 0x1c8)) continue;
        unsigned long vt = *(const unsigned long *) w - b;
        const float *c, *sz;
        if (vt == VT_BUTTON) {
            if (!g_readable((const void *) w, 0x20214 + 8)) continue;
            c = (const float *) (w + 0x2020c);
            sz = (const float *) (w + 0x20214);
        } else if (vt == VT_ITEMBUTTON) {
            if (!*(const unsigned char *) (w + 0x1b0)) continue;
            c = (const float *) (w + 0x1b4);
            sz = (const float *) (w + 0x1bc);
        } else {
            continue;
        }
        if (!*(const unsigned char *) (w + STICK_LIVE)) continue;
        if (!isfinite(c[0]) || !isfinite(c[1]) || !(sz[0] > 0.0f) || !(sz[1] > 0.0f)) continue;
        if (fabsf(ux - c[0]) <= sz[0] * 0.5f && fabsf(uy - c[1]) <= sz[1] * 0.5f) return 1;
    }
    return 0;
}

#define DPAD_GAME_CAPTURE  4.0f        /* the static stick's capture, travels (loader.c) */
static int g_dpad_refused[DPAD_PENDING];
static int g_dpad_refused_n;

static int dpad_refused(int id, int phase) {
    for (int k = 0; k < g_dpad_refused_n; k++) {
        if (g_dpad_refused[k] != id) continue;
        if (phase >= 3) g_dpad_refused[k] = g_dpad_refused[--g_dpad_refused_n];
        return 1;
    }
    return 0;
}

#define RING_SCALE         (STICK_RING + 0xdc)   /* Sprite +0xdc: its scale, two floats */

/*
 * The scale the game draws the stick's ring at, on one axis; one until it has
 * been drawn. The two differ with FULLSCREEN off: the draw writes area / window
 * for each axis on its own (0x861ec8..0x861f30), 0.41 across and 0.5 down on a
 * 20:9 phone - the D-pad's drawing is narrower than it is tall in the window's
 * units, and its test has to be too.
 */
static float ring_scale(unsigned long stick, int axis) {
    float v = *(const float *) (stick + RING_SCALE + (axis ? 4 : 0));
    if (!(v > 0.05f && v < 8.0f)) return 1.0f;
    static int said;
    if (said < 2 && shim_report_active()) {
        said++;
        char line[80];
        snprintf(line, sizeof(line), "the ring is drawn at %.3f of its texels", (double) v);
        report_line("dpad", line);
    }
    return v;
}

/*
 * Answers 0: not the D-pad's; 1: the D-pad's, and *x, *y are now its point;
 * 2: refused - inside the game's capture of a static D-pad, off its drawing,
 * and no other widget there: the game would take it as a stick press with its
 * raw position, beyond what is drawn (the owner: "pressed half as far again
 * as they are"), so nobody gets it.
 */
int shim_sticks_dpad(int id, int phase, float *x, float *y) {
    if (phase == 0 && g_dpad_raw_id == id) g_dpad_raw_id = -1;     /* Android hands the same ids out again */
    if (!buttons_mode() || !in_game() || pad_blocks_touch()) {
        g_dpad_refused_n = 0;
        g_dpad_id[0] = g_dpad_id[1] = -1;
        g_float_id[0] = g_float_id[1] = -1;
        g_dpad_dir[0] = g_dpad_dir[1] = 0;
        return 0;
    }
    unsigned long b = shim_image_base();
    unsigned long m = b ? *(const unsigned long *) (b + 0xac2428) : 0;
    if (!m || !g_readable) return 0;
    unsigned area_buf[4]; const unsigned *area = finger_space(b, area_buf);
    float fw = FIELD_W, fh = FIELD_H;
    if (!area[0] || !area[1] || !(fw > 0.0f) || !(fh > 0.0f)) return 0;
    float aw = (float) area[0], ah = (float) area[1], ax = (float) area[2], ay = (float) area[3];
    float ux = (*x - ax) * fw / aw, uy = (*y - ay) * fh / ah;
    float travel = 16.0f * size_factor() * g_reach;
    unsigned long w[2] = { m + OVERLAY_IN_MANAGER + STICK_MOVE, m + OVERLAY_IN_MANAGER + STICK_SHOOT };
    for (int k = 0; k < 2; k++)
        if (!g_readable((const void *) w[k], STICK_ALPHA + 4)) return 0;
    /*
     * THE STICKS' OWN WORD (the owner's eleventh round,
     * 25.09.2026: with the D-pads the options menu's BACK could not be
     * pressed). "In a run" stays true under the pause menu and its options;
     * the game's sticks take nothing there - the overlay's live byte and the
     * stick's own are down (stick_taking, 0x8603d8, 0x861b5c) - and neither
     * do the D-pads now: no claim, no refusal, and whatever they held is let go.
     */
    if (!stick_taking(w[0]) && !stick_taking(w[1])) {
        g_dpad_refused_n = 0;
        g_dpad_id[0] = g_dpad_id[1] = -1;
        g_float_id[0] = g_float_id[1] = -1;
        g_dpad_dir[0] = g_dpad_dir[1] = 0;
        for (int k = 0; k < DPAD_PENDING; k++) g_dpad_wait[k].used = 0;
        return 0;
    }

    if (phase != 0 && dpad_refused(id, phase)) return 2;
    int i = g_dpad_id[0] == id ? 0 : (g_dpad_id[1] == id ? 1 : -1);
    if (phase == 0 && i < 0) {
        /* the overlay's buttons and zones come first: the game offers them a finger before a stick (0x860f24) */
        if (on_overlay_widget(b, m + OVERLAY_IN_MANAGER, ux, uy)) return 0;
        if (g_kind == KIND_STATIC) {
            /*
             * The D-pad under the finger, by its drawing: the plus and the corners.
             * A texel is not a unit of the field: the stick's draw scales its
             * sprites by the screen (0x861ec0..0x861f58: area / reference * 0.5 /
             * [0xa9c7c8], written into the ring's scale, +0x1d0 + 0xdc), on top
             * of the size variant's own. Without it the test stood half as large
             * again as the picture (the owner's tenth round, 24.09.2026).
             */
            float best = 1e9f;
            for (int k = 0; k < 2; k++) {
                const float *seat = (const float *) (w[k] + STICK_SEAT);
                float sfx = size_factor() * ring_scale(w[k], 0), sfy = size_factor() * ring_scale(w[k], 1);
                float ax = fabsf(ux - seat[0]) / sfx, ay = fabsf(uy - seat[1]) / sfy;
                int on = (ax <= DPAD_ARM_LONG && ay <= DPAD_ARM_HALF)
                      || (ay <= DPAD_ARM_LONG && ax <= DPAD_ARM_HALF)
                      || (ax <= DPAD_CORNER && ay <= DPAD_CORNER);
                float d = hypotf(ax, ay);
                if (on && d < best) { best = d; i = k; }
            }
            if (i < 0) {
                unsigned long overlay = m + OVERLAY_IN_MANAGER;
                for (int k = 0; k < 2; k++) {
                    const float *seat = (const float *) (w[k] + STICK_SEAT);
                    if (hypotf(ux - seat[0], uy - seat[1]) >= DPAD_GAME_CAPTURE * travel) continue;
                    if (on_overlay_widget(b, overlay, ux, uy)) return 0;
                    if (g_dpad_refused_n < DPAD_PENDING) g_dpad_refused[g_dpad_refused_n++] = id;
                    dpad_say("refused, off the drawing", k, id, 0);
                    return 2;
                }
                return 0;
            }
            g_dpad_id[i] = id;
        } else {
            /* seated under the finger by the game; claimed when it has taken it */
            for (int k = 0; k < DPAD_PENDING; k++)
                if (!g_dpad_wait[k].used || g_dpad_wait[k].id == id) {
                    g_dpad_wait[k].used = 1;
                    g_dpad_wait[k].id = id;
                    g_dpad_wait[k].x = ux;
                    g_dpad_wait[k].y = uy;
                    break;
                }
            return 0;
        }
    }
    if (i < 0 && g_kind != KIND_STATIC) {
        int slot = -1;
        for (int k = 0; k < DPAD_PENDING; k++) if (g_dpad_wait[k].used && g_dpad_wait[k].id == id) slot = k;
        if (slot < 0) return 0;
        if (phase >= 3) { g_dpad_wait[slot].used = 0; return 0; }
        for (int k = 0; k < 2 && i < 0; k++) {
            const unsigned char *o = (const unsigned char *) w[k];
            const float *grab = (const float *) (w[k] + STICK_GRAB);
            if (o[STICK_HELD] && g_dpad_id[k] < 0
                && fabsf(grab[0] - g_dpad_wait[slot].x) < 1.0f && fabsf(grab[1] - g_dpad_wait[slot].y) < 1.0f)
                i = k;
        }
        if (i < 0) return 0;
        g_dpad_id[i] = id;
        g_dpad_mid[i][0] = g_dpad_wait[slot].x;
        g_dpad_mid[i][1] = g_dpad_wait[slot].y;
        g_dpad_wait[slot].used = 0;
    }
    if (i < 0) return 0;

    /*
     * A floating D-pad goes on raw: its middle travels, and it travels inside
     * the stick itself, which only the game thread may touch. The finger is
     * put on its point there, as it is handed to the game (shim_sticks_dpad_float).
     */
    if (g_kind == KIND_FLOATING) {
        g_float_id[i] = id;
        if (phase >= 3) g_dpad_id[i] = -1;
        return 1;
    }

    const float *seat = (const float *) (w[i] + STICK_SEAT);
    float mx = g_kind == KIND_STATIC ? seat[0] : g_dpad_mid[i][0];
    float my = g_kind == KIND_STATIC ? seat[1] : g_dpad_mid[i][1];
    float dx = ux - mx, dy = uy - my;
    if (i == 1) {   /* the raw finger against the middle, for the tap that lets go of the aim lock */
        float far = hypotf(dx, dy);
        if (g_dpad_raw_id != id) { g_dpad_raw_far = far; g_dpad_raw_id = id; }
        else if (far > g_dpad_raw_far) g_dpad_raw_far = far;
    }
    int frame = hypotf(dx, dy) < DPAD_CENTRE * size_factor() ? 0 : dpad_frame(dx, dy);
    float px, py;
    dpad_point(frame, &px, &py);
    ux = mx + px * travel;
    uy = my + py * travel;
    if (frame != g_dpad_dir[i] || phase == 0 || phase >= 3)
        dpad_say(phase == 0 ? "pressed" : (phase >= 3 ? "let go" : "turned"), i, id, frame);
    g_dpad_dir[i] = phase >= 3 ? 0 : frame;
    if (phase >= 3) g_dpad_id[i] = -1;
    *x = ux * aw / fw + ax;
    *y = uy * ah / fh + ay;
    return 1;
}

/*
 * THE FLOATING D-PAD (the owner, 27.09.2026: "floating D-pads are just the
 * dynamic ones"). They were: the finger was put on one of nine points around
 * the spot it landed on, a travel away at most, so the game's own float fork
 * (0x86300c: the pull beyond the travel drags the grab +0x180 and the drawn
 * centre +0x1b8 along) never saw a pull long enough to act on.
 *
 * Now the floating kind does what the fork does for a stick, with the D-pad's
 * own reach: a thumb that goes past the end of the drawn arms drags the D-pad
 * after it by the excess, along the line from the middle to the thumb - so the
 * thumb stays where it was on the picture and the direction it presses holds.
 * The middle is the stick's own grab, moved in the same two fields the fork
 * moves, and the finger is then put on its point from there - the pull is
 * exactly one travel, so the fork itself stays still and the game reads a
 * clean eight-way direction.
 *
 * Called for every finger as it is handed to the game (uikit.c,
 * shim_touch_deliver): the game thread, right before the overlay's update,
 * which is the only place the stick's fields may be written.
 */
void shim_sticks_dpad_float(int id, int phase, float *x, float *y) {
    int i = g_float_id[0] == id ? 0 : (g_float_id[1] == id ? 1 : -1);
    if (i < 0) return;
    if (phase >= 3) g_float_id[i] = -1;
    if (g_kind != KIND_FLOATING || !buttons_mode() || !g_readable) { g_float_id[i] = -1; return; }
    unsigned long b = shim_image_base();
    unsigned long m = b ? *(const unsigned long *) (b + 0xac2428) : 0;
    if (!m) return;
    unsigned area_buf[4]; const unsigned *area = finger_space(b, area_buf);
    float fw = FIELD_W, fh = FIELD_H;
    if (!area[0] || !area[1] || !(fw > 0.0f) || !(fh > 0.0f)) return;
    float aw = (float) area[0], ah = (float) area[1], ax = (float) area[2], ay = (float) area[3];
    unsigned long w = m + OVERLAY_IN_MANAGER + (i ? STICK_SHOOT : STICK_MOVE);
    if (!g_readable((const void *) w, STICK_ALPHA + 4)) return;
    float *grab = (float *) (w + STICK_GRAB);
    float *seat = (float *) (w + STICK_SEAT);
    float ux = (*x - ax) * fw / aw, uy = (*y - ay) * fh / ah;
    float dx = ux - grab[0], dy = uy - grab[1];
    float dist = hypotf(dx, dy);
    /*
     * The D-pad follows once the thumb is a travel from its middle - the
     * stick's own rule (the fork drags at +0x1c0), so it trails the thumb as
     * closely as a floating stick's ring does. It first followed only past the
     * drawn arms' end, nearly twice as far at 125%, and stood still until then:
     * "it does not keep up with the finger" (the owner, 27.09.2026).
     */
    float travel = 16.0f * size_factor() * g_reach;
    float reach = travel;
    if (dist > reach && phase < 3) {
        float k = (dist - reach) / dist;
        float ex = dx * k, ey = dy * k;
        grab[0] += ex;
        grab[1] += ey;
        seat[0] += ex;
        seat[1] += ey;
        dx -= ex;
        dy -= ey;
    }
    int frame = hypotf(dx, dy) < DPAD_CENTRE * size_factor() ? 0 : dpad_frame(dx, dy);
    float px, py;
    dpad_point(frame, &px, &py);
    /*
     * A DIAGONAL MUST NOT BE AN EXACT TIE. The finger goes to the point itself
     * here, so a diagonal push was (0.7071, 0.7071) of the travel to the last
     * bit, and the game, choosing the way the character faces by the larger
     * component, chose differently from frame to frame: "the sprite thrashes
     * between two poses" (the owner, 27.09.2026). The static and dynamic
     * D-pads never tie, their middle is not the game's grab point to the bit.
     * So the axis the finger came from keeps the lead: the last straight
     * direction pressed names it, and on a diagonal the other component is
     * shortened by a fiftieth - the pull is still full (it is divided by the
     * sensitivity and capped at one), the direction still 45 degrees to the eye.
     */
    if (frame == 1 || frame == 5) g_float_axis[i] = 2;
    else if (frame == 3 || frame == 7) g_float_axis[i] = 1;
    if (px != 0.0f && py != 0.0f) {
        if (g_float_axis[i] == 2) px *= 0.98f;
        else py *= 0.98f;
    }
    ux = grab[0] + px * travel;
    uy = grab[1] + py * travel;
    if (frame != g_dpad_dir[i] || phase >= 3)
        dpad_say(phase >= 3 ? "let go (floating)" : "turned (floating)", i, id, frame);
    g_dpad_dir[i] = phase >= 3 ? 0 : frame;
    *x = ux * aw / fw + ax;
    *y = uy * ah / fh + ay;
}

/* the pictures: the ring shows the D-pad and its direction, the knob nothing */
static void dpad_dress(unsigned long self, int is_move) {
    static int dressed[2];
    int i = is_move ? 0 : 1;
    unsigned long b = shim_image_base();
    if (!b) return;
    void (*frame)(unsigned long, const char *, int) =
        (void (*)(unsigned long, const char *, int)) (b + FN_SPRITE_FRAME);
    void (*play)(unsigned long, const char *, int) =
        (void (*)(unsigned long, const char *, int)) (b + GAME_SPRITE_PLAY);
    if (buttons_mode() && in_game()) {
        int f = g_dpad_dir[i];
        if (f < 0 || f > 8) f = 0;
        /*
         * THE LOCK'S FIRE SHOWS ON THE D-PAD (05.10.2026, with the tap that lets
         * it go: the research notes of that day, aim lock release). Under the aim
         * lock the stick
         * keeps firing after the finger is up (+0x194 kept, 0x863140), and the
         * knob shows it by staying where it was; the D-pad hides its knob and
         * went back to "still" at the lift, so the fire held and the fire let go
         * looked the same. With no finger on it, the shooting D-pad now shows the
         * way its value still points while the lock holds, and the middle once a
         * tap without a direction has let go.
         */
        if (!is_move && !f) {
            const unsigned char *o = (const unsigned char *) self;
            const float *v = (const float *) (self + STICK_VALUE);
            if (!o[STICK_FREE_FLAG] && !o[STICK_HELD] && (v[0] != 0.0f || v[1] != 0.0f)
                && isfinite(v[0]) && isfinite(v[1]))
                f = dpad_frame(v[0], v[1]);
        }
        /*
         * AIM LOCK SHOWS ON THE D-PAD TOO (the owner's 19th round, 03.10.2026:
         * "the D-pad has no padlock"). The knob shows the lock by the flag the
         * overlay's update writes this frame (+0x1c9, 0 = locked: 0x860854, in
         * the same branch that plays "Analog Stick Shoot Lock" at 0x860860), and
         * the hidden knob cannot show it here - so the ring plays the D-pad with
         * the knob's own padlock drawing in its middle (tools/dpad_art.py). Asked
         * once whether the file the game loaded has it (a mod's ios_buttons.anm2
         * gets the port's patch, but the answer is the file's, not ours).
         */
        const char *ring = is_move ? "Analog DPad Move" : "Analog DPad Shoot";
        if (!is_move && !((const unsigned char *) self)[0x1c9]) {
            static int has_lock = -1;
            if (has_lock < 0) {
                int shim_port_frames(const char *rel, const char *anim);
                has_lock = shim_port_frames("gfx/ui/touch/ios_buttons.anm2", "Analog DPad Shoot Lock") >= 9;
                LOG("sticks: the D-pad's padlock %s", has_lock ? "is drawn" : "is missing from ios_buttons.anm2");
            }
            if (has_lock) ring = "Analog DPad Shoot Lock";
        }
        frame(self + STICK_RING, ring, f);
        frame(self + STICK_KNOB, is_move ? "Analog DPad Blank Move" : "Analog DPad Blank Shoot", 0);
        if (!dressed[i]) {
            dressed[i] = 1;
            LOG("sticks: the %s stick is drawn as a D-pad", is_move ? "movement" : "shooting");
        }
    } else if (dressed[i]) {
        dressed[i] = 0;
        /*
         * What the stick's own constructor plays (0x862440..0x862494): the KNOB
         * (+0x10) "Analog Stick", then the overlay's "Analog Stick Move/Shoot"
         * (0x85f4d8, 0x860800); the RING (+0x1d0) "Analog Bounds". This used to
         * give the ring "Analog Stick" - the knob's own disc in place of the
         * dark bounds - and the sticks came back from BUTTONS pale and without
         * their backing (the owner, 27.09.2026).
         */
        play(self + STICK_RING, "Analog Bounds", 1);
        play(self + STICK_KNOB, is_move ? "Analog Stick Move" : "Analog Stick Shoot", 1);
        LOG("sticks: the %s stick is a stick again", is_move ? "movement" : "shooting");
    }
}

void shim_sticks_pin(unsigned long self, int (*readable)(const void *, unsigned long)) {
    g_readable = readable;
    /* we write as far as +0x328, so the check has to reach that far too */
    if (!readable((const void *) self, 0x330)) return;
    unsigned char *o = (unsigned char *) self;
    const void **knob = (const void **) (o + 0x10 + 0x38);
    if (!readable(knob, 8) || !readable(*knob, 24)) return;
    const char *name = shim_str_bytes(*knob);
    int is_move = strstr(name, "Move") != 0;
    g_seen[is_move ? 0 : 1] = self;
    int i = is_move ? 0 : 1;
    dpad_dress(self, is_move);
    /*
     * Into the report, once per stick: does the widget the game hands us sit
     * where the image says it does? The frame-loop lowering below relies on
     * exactly that, so it is checked rather than trusted.
     */
    {
        int shim_report_active(void);
        void report_line(const char *, const char *);
        static int said[2];
        unsigned long b2 = shim_image_base();
        unsigned long m = b2 ? *(const unsigned long *) (b2 + 0xac2428) : 0;
        if (!said[i] && m && shim_report_active()) {
            said[i] = 1;
            unsigned long want = m + OVERLAY_IN_MANAGER + (is_move ? STICK_MOVE : STICK_SHOOT);
            char line[160];
            snprintf(line, sizeof(line), "%s stick at manager+%#lx, the image says %#lx: %s",
                     is_move ? "movement" : "shooting", self - m, want - m,
                     self == want ? "same" : "DIFFERENT");
            report_line("sticks", line);
        }
    }
    float *home = (float *) (o + 0x1b8);
    int held = o[0x168] != 0;
    static int was_held[2];

    /*
     * A boss introduction hides the sticks - and lets go of the finger with
     * them. Measured: the byte at +0xc drops to nought while a finger is still
     * holding, and seventeen milliseconds later the hold is gone too. A shot
     * held at charge when the arena opens is lost that way, which is not what
     * the picture promises: the sticks go away for the look of it, not because
     * the player let go.
     *
     * The test for "this is only a cutscene" is the FINGER, and the first
     * attempt got that wrong: it asked whether a player still existed, and the
     * overlay's own answer comes from walking the player list (0x8608c0, which
     * compares player[0x19ec] with the device) - so during the transition into
     * a boss room the list is exactly what is empty, and the test failed at the
     * one moment it was for.
     *
     * A finger that is still holding cannot be the end of a run: there the
     * hold has gone with everything else. So while a run is going, nobody has a
     * pad, and this widget is still held, the serving is put back and the
     * opacity taken to nought in its place. When the finger lifts, we stop
     * putting anything back and the game has its widget again.
     *
     * It is done in two places, at the widget's own update and again between
     * frames, because the order inside a frame is not ours to know: the serving
     * goes out in one frame and the hold is dropped in the NEXT - sixteen
     * milliseconds later by the log - so there is a window, and this stands at
     * both ends of it.
     */
    if (g_kind == KIND_STATIC) {
        /* they stand where they were told and never move */
        place_by_inset(g_move, g_shoot);
        float want[2];
        seat_for(is_move, want);
        home[0] = want[0];
        home[1] = want[1];
        /*
         * We hold the opacity by force — otherwise a fixed stick fades out as
         * soon as it is released. But we let go wherever the game has hidden
         * the widget itself, and it says so in the byte at +0xc: the overlay
         * recomputes it every frame (0x8608c0) by walking the players and
         * asking whether its own device still drives one of them, then writes
         * `widget[0xc] = (kind & mask) != 0` at 0x8609a0. A pad taking over
         * clears it - and so does the end of a run, where the player list
         * empties and nothing is left for the overlay to serve. Holding the
         * opacity through that is what left the sticks lying on the ending.
         */
        if (g_visible && in_game() && !pad_blocks_touch() && o[0xc])
            *(float *) (o + 0x328) = 1.0f - fade_level();
        /*
         * And put it down again when there is no run. What we raise, we lower:
         * the opacity above is OURS, the game never asked for it, and nothing
         * else ever takes it back. After an ordinary run that went unnoticed
         * because the widget does not survive - after a challenge finished
         * successfully it does, and the sticks were left lying over the menu.
         *
         * Measured, not guessed: at the end of such a challenge the game empties
         * rep_gamestate1.dat, re-reads all three slots - that is the save screen
         * - and the state field settles at 1, which is "no run" exactly as the 3
         * it shows at startup. So the run does end and in_game() is right; the
         * only thing still holding the sticks up was this line.
         */
        else if (!in_game())
            *(float *) (o + 0x328) = 0.0f;
        /*
         * AND WHEN NOBODY PLAYS ON THE SCREEN ANY MORE (the owner, 28.09.2026:
         * "with the keyboard our sticks do not hide"). Measured against the
         * pad: the game hides nothing itself - with player 0 on a pad the
         * widgets stay live (mode 2, live 1) and only their opacity is nought,
         * because it was never raised: a pad connected from the start keeps
         * pad_blocks_touch true and the line above never runs. Player 0 taken
         * by the keyboard in the middle of a run left the opacity where we had
         * put it, at one, and nothing ever took it down. So it goes down here,
         * the moment the screen has no player - what we raise, we lower - and
         * comes back through the line above when a finger brings player 0
         * home. A stick a finger is holding keeps its opacity: that finger is
         * the player coming back.
         */
        else if (pad_blocks_touch() && !o[STICK_HELD])
            *(float *) (o + 0x328) = 0.0f;
    } else {
        /*
         * Dynamic and floating: the overlay seats the stick under the finger
         * itself, and it is seen only while it is held. What tells the two
         * apart is whether that seat then travels.
         *
         * The game travels it by itself, and this is the code that does it -
         * in the hold update, 0x862ff4 through 0x86303c. It takes the pull
         * from the point the finger landed on (+0x180), and as soon as the
         * pull is longer than the travel (+0x1c0) it adds the excess to BOTH
         * that point and the drawn centre (+0x1b8):
         *
         *     d = touch - grab;
         *     if (|d| > travel) { e = d / |d| * (|d| - travel);
         *                         grab += e;  centre += e; }
         *
         * So a stick left alone floats: the ring is dragged along, always a
         * travel behind the thumb. That is `floating`, and we do nothing for
         * it beyond letting the game be.
         *
         * `dynamic` must NOT travel, and there is nothing to do here for it:
         * the fork is branched over in the image (shim_sticks_apply_mode), so
         * the seat is written once, on the grab, and never again. We used to
         * hold the drawn centre from here as well - it held nothing, because
         * the image patch had already taken the movement away, and having two
         * anchors where one does the work is how the floating kind came out
         * looking exactly like the dynamic one.
         */
        if (in_game() && !held) *(float *) (o + 0x328) = 0.0f;
    }
    was_held[i] = held;

    /*
     * Size. The picture is enlarged by data rather than by code: the scale
     * lives in an animation frame, and the frames sit two pointers away from
     * the sprite. So two pre-enlarged variants live next to ios_buttons.anm2
     * and the right one is substituted by path (see shim_sticks_anm2). What is
     * left here is to stretch the stick's travel to match the new circle.
     */
    float k = size_factor();
    float travel = 16.0f * k * g_reach;
    if (k != 1.0f || g_reach != 1.0f) *(float *) (o + 0x1c0) = travel;
    /* the divisor is kept as a fraction of the travel, else it stays the native eight */
    float fraction = is_move ? g_sens : g_sens_shoot;
    *(float *) (o + 0x1c4) = travel * fraction;
    /*
     * Sensitivity. The game computes speed as the deflection divided by the
     * field at +0x1c4, capped at one (0x8bedbc divides the vector by that
     * scalar). The stick's constructor sets +0x1c0 and +0x1c4 BOTH to one with
     * a single `str d0, [x19, #0x1c0]`, while we overwrite the travel with 16
     * and more. If the divisor stays at one, full speed is reached at a
     * deflection of one unit out of sixteen — hence "the character is
     * uncontrollable". We print both fields for both sticks so as not to
     * guess.
     */
    DIAG(static int said[2]; if (said[i] < 2 && in_game()) { said[i]++;
         LOG("stick %s: travel %.2f, in the field %.2f, divisor %.2f (fraction %.2f)",
             is_move ? "movement" : "shooting", (double) travel,
             (double) *(const float *) (o + 0x1c0),
             (double) *(const float *) (o + 0x1c4), (double) fraction); });
    if (g_probe && is_move) {
        if (g_probe == 1) *(int *) (o + 0x08) = 6;          /* same as the shooting stick */
        else if (g_probe == 2) o[0x1c9] = 0;
        else if (g_probe == 3) *(float *) (o + 0x328) = 0.0f;   /* black out the whole stick */
        /*
         * A marker. In the GL snapshot four quads come through with opacities
         * of 0.196 and 0.500, and there is no telling whose stick is whose. We
         * dim the left one by a value of our own — its pair stands out at once
         * and the order inside each pair becomes visible. That answers the
         * whole question.
         */
        else *(float *) (o + 0x328) = 0.6f;
    }
}

/*
 * Which texture sheet each of the four sprites comes from.
 *
 * Both sticks submit in the same order — ring first, then knob — yet on screen
 * the left one is the other way round. So the batcher does the reordering, and
 * it reorders by material. A material is assembled during a layer's submission
 * out of two numbers: the sheet's flags and the output's own flags. Those are
 * what we read.
 *
 * We mark whose sprite went in for drawing and do the analysis at the layer's
 * submission: submission happens inside the sprite's draw, so the mark is
 * still valid.
 */
static const char *g_cur;


void shim_sticks_watch_draw(unsigned long sprite) {
    g_cur = 0;
    if (!in_game()) return;
    for (int i = 0; i < 2; i++) {
        if (!g_seen[i]) continue;
        if (sprite == g_seen[i] + 0x10)   g_cur = i ? "shooting/KNOB" : "movement/KNOB";
        else if (sprite == g_seen[i] + 0x1d0) g_cur = i ? "shooting/ring" : "movement/ring";
    }
    /*
     * Everything drawn in the overlay, not just the sticks themselves.
     *
     * Until now I assumed the left stick's knob was covered by its own ring.
     * That was never verified. So we print every sprite from the first stick
     * of the frame onwards along with its name: if something else lands on
     * top, it will show up here.
     */
    if (g_cur) g_overlay = 1;
#if ISAAC_DIAG
    /* ORDER PROBE */
    static int said, started;
    if (g_cur) started = 1;
    if (said >= 40) return;
    said++;
    const void **anim = (const void **) (sprite + 0x38);
    const char *nm = "?";
    if (probe_readable(anim, 8) && probe_readable(*anim, 24)) nm = shim_str_bytes(*anim);
    LOG("overlay %2d: %-18s GL calls %ld \"%.40s\"", said, g_cur ? g_cur : "— other —", g_gl_draws, nm);
#endif
}

void shim_sticks_watch_submit(unsigned long elem, unsigned long idx, unsigned long ctx) {
    if (!g_cur) return;
    static int told;
    if (told >= 0) return;   /* the material measurement is over */
    told++;
    /*
     * The layout comes from the submission itself: the sheet index sits at the
     * start of a layer record, the sheet table lives in the output context,
     * the record stride is 0xb0 and the flags are at +0x94.
     */
    int sheet = *(const int *) elem;
    const unsigned char *table = *(const unsigned char *const *) (ctx + 0xa8);
    if (!table || sheet < 0 || sheet > 4096) {
        LOG("material: %s — layer %lu, sheet %d (no table)", g_cur, idx, sheet);
        return;
    }
    const unsigned char *rec = table + (unsigned long) sheet * 0xb0;
    unsigned int sflags = *(const unsigned int *) (rec + 0x94);
    unsigned int cflags = *(const unsigned int *) (ctx + 0x150);
    LOG("material: %s — layer %lu, sheet %d, sheet flags %08x, output %08x, together %08x",
        g_cur, idx, sheet, sflags, cflags, sflags | cflags);
}

/*
 * A full comparison of the two sticks.
 *
 * Every earlier attempt compared the fields I myself considered important —
 * and every time the answer was "everything matches". So we compare BOTH
 * objects whole, word by word, and print only the differences. The sprites
 * live inside the object itself, so they end up in the comparison too.
 *
 * Some of the differences are legitimate: places on screen, pointers to their
 * own animations. Everything else is what we are after.
 */
#define STICK_SIZE 0x330

/*
 * Hunting for the scale field.
 *
 * The radius at +0x1c0 governs deflection, not the size of the picture — so
 * the size lives in the sprite itself. We print its floating-point fields:
 * both sprites are at 100%, so what to look for is a pair of ones in a row.
 */
void shim_sticks_scale_hunt(void) {
    static int done;
    if (done || !g_seen[0] || !in_game()) return;
    done = 1;
    {
        const unsigned char *o = (const unsigned char *) g_seen[0];
        /* every meaningful float of the stick itself: looking for 64, 128 or one */
        {
            char line[240]; int n = 0;
            for (unsigned q = 0x150; q < 0x330; q += 4) {
                float v = *(const float *) (o + q);
                if (v > 0.09f && v < 2000.0f) {
                    n += snprintf(line + n, sizeof(line) - (size_t) n, " +%03x=%.2f", q, (double) v);
                    if (n > 170) { LOG("stick fields:%s", line); n = 0; line[0] = 0; }
                }
            }
            if (n) LOG("stick fields:%s", line);
        }
        LOG("movement stick: radius +1c0 = %.2f, place +1b8 = %.1f,%.1f",
            (double) *(const float *) (o + 0x1c0),
            (double) ((const float *) (o + 0x1b8))[0], (double) ((const float *) (o + 0x1b8))[1]);
    }
    for (int part = 0; part < 2; part++) {
        unsigned long off = part ? 0x1d0 : 0x10;
        const char *what = part ? "ring" : "knob";
        const unsigned char *sp = (const unsigned char *) (g_seen[0] + off);
        char line[240];
        int n = 0;
        for (unsigned o2 = 0x90; o2 < 0x150; o2 += 4) {
            float v = *(const float *) (sp + o2);
            if (v > -1000.0f && v < 1000.0f && v != 0.0f)
                n += snprintf(line + n, sizeof(line) - (size_t) n, " +%02x=%.3f", o2, (double) v);
            if (n > 170) { LOG("scale %s:%s", what, line); n = 0; line[0] = 0; }
        }
        if (n) LOG("scale %s:%s", what, line);
    }
}

void shim_sticks_diff(void) {
    static int done;
    if (done || !g_seen[0] || !g_seen[1] || !in_game()) return;
    done = 1;
    const unsigned int *a = (const unsigned int *) g_seen[0];   /* movement */
    const unsigned int *b = (const unsigned int *) g_seen[1];   /* shooting */
    LOG("stick comparison: movement %lx, shooting %lx", g_seen[0], g_seen[1]);
    char line[200];
    int n = 0, shown = 0;
    for (unsigned off = 0; off < STICK_SIZE; off += 4) {
        unsigned i = off / 4;
        if (a[i] == b[i]) continue;
        if (++shown > 80) break;
        float fa = *(const float *) &a[i], fb = *(const float *) &b[i];
        n += snprintf(line + n, sizeof(line) - (size_t) n,
                      " +%03x:%08x/%08x", off, a[i], b[i]);
        /* the number looks like a sensible float — show it that way too */
        if ((fa > -1e6f && fa < 1e6f && fa != 0) || (fb > -1e6f && fb < 1e6f && fb != 0))
            n += snprintf(line + n, sizeof(line) - (size_t) n, "(%.2f/%.2f)", (double) fa, (double) fb);
        if (n > 140) { LOG("differences:%s", line); n = 0; line[0] = 0; }
    }
    if (n) LOG("differences:%s", line);
    LOG("stick comparison: %d differences in total", shown);
    /*
     * The stick objects matched — so what to look at is what they point to.
     * Every sprite keeps a pointer to its animation at its start; we compare
     * those records pairwise: knob against knob, ring against ring. Both
     * sticks' rings play the same animation, so there should be almost no
     * differences there — and whatever turns up will be telling.
     */
    for (int part = 0; part < 2; part++) {
        unsigned long off = part ? 0x1d0 : 0x10;
        const char *what = part ? "rings" : "knobs";
        const unsigned int *pa = *(const unsigned int *const *) (g_seen[0] + off);
        const unsigned int *pb = *(const unsigned int *const *) (g_seen[1] + off);
        if (!probe_readable(pa, 0x100) || !probe_readable(pb, 0x100)) {
            LOG("animations of the %s: unreadable", what);
            continue;
        }
        char line[200];
        int n = 0, cnt = 0;
        for (unsigned o2 = 0; o2 < 0x100; o2 += 4) {
            unsigned i = o2 / 4;
            if (pa[i] == pb[i]) continue;
            if (++cnt > 24) break;
            n += snprintf(line + n, sizeof(line) - (size_t) n, " +%02x:%08x/%08x", o2, pa[i], pb[i]);
            if (n > 140) { LOG("animations of the %s:%s", what, line); n = 0; line[0] = 0; }
        }
        if (n) LOG("animations of the %s:%s", what, line);
        LOG("animations of the %s: %d differences", what, cnt);
    }
}
