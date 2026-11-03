# -*- coding: utf-8 -*-
"""The MODS page with the player's own mods, and one mod's page - a picture to
agree on before the runtime drawing is written (the owner, 25.09.2026).

The built-in rows are the page's own baked words. A user mod's name is not
known in advance, so the game will print it at runtime in its menu font,
teammeatfont16bold, in the menus' ink; that is what is drawn here for them.
The paper is laid as the game will lay it (laid_paper): a piece a row, and a
band of columns repeated for width.

    py -3 tools/usermods_preview.py        # -> other stuff/usermods/preview-<set>.png
"""
import os
import struct

import numpy as np
from PIL import Image

import tweaks_page as tp
import lang
import credits_page
import smoothing

OUT = os.path.join(tp.ROOT, 'other stuff', 'usermods')
FONT = os.path.join(tp.ROOT, 'work', 'app', 'repentance-resources', 'data', 'font')
INK = (54, 47, 45)


class BMFont(object):
    def __init__(self, path):
        d = open(path, 'rb').read()
        self.chars, pages, at = {}, [], 4
        while at + 5 <= len(d):
            kind, size = d[at], struct.unpack_from('<i', d, at + 1)[0]
            body = at + 5
            if kind == 2:
                self.line, self.base = struct.unpack_from('<HH', d, body)
            elif kind == 3:
                pages = [p.decode() for p in d[body:body + size].split(b'\0') if p]
            elif kind == 4:
                for i in range(body, body + size, 20):
                    c = struct.unpack_from('<IHHHHhhhBB', d, i)
                    self.chars[c[0]] = c[1:9]
            at = body + size
        self.pages = [Image.open(os.path.join(os.path.dirname(path), p)).convert('RGBA') for p in pages]

    def draw(self, text):
        w = sum(self.chars.get(ord(ch), (0,) * 6 + (self.line // 3,))[6] for ch in text) + 2
        im = Image.new('RGBA', (max(w, 1), self.line + 2), (0, 0, 0, 0))
        pen = 0
        for ch in text:
            g = self.chars.get(ord(ch))
            if not g:
                pen += self.line // 3
                continue
            x, y, gw, gh, xo, yo, xa, page = g
            if gw and gh:
                glyph = self.pages[page].crop((x, y, x + gw, y + gh))
                ink = Image.new('RGBA', glyph.size, INK + (255,))
                ink.putalpha(glyph.getchannel('A'))
                im.alpha_composite(ink, (pen + xo, yo))
            pen += xa
        return im.crop(im.getchannel('A').getbbox() or (0, 0, 1, 1))


def sheet_target(set_name):
    sheet = Image.open(os.path.join(tp.PC, tp.PC_SET[set_name], tp.MENU, 'modsmenu.png')).convert('RGBA')
    target = lang.native_share(Image.open(os.path.join(tp.RES, set_name, tp.MENU, 'optionsextra.png'))
                               .convert('RGBA'))
    return sheet, target


def paper_width(set_name, target, font, names, user):
    """the list's paper: wide enough for its widest line, the runtime names too"""
    on, off = tp.onoff_of(set_name)
    onoff_w = max(on[3], off[3])
    baked = [tp.word(set_name, n, target, True) for n in names]
    widths = [im.width for im in baked] + [font.draw(u).width for u in user]
    widest = max(tp.CURSOR_W + w + tp.GAP + onoff_w + tp.GAP + tp.ARROW_W for w in widths)
    # the line stands a cursor's half right of the middle: that half again on
    # the right keeps the arrow off the torn edge
    return baked, max(0, widest + tp.CURSOR_W + tp.MODS_MARGIN * 2 - 256)


# ---------------------------------------------------------------- the paper
# The game will lay the paper itself, a row at a time, so it can grow with the
# mods: the head, a piece a row, the foot - each cut into its left part, one
# BAND repeated as often as the width needs, and its right part. The band is
# chosen once per sheet: its two ends meet with the torn edges at one height,
# and nothing drawn on the paper (the foot's scratch) lies inside it - the
# first mock-up repeated that scratch five times (the owner, 25.09.2026).
BAND_W = 24


def clean_strip(sheet):
    """head (without the PC's title), the five pieces and the foot, as one 256x256"""
    head = tp.mods_title(sheet.crop((0, 0, 256, 48)))[2]
    strip = Image.new('RGBA', (256, 256), (0, 0, 0, 0))
    strip.alpha_composite(head, (0, 0))
    strip.alpha_composite(sheet.crop((0, 48, 256, 256)), (0, 48))
    return strip


def find_band(strip, width=BAND_W):
    a = np.asarray(strip).astype(int)
    alpha = a[..., 3] > 0
    h, w = alpha.shape
    tops = [int(np.argmax(alpha[:, x])) for x in range(w)]
    bots = [h - 1 - int(np.argmax(alpha[::-1, x])) for x in range(w)]
    body = a[60:200, 60:200]
    tone = np.median(body[body[..., 3] == 255][:, :3], axis=0)
    marked = []
    for x in range(w):
        inner = a[tops[x] + 16:bots[x] - 16, x]
        marked.append(int(((np.abs(inner[:, :3] - tone).sum(axis=1) > 12) & (inner[:, 3] > 0)).sum()))
    best = None
    for x in range(40, 216 - width):
        y = x + width
        if any(marked[x:y + 1]):
            continue
        cost = (abs(tops[x] - tops[y]) + abs(bots[x] - bots[y])) * 40
        cost += int(np.abs(a[:, x, :3] - a[:, y, :3]).sum() // h)
        if best is None or cost < best[0]:
            best = (cost, x)
    return best[1]


def laid_paper(sheet, rows, bands):
    """the paper as the game will lay it: head, `rows` pieces, foot, `bands` bands wider"""
    strip = clean_strip(sheet)
    x = find_band(strip)
    parts = [(0, 48)] + [(48 + 32 * (i % 5), 32) for i in range(rows)] + [(208, 48)]
    out = Image.new('RGBA', (256 + bands * BAND_W, sum(h for _, h in parts)), (0, 0, 0, 0))
    y = 0
    for top, h in parts:
        out.alpha_composite(strip.crop((0, top, x, top + h)), (0, y))
        for k in range(bands):
            out.alpha_composite(strip.crop((x, top, x + BAND_W, top + h)), (x + k * BAND_W, y))
        out.alpha_composite(strip.crop((x, top, 256, top + h)), (x + bands * BAND_W, y))
        y += h
    return out


def stroke(width, which=1):
    credits_page.STROKE_THIN = 0
    rule, _ = smoothing.smooth(credits_page.rule_of(width, which), tone=credits_page.LIGHT + (255,),
                               ink=credits_page.INK, silhouette=credits_page.SILHOUETTE)
    return rule.crop(rule.getchannel('A').getbbox())


def framed(word, pad_x=12, pad_y=5):
    """a word in a frame drawn with the game's strokes: a button among the rows"""
    w, h = word.width + 2 * pad_x, word.height + 2 * pad_y
    top, bottom = stroke(w + 4, 0), stroke(w + 6, 2)
    left = stroke(h + 4, 1).transpose(Image.ROTATE_90)
    right = stroke(h + 6, 3).transpose(Image.ROTATE_270)
    out = Image.new('RGBA', (w + 8, h + 8), (0, 0, 0, 0))
    out.alpha_composite(word, (4 + pad_x, 4 + pad_y))
    out.alpha_composite(top, (2, 2))
    out.alpha_composite(bottom, (1, h + 6 - bottom.height))
    out.alpha_composite(left, (2, 1))
    out.alpha_composite(right, (w + 6 - right.width, 2))
    return out


ADD_WORD = {'data': '+ ADD MOD', 'dlc3.ru': '+ ДОБАВИТЬ МОД'}
SECTION_GAP = 14
OWN_WORD = {'data': 'YOUR MODS', 'dlc3.ru': 'СВОИ МОДЫ'}


def title_of(set_name):
    return tp.title_art(set_name, tp.MODS_WORD[set_name], gap=tp.TITLE_GAP_MODS)


def page_for(set_name, names, user, add_first):
    sheet, target = sheet_target(set_name)
    font = BMFont(os.path.join(FONT, 'teammeatfont16bold.fnt'))
    baked, extra = paper_width(set_name, target, font, names, user)
    bands = -(-extra // BAND_W)
    on = tp.word(set_name, 'ON', target, False)
    on_w, off_w = tp.onoff_of(set_name)[0][3], tp.onoff_of(set_name)[1][3]
    value_w = max(on_w, off_w) + tp.GAP + tp.ARROW_W
    arrow = sheet.crop((272, 0, 288, 16))
    step, baseline, row0 = tp.MODS_STEP, tp.MODS_STEP // 2 + 8, tp.MODS_ROW0
    add = tp.word(set_name, ADD_WORD[set_name], target, True)
    head_word = tp.word(set_name, OWN_WORD[set_name], target, True)
    rule = stroke(head_word.width + 10)
    heading = Image.new('RGBA', (rule.width, head_word.height + 3 + rule.height), (0, 0, 0, 0))
    heading.alpha_composite(head_word, ((rule.width - head_word.width) // 2, 0))
    heading.alpha_composite(rule, (0, head_word.height + 3))
    # the port's own, the heading, then the player's - ADD MOD first or last
    lines = [('baked', b, i in (0, 2, 3, 5, 6)) for i, b in enumerate(baked)]
    lines.append(('heading', heading, False))
    own = [('user', font.draw(u), True) for u in user]
    lines += ([('add', add, False)] + own) if add_first else (own + [('add', add, False)])
    # air above the heading: the port's own and the player's apart (the owner, 25.09.2026)
    tops, y = [], row0
    for kind, _, _ in lines:
        if kind == 'heading':
            y += SECTION_GAP
        tops.append(y)
        y += step
    page = laid_paper(sheet, -(-(y - row0) // step), bands)
    title = title_of(set_name)
    page.alpha_composite(title, ((page.width - title.width) // 2, tp.TITLE_Y))
    back = Image.new('RGBA', page.size, (170, 160, 158, 255))
    back.alpha_composite(page)
    for i, (kind, name, has_arrow) in enumerate(lines):
        top = tops[i]
        if kind == 'heading':
            back.alpha_composite(name, (page.width // 2 - name.width // 2, top + step // 2 - name.height // 2 - 2))
            continue
        if kind == 'add':               # a row as the others, with no value beside it
            nx = page.width // 2 - (tp.CURSOR_W + name.width) // 2 + tp.CURSOR_W
            back.alpha_composite(name, (nx, top + baseline - name.height))
            continue
        total = tp.CURSOR_W + name.width + tp.GAP + value_w
        nx = page.width // 2 - total // 2 + tp.CURSOR_W
        back.alpha_composite(name, (nx, top + baseline - name.height))
        vx = nx + name.width + tp.GAP
        back.alpha_composite(on, (vx, top + baseline - on.height))
        if has_arrow:
            back.alpha_composite(arrow, (vx + max(on_w, off_w) + tp.GAP, top + step // 2 - 8))
    return back, bands


PAGE_WORDS = {'data': ['ENABLED', 'MOVE UP', 'MOVE DOWN', 'SHARE', 'DELETE', 'RESTART TO APPLY'],
              'dlc3.ru': ['ВКЛЮЧЁН', 'ВЫШЕ', 'НИЖЕ', 'ПОДЕЛИТЬСЯ', 'УДАЛИТЬ',
                          'ПРИМЕНИТСЯ ПОСЛЕ ПЕРЕЗАПУСКА']}
SAMPLE = {'data': ('Better Isaac Sprites', 'v1.2', 'Someone',
                   'Replaces Isaac and his tears with a cleaner hand-drawn style. 214 files.'),
          'dlc3.ru': ('Better Isaac Sprites', 'v1.2', 'Someone',
                      'Заменяет Айзека и его слёзы на более чистую рисовку. 214 файлов.')}


def wrap(font, text, width):
    lines, line = [], ''
    for w in text.split():
        t = (line + ' ' + w).strip()
        if line and font.draw(t).width > width:
            lines.append(line)
            line = w
        else:
            line = t
    return lines + [line] if line else lines


def mod_page(set_name, bands):
    """one user mod's page on the list's own paper: its name for a title (the
    runtime font, the smaller one when the big does not fit), version and author,
    the description wrapped to the paper, then its rows"""
    sheet, target = sheet_target(set_name)
    big = BMFont(os.path.join(FONT, 'teammeatfont20bold.fnt'))
    mid = BMFont(os.path.join(FONT, 'teammeatfont16bold.fnt'))
    small = BMFont(os.path.join(FONT, 'teammeatfont10.fnt'))
    name, version, author, text = SAMPLE[set_name]
    width = 256 + bands * BAND_W - 2 * tp.MODS_MARGIN
    title = big.draw(name)
    if title.width > width:
        title = mid.draw(name)
    by = {'data': 'by', 'dlc3.ru': 'автор'}[set_name]
    info = ['%s  -  %s %s' % (version, by, author)] + wrap(small, text, width)
    text_h = 4 + len(info) * (small.line - 2) + 2          # the rows follow the text, not a whole row down
    words = [tp.word(set_name, w, target, True) for w in PAGE_WORDS[set_name]]
    rows = words[:5]
    note = tp.word(set_name, PAGE_WORDS[set_name][5], target, False)
    page = laid_paper(sheet, -(-(text_h + (len(rows) + 1) * tp.MODS_STEP) // tp.MODS_STEP), bands)
    page.alpha_composite(title, ((page.width - title.width) // 2, tp.TITLE_Y))
    back = Image.new('RGBA', page.size, (170, 160, 158, 255))
    back.alpha_composite(page)
    y = tp.MODS_ROW0 + 4
    for line in info:
        t = small.draw(line)
        back.alpha_composite(t, (page.width // 2 - t.width // 2, y))
        y += small.line - 2
    on = tp.word(set_name, 'ON', target, False)
    y = tp.MODS_ROW0 + text_h
    base = tp.MODS_STEP // 2 + 8
    for i, r in enumerate(rows):
        has_value = i == 0
        total = tp.CURSOR_W + r.width + (tp.GAP + on.width if has_value else 0)
        nx = page.width // 2 - total // 2 + tp.CURSOR_W
        back.alpha_composite(r, (nx, y + base - r.height))
        if has_value:
            back.alpha_composite(on, (nx + r.width + tp.GAP, y + base - on.height))
        y += tp.MODS_STEP
    back.alpha_composite(note, (page.width // 2 - note.width // 2, y + base - note.height))
    return back


def main():
    os.makedirs(OUT, exist_ok=True)
    for set_name, suffix, _title, names, _scale in tp.SETS:
        if set_name not in ADD_WORD:
            continue
        mods = names[len(tp.KINDS):]
        user = ['Better Isaac Sprites', 'Old Rebirth Music Restored']
        a, bands = page_for(set_name, mods, user, True)
        c = mod_page(set_name, bands)
        pics = [a, c]
        sheet = Image.new('RGBA', (sum(p.width for p in pics) + 10 * (len(pics) + 1),
                                   max(p.height for p in pics) + 20), (40, 34, 32, 255))
        x = 10
        for p in pics:
            sheet.alpha_composite(p, (x, 10))
            x += p.width + 10
        sheet = sheet.resize((sheet.width * 3, sheet.height * 3), Image.NEAREST)
        path = os.path.join(OUT, 'preview-%s.png' % suffix)
        sheet.save(path)
        print(path, sheet.size, 'band at', find_band(clean_strip(sheet_target(set_name)[0])))


if __name__ == '__main__':
    main()
