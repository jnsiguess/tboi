# -*- coding: utf-8 -*-
"""What an edge-aware upscaler would do to the picture, rendered on the desk.

The Filter option blows the canvas (585 x 270) up to the screen (3120 x 1440),
5.33 pixels to a texel. The game's own shader does it by snapping to source
texels, which is why it comes out in blocks whatever else is changed - measured
and written up in shaders/hq4x.fs.

Writing the replacement straight into GLSL and looking at it on the phone costs
a round of the owner's time per guess, and the guesses are in the weights, where
a mistake does not look like a mistake. So the same arithmetic is done here,
over the real art at its real size, and the answer is a picture he can hold
against the PC's.

    py -3 tools/upscale_preview.py [--scale 5.333]
"""
import os
import sys

import numpy as np
from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
PAPER = (233, 218, 223)          # what the options sheet is drawn on
SCALE = 3120.0 / 585.0           # the owner's screen against the canvas

# How close two colours must be to count as one, in luminance out of 255. The
# menu's ink and its light edge are 100 apart, so anything well under that keeps
# them apart while tying a colour to itself across an edge.
SAME = 24.0


def lum(a):
    return a[..., 0] * 0.299 + a[..., 1] * 0.587 + a[..., 2] * 0.114


def on_paper(im):
    """The label as the screen has it: opaque, over the sheet's own colour."""
    a = np.asarray(im.convert('RGBA')).astype(np.float64)
    al = a[..., 3:4] / 255.0
    return a[..., :3] * al + np.array(PAPER, dtype=np.float64) * (1.0 - al)


def nearest(src, w, h):
    ys = np.minimum((np.arange(h) / SCALE).astype(int), src.shape[0] - 1)
    xs = np.minimum((np.arange(w) / SCALE).astype(int), src.shape[1] - 1)
    return src[ys][:, xs]


def edge_aware(src, w, h, line=1.5, slope=False):
    """A corner-cutting upscaler of the xBR family, written out plainly.

    For each of the four corners of a source texel the two neighbours meeting
    there are asked whether they are the same colour as each other and a
    different one from the texel. That is a staircase corner: the texel pokes
    into its neighbours' colour. The corner is then cut along the diagonal and
    filled with their colour, and the cut is softened over one screen pixel, so
    the edge comes out smooth instead of stepped.

    The four corners are done by turning the neighbourhood a quarter turn at a
    time, so there is one piece of arithmetic rather than four.
    """
    sh, sw = src.shape[0], src.shape[1]
    ys = np.arange(h) / SCALE
    xs = np.arange(w) / SCALE
    gy, gx = np.meshgrid(ys, xs, indexing='ij')
    iy, ix = np.floor(gy).astype(int), np.floor(gx).astype(int)
    fy, fx = gy - iy, gx - ix

    def at(dx, dy):
        yy = np.clip(iy + dy, 0, sh - 1)
        xx = np.clip(ix + dx, 0, sw - 1)
        return src[yy, xx]

    out = at(0, 0).copy()
    soft = 1.0 / SCALE               # one screen pixel, measured in texels

    for turn in range(4):
        # a quarter turn counter-clockwise each time: (dx,dy) -> (dy,-dx),
        # which walks the corner round DR, UR, UL, DL
        def spin(dx, dy, t=turn):
            for _ in range(t):
                dx, dy = dy, -dx
            return dx, dy

        def s(dx, dy):
            return at(*spin(dx, dy))

        px, py = (fx, fy)
        for _ in range(turn):
            px, py = px, 1.0 - py
            px, py = py, px          # the same quarter turn, applied to the point
        E, F, H = s(0, 0), s(1, 0), s(0, 1)

        near = np.abs(lum(F) - lum(H)) < SAME          # the two neighbours agree
        apart = (np.abs(lum(E) - lum(F)) > SAME) & (np.abs(lum(E) - lum(H)) > SAME)
        edge = near & apart

        # how far into the corner triangle this screen pixel lies, softened
        # across one pixel so the diagonal is not itself a staircase
        here = line
        if slope:
            # A SHALLOW EDGE IS CUT FURTHER. If the neighbour's colour carries on
            # past the corner - F looks like the texel beyond H, or the other way
            # about - the edge is a two-to-one slope, not a plain forty-five, and
            # cutting only the little corner leaves the step showing.
            carry = (np.abs(lum(s(1, 0)) - lum(s(1, 2))) < SAME) |                     (np.abs(lum(s(0, 1)) - lum(s(2, 1))) < SAME)
            here = np.where(carry, line - 0.35, line)
        cover = np.clip((px + py - here) / soft + 0.5, 0.0, 1.0)
        cover = np.where(edge, cover, 0.0)[..., None]
        fill = (F + H) * 0.5
        out = out * (1.0 - cover) + fill * cover

    return out


def strip(name, img, scale=1):
    a = np.clip(img, 0, 255).astype(np.uint8)
    im = Image.fromarray(a)
    if scale != 1:
        im = im.resize((im.width * scale, im.height * scale), Image.NEAREST)
    return im


def main():
    global SCALE
    for i, a in enumerate(sys.argv):
        if a == '--scale' and i + 1 < len(sys.argv):
            SCALE = float(sys.argv[i + 1])

    want = None
    for i, a in enumerate(sys.argv):
        if a == '--src' and i + 1 < len(sys.argv):
            want = sys.argv[i + 1]
    src_im = Image.open(want or os.path.join(ROOT, 'menu-labels', 'Filter_80x20_0-440.png'))
    src = on_paper(src_im)
    sh, sw = src.shape[0], src.shape[1]
    w, h = int(sw * SCALE), int(sh * SCALE)
    print('source %d x %d, blown up to %d x %d (%.3f to one)' % (sw, sh, w, h, SCALE))

    ways = [('as it is now, no filter', nearest(src, w, h))]
    for label, line, slope in (('corner cut, gentle', 1.5, False),
                               ('corner cut, full 45', 1.0, False),
                               ('full 45 with 2:1 slopes', 1.0, True)):
        ways.append((label, edge_aware(src, w, h, line, slope)))
    base = ways[0][1]
    for label, img in ways[1:]:
        moved = int((np.abs(base - img).sum(axis=2) > 1.0).sum())
        print('  %-26s moved %6d of %d (%4.1f%%)' % (label, moved, w * h, 100.0 * moved / (w * h)))

    gap = 12
    sheet = Image.new('RGB', (w, len(ways) * (h + gap) - gap), PAPER)
    for i, (label, img) in enumerate(ways):
        sheet.paste(strip(label, img), (0, i * (h + gap)))
    where = os.path.join(os.environ.get('TEMP', '.'), 'filter-preview.png')
    sheet.save(where)
    print('written to', where, ' - top to bottom:')
    for label, _ in ways:
        print('   ', label)


if __name__ == '__main__':
    main()
