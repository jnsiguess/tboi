# -*- coding: utf-8 -*-
"""Every edge of the game's popup paper, on every canvas the port makes.

THE DRAWER (Repentance 0x7fac64; the same code in the other engine copies at
0x37cb0, 0x17de98, 0x36418c, sized by the copies in COPIES) builds a dialog's paper out of
gfx/ui/Popup.png (223 x 44), twice: first as a 40 % black shadow moved by
(5, 5), then as the paper. Its centre P and size S come from 0x7fb3fc:

    P = canvas * 0.5 + slide        (fmadd, the slide's x is always 0)
    S = floor(squash * size)        (the squash is 1 at rest)

and the pieces, in drawing order (L, T, R, B the paper's sides, H = S / 2):

    fill    src (120,10)  8 x 8    at (L, T), 2H, stretched
    left    src (1,28)   98 x 7    at (L-Hh, T-7) (Hh, 7), turned -90 deg about (L, T)
    top     src (8,1)   211 x 8    at (L, T-8)    (Ww, 8)
    right   src (1,36)   98 x 7    at (R-Hh, T)   (Hh, 7), turned -90 deg about (R, T)
    bottom  src (4,10)  211 x 17   at (L, B)      (Ww, 17)
    corners src (1,4) 7x5 at (L-7, T-5); (219,1) 3x8 at (R, T-8);
            (1,10) 3x13 at (L-3, B); (215,10) 5x17 at (R, B)

with Hh = S.y / 98 * 98 and Ww = S.x / 211 * 211 in float32, and the turn by
0x8c8a1c -> 0x8c8a5c: sincosf of float(-90 * pi/180), then per point
x' = fma(x, cos, sin * -y), y' = fma(x, sin, cos * y) about the pivot.

The interface is drawn one pixel to the unit and blown up NEAREST, so a side
at x.5 runs exactly through a column of pixel centres. Which piece takes that
column is the rasteriser's tie rule; when it is a side strip, the strip
samples exactly at its last texel's far boundary - for the left strip that is
row 35, the transparent separator between the two side strips, and for the
top strip row 9, the separator under it - and a one-unit hole runs the whole
length of the side (the owner's screenshot, 06.10.2026: column 82 of a 585
canvas, paper 420 wide).

WHAT IS MODELLED. Every piece is rasterised at one pixel to the unit with
the image's own float32 steps, vertices snapped to the GPU's sub-pixel grid
(1/256, and unsnapped as a second opinion), nearest sampling, and the tie
rule measured on the owner's screen for vertical sides (a centre on a left
side is out, on a right side in) and BOTH possible rules for horizontal
sides, which nothing has measured yet. A HOLE is a pixel that is paper when
the whole dialog is moved by 1/64 of a unit either way and is not paper where
it really stands - exactly the defect, whatever the rule.

    py -3 tools/popupedges.py           the table, before and after the fix
    py -3 tools/popupedges.py --slide   the opening animation frame by frame, old and new
    py -3 tools/popupedges.py --words   the fix's instruction words: encodings and the image's copies
    py -3 tools/popupedges.py --emulate the size function itself on unicorn, the game's words and
                                        the fixed ones in all four copies, against place() bit for bit

The self-test at the start reproduces the screenshot (585 canvas, 420 x 236
card: the hole is column 82 on the left, the right side clean) and refuses
to print a table if the model does not.
"""
import math, os, struct, sys
from fractions import Fraction
import numpy as np
from PIL import Image

F = np.float32
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
POPUP = os.path.join(ROOT, 'work', 'app', 'repentance-resources', 'data', 'gfx', 'ui', 'popup.png')
BIN = os.path.join(ROOT, 'work', 'isaac.arm64')

ALPHA = np.asarray(Image.open(POPUP).convert("RGBA"))[:, :, 3]
TEX_H, TEX_W = ALPHA.shape


# ------------------------------------------------------------ float32, exactly
def f32(x):
    return F(x)


def round_f32(q):
    """The float32 nearest to an exact rational (ties to even)."""
    y = F(float(q))
    best = None
    for c in (np.nextafter(y, F(-np.inf)), y, np.nextafter(y, F(np.inf))):
        d = abs(Fraction(float(c)) - q)
        if best is None or d < best[0] or (d == best[0] and (struct.unpack("<I", struct.pack("<f", c))[0] & 1) == 0):
            best = (d, c)
    return best[1]


def fma32(a, b, c):
    return round_f32(Fraction(float(a)) * Fraction(float(b)) + Fraction(float(c)))


RAD = F(-90.0 * 0.017453292519943295)          # 0x8ae744: double multiply, then float
SIN, COS = F(math.sin(float(RAD))), F(math.cos(float(RAD)))


def turn(points, pivot):
    """0x8c8a5c: every point about the pivot, the image's own steps."""
    out = []
    for x, y in points:
        rx, ry = f32(x - pivot[0]), f32(y - pivot[1])
        nx = fma32(rx, COS, f32(SIN * f32(-ry)))
        ny = fma32(rx, SIN, f32(COS * ry))
        out.append((f32(nx + pivot[0]), f32(ny + pivot[1])))
    return out


def rect(tl, size):
    """0x7fb528 -> 0x8c885c: the quad TL, TR, BL, BR of a rectangle."""
    x0, y0 = tl
    x1, y1 = f32(x0 + size[0]), f32(y0 + size[1])
    return [(x0, y0), (x1, y0), (x0, y1), (x1, y1)]


# ------------------------------------------------------------ the dialog's geometry
def place(canvas, size, slide=(0.0, 0.0), squash=(1.0, 1.0), whole=False):
    """0x7fb3fc: the paper's centre and size. whole = the fix (loader.c popup_on_whole_units)."""
    cw, ch = f32(canvas[0]), f32(canvas[1])
    s = (F(math.floor(f32(f32(squash[0]) * f32(size[0])))), F(math.floor(f32(f32(squash[1]) * f32(size[1])))))
    p = (fma32(cw, F(0.5), f32(slide[0])), fma32(ch, F(0.5), f32(slide[1])))
    if whole:
        hx, hy = f32(s[0] * F(0.5)), f32(s[1] * F(0.5))
        p = (f32(F(math.floor(f32(p[0] - hx))) + hx), f32(F(math.floor(f32(p[1] - hy))) + hy))
    return p, s


def pieces(p, s, offset=(0.0, 0.0)):
    """0x7fac64: the nine pieces, each (name, quad TL TR BL BR, source rect)."""
    p = (f32(p[0] + F(offset[0])), f32(p[1] + F(offset[1])))
    h = (f32(s[0] * F(0.5)), f32(s[1] * F(0.5)))
    k = (f32(s[0] / F(211)), f32(s[1] / F(98)))
    hh, ww = f32(k[1] * F(98)), f32(k[0] * F(211))
    tl = (f32(p[0] - h[0]), f32(p[1] - h[1]))
    out = [("fill", rect(tl, (f32(h[0] * F(2)), f32(h[1] * F(2)))), (120, 10, 8, 8))]
    q = rect((f32(tl[0] - hh), f32(tl[1] - F(7))), (hh, F(7)))
    out.append(("left", turn(q, q[3]), (1, 28, 98, 7)))
    out.append(("top", rect((f32(p[0] - h[0]), f32(p[1] - f32(h[1] + F(8)))), (ww, F(8))), (8, 1, 211, 8)))
    q = rect((f32(p[0] + f32(h[0] - hh)), f32(p[1] + f32(-h[1]))), (hh, F(7)))
    out.append(("right", turn(q, q[1]), (1, 36, 98, 7)))
    out.append(("bottom", rect((f32(p[0] + f32(-h[0])), f32(p[1] + h[1])), (ww, F(17))), (4, 10, 211, 17)))
    out.append(("corner TL", rect((f32(tl[0] - F(7)), f32(tl[1] - F(5))), (F(7), F(5))), (1, 4, 7, 5)))
    out.append(("corner TR", rect((f32(p[0] + h[0]), f32(p[1] + f32(-f32(h[1] + F(8))))), (F(3), F(8))), (219, 1, 3, 8)))
    out.append(("corner BL", rect((f32(p[0] + f32(-f32(h[0] + F(3)))), f32(p[1] + h[1])), (F(3), F(13))), (1, 10, 3, 13)))
    out.append(("corner BR", rect((f32(p[0] + h[0]), f32(p[1] + h[1])), (F(5), F(17))), (215, 10, 5, 17)))
    return out


def sides(p, s):
    """The paper's own four sides (the fill's), as floats."""
    l, t = float(f32(p[0] - f32(s[0] * F(0.5)))), float(f32(p[1] - f32(s[1] * F(0.5))))
    return l, t, l + float(s[0]), t + float(s[1])


# ------------------------------------------------------------ the rasteriser
def coverage(quad, px, py, snap, top_in):
    """Pixel centres inside the quad: strictly, or on a side the tie rule gives it."""
    q = [(float(x), float(y)) for x, y in quad]
    if snap:
        q = [(round(x * snap) / snap, round(y * snap) / snap) for x, y in q]
    ring = [q[0], q[1], q[3], q[2]]                          # TL TR BR BL: clockwise on a y-down canvas
    inside = np.ones(px.shape, bool)
    for i in range(4):
        (ax, ay), (bx, by) = ring[i], ring[(i + 1) % 4]
        e = (bx - ax) * (py - ay) - (by - ay) * (px - ax)    # > 0 inside
        nx, ny = by - ay, -(bx - ax)                          # outward normal
        if abs(nx) >= abs(ny):
            keep = nx > 0                                     # measured: a right side keeps its centres
        else:
            keep = (ny < 0) == top_in                         # unmeasured: both rules are tried
        inside &= (e > 0) | ((e == 0) & keep)
    return inside, q


def paper_at(parts, px, py, snap, top_in):
    """Where the paper pass leaves a texel with any alpha."""
    paper = np.zeros(px.shape, bool)
    for name, quad, (sx, sy, sw, sh) in parts:
        cov, q = coverage(quad, px, py, snap, top_in)
        if not cov.any():
            continue
        (x0, y0), (x1, y1), (x2, y2) = q[0], q[1], q[2]
        ux, uy, vx, vy = x1 - x0, y1 - y0, x2 - x0, y2 - y0
        det = ux * vy - uy * vx
        dx, dy = px[cov] - x0, py[cov] - y0
        a = (dx * vy - dy * vx) / det
        b = (ux * dy - uy * dx) / det
        tu = np.clip(np.floor(sx + a * sw).astype(int), 0, TEX_W - 1)
        tv = np.clip(np.floor(sy + b * sh).astype(int), 0, TEX_H - 1)
        hit = np.zeros(px.shape, bool)
        hit[cov] = ALPHA[tv, tu] > 0
        paper |= hit
    return paper


SNAPS = (256, 0)                 # the GPU's sub-pixel grid, and none
SHIFT = 1.0 / 64


def holes(p, s):
    """Pixels the paper should cover and does not, per rule variant: {variant: [(x, y)]}."""
    l, t, r, b = sides(p, s)
    x0, x1 = int(math.floor(l)) - 12, int(math.ceil(r)) + 12
    y0, y1 = int(math.floor(t)) - 12, int(math.ceil(b)) + 20
    gx, gy = np.meshgrid(np.arange(x0, x1), np.arange(y0, y1))
    near = ((np.abs(gx - l) < 12) | (np.abs(gx - r) < 12) | (np.abs(gy - t) < 12) | (np.abs(gy - b) < 20))
    ix, iy = gx[near], gy[near]
    px, py = ix + 0.5, iy + 0.5
    parts = pieces(p, s)
    moved = [pieces((f32(p[0] + F(d)), f32(p[1] + F(d))), s) for d in (SHIFT, -SHIFT)]
    found = {}
    for snap in SNAPS:
        meant = paper_at(moved[0], px, py, snap, True) & paper_at(moved[1], px, py, snap, True)
        for top_in in (False, True):
            got = paper_at(parts, px, py, snap, top_in)
            miss = meant & ~got
            if miss.any():
                found[(snap, top_in)] = list(zip(ix[miss].tolist(), iy[miss].tolist()))
    return found


def where(p, s, pixels):
    """Which side of the paper each hole is on."""
    l, t, r, b = sides(p, s)
    out = set()
    for x, y in pixels:
        c = (x + 0.5, y + 0.5)
        d = {"left": abs(c[0] - l), "right": abs(c[0] - r), "top": abs(c[1] - t), "bottom": abs(c[1] - b)}
        out.add(min(d, key=d.get))
    return out


def half_sides(p, s):
    l, t, r, b = sides(p, s)
    return [n for n, v in (("L", l), ("R", r), ("T", t), ("B", b)) if abs(v - math.floor(v) - 0.5) < 1e-6]


# ------------------------------------------------------------ the canvases the port makes
def canvas_for(w, h):
    """screen.c canvas_for, FULLSCREEN on, float32 as in C."""
    ratio = f32(F(w) / F(h))
    if ratio >= f32(F(480) / F(270)):
        units = min(max(f32(F(270) * ratio), F(480)), F(900))
        return int(f32(units + F(0.5))), 270
    units = min(max(f32(F(480) / ratio), F(270)), F(540))
    return 480, int(f32(units + F(0.5)))


SCREENS = [
    ("FULLSCREEN off (any phone)", None),
    ("16:9   1920x1080", (1920, 1080)),
    ("18:9   2160x1080", (2160, 1080)),
    ("18.5:9 2960x1440", (2960, 1440)),
    ("19:9   2280x1080", (2280, 1080)),
    ("19.5:9 2340x1080", (2340, 1080)),
    ("19.5:9 3120x1440 (owner)", (3120, 1440)),
    ("20:9   2400x1080", (2400, 1080)),
    ("20:9   2712x1220", (2712, 1220)),
    ("20:9   3200x1440", (3200, 1440)),
    ("21:9   2520x1080", (2520, 1080)),
    ("21:9   3840x1644", (3840, 1644)),
    ("4:3    2048x1536 tablet", (2048, 1536)),
    ("3:2    2160x1440 tablet", (2160, 1440)),
    ("16:10  2560x1600 tablet", (2560, 1600)),
    ("5:4    2208x1768 fold", (2208, 1768)),
]


def canvases():
    seen = {}
    for name, scr in SCREENS:
        c = (480, 270) if scr is None else canvas_for(*scr)
        seen.setdefault(c, []).append(name)
    return seen


def card_tweak(want, canvas, most):
    """The local tweak the fix replaces (spawner.c card_on_whole_units, 06.10.2026)."""
    edge = f32(f32(F(canvas) * F(0.5)) - f32(F(want) * F(0.5)))
    off = edge - math.floor(edge)
    if abs(off - 0.5) > 0.25:
        return want
    return want + 1 if want + 1 <= most else want - 1


def dialogs(canvas):
    """Every dialog drawn by 0x7fac64, with the sizes it can have on this canvas."""
    cw, ch = canvas
    out = [("spawner card", (420, 236), "port, 420 x 236 (approved v6)")]
    # The two game popups (0x67422c): CONTROLLER_DISCONNECTED, RESELECT_CONTROLLER.
    # Size (-1, -1): 0x762ab8 sizes them to the text - width = widest line + 2 x 25,
    # wrapped at canvas / 2 - 50; height = lines + 15 x 1.65 - and 0x7fb3fc floors it.
    # Its parity is the language's and the platform text's, so all four are tried.
    for w in (int(cw * 0.5) - 1, int(cw * 0.5)):
        for h in (98, 99):
            out.append(("controller popup", (w, h), "text-sized: w %s, h %s" % (
                "odd" if w % 2 else "even", "odd" if h % 2 else "even")))
    return out


# ------------------------------------------------------------ the table
def verdict(p, s):
    found = holes(p, s)
    if not found:
        return "clean", ""
    sidesets = set()
    rules = []
    for (snap, top_in), px in found.items():
        sidesets |= where(p, s, px)
        rules.append("%s/%s" % ("1/256" if snap else "exact", "top-in" if top_in else "top-out"))
    return "STRIPE " + "+".join(sorted(sidesets)), ", ".join(sorted(rules))


def selftest():
    p, s = place((585, 270), (420, 236))
    found = holes(p, s)
    snapped = [px for (snap, _), px in found.items() if snap]
    cols = {x for pxs in snapped for x, y in pxs}
    ok = bool(snapped) and cols == {82} and all(where(p, s, px) == {"left"} for px in snapped)
    p2, s2 = place((585, 270), (420, 236), whole=True)
    ok = ok and not holes(p2, s2)
    print("self-test (the screenshot, 585 / 420 x 236): hole columns %s, left only: %s; after the fix: %s"
          % (sorted(cols), all(where(p, s, px) == {"left"} for px in snapped), "clean" if not holes(p2, s2) else "HOLES"))
    if not ok:
        sys.exit("the model does not reproduce the owner's screenshot - no table")


def table():
    selftest()
    rows = []
    for c, names in sorted(canvases().items()):
        for name, size, note in dialogs(c):
            old = place(c, size)
            new = place(c, size, whole=True)
            before = verdict(*old)
            after = verdict(*new)
            tweak = ""
            if name == "spawner card":
                tw = card_tweak(size[0], c[0], c[0] - 60)
                tweak = "%d -> %s" % (tw, verdict(*place(c, (tw, size[1])))[0]) if tw != size[0] else "-"
            rows.append((c, names, name, size, note, " ".join(half_sides(*old)) or "-", before,
                         " ".join(half_sides(*new)) or "-", after, tweak))
    print()
    print("%-9s %-17s %-10s %-26s %-9s %-26s %-7s %-6s %s" % (
        "canvas", "dialog", "size", "sizing", "at .5", "game as is", "at .5", "fixed", "spawner tweak (old fix)"))
    for c, names, name, size, note, h0, b, h1, a, tw in rows:
        print("%-9s %-17s %-10s %-26s %-9s %-26s %-7s %-6s %s" % (
            "%dx%d" % c, name, "%dx%d" % size, note[:26], h0, b[0], h1, a[0], tw))
    print()
    print("canvases: " + "; ".join("%dx%d = %s" % (c[0], c[1], ", ".join(n)) for c, n in sorted(canvases().items())))
    bad_after = [r for r in rows if r[8][0] != "clean" or r[7] != "-"]
    bad_before = [r for r in rows if r[6][0] != "clean"]
    print("rules that show a hole (game as is): " + "; ".join(sorted({
        "%s %dx%d: %s" % (r[2], r[0][0], r[0][1], r[6][1]) for r in bad_before})[:6]) + (" ..." if len(bad_before) > 6 else ""))
    print("before the fix: %d of %d rows have a hole; after: %d" % (len(bad_before), len(rows), len(bad_after)))
    return 1 if bad_after else 0


# ------------------------------------------------------------ the opening animation
def open_frames():
    """0x7fa3dc state 0 and 0x7fa600: slide y -223 -> 0 in 112 ms; squash (0.6,1.4) held
    96 ms, to (1.2,0.8) in 32, to (1,1) in 32, then state 1 (open) at (1,1)."""
    keys = [((0.6, 1.4), 96), ((0.6, 1.4), 32), ((1.2, 0.8), 32), ((1.0, 1.0), 16)]
    out = []
    for k in range(0, 14):
        t = int(k * 1000 / 60)
        a = min(F(t) / F(112), F(1))
        slide = (F(0), f32(F(-223) + f32(f32(F(0) - F(-223)) * a)))
        seg_t, sq = t, None
        frm = keys[0][0]
        for i in range(3):
            to, dur = keys[i + 1][0], keys[i][1]
            if seg_t < dur:
                u = F(seg_t) / F(dur)
                sq = tuple(f32(F(frm[j]) + f32(f32(F(to[j]) - F(frm[j])) * u)) for j in range(2))
                break
            seg_t -= dur
            frm = to
        out.append((t, slide, sq or (F(1), F(1))))
    return out


def first_row(p, s, snap=256):
    """The first pixel row and column the fill covers: what the eye sees of the paper's corner."""
    q = pieces(p, s)[0][1]
    l, t = float(q[0][0]), float(q[0][1])
    l, t = round(l * snap) / snap, round(t * snap) / snap
    col = math.floor(l + 0.5)        # the first centre right of the side: one ON a left side is out
    row = math.ceil(t - 0.5)         # the first centre at or below the top (either rule: one row at most)
    return col, row


def slide():
    for c in ((585, 270), (480, 270), (600, 270)):
        for size in ((420, 236), (301, 99)):
            print("canvas %dx%d, dialog %dx%d" % (c + size))
            print("   ms  squash        width  top-row old/new  left-col old/new  holes old/new")
            prev = None
            for t, sl, sq in open_frames():
                o, n = place(c, size, sl, sq), place(c, size, sl, sq, whole=True)
                ro, rn = first_row(*o), first_row(*n)
                ho, hn = holes(*o), holes(*n)
                print("  %3d  %4.2f x %4.2f  %5d  %4d / %4d       %4d / %4d       %s / %s" % (
                    t, sq[0], sq[1], int(o[1][0]), ro[1], rn[1], ro[0], rn[0],
                    "+".join(sorted({x for v in ho.values() for x in where(*o, v)})) or "-",
                    "+".join(sorted({x for v in hn.values() for x in where(*n, v)})) or "-"))
            print()


# ------------------------------------------------------------ the fix's words
COPIES = (0x38448, 0x17e630, 0x364924, 0x7fb3fc)   # AB, AB+, the third copy, Repentance
NEW = [  # from +0x50, after `adrp x8 / add x8, x8, #0xff0` (x8 = &canvas)
    (0x2D400D02, "ldp s2, s3, [x8]"),
    (0x2D4806A0, "ldp s0, s1, [x21, #0x40]"),
    (0x1E200940, "fmul s0, s10, s0"),
    (0x1E254000, "frintm s0, s0"),
    (0x1E210961, "fmul s1, s11, s1"),
    (0x1E254021, "frintm s1, s1"),
    (0x2D000660, "stp s0, s1, [x19]"),
    (0x1E2C1004, "fmov s4, #0.50000000"),
    (0x1F042042, "fmadd s2, s2, s4, s8"),
    (0x1F042463, "fmadd s3, s3, s4, s9"),
    (0x1E240800, "fmul s0, s0, s4"),
    (0x1E240821, "fmul s1, s1, s4"),
    (0x1E203842, "fsub s2, s2, s0"),
    (0x1E254042, "frintm s2, s2"),
    (0x1E202842, "fadd s2, s2, s0"),
    (0x1E213863, "fsub s3, s3, s1"),
    (0x1E254063, "frintm s3, s3"),
    (0x1E212863, "fadd s3, s3, s1"),
    (0x2D000E82, "stp s2, s3, [x20]"),
    (0xD503201F, "nop"),
    (0xD503201F, "nop"),
    (0xD503201F, "nop"),
]


def words():
    from capstone import Cs, CS_ARCH_ARM64, CS_MODE_ARM
    md = Cs(CS_ARCH_ARM64, CS_MODE_ARM)
    d = open(BIN, "rb").read()
    bad = 0
    for w, text in NEW:
        got = list(md.disasm(struct.pack("<I", w), 0))
        dis = "%s %s" % (got[0].mnemonic, got[0].op_str) if got else "?"
        flag = "" if " ".join(dis.split()) == " ".join(text.split()) else "   <<< MISMATCH"
        bad += bool(flag)
        print("  %08x  %-28s %s" % (w, dis, flag))
    ref = [struct.unpack_from("<I", d, COPIES[-1] + 0x50 + 4 * i)[0] for i in range(len(NEW))]
    for at in COPIES:
        old = [struct.unpack_from("<I", d, at + 0x50 + 4 * i)[0] for i in range(len(NEW))]
        same = sum(1 for a, b in zip(old, ref) if a == b)
        print("  copy %06x: %d of %d words as Repentance's (the rest are pc-relative)" % (at, same, len(NEW)))
    if bad:
        sys.exit("encodings do not match")


def emulate():
    """0x7fb3fc and its copies run on unicorn: the image's words, and the same with NEW laid
    over +0x50, held against place() for random canvases, sizes, slides and squashes."""
    import random
    from unicorn import Uc, UC_ARCH_ARM64, UC_MODE_ARM, UC_HOOK_CODE
    from unicorn import arm64_const as A
    old = open(BIN, "rb").read()[:0x1000000]
    new = bytearray(old)
    for at in COPIES:
        struct.pack_into("<%dI" % len(NEW), new, at + 0x50, *[w for w, _ in NEW])
    anim_of = {0x7fb3fc: 0x7fa254, 0x38448: 0x372a0, 0x17e630: 0x17d448, 0x364924: 0x36373c}
    bits = lambda v: struct.unpack("<I", struct.pack("<f", v))[0]

    def run(img, fn, canvas, size, slide, squash):
        mu = Uc(UC_ARCH_ARM64, UC_MODE_ARM)
        mu.mem_map(0, 0x1000000)
        mu.mem_write(0, bytes(img))
        mu.reg_write(A.UC_ARM64_REG_CPACR_EL1, 3 << 20)
        mu.mem_write(0xa9aff0, struct.pack("<ff", *canvas))
        obj, sp, ret = 0xc00000, 0xe00000, 0xff0000
        mu.mem_write(obj + 0x40, struct.pack("<ff", *size))

        def anim(mu, addr, n, ud):              # the animation read: +0x90 the slide, +0xb0 the squash
            v = slide if mu.reg_read(A.UC_ARM64_REG_X0) == obj + 0x90 else squash
            mu.reg_write(A.UC_ARM64_REG_S0, bits(v[0]))
            mu.reg_write(A.UC_ARM64_REG_S1, bits(v[1]))
            mu.reg_write(A.UC_ARM64_REG_PC, mu.reg_read(A.UC_ARM64_REG_LR))
        mu.hook_add(UC_HOOK_CODE, anim, begin=anim_of[fn], end=anim_of[fn])
        for r, v in ((A.UC_ARM64_REG_SP, sp), (A.UC_ARM64_REG_X0, obj), (A.UC_ARM64_REG_X1, sp + 0x100),
                     (A.UC_ARM64_REG_X2, sp + 0x108), (A.UC_ARM64_REG_LR, ret)):
            mu.reg_write(r, v)
        mu.emu_start(fn, ret)
        return struct.unpack("<ff", mu.mem_read(sp + 0x100, 8)), struct.unpack("<ff", mu.mem_read(sp + 0x108, 8))

    random.seed(6)
    cases = [((585, 270), (420, 236), (0, 0), (1, 1)), ((480, 270), (301, 99), (0, 0), (1, 1))]
    for _ in range(300):
        cases.append(((random.choice(sorted({c[0] for c in canvases()})), random.choice([270, 300, 320, 360, 384])),
                      (random.randint(100, 460), random.randint(40, 250)),
                      (0.0, float(F(random.uniform(-233, 0)))),
                      (float(F(random.uniform(0.5, 1.5))), float(F(random.uniform(0.5, 1.5))))))
    bad = 0
    for fn in COPIES:
        for c, size, sl, sq in cases:
            for img, whole in ((old, False), (new, True)):
                got = run(img, fn, c, size, sl, sq)
                p, s = place(c, size, sl, sq, whole=whole)
                want = ((float(p[0]), float(p[1])), (float(s[0]), float(s[1])))
                corner = (got[0][0] - got[1][0] / 2, got[0][1] - got[1][1] / 2)
                if got != want or (whole and any(v != math.floor(v) for v in corner)):
                    bad += 1
                    if bad <= 8:
                        print("MISMATCH %06x %s %s %s %s %s: %s, model %s" % (fn, "new" if whole else "old", c, size, sl, sq, got, want))
    print("%d cases x %d copies x game/fixed on unicorn: %d differ from the model" % (len(cases), len(COPIES), bad))
    return 1 if bad else 0


if __name__ == "__main__":
    if "--emulate" in sys.argv:
        sys.exit(emulate())
    elif "--words" in sys.argv:
        words()
    elif "--slide" in sys.argv:
        slide()
    else:
        sys.exit(table())
