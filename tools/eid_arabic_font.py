# -*- coding: utf-8 -*-
"""The Arabic face of the descriptions: eid_default with the Arabic letters added.

The "Arabic EID" mod (other stuff/arabic-eid) brings Noto Sans Arabic UI (OFL)
baked at TWICE the size and draws it at half scale. Halved to the port's 100 %
it was unreadable (the owner, 27.10.2026), so the letters are now PixelAE
(other stuff/arabic-eid/PixelAE, CC0), a pixel face drawn at its own 10 px, the
third of the five shown to him (font-variants-x3.png):

  * every glyph of eid_default stays exactly as it is - same file, same sheets;
    only the word gap is PixelAE's 3 (this face writes Arabic only);
  * each Arabic code of the mod's sheet is PixelAE's letter, its baseline on
    eid_default's; an isolated form it lacks is its plain letter (the Unicode
    decomposition: U+FE8D is U+0627 written alone);
  * what PixelAE has not got at all (the Arabic semicolon, the en dash, the
    minus) stays the mod's Noto, laid on the 2x grid at an even origin, halved
    by averaging 2x2 and kept where half or more is ink;
  * it is packed like eid_default: the letter white, a one-point outline round
    it (the four neighbours) black, both opaque, the box one point larger on
    every side and the offsets one less.

AND A TWIN WITHOUT THE RIM, eid_ar_plain: eid_borderless's glyphs and the same
Arabic letters with no outline, in the same places. Arabic letters JOIN, and the
engine draws a line glyph by glyph: the rim of each letter lands on the stroke
of the one before and cuts every join (measured on the face drawn in the
engine's order). So eid.c writes an Arabic line twice - the rimmed face, then
the twin over it - and every rim lies under every letter. The twin is also what
is written on paper, where the rim is a smudge (as eid_borderless is).

The descriptor is BMFont binary like eid_default's, and names three sheets:
eid_default's own two and eid_arabic_02.png. Binary page names must all be the
same length, so the new one is seventeen characters like the other two.

    py -3 tools/eid_arabic_font.py
"""
import os
import struct

import numpy as np
from PIL import Image

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
MOD_FONT = os.path.join(ROOT, 'other stuff', 'arabic-eid', 'Arabic EID', 'resources', 'font')
ASSETS = os.path.join(ROOT, 'port', 'app', 'src', 'main', 'assets')
LEAF = 'eid_ar'
SHEET = 'eid_arabic_02.png'
PLAIN_LEAF = 'eid_ar_plain'
PLAIN_SHEET = 'eid_arabic_plain.png'           # as long as eid_borderless_0.png
PIXEL_FONT = os.path.join(ROOT, 'other stuff', 'arabic-eid', 'PixelAE', 'PixelAE-Regular.ttf')
PIXEL_SIZE = 10                                 # its own grid: 100 units of 1000
BASE = 9                                        # eid_default's baseline from the line's top
SPACE = 3                                       # PixelAE's word gap, as the owner saw it


def blocks(path):
    b = open(path, 'rb').read()
    assert b[:4] == b'BMF\x03', path
    out, i = {}, 4
    while i < len(b):
        t = b[i]
        n = struct.unpack_from('<I', b, i + 1)[0]
        out[t] = b[i + 5:i + 5 + n]
        i += 5 + n
    return out


def chars(block):
    return {c[0]: list(c[1:]) for c in (struct.unpack_from('<IHHHHhhhBB', block, k)
                                        for k in range(0, len(block), 20))}


def halved(fill2, xo2, yo2):
    """a 2x coverage box at offset (xo2, yo2) from an even origin -> 1x mask, offset"""
    x0, y0 = xo2 // 2, yo2 // 2
    h2, w2 = fill2.shape
    W = (xo2 + w2 + 1) // 2 - x0
    H = (yo2 + h2 + 1) // 2 - y0
    big = np.zeros((H * 2, W * 2))
    big[yo2 - y0 * 2:yo2 - y0 * 2 + h2, xo2 - x0 * 2:xo2 - x0 * 2 + w2] = fill2
    mask = big.reshape(H, 2, W, 2).mean(axis=(1, 3)) >= 0.5
    return mask, x0, y0


def pixel_glyphs(codes):
    """{code: (1x mask or None, x offset, y offset, advance)} drawn by PixelAE at its own 10 px.

    Its isolated forms are its plain letters (U+FE8D is U+0627 as written alone),
    so a form it lacks is taken through its Unicode decomposition. The offsets
    put its baseline on eid_default's (BASE from the line's top)."""
    import unicodedata
    from fontTools.ttLib import TTFont
    from PIL import ImageDraw, ImageFont
    cmap = TTFont(PIXEL_FONT).getBestCmap()
    face = ImageFont.truetype(PIXEL_FONT, PIXEL_SIZE, layout_engine=ImageFont.Layout.BASIC)
    out = {}
    for code in codes:
        c = code
        if c not in cmap:
            d = unicodedata.decomposition(chr(c)).split()
            if len(d) == 2 and d[0].startswith('<') and int(d[1], 16) in cmap:
                c = int(d[1], 16)
            else:
                continue
        big = PIXEL_SIZE * 3
        im = Image.new('L', (big, big))
        ImageDraw.Draw(im).text((PIXEL_SIZE, big // 2), chr(c), font=face, fill=255, anchor='ls')
        mask = np.asarray(im) >= 128
        adv = round(face.getlength(chr(c)))
        ys, xs = np.nonzero(mask)
        if not len(xs):
            out[code] = (None, 0, 0, adv)
            continue
        out[code] = (mask[ys.min():ys.max() + 1, xs.min():xs.max() + 1], int(xs.min()) - PIXEL_SIZE,
                     int(ys.min()) - big // 2 + BASE, adv)
    return out


def arabic_glyphs():
    """[(code, 1x mask or None, x offset, y offset, advance)]: every code of the mod's
    Arabic sheet, PixelAE's where it has the letter, the mod's halved otherwise"""
    mod_codes = [code for code, v in chars(blocks(os.path.join(MOD_FONT, 'arabic.fnt'))[4]).items()]
    pixel = pixel_glyphs(mod_codes)
    noto = noto_glyphs()
    glyphs, kept = [], []
    for code, mask, x0, y0, adv in noto:
        if code in pixel:
            glyphs.append((code,) + pixel[code])
        else:
            glyphs.append((code, mask, x0, y0, adv))
            kept.append(code)
    print('PixelAE %d, the mod\'s halved %d (%s...)' % (len(glyphs) - len(kept), len(kept),
                                                     ' '.join('%04X' % c for c in kept[:12])))
    return glyphs


def noto_glyphs():
    """[(code, 1x mask or None, x offset, y offset, advance)] of the mod's Arabic sheet"""
    mod = blocks(os.path.join(MOD_FONT, 'arabic.fnt'))
    mod_pages = [p.decode() for p in mod[3].split(b'\0') if p]
    arabic_page = mod_pages.index('arabic.png')
    sheet2 = np.asarray(Image.open(os.path.join(MOD_FONT, 'arabic.png')).convert('RGBA')).astype(float) / 255
    glyphs = []
    for code, (x, y, w, h, xo, yo, xa, page, chnl) in sorted(chars(mod[4]).items()):
        if page != arabic_page:
            continue
        p = sheet2[y:y + h, x:x + w]
        mask, x0, y0 = halved(p[..., 0] * p[..., 3], xo, yo)
        ys, xs = np.nonzero(mask)
        adv = xa // 2
        if not len(xs):
            glyphs.append((code, None, 0, 0, adv))
            continue
        mask = mask[ys.min():ys.max() + 1, xs.min():xs.max() + 1]
        glyphs.append((code, mask, x0 + xs.min(), y0 + ys.min(), adv))
    return glyphs


def write_face(base_leaf, leaf, sheet_name, rim, glyphs):
    """base_leaf's descriptor and sheets, and the Arabic glyphs on one sheet more"""
    own = blocks(os.path.join(ASSETS, base_leaf + '.fnt'))
    pages = [p.decode() for p in own[3].split(b'\0') if p]
    common = list(struct.unpack_from('<HHHHHBBBBB', own[2]))
    scale_w, scale_h, npages = common[2], common[3], common[4]
    mine = chars(own[4])
    mine[32][6] = SPACE                          # this face writes Arabic only: its own word gap
    assert all(len(sheet_name) == len(p) for p in pages), 'binary page names must be one length'
    out = np.zeros((scale_h, scale_w, 4), np.uint8)
    cx = cy = 1
    row_h = 0
    rows = []
    pad = 1 if rim else 0
    for code, mask, x0, y0, adv in glyphs:
        if code in mine:
            continue
        if mask is None:
            rows.append(struct.pack('<IHHHHhhhBB', code, 0, 0, 0, 0, 0, 0, adv, npages, 15))
            continue
        h, w = mask.shape
        W, H = w + 2 * pad, h + 2 * pad
        if cx + W + 1 > scale_w:
            cx, cy, row_h = 1, cy + row_h + 1, 0
        assert cy + H + 1 <= scale_h, 'the sheet is full'
        box = np.zeros((H, W), bool)
        box[pad:H - pad, pad:W - pad] = mask
        cell = out[cy:cy + H, cx:cx + W]
        if rim:
            edge = box.copy()
            edge[1:, :] |= box[:-1, :]; edge[:-1, :] |= box[1:, :]
            edge[:, 1:] |= box[:, :-1]; edge[:, :-1] |= box[:, 1:]
            cell[edge] = (0, 0, 0, 255)
        cell[box] = (255, 255, 255, 255)
        rows.append(struct.pack('<IHHHHhhhBB', code, cx, cy, W, H, x0 - pad, y0 - pad, adv, npages, 15))
        cx += W + 1
        row_h = max(row_h, H)
    every = sorted([struct.pack('<IHHHHhhhBB', c, *v) for c, v in mine.items()] + rows,
                   key=lambda r: struct.unpack_from('<I', r)[0])
    common[4] = npages + 1
    body = b'BMF\x03'
    pages_block = b''.join(p.encode() + b'\0' for p in pages + [sheet_name])
    for t, data in ((1, own[1]), (2, struct.pack('<HHHHHBBBBB', *common)), (3, pages_block),
                    (4, b''.join(every))):
        body += struct.pack('<BI', t, len(data)) + data
    if 5 in own:
        body += struct.pack('<BI', 5, len(own[5])) + own[5]
    open(os.path.join(ASSETS, leaf + '.fnt'), 'wb').write(body)
    Image.fromarray(out).save(os.path.join(ASSETS, sheet_name))
    print('%s.fnt: %d glyphs of %s + %d Arabic, sheet %s filled to row %d of %d'
          % (leaf, len(mine), base_leaf, len(rows), sheet_name, cy + row_h, scale_h))


def main():
    glyphs = arabic_glyphs()
    write_face('eid_default', LEAF, SHEET, True, glyphs)
    write_face('eid_borderless', PLAIN_LEAF, PLAIN_SHEET, False, glyphs)


if __name__ == '__main__':
    main()
