/*
 * Dropping a card or a pill from the touch screen: a long press on its button.
 *
 * The game can drop them - INPUT_DROP, action 11 - but on a phone the only
 * button that ever asks for it is the trinket's: 0x86122c reads two bytes of
 * two widgets and publishes the action from them, and nothing else in the
 * overlay does. So a player with no trinket cannot drop anything at all. The
 * owner asked for the same long press on the card and pill button, and this
 * file is it.
 *
 * Why the press has to be HELD BACK rather than watched.
 *
 *   The pocket button answers a press, not a release, and it answers it on the
 *   very first frame: the dispatcher hands the widget to 0x86448c, which walks
 *   the pocket selection on to the slot that was tapped and then publishes the
 *   group's second action, 19 - "use it". Measured on the device: one tap, one
 *   line `action 19 pressed`, and the card was gone. Letting the press through
 *   and only then deciding it was long would use the card first and drop it
 *   second - two actions on one finger.
 *
 *   So a press that lands on a pocket button is held back while the decision is
 *   made, exactly as the pause button's is (reset.c). Let go early, and the
 *   press and its release are handed over together: the card is used the way it
 *   always was, only later by the length of the tap. Held to the end, and the
 *   finger is eaten: the drop is asked for and the card is never used.
 *
 * Which buttons they are: worked out from the overlay's layout, not looked for.
 *
 *   The list builder walks the four pocket slots of Repentance every frame and
 *   takes their widget as `group + i * 0x1c8 + 0x10` (0x8615d4), the group of
 *   the local player being overlay + 0x20fb0. Measured on the device: the
 *   button that answered a tap sat at overlay + 0x20fc0, which is that formula
 *   with i = 0. So the four of them are known by arithmetic, and neither their
 *   picture, nor their name, nor where they happen to be drawn comes into it -
 *   they slide along the bottom as the bomb and the item buttons come and go.
 *
 *   Each keeps the box it answers touches in at +0x1b4 (centre) and +0x1bc (the
 *   whole size) - what ItemButton::HitTest (0x864034) reads, field for field -
 *   and the box is read afresh on every touch, so a button that has moved is
 *   still the button under the finger.
 */
#include <math.h>
#include <stdio.h>
#include <pthread.h>
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

#define HOLD_MS 400                 /* longer than a tap, shorter than a wait */
#define WANDER 48.0f                /* a thumb on glass travels; a gesture must survive it */

#define VT_ITEMBUTTON 0xa90c98      /* IsaacRepentance::TouchOverlay::ItemButton */
#define WIDGET_SHOWN  0x0c          /* byte: the widget takes part at all */
#define ITEM_CENTRE   0x1b4         /* Vector2, as ItemButton::HitTest reads it */
#define ITEM_EXTENT   0x1bc         /* Vector2: the whole width and height */

#define POCKET_ROW    0x20fb0       /* the local player's group: row one of the overlay */
#define POCKET_FIRST  (POCKET_ROW + 0x10)
#define POCKET_STRIDE 0x1c8

/*
 * BOTH ROWS: THE TWIN HAS POCKET BUTTONS OF HIS OWN.
 *
 * The overlay lays out a second row (0x860d44) when the touch player is Jacob
 * or Esau (type 19 or 20): the group at overlay + 0x21e48 is built for his twin
 * +0x2580 (0x860dec..0x860e10) by the same list builder 0x8613a8, so its pocket
 * buttons are that group's + 0x10 + i * 0x1c8, exactly as row one's are. Their
 * action is 21 instead of 19 (0x85f654, flag 0), and a press on them uses the
 * twin's card on the very first frame, the same way.
 *
 * Only row one used to be looked at here, so a long press on Esau's card was
 * never held back: the game took it at once and used the card (the owner,
 * 05.10.2026: "on the second one the long tap does not work - it uses the card
 * at once"). The row is now part of the answer: each row keeps what its own
 * player's slots hold, a finger remembers the row it landed in, and the
 * rotation - for the drop and for the exchange - is asked of that row's player.
 *
 * The drop itself is published on the DEVICE, which both twins sit on and both
 * read (0x52ae90, 0x671c5c drops the player): the same as the trinket zone and
 * the same as a held drop on PC, whichever twin's card the finger was on.
 *
 * The second group is live only while its head byte is set (0x8613c8 builds
 * nothing otherwise, 0x860420 lifts row one by it); without it, its widgets are
 * last run's and refuse the finger.
 */
#define ROW_STRIDE    0xe98         /* row two, overlay + 0x21e48: the twin's group */
#define ROWS          2

/*
 * A pocket slot can hold an ACTIVE ITEM, and that button is not ours.
 *
 * Repentance puts a pocket active - Bag of Crafting, a Dice Shard, a Wavy Cap -
 * in a pocket slot beside the cards and the pills, and the overlay gives it a
 * button in the same row. The game tells the two apart in its own list builder
 * (0x8615d4): the pair at player+0x1c10 + slot*8 is read, and when the second
 * word is 2 the FIRST word is not a card or a pill but the number of an active
 * slot, and the button is built from player+0x1964 (0x861620..0x861648).
 *
 * Nothing about such a button is a card: INPUT_DROP has nothing to drop, and
 * HOLDING it is how Bag of Crafting is used. Holding it back for four hundred
 * milliseconds and then eating the finger is exactly what stopped the bag from
 * crafting - measured: the report showed `pocket took a finger` and not a
 * single call of the item road (0x864434) while the bag was pressed.
 *
 * The kinds are read on the frame thread, where the player is handed to us
 * anyway, and only looked at on the touch thread. A byte cannot tear, and a
 * frame's staleness is nothing next to a finger.
 */
#define PLAYER_POCKET 0x1c10        /* four pairs: { what, kind } */
#define POCKET_ACTIVE 2             /* the kind the game reads as an active slot */

/*
 * TWO CONSUMABLES: THE SECOND BUTTON ONLY BRINGS ITS CARD FORWARD.
 *
 * Polydactyly, Starter Deck and Little Baggy give a second pocket slot, and the
 * game's own touch button uses what is under it at once: 0x86448c rotates the
 * queue up to the tapped slot and publishes "use" in the same breath. One
 * careless tap and the card the player was saving is gone.
 *
 * The School Bag was settled the same way and by the same hand (bag.c,
 * 09.09.2026): ANY press of the second button is an exchange alone, and using
 * is the first button's business, as on a keyboard. The owner asked for the
 * consumables to follow it (13.09.2026), so here it is - a tap on a slot past
 * the first brings that card or pill to the front and stops there. The next tap
 * lands on the first button, and that one uses.
 *
 * A POCKET ACTIVE is left alone. Repentance keeps one in a pocket slot beside
 * the cards, and it is the only thing in that row that has no exchange to make:
 * bringing a Bag of Crafting forward instead of opening it would take the item
 * away from the player entirely. The kind the game itself reads is the test.
 *
 * The rotation is asked for on the FRAME thread, where the player is handed to
 * us anyway - the same place and the same call the long press uses.
 */


#define AREA 0xabec64               /* the drawing area: width, height, origin x, origin y */
#define CANVAS_H (shim_screen_height())

/*
 * The canvas is no longer a constant: it is as wide as the screen (screen.c),
 * so every place that used to say 480 asks the game what it is laying out
 * against right now.
 */
float shim_screen_width(void);
float shim_screen_height(void);
#define CANVAS_W (shim_screen_width())

#define INPUT_SYSTEM 0xac5528       /* the object itself, not a pointer to it */
#define FN_PRESS     0x8b6960       /* (input, action, strength, ctx) */
#define FN_ROTATE    0x54a424       /* (player): the pocket queue moves on by one */
#define INPUT_DROP   11

#define POCKETS 4

/*
 * What each pocket slot holds, kept for the touch thread (see POCKET_ACTIVE).
 *
 * Both words, not just the kind: a slot we have not read yet reads as nothing,
 * and nothing is refused. The first press of a run used to be taken before the
 * frame thread had filled these in - the report caught exactly one such finger.
 */
static volatile int g_what[ROWS][POCKETS];
static volatile signed char g_kind[ROWS][POCKETS];

/*
 * Touched from two threads: fingers arrive on the layout thread, the decision
 * is made on the frame thread. Without the lock a finger let go at the very
 * moment the press turns long would be handed over as a tap AND asked to drop.
 */
static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;

static unsigned long g_overlay;             /* the touch overlay, met once a frame */

static int g_id = -1;                       /* the finger being held back */
static int g_bring = 0;                     /* slots to rotate: a tap that only exchanges */
static int g_bring_row = 0;                 /* ... of that row's player */
static float g_down_x, g_down_y;
static long g_down_ms;
static int g_eaten;                         /* it became a drop */
static int g_let_go;                        /* it turned out to be a tap */
static int g_slot;                          /* which pocket the finger is on */
static int g_row;                           /* in which row: 0 the touch player's, 1 his twin's */
static volatile int g_dropping;             /* the frame thread asks for the action */
static int g_chosen;                        /* the pocket has been brought to the front */

static long now_ms(void) {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (long) t.tv_sec * 1000 + t.tv_nsec / 1000000;
}

/* the overlay, remembered where we already stand every frame (hud.c) */
void shim_pocket_overlay(unsigned long self) {
    if (self) g_overlay = self;
}

/* the overlay itself, for whoever else needs to know which one is live */
unsigned long shim_pocket_overlay_at(void) { return g_overlay; }

/*
 * A touch, from screen pixels into the canvas units the pocket boxes are in -
 * the PICTURE's, as ItemButton::HitTest takes them (0x864034 -> vtable +0x10
 * -> 0x86255c; screen.c, "one finger, two spaces"). The press held back here is
 * the press the game would use the card with, FULLSCREEN on or off.
 */
int shim_screen_finger_units(float x, float y, float *u, float *v);

static int to_field(float x, float y, float *gx, float *gy) {
    return shim_screen_finger_units(x, y, gx, gy);
}

/*
 * Is this touch, in screen pixels, on one of the pocket buttons?
 *
 * The four are taken by arithmetic and then made to prove themselves: the class
 * must be ItemButton, the widget must be showing, and the box must be a box. A
 * button that is not there refuses the finger, and the finger goes to the game
 * as it always did.
 */
static int on_pocket(float x, float y, int *row, int *slot) {
    unsigned long overlay = g_overlay;
    if (!overlay) return 0;
    unsigned long b = shim_image_base();
    if (!b) return 0;
    float gx, gy;
    if (!to_field(x, y, &gx, &gy)) return 0;
    for (int r = 0; r < ROWS; r++) {
      /* the twin's group only while it is live (see ROW_STRIDE) */
      if (r > 0 && !*(const unsigned char *) (overlay + POCKET_ROW + (unsigned long) r * ROW_STRIDE)) continue;
      for (int i = 0; i < POCKETS; i++) {
        /* ours only when the slot really holds a card or a pill */
        if (g_what[r][i] == 0 || g_kind[r][i] == POCKET_ACTIVE) continue;
        unsigned long w = overlay + POCKET_FIRST + (unsigned long) r * ROW_STRIDE
                        + (unsigned long) i * POCKET_STRIDE;
        if (*(const unsigned long *) w != b + VT_ITEMBUTTON) continue;
        if (!*(const unsigned char *) (w + WIDGET_SHOWN)) continue;
        const float *c = (const float *) (w + ITEM_CENTRE);
        const float *s = (const float *) (w + ITEM_EXTENT);
        if (!isfinite(c[0]) || !isfinite(c[1]) || !isfinite(s[0]) || !isfinite(s[1])) continue;
        if (s[0] < 2.0f || s[0] > 128.0f || s[1] < 2.0f || s[1] > 128.0f) continue;
        if (fabsf(gx - c[0]) <= s[0] * 0.5f && fabsf(gy - c[1]) <= s[1] * 0.5f) {
            *row = r;
            *slot = i;
            DIAG(static int said;
                 if (said < 6) { said++;
                     LOG("pocket: finger on row %d button %d — box %.1f,%.1f size %.1f x %.1f",
                         r + 1, i, (double) c[0], (double) c[1], (double) s[0], (double) s[1]); });
            return 1;
        }
      }
    }
    return 0;
}

/* does a tap on this slot exchange instead of using: see the note above */
static int exchanges(int row, int slot) {
    if (row < 0 || row >= ROWS || slot <= 0 || slot >= POCKETS) return 0;
    if (!g_what[row][slot]) return 0;           /* nothing there to bring forward */
    return g_kind[row][slot] != POCKET_ACTIVE;
}

static void forget(void) {
    g_id = -1;
    g_eaten = 0;
    g_let_go = 0;
    g_slot = 0;
    g_row = 0;
    g_chosen = 0;
}

void shim_pocket_forget(void) {
    pthread_mutex_lock(&g_lock);
    g_dropping = 0;
    forget();
    pthread_mutex_unlock(&g_lock);
}

/*
 * PROBE, diagnostic builds only: the pocket road of the overlay, which the
 * walk calls once a frame for every pocket button that is being held
 * (0x86448c, and it publishes the button's action at entry+0x14 on every one
 * of those calls). A press that lasts a second writes sixty lines, so "is it
 * being held or not" is answered by counting them.
 */
void shim_pocket_action(unsigned long entry, int (*readable)(const void *, unsigned long)) {
    int shim_report_active(void);
    void report_line(const char *, const char *);
    static int said;
    if (said >= 120 || !shim_report_active()) return;
    if (!entry || !readable((const void *) entry, 0x18)) return;
    unsigned long w = *(const unsigned long *) entry;
    if (!w || !readable((const void *) w, 0x194)) w = 0;
    said++;
    char line[160];
    snprintf(line, sizeof(line), "slot %d what %d action %d/%d held %d frames %d %.3fs",
             *(const int *) (entry + 0x08), *(const int *) (entry + 0x0c),
             *(const int *) (entry + 0x10), *(const int *) (entry + 0x14),
             w ? *(const unsigned char *) (w + 0x168) : 0,
             w ? *(const int *) (w + 0x188) : 0,
             w ? (double) *(const float *) (w + 0x190) : 0.0);
    report_line("held", line);
}

/*
 * A touch, before it reaches the queue. Answers 1 if the event is ours and the
 * game must not see it - yet, or at all.
 */
int shim_pocket_touch(int id, int phase, float x, float y) {
    int answer = 0;
    pthread_mutex_lock(&g_lock);
    if (phase == 0) {
        int row = 0, slot = 0;
        if (g_id < 0 && shim_sticks_in_game() && on_pocket(x, y, &row, &slot)) {
            g_id = id;
            g_row = row;
            g_slot = slot;
            g_chosen = 0;
            g_down_x = x;
            g_down_y = y;
            g_down_ms = now_ms();
            g_eaten = 0;
            g_let_go = 0;
            answer = 1;                         /* held back while we decide */
        }
    } else if (id == g_id && !g_let_go) {
        if (g_eaten) {                          /* it became a drop: eat the rest */
            if (phase >= 3) { g_dropping = 0; forget(); }
            answer = 1;
        } else if (phase == 1) {
            if (fabsf(x - g_down_x) <= WANDER && fabsf(y - g_down_y) <= WANDER) {
                answer = 1;                     /* still on the button, still deciding */
            } else {
                /* wandered off: an ordinary touch after all, hand it over */
                shim_touch_push_raw(g_id, 0, g_down_x, g_down_y);
                g_let_go = 1;
            }
        } else {
            if (exchanges(g_row, g_slot)) {
                /*
                 * A tap on the second card: the finger is eaten and the queue
                 * is turned instead, so the card comes to the front unused.
                 */
                g_bring = g_slot;
                g_bring_row = g_row;
                forget();
                answer = 1;
            } else {
                /*
                 * Let go early - a tap. The press goes in now and the release
                 * straight after it: the game drains the queue once a frame and
                 * sees a press and a release, which is what a tap is, and the
                 * card is used exactly as it always was.
                 */
                shim_touch_push_raw(g_id, 0, g_down_x, g_down_y);
                shim_touch_push_raw(g_id, phase, x, y);
                forget();
                answer = 1;
            }
        }
    }
    pthread_mutex_unlock(&g_lock);
    return answer;
}

/*
 * Between frames: notice that the press has become long. Nothing is published
 * from here - see below for why.
 */
void shim_pocket_between_frames(void) {
    pthread_mutex_lock(&g_lock);
    if (g_id >= 0 && !g_eaten && !g_let_go && now_ms() - g_down_ms >= HOLD_MS) {
        g_eaten = 1;
        g_dropping = 1;
        LOG("pocket: held long - the card or pill is dropped, not used");
    }
    pthread_mutex_unlock(&g_lock);
}

/*
 * Asking for the drop, at the one moment in the frame where the answer sticks.
 *
 * The overlay publishes INPUT_DROP ITSELF, every single frame, from 0x86125c -
 * with a strength of one while the trinket's button is down and a strength of
 * ZERO the rest of the time. So an answer written between frames is wiped by
 * that zero before the game ever reads it, which is exactly what happened: the
 * log showed `action 11 pressed at 1.00` on every long press and the card
 * stayed in the pocket.
 *
 * 0x861d1c - the walk over the pressed widgets - is called from 0x861338, the
 * next thing after those publishes and before the game is asked what is held.
 * Writing here lands after the zero and lives. The ctx is the one the game
 * itself is using at that moment, handed to us as the fourth argument, so
 * there is nothing to work out and nothing to get wrong.
 */
void shim_pocket_publish(unsigned long group, unsigned long player, unsigned long ctx) {
    /*
     * EACH ROW ITS OWN. The walk runs once per row of the overlay (0x860d44):
     * 0x861338 for row one - the touch player's, overlay + 0x20fb0 - and then
     * 0x86136c for row two, overlay + 0x21e48, which carries Jacob's twin Esau
     * (and a null player for everybody else). Each call answers for its own
     * row's buttons only: one shared set of slots left Esau's pockets in it,
     * and Jacob's buttons were judged by his brother's cards. The drop is
     * published once a frame, from the walk of the row the finger is on - it
     * goes to the device, which both twins read.
     */
    unsigned long overlay = g_overlay;
    if (!overlay) return;                       /* no finger can be ours yet */
    int row;
    if (group == overlay + POCKET_ROW) row = 0;
    else if (group == overlay + POCKET_ROW + ROW_STRIDE) row = 1;
    else return;                                /* the overlay was built again: next frame */

    /*
     * A tap that only exchanges (see the note above): the queue is turned here,
     * on the frame thread, by the same call the long press uses - for the
     * player of the row the tap was in. Nothing is published - the card is
     * brought forward and left alone.
     */
    pthread_mutex_lock(&g_lock);
    int bring = 0;
    if (g_bring > 0 && g_bring_row == row) {
        bring = g_bring;
        g_bring = 0;
    }
    pthread_mutex_unlock(&g_lock);
    if (bring > 0 && player) {
        unsigned long base = shim_image_base();
        if (base) {
            for (int i = 0; i < bring; i++)
                ((void (*)(unsigned long)) (base + FN_ROTATE))(player);
            LOG("pocket: tapped %d in row %d — brought forward, not used", bring, row + 1);
        }
    }

    /*
     * What this row's four slots hold, for the touch thread; see POCKET_ACTIVE
     * above. A row with no player has no cards: nothing of it is held back.
     */
    if (player) {
        const int *pair = (const int *) (player + PLAYER_POCKET);
        for (int i = 0; i < POCKETS; i++) {
            g_kind[row][i] = (signed char) pair[i * 2 + 1];
            g_what[row][i] = pair[i * 2];
        }
        {
            int shim_report_active(void);
            void report_line(const char *, const char *);
            static int said[ROWS];
            if (said[row] < 6 && shim_report_active()) {
                said[row]++;
                char line[120];
                snprintf(line, sizeof(line), "row %d slots %d/%d %d/%d %d/%d %d/%d (what/kind)",
                         row + 1, pair[0], pair[1], pair[2], pair[3],
                         pair[4], pair[5], pair[6], pair[7]);
                report_line("pocket", line);
            }
        }
    } else {
        for (int i = 0; i < POCKETS; i++) {
            g_kind[row][i] = 0;
            g_what[row][i] = 0;
        }
    }

    /* the finger's row asks for the drop, and only it: once a frame */
    pthread_mutex_lock(&g_lock);
    int dropping = g_dropping && g_row == row;
    int slot = g_slot, choose = dropping && !g_chosen;
    if (choose) g_chosen = 1;
    pthread_mutex_unlock(&g_lock);
    if (!dropping) return;
    unsigned long b = shim_image_base();
    if (!b) return;

    /*
     * WHICH card is dropped: the one the finger is on, not the one that
     * happens to be at the front.
     *
     * A tap does this itself. 0x86448c takes the slot number the button
     * carries and calls Entity_Player pocket rotation (0x54a424) that many
     * times before it publishes anything - one call for slot 1, two for slot 2,
     * three for slot 3, straight off the jump table at 0x95b520. The rotation
     * walks player+0x1c10 in steps of eight and swaps the halves of each pair,
     * bringing the tapped pocket to the front.
     *
     * Our long press eats the finger, so that never happens by itself, and with
     * a single card it made no difference - there was nothing to bring forward.
     * With Polydactyly there are two, and a long press on the second one would
     * have thrown away the first. So we rotate exactly as a tap would, once per
     * gesture.
     */
    if (choose) {
        if (player) {
            for (int i = 0; i < slot; i++)
                ((void (*)(unsigned long)) (b + FN_ROTATE))(player);
        }
        DIAG(LOG("pocket: the drop is asked for — row %d pocket %d brought forward, ctx %lu",
                 row + 1, slot, ctx));
    }
    ((void (*)(unsigned long, int, float, unsigned long)) (b + FN_PRESS))(
        b + INPUT_SYSTEM, INPUT_DROP, 1.0f, ctx);
}
