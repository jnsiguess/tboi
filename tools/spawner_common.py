# -*- coding: utf-8 -*-
"""What the spawner's resource tools share: the seven language sets, the game's
string table, the glyph checks and the .anm2 writer.

The tools that use it:

    tools/spawner_tabs.py      the tab words, their strokes, the enemy-list headings
    tools/spawner_enemies.py   the enemy icons and the enemy table
    tools/spawner_rooms.py     the room icons and the room table (teleport)
    tools/spawner_floors.py    the floor table (teleport)
    tools/spawner_stats.py     the stat icons and the card's own words

THE SETS go in the order of spawner_title.h and tweaks_layout.h: 0 en (data),
1 ru, 2 de, 3 es, 4 jp, 5 kr, 6 zh. Every table these tools write keeps it.

TWO FONTS, TWO CHECKS. The tab words and headings are drawn on the desk in the
game's hand (tweaks_page.word) - its face per set is lang.SETTINGS; the rows of
the card are written at runtime in the description face (eid.c panel_font):
eid_default for the Latin, Cyrillic and Japanese sets, the Korean and Chinese
faces of their own for those two (eid.c FACES). A character a face lacks is
silently skipped by both, so every word is looked up before it is used.
"""
import io
import os
import struct
import sys
import xml.etree.ElementTree as ET

# the console is cp1251: names in kana and hangul would stop a print
try:
    sys.stdout.reconfigure(encoding='utf-8', errors='replace')
except AttributeError:
    pass

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
RES = os.path.join(ROOT, 'work', 'app', 'repentance-resources')
DATA = os.path.join(RES, 'data')
ASSETS = os.path.join(ROOT, 'port', 'app', 'src', 'main', 'assets')
CPP = os.path.join(ROOT, 'port', 'app', 'src', 'main', 'cpp')

# set folder, file suffix - the order of every table
SETS = [('data', 'en'), ('dlc3.ru', 'ru'), ('dlc3.de', 'de'), ('dlc3.es', 'es'),
        ('dlc3.jp', 'jp'), ('dlc3.kr', 'kr'), ('dlc3.zh', 'zh')]
LANGS = [s[1] for s in SETS]

# stringtable.sta's <languages>: en jp kr zh ru de es fr (index 1..8, 0 is the key)
_TABLE_INDEX = {'en': 0, 'jp': 1, 'kr': 2, 'zh': 3, 'ru': 4, 'de': 5, 'es': 6}

# the description face each set's rows are written in (eid.c: FACES, panel_font)
DESC_FACE = {'en': 'eid_default.fnt', 'ru': 'eid_default.fnt', 'de': 'eid_default.fnt',
             'es': 'eid_default.fnt', 'jp': 'eid_default.fnt',
             'kr': 'eid_korean_yoonwebdotum10_shadow.fnt', 'zh': 'eid_cn_alt.fnt'}

# the hand each set's menu words are drawn in (lang.SETTINGS, typeset.compose)
HAND_FACE = {
    'en': os.path.join(DATA, 'font', 'teammeatfont12.fnt'),
    'ru': os.path.join(DATA, 'font', 'teammeatfont12.fnt'),
    'de': os.path.join(DATA, 'font', 'teammeatfont12.fnt'),
    'es': os.path.join(DATA, 'font', 'teammeatfont12.fnt'),
    'jp': os.path.join(ROOT, 'menu-labels', 'fonts', 'MochiyPopOne-Regular.ttf'),
    'kr': os.path.join(ROOT, 'menu-labels', 'fonts', 'GamjaFlower-Regular.ttf'),
    'zh': os.path.join(RES, 'dlc3.zh', 'font', 'teammeatfontextended16bold.fnt'),
}


# ------------------------------------------------------------------ the game's strings
_table = None


def stringtable(categories=None):
    """key -> {lang: text} for the seven sets; '' where the game left it empty."""
    global _table
    if _table is None:
        _table = {}
        root = ET.parse(os.path.join(DATA, 'stringtable.sta')).getroot()
        for cat in root.findall('category'):
            for k in cat.findall('key'):
                s = [x.text or '' for x in k.findall('string')]
                _table.setdefault(cat.get('name'), {})[k.get('name')] = dict(
                    (lg, s[i] if i < len(s) else '') for lg, i in _TABLE_INDEX.items())
    out = {}
    for cat, keys in _table.items():
        if categories and cat not in categories:
            continue
        for k, v in keys.items():
            out.setdefault(k, v)
    return out


# ------------------------------------------------------------------ fonts
_charsets = {}


def _bmfont_chars(path):
    data = io.open(path, 'rb').read()
    if data[:3] != b'BMF':
        raise SystemExit('%s is not a BMFont binary' % path)
    at, ids = 4, set()
    while at + 5 <= len(data):
        kind = data[at]
        size = struct.unpack_from('<i', data, at + 1)[0]
        if kind == 4:
            for i in range(at + 5, at + 5 + size, 20):
                ids.add(struct.unpack_from('<I', data, i)[0])
        at += 5 + size
    return ids


def _ttf_chars(path):
    """The code points a TrueType face maps (its cmap, formats 4 and 12)."""
    d = io.open(path, 'rb').read()
    tables = struct.unpack_from('>H', d, 4)[0]
    cmap = None
    for i in range(tables):
        tag, _sum, off, _len = struct.unpack_from('>4sIII', d, 12 + 16 * i)
        if tag == b'cmap':
            cmap = off
    if cmap is None:
        raise SystemExit('%s has no cmap' % path)
    ids = set()
    for i in range(struct.unpack_from('>H', d, cmap + 2)[0]):
        pid, eid, off = struct.unpack_from('>HHI', d, cmap + 4 + 8 * i)
        at = cmap + off
        fmt = struct.unpack_from('>H', d, at)[0]
        if fmt == 4:
            n = struct.unpack_from('>H', d, at + 6)[0] // 2
            ends = struct.unpack_from('>%dH' % n, d, at + 14)
            starts = struct.unpack_from('>%dH' % n, d, at + 16 + 2 * n)
            deltas = struct.unpack_from('>%dh' % n, d, at + 16 + 4 * n)
            ro_at = at + 16 + 6 * n
            ranges = struct.unpack_from('>%dH' % n, d, ro_at)
            for k in range(n):
                for c in range(starts[k], ends[k] + 1):
                    if c == 0xffff:
                        continue
                    if ranges[k]:
                        g_at = ro_at + 2 * k + ranges[k] + 2 * (c - starts[k])
                        g = struct.unpack_from('>H', d, g_at)[0]
                        if g:
                            ids.add(c)
                    elif (c + deltas[k]) & 0xffff:
                        ids.add(c)
        elif fmt == 12:
            groups = struct.unpack_from('>I', d, at + 12)[0]
            for k in range(groups):
                a, b, g = struct.unpack_from('>III', d, at + 16 + 12 * k)
                if g or a:
                    ids.update(range(a, b + 1))
    return ids


def charset(path):
    if path not in _charsets:
        _charsets[path] = _ttf_chars(path) if path.lower().endswith('.ttf') else _bmfont_chars(path)
    return _charsets[path]


def desc_missing(lang, text):
    """The characters of `text` the set's description face does not have."""
    have = charset(os.path.join(ASSETS, DESC_FACE[lang]))
    return sorted(set(c for c in text if ord(c) not in have and c not in '\n'))


def hand_missing(lang, text):
    """The characters of `text` the set's hand (the menu face) does not have."""
    have = charset(HAND_FACE[lang])
    return sorted(set(c for c in text if c != ' ' and ord(c) not in have))


def require_hand(lang, words):
    """Fail loudly: a word drawn with a hole in it would be shipped as if whole."""
    bad = [(w, hand_missing(lang, w)) for w in words]
    bad = [(w, m) for w, m in bad if m]
    if bad:
        raise SystemExit('the %s hand (%s) lacks: %s' % (
            lang, os.path.basename(HAND_FACE[lang]),
            '; '.join('%s -> %s' % (w, ' '.join('%s U+%04X' % (c, ord(c)) for c in m)) for w, m in bad)))


# ------------------------------------------------------------------ C
def c_str(s):
    """A C string literal; UTF-8 kept as it is, as the other generated headers do."""
    return '"%s"' % s.replace('\\', '\\\\').replace('"', '\\"').replace('\n', '\\n')


def write_text(path, text):
    """Write only when different, so a re-run leaves the timestamps alone."""
    if os.path.isfile(path) and io.open(path, encoding='utf-8', newline='').read() == text:
        return False
    with io.open(path, 'w', encoding='utf-8', newline='\n') as f:
        f.write(text)
    return True


def save_png(im, path):
    """Save only when the pixels differ (the build repacks on a newer file)."""
    from PIL import Image, ImageChops
    if os.path.isfile(path):
        old = Image.open(path).convert('RGBA')
        if old.size == im.size and ImageChops.difference(old, im.convert('RGBA')).getbbox() is None:
            return False
    im.save(path)
    return True


# ------------------------------------------------------------------ .anm2
def _frame(cx, cy, w, h, delay, visible=True):
    return ('<Frame XPosition="0" YPosition="0" XPivot="0" YPivot="0" XCrop="%d" YCrop="%d"'
            ' Width="%d" Height="%d" XScale="100" YScale="100" Delay="%d" Visible="%s"'
            ' RedTint="255" GreenTint="255" BlueTint="255" AlphaTint="255" RedOffset="0"'
            ' GreenOffset="0" BlueOffset="0" Rotation="0" Interpolated="false"/>'
            % (cx, cy, w, h, delay, 'true' if visible else 'false'))


def anm2(png_name, anims, created):
    """A .anm2 the way tweaks_page.write_mods_anm2 makes its Pieces and Extra: one
    sheet, a layer per animation, and an animation whose frame k is crop k, drawn
    at 100 % with the pivot at its top left. In every animation all the layers
    are listed (the engine finds a layer by its record), the others hidden.
    A single frame lasts the whole animation: Delay = FrameNum.

    anims: [(name, [(x, y, w, h), ...]), ...]
    """
    T = '\t'
    out = ['<AnimatedActor>',
           T + '<Info CreatedBy="Isaac Android port" CreatedOn="%s" Version="2" Fps="30"/>' % created,
           T + '<Content>', T * 2 + '<Spritesheets>',
           T * 3 + '<Spritesheet Path="%s" Id="0"/>' % png_name,
           T * 2 + '</Spritesheets>', T * 2 + '<Layers>']
    out += [T * 3 + '<Layer Name="%s" Id="%d" SpritesheetId="0"/>' % (name, i)
            for i, (name, _f) in enumerate(anims)]
    out += [T * 2 + '</Layers>', T * 2 + '<Nulls/>', T * 2 + '<Events/>', T + '</Content>',
            T + '<Animations DefaultAnimation="%s">' % anims[0][0]]
    for name, frames in anims:
        count = len(frames)
        out.append(T * 2 + '<Animation Name="%s" FrameNum="%d" Loop="false">' % (name, count))
        out += [T * 3 + '<RootAnimation>',
                T * 4 + '<Frame XPosition="0" YPosition="0" XScale="100" YScale="100" Delay="%d"'
                ' Visible="true" RedTint="255" GreenTint="255" BlueTint="255" AlphaTint="255"'
                ' RedOffset="0" GreenOffset="0" BlueOffset="0" Rotation="0"'
                ' Interpolated="false"/>' % count,
                T * 3 + '</RootAnimation>', T * 3 + '<LayerAnimations>']
        for i, (other, oframes) in enumerate(anims):
            here = other == name
            out.append(T * 4 + '<LayerAnimation LayerId="%d" Visible="%s">' % (i, 'true' if here else 'false'))
            if here:
                delay = count if count == 1 else 1
                out += [T * 5 + _frame(x, y, w, h, delay) for x, y, w, h in frames]
            else:
                x, y, w, h = oframes[0]
                out.append(T * 5 + _frame(x, y, w, h, count, False))
            out.append(T * 4 + '</LayerAnimation>')
        out += [T * 3 + '</LayerAnimations>', T * 3 + '<NullAnimations/>', T * 3 + '<Triggers/>',
                T * 2 + '</Animation>']
    out += [T + '</Animations>', '</AnimatedActor>', '']
    return '\n'.join(out)


def stack(pictures, gap=1):
    """Pictures one under another on a transparent sheet; answers (sheet, boxes)."""
    from PIL import Image
    w = max(p.width for p in pictures)
    h = sum(p.height + gap for p in pictures) - gap
    sheet = Image.new('RGBA', (w, h), (0, 0, 0, 0))
    boxes, y = [], 0
    for p in pictures:
        sheet.alpha_composite(p, (0, y))
        boxes.append((0, y, p.width, p.height))
        y += p.height + gap
    return sheet, boxes
