/*
 * THE BOOK OF DEVICES, THE LAYER'S SIDE (batch 2, U4, 01.10.2026).
 *
 * Who is who got lost at the border between Java, the layer and the game.
 * The layer guessed a device's kind from its NUMBER: eid_craft.c counted
 * everything but 0 as a pad (the touch screen 0x10dead too), eid.c's button
 * pictures took "> 0" for a pad, items.c "!= 0", and three files called the
 * keyboard's class the touch device. The measurements disagreed because the
 * rule was wrong: the game numbers its devices itself (the keyboard 0, the
 * touch screen 0x10dead, a pad 2 in the 28.09 report).
 *
 * So the kind is read off the device's CLASS, as the game made it:
 *
 *   0xa91a10  KAGE::Input::DeviceGamepad  one per GCController (0x8ca698);
 *             the controller it was made from at +0x608 -> pad.c's slot ->
 *             the Android device Java told us for that slot (padIdentity)
 *   0xa92d88  DeviceiOS                   the touch screen, 0x10dead
 *   0xa94958  DeviceKeyboard              the keyboard, through the shell
 *
 * The input system (0xac5528) counts its devices (0x8b5c24), names the i-th
 * (0x8b5c68) and gives the object of an id (0x8b5d18). The table is taken
 * once a frame between frames - a handful of calls - and the readers ask it;
 * an id not in it yet is looked up live. Frame thread only.
 *
 * The report (report builds only) gets the table whenever it changes:
 *   devices: [0x0 keyboard -> p0] [0x10dead screen] [0x2 pad slot 0 id 7 3f9a1c2e -> p1]
 * and the pause's two probes live here as well (U2, U3): they read players
 * and devices between frames and hook nothing new.
 */
#include <jni.h>
#include <stdio.h>
#include <string.h>
#include "diag.h"
#include "devices.h"

unsigned long shim_image_base(void);
int shim_report_active(void);
void report_line(const char *kind, const char *text);
int shim_eid_players(unsigned long *out, int max);
int shim_eid_readable(const void *p, unsigned long n);
int shim_pad_slot_of(const void *controller);
unsigned shim_pad_slot_buttons(int slot);
float shim_pad_slot_axis_max(int slot);
int shim_touch_any_down(void);
void shim_touch_push(int id, int phase, float x, float y);

#define GAME_PTR          0xac3b90
#define INPUT_SYSTEM      0xac5528        /* the object itself */
#define FN_DEVICE_COUNT   0x8b5c24        /* (input) -> n */
#define FN_DEVICE_AT      0x8b5c68        /* (input, i) -> device id, -1 none */
#define FN_DEVICE_OF      0x8b5d18        /* (input, id) -> the device object, or nought */
#define FN_DEVICE_HELD    0x8b6d10        /* (input, id): the device's +0xd8, "is anything held" */
#define VT_PAD            0xa91a10
#define VT_SCREEN         0xa92d88
#define VT_KEYBOARD       0xa94958
#define DEVICE_CONTROLLER 0x608           /* the GCController the pad device was made from */
#define PLAYER_DEVICE     0x19ec
#define PLAYERS_MAX       8
#define TABLE_MAX         8
#define PAD_SLOTS         4

typedef struct {
    int id;
    int kind;
    int slot;                             /* pad.c's slot for a pad, else -1 */
    unsigned long object;
} book_t;

static book_t g_table[TABLE_MAX];
static int g_table_n;

const char *shim_device_kind_name(int kind) {
    switch (kind) {
        case DEVICE_PAD: return "pad";
        case DEVICE_SCREEN: return "screen";
        case DEVICE_KEYBOARD: return "keyboard";
        case DEVICE_OTHER: return "other";
        default: return "none";
    }
}

static unsigned long device_object(unsigned long b, int id) {
    if (id == -1) return 0;
    return ((unsigned long (*)(unsigned long, int)) (b + FN_DEVICE_OF))(b + INPUT_SYSTEM, id);
}

static int kind_of_object(unsigned long b, unsigned long obj) {
    if (!obj || !shim_eid_readable((const void *) obj, 8)) return DEVICE_NONE;
    unsigned long vt = *(const unsigned long *) obj - b;
    if (vt == VT_PAD) return DEVICE_PAD;
    if (vt == VT_SCREEN) return DEVICE_SCREEN;
    if (vt == VT_KEYBOARD) return DEVICE_KEYBOARD;
    return DEVICE_OTHER;
}

static int slot_of_object(unsigned long obj) {
    if (!obj || !shim_eid_readable((const void *) (obj + DEVICE_CONTROLLER), 8)) return -1;
    return shim_pad_slot_of(*(void *const *) (obj + DEVICE_CONTROLLER));
}

static void table_build(unsigned long b) {
    unsigned long input = b + INPUT_SYSTEM;
    int n = ((int (*)(unsigned long)) (b + FN_DEVICE_COUNT))(input), k = 0;
    for (int i = 0; i < n && k < TABLE_MAX; i++) {
        int id = ((int (*)(unsigned long, int)) (b + FN_DEVICE_AT))(input, i);
        if (id == -1) continue;
        unsigned long obj = device_object(b, id);
        g_table[k].id = id;
        g_table[k].object = obj;
        g_table[k].kind = kind_of_object(b, obj);
        g_table[k].slot = g_table[k].kind == DEVICE_PAD ? slot_of_object(obj) : -1;
        k++;
    }
    g_table_n = k;
}

int shim_device_kind(int id) {
    if (id == -1) return DEVICE_NONE;
    for (int i = 0; i < g_table_n; i++) if (g_table[i].id == id) return g_table[i].kind;
    unsigned long b = shim_image_base();
    return b ? kind_of_object(b, device_object(b, id)) : DEVICE_NONE;
}

unsigned long shim_device_object(int id) {
    if (id == -1) return 0;
    for (int i = 0; i < g_table_n; i++) if (g_table[i].id == id) return g_table[i].object;
    unsigned long b = shim_image_base();
    return b ? device_object(b, id) : 0;
}

int shim_device_pad_slot(int id) {
    if (id == -1) return -1;
    for (int i = 0; i < g_table_n; i++) if (g_table[i].id == id) return g_table[i].slot;
    unsigned long b = shim_image_base();
    if (!b) return -1;
    unsigned long obj = device_object(b, id);
    return kind_of_object(b, obj) == DEVICE_PAD ? slot_of_object(obj) : -1;
}

int shim_player_device(int k) {
    unsigned long p[PLAYERS_MAX];
    int n = shim_eid_players(p, PLAYERS_MAX);
    if (k < 0 || k >= n || !p[k] || !shim_eid_readable((const void *) (p[k] + PLAYER_DEVICE), 4)) return -1;
    return *(const int *) (p[k] + PLAYER_DEVICE);
}

int shim_device_keyboard(void) {
    for (int i = 0; i < g_table_n; i++) if (g_table[i].kind == DEVICE_KEYBOARD) return g_table[i].id;
    unsigned long b = shim_image_base();
    if (!b) return -1;
    unsigned long input = b + INPUT_SYSTEM;
    int n = ((int (*)(unsigned long)) (b + FN_DEVICE_COUNT))(input);
    for (int i = 0; i < n; i++) {
        int id = ((int (*)(unsigned long, int)) (b + FN_DEVICE_AT))(input, i);
        if (id != -1 && kind_of_object(b, device_object(b, id)) == DEVICE_KEYBOARD) return id;
    }
    return -1;
}

int shim_device_of_pad_slot(int slot) {
    if (slot < 0) return -1;
    for (int i = 0; i < g_table_n; i++) if (g_table[i].kind == DEVICE_PAD && g_table[i].slot == slot) return g_table[i].id;
    unsigned long b = shim_image_base();
    if (!b) return -1;
    unsigned long input = b + INPUT_SYSTEM;
    int n = ((int (*)(unsigned long)) (b + FN_DEVICE_COUNT))(input);
    for (int i = 0; i < n; i++) {
        int id = ((int (*)(unsigned long, int)) (b + FN_DEVICE_AT))(input, i);
        unsigned long obj = id == -1 ? 0 : device_object(b, id);
        if (kind_of_object(b, obj) == DEVICE_PAD && slot_of_object(obj) == slot) return id;
    }
    return -1;
}

/*
 * WHICH ANDROID DEVICE A SLOT IS (Java's padIdentity, on the UI thread, when
 * a slot is taken or freed): the Android id and the first characters of the
 * device's descriptor, for the report's table and, later, for the shake of
 * the right pad (the vibration's area).
 */
static volatile int g_slot_android[PAD_SLOTS] = { -1, -1, -1, -1 };
static char g_slot_desc[PAD_SLOTS][12];
static volatile int g_slots_changed;

JNIEXPORT void JNICALL
Java_ru_isaac_port_MainActivity_padIdentity(JNIEnv *env, jobject thiz, jint slot, jint android_id, jstring desc) {
    (void) thiz;
    if (slot < 0 || slot >= PAD_SLOTS) return;
    const char *s = desc ? (*env)->GetStringUTFChars(env, desc, NULL) : NULL;
    snprintf(g_slot_desc[slot], sizeof(g_slot_desc[slot]), "%s", s ? s : "");
    if (s) (*env)->ReleaseStringUTFChars(env, desc, s);
    g_slot_android[slot] = android_id;
    g_slots_changed++;
}

/*
 * WHAT A DEVICE NUMBER WAS (coop.c, the seats of a continued run; the owner,
 * 05.10.2026: "on returning, players must get the characters they played").
 * The game numbers its devices with a counter (0x8a761c: [0xac5380] + 1): a
 * pad that comes back - asleep and woken, or after a restart - is a new
 * number. What stays is what the device IS: the screen, the keyboard, or a
 * pad by its Android descriptor (padIdentity). The ledger keeps that for
 * every number seen in this process - a gone device keeps its line - and
 * coop.c turns an identity back into today's number.
 */
#define LEDGER_MAX 32
static struct { int id; char what[16]; } g_ledger[LEDGER_MAX];
static int g_ledger_n;

static int identity_now(const book_t *d, char *out, int room) {
    if (d->kind == DEVICE_SCREEN) return snprintf(out, (size_t) room, "screen"), 1;
    if (d->kind == DEVICE_KEYBOARD) return snprintf(out, (size_t) room, "keyboard"), 1;
    if (d->kind == DEVICE_PAD && d->slot >= 0 && d->slot < PAD_SLOTS && g_slot_desc[d->slot][0])
        return snprintf(out, (size_t) room, "pad:%s", g_slot_desc[d->slot]), 1;
    return 0;
}

static void ledger_note(void) {
    for (int i = 0; i < g_table_n; i++) {
        char what[16];
        if (!identity_now(&g_table[i], what, sizeof(what))) continue;
        int k = 0;
        while (k < g_ledger_n && g_ledger[k].id != g_table[i].id) k++;
        if (k == g_ledger_n) {
            if (g_ledger_n == LEDGER_MAX) {                   /* full: the oldest goes */
                memmove(&g_ledger[0], &g_ledger[1], sizeof(g_ledger[0]) * (LEDGER_MAX - 1));
                k = --g_ledger_n;
            }
            g_ledger_n++;
            g_ledger[k].id = g_table[i].id;
        }
        snprintf(g_ledger[k].what, sizeof(g_ledger[k].what), "%s", what);
    }
}

/* what device `id` is or was in this process ("screen", "keyboard", "pad:<descriptor>"); 0 if unknown */
int shim_device_identity(int id, char *out, int room) {
    if (id == -1 || room < 1) return 0;
    for (int i = 0; i < g_ledger_n; i++)
        if (g_ledger[i].id == id) { snprintf(out, (size_t) room, "%s", g_ledger[i].what); return 1; }
    return 0;
}

/* today's number of the device that is `what`, or -1 when it is not here */
int shim_device_of_identity(const char *what) {
    if (!what || !*what) return -1;
    for (int i = 0; i < g_table_n; i++) {
        char now[16];
        if (identity_now(&g_table[i], now, sizeof(now)) && !strcmp(now, what)) return g_table[i].id;
    }
    return -1;
}

/* the Android device behind a pad slot, or -1 */
int shim_device_slot_android(int slot) {
    return slot >= 0 && slot < PAD_SLOTS ? g_slot_android[slot] : -1;
}

/*
 * A PALM (batch 2, U7): Java hands a pointer that went up with FLAG_CANCELED
 * here instead of as an ordinary release. It goes down the same road as any
 * touch, as phase 4 - UIKit's touchesCancelled - and whoever takes it is named
 * in the report (uikit.c tell_claim asks shim_devices_palm_told).
 */
static volatile int g_palm_id = -1;

JNIEXPORT void JNICALL
Java_ru_isaac_port_MainActivity_touchPalm(JNIEnv *env, jobject thiz, jint id, jfloat x, jfloat y) {
    (void) env; (void) thiz;
    g_palm_id = id;
    shim_touch_push(id, 4, x, y);
    g_palm_id = -1;
}

void shim_devices_palm_told(const char *who, float x, float y) {
#if ISAAC_DIAG
    static int said;
    int id = g_palm_id;
    if (id < 0 || said >= 10 || !shim_report_active()) return;
    said++;
    char line[140];
    snprintf(line, sizeof(line), "palm cancel id %d at (%.0f, %.0f) over %s", id, (double) x, (double) y, who);
    report_line("touch", line);
#else
    (void) who; (void) x; (void) y;
#endif
}

/* ---------- the report: what is held, on which device (coop.c's seat journal) ---------- */

/* the pause button was handed to the game as a tap (reset.c, the touch thread) */
static volatile int g_pause_pressed;
void shim_devices_pause_pressed(void) { g_pause_pressed = 1; }

/* the straps' layer hook sees every sprite layer drawn (straps.c, U3) */
void shim_devices_layer_drawn(unsigned long sprite, unsigned long layer);

#if ISAAC_DIAG
#define DEVICE_ON         0xd             /* byte: the device is on */
#define DEVICE_MASK_A     0x30            /* the +0xd8 answer's parts, read off 0x8a8688: */
#define DEVICE_MASK_B     0x40            /*   two button masks, */
#define DEVICE_ANALOG_N   0x1c            /*   analog values above the device's own */
#define DEVICE_ANALOG     0xd0            /*   threshold at +0xe4, */
#define DEVICE_ANALOG_AT  0xe4
#define DEVICE_TOUCHES    0xe8            /*   and the touches it keeps: 0x50 each, */
#define DEVICE_TOUCH_SIZE 0x50
#define DEVICE_TOUCH_HELD 0x24            /*   held while this byte has bit 0 or 1, */
#define DEVICE_TOUCH_N    0x5e8           /*   this many (0x8a8570 copies them the same way) */

static int device_held(unsigned long b, int id) {
    if (id == -1) return 0;
    return ((int (*)(unsigned long, int)) (b + FN_DEVICE_HELD))(b + INPUT_SYSTEM, id) & 1;
}

/* the touch screen's "held" in parts: its masks, its analog values, its touches */
static void screen_parts(unsigned long obj, int *masks, int *analog, int *fingers) {
    *masks = *analog = *fingers = 0;
    if (!obj || !shim_eid_readable((const void *) obj, DEVICE_TOUCH_N + 4)) return;
    *masks = *(const unsigned long *) (obj + DEVICE_MASK_A) != 0 || *(const unsigned long *) (obj + DEVICE_MASK_B) != 0;
    unsigned n = *(const unsigned *) (obj + DEVICE_ANALOG_N);
    const float *v = *(const float *const *) (obj + DEVICE_ANALOG);
    float at = *(const float *) (obj + DEVICE_ANALOG_AT);
    for (unsigned i = 0; v && i < n && i < 64 && shim_eid_readable(v + i, 4); i++) if (v[i] > at) (*analog)++;
    unsigned t = *(const unsigned *) (obj + DEVICE_TOUCH_N);
    for (unsigned i = 0; i < t && i < 16; i++) {
        const unsigned char *e = (const unsigned char *) (obj + DEVICE_TOUCHES + i * DEVICE_TOUCH_SIZE);
        if (e[DEVICE_TOUCH_HELD] & 3) (*fingers)++;
    }
}

/* the touch screen is held by the game's reckoning while no real finger lies on the glass */
int shim_devices_touch_ghost(char *parts, int room) {
    unsigned long b = shim_image_base();
    if (!b) return 0;
    for (int i = 0; i < g_table_n; i++) {
        if (g_table[i].kind != DEVICE_SCREEN) continue;
        if (!device_held(b, g_table[i].id) || shim_touch_any_down()) return 0;
        int m, a, f;
        screen_parts(g_table[i].object, &m, &a, &f);
        snprintf(parts, (size_t) room, "%s%s%s%s", m ? "mask " : "", a ? "analog " : "", f ? "finger " : "",
                 m || a || f ? "" : "stick");
        return 1;
    }
    return 0;
}

/* is there real input on a device: a finger, a pad's button or deflection, a key (the game's own view) */
int shim_devices_real_input(int id) {
    unsigned long b = shim_image_base();
    if (!b) return 0;
    switch (shim_device_kind(id)) {
        case DEVICE_SCREEN: return shim_touch_any_down();
        case DEVICE_PAD: {
            int slot = shim_device_pad_slot(id);
            return slot >= 0 && (shim_pad_slot_buttons(slot) || shim_pad_slot_axis_max(slot) >= 0.3f);
        }
        case DEVICE_KEYBOARD: return device_held(b, id);
        default: return 0;
    }
}

/* every device's "held", as the dispatcher asks it (+0xd8), and what is behind it */
void shim_devices_held_line(char *out, int room) {
    unsigned long b = shim_image_base();
    int at = snprintf(out, (size_t) room, "held:");
    if (!b) return;
    for (int i = 0; i < g_table_n && at < room - 80; i++) {
        const book_t *d = &g_table[i];
        int held = device_held(b, d->id);
        if (d->kind == DEVICE_SCREEN) {
            int m, a, f;
            screen_parts(d->object, &m, &a, &f);
            at += snprintf(out + at, (size_t) (room - at), " touch[%d masks %d analog %d fingers %d real %d]",
                           held, m, a, f, shim_touch_any_down());
        } else if (d->kind == DEVICE_KEYBOARD) {
            at += snprintf(out + at, (size_t) (room - at), " keyboard[%d]", held);
        } else if (d->kind == DEVICE_PAD) {
            at += snprintf(out + at, (size_t) (room - at), " pad[slot %d: %d buttons %#x axis %.2f]", d->slot, held,
                           d->slot >= 0 ? shim_pad_slot_buttons(d->slot) : 0u,
                           (double) (d->slot >= 0 ? shim_pad_slot_axis_max(d->slot) : 0.0f));
        } else {
            at += snprintf(out + at, (size_t) (room - at), " %#x[%d]", (unsigned) d->id, held);
        }
    }
}

/* ---------- the report: the table, when it changes ---------- */

static void table_report(unsigned long b) {
    static char was[400];
    static int lines;
    if (lines >= 40) return;
    char line[400];
    int at = 0;
    line[0] = 0;
    unsigned long p[PLAYERS_MAX];
    int np = shim_eid_players(p, PLAYERS_MAX);
    for (int i = 0; i < g_table_n && at < (int) sizeof(line) - 90; i++) {
        const book_t *d = &g_table[i];
        at += snprintf(line + at, sizeof(line) - (size_t) at, "%s[%#x %s", at ? " " : "", (unsigned) d->id,
                       shim_device_kind_name(d->kind));
        if (d->kind == DEVICE_PAD)
            at += snprintf(line + at, sizeof(line) - (size_t) at, " slot %d id %d %s", d->slot,
                           shim_device_slot_android(d->slot), d->slot >= 0 ? g_slot_desc[d->slot] : "-");
        for (int k = 0; k < np; k++)
            if (p[k] && shim_eid_readable((const void *) (p[k] + PLAYER_DEVICE), 4)
                && *(const int *) (p[k] + PLAYER_DEVICE) == d->id)
                at += snprintf(line + at, sizeof(line) - (size_t) at, " -> p%d", k);
        at += snprintf(line + at, sizeof(line) - (size_t) at, "]");
    }
    if (!strcmp(line, was)) return;
    memcpy(was, line, sizeof(was));
    lines++;
    report_line("devices", line[0] ? line : "none");
    (void) b;
}

/* ---------- U2: every press of the pause button, followed into the game ---------- */

#define GAME_PAUSE        0x10dd20
#define PAUSE_STATE       0x2b8           /* 1 while the pause menu is up (straps.c) */
#define OVERLAY_MODE      8               /* the touch overlay's mode ([ov+8]) */
#define WIDGET_BITS       8               /* a widget's kind bits, */
#define WIDGET_LIVE       0xc             /* and its byte "takes touches" (0x8608c0 writes it) */
#define PAUSE_WAIT        10              /* frames a press has to become the pause */
unsigned long shim_pocket_overlay_at(void);
unsigned long shim_reset_pause_widget(void);

static long g_frame;

static int pause_state(unsigned long game) {
    if (!game || !shim_eid_readable((const void *) (game + GAME_PAUSE + PAUSE_STATE), 4)) return -1;
    return *(const int *) (game + GAME_PAUSE + PAUSE_STATE);
}

static void pause_watch(unsigned long game) {
    static int lines, waiting;
    static long pressed_at;
    static int mode, live, bits, p0, was;
    if (g_pause_pressed) {
        g_pause_pressed = 0;
        unsigned long ov = shim_pocket_overlay_at(), w = shim_reset_pause_widget();
        mode = ov && shim_eid_readable((const void *) (ov + OVERLAY_MODE), 4) ? *(const int *) (ov + OVERLAY_MODE) : -1;
        live = w && shim_eid_readable((const void *) w, WIDGET_LIVE + 1) ? *(const unsigned char *) (w + WIDGET_LIVE) : -1;
        bits = w && shim_eid_readable((const void *) w, WIDGET_LIVE + 1) ? *(const int *) (w + WIDGET_BITS) : -1;
        p0 = shim_player_device(0);
        was = pause_state(game);
        pressed_at = g_frame;
        waiting = 1;
    }
    if (!waiting) return;
    int now = pause_state(game);
    char line[200];
    if (was == 1) {
        snprintf(line, sizeof(line), "press at frame %ld while paused already (p0 %#x)", pressed_at, (unsigned) p0);
    } else if (now == 1) {
        snprintf(line, sizeof(line), "press at frame %ld -> paused after %ld frames (p0 %#x, mode %d, live %d)",
                 pressed_at, g_frame - pressed_at, (unsigned) p0, mode, live);
    } else if (g_frame - pressed_at >= PAUSE_WAIT) {
        snprintf(line, sizeof(line), "press at frame %ld LOST (live %d, bits %#x, mode %d, p0 %#x %s)",
                 pressed_at, live, (unsigned) bits, mode, (unsigned) p0, shim_device_kind_name(shim_device_kind(p0)));
    } else {
        return;
    }
    waiting = 0;
    if (lines >= 60) return;
    lines++;
    report_line("pause", line);
}

/* ---------- U3: the pause's list of items, and Tainted Jacob's players ---------- */

/*
 * What the pause's MyStuff list draws (0x8a25b8..0x8a2830, read 01.10.2026):
 * player 0's history, p+0x24d0..+0x24d8, 0x1c a record, newest first from the
 * scroll [pause+0x2d8], at most 24 drawn. A record's id is +8 (nought when
 * the byte +4 is set); it is drawn when the item config knows it (0x73f7f4)
 * and the player has it (0x4eeb74 with flag 0) - ids below 733 as layer 6 of
 * the sprite pause+0x590 through 0x89e550 (the straps' hook sees those), the
 * others through 0x775ebc. The whole list is skipped under the seed effect 10
 * (NO_HUD: the effect set at Game+0x25da0, a tree keyed at +0x1c).
 *
 * HasCollectible (0x4eeb74) is NOT called here: its flag-0 road reaches
 * 0x7027f8, 0x506238, 0x6fdd3c, 0x76f8a8.., 0x5066d0, 0x5d169c and itself -
 * not checked to be read-only end to end (the plan's rule, U3). So "has" is
 * read from the player's own counts, [p+0x1ab8][id] > 0 (ids below 733 only),
 * with the one gate of its start that matters here: a co-op ghost
 * ([p+0x284c]) has nothing unless the item's config byte +0xc1 has bit 7.
 * GetCollectible (0x73f7f4) is a leaf for id >= 0 and is read the same way.
 */
#define PAUSE_SPRITE      0x2e0
#define PAUSE_ITEMS       0x590           /* the list's sprite; layer 6 is an item */
#define PAUSE_SCROLL      0x2d8
#define PLAYER_TYPE       0x1738
#define PLAYER_VARIANT    0x3c
#define PLAYER_PARENT     0x390
#define PLAYER_TWIN       0x2580
#define PLAYER_GHOST      0x284c
#define PLAYER_HISTORY    0x24d0          /* begin, end; +0x18 is the history's own number */
#define HISTORY_X18       0x24e8
#define PLAYER_OWNED      0x1ab8          /* one int per id below 733 */
#define HISTORY_RECORD    0x1c
#define MANAGER_PTR       0xac2428
#define ITEM_CONFIG       0x55f30         /* in the manager: a vector of item configs */
#define ITEM_ALWAYS       0xc1            /* bit 7: counts for a ghost too */
#define SEED_EFFECTS      0x25da0         /* in the Game: the tree's header */
#define BIG_ID            733
#define LIST_MAX          24
#define TJACOB            37
#define TJACOB_GHOST      39

/* the Tear Cap probe's items (statsplus.c CAP_ITEMS): the trace a probe could leave in a history */
static const int CAP_PROBE[] = { 1, 6, 32, 183, 255, 564, 120 };

static int g_layer3_now, g_layer6_now, g_layer3, g_layer6;
static unsigned long g_sprite_pause, g_sprite_items;

void shim_devices_layer_drawn(unsigned long sprite, unsigned long layer) {
    if (!g_sprite_pause) return;
    if (sprite == g_sprite_pause && layer == 3) g_layer3_now++;
    else if (sprite == g_sprite_items && layer == 6) g_layer6_now++;
}

static unsigned long item_config(unsigned long b, int id) {
    if (id < 0) return 0;
    unsigned long m = *(const unsigned long *) (b + MANAGER_PTR);
    if (!m || !shim_eid_readable((const void *) (m + ITEM_CONFIG), 16)) return 0;
    const unsigned long *v = (const unsigned long *) (m + ITEM_CONFIG);
    if (!v[0] || v[1] < v[0] || (unsigned long) id >= (v[1] - v[0]) / 8) return 0;
    const unsigned long *at = (const unsigned long *) v[0] + id;
    return shim_eid_readable(at, 8) ? *at : 0;
}

static int owns(unsigned long p, unsigned long cfg, int id) {
    if (id < 0 || id >= BIG_ID) return 0;
    if (*(const unsigned char *) (p + PLAYER_GHOST)
        && !(shim_eid_readable((const void *) (cfg + ITEM_ALWAYS), 1) && (*(const unsigned char *) (cfg + ITEM_ALWAYS) & 0x80)))
        return 0;
    const int *owned = *(const int *const *) (p + PLAYER_OWNED);
    return owned && shim_eid_readable(owned + id, 4) && owned[id] > 0;
}

static int no_hud(unsigned long game) {
    unsigned long header = game + SEED_EFFECTS;
    if (!shim_eid_readable((const void *) header, 8)) return -1;
    unsigned long node = *(const unsigned long *) header, found = header;
    for (int guard = 0; node && guard < 64; guard++) {
        if (!shim_eid_readable((const void *) node, 0x20)) return -1;
        if (*(const int *) (node + 0x1c) < 10) node = *(const unsigned long *) (node + 8);
        else { found = node; node = *(const unsigned long *) node; }
    }
    return found != header && *(const int *) (found + 0x1c) < 11;
}

static void tjpause(unsigned long b, unsigned long game) {
    static int lines;
    if (lines >= 20) return;
    lines++;
    unsigned long p[PLAYERS_MAX];
    int n = shim_eid_players(p, PLAYERS_MAX);
    char line[600];
    int at = snprintf(line, sizeof(line), "players %d:", n);
    for (int i = 0; i < n && at < (int) sizeof(line) - 120; i++) {
        unsigned long q = p[i];
        if (!q || !shim_eid_readable((const void *) q, PLAYER_GHOST + 1)) {
            at += snprintf(line + at, sizeof(line) - (size_t) at, " [%d unreadable]", i);
            continue;
        }
        unsigned long twin = *(const unsigned long *) (q + PLAYER_TWIN);
        const unsigned long *h = (const unsigned long *) (q + PLAYER_HISTORY);
        at += snprintf(line + at, sizeof(line) - (size_t) at,
                       " [%d type %d 3c %d ghost %d parent %d twin %s history %ld x18 %d]", i,
                       *(const int *) (q + PLAYER_TYPE), *(const int *) (q + PLAYER_VARIANT),
                       *(const unsigned char *) (q + PLAYER_GHOST), *(const unsigned long *) (q + PLAYER_PARENT) != 0,
                       !twin ? "-" : twin == p[0] ? "p0" : twin == q ? "self" : "other",
                       h[1] >= h[0] ? (long) ((h[1] - h[0]) / HISTORY_RECORD) : -1L,
                       *(const int *) (q + HISTORY_X18));
    }
    /* player 0's list, walked as 0x8a25b8 walks it */
    int records = 0, config = 0, passed = 0, small = 0, big = 0, negative = 0, probe = 0;
    int scroll = shim_eid_readable((const void *) (game + GAME_PAUSE + PAUSE_SCROLL), 4)
                 ? *(const int *) (game + GAME_PAUSE + PAUSE_SCROLL) : 0;
    char ids[160] = "";
    int ids_at = 0;
    if (n > 0 && p[0] && shim_eid_readable((const void *) p[0], PLAYER_GHOST + 1)) {
        unsigned long begin = ((const unsigned long *) (p[0] + PLAYER_HISTORY))[0];
        unsigned long end = ((const unsigned long *) (p[0] + PLAYER_HISTORY))[1];
        records = end >= begin ? (int) ((end - begin) / HISTORY_RECORD) : 0;
        unsigned long first = end - (unsigned long) ((long) scroll * HISTORY_RECORD);
        if (records > 0 && scroll >= 0 && scroll < records && first != begin) {
            for (unsigned long e = first - HISTORY_RECORD; e >= begin && e < end; e -= HISTORY_RECORD) {
                if (!shim_eid_readable((const void *) e, HISTORY_RECORD)) break;
                int id = *(const unsigned char *) (e + 4) ? 0 : *(const int *) (e + 8);
                if (id < 0) negative++;
                unsigned long cfg = item_config(b, id);
                if (cfg) {
                    config++;
                    int has = id >= BIG_ID ? 1 : owns(p[0], cfg, id);
                    if (has) {
                        passed++;
                        if (id >= BIG_ID) big++; else small++;
                        if (ids_at < (int) sizeof(ids) - 8)
                            ids_at += snprintf(ids + ids_at, sizeof(ids) - (size_t) ids_at, " %d", id);
                    } else {
                        for (unsigned k = 0; k < sizeof(CAP_PROBE) / sizeof(CAP_PROBE[0]); k++)
                            if (CAP_PROBE[k] == id) probe++;
                    }
                }
                if (e == begin || passed >= LIST_MAX) break;
            }
        }
    }
    int nohud = no_hud(game);
    snprintf(line + at, sizeof(line) - (size_t) at,
             " | p0 list: history %d, scroll %d, known %d, passed %d (small %d, big %d), negative %d,"
             " probe items not owned %d, NO_HUD %d | drawn: layer3 %d, layer6 %d | ids%s",
             records, scroll, config, passed, small, big, negative, probe, nohud, g_layer3, g_layer6,
             ids[0] ? ids : " -");
    report_line("tjpause", line);
    char check[200];
    int ok = nohud == 1 ? g_layer6 == 0 : g_layer6 == small;
    snprintf(check, sizeof(check), "SELF-CHECK pause list: expected %d (history %d, passed %d), drawn layer6 %d + big %d%s %s",
             nohud == 1 ? 0 : passed, records, passed, g_layer6, big, nohud == 1 ? " (NO_HUD)" : "", ok ? "OK" : "FAIL");
    report_line("coop", check);
}

/* Tainted Jacob's runs: the first change of the fields that hide a history */
static void tjacob_watch(unsigned long game) {
    static unsigned long was_p0;
    static int was_ghost, was_x18, was_variant, was_n, lines, changed;
    unsigned long p[PLAYERS_MAX];
    int n = shim_eid_players(p, PLAYERS_MAX);
    if (n < 1 || !p[0] || !shim_eid_readable((const void *) p[0], PLAYER_GHOST + 1)) { was_p0 = 0; return; }
    int type = *(const int *) (p[0] + PLAYER_TYPE);
    if (type != TJACOB && type != TJACOB_GHOST) { was_p0 = 0; return; }
    int ghost = *(const unsigned char *) (p[0] + PLAYER_GHOST), x18 = *(const int *) (p[0] + HISTORY_X18);
    int variant = *(const int *) (p[0] + PLAYER_VARIANT);
    if (p[0] != was_p0) {                       /* a new run (or a new player 0): watch afresh */
        was_p0 = p[0];
        was_ghost = ghost; was_x18 = x18; was_variant = variant; was_n = n;
        changed = 0;
        return;
    }
    char line[200];
    int at = 0;
    if (ghost != was_ghost && !(changed & 1)) { changed |= 1; at += snprintf(line + at, sizeof(line) - (size_t) at, " ghost %d->%d", was_ghost, ghost); }
    if (x18 != was_x18 && !(changed & 2)) { changed |= 2; at += snprintf(line + at, sizeof(line) - (size_t) at, " x18 %d->%d", was_x18, x18); }
    if (variant != was_variant && !(changed & 4)) { changed |= 4; at += snprintf(line + at, sizeof(line) - (size_t) at, " 3c %d->%d", was_variant, variant); }
    if (n != was_n && !(changed & 8)) { changed |= 8; at += snprintf(line + at, sizeof(line) - (size_t) at, " players %d->%d", was_n, n); }
    was_ghost = ghost; was_x18 = x18; was_variant = variant; was_n = n;
    if (!at || lines >= 40) return;
    lines++;
    char out[260];
    snprintf(out, sizeof(out), "tjacob: frame %ld type %d, first change:%s", g_frame, type, line);
    report_line("tjpause", out);
    (void) game;
}

static void pause_list_watch(unsigned long b, unsigned long game) {
    static int was = -1, wait;
    g_layer3 = g_layer3_now;
    g_layer6 = g_layer6_now;
    g_layer3_now = g_layer6_now = 0;
    g_sprite_pause = game ? game + GAME_PAUSE + PAUSE_SPRITE : 0;
    g_sprite_items = game ? game + GAME_PAUSE + PAUSE_ITEMS : 0;
    int now = pause_state(game);
    if (now == 1 && was != 1) wait = 5;         /* opened: the list is drawn from the next frames on */
    was = now;
    if (wait > 0 && now == 1 && --wait == 0) tjpause(b, game);
    if (now != 1) wait = 0;
}
#else
void shim_devices_layer_drawn(unsigned long sprite, unsigned long layer) { (void) sprite; (void) layer; }
#endif

/* the table and the ledger now, without the report's watchers (coop.c, inside the Continue) */
void shim_devices_refresh(void) {
    unsigned long b = shim_image_base();
    if (!b || !*(const unsigned long *) (b + 0xac2428)) return;
    table_build(b);
    ledger_note();
}

void shim_devices_between_frames(void) {
    unsigned long b = shim_image_base();
    if (!b) return;
    /*
     * Not before the game's manager is made: the input system's lock is set
     * up with it, and locking it earlier is the game's own "Trying to lock
     * mutex that has not been initialized" (0x8d0748). (The report's device
     * probe, coop.c actions(), has asked the same three functions between
     * frames since 27.09 without trouble; this is the belt to its braces.)
     */
    if (!*(const unsigned long *) (b + 0xac2428)) return;
    table_build(b);
    ledger_note();
#if ISAAC_DIAG
    g_frame++;
    if (!shim_report_active()) { g_pause_pressed = 0; g_sprite_pause = 0; return; }
    unsigned long game = *(const unsigned long *) (b + GAME_PTR);
    table_report(b);
    pause_watch(game);
    pause_list_watch(b, game);
    if (game) tjacob_watch(game);
#else
    g_pause_pressed = 0;
#endif
}
