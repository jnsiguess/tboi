# -*- coding: utf-8 -*-
"""Straps in six languages, filling the area the owner's own lettering fills.

The measure is taken off his four English straps, along the strap's slope:
the length and height of his lettering and its centre, and his stroke. Every
translation is brought to exactly that box - scaled, and stretched or squeezed
along one axis within limits. The scaling is done on an 8x copy (Scale2x for the
game's bitmap cuts, a vector face drawn large) and every final point takes the
share of ink over its own 8x8 square; the ink and the light tone are then laid
the way the owner's are. Where a word will not go into his length even squeezed,
the strap is lengthened: cut in the middle of its body, and the gap filled with
the strap's own columns from just left of the cut, each raised along the slope -
the torn edges, the rim and the shadow carry on as they are, not as a straight
band."""
import math
import os
import sys

import numpy as np
from PIL import Image, ImageDraw, ImageFont

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
OUT_DIR = os.path.join(ROOT, 'other stuff', 'straps-lang')   # the straps and previews
os.makedirs(OUT_DIR, exist_ok=True)
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.join(ROOT, 'tools'))
import strap_base as s2  # noqa: E402
import lang  # noqa: E402
import typeset  # noqa: E402
import smoothing  # noqa: E402

ZH = os.path.join(typeset.RESOURCES, 'dlc3.zh', 'font')
FACE = {'dlc3.ru': ('bm', 'teammeatfont16.fnt', None), 'dlc3.de': ('bm', 'teammeatfont16.fnt', None),
        'dlc3.es': ('bm', 'teammeatfont16.fnt', None),
        'dlc3.zh': ('bm', 'teammeatfontextended16bold.fnt', ZH),
        'dlc3.jp': ('ttf', 'MochiyPopOne-Regular.ttf', lang.FONTS),
        'dlc3.kr': ('ttf', 'GamjaFlower-Regular.ttf', lang.FONTS)}
WORDS = {
    'dlc3.ru': dict(tweaks=['ТВИКИ'], guide=['ГАЙД'], spawner=['КОНСОЛЬ'], credits=['ТИТРЫ/ПОРТА']),
    'dlc3.de': dict(tweaks=None, guide=['HILFE'], spawner=['KONSOLE'], credits=None),
    'dlc3.es': dict(tweaks=['AJUSTES'], guide=['GUÍA'], spawner=['CONSOLA'],
                    credits=['CRÉDITOS/DEL PORT']),
    'dlc3.jp': dict(tweaks=['こまかい/せってい'], guide=['ガイド'], spawner=['コンソール'],
                    credits=['クレジット']),
    'dlc3.kr': dict(tweaks=['세부/설정', '세부 설정'], guide=['가이드'], spawner=['콘솔'],
                    credits=['크레딧']),
    'dlc3.zh': dict(tweaks=['更多选项'], guide=['指南'], spawner=['控制台'],
                    credits=['移植名单']),
}
# The pause strap keeps the owner's size (strap_install: the paper's frames have
# a fixed pivot, and es/jp/kr share data's pausescreen.anm2), so a word longer
# than his box comes down in height until it fits, instead of lengthening it.
FIXED = {'spawner'}
SQUEEZE, STRETCH = 0.70, 1.40      # width over height, against the letters' own
# the owner's letters are drawn narrow and tall (TWEAKS: 9 by 20), so Latin and
# Cyrillic may be narrowed to half; kana, Hangul and ideographs keep their square
SQUEEZE_BY = {'dlc3.ru': 0.5, 'dlc3.de': 0.5, 'dlc3.es': 0.5}
TTF_BIG = 128
# The three the owner sent back (30.09): two lines of kana or Hangul at half the
# strap's height do not read, so these go in one line; and kana and Hangul are
# not narrowed - a square letter pulled out of square read as crooked. Where
# the word is longer than the box, the letters come down to 85 % of its height
# before the strap is lengthened.
FORCE = {('dlc3.jp', 'tweaks'): dict(word='せってい', squeeze=0.95, stretch=1.10, shrink=0.85),
         # on the pause screen the full 아이템 스폰 / 道具生成 has no room between the seed
         # lines and the card: the short word, the owner's choice (30.09) - the spawner
         # will spawn more than items anyway; CONSOLE since 25.10.2026 (itemspawn_title.TITLES)
         ('dlc3.kr', 'spawner'): dict(word='콘솔', squeeze=0.95, stretch=1.10, shrink=0.85),
         ('dlc3.jp', 'credits'): dict(word='クレジット', squeeze=0.95, stretch=1.10, shrink=0.85),
         # 移植名单 in the game's bold cut, the one every Chinese menu uses: at its
         # own size, grown to the owner's height by repeating rows and columns
         ('dlc3.zh', 'credits'): dict(word='移植名单', squeeze=0.95, stretch=1.10, shrink=0.85)}                      # a vector face is drawn at this, the 8x copy of a 16-point line


# ------------------------------------------------------------------ the owner's measure
def frame(slope):
    a = math.atan(slope)
    d = np.array([math.cos(a), -math.sin(a)])      # along the strap, rising to the right
    n = np.array([math.sin(a), math.cos(a)])       # across it, downwards
    return d, n


def measure(name):
    col = s2.FILES[name][1]
    own = s2.owner(name)
    letters = s2.mask_of(own, [col['ink'], col['light']])
    ink = s2.mask_of(own, [col['ink']])
    slope = s2.SLOPE[name]
    d, n = frame(slope)
    ys, xs = np.nonzero(letters)
    p = np.stack([xs + 0.5, ys + 0.5], 1)
    u, v = p @ d, p @ n
    u0, u1, v0, v1 = u.min(), u.max(), v.min(), v.max()
    uc, vc = (u0 + u1) / 2, (v0 + v1) / 2
    centre = uc * d + vc * n
    return dict(slope=slope, d=d, n=n, length=u1 - u0 + 1, height=v1 - v0 + 1, centre=centre,
                stroke=s2.stroke(ink), cover=float(letters.sum()))


# ------------------------------------------------------------------ the 8x copy of a word
def scale2x(m):
    p = np.pad(m, 1, mode='edge')
    P = p[1:-1, 1:-1]; A = p[:-2, 1:-1]; D = p[2:, 1:-1]; C = p[1:-1, :-2]; B = p[1:-1, 2:]
    o = np.zeros((m.shape[0] * 2, m.shape[1] * 2), bool)
    o[0::2, 0::2] = np.where((C == A) & (C != D) & (A != B), A, P)
    o[0::2, 1::2] = np.where((A == B) & (A != C) & (B != D), B, P)
    o[1::2, 0::2] = np.where((D == C) & (D != B) & (C != A), C, P)
    o[1::2, 1::2] = np.where((B == D) & (B != A) & (D != C), D, P)
    return o


def crop(m):
    ys, xs = np.nonzero(m)
    return m[ys.min():ys.max() + 1, xs.min():xs.max() + 1]


def big_line(text, set_name):
    kind, font, folder = FACE[set_name]
    if kind == 'bm':
        a = typeset.compose(text, None, lang=set_name, jitter=False, font=font,
                            folder=folder, thicken=False).getchannel('A')
        m = crop(np.asarray(a) >= 128)
        return crop(scale2x(scale2x(scale2x(m))))
    f = ImageFont.truetype(os.path.join(folder, font), TTF_BIG)
    im = Image.new('L', (int(f.getlength(text)) + TTF_BIG * 2, TTF_BIG * 3), 0)
    ImageDraw.Draw(im).text((TTF_BIG, TTF_BIG * 2), text, font=f, fill=255, anchor='ls')
    return crop(np.asarray(im) >= 128)


def big_block(word, set_name):
    lines = [big_line(t, set_name) for t in word.split('/')]
    lh = max(l.shape[0] for l in lines)
    gap = int(round(lh * 0.18))
    w = max(l.shape[1] for l in lines)
    h = sum(l.shape[0] for l in lines) + gap * (len(lines) - 1)
    out = np.zeros((h, w), bool)
    y = 0
    for l in lines:
        x = (w - l.shape[1]) // 2
        out[y:y + l.shape[0], x:x + l.shape[1]] |= l
        y += l.shape[0] + gap
    return out


# ------------------------------------------------------------------ the strap, lengthened
def lengthened(img, extra, slope, cut):
    """the strap with `extra` columns let in near `cut`: a run of its own columns
    from just left of the cut, repeated if need be, each raised along the slope.
    Where exactly to cut and how long a run to repeat is chosen so that the
    column let in first looks most like the one it stands in for - the torn
    edge, the rim and the shadow run on with no step at the join."""
    if extra <= 0:
        return img, 0, 0
    a = np.asarray(img).astype(int)
    H, W = a.shape[:2]

    def raised(k, by):
        """column k raised by `by` points (the rows it leaves at the bottom empty)"""
        col = np.zeros((H, 4), int)
        s = int(round(by))
        if s >= 0:
            col[:H - s] = a[s:, k]
        else:
            col[-s:] = a[:H + s, k]
        return col

    def edges(col):
        rows = []
        for y in range(1, H):
            if (col[y] != col[y - 1]).any():
                rows.append((y, tuple(col[y - 1]), tuple(col[y])))
        return rows

    def edge_miss(p, q):
        ep, eq = edges(p), edges(q)
        miss = abs(len(ep) - len(eq)) * 3
        for (y1, a1, b1), (y2, a2, b2) in zip(ep, eq):
            miss += abs(y1 - y2) + (0 if (a1, b1) == (a2, b2) else 1)
        return miss

    solid = a[..., 3] > 0
    top = [int(np.nonzero(solid[:, x])[0].min()) if solid[:, x].any() else -1 for x in range(W)]
    bot = [int(np.nonzero(solid[:, x])[0].max()) if solid[:, x].any() else -1 for x in range(W)]
    best = None
    for c in range(max(24, cut - 20), min(W - 12, cut + 21)):
        for P in range(8, min(40, c - 16) + 1):
            # the strap's own rise over the run, off both its edges: the band is
            # bent, not straight, so the letters' slope is not the strap's here
            rt, rb = top[c - P] - top[c], bot[c - P] - bot[c]
            if min(top[c - P], top[c], bot[c - P], bot[c]) < 0 or abs(rt - rb) > 1:
                continue
            local = (rt + rb) / 2.0 / P
            # column c-P set down where column c stood: how unlike column c is it
            cand = raised(c - P, local * P)
            cost = float(np.abs(cand - a[:, c]).sum())
            # the edges: every row where the colour changes, top to bottom, must
            # sit where column c has its own - a step of one point shows
            cost += 400.0 * edge_miss(cand, a[:, c])
            cost += 40.0 * (extra > P)                  # a repeat is a second join
            if best is None or cost < best[0]:
                best = (cost, c, P, local)
    _, cut, P, slope = best
    rise = int(math.ceil(slope * extra)) + 1
    out = np.zeros((H + rise, W + extra, 4), np.uint8)
    out[rise:rise + H, :cut] = a[:, :cut]
    for j in range(extra):
        k = cut - P + (j % P)
        dy = rise - int(round(slope * (cut + j - k)))
        lo, hi = max(0, dy), min(H + rise, dy + H)
        out[lo:hi, cut + j] = a[lo - dy:hi - dy, k]
    dy = rise - int(round(slope * extra))
    out[dy:dy + H, cut + extra:] = a[:, cut:]
    return Image.fromarray(out), rise, slope


# ------------------------------------------------------------------ laying it
def coverage(block, shape, centre, d, n, sx, sy):
    H, W = shape
    yy, xx = np.mgrid[0:H * 8, 0:W * 8].astype(float)
    px, py = (xx + 0.5) / 8 - centre[0], (yy + 0.5) / 8 - centre[1]
    u = px * d[0] + py * d[1]
    v = px * n[0] + py * n[1]
    bx = np.floor(u / sx + block.shape[1] / 2.0).astype(int)
    by = np.floor(v / sy + block.shape[0] / 2.0).astype(int)
    ok = (bx >= 0) & (by >= 0) & (bx < block.shape[1]) & (by < block.shape[0])
    out = np.zeros((H * 8, W * 8), float)
    out[ok] = block[by[ok], bx[ok]]
    return out.reshape(H, 8, W, 8).mean(axis=(1, 3))


def grown(m, r):
    out = m.copy()
    if r < 0:                                   # thinned: a face bolder than the owner's hand
        for _ in range(-r):
            g = out.copy()
            g[1:, :] &= out[:-1, :]; g[:-1, :] &= out[1:, :]
            g[:, 1:] &= out[:, :-1]; g[:, :-1] &= out[:, 1:]
            out = g
        return out
    for _ in range(r):
        g = out.copy()
        g[1:, :] |= out[:-1, :]; g[:-1, :] |= out[1:, :]
        g[:, 1:] |= out[:, :-1]; g[:, :-1] |= out[:, 1:]
        out = g
    return out


def weighed(block, shape, centre, d, n, sx, sy, want, thin=False):
    """the growth of the 8x copy that gives the owner's stroke"""
    best = None
    pad = 12
    b = np.pad(block, pad)
    for r in range(-6 if thin else 0, 11):
        cov = coverage(grown(b, r), shape, centre, d, n, sx, sy)
        st = s2.stroke(cov >= 0.5)
        if best is None or abs(st - want) < abs(best[1] - want):
            best = (cov, st, r)
        if st > want + 0.3 and r > 0:
            break
    return best


def weighted(cov, want, col):
    """the threshold of the coverage that gives the owner's stroke"""
    best = None
    for t in np.arange(0.25, 0.66, 0.05):
        ink = cov >= t
        st = s2.stroke(ink)
        if best is None or abs(st - want) < abs(best[1] - want):
            best = (t, st)
    t = best[0]
    # coverage re-based on the chosen threshold, so the light tone sits in the notches
    return np.clip((cov - t) / max(1e-6, 1 - t) * 0.5 + 0.5 * (cov >= t), 0, 1) * (cov > t * 0.5), best[1]


def lay(word, set_name, name, M):
    block = big_block(word, set_name)
    bw, bh = block.shape[1], block.shape[0]
    f = FORCE.get((set_name, name), {})
    sy = M['height'] / bh
    sx = M['length'] / bw
    r = sx / sy
    extra_len = 0.0
    squeeze = f.get('squeeze', SQUEEZE_BY.get(set_name, SQUEEZE))
    stretch = f.get('stretch', STRETCH)
    if r < squeeze:
        floor = 0.0 if name in FIXED else f.get('shrink', 1.0)
        h = max(floor, min(1.0, M['length'] / (bw * sy * squeeze)))
        sy *= h
        sx = sy * squeeze
        extra_len = max(0.0, bw * sx - M['length'])
        if name in FIXED:
            extra_len = 0.0                     # fitted exactly: rounding only
    elif r > stretch:
        sx = sy * stretch
    return block, sx, sy, r, extra_len


def build(word, set_name, name, M):
    block, sx, sy, r, extra_len = lay(word, set_name, name, M)
    base = s2.sealed(s2.blank(name), s2.FILES[name][1])[0]
    extra = int(math.ceil(extra_len * M['d'][0])) if extra_len > 0 else 0
    centre = M['centre'].copy()
    rise = 0
    if extra:
        cut = int(round(centre[0]))
        base, rise, local = lengthened(base, extra, M['slope'], cut)
        # the box moves with the strap: half the columns let in, and as high as
        # the strap itself rose over them
        centre = centre + np.array([extra / 2.0, rise - local * extra / 2.0])
    cov, stroke, grow = weighed(block, base.size[::-1], centre, M['d'], M['n'], sx, sy, M['stroke'],
                                 thin=(set_name, name) in FORCE and set_name != 'dlc3.zh')   # thinning gnaws ideographs   # the approved ones stay as they were
    cov, kept = pieces_kept(cov, block, grow, base.size[::-1], centre, M['d'], M['n'], sx, sy)
    col = s2.FILES[name][1]
    letters, share = smoothing.smooth(Image.fromarray((cov * 255).round().astype(np.uint8)),
                                      tone=col['light'] + (255,), ink=col['ink'])
    if FACE[set_name][0] == 'bm':
        # lone points the edges of the scaling leave; not for kana and Hangul,
        # where a small dot is a part of the letter (the dakuten of ジ, 30.09)
        letters = lang.without_specks(letters)
    base.alpha_composite(letters)
    return base, dict(word=word, ratio=r, sx=sx, sy=sy, extra=extra, stroke=stroke,
                      share=share, cover=float((np.asarray(letters)[..., 3] > 0).sum()))


def pieces_of(m):
    """the separate pieces of a letter mask (8-connected), as lists of points"""
    H, W = m.shape
    seen = np.zeros(m.shape, bool)
    out = []
    ys, xs = np.nonzero(m)
    for y, x in zip(ys, xs):
        if seen[y, x]:
            continue
        stack, pts = [(y, x)], []
        seen[y, x] = True
        while stack:
            cy, cx = stack.pop()
            pts.append((cy, cx))
            for dy in (-1, 0, 1):
                for dx in (-1, 0, 1):
                    ny, nx = cy + dy, cx + dx
                    if 0 <= ny < H and 0 <= nx < W and m[ny, nx] and not seen[ny, nx]:
                        seen[ny, nx] = True
                        stack.append((ny, nx))
        out.append(pts)
    return out


def pieces_kept(cov, block, grow, shape, centre, d, n, sx, sy):
    """EVERY PIECE OF A LETTER STAYS. A dakuten, the dot of an i, an accent:
    brought down to the strap's size a small piece can fall below the half-ink
    threshold and vanish, or keep a point or two and read as a stray mark (the
    dakuten of ジ, the owner, 30.09.2026). So each separate piece of the 8x copy
    is laid down on its own, and where it would keep fewer than four points of
    ink, the 2x2 square of its own greatest coverage is inked."""
    pad = 12
    g = grown(np.pad(block, pad), grow)
    ink = cov >= 0.5
    kept = 0
    for pts in pieces_of(g):
        ys = np.array([p[0] for p in pts]); xs = np.array([p[1] for p in pts])
        one = np.zeros(g.shape, bool)
        one[ys, xs] = True
        # only the piece's own box is laid, to keep it quick
        c = coverage(one, shape, centre, d, n, sx, sy)
        foot = c > 0.05
        if not foot.any() or int((ink & foot).sum()) >= 4:
            continue
        best, at = -1.0, None
        fy, fx = np.nonzero(foot)
        for y in range(max(0, fy.min() - 1), min(shape[0] - 1, fy.max() + 1)):
            for x in range(max(0, fx.min() - 1), min(shape[1] - 1, fx.max() + 1)):
                v = c[y:y + 2, x:x + 2].sum()
                if v > best:
                    best, at = v, (y, x)
        if at:
            y, x = at
            cov[y:y + 2, x:x + 2] = np.maximum(cov[y:y + 2, x:x + 2], 1.0)
            kept += 1
    return cov, kept


# ------------------------------------------------------------------ ideographs, grown whole
# Scaled through the 8x copy, the bold cut fills the cells of 单 at any weight,
# and thinning it gnaws the strokes (the owner, 30.09). So Chinese that has to
# grow is grown by repeating rows and columns of the cut at its own size: a gap
# can only widen, never close.
NATIVE = {('dlc3.zh', n) for n in ('tweaks', 'guide', 'spawner', 'credits')}


def repeat_to(m, n, axis):
    """m with rows (axis 0) or columns (axis 1) repeated, evenly spread, up to n"""
    have = m.shape[axis]
    if n <= have:
        return m
    idx = np.floor(np.arange(n) * have / float(n)).astype(int)
    return np.take(m, idx, axis=axis)


def bolder(g):
    """One point more of stroke, right and down, the way the owner's hand is
    heavier than the cut: a point of paper turns to ink only if paper still
    lies beyond it, so every gap keeps at least one point and none closes."""
    g = np.pad(g, ((0, 2), (0, 2)))
    out = g.copy()
    # right: ink here, paper next, and paper after that
    out[:, 1:-1] |= g[:, :-2] & ~g[:, 1:-1] & ~g[:, 2:]
    r = out.copy()
    # down, against what the rightward growth left
    out[1:-1, :] |= r[:-2, :] & ~r[1:-1, :] & ~r[2:, :]
    return out


def native_zh(word, name, M, font='teammeatfontextended12.fnt', height=None, pad=0):
    """The game's own Chinese cut, doubled whole: the 12 draws every stroke one
    point wide, so 移 and 植 stay open, and twice it (26 high) is the owner's
    lettering height with two-point strokes and two-point gaps - nothing
    resampled. Each ideograph is set on the strap's own middle line, so a
    lengthened, bent strap carries the line where the strap runs."""
    folder = FACE['dlc3.zh'][2]
    # set with a space between the characters: an ideograph has empty columns
    # inside it (工 and 页 in 项), a space is wider than any of them
    a = typeset.compose(' '.join(word), None, lang='dlc3.zh', jitter=False, font=font,
                        folder=folder, thicken=False).getchannel('A')
    m = crop(np.asarray(a) >= 128)
    occ = m.any(0)
    runs, x = [], 0
    while x < len(occ):
        if occ[x]:
            s0 = x
            while x < len(occ) and occ[x]:
                x += 1
            runs.append((s0, x))
        else:
            x += 1
    while len(runs) > len(word):          # the glyphs: runs joined across the narrowest gaps
        gaps = [runs[i + 1][0] - runs[i][1] for i in range(len(runs) - 1)]
        i = int(np.argmin(gaps))
        runs[i:i + 2] = [(runs[i][0], runs[i + 1][1])]
    native_gap = 1                         # the cut's own spacing between two characters
    # to the owner's height exactly: rows and columns repeated, most twice, a
    # few once - a gap can widen, never close
    Ht = int(round(height or M['height']))
    k = Ht / float(m.shape[0])
    glyphs = [crop(bolder(repeat_to(repeat_to(m[:, s0:e0], Ht - 1, 0), int(round((e0 - s0) * k)) - 1, 1)))
              for s0, e0 in runs]
    Ht = max(g.shape[0] for g in glyphs)
    glyphs = [np.pad(g, ((0, Ht - g.shape[0]), (0, 0))) for g in glyphs]
    gap = max(2, int(round(native_gap * k)))
    span = sum(g.shape[1] for g in glyphs) + gap * (len(glyphs) - 1)
    H = Ht

    base = s2.sealed(s2.blank(name), s2.FILES[name][1])[0]
    centre = M['centre'].copy()
    length = span / M['d'][0]
    extra = int(math.ceil(max(0.0, length - M['length']) * M['d'][0])) + pad
    over = extra
    if name in FIXED:
        extra = 0                          # the pause strap is not lengthened: `over` says by how much it spills
    if extra:
        base, rise, local = lengthened(base, extra, M['slope'], int(round(centre[0])))
        centre = centre + np.array([extra / 2.0, rise - local * extra / 2.0])
    W, Hh = base.size
    body = np.asarray(base)[..., 3] == 255
    col = s2.FILES[name][1]
    face = s2.mask_of(base, [col['body']])

    def middle(x):
        ys = np.nonzero(face[:, int(x)])[0]
        return (ys.min() + ys.max()) / 2.0 if len(ys) else None

    # the strap's middle line under the whole word, fitted straight: column by
    # column it jumps at the torn edges, and the letters must not
    x_from = int(round(centre[0] - span / 2.0))
    xs = [x for x in range(x_from, x_from + span) if middle(x) is not None]
    fit = np.polyfit(xs, [middle(x) for x in xs], 1)
    # the owner's lettering sits this far off the strap's middle line
    off = centre[1] - np.polyval(fit, centre[0])
    cov = np.zeros((Hh, W), np.uint8)
    pen = x_from
    for g in glyphs:
        w = g.shape[1]
        cx = pen + w / 2.0
        cy = np.polyval(fit, cx) + off
        y0 = int(round(cy - H / 2.0))
        cov[y0:y0 + H, pen:pen + w] = np.maximum(cov[y0:y0 + H, pen:pen + w], g * 255)
        pen += w + gap
    # the same room above and below: the least margin to the strap's edge, over
    # every column the word covers, evened out
    inked = [x for x in range(W) if (cov[:, x] > 0).any() and face[:, x].any()]
    up = min(np.nonzero(cov[:, x])[0].min() - np.nonzero(face[:, x])[0].min() for x in inked)
    down = min(np.nonzero(face[:, x])[0].max() - np.nonzero(cov[:, x])[0].max() for x in inked)
    shift = int((down - up) // 2)
    if shift:
        cov = np.roll(cov, shift, axis=0)
    letters, share = smoothing.smooth(Image.fromarray(cov), tone=col['light'] + (255,), ink=col['ink'])
    base.alpha_composite(letters)
    return base, dict(k=k, extra=extra, over=over, margins=(up + shift, down - shift), stroke=s2.stroke(cov > 0), share=share, size=(H, span))


PADS = (0, 4, 8, 12, 16)


def zh_fits(word, name, M, h):
    """the least extra lengthening at which the word stands at height h on the
    strap's face with a point to spare, or None; the pause strap is not lengthened"""
    for pad in PADS if name not in FIXED else (0,):
        im, info = native_zh(word, name, M, height=h, pad=pad)
        # the cut is never shrunk (13 high at the least): on the pause strap the
        # word may pass the owner's box by a point, the strap runs on past it
        if min(info['margins']) >= 1 and (name not in FIXED or info['over'] <= 1):
            return pad, im, info
    return None


def zh_common(words, Ms):
    """one height for every Chinese strap: the tallest all of them take; the
    pause strap, which keeps its size, the tallest it takes up to that"""
    free = {n: w for n, w in words.items() if n not in FIXED}
    h = int(round(min(Ms[n]['height'] for n in free)))
    while h > 14:
        got = {n: zh_fits(w, n, Ms[n], h) for n, w in free.items()}
        if all(got.values()):
            break
        h -= 1
    else:
        raise SystemExit('no common height for the Chinese straps')
    heights = {n: h for n in free}
    for n in set(words) - set(free):
        k = h
        while k > 8 and not zh_fits(words[n], n, Ms[n], k):
            k -= 1
        got[n] = zh_fits(words[n], n, Ms[n], k)
        if not got[n]:
            raise SystemExit('the Chinese %s does not fit its strap' % n)
        heights[n] = k
    return heights, got


def main():
    sys.stdout.reconfigure(encoding='utf-8')
    Ms = {n: measure(n) for n in s2.ORDER}
    for n in s2.ORDER:
        M = Ms[n]
        print('owner %-8s length %.1f height %.1f stroke %.2f cover %d' % (n, M['length'], M['height'], M['stroke'], M['cover']))
    rows = [('en', {n: s2.owner(n) for n in s2.ORDER})]
    zh_h, zh_got = zh_common({n: WORDS['dlc3.zh'][n][0] for n in s2.ORDER}, Ms)
    print('zh heights', zh_h)
    for set_name, suf in s2.SETS:
        row = {}
        for n in s2.ORDER:
            cands = WORDS[set_name][n]
            if cands is None:
                row[n] = s2.owner(n)
                continue
            best = None
            if (set_name, n) in FORCE:
                cands = [FORCE[(set_name, n)]['word']]
            for w in cands:
                if len(cands) == 1:
                    best = (0, w)
                    break
                block, sx, sy, r, extra_len = lay(w, set_name, n, Ms[n])
                squeeze = SQUEEZE_BY.get(set_name, SQUEEZE)
                unfilled = Ms[n]['length'] * (1 - STRETCH / r) if r > STRETCH else 0.0
                bent = abs(math.log(min(max(r, squeeze), STRETCH)))
                score = unfilled + max(0.0, extra_len) + bent * 8
                if best is None or score < best[0]:
                    best = (score, w)
            if (set_name, n) in NATIVE:
                pad, im, nat = zh_got[n]
                nat['height'] = zh_h[n]
                info = dict(word=best[1] + ' h%d m%d/%d' % (nat['height'], nat['margins'][0], nat['margins'][1]), ratio=1.0, extra=nat['extra'], stroke=nat['stroke'], share=nat['share'],
                            cover=float((np.asarray(im)[..., 3] > 0).sum()))
            else:
                im, info = build(best[1], set_name, n, Ms[n])
            row[n] = im
            im.save(os.path.join(OUT_DIR, '%s_%s.png' % (suf, n)))
            print('%s %-8s %-18s ratio %.2f  extra %2d px  stroke %.2f (his %.2f)  cover %d (his %d)  tone %.1f%%'
                  % (suf, n, info['word'], info['ratio'], info['extra'], info['stroke'], Ms[n]['stroke'],
                     info['cover'], Ms[n]['cover'], info['share']))
        rows.append((suf, row))
    Z = 3
    cw = max(max(im.size[0] for im in row.values()) for _, row in rows) * Z + 16
    ch = max(max(im.size[1] for im in row.values()) for _, row in rows) * Z + 12
    sheet = Image.new('RGB', (60 + cw * 4, ch * len(rows) + 30), (40, 36, 34))
    d = ImageDraw.Draw(sheet)
    lab = ImageFont.truetype('arial.ttf', 15)
    for j, n in enumerate(s2.ORDER):
        d.text((60 + j * cw, 6), n.upper(), fill=(230, 220, 210), font=lab)
    for i, (suf, row) in enumerate(rows):
        d.text((10, 30 + i * ch + ch // 2 - 8), suf.upper(), fill=(230, 220, 210), font=lab)
        for j, n in enumerate(s2.ORDER):
            im = row[n]
            big = im.resize((im.size[0] * Z, im.size[1] * Z), Image.NEAREST)
            sheet.paste(big, (60 + j * cw, 30 + i * ch + (ch - 12 - big.size[1])), big)
    sheet.save(os.path.join(OUT_DIR, 'preview.png'))


if __name__ == '__main__':
    main()
