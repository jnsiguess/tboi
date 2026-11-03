/*
 * The moves a pad or a keyboard makes on one of the port's own pages. They are
 * judged in straps.c, which sees every pad state before the game does, and
 * played by the page itself (tweaks.c).
 */
#ifndef PAGES_H
#define PAGES_H

enum {
    PAGE_UP = 1,
    PAGE_DOWN,
    PAGE_LEFT,
    PAGE_RIGHT,
    PAGE_CONFIRM,
    PAGE_BACK,
};

void shim_tweaks_pad(int action);

#endif
