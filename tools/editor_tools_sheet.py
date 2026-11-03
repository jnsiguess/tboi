# -*- coding: utf-8 -*-
"""The touch editor's four tool buttons: an icon over a word, in the game's
language (the owner's item 4, 23.09.2026 - variant C of
tools/editor_buttons_preview.py, the grid drawn as its option 3).

Writes three things, all from one table so they cannot drift apart:

    assets/port_touch_tools.png     a row per language set, four cells in each
    assets/port_touch_editor.anm2   Grid0..Done6: one animation per set and button
    cpp/touchedit_tools.h           the step between buttons, per set

The word is set in the EID face (eid_default.fnt; Korean in its own face,
whose baked shadow is swapped for the same 1-px edge the icons have). Where a
word does not fit the usual step the buttons stand further apart instead of the
word being cut: the step is the widest word of the set plus a gap, never less
than the old 44.

The sets follow tweaks_layout.h tw_set_for_language: en ru de es jp kr zh.

THE OPACITY SLIDER (hotfix 1.71, approved from other stuff/touch-editor-mockup,
05.10.2026) lives in the same sheet, below the buttons, in the same manner -
light ink, the 1-px black edge, no plate:

    SliderTrack          the hairline, SLIDER_TRACK texels between its caps
    SliderFill/FillCap   one column of the thick part, stretched by whole texels
    SliderKnob           the thumb, 9 x 15 with two grip lines
    SliderLow/High       a ring (see-through) and a disc (solid) at the two ends
    Opacity0..6          the word, per set (en OPACITY, the ru word is far longer - the
                         owner's word, as Photoshop's Opacity: 100% is solid)
    Pct10..Pct100        the value, every five per cent
    Ring0..2             the white outline of a charged active, OUTSIDE the
                         picture only, for the three the editor shows (the die,
                         the Bible, the bag): the run draws a slot into a render
                         target and fades the target as a whole, so an outline
                         drawn under the picture would show through it once the
                         editor fades the pieces one by one. 2x art, 66 x 66.

    py -3 tools/editor_tools_sheet.py
"""
import os
import re
import shutil
import sys

import numpy as np
from PIL import Image

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import editor_buttons_preview as E      # noqa: E402  the icons and the colours
from eid_font_preview import Font       # noqa: E402

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
MAIN = os.path.join(ROOT, 'port', 'app', 'src', 'main')
ASSETS = os.path.join(MAIN, 'assets')
SHEET = os.path.join(ASSETS, 'port_touch_tools.png')
ANM2 = os.path.join(ASSETS, 'port_touch_editor.anm2')
HEADER = os.path.join(MAIN, 'cpp', 'touchedit_tools.h')
# The copies as they were, in the workbench: anything in assets/ rides into the APK.
KEEP = os.path.join(ROOT, 'menu-labels', 'reference')

SETS = [
    ('en', ['GRID', 'RESET', 'CANCEL', 'DONE']),
    ('ru', ['СЕТКА', 'СБРОС', 'ОТМЕНА', 'ГОТОВО']),
    ('de', ['RASTER', 'RESET', 'ABBRUCH', 'FERTIG']),
    ('es', ['REJILLA', 'REINICIAR', 'CANCELAR', 'LISTO']),
    ('jp', ['グリッド', 'リセット', 'キャンセル', 'けってい']),
    ('kr', ['격자', '초기화', '취소', '완료']),
    ('zh', ['网格', '重置', '取消', '完成']),
]
ANIMATIONS = ['Grid', 'Reset', 'Cancel', 'Done']      # touchedit.c TOOL_ANIMATION
ICON_OF = dict(zip(ANIMATIONS, E.ORDER))
STEP_MIN = 44
GAP = 6
CELL_H = 34


def outlined(im):
    """The Korean face's own shadow off, the icons' 1-px edge on."""
    a = np.asarray(im.convert('RGBA')).astype(int)
    ink = (a[..., 3] > 0) & (a[..., :3].sum(axis=2) > 3 * 128)     # the light letter, not its shadow
    pad = np.pad(ink, 1)
    edge = np.zeros_like(pad)
    for dy in (-1, 0, 1):
        for dx in (-1, 0, 1):
            edge |= np.roll(np.roll(pad, dy, 0), dx, 1)
    out = np.zeros(pad.shape + (4,), np.uint8)
    out[edge] = E.EDGE
    out[pad] = E.LIGHT
    return Image.fromarray(out)


def keep(path, tail):
    os.makedirs(KEEP, exist_ok=True)
    to = os.path.join(KEEP, os.path.basename(path) + '.before-' + tail)
    if not os.path.exists(to):
        shutil.copy2(path, to)


FRAME = ('<Frame XPosition="0" YPosition="0" XPivot="{px}" YPivot="{py}" XCrop="{x}" YCrop="{y}" '
         'Width="{w}" Height="{h}" XScale="100" YScale="100" Delay="1" Visible="true" RedTint="255" '
         'GreenTint="255" BlueTint="255" AlphaTint="255" RedOffset="0" GreenOffset="0" BlueOffset="0" '
         'Rotation="0" Interpolated="false"/>')


def animation(name, x, y, w, h, template, pivot=None):
    """A copy of the old Grid animation with its crop moved: the full set of layers stays."""
    px, py = pivot if pivot is not None else (w // 2, h // 2)
    s = re.sub(r'Name="Grid0?"', 'Name="%s"' % name, template, count=1)
    return re.sub(r'<Frame XPosition="0" YPosition="0" XPivot=[^>]*/>',
                  FRAME.format(px=px, py=py, x=x, y=y, w=w, h=h), s, count=1)


# ------------------------------------------------------------------ the slider

OPACITY = {'en': 'OPACITY', 'ru': 'НЕПРОЗРАЧНОСТЬ', 'de': 'DECKKRAFT', 'es': 'OPACIDAD',
           'jp': '不透明度', 'kr': '불투명도', 'zh': '不透明度'}
PCT_LOW, PCT_HIGH, PCT_STEP = 10, 100, 5          # sticks.c ALPHA_MIN .. 1, a twentieth a step
SLIDER_TRACK = 112                                # texels between the track's two caps
KNOB_W, KNOB_H = 7, 13                            # the thumb's light part; the edge goes round it
END_R = 4                                         # the two end marks' radius
ITEMS = os.path.join(ROOT, 'work', 'app', 'repentance-resources', 'data', 'gfx',
                     'items', 'collectibles')
# touchedit.c draws these three charged (ROW_FIRST): Ring0 the die, Ring1 the Bible, Ring2 the bag
RING_ITEMS = ['collectibles_105_dice.png', 'collectibles_033_thebible.png',
              'collectibles_710_bagofcrafting.png']


def edged(mask):
    """the editor's manner: the light ink, a 1-px black edge all round (8 neighbours)"""
    pad = np.pad(mask, 1)
    edge = np.zeros_like(pad)
    for dy in (-1, 0, 1):
        for dx in (-1, 0, 1):
            edge |= np.roll(np.roll(pad, dy, 0), dx, 1)
    out = np.zeros(pad.shape + (4,), np.uint8)
    out[edge] = E.EDGE
    out[pad] = E.LIGHT
    return out


def circle(r, filled):
    n = 2 * r + 1
    yy, xx = np.mgrid[0:n, 0:n] - r
    d = np.sqrt(xx * xx + yy * yy)
    return (d <= r + 0.3) if filled else ((d <= r + 0.3) & (d >= r - 1.2))


def slider_parts():
    """(name, picture, pivot) - each pivot is the point touchedit.c draws it at"""
    parts = []
    track = edged(np.ones((1, SLIDER_TRACK), bool))                  # (T + 2) x 3
    parts.append(('SliderTrack', Image.fromarray(track), (1, 1)))   # the first light texel
    thick = edged(np.ones((3, 3), bool))
    parts.append(('SliderFill', Image.fromarray(thick[:, 2:3].copy()), (0, 2)))   # E L L L E
    cap = np.zeros((5, 1, 4), np.uint8)
    cap[:] = E.EDGE
    parts.append(('SliderFillCap', Image.fromarray(cap), (1, 2)))    # the column left of the fill
    k = np.ones((KNOB_H, KNOB_W), bool)
    for (y, x) in ((0, 0), (0, KNOB_W - 1), (KNOB_H - 1, 0), (KNOB_H - 1, KNOB_W - 1)):
        k[y, x] = False
    knob = edged(k)
    knob[1 + 4:1 + KNOB_H - 4, 1 + 2] = E.EDGE                       # the two grip lines
    knob[1 + 4:1 + KNOB_H - 4, 1 + KNOB_W - 3] = E.EDGE
    parts.append(('SliderKnob', Image.fromarray(knob), ((KNOB_W + 2) // 2, (KNOB_H + 2) // 2)))
    for name, filled in (('SliderLow', False), ('SliderHigh', True)):
        parts.append((name, Image.fromarray(edged(circle(END_R, filled))), (END_R + 1, END_R + 1)))
    return parts


def ring(leaf):
    """The white outline the game paints round a charged active, outside the picture only.

    The game draws the item at scale 2 into its 72 x 64 target and the outline as
    four white silhouettes one TARGET pixel off each way (0x85fcd0..0x85fdf8), so
    this is built on the doubled picture: those four, where the picture itself is
    clear. Its middle is the picture's: 33,33 of 66."""
    a = np.asarray(Image.open(os.path.join(ITEMS, leaf)).convert('RGBA'))[:32, :32, 3]
    a = np.kron(a, np.ones((2, 2), np.uint8))
    have = np.zeros((66, 66), bool)
    for dx, dy in ((-1, 0), (1, 0), (0, -1), (0, 1)):
        have[1 + dy:65 + dy, 1 + dx:65 + dx] |= a > 0
    have[1:65, 1:65] &= a == 0
    out = np.zeros((66, 66, 4), np.uint8)
    out[have] = (255, 255, 255, 255)
    return Image.fromarray(out)


def shelves(pictures, width):
    """packs (name, picture, pivot) in rows: [(name, x, y, picture, pivot)] and the height"""
    placed, x, y, row_h = [], 0, 0, 0
    for name, im, pivot in pictures:
        if x + im.width > width:
            x, y, row_h = 0, y + row_h + 1, 0
        placed.append((name, x, y, im, pivot))
        x += im.width + 1
        row_h = max(row_h, im.height)
    return placed, y + row_h


def main():
    fonts = {'default': Font(os.path.join(ASSETS, 'eid_default.fnt')),
             'kr': Font(os.path.join(ASSETS, 'eid_korean_yoonwebdotum10_shadow.fnt'))}
    cells, steps = [], []
    for lang, words in SETS:
        f = fonts['kr' if lang == 'kr' else 'default']
        wds = [outlined(E.word(f, w)) if lang == 'kr' else E.word(f, w) for w in words]
        step = max(STEP_MIN, max(w.width for w in wds) + GAP)
        steps.append(step)
        row = []
        for anim, wd in zip(ANIMATIONS, wds):
            ic = E.icon(ICON_OF[anim], 1)
            c = Image.new('RGBA', (step, CELL_H), (0, 0, 0, 0))
            top = (CELL_H - (ic.height + 2 + wd.height)) // 2
            c.alpha_composite(ic, ((step - ic.width) // 2, top))
            c.alpha_composite(wd, ((step - wd.width) // 2, top + ic.height + 2))
            row.append(c)
        cells.append(row)

    # the slider's word per set and its value every five per cent, in the faces
    # the buttons' words use; each pinned by the middle of its left side
    words = []
    for k, (lang, _) in enumerate(SETS):
        f = fonts['kr' if lang == 'kr' else 'default']
        im = outlined(E.word(f, OPACITY[lang])) if lang == 'kr' else E.word(f, OPACITY[lang])
        words.append(('Opacity%d' % k, im))
    values = [('Pct%d' % v, E.word(fonts['default'], '%d%%' % v))
              for v in range(PCT_LOW, PCT_HIGH + 1, PCT_STEP)]
    label_h = max(im.height for _, im in words + values)
    extras = [(n, im, (0, im.height // 2)) for n, im in words + values]   # the middle of the line
    extras += slider_parts()
    extras += [('Ring%d' % i, ring(leaf), (33, 33)) for i, leaf in enumerate(RING_ITEMS)]

    cell_w = max(steps)
    width = cell_w * len(ANIMATIONS)
    top = CELL_H * len(SETS)
    packed, extra_h = shelves(extras, width)
    sheet = Image.new('RGBA', (width, top + 1 + extra_h), (0, 0, 0, 0))
    anims = []
    text = open(ANM2, encoding='utf-8').read()
    m = re.search(r'\t*<Animation Name="Grid0?".*?</Animation>\n', text, re.S)      # Grid0 on a rerun
    assert m, 'no Grid animation to copy'
    template = m.group(0)
    for k, row in enumerate(cells):
        for i, c in enumerate(row):
            x, y = i * cell_w + (cell_w - c.width) // 2, k * CELL_H
            sheet.alpha_composite(c, (x, y))
            anims.append(animation('%s%d' % (ANIMATIONS[i], k), x, y, c.width, c.height, template))
    for name, x, y, im, pivot in packed:
        sheet.alpha_composite(im, (x, top + 1 + y))
        anims.append(animation(name, x, top + 1 + y, im.width, im.height, template, pivot))

    keep(SHEET, 'slider')
    keep(ANM2, 'slider')
    sheet.save(SHEET)

    # the old ones (and any earlier generation) out, the new ones in where they stood
    names = '|'.join(ANIMATIONS + ['Slider[A-Za-z]*', 'Opacity', 'Pct', 'Ring'])
    pat = re.compile(r'\t*<Animation Name="(?:%s)\d*".*?</Animation>\n' % names, re.S)
    first = pat.search(text).start()
    text = pat.sub('', text)
    text = text[:first] + ''.join(anims) + text[first:]
    open(ANM2, 'w', encoding='utf-8', newline='').write(text)

    with open(HEADER, 'w', encoding='utf-8', newline='\n') as h:
        h.write('/* Written by tools/editor_tools_sheet.py - do not edit by hand. */\n')
        h.write('#ifndef TOUCHEDIT_TOOLS_H\n#define TOUCHEDIT_TOOLS_H\n\n')
        h.write('/* one per language set (tw_set_for_language): %s */\n' % ' '.join(l for l, _ in SETS))
        h.write('#define TOOL_SETS %d\n' % len(SETS))
        h.write('#define TOOL_HIGH %d.0f\n' % CELL_H)
        h.write('static const float TOOL_STEP_OF[TOOL_SETS] = { %s };\n\n'
                % ', '.join('%d.0f' % s for s in steps))
        h.write('/* the opacity slider, in texels (the editor draws them at 2 f: a texel a unit) */\n')
        h.write('#define SLIDER_TRACK %d.0f\n' % SLIDER_TRACK)
        h.write('#define SLIDER_KNOB_H %d.0f\n' % (KNOB_H + 2))
        h.write('#define SLIDER_END_R %d.0f\n' % (END_R + 1))
        h.write('#define SLIDER_LABEL_H %d.0f\n' % label_h)
        h.write('#define SLIDER_PCT_LOW %d\n#define SLIDER_PCT_HIGH %d\n#define SLIDER_PCT_STEP %d\n'
                % (PCT_LOW, PCT_HIGH, PCT_STEP))
        h.write('static const float OPACITY_W[TOOL_SETS] = { %s };\n'
                % ', '.join('%d.0f' % im.width for _, im in words))
        h.write('static const float PCT_W[%d] = { %s };\n\n#endif\n'
                % (len(values), ', '.join('%d.0f' % im.width for _, im in values)))
    print('  steps', dict(zip([l for l, _ in SETS], steps)), ' sheet', sheet.size, ' animations', len(anims),
          ' label', label_h, ' words', [im.width for _, im in words])


if __name__ == '__main__':
    main()
