/*
 * Gamepads: a GameController surface on top of Android events.
 *
 * The game does not read a gamepad itself, it goes through iOS's
 * GameController — the image imports the GCController class and a whole set of
 * its properties. So the right move is not to invent our own input but to
 * provide that surface: then all of the engine's native code works, including
 * the Controls submenu and button assignment.
 *
 * What exactly the game asks for is not a guess but a measurement of the
 * image's strings:
 *
 *   +[GCController controllers]            the list of connected pads
 *   -[GCController extendedGamepad]        the button set itself
 *   -[GCController playerIndex/vendorName] index and name
 *   -[GCExtendedGamepad buttonA/B/X/Y, leftShoulder, rightShoulder,
 *       leftTrigger, rightTrigger, leftThumbstick, rightThumbstick,
 *       dpad, buttonMenu, buttonOptions]
 *   -[GCControllerButtonInput isPressed/value]
 *   -[GCControllerDirectionPad xAxis/yAxis]
 *   -[GCControllerAxisInput value]
 *
 * There is nothing beyond that in the image: no microGamepad, no connection
 * notifications, no wireless discovery. So there is none of it here either.
 *
 * ONE CONTROLLER PER DEVICE (the owner, 28.09.2026, for the game's co-op).
 * Every physical pad is a GCController of its own, four slots as Java numbers
 * them. A hardware keyboard is NOT one: the game has a keyboard device of its
 * own and the keys go to it through the shell (a keyboard as a fifth
 * controller took every key from it, 28.09.2026; its slot is gone since batch
 * 2, U5). Which Android device is a pad is Java's book to say (InputBook);
 * the axes arrive already worked by the device's own ranges - a trigger 0..1,
 * a stick with its declared flat cut off - as GCController hands them to an
 * iOS game. The game makes an input device for every controller in the list
 * (0x8ca5e8: a new device of 0x818 bytes whenever a controller object is not
 * yet behind any device's +0x608) and marks a device disconnected (+0x605 = 0)
 * when its controller is no longer in the list. So the list is rebuilt when the
 * line-up changes, and a pad that comes back gets NEW objects: the game keeps
 * the old ones in its old device, and were they to answer with the live state
 * the dead device would press buttons. Every object carries the generation of
 * its slot and answers nothing once the slot has moved on.
 *
 * The state is per slot: Android pushes changes, the game polls every frame.
 * The objects are one per button and axis per slot — polling happens every
 * frame and littering memory here is not an option.
 */
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <pthread.h>
#include <android/log.h>
#include "diag.h"

#if ISAAC_DIAG
#define LOG(...) __android_log_print(ANDROID_LOG_INFO, "isaac", __VA_ARGS__)
#else
#define LOG(...) do { } while (0)      /* the build we play on has no log */
#endif

/* ---------- state ---------- */

enum {                                  /* buttons, one bit each */
    BTN_A = 0, BTN_B, BTN_X, BTN_Y,
    BTN_L1, BTN_R1, BTN_MENU, BTN_OPTIONS,
    BTN_L3, BTN_R3,
    BTN_UP, BTN_DOWN, BTN_LEFT, BTN_RIGHT,
    BTN_COUNT
};

enum {                                  /* axes */
    AX_LEFT_X = 0, AX_LEFT_Y, AX_RIGHT_X, AX_RIGHT_Y,
    AX_DPAD_X, AX_DPAD_Y,
    AX_L2, AX_R2,
    AX_COUNT
};

/*
 * One number for both a button and an axis: a trigger's force comes from an
 * axis and its press is judged by the same axis. We tell them apart by a base
 * value rather than a separate field — that keeps the object small.
 */
#define FROM_AXIS 100

/*
 * The game reads the d-pad not as axes but as four direction buttons — that is
 * how a real GCControllerDirectionPad presents it. The measurement said so:
 * for [GCControllerDirectionPad up/down/left/right] the layer answered "no
 * method", and the d-pad stayed silent even though the values had long been
 * sitting in the axes.
 *
 * A button is derived from the axis of the very pad it was asked of, not from
 * a hard-wired pair: that way leftThumbstick.up answers correctly too, as on
 * iOS. The base value says which side of the axis counts as pressed.
 */
#define FROM_AXIS_PLUS 200              /* pressed when the axis goes positive */
#define FROM_AXIS_MINUS 300             /* pressed when the axis goes negative */

/* the slots: four pads; Java numbers them the same way (MainActivity.PAD_SLOTS) */
#define PADS_MAX     4

typedef struct {
    int connected;
    int generation;                     /* grows on every connection: the objects' ticket */
    unsigned buttons;
    float axes[AX_COUNT];
    /* the co-op gesture (gesture_edges, shim_pad_between_frames) */
    unsigned hidden;                    /* buttons kept from the game right now */
    unsigned pending;                   /* held back, waiting for the other within PAIR_MS */
    unsigned swallowed;                 /* the gesture's own: silent until let go */
    unsigned was;                       /* the two buttons as they were last judged */
    long long down_at[BTN_COUNT];       /* when each went down */
    int reads[BTN_COUNT];               /* the game's reads of a held-back button */
    long long both_at;                  /* when both were down together */
    int fired;
    int join_wanted;                    /* the gesture was held: ask coop.c from the frame thread */
    unsigned tap_bits;                  /* a short press, handed over once */
    int tap_frames, tap_seen;
    /* the double tap (double_tap_frame, double_tap_judge): in the game's own codes, see code_bit */
    unsigned long dev;                  /* the game's device made for this slot, as last looked up */
    int dev_id;
    float stick_press;                  /* the device's own "a stick half is pressed" (+0xe4) */
    /* the surface objects, made when first asked for */
    void *controller, *gamepad;
    void *button[BTN_COUNT];
    void *axis[AX_COUNT];
    void *direction[AX_COUNT][2];       /* direction buttons: [axis][side] */
    void *trigger[2];
    void *dpad, *left_stick, *right_stick;
    long player_index;                  /* -1 is Apple's "unset" */
} pad_t;

static pad_t g_pads[PADS_MAX];
static int g_lineup;                    /* grows whenever a slot connects or goes */

/*
 * THE PAD'S PICTURES AND ITS NAME (the owner, 1.72; Java PadIcons). Java tells
 * a slot's family - 0 xbox, 1 ps, 2 switch, guide_layout.h GD_FAMILY_NAMES -
 * and the vendorName iOS gives such a pad, BEFORE the slot is seen connected:
 * the game names its device the moment it makes it (0x8a90e8 -> 0x8a9264,
 * once) and looks the name up in its table of pads (0xa9cfd8, ten entries of
 * 0xd0: "Xbox Wireless Controller" type 0, "DUALSHOCK 4 Wireless Controller"
 * type 1 ...). The type, device+0x750 (0x8a9970; 3 when the name is not in the
 * table), picks the animation of its own button prompts: XboxOne, PS4, WiiU
 * (0x6732b4, the table 0xa8db88). "Gamepad", what this file answered before,
 * is in no table: the game drew no prompt for any pad at all.
 *
 * The GUIDE shows the family of the slot the last button press came from
 * (shim_pad_icon_family, tweaks.c); a keyboard is no slot and never changes it.
 */
static volatile int g_family[PADS_MAX];
static char g_vendor_name[PADS_MAX][40];
static void *g_vendor_obj[PADS_MAX];        /* the NSString of the name, made on the frame thread */
static int g_vendor_stale[PADS_MAX];
static pthread_mutex_t g_name_lock = PTHREAD_MUTEX_INITIALIZER;
static volatile int g_last_press = -1;      /* the slot of the last button pressed anew */

/*
 * THE CO-OP GESTURE: Select and Start held together for three seconds (the
 * owner, 28.09.2026) - a thing nobody does by accident. Held TOGETHER, the
 * two do neither of their own things: no map, no pause (the owner, after the
 * 18th round). The port cannot know at the press whether the other button is
 * about to follow, so a press of either is held back for PAIR_MS: if the
 * other arrives within it, both are the gesture's and stay silent until
 * they are let go; if not, the button is handed to the game as it is, late by
 * that much, and held for as long as the finger holds it. A press shorter
 * than the window is handed over as a press the game reads exactly once
 * (the_game_read): two frames of it used to be read twice, or not at all,
 * by a game that runs its updates at its own pace.
 *
 * The edges are judged the moment the state ARRIVES from Java (gesture_edges
 * from shim_pad_state), not once a frame: judged a frame later, the game had
 * already read the raw pair - Start paused the run and the dispatcher handed
 * the pad to player 0, which killed the slot the join had just opened (the
 * 18th round's report: "actions: 12 13 26" through the whole hold). The clock
 * (the window, the three seconds) runs in shim_pad_between_frames (gl.c).
 * On the keyboard the pair is Tab and Esc, judged apart by the shell's own
 * keys (objc.c key_gesture): a keyboard is no pad slot.
 */
#define JOIN_HOLD_MS   1500                 /* the owner, 28.09.2026: three seconds were too long */
/*
 * A LONE PRESS IS THE GAME'S AT ITS SECOND READ (the owner, after the 18th
 * round: "the delay must be there only when both are held"). The game reads
 * every button once an update; a press is answered "not yet" at the first
 * read after it and "pressed" from the second - one update late, which no
 * finger can tell - and if the other button has arrived by then, both are the
 * gesture's. PAIR_MS is only the clock's fallback for a game that is not
 * reading (a menu that does not ask).
 */
#define PAIR_MS         100
#define PAIR_READS      2
#define TAP_WAIT_FRAMES 10                  /* a short press waits this long to be read once */
#define GESTURE_BITS    ((1u << BTN_OPTIONS) | (1u << BTN_MENU))
int shim_coop_join_from_pad(void *controller);
int shim_report_active(void);
void report_line(const char *kind, const char *text);

/* PROBE, report builds: the gesture's every step, so a hold that does not open can be read */
static void gesture_say(int slot, const char *what, long long ms) {
    static int said;
    if (said >= 60 || !shim_report_active()) return;
    said++;
    char line[120];
    snprintf(line, sizeof(line), "slot %d gesture: %s (%lld ms)", slot, what, ms);
    report_line("pad", line);
}

static long long pad_now_ms(void) {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (long long) t.tv_sec * 1000 + t.tv_nsec / 1000000;
}

static unsigned shown_buttons(const pad_t *p) {
    return (p->buttons & ~p->hidden) | (p->tap_frames > 0 ? p->tap_bits : 0u);
}

/* the game has asked for a button: a short press handed over is read, and ends with this frame */
static void the_game_read(pad_t *p, int button) {
    if (p->tap_frames > 0 && (p->tap_bits >> button) & 1) p->tap_seen = 1;
    if ((p->pending >> button) & 1) p->reads[button]++;
}

static const char *button_word(int bit) { return bit == BTN_OPTIONS ? "select" : "start"; }

/*
 * The two buttons judged against what they were: on every arrival of a
 * state and once a frame for the clock. Java's thread and the frame thread
 * both come here; the lock keeps the little state machine whole.
 */
static pthread_mutex_t g_gesture_lock = PTHREAD_MUTEX_INITIALIZER;

static void gesture_edges(pad_t *p, long long now) {
    int slot = (int) (p - g_pads);
    unsigned raw = (p->connected ? p->buttons : 0u) & GESTURE_BITS;
    unsigned went_down = raw & ~p->was, went_up = p->was & ~raw;
    for (int bit = BTN_MENU; bit <= BTN_OPTIONS; bit++) {
        unsigned m = 1u << bit;
        if (went_down & m) {                    /* held back: the other may follow */
            p->down_at[bit] = now;
            p->reads[bit] = 0;
            p->pending |= m;
            p->hidden |= m;
            gesture_say(slot, bit == BTN_OPTIONS ? "select down, held back" : "start down, held back", 0);
        }
    }
    if ((raw & GESTURE_BITS) == GESTURE_BITS) {  /* both down: the gesture's, whatever they were */
        if (!p->both_at) {
            p->both_at = now;
            p->fired = 0;
            p->swallowed |= GESTURE_BITS;
            p->pending &= ~GESTURE_BITS;
            p->hidden |= GESTURE_BITS;
            gesture_say(slot, "both down: holding, neither is the game's", 0);
        }
        if (!p->fired && now - p->both_at >= JOIN_HOLD_MS) {
            p->fired = 1;
            gesture_say(slot, "held: the join is asked", now - p->both_at);
            p->join_wanted = 1;                 /* asked on the frame thread (gesture), never Java's */
        }
    } else if (p->both_at) {
        gesture_say(slot, p->fired ? "one let go after the join" : "one let go before the time", now - p->both_at);
        p->both_at = 0;
    }
    for (int bit = BTN_MENU; bit <= BTN_OPTIONS; bit++) {
        unsigned m = 1u << bit;
        if (went_up & m) {
            if (p->pending & m) {               /* a short press, never handed over: once, now */
                p->tap_bits |= m;
                p->tap_frames = TAP_WAIT_FRAMES;
                p->tap_seen = 0;
                gesture_say(slot, bit == BTN_OPTIONS ? "select up early: handed over once" : "start up early: handed over once", now - p->down_at[bit]);
            } else if (p->swallowed & m) {
                gesture_say(slot, bit == BTN_OPTIONS ? "select up, the gesture's" : "start up, the gesture's", now - p->down_at[bit]);
            }
            p->pending &= ~m;
            p->swallowed &= ~m;
            p->hidden &= ~m;
        } else if ((p->pending & m) && (p->reads[bit] >= PAIR_READS || now - p->down_at[bit] >= PAIR_MS)) {
            p->pending &= ~m;                   /* alone at the game's second read: the game's, as it is */
            p->hidden &= ~m;
            gesture_say(slot, bit == BTN_OPTIONS ? "select alone: the game's" : "start alone: the game's", now - p->down_at[bit]);
        }
    }
    p->was = raw;
    (void) button_word;
}

static void gesture(pad_t *p, long long now) {
    pthread_mutex_lock(&g_gesture_lock);
    if (p->tap_frames > 0) {
        if (p->tap_seen) { p->tap_frames = 0; p->tap_bits = 0; } else p->tap_frames--;
        p->tap_seen = 0;
    }
    gesture_edges(p, now);
    int join = p->join_wanted;
    p->join_wanted = 0;
    pthread_mutex_unlock(&g_gesture_lock);
    if (join) shim_coop_join_from_pad(p->controller);
}

/*
 * THE DOUBLE TAP ON A PAD: THE GAME'S OWN WINDOW (the owner, 19th round:
 * "Mars works on touch, on the pad not at all").
 *
 * The game's detector (movement 0x516788 for Mars 593, shooting 0x5185a8 for
 * Hemoptysis and the like) asks, every player update, whether a direction's
 * action went down this update (0x671c6c); the same direction as the last
 * press (player+0x25bc / +0x26e8) with the count since it (+0x25c0 / +0x26ec)
 * at TEN or less is the double tap. Mars is that and nothing else - no button
 * of its own, R2 has nothing to do with it.
 *
 * The port shut that window for everyone (loader.c double_tap_gate,
 * 27.09.2026: the touch sticks wobbled through a direction and dashed by
 * themselves) and opens it only from its touch gestures (sticks.c
 * preset_detector, a count below nought). A pad makes no touch gesture, so
 * for a pad's player the window never opened: that is the whole of "Mars is
 * dead on the pad".
 *
 * A pad gets the game's own rule back, judged at the very moment the game
 * reads the press and by the game's own numbers. The device keeps what it
 * read last update (+0x38 for the buttons, +0x48 for the stick halves; the
 * update copies them before it asks us again, 0x8a79e4). When the game asks
 * one of our objects and the answer is "down" where the last update said
 * "up", that is the press the detector will see a moment later in this same
 * update (the input update runs before the players, 0x671130). If a player
 * on this device has its detector's last press on a direction this code is
 * bound to, within the game's window of ten, the count is written to nought -
 * the gate's own way in (cmp w8, #0) - and the game fires as it would on
 * iOS: same update, same window, its own cooldown. Nothing is held back and
 * nothing is shown late. The touch gate is not touched: the touch screen's
 * players are not on a pad.
 *
 * The codes are the device's (0x8a9988, 0x8a7a68), one bit each here:
 *   0..15  buttons: d-pad left, right, up, down, A, B, X, Y, L1, L2, -, R1,
 *          R2, -, Options, Menu
 *   16..23 stick halves 0xABAB0000+i: left x-, x+, y-, y+, right x-, x+, y-, y+
 *          (pressed beyond the device's +0xe4, measured 0.5)
 * The bindings (device+0x20, as Controls sets them) are taken between frames.
 */
#define DTAP_WINDOW        10            /* cmp w8, #0xa at 0x5167b0 / 0x5185d0, as the game has it */
#define DTAP_TABLE_MOVE    0x952c10      /* detector index -> action: 0 2 1 3 */
#define DTAP_TABLE_SHOOT   0x952c20      /* 4 6 5 7 */
#define PLAYER_DEVICE_AT   0x19ec
#define DEVICE_BUTTONS_NOW 0x30          /* u64: buttons read this update (built after our answers) */
#define DEVICE_BUTTONS_WAS 0x38          /* u64: buttons read the update before */
#define DEVICE_STICKS_NOW  0x40          /* u64: stick halves, the same two */
#define DEVICE_STICKS_WAS  0x48
#define DEVICE_ACTIONS     0x1c          /* u32: how many actions the device binds */
#define DEVICE_BINDINGS    0x20          /* int *[action]: the codes bound to each */
#define DEVICE_STICK_PRESS 0xe4          /* float: a stick half counts as pressed beyond it */
#define BINDINGS_PER_ACTION 0xabec7c     /* u32: how many codes an action has (2) */
#define STICK_CODE_BASE    0xABAB0000u
#define STICK_PRESS_DEFAULT 0.5f
#define BUTTON_PRESS       0.1f          /* shim_pad_is_pressed's own line */
#define PLAYERS_SEEN       8

static const struct { unsigned long last, ago, table; const char *name; } DETECTORS[2] = {
    { 0x25bc, 0x25c0, DTAP_TABLE_MOVE, "movement" },
    { 0x26e8, 0x26ec, DTAP_TABLE_SHOOT, "shooting" },
};

/* a game code as one bit of ours, or 0 for a code that is not a pad's */
static unsigned code_bit(unsigned code) {
    if (code < 16) return 1u << code;
    if (code >= STICK_CODE_BASE && code < STICK_CODE_BASE + 8) return 1u << (16 + (code - STICK_CODE_BASE));
    return 0;
}

/* our button number as the game's code bit (0 for the buttons the game has no code for) */
static unsigned button_code_bit(int n) {
    switch (n) {
        case BTN_A: return 1u << 4;
        case BTN_B: return 1u << 5;
        case BTN_X: return 1u << 6;
        case BTN_Y: return 1u << 7;
        case BTN_L1: return 1u << 8;
        case BTN_R1: return 1u << 11;
        case BTN_OPTIONS: return 1u << 14;
        case BTN_MENU: return 1u << 15;
        default: return 0;
    }
}

unsigned long shim_image_base(void);
int shim_eid_players(unsigned long *out, int max);
int shim_eid_readable(const void *p, unsigned long n);
int shim_device_of_pad_slot(int slot);
unsigned long shim_device_object(int id);

/* the codes bound to an action on a device, as the device stores them; how many */
static int action_codes(unsigned long b, unsigned long dev, int action, unsigned *out, int room) {
    unsigned per = *(const unsigned *) (b + BINDINGS_PER_ACTION);
    if (!per || per > 8 || (int) per > room || action < 0) return 0;
    if (!shim_eid_readable((const void *) (dev + DEVICE_ACTIONS), DEVICE_BINDINGS + 8 - DEVICE_ACTIONS)) return 0;
    if ((unsigned) action >= *(const unsigned *) (dev + DEVICE_ACTIONS)) return 0;
    const unsigned long *table = *(const unsigned long *const *) (dev + DEVICE_BINDINGS);
    if (!table || !shim_eid_readable(table + action, 8)) return 0;
    const unsigned *codes = (const unsigned *) table[action];
    if (!codes || !shim_eid_readable(codes, per * 4)) return 0;
    for (unsigned k = 0; k < per; k++) out[k] = codes[k];
    return (int) per;
}

static void dtap_say(const char *who, const char *detector, int ago) {
#if ISAAC_DIAG
    static int said;
    if (said >= 40 || !shim_report_active()) return;
    said++;
    char line[160];
    snprintf(line, sizeof(line), "%s double tap: a press %d updates after the last one the same way, "
             "within the game's ten - the %s detector is opened", who, ago, detector);
    report_line("pad", line);
#else
    (void) who; (void) detector; (void) ago;
#endif
}

/*
 * THE JUDGE, for any device of the game (frame thread, before the update
 * that will see the press): `fresh` says which of the device's codes go down
 * at that update for the first time. Every player on the device whose
 * detector's last press was a direction bound to such a code, within the
 * game's window, gets its count written to nought - the gate's own way in
 * (cmp w8, #0) - and the game fires as it would on iOS. The pad asks while
 * the game reads it (double_tap_judge), the keyboard once its keys for the
 * next update are set (objc.c, shim_keys_deliver).
 */
int shim_double_tap_open(int dev_id, unsigned long dev, int (*fresh)(unsigned code, void *ctx), void *ctx,
                         const char *who) {
    unsigned long b = shim_image_base();
    if (!b || !dev || dev_id == -1) return 0;
    unsigned long players[PLAYERS_SEEN];
    int n = shim_eid_players(players, PLAYERS_SEEN), opened = 0;
    for (int k = 0; k < n; k++) {
        unsigned long pl = players[k];
        if (!pl || !shim_eid_readable((const void *) (pl + PLAYER_DEVICE_AT), 4)) continue;
        if (*(const int *) (pl + PLAYER_DEVICE_AT) != dev_id) continue;
        for (int d = 0; d < 2; d++) {
            if (!shim_eid_readable((const void *) (pl + DETECTORS[d].last), 4)
                || !shim_eid_readable((const void *) (pl + DETECTORS[d].ago), 4)) continue;
            int last = *(const int *) (pl + DETECTORS[d].last);
            int *ago = (int *) (pl + DETECTORS[d].ago);
            if (last < 0 || last > 3 || *ago < 1 || *ago > DTAP_WINDOW) continue;
            unsigned codes[8];
            int m = action_codes(b, dev, ((const int *) (b + DETECTORS[d].table))[last], codes, 8);
            int hit = 0;
            for (int i = 0; i < m && !hit; i++) hit = fresh(codes[i], ctx);
            if (!hit) continue;
            dtap_say(who, DETECTORS[d].name, *ago);
            *ago = 0;                           /* the gate's own way in (cmp w8, #0) */
            opened++;
        }
    }
    return opened;
}

/*
 * The keyboard's side of the judge (objc.c keys_double_tap): the device's
 * update (0x8ddc40) makes SET its CURRENT and CURRENT its PREVIOUS, and asks
 * "pressed this update" as CURRENT and not PREVIOUS (0x8ddae8) - so a key down
 * in SET and up in CURRENT, once the frame's keys are in, is a press the next
 * update sees for the first time.
 */
typedef struct { const unsigned char *set, *cur; int keys; } keys_now_t;

static int key_fresh(unsigned code, void *ctx) {
    const keys_now_t *k = (const keys_now_t *) ctx;
    return code < (unsigned) k->keys && k->set[code] && !k->cur[code];
}

int shim_double_tap_keys(int dev_id, unsigned long dev, const unsigned char *set, const unsigned char *cur, int keys) {
    if (!set || !cur || keys <= 0) return 0;
    keys_now_t k = { set, cur, keys };
    return shim_double_tap_open(dev_id, dev, key_fresh, &k, "keyboard");
}

/* the frame thread, between frames: the slot's device and its line for a stick */
static void double_tap_frame(void) {
    for (int s = 0; s < PADS_MAX; s++) {
        pad_t *p = &g_pads[s];
        if (!p->connected) { p->dev = 0; p->dev_id = -1; continue; }
        int id = shim_device_of_pad_slot(s);
        unsigned long dev = shim_device_object(id);
        if (!dev || !shim_eid_readable((const void *) dev, DEVICE_STICK_PRESS + 4)) { p->dev = 0; p->dev_id = -1; continue; }
        float t = *(const float *) (dev + DEVICE_STICK_PRESS);
        if (t > 0.05f && t < 0.95f) p->stick_press = t;
        p->dev_id = id;
        p->dev = dev;
    }
}

static int pad_code_fresh(unsigned code, void *ctx) { return (code_bit(code) & *(const unsigned *) ctx) != 0; }

/*
 * The game is reading `codes` of a slot down (frame thread, inside the input
 * update): those the device read up last update are a press this update.
 */
static void double_tap_judge(pad_t *p, unsigned codes) {
    unsigned long dev = p->dev;
    if (!dev || !codes) return;
    unsigned buttons = codes & 0xffffu, sticks = codes >> 16;
    if (buttons) {
        unsigned long long now = *(const unsigned long long *) (dev + DEVICE_BUTTONS_NOW);
        unsigned long long was = *(const unsigned long long *) (dev + DEVICE_BUTTONS_WAS);
        buttons &= ~(unsigned) (now | was);         /* "now" is set only once the update's read is done */
    }
    if (sticks) {
        unsigned long long now = *(const unsigned long long *) (dev + DEVICE_STICKS_NOW);
        unsigned long long was = *(const unsigned long long *) (dev + DEVICE_STICKS_WAS);
        sticks &= ~(unsigned) (now | was) & 0xffu;
    }
    unsigned fresh = buttons | (sticks << 16);
    if (!fresh) return;
    char who[16];
    snprintf(who, sizeof(who), "slot %d", (int) (p - g_pads));
    shim_double_tap_open(p->dev_id, dev, pad_code_fresh, &fresh, who);
}

/*
 * THE DROP ACTION: ONCE A PRESS (the owner, 19th round: "with the School Bag
 * R2 switches the items many times if held, about once a frame").
 *
 * Nothing in our surface repeats a press: the game polls isPressed and judges
 * the edge itself (0x8a79e4 keeps last update's bits at +0x38, 0x8a7e88
 * compares). It is the game's drop road, 0x52acd8, every player update: for a
 * player on a pad or a keyboard it asks at 0x52ad84 whether DROP (action 11,
 * R2 by default) is HELD (0x671c5c), and on a yes swaps J&E/Forgotten
 * (0x5497e8), rotates the pocket (0x54a424), turns D Infinity or swaps the
 * School Bag's actives (0x5390f8), turns Tainted Isaac's and Tainted Cain's
 * queues (0x549634, 0x549720) - every update the button is down. None of them
 * has a latch. The two older editions in the same image ask the same place
 * "went down this update" (Afterbirth+ 0x1bda1c, Rebirth 0xb8d64 -> 0x8b6600)
 * and keep "held" for the long press alone; Repentance kept that too
 * (0x52aea0: 121 updates held drops the card). So the one call goes back to
 * "went down this update" (0x671c6c, same arguments): a press is one swap,
 * one turn of the die, one step of the pocket; the long press still drops.
 * The touch screen's road (actions 17/22) is the other branch and untouched;
 * a keyboard's player takes the same road as a pad's and is mended with it.
 *
 * The desktop Repentance says the same, measured in the owner's isaac-ng.exe
 * (04.10.2026): its drop road 0x769480 has the very shape of ours (the J&E
 * swap, the pocket, D Infinity's (form + 1) % 10, the School Bag, Tainted
 * Isaac, Tainted Cain, the 121-update counter) and asks action 11 through
 * two different functions - 0x8b1170 ("went down", the one its menus use
 * for 12, 14, 15) before the swaps, 0x8b10b0 ("held", the one walking uses)
 * for the counter. Only the iOS build has "held" in the first place. The
 * owner's word (04.10.2026): one press, one swap; the long press still drops.
 */
#define DROP_ASK          0x52ad84
#define FN_ACTION_HELD    0x671c5c
#define FN_ACTION_TRIG    0x671c6c

int shim_patch_word(unsigned long off, uint32_t was_a, uint32_t was_b, uint32_t now);

static uint32_t bl_word(unsigned long from, unsigned long to) {
    return 0x94000000u | ((uint32_t) (((long) to - (long) from) >> 2) & 0x03ffffffu);
}

static void drop_once_per_press(void) {
    static int done;
    if (done) return;
    done = 1;
    uint32_t held = bl_word(DROP_ASK, FN_ACTION_HELD), trig = bl_word(DROP_ASK, FN_ACTION_TRIG);
    int ok = shim_patch_word(DROP_ASK, held, trig, trig);
#if ISAAC_DIAG
    LOG("drop: %s", ok ? "52ad84 asks whether DROP went down this update - one press, one swap"
                       : "52ad84 is not the call expected - left alone");
    if (shim_report_active())
        report_line("pad", ok ? "drop: one press, one swap (52ad84 -> 0x671c6c)"
                              : "drop: 52ad84 is not the call expected - left alone");
#else
    (void) ok;
#endif
}

void shim_pad_between_frames(void) {
    long long now = pad_now_ms();
    for (int i = 0; i < PADS_MAX; i++) gesture(&g_pads[i], now);
    unsigned long b = shim_image_base();
    /* the game's manager first: before it there are no players and no devices to ask */
    if (!b || !*(const unsigned long *) (b + 0xac2428)) return;
    drop_once_per_press();
    double_tap_frame();
}

/* a pad state as a menu reads it: up (+1), down (-1) or neither */
static int menu_vertical(unsigned buttons, const float *axes) {
    float y = axes[AX_DPAD_Y];
    if (axes[AX_LEFT_Y] > 0.0f && axes[AX_LEFT_Y] > y) y = axes[AX_LEFT_Y];
    if (axes[AX_LEFT_Y] < 0.0f && axes[AX_LEFT_Y] < y) y = axes[AX_LEFT_Y];
    if ((buttons & (1u << BTN_UP)) || y > 0.5f) return 1;
    if ((buttons & (1u << BTN_DOWN)) || y < -0.5f) return -1;
    return 0;
}

/* and sideways: right (+1), left (-1) or neither */
static int menu_horizontal(unsigned buttons, const float *axes) {
    float x = axes[AX_DPAD_X];
    if (axes[AX_LEFT_X] > 0.0f && axes[AX_LEFT_X] > x) x = axes[AX_LEFT_X];
    if (axes[AX_LEFT_X] < 0.0f && axes[AX_LEFT_X] < x) x = axes[AX_LEFT_X];
    if ((buttons & (1u << BTN_RIGHT)) || x > 0.5f) return 1;
    if ((buttons & (1u << BTN_LEFT)) || x < -0.5f) return -1;
    return 0;
}

static int anything(unsigned buttons, const float *axes) {
    if (buttons) return 1;
    for (int i = 0; i < AX_COUNT; i++) if (axes[i] > 0.5f || axes[i] < -0.5f) return 1;
    return 0;
}

void shim_straps_pad(int vertical, int horizontal, int a, int back, int anything);

/* every connected slot as one picture: for the port's own menus, which any pad may drive */
static void union_state(unsigned *buttons, float *axes) {
    *buttons = 0;
    memset(axes, 0, sizeof(float) * AX_COUNT);
    for (int i = 0; i < PADS_MAX; i++) {
        const pad_t *p = &g_pads[i];
        if (!p->connected) continue;
        *buttons |= p->buttons;
        for (int a = 0; a < AX_COUNT; a++) {
            float v = p->axes[a], have = axes[a];
            if ((v < 0 ? -v : v) > (have < 0 ? -have : have)) axes[a] = v;
        }
    }
}

/*
 * Arrives from Java as a whole state of one slot. Android delivers buttons and
 * axes in separate events, but assembling them into one picture is easier
 * where both are available.
 */
void shim_pad_state(int slot, int connected, unsigned buttons, const float *axes, int axis_count) {
    if (slot < 0 || slot >= PADS_MAX) return;
    pad_t *p = &g_pads[slot];
    if (connected != p->connected) {
        LOG("gamepad: slot %d %s", slot, connected ? "connected" : "disconnected");
        g_lineup++;
        if (connected) {
            /* a new controller: new objects, the old ones fall silent (their generation is gone) */
            p->generation++;
            p->controller = p->gamepad = NULL;
            memset(p->button, 0, sizeof(p->button));
            memset(p->axis, 0, sizeof(p->axis));
            memset(p->direction, 0, sizeof(p->direction));
            memset(p->trigger, 0, sizeof(p->trigger));
            p->dpad = p->left_stick = p->right_stick = NULL;
            p->player_index = -1;
        }
        p->hidden = p->pending = p->swallowed = p->was = p->tap_bits = 0;
        p->tap_frames = p->tap_seen = p->fired = 0;
        p->both_at = 0;
        p->dev = 0;
        p->dev_id = -1;
    }
    p->connected = connected;
    if (!connected) {
        p->buttons = 0;
        memset(p->axes, 0, sizeof(p->axes));
    } else {
        /* a button pressed anew, or a trigger pulled past half: this pad is the one in the hand */
        int fresh = (buttons & ~p->buttons) != 0;
        if (axes)
            for (int a = AX_L2; a <= AX_R2 && a < axis_count; a++)
                if (axes[a] > 0.5f && p->axes[a] <= 0.5f) fresh = 1;
        if (fresh) g_last_press = slot;
        if (axes) {
            int n = axis_count < AX_COUNT ? axis_count : AX_COUNT;
            memcpy(p->axes, axes, sizeof(float) * (size_t) n);
        }
        /* the gesture judges the pair as it arrives, before the game can read it */
        pthread_mutex_lock(&g_gesture_lock);
        p->buttons = buttons;
        gesture_edges(p, pad_now_ms());
        pthread_mutex_unlock(&g_gesture_lock);
    }
    /* the port's straps judge the new state before the game can read it (straps.c) */
    unsigned all;
    float all_axes[AX_COUNT];
    union_state(&all, all_axes);
    shim_straps_pad(menu_vertical(all, all_axes), menu_horizontal(all, all_axes),
                    (all >> BTN_A) & 1, (all >> BTN_B) & 1, anything(all, all_axes));
}

int shim_pad_is_connected(void) {
    for (int i = 0; i < PADS_MAX; i++) if (g_pads[i].connected) return 1;
    return 0;
}

/* the buttons held on any pad: the port's own extras read them (eid_craft.c) */
unsigned shim_pad_buttons(void) {
    unsigned all = 0;
    for (int i = 0; i < PADS_MAX; i++) if (g_pads[i].connected) all |= g_pads[i].buttons;
    return all;
}

static float union_axis(int a) {
    float best = 0.0f;
    for (int i = 0; i < PADS_MAX; i++) {
        if (!g_pads[i].connected) continue;
        float v = g_pads[i].axes[a];
        if ((v < 0 ? -v : v) > (best < 0 ? -best : best)) best = v;
    }
    return best;
}
float shim_pad_left_x(void) { return union_axis(AX_LEFT_X); }
float shim_pad_left_y(void) { return union_axis(AX_LEFT_Y); }

/* ---------- the surface objects ---------- */

void *shim_objc_alloc(void *cls);
void *shim_objc_make_class(const char *name);
void *shim_str_make(const char *text);
void *shim_arr_make_from(void **items, long count);

typedef struct {
    void *isa;
    int index;                          /* a button, or FROM_AXIS + axis index */
    int pad;                            /* the slot */
    int generation;                     /* the slot's generation this object belongs to */
} input_t;

static void *class_of(const char *name) {
    char buf[80];
    snprintf(buf, sizeof(buf), "_OBJC_CLASS_$_%s", name);
    return shim_objc_make_class(buf);
}

static void *single(const char *class_name, void **slot, int pad, int index) {
    if (!*slot) {
        input_t *o = (input_t *) shim_objc_alloc(class_of(class_name));
        if (!o) return NULL;
        o->index = index;
        o->pad = pad;
        o->generation = g_pads[pad].generation;
        *slot = o;
    }
    return *slot;
}

/* the slot an object was made for, or nothing when the slot has since moved on */
static pad_t *pad_of(void *self) {
    input_t *o = (input_t *) self;
    if (!o || o->pad < 0 || o->pad >= PADS_MAX) return NULL;
    pad_t *p = &g_pads[o->pad];
    return o->generation == p->generation ? p : NULL;
}

static void *button(pad_t *p, int n) {
    return single("GCControllerButtonInput", &p->button[n], (int) (p - g_pads), n);
}
static void *axis(pad_t *p, int n) {
    return single("GCControllerAxisInput", &p->axis[n], (int) (p - g_pads), FROM_AXIS + n);
}

/* ---------- answers to the game ---------- */

/*
 * The port's own menu focus (straps.c) takes the pads for itself while one of
 * its straps is chosen: the game is told nothing is pressed, so the press that
 * chose the strap, and every press on it, never reaches the menu below.
 */
static volatile int g_muted;

void shim_pad_mute(int on) { g_muted = on != 0; }

/* the game's codes this object reads down right now (double_tap_judge) */
static unsigned read_codes(const pad_t *p, int index, float v) {
    if (index >= FROM_AXIS_MINUS) {
        int n = index - FROM_AXIS_MINUS;
        if (v <= BUTTON_PRESS) return 0;
        return n == AX_DPAD_X ? 1u << 0 : n == AX_DPAD_Y ? 1u << 3 : 0;
    }
    if (index >= FROM_AXIS_PLUS) {
        int n = index - FROM_AXIS_PLUS;
        if (v <= BUTTON_PRESS) return 0;
        return n == AX_DPAD_X ? 1u << 1 : n == AX_DPAD_Y ? 1u << 2 : 0;
    }
    if (index >= FROM_AXIS) {
        int n = index - FROM_AXIS;
        if (n == AX_L2 || n == AX_R2) return v > BUTTON_PRESS ? 1u << (n == AX_L2 ? 9 : 12) : 0;
        int half = n == AX_LEFT_X ? 16 : n == AX_LEFT_Y ? 18 : n == AX_RIGHT_X ? 20 : n == AX_RIGHT_Y ? 22 : -1;
        if (half < 0) return 0;
        float s = p->stick_press > 0.0f ? p->stick_press : STICK_PRESS_DEFAULT;
        return (v < -s ? 1u << half : 0u) | (v > s ? 2u << half : 0u);
    }
    return v > BUTTON_PRESS ? button_code_bit(index) : 0;
}

/* what the surface holds, before the double tap has its say */
static float surface_value(pad_t *p, int index) {
    /* bases are checked in descending order: the largest first */
    if (index >= FROM_AXIS_MINUS) {
        int n = index - FROM_AXIS_MINUS;
        float v = n < AX_COUNT ? -p->axes[n] : 0.0f;
        return v > 0.0f ? v : 0.0f;     /* the other side of the axis is not pressed */
    }
    if (index >= FROM_AXIS_PLUS) {
        int n = index - FROM_AXIS_PLUS;
        float v = n < AX_COUNT ? p->axes[n] : 0.0f;
        return v > 0.0f ? v : 0.0f;
    }
    if (index >= FROM_AXIS) {
        int n = index - FROM_AXIS;
        return n < AX_COUNT ? p->axes[n] : 0.0f;
    }
    the_game_read(p, index);
    return (shown_buttons(p) & (1u << index)) ? 1.0f : 0.0f;
}

static float input_force(void *self) {
    input_t *o = (input_t *) self;
    pad_t *p = pad_of(self);
    if (!p || !p->connected || g_muted) return 0.0f;
    float v = surface_value(p, o->index);
    if (p->dev) double_tap_judge(p, read_codes(p, o->index, v));
    return v;
}

long shim_pad_is_pressed(void *self, const char *sel) {
    (void) sel;
    float f = input_force(self);
    return f > 0.1f ? 1 : 0;            /* for a trigger, travel counts as a press */
}

float shim_pad_force(void *self, const char *sel) {
    (void) sel;
    return input_force(self);
}

/* an axis answers the same way, but its class differs — hence its own entry */
float shim_pad_axis_value(void *self, const char *sel) {
    (void) sel;
    return input_force(self);
}

/* d-pad and sticks: a pair of neighbouring axes */
void *shim_pad_x(void *self, const char *sel) {
    (void) sel;
    pad_t *p = pad_of(self);
    return p ? axis(p, ((input_t *) self)->index - FROM_AXIS) : NULL;
}

void *shim_pad_y(void *self, const char *sel) {
    (void) sel;
    pad_t *p = pad_of(self);
    return p ? axis(p, ((input_t *) self)->index - FROM_AXIS + 1) : NULL;
}

/* the button set's members: self is the GCExtendedGamepad of a slot */
static void *pad_button(void *self, int n) {
    pad_t *p = pad_of(self);
    return p ? button(p, n) : NULL;
}

void *shim_pad_buttonA(void *s, const char *l) { (void) l; return pad_button(s, BTN_A); }
void *shim_pad_buttonB(void *s, const char *l) { (void) l; return pad_button(s, BTN_B); }
void *shim_pad_buttonX(void *s, const char *l) { (void) l; return pad_button(s, BTN_X); }
void *shim_pad_buttonY(void *s, const char *l) { (void) l; return pad_button(s, BTN_Y); }
void *shim_pad_L1(void *s, const char *l) { (void) l; return pad_button(s, BTN_L1); }
void *shim_pad_R1(void *s, const char *l) { (void) l; return pad_button(s, BTN_R1); }
void *shim_pad_menu(void *s, const char *l) { (void) l; return pad_button(s, BTN_MENU); }
void *shim_pad_options(void *s, const char *l) { (void) l; return pad_button(s, BTN_OPTIONS); }

void *shim_pad_L2(void *s, const char *l) {
    (void) l;
    pad_t *p = pad_of(s);
    return p ? single("GCControllerButtonInput", &p->trigger[0], (int) (p - g_pads), FROM_AXIS + AX_L2) : NULL;
}
void *shim_pad_R2(void *s, const char *l) {
    (void) l;
    pad_t *p = pad_of(s);
    return p ? single("GCControllerButtonInput", &p->trigger[1], (int) (p - g_pads), FROM_AXIS + AX_R2) : NULL;
}

/*
 * A direction button of a pad. The offset picks the axis of the pair: 0 is X
 * (left and right), 1 is Y (up and down); the side says which sign counts as
 * pressed. Our Y axis is already flipped in Java, so up is positive.
 */
static void *direction(void *self, int offset, int negative) {
    pad_t *p = pad_of(self);
    if (!p) return NULL;
    int n = ((input_t *) self)->index - FROM_AXIS + offset;
    if (n < 0 || n >= AX_COUNT) return NULL;
    return single("GCControllerButtonInput", &p->direction[n][negative], (int) (p - g_pads),
                  (negative ? FROM_AXIS_MINUS : FROM_AXIS_PLUS) + n);
}

void *shim_pad_up(void *s, const char *l) { (void) l; return direction(s, 1, 0); }
void *shim_pad_down(void *s, const char *l) { (void) l; return direction(s, 1, 1); }
void *shim_pad_right(void *s, const char *l) { (void) l; return direction(s, 0, 0); }
void *shim_pad_left(void *s, const char *l) { (void) l; return direction(s, 0, 1); }

void *shim_pad_dpad(void *s, const char *l) {
    (void) l;
    pad_t *p = pad_of(s);
    return p ? single("GCControllerDirectionPad", &p->dpad, (int) (p - g_pads), FROM_AXIS + AX_DPAD_X) : NULL;
}
void *shim_pad_left_stick(void *s, const char *l) {
    (void) l;
    pad_t *p = pad_of(s);
    return p ? single("GCControllerDirectionPad", &p->left_stick, (int) (p - g_pads), FROM_AXIS + AX_LEFT_X) : NULL;
}
void *shim_pad_right_stick(void *s, const char *l) {
    (void) l;
    pad_t *p = pad_of(s);
    return p ? single("GCControllerDirectionPad", &p->right_stick, (int) (p - g_pads), FROM_AXIS + AX_RIGHT_X) : NULL;
}

/* self is the GCController of a slot */
void *shim_pad_gamepad(void *s, const char *l) {
    (void) l;
    pad_t *p = pad_of(s);
    if (!p || !p->connected) return NULL;          /* no pad, no button set */
    return single("GCExtendedGamepad", &p->gamepad, (int) (p - g_pads), 0);
}

/*
 * The player's number on the pad.
 *
 * iOS lets the game set it and read it back, and the game does exactly that
 * when it assigns a controller to a player. Without the setter the number
 * stayed at whatever we answered and the assignment quietly went nowhere.
 */
void shim_pad_set_player_index(void *self, const char *sel, long idx) {
    (void) sel;
    pad_t *p = pad_of(self);
    if (!p) return;
    /* the game re-assigns it every frame; say it once, not sixty times a second */
    if (p->player_index != idx) LOG("pad: slot %d player index set to %ld", (int) (p - g_pads), idx);
    p->player_index = idx;
}

long shim_pad_player_index(void *s, const char *l) {
    (void) l;
    pad_t *p = pad_of(s);
    return p && p->player_index >= 0 ? p->player_index : 0;
}

/* from Java (loader.c padFamily): a slot's family and the vendorName iOS would give its pad */
void shim_pad_family(int slot, int family, const char *ios_name) {
    if (slot < 0 || slot >= PADS_MAX) return;
    g_family[slot] = family >= 0 ? family : 0;
    pthread_mutex_lock(&g_name_lock);
    if (strcmp(g_vendor_name[slot], ios_name ? ios_name : "")) {
        snprintf(g_vendor_name[slot], sizeof(g_vendor_name[slot]), "%s", ios_name ? ios_name : "");
        g_vendor_stale[slot] = 1;
    }
    pthread_mutex_unlock(&g_name_lock);
    LOG("pad: slot %d draws as family %d, named \"%s\"", slot, family, ios_name ? ios_name : "");
}

/* the pictures the GUIDE shows: the family of the last pad pressed, else of a connected one, else 0 (xbox) */
int shim_pad_icon_family(void) {
    int s = g_last_press;
    if (s >= 0 && s < PADS_MAX && g_pads[s].connected) return g_family[s];
    for (int i = 0; i < PADS_MAX; i++)
        if (g_pads[i].connected) return g_family[i];
    return 0;
}

/* -[GCController vendorName]: the name Java gave the slot (shim_pad_family), "Gamepad" without one */
void *shim_pad_name(void *s, const char *l) {
    (void) l;
    static void *pad_name;
    pad_t *p = pad_of(s);
    int slot = p ? (int) (p - g_pads) : -1;
    void *name = NULL;
    if (slot >= 0) {
        pthread_mutex_lock(&g_name_lock);
        if (g_vendor_stale[slot] || !g_vendor_obj[slot]) {
            g_vendor_obj[slot] = g_vendor_name[slot][0] ? shim_str_make(g_vendor_name[slot]) : NULL;
            g_vendor_stale[slot] = 0;
        }
        name = g_vendor_obj[slot];
        pthread_mutex_unlock(&g_name_lock);
    }
    if (name) return name;
    if (!pad_name) pad_name = shim_str_make("Gamepad");
    return pad_name;
}

/*
 * The list of connected pads. An empty array is a legitimate answer: that is
 * how the engine learns there is no gamepad and leaves control to touch.
 *
 * We keep one ready list and hand back the same object as long as the line-up
 * has not changed. Real GameController does the same: `controllers` returns
 * the same array until pads are plugged in or unplugged.
 *
 * There used to be a NEW array per request here, and the request comes from
 * the shell's per-frame update. There was nothing to free it with: `release`
 * and `dealloc` are no-ops for us. Measured on the device: 465 bytes per
 * frame, about 1.6 MB per minute, for the whole session — even with the game
 * simply sitting in the menu and no pad in sight.
 */
void *shim_pad_controllers(void *cls, const char *sel) {
    (void) cls; (void) sel;
    static void *list;
    static int list_lineup = -1;
#if ISAAC_DIAG
    /* CONTROLS PROBE: what we answer when the game asks for the pads */
    { static int said, was = -1;
      int now = shim_pad_is_connected() ? 1 : 0;
      if (now != was || said < 3) { was = now; said++;
          LOG("controls: the game asked for controllers, we say %d", now); } }
#endif
    if (!list || list_lineup != g_lineup) {
        void *items[PADS_MAX];
        long n = 0;
        for (int i = 0; i < PADS_MAX; i++) {
            pad_t *p = &g_pads[i];
            if (!p->connected) continue;
            if (!p->controller) single("GCController", &p->controller, i, 0);
            if (p->controller) items[n++] = p->controller;
        }
        list = shim_arr_make_from(items, n);
        list_lineup = g_lineup;
    }
    return list;
}

/* the GCController of a slot, or nothing while it is not connected */
void *shim_pad_controller_object(int slot) {
    if (slot < 0 || slot >= PADS_MAX || !g_pads[slot].connected) return NULL;
    return g_pads[slot].controller;
}

/* how many slots there are; coop.c walks them */
int shim_pad_slots(void) { return PADS_MAX; }

/* the buttons a slot holds right now, as the game sees them; 0 for an empty slot */
unsigned shim_pad_slot_buttons(int slot) {
    if (slot < 0 || slot >= PADS_MAX || !g_pads[slot].connected) return 0;
    return shown_buttons(&g_pads[slot]);
}

/* the largest axis a slot holds right now (sticks and triggers), 0 for an empty slot (devices.c) */
float shim_pad_slot_axis_max(int slot) {
    if (slot < 0 || slot >= PADS_MAX || !g_pads[slot].connected) return 0.0f;
    float best = 0.0f;
    for (int a = 0; a < AX_COUNT; a++) {
        float v = g_pads[slot].axes[a];
        if (v < 0) v = -v;
        if (v > best) best = v;
    }
    return best;
}

/*
 * The slot a GCController of ours stands for, or -1: the game keeps the
 * controller its device was made from at device+0x608 (devices.c). A
 * controller of a slot that has since moved on is no longer the slot's.
 */
int shim_pad_slot_of(const void *controller) {
    if (!controller) return -1;
    for (int i = 0; i < PADS_MAX; i++)
        if (g_pads[i].connected && g_pads[i].controller == controller) return i;
    return -1;
}
