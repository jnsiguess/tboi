# -*- coding: utf-8 -*-
"""Pictures of the ways a too-long EID panel could be shown (07.10.2026).

The panel is the port's own: the pieces come out of the EID length desk
(desk.c `case`, the real eid.c layout - every word, every picture and its
place), the letters out of eid_default.fnt and its sheets, the pictures out of
the port's .anm2 sheets and the game's item files, drawn the way the engine
draws them. Only the arrangement differs between the options:

    A  as now                   - the panel runs off the bottom
    B  a second column          - the blocks that do not fit continue at x 235
    C  pages                    - as the Bag of Crafting's recipes, "...+N more"
    D  TextboxWidth 180         - the desk built with DESK_BOX=180
    E  raised to the hearts row - the top moves up, no higher than y 28

The room behind is the owner's screenshot (reports/2026-10-07/eid-long.jpg)
with the panel on it painted out (OpenCV inpainting), 585 x 270 units shown
4x. Below the screen's edge, a dimmed strip shows what does not fit.

    py -3 tools/eidlendesk/mockups.py      # -> other stuff/eid-overflow/*.png
"""
import io
import os
import struct
import subprocess
import sys

import cv2
import numpy as np
from PIL import Image, ImageDraw, ImageFont

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, '..', '..'))
sys.path.insert(0, os.path.join(ROOT, 'tools'))
import anm2render                                        # noqa: E402
import run as desk                                       # noqa: E402

ASSETS = os.path.join(ROOT, 'port', 'app', 'src', 'main', 'assets')
SHOT = os.path.join(ROOT, 'reports', '2026-10-07', 'eid-long.jpg')
OUT = os.path.join(ROOT, 'other stuff', 'eid-overflow')
Z = 4                                                    # screen pixels a unit, for viewing
CW, CH = 585, 270                                        # the owner's canvas
PANEL_X = 92.0                                           # shim_eid_panel_place, MOVE X 0
COL2_X = 235.0                                           # option B's second column
HEARTS = 28.0                                            # option E: the hearts row ends here
EDGE = (14, 13, 16)                                      # the outline in the sheet's alpha
SHEETS = ['eid_inline_icons', 'eid_transform_icons', 'eid_cardspills', 'eid_player_icons']
CAPTION_FONT = r'C:\Windows\Fonts\segoeui.ttf'


# ------------------------------------------------------------------ the font
class Font(object):
    def __init__(self, name):
        path = os.path.join(ASSETS, name)
        data = io.open(path, 'rb').read()
        self.chars, pages, at = {}, [], 4
        while at + 5 <= len(data):
            kind, size = data[at], struct.unpack_from('<i', data, at + 1)[0]
            body = at + 5
            if kind == 3:
                pages = [p.decode() for p in data[body:body + size].split(b'\0') if p]
            elif kind == 4:
                for i in range(body, body + size, 20):
                    cid, x, y, w, h, xo, yo, xa, page, _c = struct.unpack_from('<IHHHHhhhBB', data, i)
                    self.chars[cid] = (x, y, w, h, xo, yo, xa, page)
            at = body + size
        self.pages = [Image.open(os.path.join(ASSETS, p)).convert('RGBA') for p in pages]

    def width(self, text):
        return sum(self.chars[ord(c)][6] for c in text if ord(c) in self.chars)

    def draw(self, canvas, text, x, y, ink, alpha=1.0):
        """at the line's corner (x, y) in units, as the engine's draw string does"""
        pen = 0
        for c in text:
            g = self.chars.get(ord(c))
            if not g:
                continue
            gx, gy, gw, gh, xo, yo, xa, page = g
            if gw and gh:
                cut = self.pages[page].crop((gx, gy, gx + gw, gy + gh))
                for mask, colour in ((cut.getchannel('A'), EDGE), (cut.getchannel('R'), ink)):
                    paint = Image.new('RGBA', cut.size, colour + (255,))
                    paint.putalpha(mask.point(lambda v: int(v * alpha)))
                    paint = paint.resize((gw * Z, gh * Z), Image.NEAREST)
                    canvas.alpha_composite(paint, (int(round((x + pen + xo) * Z)), int(round((y + yo) * Z))))
            pen += xa


FONT = None


# ------------------------------------------------------------------ the pictures
def sheet_anm2(index, item_png=None):
    """one of the port's own .anm2 sheets, or the item picture's (layer 0 blank, layer 1 the item)"""
    a = anm2render.Anm2.__new__(anm2render.Anm2)
    import xml.etree.ElementTree as ET
    a.path = os.path.join(ASSETS, SHEETS[index] + '.anm2')
    t = ET.parse(a.path).getroot()
    a.sheets = {s.get('Id'): os.path.join(ASSETS, s.get('Path')) for s in t.iter('Spritesheet')}
    if item_png:
        a.sheets = {'0': None, '1': item_png}
    a.layers = {l.get('Id'): l.get('SpritesheetId') for l in t.iter('Layer')}
    anims = t.find('Animations')
    a.default = anims.get('DefaultAnimation')
    a.anims = {x.get('Name'): x for x in anims.findall('Animation')}
    return a


_anm = {}


def draw_icon(canvas, p, x, y, alpha=1.0):
    key = (p['sheet'], p['gfx'])
    if key not in _anm:
        item = anm2render.find('gfx/items/%s/%s' % ('trinkets' if p['key'] < 0 else 'collectibles', p['gfx'])) if p['gfx'] else None
        _anm[key] = sheet_anm2(p['sheet'], item)
    layer = Image.new('RGBA', canvas.size, (0, 0, 0, 0))
    _anm[key].draw(layer, p['anim'], p['frame'], 0, 0, Z * p['scale'], (Z * x, Z * y))
    if alpha < 1.0:
        layer.putalpha(layer.getchannel('A').point(lambda v: int(v * alpha)))
    canvas.alpha_composite(layer)


# ------------------------------------------------------------------ the panel, from the desk
def panel(exe, lang, kind, ident, character, own, page=None):
    """the whole panel as it was before pages (page None), or page N as the port now shows it"""
    args = [exe, 'case', lang, str(kind), str(ident), str(character)] + (['own=' + own] if own else [])
    args += ['whole'] if page is None else ['page=%d' % page]
    out = subprocess.run(args, capture_output=True).stdout.decode('utf-8', 'replace')
    top = height = 0.0
    pieces = []
    count = 1
    for line in out.splitlines():
        if line.startswith('pages '):
            count = max(1, int(line.split()[1].rstrip(',')))
        f = line.split('\t')
        if f[0] == 'T':
            top, height = float(f[1]), float(f[2])
        elif f[0] == 'P':
            pieces.append({'icon': f[1] == '1', 'x': float(f[2]), 'y': float(f[3]), 'scale': float(f[4]),
                           'rgba': tuple(float(v) for v in f[5:9]), 'sheet': int(f[9]), 'anim': f[10],
                           'frame': int(f[11]), 'key': int(f[12]), 'gfx': f[13], 'text': f[14]})
    return {'top': top, 'height': height, 'pieces': pieces, 'count': count}


def lines_and_blocks(pieces):
    """the description's blocks: [(first line y, last line y, big, [pieces])], the header apart"""
    ys = sorted(set(p['y'] for p in pieces if not p['icon']))
    for p in pieces:
        p['line'] = min(ys, key=lambda v: abs(p['y'] - v)) if not p['icon'] else \
            min(ys, key=lambda v: abs(p['y'] + 4.0 - v))
    form = any(p['icon'] and p['anim'].startswith('Transformation') for p in pieces)      # a transformation line under the name
    desc = 36.0 if form else 20.0                                  # where the description begins (M_SECTION)
    head = [p for p in pieces if p['line'] < desc - 1.0]
    body = [p for p in pieces if p['line'] >= desc - 1.0]
    starts = sorted(set([desc] + [p['line'] for p in body if p['x'] < 10.0]))
    blocks = []
    for i, s in enumerate(starts):
        end = starts[i + 1] if i + 1 < len(starts) else 1e9
        mine = [p for p in body if s <= p['line'] < end]
        last = max(p['line'] for p in mine)
        big = any(p['icon'] and p['gfx'] for p in mine) or any(p['icon'] and p['sheet'] == 3 for p in mine)
        if mine:
            blocks.append({'first': s, 'last': last, 'big': big, 'pieces': mine})
    return head, blocks, desc


def block_bottom(b):
    return b['last'] + (16.0 if b['big'] else 12.0)


def block_top(b):
    return b['first'] - (4.0 if b['big'] else 0.0)


# ------------------------------------------------------------------ the picture
def background(extra):
    bg = cv2.imread(os.path.join(OUT, '_room.png'))
    bg = cv2.cvtColor(bg, cv2.COLOR_BGR2RGB)
    im = Image.fromarray(bg).resize((CW * Z, CH * Z), Image.LANCZOS).convert('RGBA')
    if extra <= 0:
        return im
    full = Image.new('RGBA', (CW * Z, (CH + extra) * Z), (24, 22, 26, 255))
    full.paste(im, (0, 0))
    d = ImageDraw.Draw(full)
    for k in range(-CH * Z, CW * Z, 28):                     # hatching: off the screen
        d.line([(k, CH * Z + extra * Z), (k + extra * Z, CH * Z)], fill=(40, 36, 44, 255), width=6)
    d.rectangle([0, CH * Z - 3, CW * Z, CH * Z + 3], fill=(230, 40, 40, 255))
    return full


def place(canvas, pieces, ox, oy, alpha_below=None):
    for p in pieces:
        x, y = ox + p['x'], oy + p['y']
        a = 1.0
        if alpha_below is not None and y + 6.0 > CH:
            a = 0.45
        if p['icon']:
            draw_icon(canvas, p, x, y, a)
        else:
            r, g, b, al = p['rgba']
            FONT.draw(canvas, p['text'], x, y, (int(r * 255), int(g * 255), int(b * 255)), al * a)


def grey_line(canvas, text, x, y):
    FONT.draw(canvas, text, x, y, (150, 150, 150))


def caption(im, title, lines):
    f1 = ImageFont.truetype(CAPTION_FONT, 44)
    f2 = ImageFont.truetype(CAPTION_FONT, 34)
    h = 80 + 46 * len(lines)
    out = Image.new('RGBA', (im.width, im.height + h), (16, 16, 18, 255))
    out.paste(im, (0, 0))
    d = ImageDraw.Draw(out)
    d.text((24, im.height + 12), title, font=f1, fill=(240, 240, 240))
    for i, t in enumerate(lines):
        d.text((24, im.height + 70 + 46 * i), t, font=f2, fill=(200, 200, 200))
    return out


def save(im, name):
    path = os.path.join(OUT, name)
    im.convert('RGB').save(path)
    print(path)
    return path


# ------------------------------------------------------------------ the options
def option_a(c, pan, tag, title, note):
    bottom = pan['top'] + pan['height'] - 1
    extra = max(0, int(bottom - CH) + 6)
    im = background(extra)
    place(im, pan['pieces'], PANEL_X, pan['top'], alpha_below=True)
    over = bottom - CH
    return save(caption(im, 'A. Как сейчас — ' + title,
                        ['Низ панели на %d, экран кончается на 270: за краем %d ед. (%s).' % (bottom, over, note),
                         'Под красной чертой — то, чего на экране не видно.']), '%s-A-now.png' % tag)


def option_b(c, pan, tag, title):
    head, blocks, desc = lines_and_blocks(pan['pieces'])
    top = pan['top']
    im = background(0)
    place(im, head, PANEL_X, top)
    col2 = False
    y2 = desc                                           # the second column starts level with the first block
    shift = None
    still_over = 0
    for b in blocks:
        if not col2 and top + block_bottom(b) > CH - 1:
            col2 = True
            shift = y2 - block_top(b)
        if col2:
            place(im, b['pieces'], COL2_X, top + shift)
            still_over = max(still_over, top + shift + block_bottom(b) - 1 - CH)
        else:
            place(im, b['pieces'], PANEL_X, top)
    note = 'всё влезло' if still_over <= 0 else 'вторая колонка тоже вылезает на %d ед.' % still_over
    return save(caption(im, 'B. Вторая колонка — ' + title,
                        ['Не влезающие пункты целиком переносятся вправо, x 235 (ширина колонки 141). %s.' % note.capitalize(),
                         'Колонка ложится на середину комнаты — на игрока и пьедестал.']), '%s-B-columns.png' % tag)


def option_c(c, pan, tag, title, hint):
    head, blocks, desc = lines_and_blocks(pan['pieces'])
    top = pan['top']
    pages, cur, y = [], [], desc
    for b in blocks:
        h = block_bottom(b) - block_top(b)
        room = CH - 1 - top - 12.0                      # a line kept for "...+N more"
        if cur and y + h > room:
            pages.append(cur)
            cur, y = [], desc + 12.0                    # the next page opens with the "before" line
        cur.append((b, y - block_top(b)))
        y += h
    pages.append(cur)
    files = []
    done = 0
    for n, page in enumerate(pages):
        im = background(0)
        place(im, head, PANEL_X, top)
        if n > 0:
            grey_line(im, '...+%d more' % done, PANEL_X + 11, top + desc)
        for b, shift in page:
            place(im, b['pieces'], PANEL_X, top + shift)
        done += len(page)
        left = sum(len(p) for p in pages[n + 1:])
        last_y = max(block_bottom(b) + s for b, s in page)
        if left:
            grey_line(im, '...+%d more  %s' % (left, hint), PANEL_X + 11, top + last_y)
        files.append(save(caption(im, 'C. Страницы, стр. %d/%d — %s' % (n + 1, len(pages), title),
                                  ['Как рецепты Мешка: страница по высоте экрана, заголовок повторяется, "...+N more" — сколько пунктов дальше/раньше.',
                                   'Листать: свайп по панели (только пока описание не влезает), R3 на паде, Map+стрельба на клавиатуре.']),
                          '%s-C-page%d.png' % (tag, n + 1)))
    return files


def option_c_real(exe, case):
    """the pages as eid.c now cuts and composes them (hotfix 1.72), the touch screen's hint"""
    tag, lang, kind, ident, ch, own, title = case[:7]
    first = panel(exe, lang, kind, ident, ch, own, 0)
    files = []
    for n in range(first['count']):
        pan = first if n == 0 else panel(exe, lang, kind, ident, ch, own, n)
        bottom = pan['top'] + pan['height'] - 1
        im = background(max(0, int(bottom - CH) + 6))
        place(im, pan['pieces'], PANEL_X, pan['top'], alpha_below=True)
        files.append(save(caption(im, 'C, как сделано. Стр. %d/%d — %s' % (n + 1, first['count'], title),
                                  ['Нарисовано из настоящей раскладки eid.c (стол): низ страницы на %d из 270.' % bottom,
                                   'Подсказка для пальца; на паде «(Страница: R3)», на клавиатуре «(Страница: Select + стрельба)».']),
                          '%s-C-real-page%d.png' % (tag, n + 1)))
    return files


def option_d(c, pan180, tag, title):
    bottom = pan180['top'] + pan180['height'] - 1
    extra = max(0, int(bottom - CH) + 6)
    im = background(extra)
    place(im, pan180['pieces'], PANEL_X, pan180['top'], alpha_below=True)
    over = bottom - CH
    note = 'всё влезло, низ на %d' % bottom if over <= 0 else 'всё ещё за краем на %d ед.' % over
    return save(caption(im, 'D. Ширина текста 180 (TextboxWidth мода) — ' + title,
                        ['Одна колонка, строки до x %d вместо %d: %s.' % (PANEL_X + 11 + 180, PANEL_X + 11 + 130, note),
                         'Это настройка самого мода (у нас зашито 130).']), '%s-D-width180.png' % tag)


def option_e(c, pan, tag, title):
    h = pan['height']
    new_top = max(HEARTS, min(pan['top'], CH + 1 - h))
    bottom = new_top + h - 1
    extra = max(0, int(bottom - CH) + 6)
    im = background(extra)
    place(im, pan['pieces'], PANEL_X, new_top, alpha_below=True)
    raised = pan['top'] - new_top
    if bottom <= CH:
        note = 'всё влезло'
    elif bottom - CH <= 2:
        note = 'впритык: буквы последней строки доходят до самого края (за краем только воздух под строкой, %d ед.)' % (bottom - CH)
    else:
        note = 'всё ещё за краем на %d ед.' % (bottom - CH)
    return save(caption(im, 'E. Панель поднята до сердец — ' + title,
                        ['Верх %d вместо %d (подъём на %d, выше %d нельзя — сердца): %s.' % (new_top, pan['top'], raised, HEARTS, note),
                         'Картинка предмета встаёт вплотную к третьему сердцу. Помогает только когда не хватает немного.']), '%s-E-raised.png' % tag)


# ------------------------------------------------------------------ the room
def paint_out_panel():
    """the owner's screenshot with the panel that was on it painted out"""
    im = cv2.imread(SHOT)
    H, W = im.shape[:2]
    u = H / 270.0
    x0, x1, y0, y1 = int(86 * u), int(232 * u), int(44 * u), H
    roi = im[y0:y1, x0:x1].astype(int)
    b, g, r = roi[..., 0], roi[..., 1], roi[..., 2]
    mx = np.maximum(np.maximum(r, g), b)
    mn = np.minimum(np.minimum(r, g), b)
    seed = ((mx > 150) | ((mx - mn > 60) & ~((r >= g) & (g >= b) & (r - b < 140) & (r < 200)))
            | ((mx - mn < 25) & (mx > 90))).astype(np.uint8)

    def disk(rad):
        d = int(2 * rad * u) | 1
        return cv2.getStructuringElement(cv2.MORPH_ELLIPSE, (d, d))
    m = cv2.dilate(seed, disk(1.6)) | (cv2.dilate(seed, disk(3.0)) & (mx < 75).astype(np.uint8))
    m = cv2.dilate(m, disk(0.5))
    mask = np.zeros((H, W), np.uint8)
    mask[y0:y1, x0:x1] = m * 255
    cv2.imwrite(os.path.join(OUT, '_room.png'), cv2.inpaint(im, mask, int(4 * u), cv2.INPAINT_TELEA))


CASES = [
    # tag, lang, kind, id, character, own, title, note for A, the page hint
    ('knife-en', 'en_us', 1, 114, 7, '118', "Азазель у Маминого ножа (EN, снимок владельца)",
     'строки «shoot out based on / charge amount»', '(Swipe: next page)'),
    ('bethany-en', 'en_us', 1, 352, 18, '584', 'Бетани с Книгой добродетелей у Стеклянной пушки (EN)',
     'хвост описания и блок огонька', '(Swipe: next page)'),
    ('bethany-ru', 'ru', 1, 352, 18, '584', 'Бетани с Книгой добродетелей у Стеклянной пушки (RU)',
     'хвост описания и блок огонька',
     u'(\u0421\u0432\u0430\u0439\u043f: \u0434\u0430\u043b\u044c\u0448\u0435)'),
]


def main():
    global FONT
    os.makedirs(OUT, exist_ok=True)
    if not os.path.exists(os.path.join(OUT, '_room.png')):
        paint_out_panel()
    FONT = Font('eid_default.fnt')
    exe = desk.build()
    os.environ['DESK_BOX'] = '180'
    exe180 = desk.build()
    del os.environ['DESK_BOX']
    for tag, lang, kind, ident, ch, own, title, note, hint in CASES:
        pan = panel(exe, lang, kind, ident, ch, own)
        pan180 = panel(exe180, lang, kind, ident, ch, own)
        option_a(None, pan, tag, title, note)
        option_b(None, pan, tag, title)
        option_c(None, pan, tag, title, hint)
        option_d(None, pan180, tag, title)
        option_e(None, pan, tag, title)
        option_c_real(exe, (tag, lang, kind, ident, ch, own, title))


if __name__ == '__main__':
    main()
