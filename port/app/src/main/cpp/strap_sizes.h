/* Written by tools/strap_lang/strap_install.py - do not edit by hand, run the tool. */
#ifndef STRAP_SIZES_H
#define STRAP_SIZES_H

/*
 * Each language set's straps as drawn (other stuff/straps-lang): the picture's
 * width and height and how much taller than the owner's it rises at the top.
 * A tap takes the strap's box from here: x from the paper edge to the
 * picture's right end (width - 6, the columns lengthened under the paper),
 * y from the owner's top minus the rise, the picture's height down.
 */
enum { STRAP_TWEAKS, STRAP_GUIDE, STRAP_SPAWNER, STRAP_CREDITS, STRAP_KINDS };
#define STRAP_EXTEND 6
static const struct { short w, h, rise; } STRAP_SIZE[7][STRAP_KINDS] = {
    { { 78, 55, 0 }, { 76, 47, 0 }, { 78, 55, 0 }, { 78, 55, 0 } },  /* en */
    { { 78, 55, 0 }, { 76, 47, 0 }, { 78, 55, 0 }, { 78, 55, 0 } },  /* ru */
    { { 78, 55, 0 }, { 76, 47, 0 }, { 78, 55, 0 }, { 78, 55, 0 } },  /* de */
    { { 82, 58, 3 }, { 76, 47, 0 }, { 78, 55, 0 }, { 78, 55, 0 } },  /* es */
    { { 106, 66, 11 }, { 76, 47, 0 }, { 78, 55, 0 }, { 126, 74, 19 } },  /* jp */
    { { 83, 58, 3 }, { 79, 49, 2 }, { 78, 55, 0 }, { 78, 55, 0 } },  /* kr */
    { { 113, 69, 14 }, { 76, 47, 0 }, { 78, 55, 0 }, { 111, 68, 13 } },  /* zh */
};

#endif
