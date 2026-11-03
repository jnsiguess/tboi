# -*- coding: utf-8 -*-
"""The torn sheet made longer by LENGTHENING ITS LEVEL RUNS.

Quilting random pieces was the wrong answer and the owner said why: the grey
line picked up steps it does not have in the drawing, and the one torn nick at
the bottom came out several times. The sheet is not a texture - it is a line
with a few features on it, and each of them belongs in the picture once.

So nothing is rearranged. Every column of the edge stays in its own order, each
feature exactly once, and the extra length is put into the places where the edge
is LEVEL: a column identical to its neighbour is duplicated, which cannot show a
seam and cannot move the line by a pixel, because the two columns are the same
pixels.

Where the extra length goes was the only choice, and the owner picked the first
of three: shared out by the length of each level run, so the edge keeps the
rhythm he drew and simply has more of the quiet parts (13.09.2026).

    py -3 tools/credits_paper.py [width height]
"""
import os
import sys

import numpy as np
from PIL import Image

SRC = os.path.join(os.path.expanduser('~'), 'Downloads', 'custom credits', 'creditsbg.png')
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
INTO = os.path.join(ROOT, 'port', 'app', 'src', 'main', 'assets',
                    'port_credits.png')
ANM2 = os.path.join(ROOT, 'port', 'app', 'src', 'main', 'assets',
                    'port_credits.anm2')
WANT = (440, 250)
EDGE = 28


def level_runs(strip):
    """Runs of columns identical to the next one: [(start, length), ...]."""
    w = strip.shape[1]
    same = [bool((strip[:, i] == strip[:, i + 1]).all()) for i in range(w - 1)]
    runs, i = [], 0
    while i < len(same):
        if not same[i]:
            i += 1
            continue
        j = i
        while j < len(same) and same[j]:
            j += 1
        runs.append((i, j - i))
        i = j
    return runs


def lengthen(strip, out_len, how):
    """The same columns in the same order, with the level runs made longer."""
    w = strip.shape[1]
    need = out_len - w
    if need <= 0:
        return strip[:, :out_len]
    runs = level_runs(strip)
    if not runs:
        raise SystemExit('the edge has no level run to lengthen')

    if how == 'even':                       # share it out by the length of each run
        weights = [ln for _, ln in runs]
    elif how == 'longest':                  # all of it into the longest runs
        top = sorted(runs, key=lambda r: -r[1])[:max(1, len(runs) // 3)]
        weights = [ln if (st, ln) in top else 0 for st, ln in runs]
    elif how == 'flat':                     # the same amount into every run
        weights = [1] * len(runs)
    total = sum(weights) or 1
    add = [need * wgt // total for wgt in weights]
    for i in range(need - sum(add)):        # the remainder, run by run
        add[i % len(add)] += 1

    pieces, at = [], 0
    for (start, ln), extra in zip(runs, add):
        mid = start + ln // 2               # the middle of the run, where it is flattest
        pieces.append(strip[:, at:mid + 1])
        if extra:
            pieces.append(np.repeat(strip[:, mid:mid + 1], extra, axis=1))
        at = mid + 1
    pieces.append(strip[:, at:])
    out = np.concatenate(pieces, axis=1)
    return out[:, :out_len]


def build(src, how='even'):
    """The grown sheet: the flat inside, the four lengthened edges, the corners."""
    w, h = src.size
    a = np.asarray(src)
    fill_w, fill_h = WANT[0] - 2 * EDGE, WANT[1] - 2 * EDGE
    out = Image.new('RGBA', WANT, (0, 0, 0, 0))
    flat = src.getpixel((w // 2, h // 2))       # the paper is one colour inside
    out.paste(Image.new('RGBA', (fill_w + 2, fill_h + 2), flat), (EDGE - 1, EDGE - 1))

    top = a[0:EDGE, EDGE:w - EDGE]
    bottom = a[h - EDGE:h, EDGE:w - EDGE]
    left = np.transpose(a[EDGE:h - EDGE, 0:EDGE], (1, 0, 2))        # turned into a strip
    right = np.transpose(a[EDGE:h - EDGE, w - EDGE:w], (1, 0, 2))

    out.paste(Image.fromarray(lengthen(top, fill_w, how)), (EDGE, 0))
    out.paste(Image.fromarray(lengthen(bottom, fill_w, how)), (EDGE, WANT[1] - EDGE))
    out.paste(Image.fromarray(np.transpose(lengthen(left, fill_h, how), (1, 0, 2))), (0, EDGE))
    out.paste(Image.fromarray(np.transpose(lengthen(right, fill_h, how), (1, 0, 2))),
              (WANT[0] - EDGE, EDGE))

    out.paste(src.crop((0, 0, EDGE, EDGE)), (0, 0))
    out.paste(src.crop((w - EDGE, 0, w, EDGE)), (WANT[0] - EDGE, 0))
    out.paste(src.crop((0, h - EDGE, EDGE, h)), (0, WANT[1] - EDGE))
    out.paste(src.crop((w - EDGE, h - EDGE, w, h)), (WANT[0] - EDGE, WANT[1] - EDGE))
    return out


def set_attr(text, name, value):
    """Put a new value into one XML attribute, leaving the rest of the line alone."""
    at = text.index(name + '="')
    end = text.index('"', at + len(name) + 2)
    return text[:at] + name + '="' + str(value) + '"' + text[end + 1:]


def fit_anm2(w, h):
    """The animation is kept by hand; only the size of the one frame follows the sheet."""
    text = open(ANM2, encoding='utf-8').read()
    for name, value in (('XPivot', w // 2), ('YPivot', h // 2), ('Width', w), ('Height', h)):
        text = set_attr(text, name, value)
    out = open(ANM2, 'w', encoding='utf-8', newline='')
    out.write(text)
    out.close()


def main():
    global WANT
    if len(sys.argv) >= 3:
        WANT = (int(sys.argv[1]), int(sys.argv[2]))
    src = Image.open(SRC).convert('RGBA')
    src = src.crop(src.getchannel('A').getbbox())       # the drawing sits in a clear margin
    out = build(src)
    out.save(INTO)
    fit_anm2(out.width, out.height)
    print('the paper: %d x %d drawn, %d x %d written, %d columns kept at each end'
          % (src.width, src.height, out.width, out.height, EDGE))


if __name__ == '__main__':          # credits_page.py takes build() from here
    main()
