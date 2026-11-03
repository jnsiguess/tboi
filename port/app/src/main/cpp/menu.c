/*
 * Options menu: the order of the rows and the rows we add ourselves.
 *
 * How the menu is built, as taken apart from the image. A row is an animation
 * in gfx/ui/main menu/optionsmenu.anm2, one per row, and the captions are
 * drawn in OptionsMenu.png and OptionsExtra.png. What lands on screen is
 * decided by an array of row numbers kept in the menu object at offset 0x2f8;
 * every number sends the code down its own branch of a jump table, and there
 * are twenty of them (0..0x13).
 *
 * So reordering the rows means reordering numbers in an array, not touching
 * code at all.
 */
#include <stdio.h>
#include <string.h>
#include <android/log.h>
#include "diag.h"

#if ISAAC_DIAG
#define LOG(...) __android_log_print(ANDROID_LOG_INFO, "isaac", __VA_ARGS__)
#else
#define LOG(...) do { } while (0)      /* the build we play on has no log */
#endif

/*
 * The branch names come from the image: the jump table sends each row number
 * to a branch that sets an animation name. The order here is the order of the
 * branches.
 */
static const char *row_name(int n) {
    static const char *names[] = {
        "Controls", "ChangeController", "SFX", "Music", "MapOpacity",
        "PenOpacity", "Fullscreen", "Filter", "Gamma", "Popups",
        "Language", "ActiveCam", "ExtraHUD", "HUDOffset", "FoundHUD",
        "Rumble", "ChargeBars", "BulletVisibility", "AimLock", "TouchMode",
    };
    return (n >= 0 && n < (int) (sizeof(names) / sizeof(names[0]))) ? names[n] : "?";
}

void shim_sticks_menu_sync(unsigned long);

unsigned long shim_image_base(void);
int shim_report_active(void);
void report_line(const char *kind, const char *text);

/*
 * The achievement store, shared by both of the things below: one byte per
 * achievement at root + 0x28, the root being the persistent block at 0x204d0
 * inside the object the game keeps at 0xac2428, and 0x7027f8 the engine's own
 * test of it.
 */
#define ACHIEVEMENT_HAS  0x7027f8
#define ACHIEVEMENT_ROOT 0xac2428
#define ACHIEVEMENT_IN   0x204d0
#define GREEDIER 0x155

/* ------------------------------------------------ the daily-run trophies */

/*
 * Three achievements that cannot be earned in this port, granted in whatever
 * save is loaded.
 *
 * The daily run is switched off here - it needs a leaderboard service the port
 * has no way to speak to - and three achievements hang off it: The Marathon
 * (a five-win streak), Dedication (thirty-one dailies) and Broken Modem (seven
 * dailies completed). Each unlocks an item, so leaving them shut would keep
 * three items out of the game for good, through no choice of the player's.
 *
 * Where they live. The engine keeps one byte per achievement, indexed by the
 * achievement's own id, at root + 0x28 - that is exactly what its test does
 * (0x7027f8: `x8 = root + id; ldrb w8, [x8, #0x28]`, and anything non-zero is
 * "earned"). The root is the persistent block at 0x204d0 inside the object the
 * game keeps at 0xac2428, and it is the SAME block whichever save slot is
 * loaded - the file is read into it - so a check against it is a check against
 * the current slot, and nothing has to know about slots at all.
 *
 * The ids are the ids in achievements.xml, and that mapping is not a guess:
 * the file holds 638 entries and the engine's test refuses anything above 637,
 * so the file's numbering IS the internal one.
 *
 * Asked once, when a save has just been read - that is the only moment the
 * answer can change. Three byte comparisons then, and nothing at all for the
 * rest of the run. Silent: the byte is raised on its own, without the
 * engine's unlock, so no paper flies across the screen.
 */
#define ACHIEVEMENT_FLAGS 0x28      /* one byte per achievement, id is the index */
#define DAILY_TROPHIES 3
static const int DAILY_ACHIEVEMENT[DAILY_TROPHIES] = {
    336,        /* The Marathon  - a five-win streak in the dailies */
    325,        /* Dedication    - thirty-one dailies */
    354,        /* Broken Modem  - seven dailies completed */
};

void shim_menu_daily_trophies(void) {
    unsigned long b = shim_image_base();
    if (!b) return;
    const unsigned long *holder = (const unsigned long *) (b + ACHIEVEMENT_ROOT);
    if (!*holder) return;
    unsigned char *earned =
        (unsigned char *) (*holder + ACHIEVEMENT_IN + ACHIEVEMENT_FLAGS);
    for (int i = 0; i < DAILY_TROPHIES; i++) {
        int id = DAILY_ACHIEVEMENT[i];
        if (earned[id]) continue;
        earned[id] = 1;
        LOG("daily: trophy %d granted — it cannot be earned with the daily run off", id);
    }
}

/* ------------------------------------------- the difficulty on the character screen */

/*
 * A tap on a difficulty chooses THAT difficulty.
 *
 * The widget is drawn as a list - Normal, Hard, Greed, Greedier, one under the
 * other, with a cursor beside the chosen one - and on a gamepad it is a list:
 * up and down walk it. A finger has no up and down, so the game gave it the
 * only thing the screen understood, and a tap anywhere on the block stepped to
 * the next one. Reading a list and being unable to point at a line of it is
 * the part testers kept stumbling over: tapping "hard" ought to mean hard, not
 * "one along from wherever we are".
 *
 * This is a departure from the original, asked for and agreed.
 *
 * What the engine already has. Every change of difficulty on this screen goes
 * through ONE store - 0x5e7db8, `str w8, [x19, #0x38]` - and the field it
 * writes is also the widget's frame number, which is why the picture and the
 * value can never disagree. The store is followed by the step's own tail: the
 * menu sound, and 0x703660(manager + 0x204d0, 0, value), which KEEPS the value
 * in the settings store. The only other write of the field is the load of the
 * kept value when the screen opens (0x5e5b3c).
 *
 * So the port does not touch the stepping at all. When the screen has just
 * stepped for a finger, the K hook - the word after the store - puts the row
 * the finger is on in place of the row the stepping arrived at, and the
 * game's own tail sounds it and keeps it. A key or a pad walks the list
 * exactly as before.
 *
 * The rows are where difficultywidget.anm2 puts the cursor: 23, 43, 60 and 78
 * units down from the widget's own point. Nearest wins, so there are no gaps
 * between the lines and no dead band to miss.
 */
#define SCREEN_DIFFICULTY 0x38      /* the chosen difficulty, and the widget's frame */
#define SCREEN_LEAVING    0x30      /* non-zero once the screen is on its way out (a run starts: 3) */
#define WIDGET_ROWS 4
static const float ROW_AT[WIDGET_ROWS] = {23.0f, 43.0f, 60.0f, 78.0f};

/* how far around the rows a finger still counts as being on the widget */
#define REACH_LEFT   48.0f
#define REACH_RIGHT 128.0f
#define REACH_UNDER  16.0f

/*
 * The finger list, gathered exactly as the engine's own widget test (0x862e0c)
 * gathers it: a buffer of records, the count behind them, and a converter that
 * puts a finger into the coordinates the menu is laid out in - the same ones
 * the widget's drawing point arrives in, which is what makes the subtraction
 * below mean anything. The converter is the game's own proportion against the
 * window (0x86255c), the one its own tap zones on this screen go through
 * (0x8634e0 -> 0x862674): with FULLSCREEN off the finger lands where the
 * game's zone thinks it does, which is the only space that matters here.
 */
#define FINGERS_BEGIN 0x8a7618
#define FINGERS_TAKE  0x8b6b5c
#define FINGER_LIVE   0x8a7580
#define FINGER_POINT  0x86255c
#define INPUT_ROOT    0xac5528
#define FINGER_FLAGS  0xCDCD0000u
#define FINGER_STRIDE 0x50
#define FINGER_COUNT  0x500
#define FINGER_BYTES  0x520
#define FINGER_MAX    16

typedef struct { float x, y; } point_t;

static unsigned long g_screen;      /* the character-select screen, from its input */
static float g_at_x, g_at_y;        /* where the widget was last drawn */
static int g_placed;

static int rows_offered(unsigned long b) {
    int (*has)(unsigned long, int) = (int (*)(unsigned long, int)) (b + ACHIEVEMENT_HAS);
    const unsigned long *root = (const unsigned long *) (b + ACHIEVEMENT_ROOT);
    if (!*root) return WIDGET_ROWS - 1;
    return has(*root + ACHIEVEMENT_IN, GREEDIER) ? WIDGET_ROWS : WIDGET_ROWS - 1;
}

/* the live fingers as the game's widgets see them; answers how many were written */
static int fingers_now(unsigned long b, point_t *out, int room) {
    void (*begin)(void *) = (void (*)(void *)) (b + FINGERS_BEGIN);
    void (*take)(unsigned long, void *, unsigned, unsigned) =
        (void (*)(unsigned long, void *, unsigned, unsigned)) (b + FINGERS_TAKE);
    int (*live)(void *) = (int (*)(void *)) (b + FINGER_LIVE);
    point_t (*where)(void *) = (point_t (*)(void *)) (b + FINGER_POINT);
    unsigned char fingers[FINGER_BYTES];
    begin(fingers);
    take(b + INPUT_ROOT, fingers, FINGER_FLAGS, 0);
    int n = *(const int *) (fingers + FINGER_COUNT), got = 0;
    if (n < 0 || n > FINGER_MAX) return 0;
    for (int i = 0; i < n && got < room; i++) {
        void *f = fingers + (size_t) i * FINGER_STRIDE;
        if (live(f)) out[got++] = where(f);
    }
    return got;
}

/*
 * THE GAME'S OWN TAP ZONE OF THE LIST, WIDENED TO ITS LAST ROW (K8, 01.10.2026).
 *
 * Where the step comes from, found this time: the screen's input handler
 * first calls 0x8634e0 (at 0x5e78d4), and that walks a table of three touch
 * zones at 0xb14c58 - {action, x0, y0, x1, y1} - and for a live finger inside
 * one it sets the action's analogue value on the touch device (0x8b6960 ->
 * DeviceiOS +0x80 = 0x8a8398, [dev+0xd0][action] = 1). The first zone is the
 * difficulty list: action 0x1e (DOWN), x 355.04..443.90, y 116..180 in the
 * screen's frame, the widget being drawn at (365, 120). 180 is 60 below the
 * widget's point - the third row. That is why, measured on the phone, every tap
 * at 23..60 units down stepped the list and every tap at 71..86 did nothing:
 * the game's zone was made for the three-row list and never grew with the
 * fourth. The table is filled once by a static initialiser (0x865a60, in the
 * image's init list) and read by nothing but 0x8634e0.
 *
 * So the zone's lower edge follows the list that is on screen: under the last
 * offered row, as far as the rows' own reach. A tap on Greedier is then the
 * game's own step, K puts row 3 in its place, and the game's own tail sounds it
 * and keeps it. There is no second writer of the kept difficulty any more:
 * fourth_row_tapped and keep_difficulty, which answered the fourth row by
 * themselves - for ANY finger near a possibly stale anchor, a finger left over
 * from the screen before, a palm - are gone, and with them the one road by
 * which a 3 could be kept that nobody chose.
 *
 * Every number is checked against what the initialiser writes before anything
 * is changed, and the widget's own y is checked in its instruction (0x5e86b8,
 * `mov w8, #0x42f00000`, 120.0); anything else and the zone is left alone.
 */
#define ZONE_TABLE      0xb14c58
#define ZONE_ACTION_DOWN 0x1e
#define WIDGET_Y_WORD   0x5e86b8
#define WIDGET_Y_MOV    0x52a85e08u         /* mov w8, #0x42f00000 */
#define WIDGET_Y        120.0f

static int zone_fit(unsigned long b, int rows) {
    static int refused;
    if (refused) return 0;
    unsigned *z = (unsigned *) (b + ZONE_TABLE);
    float want = WIDGET_Y + ROW_AT[rows - 1] + REACH_UNDER;
    unsigned want_bits, bits3, bits4;
    float r3 = WIDGET_Y + ROW_AT[2] + REACH_UNDER, r4 = WIDGET_Y + ROW_AT[3] + REACH_UNDER;
    memcpy(&want_bits, &want, 4); memcpy(&bits3, &r3, 4); memcpy(&bits4, &r4, 4);
    if (z[4] == want_bits) return 1;
    /* what 0x865a60 writes: 0x1e, [0x9472fc], 116.0, [0x947300], 180.0 - or our own edge */
    int ours = z[4] == 0x43340000u || z[4] == bits3 || z[4] == bits4;
    if (z[0] != ZONE_ACTION_DOWN || z[1] != *(const unsigned *) (b + 0x9472fc) || z[2] != 0x42e80000u
        || z[3] != *(const unsigned *) (b + 0x947300) || !ours
        || *(const unsigned *) (b + WIDGET_Y_WORD) != WIDGET_Y_MOV) {
        refused = 1;
#if ISAAC_DIAG
        if (shim_report_active()) {
            char line[200];
            snprintf(line, sizeof(line), "zone: not the table 0x865a60 writes (%x %x %x %x %x), left alone",
                     z[0], z[1], z[2], z[3], z[4]);
            report_line("difficulty", line);
        }
#endif
        return 0;
    }
#if ISAAC_DIAG
    unsigned was = z[4];
#endif
    z[4] = want_bits;
#if ISAAC_DIAG
    if (shim_report_active()) {
        static int said;
        if (said < 6) {
            said++;
            float f;
            memcpy(&f, &was, 4);
            char line[160];
            snprintf(line, sizeof(line), "zone: the game's DOWN zone of the list ends at %.0f, was %.0f (%d rows)",
                     (double) want, (double) f, rows);
            report_line("difficulty", line);
        }
    }
#endif
    return 1;
}

/*
 * WHOSE STEP WAS IT. K only corrects a step the FINGER made, and only a finger
 * that came down on this screen after the widget had come to rest.
 *
 * The device: the step reads actions 0x1d/0x1e through every device (0x8b6600
 * with device -1), so at K each device is asked the same question the step
 * asked - its own "triggered" (+0x50, 0x8a8098, a read) - and the step is the
 * finger's when the touch device (DeviceiOS, vtable 0xa92d88) says yes and no
 * keyboard or pad does. An arrow key with a palm on the glass, or a pad with a
 * finger resting on the widget, no longer has its step turned into the row
 * under the finger.
 *
 * The finger: the anchor (the widget's drawing point) must have stood still
 * for SETTLE_FRAMES draws - it slides in with the screen - and a finger must
 * have gone down after that (the touch layer's count of fingers put down,
 * uikit.c). A finger that was already down when the anchor settled - left on
 * the glass from the screen before, a palm - makes the finger's steps not
 * count until every finger has been lifted.
 */
#define DEVICE_IOS_VTABLE 0xa92d88
#define KEYBOARD_VTABLE   0xa94958
#define DEVICE_TRIGGERED  0x50            /* virtual: the action went down this frame */
#define SETTLE_FRAMES     10

unsigned shim_touch_downs(void);
int shim_touch_any_down(void);
unsigned shim_keys_batch(void);

static int g_still;                     /* draws the anchor has not moved */
static unsigned g_downs_settled;        /* fingers put down, counted when it settled */
static int g_stale;                     /* a finger was down when it settled and still is */
static unsigned g_door_at;              /* the frame the input handler last ran */
static int g_door_value;                /* the field at the handler's door, this frame */
static int g_entered;                   /* the screen has just (re)opened this frame */

static int finger_fresh(void) {
    return g_still >= SETTLE_FRAMES && !g_stale && shim_touch_downs() != g_downs_settled;
}

enum { BY_NONE = 0, BY_TOUCH = 1, BY_KEYBOARD = 2, BY_OTHER = 4 };

/* which kinds of device say the action went down this frame */
static int action_by(unsigned long b, unsigned action) {
    const unsigned long *root = (const unsigned long *) (b + INPUT_ROOT);
    unsigned long begin = root[1], end = root[2];
    if (!begin || end < begin || end - begin > 16 * 8) return BY_NONE;
    int by = BY_NONE;
    for (unsigned long p = begin; p < end; p += 8) {
        unsigned long dev = *(const unsigned long *) p;
        if (!dev) continue;
        const unsigned long *vt = *(const unsigned long **) dev;
        if ((unsigned long) vt < b || (unsigned long) vt > b + 0xb20000) continue;
        int (*triggered)(unsigned long, unsigned) = (int (*)(unsigned long, unsigned)) vt[DEVICE_TRIGGERED / 8];
        if (!triggered(dev, action)) continue;
        if ((unsigned long) vt == b + DEVICE_IOS_VTABLE) by |= BY_TOUCH;
        else if ((unsigned long) vt == b + KEYBOARD_VTABLE) by |= BY_KEYBOARD;
        else by |= BY_OTHER;
    }
    return by;
}

#if ISAAC_DIAG
static const char *by_name(int by) {
    static const char *NAMES[8] = { "nobody", "touch", "keyboard", "touch+keyboard", "a pad",
                                    "touch+pad", "keyboard+pad", "touch+keyboard+pad" };
    return NAMES[by & 7];
}
#endif

/*
 * The KEPT difficulty: 0x703660(manager+0x204d0, 0, value) writes
 * store[0xe8c + slot*4] and raises the store's dirty byte; slot zero, so the
 * number lives at manager + 0x204d0 + 0xe8c. The manager itself is the pointer
 * at 0xac2428. Read for the report only - nothing here writes it.
 *
 * The game holds the difficulty as two bits - bit 1 is greed, bit 0 is hard,
 * and its own test for greed is (d & ~1) == 2 - so Greedier is 3, both bits.
 */
#define MANAGER_PTR      0xac2428
#define DIFFICULTY_KEPT  0x2135c
#define DIFFICULTY_RUN   0x1f6b38     /* the run's difficulty, copied from the field on confirm (0x5e791c) */

#if ISAAC_DIAG
static int difficulty_kept(unsigned long b) {
    unsigned long m = *(const unsigned long *) (b + MANAGER_PTR);
    return m ? *(const int *) (m + DIFFICULTY_KEPT) : -1;
}
#endif

/* ---------- the report (difficulty:), every change of the field with its writer ---------- */
#if ISAAC_DIAG
void shim_keys_recent(char *out, unsigned long room);
void shim_ime_recent(char *out, unsigned long room);

static int g_diff_said;
#define DIFF_LINES_MAX 60
static void diff_say(const char *line) {
    if (!shim_report_active() || g_diff_said >= DIFF_LINES_MAX) return;
    g_diff_said++;
    report_line("difficulty", line);
}

/*
 * SELECT! OVER THE GREEDIER ROW (480x270) - measured, not open: screen.c
 * lowers both corner papers on a narrow, low canvas. The menu manager draws its
 * BACK/SELECT paper (0x769aa4, gfx/ui/BackSelectWidget.anm2) in the canvas's
 * corner and taps it by its sprite (0x7689f8 -> 0x862e0c, action 14, at
 * W-56, H-75); the widget hangs off the character's paper. Where they meet on
 * this canvas is written down once, so the owner's question has the numbers.
 */
static void report_select_overlap(unsigned long b) {
    static float told_w, told_h;
    float w = *(const float *) (b + 0xac24d8), h = *(const float *) (b + 0xac24dc);
    if (!g_placed || (w == told_w && h == told_h)) return;
    told_w = w; told_h = h;
    /* where it really stands: screen.c lowers both papers by 20 on a narrow, low canvas */
    int shim_screen_papers_lowered(void);
    float sel_x = w - 56.0f, sel_y = h - 58.5f + (shim_screen_papers_lowered() ? 20.0f : 0.0f);
    float row_top = g_at_y + (ROW_AT[2] + ROW_AT[3]) * 0.5f, row_bottom = g_at_y + ROW_AT[3] + REACH_UNDER;
    float row_right = g_at_x + REACH_RIGHT;
    int overlap = sel_x - 16.0f < row_right && sel_y - 16.0f < row_bottom;
    char line[220];
    snprintf(line, sizeof(line), "select: canvas %.0fx%.0f, SELECT paper at %.1f,%.1f; the Greedier row %.1f..%.1f x %.1f..%.1f: %s",
             (double) w, (double) h, (double) sel_x, (double) sel_y, (double) (g_at_x - REACH_LEFT),
             (double) row_right, (double) row_top, (double) row_bottom, overlap ? "OVERLAP" : "apart");
    diff_say(line);
}
#endif

/* the screen whose difficulty this is, taken at its input handler's door ('D') */
void shim_menu_difficulty_screen(unsigned long screen) {
    unsigned long b = shim_image_base();
    unsigned now = shim_keys_batch();
    /* a new screen object, or the same one after a gap of frames: the screen has (re)opened */
    g_entered = screen != g_screen || now - g_door_at > 2;
    if (g_entered) { g_screen = screen; g_placed = 0; g_still = 0; g_stale = 0; }
    g_door_at = now;
    if (!b || !screen) return;
    int rows = rows_offered(b);
    zone_fit(b, rows);
    if (g_still == SETTLE_FRAMES) {
        g_downs_settled = shim_touch_downs();
        g_stale = shim_touch_any_down();
    } else if (g_stale && !shim_touch_any_down()) {
        g_stale = 0;
    }
    int value = *(const int *) (screen + SCREEN_DIFFICULTY);
#if ISAAC_DIAG
    {
        static int last_leaving;
        int kept = difficulty_kept(b);
        int leaving = *(const int *) (screen + SCREEN_LEAVING);
        char line[220];
        if (g_entered) {
            snprintf(line, sizeof(line), "entered: kept %d, shown %d, rows %d", kept, value, rows);
            diff_say(line);
        } else if (value != g_door_value) {
            /* not a step (K saw every step and left g_door_value as it left the field) */
            snprintf(line, sizeof(line), "field changed by UNKNOWN between frames: %d -> %d (kept %d)",
                     g_door_value, value, kept);
            diff_say(line);
        }
        if (leaving && !last_leaving) {
            unsigned long m = *(const unsigned long *) (b + MANAGER_PTR);
            snprintf(line, sizeof(line), "leaving (state %d): the run starts with difficulty %d, shown %d, kept %d",
                     leaving, m ? *(const int *) (m + DIFFICULTY_RUN) : -1, value, kept);
            diff_say(line);
        }
        last_leaving = leaving;
        report_select_overlap(b);
    }
#endif
    g_door_value = value;
}

/*
 * The widget's own point, taken where it is drawn ('F'). Only the point.
 *
 * Asking about the FINGER here was the first attempt, and the measurement
 * killed it: the screen handles its input before it draws, so at the moment
 * the choice is made the drawing has not run yet this frame, and the answer
 * left over from the last frame is stale. The point itself is safe to carry
 * over a frame, and whether it has stopped moving is counted here.
 */
void shim_menu_difficulty_drawn(float x, float y) {
    if (g_placed && x == g_at_x && y == g_at_y) { if (g_still < SETTLE_FRAMES + 1) g_still++; }
    else g_still = 0;
    g_at_x = x; g_at_y = y; g_placed = 1;
}

/*
 * The screen has just changed the difficulty ('K', the word after the store).
 * If the step was a fresh finger's and the finger is on a row, that row is
 * what was meant.
 */
void shim_menu_difficulty_chosen(void) {
    unsigned long b = shim_image_base();
    if (!b || !g_screen) return;
    int *chosen = (int *) (g_screen + SCREEN_DIFFICULTY);
    int stepped = *chosen;
    int by = action_by(b, 0x1d) | action_by(b, 0x1e);
    int touch = by == BY_TOUCH, fresh = finger_fresh();
    int rows = rows_offered(b), best = -1;
    point_t fingers[FINGER_MAX];
    int n = (touch && fresh && g_placed) ? fingers_now(b, fingers, FINGER_MAX) : 0;
    float at = 0.0f;
    for (int i = 0; i < n && best < 0; i++) {
        float lx = fingers[i].x - g_at_x, ly = fingers[i].y - g_at_y;
        if (lx < -REACH_LEFT || lx > REACH_RIGHT) continue;
        if (ly > ROW_AT[rows - 1] + REACH_UNDER) continue;
        best = 0;
        for (int r = 1; r < rows; r++) {
            float d = ly - ROW_AT[r], keep = ly - ROW_AT[best];
            if (d < 0.0f) d = -d;
            if (keep < 0.0f) keep = -keep;
            if (d < keep) best = r;
        }
        at = ly;
    }
    if (best >= 0) *chosen = best;
#if !ISAAC_DIAG
    (void) stepped; (void) at;
#else
    {
        char keys[120], ime[16], what[120], line[440];
        shim_keys_recent(keys, sizeof(keys));
        shim_ime_recent(ime, sizeof(ime));
        point_t any[FINGER_MAX];
        int live = g_placed ? fingers_now(b, any, FINGER_MAX) : 0;
        /* the finger in the widget's units, by the game's own converter (0x86255c, the picture's units) */
        if (best >= 0) snprintf(what, sizeof(what), "K: the finger %.1f down -> row %d", (double) at, best);
        else if (live) snprintf(what, sizeof(what), "K left the step alone (a finger at %.1f,%.1f of the widget)",
                                (double) (any[0].x - g_at_x), (double) (any[0].y - g_at_y));
        else snprintf(what, sizeof(what), "K left the step alone");
        snprintf(line, sizeof(line),
                 "step %d -> %d by %s (0x1d/0x1e), confirm 14 by %s; fingers %d, fresh %d (anchor still %d, stale %d);"
                 " %s; keys: %s; ime: %s",
                 g_door_value, stepped, by_name(by), by_name(action_by(b, 14)), live, fresh, g_still, g_stale,
                 what, keys, ime);
        diff_say(line);
    }
#endif
    /* the next frame's door compares against what this frame left */
    g_door_value = *chosen;
}

/*
 * Two measuring hands of the time the widget was being taken apart, kept for
 * their marks in trace.c ('U' 0x8a8098, 'T' 0x8ddc9c; neither is placed now -
 * loader.c says why). 0x8ddc9c is the KEYBOARD device's set (0x8ca88c), not
 * the touch screen's: vtable 0xa94958 is the keyboard, id 0; the touch screen
 * is DeviceiOS 0xa92d88, id 0x10dead.
 */
void shim_menu_action_asked(unsigned long dev, unsigned long action) {
    (void) dev; (void) action;
}

void shim_menu_touch_button(const char *what, unsigned long button, unsigned long back) {
#if !ISAAC_DIAG
    (void) what; (void) button; (void) back;
#else
    static int said;
    if (said >= 40 || !shim_report_active()) return;
    unsigned long b = shim_image_base();
    if (button > 0x400) return;
    said++;
    char line[120];
    snprintf(line, sizeof(line), "%s: keyboard key %lu from %lx", what, button,
             (b && back > b) ? back - b : back);
    report_line("keyboard", line);
#endif
}


/*
 * The daily-run sign on the main menu: how it is built and how it is removed.
 *
 * The sign is three layers of the screen-3 animation (gfx/ui/main menu/
 * gamemenu.anm2): 7 DailyPaper, 8 DailyText, 9 DailyTab. For every layer the
 * engine keeps its own "layer enabled" flag at offset 0x7c of the layer
 * record; the layer constructor (0x89cb4c) sets it to one, and the Visible
 * attributes from the file never touch it — measured: with Visible="false" on
 * both the LayerAnimation and every frame, the button still worked.
 *
 * Everything that makes the sign a button hangs off the flag of layer seven:
 *   drawing   0x89d648   a layer with zero is skipped entirely;
 *   hit area  0x7f9ee8   with zero it never calls 0x6732b4(root, device,
 *             action 0x21, 0, 1), which registers the touch rectangle;
 *   action    0x7f8f34   with zero action 0x21 no longer switches to screen 4.
 * Screen 4 is the daily-run menu; the only place in the whole image that hands
 * a four to the screen switch (0x769b88) is 0x7f8f44.
 *
 * We clear the flag on entry to the screen's update (0x7f8e34) and draw
 * (0x7f9da4); both receive the screen object in x0, and the animation sits at
 * +0x10. Every frame rather than once, because the engine can switch the layer
 * back on (0x89e640).
 *
 * The engine's own layer accessor (0x89f50c) logs a complaint for an index
 * past the counter and still returns an address — and gamemenu.anm2 for
 * Rebirth has no layers 7..9 at all. So we work out the address ourselves and
 * never step past the counter.
 */
/*
 * The three layers are found by NAME in the gamemenu.anm2 the game loads
 * (portpatch.c, plan 30.09 M8) - 7, 8 and 9 in the game's own file - so a
 * mod's file with another order is still read right, and one without them
 * has nothing to switch off.
 */
int shim_port_layer(const char *rel, const char *name);

void daily_sign_off(unsigned long screen) {
    static int daily[3] = {-2, -2, -2};
    if (!screen) return;
    if (daily[0] == -2) {
        static const char *const NAMES[3] = {"DailyPaper", "DailyText", "DailyTab"};
        for (int k = 0; k < 3; k++) daily[k] = shim_port_layer("gfx/ui/main menu/gamemenu.anm2", NAMES[k]);
    }
    const unsigned char *anim = (const unsigned char *) screen + 0x10;
    unsigned char *layers = *(unsigned char **) (anim + 0xa8);
    int count = *(const int *) (anim + 0xb0);
    if (!layers) return;
    for (int k = 0; k < 3; k++)
        if (daily[k] >= 0 && daily[k] < count) layers[daily[k] * 0xb0 + 0x7c] = 0;
}

void *shim_android_symbol(const char *);

/*
 * Inserting our own rows into the list.
 *
 * The order lives in an array of row numbers: start at +0x2f8, end at +0x300,
 * end of the reserve at +0x308. There is no reserve — the end equals the
 * boundary — so the new list gets memory from the same allocator the game
 * itself uses: the game will free this array later on its own.
 *
 * We deliberately do not free the old block: sixty-four bytes per opening of
 * the menu cost nothing, and this way a double free simply cannot happen.
 *
 * There are TWO lists, which only a measurement revealed: the main menu has
 * sixteen rows starting with Controls, the pause menu fifteen starting with
 * ChangeController. What they share is the second and third row, AimLock (18)
 * and TouchMode (19); we recognise a list by those so as not to walk into a
 * stranger. Ours go in right after them.
 *
 * Row 18 is FULLSCREEN now (loader.c), and it stands in BOTH lists. It was
 * taken out of the pause one for an afternoon, on the theory that a canvas
 * changed under a running room was what broke the room's geometry; the owner
 * then showed that switching from the main menu and walking back into the run
 * breaks it just the same. The room is not the thing that goes wrong, so
 * hiding the row only hid the switch. Kept here as a warning against fixing a
 * symptom twice.
 *
 * sticks.c still tells our rows apart by their NUMBERS through the list
 * rather than by their places, which is the honest way round either way.
 */
#define ROW_STICK_KIND 5
#define ROW_STICK_SIZE 6
#define ROW_FULLSCREEN 18

static void insert_our_rows(unsigned long self, int (*readable)(const void *, unsigned long)) {
    static void *(*allocate)(unsigned long);
    int **begin = (int **) (self + 0x2f8);
    int **end = (int **) (self + 0x300);
    int **limit = (int **) (self + 0x308);
    int *a = *begin, *b = *end;                           /* the object itself is checked */
    long n = b - a;
    if (n != 15 && n != 16) return;                       /* already done, or a foreign list */
    if (!readable(a, (unsigned long) n * sizeof(int))) return;
    if (a[1] != ROW_FULLSCREEN || a[2] != 19) return;     /* not the options menu */
    for (long i = 0; i < n; i++)
        if (a[i] == ROW_STICK_KIND || a[i] == ROW_STICK_SIZE) return;
    if (!allocate) allocate = (void *(*)(unsigned long)) shim_android_symbol("_Znwm");
    if (!allocate) { LOG("menu: allocator not found"); return; }
    long m = n + 2;
    int *fresh = (int *) allocate((unsigned long) m * sizeof(int));
    if (!fresh) return;
    memcpy(fresh, a, 3 * sizeof(int));                    /* header, FULLSCREEN, TouchMode */
    fresh[3] = ROW_STICK_KIND;
    fresh[4] = ROW_STICK_SIZE;
    memcpy(fresh + 5, a + 3, (size_t) (n - 3) * sizeof(int));
    *begin = fresh; *end = fresh + m; *limit = fresh + m;
    LOG("menu: our rows inserted, was %ld, now %ld", n, m);
}


void shim_menu_probe(unsigned long self, int (*readable)(const void *, unsigned long)) {
    /*
     * Called on EVERY draw of the list, so the memory check happens once per
     * object: pushing 784 bytes through a pipe sixty times a second is
     * pointless, and the menu object never moves during its lifetime.
     */
    static unsigned long checked;
    if (!self) return;
    if (self != checked) {
        if (!readable((const void *) self, 0x310)) return;
        checked = self;
    }
    shim_sticks_menu_sync(self);      /* values of our rows and handling of a tap */
    insert_our_rows(self, readable);

#if ISAAC_DIAG
    /* CONTROLS PROBE: the device index the menu keeps, and the first row */
    {
        static unsigned long last_menu;
        static int last_dev = -12345, last_row0 = -1;
        int dev = *(const int *) (self + 0x2f4);
        const int *const *b0 = (const int *const *) (self + 0x2f8);
        int row0 = (*b0) ? (*b0)[0] : -1;
        if (dev != last_dev || row0 != last_row0 || self != last_menu) {
            last_dev = dev; last_row0 = row0; last_menu = self;
            LOG("controls: menu %lx, device index %d, first row %d (%s)",
                self, dev, row0, row_name(row0));
        }
    }
#endif


    /*
     * The list is printed again whenever it is a DIFFERENT one. Printing once
     * per launch was not enough: the options menu inside a run is its own
     * object with its own set of rows, and until it had been seen the analysis
     * was running against the main menu's list.
     */
    const int *const *begin = (const int *const *) (self + 0x2f8);
    const int *const *end = (const int *const *) (self + 0x300);
    const int *a = *begin, *b = *end;
    long n = (b > a) ? (b - a) : 0;
#if !ISAAC_DIAG
    (void) a; (void) n;
    return;                      /* the rest is only printing — not for release */
#else
    static const int *shown_at;
    static long shown_count;
    if (a == shown_at && n == shown_count) return;
    shown_at = a; shown_count = n;
    LOG("menu: object %lx, array %p, rows %ld", self, (const void *) a, n);
    if (n <= 0 || n > 64 || !readable(a, (unsigned long) n * 4)) return;
    char line[240];
    int k = 0;
    for (long i = 0; i < n; i++) {
        k += snprintf(line + k, sizeof(line) - (size_t) k, " %ld:%s", i, row_name(a[i]));
        if (k > 170) { LOG("menu:%s", line); k = 0; line[0] = 0; }
    }
    if (k) LOG("menu:%s", line);
#endif
}
