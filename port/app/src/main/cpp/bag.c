/*
 * The School Bag: switching the two active items without using one.
 *
 * The bag gives a second active slot, and on a touch screen the overlay grows
 * a second item button for it. A tap on that button does two things at once -
 * TouchOverlay::ItemButtonPressed (0x864434) reads the slot number the button
 * carries (entry+8), and for slot 1 calls Entity_Player::SwapActiveItems
 * (0x5390f8) and then publishes the button's action, which is "use the item".
 * From a button there is no road that only swaps. (There is one from the
 * DROP action, 0x52ad78 - a short press of it swaps the actives, among other
 * things - but nothing on the phone publishes a short press of drop; see the
 * die below.)
 *
 * So switching always tries to use. That is the game's own design, and it is
 * awkward when the item that is switched TO cannot be used at all. The owner
 * asked for switching on its own - first as a long press beside the game's
 * tap, then (09.09.2026) as the only thing the bag's button does: a press
 * swaps, and using is the main button's job, as on the desktop.
 *
 * How this one is made, and why it is small:
 *
 *   The swap itself refuses nothing. 0x5390f8 checks one thing, that the
 *   second slot holds an item (player+0x1980), and swaps the two blocks of
 *   0x1c bytes at player+0x1964 and player+0x1980. Whether the item can be
 *   used never enters into it.
 *
 *   0x864434 does nothing at all for a slot number above one - it returns
 *   before both the swap and the publish. So a long press needs no patch in
 *   the image: we do the swap ourselves and write 2 into the slot number, and
 *   the game's own code declines the rest. The number is safe to write over:
 *   the whole list is rebuilt from the player every frame (0x861418 resets the
 *   three end pointers before refilling them), so what we leave there lives
 *   until the next update and no longer.
 *
 *   How long the finger has been down the widget counts itself: +0x190 is a
 *   float, raised by 0.016667 - one frame at sixty - on every update where the
 *   touch is still inside (0x862268), and zeroed the moment it is not held
 *   (0x862288), and +0x188 counts the updates from one on the frame of the
 *   grab. So a press and its length are read, not measured by us.
 */
#include <stdio.h>
#include <android/log.h>
#include "diag.h"

#if ISAAC_DIAG
#define LOG(...) __android_log_print(ANDROID_LOG_INFO, "isaac", __VA_ARGS__)
#else
#define LOG(...) do { } while (0)
#endif

unsigned long shim_image_base(void);

#define SWAP_ACTIVES 0x5390f8       /* Entity_Player::SwapActiveItems */
#define ENTRY_WIDGET 0x00           /* the widget this action belongs to */
#define ENTRY_SLOT   0x08           /* int: which active slot, 1 is the bag's */
#define SLOT_BAG     1
#define SLOT_NONE    2              /* anything above one and 0x864434 declines */
#define WIDGET_HELD  0x168          /* byte: a finger is on it */
#define WIDGET_FRAMES 0x188         /* int: updates held, 1 on the frame of the grab */
#define WIDGET_HOLD  0x190          /* float: seconds held, zeroed on release */
#define HOLD_SECONDS 0.4f           /* longer than a tap, shorter than a wait */

/* which press we have already served, so one press swaps once */
static unsigned long g_press_widget;

/*
 * D Infinity: turning the die from the touch screen.
 *
 * WHY THE GAME'S OWN ROAD CANNOT DO IT HERE. Measured, 11.09.2026, on the
 * owner's phone: the press is taken, the drop is published, the device holds
 * `cur[11] = 1.00`, the game's road at 0x52acd8 runs in the very next frame
 * and READS that same 1.00 - and the die stays as it was. The reason is one
 * line into that road:
 *
 *     0052ad1c  ldr  w20, [x19, #0x19ec]      the player's controller
 *     0052ad44  cmp  w20, #0x0010DEAD         the TOUCH pseudo-controller
 *     0052ad48  b.ne #0x52ad78                a real controller: the die road
 *     0052ad4c  mov  w1, #0x11                touch: action 17 instead
 *     0052ad5c  bl   #0x671c5c                       and 22 after it
 *
 * The report printed `controller 1105581`, which is 0x10DEAD exactly. So for
 * a player on the touch screen the road consults actions 17 and 22 - the two
 * the overlay publishes for itself at 0x8612f0 and 0x8612d0 - and the whole
 * block from 0x52ad78 to 0x52ae0c, where the pocket is rotated and an active
 * of 489 has its form stepped, is on the other side of that branch and is
 * never reached. Publishing 11, however correctly, could never have turned
 * the die. That also corrects the note in the handover: it is not "no
 * trinket, no die on iOS" - the die cannot be turned from a touch screen at
 * all, trinket or not.
 *
 * Action 11 IS read further down the same road, and that part is common to
 * both branches - 0x52ae90, where a press held for 121 frames drops the card
 * (player+0x1c30 counts them, and a frame without the press clears it). That
 * is the road pocket.c uses, and it is the reason a one-frame publish of 11
 * was harmless rather than merely useless: the counter reached one and was
 * cleared on the next frame.
 *
 * SO THE STEP IS TAKEN HERE, and it is the game's own step, instruction for
 * instruction (0x52adbc..0x52adfc):
 *
 *     ldrsh w8, [x19, #0x197e]      the form the die is showing
 *     add   w8, w8, #1              the next one
 *     ... umull/msub by 10 ...      (idx + 1) % 10
 *     strh  w23, [x19, #0x197e]
 *     ldr   x8, [0xac3b90]          the game
 *     add   x0, x8, #0x47fe0        the HUD's row of players
 *     bl    #0x6ebd10(hud, player, 0)   "slot 0 has changed, draw it again"
 *
 * What is deliberately NOT copied is the pocket rotation (0x54a424) that
 * stands before it on the game's road: that belongs to the drop press, which
 * on a keyboard does both at once, and our gesture is a press on the ACTIVE
 * item's button - swapping the player's cards behind it would be a surprise,
 * not a feature. The wrap to zero does not swap the actives here either: the
 * touch player has the bag's own button for that. On a pad or a keyboard the
 * game's drop road keeps its own rule - the tenth turn swaps (as on the PC).
 *
 * THE GESTURE. A long press on the item button, taken only while the active
 * item IS D Infinity - for anything else the button stays the game's, because
 * a long press on an item that is used by holding (Spin to Win) is the hold
 * itself. It cannot be built on top of the game's own press: the handler at
 * 0x864434 publishes "use" on the very first frame the button is down, and
 * the die would be rolled before the press had a chance to turn long. So the
 * press is taken away from the handler on every frame (the slot number set
 * above one, as the bag does) and answered from the walk over the pressed
 * widgets instead: held for HOLD_SECONDS, the die is turned once; let go
 * before that, the button's own action is published for one frame - the tap
 * it would have been, only later by the length of the press.
 */
#define ACTIVE_ITEM   0x1964        /* int in the player: the active item's id */
#define D_INFINITY    489
#define ENTRY_ACTION  0x10          /* int: the action the button publishes - "use" */
#define INPUT_SYSTEM  0xac5528      /* the object itself, not a pointer to it */
#define FN_PRESS      0x8b6960      /* (input, action, strength, ctx) */

#define DIE_FORM      0x197e        /* short in the player: which die is showing */
#define DIE_FORMS     10            /* the step wraps here, as the game's own does */
#define GAME_PTR      0xac3b90      /* the game object, a pointer */
#define HUD_PLAYERS   0x47fe0       /* the HUD's row of players, inside the game */
#define FN_HUD_CHANGED 0x6ebd10     /* (hud players, player, slot): draw that slot again */
#define ACTIVE_SLOT   0            /* the main slot: what the button we took belongs to */

static unsigned long g_die_widget;  /* the item button, while the press is ours */
static int g_die_action;            /* what it would have published */
static int g_die_served;            /* the die has been turned for this press */
static int g_die_unclaimed;         /* walks in a row whose row did not hold the button */

static int die_in_hand(unsigned long player, int (*readable)(const void *, unsigned long)) {
    if (!player || !readable((const void *) player, ACTIVE_ITEM + 4)) return 0;
    return *(const int *) (player + ACTIVE_ITEM) == D_INFINITY;
}

static void tell_die(const char *text) {
    int shim_report_active(void);
    void report_line(const char *, const char *);
    static int said;
    if (said >= 40 || !shim_report_active()) return;
    said++;
    report_line("die", text);
}

/* the game's own step, and the game's own "this slot has changed" after it */
#define HUD_ROW    0x918            /* one player's row; 0x6ebd10 walks eight of them */
#define HUD_ROWS   8

static int die_turn(unsigned long b, unsigned long player,
                    int (*readable)(const void *, unsigned long)) {
    /*
     * Still the die? The press was taken while it was, and a School Bag can
     * bring another active forward in the half second since. +0x197e is the
     * shared VarData of whatever active is in hand, so stepping it blind
     * would quietly change something else.
     */
    if (!die_in_hand(player, readable)) return 0;
    if (!readable((const void *) player, DIE_FORM + 2)) return 0;
    short *form = (short *) (player + DIE_FORM);
    int was = *form;
    int now = ((was + 1) % DIE_FORMS + DIE_FORMS) % DIE_FORMS;   /* a stray -1 cannot escape */
    *form = (short) now;
    unsigned long game = *(const unsigned long *) (b + GAME_PTR);
    if (game && readable((const void *) (game + HUD_PLAYERS), HUD_ROW * HUD_ROWS))
        ((void (*)(unsigned long, unsigned long, int)) (b + FN_HUD_CHANGED))
            (game + HUD_PLAYERS, player, ACTIVE_SLOT);
    DIAG(LOG("die: held - the form is stepped %d -> %d", was, now));
    {
        char line[120];
        snprintf(line, sizeof(line), "held: the form is stepped %d -> %d", was, now);
        tell_die(line);
    }
    return 1;
}

/*
 * ONE ROW AT A TIME. The walk runs once per row of the overlay (0x860d44):
 * 0x861338 for row one, the touch player's (overlay + 0x20fb0), then 0x86136c
 * for row two (overlay + 0x21e48), which carries Jacob's twin Esau. Both calls
 * come here. Answering the first one for whatever button was held meant that a
 * tap on ESAU's D Infinity was let go in Jacob's call: Jacob had no die, so
 * nothing was published, the press was forgotten - and Esau's tap was lost.
 * So a held button is answered only by the walk of the row it stands in: its
 * widgets live inside the row's 0xe98 bytes (ItemButtons from +0x10, the
 * actives from +0x730, 0x8613a8), and the player handed with that walk is the
 * one the button belongs to.
 */
#define ROW_SIZE 0xe98              /* one row of the overlay: the next starts here (0x8604c0) */

/* the walk over the pressed widgets, once a frame, after the overlay's own publishes */
void shim_bag_walk(unsigned long group, unsigned long player, unsigned long ctx,
                   int (*readable)(const void *, unsigned long)) {
    unsigned long b = shim_image_base();
    if (!b) return;
    unsigned long w = g_die_widget;
    if (!w) return;
    if (!group || w < group || w >= group + ROW_SIZE) {
        /*
         * Another row's button - or no row's at all: the overlay is built anew
         * when the canvas changes (FULLSCREEN, entering a run), and a press
         * held across that keeps an address no walk will ever claim. Two walks
         * a frame come here, so a button unclaimed for a few frames is let go,
         * or it would hold every later press of the die hostage.
         */
        if (++g_die_unclaimed > 8) {
            g_die_widget = 0;
            g_die_served = 0;
            g_die_unclaimed = 0;
        }
        return;
    }
    g_die_unclaimed = 0;
    if (!readable((const void *) w, WIDGET_HOLD + 4)) { g_die_widget = 0; return; }
    if (*(const unsigned char *) (w + WIDGET_HELD)) {
        if (!g_die_served && *(const float *) (w + WIDGET_HOLD) >= HOLD_SECONDS)
            g_die_served = die_turn(b, player, readable);
        return;
    }
    /* let go */
    if (!g_die_served && die_in_hand(player, readable)) {
        void (*press)(unsigned long, int, float, unsigned long) =
            (void (*)(unsigned long, int, float, unsigned long)) (b + FN_PRESS);
        press(b + INPUT_SYSTEM, g_die_action, 1.0f, ctx);
        DIAG(LOG("die: a tap after all - the item is used"));
        tell_die("let go early: a tap - the item is used");
    }
    g_die_widget = 0;
    g_die_served = 0;
}

void shim_bag_press(unsigned long entry, unsigned long player,
                    int (*readable)(const void *, unsigned long)) {
    if (!entry || !readable((const void *) entry, ENTRY_ACTION + 4)) return;
    int slot = *(const int *) (entry + ENTRY_SLOT);
    if (slot == 0) {
        /*
         * The main button. Ours only while the die is the active item; the
         * slot number is put out of the handler's reach on every frame of the
         * press, and the walk answers for it.
         */
        if (g_die_widget == 0 && !die_in_hand(player, readable)) return;
        unsigned long w = *(const unsigned long *) (entry + ENTRY_WIDGET);
        if (!w) return;
        if (g_die_widget == 0) {
            g_die_widget = w;
            g_die_unclaimed = 0;
            g_die_action = *(const int *) (entry + ENTRY_ACTION);
            g_die_served = 0;
            {
                char line[160];
                snprintf(line, sizeof(line),
                         "the press is taken: the die on widget %lx, action %d, player %lx",
                         w, g_die_action, player);
                tell_die(line);
            }
        }
        if (w == g_die_widget) *(int *) (entry + ENTRY_SLOT) = SLOT_NONE;
        return;
    }
    if (slot != SLOT_BAG) return;
    if (!player || !readable((const void *) player, 0x19a0)) return;
    unsigned long w = *(const unsigned long *) (entry + ENTRY_WIDGET);
    if (!w || !readable((const void *) w, WIDGET_FRAMES + 4)) return;
    /*
     * Every press of the bag's button swaps and never uses - the owner's
     * choice, 09.09.2026, over the game's own "swap and use at once": to use
     * the item just brought forward, tap the main button. As on the desktop,
     * where the drop key swaps and the item key uses.
     *
     * The slot number is put out of the game's reach on every call, so the
     * handler never uses behind us; the swap itself happens once per press -
     * the widget counts its own updates from one on the frame of the grab.
     */
    int frames = *(const int *) (w + WIDGET_FRAMES);
    int fresh = (w != g_press_widget || frames <= 1);
    g_press_widget = w;
    *(int *) (entry + ENTRY_SLOT) = SLOT_NONE;
    if (!fresh) return;
    unsigned long b = shim_image_base();
    if (!b) return;
    ((void (*)(unsigned long)) (b + SWAP_ACTIVES))(player);
    DIAG(LOG("bag: pressed - the actives are swapped, nothing is used"));
}
