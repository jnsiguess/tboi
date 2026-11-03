/*
 * Resetting a run from the touch screen (a long press on the pause button) and
 * from a keyboard (holding the key bound to restart, R by default).
 *
 * The iOS edition has neither. On a pad a run is reset by holding the
 * shoulders and the triggers (0x88d988, below). On the PC it is reset by
 * holding R, and the iOS edition still carries that binding - the keyboard
 * device's action 23, R in the defaults at 0xa9c6e8 - but no code of the
 * Repentance engine ever asks for action 23, so on iOS R does nothing
 * (the research notes of 08.10.2026, the R key). A touch screen has no
 * equivalent at all -
 * which on a phone is where it is wanted most. So both ways in are ours;
 * everything that happens after them is the game's own: both hold the pad's
 * combination ("the reset itself", below), through ONE holder.
 *
 * What a reset from the pad really is (disassembled end to end, every link):
 *
 *   0x88d988, once per in-run update, polls the RAW pad buttons L1 L2 R1 R2
 *   (not bindable actions) and counts the updates they are all held; past 15
 *   it starts a fade of kind 5. 0x88dbd4 runs that fade, and when it completes,
 *   case 5 (0x88ddb0) does four things: zeroes persistent counter 0x16 (the
 *   win streak) through 0x701e28, spends one Eden token if the character is
 *   Eden or Tainted Eden (and refuses the whole gesture when there are none),
 *   copies the live game's Seeds (G+0x25d40) with 0x56fc54, and hands that copy
 *   to 0x672568(M, seeds, 0, 1), then destroys the copy with 0x56fcd4.
 *
 *   0x672568 is the only road to a fresh seed. It copies the Seeds into the run
 *   request (M+0x1f6b50), then 0x572488 -> 0x572410 draws a NEW start seed from
 *   the game's MT19937 into M+0x1f6b54 and rebuilds the request's generators
 *   from it, and raises the request flag. The next update serves the request
 *   the usual way: 0x671348 -> 0x88b144(G, who, what, seeds, extra), which
 *   takes the Seeds verbatim and never re-rolls.
 *
 *   Every other way of filing a request keeps the old seed: raising the flag
 *   by hand reuses whatever Seeds sit in the record; 0x672438 overwrites them
 *   from the continue snapshot; 0x672284 copies the Seeds it is given. All
 *   three were tried on the device and all three restarted the same seed.
 *
 *   This file used to perform case 5 itself. It no longer does: the long
 *   press holds the pad's own combination and the game walks the whole road,
 *   fade-out included (see "the reset itself" below, 23.09.2026). The
 *   character, the challenge and the rest of the request stay as the run that
 *   is being reset left them - which is what "keep the character" means.
 *
 * The gesture, and why the press is held back rather than watched:
 *
 *   The pause button answers a press, not a release. Letting the press through
 *   and only then deciding it was long would pause the game first and reset it
 *   second - two actions on one finger. So a press that lands on the button is
 *   held back while the decision is made. Let go early, and the press and its
 *   release are handed over together: the game pauses exactly as it always did,
 *   only later by the length of the tap. Held to the end, and the finger is
 *   eaten: the screen fades out and the run resets, and no pause ever happens.
 *   One finger, one action, and neither can overwrite the other. When the game
 *   itself would refuse the gesture (Eden with no tokens, a daily run), nothing
 *   happens - as on a pad. Lifted while the screen darkens, the game turns the
 *   fade back, as it does for a pad.
 *
 * Where the button is:
 *
 *   The game works out the button's place as it draws it and keeps it nowhere,
 *   so it is written down here - measured, in the units the game itself designs
 *   in (a field of 480 x 270), never in pixels. Those units are the same on
 *   every screen: the drawing area's size and origin, which the game keeps at
 *   0xabec64, turn them into pixels for whatever phone is running. The button
 *   does not move with the stick size setting either - the sticks and the
 *   button are unrelated - so there is nothing to scale here. With a pad
 *   connected the overlay is only hidden from sight; the button still answers
 *   a touch, so the gesture stays on then as well.
 */
#include <stdio.h>
#include <math.h>
#include <pthread.h>
#include <string.h>
#include <time.h>
#include <android/log.h>
#include "diag.h"

#if ISAAC_DIAG
#define LOG(...) __android_log_print(ANDROID_LOG_INFO, "isaac", __VA_ARGS__)
#else
#define LOG(...) do { } while (0)
#endif

unsigned long shim_image_base(void);
void shim_touch_push_raw(int id, int phase, float x, float y);
int shim_sticks_in_game(void);

/*
 * How long a press has to last, and how far the finger may drift.
 *
 * A reset throws the run away, so the gesture must not happen by accident: 700
 * ms is longer than any tap and shorter than anyone's patience. The drift is
 * generous on purpose - a thumb held still on glass still travels a few dozen
 * pixels, and a gesture that broke off then would only look broken.
 */
#define HOLD_MS 700
#define WANDER 48.0f

/*
 * Where the button is - asked of the game rather than measured.
 *
 * The overlay's buttons are IsaacRepentance::TouchOverlay::Button, and each
 * one carries the box it answers touches in: the centre at +0x2020c and the
 * full size at +0x20214, two floats each. Button::HitTest (0x863b1c) takes
 * them verbatim - centre minus half the size, plus the size - so reading the
 * same two fields puts our long press on exactly the button the game taps,
 * corner for corner, whatever the screen or the layout does with it.
 *
 * The button is found by its drawing (0x863ccc, `this` in x0): the sprite sits
 * at +0x10 and its current animation, a std::string at sprite+0x38, is named
 * "Pause Button". That is the same road sticks.c walks to tell the two sticks
 * apart.
 *
 * The measured numbers stay as the fallback, for the frames before the button
 * has been drawn once and for the case where the box reads as nonsense: the
 * bright box on a screenshot came to 2275..2367 by 122..215 pixels, and four
 * taps aimed at the button landed between 376.4 and 387.7 across and 30.5 and
 * 37.7 down.
 */
#define BUTTON_X 382.7f
#define BUTTON_Y 31.6f
#define BUTTON_HALF 8.2f

#define BUTTON_SPRITE 0x10       /* the button keeps its sprite here */
#define SPRITE_ANIM   0x38       /* the sprite's animation; its name is a std::string */
#define BUTTON_CENTRE 0x2020c    /* Vector2, as Button::HitTest reads it */
#define BUTTON_EXTENT 0x20214    /* Vector2: the whole width and height, not the half */

/* the drawing area, as the game keeps it: width, height, origin x, origin y */
#define AREA 0xabec64
#define CANVAS_H (shim_screen_height())

/*
 * The canvas is no longer a constant: it is as wide as the screen (screen.c),
 * so every place that used to say 480 asks the game what it is laying out
 * against right now.
 */
float shim_screen_width(void);
float shim_screen_height(void);
#define CANVAS_W (shim_screen_width())

/*
 * The state is touched from two threads: touches arrive on the layout thread,
 * the decision is made on the frame thread. Without a lock a finger let go at
 * the very moment the press turns long would be handed to the game as a tap
 * AND asked for a reset - both actions on one finger, which is the one thing
 * this gesture must never do. The lock is taken before the queue's own, always
 * in that order, so the two cannot deadlock.
 */
static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;

static int g_id = -1;                        /* the finger being held back */
static float g_down_x, g_down_y;
static long g_down_ms;
static int g_eaten;                          /* it became a reset */
static int g_let_go;                         /* it turned out to be a tap */

static long now_ms(void) {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (long) t.tv_sec * 1000 + t.tv_nsec / 1000000;
}

const char *shim_str_bytes(const void *s);

/*
 * The game's own pause button, caught as it draws itself. Written on the frame
 * thread, read on the layout thread; a single aligned pointer, and it only
 * ever goes from nothing to the one button, so the two cannot disagree.
 */
static unsigned long g_widget;

/* the pause button as last drawn, for the report's pause line (devices.c, U2); frame thread */
unsigned long shim_reset_pause_widget(void) { return g_widget; }
void shim_devices_pause_pressed(void);

/*
 * The button's box, COPIED.
 *
 * The pointer above is written by the frame thread and was read by the touch
 * thread, which then went into the game's own object for the numbers. That is
 * a race with teeth: between the read of the pointer and the read of the box
 * the frame thread can rebuild the touch overlay - the FULLSCREEN switch,
 * entering or leaving a run - and free the object under it. The numbers then
 * come out of freed memory, and the long press lands nowhere, at random,
 * until something is rebuilt again. Which is exactly what the owner saw.
 *
 * So the frame thread writes the four numbers here, every frame, and the touch
 * thread reads nothing but these. Four floats, each written and read whole; a
 * torn pair costs one missed press and the next frame puts it right.
 */
static volatile float g_box[4];         /* centre x, centre y, width, height */
static volatile int g_box_known;

void shim_reset_pin(unsigned long self, int (*readable)(const void *, unsigned long)) {
    /*
     * Found ONCE was wrong, and it is why the long press died at random.
     *
     * The touch overlay is built anew whenever the canvas changes - the
     * FULLSCREEN switch, and entering or leaving a run - and the pause button
     * that comes back is a DIFFERENT object at a different address. The old
     * pointer was kept anyway, so from then on the box was read out of freed
     * memory: either numbers that still looked sane and pointed at where the
     * button used to be, or nonsense, which fell back to the guessed
     * constants. Either way the press no longer landed on the button, and it
     * only came back if a rebuild happened to put the new button in the same
     * place - which is exactly the "sometimes it works again after a restart"
     * the owner saw.
     *
     * So the pointer is refreshed whenever a DIFFERENT object draws itself.
     * The cost of the check is one comparison a frame; the name is read only
     * when the object has actually changed.
     */
    /*
     * The box is refreshed every frame even when the object has not changed:
     * it moves with the canvas and with the place the player gave it in the
     * touch editor, and it costs four reads. (Not with the HUD offset: the
     * layout, 0x860538..0x86056c, does not read it.) Only the name check -
     * which walks a string - waits for the pointer to actually change.
     *
     * A moved button (hotfix 1.71): hud.c writes the place into +0x2020c at
     * this same draw and at the hit test, and trace.c calls this AFTER it, at
     * both - so what is copied here is the place the game's own tap answers
     * at, and the long press lands where the tap does.
     */
    if (!self) return;
    if (self == g_widget) {
        if (readable((const void *) self, BUTTON_EXTENT + 8)) {
            const float *c0 = (const float *) (self + BUTTON_CENTRE);
            const float *s0 = (const float *) (self + BUTTON_EXTENT);
            if (isfinite(c0[0]) && isfinite(c0[1]) && isfinite(s0[0]) && isfinite(s0[1])) {
                g_box[0] = c0[0]; g_box[1] = c0[1];
                g_box[2] = s0[0]; g_box[3] = s0[1];
                g_box_known = 1;
            }
        }
        return;
    }
    if (!readable((const void *) self, BUTTON_EXTENT + 8)) return;
    const void *const *anim = (const void *const *) (self + BUTTON_SPRITE + SPRITE_ANIM);
    if (!readable(anim, 8) || !*anim || !readable(*anim, 24)) return;
    const char *name = shim_str_bytes(*anim);
    if (!name || strcmp(name, "Pause Button") != 0) return;
    const float *c = (const float *) (self + BUTTON_CENTRE);
    const float *s = (const float *) (self + BUTTON_EXTENT);
    g_box[0] = c[0]; g_box[1] = c[1];
    g_box[2] = s[0]; g_box[3] = s[1];
    g_box_known = 1;
    g_widget = self;
    DIAG(LOG("reset: the pause button moved to %lx", self));
    /*
     * Into the report as well: the log this used to write is compiled out of
     * the build the testers run, so "did the button get found again" could not
     * be answered from a report at all.
     */
    {
        int shim_report_active(void);
        void report_line(const char *kind, const char *text);
        static int said;
        if (shim_report_active() && said < 8) {
            said++;
            char line[120];
            snprintf(line, sizeof(line), "the pause button is at %lx, centre %.1f,%.1f size %.1f x %.1f",
                     self, (double) c[0], (double) c[1], (double) s[0], (double) s[1]);
            report_line("reset", line);
        }
    }
    LOG("reset: the pause button is the game's own — centre %.2f,%.2f size %.2f x %.2f "
        "(we used to guess %.1f,%.1f half %.1f)",
        (double) c[0], (double) c[1], (double) s[0], (double) s[1],
        (double) BUTTON_X, (double) BUTTON_Y, (double) BUTTON_HALF);
}

/* the button's box in game units: the game's own if it has drawn itself yet */
static void button_box(float *cx, float *cy, float *hw, float *hh) {
    *cx = BUTTON_X; *cy = BUTTON_Y; *hw = BUTTON_HALF; *hh = BUTTON_HALF;
    if (!g_box_known) return;
    float c0 = g_box[0], c1 = g_box[1], s0 = g_box[2], s1 = g_box[3];
    if (!isfinite(c0) || !isfinite(c1) || !isfinite(s0) || !isfinite(s1)) return;
    if (c0 < 0.0f || c0 > CANVAS_W || c1 < 0.0f || c1 > CANVAS_H) return;
    if (s0 < 2.0f || s0 > 128.0f || s1 < 2.0f || s1 > 128.0f) return;
    *cx = c0; *cy = c1; *hw = s0 * 0.5f; *hh = s1 * 0.5f;
}

/*
 * A touch, from screen pixels into the canvas units the button's box is in -
 * the PICTURE's, as the game's own Button::HitTest takes them (0x863b1c ->
 * vtable +0x10 -> 0x86255c: the finger less the area's origin, over the area;
 * screen.c, "one finger, two spaces"). So the press held back here is exactly
 * the press the game would count as a pause, with FULLSCREEN on or off.
 */
int shim_screen_finger_units(float x, float y, float *u, float *v);

static int to_field(float x, float y, float *gx, float *gy) {
    return shim_screen_finger_units(x, y, gx, gy);
}

/* is this touch, in screen pixels, on the pause button? */
static int on_button(float x, float y) {
    float gx, gy;
    if (!to_field(x, y, &gx, &gy)) return 0;
    float cx, cy, hw, hh;
    button_box(&cx, &cy, &hw, &hh);
    return fabsf(gx - cx) <= hw && fabsf(gy - cy) <= hh;
}

/* the finger is gone: forget it, whatever state it was in */
static void forget(void) {
    g_id = -1;
    g_eaten = 0;
    g_let_go = 0;
}

void shim_reset_forget(void) {
    pthread_mutex_lock(&g_lock);
    forget();
    pthread_mutex_unlock(&g_lock);
}

/*
 * A touch, before it reaches the queue. Answers 1 if the event is ours and the
 * game must not see it - yet, or at all.
 */
int shim_reset_touch(int id, int phase, float x, float y) {
    int answer = 0;
    pthread_mutex_lock(&g_lock);
    if (phase == 0) {
        if (g_id < 0 && shim_sticks_in_game() && on_button(x, y)) {
            g_id = id;
            g_down_x = x;
            g_down_y = y;
            g_down_ms = now_ms();
            g_eaten = 0;
            g_let_go = 0;
            answer = 1;                         /* held back while we decide */
        }
    } else if (id == g_id && g_let_go) {
        /*
         * THE FINGER IS STILL OURS AFTER WE LET GO OF IT, and forgetting that
         * is what killed the long press for good.
         *
         * Wandering off the button hands the press to the game and sets
         * g_let_go - but the finger's own release then matched nothing, since
         * every branch below asked for !g_let_go. So `forget` was never
         * reached, `g_id` stayed taken, and `g_id < 0` never held again: no
         * later press was ever considered, until the game was restarted. The
         * owner found it by hammering pause, which loses a release now and then
         * and leaves exactly this state (14.09.2026).
         *
         * The events themselves still belong to the game - the press was handed
         * over already - so nothing is answered here; the finger is only let go
         * of when it lifts.
         */
        if (phase >= 3) forget();
    } else if (id == g_id) {
        if (g_eaten) {                          /* it became a reset: eat the rest */
            if (phase >= 3) forget();
            answer = 1;
        } else if (phase == 1) {
            if (fabsf(x - g_down_x) <= WANDER && fabsf(y - g_down_y) <= WANDER) {
                answer = 1;                     /* still on the button, still deciding */
            } else {
                /* wandered off: an ordinary touch after all, hand it over */
                shim_touch_push_raw(g_id, 0, g_down_x, g_down_y);
                g_let_go = 1;
            }
        } else if (phase == 4) {
            /*
             * CANCELLED, not let go (batch 2, U7): a palm the system turned
             * down, or the system taking the gesture back. The press was never
             * handed over, and a cancelled touch is no tap - the game's pause
             * answers a press, so handing it over now would pause on a palm.
             */
            forget();
            answer = 1;
        } else {
            /*
             * Let go early - a tap. The press goes in now and the release
             * straight after it: the game drains the queue once a frame and
             * sees a press and a release, which is what a tap is.
             */
            shim_touch_push_raw(g_id, 0, g_down_x, g_down_y);
            shim_touch_push_raw(g_id, phase, x, y);
            forget();
            answer = 1;
            shim_devices_pause_pressed();       /* the report follows it into the game (U2) */
        }
    }
    pthread_mutex_unlock(&g_lock);
    return answer;
}

/* ---------- the reset itself: the pad's own gesture, held for the finger ---------- */

/*
 * THE FADE (the owner's list, item 12, 23.09.2026).
 *
 * This file used to perform case 5 itself the moment the press turned long:
 * streak, Eden's token, copy -> 0x672568 -> destroy. The run was reset, but
 * with no fade-out - the picture cut straight to the new run and only faded
 * in. The fade-out is not a decoration of case 5, it is the road to it:
 *
 *   0x88d988, every in-run update, asks 0x8b61a0(input, id, -1) - every
 *   device - for the raw ids 8, 9, 0xb and 0xc. Held together they count the
 *   byte fade+0x10 up; past 15 (and only when 0x88da78 lets it: not Eden or
 *   Tainted Eden without a token, not a daily run - fade+0x80) it writes speed
 *   1/30 and kind 5 into fade+8 (the fade is Game+0x1cf440) and the picture
 *   darkens. Let go before it is dark and 0x88dae4 turns it back: kind 0,
 *   speed 0.133, the picture comes back and nothing happens. Dark, and
 *   0x88dbd4 runs case 5 - and the new run fades in.
 *
 * So the long press no longer does the work; it HOLDS the combination. The
 * keyboard device answers raw ids out of its own array (+0x30 is 0x8ddab8: the
 * byte ac59d8[id], while the device is on), and that array is filled once an
 * update from the set its own key-down and key-up write (0x8ddc9c, 0x8ddcb0 -
 * nothing in the iOS build calls them). The four ids are not keys of the
 * keyboard numbering the device speaks (GLFW: 32 and up), so holding them
 * presses nothing else - which the report measures, see devices_measured.
 *
 * What the player gets is the pad's gesture exactly: the fade-out while the
 * finger stays, a change of mind by lifting it, and every refusal the game
 * makes on a pad made here too.
 *
 * ONE HOLDER, TWO HANDS (the keyboard's R, 08.10.2026). The combination is
 * held by this file alone, for "the finger OR the restart key": hold_combo is
 * asked once a frame with both wishes together and touches the device only
 * when the answer changes. So letting go of one never lets go of the other -
 * a finger lifted while R is still down leaves the combination held, and the
 * other way round - and the game sees one unbroken hold either way.
 */
#define GAME_GAME           0xac3b90    /* Game *, a global */
#define G_FADE_KIND         0x1cf44c    /* the fade's kind: 5 is the reset */
#define FADE_RESET          5
#define G_PAUSE_STATE       (0x10dd20 + 0x2b8)  /* Game: non-zero while the pause menu is up (straps.c) */
#define MANAGER_PTR         0xac2428
#define M_DIALOGS           0x10        /* the manager's dialog list: its size (spawner.c, the card) */

#define FN_KEY_DOWN         0x8ddc9c    /* the keyboard device: set[id] = 1 */
#define FN_KEY_UP           0x8ddcb0    /* the keyboard device: set[id] = 0 */

static const int COMBO[4] = { 8, 9, 0xb, 0xc };

typedef void (*key_fn)(void *device, int id);

/* the device's key-down / key-up; the desk stand (tools/resetdesk) answers them with its own */
#ifndef RESET_KEY_FN
#define RESET_KEY_FN(b, on) ((key_fn) ((b) + ((on) ? FN_KEY_DOWN : FN_KEY_UP)))
#endif

static int g_holding;           /* the combination is held (frame thread only) */
static int g_saw_fade;          /* the reset's fade has been seen for this finger */
static int g_done;              /* it finished: hold nothing more for this finger */

static void hold_combo(unsigned long b, int on) {
    if (on == g_holding) return;
    key_fn fn = RESET_KEY_FN(b, on);
    for (int i = 0; i < 4; i++) fn(0, COMBO[i]);
    g_holding = on;
}

/* ---------- the keyboard's hand: the key bound to restart ---------- */

/*
 * HOLD R TO RESTART, AS ON THE PC (the owner, 08.10.2026).
 *
 * The keyboard device binds action 23 to R (the defaults at 0xa9c6e8, or
 * whatever inputconfigs.dat says), and nothing in the Repentance engine asks
 * for it. So while a key bound to action 23 is held in a run, this file holds
 * the pad's combination for it - the same holder as the finger's - and the
 * game does the rest exactly as for a pad: 16 updates, then the fade; let go
 * before it is dark and the fade turns back; Eden without a token and a daily
 * run refused (0x88da78). Kept held, it goes on as a pad's combination does:
 * the game counts again in the new run - the finger's "one press, one reset"
 * (g_done) is the touch gesture's own rule, not the game's.
 *
 * EVENT-DRIVEN, NOT POLLED (the owner, 08.10.2026). Nothing asks the device
 * "is action 23 held" every frame. Each key, as the delivery (objc.c
 * shim_keys_deliver) hands it to the game, comes here once: a press is looked
 * up in the device's LIVE bindings for action 23 (shim_keys_restart_bound,
 * the very table the device's own query 0x8a7fec walks: [dev+0x20][23], each
 * of the [0xabec7c] slots), a release lets its key go. The delivery runs
 * between frames on the frame thread, so the table is read where the game
 * writes it; the one word the holder reads is still published atomically.
 *
 * Where it counts, as on the PC: in a run (the manager's state RUNNING), the
 * pause menu down and no dialog of the game's up (0x6644a4: the game itself
 * skips 0x88d988 while its dialog list is not empty - the spawner's card is
 * one). Keys the port takes before the game sees them never get here as
 * presses: the spawner's card, the port's pages (settings, credits), the seed
 * field (Java keeps those keys), the touch editor (the main menu, not a run).
 * A press outside a run is not remembered, so a run that starts with R already
 * down does not restart until R is pressed again. Any of those conditions
 * failing while the key is held lets the hold go for good - pressed again, it
 * counts again.
 *
 * Every way a key can be lost lets it go: its release, whoever takes it (it
 * comes here before the spawner or the straps can keep it); the app leaving
 * or losing focus (MainActivity.releaseKeys hands a release for every held
 * key); and a binding that no longer names the key - checked again at every
 * key that goes by while one is held, since a binding only changes by a key
 * (the Controls capture) or by a load at start-up.
 */
#define ACTION_RESTART      23          /* the keyboard device's restart (#INPUT_RESTART) */
#define KEYS_MAX            8           /* keys bound to restart that can be down at once */

int shim_keys_restart_bound(int hid);   /* objc.c: is this HID usage bound to action 23 now */

static int g_rkey[KEYS_MAX];            /* the HID usages held for restart (frame thread) */
static int g_rkeys;
static int g_key_want;                  /* published: some restart key is held in a run */

static void key_publish(void) {
    __atomic_store_n(&g_key_want, g_rkeys > 0, __ATOMIC_RELEASE);
}

static void key_note(const char *what, int hid) {
    DIAG({
        int shim_report_active(void);
        void report_line(const char *kind, const char *text);
        static int said;
        char line[120];
        snprintf(line, sizeof(line), "restart key %#x %s; restart keys held %d", hid, what, g_rkeys);
        LOG("reset: %s", line);
        if (shim_report_active() && said < 24) { said++; report_line("reset", line); }
    });
    (void) what; (void) hid;
}

static void key_drop(int i, const char *why) {
    int hid = g_rkey[i];
    g_rkey[i] = g_rkey[--g_rkeys];
    key_note(why, hid);
}

static void keys_drop_all(const char *why) {
    while (g_rkeys > 0) key_drop(g_rkeys - 1, why);
    key_publish();
}

/* a run the restart may count in: running, not paused, no dialog of the game's up */
static int key_run_ok(unsigned long b) {
    if (!shim_sticks_in_game()) return 0;
    unsigned long G = *(const unsigned long *) (b + GAME_GAME);
    unsigned long M = *(const unsigned long *) (b + MANAGER_PTR);
    if (!G || !M) return 0;
    if (*(const int *) (G + G_PAUSE_STATE) != 0) return 0;
    return *(const unsigned long *) (M + M_DIALOGS) == 0;
}

/*
 * A key as the delivery hands it over (frame thread): every release, before
 * anything of the port's may keep it, and every press that reaches the game.
 */
void shim_reset_key(int hid, int down) {
    unsigned long b = shim_image_base();
    if (!b) return;
    int at = -1;
    for (int i = 0; i < g_rkeys; i++) if (g_rkey[i] == hid) at = i;
    /* a binding changed under a held key: it is not the restart key any more */
    for (int i = g_rkeys - 1; i >= 0; i--)
        if (g_rkey[i] != hid && !shim_keys_restart_bound(g_rkey[i])) key_drop(i, "is no longer bound");
    if (!down) {
        for (int i = 0; i < g_rkeys; i++) if (g_rkey[i] == hid) { key_drop(i, "let go"); break; }
    } else if (at < 0 && g_rkeys < KEYS_MAX && shim_keys_restart_bound(hid)) {
        if (key_run_ok(b)) {
            g_rkey[g_rkeys++] = hid;
            key_note("held", hid);
        } else {
            key_note("pressed outside a run - ignored", hid);
        }
    }
    key_publish();
}

/* the keyboard's wish, for the holder (frame thread): held, and the run still one it counts in */
static int key_wants(unsigned long b) {
    if (!__atomic_load_n(&g_key_want, __ATOMIC_ACQUIRE)) return 0;     /* the usual frame: one load */
    if (key_run_ok(b)) return 1;
    keys_drop_all("let go: the run paused, ended or put up a dialog");
    return 0;
}

static int fade_kind(unsigned long b) {
    unsigned long G = *(const unsigned long *) (b + GAME_GAME);
    return G ? *(const int *) (G + G_FADE_KIND) : 0;
}

/* the hook on 0x671348 still reports the manager; nothing here needs it now */
void shim_reset_serve(unsigned long root) { (void) root; }

/*
 * MEASUREMENT (report build): the input devices as the pad's reset combo sees
 * them. 0x88d988 asks 0x8b61a0(input, id, -1) for the raw ids 8, 9, 0xb, 0xc;
 * with -1 that walks every device (input 0xac5528, vector +8..+0x10) and asks
 * each one's +0x30. For the keyboard device that is 0x8ddab8: the byte of its
 * 0x401-long array ac59d8, and only while the device is on (+0xd). What has to
 * be known before the long press may hold those ids on the keyboard device: is it
 * on, and is any of the four bound to an action there ([dev+0x20][action], each
 * with [0xabec7c] slots, [dev+0x1c] actions) - a bound one would act as well.
 */
#if ISAAC_DIAG
#define INPUT_MANAGER   0xac5528
#define KEYBOARD_VTABLE 0xa94958        /* DeviceKeyboard; the touch screen is DeviceiOS 0xa92d88 */
#define BIND_SLOTS      0xabec7c

int shim_eid_readable(const void *p, unsigned long n);

static void devices_measured(unsigned long b) {
    static int done;
    int shim_report_active(void);
    void report_line(const char *kind, const char *text);
    if (done || !shim_report_active()) return;
    unsigned long in = b + INPUT_MANAGER;
    unsigned long *first = *(unsigned long **) (in + 8), *last = *(unsigned long **) (in + 0x10);
    if (!first || last <= first || last - first > 16) return;
    done = 1;
    unsigned slots = *(const unsigned *) (b + BIND_SLOTS);
    for (unsigned long *d = first; d < last; d++) {
        unsigned long dev = *d;
        if (!shim_eid_readable((const void *) dev, 0xe8)) continue;
        unsigned long vt = *(const unsigned long *) dev - b;
        unsigned actions = *(const unsigned *) (dev + 0x1c);
        unsigned long *table = *(unsigned long **) (dev + 0x20);
        char line[240];
        int n = snprintf(line, sizeof(line), "device %d vtable %lx on %d actions %u slots %u; combo ids bound:",
                         (int) (d - first), vt, *(const unsigned char *) (dev + 0xd), actions, slots);
        int found = 0;
        for (unsigned a = 0; table && a < actions && a < 64 && shim_eid_readable(table + a, 8); a++) {
            const int *ids = (const int *) table[a];
            for (unsigned k = 0; ids && k < slots && k < 8 && shim_eid_readable(ids + k, 4); k++)
                if ((ids[k] == 8 || ids[k] == 9 || ids[k] == 0xb || ids[k] == 0xc) && n < (int) sizeof(line) - 16) {
                    n += snprintf(line + n, sizeof(line) - (size_t) n, " a%u=%d", a, ids[k]);
                    found = 1;
                }
        }
        if (!found && n < (int) sizeof(line) - 8) snprintf(line + n, sizeof(line) - (size_t) n, " none");
        report_line("reset", line);
        LOG("reset: %s", line);
        /*
         * The keyboard's side of it (item 30): keys reach this very device -
         * the shell's callback 0x676bd0 -> 0x8ca88c -> [input+0xa8] ->
         * 0x8ddc9c - so what a key does is whatever this table binds its GLFW
         * code to. The whole table, a line per eight actions.
         */
        if (vt == KEYBOARD_VTABLE && table) {
            for (unsigned a0 = 0; a0 < actions && a0 < 64; a0 += 8) {
                char bl[200];
                int m = snprintf(bl, sizeof(bl), "keyboard bindings %u..:", a0);
                for (unsigned a = a0; a < a0 + 8 && a < actions && shim_eid_readable(table + a, 8); a++) {
                    const int *ids = (const int *) table[a];
                    m += snprintf(bl + m, sizeof(bl) - (size_t) m, " [");
                    for (unsigned k = 0; ids && k < slots && k < 4 && shim_eid_readable(ids + k, 4); k++)
                        m += snprintf(bl + m, sizeof(bl) - (size_t) m, "%s%d", k ? "," : "", ids[k]);
                    m += snprintf(bl + m, sizeof(bl) - (size_t) m, "]");
                    if (m > (int) sizeof(bl) - 24) break;
                }
                report_line("keyboard", bl);
                LOG("keyboard: %s", bl);
            }
        }
    }
}
#endif

/* once a frame: hold the combination for as long as the finger or the restart key is on */
#define STUCK_MS 8000                       /* a press nobody ever let go of */

void shim_reset_between_frames(void) {
    unsigned long b = shim_image_base();
    if (!b) return;
    int freed = 0, eaten_now = 0, kind = fade_kind(b);
    pthread_mutex_lock(&g_lock);
    /*
     * A NET UNDER ALL OF IT. A release can be lost outright - the window goes
     * away, the canvas is rebuilt, the system takes the gesture - and then the
     * finger is held for ever. Nothing legitimate holds the pause button for
     * eight seconds, so anything that does is wreckage and is swept up.
     */
    if (g_id >= 0 && now_ms() - g_down_ms >= STUCK_MS) { forget(); freed = 1; }
    int ours = (g_id >= 0 && !g_let_go);
    if (!ours) g_saw_fade = g_done = 0;
    if (ours && kind == FADE_RESET) g_saw_fade = 1;
    /* seen dark and now over: the new run is here, and one finger is one reset */
    if (ours && g_saw_fade && kind != FADE_RESET) g_done = 1;
    /*
     * The press stops being a tap the moment the game starts its fade - or at
     * HOLD_MS if the game refused, so that a long press it said no to does
     * nothing, as on a pad, instead of turning into a pause on release.
     */
    if (ours && !g_eaten && (kind == FADE_RESET || now_ms() - g_down_ms >= HOLD_MS)) {
        g_eaten = 1;
        eaten_now = 1;
    }
    pthread_mutex_unlock(&g_lock);
    /* one holder for both hands: the finger's wish OR the restart key's */
    int finger = ours && !g_done && shim_sticks_in_game();
    hold_combo(b, finger || key_wants(b));
    if (freed) LOG("reset: a press that was never let go of has been swept up");
    if (eaten_now) LOG("reset: a long press on the pause button — %s",
                       kind == FADE_RESET ? "the game is fading out" : "the game did not start the fade");
    DIAG(devices_measured(b));
}
