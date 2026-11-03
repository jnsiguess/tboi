/* Written by tools/itemspawn_title.py - do not edit by hand, run the tool. */
#ifndef SPAWNER_TITLE_H
#define SPAWNER_TITLE_H

/* one per language set, in the order of tweaks_layout.h: width and height */
static const struct { const char *suffix; short w, h; } SP_TITLE[7] = {
    { "en", 107, 27 },
    { "ru", 105, 27 },
    { "de", 109, 27 },
    { "es", 108, 27 },
    { "jp", 125, 31 },
    { "kr", 50, 36 },
    { "zh", 63, 25 },
};

#endif
