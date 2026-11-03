# -*- coding: utf-8 -*-
"""The touch editor's slider and pause button on the desk (hotfix 1.71).

Draws the editor as touchedit.c lays it out - the same numbers (TOOL_TOP,
SLIDER_TOP, SLIDER_END_AT, SLIDER_LABEL, the tool steps, the pause and the
twins' homes from hud.c) and the pictures by the animations of the real
port_touch_editor.anm2 over the real port_touch_tools.png (crop and pivot as
the file says, so a wrong pivot in the sheet shows here) - for every language
set, on 585 x 270, 480 x 270 and 480 x 360, and checks:

    the slider's row clear of the buttons, the pause and the twins' homes;
    the word and the value inside the row the finger takes;
    every ring centred on its item (Ring<n> against Icon32 at 2 f).

    py -3 tools/touchedit_desk.py      # other stuff/touch-editor-mockup/desk/*.png
"""
import os
import re
import sys

from PIL import Image

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
ASSETS = os.path.join(ROOT, 'port', 'app', 'src', 'main', 'assets')
HEADER = os.path.join(ROOT, 'port', 'app', 'src', 'main', 'cpp', 'touchedit_tools.h')
SOURCE = os.path.join(ROOT, 'port', 'app', 'src', 'main', 'cpp', 'touchedit.c')
OUT = os.path.join(ROOT, 'other stuff', 'touch-editor-mockup', 'desk')
TOUCH = os.path.join(ROOT, 'work', 'port-edit', 'rebirth-resources', 'data', 'gfx', 'ui', 'touch')
ITEMS = os.path.join(ROOT, 'work', 'app', 'repentance-resources', 'data', 'gfx', 'items', 'collectibles')
SETS = ['en', 'ru', 'de', 'es', 'jp', 'kr', 'zh']
FIELD = (37, 33, 32, 255)
PX = 2                                      # output pixels a unit
F = 0.5


def header():
    h = open(HEADER, encoding='utf-8').read()
    num = lambda n: float(re.search(r'#define %s ([\d.]+)' % n, h).group(1))
    arr = lambda n: [float(v.rstrip('f')) for v in re.search(r'%s\[[^]]*\] = \{ ([^}]*) \}' % n, h).group(1).split(', ')]
    return dict(track=num('SLIDER_TRACK'), knob_h=num('SLIDER_KNOB_H'), end_r=num('SLIDER_END_R'),
                label_h=num('SLIDER_LABEL_H'), low=int(num('SLIDER_PCT_LOW')), high=int(num('SLIDER_PCT_HIGH')),
                step=int(num('SLIDER_PCT_STEP')), tool_step=arr('TOOL_STEP_OF'), tool_high=num('TOOL_HIGH'),
                word_w=arr('OPACITY_W'), pct_w=arr('PCT_W'))


def source():
    c = open(SOURCE, encoding='utf-8').read()
    num = lambda n: float(re.search(r'#define %s +([\d.]+)f' % n, c).group(1))
    return dict(tool_top=num('TOOL_TOP'), sliver=num('TOOL_SLIVER'), top=num('SLIDER_TOP'),
                end_at=num('SLIDER_END_AT'), label=num('SLIDER_LABEL'), gap=num('SLIDER_WORD_GAP'),
                air=num('SLIDER_AIR'))


def frames():
    t = open(os.path.join(ASSETS, 'port_touch_editor.anm2'), encoding='utf-8').read()
    out = {}
    for m in re.finditer(r'<Animation Name="([^"]+)".*?<LayerAnimation LayerId="0"[^>]*>\s*<Frame [^>]*?'
                         r'XPivot="(-?\d+)" YPivot="(-?\d+)" XCrop="(\d+)" YCrop="(\d+)" Width="(\d+)" '
                         r'Height="(\d+)"', t, re.S):
        n, px, py, x, y, w, h = m.groups()
        out[n] = tuple(int(v) for v in (px, py, x, y, w, h))
    return out


SHEET = Image.open(os.path.join(ASSETS, 'port_touch_tools.png')).convert('RGBA')
FR = frames()
H, S = header(), source()


class Canvas(object):
    def __init__(self, w, h):
        self.w, self.h = w, h
        self.im = Image.new('RGBA', (int(w * PX), int(h * PX)), FIELD)

    def sprite(self, name, x, y, sx=1.0, sy=1.0, alpha=1.0, sheet=None, frame=None):
        """the game's Render: the frame's pivot at (x, y), scaled about it; sx, sy in units a texel"""
        px, py, cx, cy, w, h = frame if frame else FR[name]
        pic = (sheet or SHEET).crop((cx, cy, cx + w, cy + h))
        ow, oh = max(1, int(round(w * sx * PX))), max(1, int(round(h * sy * PX)))
        pic = pic.resize((ow, oh), Image.NEAREST)
        if alpha < 1.0:
            pic.putalpha(pic.getchannel('A').point(lambda v: int(round(v * alpha))))
        left, top = (x - px * sx) * PX, (y - py * sy) * PX
        self.im.alpha_composite(pic, (int(round(left)), int(round(top))))
        return (x - px * sx, y - py * sy, x - px * sx + w * sx, y - py * sy + h * sy)


def slider_image(set_index, pct):
    """the slider row as the editor draws it, at a texel a unit, cropped to its ink:
    for the guide's YOUR LAYOUT slide (guide_slides.py s_layout)"""
    c = Canvas(H['track'] + 2 * (S['end_at'] + H['end_r'] + 2), 40)
    c.im = Image.new('RGBA', c.im.size, (0, 0, 0, 0))
    x0, cy = S['end_at'] + H['end_r'] + 2, 9
    t = (pct - H['low']) / float(H['high'] - H['low'])
    fill = int(t * H['track'] + 0.5)
    c.sprite('SliderLow', x0 - S['end_at'], cy)
    c.sprite('SliderHigh', x0 + H['track'] + S['end_at'], cy)
    c.sprite('SliderTrack', x0, cy)
    if fill:
        c.sprite('SliderFillCap', x0, cy)
        c.sprite('SliderFill', x0, cy, fill, 1)
    c.sprite('SliderKnob', x0 + fill, cy)
    v = (pct - H['low']) // H['step']
    wide = H['word_w'][set_index] + S['gap'] + H['pct_w'][v]
    lx = int(x0 + H['track'] * 0.5 - wide * 0.5 + 0.5)
    c.sprite('Opacity%d' % set_index, lx, cy + S['label'])
    c.sprite('Pct%d' % pct, lx + H['word_w'][set_index] + S['gap'], cy + S['label'])
    im = c.im.resize((c.im.width // PX, c.im.height // PX), Image.NEAREST)
    return im.crop(im.getchannel('A').getbbox())


def editor(w, h, set_index, pct):
    c = Canvas(w, h)
    k = 2 * F
    boxes = {}
    # the four buttons (touchedit.c tool_box), their touch boxes
    step = H['tool_step'][set_index]
    names = ['Grid', 'Reset', 'Cancel', 'Done']
    for i, n in enumerate(names):
        cx = w * 0.5 + (i - 1.5) * step * k
        cy = S['tool_top'] * k
        c.sprite('%s%d' % (n, set_index), cx, cy, k, k, 0.45 if i == 0 else 1.0)
        bw, bh = (step - S['sliver']) * k, H['tool_high'] * k
        boxes['tool%d' % i] = (cx - bw / 2, cy - bh / 2, cx + bw / 2, cy + bh / 2)
    # the slider (touchedit.c slider_frame, slider_box, draw_slider)
    x0, cy = w * 0.5 - H['track'] * 0.5 * k, S['top'] * k
    x1 = x0 + H['track'] * k
    t = (pct - H['low']) / float(H['high'] - H['low'])
    fill = int(t * H['track'] + 0.5)
    c.sprite('SliderLow', x0 - S['end_at'] * k, cy, k, k)
    c.sprite('SliderHigh', x1 + S['end_at'] * k, cy, k, k)
    c.sprite('SliderTrack', x0, cy, k, k)
    if fill:
        c.sprite('SliderFillCap', x0, cy, k, k)
        c.sprite('SliderFill', x0, cy, fill * k, k)
    c.sprite('SliderKnob', x0 + fill * k, cy, k, k)
    v = (pct - H['low']) // H['step']
    wide = H['word_w'][set_index] + S['gap'] + H['pct_w'][v]
    lx = int(x0 + H['track'] * 0.5 * k - wide * 0.5 * k + 0.5)
    ly = cy + S['label'] * k
    word = c.sprite('Opacity%d' % set_index, lx, ly, k, k)
    value = c.sprite('Pct%d' % pct, lx + (H['word_w'][set_index] + S['gap']) * k, ly, k, k)
    reach = (S['end_at'] + H['end_r'] + S['air']) * k
    row = (x0 - reach, cy - (H['knob_h'] * 0.5 + S['air'] * 0.5) * k,
           x1 + reach, cy + (S['label'] + H['label_h'] * 0.5 + S['air'] * 0.5) * k)
    boxes['slider'] = row
    # the pause and the twins' homes (hud.c)
    boxes['pause'] = (w - 60 - 72 * F - 36 * F, 64 * F - 32 * F, w - 60 - 72 * F + 36 * F, 64 * F + 32 * F)
    boxes['twin'] = (w - 60 - 147 * F - 36 * F, 64 * F - 32 * F, w - 60 - 147 * F + 36 * F, 64 * F + 32 * F)
    touch = Image.open(os.path.join(TOUCH, 'ios_buttons.png')).convert('RGBA')
    c.sprite(None, w - 60 - 72 * F, 64 * F, F, F, pct / 100.0, touch, (25, 25, 192, 192, 50, 50))
    c.sprite(None, w - 60 - 147 * F, 64 * F, F, F, pct / 100.0, touch, (32, 32, 400, 0, 64, 64))
    # the checks
    meet = lambda a, b: a[0] < b[2] and b[0] < a[2] and a[1] < b[3] and b[1] < a[3]
    inside = lambda a, b: a[0] >= b[0] and a[2] <= b[2] and a[1] >= b[1] and a[3] <= b[3]
    faults = []
    for n in ('tool0', 'tool1', 'tool2', 'tool3', 'pause', 'twin'):
        if meet(row, boxes[n]):
            faults.append('the slider row meets %s' % n)
    for n, b in (('word', word), ('value', value)):
        if not inside(b, row):
            faults.append('the %s stands outside the slider row' % n)
    if not (row[0] >= 0 and row[2] <= w):
        faults.append('the slider row leaves the canvas')
    return c.im, faults


def rings():
    """each ring's middle against its item's: Icon32 (16,16) at 2 f, RingN (33,33) at f"""
    faults = []
    for i, leaf in enumerate(['collectibles_105_dice.png', 'collectibles_033_thebible.png',
                              'collectibles_710_bagofcrafting.png']):
        item = Image.open(os.path.join(ITEMS, leaf)).convert('RGBA')
        c = Canvas(40, 40)
        c.sprite(None, 20, 20, 2 * F, 2 * F, 1.0, item, (16, 16, 0, 0, 32, 32))
        before = c.im.copy()
        c.sprite('Ring%d' % i, 20, 20, F, F)
        # a ring pixel may never land on the item's own ink
        a, b = before.load(), c.im.load()
        for y in range(c.im.height):
            for x in range(c.im.width):
                if a[x, y] != FIELD and a[x, y] != b[x, y]:
                    faults.append('Ring%d lies over its item at %d,%d' % (i, x, y))
                    break
            if faults:
                break
        c.im.save(os.path.join(OUT, 'ring%d-x2.png' % i))
    return faults


def main():
    os.makedirs(OUT, exist_ok=True)
    faults = rings()
    for w, h, tag in ((585, 270, '585x270'), (480, 270, '16x9'), (480, 360, '4x3')):
        for k, s in enumerate(SETS):
            for pct in (10, 50, 100):
                im, f = editor(w, h, k, pct)
                faults += ['%s %s %d%%: %s' % (tag, s, pct, x) for x in f]
                if pct == 50:
                    im.save(os.path.join(OUT, '%s-%s.png' % (tag, s)))
    print('\n'.join(sorted(set(faults))) if faults else 'all clear')
    return 1 if faults else 0


if __name__ == '__main__':
    sys.exit(main())
