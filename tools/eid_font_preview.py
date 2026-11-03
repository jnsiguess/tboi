# -*- coding: utf-8 -*-
"""Draw real description text with a BMFont, so a face can be judged by eye.

Written to answer one question: the Chinese half of the item descriptions is
missing a third of its characters, because the font we carry is Japanese. The
mod ships Chinese faces of its own. Before any of them is put into the port,
this draws the SAME real lines out of eid.bin with the old face and with the
new one, at the size the panel actually uses and again magnified, so the
difference is a picture rather than a claim.

    py -3 tools/eid_font_preview.py [--lang zh_cn] [--zoom 4]
"""
import io
import os
import struct
import sys

from PIL import Image

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import eid_glyphs as G

MOD = os.path.join('Z:', os.sep, 'Games', 'Steam', 'steamapps', 'common',
                   'The Binding of Isaac Rebirth', 'mods',
                   'external item descriptions_836319872', 'resources', 'font')

PANEL = (34, 32, 39)            # the EID panel's own dark ground
INK = (255, 255, 255)
EDGE = (14, 13, 16)             # the outline baked into the alpha channel
GONE = (214, 72, 72)            # a character the face does not have


class Font(object):
    def __init__(self, path):
        data = io.open(path, 'rb').read()
        if data[:3] != b'BMF':
            raise ValueError('%s is not a BMFont binary' % path)
        self.line = self.base = 0
        self.chars = {}
        pages = []
        at = 4
        while at + 5 <= len(data):
            kind = data[at]
            size = struct.unpack_from('<i', data, at + 1)[0]
            body = at + 5
            if kind == 2:
                self.line, self.base = struct.unpack_from('<HH', data, body)
            elif kind == 3:
                pages = [p.decode() for p in data[body:body + size].split(b'\0') if p]
            elif kind == 4:
                for i in range(body, body + size, 20):
                    (cid, x, y, w, h, xo, yo, xa, page, _chnl) = struct.unpack_from(
                        '<IHHHHhhhBB', data, i)
                    self.chars[cid] = (x, y, w, h, xo, yo, xa, page)
            at = body + size
        here = os.path.dirname(path)
        self.pages = [Image.open(os.path.join(here, p)).convert('RGBA') for p in pages]

    def width(self, text):
        return sum(self.chars[ord(c)][6] for c in text if ord(c) in self.chars) or 1

    def draw(self, text, ink=INK, gone=GONE, edge=EDGE):
        """One line, on its own transparent strip.

        HOW THESE SHEETS ARE PACKED, and it is not what it looks like. The
        common block says alphaChnl=1 and r=g=b=0: the COLOUR channels hold the
        letter, and ALPHA holds the same letter grown by a pixel - the outline
        EID draws behind its white text. Taking alpha for the glyph, which is
        the obvious thing to do, paints the outline instead: every character
        comes out a fat blob, and the owner rightly called the first two
        previews blown out and chewed (16.09.2026).

        So the outline goes down first in its own dark colour, and the letter
        goes on top of it.

        Missing glyphs are shown as a red box rather than as nothing: seeing
        the hole is the whole point of the picture.
        """
        w = 0
        for c in text:
            g = self.chars.get(ord(c))
            w += g[6] if g else self.line // 2 + 1
        im = Image.new('RGBA', (max(w, 1) + 2, self.line + 4), (0, 0, 0, 0))
        pen = 0
        for c in text:
            g = self.chars.get(ord(c))
            if not g:
                box = Image.new('RGBA', (self.line // 2, self.line - 2), gone + (255,))
                im.paste(box, (pen + 1, 1), box)
                pen += self.line // 2 + 1
                continue
            x, y, gw, gh, xo, yo, xa, page = g
            if gw and gh:
                cut = self.pages[page].crop((x, y, x + gw, y + gh))
                shape = cut.getchannel('R')          # the letter itself
                halo = cut.getchannel('A')           # the letter grown by one
                for mask, colour in ((halo, edge), (shape, ink)):
                    paint = Image.new('RGBA', cut.size, colour + (255,))
                    paint.putalpha(mask)
                    im.paste(paint, (pen + xo, yo), paint)
            pen += xa
        return im


def sample_lines(lang, how_many=6):
    """Real names and the first line of their text, straight out of eid.bin.

    A line carrying the up/down arrows is taken first on purpose: they are the
    stat changes, they are in three hundred descriptions, and they are the one
    thing the older Chinese face does NOT have.
    """
    langs = G.read_bin(G.BIN)
    got = langs[lang]
    pairs = []
    for i in range(0, len(got) - 1, 2):
        name, text = got[i], got[i + 1]
        if not name or len(name) < 2:
            continue
        first = text.split('#')[0].split('\n')[0].strip()
        if len(first) < 8:
            continue
        pairs.append((name, first[:30]))
    arrows = [p for p in pairs if u'↑' in p[1] or u'↓' in p[1]]
    plain = [p for p in pairs if p not in arrows]
    chosen = arrows[:2] + plain[:max(0, how_many - 2)]
    out = []
    for name, first in chosen:
        out.append(name)
        out.append(first)
    return out


def sheet(faces, lines, zoom):
    """Faces SIDE BY SIDE, one column each.

    Stacked in one tall strip this came out narrow, and anything that views it
    scales a tall picture down to fit - which made the glyphs smaller than they
    are on the phone and the comparison worthless. Columns keep the picture
    wide, so it is shown at something near its real size.
    """
    gap, pad, between = 5, 8, 14
    label = faces[0][1]
    cols = []
    for title, font in faces:
        strips = [label.draw(title, ink=(255, 210, 120))]
        for ln in lines:
            strips.append(font.draw(ln))
        cols.append(strips)

    col_w = [max(s.width for s in c) for c in cols]
    rows = max(len(c) for c in cols)
    row_h = [max((c[r].height for c in cols if r < len(c))) for r in range(rows)]

    width = pad * 2 + sum(col_w) + between * (len(cols) - 1)
    height = pad * 2 + sum(row_h) + gap * (rows - 1)
    out = Image.new('RGBA', (width, height), PANEL + (255,))
    x = pad
    for i, c in enumerate(cols):
        y = pad
        for r in range(rows):
            if r < len(c):
                out.paste(c[r], (x, y), c[r])
            y += row_h[r] + gap
        x += col_w[i] + between
    if zoom != 1:
        out = out.resize((out.width * zoom, out.height * zoom), Image.NEAREST)
    return out


CJK = ((0x2e80, 0x9fff), (0xf900, 0xfaff), (0xff00, 0xff60), (0xffe0, 0xffe6))


def breakable(c):
    o = ord(c)
    return any(a <= o <= b for a, b in CJK)


def wrap(font, text, box, break_chars):
    """The port's fit_to_width, on the desk.

    Same rule as the C: a word ends at a space, and where breaking between
    characters is allowed a single CJK character is a word of its own. Run here
    so the change can be SEEN wrapped at the real box width before it is judged
    on the phone.
    """
    lines, cur, cur_len = [], '', 0.0
    i = 0
    while i < len(text):
        start = i
        if break_chars and breakable(text[i]):
            i += 1
        else:
            while i < len(text) and text[i] != ' ':
                if break_chars and i > start and breakable(text[i]):
                    break
                i += 1
        if i < len(text) and text[i] == ' ':
            i += 1
        word = text[start:i]
        w = float(font.width(word))
        if cur_len + w <= box or cur_len < 17.0:
            cur += word
            cur_len += w
        else:
            lines.append(cur)
            cur, cur_len = word, w
    lines.append(cur)
    return lines


def main():
    lang, zoom, only, rows = 'zh_cn', 4, None, 6
    for i, a in enumerate(sys.argv):
        if a == '--lang' and i + 1 < len(sys.argv):
            lang = sys.argv[i + 1]
        if a == '--zoom' and i + 1 < len(sys.argv):
            zoom = int(sys.argv[i + 1])
        if a == '--rows' and i + 1 < len(sys.argv):
            rows = int(sys.argv[i + 1])
        if a == '--only' and i + 1 < len(sys.argv):
            only = sys.argv[i + 1].split(',')

    ours = Font(os.path.join(G.ASSETS, 'eid_default.fnt'))
    faces = [('eid_default - what the port carries now', ours)]
    for n in ('eid_cn_alt', 'eid_cn_default', 'eid_cn_old'):
        p = os.path.join(MOD, n + '.fnt')
        if os.path.isfile(p) and (only is None or n in only):
            faces.append((n, Font(p)))
    if only is not None and 'eid_default' not in only:
        faces = [f for f in faces if f[1] is not ours] or faces

    lines = sample_lines(lang, rows)
    print('%s: %d lines, drawn with %d faces' % (lang, len(lines), len(faces)))
    im = sheet(faces, lines, zoom)
    where = os.path.join(os.environ.get('TEMP', '.'), 'eid-%s-fonts.png' % lang)
    im.save(where)
    print('written to', where)


if __name__ == '__main__':
    main()
