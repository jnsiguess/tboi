# -*- coding: utf-8 -*-
"""One player's mod's page with the mod's own settings - a picture to agree on
before the code is written (plan 30.09, 4.10 part 2: "settings rows in the
Tweaks style; preview first").

The page is drawn the way tweaks.c draws PAGE_USER today, piece by piece, out
of the very pictures the game is handed: port_mods_menu_<set>.anm2 and its
sheets (the wall, the sketches, the paper's pieces, the Extra words, the
cursor, ON and OFF), and the menu font (teammeatfont16bold / 20bold / 10, in
the menus' ink) for everything a mod brings at runtime. The one new baked word
is the heading SETTINGS (and its Russian), made as YOUR MODS is made
(tweaks_page.user_extras). Everything at 100%; the sheets are enlarged
NEAREST for looking at.

    py -3 tools/modsettings_preview.py    # -> other stuff/mod-settings-preview/
"""
import math
import os
import re
import struct
import sys
import xml.etree.ElementTree as ET

from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import lang                     # noqa: E402
import tweaks_page as tp        # noqa: E402

OUT = os.path.join(tp.ROOT, 'other stuff', 'mod-settings-preview')
FONT_DIR = os.path.join(tp.ROOT, 'work', 'app', 'repentance-resources', 'data', 'font')   # the game's own fonts
HEADER = os.path.join(tp.ROOT, 'port', 'app', 'src', 'main', 'cpp', 'tweaks_layout.h')
INK = (54, 47, 45)

SETS = {'data': ('en', 0), 'dlc3.ru': ('ru', 1)}


# ------------------------------------------------------------------ the header
def header():
    text = open(HEADER, encoding='utf-8').read()
    out = {}
    for name in ('TW_GAP', 'TW_CURSOR_W', 'TW_PAPER_Y', 'MD_ROW_COUNT', 'MD_ARROW_W', 'MD_BAND_W', 'MD_HEAD_H',
                 'MD_FOOT_H', 'MD_PIECE_H', 'MD_TITLE_Y', 'MD_SECTION_GAP', 'MD_MARGIN'):
        out[name] = int(re.search(r'#define %s (\d+)' % name, text).group(1))
    for n in ('TITLE', 'HEADING', 'ADD', 'DUMP', 'ENABLED', 'UP', 'DOWN', 'EXPORT', 'DELETE', 'SURE', 'RESTART'):
        out['MX_' + n] = int(re.search(r'#define MX_%s (\d+)' % n, text).group(1))
    body = text[text.index('MD_SET[TW_SETS]'):]
    out['MD_SET'] = []
    for m in re.finditer(r'\{ "(\w+)", (-?\d+), (\d+), (\d+), (\d+), (\d+), (\d+), (\d+), (\d+), (\w+), (\w+) \}', body):
        rec = dict(suffix=m.group(1), paper_x=int(m.group(2)), paper_w=int(m.group(3)), paper_h=int(m.group(4)),
                   step=int(m.group(5)), baseline=int(m.group(6)), row0=int(m.group(7)), on_w=int(m.group(8)),
                   off_w=int(m.group(9)))
        arr = re.search(r'%s\[\] = \{([^}]*)\}' % m.group(10), text).group(1)
        rec['word_w'] = [int(v) for v in arr.split(',')]
        out['MD_SET'].append(rec)
        if len(out['MD_SET']) == 7:
            break
    out['MD_BAND_X'] = [int(v) for v in re.search(r'MD_BAND_X\[TW_SETS\] = \{([^}]*)\}', text).group(1).split(',')]
    mx = re.search(r'MX_W\[TW_SETS\]\[MX_COUNT\] = \{(.*?)\};', text, re.S).group(1)
    out['MX_W'] = [[int(v) for v in r.split(',')] for r in re.findall(r'\{([^}]*)\}', mx)]
    mx = re.search(r'MX_H\[TW_SETS\]\[MX_COUNT\] = \{(.*?)\};', text, re.S).group(1)
    out['MX_H'] = [[int(v) for v in r.split(',')] for r in re.findall(r'\{([^}]*)\}', mx)]
    return out


H = header()


# ------------------------------------------------------------------ the anm2
class Anm2(object):
    """Just enough of the game's sprite: an animation's frame, every visible layer
    at its crop, pivot, rotation (0 or 90) and scale."""

    def __init__(self, path, set_name):
        root = ET.parse(path).getroot()
        self.sheets = {}
        for s in root.iter('Spritesheet'):
            p = s.get('Path')
            if p.lower().startswith('ui/'):
                full = os.path.join(tp.RES, set_name, 'gfx', *p.lower().split('/'))
            else:
                full = os.path.join(tp.ASSETS, p)
            self.sheets[int(s.get('Id'))] = Image.open(full).convert('RGBA')
        self.layers = {int(l.get('Id')): int(l.get('SpritesheetId')) for l in root.iter('Layer')}
        self.anims = {}
        for a in root.iter('Animation'):
            layers = []
            for la in a.find('LayerAnimations'):
                layers.append((int(la.get('LayerId')), la.get('Visible') == 'true',
                               [dict(f.attrib) for f in la.iter('Frame')]))
            self.anims[a.get('Name')] = layers

    def draw(self, canvas, anim, frame, x, y, sx=1.0, sy=1.0):
        for lid, visible, frames in self.anims[anim]:
            if not visible:
                continue
            t, f = 0, None
            for fr in frames:
                d = int(fr['Delay'])
                if frame < t + d:
                    f = fr
                    break
                t += d
            if f is None or f['Visible'] != 'true':
                continue
            cx, cy, w, h = (int(f[k]) for k in ('XCrop', 'YCrop', 'Width', 'Height'))
            im = self.sheets[self.layers[lid]].crop((cx, cy, cx + w, cy + h))
            px, py = float(f['XPivot']), float(f['YPivot'])
            fx, fy = x + float(f['XPosition']), y + float(f['YPosition'])
            if float(f['Rotation']) == 90:
                im = im.transpose(Image.ROTATE_270)          # a quarter clockwise
                left, top = fx + py - h, fy - px
            else:
                if sx != 1.0 or sy != 1.0:
                    im = im.resize((max(1, int(round(w * sx))), max(1, int(round(h * sy)))), Image.NEAREST)
                left, top = fx - px * sx, fy - py * sy
            canvas.alpha_composite(im, (int(math.floor(left)), int(math.floor(top))))


# ------------------------------------------------------------------ the menu font
class Font(object):
    """the game's BMFont, as DrawString writes it: a glyph at pen + xoffset, top + yoffset"""

    def __init__(self, name):
        path = os.path.join(FONT_DIR, name + '.fnt')
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
        self.pages = [Image.open(os.path.join(FONT_DIR, p)).convert('RGBA') for p in pages]

    def width(self, text):
        return sum(self.chars[ord(c)][6] for c in text if ord(c) in self.chars)

    def write(self, canvas, text, x, y, ink=INK):
        pen = 0
        x, y = int(math.floor(x)), int(math.floor(y))
        for ch in text:
            g = self.chars.get(ord(ch))
            if not g:
                continue
            gx, gy, gw, gh, xo, yo, xa, page = g
            if gw and gh:
                glyph = self.pages[page].crop((gx, gy, gx + gw, gy + gh))
                coloured = Image.new('RGBA', glyph.size, ink + (255,))
                coloured.putalpha(glyph.getchannel('A'))
                canvas.alpha_composite(coloured, (x + pen + xo, y + yo))
            pen += xa


FONT_ROW, FONT_TITLE, FONT_SMALL = 0, 1, 2
FONTS = [Font('teammeatfont16bold'), Font('teammeatfont20bold'), Font('teammeatfont10')]
CAP_BOTTOM = [20.0, 30.0, 13.0]
CAP_TOP = [2.0, 2.0, 1.0]
SMALL_LINE = 14.0


def centred(room, width):
    return math.floor((room - width) * 0.5)


# ------------------------------------------------------------------ the sample mod
# mod.json of the example, as in the research notes of 02.10.2026 (mod settings schema);
# the page shows a label in the page's language, "en" when it has none
SAMPLE = {
    'id': 'better-isaac',
    'name': {'en': 'Better Isaac Sprites'},
    'version': 'v1.2',
    'author': 'Someone',
    'description': {'en': 'Replaces Isaac and his tears with a cleaner hand-drawn style. 214 files.',
                    'ru': 'Заменяет Айзека и его слёзы на более чистую рисовку. 214 файлов.'},
    'settings': [
        {'key': 'blood-tears', 'kind': 'switch', 'default': False,
         'label': {'en': 'Blood tears', 'ru': 'Кровавые слёзы'}},
        {'key': 'style', 'kind': 'choice', 'default': 'clean',
         'label': {'en': 'Isaac style', 'ru': 'Рисовка Айзека'},
         'values': [{'value': 'classic', 'label': {'en': 'Classic', 'ru': 'Классика'}},
                    {'value': 'clean', 'label': {'en': 'Clean', 'ru': 'Чистая'}},
                    {'value': 'retro', 'label': {'en': 'Retro', 'ru': 'Ретро'}}]},
        {'key': 'outline', 'kind': 'number', 'default': 1, 'min': 0, 'max': 3, 'step': 1,
         'label': {'en': 'Outline width', 'ru': 'Толщина обводки'}},
        {'key': 'costumes', 'kind': 'switch', 'default': True,
         'label': {'en': 'Costumes too', 'ru': 'И костюмы тоже'}},
    ],
}
# the long page: the same mod with more of them, for the scroll
MORE = [
    {'key': 'hearts', 'kind': 'choice', 'default': 'game',
     'label': {'en': 'Heart icons', 'ru': 'Значки сердец'},
     'values': [{'value': 'game', 'label': {'en': 'Game', 'ru': 'Игровые'}},
                {'value': 'round', 'label': {'en': 'Round', 'ru': 'Круглые'}}]},
    {'key': 'shadows', 'kind': 'switch', 'default': True,
     'label': {'en': 'Soft shadows', 'ru': 'Мягкие тени'}},
    {'key': 'blink', 'kind': 'number', 'default': 3, 'min': 0, 'max': 10, 'step': 1,
     'label': {'en': 'Blink rate', 'ru': 'Частота моргания'}},
    {'key': 'champions', 'kind': 'switch', 'default': False,
     'label': {'en': 'Champion colours', 'ru': 'Цвета чемпионов'}},
    {'key': 'tear-colour', 'kind': 'choice', 'default': 'blue',
     'label': {'en': 'Tear colour', 'ru': 'Цвет слёз'},
     'values': [{'value': 'blue', 'label': {'en': 'Blue', 'ru': 'Синий'}},
                {'value': 'clear', 'label': {'en': 'Clear', 'ru': 'Прозрачный'}},
                {'value': 'red', 'label': {'en': 'Red', 'ru': 'Красный'}},
                {'value': 'gold', 'label': {'en': 'Gold', 'ru': 'Золотой'}}]},
    {'key': 'old-cry', 'kind': 'switch', 'default': False,
     'label': {'en': 'Old hurt face', 'ru': 'Старое лицо от удара'}},
]

HEADING_WORD = {'data': 'SETTINGS', 'dlc3.ru': 'НАСТРОЙКИ'}
OTHER_MODS = ['Retro HUD']
NOTE = {'data': 'Files completed by the port: 2, the game\'s used instead: 0 - mods/better-isaac.check.txt',
        'dlc3.ru': 'Порт дополнил файлов: 2, взял игровые вместо: 0 - mods/better-isaac.check.txt'}


def text_of(field, suffix):
    """a label in the page's language, else "en", else the first it has"""
    if isinstance(field, str):
        return field
    return field.get(suffix) or field.get('en') or next(iter(field.values()))


# ------------------------------------------------------------------ the page
class Page(object):
    """PAGE_USER as tweaks.c lays it (lay_out, row_top, row_layout), with the new
    block: the heading SETTINGS and a row per setting under the mod's own rows."""

    def __init__(self, set_name, mod, values, cursor_key, changed, notes=(), order='A'):
        self.set_name = set_name
        self.suffix, self.set = SETS[set_name]
        self.mod, self.values, self.changed = mod, values, changed
        self.art = Anm2(os.path.join(tp.ASSETS, 'port_mods_menu_%s.anm2' % self.suffix), set_name)
        self.base = H['MD_SET'][self.set]
        target = lang.native_share(Image.open(os.path.join(tp.RES, set_name, tp.MENU, 'optionsextra.png'))
                                   .convert('RGBA'))
        word = tp.word(set_name, HEADING_WORD[set_name], target, True)       # as user_extras makes YOUR MODS
        rule = tp.mods_stroke(word.width + 10)
        self.heading = Image.new('RGBA', (rule.width, word.height + 3 + rule.height), (0, 0, 0, 0))
        self.heading.alpha_composite(word, ((rule.width - word.width) // 2, 0))
        self.heading.alpha_composite(rule, (0, word.height + 3))
        # the rows: ('mx', MX id) | ('heading',) | ('set', setting) | ('restart',)
        # A: the mod's own rows, then its settings (as asked); B: ENABLED, the
        # settings, then the rarely used rows, apart - so the settings are on
        # the first screen
        own = [('mx', H['MX_UP']), ('mx', H['MX_DOWN']), ('mx', H['MX_EXPORT']), ('mx', H['MX_DELETE'])]
        block = ([('heading',)] + [('set', s) for s in mod['settings']]) if mod['settings'] else []
        if order == 'C':
            # the owner's decision (02.10.2026), what tweaks.c draws: the SETTINGS block first, then the mod's own
            # rows apart from it (ENABLED, MOVE UP, MOVE DOWN, EXPORT, DELETE), RESTART TO APPLY under them
            self.rows = block + [('mx', H['MX_ENABLED'])] + own
        else:
            self.rows = [('mx', H['MX_ENABLED'])] + (own + block if order == 'A' else block + own)
        if changed:
            self.rows.append(('restart',))
        # the rows with air above them: the heading, RESTART, and in B the own rows after the settings
        self.gaps = set(i for i, r in enumerate(self.rows) if r[0] in ('heading', 'restart'))
        if order == 'B' and block:
            self.gaps.add(1 + len(block))
        if order == 'C' and block:
            self.gaps.add(len(block))                 # ENABLED and the rest, apart from the settings
        self.cursor = next((i for i, r in enumerate(self.rows)
                            if r[0] == 'set' and r[1]['key'] == cursor_key), 0)
        self.notes_text = list(notes)
        self.lay_out()

    # words -----------------------------------------------------------------
    def onoff_w(self):
        return max(self.base['on_w'], self.base['off_w'])

    def label(self, s):
        return text_of(s['label'], self.suffix)

    def value_text(self, s):
        v = self.values.get(s['key'], s['default'])
        if s['kind'] == 'choice':
            return text_of(next(c for c in s['values'] if c['value'] == v)['label'], self.suffix)
        if s['kind'] == 'number':
            return str(v)
        return None

    def name_width(self, row):
        if row[0] == 'mx':
            return H['MX_W'][self.set][row[1]]
        if row[0] == 'restart':
            return H['MX_W'][self.set][H['MX_RESTART']]
        if row[0] == 'set':
            return FONTS[FONT_ROW].width(self.label(row[1]))
        return 0

    def value_width(self, row):
        if row[0] == 'mx':
            if row[1] in (H['MX_UP'], H['MX_DOWN']):
                return FONTS[FONT_ROW].width('00 / 00')
            return self.onoff_w() if row[1] == H['MX_ENABLED'] else 0
        if row[0] != 'set':
            return 0
        s = row[1]
        if s['kind'] == 'switch':
            return self.onoff_w()
        if s['kind'] == 'choice':               # the row is laid for its widest value, never the one shown
            return max(FONTS[FONT_ROW].width(text_of(c['label'], self.suffix)) for c in s['values'])
        return max(FONTS[FONT_ROW].width(str(s['min'])), FONTS[FONT_ROW].width(str(s['max'])))

    # layout ----------------------------------------------------------------
    def lay_out(self):
        b, s = self.base, self.set
        gap, cur = H['TW_GAP'], H['TW_CURSOR_W']
        onoff = self.onoff_w()
        valued = gap + onoff + gap + H['MD_ARROW_W']
        widest = 0
        for i in range(H['MD_ROW_COUNT']):
            widest = max(widest, cur + b['word_w'][i] + valued)
        mx = H['MX_W'][s]
        widest = max(widest, mx[H['MX_HEADING']], cur + mx[H['MX_ADD']], cur + mx[H['MX_DUMP']],
                     cur + mx[H['MX_RESTART']], cur + mx[H['MX_ENABLED']] + gap + onoff, cur + mx[H['MX_SURE']])
        for n in [text_of(self.mod['name'], self.suffix)] + OTHER_MODS:
            widest = max(widest, cur + FONTS[FONT_ROW].width(n) + valued)
        widest = max(widest, FONTS[FONT_ROW].width(text_of(self.mod['name'], self.suffix)) - cur)
        # the new rows: a setting is a line as any other - cursor, label, gap, its widest value
        widest = max(widest, self.heading.width)
        for row in self.rows:
            if row[0] == 'set':
                widest = max(widest, cur + self.name_width(row) + gap + self.value_width(row))
        need = widest + cur + 2 * H['MD_MARGIN'] - 256
        self.bands = int(math.ceil(need / float(H['MD_BAND_W']))) if need > 0 else 0
        self.paper_w = 256 + self.bands * H['MD_BAND_W']
        self.paper_x = 240 - self.paper_w // 2
        inner = self.paper_w - 2 * H['MD_MARGIN']
        name = text_of(self.mod['name'], self.suffix)
        self.title_font = FONT_TITLE if FONTS[FONT_TITLE].width(name) <= inner else FONT_ROW
        self.facts = '%s  -  %s' % (self.mod['version'], self.mod['author'])
        self.desc = self.wrap(text_of(self.mod['description'], self.suffix), inner)
        lines = 1 + len(self.desc)
        self.text_h = 4.0 + lines * SMALL_LINE + 2.0
        self.notes = []
        for n in self.notes_text:
            self.notes += self.wrap(n, inner)
        self.note_h = 6.0 + len(self.notes) * SMALL_LINE if self.notes else 0.0
        bottom = self.row_top(len(self.rows) - 1) + b['step'] + self.note_h
        pieces = max(1, int(math.ceil((bottom - b['row0']) / float(H['MD_PIECE_H']))))
        self.paper_h = H['MD_HEAD_H'] + pieces * H['MD_PIECE_H'] + H['MD_FOOT_H']

    def wrap(self, text, width):
        f, out, line = FONTS[FONT_SMALL], [], ''
        for w in text.split():
            t = (line + ' ' + w).strip()
            if line and f.width(t) > width:
                out.append(line)
                line = w
            else:
                line = t
        return out + [line] if line else out

    def row_top(self, i):
        b = self.base
        top = b['row0'] + i * b['step'] + self.text_h
        # the settings apart from the mod's own rows, as YOUR MODS on the list; RESTART apart from all
        return top + H['MD_SECTION_GAP'] * sum(1 for k in self.gaps if k <= i)

    def row_layout(self, row):
        nw, vw = self.name_width(row), self.value_width(row)
        total = H['TW_CURSOR_W'] + nw + (H['TW_GAP'] + vw if vw > 0 else 0)
        name_x = centred(self.paper_w, total) + H['TW_CURSOR_W']
        return name_x, name_x + nw + H['TW_GAP']

    # drawing ----------------------------------------------------------------
    def scroll_room(self, screen_h=270):
        bottom = H['TW_PAPER_Y'] + self.paper_h - 1
        return max(0, bottom - screen_h)

    def scroll_to_settings(self, screen_h=270):
        """the page scrolled to the heading SETTINGS: the whole block in sight"""
        i = next(k for k, r in enumerate(self.rows) if r[0] == 'heading')
        return int(max(0, min(self.scroll_room(screen_h), H['TW_PAPER_Y'] + self.row_top(i) - 12)))

    def draw(self, w, h, scroll=0, canvas=None, top_at=None):
        """the glass of a w x h canvas, the page scrolled `scroll` units down"""
        half_x, half_y = centred(w, 480), centred(h, 270) if top_at is None else top_at
        off_x, off_y = 0, -scroll
        c = canvas or Image.new('RGBA', (w, h), (0, 0, 0, 255))
        self.art.draw(c, 'Wall', 0, -20, -20, (w + 40) / 10.0, (h + 40) / 10.0)
        self.art.draw(c, 'Sketches', 0, half_x + off_x, half_y + off_y)

        def at(px, py):
            return half_x + self.paper_x + px + off_x, half_y + H['TW_PAPER_Y'] + py + off_y

        # the paper, piece by piece (draw_live_paper)
        b = self.base
        pieces = (self.paper_h - H['MD_HEAD_H'] - H['MD_FOOT_H']) // H['MD_PIECE_H']
        y, band_x = 0, H['MD_BAND_X'][self.set]
        for k in range(pieces + 2):
            part = 0 if k == 0 else (6 if k == pieces + 1 else 1 + (k - 1) % 5)
            ph = H['MD_HEAD_H'] if part == 0 else (H['MD_FOOT_H'] if part == 6 else H['MD_PIECE_H'])
            self.art.draw(c, 'Pieces', part * 3, *at(0, y))
            for n in range(self.bands):
                self.art.draw(c, 'Pieces', part * 3 + 1, *at(band_x + n * H['MD_BAND_W'], y))
            self.art.draw(c, 'Pieces', part * 3 + 2, *at(band_x + self.bands * H['MD_BAND_W'], y))
            y += ph
        name = text_of(self.mod['name'], self.suffix)
        tw = FONTS[self.title_font].width(name)
        FONTS[self.title_font].write(c, name, *at(centred(self.paper_w, tw),
                                                  math.floor(H['MD_TITLE_Y'] - CAP_TOP[self.title_font])))

        # the rows
        base = b['baseline']
        for i, row in enumerate(self.rows):
            top = self.row_top(i)
            name_x, value_x = self.row_layout(row)
            if row[0] == 'heading':
                hx = centred(self.paper_w, self.heading.width)
                cx, cy = at(hx, top + centred(b['step'], self.heading.height) - 2)
                c.alpha_composite(self.heading, (int(cx), int(cy)))
                continue
            if row[0] in ('mx', 'restart'):
                x = row[1] if row[0] == 'mx' else H['MX_RESTART']
                self.art.draw(c, 'Extra', x, *at(name_x, top + base - H['MX_H'][self.set][x]))
            else:
                FONTS[FONT_ROW].write(c, self.label(row[1]), *at(name_x, top + base - CAP_BOTTOM[FONT_ROW]))
            cx, cy = at(value_x, top)
            if row[0] == 'mx' and row[1] in (H['MX_UP'], H['MX_DOWN']):
                FONTS[FONT_ROW].write(c, '1 / %d' % (1 + len(OTHER_MODS)), cx, cy + base - CAP_BOTTOM[FONT_ROW])
            elif (row[0] == 'mx' and row[1] == H['MX_ENABLED']) or (row[0] == 'set' and row[1]['kind'] == 'switch'):
                on = True if row[0] == 'mx' else bool(self.values.get(row[1]['key'], row[1]['default']))
                ln = b['on_w'] if on else b['off_w']
                self.art.draw(c, 'On' if on else 'Off', 0, cx + ln - 16, cy + base - 2)
            elif row[0] == 'set':
                FONTS[FONT_ROW].write(c, self.value_text(row[1]), cx, cy + base - CAP_BOTTOM[FONT_ROW])
            if i == self.cursor:
                cx, cy = at(name_x, top)
                self.art.draw(c, 'Cursor', 0, cx - 6, cy + base - 3)

        # the facts, the description, the notes (draw_live_text)
        y = b['row0'] + 4.0 - CAP_TOP[FONT_SMALL]
        for line in [self.facts] + self.desc:
            lw = FONTS[FONT_SMALL].width(line)
            FONTS[FONT_SMALL].write(c, line, *at(centred(self.paper_w, lw), math.floor(y)))
            y += SMALL_LINE
        if self.notes:
            y = self.row_top(len(self.rows) - 1) + b['step'] + 6.0 - CAP_TOP[FONT_SMALL]
            for line in self.notes:
                lw = FONTS[FONT_SMALL].width(line)
                FONTS[FONT_SMALL].write(c, line, *at(centred(self.paper_w, lw), math.floor(y)))
                y += SMALL_LINE
        return c

    def whole(self):
        """the whole page on a canvas as tall as it is, to show where the screen stands"""
        h = max(270, H['TW_PAPER_Y'] + self.paper_h + 15)
        return self.draw(480, h, top_at=0)    # the screen's top where a 16:9 screen's is


# ------------------------------------------------------------------ the sheets
CAPTION_INK = (232, 222, 208)
BACK = (40, 34, 32, 255)


def caption(sheet, text, x, y):
    FONTS[FONT_SMALL].write(sheet, text, x, y, ink=CAPTION_INK)


def framed_window(whole, scroll, h=270):
    """the whole page, the screen's place on it outlined"""
    im = whole.copy()
    px = im.load()
    for x in range(im.width):
        for yy in (scroll, scroll + h - 1):
            if 0 <= yy < im.height:
                px[x, yy] = (230, 40, 40, 255)
    for yy in range(max(0, scroll), min(im.height, scroll + h)):
        for x in (0, im.width - 1):
            px[x, yy] = (230, 40, 40, 255)
    return im


def sheet_of(items, columns, scale):
    """items: (caption, image); laid in rows of `columns`, enlarged NEAREST"""
    pad, cap = 10, 18
    rows = [items[i:i + columns] for i in range(0, len(items), columns)]
    widths = [sum(im.width for _, im in r) + pad * (len(r) + 1) for r in rows]
    heights = [max(im.height for _, im in r) + cap + pad for r in rows]
    out = Image.new('RGBA', (max(widths), sum(heights) + pad), BACK)
    y = pad
    for r, rh in zip(rows, heights):
        x = pad
        for text, im in r:
            caption(out, text, x, y)
            out.alpha_composite(im, (x, y + cap))
            x += im.width + pad
        y += rh
    return out.resize((out.width * scale, out.height * scale), Image.NEAREST)


def main():
    os.makedirs(OUT, exist_ok=True)
    long_mod = dict(SAMPLE, settings=SAMPLE['settings'] + MORE)
    changed = {'blood-tears': True}                 # off by default: turned on, waits for a restart
    long_values = dict(changed, **{'tear-colour': 'red'})
    words = {'data': ('16:9 (480x270) - the page opened',
                      '16:9 - scrolled to the settings, Blood tears turned on',
                      '20:9 (600x270) - the same',
                      '16:9 - nothing changed: no RESTART row',
                      'Order B, 16:9 - the page opened: settings under ENABLED',
                      'Order B, 16:9 - scrolled down',
                      'Long page - top', 'Long page - scrolled to the bottom', 'The whole long page; red - the screen'),
             'dlc3.ru': ('16:9 (480x270) - страница открыта',
                         '16:9 - прокручено к настройкам, Кровавые слёзы включены',
                         '20:9 (600x270) - то же',
                         '16:9 - ничего не менялось: строки перезапуска нет',
                         'Порядок Б, 16:9 - страница открыта: настройки под ВКЛЮЧЁН',
                         'Порядок Б, 16:9 - прокручено вниз',
                         'Длинная страница - верх', 'Длинная - прокручена до низа',
                         'Вся длинная страница; красное - экран')}
    for set_name, (suffix, _) in SETS.items():
        w = words[set_name]
        p = Page(set_name, SAMPLE, changed, 'style', True)
        at = p.scroll_to_settings()
        u = Page(set_name, SAMPLE, {}, 'style', False)
        b2 = Page(set_name, SAMPLE, changed, 'style', True, order='B')
        lp = Page(set_name, long_mod, long_values, 'old-cry', True, notes=[NOTE[set_name]])
        room = lp.scroll_room()
        pics = {
            '16x9-open': Page(set_name, SAMPLE, changed, None, True).draw(480, 270),
            '16x9-settings': p.draw(480, 270, scroll=at),
            '20x9-settings': p.draw(600, 270, scroll=at),
            '16x9-unchanged': u.draw(480, 270, scroll=u.scroll_to_settings()),
            'B-16x9-open': Page(set_name, SAMPLE, changed, None, True, order='B').draw(480, 270),
            'B-16x9-bottom': b2.draw(480, 270, scroll=b2.scroll_room()),
            'long-top': Page(set_name, long_mod, long_values, None, True, notes=[NOTE[set_name]]).draw(480, 270),
            'long-bottom': lp.draw(480, 270, scroll=room),
            'long-whole': framed_window(lp.whole(), room),
        }
        # the owner's order (C, 02.10.2026) - what the port draws now
        c = Page(set_name, SAMPLE, changed, 'style', True, order='C')
        pics['C-16x9-open'] = Page(set_name, SAMPLE, changed, None, True, order='C').draw(480, 270)
        pics['C-16x9-bottom'] = c.draw(480, 270, scroll=c.scroll_room())
        lc = Page(set_name, long_mod, long_values, 'old-cry', True, notes=[NOTE[set_name]], order='C')
        pics['C-long-whole'] = framed_window(lc.whole(), lc.scroll_room())
        for name, im in pics.items():
            im.save(os.path.join(OUT, 'page-%s-%s.png' % (suffix, name)))
        order = ['16x9-open', '16x9-settings', '20x9-settings', '16x9-unchanged', 'B-16x9-open', 'B-16x9-bottom']
        ov = sheet_of([(w[k], pics[n]) for k, n in enumerate(order)], 3, 2)
        ov.save(os.path.join(OUT, 'overview-%s.png' % suffix))
        ov_long = sheet_of([(w[6], pics['long-top']), (w[7], pics['long-bottom']), (w[8], pics['long-whole'])], 3, 2)
        ov_long.save(os.path.join(OUT, 'overview-long-%s.png' % suffix))
        cap = {'data': ("The owner's order - opened", "The owner's order - scrolled down", 'The whole long page'),
               'dlc3.ru': ('Порядок владельца - открыта', 'Порядок владельца - прокручено вниз', 'Вся длинная страница')}[set_name]
        ov_c = sheet_of([(cap[0], pics['C-16x9-open']), (cap[1], pics['C-16x9-bottom']), (cap[2], pics['C-long-whole'])], 3, 2)
        ov_c.save(os.path.join(OUT, 'overview-C-%s.png' % suffix))
        print(suffix, 'paper', p.paper_w, 'x', p.paper_h, 'bands', p.bands, 'settings at', at,
              '| long', lp.paper_w, 'x', lp.paper_h, 'scroll room', room, '| overview', ov.size, ov_long.size)


if __name__ == '__main__':
    main()
