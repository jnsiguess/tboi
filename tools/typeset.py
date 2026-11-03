# -*- coding: utf-8 -*-
"""Typeset a label in the game's font so that it comes out like a drawn one.

The game's font is the same one the menu labels are made with: in
`data/font/teammeatfont*.fnt` it is signed "Team Meat Font X HappyZCool". It
does have Cyrillic; Japanese, Korean and Chinese have their own fonts in the
language sets.

There were two differences from the native labels, both measured:

  thickness  — the drawn ones are fatter than the typeset ones; we grow the
             stroke by a pixel along the cross and the diagonals before
             shrinking. We do not do this for ideographs: their strokes are
             dense as it is, and thickening clogs the gaps — the label turns
             into a black blot;
  unevenness — in the native ones the letters wander vertically by 1..2 pixels,
             and the height of a letter runs from 12 to 17 with an average of
             14. Typesetting places them evenly, so we give every letter its
             own shift, a slight rotation and a small difference in size.

The jitter is taken from the name of the word rather than at random: one and
the same word is always typeset the same way, and a rebuild changes nothing.
"""
import hashlib
import os
import random
import struct

from PIL import Image, ImageChops, ImageDraw, ImageFont

INK = (54, 47, 45)
SHIFTS = [(1, 0), (-1, 0), (0, 1), (0, -1), (1, 1), (-1, -1)]
SHIFT_UP = (-2, -1, -1, 0, 0, 0, 1, 1, 2)
SPACE = 1.8               # how many times wider the space is than the font's
OUTLINED = {'lanapixel.fnt'}  # fonts whose glyph is drawn with a shadow
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
RESOURCES = os.path.join(ROOT, 'work', 'app', 'repentance-resources')

_cache = {}
_cache_ttf = {}


def _font(folder, name):
    key = (folder, name)
    if key in _cache:
        return _cache[key]
    d = open(os.path.join(folder, name), 'rb').read()
    assert d[:3] == b'BMF', 'not BMFont: %s' % name
    i, pages, chars, common = 4, [], {}, {}
    while i + 5 <= len(d):
        kind = d[i]
        (size,) = struct.unpack_from('<I', d, i + 1)
        i += 5
        body = d[i:i + size]
        i += size
        if kind == 2:
            common['height'], common['base'] = struct.unpack_from('<HH', body, 0)
        elif kind == 3:
            pages = [p.decode('latin1') for p in body.split(b'\0') if p]
        elif kind == 4:
            for k in range(0, len(body), 20):
                (id, x, y, w, h, xo, yo, xa, page, _) = struct.unpack_from('<IHHHHhhhBB', body, k)
                chars[id] = (x, y, w, h, xo, yo, xa, page)
    sheets = []
    for p in pages:
        path = os.path.join(folder, p)
        if not os.path.exists(path):
            path = os.path.join(folder, p.lower())
        sheet = Image.open(path).convert('RGBA')
        if name.lower() in OUTLINED:
            # The glyph is drawn as an outline: the letter itself is a light
            # fill, around it a black contour, at the bottom right a grey
            # shadow. For the ink we take the fill. The silhouette (fill with
            # the contour) will not do: the contour goes around the glyph on
            # the outside and closes up at thirteen pixels — the glyph comes
            # out a solid blot.
            opaque = sheet.getchannel('A').point(lambda v: 255 if v >= 128 else 0)
            light = sheet.convert('L').point(lambda v: 255 if v >= 128 else 0)
            sheet.putalpha(ImageChops.multiply(opaque, light))
        sheets.append(sheet)
    _cache[key] = (common, chars, sheets)
    return _cache[key]


def _paint(img):
    # The per-pixel walk was the bottleneck here: on the enlarged copy it alone
    # ate seconds. We set the colour for the whole sheet at once and keep the
    # transparency.
    out = Image.new('RGBA', img.size, INK + (0,))
    out.putalpha(img.getchannel('A'))
    return out


def _thicken(img, shifts=None):
    a = img.getchannel('A')
    out = a.copy()
    for dx, dy in (SHIFTS if shifts is None else shifts):
        out = ImageChops.lighter(out, ImageChops.offset(a, dx, dy))
    new = Image.new('RGBA', img.size, (0, 0, 0, 0))
    new.putalpha(out)
    return _paint(new)


DETAIL = 6                # how many times we enlarge for the thickening


def _dilate(a, r):
    """Growing with a square of side 2r+1, axis by axis: 4r cheap additions
    instead of (2r+1)^2 comparisons."""
    hor = a
    for d in range(1, r + 1):
        hor = ImageChops.lighter(hor, ImageChops.offset(a, d, 0))
        hor = ImageChops.lighter(hor, ImageChops.offset(a, -d, 0))
    out = hor
    for d in range(1, r + 1):
        out = ImageChops.lighter(out, ImageChops.offset(hor, 0, d))
        out = ImageChops.lighter(out, ImageChops.offset(hor, 0, -d))
    return out


def swollen(img, r):
    """The picture with its stroke grown by r points, and nothing else touched."""
    if not r:
        return img
    out = Image.new('RGBA', img.size, (0, 0, 0, 0))
    out.putalpha(_dilate(img.getchannel('A'), r))
    return _paint(out)


def enlarged(img, up, down, grow=0):
    """A crisp typesetting made `up/down` times bigger, exactly.

    NEAREST invents nothing on the way up, and BOX on the way down is a plain
    average of the points it covers - so every point of the answer carries the
    TRUE SHARE OF INK over its own square, and the threshold that follows means
    what it says. LANCZOS in its place rings, and a threshold over the ringing
    is what wore the thin strokes of the ideographs down to a single point.

    The growth is applied while enlarged, so its step is a fraction of an
    original point rather than a whole one.
    """
    w, h = img.size
    big = swollen(img.resize((w * up, h * up), Image.NEAREST), grow)
    return big.resize((max(1, round(big.size[0] / float(down))),
                       max(1, round(big.size[1] / float(down)))), Image.BOX)


def _thicken_smooth(img, strength):
    """Fractional thickening: we grow on an enlarged copy and hand that back.

    Whole shifts give a step of 1..2 pixels of stroke. For Latin that is still
    good enough, for Hangul and ideographs it is not: between "thin" and "the
    gaps are clogged" they have exactly one step. On a copy enlarged six times
    the step comes out at a sixth of a pixel.

    The strength is given in pixels of the original typesetting. We return the
    enlarged copy: shrinking here and then once more to the needed height would
    mean blurring the stroke twice, and the anti-aliasing threshold lands wide
    of the mark after that.
    """
    N = DETAIL
    a = img.getchannel('A')
    w, h = a.size
    big = a.resize((w * N, h * N), Image.NEAREST)
    r = int(round(strength * N)) if strength > 0 else 0
    if r > 0:
        big = _dilate(big, r)
    new = Image.new('RGBA', (w * N, h * N), (0, 0, 0, 0))
    new.putalpha(big)
    return _paint(new)


def _lay(canvas, glyph, at):
    """Lay a glyph over the canvas properly - NOT paste(glyph, at, glyph).

    Handing the same picture as both the image and the mask makes paste compute
    an alpha of a*a/255: a halftone of 128 comes out at 64, one of 81 at 26. The
    font's own smoothing was being crushed to a quarter before anything else
    touched it, and the threshold that follows then ate what was left, so every
    letter here stood thinner than the face draws it (13.09.2026).
    """
    x, y = at
    if x < 0:
        glyph = glyph.crop((-x, 0, glyph.width, glyph.height)); x = 0
    if y < 0:
        glyph = glyph.crop((0, -y, glyph.width, glyph.height)); y = 0
    w = min(glyph.width, canvas.width - x)
    h = min(glyph.height, canvas.height - y)
    if w <= 0 or h <= 0:
        return
    canvas.alpha_composite(glyph.crop((0, 0, w, h)), (x, y))


def _assemble(text, common, chars, sheets, rnd, jitter, thicken=True, shifts=None,
              strength=None, lean=0.0, curve=0.0, crisp=False):
    # The canvas is measured the way the loop below walks, not by the sum of the
    # advances: a space moves the pen WIDER than its advance (SPACE), and the
    # last glyph is drawn by its own reach (xo + w), which can stick out past
    # the advance. Counted by advances alone the canvas came out narrower than
    # the line, and the edges were quietly cropped - on a single word there is
    # no space and no visible loss, which is why the menu labels never showed it.
    width = 16
    for c in text:
        z = chars.get(ord(c))
        if not z:
            width += 6
        elif c == ' ':
            width += int(z[6] * SPACE)
        else:
            width += max(z[6], z[4] + z[2])
    width += 16                      # slack for the jitter's shift and rotation
    #
    # A line laid along a slope, or bent along an arc, moves WHOLE LETTERS.
    # Bending the finished strip column by column - which is what a rotation of
    # the raster comes to - leaves one half of a letter a pixel above the other,
    # and at a small size that chews the shapes to pieces. Here the offset is
    # worked out per glyph and added to its baseline, so every letter stays the
    # shape the font drew it.
    span = max(1, width - 32)
    room = int(abs(lean) * span) + int(abs(curve)) + 4
    canvas = Image.new('RGBA', (max(1, width), common['height'] + 24 + room * 2),
                       (0, 0, 0, 0))

    x = 8
    for c in text:
        z = chars.get(ord(c))
        if not z:
            x += 6
            continue
        if c == ' ':
            # in the native labels the words are spaced noticeably wider apart
            # than the font sets them
            x += int(z[6] * SPACE)
            continue
        sx, sy, w, h, xo, yo, xa, page = z
        if w and h:
            glyph = sheets[page].crop((sx, sy, sx + w, sy + h))
            dy = dx = 0
            if jitter and crisp:
                # A bitmap font at its own size must not be resampled: growing
                # and turning a ten-pixel glyph eats its thin strokes, and that
                # is what made `E` read as `C`. The hand-drawn wobble is kept,
                # but only as WHOLE-PIXEL moves - nothing inside a letter shifts
                # against anything else, so no letter can be damaged.
                dy = round(rnd.choice(SHIFT_UP) * jitter)
                dx = round(rnd.choice((-1, 0, 0, 0, 1)) * jitter)
            elif jitter:
                # The strength of the jitter. It cannot be counted from the
                # shift alone: at the edges of a letter the rotation adds
                # another pixel, and together that comes out twice what was
                # measured on the native ones. So we keep the rotation small.
                dy = round(rnd.choice(SHIFT_UP) * jitter)
                dx = round(rnd.choice((-1, 0, 0, 0, 1)) * jitter)
                angle = rnd.uniform(-1.2, 1.2) * jitter
                grow = 1.0 + (rnd.uniform(-0.05, 0.06) * jitter)
                new = (max(1, round(w * grow)), max(1, round(h * grow)))
                glyph = glyph.resize(new, Image.LANCZOS)
                glyph = glyph.rotate(angle, resample=Image.BICUBIC, expand=True)
                dy -= (glyph.size[1] - h) // 2
                dx -= (glyph.size[0] - w) // 2
            here = x - 8
            k = (2.0 * here / span) - 1.0
            slide = int(round(-lean * (here - span / 2.0) + curve * (1.0 - k * k)))
            _lay(canvas, glyph, (x + xo + dx, 12 + room + yo + dy + slide))
        x += xa
    if strength is not None:
        canvas = _thicken_smooth(_paint(canvas), strength)
    else:
        canvas = _thicken(_paint(canvas), shifts) if thicken else _paint(canvas)
    bbox = canvas.getchannel('A').getbbox()
    return canvas.crop(bbox) if bbox else None


TTF_SIZE = 54             # how many pixels we draw a TTF glyph at before shrinking


def _compose_ttf(text, height, font, folder, jitter, strength, lean=0.0, curve=0.0):
    """Typesetting from an ordinary font: we draw large and shrink.

    Every glyph goes into its own cell with a common origin, so the baseline is
    the same for all of them and the jitter does not wreck it. Thickening the
    enlarged copy, the way it is done for bitmap fonts, is not needed here: a
    pixel of the typesetting is three times finer than the final one anyway.
    """
    S = TTF_SIZE
    key = (folder, font)
    if key not in _cache_ttf:
        _cache_ttf[key] = ImageFont.truetype(os.path.join(folder, font), S)
    face = _cache_ttf[key]
    seed = hashlib.md5(text.encode('utf-8')).hexdigest()
    r = int(round(strength or 0))

    span = max(1.0, sum(face.getlength(c) for c in text))
    room = int(abs(lean) * span) + int(abs(curve) * S / 16.0) + S

    def canvas(jit):
        rnd = random.Random(seed)
        sheet = Image.new('L', (int(span) + S * 3, S * 3 + room * 2), 0)
        x = S
        for c in text:
            step = face.getlength(c)
            if c == ' ':
                x += step * SPACE
                continue
            cell = Image.new('L', (int(step) + 2 * S, S * 3), 0)
            ImageDraw.Draw(cell).text((S, S), c, font=face, fill=255)
            dx = dy = 0
            if jit:
                dy = round(rnd.choice(SHIFT_UP) * jit * S / 16.0)
                dx = round(rnd.choice((-1, 0, 0, 0, 1)) * jit * S / 16.0)
                cell = cell.rotate(rnd.uniform(-1.2, 1.2) * jit, resample=Image.BICUBIC)
            # the slope moves a WHOLE glyph, never a part of one
            here = x - S
            u = (2.0 * here / span) - 1.0
            slide = int(round(-lean * (here - span / 2.0)
                              + curve * (S / 16.0) * (1.0 - u * u)))
            sheet.paste(cell, (int(x) - S + dx, room + dy + slide), cell)
            x += step
        return _dilate(sheet, r) if r > 0 else sheet

    # the scale comes from a FLAT line: a sloped block is as tall as the slope
    flat_lean, flat_curve = lean, curve
    lean = curve = 0.0
    flat = canvas(0)
    lean, curve = flat_lean, flat_curve
    bbox = flat.getbbox() if flat else None
    if not bbox:
        return None
    k = height / float(bbox[3] - bbox[1])
    even = canvas(0)
    jittered = canvas(float(jitter)) if jitter else even
    bbox = jittered.getbbox()
    jittered = jittered.crop(bbox)
    col = Image.new('RGBA', jittered.size, (0, 0, 0, 0))
    col.putalpha(jittered)
    col = _paint(col)
    return col.resize((max(1, round(col.size[0] * k)), max(1, round(col.size[1] * k))),
                      Image.LANCZOS)


def preserve_gaps(thick, thin, limit=2):
    """Bring back the hairline gaps that the thickening closed.

    The sticking together happens not on the growing but on the shrinking: a
    gap between the bars that is three to five pixels wide in the typesetting
    is left as one after the squeeze, and the anti-aliasing threshold eats it.
    We look at the ungrown typesetting: if a gap there between two strokes is
    no wider than `limit`, we push it back through. Someone drawing by hand
    does the same — he will not let the bars of `ㅌ` merge, however fat he
    writes.
    """
    w, h = thick.size
    t = thin.resize((w, h), Image.LANCZOS) if thin.size != (w, h) else thin
    before = t.getchannel('A').load()
    after = thick.getchannel('A').copy()
    px = after.load()
    has = [[before[x, y] > 140 for y in range(h)] for x in range(w)]
    # We collect axis by axis: axis 0 goes down the columns and finds the slits
    # that separate strokes above and below, axis 1 goes along the rows, left
    # and right.
    found = ({}, {})
    for axis in (0, 1):
        length, across = (h, w) if axis else (w, h)
        for_axis = found[axis]
        for i in range(length):
            read = (lambda k: has[k][i]) if axis else (lambda k: has[i][k])
            j = 0
            while j < across:
                if read(j):
                    j += 1
                    continue
                start = j
                while j < across and not read(j):
                    j += 1
                # the gap is pinched by strokes on both sides and is narrow
                if start > 0 and j < across and j - start <= limit:
                    for k in range(start, j):
                        for_axis[(k, i) if axis else (i, k)] = True
    # A real slit runs along, whereas a lone light pixel inside a thick stroke
    # is pinched from both sides too — horizontally as well as vertically.
    # Without this check such pixels were pushed back through and left white
    # specks on the ideographs. We keep only those that have a neighbour along
    # the slit.
    #
    # We count a neighbour only among those that DO actually get pushed out: a
    # neighbour that is background anyway changes nothing, and a speck passed
    # the check on account of it.
    fixes = [{p for p in found[axis] if px[p] > 140} for axis in (0, 1)]
    for x, y in fixes[0]:                         # horizontal slit, runs along x
        if (x - 1, y) in fixes[0] or (x + 1, y) in fixes[0]:
            px[x, y] = 0
    for x, y in fixes[1]:                         # vertical slit, runs along y
        if (x, y - 1) in fixes[1] or (x, y + 1) in fixes[1]:
            px[x, y] = 0
    new = thick.copy()
    new.putalpha(after)
    return new


def compose(text, height=None, lang='data', jitter=True, font='teammeatfont16.fnt',
            thicken=True, shifts=None, strength=None, folder=None,
            lean=0.0, curve=0.0):
    """Typeset the text and bring it to the needed letter height.

    We take the scale from the EVEN typesetting. Counted from the jittered one,
    the letters that have wandered apart inflate the frame, the fitting by
    height squeezes the whole word, and it comes out noticeably smaller than
    the even one — on AIM LOCK it was 61 against 68.

    A LINE FITTED BY HEIGHT IS A LINE RESAMPLED. The gaps between the letters
    are drawn one or two pixels wide, and a fit of 0.72 leaves less than a pixel
    of them - after which no threshold and no mending can tell two letters
    apart. Where the size can be had from the font itself, ask for it (height
    None) and nothing is resampled at all: that is what the title does
    (14.09.2026).
    """
    if folder is None:
        folder = os.path.join(RESOURCES, lang, 'font')
        if not os.path.exists(os.path.join(folder, font)):
            folder = os.path.join(RESOURCES, 'data', 'font')
    if font.lower().endswith('.ttf'):
        return _compose_ttf(text, height, font, folder, jitter, strength, lean, curve)
    common, chars, sheets = _font(folder, font)
    seed = hashlib.md5(text.encode('utf-8')).hexdigest()
    if height is None:
        # the font's own size, taken as it is: no scale, no resampling, no loss
        return _assemble(text, common, chars, sheets, random.Random(seed),
                         float(jitter) if jitter else False, thicken, shifts,
                         strength, lean, curve, crisp=True)

    # The scale is taken from a line laid FLAT. With a slope the block is as
    # tall as the slope carries it, and fitting that to the letter height would
    # shrink the letters instead of the block.
    flat = _assemble(text, common, chars, sheets, random.Random(seed), False,
                     thicken, shifts, strength)
    if flat is None:
        return None
    k = height / flat.size[1]
    laid = _assemble(text, common, chars, sheets, random.Random(seed),
                     float(jitter) if jitter else False,
                     thicken, shifts, strength, lean, curve)
    if laid is None:
        return None
    return laid.resize((max(1, round(laid.size[0] * k)),
                        max(1, round(laid.size[1] * k))), Image.LANCZOS)
