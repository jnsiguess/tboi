/*
 * The pause inventory: everything the player is carrying, on a long press of
 * the map.
 *
 * The mod this port follows shows the same list on the map button held down;
 * its iOS build gives it a button of its own instead. The owner asked for the
 * gesture rather than the button, so it hangs on the map's own touch zone -
 * the one hud.c already knows, because the port had to move it to follow the
 * HUD offset.
 *
 * The press is NOT held back, and that is the whole point of this design.
 *
 *   The map already answers a hold, and the answer is the game's own: the hit
 *   area at overlay + 0x43610 publishes action 13, and Minimap::Update
 *   (0x577e40) counts the frames it stays held - a tap latches the big map
 *   open, a hold peeks at it and closes it again on release, and at ten frames
 *   the floor's name and seed appear. Holding the finger back would take all
 *   three away, and eating it would take them away for good.
 *
 *   So nothing is intercepted. The finger goes to the game exactly as it
 *   always did, the map does everything it always did, and the list simply
 *   appears over it once the finger has stayed put longer than the game's own
 *   gesture needs. Letting go then closes the map and leaves the list.
 *
 * What is in the list, and where it is read from - all of it the player's own
 * state, measured:
 *
 *   +0x1ab8   a pointer to one int per collectible id, up to 732: how many of
 *             each the player owns. Actives are in it too, so the list needs
 *             no separate word for them.
 *   +0x1ab0   two trinket slots. The high bit marks the golden one; the id is
 *             what is left.
 *   +0x1c10   four pocket slots, each { int id, int what }: 1 is a card, 2 a
 *             pill. A pill is named by its colour, which the descriptions turn
 *             into an effect themselves.
 *
 * Nothing here draws its own text or pictures: it borrows the panel's hands
 * (eid.c), so the list is in the same font, at the same scale, and follows the
 * widened canvas without knowing it exists.
 */
#include <math.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <android/log.h>
#include "diag.h"
#include "devices.h"
#include "bagswipe.h"

#if ISAAC_DIAG
#define LOG(...) __android_log_print(ANDROID_LOG_INFO, "isaac", __VA_ARGS__)
#else
#define LOG(...) do { } while (0)
#endif

unsigned long shim_image_base(void);
int shim_sticks_in_game(void);
float shim_screen_width(void);
float shim_screen_height(void);
void shim_touch_push_raw(int id, int phase, float x, float y);
int shim_hud_map_box(float *cx, float *cy, float *w, float *h);
int shim_trace_readable(const void *p, unsigned long n);
void shim_eid_marker(const char *name, float x, float y);
void shim_eid_marker_at(const char *name, float x, float y, float scale);
void shim_eid_write(const char *text, float x, float y,
                    float r, float g, float b, float a);
float shim_eid_line_height(void);
void shim_eid_pin(unsigned kind, unsigned id);
void shim_eid_list_player(unsigned long p);     /* eid.c: whose list is up (0: none) */
int shim_eid_pill(int colour, unsigned *kind, unsigned *id);
const char *shim_eid_name(unsigned kind, unsigned id);
const char *shim_eid_text(unsigned kind, unsigned id);
float shim_eid_show_item(const char *picture, const char *name, const char *text,
                         float x, float y);
int shim_eid_form(int form, const char **icon, const char **name, int *have, int *needed);
int shim_report_active(void);
void report_line(const char *kind, const char *text);

/*
 * Longer than the game's own hold: it acts at ten frames, a sixth of a second,
 * and the peek lasts as long as the finger does. A second and a bit is well
 * past both and still not a wait.
 */
#define HOLD_MS 1200
#define WANDER  48.0f               /* a thumb on glass travels */

#define AREA            0xabec64    /* the drawn area: width, height, x, y in pixels */
#define GAME_OBJECT     0xac3b90
#define GAME_PLAYERS    0x25c40
#define FN_PLAYER       0x4da574
#define PLAYER_OWNED    0x1ab8
#define PLAYER_TRINKETS 0x1ab0
/*
 * A pocket slot is a PAIR: { what, kind }. The kinds are the game's own, read
 * out of its list builder (0x8615d4, branches at 0x8615e4..0x861648):
 *
 *   kind 1, what != 0   a card, and `what` is its number
 *   kind 0, what != 0   a pill, and `what` is its colour
 *   kind 2, what >= 1   a POCKET ACTIVE, and `what` is the number of an active
 *                       slot: the item itself is at player+0x1964 + (what-1)*0x1c
 *
 * The third was measured on the device (Bag of Crafting: pair 3/2 with 710 at
 * player+0x199c). Until then kind 2 was taken for a pill here, which is why a
 * pocket active was described as one and a pill in the pocket as nothing.
 */
#define PLAYER_POCKET   0x1c10
#define POCKET_PILL     0
#define POCKET_CARD     1
#define POCKET_ACTIVE   2
#define POCKET_SLOTS    4
#define TRINKET_SLOTS   2
#define MAX_ITEM_ID     732

#define KIND_COLLECTIBLE 1
#define KIND_TRINKET     2
#define KIND_CARD        3
#define KIND_PILL        4
#define EID_KIND_PILL    4                  /* what the descriptions call a pill */
#define FORMS            14

/*
 * The list is meant to be read and aimed at with a thumb, so the pictures are
 * drawn at twice the size they have in a line of text and the cells are wide
 * enough to keep them apart.
 */
/*
 * The mod does not lay its list out as a grid of pictures in a corner: it
 * prints it where every other description goes, one line per thing, picture
 * first and name after (eid_holdmapdesc.lua builds ordinary description
 * entries and hands them to the same printing). So this does the same, and
 * borrows the panel's own place so the two never argue about where they are.
 */
#define ROW_ICON  14.0f             /* what the mod leaves for the picture */
#define SCALE     1.0f
#define MAX_SHOWN 256

/* `id` is the pill's COLOUR for a pill, and the thing's own number otherwise */
typedef struct { unsigned char kind; unsigned short id; } entry_t;

static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;

static int g_open;
static entry_t g_entry[MAX_SHOWN];
static int g_entries;
static float g_scroll;
static float g_row_top[MAX_SHOWN], g_row_bottom[MAX_SHOWN];
static int g_rows;
static float g_height;
/*
 * HOW TALL EVERY ENTRY IS, measured ONCE when the list is built.
 *
 * It used to be taken from the drawing, and that is what made the scrolling mad
 * (the owner, 14.09.2026: "it tries to slide by itself and stick to the finger
 * at once"). The draw stops at the bottom of the screen and skips anything more
 * than eight steps above the top, so an entry that left the view had its real
 * height - which follows the length of its description - replaced by a flat
 * guess of one step. Everything below it then moved, while the finger was
 * holding the list still.
 *
 * With the heights known in advance the scroll is a plain offset and the list
 * cannot move under the finger. eid.c measures without drawing.
 */
static float g_row_h[MAX_SHOWN];
static float g_total;               /* the whole list, scroll or no scroll */

/* the finger being watched, and the one dragging the list */
static int g_id = -1;
static float g_down_x, g_down_y;
static long g_down_ms;
static int g_drag_id = -1;
/* the finger on the Bag of Crafting's screen, and its gesture (below, bagswipe.h) */
static int g_bag_id = -1;
static bagswipe_t g_bag_swipe;
/* the finger on a long description's panel, turning its pages (eid.c, PAGES) - the same gesture */
static int g_desc_id = -1;
static bagswipe_t g_desc_swipe;

/*
 * WHERE the list was drawn, so a finger can be told to be on it. Set by the
 * drawing, read by the touches; both in canvas units.
 */
static float g_box_x, g_box_top, g_box_bottom;
static float g_drag_y, g_drag_from;
static int g_dragged;

static long now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000L + ts.tv_nsec / 1000000L;
}

/* ------------------------------------------------------------- the player */

static unsigned long player_now(void) {
    unsigned long b = shim_image_base();
    if (!b) return 0;
    unsigned long game = *(const unsigned long *) (b + GAME_OBJECT);
    if (!game) return 0;
    unsigned long pm = game + GAME_PLAYERS;
    /* empty list, no call - see the note beside player_now() in eid.c */
    if (!shim_trace_readable((const void *) (pm + 0x10), 16)) return 0;
    unsigned long first = *(const unsigned long *) (pm + 0x10);
    unsigned long past = *(const unsigned long *) (pm + 0x18);
    if (!first || past <= first) return 0;
    unsigned long me = ((unsigned long (*)(unsigned long, int)) (b + FN_PLAYER))(pm, 0);
    return shim_trace_readable((const void *) me, 0x2700) ? me : 0;
}

static void add(unsigned char kind, unsigned id) {
    if (g_entries >= MAX_SHOWN || !id) return;
    g_entry[g_entries].kind = kind;
    g_entry[g_entries].id = (unsigned short) id;
    g_entries++;
}

/*
 * The list, built when it is opened rather than every frame: it changes only
 * when the player picks something up, and by then the list is shut. `who` is
 * the player it is opened for (co-op: the one holding the map).
 */
static void gather(unsigned long who) {
    g_entries = 0;
    g_scroll = 0.0f;
    unsigned long me = who && shim_trace_readable((const void *) who, 0x2700) ? who : 0;
    if (!me) return;

    const int *counts = *(const int *const *) (me + PLAYER_OWNED);
    if (counts && shim_trace_readable(counts, (MAX_ITEM_ID + 1) * 4)) {
        for (int id = 1; id <= MAX_ITEM_ID; id++) {
            int n = counts[id];
            if (n <= 0 || n > 999) continue;
            for (int i = 0; i < n && i < 8; i++) add(KIND_COLLECTIBLE, (unsigned) id);
        }
    }

    const int *trinkets = (const int *) (me + PLAYER_TRINKETS);
    for (int i = 0; i < TRINKET_SLOTS; i++) {
        int id = trinkets[i] & 0x7fff;
        if (id > 0 && id <= 4096) add(KIND_TRINKET, (unsigned) id);
    }

    const int *pocket = (const int *) (me + PLAYER_POCKET);
    for (int i = 0; i < POCKET_SLOTS; i++) {
        int id = pocket[i * 2], what = pocket[i * 2 + 1];
        if (id <= 0) continue;
        if (what == POCKET_CARD && id <= 97) add(KIND_CARD, (unsigned) id);
        /*
         * A pill the player has not learnt yet is left out, as the mod leaves
         * it out of its own reminder (eid_holdmapdesc.lua: an unidentified pill
         * is added only with ShowUnidentifiedPillDescriptions, which is off).
         * Listing it with nothing to say - a picture and no words - was neither
         * the mod's way nor useful.
         */
        else if (what == POCKET_PILL && id <= 4096) {
            unsigned kind, effect;
            if (shim_eid_pill(id & 0x7ff, &kind, &effect)) add(KIND_PILL, (unsigned) id);
        }
    }
    LOG("items: the list holds %d things", g_entries);
}

/* ------------------------------------------------------------ the drawing */

/* the panel's own corner, so the list starts exactly where a description does */
/*
 * The list stands exactly where the panel stands.
 *
 * It used to have a place of its own, up in the corner, and there it lay over
 * the hearts and the counters - the very thing the panel's placement was
 * worked out to avoid (eid.c: the mod's (60, 45) plus the HUD Offset the game
 * lets the player set). One rule, asked for rather than copied, so a change to
 * the panel's place moves the list with it.
 */
void shim_eid_panel_place(float *x, float *y);

static void where(float *x, float *y) {
    shim_eid_panel_place(x, y);
}

/* between two entries, a third of a line: as it was before the ninth round */
#define ENTRY_AIR 0.35f

static float row_height(void) {
    float h = shim_eid_line_height();
    return (h >= 6.0f && h <= 40.0f) ? h : 11.0f;
}

static float rows_shown(void) {
    float x, y;
    where(&x, &y);
    float n = (shim_screen_height() - y - 8.0f) / row_height();
    return n < 1.0f ? 1.0f : n;
}

static void marker_for(const entry_t *e, char *out, size_t room) {
    switch (e->kind) {
        case KIND_TRINKET: snprintf(out, room, "Trinket%u", e->id); break;
        case KIND_CARD:    snprintf(out, room, "Card%u", e->id); break;
        case KIND_PILL:    snprintf(out, room, "Pill%u", e->id & 0x7ff); break;
        default:           snprintf(out, room, "Collectible%u", e->id); break;
    }
}

float shim_eid_measure_item(const char *picture, const char *name, const char *text);

/* the height of every entry, and the whole list: once, at the opening */
static int g_head;                          /* below: the character over a co-op list */

static void measure_all(void) {
    float step = row_height();
    g_total = g_head >= 0 ? step : 0.0f;    /* the head's row */
    for (int i = 0; i < g_entries; i++) {
        entry_t e = g_entry[i];
        unsigned kind = e.kind, id = e.id;
        if (e.kind == KIND_PILL && !shim_eid_pill(e.id & 0x7ff, &kind, &id)) kind = 0;
        char marker[32], picture[40];
        marker_for(&e, marker, sizeof(marker));
        snprintf(picture, sizeof(picture), "{{%s}}", marker);
        const char *name = kind ? shim_eid_name(kind, id) : NULL;
        const char *body = kind ? shim_eid_text(kind, id) : NULL;
        float h = kind ? shim_eid_measure_item(picture, name ? name : "", body ? body : "") : 0.0f;
        if (h <= 0.0f) h = step;
        g_row_h[i] = h;
        g_total += h + step * ENTRY_AIR;
    }
    for (int form = 0; form < FORMS; form++) {
        const char *icon = NULL, *label = NULL;
        int have = 0, needed = 0;
        if (shim_eid_form(form, &icon, &label, &have, &needed)) g_total += step;
    }
}


int shim_eid_hud_showing(void);
/*
 * THE LIST IS EID'S, and goes with EID's switch on the MODS page (the owner,
 * 06.10.2026: with the mod off the list still came up on a held map). It is
 * the mod's own Item Reminder (eid_holdmapdesc.lua), the game has nothing like
 * it, and every word in it is a description of the mod's. Read live, as the
 * panel reads it (eid.c, shim_eid_on): the switch takes effect without a
 * restart, so a list that is up when it goes off is put away between frames.
 */
int shim_eid_on(void);

void shim_items_draw(void) {
    if (!shim_eid_hud_showing() || !shim_eid_on()) return;
    pthread_mutex_lock(&g_lock);
    int open = g_open, n = g_entries;
    float scroll = g_scroll;
    pthread_mutex_unlock(&g_lock);
    if (!open) return;

    float x, y0;
    where(&x, &y0);
    float step = row_height();
    float bottom = shim_screen_height() - 4.0f;

    /*
     * Each thing is shown the way the panel shows it - picture, name, and the
     * description under it - because that is what the mod's own list does: it
     * builds ordinary description entries and prints them.
     */
    float y = y0 - scroll;
    int shown = 0;
    g_rows = 0;
    if (g_head >= 0) {                      /* co-op: whose list this is */
        if (y >= y0 - step && y <= bottom) {
            char head[16];
            snprintf(head, sizeof(head), "Player%d", g_head);
            shim_eid_marker_at(head, x, y, SCALE);
        }
        y += step;
    }
    for (int i = 0; i < n; i++) {
        if (y > bottom) {                       /* below the screen: only counted */
            if (g_rows < MAX_SHOWN) { g_row_top[g_rows] = y; g_row_bottom[g_rows] = y; }
            g_rows++;
            y += g_row_h[i] + step * ENTRY_AIR;
            continue;
        }
        entry_t e;
        pthread_mutex_lock(&g_lock);
        e = g_entry[i];
        pthread_mutex_unlock(&g_lock);

        unsigned kind = e.kind, id = e.id;
        if (e.kind == KIND_PILL && !shim_eid_pill(e.id & 0x7ff, &kind, &id)) { kind = 0; }
        char marker[32];
        marker_for(&e, marker, sizeof(marker));
        const char *name = kind ? shim_eid_name(kind, id) : NULL;
        const char *body = kind ? shim_eid_text(kind, id) : NULL;

        char picture[40];
        snprintf(picture, sizeof(picture), "{{%s}}", marker);
        /*
         * The height comes from the cache, never from what the drawing happened
         * to do: an entry above the view is stepped over by its own measure.
         */
        float used = g_row_h[i];
        if (y + used >= y0 - step)
            shim_eid_show_item(picture, name ? name : "", body ? body : "", x, y);
        if (g_rows < MAX_SHOWN) { g_row_top[g_rows] = y; g_row_bottom[g_rows] = y + used; }
        g_rows++;
        y += used + step * ENTRY_AIR;       /* a breath between entries */
        shown++;
    }
    g_height = g_total;

    /*
     * The transformations close the list, each with how far along it is. They
     * are not pickable: there is no description behind a transformation, only
     * the progress, and that is on the line itself.
     */
    for (int form = 0; form < FORMS; form++) {
        const char *icon = NULL, *label = NULL;
        int have = 0, needed = 0;
        if (!shim_eid_form(form, &icon, &label, &have, &needed)) continue;
        if (y >= y0 - step && y <= bottom) {
            char line[96];
            snprintf(line, sizeof(line), "%s (%d/%d)", label ? label : "", have, needed);
            shim_eid_marker_at(icon, x, y, SCALE);
            shim_eid_write(line, x + ROW_ICON, y, 0.5f, 0.5f, 1.0f, 1.0f);
        }
        y += step;
        shown++;
    }
    if (!shown) shim_eid_write("-", x, y0 + (g_head >= 0 ? step : 0.0f), 1.0f, 1.0f, 1.0f, 1.0f);

    /* where it ended up, for on_list: a finger outside this belongs to the game */
    g_box_x = x;
    g_box_top = y0;
    g_box_bottom = y > y0 ? y : y0 + step;
}

/* -------------------------------------------------------------- the finger */

/*
 * One space for both of this file's fingers, the PICTURE's (screen.c, "one
 * finger, two spaces"): the map's zone is an ItemButton the game judges through
 * 0x86255c, which is the picture, and the list is drawn from hook O inside the
 * widget pass, whose viewport is the picture. FULLSCREEN on or off, the long
 * press watches the very press the map takes, and a finger on a line of the
 * list is on the line it sees.
 */
int shim_screen_finger_units(float x, float y, float *u, float *v);

static int to_field(float x, float y, float *gx, float *gy) {
    return shim_screen_finger_units(x, y, gx, gy);
}

/*
 * Where the map is. The widget's own box is the truth, but the port only meets
 * the widget when the game hands it a finger - and the very first press has to
 * work too. So until then the box is taken from the corner it lives in: on the
 * design canvas the zone is 403.5..445.5 across, which is 55.5 units from the
 * right edge, and the HUD keeps its corners as the canvas widens.
 */
#define MAP_FROM_RIGHT 55.5f
#define MAP_CENTRE_Y   41.0f
#define MAP_W          42.0f
#define MAP_H          44.0f

static int map_box(float *cx, float *cy, float *w, float *h) {
    if (shim_hud_map_box(cx, cy, w, h)) return 1;
    float width = shim_screen_width();
    if (width < 100.0f) return 0;
    *cx = width - MAP_FROM_RIGHT;
    *cy = MAP_CENTRE_Y;
    *w = MAP_W;
    *h = MAP_H;
    return 1;
}

static int on_map(float x, float y) {
    float gx, gy, cx, cy, w, h;
    if (!to_field(x, y, &gx, &gy)) return 0;
    if (!map_box(&cx, &cy, &w, &h)) return 0;
    if (shim_report_active()) {
        static int said;
        if (!said) {
            said = 1;
            char line[120];
            snprintf(line, sizeof(line), "map box centre %.1f,%.1f size %.1f x %.1f;"
                     " finger at %.1f,%.1f", (double) cx, (double) cy, (double) w,
                     (double) h, (double) gx, (double) gy);
            report_line("items", line);
        }
    }
    return fabsf(gx - cx) <= w * 0.5f && fabsf(gy - cy) <= h * 0.5f;
}

/*
 * Is the finger ON the list? Only then is it the list's.
 *
 * While the list is up everything else on the screen goes on working - the
 * sticks above all - so the list claims a touch only inside what it actually
 * drew. The width is the panel's own text box, asked for rather than guessed,
 * so the two stay in step.
 */
float shim_eid_panel_width(void);

static int on_list(float px, float py) {
    float gx, gy;
    if (!to_field(px, py, &gx, &gy)) return 0;
    if (!(g_box_bottom > g_box_top)) return 0;
    return gx >= g_box_x - 6.0f && gx <= g_box_x + shim_eid_panel_width()
        && gy >= g_box_top - 4.0f && gy <= g_box_bottom + 4.0f;
}

/* which entry is under the finger, or -1: the drawing remembered where each went */
static int entry_at(float px, float py) {
    float gx, gy, x, y0;
    if (!to_field(px, py, &gx, &gy)) return -1;
    where(&x, &y0);
    if (gx < x - 4.0f) return -1;
    for (int i = 0; i < g_rows && i < g_entries; i++)
        if (gy >= g_row_top[i] && gy < g_row_bottom[i]) return i;
    return -1;
}

static void close_list(void) {
    /*
     * The panel lent us its working space while the list was up, so it is
     * asked to lay itself out again: what is in there now is ours, not its.
     */
    if (g_open) shim_eid_pin(0, 0);
    if (g_open) shim_eid_list_player(0);
    g_open = 0;
    g_drag_id = -1;
    g_dragged = 0;
}

static void forget(void) {
    g_id = -1;
}

/* the panel must stand aside while the list is up: they share a corner */
int shim_items_open(void) {
    pthread_mutex_lock(&g_lock);
    int open = g_open;
    pthread_mutex_unlock(&g_lock);
    return open;
}

void shim_items_forget(void) {
    pthread_mutex_lock(&g_lock);
    forget();
    g_bag_id = -1;                  /* the bag's screen held one too: its cancel goes in raw */
    g_desc_id = -1;                 /* and a description's pages */
    close_list();
    pthread_mutex_unlock(&g_lock);
}

/*
 * A FINGER ON THE BAG OF CRAFTING'S SCREEN: a sideways swipe turns a page,
 * right the next and left the previous, round the ends (the owner,
 * 07.10.2026); a hold of half a second locks the list
 * (the owner, 06.10.2026 - until then a tap was the next page, round to the
 * first, and there was no way back). bagswipe.h tells the three apart.
 *
 * The screen is a touch zone of its own: a finger that goes down on it is the
 * zone's until it is lifted, and never reaches the sticks or the buttons - the
 * zone stands ahead of all of them in the claim chain (uikit.c,
 * shim_touch_push). It used to hand a finger that wandered 48 pixels to the
 * game as a fresh press; a swipe is exactly such a finger, so it no longer
 * does.
 */
int shim_eid_craft_showing(void);
float shim_eid_panel_height(void);
float shim_eid_panel_width(void);
void shim_eid_panel_place(float *px, float *py);
void shim_eid_craft_touch_turn(int direction);
void shim_eid_craft_touch_lock(void);
void shim_touch_push_raw(int id, int phase, float x, float y);

/*
 * A LONG DESCRIPTION'S PANEL under a finger (eid.c, PAGES; hotfix 1.72): the
 * same rectangle and the same swipe as the bag's screen - right the next page,
 * left the previous, round the ends; a tap and a hold do nothing. It
 * is a zone only while the description on screen has more than one page and
 * the bag's recipes do not own the spot, and it claims a finger only where the
 * finger goes down on the panel - never on a fixed stick's ring, which the
 * panel can reach over on the left (sticks.c, shim_sticks_rings, canvas units
 * like the finger's).
 */
int shim_eid_pages_showing(void);
int shim_eid_panel_touchable(void);             /* eid.c: the panel on screen is player 0's */
void shim_eid_pages_touch_turn(int direction);
int shim_sticks_rings(float *out8);

static int on_bag_screen(float x, float y);

static int on_long_panel(float x, float y) {
    float gx, gy;
    if (!on_bag_screen(x, y) || !to_field(x, y, &gx, &gy)) return 0;
    float ring[8];
    int rings = shim_sticks_rings(ring);
    for (int i = 0; i < rings && i < 2; i++)
        if (gx >= ring[i * 4] && gx <= ring[i * 4 + 2] && gy >= ring[i * 4 + 1] && gy <= ring[i * 4 + 3]) return 0;
    return 1;
}

/*
 * THE UPPER HALF OF THE PANEL, and only that (the owner, 07.10.2026): the
 * swipe zone of the bag's recipes and of a long description's pages ends at
 * exactly half the panel's drawn height. The lower half is never claimed - it
 * is where a thumb reaching for the movement stick lands, and a stick that
 * comes to the finger (dynamic, floating) has no ring to keep clear of.
 */
static int on_bag_screen(float x, float y) {
    float gx, gy, px = 0.0f, py = 0.0f;
    if (!to_field(x, y, &gx, &gy)) return 0;
    shim_eid_panel_place(&px, &py);
    float h = shim_eid_panel_height();
    if (!(h > 0.0f)) return 0;
    return gx >= px - 6.0f && gx <= px + shim_eid_panel_width() && gy >= py - 6.0f && gy <= py + h * 0.5f;
}

/*
 * A touch before it reaches the queue. Answers 1 when the game must not see
 * it - while the list is up nothing else may be touched, and while a press on
 * the map is being judged it is held back.
 */
int shim_items_touch(int id, int phase, float x, float y) {
    int answer = 0;
    if (shim_report_active() && phase == 0) {
        static int said;
        if (said < 3) {
            said++;
            float gx = -1.0f, gy = -1.0f, cx = -1.0f, cy = -1.0f, w = -1.0f, h = -1.0f;
            int ok = to_field(x, y, &gx, &gy), box = map_box(&cx, &cy, &w, &h);
            char line[180];
            snprintf(line, sizeof(line),
                     "touch %.0f,%.0f -> %s %.1f,%.1f; box %s %.1f,%.1f %.1f x %.1f; in game %d",
                     (double) x, (double) y, ok ? "field" : "NO FIELD", (double) gx, (double) gy,
                     box ? "at" : "NONE", (double) cx, (double) cy, (double) w, (double) h,
                     shim_sticks_in_game());
            report_line("items", line);
        }
    }
    pthread_mutex_lock(&g_lock);

    /*
     * The finger on the map is NEVER taken - not even once the list is up.
     *
     * It is the game's from beginning to end, and that is the whole trick: the
     * map is a peek, held open by that press and closed by its release (the
     * overlay's walk hands the button to its handler on every frame the finger
     * is down, 0x861d1c). Swallowing the release used to leave the map
     * expanded, so the player had to tap it a second time to be rid of it.
     *
     * The list follows the same finger: it appears once the press has outlasted
     * a tap and goes when the press does, which is what the mod's own reminder
     * does on the desktop - hold the map key and it is there, let go and it is
     * not.
     */
    if (id == g_id) {
        if (phase == 1) {
            /* wandering only breaks the press while it is still being judged */
            if (!g_open && (fabsf(x - g_down_x) > WANDER || fabsf(y - g_down_y) > WANDER))
                forget();
        } else if (phase >= 2) {
            forget();
            close_list();
        }
        pthread_mutex_unlock(&g_lock);
        return 0;
    }

    if (g_open) {
        /*
         * Every other finger is the game's too - the sticks, the buttons, a
         * card being dropped - except one that lands ON the list, which is
         * there to scroll it or to pin an entry to the panel.
         */
        if (phase == 0 && !on_list(x, y)) {
            pthread_mutex_unlock(&g_lock);
            return 0;
        }
        if (phase == 0) {
            g_drag_id = id;
            g_drag_y = y;
            g_drag_from = g_scroll;
            g_dragged = 0;
        } else if (id != g_drag_id) {
            pthread_mutex_unlock(&g_lock);
            return 0;               /* a finger the list never took */
        } else {
            if (phase == 1) {
                float gy0, gy1, gx;
                if (to_field(x, g_drag_y, &gx, &gy0) && to_field(x, y, &gx, &gy1)) {
                    float moved = gy0 - gy1;
                    if (fabsf(moved) > 2.0f) g_dragged = 1;
                    float most = g_height - rows_shown() * row_height();
                    if (most < 0.0f) most = 0.0f;
                    g_scroll = g_drag_from + moved;
                    if (g_scroll < 0.0f) g_scroll = 0.0f;
                    if (g_scroll > most) g_scroll = most;
                }
            } else {
                if (!g_dragged) {
                    int i = entry_at(x, y);
                    if (i >= 0) {
                        entry_t e = g_entry[i];
                        if (e.kind == KIND_PILL) {
                            unsigned k, n;
                            /* a colour the player has not learnt yet says nothing */
                            if (shim_eid_pill(e.id & 0x7ff, &k, &n)) shim_eid_pin(k, n);
                        } else {
                            shim_eid_pin(e.kind, e.id);
                        }
                    }
                    /* the list is not put away here: it goes when the map does */
                }
                g_drag_id = -1;
                g_dragged = 0;
            }
        }
        pthread_mutex_unlock(&g_lock);
        return 1;
    }

    /*
     * A press under the id of the finger the zone holds: that finger's end was
     * never seen (a lost release), and this is a new one, judged as such.
     */
    if (id == g_bag_id && phase == 0) g_bag_id = -1;
    if (id == g_bag_id) {
        float gx, gy;
        int seen = to_field(x, y, &gx, &gy), what = SWIPE_NONE;
        if (phase == 1 && seen) what = bagswipe_move(&g_bag_swipe, gx, gy);
        if (phase >= 3) {
            g_bag_id = -1;
            if (phase == 3 && seen) what = bagswipe_up(&g_bag_swipe, gx, gy, now_ms());
        }
        if (what == SWIPE_LOCK) shim_eid_craft_touch_lock();
        else if (what != SWIPE_NONE) shim_eid_craft_touch_turn(what);
#if ISAAC_DIAG
        if (what != SWIPE_NONE && shim_report_active())
            report_line("items", what == SWIPE_LOCK ? "bag screen held: lock"
                        : what == SWIPE_NEXT ? "bag screen swiped right: next page"
                        : "bag screen swiped left: previous page");
#endif
        pthread_mutex_unlock(&g_lock);
        return 1;
    }
    /* a long description's panel: its finger, from beginning to end, as the bag's */
    if (id == g_desc_id && phase == 0) g_desc_id = -1;
    if (id == g_desc_id) {
        float gx, gy;
        int seen = to_field(x, y, &gx, &gy), what = SWIPE_NONE;
        if (phase == 1 && seen) what = bagswipe_move(&g_desc_swipe, gx, gy);
        if (phase >= 3) {
            g_desc_id = -1;
            if (phase == 3 && seen) what = bagswipe_up(&g_desc_swipe, gx, gy, now_ms());
        }
        /* a hold is the bag's lock; a description has nothing to lock */
        if (what == SWIPE_NEXT || what == SWIPE_PREVIOUS) shim_eid_pages_touch_turn(what);
#if ISAAC_DIAG
        if ((what == SWIPE_NEXT || what == SWIPE_PREVIOUS) && shim_report_active())
            report_line("items", what == SWIPE_NEXT ? "description swiped right: next page"
                        : "description swiped left: previous page");
#endif
        pthread_mutex_unlock(&g_lock);
        return 1;
    }
    /*
     * Both zones are player 0's (the owner, 11.10.2026): the touch screen is his
     * (coop.c), so a finger pages only his own panel. Another player's recipes or
     * long description are not a zone at all - the finger is the game's, a stick's.
     */
    float bag_gx, bag_gy;
    int mine = shim_eid_panel_touchable();
    if (phase == 0 && mine && g_desc_id < 0 && g_bag_id < 0 && shim_sticks_in_game() && !shim_eid_craft_showing()
        && shim_eid_pages_showing() && on_long_panel(x, y) && to_field(x, y, &bag_gx, &bag_gy)) {
        g_desc_id = id;
        bagswipe_down(&g_desc_swipe, bag_gx, bag_gy, now_ms());
        pthread_mutex_unlock(&g_lock);
        return 1;
    }
    if (phase == 0 && mine && g_bag_id < 0 && shim_sticks_in_game() && shim_eid_craft_showing()
        && on_bag_screen(x, y) && to_field(x, y, &bag_gx, &bag_gy)) {
        g_bag_id = id;
        bagswipe_down(&g_bag_swipe, bag_gx, bag_gy, now_ms());
        pthread_mutex_unlock(&g_lock);
        return 1;
    }

    /*
     * Only watched, never taken: the finger belongs to the game and the map
     * does its own work with it.
     */
    if (phase == 0) {
        if (g_id < 0 && shim_sticks_in_game() && shim_eid_on() && on_map(x, y)) {
            g_id = id;
            g_down_x = x;
            g_down_y = y;
            g_down_ms = now_ms();
        }
    }
    pthread_mutex_unlock(&g_lock);
    return answer;
}

/*
 * Between frames: a press that has outlasted a tap opens the list. This runs
 * where the frames are, not on the touch thread, so nothing is built inside a
 * system callback.
 */
/*
 * THE MAP HELD ON A PAD OR A KEYBOARD opens the list the same way (16.09.2026:
 * players found it opened only by a finger, so the pad and the keyboard had no
 * way to it at all).
 *
 * Nothing new is watched for this. The game asks one question of every input
 * device alike - is this action held, for this player's controller -
 * 0x671c5c(action, controller), which goes to the device's IsActionPressed
 * (vtable +0x48). D Infinity's drop (action 11) and the reset are read through
 * the same door. The map is action 13; the player's controller is at
 * player+0x19ec. Held past HOLD_MS the list opens; let go, it closes - the hold
 * a finger makes on the map, made with a button.
 *
 * While a finger is on the map (g_id set) this stays out of the way: the touch
 * road owns that press from start to end.
 */
#define ACTION_MAP      13

/*
 * CO-OP (players' reports on 1.73: "the others cannot open their inventory").
 * Every player holds the map on his own device and gets his own list, as the
 * mod's Item Reminder does (main.lua 1358-1368: holdTabPlayer is the player
 * whose controller holds the map, PlayersActionPressed, eid_api.lua 2485;
 * eid_holdmapdesc.lua 734-745 opens on that player, 667-707 reads his
 * controller). One list at a time, as there is one Item Reminder: whoever
 * holds first keeps it until he lets go. The finger on the map is player 0's,
 * as the touch screen is (coop.c).
 */
#define PLAYERS_SEEN    8
int shim_eid_main_players(unsigned long *out, int max);
int shim_eid_player_held(unsigned long p, int action);
int shim_eid_player_device_kind(unsigned long p);
unsigned long shim_eid_panel_player(void);
int shim_eid_player_seat(unsigned long p);      /* the panel's player may be Esau: the device is what counts */
int shim_eid_player_type(unsigned long p);

static long g_button_since = -1;            /* when the map button went down */
static int g_button_opened;                 /* the list is up because of it */
static int g_button_for_bag;                /* this hold is the bag's: no list */
static long g_scroll_at;                    /* the last frame the keys scrolled */
static unsigned long g_holder;              /* whose map button this hold is */

/*
 * THE KEYS OF THE LIST (the owner's eighth round, 24.09.2026: on a pad the
 * list opened but only a finger could scroll it, while the left thumb held
 * Select). With the list up, shooting up and down scrolls it - the right stick
 * on a pad, the arrows on a keyboard: the hand that is free.
 *
 * The same two keys page the Bag of Crafting's screen while the map is held
 * (eid_craft.c, the mod's own keys), and the list, opening on the same hold,
 * used to cover that screen before a page could be turned. So on a keyboard a
 * hold while the holder's own bag screen or long description is up belongs to
 * them, and the list stays shut (below, 11.10.2026: it used to take a shot
 * within 1.2 s to keep it shut).
 */
#define LIST_KEY_SPEED  240.0f              /* units a second */
#define ACTION_SHOOT_UP   6
#define ACTION_SHOOT_DOWN 7
#define ACTION_SHOOT_LEFT 4
#define ACTION_SHOOT_RIGHT 5
int shim_eid_craft_showing(void);

/* the player's device is a pad, by its class (devices.c, batch 2 U4; "!= 0" took the screen 0x10dead for one) */
static int on_pad(unsigned long who) {
    return who && shim_eid_player_device_kind(who) == DEVICE_PAD;
}

/* -1 up, 1 down, 0 neither; *any: some shot is held at all - on `who`'s own device */
static int shot_held(unsigned long who, int *any) {
    *any = 0;
    if (!who) return 0;
    int up = shim_eid_player_held(who, ACTION_SHOOT_UP), down = shim_eid_player_held(who, ACTION_SHOOT_DOWN);
    *any = up || down || shim_eid_player_held(who, ACTION_SHOOT_LEFT) || shim_eid_player_held(who, ACTION_SHOOT_RIGHT);
    return down - up;
}

/* who holds the map: whoever holds it already keeps it; else the first, in the game's order */
static unsigned long map_holder(void) {
    unsigned long p[PLAYERS_SEEN];
    int n = shim_eid_main_players(p, PLAYERS_SEEN);
    for (int i = 0; i < n; i++)
        if (g_holder && p[i] == g_holder && shim_eid_player_held(p[i], ACTION_MAP)) return p[i];
    for (int i = 0; i < n; i++)
        if (shim_eid_player_held(p[i], ACTION_MAP)) return p[i];
    return 0;
}

/*
 * The run's players of their own, more than one: the list says whose it is, with
 * the character's head as the mod's title does (eid_holdmapdesc.lua 773-785:
 * with more than one player, GetPlayerIcon of the player shown). Single player:
 * no head, the list exactly as before.
 */
static int g_head = -1;                     /* the character over the list, -1 none */

void shim_items_between_frames(void) {
    int eid = shim_eid_on();
    unsigned long holder = eid && shim_sticks_in_game() ? map_holder() : 0;
    int button = holder != 0;
    int shot_any = 0, shot = button ? shot_held(holder, &shot_any) : 0;
    /*
     * THE MAP ON THE KEYBOARD IS THE PAGES' while the holder's own recipes or his own
     * long description are on screen (the owner, 11.10.2026): no list over them,
     * however long Map is held, so Map + an arrow always turns a page. It used to
     * need a shot within 1.2 s, and a player who held Tab first and read the hint got
     * the list instead. The mod does the same for the bag: while its recipes are up
     * the held map is theirs and the reminder is not shown (TabDescThisFrame,
     * eid_bagofcrafting.lua 875-876). Asked every frame, as the mod asks: walk away
     * from the thing and the same hold opens the list.
     *
     * Not a pad's: a pad pages with its sticks' clicks, Select is the list's alone
     * (eid_craft.c, PAD_LINE). Co-op: only the holder's own panel (the same device -
     * Jacob's keys for Esau's bag); another player's panel leaves his list alone.
     */
    int bag = button && !g_open && (shim_eid_craft_showing() || shim_eid_pages_showing())
              && !on_pad(holder) && shim_eid_player_seat(holder) == shim_eid_player_seat(shim_eid_panel_player());
    pthread_mutex_lock(&g_lock);
    int open_now = 0;
    if (eid && g_id >= 0 && !g_open && now_ms() - g_down_ms >= HOLD_MS) open_now = 1;
    /* another player's hold is a new hold: the first one's list goes with his button */
    if (g_id < 0 && button && g_holder && holder != g_holder) {
        if (g_open && g_button_opened) close_list();
        g_button_since = -1;
        g_button_opened = 0;
        g_scroll_at = 0;
    }
    if (g_id < 0) g_holder = holder;
    if (g_id < 0) {
        if (button) {
            if (g_button_since < 0) { g_button_since = now_ms(); g_button_for_bag = 0; }
            g_button_for_bag = bag;
            if (g_open && g_button_opened) {
                long t = now_ms();
                if (shot && g_scroll_at) {
                    float most = g_height - rows_shown() * row_height();
                    if (most < 0.0f) most = 0.0f;
                    g_scroll += (float) shot * LIST_KEY_SPEED * (float) (t - g_scroll_at) / 1000.0f;
                    if (g_scroll < 0.0f) g_scroll = 0.0f;
                    if (g_scroll > most) g_scroll = most;
                }
                g_scroll_at = t;
            }
            if (!g_open && !g_button_for_bag && now_ms() - g_button_since >= HOLD_MS) {
                open_now = 1;
                g_button_opened = 1;
            }
        } else {
            g_button_since = -1;
            g_button_for_bag = 0;
            g_scroll_at = 0;
            if (g_open && g_button_opened) close_list();
            g_button_opened = 0;
        }
    }
    if (open_now) {
        /* the finger is player 0's (the touch screen is his, coop.c); a button, its holder's */
        unsigned long who = g_id >= 0 ? player_now() : holder;
        unsigned long p[PLAYERS_SEEN];
        int players = shim_eid_main_players(p, PLAYERS_SEEN);
        pthread_mutex_unlock(&g_lock);
        shim_eid_list_player(who);          /* eid.c: the transformations below are his */
        g_head = players > 1 ? shim_eid_player_type(who) : -1;
        gather(who);                        /* reads the player: not under the lock */
        measure_all();                      /* the heights, before a single frame is drawn */
        pthread_mutex_lock(&g_lock);
        g_open = 1;
        LOG("items: the list is up");
        if (shim_report_active()) {
            char line[80];
            snprintf(line, sizeof(line), "the list is up with %d things", g_entries);
            report_line("items", line);
        }
    }
    if (g_open && (!shim_sticks_in_game() || !eid)) close_list();
    pthread_mutex_unlock(&g_lock);
}
