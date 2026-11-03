/*
 * The Bag of Crafting's screen under a finger: a sideways swipe turns a page,
 * a hold locks the list (the owner, 06.10.2026: a tap used to be the next page,
 * and there was no way back).
 *
 * The screen is a touch zone of its own (items.c, on_bag_screen): a finger that
 * goes down on it is the zone's from beginning to end and never reaches the
 * sticks or the buttons. So nothing here has to tell a swipe from a thumb on
 * its way to a stick - it only tells a swipe from a tap and from a hold:
 *
 *   - the finger has gone SWIPE_MIN units sideways from where it went down,
 *     and further sideways than up or down: a page, decided at that very
 *     moment, once per touch. The finger moving RIGHT is the next page, LEFT
 *     the previous one (the owner, 07.10.2026: the other way round read as
 *     backwards to him - he swiped "forward" and landed on the page before);
 *   - it has gone SWIPE_MIN up or down first: nothing, for the rest of the
 *     touch (there is no vertical scroll on the screen);
 *   - let go with neither, after BAG_HOLD_MS: the lock, as before; sooner: a
 *     tap, which does nothing now.
 *
 * Units are the canvas's (screen.c, shim_screen_finger_units) - the same units
 * the screen is laid out in, so the distance is the same on every phone.
 *
 * Pure logic, no state outside the structs: tools/test_bagswipe.c drives it on
 * the desk with made-up fingers, and items.c's whole touch road with it.
 */
#ifndef BAGSWIPE_H
#define BAGSWIPE_H

#include <math.h>

#define SWIPE_MIN    24.0f         /* canvas units: ~9% of the 270-unit height */
#define BAG_HOLD_MS  500

enum { SWIPE_NONE = 0, SWIPE_NEXT = 1, SWIPE_PREVIOUS = -1, SWIPE_LOCK = 2 };

typedef struct {
    float x0, y0;                  /* where the finger went down, canvas units */
    long down_ms;
    int decided;                   /* a page was turned, or the touch went vertical */
} bagswipe_t;

static inline void bagswipe_down(bagswipe_t *s, float x, float y, long now_ms) {
    s->x0 = x;
    s->y0 = y;
    s->down_ms = now_ms;
    s->decided = 0;
}

/* a move: SWIPE_NEXT / SWIPE_PREVIOUS the moment the threshold is crossed, else SWIPE_NONE */
static inline int bagswipe_move(bagswipe_t *s, float x, float y) {
    if (s->decided) return SWIPE_NONE;
    float dx = x - s->x0, dy = y - s->y0;
    if (fabsf(dx) >= SWIPE_MIN && fabsf(dx) > fabsf(dy)) {
        s->decided = 1;
        return dx > 0.0f ? SWIPE_NEXT : SWIPE_PREVIOUS;
    }
    if (fabsf(dy) >= SWIPE_MIN) s->decided = 1;
    return SWIPE_NONE;
}

/* the finger lifted (not cancelled) at x, y: a last look at the move, then the hold */
static inline int bagswipe_up(bagswipe_t *s, float x, float y, long now_ms) {
    int turn = bagswipe_move(s, x, y);
    if (turn != SWIPE_NONE) return turn;
    if (!s->decided && now_ms - s->down_ms >= BAG_HOLD_MS) return SWIPE_LOCK;
    return SWIPE_NONE;
}

/*
 * THE PAGE A SWIPE TURNS TO (eid_craft.c). A page shows as many recipes as fit
 * under the panel, so pages are not all one length and the page before is not
 * simply this page's length back: the starts walked through are kept and
 * walked back. A start at or past where the list is now is stale (the keys
 * moved the list, or a new one began at 0) and is dropped; with none left the
 * step back is this page's length, as the keys take it.
 *
 * bagswipe_turn itself stops at both ends; the swipes and the sticks' clicks
 * go through bagswipe_round below, which goes round (the owner, 07.10.2026:
 * the finger as the pad - past the last page the first, before the first the
 * last). The screen marks its ends - "N more" above the first recipe when
 * there are pages before, below the last when there are pages after.
 */
#define PAGE_STARTS 64

typedef struct { int start[PAGE_STARTS]; int n; } bagpages_t;

/* offset: the first recipe shown; page: how many are shown; total: in the list */
static inline int bagswipe_turn(bagpages_t *p, int offset, int page, int total, int direction) {
    if (page < 1) page = 1;
    if (direction > 0) {
        if (offset + page >= total) return offset;          /* the last page */
        if (p->n < PAGE_STARTS) p->start[p->n++] = offset;
        return offset + page;
    }
    if (direction < 0) {
        while (p->n && p->start[p->n - 1] >= offset) p->n--;
        if (p->n) return p->start[--p->n];
        return offset > page ? offset - page : 0;
    }
    return offset;
}

/*
 * A PAGE ROUND THE ENDS (eid_craft.c): R3 and a swipe right the next page, L3
 * and a swipe left the previous, all going round, as R3 always has (the owner,
 * 07.10.2026: the pad needs a way back, and the finger goes round like it). Past the last page is the first, and the history
 * of starts begins anew; before the first is a last page of this page's
 * length, the tail of the list - how many fit there is not known until it is
 * laid out, and the swipe's history then walks back from it.
 */
static inline int bagswipe_round(bagpages_t *p, int offset, int page, int total, int direction) {
    if (page < 1) page = 1;
    if (direction > 0 && offset + page >= total) { p->n = 0; return 0; }
    if (direction < 0 && offset <= 0) { p->n = 0; return total > page ? total - page : 0; }
    return bagswipe_turn(p, offset, page, total, direction);
}

/*
 * A STICK'S CLICK, a tap or a hold (eid_craft.c): fed whether the button is
 * down and the time, it answers CLICK_HOLD once, the moment the button has
 * been down `hold` long, and CLICK_TAP on a release that came sooner; a
 * release after the hold answers nothing - the hold has had its say. L3 held
 * a second locks the list, R3 held half a second asks for more recipes; a tap
 * of either turns a page.
 */
enum { CLICK_NONE = 0, CLICK_TAP = 1, CLICK_HOLD = 2 };

typedef struct { long long down_at; int down, held; } stickclick_t;

static inline int stickclick_step(stickclick_t *c, int pressed, long long now, long long hold) {
    if (pressed && !c->down) { c->down = 1; c->held = 0; c->down_at = now; return CLICK_NONE; }
    if (pressed && !c->held && now - c->down_at >= hold) { c->held = 1; return CLICK_HOLD; }
    if (!pressed && c->down) { c->down = 0; return c->held ? CLICK_NONE : CLICK_TAP; }
    return CLICK_NONE;
}

#endif
