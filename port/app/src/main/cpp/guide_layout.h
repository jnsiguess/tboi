/* Written by tools/guide_slides.py - do not edit by hand, run the tool. */
#ifndef GUIDE_LAYOUT_H
#define GUIDE_LAYOUT_H

/* the slides, and where the collection screen puts its pieces (screen units) */
#define GD_SLIDES 19
#define GD_PAPER_X 68
#define GD_PAPER_Y 6
#define GD_PAPER_W 390
#define GD_PAPER_H 185
#define GD_NOTE_X 80
#define GD_NOTE_Y 189
#define GD_ARROW_W 16
#define GD_ARROW_H 32
#define GD_NEXT_X 424
#define GD_BACK_X 86
#define GD_ARROW_Y 82

/* the families of pad pictures (Java PadIcons, pad.c shim_pad_icon_family): the first is the
   main sheet; the others, port_guide_<set>_<name>.anm2, hold the slides that carry a pad
   button, and GD_FAMILY_FRAME is a slide's frame there, -1 for one drawn from the main sheet */
#define GD_FAMILIES 3
#define GD_FAMILY_NAMES { "xbox", "ps", "switch" }
static const signed char GD_FAMILY_FRAME[GD_SLIDES] = { -1, -1, -1, -1, -1, -1, -1, 0, -1, 1, 2, -1, -1, -1, 3, -1, -1, -1, -1 };

#endif
