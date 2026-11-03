/*
 * The game's own local co-op, watched - report builds only (27.09.2026).
 *
 * The owner's testers started a second player from the port's APK: "held the
 * touch pause, then Select or Start on the pad". The 07.09.2026 reading had
 * called co-op absent on iOS, and it was wrong. What the image holds:
 *
 *   0x567434  the JOIN HANDLER, every frame from the Game's update (0x88d7ac
 *             tail-jumps to it with x0 = Game). For every input device
 *             (0x8b5c24 counts them, 0x8b5c68 names the i-th) that drives no
 *             player yet: action 26 held (0x8b6600) opens a join slot -
 *             Game+0x5c, four of 0x14 bytes: device +0x5c, character +0x60,
 *             index into the character list +0x64 (-1 = no joiner), +0x68,
 *             +0x6c - and the HUD's "coop menu.anm2" shows the choice; 27/28
 *             step the character (sounds 0xc2/0xc3), 14 confirms, 15 backs
 *             out. With bit 4 of Game+0x1cf400 the choice is skipped and the
 *             player is added at once with character -1 (0x567b44). Held 26
 *             with 8 and 10 triggered adds one straight away as well.
 *   0x566318  add a player of a character (list, character) - the join, and
 *             Esau (0x14) and the Soul (0x10) too; 0x5067e4(player, device,
 *             flag) seats it on a device (player+0x19ec).
 *   0x6733d8  the DEVICE DISPATCHER, every frame from the Manager (read again
 *             30.09.2026, batch 2): 0x6740e0 counts the players "of their
 *             own" by the game's rule (own_player below) and sits an own
 *             player whose device is gone on -1 (0x6741f0); then, with the
 *             object [0xac2490] live, it chooses through 0x674300/0x67422c and
 *             writes its +0x34 (0x769d3c); without it, an orphan gets the first
 *             pressed free device (0x673494 -> 0x674300 -> 0x5067e4, the
 *             RESELECT_CONTROLLER window), and a single own player is moved to
 *             whatever device is HELD (0x6735c4: on the screen -> 0x6744e0, the
 *             other devices' +0xd8; else -> 0x674448, the screen's). With two
 *             own players or more that last step does nothing.
 *
 * WHAT THE PORT DOES ABOUT IT (test builds first, the owner, 27.09.2026):
 *
 *   - The touch controls stay for a player on the touch screen. The game
 *     itself shows the overlay while the touch device drives a player
 *     (0x8608c0 walks the players and writes the live byte); the port's own
 *     extras - the static sticks' forced opacity and the D-pads - used to
 *     step aside whenever a pad was merely connected. They now ask
 *     shim_coop_touch_player(): is anyone playing on the touch screen.
 *   - ONE WRITER A TARGET (batch 2, U1, 01.10.2026). player+0x19ec is the
 *     game's to write: its dispatcher (one player, the orphans), and the
 *     port only where the game does not - touch_wants_player0 (two players
 *     or more), join_device (the gesture), for now key_wants_player0. The
 *     port's polling writer, reseat_stolen_player, is gone: it counted Esau
 *     and the Soul as players of their own (it asked only +0x390), put J&E
 *     back "where they sat alone" every frame, and with a pad met earlier in
 *     the session it threw them pad <-> screen through the frames with not
 *     one press - the twins' flicker and their dead pause. What it guarded
 *     against (player 0 stolen by a joining pad) cannot happen now: the
 *     game's own trigger is off (loader.c coop_join_ours), the dispatcher is
 *     told no about a device that is joining (busy_joining, all three of its
 *     questions), and our gesture hides Select and Start from the game.
 *
 * The probes hook nothing (the trace table is full); they read between frames
 * and write a line when something changes:
 *   coop: N player(s), pause P: [i device D type T variant V character C]
 *   coop: device D actions: a b c        - what the game reads from it now
 *   coop: join slots: [k device D character C index I f F b B] flags 0xNN
 *   seat: p0 A->B by WHO; held: ...      - every move of player 0, and who made it
 *   coop: SELF-CHECK own players / seat / join ...
 * The device lines name the pad button behind action 26 without reading the
 * device's binding tables: press each button once and read the report.
 */
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <pthread.h>
#include "diag.h"
#include "devices.h"

unsigned long shim_image_base(void);
int shim_report_active(void);
void report_line(const char *kind, const char *text);
int shim_eid_players(unsigned long *out, int max);
int shim_eid_readable(const void *p, unsigned long n);

#define GAME_PTR        0xac3b90
#define INPUT_SYSTEM    0xac5528        /* the object itself */
#define FN_DEVICE_COUNT 0x8b5c24        /* (input) -> n */
#define FN_DEVICE_AT    0x8b5c68        /* (input, i) -> device id, -1 none */
#define FN_ACTION_HELD  0x8b6600        /* (input, action, device, 0) -> pressed */
#define GAME_PAUSE      0x10dd20
#define PAUSE_STATE     0x2b8           /* 1 while the pause menu is up (straps.c) */
#define GAME_FLAGS      0x1cf400        /* byte; bit 4 skips the character choice */
/*
 * The four join slots: +0x5c of the player manager at Game+0x25c40 (the join
 * handler's x19; the HUD reads them the same way, 0x6eecbc: Game + 0x25c40 +
 * k*0x14 + 0x5c). The first reading put them at Game+0x5c and saw garbage.
 * A slot: device, character, index into the character list (-1: not
 * choosing), a float, a byte. The HUD draws slot k's choice where player k's
 * hearts stand (the position array it walks for the players, 0x6eed14).
 */
#define PLAYER_MANAGER  0x25c40
#define JOIN_SLOTS      (PLAYER_MANAGER + 0x5c)
#define JOIN_SLOT_SIZE  0x14
#define JOIN_SLOT_N     4
#define PLAYER_DEVICE   0x19ec
#define PLAYER_ORDER    0x19f0          /* the game's own order of the players (0x674160) */
#define PLAYER_PARENT   0x390           /* not on the twins: a twin is +0x2580 */
#define PLAYER_TWIN     0x2580          /* Esau for Jacob and back, the Soul, T.Lazarus's other form */
#define PLAYER_GHOST    0x284c          /* byte: a co-op ghost */
#define PLAYER_TYPE     0x1738
#define TOUCH_DEVICE    0x10dead        /* the game's own number for the touch screen (0x67413c) */
#define FN_SEAT         0x5067e4        /* (player, device, also the twins) */
#define FN_LIVE_PLAYERS 0x56b3ac        /* (player manager) -> the live players of their own; a leaf, reads only */
#define ENT_TYPE        0x38
#define ENT_VARIANT     0x3c
#define PLAYERS_MAX     8
#define DEVICES_MAX     8
#define ACTIONS         32
#define LINES_MAX       240             /* the probes' budget */
#define OWN_LINES_MAX   80              /* and our own doings' - apart, so the probes cannot drown them
                                           (the 18th round: 221 "actions" lines ate the join's answer) */

static int g_said, g_own_said;

/* a probe's line, out of the probes' budget */
static void say(const char *line) {
    if (g_said >= LINES_MAX) return;
    g_said++;
    report_line("coop", line);
}

/* what the port itself did or refused: its own budget */
static void tell(const char *line) {
    if (g_own_said >= OWN_LINES_MAX) return;
    g_own_said++;
    report_line("coop", line);
}

static void players(unsigned long b, unsigned long game) {
    static unsigned long was[PLAYERS_MAX];
    static unsigned was_dev[PLAYERS_MAX];
    static int was_n = -1, was_pause = -1;
    unsigned long p[PLAYERS_MAX];
    int n = game ? shim_eid_players(p, PLAYERS_MAX) : 0;
    int pause = -1;
    if (game && shim_eid_readable((const void *) (game + GAME_PAUSE + PAUSE_STATE), 4))
        pause = *(const int *) (game + GAME_PAUSE + PAUSE_STATE);
    unsigned dev[PLAYERS_MAX];
    for (int i = 0; i < n; i++)
        dev[i] = (p[i] && shim_eid_readable((const void *) (p[i] + PLAYER_DEVICE), 4))
                     ? *(const unsigned *) (p[i] + PLAYER_DEVICE) : 0xffffffffu;
    int changed = n != was_n || (n > 1 && pause != was_pause);
    for (int i = 0; i < n && !changed; i++) if (p[i] != was[i] || dev[i] != was_dev[i]) changed = 1;
    if (!changed) return;
    was_n = n;
    was_pause = pause;
    memcpy(was, p, sizeof(unsigned long) * (size_t) n);
    memcpy(was_dev, dev, sizeof(unsigned) * (size_t) n);
    if (n == 0) return;                      /* the menus: nobody to tell about */
    char line[400];
    int at = snprintf(line, sizeof(line), "%d player(s), pause %d:", n, pause);
    for (int i = 0; i < n && at < (int) sizeof(line) - 80; i++) {
        unsigned long q = p[i];
        if (dev[i] == 0xffffffffu) {
            at += snprintf(line + at, sizeof(line) - (size_t) at, " [%d unreadable]", i);
            continue;
        }
        at += snprintf(line + at, sizeof(line) - (size_t) at, " [%d device %#x type %d variant %d character %d]",
                       i, dev[i], *(const int *) (q + ENT_TYPE), *(const int *) (q + ENT_VARIANT),
                       *(const int *) (q + PLAYER_TYPE));
    }
    say(line);
    (void) b;
}

/* what the game reads from every device: the actions held, as a set */
static void actions(unsigned long b) {
    static unsigned was[DEVICES_MAX];
    static int was_dev[DEVICES_MAX];
    unsigned long input = b + INPUT_SYSTEM;
    int n = ((int (*)(unsigned long)) (b + FN_DEVICE_COUNT))(input);
    if (n > DEVICES_MAX) n = DEVICES_MAX;
    for (int i = 0; i < n; i++) {
        int dev = ((int (*)(unsigned long, int)) (b + FN_DEVICE_AT))(input, i);
        if (dev == -1) continue;
        unsigned held = 0;
        for (int a = 0; a < ACTIONS; a++)
            if (((int (*)(unsigned long, int, int, unsigned long)) (b + FN_ACTION_HELD))(input, a, dev, 0) & 1)
                held |= 1u << a;
        if (held == was[i] && dev == was_dev[i]) continue;
        was[i] = held;
        was_dev[i] = dev;
        char line[200];
        int at = snprintf(line, sizeof(line), "device %#x actions:", (unsigned) dev);
        if (!held) at += snprintf(line + at, sizeof(line) - (size_t) at, " none");
        for (int a = 0; a < ACTIONS && at < (int) sizeof(line) - 8; a++)
            if (held & (1u << a)) at += snprintf(line + at, sizeof(line) - (size_t) at, " %d", a);
        say(line);
    }
}

static void slots(unsigned long game) {
    static unsigned char was[JOIN_SLOT_N * JOIN_SLOT_SIZE + 1];
    static int primed;
    if (!shim_eid_readable((const void *) (game + JOIN_SLOTS), JOIN_SLOT_N * JOIN_SLOT_SIZE)
        || !shim_eid_readable((const void *) (game + GAME_FLAGS), 1)) return;
    unsigned char now[JOIN_SLOT_N * JOIN_SLOT_SIZE + 1];
    memcpy(now, (const void *) (game + JOIN_SLOTS), JOIN_SLOT_N * JOIN_SLOT_SIZE);
    now[JOIN_SLOT_N * JOIN_SLOT_SIZE] = *(const unsigned char *) (game + GAME_FLAGS);
    if (primed && !memcmp(now, was, sizeof(now))) return;
    primed = 1;
    memcpy(was, now, sizeof(now));
    char line[400];
    int at = snprintf(line, sizeof(line), "join slots:");
    for (int k = 0; k < JOIN_SLOT_N && at < (int) sizeof(line) - 90; k++) {
        const unsigned char *s = now + k * JOIN_SLOT_SIZE;
        int dev, chr, idx; float f; unsigned char flag;
        memcpy(&dev, s + 0, 4); memcpy(&chr, s + 4, 4); memcpy(&idx, s + 8, 4);
        memcpy(&f, s + 12, 4); flag = s[16];
        if (dev == -1 && idx == -1) continue;
        if (dev != -1 && dev != (int) TOUCH_DEVICE && (unsigned) dev > 0xff) continue;   /* never written yet */
        at += snprintf(line + at, sizeof(line) - (size_t) at, " [%d device %#x character %d index %d f %.2f b %d]",
                       k, (unsigned) dev, chr, idx, (double) f, flag);
    }
    snprintf(line + at, sizeof(line) - (size_t) at, " flags %#x", now[JOIN_SLOT_N * JOIN_SLOT_SIZE]);
    say(line);
}

#define GAME_JOIN_LOCK  0x1cf4c0        /* int: nought while joining is allowed */
#define FN_ADD_AT_ONCE  0x567b44        /* (game, character, device) */
#define FN_PLAY_SOUND   0x671c8c        /* (manager, id, w2, w3, volume, pitch) */
#define MANAGER_PTR     0xac2428
#define SOUND_NOT_NOW   0xbb
int shim_sticks_in_game(void);

/* the game's device made for one of our GCControllers (pad.c): through the book of devices */
static int device_of_controller(void *controller) {
    int shim_pad_slot_of(const void *controller);
    return shim_device_of_pad_slot(shim_pad_slot_of(controller));
}

/* how many players the run has now (0 in the menus, and for a moment on the way into a boss room) */
int shim_coop_players(void) {
    unsigned long p[PLAYERS_MAX];
    return shim_eid_players(p, PLAYERS_MAX);
}

/* is anyone playing on the touch screen (a player whose device is the screen's, by its class) */
int shim_coop_touch_player(void) {
    unsigned long b = shim_image_base();
    if (!b) return 0;
    unsigned long game = *(const unsigned long *) (b + GAME_PTR);
    if (!game) return 0;
    unsigned long p[PLAYERS_MAX];
    int n = shim_eid_players(p, PLAYERS_MAX);
    for (int i = 0; i < n; i++)
        if (p[i] && shim_eid_readable((const void *) (p[i] + PLAYER_DEVICE), 4)
            && shim_device_kind(*(const int *) (p[i] + PLAYER_DEVICE)) == DEVICE_SCREEN) return 1;
    return 0;
}

/*
 * A PLAYER OF HIS OWN, by the game's rule word for word (0x674160..0x67418c,
 * the dispatcher's 0x6740e0): his order +0x19f0 is not negative, he has no
 * parent (+0x390), and his twin (+0x2580) - if he has one - does not come
 * before him in the game's order. So Jacob and Esau are ONE player of their
 * own, and so are T.Forgotten and his Soul, T.Lazarus and his other form
 * with Birthright: the one first in the order. The port's old rule asked only
 * +0x390, which the twins do not carry - for it J&E were two players on one
 * device, and it "put them back" every frame (U1).
 */
static int own_player(unsigned long p) {
    if (!p || !shim_eid_readable((const void *) (p + PLAYER_ORDER), 4)
        || !shim_eid_readable((const void *) (p + PLAYER_PARENT), 8)
        || !shim_eid_readable((const void *) (p + PLAYER_TWIN), 8)) return 0;
    int order = *(const int *) (p + PLAYER_ORDER);
    if (order < 0) return 0;
    if (*(const unsigned long *) (p + PLAYER_PARENT) != 0) return 0;
    unsigned long twin = *(const unsigned long *) (p + PLAYER_TWIN);
    if (twin && twin != p) {
        if (!shim_eid_readable((const void *) (twin + PLAYER_ORDER), 4)) return 0;
        if (*(const int *) (twin + PLAYER_ORDER) < order) return 0;
    }
    return 1;
}

int shim_coop_own_player(unsigned long p) { return own_player(p); }

/* hudlayout.c: how many players of their own the run has (Jacob and Esau are one) */
int shim_coop_own_count(void) {
    unsigned long p[PLAYERS_MAX];
    int n = shim_eid_players(p, PLAYERS_MAX), m = 0;
    for (int i = 0; i < n; i++) m += own_player(p[i]);
    return m;
}

/* the players' devices, and which are players of their own; nought when a player cannot be read */
static int seats(unsigned long *p, unsigned *dev, int *own, int max) {
    int n = shim_eid_players(p, max);
    for (int i = 0; i < n; i++) {
        if (!p[i] || !shim_eid_readable((const void *) (p[i] + PLAYER_DEVICE), 4)
            || !shim_eid_readable((const void *) (p[i] + PLAYER_PARENT), 8)) return 0;
        dev[i] = *(const unsigned *) (p[i] + PLAYER_DEVICE);
        own[i] = own_player(p[i]);
    }
    return n;
}

#define NO_DEVICE          0xffffffffu
#define DISPATCHER_OBJECT  0xac2490     /* the object the dispatcher's menu road writes (+0x34 via 0x769d3c) */
#define CHOSEN_DEVICE      0x34
#define CHOSEN_ON          0x38

/* ---------- the report's watchers of player 0's seat (report builds only) ---------- */

#if ISAAC_DIAG
/* who of ours last wrote player 0's device, and where to: the journal names the writer */
static unsigned g_ours_dev = NO_DEVICE;
static const char *g_ours_who;
static void ours_wrote(unsigned dev, const char *who) { g_ours_dev = dev; g_ours_who = who; }

/* the seat lines and the self-checks: budgets of their own, apart from the join's */
static void seat_say(const char *line) {
    static int said;
    if (said >= 150 || !shim_report_active()) return;
    said++;
    report_line("seat", line);
}
static void check_say(const char *line) {
    static int said;
    if (said >= 60 || !shim_report_active()) return;
    said++;
    report_line("coop", line);
}

void shim_devices_held_line(char *out, int room);
int shim_devices_touch_ghost(char *parts, int room);
int shim_devices_real_input(int id);

/*
 * THE ZERO STEP (batch 2): the whole reading of U1 stands on the dispatcher
 * taking its "no object" road in a run. So once a run - and whenever it
 * changes within one - the report says whether [0xac2490] is live, with its
 * +0x38 and +0x34. Live in a run means the reading is wrong and is done again.
 */
static void zero_step(unsigned long b) {
    static int in_run, lines, was = -1;
    if (shim_coop_players() == 0) { in_run = 0; was = -1; return; }
    unsigned long obj = *(const unsigned long *) (b + DISPATCHER_OBJECT);
    int live = obj && shim_eid_readable((const void *) (obj + CHOSEN_DEVICE), 8);
    int state = !obj ? 0 : live ? 1 + (*(const int *) (obj + CHOSEN_ON) != 0) : 3;
    if (in_run && state == was) return;
    in_run = 1;
    was = state;
    if (lines >= 30) return;
    lines++;
    char line[160];
    if (!obj) snprintf(line, sizeof(line), "dispatcher object [0xac2490] in the run: empty");
    else if (!live) snprintf(line, sizeof(line), "dispatcher object [0xac2490] in the run: %lx, unreadable", obj);
    else snprintf(line, sizeof(line), "dispatcher object [0xac2490] in the run: LIVE at %lx, +0x38 %d, +0x34 %#x",
                  obj, *(const int *) (obj + CHOSEN_ON), *(const unsigned *) (obj + CHOSEN_DEVICE));
    report_line("coop", line);
}

/*
 * SELF-CHECK own players: whenever the line-up changes, the count of live
 * players of their own by the game's own conditions of 0x56b3ac (no parent,
 * the twin rule, +0x3c nought, not a ghost - it has no +0x19f0 condition, so
 * own_player is shown apart), against 0x56b3ac itself (a leaf that reads
 * only). Every player's fields go into the line: on J&E, T.Forgotten and
 * T.Lazarus it confirms the rule by itself.
 */
static void own_check(unsigned long b, unsigned long game) {
    static unsigned long was[PLAYERS_MAX * 6];
    static int was_n = -1;
    unsigned long p[PLAYERS_MAX];
    int n = shim_eid_players(p, PLAYERS_MAX);
    unsigned long now[PLAYERS_MAX * 6];
    memset(now, 0, sizeof(now));
    for (int i = 0; i < n; i++) {
        if (!p[i] || !shim_eid_readable((const void *) p[i], PLAYER_GHOST + 1)) return;   /* the farthest field read */
        now[i * 6 + 0] = p[i];
        now[i * 6 + 1] = (unsigned) *(const int *) (p[i] + PLAYER_TYPE);
        now[i * 6 + 2] = (unsigned) *(const int *) (p[i] + PLAYER_ORDER);
        now[i * 6 + 3] = *(const unsigned long *) (p[i] + PLAYER_PARENT);
        now[i * 6 + 4] = *(const unsigned long *) (p[i] + PLAYER_TWIN);
        now[i * 6 + 5] = (unsigned) *(const int *) (p[i] + ENT_VARIANT) * 2u + *(const unsigned char *) (p[i] + PLAYER_GHOST);
    }
    if (n == was_n && !memcmp(now, was, sizeof(now))) return;
    was_n = n;
    memcpy(was, now, sizeof(now));
    if (n == 0) return;
    int ours = 0;
    for (int i = 0; i < n; i++) {
        unsigned long q = p[i], t = *(const unsigned long *) (q + PLAYER_TWIN);
        if (*(const int *) (q + ENT_VARIANT) != 0 || *(const unsigned long *) (q + PLAYER_PARENT) != 0) continue;
        if (t && t != q && shim_eid_readable((const void *) (t + PLAYER_ORDER), 4)
            && *(const int *) (t + PLAYER_ORDER) < *(const int *) (q + PLAYER_ORDER)) continue;
        if (!*(const unsigned char *) (q + PLAYER_GHOST)) ours++;
    }
    int theirs = ((int (*)(unsigned long)) (b + FN_LIVE_PLAYERS))(game + PLAYER_MANAGER);
    char line[600];
    int at = snprintf(line, sizeof(line), "SELF-CHECK own players: ours %d, the game's %d %s;", ours, theirs,
                      ours == theirs ? "OK" : "FAIL");
    for (int i = 0; i < n && at < (int) sizeof(line) - 110; i++) {
        unsigned long q = p[i], t = *(const unsigned long *) (q + PLAYER_TWIN);
        char twin[8] = "-";
        if (t == q) snprintf(twin, sizeof(twin), "self");
        else if (t) {
            snprintf(twin, sizeof(twin), "other");
            for (int k = 0; k < n; k++) if (p[k] == t) snprintf(twin, sizeof(twin), "p%d", k);
        }
        at += snprintf(line + at, sizeof(line) - (size_t) at,
                       " [%d type %d dev %#x order %d parent %d twin %s 3c %d ghost %d own %d]", i,
                       *(const int *) (q + PLAYER_TYPE), *(const unsigned *) (q + PLAYER_DEVICE),
                       *(const int *) (q + PLAYER_ORDER), *(const unsigned long *) (q + PLAYER_PARENT) != 0, twin,
                       *(const int *) (q + ENT_VARIANT), *(const unsigned char *) (q + PLAYER_GHOST), own_player(q));
    }
    check_say(line);
}

/*
 * THE SEAT JOURNAL: every move of player 0 between two frames, and who made
 * it - one of ours (they say so as they write), else the game: "orphan/
 * reselect" when it came from -1, "single" when one player of his own is
 * there, the menu's road when [0xac2490] is live. With the game's move goes
 * what every device answers to "is anything held" (+0xd8), the dispatcher's
 * own question. Three self-checks ride on it:
 *   - two players of their own on one device (the old theft, or a writer we
 *     do not know);
 *   - the touch screen held by the game's reckoning with no finger on the
 *     glass, two frames running (uikit.c keeps a release back one frame);
 *   - more than ten moves by the game in a second: FAIL only when one side
 *     has no real input - both held is the game's own behaviour, as on iOS.
 */
static void seat_journal(unsigned long b) {
    static unsigned long was_p0;
    static unsigned was_dev = NO_DEVICE;
    static long frame, moves[11];
    static int moves_at, twice_said, pingpong_said, ghost_wait;
    frame++;
    unsigned long p[PLAYERS_MAX];
    unsigned dev[PLAYERS_MAX];
    int own[PLAYERS_MAX];
    int n = seats(p, dev, own, PLAYERS_MAX);
    if (n < 1) {
        was_p0 = 0; was_dev = NO_DEVICE;
        twice_said = pingpong_said = ghost_wait = 0;
        memset(moves, 0, sizeof(moves));
        g_ours_who = NULL;
        return;
    }
    int owners = 0;
    for (int i = 0; i < n; i++) owners += own[i];
    char line[500];
    for (int i = 0; i < n && !twice_said; i++)
        for (int j = i + 1; j < n && !twice_said; j++)
            if (own[i] && own[j] && dev[i] == dev[j] && dev[i] != NO_DEVICE) {
                snprintf(line, sizeof(line), "SELF-CHECK seat: FAIL two own players on device %#x (players %d and %d)",
                         dev[i], i, j);
                check_say(line);
                twice_said = 1;
            }
    if (ghost_wait > 0) {
        char parts[48];
        if (dev[0] == TOUCH_DEVICE && shim_devices_touch_ghost(parts, sizeof(parts))) {
            if (--ghost_wait == 0) {
                snprintf(line, sizeof(line), "SELF-CHECK seat: FAIL touch held without a finger (%s)", parts);
                check_say(line);
            }
        } else {
            ghost_wait = 0;
        }
    }
    if (p[0] != was_p0) { was_p0 = p[0]; was_dev = dev[0]; g_ours_who = NULL; return; }
    if (dev[0] == was_dev) return;
    unsigned from = was_dev;
    was_dev = dev[0];
    char held[360];
    if (g_ours_who && g_ours_dev == dev[0]) {
        snprintf(line, sizeof(line), "p0 %#x->%#x by %s", from, dev[0], g_ours_who);
        g_ours_who = NULL;
        seat_say(line);
        return;
    }
    g_ours_who = NULL;
    shim_devices_held_line(held, sizeof(held));
    unsigned long obj = *(const unsigned long *) (b + DISPATCHER_OBJECT);
    char why[48];
    if (from == NO_DEVICE) snprintf(why, sizeof(why), "orphan/reselect");
    else if (dev[0] == NO_DEVICE) snprintf(why, sizeof(why), "orphan: its device is gone");
    else if (obj) snprintf(why, sizeof(why), "the menu's road, [0xac2490] live");
    else if (owners == 1) snprintf(why, sizeof(why), "single");
    else snprintf(why, sizeof(why), "%d own players", owners);
    snprintf(line, sizeof(line), "p0 %#x->%#x by the game (%s); %s", from, dev[0], why, held);
    seat_say(line);
    if (dev[0] == TOUCH_DEVICE) {
        char parts[48];
        ghost_wait = shim_devices_touch_ghost(parts, sizeof(parts)) ? 1 : 0;
    }
    moves[moves_at] = frame;
    moves_at = (moves_at + 1) % 11;
    long oldest = moves[moves_at];              /* the eleventh move back */
    if (!pingpong_said && oldest && frame - oldest <= 60 && from != NO_DEVICE && dev[0] != NO_DEVICE) {
        pingpong_said = 1;
        int ra = shim_devices_real_input((int) from), rb = shim_devices_real_input((int) dev[0]);
        if (!ra || !rb)
            snprintf(line, sizeof(line), "SELF-CHECK seat: FAIL ping-pong %#x<->%#x, 11 moves in %ld frames, real input %d/%d",
                     from, dev[0], frame - oldest, ra, rb);
        else
            snprintf(line, sizeof(line), "SELF-CHECK seat: native ping-pong (both held) %#x<->%#x, 11 moves in %ld frames",
                     from, dev[0], frame - oldest);
        check_say(line);
    }
}
#define OURS(dev, who) ours_wrote(dev, who)
#else
#define OURS(dev, who) do { } while (0)
#endif

/*
 * THE TOUCH SCREEN STAYS PLAYER 0'S (the owner, 28.09.2026): with two or more
 * players the game's dispatcher (0x6733d8) no longer hands the screen to
 * anyone, and player 0, once on a pad, could not come back to it. A finger
 * going down on the screen (uikit.c tells us) while nobody plays on it seats
 * player 0 there; a press on a free pad hands player 0 that pad again - the
 * game's own dispatcher does that part, as it always did. Only the edge of a
 * touch counts, once in half a second at most, so a held finger and a held
 * pad never pull the player back and forth within a frame. With one player
 * of his own - J&E are one (own_player) - the game does all of this by
 * itself and we stay out of it.
 */
static volatile int g_touch_edge;
void shim_coop_touch_began(void) { g_touch_edge = 1; }

#define TOUCH_SEAT_COOLDOWN 30

static void touch_wants_player0(unsigned long b) {
    static int cooldown;
    if (cooldown > 0) cooldown--;
    if (!g_touch_edge) return;
    g_touch_edge = 0;
    if (cooldown > 0 || !shim_sticks_in_game()) return;
    unsigned long p[PLAYERS_MAX];
    unsigned dev[PLAYERS_MAX];
    int own[PLAYERS_MAX];
    int n = seats(p, dev, own, PLAYERS_MAX);
    int players = 0;
    for (int i = 0; i < n; i++) {
        if (dev[i] == TOUCH_DEVICE) return;          /* the screen already plays */
        if (own[i]) players++;
    }
    if (players < 2 || !own[0] || dev[0] == NO_DEVICE) return;
    ((void (*)(unsigned long, unsigned, int)) (b + FN_SEAT))(p[0], TOUCH_DEVICE, 1);
    OURS(TOUCH_DEVICE, "touch_wants_player0 (a finger, two own players or more)");
    cooldown = TOUCH_SEAT_COOLDOWN;
    char line[120];
    snprintf(line, sizeof(line), "a finger on the screen: player 0 moved from device %#x to the touch screen", dev[0]);
    tell(line);
}

/*
 * THE KEYBOARD TAKES PLAYER 0 BACK FROM THE SCREEN (a tester, 28.09.2026: "in
 * a run on the keyboard, touch the screen and the character moves only by
 * touch, the keys do nothing"). With one player the game's own dispatcher
 * should do it: on the screen, 0x6744e0 asks the other devices "is anything
 * held" (the keyboard's +0xd8 is 0x8ddbbc: any key down in its set) - and a
 * key pressed and let go within one delivery is never seen held. Until the
 * keyboard's delivery shows every press to a frame (batch 2, K5) the port
 * keeps its own hand here, but gives the game's first: a key going down while
 * player 0 sits on the screen waits KEY_NATIVE_FRAMES frames, and only if the
 * game has not moved him by then is he seated on the keyboard device the
 * game's own way (0x5067e4). The report names who did it every time (U8: the
 * hand is taken away once no report shows "the game did not").
 */
static volatile int g_key_edge;
void shim_coop_key_edge(void) { g_key_edge = 1; }

#define KEY_NATIVE_FRAMES 3

static void key_wants_player0(unsigned long b) {
    static int cooldown, wait;
    if (cooldown > 0) cooldown--;
    int edge = g_key_edge;
    g_key_edge = 0;
    if (!shim_sticks_in_game()) { wait = 0; return; }
    unsigned long p[PLAYERS_MAX];
    unsigned dev[PLAYERS_MAX];
    int own[PLAYERS_MAX];
    int n = seats(p, dev, own, PLAYERS_MAX);
    if (n < 1 || !own[0] || dev[0] != TOUCH_DEVICE) { wait = 0; return; }   /* not on the screen (any more) */
    if (edge && !wait && cooldown == 0) wait = KEY_NATIVE_FRAMES;
    if (!wait || --wait > 0) return;
    int kbd = shim_device_keyboard();
    if (kbd < 0) return;
    for (int i = 1; i < n; i++) if (own[i] && dev[i] == (unsigned) kbd) return;   /* another player's keys */
    ((void (*)(unsigned long, unsigned, int)) (b + FN_SEAT))(p[0], (unsigned) kbd, 1);
    OURS((unsigned) kbd, "key_wants_player0 (the game did not in 3 frames)");
    cooldown = TOUCH_SEAT_COOLDOWN;
    char line[120];
    snprintf(line, sizeof(line), "a key: player 0 moved from the touch screen back to the keyboard device %#x", (unsigned) kbd);
    tell(line);
}

/*
 * THE PORT'S DOOR INTO CO-OP (the owner, 28.09.2026): Select and Start held
 * three seconds on a pad, Esc and Tab on the keyboard (pad.c). This does what
 * the game's own handler did on action 26 before loader.c stopped asking for
 * it (coop_join_ours), for that controller's device only:
 *
 *   - the device made for the GCController the gesture came from
 *     (device_of_controller);
 *   - nobody may be playing on it already, and there must be a run;
 *   - a join slot in the Game (Game+0x5c, four of 0x14: device, character,
 *     index, a float, a byte) is taken as 0x567690 takes it - the slot this
 *     device already holds, else the first free one - and filled as the
 *     handler's third branch fills it (0x56784c): character 0, index 1, 1.0,
 *     0. Bit 4 of Game+0x1cf400 skips the choice and adds the player at once
 *     (0x567b44, character -1), as the game does.
 *   - From there the game's own choice runs (0x567874..): left and right
 *     step the character, A confirms, B backs out.
 *
 * The game only lets a player in at the start of the first floor (Game+0x1cf4c0
 * is nought there); anywhere else the game's own "not now" sound is played
 * and nothing is opened.
 */
/*
 * The joins asked for, by device: shim_coop_join_asked answers yes for the
 * device until the handler has asked once, and the frame after that the
 * entry goes (the handler reads it once a pass). An entry nobody asks for in
 * two seconds is dropped - the handler asks only for a free device in a run.
 */
#define WANTS 4
#define WANT_MS 2000
static struct { int device; long long at; int asked; } g_want[WANTS];
/*
 * THE DEVICES CHOOSING A CHARACTER NOW - a set, one entry a device (batch 2,
 * U1 step 5): put in when the handler takes our "yes", taken out when that
 * device's slot is no longer open (joined, backed out, refused with the
 * game's "not now") or the run has moved past the join (Game+0x1cf4c0 no
 * longer nought). No clock: a choice may take as long as the player likes.
 */
static int g_choosing[WANTS] = { -1, -1, -1, -1 };

static int choosing_has(int device) {
    for (int k = 0; k < WANTS; k++) if (g_choosing[k] == device && device != -1) return 1;
    return 0;
}

static void choosing_add(int device) {
    if (choosing_has(device)) return;
    for (int k = 0; k < WANTS; k++) if (g_choosing[k] == -1) { g_choosing[k] = device; return; }
}

static long long coop_now_ms(void) {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (long long) t.tv_sec * 1000 + t.tv_nsec / 1000000;
}

/* the handler's question, through the trampoline at 0x8dab28 (loader.c coop_join_ours) */
void shim_coop_continue_handout(unsigned long player_manager);

int shim_coop_join_asked(unsigned long input, int action, int device, unsigned long flags) {
    (void) flags;
    /* the trampoline is shared (loader.c coop_handout_ours): the join asks with the input system */
    if (input != shim_image_base() + INPUT_SYSTEM) { shim_coop_continue_handout(input); return 0; }
    if (action != 26) return 0;
    for (int k = 0; k < WANTS; k++)
        if (g_want[k].at && g_want[k].device == device) {
            if (!g_want[k].asked) {
                g_want[k].asked = 1;
                choosing_add(device);
            }
            return 1;
        }
    return 0;
}

static void wants_between_frames(void) {
    long long now = coop_now_ms();
    for (int k = 0; k < WANTS; k++) {
        if (!g_want[k].at) continue;
        if (g_want[k].asked) {
            char line[100];
            snprintf(line, sizeof(line), "the game took the join for device %#x", (unsigned) g_want[k].device);
            tell(line);
            g_want[k].at = 0;
        } else if (now - g_want[k].at > WANT_MS) {
            char line[120];
            snprintf(line, sizeof(line), "the game never asked for device %#x's join (not a free device in a run?)",
                     (unsigned) g_want[k].device);
            tell(line);
            g_want[k].at = 0;
        }
    }
}

/* the slot that holds this device while it chooses, or -1 */
static int choosing_slot(unsigned long game, int device) {
    if (!shim_eid_readable((const void *) (game + JOIN_SLOTS), JOIN_SLOT_N * JOIN_SLOT_SIZE)) return -1;
    for (int k = 0; k < JOIN_SLOT_N; k++) {
        const unsigned char *sl = (const unsigned char *) (game + JOIN_SLOTS + k * JOIN_SLOT_SIZE);
        int dev, idx; memcpy(&dev, sl, 4); memcpy(&idx, sl + 8, 4);
        if (dev == device && idx >= 0) return k;
    }
    return -1;
}

/*
 * The choosers, watched (it used to seat player 0 back on the screen when the
 * dispatcher handed him the choosing device; the dispatcher is now told no
 * about that device in all three of its questions, so there is nothing to put
 * back - only a report line if it happens all the same).
 */
static void watch_the_choosers(unsigned long b, unsigned long game) {
    int any = 0;
    for (int k = 0; k < WANTS; k++) if (g_choosing[k] != -1) any = 1;
    if (!any) return;
    if (!game || (shim_eid_readable((const void *) (game + GAME_JOIN_LOCK), 4) && *(const int *) (game + GAME_JOIN_LOCK))) {
        for (int k = 0; k < WANTS; k++) g_choosing[k] = -1;
        tell("the run moved past the join: no device is choosing");
        return;
    }
    unsigned long p[PLAYERS_MAX];
    unsigned dev[PLAYERS_MAX];
    int own[PLAYERS_MAX];
    int n = seats(p, dev, own, PLAYERS_MAX);
    for (int k = 0; k < WANTS; k++) {
        int d = g_choosing[k];
        if (d == -1) continue;
        char line[120];
        int joined = -1;
        for (int i = 1; i < n; i++) if (own[i] && (int) dev[i] == d) joined = i;
        if (joined > 0) {
            snprintf(line, sizeof(line), "device %#x joined as player %d", (unsigned) d, joined + 1);
            tell(line);
            g_choosing[k] = -1;
        } else if (choosing_slot(game, d) < 0) {
            snprintf(line, sizeof(line), "device %#x no longer chooses (its slot is closed)", (unsigned) d);
            tell(line);
            g_choosing[k] = -1;
        }
#if ISAAC_DIAG
        else if (n >= 1 && own[0] && (int) dev[0] == d) {
            static int said_for = -2;
            if (said_for != d) {
                said_for = d;
                snprintf(line, sizeof(line), "SELF-CHECK join: FAIL player 0 on the choosing device %#x", (unsigned) d);
                check_say(line);
            }
        }
#endif
    }
    (void) b;
}

/*
 * THE DISPATCHER'S THREE QUESTIONS (loader.c coop_dispatcher_asks): 0x674380
 * "did anything go down" (0x8b6d38, the device's +0xe0), 0x67438c and
 * 0x674534 "is anything held" (0x8b6d10, +0xd8; loader.c's comments and the
 * names below still call them "pressed" and "pressed anew" - it is the other
 * way round, the behaviour is the same). The answer is no for a device that
 * is joining, by OUR record, not by the slot's fields (a slot left naming the
 * keyboard after a join thrown away kept the keyboard from player 0 for
 * good): a join asked of the handler and not yet taken (g_want), or a device
 * choosing whose slot is really open (g_choosing).
 */
#define FN_DEVICE_WENT_DOWN 0x8b6d38    /* (input, id): the device's +0xe0 */
#define FN_DEVICE_HELD      0x8b6d10    /* (input, id): the device's +0xd8 (0x8a8688; the keyboard's 0x8ddbbc) */

static int busy_joining(int device) {
    for (int k = 0; k < WANTS; k++) if (g_want[k].at && g_want[k].device == device) return 1;
    if (!choosing_has(device)) return 0;
    unsigned long b = shim_image_base();
    unsigned long game = b ? *(const unsigned long *) (b + GAME_PTR) : 0;
    return game && choosing_slot(game, device) >= 0;
}

#if ISAAC_DIAG
static void refused(int device, const char *question) {
    static int said, was = -2;
    if (said >= 20 || device == was || !shim_report_active()) return;
    said++;
    was = device;
    char line[120];
    snprintf(line, sizeof(line), "the dispatcher asked \"%s\" of device %#x while it joins: no", question, (unsigned) device);
    report_line("coop", line);
}
#define REFUSED(d, q) refused(d, q)
#else
#define REFUSED(d, q) do { } while (0)
#endif

/*
 * "DID ANYTHING GO DOWN" IS THE GAME'S OWN ANSWER, for every device (06.10.2026,
 * after 1.71: "the Change Controller button doesn't work at all").
 *
 * The chooser 0x674300 asks 0x674380 (+0xe0, 0x8a87e0: a button set now and
 * not at the last update, or an analog value crossing +0xe4) only when its
 * flag is set - the flag is Manager+0x56588 (0x671158: ldrb w1, [Manager +
 * 0x55a58 + 0xb30]), and the one writer of it in the image is Change
 * Controller (0x673648: 0x8b69f4 spends action 14 on the player's device,
 * the player goes to -1, the byte to 1). Without the flag the chooser asks
 * "is anything held" (0x67438c) - so "Select a Controller!" after a Continue
 * or a pad gone to sleep is answered by any button held, by the game itself
 * (pass 22: "chooser: pad 0x2 ... asked as held (0x67438c)" and the window
 * closed on it). The edge is how Change Controller waits for a NEW press: the
 * button that confirmed the row is still down for some frames, and its edge
 * is spent. The 05.10 answer "went down OR held" for pads (meant for the
 * Continue window, which never asks this) took that held confirm button as the
 * choice and gave the player straight back the pad he had just let go.
 *
 * The screen is still not taken with two players or more (the game's own
 * rule at 0x674364). A device joining is refused, as before.
 */
#define MANAGER_CHOOSER_EDGE 0x56588    /* byte: the chooser waits for a press going down (set by 0x673648) */

#if ISAAC_DIAG
static int chooser_waits_for_a_press(void) {
    unsigned long b = shim_image_base();
    unsigned long mgr = b ? *(const unsigned long *) (b + MANAGER_PTR) : 0;
    if (!mgr || !shim_eid_readable((const void *) (mgr + MANAGER_CHOOSER_EDGE), 1)) return -1;
    return *(const unsigned char *) (mgr + MANAGER_CHOOSER_EDGE);
}

static void chooser_say(int device, int went, int held) {
    static int said, was_dev = -2, was_went = -1, was_held = -1;
    if (said >= 24 || !shim_report_active() || (went <= 0 && !held)) return;
    if (device == was_dev && went == was_went && held == was_held) return;
    was_dev = device; was_went = went; was_held = held;
    said++;
    char line[200];
    snprintf(line, sizeof(line), "chooser: %s %#x: went down %d, held %d -> %s", shim_device_kind_name(shim_device_kind(device)),
             (unsigned) device, went, held,
             went < 0 ? "asked as held (0x67438c)" : went ? "taken: a new press" : "not taken: held, no new press");
    report_line("coop", line);
}
#endif

int shim_coop_device_pressed(unsigned long input, int device) {
    if (busy_joining(device)) { REFUSED(device, "went down"); return 0; }
    unsigned long b = shim_image_base();
    int went = ((int (*)(unsigned long, int)) (b + FN_DEVICE_WENT_DOWN))(input, device);
#if ISAAC_DIAG
    {
        int held = ((int (*)(unsigned long, int)) (b + FN_DEVICE_HELD))(input, device) & 1;
        chooser_say(device, went & 1, held);
    }
#endif
    return went;
}
int shim_coop_device_pressed_new(unsigned long input, int device) {
    if (busy_joining(device)) { REFUSED(device, "held"); return 0; }
    unsigned long b = shim_image_base();
    int held = ((int (*)(unsigned long, int)) (b + FN_DEVICE_HELD))(input, device);
#if ISAAC_DIAG
    /*
     * The 21st pass had no "chooser:" line at all: the window's chooser may
     * ask this question (0x67438c, its flag w1 = 0) and not 0x674380. While
     * the window is up, a pad's answer here goes into the report as well.
     */
    {
        unsigned long mgr = *(const unsigned long *) (b + MANAGER_PTR);
        if (mgr && shim_eid_readable((const void *) (mgr + 0x56584), 4) && *(const int *) (mgr + 0x56584) != -1
            && shim_device_kind(device) == DEVICE_PAD)
            chooser_say(device, -1, held & 1);
    }
#endif
    return held;
}

/*
 * THE CHOICE STANDS WHERE THE NEW PLAYER WILL (the owner, 28.09.2026: "the
 * menu comes up over player 0's health"). Slot k belongs to player k: the
 * game keeps player 0's device in slot 0 with no choice going (index -1; the
 * report showed "slot 0 device 0x2 index -1" before any join), and the
 * handler takes the slot that already holds the joining device, else the
 * first free one. The HUD draws slot k at player k's corner. Player 0 had
 * been on the pad (the dispatcher hands it the pad on a press), so slot 0
 * still named the pad when the pad asked to join: the choice opened in slot
 * 0, over the first player's hearts. So before the join is asked, the slot of
 * every player of his own in the run is made to name that player's device as
 * he sits NOW - player 0 back on the screen - wherever no choice is going;
 * the joiner matches no slot and takes the first free one, the one of the
 * index it will have. Esau is no player of his own: he takes no slot.
 */
static void slots_of_the_players(unsigned long game) {
    if (!shim_eid_readable((const void *) (game + JOIN_SLOTS), JOIN_SLOT_N * JOIN_SLOT_SIZE)) return;
    unsigned long p[PLAYERS_MAX];
    unsigned dev[PLAYERS_MAX];
    int own[PLAYERS_MAX];
    int n = seats(p, dev, own, PLAYERS_MAX), k = 0;
    for (int i = 0; i < n && k < JOIN_SLOT_N; i++) {
        if (!own[i]) continue;
        unsigned char *sl = (unsigned char *) (game + JOIN_SLOTS + k * JOIN_SLOT_SIZE);
        int was, idx; memcpy(&was, sl, 4); memcpy(&idx, sl + 8, 4);
        if (idx < 0 && was != (int) dev[i]) {
            int d = (int) dev[i], none = -1;
            memcpy(sl, &d, 4);
            memcpy(sl + 8, &none, 4);
            char line[100];
            snprintf(line, sizeof(line), "slot %d named device %#x, now player %d's %#x", k, (unsigned) was, i + 1, dev[i]);
            tell(line);
        }
        k++;
    }
}

static int join_device(unsigned long b, unsigned long game, int id) {
    /*
     * The pad may be player 0's already: the game's dispatcher hands player 0
     * to a pad on its first press. The gesture means "a second player on this
     * pad", so player 0 goes back to the screen first (the handler asks only
     * for a device that drives no player), when the screen is free and player
     * 0 is the only one of his own (J&E count as one - own_player).
     */
    unsigned long p[PLAYERS_MAX];
    unsigned dev[PLAYERS_MAX];
    int own[PLAYERS_MAX];
    int np = seats(p, dev, own, PLAYERS_MAX);
    int owners = 0, touch_taken = 0, mine = -1;
    for (int i = 0; i < np; i++) {
        if (own[i]) owners++;
        if (dev[i] == TOUCH_DEVICE) touch_taken = 1;
        if ((int) dev[i] == id && own[i]) mine = i;
    }
    if (mine == 0 && owners == 1 && !touch_taken) {
        ((void (*)(unsigned long, unsigned, int)) (b + FN_SEAT))(p[0], TOUCH_DEVICE, 1);
        OURS(TOUCH_DEVICE, "join_device (player 0 off the joining device)");
        tell("join asked on player 0's own pad: player 0 goes back to the touch screen first");
    } else if (mine >= 0) {
        tell("join asked on a pad that already plays"); return 0;
    }
    slots_of_the_players(game);
    int k = 0;
    for (int i = 0; i < WANTS; i++) if (!g_want[i].at || g_want[i].device == id) { k = i; break; }
    g_want[k].device = id;
    g_want[k].at = coop_now_ms();
    g_want[k].asked = 0;
    char line[100];
    snprintf(line, sizeof(line), "device %#x asks to join: the game's handler is told", (unsigned) id);
    tell(line);
    return 1;
}

int shim_coop_join_from_pad(void *controller) {
    unsigned long b = shim_image_base();
    if (!b) return 0;
    unsigned long game = *(const unsigned long *) (b + GAME_PTR);
    if (!game || !controller || !shim_sticks_in_game()) { tell("join asked with no run or no pad"); return 0; }
    int id = device_of_controller(controller);
    if (id < 0) { tell("join asked, but the game has no device for our pad"); return 0; }
    return join_device(b, game, id);
}

/* the game's own keyboard device, by its class (devices.c), not by a number the game hands out */
int shim_coop_join_keyboard(void) {
    unsigned long b = shim_image_base();
    if (!b) return 0;
    unsigned long game = *(const unsigned long *) (b + GAME_PTR);
    if (!game || !shim_sticks_in_game()) { tell("keyboard join asked with no run"); return 0; }
    int id = shim_device_keyboard();
    char line[100];
    snprintf(line, sizeof(line), "keyboard join: the game's keyboard device is %#x", (unsigned) id);
    tell(line);
    if (id < 0) return 0;
    if ((unsigned) id == TOUCH_DEVICE) { tell("keyboard join: the keyboard is the touch screen's device - no second player on it"); return 0; }
    return join_device(b, game, id);
}

/*
 * PROBE, report builds (the 18th round: the slot opened and the game never
 * stepped it). The join handler 0x567434 is reached through 0x88d988's tail,
 * and that function leaves early on: [Game+0x27630] != 0 (after 0x7ea144),
 * [Game+0x47d04] != 0 (after 0x667dd4 on Game+0x47aa0), bit 0 of
 * 0x6a6658(Game+0x25810), and the pause ([Game+0x10dfd8] != 0) unless the
 * float at Game+0x1cf440 is above nought or 0x89a0b4(Game+0x25c00) says so.
 * While a slot is open, the raw words behind those gates and the slot itself
 * are written twice a second, so the next report names the gate.
 */
static void handler_gates(unsigned long b, unsigned long game) {
    static int tick, lines;
    if (lines >= 40 || !shim_report_active()) return;
    if (!shim_eid_readable((const void *) (game + JOIN_SLOTS), JOIN_SLOT_N * JOIN_SLOT_SIZE)) return;
    int open = 0;
    for (int k = 0; k < JOIN_SLOT_N; k++) {
        const unsigned char *s = (const unsigned char *) (game + JOIN_SLOTS + k * JOIN_SLOT_SIZE);
        int dev, idx; memcpy(&dev, s, 4); memcpy(&idx, s + 8, 4);
        if (dev >= 0 && dev <= 0xff && idx >= 0) open = 1;
    }
    if (!open) { tick = 0; return; }
    if (tick++ % 30) return;
    if (!shim_eid_readable((const void *) (game + 0x27630), 4) || !shim_eid_readable((const void *) (game + 0x47d04), 4)
        || !shim_eid_readable((const void *) (game + 0x25810), 16) || !shim_eid_readable((const void *) (game + 0x10dfd8), 4)
        || !shim_eid_readable((const void *) (game + 0x1cf440), 4) || !shim_eid_readable((const void *) (game + 0x1cf43e), 1)) return;
    unsigned long p[PLAYERS_MAX];
    int n = shim_eid_players(p, PLAYERS_MAX);
    unsigned d0 = (n > 0 && p[0] && shim_eid_readable((const void *) (p[0] + PLAYER_DEVICE), 4))
                  ? *(const unsigned *) (p[0] + PLAYER_DEVICE) : 0xffffffffu;
    const unsigned char *s0 = (const unsigned char *) (game + JOIN_SLOTS);
    int dev, chr, idx; memcpy(&dev, s0, 4); memcpy(&chr, s0 + 4, 4); memcpy(&idx, s0 + 8, 4);
    char line[300];
    snprintf(line, sizeof(line),
             "gates: [27630] %d, [47d04] %d, [25810] %#x %#x %#x %#x, pause %d, f1cf440 %.2f, b1cf43e %u, lock %d, flags %#x"
             " | slot0 dev %#x chr %d idx %d | p0 dev %#x of %d",
             *(const int *) (game + 0x27630), *(const int *) (game + 0x47d04),
             ((const unsigned *) (game + 0x25810))[0], ((const unsigned *) (game + 0x25810))[1],
             ((const unsigned *) (game + 0x25810))[2], ((const unsigned *) (game + 0x25810))[3],
             *(const int *) (game + 0x10dfd8), (double) *(const float *) (game + 0x1cf440),
             *(const unsigned char *) (game + 0x1cf43e),
             shim_eid_readable((const void *) (game + GAME_JOIN_LOCK), 4) ? *(const int *) (game + GAME_JOIN_LOCK) : -9,
             *(const unsigned char *) (game + GAME_FLAGS), (unsigned) dev, chr, idx, d0, n);
    lines++;
    report_line("coop", line);
}

/*
 * THE CHOSEN DEVICE FOLLOWS PLAYER 0 (the owner, 28.09.2026: "with the
 * keyboard our sticks do not hide; look at how WE did it for the pad").
 *
 * How the pad does it, read off the image: the touch overlay decides every
 * frame (0x8608c0) whether it is up, and asks the object [0xac2490] first:
 * while its +0x38 is set, the overlay is up exactly when its chosen device
 * (+0x34, written by 0x769d3c) is the screen's own (0x10dead). The pad's
 * press goes through the dispatcher's menu road (0x674300 -> 0x66457c ->
 * 0x769d3c): the pad becomes the chosen device, the overlay goes down, and
 * the port's extras follow (sticks.c pad_blocks_touch). A key goes another
 * way: the keyboard device takes player 0 by itself (measured: "1 player(s):
 * [0 device 0]" with the overlay still up, mode 2, chosen device the
 * screen), and nothing tells the object. On the desktop there is no overlay
 * to care.
 *
 * So the port says it, the game's own way: once player 0 sits on a device
 * that is not the screen while the object still names the screen, the chosen
 * device is made player 0's (0x769d3c's one store). The finger's way back is
 * the game's: a touch names the screen again through the dispatcher, as it
 * does after a pad.
 *
 * THIS IS A SECOND WRITER of +0x34 beside 0x769d3c (batch 2, U1 step 8): each
 * of its writes goes into the report with the frame. If the zero step shows
 * the object empty in runs, it works in the menus only; if it is live in
 * runs, it is taken apart with the seat's writers.
 */
static void chosen_follows_player0(unsigned long b) {
    unsigned long disp = *(const unsigned long *) (b + DISPATCHER_OBJECT);
    if (!disp || !shim_eid_readable((const void *) (disp + CHOSEN_DEVICE), 8)) return;
    if (!*(const int *) (disp + CHOSEN_ON)) return;
    unsigned long p[PLAYERS_MAX];
    unsigned dev[PLAYERS_MAX];
    int own[PLAYERS_MAX];
    int n = seats(p, dev, own, PLAYERS_MAX);
    if (n < 1 || !own[0] || dev[0] == NO_DEVICE || dev[0] == TOUCH_DEVICE) return;
    unsigned *chosen = (unsigned *) (disp + CHOSEN_DEVICE);
    if (*chosen != TOUCH_DEVICE) return;
    *chosen = dev[0];
#if ISAAC_DIAG
    {
        static int said;
        if (said < 60 && shim_report_active()) {
            said++;
            char line[160];
            snprintf(line, sizeof(line), "chosen: [0xac2490]+0x34 %#x -> %#x (player 0's), %d player(s), write %d",
                     TOUCH_DEVICE, dev[0], n, said);
            report_line("coop", line);
        }
    }
#endif
}

/*
 * THE HUD'S PLAYER ENTRIES AFTER A CONTINUE (the owner, 28.09.2026: "after
 * closing and opening the game in a co-op run the extra players' health bars
 * are gone, and stay gone"). The HUD keeps eight entries of 0x918 bytes at
 * Game+0x47fe0, one per player, and fills them in 0x6f0440: every player that
 * exists (+0x1c2) gets the entry of its join slot - 0x5673ec finds the slot
 * naming the player's device, or takes a free one for it - and a player with
 * no device (-1) gets none. The game calls that when a player is added (the
 * join, the twins: 0x551310). A save carries no devices: on Continue every
 * co-op player comes back with -1 ("Controller -1 has disconnected"), the
 * entries are built empty, and when the game's own "press a button" hands
 * the players their devices again nobody builds them anew. So the port does
 * what the join does: once a co-op player's device has gone from -1 to a
 * device, the entries are built again, the game's own way - only in co-op,
 * two players of their own or more (J&E after a Continue are one: batch 2).
 */
#define HUD_ENTRIES        0x47fe0
#define FN_HUD_ENTRIES     0x6f0440
/*
 * THE CORNERS STAY THE PLAYERS' (the owner, 05.10.2026: "the co-op player's
 * pocket item is a tiny strip in the lower left corner"). 0x6f0440 gives a
 * player the entry of the join slot that names his device, else the FIRST
 * FREE slot (0x5673ec), and nothing in the game ever frees a slot: a pad that
 * went to sleep and came back under a new number, or a device a player sat on
 * before, keeps naming its slot. Rebuilt over such slots, player 2 went to
 * slot 2 - entry 2, the lower left corner (0x6ee4e0: 80, H-33), where "under
 * the hearts" is the screen's bottom edge - instead of slot 1, the upper right
 * corner the join gave him. The game itself rebuilds only on a Continue, and
 * there the slots are emptied first: 0x56b0a0 ends with 0x5673a8(manager, 1),
 * every slot to -1 and then 0x6f0440. So the rebuild is done that way, while
 * no slot holds a choice (index >= 0: a joiner choosing his character); with
 * a choice going, the plain rebuild as before.
 */
#define FN_SLOTS_RESET     0x5673a8     /* (player manager, rebuild): every join slot to -1, then 0x6f0440 */

static int a_slot_chooses(unsigned long game) {
    if (!shim_eid_readable((const void *) (game + JOIN_SLOTS), JOIN_SLOT_N * JOIN_SLOT_SIZE)) return 1;
    for (int k = 0; k < JOIN_SLOT_N; k++) {
        int idx;
        memcpy(&idx, (const void *) (game + JOIN_SLOTS + k * JOIN_SLOT_SIZE + 8), 4);
        if (idx >= 0) return 1;
    }
    return 0;
}

#if ISAAC_DIAG
/*
 * Which HUD entry draws whom (Game+0x47fe0, eight of 0x918, +0 the player),
 * and the corner of it (0x6ee4e0..0x6ee5d0): 0 upper left, 1 upper right,
 * 2 lower left, 3 lower right, 4 the twin of entry 0 (lower right, Esau's);
 * 5..7 sit at (0, 0) and are never drawn (0x6ee674). SELF-CHECK: the m-th
 * player of his own holds entry m - the corner the join gave him.
 */
static void hud_corners_line(unsigned long game, int n, const unsigned long *p) {
    static const char *corner[8] = { "upper left", "upper right", "lower left", "lower right",
                                     "Esau's lower right", "not drawn", "not drawn", "not drawn" };
    if (!shim_report_active() || !shim_eid_readable((const void *) (game + HUD_ENTRIES), 8 * 0x918)) return;
    char line[500];
    int at = 0, m = 0, bad = 0;
    for (int i = 0; i < n && at < (int) sizeof(line) - 80; i++) {
        int e = -1;
        for (int k = 0; k < 8; k++)
            if (*(const unsigned long *) (game + HUD_ENTRIES + (unsigned long) k * 0x918) == p[i]) e = k;
        int own = own_player(p[i]);
        if (own && *(const int *) (p[i] + PLAYER_DEVICE) != -1) {   /* no device, no entry: the game's own */
            if (e != m) bad = 1;
            m++;
        }
        at += snprintf(line + at, sizeof(line) - (size_t) at, " [%d type %d own %d entry %d %s]", i,
                       *(const int *) (p[i] + PLAYER_TYPE), own, e, e >= 0 ? corner[e] : "none");
    }
    char out[560];
    snprintf(out, sizeof(out), "SELF-CHECK hud corners: %s;%s", bad ? "FAIL (a player of his own off his join corner)" : "OK", line);
    check_say(out);
}
#endif

static void hud_entries_follow_devices(unsigned long b, unsigned long game) {
    static unsigned was[PLAYERS_MAX];
    static int was_n;
    unsigned long p[PLAYERS_MAX];
    unsigned dev[PLAYERS_MAX];
    int own[PLAYERS_MAX];
    int n = seats(p, dev, own, PLAYERS_MAX);
    int rebuild = 0, owners = 0;
    for (int i = 0; i < n && i < PLAYERS_MAX; i++) {
        if (i < was_n && was[i] == NO_DEVICE && dev[i] != NO_DEVICE && own[i]) rebuild = 1;
        owners += own[i];
        was[i] = dev[i];
    }
    was_n = n;
    if (!rebuild || owners < 2) return;
    if (!a_slot_chooses(game)) {
        ((void (*)(unsigned long, int)) (b + FN_SLOTS_RESET))(game + PLAYER_MANAGER, 1);
        tell("a player got its device back: join slots emptied and the HUD's player entries built again (0x5673a8)");
    } else {
        ((void (*)(unsigned long)) (b + FN_HUD_ENTRIES))(game + HUD_ENTRIES);
        tell("a player got its device back while a slot chooses: the HUD's player entries built again (0x6f0440)");
    }
#if ISAAC_DIAG
    hud_corners_line(game, n, p);
#endif
}

/*
 * THE TWINS AFTER A RESTORED RUN (the owner, 04.10.2026: "do it as well as
 * possible"; the research notes of 04.10.2026, co-op twins).
 *
 * A twin (+0x2580: Esau for Jacob, T.Forgotten's soul, T.Lazarus' other form
 * with Birthright) is made by 0x551310 right after its main and linked both
 * ways through the game's weak link 0x4ee4a8 (slot, player): it leaves the old
 * player's list of watchers (+0x3b0) and joins the new one's. A Continue
 * (0x56959c) throws the links away and finds them again by type and device;
 * loader.c makes that search stop at the first match, but a save written
 * before that - or one where the pairs' devices were already crossed - can
 * still bring them back crossed or one-sided.
 *
 * So once the run is restored, the links are looked at, kind by kind (J&E,
 * T.Forgotten and the soul, T.Lazarus' two forms): a kind is spoilt when a
 * link is one-sided, joins two players that are no pair, sits on a child
 * (+0x390), or a main has nobody while a free partner stands after him. A
 * spoilt kind is linked again in the list's order - every main (19, 35; 29 or
 * 38, whichever stands first) takes the first free partner after him, which is
 * how 0x551310 lays them out - through 0x4ee4a8 both ways, and the twin is put
 * on the main's device, as 0x551310 does; the HUD's entries are built again,
 * as 0x551310 does. Nobody is made and nobody is taken away. A kind that is
 * whole is left alone, even when it is not in that order (two T.Lazarus who
 * took Birthright in the other order). A link to something not in the list is
 * not touched (its old owner cannot be read) - only reported.
 *
 * "Restored" is when the run appears (players from none) or when the line-up
 * changes with every player on -1 - the mark 0x56b0a0 leaves on a Continue.
 * In the middle of a run the game's own doings are not mended, only watched:
 * the type change 0x510268 makes a new Esau for a Jacob who still has one
 * (0x5106a0..0x5107a8, and T.Forgotten's soul at 0x510f94) - that is the
 * game's, and the report says so.
 */
#define MENU_DEVICE      0x5658c        /* Manager: the device that last pressed in a menu (0x670ea4) */
#define PLAYER_HIDDEN    0x2588         /* T.Lazarus' other form while he has no Birthright (0x51e9b0) */
#define FN_TWIN_LINK     0x4ee4a8       /* (player + 0x2580, other or nought): the game's weak link */
#define TWINS_MAX        16             /* four J&E pairs and their children */
#define TWIN_OUTSIDE     (-2)           /* a twin that is not in the players' list */
#define PLAYER_NOWHERE   0xffffffffu
/* the run read from the save, inside the Manager (0x6713d0: Manager + 0x21428 goes to 0x88b994) */
#define SAVE_STATE       0x21428
#define SAVE_PLAYERS_N   (SAVE_STATE + 0x23068)  /* 0x56959c: the count */
#define SAVE_PLAYER0     (SAVE_STATE + 0x23070)  /* the records, 0x6e8 bytes each */
#define SAVE_PLAYER_SIZE 0x6e8
#define REC_DEVICE       0x630          /* written from +0x19ec (0x54faf4), read back (0x525308) */
#define REC_SUB          0x634          /* not nought: the +0x2588 helper of record i + this */
#define REC_PARENT       0x638          /* the parent's record, or negative */

/* the partner a main looks for, as 0x5697fc picks it; -1: not a main */
static int twin_partner(int type) {
    switch (type) {
    case 19: return 20;                 /* Jacob -> Esau */
    case 29: return 38;                 /* T.Lazarus -> his other form */
    case 38: return 29;
    case 35: return 40;                 /* T.Forgotten -> his soul */
    default: return -1;
    }
}

/* the kind of a twin type: 1 J&E, 2 T.Lazarus, 3 T.Forgotten; nought for the rest */
static int twin_kind(int type) {
    switch (type) {
    case 19: case 20: return 1;
    case 29: case 38: return 2;
    case 35: case 40: return 3;
    default: return 0;
    }
}

/* may a and b be a pair at all */
static int twin_pair_types(int a, int b) {
    return twin_partner(a) == b || twin_partner(b) == a;
}

typedef struct {
    unsigned long p[TWINS_MAX];
    int n, type[TWINS_MAX], parent[TWINS_MAX], twin[TWINS_MAX], hidden[TWINS_MAX];
    unsigned dev[TWINS_MAX];
} twins_t;

/* the players, their types and links by index; nought when the list cannot be read whole */
static int twins_read(twins_t *t) {
    t->n = shim_eid_players(t->p, TWINS_MAX);
    if (t->n <= 0 || t->n >= TWINS_MAX) return 0;   /* full: there may be more than we see */
    for (int i = 0; i < t->n; i++) {
        unsigned long q = t->p[i];
        if (!q || !shim_eid_readable((const void *) (q + PLAYER_PARENT), 8)
            || !shim_eid_readable((const void *) (q + PLAYER_TYPE), 4)
            || !shim_eid_readable((const void *) (q + ENT_VARIANT), 4)
            || !shim_eid_readable((const void *) (q + PLAYER_DEVICE), 4)
            || !shim_eid_readable((const void *) (q + PLAYER_TWIN), 16)) return 0;   /* +0x2580 and +0x2588 */
        /*
         * A co-op baby or Found Soul (variant 1, made by 0x565f90) is no twin
         * of anybody: the save skips it (0x569b70) and the restore never links
         * it. Its type field is kept out of the kinds.
         */
        t->type[i] = *(const int *) (q + ENT_VARIANT) == 0 ? *(const int *) (q + PLAYER_TYPE) : -1;
        t->parent[i] = *(const unsigned long *) (q + PLAYER_PARENT) != 0;
        t->dev[i] = *(const unsigned *) (q + PLAYER_DEVICE);
        t->hidden[i] = *(const unsigned long *) (q + PLAYER_HIDDEN) != 0;
        unsigned long w = *(const unsigned long *) (q + PLAYER_TWIN);
        t->twin[i] = -1;
        if (w) {
            t->twin[i] = TWIN_OUTSIDE;
            for (int k = 0; k < t->n; k++) if (t->p[k] == w) t->twin[i] = k;
        }
    }
    return 1;
}

/*
 * A link that holds: both ways, between a pair, no child on either end - and
 * for T.Lazarus, neither end keeping a hidden form. His other form lives in
 * +0x2588 until Birthright is taken; then 0x51eb2c..0x51eb58 links it as the
 * twin (+0x2580, both ways) and clears +0x2588 on both. A T.Lazarus linked as
 * a twin while he still keeps a hidden form is no pair: the game's own update
 * (0x51e96c..0x51ea18: a twin, and Birthright on neither) would make the other
 * player his hidden form, out of the list, and his own form would be lost.
 */
static int twin_link_whole(const twins_t *t, int i) {
    int w = t->twin[i];
    if (w < 0 || t->twin[w] != i || !twin_pair_types(t->type[i], t->type[w]) || t->parent[i] || t->parent[w]) return 0;
    return twin_kind(t->type[i]) != 2 || (!t->hidden[i] && !t->hidden[w]);
}

/* may i be paired anew: no child, no twin yet; a T.Lazarus only with no hidden form */
static int twin_free(const twins_t *t, int i, const int *want) {
    if (t->parent[i] || (want ? want[i] : t->twin[i]) >= 0) return 0;
    return twin_kind(t->type[i]) != 2 || !t->hidden[i];
}

/*
 * The links a kind should have; want[i] is i's twin, or -1.
 *
 * J&E and T.Forgotten (kinds 1, 3): the twin is made right after its main
 * (0x551310 from 0x506a84) and nobody stands between them, so the list's
 * order is the truth - every main, in order, takes the first free partner
 * after him that is no child.
 *
 * T.Lazarus (kind 2) has no such order: the other form joins the list when
 * Birthright is taken, at its end, and both forms change type with every flip
 * (29 <-> 38). So a whole link stays as it is, a broken one is cut, and only
 * the forms that keep no hidden form (Birthright pairs that lost their link)
 * are paired anew, in order.
 */
static void twins_wanted(const twins_t *t, int kind, int *want) {
    for (int i = 0; i < t->n; i++) want[i] = -1;
    if (kind == 2)
        for (int i = 0; i < t->n; i++)
            if (twin_kind(t->type[i]) == kind && twin_link_whole(t, i)) want[i] = t->twin[i];
    for (int i = 0; i < t->n; i++) {
        if (twin_kind(t->type[i]) != kind || !twin_free(t, i, want)) continue;
        int pt = twin_partner(t->type[i]);
        if (pt < 0) continue;
        for (int j = i + 1; j < t->n; j++)
            if (t->type[j] == pt && twin_free(t, j, want)) { want[i] = j; want[j] = i; break; }
    }
}

/*
 * Is a kind spoilt: 1 spoilt, 0 whole, -1 a link out of the list (left alone).
 * why gets a word for the report. A broken link spoils a kind, and so does a
 * main with nobody while a free partner of his has nobody either, anywhere in
 * the list: J&E and T.Forgotten are never apart in the game (a ghost
 * T.Forgotten has no soul at all, 0x508548), and two T.Lazarus forms with no
 * hidden form and no twin are a Birthright pair that lost its link.
 */
static int twins_spoilt(const twins_t *t, int kind, const char **why) {
    int spoilt = 0;
    for (int i = 0; i < t->n; i++) {
        if (twin_kind(t->type[i]) != kind) continue;
        int w = t->twin[i];
        if (w == TWIN_OUTSIDE) { *why = "a link out of the list"; return -1; }
        if (w < 0 || twin_link_whole(t, i)) continue;
        *why = t->twin[w] != i ? "one-sided" : !twin_pair_types(t->type[i], t->type[w]) ? "not a pair"
             : (t->parent[i] || t->parent[w]) ? "a child linked" : "a T.Lazarus with a hidden form linked";
        spoilt = 1;
    }
    if (spoilt) return 1;
    for (int i = 0; i < t->n; i++) {
        if (twin_kind(t->type[i]) != kind || !twin_free(t, i, NULL)) continue;
        int pt = twin_partner(t->type[i]);
        if (pt < 0) continue;
        for (int j = 0; j < t->n; j++)
            if (t->type[j] == pt && twin_free(t, j, NULL)) { *why = "a main alone, a partner free"; return 1; }
    }
    return 0;
}

#if ISAAC_DIAG
/* the list in one line: [i type T dev D twin j|-|out child hidden] */
static int twins_line(const twins_t *t, char *line, int room) {
    int at = 0;
    for (int i = 0; i < t->n && at < room - 60; i++) {
        char tw[8];
        if (t->twin[i] == TWIN_OUTSIDE) snprintf(tw, sizeof(tw), "out");
        else if (t->twin[i] < 0) snprintf(tw, sizeof(tw), "-");
        else snprintf(tw, sizeof(tw), "p%d", t->twin[i]);
        at += snprintf(line + at, (size_t) (room - at), " [%d type %d dev %#x twin %s%s%s]", i, t->type[i],
                       t->dev[i], tw, t->parent[i] ? " child" : "", t->hidden[i] ? " hidden" : "");
    }
    return at;
}

/* the run as the save holds it, right after it was read back: what the twins were found by */
static void saved_devices_line(unsigned long b, const twins_t *t) {
    if (!shim_report_active()) return;
    unsigned long mgr = *(const unsigned long *) (b + MANAGER_PTR);
    if (!mgr || !shim_eid_readable((const void *) (mgr + SAVE_PLAYERS_N), 4)) return;
    int n = *(const int *) (mgr + SAVE_PLAYERS_N);
    if (n < 1 || n > TWINS_MAX
        || !shim_eid_readable((const void *) (mgr + SAVE_PLAYER0), (unsigned long) n * SAVE_PLAYER_SIZE)) return;
    char line[700];
    int at = snprintf(line, sizeof(line), "continue: saved players %d:", n);
    int mains = 0, same = 1;
    for (int i = 0; i < n && at < (int) sizeof(line) - 60; i++) {
        const unsigned char *r = (const unsigned char *) (mgr + SAVE_PLAYER0 + (unsigned long) i * SAVE_PLAYER_SIZE);
        int type = *(const int *) r, dev = *(const int *) (r + REC_DEVICE);
        int sub = *(const int *) (r + REC_SUB), parent = *(const int *) (r + REC_PARENT);
        at += snprintf(line + at, sizeof(line) - (size_t) at, " [%d type %d dev %#x%s", i, type, (unsigned) dev,
                       sub ? " helper" : "");
        if (parent >= 0) at += snprintf(line + at, sizeof(line) - (size_t) at, " parent %d", parent);
        at += snprintf(line + at, sizeof(line) - (size_t) at, "]");
        if (!sub) {
            if (mains >= t->n || t->type[mains] != type) same = 0;
            mains++;
        }
    }
    if (mains != t->n) same = 0;
    snprintf(line + at, sizeof(line) - (size_t) at, " | %s the live line-up", same ? "matches" : "does NOT match");
    report_line("coop", line);
}

/*
 * WHO GOT WHICH DEVICE ON THE CONTINUE (the owner, 05.10.2026: "the
 * characters still get mixed after re-entering"). The restore does not keep
 * the saved devices: 0x56b0a0 sits everybody on -1, then hands them out in
 * the players' order - player 0 the device that last pressed in the menus
 * (Manager+0x5658c, written by 0x670ea4), every next one the next device the
 * input system has seen used (+0xc, set by 0x8b5f80 the first time anything
 * is held on it), in the input list's own order: the screen, the keyboard,
 * the pads. A player left without one is the game's "Select a Controller!".
 * The line names the menu device, every device with its "used" byte, and
 * each player's saved device beside the one he got; "kept" when every saved
 * device that still exists went back to its player.
 */
#define DEVICE_USED      0xc            /* byte: the device has been used (0x8a7d88; 0x8b5f80 sets it) */

static void restore_seats_line(unsigned long b, const twins_t *t) {
    if (!shim_report_active()) return;
    unsigned long mgr = *(const unsigned long *) (b + MANAGER_PTR);
    if (!mgr || !shim_eid_readable((const void *) (mgr + MENU_DEVICE), 4)
        || !shim_eid_readable((const void *) (mgr + SAVE_PLAYERS_N), 4)) return;
    char line[800];
    int at = snprintf(line, sizeof(line), "continue: seats: menu device %#x; devices",
                      *(const unsigned *) (mgr + MENU_DEVICE));
    unsigned long input = b + INPUT_SYSTEM;
    int nd = ((int (*)(unsigned long)) (b + FN_DEVICE_COUNT))(input);
    for (int i = 0; i < nd && i < DEVICES_MAX && at < (int) sizeof(line) - 60; i++) {
        int id = ((int (*)(unsigned long, int)) (b + FN_DEVICE_AT))(input, i);
        unsigned long o = shim_device_object(id);
        int used = o && shim_eid_readable((const void *) (o + DEVICE_USED), 1) ? *(const unsigned char *) (o + DEVICE_USED) : -1;
        at += snprintf(line + at, sizeof(line) - (size_t) at, " [%#x %s used %d]", (unsigned) id,
                       shim_device_kind_name(shim_device_kind(id)), used);
    }
    int n = *(const int *) (mgr + SAVE_PLAYERS_N), k = 0, kept = 1;
    at += snprintf(line + at, sizeof(line) - (size_t) at, "; players");
    for (int i = 0; i < n && i < TWINS_MAX && k < t->n && at < (int) sizeof(line) - 60; i++) {
        const unsigned char *r = (const unsigned char *) (mgr + SAVE_PLAYER0 + (unsigned long) i * SAVE_PLAYER_SIZE);
        if (!shim_eid_readable(r, SAVE_PLAYER_SIZE)) break;
        if (*(const int *) (r + REC_SUB)) continue;                 /* a hidden form: not in the list */
        int saved = *(const int *) (r + REC_DEVICE);
        unsigned now = t->dev[k];
        int exists = saved != -1 && shim_device_object(saved) != 0;
        if (exists && (unsigned) saved != now) kept = 0;
        at += snprintf(line + at, sizeof(line) - (size_t) at, " [%d type %d saved %#x%s now %#x]", k, t->type[k],
                       (unsigned) saved, exists ? "" : " gone", now);
        k++;
    }
    snprintf(line + at, sizeof(line) - (size_t) at, " | %s", kept ? "kept" : "NOT kept: a saved device that still exists went to another player");
    report_line("coop", line);
}

/*
 * THE GAME'S "Select a Controller!" (RESELECT_CONTROLLER, 0x67422c, its id at
 * Manager+0x56584): up while a player of his own has no device, closed by the
 * dispatcher when the last one is seated. Its chooser 0x674300 takes the
 * first device in the input list on which anything went down that drives no
 * player - and, with two players of their own or more, never the screen
 * (0x674364..0x67436c). The line says when it opens, for whom, and when it
 * closes, who got what.
 */
#define RESELECT_POPUP   0x56584        /* Manager: the window's id, -1 when none (0x671150 -> 0x6733d8's x3) */

static void reselect_watch(unsigned long b) {
    static int was = -1, lines;
    if (lines >= 20 || !shim_report_active()) return;
    unsigned long mgr = *(const unsigned long *) (b + MANAGER_PTR);
    if (!mgr || !shim_eid_readable((const void *) (mgr + RESELECT_POPUP), 4)) return;
    int id = *(const int *) (mgr + RESELECT_POPUP);
    if ((id == -1) == (was == -1)) return;
    was = id;
    unsigned long p[PLAYERS_MAX];
    unsigned dev[PLAYERS_MAX];
    int own[PLAYERS_MAX];
    int n = seats(p, dev, own, PLAYERS_MAX), owners = 0;
    for (int i = 0; i < n; i++) owners += own[i];
    char line[400];
    int at = snprintf(line, sizeof(line), "reselect window %s (own players %d, the screen %s):", id == -1 ? "closed" : "up",
                      owners, owners >= 2 ? "is not taken" : "may be taken");
    for (int i = 0; i < n && at < (int) sizeof(line) - 60; i++)
        at += snprintf(line + at, sizeof(line) - (size_t) at, " [%d type %d own %d dev %#x]", i,
                       *(const int *) (p[i] + PLAYER_TYPE), own[i], dev[i]);
    lines++;
    report_line("coop", line);
}
#endif

static void twins_after_restore(unsigned long b, unsigned long game, const char *how) {
    twins_t t;
    if (!twins_read(&t)) return;
#if ISAAC_DIAG
    saved_devices_line(b, &t);
    restore_seats_line(b, &t);
    char line[900];
#endif
    (void) how;
    int want[TWINS_MAX], hud = 0;
    for (int kind = 1; kind <= 3; kind++) {
        if (kind > 1 && hud && !twins_read(&t)) break;      /* a kind mended: read the links anew */
        const char *why = "";
        int s = twins_spoilt(&t, kind, &why);
#if ISAAC_DIAG
        if (s < 0 && shim_report_active()) {
            int at = snprintf(line, sizeof(line), "twins: kind %d after the restore (%s) has %s - left alone:", kind, how, why);
            twins_line(&t, line + at, (int) sizeof(line) - at);
            report_line("coop", line);
        }
#endif
        if (s <= 0) continue;
#if ISAAC_DIAG
        char before[400];
        twins_line(&t, before, (int) sizeof(before));
#endif
        twins_wanted(&t, kind, want);
        int moved[TWINS_MAX] = { 0 };
        for (int i = 0; i < t.n; i++) {
            if (twin_kind(t.type[i]) != kind || t.twin[i] == want[i]) continue;
            unsigned long to = want[i] >= 0 ? t.p[want[i]] : 0;
            ((void (*)(unsigned long, unsigned long)) (b + FN_TWIN_LINK))(t.p[i] + PLAYER_TWIN, to);
            moved[i] = 1;
            hud = 1;
        }
        /* a pair linked anew: the twin sits where his main sits (0x551368) */
        for (int i = 0; i < t.n; i++) {
            int j = want[i];
            if (twin_kind(t.type[i]) != kind || j <= i || !(moved[i] || moved[j])) continue;
            *(unsigned *) (t.p[j] + PLAYER_DEVICE) = *(const unsigned *) (t.p[i] + PLAYER_DEVICE);
        }
#if ISAAC_DIAG
        if (shim_report_active()) {
            twins_t now;
            char after[400] = "";
            if (twins_read(&now)) twins_line(&now, after, (int) sizeof(after));
            snprintf(line, sizeof(line), "twins of kind %d mended after the restore (%s; %s):%s ->%s",
                     kind, how, why, before, after);
            report_line("coop", line);
        }
#endif
    }
    if (hud) ((void (*)(unsigned long)) (b + FN_HUD_ENTRIES))(game + HUD_ENTRIES);   /* as 0x551310 ends */
#if ISAAC_DIAG
    if (twins_read(&t)) hud_corners_line(game, t.n, t.p);
#endif
}

/*
 * EVERY PLAYER GETS BACK WHAT HE PLAYED ON (the owner, 05.10.2026: "on
 * returning, players must get the character or characters they played").
 *
 * The game does not: a Continue (0x56b0a0) sits everybody on -1 and hands the
 * devices out anew - player 0 the device that last pressed in a menu
 * (Manager+0x5658c), every next one the next used device in the input list's
 * order, the screen and the keyboard before the pads. The save keeps each
 * player's device (record+0x630), but as the game's NUMBER, and a number means
 * nothing in another process, nor for a pad that slept and came back.
 *
 * So the port remembers what the device WAS - the screen, the keyboard, a pad
 * by its Android descriptor (devices.c's ledger) - beside the save:
 *   - between frames, every player's last device (a player on -1 keeps the
 *     one he had: a pad asleep at the moment of the save is still his);
 *   - when the game writes rep_gamestateN.dat, saves.c writes those beside it,
 *     in the app's own folder (port-seats-<leaf>.txt), one line a player of
 *     the list (type, what) - the order the save writes its records in
 *     (0x569afc; co-op babies and Found Souls, variant 1, are skipped there and
 *     here);
 *   - when the game reads rep_gamestateN.dat, saves.c hands the lines back.
 * Once the run is restored, with two players of their own or more (one is
 * the game's own business: its dispatcher hands him whatever is held), every
 * player of his own whose device is here today gets it back, through the
 * game's seat with the twins (0x5067e4, flag 1), first come first served.
 * The lines are trusted only if their types match the restored list one by
 * one; else the record's own number is asked of the ledger (the same process:
 * exit to the menu and Continue). A player whose device is not here keeps
 * what the game gave him, if nobody of the restored took it; else the next
 * used device in the game's own order; else -1 and the game's window. Then
 * the join slots and the HUD's entries are made anew, as 0x56b0a0 ends
 * (0x5673a8): every corner follows its player.
 */
#define SEATS_MAX    TWINS_MAX
#define SEAT_WHAT    16
static pthread_mutex_t g_seats_lock = PTHREAD_MUTEX_INITIALIZER;
#define SEAT_RUNS    4                  /* rep_gamestate1..3, and a spare */
static char g_seats_text[SEATS_MAX * 32];           /* the line-up now, for the next save */
/*
 * The lines beside every run file the game has read or written in this
 * process: the save menu reads all three slots, so which one is continued is
 * told by the lines themselves - the newest whose types are the restored
 * line-up, one by one.
 */
static struct { char leaf[40]; char text[SEATS_MAX * 32]; long seq; } g_seats_runs[SEAT_RUNS];
static long g_seats_seq;

/* between frames: the players' last devices, as text for the save */
static unsigned long g_seat_ptr[SEATS_MAX];        /* the players, as last seen */
static char g_seat_what[SEATS_MAX][SEAT_WHAT];      /* and what each last played on */
static unsigned g_seat_now[SEATS_MAX];              /* the device each sat on when last seen (-1: none) */
static unsigned char g_seat_lost[SEATS_MAX];        /* on -1 because his device went away (see seat_lost) */
static int g_seat_n;

#define FN_DEVICE_ALIVE  0x8b6c9c       /* (input, id): the device exists */

/*
 * WHY A PLAYER IS ON -1 (the owner, 06.10.2026: "the Change Controller button
 * doesn't work at all" since 1.7). The game puts a player of his own on -1 in
 * two ways, and only one of them is a device that went away:
 *   - 0x6741f0, its dispatcher: the device is no longer in the input list
 *     (0x8b6c9c says no) - a pad asleep, out of reach, unplugged;
 *   - 0x673648, Pause -> Options -> Change Controller (the row's handler
 *     0x6e31d0 -> 0x8a2a98, player 0): his confirm press is spent on his device
 *     (0x8b69f4, action 14), he is let go (0x5067e4(p, -1, 1)) with the device
 *     still here, and Manager+0x56588 = 1 makes the window's chooser wait for a
 *     button GOING DOWN (0x674380) - a new press on any free device.
 * A player is lost when the device he sat on the frame before is gone from
 * the input list at the moment he is first seen on -1 (the dispatcher writes
 * -1 for a gone device only, and a gone device never comes back under its old
 * number: 0x8a761c counts them). Once lost, lost until he sits again.
 */
static int seat_lost(unsigned long b, int k) {
    if (g_seat_now[k] == NO_DEVICE) return g_seat_lost[k];
    return !(((int (*)(unsigned long, int)) (b + FN_DEVICE_ALIVE))(b + INPUT_SYSTEM, (int) g_seat_now[k]) & 1);
}

static void seats_memory(unsigned long b) {
    unsigned long *ptr = g_seat_ptr;
    char (*what)[SEAT_WHAT] = g_seat_what;
    int n_was = g_seat_n;
    unsigned long p[SEATS_MAX];
    int n = shim_eid_players(p, SEATS_MAX);
    if (n <= 0 || n >= SEATS_MAX) return;              /* the menus: keep the last run's lines */
    char nw[SEATS_MAX][SEAT_WHAT];
    unsigned long np[SEATS_MAX];
    unsigned nn[SEATS_MAX];
    unsigned char nl[SEATS_MAX];
    char text[sizeof(g_seats_text)];
    int at = 0, k = 0;
    for (int i = 0; i < n; i++) {
        unsigned long q = p[i];
        if (!q || !shim_eid_readable((const void *) (q + PLAYER_DEVICE), 4)
            || !shim_eid_readable((const void *) (q + ENT_VARIANT), 4)
            || !shim_eid_readable((const void *) (q + PLAYER_TYPE), 4)) return;
        if (*(const int *) (q + ENT_VARIANT) != 0) continue;  /* not in the save */
        np[k] = q;
        nw[k][0] = 0;
        int dev = *(const int *) (q + PLAYER_DEVICE);
        nn[k] = (unsigned) dev;
        nl[k] = 0;
        if (dev == -1)
            for (int j = 0; j < n_was; j++) if (ptr[j] == q) nl[k] = (unsigned char) seat_lost(b, j);
        if (!(dev != -1 && shim_device_identity(dev, nw[k], SEAT_WHAT)))
            for (int j = 0; j < n_was; j++) if (ptr[j] == q) snprintf(nw[k], SEAT_WHAT, "%s", what[j]);
        at += snprintf(text + at, sizeof(text) - (size_t) at, "%d %s\n", *(const int *) (q + PLAYER_TYPE),
                       nw[k][0] ? nw[k] : "-");
        k++;
    }
    memcpy(ptr, np, sizeof(unsigned long) * (size_t) k);
    memcpy(what, nw, sizeof(nw[0]) * (size_t) k);
    memcpy(g_seat_now, nn, sizeof(nn[0]) * (size_t) k);
    memcpy(g_seat_lost, nl, sizeof(nl[0]) * (size_t) k);
    g_seat_n = k;
    pthread_mutex_lock(&g_seats_lock);
    memcpy(g_seats_text, text, (size_t) at + 1);
    pthread_mutex_unlock(&g_seats_lock);
}

/* the entry of a run file, taken anew when needed (the oldest goes) */
static int seats_run_of(const char *leaf) {
    int k = 0;
    for (int i = 0; i < SEAT_RUNS; i++) {
        if (!strcmp(g_seats_runs[i].leaf, leaf)) return i;
        if (g_seats_runs[i].seq < g_seats_runs[k].seq) k = i;
    }
    snprintf(g_seats_runs[k].leaf, sizeof(g_seats_runs[k].leaf), "%s", leaf);
    g_seats_runs[k].text[0] = 0;
    return k;
}

/* saves.c, when the game has written a run: the text to keep beside it; 0 when there is none */
int shim_coop_seats_text(const char *leaf, char *out, int room) {
    pthread_mutex_lock(&g_seats_lock);
    int n = snprintf(out, (size_t) room, "%s", g_seats_text);
    if (n > 0 && n < room) {
        int k = seats_run_of(leaf);
        snprintf(g_seats_runs[k].text, sizeof(g_seats_runs[k].text), "%s", g_seats_text);
        g_seats_runs[k].seq = ++g_seats_seq;
    }
    pthread_mutex_unlock(&g_seats_lock);
    return n > 0 && n < room ? n : 0;
}

/* saves.c, when the game reads a run: the text that was kept beside it (NULL: none) */
void shim_coop_seats_read(const char *leaf, const char *text, long len) {
    pthread_mutex_lock(&g_seats_lock);
    int k = seats_run_of(leaf);
    long n = text && len > 0 ? (len < (long) sizeof(g_seats_runs[k].text) - 1 ? len : (long) sizeof(g_seats_runs[k].text) - 1) : 0;
    if (n) memcpy(g_seats_runs[k].text, text, (size_t) n);
    g_seats_runs[k].text[n] = 0;
    g_seats_runs[k].seq = ++g_seats_seq;
    pthread_mutex_unlock(&g_seats_lock);
}

/*
 * A PAD THAT COMES BACK FINDS ITS PLAYER (the same wish, in the middle of a
 * run). A pad asleep or out of reach is gone for the game (0x6741f0 sits its
 * player on -1, the "Select a Controller!" window opens), and back it is a
 * new device with a new number. Its Android descriptor is the same, though,
 * and devices.c's ledger tells it: a player of his own on -1 whose last
 * device is here again and drives nobody is seated back on it (0x5067e4,
 * flag 1 - his twin with him), between frames, before the window's chooser
 * hands it to the first pad pressed. Not while a device of ours joins.
 *
 * Only a player who LOST his device (seat_lost): one the game let go with his
 * device still here - Change Controller - waits for the game's own window and
 * a new press, as on iOS. 1.7 and 1.71 seated him straight back on the device
 * he had just given up, the frame after, so the button did nothing at all.
 */
#if ISAAC_DIAG
static void let_go_say(int i, unsigned was) {
    static int said;
    if (said >= 20 || !shim_report_active()) return;
    said++;
    char line[200];
    snprintf(line, sizeof(line), "player %d let go with device %#x still here (Change Controller): left to the game's"
             " window, Manager+0x56588 %d", i + 1, was, chooser_waits_for_a_press());
    report_line("coop", line);
}
#endif

static void orphans_take_their_own(unsigned long b) {
    unsigned long p[PLAYERS_MAX];
    unsigned dev[PLAYERS_MAX];
    int own[PLAYERS_MAX];
    int n = seats(p, dev, own, PLAYERS_MAX);
    for (int i = 0; i < n; i++) {
        if (!own[i] || dev[i] != NO_DEVICE) continue;
        const char *what = NULL;
        int lost = 0;
        for (int k = 0; k < g_seat_n; k++)
            if (g_seat_ptr[k] == p[i] && g_seat_what[k][0]) {
                what = g_seat_what[k];
                lost = seat_lost(b, k);
#if ISAAC_DIAG
                if (!lost && g_seat_now[k] != NO_DEVICE) let_go_say(i, g_seat_now[k]);
#endif
            }
        if (!what || !lost) continue;
        int d = shim_device_of_identity(what), busy = d == -1 || busy_joining(d);
        for (int j = 0; j < n && !busy; j++) if (dev[j] == (unsigned) d) busy = 1;
        if (busy) continue;
        ((void (*)(unsigned long, unsigned, int)) (b + FN_SEAT))(p[i], (unsigned) d, 1);
        char line[160];
        snprintf(line, sizeof(line), "player %d's %s is back as device %#x: seated on it again", i + 1, what, (unsigned) d);
        tell(line);
        dev[i] = (unsigned) d;
    }
}

/*
 * The lines of one run against the save's records (the ones the restore read):
 * 1 and what[r] filled (by record) when the types are the records', one by
 * one. A record of a hidden form (+0x634) has no line: it is not in the list.
 */
static int seats_lines_match(const char *s, const int *rtype, const int *rsub, int nr, char what[][SEAT_WHAT]) {
    int k = 0;
    for (int r = 0; r < nr; r++) {
        what[r][0] = 0;
        if (rsub[r]) continue;
        int type;
        char w[SEAT_WHAT];
        if (sscanf(s, "%d %15s", &type, w) != 2 || type != rtype[r]) return 0;
        snprintf(what[r], SEAT_WHAT, "%s", strcmp(w, "-") ? w : "");
        const char *nl = strchr(s, '\n');
        s = nl ? nl + 1 : s + strlen(s);
        k++;
    }
    return k > 0 && !*s;                                    /* every line used, none left over */
}

/*
 * THE CONTINUE'S HAND-OUT, OURS (the 21st pass, 05.10.2026: "it still mixes
 * the characters up; it simply does not remember who was where").
 *
 * The report of that pass showed why the first try could not work: the save
 * held [0 J dev 0x10dead] [1 E 0x10dead] [2 J 0x2] [3 E 0x2], but the restore
 * gave back the list as J, J, E, E (0x569358 matches the live players to the
 * records by device and makes the missing ones at the end) - the order of the
 * list is not the records', and once 0x56b0a0 has thrown every device away
 * nothing tells which player came from which record. Then 0x56b0a0 gave the
 * menu's device, the pad (Continue was pressed on it), to player 0 - the
 * screen's Jacob - and nothing to the other Jacob: the window opened, and the
 * pad, held by player 0, could not answer it until a finger took player 0 off
 * it. That is the 21st pass's second complaint too.
 *
 * The one moment the mapping exists is right after 0x56959c: every player
 * sits on his OWN record's device (0x5252c8, 0x525308). So the call of
 * 0x56b0a0 at 0x88b9f0 comes here (loader.c coop_handout_ours, through the
 * join's trampoline 0x8dab28 shared by its first argument: the join asks with
 * the input system, this call gives the player manager), and:
 *   - a record's number is turned into what the device WAS: the lines kept
 *     beside the run (records and lines in one order, 0x569afc), else this
 *     process's ledger, else 0x10dead is the screen;
 *   - every player of his own gets that device back if it is here today and
 *     nobody before him took it; the rest are handed out as 0x56b0a0 does - the
 *     menu's device, then the used ones in the input list's order - skipping
 *     what is taken; nothing left: -1 and the game's window;
 *   - the seats themselves are the game's: everybody to -1 (flag 0, as
 *     0x56b0a0's first loop), then the players of their own with flag 1, their
 *     twins and children with them; then 0x5673a8(manager, 1), 0x56b0a0's end.
 * With one player of his own, or when the list cannot be read, it is the
 * game's 0x56b0a0 untouched.
 */
#define FN_HANDOUT       0x56b0a0       /* (player manager): the Continue's hand-out of devices */
#define FN_DEVICE_USED   0x8b6cb8       /* (input, id): the device's +0xc, "used" */

static void continue_handout(unsigned long b, unsigned long pm) {
    twins_t t;
    int own[TWINS_MAX], owners = 0;
    unsigned long game = *(const unsigned long *) (b + GAME_PTR);
    /* the list read is the Game's: only when this manager is that Game's */
    if (!game || pm != game + PLAYER_MANAGER || !twins_read(&t)) { ((void (*)(unsigned long)) (b + FN_HANDOUT))(pm); return; }
    for (int i = 0; i < t.n; i++) { own[i] = t.type[i] >= 0 && own_player(t.p[i]); owners += own[i]; }
    if (owners < 2) { ((void (*)(unsigned long)) (b + FN_HANDOUT))(pm); return; }
    unsigned long mgr = *(const unsigned long *) (b + MANAGER_PTR);
    unsigned long input = b + INPUT_SYSTEM;
    /* the records the restore read: their types and numbers */
    int nr = 0, rtype[TWINS_MAX * 2], rsub[TWINS_MAX * 2], rdev[TWINS_MAX * 2];
    if (mgr && shim_eid_readable((const void *) (mgr + SAVE_PLAYERS_N), 4)) {
        nr = *(const int *) (mgr + SAVE_PLAYERS_N);
        if (nr < 0 || nr > TWINS_MAX * 2) nr = 0;
        for (int r = 0; r < nr; r++) {
            const unsigned char *rec = (const unsigned char *) (mgr + SAVE_PLAYER0 + (unsigned long) r * SAVE_PLAYER_SIZE);
            if (!shim_eid_readable(rec, SAVE_PLAYER_SIZE)) { nr = 0; break; }
            rtype[r] = *(const int *) rec;
            rsub[r] = *(const int *) (rec + REC_SUB);
            rdev[r] = *(const int *) (rec + REC_DEVICE);
        }
    }
    /* what each record's device was: the newest lines that are these records */
    char what[TWINS_MAX * 2][SEAT_WHAT];
    int from_lines = 0;
    for (int r = 0; r < nr; r++) what[r][0] = 0;
    pthread_mutex_lock(&g_seats_lock);
    long best = 0;
    for (int k = 0; k < SEAT_RUNS && nr > 0; k++) {
        char w[TWINS_MAX * 2][SEAT_WHAT];
        if (g_seats_runs[k].seq > best && g_seats_runs[k].text[0]
            && seats_lines_match(g_seats_runs[k].text, rtype, rsub, nr, w)) {
            best = g_seats_runs[k].seq;
            memcpy(what, w, sizeof(w[0]) * (size_t) nr);
            from_lines = 1;
        }
    }
    pthread_mutex_unlock(&g_seats_lock);
    if (!from_lines)
        for (int r = 0; r < nr; r++)
            if (rdev[r] != -1 && !shim_device_identity(rdev[r], what[r], SEAT_WHAT) && (unsigned) rdev[r] == TOUCH_DEVICE)
                snprintf(what[r], SEAT_WHAT, "screen");
    /* a player's own record is the one whose number he sits on now (0x525308); one number, one meaning */
    char want[TWINS_MAX][SEAT_WHAT];
    for (int i = 0; i < t.n; i++) {
        want[i][0] = 0;
        if (!own[i] || t.dev[i] == NO_DEVICE) continue;
        int clash = 0;
        for (int r = 0; r < nr; r++) {
            if (rsub[r] || (unsigned) rdev[r] != t.dev[i] || !what[r][0]) continue;
            if (want[i][0] && strcmp(want[i], what[r])) clash = 1;
            snprintf(want[i], SEAT_WHAT, "%s", what[r]);
        }
        if (clash) want[i][0] = 0;
    }
    /* first come first served: every own player whose device is here */
    int final[TWINS_MAX], taken[TWINS_MAX * 2], nt = 0;
    for (int i = 0; i < t.n; i++) {
        final[i] = -2;
        if (!own[i] || !want[i][0]) continue;
        int d = shim_device_of_identity(want[i]), dup = 0;
        for (int k = 0; k < nt; k++) if (taken[k] == d) dup = 1;
        if (d != -1 && !dup) { final[i] = d; taken[nt++] = d; }
    }
    /* the rest as 0x56b0a0 hands them: the menu's device, then the used ones in the list's order */
    int queue[DEVICES_MAX + 1], nq = 0;
    int menu = mgr && shim_eid_readable((const void *) (mgr + MENU_DEVICE), 4) ? *(const int *) (mgr + MENU_DEVICE) : -1;
    if (menu != -1) queue[nq++] = menu;
    int nd = ((int (*)(unsigned long)) (b + FN_DEVICE_COUNT))(input);
    for (int j = 0; j < nd && j < DEVICES_MAX; j++) {
        int id = ((int (*)(unsigned long, int)) (b + FN_DEVICE_AT))(input, j);
        if (id == -1 || id == menu || !(((int (*)(unsigned long, int)) (b + FN_DEVICE_USED))(input, id) & 1)) continue;
        queue[nq++] = id;
    }
    int qi = 0;
    for (int i = 0; i < t.n; i++) {
        if (!own[i] || final[i] != -2) continue;
        int d = -1;
        while (qi < nq && d == -1) {
            int c = queue[qi++], dup = !(((int (*)(unsigned long, int)) (b + FN_DEVICE_ALIVE))(input, c) & 1);
            for (int k = 0; k < nt && !dup; k++) if (taken[k] == c) dup = 1;
            if (!dup) d = c;
        }
        final[i] = d;
        if (d != -1) taken[nt++] = d;
    }
    /* the game's seats: everybody off (0x56b0a0's first loop), then each player of his own with his twins */
    for (int i = 0; i < t.n; i++) ((void (*)(unsigned long, unsigned, int)) (b + FN_SEAT))(t.p[i], NO_DEVICE, 0);
    for (int i = 0; i < t.n; i++)
        if (own[i] && final[i] != -1) ((void (*)(unsigned long, unsigned, int)) (b + FN_SEAT))(t.p[i], (unsigned) final[i], 1);
    ((void (*)(unsigned long, int)) (b + FN_SLOTS_RESET))(pm, 1);
#if ISAAC_DIAG
    if (shim_report_active()) {
        char line[800];
        int at = snprintf(line, sizeof(line), "continue: hand-out ours (%s, menu device %#x):",
                          from_lines ? "the lines beside the save" : "no lines: this process's numbers", (unsigned) menu);
        for (int i = 0; i < t.n && at < (int) sizeof(line) - 80; i++) {
            if (!own[i]) continue;
            at += snprintf(line + at, sizeof(line) - (size_t) at, " [%d type %d record dev %#x was %s -> %#x]", i,
                           t.type[i], t.dev[i], want[i][0] ? want[i] : "?", (unsigned) final[i]);
        }
        report_line("coop", line);
    }
#endif
}

/* the shared trampoline's second caller (loader.c coop_handout_ours): 0x88b9f0, x0 = the player manager */
void shim_coop_continue_handout(unsigned long player_manager) {
    unsigned long b = shim_image_base();
    shim_devices_refresh();                         /* the book of devices as it is now */
    continue_handout(b, player_manager);
}

/*
 * The run restored: its line-up changed with every player on -1 - the mark
 * of 0x56b0a0 right after a Continue's 0x56959c - or it appeared from no
 * players with others than the last run's (a boss room's moment of no players
 * brings the same ones back, and is no restore).
 */
static void twins_watch_restore(unsigned long b, unsigned long game) {
    static unsigned long was[TWINS_MAX], last[TWINS_MAX];
    static int was_n, last_n;
    unsigned long p[TWINS_MAX];
    int n = game ? shim_eid_players(p, TWINS_MAX) : 0;
    if (n <= 0) { was_n = 0; return; }
    int lineup = n != was_n;
    for (int i = 0; i < n && !lineup; i++) if (p[i] != was[i]) lineup = 1;
    if (!lineup) return;
    int from_none = was_n == 0, others = n != last_n;
    for (int i = 0; i < n && !others; i++) if (p[i] != last[i]) others = 1;
    was_n = last_n = n;
    memcpy(was, p, sizeof(unsigned long) * (size_t) n);
    memcpy(last, p, sizeof(unsigned long) * (size_t) n);
    int all_nowhere = 1;
    for (int i = 0; i < n; i++)
        if (!p[i] || !shim_eid_readable((const void *) (p[i] + PLAYER_DEVICE), 4)
            || *(const unsigned *) (p[i] + PLAYER_DEVICE) != PLAYER_NOWHERE) all_nowhere = 0;
    if (all_nowhere) twins_after_restore(b, game, "everybody on -1");
    else if (from_none && others) twins_after_restore(b, game, "a run appeared");
}

#if ISAAC_DIAG
/*
 * SELF-CHECK twins, whenever the players, their types or their links change:
 * every link both ways, between a pair, on no child, no main alone beside a
 * free partner. A player whose type changes is named with his twin before and
 * after - the type change 0x510268 makes a new Esau (0x5107a8) or soul
 * (0x510f9c) even for a main who has one, and that is the game's own.
 */
static void twins_check(void) {
    static unsigned long was_p[TWINS_MAX];
    static int was_type[TWINS_MAX], was_twin[TWINS_MAX], was_parent[TWINS_MAX], was_n = -1;
    if (!shim_report_active()) return;
    twins_t t;
    if (!twins_read(&t)) { if (shim_coop_players() == 0) was_n = -1; return; }
    int changed = t.n != was_n;
    for (int i = 0; i < t.n && !changed; i++)
        if (t.p[i] != was_p[i] || t.type[i] != was_type[i] || t.twin[i] != was_twin[i] || t.parent[i] != was_parent[i])
            changed = 1;
    if (!changed) return;
    char line[700];
    for (int i = 0; i < t.n && was_n > 0; i++)
        for (int k = 0; k < was_n; k++)
            if (was_p[k] == t.p[i] && was_type[k] != t.type[i] && (twin_kind(t.type[i]) || twin_kind(was_type[k]))) {
                snprintf(line, sizeof(line), "twins: player %d type %d -> %d, twin %d -> %d, players %d -> %d"
                         " (a type change in the run: 0x510268 makes a twin even for a main who has one)",
                         i, was_type[k], t.type[i], was_twin[k], t.twin[i], was_n, t.n);
                check_say(line);
            }
    was_n = t.n;
    memcpy(was_p, t.p, sizeof(unsigned long) * (size_t) t.n);
    memcpy(was_type, t.type, sizeof(int) * (size_t) t.n);
    memcpy(was_twin, t.twin, sizeof(int) * (size_t) t.n);
    memcpy(was_parent, t.parent, sizeof(int) * (size_t) t.n);
    int any = 0;
    for (int i = 0; i < t.n; i++) if (twin_kind(t.type[i])) any = 1;
    if (!any) return;                               /* nobody with a twin: nothing to say */
    const char *why = "";
    int bad = 0;
    for (int kind = 1; kind <= 3 && !bad; kind++) bad = twins_spoilt(&t, kind, &why) != 0;
    int at = snprintf(line, sizeof(line), "SELF-CHECK twins: %s%s;", bad ? "FAIL " : "OK", bad ? why : "");
    twins_line(&t, line + at, (int) sizeof(line) - at);
    check_say(line);
}
#endif

void shim_coop_between_frames(void) {
    unsigned long b = shim_image_base();
    if (!b) return;
    unsigned long game = *(const unsigned long *) (b + GAME_PTR);
    shim_devices_between_frames();              /* the book of devices first: everything below asks it */
    twins_watch_restore(b, game);               /* before anyone seats anybody this frame */
    if (game) {
        orphans_take_their_own(b);
        seats_memory(b);
    }
#if ISAAC_DIAG
    if (game) twins_check();
#endif
#if ISAAC_DIAG
    if (game && shim_report_active()) {
        seat_journal(b);                        /* before our own writers of this frame */
        reselect_watch(b);
        zero_step(b);
        own_check(b, game);
    }
#endif
    if (game) hud_entries_follow_devices(b, game);
    if (game) chosen_follows_player0(b);
    if (game) handler_gates(b, game);
    wants_between_frames();
    watch_the_choosers(b, game);
    if (game) {
        touch_wants_player0(b);
        key_wants_player0(b);
    } else {
        g_touch_edge = 0;
        g_key_edge = 0;
    }
    if (!shim_report_active() || g_said >= LINES_MAX) return;
    players(b, game);
    actions(b);
    if (game) slots(game);
}
