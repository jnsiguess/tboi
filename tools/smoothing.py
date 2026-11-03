# -*- coding: utf-8 -*-
"""How this game smooths a drawn letter, counted off the untouched sheets.

THE RULE. The ink is the WHOLE letter. The smoothing is ONE further point laid
in every NOTCH OF ITS STAIRCASE - a point outside the ink that has ink above or
below AND ink left or right. A straight edge is left hard.

    the untouched gamemenu sheet, the five main-menu buttons

        NEW RUN 294 light points, CHALLENGES 189, STATS 115, OPTIONS 264
        in a notch: 100%, 100%, 100%, 99%
        touching the ink by a corner only: 0% of all of them
        standing clear of the ink: 0% of all of them
        neighbours: two, for all but a handful

    the untouched optionsmenu sheet, the Latin menu labels

        Fullscreen 12.2% halftones, Controls 14.0%, Language 14.4%, Filter 12.0%
        in a notch: 99.2%, 100%, 100%, 98.6%; touching: 100% of all of them

THE TONE IS THE SAME ONE in both, which is what settles the matter. A label is a
sprite, so its notch carries plain BLACK AT 81 of transparency; a button is
painted into the paper, so its notch carries a flat (159,150,152). Over this
paper the two are the same colour to the digit:

    233 * (1 - 81/255) = 159    218 * ... = 149    223 * ... = 152

WHAT THIS REPLACES. Every tool here used to pick a THRESHOLD WIDTH around 128
and keep whatever fell inside it as a halftone, aiming at a share of 17.5%. That
carves the smoothing OUT of the letter's own rim: the letter comes out thinner
than it was drawn and EVERY edge goes soft, straight ones included. The owner
said the page read as blurred three times over, and was the one who put his
finger on it - on the originals the smoothing is laid over the symbols rather
than gnawing at their edges (13.09.2026). The count above bore him out.

The share is no longer aimed at: it falls out of the shape of the letter, and on
type of the native size it lands in the native 12..15% by itself. It is still
reported, because a share far from that means the letter is the wrong weight.
"""
import numpy as np
from PIL import Image

INK = (54, 47, 45)                  # the ink of the native labels
SPRITE_TONE = (0, 0, 0, 81)         # the notch of a label drawn as a sprite
PAPER_TONE = (159, 150, 152, 255)   # the notch of a button painted into paper
SILHOUETTE = 128                    # from here up the point belongs to the letter


def _neighbours(a):
    up, down = np.roll(a, 1, 0), np.roll(a, -1, 0)
    left, right = np.roll(a, 1, 1), np.roll(a, -1, 1)
    up[0], down[-1] = False, False
    left[:, 0], right[:, -1] = False, False
    return up, down, left, right


def notches(a):
    """The staircase notches of a silhouette, as a mask."""
    up, down, left, right = _neighbours(a)
    return ~a & (up | down) & (left | right)


def coverage(im):
    """How much of each point the letter covers, whatever the picture is made of.

    A picture in mode L IS that coverage already - a bare map, the way the
    credits page keeps it. Converting one to RGBA would hand back an alpha of
    255 everywhere and swallow the whole sheet in ink, which it duly did the
    first time this was shared out (13.09.2026).
    """
    if im.mode == 'L':
        return np.asarray(im)
    return np.asarray(im.convert('RGBA'))[:, :, 3]


def smooth(im, tone=SPRITE_TONE, ink=INK, silhouette=SILHOUETTE):
    """The picture as the game draws it: solid ink, one tone in the notches.

    `im` is anything that carries coverage - a typeset line, a shrunk drawing, a
    bare map - and only that is read. Answers the picture and the share the tone
    came out at.
    """
    a = coverage(im) >= silhouette
    notch = notches(a)
    out = np.zeros(a.shape + (4,), dtype=np.uint8)
    out[a] = tuple(ink) + (255,)
    out[notch] = tuple(tone)
    lit = int(a.sum()) + int(notch.sum())
    return Image.fromarray(out), (100.0 * notch.sum() / lit if lit else 0.0)


def share(im):
    """What share of a finished picture's lit points are not solid ink."""
    a = np.asarray(im.convert('RGBA'))[:, :, 3]
    full = int((a == 255).sum())
    part = int(((a > 0) & (a < 255)).sum())
    return 100.0 * part / (full + part) if full + part else 0.0
