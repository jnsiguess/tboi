# -*- coding: utf-8 -*-
"""The touch D-pads (the owner's item 19, 23.09.2026): the game's own DPad of
Rebirth (ios_buttons.anm2 "DPad": a centre and four arms, (208,112) and
(160,112) of ios_buttons.png) repainted in the sticks' colours, with the
stick's own figure in the middle - the walker for moving, the shooter for
shooting - and diagonal arrows added, for eight directions.

The colours are the sticks' ones, measured on ios_buttons.png: the body 54,
its dark rim 38, the drawing 149 with half-ink edges 85. The DPad's own
colours map onto them one for one: its fill 127 -> 54, its bevel and arrows
95 -> 38 and 149, the black border stays.

The figures are taken off the sticks at their own size (they fit the 37-pixel
centre as they are, nothing is scaled): the move stick's walker without its
four arrows, the shoot stick's shooter without its tear lines.

    py -3 tools/dpad_art.py --preview     # the variants, to be chosen by eye
    py -3 tools/dpad_art.py --final       # the chosen look (the owner, 23.09.2026)
    py -3 tools/dpad_art.py --write       # into ios_buttons.png and every ios_buttons*.anm2

The game draws a D-pad through the stick's own ring sprite: `Analog DPad Move`
and `Analog DPad Shoot` are copies of the native DPad (five layers: the centre
with the diagonals and the figure, then the arms), nine frames each - still,
and the eight directions with their arms at the native pressed tint. The names
begin with "Analog" so the size variants scale them like the sticks. The knob
plays `Analog DPad Blank ...`, every layer hidden. sticks.c picks the frame.

AIM LOCK (the owner's 19th round, 03.10.2026: "the D-pad has no padlock"). The
shooting knob shows the lock with "Analog Stick Shoot Lock" (336,0): the
shooter moved two pixels left and a padlock at its lower right, the game's own
drawing. `Analog DPad Shoot Lock` is `Analog DPad Shoot` with that drawing in
the middle instead of the shooter's - the same box, so the tears stay where
they were; the padlock lies wholly in the centre piece, so its arms are the
shooting D-pad's own crops. sticks.c plays it while the shooting stick's flag
+0x1c9 says "locked" - the branch the knob's lock is chosen by.

RUN AFTER tools/twin_buttons.py: that one rebuilds the sheets and the markup
from its own copies and would take the D-pads out.
"""
import copy
import glob
import os
import shutil
import sys
import xml.etree.ElementTree as ET

import numpy as np
from PIL import Image

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
# M8 (plan 30.09): the game's files in work/app stay vanilla; the port's versions of
# them live in work/port-edit, and tools/port_patch.py makes port_patch/ out of them.
TOUCH = os.path.join(ROOT, 'work', 'port-edit', 'rebirth-resources', 'data', 'gfx', 'ui', 'touch')
SHEET = os.path.join(TOUCH, 'ios_buttons.png.before-twins')      # the game's own, untouched
ANM2 = os.path.join(TOUCH, 'ios_buttons.anm2')
OUT = os.path.join(ROOT, 'other stuff', 'dpad')

BODY, RIM, INK, HALF, EDGE = 54, 38, 149, 85, 0
ARM, CENTRE = (160, 112), (208, 112)
BLANK, MOVE, SHOOT, LOCK = (144, 0), (208, 0), (272, 0), (336, 0)
CANVAS = 150


def grey(v, a=255):
    return (v, v, v, a)


def load():
    return np.asarray(Image.open(SHEET).convert('RGBA')).astype(int)


def cell(s, at, size=48):
    x, y = at
    return s[y:y + size, x:x + size].copy()


def arm_piece(s):
    """the arm repainted: fill -> body, the arrow -> ink, the bevel -> rim"""
    a = cell(s, ARM)
    out = a.copy()
    v = a[..., 0]
    solid = a[..., 3] > 0
    fill = solid & (v == 127)
    dark = solid & (v == 95)
    # the bevel is the 95 line hugging the border; the arrow is the 95 inside
    border = solid & (v == 0)
    near = np.zeros_like(border)
    for dy in (-1, 0, 1):
        for dx in (-1, 0, 1):
            near |= np.roll(np.roll(border, dy, 0), dx, 1)
    bevel = dark & near
    arrow = dark & ~near
    out[fill] = grey(BODY)
    out[bevel] = grey(RIM)
    out[arrow] = grey(INK)
    return out


def centre_piece(s):
    a = cell(s, CENTRE)
    out = a.copy()
    v = a[..., 0]
    solid = a[..., 3] > 0
    out[solid & (v == 127)] = grey(BODY)
    out[solid & (v == 95)] = grey(RIM)
    return out


def figure(s, which):
    """the stick's drawing without its direction marks, cut to its ink"""
    blank = cell(s, BLANK, 64)
    g = cell(s, which, 64)
    ink = np.abs(g - blank).sum(2) > 0
    keep = np.zeros_like(ink)
    if which == MOVE:
        keep[17:46, 21:42] = True           # the walker; the four arrows lie outside
    else:
        keep[20:47, 22:42] = True           # the shooter; the tear lines run out of it
        keep[43:47, 31] = False              # the downward tear line between the legs
    ink &= keep
    ys, xs = np.nonzero(ink)
    fig = np.zeros(g.shape, int)
    fig[ink] = g[ink]
    return fig[ys.min():ys.max() + 1, xs.min():xs.max() + 1]


def shooter(s, style):
    """the shoot stick's drawing (the floor's ATTACK) in three cuts:

    full     all of it: the four tears on their dotted lines
    close    the tears pulled in to a pixel from the figure, no dots
    dotted   the tears pulled in, one dot of the line left before each
    """
    blank = cell(s, BLANK, 64)
    g = cell(s, SHOOT, 64)
    ink = np.abs(g - blank).sum(2) > 0
    ys, xs = np.nonzero(ink)
    y0, x0 = ys.min(), xs.min()
    G = g[y0:ys.max() + 1, x0:xs.max() + 1].copy()
    M = ink[y0:ys.max() + 1, x0:xs.max() + 1].copy()
    if style == 'full':
        G[~M] = 0
        return G
    # the parts, in the glyph's own rows and columns
    tears = {'top': (0, 6, 19, 29), 'bottom': (40, 46, 18, 28),
             'left': (18, 26, 0, 6), 'right': (18, 26, 41, 48)}
    fig = M.copy()
    fig[:11] = False
    fig[39:] = False
    fig[:, :12] = False
    fig[:, 33:] = False
    fig[35:39, 22] = False                     # the line down between the legs
    gap = 1 if style == 'close' else 4
    # where each tear goes: next to the figure's box (rows 11..38, cols 12..32)
    to = {'top': (11 - gap - 6, 0), 'bottom': (38 + gap + 1 - 40, 0),
          'left': (0, 12 - gap - 6), 'right': (0, 32 + gap + 1 - 41)}
    H, W = M.shape
    pad = 8
    out = np.zeros((H + 2 * pad, W + 2 * pad, 4), int)
    sel = fig
    out[pad:pad + H, pad:pad + W][sel] = G[sel]
    for k, (r0, r1, c0, c1) in tears.items():
        part = np.zeros_like(M)
        part[r0:r1, c0:c1] = M[r0:r1, c0:c1]
        dy, dx = to[k]
        yy, xx = np.nonzero(part)
        out[yy + dy + pad, xx + dx + pad] = G[yy, xx]
        if style == 'dotted':
            # one dot of the line, halfway between the tear and the figure
            cy, cx = (yy.min() + yy.max()) // 2 + dy + pad, (xx.min() + xx.max()) // 2 + dx + pad
            if k == 'top':
                out[yy.max() + dy + pad + 2, cx - 1] = grey(INK)
            elif k == 'bottom':
                out[yy.min() + dy + pad - 2, cx] = grey(INK)
            elif k == 'left':
                out[cy + 1, xx.max() + dx + pad + 2:xx.max() + dx + pad + 4] = grey(INK)
            else:
                out[cy + 1, xx.min() + dx + pad - 3:xx.min() + dx + pad - 1] = grey(INK)
    a = out[..., 3] > 0
    ys, xs = np.nonzero(a)
    return out[ys.min():ys.max() + 1, xs.min():xs.max() + 1]


def whole(s, which):
    """a stick's drawing entire, cut to its ink: the floor's MOVE or ATTACK"""
    blank = cell(s, BLANK, 64)
    g = cell(s, which, 64)
    ink = np.abs(g - blank).sum(2) > 0
    ys, xs = np.nonzero(ink)
    out = np.zeros(g.shape, int)
    out[ink] = g[ink]
    return out[ys.min():ys.max() + 1, xs.min():xs.max() + 1]


def centre_drawing(s, which):
    """what the owner chose (23.09.2026): the walker bare - the cross has the
    arrows already - and the shooter entire, its tears being the point"""
    return figure(s, MOVE) if which == MOVE else whole(s, which)


def final_preview():
    """both pads as chosen: bare diagonals, the sticks' drawings entire"""
    import editor_buttons_preview as E
    from eid_font_preview import Font
    s = load()
    pad = with_diagonals(rendered({'arm': arm_piece(s), 'centre': centre_piece(s)}), 'bare')
    o = CANVAS // 2
    font = Font(os.path.join(ROOT, 'port', 'app', 'src', 'main', 'assets', 'eid_default.fnt'))
    W, H = 2 * (CANVAS + 90), CANVAS + 16
    sheet = Image.new('RGBA', (W, H), (122, 112, 104, 255))
    for i, (which, label) in enumerate(((MOVE, 'MOVE'), (SHOOT, 'SHOOT'))):
        c = pad.copy()
        fig = centre_drawing(s, which)
        put(c, fig, o - (fig.shape[1] + 1) // 2 if which == SHOOT else o - 1 - fig.shape[1] // 2,
            o - fig.shape[0] // 2)
        x = i * (CANVAS + 90)
        sheet.alpha_composite(Image.fromarray(c.astype(np.uint8)), (x, 0))
        t = E.word(font, label)
        sheet.alpha_composite(t, (x + CANVAS // 2 - t.width // 2, CANVAS + 2))
        st = cell(s, which, 64)
        sheet.alpha_composite(Image.fromarray(st.astype(np.uint8)), (x + CANVAS + 13, CANVAS // 2 - 32))
        t = E.word(font, 'STICK')
        sheet.alpha_composite(t, (x + CANVAS + 45 - t.width // 2, CANVAS // 2 + 36))
        print(label, fig.shape)
    sheet.resize((W * 2, H * 2), Image.NEAREST).save(os.path.join(OUT, 'dpad_final_x2.png'))
    big = sheet.crop((20, 20, 130, 130)).resize((440, 440), Image.NEAREST)
    big2 = sheet.crop((CANVAS + 90 + 20, 20, CANVAS + 90 + 130, 130)).resize((440, 440), Image.NEAREST)
    z = Image.new('RGBA', (900, 440), (122, 112, 104, 255))
    z.paste(big, (0, 0))
    z.paste(big2, (460, 0))
    z.save(os.path.join(OUT, 'dpad_final_zoom.png'))


def shoot_preview():
    import editor_buttons_preview as E
    from eid_font_preview import Font
    s = load()
    pad = with_diagonals(rendered({'arm': arm_piece(s), 'centre': centre_piece(s)}), 'bare')
    o = CANVAS // 2
    items = []
    for label, st in (('NOW', None), ('1 FULL', 'full'), ('2 CLOSE', 'close'), ('3 DOTTED', 'dotted')):
        c = pad.copy()
        fig = figure(s, SHOOT) if st is None else shooter(s, st)
        put(c, fig, o - fig.shape[1] // 2, o - fig.shape[0] // 2)
        items.append((label, c, fig.shape))
    font = Font(os.path.join(ROOT, 'port', 'app', 'src', 'main', 'assets', 'eid_default.fnt'))
    W, H = len(items) * CANVAS + 90, CANVAS + 16
    sheet = Image.new('RGBA', (W, H), (122, 112, 104, 255))
    for i, (label, img, shape) in enumerate(items):
        sheet.alpha_composite(Image.fromarray(img.astype(np.uint8)), (i * CANVAS, 0))
        t = E.word(font, label)
        sheet.alpha_composite(t, (i * CANVAS + CANVAS // 2 - t.width // 2, CANVAS + 2))
        print(label, shape)
    st = cell(s, SHOOT, 64)
    sheet.alpha_composite(Image.fromarray(st.astype(np.uint8)), (len(items) * CANVAS + 13, CANVAS // 2 - 32))
    t = E.word(font, 'STICK')
    sheet.alpha_composite(t, (len(items) * CANVAS + 45 - t.width // 2, CANVAS // 2 + 36))
    sheet.resize((W * 2, H * 2), Image.NEAREST).save(os.path.join(OUT, 'dpad_shoot_x2.png'))


def rendered(pieces):
    """the DPad animation's frame 0, laid out by its own layers"""
    root = ET.parse(ANM2).getroot()
    an = [a for a in root.iter('Animation') if a.get('Name') == 'DPad'][0]
    c = Image.new('RGBA', (CANVAS, CANVAS), (0, 0, 0, 0))
    o = CANVAS // 2
    for la in an.iter('LayerAnimation'):
        f = list(la.iter('Frame'))[0]
        g = lambda n: float(f.get(n))
        piece = pieces['centre'] if int(g('XCrop')) == CENTRE[0] else pieces['arm']
        im = Image.fromarray(piece.astype(np.uint8))
        px, py, rot = g('XPivot'), g('YPivot'), g('Rotation')
        big = Image.new('RGBA', (200, 200), (0, 0, 0, 0))
        big.alpha_composite(im, (int(100 - px), int(100 - py)))
        big = big.rotate(-rot, center=(100, 100))
        c.alpha_composite(big, (int(o + g('XPosition') - 100), int(o + g('YPosition') - 100)))
    return np.asarray(c).astype(int)


def outlined(mask, fill, edge=EDGE):
    """a shape with a 1-px border of its own"""
    pad = np.pad(mask, 1)
    out = np.zeros(pad.shape + (4,), int)
    ring = np.zeros_like(pad)
    for dy in (-1, 0, 1):
        for dx in (-1, 0, 1):
            ring |= np.roll(np.roll(pad, dy, 0), dx, 1)
    out[ring] = grey(edge)
    out[pad] = fill
    return out


def corner_triangle(n):
    """a triangle pointing up-left: its right angle is the tip"""
    m = np.zeros((n, n), bool)
    for y in range(n):
        m[y, :n - y] = True
    return m


def put(canvas, img, x, y):
    h, w = img.shape[:2]
    sub = canvas[y:y + h, x:x + w]
    a = img[..., 3] > 0
    sub[a] = img[a]


def flips(img, k):
    """k: 0 up-left, 1 up-right, 2 down-right, 3 down-left"""
    if k in (1, 2):
        img = img[:, ::-1]
    if k in (2, 3):
        img = img[::-1]
    return img


# the four corners as signs
CORNERS = [(-1, -1), (1, -1), (1, 1), (-1, 1)]


def with_diagonals(pad, style):
    c = pad.copy()
    o = CANVAS // 2
    if style == 'bare':
        tri = outlined(corner_triangle(13), grey(INK))
        for k, (sx, sy) in enumerate(CORNERS):
            t = flips(tri, k)
            cx, cy = o + sx * 30, o + sy * 30
            put(c, t, cx - t.shape[1] // 2, cy - t.shape[0] // 2)
    elif style == 'wedge':
        # the corner between two arms closed by a plate cut on the diagonal:
        # the pad's outline becomes an octagon, the arrow sits on the plate
        n = 33
        m = corner_triangle(n)[::-1, ::-1]          # the right angle toward the centre
        plate = outlined(m, grey(BODY))
        tri = outlined(corner_triangle(11), grey(INK), RIM)
        for k, (sx, sy) in enumerate(CORNERS):
            p = flips(plate, (k + 2) % 4 if False else k)
            # the plate's right angle goes into the notch, 19 px out from the middle
            x0 = o + 19 if sx > 0 else o - 19 - p.shape[1]
            y0 = o + 19 if sy > 0 else o - 19 - p.shape[0]
            put(c, p, x0 + (-1 if sx > 0 else 1), y0 + (-1 if sy > 0 else 1))
            t = flips(tri, k)
            cx, cy = o + sx * 30, o + sy * 30
            put(c, t, cx - t.shape[1] // 2, cy - t.shape[0] // 2)
    elif style == 'disc':
        r = 9
        yy, xx = np.mgrid[-r:r + 1, -r:r + 1]
        disc = outlined(xx * xx + yy * yy <= r * r + 2, grey(BODY))
        tri = corner_triangle(8)
        for k, (sx, sy) in enumerate(CORNERS):
            cx, cy = o + sx * 33, o + sy * 33
            put(c, disc, cx - disc.shape[1] // 2, cy - disc.shape[0] // 2)
            t = flips(tri, k)
            img = np.zeros(t.shape + (4,), int)
            img[t] = grey(INK)
            put(c, img, cx - t.shape[1] // 2 + sx, cy - t.shape[0] // 2 + sy)
    return c


def preview():
    import editor_buttons_preview as E
    from eid_font_preview import Font
    os.makedirs(OUT, exist_ok=True)
    s = load()
    pieces = {'arm': arm_piece(s), 'centre': centre_piece(s)}
    pad = rendered(pieces)
    rows = []
    for fig_at, name in ((MOVE, 'MOVE'), (SHOOT, 'SHOOT')):
        base = pad.copy()
        fig = figure(s, fig_at)
        o = CANVAS // 2
        put(base, fig, o - 1 - fig.shape[1] // 2, o - fig.shape[0] // 2)
        rows.append((name, [('NO DIAGONALS', base)] + [(label, with_diagonals(base, st)) for label, st in
                            (('A BARE', 'bare'), ('B PLATES', 'wedge'), ('C DISCS', 'disc'))]))
    font = Font(os.path.join(ROOT, 'port', 'app', 'src', 'main', 'assets', 'eid_default.fnt'))
    stick = lambda at: cell(s, at, 64)
    W, H = 4 * CANVAS + 90, 2 * (CANVAS + 16)
    sheet = Image.new('RGBA', (W, H), (122, 112, 104, 255))
    for r, (name, items) in enumerate(rows):
        y = r * (CANVAS + 16)
        for i, (label, img) in enumerate(items):
            sheet.alpha_composite(Image.fromarray(img.astype(np.uint8)), (i * CANVAS, y))
            t = E.word(font, label)
            sheet.alpha_composite(t, (i * CANVAS + CANVAS // 2 - t.width // 2, y + CANVAS + 2))
        st = stick(MOVE if name == 'MOVE' else SHOOT)
        sheet.alpha_composite(Image.fromarray(st.astype(np.uint8)), (4 * CANVAS + 13, y + CANVAS // 2 - 32))
        t = E.word(font, 'STICK')
        sheet.alpha_composite(t, (4 * CANVAS + 45 - t.width // 2, y + CANVAS // 2 + 36))
    sheet = sheet.resize((W * 2, H * 2), Image.NEAREST)
    sheet.save(os.path.join(OUT, 'dpad_variants_x2.png'))
    print('  written to', OUT)


# ------------------------------------------------------------------ into the game

DPAD_ANIMS = {MOVE: 'Analog DPad Move', SHOOT: 'Analog DPad Shoot', LOCK: 'Analog DPad Shoot Lock'}
BLANK_ANIMS = ['Analog DPad Blank Move', 'Analog DPad Blank Shoot']
# where the pieces go on ios_buttons.png: a band free on the main sheet and the
# Japanese one alike (checked in write(), refused if anything is there)
FREE = (256, 128, 512, 256)
# the lock's centre does not fit there too: a second band, empty on both sheets
# and in every crop of the markup before the D-pads (checked in write())
FREE2 = (0, 128, 160, 192)
SHEETS = [os.path.join(TOUCH, 'ios_buttons.png'),
          os.path.join(ROOT, 'work', 'port-edit', 'rebirth-resources', 'data.jp', 'gfx', 'ui', 'touch',
                       'ios_buttons.png')]
# frames 1..8: up, up-right, right, down-right, down, down-left, left, up-left;
# the arms each frame presses (layers 1 up, 2 right, 3 down, 4 left)
PRESSED = [(), (1,), (1, 2), (2,), (2, 3), (3,), (3, 4), (4,), (4, 1)]
ARM_PRESSED, CENTRE_PRESSED = 155, 200      # the native DPad's own tints


def pad_picture(s, which):
    """the D-pad as the owner chose it, on the CANVAS, its middle at CANVAS // 2"""
    pad = with_diagonals(rendered({'arm': arm_piece(s), 'centre': centre_piece(s)}), 'bare')
    fig = centre_drawing(s, which)
    o = CANVAS // 2
    put(pad, fig, o - (fig.shape[1] + 1) // 2 if which in (SHOOT, LOCK) else o - 1 - fig.shape[1] // 2,
        o - fig.shape[0] // 2)
    return pad


def pieces_of(pad):
    """the picture cut into the centre and the four arms, each drawn by a layer.

    An arm is what lies within 21 of the axis and 20 or more out along it - the
    native arm's body starts there - so a pressed arm darkens with whatever of
    the middle drawing reaches into it (the shooter's tears), as it should.
    Everything else, the diagonals among it, is the centre."""
    o = CANVAS // 2
    yy, xx = np.mgrid[0:CANVAS, 0:CANVAS]
    dx, dy = xx - o, yy - o
    solid = pad[..., 3] > 0
    arms = [(np.abs(dx) <= 21) & (dy <= -20), (np.abs(dy) <= 21) & (dx >= 20),
            (np.abs(dx) <= 21) & (dy >= 20), (np.abs(dy) <= 21) & (dx <= -20)]
    centre = solid.copy()
    for m in arms:
        centre &= ~m
    out = []
    for m in [centre] + arms:
        m = m & solid
        ys, xs = np.nonzero(m)
        y0, x0 = ys.min(), xs.min()
        img = np.zeros((ys.max() - y0 + 1, xs.max() - x0 + 1, 4), int)
        img[ys - y0, xs - x0] = pad[ys, xs]
        out.append((img, o - x0, o - y0))          # the picture and its pivot: the pad's middle
    return out


def shelve(sizes, box):
    """rows of rectangles into the box, tallest first; answers their corners"""
    x0, y0, x1, y1 = box
    order = sorted(range(len(sizes)), key=lambda i: -sizes[i][1])
    at = [None] * len(sizes)
    x, y, row = x0, y0, 0
    for i in order:
        w, h = sizes[i]
        if x + w > x1:
            x, y, row = x0, y + row + 1, 0
        if y + h > y1:
            raise SystemExit('the D-pads do not fit the free band')
        at[i] = (x, y)
        x += w + 1
        row = max(row, h)
    return at


def shelve_boxes(sizes, boxes):
    """rows of rectangles into the boxes in turn, tallest first; answers their corners"""
    order = sorted(range(len(sizes)), key=lambda i: -sizes[i][1])
    at = [None] * len(sizes)
    state = [[b[0], b[1], 0] for b in boxes]            # x, y, row height
    for i in order:
        w, h = sizes[i]
        for k, (x0, y0, x1, y1) in enumerate(boxes):
            x, y, row = state[k]
            if x + w > x1:
                x, y, row = x0, y + row + 1, 0
            if y + h > y1 or x + w > x1:
                continue
            at[i] = (x, y)
            state[k] = [x + w + 1, y, max(row, h)]
            break
        else:
            raise SystemExit('the D-pads do not fit the free bands')
    return at


def crops_of(path):
    out = []
    for f in ET.parse(path).getroot().iter('Frame'):
        if f.get('XCrop') is not None:
            out.append(tuple(int(float(f.get(k))) for k in ('XCrop', 'YCrop', 'Width', 'Height')))
    return out


def frame_el(template, **kv):
    f = copy.deepcopy(template)
    for k, v in kv.items():
        f.set(k, str(v))
    return f


def build_animation(dpad, name, placed, scale, blank=False):
    """a copy of the native DPad: its five layers, nine frames"""
    an = copy.deepcopy(dpad)
    an.set('Name', name)
    frames = 1 if blank else len(PRESSED)
    an.set('FrameNum', str(frames))
    for f in an.find('RootAnimation').iter('Frame'):
        f.set('Delay', str(frames))
    for la in an.iter('LayerAnimation'):
        layer = int(la.get('LayerId'))
        template = list(la.iter('Frame'))[0]
        for f in list(la):
            la.remove(f)
        (x, y), (img, px, py) = placed[layer]
        for k in range(frames):
            tint = 255
            if not blank and PRESSED[k]:
                tint = ARM_PRESSED if layer in PRESSED[k] else (CENTRE_PRESSED if layer == 0 else 255)
            la.append(frame_el(template, XPosition=0, YPosition=0, XPivot=px, YPivot=py, XCrop=x, YCrop=y,
                               Width=img.shape[1], Height=img.shape[0], XScale=scale, YScale=scale,
                               Delay=1, Visible='false' if blank else 'true', RedTint=tint,
                               GreenTint=tint, BlueTint=tint, AlphaTint=255, Rotation=0,
                               Interpolated='false'))
    return an


def write():
    s = load()
    pads = {w: pieces_of(pad_picture(s, w)) for w in (MOVE, SHOOT, LOCK)}
    # the lock changes the middle only: its arms are the shooting D-pad's, to the pixel
    for i in range(1, 5):
        a, b = pads[SHOOT][i], pads[LOCK][i]
        if a[0].shape != b[0].shape or (a[0] != b[0]).any() or a[1:] != b[1:]:
            raise SystemExit('the padlock reaches into arm %d' % i)
    order = [(w, i) for w in (MOVE, SHOOT) for i in range(5)] + [(LOCK, 0)]
    sizes = [(pads[w][i][0].shape[1], pads[w][i][0].shape[0]) for w, i in order]
    at = dict(zip(order, shelve_boxes(sizes, [FREE, FREE2])))
    for i in range(1, 5):
        at[(LOCK, i)] = at[(SHOOT, i)]
    taken = crops_of(ANM2 + '.before-dpad')
    for x0, y0, x1, y1 in (FREE, FREE2):
        for cx, cy, cw, ch in taken:
            if cx < x1 and cx + cw > x0 and cy < y1 and cy + ch > y0:
                raise SystemExit('the band %s is in the crop %s of the markup' % ((x0, y0, x1, y1), (cx, cy, cw, ch)))
    for path in SHEETS:
        keep = path + '.before-dpad'
        if not os.path.exists(keep):
            shutil.copyfile(path, keep)
        sheet = np.asarray(Image.open(keep).convert('RGBA')).astype(int)
        for x0, y0, x1, y1 in (FREE, FREE2):
            if sheet[y0:y1, x0:x1, 3].any():
                raise SystemExit('%s: the band %s is not free' % (path, (x0, y0, x1, y1)))
        for (w, i), (x, y) in at.items():
            if w == LOCK and i:
                continue                                # the shooting D-pad's own arms
            img = pads[w][i][0]
            sheet[y:y + img.shape[0], x:x + img.shape[1]] = img
        Image.fromarray(sheet.astype(np.uint8)).save(path)
        print('  %s: the pieces laid in' % path)

    anm2s = [p for p in glob.glob(os.path.join(TOUCH, 'ios_buttons*.anm2'))
             if not os.path.basename(p).split('.anm2')[1]]
    for path in anm2s:
        keep = path + '.before-dpad'
        if not os.path.exists(keep):
            shutil.copyfile(path, keep)
        base = os.path.basename(path)
        scale = int(base[len('ios_buttons_'):-len('.anm2')]) if base != 'ios_buttons.anm2' else 100
        tree = ET.parse(keep)
        anims = tree.getroot().find('Animations')
        dpad = [a for a in anims if a.get('Name') == 'DPad'][0]
        for w in (MOVE, SHOOT, LOCK):
            placed = {i: (at[(w, i)], pads[w][i]) for i in range(5)}
            anims.append(build_animation(dpad, DPAD_ANIMS[w], placed, scale))
        for name in BLANK_ANIMS:
            placed = {i: (at[(MOVE, i)], pads[MOVE][i]) for i in range(5)}
            anims.append(build_animation(dpad, name, placed, scale, blank=True))
        tree.write(path, encoding='utf-8', xml_declaration=True)
        print('  %s: the D-pad animations added at %d%%' % (base, scale))


if __name__ == '__main__':
    if '--write' in sys.argv:
        write()
        raise SystemExit
    if '--preview' in sys.argv:
        preview()
    elif '--final' in sys.argv:
        final_preview()
    elif '--shoot' in sys.argv:
        shoot_preview()
    else:
        print(__doc__)
