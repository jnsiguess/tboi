# -*- coding: utf-8 -*-
"""PREVIEW ONLY: the four straps in ru, de, es, jp, kr, zh, by the port's own
recorded method for lettering. Writes only into this scratch folder.

The method (where it is written down):
  lang.py, above TITLE_FONT  - "THE TITLE IS SET AT THE FONT'S OWN SIZE AND IS
      NEVER RESAMPLED ... The slope and the bow are laid by whole letters ...
      turning the raster instead leaves one half of a letter a pixel above the
      other. The weight is made up by ONE STEP DOWNWARDS."
  typeset._assemble          - "A line laid along a slope ... moves WHOLE LETTERS."
  guide_strap.py (docstring) - "one letter at a time, each a step higher than the
      one before - the rise his TWEAKS has - in his two colours; nothing is
      turned or scaled, so no pixel falls between texels."
  smoothing.py               - solid ink, one tone in every notch of the staircase.
  lang.SETTINGS / tweaks_page.title_art - the face of each language, at its own
      size (Team Meat cuts; extended cuts for zh; Mochiy Pop / Gamja Flower
      rasterised AT the size, never shrunk, for jp / kr).

So: every glyph is the cut's own bitmap (or a vector rasterised at the size
wanted), laid whole on the owner's slope by an integer offset, grown at most
one row down, then ink + notch tone in the strap's own two colours. Nothing is
resized, rotated or sheared, and the font's own advance is never tightened -
a word that does not fit takes a smaller cut or its second form.
"""
import hashlib
import os
import random
import sys

import numpy as np
from PIL import Image, ImageDraw, ImageFont

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
sys.path.insert(0, os.path.join(ROOT, 'tools'))
import credits_page as cp          # noqa: E402
import guide_strap as gs           # noqa: E402
import lang                        # noqa: E402
import smoothing                   # noqa: E402
import strap_layers as sl          # noqa: E402
import typeset                     # noqa: E402

OUT = os.path.dirname(os.path.abspath(__file__))
RES = sl.RES
SETS = [('dlc3.ru', 'ru'), ('dlc3.de', 'de'), ('dlc3.es', 'es'),
        ('dlc3.jp', 'jp'), ('dlc3.kr', 'kr'), ('dlc3.zh', 'zh')]
ORDER = ['tweaks', 'guide', 'spawner', 'credits']
OPT = dict(body=(246, 210, 197), ink=(56, 39, 41), light=(164, 151, 154))
TTL = dict(body=(236, 209, 196), ink=(55, 43, 45), light=(159, 150, 152))
FILES = {
    'tweaks': (os.path.join('gfx', 'ui', 'main menu', 'port_strap_tweaks.png'), OPT),
    'guide': (os.path.join('gfx', 'ui', 'main menu', 'port_strap_guide.png'), OPT),
    'spawner': (os.path.join('gfx', 'ui', 'port_strap_spawner.png'), TTL),
    'credits': (os.path.join('gfx', 'ui', 'main menu', 'port_strap_credits.png'), TTL),
}

# None = the owner's drawing as it is (the word is the English one letter for
# letter). '/' parts the lines. Candidates in order: the game's own word first
# (tweaks_page.SETS titles, itemspawn_title.TITLES), shorter forms after.
WORDS = {
    'dlc3.ru': dict(tweaks=['ТВИКИ'], guide=['ГАЙД'], spawner=['СПАВН'], credits=['ТИТРЫ/ПОРТА']),
    'dlc3.de': dict(tweaks=None, guide=['HILFE'], spawner=None, credits=None),
    'dlc3.es': dict(tweaks=['AJUSTES'], guide=['GUÍA'], spawner=['SPAWN DE/OBJETOS', 'SPAWN'],
                    credits=['CRÉDITOS/DEL PORT', 'CRÉDITOS', 'AUTORES', 'EQUIPO']),
    'dlc3.jp': dict(tweaks=['こまかい/せってい'], guide=['ガイド'], spawner=['アイテム/スポーン'],
                    credits=['クレジット']),
    'dlc3.kr': dict(tweaks=['세부/설정'], guide=['가이드'], spawner=['아이템/스폰'], credits=['크레딧']),
    'dlc3.zh': dict(tweaks=['更多/选项'], guide=['指南'], spawner=['道具/生成'], credits=['移植/名单']),
}

DATA_FONT = os.path.join(RES, 'data', 'font')
ZH_FONT = os.path.join(RES, 'dlc3.zh', 'font')
# faces, largest first: (kind, font, size, growth). Growth 'down' is the title's
# TITLE_BOLD - one row down, which cannot close a gap between neighbours.
# Latin / Cyrillic: the cut, largest first; at each cut the growth whose
# stroke comes nearest the owner's is taken - 'grown' is the menu rows' (right
# and down, gaps kept; lang.compose_like_native), 'down' the title's.
LATIN = [[('bm', 'teammeatfont%d.fnt' % c, 0, g) for g in ('grown', 'down', None)] for c in (16, 12, 10)]
FACES = {
    'latin': LATIN,
    'dlc3.zh': [[('zh', 'teammeatfontextended16bold.fnt', 0, None)], [('zh', 'teammeatfontextended12.fnt', 0, None)],
                [('zh', 'teammeatfontextended10.fnt', 0, None)]],
    'dlc3.jp': [[('ttf', 'MochiyPopOne-Regular.ttf', s, None)] for s in range(20, 10, -1)],
    # Korean: lang.SETTINGS grows Gamja a point at 22; at strap sizes the growth
    # can close the jamo; only where no grown size fits does it go without
    'dlc3.kr': ([[('ttf', 'GamjaFlower-Regular.ttf', s, 'grown')] for s in range(24, 12, -1)]
                + [[('ttf', 'GamjaFlower-Regular.ttf', s, None)] for s in range(24, 12, -1)]),
}
# The owner's slope, off the feet of his own letters (whole letters, first
# line): ITEM 28,25,23,20 over 26..54; PORT 28,25,23,19 over 22..51; CREDITS
# 40..29 over 24..61; GUIDE 34..24 over 26..65; TWEAKS (one joined blob) the T's
# stem 38 at 26 to the S 25 at 64. Rise per column, the far end up.
SLOPE = {'tweaks': 0.33, 'guide': 0.26, 'spawner': 0.30, 'credits': 0.30}


# ------------------------------------------------------------------ owner
def owner(name):
    return Image.open(os.path.join(RES, 'data', FILES[name][0])).convert('RGBA')


def mask_of(im, colours):
    a = np.asarray(im).astype(int)
    m = np.zeros(a.shape[:2], bool)
    for c in colours:
        m |= (a[..., 0] == c[0]) & (a[..., 1] == c[1]) & (a[..., 2] == c[2]) & (a[..., 3] == 255)
    return m


def erode(m):
    e = m.copy()
    e[1:, :] &= m[:-1, :]; e[:-1, :] &= m[1:, :]
    e[:, 1:] &= m[:, :-1]; e[:, :-1] &= m[:, 1:]
    return e


def stroke(ink):
    edge = int((ink & ~erode(ink)).sum())
    return 2.0 * ink.sum() / edge if edge else 0.0


def geometry(name):
    """the owner's lettering: centre, slope of its foot, stroke; where ours may go"""
    col = FILES[name][1]
    own = owner(name)
    letters = mask_of(own, [col['ink'], col['light']])
    ink = mask_of(own, [col['ink']])
    ys, xs = np.nonzero(letters)
    # the slope: the lowest ink of every column, fitted by a line
    cols = [x for x in range(xs.min(), xs.max() + 1) if ink[:, x].any()]
    feet = [np.nonzero(ink[:, x])[0].max() for x in cols]
    slope = SLOPE[name]
    body = mask_of(own, [col['body'], col['ink'], col['light']])
    allowed = erode(body)
    allowed[:, :xs.min() - 1] = False              # the menu paper covers the strap left of here
    centre = ((xs.min() + xs.max() + 1) / 2.0, (ys.min() + ys.max() + 1) / 2.0)
    return dict(centre=centre, slope=slope, allowed=allowed, stroke=stroke(ink),
                box=(xs.min(), ys.min(), xs.max() + 1, ys.max() + 1))


def blank(name):
    if name == 'guide':
        paper = gs.in_tweaks_colours(Image.open(gs.PAPER).convert('RGBA'))
    else:
        sub = {'tweaks': 'options', 'spawner': 'pause screen', 'credits': 'title'}[name]
        paper = Image.open(os.path.join(sl.ART, 'straps', sub, 'paper.png')).convert('RGBA')
    return sl.lengthened(paper)


# ------------------------------------------------------------------ glyphs
def glyphs(text, face):
    """[(coverage L, x, y)] of one line at the face's own size, and its advance.
    Nothing here is resized: a bitmap cut is cropped from its sheet, a vector
    face is rasterised at the size asked for."""
    kind, font, size, _ = face
    out, pen = [], 0
    if kind in ('bm', 'zh'):
        common, chars, sheets = typeset._font(DATA_FONT if kind == 'bm' else ZH_FONT, font)
        for c in text:
            z = chars.get(ord(c))
            if not z:
                return None, 0
            sx, sy, w, h, xo, yo, xa, page = z
            if c == ' ':
                pen += int(xa * typeset.SPACE)
                continue
            out.append((sheets[page].crop((sx, sy, sx + w, sy + h)).getchannel('A'), pen + xo, yo))
            pen += xa
    else:
        f = ImageFont.truetype(os.path.join(lang.FONTS, font), size)
        asc = f.getmetrics()[0]
        for c in text:
            if c == ' ':
                pen += int(round(f.getlength(' ') * typeset.SPACE))
                continue
            g = Image.new('L', (size * 2, size * 2), 0)
            ImageDraw.Draw(g).text((0, 0), c, font=f, fill=255)
            out.append((g, pen, 0))
            pen += int(round(f.getlength(c)))
        del asc
    return out, pen


def ink_rows(g):
    a = np.asarray(g) >= 128
    ys = np.nonzero(a.any(1))[0]
    return (ys.min(), ys.max() + 1) if len(ys) else None


def grow(a, how):
    """a: L array. 'down' = lighter with itself one row down (TITLE_BOLD);
    'grown' = credits_page.grown (right and down, gaps kept - Korean only)."""
    if how == 'down':
        b = np.zeros((a.shape[0] + 1, a.shape[1]), np.uint8)
        b[:-1] = a
        b[1:] = np.maximum(b[1:], a)
        return b
    if how == 'grown':
        return np.asarray(cp.grown(Image.fromarray(a)))
    return a


def labels(m):
    """connected pieces of a bool map (8-neighbour): label map and count"""
    h, w = m.shape
    lab = np.zeros(m.shape, np.int32)
    n = 0
    for y, x in zip(*np.nonzero(m)):
        if lab[y, x]:
            continue
        n += 1
        st = [(y, x)]
        lab[y, x] = n
        while st:
            cy, cx = st.pop()
            for dy in (-1, 0, 1):
                for dx in (-1, 0, 1):
                    ny, nx = cy + dy, cx + dx
                    if 0 <= ny < h and 0 <= nx < w and m[ny, nx] and not lab[ny, nx]:
                        lab[ny, nx] = n
                        st.append((ny, nx))
    return lab, n


def pieces(m):
    return labels(m)[1]


def welds(sil, parts):
    """What a finished silhouette joins that the glyphs keep apart.

    parts: [(y, x, bool glyph)] - every glyph's own thresholded bitmap where it
    was put. Answers (touching pairs {(i, j)}, pieces welded inside glyphs,
    glyph points missing from the silhouette). A pair counts when the two
    glyphs share a piece of the silhouette; a glyph's own pieces (an accent, a
    breve, a jamo) count as welded when they end up in fewer pieces than it
    has alone. The piece count of the whole word could hide both: jitter can
    part one pair while the growth joins another."""
    lab, _ = labels(sil)
    owner_of, pairs, inner, lost = [], set(), 0, 0
    for y, x, g in parts:
        h, w = g.shape
        box = lab[y:y + h, x:x + w]
        hit = box[g]
        lost += int((hit == 0).sum())
        own = set(int(v) for v in hit if v)
        owner_of.append(own)
        inner += max(0, pieces(g) - len(own))
    for i in range(len(parts)):
        for j in range(i + 1, len(parts)):
            if owner_of[i] & owner_of[j]:
                pairs.add((i, j))
    return pairs, inner, lost


def lettering(word, face, slope, jitter=True):
    """The silhouette (bool) of the word on the owner's slope, and the count of
    pieces its glyphs have when each stands alone (to catch letters that touch).
    Every glyph moves by whole pixels only: its own line's offset, the slope's
    rise at its pen position, and the typeset crisp jitter (a pixel up or down)."""
    lines = []
    for t in word.split('/'):
        gl, pen = glyphs(t, face)
        if gl is None:
            return None
        lines.append((gl, pen))
    # line tops: every line's ink top; the pitch keeps two clear rows between
    # the lines (the owner's: 10..11 of letter on a 12..13 pitch)
    heights = []
    for gl, pen in lines:
        tops = [y + ink_rows(g)[0] for g, x, y in gl if ink_rows(g)]
        bots = [y + ink_rows(g)[1] for g, x, y in gl if ink_rows(g)]
        heights.append((min(tops), max(bots)))
    grow_rows = 1 if face[3] in ('down', 'grown') else 0
    rnd = random.Random(hashlib.md5(word.encode('utf-8')).hexdigest())
    W = max(p for _, p in lines) + 80
    H = sum(b - t for t, b in heights) + 60 + int(abs(slope) * W)
    canvas = np.zeros((H, W), np.uint8)
    parts = []
    y_line = 30 + int(abs(slope) * W)
    x0 = 20
    for k, ((gl, pen), (t, b)) in enumerate(zip(lines, heights)):
        # the owner sets the second line under the first, a little to the right
        indent = 0 if k == 0 else 1
        for g, x, y in gl:
            if not ink_rows(g):
                continue
            here = x0 + indent + x
            dy = round(rnd.choice(typeset.SHIFT_UP) * lang.JITTER) if jitter else 0
            rise = int(round(slope * (here - x0)))
            gy = y_line + (y - t) + dy - rise
            ga = np.asarray(g)
            sub = canvas[gy:gy + ga.shape[0], here:here + ga.shape[1]]
            np.maximum(sub, ga, out=sub)
            gm = ga >= 128
            py_, px_ = np.nonzero(gm)
            parts.append((gy + py_.min(), here + px_.min(),
                          gm[py_.min():py_.max() + 1, px_.min():px_.max() + 1]))
        y_line += (b - t) + grow_rows + 2
    a = grow(canvas, face[3]) >= 128
    ys, xs = np.nonzero(a)
    y0, x0 = ys.min(), xs.min()
    a = a[y0:ys.max() + 1, x0:xs.max() + 1]
    parts = [(y - y0, x - x0, g) for y, x, g in parts]
    return a, parts


# ------------------------------------------------------------------ fitting
def place(sil, geo, size, dx, dy):
    h, w = sil.shape
    x = int(round(geo['centre'][0] - w / 2.0)) + dx
    y = int(round(geo['centre'][1] - h / 2.0)) + dy
    canvas = np.zeros((size[1], size[0]), bool)
    if x < 0 or y < 0 or x + w > size[0] or y + h > size[1]:
        return None
    canvas[y:y + h, x:x + w] = sil
    return canvas


def attempt(name, word, face, geo, base, jitter=True):
    got = lettering(word, face, geo['slope'], jitter)
    if got is None:
        return None
    sil, parts = got
    best = None
    for dx in range(-6, 7):
        for dy in range(-5, 6):
            ink = place(sil, geo, base.size, dx, dy)
            if ink is None:
                continue
            lit = ink | smoothing.notches(ink)
            out = int((lit & ~geo['allowed']).sum())
            key = (out, abs(dx) + abs(dy))
            if best is None or key < best[0]:
                best = (key, ink, (dx, dy))
    if best is None:
        return None
    (outside, _), ink, nudge = best
    # The baseline is the cut's own setting: flat, no jitter, NOT grown. Pairs
    # of letters the cut itself sets touching (TI in teammeatfont10) are the
    # font's and allowed; every other pair that touches here was joined by the
    # slope, the jitter or the growth, and is a weld. So is an accent, a breve
    # or a jamo that ends up in one piece with its letter, and so is any point
    # of a glyph the growth's gap pass punched out. Korean is held to this as
    # well: at strap sizes the growth welded whole syllables into one blot.
    flat, fparts = lettering(word, face[:3] + (None,), 0.0, jitter=False)
    own_pairs, _, _ = welds(flat, fparts)
    pairs, inner, lost = welds(sil, parts)
    merged = len(pairs - own_pairs) + inner + lost
    return dict(ink=ink, outside=outside, merged=merged, nudge=nudge, face=face, word=word, jitter=jitter,
                stroke=stroke(ink), size=sil.shape[::-1], weld=(len(pairs - own_pairs), inner, lost))


def best(set_name, name, geo, base):
    words = WORDS[set_name][name]
    faces = FACES.get(set_name, FACES['latin'])
    fallback = None
    for word in words:
        for cut in faces:
            good = []
            for face in cut:
                # the hand's wobble (typeset crisp jitter, a pixel up or down)
                # first; if it makes two letters touch, the same without it
                for jitter in (True, False):
                    r = attempt(name, word, face, geo, base, jitter)
                    if r is None:
                        continue
                    if r['outside'] == 0 and r['merged'] <= 0:
                        good.append(r)
                        break
                    if fallback is None or (r['outside'], r['merged']) < (fallback['outside'], fallback['merged']):
                        fallback = r
            if good:
                return min(good, key=lambda r: abs(r['stroke'] - geo['stroke']))
    fallback['nofit'] = True
    return fallback


def sealed(base, col):
    """The owner's paper carries ONE point of its body tone at alpha 227
    (his letters covered it). Where ours leave it bare it reads as a dark dot
    on the menu, so a body-tone point inside the body is made solid."""
    a = np.asarray(base).copy()
    hit = ((a[..., 0] == col['body'][0]) & (a[..., 1] == col['body'][1]) & (a[..., 2] == col['body'][2])
           & (a[..., 3] > 0) & (a[..., 3] < 255))
    a[..., 3][hit] = 255
    return Image.fromarray(a), int(hit.sum())


def finish(name, r):
    col = FILES[name][1]
    base, r['sealed'] = sealed(blank(name), col)
    m = Image.fromarray((r['ink'] * 255).astype(np.uint8))
    letters, share = smoothing.smooth(m, tone=col['light'] + (255,), ink=col['ink'])
    base.alpha_composite(letters)
    r['share'] = share
    return base


def main():
    try:
        sys.stdout.reconfigure(encoding='utf-8')
    except Exception:
        pass
    geos = {n: geometry(n) for n in ORDER}
    for n in ORDER:
        g = geos[n]
        print('owner %-8s slope %.3f (%.1f deg)  stroke %.2f  lettering %s'
              % (n, g['slope'], np.degrees(np.arctan(g['slope'])), g['stroke'], g['box']))
    # the blank strap must be the owner's file without his letters
    for n in ORDER:
        own, b = np.asarray(owner(n)), np.asarray(blank(n))
        col = FILES[n][1]
        letters = mask_of(owner(n), [col['ink'], col['light']])
        diff = (own != b).any(axis=2) & ~letters
        print('blank %-8s size %s  differs outside his letters: %d px' % (n, b.shape[1::-1], int(diff.sum())))
    results = {}
    for set_name, suffix in SETS:
        for n in ORDER:
            if WORDS[set_name][n] is None:
                im = owner(n)
                results[(suffix, n)] = (im, dict(owner=True))
                print('%-2s %-8s the owner\'s drawing (same word)' % (suffix, n))
            else:
                r = best(set_name, n, geos[n], blank(n))
                im = finish(n, r)
                results[(suffix, n)] = (im, r)
                f = r['face']
                print('%-2s %-8s %-18s %s%s%s%s  %dx%d  stroke %.2f (his %.2f)  tone %.1f%%  outside %d  welds %s%s'
                      % (suffix, n, r['word'], f[1], ' %d' % f[2] if f[2] else '', ' +' + f[3] if f[3] else '',
                         '' if r['jitter'] else ' nojitter',
                         r['size'][0], r['size'][1], r['stroke'], geos[n]['stroke'], r['share'],
                         r['outside'], r['weld'], '  DOES NOT FIT' if r.get('nofit') else ''))
            if im.size != owner(n).size:
                raise SystemExit('size differs: %s %s' % (suffix, n))
            im.save(os.path.join(OUT, '%s_%s.png' % (suffix, n)))
    sheet(results)


def sheet(results):
    lab = ImageFont.truetype('arial.ttf', 15)
    Z = 3
    cw, ch = 80 * Z + 12, 56 * Z + 22
    head, left = 26, 150
    rows = [('en', 'EN  owner (as is)')] + [(s, '%s  %s' % (s.upper(), n)) for n, s in SETS]
    out = Image.new('RGBA', (left + len(ORDER) * cw + 10, head + len(rows) * ch + 10), (48, 42, 40, 255))
    d = ImageDraw.Draw(out)
    for j, n in enumerate(ORDER):
        d.text((left + j * cw + 4, 6), n.upper(), fill=(235, 225, 215), font=lab)
    for i, (suffix, title) in enumerate(rows):
        y = head + i * ch
        d.text((8, y + ch // 2 - 12), title, fill=(235, 225, 215), font=lab)
        for j, n in enumerate(ORDER):
            if suffix == 'en':
                im, info = owner(n), dict(owner=True)
            else:
                im, info = results[(suffix, n)]
            out.alpha_composite(im.resize((im.width * Z, im.height * Z), Image.NEAREST), (left + j * cw, y))
            note = ''
            if suffix != 'en' and info.get('owner'):
                note = 'owner (same word)'
            elif info.get('nofit'):
                note = 'NO FIT'
            elif not info.get('owner'):
                f = info['face']
                note = '%s%s%s' % (f[1].replace('.fnt', '').replace('-Regular.ttf', '').replace('teammeatfont', 'tm'),
                                   ' %d' % f[2] if f[2] else '', ' +' + f[3] if f[3] else '')
            if note:
                d.text((left + j * cw + 4, y + 55 * Z + 2), note, fill=(200, 170, 120), font=lab)
    out.save(os.path.join(OUT, 'preview.png'))


if __name__ == '__main__':
    main()
